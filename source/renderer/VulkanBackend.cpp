#include "Precomp.h"
#include "renderer/VulkanBackend.h"
#include "renderer/VulkanResourceTracker.h"
#include "renderer/DescriptorBuffer.h"

#include "shaderc/shaderc.hpp"

#include "GLFW/glfw3.h"

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

		int32_t i = 0;
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

			// Request Physical Device Properties
			VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT };

			VkPhysicalDeviceProperties2 device_properties2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			device_properties2.pNext = &descriptor_buffer_properties;
			vkGetPhysicalDeviceProperties2(device, &device_properties2);

			// Enable device features
			VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT };

			VkPhysicalDeviceBufferDeviceAddressFeaturesEXT buffer_device_address_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT };
			descriptor_buffer_features.pNext = &buffer_device_address_features;

			VkPhysicalDeviceMaintenance4Features maintenance4_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES };
			buffer_device_address_features.pNext = &maintenance4_features;

			VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
			maintenance4_features.pNext = &dynamic_rendering_features;

			VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };
			dynamic_rendering_features.pNext = &timeline_semaphore_features;

			VkPhysicalDeviceFeatures2 device_features2 = {};
			device_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			device_features2.pNext = &descriptor_buffer_features;

			vkGetPhysicalDeviceFeatures2(device, &device_features2);

			if (device_properties2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
				required_extensions.empty() && swapchain_suitable &&
				device_features2.features.samplerAnisotropy &&
				descriptor_buffer_features.descriptorBuffer &&
				buffer_device_address_features.bufferDeviceAddress &&
				buffer_device_address_features.bufferDeviceAddressCaptureReplay &&
				maintenance4_features.maintenance4 &&
				dynamic_rendering_features.dynamicRendering &&
				timeline_semaphore_features.timelineSemaphore)
			{
				vk_inst.physical_device = device;

				vk_inst.device_props.max_anisotropy = device_properties2.properties.limits.maxSamplerAnisotropy;
				vk_inst.device_props.descriptor_buffer_offset_alignment = descriptor_buffer_properties.descriptorBufferOffsetAlignment;

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
		VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };

		VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT };
		descriptor_indexing_features.pNext = &descriptor_buffer_features;

		VkPhysicalDeviceBufferDeviceAddressFeaturesEXT buffer_device_address_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT };
		descriptor_buffer_features.pNext = &buffer_device_address_features;

		VkPhysicalDeviceSynchronization2Features sync_2_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
		buffer_device_address_features.pNext = &sync_2_features;

		VkPhysicalDeviceMaintenance4Features maintenance4_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES };
		sync_2_features.pNext = &maintenance4_features;

		VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
		maintenance4_features.pNext = &dynamic_rendering_features;

		VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };
		dynamic_rendering_features.pNext = &timeline_semaphore_features;

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

#ifdef _DEBUG
		LoadVulkanFunction<PFN_vkDebugMarkerSetObjectNameEXT>("vkSetDebugUtilsObjectNameEXT", vk_inst.pFunc.debug_marker_set_object_name_ext);
#endif
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
		create_info.presentMode = vk_inst.swapchain.desired_present_mode;
		create_info.clipped = VK_TRUE;
		create_info.oldSwapchain = VK_NULL_HANDLE;

		std::vector<VkPresentModeKHR> present_modes = { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR };
		VkSwapchainPresentModesCreateInfoEXT present_modes_create_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT };
		present_modes_create_info.presentModeCount = static_cast<uint32_t>(present_modes.size());
		present_modes_create_info.pPresentModes = present_modes.data();
		
		create_info.pNext = &present_modes_create_info;

		VkCheckResult(vkCreateSwapchainKHR(vk_inst.device, &create_info, nullptr, &vk_inst.swapchain.swapchain));
		// If pSwapchainImages is nullptr, it will instead return the pSwapchainImageCount, so we query this first
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
			VulkanResourceTracker::TrackImage(vk_inst.swapchain.images[i], VK_IMAGE_LAYOUT_UNDEFINED);
		}
	}

	static void DestroySwapChain()
	{
		for (size_t i = 0; i < vk_inst.swapchain.image_available_semaphores.size(); ++i)
		{
			VulkanResourceTracker::RemoveImage(vk_inst.swapchain.images[i]);
			vkDestroySemaphore(vk_inst.device, vk_inst.swapchain.image_available_semaphores[i], nullptr);
		}

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

	static std::vector<char> ReadFile(const char* filepath)
	{
		std::ifstream file(filepath, std::ios::ate | std::ios::binary);
		if (!file.is_open())
		{
			VK_EXCEPT("Assets", "Could not open file: {}", filepath);
		}

		size_t file_size = (size_t)file.tellg();
		std::vector<char> buffer(file_size);

		file.seekg(0);
		file.read(buffer.data(), file_size);
		file.close();

		return buffer;
	}

	class ShadercIncluder : public shaderc::CompileOptions::IncluderInterface
	{
	public:
		virtual shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type,
			const char* requesting_source, size_t include_depth) override
		{
			if (include_depth == 1)
				m_include_directory = GetDirectoryFromFilepath(requesting_source);

			shaderc_include_result* result = new shaderc_include_result();
			result->source_name = requested_source;
			result->source_name_length = strlen(requested_source);

			std::string requested_source_filepath = MakeRequestedFilepath(requested_source);
			std::vector<char> requested_source_text = ReadFile(requested_source_filepath.c_str());

			result->content = new char[requested_source_text.size()];
			memcpy((void*)result->content, requested_source_text.data(), requested_source_text.size());
			result->content_length = requested_source_text.size();

			return result;
		}

		// Handles shaderc_include_result_release_fn callbacks.
		virtual void ReleaseInclude(shaderc_include_result* data) override
		{
			delete data->content;
			delete data;
		}

	private:
		std::string MakeRequestedFilepath(const char* requested_source)
		{
			std::string requested_source_filepath = m_include_directory + requested_source;
			return requested_source_filepath;
		}

		std::string GetDirectoryFromFilepath(const std::string& filepath)
		{
			return std::string(filepath).substr(0, std::string(filepath).find_last_of("\\/") + 1);
		}

		std::string StripDirectoryFromFilepath(const std::string& filepath)
		{
			return std::string(filepath).substr(std::string(filepath).find_last_of("\\/") + 1, filepath.size());
		}

	private:
		std::string m_include_directory = "";

	};

	struct Data
	{
		struct ShaderCompiler
		{
			shaderc::Compiler compiler;
			ShadercIncluder includer;
		} shader_compiler;

		struct DescriptorBuffers
		{
			DescriptorBuffer uniform{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RESERVED_DESCRIPTOR_UBO_COUNT * VulkanInstance::MAX_FRAMES_IN_FLIGHT };
			DescriptorBuffer storage_buffer{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
			DescriptorBuffer storage_image{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
			DescriptorBuffer sampled_image{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE };
			DescriptorBuffer sampler{ VK_DESCRIPTOR_TYPE_SAMPLER };
		} descriptor_buffers;
	} static* data;

	static std::vector<uint32_t> CompileShader(const char* filepath, shaderc_shader_kind shader_type)
	{
		auto shader_text = ReadFile(filepath);

		shaderc::CompileOptions compile_options = {};
#ifdef _DEBUG
		compile_options.SetOptimizationLevel(shaderc_optimization_level_zero);
		compile_options.SetGenerateDebugInfo();
		compile_options.SetWarningsAsErrors();
#else
		compile_options.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif
		compile_options.SetIncluder(std::make_unique<ShadercIncluder>());
		compile_options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
		compile_options.SetTargetSpirv(shaderc_spirv_version_1_6);

		shaderc::SpvCompilationResult shader_compile_result = data->shader_compiler.compiler.CompileGlslToSpv(
			shader_text.data(), shader_text.size(), shader_type, filepath, "main", compile_options);

		if (shader_compile_result.GetCompilationStatus() != shaderc_compilation_status_success)
		{
			VK_EXCEPT("Vulkan", shader_compile_result.GetErrorMessage());
		}

		return { shader_compile_result.begin(), shader_compile_result.end() };
	}

	static VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code)
	{
		VkShaderModuleCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		create_info.codeSize = code.size() * sizeof(uint32_t);
		create_info.pCode = code.data();

		VkShaderModule shader_module;
		VkCheckResult(vkCreateShaderModule(vk_inst.device, &create_info, nullptr, &shader_module));

		return shader_module;
	}

	void Init(::GLFWwindow* window)
	{
		vk_inst.window = window;

		VulkanResourceTracker::Init();

		CreateInstance();
		EnableValidationLayers();

		VkCheckResult(glfwCreateWindowSurface(vk_inst.instance, vk_inst.window, nullptr, &vk_inst.swapchain.surface));
		CreatePhysicalDevice();
		CreateDevice();

		CreateCommandPool();
		CreateSwapChain();

		data = new Data();
	}

	void Exit()
	{
		delete data;

		vkDestroyCommandPool(vk_inst.device, vk_inst.cmd_pools.graphics, nullptr);
		
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

		VulkanResourceTracker::Exit();
	}

	VkResult SwapChainAcquireNextImage()
	{
		VkResult image_result = vkAcquireNextImageKHR(vk_inst.device, vk_inst.swapchain.swapchain, UINT64_MAX,
			vk_inst.swapchain.image_available_semaphores[vk_inst.current_frame], VK_NULL_HANDLE, &vk_inst.swapchain.current_image);

		return image_result;
	}

	VkResult SwapChainPresent(const std::vector<VkSemaphore>& wait_semaphores)
	{
		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = (uint32_t)wait_semaphores.size();
		present_info.pWaitSemaphores = wait_semaphores.data();

		present_info.swapchainCount = 1;
		present_info.pSwapchains = &vk_inst.swapchain.swapchain;
		present_info.pImageIndices = &vk_inst.swapchain.current_image;

		VkSwapchainPresentModeInfoEXT present_mode_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT };
		present_mode_info.swapchainCount = 1;
		present_mode_info.pPresentModes = &vk_inst.swapchain.desired_present_mode;

		present_info.pNext = &present_mode_info;

		VkResult present_result = vkQueuePresentKHR(vk_inst.queues.graphics, &present_info);
		vk_inst.current_frame = (vk_inst.current_frame + 1) % VulkanInstance::MAX_FRAMES_IN_FLIGHT;

		return present_result;
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

	void CopyToSwapchain(VkCommandBuffer command_buffer, VkImage src_image)
	{
		// Copy final result to swapchain image
		// We use vkCmdBlitImage here to have format conversions done automatically for us
		// E.g. R8G8B8A8 to B8G8R8A8
		VkImage swapchain_image = vk_inst.swapchain.images[vk_inst.swapchain.current_image];

		VkImageBlit blit_region = {};
		blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit_region.srcSubresource.mipLevel = 0;
		blit_region.srcSubresource.baseArrayLayer = 0;
		blit_region.srcSubresource.layerCount = 1;
		blit_region.srcOffsets[0] = { 0, 0, 0 };
		blit_region.srcOffsets[1] = { (int32_t)vk_inst.swapchain.extent.width, (int32_t)vk_inst.swapchain.extent.height, 1 };

		blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit_region.dstSubresource.mipLevel = 0;
		blit_region.dstSubresource.baseArrayLayer = 0;
		blit_region.dstSubresource.layerCount = 1;
		blit_region.dstOffsets[0] = { 0, 0, 0 };
		blit_region.dstOffsets[1] = { (int32_t)vk_inst.swapchain.extent.width, (int32_t)vk_inst.swapchain.extent.height, 1 };

		vkCmdBlitImage(
			command_buffer,
			src_image, VulkanResourceTracker::GetImageLayout(src_image),
			swapchain_image, VulkanResourceTracker::GetImageLayout(swapchain_image),
			1, &blit_region, VK_FILTER_NEAREST
		);
	}

	void SetVSyncEnabled(bool enabled)
	{
		bool changed = vk_inst.swapchain.vsync_enabled != enabled;
		
		if (changed)
		{
			vk_inst.swapchain.vsync_enabled = enabled;

			if (vk_inst.swapchain.vsync_enabled)
				vk_inst.swapchain.desired_present_mode = VK_PRESENT_MODE_FIFO_KHR;
			else
				vk_inst.swapchain.desired_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
		}
	}

	bool IsVSyncEnabled()
	{
		return vk_inst.swapchain.vsync_enabled;
	}

	void DebugNameObject(uint64_t object, VkDebugReportObjectTypeEXT object_type, const char* debug_name)
	{
#ifdef _DEBUG
		VkDebugMarkerObjectNameInfoEXT debug_name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		debug_name_info.objectType = object_type;
		debug_name_info.object = object;
		debug_name_info.pObjectName = debug_name;

		vk_inst.pFunc.debug_marker_set_object_name_ext(vk_inst.device, &debug_name_info);
#endif
	}

	VkDeviceMemory AllocateDeviceMemory(VkBuffer buffer, VkMemoryPropertyFlags mem_flags)
	{
		VkMemoryRequirements mem_req = {};
		vkGetBufferMemoryRequirements(vk_inst.device, buffer, &mem_req);

		VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc_info.allocationSize = mem_req.size;
		alloc_info.memoryTypeIndex = FindMemoryType(mem_req.memoryTypeBits, mem_flags);
		
		VkDeviceMemory device_memory = VK_NULL_HANDLE;
		VkCheckResult(vkAllocateMemory(vk_inst.device, &alloc_info, nullptr, &device_memory));
		VkCheckResult(vkBindBufferMemory(vk_inst.device, buffer, device_memory, 0));

		return device_memory;
	}

	VkDeviceMemory AllocateDeviceMemory(VkImage image, VkMemoryPropertyFlags mem_flags)
	{
		VkMemoryRequirements mem_req = {};
		vkGetImageMemoryRequirements(vk_inst.device, image, &mem_req);

		VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc_info.allocationSize = mem_req.size;
		alloc_info.memoryTypeIndex = FindMemoryType(mem_req.memoryTypeBits, mem_flags);

		VkDeviceMemory device_memory = VK_NULL_HANDLE;
		VkCheckResult(vkAllocateMemory(vk_inst.device, &alloc_info, nullptr, &device_memory));
		VkCheckResult(vkBindImageMemory(vk_inst.device, image, device_memory, 0));

		return device_memory;
	}

	void FreeDeviceMemory(VkDeviceMemory device_memory)
	{
		vkFreeMemory(vk_inst.device, device_memory, nullptr);
	}

	uint8_t* MapMemory(VkDeviceMemory device_memory, VkDeviceSize size, VkDeviceSize offset)
	{
		uint8_t* mapped_ptr = nullptr;
		VkCheckResult(vkMapMemory(vk_inst.device, device_memory, offset, size, 0, reinterpret_cast<void**>(&mapped_ptr)));
		
		return mapped_ptr;
	}

	void UnmapMemory(VkDeviceMemory device_memory)
	{
		vkUnmapMemory(vk_inst.device, device_memory);
	}

	DescriptorAllocation AllocateDescriptors(VkDescriptorType type, uint32_t num_descriptors, uint32_t align)
	{
		switch (type)
		{
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			return data->descriptor_buffers.uniform.Allocate(num_descriptors, align);
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			return data->descriptor_buffers.storage_buffer.Allocate(num_descriptors, align);
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			return data->descriptor_buffers.storage_image.Allocate(num_descriptors, align);
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			return data->descriptor_buffers.sampled_image.Allocate(num_descriptors, align);
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			return data->descriptor_buffers.sampler.Allocate(num_descriptors, align);
		default:
			VK_EXCEPT("Vulkan::AllocateDescriptors", "Tried to allocate descriptors for a descriptor type that is not supported");
		}
	}

	void FreeDescriptors(const DescriptorAllocation& descriptors)
	{
		if (descriptors.IsNull())
			return;

		switch (descriptors.GetType())
		{
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			return data->descriptor_buffers.uniform.Free(descriptors);
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			return data->descriptor_buffers.storage_buffer.Free(descriptors);
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			return data->descriptor_buffers.storage_image.Free(descriptors);
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			return data->descriptor_buffers.sampled_image.Free(descriptors);
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			return data->descriptor_buffers.sampler.Free(descriptors);
		default:
			VK_EXCEPT("Vulkan::AllocateDescriptors", "Tried to allocate descriptors for a descriptor type that is not supported");
		}
	}

	std::vector<VkDescriptorSetLayout> GetDescriptorBufferLayouts()
	{
		return std::vector<VkDescriptorSetLayout>
		{
			data->descriptor_buffers.uniform.GetDescriptorSetLayout(),
			data->descriptor_buffers.storage_buffer.GetDescriptorSetLayout(),
			data->descriptor_buffers.storage_image.GetDescriptorSetLayout(),
			data->descriptor_buffers.sampled_image.GetDescriptorSetLayout(),
			data->descriptor_buffers.sampler.GetDescriptorSetLayout()
		};
	}

	std::vector<VkDescriptorBufferBindingInfoEXT> GetDescriptorBufferBindingInfos()
	{
		return std::vector<VkDescriptorBufferBindingInfoEXT>
		{
			data->descriptor_buffers.uniform.GetBindingInfo(),
			data->descriptor_buffers.storage_buffer.GetBindingInfo(),
			data->descriptor_buffers.storage_image.GetBindingInfo(),
			data->descriptor_buffers.sampled_image.GetBindingInfo(),
			data->descriptor_buffers.sampler.GetBindingInfo()
		};
	}

	size_t GetDescriptorTypeSize(VkDescriptorType type)
	{
		switch (type)
		{
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			return vk_inst.descriptor_sizes.uniform_buffer;
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			return vk_inst.descriptor_sizes.storage_buffer;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			return vk_inst.descriptor_sizes.storage_image;
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			return vk_inst.descriptor_sizes.sampled_image;
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			return vk_inst.descriptor_sizes.sampler;
		default:
			VK_EXCEPT("Vulkan::GetDescriptorTypeSize", "Tried to retrieve descriptor size for a descriptor type that is not supported");
		}
	}

	VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage_flags)
	{
		VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		buffer_info.size = size;
		buffer_info.usage = usage_flags;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer buffer = VK_NULL_HANDLE;
		VkCheckResult(vkCreateBuffer(vk_inst.device, &buffer_info, nullptr, &buffer));

		VulkanResourceTracker::TrackBuffer(buffer);
		return buffer;
	}

	void DestroyBuffer(VkBuffer buffer)
	{
		VulkanResourceTracker::RemoveBuffer(buffer);
		vkDestroyBuffer(vk_inst.device, buffer, nullptr);
	}

	VkImage CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
		VkImageUsageFlags usage, uint32_t num_mips, uint32_t num_layers, VkImageCreateFlags create_flags)
	{
		VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.extent.width = width;
		image_info.extent.height = height;
		image_info.extent.depth = 1;
		image_info.mipLevels = num_mips;
		image_info.arrayLayers = num_layers;
		image_info.format = format;
		image_info.tiling = tiling;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_info.usage = usage;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_info.flags = create_flags;

		VkImage image = VK_NULL_HANDLE;
		VkCheckResult(vkCreateImage(vk_inst.device, &image_info, nullptr, &image));

		VulkanResourceTracker::TrackImage(image, image_info.initialLayout, num_mips, num_layers);
		return image;
	}

	void DestroyImage(VkImage image)
	{
		vkDestroyImage(vk_inst.device, image, nullptr);
	}

	void GenerateMips(VkImage image, VkFormat format, uint32_t width, uint32_t height, uint32_t num_mips)
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
		barrier.image = image;
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
				image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
		VulkanResourceTracker::UpdateImageLayout(image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
	}

	VkImageView CreateImageView(VkImage image, VkImageViewType view_type, VkFormat format, uint32_t base_mip, uint32_t num_mips, uint32_t base_layer, uint32_t num_layers)
	{
		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image;
		view_info.viewType = view_type;
		view_info.format = format;
		//view_info.components.r = VK_COMPONENT_SWIZZLE_R;
		//view_info.components.g = VK_COMPONENT_SWIZZLE_G;
		//view_info.components.b = VK_COMPONENT_SWIZZLE_B;
		//view_info.components.a = VK_COMPONENT_SWIZZLE_A;
		if (format == VK_FORMAT_D32_SFLOAT)
			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		else
			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_info.subresourceRange.baseMipLevel = base_mip;
		view_info.subresourceRange.levelCount = num_mips;
		view_info.subresourceRange.baseArrayLayer = base_layer;
		view_info.subresourceRange.layerCount = num_layers;
		view_info.flags = 0;

		VkImageView image_view = VK_NULL_HANDLE;
		VkCheckResult(vkCreateImageView(vk_inst.device, &view_info, nullptr, &image_view));

		return image_view;
	}

	void DestroyImageView(VkImageView image_view)
	{
		vkDestroyImageView(vk_inst.device, image_view, nullptr);
	}

	VkSampler CreateSampler(const VkSamplerCreateInfo& sampler_info)
	{
		VkSampler sampler = VK_NULL_HANDLE;
		VkCheckResult(vkCreateSampler(vk_inst.device, &sampler_info, nullptr, &sampler));

		return sampler;
	}

	void DestroySampler(VkSampler sampler)
	{
		vkDestroySampler(vk_inst.device, sampler, nullptr);
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

	void CmdImageMemoryBarrier(VkCommandBuffer command_buffer, uint32_t num_barriers, const VkImageMemoryBarrier2* image_barriers)
	{
		VkDependencyInfo dependency_info = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency_info.imageMemoryBarrierCount = num_barriers;
		dependency_info.pImageMemoryBarriers = image_barriers;
		dependency_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		vkCmdPipelineBarrier2(command_buffer, &dependency_info);

		for (uint32_t i = 0; i < num_barriers; ++i)
		{
			VulkanResourceTracker::UpdateImageLayout(image_barriers[i].image, image_barriers[i].newLayout,
				image_barriers[i].subresourceRange.baseMipLevel, image_barriers[i].subresourceRange.levelCount,
				image_barriers[i].subresourceRange.baseArrayLayer, image_barriers[i].subresourceRange.layerCount);
		}
	}

	void ImageMemoryBarrierImmediate(uint32_t num_barriers, const VkImageMemoryBarrier2* image_barriers)
	{
		VkCommandBuffer command_buffer = BeginImmediateCommand();
		CmdImageMemoryBarrier(command_buffer, num_barriers, image_barriers);
		EndImmediateCommand(command_buffer);
	}

	VkPipelineLayout CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts, const std::vector<VkPushConstantRange>& push_constant_ranges)
	{
		VkPipelineLayoutCreateInfo pipeline_layout_info = {};
		pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.setLayoutCount = (uint32_t)descriptor_set_layouts.size();
		pipeline_layout_info.pSetLayouts = descriptor_set_layouts.data();
		pipeline_layout_info.pushConstantRangeCount = (uint32_t)push_constant_ranges.size();
		pipeline_layout_info.pPushConstantRanges = push_constant_ranges.data();

		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		vkCreatePipelineLayout(vk_inst.device, &pipeline_layout_info, nullptr, &pipeline_layout);

		return pipeline_layout;
	}

	VkPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info, VkPipelineLayout pipeline_layout)
	{
		// TODO: Vulkan extension for shader objects? No longer need to make compiled pipeline states then
		// https://www.khronos.org/blog/you-can-use-vulkan-without-pipelines-today
		std::vector<uint32_t> vert_spv = CompileShader(info.vs_path, shaderc_vertex_shader);
		std::vector<uint32_t> frag_spv = CompileShader(info.fs_path, shaderc_fragment_shader);

		VkShaderModule vert_shader_module = CreateShaderModule(vert_spv);
		VkShaderModule frag_shader_module = CreateShaderModule(frag_spv);

		VkPipelineShaderStageCreateInfo vert_shader_stage_info = {};
		vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vert_shader_stage_info.module = vert_shader_module;
		vert_shader_stage_info.pName = "main";

		VkPipelineShaderStageCreateInfo frag_shader_stage_info = {};
		frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		frag_shader_stage_info.module = frag_shader_module;
		frag_shader_stage_info.pName = "main";

		VkPipelineShaderStageCreateInfo shader_stages[] = { vert_shader_stage_info, frag_shader_stage_info };

		// TODO: Vertex pulling, we won't need vertex input layouts, or maybe even mesh shaders
		// https://www.khronos.org/blog/mesh-shading-for-vulkan
		VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
		vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_info.vertexBindingDescriptionCount = (uint32_t)info.input_bindings.size();
		vertex_input_info.pVertexBindingDescriptions = info.input_bindings.data();
		vertex_input_info.vertexAttributeDescriptionCount = (uint32_t)info.input_attributes.size();
		vertex_input_info.pVertexAttributeDescriptions = info.input_attributes.data();

		VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {};
		input_assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		input_assembly_info.primitiveRestartEnable = VK_FALSE;

		std::vector<VkDynamicState> dynamic_states =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamic_state = {};
		dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_state.dynamicStateCount = (uint32_t)dynamic_states.size();
		dynamic_state.pDynamicStates = dynamic_states.data();

		VkPipelineViewportStateCreateInfo viewport_state = {};
		viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_state.viewportCount = 1;
		viewport_state.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = info.cull_mode;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f;
		rasterizer.depthBiasClamp = 0.0f;
		rasterizer.depthBiasSlopeFactor = 0.0f;

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f;
		multisampling.pSampleMask = nullptr;
		multisampling.alphaToCoverageEnable = VK_FALSE;
		multisampling.alphaToOneEnable = VK_FALSE;

		VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
		depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth_stencil.depthTestEnable = info.depth_test;
		depth_stencil.depthWriteEnable = info.depth_write;
		depth_stencil.depthCompareOp = info.depth_func;
		depth_stencil.depthBoundsTestEnable = VK_FALSE;
		depth_stencil.minDepthBounds = 0.0f;
		depth_stencil.maxDepthBounds = 1.0f;
		depth_stencil.stencilTestEnable = VK_FALSE;
		depth_stencil.front = {};
		depth_stencil.back = {};

		std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachments(info.color_attachment_formats.size());
		for (auto& color_blend_attachment : color_blend_attachments)
		{
			color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
				VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			color_blend_attachment.blendEnable = VK_FALSE;// VK_TRUE;
			color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
			color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
		}

		VkPipelineColorBlendStateCreateInfo color_blend = {};
		color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend.logicOpEnable = VK_FALSE;
		color_blend.logicOp = VK_LOGIC_OP_COPY;
		color_blend.attachmentCount = (uint32_t)info.color_attachment_formats.size();
		color_blend.pAttachments = color_blend_attachments.data();
		color_blend.blendConstants[0] = 0.0f;
		color_blend.blendConstants[1] = 0.0f;
		color_blend.blendConstants[2] = 0.0f;
		color_blend.blendConstants[3] = 0.0f;

		VkGraphicsPipelineCreateInfo pipeline_info = {};
		pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.stageCount = 2;
		pipeline_info.pStages = shader_stages;
		pipeline_info.pVertexInputState = &vertex_input_info;
		pipeline_info.pInputAssemblyState = &input_assembly_info;
		pipeline_info.pViewportState = &viewport_state;
		pipeline_info.pRasterizationState = &rasterizer;
		pipeline_info.pMultisampleState = &multisampling;
		pipeline_info.pDepthStencilState = &depth_stencil;
		pipeline_info.pColorBlendState = &color_blend;
		pipeline_info.pDynamicState = &dynamic_state;
		pipeline_info.layout = pipeline_layout;
		pipeline_info.renderPass = VK_NULL_HANDLE;
		pipeline_info.subpass = 0;
		pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_info.basePipelineIndex = -1;
		pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

		VkPipelineRenderingCreateInfo pipeline_rendering_info = {};
		pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		pipeline_rendering_info.colorAttachmentCount = (uint32_t)info.color_attachment_formats.size();
		pipeline_rendering_info.pColorAttachmentFormats = info.color_attachment_formats.data();
		pipeline_rendering_info.depthAttachmentFormat = info.depth_stencil_attachment_format;
		pipeline_rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
		pipeline_rendering_info.viewMask = 0;
		pipeline_info.pNext = &pipeline_rendering_info;

		VkPipeline pipeline = VK_NULL_HANDLE;
		VkCheckResult(vkCreateGraphicsPipelines(vk_inst.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));

		vkDestroyShaderModule(vk_inst.device, frag_shader_module, nullptr);
		vkDestroyShaderModule(vk_inst.device, vert_shader_module, nullptr);

		return pipeline;
	}

	VkPipeline CreateComputePipeline(const ComputePipelineInfo& info, VkPipelineLayout pipeline_layout)
	{
		std::vector<uint32_t> compute_spv = CompileShader(info.cs_path, shaderc_compute_shader);
		VkShaderModule compute_shader_module = CreateShaderModule(compute_spv);

		VkPipelineShaderStageCreateInfo compute_shader_stage_info = {};
		compute_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		compute_shader_stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		compute_shader_stage_info.module = compute_shader_module;
		compute_shader_stage_info.pName = "main";

		VkComputePipelineCreateInfo pipeline_info = {};
		pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline_info.layout = pipeline_layout;
		pipeline_info.stage = compute_shader_stage_info;
		pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

		VkPipeline pipeline = VK_NULL_HANDLE;
		VkCheckResult(vkCreateComputePipelines(vk_inst.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));

		vkDestroyShaderModule(vk_inst.device, compute_shader_module, nullptr);

		return pipeline;
	}

}
