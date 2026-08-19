#pragma once

/* The Element layer: the persistent tree that survives rebuilds.
 *
 * Widgets are thrown away every frame; Elements are not.  An Element keeps its
 * State and its RenderObject alive as long as the widget it reconciles against
 * keeps the same type and key, which is what makes `setState` preserve a text
 * cursor, a scroll position or an animation instead of resetting it.  Within one
 * parent that match is made by key where a child has one, so reordering a list
 * moves its Elements rather than rebuilding them -- see updateChildren().
 *
 * Rebuild granularity: when anything is dirty we rebuild the *whole* tree, not
 * just the dirty subtree.  That is a deliberate trade against Flutter, and it is
 * what lets Widgets live in a bump arena that gets reset wholesale -- a
 * dirty-subtree rebuild would leave clean Elements pointing at widgets in an
 * arena we are about to reuse.  A full rebuild of a few hundred widgets is a few
 * hundred pointer bumps and virtual calls; the expensive parts (layout diffing
 * against LVGL, and LVGL's own dirty-area redraw) stay incremental because
 * RenderObjects only touch LVGL for values that actually changed.
 */

#include <vector>

#include "fmsui/render.h"
#include "fmsui/widget.h"

namespace fmsui {

class BuildOwner {
public:
    void scheduleBuild() { needs_build_ = true; }
    bool needsBuild() const { return needs_build_; }
    void clearNeedsBuild() { needs_build_ = false; }

private:
    bool needs_build_ = true;
};

class Element {
public:
    explicit Element(const Widget *w);
    virtual ~Element();

    Element(const Element &) = delete;
    Element &operator=(const Element &) = delete;

    virtual void mount(Element *parent, BuildOwner *owner);
    virtual void update(const Widget *w);
    virtual void unmount();

    /* The topmost RenderObject in this element's subtree, or null if it has
     * none yet.  A ComponentElement forwards to its child. */
    virtual RenderObject *renderObject() const = 0;

    const Widget *widget() const { return widget_; }
    Element *parent() const { return parent_; }
    BuildOwner *owner() const { return owner_; }

    void markNeedsBuild();

    /* Nearest ancestor InheritedWidget of the given type, or null.  This is what
     * `FmsTheme::of(ctx)` is built on. */
    const Widget *findAncestorInherited(WidgetType type) const;

protected:
    /* Reconcile one slot: update the existing element if the new widget matches
     * its type and key, otherwise throw it away and inflate a new one. */
    Element *updateChild(Element *child, Widget *next);

    /* An old keyed child, offered up for adoption at a different position. */
    struct KeyedSlot {
        int32_t key;
        Element *element;  /* null once someone has taken it */
    };

    /* Reconcile a whole list of slots, letting a keyed child move rather than be
     * rebuilt.  See the comment on the definition for the algorithm.
     *
     * `scratch` and `pool` are working buffers the caller owns and reuses across
     * frames: reconciliation is re-entrant (updating a child runs the child's own
     * reconcile before we are done here), so they cannot be shared globally, and
     * allocating them per frame would show up in the build time. */
    void updateChildren(std::vector<Element *> &children, const WidgetList &next,
                        std::vector<Element *> &scratch, std::vector<KeyedSlot> &pool);

    const Widget *widget_ = nullptr;
    WidgetType type_ = nullptr;
    Key key_{};
    Element *parent_ = nullptr;
    BuildOwner *owner_ = nullptr;
};

/* ---- Composition ------------------------------------------------------- */

class ComponentElement : public Element {
public:
    using Element::Element;

    RenderObject *renderObject() const override {
        return child_ != nullptr ? child_->renderObject() : nullptr;
    }

    void mount(Element *parent, BuildOwner *owner) override;
    void update(const Widget *w) override;
    void unmount() override;

protected:
    virtual Widget *build() = 0;
    void rebuild();

    Element *child_ = nullptr;
};

class StatelessElement : public ComponentElement {
public:
    using ComponentElement::ComponentElement;

protected:
    Widget *build() override;
};

/* Holds an InheritedWidget in the tree so descendants can find it by type. */
class InheritedElement : public ComponentElement {
public:
    using ComponentElement::ComponentElement;

protected:
    Widget *build() override;
};

class StatefulElement : public ComponentElement {
public:
    explicit StatefulElement(const StatefulWidget *w);
    ~StatefulElement() override;

    void mount(Element *parent, BuildOwner *owner) override;
    void update(const Widget *w) override;
    void unmount() override;

protected:
    Widget *build() override;

private:
    StateBase *state_ = nullptr;
};

/* ---- Render objects ---------------------------------------------------- */

class RenderObjectElement : public Element {
public:
    using Element::Element;

    RenderObject *renderObject() const override { return ro_; }

    void mount(Element *parent, BuildOwner *owner) override;
    void update(const Widget *w) override;
    void unmount() override;

protected:
    virtual void syncChildren() {}
    virtual void unmountChildren() {}

    RenderObject *ro_ = nullptr;
};

class LeafRenderObjectElement : public RenderObjectElement {
public:
    using RenderObjectElement::RenderObjectElement;
};

class SingleChildRenderObjectElement : public RenderObjectElement {
public:
    using RenderObjectElement::RenderObjectElement;

protected:
    void syncChildren() override;
    void unmountChildren() override;

private:
    Element *child_ = nullptr;
    /* Reused across rebuilds so the reconciler does not malloc a vector per
     * object per frame. */
    std::vector<RenderObject *> kids_;
};

class MultiChildRenderObjectElement : public RenderObjectElement {
public:
    using RenderObjectElement::RenderObjectElement;

protected:
    void syncChildren() override;
    void unmountChildren() override;

private:
    std::vector<Element *> children_;

    /* Reused across rebuilds, like kids_. Both stay empty -- and therefore cost
     * nothing but the vector header -- unless this list actually changes shape:
     * a list that only had its values change never reaches the code that fills
     * them. */
    std::vector<Element *> scratch_;
    std::vector<KeyedSlot> pool_;

    std::vector<RenderObject *> kids_;  /* reused, see above */
};

}  // namespace fmsui
