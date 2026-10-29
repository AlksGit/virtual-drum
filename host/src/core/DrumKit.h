#pragma once

// Layout of the virtual drum kit, shared by the fake data generator and the 3D view.
//
// Zones are defined the way the real detection will see them: by the stick orientation at
// the moment of the hit, as an azimuth (yaw) and an elevation (pitch) seen from the player.
// World frame (see docs/PROTOCOL.md): X forward from the player towards the kit, Y to the
// player's left, Z up. Azimuth is positive to the left, elevation positive upwards.

#include <array>
#include <cstddef>

namespace vdrum
{

enum class Pad
{
    hiHat,
    snare,
    highTom,
    midTom,
    floorTom,
    crash,
    ride
};

struct PadInfo
{
    Pad pad;
    const char* name;
    float azimuthDeg;   // stick yaw at the hit, positive to the left
    float elevationDeg; // stick pitch at the hit, negative = tip below horizontal
};

// Angles chosen so that the physical layout (KitLayout.h) looks like a real kit without
// pads intersecting, with at least 18 degrees between any two zones.
inline constexpr std::array<PadInfo, 7> kDrumKit { {
    { Pad::hiHat,    "Hi-hat",     44.0f, -10.0f },
    { Pad::snare,    "Snare",       8.0f, -16.0f },
    { Pad::highTom,  "High tom",   18.0f,  -1.0f },
    { Pad::midTom,   "Mid tom",   -16.0f,  -1.0f },
    { Pad::floorTom, "Floor tom", -44.0f, -20.0f },
    { Pad::crash,    "Crash",      48.0f,  14.0f },
    { Pad::ride,     "Ride",      -50.0f,   9.0f },
} };

constexpr bool kitMatchesPadOrder()
{
    for (std::size_t i = 0; i < kDrumKit.size(); ++i)
        if (static_cast<std::size_t> (kDrumKit[i].pad) != i)
            return false;
    return true;
}

static_assert (kitMatchesPadOrder(), "kDrumKit entries must follow the order of enum Pad");

constexpr const PadInfo& padInfo (Pad pad)
{
    return kDrumKit[static_cast<std::size_t> (pad)];
}

} // namespace vdrum
