#include "Sampler.h"

#include <algorithm>
#include <cmath>

namespace vdrum
{

namespace
{

constexpr float kMasterGain = 0.6f;

// Gentle saturation so that many loud voices never clip hard.
float softClip (float x)
{
    return std::tanh (x);
}

} // namespace

Sampler::Sampler() : active (new SampleBank()) {}

Sampler::~Sampler()
{
    delete active;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

void Sampler::setSamples (SampleBank bank)
{
    for (auto& voice : voices)
        voice = {};
    *active = std::move (bank);
    // An older change accepted before the device stopped would otherwise replace this bank on
    // the first render() after the restart.
    delete pending.exchange (nullptr);
}

bool Sampler::requestSamples (std::unique_ptr<SampleBank>& bank)
{
    if (bank == nullptr || pending.load() != nullptr || retired.load() != nullptr)
        return false;

    SampleBank* expected = nullptr;
    if (! pending.compare_exchange_strong (expected, bank.get()))
        return false;

    bank.release(); // now owned through `pending`
    return true;
}

std::unique_ptr<SampleBank> Sampler::takeRetired()
{
    return std::unique_ptr<SampleBank> (retired.exchange (nullptr));
}

void Sampler::applyPendingSwap()
{
    // Only swap when the previous old bank has been collected, so nothing is freed here.
    if (retired.load (std::memory_order_acquire) != nullptr)
        return;

    auto* next = pending.exchange (nullptr, std::memory_order_acq_rel);
    if (next == nullptr)
        return;

    // Voices point into the old bank: stop them before it changes hands.
    for (auto& voice : voices)
        voice.sample = nullptr;

    retired.store (active, std::memory_order_release);
    active = next;
}

float Sampler::velocityToGain (float velocity)
{
    const auto v = std::clamp (velocity, 0.0f, 1.0f);
    return std::pow (v, 1.6f);
}

void Sampler::chooseLayer (std::size_t layerCount, float velocity, std::size_t& layer, float& gain)
{
    const auto v = std::clamp (velocity, 0.0f, 1.0f);
    if (layerCount <= 1)
    {
        layer = 0;
        gain = velocityToGain (v);
        return;
    }

    // Even split of the velocity range; within a layer, a mild gain slope smooths the steps.
    const auto position = v * static_cast<float> (layerCount);
    layer = std::min (layerCount - 1, static_cast<std::size_t> (position));
    gain = 0.85f + 0.15f * std::clamp (position - static_cast<float> (layer), 0.0f, 1.0f);
}

void Sampler::padPan (Pad pad, float& left, float& right)
{
    // Pan from the pad azimuth: +50 degrees (left) to -50 degrees (right) maps to -1..1.
    const auto pan = std::clamp (-padInfo (pad).azimuthDeg / 50.0f, -1.0f, 1.0f) * 0.7f;
    const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
    left = std::cos (angle);
    right = std::sin (angle);
}

void Sampler::trigger (Pad pad, float velocity)
{
    const auto& sound = (*active)[static_cast<std::size_t> (pad)];
    if (sound.layers.empty())
        return;

    std::size_t layer = 0;
    float layerGain = 1.0f;
    chooseLayer (sound.layers.size(), velocity, layer, layerGain);
    const auto& sample = sound.layers[layer];
    if (sample.empty())
        return;

    // Too many voices on this pad: release its oldest one (fades out instead of a hard cut).
    if (activeVoiceCount (pad) >= kMaxVoicesPerPad)
    {
        Voice* oldest = nullptr;
        for (auto& voice : voices)
            if (voice.sample != nullptr && voice.release == 0 && voice.pad == pad
                && (oldest == nullptr || voice.age < oldest->age))
                oldest = &voice;
        if (oldest != nullptr)
            oldest->release = kReleaseSamples;
    }

    auto* target = findVoiceSlot();

    float left = 0.0f, right = 0.0f;
    padPan (pad, left, right);
    const auto gain = layerGain * kMasterGain;

    target->sample = &sample;
    target->position = 0;
    target->gainLeft = gain * left;
    target->gainRight = gain * right;
    target->age = ++triggerCounter;
    target->pad = pad;
    target->release = 0;
}

Sampler::Voice* Sampler::findVoiceSlot()
{
    // A free voice, else the oldest releasing one (already fading), else the oldest one.
    Voice* oldestReleasing = nullptr;
    Voice* oldest = &voices[0];
    for (auto& voice : voices)
    {
        if (voice.sample == nullptr)
            return &voice;
        if (voice.release > 0 && (oldestReleasing == nullptr || voice.age < oldestReleasing->age))
            oldestReleasing = &voice;
        if (voice.age < oldest->age)
            oldest = &voice;
    }
    return oldestReleasing != nullptr ? oldestReleasing : oldest;
}

void Sampler::render (HitQueue& queue, float* left, float* right, int numSamples)
{
    applyPendingSwap();

    HitEvent event;
    while (queue.pop (event))
        trigger (event.pad, event.velocity);

    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);

    for (auto& voice : voices)
    {
        if (voice.sample == nullptr)
            continue;

        const auto& data = *voice.sample;
        const auto remaining = data.size() - voice.position;
        auto count = std::min (remaining, static_cast<std::size_t> (numSamples));

        if (voice.release > 0)
        {
            // Linear fade to silence over the remaining release samples.
            count = std::min (count, static_cast<std::size_t> (voice.release));
            const auto step = 1.0f / static_cast<float> (kReleaseSamples);
            auto level = static_cast<float> (voice.release) * step;
            for (std::size_t i = 0; i < count; ++i)
            {
                level -= step;
                const auto s = data[voice.position + i] * std::max (level, 0.0f);
                left[i] += s * voice.gainLeft;
                right[i] += s * voice.gainRight;
            }
            voice.release -= static_cast<int> (count);
            voice.position += count;
            if (voice.release <= 0 || voice.position >= data.size())
                voice = {};
            continue;
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto s = data[voice.position + i];
            left[i] += s * voice.gainLeft;
            right[i] += s * voice.gainRight;
        }

        voice.position += count;
        if (voice.position >= data.size())
            voice.sample = nullptr;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = softClip (left[i]);
        right[i] = softClip (right[i]);
    }
}

int Sampler::activeVoiceCount() const noexcept
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.sample != nullptr; }));
}

int Sampler::activeVoiceCount (Pad pad) const noexcept
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [pad] (const Voice& v)
                                            { return v.sample != nullptr && v.release == 0 && v.pad == pad; }));
}

} // namespace vdrum
