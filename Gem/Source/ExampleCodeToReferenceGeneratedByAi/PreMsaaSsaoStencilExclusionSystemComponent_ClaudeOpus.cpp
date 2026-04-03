
#include "PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus.h"
#include "PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/FeatureProcessorFactory.h>
#include <Atom/RPI.Public/RPISystemInterface.h>
#include <Atom/RPI.Public/Scene.h>

namespace O3DEProjectPlaygroundCH
{
    void PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus, AZ::Component>()
                ->Version(0);
        }

        PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus::Reflect(context);
    }

    void PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        // Same service name as the Vulkan-only version so they're mutually exclusive
        provided.push_back(AZ_CRC_CE("SsaoStencilExclusionService_ClaudeOpus"));
    }

    void PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("SsaoStencilExclusionService_ClaudeOpus"));
    }

    void PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("RPISystem"));
    }

    void PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus::GetDependentServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus::Activate()
    {
        AZ::RPI::FeatureProcessorFactory::Get()->RegisterFeatureProcessor<PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus>();

        auto* passSystem = AZ::RPI::PassSystemInterface::Get();
        if (passSystem)
        {
            m_loadTemplatesHandler = AZ::RPI::PassSystemInterface::OnReadyLoadTemplatesEvent::Handler(
                [this]() { this->LoadPassTemplateMappings(); }
            );
            passSystem->ConnectEvent(m_loadTemplatesHandler);
        }

        AZ::TickBus::Handler::BusConnect();
    }

    void PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        AZ::RPI::FeatureProcessorFactory::Get()->UnregisterFeatureProcessor<PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus>();
        m_loadTemplatesHandler.Disconnect();
    }

    void PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus::OnTick(
        [[maybe_unused]] float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (m_featureProcessorEnabled)
        {
            AZ::TickBus::Handler::BusDisconnect();
            return;
        }

        auto* rpiSystem = AZ::RPI::RPISystemInterface::Get();
        if (!rpiSystem)
        {
            return;
        }

        AZ::RPI::Scene* scene = rpiSystem->GetSceneByName(AZ::Name("Main"));
        if (!scene)
        {
            return;
        }

        scene->EnableFeatureProcessor<PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus>();
        m_featureProcessorEnabled = true;

        AZ_TracePrintf("PreMsaaSsaoStencilExclusion_ClaudeOpus",
            "PreMsaaSsaoStencilExclusionFeatureProcessor enabled on default scene\n");

        AZ::TickBus::Handler::BusDisconnect();
    }

    void PreMsaaSsaoStencilExclusionSystemComponent_ClaudeOpus::LoadPassTemplateMappings()
    {
        auto* passSystem = AZ::RPI::PassSystemInterface::Get();
        if (!passSystem)
        {
            return;
        }

        // Uses the same PassTemplates azasset (it contains both Vulkan-only and pre-MSAA templates)
        if (!passSystem->LoadPassTemplateMappings("Assets/Passes/ExampleCodeToReferenceGeneratedByAi/PassTemplates_ClaudeOpus.azasset"))
        {
            AZ_Warning("PreMsaaSsaoStencilExclusion_ClaudeOpus", false,
                "Failed to load PassTemplates_ClaudeOpus.azasset");
        }
    }
}
