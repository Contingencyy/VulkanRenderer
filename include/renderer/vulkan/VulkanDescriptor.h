#pragma once
#include "renderer/vulkan/VulkanTypes.h"

namespace Vulkan
{

	namespace Descriptor
	{

		void Init();
		void Exit();

		VulkanDescriptorAllocation Allocate(VulkanDescriptorType type, uint32_t num_descriptors = 1, uint32_t frame_index = 0);
		void Free(const VulkanDescriptorAllocation& alloc, uint32_t frame_index = 0);

		void Write(const VulkanDescriptorAllocation& descriptors, const VulkanBuffer& buffer, uint32_t descriptor_offset = 0);
		void Write(const VulkanDescriptorAllocation& descriptors, const VulkanImageView& view, VkImageLayout layout, uint32_t descriptor_offset = 0);
		void Write(const VulkanDescriptorAllocation& descriptors, const VulkanSampler& sampler, uint32_t descriptor_offset = 0);

		bool IsValid(const VulkanDescriptorAllocation& alloc);

		std::vector<VkDescriptorSetLayout> GetDescriptorSetLayouts();
		void BindDescriptors(const VulkanCommandBuffer& command_buffer, const VulkanPipeline& pipeline);

	}

}
