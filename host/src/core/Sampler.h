#pragma once

// Minimal drum sampler: one mono sample per pad, played with a gain that follows the
// velocity, panned across the stereo field. render() runs on the audio thread: it never
// allocates, frees, locks or blocks.

#include "DrumKit.h"
#include "HitQueue.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace vdrum
{

using SampleBank = std::array<std::vector<float>, kDrumKit.size()>;

class Sampler
{
public:
    static constexpr int kMaxVoices = 24;

    Sampler();
    ~Sampler();

    Sampler (const Sampler&) = delete;
    Sampler& operator= (const Sampler&) = delete;

    // Replaces the samples immediately. Only while rendering is stopped.
    void setSamples (SampleBank bank);

    // Thread-safe kit change while rendering: the bank is swapped in at the start of the next
    // render() (playing voices stop). Returns false, leaving `bank` untouched, while a previous
    // change is still pending or its old bank has not been collected with takeRetired().
    bool requestSamples (std::unique_ptr<SampleBank>& bank);

    // Bank replaced by the last swap, to be freed outside the audio thread. Null if none.
    std::unique_ptr<SampleBank> takeRetired();

    // Velocity 0..1 to linear gain: a slightly expanding curve, as most drum samplers use.
    static float velocityToGain (float velocity);

    // Equal-power pan per pad, seen from the player (hi-hat left, ride right).
    static void padPan (Pad pad, float& left, float& right);

    // Starts a voice immediately. Audio thread only (or before rendering starts).
    void trigger (Pad pad, float velocity);

    // Applies a pending kit change, pops every pending hit from the queue, then mixes all
    // active voices into left / right (overwritten). Audio thread only.
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

    void applyPendingSwap();

    SampleBank* active;                          // owned; used by the audio thread
    std::atomic<SampleBank*> pending { nullptr }; // owned; set by requestSamples
    std::atomic<SampleBank*> retired { nullptr }; // owned; collected by takeRetired
    std::array<Voice, kMaxVoices> voices {};
    unsigned triggerCounter = 0;
};

} // namespace vdrum
