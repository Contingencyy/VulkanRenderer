#include "renderer/Renderer.h"
#include "renderer/VulkanBackend.h"
#include "renderer/ResourceSlotmap.h"
#include "Common.h"

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include "GLFW/glfw3.h"

#include <array>
#include <vector>
#include <optional>
#include <assert.h>
#include <cstring>
#include <set>
#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>

namespace Renderer
{

	static constexpr uint32_t DEFAULT_DESCRIPTOR_COUNT_PER_TYPE = 1000;

	static constexpr uint32_t UNIFORM_BINDING = 0;
	static constexpr uint32_t STORAGE_BINDING = 1;
	static constexpr uint32_t TEXTURE_BINDING = 2;

	struct Texture
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageView image_view = VK_NULL_HANDLE;
		VkDeviceMemory device_memory = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;

		/*~Texture()
		{
			vkDestroySampler(vk_inst.device, sampler, nullptr);
			vkDestroyImageView(vk_inst.device, image_view, nullptr);
			vkDestroyImage(vk_inst.device, image, nullptr);
			vkFreeMemory(vk_inst.device, device_memory, nullptr);
		}*/
	};

	struct Mesh
	{
		VkBuffer vertex_buffer = VK_NULL_HANDLE;
		VkDeviceMemory vertex_buffer_memory = VK_NULL_HANDLE;
		VkBuffer index_buffer = VK_NULL_HANDLE;
		VkDeviceMemory index_buffer_memory = VK_NULL_HANDLE;

		/*~Mesh()
		{
			vkDestroyBuffer(vk_inst.device, vertex_buffer, nullptr);
			vkFreeMemory(vk_inst.device, vertex_buffer_memory, nullptr);
			vkDestroyBuffer(vk_inst.device, index_buffer, nullptr);
			vkFreeMemory(vk_inst.device, index_buffer_memory, nullptr);
		}*/
	};

	struct Data
	{
		::GLFWwindow* window = nullptr;

		ResourceSlotmap<Texture> texture_slotmap;
		ResourceSlotmap<Mesh> mesh_slotmap;

		VkRenderPass render_pass = VK_NULL_HANDLE;
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		VkPipeline graphics_pipeline = VK_NULL_HANDLE;

		VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
		VkBuffer descriptor_buffer = VK_NULL_HANDLE;
		VkDeviceMemory descriptor_buffer_memory = VK_NULL_HANDLE;
		void* descriptor_buffer_ptr = nullptr;
		uint32_t descriptor_uniform_buffers_current = 0;
		uint32_t descriptor_storage_buffers_current = 0;
		uint32_t descriptor_combined_image_samplers_current = 0;

		VkBuffer vertex_buffer = VK_NULL_HANDLE;
		VkDeviceMemory vertex_buffer_memory = VK_NULL_HANDLE;
		VkBuffer index_buffer = VK_NULL_HANDLE;
		VkDeviceMemory index_buffer_memory = VK_NULL_HANDLE;

		std::vector<VkBuffer> uniform_buffers;
		std::vector<VkDeviceMemory> uniform_buffers_memory;
		std::vector<void*> uniform_buffers_mapped;

		std::vector<VkCommandBuffer> command_buffers;
		std::vector<VkSemaphore> image_available_semaphores;
		std::vector<VkSemaphore> render_finished_semaphores;
		std::vector<VkFence> in_flight_fences;

		uint32_t current_frame = 0;
	} static data;

	struct UniformBufferObject
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 proj;
	};

	static const std::vector<Vertex> s_vertices =
	{
		{{ -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }},
		{{  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }},
		{{  0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }},
		{{ -0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }},

		{{ -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }},
		{{  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }},
		{{  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }},
		{{ -0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }},
	};

	static const std::vector<uint32_t> s_indices =
	{
		0, 1, 2, 2, 3, 0,
		4, 5, 6, 6, 7, 4,
	};

	static std::vector<char> ReadFile(const char* filepath)
	{
		std::ifstream file(filepath, std::ios::ate | std::ios::binary);
		if (!file.is_open())
		{
			VK_EXCEPT("Assets", "Could not open file: {}", filepath);
		}

		size_t file_size = (size_t)file.tellg();
		std::vector<char> buffer(file_size);

		file.seekg(0);
		file.read(buffer.data(), file_size);
		file.close();

		return buffer;
	}
	
	static VkVertexInputBindingDescription GetVertexBindingDescription()
	{
		VkVertexInputBindingDescription binding_desc = {};
		binding_desc.binding = 0;
		binding_desc.stride = sizeof(Vertex);
		binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		return binding_desc;
	}

	static std::array<VkVertexInputAttributeDescription, 3> GetVertexAttributeDescription()
	{
		std::array<VkVertexInputAttributeDescription, 3> attribute_desc = {};
		attribute_desc[0].binding = 0;
		attribute_desc[0].location = 0;
		attribute_desc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute_desc[0].offset = offsetof(Vertex, pos);

		attribute_desc[1].binding = 0;
		attribute_desc[1].location = 1;
		attribute_desc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute_desc[1].offset = offsetof(Vertex, color);

		attribute_desc[2].binding = 0;
		attribute_desc[2].location = 2;
		attribute_desc[2].format = VK_FORMAT_R32G32_SFLOAT;
		attribute_desc[2].offset = offsetof(Vertex, tex_coord);

		return attribute_desc;
	}

	static void CreateFramebuffers()
	{
		vk_inst.swapchain.framebuffers.resize(vk_inst.swapchain.image_views.size());

		for (size_t i = 0; i < vk_inst.swapchain.image_views.size(); ++i)
		{
			std::array<VkImageView, 2> attachments
			{
				vk_inst.swapchain.image_views[i],
				vk_inst.swapchain.depth_image_view
			};

			VkFramebufferCreateInfo frame_buffer_info = {};
			frame_buffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			frame_buffer_info.renderPass = data.render_pass;
			frame_buffer_info.attachmentCount = (uint32_t)attachments.size();
			frame_buffer_info.pAttachments = attachments.data();
			frame_buffer_info.width = vk_inst.swapchain.extent.width;
			frame_buffer_info.height = vk_inst.swapchain.extent.height;
			frame_buffer_info.layers = 1;

			VkCheckResult(vkCreateFramebuffer(vk_inst.device, &frame_buffer_info, nullptr, &vk_inst.swapchain.framebuffers[i]));
		}
	}

	static void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
	{
		VkCommandBuffer command_buffer = VulkanBackend::BeginSingleTimeCommands();

		VkBufferImageCopy region = {};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };

		vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		VulkanBackend::EndSingleTimeCommands(command_buffer);
	}

	static void CopyBuffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size)
	{
		VkCommandBuffer command_buffer = VulkanBackend::BeginSingleTimeCommands();

		VkBufferCopy copy_region = {};
		copy_region.srcOffset = 0;
		copy_region.dstOffset = 0;
		copy_region.size = size;
		vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

		VulkanBackend::EndSingleTimeCommands(command_buffer);
	}

	static void CreateVertexBuffer()
	{
		VkDeviceSize buffer_size = sizeof(s_vertices[0]) * s_vertices.size();

		// Staging buffer
		VkBuffer staging_buffer;
		VkDeviceMemory staging_buffer_memory;
		VulkanBackend::CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

		// NOTE: The driver may or may not have immediately copied this over to buffer memory (e.g. caching)
		// or writes to the buffer are not visible in the mapped memory yet.
		// To deal with this problem, you either have to use a memory heap that is host coherent (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		// or call vkFlushMappedMemoryRanges after writing to the mapped memory and then calling vkInvalidateMappedMemoryRanges before reading from it
		// The transfer of data to the GPU happens in the background and the specification states it is guaranteed to be complete as of the next call to vkQueueSubmit
		void* ptr;
		VkCheckResult(vkMapMemory(vk_inst.device, staging_buffer_memory, 0, buffer_size, 0, &ptr));
		memcpy(ptr, s_vertices.data(), (size_t)buffer_size);
		vkUnmapMemory(vk_inst.device, staging_buffer_memory);

		// Device local buffer
		VulkanBackend::CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, data.vertex_buffer, data.vertex_buffer_memory);
		CopyBuffer(staging_buffer, data.vertex_buffer, buffer_size);

		vkDestroyBuffer(vk_inst.device, staging_buffer, nullptr);
		vkFreeMemory(vk_inst.device, staging_buffer_memory, nullptr);
	}

	static void CreateIndexBuffer()
	{
		VkDeviceSize buffer_size = sizeof(s_indices[0]) * s_indices.size();

		// Staging buffer
		VkBuffer staging_buffer;
		VkDeviceMemory staging_buffer_memory;
		VulkanBackend::CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

		// NOTE: The driver may or may not have immediately copied this over to buffer memory (e.g. caching)
		// or writes to the buffer are not visible in the mapped memory yet.
		// To deal with this problem, you either have to use a memory heap that is host coherent (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		// or call vkFlushMappedMemoryRanges after writing to the mapped memory and then calling vkInvalidateMappedMemoryRanges before reading from it
		// The transfer of data to the GPU happens in the background and the specification states it is guaranteed to be complete as of the next call to vkQueueSubmit
		void* ptr;
		VkCheckResult(vkMapMemory(vk_inst.device, staging_buffer_memory, 0, buffer_size, 0, &ptr));
		memcpy(ptr, s_indices.data(), (size_t)buffer_size);
		vkUnmapMemory(vk_inst.device, staging_buffer_memory);

		// Device local buffer
		VulkanBackend::CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, data.index_buffer, data.index_buffer_memory);
		CopyBuffer(staging_buffer, data.index_buffer, buffer_size);

		vkDestroyBuffer(vk_inst.device, staging_buffer, nullptr);
		vkFreeMemory(vk_inst.device, staging_buffer_memory, nullptr);
	}

	static void CreateUniformBuffers()
	{
		data.uniform_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		data.uniform_buffers_memory.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		data.uniform_buffers_mapped.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);

		VkDeviceSize buffer_size = sizeof(UniformBufferObject);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VulkanBackend::CreateBuffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, data.uniform_buffers[i], data.uniform_buffers_memory[i]);
			VkCheckResult(vkMapMemory(vk_inst.device, data.uniform_buffers_memory[i], 0, buffer_size, 0, &data.uniform_buffers_mapped[i]));

			// Update descriptor
			VkBufferDeviceAddressInfoEXT buffer_address_info = {};
			buffer_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			buffer_address_info.buffer = data.uniform_buffers[i];

			VkDescriptorAddressInfoEXT descriptor_address_info = {};
			descriptor_address_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
			descriptor_address_info.address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_address_info);
			descriptor_address_info.format = VK_FORMAT_UNDEFINED;
			descriptor_address_info.range = sizeof(UniformBufferObject);

			VkDescriptorGetInfoEXT descriptor_info = {};
			descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
			descriptor_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptor_info.data.pUniformBuffer = &descriptor_address_info;

			VkDeviceSize binding_offset;
			vk_inst.pFunc.get_descriptor_set_layout_binding_offset_ext(vk_inst.device, data.descriptor_set_layout, UNIFORM_BINDING, &binding_offset);

			uint8_t* descriptor_ptr = (uint8_t*)data.descriptor_buffer_ptr + binding_offset + vk_inst.descriptor_sizes.uniform_buffer * data.descriptor_uniform_buffers_current;
			vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info, vk_inst.descriptor_sizes.uniform_buffer, descriptor_ptr);
			data.descriptor_uniform_buffers_current++;
		}
	}

	static void UpdateUniformBuffer(uint32_t current_image)
	{
		static auto start_time = std::chrono::high_resolution_clock::now();

		auto current_time = std::chrono::high_resolution_clock::now();
		float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

		UniformBufferObject ubo = {};
		ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.proj = glm::perspective(glm::radians(45.0f), vk_inst.swapchain.extent.width / (float)vk_inst.swapchain.extent.height, 0.1f, 10.0f);
		ubo.proj[1][1] *= -1.0f;

		memcpy(data.uniform_buffers_mapped[current_image], &ubo, sizeof(ubo));
	}

	static void CreateDescriptorSetLayout()
	{
		std::array<VkDescriptorSetLayoutBinding, 3> bindings = {};
		std::array<VkDescriptorBindingFlags, 3> binding_flags = {};
		std::array<VkDescriptorType, 3> descriptor_types =
		{
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
		};

		for (size_t i = 0; i < 3; ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorType = descriptor_types[i];
			bindings[i].descriptorCount = DEFAULT_DESCRIPTOR_COUNT_PER_TYPE;
			bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
			binding_flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
		}

		VkDescriptorSetLayoutBindingFlagsCreateInfo binding_info = {};
		binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		binding_info.pBindingFlags = binding_flags.data();
		binding_info.bindingCount = 3;

		VkDescriptorSetLayoutCreateInfo layout_info = {};
		layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layout_info.bindingCount = 3;
		layout_info.pBindings = bindings.data();
		layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
		layout_info.pNext = &binding_info;

		VkCheckResult(vkCreateDescriptorSetLayout(vk_inst.device, &layout_info, nullptr, &data.descriptor_set_layout));
	}

	static void CreateDescriptorBuffer()
	{
		VkDeviceSize buffer_size;
		vk_inst.pFunc.get_descriptor_set_layout_size_ext(vk_inst.device, data.descriptor_set_layout, &buffer_size);

		VulkanBackend::CreateBuffer(buffer_size, VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, data.descriptor_buffer, data.descriptor_buffer_memory);
		VkCheckResult(vkMapMemory(vk_inst.device, data.descriptor_buffer_memory, 0, buffer_size, 0, &data.descriptor_buffer_ptr));
	}

	static void CreateCommandBuffers()
	{
		data.command_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.commandPool = vk_inst.cmd_pools.graphics;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = (uint32_t)data.command_buffers.size();

		VkCheckResult(vkAllocateCommandBuffers(vk_inst.device, &alloc_info, data.command_buffers.data()));
	}

	static void CreateSyncObjects()
	{
		data.image_available_semaphores.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		data.render_finished_semaphores.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		data.in_flight_fences.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaphore_info = {};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fence_info = {};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkCheckResult(vkCreateSemaphore(vk_inst.device, &semaphore_info, nullptr, &data.image_available_semaphores[i]));
			VkCheckResult(vkCreateSemaphore(vk_inst.device, &semaphore_info, nullptr, &data.render_finished_semaphores[i]));
			VkCheckResult(vkCreateFence(vk_inst.device, &fence_info, nullptr, &data.in_flight_fences[i]));
		}
	}

	static VkShaderModule CreateShaderModule(const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		create_info.codeSize = code.size();
		create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shader_module;
		VkCheckResult(vkCreateShaderModule(vk_inst.device, &create_info, nullptr, &shader_module));

		return shader_module;
	}

	static void CreateRenderPass()
	{
		VkAttachmentDescription color_attachment = {};
		color_attachment.format = vk_inst.swapchain.format;
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		
		VkAttachmentDescription depth_attachment = {};
		depth_attachment.format = VulkanBackend::FindDepthFormat();
		depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference color_attachment_ref = {};
		color_attachment_ref.attachment = 0;
		color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depth_attachment_ref = {};
		depth_attachment_ref.attachment = 1;
		depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_attachment_ref;
		subpass.pDepthStencilAttachment = &depth_attachment_ref;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		std::array<VkAttachmentDescription, 2> attachments = { color_attachment, depth_attachment };
		VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_info.attachmentCount = (uint32_t)attachments.size();
		render_pass_info.pAttachments = attachments.data();
		render_pass_info.subpassCount = 1;
		render_pass_info.pSubpasses = &subpass;
		render_pass_info.dependencyCount = 1;
		render_pass_info.pDependencies = &dependency;

		VkCheckResult(vkCreateRenderPass(vk_inst.device, &render_pass_info, nullptr, &data.render_pass));
	}

	static void CreateGraphicsPipeline()
	{
		// TODO: Use libshaderc to compile shaders into SPIR-V from code
		// TODO: Vulkan extension for shader objects? No longer need to make compiled pipeline states then
		// https://www.khronos.org/blog/you-can-use-vulkan-without-pipelines-today
		auto vert_shader_code = ReadFile("assets/shaders/bin/VertexShader.spv");
		auto frag_shader_code = ReadFile("assets/shaders/bin/FragmentShader.spv");

		VkShaderModule vert_shader_module = CreateShaderModule(vert_shader_code);
		VkShaderModule frag_shader_module = CreateShaderModule(frag_shader_code);

		VkPipelineShaderStageCreateInfo vert_shader_stage_info = {};
		vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vert_shader_stage_info.module = vert_shader_module;
		vert_shader_stage_info.pName = "main";

		VkPipelineShaderStageCreateInfo frag_shader_stage_info = {};
		frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		frag_shader_stage_info.module = frag_shader_module;
		frag_shader_stage_info.pName = "main";

		VkPipelineShaderStageCreateInfo shader_stages[] = { vert_shader_stage_info, frag_shader_stage_info };

		// TODO: Vertex pulling, we won't need vertex input layouts, or maybe even mesh shaders
		// https://www.khronos.org/blog/mesh-shading-for-vulkan
		auto binding_desc = GetVertexBindingDescription();
		auto attribute_desc = GetVertexAttributeDescription();

		VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
		vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_info.vertexBindingDescriptionCount = 1;
		vertex_input_info.pVertexBindingDescriptions = &binding_desc;
		vertex_input_info.vertexAttributeDescriptionCount = (uint32_t)attribute_desc.size();
		vertex_input_info.pVertexAttributeDescriptions = attribute_desc.data();

		VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {};
		input_assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		input_assembly_info.primitiveRestartEnable = VK_FALSE;

		std::vector<VkDynamicState> dynamic_states =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamic_state = {};
		dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_state.dynamicStateCount = (uint32_t)dynamic_states.size();
		dynamic_state.pDynamicStates = dynamic_states.data();

		VkPipelineViewportStateCreateInfo viewport_state = {};
		viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_state.viewportCount = 1;
		viewport_state.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f;
		rasterizer.depthBiasClamp = 0.0f;
		rasterizer.depthBiasSlopeFactor = 0.0f;

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f;
		multisampling.pSampleMask = nullptr;
		multisampling.alphaToCoverageEnable = VK_FALSE;
		multisampling.alphaToOneEnable = VK_FALSE;

		VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
		depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth_stencil.depthTestEnable = VK_TRUE;
		depth_stencil.depthWriteEnable = VK_TRUE;
		depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depth_stencil.depthBoundsTestEnable = VK_FALSE;
		depth_stencil.minDepthBounds = 0.0f;
		depth_stencil.maxDepthBounds = 1.0f;
		depth_stencil.stencilTestEnable = VK_FALSE;
		depth_stencil.front = {};
		depth_stencil.back = {};

		VkPipelineColorBlendAttachmentState color_blend_attachment = {};
		color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		color_blend_attachment.blendEnable = VK_FALSE;// VK_TRUE;
		color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
		color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo color_blend = {};
		color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend.logicOpEnable = VK_FALSE;
		color_blend.logicOp = VK_LOGIC_OP_COPY;
		color_blend.attachmentCount = 1;
		color_blend.pAttachments = &color_blend_attachment;
		color_blend.blendConstants[0] = 0.0f;
		color_blend.blendConstants[1] = 0.0f;
		color_blend.blendConstants[2] = 0.0f;
		color_blend.blendConstants[3] = 0.0f;

		VkPipelineLayoutCreateInfo pipeline_layout_info = {};
		pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.setLayoutCount = 1;
		pipeline_layout_info.pSetLayouts = &data.descriptor_set_layout;
		pipeline_layout_info.pushConstantRangeCount = 0;
		pipeline_layout_info.pPushConstantRanges = nullptr;

		VkCheckResult(vkCreatePipelineLayout(vk_inst.device, &pipeline_layout_info, nullptr, &data.pipeline_layout));

		VkGraphicsPipelineCreateInfo pipeline_info = {};
		pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.stageCount = 2;
		pipeline_info.pStages = shader_stages;
		pipeline_info.pVertexInputState = &vertex_input_info;
		pipeline_info.pInputAssemblyState = &input_assembly_info;
		pipeline_info.pViewportState = &viewport_state;
		pipeline_info.pRasterizationState = &rasterizer;
		pipeline_info.pMultisampleState = &multisampling;
		pipeline_info.pDepthStencilState = &depth_stencil;
		pipeline_info.pColorBlendState = &color_blend;
		pipeline_info.pDynamicState = &dynamic_state;
		pipeline_info.layout = data.pipeline_layout;
		pipeline_info.renderPass = data.render_pass;
		pipeline_info.subpass = 0;
		pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_info.basePipelineIndex = -1;
		pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

		VkCheckResult(vkCreateGraphicsPipelines(vk_inst.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &data.graphics_pipeline));

		vkDestroyShaderModule(vk_inst.device, frag_shader_module, nullptr);
		vkDestroyShaderModule(vk_inst.device, vert_shader_module, nullptr);
	}

	static void RecordCommandBuffer(VkCommandBuffer command_buffer, uint32_t image_index)
	{
		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = 0;
		begin_info.pInheritanceInfo = nullptr;

		VkCheckResult(vkBeginCommandBuffer(command_buffer, &begin_info));

		VkRenderPassBeginInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass = data.render_pass;
		render_pass_info.framebuffer = vk_inst.swapchain.framebuffers[image_index];
		// NOTE: Define the render area, which defines where shader loads and stores will take place
		// Pixels outside this region will have undefined values
		render_pass_info.renderArea.offset = { 0, 0 };
		render_pass_info.renderArea.extent = vk_inst.swapchain.extent;

		std::array<VkClearValue, 2> clear_values = {};
		clear_values[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clear_values[1].depthStencil = { 1.0f, 0 };

		render_pass_info.clearValueCount = (uint32_t)clear_values.size();
		render_pass_info.pClearValues = clear_values.data();

		vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);

		// Vertex and index buffers
		VkBuffer vertex_buffers[] = { data.vertex_buffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
		vkCmdBindIndexBuffer(command_buffer, data.index_buffer, 0, VK_INDEX_TYPE_UINT32);

		// Viewport and scissor
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)vk_inst.swapchain.extent.width;
		viewport.height = (float)vk_inst.swapchain.extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = vk_inst.swapchain.extent;
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		// Bind descriptor buffers
		VkBufferDeviceAddressInfo buffer_address_info = {};
		buffer_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		buffer_address_info.buffer = data.descriptor_buffer;

		VkDescriptorBufferBindingInfoEXT descriptor_binding_info = {};
		descriptor_binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
		descriptor_binding_info.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
		descriptor_binding_info.address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_address_info);

		vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, 1, &descriptor_binding_info);
		uint32_t buffer_indices = 0;
		VkDeviceSize buffer_offsets = 0;
		vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			data.pipeline_layout, 0, 1, &buffer_indices, &buffer_offsets);

		// Draw call
		vkCmdDrawIndexed(command_buffer, (uint32_t)s_indices.size(), 1, 0, 0, 0);
		vkCmdEndRenderPass(command_buffer);

		VkCheckResult(vkEndCommandBuffer(command_buffer));
	}

	void Init(::GLFWwindow* window)
	{
		data.window = window;
		VulkanBackend::Init(window);

		CreateRenderPass();
		CreateDescriptorSetLayout();
		CreateDescriptorBuffer();
		CreateGraphicsPipeline();

		CreateFramebuffers();
		CreateVertexBuffer();
		CreateIndexBuffer();
		CreateUniformBuffers();
		CreateCommandBuffers();
		CreateSyncObjects();
	}

	void Exit()
	{
		vkDeviceWaitIdle(vk_inst.device);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroyFence(vk_inst.device, data.in_flight_fences[i], nullptr);
			vkDestroySemaphore(vk_inst.device, data.render_finished_semaphores[i], nullptr);
			vkDestroySemaphore(vk_inst.device, data.image_available_semaphores[i], nullptr);
		}
		vkDestroyBuffer(vk_inst.device, data.index_buffer, nullptr);
		vkFreeMemory(vk_inst.device, data.index_buffer_memory, nullptr);
		vkDestroyBuffer(vk_inst.device, data.vertex_buffer, nullptr);
		vkFreeMemory(vk_inst.device, data.vertex_buffer_memory, nullptr);
		// Destroying the command pool will also destroy any command buffers associated with that pool
		vkDestroyRenderPass(vk_inst.device, data.render_pass, nullptr);
		vkDestroyPipeline(vk_inst.device, data.graphics_pipeline, nullptr);
		for (size_t i = 0; i < VulkanInstance::VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkUnmapMemory(vk_inst.device, data.uniform_buffers_memory[i]);
			vkDestroyBuffer(vk_inst.device, data.uniform_buffers[i], nullptr);
			vkFreeMemory(vk_inst.device, data.uniform_buffers_memory[i], nullptr);
		}
		vkUnmapMemory(vk_inst.device, data.descriptor_buffer_memory);
		vkDestroyBuffer(vk_inst.device, data.descriptor_buffer, nullptr);
		vkFreeMemory(vk_inst.device, data.descriptor_buffer_memory, nullptr);
		vkDestroyDescriptorSetLayout(vk_inst.device, data.descriptor_set_layout, nullptr);
		vkDestroyPipelineLayout(vk_inst.device, data.pipeline_layout, nullptr);

		VulkanBackend::Exit();
	}

	void RenderFrame()
	{
		// Wait for completion of all rendering on the GPU
		vkWaitForFences(vk_inst.device, 1, &data.in_flight_fences[data.current_frame], VK_TRUE, UINT64_MAX);

		// Get an available image index from the swap chain
		uint32_t image_index;
		VkResult image_result = vkAcquireNextImageKHR(vk_inst.device, vk_inst.swapchain.swapchain, UINT64_MAX, data.image_available_semaphores[data.current_frame], VK_NULL_HANDLE, &image_index);

		if (image_result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			VulkanBackend::RecreateSwapChain();
			CreateFramebuffers();
			return;
		}
		else if (image_result != VK_SUCCESS && image_result != VK_SUBOPTIMAL_KHR)
		{
			VkCheckResult(image_result);
		}

		// Reset the fence
		vkResetFences(vk_inst.device, 1, &data.in_flight_fences[data.current_frame]);

		// Reset and record the command buffer
		vkResetCommandBuffer(data.command_buffers[data.current_frame], 0);
		RecordCommandBuffer(data.command_buffers[data.current_frame], image_index);

		// Update UBOs
		UpdateUniformBuffer(data.current_frame);

		// Submit the command buffer for execution
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore wait_semaphores[] = { data.image_available_semaphores[data.current_frame] };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = wait_semaphores;
		submit_info.pWaitDstStageMask = wait_stages;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &data.command_buffers[data.current_frame];

		VkSemaphore signal_semaphores[] = { data.render_finished_semaphores[data.current_frame] };
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = signal_semaphores;

		VkCheckResult(vkQueueSubmit(vk_inst.queues.graphics, 1, &submit_info, data.in_flight_fences[data.current_frame]));

		// Present
		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = signal_semaphores;

		VkSwapchainKHR swapchains[] = { vk_inst.swapchain.swapchain };
		present_info.swapchainCount = 1;
		present_info.pSwapchains = swapchains;
		present_info.pImageIndices = &image_index;
		present_info.pResults = nullptr;

		VkResult present_result = vkQueuePresentKHR(vk_inst.queues.graphics, &present_info);

		if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
		{
			VulkanBackend::RecreateSwapChain();
			CreateFramebuffers();
		}
		else
		{
			VkCheckResult(present_result);
		}

		data.current_frame = (data.current_frame + 1) % VulkanInstance::MAX_FRAMES_IN_FLIGHT;
	}

	ResourceHandle_t CreateTexture(const CreateTextureArgs& args)
	{
		// NOTE: BPP are always 4 for now
		uint32_t bpp = 4;
		VkDeviceSize image_size = args.width * args.height * bpp;

		// Create staging buffer
		VkBuffer staging_buffer;
		VkDeviceMemory staging_buffer_memory;
		VulkanBackend::CreateBuffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

		// Copy data into the mapped memory of the staging buffer
		void* mapped_ptr;
		VkCheckResult(vkMapMemory(vk_inst.device, staging_buffer_memory, 0, image_size, 0, &mapped_ptr));
		memcpy(mapped_ptr, args.data_ptr, (size_t)image_size);
		vkUnmapMemory(vk_inst.device, staging_buffer_memory);

		// Create texture image
		Texture texture = {};
		VulkanBackend::CreateImage(args.width, args.height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.image, texture.device_memory);

		// Copy staging buffer data into final texture image memory (device local)
		VulkanBackend::TransitionImageLayout(texture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		CopyBufferToImage(staging_buffer, texture.image, args.width, args.height);
		VulkanBackend::TransitionImageLayout(texture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// Clean up staging buffer (memory)
		vkDestroyBuffer(vk_inst.device, staging_buffer, nullptr);
		vkFreeMemory(vk_inst.device, staging_buffer_memory, nullptr);

		// Create image view
		VulkanBackend::CreateImageView(texture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, texture.image_view);

		// Create image sampler
		VkPhysicalDeviceProperties properties = {};
		vkGetPhysicalDeviceProperties(vk_inst.physical_device, &properties);

		VkSamplerCreateInfo sampler_info = {};
		sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		// Oversampling
		sampler_info.magFilter = VK_FILTER_LINEAR;
		// Undersampling
		sampler_info.minFilter = VK_FILTER_LINEAR;
		sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler_info.anisotropyEnable = VK_TRUE;
		sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
		sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		sampler_info.unnormalizedCoordinates = VK_FALSE;
		sampler_info.compareEnable = VK_FALSE;
		sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_info.mipLodBias = 0.0f;
		sampler_info.minLod = 0.0f;
		sampler_info.maxLod = 0.0f;

		VkCheckResult(vkCreateSampler(vk_inst.device, &sampler_info, nullptr, &texture.sampler));
		ResourceHandle_t handle = data.texture_slotmap.Insert(texture);

		// Update descriptor
		VkDescriptorImageInfo image_info = {};
		image_info.imageView = texture.image_view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_info.sampler = texture.sampler;

		VkDescriptorGetInfoEXT descriptor_info = {};
		descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
		descriptor_info.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptor_info.data.pCombinedImageSampler = &image_info;

		VkDeviceSize binding_offset;
		vk_inst.pFunc.get_descriptor_set_layout_binding_offset_ext(vk_inst.device, data.descriptor_set_layout, TEXTURE_BINDING, &binding_offset);
		
		uint8_t* descriptor_ptr = (uint8_t*)data.descriptor_buffer_ptr + binding_offset +
			vk_inst.descriptor_sizes.combined_image_sampler * data.descriptor_combined_image_samplers_current;
		vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info, vk_inst.descriptor_sizes.combined_image_sampler, descriptor_ptr);
		data.descriptor_combined_image_samplers_current++;

		return handle;
	}

	ResourceHandle_t CreateMesh(const CreateMeshArgs& args)
	{
		return ResourceHandle_t{};
	}

}
