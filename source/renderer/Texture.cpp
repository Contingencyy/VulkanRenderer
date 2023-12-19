#include "renderer/Texture.h"

#include <vector>

static VkFormat ToVkFormat(TextureFormat format)
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
	}
}

static bool IsHDRFormat(TextureFormat format)
{
	switch (format)
	{
	case TEXTURE_FORMAT_RGBA8_UNORM:
	case TEXTURE_FORMAT_RGBA8_SRGB:
		return false;
	case TEXTURE_FORMAT_RGBA16_SFLOAT:
	case TEXTURE_FORMAT_RGBA32_SFLOAT:
		return true;
	}
}

static VkImageUsageFlags ToVkImageUsageFlags(TextureUsageFlags usage_flags)
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

	return vk_usage_flags;
}

Texture::Texture(const TextureCreateInfo& create_info)
	: m_create_info(create_info)
{
	// TODO: Image tiling could be deduced inside CreateImage, because we want to use OPTIMAL always unless not supported by the image format
	m_vk_image = Vulkan::CreateImage(create_info.width, create_info.height, ToVkFormat(create_info.format), VK_IMAGE_TILING_OPTIMAL, ToVkImageUsageFlags(create_info.usage_flags),
		create_info.num_mips, create_info.num_layers, create_info.dimension == TEXTURE_DIMENSION_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0);
	m_vk_device_memory = Vulkan::AllocateDeviceMemory(m_vk_image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

#ifdef _DEBUG
	Vulkan::DebugNameObject((uint64_t)m_vk_image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, create_info.name.c_str());
	Vulkan::DebugNameObject((uint64_t)m_vk_device_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, create_info.name.c_str());
#endif
}

Texture::~Texture()
{
	Vulkan::FreeDeviceMemory(m_vk_device_memory);
	Vulkan::DestroyImage(m_vk_image);
}


//void CreateImageCube(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags memory_flags, Image& image, uint32_t num_mips)
//{
//	CreateImage(width, height, format, tiling, usage, memory_flags, image, num_mips, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
//}
//
//void CreateImageView(VkImageViewType view_type, VkImageAspectFlags aspect_flags, Image* image, ImageView& view,
//	uint32_t base_mip, uint32_t num_mips, uint32_t base_layer, uint32_t num_layers)
//{
//	VkImageViewCreateInfo view_info = {};
//	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
//	view_info.image = image->image;
//	view_info.viewType = view_type;
//	view_info.format = image->format;
//	view_info.subresourceRange.aspectMask = aspect_flags;
//	view_info.subresourceRange.baseMipLevel = base_mip;
//	view_info.subresourceRange.levelCount = num_mips;
//	view_info.subresourceRange.baseArrayLayer = base_layer;
//	view_info.subresourceRange.layerCount = num_layers;
//
//	view.image = image;
//	view.view_type = view_type;
//	view.base_mip = base_mip;
//	view.num_mips = num_mips;
//	view.base_layer = base_layer;
//	view.num_layers = num_layers;
//	view.layout = VK_IMAGE_LAYOUT_UNDEFINED;
//	VkCheckResult(vkCreateImageView(vk_inst.device, &view_info, nullptr, &view.view));
//}


//void CopyBufferToImage(const Buffer& src_buffer, const Image& dst_image, uint32_t width, uint32_t height, VkDeviceSize src_offset = 0);
