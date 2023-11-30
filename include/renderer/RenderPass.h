#pragma once
#include "renderer/VulkanBackend.h"

struct TextureResource;

enum RenderPassType
{
	RENDER_PASS_TYPE_GRAPHICS,
	RENDER_PASS_TYPE_COMPUTE
};

class RenderPass
{
public:
	enum AttachmentType
	{
		ATTACHMENT_TYPE_COLOR,
		ATTACHMENT_TYPE_DEPTH_STENCIL,
		ATTACHMENT_TYPE_READ_ONLY
	};

	struct AttachmentInfo
	{
		AttachmentType attachment_type = ATTACHMENT_TYPE_COLOR;
		VkFormat format;
		VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		VkImageLayout expected_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkClearValue clear_value = {};
	};

	struct Attachment
	{
		AttachmentInfo info;
		Vulkan::ImageView view;
	};

	struct BeginInfo
	{
		uint32_t render_width;
		uint32_t render_height;
	};

public:
	RenderPass(RenderPassType type);
	~RenderPass();

	void Begin(VkCommandBuffer command_buffer, const BeginInfo& begin_info);
	void PushConstants(VkCommandBuffer command_buffer, VkShaderStageFlags stage_bits, uint32_t offset, uint32_t size, const void* ptr);
	void SetDescriptorBufferOffsets(VkCommandBuffer command_buffer, uint32_t first, uint32_t count, const uint32_t* indices, const VkDeviceSize* offsets);
	void End(VkCommandBuffer command_buffer);

	void SetAttachmentInfos(const std::vector<AttachmentInfo>& attachment_infos);
	void SetAttachment(const Vulkan::ImageView& attachment_view, uint32_t index);
	std::vector<VkFormat> GetColorAttachmentFormats();
	VkFormat GetDepthStencilAttachmentFormat();

	void Build(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts,
		const std::vector<VkPushConstantRange>& push_constant_ranges, const Vulkan::GraphicsPipelineInfo& graphics_pipeline_info);
	void Build(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts,
		const std::vector<VkPushConstantRange>& push_constant_ranges, const Vulkan::ComputePipelineInfo& compute_pipeline_info);

private:
	RenderPassType m_type = RENDER_PASS_TYPE_GRAPHICS;

	VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;

	std::vector<Attachment> m_attachments;

};
