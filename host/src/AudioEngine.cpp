#include "AudioEngine.h"

#include "DrumSynth.h"

namespace vdrum
{

namespace
{

constexpr double kPreferredSampleRate = 48000.0;
constexpr int kPreferredBufferSize = 64;

} // namespace

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::start()
{
    const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (error.isNotEmpty())
        return error;

    // Prefer the lowest-latency driver type available. On Windows the default "Windows Audio"
    // (shared WASAPI) imposes ~10 ms periods; the low-latency mode gets close to the driver
    // minimum. Exclusive mode is not used: it would silence every other application.
    const auto defaultType = deviceManager.getCurrentAudioDeviceType();
    for (const auto* preferred : { "ASIO", "Windows Audio (Low Latency Mode)" })
    {
        bool available = false;
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            available = available || type->getTypeName() == preferred;
        if (! available)
            continue;

        deviceManager.setCurrentAudioDeviceType (preferred, true);
        if (deviceManager.getCurrentAudioDevice() != nullptr)
            break;
        deviceManager.setCurrentAudioDeviceType (defaultType, true); // could not open: fall back
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "no audio output device";

    // Ask for 48 kHz and the smallest buffer the device offers that is at least 64 samples
    // (devices only accept sizes from their own list).
    auto setup = deviceManager.getAudioDeviceSetup();
    if (device->getAvailableSampleRates().contains (kPreferredSampleRate))
        setup.sampleRate = kPreferredSampleRate;

    auto bufferSize = device->getDefaultBufferSize();
    for (const auto size : device->getAvailableBufferSizes())
        if (size >= kPreferredBufferSize && size < bufferSize)
            bufferSize = size;
    setup.bufferSize = bufferSize;
    deviceManager.setAudioDeviceSetup (setup, true);

    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return "no audio output device";

    deviceManager.addAudioCallback (this);
    return {};
}

juce::String AudioEngine::describeDevice()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "no audio";

    const auto sampleRate = device->getCurrentSampleRate();
    const auto bufferSize = device->getCurrentBufferSizeSamples();
    const auto latencyMs = 1000.0 * (bufferSize + device->getOutputLatencyInSamples()) / sampleRate;

    auto smallest = bufferSize;
    for (const auto size : device->getAvailableBufferSizes())
        smallest = juce::jmin (smallest, size);

    return device->getTypeName() + " / " + device->getName() + "   "
         + juce::String (juce::roundToInt (sampleRate)) + " Hz, " + juce::String (bufferSize) + " samples (device min "
         + juce::String (smallest) + "), ~" + juce::String (latencyMs, 1) + " ms out";
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Rendering is not running yet: allocations are fine here.
    sampler.setSamples (synthesiseKit (device->getCurrentSampleRate()));
    const auto maxBlock = static_cast<std::size_t> (juce::jmax (device->getCurrentBufferSizeSamples(), 4096));
    scratchLeft.assign (maxBlock, 0.0f);
    scratchRight.assign (maxBlock, 0.0f);

    HitEvent stale;
    while (hitQueue.pop (stale)) {}
    acceptingHits = true;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    // Process in chunks that fit the scratch buffers (the block size can exceed the nominal one).
    int done = 0;
    while (done < numSamples)
    {
        const auto chunk = juce::jmin (numSamples - done, static_cast<int> (scratchLeft.size()));
        sampler.render (hitQueue, scratchLeft.data(), scratchRight.data(), chunk);

        const auto gain = muted ? 0.0f : 1.0f;
        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            auto* out = outputChannelData[channel];
            if (out == nullptr)
                continue;

            const auto* source = (channel % 2 == 0 || numOutputChannels == 1) ? scratchLeft.data() : scratchRight.data();
            for (int i = 0; i < chunk; ++i)
                out[done + i] = gain * source[i];
        }
        done += chunk;
    }
}

void AudioEngine::audioDeviceStopped()
{
    acceptingHits = false;
}

} // namespace vdrum
