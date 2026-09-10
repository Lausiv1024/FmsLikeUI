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

#include <atomic>
#include <cstdint>
#include <vector>

#include "fmsui/render.h"
#include "fmsui/widget.h"

namespace fmsui {

/* Whoever is running right now, as a number we only ever compare.
 *
 * `std::this_thread::get_id()` is what this used to be, and it cannot be used
 * here: on ESP-IDF it goes through pthread_self(), which asserts outright when
 * it is called from a FreeRTOS task that was not created as a pthread -- and the
 * task esp_lvgl_port creates to run the frame loop is exactly that. The board
 * rebooted on the first frame.
 *
 * So the platform supplies the identity, the way it already supplies the
 * microsecond clock. Install one before the first frame:
 *
 *     device:   (ThreadId)xTaskGetCurrentTaskHandle()
 *     host/sim: std::hash<std::thread::id>{}(std::this_thread::get_id())
 *
 * With none installed the check is simply off. */
using ThreadId = uintptr_t;
using ThreadIdFn = ThreadId (*)();

/* Owns the "something changed" flag, and knows which thread is allowed to touch
 * the tree.
 *
 * The flag is the one thing in the framework that any task may poke. Everything
 * else -- the element tree, the render objects, the build arena, LVGL itself --
 * belongs to the thread that runs the frame loop, and there is no lock anywhere
 * that would make it otherwise.
 */
class BuildOwner {
public:
    /* Ask for a rebuild. Callable from any task: one release store, no
     * allocation, no locking, nothing that can block.
     *
     * Call it *after* publishing whatever changed. If takeNeedsBuild() observes
     * this store, its acquire makes the preceding writes visible to that build.
     * The flag is coalesced, though: one acquire does not synchronize with every
     * producer whose store wrote true. Multiple producers must therefore give
     * each shared payload its own synchronization (atomic, lock, queue, etc.). */
    void scheduleBuild() { needs_build_.store(true, std::memory_order_release); }

    /* Claim the request, if there is one. A store that lands after this returns
     * is not lost: the flag stays set and the next frame picks it up. */
    bool takeNeedsBuild() { return needs_build_.exchange(false, std::memory_order_acquire); }

    bool needsBuild() const { return needs_build_.load(std::memory_order_acquire); }

    /* For callers that cannot afford a call into flash -- see FmsApp::requester(). */
    std::atomic<bool> *dirtyFlag() { return &needs_build_; }

    /* How to ask who is running. Null -- the default -- turns the check off
     * rather than guessing, because a wrong answer here would abort a running
     * aircraft display over a diagnostic. FmsApp::setThreadId() is the way in. */
    static void setThreadIdFn(ThreadIdFn fn) { thread_id_fn_ = fn; }

    /* Remember the thread the frame loop runs on, so that mutating the tree from
     * anywhere else can be caught instead of silently corrupting the arena.
     * FmsApp does this on its first frame; the unit tests drive the tree
     * directly and never bind, which leaves the check inert. */
    void bindToCurrentThread() {
        if (thread_id_fn_ == nullptr) return;  // no identity available: no check
        ui_thread_ = thread_id_fn_();
        bound_ = true;
    }
    bool bound() const { return bound_; }
    bool onBuildThread() const { return !bound_ || ui_thread_ == thread_id_fn_(); }

private:
    static ThreadIdFn thread_id_fn_;

    std::atomic<bool> needs_build_{true};
    ThreadId ui_thread_{};
    bool bound_ = false;
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
