#include "fmsui/app.h"

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

}  // namespace

FmsApp &FmsApp::instance() {
    static FmsApp app;
    return app;
}

void FmsApp::init(lv_display_t *display, WidgetBuilder builder) {
    display_ = display;
    builder_ = std::move(builder);

    screen_ = Size{static_cast<float>(lv_display_get_horizontal_resolution(display)),
                   static_cast<float>(lv_display_get_vertical_resolution(display))};

    /* The active screen is our paint root.  Strip the default theme off it so
     * nothing draws that we did not ask for, and turn off scrolling -- the
     * framework positions everything absolutely. */
    lv_root_ = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(lv_root_);
    lv_obj_set_style_bg_color(lv_root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_root_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(lv_root_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(lv_root_, LV_OBJ_FLAG_SCROLLABLE);

    owner_.scheduleBuild();

    /* Run ahead of LVGL's own refresh timer.  It early-outs when nothing is
     * dirty, so a static screen costs one predicate per tick. */
    timer_ = lv_timer_create(timerCb, 10, this);
    lv_timer_set_repeat_count(timer_, -1);
}

FrameStats FmsApp::stats() const {
    const std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void FmsApp::shutdown() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }

    /* The element tree owns the render objects, and a RenderLv owns its lv_obj.
     * So this is what takes the users of the shared styles off the screen, and
     * it has to finish before the styles they point at go away. */
    if (root_ != nullptr) {
        root_->unmount();
        delete root_;
        root_ = nullptr;
    }
    releaseStyleCache();

    builder_ = nullptr;
    display_ = nullptr;
    lv_root_ = nullptr;
}

void FmsApp::setBackground(Color c) {
    if (lv_root_ == nullptr) return;
    lv_obj_set_style_bg_color(lv_root_, lv_color_make(c.r, c.g, c.b), 0);
    lv_obj_set_style_bg_opa(lv_root_, c.a, 0);
}

void FmsApp::timerCb(lv_timer_t *t) { static_cast<FmsApp *>(lv_timer_get_user_data(t))->frame(); }

void FmsApp::frame() {
    /* Whichever thread runs the frame loop owns the tree. Recorded here rather
     * than in init(), because on the device init() runs from app_main while this
     * runs from the task esp_lvgl_port created -- they are not the same thread,
     * and it is this one that matters. */
    if (!owner_.bound()) owner_.bindToCurrentThread();

    if (!owner_.takeNeedsBuild()) return;

    const auto now = [this] { return clock_ != nullptr ? clock_() : lv_tick_get() * 1000U; };
    const uint32_t t0 = now();

    /* 1. Rebuild. The previous pass's widgets stay intact in the other arena
     *    until the pass after this one, so anything still holding them is safe. */
    arenas_.beginBuild();
    Widget *tree = new ViewWidget{{.screen = screen_, .child = builder_()}};

    /* 2. Reconcile. */
    if (root_ == nullptr) {
        root_ = tree->createElement();
        root_->mount(nullptr, &owner_);
    } else {
        root_->update(tree);
    }

    auto *view = static_cast<RenderView *>(root_->renderObject());
    const uint32_t t_built = now();

    /* 3. Lay out. */
    view->layout(BoxConstraints::tight(screen_));
    const uint32_t t_laid_out = now();

    /* 4. Push to LVGL. Only values that moved are written. */
    PaintContext ctx;
    ctx.root = lv_root_;
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
    f.builds = ++builds_;
    f.widgets = static_cast<uint32_t>(arenas_.current().objectCount());
    f.lv_objects = static_cast<uint32_t>(ctx.next_index);
    f.lv_created = ctx.created;
    f.lv_moved = ctx.moved;
    f.lv_retexted = ctx.retexted;
    f.arena_bytes = static_cast<uint32_t>(arenas_.current().highWaterMark());
    f.build_us = t_built - t0;
    f.layout_us = t_laid_out - t_built;
    f.paint_us = t_painted - t_laid_out;
    f.total_us = t_painted - t0;

    const std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = f;
}

void runApp(WidgetBuilder builder) {
    FmsApp::instance().init(lv_display_get_default(), std::move(builder));
}

}  // namespace fmsui
