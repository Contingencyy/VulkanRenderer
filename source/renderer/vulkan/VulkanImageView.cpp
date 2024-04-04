#include "Precomp.h"
#include "renderer/vulkan/VulkanImageView.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	namespace ImageView
	{

		VulkanImageView Create(const VulkanImage& image, const TextureViewCreateInfo& texture_view_info)
		{
			VkImageViewCreateInfo vk_view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			vk_view_info.image = image.vk_image;
			vk_view_info.viewType = Util::ToVkViewType(texture_view_info.dimension);
			vk_view_info.format = Util::ToVkFormat(texture_view_info.format);
			//vk_view_info.components.r = VK_COMPONENT_SWIZZLE_R;
			//vk_view_info.components.g = VK_COMPONENT_SWIZZLE_G;
			//vk_view_info.components.b = VK_COMPONENT_SWIZZLE_B;
			//vk_view_info.components.a = VK_COMPONENT_SWIZZLE_A;

			if (Util::ToVkFormat(texture_view_info.format) == VK_FORMAT_D32_SFLOAT)
				vk_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			else
				vk_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

			vk_view_info.subresourceRange.baseMipLevel = texture_view_info.base_mip;
			vk_view_info.subresourceRange.levelCount = texture_view_info.num_mips == UINT32_MAX ? image.num_mips : texture_view_info.num_mips;
			vk_view_info.subresourceRange.baseArrayLayer = texture_view_info.base_layer;
			vk_view_info.subresourceRange.layerCount = texture_view_info.num_layers == UINT32_MAX ? image.num_layers : texture_view_info.num_layers;
			vk_view_info.flags = 0;

			VkImageView vk_image_view = VK_NULL_HANDLE;
			VkCheckResult(vkCreateImageView(vk_inst.device, &vk_view_info, nullptr, &vk_image_view));

			VulkanImageView image_view = {};
			image_view.image = image;
			image_view.vk_image_view = vk_image_view;
			image_view.base_mip = vk_view_info.subresourceRange.baseMipLevel;
			image_view.num_mips = vk_view_info.subresourceRange.levelCount;
			image_view.base_layer = vk_view_info.subresourceRange.baseArrayLayer;
			image_view.num_layers = vk_view_info.subresourceRange.layerCount;

			return image_view;
		}

		void Destroy(VulkanImageView& image_view)
		{
			if (!image_view.vk_image_view)
				return;

			vkDestroyImageView(vk_inst.device, image_view.vk_image_view, nullptr);

			image_view = {};
		}

	}

}
