
#include "SsaoStencilExclusionFeatureProcessor_ClaudeOpus.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/RenderPipeline.h>
#include <Atom/RPI.Reflect/Pass/PassRequest.h>

namespace O3DEProjectPlaygroundCH
{
    void SsaoStencilExclusionFeatureProcessor::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<SsaoStencilExclusionFeatureProcessor, AZ::RPI::FeatureProcessor>()
                ->Version(0);
        }
    }

    void SsaoStencilExclusionFeatureProcessor::AddRenderPasses(AZ::RPI::RenderPipeline* renderPipeline)
    {
        // Check if our passes already exist in this pipeline (avoid duplicates on hot-reload)
        if (renderPipeline->FindFirstPass(AZ::Name("SsaoStencilSave_ClaudeOpus")))
        {
            return;
        }

        AddSaveRestorePasses(renderPipeline);
    }

    void SsaoStencilExclusionFeatureProcessor::AddSaveRestorePasses(AZ::RPI::RenderPipeline* renderPipeline)
    {
        auto* passSystem = AZ::RPI::PassSystemInterface::Get();

        // Verify the Ssao pass exists in this pipeline
        if (!renderPipeline->FindFirstPass(AZ::Name("Ssao")))
        {
            return; // This pipeline doesn't have SSAO (e.g. low-end pipeline)
        }

        // --- Create SAVE pass (before Ssao) ---
        {
            AZ::RPI::PassRequest saveRequest;
            saveRequest.m_passName = AZ::Name("SsaoStencilSave_ClaudeOpus");
            saveRequest.m_templateName = AZ::Name("SsaoStencilSaveTemplate_ClaudeOpus");

            // SourceInput → the diffuse buffer feeding into Ssao
            AZ::RPI::PassConnection sourceConn;
            sourceConn.m_localSlot = AZ::Name("SourceInput");
            sourceConn.m_attachmentRef.m_pass = AZ::Name("SubsurfaceScatteringPass");
            sourceConn.m_attachmentRef.m_attachment = AZ::Name("Output");
            saveRequest.m_connections.push_back(sourceConn);

            // StencilInput → depth-stencil buffer (read stencil plane as texture)
            AZ::RPI::PassConnection stencilConn;
            stencilConn.m_localSlot = AZ::Name("StencilInput");
            stencilConn.m_attachmentRef.m_pass = AZ::Name("Parent");
            stencilConn.m_attachmentRef.m_attachment = AZ::Name("DepthStencil");
            saveRequest.m_connections.push_back(stencilConn);

            AZ::RPI::Ptr<AZ::RPI::Pass> savePass = passSystem->CreatePassFromRequest(&saveRequest);
            if (savePass)
            {
                if (renderPipeline->AddPassBefore(savePass, AZ::Name("Ssao")))
                {
                    AZ_TracePrintf("O3DEProjectPlaygroundCH", "Inserted SsaoStencilSave pass before Ssao in pipeline [%s]\n",
                        renderPipeline->GetId().GetCStr());
                }
            }
            else
            {
                AZ_Warning("O3DEProjectPlaygroundCH", false, "Failed to create SsaoStencilSave pass");
                return;
            }
        }

        // --- Create RESTORE pass (after Ssao) ---
        {
            AZ::RPI::PassRequest restoreRequest;
            restoreRequest.m_passName = AZ::Name("SsaoStencilRestore_ClaudeOpus");
            restoreRequest.m_templateName = AZ::Name("SsaoStencilRestoreTemplate_ClaudeOpus");

            // SourceInput → the saved temp buffer from the SAVE pass
            AZ::RPI::PassConnection sourceConn;
            sourceConn.m_localSlot = AZ::Name("SourceInput");
            sourceConn.m_attachmentRef.m_pass = AZ::Name("SsaoStencilSave_ClaudeOpus");
            sourceConn.m_attachmentRef.m_attachment = AZ::Name("SavedOutput");
            restoreRequest.m_connections.push_back(sourceConn);

            // DiffuseInputOutput → the diffuse buffer after SSAO modulation
            AZ::RPI::PassConnection diffuseConn;
            diffuseConn.m_localSlot = AZ::Name("DiffuseInputOutput");
            diffuseConn.m_attachmentRef.m_pass = AZ::Name("Ssao");
            diffuseConn.m_attachmentRef.m_attachment = AZ::Name("Output");
            restoreRequest.m_connections.push_back(diffuseConn);

            // StencilInput → depth-stencil buffer (read stencil plane as texture)
            AZ::RPI::PassConnection stencilConn;
            stencilConn.m_localSlot = AZ::Name("StencilInput");
            stencilConn.m_attachmentRef.m_pass = AZ::Name("Parent");
            stencilConn.m_attachmentRef.m_attachment = AZ::Name("DepthStencil");
            restoreRequest.m_connections.push_back(stencilConn);

            AZ::RPI::Ptr<AZ::RPI::Pass> restorePass = passSystem->CreatePassFromRequest(&restoreRequest);
            if (restorePass)
            {
                if (renderPipeline->AddPassAfter(restorePass, AZ::Name("Ssao")))
                {
                    AZ_TracePrintf("O3DEProjectPlaygroundCH", "Inserted SsaoStencilRestore pass after Ssao in pipeline [%s]\n",
                        renderPipeline->GetId().GetCStr());
                }
            }
            else
            {
                AZ_Warning("O3DEProjectPlaygroundCH", false, "Failed to create SsaoStencilRestore pass");
            }
        }
    }
}
