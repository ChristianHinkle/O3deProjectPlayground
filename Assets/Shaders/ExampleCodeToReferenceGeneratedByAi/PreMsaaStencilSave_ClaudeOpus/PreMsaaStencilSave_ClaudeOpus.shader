{
    // Pre-MSAA stencil save shader config.
    // Hardware stencil test: only pixels WITHOUT bit 0x80 pass (excluded materials).
    // Runs before MSAA resolve where diffuse and depth-stencil are both MSAA.

    "Source": "PreMsaaStencilSave_ClaudeOpus.azsl",
    "DepthStencilState": {
        "Depth": {
            "Enable": false
        },
        "Stencil": {
            "Enable": true,
            // Only check bit 0x80
            "ReadMask": "0x80",
            // Don't write stencil
            "WriteMask": "0x00",
            "FrontFace": {
                // Pass if (stencil & 0x80) != 0x80 (excluded pixels)
                "Func": "NotEqual",
                "DepthFailOp": "Keep",
                "FailOp": "Keep",
                "PassOp": "Keep"
            },
            "BackFace": {
                "Func": "NotEqual",
                "DepthFailOp": "Keep",
                "FailOp": "Keep",
                "PassOp": "Keep"
            }
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
