#include "fmsui/refresh.h"

namespace fmsui {

void RefreshStats::attach(lv_display_t *display, uint32_t (*clock)()) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        clock_ = clock;
        started_us_ = 0;
        interval_ = RefreshSnapshot{};
    }
    lv_display_add_event_cb(display, onStart, LV_EVENT_REFR_START, this);
    lv_display_add_event_cb(display, onReady, LV_EVENT_REFR_READY, this);
}

void RefreshStats::onStart(lv_event_t *e) {
    auto *self = static_cast<RefreshStats *>(lv_event_get_user_data(e));
    const std::lock_guard<std::mutex> lock(self->mutex_);
    if (self->clock_ != nullptr) self->started_us_ = self->clock_();
}

void RefreshStats::onReady(lv_event_t *e) {
    auto *self = static_cast<RefreshStats *>(lv_event_get_user_data(e));
    const std::lock_guard<std::mutex> lock(self->mutex_);
    if (self->clock_ == nullptr) return;

    /* Unsigned, so a clock that wrapped mid-refresh still gives the elapsed
     * time -- a 32-bit microsecond counter comes round every 71 minutes. */
    const uint32_t us = self->clock_() - self->started_us_;

    self->interval_.refreshes++;
    self->interval_.busy_us += us;
    if (us > self->interval_.max_us) self->interval_.max_us = us;
}

RefreshSnapshot RefreshStats::take() {
    const std::lock_guard<std::mutex> lock(mutex_);
    const RefreshSnapshot s = interval_;
    interval_ = RefreshSnapshot{};
    return s;
}

}  // namespace fmsui
