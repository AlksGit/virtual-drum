#include "KitModel.h"

#include "KitLayout.h"

#include <cmath>

namespace vdrum
{

Vec3 srgbToLinear (unsigned rgb)
{
    const auto channel = [] (unsigned value)
    {
        const auto c = static_cast<float> (value) / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f);
    };
    return { channel ((rgb >> 16) & 0xffu), channel ((rgb >> 8) & 0xffu), channel (rgb & 0xffu) };
}

int KitModel::addMesh (Mesh mesh)
{
    meshes.push_back (std::move (mesh));
    return static_cast<int> (meshes.size()) - 1;
}

namespace
{

constexpr int kSegments = 96;

Mesh translated (Mesh mesh, Vec3 offset)
{
    for (auto& v : mesh.vertices)
    {
        v.px += offset.x;
        v.py += offset.y;
        v.pz += offset.z;
    }
    return mesh;
}

// Materials of the kit: a dark lacquered kit with chrome hardware and bronze cymbals.
const Material& lacquerMaterial()
{
    static const Material m { srgbToLinear (0x2450a8), 0.0f, 0.40f, 1.0f, SurfacePattern::sparkle, 1.0f };
    return m;
}

const Material& chromeMaterial()
{
    static const Material m { srgbToLinear (0xe8e6e1), 1.0f, 0.10f, 0.0f, SurfacePattern::none, 1.0f };
    return m;
}

const Material& brushedSteelMaterial()
{
    static const Material m { srgbToLinear (0xd5d8de), 1.0f, 0.18f, 0.0f, SurfacePattern::brushed, 1.0f };
    return m;
}

Material headMaterial (float radius)
{
    return { srgbToLinear (0xb8b3a8), 0.0f, 0.68f, 0.0f, SurfacePattern::drumHead, radius };
}

Material cymbalMaterial (float radius)
{
    return { srgbToLinear (0xd8a253), 1.0f, 0.30f, 0.0f, SurfacePattern::cymbal, radius };
}

const Vec3 kDrumFlash { 0.10f, 0.80f, 1.00f };   // neon cyan
const Vec3 kCymbalFlash { 1.00f, 0.55f, 0.15f }; // warm amber

} // namespace

KitModel KitModel::build()
{
    KitModel model;

    // Stage floor and a round rug under the kit.
    {
        const auto floor = model.addMesh (makeDisc (6.0f, 0.0f, 128, true));
        model.parts.push_back ({ floor, { srgbToLinear (0x0c0e13), 0.0f, 0.80f, 0.0f, SurfacePattern::none, 1.0f }, Mat4::identity(), -1, {} });

        const auto rug = model.addMesh (makeDisc (1.45f, 0.004f, 128, true));
        model.parts.push_back ({ rug, { srgbToLinear (0x231c2a), 0.0f, 0.95f, 0.0f, SurfacePattern::rug, 1.45f },
                                 Mat4::translation ({ 0.35f, 0.0f, 0.0f }), -1, {} });
    }

    for (const auto& info : kDrumKit)
    {
        const auto placement = KitLayout::placement (info.pad);
        const auto padIndex = static_cast<int> (info.pad);
        const auto transform = Mat4::translation (placement.strikeCentre) * Mat4::alignZ (placement.surfaceNormal);
        const auto r = placement.radius;
        float standTop = 0.0f;

        if (placement.shape == PadShape::drum)
        {
            const auto depth = placement.depth;

            auto shell = translated (makeTube (r, r, depth, kSegments), { 0.0f, 0.0f, -depth });
            append (shell, makeDisc (r * 0.98f, -depth, kSegments, false));
            model.parts.push_back ({ model.addMesh (std::move (shell)), lacquerMaterial(), transform, padIndex, kDrumFlash * 0.12f });

            model.parts.push_back ({ model.addMesh (makeDisc (r, 0.0f, kSegments, true)), headMaterial (r), transform, padIndex, kDrumFlash });

            // Chrome hoops at both ends.
            auto hoops = translated (makeTube (r * 1.025f, r * 1.025f, 0.016f, kSegments), { 0.0f, 0.0f, -0.010f });
            append (hoops, translated (makeTube (r * 1.025f, r * 1.025f, 0.016f, kSegments), { 0.0f, 0.0f, -depth - 0.006f }));
            model.parts.push_back ({ model.addMesh (std::move (hoops)), chromeMaterial(), transform, -1, {} });

            standTop = placement.strikeCentre.z - depth;
        }
        else
        {
            auto cymbal = makeCymbal (r, kSegments);
            if (info.pad == Pad::hiHat)
                append (cymbal, translated (makeCymbal (r, kSegments), { 0.0f, 0.0f, -0.025f }));
            model.parts.push_back ({ model.addMesh (std::move (cymbal)), cymbalMaterial (r), transform, padIndex, kCymbalFlash });

            standTop = placement.strikeCentre.z - 0.03f;
        }

        const Vec3 top { placement.strikeCentre.x, placement.strikeCentre.y, standTop };
        auto stand = translated (makeTube (0.013f, 0.010f, top.z, 24), { top.x, top.y, 0.0f });
        model.parts.push_back ({ model.addMesh (std::move (stand)), brushedSteelMaterial(), Mat4::identity(), -1, {} });
    }

    model.stickMesh = model.addMesh (makeStick (KitLayout::kStickLength, 0.0075f, 0.004f, 24));
    model.stickMaterial = { srgbToLinear (0xd9b47c), 0.0f, 0.50f, 0.3f, SurfacePattern::wood, 1.0f };
    return model;
}

} // namespace vdrum
