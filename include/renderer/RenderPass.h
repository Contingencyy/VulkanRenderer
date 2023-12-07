#pragma once
#include "renderer/VulkanBackend.h"

#include <array>

struct TextureResource;

enum RenderPassType
{
	RENDER_PASS_TYPE_GRAPHICS,
	RENDER_PASS_TYPE_COMPUTE
};

class RenderPass
{
public:
	enum AttachmentSlot
	{
		// Attachment slots used by graphics passes
		ATTACHMENT_SLOT_INVALID = 0xffffffff,
		ATTACHMENT_SLOT_COLOR0,
		ATTACHMENT_SLOT_COLOR1,
		ATTACHMENT_SLOT_DEPTH_STENCIL,
		// Attachment slots used by compute passes
		ATTACHMENT_SLOT_READ_ONLY0 = ATTACHMENT_SLOT_COLOR0,
		ATTACHMENT_SLOT_READ_WRITE0 = ATTACHMENT_SLOT_COLOR1,
		ATTACHMENT_SLOT_MAX_SLOTS = 3
	};

	enum AttachmentType
	{
		ATTACHMENT_TYPE_COLOR,
		ATTACHMENT_TYPE_DEPTH_STENCIL,
		ATTACHMENT_TYPE_READ_ONLY,
		ATTACHMENT_TYPE_READ_WRITE
	};

	struct AttachmentInfo
	{
		AttachmentSlot attachment_slot = ATTACHMENT_SLOT_INVALID;
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
	void SetAttachment(AttachmentSlot slot, const Vulkan::ImageView& attachment_view);
	std::vector<VkFormat> GetColorAttachmentFormats();
	VkFormat GetDepthStencilAttachmentFormat();

	void Build(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts,
		const std::vector<VkPushConstantRange>& push_constant_ranges, const Vulkan::GraphicsPipelineInfo& graphics_pipeline_info);
	void Build(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts,
		const std::vector<VkPushConstantRange>& push_constant_ranges, const Vulkan::ComputePipelineInfo& compute_pipeline_info);

private:
	bool IsColorAttachment(AttachmentSlot slot);
	bool IsDepthStencilAttachment(AttachmentSlot slot);

private:
	RenderPassType m_type = RENDER_PASS_TYPE_GRAPHICS;

	VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;

	std::array<Attachment, ATTACHMENT_SLOT_MAX_SLOTS> m_attachments;

};
