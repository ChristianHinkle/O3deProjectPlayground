{
    // LightUtil Override: 2-band cel-shaded forward pass shader.
    // Overrides per-light shading via engine LightUtil #ifndef hooks.
    // WriteMask 0x7F: avoids writing stencil bit 0x80 for SSAO exclusion.
    // DrawList "forward" registers this for the forward lighting pass.

    "Source": "LightUtilOverride_MinimalPBR_SimpleCelShaded.azsl",
    "DepthStencilState": {
        "Depth": {
            "Enable": true,
            "CompareFunc": "GreaterEqual"
        },
        "Stencil": {
            "Enable": true,
            "ReadMask": "0x00",
            "WriteMask": "0x7F",
            "FrontFace": {
                "Func": "Always",
                "DepthFailOp": "Keep",
                "FailOp": "Keep",
                "PassOp": "Replace"
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
    },
    "DrawList": "forward"
}
