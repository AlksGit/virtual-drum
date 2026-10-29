#include "KitModel.h"

#include "KitLayout.h"

#include <algorithm>
#include <array>
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

constexpr int kSegments = 96;      // drums and cymbals
constexpr int kSmallSegments = 16; // tubes, rods, hardware

const Vec3 kDrumFlash { 0.10f, 0.80f, 1.00f };   // neon cyan
const Vec3 kCymbalFlash { 1.00f, 0.55f, 0.15f }; // warm amber
const Vec3 kNeonCyan { 0.15f, 1.30f, 1.80f };

// --- Materials: a dark lacquered kit, chrome hardware, bronze cymbals ---------------------

Material lacquer()      { return { srgbToLinear (0x2450a8), 0.0f, 0.40f, 1.0f, SurfacePattern::sparkle, 1.0f }; }
Material chrome()       { return { srgbToLinear (0xe8e6e1), 1.0f, 0.10f, 0.0f, SurfacePattern::none, 1.0f }; }
Material brushedSteel() { return { srgbToLinear (0xd5d8de), 1.0f, 0.18f, 0.0f, SurfacePattern::brushed, 1.0f }; }
Material rubber()       { return { srgbToLinear (0x111114), 0.0f, 0.85f, 0.0f, SurfacePattern::none, 1.0f }; }
Material felt()         { return { srgbToLinear (0x6b1d2a), 0.0f, 0.95f, 0.0f, SurfacePattern::none, 1.0f }; }
Material darkMetal()    { return { srgbToLinear (0x2a2c31), 1.0f, 0.35f, 0.0f, SurfacePattern::brushed, 1.0f }; }
Material leather()      { return { srgbToLinear (0x141417), 0.0f, 0.45f, 0.4f, SurfacePattern::none, 1.0f }; }
Material headCoated (float radius) { return { srgbToLinear (0xb8b3a8), 0.0f, 0.68f, 0.0f, SurfacePattern::drumHead, radius }; }
Material headBlack (float radius)  { return { srgbToLinear (0x16161a), 0.0f, 0.30f, 0.6f, SurfacePattern::drumHead, radius }; }
Material bronze (float radius)     { return { srgbToLinear (0xd8a253), 1.0f, 0.30f, 0.0f, SurfacePattern::cymbal, radius }; }

Material neon (Vec3 colour)
{
    Material m { srgbToLinear (0x0a0a0c), 0.0f, 0.4f, 0.0f, SurfacePattern::none, 1.0f };
    m.glow = colour;
    return m;
}

// --- Small helpers --------------------------------------------------------------------------

class Builder
{
public:
    explicit Builder (KitModel& modelIn) : model (modelIn) {}

    void add (const Mesh& mesh, const Material& material, const Mat4& transform = Mat4::identity(),
              int pad = -1, Vec3 flash = {})
    {
        if (mesh.indices.empty())
            return;
        model.parts.push_back ({ model.addMesh (mesh), material, transform, pad, flash });
    }

private:
    KitModel& model;
};

Mesh translated (const Mesh& mesh, Vec3 offset)
{
    return transformed (mesh, Mat4::translation (offset));
}

Vec3 ringPoint (float radius, float angle, float z)
{
    return { radius * std::cos (angle), radius * std::sin (angle), z };
}

// Rounded bar along Z, centred on the origin (drum lugs, clutch parts).
Mesh makeCapsule (float radius, float length)
{
    const auto h = 0.5f * length;
    return makeLathe ({ { 0.0005f, -h },
                        { radius * 0.75f, -h + 0.15f * radius },
                        { radius, -h + 0.6f * radius },
                        { radius, h - 0.6f * radius },
                        { radius * 0.75f, h - 0.15f * radius },
                        { 0.0005f, h } }, kSmallSegments);
}

// Telescopic tripod stand in world coordinates: three braced legs from a collar, a lower and
// an upper tube with a clamp at the joint, rubber feet.
void addTripod (Builder& builder, Vec3 base, float topZ, float legSpread, float collarZ, float baseAngle)
{
    Mesh metal, feet;
    const Vec3 collar { base.x, base.y, collarZ };

    for (int i = 0; i < 3; ++i)
    {
        const auto angle = baseAngle + 2.0f * kPi * static_cast<float> (i) / 3.0f;
        const Vec3 foot { base.x + legSpread * std::cos (angle), base.y + legSpread * std::sin (angle), 0.012f };
        append (metal, makeCylinderBetween (collar, foot, 0.0075f, kSmallSegments));
        const auto braceStart = collar + (foot - collar) * 0.45f;
        append (metal, makeCylinderBetween (braceStart, { base.x, base.y, collarZ - 0.12f }, 0.0045f, kSmallSegments));
        append (feet, makeCylinderBetween ({ foot.x, foot.y, 0.0f }, { foot.x, foot.y, 0.022f }, 0.014f, kSmallSegments));
    }

    const auto jointZ = collarZ + 0.6f * (topZ - collarZ);
    append (metal, makeCylinderBetween ({ base.x, base.y, collarZ - 0.14f }, { base.x, base.y, jointZ }, 0.0125f, kSmallSegments));
    append (metal, makeCylinderBetween ({ base.x, base.y, jointZ }, { base.x, base.y, topZ }, 0.0095f, kSmallSegments));
    append (metal, translated (makeTorus (0.0145f, 0.0045f, kSmallSegments, 8), { base.x, base.y, jointZ }));
    append (metal, translated (makeTorus (0.0140f, 0.0050f, kSmallSegments, 8), { base.x, base.y, collarZ }));

    builder.add (metal, brushedSteel());
    builder.add (feet, rubber());
}

// --- Drums ---------------------------------------------------------------------------------

struct DrumStyle
{
    int lugCount;
    bool doubleLugs;    // separate top and bottom lugs on deep drums
    bool snareThrowOff;
    bool lacquerHoops;  // bass drums have wooden hoops, painted like the shell
};

// Drum in its local frame: batter head at z = 0 facing +Z, shell down to z = -depth.
void addDrum (Builder& builder, const Mat4& transform, float r, float depth, const DrumStyle& style,
              int pad, const Material& batter, const Material& resonant)
{
    builder.add (translated (makeTube (r, r, depth, kSegments), { 0.0f, 0.0f, -depth }), lacquer(), transform, pad, kDrumFlash * 0.12f);

    builder.add (makeDisc (r * 0.995f, 0.001f, kSegments, true), batter, transform, pad, kDrumFlash);
    builder.add (makeDisc (r * 0.995f, -depth - 0.001f, kSegments, false), resonant, transform);

    // Hoops: rounded rim plus a flange at each end.
    Mesh hoops;
    const auto hoopRadius = r * 1.012f + 0.004f;
    for (const auto z : { 0.004f, -depth - 0.004f })
    {
        append (hoops, translated (makeTorus (hoopRadius, 0.0065f, kSegments, 10), { 0.0f, 0.0f, z }));
        const auto flangeBottom = z > 0.0f ? z - 0.022f : z;
        append (hoops, translated (makeTube (hoopRadius, hoopRadius, 0.022f, kSegments), { 0.0f, 0.0f, flangeBottom }));
    }
    builder.add (hoops, style.lacquerHoops ? lacquer() : chrome(), transform);

    // Lugs, tension rods and rod heads.
    Mesh hardware;
    const auto lugRadius = r + 0.010f;
    const auto rodRadius = hoopRadius + 0.006f;
    const auto lugLength = std::min (0.055f, 0.45f * depth);
    const std::array<float, 2> lugCentres = style.doubleLugs ? std::array<float, 2> { -0.22f * depth, -0.78f * depth }
                                                             : std::array<float, 2> { -0.5f * depth, -0.5f * depth };

    for (int i = 0; i < style.lugCount; ++i)
    {
        const auto angle = 2.0f * kPi * (static_cast<float> (i) + 0.5f) / static_cast<float> (style.lugCount);

        append (hardware, translated (makeCapsule (0.0075f, lugLength), ringPoint (lugRadius, angle, lugCentres[0])));
        if (style.doubleLugs)
            append (hardware, translated (makeCapsule (0.0075f, lugLength), ringPoint (lugRadius, angle, lugCentres[1])));

        const auto topLug = lugCentres[0] + 0.5f * lugLength;
        const auto bottomLug = lugCentres[1] - 0.5f * lugLength;
        append (hardware, makeCylinderBetween (ringPoint (rodRadius, angle, 0.012f), ringPoint (lugRadius, angle, topLug), 0.0022f, 8));
        append (hardware, makeCylinderBetween (ringPoint (rodRadius, angle, -depth - 0.012f), ringPoint (lugRadius, angle, bottomLug), 0.0022f, 8));

        append (hardware, translated (makeCylinderBetween ({ 0, 0, 0 }, { 0, 0, 0.009f }, 0.0045f, 8), ringPoint (rodRadius, angle, 0.010f)));
        append (hardware, translated (makeCylinderBetween ({ 0, 0, 0 }, { 0, 0, -0.009f }, 0.0045f, 8), ringPoint (rodRadius, angle, -depth - 0.010f)));
    }

    if (style.snareThrowOff)
    {
        // Throw-off lever and butt plate on opposite sides of the shell.
        append (hardware, transformed (makeBox ({ 0.018f, 0.028f, 0.05f }), Mat4::translation ({ r + 0.012f, 0.0f, -0.5f * depth })));
        append (hardware, transformed (makeBox ({ 0.014f, 0.022f, 0.03f }), Mat4::translation ({ -r - 0.010f, 0.0f, -0.6f * depth })));
        append (hardware, makeCylinderBetween ({ r + 0.022f, 0.0f, -0.3f * depth }, { r + 0.022f, 0.0f, -0.62f * depth }, 0.0035f, 8));
    }

    builder.add (hardware, chrome(), transform);
}

// --- Cymbals -------------------------------------------------------------------------------

// Cymbal in its local frame (edge at z = 0, bell up), with felts and wing nut on the post.
void addCymbal (Builder& builder, const Mat4& transform, float r, int pad, bool withTopHardware)
{
    builder.add (makeCymbal (r, kSegments), bronze (r), transform, pad, kCymbalFlash);

    const auto bellTop = 0.24f * r;
    Mesh felts, metal;
    append (felts, translated (makeCylinderBetween ({ 0, 0, 0 }, { 0, 0, 0.012f }, 0.022f, kSmallSegments), { 0.0f, 0.0f, 0.15f * r - 0.014f }));
    if (withTopHardware)
    {
        append (felts, translated (makeCylinderBetween ({ 0, 0, 0 }, { 0, 0, 0.012f }, 0.022f, kSmallSegments), { 0.0f, 0.0f, bellTop + 0.002f }));
        append (metal, makeCylinderBetween ({ 0.0f, 0.0f, -0.03f }, { 0.0f, 0.0f, bellTop + 0.05f }, 0.004f, kSmallSegments));
        append (metal, translated (makeTorus (0.008f, 0.004f, kSmallSegments, 8), { 0.0f, 0.0f, bellTop + 0.024f }));
        append (metal, transformed (makeBox ({ 0.04f, 0.006f, 0.014f }), Mat4::translation ({ 0.0f, 0.0f, bellTop + 0.034f })));
    }
    builder.add (felts, felt(), transform);
    builder.add (metal, chrome(), transform);
}

// Tilter at the top of a cymbal stand, joining the tube to the cymbal post.
void addTilter (Builder& builder, Vec3 tubeTop, Vec3 cymbalBase)
{
    auto metal = translated (makeSphere (0.016f, 8, kSmallSegments), tubeTop);
    append (metal, makeCylinderBetween (tubeTop, cymbalBase, 0.006f, kSmallSegments));
    append (metal, transformed (makeBox ({ 0.006f, 0.05f, 0.012f }), Mat4::translation (tubeTop + Vec3 { 0.0f, 0.03f, 0.0f })));
    builder.add (metal, chrome());
}

// Footboard of a pedal, from heel to toe.
void addFootboard (Builder& builder, Vec3 heel, Vec3 toe)
{
    builder.add (transformed (makeBox ({ 0.085f, 0.012f, length (toe - heel) }),
                              Mat4::translation ((heel + toe) * 0.5f) * Mat4::alignZ (toe - heel)), darkMetal());
}

void addBassDrum (Builder& builder)
{
    const auto batter = KitLayout::kKickBatterCentre;
    const auto r = KitLayout::kKickRadius;
    const auto depth = KitLayout::kKickDepth;
    const auto transform = Mat4::translation (batter) * Mat4::alignZ ({ -1.0f, 0.0f, 0.0f });

    addDrum (builder, transform, r, depth, { 10, false, false, true }, -1, headCoated (r), headBlack (r));

    // Front head logo: a neon ring and a port hole.
    builder.add (translated (makeTorus (0.62f * r, 0.0045f, kSegments, 8), { 0.0f, 0.0f, -depth - 0.004f }), neon (kNeonCyan), transform);
    builder.add (makeDisc (0.13f * r, -depth - 0.003f, 48, false), { srgbToLinear (0x050506), 0.0f, 0.9f, 0.0f, SurfacePattern::none, 1.0f }, transform);

    // Spurs at the front, angled out to the floor.
    const auto front = batter + Vec3 { depth, 0.0f, 0.0f };
    Mesh spurs, spurFeet;
    for (const auto side : { 1.0f, -1.0f })
    {
        const Vec3 mount { front.x - 0.12f, front.y + side * (r + 0.012f), batter.z - 0.02f };
        const Vec3 foot { front.x - 0.05f, front.y + side * (r + 0.12f), 0.01f };
        append (spurs, makeCylinderBetween (mount, foot, 0.006f, kSmallSegments));
        append (spurFeet, makeCylinderBetween ({ foot.x, foot.y, 0.0f }, { foot.x, foot.y, 0.02f }, 0.012f, kSmallSegments));
    }
    builder.add (spurs, chrome());
    builder.add (spurFeet, rubber());

    // Pedal: footboard, frame, beater shaft and a felt beater against the batter head.
    addFootboard (builder, { batter.x - 0.32f, batter.y, 0.025f }, { batter.x - 0.06f, batter.y, 0.075f });

    Mesh frame;
    append (frame, transformed (makeBox ({ 0.14f, 0.11f, 0.008f }), Mat4::translation ({ batter.x - 0.04f, batter.y, 0.004f })));
    for (const auto side : { 0.045f, -0.045f })
        append (frame, makeCylinderBetween ({ batter.x - 0.04f, batter.y + side, 0.008f }, { batter.x - 0.04f, batter.y + side, 0.16f }, 0.006f, kSmallSegments));
    append (frame, makeCylinderBetween ({ batter.x - 0.04f, batter.y - 0.05f, 0.15f }, { batter.x - 0.04f, batter.y + 0.05f, 0.15f }, 0.005f, kSmallSegments));
    const Vec3 beater { batter.x - 0.028f, batter.y, batter.z + 0.05f };
    append (frame, makeCylinderBetween ({ batter.x - 0.04f, batter.y, 0.15f }, beater, 0.0035f, kSmallSegments));
    builder.add (frame, chrome());
    builder.add (makeCylinderBetween (beater + Vec3 { 0.0f, -0.026f, 0.0f }, beater + Vec3 { 0.0f, 0.026f, 0.0f }, 0.028f, kSmallSegments), felt());
}

void addDrums (Builder& builder)
{
    // Tom holder: a post on top of the bass drum, one arm per mounted tom.
    const auto kickTop = KitLayout::kKickBatterCentre.z + KitLayout::kKickRadius;
    const Vec3 postBase { KitLayout::kKickBatterCentre.x + 0.22f, KitLayout::kKickBatterCentre.y, kickTop + 0.012f };
    const Vec3 postTop { postBase.x, postBase.y, 0.72f };
    Mesh tomMount;
    append (tomMount, makeCylinderBetween (postBase - Vec3 { 0.0f, 0.0f, 0.02f }, postTop, 0.011f, kSmallSegments));
    append (tomMount, transformed (makeBox ({ 0.07f, 0.04f, 0.025f }), Mat4::translation (postBase)));
    append (tomMount, translated (makeSphere (0.02f, 8, kSmallSegments), postTop));

    for (const auto& info : kDrumKit)
    {
        const auto placement = KitLayout::placement (info.pad);
        if (placement.shape != PadShape::drum)
            continue;

        const auto pad = static_cast<int> (info.pad);
        const auto transform = Mat4::translation (placement.strikeCentre) * Mat4::alignZ (placement.surfaceNormal);
        const auto r = placement.radius;
        const auto depth = placement.depth;

        if (info.pad == Pad::snare)
        {
            addDrum (builder, transform, r, depth, { 10, false, true, false }, pad, headCoated (r), headCoated (r));

            // Stand with a three-arm basket under the drum.
            const auto bottom = transform.transformPoint ({ 0.0f, 0.0f, -depth - 0.05f });
            addTripod (builder, { bottom.x, bottom.y, 0.0f }, bottom.z, 0.26f, 0.22f, 0.4f);
            Mesh basket;
            for (int i = 0; i < 3; ++i)
            {
                const auto angle = 0.4f + 2.0f * kPi * static_cast<float> (i) / 3.0f;
                const auto grip = transform.transformPoint (ringPoint (r * 1.02f, angle, -depth - 0.006f));
                append (basket, makeCylinderBetween (bottom, grip, 0.005f, kSmallSegments));
                append (basket, translated (makeSphere (0.009f, 6, 10), grip));
            }
            builder.add (basket, chrome());
        }
        else if (info.pad == Pad::floorTom)
        {
            addDrum (builder, transform, r, depth, { 8, true, false, false }, pad, headCoated (r), headBlack (r));

            // Three legs from brackets on the shell, angled out to the floor.
            Mesh legs, feet;
            for (int i = 0; i < 3; ++i)
            {
                const auto angle = 0.5f + 2.0f * kPi * static_cast<float> (i) / 3.0f;
                const auto top = transform.transformPoint (ringPoint (r + 0.022f, angle, -0.25f * depth));
                const auto outward = normalise (Vec3 { top.x - placement.strikeCentre.x, top.y - placement.strikeCentre.y, 0.0f });
                const Vec3 foot { top.x + 0.06f * outward.x, top.y + 0.06f * outward.y, 0.012f };
                append (legs, makeCylinderBetween (top + Vec3 { 0.0f, 0.0f, 0.03f }, foot, 0.0075f, kSmallSegments));
                append (legs, translated (makeBox ({ 0.03f, 0.03f, 0.05f }), top));
                append (feet, makeCylinderBetween ({ foot.x, foot.y, 0.0f }, { foot.x, foot.y, 0.024f }, 0.013f, kSmallSegments));
            }
            builder.add (legs, chrome());
            builder.add (feet, rubber());
        }
        else
        {
            addDrum (builder, transform, r, depth, { 6, false, false, false }, pad, headCoated (r), headBlack (r));

            // Arm from the post to a bracket on the side of the shell facing it.
            const auto towardsPost = normalise (Vec3 { postTop.x - placement.strikeCentre.x, postTop.y - placement.strikeCentre.y, 0.0f });
            const auto bracket = transform.transformPoint (towardsPost * (r + 0.012f) + Vec3 { 0.0f, 0.0f, -0.55f * depth });
            append (tomMount, makeCylinderBetween (postTop, bracket, 0.008f, kSmallSegments));
            append (tomMount, translated (makeSphere (0.016f, 8, kSmallSegments), bracket));
        }
    }
    builder.add (tomMount, chrome());
}

// Base of a cymbal stand: starting under the cymbal, move away from the player until the
// tube is clear of every other pad (and of the bass drum).
Vec3 clearStandBase (Vec3 underCymbal, Pad self)
{
    const auto outward = normalise (Vec3 { underCymbal.x - KitLayout::kPivot.x, underCymbal.y - KitLayout::kPivot.y, 0.0f });
    for (float d = 0.0f; d < 0.8f; d += 0.01f)
    {
        const auto candidate = underCymbal + outward * d;
        bool clear = true;
        for (const auto& other : kDrumKit)
        {
            if (other.pad == self)
                continue;
            const auto p = KitLayout::placement (other.pad);
            clear = clear && std::hypot (candidate.x - p.strikeCentre.x, candidate.y - p.strikeCentre.y) > p.radius + 0.06f;
        }
        if (clear)
            return candidate;
    }
    return underCymbal + outward * 0.8f;
}

void addCymbals (Builder& builder)
{
    for (const auto& info : kDrumKit)
    {
        const auto placement = KitLayout::placement (info.pad);
        if (placement.shape != PadShape::cymbal)
            continue;

        const auto pad = static_cast<int> (info.pad);
        const auto transform = Mat4::translation (placement.strikeCentre) * Mat4::alignZ (placement.surfaceNormal);
        const auto r = placement.radius;
        const auto centre = placement.strikeCentre;

        if (info.pad == Pad::hiHat)
        {
            // Top cymbal, bottom cymbal upside down under it, rod, clutch, pedal.
            addCymbal (builder, transform, r, pad, false);
            builder.add (makeCymbal (r, kSegments), bronze (r),
                         transform * Mat4::translation ({ 0.0f, 0.0f, -0.022f }) * Mat4::rotation (Quat::fromAxisAngle ({ 1, 0, 0 }, kPi)),
                         pad, kCymbalFlash * 0.6f);

            const Vec3 base { centre.x, centre.y, 0.0f };
            addTripod (builder, base, centre.z - 0.05f, 0.27f, 0.20f, 0.3f);

            Mesh metal;
            append (metal, makeCylinderBetween ({ base.x, base.y, centre.z - 0.06f }, { base.x, base.y, centre.z + 0.10f }, 0.0035f, kSmallSegments));
            append (metal, translated (makeCapsule (0.010f, 0.03f), { base.x, base.y, centre.z + 0.07f }));
            append (metal, translated (makeTorus (0.016f, 0.005f, kSmallSegments, 8), { base.x, base.y, centre.z - 0.04f }));
            builder.add (metal, chrome());

            const auto towardsPlayer = normalise (Vec3 { -centre.x, -centre.y, 0.0f });
            addFootboard (builder, base + towardsPlayer * 0.30f + Vec3 { 0.0f, 0.0f, 0.025f },
                          base + towardsPlayer * 0.04f + Vec3 { 0.0f, 0.0f, 0.07f });
            continue;
        }

        addCymbal (builder, transform, r, pad, true);

        // Boom stand: the tube stands where it clears every other pad, a boom arm reaches the
        // cymbal (a straight stand would go through the hi-hat or the floor tom).
        const auto cymbalBase = transform.transformPoint ({ 0.0f, 0.0f, -0.03f });
        const auto standBase = clearStandBase ({ cymbalBase.x, cymbalBase.y, 0.0f }, info.pad);
        const Vec3 boomStart { standBase.x, standBase.y, cymbalBase.z - 0.30f };
        const auto tilterPoint = cymbalBase - Vec3 { 0.0f, 0.0f, 0.04f };
        addTripod (builder, standBase, boomStart.z, 0.32f, 0.22f, info.pad == Pad::crash ? 0.2f : 0.9f);

        Mesh boom;
        append (boom, translated (makeTorus (0.0135f, 0.0055f, kSmallSegments, 8), boomStart));
        append (boom, makeCylinderBetween (boomStart, tilterPoint, 0.0085f, kSmallSegments));
        // Counterweight at the other end of the boom.
        const auto counterweight = boomStart - normalise (tilterPoint - boomStart) * 0.12f;
        append (boom, makeCylinderBetween (boomStart, counterweight, 0.0085f, kSmallSegments));
        append (boom, translated (makeSphere (0.022f, 8, kSmallSegments), counterweight));
        builder.add (boom, brushedSteel());
        addTilter (builder, tilterPoint, cymbalBase);
    }
}

void addThrone (Builder& builder)
{
    const auto seat = KitLayout::kThroneSeatCentre;
    const auto r = KitLayout::kThroneRadius;
    builder.add (makeLathe ({ { 0.001f, -0.045f }, { r * 0.92f, -0.045f }, { r, -0.025f }, { r * 1.01f, 0.0f },
                              { r * 0.97f, 0.03f }, { r * 0.8f, 0.045f }, { 0.001f, 0.05f } }, 48),
                 leather(), Mat4::translation (seat));
    addTripod (builder, { seat.x, seat.y, 0.0f }, seat.z - 0.045f, 0.30f, 0.18f, 0.0f);
}

} // namespace

KitModel KitModel::build()
{
    KitModel model;
    Builder builder (model);

    // Stage floor and a round rug with a neon ring.
    builder.add (makeDisc (6.0f, 0.0f, 128, true), { srgbToLinear (0x0c0e13), 0.0f, 0.80f, 0.0f, SurfacePattern::none, 1.0f });
    const auto rugTransform = Mat4::translation ({ 0.35f, 0.0f, 0.0f });
    constexpr float kRugRadius = 1.45f;
    builder.add (makeDisc (kRugRadius, 0.004f, 128, true), { srgbToLinear (0x231c2a), 0.0f, 0.95f, 0.0f, SurfacePattern::rug, kRugRadius }, rugTransform);
    builder.add (translated (makeTorus (0.93f * kRugRadius, 0.006f, 160, 8), { 0.0f, 0.0f, 0.006f }), neon (kNeonCyan), rugTransform);

    addBassDrum (builder);
    addDrums (builder);
    addCymbals (builder);
    addThrone (builder);

    model.stickMesh = model.addMesh (makeStick (KitLayout::kStickLength, 0.0075f, 0.004f, 24));
    model.stickMaterial = { srgbToLinear (0xd9b47c), 0.0f, 0.50f, 0.3f, SurfacePattern::wood, 1.0f };
    return model;
}

} // namespace vdrum
