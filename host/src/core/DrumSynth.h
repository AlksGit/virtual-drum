#pragma once

// Procedural drum sounds, so that the sampler works out of the box without shipping
// audio files (the repository is public). Real samples can replace them later.

#include "DrumKit.h"
#include "Sampler.h"

#include <vector>

namespace vdrum
{

// Mono sound for a pad, normalised to a peak of 0.9. Deterministic.
std::vector<float> synthesiseDrum (Pad pad, double sampleRate);

SampleBank synthesiseKit (double sampleRate);

} // namespace vdrum
