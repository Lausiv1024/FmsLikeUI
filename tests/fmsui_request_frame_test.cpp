/* requestFrame() under load: many requests, several producers, one frame loop.
 *
 * requestFrame() is not a queue.  It says "read your inputs again next frame",
 * so any number of requests that arrive before the UI gets round to them are
 * allowed to become one build.  That makes "builds == requests" the wrong thing
 * to test -- it would call the coalescing a bug.  What these tests hold the
 * frame loop to instead:
 *
 *   - it never builds more often than it was asked to
 *   - a request that lands while a build is running is picked up by the next one
 *   - what a build reads never goes backwards
 *   - once the producers stop, the last thing they published is what gets built
 *   - after that the flag is down and idle frames build nothing
 *
 * Nothing here waits in wall-clock time or passes on "fast enough".  Frames run
 * on a fake clock, and where two threads have to meet -- a producer in the
 * middle of a build, a producer that must not finish before the UI has built
 * anything -- they meet at a condition variable or an atomic wait.  A deadlock
 * is therefore a hang rather than a slow pass, and CTest's timeout on this
 * executable is what turns it into a failure.
 *
 * Separate executable from the other two for the same reason they are separate
 * from each other: a thread test that hangs or crashes should take nothing else
 * down with it, and should read as "the frame request path broke".
 *
 * What this cannot show: FreeRTOS scheduling, the IRAM interrupt path, or any
 * latency or frame rate on the device.  A clean run here is evidence about the
 * coalescing, the publish-then-request order and convergence on the host -- it
 * is not a proof that there is no data race.  A single producer gets a
 * publication edge when the frame loop acquires its release request.  The
 * multi-producer case keeps every payload atomic because one coalesced flag
 * cannot publish every producer's unrelated data.  Only the frame loop touches
 * the tree.
 */

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
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

void checkInt(long got, long want, const char *what, const char *file, int line) {
    g_checks++;
    if (got == want) return;
    g_failures++;
    std::printf("  FAIL %s:%d: %s = %ld, want %ld\n", file, line, what, got, want);
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(got, want) checkInt((got), (want), #got, __FILE__, __LINE__)

/* ---- The load ---------------------------------------------------------- */

/* Fixed, so a run under ASan does the same work as a plain one.  100,000
 * requests in all across the three loaded cases. */
constexpr uint32_t kBurstRequests = 10000;
constexpr uint32_t kStreamRequests = 40000;
constexpr uint32_t kProducers = 4;
constexpr uint32_t kRequestsPerProducer = 12500;

/* Handshakes, not load: each round is two requests. */
constexpr uint32_t kMidBuildRounds = 1000;

/* A producer stops at every multiple of this until a build has read it.  See
 * waitForABuildThatRead(). */
constexpr uint32_t kCheckpoint = 1000;

/* How long "nothing more happens" has to hold after a drain. */
constexpr uint32_t kIdleFrames = 32;

/* ---- A session --------------------------------------------------------- */

/* Time is ours, not the wall clock's.  kFrameMs is the app's own timer period,
 * so every step runs FmsApp's frame exactly once. */
uint32_t g_micros = 0;
uint32_t fakeMicros() { return g_micros; }

constexpr int32_t kWidth = 320;   /* small: the screen is one label, and the */
constexpr int32_t kHeight = 240;  /* UI thread spins frames for as long as the load runs */
constexpr uint32_t kFrameMs = 10;

ThreadId hostThreadId() {
    return static_cast<ThreadId>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

/* One display and one app, torn down app first.
 *
 * The thread identity is installed so that the frame loop binds to this thread:
 * a producer that reached for setState() instead of requestFrame() would then
 * trip the assert in markNeedsBuild() rather than race the build quietly. */
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

        FmsApp &app = FmsApp::instance();
        app.setClock(fakeMicros);
        app.setThreadId(hostThreadId);
        app.init(disp_, std::move(builder));

        step();  // the build init() asked for
        baseline_objects_ = liveObjects();
    }

    ~Session() {
        FmsApp::instance().shutdown();
        lv_display_delete(disp_);
        std::free(fb_);
    }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    void step(uint32_t frames = 1) {
        for (uint32_t i = 0; i < frames; i++) {
            g_micros += kFrameMs * 1000;
            lv_tick_inc(kFrameMs);
            lv_timer_handler();
        }
    }

    /* The running total, which carries on across sessions -- the singleton is
     * reused -- so every test works in differences from a reading it took. */
    static uint32_t builds() { return FmsApp::instance().stats().builds; }

    int32_t liveObjects() const {
        return static_cast<int32_t>(lv_obj_get_child_count(lv_display_get_screen_active(disp_)));
    }
    int32_t baselineObjects() const { return baseline_objects_; }

    /* What is on the screen, as opposed to what the builder read: the value has
     * to get through layout and paint as well to count as having arrived. */
    bool shows(uint32_t value) const {
        char want[16];
        std::snprintf(want, sizeof(want), "%u", value);

        lv_obj_t *screen = lv_display_get_screen_active(disp_);
        const uint32_t n = lv_obj_get_child_count(screen);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t *child = lv_obj_get_child(screen, i);
            if (!lv_obj_check_type(child, &lv_label_class)) continue;
            const char *t = lv_label_get_text(child);
            if (t != nullptr && std::strcmp(t, want) == 0) return true;
        }
        return false;
    }

private:
    uint8_t *fb_ = nullptr;
    lv_display_t *disp_ = nullptr;
    int32_t baseline_objects_ = 0;
};

/* ---- Meeting points ---------------------------------------------------- */

/* Holds producers until the frame loop is ready to run against them, so the load
 * starts on both sides at once instead of the producers getting a head start. */
class StartGate {
public:
    void open() {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            open_ = true;
        }
        cv_.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return open_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_ = false;
};

/* Block a producer until some build has read `gen` or later.
 *
 * Without this a loaded CI box can let a producer run all of its requests
 * between two frames, and the test passes having never had a producer and a
 * build overlap at all.  Stopping at every kCheckpoint means each one needs a
 * build that started after it was published, so a run of N requests is
 * guaranteed at least N / kCheckpoint builds in the middle of it.
 *
 * The waiting is the test's, not requestFrame()'s: the request has already
 * been made, and returned, by the time a producer gets here. */
void waitForABuildThatRead(const std::atomic<uint32_t> &seen, uint32_t gen) {
    uint32_t v = seen.load(std::memory_order_acquire);
    while (v < gen) {
        seen.wait(v, std::memory_order_acquire);
        v = seen.load(std::memory_order_acquire);
    }
}

/* ---- Convergence ------------------------------------------------------- */

struct Drain {
    uint32_t pickup_builds = 0;  /* the frame that took what the producers left */
    uint32_t idle_builds = 0;    /* every frame after it -- has to be none */
};

/* With every producer stopped there is at most one request outstanding, so one
 * frame finishes the work and the flag is down after it.  The idle frames are
 * what show that: each of them would take the flag if anything had set it. */
Drain drainThenIdle(Session &s) {
    Drain d;
    const uint32_t before = Session::builds();
    s.step();
    const uint32_t after_pickup = Session::builds();
    s.step(kIdleFrames);
    d.pickup_builds = after_pickup - before;
    d.idle_builds = Session::builds() - after_pickup;
    return d;
}

/* ---- 1. A burst while the UI is not running ---------------------------- */

void test_a_burst_while_the_ui_is_stopped_is_one_build() {
    std::printf("burst: %u requests before the UI looks become one build\n", kBurstRequests);

    std::atomic<uint32_t> published{0};
    uint32_t last_read = 0;  // the UI thread's
    uint32_t builder_calls = 0;

    Session s([&] {
        builder_calls++;
        last_read = published.load(std::memory_order_relaxed);
        return new Text{{.text = fmt("%u", last_read)}};
    });

    const uint32_t before = Session::builds();
    const uint32_t calls_before = builder_calls;

    std::thread producer([&] {
        for (uint32_t gen = 1; gen <= kBurstRequests; gen++) {
            published.store(gen, std::memory_order_relaxed);
            FmsApp::instance().requestFrame();
        }
    });
    producer.join();

    /* Nobody has run a frame, so nobody has built. */
    CHECK_EQ(Session::builds() - before, 0);

    const Drain d = drainThenIdle(s);
    CHECK_EQ(d.pickup_builds, 1);
    CHECK_EQ(d.idle_builds, 0);
    CHECK_EQ(builder_calls - calls_before, 1);
    CHECK(Session::builds() - before <= kBurstRequests);

    CHECK_EQ(last_read, kBurstRequests);
    CHECK(s.shows(kBurstRequests));
}

/* ---- 2. A request that lands while FmsApp is building ------------------ */

void test_a_request_during_a_build_reaches_the_next_frame() {
    std::printf("mid-build: a request made while the builder runs is built next frame "
                "(%u rounds)\n", kMidBuildRounds);

    std::atomic<uint32_t> published{0};

    /* The UI thread's alone. */
    uint32_t last_read = 0;
    bool hold_this_build = false;

    /* The meeting point inside the build. */
    std::mutex mutex;
    std::condition_variable cv;
    bool building = false;
    bool requested = false;
    bool quit = false;

    Session s([&] {
        /* Read first, then let the producer in: this build has already taken the
         * flag and already has its value, so whatever the producer does now can
         * only be seen by a later build. */
        last_read = published.load(std::memory_order_relaxed);
        if (hold_this_build) {
            hold_this_build = false;
            std::unique_lock<std::mutex> lock(mutex);
            building = true;
            cv.notify_all();
            /* If requestFrame() waited on the build in any way, this is where the
             * two threads would stop for good -- and CTest's timeout would say so. */
            cv.wait(lock, [&] { return requested; });
            requested = false;
        }
        return new Text{{.text = fmt("%u", last_read)}};
    });

    std::thread producer([&] {
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return building || quit; });
                if (quit) return;
                building = false;
            }
            published.store(published.load(std::memory_order_relaxed) + 1,
                             std::memory_order_relaxed);
            FmsApp::instance().requestFrame();
            {
                const std::lock_guard<std::mutex> lock(mutex);
                requested = true;
            }
            cv.notify_all();
        }
    });

    /* Counted rather than checked per round, so that a regression prints one
     * line per property and not a thousand. */
    uint32_t first_build_wrong = 0;
    uint32_t request_lost = 0;
    uint32_t wrong_after_idle = 0;

    for (uint32_t round = 1; round <= kMidBuildRounds; round++) {
        const uint32_t before = Session::builds();
        hold_this_build = true;
        FmsApp::instance().requestFrame();  // the UI's own reason to build this frame

        /* Builds once, reads the previous round's value, and is held until the
         * producer has published this round's and asked again. */
        s.step();
        if (Session::builds() - before != 1 || last_read != round - 1) first_build_wrong++;

        /* The request that arrived mid-build: this frame has to build it. */
        s.step();
        if (Session::builds() - before != 2 || last_read != round || !s.shows(round)) {
            request_lost++;
        }

        /* And that was the last of it. */
        s.step();
        if (Session::builds() - before != 2) wrong_after_idle++;
    }

    {
        const std::lock_guard<std::mutex> lock(mutex);
        quit = true;
    }
    cv.notify_all();
    producer.join();

    CHECK_EQ(first_build_wrong, 0);
    CHECK_EQ(request_lost, 0);
    CHECK_EQ(wrong_after_idle, 0);
    CHECK_EQ(last_read, kMidBuildRounds);
    CHECK(s.shows(kMidBuildRounds));

    const Drain d = drainThenIdle(s);
    CHECK_EQ(d.pickup_builds, 0);
    CHECK_EQ(d.idle_builds, 0);
}

/* ---- 3. One producer, streaming ---------------------------------------- */

void test_one_producer_streaming_never_goes_backwards() {
    std::printf("stream: one producer, %u requests, frames running alongside\n",
                kStreamRequests);

    std::atomic<uint32_t> published{0};
    std::atomic<uint32_t> seen{0};  // written by the UI, waited on by the producer

    uint32_t last_read = 0;  // the UI thread's
    uint32_t went_backwards = 0;

    Session s([&] {
        const uint32_t v = published.load(std::memory_order_relaxed);
        if (v < last_read) went_backwards++;
        last_read = v;
        seen.store(v, std::memory_order_release);
        seen.notify_all();
        return new Text{{.text = fmt("%u", v)}};
    });

    const uint32_t before = Session::builds();
    StartGate start;
    std::atomic<bool> finished{false};

    std::thread producer([&] {
        start.wait();
        for (uint32_t gen = 1; gen <= kStreamRequests; gen++) {
            published.store(gen, std::memory_order_relaxed);  // publish...
            FmsApp::instance().requestFrame();                // ...then ask
            /* Not at the very end: the last stretch runs free, so what is left
             * pending when the producer stops is the drain's to pick up. */
            if (gen % kCheckpoint == 0 && gen != kStreamRequests) {
                waitForABuildThatRead(seen, gen);
            }
        }
        finished.store(true, std::memory_order_release);
    });

    start.open();
    while (!finished.load(std::memory_order_acquire)) s.step();
    producer.join();

    const uint32_t under_load = Session::builds() - before;
    const Drain d = drainThenIdle(s);

    std::printf("  %u builds under load, %u to drain\n", under_load, d.pickup_builds);

    CHECK_EQ(went_backwards, 0);
    CHECK(under_load >= kStreamRequests / kCheckpoint - 1);  // the checkpoints forced these
    CHECK(under_load + d.pickup_builds <= kStreamRequests);
    CHECK(d.pickup_builds <= 1);
    CHECK_EQ(d.idle_builds, 0);

    CHECK_EQ(last_read, kStreamRequests);
    CHECK(s.shows(kStreamRequests));
    CHECK_EQ(s.liveObjects(), s.baselineObjects());
}

/* ---- 4. Several producers at once -------------------------------------- */

void test_several_producers_all_arrive_and_the_loop_drains() {
    std::printf("producers: %u threads x %u requests against one frame loop\n", kProducers,
                kRequestsPerProducer);

    /* Each producer owns a generation of its own, so each one's order can be
     * checked; `total` is shared, and is the one the screen shows. */
    std::array<std::atomic<uint32_t>, kProducers> published{};
    std::array<std::atomic<uint32_t>, kProducers> seen{};
    std::atomic<uint32_t> total{0};

    /* The UI thread's. */
    std::array<uint32_t, kProducers> last_read{};
    uint32_t last_total = 0;
    uint32_t went_backwards = 0;

    Session s([&] {
        for (uint32_t p = 0; p < kProducers; p++) {
            const uint32_t v = published[p].load(std::memory_order_relaxed);
            if (v < last_read[p]) went_backwards++;
            last_read[p] = v;
            seen[p].store(v, std::memory_order_release);
            seen[p].notify_all();
        }
        const uint32_t t = total.load(std::memory_order_relaxed);
        if (t < last_total) went_backwards++;
        last_total = t;
        return new Text{{.text = fmt("%u", t)}};
    });

    const uint32_t before = Session::builds();
    StartGate start;
    std::atomic<uint32_t> running{kProducers};

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (uint32_t p = 0; p < kProducers; p++) {
        producers.emplace_back([&, p] {
            start.wait();
            for (uint32_t gen = 1; gen <= kRequestsPerProducer; gen++) {
                published[p].store(gen, std::memory_order_relaxed);
                total.fetch_add(1, std::memory_order_relaxed);
                FmsApp::instance().requestFrame();
                if (gen % kCheckpoint == 0 && gen != kRequestsPerProducer) {
                    waitForABuildThatRead(seen[p], gen);
                }
            }
            running.fetch_sub(1, std::memory_order_release);
        });
    }

    start.open();
    while (running.load(std::memory_order_acquire) != 0) s.step();
    for (std::thread &t : producers) t.join();

    const uint32_t under_load = Session::builds() - before;
    const Drain d = drainThenIdle(s);
    const uint32_t all_requests = kProducers * kRequestsPerProducer;

    std::printf("  %u builds under load, %u to drain\n", under_load, d.pickup_builds);

    CHECK_EQ(went_backwards, 0);
    CHECK(under_load >= kRequestsPerProducer / kCheckpoint - 1);
    CHECK(under_load + d.pickup_builds <= all_requests);
    CHECK(d.pickup_builds <= 1);
    CHECK_EQ(d.idle_builds, 0);

    for (uint32_t p = 0; p < kProducers; p++) CHECK_EQ(last_read[p], kRequestsPerProducer);
    CHECK_EQ(last_total, all_requests);
    CHECK(s.shows(all_requests));
    CHECK_EQ(s.liveObjects(), s.baselineObjects());
}

}  // namespace

int main() {
    lv_init();

    test_a_burst_while_the_ui_is_stopped_is_one_build();
    test_a_request_during_a_build_reaches_the_next_frame();
    test_one_producer_streaming_never_goes_backwards();
    test_several_producers_all_arrive_and_the_loop_drains();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
