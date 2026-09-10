#include "fmsui/render.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace fmsui {
namespace {

inline int32_t px(float v) { return static_cast<int32_t>(lroundf(v)); }

inline lv_color_t toLv(Color c) { return lv_color_make(c.r, c.g, c.b); }

/* Strip the styling and behaviour lv_obj_create() gives away for free.  We want
 * a bare rectangle: no default grey background, no border, no scrolling, and --
 * importantly -- not clickable, or a decoration would swallow the touch meant
 * for the GestureDetector underneath it. */
void makeInert(lv_obj_t *o) {
    /* remove_style_all leaves the object with LVGL's built-in defaults, and those
     * are already zero padding, no border, transparent background. Setting the
     * padding again would add four properties to a local style -- four
     * reallocations, on every object, for no change. */
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_text_align_t toLvAlign(TextAlign a) {
    switch (a) {
        case TextAlign::Center:
            return LV_TEXT_ALIGN_CENTER;
        case TextAlign::Right:
            return LV_TEXT_ALIGN_RIGHT;
        case TextAlign::Left:
            break;
    }
    return LV_TEXT_ALIGN_LEFT;
}

/* Shared styles, instead of a local style per object.
 *
 * lv_obj_set_style_x() writes into the object's *local* style, and LVGL grows
 * that style's property array one property at a time -- so six setters on a
 * fresh object is six reallocations, out of a heap that lives in PSRAM. Measured
 * on the device: creating an lv_obj cost 1.85ms.
 *
 * A UI has few distinct looks even when it has many objects: this page has ~150
 * labels but only a handful of (font, colour, alignment) combinations. Building
 * each combination once and sharing it turns those reallocations into a single
 * lv_obj_add_style().
 *
 * Entries are never evicted, and the addresses never move. An lv_obj holds a
 * bare pointer to the lv_style_t it was given, so dropping one under an LRU
 * would leave live objects reading freed memory -- and there is nothing to
 * evict for: colours, fonts, alignments, borders and radii come from a theme and
 * a screen design, so the set is finite and the count settles. Generating a
 * fresh colour or radius per frame, or animating a style continuously, is
 * outside what this assumes.
 *
 * What bounds it is therefore the design, and the way to find that bound is to
 * measure it -- styleCacheStats() on the catalog screen -- rather than to pick a
 * cap up front and truncate at it. The cache lives as long as the LVGL session;
 * releaseStyleCache() ends it, once every object that used one is gone.
 */
class StyleCache {
public:
    ~StyleCache() { clear(); }

    const lv_style_t *text(const lv_font_t *font, Color color, TextAlign align) {
        for (const auto &e : text_) {
            if (e->font == font && e->color == color && e->align == align) return &e->style;
        }
        auto e = std::make_unique<TextEntry>(TextEntry{font, color, align, {}});
        lv_style_init(&e->style);
        lv_style_set_text_font(&e->style, font);
        lv_style_set_text_color(&e->style, toLv(color));
        lv_style_set_text_opa(&e->style, color.a);
        lv_style_set_text_align(&e->style, toLvAlign(align));
        text_.push_back(std::move(e));
        publishStats();
        return &text_.back()->style;
    }

    const lv_style_t *box(const BoxDecoration &d) {
        for (const auto &e : box_) {
            if (e->deco == d) return &e->style;
        }
        auto e = std::make_unique<BoxEntry>(BoxEntry{d, {}});
        lv_style_init(&e->style);
        lv_style_set_bg_color(&e->style, toLv(d.color));
        lv_style_set_bg_opa(&e->style, d.color.a);
        lv_style_set_border_color(&e->style, toLv(d.border_color));
        lv_style_set_border_opa(&e->style, d.border_color.a);
        lv_style_set_border_width(&e->style, px(d.border_width));
        lv_style_set_radius(&e->style, px(d.radius));
        box_.push_back(std::move(e));
        publishStats();
        return &box_.back()->style;
    }

    StyleCacheStats stats() const {
        const std::lock_guard<std::mutex> lock(stats_mutex_);
        return published_stats_;
    }

    void clear() {
        for (const auto &e : text_) lv_style_reset(&e->style);
        for (const auto &e : box_) lv_style_reset(&e->style);
        text_.clear();
        box_.clear();
        publishStats();
    }

private:
    /* Rendering owns the vectors and is single-task. Diagnostics are read from
     * another task on the device, so they get a separately published snapshot
     * instead of touching vector::size() while a push_back may be in flight. */
    void publishStats() {
        const StyleCacheStats next{static_cast<uint32_t>(text_.size()),
                                   static_cast<uint32_t>(box_.size())};
        const std::lock_guard<std::mutex> lock(stats_mutex_);
        published_stats_ = next;
    }

    /* One heap block per entry, so &style stays put as the vectors grow. */
    struct TextEntry {
        const lv_font_t *font;
        Color color;
        TextAlign align;
        lv_style_t style;
    };
    struct BoxEntry {
        BoxDecoration deco;
        lv_style_t style;
    };

    std::vector<std::unique_ptr<TextEntry>> text_;
    std::vector<std::unique_ptr<BoxEntry>> box_;
    mutable std::mutex stats_mutex_;
    StyleCacheStats published_stats_{};
};

StyleCache &styleCache() {
    static StyleCache cache;
    return cache;
}

/* Clamp `a` so it can be satisfied within `c`. */
BoxConstraints enforce(BoxConstraints a, BoxConstraints c) {
    BoxConstraints r;
    r.minWidth = c.constrainWidth(a.minWidth);
    r.maxWidth = c.constrainWidth(a.maxWidth);
    r.minHeight = c.constrainHeight(a.minHeight);
    r.maxHeight = c.constrainHeight(a.maxHeight);
    if (r.minWidth > r.maxWidth) r.minWidth = r.maxWidth;
    if (r.minHeight > r.maxHeight) r.minHeight = r.maxHeight;
    return r;
}

}  // namespace

StyleCacheStats styleCacheStats() { return styleCache().stats(); }

void releaseStyleCache() { styleCache().clear(); }

/* ---- RenderObject ------------------------------------------------------ */

RenderObject::~RenderObject() = default;

void RenderObject::layout(BoxConstraints c) {
    constraints = c;
    performLayout();
}

void RenderObject::setChildren(const std::vector<RenderObject *> &kids) {
    /* The reconciler calls this for every render object on every rebuild, and the
     * list is almost always the same one as last time. Copying it anyway meant a
     * vector assignment per object per frame, out of a heap that lives in PSRAM. */
    if (children_ == kids) return;

    children_ = kids;
    for (RenderObject *c : children_) c->parent = this;
}

void RenderObject::paintChildren(PaintContext &ctx, Offset origin) {
    for (RenderObject *c : children_) c->paint(ctx, origin + c->offset);
}

void RenderObject::paint(PaintContext &ctx, Offset origin) { paintChildren(ctx, origin); }

/* ---- RenderLv ---------------------------------------------------------- */

RenderLv::~RenderLv() {
    if (lv_ != nullptr) lv_obj_delete(lv_);
}

void RenderLv::paint(PaintContext &ctx, Offset origin) {
    if (lv_ == nullptr) {
        lv_ = createLv(ctx.root);
        ctx.created++;
    }

    if (painted_pos_ != origin) {
        lv_obj_set_pos(lv_, px(origin.dx), px(origin.dy));
        painted_pos_ = origin;
    }
    if (painted_size_ != size) {
        lv_obj_set_size(lv_, px(size.width), px(size.height));
        painted_size_ = size;
    }

    syncLv(ctx, lv_);

    /* LVGL draws children in index order, so handing out consecutive indices in
     * paint order is what makes a Stack's later children land on top. */
    if (lv_obj_get_index(lv_) != static_cast<uint32_t>(ctx.next_index)) {
        lv_obj_move_to_index(lv_, ctx.next_index);
        ctx.moved++;
    }
    ctx.next_index++;

    paintChildren(ctx, origin);
}

/* ---- RenderText -------------------------------------------------------- */

void RenderText::performLayout() {
    const lv_font_t *f = font != nullptr ? font : LV_FONT_DEFAULT;
    const float max_width = constraints.hasBoundedWidth() ? constraints.maxWidth : kInf;

    /* Only re-measure when something that affects the measurement moved. */
    if (measured_font_ != f || measured_max_width_ != max_width || measured_text_ != text) {
        lv_point_t measured;
        lv_text_get_size(&measured, text.c_str(), f, 0, 0,
                         max_width == kInf ? LV_COORD_MAX : px(max_width), LV_TEXT_FLAG_NONE);
        measured_ = Size{static_cast<float>(measured.x), static_cast<float>(measured.y)};
        measured_font_ = f;
        measured_max_width_ = max_width;
        measured_text_ = text;
    }

    size = constraints.constrain(measured_);
}

lv_obj_t *RenderText::createLv(lv_obj_t *parent) {
    lv_obj_t *o = lv_label_create(parent);
    makeInert(o);
    lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    /* A new lv_label says "Text".  syncLv only writes the string when it differs
     * from what it last wrote, and it has written nothing yet -- so an empty
     * Text would keep LVGL's placeholder and put the word "Text" on the screen.
     * Start the object in the state syncLv believes it is in. */
    lv_label_set_text(o, "");
    return o;
}

void RenderText::syncLv(PaintContext &ctx, lv_obj_t *obj) {
    const lv_style_t *want =
        styleCache().text(font != nullptr ? font : LV_FONT_DEFAULT, color, align);
    if (applied_style_ != want) {
        if (applied_style_ != nullptr) {
            lv_obj_remove_style(obj, const_cast<lv_style_t *>(applied_style_), LV_PART_MAIN);
        }
        lv_obj_add_style(obj, const_cast<lv_style_t *>(want), 0);
        applied_style_ = want;
    }

    /* lv_label_set_text reallocates and invalidates, so only call it when the
     * string really moved. */
    if (applied_text_ != text) {
        lv_label_set_text(obj, text.c_str());
        applied_text_ = text;
        ctx.retexted++;
    }
}

/* ---- RenderDecoratedBox ------------------------------------------------ */

void RenderDecoratedBox::performLayout() {
    RenderObject *c = firstChild();
    if (c == nullptr) {
        /* No child: fill whatever we are given, like Flutter's Container. */
        size = Size{constraints.hasBoundedWidth() ? constraints.maxWidth : constraints.minWidth,
                    constraints.hasBoundedHeight() ? constraints.maxHeight : constraints.minHeight};
        return;
    }
    c->layout(constraints);
    c->offset = Offset{0, 0};
    size = constraints.constrain(c->size);
}

lv_obj_t *RenderDecoratedBox::createLv(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    makeInert(o);
    return o;
}

void RenderDecoratedBox::syncLv(PaintContext &ctx, lv_obj_t *obj) {
    const lv_style_t *want = styleCache().box(decoration);
    if (applied_style_ == want) return;

    if (applied_style_ != nullptr) {
        lv_obj_remove_style(obj, const_cast<lv_style_t *>(applied_style_), LV_PART_MAIN);
    }
    lv_obj_add_style(obj, const_cast<lv_style_t *>(want), 0);
    applied_style_ = want;
}

/* ---- Canvas ------------------------------------------------------------ */

namespace {

lv_point_precise_t pt(Offset origin, Offset p) {
    lv_point_precise_t r;
    r.x = origin.dx + p.dx;
    r.y = origin.dy + p.dy;
    return r;
}

}  // namespace

void Canvas::fillRect(Rect r, Color c) {
    if (c.a == 0) return;

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = toLv(c);
    d.bg_opa = c.a;
    d.border_width = 0;
    d.radius = 0;

    lv_area_t a;
    a.x1 = px(origin_.dx + r.left);
    a.y1 = px(origin_.dy + r.top);
    a.x2 = px(origin_.dx + r.right) - 1;
    a.y2 = px(origin_.dy + r.bottom) - 1;
    lv_draw_rect(layer_, &d, &a);
}

void Canvas::strokeRect(Rect r, Color c, float width) {
    if (c.a == 0 || width <= 0) return;

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = toLv(c);
    d.border_opa = c.a;
    d.border_width = px(width);
    d.radius = 0;

    lv_area_t a;
    a.x1 = px(origin_.dx + r.left);
    a.y1 = px(origin_.dy + r.top);
    a.x2 = px(origin_.dx + r.right) - 1;
    a.y2 = px(origin_.dy + r.bottom) - 1;
    lv_draw_rect(layer_, &d, &a);
}

void Canvas::line(Offset a, Offset b, Color c, float width) {
    if (c.a == 0 || width <= 0) return;

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = toLv(c);
    d.opa = c.a;
    d.width = width;
    d.p1 = pt(origin_, a);
    d.p2 = pt(origin_, b);
    lv_draw_line(layer_, &d);
}

void Canvas::polygon(const Offset *points, size_t n, Color fill, Color stroke,
                     float stroke_width) {
    if (n < 3) return;

    if (fill.a != 0) {
        lv_draw_triangle_dsc_t d;
        lv_draw_triangle_dsc_init(&d);
        d.color = toLv(fill);
        d.opa = fill.a;

        /* Fan from point 0. Correct for any convex polygon, which is all the FMS
         * shapes are (trapezoid tabs, triangles). */
        for (size_t i = 1; i + 1 < n; i++) {
            d.p[0] = pt(origin_, points[0]);
            d.p[1] = pt(origin_, points[i]);
            d.p[2] = pt(origin_, points[i + 1]);
            lv_draw_triangle(layer_, &d);
        }
    }

    if (stroke.a != 0 && stroke_width > 0) {
        for (size_t i = 0; i < n; i++) {
            line(points[i], points[(i + 1) % n], stroke, stroke_width);
        }
    }
}

void Canvas::circle(Offset center, float radius, Color fill, Color stroke, float stroke_width) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = toLv(fill);
    d.bg_opa = fill.a;
    d.border_color = toLv(stroke);
    d.border_opa = stroke.a;
    d.border_width = px(stroke_width);

    lv_area_t a;
    a.x1 = px(origin_.dx + center.dx - radius);
    a.y1 = px(origin_.dy + center.dy - radius);
    a.x2 = px(origin_.dx + center.dx + radius) - 1;
    a.y2 = px(origin_.dy + center.dy + radius) - 1;
    lv_draw_rect(layer_, &d, &a);
}

/* ---- RenderCustomPaint ------------------------------------------------- */

void RenderCustomPaint::performLayout() {
    RenderObject *c = firstChild();
    if (c != nullptr) {
        c->layout(constraints);
        c->offset = Offset{0, 0};
        size = constraints.constrain(c->size);
        return;
    }
    if (preferred.width > 0 || preferred.height > 0) {
        size = constraints.constrain(preferred);
        return;
    }
    /* Nothing to go on: fill what we are given. */
    size = Size{constraints.hasBoundedWidth() ? constraints.maxWidth : constraints.minWidth,
                constraints.hasBoundedHeight() ? constraints.maxHeight : constraints.minHeight};
}

lv_obj_t *RenderCustomPaint::createLv(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    makeInert(o);
    lv_obj_add_event_cb(o, drawCb, LV_EVENT_DRAW_MAIN, this);
    return o;
}

void RenderCustomPaint::drawCb(lv_event_t *e) {
    auto *self = static_cast<RenderCustomPaint *>(lv_event_get_user_data(e));
    if (!self->painter) return;

    auto *obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    /* The draw layer works in absolute screen coordinates, so hand the painter a
     * canvas that has been shifted to the widget's own origin. */
    Canvas canvas(layer, Offset{static_cast<float>(coords.x1), static_cast<float>(coords.y1)});
    self->painter(canvas, self->size);
}

/* ---- RenderGestureDetector --------------------------------------------- */

void RenderGestureDetector::performLayout() {
    RenderObject *c = firstChild();
    if (c == nullptr) {
        size = constraints.smallest();
        return;
    }
    c->layout(constraints);
    c->offset = Offset{0, 0};
    size = c->size;
}

lv_obj_t *RenderGestureDetector::createLv(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    makeInert(o);
    /* Invisible, but it is the one thing in the tree that *is* clickable --
     * once syncLv has seen a callback worth catching a touch for. */
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(o, handleEvent, LV_EVENT_ALL, this);
    clickable_ = false;  // makeInert already cleared the flag
    return o;
}

void RenderGestureDetector::syncLv(PaintContext &ctx, lv_obj_t *obj) {
    /* A detector with nothing to call must not take the touch: LVGL's hit test
     * stops at the topmost clickable object, so leaving the flag on would let a
     * disabled button swallow a tap meant for a dropdown's barrier behind it --
     * which is exactly what the old "no callback, no detector" shape gave us for
     * free. */
    const bool want = static_cast<bool>(on_tap) || static_cast<bool>(on_press) ||
                      static_cast<bool>(on_release);
    if (want == clickable_) return;

    if (want) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
    clickable_ = want;
}

void RenderGestureDetector::handleEvent(lv_event_t *e) {
    auto *self = static_cast<RenderGestureDetector *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:
            if (self->on_press) self->on_press();
            break;
        case LV_EVENT_RELEASED:
            if (self->on_release) self->on_release();
            break;
        case LV_EVENT_CLICKED:
            if (self->on_tap) self->on_tap();
            break;
        default:
            break;
    }
}

/* ---- Layout-only ------------------------------------------------------- */

void RenderPadding::performLayout() {
    RenderObject *c = firstChild();
    if (c == nullptr) {
        size = constraints.constrain(Size{padding.horizontal(), padding.vertical()});
        return;
    }
    c->layout(constraints.deflate(padding));
    c->offset = Offset{padding.left, padding.top};
    size = constraints.constrain(Size{c->size.width + padding.horizontal(),
                                      c->size.height + padding.vertical()});
}

void RenderAlign::performLayout() {
    RenderObject *c = firstChild();
    if (c == nullptr) {
        size = constraints.constrain(Size{0, 0});
        return;
    }
    c->layout(constraints.loosen());

    /* Fill the space on any axis that is bounded; shrink-wrap where it is not. */
    size = constraints.constrain(
        Size{constraints.hasBoundedWidth() ? constraints.maxWidth : c->size.width,
             constraints.hasBoundedHeight() ? constraints.maxHeight : c->size.height});
    c->offset = alignment.inscribe(c->size, size);
}

void RenderConstrainedBox::performLayout() {
    const BoxConstraints c = enforce(additional, constraints);
    RenderObject *child = firstChild();
    if (child == nullptr) {
        size = c.constrain(Size{0, 0});
        return;
    }
    child->layout(c);
    child->offset = Offset{0, 0};
    size = child->size;
}

void RenderFlexible::performLayout() {
    RenderObject *c = firstChild();
    if (c == nullptr) {
        size = constraints.smallest();
        return;
    }
    c->layout(constraints);
    c->offset = Offset{0, 0};
    size = c->size;
}

void RenderFlex::performLayout() {
    const bool horiz = direction == Axis::Horizontal;

    auto mainOf = [horiz](Size s) { return horiz ? s.width : s.height; };
    auto crossOf = [horiz](Size s) { return horiz ? s.height : s.width; };

    const float max_main = horiz ? constraints.maxWidth : constraints.maxHeight;
    const float min_main = horiz ? constraints.minWidth : constraints.minHeight;
    const float max_cross = horiz ? constraints.maxHeight : constraints.maxWidth;
    const float min_cross = horiz ? constraints.minHeight : constraints.minWidth;

    const size_t n = children_.size();
    const float total_spacing = n > 1 ? spacing * static_cast<float>(n - 1) : 0;

    /* Stretch means "all children the same size across", but the same as *what*?
     *
     * When the parent has already fixed our cross extent (min == max, which is
     * the usual case -- a page column inside an Expanded), the answer is: that.
     *
     * When it has not -- a dropdown's popup, sized only by an upper bound of the
     * whole screen -- filling the bound would make the popup as wide as the
     * display. Flutter fills, and gives you IntrinsicWidth to say otherwise. We
     * shrink-wrap to the widest child instead: a second layout pass, but only on
     * the rare loose-and-stretching case, and it is what the caller meant. */
    const bool cross_is_tight = (min_cross >= max_cross) && max_cross != kInf;
    const bool stretch_to_widest = cross_axis == CrossAxis::Stretch && !cross_is_tight;

    float stretch_cross = 0;  /* filled in by the measuring pass, if there is one */

    auto childConstraints = [&](float lo_main, float hi_main) {
        float lo_cross = 0;
        if (cross_axis == CrossAxis::Stretch) {
            if (stretch_to_widest) {
                lo_cross = stretch_cross;  /* 0 during the measuring pass */
            } else if (max_cross != kInf) {
                lo_cross = max_cross;
            }
        }
        const float hi_cross = stretch_to_widest && stretch_cross > 0 ? stretch_cross : max_cross;
        if (horiz) return BoxConstraints{lo_main, hi_main, lo_cross, hi_cross};
        return BoxConstraints{lo_cross, hi_cross, lo_main, hi_main};
    };

    if (stretch_to_widest) {
        /* Measure: lay the children out loose across, and remember the widest.
         *
         * The constraint is built here rather than through childConstraints(),
         * which closes over stretch_cross by reference -- reading the accumulator
         * we are in the middle of filling would make every child after the first
         * tight at the first one's width. */
        const BoxConstraints loose_cross =
            horiz ? BoxConstraints{0, kInf, 0, max_cross} : BoxConstraints{0, max_cross, 0, kInf};
        for (RenderObject *c : children_) {
            c->layout(loose_cross);
            stretch_cross = std::max(stretch_cross, crossOf(c->size));
        }
        stretch_cross = std::max(stretch_cross, min_cross);
        if (max_cross != kInf) stretch_cross = std::min(stretch_cross, max_cross);
        /* The passes below now lay everything out again, tight at stretch_cross. */
    }

    /* Pass 1: inflexible children take their natural size along the main axis. */
    int total_flex = 0;
    float allocated = 0;
    float cross_max = 0;
    for (RenderObject *c : children_) {
        if (c->flex > 0) {
            total_flex += c->flex;
            continue;
        }
        c->layout(childConstraints(0, kInf));
        allocated += mainOf(c->size);
        cross_max = std::max(cross_max, crossOf(c->size));
    }

    /* Pass 2: share out what is left among the flexible ones.  With an
     * unbounded main axis there is nothing to share, so they take their natural
     * size too -- an Expanded inside an unbounded Column is a mistake, but it
     * should degrade rather than produce an infinite size. */
    if (total_flex > 0) {
        const bool bounded = max_main != kInf;
        const float free_space =
            bounded ? std::max(0.0F, max_main - allocated - total_spacing) : 0.0F;
        const float per_flex = bounded ? free_space / static_cast<float>(total_flex) : 0.0F;

        for (RenderObject *c : children_) {
            if (c->flex <= 0) continue;
            if (bounded) {
                const float extent = per_flex * static_cast<float>(c->flex);
                c->layout(childConstraints(c->fit == FlexFit::Tight ? extent : 0, extent));
            } else {
                c->layout(childConstraints(0, kInf));
            }
            allocated += mainOf(c->size);
            cross_max = std::max(cross_max, crossOf(c->size));
        }
    }

    /* Our own size. */
    float main_extent = (main_size == MainAxisSize::Max && max_main != kInf)
                            ? max_main
                            : allocated + total_spacing;
    main_extent = std::max(main_extent, min_main);
    if (max_main != kInf) main_extent = std::min(main_extent, max_main);

    float cross_extent = cross_max;
    if (cross_axis == CrossAxis::Stretch) {
        if (stretch_to_widest) {
            cross_extent = stretch_cross;
        } else if (max_cross != kInf) {
            cross_extent = max_cross;
        }
    }
    cross_extent = std::max(cross_extent, min_cross);
    if (max_cross != kInf) cross_extent = std::min(cross_extent, max_cross);

    size = horiz ? Size{main_extent, cross_extent} : Size{cross_extent, main_extent};

    /* Placement. */
    const float remaining = std::max(0.0F, main_extent - allocated - total_spacing);
    float leading = 0;
    float between = spacing;
    switch (main_axis) {
        case MainAxis::Start:
            break;
        case MainAxis::End:
            leading = remaining;
            break;
        case MainAxis::Center:
            leading = remaining / 2;
            break;
        case MainAxis::SpaceBetween:
            if (n > 1) between += remaining / static_cast<float>(n - 1);
            break;
        case MainAxis::SpaceAround:
            if (n > 0) {
                const float gap = remaining / static_cast<float>(n);
                leading = gap / 2;
                between += gap;
            }
            break;
        case MainAxis::SpaceEvenly: {
            const float gap = remaining / static_cast<float>(n + 1);
            leading = gap;
            between += gap;
            break;
        }
    }

    float pos = leading;
    for (RenderObject *c : children_) {
        float cross_pos = 0;
        switch (cross_axis) {
            case CrossAxis::Start:
            case CrossAxis::Stretch:
                cross_pos = 0;
                break;
            case CrossAxis::End:
                cross_pos = cross_extent - crossOf(c->size);
                break;
            case CrossAxis::Center:
                cross_pos = (cross_extent - crossOf(c->size)) / 2;
                break;
        }
        c->offset = horiz ? Offset{pos, cross_pos} : Offset{cross_pos, pos};
        pos += mainOf(c->size) + between;
    }
}

void RenderStack::performLayout() {
    const BoxConstraints loose = constraints.loosen();

    bool has_unpositioned = false;
    float w = 0;
    float h = 0;
    for (RenderObject *c : children_) {
        if (c->isPositioned()) continue;
        has_unpositioned = true;
        c->layout(loose);
        w = std::max(w, c->size.width);
        h = std::max(h, c->size.height);
    }

    size = has_unpositioned
               ? constraints.constrain(Size{w, h})
               : Size{constraints.hasBoundedWidth() ? constraints.maxWidth : constraints.minWidth,
                      constraints.hasBoundedHeight() ? constraints.maxHeight : constraints.minHeight};

    for (RenderObject *c : children_) {
        if (c->isPositioned()) {
            /* A Positioned lays itself out against the stack's final size. */
            c->layout(BoxConstraints::tight(size));
            c->offset = Offset{0, 0};
        } else {
            c->offset = alignment.inscribe(c->size, size);
        }
    }
}

void RenderPositioned::performLayout() {
    /* `constraints` here is the stack's size, made tight by RenderStack. */
    const float stack_w = constraints.maxWidth;
    const float stack_h = constraints.maxHeight;

    RenderObject *c = firstChild();
    size = Size{stack_w, stack_h};
    if (c == nullptr) return;

    const bool has_l = !std::isnan(left);
    const bool has_t = !std::isnan(top);
    const bool has_r = !std::isnan(right);
    const bool has_b = !std::isnan(bottom);

    BoxConstraints cc{0, stack_w, 0, stack_h};
    if (!std::isnan(width)) {
        cc.minWidth = cc.maxWidth = width;
    } else if (has_l && has_r) {
        const float ww = std::max(0.0F, stack_w - left - right);
        cc.minWidth = cc.maxWidth = ww;
    }
    if (!std::isnan(height)) {
        cc.minHeight = cc.maxHeight = height;
    } else if (has_t && has_b) {
        const float hh = std::max(0.0F, stack_h - top - bottom);
        cc.minHeight = cc.maxHeight = hh;
    }

    c->layout(cc);

    float x = 0;
    if (has_l) {
        x = left;
    } else if (has_r) {
        x = stack_w - right - c->size.width;
    }

    float y = 0;
    if (has_t) {
        y = top;
    } else if (has_b) {
        y = stack_h - bottom - c->size.height;
    }

    c->offset = Offset{x, y};
}

/* ---- RenderLayer ------------------------------------------------------- */

RenderLayer::~RenderLayer() {
    if (barrier_lv_ != nullptr) lv_obj_delete(barrier_lv_);
}

void RenderLayer::performLayout() {
    /* Zero size in the flow: a popup must not push its row around. */
    size = Size{0, 0};

    RenderObject *c = firstChild();
    if (c == nullptr) return;

    /* Give the child the whole screen to fit in. The root is the RenderView, so
     * walking up is enough -- no need to hand the screen size down. */
    const RenderObject *root = this;
    while (root->parent != nullptr) root = root->parent;

    /* Our own minWidth is passed on, so a parent that stretches us can say "the
     * popup must be at least as wide as the control it hangs from" -- which is
     * what a dropdown wants. Everything else is bounded only by the screen. */
    c->layout(BoxConstraints{constraints.minWidth, root->size.width, 0, root->size.height});
    c->offset = Offset{0, 0};
}

void RenderLayer::paint(PaintContext &ctx, Offset origin) {
    if (!ctx.overlay_pass) {
        /* Come back to this once everything else has been painted. */
        ctx.deferred.emplace_back(this, origin);
        return;
    }

    if (barrier && on_barrier_tap) {
        if (barrier_lv_ == nullptr) {
            barrier_lv_ = lv_obj_create(ctx.root);
            makeInert(barrier_lv_);
            lv_obj_set_style_bg_opa(barrier_lv_, LV_OPA_TRANSP, 0);
            lv_obj_add_flag(barrier_lv_, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(barrier_lv_, barrierCb, LV_EVENT_CLICKED, this);
        }
        /* The Layer sits wherever the dropdown is, but the barrier has to cover
         * the whole screen, so it is placed back at the origin. */
        const RenderObject *root = this;
        while (root->parent != nullptr) root = root->parent;

        lv_obj_set_pos(barrier_lv_, px(-origin.dx), px(-origin.dy));
        lv_obj_set_size(barrier_lv_, px(root->size.width), px(root->size.height));
        lv_obj_move_to_index(barrier_lv_, ctx.next_index);
        ctx.next_index++;
    } else if (barrier_lv_ != nullptr) {
        lv_obj_delete(barrier_lv_);
        barrier_lv_ = nullptr;
    }

    paintChildren(ctx, origin);
}

void RenderLayer::barrierCb(lv_event_t *e) {
    auto *self = static_cast<RenderLayer *>(lv_event_get_user_data(e));
    if (self->on_barrier_tap) self->on_barrier_tap();
}

/* ---- RenderView -------------------------------------------------------- */

void RenderView::performLayout() {
    size = screen;
    RenderObject *c = firstChild();
    if (c == nullptr) return;
    c->layout(BoxConstraints::tight(screen));
    c->offset = Offset{0, 0};
}

}  // namespace fmsui
