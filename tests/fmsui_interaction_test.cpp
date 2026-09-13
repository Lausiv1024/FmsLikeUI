/* Headless interaction tests: what a finger actually reaches.
 *
 * The other host test file works on the render tree alone -- layout maths and
 * element identity, with nothing that could be touched.  This one starts one
 * step earlier, at a pointer input device LVGL reads, and follows a tap all the
 * way through: hit test, GestureDetector, setState, rebuild, repaint.
 *
 * Sending LV_EVENT_CLICKED straight to an object would be shorter and would
 * prove much less.  A real tap has to find its target among everything else on
 * the screen, which is where the interesting failures are: a detector with no
 * callback that swallows the touch anyway, a dropdown barrier that lets the
 * press through to the button behind it, a popup whose lv_objs outlive the list
 * that made them.  None of those are visible to a directed event.
 *
 * Separate executable from fmsui_test on purpose.  These tests own an LVGL
 * display, an input device and the FmsApp singleton, and a failure here should
 * read as "the input chain broke" rather than "something about layout" -- which
 * it would not if the two shared a process and a global LVGL state.
 *
 * What this cannot show: the touch controller on the device.  Coordinate
 * mapping, interrupt latency and driver quirks live below the point LVGL is
 * handed a point, and are checked on the board by hand, recorded separately.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "lvgl.h"

#include "fmsui/fmsui.h"
#include "fmsui_fonts.h"

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

void checkInt(long got, long want, const char *what, const char *file, int line) {
    g_checks++;
    if (got == want) return;
    g_failures++;
    std::printf("  FAIL %s:%d: %s = %ld, want %ld\n", file, line, what, got, want);
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(got, want) checkInt((got), (want), #got, __FILE__, __LINE__)

/* ---- The pointer ------------------------------------------------------- */

/* LVGL polls the input device from a timer, so the "finger" is a pair of
 * variables it reads whenever it gets round to it, exactly as a real driver
 * would present the last sample it took. */
struct Pointer {
    lv_point_t point{0, 0};
    bool pressed = false;
};
Pointer g_pointer;

void pointerRead(lv_indev_t *, lv_indev_data_t *data) {
    data->point = g_pointer.point;
    data->state = g_pointer.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Time is ours, not the wall clock's: every step advances it by one frame, so a
 * tap takes the same number of reads on a loaded CI box as on an idle one.  A
 * test that waited in real milliseconds would be the kind that fails once a
 * fortnight and is then marked flaky rather than fixed. */
uint32_t g_micros = 0;
uint32_t fakeMicros() { return g_micros; }

constexpr int32_t kWidth = 1280;   /* the Tab5's panel, and the simulator's */
constexpr int32_t kHeight = 720;
constexpr uint32_t kFrameMs = 33;

/* ---- A session --------------------------------------------------------- */

/* One display, one pointer, one app, torn down in that order.
 *
 * The order is the whole reason this is a class.  FmsApp::shutdown() destroys
 * the element tree -- and with it every lv_obj -- and only then releases the
 * shared styles those objects were pointing at; the display can go once nothing
 * is left on it.  Get that wrong and the failure is a use-after-free under
 * ASan in whichever test happens to run next, which is a long way from the
 * mistake.
 *
 * Each test builds its own, so nothing carries over: not the style cache, not
 * the screen's children, not the singleton's element tree. */
class Session {
public:
    explicit Session(WidgetBuilder builder) {
        fb_ = static_cast<uint8_t *>(
            std::calloc(static_cast<size_t>(kWidth) * kHeight * 2, 1));

        disp_ = lv_display_create(kWidth, kHeight);
        lv_display_set_color_format(disp_, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(disp_, fb_, nullptr,
                               static_cast<size_t>(kWidth) * kHeight * 2,
                               LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(disp_, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
            lv_display_flush_ready(d);  // nothing to push anywhere
        });

        g_pointer = Pointer{};
        indev_ = lv_indev_create();
        lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev_, pointerRead);
        lv_indev_set_display(indev_, disp_);

        FmsApp::instance().setClock(fakeMicros);
        FmsApp::instance().init(disp_, std::move(builder));

        settle();
    }

    ~Session() {
        FmsApp::instance().shutdown();
        FmsApp::instance().setIdleTimeout(0);
        FmsApp::instance().setOutputOffCallback({});
        FmsApp::instance().setOutputOnCallback({});
        lv_indev_delete(indev_);
        lv_display_delete(disp_);
        std::free(fb_);
    }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    /* One frame of the fake clock: LVGL reads the pointer, the app runs its
     * frame timer, and the display refreshes what changed. */
    void step(int frames = 1) {
        for (int i = 0; i < frames; i++) {
            g_micros += kFrameMs * 1000;
            lv_tick_inc(kFrameMs);
            lv_timer_handler();
        }
    }

    /* Enough frames for a rebuild and the repaint that follows it. */
    void settle() { step(4); }

    /* Press, hold, release -- LVGL turns that into the CLICKED a GestureDetector
     * listens for -- and then let the rebuild the callback asked for happen.
     * Held for three reads rather than one so that press and release are never
     * seen in the same poll, which is what a real finger looks like and what
     * LVGL needs to raise PRESSED and RELEASED separately. */
    void tap(int32_t x, int32_t y) {
        g_pointer.point = lv_point_t{x, y};
        g_pointer.pressed = true;
        step(3);
        g_pointer.pressed = false;
        settle();
    }

    void tap(lv_point_t p) { tap(p.x, p.y); }

    /* Ask the builder for a fresh tree without any input, which is what a sensor
     * task or a parent's own state change looks like from here. */
    void rebuild() {
        FmsApp::instance().requestFrame();
        settle();
    }

    void setIdleTimeout(uint32_t timeout_ms) { FmsApp::instance().setIdleTimeout(timeout_ms); }

    void blank() { FmsApp::instance().blankDisplay(); }
    void wake() { FmsApp::instance().wakeDisplay(); }

    uint32_t inactiveTime() const { return lv_display_get_inactive_time(disp_); }

    bool framebufferBlack() const {
        const size_t bytes = static_cast<size_t>(kWidth) * kHeight * 2;
        for (size_t i = 0; i < bytes; i++) {
            if (fb_[i] != 0) return false;
        }
        return true;
    }

    lv_obj_t *screen() const { return lv_display_get_screen_active(disp_); }

    /* Every lv_obj the framework makes is a direct child of the screen, so this
     * is the live object count and not an approximation of it. */
    int32_t liveObjects() const {
        return static_cast<int32_t>(lv_obj_get_child_count(screen()));
    }

private:
    uint8_t *fb_ = nullptr;
    lv_display_t *disp_ = nullptr;
    lv_indev_t *indev_ = nullptr;
};

/* ---- Finding something to tap ------------------------------------------ */

/* Coordinates are read off the screen rather than written into the test.
 *
 * A hard-coded (412, 96) is a number nobody can check and that a change to the
 * theme's row height silently invalidates -- the tap keeps landing, just not on
 * what the test names.  Labels are flat children of the screen at absolute
 * positions, so asking LVGL where the text is gives the same answer the layout
 * gave, whatever the metrics are. */
lv_obj_t *labelNamed(const Session &s, const char *text) {
    lv_obj_t *screen = s.screen();
    lv_obj_t *found = nullptr;
    int matches = 0;

    const uint32_t n = lv_obj_get_child_count(screen);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(screen, i);
        if (!lv_obj_check_type(child, &lv_label_class)) continue;
        const char *t = lv_label_get_text(child);
        if (t != nullptr && std::strcmp(t, text) == 0) {
            found = child;
            matches++;
        }
    }

    /* Ambiguity is a failure, not a coin toss.  Two labels reading the same
     * thing means the test is about to tap whichever one it happened to find
     * last, and would keep passing while checking the wrong control. */
    if (matches > 1) {
        g_failures++;
        std::printf("  FAIL label \"%s\" appears %d times; the test cannot mean one of them\n",
                    text, matches);
        return nullptr;
    }
    return found;
}

bool showing(const Session &s, const char *text) { return labelNamed(s, text) != nullptr; }

/* The middle of a label is inside the control that owns it, which is what we
 * actually want to hit: the label itself is inert, so LVGL's hit test walks
 * past it to the GestureDetector underneath. */
lv_point_t centerOf(lv_obj_t *o) {
    return lv_point_t{lv_obj_get_x(o) + lv_obj_get_width(o) / 2,
                      lv_obj_get_y(o) + lv_obj_get_height(o) / 2};
}

/* Tap whatever is under a label, by its text. */
void tapLabel(Session &s, const char *text) {
    lv_obj_t *label = labelNamed(s, text);
    if (label == nullptr) {
        g_failures++;
        std::printf("  FAIL nothing on screen reads \"%s\"\n", text);
        return;
    }
    s.tap(centerOf(label));
}

bool textIs(lv_obj_t *label, const char *want) {
    if (label == nullptr) return false;
    const char *t = lv_label_get_text(label);
    return t != nullptr && std::strcmp(t, want) == 0;
}

bool colorIs(lv_obj_t *o, Color want) {
    if (o == nullptr) return false;
    const lv_color_t got = lv_obj_get_style_text_color(o, LV_PART_MAIN);
    return lv_color_eq(got, lv_color_make(want.r, want.g, want.b));
}

/* ---- Display blanking -------------------------------------------------- */

FmsThemeData testTheme();
Widget *themed(Widget *child);

class BlankStateWidget : public StatefulWidget {
public:
    BlankStateWidget() = default;
    FMSUI_WIDGET(BlankStateWidget)
    StateBase *createState() const override;
};

class BlankState : public State<BlankStateWidget> {
public:
    Widget *build(BuildContext &) override {
        return new GestureDetector{{
            .on_tap = [this] {
                setState([&] { value_++; });
            },
            .child = new SizedBox{{
                .width = 300,
                .height = 120,
                .child = new Text{{.text = fmt("STATE %d", value_)}},
            }},
        }};
    }

private:
    int value_ = 0;
};

StateBase *BlankStateWidget::createState() const { return new BlankState(); }

void test_display_api_is_safe_before_init() {
    std::printf("display: calls before init are harmless\n");

    FmsApp &app = FmsApp::instance();
    app.shutdown();
    app.setIdleTimeout(0);
    app.setOutputOffCallback({});
    app.setOutputOnCallback({});
    app.blankDisplay();
    app.wakeDisplay();
    app.notifyUserActivity();

    CHECK(!app.isDisplayBlanked());
    CHECK_EQ(app.idleTimeout(), 0);
}

void test_idle_timeout_waits_for_deadline_and_any_touch_resets_it() {
    std::printf("display: timeout uses display inactivity, including an empty-screen tap\n");

    Session s([&] { return themed(new Center(new Text{{.text = "IDLE"}})); });
    s.setIdleTimeout(300);

    s.step(4);
    CHECK(!FmsApp::instance().isDisplayBlanked());

    /* (4,4) is deliberately not inside a clickable Widget. LVGL still records
     * the pointer press as display activity, which is the contract we need. */
    s.tap(4, 4);
    CHECK(s.inactiveTime() < 300);
    CHECK(!FmsApp::instance().isDisplayBlanked());

    s.step(4);
    CHECK(!FmsApp::instance().isDisplayBlanked());
    s.step(2);
    CHECK(FmsApp::instance().isDisplayBlanked());
}

void test_blank_and_wake_wait_for_refresh_and_absorb_first_tap() {
    std::printf("display: black frame, callbacks and wake-tap absorption\n");

    int key_taps = 0;
    int output_off = 0;
    int output_on = 0;
    bool black_at_output_off = false;

    Session s([&] {
        return themed(new FmsButton{{
            .text = "TARGET",
            .on_tap = [&] { key_taps++; },
        }});
    });
    FmsApp &app = FmsApp::instance();
    app.setIdleTimeout(0);
    app.setOutputOffCallback([&] {
        output_off++;
        black_at_output_off = s.framebufferBlack();
    });
    app.setOutputOnCallback([&] { output_on++; });

    s.blank();
    CHECK(app.isDisplayBlanked());
    CHECK_EQ(output_off, 0);  // blanking is not complete until refresh
    s.step(2);
    CHECK(app.isDisplayBlanked());
    CHECK_EQ(output_off, 1);
    CHECK(black_at_output_off);

    s.blank();
    s.step(2);
    CHECK_EQ(output_off, 1);  // repeated request is idempotent

    /* The label remains in the preserved tree, but the full-screen overlay is
     * the actual hit target. Its first tap wakes only. */
    app.setIdleTimeout(1000);
    tapLabel(s, "TARGET");
    CHECK_EQ(key_taps, 0);
    CHECK_EQ(output_on, 1);
    CHECK(!app.isDisplayBlanked());
    CHECK(s.inactiveTime() < 1000);
    CHECK(!s.framebufferBlack());

    /* The next real tap reaches the old detector and its callback. */
    tapLabel(s, "TARGET");
    CHECK_EQ(key_taps, 1);

    app.wakeDisplay();
    s.step(2);
    CHECK_EQ(output_on, 1);  // already awake
}

void test_explicit_wake_resets_idle_deadline() {
    std::printf("display: explicit wake restarts the idle deadline\n");

    Session s([&] { return themed(new Center(new Text{{.text = "WAKE"}})); });
    FmsApp &app = FmsApp::instance();
    app.setIdleTimeout(100);

    s.step(4);
    CHECK(app.isDisplayBlanked());

    app.wakeDisplay();
    s.step(2);
    CHECK(!app.isDisplayBlanked());
    CHECK(s.inactiveTime() < 100);

    /* The next Awake frame must not immediately enter Blanking again. */
    s.step();
    CHECK(!app.isDisplayBlanked());
}

void test_shutdown_restores_output_after_blank() {
    std::printf("display: shutdown restores an output disabled by blanking\n");

    int output_off = 0;
    int output_on = 0;
    {
        Session s([&] { return themed(new Center(new Text{{.text = "SHUTDOWN"}})); });
        FmsApp &app = FmsApp::instance();
        app.setOutputOffCallback([&] { output_off++; });
        app.setOutputOnCallback([&] { output_on++; });

        s.blank();
        s.step(2);
        CHECK_EQ(output_off, 1);
        CHECK_EQ(output_on, 0);

        app.shutdown();
        CHECK_EQ(output_on, 1);
        CHECK(!app.isDisplayBlanked());
    }

    output_off = 0;
    output_on = 0;
    {
        Session s([&] { return themed(new Center(new Text{{.text = "WAKING"}})); });
        FmsApp &app = FmsApp::instance();
        app.setOutputOffCallback([&] { output_off++; });
        app.setOutputOnCallback([&] { output_on++; });

        s.blank();
        s.step(2);
        CHECK_EQ(output_off, 1);
        app.wakeDisplay();
        CHECK(app.isDisplayBlanked());

        app.shutdown();
        CHECK_EQ(output_on, 1);
        CHECK(!app.isDisplayBlanked());
    }
}

void test_state_and_latest_request_survive_blank_wake() {
    std::printf("display: State and a queued request survive blanking\n");

    std::string text = "OLD";
    Session s([&] { return themed(new Center(new Text{{.text = Str(text)}})); });

    s.blank();
    s.step(2);
    CHECK(showing(s, "OLD"));

    text = "NEW";
    FmsApp::instance().requestFrame();
    s.step(2);  // blanking must not consume or paint the request
    CHECK(showing(s, "OLD"));

    s.wake();
    s.step(3);
    CHECK(!FmsApp::instance().isDisplayBlanked());
    CHECK(showing(s, "NEW"));
}

void test_state_object_is_not_recreated_by_blank_wake() {
    std::printf("display: State identity survives blank/wake\n");

    Session s([&] { return themed(new BlankStateWidget()); });
    tapLabel(s, "STATE 0");
    CHECK(showing(s, "STATE 1"));

    s.blank();
    s.step(2);
    s.wake();
    s.step(3);
    CHECK(showing(s, "STATE 1"));

    /* A wake does not dispose/recreate the State. The same callback still owns
     * its counter and can advance it on the next ordinary tap. */
    tapLabel(s, "STATE 1");
    CHECK(showing(s, "STATE 2"));
}

void test_blank_wake_shutdown_and_reinit() {
    std::printf("display: blank/wake/shutdown can be repeated across init\n");

    {
        Session s([&] { return themed(new Center(new Text{{.text = "FIRST"}})); });
        s.blank();
        s.step(2);
        s.wake();
        s.step(2);
    }

    Session s([&] { return themed(new Center(new Text{{.text = "SECOND"}})); });
    CHECK(!FmsApp::instance().isDisplayBlanked());
    CHECK(showing(s, "SECOND"));
}

/* ---- The theme these tests build against ------------------------------- */

FmsThemeData testTheme() {
    FmsThemeData data;
    data.color = defaultPalette();
    data.font = FmsTypography{
        .unit = &fms_b612_mono_16,
        .label = &fms_b612_mono_20,
        .body = &fms_b612_mono_24,
        .value = &fms_b612_mono_28,
        .title = &fms_b612_mono_32,
    };
    data.metric = defaultMetrics();
    return data;
}

Widget *themed(Widget *child) {
    return new FmsTheme{{.data = testTheme(), .child = child}};
}

/* A block of colour with a name on it, big enough to aim at. */
Widget *panel(const char *name, Color fill) {
    return new Container{{
        .color = fill,
        .alignment = Alignment::center(),
        .child = new Text{{.text = name, .color = Color::rgb(0xFFFFFF)}},
    }};
}

/* ---- GestureDetector --------------------------------------------------- */

void test_a_tap_is_one_press_one_release_and_one_tap() {
    std::printf("gesture: a single tap fires each callback exactly once\n");

    int presses = 0;
    int releases = 0;
    int taps = 0;

    Session s([&] {
        return themed(new Center(new GestureDetector{{
            .on_tap = [&] { taps++; },
            .on_press = [&] { presses++; },
            .on_release = [&] { releases++; },
            .child = new SizedBox{{.width = 300,
                                   .height = 120,
                                   .child = panel("TARGET", Color::rgb(0x2A3038))}},
        }}));
    });

    CHECK_EQ(presses, 0);
    CHECK_EQ(taps, 0);

    tapLabel(s, "TARGET");

    CHECK_EQ(presses, 1);
    CHECK_EQ(releases, 1);
    CHECK_EQ(taps, 1);

    /* And a second tap is a second one of each, not a doubling or a latch. */
    tapLabel(s, "TARGET");
    CHECK_EQ(presses, 2);
    CHECK_EQ(releases, 2);
    CHECK_EQ(taps, 2);

    /* Somewhere the detector is not: the tree is not listening there. */
    s.tap(4, 4);
    CHECK_EQ(presses, 2);
    CHECK_EQ(taps, 2);
}

void test_a_detector_without_callbacks_lets_the_touch_through() {
    std::printf("gesture: an empty detector on top does not swallow the tap\n");

    int front = 0;
    int behind = 0;
    bool front_listens = false;

    /* Two detectors covering the same rectangle.  The second child of a Stack is
     * painted after the first, so `front` is the one a finger meets first --
     * which is the arrangement that matters, because a dropdown's barrier and a
     * disabled button both end up in exactly this position. */
    Session s([&] {
        return themed(new Stack{{
            .alignment = Alignment::center(),
            .children = {
                new GestureDetector{{
                    .on_tap = [&] { behind++; },
                    .child = new SizedBox{{.width = 400,
                                           .height = 200,
                                           .child = panel("BEHIND", Color::rgb(0x203040))}},
                }},
                new GestureDetector{{
                    .on_tap = front_listens ? VoidCallback([&] { front++; }) : VoidCallback{},
                    .child = new SizedBox{{.width = 400, .height = 200}},
                }},
            },
        }});
    });

    const int32_t objects = s.liveObjects();

    /* The front detector is there, sized and positioned over the target, and has
     * nothing to call.  It must be as invisible to the touch as it is to the
     * eye. */
    tapLabel(s, "BEHIND");
    CHECK_EQ(behind, 1);
    CHECK_EQ(front, 0);

    /* Give it a callback and it takes the touch it is now entitled to. */
    front_listens = true;
    s.rebuild();
    CHECK_EQ(s.liveObjects(), objects);  // a callback appearing is not a new shape

    tapLabel(s, "BEHIND");
    CHECK_EQ(front, 1);
    CHECK_EQ(behind, 1);  // and the one underneath hears nothing

    /* Take it away again and the touch goes back through.  This is the case that
     * used to be handled by dropping the detector from the tree entirely, which
     * cost three lv_objs to rebuild for what is a callback going null. */
    front_listens = false;
    s.rebuild();
    CHECK_EQ(s.liveObjects(), objects);

    tapLabel(s, "BEHIND");
    CHECK_EQ(behind, 2);
    CHECK_EQ(front, 1);  // the callback it no longer has is not called
}

void test_toggling_callbacks_never_calls_a_stale_one() {
    std::printf("gesture: a rebuilt tree calls this frame's callback, not last frame's\n");

    /* Each rebuild hands the detector a freshly-made lambda closing over the
     * generation that made it.  A RenderObject that kept the old std::function
     * -- or the arena widget it came from, which is reset every pass -- would
     * report a stale generation here, or crash. */
    int generation = 0;
    int called_with = -1;
    int calls = 0;

    Session s([&] {
        const int g = generation;
        return themed(new Center(new GestureDetector{{
            .on_tap =
                [&, g] {
                    called_with = g;
                    calls++;
                },
            .child = new SizedBox{{.width = 300,
                                   .height = 120,
                                   .child = panel("GEN", Color::rgb(0x2A3038))}},
        }}));
    });

    const int32_t objects = s.liveObjects();

    tapLabel(s, "GEN");
    CHECK_EQ(calls, 1);
    CHECK_EQ(called_with, 0);

    for (generation = 1; generation <= 5; generation++) {
        s.rebuild();
        tapLabel(s, "GEN");
        CHECK_EQ(called_with, generation);
    }

    CHECK_EQ(calls, 6);
    CHECK_EQ(s.liveObjects(), objects);  // five rebuilds, same shape
}

/* ---- FmsDropdown ------------------------------------------------------- */

/* Whenever these tests use CRZ as the "popup is up" signal, the closed box is
 * on CLB (the repetition test resets it after selecting CRZ).  A label reading
 * CRZ can therefore only be a row of the open list at each assertion. */
const char *const kModes[] = {"CLB", "CRZ", "DES"};
constexpr size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

/* The dropdown at the top left, with something tappable filling everything
 * below it -- which is where the popup hangs and where the barrier goes. */
struct DropdownPage {
    int index = 0;
    int selections = 0;
    int last_selected = -1;
    int behind = 0;

    Widget *build() {
        return themed(new Column{{
            .cross = CrossAxis::Start,
            .children = {
                new FmsDropdown{{
                    .items = kModes,
                    .count = kModeCount,
                    .index = index,
                    .text = {},
                    .on_selected =
                        [this](int i) {
                            selections++;
                            last_selected = i;
                            index = i;
                        },
                }},
                new Expanded{{
                    .child = new GestureDetector{{
                        .on_tap = [this] { behind++; },
                        .child = panel("BEHIND", Color::rgb(0x101820)),
                    }},
                }},
            },
        }});
    }
};

void test_a_dropdown_opens_and_reports_the_row_that_was_tapped() {
    std::printf("dropdown: tap to open, tap a row to select and close\n");

    DropdownPage page;
    Session s([&] { return page.build(); });

    CHECK(!showing(s, "CRZ"));  // closed: the list is not in the tree at all

    tapLabel(s, "CLB");  // the box shows the selection; tapping it opens
    CHECK(showing(s, "CRZ"));
    CHECK_EQ(page.selections, 0);  // opening is not choosing

    tapLabel(s, "DES");
    CHECK_EQ(page.selections, 1);
    CHECK_EQ(page.last_selected, 2);
    CHECK(!showing(s, "CRZ"));  // and picking closes it
    CHECK_EQ(page.behind, 0);   // the row tap did not also reach what it covered

    /* The box now reads what was chosen, so the selection made it back out
     * through the parent's state and into the next build. */
    CHECK(showing(s, "DES"));
    CHECK(!showing(s, "CLB"));
}

void test_the_barrier_closes_without_selecting_or_leaking_the_touch() {
    std::printf("dropdown: the barrier dismisses, and nothing behind it hears\n");

    DropdownPage page;
    Session s([&] { return page.build(); });

    lv_obj_t *behind_label = labelNamed(s, "BEHIND");
    CHECK(behind_label != nullptr);
    const lv_point_t away = centerOf(behind_label);

    /* First establish that there really is a live target under that point --
     * otherwise "the barrier did not leak" would pass on a tap that had nothing
     * to leak to. */
    s.tap(away);
    CHECK_EQ(page.behind, 1);

    tapLabel(s, "CLB");
    CHECK(showing(s, "CRZ"));

    s.tap(away);
    CHECK(!showing(s, "CRZ"));    // dismissed
    CHECK_EQ(page.selections, 0);  // without choosing anything
    CHECK_EQ(page.behind, 1);      // and the button under the barrier never fired

    /* Once dismissed the barrier is gone too, so the same point works again. */
    s.tap(away);
    CHECK_EQ(page.behind, 2);
}

void test_a_dropdown_without_items_is_just_a_button() {
    std::printf("dropdown: no items means no popup, only on_tap\n");

    int taps = 0;
    Session s([&] {
        return themed(new Column{{
            .cross = CrossAxis::Start,
            .children = {new FmsDropdown{{
                .text = "ACTIVE",
                .on_selected = {},
                .on_tap = [&] { taps++; },
            }}},
        }});
    });

    const int32_t objects = s.liveObjects();

    tapLabel(s, "ACTIVE");
    CHECK_EQ(taps, 1);
    /* This is the menu-bar form -- ACTIVE, POSITION, SEC INDEX -- and it must
     * not quietly grow a list it has no items for. */
    CHECK_EQ(s.liveObjects(), objects);

    tapLabel(s, "ACTIVE");
    CHECK_EQ(taps, 2);
    CHECK_EQ(s.liveObjects(), objects);
}

void test_opening_and_closing_a_dropdown_returns_to_its_baseline() {
    std::printf("dropdown: repeated open/close settles at a fixed object and style count\n");

    DropdownPage page;
    Session s([&] { return page.build(); });

    /* The box's own label, captured while closed so there is only one CLB on the
     * screen to capture.  Opening the list must not rebuild the control the list
     * hangs from: that is three lv_objs destroyed and remade on the way open and
     * three more on the way shut, per interaction, for a box that did not
     * change. */
    lv_obj_t *box_label = labelNamed(s, "CLB");
    CHECK(box_label != nullptr);

    const int32_t closed_objects = s.liveObjects();

    /* One full cycle first, so every look the popup needs has been made.  The
     * claim being tested is that repetition is free, not that the first one
     * is. */
    tapLabel(s, "CLB");
    tapLabel(s, "CRZ");  // the barrier is the only other way out; use a row
    CHECK_EQ(page.last_selected, 1);
    page.index = 0;      // back to CLB so the labels below stay unambiguous
    s.rebuild();

    CHECK_EQ(s.liveObjects(), closed_objects);
    const StyleCacheStats settled = styleCacheStats();

    for (int i = 0; i < 8; i++) {
        tapLabel(s, "CLB");
        CHECK(showing(s, "CRZ"));
        const int32_t open_objects = s.liveObjects();

        tapLabel(s, "CRZ");
        page.index = 0;
        s.rebuild();

        CHECK(!showing(s, "CRZ"));
        CHECK(open_objects > closed_objects);          // the popup was really there
        CHECK_EQ(s.liveObjects(), closed_objects);     // ...and is really gone
    }

    const StyleCacheStats after = styleCacheStats();
    CHECK_EQ(after.text_styles, settled.text_styles);
    CHECK_EQ(after.box_styles, settled.box_styles);

    /* Same lv_obj, eight cycles later: the box was updated, never rebuilt. */
    CHECK(labelNamed(s, "CLB") == box_label);
}

/* ---- FmsKeypad / FmsScratchpad ----------------------------------------- */

/* A parent that owns the entry, because the scratchpad does not: it is handed
 * text, a message and an error flag, and shows one of them.  Making the test's
 * page hold that state is not a shortcut -- it is the arrangement every real
 * page uses, and it is what puts setState() in the path of every key. */
class EntryPage : public StatefulWidget {
public:
    struct Counts {
        int keys = 0;
        int backspaces = 0;
        int clears = 0;
        int enters = 0;
        char last_key = '\0';
    };

    explicit EntryPage(Counts *counts, bool letters) : counts_(counts), letters_(letters) {}
    FMSUI_WIDGET(EntryPage)
    StateBase *createState() const override;

    Counts *counts_;
    bool letters_;
};

class EntryPageState : public State<EntryPage> {
public:
    Widget *build(BuildContext &ctx) override {
        (void)FmsTheme::of(ctx);
        EntryPage::Counts *counts = widget().counts_;

        return new Column{{
            .cross = CrossAxis::Stretch,
            .spacing = 12,
            .children = {
                new FmsScratchpad{{
                    .text = Str(typed_),
                    .message = Str(message_),
                    .error = error_,
                }},
                new FmsKeypad{{
                    .on_key =
                        [this, counts](char c) {
                            counts->keys++;
                            counts->last_key = c;
                            setState([&] {
                                typed_ += c;
                                message_.clear();  // typing dismisses the last advisory
                                error_ = false;
                            });
                        },
                    .on_backspace =
                        [this, counts] {
                            counts->backspaces++;
                            setState([&] {
                                if (!typed_.empty()) typed_.pop_back();
                            });
                        },
                    .on_clear =
                        [this, counts] {
                            counts->clears++;
                            setState([&] {
                                typed_.clear();
                                message_.clear();
                                error_ = false;
                            });
                        },
                    .on_enter =
                        [this, counts] {
                            counts->enters++;
                            setState([&] {
                                /* An empty entry is the error case every FMS
                                 * page has, and the one that makes the
                                 * scratchpad go amber. */
                                error_ = typed_.empty();
                                message_ = error_ ? "NO ENTRY" : "ENTRY ACCEPTED";
                                typed_.clear();
                            });
                        },
                    .letters = widget().letters_,
                }},
            },
        }};
    }

private:
    std::string typed_;
    std::string message_;
    bool error_ = false;
};

StateBase *EntryPage::createState() const { return new EntryPageState(); }

void test_the_keypad_delivers_the_character_that_was_pressed() {
    std::printf("keypad: digits and letters both hand on the key that was hit\n");

    EntryPage::Counts counts;
    bool letters = false;
    Session s([&] { return themed(new EntryPage(&counts, letters)); });

    /* Digits.  Each is looked up by its own face, so the test asserts the key
     * that was aimed at and the character that came out are the same one --
     * which a hard-coded coordinate could not. */
    const char *const digits[] = {"7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "/"};
    for (const char *face : digits) {
        tapLabel(s, face);
        CHECK(counts.last_key == face[0]);
    }
    CHECK_EQ(counts.keys, 12);

    /* And the same keypad in its letter arrangement.  A fresh page, because the
     * digits above are still in the scratchpad. */
    letters = true;
    s.rebuild();

    tapLabel(s, "CLR");
    const char *const chars[] = {"A", "G", "H", "N", "O", "U", "V", "Z", "-"};
    for (const char *face : chars) {
        tapLabel(s, face);
        CHECK(counts.last_key == face[0]);
    }
    CHECK_EQ(counts.keys, 21);
    CHECK_EQ(counts.backspaces, 0);
    CHECK_EQ(counts.clears, 1);
    CHECK_EQ(counts.enters, 0);
}

void test_del_clr_and_enter_each_call_only_their_own() {
    std::printf("keypad: DEL, CLR and ENTER do one thing each\n");

    EntryPage::Counts counts;
    Session s([&] { return themed(new EntryPage(&counts, false)); });

    tapLabel(s, "7");
    tapLabel(s, "8");
    tapLabel(s, "9");
    CHECK_EQ(counts.keys, 3);

    tapLabel(s, "DEL");
    CHECK_EQ(counts.backspaces, 1);
    CHECK_EQ(counts.clears, 0);
    CHECK_EQ(counts.enters, 0);
    CHECK_EQ(counts.keys, 3);  // DEL is not a character

    tapLabel(s, "CLR");
    CHECK_EQ(counts.clears, 1);
    CHECK_EQ(counts.backspaces, 1);
    CHECK_EQ(counts.enters, 0);
    CHECK_EQ(counts.keys, 3);

    tapLabel(s, "ENTER");
    CHECK_EQ(counts.enters, 1);
    CHECK_EQ(counts.clears, 1);
    CHECK_EQ(counts.backspaces, 1);
    CHECK_EQ(counts.keys, 3);
}

void test_the_scratchpad_shows_entry_then_message_then_error() {
    std::printf("scratchpad: entry, advisory and error are three different readings\n");

    const FmsThemeData theme = testTheme();
    EntryPage::Counts counts;
    Session s([&] { return themed(new EntryPage(&counts, false)); });

    /* Nothing typed: ENTER is the error case, and the scratchpad has to say so
     * in the colour as well as the words -- amber is the whole point of it. */
    tapLabel(s, "ENTER");
    lv_obj_t *pad = labelNamed(s, "NO ENTRY");
    CHECK(textIs(pad, "NO ENTRY"));
    CHECK(colorIs(pad, theme.color.attention));

    /* Typing takes the message away and puts the entry back, in entry cyan. */
    tapLabel(s, "1");
    tapLabel(s, "2");
    tapLabel(s, "3");
    pad = labelNamed(s, "123");
    CHECK(textIs(pad, "123"));
    CHECK(colorIs(pad, theme.color.entry));
    CHECK(!showing(s, "NO ENTRY"));

    tapLabel(s, "DEL");
    CHECK(textIs(labelNamed(s, "12"), "12"));

    /* A real entry: the advisory is not an error, so it reads in label white and
     * not in amber.  Getting these two the same way round is the difference
     * between "here is what you asked for" and "something is wrong". */
    tapLabel(s, "ENTER");
    pad = labelNamed(s, "ENTRY ACCEPTED");
    CHECK(textIs(pad, "ENTRY ACCEPTED"));
    CHECK(colorIs(pad, theme.color.label));
    CHECK(!colorIs(pad, theme.color.attention));

    tapLabel(s, "CLR");
    CHECK(!showing(s, "ENTRY ACCEPTED"));
}

}  // namespace

int main() {
    lv_init();

    test_display_api_is_safe_before_init();
    test_idle_timeout_waits_for_deadline_and_any_touch_resets_it();
    test_blank_and_wake_wait_for_refresh_and_absorb_first_tap();
    test_explicit_wake_resets_idle_deadline();
    test_shutdown_restores_output_after_blank();
    test_state_and_latest_request_survive_blank_wake();
    test_state_object_is_not_recreated_by_blank_wake();
    test_blank_wake_shutdown_and_reinit();

    test_a_tap_is_one_press_one_release_and_one_tap();
    test_a_detector_without_callbacks_lets_the_touch_through();
    test_toggling_callbacks_never_calls_a_stale_one();

    test_a_dropdown_opens_and_reports_the_row_that_was_tapped();
    test_the_barrier_closes_without_selecting_or_leaking_the_touch();
    test_a_dropdown_without_items_is_just_a_button();
    test_opening_and_closing_a_dropdown_returns_to_its_baseline();

    test_the_keypad_delivers_the_character_that_was_pressed();
    test_del_clr_and_enter_each_call_only_their_own();
    test_the_scratchpad_shows_entry_then_message_then_error();

    /* Every Session shut its app down and released the style cache on the way
     * out, so there is nothing of ours left to hand back here. */
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
