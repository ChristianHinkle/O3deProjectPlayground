{
    // SSAO Stencil Copy Shader Config
    //
    // Used by both the Save and Restore passes (SsaoStencilSave / SsaoStencilRestore).
    // The same shader copies pixels based on stencil -- the only difference between
    // save and restore is which textures the pass template connects to the slots.
    //
    // No hardware stencil testing is configured here because the stencil is read as a
    // texture in the shader (via Texture2DMS<uint>). This avoids MSAA state mismatches
    // between the non-MSAA diffuse buffer and MSAA depth-stencil at this pipeline stage.
    //
    // Depth testing is disabled since this is a fullscreen post-processing pass.
    //
    // NOTE: Stencil-as-texture reading works on Vulkan but not O3DE's DX12 backend.

    "Source": "SsaoStencilCopy_ClaudeOpus.azsl",
    "DepthStencilState": {
        "Depth": {
            "Enable": false
        }
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
