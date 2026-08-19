#pragma once

/* The basic widget set.
 *
 * Every widget takes a single Args struct so that C++20 designated initializers
 * give us named arguments:
 *
 *     new Column{{
 *       .cross = CrossAxis::Start,
 *       .children = {
 *         new Text{{ .text = "V1", .color = white }},
 *         new Expanded{{ .child = new Container{{ .color = grey }} }},
 *       },
 *     }}
 *
 * The doubled braces are the price: the outer pair is `new Column{...}`, the
 * inner pair is the Args aggregate.
 */

#include "fmsui/element.h"
#include "fmsui/foundation.h"
#include "fmsui/render.h"
#include "fmsui/str.h"
#include "fmsui/widget.h"

namespace fmsui {

/* ---- Text -------------------------------------------------------------- */

struct TextArgs {
    Str text;
    const lv_font_t *font = nullptr;  // null means LVGL's default font
    Color color = Color::rgb(0xFFFFFF);
    TextAlign align = TextAlign::Left;
    Key key{};
};

class Text : public LeafRenderObjectWidget {
public:
    explicit Text(TextArgs a) : args_(a) { key = a.key; }
    FMSUI_WIDGET(Text)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    TextArgs args_;
};

/* ---- Container --------------------------------------------------------- */

/* Compose-only: Container is a StatelessWidget over the primitives below, the
 * same way Flutter's is.  Keeping it out of the render layer means there is
 * exactly one implementation of padding, of alignment, and of decoration. */
struct ContainerArgs {
    float width = NAN;
    float height = NAN;
    EdgeInsets padding{};
    EdgeInsets margin{};
    Color color = Color::transparent();
    Color border_color = Color::transparent();
    float border_width = 0;
    float radius = 0;
    /* NaN means "do not align" -- just wrap the child at its natural size.
     * A pointer would be the obvious way to say "unset", but only Widgets may be
     * `new`ed here (the arena owns them); anything else would leak. */
    Alignment alignment{NAN, NAN};
    Widget *child = nullptr;
    Key key{};
};

class Container : public StatelessWidget {
public:
    explicit Container(ContainerArgs a) : args_(a) { key = a.key; }
    FMSUI_WIDGET(Container)

    Widget *build(BuildContext &ctx) const override;

private:
    ContainerArgs args_;
};

/* ---- Decoration / padding / sizing primitives -------------------------- */

struct DecoratedBoxArgs {
    BoxDecoration decoration{};
    Widget *child = nullptr;
    Key key{};
};

class DecoratedBox : public SingleChildRenderObjectWidget {
public:
    explicit DecoratedBox(DecoratedBoxArgs a) : args_(a) {
        key = a.key;
        child = a.child;
    }
    FMSUI_WIDGET(DecoratedBox)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    DecoratedBoxArgs args_;
};

struct PaddingArgs {
    EdgeInsets padding{};
    Widget *child = nullptr;
    Key key{};
};

class Padding : public SingleChildRenderObjectWidget {
public:
    explicit Padding(PaddingArgs a) : args_(a) {
        key = a.key;
        child = a.child;
    }
    FMSUI_WIDGET(Padding)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    PaddingArgs args_;
};

struct AlignArgs {
    Alignment alignment = Alignment::center();
    Widget *child = nullptr;
    Key key{};
};

class Align : public SingleChildRenderObjectWidget {
public:
    explicit Align(AlignArgs a) : args_(a) {
        key = a.key;
        child = a.child;
    }
    FMSUI_WIDGET(Align)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    AlignArgs args_;
};

/* Sugar for Align{{ .alignment = center }}. */
class Center : public StatelessWidget {
public:
    explicit Center(Widget *child) : child_(child) {}
    FMSUI_WIDGET(Center)

    Widget *build(BuildContext &ctx) const override;

private:
    Widget *child_;
};

struct SizedBoxArgs {
    float width = NAN;   // NAN: leave this axis to the child / the constraints
    float height = NAN;
    Widget *child = nullptr;
    Key key{};
};

class SizedBox : public SingleChildRenderObjectWidget {
public:
    explicit SizedBox(SizedBoxArgs a) : args_(a) {
        key = a.key;
        child = a.child;
    }
    FMSUI_WIDGET(SizedBox)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    SizedBoxArgs args_;
};

/* ---- Flex -------------------------------------------------------------- */

struct FlexArgs {
    MainAxis main = MainAxis::Start;
    CrossAxis cross = CrossAxis::Center;
    MainAxisSize main_size = MainAxisSize::Max;
    float spacing = 0;
    WidgetList children{};
    Key key{};
};

class Row : public MultiChildRenderObjectWidget {
public:
    explicit Row(FlexArgs a) : args_(a) {
        key = a.key;
        children = a.children;
    }
    FMSUI_WIDGET(Row)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    FlexArgs args_;
};

class Column : public MultiChildRenderObjectWidget {
public:
    explicit Column(FlexArgs a) : args_(a) {
        key = a.key;
        children = a.children;
    }
    FMSUI_WIDGET(Column)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    FlexArgs args_;
};

struct FlexibleArgs {
    int flex = 1;
    FlexFit fit = FlexFit::Loose;
    Widget *child = nullptr;
    Key key{};
};

/* Takes a share of what is left over on the main axis, but may use less. */
class Flexible : public SingleChildRenderObjectWidget {
public:
    explicit Flexible(FlexibleArgs a) : args_(a) {
        key = a.key;
        child = a.child;
    }
    FMSUI_WIDGET(Flexible)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    FlexibleArgs args_;
};

struct ExpandedArgs {
    int flex = 1;
    Widget *child = nullptr;
    Key key{};
};

/* Flexible with a tight fit: takes its whole share, no less. */
class Expanded : public SingleChildRenderObjectWidget {
public:
    explicit Expanded(ExpandedArgs a) : args_(a) {
        key = a.key;
        child = a.child;
    }
    FMSUI_WIDGET(Expanded)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    ExpandedArgs args_;
};

struct SpacerArgs {
    int flex = 1;
    Key key{};
};

/* Empty, but it pushes. */
class Spacer : public StatelessWidget {
public:
    explicit Spacer(SpacerArgs a = {}) : args_(a) { key = a.key; }
    FMSUI_WIDGET(Spacer)

    Widget *build(BuildContext &ctx) const override;

private:
    SpacerArgs args_;
};

/* ---- Stack ------------------------------------------------------------- */

struct StackArgs {
    Alignment alignment = Alignment::topLeft();
    WidgetList children{};
    Key key{};
};

class Stack : public MultiChildRenderObjectWidget {
public:
    explicit Stack(StackArgs a) : args_(a) {
        key = a.key;
        children = a.children;
    }
    FMSUI_WIDGET(Stack)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    StackArgs args_;
};

struct PositionedArgs {
    float left = NAN;
    float top = NAN;
    float right = NAN;
    float bottom = NAN;
    float width = NAN;
    float height = NAN;
    Widget *child = nullptr;
    Key key{};
};

class Positioned : public SingleChildRenderObjectWidget {
public:
    explicit Positioned(PositionedArgs a) : args_(a) {
        key = a.key;
        child = a.child;
    }
    FMSUI_WIDGET(Positioned)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    PositionedArgs args_;
};

/* ---- Custom painting --------------------------------------------------- */

struct CustomPaintArgs {
    Painter painter{};
    /* Only consulted when there is no child and the constraints are loose. */
    Size preferred{0, 0};
    Widget *child = nullptr;  // painted on top of whatever the painter drew
    Key key{};
};

class CustomPaint : public SingleChildRenderObjectWidget {
public:
    explicit CustomPaint(CustomPaintArgs a) : args_(std::move(a)) {
        key = args_.key;
        child = args_.child;
    }
    FMSUI_WIDGET(CustomPaint)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    CustomPaintArgs args_;
};

/* ---- Overlay ----------------------------------------------------------- */

struct LayerArgs {
    /* A full-screen invisible catcher behind the child: tap anywhere else and
     * this fires. Without one, a popup can only be dismissed by hitting it. */
    VoidCallback on_barrier_tap{};
    Widget *child = nullptr;
    Key key{};
};

/* Paints its child last and takes no space where it sits. Anchor a popup by
 * putting the Layer where the popup should appear. */
class Layer : public SingleChildRenderObjectWidget {
public:
    explicit Layer(LayerArgs a) : args_(std::move(a)) {
        key = args_.key;
        child = args_.child;
    }
    FMSUI_WIDGET(Layer)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    LayerArgs args_;
};

/* ---- Input ------------------------------------------------------------- */

struct GestureDetectorArgs {
    VoidCallback on_tap{};
    VoidCallback on_press{};
    VoidCallback on_release{};
    Widget *child = nullptr;
    Key key{};
};

class GestureDetector : public SingleChildRenderObjectWidget {
public:
    explicit GestureDetector(GestureDetectorArgs a) : args_(std::move(a)) {
        key = args_.key;
        child = args_.child;
    }
    FMSUI_WIDGET(GestureDetector)

    RenderObject *createRenderObject() const override;
    void updateRenderObject(RenderObject *ro) const override;

private:
    GestureDetectorArgs args_;
};

}  // namespace fmsui
