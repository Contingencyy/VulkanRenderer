#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"

namespace Vulkan
{

	inline void VkCheckResult(VkResult result);

	void DebugNameObject(uint64_t object, VkDebugReportObjectTypeEXT object_type, const std::string& debug_name);

	namespace Util
	{

		VkMemoryPropertyFlags ToVkMemoryPropertyFlags(Flags memory_flags);
		uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags mem_properties);

		VkBufferUsageFlags ToVkBufferUsageFlags(Flags usage_flags);

		VkFormat ToVkFormat(TextureFormat format);
		std::vector<VkFormat> ToVkFormats(const std::vector<TextureFormat>& formats);
		VkImageUsageFlags ToVkImageUsageFlags(Flags usage_flags);

		VkImageViewType ToVkViewType(Flags dimension, uint32_t num_layers = 1);

		VkSamplerAddressMode ToVkAddressMode(SamplerAddressMode address_mode);
		VkBorderColor ToVkBorderColor(SamplerBorderColor border_color);
		VkFilter ToVkFilter(SamplerFilter filter);
		VkSamplerMipmapMode ToVkSamplerMipmapMode(SamplerFilter filter);

	}
}
