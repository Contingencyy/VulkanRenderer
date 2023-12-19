#include "renderer/VulkanResourceTracker.h"

#include <unordered_map>

namespace VulkanResourceTracker
{

	struct TrackedBuffer
	{
		VkBuffer vk_buffer;
	};

	struct TrackedImage
	{
		VkImage vk_image;
		VkPipelineStageFlags last_pipeline_stage_used;
	};

	struct Data
	{
		std::unordered_map<VkBuffer, TrackedBuffer> tracked_buffers;
		std::unordered_map<VkImage, TrackedImage> tracked_images;
	} static *data;

	void Init()
	{
		data = new Data();
	}

	void Exit()
	{

	}

	void TrackBuffer(VkBuffer vk_buffer)
	{
		auto tracked = data->tracked_buffers.find(vk_buffer);
		VK_ASSERT(tracked == data->tracked_buffers.end() && "Tracked a buffer that was already being tracked");

		if (tracked == data->tracked_buffers.end())
		{
			data->tracked_buffers.emplace(vk_buffer, TrackedBuffer{ .vk_buffer = vk_buffer });
		}
	}

	void TrackImage(VkImage vk_image, VkImageLayout layout)
	{
		auto tracked = data->tracked_images.find(vk_image);
		VK_ASSERT(tracked == data->tracked_images.end() && "Tracked an image that was already being tracked");

		if (tracked == data->tracked_images.end())
		{
			data->tracked_images.emplace(vk_image, TrackedImage{ .vk_image = vk_image });
		}
	}

}
