#include "Precomp.h"
#include "renderer/vulkan/VulkanUtils.h"
#include "renderer/vulkan/VulkanInstance.h"

namespace Vulkan
{

	void VkCheckResult(VkResult result)
	{
		if (result != VK_SUCCESS)
		{
			VK_EXCEPT("Vulkan", string_VkResult(result));
		}
	}

	void DebugNameObject(uint64_t object, VkDebugReportObjectTypeEXT object_type, const std::string& debug_name)
	{
#ifdef _DEBUG
		VkDebugMarkerObjectNameInfoEXT debug_name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		debug_name_info.objectType = object_type;
		debug_name_info.object = object;
		debug_name_info.pObjectName = debug_name.c_str();

		vk_inst.pFunc.debug_marker_set_object_name_ext(vk_inst.device, &debug_name_info);
#endif
	}

	namespace Util
	{

		VkDeviceAddress GetBufferDeviceAddress(const VulkanBuffer& buffer)
		{
			VkBufferDeviceAddressInfo address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			address_info.buffer = buffer.vk_buffer;

			return vkGetBufferDeviceAddress(vk_inst.device, &address_info);
		}

		VkDeviceAddress GetAccelerationStructureDeviceAddress(VkAccelerationStructureKHR acceleration_structure)
		{
			VkAccelerationStructureDeviceAddressInfoKHR as_device_address_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
			as_device_address_info.accelerationStructure = acceleration_structure;

			return vk_inst.pFunc.raytracing.get_acceleration_structure_device_address(vk_inst.device, &as_device_address_info);
		}

		VkMemoryPropertyFlags ToVkMemoryPropertyFlags(Flags memory_flags)
		{
			VkMemoryPropertyFlags vk_mem_property_flags = 0;

			if (memory_flags & GPU_MEMORY_DEVICE_LOCAL)
				vk_mem_property_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			if (memory_flags & GPU_MEMORY_HOST_VISIBLE)
				vk_mem_property_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			if (memory_flags & GPU_MEMORY_HOST_COHERENT)
				vk_mem_property_flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

			return vk_mem_property_flags;
		}

		uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags mem_properties)
		{
			VkPhysicalDeviceMemoryProperties device_mem_properties = {};
			vkGetPhysicalDeviceMemoryProperties(vk_inst.physical_device, &device_mem_properties);

			for (uint32_t i = 0; i < device_mem_properties.memoryTypeCount; ++i)
			{
				if (type_filter & (1 << i) &&
					(device_mem_properties.memoryTypes[i].propertyFlags & mem_properties) == mem_properties)
				{
					return i;
				}
			}

			VK_EXCEPT("Vulkan", "Failed to find suitable memory type");
		}

		VkBufferUsageFlags ToVkBufferUsageFlags(Flags usage_flags)
		{
			VkBufferUsageFlags vk_usage_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

			if (usage_flags & BUFFER_USAGE_STAGING)
				vk_usage_flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			if (usage_flags & BUFFER_USAGE_UNIFORM)
				vk_usage_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			if (usage_flags & BUFFER_USAGE_VERTEX)
				vk_usage_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			if (usage_flags & BUFFER_USAGE_INDEX)
				vk_usage_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			if ((usage_flags & BUFFER_USAGE_READ_ONLY) || (usage_flags & BUFFER_USAGE_READ_WRITE))
				vk_usage_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			if (usage_flags & BUFFER_USAGE_COPY_SRC)
				vk_usage_flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			if (usage_flags & BUFFER_USAGE_COPY_DST)
				vk_usage_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			if (usage_flags & BUFFER_USAGE_RAYTRACING_ACCELERATION_STRUCTURE)
				vk_usage_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
			if (usage_flags & BUFFER_USAGE_RAYTRACING_SCRATCH)
				vk_usage_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			if (usage_flags & BUFFER_USAGE_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_INPUT)
				vk_usage_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			if (usage_flags & BUFFER_USAGE_RESOURCE_DESCRIPTORS)
				vk_usage_flags |= VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
			if (usage_flags & BUFFER_USAGE_SAMPLER_DESCRIPTORS)
				vk_usage_flags |= VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

			return vk_usage_flags;
		}

		VkFormat ToVkFormat(TextureFormat format)
		{
			switch (format)
			{
			case TEXTURE_FORMAT_UNDEFINED:
				return VK_FORMAT_UNDEFINED;
			case TEXTURE_FORMAT_RGBA8_UNORM:
				return VK_FORMAT_R8G8B8A8_UNORM;
			case TEXTURE_FORMAT_RGBA8_SRGB:
				return VK_FORMAT_R8G8B8A8_SRGB;
			case TEXTURE_FORMAT_RGBA16_SFLOAT:
				return VK_FORMAT_R16G16B16A16_SFLOAT;
			case TEXTURE_FORMAT_RGBA32_SFLOAT:
				return VK_FORMAT_R32G32B32A32_SFLOAT;
			case TEXTURE_FORMAT_RG16_SFLOAT:
				return VK_FORMAT_R16G16_SFLOAT;
			case TEXTURE_FORMAT_D32_SFLOAT:
				return VK_FORMAT_D32_SFLOAT;
			}
		}

		std::vector<VkFormat> ToVkFormats(const std::vector<TextureFormat>& formats)
		{
			std::vector<VkFormat> vk_formats;

			for (const auto& format : formats)
			{
				vk_formats.push_back(ToVkFormat(format));
			}

			return vk_formats;
		}

		VkImageUsageFlags ToVkImageUsageFlags(Flags usage_flags)
		{
			VkImageUsageFlags vk_usage_flags = 0;

			if (usage_flags & TEXTURE_USAGE_RENDER_TARGET)
				vk_usage_flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			if ((usage_flags & TEXTURE_USAGE_DEPTH_TARGET) || (usage_flags & TEXTURE_USAGE_DEPTH_STENCIL_TARGET))
				vk_usage_flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			if (usage_flags & TEXTURE_USAGE_SAMPLED)
				vk_usage_flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
			if ((usage_flags & TEXTURE_USAGE_READ_ONLY) || (usage_flags & TEXTURE_USAGE_READ_WRITE))
				vk_usage_flags |= VK_IMAGE_USAGE_STORAGE_BIT;
			if (usage_flags & TEXTURE_USAGE_COPY_SRC)
				vk_usage_flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			if (usage_flags & TEXTURE_USAGE_COPY_DST)
				vk_usage_flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

			return vk_usage_flags;
		}

		VkImageViewType ToVkViewType(Flags dimension, uint32_t num_layers)
		{
			if (num_layers == 1)
			{
				switch (dimension)
				{
				case TEXTURE_DIMENSION_2D:
					return VK_IMAGE_VIEW_TYPE_2D;
				case TEXTURE_DIMENSION_CUBE:
					return VK_IMAGE_VIEW_TYPE_CUBE;
				}
			}
			else
			{
				switch (dimension)
				{
				case TEXTURE_DIMENSION_2D:
					return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
				case TEXTURE_DIMENSION_CUBE:
					return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
				}
			}
		}

		VkSamplerAddressMode ToVkAddressMode(SamplerAddressMode address_mode)
		{
			switch (address_mode)
			{
			case SAMPLER_ADDRESS_MODE_REPEAT:
				return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			case SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
				return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			case SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
				return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
				return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			case SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
				return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
			}
		}

		VkBorderColor ToVkBorderColor(SamplerBorderColor border_color)
		{
			switch (border_color)
			{
			case SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
				return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			case SAMPLER_BORDER_COLOR_INT_TRANSPARENT_BLACK:
				return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
			case SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
				return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			case SAMPLER_BORDER_COLOR_INT_OPAQUE_BLACK:
				return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			case SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
				return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			case SAMPLER_BORDER_COLOR_INT_OPAQUE_WHITE:
				return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
			}
		}

		VkFilter ToVkFilter(SamplerFilter filter)
		{
			switch (filter)
			{
			case SAMPLER_FILTER_NEAREST:
				return VK_FILTER_NEAREST;
			case SAMPLER_FILTER_LINEAR:
				return VK_FILTER_LINEAR;
			case SAMPLER_FILTER_CUBIC:
				return VK_FILTER_CUBIC_EXT;
			}
		}

		VkSamplerMipmapMode ToVkSamplerMipmapMode(SamplerFilter filter)
		{
			switch (filter)
			{
			case SAMPLER_FILTER_NEAREST:
				return VK_SAMPLER_MIPMAP_MODE_NEAREST;
			case SAMPLER_FILTER_LINEAR:
				return VK_SAMPLER_MIPMAP_MODE_LINEAR;
			case SAMPLER_FILTER_CUBIC:
				VK_ASSERT(false && "Cannot enable trilinear filtering for mipmap sampler mode");
			}
		}

		VkPipelineBindPoint ToVkPipelineBindPoint(VulkanPipelineType type)
		{
			switch (type)
			{
			case VULKAN_PIPELINE_TYPE_GRAPHICS:
				return VK_PIPELINE_BIND_POINT_GRAPHICS;
			case VULKAN_PIPELINE_TYPE_COMPUTE:
				return VK_PIPELINE_BIND_POINT_COMPUTE;
			default:
				VK_EXCEPT("Command::ToVkPipelineBindPoint", "Invalid VulkanPipelineType");
			}
		}

		VkIndexType ToVkIndexType(uint32_t index_byte_size)
		{
			switch (index_byte_size)
			{
			case 2:
				return VK_INDEX_TYPE_UINT16;
			case 4:
				return VK_INDEX_TYPE_UINT32;
			default:
				VK_EXCEPT("Vulkan::Command::ToVkIndexType", "Index byte size is not supported");
			}
		}

	}
}
