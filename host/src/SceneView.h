#pragma once

#include "SceneRenderer.h"
#include "SceneState.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <atomic>

namespace vdrum
{

// 3D view of the kit and the two sticks (OpenGL 4.1 Core, rendering in SceneRenderer).
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

    // Camera azimuth around the kit in degrees (0 = from the front of the kit, 180 = behind
    // the player). Message thread.
    void setCameraAzimuthDegrees (float degrees) { cameraAzimuth = degToRad (degrees); }

    // Text shown in the top-left corner (called on the message thread).
    void setOverlayText (const juce::String& text);

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void resetCamera();
    CameraPose cameraPose() const;

    const SceneState& sceneState;
    juce::OpenGLContext glContext;
    SceneRenderer renderer { glContext }; // GL thread only
    juce::Label overlay;

    int framesInWindow = 0;     // GL thread only
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
