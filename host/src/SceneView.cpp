#include "SceneView.h"

namespace vdrum
{

SceneView::SceneView()
{
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

void SceneView::newOpenGLContextCreated()
{
    DBG ("OpenGL context created: " << juce::String (reinterpret_cast<const char*> (juce::gl::glGetString (juce::gl::GL_VERSION))));
}

void SceneView::renderOpenGL()
{
    using namespace juce::gl;

    const auto scale = static_cast<float> (glContext.getRenderingScale());
    glViewport (0, 0,
                juce::roundToInt (scale * static_cast<float> (getWidth())),
                juce::roundToInt (scale * static_cast<float> (getHeight())));

    const auto background = juce::Colour (0xff10131a);
    glClearColor (background.getFloatRed(), background.getFloatGreen(), background.getFloatBlue(), 1.0f);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void SceneView::openGLContextClosing()
{
    // No GL resources yet. Shaders and buffers of the 3D scene will be released here.
}

} // namespace vdrum
