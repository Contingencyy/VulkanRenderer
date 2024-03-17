#pragma once

namespace Vulkan
{

	struct VulkanInstance
	{
		std::vector<const char*> extensions =
		{
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
			VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
			VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
			//VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
			//VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
		};

		std::set<int32_t> ignored_debug_messages =
		{
			// NOTE: Timeline semaphores are not supported by the sync validation at the time of writing this
			// Validation Error: [ VUID-vkResetCommandBuffer-commandBuffer-00045 ]
			511214570,
			// Validation Error: [ VUID-vkFreeCommandBuffers-pCommandBuffers-00047 ]
			448332540
		};

		VkInstance instance = VK_NULL_HANDLE;
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		uint32_t current_frame_index = 0;
		uint32_t last_finished_frame = 0;

		struct DeviceProperties
		{
			uint32_t max_anisotropy;
			uint32_t descriptor_buffer_offset_alignment;
		} device_props;

		struct DescriptorSizes
		{
			size_t uniform_buffer = 0;
			size_t storage_buffer = 0;
			size_t storage_image = 0;
			size_t sampled_image = 0;
			size_t sampler = 0;
		} descriptor_sizes;

		struct Swapchain
		{
			VkSurfaceKHR surface = VK_NULL_HANDLE;
			VkSwapchainKHR swapchain = VK_NULL_HANDLE;
			VkFormat format = VK_FORMAT_UNDEFINED;
			VkExtent2D extent = { 0, 0 };
			uint32_t current_image = 0;

			VkPresentModeKHR desired_present_mode = VK_PRESENT_MODE_FIFO_KHR;
			bool vsync_enabled = true;

			std::vector<VulkanImage> images;
			std::vector<VulkanFence> image_available_fences;
		} swapchain;

		struct Queues
		{
			//VulkanCommandQueue present;
			VulkanCommandQueue graphics_compute;
			VulkanCommandQueue transfer;
		} queues;

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

		struct ImGui
		{
			VkDescriptorPool descriptor_pool;
		} imgui;
	};

	extern VulkanInstance vk_inst;

}
