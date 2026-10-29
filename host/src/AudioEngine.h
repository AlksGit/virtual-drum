#pragma once

#include "HitQueue.h"
#include "Sampler.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <vector>

namespace vdrum
{

// Plays the sampler on the default audio output. Hits are pushed from any single producer
// thread (the data source) through a lock-free queue and start on the next audio block.
// Requests a 64-sample buffer at 48 kHz; the device may grant something else.
class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // Opens the default output device. Returns an error message, or an empty string.
    juce::String start();

    // Producer side: call from the data source thread only.
    // Hits arriving while no audio is running are dropped, so that they cannot pile up and
    // all play at once when the device starts.
    bool pushHit (const HitEvent& hit) noexcept { return acceptingHits && hitQueue.push (hit); }

    void setMuted (bool shouldBeMuted) noexcept { muted = shouldBeMuted; }
    bool isMuted() const noexcept { return muted; }

    // Device name, sample rate, buffer size and output latency (message thread).
    juce::String describeDevice();

private:
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager deviceManager;
    Sampler sampler;
    HitQueue hitQueue;
    std::vector<float> scratchLeft, scratchRight; // sized in audioDeviceAboutToStart
    std::atomic<bool> muted { false };
    std::atomic<bool> acceptingHits { false };
};

} // namespace vdrum
