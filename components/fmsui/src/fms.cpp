#include "fmsui/fms.h"

#include <cassert>
#include <cstring>
#include <utility>
#include <vector>

namespace fmsui {

Color colorFor(const FmsThemeData &t, FmsRole role) {
    switch (role) {
        case FmsRole::Entry:
            return t.color.entry;
        case FmsRole::Computed:
            return t.color.computed;
        case FmsRole::Attention:
            return t.color.attention;
        case FmsRole::Constraint:
            return t.color.constraint;
        case FmsRole::Label:
            break;
    }
    return t.color.label;
}

/* ---- Text -------------------------------------------------------------- */

Widget *FmsLabel::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);
    return new Text{{.text = args_.text, .font = t.font.label, .color = t.color.label}};
}

Widget *FmsValue::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);
    const Color c = colorFor(t, args_.role);

    if (args_.unit.empty()) {
        return new Text{{.text = args_.text, .font = t.font.value, .color = c}};
    }
    return new Row{{
        .cross = CrossAxis::End,
        .main_size = MainAxisSize::Min,
        .spacing = 5,
        .children = {
            new Text{{.text = args_.text, .font = t.font.value, .color = c}},
            new Text{{.text = args_.unit, .font = t.font.unit, .color = c}},
        },
    }};
}

/* ---- Fields and controls ----------------------------------------------- */

namespace {

/* The frame every field, button and dropdown sits in. */
Widget *framed(const FmsThemeData &t, Color fill, EdgeInsets pad, float height, Widget *child) {
    return new Container{{
        .height = height,
        .padding = pad,
        .color = fill,
        .border_color = t.color.border,
        .border_width = 1,
        .alignment = Alignment::center(),
        .child = child,
    }};
}

}  // namespace

Widget *FmsFieldBox::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);
    const Color c = colorFor(t, args_.role);

    /* An empty field is dashes, not blank: the real screens always show the shape
     * of what is missing. */
    Widget *content =
        args_.empty
            ? static_cast<Widget *>(
                  new Text{{.text = "-----", .font = t.font.value, .color = c}})
            : new FmsValue{{.text = args_.text, .unit = args_.unit, .role = args_.role}};

    Widget *box = framed(t, t.color.surface, EdgeInsets::symmetric(t.metric.field_padding, 0),
                         t.metric.row_height, content);

    /* The GestureDetector is always here, even with nothing to call.  A wrapper
     * that comes and goes changes the shape of the subtree, and the reconciler
     * matches slot for slot: the slot that held a GestureDetector would then
     * hold a Container, the types would not match, and everything below it --
     * every lv_obj included -- would be destroyed and built again.  An empty
     * callback costs a comparison in handleEvent; a rebuild costs ~1ms per
     * lv_obj on the device. */
    return new GestureDetector{{.on_tap = args_.on_tap, .child = box}};
}

Widget *FmsButton::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);

    const Color text_color =
        args_.enabled ? colorFor(t, args_.role) : t.color.border;  /* greyed out */

    Widget *content;
    if (args_.text2.empty()) {
        content = new Text{{.text = args_.text,
                            .font = t.font.label,
                            .color = text_color,
                            .align = TextAlign::Center}};
    } else {
        content = new Column{{
            .main = MainAxis::Center,
            .cross = CrossAxis::Center,
            .main_size = MainAxisSize::Min,
            .spacing = 0,
            .children = {
                new Text{{.text = args_.text, .font = t.font.label, .color = text_color,
                          .align = TextAlign::Center}},
                new Text{{.text = args_.text2, .font = t.font.label, .color = text_color,
                          .align = TextAlign::Center}},
            },
        }};
    }

    Widget *box = new Container{{
        .height = args_.text2.empty() ? t.metric.button_height : t.metric.button_height * 1.7F,
        .padding = EdgeInsets::symmetric(12, 2),
        .color = t.color.button,
        .border_color = t.color.border,
        .border_width = 1,
        .alignment = Alignment::center(),
        .child = content,
    }};

    /* Disabled varies only the callback, never the shape.  A button that loses
     * its GestureDetector when it greys out takes its Container and Text with
     * it -- three lv_objs recreated for what is a colour change. */
    return new GestureDetector{{
        .on_tap = args_.enabled ? args_.on_tap : VoidCallback{},
        .child = box,
    }};
}

namespace {

/* The triangle, drawn rather than typed: B612 Mono has no U+25BC, and a drawn
 * one recolours and scales with the widget instead of being stuck at whatever
 * the font thinks its size is. */
Widget *dropdownArrow(Color marker, float height) {
    return new CustomPaint{{
        .painter =
            [marker](Canvas &canvas, Size size) {
                const float w = 12;
                const float h = 7;
                const float x = (size.width - w) / 2;
                const float y = (size.height - h) / 2;
                const Offset pts[3] = {{x, y}, {x + w, y}, {x + w / 2, y + h}};
                canvas.polygon(pts, 3, marker, Color::transparent(), 0);
            },
        .preferred = Size{16, height},
    }};
}

}  // namespace

class FmsDropdownState : public State<FmsDropdown> {
public:
    Widget *build(BuildContext &ctx) override {
        const FmsThemeData &t = FmsTheme::of(ctx);
        const FmsDropdownArgs &a = widget().args;

        const bool has_items = a.items != nullptr && a.count > 0;
        const bool valid = has_items && a.index >= 0 && a.index < static_cast<int>(a.count);

        const Str shown = valid ? Str(a.items[a.index]) : a.text;
        const Color role_color = colorFor(t, a.role);

        /* Open, the box shows the selection the way the list does: filled, with
         * dark text on it. That is what the real screen does, and it is what
         * ties the box to the list hanging below it. */
        const Color box_fill = open_ ? role_color : t.color.surface;
        const Color text_color = open_ ? t.color.background : role_color;

        Widget *box = new Container{{
            .height = t.metric.row_height,
            .padding = EdgeInsets::only(t.metric.field_padding, 0, 6, 0),
            .color = box_fill,
            .border_color = t.color.border,
            .border_width = 1,
            .child = new Row{{
                .cross = CrossAxis::Center,
                .main_size = MainAxisSize::Min,
                .spacing = 8,
                .children = {
                    new Text{{.text = shown, .font = t.font.body, .color = text_color}},
                    dropdownArrow(open_ ? t.color.background : t.color.label,
                                  t.metric.row_height),
                },
            }},
        }};

        Widget *tappable = new GestureDetector{{
            .on_tap =
                [this, has_items, on_tap = a.on_tap] {
                    if (has_items) {
                        setState([&] { open_ = !open_; });
                    } else if (on_tap) {
                        on_tap();
                    }
                },
            .child = box,
        }};

        const bool showing = open_ && has_items;

        /* The list hangs off the bottom of the box, over whatever is below it.
         * A Layer takes no space where it sits, so opening the list does not
         * shove the row around.
         *
         * Stretch, so the Layer inherits the box's width as a minimum and the
         * popup comes out at least as wide as the control it hangs from. Under a
         * loose constraint Stretch shrink-wraps to the widest child, which here
         * is the box.
         *
         * The Column and the Layer are here whether the list is showing or not.
         * Returning the bare GestureDetector when closed would make opening the
         * list change the shape of the subtree, and the box -- which does not
         * change at all -- would be destroyed and rebuilt both on the way open
         * and on the way shut. Only the popup below the Layer comes and goes
         * now: an empty Layer takes no space, paints nothing, and without a
         * barrier callback puts no full-screen catcher on the screen either. */
        return new Column{{
            .cross = CrossAxis::Stretch,
            .main_size = MainAxisSize::Min,
            .children = {
                tappable,
                new Layer{{
                    .on_barrier_tap = showing ? VoidCallback([this] {
                                                    setState([&] { open_ = false; });
                                                })
                                              : VoidCallback{},
                    .child = showing ? popup(t, a) : nullptr,
                }},
            },
        }};
    }

private:
    Widget *popup(const FmsThemeData &t, const FmsDropdownArgs &a) {
        std::vector<Widget *> rows;
        rows.reserve(a.count);

        for (size_t i = 0; i < a.count; i++) {
            const bool current = static_cast<int>(i) == a.index;
            const int index = static_cast<int>(i);
            const auto on_selected = a.on_selected;

            rows.push_back(new GestureDetector{{
                .on_tap =
                    [this, index, on_selected] {
                        setState([&] { open_ = false; });
                        if (on_selected) on_selected(index);
                    },
                /* A Row rather than `.alignment = centerLeft`: an Align fills the
                 * width it is offered whenever that width is bounded, which would
                 * make every item -- and so the popup -- as wide as the screen.
                 * A Row with MainAxisSize::Min shrink-wraps instead, and its
                 * CrossAxis::Center still centres the text in the row's height. */
                .child = new Container{{
                    .height = t.metric.row_height,
                    .padding = EdgeInsets::symmetric(t.metric.field_padding, 0),
                    .color = current ? colorFor(t, a.role) : Color::transparent(),
                    .child = new Row{{
                        .cross = CrossAxis::Center,
                        .main_size = MainAxisSize::Min,
                        .children = {new Text{{.text = a.items[i],
                                               .font = t.font.body,
                                               .color = current ? t.color.background
                                                                : t.color.label}}},
                    }},
                }},
            }});
        }

        return new Container{{
            .color = t.color.button,
            .border_color = t.color.border,
            .border_width = 1,
            .child = new Column{{
                .cross = CrossAxis::Stretch,
                .main_size = MainAxisSize::Min,
                .children = WidgetList(rows.data(), rows.size()),
            }},
        }};
    }

    bool open_ = false;
};

StateBase *FmsDropdown::createState() const { return new FmsDropdownState(); }

Widget *FmsRadio::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);

    const bool on = args_.selected;
    const Color ring = on ? t.color.entry : t.color.border;
    const Color dot = on ? t.color.entry : Color::transparent();

    Widget *knob = new CustomPaint{{
        .painter =
            [ring, dot](Canvas &canvas, Size size) {
                const Offset c{size.width / 2, size.height / 2};
                canvas.circle(c, 9, Color::transparent(), ring, 2);
                canvas.circle(c, 4, dot, Color::transparent(), 0);
            },
        .preferred = Size{24, 24},
    }};

    Widget *row = new Row{{
        .cross = CrossAxis::Center,
        .main_size = MainAxisSize::Min,
        .spacing = 10,
        .children = {
            knob,
            new Text{{.text = args_.text,
                      .font = t.font.body,
                      .color = on ? t.color.entry : t.color.label}},
        },
    }};

    /* Both wrappers are unconditional, so a radio that gains or loses its
     * callback keeps its Elements.  The padding is here either way: it is what
     * makes the target something a finger can hit, and dropping it for a
     * display-only radio would misalign it against the tappable ones beside
     * it. */
    return new GestureDetector{{
        .on_tap = args_.on_tap,
        .child = new Container{{.padding = EdgeInsets::symmetric(4, 8), .child = row}},
    }};
}

/* ---- Structure --------------------------------------------------------- */

Widget *FmsDivider::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);
    const float inset = args_.inset;
    const Color c = t.color.border;

    return new CustomPaint{{
        .painter =
            [c, inset](Canvas &canvas, Size size) {
                canvas.line(Offset{inset, size.height / 2},
                            Offset{size.width - inset, size.height / 2}, c, 1);
            },
        .preferred = Size{0, 9},
    }};
}

Widget *FmsPanel::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);

    return new Column{{
        .cross = CrossAxis::Stretch,
        .main_size = MainAxisSize::Min,
        .children = {
            /* The pale band. Dark text on light, the one inversion on the screen,
             * which is what makes it read as a heading. */
            new Container{{
                .height = 30,
                .padding = EdgeInsets::symmetric(10, 0),
                .color = t.color.title_bar,
                .alignment = Alignment::centerLeft(),
                .child = new Text{{.text = args_.title,
                                   .font = t.font.label,
                                   .color = t.color.title_text}},
            }},
            new Expanded{{
                .child = new Container{{
                    .padding = EdgeInsets::all(10),
                    .border_color = t.color.border,
                    .border_width = 1,
                    .child = args_.child,
                }},
            }},
        },
    }};
}

/* ---- Tabs -------------------------------------------------------------- */

Widget *FmsTabs::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);
    const float slant = t.metric.tab_slant;

    std::vector<Widget *> items;
    items.reserve(args_.count);

    for (size_t i = 0; i < args_.count; i++) {
        const bool selected = static_cast<int>(i) == args_.index;
        const Color face = selected ? t.color.surface : t.color.button;
        const Color edge = t.color.border;
        const Color text = selected ? t.color.computed : t.color.label;

        Widget *shape = new CustomPaint{{
            .painter =
                [face, edge, selected, slant](Canvas &canvas, Size size) {
                    /* A trapezoid: the left edge leans in, the right edge is
                     * vertical, so a row of them reads as overlapping cards the
                     * way the real tabs do. The selected tab's bottom edge is
                     * left open, joining it to the panel below -- that gap is
                     * the whole visual trick.
                     *
                     * Inset by half a stroke: a 1px line centred on y=0 puts half
                     * of itself outside the object, where LVGL clips it away, and
                     * the tab loses its top edge. */
                    const float i = 0.5F;
                    const Offset pts[4] = {
                        {slant + i, i},
                        {size.width - i, i},
                        {size.width - i, size.height},
                        {i, size.height},
                    };
                    canvas.polygon(pts, 4, face, Color::transparent(), 0);

                    canvas.line(pts[0], pts[1], edge, 1);  // top
                    canvas.line(pts[1], pts[2], edge, 1);  // right
                    canvas.line(pts[3], pts[0], edge, 1);  // the slant
                    if (!selected) {
                        canvas.line(pts[2], pts[3], edge, 1);  // bottom
                    }
                },
            .child = new Container{{
                .height = t.metric.tab_height,
                .padding = EdgeInsets::only(slant + 10, 0, 12, 0),
                .alignment = Alignment::center(),
                .child = new Text{{.text = args_.labels[i], .font = t.font.label, .color = text}},
            }},
        }};

        const auto on_changed = args_.on_changed;
        const int index = static_cast<int>(i);
        items.push_back(new GestureDetector{{
            .on_tap =
                [on_changed, index] {
                    if (on_changed) on_changed(index);
                },
            .child = shape,
        }});
    }

    return new Row{{
        .cross = CrossAxis::End,
        .main_size = MainAxisSize::Min,
        .spacing = -slant + 2,  /* overlap, so the slants tuck under each other */
        .children = WidgetList(items.data(), items.size()),
    }};
}

/* ---- Scaffold ---------------------------------------------------------- */

Widget *FmsScaffold::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);

    return new Container{{
        .padding = EdgeInsets::all(8),
        .color = t.color.background,
        .child = new Column{{
            .cross = CrossAxis::Stretch,
            .spacing = 6,
            .children = {
                new Row{{
                    .cross = CrossAxis::Center,
                    .children = {
                        new Text{{.text = args_.title, .font = t.font.body,
                                  .color = t.color.entry}},
                        new Spacer{},
                        new Text{{.text = args_.subtitle, .font = t.font.body,
                                  .color = t.color.label}},
                    },
                }},
                new Expanded{{.child = args_.child}},
                new Row{{
                    .cross = CrossAxis::End,
                    .spacing = 10,
                    .children = args_.footer,
                }},
            },
        }},
    }};
}

/* ---- Paging ------------------------------------------------------------ */

Widget *FmsWindow::build(BuildContext &ctx) const {
    (void)FmsTheme::of(ctx);  // the rows read it; assert we are inside one
    assert(args_.row && "FmsWindow without a row builder has nothing to draw");

    const int n = args_.pos.window > 0 ? args_.pos.window : 0;

    std::vector<Widget *> rows;
    rows.reserve(static_cast<size_t>(n));
    for (int slot = 0; slot < n; slot++) {
        Widget *w = args_.row(args_.pos.at(slot), slot);
        /* A null row would quietly shorten the Column, which is the one shape
         * change the blank slots exist to prevent. */
        assert(w != nullptr && "FmsWindow row builder must return a widget for every slot");
        rows.push_back(w);
    }

    /* No keys: see the class comment.  Slot-for-slot is the whole trick. */
    return new Column{{
        .cross = CrossAxis::Stretch,
        .main_size = MainAxisSize::Min,
        .spacing = args_.spacing,
        .children = WidgetList(rows.data(), rows.size()),
    }};
}

namespace {

/* One arrow in its box.  Drawn rather than typed, like the dropdown's triangle
 * and for the same reason: B612 Mono has no U+25B2/U+25BC, and a drawn one takes
 * the widget's colour instead of the font's size. */
Widget *pagerArrow(const FmsThemeData &t, bool down, bool live, VoidCallback on_tap) {
    const Color mark = live ? t.color.entry : t.color.border;

    Widget *glyph = new CustomPaint{{
        .painter =
            [mark, down](Canvas &canvas, Size size) {
                const float w = 20;
                const float h = 11;
                const float x = (size.width - w) / 2;
                const float y = (size.height - h) / 2;
                const Offset up[3] = {{x + w / 2, y}, {x + w, y + h}, {x, y + h}};
                const Offset dn[3] = {{x, y}, {x + w, y}, {x + w / 2, y + h}};
                canvas.polygon(down ? dn : up, 3, mark, Color::transparent(), 0);
            },
        .preferred = Size{24, 12},
    }};

    /* Wider than the glyph on purpose.  The triangle is small because that is
     * how the reference screen draws it; the box around it is finger-sized
     * because this one is a touch panel and the arrow is the only way through a
     * long flight plan. */
    Widget *box = new Container{{
        .width = t.metric.button_height * 1.2F,
        .height = t.metric.button_height,
        .color = t.color.button,
        .border_color = t.color.border,
        .border_width = 1,
        .alignment = Alignment::center(),
        .child = glyph,
    }};

    /* Greying out varies the callback and the colour, never the shape -- the
     * same rule FmsButton follows, and it matters more here, because these two
     * cross between live and dead every time the window reaches an end. */
    return new GestureDetector{{
        .on_tap = live ? std::move(on_tap) : VoidCallback{},
        .child = box,
    }};
}

}  // namespace

Widget *FmsPager::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);

    return new Row{{
        .cross = CrossAxis::Center,
        .main_size = MainAxisSize::Min,
        .spacing = 6,
        .children = {
            pagerArrow(t, true, args_.pos.canNext(), args_.on_next),
            pagerArrow(t, false, args_.pos.canPrev(), args_.on_prev),
        },
    }};
}

/* ---- Scratchpad and keypad --------------------------------------------- */

Widget *FmsScratchpad::build(BuildContext &ctx) const {
    const FmsThemeData &t = FmsTheme::of(ctx);

    const bool showing_message = !args_.message.empty();
    const Str text = showing_message ? args_.message : args_.text;
    const Color c = showing_message
                        ? (args_.error ? t.color.attention : t.color.label)
                        : t.color.entry;

    return new Container{{
        .height = t.metric.row_height,
        .padding = EdgeInsets::symmetric(t.metric.field_padding, 0),
        .color = t.color.surface,
        .border_color = args_.error ? t.color.attention : t.color.border,
        .border_width = 1,
        .alignment = Alignment::centerLeft(),
        .child = new Text{{.text = text, .font = t.font.value, .color = c}},
    }};
}

Widget *FmsKeypad::build(BuildContext &ctx) const {
    (void)FmsTheme::of(ctx);  // assert we are inside a theme; the buttons read it

    static const char *const kDigitRows[] = {"789", "456", "123", ".0/"};
    static const char *const kLetterRows[] = {"ABCDEFG", "HIJKLMN", "OPQRSTU", "VWXYZ-/"};

    const char *const *rows = args_.letters ? kLetterRows : kDigitRows;
    constexpr size_t kRowCount = 4;

    const auto on_key = args_.on_key;

    std::vector<Widget *> row_widgets;
    row_widgets.reserve(kRowCount + 1);

    for (size_t r = 0; r < kRowCount; r++) {
        const char *chars = rows[r];
        const size_t n = std::strlen(chars);

        std::vector<Widget *> keys;
        keys.reserve(n);
        for (size_t i = 0; i < n; i++) {
            const char ch = chars[i];
            keys.push_back(new Expanded{{
                .child = new FmsButton{{
                    .text = Str(std::string_view(&chars[i], 1)),
                    .on_tap =
                        [on_key, ch] {
                            if (on_key) on_key(ch);
                        },
                }},
            }});
        }

        row_widgets.push_back(new Row{{
            .spacing = 6,
            .children = WidgetList(keys.data(), keys.size()),
        }});
    }

    /* CLR is amber because it is the one key that throws work away. */
    row_widgets.push_back(new Row{{
        .spacing = 6,
        .children = {
            new Expanded{{.child = new FmsButton{{.text = "DEL", .on_tap = args_.on_backspace}}}},
            new Expanded{{.child = new FmsButton{{.text = "CLR",
                                                  .role = FmsRole::Attention,
                                                  .on_tap = args_.on_clear}}}},
            new Expanded{{.flex = 2,
                          .child = new FmsButton{{.text = "ENTER",
                                                  .role = FmsRole::Computed,
                                                  .on_tap = args_.on_enter}}}},
        },
    }});

    return new Column{{
        .cross = CrossAxis::Stretch,
        .main_size = MainAxisSize::Min,
        .spacing = 6,
        .children = WidgetList(row_widgets.data(), row_widgets.size()),
    }};
}

}  // namespace fmsui
