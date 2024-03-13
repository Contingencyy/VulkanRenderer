#include "Precomp.h"
#include "renderer/vulkan/VulkanSwapChain.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanSync.h"
#include "renderer/vulkan/VulkanUtils.h"
#include "renderer/vulkan/VulkanResourceTracker.h"

#include "GLFW/glfw3.h"

namespace Vulkan
{

	namespace SwapChain
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

		static VkExtent2D ChooseSwapChainExtent(uint32_t output_width, uint32_t output_height, const VkSurfaceCapabilitiesKHR& capabilities)
		{
			if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
			{
				return capabilities.currentExtent;
			}
			else
			{
				VkExtent2D actual_extent = { output_width, output_height };

				actual_extent.width = std::clamp(actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
				actual_extent.height = std::clamp(actual_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

				return actual_extent;
			}
		}

		/*
			============================ PUBLIC INTERFACE FUNCTIONS =================================
		*/

		void Create(uint32_t output_width, uint32_t output_height)
		{
			SwapChainSupportDetails swapchain_support = QuerySwapChainSupport(vk_inst.physical_device);
			if (swapchain_support.formats.empty() || swapchain_support.present_modes.empty())
			{
				VK_EXCEPT("Vulkan::CreateSwapChain", "SwapChain does not support any formats or any present modes");
			}

			VkSurfaceFormatKHR surface_format = ChooseSwapChainFormat(swapchain_support.formats);
			VkExtent2D extent = ChooseSwapChainExtent(output_width, output_height, swapchain_support.capabilities);

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

			uint32_t queue_family_indices = vk_inst.queues.graphics_compute.queue_family_index;

			//if (queue_indices.graphics_compute != queue_indices.present)
			//{
			//	// NOTE: VK_SHARING_MODE_CONCURRENT specifies that swap chain images are shared between multiple queue families without explicit ownership
			//	create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			//	create_info.queueFamilyIndexCount = 2;
			//	create_info.pQueueFamilyIndices = queue_family_indices;
			//}
			//else
			//{
			// NOTE: VK_SHARING_MODE_EXCLUSIVE specifies that swap chain images are owned by a single queue family and ownership must be explicitly transferred
			create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			create_info.queueFamilyIndexCount = 0;
			create_info.pQueueFamilyIndices = nullptr;
			//}

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

			std::vector<VkImage> vk_swapchain_images(Vulkan::MAX_FRAMES_IN_FLIGHT);
			VkCheckResult(vkGetSwapchainImagesKHR(vk_inst.device, vk_inst.swapchain.swapchain, &image_count, vk_swapchain_images.data()));

			vk_inst.swapchain.extent = extent;
			vk_inst.swapchain.format = create_info.imageFormat;

			vk_inst.swapchain.image_available_fences.resize(Vulkan::MAX_FRAMES_IN_FLIGHT);
			vk_inst.swapchain.images.resize(image_count);

			for (size_t i = 0; i < Vulkan::MAX_FRAMES_IN_FLIGHT; ++i)
			{
				vk_inst.swapchain.image_available_fences[i] = CreateFence(VULKAN_FENCE_TYPE_BINARY);

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

		void Destroy()
		{
			for (size_t i = 0; i < Vulkan::MAX_FRAMES_IN_FLIGHT; ++i)
			{
				ResourceTracker::RemoveImage(vk_inst.swapchain.images[i]);
				DestroyFence(vk_inst.swapchain.image_available_fences[i]);
			}

			vkDestroySwapchainKHR(vk_inst.device, vk_inst.swapchain.swapchain, nullptr);
		}

		VkResult AcquireNextImage()
		{
			VkResult image_result = vkAcquireNextImageKHR(vk_inst.device, vk_inst.swapchain.swapchain, UINT64_MAX,
				vk_inst.swapchain.image_available_fences[vk_inst.current_frame_index].vk_semaphore, VK_NULL_HANDLE, &vk_inst.swapchain.current_image);

			return image_result;
		}

		VkResult Present(const std::vector<VulkanFence>& wait_fences)
		{
			std::vector<VkSemaphore> vk_wait_semaphores;
			for (const auto& wait_fence : wait_fences)
			{
				VK_ASSERT(wait_fence.type == VULKAN_FENCE_TYPE_BINARY && "SwapChainPresent only supports binary semaphores");
				vk_wait_semaphores.push_back(wait_fence.vk_semaphore);
			}

			VkPresentInfoKHR present_info = {};
			present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			present_info.waitSemaphoreCount = (uint32_t)vk_wait_semaphores.size();
			present_info.pWaitSemaphores = vk_wait_semaphores.data();

			present_info.swapchainCount = 1;
			present_info.pSwapchains = &vk_inst.swapchain.swapchain;
			present_info.pImageIndices = &vk_inst.swapchain.current_image;

			VkSwapchainPresentModeInfoEXT present_mode_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT };
			present_mode_info.swapchainCount = 1;
			present_mode_info.pPresentModes = &vk_inst.swapchain.desired_present_mode;

			present_info.pNext = &present_mode_info;
			VkResult present_result = vkQueuePresentKHR(vk_inst.queues.graphics_compute.vk_queue, &present_info);

			return present_result;
		}

		void SetVSync(bool enabled)
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

	}

}
