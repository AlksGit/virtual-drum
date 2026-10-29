#pragma once

// What the 3D view draws, independent of OpenGL: meshes, physically based materials and
// the parts of the kit that use them. Built once; only the sticks move.

#include "DrumKit.h"
#include "Mat4.h"
#include "Mesh.h"

#include <vector>

namespace vdrum
{

// Procedural surface detail computed in the fragment shader from the local position.
enum class SurfacePattern : int
{
    none = 0,
    cymbal = 1,   // lathe grooves and hammer marks; patternScale = radius
    sparkle = 2,  // metal flakes under a clear coat (lacquered shells)
    drumHead = 3, // coated head with a darker printed rim; patternScale = radius
    wood = 4,     // grain along the stick
    brushed = 5,  // brushed metal along the local Z axis
    rug = 6,      // woven fibres; patternScale = radius
};

struct Material
{
    Vec3 baseColour;            // linear RGB
    float metallic = 0.0f;
    float roughness = 0.5f;
    float clearcoat = 0.0f;     // extra glossy dielectric layer (lacquer)
    SurfacePattern pattern = SurfacePattern::none;
    float patternScale = 1.0f;
    Vec3 glow {};               // constant emission (neon accents), linear RGB
};

// sRGB colour (0xRRGGBB) to linear RGB.
Vec3 srgbToLinear (unsigned rgb);

struct KitPart
{
    int mesh = -1;      // index into KitModel::meshes
    Material material;
    Mat4 model;
    int pad = -1;       // pad whose hits light this part, -1 for none
    Vec3 flashColour;   // emission at full velocity (linear RGB)
};

struct KitModel
{
    std::vector<Mesh> meshes;
    std::vector<KitPart> parts;
    int stickMesh = -1;
    Material stickMaterial;

    static KitModel build();

    int addMesh (Mesh mesh);
};

} // namespace vdrum
