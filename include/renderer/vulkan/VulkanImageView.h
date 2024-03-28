#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"

namespace Vulkan
{

	namespace ImageView
	{

		VulkanImageView Create(const VulkanImage& image, const TextureViewCreateInfo& texture_view_info);
		void Destroy(VulkanImageView& image_view);

	}

}
