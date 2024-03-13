#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"

namespace Vulkan
{

	namespace Buffer
	{

		VulkanBuffer CreateVertex(uint64_t size_in_bytes, const std::string& name);
		VulkanBuffer CreateIndex(uint64_t size_in_bytes, const std::string& name);

		VulkanBuffer Create(const BufferCreateInfo& buffer_info);
		void Destroy(const VulkanBuffer& buffer);

		VkMemoryRequirements GetMemoryRequirements(const VulkanBuffer& buffer);

	}

}
