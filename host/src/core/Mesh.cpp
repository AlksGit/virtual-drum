#include "Mesh.h"

#include <cmath>

namespace vdrum
{

void Mesh::addVertex (Vec3 position, Vec3 normal)
{
    vertices.push_back ({ position.x, position.y, position.z, normal.x, normal.y, normal.z });
}

void Mesh::addTriangle (std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    indices.push_back (a);
    indices.push_back (b);
    indices.push_back (c);
}

void append (Mesh& target, const Mesh& source)
{
    const auto offset = static_cast<std::uint32_t> (target.vertices.size());
    target.vertices.insert (target.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (const auto index : source.indices)
        target.indices.push_back (index + offset);
}

namespace
{

// Surface of revolution around Z through the given (radius, z) profile, outward normals.
// The profile goes from bottom to top; triangles wind counter-clockwise seen from outside.
Mesh makeRevolution (const std::vector<std::pair<float, float>>& profile, int segments)
{
    Mesh mesh;
    const auto rings = static_cast<int> (profile.size());

    for (int ring = 0; ring < rings; ++ring)
    {
        // Normal of the profile line, perpendicular to its tangent in the (r, z) plane.
        const auto prev = profile[static_cast<std::size_t> (ring > 0 ? ring - 1 : ring)];
        const auto next = profile[static_cast<std::size_t> (ring < rings - 1 ? ring + 1 : ring)];
        const auto dr = next.first - prev.first;
        const auto dz = next.second - prev.second;
        const auto normalR = dz;
        const auto normalZ = -dr;
        const auto normalLength = std::sqrt (normalR * normalR + normalZ * normalZ);

        for (int s = 0; s <= segments; ++s)
        {
            const auto angle = 2.0f * kPi * static_cast<float> (s) / static_cast<float> (segments);
            const auto c = std::cos (angle);
            const auto sn = std::sin (angle);
            const auto [radius, z] = profile[static_cast<std::size_t> (ring)];
            const Vec3 normal = normalLength > 0.0f ? Vec3 { c * normalR / normalLength, sn * normalR / normalLength, normalZ / normalLength }
                                                    : Vec3 { c, sn, 0.0f };
            mesh.addVertex ({ radius * c, radius * sn, z }, normal);
        }
    }

    const auto stride = static_cast<std::uint32_t> (segments + 1);
    for (int ring = 0; ring < rings - 1; ++ring)
        for (int s = 0; s < segments; ++s)
        {
            const auto a = static_cast<std::uint32_t> (ring) * stride + static_cast<std::uint32_t> (s);
            const auto b = a + 1;
            const auto c = a + stride;
            const auto d = c + 1;
            mesh.addTriangle (a, b, d);
            mesh.addTriangle (a, d, c);
        }

    return mesh;
}

} // namespace

Mesh makeTube (float bottomRadius, float topRadius, float height, int segments)
{
    return makeRevolution ({ { bottomRadius, 0.0f }, { topRadius, height } }, segments);
}

Mesh makeDisc (float radius, float height, int segments, bool facingUp)
{
    Mesh mesh;
    const Vec3 normal { 0.0f, 0.0f, facingUp ? 1.0f : -1.0f };
    mesh.addVertex ({ 0.0f, 0.0f, height }, normal);

    for (int s = 0; s <= segments; ++s)
    {
        const auto angle = 2.0f * kPi * static_cast<float> (s) / static_cast<float> (segments);
        mesh.addVertex ({ radius * std::cos (angle), radius * std::sin (angle), height }, normal);
    }

    for (int s = 1; s <= segments; ++s)
    {
        const auto a = static_cast<std::uint32_t> (s);
        if (facingUp)
            mesh.addTriangle (0, a, a + 1);
        else
            mesh.addTriangle (0, a + 1, a);
    }
    return mesh;
}

Mesh makeCymbal (float radius, int segments)
{
    // Top surface profile from the edge inwards, then the bell: (radius, height).
    const auto rise = 0.06f * radius;
    const auto bellRadius = 0.22f * radius;
    const auto bellHeight = 0.18f * radius;

    // Profile from the edge inwards and up to the bell top: normals point up and out,
    // triangles wind counter-clockwise seen from above.
    auto top = makeRevolution ({ { radius, 0.0f },
                                 { bellRadius, rise },
                                 { bellRadius * 0.85f, rise + 0.6f * bellHeight },
                                 { bellRadius * 0.4f, rise + bellHeight },
                                 { 0.001f, rise + bellHeight } }, segments);

    // Underside, slightly below, from the centre outwards and down: normals point down.
    auto bottom = makeRevolution ({ { 0.001f, rise - 0.004f }, { radius, -0.004f } }, segments);

    append (top, bottom);
    return top;
}

Mesh makeStick (float length, float gripRadius, float tipRadius, int segments)
{
    // Butt, straight shaft, taper, and a small bead at the tip.
    auto mesh = makeRevolution ({ { 0.001f, 0.0f },
                                  { gripRadius, 0.004f },
                                  { gripRadius, 0.70f * length },
                                  { tipRadius, 0.95f * length },
                                  { tipRadius * 1.5f, 0.975f * length },
                                  { 0.001f, length } }, segments);
    return mesh;
}

} // namespace vdrum
