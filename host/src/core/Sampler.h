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

// The sound of one pad. One layer: its gain follows the velocity curve. Several layers, softest
// first (multi-sampled kits): the velocity picks the layer, and the recorded level of each layer
// carries the dynamics.
struct PadSound
{
    std::vector<std::vector<float>> layers;

    PadSound() = default;
    PadSound (std::vector<float> single) { layers.push_back (std::move (single)); } // NOLINT: implicit on purpose
};

using SampleBank = std::array<PadSound, kDrumKit.size()>;

class Sampler
{
public:
    // A pad keeps at most kMaxVoicesPerPad sounding voices: a new hit releases the oldest one
    // with a short fade (kReleaseSamples, ~2 ms at 48 kHz) instead of cutting it, which clicks.
    // kMaxVoices leaves room for every pad plus the voices still fading out.
    static constexpr int kMaxVoices = 40;
    static constexpr int kMaxVoicesPerPad = 4;
    static constexpr int kReleaseSamples = 96;

    Sampler();
    ~Sampler();

    Sampler (const Sampler&) = delete;
    Sampler& operator= (const Sampler&) = delete;

    // Replaces the samples immediately, and drops a change still pending from requestSamples.
    // Only while rendering is stopped.
    void setSamples (SampleBank bank);

    // Thread-safe kit change while rendering: the bank is swapped in at the start of the next
    // render() (playing voices stop). Returns false, leaving `bank` untouched, while a previous
    // change is still pending or its old bank has not been collected with takeRetired().
    bool requestSamples (std::unique_ptr<SampleBank>& bank);

    // Bank replaced by the last swap, to be freed outside the audio thread. Null if none.
    std::unique_ptr<SampleBank> takeRetired();

    // Velocity 0..1 to linear gain: a slightly expanding curve, as most drum samplers use.
    static float velocityToGain (float velocity);

    // Layer index and gain for a velocity, given the number of layers of the pad.
    static void chooseLayer (std::size_t layerCount, float velocity, std::size_t& layer, float& gain);

    // Equal-power pan per pad, seen from the player (hi-hat left, ride right).
    static void padPan (Pad pad, float& left, float& right);

    // Starts a voice immediately. Audio thread only (or before rendering starts).
    void trigger (Pad pad, float velocity);

    // Applies a pending kit change, pops every pending hit from the queue, then mixes all
    // active voices into left / right (overwritten). Audio thread only.
    void render (HitQueue& queue, float* left, float* right, int numSamples);

    int activeVoiceCount() const noexcept;
    int activeVoiceCount (Pad pad) const noexcept; // sounding, not releasing

private:
    struct Voice
    {
        const std::vector<float>* sample = nullptr;
        std::size_t position = 0;
        float gainLeft = 0.0f;
        float gainRight = 0.0f;
        unsigned age = 0;    // trigger order, to steal the oldest voice
        Pad pad = Pad::snare;
        int release = 0;     // samples left in the release fade, 0 = not releasing
    };

    Voice* findVoiceSlot();

    void applyPendingSwap();

    SampleBank* active;                          // owned; used by the audio thread
    std::atomic<SampleBank*> pending { nullptr }; // owned; set by requestSamples
    std::atomic<SampleBank*> retired { nullptr }; // owned; collected by takeRetired
    std::array<Voice, kMaxVoices> voices {};
    unsigned triggerCounter = 0;
};

} // namespace vdrum
