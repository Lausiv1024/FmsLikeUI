#pragma once

/* The FMS widget set.
 *
 * Everything here is a StatelessWidget composed from the primitives -- there is
 * no new render object except where a shape genuinely cannot be made of
 * rectangles (the slanted tabs, the dropdown triangle, the radio dot), and those
 * use CustomPaint.
 *
 * Colours come from FmsTheme by role, never by name: a value the pilot may enter
 * is `entry`, not `cyan`.
 */

#include <cstddef>
#include <functional>

#include "fmsui/theme.h"
#include "fmsui/widgets.h"

namespace fmsui {

/* ---- Text -------------------------------------------------------------- */

struct FmsLabelArgs {
    Str text;
    Key key{};
};

/* A static name. Always the label colour, always the label size. */
class FmsLabel : public StatelessWidget {
public:
    explicit FmsLabel(FmsLabelArgs a) : args_(std::move(a)) { key = a.key; }
    FMSUI_WIDGET(FmsLabel)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsLabelArgs args_;
};

/* What a value *means*, which on a real FMS is what its colour says. */
enum class FmsRole : uint8_t {
    Entry,       /* the pilot may type this        (cyan) */
    Computed,    /* the system worked this out     (green) */
    Attention,   /* wants action, or is abnormal   (amber) */
    Constraint,  /* imposed from outside           (magenta) */
    Label,       /* not a value at all             (white) */
};

Color colorFor(const FmsThemeData &theme, FmsRole role);

struct FmsValueArgs {
    Str text;
    Str unit;  /* drawn smaller, after the number, as on the real screens */
    FmsRole role = FmsRole::Computed;
    Key key{};
};

class FmsValue : public StatelessWidget {
public:
    explicit FmsValue(FmsValueArgs a) : args_(std::move(a)) { key = a.key; }
    FMSUI_WIDGET(FmsValue)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsValueArgs args_;
};

/* ---- Fields and controls ----------------------------------------------- */

struct FmsFieldBoxArgs {
    Str text;
    Str unit;
    FmsRole role = FmsRole::Entry;
    /* An empty field shows the dashes the real screens show, not nothing. */
    bool empty = false;
    VoidCallback on_tap{};
    Key key{};
};

/* A framed, tappable value. The atom of every FMS page. */
class FmsFieldBox : public StatelessWidget {
public:
    explicit FmsFieldBox(FmsFieldBoxArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsFieldBox)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsFieldBoxArgs args_;
};

struct FmsButtonArgs {
    Str text;
    /* Two lines, as on POS MONITOR / CPNY T.O REQUEST. Empty means one line. */
    Str text2;
    FmsRole role = FmsRole::Label;
    bool enabled = true;
    VoidCallback on_tap{};
    Key key{};
};

class FmsButton : public StatelessWidget {
public:
    explicit FmsButton(FmsButtonArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsButton)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsButtonArgs args_;
};

struct FmsDropdownArgs {
    /* With items, this is a real dropdown: tapping it opens the list, the current
     * item is highlighted, and picking one closes it. Without items it is just a
     * framed button with a triangle -- which is what the menu bar's ACTIVE,
     * POSITION, SEC INDEX and DATA are on the real screens. */
    const char *const *items = nullptr;
    size_t count = 0;
    int index = 0;

    /* Shown when there are no items. With items, the selected one is shown. */
    Str text;

    FmsRole role = FmsRole::Entry;
    std::function<void(int)> on_selected{};
    VoidCallback on_tap{};  // items-less form
    Key key{};
};

/* A framed value with the little triangle, and the list it opens.
 *
 * Stateful because "is the list open" belongs to the control, not to whoever
 * placed it: making every caller thread an `open` bool through their State would
 * be boilerplate that says nothing about their problem.
 */
class FmsDropdown : public StatefulWidget {
public:
    explicit FmsDropdown(FmsDropdownArgs a) : args(std::move(a)) { key = args.key; }
    FMSUI_WIDGET(FmsDropdown)
    StateBase *createState() const override;

    FmsDropdownArgs args;
};

struct FmsRadioArgs {
    Str text;
    bool selected = false;
    VoidCallback on_tap{};
    Key key{};
};

class FmsRadio : public StatelessWidget {
public:
    explicit FmsRadio(FmsRadioArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsRadio)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsRadioArgs args_;
};

/* ---- Structure --------------------------------------------------------- */

struct FmsDividerArgs {
    float inset = 0;
    Key key{};
};

class FmsDivider : public StatelessWidget {
public:
    explicit FmsDivider(FmsDividerArgs a = {}) : args_(a) { key = a.key; }
    FMSUI_WIDGET(FmsDivider)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsDividerArgs args_;
};

struct FmsPanelArgs {
    Str title;  /* the pale band across the top, e.g. ACTIVE/PERF */
    Widget *child = nullptr;
    Key key{};
};

class FmsPanel : public StatelessWidget {
public:
    explicit FmsPanel(FmsPanelArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsPanel)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsPanelArgs args_;
};

/* The slanted tabs: T.O / CLB / CRZ / DES / APPR / GA.
 *
 * The one shape in the whole UI that cannot be made of rectangles.  Each tab is
 * a trapezoid whose left edge leans, and they overlap so the selected one sits
 * in front -- which is exactly what a Stack of CustomPaints gives us.
 */
struct FmsTabsArgs {
    /* Not a WidgetList: these are strings, and making the caller wrap each one in
     * a Text would let them style tabs inconsistently. */
    const char *const *labels = nullptr;
    size_t count = 0;
    int index = 0;
    std::function<void(int)> on_changed{};
    Key key{};
};

class FmsTabs : public StatelessWidget {
public:
    explicit FmsTabs(FmsTabsArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsTabs)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsTabsArgs args_;
};

struct FmsScaffoldArgs {
    Str title;      /* FMS 1 / FMS 2 */
    Str subtitle;   /* ETD64 */
    Widget *child = nullptr;
    WidgetList footer;  /* MSG LIST, POS MONITOR ... */
    Key key{};
};

class FmsScaffold : public StatelessWidget {
public:
    explicit FmsScaffold(FmsScaffoldArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsScaffold)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsScaffoldArgs args_;
};

/* ---- Paging ------------------------------------------------------------ */

/* Where a fixed-height window sits over a longer list.
 *
 * The real FMS has no scrollbar. It shows a fixed number of lines and steps the
 * window along the flight plan one waypoint at a time, which is also the better
 * fit for this framework: every RenderObject that paints owns an lv_obj, so a
 * scroll view over N items has to materialise N of them -- or grow a
 * virtualising layer to avoid it -- while a window of W rows only ever costs W,
 * however long the list gets.
 *
 * This is a value rather than a widget, and it is meant to be shared: the page's
 * State holds one and hands it to both FmsWindow and FmsPager. That is what lets
 * the arrows sit somewhere the window is not -- in the list's own header row, or
 * down in the scaffold's footer -- which they must, because a control that pages
 * a region cannot itself be inside the region it pages.
 */
struct FmsWindowPos {
    int count = 0;   /* items in the whole list */
    int window = 1;  /* rows on screen -- a constant, blanks included */
    int first = 0;   /* index of the item in the top row */
    int step = 1;    /* 1 steps a waypoint at a time; `window` steps a page */

    /* How far the window can be moved before it hangs off the end. Zero when
     * the list is shorter than the window: there is nowhere to go. */
    int maxFirst() const { return count > window ? count - window : 0; }

    int clampFirst(int f) const {
        const int hi = maxFirst();
        if (f < 0) return 0;
        return f > hi ? hi : f;
    }

    bool canPrev() const { return first > 0; }
    bool canNext() const { return first < maxFirst(); }

    void prev() { first = clampFirst(first - step); }
    void next() { first = clampFirst(first + step); }

    /* The item in `slot`, or -1 where the window hangs past the end of the list.
     * Draw the blank row rather than dropping it: a list that gets shorter at
     * the bottom changes the shape of the tree, and the reconciler then destroys
     * and recreates lv_objs for what was only ever a text change. */
    int at(int slot) const {
        const int i = first + slot;
        return (slot >= 0 && slot < window && i < count) ? i : -1;
    }
};

struct FmsWindowArgs {
    FmsWindowPos pos;
    float spacing = 0;
    /* Called once per row, with the item to draw or -1 for a row past the end.
     * Must return a widget for every slot, blank ones included. */
    std::function<Widget *(int index, int slot)> row{};
    Key key{};
};

/* A Column of exactly `pos.window` rows, whatever the list is doing.
 *
 * The rows are deliberately unkeyed, which is the opposite of what a reorderable
 * list wants and is right here for the same reason. A key ties an Element to a
 * waypoint rather than to a line on the screen, so stepping the window destroys
 * the row that fell off the top and builds the one that arrived at the bottom.
 * Unkeyed, the rows match slot for slot and a step rewrites their strings and
 * nothing else. Measured on the F-PLN demo, that is 7 lv_objs created and 7
 * moved per step against none at all, and about three times the paint.
 *
 * The keyed case is only that cheap because every row there has the same number
 * of lv_objs, so the survivors keep the indices they had. Give one row a field
 * the others do not have and every object after it shifts as well -- which on
 * the Tab5 is about half a millisecond each, and is what the reorder demo
 * measures. Keys are for a list that is reordered. This one is not reordered,
 * it is looked through.
 */
class FmsWindow : public StatelessWidget {
public:
    explicit FmsWindow(FmsWindowArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsWindow)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsWindowArgs args_;
};

struct FmsPagerArgs {
    /* Read only to decide which arrows are live -- the pager never moves the
     * window itself, because the position belongs to the page's State. */
    FmsWindowPos pos;
    VoidCallback on_prev{};
    VoidCallback on_next{};
    Key key{};
};

/* The two arrows, and nothing else: down first, then up, as on the reference
 * screen. Knows about a window position but nothing about the window, so it can
 * be placed anywhere -- which is the whole point of it being separate.
 *
 * At the ends of the list an arrow greys out. Like FmsButton, that varies the
 * colour and the callback and never the shape, so reaching the last waypoint
 * costs nothing to draw. */
class FmsPager : public StatelessWidget {
public:
    explicit FmsPager(FmsPagerArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsPager)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsPagerArgs args_;
};

/* ---- Entry ------------------------------------------------------------- */

/* The scratchpad: what you have typed but not yet put anywhere.  On the real
 * aircraft this is a physical keyboard; here it is on screen, so the keypad
 * comes with it. */
struct FmsScratchpadArgs {
    Str text;
    Str message;  /* an error or advisory, shown instead of the entry when set */
    bool error = false;
    Key key{};
};

class FmsScratchpad : public StatelessWidget {
public:
    explicit FmsScratchpad(FmsScratchpadArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsScratchpad)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsScratchpadArgs args_;
};

struct FmsKeypadArgs {
    std::function<void(char)> on_key{};    /* a digit, a letter, '.', '/', '-' */
    VoidCallback on_backspace{};
    VoidCallback on_clear{};
    VoidCallback on_enter{};
    bool letters = false;                  /* digits only, or the full MCDU set */
    Key key{};
};

class FmsKeypad : public StatelessWidget {
public:
    explicit FmsKeypad(FmsKeypadArgs a) : args_(std::move(a)) { key = args_.key; }
    FMSUI_WIDGET(FmsKeypad)
    Widget *build(BuildContext &ctx) const override;

private:
    FmsKeypadArgs args_;
};

}  // namespace fmsui
