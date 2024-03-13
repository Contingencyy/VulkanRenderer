#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/DescriptorBuffer.h"
#include "renderer/DescriptorAllocation.h"
#include "renderer/RenderTypes.h"
#include "Shared.glsl.h"

typedef struct GLFWwindow;

namespace Vulkan
{

	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	void Init(::GLFWwindow* window, uint32_t window_width, uint32_t window_height);
	void Exit();

	void BeginFrame();
	void EndFrame();

	void ResizeOutputResolution(uint32_t output_width, uint32_t output_height);
	void WaitDeviceIdle();

	inline uint32_t GetCurrentBackBufferIndex();
	inline uint32_t GetCurrentFrameIndex();
	inline uint32_t GetLastFinishedFrameIndex();

	VulkanCommandQueue GetCommandQueue(VulkanCommandBufferType type);

	DescriptorAllocation AllocateDescriptors(VkDescriptorType type, uint32_t num_descriptors = 1, uint32_t align = 0);
	void FreeDescriptors(const DescriptorAllocation& descriptors);
	std::vector<VkDescriptorSetLayout> GetDescriptorBufferLayouts();
	std::vector<VkDescriptorBufferBindingInfoEXT> GetDescriptorBufferBindingInfos();
	std::vector<uint32_t> GetDescriptorBufferIndices();
	std::vector<uint64_t> GetDescriptorBufferOffsets();
	size_t GetDescriptorTypeSize(VkDescriptorType type);

	VulkanSampler CreateSampler(const SamplerCreateInfo& sampler_info);
	void DestroySampler(VulkanSampler sampler);

	VkPipelineLayout CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts, const std::vector<VkPushConstantRange>& push_constant_ranges);

	struct GraphicsPipelineInfo
	{
		std::vector<VkVertexInputBindingDescription> input_bindings;
		std::vector<VkVertexInputAttributeDescription> input_attributes;

		std::vector<TextureFormat> color_attachment_formats;
		TextureFormat depth_stencil_attachment_format = TEXTURE_FORMAT_UNDEFINED;

		const char* vs_path;
		const char* fs_path;

		bool depth_test = false;
		bool depth_write = false;
		VkCompareOp depth_func = VK_COMPARE_OP_LESS;

		VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
	};

	VkPipeline CreateGraphicsPipeline(const GraphicsPipelineInfo& info, VkPipelineLayout pipeline_layout);

	struct ComputePipelineInfo
	{
		const char* cs_path;
	};

	VkPipeline CreateComputePipeline(const ComputePipelineInfo& info, VkPipelineLayout pipeline_layout);

	void InitImGui(::GLFWwindow* window, const VulkanCommandBuffer& command_buffer);
	void ExitImGui();
	VkDescriptorSet AddImGuiTexture(VkImage image, VkImageView image_view, VkSampler sampler);

}
