#include "fmsui/app.h"

#include <mutex>
#include <utility>

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
    enum class DisplayPhase : uint8_t { Awake, Blanking, Blanked, Waking };
    enum class RefreshWait : uint8_t { None, Blank, Wake };

    BuildArenas arenas;
    BuildOwner owner;
    WidgetBuilder builder;
    Element *root = nullptr;
    lv_obj_t *lv_root = nullptr;
    lv_display_t *display = nullptr;
    lv_timer_t *timer = nullptr;
    Size screen{};
    FmsApp::MicrosClock clock = nullptr;
    uint32_t idle_timeout_ms = 0;
    VoidCallback output_off;
    VoidCallback output_on;
    DisplayPhase display_phase = DisplayPhase::Awake;
    RefreshWait refresh_wait = RefreshWait::None;
    bool refresh_started = false;
    bool swallow_release = false;
    lv_obj_t *blank_overlay = nullptr;

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

void destroyBlankOverlay(AppState &s) {
    if (s.blank_overlay == nullptr) return;
    lv_obj_delete(s.blank_overlay);
    s.blank_overlay = nullptr;
}

void overlayEventCb(lv_event_t *e);

bool createBlankOverlay(AppState &s) {
    if (s.lv_root == nullptr) return false;

    lv_obj_t *overlay = lv_obj_create(s.lv_root);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, static_cast<int32_t>(s.screen.width),
                    static_cast<int32_t>(s.screen.height));
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay, overlayEventCb, LV_EVENT_ALL, &s);

    /* The framework's normal paint and deferred layers are all direct children
     * of the active screen.  Appending this child therefore puts it above a
     * popup layer as well as above ordinary content. */
    lv_obj_move_to_index(overlay, lv_obj_get_child_count(s.lv_root));
    s.blank_overlay = overlay;
    return true;
}

void beginBlank(AppState &s) {
    if (s.display_phase != AppState::DisplayPhase::Awake || s.display == nullptr ||
        s.blank_overlay != nullptr) {
        return;
    }
    if (!createBlankOverlay(s)) return;

    s.display_phase = AppState::DisplayPhase::Blanking;
    s.refresh_wait = AppState::RefreshWait::Blank;
    s.refresh_started = false;
    /* Explicitly invalidate the complete screen.  Creation/style changes
     * already invalidate the overlay, but this makes the required black-frame
     * refresh independent of LVGL's object invalidation optimisations. */
    lv_obj_invalidate(s.lv_root);
}

void beginWake(AppState &s) {
    if (s.display_phase != AppState::DisplayPhase::Blanked ||
        s.blank_overlay == nullptr) {
        return;
    }

    /* An explicit wake is activity too.  Without resetting LVGL's inactivity
     * clock, a display woken after the idle deadline would enter Blanking
     * again on the first Awake frame. */
    lv_display_trigger_activity(s.display);

    /* Keep output disabled while the old framebuffer is replaced.  The
     * transparent, still-clickable overlay remains until a held wake tap is
     * released, so its release cannot become a click on the old content. */
    lv_obj_set_style_bg_opa(s.blank_overlay, LV_OPA_TRANSP, 0);
    s.display_phase = AppState::DisplayPhase::Waking;
    s.refresh_wait = AppState::RefreshWait::Wake;
    s.refresh_started = false;
    lv_obj_invalidate(s.lv_root);
}

void displayEventCb(lv_event_t *e) {
    auto *s = static_cast<AppState *>(lv_event_get_user_data(e));
    if (s == nullptr || lv_event_get_target(e) != s->display) return;

    switch (lv_event_get_code(e)) {
        case LV_EVENT_REFR_START:
            /* REFR_READY is also sent when no area was dirty.  Only consume a
             * READY after the START belonging to the invalidation requested by
             * blankDisplay()/wakeDisplay(). */
            if (s->refresh_wait != AppState::RefreshWait::None) {
                s->refresh_started = true;
            }
            break;

        case LV_EVENT_REFR_READY:
            if (!s->refresh_started) break;

            {
                const AppState::RefreshWait completed = s->refresh_wait;
                s->refresh_wait = AppState::RefreshWait::None;
                s->refresh_started = false;

                if (completed == AppState::RefreshWait::Blank &&
                    s->display_phase == AppState::DisplayPhase::Blanking) {
                    s->display_phase = AppState::DisplayPhase::Blanked;
                    if (s->output_off) s->output_off();
                } else if (completed == AppState::RefreshWait::Wake &&
                           s->display_phase == AppState::DisplayPhase::Waking) {
                    s->display_phase = AppState::DisplayPhase::Awake;
                    if (s->output_on) s->output_on();
                    if (s->display_phase == AppState::DisplayPhase::Awake &&
                        !s->swallow_release) {
                        destroyBlankOverlay(*s);
                    }
                }
            }
            break;

        default:
            break;
    }
}

void overlayEventCb(lv_event_t *e) {
    auto *s = static_cast<AppState *>(lv_event_get_user_data(e));
    if (s == nullptr) return;

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED &&
        s->display_phase == AppState::DisplayPhase::Blanked) {
        s->swallow_release = true;
        lv_indev_wait_release(lv_event_get_indev(e));
        beginWake(*s);
    } else if ((code == LV_EVENT_PRESS_LOST || code == LV_EVENT_RELEASED) &&
               s->swallow_release) {
        s->swallow_release = false;
        if (s->display_phase == AppState::DisplayPhase::Awake &&
            s->refresh_wait == AppState::RefreshWait::None) {
            destroyBlankOverlay(*s);
        }
    }

    /* The overlay is a full-screen input barrier. */
    if (code < LV_EVENT_DRAW_MAIN_BEGIN) lv_event_stop_bubbling(e);
}

void frame(AppState &s) {
    /* Whichever thread runs the frame loop owns the tree. Recorded here rather
     * than in init(), because on the device init() runs from app_main while this
     * runs from the task esp_lvgl_port created -- they are not the same thread,
     * and it is this one that matters. */
    if (!s.owner.bound()) s.owner.bindToCurrentThread();

    if (s.display_phase == AppState::DisplayPhase::Awake &&
        s.idle_timeout_ms != 0 && s.display != nullptr &&
        lv_display_get_inactive_time(s.display) >= s.idle_timeout_ms) {
        beginBlank(s);
    }

    /* Do not consume a rebuild request while the output is stably blank or
     * while the black frame is being committed.  It remains pending and is
     * consumed during Waking, so the first visible frame uses the latest data. */
    if (s.display_phase == AppState::DisplayPhase::Blanking ||
        s.display_phase == AppState::DisplayPhase::Blanked) {
        return;
    }

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
    if (s.display != nullptr || s.timer != nullptr || s.root != nullptr) shutdown();
    if (display == nullptr) return;

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

    s.display_phase = AppState::DisplayPhase::Awake;
    s.refresh_wait = AppState::RefreshWait::None;
    s.refresh_started = false;
    s.swallow_release = false;
    s.blank_overlay = nullptr;
    lv_display_add_event_cb(display, displayEventCb, LV_EVENT_REFR_START, &s);
    lv_display_add_event_cb(display, displayEventCb, LV_EVENT_REFR_READY, &s);

    s.owner.scheduleBuild();

    /* Run ahead of LVGL's own refresh timer.  It early-outs when nothing is
     * dirty, so a static screen costs one predicate per tick. */
    s.timer = lv_timer_create(timerCb, 10, &s);
    lv_timer_set_repeat_count(s.timer, -1);
}

void FmsApp::requestFrame() { state().owner.scheduleBuild(); }

void FmsApp::setIdleTimeout(uint32_t timeout_ms) { state().idle_timeout_ms = timeout_ms; }

uint32_t FmsApp::idleTimeout() const { return state().idle_timeout_ms; }

void FmsApp::setOutputOffCallback(VoidCallback callback) {
    state().output_off = std::move(callback);
}

void FmsApp::setOutputOnCallback(VoidCallback callback) {
    state().output_on = std::move(callback);
}

void FmsApp::blankDisplay() { beginBlank(state()); }

void FmsApp::wakeDisplay() { beginWake(state()); }

bool FmsApp::isDisplayBlanked() const {
    return state().display_phase != AppState::DisplayPhase::Awake;
}

void FmsApp::notifyUserActivity() {
    AppState &s = state();
    if (s.display != nullptr) lv_display_trigger_activity(s.display);
}

FrameRequester FmsApp::requester() { return FrameRequester(state().owner.dirtyFlag()); }

Size FmsApp::screenSize() const { return state().screen; }

FrameStats FmsApp::stats() const {
    AppState &s = state();
    const std::lock_guard<std::mutex> lock(s.stats_mutex);
    return s.stats;
}

void FmsApp::shutdown() {
    AppState &s = state();

    const bool output_is_off =
        s.display_phase == AppState::DisplayPhase::Blanked ||
        s.display_phase == AppState::DisplayPhase::Waking;

    if (s.timer != nullptr) {
        lv_timer_delete(s.timer);
        s.timer = nullptr;
    }

    if (s.display != nullptr) {
        lv_display_remove_event_cb_with_user_data(s.display, displayEventCb, &s);
    }

    destroyBlankOverlay(s);
    if (output_is_off && s.output_on) s.output_on();
    s.display_phase = AppState::DisplayPhase::Awake;
    s.refresh_wait = AppState::RefreshWait::None;
    s.refresh_started = false;
    s.swallow_release = false;

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
