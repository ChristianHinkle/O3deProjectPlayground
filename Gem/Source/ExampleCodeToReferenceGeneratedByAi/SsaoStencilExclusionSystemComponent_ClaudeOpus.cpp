
#include "SsaoStencilExclusionSystemComponent_ClaudeOpus.h"
#include "SsaoStencilExclusionFeatureProcessor_ClaudeOpus.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/FeatureProcessorFactory.h>
#include <Atom/RPI.Public/RPISystemInterface.h>
#include <Atom/RPI.Public/Scene.h>

namespace O3DEProjectPlaygroundCH
{
    void SsaoStencilExclusionSystemComponent_ClaudeOpus::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<SsaoStencilExclusionSystemComponent_ClaudeOpus, AZ::Component>()
                ->Version(0);
        }

        // Reflect the FeatureProcessor so the factory can create instances
        SsaoStencilExclusionFeatureProcessor::Reflect(context);
    }

    void SsaoStencilExclusionSystemComponent_ClaudeOpus::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("SsaoStencilExclusionService_ClaudeOpus"));
    }

    void SsaoStencilExclusionSystemComponent_ClaudeOpus::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("SsaoStencilExclusionService_ClaudeOpus"));
    }

    void SsaoStencilExclusionSystemComponent_ClaudeOpus::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("RPISystem"));
    }

    void SsaoStencilExclusionSystemComponent_ClaudeOpus::GetDependentServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void SsaoStencilExclusionSystemComponent_ClaudeOpus::Activate()
    {
        AZ::RPI::FeatureProcessorFactory::Get()->RegisterFeatureProcessor<SsaoStencilExclusionFeatureProcessor>();

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

    void SsaoStencilExclusionSystemComponent_ClaudeOpus::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        AZ::RPI::FeatureProcessorFactory::Get()->UnregisterFeatureProcessor<SsaoStencilExclusionFeatureProcessor>();
        m_loadTemplatesHandler.Disconnect();
    }

    void SsaoStencilExclusionSystemComponent_ClaudeOpus::OnTick(
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

        scene->EnableFeatureProcessor<SsaoStencilExclusionFeatureProcessor>();
        m_featureProcessorEnabled = true;

        AZ_TracePrintf("SsaoStencilExclusion_ClaudeOpus",
            "SsaoStencilExclusionFeatureProcessor enabled on default scene\n");

        AZ::TickBus::Handler::BusDisconnect();
    }

    void SsaoStencilExclusionSystemComponent_ClaudeOpus::LoadPassTemplateMappings()
    {
        auto* passSystem = AZ::RPI::PassSystemInterface::Get();
        if (!passSystem)
        {
            return;
        }

        if (!passSystem->LoadPassTemplateMappings("Assets/Passes/ExampleCodeToReferenceGeneratedByAi/PassTemplates_ClaudeOpus.azasset"))
        {
            AZ_Warning("SsaoStencilExclusion_ClaudeOpus", false,
                "Failed to load PassTemplates_ClaudeOpus.azasset -- SSAO stencil exclusion will not be active");
        }
    }
}
