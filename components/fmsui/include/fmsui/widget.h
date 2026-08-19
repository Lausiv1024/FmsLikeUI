#pragma once

/* The Widget layer: an immutable description of what the UI should look like,
 * rebuilt from scratch on every frame that anything changed and then discarded.
 *
 * Widgets are always allocated from the build arena -- `operator new` is
 * overridden to see to that -- so `new Column{{...}}` inside build() is a
 * pointer bump, not a heap allocation.
 */

#include <cstddef>
#include <functional>
#include <initializer_list>

#include "fmsui/arena.h"
#include "fmsui/foundation.h"
#include "fmsui/str.h"

namespace fmsui {

class Element;
class RenderObject;
class StateBase;

/* Flutter's BuildContext is the Element itself; keep that, so `Theme::of(ctx)`
 * can walk the element tree in M2. */
using BuildContext = Element;

/* A runtime type identity that does not need RTTI (which ESP-IDF disables by
 * default): the address of a per-class static byte. */
using WidgetType = const void *;

namespace detail {
template <class T>
struct TypeTag {
    static const char id;
};
template <class T>
const char TypeTag<T>::id = 0;
}  // namespace detail

template <class T>
constexpr WidgetType widgetTypeOf() {
    return &detail::TypeTag<T>::id;
}

class Widget : public ArenaObject {
public:
    Widget();
    ~Widget() override = default;

    Key key{};

    virtual WidgetType type() const = 0;
    virtual Element *createElement() const = 0;

    /* Two widgets reconcile onto the same Element only if both the type and the
     * key match; otherwise the old Element (and its State) is thrown away. */
    static bool canUpdate(WidgetType old_type, Key old_key, const Widget *next) {
        return next != nullptr && old_type == next->type() && old_key == next->key;
    }

    static void *operator new(size_t bytes);
    static void operator delete(void *, size_t) noexcept {}  // the arena frees in bulk
    static void *operator new[](size_t) = delete;
    static void operator delete[](void *) = delete;
};

/* Every concrete widget puts this in its public section. */
#define FMSUI_WIDGET(Class)                                                  \
    ::fmsui::WidgetType type() const override {                              \
        return ::fmsui::widgetTypeOf<Class>();                               \
    }

/* An arena-backed list of children, so `.children = {a, b, c}` does not
 * heap-allocate a vector per Row. */
class WidgetList {
public:
    WidgetList() = default;
    WidgetList(std::initializer_list<Widget *> items);  // NOLINT(google-explicit-constructor)

    /* For lists whose length is only known at run time -- a tab bar's tabs, a
     * keypad's keys. Copies into the arena, like the initializer_list form: an
     * earlier version took the pointer without copying, and the first caller to
     * hand it a stack array got a use-after-free. An API that needs a comment to
     * be used safely is the wrong API. */
    WidgetList(Widget *const *items, size_t count);

    Widget *const *begin() const { return data_; }
    Widget *const *end() const { return data_ + size_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    Widget *operator[](size_t i) const { return data_[i]; }

private:
    Widget **data_ = nullptr;
    size_t size_ = 0;
};

using VoidCallback = std::function<void()>;

/* ---- Composition ------------------------------------------------------- */

class StatelessWidget : public Widget {
public:
    Element *createElement() const override;
    virtual Widget *build(BuildContext &ctx) const = 0;
};

class StatefulWidget : public Widget {
public:
    Element *createElement() const override;

    /* The State outlives the Widget: it is heap-allocated once, kept by the
     * Element, and handed a fresh Widget on every rebuild. */
    virtual StateBase *createState() const = 0;
};

class StatefulElement;

class StateBase {
public:
    virtual ~StateBase() = default;

    virtual void initState() {}
    virtual void dispose() {}
    virtual Widget *build(BuildContext &ctx) = 0;

    /* Mutate, then ask for a rebuild.  Doing it in one call is what stops the
     * two from drifting apart. */
    template <class Fn>
    void setState(Fn &&mutate) {
        mutate();
        markNeedsBuild();
    }

    void markNeedsBuild();

protected:
    const Widget *rawWidget() const { return widget_; }
    BuildContext *context() const { return context_; }

private:
    friend class StatefulElement;
    StatefulElement *element_ = nullptr;
    const Widget *widget_ = nullptr;
    BuildContext *context_ = nullptr;
};

/* State<MyWidget> gives typed access to the current widget. */
template <class W>
class State : public StateBase {
protected:
    const W &widget() const { return *static_cast<const W *>(rawWidget()); }
};

/* ---- Inherited ---------------------------------------------------------- */

/* Data handed down the tree, read with `Something::of(ctx)`.
 *
 * Much simpler than Flutter's, and the full-tree rebuild is why: Flutter has to
 * track which Elements read which InheritedWidget so it can rebuild exactly
 * those when the value changes.  We rebuild everything on any change, so there
 * is nothing to track and nothing to notify -- `of()` is just a walk up the
 * element tree.
 */
class InheritedWidget : public Widget {
public:
    Widget *child = nullptr;
    Element *createElement() const override;
};

/* ---- Render object widgets --------------------------------------------- */

class RenderObjectWidget : public Widget {
public:
    virtual RenderObject *createRenderObject() const = 0;
    virtual void updateRenderObject(RenderObject *ro) const = 0;
};

class LeafRenderObjectWidget : public RenderObjectWidget {
public:
    Element *createElement() const override;
};

class SingleChildRenderObjectWidget : public RenderObjectWidget {
public:
    Widget *child = nullptr;
    Element *createElement() const override;
};

class MultiChildRenderObjectWidget : public RenderObjectWidget {
public:
    WidgetList children;
    Element *createElement() const override;
};

}  // namespace fmsui
