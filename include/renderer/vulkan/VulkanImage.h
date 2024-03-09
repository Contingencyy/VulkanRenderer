#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"

namespace Vulkan
{

	VulkanImage CreateImage(const TextureCreateInfo& texture_info);
	void DestroyImage(const VulkanImage& image);

	void CopyFromBuffer(VulkanCommandBuffer& command_buffer, VulkanBuffer& src_buffer, uint64_t src_offset, VulkanImage& dst_image, uint32_t dst_width, uint32_t dst_height);
	void GenerateMips(VulkanCommandBuffer& command_buffer, VulkanImage& image);

	void TransitionImageLayout(VulkanCommandBuffer& command_buffer, const VulkanImageLayoutTransition& layout_info);
	void TransitionImageLayouts(VulkanCommandBuffer& command_buffers, const std::vector<VulkanImageLayoutTransition> layout_infos);

	uint64_t GetImageAlign(VulkanImage image);

}
