#pragma once
#include "renderer/vulkan/VulkanIncludes.h"

#include <vector>

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
	VkPipelineStageFlags2 stage_flags = VK_PIPELINE_STAGE_2_NONE;
	uint64_t fence_value = 0ull;
};

enum VulkanPipelineType
{
	VULKAN_PIPELINE_TYPE_GRAPHICS,
	VULKAN_PIPELINE_TYPE_COMPUTE,
	VULKAN_PIPELINE_TYPE_NUM_TYPES
};

struct VulkanPipeline
{
	VulkanPipelineType type = VULKAN_PIPELINE_TYPE_NUM_TYPES;
	VkPipeline vk_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout vk_pipeline_layout = VK_NULL_HANDLE;
};

enum VulkanCommandBufferType
{
	VULKAN_COMMAND_BUFFER_TYPE_GRAPHICS_COMPUTE,
	VULKAN_COMMAND_BUFFER_TYPE_TRANSFER,
	VULKAN_COMMAND_BUFFER_TYPE_NUM_TYPES
};

struct VulkanCommandQueue
{
	VulkanCommandBufferType type = VULKAN_COMMAND_BUFFER_TYPE_NUM_TYPES;
	VkQueue vk_queue = VK_NULL_HANDLE;
	uint32_t queue_family_index = ~0u;

	VulkanFence fence;
};

struct VulkanCommandPool
{
	VkCommandPool vk_command_pool = VK_NULL_HANDLE;
	VulkanCommandBufferType type = VULKAN_COMMAND_BUFFER_TYPE_NUM_TYPES;
};

struct VulkanCommandBuffer
{
	VkCommandBuffer vk_command_buffer = VK_NULL_HANDLE;
	VulkanCommandBufferType type = VULKAN_COMMAND_BUFFER_TYPE_NUM_TYPES;

	VulkanPipeline pipeline_bound;

	std::vector<VulkanFence> wait_fences;
};

// NOTE: The order needs to match the DescriptorSetXYZ consts in assets/shaders/Shared.glsl.h
enum VulkanDescriptorType
{
	VULKAN_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	VULKAN_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	VULKAN_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	VULKAN_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	VULKAN_DESCRIPTOR_TYPE_SAMPLER,
	VULKAN_DESCRIPTOR_TYPE_NUM_TYPES
};

struct VulkanDescriptorAllocation
{
	VulkanDescriptorType type = VULKAN_DESCRIPTOR_TYPE_NUM_TYPES;

	uint32_t num_descriptors = 0u;
	uint32_t descriptor_size_in_bytes = 0u;
	uint32_t descriptor_offset = 0u;
	uint8_t* ptr = nullptr;
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
	VkDeviceAddress vk_device_address = 0;
	VkBufferUsageFlags vk_usage_flags = VK_BUFFER_USAGE_FLAG_BITS_MAX_ENUM;
	VulkanMemory memory;

	uint64_t size_in_bytes = 0ull;
	uint64_t offset_in_bytes = 0ull;

	// NOTE: Only used for raytracing acceleration structures
	VkAccelerationStructureKHR vk_acceleration_structure = VK_NULL_HANDLE;
};

struct VulkanImage
{
	VkImage vk_image = VK_NULL_HANDLE;
	VulkanMemory memory;
	VkFormat vk_format = VK_FORMAT_UNDEFINED;

	uint32_t width = 0u;
	uint32_t height = 0u;
	uint32_t depth = 0u;

	uint32_t num_mips = 0u;
	uint32_t num_layers = 0u;

	// TODO: We can add offsets into the actual buffer here once we have a GPU memory allocator
};

struct VulkanImageView
{
	VulkanImage image;
	VkImageView vk_image_view = VK_NULL_HANDLE;

	uint32_t base_mip = 0u;
	uint32_t num_mips = UINT32_MAX;
	uint32_t base_layer = 0u;
	uint32_t num_layers = UINT32_MAX;
};

struct VulkanImageLayoutTransition
{
	VulkanImage image;
	VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	uint32_t base_mip = 0u;
	uint32_t num_mips = UINT32_MAX;
	uint32_t base_layer = 0u;
	uint32_t num_layers = UINT32_MAX;
};

struct VulkanSampler
{
	VkSampler vk_sampler = VK_NULL_HANDLE;
	VulkanDescriptorAllocation descriptor;
};
