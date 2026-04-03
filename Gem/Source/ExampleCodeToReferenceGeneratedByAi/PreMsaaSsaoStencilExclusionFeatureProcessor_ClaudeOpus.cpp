
#include "PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/RenderPipeline.h>
#include <Atom/RPI.Reflect/Pass/PassRequest.h>

namespace O3DEProjectPlaygroundCH
{
    void PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus, AZ::RPI::FeatureProcessor>()
                ->Version(0);
        }
    }

    void PreMsaaSsaoStencilExclusionFeatureProcessor_ClaudeOpus::AddRenderPasses(AZ::RPI::RenderPipeline* renderPipeline)
    {
        // Skip if our passes already exist (e.g., hot-reload)
        if (renderPipeline->FindFirstPass(AZ::Name("PreMsaaStencilSave_ClaudeOpus")))
        {
            return;
        }

        // Skip pipelines without SSAO
        if (!renderPipeline->FindFirstPass(AZ::Name("Ssao")))
        {
            return;
        }

        auto* passSystem = AZ::RPI::PassSystemInterface::Get();

        // --- Pass 1: Save (before MSAA resolve, after DiffuseGlobalFullscreenPass) ---
        // At this pipeline stage, both diffuse and depth-stencil are MSAA.
        // Hardware stencil testing works because sample counts match.
        {
            AZ::RPI::PassRequest request;
            request.m_passName = AZ::Name("PreMsaaStencilSave_ClaudeOpus");
            request.m_templateName = AZ::Name("PreMsaaStencilSaveTemplate_ClaudeOpus");

            // SourceInput → MSAA diffuse (same buffer DiffuseGlobalFullscreenPass writes to)
            AZ::RPI::PassConnection sourceConn;
            sourceConn.m_localSlot = AZ::Name("SourceInput");
            sourceConn.m_attachmentRef.m_pass = AZ::Name("DiffuseGlobalFullscreenPass");
            sourceConn.m_attachmentRef.m_attachment = AZ::Name("DiffuseInputOutput");
            request.m_connections.push_back(sourceConn);

            // DepthStencilInputOutput → MSAA depth-stencil from OpaqueParent
            AZ::RPI::PassConnection dsConn;
            dsConn.m_localSlot = AZ::Name("DepthStencilInputOutput");
            dsConn.m_attachmentRef.m_pass = AZ::Name("Parent");
            dsConn.m_attachmentRef.m_attachment = AZ::Name("DepthStencil");
            request.m_connections.push_back(dsConn);

            // SavedOutput → connected internally to transient MSAA image in the template

            auto pass = passSystem->CreatePassFromRequest(&request);
            if (pass)
            {
                // Insert after DiffuseGlobalFullscreenPass (before ReflectionsPass)
                if (renderPipeline->AddPassAfter(pass, AZ::Name("DiffuseGlobalFullscreenPass")))
                {
                    AZ_TracePrintf("PreMsaaSsaoStencilExclusion_ClaudeOpus",
                        "Inserted PreMsaaStencilSave pass after DiffuseGlobalFullscreenPass\n");
                }
            }
            else
            {
                AZ_Warning("PreMsaaSsaoStencilExclusion_ClaudeOpus", false, "Failed to create save pass");
                return;
            }
        }

        // --- Pass 2: Resolve MSAA temp to non-MSAA ---
        // Uses the engine's existing MSAAResolveColorTemplate (same as MSAAResolveDiffusePass).
        {
            AZ::RPI::PassRequest request;
            request.m_passName = AZ::Name("PreMsaaStencilResolve_ClaudeOpus");
            request.m_templateName = AZ::Name("MSAAResolveColorTemplate");

            // Input → the MSAA temp buffer from the save pass
            AZ::RPI::PassConnection inputConn;
            inputConn.m_localSlot = AZ::Name("Input");
            inputConn.m_attachmentRef.m_pass = AZ::Name("PreMsaaStencilSave_ClaudeOpus");
            inputConn.m_attachmentRef.m_attachment = AZ::Name("SavedOutput");
            request.m_connections.push_back(inputConn);

            // Output → resolved non-MSAA image (created internally by the template)

            auto pass = passSystem->CreatePassFromRequest(&request);
            if (pass)
            {
                // Insert after MSAAResolveDiffusePass (alongside other resolve passes)
                if (renderPipeline->AddPassAfter(pass, AZ::Name("MSAAResolveDiffusePass")))
                {
                    AZ_TracePrintf("PreMsaaSsaoStencilExclusion_ClaudeOpus",
                        "Inserted PreMsaaStencilResolve pass after MSAAResolveDiffusePass\n");
                }
            }
            else
            {
                AZ_Warning("PreMsaaSsaoStencilExclusion_ClaudeOpus", false, "Failed to create resolve pass");
                return;
            }
        }

        // --- Pass 3: Restore (after Ssao) ---
        // Reads the resolved non-MSAA temp and overwrites SSAO-darkened diffuse for excluded pixels.
        {
            AZ::RPI::PassRequest request;
            request.m_passName = AZ::Name("PreMsaaStencilRestore_ClaudeOpus");
            request.m_templateName = AZ::Name("PreMsaaStencilRestoreTemplate_ClaudeOpus");

            // SourceInput → resolved temp buffer from the resolve pass
            AZ::RPI::PassConnection sourceConn;
            sourceConn.m_localSlot = AZ::Name("SourceInput");
            sourceConn.m_attachmentRef.m_pass = AZ::Name("PreMsaaStencilResolve_ClaudeOpus");
            sourceConn.m_attachmentRef.m_attachment = AZ::Name("Output");
            request.m_connections.push_back(sourceConn);

            // DiffuseInputOutput → post-SSAO non-MSAA diffuse buffer
            AZ::RPI::PassConnection diffuseConn;
            diffuseConn.m_localSlot = AZ::Name("DiffuseInputOutput");
            diffuseConn.m_attachmentRef.m_pass = AZ::Name("Ssao");
            diffuseConn.m_attachmentRef.m_attachment = AZ::Name("Output");
            request.m_connections.push_back(diffuseConn);

            auto pass = passSystem->CreatePassFromRequest(&request);
            if (pass)
            {
                if (renderPipeline->AddPassAfter(pass, AZ::Name("Ssao")))
                {
                    AZ_TracePrintf("PreMsaaSsaoStencilExclusion_ClaudeOpus",
                        "Inserted PreMsaaStencilRestore pass after Ssao\n");
                }
            }
            else
            {
                AZ_Warning("PreMsaaSsaoStencilExclusion_ClaudeOpus", false, "Failed to create restore pass");
            }
        }
    }
}
