#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"
#include "GLFW/glfw3.h"

#include "Allocator.h"

#include <assert.h>
#include <cstring>
#include <vector>
#include <optional>

#define VK_ASSERT(x) assert(x)
#define VK_VERIFY(x) (VK_ASSERT(x), x)
#define VK_VERIFY_NOT(x) (VK_ASSERT(!(x)), x)

static inline void VkCheckResult(VkResult result)
{
	if (result != VK_SUCCESS)
	{
		printf("[Vulkan] ERROR: %s (%i)\n", string_VkResult(result), result);
		VK_ASSERT(result == VK_SUCCESS);
	}
}

const uint32_t DEFAULT_WINDOW_WIDTH = 1280;
const uint32_t DEFAULT_WINDOW_HEIGHT = 720;

static GLFWwindow* s_window = nullptr;

static std::vector<const char*> s_validation_layers = { "VK_LAYER_KHRONOS_validation" };
#ifdef _DEBUG
static const bool s_enable_validation_layers = true;
#else
static const bool s_enable_validation_layers = false;
#endif

static Allocator s_alloc;

static VkInstance s_instance = VK_NULL_HANDLE;
static VkDebugUtilsMessengerEXT s_debug_messenger = VK_NULL_HANDLE;
static VkPhysicalDevice s_physical_device = VK_NULL_HANDLE;
static VkDevice s_device = VK_NULL_HANDLE;
static VkQueue s_graphics_queue = VK_NULL_HANDLE;

static void CreateWindow()
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	s_window = glfwCreateWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "VulkanRenderer", nullptr, nullptr);
}

static void DestroyWindow()
{
	glfwDestroyWindow(s_window);
	glfwTerminate();
}

static std::vector<const char*> GetRequiredExtensions()
{
	uint32_t glfw_extension_count = 0;
	const char** glfw_extensions;
	glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

	std::vector<const char*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

	if (s_enable_validation_layers)
	{
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	return extensions;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VkDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
	printf("[Vulkan Validation Layer] %s\n", callback_data->pMessage);
	return VK_FALSE;
}

static void InitVulkan()
{
	// ---------------------------------------------------------------------------------------------------
	// Create vulkan instance

	VkApplicationInfo app_info = {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "VulkanRenderer";
	app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
	app_info.pEngineName = "No Engine";
	app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
	app_info.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;

	std::vector<const char*> required_extensions = GetRequiredExtensions();
	create_info.enabledExtensionCount = (uint32_t)required_extensions.size();
	create_info.ppEnabledExtensionNames = required_extensions.data();

	// ---------------------------------------------------------------------------------------------------
	// Enable validation layer for vulkan instance creation

	if (s_enable_validation_layers)
	{
		create_info.enabledLayerCount = s_validation_layers.size();
		create_info.ppEnabledLayerNames = s_validation_layers.data();

		// Init debug messenger for vulkan instance creation
		VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = {};
		debug_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debug_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debug_messenger_create_info.pfnUserCallback = VkDebugCallback;
		debug_messenger_create_info.pUserData = nullptr;
		create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debug_messenger_create_info;
	}

	VkCheckResult(vkCreateInstance(&create_info, nullptr, &s_instance));

	if (s_enable_validation_layers)
	{
		// ---------------------------------------------------------------------------------------------------
		// Enable validation layers

		uint32_t layer_count = 0;
		VkCheckResult(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));

		std::vector<VkLayerProperties> available_layers(layer_count);
		VkCheckResult(vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data()));

		for (const auto& validation_layer : s_validation_layers)
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

		// ---------------------------------------------------------------------------------------------------
		// Create debug messenger with custom message callback

		VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {};
		debug_utils_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debug_utils_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug_utils_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debug_utils_create_info.pfnUserCallback = VkDebugCallback;
		debug_utils_create_info.pUserData = nullptr;

		auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkCreateDebugUtilsMessengerEXT");
		if (VK_VERIFY(func))
		{
			VkCheckResult(func(s_instance, &debug_utils_create_info, nullptr, &s_debug_messenger));
		}
	}

	// ---------------------------------------------------------------------------------------------------
	// Check available vulkan extensions

	uint32_t extension_count = 0;
	VkCheckResult(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr));

	std::vector<VkExtensionProperties> extensions(extension_count);
	VkCheckResult(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data()));

	for (const auto& extension : extensions)
	{
		printf("[Vulkan] Extension found: %s\n", extension.extensionName);
	}

	// ---------------------------------------------------------------------------------------------------
	// Select GPU device

	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(s_instance, &device_count, nullptr);
	if (VK_VERIFY_NOT(device_count == 0))
	{
		// TODO: Log error, no GPU devices found
		printf("[Vulkan] ERROR: No GPU devices found\n");
	}

	std::vector<VkPhysicalDevice> devices(device_count);
	vkEnumeratePhysicalDevices(s_instance, &device_count, devices.data());

	for (const auto& device : devices)
	{
		// TODO: Add checks to select the best GPU device
		VkPhysicalDeviceProperties device_properties;
		vkGetPhysicalDeviceProperties(device, &device_properties);

		VkPhysicalDeviceFeatures device_features;
		vkGetPhysicalDeviceFeatures(device, &device_features);

		if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			s_physical_device = device;
			break;
		}
	}

	if (VK_VERIFY_NOT(s_physical_device == VK_NULL_HANDLE))
	{
		// TODO: Log error, no suitable GPU devices found
		printf("[Vulkan] ERROR: No suitable GPU devices found\n");
	}

	// ---------------------------------------------------------------------------------------------------
	// Find queue families required

	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphics_family;
	};

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(s_physical_device, &queue_family_count, nullptr);

	std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(s_physical_device, &queue_family_count, queue_families.data());

	QueueFamilyIndices queue_family_indices;
	int i = 0;
	for (const auto& queue_family : queue_families)
	{
		if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			queue_family_indices.graphics_family = i;
		}

		if (queue_family_indices.graphics_family.has_value())
		{
			break;
		}

		i++;
	}

	if (VK_VERIFY_NOT(!queue_family_indices.graphics_family.has_value()))
	{
		printf("[Vulkan] ERROR: No graphics queue family found\n");
	}

	// ---------------------------------------------------------------------------------------------------
	// Create logical device and queues per queue family

	VkDeviceQueueCreateInfo queue_create_info = {};
	queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_info.queueFamilyIndex = queue_family_indices.graphics_family.value();
	queue_create_info.queueCount = 1;
	float queue_priority = 1.0;
	queue_create_info.pQueuePriorities = &queue_priority;

	// Note: We will fill this later when we need specific features
	VkPhysicalDeviceFeatures device_features = {};

	VkDeviceCreateInfo device_create_info = {};
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pEnabledFeatures = &device_features;
	device_create_info.enabledExtensionCount = 0;
	if (s_enable_validation_layers)
	{
		device_create_info.enabledLayerCount = (uint32_t)s_validation_layers.size();
		device_create_info.ppEnabledLayerNames = s_validation_layers.data();
	}
	else
	{
		device_create_info.enabledLayerCount = 0;
	}

	VkCheckResult(vkCreateDevice(s_physical_device, &device_create_info, nullptr, &s_device));
	vkGetDeviceQueue(s_device, queue_family_indices.graphics_family.value(), 0, &s_graphics_queue);
}

static void ExitVulkan()
{
	vkDestroyDevice(s_device, nullptr);
	if (s_enable_validation_layers)
	{
		auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkDestroyDebugUtilsMessengerEXT");
		if (VK_VERIFY(func))
		{
			func(s_instance, s_debug_messenger, nullptr);
		}
	}
	vkDestroyInstance(s_instance, nullptr);
}

int main(int argc, char* argv[])
{
	(void)argc; (void)argv;

	CreateWindow();
	InitVulkan();

	uint32_t extension_count;
	vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

	while (!glfwWindowShouldClose(s_window))
	{
		glfwPollEvents();
	}

	ExitVulkan();
	DestroyWindow();

	return 0;
}