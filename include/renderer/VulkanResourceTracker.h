#pragma once
#include "renderer/VulkanBackend.h"

namespace VulkanResourceTracker
{

	void Init();
	void Exit();

	void TrackBuffer(VkBuffer buffer);
	void TrackImage(VkImage image, VkImageLayout layout);

};
