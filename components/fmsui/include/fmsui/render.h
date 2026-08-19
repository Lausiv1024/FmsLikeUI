#pragma once

/* The RenderObject layer: layout and painting.
 *
 * Layout is Flutter's: constraints go down, sizes come back up, and the parent
 * places the child.  Painting is where we meet LVGL -- a RenderObject that
 * actually puts pixels on the screen owns one lv_obj, and layout-only nodes
 * (Padding, Column, Align) own nothing at all.  Every lv_obj is a direct child
 * of one root object and is positioned in absolute screen coordinates, so the
 * LVGL tree stays flat and shallow no matter how deep the widget tree gets.
 *
 * Paint order is enforced by index: the paint walk hands out consecutive child
 * indices, which is what LVGL uses for z-order, so a Stack's later children end
 * up on top exactly as the widget tree says they should.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "lvgl.h"

#include "fmsui/foundation.h"

namespace fmsui {

class RenderObject;

class RenderLayer;

class PaintContext {
public:
    lv_obj_t *root = nullptr;
    /* Next LVGL child index to assign; also the running count of live objects. */
    int32_t next_index = 0;

    /* Layers found during the main pass, to be painted after it.  This is what
     * puts a dropdown's popup over the widgets that come after it in the tree:
     * z-order here is paint order, and the main pass has no way to jump ahead. */
    std::vector<std::pair<RenderLayer *, Offset>> deferred;
    bool overlay_pass = false;

    /* How many lv_objs this pass had to create. Creating one is far more
     * expensive than updating one, so the two have to be told apart before any
     * optimisation is attempted. */
    uint32_t created = 0;

    /* ...and how many had to be moved in the parent's child order.  Each move
     * memmoves the parent's child array and invalidates the old and new areas,
     * which is what makes reordering a long list expensive. */
    uint32_t moved = 0;
};

enum class FlexFit : uint8_t { Tight, Loose };

class RenderObject {
public:
    virtual ~RenderObject();

    RenderObject *parent = nullptr;

    Size size;                  /* set by performLayout() */
    Offset offset;              /* position within the parent, set by the parent */
    BoxConstraints constraints; /* what the parent asked for */

    /* Flex participation, written by RenderFlexible (Expanded / Flexible) and
     * read by RenderFlex.  Zero means "not flexible". */
    int flex = 0;
    FlexFit fit = FlexFit::Loose;

    void layout(BoxConstraints c);
    virtual void performLayout() = 0;

    /* Paints this object and then its children, at an absolute screen origin. */
    virtual void paint(PaintContext &ctx, Offset origin);

    /* Lets RenderStack tell its two kinds of children apart without RTTI, which
     * ESP-IDF turns off by default. */
    virtual bool isPositioned() const { return false; }

    void setChildren(const std::vector<RenderObject *> &kids);
    const std::vector<RenderObject *> &children() const { return children_; }
    RenderObject *firstChild() const { return children_.empty() ? nullptr : children_.front(); }

protected:
    void paintChildren(PaintContext &ctx, Offset origin);

    /* Children are owned by their Elements, not by us. */
    std::vector<RenderObject *> children_;
};

/* Base for anything that owns an lv_obj. */
class RenderLv : public RenderObject {
public:
    ~RenderLv() override;

    void paint(PaintContext &ctx, Offset origin) override;

    /* Tell LVGL this object's pixels are stale.  Position and size changes are
     * detected automatically; this is for the cases we cannot see -- chiefly a
     * CustomPaint whose painter closes over a value that moved. */
    void markNeedsRepaint() {
        if (lv_ != nullptr) lv_obj_invalidate(lv_);
    }

protected:
    virtual lv_obj_t *createLv(lv_obj_t *parent) = 0;

    /* Push whatever changed into LVGL.  Called after the object exists and has
     * been positioned. Implementations must compare before they write: an
     * unnecessary lv_label_set_text() reallocates and invalidates the area. */
    virtual void syncLv(lv_obj_t *obj) { (void)obj; }

    lv_obj_t *lv_ = nullptr;

private:
    Offset painted_pos_{-1e9F, -1e9F};
    Size painted_size_{-1, -1};
};

/* ---- Leaves ------------------------------------------------------------ */

class RenderText : public RenderLv {
public:
    /* Written by the widget; each setter marks layout or paint dirty only when
     * the value actually moves. */
    std::string text;
    const lv_font_t *font = nullptr;
    Color color = Color::rgb(0xFFFFFF);
    TextAlign align = TextAlign::Left;

    void performLayout() override;

protected:
    lv_obj_t *createLv(lv_obj_t *parent) override;
    void syncLv(lv_obj_t *obj) override;

private:
    /* Measuring text means walking the string and summing glyph widths, and
     * layout runs over the whole tree on every rebuild. On a page with 150
     * labels that was most of the layout pass, all of it re-deriving answers
     * that had not changed. */
    std::string measured_text_;
    const lv_font_t *measured_font_ = nullptr;
    float measured_max_width_ = -1;
    Size measured_{0, 0};

    std::string applied_text_;
    const lv_style_t *applied_style_ = nullptr;
};

/* A filled and/or framed rectangle -- the shape the whole FMS UI is made of. */
struct BoxDecoration {
    Color color = Color::transparent();
    Color border_color = Color::transparent();
    float border_width = 0;
    float radius = 0;

    bool operator==(const BoxDecoration &o) const {
        return color == o.color && border_color == o.border_color &&
               border_width == o.border_width && radius == o.radius;
    }
    bool operator!=(const BoxDecoration &o) const { return !(*this == o); }
};

class RenderDecoratedBox : public RenderLv {
public:
    BoxDecoration decoration;

    void performLayout() override;

protected:
    lv_obj_t *createLv(lv_obj_t *parent) override;
    void syncLv(lv_obj_t *obj) override;

private:
    const lv_style_t *applied_style_ = nullptr;
};

/* ---- Custom painting --------------------------------------------------- */

/* A thin drawing surface over an LVGL draw layer, in coordinates local to the
 * widget.  It exists so the shapes that make an FMS look like an FMS -- the
 * slanted tabs, the dropdown triangle, the radio dot -- can be drawn as shapes
 * rather than smuggled in as font glyphs.  Drawn that way they scale and recolour
 * with the widget, and they do not constrain which font we can use.
 */
class Canvas {
public:
    Canvas(lv_layer_t *layer, Offset origin) : layer_(layer), origin_(origin) {}

    void fillRect(Rect r, Color c);
    void strokeRect(Rect r, Color c, float width);
    void line(Offset a, Offset b, Color c, float width);

    /* Filled by fanning triangles from the first point, so it is right for any
     * convex polygon -- which is all we need. The outline is a closed polyline. */
    void polygon(const Offset *points, size_t n, Color fill, Color stroke, float stroke_width);

    void circle(Offset center, float radius, Color fill, Color stroke, float stroke_width);

private:
    lv_layer_t *layer_;
    Offset origin_;  /* where this widget's (0,0) is on the screen */
};

/* `size` is the widget's laid-out size; paint within it. */
using Painter = std::function<void(Canvas &, Size)>;

class RenderCustomPaint : public RenderLv {
public:
    Painter painter;
    /* Used only when there is no child and the constraints are loose. */
    Size preferred{0, 0};

    void performLayout() override;

protected:
    lv_obj_t *createLv(lv_obj_t *parent) override;

private:
    static void drawCb(lv_event_t *e);
};

/* An invisible lv_obj that exists only to catch touches. */
class RenderGestureDetector : public RenderLv {
public:
    std::function<void()> on_tap;
    std::function<void()> on_press;
    std::function<void()> on_release;

    void performLayout() override;

protected:
    lv_obj_t *createLv(lv_obj_t *parent) override;
    void syncLv(lv_obj_t *obj) override;

private:
    static void handleEvent(lv_event_t *e);

    /* Whether LV_OBJ_FLAG_CLICKABLE is currently set, so syncLv only touches
     * LVGL when it actually changes.  Detectors are emitted unconditionally so
     * that the tree keeps its shape, which means one with no callbacks at all is
     * normal and must be as invisible to a touch as it is to the eye. */
    bool clickable_ = false;
};

/* ---- Layout-only (no lv_obj) ------------------------------------------- */

class RenderPadding : public RenderObject {
public:
    EdgeInsets padding;
    void performLayout() override;
};

class RenderAlign : public RenderObject {
public:
    Alignment alignment;
    /* When bounded, an Align fills the space it is given; when the incoming
     * constraint is unbounded on an axis it shrink-wraps the child instead. */
    void performLayout() override;
};

class RenderConstrainedBox : public RenderObject {
public:
    BoxConstraints additional;
    void performLayout() override;
};

class RenderFlexible : public RenderObject {
public:
    void performLayout() override;
};

class RenderFlex : public RenderObject {
public:
    Axis direction = Axis::Vertical;
    MainAxis main_axis = MainAxis::Start;
    CrossAxis cross_axis = CrossAxis::Center;
    MainAxisSize main_size = MainAxisSize::Max;
    float spacing = 0;

    void performLayout() override;
};

class RenderStack : public RenderObject {
public:
    Alignment alignment = Alignment::topLeft();
    void performLayout() override;
};

/* Positioned children of a Stack carry their edges here. */
class RenderPositioned : public RenderObject {
public:
    /* NaN means "not set on this edge". */
    float left = NAN;
    float top = NAN;
    float right = NAN;
    float bottom = NAN;
    float width = NAN;
    float height = NAN;

    bool isPositioned() const override { return true; }
    void performLayout() override;
};

/* ---- Overlay ----------------------------------------------------------- */

/* Paints its child after everything else, and takes no space where it sits.
 *
 * A dropdown's popup has to be drawn over whatever comes after the dropdown in
 * the tree, and it must not push the row it lives in around.  Both fall out of
 * this: zero size in the flow, and a paint deferred to the end.
 *
 * The child is anchored where the Layer sits, so a popup lands directly under
 * the control that opened it.
 */
class RenderLayer : public RenderObject {
public:
    ~RenderLayer() override;

    /* An invisible full-screen catcher behind the child, so a tap anywhere else
     * dismisses the popup. */
    bool barrier = false;
    std::function<void()> on_barrier_tap;

    void performLayout() override;
    void paint(PaintContext &ctx, Offset origin) override;

private:
    static void barrierCb(lv_event_t *e);

    lv_obj_t *barrier_lv_ = nullptr;
};

/* ---- Root -------------------------------------------------------------- */

class RenderView : public RenderObject {
public:
    Size screen;
    void performLayout() override;
};

}  // namespace fmsui
