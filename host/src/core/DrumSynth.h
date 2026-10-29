#pragma once

// Procedural drum sounds, so that the sampler works out of the box without shipping audio
// files (the repository is public). Several synthesised kits; user sample kits are loaded by
// the app (KitLibrary) and fall back to these sounds for missing pads.

#include "DrumKit.h"
#include "Sampler.h"

#include <array>
#include <vector>

namespace vdrum
{

enum class SynthKit
{
    studio,  // balanced acoustic kit
    rock,    // lower tuning, punchier, a bit of saturation
    jazz,    // brushes on the snare, small high-tuned toms, dark washy ride
    electro, // TR-808 style electronic sounds
};

inline constexpr std::array<SynthKit, 4> kSynthKits { SynthKit::studio, SynthKit::rock, SynthKit::jazz, SynthKit::electro };

const char* synthKitName (SynthKit kit);

// Mono sound for a pad, normalised to a peak of 0.9. Deterministic.
std::vector<float> synthesiseDrum (Pad pad, double sampleRate, SynthKit kit = SynthKit::studio);

SampleBank synthesiseKit (double sampleRate, SynthKit kit = SynthKit::studio);

} // namespace vdrum
