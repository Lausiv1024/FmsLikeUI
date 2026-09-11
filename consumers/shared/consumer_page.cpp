#include "consumer_page.h"

#include <cinttypes>  // uint32_t is `long unsigned int` on riscv32, so %u will not do

using namespace fmsui;

namespace {

class ConsumerPageState : public State<ConsumerPage> {
public:
    Widget *build(BuildContext &ctx) override {
        const FmsThemeData &t = FmsTheme::of(ctx);

        const std::atomic<uint32_t> *published = widget().args.sample;
        const uint32_t sample =
            published != nullptr ? published->load(std::memory_order_relaxed) : 0;

        return new Column{{
            .main = MainAxis::Center,
            .spacing = 12,
            .children = {
                new FmsLabel{{.text = "CONSUMER"}},
                new Text{{.text = fmt("SAMPLE %" PRIu32, sample),
                          .font = t.font.value,
                          .color = t.color.computed}},
                new FmsButton{{
                    .text = "TAP",
                    .role = FmsRole::Entry,
                    .on_tap = [this] { setState([this] { taps_++; }); },
                }},
                new Text{{.text = fmt("TAPS %" PRIu32, taps_),
                          .font = t.font.body,
                          .color = t.color.entry}},
            },
        }};
    }

private:
    uint32_t taps_ = 0;
};

}  // namespace

StateBase *ConsumerPage::createState() const { return new ConsumerPageState(); }

FmsThemeData consumer_theme(const lv_font_t *font) {
    return FmsThemeData{
        .color = defaultPalette(),
        .font = {.unit = font, .label = font, .body = font, .value = font, .title = font},
        .metric = defaultMetrics(),
    };
}

Widget *consumer_build(const FmsThemeData &theme, const std::atomic<uint32_t> &sample) {
    return new FmsTheme{{
        .data = theme,
        .child = new ConsumerPage{{.sample = &sample}},
    }};
}
