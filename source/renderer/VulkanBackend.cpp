#include "renderer/VulkanBackend.h"
#include "Common.h"

#include "GLFW/glfw3.h"

#include <assert.h>
#include <set>
#include <algorithm>

VulkanInstance vk_inst;

void VkCheckResult(VkResult result)
{
	if (result != VK_SUCCESS)
	{
		VK_EXCEPT("Vulkan", string_VkResult(result));
	}
}

namespace Vulkan
{

	template<typename TFunc>
	static void LoadVulkanFunction(const char* func_name, TFunc& func_ptr)
	{
		func_ptr = (TFunc)vkGetInstanceProcAddr(vk_inst.instance, func_name);
		if (!func_ptr)
		{
			VK_EXCEPT("Vulkan", "Could not find function pointer for {}", func_name);
		}
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL VkDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
	{
		(void)type; (void)user_data;
		static const char* sender = "Vulkan validation layer";

		switch (severity)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			LOG_VERBOSE(sender, callback_data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			LOG_INFO(sender, callback_data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			LOG_WARN(sender, callback_data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			VK_EXCEPT(sender, callback_data->pMessage);
			break;
		}

		return VK_FALSE;
	}

	static std::vector<const char*> GetRequiredExtensions()
	{
		uint32_t glfw_extension_count = 0;
		const char** glfw_extensions;
		glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

		std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

		if (VulkanInstance::ENABLE_VALIDATION_LAYERS)
		{
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		return extensions;
	}

	static void FindQueueIndices()
	{
		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(vk_inst.physical_device, &queue_family_count, nullptr);

		std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(vk_inst.physical_device, &queue_family_count, queue_families.data());

		int i = 0;
		for (const auto& queue_family : queue_families)
		{
			// Check queue for present capabilities
			VkBool32 present_supported = false;
			VkCheckResult(vkGetPhysicalDeviceSurfaceSupportKHR(vk_inst.physical_device, i, vk_inst.swapchain.surface, &present_supported));

			if (present_supported)
			{
				vk_inst.queue_indices.present = i;
			}

			// Check queue for graphics and compute capabilities
			if ((queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT))
			{
				vk_inst.queue_indices.graphics_compute = i;
			}

			// Early-out if we found a graphics and a present queue family
			if (vk_inst.queue_indices.graphics_compute != ~0u && vk_inst.queue_indices.present != ~0u)
			{
				break;
			}

			i++;
		}

		if (vk_inst.queue_indices.present == ~0u)
		{
			VK_EXCEPT("Vulkan", "No present queue family found");
		}

		if (vk_inst.queue_indices.graphics_compute == ~0u)
		{
			VK_EXCEPT("Vulkan", "No graphics/compute queue family found");
		}
	}

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> present_modes;
	};

	static SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device)
	{
		SwapChainSupportDetails swapchain_details;
		VkCheckResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, vk_inst.swapchain.surface, &swapchain_details.capabilities));

		uint32_t format_count = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, vk_inst.swapchain.surface, &format_count, nullptr);

		if (format_count != 0)
		{
			swapchain_details.formats.resize(format_count);
			VkCheckResult(vkGetPhysicalDeviceSurfaceFormatsKHR(device, vk_inst.swapchain.surface, &format_count, swapchain_details.formats.data()));
		}

		uint32_t present_mode_count = 0;
		VkCheckResult(vkGetPhysicalDeviceSurfacePresentModesKHR(device, vk_inst.swapchain.surface, &present_mode_count, nullptr));

		if (present_mode_count != 0)
		{
			swapchain_details.present_modes.resize(present_mode_count);
			VkCheckResult(vkGetPhysicalDeviceSurfacePresentModesKHR(device, vk_inst.swapchain.surface, &present_mode_count, swapchain_details.present_modes.data()));
		}

		return swapchain_details;
	}

	static void CreateInstance()
	{
		VkApplicationInfo app_info = {};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pApplicationName = "VulkanRenderer";
		app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
		app_info.pEngineName = "No Engine";
		app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
		app_info.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo instance_create_info = {};
		instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instance_create_info.pApplicationInfo = &app_info;

		std::vector<const char*> required_extensions = GetRequiredExtensions();
		instance_create_info.enabledExtensionCount = (uint32_t)required_extensions.size();
		instance_create_info.ppEnabledExtensionNames = required_extensions.data();

		if (VulkanInstance::ENABLE_VALIDATION_LAYERS)
		{
			instance_create_info.enabledLayerCount = (uint32_t)vk_inst.debug.validation_layers.size();
			instance_create_info.ppEnabledLayerNames = vk_inst.debug.validation_layers.data();

			// Init debug messenger for vulkan instance creation
			VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = {};
			debug_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debug_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debug_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debug_messenger_create_info.pfnUserCallback = VkDebugCallback;
			debug_messenger_create_info.pUserData = nullptr;
			instance_create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debug_messenger_create_info;
		}

		VkCheckResult(vkCreateInstance(&instance_create_info, nullptr, &vk_inst.instance));
	}

	static void EnableValidationLayers()
	{
		if (VulkanInstance::ENABLE_VALIDATION_LAYERS)
		{
			// Enable validation layers

			uint32_t layer_count = 0;
			VkCheckResult(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));

			std::vector<VkLayerProperties> available_layers(layer_count);
			VkCheckResult(vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data()));

			for (const auto& validation_layer : vk_inst.debug.validation_layers)
			{
				bool layer_found = false;

				for (const auto& available_layer : available_layers)
				{
					if (strcmp(validation_layer, available_layer.layerName) == 0)
					{
						layer_found = true;
						break;
					}
				}

				VK_ASSERT(layer_found);
			}

			// Create debug messenger with custom message callback

			VkDebugUtilsMessengerCreateInfoEXT create_info = {};
			create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			create_info.pfnUserCallback = VkDebugCallback;
			create_info.pUserData = nullptr;

			PFN_vkCreateDebugUtilsMessengerEXT func = {};
			LoadVulkanFunction<PFN_vkCreateDebugUtilsMessengerEXT>("vkCreateDebugUtilsMessengerEXT", func);
			VkCheckResult(func(vk_inst.instance, &create_info, nullptr, &vk_inst.debug.debug_messenger));
		}
	}

	static void CreatePhysicalDevice()
	{
		uint32_t device_count = 0;
		vkEnumeratePhysicalDevices(vk_inst.instance, &device_count, nullptr);
		if (device_count == 0)
		{
			VK_EXCEPT("Vulkan", "No GPU devices found");
		}

		std::vector<VkPhysicalDevice> devices(device_count);
		vkEnumeratePhysicalDevices(vk_inst.instance, &device_count, devices.data());

		for (const auto& device : devices)
		{
			uint32_t extension_count = 0;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

			std::vector<VkExtensionProperties> available_extensions(extension_count);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

			std::set<std::string> required_extensions(vk_inst.extensions.begin(), vk_inst.extensions.end());

			for (const auto& extension : available_extensions)
			{
				required_extensions.erase(extension.extensionName);
			}

			bool swapchain_suitable = false;
			if (required_extensions.empty())
			{
				SwapChainSupportDetails swapchain_support = QuerySwapChainSupport(device);
				swapchain_suitable = !swapchain_support.formats.empty() && !swapchain_support.present_modes.empty();
			}

			VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_properties = {};
			descriptor_buffer_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;

			VkPhysicalDeviceProperties2 device_properties2 = {};
			device_properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
			device_properties2.pNext = &descriptor_buffer_properties;
			vkGetPhysicalDeviceProperties2(device, &device_properties2);

			VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features = {};
			descriptor_buffer_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;

			VkPhysicalDeviceMaintenance4Features maintenance4_features = {};
			maintenance4_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES;
			descriptor_buffer_features.pNext = &maintenance4_features;

			VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {};
			dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
			maintenance4_features.pNext = &dynamic_rendering_features;

			VkPhysicalDeviceFeatures2 device_features2 = {};
			device_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			device_features2.pNext = &descriptor_buffer_features;
			vkGetPhysicalDeviceFeatures2(device, &device_features2);

			if (device_properties2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
				required_extensions.empty() && swapchain_suitable &&
				device_features2.features.samplerAnisotropy &&
				descriptor_buffer_features.descriptorBuffer &&
				maintenance4_features.maintenance4)
			{
				vk_inst.physical_device = device;

				vk_inst.device_props.max_anisotropy = device_properties2.properties.limits.maxSamplerAnisotropy;

				vk_inst.descriptor_sizes.uniform_buffer = descriptor_buffer_properties.uniformBufferDescriptorSize;
				vk_inst.descriptor_sizes.storage_buffer = descriptor_buffer_properties.storageBufferDescriptorSize;
				vk_inst.descriptor_sizes.storage_image = descriptor_buffer_properties.storageImageDescriptorSize;
				vk_inst.descriptor_sizes.sampled_image = descriptor_buffer_properties.sampledImageDescriptorSize;
				vk_inst.descriptor_sizes.sampler = descriptor_buffer_properties.samplerDescriptorSize;
				break;
			}
		}

		if (vk_inst.physical_device == VK_NULL_HANDLE)
		{
			VK_EXCEPT("Vulkan", "No suitable GPU device found");
		}
	}

	static void CreateDevice()
	{
		FindQueueIndices();

		std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
		std::set<uint32_t> unique_queue_families = { vk_inst.queue_indices.present, vk_inst.queue_indices.graphics_compute };
		float queue_priority = 1.0;

		for (uint32_t queue_family : unique_queue_families)
		{
			VkDeviceQueueCreateInfo& queue_create_info = queue_create_infos.emplace_back();
			queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_create_info.queueFamilyIndex = queue_family;
			queue_create_info.queueCount = 1;
			queue_create_info.pQueuePriorities = &queue_priority;
		}

		VkDeviceCreateInfo device_create_info = {};
		device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_create_info.pQueueCreateInfos = queue_create_infos.data();
		device_create_info.queueCreateInfoCount = (uint32_t)queue_create_infos.size();
		device_create_info.ppEnabledExtensionNames = vk_inst.extensions.data();
		device_create_info.enabledExtensionCount = (uint32_t)vk_inst.extensions.size();

		// Request additional features to be enabled
		VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {};
		descriptor_indexing_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

		VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features = {};
		descriptor_buffer_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
		descriptor_indexing_features.pNext = &descriptor_buffer_features;

		VkPhysicalDeviceBufferDeviceAddressFeaturesEXT buffer_device_address_features = {};
		buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT;
		descriptor_buffer_features.pNext = &buffer_device_address_features;

		VkPhysicalDeviceSynchronization2Features sync_2_features = {};
		sync_2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
		buffer_device_address_features.pNext = &sync_2_features;

		VkPhysicalDeviceMaintenance4Features maintenance4_features = {};
		maintenance4_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES;
		sync_2_features.pNext = &maintenance4_features;

		VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {};
		dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
		maintenance4_features.pNext = &dynamic_rendering_features;

		VkPhysicalDeviceFeatures2 device_features2 = {};
		device_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		device_features2.features.samplerAnisotropy = VK_TRUE;
		device_features2.pNext = &descriptor_indexing_features;
		vkGetPhysicalDeviceFeatures2(vk_inst.physical_device, &device_features2);
		device_create_info.pNext = &device_features2;

		if (VulkanInstance::ENABLE_VALIDATION_LAYERS)
		{
			device_create_info.enabledLayerCount = (uint32_t)vk_inst.debug.validation_layers.size();
			device_create_info.ppEnabledLayerNames = vk_inst.debug.validation_layers.data();
		}
		else
		{
			device_create_info.enabledLayerCount = 0;
		}

		VkCheckResult(vkCreateDevice(vk_inst.physical_device, &device_create_info, nullptr, &vk_inst.device));

		// Create queues
		vkGetDeviceQueue(vk_inst.device, vk_inst.queue_indices.present, 0, &vk_inst.queues.present);
		vkGetDeviceQueue(vk_inst.device, vk_inst.queue_indices.graphics_compute, 0, &vk_inst.queues.graphics);

		// Load function pointers for extensions
		LoadVulkanFunction<PFN_vkGetDescriptorEXT>("vkGetDescriptorEXT", vk_inst.pFunc.get_descriptor_ext);
		LoadVulkanFunction<PFN_vkGetDescriptorSetLayoutSizeEXT>("vkGetDescriptorSetLayoutSizeEXT", vk_inst.pFunc.get_descriptor_set_layout_size_ext);
		LoadVulkanFunction<PFN_vkGetDescriptorSetLayoutBindingOffsetEXT>("vkGetDescriptorSetLayoutBindingOffsetEXT", vk_inst.pFunc.get_descriptor_set_layout_binding_offset_ext);
		LoadVulkanFunction<PFN_vkCmdSetDescriptorBufferOffsetsEXT>("vkCmdSetDescriptorBufferOffsetsEXT", vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext);
		LoadVulkanFunction<PFN_vkCmdBindDescriptorBuffersEXT>("vkCmdBindDescriptorBuffersEXT", vk_inst.pFunc.cmd_bind_descriptor_buffers_ext);
	}

	static void CreateCommandPool()
	{
		VkCommandPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = vk_inst.queue_indices.graphics_compute;
		VkCheckResult(vkCreateCommandPool(vk_inst.device, &pool_info, nullptr, &vk_inst.cmd_pools.graphics));
	}

	static VkSurfaceFormatKHR ChooseSwapChainFormat(const std::vector<VkSurfaceFormatKHR>& available_formats)
	{
		for (const auto& available_format : available_formats)
		{
			if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
				available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				return available_format;
			}

			return available_formats[0];
		}

		VK_EXCEPT("Vulkan", "Swapchain does not have any formats");
		VkSurfaceFormatKHR default_format = {};
		default_format.format = VK_FORMAT_UNDEFINED;
		default_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		return default_format;
	}

	static VkPresentModeKHR ChooseSwapChainPresentMode(const std::vector<VkPresentModeKHR>& available_present_modes)
	{
		for (const auto& available_present_mode : available_present_modes)
		{
			if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				return available_present_mode;
			}
		}

		return VK_PRESENT_MODE_FIFO_KHR;
	}

	static VkExtent2D ChooseSwapChainExtent(const VkSurfaceCapabilitiesKHR& capabilities)
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}
		else
		{
			int width, height;
			glfwGetFramebufferSize(vk_inst.window, &width, &height);

			VkExtent2D actual_extent =
			{
				(uint32_t)width,
				(uint32_t)height
			};

			actual_extent.width = std::clamp(actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			actual_extent.height = std::clamp(actual_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

			return actual_extent;
		}
	}

	static void CreateSwapChain()
	{
		SwapChainSupportDetails swapchain_support = QuerySwapChainSupport(vk_inst.physical_device);

		VkSurfaceFormatKHR surface_format = ChooseSwapChainFormat(swapchain_support.formats);
		VkPresentModeKHR present_mode = ChooseSwapChainPresentMode(swapchain_support.present_modes);
		VkExtent2D extent = ChooseSwapChainExtent(swapchain_support.capabilities);

		uint32_t image_count = swapchain_support.capabilities.minImageCount;
		if (swapchain_support.capabilities.maxImageCount > 0 && image_count > swapchain_support.capabilities.maxImageCount)
		{
			image_count = swapchain_support.capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		create_info.surface = vk_inst.swapchain.surface;
		create_info.minImageCount = image_count;
		create_info.imageFormat = surface_format.format;
		create_info.imageColorSpace = surface_format.colorSpace;
		create_info.imageExtent = extent;
		create_info.imageArrayLayers = 1;
		create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		uint32_t queue_family_indices[] = { vk_inst.queue_indices.graphics_compute, vk_inst.queue_indices.present };

		if (vk_inst.queue_indices.graphics_compute != vk_inst.queue_indices.present)
		{
			// NOTE: VK_SHARING_MODE_CONCURRENT specifies that swap chain images are shared between multiple queue families without explicit ownership
			create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			create_info.queueFamilyIndexCount = 2;
			create_info.pQueueFamilyIndices = queue_family_indices;
		}
		else
		{
			// NOTE: VK_SHARING_MODE_EXCLUSIVE specifies that swap chain images are owned by a single queue family and ownership must be explicitly transferred
			create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			create_info.queueFamilyIndexCount = 0;
			create_info.pQueueFamilyIndices = nullptr;
		}

		create_info.preTransform = swapchain_support.capabilities.currentTransform;
		create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		create_info.presentMode = present_mode;
		create_info.clipped = VK_TRUE;
		create_info.oldSwapchain = VK_NULL_HANDLE;

		VkCheckResult(vkCreateSwapchainKHR(vk_inst.device, &create_info, nullptr, &vk_inst.swapchain.swapchain));
		VkCheckResult(vkGetSwapchainImagesKHR(vk_inst.device, vk_inst.swapchain.swapchain, &image_count, nullptr));

		vk_inst.swapchain.images.resize(image_count);
		VkCheckResult(vkGetSwapchainImagesKHR(vk_inst.device, vk_inst.swapchain.swapchain, &image_count, vk_inst.swapchain.images.data()));

		vk_inst.swapchain.extent = extent;
		vk_inst.swapchain.format = create_info.imageFormat;

		VkSemaphoreCreateInfo semaphore_info = {};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vk_inst.swapchain.image_available_semaphores.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);

		for (size_t i = 0; i < vk_inst.swapchain.image_available_semaphores.size(); ++i)
		{
			VkCheckResult(vkCreateSemaphore(vk_inst.device, &semaphore_info, nullptr, &vk_inst.swapchain.image_available_semaphores[i]));
		}
	}

	static void DestroySwapChain()
	{
		vkDestroySwapchainKHR(vk_inst.device, vk_inst.swapchain.swapchain, nullptr);
	}

	static VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
	{
		for (VkFormat format : candidates)
		{
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(vk_inst.physical_device, format, &props);

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
			{
				return format;
			}
			else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
			{
				return format;
			}
		}

		VK_EXCEPT("Vulkan", "Failed to find a supported format");
	}

	static bool HasStencilComponent(VkFormat format)
	{
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
	}

	static inline bool HasImageLayoutBitSet(VkImageLayout layout, VkImageLayout check)
	{
		return (layout & check) == 1;
	}

	static void GetImageLayoutAccessAndStageFlags(VkImageLayout layout, VkAccessFlags2& access_flags, VkPipelineStageFlags2& stage_flags)
	{
		// Access mask
		switch (layout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			access_flags = VK_ACCESS_2_NONE;
			break;
		case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			access_flags = VK_ACCESS_2_MEMORY_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			access_flags = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
			break;
		default:
			VK_EXCEPT("Vulkan", "No VkAccessFlags2 found for layout");
			break;
		}

		// Pipeline stage flag
		switch (layout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			stage_flags = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			stage_flags = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			stage_flags = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			stage_flags = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			stage_flags = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
			break;
		case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			stage_flags = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			break;
		default:
			VK_EXCEPT("Vulkan", "No VkPipelineStageFlags2 found for layout");
			break;
		}
	}

	void Init(::GLFWwindow* window)
	{
		vk_inst.window = window;

		CreateInstance();
		EnableValidationLayers();

		VkCheckResult(glfwCreateWindowSurface(vk_inst.instance, vk_inst.window, nullptr, &vk_inst.swapchain.surface));
		CreatePhysicalDevice();
		CreateDevice();

		CreateCommandPool();
		CreateSwapChain();
	}

	void Exit()
	{
		vkDestroyCommandPool(vk_inst.device, vk_inst.cmd_pools.graphics, nullptr);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroySemaphore(vk_inst.device, vk_inst.swapchain.image_available_semaphores[i], nullptr);
		}
		DestroySwapChain();
		vkDestroySurfaceKHR(vk_inst.instance, vk_inst.swapchain.surface, nullptr);

		if (VulkanInstance::ENABLE_VALIDATION_LAYERS)
		{
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk_inst.instance, "vkDestroyDebugUtilsMessengerEXT");
			if (!func)
			{
				VK_EXCEPT("Vulkan", "Could not find function pointer vkDestroyDebugUtilsMessengerEXT");
			}

			func(vk_inst.instance, vk_inst.debug.debug_messenger, nullptr);
		}

		vkDestroyDevice(vk_inst.device, nullptr);
		vkDestroyInstance(vk_inst.instance, nullptr);
	}

	bool SwapChainAcquireNextImage()
	{
		VkResult image_result = vkAcquireNextImageKHR(vk_inst.device, vk_inst.swapchain.swapchain, UINT64_MAX,
			vk_inst.swapchain.image_available_semaphores[vk_inst.current_frame], VK_NULL_HANDLE, &vk_inst.swapchain.current_image);

		if (image_result == VK_ERROR_OUT_OF_DATE_KHR || image_result == VK_SUBOPTIMAL_KHR)
		{
			Vulkan::RecreateSwapChain();
			return true;
		}
		else if (image_result != VK_SUCCESS && image_result != VK_SUBOPTIMAL_KHR)
		{
			VkCheckResult(image_result);
		}

		return false;
	}

	Image SwapChainGetCurrentImage()
	{
		Image result;
		result.image = vk_inst.swapchain.images[vk_inst.swapchain.current_image];
		result.format = vk_inst.swapchain.format;

		return result;
	}

	bool SwapChainPresent(const std::vector<VkSemaphore>& signal_semaphores)
	{
		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = (uint32_t)signal_semaphores.size();
		present_info.pWaitSemaphores = signal_semaphores.data();

		present_info.swapchainCount = 1;
		present_info.pSwapchains = &vk_inst.swapchain.swapchain;
		present_info.pImageIndices = &vk_inst.swapchain.current_image;
		present_info.pResults = nullptr;

		VkResult present_result = vkQueuePresentKHR(vk_inst.queues.graphics, &present_info);

		if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
		{
			Vulkan::RecreateSwapChain();
			return true;
		}
		else
		{
			VkCheckResult(present_result);
		}

		return false;
	}

	void RecreateSwapChain()
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(vk_inst.window, &width, &height);
		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(vk_inst.window, &width, &height);
			glfwWaitEvents();
		}

		vkDeviceWaitIdle(vk_inst.device);

		DestroySwapChain();
		CreateSwapChain();
	}

	void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags, Buffer& buffer)
	{
		VkBufferCreateInfo buffer_info = {};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = size;
		buffer_info.usage = usage;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkCheckResult(vkCreateBuffer(vk_inst.device, &buffer_info, nullptr, &buffer.buffer));

		VkMemoryRequirements mem_requirements = {};
		vkGetBufferMemoryRequirements(vk_inst.device, buffer.buffer, &mem_requirements);

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_requirements.size;
		alloc_info.memoryTypeIndex = FindMemoryType(mem_requirements.memoryTypeBits, mem_flags);

		VkCheckResult(vkAllocateMemory(vk_inst.device, &alloc_info, nullptr, &buffer.memory));
		VkCheckResult(vkBindBufferMemory(vk_inst.device, buffer.buffer, buffer.memory, 0));

		// TODO: Offset will be used later when we sub-allocate from large buffers
		buffer.size = size;
		buffer.offset = 0;
	}

	void CreateStagingBuffer(VkDeviceSize size, Buffer& buffer)
	{
		CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer);
		VkCheckResult(vkMapMemory(vk_inst.device, buffer.memory, 0, size, 0, &buffer.ptr));
	}

	void CreateUniformBuffer(VkDeviceSize size, Buffer& buffer)
	{
		CreateBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer);
		VkCheckResult(vkMapMemory(vk_inst.device, buffer.memory, 0, size, 0, &buffer.ptr));
	}

	void CreateDescriptorBuffer(VkDeviceSize size, Buffer& buffer)
	{
		CreateBuffer(size, VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer);
		VkCheckResult(vkMapMemory(vk_inst.device, buffer.memory, 0, size, 0, &buffer.ptr));
	}

	void CreateVertexBuffer(VkDeviceSize size, Buffer& buffer)
	{
		CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer);
	}

	void CreateIndexBuffer(VkDeviceSize size, Buffer& buffer)
	{
		CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer);
	}

	void DestroyBuffer(const Buffer& buffer)
	{
		if (buffer.ptr)
		{
			vkUnmapMemory(vk_inst.device, buffer.memory);
		}
		vkDestroyBuffer(vk_inst.device, buffer.buffer, nullptr);
		vkFreeMemory(vk_inst.device, buffer.memory, nullptr);
	}

	void WriteBuffer(void* dst_ptr, void* src_ptr, VkDeviceSize size)
	{
		// NOTE: The driver may or may not have immediately copied this over to buffer memory (e.g. caching)
		// or writes to the buffer are not visible in the mapped memory yet.
		// To deal with this problem, you either have to use a memory heap that is host coherent (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		// or call vkFlushMappedMemoryRanges after writing to the mapped memory and then calling vkInvalidateMappedMemoryRanges before reading from it
		// The transfer of data to the GPU happens in the background and the specification states it is guaranteed to be complete as of the next call to vkQueueSubmit
		memcpy(dst_ptr, src_ptr, size);
	}

	void CopyBuffer(const Buffer& src_buffer, const Buffer& dst_buffer, VkDeviceSize size, VkDeviceSize src_offset, VkDeviceSize dst_offset)
	{
		VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();

		VkBufferCopy copy_region = {};
		copy_region.srcOffset = src_offset;
		copy_region.dstOffset = dst_offset;
		copy_region.size = size;
		vkCmdCopyBuffer(command_buffer, src_buffer.buffer, dst_buffer.buffer, 1, &copy_region);

		Vulkan::EndImmediateCommand(command_buffer);
	}

	void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
		VkImageUsageFlags usage, VkMemoryPropertyFlags memory_flags, Image& image, uint32_t num_mips)
	{
		VkImageCreateInfo image_info = {};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.extent.width = width;
		image_info.extent.height = height;
		image_info.extent.depth = 1;
		image_info.mipLevels = num_mips;
		image_info.arrayLayers = 1;
		image_info.format = format;
		image_info.tiling = tiling;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_info.usage = usage;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.flags = 0;

		VkCheckResult(vkCreateImage(vk_inst.device, &image_info, nullptr, &image.image));
		image.format = format;

		VkMemoryRequirements mem_requirements = {};
		vkGetImageMemoryRequirements(vk_inst.device, image.image, &mem_requirements);

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_requirements.size;
		alloc_info.memoryTypeIndex = FindMemoryType(mem_requirements.memoryTypeBits, memory_flags);

		VkCheckResult(vkAllocateMemory(vk_inst.device, &alloc_info, nullptr, &image.memory));
		VkCheckResult(vkBindImageMemory(vk_inst.device, image.image, image.memory, 0));
	}

	void CreateImageView(VkImageAspectFlags aspect_flags, Image& image, uint32_t num_mips)
	{
		VkImageViewCreateInfo view_info = {};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = image.image;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = image.format;
		view_info.subresourceRange.aspectMask = aspect_flags;
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.levelCount = num_mips;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.layerCount = 1;

		VkCheckResult(vkCreateImageView(vk_inst.device, &view_info, nullptr, &image.view));
	}

	void GenerateMips(uint32_t width, uint32_t height, uint32_t num_mips, VkFormat format, Image& image)
	{
		VkFormatProperties format_properties;
		vkGetPhysicalDeviceFormatProperties(vk_inst.physical_device, format, &format_properties);

		if (!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		{
			VK_EXCEPT("Vulkan", "Texture image format does not support linear filter in blitting operation");
		}

		VkCommandBuffer command_buffer = BeginImmediateCommand();

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image.image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		int32_t mip_width = width;
		int32_t mip_height = height;

		for (uint32_t i = 1; i < num_mips; ++i)
		{
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);

			VkImageBlit blit = {};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mip_width, mip_height, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mip_width > 1 ? mip_width / 2 : 1, mip_height > 1 ? mip_height / 2 : 1, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;

			vkCmdBlitImage(command_buffer,
				image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR
			);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);

			if (mip_width > 1) mip_width /= 2;
			if (mip_height > 1) mip_height /= 2;
		}

		barrier.subresourceRange.baseMipLevel = num_mips - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		EndImmediateCommand(command_buffer);
	}

	void DestroyImage(const Image& image)
	{
		vkDestroyImageView(vk_inst.device, image.view, nullptr);
		vkDestroyImage(vk_inst.device, image.image, nullptr);
		vkFreeMemory(vk_inst.device, image.memory, nullptr);
	}

	void CopyBufferToImage(const Buffer& src_buffer, const Image& dst_image, uint32_t width, uint32_t height, VkDeviceSize src_offset)
	{
		VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();

		VkBufferImageCopy region = {};
		region.bufferOffset = src_offset;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };

		vkCmdCopyBufferToImage(command_buffer, src_buffer.buffer, dst_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		Vulkan::EndImmediateCommand(command_buffer);
	}

	VkFormat FindDepthFormat()
	{
		return FindSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);
	}

	uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags mem_properties)
	{
		VkPhysicalDeviceMemoryProperties device_mem_properties = {};
		vkGetPhysicalDeviceMemoryProperties(vk_inst.physical_device, &device_mem_properties);

		for (uint32_t i = 0; i < device_mem_properties.memoryTypeCount; ++i)
		{
			if (type_filter & (1 << i) &&
				(device_mem_properties.memoryTypes[i].propertyFlags & mem_properties) == mem_properties)
			{
				return i;
			}
		}

		VK_EXCEPT("Vulkan", "Failed to find suitable memory type");
	}

	VkCommandBuffer BeginImmediateCommand()
	{
		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandPool = vk_inst.cmd_pools.graphics;
		alloc_info.commandBufferCount = 1;

		VkCommandBuffer command_buffer;
		VkCheckResult(vkAllocateCommandBuffers(vk_inst.device, &alloc_info, &command_buffer));

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkCheckResult(vkBeginCommandBuffer(command_buffer, &begin_info));
		return command_buffer;
	}

	void EndImmediateCommand(VkCommandBuffer command_buffer)
	{
		VkCheckResult(vkEndCommandBuffer(command_buffer));

		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;

		VkCheckResult(vkQueueSubmit(vk_inst.queues.graphics, 1, &submit_info, VK_NULL_HANDLE));
		VkCheckResult(vkQueueWaitIdle(vk_inst.queues.graphics));

		vkFreeCommandBuffers(vk_inst.device, vk_inst.cmd_pools.graphics, 1, &command_buffer);
	}

	VkBufferMemoryBarrier2 BufferMemoryBarrier(Buffer& buffer, VkAccessFlags2 src_access, VkPipelineStageFlags2 src_stage, VkAccessFlags2 dst_access, VkPipelineStageFlags2 dst_stage)
	{
		VkBufferMemoryBarrier2 buffer_memory_barrier = {};
		buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		buffer_memory_barrier.buffer = buffer.buffer;
		buffer_memory_barrier.size = buffer.size;
		buffer_memory_barrier.offset = buffer.offset;
		buffer_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		buffer_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		buffer_memory_barrier.srcAccessMask = src_access;
		buffer_memory_barrier.srcStageMask = src_stage;
		buffer_memory_barrier.dstAccessMask = dst_access;
		buffer_memory_barrier.dstStageMask = dst_stage;

		return buffer_memory_barrier;
	}

	void CmdBufferMemoryBarrier(VkCommandBuffer command_buffer, Buffer& buffer, VkAccessFlags2 src_access, VkPipelineStageFlags2 src_stage, VkAccessFlags2 dst_access, VkPipelineStageFlags2 dst_stage)
	{
		VkBufferMemoryBarrier2 barrier = BufferMemoryBarrier(buffer, src_access, src_stage, dst_access, dst_stage);

		VkDependencyInfo dependency_info = {};
		dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency_info.bufferMemoryBarrierCount = 1;
		dependency_info.pBufferMemoryBarriers = &barrier;
		dependency_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		vkCmdPipelineBarrier2(command_buffer, &dependency_info);
	}

	void CmdBufferMemoryBarriers(VkCommandBuffer command_buffer, const std::vector<VkBufferMemoryBarrier2>& buffer_barriers)
	{
		VkDependencyInfo dependency_info = {};
		dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency_info.bufferMemoryBarrierCount = (uint32_t)buffer_barriers.size();
		dependency_info.pBufferMemoryBarriers = buffer_barriers.data();
		dependency_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		vkCmdPipelineBarrier2(command_buffer, &dependency_info);
	}

	void BufferMemoryBarrierImmediate(Buffer& buffer, VkAccessFlags2 src_access, VkPipelineStageFlags2 src_stage, VkAccessFlags2 dst_access, VkPipelineStageFlags2 dst_stage)
	{
		VkCommandBuffer command_buffer = BeginImmediateCommand();
		CmdBufferMemoryBarrier(command_buffer, buffer, src_access, src_stage, dst_access, dst_stage);
		EndImmediateCommand(command_buffer);
	}

	VkImageMemoryBarrier2 ImageMemoryBarrier(Image& image, VkImageLayout new_layout, uint32_t num_mips)
	{
		VkImageMemoryBarrier2 image_memory_barrier = {};
		image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		image_memory_barrier.image = image.image;
		image_memory_barrier.oldLayout = image.layout;
		image_memory_barrier.newLayout = new_layout;

		image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		GetImageLayoutAccessAndStageFlags(image.layout, image_memory_barrier.srcAccessMask, image_memory_barrier.srcStageMask);
		GetImageLayoutAccessAndStageFlags(new_layout, image_memory_barrier.dstAccessMask, image_memory_barrier.dstStageMask);

		if (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL || new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (HasStencilComponent(image.format))
			{
				image_memory_barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else
		{
			image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		image_memory_barrier.subresourceRange.baseMipLevel = 0;
		image_memory_barrier.subresourceRange.levelCount = num_mips;
		image_memory_barrier.subresourceRange.baseArrayLayer = 0;
		image_memory_barrier.subresourceRange.layerCount = 1;

		// TODO: This smells bad, we are updating the images layout even though we haven't actually issued a pipeline barrier to be executed
		// Probably having a resource tracker a la DX12 will do the trick here (hashmap with the VkImage pointer being the key)
		image.layout = new_layout;
		return image_memory_barrier;
	}

	void CmdTransitionImageLayout(VkCommandBuffer command_buffer, Image& image, VkImageLayout new_layout, uint32_t num_mips)
	{
		VkImageMemoryBarrier2 barrier = ImageMemoryBarrier(image, new_layout, num_mips);

		VkDependencyInfo dependency_info = {};
		dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency_info.imageMemoryBarrierCount = 1;
		dependency_info.pImageMemoryBarriers = &barrier;
		dependency_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		vkCmdPipelineBarrier2(command_buffer, &dependency_info);
	}

	void CmdTransitionImageLayouts(VkCommandBuffer command_buffer, const std::vector<VkImageMemoryBarrier2>& image_barriers)
	{
		VkDependencyInfo dependency_info = {};
		dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency_info.imageMemoryBarrierCount = (uint32_t)image_barriers.size();
		dependency_info.pImageMemoryBarriers = image_barriers.data();
		dependency_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		vkCmdPipelineBarrier2(command_buffer, &dependency_info);
	}

	void TransitionImageLayoutImmediate(Image& image, VkImageLayout new_layout, uint32_t num_mips)
	{
		VkCommandBuffer command_buffer = BeginImmediateCommand();
		CmdTransitionImageLayout(command_buffer, image, new_layout, num_mips);
		EndImmediateCommand(command_buffer);
	}
	
	VkMemoryBarrier2 MemoryBarrier(VkAccessFlags2 src_access, VkPipelineStageFlags2 src_stage, VkAccessFlags2 dst_access, VkPipelineStageFlags2 dst_stage)
	{
		VkMemoryBarrier2 memory_barrier = {};
		memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		memory_barrier.srcAccessMask = src_access;
		memory_barrier.srcStageMask = src_stage;
		memory_barrier.dstAccessMask = dst_access;
		memory_barrier.dstStageMask = dst_stage;

		return memory_barrier;
	}

	void CmdMemoryBarrier(VkCommandBuffer command_buffer, const VkMemoryBarrier2& memory_barrier)
	{
		VkDependencyInfo dependency_info = {};
		dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency_info.memoryBarrierCount = 1;
		dependency_info.pMemoryBarriers = &memory_barrier;

		vkCmdPipelineBarrier2(command_buffer, &dependency_info);
	}

	void CmdMemoryBarriers(VkCommandBuffer command_buffer, const std::vector<VkMemoryBarrier2>& memory_barriers)
	{
		VkDependencyInfo dependency_info = {};
		dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency_info.memoryBarrierCount = (uint32_t)memory_barriers.size();
		dependency_info.pMemoryBarriers = memory_barriers.data();

		vkCmdPipelineBarrier2(command_buffer, &dependency_info);
	}

	VkMemoryBarrier2 ExecutionBarrier(VkPipelineStageFlags2 src_stage, VkPipelineStageFlags2 dst_stage)
	{
		VkMemoryBarrier2 execution_barrier = {};
		execution_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		execution_barrier.srcStageMask = src_stage;
		execution_barrier.dstStageMask = dst_stage;

		return execution_barrier;
	}

	void CmdExecutionBarrier(VkCommandBuffer command_buffer, const VkMemoryBarrier2& memory_barrier)
	{
		VkDependencyInfo dependency_info = {};
		dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency_info.memoryBarrierCount = 1;
		dependency_info.pMemoryBarriers = &memory_barrier;

		vkCmdPipelineBarrier2(command_buffer, &dependency_info);
	}

}
