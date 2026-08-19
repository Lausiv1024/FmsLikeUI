#pragma once

/* Strings that live in the build arena.
 *
 * A Widget is a description built and thrown away every frame, so it must not
 * hold a pointer into the caller's temporary.  `.text = fmt("%d", v1_)` has to
 * survive until the RenderObject copies it, and no longer -- which is exactly
 * the arena's lifetime.  Str copies on construction and is then trivially
 * destructible.
 */

#include <cstddef>
#include <string>
#include <string_view>

namespace fmsui {

class Str {
public:
    constexpr Str() = default;

    Str(const char *s);              // NOLINT(google-explicit-constructor)
    Str(std::string_view s);         // NOLINT(google-explicit-constructor)
    Str(const std::string &s);       // NOLINT(google-explicit-constructor)

    const char *c_str() const { return data_ != nullptr ? data_ : ""; }
    std::string_view view() const { return std::string_view(c_str(), size_); }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    bool operator==(const Str &o) const { return view() == o.view(); }
    bool operator!=(const Str &o) const { return !(*this == o); }
    bool operator==(std::string_view o) const { return view() == o; }

private:
    void copyFrom(const char *s, size_t n);

    const char *data_ = nullptr;
    size_t size_ = 0;
};

/* printf into the build arena.  Only legal during a build pass. */
Str fmt(const char *format, ...) __attribute__((format(printf, 1, 2)));

}  // namespace fmsui
