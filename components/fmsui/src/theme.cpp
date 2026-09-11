#include "fmsui/theme.h"

#include <cassert>

#include "fmsui/element.h"

namespace fmsui {

FmsPalette defaultPalette() {
    FmsPalette p;
    p.background = Color::rgb(0x000000);
    p.surface = Color::rgb(0x14181C);
    p.border = Color::rgb(0x4A5560);

    p.label = Color::rgb(0xD8DCE0);
    p.entry = Color::rgb(0x29C5E8);
    p.computed = Color::rgb(0x1AE01A);
    p.attention = Color::rgb(0xFF9E1B);
    p.constraint = Color::rgb(0xE060E0);

    p.button = Color::rgb(0x2A3038);
    p.button_text = Color::rgb(0xD8DCE0);
    p.title_bar = Color::rgb(0xC8D0C8);
    p.title_text = Color::rgb(0x0A0E12);
    return p;
}

FmsMetrics defaultMetrics() {
    FmsMetrics m;
    m.row_height = 44;
    m.button_height = 48;
    m.tab_height = 34;
    m.tab_slant = 14;
    m.field_padding = 12;
    return m;
}

FmsMetrics denseMetrics() {
    /* A finger still has to hit these, so nothing goes below 34px: that is about
     * 3mm on a 294dpi panel, which is already at the limit. */
    FmsMetrics m;
    m.row_height = 36;
    m.button_height = 38;
    m.tab_height = 30;
    m.tab_slant = 12;
    m.field_padding = 8;
    return m;
}

const FmsThemeData &FmsTheme::of(BuildContext &ctx) {
    const Widget *w = Element::of(ctx).findAncestorInherited(widgetTypeOf<FmsTheme>());
    assert(w != nullptr && "FmsTheme::of() called from outside any FmsTheme");
    return static_cast<const FmsTheme *>(w)->data;
}

}  // namespace fmsui
