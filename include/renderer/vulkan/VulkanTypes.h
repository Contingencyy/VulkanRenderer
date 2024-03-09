#pragma once
#include "renderer/vulkan/VulkanIncludes.h"

struct VulkanCommandBuffer
{
	VkCommandBuffer vk_command_buffer = VK_NULL_HANDLE;
};

enum VulkanFenceType
{
	VULKAN_FENCE_TYPE_BINARY,
	VULKAN_FENCE_TYPE_TIMELINE,
	VULKAN_FENCE_TYPE_NUM_TYPES
};

struct VulkanFence
{
	VulkanFenceType type = VULKAN_FENCE_TYPE_NUM_TYPES;
	VkSemaphore vk_semaphore = VK_NULL_HANDLE;
	uint64_t fence_value = 0;
};

struct VulkanMemory
{
	VkDeviceMemory vk_device_memory = VK_NULL_HANDLE;
	VkMemoryPropertyFlags vk_memory_flags = VK_MEMORY_PROPERTY_FLAG_BITS_MAX_ENUM;
	uint32_t vk_memory_index = 0;
};

struct VulkanBuffer
{
	VkBuffer vk_buffer = VK_NULL_HANDLE;
	VkBufferUsageFlags vk_usage_flags = VK_BUFFER_USAGE_FLAG_BITS_MAX_ENUM;
	VulkanMemory memory;
	uint64_t size_in_bytes = 0;

	// TODO: We can add offsets into the actual buffer here once we have a GPU memory allocator
};

struct VulkanImage
{
	VkImage vk_image = VK_NULL_HANDLE;
	VulkanMemory memory;
	VkFormat vk_format = VK_FORMAT_UNDEFINED;

	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t depth = 0;

	uint32_t num_mips = 0;
	uint32_t num_layers = 0;

	// TODO: We can add offsets into the actual buffer here once we have a GPU memory allocator
};

struct VulkanImageView
{
	VulkanImage image;
	VkImageView vk_image_view = VK_NULL_HANDLE;

	uint32_t base_mip = 0;
	uint32_t num_mips = UINT32_MAX;
	uint32_t base_layer = 0;
	uint32_t num_layers = UINT32_MAX;
};

struct VulkanImageLayoutTransition
{
	VulkanImage image;
	VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	uint32_t base_mip = 0;
	uint32_t num_mips = UINT32_MAX;
	uint32_t base_layer = 0;
	uint32_t num_layers = UINT32_MAX;
};

struct VulkanSampler
{
	VkSampler vk_sampler = VK_NULL_HANDLE;
};
