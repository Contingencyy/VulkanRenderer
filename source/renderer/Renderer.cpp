#include "renderer/Renderer.h"
#include "Logger.h"

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include "GLFW/glfw3.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <array>
#include <vector>
#include <optional>
#include <assert.h>
#include <cstring>
#include <set>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <exception>

#define VK_ASSERT(x) assert(x)
#define VK_EXCEPT(...) auto logged_msg = LOG_ERR(__VA_ARGS__); throw std::runtime_error(logged_msg)

namespace Renderer
{

	const uint32_t MAX_FRAMES_IN_FLIGHT = 2;
#ifdef _DEBUG
	static const bool s_enable_validation_layers = true;
#else
	static const bool s_enable_validation_layers = true;
	//static const bool s_enable_validation_layers = false;
#endif

	struct Data
	{
		::GLFWwindow* window = nullptr;

		std::vector<const char*> validation_layers = { "VK_LAYER_KHRONOS_validation" };
		std::vector<const char*> device_extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_SHADER_OBJECT_EXTENSION_NAME };
		VkInstance instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		VkQueue graphics_queue = VK_NULL_HANDLE;
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkQueue present_queue = VK_NULL_HANDLE;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		std::vector<VkImage> swapchain_images;
		std::vector<VkImageView> swapchain_image_views;
		VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
		VkExtent2D swapchain_extent;
		VkRenderPass render_pass = VK_NULL_HANDLE;
		VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
		VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> descriptor_sets;
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		VkPipeline graphics_pipeline = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> swapchain_framebuffers;
		VkImage depth_image = VK_NULL_HANDLE;
		VkDeviceMemory depth_image_memory = VK_NULL_HANDLE;
		VkImageView depth_image_view = VK_NULL_HANDLE;
		VkCommandPool command_pool = VK_NULL_HANDLE;
		VkBuffer vertex_buffer = VK_NULL_HANDLE;
		VkDeviceMemory vertex_buffer_memory = VK_NULL_HANDLE;
		VkBuffer index_buffer = VK_NULL_HANDLE;
		VkDeviceMemory index_buffer_memory = VK_NULL_HANDLE;
		VkImage texture_image = VK_NULL_HANDLE;
		VkDeviceMemory texture_image_memory = VK_NULL_HANDLE;
		VkImageView texture_image_view = VK_NULL_HANDLE;
		VkSampler texture_sampler = VK_NULL_HANDLE;
		std::vector<VkBuffer> uniform_buffers;
		std::vector<VkDeviceMemory> uniform_buffers_memory;
		std::vector<void*> uniform_buffers_mapped;
		std::vector<VkCommandBuffer> command_buffers;
		std::vector<VkSemaphore> image_available_semaphores;
		std::vector<VkSemaphore> render_finished_semaphores;
		std::vector<VkFence> in_flight_fences;
		uint32_t current_frame = 0;
		bool framebuffer_resized = false;
	} static rend;

	struct UniformBufferObject
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 proj;
	};

	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 color;
		glm::vec2 tex_coord;

		static VkVertexInputBindingDescription GetBindingDescription()
		{
			VkVertexInputBindingDescription binding_desc = {};
			binding_desc.binding = 0;
			binding_desc.stride = sizeof(Vertex);
			binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			return binding_desc;
		}

		static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescription()
		{
			std::array<VkVertexInputAttributeDescription, 3> attribute_desc = {};
			attribute_desc[0].binding = 0;
			attribute_desc[0].location = 0;
			attribute_desc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attribute_desc[0].offset = offsetof(Vertex, pos);

			attribute_desc[1].binding = 0;
			attribute_desc[1].location = 1;
			attribute_desc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
			attribute_desc[1].offset = offsetof(Vertex, color);

			attribute_desc[2].binding = 0;
			attribute_desc[2].location = 2;
			attribute_desc[2].format = VK_FORMAT_R32G32_SFLOAT;
			attribute_desc[2].offset = offsetof(Vertex, tex_coord);

			return attribute_desc;
		}
	};

	static const std::vector<Vertex> s_vertices =
	{
		{{ -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }},
		{{  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }},
		{{  0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }},
		{{ -0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }},

		{{ -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }},
		{{  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }},
		{{  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }},
		{{ -0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }},
	};

	static const std::vector<uint32_t> s_indices =
	{
		0, 1, 2, 2, 3, 0,
		4, 5, 6, 6, 7, 4,
	};

	static std::vector<char> ReadFile(const std::string& filepath)
	{
		std::ifstream file(filepath, std::ios::ate | std::ios::binary);
		if (!file.is_open())
		{
			VK_EXCEPT("FILEIO", "Could not open file: {}", filepath);
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
			VK_EXCEPT("Vulkan", "{} {}", string_VkResult(result));
		}
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
			VkCheckResult(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, rend.surface, &present_supported));

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

		if (!indices.graphics_family.has_value())
		{
			VK_EXCEPT("Vulkan", "No graphics queue family found");
		}

		if (!indices.present_family.has_value())
		{
			VK_EXCEPT("Vulkan", "No present queue family found");
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
		VkCheckResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, rend.surface, &swapchain_details.capabilities));

		uint32_t format_count = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, rend.surface, &format_count, nullptr);

		if (format_count != 0)
		{
			swapchain_details.formats.resize(format_count);
			VkCheckResult(vkGetPhysicalDeviceSurfaceFormatsKHR(device, rend.surface, &format_count, swapchain_details.formats.data()));
		}

		uint32_t present_mode_count = 0;
		VkCheckResult(vkGetPhysicalDeviceSurfacePresentModesKHR(device, rend.surface, &present_mode_count, nullptr));

		if (present_mode_count != 0)
		{
			swapchain_details.present_modes.resize(present_mode_count);
			VkCheckResult(vkGetPhysicalDeviceSurfacePresentModesKHR(device, rend.surface, &present_mode_count, swapchain_details.present_modes.data()));
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
			glfwGetFramebufferSize(rend.window, &width, &height);

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
		SwapChainSupportDetails swapchain_support = QuerySwapChainSupport(rend.physical_device);

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
		create_info.surface = rend.surface;
		create_info.minImageCount = image_count;
		create_info.imageFormat = surface_format.format;
		create_info.imageColorSpace = surface_format.colorSpace;
		create_info.imageExtent = extent;
		create_info.imageArrayLayers = 1;
		// TODO: We set the VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT here because we want to render to the swap chain image
		// later on we change this to VK_IMAGE_USAGE_TRANSFER_DST_BIT to simply copy a texture to it
		create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		QueueFamilyIndices indices = FindQueueFamilies(rend.physical_device);
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

		VkCheckResult(vkCreateSwapchainKHR(rend.device, &create_info, nullptr, &rend.swapchain));
		VkCheckResult(vkGetSwapchainImagesKHR(rend.device, rend.swapchain, &image_count, nullptr));
		rend.swapchain_images.resize(image_count);
		VkCheckResult(vkGetSwapchainImagesKHR(rend.device, rend.swapchain, &image_count, rend.swapchain_images.data()));

		rend.swapchain_format = surface_format.format;
		rend.swapchain_extent = extent;
	}

	static VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags)
	{
		VkImageViewCreateInfo view_info = {};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = image;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = format;
		view_info.subresourceRange.aspectMask = aspect_flags;
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.layerCount = 1;

		VkImageView image_view;
		VkCheckResult(vkCreateImageView(rend.device, &view_info, nullptr, &image_view));

		return image_view;
	}

	static void CreateImageViews()
	{
		rend.swapchain_image_views.resize(rend.swapchain_images.size());
		for (size_t i = 0; i < rend.swapchain_images.size(); ++i)
		{
			rend.swapchain_image_views[i] = CreateImageView(rend.swapchain_images[i], rend.swapchain_format, VK_IMAGE_ASPECT_COLOR_BIT);
		}
	}

	static void CreateFramebuffers()
	{
		rend.swapchain_framebuffers.resize(rend.swapchain_image_views.size());

		for (size_t i = 0; i < rend.swapchain_image_views.size(); ++i)
		{
			std::array<VkImageView, 2> attachments
			{
				rend.swapchain_image_views[i],
				rend.depth_image_view
			};

			VkFramebufferCreateInfo frame_buffer_info = {};
			frame_buffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			frame_buffer_info.renderPass = rend.render_pass;
			frame_buffer_info.attachmentCount = (uint32_t)attachments.size();
			frame_buffer_info.pAttachments = attachments.data();
			frame_buffer_info.width = rend.swapchain_extent.width;
			frame_buffer_info.height = rend.swapchain_extent.height;
			frame_buffer_info.layers = 1;

			VkCheckResult(vkCreateFramebuffer(rend.device, &frame_buffer_info, nullptr, &rend.swapchain_framebuffers[i]));
		}
	}

	static void CreateCommandPool()
	{
		QueueFamilyIndices queue_family_indices = FindQueueFamilies(rend.physical_device);

		VkCommandPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = queue_family_indices.graphics_family.value();

		VkCheckResult(vkCreateCommandPool(rend.device, &pool_info, nullptr, &rend.command_pool));
	}

	static VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
	{
		for (VkFormat format : candidates)
		{
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(rend.physical_device, format, &props);

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

	static VkFormat FindDepthFormat()
	{
		return FindSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);
	}

	static bool HasStencilComponent(VkFormat format)
	{
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
	}

	static VkCommandBuffer BeginSingleTimeCommands()
	{
		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandPool = rend.command_pool;
		alloc_info.commandBufferCount = 1;

		VkCommandBuffer command_buffer;
		VkCheckResult(vkAllocateCommandBuffers(rend.device, &alloc_info, &command_buffer));

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkCheckResult(vkBeginCommandBuffer(command_buffer, &begin_info));
		return command_buffer;
	}

	static void EndSingleTimeCommands(VkCommandBuffer command_buffer)
	{
		VkCheckResult(vkEndCommandBuffer(command_buffer));

		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;

		VkCheckResult(vkQueueSubmit(rend.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
		VkCheckResult(vkQueueWaitIdle(rend.graphics_queue));

		vkFreeCommandBuffers(rend.device, rend.command_pool, 1, &command_buffer);
	}

	static uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags mem_properties)
	{
		VkPhysicalDeviceMemoryProperties device_mem_properties = {};
		vkGetPhysicalDeviceMemoryProperties(rend.physical_device, &device_mem_properties);

		for (uint32_t i = 0; i < device_mem_properties.memoryTypeCount; ++i)
		{
			if (type_filter & (1 << i) &&
				(device_mem_properties.memoryTypes[i].propertyFlags & mem_properties) == mem_properties)
			{
				return i;
			}
		}

		VK_EXCEPT("Vulkan", "Failed to find suitable memory type");
		VK_ASSERT(false);
	}

	static void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout)
	{
		VkCommandBuffer command_buffer = BeginSingleTimeCommands();

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = old_layout;
		barrier.newLayout = new_layout;
		// NOTE: Used to transfer ownership of the resource if EXCLUSIVE flag is used
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (HasStencilComponent(format))
			{
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		VkPipelineStageFlags src_stage;
		VkPipelineStageFlags dst_stage;

		// TODO: This can be written much nicer, old_layout defines what goes into srcAccessMask and src_stage
		// and new_layout defines what goes into dstAccessMask and dst_stage, no need for these weird if combinations
		if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		}

		vkCmdPipelineBarrier(
			command_buffer,
			src_stage, dst_stage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		EndSingleTimeCommands(command_buffer);
	}

	static void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
	{
		VkCommandBuffer command_buffer = BeginSingleTimeCommands();

		VkBufferImageCopy region = {};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };

		vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		EndSingleTimeCommands(command_buffer);
	}

	static void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
		VkMemoryPropertyFlags memory_flags, VkImage& image, VkDeviceMemory& image_memory)
	{
		VkImageCreateInfo image_info = {};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.extent.width = width;
		image_info.extent.height = height;
		image_info.extent.depth = 1;
		image_info.mipLevels = 1;
		image_info.arrayLayers = 1;
		image_info.format = format;
		image_info.tiling = tiling;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_info.usage = usage;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.flags = 0;

		VkCheckResult(vkCreateImage(rend.device, &image_info, nullptr, &image));

		VkMemoryRequirements mem_requirements = {};
		vkGetImageMemoryRequirements(rend.device, image, &mem_requirements);

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_requirements.size;
		alloc_info.memoryTypeIndex = FindMemoryType(mem_requirements.memoryTypeBits, memory_flags);

		VkCheckResult(vkAllocateMemory(rend.device, &alloc_info, nullptr, &image_memory));
		VkCheckResult(vkBindImageMemory(rend.device, image, image_memory, 0));
	}

	static void CreateDepthResources()
	{
		VkFormat depth_format = FindDepthFormat();
		CreateImage(rend.swapchain_extent.width, rend.swapchain_extent.height, depth_format, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rend.depth_image, rend.depth_image_memory);
		rend.depth_image_view = CreateImageView(rend.depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
		TransitionImageLayout(rend.depth_image, depth_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	static void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags, VkBuffer& buffer, VkDeviceMemory& device_memory)
	{
		VkBufferCreateInfo buffer_info = {};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = size;
		buffer_info.usage = usage;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkCheckResult(vkCreateBuffer(rend.device, &buffer_info, nullptr, &buffer));

		VkMemoryRequirements mem_requirements = {};
		vkGetBufferMemoryRequirements(rend.device, buffer, &mem_requirements);

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_requirements.size;
		alloc_info.memoryTypeIndex = FindMemoryType(mem_requirements.memoryTypeBits, mem_flags);

		VkCheckResult(vkAllocateMemory(rend.device, &alloc_info, nullptr, &device_memory));
		VkCheckResult(vkBindBufferMemory(rend.device, buffer, device_memory, 0));
	}

	static void CreateTextureImage()
	{
		int width, height, channels;
		stbi_uc* pixels = stbi_load("assets/textures/statue.jpg", &width, &height, &channels, STBI_rgb_alpha);

		VkDeviceSize image_size = width * height * 4;

		if (!pixels)
		{
			VK_EXCEPT("Vulkan", "Failed to load texture data from file: {}", "assets/textures/statue.jpg");
		}

		VkBuffer staging_buffer;
		VkDeviceMemory staging_buffer_memory;
		CreateBuffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

		void* data;
		VkCheckResult(vkMapMemory(rend.device, staging_buffer_memory, 0, image_size, 0, &data));
		memcpy(data, pixels, (size_t)image_size);
		vkUnmapMemory(rend.device, staging_buffer_memory);

		stbi_image_free(pixels);

		CreateImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rend.texture_image, rend.texture_image_memory);
		TransitionImageLayout(rend.texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		CopyBufferToImage(staging_buffer, rend.texture_image, (uint32_t)width, (uint32_t)height);
		TransitionImageLayout(rend.texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		vkDestroyBuffer(rend.device, staging_buffer, nullptr);
		vkFreeMemory(rend.device, staging_buffer_memory, nullptr);
	}

	static void CreateTextureImageView()
	{
		rend.texture_image_view = CreateImageView(rend.texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	static void CreateTextureSampler()
	{
		VkPhysicalDeviceProperties properties = {};
		vkGetPhysicalDeviceProperties(rend.physical_device, &properties);

		VkSamplerCreateInfo sampler_info = {};
		sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		// Oversampling
		sampler_info.magFilter = VK_FILTER_LINEAR;
		// Undersampling
		sampler_info.minFilter = VK_FILTER_LINEAR;
		sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.anisotropyEnable = VK_TRUE;
		sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
		sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		sampler_info.unnormalizedCoordinates = VK_FALSE;
		sampler_info.compareEnable = VK_FALSE;
		sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_info.mipLodBias = 0.0f;
		sampler_info.minLod = 0.0f;
		sampler_info.maxLod = 0.0f;

		VkCheckResult(vkCreateSampler(rend.device, &sampler_info, nullptr, &rend.texture_sampler));
	}

	static void CopyBuffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size)
	{
		VkCommandBuffer command_buffer = BeginSingleTimeCommands();

		VkBufferCopy copy_region = {};
		copy_region.srcOffset = 0;
		copy_region.dstOffset = 0;
		copy_region.size = size;
		vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

		EndSingleTimeCommands(command_buffer);
	}

	static void CreateVertexBuffer()
	{
		VkDeviceSize buffer_size = sizeof(s_vertices[0]) * s_vertices.size();

		// Staging buffer
		VkBuffer staging_buffer;
		VkDeviceMemory staging_buffer_memory;
		CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

		// NOTE: The driver may or may not have immediately copied this over to buffer memory (e.g. caching)
		// or writes to the buffer are not visible in the mapped memory yet.
		// To deal with this problem, you either have to use a memory heap that is host coherent (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		// or call vkFlushMappedMemoryRanges after writing to the mapped memory and then calling vkInvalidateMappedMemoryRanges before reading from it
		// The transfer of data to the GPU happens in the background and the specification states it is guaranteed to be complete as of the next call to vkQueueSubmit
		void* ptr;
		VkCheckResult(vkMapMemory(rend.device, staging_buffer_memory, 0, buffer_size, 0, &ptr));
		memcpy(ptr, s_vertices.data(), (size_t)buffer_size);
		vkUnmapMemory(rend.device, staging_buffer_memory);

		// Device local buffer
		CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rend.vertex_buffer, rend.vertex_buffer_memory);
		CopyBuffer(staging_buffer, rend.vertex_buffer, buffer_size);

		vkDestroyBuffer(rend.device, staging_buffer, nullptr);
		vkFreeMemory(rend.device, staging_buffer_memory, nullptr);
	}

	static void CreateIndexBuffer()
	{
		VkDeviceSize buffer_size = sizeof(s_indices[0]) * s_indices.size();

		// Staging buffer
		VkBuffer staging_buffer;
		VkDeviceMemory staging_buffer_memory;
		CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

		// NOTE: The driver may or may not have immediately copied this over to buffer memory (e.g. caching)
		// or writes to the buffer are not visible in the mapped memory yet.
		// To deal with this problem, you either have to use a memory heap that is host coherent (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		// or call vkFlushMappedMemoryRanges after writing to the mapped memory and then calling vkInvalidateMappedMemoryRanges before reading from it
		// The transfer of data to the GPU happens in the background and the specification states it is guaranteed to be complete as of the next call to vkQueueSubmit
		void* ptr;
		VkCheckResult(vkMapMemory(rend.device, staging_buffer_memory, 0, buffer_size, 0, &ptr));
		memcpy(ptr, s_indices.data(), (size_t)buffer_size);
		vkUnmapMemory(rend.device, staging_buffer_memory);

		// Device local buffer
		CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rend.index_buffer, rend.index_buffer_memory);
		CopyBuffer(staging_buffer, rend.index_buffer, buffer_size);

		vkDestroyBuffer(rend.device, staging_buffer, nullptr);
		vkFreeMemory(rend.device, staging_buffer_memory, nullptr);
	}

	static void CreateUniformBuffers()
	{
		rend.uniform_buffers.resize(MAX_FRAMES_IN_FLIGHT);
		rend.uniform_buffers_memory.resize(MAX_FRAMES_IN_FLIGHT);
		rend.uniform_buffers_mapped.resize(MAX_FRAMES_IN_FLIGHT);

		VkDeviceSize buffer_size = sizeof(UniformBufferObject);

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			CreateBuffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, rend.uniform_buffers[i], rend.uniform_buffers_memory[i]);
			VkCheckResult(vkMapMemory(rend.device, rend.uniform_buffers_memory[i], 0, buffer_size, 0, &rend.uniform_buffers_mapped[i]));
		}
	}

	static void UpdateUniformBuffer(uint32_t current_image)
	{
		static auto start_time = std::chrono::high_resolution_clock::now();

		auto current_time = std::chrono::high_resolution_clock::now();
		float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

		UniformBufferObject ubo = {};
		ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.proj = glm::perspective(glm::radians(45.0f), rend.swapchain_extent.width / (float)rend.swapchain_extent.height, 0.1f, 10.0f);
		ubo.proj[1][1] *= -1.0f;

		memcpy(rend.uniform_buffers_mapped[current_image], &ubo, sizeof(ubo));
	}

	static void CreateDescriptorPool()
	{
		std::array<VkDescriptorPoolSize, 2> pool_sizes = {};
		pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		pool_sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
		pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pool_sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.poolSizeCount = (uint32_t)pool_sizes.size();
		pool_info.pPoolSizes = pool_sizes.data();
		pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;
		// Note: Flag to determine if individual descriptors can be freed: VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT

		VkCheckResult(vkCreateDescriptorPool(rend.device, &pool_info, nullptr, &rend.descriptor_pool));
	}

	static void CreateDescriptorSets()
	{
		std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, rend.descriptor_set_layout);

		VkDescriptorSetAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc_info.descriptorPool = rend.descriptor_pool;
		alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
		alloc_info.pSetLayouts = layouts.data();

		rend.descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
		VkCheckResult(vkAllocateDescriptorSets(rend.device, &alloc_info, rend.descriptor_sets.data()));

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkDescriptorBufferInfo buffer_info = {};
			buffer_info.buffer = rend.uniform_buffers[i];
			buffer_info.offset = 0;
			buffer_info.range = sizeof(UniformBufferObject);

			VkDescriptorImageInfo image_info = {};
			image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			image_info.imageView = rend.texture_image_view;
			image_info.sampler = rend.texture_sampler;

			std::array<VkWriteDescriptorSet, 2> descriptor_writes = {};

			descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptor_writes[0].dstSet = rend.descriptor_sets[i];
			descriptor_writes[0].dstBinding = 0;
			descriptor_writes[0].dstArrayElement = 0;
			descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptor_writes[0].descriptorCount = 1;
			descriptor_writes[0].pBufferInfo = &buffer_info;
			descriptor_writes[0].pImageInfo = nullptr;
			descriptor_writes[0].pTexelBufferView = nullptr;

			descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptor_writes[1].dstSet = rend.descriptor_sets[i];
			descriptor_writes[1].dstBinding = 1;
			descriptor_writes[1].dstArrayElement = 0;
			descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptor_writes[1].descriptorCount = 1;
			descriptor_writes[1].pImageInfo = &image_info;

			vkUpdateDescriptorSets(rend.device, (uint32_t)descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);
		}
	}

	static void CreateCommandBuffers()
	{
		rend.command_buffers.resize(MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.commandPool = rend.command_pool;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = (uint32_t)rend.command_buffers.size();

		VkCheckResult(vkAllocateCommandBuffers(rend.device, &alloc_info, rend.command_buffers.data()));
	}

	static void CreateSyncObjects()
	{
		rend.image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
		rend.render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
		rend.in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaphore_info = {};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fence_info = {};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkCheckResult(vkCreateSemaphore(rend.device, &semaphore_info, nullptr, &rend.image_available_semaphores[i]));
			VkCheckResult(vkCreateSemaphore(rend.device, &semaphore_info, nullptr, &rend.render_finished_semaphores[i]));
			VkCheckResult(vkCreateFence(rend.device, &fence_info, nullptr, &rend.in_flight_fences[i]));
		}
	}

	static void DestroySwapChain()
	{
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroyFramebuffer(rend.device, rend.swapchain_framebuffers[i], nullptr);
			vkDestroyImageView(rend.device, rend.swapchain_image_views[i], nullptr);
		}
		
		vkDestroyImageView(rend.device, rend.depth_image_view, nullptr);
		vkDestroyImage(rend.device, rend.depth_image, nullptr);
		vkFreeMemory(rend.device, rend.depth_image_memory, nullptr);

		vkDestroySwapchainKHR(rend.device, rend.swapchain, nullptr);
	}

	static void RecreateSwapChain()
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(rend.window, &width, &height);
		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(rend.window, &width, &height);
			glfwWaitEvents();
		}

		vkDeviceWaitIdle(rend.device);

		DestroySwapChain();

		CreateSwapChain();
		CreateImageViews();
		CreateDepthResources();
		CreateFramebuffers();
	}

	static VkShaderModule CreateShaderModule(const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		create_info.codeSize = code.size();
		create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shader_module;
		VkCheckResult(vkCreateShaderModule(rend.device, &create_info, nullptr, &shader_module));

		return shader_module;
	}

	static void CreateRenderPass()
	{
		VkAttachmentDescription color_attachment = {};
		color_attachment.format = rend.swapchain_format;
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription depth_attachment = {};
		depth_attachment.format = FindDepthFormat();
		depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference color_attachment_ref = {};
		color_attachment_ref.attachment = 0;
		color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depth_attachment_ref = {};
		depth_attachment_ref.attachment = 1;
		depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_attachment_ref;
		subpass.pDepthStencilAttachment = &depth_attachment_ref;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		std::array<VkAttachmentDescription, 2> attachments = { color_attachment, depth_attachment };
		VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_info.attachmentCount = (uint32_t)attachments.size();
		render_pass_info.pAttachments = attachments.data();
		render_pass_info.subpassCount = 1;
		render_pass_info.pSubpasses = &subpass;
		render_pass_info.dependencyCount = 1;
		render_pass_info.pDependencies = &dependency;

		VkCheckResult(vkCreateRenderPass(rend.device, &render_pass_info, nullptr, &rend.render_pass));
	}

	static void CreateDescriptorSetLayout()
	{
		VkDescriptorSetLayoutBinding ubo_layout_binding = {};
		ubo_layout_binding.binding = 0;
		ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubo_layout_binding.descriptorCount = 1;
		ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		ubo_layout_binding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding sampler_layout_binding = {};
		sampler_layout_binding.binding = 1;
		sampler_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sampler_layout_binding.descriptorCount = 1;
		sampler_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		sampler_layout_binding.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = { ubo_layout_binding, sampler_layout_binding };
		VkDescriptorSetLayoutCreateInfo layout_info = {};
		layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layout_info.bindingCount = (uint32_t)bindings.size();
		layout_info.pBindings = bindings.data();

		VkCheckResult(vkCreateDescriptorSetLayout(rend.device, &layout_info, nullptr, &rend.descriptor_set_layout));
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
		auto binding_desc = Vertex::GetBindingDescription();
		auto attribute_desc = Vertex::GetAttributeDescription();

		VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
		vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_info.vertexBindingDescriptionCount = 1;
		vertex_input_info.pVertexBindingDescriptions = &binding_desc;
		vertex_input_info.vertexAttributeDescriptionCount = (uint32_t)attribute_desc.size();
		vertex_input_info.pVertexAttributeDescriptions = attribute_desc.data();

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
		depth_stencil.depthTestEnable = VK_TRUE;
		depth_stencil.depthWriteEnable = VK_TRUE;
		depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depth_stencil.depthBoundsTestEnable = VK_FALSE;
		depth_stencil.minDepthBounds = 0.0f;
		depth_stencil.maxDepthBounds = 1.0f;
		depth_stencil.stencilTestEnable = VK_FALSE;
		depth_stencil.front = {};
		depth_stencil.back = {};

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
		pipeline_layout_info.setLayoutCount = 1;
		pipeline_layout_info.pSetLayouts = &rend.descriptor_set_layout;
		pipeline_layout_info.pushConstantRangeCount = 0;
		pipeline_layout_info.pPushConstantRanges = nullptr;

		VkCheckResult(vkCreatePipelineLayout(rend.device, &pipeline_layout_info, nullptr, &rend.pipeline_layout));

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
		pipeline_info.layout = rend.pipeline_layout;
		pipeline_info.renderPass = rend.render_pass;
		pipeline_info.subpass = 0;
		pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_info.basePipelineIndex = -1;

		VkCheckResult(vkCreateGraphicsPipelines(rend.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &rend.graphics_pipeline));

		vkDestroyShaderModule(rend.device, frag_shader_module, nullptr);
		vkDestroyShaderModule(rend.device, vert_shader_module, nullptr);
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
		render_pass_info.renderPass = rend.render_pass;
		render_pass_info.framebuffer = rend.swapchain_framebuffers[image_index];
		// NOTE: Define the render area, which defines where shader loads and stores will take place
		// Pixels outside this region will have undefined values
		render_pass_info.renderArea.offset = { 0, 0 };
		render_pass_info.renderArea.extent = rend.swapchain_extent;

		std::array<VkClearValue, 2> clear_values = {};
		clear_values[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clear_values[1].depthStencil = { 1.0f, 0 };

		render_pass_info.clearValueCount = (uint32_t)clear_values.size();
		render_pass_info.pClearValues = clear_values.data();

		vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rend.graphics_pipeline);

		// Vertex and index buffers
		VkBuffer vertex_buffers[] = { rend.vertex_buffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
		vkCmdBindIndexBuffer(command_buffer, rend.index_buffer, 0, VK_INDEX_TYPE_UINT32);

		// Viewport and scissor
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)rend.swapchain_extent.width;
		viewport.height = (float)rend.swapchain_extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = rend.swapchain_extent;
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		// Descriptor sets
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rend.pipeline_layout,
			0, 1, &rend.descriptor_sets[image_index], 0, nullptr);

		// Draw call
		vkCmdDrawIndexed(command_buffer, (uint32_t)s_indices.size(), 1, 0, 0, 0);
		vkCmdEndRenderPass(command_buffer);

		VkCheckResult(vkEndCommandBuffer(command_buffer));
	}

	void Init(::GLFWwindow* window)
	{
		rend.window = window;

		// ---------------------------------------------------------------------------------------------------
		// Create vulkan instance

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

			if (s_enable_validation_layers)
			{
				instance_create_info.enabledLayerCount = (uint32_t)rend.validation_layers.size();
				instance_create_info.ppEnabledLayerNames = rend.validation_layers.data();

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

			VkCheckResult(vkCreateInstance(&instance_create_info, nullptr, &rend.instance));
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

				for (const auto& validation_layer : rend.validation_layers)
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

				auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(rend.instance, "vkCreateDebugUtilsMessengerEXT");
				if (!func)
				{
					VK_EXCEPT("Vulkan", "Could not find function pointer for vkCreateDebugUtilsMessengerEXT");
				}

				VkCheckResult(func(rend.instance, &create_info, nullptr, &rend.debug_messenger));
			}
		}

		// ---------------------------------------------------------------------------------------------------
		// Create surface

		{
			VkCheckResult(glfwCreateWindowSurface(rend.instance, rend.window, nullptr, &rend.surface));
		}

		// ---------------------------------------------------------------------------------------------------
		// Select GPU device

		{
			uint32_t device_count = 0;
			vkEnumeratePhysicalDevices(rend.instance, &device_count, nullptr);
			if (device_count == 0)
			{
				VK_EXCEPT("Vulkan", "No GPU devices found");
			}

			std::vector<VkPhysicalDevice> devices(device_count);
			vkEnumeratePhysicalDevices(rend.instance, &device_count, devices.data());

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

				std::set<std::string> required_extensions(rend.device_extensions.begin(), rend.device_extensions.end());

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
					required_extensions.empty() && swapchain_suitable &&
					device_features.samplerAnisotropy)
				{
					rend.physical_device = device;
					break;
				}
			}

			if (rend.physical_device == VK_NULL_HANDLE)
			{
				VK_EXCEPT("Vulkan", "No suitable GPU device found");
			}
		}

		// ---------------------------------------------------------------------------------------------------
		// Create logical device and queues per queue family

		{
			QueueFamilyIndices queue_family_indices = FindQueueFamilies(rend.physical_device);

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
			device_features.samplerAnisotropy = VK_TRUE;

			VkDeviceCreateInfo device_create_info = {};
			device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			device_create_info.pQueueCreateInfos = queue_create_infos.data();
			device_create_info.queueCreateInfoCount = (uint32_t)queue_create_infos.size();
			device_create_info.pEnabledFeatures = &device_features;
			device_create_info.ppEnabledExtensionNames = rend.device_extensions.data();
			device_create_info.enabledExtensionCount = (uint32_t)rend.device_extensions.size();
			if (s_enable_validation_layers)
			{
				device_create_info.enabledLayerCount = (uint32_t)rend.validation_layers.size();
				device_create_info.ppEnabledLayerNames = rend.validation_layers.data();
			}
			else
			{
				device_create_info.enabledLayerCount = 0;
			}

			VkCheckResult(vkCreateDevice(rend.physical_device, &device_create_info, nullptr, &rend.device));
			vkGetDeviceQueue(rend.device, queue_family_indices.graphics_family.value(), 0, &rend.graphics_queue);
			vkGetDeviceQueue(rend.device, queue_family_indices.present_family.value(), 0, &rend.present_queue);
		}

		CreateSwapChain();
		CreateImageViews();

		CreateRenderPass();
		CreateDescriptorSetLayout();
		CreateGraphicsPipeline();

		CreateCommandPool();
		CreateDepthResources();
		CreateFramebuffers();
		CreateTextureImage();
		CreateTextureImageView();
		CreateTextureSampler();
		CreateVertexBuffer();
		CreateIndexBuffer();
		CreateUniformBuffers();
		CreateDescriptorPool();
		CreateDescriptorSets();
		CreateCommandBuffers();
		CreateSyncObjects();
	}

	void Exit()
	{
		vkDeviceWaitIdle(rend.device);

		vkDestroySampler(rend.device, rend.texture_sampler, nullptr);
		vkDestroyImageView(rend.device, rend.texture_image_view, nullptr);
		vkDestroyImage(rend.device, rend.texture_image, nullptr);
		vkFreeMemory(rend.device, rend.texture_image_memory, nullptr);
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroyFence(rend.device, rend.in_flight_fences[i], nullptr);
			vkDestroySemaphore(rend.device, rend.render_finished_semaphores[i], nullptr);
			vkDestroySemaphore(rend.device, rend.image_available_semaphores[i], nullptr);
		}
		vkDestroyBuffer(rend.device, rend.index_buffer, nullptr);
		vkFreeMemory(rend.device, rend.index_buffer_memory, nullptr);
		vkDestroyBuffer(rend.device, rend.vertex_buffer, nullptr);
		vkFreeMemory(rend.device, rend.vertex_buffer_memory, nullptr);
		// Destroying the command pool will also destroy any command buffers associated with that pool
		vkDestroyCommandPool(rend.device, rend.command_pool, nullptr);
		vkDestroyRenderPass(rend.device, rend.render_pass, nullptr);
		vkDestroyPipeline(rend.device, rend.graphics_pipeline, nullptr);
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroyBuffer(rend.device, rend.uniform_buffers[i], nullptr);
			vkFreeMemory(rend.device, rend.uniform_buffers_memory[i], nullptr);
		}
		vkDestroyDescriptorPool(rend.device, rend.descriptor_pool, nullptr);
		vkDestroyDescriptorSetLayout(rend.device, rend.descriptor_set_layout, nullptr);
		vkDestroyPipelineLayout(rend.device, rend.pipeline_layout, nullptr);
		DestroySwapChain();
		vkDestroyDevice(rend.device, nullptr);
		if (s_enable_validation_layers)
		{
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(rend.instance, "vkDestroyDebugUtilsMessengerEXT");
			if (!func)
			{
				VK_EXCEPT("Vulkan", "Could not find function pointer vkDestroyDebugUtilsMessengerEXT");
			}

			func(rend.instance, rend.debug_messenger, nullptr);
		}
		vkDestroySurfaceKHR(rend.instance, rend.surface, nullptr);
		vkDestroyInstance(rend.instance, nullptr);
	}

	void RenderFrame()
	{
		// Wait for completion of all rendering on the GPU
		vkWaitForFences(rend.device, 1, &rend.in_flight_fences[rend.current_frame], VK_TRUE, UINT64_MAX);

		// Get an available image index from the swap chain
		uint32_t image_index;
		VkResult image_result = vkAcquireNextImageKHR(rend.device, rend.swapchain, UINT64_MAX, rend.image_available_semaphores[rend.current_frame], VK_NULL_HANDLE, &image_index);

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
		vkResetFences(rend.device, 1, &rend.in_flight_fences[rend.current_frame]);

		// Reset and record the command buffer
		vkResetCommandBuffer(rend.command_buffers[rend.current_frame], 0);
		RecordCommandBuffer(rend.command_buffers[rend.current_frame], image_index);

		// Update UBOs
		UpdateUniformBuffer(rend.current_frame);

		// Submit the command buffer for execution
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore wait_semaphores[] = { rend.image_available_semaphores[rend.current_frame] };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = wait_semaphores;
		submit_info.pWaitDstStageMask = wait_stages;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &rend.command_buffers[rend.current_frame];

		VkSemaphore signal_semaphores[] = { rend.render_finished_semaphores[rend.current_frame] };
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = signal_semaphores;

		VkCheckResult(vkQueueSubmit(rend.graphics_queue, 1, &submit_info, rend.in_flight_fences[rend.current_frame]));

		// Present
		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = signal_semaphores;

		VkSwapchainKHR swapchains[] = { rend.swapchain };
		present_info.swapchainCount = 1;
		present_info.pSwapchains = swapchains;
		present_info.pImageIndices = &image_index;
		present_info.pResults = nullptr;

		VkResult present_result = vkQueuePresentKHR(rend.graphics_queue, &present_info);

		if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR || rend.framebuffer_resized)
		{
			rend.framebuffer_resized = false;
			RecreateSwapChain();
		}
		else
		{
			VkCheckResult(present_result);
		}

		rend.current_frame = (rend.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

}
