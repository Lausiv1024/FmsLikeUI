#include "fmsui/app.h"

#include <mutex>

#include "fmsui/arena.h"
#include "fmsui/element.h"
#include "fmsui/render.h"
#include "fmsui/widgets.h"

namespace fmsui {
namespace {

/* The root of every tree. Not exported: the app puts it there. */
struct ViewArgs {
    Size screen{};
    Widget *child = nullptr;
};

class ViewWidget : public SingleChildRenderObjectWidget {
public:
    explicit ViewWidget(ViewArgs a) : args_(a) { child = a.child; }
    FMSUI_WIDGET(ViewWidget)

    RenderObject *createRenderObject() const override {
        auto *r = new RenderView();
        r->screen = args_.screen;
        return r;
    }
    void updateRenderObject(RenderObject *ro) const override {
        static_cast<RenderView *>(ro)->screen = args_.screen;
    }

private:
    ViewArgs args_;
};

/* Everything the app runs on.
 *
 * These were FmsApp's own members once, which put BuildArenas, BuildOwner and
 * Element into app.h and so in front of every application. There is only ever
 * one app, so the state is simply the library's: one object with static
 * storage, and nothing allocated to hide it behind a pointer. */
struct AppState {
    BuildArenas arenas;
    BuildOwner owner;
    WidgetBuilder builder;
    Element *root = nullptr;
    lv_obj_t *lv_root = nullptr;
    lv_display_t *display = nullptr;
    lv_timer_t *timer = nullptr;
    Size screen{};
    FmsApp::MicrosClock clock = nullptr;

    /* The running build count belongs to the frame loop alone -- it is not
     * incremented under the lock, only copied into the frame that carries it. */
    uint32_t builds = 0;

    /* Guards nothing but the copy below. No build, layout, paint or logging
     * happens inside it. */
    std::mutex stats_mutex;
    FrameStats stats{};
};

/* Function-local, like FmsApp::instance(): made on first use, and safe for that
 * first use to come from any thread -- a producer's requestFrame() may be it. */
AppState &state() {
    static AppState s;
    return s;
}

void frame(AppState &s) {
    /* Whichever thread runs the frame loop owns the tree. Recorded here rather
     * than in init(), because on the device init() runs from app_main while this
     * runs from the task esp_lvgl_port created -- they are not the same thread,
     * and it is this one that matters. */
    if (!s.owner.bound()) s.owner.bindToCurrentThread();

    if (!s.owner.takeNeedsBuild()) return;

    const auto now = [&s] { return s.clock != nullptr ? s.clock() : lv_tick_get() * 1000U; };
    const uint32_t t0 = now();

    /* 1. Rebuild. The previous pass's widgets stay intact in the other arena
     *    until the pass after this one, so anything still holding them is safe. */
    s.arenas.beginBuild();
    Widget *tree = new ViewWidget{{.screen = s.screen, .child = s.builder()}};

    /* 2. Reconcile. */
    if (s.root == nullptr) {
        s.root = tree->createElement();
        s.root->mount(nullptr, &s.owner);
    } else {
        s.root->update(tree);
    }

    auto *view = static_cast<RenderView *>(s.root->renderObject());
    const uint32_t t_built = now();

    /* 3. Lay out. */
    view->layout(BoxConstraints::tight(s.screen));
    const uint32_t t_laid_out = now();

    /* 4. Push to LVGL. Only values that moved are written. */
    PaintContext ctx;
    ctx.root = s.lv_root;
    ctx.next_index = 0;
    view->paint(ctx, Offset{0, 0});

    /* Layers deferred by the main pass -- dropdown popups and the like -- get
     * painted now, so their lv_obj indices land above everything else. Index,
     * not iterator: a layer may defer another one. */
    ctx.overlay_pass = true;
    for (size_t i = 0; i < ctx.deferred.size(); i++) {
        auto [layer, origin] = ctx.deferred[i];
        layer->paint(ctx, origin);
    }

    const uint32_t t_painted = now();

    /* 5. Publish. Assembled locally first: a reader must never get a struct that
     *    is half this frame and half the last one. */
    FrameStats f;
    f.builds = ++s.builds;
    f.widgets = static_cast<uint32_t>(s.arenas.current().objectCount());
    f.lv_objects = static_cast<uint32_t>(ctx.next_index);
    f.lv_created = ctx.created;
    f.lv_moved = ctx.moved;
    f.lv_retexted = ctx.retexted;
    f.arena_bytes = static_cast<uint32_t>(s.arenas.current().highWaterMark());
    f.build_us = t_built - t0;
    f.layout_us = t_laid_out - t_built;
    f.paint_us = t_painted - t_laid_out;
    f.total_us = t_painted - t0;

    const std::lock_guard<std::mutex> lock(s.stats_mutex);
    s.stats = f;
}

void timerCb(lv_timer_t *t) { frame(*static_cast<AppState *>(lv_timer_get_user_data(t))); }

}  // namespace

FmsApp &FmsApp::instance() {
    static FmsApp app;
    return app;
}

void FmsApp::init(lv_display_t *display, WidgetBuilder builder) {
    AppState &s = state();
    s.display = display;
    s.builder = std::move(builder);

    s.screen = Size{static_cast<float>(lv_display_get_horizontal_resolution(display)),
                    static_cast<float>(lv_display_get_vertical_resolution(display))};

    /* The active screen is our paint root.  Strip the default theme off it so
     * nothing draws that we did not ask for, and turn off scrolling -- the
     * framework positions everything absolutely. */
    s.lv_root = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(s.lv_root);
    lv_obj_set_style_bg_color(s.lv_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.lv_root, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(s.lv_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s.lv_root, LV_OBJ_FLAG_SCROLLABLE);

    s.owner.scheduleBuild();

    /* Run ahead of LVGL's own refresh timer.  It early-outs when nothing is
     * dirty, so a static screen costs one predicate per tick. */
    s.timer = lv_timer_create(timerCb, 10, &s);
    lv_timer_set_repeat_count(s.timer, -1);
}

void FmsApp::requestFrame() { state().owner.scheduleBuild(); }

FrameRequester FmsApp::requester() { return FrameRequester(state().owner.dirtyFlag()); }

Size FmsApp::screenSize() const { return state().screen; }

FrameStats FmsApp::stats() const {
    AppState &s = state();
    const std::lock_guard<std::mutex> lock(s.stats_mutex);
    return s.stats;
}

void FmsApp::shutdown() {
    AppState &s = state();

    if (s.timer != nullptr) {
        lv_timer_delete(s.timer);
        s.timer = nullptr;
    }

    /* The element tree owns the render objects, and a RenderLv owns its lv_obj.
     * So this is what takes the users of the shared styles off the screen, and
     * it has to finish before the styles they point at go away. */
    if (s.root != nullptr) {
        s.root->unmount();
        delete s.root;
        s.root = nullptr;
    }

    /* Then the widgets the last two builds left behind, and the arenas' memory.
     * Not before the tree: unmounting runs State::dispose(), and a State may
     * read its widget() there. */
    s.arenas.release();

    releaseStyleCache();

    s.builder = nullptr;
    s.display = nullptr;
    s.lv_root = nullptr;
}

void FmsApp::setBackground(Color c) {
    AppState &s = state();
    if (s.lv_root == nullptr) return;
    lv_obj_set_style_bg_color(s.lv_root, lv_color_make(c.r, c.g, c.b), 0);
    lv_obj_set_style_bg_opa(s.lv_root, c.a, 0);
}

void FmsApp::setClock(MicrosClock clock) { state().clock = clock; }

void FmsApp::setThreadId(ThreadIdFn fn) { BuildOwner::setThreadIdFn(fn); }

void runApp(WidgetBuilder builder) {
    FmsApp::instance().init(lv_display_get_default(), std::move(builder));
}

}  // namespace fmsui
