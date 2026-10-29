#pragma once

#include "KitModel.h"
#include "SceneState.h"

#include <juce_opengl/juce_opengl.h>

#include <memory>
#include <vector>

namespace vdrum
{

struct CameraPose
{
    Vec3 eye;
    Vec3 target;
};

// Draws the kit on JUCE's OpenGL thread. All methods must be called with the context
// active (from the OpenGLRenderer callbacks).
//
// Passes: shadow map from the key spot light, physically based scene into a multisampled
// HDR framebuffer, then tone mapping into the framebuffer JUCE provides.
class SceneRenderer
{
public:
    explicit SceneRenderer (juce::OpenGLContext& context);
    ~SceneRenderer();

    // Returns false (and logs) if a shader fails to compile; the view then stays empty.
    bool create (const KitModel& model);
    void release();

    void render (const SceneState::Snapshot& snapshot, double nowSeconds, const CameraPose& camera);

private:
    struct GpuMesh
    {
        juce::uint32 vertexArray = 0;
        juce::uint32 vertexBuffer = 0;
        juce::uint32 indexBuffer = 0;
        int indexCount = 0;
    };

    struct DrawItem
    {
        const GpuMesh* mesh;
        Mat4 model;
        const Material* material;
        Vec3 emissive;
    };

    struct SceneUniforms
    {
        int model = -1, normalMatrix = -1, viewProjection = -1, lightViewProjection = -1;
        int baseColour = -1, metallic = -1, roughness = -1, clearcoat = -1, pattern = -1, patternScale = -1, emissive = -1;
        int cameraPosition = -1, spotPosition = -1, spotDirection = -1, spotColour = -1, spotCosInner = -1, spotCosOuter = -1;
        int rimDirectionA = -1, rimColourA = -1, rimDirectionB = -1, rimColourB = -1;
        int shadowMap = -1, shadowTexel = -1;
    };

    std::unique_ptr<juce::OpenGLShaderProgram> compile (const char* vertex, const char* fragment);
    GpuMesh upload (const Mesh& mesh);
    static void release (GpuMesh& mesh);
    bool ensureTargets (int width, int height);
    void releaseTargets();
    void collectDrawItems (const SceneState::Snapshot& snapshot, double nowSeconds);
    void drawShadowPass (const Mat4& lightViewProjection);
    void drawScenePass (const Mat4& viewProjection, const Mat4& lightViewProjection, Vec3 eye);
    void drawTonemapPass (int width, int height);

    juce::OpenGLContext& context;
    KitModel kit;
    std::vector<GpuMesh> meshes;
    std::vector<DrawItem> drawItems;

    std::unique_ptr<juce::OpenGLShaderProgram> sceneProgram, shadowProgram, tonemapProgram;
    SceneUniforms sceneUniforms;
    int shadowModel = -1, shadowLightViewProjection = -1;
    int tonemapImage = -1, tonemapResolution = -1, tonemapExposure = -1;

    juce::uint32 emptyVertexArray = 0;
    juce::uint32 shadowFramebuffer = 0, shadowTexture = 0;
    juce::uint32 msaaFramebuffer = 0, msaaColour = 0, msaaDepth = 0;
    juce::uint32 resolveFramebuffer = 0, resolveTexture = 0;
    int targetWidth = 0, targetHeight = 0;
    bool ready = false;
};

} // namespace vdrum
