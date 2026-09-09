/* FmsLikeUI simulator.
 *
 *   fmsui_sim                      interactive SDL window, mouse acts as the finger
 *   fmsui_sim --shot out.png       render headless and dump a PNG
 *   fmsui_sim --demo m0            the raw-LVGL probe screen instead of the framework
 *   fmsui_sim --demo fplan         ACTIVE/F-PLN: a windowed list, stepped not scrolled
 *
 * The headless mode is what lets the UI be verified without a display server,
 * and it renders through exactly the same LVGL pipeline as the window mode.
 */

#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "lvgl.h"

#include "fmsui/fmsui.h"
#include "catalog.h"
#include "fms_pages.h"
#include "fplan_demo.h"
#include "m0_probe.h"
#include "m1_demo.h"
#include "reorder_demo.h"
#include "png_write.h"

namespace {

constexpr int32_t kWidth = 1280;
constexpr int32_t kHeight = 720;

/* Headless runs use a fake clock so a screenshot is reproducible; the window
 * mode uses the wall clock. */
bool g_headless = false;
uint32_t g_fake_ms = 0;

uint32_t tick_cb() {
    if (g_headless) return g_fake_ms;
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count());
}

enum class Demo { M0Probe, M1Core, Catalog, Pages, Reorder, Fplan };

/* --rows N for the reorder demo, and --stats to print what each interaction
 * cost.  A tap that calls setState produces exactly one frame() pass, so the
 * stats read straight after it are that interaction's, not an average. */
int g_rows = 6;
bool g_print_stats = false;

void printStats(const char *what) {
    if (!g_print_stats) return;
    const fmsui::FrameStats &s = fmsui::FmsApp::instance().stats();
    std::printf("stats %-10s rows=%d widgets=%u lv_objs=%u lv_created=%u lv_moved=%u "
                "lv_retexted=%u build=%uus layout=%uus paint=%uus total=%uus\n",
                what, g_rows, static_cast<unsigned>(s.widgets),
                static_cast<unsigned>(s.lv_objects), static_cast<unsigned>(s.lv_created),
                static_cast<unsigned>(s.lv_moved), static_cast<unsigned>(s.lv_retexted),
                static_cast<unsigned>(s.build_us), static_cast<unsigned>(s.layout_us),
                static_cast<unsigned>(s.paint_us), static_cast<unsigned>(s.total_us));
}

/* Scripted touch, so a headless run can prove the whole chain -- hit test,
 * GestureDetector, setState, rebuild, repaint -- and not just that the first
 * frame looks right.  A screenshot alone cannot tell you the UI is alive. */
struct ScriptedTouch {
    std::vector<lv_point_t> taps;  // --tap may be given more than once
    lv_point_t point{0, 0};
    bool pressed = false;
    bool enabled() const { return !taps.empty(); }
};
ScriptedTouch g_touch;

void touch_read_cb(lv_indev_t *, lv_indev_data_t *data) {
    data->point = g_touch.point;
    data->state = g_touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Advance LVGL by one frame of the fake clock. */
void step(int frames = 1) {
    for (int i = 0; i < frames; i++) {
        g_fake_ms += 33;
        lv_timer_handler();
    }
}

/* Who is running, for the framework's wrong-thread check.  The host has real
 * threads, so std::thread::id is the identity -- hashed down to the integer the
 * framework compares, because it cannot name std::thread::id without dragging
 * <thread> into a header the device also compiles. */
fmsui::ThreadId thread_id_cb() {
    return static_cast<fmsui::ThreadId>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

uint32_t micros_cb() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start).count());
}

void buildScreen(Demo demo, const char *platform) {
    if (demo == Demo::M0Probe) {
        m0_probe_build(platform);
        return;
    }
    fmsui::FmsApp::instance().setClock(micros_cb);
    fmsui::FmsApp::instance().setThreadId(thread_id_cb);
    if (demo == Demo::Pages) {
        fmsui::runApp([] { return fms_pages_build(); });
    } else if (demo == Demo::Catalog) {
        fmsui::runApp([] { return catalog_build(); });
    } else if (demo == Demo::Reorder) {
        const int rows = g_rows;
        fmsui::runApp([rows] { return reorder_demo_build(rows); });
    } else if (demo == Demo::Fplan) {
        fmsui::runApp([] { return fplan_demo_build(); });
    } else {
        fmsui::runApp([] { return m1_demo_build(); });
    }
}

int run_headless(Demo demo, const char *out_path, int frames) {
    g_headless = true;

    auto *fb = static_cast<uint8_t *>(std::calloc(static_cast<size_t>(kWidth) * kHeight * 2, 1));
    if (fb == nullptr) return 1;

    lv_display_t *disp = lv_display_create(kWidth, kHeight);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, fb, nullptr, static_cast<size_t>(kWidth) * kHeight * 2,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
        lv_display_flush_ready(d);  // the buffer *is* the framebuffer; nothing to push
    });

    if (g_touch.enabled()) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_display(indev, disp);
    }

    buildScreen(demo, "SIM (headless)");

    step(frames);
    printStats("initial");

    for (const lv_point_t &p : g_touch.taps) {
        /* Press, hold a few frames, release. LVGL turns press+release inside the
         * object into a CLICKED, which is what GestureDetector listens for. Then
         * give the framework a frame to rebuild after setState, and LVGL one to
         * repaint what changed. */
        g_touch.point = p;
        g_touch.pressed = true;
        step(3);
        g_touch.pressed = false;
        step(4);
        std::printf("tapped at (%d, %d)\n", static_cast<int>(p.x), static_cast<int>(p.y));
        printStats("after-tap");
    }

    lv_refr_now(disp);

    auto *rgb = static_cast<uint8_t *>(std::malloc(static_cast<size_t>(kWidth) * kHeight * 3));
    if (rgb == nullptr) return 1;
    for (size_t i = 0; i < static_cast<size_t>(kWidth) * kHeight; i++) {
        const uint16_t p = static_cast<uint16_t>(fb[i * 2] | (fb[i * 2 + 1] << 8));
        const uint8_t r5 = (p >> 11) & 0x1F;
        const uint8_t g6 = (p >> 5) & 0x3F;
        const uint8_t b5 = p & 0x1F;
        // Replicate the high bits into the low ones so 0x1F maps to 0xFF.
        rgb[i * 3 + 0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        rgb[i * 3 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        rgb[i * 3 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    }

    const bool ok = pngw::write_rgb(out_path, rgb, kWidth, kHeight);
    std::free(rgb);
    std::free(fb);

    if (!ok) {
        std::fprintf(stderr, "failed to write %s\n", out_path);
        return 1;
    }
    std::printf("wrote %s (%dx%d, %d frames)\n", out_path, kWidth, kHeight, frames);
    return 0;
}

int run_window(Demo demo) {
    lv_display_t *disp = lv_sdl_window_create(kWidth, kHeight);
    lv_sdl_window_set_title(disp, "FmsLikeUI simulator - M5 Tab5 (1280x720)");

    lv_indev_t *mouse = lv_sdl_mouse_create();
    lv_indev_set_display(mouse, disp);

    buildScreen(demo, "SIM (SDL2)");

    while (true) {
        const uint32_t idle = lv_timer_handler();
        lv_delay_ms(idle < 5 ? 5 : (idle > 30 ? 30 : idle));
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    const char *shot_path = nullptr;
    int frames = 3;
    Demo demo = Demo::Pages;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot_path = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--tap") == 0 && i + 1 < argc) {
            int x = 0;
            int y = 0;
            if (std::sscanf(argv[++i], "%d,%d", &x, &y) != 2) {
                std::fprintf(stderr, "--tap wants X,Y\n");
                return 2;
            }
            g_touch.taps.push_back(lv_point_t{x, y});
        } else if (std::strcmp(argv[i], "--rows") == 0 && i + 1 < argc) {
            g_rows = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--stats") == 0) {
            g_print_stats = true;
        } else if (std::strcmp(argv[i], "--demo") == 0 && i + 1 < argc) {
            const char *name = argv[++i];
            if (std::strcmp(name, "m0") == 0) {
                demo = Demo::M0Probe;
            } else if (std::strcmp(name, "m1") == 0) {
                demo = Demo::M1Core;
            } else if (std::strcmp(name, "catalog") == 0) {
                demo = Demo::Catalog;
            } else if (std::strcmp(name, "pages") == 0) {
                demo = Demo::Pages;
            } else if (std::strcmp(name, "reorder") == 0) {
                demo = Demo::Reorder;
                if (g_rows < 1) g_rows = 6;
            } else if (std::strcmp(name, "fplan") == 0) {
                demo = Demo::Fplan;
            } else {
                std::fprintf(stderr,
                             "unknown demo '%s' (expected m0, m1, catalog, pages, reorder "
                             "or fplan)\n",
                             name);
                return 2;
            }
        } else {
            std::fprintf(stderr,
                         "usage: %s [--demo m0|m1|catalog|pages|reorder|fplan] "
                         "[--shot out.png] "
                         "[--frames N] [--tap X,Y]... [--rows N] [--stats]\n",
                         argv[0]);
            return 2;
        }
    }

    lv_init();
    lv_tick_set_cb(tick_cb);

    return shot_path != nullptr ? run_headless(demo, shot_path, frames) : run_window(demo);
}
