#pragma once
#include "renderer/VulkanIncludes.h"
#include "renderer/DescriptorBuffer.h"
#include "renderer/DescriptorAllocation.h"
#include "Shared.glsl.h"

typedef struct GLFWwindow;

inline void VkCheckResult(VkResult result);

namespace Vulkan
{

	void Init(::GLFWwindow* window);
	void Exit();
	
	VkResult SwapChainAcquireNextImage();
	VkResult SwapChainPresent(const std::vector<VkSemaphore>& wait_semaphores);
	void RecreateSwapChain();
	void CopyToSwapchain(VkCommandBuffer command_buffer, VkImage src_image);

	void DebugNameObject(uint64_t object, VkDebugReportObjectTypeEXT object_type, const char* debug_name);

	VkDeviceMemory AllocateDeviceMemory(VkBuffer buffer, VkMemoryPropertyFlags mem_flags);
	VkDeviceMemory AllocateDeviceMemory(VkImage image, VkMemoryPropertyFlags mem_flags);
	void FreeDeviceMemory(VkDeviceMemory device_memory);
	uint8_t* MapMemory(VkDeviceMemory device_memory, VkDeviceSize size, VkDeviceSize offset = 0);
	void UnmapMemory(VkDeviceMemory device_memory);

	DescriptorAllocation AllocateDescriptors(VkDescriptorType type, uint32_t num_descriptors = 1, uint32_t align = 0);
	void FreeDescriptors(const DescriptorAllocation& descriptors);
	std::vector<VkDescriptorSetLayout> GetDescriptorBufferLayouts();
	std::vector<VkDescriptorBufferBindingInfoEXT> GetDescriptorBufferBindingInfos();
	size_t GetDescriptorTypeSize(VkDescriptorType type);

	VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage_flags);
	void DestroyBuffer(VkBuffer buffer);

	VkImage CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
		VkImageUsageFlags usage, uint32_t num_mips, uint32_t num_layers, VkImageCreateFlags create_flags);
	void DestroyImage(VkImage image);
	void GenerateMips(VkImage image, VkFormat format, uint32_t width, uint32_t height, uint32_t num_mips);

	VkImageView CreateImageView(VkImage image, VkImageViewType view_type, VkFormat format, uint32_t base_mip = 0, uint32_t num_mips = 1, uint32_t base_layer = 0, uint32_t num_layers = 1);
	void DestroyImageView(VkImageView image_view);

	VkSampler CreateSampler(const VkSamplerCreateInfo& sampler_info);
	void DestroySampler(VkSampler sampler);

	VkFormat FindDepthFormat();
	uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags mem_properties);

	VkCommandBuffer BeginImmediateCommand();
	void EndImmediateCommand(VkCommandBuffer command_buffer);

	void CmdImageMemoryBarrier(VkCommandBuffer command_buffer, uint32_t num_barriers, const VkImageMemoryBarrier2* image_barriers);
	void ImageMemoryBarrierImmediate(uint32_t num_barriers, const VkImageMemoryBarrier2* image_barriers);

	VkPipelineLayout CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts, const std::vector<VkPushConstantRange>& push_constant_ranges);

	struct GraphicsPipelineInfo
	{
		std::vector<VkVertexInputBindingDescription> input_bindings;
		std::vector<VkVertexInputAttributeDescription> input_attributes;

		std::vector<VkFormat> color_attachment_formats;
		VkFormat depth_stencil_attachment_format = VK_FORMAT_UNDEFINED;

		const char* vs_path;
		const char* fs_path;

		bool depth_test = false;
		bool depth_write = false;
		VkCompareOp depth_func = VK_COMPARE_OP_LESS;

		VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
	};

	VkPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info, VkPipelineLayout pipeline_layout);

	struct ComputePipelineInfo
	{
		const char* cs_path;
	};

	VkPipeline CreateComputePipeline(const ComputePipelineInfo& info, VkPipelineLayout pipeline_layout);

}

struct VulkanInstance
{
	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
#ifdef _DEBUG
	static constexpr bool ENABLE_VALIDATION_LAYERS = true;
#else
	static constexpr bool ENABLE_VALIDATION_LAYERS = false;
#endif

	::GLFWwindow* window;

	std::vector<const char*> extensions =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
		VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
		//VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
		//VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
	};

	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t current_frame = 0;

	struct DeviceProperties
	{
		uint32_t max_anisotropy;
		uint32_t descriptor_buffer_offset_alignment;
	} device_props;

	struct DescriptorSizes
	{
		size_t uniform_buffer;
		size_t storage_buffer;
		size_t storage_image;
		size_t sampled_image;
		size_t sampler;
	} descriptor_sizes;

	struct Swapchain
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkExtent2D extent = { 0, 0 };
		uint32_t current_image = 0;

		std::vector<VkImage> images;
		std::vector<VkSemaphore> image_available_semaphores;
	} swapchain;

	struct QueueIndices
	{
		// NOTE: We are using a combined graphics compute queue for now..
		uint32_t present = ~0u;
		uint32_t graphics_compute = ~0u;
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
		PFN_vkDebugMarkerSetObjectNameEXT debug_marker_set_object_name_ext;
	} pFunc;
};

extern VulkanInstance vk_inst;
