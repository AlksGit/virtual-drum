#pragma once

// Procedural meshes for the 3D kit. Shapes are built around the +Z axis, then placed with a
// model matrix or merged with transformed().

#include "Mat4.h"

#include <cstdint>
#include <vector>

namespace vdrum
{

struct Vertex
{
    float px, py, pz;
    float nx, ny, nz;
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices; // triangles

    void addVertex (Vec3 position, Vec3 normal);
    void addTriangle (std::uint32_t a, std::uint32_t b, std::uint32_t c);
};

// Open tube from z = 0 to z = height, radius changing linearly from bottom to top.
Mesh makeTube (float bottomRadius, float topRadius, float height, int segments);

// Flat disc at z = height, facing +Z (or -Z when facingUp is false).
Mesh makeDisc (float radius, float height, int segments, bool facingUp);

// Cymbal: shallow cone, edge at z = 0, centre raised, with a small bell.
Mesh makeCymbal (float radius, int segments);

// Stick along +Z from z = 0 (grip) to z = length (tip), tapering towards the tip.
Mesh makeStick (float length, float gripRadius, float tipRadius, int segments);

// Surface of revolution around Z through a (radius, z) profile. Normals point to the left of
// the profile direction in the (r, z) plane: outwards for a profile going up the outside.
Mesh makeLathe (const std::vector<std::pair<float, float>>& profile, int segments);

// Closed cylinder from point a to point b.
Mesh makeCylinderBetween (Vec3 a, Vec3 b, float radius, int segments);

// Ring around Z at height 0: circle of radius minorRadius swept at majorRadius.
Mesh makeTorus (float majorRadius, float minorRadius, int majorSegments, int minorSegments);

// Axis-aligned box centred on the origin.
Mesh makeBox (Vec3 size);

Mesh makeSphere (float radius, int rings, int segments);

// Copy of `mesh` with positions and normals transformed by `transform`.
Mesh transformed (const Mesh& mesh, const Mat4& transform);

// Appends `source` to `target`.
void append (Mesh& target, const Mesh& source);

} // namespace vdrum
