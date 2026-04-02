
#pragma once

#include <Atom/RPI.Public/FeatureProcessor.h>

namespace O3DEProjectPlaygroundCH
{
    //! Minimal FeatureProcessor that inserts save/restore passes around the SSAO pass
    //! during pipeline construction. This excludes specific materials from SSAO (e.g., NPR materials) by saving
    //! their diffuse values before SSAO and restoring them after.
    class SsaoStencilExclusionFeatureProcessor final : public AZ::RPI::FeatureProcessor
    {
    public:
        AZ_RTTI(SsaoStencilExclusionFeatureProcessor, "{7E3F4A2C-B891-4D67-A5CE-9F1B3E8D2A4C}", AZ::RPI::FeatureProcessor);
        AZ_CLASS_ALLOCATOR(SsaoStencilExclusionFeatureProcessor, AZ::SystemAllocator);
        AZ_FEATURE_PROCESSOR(SsaoStencilExclusionFeatureProcessor);

        static void Reflect(AZ::ReflectContext* context);

        // FeatureProcessor overrides
        void Activate() override {}
        void Deactivate() override {}
        void Render(const RenderPacket&) override {}
        void Simulate(const SimulatePacket&) override {}

        void AddRenderPasses(AZ::RPI::RenderPipeline* renderPipeline) override;

    private:
        void AddSaveRestorePasses(AZ::RPI::RenderPipeline* renderPipeline);
    };
}
