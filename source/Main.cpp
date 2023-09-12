#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"
#include "GLFW/glfw3.h"

#include "Allocator.h"

#include <assert.h>
#include <cstring>
#include <vector>

static inline void VkCheckResult(VkResult result)
{
	if (result != VK_SUCCESS)
	{
		printf("Vulkan error: %s (%i)\n", string_VkResult(result), result);
		assert(result == VK_SUCCESS);
	}
}

const uint32_t DEFAULT_WINDOW_WIDTH = 1280;
const uint32_t DEFAULT_WINDOW_HEIGHT = 720;

static GLFWwindow* s_window = nullptr;

static const char* s_validation_layers = "VK_LAYER_KHRONOS_validation";
#ifdef _DEBUG
static const bool s_enable_validation_layers = true;
#else
static const bool s_enable_validation_layers = false;
#endif

static Allocator s_alloc;

static VkInstance s_instance = nullptr;
static VkDebugUtilsMessengerEXT s_debug_messenger = nullptr;

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
	printf("[Vulkan validation layer] %s\n", callback_data->pMessage);
	return VK_FALSE;
}

static void InitVulkan()
{
	if (s_enable_validation_layers)
	{
		// Enable validation layers
		uint32_t layer_count = 0;
		VkCheckResult(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));

		VkLayerProperties* available_layers = (VkLayerProperties*)s_alloc.Allocate(layer_count * sizeof(VkLayerProperties));
		VkCheckResult(vkEnumerateInstanceLayerProperties(&layer_count, available_layers));

		bool layer_found = false;
		for (uint32_t i = 0; i < layer_count; ++i)
		{
			if (strcmp(s_validation_layers, available_layers[i].layerName) == 0)
			{
				layer_found = true;
				break;
			}
		}

		assert(layer_found);
		s_alloc.Release(available_layers);
	}

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

	if (s_enable_validation_layers)
	{
		create_info.enabledLayerCount = 1;
		create_info.ppEnabledLayerNames = &s_validation_layers;

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

	s_instance = (VkInstance)s_alloc.Allocate(sizeof(VkInstance));
	VkCheckResult(vkCreateInstance(&create_info, nullptr, &s_instance));

	// Create debug messenger
	if (s_enable_validation_layers)
	{
		VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {};
		debug_utils_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debug_utils_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug_utils_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debug_utils_create_info.pfnUserCallback = VkDebugCallback;
		debug_utils_create_info.pUserData = nullptr;

		auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkCreateDebugUtilsMessengerEXT");
		assert(func);
		if (func != nullptr)
		{
			VkCheckResult(func(s_instance, &debug_utils_create_info, nullptr, &s_debug_messenger));
		}
	}

	// Check extensions
	uint32_t extension_count = 0;
	VkCheckResult(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr));
	VkExtensionProperties* extensions = (VkExtensionProperties*)s_alloc.Allocate(extension_count * sizeof(VkExtensionProperties));
	VkCheckResult(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions));

	for (uint32_t i = 0; i < extension_count; ++i)
	{
		printf("[Vulkan] Extension found: %s\n", extensions[i].extensionName);
	}
	s_alloc.Release(extensions);
}

static void ExitVulkan()
{
	vkDestroyInstance(s_instance, nullptr);
	s_alloc.Release(s_instance);
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