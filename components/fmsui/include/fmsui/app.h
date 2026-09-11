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
#include <cstdint>
#include <functional>

#include "lvgl.h"

#include "fmsui/foundation.h"
#include "fmsui/widget.h"

namespace fmsui {

using WidgetBuilder = std::function<Widget *()>;

/* One frame's numbers, and only ever one frame's.
 *
 * This is handed out by value, not by reference, because the frame loop and
 * whoever logs it are different tasks. A reference would let a reader walk the
 * fields while the loop is part-way through writing the next frame into them,
 * and come away with a build time from one frame and a paint time from another
 * -- a reading that looks plausible and describes no frame that ever ran.
 *
 * Making each field a separate atomic would not fix that. Every individual read
 * would be safe and the set would still be a mixture. What has to be atomic is
 * the whole struct, so the loop assembles a frame locally and publishes it in
 * one go, and stats() copies it out the same way. */
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
     * text costs about 0.18ms on the Tab5, and a frame can read 0 created and
     * 0 moved and still take 9ms: that is exactly what stepping an FmsWindow
     * does. See docs/PERF.md; a low lv_moved means a reorder was cheap, not
     * that the frame was. */
    uint32_t lv_moved = 0;

    /* ...and of which had their text rewritten, at about 0.18ms each on the
     * Tab5. This is the one that says what a window step cost, the way lv_moved
     * is the one that says what a reorder cost. */
    uint32_t lv_retexted = 0;
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

/* Whoever is running right now, as a number the framework only ever compares.
 *
 * `std::this_thread::get_id()` is what this used to be, and it cannot be used
 * here: on ESP-IDF it goes through pthread_self(), which asserts outright when
 * it is called from a FreeRTOS task that was not created as a pthread -- and the
 * task esp_lvgl_port creates to run the frame loop is exactly that. The board
 * rebooted on the first frame.
 *
 * So the platform supplies the identity, the way it already supplies the
 * microsecond clock. Install one with FmsApp::setThreadId() before the first
 * frame:
 *
 *     device:   (ThreadId)xTaskGetCurrentTaskHandle()
 *     host/sim: std::hash<std::thread::id>{}(std::this_thread::get_id())
 *
 * With none installed the check is simply off. */
using ThreadId = uintptr_t;
using ThreadIdFn = ThreadId (*)();

/* The app: one per process.
 *
 * It holds no state of its own. The arenas, the element tree, the frame timer
 * and the published stats all belong to the library, in app.cpp, so that none
 * of their types has to appear in this header. */
class FmsApp {
public:
    static FmsApp &instance();

    FmsApp(const FmsApp &) = delete;
    FmsApp &operator=(const FmsApp &) = delete;

    /* `builder` is called on every rebuild and must return a fresh widget tree.
     * It runs inside a build pass, so `new` inside it hits the arena. */
    void init(lv_display_t *display, WidgetBuilder builder);

    /* Force a rebuild on the next frame. setState() does this for you.
     *
     * This is the only entry point that may be called from another task. Publish
     * what changed somewhere the build can read it, then call this. It is one
     * release store -- no allocation, no locking, and it cannot block, so a
     * sensor task at 100Hz can call it on every sample without waiting on the UI.
     *
     *     altitude_.store(v, std::memory_order_relaxed);   // yours to own
     *     FmsApp::instance().requestFrame();               // then this
     *
     * This is a single-producer example. When the frame loop acquires this
     * producer's request, the release/acquire pair orders the relaxed altitude
     * store before the build. With multiple producers, the coalesced request
     * flag is not a publication barrier for every producer: synchronize each
     * shared payload independently with an atomic, lock, queue, or immutable
     * handoff. requestFrame() only schedules and coalesces the rebuild.
     *
     * Order still matters: publish first, request second. Requesting a frame
     * does not queue the value, it only says "read your inputs again", so the UI
     * shows the latest reading rather than replaying every one of them -- which
     * is what you want from a stream of sensor, CAN or network updates.
     *
     * What you must NOT do is reach into a State from another task. setState()
     * runs your mutation immediately, on the calling task, racing the build. */
    void requestFrame();

    /* A requester that can be poked from an IRAM interrupt handler. Fetch it
     * during setup; see FrameRequester. */
    FrameRequester requester();

    Size screenSize() const;

    /* The last completed frame, as a consistent copy. Safe to call from any
     * task; see FrameStats for why it is a copy. */
    FrameStats stats() const;

    /* Tear the app down: stop the frame timer, destroy the element tree -- which
     * destroys every lv_obj it made -- then the widgets the last two builds left
     * in the arenas, and only then release the shared styles, which those
     * objects were pointing at.
     *
     * The device never calls this; the app is the process. The simulator and the
     * tests do, so that a run ends with nothing outstanding and LeakSanitizer
     * has nothing of ours to report. Safe on an app that was never init()ed, and
     * init() may be called again afterwards. */
    void shutdown();

    void setBackground(Color c);

    /* LVGL's tick is only millisecond-resolution, which rounds a whole build
     * pass down to zero.  Install the platform's real microsecond clock
     * (esp_timer_get_time on the device, steady_clock in the simulator) so the
     * M5 profiling numbers mean something. */
    using MicrosClock = uint32_t (*)();
    void setClock(MicrosClock clock);

    /* How to ask which thread is running, so that setState() from the wrong one
     * can be caught rather than corrupting the arena in the background. Install
     * it before the first frame; see ThreadId above for why the framework cannot
     * work this out for itself. Without it the check is off and nothing else
     * changes. */
    void setThreadId(ThreadIdFn fn);

private:
    FmsApp() = default;
};

/* Convenience: FmsApp::instance().init(lv_display_get_default(), builder). */
void runApp(WidgetBuilder builder);

}  // namespace fmsui
