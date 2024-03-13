#include "Precomp.h"
#include "renderer/vulkan/VulkanImage.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanDeviceMemory.h"
#include "renderer/vulkan/VulkanResourceTracker.h"
#include "renderer/vulkan/VulkanUtils.h"

namespace Vulkan
{

	namespace Image
	{

		VulkanImage Create(const TextureCreateInfo& texture_info)
		{
			VkImageCreateInfo vk_image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			vk_image_info.imageType = VK_IMAGE_TYPE_2D;
			vk_image_info.extent.width = texture_info.width;
			vk_image_info.extent.height = texture_info.height;
			vk_image_info.extent.depth = 1;
			vk_image_info.mipLevels = texture_info.num_mips;
			vk_image_info.arrayLayers = texture_info.num_layers;
			vk_image_info.format = Util::ToVkFormat(texture_info.format);
			vk_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
			vk_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			vk_image_info.usage = Util::ToVkImageUsageFlags(texture_info.usage_flags);
			vk_image_info.samples = VK_SAMPLE_COUNT_1_BIT;
			vk_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			vk_image_info.flags = texture_info.dimension == TEXTURE_DIMENSION_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;

			VkImage vk_image = VK_NULL_HANDLE;
			VkCheckResult(vkCreateImage(vk_inst.device, &vk_image_info, nullptr, &vk_image));
			Vulkan::DebugNameObject((uint64_t)vk_image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, texture_info.name.c_str());

			VulkanImage image = {};
			image.vk_image = vk_image;
			image.vk_format = vk_image_info.format;
			image.width = vk_image_info.extent.width;
			image.height = vk_image_info.extent.height;
			image.depth = vk_image_info.extent.depth;
			image.num_mips = vk_image_info.mipLevels;
			image.num_layers = vk_image_info.arrayLayers;
			image.memory = DeviceMemory::Allocate(image, texture_info);

			ResourceTracker::TrackImage(image, vk_image_info.initialLayout);

			return image;
		}

		void Destroy(const VulkanImage& image)
		{
			ResourceTracker::RemoveImage(image);
			vkDestroyImage(vk_inst.device, image.vk_image, nullptr);
		}

		VkMemoryRequirements GetMemoryRequirements(const VulkanImage& image)
		{
			VkMemoryRequirements2 memory_req = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
			VkImageMemoryRequirementsInfo2 image_memory_req = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
			image_memory_req.image = image.vk_image;

			vkGetImageMemoryRequirements2(vk_inst.device, &image_memory_req, &memory_req);
			return memory_req.memoryRequirements;
		}

	}

}
