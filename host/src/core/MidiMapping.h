#pragma once

// General MIDI drum mapping of the kit's pads, and the raw MIDI messages sent for a hit.
// Channel 10 (index 9) is the General MIDI percussion channel, so a DAW's drum instrument
// plays the right sounds without any setup.

#include "DrumKit.h"

#include <array>
#include <cstdint>

namespace vdrum::midi
{

constexpr std::uint8_t kDrumChannel = 9; // MIDI channel 10, zero-based
constexpr int kNoteLengthMs = 30;        // note off sent this long after the note on

// General MIDI percussion key map (GM 1 Level 1 Sound Set).
constexpr std::uint8_t noteForPad (Pad pad)
{
    switch (pad)
    {
        case Pad::hiHat:    return 42; // Closed Hi-Hat
        case Pad::snare:    return 38; // Acoustic Snare
        case Pad::highTom:  return 50; // High Tom
        case Pad::midTom:   return 47; // Low-Mid Tom
        case Pad::floorTom: return 43; // High Floor Tom
        case Pad::crash:    return 49; // Crash Cymbal 1
        case Pad::ride:     return 51; // Ride Cymbal 1
    }
    return 38;
}

// Velocity 0..1 to a MIDI velocity 1..127 (0 would mean note off).
constexpr std::uint8_t midiVelocity (float velocity)
{
    const auto scaled = static_cast<int> (velocity * 127.0f + 0.5f);
    return static_cast<std::uint8_t> (scaled < 1 ? 1 : (scaled > 127 ? 127 : scaled));
}

using Message = std::array<std::uint8_t, 3>;

constexpr Message noteOn (Pad pad, float velocity)
{
    return { static_cast<std::uint8_t> (0x90 | kDrumChannel), noteForPad (pad), midiVelocity (velocity) };
}

constexpr Message noteOff (Pad pad)
{
    return { static_cast<std::uint8_t> (0x80 | kDrumChannel), noteForPad (pad), 0 };
}

} // namespace vdrum::midi
