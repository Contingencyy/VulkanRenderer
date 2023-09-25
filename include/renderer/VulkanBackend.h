#pragma once

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include <vector>

inline void VkCheckResult(VkResult result);

typedef struct GLFWwindow;

struct VulkanInstance
{
	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
#ifdef _DEBUG
	static constexpr bool ENABLE_VALIDATION_LAYERS = true;
#else
	static constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

	::GLFWwindow* window;

	std::vector<const char*> extensions =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
		VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME
	};

	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;

	struct Pfn
	{
		PFN_vkGetDescriptorEXT get_descriptor_ext;
		PFN_vkGetDescriptorSetLayoutSizeEXT get_descriptor_set_layout_size_ext;
		PFN_vkGetDescriptorSetLayoutBindingOffsetEXT get_descriptor_set_layout_binding_offset_ext;
		PFN_vkCmdSetDescriptorBufferOffsetsEXT cmd_set_descriptor_buffer_offsets_ext;
		PFN_vkCmdBindDescriptorBuffersEXT cmd_bind_descriptor_buffers_ext;
	} pFunc;

	struct Debug
	{
		std::vector<const char*> validation_layers = { "VK_LAYER_KHRONOS_validation" };
		VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
	} debug;

	struct DescriptorSizes
	{
		size_t uniform_buffer;
		size_t storage_buffer;
		size_t combined_image_sampler;
	} descriptor_sizes;

	struct Swapchain
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkExtent2D extent = { 0, 0 };
		std::vector<VkImage> images;
		std::vector<VkImageView> image_views;
		std::vector<VkFramebuffer> framebuffers;
		// TODO: Depth images will be removed from swapchain, swapchain color buffers will only be copied to eventually
		VkImage depth_image = VK_NULL_HANDLE;
		VkDeviceMemory depth_image_memory = VK_NULL_HANDLE;
		VkImageView depth_image_view = VK_NULL_HANDLE;
	} swapchain;

	struct QueueIndices
	{
		uint32_t present = ~0u;
		uint32_t graphics = ~0u;
		//uint32_t compute = ~0u;
		//uint32_t transfer = ~0u;
	} queue_indices;

	struct Queues
	{
		VkQueue present = VK_NULL_HANDLE;
		VkQueue graphics = VK_NULL_HANDLE;
		//VkQueue compute = VK_NULL_HANDLE;
		//VkQueue transfer = VK_NULL_HANDLE;
	} queues;

	struct CommandPools
	{
		VkCommandPool graphics = VK_NULL_HANDLE;
		//VkCommandPool compute = VK_NULL_HANDLE;
		//VkCommandPool transfer = VK_NULL_HANDLE;
	} cmd_pools;
};

extern VulkanInstance vk_inst;

namespace VulkanBackend
{

	void Init(::GLFWwindow* window);
	void Exit();
	
	void RecreateSwapChain();

	void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags, VkBuffer& buffer, VkDeviceMemory& device_memory);
	void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
		VkMemoryPropertyFlags memory_flags, VkImage& image, VkDeviceMemory& image_memory);
	void CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView& image_view);
	VkFormat FindDepthFormat();

	VkCommandBuffer BeginSingleTimeCommands();
	void EndSingleTimeCommands(VkCommandBuffer command_buffer);
	void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);

}
