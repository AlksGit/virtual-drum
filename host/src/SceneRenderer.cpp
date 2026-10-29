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
constexpr float kBloomThreshold = 1.0f;
constexpr float kBloomStrength = 0.12f;

// Cymbal swing after a hit: damped oscillation, amplitude scaled by velocity.
constexpr float kSwingAmplitudeRad = 0.11f;
constexpr float kSwingFrequencyHz = 2.4f;
constexpr float kSwingDecaySeconds = 0.9f;

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
    bloomDownProgram = compile (shaders::kFullscreenVertex, shaders::kBloomDownsampleFragment);
    bloomUpProgram = compile (shaders::kFullscreenVertex, shaders::kBloomUpsampleFragment);
    if (sceneProgram == nullptr || shadowProgram == nullptr || tonemapProgram == nullptr
        || bloomDownProgram == nullptr || bloomUpProgram == nullptr)
        return false;

    for (auto* entry : { &bloomDown, &bloomUp })
    {
        const auto id = (entry == &bloomDown ? bloomDownProgram : bloomUpProgram)->getProgramID();
        entry->source = static_cast<int> (glGetUniformLocation (id, "source"));
        entry->sourceTexel = static_cast<int> (glGetUniformLocation (id, "sourceTexel"));
        entry->uvScale = static_cast<int> (glGetUniformLocation (id, "uvScale"));
        entry->targetSize = static_cast<int> (glGetUniformLocation (id, "targetSize"));
        entry->firstPass = static_cast<int> (glGetUniformLocation (id, "firstPass"));
        entry->threshold = static_cast<int> (glGetUniformLocation (id, "threshold"));
        entry->radius = static_cast<int> (glGetUniformLocation (id, "radius"));
    }

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
    u.hitAge = sceneLocation ("hitAge");
    u.hitStrength = sceneLocation ("hitStrength");
    u.rippleColour = sceneLocation ("rippleColour");

    shadowModel = static_cast<int> (glGetUniformLocation (shadowProgram->getProgramID(), "model"));
    shadowLightViewProjection = static_cast<int> (glGetUniformLocation (shadowProgram->getProgramID(), "lightViewProjection"));
    tonemapImage = static_cast<int> (glGetUniformLocation (tonemapProgram->getProgramID(), "hdrImage"));
    tonemapBloom = static_cast<int> (glGetUniformLocation (tonemapProgram->getProgramID(), "bloomImage"));
    tonemapBloomStrength = static_cast<int> (glGetUniformLocation (tonemapProgram->getProgramID(), "bloomStrength"));
    tonemapResolution = static_cast<int> (glGetUniformLocation (tonemapProgram->getProgramID(), "resolution"));
    tonemapImageScale = static_cast<int> (glGetUniformLocation (tonemapProgram->getProgramID(), "imageScale"));
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
    viewWidth = width;
    viewHeight = height;

    // Grow-only, in steps of 256 pixels: a live window resize then reallocates a few times
    // instead of at every step (the 4x HDR targets are large on Retina screens).
    if (msaaFramebuffer != 0 && width <= targetWidth && height <= targetHeight)
        return true;

    const auto roundUp = [] (int value) { return (value + 255) / 256 * 256; };
    width = roundUp (juce::jmax (width, targetWidth));
    height = roundUp (juce::jmax (height, targetHeight));

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
        ready = false; // do not retry (and log) at every frame
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
    releaseBloomTargets();
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
    bloomDownProgram.reset();
    bloomUpProgram.reset();
    ready = false;
}

void SceneRenderer::collectDrawItems (const SceneState::Snapshot& snapshot, double nowSeconds)
{
    drawItems.clear();

    for (const auto& part : kit.parts)
    {
        DrawItem item { &meshes[static_cast<std::size_t> (part.mesh)], part.model, &part.material, part.material.glow };

        if (part.pad >= 0)
        {
            const auto& hit = snapshot.pads[static_cast<std::size_t> (part.pad)];
            const auto age = static_cast<float> (nowSeconds - hit.timeSeconds);
            if (age >= 0.0f && age < 10.0f)
            {
                item.emissive = item.emissive + part.flashColour * (hit.velocity * std::exp (-age / static_cast<float> (kFlashDecaySeconds)));
                item.hitAge = age;
                item.hitStrength = hit.velocity;
                item.rippleColour = part.flashColour;

                if (part.wobbles)
                {
                    // Swing around the horizontal axis facing the player, like a cymbal pushed
                    // down on its near edge.
                    const auto angle = kSwingAmplitudeRad * part.wobbleAmount * hit.velocity
                                     * std::exp (-age / kSwingDecaySeconds) * std::sin (2.0f * kPi * kSwingFrequencyHz * age);
                    const auto towardsPlayer = normalise (Vec3 { KitLayout::kPivot.x - part.pivot.x, KitLayout::kPivot.y - part.pivot.y, 0.0f });
                    const auto axis = normalise (cross (Vec3 { 0.0f, 0.0f, 1.0f }, towardsPlayer));
                    item.model = Mat4::translation (part.pivot) * Mat4::rotation (Quat::fromAxisAngle (axis, angle))
                               * Mat4::translation (part.pivot * -1.0f) * part.model;
                }
            }
        }
        drawItems.push_back (item);
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
    glViewport (0, 0, viewWidth, viewHeight);
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
        glUniform1f (u.hitAge, item.hitAge);
        glUniform1f (u.hitStrength, item.hitStrength);
        setVector (u.rippleColour, item.rippleColour);

        glBindVertexArray (item.mesh->vertexArray);
        glDrawElements (GL_TRIANGLES, item.mesh->indexCount, GL_UNSIGNED_INT, nullptr);
    }
}

void SceneRenderer::releaseBloomTargets()
{
    if (bloomLevelCount > 0)
    {
        glDeleteFramebuffers (bloomLevelCount, bloomFramebuffers);
        glDeleteTextures (bloomLevelCount, bloomTextures);
    }
    for (int i = 0; i < kBloomLevels; ++i)
        bloomFramebuffers[i] = bloomTextures[i] = 0;
    bloomLevelCount = 0;
    bloomViewWidth = bloomViewHeight = 0;
}

bool SceneRenderer::ensureBloomTargets (int width, int height)
{
    if (bloomLevelCount > 0 && width == bloomViewWidth && height == bloomViewHeight)
        return true;

    // Half resolution and below: reallocating on resize is cheap.
    releaseBloomTargets();

    auto complete = true;
    for (int i = 0; i < kBloomLevels; ++i)
    {
        const auto w = width >> (i + 1);
        const auto h = height >> (i + 1);
        if (w < 2 || h < 2)
            break;

        GLuint framebuffer = 0, texture = 0;
        glGenFramebuffers (1, &framebuffer);
        glGenTextures (1, &texture);
        glBindTexture (GL_TEXTURE_2D, texture);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer (GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        complete = complete && glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

        bloomFramebuffers[i] = framebuffer;
        bloomTextures[i] = texture;
        bloomWidths[i] = w;
        bloomHeights[i] = h;
        bloomLevelCount = i + 1;
    }
    glBindTexture (GL_TEXTURE_2D, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, context.getFrameBufferID());

    if (! complete || bloomLevelCount == 0)
    {
        DBG ("Bloom framebuffers incomplete");
        releaseBloomTargets();
        return false;
    }

    bloomViewWidth = width;
    bloomViewHeight = height;
    return true;
}

void SceneRenderer::drawBloomPass()
{
    glDisable (GL_DEPTH_TEST);
    glBindVertexArray (emptyVertexArray);
    glActiveTexture (GL_TEXTURE0);

    // Downsample: bright parts of the HDR image, then successive halvings.
    bloomDownProgram->use();
    glUniform1i (bloomDown.source, 0);
    glUniform1f (bloomDown.threshold, kBloomThreshold);
    for (int i = 0; i < bloomLevelCount; ++i)
    {
        glBindFramebuffer (GL_FRAMEBUFFER, bloomFramebuffers[i]);
        glViewport (0, 0, bloomWidths[i], bloomHeights[i]);

        const auto first = i == 0;
        glBindTexture (GL_TEXTURE_2D, first ? resolveTexture : bloomTextures[i - 1]);
        const auto sourceWidth = first ? targetWidth : bloomWidths[i - 1];
        const auto sourceHeight = first ? targetHeight : bloomHeights[i - 1];
        glUniform2f (bloomDown.sourceTexel, 1.0f / static_cast<float> (sourceWidth), 1.0f / static_cast<float> (sourceHeight));
        glUniform2f (bloomDown.uvScale,
                     first ? static_cast<float> (viewWidth) / static_cast<float> (targetWidth) : 1.0f,
                     first ? static_cast<float> (viewHeight) / static_cast<float> (targetHeight) : 1.0f);
        glUniform2f (bloomDown.targetSize, static_cast<float> (bloomWidths[i]), static_cast<float> (bloomHeights[i]));
        glUniform1i (bloomDown.firstPass, first ? 1 : 0);
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }

    // Upsample: blur each level and add it onto the next larger one.
    bloomUpProgram->use();
    glUniform1i (bloomUp.source, 0);
    glUniform1f (bloomUp.radius, 1.0f);
    glEnable (GL_BLEND);
    glBlendFunc (GL_ONE, GL_ONE);
    for (int i = bloomLevelCount - 1; i > 0; --i)
    {
        glBindFramebuffer (GL_FRAMEBUFFER, bloomFramebuffers[i - 1]);
        glViewport (0, 0, bloomWidths[i - 1], bloomHeights[i - 1]);
        glBindTexture (GL_TEXTURE_2D, bloomTextures[i]);
        glUniform2f (bloomUp.sourceTexel, 1.0f / static_cast<float> (bloomWidths[i]), 1.0f / static_cast<float> (bloomHeights[i]));
        glUniform2f (bloomUp.targetSize, static_cast<float> (bloomWidths[i - 1]), static_cast<float> (bloomHeights[i - 1]));
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }
    glDisable (GL_BLEND);
}

void SceneRenderer::drawTonemapPass (int width, int height)
{
    glBindFramebuffer (GL_FRAMEBUFFER, context.getFrameBufferID());
    glViewport (0, 0, width, height);
    glDisable (GL_DEPTH_TEST);

    tonemapProgram->use();
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, resolveTexture);
    glUniform1i (tonemapImage, 0);
    glActiveTexture (GL_TEXTURE1);
    glBindTexture (GL_TEXTURE_2D, bloomLevelCount > 0 ? bloomTextures[0] : 0);
    glUniform1i (tonemapBloom, 1);
    glUniform1f (tonemapBloomStrength, bloomLevelCount > 0 ? kBloomStrength : 0.0f);
    glActiveTexture (GL_TEXTURE0);
    glUniform2f (tonemapResolution, static_cast<float> (width), static_cast<float> (height));
    glUniform2f (tonemapImageScale, static_cast<float> (viewWidth) / static_cast<float> (targetWidth),
                 static_cast<float> (viewHeight) / static_cast<float> (targetHeight));
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

    // Resolve the multisampled HDR image into a texture.
    glBindFramebuffer (GL_READ_FRAMEBUFFER, msaaFramebuffer);
    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, resolveFramebuffer);
    glBlitFramebuffer (0, 0, viewWidth, viewHeight, 0, 0, viewWidth, viewHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    if (ensureBloomTargets (width, height))
        drawBloomPass();
    drawTonemapPass (width, height);

    // Leave the state as JUCE expects it for painting the overlay components.
    glBindVertexArray (0);
    glUseProgram (0);
    glActiveTexture (GL_TEXTURE1);
    glBindTexture (GL_TEXTURE_2D, 0);
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, context.getFrameBufferID());
    glViewport (viewport[0], viewport[1], viewport[2], viewport[3]);
}

} // namespace vdrum
