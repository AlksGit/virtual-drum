#include "TestFramework.h"

#include "DrumKit.h"
#include "FakeDataSource.h"
#include "FakeDrummer.h"
#include "Quaternion.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <thread>

using namespace vdrum;
using namespace vdrum::protocol;

namespace
{

std::vector<Frame> simulate (FakeDrummerSettings settings, std::uint64_t durationUs)
{
    FakeDrummer drummer (settings);
    std::vector<Frame> frames;
    // Feed the time axis in uneven steps, as the real-time loop would.
    for (std::uint64_t t = 0; t <= durationUs; t += 997)
        drummer.generateUntil (t, frames);
    return frames;
}

const Header& headerOf (const Frame& frame)
{
    return std::visit ([] (const auto& packet) -> const Header& { return packet.header; }, frame.packet);
}

std::vector<const HitPacket*> hitsOf (const std::vector<Frame>& frames, int stickId = -1)
{
    std::vector<const HitPacket*> hits;
    for (const auto& frame : frames)
        if (const auto* hit = std::get_if<HitPacket> (&frame.packet))
            if (stickId < 0 || hit->header.stickId == stickId)
                hits.push_back (hit);
    return hits;
}

// Nearest pad of the kit to the stick orientation, and the angular distance to it.
std::pair<Pad, float> nearestPad (const QuatQ14& q)
{
    const auto quat = Quat::fromQ14 (q);
    const auto azimuth = radToDeg (quat.azimuthRad());
    const auto elevation = radToDeg (quat.elevationRad());

    auto best = Pad::snare;
    auto bestDistance = 1.0e9f;
    for (const auto& pad : kDrumKit)
    {
        const auto distance = std::hypot (azimuth - pad.azimuthDeg, elevation - pad.elevationDeg);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = pad.pad;
        }
    }
    return { best, bestDistance };
}

class CollectingListener final : public DataSource::Listener
{
public:
    void frameReceived (const Frame&) override { ++frames; }
    void sourceFailed (const std::string&) override { failed = true; }

    std::atomic<int> frames { 0 };
    std::atomic<bool> failed { false };
};

} // namespace

VDRUM_TEST (fakeDrummerIsDeterministic)
{
    const auto a = simulate ({ 100.0, 42 }, 5'000'000);
    const auto b = simulate ({ 100.0, 42 }, 5'000'000);
    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK (encodeFrame (a[i]) == encodeFrame (b[i]));

    const auto c = simulate ({ 100.0, 43 }, 5'000'000);
    CHECK (hitsOf (c).size() > 0);
}

VDRUM_TEST (orientationStreamsRunAt100HzPerStick)
{
    const auto frames = simulate ({}, 10'000'000);
    std::map<int, int> orientCount;
    for (const auto& frame : frames)
        if (std::holds_alternative<OrientPacket> (frame.packet))
            ++orientCount[headerOf (frame).stickId];

    CHECK (std::abs (orientCount[0] - 1000) <= 1);
    CHECK (std::abs (orientCount[1] - 1000) <= 1);
}

VDRUM_TEST (statusIsSentOncePerSecondPerStick)
{
    const auto frames = simulate ({}, 10'000'000);
    int statusCount = 0;
    for (const auto& frame : frames)
        if (const auto* status = std::get_if<StatusPacket> (&frame.packet))
        {
            ++statusCount;
            CHECK (status->vbatMv > 3300 && status->vbatMv <= 4200);
        }
    CHECK (statusCount == 20);
}

VDRUM_TEST (framesAreInTimeOrderAndSeqIncrementsPerStick)
{
    const auto frames = simulate ({}, 8'000'000);
    std::map<int, int> lastSeq;
    std::uint32_t lastRx = 0;

    for (const auto& frame : frames)
    {
        CHECK (frame.rxTimeUs >= lastRx);
        lastRx = frame.rxTimeUs;

        const auto& header = headerOf (frame);
        if (lastSeq.count (header.stickId) != 0)
            CHECK (header.seq == static_cast<std::uint16_t> (lastSeq[header.stickId] + 1));
        lastSeq[header.stickId] = header.seq;
    }
}

VDRUM_TEST (grooveHasTheExpectedNumberOfHits)
{
    // 120 bpm: one 4/4 bar = 2 s. Lead-in 0.5 s, then 8 bars = 2 phrases.
    const auto frames = simulate ({ 120.0, 7 }, 500'000 + 16'000'000 - 50'000);
    const auto right = hitsOf (frames, 1).size();
    const auto left = hitsOf (frames, 0).size();

    // Right: 8 eighths per normal bar, 4 + 4 fill strokes in fill bars -> 64.
    CHECK (right == 64);
    // Left: 2 backbeats per normal bar, 1 + 4 fill strokes in fill bars, plus ghosts.
    CHECK (left >= 6 * 2 + 2 * 5);
    CHECK (left <= 6 * 2 + 2 * 5 + 8);
}

VDRUM_TEST (hitsAimAtAPadAndHaveValidVelocity)
{
    const auto frames = simulate ({ 110.0, 3 }, 20'000'000);
    const auto hits = hitsOf (frames);
    REQUIRE (hits.size() > 50);

    std::map<Pad, int> padsHit;
    for (const auto* hit : hits)
    {
        CHECK (hit->velocity >= 1 && hit->velocity <= 127);
        const auto [pad, distance] = nearestPad (hit->quat);
        CHECK (distance < 5.0f);
        ++padsHit[pad];
    }

    // The groove and its fills visit every pad of the kit.
    CHECK (padsHit.size() == kDrumKit.size());
}

VDRUM_TEST (quaternionsAreUnitLength)
{
    const auto frames = simulate ({}, 4'000'000);
    for (const auto& frame : frames)
    {
        const QuatQ14* q = nullptr;
        if (const auto* hit = std::get_if<HitPacket> (&frame.packet))
            q = &hit->quat;
        else if (const auto* orient = std::get_if<OrientPacket> (&frame.packet))
            q = &orient->quat;
        if (q == nullptr)
            continue;

        const Quat raw { q14ToFloat ((*q)[0]), q14ToFloat ((*q)[1]), q14ToFloat ((*q)[2]), q14ToFloat ((*q)[3]) };
        CHECK (std::abs (raw.norm() - 1.0f) < 0.002f);
    }
}

VDRUM_TEST (sticksRiseBetweenStrokes)
{
    // Between two hits the tip goes clearly above the pad, then comes back down onto it.
    const auto frames = simulate ({ 90.0, 5 }, 6'000'000);
    float minElevation = 90.0f;
    float maxElevation = -90.0f;
    for (const auto& frame : frames)
        if (const auto* orient = std::get_if<OrientPacket> (&frame.packet))
            if (orient->header.stickId == 0)
            {
                const auto elevation = radToDeg (Quat::fromQ14 (orient->quat).elevationRad());
                minElevation = std::min (minElevation, elevation);
                maxElevation = std::max (maxElevation, elevation);
            }

    CHECK (maxElevation - minElevation > 25.0f);
}

VDRUM_TEST (tempoChangeAppliesToLaterBars)
{
    FakeDrummer drummer ({ 120.0, 9 });
    std::vector<Frame> frames;
    drummer.generateUntil (10'000'000, frames);
    drummer.setTempo (60.0);
    frames.clear();
    drummer.generateUntil (40'000'000, frames);

    // At 60 bpm, consecutive right-hand eighths are ~500 ms apart (they were 250 ms).
    const auto hits = hitsOf (frames, 1);
    REQUIRE (hits.size() > 10);
    const auto last = hits.back()->tUs;
    const auto previous = hits[hits.size() - 2]->tUs;
    const auto intervalMs = static_cast<double> (last - previous) / 1000.0;
    CHECK (intervalMs > 450.0 && intervalMs < 550.0);
}

VDRUM_TEST (q14RoundTripPreservesTheAim)
{
    const auto quat = Quat::fromAzimuthElevation (degToRad (-40.0f), degToRad (-30.0f), degToRad (3.0f));
    const auto back = Quat::fromQ14 (quat.toQ14());
    CHECK (std::abs (radToDeg (back.azimuthRad()) + 40.0f) < 0.1f);
    CHECK (std::abs (radToDeg (back.elevationRad()) + 30.0f) < 0.1f);
}

VDRUM_TEST (fakeSourceDeliversFramesInRealTime)
{
    FakeDataSource source ({ 120.0, 1 });
    CollectingListener listener;
    std::string error;
    REQUIRE (source.start (listener, error));
    CHECK (source.isRunning());

    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    source.stop();

    // 2 sticks x 100 Hz over 0.4 s = ~80 ORIENT frames; loose bounds for busy CI machines.
    CHECK (listener.frames > 40);
    CHECK (listener.frames < 200);
    CHECK (! listener.failed);
    CHECK (! source.isRunning());
    CHECK (source.getStats().lengthErrors == 0);
}
