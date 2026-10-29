#pragma once

// Deterministic simulation of a drummer playing with the two sticks.
// Produces the packets the receiver would send (ORIENT at 100 Hz, HIT on each stroke,
// STATUS at 1 Hz), from a time axis given by the caller. No threads, no clock: the
// real-time pacing lives in FakeDataSource, which makes this class unit-testable.

#include "DrumKit.h"
#include "Packets.h"
#include "Quaternion.h"

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace vdrum
{

struct FakeDrummerSettings
{
    double bpm = 96.0;
    std::uint64_t seed = 1;
};

class FakeDrummer
{
public:
    static constexpr std::uint64_t kOrientPeriodUs = 10'000;   // 100 Hz per stick
    static constexpr std::uint64_t kStatusPeriodUs = 1'000'000; // 1 Hz per stick
    static constexpr int kBeatsPerBar = 4;
    static constexpr int kBarsPerPhrase = 4; // the last bar of a phrase ends with a fill

    explicit FakeDrummer (FakeDrummerSettings settings = {});

    // Tempo used for the bars scheduled from now on (the bar being played keeps its tempo).
    void setTempo (double bpm);
    double getTempo() const noexcept { return bpm; }

    // Appends, in time order, every frame with a time in (previous call, timeUs].
    // Time is in microseconds since the start of the simulation.
    void generateUntil (std::uint64_t timeUs, std::vector<protocol::Frame>& out);

private:
    struct Stroke
    {
        std::uint64_t timeUs = 0;
        Pad pad = Pad::snare;
        std::uint8_t velocity = 0;
        float azimuthRad = 0.0f;   // actual aim, with a little human scatter
        float elevationRad = 0.0f;
    };

    struct Stick
    {
        std::uint8_t id = 0;
        std::uint16_t seq = 0;
        std::uint32_t clockOffsetUs = 0; // stick clock vs receiver clock
        Stroke previous;                 // last stroke played
        std::deque<Stroke> upcoming;     // scheduled, in time order
        std::uint64_t nextOrientUs = 0;
        std::uint64_t nextStatusUs = 0;
        float rollPhase = 0.0f;
    };

    // Small deterministic PRNG (splitmix64), identical on every platform.
    class Random
    {
    public:
        explicit Random (std::uint64_t seed) : state (seed) {}
        std::uint64_t next();
        float uniform (float low, float high); // [low, high)
        bool chance (float probability);

    private:
        std::uint64_t state;
    };

    void scheduleNextBar();
    void addStroke (std::size_t stickIndex, double beatPosition, Pad pad, int velocity);
    Quat orientationAt (const Stick& stick, std::uint64_t timeUs) const;
    protocol::Frame makeFrame (Stick& stick, std::uint64_t timeUs, protocol::Packet packet);

    Random random;
    double bpm;
    std::array<Stick, 2> sticks;     // 0 = left, 1 = right
    std::uint64_t nextBarStartUs = 0;
    double scheduledBarLengthUs = 0.0;
    int barIndex = 0;
    std::uint64_t lastTimeUs = 0;
    bool started = false;
};

} // namespace vdrum
