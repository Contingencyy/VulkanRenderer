#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"
#include "Shared.glsl.h"

typedef struct GLFWwindow;

namespace Vulkan
{

	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	void Init(::GLFWwindow* window, uint32_t window_width, uint32_t window_height);
	void Exit();

	bool BeginFrame();
	void CopyToBackBuffer(VulkanCommandBuffer& command_buffer, const VulkanImage& src_image);
	bool EndFrame(const VulkanFence& present_wait_fence);

	void WaitDeviceIdle();

	void GetOutputResolution(uint32_t& output_width, uint32_t& output_height);
	uint32_t GetCurrentBackBufferIndex();
	uint32_t GetCurrentFrameIndex();
	uint32_t GetLastFinishedFrameIndex();

	VulkanCommandQueue GetCommandQueue(VulkanCommandBufferType type);

	VulkanSampler CreateSampler(const SamplerCreateInfo& sampler_info);
	void DestroySampler(VulkanSampler sampler);

	struct GraphicsPipelineInfo
	{
		std::vector<VkVertexInputBindingDescription> input_bindings;
		std::vector<VkVertexInputAttributeDescription> input_attributes;
		std::vector<VkPushConstantRange> push_ranges;

		std::vector<TextureFormat> color_attachment_formats;
		TextureFormat depth_stencil_attachment_format = TEXTURE_FORMAT_UNDEFINED;

		const char* vs_path = nullptr;
		const char* fs_path = nullptr;

		bool depth_test = false;
		bool depth_write = false;
		VkCompareOp depth_func = VK_COMPARE_OP_LESS;

		VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
	};

	VulkanPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info);

	struct ComputePipelineInfo
	{
		std::vector<VkPushConstantRange> push_ranges;
		const char* cs_path;
	};

	VulkanPipeline CreateComputePipeline(const ComputePipelineInfo& info);
	void DestroyPipeline(const VulkanPipeline& pipeline);

	void InitImGui(::GLFWwindow* window);
	void ExitImGui();
	VkDescriptorSet AddImGuiTexture(VkImage image, VkImageView image_view, VkSampler sampler);

}
