#pragma once
#include "renderer/VulkanBackend.h"

struct TextureResource;

enum RenderPassType
{
	RenderPassType_Graphics,
	RenderPassType_Compute
};

class RenderPass
{
public:
	enum AttachmentType
	{
		AttachmentType_Color,
		AttachmentType_DepthStencil,
		AttachmentType_ReadOnly
	};

	struct AttachmentInfo
	{
		AttachmentType attachment_type = AttachmentType_Color;
		VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		VkImageLayout expected_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkClearValue clear_value = {};
	};

	struct Attachment
	{
		AttachmentInfo info;
		TextureResource* resource;
	};

public:
	RenderPass(RenderPassType type);
	~RenderPass();

	void Begin(VkCommandBuffer command_buffer);
	void PushConstants(VkCommandBuffer command_buffer, VkShaderStageFlags stage_bits, uint32_t offset, uint32_t size, const void* ptr);
	void SetDescriptorBufferOffsets(VkCommandBuffer command_buffer, uint32_t first, uint32_t count, const uint32_t* indices, const VkDeviceSize* offsets);
	void End(VkCommandBuffer command_buffer);

	void SetAttachments(const std::vector<Attachment>& attachments);
	std::vector<VkFormat> GetColorAttachmentFormats();
	VkFormat GetDepthStencilAttachmentFormat();

	void Build(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts,
		const std::vector<VkPushConstantRange>& push_constant_ranges, const Vulkan::GraphicsPipelineInfo& graphics_pipeline_info);
	void Build(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts,
		const std::vector<VkPushConstantRange>& push_constant_ranges, const Vulkan::ComputePipelineInfo& compute_pipeline_info);

private:
	RenderPassType m_type = RenderPassType_Graphics;

	VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;

	std::vector<Attachment> m_attachments;

};
