/* M3 widget catalogue.
 *
 * Every FMS widget on one screen, all of them live -- tabs switch, radios
 * select, the keypad types into the scratchpad, fields take what the scratchpad
 * holds.  A catalogue of *pictures* would prove nothing; the point is that each
 * one works, and that they work together.
 */

#include "catalog.h"

#include <cstdlib>

#include "fmsui_fonts.h"

using namespace fmsui;

namespace {

const char *const kTabs[] = {"T.O", "CLB", "CRZ", "DES", "APPR", "GA"};
constexpr size_t kTabCount = sizeof(kTabs) / sizeof(kTabs[0]);

const char *const kModes[] = {"LRC", "ECON", "SELECTED"};
constexpr size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

class CatalogPage : public StatefulWidget {
public:
    FMSUI_WIDGET(CatalogPage)
    StateBase *createState() const override;
};

class CatalogPageState : public State<CatalogPage> {
public:
    Widget *build(BuildContext &ctx) override {
        const FmsThemeData &t = FmsTheme::of(ctx);

        return new FmsScaffold{{
            .title = "FMS 1",
            .subtitle = "ETD64",
            .child = new FmsPanel{{
                .title = "ACTIVE/PERF",
                .child = new Row{{
                    .cross = CrossAxis::Stretch,
                    .spacing = 16,
                    .children = {
                        new Expanded{{.flex = 3, .child = leftColumn(t)}},
                        new Expanded{{.flex = 2, .child = rightColumn(t)}},
                    },
                }},
            }},
            .footer = {
                new FmsButton{{.text = "MSG", .text2 = "LIST"}},
                new Spacer{},
                new FmsButton{{.text = "POS MONITOR"}},
                new FmsButton{{.text = "ACTIVATE",
                               .text2 = "APPR",
                               .role = FmsRole::Attention,
                               .on_tap = [this] { setState([&] { message_ = "APPR ACTIVATED"; }); }}},
            },
        }};
    }

private:
    /* ---- left: tabs, fields, controls ---- */
    Widget *leftColumn(const FmsThemeData &t) {
        (void)t;
        return new Column{{
            .cross = CrossAxis::Stretch,
            .main_size = MainAxisSize::Min,
            .spacing = 0,
            .children = {
                new FmsTabs{{
                    .labels = kTabs,
                    .count = kTabCount,
                    .index = tab_,
                    .on_changed = [this](int i) { setState([&] { tab_ = i; }); },
                }},

                new Container{{
                    .padding = EdgeInsets::all(12),
                    .border_color = t.color.border,
                    .border_width = 1,
                    .child = new Column{{
                        .cross = CrossAxis::Start,
                        .main_size = MainAxisSize::Min,
                        .spacing = 10,
                        .children = {
                            new Row{{
                                .cross = CrossAxis::Center,
                                .main_size = MainAxisSize::Min,
                                .spacing = 12,
                                .children = {
                                    new FmsLabel{{.text = "V1"}},
                                    new FmsFieldBox{{.text = fmt("%d", v1_),
                                                     .unit = "KT",
                                                     .on_tap = [this] { commit(&v1_); }}},
                                    new FmsLabel{{.text = "VR"}},
                                    new FmsFieldBox{{.text = fmt("%d", vr_),
                                                     .unit = "KT",
                                                     .on_tap = [this] { commit(&vr_); }}},
                                    new FmsLabel{{.text = "V2"}},
                                    new FmsFieldBox{{.text = fmt("%d", v2_),
                                                     .unit = "KT",
                                                     .on_tap = [this] { commit(&v2_); }}},
                                },
                            }},

                            new Row{{
                                .cross = CrossAxis::Center,
                                .main_size = MainAxisSize::Min,
                                .spacing = 12,
                                .children = {
                                    new FmsLabel{{.text = "T.O SHIFT"}},
                                    new FmsFieldBox{{.empty = true, .on_tap = [this] {
                                                         setState([&] {
                                                             message_ = "NOT ALLOWED";
                                                             error_ = true;
                                                         });
                                                     }}},
                                    new FmsLabel{{.text = "MODE"}},
                                    new FmsDropdown{{
                                        .items = kModes,
                                        .count = kModeCount,
                                        .index = mode_index_,
                                        .on_selected =
                                            [this](int i) { setState([&] { mode_index_ = i; }); },
                                    }},
                                },
                            }},

                            new FmsDivider{},

                            new Row{{
                                .cross = CrossAxis::Center,
                                .main_size = MainAxisSize::Min,
                                .spacing = 24,
                                .children = {
                                    new FmsRadio{{.text = "TOGA",
                                                  .selected = !flex_,
                                                  .on_tap = [this] { setState([&] { flex_ = false; }); }}},
                                    new FmsRadio{{.text = "FLEX",
                                                  .selected = flex_,
                                                  .on_tap = [this] { setState([&] { flex_ = true; }); }}},
                                    new FmsFieldBox{{.text = fmt("+%d", flex_temp_),
                                                     .unit = "\xC2\xB0" "C",
                                                     .role = FmsRole::Entry,
                                                     .on_tap = [this] { commit(&flex_temp_); }}},
                                },
                            }},

                            new FmsDivider{},

                            new Row{{
                                .cross = CrossAxis::Center,
                                .main_size = MainAxisSize::Min,
                                .spacing = 12,
                                .children = {
                                    new FmsLabel{{.text = "REC MAX"}},
                                    new FmsValue{{.text = "FL400", .role = FmsRole::Computed}},
                                    new FmsLabel{{.text = "SPD LIM"}},
                                    new FmsValue{{.text = "250",
                                                  .unit = "KT",
                                                  .role = FmsRole::Constraint}},
                                },
                            }},
                        },
                    }},
                }},
            },
        }};
    }

    /* ---- right: scratchpad and keypad ---- */
    Widget *rightColumn(const FmsThemeData &t) {
        (void)t;
        return new Column{{
            .cross = CrossAxis::Stretch,
            .spacing = 10,
            .children = {
                new FmsScratchpad{{
                    .text = scratch_.empty() ? Str("") : Str(scratch_),
                    .message = Str(message_),
                    .error = error_,
                }},
                new Expanded{{
                    .child = new FmsKeypad{{
                        .on_key = [this](char c) { type(c); },
                        .on_backspace = [this] { backspace(); },
                        .on_clear = [this] { clear(); },
                        .on_enter = [this] {
                            setState([&] {
                                message_ = "SELECT A FIELD";
                                error_ = false;
                            });
                        },
                    }},
                }},
            },
        }};
    }

    /* ---- scratchpad behaviour ---- */
    void type(char c) {
        setState([&] {
            message_.clear();
            error_ = false;
            if (scratch_.size() < 12) scratch_.push_back(c);
        });
    }

    void backspace() {
        setState([&] {
            message_.clear();
            error_ = false;
            if (!scratch_.empty()) scratch_.pop_back();
        });
    }

    void clear() {
        setState([&] {
            scratch_.clear();
            message_.clear();
            error_ = false;
        });
    }

    /* Tapping a field takes whatever is in the scratchpad, which is how a real
     * MCDU works: you type first, then say where it goes. */
    void commit(int *target) {
        setState([&] {
            if (scratch_.empty()) {
                message_ = "SCRATCHPAD EMPTY";
                error_ = true;
                return;
            }
            const int v = std::atoi(scratch_.c_str());
            if (v <= 0) {
                message_ = "FORMAT ERROR";
                error_ = true;
                return;
            }
            *target = v;
            scratch_.clear();
            message_.clear();
            error_ = false;
        });
    }

    int tab_ = 0;
    int v1_ = 153;
    int vr_ = 155;
    int v2_ = 160;
    int flex_temp_ = 63;
    bool flex_ = true;
    int mode_index_ = 1;

    std::string scratch_;
    std::string message_;
    bool error_ = false;
};

StateBase *CatalogPage::createState() const { return new CatalogPageState(); }

}  // namespace

Widget *catalog_build() {
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

    return new FmsTheme{{.data = data, .child = new CatalogPage()}};
}
