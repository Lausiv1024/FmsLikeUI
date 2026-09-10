/* Unit tests for the layout maths and the reconciler.
 *
 * These run on the host with no display and no LVGL objects, which is the point:
 * flex distribution and element identity are where the subtle bugs live, and
 * they should be caught by `ninja test` in a second, not by squinting at a
 * screenshot on a 5-inch panel.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "lvgl.h"

#include "fmsui/fmsui.h"

using namespace fmsui;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char *expr, const char *file, int line) {
    g_checks++;
    if (ok) return;
    g_failures++;
    std::printf("  FAIL %s:%d: %s\n", file, line, expr);
}

void checkNear(float got, float want, const char *what, const char *file, int line) {
    g_checks++;
    if (std::abs(got - want) < 0.01F) return;
    g_failures++;
    std::printf("  FAIL %s:%d: %s = %.2f, want %.2f\n", file, line, what, got, want);
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(got, want) checkNear((got), (want), #got, __FILE__, __LINE__)

/* A render object that wants a fixed size but obeys its constraints -- stands in
 * for whatever real leaf would be there. */
class FixedBox : public RenderObject {
public:
    Size desired{0, 0};
    void performLayout() override { size = constraints.constrain(desired); }
};

FixedBox *box(float w, float h) {
    auto *b = new FixedBox();
    b->desired = Size{w, h};
    return b;
}

/* Owns the render objects a test builds, so the test itself stays readable. */
class Scope {
public:
    ~Scope() {
        for (RenderObject *r : owned_) delete r;
    }
    template <class T>
    T *keep(T *r) {
        owned_.push_back(r);
        return r;
    }

private:
    std::vector<RenderObject *> owned_;
};

/* ---- Layout ------------------------------------------------------------ */

void test_flex_distributes_free_space() {
    std::printf("flex: 2:1:1 of the free space\n");
    Scope s;

    auto *a = s.keep(box(0, 50));
    auto *b = s.keep(box(0, 50));
    auto *c = s.keep(box(0, 50));
    a->flex = 2;
    a->fit = FlexFit::Tight;
    b->flex = 1;
    b->fit = FlexFit::Tight;
    c->flex = 1;
    c->fit = FlexFit::Tight;

    auto *row = s.keep(new RenderFlex());
    row->direction = Axis::Horizontal;
    row->setChildren({a, b, c});
    row->layout(BoxConstraints::tight(Size{400, 100}));

    CHECK_EQ(a->size.width, 200);
    CHECK_EQ(b->size.width, 100);
    CHECK_EQ(c->size.width, 100);
    CHECK_EQ(a->offset.dx, 0);
    CHECK_EQ(b->offset.dx, 200);
    CHECK_EQ(c->offset.dx, 300);
}

void test_flex_leaves_room_for_inflexible_children() {
    std::printf("flex: fixed children are subtracted before the split\n");
    Scope s;

    auto *fixed = s.keep(box(100, 50));
    auto *grow = s.keep(box(0, 50));
    grow->flex = 1;
    grow->fit = FlexFit::Tight;

    auto *row = s.keep(new RenderFlex());
    row->direction = Axis::Horizontal;
    row->setChildren({fixed, grow});
    row->layout(BoxConstraints::tight(Size{400, 100}));

    CHECK_EQ(fixed->size.width, 100);
    CHECK_EQ(grow->size.width, 300);
    CHECK_EQ(grow->offset.dx, 100);
}

void test_flex_spacing_is_taken_from_the_free_space() {
    std::printf("flex: spacing eats into what the flex children share\n");
    Scope s;

    auto *a = s.keep(box(0, 50));
    auto *b = s.keep(box(0, 50));
    a->flex = 1;
    a->fit = FlexFit::Tight;
    b->flex = 1;
    b->fit = FlexFit::Tight;

    auto *row = s.keep(new RenderFlex());
    row->direction = Axis::Horizontal;
    row->spacing = 20;
    row->setChildren({a, b});
    row->layout(BoxConstraints::tight(Size{420, 100}));

    CHECK_EQ(a->size.width, 200);
    CHECK_EQ(b->size.width, 200);
    CHECK_EQ(b->offset.dx, 220);
}

void test_main_axis_alignment() {
    std::printf("flex: main axis alignment\n");

    struct Case {
        MainAxis align;
        float want_first;
        float want_second;
    };
    const Case cases[] = {
        {MainAxis::Start, 0, 100},
        {MainAxis::End, 200, 300},
        {MainAxis::Center, 100, 200},
        {MainAxis::SpaceBetween, 0, 300},
        {MainAxis::SpaceAround, 50, 250},
        {MainAxis::SpaceEvenly, 66.67F, 233.33F},
    };

    for (const Case &tc : cases) {
        Scope s;
        auto *a = s.keep(box(100, 50));
        auto *b = s.keep(box(100, 50));
        auto *row = s.keep(new RenderFlex());
        row->direction = Axis::Horizontal;
        row->main_axis = tc.align;
        row->setChildren({a, b});
        row->layout(BoxConstraints::tight(Size{400, 100}));

        CHECK_EQ(a->offset.dx, tc.want_first);
        CHECK_EQ(b->offset.dx, tc.want_second);
    }
}

void test_cross_axis_stretch_makes_the_cross_constraint_tight() {
    std::printf("flex: CrossAxis::Stretch\n");
    Scope s;

    auto *a = s.keep(box(100, 10));  // wants to be 10 tall
    auto *col = s.keep(new RenderFlex());
    col->direction = Axis::Vertical;
    col->cross_axis = CrossAxis::Stretch;
    col->setChildren({a});
    col->layout(BoxConstraints::tight(Size{400, 200}));

    CHECK_EQ(a->size.width, 400);  // stretched across
    CHECK_EQ(a->size.height, 10);  // natural along the main axis
}

void test_stretch_shrink_wraps_under_a_loose_cross_constraint() {
    std::printf("flex: Stretch shrink-wraps to the widest child when the cross axis is loose\n");
    Scope s;

    auto *narrow = s.keep(box(40, 20));
    auto *wide = s.keep(box(90, 20));

    auto *col = s.keep(new RenderFlex());
    col->direction = Axis::Vertical;
    col->cross_axis = CrossAxis::Stretch;
    col->main_size = MainAxisSize::Min;
    col->setChildren({narrow, wide});

    /* Loose across: an upper bound of 500, no lower bound. This is what a popup
     * anchored on the screen gets. Filling 500 would be Flutter's answer; ours is
     * to take the width of the widest child. */
    col->layout(BoxConstraints{0, 500, 0, 500});

    CHECK_EQ(col->size.width, 90);
    CHECK_EQ(narrow->size.width, 90);  // stretched to match
    CHECK_EQ(wide->size.width, 90);
}

void test_stretch_fills_a_tight_cross_constraint() {
    std::printf("flex: Stretch still fills when the cross axis is tight\n");
    Scope s;

    auto *a = s.keep(box(40, 20));
    auto *col = s.keep(new RenderFlex());
    col->direction = Axis::Vertical;
    col->cross_axis = CrossAxis::Stretch;
    col->setChildren({a});
    col->layout(BoxConstraints::tight(Size{300, 200}));

    CHECK_EQ(col->size.width, 300);
    CHECK_EQ(a->size.width, 300);
}

void test_main_axis_size_min_shrink_wraps() {
    std::printf("flex: MainAxisSize::Min\n");
    Scope s;

    auto *a = s.keep(box(100, 50));
    auto *b = s.keep(box(60, 50));
    auto *row = s.keep(new RenderFlex());
    row->direction = Axis::Horizontal;
    row->main_size = MainAxisSize::Min;
    row->setChildren({a, b});
    row->layout(BoxConstraints{0, 400, 0, 100});

    CHECK_EQ(row->size.width, 160);
}

void test_padding_deflates_then_reinflates() {
    std::printf("padding\n");
    Scope s;

    auto *child = s.keep(box(1000, 1000));  // will be clamped by the constraint
    auto *pad = s.keep(new RenderPadding());
    pad->padding = EdgeInsets::only(10, 20, 30, 40);
    pad->setChildren({child});
    pad->layout(BoxConstraints::tight(Size{200, 200}));

    CHECK_EQ(child->size.width, 160);   // 200 - 10 - 30
    CHECK_EQ(child->size.height, 140);  // 200 - 20 - 40
    CHECK_EQ(child->offset.dx, 10);
    CHECK_EQ(child->offset.dy, 20);
    CHECK_EQ(pad->size.width, 200);
}

void test_align_centres_and_fills() {
    std::printf("align\n");
    Scope s;

    auto *child = s.keep(box(100, 50));
    auto *al = s.keep(new RenderAlign());
    al->alignment = Alignment::center();
    al->setChildren({child});
    al->layout(BoxConstraints::tight(Size{300, 200}));

    CHECK_EQ(al->size.width, 300);      // fills the bounded space
    CHECK_EQ(child->offset.dx, 100);    // (300 - 100) / 2
    CHECK_EQ(child->offset.dy, 75);     // (200 - 50) / 2
}

void test_align_shrink_wraps_when_unbounded() {
    std::printf("align: unbounded axis shrink-wraps\n");
    Scope s;

    auto *child = s.keep(box(100, 50));
    auto *al = s.keep(new RenderAlign());
    al->setChildren({child});
    al->layout(BoxConstraints{0, kInf, 0, kInf});

    CHECK_EQ(al->size.width, 100);
    CHECK_EQ(al->size.height, 50);
}

void test_constrained_box_is_clamped_by_its_parent() {
    std::printf("constrained box: additional.enforce(constraints)\n");
    Scope s;

    auto *cb = s.keep(new RenderConstrainedBox());
    cb->additional = BoxConstraints::tight(Size{500, 500});  // asks for more than it may have
    cb->layout(BoxConstraints{0, 200, 0, 200});

    CHECK_EQ(cb->size.width, 200);
    CHECK_EQ(cb->size.height, 200);
}

void test_stack_positions_from_edges() {
    std::printf("stack + positioned\n");
    Scope s;

    auto *back = s.keep(box(50, 50));
    auto *front = s.keep(box(30, 20));

    auto *pos = s.keep(new RenderPositioned());
    pos->right = 10;
    pos->bottom = 5;
    pos->setChildren({front});

    auto *stack = s.keep(new RenderStack());
    stack->setChildren({back, pos});
    stack->layout(BoxConstraints::tight(Size{200, 100}));

    /* The unpositioned child sizes the stack only when the stack is loose; here
     * it is tight, so the stack is 200x100 and the positioned child is measured
     * from the right and bottom edges. */
    CHECK_EQ(front->offset.dx, 160);  // 200 - 10 - 30
    CHECK_EQ(front->offset.dy, 75);   // 100 - 5 - 20
}

/* ---- Reconciliation ---------------------------------------------------- */

/* Counts how often it is created, so a test can tell "the State survived the
 * rebuild" apart from "a fresh State happened to hold the same number". */
int g_state_constructions = 0;
int g_state_disposals = 0;

class CounterState;

/* Every live CounterState, by the start value it was built from -- so a test can
 * hold on to a State across a rebuild and check it is the same object. */
std::vector<std::pair<int, CounterState *>> g_live_states;

class Counter : public StatefulWidget {
public:
    explicit Counter(int start, Key k = {}) : start_(start) { key = k; }
    FMSUI_WIDGET(Counter)
    StateBase *createState() const override;
    int start() const { return start_; }

private:
    int start_;
};

class CounterState : public State<Counter> {
public:
    int value = -1;

    void initState() override {
        g_state_constructions++;
        value = widget().start();
        g_live_states.push_back({value, this});
    }
    void dispose() override {
        g_state_disposals++;
        for (size_t i = 0; i < g_live_states.size(); i++) {
            if (g_live_states[i].second != this) continue;
            g_live_states.erase(g_live_states.begin() + static_cast<long>(i));
            break;
        }
    }
    Widget *build(BuildContext &ctx) override {
        (void)ctx;
        return new Text{{.text = fmt("%d", value)}};
    }
};

StateBase *Counter::createState() const { return new CounterState(); }

/* The live State that was built from this start value, or null. */
CounterState *stateFor(int start) {
    for (const auto &e : g_live_states) {
        if (e.first == start) return e.second;
    }
    return nullptr;
}

void resetCounters() {
    g_state_constructions = 0;
    g_state_disposals = 0;
    g_live_states.clear();
}

/* A hand-rolled root so the reconciler can be driven without a display. */
class TestRoot : public SingleChildRenderObjectWidget {
public:
    explicit TestRoot(Widget *c) { child = c; }
    FMSUI_WIDGET(TestRoot)
    RenderObject *createRenderObject() const override { return new RenderView(); }
    void updateRenderObject(RenderObject *) const override {}
};

void test_state_survives_a_rebuild() {
    std::printf("reconcile: State survives when type and key match\n");

    BuildArenas arenas;
    BuildOwner owner;
    g_state_constructions = 0;

    arenas.beginBuild();
    Widget *t1 = new TestRoot(new Counter(7));
    Element *root = t1->createElement();
    root->mount(nullptr, &owner);

    CHECK(g_state_constructions == 1);

    /* Rebuild with an identical tree: the Element, and therefore the State,
     * must be reused rather than recreated. */
    arenas.beginBuild();
    Widget *t2 = new TestRoot(new Counter(7));
    root->update(t2);

    CHECK(g_state_constructions == 1);

    root->unmount();
    delete root;
}

void test_a_different_key_throws_the_state_away() {
    std::printf("reconcile: a changed key replaces the Element\n");

    BuildArenas arenas;
    BuildOwner owner;
    g_state_constructions = 0;

    arenas.beginBuild();
    Element *root = (new TestRoot(new Counter(7, Key{1})))->createElement();
    root->mount(nullptr, &owner);
    CHECK(g_state_constructions == 1);

    arenas.beginBuild();
    root->update(new TestRoot(new Counter(7, Key{2})));
    CHECK(g_state_constructions == 2);  // new key -> new State

    root->unmount();
    delete root;
}

void test_children_can_be_added_and_removed() {
    std::printf("reconcile: a shrinking child list unmounts the extras\n");

    BuildArenas arenas;
    BuildOwner owner;
    g_state_constructions = 0;

    arenas.beginBuild();
    Element *root = (new TestRoot(new Column{{.children = {new Counter(1), new Counter(2),
                                                           new Counter(3)}}}))
                        ->createElement();
    root->mount(nullptr, &owner);
    CHECK(g_state_constructions == 3);

    /* Drop one. The two survivors keep their State; nothing new is built. */
    arenas.beginBuild();
    root->update(new TestRoot(new Column{{.children = {new Counter(1), new Counter(2)}}}));
    CHECK(g_state_constructions == 3);

    /* Grow again: exactly one new State. */
    arenas.beginBuild();
    root->update(new TestRoot(
        new Column{{.children = {new Counter(1), new Counter(2), new Counter(9)}}}));
    CHECK(g_state_constructions == 4);

    root->unmount();
    delete root;
}

/* What each child of the root's Column renders, in render order.  A State that
 * moved shows the mark the test wrote into it; one that was rebuilt shows its
 * start value.  That is the difference the reorder tests turn on. */
bool textsAre(Element *root, std::initializer_list<const char *> want) {
    RenderObject *flex = root->renderObject()->firstChild();
    const std::vector<RenderObject *> &kids = flex->children();
    if (kids.size() != want.size()) return false;
    size_t i = 0;
    for (const char *w : want) {
        if (static_cast<RenderText *>(kids[i++])->text != w) return false;
    }
    return true;
}

void test_keyed_children_survive_a_reorder() {
    std::printf("reconcile: keyed children move instead of being rebuilt\n");

    BuildArenas arenas;
    BuildOwner owner;
    resetCounters();

    arenas.beginBuild();
    Element *root =
        (new TestRoot(new Column{{.children = {new Counter(1, Key{1}), new Counter(2, Key{2}),
                                               new Counter(3, Key{3})}}}))
            ->createElement();
    root->mount(nullptr, &owner);
    CHECK(g_state_constructions == 3);

    /* Mark each State. A rebuilt one would go back to showing its start value. */
    CounterState *s1 = stateFor(1);
    CounterState *s2 = stateFor(2);
    CounterState *s3 = stateFor(3);
    CHECK(s1 != nullptr && s2 != nullptr && s3 != nullptr);
    s1->value = 11;
    s2->value = 22;
    s3->value = 33;

    arenas.beginBuild();
    root->update(new TestRoot(new Column{{.children = {new Counter(3, Key{3}),
                                                       new Counter(1, Key{1}),
                                                       new Counter(2, Key{2})}}}));

    CHECK(g_state_constructions == 3);  // nothing new was built
    CHECK(g_state_disposals == 0);      // nothing was thrown away
    CHECK(stateFor(1) == s1);
    CHECK(stateFor(2) == s2);
    CHECK(stateFor(3) == s3);

    /* The marks moved with the States, and the render objects are in the new
     * order -- so the children really were adopted at new positions. */
    CHECK(textsAre(root, {"33", "11", "22"}));

    root->unmount();
    delete root;
}

void test_keyed_prepend_rebuilds_only_the_new_child() {
    std::printf("reconcile: inserting at the front does not rebuild the rest\n");

    BuildArenas arenas;
    BuildOwner owner;
    resetCounters();

    arenas.beginBuild();
    Element *root =
        (new TestRoot(new Column{{.children = {new Counter(1, Key{1}), new Counter(2, Key{2}),
                                               new Counter(3, Key{3})}}}))
            ->createElement();
    root->mount(nullptr, &owner);
    stateFor(1)->value = 11;
    stateFor(2)->value = 22;
    stateFor(3)->value = 33;

    arenas.beginBuild();
    root->update(new TestRoot(new Column{{.children = {new Counter(0, Key{0}),
                                                       new Counter(1, Key{1}),
                                                       new Counter(2, Key{2}),
                                                       new Counter(3, Key{3})}}}));

    CHECK(g_state_constructions == 4);  // exactly one new State
    CHECK(g_state_disposals == 0);
    CHECK(textsAre(root, {"0", "11", "22", "33"}));

    root->unmount();
    delete root;
}

void test_removing_a_keyed_child_disposes_it_once() {
    std::printf("reconcile: a keyed child removed from the middle is disposed once\n");

    BuildArenas arenas;
    BuildOwner owner;
    resetCounters();

    arenas.beginBuild();
    Element *root =
        (new TestRoot(new Column{{.children = {new Counter(1, Key{1}), new Counter(2, Key{2}),
                                               new Counter(3, Key{3})}}}))
            ->createElement();
    root->mount(nullptr, &owner);
    stateFor(1)->value = 11;
    stateFor(3)->value = 33;

    arenas.beginBuild();
    root->update(new TestRoot(
        new Column{{.children = {new Counter(1, Key{1}), new Counter(3, Key{3})}}}));

    CHECK(g_state_constructions == 3);  // no rebuilds
    CHECK(g_state_disposals == 1);      // just the one that left
    CHECK(stateFor(2) == nullptr);
    CHECK(textsAre(root, {"11", "33"}));

    root->unmount();
    CHECK(g_state_disposals == 3);
    delete root;
}

void test_unkeyed_children_still_reconcile_positionally() {
    std::printf("reconcile: without keys, State follows position\n");

    BuildArenas arenas;
    BuildOwner owner;
    resetCounters();

    arenas.beginBuild();
    Element *root = (new TestRoot(new Column{{.children = {new Counter(1), new Counter(2),
                                                           new Counter(3)}}}))
                        ->createElement();
    root->mount(nullptr, &owner);
    stateFor(1)->value = 11;
    stateFor(2)->value = 22;
    stateFor(3)->value = 33;

    /* Drop the middle one. With no keys there is nothing to identify the
     * survivors by, so slot 1 keeps the State that was already in slot 1 and
     * the list simply gets shorter -- the old behaviour, unchanged. */
    arenas.beginBuild();
    root->update(new TestRoot(new Column{{.children = {new Counter(1), new Counter(3)}}}));

    CHECK(g_state_constructions == 3);
    CHECK(g_state_disposals == 1);
    CHECK(textsAre(root, {"11", "22"}));

    root->unmount();
    delete root;
}

void test_mixed_keyed_and_unkeyed_children() {
    std::printf("reconcile: keyed children move past an unkeyed one\n");

    BuildArenas arenas;
    BuildOwner owner;
    resetCounters();

    arenas.beginBuild();
    Element *root =
        (new TestRoot(new Column{{.children = {new Counter(1, Key{1}), new Counter(2),
                                               new Counter(3, Key{3})}}}))
            ->createElement();
    root->mount(nullptr, &owner);
    stateFor(1)->value = 11;
    stateFor(2)->value = 22;
    stateFor(3)->value = 33;

    arenas.beginBuild();
    root->update(new TestRoot(new Column{{.children = {new Counter(3, Key{3}), new Counter(2),
                                                       new Counter(1, Key{1})}}}));

    /* The two keyed ones swapped and kept their State; the unkeyed one in the
     * middle could not be identified across the move, so it was rebuilt. */
    CHECK(g_state_constructions == 4);
    CHECK(g_state_disposals == 1);
    CHECK(textsAre(root, {"33", "2", "11"}));

    root->unmount();
    delete root;
}

/* ---- FMS widgets ------------------------------------------------------- */

/* Every render object in a subtree, in tree order.  Comparing two of these
 * across a rebuild answers the only question that matters for cost: did the
 * reconciler keep the objects, or throw them away and make new ones?  A new
 * RenderLv means a new lv_obj, and an lv_obj costs about a millisecond on the
 * device. */
void collectRenderObjects(RenderObject *r, std::vector<RenderObject *> &out) {
    out.push_back(r);
    for (RenderObject *c : r->children()) collectRenderObjects(c, out);
}

/* A theme with no fonts: RenderText falls back to LVGL's default, and nothing
 * here is laid out anyway. */
FmsThemeData testTheme() {
    FmsThemeData data;
    data.color = defaultPalette();
    data.font = FmsTypography{};
    data.metric = defaultMetrics();
    return data;
}

void test_disabling_a_button_keeps_its_subtree() {
    std::printf("fms: a button greying out does not rebuild it\n");

    BuildArenas arenas;
    BuildOwner owner;

    const auto build = [](bool enabled) {
        return new TestRoot(new FmsTheme{{
            .data = testTheme(),
            .child = new FmsButton{{.text = "MOVE UP",
                                    .text2 = "",
                                    .role = FmsRole::Entry,
                                    .enabled = enabled,
                                    .on_tap = [] {}}},
        }});
    };

    arenas.beginBuild();
    Element *root = build(true)->createElement();
    root->mount(nullptr, &owner);

    std::vector<RenderObject *> before;
    collectRenderObjects(root->renderObject(), before);
    CHECK(before.size() > 2);  // the detector, the box and the label at least

    /* The detector is the button's outermost render object.  Held as a plain
     * RenderObject* and looked up again after each rebuild: should this ever
     * regress, the old one is deleted and the slot holds a decorated box
     * instead, and the test has to say so rather than downcast it and read
     * whatever is there. */
    RenderObject *const first = root->renderObject()->firstChild();
    CHECK(static_cast<bool>(static_cast<RenderGestureDetector *>(first)->on_tap));

    arenas.beginBuild();
    root->update(build(false));

    std::vector<RenderObject *> after;
    collectRenderObjects(root->renderObject(), after);

    /* The same objects, in the same places: only the colour and the callback
     * moved. */
    CHECK(before == after);
    CHECK(root->renderObject()->firstChild() == first);
    if (root->renderObject()->firstChild() == first) {
        CHECK(!static_cast<RenderGestureDetector *>(first)->on_tap);  // reacts to nothing
    }

    /* And back, without a rebuild either way. */
    arenas.beginBuild();
    root->update(build(true));

    std::vector<RenderObject *> again;
    collectRenderObjects(root->renderObject(), again);
    CHECK(before == again);
    if (root->renderObject()->firstChild() == first) {
        CHECK(static_cast<bool>(static_cast<RenderGestureDetector *>(first)->on_tap));
    }

    root->unmount();
    delete root;
}

void test_a_field_box_without_a_callback_keeps_its_shape() {
    std::printf("fms: a field box gaining a callback does not rebuild it\n");

    BuildArenas arenas;
    BuildOwner owner;

    const auto build = [](bool tappable) {
        return new TestRoot(new FmsTheme{{
            .data = testTheme(),
            .child = new FmsFieldBox{{.text = "153",
                                      .unit = "KT",
                                      .role = FmsRole::Entry,
                                      .empty = false,
                                      .on_tap = tappable ? VoidCallback([] {}) : VoidCallback{}}},
        }});
    };

    arenas.beginBuild();
    Element *root = build(false)->createElement();
    root->mount(nullptr, &owner);

    std::vector<RenderObject *> before;
    collectRenderObjects(root->renderObject(), before);

    arenas.beginBuild();
    root->update(build(true));

    std::vector<RenderObject *> after;
    collectRenderObjects(root->renderObject(), after);
    CHECK(before == after);

    root->unmount();
    delete root;
}

/* ---- Windowed lists ---------------------------------------------------- */

const char *const kWindowItems[] = {"ALPHA", "BRAVO", "CHARLIE", "DELTA", "ECHO", "FOXTROT"};

void test_a_window_position_clamps_at_both_ends() {
    std::printf("window: the position clamps, and blank slots read as -1\n");

    FmsWindowPos p;
    p.count = 24;
    p.window = 8;
    p.step = 1;

    CHECK(p.maxFirst() == 16);  // 24 - 8: the last full screen
    CHECK(!p.canPrev());
    CHECK(p.canNext());

    p.next();
    CHECK(p.first == 1);
    CHECK(p.canPrev());

    p.first = 16;
    CHECK(!p.canNext());
    p.next();
    CHECK(p.first == 16);  // already at the end; next() is not an off-by-one

    p.prev();
    CHECK(p.first == 15);

    /* A step of `window` is the same type doing page-at-a-time, and it clamps to
     * the last full screen rather than overshooting into blanks. */
    p.step = 8;
    p.first = 0;
    p.next();
    CHECK(p.first == 8);
    p.next();
    CHECK(p.first == 16);
    p.next();
    CHECK(p.first == 16);

    /* A list shorter than the window has nowhere to go, in either direction. */
    FmsWindowPos q;
    q.count = 3;
    q.window = 8;
    CHECK(q.maxFirst() == 0);
    CHECK(!q.canNext());
    CHECK(!q.canPrev());
    CHECK(q.at(0) == 0);
    CHECK(q.at(2) == 2);
    CHECK(q.at(3) == -1);  // past the end of the list
    CHECK(q.at(8) == -1);  // past the end of the window

    /* And an empty one is not a special case anywhere. */
    FmsWindowPos e;
    e.window = 4;
    CHECK(e.at(0) == -1);
    CHECK(!e.canNext());
    CHECK(!e.canPrev());
}

Widget *windowOf(int first, bool keyed) {
    FmsWindowPos pos;
    pos.count = 6;
    pos.window = 3;
    pos.first = first;

    return new TestRoot(new FmsTheme{{
        .data = testTheme(),
        .child = new FmsWindow{{
            .pos = pos,
            .row =
                [keyed](int index, int slot) -> Widget * {
                    (void)slot;
                    return new Text{{.text = index >= 0 ? Str(kWindowItems[index]) : Str("-----"),
                                     .key = keyed && index >= 0 ? Key{index} : Key{}}};
                },
        }},
    }});
}

void test_a_window_draws_a_row_per_slot_even_past_the_end() {
    std::printf("window: a slot past the end of the list still draws\n");

    BuildArenas arenas;
    BuildOwner owner;

    arenas.beginBuild();
    Element *root = windowOf(0, false)->createElement();
    root->mount(nullptr, &owner);
    CHECK(textsAre(root, {"ALPHA", "BRAVO", "CHARLIE"}));

    /* Hang the window off the end.  Three rows, still: the missing ones draw
     * blank rather than being dropped, because a Column that gets shorter is a
     * change of shape and costs the reconciler real work. */
    arenas.beginBuild();
    root->update(windowOf(5, false));
    CHECK(textsAre(root, {"FOXTROT", "-----", "-----"}));

    root->unmount();
    delete root;
}

void test_stepping_an_unkeyed_window_only_rewrites_text() {
    std::printf("window: stepping it disturbs nothing but the strings\n");

    BuildArenas arenas;
    BuildOwner owner;

    arenas.beginBuild();
    Element *root = windowOf(0, false)->createElement();
    root->mount(nullptr, &owner);
    CHECK(textsAre(root, {"ALPHA", "BRAVO", "CHARLIE"}));

    std::vector<RenderObject *> before;
    collectRenderObjects(root->renderObject(), before);

    arenas.beginBuild();
    root->update(windowOf(1, false));
    CHECK(textsAre(root, {"BRAVO", "CHARLIE", "DELTA"}));

    std::vector<RenderObject *> after;
    collectRenderObjects(root->renderObject(), after);

    /* The same objects, in the same order.  This is the whole claim behind
     * paging rather than scrolling, said without LVGL in the room: nothing was
     * created, so nothing had to be created on the panel either, and nothing
     * changed position, so nothing had to be told that it had moved. */
    CHECK(before == after);

    /* All the way to the end, still the same objects. */
    arenas.beginBuild();
    root->update(windowOf(3, false));
    CHECK(textsAre(root, {"DELTA", "ECHO", "FOXTROT"}));

    std::vector<RenderObject *> at_end;
    collectRenderObjects(root->renderObject(), at_end);
    CHECK(before == at_end);

    root->unmount();
    delete root;
}

void test_keying_the_rows_of_a_window_rebuilds_them() {
    std::printf("window: keys would make a step build and destroy rows\n");

    BuildArenas arenas;
    BuildOwner owner;

    arenas.beginBuild();
    Element *root = windowOf(0, true)->createElement();
    root->mount(nullptr, &owner);
    CHECK(textsAre(root, {"ALPHA", "BRAVO", "CHARLIE"}));

    std::vector<RenderObject *> before;
    collectRenderObjects(root->renderObject(), before);

    arenas.beginBuild();
    root->update(windowOf(1, true));
    CHECK(textsAre(root, {"BRAVO", "CHARLIE", "DELTA"}));

    std::vector<RenderObject *> after;
    collectRenderObjects(root->renderObject(), after);

    /* Same shape, different objects: the key tied each row to an item, so the
     * row that fell off the top was destroyed and the one that arrived at the
     * bottom was built.  That is right for a list that is reordered and wrong
     * for one that is only looked through, and it is why FmsWindow does not key
     * its rows.
     *
     * The test is here to fail if that ever stops being true, because the whole
     * argument for the unkeyed window is that the keyed one costs more. */
    CHECK(after.size() == before.size());
    CHECK(before != after);

    root->unmount();
    delete root;
}

void test_a_pager_at_an_end_keeps_its_shape() {
    std::printf("window: an arrow greying out does not rebuild the pager\n");

    BuildArenas arenas;
    BuildOwner owner;

    const auto build = [](int first) {
        FmsWindowPos pos;
        pos.count = 6;
        pos.window = 3;
        pos.first = first;
        return new TestRoot(new FmsTheme{{
            .data = testTheme(),
            .child = new FmsPager{{.pos = pos, .on_prev = [] {}, .on_next = [] {}}},
        }});
    };

    /* At the top, where the up arrow is dead. */
    arenas.beginBuild();
    Element *root = build(0)->createElement();
    root->mount(nullptr, &owner);

    std::vector<RenderObject *> before;
    collectRenderObjects(root->renderObject(), before);
    CHECK(before.size() > 4);  // a row, two detectors, two boxes, two glyphs

    /* Into the middle, off the far end, and back.  The arrows cross between live
     * and dead every time the window reaches an end, which is often, so this has
     * to cost nothing -- the same rule FmsButton follows. */
    for (const int first : {1, 3, 0}) {
        arenas.beginBuild();
        root->update(build(first));

        std::vector<RenderObject *> after;
        collectRenderObjects(root->renderObject(), after);
        CHECK(before == after);
    }

    root->unmount();
    delete root;
}

/* ---- Paint accounting -------------------------------------------------- */

/* A display, so paint has somewhere to push.  Every test above this line works
 * on the render tree alone; counting what reached LVGL needs LVGL. */
lv_display_t *headlessDisplay() {
    static lv_display_t *disp = nullptr;
    if (disp != nullptr) return disp;

    constexpr int32_t kW = 320;
    constexpr int32_t kH = 240;
    static auto *fb = static_cast<uint8_t *>(std::calloc(static_cast<size_t>(kW) * kH * 2, 1));

    disp = lv_display_create(kW, kH);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, fb, nullptr, static_cast<size_t>(kW) * kH * 2,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp,
                            [](lv_display_t *d, const lv_area_t *, uint8_t *) {
                                lv_display_flush_ready(d);  // nothing to push anywhere
                            });
    return disp;
}

void test_paint_counts_the_text_it_rewrites() {
    std::printf("paint: a rewritten label is counted, an unchanged one is not\n");

    lv_display_t *disp = headlessDisplay();
    lv_obj_t *screen = lv_display_get_screen_active(disp);

    BuildArenas arenas;
    BuildOwner owner;

    const auto build = [](const char *a, const char *b) {
        return new TestRoot(
            new Column{{.children = {new Text{{.text = a}}, new Text{{.text = b}}}}});
    };

    const auto paint = [&](Element *el) {
        auto *view = static_cast<RenderView *>(el->renderObject());
        view->screen = Size{320, 240};
        view->layout(BoxConstraints::tight(view->screen));

        PaintContext ctx;
        ctx.root = screen;
        view->paint(ctx, Offset{0, 0});
        return ctx;
    };

    arenas.beginBuild();
    Element *el = build("ALPHA", "BRAVO")->createElement();
    el->mount(nullptr, &owner);

    const PaintContext first = paint(el);
    CHECK(first.created == 2);
    CHECK(first.retexted == 2);  // creating a label writes its text as well

    /* Same strings: there is nothing to push, and pushing anyway would cost a
     * reallocation and an invalidation per label. */
    arenas.beginBuild();
    el->update(build("ALPHA", "BRAVO"));
    const PaintContext again = paint(el);
    CHECK(again.created == 0);
    CHECK(again.moved == 0);
    CHECK(again.retexted == 0);

    /* One string changed.  This is the frame that used to be invisible: nothing
     * is created, nothing moves, and on the device it still costs 0.14ms -- so
     * `created` and `moved` both reading zero says nothing about what it cost. */
    arenas.beginBuild();
    el->update(build("ALPHA", "CHARLIE"));
    const PaintContext one = paint(el);
    CHECK(one.created == 0);
    CHECK(one.moved == 0);
    CHECK(one.retexted == 1);

    el->unmount();
    delete el;
}

void test_stepping_a_window_is_all_text_and_no_structure() {
    std::printf("paint: a window step rewrites every row and disturbs nothing\n");

    lv_display_t *disp = headlessDisplay();
    lv_obj_t *screen = lv_display_get_screen_active(disp);

    BuildArenas arenas;
    BuildOwner owner;

    arenas.beginBuild();
    Element *el = windowOf(0, false)->createElement();
    el->mount(nullptr, &owner);

    const auto paint = [&](Element *e) {
        auto *view = static_cast<RenderView *>(e->renderObject());
        view->screen = Size{320, 240};
        view->layout(BoxConstraints::tight(view->screen));

        PaintContext ctx;
        ctx.root = screen;
        view->paint(ctx, Offset{0, 0});
        return ctx;
    };

    (void)paint(el);  // first pass creates everything; the step is what matters

    arenas.beginBuild();
    el->update(windowOf(1, false));
    const PaintContext step = paint(el);

    /* The shape of the whole argument for paging, in three numbers: a step is
     * one text write per visible row, and nothing else at all. */
    CHECK(step.created == 0);
    CHECK(step.moved == 0);
    CHECK(step.retexted == 3);  // the window is three rows wide in these tests

    el->unmount();
    delete el;
}

/* ---- Shared styles ----------------------------------------------------- */

void test_repeating_a_style_set_does_not_grow_the_cache() {
    std::printf("styles: the same set of looks, repeated, is cached once each\n");

    lv_display_t *disp = headlessDisplay();
    lv_obj_t *screen = lv_display_get_screen_active(disp);

    /* The tests above painted too, and their objects are gone, so this is a
     * clean session to count within. */
    releaseStyleCache();

    BuildArenas arenas;
    BuildOwner owner;

    /* Two text looks and one box look, with only the strings varying -- the
     * shape of every real screen: many objects, few looks. */
    const auto build = [](int tick, bool extra_look) {
        return new TestRoot(new Column{{.children = {
            new Text{{.text = fmt("%d", tick), .color = Color::rgb(0x00FF00)}},
            new Text{{.text = fmt("%d", tick * 2),
                      .color = Color::rgb(extra_look ? 0xFFAA00 : 0x00FF00)}},
            new Text{{.text = "FIXED", .color = Color::rgb(0xFF00FF)}},
            new Container{{.color = Color::rgb(0x202020),
                           .child = new Text{{.text = "BOXED", .color = Color::rgb(0x00FF00)}}}},
        }}});
    };

    const auto paint = [&](Element *e) {
        auto *view = static_cast<RenderView *>(e->renderObject());
        view->screen = Size{320, 240};
        view->layout(BoxConstraints::tight(view->screen));

        PaintContext ctx;
        ctx.root = screen;
        view->paint(ctx, Offset{0, 0});
    };

    arenas.beginBuild();
    Element *el = build(0, false)->createElement();
    el->mount(nullptr, &owner);
    paint(el);

    const StyleCacheStats first = styleCacheStats();
    CHECK(first.text_styles == 2);  // green and magenta
    CHECK(first.box_styles == 1);

    /* Twenty more frames of changing text. Nothing about the *looks* changed, so
     * nothing should be added -- this is the claim the cache rests on, and the
     * reason it can be allowed to never evict. */
    for (int i = 1; i <= 20; i++) {
        arenas.beginBuild();
        el->update(build(i, false));
        paint(el);
    }

    const StyleCacheStats after = styleCacheStats();
    CHECK(after.text_styles == first.text_styles);
    CHECK(after.box_styles == first.box_styles);

    /* And one genuinely new colour does add exactly one, so the numbers above
     * are a cache that is working rather than a counter that is stuck. */
    arenas.beginBuild();
    el->update(build(21, true));
    paint(el);

    const StyleCacheStats grown = styleCacheStats();
    CHECK(grown.text_styles == first.text_styles + 1);
    CHECK(grown.box_styles == first.box_styles);

    el->unmount();
    delete el;
}

void test_releasing_the_style_cache_empties_it() {
    std::printf("styles: releasing the cache ends the session\n");

    releaseStyleCache();

    const StyleCacheStats empty = styleCacheStats();
    CHECK(empty.text_styles == 0);
    CHECK(empty.box_styles == 0);
}

/* ---- Published diagnostics -------------------------------------------- */

std::atomic<uint32_t> g_test_refresh_clock{0};

uint32_t testRefreshClock() { return g_test_refresh_clock.load(std::memory_order_relaxed); }

void test_refresh_stats_take_a_consistent_interval() {
    std::printf("stats: refresh intervals stay coherent across two threads\n");

    constexpr int32_t kW = 4;
    constexpr int32_t kH = 4;
    std::vector<uint8_t> fb(static_cast<size_t>(kW) * kH * 2);
    lv_display_t *disp = lv_display_create(kW, kH);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, fb.data(), nullptr, fb.size(), LV_DISPLAY_RENDER_MODE_DIRECT);

    RefreshStats refresh;
    refresh.attach(disp, testRefreshClock);

    constexpr uint32_t kRefreshes = 2000;
    constexpr uint32_t kDurationUs = 7;
    std::atomic<bool> done{false};
    std::atomic<bool> inconsistent{false};
    uint64_t seen_refreshes = 0;
    uint64_t seen_busy_us = 0;

    std::thread reader([&] {
        const auto consume = [&](const RefreshSnapshot &s) {
            if (s.busy_us != static_cast<uint64_t>(s.refreshes) * kDurationUs) {
                inconsistent.store(true, std::memory_order_relaxed);
            }
            if ((s.refreshes == 0 && s.max_us != 0) ||
                (s.refreshes != 0 && s.max_us != kDurationUs)) {
                inconsistent.store(true, std::memory_order_relaxed);
            }
            seen_refreshes += s.refreshes;
            seen_busy_us += s.busy_us;
        };

        while (!done.load(std::memory_order_acquire)) {
            consume(refresh.take());
            std::this_thread::yield();
        }
        consume(refresh.take());
    });

    for (uint32_t i = 0; i < kRefreshes; i++) {
        g_test_refresh_clock.store(i * 100 + 1, std::memory_order_relaxed);
        lv_display_send_event(disp, LV_EVENT_REFR_START, nullptr);
        g_test_refresh_clock.store(i * 100 + 1 + kDurationUs, std::memory_order_relaxed);
        lv_display_send_event(disp, LV_EVENT_REFR_READY, nullptr);
        if ((i & 31U) == 0) std::this_thread::yield();
    }

    done.store(true, std::memory_order_release);
    reader.join();

    CHECK(!inconsistent.load(std::memory_order_relaxed));
    CHECK(seen_refreshes == kRefreshes);
    CHECK(seen_busy_us == static_cast<uint64_t>(kRefreshes) * kDurationUs);

    /* The callbacks point at the local RefreshStats, so remove their owner by
     * deleting the display before the object leaves scope. */
    lv_display_delete(disp);
}

std::atomic<uint32_t> g_test_frame_clock{0};

uint32_t testFrameClock() { return g_test_frame_clock.fetch_add(1, std::memory_order_relaxed); }

void test_frame_and_style_stats_are_safe_to_read_during_frames() {
    std::printf("stats: frame and style snapshots stay coherent while painting\n");

    releaseStyleCache();
    lv_display_t *disp = headlessDisplay();
    FmsApp &app = FmsApp::instance();
    std::atomic<uint32_t> style_index{0};
    g_test_frame_clock.store(0, std::memory_order_relaxed);
    app.setClock(testFrameClock);
    app.init(disp, [&] {
        const uint32_t i = style_index.load(std::memory_order_relaxed);
        const uint32_t shade = i & 0xFFU;
        return new Text{{.text = fmt("%u", i),
                         .color = Color::rgb((shade << 16) | (shade << 8) | shade)}};
    });

    constexpr uint32_t kLooks = 32;
    constexpr uint32_t kFrames = 512;
    std::atomic<bool> done{false};
    std::atomic<bool> inconsistent{false};
    std::atomic<uint32_t> reads{0};

    std::thread reader([&] {
        uint32_t last_builds = 0;
        uint32_t last_text_styles = 0;
        while (!done.load(std::memory_order_acquire)) {
            const FrameStats f = app.stats();
            const StyleCacheStats s = styleCacheStats();
            if (f.builds != 0 && f.total_us != f.build_us + f.layout_us + f.paint_us) {
                inconsistent.store(true, std::memory_order_relaxed);
            }
            if (f.builds < last_builds || s.text_styles < last_text_styles ||
                s.text_styles > kLooks || s.box_styles != 0) {
                inconsistent.store(true, std::memory_order_relaxed);
            }
            last_builds = f.builds;
            last_text_styles = s.text_styles;
            reads.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    for (uint32_t i = 0; i < kFrames; i++) {
        style_index.store(i % kLooks, std::memory_order_relaxed);
        app.requestFrame();
        lv_tick_inc(10);
        lv_timer_handler();
        std::this_thread::yield();
    }

    done.store(true, std::memory_order_release);
    reader.join();

    const FrameStats final_frame = app.stats();
    const StyleCacheStats final_styles = styleCacheStats();
    CHECK(reads.load(std::memory_order_relaxed) > 0);
    CHECK(!inconsistent.load(std::memory_order_relaxed));
    CHECK(final_frame.builds >= kFrames);
    CHECK(final_styles.text_styles == kLooks);
    CHECK(final_styles.box_styles == 0);

    app.shutdown();
    CHECK(styleCacheStats().text_styles == 0);
    CHECK(styleCacheStats().box_styles == 0);
}

/* ---- Cross-thread ------------------------------------------------------ */

void test_request_from_another_thread_is_seen() {
    std::printf("threads: requestFrame from another thread reaches the frame loop\n");

    BuildOwner owner;
    CHECK(owner.takeNeedsBuild());   // starts dirty, and claiming it clears it
    CHECK(!owner.takeNeedsBuild());

    /* What a sensor task does: publish, then ask for a frame. */
    std::atomic<int> published{0};
    std::thread producer([&] {
        published.store(42, std::memory_order_relaxed);
        owner.scheduleBuild();
    });
    producer.join();

    CHECK(owner.takeNeedsBuild());
    /* The acquire in takeNeedsBuild pairs with the release in scheduleBuild, so
     * the value published before the request is visible to the build after it. */
    CHECK(published.load(std::memory_order_relaxed) == 42);
    CHECK(!owner.takeNeedsBuild());
}

void test_a_request_during_a_build_is_not_lost() {
    std::printf("threads: a request that lands mid-build survives to the next frame\n");

    BuildOwner owner;
    (void)owner.takeNeedsBuild();  // consume the initial dirty state

    /* The frame loop claims the flag and starts building... */
    owner.scheduleBuild();
    CHECK(owner.takeNeedsBuild());

    /* ...and a producer asks again while that build is still running. */
    std::thread producer([&] { owner.scheduleBuild(); });
    producer.join();

    /* The next frame must still rebuild, or that update is never shown. */
    CHECK(owner.takeNeedsBuild());
}

/* What the simulator and the host install.  The device installs its FreeRTOS
 * task handle instead; the framework only ever compares these. */
ThreadId hostThreadId() {
    return static_cast<ThreadId>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

void test_without_a_thread_id_the_check_is_off() {
    std::printf("threads: with no thread-id function the check stays out of the way\n");

    BuildOwner::setThreadIdFn(nullptr);

    BuildOwner owner;
    /* This is the state the device is in when nobody installs one, and it has to
     * be harmless.  It was not: the check used to call std::this_thread::get_id()
     * unconditionally, which on ESP-IDF goes through pthread_self() and asserts
     * outright on a FreeRTOS task that was not created as a pthread -- so the
     * board rebooted on its first frame.  Never binding, and never asking, is
     * the only safe thing to do when the platform has not said how to answer. */
    owner.bindToCurrentThread();
    CHECK(!owner.bound());
    CHECK(owner.onBuildThread());  // permissive: a diagnostic must not deny work

    std::thread other([&] { CHECK(owner.onBuildThread()); });
    other.join();
}

void test_the_frame_thread_is_recorded() {
    std::printf("threads: the build thread is remembered once bound\n");

    BuildOwner::setThreadIdFn(hostThreadId);

    BuildOwner owner;
    CHECK(!owner.bound());
    CHECK(owner.onBuildThread());  // unbound: the check is inert, as in these tests

    std::thread frame_loop([&] { owner.bindToCurrentThread(); });
    frame_loop.join();

    CHECK(owner.bound());
    /* Bound to a thread that is not this one, so this one is not allowed to
     * mutate the tree -- which is what markNeedsBuild() asserts on. */
    CHECK(!owner.onBuildThread());

    BuildOwner::setThreadIdFn(nullptr);  // global state: leave it as it was found
}

void test_the_isr_requester_sets_the_same_flag() {
    std::printf("threads: FrameRequester pokes the flag FmsApp reads\n");

    BuildOwner owner;
    (void)owner.takeNeedsBuild();

    /* FmsApp::requester() hands out one of these; here we build it the same way
     * from the owner's flag, since constructing an FmsApp needs a display. */
    std::atomic<bool> *flag = owner.dirtyFlag();
    flag->store(true, std::memory_order_release);

    CHECK(owner.takeNeedsBuild());
}

void test_arena_resets_between_builds() {
    std::printf("arena: reset destroys the widgets and rewinds\n");

    Arena a;
    Arena::setCurrent(&a);

    (void)new Text{{.text = "hello"}};
    (void)new Text{{.text = "world"}};
    CHECK(a.objectCount() == 2);
    CHECK(a.bytesUsed() > 0);

    a.reset();
    CHECK(a.objectCount() == 0);
    CHECK(a.bytesUsed() == 0);

    Arena::setCurrent(nullptr);
}

}  // namespace

int main() {
    lv_init();  // RenderText measures with LVGL's font tables

    test_flex_distributes_free_space();
    test_flex_leaves_room_for_inflexible_children();
    test_flex_spacing_is_taken_from_the_free_space();
    test_main_axis_alignment();
    test_cross_axis_stretch_makes_the_cross_constraint_tight();
    test_stretch_shrink_wraps_under_a_loose_cross_constraint();
    test_stretch_fills_a_tight_cross_constraint();
    test_main_axis_size_min_shrink_wraps();
    test_padding_deflates_then_reinflates();
    test_align_centres_and_fills();
    test_align_shrink_wraps_when_unbounded();
    test_constrained_box_is_clamped_by_its_parent();
    test_stack_positions_from_edges();

    test_state_survives_a_rebuild();
    test_a_different_key_throws_the_state_away();
    test_children_can_be_added_and_removed();
    test_keyed_children_survive_a_reorder();
    test_keyed_prepend_rebuilds_only_the_new_child();
    test_removing_a_keyed_child_disposes_it_once();
    test_unkeyed_children_still_reconcile_positionally();
    test_mixed_keyed_and_unkeyed_children();

    test_disabling_a_button_keeps_its_subtree();
    test_a_field_box_without_a_callback_keeps_its_shape();

    test_a_window_position_clamps_at_both_ends();
    test_a_window_draws_a_row_per_slot_even_past_the_end();
    test_stepping_an_unkeyed_window_only_rewrites_text();
    test_keying_the_rows_of_a_window_rebuilds_them();
    test_a_pager_at_an_end_keeps_its_shape();

    test_paint_counts_the_text_it_rewrites();
    test_stepping_a_window_is_all_text_and_no_structure();

    test_repeating_a_style_set_does_not_grow_the_cache();
    test_releasing_the_style_cache_empties_it();

    test_refresh_stats_take_a_consistent_interval();
    test_frame_and_style_stats_are_safe_to_read_during_frames();

    test_request_from_another_thread_is_seen();
    test_a_request_during_a_build_is_not_lost();
    test_without_a_thread_id_the_check_is_off();
    test_the_frame_thread_is_recorded();
    test_the_isr_requester_sets_the_same_flag();
    test_arena_resets_between_builds();

    /* Every element built above has been unmounted and deleted, so the objects
     * that held these styles are gone and it is safe -- and, for a sanitizer
     * run, necessary -- to hand them back. */
    releaseStyleCache();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
