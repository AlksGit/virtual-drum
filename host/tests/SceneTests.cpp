#include "TestFramework.h"

#include "KitLayout.h"
#include "Mat4.h"
#include "Mesh.h"
#include "SceneState.h"

#include <cmath>

using namespace vdrum;

namespace
{

bool approx (float a, float b, float tolerance = 1.0e-4f)
{
    return std::abs (a - b) <= tolerance;
}

bool approx (Vec3 a, Vec3 b, float tolerance = 1.0e-4f)
{
    return approx (a.x, b.x, tolerance) && approx (a.y, b.y, tolerance) && approx (a.z, b.z, tolerance);
}

void checkMeshIsValid (const Mesh& mesh)
{
    CHECK (! mesh.vertices.empty());
    CHECK (mesh.indices.size() % 3 == 0);
    for (const auto index : mesh.indices)
        CHECK (index < mesh.vertices.size());
    for (const auto& v : mesh.vertices)
        CHECK (approx (length ({ v.nx, v.ny, v.nz }), 1.0f, 1.0e-3f));

    // Counter-clockwise winding seen from the side the normals point to (needed for culling).
    int wrongWinding = 0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const auto faceNormal = cross (Vec3 { b.px - a.px, b.py - a.py, b.pz - a.pz }, Vec3 { c.px - a.px, c.py - a.py, c.pz - a.pz });
        if (length (faceNormal) < 1.0e-9f)
            continue; // degenerate triangle at a pole
        const Vec3 vertexNormal { a.nx + b.nx + c.nx, a.ny + b.ny + c.ny, a.nz + b.nz + c.nz };
        if (dot (faceNormal, vertexNormal) <= 0.0f)
            ++wrongWinding;
    }
    CHECK (wrongWinding == 0);
}

} // namespace

VDRUM_TEST (rotationMatrixMatchesQuaternionRotation)
{
    const auto q = Quat::fromAzimuthElevation (degToRad (30.0f), degToRad (-20.0f), degToRad (10.0f));
    const auto m = Mat4::rotation (q);
    for (const auto v : { Vec3 { 1, 0, 0 }, Vec3 { 0, 1, 0 }, Vec3 { 0.3f, -0.5f, 0.8f } })
        CHECK (approx (m.transformPoint (v), q.rotate (v)));
}

VDRUM_TEST (azimuthAndElevationRoundTrip)
{
    const auto q = Quat::fromAzimuthElevation (degToRad (-44.0f), degToRad (-20.0f), degToRad (25.0f));
    CHECK (approx (radToDeg (q.azimuthRad()), -44.0f, 1.0e-3f));
    CHECK (approx (radToDeg (q.elevationRad()), -20.0f, 1.0e-3f));
    CHECK (approx (q.tipDirection(), directionFromAngles (degToRad (-44.0f), degToRad (-20.0f))));
}

VDRUM_TEST (alignZMapsZOntoTheDirection)
{
    for (const auto direction : { Vec3 { 0, 0, 1 }, Vec3 { 0, 0, -1 }, Vec3 { 1, 2, 0.5f }, Vec3 { -0.2f, 0.1f, -1 } })
    {
        const auto m = Mat4::alignZ (direction);
        CHECK (approx (m.transformPoint ({ 0, 0, 1 }), normalise (direction)));
    }
}

VDRUM_TEST (viewAndProjectionPutTheTargetAtTheCentre)
{
    const Vec3 eye { -1.5f, 0.2f, 1.6f };
    const Vec3 target { 0.55f, 0.0f, 0.72f };
    const auto view = Mat4::lookAt (eye, target, { 0, 0, 1 });
    const auto projection = Mat4::perspective (degToRad (45.0f), 16.0f / 9.0f, 0.05f, 20.0f);

    const auto inView = view.transformPoint (target);
    CHECK (approx (inView.x, 0.0f) && approx (inView.y, 0.0f));
    CHECK (approx (inView.z, -length (target - eye), 1.0e-3f)); // camera looks down -Z

    const auto ndc = (projection * view).transformPoint (target);
    CHECK (approx (ndc.x, 0.0f) && approx (ndc.y, 0.0f));
    CHECK (ndc.z > -1.0f && ndc.z < 1.0f);
}

VDRUM_TEST (normalMatrixKeepsNormalsPerpendicularUnderScale)
{
    const auto model = Mat4::rotation (Quat::fromAxisAngle ({ 0, 0, 1 }, 0.7f)) * Mat4::scale ({ 2.0f, 1.0f, 0.5f });
    const Vec3 tangent { 1, 1, 0 };
    const Vec3 normal { 1, -1, 0 };
    const auto transformedTangent = model.transformPoint (tangent);
    const auto transformedNormal = model.normalMatrix().transformPoint (normal);
    CHECK (approx (dot (transformedTangent, transformedNormal), 0.0f, 1.0e-4f));
}

VDRUM_TEST (eachPadIsRecognisedFromItsOwnZone)
{
    for (const auto& info : kDrumKit)
    {
        const auto q = Quat::fromAzimuthElevation (degToRad (info.azimuthDeg + 3.0f), degToRad (info.elevationDeg - 3.0f));
        CHECK (nearestPad (q) == info.pad);
    }
}

VDRUM_TEST (padsDoNotIntersect)
{
    // When two footprints overlap seen from above, one pad must be fully above the other.
    for (std::size_t i = 0; i < kDrumKit.size(); ++i)
        for (std::size_t j = i + 1; j < kDrumKit.size(); ++j)
        {
            const auto a = KitLayout::placement (kDrumKit[i].pad);
            const auto b = KitLayout::placement (kDrumKit[j].pad);
            const auto horizontal = std::hypot (a.strikeCentre.x - b.strikeCentre.x, a.strikeCentre.y - b.strikeCentre.y);
            if (horizontal >= a.radius + b.radius)
                continue;

            const auto aBottom = a.strikeCentre.z - a.depth;
            const auto bBottom = b.strikeCentre.z - b.depth;
            const auto gap = std::max (bBottom - a.strikeCentre.z, aBottom - b.strikeCentre.z);
            CHECK (gap > 0.02f);
        }
}

VDRUM_TEST (padsAreAtRealisticHeights)
{
    CHECK (approx (KitLayout::placement (Pad::snare).strikeCentre.z, 0.63f, 0.05f));
    for (const auto& info : kDrumKit)
    {
        const auto z = KitLayout::placement (info.pad).strikeCentre.z;
        CHECK (z > 0.45f && z < 1.2f);
    }
}

VDRUM_TEST (stickTipLandsOnTheAimedPad)
{
    for (const auto& info : kDrumKit)
    {
        const auto q = Quat::fromAzimuthElevation (degToRad (info.azimuthDeg), degToRad (info.elevationDeg));
        Vec3 tip, grip;
        KitLayout::stickEnds (1, q, tip, grip);
        const auto placement = KitLayout::placement (info.pad);
        CHECK (length (tip - placement.strikeCentre) < 0.5f * KitLayout::kTipSpacing + 1.0e-3f);
        CHECK (approx (length (tip - grip), KitLayout::kStickLength, 1.0e-3f));
    }
}

VDRUM_TEST (proceduralMeshesAreValid)
{
    checkMeshIsValid (makeTube (0.2f, 0.2f, 0.3f, 32));
    checkMeshIsValid (makeDisc (0.2f, 0.0f, 32, true));
    checkMeshIsValid (makeDisc (0.2f, -0.1f, 32, false));
    checkMeshIsValid (makeCymbal (0.23f, 48));
    checkMeshIsValid (makeStick (0.42f, 0.0075f, 0.004f, 12));
}

VDRUM_TEST (sceneStateRecordsHitsOnTheAimedPad)
{
    SceneState state;

    protocol::HitPacket hit;
    hit.header.stickId = 1;
    hit.velocity = 127;
    const auto& ride = padInfo (Pad::ride);
    hit.quat = Quat::fromAzimuthElevation (degToRad (ride.azimuthDeg), degToRad (ride.elevationDeg)).toQ14();

    protocol::Frame frame;
    frame.packet = hit;
    state.apply (frame, 12.5);

    const auto snapshot = state.snapshot();
    const auto& padHit = snapshot.pads[static_cast<std::size_t> (Pad::ride)];
    CHECK (padHit.timeSeconds == 12.5);
    CHECK (approx (padHit.velocity, 1.0f));
    CHECK (snapshot.hitCount == 1);
    CHECK (snapshot.stickSeen[1]);
    CHECK (! snapshot.stickSeen[0]);
}

VDRUM_TEST (cymbalTopFacesUp)
{
    const auto cymbal = makeCymbal (0.23f, 32);
    // The first ring of the top surface is the rim: its normals point up and outwards.
    CHECK (cymbal.vertices.front().nz > 0.5f);
}
