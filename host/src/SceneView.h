#pragma once

#include "KitLayout.h"
#include "Mesh.h"
#include "SceneState.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>
#include <memory>

namespace vdrum
{

// 3D view of the kit and the two sticks, rendered with OpenGL 4.1 Core.
// Rendering runs on JUCE's OpenGL thread and never touches the component: the camera comes
// through atomics, the viewport from JUCE, and scene data from SceneState snapshots.
// Mouse: drag to orbit, wheel to zoom, double-click to reset the camera.
class SceneView final : public juce::Component,
                        private juce::OpenGLRenderer
{
public:
    explicit SceneView (const SceneState& state);
    ~SceneView() override;

    // Frames rendered per second, measured on the GL thread.
    float getFramesPerSecond() const noexcept { return framesPerSecond; }

    // Text shown in the top-left corner (called on the message thread).
    void setOverlayText (const juce::String& text);

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct GpuMesh
    {
        juce::uint32 vertexArray = 0;
        juce::uint32 vertexBuffer = 0;
        juce::uint32 indexBuffer = 0;
        int indexCount = 0;
    };

    struct Material
    {
        Vec3 baseColour;       // linear RGB
        float specular = 0.2f;
        float shininess = 24.0f;
    };

    struct PadMeshes
    {
        GpuMesh body; // drum shell or cymbal
        GpuMesh head; // drum head + hoop (empty for cymbals)
        GpuMesh stand;
    };

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    bool buildShader();
    GpuMesh upload (const Mesh& mesh);
    static void release (GpuMesh& mesh);
    void draw (const GpuMesh& mesh, const Mat4& model, const Material& material, Vec3 emissive);
    void resetCamera();
    Vec3 cameraPosition() const;

    const SceneState& sceneState;
    juce::OpenGLContext glContext;
    juce::Label overlay;

    // GL thread only.
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    struct Uniforms
    {
        int model = -1, view = -1, projection = -1, normalMatrix = -1;
        int baseColour = -1, emissive = -1, specular = -1, shininess = -1, cameraPosition = -1, fogColour = -1;
    } uniforms;
    std::array<PadMeshes, kDrumKit.size()> padMeshes {};
    GpuMesh stickMesh, floorMesh;
    bool resourcesReady = false;
    Mat4 viewMatrix, projectionMatrix;
    int framesInWindow = 0;
    double fpsWindowStart = 0.0;

    // Written on the message thread, read on the GL thread.
    std::atomic<float> cameraAzimuth { 0.0f };
    std::atomic<float> cameraElevation { 0.0f };
    std::atomic<float> cameraDistance { 0.0f };
    std::atomic<float> framesPerSecond { 0.0f };

    // Message thread only.
    float dragStartAzimuth = 0.0f;
    float dragStartElevation = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneView)
};

} // namespace vdrum
