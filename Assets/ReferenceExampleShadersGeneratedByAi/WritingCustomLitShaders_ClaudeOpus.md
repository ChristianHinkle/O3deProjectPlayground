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

On top of the point light math, spot lights add an angular falloff. The engine uses a smoothstep penumbra between the inner and outer cone angles:

```hlsl
float cosAngle = dot(dirToLight, -spotDirection);
// Early out: if outside the outer cone, set falloff to 1.0 (zero intensity after attenuation)
if (cosAngle < cosOuterConeAngle) { falloff = 1.0; }

// In the Apply(), after computing lightIntensity from distance attenuation:
if (cosAngle < cosInnerConeAngle)  // in penumbra
{
    float penumbraMask = (cosAngle - cosOuterConeAngle) / (cosInnerConeAngle - cosOuterConeAngle);
    penumbraMask = penumbraMask * penumbraMask * (3.0 - 2.0 * penumbraMask);  // smoothstep
    lightIntensity *= penumbraMask;
}
```

The smoothstep (`x*x*(3-2x)`) creates a smoother transition than linear interpolation. The engine also applies gobo (cookie) textures to spot lights when available.

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
- **`surface.lightingChannels`**: If you use the engine's `Surface` struct and lighting pipeline, you **must** set `surface.lightingChannels = 0xFFFFFFFF` (or the appropriate channel mask). Every light type checks `IsSameLightChannel(light.m_lightingChannelMask, surface.lightingChannels)` before applying. If `lightingChannels` is uninitialized (zero), the bitwise AND is always zero, and **all direct lights are silently skipped**. The surface will appear to receive only IBL/skylight. This field has no default value in the `Surface` class.

## Considerations for Custom Shaders Using the Engine Pipeline

### FORCE_IBL_IN_FORWARD_PASS (Recommended for All Custom Shaders)

Shaders that use the engine's lighting pipeline with `FORCE_OPAQUE` (the Full Pipeline and Full Pipeline with Custom Lighting approaches) should define `FORCE_IBL_IN_FORWARD_PASS 1` before `FORCE_OPAQUE`:

```hlsl
#define FORCE_IBL_IN_FORWARD_PASS 1
#define FORCE_OPAQUE 1
```

**Why this matters:** With `FORCE_OPAQUE` alone, the forward pass does NOT apply diffuse IBL. Instead, the engine defers IBL to the `DiffuseGlobalFullscreen` post-pass, which runs after the forward pass. That post-pass reads the `m_albedo` render target, multiplies it by the environment cubemap, and adds the result to the diffuse buffer. This happens entirely outside your shader -- your cel-shading quantization, your custom diffuse function, none of it processes the IBL contribution. The ambient light arrives as smooth, physically-based lighting on top of your stylized result.

With `FORCE_IBL_IN_FORWARD_PASS`, the IBL is applied inside your forward pass via `ApplyIblForward()`. It accumulates into `lightingData.diffuseLighting` alongside all direct lights. Your post-process quantization or custom shading math then processes the IBL the same as any other light source. For cel-shading, this means the ambient light gets quantized into the same lit/unlit bands as everything else, rather than appearing as a separate smooth layer.

This applies regardless of stencil-based SSAO exclusion. Even without any SSAO work, custom shaders that want full control over how ALL light (including ambient/IBL) affects the final look should use this define.

The Manual Light Loop approach doesn't need this because it samples the IBL cubemap directly in its own shader code.

### Controlling Specular IBL in Custom Shaders

`ApplyIblForward()` computes both **diffuse IBL** (ambient light from the environment cubemap) and **specular IBL** (reflections based on Fresnel, roughness, and specularF0). For cel-shaded materials, the specular IBL produces smooth view-dependent gradients (brighter at grazing angles) that break the flat-band look.

Three layers contribute specular IBL. All three must be addressed to fully eliminate unwanted smooth reflections:

**1. Forward pass specular via `ApplyIblForward`:** With `FORCE_IBL_IN_FORWARD_PASS`, the function computes specular IBL and adds it to `lightingData.specularLighting[0]`. Zero it after the call:

```hlsl
ApplyIblForward(surface, lightingData);
lightingData.specularLighting[0] = real3(0.0, 0.0, 0.0);  // Remove specular IBL
```

This preserves the diffuse IBL (ambient light, reflection probe blending) while removing the specular. Alternatively, the specular can be quantized into bands instead of zeroed (see below).

**2. Reflection pipeline via `m_specularF0` output:** After the forward pass, the reflection pipeline (`ReflectionGlobalFullscreen` → `ReflectionComposite`) reads the `m_specularF0` render target to compute reflections. Write zero specularF0 to prevent this:

```hlsl
OUT.m_specularF0 = float4(0.0, 0.0, 0.0, 1.0);  // No reflections from pipeline
```

**3. IBL enabled flag via `m_normal.a`:** The `GetPbrLightingOutput` function encodes `o_enableIBL` into the normal's alpha channel. The reflection pipeline reads this flag to decide which pixels receive IBL specular. Write zero normal to signal "no IBL":

```hlsl
OUT.m_normal = float4(0.0, 0.0, 0.0, 0.0);  // No IBL flag = reflection pipeline skips this pixel
```

Note: zeroing the normal also means screen-space effects that read the normal buffer (SSAO edge detection, SSR) won't work for these pixels.

**Why not also zero `diffuseResponse`/`specularResponse`?** Setting `specularResponse = 0` prevents per-light specular from being computed by `GetSpecularLighting()` inside `ApplyDirectLighting`, but the IBL specular in `ApplyIblForward` uses `specularF0` and BRDF lookup values directly, not `specularResponse`. Both the response AND the post-IBL zeroing are needed for a fully clean result.

### Quantizing Specular Instead of Zeroing

Rather than eliminating specular, you can keep it and step it into bands. `lightingData.specularLighting[0]` is a single accumulator that ALL specular sources add to:

- Per-light specular highlights (from `ApplyDirectLighting` → `GetSpecularLighting` per light)
- IBL specular reflections (from `ApplyIblForward`)

By the time you read it, it's already the combined result. Stepping it once quantizes all specular sources together -- no separate handling per source needed:

```hlsl
ApplyDirectLighting(surface, lightingData, IN.m_position, tileIterator);
ApplyIblForward(surface, lightingData);
// specularLighting[0] now contains: all per-light highlights + IBL reflections

float specLuminance = GetLuminance(lightingData.specularLighting[0]);
float specBand = step(specularThreshold, specLuminance);  // specularThreshold is a material property (default 0.3)
float3 celSpecular = specBand * litColor;

OUT.m_specularColor = float4(celSpecular, 1.0);
```

**Energy conservation with cel-shading:** PBR energy conservation (`diffuseResponse = 1 - specularResponse`) is still important for cel-shading -- without it, surfaces can appear unnaturally bright at grazing angles where both diffuse and specular contribute full energy. Well-regarded cel-shaded games (Guilty Gear Xrd, etc.) preserve energy conservation.

The issue with keeping the Fresnel response isn't energy conservation itself -- it's that Fresnel creates **smooth gradients** across the surface. The solution: keep the PBR energy split, then quantize BOTH diffuse and specular into bands. The Fresnel transition becomes a hard edge instead of a smooth gradient:

```hlsl
// Keep PBR energy conservation
lightingData.specularResponse = FresnelSchlickWithRoughness(
    lightingData.GetSpecularNdotV(), surface.specularF0, surface.roughnessLinear);
lightingData.diffuseResponse = 1.0f - lightingData.specularResponse;

// Engine computes lighting with proper energy split
ApplyDirectLighting(surface, lightingData, IN.m_position, tileIterator);
ApplyIblForward(surface, lightingData);

// Quantize BOTH into bands -- energy-conserved cel-shading
float diffuseBand = step(diffuseThreshold, GetLuminance(lightingData.diffuseLighting));
float specBand = step(specularThreshold, GetLuminance(lightingData.specularLighting[0]));
```

At grazing angles, Fresnel shifts energy from diffuse to specular. The diffuse band might flip from lit to unlit. The specular band might flip from off to on. Both transitions are hard-edged. The total energy never exceeds what came in -- the quantization makes the energy-conserved transition crisp rather than smooth.

If specular is not desired at all (pure flat cel-shading), setting `diffuseResponse = 1.0` and `specularResponse = 0` is acceptable -- it trades energy conservation for guaranteed flat diffuse with no view-dependent variation. Zero `specularLighting[0]` after `ApplyIblForward` as shown in the zeroing section above. The `m_specularF0` and `m_normal` output zeroing (layers 2 and 3) are still needed either way to prevent the post-forward-pass reflection pipeline from adding its own specular on top.

### Reflection Probe Support (`o_materialUseForwardPassIBLSpecular`)

`FORCE_IBL_IN_FORWARD_PASS` alone is **not enough** for reflection probes to work. `ApplyIblForward` will run, call `GetIblSpecular`, and read `ObjectSrg::m_reflectionProbeData` -- but that data stays zeroed unless the C++ side populates it. `MeshFeatureProcessor::MaterialRequiresForwardPassIblSpecular` (`MeshFeatureProcessor.cpp`) scans each material's shader items looking for the `o_materialUseForwardPassIBLSpecular` shader option set to 1, and only then sets `m_hasForwardPassIblSpecularMaterial`, which gates the probe data upload into `ObjectSrg::m_reflectionProbeData` / `m_reflectionProbeCubeMap`.

`FORCE_IBL_IN_FORWARD_PASS` is the *shader-side* switch. `o_materialUseForwardPassIBLSpecular` is the *C++-side* switch that actually delivers the probe data. You need both.

The fix is a material property connected to the shader option, defaulting to true:

```json
{
    "id": "forwardPassIBLSpecular",
    "type": "Bool",
    "defaultValue": true,
    "connection": {
        "type": "ShaderOption",
        "name": "o_materialUseForwardPassIBLSpecular"
    }
}
```

Without this, custom shaders see the global IBL cubemap (which comes from SceneSrg, not ObjectSrg) but never any reflection probe contribution, even when a probe overlaps the mesh. Standard PBR materials include this via `GeneralCommonPropertyGroup.json`, which is why they "just work."

## Per-Material SSAO Exclusion

### The Problem

O3DE's SSAO computes ambient occlusion from the depth buffer and multiplies it into the diffuse buffer for every pixel. There's no built-in per-material opt-out. For custom-shaded materials (cel-shading, stylized looks, etc.), this introduces physically-based occlusion darkening that conflicts with the intended visual.

### How SSAO Flows Through the Pipeline

Understanding why this is hard requires understanding where SSAO sits in the pipeline:

```
Forward Pass (MSAA) → writes diffuse, specular, albedo, normal, depth-stencil
     ↓
DiffuseGlobalFullscreen (MSAA) → adds IBL to diffuse (stencil-tested, bit 0x80)
     ↓
Reflections, SkyBox, ReflectionComposite (MSAA)
     ↓
MSAAResolveDiffuse → resolves MSAA diffuse to non-MSAA          ← MSAA boundary
MSAAResolveSpecular → resolves MSAA specular to non-MSAA
     ↓
SubsurfaceScattering (non-MSAA)
     ↓
SsaoParent (non-MSAA):
  ├─ DepthDownsample → SsaoCompute → SsaoBlur → Upsample
  └─ ModulateWithSsao (ComputePass) → diffuse *= ssao           ← the problem
     ↓
DiffuseSpecularMerge → final = diffuse + specular
```

The `ModulateWithSsao` pass is a **ComputePass** that reads the SSAO texture and writes `diffuse *= ssao` in-place. Compute passes have no stencil testing capability. The pass darkens every pixel unconditionally.

### The Depth-Stencil Buffer

The depth and stencil data share a single GPU texture (`D32_FLOAT_S8X24_UINT` -- 32 bits depth + 8 bits stencil). The engine writes stencil during the forward pass using the `DepthStencilState` from each material's `.shader` file. Different materials can write different stencil values via `WriteMask` and `PassOp: Replace`.

Stencil bit 0x80 (`UseDiffuseGIPass`, defined in `RenderCommon.h`) is the engine's marker for pixels that should receive diffuse GI and other PBR post-processing. Passes like `DiffuseGlobalFullscreen` and `ReflectionComposite` use hardware stencil testing against this bit to decide which pixels to process.

The depth-stencil is MSAA throughout the pipeline -- it's never resolved. This matters because hardware stencil testing requires the depth-stencil and render targets to have matching MSAA sample counts.

### The MSAA Constraint

The SSAO modulation happens AFTER the MSAA resolve. At that point, the diffuse buffer is non-MSAA but the depth-stencil is still MSAA. This means you cannot bind the depth-stencil as a `DepthStencil` attachment alongside the non-MSAA diffuse render target -- the GPU rejects the MSAA mismatch.

This is why the engine's SSAO modulation uses a ComputePass (no render targets, no MSAA constraint) rather than a fullscreen triangle with stencil testing.

### Approach 1: Disable SSAO Globally

The simplest option. Disable SSAO in the level's PostFX settings or via C++ by disabling the `ModulateWithSsao` pass. No stencil work needed. Acceptable if the project is fully stylized with no PBR materials that need AO.

### Approach 2: Post-MSAA Stencil-as-Texture Save/Restore (Vulkan Only)

An alternative approach that reads the stencil buffer as a shader texture after MSAA resolve. Simpler than Approach 3 (2 passes instead of 3, smaller temp buffer) but only works on Vulkan due to an O3DE DX12 engine bug. Instead of modifying the SSAO pass itself, we **sandwich** the Ssao parent pass with save/restore passes that preserve excluded pixels:

1. **Save pass** (before Ssao): Copies diffuse for excluded pixels to a temp buffer
2. **Ssao runs normally**: Darkens everything including excluded pixels
3. **Restore pass** (after Ssao): Overwrites excluded pixels' diffuse with saved pre-SSAO values

The save/restore passes identify excluded pixels by reading the stencil buffer as a **shader texture** (not hardware stencil testing), which avoids the MSAA mismatch. The stencil plane is bound via `ImageViewDesc` with `"AspectFlags": ["Stencil"]` in the pass template, and read as `Texture2DMS<uint>` in the shader.

**Stencil marking:** Materials that want SSAO exclusion use `WriteMask: 0x7F` in both their forward pass and depth pass `.shader` files. This prevents writing stencil bit 0x80. The save/restore shader checks `stencilValue & 0x80`: if set, the pixel is PBR (discard, let SSAO apply); if not set, the pixel is excluded (copy it).

**The depth pass matters:** The depth prepass runs before the forward pass and also writes stencil. The engine's default `DepthPass.shader` writes stencil for all geometry. If it writes bit 0x80 before the forward pass runs, the forward pass `WriteMask: 0x7F` preserves that bit (it can't clear bits it doesn't write). The fix: a custom depth pass `.shader` (`StencilExcludeDepthPass_ClaudeOpus.shader`) that also uses `WriteMask: 0x7F`. It references the engine's `DepthPass.azsl` via an `#include` wrapper -- zero duplicated shader code, only the JSON stencil config differs.

**IBL compensation:** Stencil bit 0x80 is also used by `DiffuseGlobalFullscreen` to apply IBL. Excluded pixels (without bit 0x80) are skipped by that pass, losing ambient light. The fix: add `#define FORCE_IBL_IN_FORWARD_PASS 1` to the excluded material's `.azsl` file, which tells the forward pass to apply IBL directly instead of deferring to the fullscreen post-pass.

**DX12 limitation:** Reading the stencil plane as a shader texture works on **Vulkan** but not on O3DE's DX12 backend. The DX12 backend doesn't correctly create the Shader Resource View for the stencil plane of `D32_FLOAT_S8X24_UINT`. This is an engine-level issue, not a fundamental DX12 limitation (DX12 supports stencil SRVs via `DXGI_FORMAT_X32_TYPELESS_G8X24_UINT`). Use `--rhi=vulkan` when running the editor.

**Files:**

| File | Purpose |
|---|---|
| `Shaders/PostProcessing_ClaudeOpus/SsaoStencilCopy_ClaudeOpus.azsl` | Fullscreen copy shader that reads stencil as texture, discards included pixels |
| `Shaders/PostProcessing_ClaudeOpus/SsaoStencilCopy_ClaudeOpus.shader` | Shader config (no hardware stencil, no blend -- just copy) |
| `Passes/SsaoStencilSave_ClaudeOpus.pass` | Save pass template with transient temp image |
| `Passes/SsaoStencilRestore_ClaudeOpus.pass` | Restore pass template reading from temp, writing to diffuse |
| `Passes/PassTemplates_ClaudeOpus.azasset` | Registers all custom pass templates with the pass system |
| `Shaders/Depth_ClaudeOpus/StencilExcludeDepthPass_ClaudeOpus.shader` | Custom depth pass with WriteMask 0x7F |
| `Shaders/Depth_ClaudeOpus/StencilExcludeDepthPass_ClaudeOpus.azsl` | Wrapper that #includes the engine's DepthPass.azsl |
| `Gem/Source/SsaoStencilExclusionFeatureProcessor_ClaudeOpus.*` | C++ FeatureProcessor that injects save/restore passes |

### Approach 3: Pre-MSAA-Resolve Hardware Stencil Save/Restore (Cross-Platform, Active Implementation)

The active cross-platform approach. Uses the same stencil pattern as the engine's own passes (`DiffuseGlobalFullscreen`, `ReflectionComposite`). The save pass runs BEFORE the MSAA resolve, where both the diffuse buffer and depth-stencil are MSAA. Hardware stencil testing works because the sample counts match. Proven on both DX12 and Vulkan, no engine bugs to work around.

**How it works:**

1. **Save pass** (after `DiffuseGlobalFullscreenPass`, before MSAA resolve): A FullscreenTriangle pass with hardware stencil testing against the MSAA depth-stencil. Reads the MSAA diffuse buffer and writes excluded pixels to an MSAA temp buffer. Stencil test (`NotEqual` with ref 0x80, mask 0x80) ensures only excluded pixels are processed. Both render target and depth-stencil are MSAA -- no mismatch. The temp buffer must be explicitly cleared to `(0,0,0,0)` via `LoadAction: Clear` so that pixels rejected by the stencil test have zero alpha.
2. **Restore pass** (after `Ssao`): Reads the MSAA temp buffer directly using `Texture2DMS` and `Load(screenCoords, 0)` -- no MSAA resolve step. This avoids edge blending artifacts where the resolve would average saved pixels with the clear color at mesh silhouettes, producing gray outlines. The shader checks `saved.a > -1.0 + epsilon` to identify unsaved pixels (alpha = 0 from clear) and MSAA edge pixels, discarding them to preserve SSAO-darkened values. Saved interior pixels have alpha = -1 (from the forward pass's `m_diffuseColor.a`).

**Why this works on both backends:** No stencil texture reads. The save pass uses hardware stencil testing (a GPU fixed-function operation). The restore pass uses the MSAA temp buffer contents as the mask. Neither operation has backend-specific issues.

**Pipeline insertion points:** Both passes are injected by `PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus` via `AddPassAfter` during pipeline construction:
- Save → after `DiffuseGlobalFullscreenPass` (MSAA stage)
- Restore → after `Ssao` (post-processing stage)

**Tradeoffs compared to Approach 2:**
- 2 passes (same count as Approach 2)
- An MSAA temp buffer (same size as the MSAA diffuse)
- But: works on DX12 and Vulkan without any engine fixes

**MSAA silhouette edge artifacts:** At mesh silhouette edges, MSAA samples contain mixed stencil values across samples. Passes that check stencil (DiffuseGlobalFullscreen, ReflectionComposite) may partially process edge pixels, adding small amounts of IBL or reflection that appear as faint light-colored fringes. This is a fundamental limitation of per-material stencil masking with MSAA. It's subtle and primarily visible against the sky. The same save/restore pattern could be extended to cover these passes if needed.

**Files:**

| File | Purpose |
|---|---|
| `Shaders/.../PreMsaaStencilSave_ClaudeOpus/` | Save shader: fullscreen copy with hardware stencil config in .shader (NotEqual 0x80) |
| `Shaders/.../PreMsaaStencilRestore_ClaudeOpus/` | Restore shader: reads MSAA temp via Texture2DMS, discards non-saved pixels by alpha check |
| `Passes/.../PreMsaaStencilSave_ClaudeOpus.pass` | Save pass template: MSAA temp with `MultisampleSource`, `LoadAction: Clear`, `StencilRef: 128` |
| `Passes/.../PreMsaaStencilRestore_ClaudeOpus.pass` | Restore pass template: reads MSAA temp, writes diffuse with `LoadAction: Load` |
| `Gem/Source/.../PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus.*` | FeatureProcessor: injects 2 passes during pipeline construction |
| `Gem/Source/.../PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus.*` | System component: registers FP, loads templates |

**Shared with Approach 2** (both approaches use these):

| File | Purpose |
|---|---|
| `Shaders/.../Depth_ClaudeOpus/StencilExcludeDepthPass_ClaudeOpus.*` | Custom depth pass with WriteMask 0x7F |
| `Passes/.../PassTemplates_ClaudeOpus.azasset` | Registers all pass templates (both approaches) |

**Switching between approaches:** Both system components provide the same service (`SsaoStencilExclusionService_ClaudeOpus`) and are mutually exclusive. In `O3DEProjectPlaygroundCHModule.cpp`, toggle which component is listed in `GetRequiredSystemComponents()`.

### Workaround: Normal Buffer Detection (Hacky, Both Backends)

A workaround that avoids stencil entirely by checking the normal render target. Custom materials that write `m_normal = float4(0,0,0,0)` can be identified by `dot(normal.rgb, normal.rgb) < threshold`. This works on both DX12 and Vulkan with no stencil configuration needed.

This is a hack, not an architecture. It works by exploiting the assumption that excluded materials don't need the normal buffer -- encoding "I'm excluded" as the absence of data in an unrelated render target. If a material later needs to output encoded normals (for screen-space reflections, SSAO edge detection, or any future effect that reads the normal buffer), the detection breaks silently. It also prevents excluded materials from ever participating in normal-dependent post-processing. Use this only as a last resort when stencil-based approaches aren't available.

### Approach 4: Replace ModulateWithSsao with Stencil-Aware Version (Reference Only)

An alternative design that replaces the engine's compute-based SSAO modulation with a fullscreen triangle that uses hardware stencil testing and multiplicative blending. The shader outputs the SSAO value; the blend state computes `dest * src`. The stencil test (ReadMask 0x80, Func Equal, StencilRef 128) skips excluded pixels.

This approach requires wiring the depth-stencil into the SsaoParent pass hierarchy, which is difficult to do at runtime without duplicating engine pass template JSON files. The files `ModulateTextureWithStencil_ClaudeOpus.*` implement this as a reference but it's not used in the active pipeline.

### Comparison

| | Disable Globally | Post-MSAA Save/Restore (Approach 2) | **Pre-MSAA Hardware Stencil (Approach 3)** | Replace Modulation Pass (Approach 4) |
|---|---|---|---|---|
| **Status** | Available | Implemented (Vulkan only) | **Active implementation** | Reference only |
| Per-material control | No | Yes | Yes | Yes |
| DX12 support | Yes | Vulkan only (engine bug) | **Both backends** | Requires pass JSON overrides |
| Passes added | 0 | 2 | 2 | 0 (replaces 1) |
| Extra VRAM | None | ~32-128MB temp (non-MSAA) | ~64-256MB temp (MSAA) | None |
| Maintenance | None | Low (C++ + pass templates) | Moderate (C++ + pass templates) | High (JSON pass overrides) |
| Engine code duplication | None | None | None | ~670 lines of JSON |

## Modifying the Render Pipeline from C++ (FeatureProcessors)

### Why C++ Instead of JSON Pass Overrides

O3DE's pass system is data-driven: `.pass` JSON files define pass templates, and the pipeline is built from them. To modify the pipeline, the obvious approach is to override the JSON files. But this has serious problems:

- **Full file duplication.** Overriding `SsaoParent.pass` or `OpaqueParent.pass` requires copying the entire engine file (~140 or ~530 lines) and making small changes. If the engine updates the file, your override is stale.
- **Name collisions.** Project-level pass files in `Assets/Passes/` get registered under a different asset catalog path (`assets/passes/`) than engine files (`passes/`). They don't actually override -- they coexist. Putting files in `Gem/Assets/Passes/` does override (same catalog path, higher scan order), but risks replacing engine files entirely (e.g., `PassTemplates.azasset` would replace ALL engine pass template registrations).
- **Templates can't be modified after instantiation.** `RemovePassTemplate` fails if any live passes reference the template. By the first tick, the pipeline is already built from templates.

The C++ approach avoids all of this: register custom pass templates via the asset system, then inject passes into the live pipeline during construction.

### The FeatureProcessor Pattern

O3DE's `FeatureProcessor` is the standard way for gems to participate in rendering. The key method for pipeline modification is `AddRenderPasses(RenderPipeline* pipeline)`, which is called during pipeline construction. At this point, passes can be added via `pipeline->AddPassBefore()` / `AddPassAfter()` and they become part of the normal build/initialization cycle.

```cpp
class SsaoStencilExclusionFeatureProcessor : public AZ::RPI::FeatureProcessor
{
    void AddRenderPasses(AZ::RPI::RenderPipeline* renderPipeline) override
    {
        // Build PassRequest programmatically
        AZ::RPI::PassRequest saveRequest;
        saveRequest.m_passName = AZ::Name("SsaoStencilSave_ClaudeOpus");
        saveRequest.m_templateName = AZ::Name("SsaoStencilSaveTemplate_ClaudeOpus");
        // ... add connections ...

        auto savePass = passSystem->CreatePassFromRequest(&saveRequest);
        renderPipeline->AddPassBefore(savePass, AZ::Name("Ssao"));
    }
};
```

### Registration and Activation

The FeatureProcessor requires two things in the system component:

1. **Reflected** in the system component's `Reflect()` so the factory can create instances.
2. **Registered** with `FeatureProcessorFactory::Get()->RegisterFeatureProcessor<T>()` in the system component's `Activate()`.

That's it. When any Scene is created, it calls `FeatureProcessorFactory::EnableAllForScene(this)` which automatically creates and activates every registered feature processor on that scene.

Pass templates are loaded via `PassSystemInterface::LoadPassTemplateMappings()` in an `OnReadyLoadTemplatesEvent` handler connected during the system component's `Activate()`. The `.azasset` file maps template names to `.pass` files.

```cpp
void MySystemComponent::Activate()
{
    // Register FP -- Scene will auto-enable it via EnableAllForScene()
    AZ::RPI::FeatureProcessorFactory::Get()->RegisterFeatureProcessor<MyFeatureProcessor>();

    // Load pass templates when the pass system is ready
    auto* passSystem = AZ::RPI::PassSystemInterface::Get();
    if (passSystem)
    {
        m_loadTemplatesHandler = AZ::RPI::PassSystemInterface::OnReadyLoadTemplatesEvent::Handler(
            [this]() { this->LoadPassTemplateMappings(); }
        );
        passSystem->ConnectEvent(m_loadTemplatesHandler);
    }
}

void MySystemComponent::Deactivate()
{
    AZ::RPI::FeatureProcessorFactory::Get()->UnregisterFeatureProcessor<MyFeatureProcessor>();
    m_loadTemplatesHandler.Disconnect();
}
```

### Timing

| Event | What's available | What to do |
|---|---|---|
| System component `Activate()` | RPI system ready (require `RPISystem` service) | Register FP with factory, connect template load handler |
| `OnReadyLoadTemplatesEvent` | Pass system ready for template loading | Load YOUR pass templates via `LoadPassTemplateMappings()` |
| Scene creation (automatic) | Factory calls `EnableAllForScene()` | FP is created and activated -- no action needed |
| `AddRenderPasses` (from FeatureProcessor) | Pipeline being constructed | Insert passes via `AddPassBefore`/`AddPassAfter` |

`InsertChild` on a live, already-built pipeline does NOT work -- the parent pass's `QueueForBuildAndInitialization` rebuilds from the template and removes dynamically inserted children. `AddRenderPasses` is called during construction, before the first frame, so inserted passes are part of the normal build cycle.

## The DX12 Stencil Limitation and Path Forward

Two separate problems prevent stencil-based post-processing from working on O3DE's DX12 backend. Understanding them separately is important because they have different solutions.

### Problem 1: No MSAA Stencil Resolve

The depth-stencil buffer is MSAA (multiple samples per pixel) and is never resolved to non-MSAA. The engine resolves diffuse and specular (`MSAAResolveDiffusePass`, `MSAAResolveSpecularPass`) but not depth-stencil. All post-processing after the MSAA resolve (SSAO, bloom, tone mapping, etc.) operates on non-MSAA buffers.

Hardware stencil testing requires the depth-stencil and render targets to have matching MSAA sample counts. Since post-processing render targets are non-MSAA but the depth-stencil is MSAA, you cannot use hardware stencil testing in any post-processing pass. This is why the engine's SSAO modulation uses a ComputePass (no render targets, no MSAA constraint) instead of a fullscreen triangle with stencil.

The engine's stencil-using passes (DiffuseGlobalFullscreen, ReflectionComposite) all run BEFORE the MSAA resolve, where the diffuse buffer is still MSAA and the sample counts match.

**Solution:** Add an `MSAAResolveStencilPass` alongside the existing resolve passes. This pass would read the MSAA stencil and write to a non-MSAA R8_UINT texture (taking sample 0 or the max across samples). Post-processing passes could then read the resolved stencil as a regular texture or use it for hardware stencil testing with matching non-MSAA render targets.

This could be implemented at the project level via a FeatureProcessor-injected pass, using the same `AddPassAfter` pattern. However, the resolve pass itself needs to read the MSAA stencil as a texture -- which leads to Problem 2.

### Problem 2: DX12 Stencil Shader Resource View (SRV) Creation

To read the stencil plane of a `D32_FLOAT_S8X24_UINT` texture as a shader resource, the GPU needs a Shader Resource View (SRV) with a specific format:
- **DX12**: `DXGI_FORMAT_X32_TYPELESS_G8X24_UINT` -- stencil appears in the `.g` channel of a `uint2`
- **Vulkan**: `VK_IMAGE_ASPECT_STENCIL_BIT` on the `VkImageView` -- stencil appears as a single `uint`

O3DE's pass system supports requesting a stencil view via `"AspectFlags": ["Stencil"]` in the pass template's `ImageViewDesc`. On Vulkan, this correctly creates a stencil-only image view. On DX12, the view is not created correctly -- stencil reads return garbage/zero.

This is an O3DE engine bug in the DX12 RHI layer, not a fundamental DX12 limitation. DX12 fully supports stencil SRVs. The fix would be in `Gems/Atom/RHI/DX12/Code/Source/` where `ImageView` or `ShaderResourceView` is created from a depth-stencil texture with a stencil aspect flag. The view needs to use `DXGI_FORMAT_X32_TYPELESS_G8X24_UINT` for the stencil plane.

**This single engine fix would unblock stencil texture reads on DX12 for the entire engine**, enabling both the stencil resolve pass (Problem 1's solution) and direct stencil reads in post-processing shaders.

### What This Means in Practice

| | Vulkan | DX12 (current) | DX12 (with engine fix) |
|---|---|---|---|
| Hardware stencil test (pre-MSAA resolve) | Works | Works | Works |
| Hardware stencil test (post-MSAA resolve) | MSAA mismatch | MSAA mismatch | MSAA mismatch |
| Read stencil as texture | Works | Broken (engine bug) | Works |
| Stencil resolve pass | Works | Blocked by SRV bug | Works |
| Per-material SSAO exclusion | Stencil save/restore works | Vulkan only | Full support |

The two problems are independent but compound: even if the stencil resolve pass existed (Problem 1), it couldn't read the MSAA stencil on DX12 (Problem 2). Fixing Problem 2 (the SRV creation bug) unblocks everything -- the resolve pass becomes possible, and direct stencil reads in post-processing become possible.

### Effects That Benefit From Post-MSAA-Resolve Stencil Access

Any post-processing effect that should behave differently per-material:

- **Per-material SSAO exclusion** -- Skip AO for stylized materials.
- **Selective bloom/DOF/color grading** -- Different post-processing for different material categories (e.g., UI elements in 3D space that shouldn't bloom).
- **Outline/silhouette rendering** -- Draw outlines around stencil-marked objects in a post-process pass.
- **Portal/window effects** -- Different post-processing inside vs outside a stencil-masked region.

All of these can be achieved TODAY using the save/restore pattern: save protected pixels before the effect, let the effect run on everything, restore protected pixels after. This works for any **binary** exclusion decision (apply the effect or don't).

Two cross-platform approaches exist for the save/restore pattern:
- **Pre-MSAA hardware stencil** (Approach 3 above): Move the save pass to before the MSAA resolve where hardware stencil testing works on both DX12 and Vulkan. Requires an extra resolve pass for the temp buffer. This is the same pattern the engine's own stencil-using passes follow.
- **Post-MSAA stencil-as-texture** (Approach 2 above, current implementation): Reads stencil as a shader texture after MSAA resolve. Simpler (2 passes instead of 3, smaller temp buffer), but only works on Vulkan due to O3DE's DX12 stencil SRV bug.

Fixing the engine's DX12 stencil SRV creation (Problem 2 above) would make the post-MSAA approach work on both backends, making it the preferred solution.

Beyond save/restore, a resolved non-MSAA stencil buffer (Problem 1) would enable further improvements:
- Hardware stencil testing directly in post-processing passes (faster than shader-based discard)
- **Non-binary** stencil use (e.g., "apply 50% bloom to material A, 100% to material B" by reading the stencil value as a parameter inside the effect shader, not just as a save/restore gate)
- Eliminating the save/restore overhead entirely (two extra passes per excluded effect)

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

### Custom Light Loops (directory: `ManualLightLoop_SimpleDiffuse_ClaudeOpus`, `ManualLightLoop_SimpleCelShaded_ClaudeOpus`)

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

**Best for:** Learning how O3DE's lighting data is structured. In practice, Full Pipeline with Custom Lighting provides the same per-light access inside `Apply()` with far less maintenance (see "Do You Need Manual Light Loops?" below).

### Full Pipeline (directory: `FullPipeline_MinimalPBR_SimpleDiffuse_ClaudeOpus`, `FullPipeline_MinimalPBR_SimpleCelShaded_ClaudeOpus`)

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

### Deferred Rendering (What Unreal Engine Does -- not applicable to O3DE)

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

| | Custom Light Loops | Full Pipeline | BRDF Override | LightUtil Override | Deferred (reference) |
|---|---|---|---|---|---|
| Per-light artistic control | Full | None | All lights (same BRDF) | Per-overridden type | None |
| Per-material shading model | Yes | Partial (output only) | Yes | Yes | No |
| Supports all light types | No (manual) | Yes | Yes | Unoverridden fall back | Yes |
| Future-proof | No | Yes | Yes | New types get PBR default | Yes |
| Shadow support | Manual | Automatic | Automatic | Automatic | Automatic |
| Duplicated engine code | High | None | ~35 lines (MinimalPBR only) | ~350 lines (attenuation math) | N/A |
| Maintenance burden | High | Low | Low | Moderate | Low |
| NPR suitability | Excellent | Good | Excellent | Excellent | Poor |
| Starting point | — | CelShaded / SimpleDiffuse | BrdfOverride / CustomBasePBR | LightUtilOverride | — |

### Do You Need Manual Light Loops?

Manual light loops (Custom Light Loops) bypass the engine's pipeline entirely -- no shadows, no GPU culling, no future light type support. The question is whether they unlock any visual effect that Full Pipeline with Custom Lighting can't achieve.

**They don't.** The LightUtil Override's `Apply()` method receives the same light struct with the same fields (`m_position`, `m_direction`, `m_rgbIntensityCandelas`, etc.). You have full per-light control inside `Apply()`. The only thing manual loops give you beyond that is control over the *iteration itself* (e.g., skip lights, reorder them), which isn't needed for any common NPR effect.

Manual light loops are valuable as a learning tool -- they show exactly where light data lives and how attenuation works. But for production NPR shaders, Full Pipeline with Custom Lighting + post-process catch-all is strictly better: same artistic control, plus shadows, culling, and forward compatibility.

### Do You Need Per-Light Overrides, or Is Post-Process Enough?

Most NPR effects are functions of **total light intensity** on the surface, not per-light decisions. Post-process alone handles:

- **Cel-shading / toon bands** -- Quantize aggregate luminance
- **Hatching / crosshatching** -- Hatch density driven by total brightness
- **Dithering / stippling** -- Threshold pattern based on total brightness
- **Color ramp mapping** -- Map luminance to an artistic gradient
- **Posterization / flat shading** -- Reduce to N colors
- **Ink outlines** -- Depth/normal edge detection, not light-dependent
- **Rim lighting** -- View-angle effect (`1 - NdotV`), independent of light sources

Per-light overrides matter when **nonlinear functions interact with light summation.** The core difference: `step(threshold, A + B)` is not the same as `step(threshold, A) + step(threshold, B)`.

Example with two dim lights hitting a surface:

| | Light A (NdotL=0.3) | Light B (NdotL=0.4) | Result |
|---|---|---|---|
| **Post-process**: PBR sum, then step | 0.3 + 0.4 = 0.7 | step(0.5, 0.7) = 1.0 | **Lit** |
| **Per-light override**: step each, then sum | step(0.5, 0.3) + step(0.5, 0.4) = 0 + 0 | 0.0 | **Dark** |

With post-process, two individually-weak lights can combine to cross the threshold. With per-light override, each light independently decides "lit or not" before combining. This affects the **shape of shadow boundaries** when multiple lights overlap.

Effects where this per-light decision genuinely changes the visual result:

- **Per-light shadow boundary control** -- Each light stamps a hard cel-shaded edge that holds its shape regardless of other lights. With post-process only, adding a second light can "fill in" a shadow, softening the cel look.
- **Per-light color attribution** -- Warm-toned sunlight vs cool-toned point lights, decided per-light inside `Apply()`. Post-process only sees the combined color.
- **Per-light anisotropic stylization** -- Hatching strokes aligned to each light's direction independently. Post-process can only orient strokes based on the aggregate or the surface normal.

These are real differences, but they're subtle and increasingly niche. In typical scenes with one dominant directional light plus ambient, post-process and per-light override produce nearly identical results.

**Practical recommendation:** Start with **Full Pipeline** (aggregate output stylization) — it covers the vast majority of NPR effects with zero custom lighting code. If you need to change the diffuse or specular shading model itself (not just post-process the result), use a **BRDF Override** (`CustomMinimalPBR_BrdfOverride_ClaudeOpus` or `CustomBasePBR_ClaudeOpus`). Only use **LightUtil Overrides** (`CustomMinimalPBR_LightUtilOverride_ClaudeOpus`) if you need different shading math per light type. **Manual Light Loops** are useful for learning but offer no capability the other approaches don't provide.

### Why This Matters for O3DE Specifically

O3DE uses forward+ rendering by default. This is the pipeline where each material's pixel shader runs with access to all light data. This is precisely what makes Custom Light Loops and Full Pipeline with Custom Lighting possible -- your pixel shader gets to decide how every light affects every pixel. A deferred renderer like Unreal's cannot offer this because lighting happens in a separate pass with no knowledge of the original material.

Full Pipeline is a valid middle ground, but it does reduce the forward+ advantage. If all you're doing is computing standard PBR and then quantizing the result, you could achieve nearly the same thing in a deferred engine by post-processing the lighting buffer. You're not fully leveraging the per-material-per-light access that forward+ provides.

Full Pipeline with Custom Lighting is the strongest middle ground: you get per-light artistic control for the types you override, automatic PBR fallback for types you don't, and you stay within the engine's pipeline for shadows, culling, and future features. The tradeoff is implementation complexity (include-order dependencies, interface contracts).

The sweet spot depends on how much control your art direction needs. For most NPR work, start with **Full Pipeline** (simplest, most future-proof). If you need a custom shading model, use **BRDF Override** — it changes diffuse/specular for all lights with zero duplicated engine code. Add **LightUtil Overrides** only if you need per-light-type control. **Custom Light Loops** are useful for learning but offer no capability the other approaches don't provide.

### Full Pipeline with Custom Lighting

Two techniques for overriding the engine's lighting calculations, in order of preference:

#### BRDF Override (Recommended)

**Directories:** `CustomMinimalPBR_BrdfOverride_ClaudeOpus` (MinimalPBR), `CustomBasePBR_ClaudeOpus` (BasePBR)

The engine's `GetDiffuseLighting` and `GetSpecularLighting` functions use `#ifndef` guards — you can replace them with custom implementations via `#define` macros. Every light type's `Apply()` method calls these functions, so overriding them changes all lights at once with zero duplicated attenuation math.

```hlsl
// Define macros BEFORE the engine's BRDF file is included:
#define GetDiffuseLighting(surface, lightingData, lightIntensity, dirToLight) \
    GetDiffuseLighting_Custom(surface, lightingData, lightIntensity, dirToLight)

#define GetSpecularLighting(surface, lightingData, lightIntensity, dirToLight, viewIndex) \
    GetSpecularLighting_Custom(surface, lightingData, lightIntensity, dirToLight, viewIndex)

// Then define your custom implementations:
real3 GetDiffuseLighting_Custom(Surface surface, LightingData lightingData, real3 lightIntensity, real3 dirToLight)
{
    // Standard PBR diffuse — modify this to change shading for all lights
    real3 diffuse = DiffuseLambertian(surface.albedo, surface.GetDiffuseNormal(), dirToLight, lightingData.diffuseResponse);
    diffuse *= lightIntensity;
    return diffuse;
}
```

**For BasePBR materials** (`CustomBasePBR_ClaudeOpus`): The engine's `BasePBR_LightingBrdf.azsli` already has `#ifndef` guards. Define your macros before including it, and it skips its defaults while still defining the `_BasePBR` implementations you can forward to:

```hlsl
// In CustomBasePBR_LightingBrdf.azsli:
#define GetDiffuseLighting(...) GetDiffuseLighting_Custom(...)
#include <.../BasePBR/BasePBR_LightingBrdf.azsli>  // defines _BasePBR versions

real3 GetDiffuseLighting_Custom(Surface surface, LightingData lightingData, real3 lightIntensity, real3 dirToLight)
{
    return GetDiffuseLighting_BasePBR(surface, lightingData, lightIntensity, dirToLight);  // forward to engine
}
```

**For MinimalPBR materials** (`CustomMinimalPBR_BrdfOverride_ClaudeOpus`): `StandardLighting.azsli` does NOT have `#ifndef` guards on these functions, so you can't include it. Instead, include its pieces separately:

1. Prerequisites (Surface, LightingData, Brdf) — same includes
2. Your custom BRDF file (defines the macros + custom functions)
3. `Lights.azsli` directly (light type files that call your functions)
4. A small pipeline utilities file (~35 lines copied from StandardLighting.azsli: `PbrLightingOutput`, `GetPbrLightingOutput`, `ApplyDecals`/`ApplyDirectLighting` wrappers, and includes for `ForwardPassDecals.azsli`/`ForwardPassDirectLighting.azsli`)

Also requires `#define ENABLE_TRANSMISSION 0` before the prerequisites (normally set by `StandardLighting.azsli`).

See `CustomMinimalPBR_BrdfOverride_LightingPipeline.azsli` for the pipeline utilities file.

**Include order for MinimalPBR BRDF Override** (critical — order matters):

```hlsl
#define ENABLE_TRANSMISSION 0
#define FORCE_IBL_IN_FORWARD_PASS 1
#define FORCE_OPAQUE 1

// Standard SRG includes
#include <scenesrg_all.srgi>
#include <viewsrg_all.srgi>
#include <Atom/Features/PBR/DefaultObjectSrg.azsli>
// ... other SRG/transform/output includes

// Step 1: Prerequisites (Surface, LightingData, BRDF math)
#include <Atom/Features/PBR/LightingOptions.azsli>
#include <Atom/Features/PBR/Lighting/LightingData.azsli>
#include <Atom/Features/PBR/Surfaces/StandardSurface.azsli>
#include <Atom/Features/PBR/LightingUtils.azsli>
#include <Atom/Features/PBR/Microfacet/Brdf.azsli>
#include <Atom/Features/SampleBrdfMap.azsli>
#include <Atom/Features/GoboTexture.azsli>

// Step 2: Custom BRDF (#defines + custom GetDiffuseLighting/GetSpecularLighting)
#include "YourCustom_LightingBrdf.azsli"

// Step 3: Custom LightUtil overrides (empty stub, or per-light-type overrides)
#include "YourCustom_LightUtils.azsli"

// Step 4: Lighting pipeline (Lights.azsli + PbrLightingOutput + ApplyDecals/ApplyDirectLighting)
#include "YourCustom_LightingPipeline.azsli"

// Step 5: IBL
#include <Atom/Features/PBR/Lights/IblForward.azsli>
```

Steps 1's prerequisites use `#pragma once`, so they won't conflict with anything included later. Step 2 defines the `#define` macros before Step 4 includes `Lights.azsli` (which contains the light type files that call `GetDiffuseLighting`/`GetSpecularLighting`).

**Include order for BasePBR BRDF Override** is simpler — it follows the engine's `BasePBR.azsli` structure. Replace `BasePBR_LightingBrdf.azsli` with your custom version (which includes BasePBR's internally), and insert a LightUtils stub before `BasePBR_LightingEval.azsli`. See `CustomBasePBR.azsli` for the complete include chain.

**Specular details:**

`specularF0Factor` must be non-zero. `Surface::SetAlbedoAndSpecularF0(albedo, specularF0Factor, metallic)` computes `specularF0` from the factor. With `specularF0Factor = 0`, Fresnel is zero at all angles and no specular is produced by any light (direct or IBL). Use `specularF0Factor = 0.5` for standard dielectric reflectance (~4% at normal incidence).

Per-light specular accumulates in `lightingData.specularLighting[0]` alongside IBL specular from `ApplyIblForward`. Quantizing it once in the pixel shader output steps all specular sources together — no separate handling per source needed:

```hlsl
float specLuminance = GetLuminance(lightingData.specularLighting[0]);
float specBand = step(specularThreshold, specLuminance);
```

**`@gemroot:` paths for BasePBR materialtypes:** Custom `.materialtype` files that `$import` engine property groups (BaseColorPropertyGroup.json, etc.) must use absolute gem paths because `$import` resolves relative to the file location:

```json
{ "$import": "@gemroot:Atom_Feature_Common@/Assets/Materials/Types/MaterialInputs/BaseColorPropertyGroup.json" }
```

**`InitializeLightingData` / `FinalizeLightingData` overrides (BasePBR only):** These functions also use `#ifndef` guards in `BasePBR_LightingEval.azsli`. Override them to customize lighting setup (Fresnel response, multiscatter compensation) or to post-process the aggregate lighting result. `FinalizeLightingData` runs after all lights are accumulated and before the framework assembles the output — making it the natural place for aggregate stylization in BasePBR materials (the equivalent of modifying the pixel shader output in MinimalPBR). See `CustomBasePBR_LightingEval.azsli` for the forwarding setup.

#### Aggregate Output Stylization (Recommended for NPR)

For NPR styles like cel-shading, let the engine compute standard PBR lighting (all light types, shadows, IBL), then stylize the aggregate result at the output stage. This is what `FullPipeline_CustomLighting_MinimalPBR_SimpleCelShaded_ClaudeOpus` does — no BRDF or LightUtil overrides, just aggregate banding:

```hlsl
// After the engine pipeline runs (all lights accumulated via standard PBR):
float diffuseLuminance = GetLuminance(lightingData.diffuseLighting);
float band = step(MaterialSrg::m_diffuseThreshold, diffuseLuminance);
float3 celColor = lerp(MaterialSrg::m_unlitColor, MaterialSrg::m_litColor, band);

float specLuminance = GetLuminance(lightingData.specularLighting[0]);
float specBand = step(MaterialSrg::m_specularThreshold, specLuminance);
float3 celSpecular = specBand * MaterialSrg::m_litColor;
```

This works for the vast majority of NPR effects because most are functions of total light intensity, not per-light decisions. All light types are handled automatically.

#### How Cast Shadows Work

Shadows are computed by the engine *before* any custom lighting code runs. The engine samples shadow maps, computes visibility, and passes `litRatio` (0.0 = fully shadowed, 1.0 = fully lit) into each light's `Apply()`. Your BRDF functions receive light intensity that's already been shadow-attenuated via `litRatio`. No shadow map sampling or cascade setup needed.

This also means mixing manual light loops with the engine pipeline would double-count lights — `ApplyDirectLighting()` already iterates all lights with shadows and culling.

#### LightUtil Override (Advanced — Per-Light-Type Control)

**Directory:** `CustomMinimalPBR_LightUtilOverride_ClaudeOpus`

Use this only when BRDF overrides aren't enough — when you need different shading math for different light types (e.g., directional lights use smooth gradients while point lights use hard bands), or when you need to modify per-light attenuation curves.

Each light type's utility class is wrapped in a `#ifndef` guard:

```hlsl
#ifndef SimplePointLightUtil
#define SimplePointLightUtil SimplePointLightUtil_PBR
#endif
```

You `#define` your own class before the engine includes run. Your class must be self-contained — you can't compose with the `_PBR` base class because including the base light files triggers a circular dependency (`LightTypesCommon.azsli` → `BackLighting.azsli` → `GetDiffuseLighting`, which isn't defined yet).

**Include order for LightUtil overrides** (uses `StandardLighting.azsli`):

```hlsl
// Step 1: Prerequisites (Surface, LightingData, BRDF)
#include <Atom/Features/PBR/LightingOptions.azsli>
#include <Atom/Features/PBR/Lighting/LightingData.azsli>
#include <Atom/Features/PBR/Surfaces/StandardSurface.azsli>
#include <Atom/Features/PBR/LightingUtils.azsli>
#include <Atom/Features/PBR/Microfacet/Brdf.azsli>
#include <Atom/Features/SampleBrdfMap.azsli>
#include <Atom/Features/GoboTexture.azsli>

// Step 2: Your custom LightUtil overrides (#defines + class definitions)
#include "YourCustom_LightUtils.azsli"

// Step 3: StandardLighting.azsli (prereqs skipped via #pragma once)
#include <Atom/Features/PBR/Lighting/StandardLighting.azsli>
```

`StandardLighting.azsli` uses `#pragma once` on its sub-includes, so Step 1's prerequisites won't be double-included. When it reaches `Lights.azsli`, each light file checks `#ifndef SimplePointLightUtil` (etc.), finds your `#define` already set, and uses your class.

**Key constraints:**
- Custom classes must be self-contained. You can't compose with the `_PBR` base class because including the base light files triggers a circular dependency (`LightTypesCommon.azsli` → `BackLighting.azsli` → `GetDiffuseLighting`, which isn't defined yet).
- Include only `LightStructures.azsli` for struct definitions. Duplicate attenuation math from the `_PBR` classes.
- `GetIntensityAdjustedByRadiusAndRoughness` must be copied with a different name (e.g., `GetCustomIntensityAdjustedByRadiusAndRoughness`) to avoid ODR violations.
- Each class needs specific interface methods beyond `Init()`/`Apply()` — see `CustomMinimalPBR_LightUtilOverride_LightUtils.azsli` for the complete implementation with all required methods per light type.

| Light Type | Required Methods |
|---|---|
| DirectionalLight | `Init()`, `Apply()` |
| SimplePointLight | `Init()`, `Apply()`, `GetSurfaceToLightDirection()`, `GetFalloff()` |
| SimpleSpotLight | `Init()`, `Apply()`, `GetSurfaceToLightDirection()`, `GetFalloff()` |
| PointLight (Sphere) | `Init()`, `Apply()`, `ApplySampled()`, `GetSurfaceToLightDirection()`, `GetFalloff()` |
| DiskLight (SpotDisk) | `Init()`, `Apply()`, `ApplySampled()`, `GetSurfaceToLightDirection()`, `GetDirectionToConeTip()`, `GetFalloff()` |

#### Specular in Manual Light Loop Shaders

The Manual Light Loop approach doesn't have access to the engine's `SpecularGGX` or `GetSpecularLighting`. Use Blinn-Phong instead:

```hlsl
float3 halfVec = normalize(dirToLight + dirToCamera);
float NdotH = saturate(dot(normal, halfVec));
specularAmount += pow(NdotH, shininess) * intensity;
```

Map PBR roughness to Blinn-Phong shininess: `shininess ≈ 2 / (roughness^4) - 2`.

#### Light Types in the Editor

The editor's Light component creates different light types depending on the mode selected:

| Editor Mode | Light Type | SRG Array |
|---|---|---|
| Simple Punctual (Point) | `SimplePointLight` | `ViewSrg::m_simplePointLights` |
| Simple Punctual (Spot) | `SimpleSpotLight` | `ViewSrg::m_simpleSpotLights` |
| Sphere (default Point) | `PointLight` | `ViewSrg::m_pointLights` |
| SpotDisk (default Spot) | `DiskLight` | `ViewSrg::m_diskLights` |

The "Simple Punctual" variants are cheaper (no shadows, no bulb radius) but less commonly used. The Sphere and SpotDisk variants are the editor defaults. Custom shaders should handle both pairs to work with all editor Light component configurations.

### Making Your Own Material Emissive

If you want your custom material to *be* emissive (so it lights other things via the probe grid), add an emissive term to your forward pass output. The ray tracing pass samples whatever color your surface outputs:

```hlsl
float3 emissive = MyMaterialSrg::m_emissiveColor * MyMaterialSrg::m_emissiveIntensity;
psOutput.m_diffuseColor = float4(diffuse + emissive, 1.0);
```

The probes will pick up the emissive contribution when their rays hit your surface, and that light will then appear on nearby geometry in subsequent frames.
