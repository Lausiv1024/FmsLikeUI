#pragma once

/* Bump allocator for the throwaway Widget tree.
 *
 * A build pass creates every Widget from scratch and then drops the lot, so the
 * allocator only ever needs to hand out memory and, once, give it all back.
 * `new Column{...}` inside build() therefore costs a pointer increment, and
 * nothing fragments the heap.
 *
 * Two arenas alternate.  While a build pass fills one, the previous pass's
 * widgets are still intact in the other -- so a callback captured last frame
 * stays alive until the frame after next, and State::widget() can be read
 * outside build() without pointing at rubble.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fmsui {

/* Anything the arena has to destroy (rather than just forget) derives from this
 * -- Widgets do, because they hold std::function callbacks. */
class ArenaObject {
public:
    virtual ~ArenaObject() = default;
};

class Arena {
public:
    explicit Arena(size_t chunk_bytes = 32 * 1024);
    ~Arena();

    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;

    void *allocate(size_t bytes, size_t align);

    /* Objects registered here get their destructor run on reset(). */
    void track(ArenaObject *obj) { tracked_.push_back(obj); }

    /* Destroy everything and rewind to empty.  Chunks are kept for reuse, so a
     * steady-state UI stops calling malloc entirely after the first few frames. */
    void reset();

    bool owns(const void *p) const;

    size_t bytesUsed() const { return used_; }
    size_t bytesReserved() const;
    size_t highWaterMark() const { return high_water_; }
    size_t objectCount() const { return tracked_.size(); }

    /* The arena that `new Widget{...}` allocates from. */
    static Arena *current() { return current_; }
    static void setCurrent(Arena *a) { current_ = a; }

    /* The chunks come from the default heap, and on the device that means PSRAM
     * -- ESP-IDF sends anything over CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (16kB)
     * there, and a chunk is 32kB.
     *
     * Forcing them into internal SRAM was tried, on the theory that writing the
     * widget tree to external memory every frame was what made a rebuild cost
     * 20us per widget. It made everything *slower*: build 15ms -> 26ms, layout
     * 2ms -> 4ms, paint 0.6ms -> 2.2ms. Internal SRAM is 200kB and already spoken
     * for; taking 64kB of it for the arena pushed the Elements, the RenderObjects
     * and LVGL's own objects out to PSRAM instead. The arena was not the problem,
     * and internal SRAM is not ours to take. */

private:
    struct Chunk {
        uint8_t *base;
        size_t capacity;
        size_t offset;
    };

    void addChunk(size_t min_bytes);

    std::vector<Chunk> chunks_;
    std::vector<ArenaObject *> tracked_;
    size_t chunk_bytes_;
    size_t chunk_index_ = 0;
    size_t used_ = 0;
    size_t high_water_ = 0;

    static Arena *current_;
};

/* The two arenas the frame loop alternates between. */
class BuildArenas {
public:
    /* Switch to the other arena and clear it.  What it held was two builds ago
     * and is unreachable; what the *previous* build produced is untouched. */
    void beginBuild();

    Arena &current() { return *current_; }
    const Arena &front() const { return *current_; }
    const Arena &back() const { return current_ == &a_ ? b_ : a_; }

private:
    Arena a_;
    Arena b_;
    Arena *current_ = &a_;
};

}  // namespace fmsui
