#pragma once

// What the 3D view needs to know, shared between the data source thread (writer) and the
// rendering thread (reader). All methods are thread-safe; the critical sections only copy
// a few dozen bytes, so neither side waits noticeably.

#include "DrumKit.h"
#include "Packets.h"
#include "Quaternion.h"

#include <array>
#include <chrono>
#include <mutex>

namespace vdrum
{

// Common clock of the data source listener and the renderer, in seconds.
inline double steadySeconds()
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Pad aimed at by a stick orientation: the zone with the nearest azimuth / elevation.
inline Pad nearestPad (const Quat& orientation)
{
    const auto azimuth = radToDeg (orientation.azimuthRad());
    const auto elevation = radToDeg (orientation.elevationRad());

    auto best = Pad::snare;
    auto bestDistanceSquared = 1.0e12f;
    for (const auto& info : kDrumKit)
    {
        const auto da = azimuth - info.azimuthDeg;
        const auto de = elevation - info.elevationDeg;
        const auto distanceSquared = da * da + de * de;
        if (distanceSquared < bestDistanceSquared)
        {
            bestDistanceSquared = distanceSquared;
            best = info.pad;
        }
    }
    return best;
}

class SceneState
{
public:
    static constexpr int kStickCount = 2;

    struct PadHit
    {
        double timeSeconds = -1.0e9; // last hit, on the caller's clock
        float velocity = 0.0f;       // 0..1
    };

    struct Snapshot
    {
        std::array<Quat, kStickCount> sticks {};
        std::array<bool, kStickCount> stickSeen {};
        std::array<PadHit, kDrumKit.size()> pads {};
        std::uint64_t hitCount = 0;
    };

    // Applies a received frame. `nowSeconds` is the time of reception on the caller's clock
    // (the same clock the renderer uses to animate hits).
    void apply (const protocol::Frame& frame, double nowSeconds)
    {
        if (const auto* orient = std::get_if<protocol::OrientPacket> (&frame.packet))
        {
            setStick (orient->header.stickId, Quat::fromQ14 (orient->quat));
        }
        else if (const auto* hit = std::get_if<protocol::HitPacket> (&frame.packet))
        {
            const auto orientation = Quat::fromQ14 (hit->quat);
            const auto pad = static_cast<std::size_t> (nearestPad (orientation));

            const std::lock_guard<std::mutex> lock (mutex);
            if (hit->header.stickId < kStickCount)
            {
                state.sticks[hit->header.stickId] = orientation;
                state.stickSeen[hit->header.stickId] = true;
            }
            state.pads[pad].timeSeconds = nowSeconds;
            state.pads[pad].velocity = static_cast<float> (hit->velocity) / 127.0f;
            ++state.hitCount;
        }
    }

    Snapshot snapshot() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return state;
    }

private:
    void setStick (std::uint8_t stickId, const Quat& orientation)
    {
        if (stickId >= kStickCount)
            return;

        const std::lock_guard<std::mutex> lock (mutex);
        state.sticks[stickId] = orientation;
        state.stickSeen[stickId] = true;
    }

    mutable std::mutex mutex;
    Snapshot state;
};

} // namespace vdrum
