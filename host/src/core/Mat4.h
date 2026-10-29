#pragma once

// Column-major 4x4 matrix for the 3D view (same memory layout as OpenGL expects).

#include "Quaternion.h"

#include <array>
#include <cmath>

namespace vdrum
{

inline Vec3 operator+ (Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator- (Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator* (Vec3 v, float s) { return { v.x * s, v.y * s, v.z * s }; }
inline float dot (Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross (Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline float length (Vec3 v) { return std::sqrt (dot (v, v)); }

inline Vec3 normalise (Vec3 v)
{
    const auto len = length (v);
    return len > 0.0f ? v * (1.0f / len) : Vec3 {};
}

// Unit direction for an azimuth (positive to the left, around Z) and an elevation.
inline Vec3 directionFromAngles (float azimuthRad, float elevationRad)
{
    const auto c = std::cos (elevationRad);
    return { c * std::cos (azimuthRad), c * std::sin (azimuthRad), std::sin (elevationRad) };
}

struct Mat4
{
    std::array<float, 16> m { 1, 0, 0, 0,
                              0, 1, 0, 0,
                              0, 0, 1, 0,
                              0, 0, 0, 1 };

    float& at (int row, int column) { return m[static_cast<std::size_t> (column * 4 + row)]; }
    float at (int row, int column) const { return m[static_cast<std::size_t> (column * 4 + row)]; }

    const float* data() const { return m.data(); }

    static Mat4 identity() { return {}; }

    Mat4 operator* (const Mat4& o) const
    {
        Mat4 r;
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += at (row, k) * o.at (k, column);
                r.at (row, column) = sum;
            }
        return r;
    }

    Vec3 transformPoint (Vec3 p) const
    {
        const auto x = at (0, 0) * p.x + at (0, 1) * p.y + at (0, 2) * p.z + at (0, 3);
        const auto y = at (1, 0) * p.x + at (1, 1) * p.y + at (1, 2) * p.z + at (1, 3);
        const auto z = at (2, 0) * p.x + at (2, 1) * p.y + at (2, 2) * p.z + at (2, 3);
        const auto w = at (3, 0) * p.x + at (3, 1) * p.y + at (3, 2) * p.z + at (3, 3);
        return w != 0.0f ? Vec3 { x / w, y / w, z / w } : Vec3 { x, y, z };
    }

    static Mat4 translation (Vec3 t)
    {
        Mat4 r;
        r.at (0, 3) = t.x;
        r.at (1, 3) = t.y;
        r.at (2, 3) = t.z;
        return r;
    }

    static Mat4 scale (Vec3 s)
    {
        Mat4 r;
        r.at (0, 0) = s.x;
        r.at (1, 1) = s.y;
        r.at (2, 2) = s.z;
        return r;
    }

    static Mat4 rotation (const Quat& q)
    {
        const auto n = q.normalised();
        const auto w = n.w, x = n.x, y = n.y, z = n.z;

        Mat4 r;
        r.at (0, 0) = 1 - 2 * (y * y + z * z);
        r.at (0, 1) = 2 * (x * y - w * z);
        r.at (0, 2) = 2 * (x * z + w * y);
        r.at (1, 0) = 2 * (x * y + w * z);
        r.at (1, 1) = 1 - 2 * (x * x + z * z);
        r.at (1, 2) = 2 * (y * z - w * x);
        r.at (2, 0) = 2 * (x * z - w * y);
        r.at (2, 1) = 2 * (y * z + w * x);
        r.at (2, 2) = 1 - 2 * (x * x + y * y);
        return r;
    }

    // Rotation that maps +Z onto `direction` (used to orient cylinders built along Z).
    static Mat4 alignZ (Vec3 direction)
    {
        const auto zAxis = normalise (direction);
        const auto helper = std::abs (zAxis.z) < 0.99f ? Vec3 { 0, 0, 1 } : Vec3 { 1, 0, 0 };
        const auto xAxis = normalise (cross (helper, zAxis));
        const auto yAxis = cross (zAxis, xAxis);

        Mat4 r;
        r.at (0, 0) = xAxis.x; r.at (0, 1) = yAxis.x; r.at (0, 2) = zAxis.x;
        r.at (1, 0) = xAxis.y; r.at (1, 1) = yAxis.y; r.at (1, 2) = zAxis.y;
        r.at (2, 0) = xAxis.z; r.at (2, 1) = yAxis.z; r.at (2, 2) = zAxis.z;
        return r;
    }

    static Mat4 perspective (float verticalFovRad, float aspect, float nearPlane, float farPlane)
    {
        const auto f = 1.0f / std::tan (0.5f * verticalFovRad);
        Mat4 r;
        r.m.fill (0.0f);
        r.at (0, 0) = f / aspect;
        r.at (1, 1) = f;
        r.at (2, 2) = (farPlane + nearPlane) / (nearPlane - farPlane);
        r.at (2, 3) = 2.0f * farPlane * nearPlane / (nearPlane - farPlane);
        r.at (3, 2) = -1.0f;
        return r;
    }

    static Mat4 lookAt (Vec3 eye, Vec3 target, Vec3 up)
    {
        const auto forward = normalise (target - eye);
        const auto side = normalise (cross (forward, up));
        const auto upAxis = cross (side, forward);

        Mat4 r;
        r.at (0, 0) = side.x;     r.at (0, 1) = side.y;     r.at (0, 2) = side.z;
        r.at (1, 0) = upAxis.x;   r.at (1, 1) = upAxis.y;   r.at (1, 2) = upAxis.z;
        r.at (2, 0) = -forward.x; r.at (2, 1) = -forward.y; r.at (2, 2) = -forward.z;
        r.at (0, 3) = -dot (side, eye);
        r.at (1, 3) = -dot (upAxis, eye);
        r.at (2, 3) = dot (forward, eye);
        return r;
    }

    // Inverse-transpose of the upper 3x3, for transforming normals (rotation + scale only).
    Mat4 normalMatrix() const
    {
        const auto a = at (0, 0), b = at (0, 1), c = at (0, 2);
        const auto d = at (1, 0), e = at (1, 1), f = at (1, 2);
        const auto g = at (2, 0), h = at (2, 1), i = at (2, 2);
        const auto det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        const auto inv = det != 0.0f ? 1.0f / det : 0.0f;

        Mat4 r;
        // Transpose of the inverse = cofactor matrix / det.
        r.at (0, 0) = (e * i - f * h) * inv; r.at (0, 1) = (f * g - d * i) * inv; r.at (0, 2) = (d * h - e * g) * inv;
        r.at (1, 0) = (c * h - b * i) * inv; r.at (1, 1) = (a * i - c * g) * inv; r.at (1, 2) = (b * g - a * h) * inv;
        r.at (2, 0) = (b * f - c * e) * inv; r.at (2, 1) = (c * d - a * f) * inv; r.at (2, 2) = (a * e - b * d) * inv;
        return r;
    }
};

} // namespace vdrum
