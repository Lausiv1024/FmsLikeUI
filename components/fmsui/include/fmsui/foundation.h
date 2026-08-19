#pragma once

/* Geometry, colour and constraint primitives.
 *
 * Coordinates are float: the ESP32-P4 has a single-precision FPU (rv32imafc),
 * so float layout maths costs the same as integer here and spares us the
 * rounding bugs that come with fixed-point flex distribution.  Values are
 * rounded once, at the point where they are handed to LVGL.
 */

#include <cmath>
#include <cstdint>
#include <limits>

namespace fmsui {

inline constexpr float kInf = std::numeric_limits<float>::infinity();

struct Size {
    float width = 0;
    float height = 0;

    constexpr bool operator==(const Size &o) const { return width == o.width && height == o.height; }
    constexpr bool operator!=(const Size &o) const { return !(*this == o); }
};

struct Offset {
    float dx = 0;
    float dy = 0;

    constexpr Offset operator+(const Offset &o) const { return {dx + o.dx, dy + o.dy}; }
    constexpr Offset operator-(const Offset &o) const { return {dx - o.dx, dy - o.dy}; }
    constexpr bool operator==(const Offset &o) const { return dx == o.dx && dy == o.dy; }
    constexpr bool operator!=(const Offset &o) const { return !(*this == o); }
};

struct Rect {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;

    constexpr float width() const { return right - left; }
    constexpr float height() const { return bottom - top; }
    constexpr bool contains(Offset p) const {
        return p.dx >= left && p.dx < right && p.dy >= top && p.dy < bottom;
    }
    static constexpr Rect fromLTWH(float l, float t, float w, float h) { return {l, t, l + w, t + h}; }
};

struct EdgeInsets {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;

    static constexpr EdgeInsets all(float v) { return {v, v, v, v}; }
    static constexpr EdgeInsets symmetric(float horizontal, float vertical) {
        return {horizontal, vertical, horizontal, vertical};
    }
    static constexpr EdgeInsets only(float l = 0, float t = 0, float r = 0, float b = 0) {
        return {l, t, r, b};
    }

    constexpr float horizontal() const { return left + right; }
    constexpr float vertical() const { return top + bottom; }
    constexpr bool operator==(const EdgeInsets &o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    constexpr bool operator!=(const EdgeInsets &o) const { return !(*this == o); }
};

/* 8-bit-per-channel colour with an alpha, independent of LVGL's compile-time
 * colour depth.  Conversion to lv_color_t happens in the LVGL backend. */
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    static constexpr Color rgb(uint32_t hex) {
        return {static_cast<uint8_t>((hex >> 16) & 0xFF), static_cast<uint8_t>((hex >> 8) & 0xFF),
                static_cast<uint8_t>(hex & 0xFF), 255};
    }
    static constexpr Color rgba(uint32_t hex, uint8_t alpha) {
        Color c = rgb(hex);
        c.a = alpha;
        return c;
    }
    static constexpr Color transparent() { return {0, 0, 0, 0}; }

    constexpr bool operator==(const Color &o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    constexpr bool operator!=(const Color &o) const { return !(*this == o); }
};

/* Identity across rebuilds.  Two widgets of the same type reconcile onto the
 * same Element only if their keys also match; give list items a key when they
 * can be reordered, or state will follow position instead of identity. */
struct Key {
    static constexpr int32_t kNone = INT32_MIN;
    int32_t value = kNone;

    constexpr bool isNone() const { return value == kNone; }
    constexpr bool operator==(const Key &o) const { return value == o.value; }
};

/* Layout contract, straight out of Flutter: constraints travel down, sizes come
 * back up, and the parent decides where the child sits. */
struct BoxConstraints {
    float minWidth = 0;
    float maxWidth = kInf;
    float minHeight = 0;
    float maxHeight = kInf;

    static constexpr BoxConstraints tight(Size s) { return {s.width, s.width, s.height, s.height}; }
    static constexpr BoxConstraints loose(Size s) { return {0, s.width, 0, s.height}; }
    static constexpr BoxConstraints expand() { return {kInf, kInf, kInf, kInf}; }

    constexpr bool hasBoundedWidth() const { return maxWidth != kInf; }
    constexpr bool hasBoundedHeight() const { return maxHeight != kInf; }
    constexpr bool hasTightWidth() const { return minWidth >= maxWidth; }
    constexpr bool hasTightHeight() const { return minHeight >= maxHeight; }

    constexpr float constrainWidth(float w) const {
        return w < minWidth ? minWidth : (w > maxWidth ? maxWidth : w);
    }
    constexpr float constrainHeight(float h) const {
        return h < minHeight ? minHeight : (h > maxHeight ? maxHeight : h);
    }
    constexpr Size constrain(Size s) const { return {constrainWidth(s.width), constrainHeight(s.height)}; }

    /* The largest size this allows -- meaningless on an unbounded axis, so
     * callers must check hasBounded*() first. */
    constexpr Size biggest() const { return {maxWidth, maxHeight}; }
    constexpr Size smallest() const { return {minWidth, minHeight}; }

    constexpr BoxConstraints loosen() const { return {0, maxWidth, 0, maxHeight}; }

    constexpr BoxConstraints deflate(EdgeInsets i) const {
        const float dw = i.horizontal();
        const float dh = i.vertical();
        const float mw = maxWidth == kInf ? kInf : (maxWidth - dw < 0 ? 0 : maxWidth - dw);
        const float mh = maxHeight == kInf ? kInf : (maxHeight - dh < 0 ? 0 : maxHeight - dh);
        const float nw = minWidth - dw < 0 ? 0 : minWidth - dw;
        const float nh = minHeight - dh < 0 ? 0 : minHeight - dh;
        return {nw > mw ? mw : nw, mw, nh > mh ? mh : nh, mh};
    }

    /* Replace an axis with a tight value, leaving the other one alone. */
    constexpr BoxConstraints tightenWidth(float w) const {
        const float v = constrainWidth(w);
        return {v, v, minHeight, maxHeight};
    }
    constexpr BoxConstraints tightenHeight(float h) const {
        const float v = constrainHeight(h);
        return {minWidth, maxWidth, v, v};
    }

    constexpr bool operator==(const BoxConstraints &o) const {
        return minWidth == o.minWidth && maxWidth == o.maxWidth && minHeight == o.minHeight &&
               maxHeight == o.maxHeight;
    }
    constexpr bool operator!=(const BoxConstraints &o) const { return !(*this == o); }
};

enum class Axis : uint8_t { Horizontal, Vertical };

/* How the children are packed along the flex axis. */
enum class MainAxis : uint8_t { Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly };

/* Where the children sit across the flex axis. */
enum class CrossAxis : uint8_t { Start, End, Center, Stretch };

/* Whether a Row/Column takes all the space it is offered along its main axis,
 * or shrinks to the size of its children. */
enum class MainAxisSize : uint8_t { Max, Min };

struct Alignment {
    /* -1 is left/top, 0 is centre, +1 is right/bottom. */
    float x = 0;
    float y = 0;

    static constexpr Alignment topLeft() { return {-1, -1}; }
    static constexpr Alignment topCenter() { return {0, -1}; }
    static constexpr Alignment topRight() { return {1, -1}; }
    static constexpr Alignment centerLeft() { return {-1, 0}; }
    static constexpr Alignment center() { return {0, 0}; }
    static constexpr Alignment centerRight() { return {1, 0}; }
    static constexpr Alignment bottomLeft() { return {-1, 1}; }
    static constexpr Alignment bottomCenter() { return {0, 1}; }
    static constexpr Alignment bottomRight() { return {1, 1}; }

    /* Where a child of size `child` lands inside a box of size `parent`. */
    constexpr Offset inscribe(Size child, Size parent) const {
        return {(parent.width - child.width) * (x + 1) / 2, (parent.height - child.height) * (y + 1) / 2};
    }
};

enum class TextAlign : uint8_t { Left, Center, Right };

}  // namespace fmsui
