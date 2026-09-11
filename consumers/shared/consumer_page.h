#pragma once

/* The page both consumers build.
 *
 * The smallest tree that still goes through what an application leans on:
 *
 *   - a StatefulWidget of its own, whose State changes through setState()
 *   - a value another task publishes, read on every build
 *   - FmsTheme holding fonts the application chose -- nothing from fmsui_fonts
 *   - primitives (Column, Text) and FMS widgets (FmsLabel, FmsButton) together
 *
 * A fixture for the usage contract in docs/USING.md, not a demo. It includes
 * <fmsui/fmsui.h> and nothing else from the framework, which is the point.
 */

#include <atomic>
#include <cstdint>

#include "fmsui/fmsui.h"

/* Every step of the type ladder set to one font, which is what an application
 * with a single font does. One with several fills the steps differently. */
fmsui::FmsThemeData consumer_theme(const lv_font_t *font);

struct ConsumerPageArgs {
    /* Owned by the application and written by another task, which publishes
     * here first and calls FmsApp::requestFrame() second. */
    const std::atomic<uint32_t> *sample = nullptr;
};

class ConsumerPage : public fmsui::StatefulWidget {
public:
    explicit ConsumerPage(ConsumerPageArgs a) : args(a) {}
    FMSUI_WIDGET(ConsumerPage)

    fmsui::StateBase *createState() const override;

    ConsumerPageArgs args;
};

/* What the builder given to runApp() returns: the theme, with the page under it. */
fmsui::Widget *consumer_build(const fmsui::FmsThemeData &theme,
                              const std::atomic<uint32_t> &sample);
