#include "fmsui/widgets.h"

#include <cmath>

namespace fmsui {

/* ---- Text -------------------------------------------------------------- */

RenderObject *Text::createRenderObject() const { return new RenderText(); }

void Text::updateRenderObject(RenderObject *ro) const {
    auto *r = static_cast<RenderText *>(ro);
    /* Compare first: assigning an identical std::string still copies it, and
     * almost every label on the screen says the same thing it said last frame. */
    if (r->text != args_.text.view()) r->text.assign(args_.text.view());
    r->font = args_.font;
    r->color = args_.color;
    r->align = args_.align;
}

/* ---- DecoratedBox / Padding / Align / SizedBox -------------------------- */

RenderObject *DecoratedBox::createRenderObject() const { return new RenderDecoratedBox(); }

void DecoratedBox::updateRenderObject(RenderObject *ro) const {
    static_cast<RenderDecoratedBox *>(ro)->decoration = args_.decoration;
}

RenderObject *Padding::createRenderObject() const { return new RenderPadding(); }

void Padding::updateRenderObject(RenderObject *ro) const {
    static_cast<RenderPadding *>(ro)->padding = args_.padding;
}

RenderObject *Align::createRenderObject() const { return new RenderAlign(); }

void Align::updateRenderObject(RenderObject *ro) const {
    static_cast<RenderAlign *>(ro)->alignment = args_.alignment;
}

Widget *Center::build(BuildContext &ctx) const {
    (void)ctx;
    return new Align{{.alignment = Alignment::center(), .child = child_}};
}

RenderObject *SizedBox::createRenderObject() const { return new RenderConstrainedBox(); }

void SizedBox::updateRenderObject(RenderObject *ro) const {
    auto *r = static_cast<RenderConstrainedBox *>(ro);
    BoxConstraints c;  // unbounded on both axes by default
    if (!std::isnan(args_.width)) {
        c.minWidth = c.maxWidth = args_.width;
    }
    if (!std::isnan(args_.height)) {
        c.minHeight = c.maxHeight = args_.height;
    }
    r->additional = c;
}

/* ---- Container --------------------------------------------------------- */

Widget *Container::build(BuildContext &ctx) const {
    (void)ctx;

    Widget *w = args_.child;

    if (!std::isnan(args_.alignment.x)) {
        w = new Align{{.alignment = args_.alignment, .child = w}};
    }
    if (args_.padding != EdgeInsets{}) {
        w = new Padding{{.padding = args_.padding, .child = w}};
    }

    const BoxDecoration deco{args_.color, args_.border_color, args_.border_width, args_.radius};
    /* Skip the lv_obj entirely when there is nothing to draw. */
    if (deco != BoxDecoration{}) {
        w = new DecoratedBox{{.decoration = deco, .child = w}};
    }

    if (!std::isnan(args_.width) || !std::isnan(args_.height)) {
        w = new SizedBox{{.width = args_.width, .height = args_.height, .child = w}};
    }
    if (args_.margin != EdgeInsets{}) {
        w = new Padding{{.padding = args_.margin, .child = w}};
    }

    /* An empty Container still has to occupy a slot. */
    if (w == nullptr) w = new SizedBox{{}};
    return w;
}

/* ---- Flex -------------------------------------------------------------- */

namespace {

void applyFlex(RenderFlex *r, const FlexArgs &a, Axis axis) {
    r->direction = axis;
    r->main_axis = a.main;
    r->cross_axis = a.cross;
    r->main_size = a.main_size;
    r->spacing = a.spacing;
}

}  // namespace

RenderObject *Row::createRenderObject() const { return new RenderFlex(); }

void Row::updateRenderObject(RenderObject *ro) const {
    applyFlex(static_cast<RenderFlex *>(ro), args_, Axis::Horizontal);
}

RenderObject *Column::createRenderObject() const { return new RenderFlex(); }

void Column::updateRenderObject(RenderObject *ro) const {
    applyFlex(static_cast<RenderFlex *>(ro), args_, Axis::Vertical);
}

RenderObject *Flexible::createRenderObject() const { return new RenderFlexible(); }

void Flexible::updateRenderObject(RenderObject *ro) const {
    ro->flex = args_.flex;
    ro->fit = args_.fit;
}

RenderObject *Expanded::createRenderObject() const { return new RenderFlexible(); }

void Expanded::updateRenderObject(RenderObject *ro) const {
    ro->flex = args_.flex;
    ro->fit = FlexFit::Tight;
}

Widget *Spacer::build(BuildContext &ctx) const {
    (void)ctx;
    return new Expanded{{.flex = args_.flex, .child = new SizedBox{{}}}};
}

/* ---- Stack ------------------------------------------------------------- */

RenderObject *Stack::createRenderObject() const { return new RenderStack(); }

void Stack::updateRenderObject(RenderObject *ro) const {
    static_cast<RenderStack *>(ro)->alignment = args_.alignment;
}

RenderObject *Positioned::createRenderObject() const { return new RenderPositioned(); }

void Positioned::updateRenderObject(RenderObject *ro) const {
    auto *r = static_cast<RenderPositioned *>(ro);
    r->left = args_.left;
    r->top = args_.top;
    r->right = args_.right;
    r->bottom = args_.bottom;
    r->width = args_.width;
    r->height = args_.height;
}

/* ---- CustomPaint ------------------------------------------------------- */

RenderObject *CustomPaint::createRenderObject() const { return new RenderCustomPaint(); }

void CustomPaint::updateRenderObject(RenderObject *ro) const {
    auto *r = static_cast<RenderCustomPaint *>(ro);
    /* Copy the painter out of the arena: it is a std::function whose captures
     * would otherwise be overwritten by the next build pass, and LVGL calls it
     * from its own redraw, not from ours. */
    r->painter = args_.painter;
    r->preferred = args_.preferred;
    /* A repaint is not implied by a rebuild, so ask for one explicitly: the
     * painter may close over values that changed even though the size did not. */
    r->markNeedsRepaint();
}

/* ---- Layer ------------------------------------------------------------- */

RenderObject *Layer::createRenderObject() const { return new RenderLayer(); }

void Layer::updateRenderObject(RenderObject *ro) const {
    auto *r = static_cast<RenderLayer *>(ro);
    r->on_barrier_tap = args_.on_barrier_tap;
    r->barrier = static_cast<bool>(args_.on_barrier_tap);
}

/* ---- GestureDetector --------------------------------------------------- */

RenderObject *GestureDetector::createRenderObject() const { return new RenderGestureDetector(); }

void GestureDetector::updateRenderObject(RenderObject *ro) const {
    auto *r = static_cast<RenderGestureDetector *>(ro);
    /* Copy the callbacks out of the arena and into the RenderObject, which is
     * persistent -- a touch arriving between frames must not run a lambda whose
     * captures the next build pass is about to overwrite. */
    r->on_tap = args_.on_tap;
    r->on_press = args_.on_press;
    r->on_release = args_.on_release;
}

}  // namespace fmsui
