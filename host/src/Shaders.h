#pragma once

// GLSL 4.10 core shaders of the 3D view (OpenGL 4.1 is the macOS maximum).

namespace vdrum::shaders
{

// --- Scene: physically based shading ---------------------------------------------------

inline const char* const kSceneVertex = R"(#version 410 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;

uniform mat4 model;
uniform mat4 normalMatrix;
uniform mat4 viewProjection;
uniform mat4 lightViewProjection;

out vec3 worldPosition;
out vec3 worldNormal;
out vec3 localPosition;
out vec4 lightSpacePosition;

void main()
{
    vec4 world = model * vec4 (position, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3 (normalMatrix) * normal;
    localPosition = position;
    lightSpacePosition = lightViewProjection * world;
    gl_Position = viewProjection * world;
}
)";

inline const char* const kSceneFragment = R"(#version 410 core
in vec3 worldPosition;
in vec3 worldNormal;
in vec3 localPosition;
in vec4 lightSpacePosition;

uniform vec3 baseColour;
uniform float metallic;
uniform float roughness;
uniform float clearcoat;
uniform int pattern;
uniform float patternScale;
uniform vec3 emissive;
uniform float hitAge;      // seconds since the last hit on this part's pad (large if none)
uniform float hitStrength; // velocity of that hit, 0..1
uniform vec3 rippleColour; // colour of the ring that runs across a struck head

uniform vec3 cameraPosition;
uniform vec3 spotPosition;
uniform vec3 spotDirection;
uniform vec3 spotColour;
uniform float spotCosInner;
uniform float spotCosOuter;
uniform vec3 rimDirectionA;
uniform vec3 rimColourA;
uniform vec3 rimDirectionB;
uniform vec3 rimColourB;

uniform sampler2DShadow shadowMap;
uniform float shadowTexel;

out vec4 fragColour;

const float PI = 3.14159265;

float hash (vec3 p)
{
    p = fract (p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract (p.x * p.y * p.z * (p.x + p.y + p.z));
}

float valueNoise (vec3 x)
{
    vec3 i = floor (x);
    vec3 f = fract (x);
    f = f * f * (3.0 - 2.0 * f);
    return mix (mix (mix (hash (i), hash (i + vec3 (1, 0, 0)), f.x),
                     mix (hash (i + vec3 (0, 1, 0)), hash (i + vec3 (1, 1, 0)), f.x), f.y),
                mix (mix (hash (i + vec3 (0, 0, 1)), hash (i + vec3 (1, 0, 1)), f.x),
                     mix (hash (i + vec3 (0, 1, 1)), hash (i + vec3 (1, 1, 1)), f.x), f.y), f.z);
}

float distributionGGX (float NoH, float a)
{
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float visibilitySmithGGX (float NoV, float NoL, float a)
{
    float k = 0.5 * a;
    float gv = NoV / (NoV * (1.0 - k) + k);
    float gl = NoL / (NoL * (1.0 - k) + k);
    return gv * gl / max (4.0 * NoV * NoL, 1.0e-4);
}

vec3 fresnelSchlick (vec3 f0, float cosTheta)
{
    return f0 + (1.0 - f0) * pow (clamp (1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Analytic approximation of the split-sum environment BRDF (Karis).
vec3 environmentBRDF (vec3 f0, float rough, float NoV)
{
    const vec4 c0 = vec4 (-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4 (1.0, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min (r.x * r.x, exp2 (-9.28 * NoV)) * r.x + r.y;
    vec2 ab = vec2 (-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// Studio surroundings seen in reflections: a dark room, a large softbox above the kit and
// two neon strips behind it (cyan on the left, magenta on the right). Rougher surfaces see
// a blurrier version.
vec3 environment (vec3 d, float rough)
{
    float blur = mix (0.01, 0.45, rough * rough);
    vec3 colour = mix (vec3 (0.003, 0.004, 0.006), vec3 (0.012, 0.014, 0.020), smoothstep (-0.3, 0.7, d.z));

    float softbox = smoothstep (0.86 - blur, 0.90 + 0.3 * blur, dot (d, normalize (vec3 (0.3, 0.0, 1.0))));
    colour += softbox * vec3 (1.6, 1.55, 1.5) * (1.0 - 0.7 * blur);

    // Below the horizon: the rug lit by the spot, as seen in chrome and cymbals.
    float floorPool = (1.0 - smoothstep (-0.30, -0.05, d.z)) * smoothstep (-0.95, -0.55, d.z) * (1.0 - 0.3 * blur);
    colour += floorPool * vec3 (0.09, 0.085, 0.095);

    // A soft warm band around the horizon (distant room lights), so chrome has something to show.
    float t = (d.z - 0.06) / (0.035 + 0.4 * blur);
    float horizon = exp (-t * t); // not pow (x, 2.0): pow is undefined for x < 0
    colour += horizon * vec3 (0.85, 0.80, 0.72) * (1.0 - 0.7 * blur);

    float azimuth = atan (d.y, d.x);
    float band = smoothstep (-0.05, 0.10, d.z) * (1.0 - smoothstep (0.45, 0.75, d.z));
    float left = (1.0 - smoothstep (0.0, 0.07 + blur, abs (azimuth - 0.95))) * band;
    float right = (1.0 - smoothstep (0.0, 0.07 + blur, abs (azimuth + 0.95))) * band;
    colour += (left * vec3 (0.10, 0.85, 1.40) + right * vec3 (1.40, 0.18, 0.95)) * (1.2 - blur);
    return colour;
}

float shadowFactor (vec3 n, vec3 l)
{
    vec3 p = lightSpacePosition.xyz / lightSpacePosition.w * 0.5 + 0.5;
    if (p.z >= 1.0 || p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0)
        return 1.0;

    float bias = max (0.0008 * (1.0 - dot (n, l)), 0.00015);
    float lit = 0.0;
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y)
            lit += texture (shadowMap, vec3 (p.xy + vec2 (x, y) * shadowTexel, p.z - bias));
    return lit / 25.0;
}

vec3 directLight (vec3 n, vec3 v, vec3 l, vec3 radiance, vec3 diffuseColour, vec3 f0, float a, float NoV)
{
    float NoL = dot (n, l);
    if (NoL <= 0.0)
        return vec3 (0.0);

    vec3 h = normalize (v + l);
    float NoH = max (dot (n, h), 0.0);
    float VoH = max (dot (v, h), 0.0);
    vec3 F = fresnelSchlick (f0, VoH);
    vec3 specular = distributionGGX (NoH, a) * visibilitySmithGGX (NoV, NoL, a) * F;
    vec3 diffuse = (1.0 - F) * diffuseColour / PI;
    return (diffuse + specular) * radiance * NoL;
}

void main()
{
    vec3 n = normalize (worldNormal);
    if (! gl_FrontFacing)
        n = -n;

    vec3 albedo = baseColour;
    float rough = roughness;
    float metal = metallic;
    float coat = clearcoat;
    vec3 extraEmission = vec3 (0.0);

    if (pattern == 1) // cymbal: lathe grooves, hammer marks, brighter bell
    {
        float r = length (localPosition.xy);
        float grooves = sin (r * 2.0 * PI * 260.0);
        float hammer = valueNoise (localPosition * 70.0);
        rough = clamp (rough + 0.06 * grooves + 0.10 * (hammer - 0.5), 0.08, 1.0);
        albedo *= 0.90 + 0.12 * hammer + 0.04 * grooves;
        if (r < 0.22 * patternScale)
        {
            albedo *= 1.08;
            rough *= 0.75;
        }
    }
    else if (pattern == 2) // lacquer with metal flakes
    {
        // Semi-metallic flakes: they glint in the light but never turn darker than the paint
        // when they reflect a dark part of the room.
        float flake = hash (floor (localPosition * 420.0));
        if (flake > 0.97)
        {
            metal = 0.5;
            rough = 0.2;
            albedo = baseColour * 1.8 + vec3 (0.05, 0.06, 0.09);
        }
    }
    else if (pattern == 3) // coated head, darker collar at the edge
    {
        float r = length (localPosition.xy) / patternScale;
        albedo *= 0.95 + 0.07 * valueNoise (localPosition * 260.0);
        albedo *= mix (1.0, 0.55, smoothstep (0.90, 0.95, r));

        // Ring of light running from the centre to the rim after a hit.
        float front = hitAge * 3.2;
        float t = (r - front) / 0.07;
        extraEmission = rippleColour * (2.2 * hitStrength * exp (-t * t) * exp (-hitAge * 4.0));
    }
    else if (pattern == 4) // wood grain along the stick
    {
        float grain = valueNoise (vec3 (localPosition.xy * 90.0, localPosition.z * 2.5));
        albedo *= 0.78 + 0.40 * grain;
        rough = clamp (rough + 0.15 * (grain - 0.5), 0.2, 1.0);
    }
    else if (pattern == 5) // brushed metal along local Z
    {
        float brush = valueNoise (vec3 (localPosition.xy * 900.0, localPosition.z * 3.0));
        rough = clamp (rough + 0.18 * (brush - 0.5), 0.05, 1.0);
    }
    else if (pattern == 6) // rug: weave, neon border ring
    {
        float r = length (localPosition.xy) / patternScale;
        float weave = 0.5 + 0.5 * sin (localPosition.x * 700.0) * sin (localPosition.y * 700.0);
        albedo *= 0.80 + 0.30 * weave + 0.25 * valueNoise (localPosition * 12.0);
        float ring = (1.0 - smoothstep (0.0, 0.012, abs (r - 0.93)));
        albedo = mix (albedo, vec3 (0.02, 0.30, 0.42), ring);
    }

    vec3 v = normalize (cameraPosition - worldPosition);
    float NoV = max (dot (n, v), 1.0e-4);
    vec3 f0 = mix (vec3 (0.04), albedo, metal);
    float a = max (rough * rough, 0.002);
    vec3 diffuseColour = albedo * (1.0 - metal);

    // Key light: spot above the kit, with soft shadows.
    vec3 toSpot = spotPosition - worldPosition;
    float distance = length (toSpot);
    vec3 l = toSpot / distance;
    float cone = smoothstep (spotCosOuter, spotCosInner, dot (-l, spotDirection));
    vec3 spotRadiance = spotColour * cone / (distance * distance) * shadowFactor (n, l);
    vec3 colour = directLight (n, v, l, spotRadiance, diffuseColour, f0, a, NoV);

    // Soft fill from the player's side, so the shells do not fall into black.
    colour += directLight (n, v, normalize (vec3 (-0.8, 0.25, 0.55)), vec3 (0.22, 0.24, 0.30), diffuseColour, f0, a, NoV);

    // Neon rim lights from behind the kit.
    colour += directLight (n, v, rimDirectionA, rimColourA, diffuseColour, f0, a, NoV);
    colour += directLight (n, v, rimDirectionB, rimColourB, diffuseColour, f0, a, NoV);

    // Studio environment: blurred irradiance for diffuse, reflections for specular.
    vec3 reflected = reflect (-v, n);
    colour += diffuseColour * environment (n, 1.0) * 0.8;
    colour += environment (reflected, rough) * environmentBRDF (f0, rough, NoV);

    // Clear coat: a thin glossy dielectric layer on top (lacquer, varnish).
    if (coat > 0.0)
    {
        float fc = 0.04 + 0.96 * pow (clamp (1.0 - NoV, 0.0, 1.0), 5.0);
        vec3 h = normalize (v + l);
        float coatSpecular = distributionGGX (max (dot (n, h), 0.0), 0.03) * visibilitySmithGGX (NoV, max (dot (n, l), 0.0), 0.03) * 0.04;
        colour = colour * (1.0 - coat * fc)
               + coat * (fc * environment (reflected, 0.04) + coatSpecular * spotRadiance * max (dot (n, l), 0.0));
    }

    fragColour = vec4 (colour + emissive + extraEmission, 1.0);
}
)";

// --- Shadow map: depth only -------------------------------------------------------------

inline const char* const kShadowVertex = R"(#version 410 core
layout (location = 0) in vec3 position;
uniform mat4 model;
uniform mat4 lightViewProjection;
void main()
{
    gl_Position = lightViewProjection * model * vec4 (position, 1.0);
}
)";

inline const char* const kShadowFragment = R"(#version 410 core
void main() {}
)";

// --- Bloom: a pyramid of downsampled, blurred copies of the bright parts -----------------
// (13-tap downsample and 3x3 tent upsample, as in Jimenez, "Next generation post processing
// in Call of Duty: Advanced Warfare", 2014.)

inline const char* const kBloomDownsampleFragment = R"(#version 410 core
uniform sampler2D source;
uniform vec2 sourceTexel;  // 1 / size of the source texture
uniform vec2 uvScale;      // part of the source covered by the view (first pass only)
uniform vec2 targetSize;
uniform int firstPass;
uniform float threshold;

out vec4 fragColour;

// uv covers the view (0..1); the offset is in source texels.
vec3 fetch (vec2 uv, vec2 offset)
{
    return texture (source, uv * uvScale + offset * sourceTexel).rgb;
}

// Keeps what is brighter than the threshold, with a soft knee to avoid hard edges.
vec3 brightPart (vec3 c)
{
    float brightness = max (c.r, max (c.g, c.b));
    float knee = 0.5 * threshold;
    float soft = clamp (brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1.0e-5);
    return c * max (soft, brightness - threshold) / max (brightness, 1.0e-5);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / targetSize;
    vec3 a = fetch (uv, vec2 (-2.0, 2.0)), b = fetch (uv, vec2 (0.0, 2.0)), c = fetch (uv, vec2 (2.0, 2.0));
    vec3 d = fetch (uv, vec2 (-2.0, 0.0)), e = fetch (uv, vec2 (0.0, 0.0)), f = fetch (uv, vec2 (2.0, 0.0));
    vec3 g = fetch (uv, vec2 (-2.0, -2.0)), h = fetch (uv, vec2 (0.0, -2.0)), i = fetch (uv, vec2 (2.0, -2.0));
    vec3 j = fetch (uv, vec2 (-1.0, 1.0)), k = fetch (uv, vec2 (1.0, 1.0));
    vec3 l = fetch (uv, vec2 (-1.0, -1.0)), m = fetch (uv, vec2 (1.0, -1.0));

    vec3 colour = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (firstPass == 1)
        colour = brightPart (colour);
    fragColour = vec4 (max (colour, vec3 (0.0)), 1.0);
}
)";

inline const char* const kBloomUpsampleFragment = R"(#version 410 core
uniform sampler2D source;
uniform vec2 sourceTexel;
uniform vec2 targetSize;
uniform float radius;

out vec4 fragColour;

void main()
{
    vec2 uv = gl_FragCoord.xy / targetSize;
    vec2 t = sourceTexel * radius;
    vec3 colour = texture (source, uv + t * vec2 (-1.0, 1.0)).rgb
                + texture (source, uv + t * vec2 (0.0, 1.0)).rgb * 2.0
                + texture (source, uv + t * vec2 (1.0, 1.0)).rgb
                + texture (source, uv + t * vec2 (-1.0, 0.0)).rgb * 2.0
                + texture (source, uv).rgb * 4.0
                + texture (source, uv + t * vec2 (1.0, 0.0)).rgb * 2.0
                + texture (source, uv + t * vec2 (-1.0, -1.0)).rgb
                + texture (source, uv + t * vec2 (0.0, -1.0)).rgb * 2.0
                + texture (source, uv + t * vec2 (1.0, -1.0)).rgb;
    fragColour = vec4 (colour / 16.0, 1.0);
}
)";

// --- Final pass: exposure, filmic tone mapping, vignette, dithering ----------------------

inline const char* const kFullscreenVertex = R"(#version 410 core
void main()
{
    // One triangle covering the screen, no vertex buffer needed.
    vec2 corners[3] = vec2[3] (vec2 (-1.0, -1.0), vec2 (3.0, -1.0), vec2 (-1.0, 3.0));
    gl_Position = vec4 (corners[gl_VertexID], 0.0, 1.0);
}
)";

inline const char* const kTonemapFragment = R"(#version 410 core
uniform sampler2D hdrImage;
uniform sampler2D bloomImage; // half resolution, covers the view exactly
uniform vec2 resolution;      // size of the view in pixels
uniform vec2 imageScale;      // fraction of the HDR texture covered by the view
uniform float exposure;
uniform float bloomStrength;

out vec4 fragColour;

vec3 acesFilm (vec3 x)
{
    return clamp ((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / resolution;
    vec3 hdr = texture (hdrImage, uv * imageScale).rgb + bloomStrength * texture (bloomImage, uv).rgb;
    vec3 colour = acesFilm (hdr * exposure);

    float vignette = (1.0 - smoothstep (0.30, 1.05, length ((uv - 0.5) * vec2 (1.0, 0.8)) * 1.3));
    colour *= mix (0.72, 1.0, vignette);

    colour = pow (colour, vec3 (1.0 / 2.2));
    float noise = fract (sin (dot (gl_FragCoord.xy, vec2 (12.9898, 78.233))) * 43758.5453);
    colour += (noise - 0.5) / 255.0;
    fragColour = vec4 (colour, 1.0);
}
)";

} // namespace vdrum::shaders
