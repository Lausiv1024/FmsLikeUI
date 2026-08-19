#include "fmsui/element.h"

#include <cassert>
#include <cstdarg>
#include <cstdio>

namespace fmsui {

/* ---- Widget ------------------------------------------------------------ */

Widget::Widget() {
    /* Register for destruction with the arena that operator new took this from.
     * Doing it here rather than in operator new means the vtable is already in
     * place by the time reset() calls the virtual destructor. */
    Arena *a = Arena::current();
    assert(a != nullptr && "widgets may only be constructed during a build pass");
    assert(a->owns(this) && "widgets must be allocated with new, from the build arena");
    a->track(this);
}

void *Widget::operator new(size_t bytes) {
    Arena *a = Arena::current();
    assert(a != nullptr && "widgets may only be allocated during a build pass");
    return a->allocate(bytes, alignof(max_align_t));
}

Element *StatelessWidget::createElement() const { return new StatelessElement(this); }
Element *InheritedWidget::createElement() const { return new InheritedElement(this); }
Element *StatefulWidget::createElement() const { return new StatefulElement(this); }
Element *LeafRenderObjectWidget::createElement() const { return new LeafRenderObjectElement(this); }
Element *SingleChildRenderObjectWidget::createElement() const {
    return new SingleChildRenderObjectElement(this);
}
Element *MultiChildRenderObjectWidget::createElement() const {
    return new MultiChildRenderObjectElement(this);
}

WidgetList::WidgetList(std::initializer_list<Widget *> items) : size_(items.size()) {
    if (size_ == 0) return;
    Arena *a = Arena::current();
    assert(a != nullptr && "widget lists may only be built during a build pass");
    data_ = static_cast<Widget **>(a->allocate(sizeof(Widget *) * size_, alignof(Widget *)));
    size_t i = 0;
    for (Widget *w : items) data_[i++] = w;
}

WidgetList::WidgetList(Widget *const *items, size_t count) : size_(count) {
    if (size_ == 0) return;
    Arena *a = Arena::current();
    assert(a != nullptr && "widget lists may only be built during a build pass");
    data_ = static_cast<Widget **>(a->allocate(sizeof(Widget *) * size_, alignof(Widget *)));
    for (size_t i = 0; i < size_; i++) data_[i] = items[i];
}

/* ---- Str --------------------------------------------------------------- */

void Str::copyFrom(const char *s, size_t n) {
    Arena *a = Arena::current();
    assert(a != nullptr && "strings may only be built during a build pass");
    char *buf = static_cast<char *>(a->allocate(n + 1, 1));
    for (size_t i = 0; i < n; i++) buf[i] = s[i];
    buf[n] = '\0';
    data_ = buf;
    size_ = n;
}

Str::Str(const char *s) {
    if (s == nullptr) return;
    size_t n = 0;
    while (s[n] != '\0') n++;
    copyFrom(s, n);
}

Str::Str(std::string_view s) { copyFrom(s.data(), s.size()); }
Str::Str(const std::string &s) { copyFrom(s.data(), s.size()); }

Str fmt(const char *format, ...) {
    char stack_buf[128];

    va_list args;
    va_start(args, format);
    const int n = vsnprintf(stack_buf, sizeof(stack_buf), format, args);
    va_end(args);

    if (n < 0) return Str();
    if (static_cast<size_t>(n) < sizeof(stack_buf)) {
        return Str(std::string_view(stack_buf, static_cast<size_t>(n)));
    }

    /* Rare: format straight into the arena when it does not fit the stack. */
    Arena *a = Arena::current();
    assert(a != nullptr);
    char *buf = static_cast<char *>(a->allocate(static_cast<size_t>(n) + 1, 1));
    va_start(args, format);
    vsnprintf(buf, static_cast<size_t>(n) + 1, format, args);
    va_end(args);
    return Str(std::string_view(buf, static_cast<size_t>(n)));
}

/* ---- StateBase --------------------------------------------------------- */

void StateBase::markNeedsBuild() {
    if (element_ != nullptr) element_->markNeedsBuild();
}

/* ---- Element ----------------------------------------------------------- */

Element::Element(const Widget *w) : widget_(w), type_(w->type()), key_(w->key) {}
Element::~Element() = default;

void Element::mount(Element *parent, BuildOwner *owner) {
    parent_ = parent;
    owner_ = owner;
}

void Element::update(const Widget *w) { widget_ = w; }

void Element::unmount() {
    parent_ = nullptr;
    owner_ = nullptr;
}

void Element::markNeedsBuild() {
    if (owner_ != nullptr) owner_->scheduleBuild();
}

Element *Element::updateChild(Element *child, Widget *next) {
    if (next == nullptr) {
        if (child != nullptr) {
            child->unmount();
            delete child;
        }
        return nullptr;
    }

    if (child != nullptr) {
        if (Widget::canUpdate(child->type_, child->key_, next)) {
            child->update(next);
            return child;
        }
        /* Different type or key: the old element (and its State) is gone. */
        child->unmount();
        delete child;
    }

    Element *fresh = next->createElement();
    fresh->mount(this, owner_);
    return fresh;
}

void Element::updateChildren(std::vector<Element *> &children, const WidgetList &next,
                            std::vector<Element *> &scratch, std::vector<KeyedSlot> &pool) {
    /* Flutter's list reconcile, scoped to this one parent.
     *
     * Slot-for-slot matching is right until a list is reordered or something is
     * inserted anywhere but the end -- then every slot after the change sees a
     * different key, and a whole run of Elements (with their State, their
     * RenderObjects and their lv_objs) is destroyed and rebuilt for having
     * moved. So: match what we can from both ends, and let the keyed children
     * left in the middle be adopted at their new positions.
     *
     * A keyed child can only be adopted by the parent it was already under.
     * Moving between parents is what Flutter needs a GlobalKey and a
     * tree-wide inactive list for; nothing here needs it.
     */
    const size_t old_n = children.size();
    const size_t new_n = next.size();

    /* An empty slot only matches an absent widget; otherwise the usual rule. */
    const auto matches = [](const Element *e, Widget *w) {
        if (e == nullptr) return w == nullptr;
        return Widget::canUpdate(e->type_, e->key_, w);
    };

    /* 1. Match from the front, updating in place. */
    size_t head = 0;
    while (head < old_n && head < new_n && matches(children[head], next[head])) {
        children[head] = updateChild(children[head], next[head]);
        head++;
    }

    /* 2. Match from the back, counting only: where the tail lands depends on
     *    what the middle turns out to be. */
    size_t old_tail = old_n;
    size_t new_tail = new_n;
    while (old_tail > head && new_tail > head &&
           matches(children[old_tail - 1], next[new_tail - 1])) {
        old_tail--;
        new_tail--;
    }

    /* Nothing in the middle means nothing moved and nothing was inserted, which
     * is every list on a page whose values changed but whose shape did not.
     * Update the tail where it sits and leave the working buffers alone -- this
     * is the path that has to stay as cheap as the positional reconcile it
     * replaced. */
    if (head == old_tail && head == new_tail) {
        for (size_t i = old_tail; i < old_n; i++) {
            children[i] = updateChild(children[i], next[new_tail + (i - old_tail)]);
        }
        return;
    }

    /* 3. Put the old middle's keyed children up for adoption. The unkeyed ones
     *    cannot be identified across a move, so they go now. */
    pool.clear();
    for (size_t i = head; i < old_tail; i++) {
        Element *e = children[i];
        if (e == nullptr) continue;
        if (e->key_.isNone()) {
            e->unmount();
            delete e;
            continue;
        }
#ifndef NDEBUG
        for (const KeyedSlot &s : pool) {
            assert(s.key != e->key_.value && "two children of one parent share a key");
        }
#endif
        pool.push_back(KeyedSlot{e->key_.value, e});
    }

    /* 4. Build the new list: head as-is, then the middle, adopting where a key
     *    matches. */
    scratch.clear();
    scratch.reserve(new_n);
    for (size_t i = 0; i < head; i++) scratch.push_back(children[i]);

    for (size_t i = head; i < new_tail; i++) {
        Widget *w = next[i];
        Element *adopted = nullptr;
        if (w != nullptr && !w->key.isNone()) {
            for (KeyedSlot &s : pool) {
                if (s.element == nullptr || s.key != w->key.value) continue;
                /* Same key, different type: not the same thing after all. Leave
                 * it in the pool to be destroyed below. */
                if (Widget::canUpdate(s.element->type_, s.element->key_, w)) {
                    adopted = s.element;
                    s.element = nullptr;
                }
                break;
            }
        }
        scratch.push_back(updateChild(adopted, w));
    }

    /* 5. Whatever no one took is gone. */
    for (const KeyedSlot &s : pool) {
        if (s.element == nullptr) continue;
        s.element->unmount();
        delete s.element;
    }
    pool.clear();

    /* 6. Now the tail, whose new positions are finally known. */
    for (size_t i = old_tail; i < old_n; i++) {
        scratch.push_back(updateChild(children[i], next[new_tail + (i - old_tail)]));
    }

    /* Swapping rather than assigning keeps both buffers' capacity for next
     * frame, which is what stops this from calling malloc every time. */
    children.swap(scratch);
    scratch.clear();
}

/* ---- ComponentElement -------------------------------------------------- */

void ComponentElement::mount(Element *parent, BuildOwner *owner) {
    Element::mount(parent, owner);
    rebuild();
}

void ComponentElement::update(const Widget *w) {
    Element::update(w);
    rebuild();
}

void ComponentElement::unmount() {
    if (child_ != nullptr) {
        child_->unmount();
        delete child_;
        child_ = nullptr;
    }
    Element::unmount();
}

void ComponentElement::rebuild() { child_ = updateChild(child_, build()); }

Widget *StatelessElement::build() {
    return static_cast<const StatelessWidget *>(widget_)->build(*this);
}

Widget *InheritedElement::build() {
    return static_cast<const InheritedWidget *>(widget_)->child;
}

const Widget *Element::findAncestorInherited(WidgetType type) const {
    for (const Element *e = this; e != nullptr; e = e->parent_) {
        if (e->type_ == type) return e->widget_;
    }
    return nullptr;
}

/* ---- StatefulElement --------------------------------------------------- */

StatefulElement::StatefulElement(const StatefulWidget *w)
    : ComponentElement(w), state_(w->createState()) {}

StatefulElement::~StatefulElement() {
    delete state_;
    state_ = nullptr;
}

void StatefulElement::mount(Element *parent, BuildOwner *owner) {
    state_->element_ = this;
    state_->context_ = this;
    state_->widget_ = widget_;
    state_->initState();
    ComponentElement::mount(parent, owner);
}

void StatefulElement::update(const Widget *w) {
    Element::update(w);
    state_->widget_ = w;
    rebuild();
}

void StatefulElement::unmount() {
    state_->dispose();
    ComponentElement::unmount();
}

Widget *StatefulElement::build() { return state_->build(*this); }

/* ---- RenderObjectElement ----------------------------------------------- */

void RenderObjectElement::mount(Element *parent, BuildOwner *owner) {
    Element::mount(parent, owner);

    const auto *w = static_cast<const RenderObjectWidget *>(widget_);
    ro_ = w->createRenderObject();
    w->updateRenderObject(ro_);
    syncChildren();
}

void RenderObjectElement::update(const Widget *w) {
    Element::update(w);
    static_cast<const RenderObjectWidget *>(w)->updateRenderObject(ro_);
    syncChildren();
}

void RenderObjectElement::unmount() {
    unmountChildren();
    delete ro_;  // deletes the lv_obj too, if it had one
    ro_ = nullptr;
    Element::unmount();
}

void SingleChildRenderObjectElement::syncChildren() {
    const auto *w = static_cast<const SingleChildRenderObjectWidget *>(widget_);
    child_ = updateChild(child_, w->child);

    kids_.clear();
    if (child_ != nullptr) {
        if (RenderObject *r = child_->renderObject()) kids_.push_back(r);
    }
    ro_->setChildren(kids_);
}

void SingleChildRenderObjectElement::unmountChildren() {
    if (child_ != nullptr) {
        child_->unmount();
        delete child_;
        child_ = nullptr;
    }
}

void MultiChildRenderObjectElement::syncChildren() {
    const auto *w = static_cast<const MultiChildRenderObjectWidget *>(widget_);

    /* Unkeyed children reconcile slot for slot; keyed ones may move. */
    updateChildren(children_, w->children, scratch_, pool_);

    kids_.clear();
    kids_.reserve(children_.size());
    for (Element *e : children_) {
        if (e == nullptr) continue;
        if (RenderObject *r = e->renderObject()) kids_.push_back(r);
    }
    ro_->setChildren(kids_);
}

void MultiChildRenderObjectElement::unmountChildren() {
    for (Element *e : children_) {
        if (e == nullptr) continue;
        e->unmount();
        delete e;
    }
    children_.clear();
}

}  // namespace fmsui
