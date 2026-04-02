
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/EBus/Event.h>

namespace O3DEProjectPlaygroundCH
{
    //! System component that sets up stencil-based SSAO exclusion.
    //! Registers the SsaoStencilExclusionFeatureProcessor, loads custom pass templates,
    //! and enables the feature processor on the main scene.
    //!
    //! Materials that use WriteMask 0x7F (avoiding stencil bit 0x80) and the custom
    //! StencilExcludeDepthPass will be excluded from SSAO darkening.
    class SsaoStencilExclusionSystemComponent_ClaudeOpus
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(SsaoStencilExclusionSystemComponent_ClaudeOpus, "{A1D3E7F0-5B8C-4926-9E2A-7F3D1C6B8A4E}");

        static void Reflect(AZ::ReflectContext* context);

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

    protected:
        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus::Handler
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        void LoadPassTemplateMappings();

        AZ::Event<>::Handler m_loadTemplatesHandler;
        bool m_featureProcessorEnabled = false;
    };
}
