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

Mesh makeLathe (const std::vector<std::pair<float, float>>& profile, int segments)
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

Mesh makeTube (float bottomRadius, float topRadius, float height, int segments)
{
    return makeLathe ({ { bottomRadius, 0.0f }, { topRadius, height } }, segments);
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
    auto top = makeLathe ({ { radius, 0.0f },
                                 { bellRadius, rise },
                                 { bellRadius * 0.85f, rise + 0.6f * bellHeight },
                                 { bellRadius * 0.4f, rise + bellHeight },
                                 { 0.001f, rise + bellHeight } }, segments);

    // Underside, slightly below, from the centre outwards and down: normals point down.
    auto bottom = makeLathe ({ { 0.001f, rise - 0.004f }, { radius, -0.004f } }, segments);

    append (top, bottom);
    return top;
}

Mesh makeStick (float length, float gripRadius, float tipRadius, int segments)
{
    // Butt, straight shaft, taper, and a small bead at the tip.
    auto mesh = makeLathe ({ { 0.001f, 0.0f },
                                  { gripRadius, 0.004f },
                                  { gripRadius, 0.70f * length },
                                  { tipRadius, 0.95f * length },
                                  { tipRadius * 1.5f, 0.975f * length },
                                  { 0.001f, length } }, segments);
    return mesh;
}

} // namespace vdrum

namespace vdrum
{

Mesh transformed (const Mesh& mesh, const Mat4& transform)
{
    const auto normalMatrix = transform.normalMatrix();
    Mesh out;
    out.indices = mesh.indices;
    out.vertices.reserve (mesh.vertices.size());
    for (const auto& v : mesh.vertices)
    {
        const auto p = transform.transformPoint ({ v.px, v.py, v.pz });
        const auto n = normalise (normalMatrix.transformPoint ({ v.nx, v.ny, v.nz }));
        out.vertices.push_back ({ p.x, p.y, p.z, n.x, n.y, n.z });
    }
    return out;
}

Mesh makeCylinderBetween (Vec3 a, Vec3 b, float radius, int segments)
{
    const auto axis = b - a;
    const auto height = length (axis);
    auto mesh = makeTube (radius, radius, height, segments);
    append (mesh, makeDisc (radius, height, segments, true));
    append (mesh, makeDisc (radius, 0.0f, segments, false));
    return transformed (mesh, Mat4::translation (a) * Mat4::alignZ (axis));
}

Mesh makeTorus (float majorRadius, float minorRadius, int majorSegments, int minorSegments)
{
    // Circle profile traversed counter-clockwise in the (r, z) plane: normals point outwards.
    std::vector<std::pair<float, float>> profile;
    for (int i = 0; i <= minorSegments; ++i)
    {
        const auto angle = -0.5f * kPi + 2.0f * kPi * static_cast<float> (i) / static_cast<float> (minorSegments);
        profile.emplace_back (majorRadius + minorRadius * std::cos (angle), minorRadius * std::sin (angle));
    }
    return makeLathe (profile, majorSegments);
}

Mesh makeBox (Vec3 size)
{
    const auto h = size * 0.5f;
    Mesh mesh;
    const auto face = [&mesh] (Vec3 normal, Vec3 u, Vec3 v, Vec3 centre)
    {
        const auto base = static_cast<std::uint32_t> (mesh.vertices.size());
        mesh.addVertex (centre - u - v, normal);
        mesh.addVertex (centre + u - v, normal);
        mesh.addVertex (centre + u + v, normal);
        mesh.addVertex (centre - u + v, normal);
        mesh.addTriangle (base, base + 1, base + 2);
        mesh.addTriangle (base, base + 2, base + 3);
    };
    // u x v points along the normal on each face, so every face winds counter-clockwise.
    face ({ 1, 0, 0 }, { 0, h.y, 0 }, { 0, 0, h.z }, { h.x, 0, 0 });
    face ({ -1, 0, 0 }, { 0, 0, h.z }, { 0, h.y, 0 }, { -h.x, 0, 0 });
    face ({ 0, 1, 0 }, { 0, 0, h.z }, { h.x, 0, 0 }, { 0, h.y, 0 });
    face ({ 0, -1, 0 }, { h.x, 0, 0 }, { 0, 0, h.z }, { 0, -h.y, 0 });
    face ({ 0, 0, 1 }, { h.x, 0, 0 }, { 0, h.y, 0 }, { 0, 0, h.z });
    face ({ 0, 0, -1 }, { 0, h.y, 0 }, { h.x, 0, 0 }, { 0, 0, -h.z });
    return mesh;
}

Mesh makeSphere (float radius, int rings, int segments)
{
    std::vector<std::pair<float, float>> profile;
    for (int i = 0; i <= rings; ++i)
    {
        const auto angle = -0.5f * kPi + kPi * static_cast<float> (i) / static_cast<float> (rings);
        profile.emplace_back (std::max (radius * std::cos (angle), 1.0e-4f), radius * std::sin (angle));
    }
    return makeLathe (profile, segments);
}

} // namespace vdrum
