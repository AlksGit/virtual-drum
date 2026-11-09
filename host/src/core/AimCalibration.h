#pragma once

// Per-stick aim correction. The sticks measure pitch and roll against gravity, but their yaw
// starts wherever they lay at power-up and drifts slowly (docs/ARCHITECTURE.md), and the height
// the player hits at depends on where they sit. Recentering takes the latest orientation of each
// stick, assumed to point at a target pad (the snare), and from then on adds the same azimuth
// and elevation offsets to that stick's tip direction (roll kept), so the angles between pads
// stay those of the kit (DrumKit.h).
// A stick restarts after each sleep (System OFF wakes it through a reset) with its yaw back to
// zero: the heading it has when it wakes reads straight ahead. Its azimuth offset is then
// dropped (the elevation offset, set by the seat height, is kept) until the next recenter.
// Thread-safe: frames arrive on the source thread, recenter() comes from the UI.

#include "Packets.h"
#include "Quaternion.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <variant>

namespace vdrum
{

class AimCalibration
{
public:
    static constexpr int kStickCount = 2;

    // Corrects the orientation of a HIT or ORIENT frame in place, after noting the raw one.
    void apply (protocol::Frame& frame)
    {
        std::visit ([&] (const auto& packet) { trackRestart (packet.header, frame.rxTimeUs); }, frame.packet);
        if (auto* orient = std::get_if<protocol::OrientPacket> (&frame.packet))
            correct (orient->header.stickId, orient->quat);
        else if (auto* hit = std::get_if<protocol::HitPacket> (&frame.packet))
            correct (hit->header.stickId, hit->quat);
    }

    // Makes every stick seen so far point at (azimuth, elevation), in degrees, from now on.
    // Returns the number of sticks recentered.
    int recenter (float azimuthDeg, float elevationDeg)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        int count = 0;
        for (int i = 0; i < kStickCount; ++i)
        {
            if (! seen[i])
                continue;
            offsets[i] = { degToRad (azimuthDeg) - latest[i].azimuthRad(),
                           degToRad (elevationDeg) - latest[i].elevationRad() };
            ++count;
        }
        return count;
    }

private:
    // Restart = seq going backwards, or a silence ended by a seq back near 0 (the seq wraps
    // forwards at 2^16 while a stick streams; a radio hole keeps it going forwards).
    static constexpr std::uint32_t kRestartSilenceUs = 500000;
    static constexpr std::uint16_t kRestartSeq = 300; // ~3 s of packets after a boot

    void trackRestart (const protocol::Header& header, std::uint32_t rxTimeUs)
    {
        if (header.stickId >= kStickCount)
            return;
        const std::lock_guard<std::mutex> lock (mutex);
        auto& last = lastPacket[header.stickId];
        if (last.valid)
        {
            const auto forward = static_cast<std::uint16_t> (header.seq - last.seq);
            const bool backwards = forward > 0x8000u;
            const bool silent = rxTimeUs - last.rxTimeUs > kRestartSilenceUs;
            if (backwards || (silent && header.seq < kRestartSeq))
                offsets[header.stickId].azimuthRad = 0.0f;
        }
        last = { true, header.seq, rxTimeUs };
    }

    struct LastPacket
    {
        bool valid = false;
        std::uint16_t seq = 0;
        std::uint32_t rxTimeUs = 0;
    };

    struct Offset
    {
        float azimuthRad = 0.0f;
        float elevationRad = 0.0f;
    };

    void correct (std::uint8_t stickId, protocol::QuatQ14& quat)
    {
        if (stickId >= kStickCount)
            return;
        const std::lock_guard<std::mutex> lock (mutex);
        const auto raw = Quat::fromQ14 (quat);
        latest[stickId] = raw;
        seen[stickId] = true;

        const auto& offset = offsets[stickId];
        if (offset.azimuthRad == 0.0f && offset.elevationRad == 0.0f)
            return;
        // raw = direction (azimuth, elevation) * roll: swap the direction, keep the roll.
        constexpr auto kMaxElevation = 1.5533f; // 89 degrees: the azimuth is undefined at the vertical
        const auto azimuth = raw.azimuthRad();
        const auto elevation = raw.elevationRad();
        const auto shifted = std::clamp (elevation + offset.elevationRad, -kMaxElevation, kMaxElevation);
        const auto from = Quat::fromAzimuthElevation (azimuth, elevation);
        const auto to = Quat::fromAzimuthElevation (azimuth + offset.azimuthRad, shifted);
        quat = (to * from.conjugate() * raw).normalised().toQ14();
    }

    std::mutex mutex;
    std::array<Quat, kStickCount> latest {};
    std::array<bool, kStickCount> seen {};
    std::array<Offset, kStickCount> offsets {}; // zero until recentered
    std::array<LastPacket, kStickCount> lastPacket {};
};

} // namespace vdrum
