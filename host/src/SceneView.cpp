#include "SceneView.h"

namespace vdrum
{

namespace
{

// Camera orbits around the middle of the kit. Default: behind and above the player.
constexpr Vec3 kCameraTarget { 0.55f, 0.0f, 0.68f };
constexpr float kDefaultAzimuth = degToRad (196.0f); // behind the player, slightly to the right
constexpr float kDefaultElevation = degToRad (24.0f);
constexpr float kDefaultDistance = 2.2f;
constexpr float kMinElevation = degToRad (3.0f);
constexpr float kMaxElevation = degToRad (85.0f);
constexpr float kMinDistance = 0.8f;
constexpr float kMaxDistance = 5.0f;

} // namespace

SceneView::SceneView (const SceneState& state)
    : sceneState (state)
{
    resetCamera();

    overlay.setColour (juce::Label::textColourId, juce::Colour (0xffa9b6cc));
    overlay.setFont (juce::FontOptions (14.0f));
    overlay.setJustificationType (juce::Justification::topLeft);
    overlay.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (overlay);

    // The renderer does its own multisampling in HDR, so the default framebuffer needs none.
    glContext.setPixelFormat (juce::OpenGLPixelFormat (8, 8, 24, 8));

    // 4.1 Core is the highest profile available on macOS, so it is the common baseline
    // for Windows and Mac.
    glContext.setPreferredVersion (juce::OpenGLContext::Version { 4, 1 });
    glContext.setPreferredProfile (juce::OpenGLContext::Profile::core);
    glContext.setRenderer (this);
    glContext.setContinuousRepainting (true);
    glContext.attachTo (*this);
}

SceneView::~SceneView()
{
    // Must detach before the renderer (this object) is destroyed.
    glContext.detach();
}

void SceneView::setOverlayText (const juce::String& text)
{
    overlay.setText (text, juce::dontSendNotification);
}

void SceneView::resized()
{
    overlay.setBounds (getLocalBounds().reduced (12).removeFromTop (80));
}

void SceneView::resetCamera()
{
    cameraAzimuth = kDefaultAzimuth;
    cameraElevation = kDefaultElevation;
    cameraDistance = kDefaultDistance;
}

void SceneView::mouseDown (const juce::MouseEvent&)
{
    dragStartAzimuth = cameraAzimuth;
    dragStartElevation = cameraElevation;
}

void SceneView::mouseDrag (const juce::MouseEvent& event)
{
    cameraAzimuth = dragStartAzimuth - 0.008f * static_cast<float> (event.getDistanceFromDragStartX());
    cameraElevation = juce::jlimit (kMinElevation, kMaxElevation,
                                    dragStartElevation + 0.006f * static_cast<float> (event.getDistanceFromDragStartY()));
}

void SceneView::mouseDoubleClick (const juce::MouseEvent&)
{
    resetCamera();
}

void SceneView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    cameraDistance = juce::jlimit (kMinDistance, kMaxDistance, cameraDistance * (1.0f - 0.5f * wheel.deltaY));
}

CameraPose SceneView::cameraPose() const
{
    return { kCameraTarget + directionFromAngles (cameraAzimuth, cameraElevation) * cameraDistance.load(), kCameraTarget };
}

void SceneView::newOpenGLContextCreated()
{
    DBG ("OpenGL context created: " << juce::String (reinterpret_cast<const char*> (juce::gl::glGetString (juce::gl::GL_VERSION))));
    renderer.create (KitModel::build());
    fpsWindowStart = steadySeconds();
    framesInWindow = 0;
}

void SceneView::renderOpenGL()
{
    const auto now = steadySeconds();
    ++framesInWindow;
    if (now - fpsWindowStart >= 0.5)
    {
        framesPerSecond = static_cast<float> (framesInWindow / (now - fpsWindowStart));
        framesInWindow = 0;
        fpsWindowStart = now;
    }

    renderer.render (sceneState.snapshot(), now, cameraPose());
}

void SceneView::openGLContextClosing()
{
    renderer.release();
}

} // namespace vdrum
