#pragma once

/* The frame loop.
 *
 * On any frame where something asked for a rebuild:
 *
 *   1. swap the build arena and rebuild the whole widget tree
 *   2. reconcile it against the element tree (State and RenderObjects survive)
 *   3. lay out from the root with the screen as a tight constraint
 *   4. walk the render tree and push only what changed into LVGL
 *
 * LVGL then does its own dirty-area redraw, so a full rebuild does not imply a
 * full repaint.
 */

#include <cstddef>
#include <functional>

#include "lvgl.h"

#include "fmsui/element.h"
#include "fmsui/foundation.h"
#include "fmsui/widget.h"

namespace fmsui {

using WidgetBuilder = std::function<Widget *()>;

struct FrameStats {
    uint32_t builds = 0;       /* rebuild passes since start */
    uint32_t widgets = 0;      /* widgets created in the last pass */
    uint32_t lv_objects = 0;   /* lv_objs the last paint touched */
    uint32_t lv_created = 0;   /* ...of which had to be created */
    /* ...and of which had to be told they had moved in the z-order.  On the
     * device this is what a reorder costs: measured on the Tab5, paint is
     * 2.3ms + 0.46ms per moved object, so this number -- not the widget count,
     * not even lv_objects -- is what says whether a frame will fit. */
    uint32_t lv_moved = 0;
    uint32_t arena_bytes = 0;  /* arena high-water mark */

    /* The pass, broken into its three phases -- guessing which one costs is how
     * you optimise the wrong thing. */
    uint32_t build_us = 0;   /* widget build + reconcile */
    uint32_t layout_us = 0;  /* constraints down, sizes up */
    uint32_t paint_us = 0;   /* pushing it all into LVGL */
    uint32_t total_us = 0;
};

class FmsApp {
public:
    static FmsApp &instance();

    /* `builder` is called on every rebuild and must return a fresh widget tree.
     * It runs inside a build pass, so `new` inside it hits the arena. */
    void init(lv_display_t *display, WidgetBuilder builder);

    /* Force a rebuild on the next frame. setState() does this for you. */
    void requestFrame() { owner_.scheduleBuild(); }

    Size screenSize() const { return screen_; }
    const FrameStats &stats() const { return stats_; }

    void setBackground(Color c);

    /* LVGL's tick is only millisecond-resolution, which rounds a whole build
     * pass down to zero.  Install the platform's real microsecond clock
     * (esp_timer_get_time on the device, steady_clock in the simulator) so the
     * M5 profiling numbers mean something. */
    using MicrosClock = uint32_t (*)();
    void setClock(MicrosClock clock) { clock_ = clock; }

private:
    void frame();
    static void timerCb(lv_timer_t *t);

    BuildArenas arenas_;
    BuildOwner owner_;
    WidgetBuilder builder_;
    Element *root_ = nullptr;
    lv_obj_t *lv_root_ = nullptr;
    lv_display_t *display_ = nullptr;
    Size screen_{};
    FrameStats stats_{};
    MicrosClock clock_ = nullptr;
};

/* Convenience: FmsApp::instance().init(lv_display_get_default(), builder). */
void runApp(WidgetBuilder builder);

}  // namespace fmsui
