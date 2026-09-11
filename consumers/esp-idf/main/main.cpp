/* An ESP-IDF application of FmsLikeUI, as far as the framework can tell.
 *
 * It exists to show that the fmsui component, on its own, compiles and links
 * into an ESP32-P4 image. There is no board here: the display is LVGL's own,
 * backed by a buffer that goes nowhere, there is no touch, and no panel is
 * initialised. Bringing those up is the application's job -- on the Tab5 the
 * BSP and esp_lvgl_port do it, see main/main.cpp at the top of the repository.
 *
 * CI builds this and does not flash it, so nothing below has run on hardware.
 * It is still written the way it would have to run: this task owns LVGL and the
 * frame loop, and a second task publishes data and requests frames.
 */

#include <atomic>
#include <cstdint>
#include <cstdlib>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "fmsui/fmsui.h"

#include "consumer_page.h"

namespace {

constexpr char kTag[] = "consumer";

constexpr int32_t kWidth = 320;
constexpr int32_t kHeight = 240;

/* Written by the sampler task, read by the page's build. */
std::atomic<uint32_t> g_sample{0};

uint32_t millis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
uint32_t micros() { return static_cast<uint32_t>(esp_timer_get_time()); }

/* Not std::this_thread::get_id(): see ThreadId in fmsui/element.h. */
fmsui::ThreadId thread_id() {
    return reinterpret_cast<fmsui::ThreadId>(xTaskGetCurrentTaskHandle());
}

/* Another task with data for the page: publish, then ask for a frame. Never
 * setState() from here -- that would run on this task, racing the build. */
void sampler(void *) {
    for (;;) {
        g_sample.store(g_sample.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        fmsui::FmsApp::instance().requestFrame();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}  // namespace

extern "C" void app_main(void) {
    lv_init();
    lv_tick_set_cb(millis);

    const size_t fb_bytes = static_cast<size_t>(kWidth) * kHeight * 2;  // RGB565
    auto *fb = static_cast<uint8_t *>(std::calloc(fb_bytes, 1));
    if (fb == nullptr) {
        ESP_LOGE(kTag, "no memory for a %d x %d buffer", static_cast<int>(kWidth),
                 static_cast<int>(kHeight));
        return;
    }

    lv_display_t *display = lv_display_create(kWidth, kHeight);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, fb, nullptr, fb_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
        lv_display_flush_ready(d);  // a panel driver would send the area here
    });

    fmsui::FmsApp &app = fmsui::FmsApp::instance();
    app.setClock(micros);
    app.setThreadId(thread_id);

    /* No fmsui_fonts: the one font this LVGL was configured with. */
    const fmsui::FmsThemeData theme = consumer_theme(LV_FONT_DEFAULT);
    fmsui::runApp([theme] { return consumer_build(theme, g_sample); });

    xTaskCreate(sampler, "sampler", 2048, nullptr, tskIDLE_PRIORITY + 1, nullptr);

    /* From here this task runs LVGL, and with it the frame loop. */
    for (;;) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
