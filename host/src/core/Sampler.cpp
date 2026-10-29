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
    const auto& sample = (*active)[static_cast<std::size_t> (pad)];
    if (sample.empty())
        return;

    // Free voice, else steal the oldest one.
    auto* target = &voices[0];
    for (auto& voice : voices)
    {
        if (voice.sample == nullptr)
        {
            target = &voice;
            break;
        }
        if (voice.age < target->age)
            target = &voice;
    }

    float left = 0.0f, right = 0.0f;
    padPan (pad, left, right);
    const auto gain = velocityToGain (velocity) * kMasterGain;

    target->sample = &sample;
    target->position = 0;
    target->gainLeft = gain * left;
    target->gainRight = gain * right;
    target->age = ++triggerCounter;
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
        const auto count = std::min (remaining, static_cast<std::size_t> (numSamples));

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

} // namespace vdrum
