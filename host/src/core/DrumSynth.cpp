#include "DrumSynth.h"

#include <algorithm>
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

// One-pole low-pass, to darken noise (brushes, dark cymbals).
class LowPass
{
public:
    LowPass (double cutoffHz, double sampleRate)
        : coefficient (static_cast<float> (1.0 - std::exp (-kTwoPi * cutoffHz / sampleRate))) {}

    float process (float x)
    {
        state += coefficient * (x - state);
        return state;
    }

private:
    float coefficient;
    float state = 0.0f;
};

float saturate (float x, float drive)
{
    return drive > 0.0f ? std::tanh (x * (1.0f + drive)) / std::tanh (1.0f + drive) : x;
}

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

// Pitched membrane: sine with a falling pitch, an optional inharmonic overtone, and noise
// (snare wires, stick attack or brushes).
struct Membrane
{
    double seconds;
    double startHz, endHz, pitchTime;
    double decay;
    float overtone = 0.0f;      // level of a second mode at 1.59 x the pitch
    float noise = 0.0f;
    double noiseDecay = 0.01;
    double noiseHighPassHz = 1800.0;
    double noiseLowPassHz = 0.0; // 0 = no low-pass
    double attackSeconds = 0.0;  // slow attack for brushes
    float drive = 0.0f;
};

std::vector<float> render (const Membrane& m, double sampleRate, std::uint32_t seed)
{
    std::vector<float> out (static_cast<std::size_t> (m.seconds * sampleRate));
    Noise noise (seed);
    HighPass highPass (m.noiseHighPassHz, sampleRate);
    LowPass lowPass (m.noiseLowPassHz > 0.0 ? m.noiseLowPassHz : 20000.0, sampleRate);
    double phase = 0.0, overtonePhase = 0.0;

    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto frequency = m.endHz + (m.startHz - m.endHz) * std::exp (-t / m.pitchTime);
        phase += kTwoPi * frequency / sampleRate;
        overtonePhase += kTwoPi * 1.59 * frequency / sampleRate;

        const auto envelope = std::exp (-t / m.decay);
        const auto body = (std::sin (phase) + m.overtone * std::sin (overtonePhase) * std::exp (-t / (0.6 * m.decay))) * envelope;

        auto n = highPass.process (noise.next());
        if (m.noiseLowPassHz > 0.0)
            n = lowPass.process (n);
        const auto attack = m.attackSeconds > 0.0 ? std::min (1.0, t / m.attackSeconds) : 1.0;
        const auto snap = static_cast<float> (attack * std::exp (-t / m.noiseDecay)) * n;

        out[i] = saturate (static_cast<float> (body) + m.noise * snap, m.drive);
    }
    return out;
}

// Metallic sound: six detuned square waves (as in the TR-808 cymbals) plus noise, an optional
// bell tone.
struct Metal
{
    double seconds;
    double decay;
    double highPassHz;
    float noise;
    float bell = 0.0f;
    double bellHz = 620.0;
    double brightness = 1.6;  // pitch multiplier of the square partials
    double lowPassHz = 0.0;   // 0 = no low-pass (dark cymbals)
};

std::vector<float> render (const Metal& m, double sampleRate, std::uint32_t seed)
{
    static constexpr std::array<double, 6> kPartials { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

    std::vector<float> out (static_cast<std::size_t> (m.seconds * sampleRate));
    Noise noise (seed);
    HighPass highPass (m.highPassHz, sampleRate);
    LowPass lowPass (m.lowPassHz > 0.0 ? m.lowPassHz : 20000.0, sampleRate);
    std::array<double, 6> phases {};

    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        double square = 0.0;
        for (std::size_t k = 0; k < kPartials.size(); ++k)
        {
            phases[k] += kPartials[k] * m.brightness / sampleRate;
            phases[k] -= std::floor (phases[k]);
            square += phases[k] < 0.5 ? 1.0 : -1.0;
        }

        const auto envelope = std::exp (-t / m.decay);
        const auto attack = std::min (1.0, t / 0.0015);
        const auto bell = m.bell * std::sin (kTwoPi * m.bellHz * t) * std::exp (-t / (0.6 * m.decay));
        auto shimmer = highPass.process (static_cast<float> (square / 6.0) + m.noise * noise.next());
        if (m.lowPassHz > 0.0)
            shimmer = lowPass.process (shimmer);
        out[i] = static_cast<float> (attack * envelope) * shimmer + static_cast<float> (bell);
    }
    return out;
}

std::vector<float> synthesiseStudio (Pad pad, double sr)
{
    switch (pad)
    {
        case Pad::snare:    return render (Membrane { 0.35, 240.0, 185.0, 0.012, 0.075, 0.35f, 1.3f, 0.09, 1800.0 }, sr, 11);
        case Pad::highTom:  return render (Membrane { 0.9, 230.0, 185.0, 0.03, 0.30, 0.30f, 0.25f, 0.01 }, sr, 12);
        case Pad::midTom:   return render (Membrane { 1.0, 175.0, 140.0, 0.03, 0.34, 0.30f, 0.25f, 0.01 }, sr, 13);
        case Pad::floorTom: return render (Membrane { 1.2, 120.0, 88.0, 0.04, 0.42, 0.25f, 0.25f, 0.01 }, sr, 14);
        case Pad::hiHat:    return render (Metal { 0.25, 0.045, 7000.0, 0.6f }, sr, 21);
        case Pad::crash:    return render (Metal { 2.8, 0.9, 3500.0, 0.9f }, sr, 22);
        case Pad::ride:     return render (Metal { 2.6, 1.1, 5000.0, 0.3f, 0.35f }, sr, 23);
    }
    return {};
}

std::vector<float> synthesiseRock (Pad pad, double sr)
{
    Membrane snare { 0.45, 200.0, 160.0, 0.015, 0.10, 0.45f, 1.8f, 0.13, 1400.0 };
    snare.drive = 0.8f;
    Membrane highTom { 1.1, 190.0, 145.0, 0.035, 0.38, 0.35f, 0.4f, 0.015 };
    highTom.drive = 0.6f;
    Membrane midTom { 1.2, 150.0, 110.0, 0.035, 0.42, 0.35f, 0.4f, 0.015 };
    midTom.drive = 0.6f;
    Membrane floorTom { 1.5, 100.0, 70.0, 0.045, 0.55, 0.30f, 0.4f, 0.015 };
    floorTom.drive = 0.6f;

    switch (pad)
    {
        case Pad::snare:    return render (snare, sr, 31);
        case Pad::highTom:  return render (highTom, sr, 32);
        case Pad::midTom:   return render (midTom, sr, 33);
        case Pad::floorTom: return render (floorTom, sr, 34);
        case Pad::hiHat:    return render (Metal { 0.32, 0.07, 6000.0, 0.8f }, sr, 41);
        case Pad::crash:    return render (Metal { 3.4, 1.3, 2800.0, 1.0f, 0.0f, 620.0, 1.4 }, sr, 42);
        case Pad::ride:     return render (Metal { 3.0, 1.3, 4000.0, 0.35f, 0.55f, 560.0 }, sr, 43);
    }
    return {};
}

std::vector<float> synthesiseJazz (Pad pad, double sr)
{
    // Brush on the snare: a soft swish of dark noise with a faint body.
    Membrane brush { 0.5, 220.0, 200.0, 0.02, 0.05, 0.0f, 2.5f, 0.16, 600.0, 4500.0, 0.006 };
    Metal ride { 3.6, 1.7, 2500.0, 0.6f, 0.2f, 520.0, 1.2, 6500.0 };
    Metal crash { 2.4, 0.8, 2200.0, 0.8f, 0.0f, 620.0, 1.3, 7000.0 };

    switch (pad)
    {
        case Pad::snare:    return render (brush, sr, 51);
        case Pad::highTom:  return render (Membrane { 0.7, 320.0, 280.0, 0.02, 0.22, 0.25f, 0.15f, 0.008 }, sr, 52);
        case Pad::midTom:   return render (Membrane { 0.8, 250.0, 215.0, 0.02, 0.25, 0.25f, 0.15f, 0.008 }, sr, 53);
        case Pad::floorTom: return render (Membrane { 1.0, 170.0, 140.0, 0.03, 0.32, 0.25f, 0.15f, 0.008 }, sr, 54);
        case Pad::hiHat:    return render (Metal { 0.2, 0.035, 7500.0, 0.4f, 0.0f, 620.0, 1.6, 9000.0 }, sr, 61);
        case Pad::crash:    return render (crash, sr, 62);
        case Pad::ride:     return render (ride, sr, 63);
    }
    return {};
}

std::vector<float> synthesiseElectro (Pad pad, double sr)
{
    // TR-808 style: two-tone snare with a noise burst, pure sine toms with a deep pitch drop.
    switch (pad)
    {
        case Pad::snare:    return render (Membrane { 0.3, 260.0, 185.0, 0.01, 0.08, 0.9f, 1.1f, 0.11, 3000.0 }, sr, 71);
        case Pad::highTom:  return render (Membrane { 0.6, 330.0, 190.0, 0.05, 0.25, 0.0f, 0.0f, 0.01 }, sr, 72);
        case Pad::midTom:   return render (Membrane { 0.7, 250.0, 140.0, 0.05, 0.28, 0.0f, 0.0f, 0.01 }, sr, 73);
        case Pad::floorTom: return render (Membrane { 0.9, 170.0, 90.0, 0.06, 0.35, 0.0f, 0.0f, 0.01 }, sr, 74);
        case Pad::hiHat:    return render (Metal { 0.12, 0.025, 8000.0, 0.2f, 0.0f, 620.0, 2.0 }, sr, 81);
        case Pad::crash:    return render (Metal { 1.8, 0.75, 5000.0, 0.35f, 0.0f, 620.0, 2.0 }, sr, 82);
        case Pad::ride:     return render (Metal { 1.4, 0.55, 6000.0, 0.1f, 0.4f, 760.0, 2.0 }, sr, 83);
    }
    return {};
}

} // namespace

const char* synthKitName (SynthKit kit)
{
    switch (kit)
    {
        case SynthKit::studio:  return "Studio";
        case SynthKit::rock:    return "Rock";
        case SynthKit::jazz:    return "Jazz (brushes)";
        case SynthKit::electro: return "Electro 808";
    }
    return "Studio";
}

std::vector<float> synthesiseDrum (Pad pad, double sampleRate, SynthKit kit)
{
    std::vector<float> out;
    switch (kit)
    {
        case SynthKit::studio:  out = synthesiseStudio (pad, sampleRate); break;
        case SynthKit::rock:    out = synthesiseRock (pad, sampleRate); break;
        case SynthKit::jazz:    out = synthesiseJazz (pad, sampleRate); break;
        case SynthKit::electro: out = synthesiseElectro (pad, sampleRate); break;
    }
    normalise (out);
    return out;
}

SampleBank synthesiseKit (double sampleRate, SynthKit kit)
{
    SampleBank bank;
    for (const auto& info : kDrumKit)
        bank[static_cast<std::size_t> (info.pad)] = synthesiseDrum (info.pad, sampleRate, kit);
    return bank;
}

} // namespace vdrum
