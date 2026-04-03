{
    // Pre-MSAA stencil restore shader config.
    // No stencil testing -- the temp buffer contents serve as the mask.
    // Runs after SSAO where diffuse is non-MSAA.

    "Source": "PreMsaaStencilRestore_ClaudeOpus.azsl",
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
