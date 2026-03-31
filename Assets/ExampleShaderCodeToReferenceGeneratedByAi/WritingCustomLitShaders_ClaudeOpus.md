# Writing Custom Lit Shaders in O3DE Atom

How to write hand-written AZSL shaders that receive real scene lighting without duplicating the engine's PBR pipeline.

## The File Set

Every custom material needs four files:

- **`.azsl`** -- The shader source (AZSL, which is HLSL with O3DE extensions for SRGs).
- **`.shader`** -- Declares the entry points, depth/stencil state, and which draw list the shader belongs to.
- **`.materialtype`** -- Defines exposed properties (the UI you see in the Material Editor) and maps them to shader inputs. Also lists which `.shader` files participate.
- **`.material`** -- An instance of a `.materialtype` with specific property values.

The `.shader` file for a lit forward pass material always has `"DrawList": "forward"`. This is what tells Atom to render your material during the forward lighting pass. Without it, your shader won't run at all.

## Required Includes

```hlsl
#include <scenesrg_all.srgi>     // SceneSrg: directional lights, IBL cubemaps, object transforms
#include <viewsrg_all.srgi>      // ViewSrg: point/spot lights, camera matrices, shadow data
#include <Atom/Features/PBR/DefaultObjectSrg.azsli>                  // Per-object data (ObjectSrg)
#include <Atom/RPI/ShaderResourceGroups/UnusedFallbackDrawSrg.azsli> // Satisfies the DrawSrg slot
#include <Atom/Features/InstancedTransforms.azsli>                   // GetObjectToWorldMatrix(), etc.
#include <Atom/Features/Pipeline/Forward/ForwardPassOutput.azsli>    // ForwardPassOutput struct
```

### What These Give You

The `scenesrg_all.srgi` and `viewsrg_all.srgi` files are the big ones. They pull in **every** partial SRG definition from the engine, including all light structure types (`DirectionalLight`, `SimplePointLight`, `SimpleSpotLight`, etc.) and the IBL cubemap textures. These `.srgi` files live in your project's `ShaderLib/` directory and include the engine's partials via `SceneSrgAll.azsli` and `ViewSrgAll.azsli`.

`DefaultObjectSrg.azsli` and `UnusedFallbackDrawSrg.azsli` exist because Atom's SRG binding system expects four frequency slots to be declared: `SRG_PerScene`, `SRG_PerView`, `SRG_PerDraw`, and `SRG_PerMaterial`. Scene and View are covered by the `.srgi` files. The draw SRG is handled by the fallback include. Your material SRG is the fourth.

`InstancedTransforms.azsli` provides `GetObjectToWorldMatrix(instanceId)` and `GetObjectToWorldMatrixInverseTranspose(instanceId)` which are needed to transform vertices and normals from object space to world space.

## Shader Resource Groups (SRGs)

SRGs are how Atom organizes GPU-accessible data by update frequency:

| SRG | Updated | Contains |
|-----|---------|----------|
| `SRG_PerScene` (SceneSrg) | Once per frame | Directional lights, IBL cubemaps, IBL orientation/exposure, object transform buffers |
| `SRG_PerView` (ViewSrg) | Once per camera | View/projection matrices, point lights, spot lights, disk/capsule/quad/polygon lights, shadow data |
| `SRG_PerDraw` (ObjectSrg) | Once per draw call | Object-to-world transforms (via instance ID lookup) |
| `SRG_PerMaterial` | Per material | Your custom properties (base color, roughness, whatever you define) |

You define your own `SRG_PerMaterial`. The other three are provided by the engine includes.

## Where Light Data Lives

This is the key thing that is not obvious: **light data is split across two SRGs, not one.**

### SceneSrg (scene-wide, all views share it)

- `SceneSrg::m_directionalLights[]` -- Array of `DirectionalLight` structs
- `SceneSrg::m_directionalLightCount` -- How many are active
- `SceneSrg::m_diffuseEnvMap` -- IBL diffuse irradiance cubemap (from the HDRi Skybox or Global IBL component)
- `SceneSrg::m_specularEnvMap` -- IBL specular reflection cubemap (mipped by roughness)
- `SceneSrg::m_iblOrientation` -- Quaternion rotating the IBL cubemap
- `SceneSrg::m_iblExposure` -- Exposure value (used as `pow(2.0, exposure)`)
- `SceneSrg::m_samplerEnv` -- Linear sampler for the cubemaps

### ViewSrg (per-camera)

- `ViewSrg::m_simplePointLights[]` / `m_simplePointLightCount`
- `ViewSrg::m_simpleSpotLights[]` / `m_simpleSpotLightCount`
- `ViewSrg::m_diskLights[]` / `m_diskLightCount`
- `ViewSrg::m_capsuleLights[]` / `m_capsuleLightCount`
- `ViewSrg::m_quadLights[]` / `m_quadLightCount`
- `ViewSrg::m_polygonLights[]` / `m_polygonLightCount`
- `ViewSrg::m_viewProjectionMatrix`, `m_worldPosition`, etc.

The reason for this split: directional lights and IBL are global to the scene (the sun doesn't change per camera), while point/spot/area lights can be culled per view.

## Light Structures

Defined inside the CoreLights SRG partials (automatically available via the `.srgi` includes). The key fields you'll use:

### DirectionalLight

```hlsl
float3 m_direction;        // Direction the light points (negate it to get direction-to-light)
float3 m_rgbIntensityLux;  // Color * intensity in lux
```

### SimplePointLight

```hlsl
float3 m_position;
float  m_invAttenuationRadiusSquared;  // 1 / (radius^2), for windowed falloff
float3 m_rgbIntensityCandelas;         // Color * intensity in candelas
```

### SimpleSpotLight

```hlsl
float3 m_position;
float  m_invAttenuationRadiusSquared;
float3 m_direction;                    // Direction the spotlight points
float  m_cosInnerConeAngle;            // Full intensity inside this cone
float3 m_rgbIntensityCandelas;
float  m_cosOuterConeAngle;            // Zero intensity outside this cone
```

## The Lighting Math

### Lambert Diffuse (the core of all of this)

```hlsl
float NdotL = saturate(dot(normal, dirToLight));
diffuse += albedo * NdotL * lightColor;
```

That is the fundamental operation. `N dot L` gives you how much a surface faces the light. `saturate` clamps it to `[0, 1]` so back-facing surfaces get zero light. Everything else is just getting `dirToLight` and `lightColor` correct for each light type.

### Point Light Attenuation

Point lights need distance falloff. The engine uses a physically-based inverse-square law with a smooth windowed cutoff so lights don't abruptly pop off at their radius boundary:

```hlsl
float3 toLight = lightPos - worldPos;
float distSq = dot(toLight, toLight);
float3 dirToLight = toLight * rsqrt(distSq);   // normalize via reciprocal sqrt

// Windowed falloff: smooth fade to zero at the attenuation radius
float falloff = distSq * invAttenuationRadiusSq;
float radiusAttenuation = saturate(1.0 - falloff * falloff);
radiusAttenuation *= radiusAttenuation;  // square it for a smoother curve

float3 lightIntensity = rgbIntensityCandelas / distSq * radiusAttenuation;
```

The `invAttenuationRadiusSquared` value is precomputed by the engine as `1.0 / (radius * radius)`. The double-squaring of the radius attenuation creates a smooth falloff curve that reaches exactly zero at the light's radius.

### Spot Light Cone

On top of the point light math, spot lights add an angular falloff:

```hlsl
float cosAngle = dot(-dirToLight, spotDirection);
float coneAttenuation = saturate((cosAngle - cosOuter) / (cosInner - cosOuter));
```

This linearly interpolates between full intensity (inside the inner cone) and zero (outside the outer cone).

### IBL Diffuse (Ambient / Skylight)

IBL (Image-Based Lighting) provides ambient light from the environment cubemap. It is **not** a light loop -- it is a single cubemap sample using the surface normal as the lookup direction:

```hlsl
float3 irradianceDir = RotateByQuaternion(normal, SceneSrg::m_iblOrientation);
float3 iblSample = SceneSrg::m_diffuseEnvMap.Sample(
    SceneSrg::m_samplerEnv,
    WorldToCubemapCoords(irradianceDir)
).rgb;
float iblExposure = pow(2.0, SceneSrg::m_iblExposure);
diffuse += albedo * iblSample * iblExposure;
```

Two quirks here:
1. The IBL orientation quaternion must be applied to rotate the normal into the cubemap's space.
2. O3DE's cubemap coordinate system is different from world space: `float3(-dir.x, dir.z, -dir.y)`.

The `m_diffuseEnvMap` cubemap is pre-convolved (blurred) so that a single sample gives you the correct irradiance for any normal direction. This is why you don't need to do any integration or multi-sampling -- the engine pre-processes this when it builds the IBL from your HDRi.

## ForwardPassOutput

The forward pass renders into multiple render targets (MRT). Even if you only care about diffuse color, you need to fill them all or you'll get artifacts from uninitialized data:

```hlsl
psOutput.m_diffuseColor  = float4(diffuse, 1.0);     // Your lit color goes here
psOutput.m_specularColor = float4(0.0, 0.0, 0.0, 1.0);  // Zero if no specular
psOutput.m_albedo        = float4(albedo, 1.0);       // Used by deferred GI passes
psOutput.m_specularF0    = float4(0.0, 0.0, 0.0, 1.0);  // Fresnel reflectance at normal incidence
psOutput.m_normal        = float4(0.0, 0.0, 0.0, 0.0);  // Encoded world normal (used by SSR, SSAO, etc.)
```

`m_diffuseColor` is the one that matters for your final lit result. The others feed into screen-space post-processing effects. If you want SSAO or SSR to work correctly with your material, you'd need to encode the normal properly (the engine uses signed octahedron encoding) and fill in the other targets accurately. For now, zeroes are fine -- things will render, you just won't get those effects.

## The Vertex Shader

The vertex shader pattern is the same across nearly every O3DE material:

```hlsl
VSOutput MainVS(VSInput vsInput, uint instanceId : SV_InstanceID)
{
    float4x4 objectToWorld = GetObjectToWorldMatrix(instanceId);
    float3 worldPosition = mul(objectToWorld, float4(vsInput.m_position, 1.0)).xyz;

    vsOutput.m_position = mul(ViewSrg::m_viewProjectionMatrix, float4(worldPosition, 1.0));
    vsOutput.m_worldPosition = worldPosition;

    float3x3 objectToWorldIT = GetObjectToWorldMatrixInverseTranspose(instanceId);
    vsOutput.m_worldNormal = normalize(mul(objectToWorldIT, vsInput.m_normal));
}
```

Key details:
- `instanceId` is how O3DE handles instanced rendering. Even non-instanced objects go through this path.
- `GetObjectToWorldMatrixInverseTranspose` is critical for normals. If an object has non-uniform scale, using the regular object-to-world matrix would skew the normals. The inverse-transpose corrects for this.
- You pass world-space position and normal to the pixel shader so lighting math happens in world space.

## What the Engine's PBR Does Beyond This

Our shaders implement the basics. Here's what the engine's full PBR pipeline adds, in case you want to incorporate any of these later:

- **Specular reflections** -- Microfacet BRDF (GGX distribution, Smith geometry, Fresnel-Schlick). Uses `m_specularEnvMap` for IBL specular and per-light specular highlights.
- **Energy conservation** -- Fresnel-based split between diffuse and specular response (`diffuseResponse = 1 - specularResponse`).
- **Normal mapping** -- TBN matrix construction from vertex tangents, sampling a normal map texture.
- **Shadow mapping** -- Directional light cascaded shadow maps, projected shadows for spot/point lights. The shadow data is in ViewSrg.
- **GPU light culling** -- A tile-based system (`LightCullingTileIterator`) that pre-sorts lights per screen tile via `PassSrg::m_lightListRemapped`. Our shaders use the simpler CPU-culled arrays instead.
- **Area lights** -- Disk, capsule, quad, polygon lights with LTC (Linearly Transformed Cosines) for accurate area light shading.
- **Ambient occlusion** -- SSAO integration via `diffuseAmbientOcclusion`.
- **Reflection probes** -- Local cubemap overrides that blend with the global IBL.
- **Subsurface scattering** -- For skin, wax, foliage, etc.

## Reusing Engine Code Without Duplicating It

For things like shadow sampling or GPU light culling, you can `#include` the engine's utility headers directly rather than reimplementing them:

| What you need | Include |
|---------------|---------|
| Light structures | Already in your SRG includes |
| Lambert/GGX BRDF | `<Atom/Features/PBR/Microfacet/Brdf.azsli>` |
| Cubemap coord conversion | `<Atom/Features/PBR/LightingUtils.azsli>` |
| Quaternion math | `<Atom/RPI/Math.azsli>` |
| GPU light culling iterator | `<Atom/Features/LightCulling/LightCullingTileIterator.azsli>` |
| Directional light shadow sampling | `<Atom/Features/Shadow/DirectionalLightShadow.azsli>` |
| Projected shadow sampling | `<Atom/Features/Shadow/ProjectedShadow.azsli>` |

The engine's lighting functions (`GetDiffuseLighting`, `GetSpecularLighting` in `StandardLighting.azsli`) are tightly coupled to the engine's `Surface` and `LightingData` classes. If you use those, you're basically opting into the full PBR pipeline. The approach taken in our shaders -- reading the raw light data from the SRGs and doing our own math -- gives you total control over the shading model while still benefiting from the engine's light management.

## Practical Notes

- **Depth and shadow passes**: The `.materialtype` references `Shaders/Depth/DepthPass.shader` and `Shaders/Shadow/Shadowmap.shader`. These are engine-provided shaders that handle depth pre-pass and shadow map generation. You don't need to write these yourself -- they only need vertex position, which your mesh already has.
- **Units matter**: Directional lights use lux, point/spot lights use candelas. These are physically-based units. A directional light at intensity 4 lux is dim; you might see values like 2-10 in typical scenes. Point lights in candelas can be in the hundreds or thousands.
- **The `0.001 * 0.001` minimum distance**: Prevents division by zero when a fragment is exactly at the light's position.
- **`rsqrt(distSq)`**: Reciprocal square root -- faster than `normalize(toLight)` since we already have `distSq` computed and need both the direction and the distance.

## Emissive Surfaces and Global Illumination

### Do Emissive Materials Light Nearby Surfaces?

Not directly. An emissive material just writes bright pixels to its own render target -- it doesn't create a light source. In the forward pass, there is no mechanism for one surface to know about another surface's emissive output.

However, O3DE has a real-time global illumination system called **DiffuseProbeGrid** (based on NVIDIA RTX-GI) that *does* propagate light from emissive surfaces to nearby geometry. This project already has the DiffuseProbeGrid gem enabled.

### How DiffuseProbeGrid Works

The system places a 3D grid of probes in the level. Each probe ray-traces the scene from its position, sampling whatever it hits -- including emissive surfaces. The results are filtered into irradiance textures that represent how much indirect light reaches each probe from every direction.

The important thing: **this happens entirely outside your forward pass shader.** The pipeline is:

1. **Forward Pass** (your shader) -- Outputs direct lighting to `m_diffuseColor`, surface albedo to `m_albedo`, encoded normal to `m_normal`
2. **DiffuseProbeGrid Ray Trace Pass** -- Casts rays from probes, captures emissive + bounced light, builds irradiance textures
3. **DiffuseComposite Pass** -- A fullscreen pass that reads your `m_albedo` output and multiplies it by the probe irradiance:
   ```hlsl
   diffuse += (albedo / PI) * probeIrradiance * blendWeight;
   ```
4. **DiffuseSpecularMerge** -- Combines the final diffuse and specular buffers into the output image

### What This Means for Custom Shaders

Your custom shader already supports receiving GI from emissive surfaces. The DiffuseComposite pass reads the `m_albedo` render target that you're already writing to. As long as you output a meaningful albedo, the GI system will multiply it by whatever indirect light the probes captured -- including bounced light from emissive materials.

No shader changes needed. You just need:
- A **DiffuseProbeGrid component** placed in the level (covers a volume of space)
- A **GPU that supports ray tracing** (DX12 with DXR support)
- Emissive surfaces within or near the probe grid volume

The probe grid has a configurable `EmissiveMultiplier` that scales how much emissive surfaces contribute to the GI.

### The IBL Double-Counting Caveat

There is one subtlety. Our shaders apply IBL (skylight) directly in the forward pass. The DiffuseComposite pass *also* applies global IBL as a fallback where probe coverage is incomplete:

```hlsl
// In DiffuseComposite:
float3 globalDiffuse = (albedo * globalIrradiance) * pow(2.0, SceneSrg::m_iblExposure);
diffuse += globalDiffuse * (1.0 - probeIrradianceBlendWeight);
```

Where probes fully cover an area (`blendWeight = 1.0`), the composite pass uses only probe data and the IBL fallback term drops to zero. Where there are no probes (`blendWeight = 0.0`), it falls back entirely to the global IBL cubemap.

Since our forward pass *already* applies IBL, you could get double-brightness in areas without probe coverage. The engine's PBR shaders handle this by letting the DiffuseGlobalFullscreen pass manage all ambient/IBL, but they also apply IBL in the forward pass. The system seems designed to tolerate this overlap. If you notice over-brightening, you could:
- Remove the IBL section from your forward pass and let the composite pass handle all ambient
- Or keep it and accept that the two systems overlap slightly (this is what the engine's own shaders do)

## Approaches to Custom Lighting in Forward+ Rendering

There are three broad strategies for implementing non-standard shading models. Each trades off artistic control against maintenance burden and future-proofing. Understanding these tradeoffs is important because O3DE's forward+ pipeline is one of the main reasons to use this engine for NPR work.

### Approach A: Custom Light Loops (What Our Shaders Do)

You read light data directly from the SRGs and write your own shading math per light type.

**What you can do:**
- Complete per-light control. Each light type can use a different shading model. A directional light could use smooth gradients while point lights use hard cel-shading bands.
- Artistic attenuation curves that aren't physically based. You could make a point light fall off linearly, or use a custom easing function.
- Per-light color remapping, hue shifting, or artistic adjustments that depend on the angle, distance, or light color.
- Light-type-specific effects: e.g., only directional lights cast hatching lines, only point lights produce rim highlights.
- You decide exactly what math runs. Nothing is hidden.

**What you give up:**
- You must explicitly handle each light type. O3DE currently has 8: DirectionalLight, SimplePointLight, SimpleSpotLight, PointLight (Sphere), DiskLight (SpotDisk), CapsuleLight, QuadLight, PolygonLight. Our shaders currently handle 5 of these and miss capsule, quad, and polygon lights.
- If the engine adds a new light type (or a new rendering feature like shadow maps), your shader won't support it until you manually implement it.
- You're reimplementing attenuation math, cone falloff, etc. that the engine already has. The math is identical -- it's pure code duplication.
- No shadow support unless you also manually include and call the engine's shadow sampling functions.

**Best for:** Shaders where the artistic look requires per-light decisions. Cel-shading with per-light-type behavior, hatching/crosshatching, painterly styles where different light sources should produce visually distinct effects.

### Approach B: Engine Pipeline with Post-Processing (Stylize the Result)

You populate the engine's `Surface` struct, call its lighting evaluation, and get back a `LightingData` result with fully computed diffuse/specular. Then you stylize that aggregate result.

**What you can do:**
- Automatically supports ALL light types (all 8 current types, plus any future ones).
- Automatically gets shadows, transmission, light culling, energy conservation -- everything the engine computes.
- Future-proof: when the engine adds a new light type or rendering feature, your shader picks it up for free.
- No duplicated attenuation/falloff math.
- Still gives you control over the final look: you can quantize the diffuse into bands, remap colors, apply artistic curves to the aggregate lighting.

**What you give up:**
- You cannot distinguish which light contributed what. The diffuse result is a single `float3` that is the sum of all lights multiplied by albedo. You can convert it to luminance and step/quantize that, but you cannot say "make this point light hard-edged and that directional light soft."
- You lose the per-light artistic decisions that make Approach A powerful. Every light goes through the same PBR math first, and you only get to modify the combined output.
- This is essentially: realistic lighting in, stylized lighting out. If the realistic base doesn't capture the artistic direction you want, you're stuck.

**Best for:** Styles where the aggregate lighting matters more than individual light behavior. Simple cel-shading (just quantize total brightness), tinted shadows, color grading that depends on light intensity, or when you want a material that looks "almost PBR but with a twist."

### Approach C: Deferred Rendering (What Unreal Engine Does)

The geometry pass outputs surface properties (albedo, normal, roughness, metallic) to a G-buffer. A separate fullscreen pass computes lighting for all pixels using those properties.

**What you can do:**
- Extremely efficient for many lights (lighting is computed in screen-space, cost is per-pixel not per-object-per-light).
- The lighting pass can be highly optimized since it runs once for all geometry.

**What you give up:**
- **All per-material lighting control.** Every material uses the same lighting equation because lighting is computed after the geometry pass. A cel-shaded material and a realistic material cannot coexist in the same scene (without workarounds like custom stencil masks or hybrid rendering).
- NPR is fundamentally difficult. You can't make one object respond to light differently from another because the lighting pass has no concept of "which material is this pixel."
- Transparency and alpha blending require a separate forward pass anyway, partially negating the deferred benefit.
- G-buffer bandwidth is high (4-5 render targets of surface data).

**Best for:** Photorealistic rendering with many lights. Not suitable for per-material NPR unless supplemented with forward passes.

### Comparison Table

| | A: Custom Light Loops | B: Post-Process Pipeline | C: Deferred |
|---|---|---|---|
| Per-light artistic control | Full | None | None |
| Per-material shading model | Yes | Partial (output only) | No |
| Supports all light types automatically | No (manual) | Yes | Yes |
| Future-proof for new light types | No | Yes | Yes |
| Shadow support | Manual | Automatic | Automatic |
| Code duplication with engine | High | None | N/A |
| Maintenance burden | High | Low | Low |
| NPR suitability | Excellent | Good | Poor |

### Why This Matters for O3DE Specifically

O3DE uses forward+ rendering by default. This is the pipeline where each material's pixel shader runs with access to all light data. This is precisely what makes Approach A possible and powerful -- your pixel shader gets to decide how every light affects every pixel. A deferred renderer like Unreal's cannot offer this because lighting happens in a separate pass with no knowledge of the original material.

Approach B is a valid middle ground, but it does reduce the forward+ advantage. If all you're doing is computing standard PBR and then quantizing the result, you could achieve nearly the same thing in a deferred engine by post-processing the lighting buffer. You're not fully leveraging the per-material-per-light access that forward+ provides.

The sweet spot depends on how much per-light control your art direction actually needs. If you just want "2-band cel shading that works with all lights," Approach B is pragmatic and future-proof. If you want "directional lights cast hatching, point lights cast hard-edged pools of light, and spot lights produce color-shifted rim highlights," only Approach A can do that.

### The Engine's Own Customization Hook (A Middle Ground)

The engine provides a designed customization point that sits between A and B. Every light type's utility class is wrapped in a `#ifndef` guard:

```hlsl
// In SimplePointLight.azsli:
#ifndef SimplePointLightUtil
#define SimplePointLightUtil SimplePointLightUtil_PBR
#endif
```

This means you can define your own `SimplePointLightUtil` class *before* including the engine's light files, and the engine will use yours instead of the PBR default. The engine even provides `LightUtilTemplate.azsli` as a starting point for this.

With this approach you can:
- Override the `Apply()` method for specific light types with your own shading math
- Delegate to the base PBR class for Init/GetFalloff/GetSurfaceToLightDirection (reuse the attenuation math)
- Only customize the final diffuse/specular calculation
- Leave light types you don't care about customizing on their PBR defaults

This gives per-light-type control (like A) while reusing the engine's infrastructure (like B), but it does require defining a custom util class for each light type you want to override. New light types added by the engine would fall back to PBR defaults until you write a custom util for them -- which is arguably the right behavior (new lights work, just with standard shading, until you decide to stylize them).

### Making Your Own Material Emissive

If you want your custom material to *be* emissive (so it lights other things via the probe grid), add an emissive term to your forward pass output. The ray tracing pass samples whatever color your surface outputs:

```hlsl
float3 emissive = MyMaterialSrg::m_emissiveColor * MyMaterialSrg::m_emissiveIntensity;
psOutput.m_diffuseColor = float4(diffuse + emissive, 1.0);
```

The probes will pick up the emissive contribution when their rays hit your surface, and that light will then appear on nearby geometry in subsequent frames.
