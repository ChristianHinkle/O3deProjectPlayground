{
    "Source": "FullPipeline_MinimalPBR_SimpleCelShaded.azsl",
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
