#pragma once
#include "renderer/VulkanBackend.h"

namespace VulkanResourceTracker
{

	void Init();
	void Exit();

	void TrackBuffer(VkBuffer buffer);
	void RemoveBuffer(VkBuffer buffer);
	
	void TrackImage(VkImage image, VkImageLayout layout, uint32_t num_mips = 1, uint32_t num_layers = 1);
	void RemoveImage(VkImage image);

	VkImageMemoryBarrier2 ImageMemoryBarrier(VkImage image, VkImageLayout new_layout,
		uint32_t base_mip_level = 0, uint32_t num_mips = UINT32_MAX, uint32_t base_layer = 0, uint32_t num_layers = UINT32_MAX);
	void UpdateImageLayout(VkImage image, VkImageLayout new_layout,
		uint32_t base_mip = 0, uint32_t num_mips = UINT32_MAX, uint32_t base_layer = 0, uint32_t num_layers = UINT32_MAX);
	VkImageLayout GetImageLayout(VkImage image,
		uint32_t base_mip = 0, uint32_t num_mips = UINT32_MAX, uint32_t base_layer = 0, uint32_t num_layers = UINT32_MAX);

};
