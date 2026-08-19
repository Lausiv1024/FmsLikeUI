/* Device entry point.
 *
 * Brings up the Tab5 panel through the BSP, rotates LVGL into landscape, and
 * runs the same UI code the simulator runs.  It then logs frame rate, refresh
 * time and memory over UART once a second -- those numbers decide whether the
 * rendering pipeline as configured is good enough to build on.
 *
 * Which screen it shows:
 *   idf.py build                       ACTIVE/PERF + ACTIVE/INIT (default)
 *   idf.py -DFMSUI_DEMO=catalog build  every FMS widget on one screen
 *   idf.py -DFMSUI_DEMO=m1 build       the core/theme demo
 *   idf.py -DFMSUI_DEMO=m0 build       the raw-LVGL bring-up probe
 *   idf.py -DFMSUI_DEMO=reorder -DFMSUI_ROWS=40 build
 *                                      keyed reconciliation, at the lv_obj count
 *                                      the reorder cost has to be judged at
 */

#include <cinttypes>

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "fmsui/fmsui.h"
#include "catalog.h"
#include "fms_pages.h"
#include "m0_probe.h"
#include "m1_demo.h"
#include "reorder_demo.h"

namespace {

constexpr char kTag[] = "fmsui";

/* The panel is natively portrait (720x1280).  Landscape therefore costs a
 * rotation somewhere in the pipeline: esp_lvgl_port does it in software unless
 * CONFIG_LVGL_PORT_ENABLE_PPA hands it to the ESP32-P4's 2D accelerator.  Which
 * one we got is the most important thing to know, so say it out loud. */
#if CONFIG_LVGL_PORT_ENABLE_PPA
constexpr char kRotationMode[] = "PPA (hardware)";
#else
constexpr char kRotationMode[] = "software";
#endif

struct Stats {
    volatile uint32_t refreshes;
    volatile uint64_t refresh_us_total;
    volatile uint32_t refresh_us_max;
    int64_t refresh_started_us;
};

Stats g_stats{};

void on_refr_start(lv_event_t *) { g_stats.refresh_started_us = esp_timer_get_time(); }

void on_refr_ready(lv_event_t *) {
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - g_stats.refresh_started_us);
    g_stats.refreshes++;
    g_stats.refresh_us_total += us;
    if (us > g_stats.refresh_us_max) g_stats.refresh_us_max = us;
}

uint32_t micros() { return static_cast<uint32_t>(esp_timer_get_time()); }

void log_memory() {
    ESP_LOGI(kTag, "heap: internal free %u (largest %u) | psram free %u (largest %u)",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "FmsLikeUI -- LVGL %d.%d.%d, rotation: %s", LVGL_VERSION_MAJOR,
             LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, kRotationMode);
    log_memory();

    lv_display_t *disp = bsp_display_start();
    if (disp == nullptr) {
        ESP_LOGE(kTag, "bsp_display_start() failed");
        return;
    }

    /* Everything below touches LVGL, which the BSP drives from its own task. */
    bsp_display_lock(0);

    bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90);

    lv_display_add_event_cb(disp, on_refr_start, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, nullptr);

#if defined(FMSUI_DEMO_M0)
    m0_probe_build("M5 Tab5 / ESP32-P4");
#else
    fmsui::FmsApp::instance().setClock(micros);
#if defined(FMSUI_DEMO_M1)
    fmsui::runApp([] { return m1_demo_build(); });
#elif defined(FMSUI_DEMO_CATALOG)
    fmsui::runApp([] { return catalog_build(); });
#elif defined(FMSUI_DEMO_REORDER)
    fmsui::runApp([] { return reorder_demo_build(FMSUI_REORDER_ROWS); });
#else
    fmsui::runApp([] { return fms_pages_build(); });
#endif
#endif

    const int32_t w = lv_display_get_horizontal_resolution(disp);
    const int32_t h = lv_display_get_vertical_resolution(disp);

    bsp_display_unlock();

    bsp_display_backlight_on();

    ESP_LOGI(kTag, "display is %" PRId32 "x%" PRId32 " after rotation (panel native is %dx%d)", w, h,
             BSP_LCD_H_RES, BSP_LCD_V_RES);
    log_memory();

    uint32_t last_refreshes = 0;
    uint64_t last_us_total = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        const uint32_t refreshes = g_stats.refreshes;
        const uint64_t us_total = g_stats.refresh_us_total;
        const uint32_t frames = refreshes - last_refreshes;
        const uint64_t us = us_total - last_us_total;
        last_refreshes = refreshes;
        last_us_total = us_total;

        /* The screen is static until you touch it, so LVGL only refreshes when
         * something is dirty.  Drag a finger across it: that is when these
         * numbers mean anything.  `avg` spans "refresh started" to "flushed". */
        ESP_LOGI(kTag, "%2" PRIu32 " refr/s | avg %6.2f ms | max %6.2f ms | rotation: %s", frames,
                 frames > 0 ? static_cast<double>(us) / frames / 1000.0 : 0.0,
                 g_stats.refresh_us_max / 1000.0, kRotationMode);
        g_stats.refresh_us_max = 0;

#if !defined(FMSUI_DEMO_M0)
        const fmsui::FrameStats &f = fmsui::FmsApp::instance().stats();
        ESP_LOGI(kTag,
                 "   fmsui: %" PRIu32 " builds | %" PRIu32 " widgets | %" PRIu32 " lv_objs (+%" PRIu32
                 " new, %" PRIu32 " moved) | arena %" PRIu32 " B",
                 f.builds, f.widgets, f.lv_objects, f.lv_created, f.lv_moved, f.arena_bytes);
        ESP_LOGI(kTag,
                 "   fmsui: total %5" PRIu32 " us = build %5" PRIu32 " + layout %5" PRIu32
                 " + paint %5" PRIu32,
                 f.total_us, f.build_us, f.layout_us, f.paint_us);
#endif
    }
}
