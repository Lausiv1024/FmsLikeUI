/* Keyed reconciliation, shown rather than asserted -- and measured.
 *
 * A flight plan whose legs can be moved.  Each leg is a StatefulWidget whose
 * State stamps itself with a serial number the first time it is built -- a value
 * that exists nowhere in the widget's configuration, so it can only survive a
 * move if the Element survived with it.
 *
 * KEYS can be switched off, which is the point of the screen: with keys, moving
 * a leg moves its stamp and creates no lv_objs; without them the stamps stay
 * where they are while the names slide past, because a slot-for-slot match can
 * only ever hand the data to whichever Element is already standing there.
 *
 * REVERSE is here for the measurement rather than the demonstration.  Swapping
 * two adjacent legs barely disturbs the flat LVGL tree -- only those two rows'
 * objects change index -- so it says nothing about how a reorder scales.
 * Reversing the whole plan gives every lv_obj a new index at once, which is the
 * worst case, and `--rows N --stats` prints what it cost.
 */

#include "reorder_demo.h"

#include <cinttypes>  // uint32_t is `long unsigned int` on riscv32, so %u will not do
#include <string>
#include <vector>

#include "fmsui_fonts.h"

using namespace fmsui;

namespace {

/* Handed out on first build and never again, so a stamp identifies an Element
 * rather than the data it happens to be showing. */
int g_next_stamp = 0;

struct Leg {
    std::string name;
    std::string via;
    int altitude;
};

const char *const kNames[] = {"MID", "BOGNA", "KENET", "OCK", "BIG", "LAM", "DET", "TIGER"};
const char *const kAirways[] = {"UM605", "UN862", "UY6", "UL10"};
constexpr size_t kNameCount = sizeof(kNames) / sizeof(kNames[0]);
constexpr size_t kAirwayCount = sizeof(kAirways) / sizeof(kAirways[0]);

/* ---- One leg ------------------------------------------------------------ */

struct LegRowArgs {
    const Leg *leg = nullptr;
    bool selected = false;
    bool compact = false;
    VoidCallback on_tap{};
    Key key{};
};

class LegRow : public StatefulWidget {
public:
    explicit LegRow(LegRowArgs a) : args(std::move(a)) { key = args.key; }
    FMSUI_WIDGET(LegRow)
    StateBase *createState() const override;

    LegRowArgs args;
};

class LegRowState : public State<LegRow> {
public:
    void initState() override { stamp_ = ++g_next_stamp; }

    Widget *build(BuildContext &ctx) override {
        const FmsThemeData &t = FmsTheme::of(ctx);
        const Leg &leg = *widget().args.leg;
        const bool sel = widget().args.selected;
        const bool compact = widget().args.compact;

        /* A long plan is drawn small so it still fits the panel; the widget
         * count per row is the same either way, which is what the measurement
         * cares about. */
        const lv_font_t *big = compact ? t.font.unit : t.font.value;
        const lv_font_t *small = compact ? t.font.unit : t.font.label;
        const float height = compact ? 16 : 52;
        const float scale = compact ? 0.6F : 1.0F;

        return new GestureDetector{{
            .on_tap = widget().args.on_tap,
            .child = new Container{{
                .height = height,
                .padding = EdgeInsets::symmetric(compact ? 4.0F : 14.0F, 0),
                .color = sel ? t.color.button : t.color.surface,
                .border_color = sel ? t.color.entry : t.color.border,
                .border_width = 1,
                .child = new Row{{
                    .cross = CrossAxis::Center,
                    .spacing = compact ? 8.0F : 16.0F,
                    .children = {
                        /* The stamp. This is the whole demonstration: it comes
                         * from initState, so a rebuilt row cannot show the
                         * number the moved row was showing. */
                        new SizedBox{{
                            .width = 64 * scale,
                            .child = new Text{{.text = fmt("#%d", stamp_),
                                               .font = big,
                                               .color = t.color.attention}},
                        }},
                        new SizedBox{{
                            .width = 120 * scale,
                            .child = new Text{{.text = leg.name,
                                               .font = big,
                                               .color = t.color.entry}},
                        }},
                        new SizedBox{{
                            .width = 120 * scale,
                            .child = new Text{{.text = leg.via,
                                               .font = small,
                                               .color = t.color.label}},
                        }},
                        new Spacer{},
                        new Text{{.text = fmt("%d", leg.altitude),
                                  .font = big,
                                  .color = t.color.computed}},
                        new Text{{.text = "FT", .font = t.font.unit, .color = t.color.computed}},
                    },
                }},
            }},
        }};
    }

private:
    int stamp_ = 0;
};

StateBase *LegRow::createState() const { return new LegRowState(); }

/* ---- The page ----------------------------------------------------------- */

class ReorderPage : public StatefulWidget {
public:
    explicit ReorderPage(int rows) : rows(rows) {}
    FMSUI_WIDGET(ReorderPage)
    StateBase *createState() const override;

    int rows;
};

class ReorderPageState : public State<ReorderPage> {
public:
    void initState() override {
        const size_t n = static_cast<size_t>(widget().rows);
        legs_.reserve(n);
        for (size_t i = 0; i < n; i++) {
            std::string name = kNames[i % kNameCount];
            if (i >= kNameCount) name += std::to_string(i / kNameCount);
            legs_.push_back(Leg{name, kAirways[i % kAirwayCount], 24000 - static_cast<int>(i) * 137});
        }
        rows_.reserve(n);
        reset();
    }

    Widget *build(BuildContext &ctx) override {
        const FmsThemeData &t = FmsTheme::of(ctx);
        const FrameStats &s = FmsApp::instance().stats();
        const bool compact = legs_.size() > 12;

        /* The list is built at run time, so the children go through the
         * pointer+count WidgetList, which copies into the arena.  The vector
         * itself is a member so that building it does not malloc every frame
         * and muddy the build timing we are here to read. */
        rows_.clear();
        for (size_t i = 0; i < order_.size(); i++) {
            const int id = order_[i];
            rows_.push_back(new LegRow{{
                .leg = &legs_[static_cast<size_t>(id)],
                .selected = static_cast<int>(i) == selected_,
                .compact = compact,
                .on_tap = [this, i] { setState([&] { selected_ = static_cast<int>(i); }); },
                /* The only difference between the two halves of this demo. An
                 * unkeyed row can only be matched by position, so moving one
                 * moves the data past a set of Elements that stay put. */
                .key = keyed_ ? Key{id} : Key{},
            }});
        }

        /* lv_created is the number that matters for the claim, and paint_us for
         * the cost: paint is where every lv_obj whose index moved gets told.
         *
         * lv_retexted is here as the control.  A reorder moves rows without
         * changing what they say, so it stays near zero while lv_moved climbs --
         * which is the exact mirror of the F-PLN demo, where the window steps
         * without moving anything and rewrites every string on screen. The two
         * screens between them are why paint needs both numbers.
         *
         * "PREV FRAME" is not hedging, it is what these are.  FmsApp writes the
         * stats after paint and this build() runs before that, so an interaction
         * can never show its own cost -- tap REVERSE and you are reading what
         * the tap *before* it cost.  Unlabelled, that reads as a plausible
         * measurement of what you just did, which is the worst kind of wrong.
         *
         * The fix is not to force an extra frame to catch up: that frame would
         * overwrite the very stats being measured, and every other reader --
         * the simulator's --stats, the device's serial log -- takes them from
         * the same "last completed frame" slot. Press REVERSE twice, or read the
         * serial log, which is sampled outside the frame and so is never stale. */
        Widget *readout = new Text{{
            .text = fmt("PREV FRAME: builds %" PRIu32 " | widgets %" PRIu32 " | lv_objs %" PRIu32
                        " | lv_created %" PRIu32 " | lv_moved %" PRIu32 " | lv_retexted %" PRIu32
                        " | build %" PRIu32 " us | layout %" PRIu32 " us | paint %" PRIu32 " us",
                        s.builds, s.widgets, s.lv_objects, s.lv_created, s.lv_moved,
                        s.lv_retexted, s.build_us, s.layout_us, s.paint_us),
            .font = t.font.unit,
            .color = t.color.computed}};
        Widget *caption = new Text{{
            .text = keyed_ ? "KEYS ON: THE STAMP TRAVELS WITH THE LEG"
                           : "KEYS OFF: THE STAMPS STAY, THE LEGS SLIDE PAST",
            .font = t.font.label,
            .color = keyed_ ? t.color.computed : t.color.attention}};

        Widget *body = new Expanded{{
            .child = new Row{{
                .cross = CrossAxis::Start,
                .spacing = 24,
                .children = {
                    new Expanded{{
                        .flex = 3,
                        .child = new Column{{
                            .cross = CrossAxis::Stretch,
                            .main_size = MainAxisSize::Min,
                            .spacing = compact ? 1.0F : 8.0F,
                            .children = WidgetList(rows_.data(), rows_.size()),
                        }},
                    }},
                    new Expanded{{.flex = 1, .child = controls(t)}},
                },
            }},
        }};

        Widget *title = new Text{{
            .text = fmt("ACTIVE/F-PLN   KEYED RECONCILE   %zu LEGS", legs_.size()),
            .font = compact ? t.font.body : t.font.title,
            .color = t.color.label}};

        /* A long plan runs off the bottom -- there is no clipping, so the
         * readout goes above it rather than being drawn over. */
        WidgetList children =
            compact ? WidgetList{title, readout, caption, new FmsDivider{}, body}
                    : WidgetList{title, new FmsDivider{}, body, new FmsDivider{}, readout, caption};

        return new Container{{
            .padding = EdgeInsets::all(compact ? 12 : 24),
            .color = t.color.background,
            .child = new Column{{
                .cross = CrossAxis::Stretch,
                .spacing = compact ? 6.0F : 16.0F,
                .children = children,
            }},
        }};
    }

private:
    Widget *controls(const FmsThemeData &t) {
        return new Column{{
            .cross = CrossAxis::Stretch,
            .main_size = MainAxisSize::Min,
            .spacing = 10,
            .children = {
                new FmsButton{{.text = "MOVE UP",
                               .text2 = "",
                               .role = FmsRole::Entry,
                               .enabled = selected_ > 0,
                               .on_tap = [this] { move(-1); }}},
                new FmsButton{{.text = "MOVE DOWN",
                               .text2 = "",
                               .role = FmsRole::Entry,
                               .enabled = selected_ < static_cast<int>(order_.size()) - 1,
                               .on_tap = [this] { move(1); }}},
                /* The worst case: every row changes index at once. */
                new FmsButton{{.text = "REVERSE",
                               .text2 = "",
                               .role = FmsRole::Constraint,
                               .on_tap = [this] { reverse(); }}},
                new SizedBox{{.height = 12}},
                new FmsButton{{.text = keyed_ ? "KEYS  ON" : "KEYS  OFF",
                               .text2 = "",
                               .role = keyed_ ? FmsRole::Computed : FmsRole::Attention,
                               .on_tap = [this] { setState([&] { keyed_ = !keyed_; }); }}},
                new FmsButton{{.text = "RESET",
                               .text2 = "",
                               .role = FmsRole::Label,
                               .on_tap = [this] { setState([&] { reset(); }); }}},
                new SizedBox{{.height = 12}},
                new Text{{.text = "TAP A LEG TO SELECT IT",
                          .font = t.font.unit,
                          .color = t.color.label}},
            },
        }};
    }

    void reset() {
        order_.clear();
        for (size_t i = 0; i < legs_.size(); i++) order_.push_back(static_cast<int>(i));
        selected_ = 0;
    }

    void move(int delta) {
        const int to = selected_ + delta;
        if (to < 0 || to >= static_cast<int>(order_.size())) return;
        setState([&] {
            const int tmp = order_[static_cast<size_t>(selected_)];
            order_[static_cast<size_t>(selected_)] = order_[static_cast<size_t>(to)];
            order_[static_cast<size_t>(to)] = tmp;
            selected_ = to;
        });
    }

    void reverse() {
        setState([&] {
            for (size_t i = 0, j = order_.size(); i + 1 < j; i++, j--) {
                const int tmp = order_[i];
                order_[i] = order_[j - 1];
                order_[j - 1] = tmp;
            }
            selected_ = static_cast<int>(order_.size()) - 1 - selected_;
        });
    }

    std::vector<Leg> legs_;
    std::vector<int> order_;
    std::vector<Widget *> rows_;  /* reused, so building it is not in the timings */
    int selected_ = 0;
    bool keyed_ = true;
};

StateBase *ReorderPage::createState() const { return new ReorderPageState(); }

}  // namespace

Widget *reorder_demo_build(int rows) {
    g_next_stamp = 0;

    FmsThemeData data;
    data.color = defaultPalette();
    data.font = FmsTypography{
        .unit = &fms_b612_mono_16,
        .label = &fms_b612_mono_20,
        .body = &fms_b612_mono_24,
        .value = &fms_b612_mono_28,
        .title = &fms_b612_mono_32,
    };
    data.metric = rows > 12 ? denseMetrics() : defaultMetrics();

    return new FmsTheme{{.data = data, .child = new ReorderPage(rows)}};
}
