/* ACTIVE/F-PLN -- paging instead of scrolling, and the measurement for it.
 *
 * Built against the reference photograph of the A350 MFD flight plan page: a
 * header, a menu bar, eight waypoint lines under a pale title band, the TMPY
 * buttons, and the FACT row with the arrows in it.
 *
 * The panel is drawn at the proportions of the real screen and the controls sit
 * beside it, because this is an instrument as much as a page.  What it is here
 * to show is in the readout: stepping the window creates no lv_obj and moves
 * none, however long the plan is, because the rows are matched slot for slot.
 * Turn KEYS on -- the obvious thing to do to a list, and the wrong one here --
 * and each step destroys the row that left the top and builds the one that
 * arrived at the bottom instead.  On the Tab5 that is 11.6ms against 29.8ms per
 * step: a third of the frame budget against nine tenths of it.
 *
 * Note what the unkeyed 11.6ms is *not*: a floor.  Nothing is created and
 * nothing moves, but all 56 strings on screen are rewritten, and a label's text
 * costs about 0.14ms on the device.  A frame can read zero and zero and still
 * be most of a millisecond per line.
 */

#include "fplan_demo.h"

#include <cinttypes>  // uint32_t is `long unsigned int` on riscv32, so %u will not do

#include "fmsui_fonts.h"

using namespace fmsui;

namespace {

/* A leg of the plan.  Strings, not numbers, for the reason the other FMS pages
 * give: an MCDU field *is* a string -- FL105, 185, --:--. */
struct Leg {
    const char *via;    /* the airway or procedure into this fix */
    const char *ident;  /* the fix itself */
    const char *time;
    const char *spd;
    const char *alt;
    FmsRole alt_role;   /* magenta when it is a constraint, green when computed */
    const char *trk;
    const char *dist;
};

/* Long enough that the window is the only way through it, and taken from the
 * departure in the photograph. */
const Leg kPlan[] = {
    {"", "HAAB07R", "00:00", "---", "-----", FmsRole::Computed, "069", "1"},
    {"C072", "(8060)", "--:--", "---", "FL080", FmsRole::Constraint, "070", "4"},
    {"BEND1A", "10500", "--:--", "---", "FL105", FmsRole::Constraint, "179", "3"},
    {"BEND1A", "D111F", "--:--", "185", "-----", FmsRole::Computed, "225", "10"},
    {"BEND1A", "D193J", "--:--", "---", "-----", FmsRole::Computed, "194", "8"},
    {"BEND1A", "D194S", "--:--", "---", "FL125", FmsRole::Constraint, "191", "3"},
    {"BEND1A", "D194W", "--:--", "---", "-----", FmsRole::Computed, "251", "7"},
    {"BEND1A", "AD27B", "--:--", "---", "FL145", FmsRole::Constraint, "263", "9"},
    {"UM605", "MID", "00:14", "290", "FL180", FmsRole::Constraint, "271", "12"},
    {"UM605", "BOGNA", "00:19", "310", "FL240", FmsRole::Computed, "268", "18"},
    {"UM605", "KENET", "00:26", "320", "FL280", FmsRole::Computed, "274", "22"},
    {"UN862", "OCK", "00:33", "---", "FL310", FmsRole::Computed, "281", "16"},
    {"UN862", "BIG", "00:39", "---", "FL330", FmsRole::Computed, "277", "20"},
    {"UN862", "LAM", "00:46", "---", "FL350", FmsRole::Computed, "283", "25"},
    {"UY6", "DET", "00:54", "---", "FL370", FmsRole::Computed, "279", "31"},
    {"UY6", "TIGER", "01:03", "---", "FL370", FmsRole::Computed, "285", "28"},
    {"UY6", "SANDY", "01:11", "---", "FL370", FmsRole::Computed, "290", "34"},
    {"UL10", "WELIN", "01:20", "---", "FL370", FmsRole::Computed, "288", "26"},
    {"UL10", "NORRY", "01:28", "---", "FL350", FmsRole::Computed, "294", "30"},
    {"UL10", "BARTN", "01:36", "280", "FL290", FmsRole::Constraint, "301", "24"},
    {"UL10", "GORLO", "01:43", "250", "FL220", FmsRole::Constraint, "297", "19"},
    {"STAR2C", "TOBID", "01:50", "230", "FL140", FmsRole::Constraint, "305", "15"},
    {"STAR2C", "RIDSU", "01:56", "210", "8000", FmsRole::Constraint, "312", "11"},
    {"STAR2C", "OMAA27R", "02:04", "---", "-----", FmsRole::Computed, "", ""},
};
constexpr int kPlanCount = static_cast<int>(sizeof(kPlan) / sizeof(kPlan[0]));

/* Eight lines, which is what the reference screen shows. */
constexpr int kWindowRows = 8;

/* ---- One line of the plan ---------------------------------------------- */

/* Fixed column widths, because a flight plan that does not line up is unreadable
 * -- and because a cell whose width follows its contents would make the row's
 * layout depend on the data, which is the sort of thing that turns a step of the
 * window into a relayout of everything. */
Widget *cell(float width, const Str &text, const lv_font_t *font, Color color,
             TextAlign align = TextAlign::Left) {
    return new SizedBox{{
        .width = width,
        .child = new Text{{.text = text, .font = font, .color = color, .align = align}},
    }};
}

/* `leg` is null for a slot past the end of the plan.  The blank still draws --
 * every widget in the same place, only the strings emptied -- because a row that
 * disappeared would change the shape of the tree and cost the reconciler real
 * work for what is a text change.  The real screens dash their empty lines out
 * for the same reason a pilot needs them to: an absent line and a line with
 * nothing on it mean different things.
 *
 * `key` is threaded in only so the demo's KEYS toggle can put one here. In an
 * application these rows would simply have none. */
Widget *legRow(const FmsThemeData &t, const Leg *leg, Key key) {
    const char *via = leg != nullptr ? leg->via : "";
    const char *ident = leg != nullptr ? leg->ident : "";
    const char *time = leg != nullptr ? leg->time : "";
    const char *spd = leg != nullptr ? leg->spd : "";
    const char *alt = leg != nullptr ? leg->alt : "";
    const char *trk = leg != nullptr ? leg->trk : "";
    const char *dist = leg != nullptr ? leg->dist : "";
    const FmsRole alt_role = leg != nullptr ? leg->alt_role : FmsRole::Computed;

    /* The small line: which airway got us here, and the track and distance of
     * the leg. */
    Widget *upper = new Row{{
        .cross = CrossAxis::Center,
        .children = {
            new SizedBox{{.width = 24}},
            cell(120, via, t.font.unit, t.color.label),
            new Spacer{},
            cell(56, trk[0] != '\0' ? fmt("%s%s", trk, "\xC2\xB0") : Str(""), t.font.unit,
                 t.color.label, TextAlign::Right),
            cell(40, dist, t.font.unit, t.color.label, TextAlign::Right),
        },
    }};

    /* The line the pilot actually reads. */
    Widget *lower = new Row{{
        .cross = CrossAxis::Center,
        .children = {
            cell(150, ident, t.font.body, t.color.attention),
            cell(96, time, t.font.body, t.color.label),
            cell(72, spd, t.font.body, spd[0] == '-' || spd[0] == '\0'
                                           ? t.color.label
                                           : colorFor(t, FmsRole::Constraint)),
            cell(96, alt, t.font.body,
                 alt[0] == '-' || alt[0] == '\0' ? t.color.label : colorFor(t, alt_role)),
            new Spacer{},
        },
    }};

    return new Column{{
        .cross = CrossAxis::Stretch,
        .main_size = MainAxisSize::Min,
        .spacing = 0,
        .children = {upper, lower},
        .key = key,
    }};
}

/* ---- The page ----------------------------------------------------------- */

class FplanPage : public StatefulWidget {
public:
    FMSUI_WIDGET(FplanPage)
    StateBase *createState() const override;
};

class FplanPageState : public State<FplanPage> {
public:
    void initState() override {
        pos_.count = kPlanCount;
        pos_.window = kWindowRows;
        pos_.first = 0;
        pos_.step = 1;
    }

    Widget *build(BuildContext &ctx) override {
        const FmsThemeData &t = FmsTheme::of(ctx);

        /* 8, not 16: the reference page is a dense one and eight lines plus the
         * TMPY buttons plus the FACT row very nearly fill 720 on their own. */
        return new Container{{
            .padding = EdgeInsets::all(8),
            .color = t.color.background,
            /* Stretch, not Start: FmsScaffold puts its body in an Expanded, and
             * an Expanded under a loose height has no share to take -- the
             * footer then lands on top of the FACT row instead of below it. */
            .child = new Row{{
                .cross = CrossAxis::Stretch,
                .spacing = 24,
                .children = {
                    new SizedBox{{.width = 620, .child = screen(t)}},
                    new Expanded{{.child = controls(t)}},
                },
            }},
        }};
    }

private:
    /* ---- the F-PLN screen itself ---- */

    Widget *screen(const FmsThemeData &t) {
        /* The arrows.  One position, two possible homes, and the page decides
         * which -- the widget has no opinion, which is what makes the choice
         * available at all. */
        Widget *pager = new FmsPager{{
            .pos = pos_,
            .on_prev = [this] { setState([&] { pos_.prev(); }); },
            .on_next = [this] { setState([&] { pos_.next(); }); },
        }};

        WidgetList footer =
            footer_pager_
                ? WidgetList{new FmsButton{{.text = "INIT"}},
                             new FmsButton{{.text = "F-PLN", .text2 = "INFO"}},
                             new FmsButton{{.text = "MSG", .text2 = "LIST"}}, new Spacer{}, pager}
                : WidgetList{new FmsButton{{.text = "INIT"}},
                             new FmsButton{{.text = "F-PLN", .text2 = "INFO"}},
                             new FmsButton{{.text = "MSG", .text2 = "LIST"}}, new Spacer{}};

        return new FmsScaffold{{
            .title = "FMS 1",
            .subtitle = "ETH350",
            .child = new Column{{
                .cross = CrossAxis::Stretch,
                .main_size = MainAxisSize::Min,
                .spacing = 4,
                .children = {menuBar(), plan(t), new FmsDivider{}, factRow(t, pager)},
            }},
            .footer = footer,
        }};
    }

    Widget *menuBar() {
        return new Row{{
            .cross = CrossAxis::Center,
            .spacing = 6,
            .children = {
                new FmsDropdown{{.text = "ACTIVE", .role = FmsRole::Label}},
                new FmsDropdown{{.text = "POSITION", .role = FmsRole::Label}},
                new FmsDropdown{{.text = "SEC INDEX", .role = FmsRole::Label}},
                new FmsDropdown{{.text = "DATA", .role = FmsRole::Label}},
                new Spacer{},
            },
        }};
    }

    Widget *plan(const FmsThemeData &t) {
        /* The pale band, with TMPY sitting in it as on the real page. */
        Widget *title = new Container{{
            .height = t.metric.row_height * 0.8F,
            .padding = EdgeInsets::symmetric(t.metric.field_padding, 0),
            .color = t.color.title_bar,
            .alignment = Alignment::centerLeft(),
            .child = new Row{{
                .cross = CrossAxis::Center,
                .children = {
                    new Text{{.text = "ACTIVE/F-PLN", .font = t.font.body,
                              .color = t.color.title_text}},
                    new Spacer{},
                    new Text{{.text = "TMPY", .font = t.font.label,
                              .color = t.color.attention}},
                },
            }},
        }};

        Widget *headings = new Row{{
            .cross = CrossAxis::Center,
            .children = {
                cell(150, "FROM", t.font.label, t.color.label),
                cell(96, "TIME", t.font.label, t.color.label),
                cell(72, "SPD", t.font.label, t.color.label),
                cell(96, "ALT", t.font.label, t.color.label),
                new Spacer{},
                cell(120, "TRK DIST FPA", t.font.unit, t.color.label, TextAlign::Right),
            },
        }};

        /* The window.  Eight rows, always: whether the plan has 24 legs or four
         * hundred, and whether the bottom of it is in view or not. */
        Widget *window = new FmsWindow{{
            .pos = pos_,
            .spacing = 2,
            .row =
                [this, &t](int index, int slot) {
                    (void)slot;
                    const Leg *leg = index >= 0 ? &kPlan[index] : nullptr;
                    /* Keys here are the demo, not the recommendation -- see the
                     * caption under the readout. */
                    return legRow(t, leg, keyed_ && index >= 0 ? Key{index} : Key{});
                },
        }};

        return new Column{{
            .cross = CrossAxis::Stretch,
            .main_size = MainAxisSize::Min,
            .spacing = 4,
            .children = {
                title,
                headings,
                window,
                new FmsDivider{},
                new Row{{
                    .cross = CrossAxis::Center,
                    .children = {
                        new FmsButton{{.text = "ERASE", .text2 = "TMPY",
                                       .role = FmsRole::Attention}},
                        new Spacer{},
                        new FmsButton{{.text = "INSERT", .text2 = "TMPY",
                                       .role = FmsRole::Entry}},
                    },
                }},
            },
        }};
    }

    /* FACT, and the arrows when they belong to the list rather than the page. */
    Widget *factRow(const FmsThemeData &t, Widget *pager) {
        WidgetList children =
            footer_pager_
                ? WidgetList{new FmsButton{{.text = "FACT"}}, new Spacer{},
                             new Text{{.text = "--:--   ---.- T   ---- NM", .font = t.font.label,
                                       .color = t.color.computed}},
                             new Spacer{},
                             new FmsButton{{.text = "DEST", .enabled = false}}}
                : WidgetList{new FmsButton{{.text = "FACT"}}, new Spacer{},
                             new Text{{.text = "--:--   ---.- T   ---- NM", .font = t.font.label,
                                       .color = t.color.computed}},
                             new Spacer{}, pager,
                             new FmsButton{{.text = "DEST", .enabled = false}}};

        return new Row{{.cross = CrossAxis::Center, .spacing = 10, .children = children}};
    }

    /* ---- the instrument beside it ---- */

    Widget *controls(const FmsThemeData &t) {
        const FrameStats &s = FmsApp::instance().stats();

        /* "PREV FRAME" is what these are, not a hedge: FmsApp writes the stats
         * after paint and this build runs before it, so an interaction can never
         * show its own cost.  Step twice, or read the serial log. */
        Widget *readout = new Column{{
            .cross = CrossAxis::Stretch,
            .main_size = MainAxisSize::Min,
            .spacing = 2,
            .children = {
                new Text{{.text = "PREV FRAME", .font = t.font.label, .color = t.color.label}},
                new Text{{.text = fmt("widgets   %" PRIu32, s.widgets), .font = t.font.unit,
                          .color = t.color.label}},
                new Text{{.text = fmt("lv_objs   %" PRIu32, s.lv_objects), .font = t.font.unit,
                          .color = t.color.label}},
                /* The two that carry the claim.  Everything else on this panel
                 * is context for them. */
                new Text{{.text = fmt("lv_created %" PRIu32, s.lv_created), .font = t.font.label,
                          .color = s.lv_created == 0 ? t.color.computed : t.color.attention}},
                new Text{{.text = fmt("lv_moved   %" PRIu32, s.lv_moved), .font = t.font.label,
                          .color = s.lv_moved == 0 ? t.color.computed : t.color.attention}},
                new Text{{.text = fmt("build  %" PRIu32 " us", s.build_us), .font = t.font.unit,
                          .color = t.color.label}},
                new Text{{.text = fmt("layout %" PRIu32 " us", s.layout_us), .font = t.font.unit,
                          .color = t.color.label}},
                new Text{{.text = fmt("paint  %" PRIu32 " us", s.paint_us), .font = t.font.unit,
                          .color = t.color.label}},
            },
        }};

        return new Column{{
            .cross = CrossAxis::Stretch,
            .main_size = MainAxisSize::Min,
            .spacing = 10,
            .children = {
                new Text{{.text = fmt("LEG %d-%d OF %d", pos_.first + 1,
                                      pos_.first + pos_.window < pos_.count
                                          ? pos_.first + pos_.window
                                          : pos_.count,
                                      pos_.count),
                          .font = t.font.body, .color = t.color.entry}},
                new FmsDivider{},
                readout,
                new FmsDivider{},
                new FmsButton{{.text = keyed_ ? "KEYS  ON" : "KEYS  OFF",
                               .role = keyed_ ? FmsRole::Attention : FmsRole::Computed,
                               .on_tap = [this] { setState([&] { keyed_ = !keyed_; }); }}},
                new Text{{.text = keyed_ ? "KEYED: A ROW DIES, A ROW IS BUILT"
                                         : "UNKEYED: ONLY THE TEXT CHANGES",
                          .font = t.font.unit,
                          .color = keyed_ ? t.color.attention : t.color.computed}},
                new SizedBox{{.height = 6}},
                new FmsButton{{.text = pos_.step == 1 ? "STEP  LEG" : "STEP  PAGE",
                               .role = FmsRole::Entry,
                               .on_tap =
                                   [this] {
                                       setState([&] {
                                           pos_.step = pos_.step == 1 ? kWindowRows : 1;
                                       });
                                   }}},
                new SizedBox{{.height = 6}},
                new FmsButton{{.text = footer_pager_ ? "ARROWS FOOTER" : "ARROWS  LIST",
                               .role = FmsRole::Label,
                               .on_tap =
                                   [this] {
                                       setState([&] { footer_pager_ = !footer_pager_; });
                                   }}},
                new Text{{.text = footer_pager_ ? "PAGING EVERYTHING BELOW THE TITLE"
                                                : "PAGING THE LIST ONLY",
                          .font = t.font.unit,
                          .color = t.color.label}},
            },
        }};
    }

    FmsWindowPos pos_{};
    bool keyed_ = false;         /* off: the way an application should have it */
    bool footer_pager_ = false;  /* off: arrows in the FACT row, as on the photo */
};

StateBase *FplanPage::createState() const { return new FplanPageState(); }

}  // namespace

Widget *fplan_demo_build() {
    FmsThemeData data;
    data.color = defaultPalette();
    /* One step down the ladder from ACTIVE/PERF.  Eight waypoint lines, each of
     * them two rows of type, plus the TMPY buttons and the FACT row do not fit
     * 720 at 24px -- and a flight plan page that scrolls to show its own footer
     * would rather defeat the point of this screen. */
    data.font = FmsTypography{
        .unit = &fms_b612_mono_16,
        .label = &fms_b612_mono_16,
        .body = &fms_b612_mono_20,
        .value = &fms_b612_mono_20,
        .title = &fms_b612_mono_24,
    };
    data.metric = denseMetrics();

    return new FmsTheme{{.data = data, .child = new FplanPage()}};
}
