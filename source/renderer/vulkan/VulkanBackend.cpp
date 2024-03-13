#include "Precomp.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanSwapChain.h"
#include "renderer/vulkan/VulkanCommandQueue.h"
#include "renderer/vulkan/VulkanCommandPool.h"
#include "renderer/vulkan/VulkanCommandBuffer.h"
#include "renderer/vulkan/VulkanCommands.h"
#include "renderer/vulkan/VulkanUtils.h"
#include "renderer/vulkan/VulkanResourceTracker.h"
#include "renderer/DescriptorBuffer.h"

#include "shaderc/shaderc.hpp"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"

namespace Vulkan
{

	VulkanInstance vk_inst;

	/*
		============================ PRIVATE FUNCTIONS =================================
	*/

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

#ifdef ENABLE_VK_DEBUG_LAYER
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

		return extensions;
	}

	static void CreateInstance()
	{
		VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app_info.pApplicationName = "VulkanRenderer";
		app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
		app_info.pEngineName = "No Engine";
		app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
		app_info.apiVersion = VK_API_VERSION_1_3;
		app_info.pNext = nullptr;

		VkInstanceCreateInfo instance_create_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instance_create_info.pApplicationInfo = &app_info;
		instance_create_info.flags = 0;
		instance_create_info.pNext = nullptr;

		std::vector<const char*> required_extensions = GetRequiredExtensions();
		instance_create_info.enabledExtensionCount = (uint32_t)required_extensions.size();
		instance_create_info.ppEnabledExtensionNames = required_extensions.data();

#ifdef ENABLE_VK_DEBUG_LAYER
		instance_create_info.enabledLayerCount = (uint32_t)vk_inst.debug.validation_layers.size();
		instance_create_info.ppEnabledLayerNames = vk_inst.debug.validation_layers.data();

		// Init debug messenger for vulkan instance creation
		VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debug_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debug_messenger_create_info.pfnUserCallback = VkDebugCallback;
		debug_messenger_create_info.pUserData = nullptr;
		debug_messenger_create_info.pNext = nullptr;
		debug_messenger_create_info.flags = 0;

		instance_create_info.pNext = &debug_messenger_create_info;
#endif

		VkCheckResult(vkCreateInstance(&instance_create_info, nullptr, &vk_inst.instance));
	}

	static void EnableValidationLayers()
	{
#ifdef ENABLE_VK_DEBUG_LAYER
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
#endif
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

			VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain_maintenance_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };
			timeline_semaphore_features.pNext = &swapchain_maintenance_features;

			VkPhysicalDeviceFeatures2 device_features2 = {};
			device_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			device_features2.pNext = &descriptor_buffer_features;

			vkGetPhysicalDeviceFeatures2(device, &device_features2);

			if (device_properties2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
				required_extensions.empty() &&
				device_features2.features.samplerAnisotropy &&
				descriptor_buffer_features.descriptorBuffer &&
				buffer_device_address_features.bufferDeviceAddress &&
				buffer_device_address_features.bufferDeviceAddressCaptureReplay &&
				maintenance4_features.maintenance4 &&
				dynamic_rendering_features.dynamicRendering &&
				timeline_semaphore_features.timelineSemaphore &&
				swapchain_maintenance_features.swapchainMaintenance1)
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

	static void FindQueueIndices()
	{
		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(vk_inst.physical_device, &queue_family_count, nullptr);

		std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(vk_inst.physical_device, &queue_family_count, queue_families.data());

		int32_t i = 0;
		for (const auto& queue_family : queue_families)
		{
			VkBool32 present_supported = false;
			VkCheckResult(vkGetPhysicalDeviceSurfaceSupportKHR(vk_inst.physical_device, i, vk_inst.swapchain.surface, &present_supported));

			/*if (present_supported)
			{
				vk_inst.queues.present.vk_queue_index = i;
			}*/
			// Check queue for graphics and compute capabilities
			if ((queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT))
			{
				vk_inst.queues.graphics_compute.queue_family_index = i;
			}
			if (queue_family.queueFlags & VK_QUEUE_TRANSFER_BIT)
			{
				vk_inst.queues.transfer.queue_family_index = i;
			}

			if (//vk_inst.queues.present.vk_queue_index != ~0u &&
				vk_inst.queues.graphics_compute.queue_family_index != ~0u &&
				vk_inst.queues.transfer.queue_family_index != ~0u)
				break;

			i++;
		}
	}

	static void CreateDevice()
	{
		FindQueueIndices();

		std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
		std::set<uint32_t> unique_queue_families = { //vk_inst.queues.present.vk_queue_index,
			vk_inst.queues.graphics_compute.queue_family_index, vk_inst.queues.transfer.queue_family_index };
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

#ifdef ENABLE_VK_DEBUG_LAYER
		device_create_info.enabledLayerCount = (uint32_t)vk_inst.debug.validation_layers.size();
		device_create_info.ppEnabledLayerNames = vk_inst.debug.validation_layers.data();
#endif

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

		VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain_maintenance_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };
		timeline_semaphore_features.pNext = &swapchain_maintenance_features;

		VkPhysicalDeviceFeatures2 device_features2 = {};
		device_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		device_features2.features.samplerAnisotropy = VK_TRUE;
		device_features2.pNext = &descriptor_indexing_features;
		vkGetPhysicalDeviceFeatures2(vk_inst.physical_device, &device_features2);
		device_create_info.pNext = &device_features2;

#ifdef ENABLE_VK_DEBUG_LAYER
		device_create_info.enabledLayerCount = (uint32_t)vk_inst.debug.validation_layers.size();
		device_create_info.ppEnabledLayerNames = vk_inst.debug.validation_layers.data();
#else
		device_create_info.enabledLayerCount = 0;
#endif

		VkCheckResult(vkCreateDevice(vk_inst.physical_device, &device_create_info, nullptr, &vk_inst.device));

		// Create queues
		//vk_inst.queues.present = CreateCommandQueue(vk_inst.queues.present.vk_queue_index, 0);
		vk_inst.queues.graphics_compute = CommandQueue::Create(VULKAN_COMMAND_BUFFER_TYPE_GRAPHICS_COMPUTE, vk_inst.queues.graphics_compute.queue_family_index, 0);
		vk_inst.queues.transfer = CommandQueue::Create(VULKAN_COMMAND_BUFFER_TYPE_TRANSFER, vk_inst.queues.transfer.queue_family_index, 0);

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
			DescriptorBuffer uniform{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RESERVED_DESCRIPTOR_UBO_COUNT * Vulkan::MAX_FRAMES_IN_FLIGHT };
			DescriptorBuffer storage_buffer{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
			DescriptorBuffer storage_image{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
			DescriptorBuffer sampled_image{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE };
			DescriptorBuffer sampler{ VK_DESCRIPTOR_TYPE_SAMPLER };

			std::vector<uint32_t> indices = { 0, 1, 2, 3, 4 };
			std::vector<uint64_t> offsets = { 0, 0, 0, 0, 0 };
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

	/*
		============================ PUBLIC INTERFACE FUNCTIONS =================================
	*/

	void Init(::GLFWwindow* window, uint32_t window_width, uint32_t window_height)
	{
		CreateInstance();
		EnableValidationLayers();

		VkCheckResult(glfwCreateWindowSurface(vk_inst.instance, window, nullptr, &vk_inst.swapchain.surface));
		ResourceTracker::Init();

		CreatePhysicalDevice();
		CreateDevice();
		SwapChain::Create(window_width, window_height);

		data = new Data();
	}

	void Exit()
	{
		//vkQueueWaitIdle(vk_inst.queues.present.vk_queue);
		vkQueueWaitIdle(vk_inst.queues.graphics_compute.vk_queue);
		vkQueueWaitIdle(vk_inst.queues.transfer.vk_queue);

		delete data;

		SwapChain::Destroy();
		vkDestroySurfaceKHR(vk_inst.instance, vk_inst.swapchain.surface, nullptr);

#ifdef ENABLE_VK_DEBUG_LAYER
		auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vk_inst.instance, "vkDestroyDebugUtilsMessengerEXT");
		if (!func)
		{
			VK_EXCEPT("Vulkan", "Could not find function pointer vkDestroyDebugUtilsMessengerEXT");
		}

		func(vk_inst.instance, vk_inst.debug.debug_messenger, nullptr);
#endif

		vkDestroyDevice(vk_inst.device, nullptr);
		vkDestroyInstance(vk_inst.instance, nullptr);

		ResourceTracker::Exit();
	}

	void BeginFrame()
	{
		// Get the next available image from the swapchain
		VkResult result = Vulkan::SwapChain::AcquireNextImage();
		if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
		{
			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				Vulkan::ResizeOutputResolution(data->output_resolution.width, data->output_resolution.height);
				CreateRenderTargets();
			}

			return;
		}
		else
		{
			VkCheckResult(result);
		}

		// ImGui (backends) new frame
		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplVulkan_NewFrame();
		ImGui::NewFrame();

		// Update UBO descriptor buffer offset
		// We need a UBO per in-flight frame, so we need to update the offset at which we want to bind our UBOs from the descriptor buffer
		data->descriptor_buffers.offsets[DESCRIPTOR_SET_UBO] = VK_ALIGN_POW2(
			RESERVED_DESCRIPTOR_UBO_COUNT * vk_inst.current_frame_index * vk_inst.descriptor_sizes.uniform_buffer,
			vk_inst.device_props.descriptor_buffer_offset_alignment
		);
	}

	void EndFrame()
	{
		// Present
		std::vector<VkSemaphore> present_wait_semaphore = { frame->sync.render_finished_semaphore_binary };
		VkResult result = Vulkan::SwapChain::Present(present_wait_semaphore);

		if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR))
		{
			Vulkan::ResizeOutputResolution(data->output_resolution.width, data->output_resolution.height);
			CreateRenderTargets();

			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				return;
			}
		}
		else
		{
			VkCheckResult(result);
		}

		vk_inst.current_frame_index = (vk_inst.current_frame_index + 1) % Vulkan::MAX_FRAMES_IN_FLIGHT;

		ResourceTracker::ReleaseStaleTempResources();
	}

	void ResizeOutputResolution(uint32_t output_width, uint32_t output_height)
	{
		if (output_width == 0 || output_height == 0)
		{
			return;
		}

		vkDeviceWaitIdle(vk_inst.device);

		SwapChain::Destroy();
		SwapChain::Create(output_width, output_height);
	}

	void WaitDeviceIdle()
	{
		vkDeviceWaitIdle(vk_inst.device);
	}

	uint32_t GetCurrentBackBufferIndex()
	{
		return vk_inst.swapchain.current_image;
	}

	uint32_t GetCurrentFrameIndex()
	{
		return vk_inst.current_frame_index;
	}

	uint32_t GetLastFinishedFrameIndex()
	{
		// TODO: Could probably use a fence value here instead that signals the finish of a frame
		return std::max(0, (int32_t)vk_inst.current_frame_index - (int32_t)Vulkan::MAX_FRAMES_IN_FLIGHT);
	}

	VulkanCommandQueue GetCommandQueue(VulkanCommandBufferType type)
	{
		switch (type)
		{
		case VULKAN_COMMAND_BUFFER_TYPE_GRAPHICS_COMPUTE:
			return vk_inst.queues.graphics_compute;
		case VULKAN_COMMAND_BUFFER_TYPE_TRANSFER:
			return vk_inst.queues.transfer;
		default:
			VK_EXCEPT("Vulkan::GetCommandQueue", "Tried to retrieve a command queue for an unknown type");
		}
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

	std::vector<uint32_t> GetDescriptorBufferIndices()
	{
		return data->descriptor_buffers.indices;
	}

	std::vector<uint64_t> GetDescriptorBufferOffsets()
	{
		return data->descriptor_buffers.offsets;
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

	VulkanSampler CreateSampler(const SamplerCreateInfo& sampler_info)
	{
		VkSamplerCreateInfo vk_sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		vk_sampler_info.addressModeU = Util::ToVkAddressMode(sampler_info.address_u);
		vk_sampler_info.addressModeV = Util::ToVkAddressMode(sampler_info.address_v);
		vk_sampler_info.addressModeW = Util::ToVkAddressMode(sampler_info.address_w);
		vk_sampler_info.borderColor = Util::ToVkBorderColor(sampler_info.border_color);

		vk_sampler_info.minFilter = Util::ToVkFilter(sampler_info.filter_min);
		vk_sampler_info.magFilter = Util::ToVkFilter(sampler_info.filter_mag);
		vk_sampler_info.mipmapMode = Util::ToVkSamplerMipmapMode(sampler_info.filter_mip);

		vk_sampler_info.anisotropyEnable = static_cast<VkBool32>(sampler_info.enable_anisotropy);
		vk_sampler_info.maxAnisotropy = vk_inst.device_props.max_anisotropy;

		vk_sampler_info.minLod = sampler_info.min_lod;
		vk_sampler_info.maxLod = sampler_info.max_lod;

		VkSampler vk_sampler = VK_NULL_HANDLE;
		VkCheckResult(vkCreateSampler(vk_inst.device, &vk_sampler_info, nullptr, &vk_sampler));

		VulkanSampler sampler = {};
		sampler.vk_sampler = vk_sampler;

		return sampler;
	}

	void DestroySampler(VulkanSampler sampler)
	{
		vkDestroySampler(vk_inst.device, sampler.vk_sampler, nullptr);
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
			color_blend_attachment.blendEnable = VK_TRUE;
			color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
			color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
		std::vector<VkFormat> color_attachment_formats = Util::ToVkFormats(info.color_attachment_formats);
		pipeline_rendering_info.pColorAttachmentFormats = color_attachment_formats.data();
		pipeline_rendering_info.depthAttachmentFormat = Util::ToVkFormat(info.depth_stencil_attachment_format);
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

	void InitImGui(::GLFWwindow* window, const VulkanCommandBuffer& command_buffer)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

		// Create imgui descriptor pool
		// First descriptor is for the font, the other ones for descriptor sets for ImGui::Image calls
		VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 + 1000 };
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		// First descriptor is for the font, the other ones for descriptor sets for ImGui::Image calls
		pool_info.maxSets = 1 + 1000;
		pool_info.poolSizeCount = 1;
		pool_info.pPoolSizes = &pool_size;
		VkCheckResult(vkCreateDescriptorPool(vk_inst.device, &pool_info, nullptr, &vk_inst.imgui.descriptor_pool));

		// Init imgui
		ImGui_ImplGlfw_InitForVulkan(window, true);
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = vk_inst.instance;
		init_info.PhysicalDevice = vk_inst.physical_device;
		init_info.Device = vk_inst.device;
		init_info.QueueFamily = vk_inst.queues.graphics_compute.queue_family_index;
		init_info.Queue = vk_inst.queues.graphics_compute.vk_queue;
		init_info.PipelineCache = VK_NULL_HANDLE;
		init_info.DescriptorPool = vk_inst.imgui.descriptor_pool;
		init_info.MinImageCount = Vulkan::MAX_FRAMES_IN_FLIGHT;
		init_info.ImageCount = Vulkan::MAX_FRAMES_IN_FLIGHT;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.Allocator = nullptr;
		init_info.UseDynamicRendering = true;
		init_info.ColorAttachmentFormat = VK_FORMAT_R8G8B8A8_UNORM;
		init_info.CheckVkResultFn = VkCheckResult;
		ImGui_ImplVulkan_Init(&init_info, nullptr);

		// Upload imgui font
		ImGui_ImplVulkan_CreateFontsTexture(command_buffer.vk_command_buffer);

		ImGui_ImplVulkan_DestroyFontUploadObjects();
	}

	void ExitImGui()
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		vkDestroyDescriptorPool(vk_inst.device, vk_inst.imgui.descriptor_pool, nullptr);
	}

	VkDescriptorSet AddImGuiTexture(VkImage image, VkImageView image_view, VkSampler sampler)
	{
		return ImGui_ImplVulkan_AddTexture(sampler, image_view, ResourceTracker::GetImageLayout({ image }));
	}

}
