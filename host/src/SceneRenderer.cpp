#include "SceneRenderer.h"

#include "KitLayout.h"
#include "Shaders.h"

#include <cmath>

namespace vdrum
{

using namespace juce::gl;

namespace
{

constexpr int kShadowMapSize = 2048;
constexpr int kMsaaSamples = 4;
constexpr double kFlashDecaySeconds = 0.18;
constexpr float kExposure = 1.0f;

// Key light: a spot above and slightly in front of the kit.
constexpr Vec3 kSpotPosition { 0.75f, -0.35f, 3.1f };
constexpr Vec3 kSpotTarget { 0.55f, 0.0f, 0.55f };
constexpr float kSpotInnerDeg = 22.0f;
constexpr float kSpotOuterDeg = 40.0f;
const Vec3 kSpotColour { 14.5f, 14.0f, 13.2f };

// Neon rims, from behind the kit towards the player.
const Vec3 kRimDirectionA = normalise ({ 0.9f, 0.9f, 0.5f });  // left, cyan
const Vec3 kRimColourA { 0.04f, 0.34f, 0.62f };
const Vec3 kRimDirectionB = normalise ({ 0.9f, -0.9f, 0.5f }); // right, magenta
const Vec3 kRimColourB { 0.58f, 0.06f, 0.36f };

// Clear colour of the HDR buffer: the darkest tone of the studio environment.
const Vec3 kBackground { 0.003f, 0.004f, 0.006f };

void setMatrix (int location, const Mat4& m)
{
    glUniformMatrix4fv (location, 1, GL_FALSE, m.data());
}

void setVector (int location, Vec3 v)
{
    glUniform3f (location, v.x, v.y, v.z);
}

} // namespace

SceneRenderer::SceneRenderer (juce::OpenGLContext& contextIn) : context (contextIn) {}

SceneRenderer::~SceneRenderer() = default;

std::unique_ptr<juce::OpenGLShaderProgram> SceneRenderer::compile (const char* vertex, const char* fragment)
{
    auto program = std::make_unique<juce::OpenGLShaderProgram> (context);
    if (! program->addVertexShader (vertex) || ! program->addFragmentShader (fragment) || ! program->link())
    {
        DBG ("Shader error: " << program->getLastError());
        return nullptr;
    }
    return program;
}

SceneRenderer::GpuMesh SceneRenderer::upload (const Mesh& mesh)
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

void SceneRenderer::release (GpuMesh& mesh)
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

bool SceneRenderer::create (const KitModel& model)
{
    sceneProgram = compile (shaders::kSceneVertex, shaders::kSceneFragment);
    shadowProgram = compile (shaders::kShadowVertex, shaders::kShadowFragment);
    tonemapProgram = compile (shaders::kFullscreenVertex, shaders::kTonemapFragment);
    if (sceneProgram == nullptr || shadowProgram == nullptr || tonemapProgram == nullptr)
        return false;

    const auto sceneLocation = [this] (const char* name) { return static_cast<int> (glGetUniformLocation (sceneProgram->getProgramID(), name)); };
    auto& u = sceneUniforms;
    u.model = sceneLocation ("model");
    u.normalMatrix = sceneLocation ("normalMatrix");
    u.viewProjection = sceneLocation ("viewProjection");
    u.lightViewProjection = sceneLocation ("lightViewProjection");
    u.baseColour = sceneLocation ("baseColour");
    u.metallic = sceneLocation ("metallic");
    u.roughness = sceneLocation ("roughness");
    u.clearcoat = sceneLocation ("clearcoat");
    u.pattern = sceneLocation ("pattern");
    u.patternScale = sceneLocation ("patternScale");
    u.emissive = sceneLocation ("emissive");
    u.cameraPosition = sceneLocation ("cameraPosition");
    u.spotPosition = sceneLocation ("spotPosition");
    u.spotDirection = sceneLocation ("spotDirection");
    u.spotColour = sceneLocation ("spotColour");
    u.spotCosInner = sceneLocation ("spotCosInner");
    u.spotCosOuter = sceneLocation ("spotCosOuter");
    u.rimDirectionA = sceneLocation ("rimDirectionA");
    u.rimColourA = sceneLocation ("rimColourA");
    u.rimDirectionB = sceneLocation ("rimDirectionB");
    u.rimColourB = sceneLocation ("rimColourB");
    u.shadowMap = sceneLocation ("shadowMap");
    u.shadowTexel = sceneLocation ("shadowTexel");

    shadowModel = static_cast<int> (glGetUniformLocation (shadowProgram->getProgramID(), "model"));
    shadowLightViewProjection = static_cast<int> (glGetUniformLocation (shadowProgram->getProgramID(), "lightViewProjection"));
    tonemapImage = static_cast<int> (glGetUniformLocation (tonemapProgram->getProgramID(), "hdrImage"));
    tonemapResolution = static_cast<int> (glGetUniformLocation (tonemapProgram->getProgramID(), "resolution"));
    tonemapExposure = static_cast<int> (glGetUniformLocation (tonemapProgram->getProgramID(), "exposure"));

    kit = model;
    meshes.clear();
    for (const auto& mesh : kit.meshes)
        meshes.push_back (upload (mesh));

    // Core profiles need a bound vertex array even for the buffer-less fullscreen pass.
    glGenVertexArrays (1, &emptyVertexArray);

    // Shadow map: depth texture with hardware comparison (filtered PCF).
    glGenTextures (1, &shadowTexture);
    glBindTexture (GL_TEXTURE_2D, shadowTexture);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowMapSize, kShadowMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture (GL_TEXTURE_2D, 0);

    glGenFramebuffers (1, &shadowFramebuffer);
    glBindFramebuffer (GL_FRAMEBUFFER, shadowFramebuffer);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTexture, 0);
    glDrawBuffer (GL_NONE);
    glReadBuffer (GL_NONE);
    const auto shadowComplete = glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer (GL_FRAMEBUFFER, context.getFrameBufferID());

    if (! shadowComplete)
    {
        DBG ("Shadow framebuffer incomplete");
        return false;
    }

    ready = true;
    return true;
}

void SceneRenderer::releaseTargets()
{
    if (msaaFramebuffer != 0)
    {
        GLuint framebuffers[2] = { msaaFramebuffer, resolveFramebuffer };
        glDeleteFramebuffers (2, framebuffers);
        GLuint renderbuffers[2] = { msaaColour, msaaDepth };
        glDeleteRenderbuffers (2, renderbuffers);
        GLuint texture = resolveTexture;
        glDeleteTextures (1, &texture);
    }
    msaaFramebuffer = msaaColour = msaaDepth = resolveFramebuffer = resolveTexture = 0;
    targetWidth = targetHeight = 0;
}

bool SceneRenderer::ensureTargets (int width, int height)
{
    if (width == targetWidth && height == targetHeight && msaaFramebuffer != 0)
        return true;

    releaseTargets();

    GLuint framebuffers[2] = {}, renderbuffers[2] = {}, texture = 0;
    glGenFramebuffers (2, framebuffers);
    glGenRenderbuffers (2, renderbuffers);
    glGenTextures (1, &texture);
    msaaFramebuffer = framebuffers[0];
    resolveFramebuffer = framebuffers[1];
    msaaColour = renderbuffers[0];
    msaaDepth = renderbuffers[1];
    resolveTexture = texture;

    glBindRenderbuffer (GL_RENDERBUFFER, msaaColour);
    glRenderbufferStorageMultisample (GL_RENDERBUFFER, kMsaaSamples, GL_RGBA16F, width, height);
    glBindRenderbuffer (GL_RENDERBUFFER, msaaDepth);
    glRenderbufferStorageMultisample (GL_RENDERBUFFER, kMsaaSamples, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer (GL_RENDERBUFFER, 0);

    glBindFramebuffer (GL_FRAMEBUFFER, msaaFramebuffer);
    glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColour);
    glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepth);
    auto complete = glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    glBindTexture (GL_TEXTURE_2D, resolveTexture);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);

    glBindFramebuffer (GL_FRAMEBUFFER, resolveFramebuffer);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture, 0);
    complete = complete && glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer (GL_FRAMEBUFFER, context.getFrameBufferID());

    if (! complete)
    {
        DBG ("HDR framebuffer incomplete");
        releaseTargets();
        return false;
    }

    targetWidth = width;
    targetHeight = height;
    return true;
}

void SceneRenderer::release()
{
    for (auto& mesh : meshes)
        release (mesh);
    meshes.clear();
    drawItems.clear();

    releaseTargets();
    if (shadowFramebuffer != 0)
    {
        GLuint framebuffer = shadowFramebuffer;
        glDeleteFramebuffers (1, &framebuffer);
        GLuint texture = shadowTexture;
        glDeleteTextures (1, &texture);
    }
    if (emptyVertexArray != 0)
    {
        GLuint vertexArray = emptyVertexArray;
        glDeleteVertexArrays (1, &vertexArray);
    }
    shadowFramebuffer = shadowTexture = emptyVertexArray = 0;

    sceneProgram.reset();
    shadowProgram.reset();
    tonemapProgram.reset();
    ready = false;
}

void SceneRenderer::collectDrawItems (const SceneState::Snapshot& snapshot, double nowSeconds)
{
    drawItems.clear();

    for (const auto& part : kit.parts)
    {
        Vec3 emissive {};
        if (part.pad >= 0)
        {
            const auto& hit = snapshot.pads[static_cast<std::size_t> (part.pad)];
            const auto age = nowSeconds - hit.timeSeconds;
            if (age >= 0.0)
                emissive = part.flashColour * (hit.velocity * static_cast<float> (std::exp (-age / kFlashDecaySeconds)));
        }
        drawItems.push_back ({ &meshes[static_cast<std::size_t> (part.mesh)], part.model, &part.material, emissive });
    }

    for (int stick = 0; stick < SceneState::kStickCount; ++stick)
    {
        Vec3 tip, grip;
        KitLayout::stickEnds (stick, snapshot.sticks[static_cast<std::size_t> (stick)], tip, grip);
        drawItems.push_back ({ &meshes[static_cast<std::size_t> (kit.stickMesh)],
                               Mat4::translation (grip) * Mat4::alignZ (tip - grip), &kit.stickMaterial, {} });
    }
}

void SceneRenderer::drawShadowPass (const Mat4& lightViewProjection)
{
    glBindFramebuffer (GL_FRAMEBUFFER, shadowFramebuffer);
    glViewport (0, 0, kShadowMapSize, kShadowMapSize);
    glClear (GL_DEPTH_BUFFER_BIT);

    glEnable (GL_POLYGON_OFFSET_FILL);
    glPolygonOffset (1.5f, 3.0f);

    shadowProgram->use();
    setMatrix (shadowLightViewProjection, lightViewProjection);
    for (const auto& item : drawItems)
    {
        if (item.mesh->indexCount == 0)
            continue;
        setMatrix (shadowModel, item.model);
        glBindVertexArray (item.mesh->vertexArray);
        glDrawElements (GL_TRIANGLES, item.mesh->indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glDisable (GL_POLYGON_OFFSET_FILL);
}

void SceneRenderer::drawScenePass (const Mat4& viewProjection, const Mat4& lightViewProjection, Vec3 eye)
{
    glBindFramebuffer (GL_FRAMEBUFFER, msaaFramebuffer);
    glViewport (0, 0, targetWidth, targetHeight);
    glClearColor (kBackground.x, kBackground.y, kBackground.z, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    sceneProgram->use();
    const auto& u = sceneUniforms;
    setMatrix (u.viewProjection, viewProjection);
    setMatrix (u.lightViewProjection, lightViewProjection);
    setVector (u.cameraPosition, eye);
    setVector (u.spotPosition, kSpotPosition);
    setVector (u.spotDirection, normalise (kSpotTarget - kSpotPosition));
    setVector (u.spotColour, kSpotColour);
    glUniform1f (u.spotCosInner, std::cos (degToRad (kSpotInnerDeg)));
    glUniform1f (u.spotCosOuter, std::cos (degToRad (kSpotOuterDeg)));
    setVector (u.rimDirectionA, kRimDirectionA);
    setVector (u.rimColourA, kRimColourA);
    setVector (u.rimDirectionB, kRimDirectionB);
    setVector (u.rimColourB, kRimColourB);

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, shadowTexture);
    glUniform1i (u.shadowMap, 0);
    glUniform1f (u.shadowTexel, 1.0f / static_cast<float> (kShadowMapSize));

    for (const auto& item : drawItems)
    {
        if (item.mesh->indexCount == 0)
            continue;

        const auto& m = *item.material;
        setMatrix (u.model, item.model);
        setMatrix (u.normalMatrix, item.model.normalMatrix());
        setVector (u.baseColour, m.baseColour);
        glUniform1f (u.metallic, m.metallic);
        glUniform1f (u.roughness, m.roughness);
        glUniform1f (u.clearcoat, m.clearcoat);
        glUniform1i (u.pattern, static_cast<int> (m.pattern));
        glUniform1f (u.patternScale, m.patternScale);
        setVector (u.emissive, item.emissive);

        glBindVertexArray (item.mesh->vertexArray);
        glDrawElements (GL_TRIANGLES, item.mesh->indexCount, GL_UNSIGNED_INT, nullptr);
    }
}

void SceneRenderer::drawTonemapPass (int width, int height)
{
    // Resolve the multisampled HDR image, then tone map it into JUCE's framebuffer.
    glBindFramebuffer (GL_READ_FRAMEBUFFER, msaaFramebuffer);
    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, resolveFramebuffer);
    glBlitFramebuffer (0, 0, targetWidth, targetHeight, 0, 0, targetWidth, targetHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer (GL_FRAMEBUFFER, context.getFrameBufferID());
    glViewport (0, 0, width, height);
    glDisable (GL_DEPTH_TEST);

    tonemapProgram->use();
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, resolveTexture);
    glUniform1i (tonemapImage, 0);
    glUniform2f (tonemapResolution, static_cast<float> (width), static_cast<float> (height));
    glUniform1f (tonemapExposure, kExposure);

    glBindVertexArray (emptyVertexArray);
    glDrawArrays (GL_TRIANGLES, 0, 3);
}

void SceneRenderer::render (const SceneState::Snapshot& snapshot, double nowSeconds, const CameraPose& camera)
{
    // JUCE sets the viewport to the component's physical pixel area before calling us.
    GLint viewport[4] = { 0, 0, 1, 1 };
    glGetIntegerv (GL_VIEWPORT, viewport);
    const auto width = juce::jmax (1, static_cast<int> (viewport[2]));
    const auto height = juce::jmax (1, static_cast<int> (viewport[3]));

    if (! ready || ! ensureTargets (width, height))
    {
        glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
        glClear (GL_COLOR_BUFFER_BIT);
        return;
    }

    collectDrawItems (snapshot, nowSeconds);

    const auto spotDirection = normalise (kSpotTarget - kSpotPosition);
    const auto lightView = Mat4::lookAt (kSpotPosition, kSpotPosition + spotDirection, { 1.0f, 0.0f, 0.0f });
    const auto lightProjection = Mat4::perspective (2.0f * degToRad (kSpotOuterDeg + 4.0f), 1.0f, 1.0f, 6.0f);
    const auto lightViewProjection = lightProjection * lightView;

    const auto view = Mat4::lookAt (camera.eye, camera.target, { 0.0f, 0.0f, 1.0f });
    const auto projection = Mat4::perspective (degToRad (42.0f), static_cast<float> (width) / static_cast<float> (height), 0.1f, 30.0f);

    glEnable (GL_DEPTH_TEST);
    glDepthFunc (GL_LEQUAL);
    glDisable (GL_CULL_FACE);
    glDisable (GL_BLEND); // JUCE leaves blending on after painting the overlay

    drawShadowPass (lightViewProjection);
    drawScenePass (projection * view, lightViewProjection, camera.eye);
    drawTonemapPass (width, height);

    // Leave the state as JUCE expects it for painting the overlay components.
    glBindVertexArray (0);
    glUseProgram (0);
    glBindTexture (GL_TEXTURE_2D, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, context.getFrameBufferID());
    glViewport (viewport[0], viewport[1], viewport[2], viewport[3]);
}

} // namespace vdrum
