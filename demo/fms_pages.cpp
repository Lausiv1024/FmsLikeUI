/* M4: the reference screens.
 *
 * ACTIVE/PERF on the left, ACTIVE/INIT on the right, laid out to match the
 * photographs of the real A350 MFD.
 *
 * One deliberate departure. On the aircraft the pilot types on a physical
 * keyboard and the scratchpad lives permanently at the bottom of the page. This
 * is a touch panel with no keyboard, so tapping a field opens a keypad over the
 * screen instead: you edit the field you pointed at, and the scratchpad is the
 * line above the keys. Same two-step commit, different hardware.
 *
 * Values are strings, not numbers, because an MCDU field *is* a string -- FL370,
 * EGLL, -56, HD 075. Parsing them into types is the application's business, not
 * the widget's.
 */

#include "fms_pages.h"

#include <string>
#include <vector>

#include "fmsui_fonts.h"

using namespace fmsui;

namespace {

const char *const kPerfTabs[] = {"T.O", "CLB", "CRZ", "DES", "APPR", "GA"};
constexpr size_t kPerfTabCount = sizeof(kPerfTabs) / sizeof(kPerfTabs[0]);

/* The real MODE list: LRC (long range cruise) and ECON. */
const char *const kModes[] = {"LRC", "ECON"};
constexpr size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

const char *const kOnOff[] = {"ON", "OFF"};
constexpr size_t kOnOffCount = 2;

/* ---- shared helpers ---------------------------------------------------- */

/* A label followed by its framed value: the unit of every row on these pages. */
Widget *field(const char *label, const Str &value, const char *unit, FmsRole role,
              VoidCallback on_tap, bool empty = false) {
    return new Row{{
        .cross = CrossAxis::Center,
        .main_size = MainAxisSize::Min,
        .spacing = 8,
        .children = {
            new FmsLabel{{.text = label}},
            new FmsFieldBox{{.text = value,
                             .unit = unit,
                             .role = role,
                             .empty = empty,
                             .on_tap = std::move(on_tap)}},
        },
    }};
}

/* A label and a value with no box round it: a number the system worked out and
 * that the pilot cannot touch. */
Widget *readout(const char *label, const char *value, const char *unit, FmsRole role) {
    return new Row{{
        .cross = CrossAxis::Center,
        .main_size = MainAxisSize::Min,
        .spacing = 8,
        .children = {
            new FmsLabel{{.text = label}},
            new FmsValue{{.text = value, .unit = unit, .role = role}},
        },
    }};
}

/* The row of menus across the top of both pages. */
Widget *menuBar(const Str &mode) {
    return new Row{{
        .cross = CrossAxis::Center,
        .spacing = 6,
        .children = {
            new FmsDropdown{{.text = mode, .role = FmsRole::Label}},
            new FmsDropdown{{.text = "POSITION", .role = FmsRole::Label}},
            new FmsDropdown{{.text = "SEC INDEX", .role = FmsRole::Label}},
            new FmsDropdown{{.text = "DATA", .role = FmsRole::Label}},
            new Spacer{},
        },
    }};
}

Widget *rowOf(std::initializer_list<Widget *> items, float spacing = 18) {
    return new Row{{
        .cross = CrossAxis::Center,
        .main_size = MainAxisSize::Min,
        .spacing = spacing,
        .children = WidgetList(items),
    }};
}

/* ---- the page ---------------------------------------------------------- */

class FmsPages : public StatefulWidget {
public:
    FMSUI_WIDGET(FmsPages)
    StateBase *createState() const override;
};

class FmsPagesState : public State<FmsPages> {
public:
    Widget *build(BuildContext &ctx) override {
        const FmsThemeData &t = FmsTheme::of(ctx);

        Widget *pages = new Row{{
            .cross = CrossAxis::Stretch,
            .spacing = 8,
            .children = {
                new Expanded{{.child = perfPage(t)}},
                new Expanded{{.child = initPage(t)}},
            },
        }};

        /* Always a Stack, even with nothing over the pages.
         *
         * Returning `pages` directly when idle and a Stack when editing was the
         * obvious way to write this, and it cost 600ms per keypad open. Changing
         * the widget type at the root changes the Element's identity, so the
         * reconciler threw away the whole tree -- all 282 lv_objs -- and built it
         * again. Keeping the root a Stack means `children[0]` is the same Row in
         * both states, so the pages keep their Elements and only the two overlay
         * children get created and destroyed. 600ms -> 11ms.
         *
         * The rule this teaches: keep the shape of the tree stable and vary the
         * leaves. It is the same rule in Flutter, for the same reason. */
        std::vector<Widget *> layers;
        layers.push_back(pages);

        if (editing_ != nullptr) {
            layers.push_back(new Positioned{{
                .left = 0,
                .top = 0,
                .right = 0,
                .bottom = 0,
                .child = new GestureDetector{{
                    /* Tapping outside the keypad cancels: that is what a scrim
                     * is for. */
                    .on_tap = [this] { setState([&] { editing_ = nullptr; }); },
                    .child = new Container{{.color = Color::rgba(0x000000, 200)}},
                }},
            }});
            layers.push_back(new Positioned{{
                .right = 24,
                .bottom = 24,
                .width = 460,
                .child = keypadPanel(t),
            }});
        }

        return new Stack{{.children = WidgetList(layers.data(), layers.size())}};
    }

private:
    /* ---- ACTIVE/PERF ---- */
    Widget *perfPage(const FmsThemeData &t) {
        return new Column{{
            .cross = CrossAxis::Stretch,
            .spacing = 6,
            .children = {
                new Row{{
                    .cross = CrossAxis::Center,
                    .children = {
                        new FmsDropdown{{.text = "FMS 1", .role = FmsRole::Entry}},
                        new Spacer{},
                        new FmsLabel{{.text = "ETD64"}},
                    },
                }},
                menuBar(active_),
                new Expanded{{
                    .child = new FmsPanel{{
                        .title = "ACTIVE/PERF",
                        .child = new Column{{
                            .cross = CrossAxis::Stretch,
                            .main_size = MainAxisSize::Min,
                            .spacing = 6,
                            .children = {
                                /* The four flight levels across the top. */
                                new Row{{
                                    .cross = CrossAxis::Center,
                                    .children = {
                                        field("CRZ", crz_fl_, "", FmsRole::Entry,
                                              [this] { edit("CRZ FL", &crz_fl_); }),
                                        new Spacer{},
                                        readout("OPT", "FL385", "", FmsRole::Computed),
                                    },
                                }},
                                new Row{{
                                    .cross = CrossAxis::Center,
                                    .children = {
                                        readout("REC MAX", "FL400", "", FmsRole::Computed),
                                        new Spacer{},
                                        readout("EO MAX", "FL243", "", FmsRole::Computed),
                                    },
                                }},

                                new FmsTabs{{
                                    .labels = kPerfTabs,
                                    .count = kPerfTabCount,
                                    .index = tab_,
                                    .on_changed = [this](int i) { setState([&] { tab_ = i; }); },
                                }},

                                new Container{{
                                    .padding = EdgeInsets::all(10),
                                    .border_color = t.color.border,
                                    .border_width = 1,
                                    .child = new Column{{
                                        .cross = CrossAxis::Stretch,
                                        .main_size = MainAxisSize::Min,
                                        .spacing = 8,
                                        .children = {
                                            new Row{{
                                                .cross = CrossAxis::Center,
                                                .children = {
                                                    readout("RWY", "09R", "", FmsRole::Computed),
                                                    new Spacer{},
                                                    field("T.O SHIFT", Str(""), "", FmsRole::Entry,
                                                          [this] { edit("T.O SHIFT", &to_shift_); },
                                                          to_shift_.empty()),
                                                },
                                            }},

                                            /* V speeds and the flap speeds beside
                                             * them, exactly as the aircraft shows
                                             * them: entered on the left, computed
                                             * on the right. */
                                            new Row{{
                                                .cross = CrossAxis::Start,
                                                .spacing = 20,
                                                .children = {
                                                    new Column{{
                                                        .cross = CrossAxis::Start,
                                                        .main_size = MainAxisSize::Min,
                                                        .spacing = 6,
                                                        .children = {
                                                            field("V1", v1_, "KT", FmsRole::Entry,
                                                                  [this] { edit("V1", &v1_); }),
                                                            field("VR", vr_, "KT", FmsRole::Entry,
                                                                  [this] { edit("VR", &vr_); }),
                                                            field("V2", v2_, "KT", FmsRole::Entry,
                                                                  [this] { edit("V2", &v2_); }),
                                                        },
                                                    }},
                                                    new Column{{
                                                        .cross = CrossAxis::Start,
                                                        .main_size = MainAxisSize::Min,
                                                        .spacing = 10,
                                                        .children = {
                                                            readout("F", "151", "KT",
                                                                    FmsRole::Computed),
                                                            readout("S", "189", "KT",
                                                                    FmsRole::Computed),
                                                            readout("O", "213", "KT",
                                                                    FmsRole::Computed),
                                                        },
                                                    }},
                                                    new Spacer{},
                                                    new Column{{
                                                        .cross = CrossAxis::Start,
                                                        .main_size = MainAxisSize::Min,
                                                        .spacing = 6,
                                                        .children = {
                                                            new FmsRadio{{
                                                                .text = "TOGA",
                                                                .selected = !flex_,
                                                                .on_tap = [this] {
                                                                    setState([&] { flex_ = false; });
                                                                },
                                                            }},
                                                            new FmsRadio{{
                                                                .text = "FLEX",
                                                                .selected = flex_,
                                                                .on_tap = [this] {
                                                                    setState([&] { flex_ = true; });
                                                                },
                                                            }},
                                                            new FmsFieldBox{{
                                                                .text = flex_temp_,
                                                                .unit = "\xC2\xB0" "C",
                                                                .role = FmsRole::Entry,
                                                                .on_tap = [this] {
                                                                    edit("FLEX TEMP", &flex_temp_);
                                                                },
                                                            }},
                                                        },
                                                    }},
                                                },
                                            }},

                                            new FmsDivider{},

                                            rowOf({
                                                field("FLAPS", flaps_, "", FmsRole::Entry,
                                                      [this] { edit("FLAPS", &flaps_); }),
                                                field("THS FOR", ths_, "%", FmsRole::Entry,
                                                      [this] { edit("THS", &ths_); }),
                                            }),

                                            rowOf({
                                                new FmsLabel{{.text = "PACKS"}},
                                                new FmsDropdown{{
                                                    .items = kOnOff,
                                                    .count = kOnOffCount,
                                                    .index = packs_on_ ? 0 : 1,
                                                    .on_selected =
                                                        [this](int i) {
                                                            setState([&] { packs_on_ = (i == 0); });
                                                        },
                                                }},
                                                new FmsLabel{{.text = "ANTI-ICE"}},
                                                new FmsDropdown{{
                                                    .items = kOnOff,
                                                    .count = kOnOffCount,
                                                    .index = anti_ice_on_ ? 0 : 1,
                                                    .on_selected =
                                                        [this](int i) {
                                                            setState([&] { anti_ice_on_ = (i == 0); });
                                                        },
                                                }},
                                            }),

                                            new FmsDivider{},

                                            rowOf({
                                                field("THR RED", thr_red_, "FT", FmsRole::Entry,
                                                      [this] { edit("THR RED", &thr_red_); }),
                                                field("ACCEL", accel_, "FT", FmsRole::Entry,
                                                      [this] { edit("ACCEL", &accel_); }),
                                            }),

                                            rowOf({
                                                field("TRANS", trans_, "FT", FmsRole::Entry,
                                                      [this] { edit("TRANS", &trans_); }),
                                                new FmsButton{{.text = "NOISE"}},
                                            }),
                                        },
                                    }},
                                }},
                            },
                        }},
                    }},
                }},
                new Row{{
                    .cross = CrossAxis::End,
                    .spacing = 8,
                    .children = {
                        new FmsButton{{.text = "MSG", .text2 = "LIST"}},
                        new Spacer{},
                        new FmsButton{{.text = "POS MONITOR"}},
                    },
                }},
            },
        }};
    }

    /* ---- ACTIVE/INIT ---- */
    Widget *initPage(const FmsThemeData &t) {
        (void)t;
        return new Column{{
            .cross = CrossAxis::Stretch,
            .spacing = 6,
            .children = {
                new Row{{
                    .cross = CrossAxis::Center,
                    .children = {
                        new FmsDropdown{{.text = "FMS 2", .role = FmsRole::Entry}},
                        new Spacer{},
                        new FmsLabel{{.text = "ETD64"}},
                    },
                }},
                menuBar(active_),
                new Expanded{{
                    .child = new FmsPanel{{
                        .title = "ACTIVE/INIT",
                        .child = new Column{{
                            .cross = CrossAxis::Stretch,
                            .main_size = MainAxisSize::Min,
                            .spacing = 8,
                            .children = {
                                new Row{{
                                    .cross = CrossAxis::Center,
                                    .children = {
                                        field("FLT NBR", flt_nbr_, "", FmsRole::Entry,
                                              [this] { edit("FLT NBR", &flt_nbr_); }),
                                        new Spacer{},
                                        new FmsButton{{.text = "ACFT STATUS"}},
                                    },
                                }},

                                rowOf({
                                    field("FROM", from_, "", FmsRole::Entry,
                                          [this] { edit("FROM", &from_); }),
                                    field("TO", to_, "", FmsRole::Entry,
                                          [this] { edit("TO", &to_); }),
                                    field("ALTN", altn_, "", FmsRole::Entry,
                                          [this] { edit("ALTN", &altn_); }),
                                }, 12),

                                new Row{{
                                    .cross = CrossAxis::Center,
                                    .children = {
                                        field("CPNY RTE", cpny_rte_, "", FmsRole::Entry,
                                              [this] { edit("CPNY RTE", &cpny_rte_); }),
                                        new Spacer{},
                                        new FmsButton{{.text = "RTE SEL"}},
                                    },
                                }},

                                new Row{{
                                    .cross = CrossAxis::Center,
                                    .children = {
                                        field("ALTN RTE", altn_rte_, "", FmsRole::Entry,
                                              [this] { edit("ALTN RTE", &altn_rte_); }),
                                        new Spacer{},
                                        new FmsButton{{.text = "ALTN RTE SEL"}},
                                    },
                                }},

                                new FmsDivider{},

                                new Row{{
                                    .cross = CrossAxis::Center,
                                    .children = {
                                        field("CRZ FL", crz_fl_, "", FmsRole::Entry,
                                              [this] { edit("CRZ FL", &crz_fl_); }),
                                        new Spacer{},
                                        field("CRZ TEMP", crz_temp_, "\xC2\xB0" "C",
                                              FmsRole::Entry,
                                              [this] { edit("CRZ TEMP", &crz_temp_); }),
                                    },
                                }},

                                new Row{{
                                    .cross = CrossAxis::Center,
                                    .children = {
                                        new Row{{
                                            .cross = CrossAxis::Center,
                                            .main_size = MainAxisSize::Min,
                                            .spacing = 8,
                                            .children = {
                                                new FmsLabel{{.text = "MODE"}},
                                                new FmsDropdown{{
                                                    .items = kModes,
                                                    .count = kModeCount,
                                                    .index = mode_index_,
                                                    .on_selected =
                                                        [this](int i) {
                                                            setState([&] { mode_index_ = i; });
                                                        },
                                                }},
                                            },
                                        }},
                                        new Spacer{},
                                        field("TROPO", tropo_, "FT", FmsRole::Entry,
                                              [this] { edit("TROPO", &tropo_); }),
                                    },
                                }},

                                new Row{{
                                    .cross = CrossAxis::Center,
                                    .children = {
                                        field("CI", ci_, "", FmsRole::Entry,
                                              [this] { edit("CI", &ci_); }),
                                        new Spacer{},
                                        field("TRIP WIND", trip_wind_, "", FmsRole::Entry,
                                              [this] { edit("TRIP WIND", &trip_wind_); }),
                                    },
                                }},

                                new FmsDivider{},

                                /* The page's own menu. On the aircraft these are
                                 * line-select keys down the side; on a touch panel
                                 * they are just buttons. */
                                new Row{{
                                    .cross = CrossAxis::Start,
                                    .spacing = 8,
                                    .children = {
                                        new Expanded{{
                                            .child = new Column{{
                                                .cross = CrossAxis::Stretch,
                                                .main_size = MainAxisSize::Min,
                                                .spacing = 6,
                                                .children = {
                                                    new FmsButton{{.text = "IRS"}},
                                                    new FmsButton{{.text = "DEPARTURE"}},
                                                    new FmsButton{{.text = "NAVAIDS"}},
                                                },
                                            }},
                                        }},
                                        new Expanded{{
                                            .child = new Column{{
                                                .cross = CrossAxis::Stretch,
                                                .main_size = MainAxisSize::Min,
                                                .spacing = 6,
                                                .children = {
                                                    new FmsButton{{.text = "FUEL&LOAD"}},
                                                    new FmsButton{{.text = "T.O. PERF"}},
                                                    new FmsButton{{.text = "RTE SUMMARY"}},
                                                },
                                            }},
                                        }},
                                    },
                                }},
                            },
                        }},
                    }},
                }},
                new Row{{
                    .cross = CrossAxis::End,
                    .spacing = 8,
                    .children = {
                        new FmsButton{{.text = "MSG", .text2 = "LIST"}},
                        new Spacer{},
                        new FmsButton{{.text = "CPNY T.O", .text2 = "REQUEST"}},
                    },
                }},
            },
        }};
    }

    /* ---- the keypad overlay ---- */
    Widget *keypadPanel(const FmsThemeData &t) {
        return new Container{{
            .padding = EdgeInsets::all(12),
            .color = t.color.background,
            .border_color = t.color.entry,
            .border_width = 2,
            .child = new Column{{
                .cross = CrossAxis::Stretch,
                .main_size = MainAxisSize::Min,
                .spacing = 10,
                .children = {
                    new Row{{
                        .cross = CrossAxis::Center,
                        .children = {
                            new FmsLabel{{.text = editing_label_}},
                            new Spacer{},
                            new FmsButton{{
                                .text = "ABC",
                                .role = letters_ ? FmsRole::Entry : FmsRole::Label,
                                .on_tap = [this] { setState([&] { letters_ = !letters_; }); },
                            }},
                        },
                    }},
                    new FmsScratchpad{{
                        .text = Str(scratch_),
                        .message = Str(message_),
                        .error = error_,
                    }},
                    new FmsKeypad{{
                        .on_key = [this](char c) { type(c); },
                        .on_backspace = [this] { backspace(); },
                        .on_clear = [this] { setState([&] { editing_ = nullptr; }); },
                        .on_enter = [this] { commit(); },
                        .letters = letters_,
                    }},
                },
            }},
        }};
    }

    /* ---- editing ---- */
    void edit(const char *label, std::string *target) {
        setState([&] {
            editing_ = target;
            editing_label_ = label;
            scratch_ = *target;
            message_.clear();
            error_ = false;
            letters_ = false;
        });
    }

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

    void commit() {
        setState([&] {
            if (scratch_.empty()) {
                message_ = "ENTRY REQUIRED";
                error_ = true;
                return;
            }
            *editing_ = scratch_;
            editing_ = nullptr;
        });
    }

    /* PERF */
    int tab_ = 0;
    bool flex_ = true;
    std::string crz_fl_ = "FL370";
    std::string to_shift_;
    std::string v1_ = "153";
    std::string vr_ = "155";
    std::string v2_ = "160";
    std::string flex_temp_ = "+63";
    std::string flaps_ = "1";
    std::string ths_ = "30.6";
    bool packs_on_ = true;
    bool anti_ice_on_ = false;
    std::string thr_red_ = "1580";
    std::string accel_ = "1580";
    std::string trans_ = "6000";

    /* INIT */
    std::string active_ = "ACTIVE";
    std::string flt_nbr_ = "ETD64";
    std::string from_ = "EGLL";
    std::string to_ = "OMAA";
    std::string altn_ = "OMDB";
    std::string cpny_rte_ = "EGLLOMAA";
    std::string altn_rte_ = "NONE";
    std::string crz_temp_ = "-56";
    int mode_index_ = 1;  /* ECON */
    std::string tropo_ = "36090";
    std::string ci_ = "193";
    std::string trip_wind_ = "HD075";

    /* editing */
    std::string *editing_ = nullptr;
    std::string editing_label_;
    std::string scratch_;
    std::string message_;
    bool error_ = false;
    bool letters_ = false;
};

StateBase *FmsPages::createState() const { return new FmsPagesState(); }

}  // namespace

Widget *fms_pages_build() {
    FmsThemeData data;
    data.color = defaultPalette();
    /* One step down the ladder from the catalogue: two of these pages side by
     * side on a 1280x720 panel simply do not fit at 28px values. */
    data.font = FmsTypography{
        .unit = &fms_b612_mono_16,
        .label = &fms_b612_mono_16,
        .body = &fms_b612_mono_20,
        .value = &fms_b612_mono_24,
        .title = &fms_b612_mono_24,
    };
    data.metric = denseMetrics();

    return new FmsTheme{{
        .data = data,
        .child = new Container{{
            .padding = EdgeInsets::all(8),
            .color = data.color.background,
            .child = new FmsPages(),
        }},
    }};
}
