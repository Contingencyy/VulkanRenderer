#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"

namespace Vulkan
{

	namespace Image
	{
		
		VulkanImage Create(const TextureCreateInfo& texture_info);
		void Destroy(VulkanImage& image);

		VkMemoryRequirements GetMemoryRequirements(const VulkanImage& image);

	}

}
