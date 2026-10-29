#include "AudioEngine.h"

#include "DrumSynth.h"

namespace vdrum
{

namespace
{

constexpr double kPreferredSampleRate = 48000.0;
constexpr int kPreferredBufferSize = 64;

} // namespace

AudioEngine::AudioEngine()
    : kitFactory ([] (double sampleRate) { return synthesiseKit (sampleRate); })
{
    // Frees kits replaced on the audio thread, and retries a kit change that had to wait.
    startTimerHz (10);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::setKit (KitFactory factory)
{
    kitFactory = std::move (factory);
    kitChangePending = true;
    submitKit();
}

void AudioEngine::submitKit()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (! kitChangePending || device == nullptr || ! device->isPlaying())
        return; // applied by audioDeviceAboutToStart

    auto bank = std::make_unique<SampleBank> (kitFactory (device->getCurrentSampleRate()));
    if (sampler.requestSamples (bank))
        kitChangePending = false; // otherwise retried by the timer
}

void AudioEngine::timerCallback()
{
    sampler.takeRetired(); // destroyed here, on the message thread
    submitKit();
}

juce::String AudioEngine::start (const juce::String& preferredDriverType)
{
    const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (error.isNotEmpty())
        return error;

    // The user's choice first, else the lowest-latency driver type available. On Windows the
    // default "Windows Audio" (shared WASAPI) imposes ~10 ms periods; the low-latency mode gets
    // close to the driver minimum. Exclusive mode is never chosen automatically: it silences
    // every other application.
    const auto available = availableDriverTypes();
    const auto defaultType = deviceManager.getCurrentAudioDeviceType();
    juce::StringArray candidates;
    if (preferredDriverType.isNotEmpty())
        candidates.add (preferredDriverType);
    candidates.addArray ({ "ASIO", "Windows Audio (Low Latency Mode)" });

    for (const auto& candidate : candidates)
    {
        if (! available.contains (candidate))
            continue;

        deviceManager.setCurrentAudioDeviceType (candidate, true);
        if (deviceManager.getCurrentAudioDevice() != nullptr)
            break;
        deviceManager.setCurrentAudioDeviceType (defaultType, true); // could not open: fall back
    }

    const auto setupError = useSmallestBuffer();
    if (setupError.isNotEmpty())
        return setupError;

    deviceManager.addAudioCallback (this);
    return {};
}

juce::String AudioEngine::useSmallestBuffer()
{
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
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return error.isNotEmpty() ? error : juce::String ("no audio output device");
    return {};
}

juce::StringArray AudioEngine::availableDriverTypes()
{
    juce::StringArray names;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        names.add (type->getTypeName());
    return names;
}

juce::String AudioEngine::currentDriverType() const
{
    return deviceManager.getCurrentAudioDeviceType();
}

juce::String AudioEngine::setDriverType (const juce::String& typeName)
{
    if (! availableDriverTypes().contains (typeName))
        return "unknown driver type: " + typeName;

    const auto previous = currentDriverType();
    deviceManager.setCurrentAudioDeviceType (typeName, true);
    auto error = useSmallestBuffer();
    if (error.isNotEmpty())
    {
        // Could not open (device busy in exclusive mode, missing ASIO driver...): go back.
        deviceManager.setCurrentAudioDeviceType (previous, true);
        useSmallestBuffer();
        return typeName + ": " + error;
    }
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
    sampler.setSamples (kitFactory (device->getCurrentSampleRate()));
    kitChangePending = false;
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
