#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"

namespace Vulkan
{

	VulkanBuffer CreateBuffer(const BufferCreateInfo& buffer_info);
	void DestroyBuffer(const VulkanBuffer& buffer);

	void CopyBuffers(VulkanCommandBuffer& command_buffer, VulkanBuffer& src_buffer, uint64_t src_offset, VulkanBuffer& dst_buffer, uint64_t dst_offset, uint64_t num_bytes);

}
