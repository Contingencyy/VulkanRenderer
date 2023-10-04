#pragma once
#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include <vector>

typedef struct GLFWwindow;

inline void VkCheckResult(VkResult result);

namespace Vulkan
{

	struct Buffer
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		void* ptr = nullptr;

		uint32_t num_elements = 0;
	};

	struct Image
	{
		VkImage image = VK_NULL_HANDLE;;
		VkImageView view = VK_NULL_HANDLE;;
		VkDeviceMemory memory = VK_NULL_HANDLE;;
		VkSampler sampler = VK_NULL_HANDLE;
		VkFormat format = VK_FORMAT_UNDEFINED;
	};

	void Init(::GLFWwindow* window);
	void Exit();
	
	void RecreateSwapChain();

	void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags, Buffer& buffer);
	void CreateStagingBuffer(VkDeviceSize size, Buffer& buffer);
	void CreateUniformBuffer(VkDeviceSize size, Buffer& buffer);
	void CreateDescriptorBuffer(VkDeviceSize size, Buffer& buffer);
	void CreateVertexBuffer(VkDeviceSize size, Buffer& buffer);
	void CreateIndexBuffer(VkDeviceSize size, Buffer& buffer);

	void DestroyBuffer(const Buffer& buffer);

	void WriteBuffer(void* dst_ptr, void* src_ptr, VkDeviceSize size);
	void CopyBuffer(const Buffer& src_buffer, const Buffer& dst_buffer, VkDeviceSize size, VkDeviceSize src_offset = 0, VkDeviceSize dst_offset = 0);

	void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
		VkMemoryPropertyFlags memory_flags, Image& image, uint32_t num_mips = 1);
	void CreateImageView(VkImageAspectFlags aspect_flags, Image& image, uint32_t num_mips = 1);
	void GenerateMips(uint32_t width, uint32_t height, uint32_t num_mips, VkFormat format, Image& image);

	void DestroyImage(const Image& image);

	void CopyBufferToImage(const Buffer& src_buffer, const Image& dst_image, uint32_t width, uint32_t height, VkDeviceSize src_offset = 0);
	
	void CreateFramebuffer(const std::vector<VkImageView>& image_views, VkRenderPass render_pass, uint32_t width, uint32_t height, VkFramebuffer& framebuffer);

	VkFormat FindDepthFormat();
	uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags mem_properties);

	VkCommandBuffer BeginSingleTimeCommands();
	void EndSingleTimeCommands(VkCommandBuffer command_buffer);
	void TransitionImageLayout(VkCommandBuffer command_buffer, const Image& image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t num_mips = 1);
	void TransitionImageLayout(const Image& image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t num_mips = 1);

}

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
		//VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
		//VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
		//VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME
	};

	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;

	struct DeviceProperties
	{
		uint32_t max_anisotropy;
	} device_props;

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
		VkFormat format;
		VkExtent2D extent = { 0, 0 };

		std::vector<VkImage> images;
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

	struct Debug
	{
		std::vector<const char*> validation_layers = { "VK_LAYER_KHRONOS_validation" };
		VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
	} debug;

	struct Pfn
	{
		PFN_vkGetDescriptorEXT get_descriptor_ext;
		PFN_vkGetDescriptorSetLayoutSizeEXT get_descriptor_set_layout_size_ext;
		PFN_vkGetDescriptorSetLayoutBindingOffsetEXT get_descriptor_set_layout_binding_offset_ext;
		PFN_vkCmdSetDescriptorBufferOffsetsEXT cmd_set_descriptor_buffer_offsets_ext;
		PFN_vkCmdBindDescriptorBuffersEXT cmd_bind_descriptor_buffers_ext;
	} pFunc;
};

extern VulkanInstance vk_inst;
