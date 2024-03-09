#include "Precomp.h"
#include "renderer/vulkan/VulkanSwapChain.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanUtils.h"
#include "renderer/vulkan/VulkanResourceTracker.h"

#include "GLFW/glfw3.h"

namespace Vulkan
{

	/*
		============================ PRIVATE FUNCTIONS =================================
	*/

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

	/*
		============================ PUBLIC INTERFACE FUNCTIONS =================================
	*/

	void CreateSwapChain()
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

		QueueIndices queue_indices = FindQueueIndices();
		uint32_t queue_family_indices[] = { queue_indices.graphics_compute, queue_indices.present };

		if (queue_indices.graphics_compute != queue_indices.present)
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

		std::vector<VkImage> vk_swapchain_images(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkCheckResult(vkGetSwapchainImagesKHR(vk_inst.device, vk_inst.swapchain.swapchain, &image_count, vk_swapchain_images.data()));

		vk_inst.swapchain.extent = extent;
		vk_inst.swapchain.format = create_info.imageFormat;

		VkSemaphoreCreateInfo semaphore_info = {};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vk_inst.swapchain.image_available_semaphores.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);

		vk_inst.swapchain.images.resize(image_count);
		for (size_t i = 0; i < vk_inst.swapchain.image_available_semaphores.size(); ++i)
		{
			VkCheckResult(vkCreateSemaphore(vk_inst.device, &semaphore_info, nullptr, &vk_inst.swapchain.image_available_semaphores[i]));

			VulkanImage swapchain_image = {};
			swapchain_image.vk_image = vk_swapchain_images[i];
			swapchain_image.memory = {};
			swapchain_image.vk_format = vk_inst.swapchain.format;
			swapchain_image.width = vk_inst.swapchain.extent.width;
			swapchain_image.height = vk_inst.swapchain.extent.height;
			swapchain_image.depth = 1;
			swapchain_image.num_mips = 1;
			swapchain_image.num_layers = 1;

			ResourceTracker::TrackImage(swapchain_image, VK_IMAGE_LAYOUT_UNDEFINED);
		}
	}

	void DestroySwapChain()
	{
		for (size_t i = 0; i < vk_inst.swapchain.image_available_semaphores.size(); ++i)
		{
			ResourceTracker::RemoveImage(vk_inst.swapchain.images[i]);
			vkDestroySemaphore(vk_inst.device, vk_inst.swapchain.image_available_semaphores[i], nullptr);
		}

		vkDestroySwapchainKHR(vk_inst.device, vk_inst.swapchain.swapchain, nullptr);
	}

	bool SwapChainAcquireNextImage()
	{
		VkResult image_result = vkAcquireNextImageKHR(vk_inst.device, vk_inst.swapchain.swapchain, UINT64_MAX,
			vk_inst.swapchain.image_available_semaphores[vk_inst.current_frame], VK_NULL_HANDLE, &vk_inst.swapchain.current_image);

		return image_result;
	}

	bool SwapChainPresent(const std::vector<VulkanFence>& wait_semaphores)
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

		VkResult present_result = vkQueuePresentKHR(vk_inst.queues.graphics_compute->GetVkQueue(), &present_info);
		vk_inst.current_frame = (vk_inst.current_frame + 1) % VulkanInstance::MAX_FRAMES_IN_FLIGHT;

		return present_result;
	}

	void SwapChainRecreate()
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

	void SwapChainCopyFrom(VulkanCommandBuffer& command_buffer, VulkanImage src_image)
	{
		// Copy final result to swapchain image
		// We use vkCmdBlitImage here to have format conversions done automatically for us
		// E.g. R8G8B8A8 to B8G8R8A8
		VulkanImage swapchain_image = vk_inst.swapchain.images[vk_inst.swapchain.current_image];

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
			command_buffer.vk_command_buffer,
			src_image.vk_image, ResourceTracker::GetImageLayout({ src_image }),
			swapchain_image.vk_image, ResourceTracker::GetImageLayout({ swapchain_image }),
			1, &blit_region, VK_FILTER_NEAREST
		);
	}

	void SwapChainSetVSync(bool enabled)
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

	bool SwapChainIsVSyncEnabled()
	{
		return vk_inst.swapchain.vsync_enabled;
	}

}
