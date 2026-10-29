#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

namespace vdrum
{

// Component that owns the OpenGL context used for the 3D drum kit view.
// Rendering runs on JUCE's OpenGL thread, never on the message thread.
class SceneView final : public juce::Component,
                        private juce::OpenGLRenderer
{
public:
    SceneView();
    ~SceneView() override;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    juce::OpenGLContext glContext;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneView)
};

} // namespace vdrum
