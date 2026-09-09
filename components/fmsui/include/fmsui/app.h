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

#include <atomic>
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
     * device a move costs about 0.46ms, so for a reorder this number -- not the
     * widget count, not even lv_objects -- is what says whether the frame fits.
     *
     * It is not the whole story for every frame, though. Rewriting a label's
     * text costs about 0.14ms on the Tab5 and is counted nowhere here, so a
     * frame can read 0 created and 0 moved and still take 9ms: that is exactly
     * what stepping an FmsWindow does, changing 56 strings and nothing else.
     * See docs/PERF.md; a low lv_moved means a reorder was cheap, not that the
     * frame was. */
    uint32_t lv_moved = 0;
    uint32_t arena_bytes = 0;  /* arena high-water mark */

    /* The pass, broken into its three phases -- guessing which one costs is how
     * you optimise the wrong thing. */
    uint32_t build_us = 0;   /* widget build + reconcile */
    uint32_t layout_us = 0;  /* constraints down, sizes up */
    uint32_t paint_us = 0;   /* pushing it all into LVGL */
    uint32_t total_us = 0;
};

/* A frame request stripped down to a pointer to one atomic flag.
 *
 * FmsApp::instance() is a function, and on the device that function lives in
 * flash -- so an ESP-IDF interrupt handler registered with ESP_INTR_FLAG_IRAM
 * cannot call FmsApp::instance().requestFrame(): the flash cache may be turned
 * off while it runs. Take one of these during setup, keep it somewhere your
 * handler can reach, and request() is a single inlined release store with
 * nothing in flash, no allocation and no locking.
 *
 * That said, prefer the ordinary ESP-IDF shape -- have the ISR notify a task
 * and call requestFrame() from there. An ISR that only sets a flag the UI reads
 * next frame has bought itself nothing, because the data it received still has
 * to get somewhere the build can see it. */
class FrameRequester {
public:
    FrameRequester() = default;

    void request() const {
        if (flag_ != nullptr) flag_->store(true, std::memory_order_release);
    }
    bool valid() const { return flag_ != nullptr; }

private:
    friend class FmsApp;
    explicit FrameRequester(std::atomic<bool> *flag) : flag_(flag) {}
    std::atomic<bool> *flag_ = nullptr;
};

class FmsApp {
public:
    static FmsApp &instance();

    /* `builder` is called on every rebuild and must return a fresh widget tree.
     * It runs inside a build pass, so `new` inside it hits the arena. */
    void init(lv_display_t *display, WidgetBuilder builder);

    /* Force a rebuild on the next frame. setState() does this for you.
     *
     * This is the only entry point that may be called from another task, and it
     * is the whole cross-task story: publish what changed somewhere the build
     * can read it, then call this. It is one release store -- no allocation, no
     * locking, and it cannot block, so a sensor task at 100Hz can call it on
     * every sample without ever waiting on the UI.
     *
     *     altitude_.store(v, std::memory_order_relaxed);   // yours to own
     *     FmsApp::instance().requestFrame();               // then this
     *
     * Order matters: publish first, request second. Requesting a frame does not
     * queue the value, it only says "read your inputs again", so the UI shows
     * the latest reading rather than replaying every one of them -- which is
     * what you want from a stream of sensor, CAN or network updates.
     *
     * What you must NOT do is reach into a State from another task. setState()
     * runs your mutation immediately, on the calling task, racing the build. */
    void requestFrame() { owner_.scheduleBuild(); }

    /* A requester that can be poked from an IRAM interrupt handler. Fetch it
     * during setup; see FrameRequester. */
    FrameRequester requester() { return FrameRequester(owner_.dirtyFlag()); }

    Size screenSize() const { return screen_; }
    const FrameStats &stats() const { return stats_; }

    void setBackground(Color c);

    /* LVGL's tick is only millisecond-resolution, which rounds a whole build
     * pass down to zero.  Install the platform's real microsecond clock
     * (esp_timer_get_time on the device, steady_clock in the simulator) so the
     * M5 profiling numbers mean something. */
    using MicrosClock = uint32_t (*)();
    void setClock(MicrosClock clock) { clock_ = clock; }

    /* How to ask which thread is running, so that setState() from the wrong one
     * can be caught rather than corrupting the arena in the background. Install
     * it before the first frame; see ThreadId in element.h for why the framework
     * cannot work this out for itself. Without it the check is off and nothing
     * else changes. */
    void setThreadId(ThreadIdFn fn) { BuildOwner::setThreadIdFn(fn); }

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
