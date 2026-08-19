/* Core + theme demo.
 *
 * Built out of the raw primitives (Column, Row, Container, Text,
 * GestureDetector) rather than the FMS widgets, because the point is to prove
 * the core works: flex distribution, reconciliation across setState, Stack
 * z-order, touch landing on the right target -- and now that every colour and
 * font comes from FmsTheme rather than from a constant next to the widget.
 */

#include "m1_demo.h"

#include <cinttypes>  // uint32_t is `long unsigned int` on riscv32, so %u will not do

#include "fmsui_fonts.h"

using namespace fmsui;

namespace {

/* A framed value with a unit, the atom of every FMS page.  Tapping it steps the
 * value, which is how we prove setState reaches the right State object. */
struct FieldArgs {
    Str label;
    Str value;
    Str unit;
    Color color;
    VoidCallback on_tap{};
};

class Field : public StatelessWidget {
public:
    explicit Field(FieldArgs a) : args_(std::move(a)) {}
    FMSUI_WIDGET(Field)

    Widget *build(BuildContext &ctx) const override {
        const FmsThemeData &t = FmsTheme::of(ctx);

        return new Row{{
            .cross = CrossAxis::Center,
            .main_size = MainAxisSize::Min,
            .spacing = 10,
            .children = {
                new Text{{.text = args_.label, .font = t.font.label, .color = t.color.label}},
                new GestureDetector{{
                    .on_tap = args_.on_tap,
                    .child = new Container{{
                        .height = 44,
                        .padding = EdgeInsets::symmetric(12, 0),
                        .color = t.color.surface,
                        .border_color = t.color.border,
                        .border_width = 1,
                        .child = new Row{{
                            .cross = CrossAxis::Center,
                            .main_size = MainAxisSize::Min,
                            .spacing = 6,
                            .children = {
                                new Text{{.text = args_.value,
                                          .font = t.font.value,
                                          .color = args_.color}},
                                new Text{{.text = args_.unit,
                                          .font = t.font.unit,
                                          .color = args_.color}},
                            },
                        }},
                    }},
                }},
            },
        }};
    }

private:
    FieldArgs args_;
};

/* ---- The page ---------------------------------------------------------- */

class DemoPage : public StatefulWidget {
public:
    FMSUI_WIDGET(DemoPage)
    StateBase *createState() const override;
};

class DemoPageState : public State<DemoPage> {
public:
    Widget *build(BuildContext &ctx) override {
        const FmsThemeData &t = FmsTheme::of(ctx);
        const FrameStats &s = FmsApp::instance().stats();

        return new Container{{
            .padding = EdgeInsets::all(24),
            .color = t.color.background,
            .child = new Column{{
                .cross = CrossAxis::Stretch,
                .spacing = 16,
                .children = {
                    new Text{{.text = "FmsLikeUI  M2  THEME",
                              .font = t.font.title,
                              .color = t.color.label}},

                    /* Flex: the Spacer pushes CRZ to the far right. */
                    new Row{{
                        .cross = CrossAxis::Center,
                        .spacing = 24,
                        .children = {
                            new Field{{.label = "V1",
                                       .value = fmt("%d", v1_),
                                       .unit = "KT",
                                       .color = t.color.entry,
                                       .on_tap = [this] { setState([&] { v1_ += 1; }); }}},
                            new Field{{.label = "VR",
                                       .value = fmt("%d", vr_),
                                       .unit = "KT",
                                       .color = t.color.entry,
                                       .on_tap = [this] { setState([&] { vr_ += 1; }); }}},
                            new Field{{.label = "V2",
                                       .value = fmt("%d", v2_),
                                       .unit = "KT",
                                       .color = t.color.entry,
                                       .on_tap = [this] { setState([&] { v2_ += 1; }); }}},
                            new Spacer{},
                            new Field{{.label = "CRZ",
                                       .value = fmt("FL%d", flight_level_),
                                       .unit = "",
                                       .color = t.color.computed,
                                       .on_tap = [this] { setState([&] { flight_level_ += 10; }); }}},
                        },
                    }},

                    /* Expanded with different flex factors: 2:1:1 of the row. */
                    new SizedBox{{
                        .height = 120,
                        .child = new Row{{
                            .spacing = 12,
                            .children = {
                                new Expanded{{.flex = 2, .child = panel(t, "FLEX 2", t.color.entry)}},
                                new Expanded{{.flex = 1, .child = panel(t, "FLEX 1", t.color.computed)}},
                                new Expanded{{.flex = 1, .child = panel(t, "FLEX 1", t.color.attention)}},
                            },
                        }},
                    }},

                    /* Stack: later children paint on top. */
                    new SizedBox{{
                        .height = 120,
                        .child = new Stack{{
                            .children = {
                                new Container{{.color = t.color.surface,
                                               .border_color = t.color.border,
                                               .border_width = 1}},
                                new Positioned{{
                                    .left = 20,
                                    .top = 20,
                                    .child = new Text{{.text = "STACK: BOTTOM",
                                                       .font = t.font.label,
                                                       .color = t.color.label}},
                                }},
                                new Positioned{{
                                    .right = 20,
                                    .bottom = 20,
                                    .child = new Container{{
                                        .padding = EdgeInsets::all(10),
                                        .color = t.color.attention,
                                        .child = new Text{{.text = "ON TOP",
                                                           .font = t.font.label,
                                                           .color = t.color.background}},
                                    }},
                                }},
                            },
                        }},
                    }},

                    new Spacer{},

                    /* Proof that a rebuild is not a repaint: these numbers only
                     * move when something actually asked for one. */
                    new Text{{.text = fmt("builds %" PRIu32 " | widgets %" PRIu32
                                          " | lv_objs %" PRIu32 " | arena %" PRIu32
                                          " B | %" PRIu32 " us",
                                          s.builds, s.widgets, s.lv_objects, s.arena_bytes,
                                          s.build_us),
                              .font = t.font.unit,
                              .color = t.color.computed}},
                    new Text{{.text = "TAP A FIELD TO INCREMENT IT",
                              .font = t.font.unit,
                              .color = t.color.attention}},
                },
            }},
        }};
    }

private:
    static Widget *panel(const FmsThemeData &t, const char *text, Color c) {
        return new Container{{
            .color = t.color.surface,
            .border_color = c,
            .border_width = 1,
            .alignment = Alignment::center(),
            .child = new Text{{.text = text, .font = t.font.body, .color = c}},
        }};
    }

    int v1_ = 153;
    int vr_ = 155;
    int v2_ = 160;
    int flight_level_ = 370;
};

StateBase *DemoPage::createState() const { return new DemoPageState(); }

}  // namespace

Widget *m1_demo_build() {
    FmsThemeData data;
    data.color = defaultPalette();
    data.font = FmsTypography{
        .unit = &fms_b612_mono_16,
        .label = &fms_b612_mono_20,
        .body = &fms_b612_mono_24,
        .value = &fms_b612_mono_28,
        .title = &fms_b612_mono_32,
    };
    data.metric = defaultMetrics();

    return new FmsTheme{{.data = data, .child = new DemoPage()}};
}
