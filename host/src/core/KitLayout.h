#pragma once

// Physical placement of the kit in the world, derived from the hit zones of DrumKit.h:
// each pad sits at arm's reach from the player's centre, in the direction given by its
// azimuth / elevation. The stick is drawn ending at that point, so the hand moves along
// the ray as the arm would. Units are metres, world frame as in DrumKit.h.

#include "DrumKit.h"
#include "Mat4.h"

namespace vdrum
{

enum class PadShape
{
    drum,
    cymbal
};

struct PadPlacement
{
    Pad pad;
    PadShape shape;
    Vec3 strikeCentre;  // centre of the striking surface (drum head / cymbal top)
    Vec3 surfaceNormal; // facing up and slightly towards the player
    float radius;
    float depth;        // shell depth for drums, thickness for cymbals
};

struct KitLayout
{
    static constexpr Vec3 kPivot { 0.0f, 0.0f, 0.85f }; // player's centre, at hand height
    static constexpr float kReach = 0.80f;              // pivot to stick tip (arm + stick)
    static constexpr float kStickLength = 0.42f;        // grip to tip
    static constexpr float kTipSpacing = 0.06f;         // keeps two sticks on one pad apart

    // Where the tip of a stick pointing at (azimuth, elevation) is.
    static Vec3 tipPosition (float azimuthRad, float elevationRad)
    {
        return kPivot + directionFromAngles (azimuthRad, elevationRad) * kReach;
    }

    // Tip and grip of a stick for the 3D view. Left stick = 0, right = 1.
    static void stickEnds (int stickId, const Quat& orientation, Vec3& tip, Vec3& grip)
    {
        const auto direction = orientation.tipDirection();
        const auto side = normalise (cross (Vec3 { 0, 0, 1 }, direction)); // to the stick's left
        const auto offset = side * (stickId == 0 ? 0.5f * kTipSpacing : -0.5f * kTipSpacing);

        tip = kPivot + direction * kReach + offset;
        grip = tip - direction * kStickLength;
    }

    static PadPlacement placement (Pad pad)
    {
        const auto& info = padInfo (pad);
        const auto centre = tipPosition (degToRad (info.azimuthDeg), degToRad (info.elevationDeg));

        // Tilt the surface a little towards the player, like a real kit.
        const auto towardsPlayer = normalise (Vec3 { kPivot.x - centre.x, kPivot.y - centre.y, 0.0f });

        switch (pad)
        {
            case Pad::hiHat:    return { pad, PadShape::cymbal, centre, normalise (Vec3 { 0, 0, 1 } + towardsPlayer * 0.10f), 0.18f, 0.008f };
            case Pad::snare:    return { pad, PadShape::drum,   centre, normalise (Vec3 { 0, 0, 1 } + towardsPlayer * 0.15f), 0.18f, 0.14f };
            case Pad::highTom:  return { pad, PadShape::drum,   centre, normalise (Vec3 { 0, 0, 1 } + towardsPlayer * 0.45f), 0.14f, 0.16f };
            case Pad::midTom:   return { pad, PadShape::drum,   centre, normalise (Vec3 { 0, 0, 1 } + towardsPlayer * 0.45f), 0.16f, 0.18f };
            case Pad::floorTom: return { pad, PadShape::drum,   centre, normalise (Vec3 { 0, 0, 1 } + towardsPlayer * 0.05f), 0.20f, 0.36f };
            case Pad::crash:    return { pad, PadShape::cymbal, centre, normalise (Vec3 { 0, 0, 1 } + towardsPlayer * 0.35f), 0.23f, 0.008f };
            case Pad::ride:     return { pad, PadShape::cymbal, centre, normalise (Vec3 { 0, 0, 1 } + towardsPlayer * 0.25f), 0.26f, 0.008f };
        }
        return { pad, PadShape::drum, centre, Vec3 { 0, 0, 1 }, 0.15f, 0.15f };
    }
};

} // namespace vdrum
