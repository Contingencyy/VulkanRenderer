#include "Precomp.h"
#include "renderer/RenderPass.h"
#include "renderer/vulkan/VulkanCommands.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "renderer/vulkan/VulkanResourceTracker.h"

static inline VkRenderingAttachmentInfo ToVkRenderingAttachmentInfo(const RenderPass::Attachment& attachment)
{
	VkRenderingAttachmentInfo attachment_info = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	attachment_info.imageView = attachment.view.vk_image_view;
	attachment_info.imageLayout = attachment.info.expected_layout;

	attachment_info.clearValue = attachment.info.clear_value;
	attachment_info.loadOp = attachment.info.load_op;
	attachment_info.storeOp = attachment.info.store_op;

	attachment_info.resolveImageView = VK_NULL_HANDLE;
	attachment_info.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;

	return attachment_info;
}

static inline bool IsAttachmentValid(const RenderPass::Attachment& attachment)
{
	return (
		attachment.view.vk_image_view
	);
}

RenderPass::RenderPass(const std::vector<Stage>& stages)
	: m_stages(stages)
{
}

RenderPass::~RenderPass()
{
	for (const auto& stage : m_stages)
	{
		Vulkan::DestroyPipeline(stage.pipeline);
	}
}

void RenderPass::BeginStage(VulkanCommandBuffer& command_buffer, uint32_t stage_index, uint32_t render_width, uint32_t render_height)
{
	Stage& stage = m_stages[stage_index];
	std::vector<VulkanImageLayoutTransition> attachment_transitions;

	for (uint32_t i = 0; i < ATTACHMENT_SLOT_NUM_SLOTS; ++i)
	{
		if (IsAttachmentValid(stage.attachments[i]))
		{
			if (stage.attachments[i].info.expected_layout != Vulkan::ResourceTracker::GetImageLayout({ .image = stage.attachments[i].view.image }))
				attachment_transitions.emplace_back(stage.attachments[i].view.image, stage.attachments[i].info.expected_layout);
		}
	}

	Vulkan::Command::TransitionLayouts(command_buffer, attachment_transitions);

	if (stage.pipeline.type == VULKAN_PIPELINE_TYPE_GRAPHICS)
	{
		std::vector<VkRenderingAttachmentInfo> color_attachment_infos;
		VkRenderingAttachmentInfo depth_attachment_info = ToVkRenderingAttachmentInfo(stage.attachments[ATTACHMENT_SLOT_DEPTH_STENCIL]);

		for (uint32_t slot = ATTACHMENT_SLOT_COLOR0; slot < ATTACHMENT_SLOT_DEPTH_STENCIL; ++slot)
		{
			if (IsAttachmentValid(stage.attachments[slot]))
			{
				color_attachment_infos.push_back(ToVkRenderingAttachmentInfo(stage.attachments[slot]));
			}
		}
		
		Vulkan::Command::BeginRendering(command_buffer,
			color_attachment_infos.size(), color_attachment_infos.data(),
			IsAttachmentValid(stage.attachments[ATTACHMENT_SLOT_DEPTH_STENCIL]) ? &depth_attachment_info : nullptr,
			nullptr,
			render_width, render_height
		);
	}
	else if (stage.pipeline.type == VULKAN_PIPELINE_TYPE_COMPUTE)
	{
		for (const auto& attachment : stage.attachments)
		{
			if (attachment.info.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
			{
				Vulkan::Command::ClearImage(command_buffer, attachment.view.image, attachment.info.clear_value.color);
			}
		}
	}

	if (stage.pipeline.vk_pipeline)
		Vulkan::Command::BindPipeline(command_buffer, stage.pipeline);
}

void RenderPass::EndStage(const VulkanCommandBuffer& command_buffer, uint32_t stage_index)
{
	Stage& stage = m_stages[stage_index];

	if (stage.pipeline.type == VULKAN_PIPELINE_TYPE_GRAPHICS)
	{
		Vulkan::Command::EndRendering(command_buffer);
	}
}

void RenderPass::SetStageAttachment(uint32_t stage_index, RenderPass::AttachmentSlot slot, const VulkanImageView& attachment_view)
{
	m_stages[stage_index].attachments[slot].view = attachment_view;
}

uint32_t RenderPass::GetStageCount()
{
	return static_cast<uint32_t>(m_stages.size());
}
