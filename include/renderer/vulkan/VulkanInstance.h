#pragma once

namespace Vulkan
{

	struct VulkanInstance
	{
		static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

		::GLFWwindow* window;

		std::vector<const char*> extensions =
		{
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
			VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
			VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
			//VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
			//VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
		};

		VkInstance instance = VK_NULL_HANDLE;
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		uint32_t current_frame = 0;
		uint32_t last_finished_frame = 0;

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

			VkPresentModeKHR desired_present_mode = VK_PRESENT_MODE_FIFO_KHR;
			bool vsync_enabled = true;

			std::vector<VulkanImage> images;
			std::vector<VkSemaphore> image_available_semaphores;
		} swapchain;

		struct Queues
		{
			std::unique_ptr<CommandQueue> graphics_compute;
			std::unique_ptr<CommandQueue> transfer;
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
