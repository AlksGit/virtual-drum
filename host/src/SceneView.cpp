#include "SceneView.h"

#include <cmath>

namespace vdrum
{

using namespace juce::gl;

namespace
{

// Camera orbits around the middle of the kit. Default: behind and above the player.
constexpr Vec3 kCameraTarget { 0.55f, 0.0f, 0.72f };
constexpr float kDefaultAzimuth = kPi;            // behind the player (-X side)
constexpr float kDefaultElevation = degToRad (28.0f);
constexpr float kDefaultDistance = 2.1f;
constexpr float kMinElevation = degToRad (5.0f);
constexpr float kMaxElevation = degToRad (85.0f);
constexpr float kMinDistance = 0.8f;
constexpr float kMaxDistance = 5.0f;

constexpr double kFlashDecaySeconds = 0.18;

const char* const kVertexShader = R"(#version 410 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 normalMatrix;

out vec3 worldPosition;
out vec3 worldNormal;

void main()
{
    vec4 world = model * vec4 (position, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3 (normalMatrix) * normal;
    gl_Position = projection * view * world;
}
)";

const char* const kFragmentShader = R"(#version 410 core
in vec3 worldPosition;
in vec3 worldNormal;

uniform vec3 baseColour;   // linear RGB
uniform vec3 emissive;     // linear RGB, added on top
uniform float specular;
uniform float shininess;
uniform vec3 cameraPosition;
uniform vec3 fogColour;    // linear RGB, same as the clear colour

out vec4 fragColour;

void main()
{
    vec3 n = normalize (worldNormal);
    if (! gl_FrontFacing)
        n = -n;

    vec3 v = normalize (cameraPosition - worldPosition);
    vec3 keyLight = normalize (vec3 (0.4, 0.5, 1.0));
    vec3 fillLight = normalize (vec3 (-0.7, -0.3, 0.4));

    float diffuse = 0.85 * max (dot (n, keyLight), 0.0) + 0.25 * max (dot (n, fillLight), 0.0);
    vec3 h = normalize (keyLight + v);
    float highlight = specular * pow (max (dot (n, h), 0.0), shininess);
    float rim = 0.20 * pow (1.0 - max (dot (n, v), 0.0), 3.0);

    vec3 colour = baseColour * (0.10 + diffuse) + vec3 (highlight) + rim * vec3 (0.25, 0.55, 1.0) + emissive;

    // Fade into the background with distance from the kit.
    float fog = clamp ((length (worldPosition.xy - vec2 (0.4, 0.0)) - 0.9) / 1.4, 0.0, 1.0);
    colour = mix (colour, fogColour, fog);

    fragColour = vec4 (pow (colour, vec3 (1.0 / 2.2)), 1.0);
}
)";

// sRGB hex colour to linear RGB.
Vec3 linearColour (juce::uint32 rgb)
{
    const auto channel = [] (juce::uint32 value) { return std::pow (static_cast<float> (value) / 255.0f, 2.2f); };
    return { channel ((rgb >> 16) & 0xffu), channel ((rgb >> 8) & 0xffu), channel (rgb & 0xffu) };
}

const Vec3 kBackground = linearColour (0x07090e);
const Vec3 kDrumFlash { 0.10f, 0.75f, 1.00f };   // neon cyan
const Vec3 kCymbalFlash { 1.00f, 0.55f, 0.12f }; // warm amber

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

// Vertical tube from the floor up to `top`, built directly in world coordinates.
Mesh makeStand (Vec3 top, float radius)
{
    return translated (makeTube (radius, radius * 0.8f, top.z, 16), { top.x, top.y, 0.0f });
}

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

    // 24-bit depth: JUCE defaults to 16, which makes the close cymbal surfaces z-fight
    // (macOS honours the request exactly).
    juce::OpenGLPixelFormat pixelFormat (8, 8, 24, 8);
    pixelFormat.multisamplingLevel = 4;
    glContext.setPixelFormat (pixelFormat);
    glContext.setMultisamplingEnabled (true);

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
    overlay.setBounds (getLocalBounds().reduced (12).removeFromTop (60));
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

Vec3 SceneView::cameraPosition() const
{
    return kCameraTarget + directionFromAngles (cameraAzimuth, cameraElevation) * cameraDistance.load();
}

bool SceneView::buildShader()
{
    shader = std::make_unique<juce::OpenGLShaderProgram> (glContext);
    if (! shader->addVertexShader (kVertexShader) || ! shader->addFragmentShader (kFragmentShader) || ! shader->link())
    {
        DBG ("Shader error: " << shader->getLastError());
        shader.reset();
        return false;
    }

    const auto location = [this] (const char* name) { return static_cast<int> (glGetUniformLocation (shader->getProgramID(), name)); };
    uniforms.model = location ("model");
    uniforms.view = location ("view");
    uniforms.projection = location ("projection");
    uniforms.normalMatrix = location ("normalMatrix");
    uniforms.baseColour = location ("baseColour");
    uniforms.emissive = location ("emissive");
    uniforms.specular = location ("specular");
    uniforms.shininess = location ("shininess");
    uniforms.cameraPosition = location ("cameraPosition");
    uniforms.fogColour = location ("fogColour");
    return true;
}

SceneView::GpuMesh SceneView::upload (const Mesh& mesh)
{
    GpuMesh gpu;
    if (mesh.indices.empty())
        return gpu;

    GLuint vertexArray = 0, buffers[2] = {};
    glGenVertexArrays (1, &vertexArray);
    glGenBuffers (2, buffers);

    glBindVertexArray (vertexArray);
    glBindBuffer (GL_ARRAY_BUFFER, buffers[0]);
    glBufferData (GL_ARRAY_BUFFER, static_cast<GLsizeiptr> (mesh.vertices.size() * sizeof (Vertex)), mesh.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr> (mesh.indices.size() * sizeof (std::uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray (0);
    glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, sizeof (Vertex), reinterpret_cast<const void*> (offsetof (Vertex, px)));
    glEnableVertexAttribArray (1);
    glVertexAttribPointer (1, 3, GL_FLOAT, GL_FALSE, sizeof (Vertex), reinterpret_cast<const void*> (offsetof (Vertex, nx)));
    glBindVertexArray (0);

    gpu.vertexArray = vertexArray;
    gpu.vertexBuffer = buffers[0];
    gpu.indexBuffer = buffers[1];
    gpu.indexCount = static_cast<int> (mesh.indices.size());
    return gpu;
}

void SceneView::release (GpuMesh& mesh)
{
    if (mesh.vertexArray != 0)
    {
        GLuint buffers[2] = { mesh.vertexBuffer, mesh.indexBuffer };
        glDeleteBuffers (2, buffers);
        GLuint vertexArray = mesh.vertexArray;
        glDeleteVertexArrays (1, &vertexArray);
    }
    mesh = {};
}

void SceneView::newOpenGLContextCreated()
{
    DBG ("OpenGL context created: " << juce::String (reinterpret_cast<const char*> (glGetString (GL_VERSION))));

    if (! buildShader())
        return;

    for (const auto& info : kDrumKit)
    {
        const auto placement = KitLayout::placement (info.pad);
        auto& meshes = padMeshes[static_cast<std::size_t> (info.pad)];
        float standTop = 0.0f;

        if (placement.shape == PadShape::drum)
        {
            auto shell = translated (makeTube (placement.radius, placement.radius, placement.depth, 64), { 0.0f, 0.0f, -placement.depth });
            append (shell, makeDisc (placement.radius, -placement.depth, 64, false));
            meshes.body = upload (shell);

            auto head = makeDisc (placement.radius, 0.0f, 64, true);
            append (head, translated (makeTube (placement.radius * 1.03f, placement.radius * 1.03f, 0.014f, 64), { 0.0f, 0.0f, -0.006f }));
            meshes.head = upload (head);

            standTop = placement.strikeCentre.z - placement.depth;
        }
        else
        {
            auto cymbal = makeCymbal (placement.radius, 64);
            if (info.pad == Pad::hiHat)
                append (cymbal, translated (makeCymbal (placement.radius, 64), { 0.0f, 0.0f, -0.025f }));
            meshes.body = upload (cymbal);

            standTop = placement.strikeCentre.z - 0.03f;
        }

        meshes.stand = upload (makeStand ({ placement.strikeCentre.x, placement.strikeCentre.y, standTop }, 0.012f));
    }

    stickMesh = upload (makeStick (KitLayout::kStickLength, 0.0075f, 0.004f, 16));
    floorMesh = upload (makeDisc (2.6f, 0.0f, 96, true));

    fpsWindowStart = steadySeconds();
    framesInWindow = 0;
    resourcesReady = true;
}

void SceneView::openGLContextClosing()
{
    for (auto& meshes : padMeshes)
    {
        release (meshes.body);
        release (meshes.head);
        release (meshes.stand);
    }
    release (stickMesh);
    release (floorMesh);
    shader.reset();
    resourcesReady = false;
}

void SceneView::draw (const GpuMesh& mesh, const Mat4& model, const Material& material, Vec3 emissive)
{
    if (mesh.indexCount == 0)
        return;

    glUniformMatrix4fv (uniforms.model, 1, GL_FALSE, model.data());
    glUniformMatrix4fv (uniforms.normalMatrix, 1, GL_FALSE, model.normalMatrix().data());
    glUniform3f (uniforms.baseColour, material.baseColour.x, material.baseColour.y, material.baseColour.z);
    glUniform3f (uniforms.emissive, emissive.x, emissive.y, emissive.z);
    glUniform1f (uniforms.specular, material.specular);
    glUniform1f (uniforms.shininess, material.shininess);

    glBindVertexArray (mesh.vertexArray);
    glDrawElements (GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
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

    // JUCE sets the viewport to the component's physical pixel area (Retina included) before
    // calling us: reuse it rather than reading the component size from this thread.
    GLint viewport[4] = { 0, 0, 1, 1 };
    glGetIntegerv (GL_VIEWPORT, viewport);
    const auto aspect = static_cast<float> (juce::jmax (1, viewport[2])) / static_cast<float> (juce::jmax (1, viewport[3]));

    const auto background = std::pow (kBackground.x, 1.0f / 2.2f);
    glClearColor (background, std::pow (kBackground.y, 1.0f / 2.2f), std::pow (kBackground.z, 1.0f / 2.2f), 1.0f);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (! resourcesReady)
        return;

    glEnable (GL_DEPTH_TEST);
    glDepthFunc (GL_LEQUAL);
    glDisable (GL_CULL_FACE);
    glDisable (GL_BLEND); // JUCE leaves blending on after painting the overlay

    shader->use();

    const auto eye = cameraPosition();
    viewMatrix = Mat4::lookAt (eye, kCameraTarget, { 0.0f, 0.0f, 1.0f });
    projectionMatrix = Mat4::perspective (degToRad (45.0f), aspect, 0.1f, 20.0f);
    glUniformMatrix4fv (uniforms.view, 1, GL_FALSE, viewMatrix.data());
    glUniformMatrix4fv (uniforms.projection, 1, GL_FALSE, projectionMatrix.data());
    glUniform3f (uniforms.cameraPosition, eye.x, eye.y, eye.z);
    glUniform3f (uniforms.fogColour, kBackground.x, kBackground.y, kBackground.z);

    static const Material floorMaterial { linearColour (0x10141d), 0.05f, 8.0f };
    static const Material shellMaterial { linearColour (0x1d3566), 0.45f, 48.0f };
    static const Material headMaterial { linearColour (0xd9d4c7), 0.08f, 12.0f };
    static const Material cymbalMaterial { linearColour (0xb98f4e), 0.70f, 64.0f };
    static const Material standMaterial { linearColour (0x50566a), 0.55f, 40.0f };
    static const Material stickMaterial { linearColour (0xcfa66a), 0.25f, 20.0f };

    const auto noEmission = Vec3 {};
    draw (floorMesh, Mat4::identity(), floorMaterial, noEmission);

    const auto snapshot = sceneState.snapshot();

    for (const auto& info : kDrumKit)
    {
        const auto index = static_cast<std::size_t> (info.pad);
        const auto placement = KitLayout::placement (info.pad);
        const auto model = Mat4::translation (placement.strikeCentre) * Mat4::alignZ (placement.surfaceNormal);

        const auto& hit = snapshot.pads[index];
        const auto age = now - hit.timeSeconds;
        const auto flash = age >= 0.0 ? hit.velocity * static_cast<float> (std::exp (-age / kFlashDecaySeconds)) : 0.0f;

        const auto& meshes = padMeshes[index];
        draw (meshes.stand, Mat4::identity(), standMaterial, noEmission);

        if (placement.shape == PadShape::drum)
        {
            draw (meshes.body, model, shellMaterial, kDrumFlash * (0.35f * flash));
            draw (meshes.head, model, headMaterial, kDrumFlash * (1.6f * flash));
        }
        else
        {
            draw (meshes.body, model, cymbalMaterial, kCymbalFlash * (1.4f * flash));
        }
    }

    for (int stick = 0; stick < SceneState::kStickCount; ++stick)
    {
        Vec3 tip, grip;
        KitLayout::stickEnds (stick, snapshot.sticks[static_cast<std::size_t> (stick)], tip, grip);
        draw (stickMesh, Mat4::translation (grip) * Mat4::alignZ (tip - grip), stickMaterial, noEmission);
    }

    // Leave the state as JUCE expects it for painting the overlay components.
    glBindVertexArray (0);
    glUseProgram (0);
    glDisable (GL_DEPTH_TEST);
}

} // namespace vdrum
