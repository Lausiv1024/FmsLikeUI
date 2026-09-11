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
 *   idf.py -DFMSUI_DEMO=fplan build    ACTIVE/F-PLN, a list stepped by its arrows
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
#include "fplan_demo.h"
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

/* LVGL's redraw is timed on the port's own task and read from this one, so the
 * counters live behind take() rather than in loose volatiles here. */
fmsui::RefreshStats g_refresh;

uint32_t micros() { return static_cast<uint32_t>(esp_timer_get_time()); }

/* Who is running, for the framework's wrong-thread check.
 *
 * Not std::this_thread::get_id(): that goes through pthread_self(), which
 * asserts when called from a FreeRTOS task that was not created as a pthread --
 * and the frame loop runs on the task esp_lvgl_port creates, which is one. The
 * board rebooted on its first frame until this replaced it. The task handle is
 * the identity FreeRTOS actually has.
 *
 * The raw-LVGL probe never starts the framework, so it has no use for this. */
#if !defined(FMSUI_DEMO_M0)
fmsui::ThreadId thread_id() {
    return reinterpret_cast<fmsui::ThreadId>(xTaskGetCurrentTaskHandle());
}
#endif

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

    g_refresh.attach(disp, micros);

#if defined(FMSUI_DEMO_M0)
    m0_probe_build("M5 Tab5 / ESP32-P4");
#else
    fmsui::FmsApp::instance().setClock(micros);
    fmsui::FmsApp::instance().setThreadId(thread_id);
#if defined(FMSUI_DEMO_M1)
    fmsui::runApp([] { return m1_demo_build(); });
#elif defined(FMSUI_DEMO_CATALOG)
    fmsui::runApp([] { return catalog_build(); });
#elif defined(FMSUI_DEMO_FPLAN)
    fmsui::runApp([] { return fplan_demo_build(); });
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

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* One call, one second's worth: the count, the total and the worst come
         * out together, so `avg` cannot be this second's microseconds over the
         * next second's count. The screen is static until you touch it, so LVGL
         * only refreshes when something is dirty -- drag a finger across it, and
         * that is when these numbers mean anything. `avg` spans "refresh
         * started" to "flushed". */
        const fmsui::RefreshSnapshot r = g_refresh.take();
        ESP_LOGI(kTag, "%2" PRIu32 " refr/s | avg %6.2f ms | max %6.2f ms | rotation: %s",
                 r.refreshes,
                 r.refreshes > 0 ? static_cast<double>(r.busy_us) / r.refreshes / 1000.0 : 0.0,
                 r.max_us / 1000.0, kRotationMode);

#if !defined(FMSUI_DEMO_M0)
        const fmsui::FrameStats f = fmsui::FmsApp::instance().stats();
        ESP_LOGI(kTag,
                 "   fmsui: %" PRIu32 " builds | %" PRIu32 " widgets | %" PRIu32 " lv_objs (+%" PRIu32
                 " new, %" PRIu32 " moved, %" PRIu32 " retext) | arena %" PRIu32 " B",
                 f.builds, f.widgets, f.lv_objects, f.lv_created, f.lv_moved, f.lv_retexted,
                 f.arena_bytes);
        ESP_LOGI(kTag,
                 "   fmsui: total %5" PRIu32 " us = build %5" PRIu32 " + layout %5" PRIu32
                 " + paint %5" PRIu32,
                 f.total_us, f.build_us, f.layout_us, f.paint_us);

        /* The style cache never evicts, so these are also its high-water marks.
         * They should stop moving once every look on the page has been seen; if
         * they keep climbing, something is generating a fresh colour or radius
         * per frame and the cache is the wrong shape for it. */
        const fmsui::StyleCacheStats sc = fmsui::styleCacheStats();
        ESP_LOGI(kTag, "   fmsui: styles %" PRIu32 " text + %" PRIu32 " box", sc.text_styles,
                 sc.box_styles);
#endif
    }
}
