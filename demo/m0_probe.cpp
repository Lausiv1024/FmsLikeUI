#include "m0_probe.h"

#include <cstdio>

#include "lvgl.h"

#include "fmsui_fonts.h"

namespace {

/* Initial FMS palette, lifted from the reference screenshots.  These move into
 * FmsTheme in M2; here they exist so we can eyeball the colours on the real
 * panel early -- LCD gamma tends to make cyan and green look different from
 * what the simulator shows. */
constexpr lv_color_t kBackground = LV_COLOR_MAKE(0x00, 0x00, 0x00);
constexpr lv_color_t kSurface = LV_COLOR_MAKE(0x14, 0x18, 0x1C);
constexpr lv_color_t kBorder = LV_COLOR_MAKE(0x4A, 0x55, 0x60);
constexpr lv_color_t kLabel = LV_COLOR_MAKE(0xD8, 0xDC, 0xE0);
constexpr lv_color_t kCyan = LV_COLOR_MAKE(0x29, 0xC5, 0xE8);
constexpr lv_color_t kGreen = LV_COLOR_MAKE(0x1A, 0xE0, 0x1A);
constexpr lv_color_t kAmber = LV_COLOR_MAKE(0xFF, 0x9E, 0x1B);
constexpr lv_color_t kMagenta = LV_COLOR_MAKE(0xE0, 0x60, 0xE0);

struct Probe {
    lv_obj_t *touch_readout;
    lv_obj_t *crosshair_h;
    lv_obj_t *crosshair_v;
    lv_obj_t *tap_counter;
    int taps;
};

Probe g_probe{};

lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

/* Touch anywhere: move the crosshair there and print the coordinates.  If the
 * panel needs a rotation we have not applied, the crosshair will not follow the
 * finger -- that is the cheapest possible test for it. */
void on_screen_press(lv_event_t *e) {
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_obj_set_pos(g_probe.crosshair_h, p.x - 20, p.y);
    lv_obj_set_pos(g_probe.crosshair_v, p.x, p.y - 20);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "TOUCH  X %4d   Y %4d", static_cast<int>(p.x), static_cast<int>(p.y));
    lv_label_set_text(g_probe.touch_readout, buf);

    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g_probe.taps++;
        std::snprintf(buf, sizeof(buf), "TAPS  %d", g_probe.taps);
        lv_label_set_text(g_probe.tap_counter, buf);
    }
}

/* A framed value box, the shape the whole FMS UI is built from. */
lv_obj_t *make_field(lv_obj_t *parent, int x, int y, const char *text, lv_color_t color) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, 150, 44);
    lv_obj_set_style_bg_color(box, kSurface, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, kBorder, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 0, 0);

    lv_obj_t *l = make_label(box, text, color, &fms_b612_mono_28);
    lv_obj_center(l);
    return box;
}

}  // namespace

void m0_probe_build(const char *platform) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, kBackground, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    const int32_t w = lv_display_get_horizontal_resolution(nullptr);
    const int32_t h = lv_display_get_vertical_resolution(nullptr);

    /* A 1px frame on the screen edge: if any of the four sides is missing or
     * clipped, the resolution or the rotation is wrong. */
    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_pos(frame, 0, 0);
    lv_obj_set_size(frame, w, h);
    lv_obj_set_style_border_color(frame, kBorder, 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_radius(frame, 0, 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "FmsLikeUI  M0 PROBE   %s", platform);
    lv_obj_t *title = make_label(scr, buf, kLabel, &fms_b612_mono_32);
    lv_obj_set_pos(title, 24, 20);

    std::snprintf(buf, sizeof(buf), "DISPLAY  %d x %d      LVGL %d.%d.%d", static_cast<int>(w),
                  static_cast<int>(h), LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    lv_obj_t *info = make_label(scr, buf, kGreen, &fms_b612_mono_24);
    lv_obj_set_pos(info, 24, 66);

    /* Palette row -- these are the semantic colours of the real FMS: cyan is a
     * value the pilot may enter, green is computed by the system, amber wants
     * attention, magenta is a constraint. */
    make_field(scr, 24, 130, "CYAN", kCyan);
    make_field(scr, 190, 130, "GREEN", kGreen);
    make_field(scr, 356, 130, "AMBER", kAmber);
    make_field(scr, 522, 130, "MAGENTA", kMagenta);
    make_field(scr, 688, 130, "LABEL", kLabel);

    g_probe.touch_readout = make_label(scr, "TOUCH  X ----   Y ----", kCyan, &fms_b612_mono_28);
    lv_obj_set_pos(g_probe.touch_readout, 24, 200);

    g_probe.taps = 0;
    g_probe.tap_counter = make_label(scr, "TAPS  0", kAmber, &fms_b612_mono_28);
    lv_obj_set_pos(g_probe.tap_counter, 24, 240);

    /* Corner markers.  Their labels say where they *should* be, so a rotated or
     * mirrored panel is obvious at a glance. */
    struct Corner {
        const char *text;
        lv_align_t align;
    };
    const Corner corners[] = {
        {"TOP-LEFT", LV_ALIGN_TOP_LEFT},
        {"TOP-RIGHT", LV_ALIGN_TOP_RIGHT},
        {"BOTTOM-LEFT", LV_ALIGN_BOTTOM_LEFT},
        {"BOTTOM-RIGHT", LV_ALIGN_BOTTOM_RIGHT},
    };
    for (const Corner &c : corners) {
        lv_obj_t *l = make_label(scr, c.text, kMagenta, &fms_b612_mono_20);
        lv_obj_align(l, c.align, c.align == LV_ALIGN_TOP_LEFT || c.align == LV_ALIGN_BOTTOM_LEFT ? 8 : -8,
                     c.align == LV_ALIGN_TOP_LEFT || c.align == LV_ALIGN_TOP_RIGHT ? 8 : -8);
    }

    static const lv_point_precise_t h_pts[] = {{0, 0}, {40, 0}};
    static const lv_point_precise_t v_pts[] = {{0, 0}, {0, 40}};

    g_probe.crosshair_h = lv_line_create(scr);
    lv_line_set_points(g_probe.crosshair_h, h_pts, 2);
    lv_obj_set_style_line_color(g_probe.crosshair_h, kAmber, 0);
    lv_obj_set_style_line_width(g_probe.crosshair_h, 2, 0);
    lv_obj_set_pos(g_probe.crosshair_h, -100, -100);

    g_probe.crosshair_v = lv_line_create(scr);
    lv_line_set_points(g_probe.crosshair_v, v_pts, 2);
    lv_obj_set_style_line_color(g_probe.crosshair_v, kAmber, 0);
    lv_obj_set_style_line_width(g_probe.crosshair_v, 2, 0);
    lv_obj_set_pos(g_probe.crosshair_v, -100, -100);

    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_screen_press, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(scr, on_screen_press, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(scr, on_screen_press, LV_EVENT_CLICKED, nullptr);
}
