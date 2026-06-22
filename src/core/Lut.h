#pragma once

#include <array>
#include <cstdint>

// Three per-channel 256-entry 8-bit tone LUTs (red, green, blue). The master
// (RGB) curve folds into all three; per-channel curves modify one each. Shared
// by the edit graph (preview accumulation) and the canvas (GPU LUT texture).
using ChannelLuts = std::array<std::array<uint8_t, 256>, 3>;

inline ChannelLuts identityChannelLuts()
{
    ChannelLuts luts;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i)
            luts[c][i] = static_cast<uint8_t>(i);
    return luts;
}
