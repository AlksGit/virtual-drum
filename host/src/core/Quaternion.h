#pragma once

// Minimal unit quaternion math for stick orientations.
//
// Convention (see docs/PROTOCOL.md): the quaternion rotates the stick body frame into the
// world frame. Body X points along the stick towards the tip. World frame: X forward from
// the player towards the kit, Y to the player's left, Z up.

#include "Packets.h"

#include <cmath>

namespace vdrum
{

struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Quat
{
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;

    static Quat fromAxisAngle (Vec3 axis, float angleRad)
    {
        const auto half = 0.5f * angleRad;
        const auto s = std::sin (half);
        return { std::cos (half), axis.x * s, axis.y * s, axis.z * s };
    }

    // Stick pose from tip direction: azimuth (yaw, positive to the left), elevation
    // (positive = tip up) and roll around the stick axis, all in radians.
    static Quat fromAzimuthElevation (float azimuthRad, float elevationRad, float rollRad = 0.0f)
    {
        return fromAxisAngle ({ 0.0f, 0.0f, 1.0f }, azimuthRad)
             * fromAxisAngle ({ 0.0f, 1.0f, 0.0f }, -elevationRad)
             * fromAxisAngle ({ 1.0f, 0.0f, 0.0f }, rollRad);
    }

    static Quat fromQ14 (const protocol::QuatQ14& q)
    {
        return Quat { protocol::q14ToFloat (q[0]), protocol::q14ToFloat (q[1]),
                      protocol::q14ToFloat (q[2]), protocol::q14ToFloat (q[3]) }.normalised();
    }

    protocol::QuatQ14 toQ14() const
    {
        return { protocol::floatToQ14 (w), protocol::floatToQ14 (x), protocol::floatToQ14 (y), protocol::floatToQ14 (z) };
    }

    float norm() const { return std::sqrt (w * w + x * x + y * y + z * z); }

    Quat normalised() const
    {
        const auto n = norm();
        return n > 0.0f ? Quat { w / n, x / n, y / n, z / n } : Quat {};
    }

    Quat conjugate() const { return { w, -x, -y, -z }; }

    Quat operator* (const Quat& o) const
    {
        return { w * o.w - x * o.x - y * o.y - z * o.z,
                 w * o.x + x * o.w + y * o.z - z * o.y,
                 w * o.y - x * o.z + y * o.w + z * o.x,
                 w * o.z + x * o.y - y * o.x + z * o.w };
    }

    Vec3 rotate (Vec3 v) const
    {
        const auto r = *this * Quat { 0.0f, v.x, v.y, v.z } * conjugate();
        return { r.x, r.y, r.z };
    }

    // Direction of the stick tip in the world frame.
    Vec3 tipDirection() const { return rotate ({ 1.0f, 0.0f, 0.0f }); }

    float azimuthRad() const
    {
        const auto tip = tipDirection();
        return std::atan2 (tip.y, tip.x);
    }

    float elevationRad() const
    {
        const auto tip = tipDirection();
        return std::asin (tip.z < -1.0f ? -1.0f : (tip.z > 1.0f ? 1.0f : tip.z));
    }
};

constexpr float kPi = 3.14159265358979f;

constexpr float degToRad (float degrees) { return degrees * kPi / 180.0f; }
constexpr float radToDeg (float radians) { return radians * 180.0f / kPi; }

} // namespace vdrum
