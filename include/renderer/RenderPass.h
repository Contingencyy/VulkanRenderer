#pragma once
#include "renderer/vulkan/VulkanTypes.h"
#include "renderer/RenderTypes.h"

class RenderPass
{
public:
	enum AttachmentSlot
	{
		ATTACHMENT_SLOT_READ_ONLY0,
		ATTACHMENT_SLOT_READ_ONLY1,
		ATTACHMENT_SLOT_READ_WRITE0,
		ATTACHMENT_SLOT_READ_WRITE1,
		ATTACHMENT_SLOT_COLOR0 = ATTACHMENT_SLOT_READ_WRITE0,
		ATTACHMENT_SLOT_COLOR1 = ATTACHMENT_SLOT_READ_WRITE1,
		ATTACHMENT_SLOT_DEPTH_STENCIL,
		ATTACHMENT_SLOT_NUM_READ_ONLY_ATTACHMENTS = 2,
		ATTACHMENT_SLOT_NUM_READ_WRITE_ATTACHMENTS = 3,
		ATTACHMENT_SLOT_NUM_SLOTS = 5
	};

	struct AttachmentInfo
	{
		TextureFormat format = TEXTURE_FORMAT_UNDEFINED;
		VkImageLayout expected_layout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_MAX_ENUM;
		VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_MAX_ENUM;
		VkClearValue clear_value = {};
	};

	struct Attachment
	{
		VulkanImageView view;
		AttachmentInfo info;
	};

	struct Stage
	{
		VulkanPipeline pipeline;
		Attachment attachments[ATTACHMENT_SLOT_NUM_SLOTS];
	};

public:
	RenderPass() = default;
	explicit RenderPass(const std::vector<Stage>& stages);
	~RenderPass();

	void BeginStage(VulkanCommandBuffer& command_buffer, uint32_t stage_index, uint32_t render_width, uint32_t render_height);
	void EndStage(const VulkanCommandBuffer& command_buffer, uint32_t stage_index);

	void SetStageAttachment(uint32_t stage_index, AttachmentSlot slot, const VulkanImageView& attachment_view);
	uint32_t GetStageCount();

private:
	std::vector<Stage> m_stages;

};
