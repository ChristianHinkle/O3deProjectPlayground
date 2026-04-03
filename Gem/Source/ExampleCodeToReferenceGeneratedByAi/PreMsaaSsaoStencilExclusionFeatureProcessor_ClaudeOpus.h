
#pragma once

#include <Atom/RPI.Public/FeatureProcessor.h>

namespace O3DEProjectPlaygroundCH
{
    //! FeatureProcessor that implements per-material SSAO exclusion using hardware stencil
    //! testing at the pre-MSAA-resolve pipeline stage. Works on both DX12 and Vulkan.
    //!
    //! Inserts three passes into the pipeline:
    //! 1. Save pass (before MSAA resolve): Hardware stencil test copies excluded pixels' MSAA
    //!    diffuse to an MSAA temp buffer.
    //! 2. Temp resolve pass (after MSAAResolveDiffusePass): Resolves the MSAA temp to non-MSAA.
    //! 3. Restore pass (after Ssao): Copies resolved temp back to diffuse for excluded pixels,
    //!    undoing SSAO darkening.
    //!
    //! Materials opt into exclusion by using WriteMask 0x7F in both their forward pass and
    //! depth pass .shader files (avoiding stencil bit 0x80).
    class PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus final : public AZ::RPI::FeatureProcessor
    {
    public:
        AZ_RTTI(PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus, "{3F2A8B5C-D914-4E07-B6C1-A8E9F5D23C7B}", AZ::RPI::FeatureProcessor);
        AZ_CLASS_ALLOCATOR(PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus, AZ::SystemAllocator);
        AZ_FEATURE_PROCESSOR(PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus);

        static void Reflect(AZ::ReflectContext* context);

        void Activate() override {}
        void Deactivate() override {}
        void Render(const RenderPacket&) override {}
        void Simulate(const SimulatePacket&) override {}

        void AddRenderPasses(AZ::RPI::RenderPipeline* renderPipeline) override;
    };
}
