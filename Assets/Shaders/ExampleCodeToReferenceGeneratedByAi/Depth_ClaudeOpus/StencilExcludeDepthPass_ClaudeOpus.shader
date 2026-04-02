{
    // Custom Depth Pass for Stencil-Excluded Materials
    //
    // Identical to the engine's Shaders/Depth/DepthPass.shader but with stencil WriteMask 0x7F.
    // This prevents bit 0x80 (UseDiffuseGIPass) from being written during the depth prepass,
    // which is necessary for stencil-based SSAO exclusion to work.
    //
    // Without this, the engine's depth prepass writes stencil 0xFF for ALL geometry,
    // and the forward pass WriteMask 0x7F can only preserve (not clear) bit 0x80.
    //
    // The Source references the engine's DepthPass.azsl via a wrapper include file.
    // No shader code is duplicated -- only this JSON config differs from the engine's version.
    //
    // Materials that want stencil-based exclusion from post-processing effects should
    // reference this depth pass in their .materialtype "shaders" array instead of
    // the engine's "Shaders/Depth/DepthPass.shader".

    "Source" : "StencilExcludeDepthPass_ClaudeOpus.azsl",

    "DepthStencilState" : {
        "Depth" : { "Enable" : true, "CompareFunc" : "GreaterEqual" },
        "Stencil" : {
            "Enable" : true,
            // Don't read stencil (always pass the stencil test)
            "ReadMask" : "0x00",
            // Write all bits EXCEPT bit 7 (0x80). This leaves bit 0x80 at 0
            // (from the frame clear), allowing post-processing passes to identify
            // these pixels as excluded.
            "WriteMask" : "0x7F",
            "FrontFace" : {
                "Func" : "Always",
                "DepthFailOp" : "Keep",
                "FailOp" : "Keep",
                "PassOp" : "Replace"
            }
        }
    },

    "ProgramSettings" :
    {
        "EntryPoints":
        [
            {
                "name": "DepthPassVS",
                "type" : "Vertex"
            }
        ]
    },

    "DrawList" : "depth"
}
