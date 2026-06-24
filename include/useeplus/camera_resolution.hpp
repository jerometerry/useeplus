#pragma once

#include <cstdint>

#include "useeplus_protocol.h"

struct CameraResolution {
    uint32_t width;
    uint32_t height;
    up_resolution_index hw_index;

    bool operator==(const CameraResolution&) const = default;
};

namespace SupportedResolutions {
inline constexpr CameraResolution HD_720P{1280, 720, UP_RES_720P};
inline constexpr CameraResolution VGA_480P{640, 480, UP_RES_480P};
inline constexpr CameraResolution LOW_240P{320, 240, UP_RES_240P};

constexpr CameraResolution getClosest(uint32_t width, uint32_t /*height*/) {
    if (width >= HD_720P.width) {
        return HD_720P;
    } else if (width <= LOW_240P.width) {
        return LOW_240P;
    }

    return VGA_480P;
}

}  // namespace SupportedResolutions