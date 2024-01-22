#include "renderer/Texture.h"
#include "renderer/Buffer.h"
#include "renderer/VulkanResourceTracker.h"

#include "imgui/imgui_impl_vulkan.h"

#include <vector>

std::unique_ptr<Texture> Texture::Create(const TextureCreateInfo& create_info)
{
	return std::unique_ptr<Texture>(new Texture(create_info));
}

void TextureView::WriteDescriptorInfo(VkDescriptorType type, VkImageLayout layout, uint32_t descriptor_offset)
{
	// Allocate a descriptor first before writing to it, if descriptor allocation is null
	if (descriptor.IsNull())
	{
		descriptor = Vulkan::AllocateDescriptors(type);
	}

	VkDescriptorGetInfoEXT descriptor_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
	descriptor_info.type = descriptor.GetType();

	VkDescriptorImageInfo image_descriptor_info = {};
	image_descriptor_info.imageView = view;
	image_descriptor_info.imageLayout = layout;
	image_descriptor_info.sampler = VK_NULL_HANDLE;

	if (descriptor_info.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
	{
		descriptor_info.data.pStorageImage = &image_descriptor_info;
	}
	else if (descriptor_info.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
	{
		descriptor_info.data.pSampledImage = &image_descriptor_info;
	}

	descriptor.WriteDescriptor(descriptor_info, descriptor_offset);
}

void TextureView::TransitionLayout(VkCommandBuffer command_buffer, VkImageLayout new_layout) const
{
	VkImageMemoryBarrier2 barrier = VulkanResourceTracker::ImageMemoryBarrier(texture->GetVkImage(), new_layout,
		create_info.base_mip, create_info.num_mips, create_info.base_layer, create_info.num_layers);

	Vulkan::CmdImageMemoryBarrier(command_buffer, 1, &barrier);
}

void TextureView::TransitionLayoutImmediate(VkImageLayout new_layout) const
{
	VkImageMemoryBarrier2 barrier = VulkanResourceTracker::ImageMemoryBarrier(texture->GetVkImage(), new_layout,
		create_info.base_mip, create_info.num_mips, create_info.base_layer, create_info.num_layers);

	Vulkan::ImageMemoryBarrierImmediate(1, &barrier);
}

VkImageLayout TextureView::GetLayout() const
{
	return VulkanResourceTracker::GetImageLayout(texture->GetVkImage(),
		create_info.base_mip, create_info.num_mips, create_info.base_layer, create_info.num_layers);
}

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
	case TEXTURE_FORMAT_RG16_SFLOAT:
		return VK_FORMAT_R16G16_SFLOAT;
	case TEXTURE_FORMAT_D32_SFLOAT:
		return VK_FORMAT_D32_SFLOAT;
	}
}

static VkImageUsageFlags ToVkImageUsageFlags(Flags usage_flags)
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

static VkImageViewType ToVkViewType(Flags dimension, uint32_t num_layers = 1)
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

Texture::Texture(const TextureCreateInfo& create_info)
	: m_create_info(create_info)
{
	// TODO: Image tiling could be deduced inside CreateImage, because we want to use OPTIMAL always unless not supported by the image format
	m_vk_image = Vulkan::CreateImage(create_info.width, create_info.height, ToVkFormat(create_info.format), VK_IMAGE_TILING_OPTIMAL, ToVkImageUsageFlags(create_info.usage_flags),
		create_info.num_mips, create_info.num_layers, create_info.dimension == TEXTURE_DIMENSION_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0);
	m_vk_device_memory = Vulkan::AllocateDeviceMemory(m_vk_image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	Vulkan::DebugNameObject((uint64_t)m_vk_image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, create_info.name.c_str());
	Vulkan::DebugNameObject((uint64_t)m_vk_device_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, create_info.name.c_str());
}

Texture::~Texture()
{
	for (auto& view : m_views)
	{
		if (!view.second.descriptor.IsNull())
			Vulkan::FreeDescriptors(view.second.descriptor);

		Vulkan::DestroyImageView(view.second.view);
	}

	Vulkan::FreeDeviceMemory(m_vk_device_memory);
	Vulkan::DestroyImage(m_vk_image);
}

void Texture::CopyFromBuffer(VkCommandBuffer command_buffer, const Buffer& src_buffer, VkDeviceSize src_offset) const
{
	VkBufferImageCopy2 buffer_image_copy = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2 };
	buffer_image_copy.bufferOffset = src_offset;
	buffer_image_copy.bufferImageHeight = 0;
	buffer_image_copy.bufferRowLength = 0;

	buffer_image_copy.imageExtent = { m_create_info.width, m_create_info.height, 1 };
	buffer_image_copy.imageOffset = { 0, 0, 0 };

	buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	buffer_image_copy.imageSubresource.mipLevel = 0;
	buffer_image_copy.imageSubresource.baseArrayLayer = 0;
	buffer_image_copy.imageSubresource.layerCount = 1;

	VkCopyBufferToImageInfo2 copy_buffer_image_info = { VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2 };
	copy_buffer_image_info.srcBuffer = src_buffer.GetVkBuffer();
	copy_buffer_image_info.dstImage = m_vk_image;
	copy_buffer_image_info.dstImageLayout = VulkanResourceTracker::GetImageLayout(m_vk_image);
	copy_buffer_image_info.regionCount = 1;
	copy_buffer_image_info.pRegions = &buffer_image_copy;

	vkCmdCopyBufferToImage2(command_buffer, &copy_buffer_image_info);
}

void Texture::CopyFromBufferImmediate(const Buffer& src_buffer, VkDeviceSize src_offset) const
{
	VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();
	CopyFromBuffer(command_buffer, src_buffer, src_offset);
	Vulkan::EndImmediateCommand(command_buffer);
}

void Texture::TransitionLayout(VkCommandBuffer command_buffer, VkImageLayout new_layout,
	uint32_t base_mip, uint32_t num_mips, uint32_t base_layer, uint32_t num_layers) const
{
	if (num_mips == UINT32_MAX)
		num_mips = m_create_info.num_mips;
	if (num_layers == UINT32_MAX)
		num_layers = m_create_info.num_layers;

	VkImageMemoryBarrier2 image_memory_barrier = VulkanResourceTracker::ImageMemoryBarrier(
		m_vk_image, new_layout,
		base_mip, num_mips, base_layer, num_layers
	);

	Vulkan::CmdImageMemoryBarrier(command_buffer, 1, &image_memory_barrier);
}

void Texture::TransitionLayoutImmediate(VkImageLayout new_layout, uint32_t base_mip, uint32_t num_mips, uint32_t base_layer, uint32_t num_layers) const
{
	VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();
	TransitionLayout(command_buffer, new_layout, base_mip, num_mips, base_layer, num_layers);
	Vulkan::EndImmediateCommand(command_buffer);
}

void Texture::AppendToChain(std::unique_ptr<Texture>&& texture)
{
	m_chainned_textures.push_back(std::move(texture));
}

Texture& Texture::GetChainned(uint32_t index)
{
	VK_ASSERT(index < m_chainned_textures.size());
	return *m_chainned_textures[index].get();
}

VkFormat Texture::GetVkFormat() const
{
	return ToVkFormat(m_create_info.format);
}

TextureView* Texture::GetView(const TextureViewCreateInfo& view_info)
{
	TextureViewCreateInfo view_info_key = view_info;

	if (view_info.type == VK_IMAGE_VIEW_TYPE_MAX_ENUM)
	{
		view_info_key.type = ToVkViewType(m_create_info.dimension);
	}
	if (view_info.num_mips == UINT32_MAX)
	{
		view_info_key.num_mips = m_create_info.num_mips;
	}
	if (view_info.num_layers == UINT32_MAX)
	{
		view_info_key.num_layers = m_create_info.num_layers;
	}

	if (m_views.find(view_info_key) == m_views.end())
	{
		// This view does not exist, so we need to create it first
		m_views.emplace(
			view_info_key,
			TextureView{
				.texture = this,
				.view = Vulkan::CreateImageView(m_vk_image, view_info_key.type, ToVkFormat(m_create_info.format),
					view_info_key.base_mip, view_info_key.num_mips, view_info_key.base_layer, view_info_key.num_layers),
				.format = ToVkFormat(m_create_info.format),
				.create_info = view_info_key
			}
		);
	}
	
	return &m_views.at(view_info_key);
}
