#include "FakeDrummer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vdrum
{

namespace
{

constexpr std::uint64_t kLeadInUs = 500'000;      // sticks at rest before the first bar
constexpr std::uint64_t kLinkLatencyUs = 1'500;   // fake radio + USB delay for rx_time_us
constexpr float kTimingJitterUs = 2'500.0f;
constexpr float kVelocityJitter = 7.0f;
constexpr float kAimScatterAzimuthDeg = 2.0f;
constexpr float kAimScatterElevationDeg = 1.5f;
constexpr double kMinBpm = 40.0;
constexpr double kMaxBpm = 240.0;

constexpr std::size_t kLeft = 0;
constexpr std::size_t kRight = 1;

float smoothstep (float x)
{
    x = std::clamp (x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

float lerp (float a, float b, float t)
{
    return a + (b - a) * t;
}

} // namespace

std::uint64_t FakeDrummer::Random::next()
{
    state += 0x9E3779B97F4A7C15ull;
    auto z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

float FakeDrummer::Random::uniform (float low, float high)
{
    const auto unit = static_cast<float> (next() >> 40) / static_cast<float> (1ull << 24);
    return low + (high - low) * unit;
}

bool FakeDrummer::Random::chance (float probability)
{
    return uniform (0.0f, 1.0f) < probability;
}

FakeDrummer::FakeDrummer (FakeDrummerSettings settings)
    : random (settings.seed),
      bpm (std::clamp (settings.bpm, kMinBpm, kMaxBpm))
{
    const auto& snare = padInfo (Pad::snare);

    for (std::size_t i = 0; i < sticks.size(); ++i)
    {
        auto& stick = sticks[i];
        stick.id = static_cast<std::uint8_t> (i);
        stick.clockOffsetUs = static_cast<std::uint32_t> (random.next() & 0xFFFFFFu);
        stick.rollPhase = random.uniform (0.0f, 2.0f * kPi);

        // Resting just above the snare, slightly apart.
        stick.previous.timeUs = 0;
        stick.previous.pad = Pad::snare;
        stick.previous.azimuthRad = degToRad (snare.azimuthDeg + (i == kLeft ? 6.0f : -6.0f));
        stick.previous.elevationRad = degToRad (snare.elevationDeg - 5.0f);

        // Stagger the two streams, as two independent radios would be.
        stick.nextOrientUs = i * (kOrientPeriodUs / 2);
        stick.nextStatusUs = 200'000 + i * (kStatusPeriodUs / 2);
    }

    nextBarStartUs = kLeadInUs;
}

void FakeDrummer::setTempo (double newBpm)
{
    bpm = std::clamp (newBpm, kMinBpm, kMaxBpm);
}

void FakeDrummer::addStroke (std::size_t stickIndex, double beatPosition, Pad pad, int velocity)
{
    const auto beatUs = 60.0e6 / bpm;
    const auto jitter = static_cast<double> (random.uniform (-kTimingJitterUs, kTimingJitterUs));
    const auto timeUs = static_cast<double> (nextBarStartUs) + beatPosition * beatUs + jitter;

    const auto& info = padInfo (pad);
    Stroke stroke;
    stroke.timeUs = static_cast<std::uint64_t> (std::max (0.0, timeUs));
    stroke.pad = pad;
    stroke.velocity = static_cast<std::uint8_t> (std::clamp (
        static_cast<int> (std::lround (static_cast<float> (velocity) + random.uniform (-kVelocityJitter, kVelocityJitter))), 1, 127));
    stroke.azimuthRad = degToRad (info.azimuthDeg + random.uniform (-kAimScatterAzimuthDeg, kAimScatterAzimuthDeg));
    stroke.elevationRad = degToRad (info.elevationDeg + random.uniform (-kAimScatterElevationDeg, kAimScatterElevationDeg));

    sticks[stickIndex].upcoming.push_back (stroke);
}

void FakeDrummer::scheduleNextBar()
{
    const auto barInPhrase = barIndex % kBarsPerPhrase;
    const bool fillBar = barInPhrase == kBarsPerPhrase - 1;
    const bool crashOnOne = barInPhrase == 0 && barIndex > 0;
    const bool useRide = (barIndex / kBarsPerPhrase) % 2 == 1;

    // Right hand: eighth notes on the hi-hat (or the ride every other phrase).
    for (int eighth = 0; eighth < 2 * kBeatsPerBar; ++eighth)
    {
        if (fillBar && eighth >= 4)
            break; // the fill takes over on beat 3

        auto pad = useRide ? Pad::ride : Pad::hiHat;
        auto velocity = (eighth % 2 == 0) ? 92 : 68;
        if (eighth == 0 && crashOnOne)
        {
            pad = Pad::crash;
            velocity = 118;
        }
        addStroke (kRight, 0.5 * eighth, pad, velocity);
    }

    // Left hand: backbeat on 2 and 4, sometimes a ghost note before beat 3.
    addStroke (kLeft, 1.0, Pad::snare, 108);
    if (random.chance (0.35f))
        addStroke (kLeft, 1.75, Pad::snare, 30);
    if (! fillBar)
        addStroke (kLeft, 3.0, Pad::snare, 108);

    // Fill: sixteenth notes on beats 3-4, alternating hands, down the toms, crescendo.
    if (fillBar)
    {
        static constexpr std::array<Pad, 8> kFillPads { Pad::snare, Pad::snare, Pad::highTom, Pad::highTom,
                                                        Pad::midTom, Pad::midTom, Pad::floorTom, Pad::floorTom };
        for (std::size_t i = 0; i < kFillPads.size(); ++i)
            addStroke (i % 2 == 0 ? kRight : kLeft, 2.0 + 0.25 * static_cast<double> (i), kFillPads[i],
                       78 + 5 * static_cast<int> (i));
    }

    for (auto& stick : sticks)
        std::sort (stick.upcoming.begin(), stick.upcoming.end(),
                   [] (const Stroke& a, const Stroke& b) { return a.timeUs < b.timeUs; });

    nextBarStartUs += static_cast<std::uint64_t> (std::lround (kBeatsPerBar * 60.0e6 / bpm));
    ++barIndex;
}

Quat FakeDrummer::orientationAt (const Stick& stick, std::uint64_t timeUs) const
{
    const auto& from = stick.previous;
    const auto& to = stick.upcoming.front(); // generateUntil keeps at least one scheduled

    const auto roll = degToRad (4.0f) * std::sin (stick.rollPhase + 2.0f * kPi * 0.3f * static_cast<float> (timeUs) * 1.0e-6f);

    if (timeUs <= from.timeUs || to.timeUs <= from.timeUs)
        return Quat::fromAzimuthElevation (from.azimuthRad, from.elevationRad, roll);

    const auto intervalUs = static_cast<float> (to.timeUs - from.timeUs);
    const auto phase = std::clamp (static_cast<float> (timeUs - from.timeUs) / intervalUs, 0.0f, 1.0f);

    // Lift between strokes: higher before a loud stroke, lower when strokes are close.
    const auto liftDeg = (12.0f + 38.0f * static_cast<float> (to.velocity) / 127.0f)
                       * std::min (1.0f, intervalUs / 300'000.0f);

    // The peak comes late (phase^1.6), so the downstroke is quicker than the upstroke.
    const auto elevation = lerp (from.elevationRad, to.elevationRad, smoothstep (phase))
                         + degToRad (liftDeg) * std::sin (kPi * std::pow (phase, 1.6f));
    const auto azimuth = lerp (from.azimuthRad, to.azimuthRad, smoothstep (phase * 1.3f));

    return Quat::fromAzimuthElevation (azimuth, elevation, roll);
}

protocol::Frame FakeDrummer::makeFrame (Stick& stick, std::uint64_t timeUs, protocol::Packet packet)
{
    std::visit ([&stick] (auto& p)
    {
        p.header.stickId = stick.id;
        p.header.seq = stick.seq++;
    }, packet);

    protocol::Frame frame;
    frame.rxTimeUs = static_cast<std::uint32_t> (timeUs + kLinkLatencyUs);
    frame.rssi = static_cast<std::int8_t> (std::lround (random.uniform (-51.0f, -39.0f)));
    frame.packet = std::move (packet);
    return frame;
}

void FakeDrummer::generateUntil (std::uint64_t timeUs, std::vector<protocol::Frame>& out)
{
    // Keep strokes scheduled at least one bar ahead, so every stick always has a target.
    const auto barLengthUs = static_cast<std::uint64_t> (kBeatsPerBar * 60.0e6 / bpm);
    while (nextBarStartUs <= timeUs + barLengthUs
           || sticks[kLeft].upcoming.empty() || sticks[kRight].upcoming.empty())
        scheduleNextBar();

    for (;;)
    {
        enum class Kind { hit, orient, status };
        std::size_t stickIndex = 0;
        auto kind = Kind::orient;
        auto earliest = std::numeric_limits<std::uint64_t>::max();

        // Strict comparisons give hits priority over orientation at the same instant.
        for (std::size_t i = 0; i < sticks.size(); ++i)
        {
            const auto& stick = sticks[i];
            if (! stick.upcoming.empty() && stick.upcoming.front().timeUs < earliest)
                { earliest = stick.upcoming.front().timeUs; kind = Kind::hit; stickIndex = i; }
            if (stick.nextOrientUs < earliest)
                { earliest = stick.nextOrientUs; kind = Kind::orient; stickIndex = i; }
            if (stick.nextStatusUs < earliest)
                { earliest = stick.nextStatusUs; kind = Kind::status; stickIndex = i; }
        }

        if (earliest > timeUs)
            break;

        auto& stick = sticks[stickIndex];
        const auto stickClock = static_cast<std::uint32_t> (earliest + stick.clockOffsetUs);

        switch (kind)
        {
            case Kind::hit:
            {
                const auto stroke = stick.upcoming.front();
                stick.upcoming.pop_front();
                stick.previous = stroke;

                if (stick.upcoming.empty())
                    scheduleNextBar();

                const auto roll = degToRad (4.0f) * std::sin (stick.rollPhase + 2.0f * kPi * 0.3f * static_cast<float> (earliest) * 1.0e-6f);

                protocol::HitPacket hit;
                hit.tUs = stickClock;
                hit.velocity = stroke.velocity;
                hit.quat = Quat::fromAzimuthElevation (stroke.azimuthRad, stroke.elevationRad, roll).toQ14();
                out.push_back (makeFrame (stick, earliest, hit));
                break;
            }

            case Kind::orient:
            {
                protocol::OrientPacket orient;
                orient.tUs = stickClock;
                orient.quat = orientationAt (stick, earliest).toQ14();
                stick.nextOrientUs += kOrientPeriodUs;
                out.push_back (makeFrame (stick, earliest, orient));
                break;
            }

            case Kind::status:
            {
                protocol::StatusPacket status;
                status.vbatMv = static_cast<std::uint16_t> (4150 - std::min<std::uint64_t> (earliest / 10'000'000, 800));
                status.gyroBias = { static_cast<std::int16_t> (stick.id == kLeft ? 3 : -2), -1, 4 };
                status.fwVersion = 0;
                stick.nextStatusUs += kStatusPeriodUs;
                out.push_back (makeFrame (stick, earliest, status));
                break;
            }
        }
    }
}

} // namespace vdrum
