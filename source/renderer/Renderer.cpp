#include "renderer/Renderer.h"
#include "renderer/VulkanBackend.h"
#include "renderer/ResourceSlotmap.h"
#include "Common.h"

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"

#include <array>
#include <optional>
#include <assert.h>
#include <cstring>
#include <set>
#include <algorithm>
#include <exception>
#include <fstream>

namespace Renderer
{

	static constexpr uint32_t DEFAULT_DESCRIPTOR_COUNT_PER_TYPE = 1000;

	static constexpr uint32_t MAX_DRAW_LIST_ENTRIES = 10000;
	static constexpr uint32_t MAX_UNIQUE_MATERIALS = 1000;

	enum Bindings
	{
		Bindings_Uniform,
		Bindings_Storage,
		Bindings_CombinedImageSampler,
		Bindings_NumBindings
	};

	struct TextureResource
	{
		Vulkan::Image image;

		~TextureResource()
		{
			Vulkan::DestroyImage(image);
		}
	};

	struct MeshResource
	{
		Vulkan::Buffer vertex_buffer;
		Vulkan::Buffer index_buffer;

		~MeshResource()
		{
			Vulkan::DestroyBuffer(vertex_buffer);
			Vulkan::DestroyBuffer(index_buffer);
		}
	};

	struct MaterialResource
	{
		TextureHandle_t base_color_tex_handle;

		struct alignas(16) MaterialData
		{
			glm::vec4 base_color_factor = glm::vec4(1.0f);
			uint32_t base_color_tex_index = 0;
		} data;
	};

	struct DrawList
	{
		struct Entry
		{
			MeshHandle_t mesh_handle;
			MaterialHandle_t material_handle;
			glm::mat4 transform;
		};

		size_t current_entry = 0;
		std::array<Entry, MAX_DRAW_LIST_ENTRIES> entries;

		Entry& GetNextEntry()
		{
			Entry& entry = entries[current_entry];
			current_entry++;
			return entry;
		}
	};

	struct CameraData
	{
		glm::mat4 view;
		glm::mat4 proj;
	};

	enum ReservedDescriptorStorage
	{
		ReservedDescriptorStorage_Material,
		ReservedDescriptorStorage_NumDescriptors
	};

	struct Data
	{
		~Data() {}

		::GLFWwindow* window = nullptr;

		ResourceSlotmap<TextureResource> texture_slotmap;
		ResourceSlotmap<MeshResource> mesh_slotmap;
		ResourceSlotmap<MaterialResource> material_slotmap;

		VkRenderPass render_pass = VK_NULL_HANDLE;
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		VkPipeline graphics_pipeline = VK_NULL_HANDLE;

		VkDescriptorSetLayout reserved_descriptor_set_layout = VK_NULL_HANDLE;
		VkDescriptorSetLayout bindless_descriptor_set_layout = VK_NULL_HANDLE;
		Vulkan::Buffer reserved_descriptor_buffer;
		Vulkan::Buffer bindless_descriptor_buffer;
		uint32_t bindless_descriptors_uniform_buffers_current = 0;
		uint32_t bindless_descriptors_storage_buffers_current = 0;
		uint32_t bindless_descriptors_combined_image_samplers_current = 0;

		std::vector<VkCommandBuffer> command_buffers;
		std::vector<VkSemaphore> image_available_semaphores;
		std::vector<VkSemaphore> render_finished_semaphores;
		std::vector<VkFence> in_flight_fences;

		DrawList draw_list;

		std::vector<Vulkan::Buffer> camera_uniform_buffers;
		std::vector<Vulkan::Buffer> instance_buffers;
		Vulkan::Buffer material_buffer;

		TextureHandle_t default_white_texture_handle;
		TextureResource* default_white_texture_resource;

		uint32_t current_frame = 0;

		struct ImGui
		{
			VkDescriptorPool descriptor_pool;
			VkRenderPass render_pass;
		} imgui;
	} static data;

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
	
	static std::array<VkVertexInputBindingDescription, 2> GetVertexBindingDescription()
	{
		std::array<VkVertexInputBindingDescription, 2> binding_descs = {};
		binding_descs[0].binding = 0;
		binding_descs[0].stride = sizeof(Vertex);
		binding_descs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		binding_descs[1].binding = 1;
		binding_descs[1].stride = sizeof(glm::mat4);
		binding_descs[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		return binding_descs;
	}

	static std::array<VkVertexInputAttributeDescription, 6> GetVertexAttributeDescription()
	{
		std::array<VkVertexInputAttributeDescription, 6> attribute_desc = {};
		attribute_desc[0].binding = 0;
		attribute_desc[0].location = 0;
		attribute_desc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute_desc[0].offset = offsetof(Vertex, pos);

		attribute_desc[1].binding = 0;
		attribute_desc[1].location = 1;
		attribute_desc[1].format = VK_FORMAT_R32G32_SFLOAT;
		attribute_desc[1].offset = offsetof(Vertex, tex_coord);

		attribute_desc[2].binding = 1;
		attribute_desc[2].location = 2;
		attribute_desc[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[2].offset = 0;

		attribute_desc[3].binding = 1;
		attribute_desc[3].location = 3;
		attribute_desc[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[3].offset = 16;

		attribute_desc[4].binding = 1;
		attribute_desc[4].location = 4;
		attribute_desc[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[4].offset = 32;

		attribute_desc[5].binding = 1;
		attribute_desc[5].location = 5;
		attribute_desc[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[5].offset = 48;

		return attribute_desc;
	}

	static void CreateFramebuffers()
	{
		vk_inst.swapchain.framebuffers.resize(vk_inst.swapchain.images.size());

		for (size_t i = 0; i < vk_inst.swapchain.images.size(); ++i)
		{
			std::array<VkImageView, 2> attachments
			{
				vk_inst.swapchain.images[i].view,
				vk_inst.swapchain.depth_image.view
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

	static void CreateDescriptorSetLayouts()
	{
		// Fixed descriptor set layout for reserved rendering resources that are always present
		// e.g. the global material buffer
		{
			VkDescriptorSetLayoutBinding bindings = {};
			bindings.binding = 0;
			bindings.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings.descriptorCount = ReservedDescriptorStorage_NumDescriptors;
			bindings.stageFlags = VK_SHADER_STAGE_ALL;

			VkDescriptorBindingFlags binding_flags = {};

			VkDescriptorSetLayoutBindingFlagsCreateInfo binding_info = {};
			binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
			binding_info.pBindingFlags = &binding_flags;
			binding_info.bindingCount = 1;

			VkDescriptorSetLayoutCreateInfo layout_info = {};
			layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layout_info.bindingCount = 1;
			layout_info.pBindings = &bindings;
			layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
			layout_info.pNext = &binding_info;

			VkCheckResult(vkCreateDescriptorSetLayout(vk_inst.device, &layout_info, nullptr, &data.reserved_descriptor_set_layout));
		}

		// Bindless descriptor set layout
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

			VkCheckResult(vkCreateDescriptorSetLayout(vk_inst.device, &layout_info, nullptr, &data.bindless_descriptor_set_layout));
		}
	}

	static void CreateDescriptorBuffers()
	{
		// Reserved descriptor buffer
		{
			VkDeviceSize buffer_size;
			vk_inst.pFunc.get_descriptor_set_layout_size_ext(vk_inst.device, data.reserved_descriptor_set_layout, &buffer_size);
			Vulkan::CreateDescriptorBuffer(buffer_size, data.reserved_descriptor_buffer);
		}

		// Bindless descriptor buffer
		{
			VkDeviceSize buffer_size;
			vk_inst.pFunc.get_descriptor_set_layout_size_ext(vk_inst.device, data.bindless_descriptor_set_layout, &buffer_size);
			Vulkan::CreateDescriptorBuffer(buffer_size, data.bindless_descriptor_buffer);
		}
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

	static void CreateDefaultTextures()
	{
		// Default white texture
		std::vector<uint8_t> pixel_data = { 0xFF, 0xFF, 0xFF, 0xFF };

		CreateTextureArgs texture_args = {};
		texture_args.width = 1;
		texture_args.height = 1;
		texture_args.pixels = pixel_data;

		data.default_white_texture_handle = CreateTexture(texture_args);
		data.default_white_texture_resource = data.texture_slotmap.Find(data.default_white_texture_handle);
	}

	static void CreateUniformBuffers()
	{
		data.camera_uniform_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize buffer_size = sizeof(CameraData);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			Vulkan::CreateUniformBuffer(buffer_size, data.camera_uniform_buffers[i]);

			// Update descriptor
			VkBufferDeviceAddressInfoEXT buffer_address_info = {};
			buffer_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			buffer_address_info.buffer = data.camera_uniform_buffers[i].buffer;

			VkDescriptorAddressInfoEXT descriptor_address_info = {};
			descriptor_address_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
			descriptor_address_info.address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_address_info);
			descriptor_address_info.format = VK_FORMAT_UNDEFINED;
			descriptor_address_info.range = sizeof(CameraData);

			VkDescriptorGetInfoEXT descriptor_info = {};
			descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
			descriptor_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptor_info.data.pUniformBuffer = &descriptor_address_info;

			VkDeviceSize binding_offset;
			vk_inst.pFunc.get_descriptor_set_layout_binding_offset_ext(vk_inst.device, data.bindless_descriptor_set_layout, Bindings_Uniform, &binding_offset);

			uint8_t* descriptor_ptr = (uint8_t*)data.bindless_descriptor_buffer.ptr + binding_offset +
				vk_inst.descriptor_sizes.uniform_buffer * data.bindless_descriptors_uniform_buffers_current;
			vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info, vk_inst.descriptor_sizes.uniform_buffer, descriptor_ptr);
			data.bindless_descriptors_uniform_buffers_current++;
		}
	}

	static void CreateInstanceBuffers()
	{
		data.instance_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize instance_buffer_size = MAX_DRAW_LIST_ENTRIES * sizeof(glm::mat4);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			Vulkan::Buffer& instance_buffer = data.instance_buffers[i];

			Vulkan::CreateBuffer(instance_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, instance_buffer);
			VkCheckResult(vkMapMemory(vk_inst.device, instance_buffer.memory, 0, instance_buffer_size, 0, &instance_buffer.ptr));
		}
	}

	static void CreateMaterialBuffer()
	{
		VkDeviceSize material_buffer_size = MAX_UNIQUE_MATERIALS * sizeof(MaterialResource);
		Vulkan::CreateBuffer(material_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, data.material_buffer);

		// Update descriptor
		VkBufferDeviceAddressInfoEXT buffer_address_info = {};
		buffer_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		buffer_address_info.buffer = data.material_buffer.buffer;

		VkDescriptorAddressInfoEXT descriptor_address_info = {};
		descriptor_address_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
		descriptor_address_info.address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_address_info);
		descriptor_address_info.format = VK_FORMAT_UNDEFINED;
		descriptor_address_info.range = MAX_UNIQUE_MATERIALS * sizeof(MaterialResource);

		VkDescriptorGetInfoEXT descriptor_info = {};
		descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
		descriptor_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptor_info.data.pStorageBuffer = &descriptor_address_info;

		VkDeviceSize binding_offset;
		vk_inst.pFunc.get_descriptor_set_layout_binding_offset_ext(vk_inst.device, data.reserved_descriptor_set_layout, 0, &binding_offset);

		uint8_t* descriptor_ptr = (uint8_t*)data.reserved_descriptor_buffer.ptr + binding_offset + ReservedDescriptorStorage_Material * vk_inst.descriptor_sizes.storage_buffer;
		vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info, vk_inst.descriptor_sizes.storage_buffer, descriptor_ptr);
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
		color_attachment.format = vk_inst.swapchain.images[0].format;
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		
		VkAttachmentDescription depth_attachment = {};
		depth_attachment.format = Vulkan::FindDepthFormat();
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
		vertex_input_info.vertexBindingDescriptionCount = (uint32_t)binding_desc.size();
		vertex_input_info.pVertexBindingDescriptions = binding_desc.data();
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

		std::array<VkPushConstantRange, 2> push_constants = {};
		push_constants[0].offset = 0;
		push_constants[0].size = sizeof(uint32_t);
		push_constants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		push_constants[1].offset = VK_ALIGN_POW2(4 + alignof(uint32_t), 16);
		push_constants[1].size = sizeof(uint32_t);
		push_constants[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayout, 2> descriptor_set_layouts = { data.reserved_descriptor_set_layout, data.bindless_descriptor_set_layout };
		VkPipelineLayoutCreateInfo pipeline_layout_info = {};
		pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.setLayoutCount = (uint32_t)descriptor_set_layouts.size();
		pipeline_layout_info.pSetLayouts = descriptor_set_layouts.data();
		pipeline_layout_info.pushConstantRangeCount = (uint32_t)push_constants.size();
		pipeline_layout_info.pPushConstantRanges = push_constants.data();

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

		// Push constants
		uint32_t camera_ubo_index = data.current_frame;
		vkCmdPushConstants(command_buffer, data.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &camera_ubo_index);

		// Bind descriptor buffers
		std::array<VkBufferDeviceAddressInfo, 2> buffer_address_infos = {};
		buffer_address_infos[0].sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		buffer_address_infos[0].buffer = data.reserved_descriptor_buffer.buffer;

		buffer_address_infos[1].sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		buffer_address_infos[1].buffer = data.bindless_descriptor_buffer.buffer;

		std::array<VkDescriptorBufferBindingInfoEXT, 2> descriptor_buffer_binding_infos = {};
		descriptor_buffer_binding_infos[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
		descriptor_buffer_binding_infos[0].usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
		descriptor_buffer_binding_infos[0].address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_address_infos[0]);

		descriptor_buffer_binding_infos[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
		descriptor_buffer_binding_infos[1].usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
		descriptor_buffer_binding_infos[1].address = vkGetBufferDeviceAddress(vk_inst.device, &buffer_address_infos[1]);

		vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_infos.size(), descriptor_buffer_binding_infos.data());
		uint32_t buffer_indices[2] = { 0, 1 };
		VkDeviceSize buffer_offsets[2] = { 0, 0 };
		vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			data.pipeline_layout, 0, 2, &buffer_indices[0], &buffer_offsets[0]);

		// Instance buffer
		const Vulkan::Buffer& instance_buffer = data.instance_buffers[data.current_frame];

		for (size_t i = 0; i < data.draw_list.current_entry; ++i)
		{
			const DrawList::Entry& entry = data.draw_list.entries[i];

			// TODO: Default mesh
			MeshResource* mesh = nullptr;
			if (VK_RESOURCE_HANDLE_VALID(entry.mesh_handle))
			{
				mesh = data.mesh_slotmap.Find(entry.mesh_handle);
			}
			VK_ASSERT(mesh && "Tried to render a mesh with an invalid mesh handle");
			
			// Check material handles for validity
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(entry.material_handle));
			MaterialResource* material = data.material_slotmap.Find(entry.material_handle);
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(material->base_color_tex_handle));

			vkCmdPushConstants(command_buffer, data.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 16, sizeof(uint32_t), &entry.material_handle.index);

			// Vertex and index buffers
			VkBuffer vertex_buffers[] = { mesh->vertex_buffer.buffer, instance_buffer.buffer };
			VkDeviceSize offsets[] = { 0, i * sizeof(glm::mat4) };
			vkCmdBindVertexBuffers(command_buffer, 0, 2, vertex_buffers, offsets);
			vkCmdBindIndexBuffer(command_buffer, mesh->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			// Draw call
			vkCmdDrawIndexed(command_buffer, mesh->index_buffer.num_elements, 1, 0, 0, 0);
		}

		vkCmdEndRenderPass(command_buffer);

		// Render ImGui
		VkRenderPassBeginInfo imgui_pass_info = {};
		imgui_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		imgui_pass_info.renderPass = data.imgui.render_pass;
		imgui_pass_info.framebuffer = vk_inst.swapchain.framebuffers[image_index];
		imgui_pass_info.renderArea.offset = { 0, 0 };
		imgui_pass_info.renderArea.extent = vk_inst.swapchain.extent;

		vkCmdBeginRenderPass(command_buffer, &imgui_pass_info, VK_SUBPASS_CONTENTS_INLINE);

		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer, nullptr);

		vkCmdEndRenderPass(command_buffer);

		VkCheckResult(vkEndCommandBuffer(command_buffer));
	}

	static void InitDearImGui()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

		// Create imgui descriptor pool
		VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1;
		pool_info.poolSizeCount = 1;
		pool_info.pPoolSizes = &pool_size;
		VkCheckResult(vkCreateDescriptorPool(vk_inst.device, &pool_info, nullptr, &data.imgui.descriptor_pool));

		// Create imgui render pass
		VkAttachmentDescription color_attachment = {};
		color_attachment.format = vk_inst.swapchain.images[0].format;
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription depth_attachment = {};
		depth_attachment.format = Vulkan::FindDepthFormat();
		depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstSubpass = 0;
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

		VkCheckResult(vkCreateRenderPass(vk_inst.device, &render_pass_info, nullptr, &data.imgui.render_pass));

		// Init imgui
		ImGui_ImplGlfw_InitForVulkan(vk_inst.window, true);
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = vk_inst.instance;
		init_info.PhysicalDevice = vk_inst.physical_device;
		init_info.Device = vk_inst.device;
		init_info.QueueFamily = vk_inst.queue_indices.graphics;
		init_info.Queue = vk_inst.queues.graphics;
		init_info.PipelineCache = VK_NULL_HANDLE;
		init_info.DescriptorPool = data.imgui.descriptor_pool;
		init_info.Subpass = 0;
		init_info.MinImageCount = VulkanInstance::MAX_FRAMES_IN_FLIGHT;
		init_info.ImageCount = VulkanInstance::MAX_FRAMES_IN_FLIGHT;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.Allocator = nullptr;
		init_info.CheckVkResultFn = VkCheckResult;
		ImGui_ImplVulkan_Init(&init_info, data.imgui.render_pass);

		// Upload imgui font
		VkCommandBuffer command_buffer = Vulkan::BeginSingleTimeCommands();
		ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
		Vulkan::EndSingleTimeCommands(command_buffer);
		ImGui_ImplVulkan_DestroyFontUploadObjects();
	}

	static void ExitDearImGui()
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		vkDestroyRenderPass(vk_inst.device, data.imgui.render_pass, nullptr);
		vkDestroyDescriptorPool(vk_inst.device, data.imgui.descriptor_pool, nullptr);
	}

	void Init(::GLFWwindow* window)
	{
		data.window = window;
		Vulkan::Init(window);

		CreateRenderPass();
		CreateDescriptorSetLayouts();
		CreateDescriptorBuffers();
		CreateGraphicsPipeline();

		CreateFramebuffers();
		CreateCommandBuffers();
		CreateSyncObjects();
		CreateDefaultTextures();
		CreateUniformBuffers();
		CreateInstanceBuffers();
		CreateMaterialBuffer();

		InitDearImGui();
	}

	void Exit()
	{
		vkDeviceWaitIdle(vk_inst.device);

		ExitDearImGui();

		// Clean up vulkan stuff
		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroyFence(vk_inst.device, data.in_flight_fences[i], nullptr);
			vkDestroySemaphore(vk_inst.device, data.render_finished_semaphores[i], nullptr);
			vkDestroySemaphore(vk_inst.device, data.image_available_semaphores[i], nullptr);
		}

		// Destroying the command pool will also destroy any command buffers associated with that pool
		vkDestroyRenderPass(vk_inst.device, data.render_pass, nullptr);
		vkDestroyPipeline(vk_inst.device, data.graphics_pipeline, nullptr);
		for (size_t i = 0; i < VulkanInstance::VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			Vulkan::DestroyBuffer(data.camera_uniform_buffers[i]);
			Vulkan::DestroyBuffer(data.instance_buffers[i]);
		}
		Vulkan::DestroyBuffer(data.material_buffer);
		Vulkan::DestroyBuffer(data.reserved_descriptor_buffer);
		Vulkan::DestroyBuffer(data.bindless_descriptor_buffer);
		vkDestroyDescriptorSetLayout(vk_inst.device, data.reserved_descriptor_set_layout, nullptr);
		vkDestroyDescriptorSetLayout(vk_inst.device, data.bindless_descriptor_set_layout, nullptr);
		vkDestroyPipelineLayout(vk_inst.device, data.pipeline_layout, nullptr);

		// Clean up the renderer data
		data.~Data();

		// Finally, exit the vulkan render backend
		Vulkan::Exit();
	}

	void BeginFrame(const glm::mat4& view, const glm::mat4& proj)
	{
		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplVulkan_NewFrame();
		ImGui::NewFrame();

		CameraData ubo = {};
		ubo.view = view;
		ubo.proj = proj;

		memcpy(data.camera_uniform_buffers[data.current_frame].ptr, &ubo, sizeof(ubo));
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
			Vulkan::RecreateSwapChain();
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
			Vulkan::RecreateSwapChain();
			CreateFramebuffers();
		}
		else
		{
			VkCheckResult(present_result);
		}

		data.current_frame = (data.current_frame + 1) % VulkanInstance::MAX_FRAMES_IN_FLIGHT;
	}

	void EndFrame()
	{
		ImGui::EndFrame();
		data.draw_list.current_entry = 0;
	}

	TextureHandle_t CreateTexture(const CreateTextureArgs& args)
	{
		// NOTE: BPP are always 4 for now
		uint32_t bpp = 4;
		VkDeviceSize image_size = args.width * args.height * bpp;

		// Create staging buffer
		Vulkan::Buffer staging_buffer;
		Vulkan::CreateStagingBuffer(image_size, staging_buffer);

		// Copy data into the mapped memory of the staging buffer
		Vulkan::WriteBuffer(staging_buffer.ptr, (void*)args.pixels.data(), image_size);

		// Create texture image
		ResourceSlotmap<TextureResource>::ReservedResource reserved = data.texture_slotmap.Reserve();
		Vulkan::CreateImage(args.width, args.height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reserved.resource->image);

		// Copy staging buffer data into final texture image memory (device local)
		Vulkan::TransitionImageLayout(reserved.resource->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		Vulkan::CopyBufferToImage(staging_buffer, reserved.resource->image, args.width, args.height);
		Vulkan::TransitionImageLayout(reserved.resource->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// Clean up staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		// Create image view
		Vulkan::CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT, reserved.resource->image);

		// Create image sampler
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
		sampler_info.maxAnisotropy = vk_inst.device_props.max_anisotropy;
		sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		sampler_info.unnormalizedCoordinates = VK_FALSE;
		sampler_info.compareEnable = VK_FALSE;
		sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_info.mipLodBias = 0.0f;
		sampler_info.minLod = 0.0f;
		sampler_info.maxLod = 0.0f;

		VkCheckResult(vkCreateSampler(vk_inst.device, &sampler_info, nullptr, &reserved.resource->image.sampler));

		// Update descriptor
		VkDescriptorImageInfo image_info = {};
		image_info.imageView = reserved.resource->image.view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_info.sampler = reserved.resource->image.sampler;

		VkDescriptorGetInfoEXT descriptor_info = {};
		descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
		descriptor_info.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptor_info.data.pCombinedImageSampler = &image_info;

		VkDeviceSize binding_offset;
		vk_inst.pFunc.get_descriptor_set_layout_binding_offset_ext(vk_inst.device, data.bindless_descriptor_set_layout, Bindings_CombinedImageSampler, &binding_offset);
		
		uint8_t* descriptor_ptr = (uint8_t*)data.bindless_descriptor_buffer.ptr + binding_offset +
			vk_inst.descriptor_sizes.combined_image_sampler * data.bindless_descriptors_combined_image_samplers_current;
		vk_inst.pFunc.get_descriptor_ext(vk_inst.device, &descriptor_info, vk_inst.descriptor_sizes.combined_image_sampler, descriptor_ptr);
		data.bindless_descriptors_combined_image_samplers_current++;

		return reserved.handle;
	}

	void DestroyTexture(TextureHandle_t handle)
	{
		data.texture_slotmap.Delete(handle);
	}

	MeshHandle_t CreateMesh(const CreateMeshArgs& args)
	{
		VkDeviceSize vertex_buffer_size = sizeof(Vertex) * args.vertices.size();
		VkDeviceSize index_buffer_size = sizeof(uint32_t) * args.indices.size();

		// Create staging buffer
		Vulkan::Buffer staging_buffer;
		Vulkan::CreateStagingBuffer(vertex_buffer_size + index_buffer_size, staging_buffer);

		// Write data into staging buffer
		Vulkan::WriteBuffer(staging_buffer.ptr, (void*)args.vertices.data(), vertex_buffer_size);
		Vulkan::WriteBuffer((uint8_t*)staging_buffer.ptr + vertex_buffer_size, (void*)args.indices.data(), index_buffer_size);

		// Reserve mesh resource
		ResourceSlotmap<MeshResource>::ReservedResource reserved = data.mesh_slotmap.Reserve();

		// Create vertex and index buffers
		Vulkan::CreateVertexBuffer(vertex_buffer_size, reserved.resource->vertex_buffer);
		Vulkan::CreateIndexBuffer(index_buffer_size, reserved.resource->index_buffer);

		// Copy staging buffer data into vertex and index buffers
		Vulkan::CopyBuffer(staging_buffer, reserved.resource->vertex_buffer, vertex_buffer_size);
		Vulkan::CopyBuffer(staging_buffer, reserved.resource->index_buffer, index_buffer_size, vertex_buffer_size);
		
		reserved.resource->vertex_buffer.num_elements = (uint32_t)args.vertices.size();
		reserved.resource->index_buffer.num_elements = (uint32_t)args.indices.size();

		// Destroy staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		return reserved.handle;
	}

	void DestroyMesh(MeshHandle_t handle)
	{
		data.mesh_slotmap.Delete(handle);
	}

	MaterialHandle_t CreateMaterial(const CreateMaterialArgs& args)
	{
		ResourceSlotmap<MaterialResource>::ReservedResource reserved = data.material_slotmap.Reserve();
		reserved.resource->data.base_color_factor = args.base_color_factor;
		if (VK_RESOURCE_HANDLE_VALID(args.base_color_handle))
		{
			reserved.resource->base_color_tex_handle = args.base_color_handle;
			reserved.resource->data.base_color_tex_index = args.base_color_handle.index;
		}
		else
		{
			reserved.resource->base_color_tex_handle = data.default_white_texture_handle;
			reserved.resource->data.base_color_tex_index = data.default_white_texture_handle.index;
		}

		VkDeviceSize material_size = sizeof(MaterialResource::MaterialData);

		// TODO: Suballocate from big upload buffer, having a staging buffer for every little upload is unnecessary
		// Create staging buffer
		Vulkan::Buffer staging_buffer;
		Vulkan::CreateStagingBuffer(material_size, staging_buffer);

		// Write data into staging buffer
		Vulkan::WriteBuffer(staging_buffer.ptr, (void*)&reserved.resource->data, material_size);

		// Copy staging buffer material resource into device local material buffer
		Vulkan::CopyBuffer(staging_buffer, data.material_buffer, material_size, 0, reserved.handle.index * material_size);

		// Destroy staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		return reserved.handle;
	}

	void DestroyMaterial(MaterialHandle_t handle)
	{
		data.material_slotmap.Delete(handle);
		// NOTE: We could update the material buffer entry here to be 0 or some invalid value,
		// but it will be overwritten anyways when the slot is re-used
	}

	void SubmitMesh(MeshHandle_t mesh_handle, MaterialHandle_t material_handle, const glm::mat4& transform)
	{
		Vulkan::Buffer& instance_buffer = data.instance_buffers[data.current_frame];
		memcpy((glm::mat4*)instance_buffer.ptr + data.draw_list.current_entry, &transform, sizeof(glm::mat4));

		DrawList::Entry& entry = data.draw_list.GetNextEntry();
		entry.mesh_handle = mesh_handle;
		entry.material_handle = material_handle;
		entry.transform = transform;
	}

}
