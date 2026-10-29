#include "DrumSynth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vdrum
{

namespace
{

constexpr double kTwoPi = 6.283185307179586;

// Deterministic white noise in [-1, 1).
class Noise
{
public:
    explicit Noise (std::uint32_t seed) : state (seed) {}

    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (state) / 2147483648.0f - 1.0f;
    }

private:
    std::uint32_t state;
};

// One-pole high-pass, enough to take the low end out of noise and metallic tones.
class HighPass
{
public:
    HighPass (double cutoffHz, double sampleRate)
        : coefficient (static_cast<float> (std::exp (-kTwoPi * cutoffHz / sampleRate))) {}

    float process (float x)
    {
        const auto y = coefficient * (previousOutput + x - previousInput);
        previousInput = x;
        previousOutput = y;
        return y;
    }

private:
    float coefficient;
    float previousInput = 0.0f;
    float previousOutput = 0.0f;
};

void normalise (std::vector<float>& data)
{
    float peak = 0.0f;
    for (const auto s : data)
        peak = std::max (peak, std::abs (s));
    if (peak > 0.0f)
        for (auto& s : data)
            s *= 0.9f / peak;

    // Short fade-out so that the end of the buffer never clicks.
    const auto fade = std::min<std::size_t> (data.size(), 256);
    for (std::size_t i = 0; i < fade; ++i)
        data[data.size() - 1 - i] *= static_cast<float> (i) / static_cast<float> (fade);
}

// Pitched membrane: sine with a falling pitch, plus a short noise attack.
std::vector<float> membrane (double sampleRate, double seconds, double startHz, double endHz,
                             double pitchTime, double decayTime, float noiseAmount, double noiseDecay, std::uint32_t seed)
{
    std::vector<float> out (static_cast<std::size_t> (seconds * sampleRate));
    Noise noise (seed);
    HighPass noiseFilter (1800.0, sampleRate);
    double phase = 0.0;

    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto frequency = endHz + (startHz - endHz) * std::exp (-t / pitchTime);
        phase += kTwoPi * frequency / sampleRate;

        const auto body = std::sin (phase) * std::exp (-t / decayTime);
        const auto snap = static_cast<float> (noiseFilter.process (noise.next()) * std::exp (-t / noiseDecay));
        out[i] = static_cast<float> (body) + noiseAmount * snap;
    }
    return out;
}

// Metallic sound: six detuned square waves (as in the TR-808 cymbals) plus noise.
std::vector<float> metal (double sampleRate, double seconds, double decayTime, double highPassHz,
                          float noiseAmount, float bellAmount, std::uint32_t seed)
{
    static constexpr std::array<double, 6> kPartials { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

    std::vector<float> out (static_cast<std::size_t> (seconds * sampleRate));
    Noise noise (seed);
    HighPass filter (highPassHz, sampleRate);
    std::array<double, 6> phases {};

    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        double square = 0.0;
        for (std::size_t k = 0; k < kPartials.size(); ++k)
        {
            phases[k] += kPartials[k] * 1.6 / sampleRate;
            phases[k] -= std::floor (phases[k]);
            square += phases[k] < 0.5 ? 1.0 : -1.0;
        }

        const auto envelope = std::exp (-t / decayTime);
        const auto attack = std::min (1.0, t / 0.0015);
        const auto bell = bellAmount * std::sin (kTwoPi * 620.0 * t) * std::exp (-t / (0.6 * decayTime));
        const auto raw = static_cast<float> (square / 6.0) + noiseAmount * noise.next();
        out[i] = static_cast<float> (attack * envelope) * filter.process (raw) + static_cast<float> (bell);
    }
    return out;
}

} // namespace

std::vector<float> synthesiseDrum (Pad pad, double sampleRate)
{
    std::vector<float> out;
    switch (pad)
    {
        case Pad::snare:    out = membrane (sampleRate, 0.35, 260.0, 185.0, 0.012, 0.07, 1.4f, 0.09, 11); break;
        case Pad::highTom:  out = membrane (sampleRate, 0.9, 260.0, 200.0, 0.03, 0.28, 0.25f, 0.01, 12); break;
        case Pad::midTom:   out = membrane (sampleRate, 1.0, 200.0, 150.0, 0.03, 0.32, 0.25f, 0.01, 13); break;
        case Pad::floorTom: out = membrane (sampleRate, 1.2, 130.0, 95.0, 0.04, 0.38, 0.25f, 0.01, 14); break;
        case Pad::hiHat:    out = metal (sampleRate, 0.25, 0.045, 7000.0, 0.6f, 0.0f, 21); break;
        case Pad::crash:    out = metal (sampleRate, 2.8, 0.9, 3500.0, 0.9f, 0.0f, 22); break;
        case Pad::ride:     out = metal (sampleRate, 2.6, 1.1, 5000.0, 0.3f, 0.35f, 23); break;
    }
    normalise (out);
    return out;
}

SampleBank synthesiseKit (double sampleRate)
{
    SampleBank bank;
    for (const auto& info : kDrumKit)
        bank[static_cast<std::size_t> (info.pad)] = synthesiseDrum (info.pad, sampleRate);
    return bank;
}

} // namespace vdrum
