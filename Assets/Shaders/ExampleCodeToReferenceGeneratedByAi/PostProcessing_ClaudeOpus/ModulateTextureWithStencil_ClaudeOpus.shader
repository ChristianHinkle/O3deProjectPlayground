{
    // Stencil-Aware SSAO Modulation Shader Config (Reference Implementation)
    //
    // Designed to replace the engine's compute-based ModulateTexture shader for SSAO.
    // Uses hardware stencil testing to skip excluded pixels (bit 0x80 not set) and
    // multiplicative blending (dest * src) to darken included pixels by the SSAO value.
    //
    // The shader outputs the SSAO grayscale value as a color. The blend state
    // (BlendSource: Zero, BlendDest: ColorSource) computes: dest * src, effectively
    // multiplying the existing diffuse by the SSAO factor without reading the diffuse
    // in the shader.
    //
    // Stencil config: ReadMask 0x80 with Func Equal and StencilRef 128 (set in .pass)
    // means only pixels where (stencil & 0x80) == 0x80 pass the test.
    //
    // Not currently used in the active pipeline -- see SsaoStencilCopy for the active approach.

    "Source": "ModulateTextureWithStencil_ClaudeOpus.azsl",
    "DepthStencilState": {
        "Depth": {
            "Enable": false
        },
        "Stencil": {
            "Enable": true,
            // Only check bit 0x80
            "ReadMask": "0x80",
            // Don't write to stencil
            "WriteMask": "0x00",
            "FrontFace": {
                // Only pass if (stencil & ReadMask) == StencilRef
                "Func": "Equal",
                "DepthFailOp": "Keep",
                "FailOp": "Keep",
                "PassOp": "Keep"
            },
            "BackFace": {
                "Func": "Equal",
                "DepthFailOp": "Keep",
                "FailOp": "Keep",
                "PassOp": "Keep"
            }
        }
    },
    "GlobalTargetBlendState": {
        "Enable": true,
        // Multiplicative blend: finalColor = 0 * shaderOutput + existingColor * shaderOutput
        // = existingColor * shaderOutput. The shader outputs the SSAO value,
        // so the result is: diffuse *= ssao.
        "BlendSource": "Zero",
        "BlendDest": "ColorSource",
        "BlendOp": "Add"
    },
    "ProgramSettings": {
        "EntryPoints": [
            {
                "name": "MainVS",
                "type": "Vertex"
            },
            {
                "name": "MainPS",
                "type": "Fragment"
            }
        ]
    }
}
