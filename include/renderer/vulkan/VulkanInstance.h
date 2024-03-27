#pragma once

typedef struct GLFWwindow;

namespace Vulkan
{

	struct VulkanInstance
	{
		::GLFWwindow* glfw_window = nullptr;

		std::vector<const char*> extensions =
		{
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
			VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
			VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
			/*VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
			VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
			VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
			VK_EXT_SHADER_OBJECT_EXTENSION_NAME,*/
		};

		// A set of debug messages to ignore, useful for known bugs in the validation layer(s)
		std::set<int32_t> ignored_debug_messages =
		{
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

			VkDeviceSize min_imported_host_pointer_alignment = 0;
		} debug;

		struct Pfn
		{
			PFN_vkGetDescriptorEXT get_descriptor_ext;
			PFN_vkGetDescriptorSetLayoutSizeEXT get_descriptor_set_layout_size_ext;
			PFN_vkGetDescriptorSetLayoutBindingOffsetEXT get_descriptor_set_layout_binding_offset_ext;
			PFN_vkCmdSetDescriptorBufferOffsetsEXT cmd_set_descriptor_buffer_offsets_ext;
			PFN_vkCmdBindDescriptorBuffersEXT cmd_bind_descriptor_buffers_ext;
			PFN_vkDebugMarkerSetObjectNameEXT debug_marker_set_object_name_ext;

			struct Raytracing
			{
				PFN_vkCmdBuildAccelerationStructuresKHR cmd_build_acceleration_structures;
				PFN_vkCreateAccelerationStructureKHR create_acceleration_structure;
				PFN_vkDestroyAccelerationStructureKHR destroy_acceleration_structure;
				PFN_vkGetAccelerationStructureBuildSizesKHR get_acceleration_structure_build_sizes;
				PFN_vkGetAccelerationStructureDeviceAddressKHR get_acceleration_structure_device_address;
			} raytracing;
		} pFunc;

		struct ImGui
		{
			VkDescriptorPool descriptor_pool;
		} imgui;
	};

	extern VulkanInstance vk_inst;

}
