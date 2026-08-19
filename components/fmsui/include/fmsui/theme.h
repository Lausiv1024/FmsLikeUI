#pragma once

/* The FMS theme: colours and type, handed down the tree.
 *
 * The colour names are semantic, not descriptive, because on a real Airbus FMS
 * the colour *is* the meaning: cyan is a value the pilot may enter, green is one
 * the system computed, amber wants attention, magenta is a constraint.  Calling
 * the field `cyan` would let a caller pick it for the wrong reason.
 */

#include "fmsui/foundation.h"
#include "fmsui/widget.h"
#include "lvgl.h"

namespace fmsui {

struct FmsPalette {
    Color background;   /* the screen behind everything */
    Color surface;      /* panel and field fill */
    Color border;       /* 1px frames and rules */

    Color label;        /* static text: names of things */
    Color entry;        /* a value the pilot may enter          (cyan) */
    Color computed;     /* a value the system worked out        (green) */
    Color attention;    /* wants action, or is out of the norm  (amber) */
    Color constraint;   /* imposed from outside                 (magenta) */

    Color button;       /* pushbutton face */
    Color button_text;
    Color title_bar;    /* the ACTIVE/PERF band */
    Color title_text;
};

/* The size ladder. `unit` is the small suffix after a value ("KT", "FT"), which
 * on the real screens is noticeably smaller than the number it follows. */
struct FmsTypography {
    const lv_font_t *unit;
    const lv_font_t *label;
    const lv_font_t *body;
    const lv_font_t *value;
    const lv_font_t *title;
};

/* Sizes, so a dense page can be dense.  These started as constants inside the
 * widgets, which meant an FMS page could not be made to fit 720px without
 * editing the widget set. */
struct FmsMetrics {
    float row_height;     /* fields, dropdowns */
    float button_height;
    float tab_height;
    float tab_slant;      /* how far a tab's left edge leans */
    float field_padding;  /* horizontal padding inside a field or dropdown */
};

struct FmsThemeData {
    FmsPalette color;
    FmsTypography font;
    FmsMetrics metric;
};

/* The palette read off the reference screenshots.  Fonts are filled in by the
 * caller, because the framework must not depend on which fonts got generated. */
FmsPalette defaultPalette();

/* Comfortable: what a widget catalogue or a sparse page wants. */
FmsMetrics defaultMetrics();

/* Tight: what a real FMS page needs to fit 1280x720. */
FmsMetrics denseMetrics();

class FmsTheme : public InheritedWidget {
public:
    struct Args {
        FmsThemeData data;
        Widget *child = nullptr;
    };

    explicit FmsTheme(Args a) : data(a.data) { child = a.child; }
    FMSUI_WIDGET(FmsTheme)

    FmsThemeData data;

    /* Nearest enclosing theme.  Asserts if there is none: a widget that reads
     * the theme and finds nothing has been put somewhere it cannot work, and
     * silently returning a default would hide that. */
    static const FmsThemeData &of(BuildContext &ctx);
};

}  // namespace fmsui
