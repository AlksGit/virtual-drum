#pragma once

// Procedural meshes for the 3D kit (placeholders until real glTF models, see TASK-019).
// All shapes are built around the +Z axis and placed with a model matrix.

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

// Appends `source` to `target`.
void append (Mesh& target, const Mesh& source);

} // namespace vdrum
