
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/EBus/Event.h>

namespace O3DEProjectPlaygroundCH
{
    //! System component for the pre-MSAA hardware stencil SSAO exclusion approach.
    //! Cross-platform (DX12 + Vulkan). Uses hardware stencil testing at the pre-MSAA-resolve
    //! pipeline stage where MSAA sample counts match between render targets and depth-stencil.
    //!
    //! This is the cross-platform alternative to SsaoStencilExclusionSystemComponent_ClaudeOpus
    //! which uses stencil-as-texture reads (Vulkan only).
    //!
    //! IMPORTANT: Only one of these system components should be active at a time.
    //! Enable this one for DX12 support, or the other one for simpler Vulkan-only usage.
    class PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus, "{B2E4C8D1-6A3F-4752-8D19-E5C7A9F01B3E}");

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
