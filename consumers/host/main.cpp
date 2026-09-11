/* An application of FmsLikeUI, as far as the framework can tell.
 *
 * It owns what an application owns -- initialising LVGL, the display, the input
 * device, the tick and the choice of font -- and reaches the framework through
 * <fmsui/fmsui.h> alone. The display is a buffer nobody looks at and the pointer
 * is a struct this file moves, so it runs wherever CTest does, with no window.
 *
 * What it holds the framework to, in order:
 *
 *   1. runApp() builds the page and paints it into LVGL
 *   2. a value another thread publishes and then requests a frame for is on the
 *      screen after the next frame, and nothing builds after that
 *   3. a tap on an FmsButton goes through setState() into the next frame
 *   4. every label is in the font the application put in the theme -- not the
 *      fallback, and with fmsui_fonts nowhere in the build
 *   5. shutdown() takes every lv_obj the framework made back off the screen
 *
 * This checks the usage contract in docs/USING.md. The framework's own
 * behaviour is tested in far more depth under tests/.
 */

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>

#include "lvgl.h"

#include "fmsui/fmsui.h"

#include "consumer_page.h"

namespace {

constexpr int32_t kWidth = 480;
constexpr int32_t kHeight = 320;

/* The frame loop's timer period, so one step runs at most one frame. */
constexpr uint32_t kStepMs = 10;

/* LVGL reads the pointer every LV_DEF_REFR_PERIOD (33ms in the bundled
 * lv_conf.h), so a press has to be held across more steps than that. */
constexpr int kHoldSteps = 8;

int g_failures = 0;

void check(bool ok, const char *what) {
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failures++;
}

void step(int frames = 1) {
    for (int i = 0; i < frames; i++) {
        lv_tick_inc(kStepMs);
        lv_timer_handler();
    }
}

/* Written by another thread, read by the page's build. */
std::atomic<uint32_t> g_sample{0};

/* The application's font. A real one would be generated for the application,
 * or loaded through LVGL; this is a copy of the only font the bundled lv_conf.h
 * enables, at an address of its own. So a label in it can be told apart from
 * one that fell back to LV_FONT_DEFAULT, which is what a framework ignoring the
 * theme's fonts would draw. */
lv_font_t g_font;

/* The pointer, as a touch driver would report it. */
struct {
    lv_point_t at{};
    bool pressed = false;
} g_pointer;

void readPointer(lv_indev_t *, lv_indev_data_t *data) {
    data->point = g_pointer.at;
    data->state = g_pointer.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void tap(lv_obj_t *target) {
    lv_area_t a;
    lv_obj_get_coords(target, &a);
    g_pointer.at = {(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
    g_pointer.pressed = true;
    step(kHoldSteps);
    g_pointer.pressed = false;
    step(kHoldSteps);
}

fmsui::ThreadId thisThread() {
    return static_cast<fmsui::ThreadId>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

/* Found through LVGL, the way anything outside the framework has to. */
lv_obj_t *labelShowing(lv_obj_t *screen, const char *text) {
    const uint32_t n = lv_obj_get_child_count(screen);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(screen, i);
        if (lv_obj_check_type(child, &lv_label_class) &&
            std::strcmp(lv_label_get_text(child), text) == 0) {
            return child;
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    lv_init();

    /* ---- What the application brings ------------------------------------ */

    const size_t fb_bytes = static_cast<size_t>(kWidth) * kHeight * 2;  // RGB565
    auto *fb = static_cast<uint8_t *>(std::calloc(fb_bytes, 1));
    if (fb == nullptr) return 1;

    lv_display_t *display = lv_display_create(kWidth, kHeight);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, fb, nullptr, fb_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
        lv_display_flush_ready(d);  // a panel driver would send the area here
    });

    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, readPointer);
    lv_indev_set_display(pointer, display);

    /* No fmsui_fonts: a font of the application's own. */
    g_font = *LV_FONT_DEFAULT;
    const fmsui::FmsThemeData theme = consumer_theme(&g_font);

    /* ---- The framework --------------------------------------------------- */

    fmsui::FmsApp &app = fmsui::FmsApp::instance();
    app.setThreadId(thisThread);
    fmsui::runApp([theme] { return consumer_build(theme, g_sample); });

    lv_obj_t *screen = lv_display_get_screen_active(display);

    std::printf("1. runApp()\n");
    step();
    check(app.stats().builds == 1, "the first frame built the page");
    check(labelShowing(screen, "SAMPLE 0") != nullptr, "and painted it into LVGL");

    std::printf("2. requestFrame() from another thread\n");
    {
        const uint32_t before = app.stats().builds;
        std::thread producer([] {
            g_sample.store(42, std::memory_order_relaxed);  // publish...
            fmsui::FmsApp::instance().requestFrame();       // ...then ask
        });
        producer.join();

        step();
        check(app.stats().builds == before + 1, "the next frame built");
        check(labelShowing(screen, "SAMPLE 42") != nullptr, "and shows the published value");

        step(kHoldSteps);
        check(app.stats().builds == before + 1, "nothing builds without another request");
    }

    std::printf("3. a tap goes through setState()\n");
    {
        lv_obj_t *button = labelShowing(screen, "TAP");
        check(button != nullptr, "the button is on the screen");
        if (button != nullptr) tap(button);
        check(labelShowing(screen, "TAPS 1") != nullptr, "tapping it rebuilt the page with the new state");
    }

    std::printf("4. the fonts are the application's\n");
    {
        uint32_t labels = 0;
        uint32_t other_font = 0;
        const uint32_t n = lv_obj_get_child_count(screen);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t *child = lv_obj_get_child(screen, i);
            if (!lv_obj_check_type(child, &lv_label_class)) continue;
            labels++;
            if (lv_obj_get_style_text_font(child, LV_PART_MAIN) != &g_font) other_font++;
        }
        std::printf("  (%u labels)\n", static_cast<unsigned>(labels));
        check(labels >= 4, "every text on the page is on the screen");
        check(other_font == 0, "each in the font the theme was given");
    }

    std::printf("5. shutdown()\n");
    app.shutdown();
    check(lv_obj_get_child_count(screen) == 0, "every lv_obj the framework made is gone");

    lv_indev_delete(pointer);
    lv_display_delete(display);
    std::free(fb);

    std::printf("\n%s, %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
