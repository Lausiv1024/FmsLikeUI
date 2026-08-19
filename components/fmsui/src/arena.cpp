#include "fmsui/arena.h"

#include <cassert>
#include <cstdlib>

namespace fmsui {

Arena *Arena::current_ = nullptr;

Arena::Arena(size_t chunk_bytes) : chunk_bytes_(chunk_bytes) {}

Arena::~Arena() {
    reset();
    for (Chunk &c : chunks_) std::free(c.base);
}

void Arena::addChunk(size_t min_bytes) {
    const size_t cap = min_bytes > chunk_bytes_ ? min_bytes : chunk_bytes_;
    uint8_t *base = static_cast<uint8_t *>(std::malloc(cap));
    assert(base != nullptr && "arena chunk allocation failed");
    chunks_.push_back(Chunk{base, cap, 0});
}

void *Arena::allocate(size_t bytes, size_t align) {
    /* Walk forward from the current chunk.  After reset() the chunks are still
     * here with offset 0, so the steady state reuses them without malloc. */
    while (chunk_index_ < chunks_.size()) {
        Chunk &c = chunks_[chunk_index_];
        const size_t aligned = (c.offset + align - 1) & ~(align - 1);
        if (aligned + bytes <= c.capacity) {
            c.offset = aligned + bytes;
            used_ += bytes;
            if (used_ > high_water_) high_water_ = used_;
            return c.base + aligned;
        }
        chunk_index_++;
    }

    addChunk(bytes + align);
    chunk_index_ = chunks_.size() - 1;
    Chunk &c = chunks_[chunk_index_];
    const size_t aligned = (c.offset + align - 1) & ~(align - 1);
    c.offset = aligned + bytes;
    used_ += bytes;
    if (used_ > high_water_) high_water_ = used_;
    return c.base + aligned;
}

void Arena::reset() {
    /* Destroy in reverse order of construction, so a widget's children outlive
     * it the same way they would with ordinary scoping. */
    for (size_t i = tracked_.size(); i > 0; i--) {
        tracked_[i - 1]->~ArenaObject();
    }
    tracked_.clear();

    for (Chunk &c : chunks_) c.offset = 0;
    chunk_index_ = 0;
    used_ = 0;
}

bool Arena::owns(const void *p) const {
    const uint8_t *q = static_cast<const uint8_t *>(p);
    for (const Chunk &c : chunks_) {
        if (q >= c.base && q < c.base + c.capacity) return true;
    }
    return false;
}

size_t Arena::bytesReserved() const {
    size_t total = 0;
    for (const Chunk &c : chunks_) total += c.capacity;
    return total;
}

void BuildArenas::beginBuild() {
    current_ = (current_ == &a_) ? &b_ : &a_;
    current_->reset();
    Arena::setCurrent(current_);
}

}  // namespace fmsui
