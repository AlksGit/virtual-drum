#include "TestFramework.h"

#include "KitModel.h"

#include <cmath>
#include <set>

using namespace vdrum;

VDRUM_TEST (srgbConversionMatchesTheStandard)
{
    CHECK (srgbToLinear (0x000000).x == 0.0f);
    CHECK (std::abs (srgbToLinear (0xffffff).y - 1.0f) < 1.0e-6f);
    CHECK (std::abs (srgbToLinear (0x808080).z - 0.2158605f) < 1.0e-4f);
}

VDRUM_TEST (kitPartsReferenceValidMeshes)
{
    const auto kit = KitModel::build();
    REQUIRE (! kit.parts.empty());
    for (const auto& part : kit.parts)
    {
        REQUIRE (part.mesh >= 0 && part.mesh < static_cast<int> (kit.meshes.size()));
        CHECK (! kit.meshes[static_cast<std::size_t> (part.mesh)].indices.empty());
    }
    REQUIRE (kit.stickMesh >= 0 && kit.stickMesh < static_cast<int> (kit.meshes.size()));
}

VDRUM_TEST (everyPadHasAPartThatLightsUpOnHits)
{
    const auto kit = KitModel::build();
    std::set<int> flashingPads;
    for (const auto& part : kit.parts)
        if (part.pad >= 0 && length (part.flashColour) > 0.5f)
            flashingPads.insert (part.pad);
    CHECK (flashingPads.size() == kDrumKit.size());
}

VDRUM_TEST (materialsArePhysicallyPlausible)
{
    const auto kit = KitModel::build();
    auto check = [] (const Material& m)
    {
        CHECK (m.metallic >= 0.0f && m.metallic <= 1.0f);
        CHECK (m.roughness > 0.0f && m.roughness <= 1.0f);
        CHECK (m.clearcoat >= 0.0f && m.clearcoat <= 1.0f);
        // Albedo of real materials stays below ~0.95 (pure white does not exist).
        CHECK (m.baseColour.x < 0.95f && m.baseColour.y < 0.95f && m.baseColour.z < 0.95f);
    };
    for (const auto& part : kit.parts)
        check (part.material);
    check (kit.stickMaterial);
}
