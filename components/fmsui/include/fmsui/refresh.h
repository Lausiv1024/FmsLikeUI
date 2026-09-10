#pragma once

/* What LVGL's own redraw costs.
 *
 * The frame loop's numbers (FrameStats) stop at "pushed into LVGL". This is the
 * other half: LVGL's dirty-area redraw, timed from LV_EVENT_REFR_START to
 * LV_EVENT_REFR_READY, which on the Tab5 also covers the rotation and the flush
 * to the panel.
 *
 * It is written on the task that runs lv_timer_handler and read from wherever
 * the logging happens, which on the device is a different task. So the counters
 * are not left loose for each reader to sample one at a time -- take() returns
 * all of them at once, under one lock, and starts a fresh interval. A reader
 * cannot end up dividing this interval's microseconds by the next interval's
 * refresh count.
 */

#include <cstdint>
#include <mutex>

#include "lvgl.h"

namespace fmsui {

/* One interval's worth of refreshes: everything since the previous take(). */
struct RefreshSnapshot {
    uint32_t refreshes = 0;  /* redraws LVGL completed */
    uint64_t busy_us = 0;    /* summed REFR_START -> REFR_READY */
    uint32_t max_us = 0;     /* the worst single one */
};

class RefreshStats {
public:
    /* Start counting `display`'s redraws. `clock` is the platform's microsecond
     * clock -- the same one FmsApp::setClock() is given; LVGL's own tick is
     * milliseconds, which rounds most of a refresh away. */
    void attach(lv_display_t *display, uint32_t (*clock)());

    /* Read the interval and begin the next one. */
    RefreshSnapshot take();

private:
    static void onStart(lv_event_t *e);
    static void onReady(lv_event_t *e);

    std::mutex mutex_;
    uint32_t (*clock_)() = nullptr;
    uint32_t started_us_ = 0;
    RefreshSnapshot interval_;
};

}  // namespace fmsui
