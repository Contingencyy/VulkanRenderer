#include "Precomp.h"
#include "renderer/RenderPass.h"
#include "renderer/RenderTypes.h"
#include "renderer/Texture.h"

RenderPass::RenderPass(RenderPassType type)
	: m_type(type)
{
}

RenderPass::~RenderPass()
{
	vkDestroyPipeline(vk_inst.device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(vk_inst.device, m_pipeline_layout, nullptr);
}

void RenderPass::Begin(VkCommandBuffer command_buffer, const BeginInfo& begin_info)
{
	std::vector<VkImageMemoryBarrier2> barriers;

	if (m_type == RENDER_PASS_TYPE_GRAPHICS)
	{
		std::vector<VkRenderingAttachmentInfo> color_attachment_infos;
		VkRenderingAttachmentInfo depth_attachment_info = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };

		for (auto& attachment : m_attachments)
		{
			if (attachment.info.slot == ATTACHMENT_SLOT_INVALID)
				continue;

			attachment.view->TransitionLayout(command_buffer, attachment.info.expected_layout);

			VkRenderingAttachmentInfo* attachment_info = &depth_attachment_info;
			if (IsColorAttachment(attachment.info.slot))
			{
				attachment_info = &color_attachment_infos.emplace_back();
			}

			attachment_info->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			attachment_info->imageView = attachment.view->view;
			attachment_info->imageLayout = attachment.view->GetLayout();
			attachment_info->loadOp = attachment.info.load_op;
			attachment_info->storeOp = attachment.info.store_op;
			attachment_info->clearValue = attachment.info.clear_value;
			attachment_info->resolveImageView = VK_NULL_HANDLE;
			attachment_info->resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment_info->resolveMode = VK_RESOLVE_MODE_NONE;
			attachment_info->pNext = nullptr;
		}

		VkRenderingInfo rendering_info = {};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.colorAttachmentCount = (uint32_t)color_attachment_infos.size();
		rendering_info.pColorAttachments = color_attachment_infos.data();
		rendering_info.pDepthAttachment = m_attachments[ATTACHMENT_SLOT_DEPTH_STENCIL].info.slot == ATTACHMENT_SLOT_INVALID ? nullptr : &depth_attachment_info;
		rendering_info.pStencilAttachment = nullptr;
		rendering_info.viewMask = 0;
		rendering_info.renderArea = { 0, 0, begin_info.render_width, begin_info.render_height };
		rendering_info.layerCount = 1;
		rendering_info.flags = 0;
		rendering_info.pNext = nullptr;

		// Begin rendering and bind the pipeline state
		vkCmdBeginRendering(command_buffer, &rendering_info);
		if (m_pipeline)
		{
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		}
	}
	else if (m_type == RENDER_PASS_TYPE_COMPUTE)
	{
		// Clear all attachments that are marked as LOAD_OP_CLEAR
		//std::vector<VkImageMemoryBarrier2> clear_barriers;

		//for (size_t i = 0; i < m_attachments.size(); ++i)
		//{
		//	Attachment& attachment = m_attachments[i];

		//	if (attachment.info.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
		//	{
		//		clear_barriers.push_back(Vulkan::ImageMemoryBarrier(attachment.resource->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
		//	}
		//}

		//// Transition all attachments which need to be cleared to TRANSFER_DST
		//Vulkan::CmdTransitionImageLayouts(command_buffer, clear_barriers);

		//for (size_t i = 0; i < m_attachments.size(); ++i)
		//{
		//	Attachment& attachment = m_attachments[i];

		//	if (attachment.info.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
		//	{
		//		VkImageSubresourceRange clear_range = {};
		//		clear_range.baseMipLevel = 0;
		//		clear_range.levelCount = 1;
		//		clear_range.baseArrayLayer = 0;
		//		clear_range.layerCount = 1;

		//		if (attachment.info.attachment_type == AttachmentType_DepthStencil)
		//		{
		//			clear_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		//			vkCmdClearDepthStencilImage(command_buffer, attachment.resource->image.image,
		//				attachment.resource->image.layout, &attachment.info.clear_value.depthStencil, 1, &clear_range);
		//		}
		//		else
		//		{
		//			clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		//			vkCmdClearColorImage(command_buffer, attachment.resource->image.image,
		//				attachment.resource->image.layout, &attachment.info.clear_value.color, 1, &clear_range);
		//		}
		//	}
		//}

		for (auto& attachment : m_attachments)
		{
			if (attachment.info.slot == ATTACHMENT_SLOT_INVALID)
				continue;

			attachment.view->TransitionLayout(command_buffer, attachment.info.expected_layout);
		}

		if (m_pipeline)
		{
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
		}
	}
}

void RenderPass::PushConstants(VkCommandBuffer command_buffer, VkShaderStageFlags stage_bits, uint32_t offset, uint32_t size, const void* ptr)
{
	vkCmdPushConstants(command_buffer, m_pipeline_layout, stage_bits, offset, size, ptr);
}

void RenderPass::SetDescriptorBufferOffsets(VkCommandBuffer command_buffer, uint32_t first, uint32_t count, const uint32_t* indices, const VkDeviceSize* offsets)
{
	VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
	if (m_type == RENDER_PASS_TYPE_COMPUTE)
	{
		bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
	}

	vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext(command_buffer, bind_point, m_pipeline_layout, first, count, indices, offsets);
}

void RenderPass::End(VkCommandBuffer command_buffer)
{
	if (m_type == RENDER_PASS_TYPE_GRAPHICS)
	{
		vkCmdEndRendering(command_buffer);
	}
}

void RenderPass::SetAttachmentInfos(const std::vector<AttachmentInfo>& attachment_infos)
{
	for (const auto& info : attachment_infos)
	{
		m_attachments[info.slot].info = info;
	}
}

void RenderPass::SetAttachment(AttachmentSlot slot, TextureView* attachment_view)
{
	VK_ASSERT(attachment_view->format == m_attachments[slot].info.format && "The format of the attachment does not match the format specified in the attachment info");
	VK_ASSERT(slot < m_attachments.size() && "Tried to set an attachment with an index larger than the total amount of attachments specified in the render pass");

	m_attachments[slot].view = attachment_view;
}

std::vector<VkFormat> RenderPass::GetColorAttachmentFormats()
{
	VK_ASSERT(m_type == RENDER_PASS_TYPE_GRAPHICS);
	std::vector<VkFormat> formats;

	for (const auto& attachment : m_attachments)
	{
		if (attachment.info.slot == ATTACHMENT_SLOT_INVALID)
			continue;

		if (IsColorAttachment(attachment.info.slot))
		{
			formats.push_back(attachment.info.format);
		}
	}

	return formats;
}

VkFormat RenderPass::GetDepthStencilAttachmentFormat()
{
	VK_ASSERT(m_type == RENDER_PASS_TYPE_GRAPHICS);

	for (const auto& attachment : m_attachments)
	{
		if (IsDepthStencilAttachment(attachment.info.slot))
		{
			return attachment.info.format;
		}
	}

	return VK_FORMAT_UNDEFINED;
}

void RenderPass::Build(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts,
	const std::vector<VkPushConstantRange>& push_constant_ranges, const Vulkan::GraphicsPipelineInfo& graphics_pipeline_info)
{
	VK_ASSERT(m_type == RENDER_PASS_TYPE_GRAPHICS && "Tried to build a graphics render pass, but the actual type of the render pass is not of type GRAPHICS");

	m_pipeline_layout = Vulkan::CreatePipelineLayout(descriptor_set_layouts, push_constant_ranges);
	m_pipeline = Vulkan::CreateGraphicsPipeline(graphics_pipeline_info, m_pipeline_layout);
}

void RenderPass::Build(const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts,
	const std::vector<VkPushConstantRange>& push_constant_ranges, const Vulkan::ComputePipelineInfo& compute_pipeline_info)
{
	VK_ASSERT(m_type == RENDER_PASS_TYPE_COMPUTE && "Tried to build a compute render pass, but the actual type of the render pass is not of type COMPUTE");

	m_pipeline_layout = Vulkan::CreatePipelineLayout(descriptor_set_layouts, push_constant_ranges);
	m_pipeline = Vulkan::CreateComputePipeline(compute_pipeline_info, m_pipeline_layout);
}

bool RenderPass::IsColorAttachment(AttachmentSlot slot)
{
	return (slot == ATTACHMENT_SLOT_COLOR0) || (slot == ATTACHMENT_SLOT_COLOR1);
}

bool RenderPass::IsDepthStencilAttachment(AttachmentSlot slot)
{
	return slot == ATTACHMENT_SLOT_DEPTH_STENCIL;
}
