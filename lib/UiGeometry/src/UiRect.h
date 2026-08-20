#pragma once

#include <cstdint>

namespace uigeom {

struct Rect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;

    // `pad` grows the hit-test area on all sides without changing the drawn
    // button size, so narrow/adjacent touch targets stay reachable despite
    // resistive-touch calibration slop.
    bool contains(uint16_t px, uint16_t py, int16_t pad = 0) const {
        const int left = static_cast<int>(x) - pad;
        const int top = static_cast<int>(y) - pad;
        const int right = static_cast<int>(x) + static_cast<int>(w) + pad;
        const int bottom = static_cast<int>(y) + static_cast<int>(h) + pad;
        const int ipx = static_cast<int>(px);
        const int ipy = static_cast<int>(py);
        return ipx >= left && ipy >= top && ipx < right && ipy < bottom;
    }
};

inline bool overlaps(const Rect& a, const Rect& b) {
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
}

}  // namespace uigeom
