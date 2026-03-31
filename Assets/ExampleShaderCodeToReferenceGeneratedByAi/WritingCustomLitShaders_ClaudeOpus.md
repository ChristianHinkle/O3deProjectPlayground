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

### Making Your Own Material Emissive

If you want your custom material to *be* emissive (so it lights other things via the probe grid), add an emissive term to your forward pass output. The ray tracing pass samples whatever color your surface outputs:

```hlsl
float3 emissive = MyMaterialSrg::m_emissiveColor * MyMaterialSrg::m_emissiveIntensity;
psOutput.m_diffuseColor = float4(diffuse + emissive, 1.0);
```

The probes will pick up the emissive contribution when their rays hit your surface, and that light will then appear on nearby geometry in subsequent frames.
