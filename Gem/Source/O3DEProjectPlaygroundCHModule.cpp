
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/Module/Module.h>

#include "O3DEProjectPlaygroundCHSystemComponent.h"
#include "YoComponent.h"
#include "ExampleCodeToReferenceGeneratedByAi/SsaoStencilExclusionSystemComponent_ClaudeOpus.h"

#include <O3DEProjectPlaygroundCH/O3DEProjectPlaygroundCHTypeIds.h>

namespace O3DEProjectPlaygroundCH
{
    class O3DEProjectPlaygroundCHModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(O3DEProjectPlaygroundCHModule, O3DEProjectPlaygroundCHModuleTypeId, AZ::Module);
        AZ_CLASS_ALLOCATOR(O3DEProjectPlaygroundCHModule, AZ::SystemAllocator);

        O3DEProjectPlaygroundCHModule()
            : AZ::Module()
        {
            // Push results of [MyComponent]::CreateDescriptor() into m_descriptors here.
            m_descriptors.insert(m_descriptors.end(), {
                O3DEProjectPlaygroundCHSystemComponent::CreateDescriptor(),
                YoComponent::CreateDescriptor(),
                SsaoStencilExclusionSystemComponent_ClaudeOpus::CreateDescriptor(),
            });
        }

        /**
         * Add required SystemComponents to the SystemEntity.
         */
        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{
                azrtti_typeid<O3DEProjectPlaygroundCHSystemComponent>(),
                azrtti_typeid<YoComponent>(),
                azrtti_typeid<SsaoStencilExclusionSystemComponent_ClaudeOpus>(),
            };
        }
    };
}// namespace O3DEProjectPlaygroundCH

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), O3DEProjectPlaygroundCH::O3DEProjectPlaygroundCHModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_O3DEProjectPlaygroundCH, O3DEProjectPlaygroundCH::O3DEProjectPlaygroundCHModule)
#endif
