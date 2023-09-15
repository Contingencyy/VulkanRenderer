#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"
#include "GLFW/glfw3.h"

#include "Allocator.h"
#include "Logger.h"

#include <assert.h>
#include <cstring>
#include <vector>
#include <optional>
#include <set>
#include <algorithm>
#include <fstream>

#define VK_ASSERT(x) assert(x)
#define VK_VERIFY(x) (VK_ASSERT(x), x)
#define VK_VERIFY_NOT(x) (VK_ASSERT(!(x)), x)

static std::vector<char> ReadFile(const std::string& filepath)
{
	std::ifstream file(filepath, std::ios::ate | std::ios::binary);
	if (VK_VERIFY_NOT(!file.is_open()))
	{
		LOG_ERR("FILEIO", "Could not open file: {}", filepath);
	}

	size_t file_size = (size_t)file.tellg();
	std::vector<char> buffer(file_size);

	file.seekg(0);
	file.read(buffer.data(), file_size);
	file.close();

	return buffer;
}

static inline void VkCheckResult(VkResult result)
{
	if (result != VK_SUCCESS)
	{
		LOG_ERR("Vulkan", "{} {}", string_VkResult(result), result);
		VK_ASSERT(result == VK_SUCCESS);
	}
}

const uint32_t DEFAULT_WINDOW_WIDTH = 1280;
const uint32_t DEFAULT_WINDOW_HEIGHT = 720;

static GLFWwindow* s_window = nullptr;

const uint32_t MAX_FRAMES_IN_FLIGHT = 2;

static std::vector<const char*> s_validation_layers = { "VK_LAYER_KHRONOS_validation" };
#ifdef _DEBUG
static const bool s_enable_validation_layers = true;
#else
static const bool s_enable_validation_layers = false;
#endif
static std::vector<const char*> s_device_extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
static VkInstance s_instance = VK_NULL_HANDLE;
static VkDebugUtilsMessengerEXT s_debug_messenger = VK_NULL_HANDLE;
static VkPhysicalDevice s_physical_device = VK_NULL_HANDLE;
static VkDevice s_device = VK_NULL_HANDLE;
static VkQueue s_graphics_queue = VK_NULL_HANDLE;
static VkSurfaceKHR s_surface = VK_NULL_HANDLE;
static VkQueue s_present_queue = VK_NULL_HANDLE;
static VkSwapchainKHR s_swapchain;
static std::vector<VkImage> s_swapchain_images;
static std::vector<VkImageView> s_swapchain_image_views;
static VkFormat s_swapchain_format;
static VkExtent2D s_swapchain_extent;
static VkRenderPass s_render_pass;
static VkPipelineLayout s_pipeline_layout;
static VkPipeline s_graphics_pipeline;
static std::vector<VkFramebuffer> s_swapchain_frame_buffers;
static VkCommandPool s_command_pool;
static std::vector<VkCommandBuffer> s_command_buffers;
static std::vector<VkSemaphore> s_image_available_semaphores;
static std::vector<VkSemaphore> s_render_finished_semaphores;
static std::vector<VkFence> s_in_flight_fences;
static uint32_t s_current_frame = 0;
static bool s_framebuffer_resized = false;

static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
{
	s_framebuffer_resized = true;
}

static void CreateWindow()
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	s_window = glfwCreateWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "VulkanRenderer", nullptr, nullptr);
	glfwSetFramebufferSizeCallback(s_window, FramebufferResizeCallback);
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
	(void)type; (void)user_data;
	switch (severity)
	{
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
		LOG_VERBOSE("Vulkan validation layer", callback_data->pMessage);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		LOG_INFO("Vulkan validation layer", callback_data->pMessage);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		LOG_WARN("Vulkan validation layer", callback_data->pMessage);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		LOG_ERR("Vulkan validation layer", callback_data->pMessage);
		break;
	}

	return VK_FALSE;
}

struct QueueFamilyIndices
{
	std::optional<uint32_t> graphics_family;
	std::optional<uint32_t> present_family;
};

QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device)
{
	QueueFamilyIndices indices;

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

	std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

	int i = 0;
	for (const auto& queue_family : queue_families)
	{
		// Check queue for graphics capabilities
		if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			indices.graphics_family = i;
		}

		// Check queue for present capabilities
		VkBool32 present_supported = false;
		VkCheckResult(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, s_surface, &present_supported));

		if (present_supported)
		{
			indices.present_family = i;
		}

		// Early-out if we found a graphics and a present queue family
		if (indices.graphics_family.has_value() &&
			indices.present_family.has_value())
		{
			break;
		}

		i++;
	}

	if (VK_VERIFY_NOT(!indices.graphics_family.has_value()))
	{
		LOG_ERR("Vulkan", "No graphics queue family found");
	}

	if (VK_VERIFY_NOT(!indices.present_family.has_value()))
	{
		LOG_ERR("Vulkan", "No present queue family found");
	}

	return indices;
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
	VkCheckResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, s_surface, &swapchain_details.capabilities));

	uint32_t format_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, s_surface, &format_count, nullptr);

	if (format_count != 0)
	{
		swapchain_details.formats.resize(format_count);
		VkCheckResult(vkGetPhysicalDeviceSurfaceFormatsKHR(device, s_surface, &format_count, swapchain_details.formats.data()));
	}

	uint32_t present_mode_count = 0;
	VkCheckResult(vkGetPhysicalDeviceSurfacePresentModesKHR(device, s_surface, &present_mode_count, nullptr));

	if (present_mode_count != 0)
	{
		swapchain_details.present_modes.resize(present_mode_count);
		VkCheckResult(vkGetPhysicalDeviceSurfacePresentModesKHR(device, s_surface, &present_mode_count, swapchain_details.present_modes.data()));
	}

	return swapchain_details;
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

	LOG_ERR("Vulkan", "Swapchain does not have any formats");
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
		glfwGetFramebufferSize(s_window, &width, &height);

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
	SwapChainSupportDetails swapchain_support = QuerySwapChainSupport(s_physical_device);

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
	create_info.surface = s_surface;
	create_info.minImageCount = image_count;
	create_info.imageFormat = surface_format.format;
	create_info.imageColorSpace = surface_format.colorSpace;
	create_info.imageExtent = extent;
	create_info.imageArrayLayers = 1;
	// TODO: We set the VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT here because we want to render to the swap chain image
	// later on we change this to VK_IMAGE_USAGE_TRANSFER_DST_BIT to simply copy a texture to it
	create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	QueueFamilyIndices indices = FindQueueFamilies(s_physical_device);
	uint32_t queue_family_indices[] = { indices.graphics_family.value(), indices.present_family.value() };

	if (indices.graphics_family != indices.present_family)
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

	VkCheckResult(vkCreateSwapchainKHR(s_device, &create_info, nullptr, &s_swapchain));
	VkCheckResult(vkGetSwapchainImagesKHR(s_device, s_swapchain, &image_count, nullptr));
	s_swapchain_images.resize(image_count);
	VkCheckResult(vkGetSwapchainImagesKHR(s_device, s_swapchain, &image_count, s_swapchain_images.data()));

	s_swapchain_format = surface_format.format;
	s_swapchain_extent = extent;
}

static void CreateImageViews()
{
	s_swapchain_image_views.resize(s_swapchain_images.size());
	for (size_t i = 0; i < s_swapchain_images.size(); ++i)
	{
		VkImageViewCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		create_info.image = s_swapchain_images[i];
		create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		create_info.format = s_swapchain_format;
		create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		create_info.subresourceRange.baseMipLevel = 0;
		create_info.subresourceRange.levelCount = 1;
		create_info.subresourceRange.baseArrayLayer = 0;
		create_info.subresourceRange.layerCount = 1;

		VkCheckResult(vkCreateImageView(s_device, &create_info, nullptr, &s_swapchain_image_views[i]));
	}
}

static void CreateFramebuffers()
{
	s_swapchain_frame_buffers.resize(s_swapchain_image_views.size());

	for (size_t i = 0; i < s_swapchain_image_views.size(); ++i)
	{
		VkImageView attachments[] =
		{
			s_swapchain_image_views[i]
		};

		VkFramebufferCreateInfo frame_buffer_info = {};
		frame_buffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frame_buffer_info.renderPass = s_render_pass;
		frame_buffer_info.attachmentCount = 1;
		frame_buffer_info.pAttachments = attachments;
		frame_buffer_info.width = s_swapchain_extent.width;
		frame_buffer_info.height = s_swapchain_extent.height;
		frame_buffer_info.layers = 1;

		VkCheckResult(vkCreateFramebuffer(s_device, &frame_buffer_info, nullptr, &s_swapchain_frame_buffers[i]));
	}
}

static void DestroySwapChain()
{
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		vkDestroyFramebuffer(s_device, s_swapchain_frame_buffers[i], nullptr);
		vkDestroyImageView(s_device, s_swapchain_image_views[i], nullptr);
	}
	vkDestroySwapchainKHR(s_device, s_swapchain, nullptr);
}

static void RecreateSwapChain()
{
	int width = 0, height = 0;
	glfwGetFramebufferSize(s_window, &width, &height);
	while (width == 0 || height == 0)
	{
		glfwGetFramebufferSize(s_window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(s_device);

	DestroySwapChain();

	CreateSwapChain();
	CreateImageViews();
	CreateFramebuffers();
}

static VkShaderModule CreateShaderModule(const std::vector<char>& code)
{
	VkShaderModuleCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize = code.size();
	create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shader_module;
	VkCheckResult(vkCreateShaderModule(s_device, &create_info, nullptr, &shader_module));

	return shader_module;
}

static void CreateRenderPass()
{
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = s_swapchain_format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;

	VkCheckResult(vkCreateRenderPass(s_device, &render_pass_info, nullptr, &s_render_pass));
}

static void CreateGraphicsPipeline()
{
	// TODO: Use libshaderc to compile shaders into SPIR-V from code
	// TODO: Vulkan extension for shader objects? No longer need to make compiled pipeline states then
	// https://www.khronos.org/blog/you-can-use-vulkan-without-pipelines-today
	auto vert_shader_code = ReadFile("assets/shaders/bin/VertexShader.spv");
	auto frag_shader_code = ReadFile("assets/shaders/bin/FragmentShader.spv");

	VkShaderModule vert_shader_module = CreateShaderModule(vert_shader_code);
	VkShaderModule frag_shader_module = CreateShaderModule(frag_shader_code);

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
	vertex_input_info.vertexBindingDescriptionCount = 0;
	vertex_input_info.pVertexBindingDescriptions = nullptr;
	vertex_input_info.vertexAttributeDescriptionCount = 0;
	vertex_input_info.pVertexAttributeDescriptions = nullptr;

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
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
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

	VkPipelineColorBlendAttachmentState color_blend_attachment = {};
	color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	color_blend_attachment.blendEnable = VK_FALSE;// VK_TRUE;
	color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
	color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo color_blend = {};
	color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blend.logicOpEnable = VK_FALSE;
	color_blend.logicOp = VK_LOGIC_OP_COPY;
	color_blend.attachmentCount = 1;
	color_blend.pAttachments = &color_blend_attachment;
	color_blend.blendConstants[0] = 0.0f;
	color_blend.blendConstants[1] = 0.0f;
	color_blend.blendConstants[2] = 0.0f;
	color_blend.blendConstants[3] = 0.0f;

	VkPipelineLayoutCreateInfo pipeline_layout_info = {};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 0;
	pipeline_layout_info.pSetLayouts = nullptr;
	pipeline_layout_info.pushConstantRangeCount = 0;
	pipeline_layout_info.pPushConstantRanges = nullptr;

	VkCheckResult(vkCreatePipelineLayout(s_device, &pipeline_layout_info, nullptr, &s_pipeline_layout));

	VkGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = shader_stages;
	pipeline_info.pVertexInputState = &vertex_input_info;
	pipeline_info.pInputAssemblyState = &input_assembly_info;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisampling;
	pipeline_info.pDepthStencilState = nullptr;
	pipeline_info.pColorBlendState = &color_blend;
	pipeline_info.pDynamicState = &dynamic_state;
	pipeline_info.layout = s_pipeline_layout;
	pipeline_info.renderPass = s_render_pass;
	pipeline_info.subpass = 0;
	pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
	pipeline_info.basePipelineIndex = -1;

	VkCheckResult(vkCreateGraphicsPipelines(s_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &s_graphics_pipeline));

	vkDestroyShaderModule(s_device, frag_shader_module, nullptr);
	vkDestroyShaderModule(s_device, vert_shader_module, nullptr);
}

static void RecordCommandBuffer(VkCommandBuffer command_buffer, uint32_t image_index)
{
	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = 0;
	begin_info.pInheritanceInfo = nullptr;

	VkCheckResult(vkBeginCommandBuffer(command_buffer, &begin_info));

	VkRenderPassBeginInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_info.renderPass = s_render_pass;
	render_pass_info.framebuffer = s_swapchain_frame_buffers[image_index];
	// NOTE: Define the render area, which defines where shader loads and stores will take place
	// Pixels outside this region will have undefined values
	render_pass_info.renderArea.offset = { 0, 0 };
	render_pass_info.renderArea.extent = s_swapchain_extent;
	VkClearValue clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
	render_pass_info.clearValueCount = 1;
	render_pass_info.pClearValues = &clear_color;

	vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, s_graphics_pipeline);

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)s_swapchain_extent.width;
	viewport.height = (float)s_swapchain_extent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = s_swapchain_extent;
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);

	vkCmdDraw(command_buffer, 3, 1, 0, 0);
	vkCmdEndRenderPass(command_buffer);

	VkCheckResult(vkEndCommandBuffer(command_buffer));
}

static void RenderFrame()
{
	// Wait for completion of all rendering on the GPU
	vkWaitForFences(s_device, 1, &s_in_flight_fences[s_current_frame], VK_TRUE, UINT64_MAX);

	// Get an available image index from the swap chain
	uint32_t image_index;
	VkResult image_result = vkAcquireNextImageKHR(s_device, s_swapchain, UINT64_MAX, s_image_available_semaphores[s_current_frame], VK_NULL_HANDLE, &image_index);

	if (image_result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapChain();
		return;
	}
	else if (image_result != VK_SUCCESS && image_result != VK_SUBOPTIMAL_KHR)
	{
		VkCheckResult(image_result);
	}

	// Reset the fence
	vkResetFences(s_device, 1, &s_in_flight_fences[s_current_frame]);

	// Reset and record the command buffer
	vkResetCommandBuffer(s_command_buffers[s_current_frame], 0);
	RecordCommandBuffer(s_command_buffers[s_current_frame], image_index);

	// Submit the command buffer for execution
	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore wait_semaphores[] = { s_image_available_semaphores[s_current_frame] };
	VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = wait_semaphores;
	submit_info.pWaitDstStageMask = wait_stages;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &s_command_buffers[s_current_frame];

	VkCheckResult(vkQueueSubmit(s_graphics_queue, 1, &submit_info, s_in_flight_fences[s_current_frame]));

	VkSemaphore signal_semaphores[] = { s_render_finished_semaphores[s_current_frame] };
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = signal_semaphores;

	// Present
	VkPresentInfoKHR present_info = {};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = signal_semaphores;

	VkSwapchainKHR swapchains[] = { s_swapchain };
	present_info.swapchainCount = 1;
	present_info.pSwapchains = swapchains;
	present_info.pImageIndices = &image_index;
	present_info.pResults = nullptr;

	VkResult present_result = vkQueuePresentKHR(s_graphics_queue, &present_info);

	if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR || s_framebuffer_resized)
	{
		s_framebuffer_resized = false;
		RecreateSwapChain();
	}
	else
	{
		VkCheckResult(present_result);
	}

	s_current_frame = (s_current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

static void InitVulkan()
{
	// ---------------------------------------------------------------------------------------------------
	// Create vulkan instance

	{
		VkApplicationInfo app_info = {};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pApplicationName = "VulkanRenderer";
		app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
		app_info.pEngineName = "No Engine";
		app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
		app_info.apiVersion = VK_API_VERSION_1_0;

		VkInstanceCreateInfo instance_create_info = {};
		instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instance_create_info.pApplicationInfo = &app_info;

		std::vector<const char*> required_extensions = GetRequiredExtensions();
		instance_create_info.enabledExtensionCount = (uint32_t)required_extensions.size();
		instance_create_info.ppEnabledExtensionNames = required_extensions.data();

		if (s_enable_validation_layers)
		{
			instance_create_info.enabledLayerCount = (uint32_t)s_validation_layers.size();
			instance_create_info.ppEnabledLayerNames = s_validation_layers.data();

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

		VkCheckResult(vkCreateInstance(&instance_create_info, nullptr, &s_instance));
	}


	// ---------------------------------------------------------------------------------------------------
	// Enable validation layer for vulkan instance creation

	{
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

			VkDebugUtilsMessengerCreateInfoEXT create_info = {};
			create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			create_info.pfnUserCallback = VkDebugCallback;
			create_info.pUserData = nullptr;

			auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkCreateDebugUtilsMessengerEXT");
			if (VK_VERIFY(func))
			{
				VkCheckResult(func(s_instance, &create_info, nullptr, &s_debug_messenger));
			}
		}
	}

	// ---------------------------------------------------------------------------------------------------
	// Create surface

	{
		VkCheckResult(glfwCreateWindowSurface(s_instance, s_window, nullptr, &s_surface));
	}

	// ---------------------------------------------------------------------------------------------------
	// Select GPU device

	{
		uint32_t device_count = 0;
		vkEnumeratePhysicalDevices(s_instance, &device_count, nullptr);
		if (VK_VERIFY_NOT(device_count == 0))
		{
			LOG_ERR("Vulkan", "No GPU devices found");
		}

		std::vector<VkPhysicalDevice> devices(device_count);
		vkEnumeratePhysicalDevices(s_instance, &device_count, devices.data());

		for (const auto& device : devices)
		{
			VkPhysicalDeviceProperties device_properties;
			vkGetPhysicalDeviceProperties(device, &device_properties);

			VkPhysicalDeviceFeatures device_features;
			vkGetPhysicalDeviceFeatures(device, &device_features);

			uint32_t extension_count = 0;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

			std::vector<VkExtensionProperties> available_extensions(extension_count);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

			std::set<std::string> required_extensions(s_device_extensions.begin(), s_device_extensions.end());

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

			if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
				required_extensions.empty() && swapchain_suitable)
			{
				s_physical_device = device;
				break;
			}
		}

		if (VK_VERIFY_NOT(s_physical_device == VK_NULL_HANDLE))
		{
			LOG_ERR("Vulkan", "No suitable GPU device found");
		}
	}

	// ---------------------------------------------------------------------------------------------------
	// Create logical device and queues per queue family

	{
		QueueFamilyIndices queue_family_indices = FindQueueFamilies(s_physical_device);

		std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
		std::set<uint32_t> unique_queue_families = { queue_family_indices.graphics_family.value(), queue_family_indices.present_family.value() };
		float queue_priority = 1.0;

		for (uint32_t queue_family : unique_queue_families)
		{
			VkDeviceQueueCreateInfo& queue_create_info = queue_create_infos.emplace_back();
			queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_create_info.queueFamilyIndex = queue_family;
			queue_create_info.queueCount = 1;
			queue_create_info.pQueuePriorities = &queue_priority;
		}

		// Note: We will fill this later when we need specific features
		VkPhysicalDeviceFeatures device_features = {};

		VkDeviceCreateInfo device_create_info = {};
		device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_create_info.pQueueCreateInfos = queue_create_infos.data();
		device_create_info.queueCreateInfoCount = (uint32_t)queue_create_infos.size();
		device_create_info.pEnabledFeatures = &device_features;
		device_create_info.ppEnabledExtensionNames = s_device_extensions.data();
		device_create_info.enabledExtensionCount = (uint32_t)s_device_extensions.size();
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
		vkGetDeviceQueue(s_device, queue_family_indices.present_family.value(), 0, &s_present_queue);
	}

	// ---------------------------------------------------------------------------------------------------
	// Create render passes and graphics pipelines

	CreateRenderPass();
	CreateGraphicsPipeline();

	// ---------------------------------------------------------------------------------------------------
	// Create the swap chain, image views and frame buffers

	CreateSwapChain();
	CreateImageViews();
	CreateFramebuffers();

	// ---------------------------------------------------------------------------------------------------
	// Create command pools

	{
		QueueFamilyIndices queue_family_indices = FindQueueFamilies(s_physical_device);

		VkCommandPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = queue_family_indices.graphics_family.value();

		VkCheckResult(vkCreateCommandPool(s_device, &pool_info, nullptr, &s_command_pool));
	}

	// ---------------------------------------------------------------------------------------------------
	// Create command buffers

	{
		s_command_buffers.resize(MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.commandPool = s_command_pool;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = (uint32_t)s_command_buffers.size();

		VkCheckResult(vkAllocateCommandBuffers(s_device, &alloc_info, s_command_buffers.data()));
	}

	// ---------------------------------------------------------------------------------------------------
	// Create fences and semaphores

	{
		s_image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
		s_render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
		s_in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaphore_info = {};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fence_info = {};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkCheckResult(vkCreateSemaphore(s_device, &semaphore_info, nullptr, &s_image_available_semaphores[i]));
			VkCheckResult(vkCreateSemaphore(s_device, &semaphore_info, nullptr, &s_render_finished_semaphores[i]));
			VkCheckResult(vkCreateFence(s_device, &fence_info, nullptr, &s_in_flight_fences[i]));
		}
	}
}

static void ExitVulkan()
{
	vkDeviceWaitIdle(s_device);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		vkDestroyFence(s_device, s_in_flight_fences[i], nullptr);
		vkDestroySemaphore(s_device, s_render_finished_semaphores[i], nullptr);
		vkDestroySemaphore(s_device, s_image_available_semaphores[i], nullptr);
	}
	// Destroying the command pool will also destroy any command buffers associated with that pool
	vkDestroyCommandPool(s_device, s_command_pool, nullptr);
	vkDestroyRenderPass(s_device, s_render_pass, nullptr);
	vkDestroyPipeline(s_device, s_graphics_pipeline, nullptr);
	vkDestroyPipelineLayout(s_device, s_pipeline_layout, nullptr);
	DestroySwapChain();
	vkDestroyDevice(s_device, nullptr);
	if (s_enable_validation_layers)
	{
		auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkDestroyDebugUtilsMessengerEXT");
		if (VK_VERIFY(func))
		{
			func(s_instance, s_debug_messenger, nullptr);
		}
	}
	vkDestroySurfaceKHR(s_instance, s_surface, nullptr);
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
		RenderFrame();
	}

	ExitVulkan();
	DestroyWindow();

	return 0;
}