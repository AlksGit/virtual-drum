#pragma once

// Minimal drum sampler: one mono sample per pad, played with a gain that follows the
// velocity, panned across the stereo field. render() runs on the audio thread: it never
// allocates, locks or blocks.

#include "DrumKit.h"
#include "HitQueue.h"

#include <array>
#include <vector>

namespace vdrum
{

using SampleBank = std::array<std::vector<float>, kDrumKit.size()>;

class Sampler
{
public:
    static constexpr int kMaxVoices = 24;

    // Must be called before rendering starts (not on the audio thread).
    void setSamples (SampleBank bank);

    // Velocity 0..1 to linear gain: a slightly expanding curve, as most drum samplers use.
    static float velocityToGain (float velocity);

    // Equal-power pan per pad, seen from the player (hi-hat left, ride right).
    static void padPan (Pad pad, float& left, float& right);

    // Starts a voice immediately. Audio thread only (or before rendering starts).
    void trigger (Pad pad, float velocity);

    // Pops every pending hit from the queue, then mixes all active voices into
    // left / right (overwritten). Audio thread only.
    void render (HitQueue& queue, float* left, float* right, int numSamples);

    int activeVoiceCount() const noexcept;

private:
    struct Voice
    {
        const std::vector<float>* sample = nullptr;
        std::size_t position = 0;
        float gainLeft = 0.0f;
        float gainRight = 0.0f;
        unsigned age = 0; // trigger order, to steal the oldest voice
    };

    SampleBank samples;
    std::array<Voice, kMaxVoices> voices {};
    unsigned triggerCounter = 0;
};

} // namespace vdrum
