#pragma once
#include "renderer/DescriptorAllocation.h"

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
		uint8_t* ptr = nullptr;

		VkDeviceSize size = 0;
		VkDeviceSize offset = 0;
	};

	struct Image
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkFormat format = VK_FORMAT_UNDEFINED;
	};

	struct ImageView
	{
		Image* image = nullptr;
		VkImageView view = VK_NULL_HANDLE;
		VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

		uint32_t base_mip = 0;
		uint32_t num_mips = 1;
		uint32_t base_layer = 0;
		uint32_t num_layers = 1;
	};

	void Init(::GLFWwindow* window);
	void Exit();
	
	bool SwapChainAcquireNextImage();
	bool SwapChainPresent(const std::vector<VkSemaphore>& signal_semaphores);
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
		VkMemoryPropertyFlags memory_flags, Image& image, uint32_t num_mips = 1, uint32_t array_layers = 1, VkImageCreateFlags create_flags = 0);
	void CreateImageCube(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
		VkMemoryPropertyFlags memory_flags, Image& image, uint32_t num_mips = 1);
	void CreateImageView(VkImageViewType view_type, VkImageAspectFlags aspect_flags, Image* image, ImageView& view,
		uint32_t base_mip = 0, uint32_t num_mips = 1, uint32_t base_layer = 0, uint32_t num_layers = 1);
	void GenerateMips(uint32_t width, uint32_t height, uint32_t num_mips, VkFormat format, Image& image);

	void DestroyImage(const Image& image);
	void DestroyImageView(const ImageView& view);

	void CopyBufferToImage(const Buffer& src_buffer, const Image& dst_image, uint32_t width, uint32_t height, VkDeviceSize src_offset = 0);

	VkFormat FindDepthFormat();
	uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags mem_properties);

	VkCommandBuffer BeginImmediateCommand();
	void EndImmediateCommand(VkCommandBuffer command_buffer);

	VkBufferMemoryBarrier2 BufferMemoryBarrier(Buffer& buffer, VkAccessFlags2 src_access, VkPipelineStageFlags2 src_stage, VkAccessFlags2 dst_access, VkPipelineStageFlags2 dst_stage);
	void CmdBufferMemoryBarrier(VkCommandBuffer command_buffer, Buffer& buffer, VkAccessFlags2 src_access, VkPipelineStageFlags2 src_stage, VkAccessFlags2 dst_access, VkPipelineStageFlags2 dst_stage);
	void CmdBufferMemoryBarriers(VkCommandBuffer command_buffer, const std::vector<VkBufferMemoryBarrier2>& buffer_barriers);
	void BufferMemoryBarrierImmediate(Buffer& buffer, VkAccessFlags2 src_access, VkPipelineStageFlags2 src_stage, VkAccessFlags2 dst_access, VkPipelineStageFlags2 dst_stage);

	VkImageMemoryBarrier2 ImageMemoryBarrier(ImageView& view, VkImageLayout new_layout,
		uint32_t base_mip_level = 0, uint32_t num_mips = 1, uint32_t base_array_layer = 0, uint32_t layer_count = 1);
	void CmdTransitionImageLayout(VkCommandBuffer command_buffer, ImageView& view, VkImageLayout new_layout);
	void CmdTransitionImageLayouts(VkCommandBuffer command_buffer, const std::vector<VkImageMemoryBarrier2>& image_barriers);
	void TransitionImageLayoutImmediate(ImageView& view, VkImageLayout new_layout, uint32_t num_mips = 1);

	VkMemoryBarrier2 MemoryBarrier(VkAccessFlags2 src_access, VkPipelineStageFlags2 src_stage, VkAccessFlags2 dst_access, VkPipelineStageFlags2 dst_stage);
	void CmdMemoryBarrier(VkCommandBuffer command_buffer, const VkMemoryBarrier2& memory_barrier);
	void CmdMemoryBarriers(VkCommandBuffer command_buffer, const std::vector<VkMemoryBarrier2>& memory_barriers);

	VkMemoryBarrier2 ExecutionBarrier(VkPipelineStageFlags2 src_stage, VkPipelineStageFlags2 dst_stage);
	void CmdExecutionBarrier(VkCommandBuffer command_buffer, const VkMemoryBarrier2& memory_barrier);

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
		//VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
		//VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
		//VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME
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
	} pFunc;
};

extern VulkanInstance vk_inst;
