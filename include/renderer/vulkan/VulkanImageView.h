#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"

namespace Vulkan
{

	VulkanImageView CreateImageView(VulkanImage image, const TextureViewCreateInfo& texture_view_info);
	void DestroyImageView(VulkanImageView image_view);

}
