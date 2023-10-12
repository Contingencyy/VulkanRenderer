#include "renderer/Renderer.h"
#include "renderer/VulkanBackend.h"
#include "renderer/DescriptorBuffer.h"
#include "renderer/ResourceSlotmap.h"
#include "Common.h"
#include "Shared.glsl.h"

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"
#include "shaderc/shaderc.hpp"

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
#include <numeric>
#include <string>

namespace Renderer
{

	static constexpr uint32_t MAX_DRAW_LIST_ENTRIES = 10000;

	//struct BufferResource
	//{
	//	Vulkan::Buffer buffer;
	//	DescriptorAllocation descriptors;

	//	~BufferResource()
	//	{
	//		Vulkan::DestroyBuffer(buffer);
	//		// TODO: Free descriptors
	//	}
	//};

	struct TextureResource
	{
		Vulkan::Image image;
		DescriptorAllocation descriptor;

		~TextureResource()
		{
			Vulkan::DestroyImage(image);
			// TODO: Free descriptors
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
		TextureHandle_t base_color_texture_handle;
		TextureHandle_t normal_texture_handle;
		TextureHandle_t metallic_roughness_texture_handle;

		MaterialData data;
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
			VK_ASSERT(current_entry < MAX_DRAW_LIST_ENTRIES &&
				"Exceeded the maximum amount of draw list entries");

			Entry& entry = entries[current_entry];
			current_entry++;
			return entry;
		}

		void Reset()
		{
			current_entry = 0;
		}
	};

	struct GraphicsPass
	{
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;

		std::vector<Vulkan::Image> color_attachments;
		std::vector<VkRenderingAttachmentInfo> color_attachment_infos;
		Vulkan::Image depth_attachment;
		VkRenderingAttachmentInfo depth_attachment_info;

		VkRenderingFlags flags;
		bool depth_enabled = false;

		~GraphicsPass()
		{
			DestroyAttachments();
			vkDestroyPipeline(vk_inst.device, pipeline, nullptr);
			vkDestroyPipelineLayout(vk_inst.device, pipeline_layout, nullptr);
		}

		void DestroyAttachments()
		{
			for (auto& color_attachment : color_attachments)
			{
				Vulkan::DestroyImage(color_attachment);
			}
			color_attachments.clear();

			if (depth_enabled)
			{
				Vulkan::DestroyImage(depth_attachment);
			}
		}

		VkRenderingInfo GetRenderingInfo()
		{
			for (size_t i = 0; i < color_attachments.size(); ++i)
			{
				color_attachment_infos[i].imageLayout = color_attachments[i].layout;
			}

			VkRenderingInfo result = {};
			result.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			result.colorAttachmentCount = (uint32_t)color_attachment_infos.size();
			result.pColorAttachments = color_attachment_infos.data();
			if (depth_enabled)
			{
				depth_attachment_info.imageLayout = depth_attachment.layout;
				result.pDepthAttachment = &depth_attachment_info;
			}
			result.viewMask = 0;
			result.renderArea = { 0, 0, vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height };
			result.layerCount = 1;
			result.flags = 0;

			return result;
		}

		std::vector<VkFormat> GetColorAttachmentFormats()
		{
			std::vector<VkFormat> result;
			result.reserve(color_attachments.size());
			
			for (auto& color_attachment : color_attachments)
			{
				result.push_back(color_attachment.format);
			}

			return result;
		}
	};

	struct ComputePass
	{
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;

		std::vector<Vulkan::Image> attachments;

		~ComputePass()
		{
			DestroyAttachments();
			vkDestroyPipeline(vk_inst.device, pipeline, nullptr);
			vkDestroyPipelineLayout(vk_inst.device, pipeline_layout, nullptr);
		}

		void DestroyAttachments()
		{
			for (auto& attachment : attachments)
			{
				Vulkan::DestroyImage(attachment);
			}
			attachments.clear();
		}
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

	class ShadercIncluder : public shaderc::CompileOptions::IncluderInterface
	{
	public:
		virtual shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type,
			const char* requesting_source, size_t include_depth) override
		{
			shaderc_include_result* result = new shaderc_include_result();
			result->source_name = requested_source;
			result->source_name_length = strlen(requested_source);

			std::string requested_source_filepath = MakeRequestedFilepathFromRequestingSource(requesting_source, requested_source);
			std::vector<char> requested_source_text = ReadFile(requested_source_filepath.c_str());

			result->content = new char[requested_source_text.size()];
			memcpy((void*)result->content, requested_source_text.data(), requested_source_text.size());
			result->content_length = requested_source_text.size();

			return result;
		}

		// Handles shaderc_include_result_release_fn callbacks.
		virtual void ReleaseInclude(shaderc_include_result* data) override
		{
			delete data->content;
			delete data;
		}

	private:
		std::string MakeRequestedFilepathFromRequestingSource(const char* requesting_source, const char* requested_source)
		{
			std::string filepath = std::string(requesting_source).substr(0, std::string(requesting_source).find_last_of("\\/") + 1);
			std::string requested_source_filepath = filepath + requested_source;
			return requested_source_filepath;
		}

	};

	struct Data
	{
		::GLFWwindow* window = nullptr;

		// Resource slotmaps
		ResourceSlotmap<TextureResource> texture_slotmap;
		ResourceSlotmap<MeshResource> mesh_slotmap;
		ResourceSlotmap<MaterialResource> material_slotmap;

		// Command buffers and synchronization primitives
		std::vector<VkCommandBuffer> command_buffers;
		std::vector<VkSemaphore> render_finished_semaphores;
		std::vector<VkFence> in_flight_fences;

		// Descriptor buffers
		DescriptorBuffer descriptor_buffer_uniform{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)vk_inst.descriptor_sizes.uniform_buffer };
		DescriptorBuffer descriptor_buffer_storage{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)vk_inst.descriptor_sizes.storage_buffer };
		DescriptorBuffer descriptor_buffer_storage_image{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (uint32_t)vk_inst.descriptor_sizes.storage_image };
		DescriptorBuffer descriptor_buffer_sampled_image{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (uint32_t)vk_inst.descriptor_sizes.sampled_image };
		DescriptorBuffer descriptor_buffer_sampler{ VK_DESCRIPTOR_TYPE_SAMPLER, (uint32_t)vk_inst.descriptor_sizes.sampler };

		// Render passes
		GraphicsPass lighting_pass;
		ComputePass post_process_pass;

		DescriptorAllocation reserved_storage_image_descriptors;

		// Draw submission list
		DrawList draw_list;

		// Uniform buffers
		// TODO: Free reserved descriptors on Exit()
		DescriptorAllocation reserved_ubo_descriptors;
		std::vector<Vulkan::Buffer> camera_uniform_buffers;
		std::vector<Vulkan::Buffer> instance_buffers;

		// Storage buffers
		// TODO: Free reserved descriptors on Exit()
		DescriptorAllocation reserved_storage_descriptors;
		Vulkan::Buffer material_buffer;

		// Default resources
		TextureHandle_t default_white_texture_handle;
		TextureHandle_t default_normal_texture_handle;
		VkSampler default_sampler = VK_NULL_HANDLE;
		// TODO: Free reserved descriptors on Exit()
		DescriptorAllocation reserved_sampler_descriptors;

		shaderc::Compiler shader_compiler;
		ShadercIncluder shader_includer;

		struct Statistics
		{
			uint32_t total_vertex_count = 0;
			uint32_t total_triangle_count = 0;

			void Reset()
			{
				total_vertex_count = 0;
				total_triangle_count = 0;
			}
		} stats;

		struct ImGui
		{
			VkDescriptorPool descriptor_pool;
			GraphicsPass pass;
		} imgui;
	} static *data;
	
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

	static std::array<VkVertexInputAttributeDescription, 7> GetVertexAttributeDescription()
	{
		std::array<VkVertexInputAttributeDescription, 7> attribute_desc = {};
		attribute_desc[0].binding = 0;
		attribute_desc[0].location = 0;
		attribute_desc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute_desc[0].offset = offsetof(Vertex, pos);

		attribute_desc[1].binding = 0;
		attribute_desc[1].location = 1;
		attribute_desc[1].format = VK_FORMAT_R32G32_SFLOAT;
		attribute_desc[1].offset = offsetof(Vertex, tex_coord);

		attribute_desc[2].binding = 0;
		attribute_desc[2].location = 2;
		attribute_desc[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute_desc[2].offset = offsetof(Vertex, normal);

		attribute_desc[3].binding = 1;
		attribute_desc[3].location = 3;
		attribute_desc[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[3].offset = 0;

		attribute_desc[4].binding = 1;
		attribute_desc[4].location = 4;
		attribute_desc[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[4].offset = 16;

		attribute_desc[5].binding = 1;
		attribute_desc[5].location = 5;
		attribute_desc[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[5].offset = 32;

		attribute_desc[6].binding = 1;
		attribute_desc[6].location = 6;
		attribute_desc[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attribute_desc[6].offset = 48;

		return attribute_desc;
	}

	static void CreateCommandBuffers()
	{
		data->command_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.commandPool = vk_inst.cmd_pools.graphics;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = (uint32_t)data->command_buffers.size();

		VkCheckResult(vkAllocateCommandBuffers(vk_inst.device, &alloc_info, data->command_buffers.data()));
	}

	static void CreateSyncObjects()
	{
		data->render_finished_semaphores.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		data->in_flight_fences.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaphore_info = {};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fence_info = {};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkCheckResult(vkCreateSemaphore(vk_inst.device, &semaphore_info, nullptr, &data->render_finished_semaphores[i]));
			VkCheckResult(vkCreateFence(vk_inst.device, &fence_info, nullptr, &data->in_flight_fences[i]));
		}
	}

	static void CreateDefaultSamplers()
	{
		// Create sampler
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
		//sampler_info.maxLod = (float)num_mips;
		sampler_info.maxLod = std::numeric_limits<float>::max();

		VkCheckResult(vkCreateSampler(vk_inst.device, &sampler_info, nullptr, &data->default_sampler));

		// Allocate and update sampler descriptor
		data->reserved_sampler_descriptors = data->descriptor_buffer_sampler.Allocate();
		data->reserved_sampler_descriptors.WriteDescriptor(data->default_sampler);
	}

	static void CreateDefaultTextures()
	{
		// Default white texture
		std::vector<uint8_t> white_pixel = { 0xFF, 0xFF, 0xFF, 0xFF };

		CreateTextureArgs texture_args = {};
		texture_args.width = 1;
		texture_args.height = 1;
		texture_args.pixels = white_pixel;

		data->default_white_texture_handle = CreateTexture(texture_args);

		// Default normal texture
		std::vector<uint8_t> normal_pixel = { 127, 127, 255, 255 };
		texture_args.pixels = normal_pixel;

		data->default_normal_texture_handle = CreateTexture(texture_args);
	}

	static void CreateUniformBuffers()
	{
		data->camera_uniform_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize buffer_size = sizeof(CameraData);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			Vulkan::CreateUniformBuffer(buffer_size, data->camera_uniform_buffers[i]);
			data->reserved_ubo_descriptors.WriteDescriptor(data->camera_uniform_buffers[i],
				buffer_size, RESERVED_DESCRIPTOR_UNIFORM_BUFFER_CAMERA * VulkanInstance::MAX_FRAMES_IN_FLIGHT + i);
		}
	}

	static void CreateInstanceBuffers()
	{
		data->instance_buffers.resize(VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize instance_buffer_size = MAX_DRAW_LIST_ENTRIES * sizeof(glm::mat4);

		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			Vulkan::Buffer& instance_buffer = data->instance_buffers[i];
			Vulkan::CreateBuffer(instance_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, instance_buffer);
			VkCheckResult(vkMapMemory(vk_inst.device, instance_buffer.memory, 0, instance_buffer_size, 0, &instance_buffer.ptr));
		}
	}

	static void CreateMaterialBuffer()
	{
		VkDeviceSize material_buffer_size = MAX_UNIQUE_MATERIALS * sizeof(MaterialResource);
		Vulkan::CreateBuffer(material_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, data->material_buffer);
		data->reserved_storage_descriptors.WriteDescriptor(data->material_buffer, material_buffer_size, RESERVED_DESCRIPTOR_STORAGE_BUFFER_MATERIAL);
	}

	static std::vector<uint32_t> CompileShader(const char* filepath, shaderc_shader_kind shader_type)
	{
		auto shader_text = ReadFile(filepath);

		shaderc::CompileOptions compile_options = {};
#ifdef _DEBUG
		compile_options.SetOptimizationLevel(shaderc_optimization_level_zero);
#else
		compile_options.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif
		compile_options.SetIncluder(std::make_unique<ShadercIncluder>());

		shaderc::SpvCompilationResult shader_compile_result = data->shader_compiler.CompileGlslToSpv(
			shader_text.data(), shader_text.size(),	shader_type, filepath, "main", compile_options);

		if (shader_compile_result.GetCompilationStatus() != shaderc_compilation_status_success)
		{
			VK_EXCEPT("Vulkan", shader_compile_result.GetErrorMessage());
		}

		return { shader_compile_result.begin(), shader_compile_result.end() };
	}

	static VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code)
	{
		VkShaderModuleCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		create_info.codeSize = code.size() * sizeof(uint32_t);
		create_info.pCode = code.data();

		VkShaderModule shader_module;
		VkCheckResult(vkCreateShaderModule(vk_inst.device, &create_info, nullptr, &shader_module));

		return shader_module;
	}

	static void CreateGraphicsPipeline(const char* vs_path, const char* ps_path, GraphicsPass& pass)
	{
		// TODO: Vulkan extension for shader objects? No longer need to make compiled pipeline states then
		// https://www.khronos.org/blog/you-can-use-vulkan-without-pipelines-today
		std::vector<uint32_t> vert_spv = CompileShader(vs_path, shaderc_vertex_shader);
		std::vector<uint32_t> frag_spv = CompileShader(ps_path, shaderc_fragment_shader);

		VkShaderModule vert_shader_module = CreateShaderModule(vert_spv);
		VkShaderModule frag_shader_module = CreateShaderModule(frag_spv);

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
		depth_stencil.depthTestEnable = pass.depth_enabled;
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

		std::array<VkPushConstantRange, 1> push_constants = {};
		push_constants[0].offset = 0;
		push_constants[0].size = 2 * sizeof(uint32_t);
		push_constants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayout, 5> descriptor_set_layouts = {
			data->descriptor_buffer_uniform.GetDescriptorSetLayout(),
			data->descriptor_buffer_storage.GetDescriptorSetLayout(),
			data->descriptor_buffer_storage_image.GetDescriptorSetLayout(),
			data->descriptor_buffer_sampled_image.GetDescriptorSetLayout(),
			data->descriptor_buffer_sampler.GetDescriptorSetLayout(),
		};

		VkPipelineLayoutCreateInfo pipeline_layout_info = {};
		pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.setLayoutCount = (uint32_t)descriptor_set_layouts.size();
		pipeline_layout_info.pSetLayouts = descriptor_set_layouts.data();
		pipeline_layout_info.pushConstantRangeCount = (uint32_t)push_constants.size();
		pipeline_layout_info.pPushConstantRanges = push_constants.data();

		VkCheckResult(vkCreatePipelineLayout(vk_inst.device, &pipeline_layout_info, nullptr, &pass.pipeline_layout));

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
		pipeline_info.layout = pass.pipeline_layout;
		pipeline_info.renderPass = VK_NULL_HANDLE;
		pipeline_info.subpass = 0;
		pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_info.basePipelineIndex = -1;
		pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

		VkPipelineRenderingCreateInfo pipeline_rendering_info = {};
		pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		pipeline_rendering_info.colorAttachmentCount = (uint32_t)pass.color_attachments.size();
		std::vector<VkFormat> color_attachment_formats = pass.GetColorAttachmentFormats();
		pipeline_rendering_info.pColorAttachmentFormats = color_attachment_formats.data();
		pipeline_rendering_info.depthAttachmentFormat = pass.depth_attachment.format;
		pipeline_rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
		pipeline_rendering_info.viewMask = 0;
		pipeline_info.pNext = &pipeline_rendering_info;

		VkCheckResult(vkCreateGraphicsPipelines(vk_inst.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pass.pipeline));

		vkDestroyShaderModule(vk_inst.device, frag_shader_module, nullptr);
		vkDestroyShaderModule(vk_inst.device, vert_shader_module, nullptr);
	}

	static void CreateComputePipeline(const char* cs_path, ComputePass& pass)
	{
		std::vector<uint32_t> compute_spv = CompileShader(cs_path, shaderc_compute_shader);
		VkShaderModule compute_shader_module = CreateShaderModule(compute_spv);

		VkPipelineShaderStageCreateInfo compute_shader_stage_info = {};
		compute_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		compute_shader_stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		compute_shader_stage_info.module = compute_shader_module;
		compute_shader_stage_info.pName = "main";

		std::array<VkPushConstantRange, 1> push_constants = {};
		push_constants[0].offset = 0;
		push_constants[0].size = 2 * sizeof(uint32_t);
		push_constants[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		std::array<VkDescriptorSetLayout, 5> descriptor_set_layouts = {
			data->descriptor_buffer_uniform.GetDescriptorSetLayout(),
			data->descriptor_buffer_storage.GetDescriptorSetLayout(),
			data->descriptor_buffer_storage_image.GetDescriptorSetLayout(),
			data->descriptor_buffer_sampled_image.GetDescriptorSetLayout(),
			data->descriptor_buffer_sampler.GetDescriptorSetLayout(),
		};

		VkPipelineLayoutCreateInfo pipeline_layout_info = {};
		pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.setLayoutCount = (uint32_t)descriptor_set_layouts.size();
		pipeline_layout_info.pSetLayouts = descriptor_set_layouts.data();
		pipeline_layout_info.pushConstantRangeCount = (uint32_t)push_constants.size();
		pipeline_layout_info.pPushConstantRanges = push_constants.data();

		VkCheckResult(vkCreatePipelineLayout(vk_inst.device, &pipeline_layout_info, nullptr, &pass.pipeline_layout));

		VkComputePipelineCreateInfo pipeline_info = {};
		pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline_info.layout = pass.pipeline_layout;
		pipeline_info.stage = compute_shader_stage_info;
		pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

		VkCheckResult(vkCreateComputePipelines(vk_inst.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pass.pipeline));

		vkDestroyShaderModule(vk_inst.device, compute_shader_module, nullptr);
	}

	static void CreateFramebuffers()
	{
		// Create lighting pass color and depth attachments
		data->lighting_pass.color_attachments.resize(1);
		Vulkan::CreateImage(
			vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height,
			VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			data->lighting_pass.color_attachments[0]
		);
		Vulkan::CreateImageView(
			VK_IMAGE_ASPECT_COLOR_BIT,
			data->lighting_pass.color_attachments[0]
		);
		data->reserved_storage_image_descriptors.WriteDescriptor(data->lighting_pass.color_attachments[0], VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, 0);

		Vulkan::CreateImage(
			vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height,
			Vulkan::FindDepthFormat(),
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			data->lighting_pass.depth_attachment
		);
		Vulkan::CreateImageView(
			VK_IMAGE_ASPECT_DEPTH_BIT,
			data->lighting_pass.depth_attachment
		);

		data->lighting_pass.color_attachment_infos.resize(1);
		data->lighting_pass.color_attachment_infos[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		data->lighting_pass.color_attachment_infos[0].imageView = data->lighting_pass.color_attachments[0].view;
		data->lighting_pass.color_attachment_infos[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		data->lighting_pass.color_attachment_infos[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		data->lighting_pass.color_attachment_infos[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		data->lighting_pass.color_attachment_infos[0].clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };

		data->lighting_pass.depth_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		data->lighting_pass.depth_attachment_info.imageView = data->lighting_pass.depth_attachment.view;
		data->lighting_pass.depth_attachment_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		data->lighting_pass.depth_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		data->lighting_pass.depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		data->lighting_pass.depth_attachment_info.clearValue.depthStencil = { 1.0, 0 };

		// Create post-processing color attachment
		data->post_process_pass.attachments.resize(1);
		Vulkan::CreateImage(vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height, vk_inst.swapchain.format, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, data->post_process_pass.attachments[0]);
		Vulkan::CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT, data->post_process_pass.attachments[0]);
		data->reserved_storage_image_descriptors.WriteDescriptor(data->post_process_pass.attachments[0], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, 1);

		// Create imgui color attachment info
		data->imgui.pass.color_attachment_infos.resize(1);
		data->imgui.pass.color_attachment_infos[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		data->imgui.pass.color_attachment_infos[0].imageView = data->post_process_pass.attachments[0].view;
		data->imgui.pass.color_attachment_infos[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		data->imgui.pass.color_attachment_infos[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		data->imgui.pass.color_attachment_infos[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}

	static void DestroyFramebuffers()
	{
		data->lighting_pass.DestroyAttachments();
		data->post_process_pass.DestroyAttachments();
	}

	static void CreateRenderPasses()
	{
		data->reserved_storage_image_descriptors = data->descriptor_buffer_storage_image.Allocate(RESERVED_DESCRIPTOR_STORAGE_IMAGE_COUNT);
		CreateFramebuffers();

		// Lighting raster pass
		{
			data->lighting_pass.depth_enabled = true;
			CreateGraphicsPipeline("assets/shaders/VertexShader.vert", "assets/shaders/FragmentShader.frag", data->lighting_pass);
		}

		// Post-processing compute pass
		{
			CreateComputePipeline("assets/shaders/PostProcessCS.glsl", data->post_process_pass);
		}
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
		VkCheckResult(vkCreateDescriptorPool(vk_inst.device, &pool_info, nullptr, &data->imgui.descriptor_pool));
		
		// Init imgui
		ImGui_ImplGlfw_InitForVulkan(vk_inst.window, true);
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = vk_inst.instance;
		init_info.PhysicalDevice = vk_inst.physical_device;
		init_info.Device = vk_inst.device;
		init_info.QueueFamily = vk_inst.queue_indices.graphics_compute;
		init_info.Queue = vk_inst.queues.graphics;
		init_info.PipelineCache = VK_NULL_HANDLE;
		init_info.DescriptorPool = data->imgui.descriptor_pool;
		init_info.MinImageCount = VulkanInstance::MAX_FRAMES_IN_FLIGHT;
		init_info.ImageCount = VulkanInstance::MAX_FRAMES_IN_FLIGHT;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.Allocator = nullptr;
		init_info.UseDynamicRendering = true;
		init_info.ColorAttachmentFormat = data->post_process_pass.attachments[0].format;
		init_info.CheckVkResultFn = VkCheckResult;
		ImGui_ImplVulkan_Init(&init_info, nullptr);

		// Upload imgui font
		VkCommandBuffer command_buffer = Vulkan::BeginImmediateCommand();
		ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
		Vulkan::EndImmediateCommand(command_buffer);
		ImGui_ImplVulkan_DestroyFontUploadObjects();
	}

	static void ExitDearImGui()
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		vkDestroyDescriptorPool(vk_inst.device, data->imgui.descriptor_pool, nullptr);
	}

	void Init(::GLFWwindow* window)
	{
		Vulkan::Init(window);

		data = new Data();
		data->window = window;

		CreateRenderPasses();
		InitDearImGui();

		CreateCommandBuffers();
		CreateSyncObjects();

		data->reserved_ubo_descriptors = data->descriptor_buffer_uniform.Allocate(RESERVED_DESCRIPTOR_UNIFORM_BUFFER_COUNT * VulkanInstance::MAX_FRAMES_IN_FLIGHT);
		CreateUniformBuffers();
		data->reserved_storage_descriptors = data->descriptor_buffer_storage.Allocate(RESERVED_DESCRIPTOR_STORAGE_BUFFER_COUNT);
		CreateMaterialBuffer();
		CreateInstanceBuffers();

		CreateDefaultSamplers();
		CreateDefaultTextures();
	}

	void Exit()
	{
		vkDeviceWaitIdle(vk_inst.device);

		ExitDearImGui();

		// Clean up vulkan stuff
		for (size_t i = 0; i < VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroyFence(vk_inst.device, data->in_flight_fences[i], nullptr);
			vkDestroySemaphore(vk_inst.device, data->render_finished_semaphores[i], nullptr);
		}

		vkDestroySampler(vk_inst.device, data->default_sampler, nullptr);

		for (size_t i = 0; i < VulkanInstance::VulkanInstance::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			Vulkan::DestroyBuffer(data->camera_uniform_buffers[i]);
			Vulkan::DestroyBuffer(data->instance_buffers[i]);
		}
		Vulkan::DestroyBuffer(data->material_buffer);

		// Clean up the renderer data
		delete data;

		// Finally, exit the vulkan render backend
		Vulkan::Exit();
	}

	void BeginFrame(const glm::mat4& view, const glm::mat4& proj)
	{
		// Wait for completion of all rendering for the current swapchain image
		VkCheckResult(vkWaitForFences(vk_inst.device, 1, &data->in_flight_fences[vk_inst.current_frame], VK_TRUE, UINT64_MAX));

		// Reset the fence
		VkCheckResult(vkResetFences(vk_inst.device, 1, &data->in_flight_fences[vk_inst.current_frame]));

		// Get an available image index from the swap chain
		bool resized = Vulkan::SwapChainAcquireNextImage();
		if (resized)
		{
			DestroyFramebuffers();
			CreateFramebuffers();
			return;
		}

		// Reset and record the command buffer
		VkCommandBuffer graphics_command_buffer = data->command_buffers[vk_inst.current_frame];
		vkResetCommandBuffer(graphics_command_buffer, 0);

		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplVulkan_NewFrame();
		ImGui::NewFrame();

		CameraData ubo = {};
		ubo.view = view;
		ubo.proj = proj;
		ubo.view_pos = glm::inverse(view)[3];

		memcpy(data->camera_uniform_buffers[vk_inst.current_frame].ptr, &ubo, sizeof(ubo));
	}

	void RenderFrame()
	{
		VkCommandBuffer command_buffer = data->command_buffers[vk_inst.current_frame];

		VkCommandBufferBeginInfo command_buffer_begin_info = {};
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VkCheckResult(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));

		// ----------------------------------------------------------------------------------------------------------------
		// Lighting pass

		std::vector<VkImageMemoryBarrier2> lighting_pass_begin_transitions =
		{
			Vulkan::ImageMemoryBarrier(data->lighting_pass.color_attachments[0], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
			Vulkan::ImageMemoryBarrier(data->lighting_pass.depth_attachment, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
		};
		Vulkan::CmdTransitionImageLayouts(command_buffer, lighting_pass_begin_transitions);

		VkRenderingInfo light_pass_rendering_info = data->lighting_pass.GetRenderingInfo();
		vkCmdBeginRendering(command_buffer, &light_pass_rendering_info);
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data->lighting_pass.pipeline);

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
		uint32_t camera_ubo_index = RESERVED_DESCRIPTOR_UNIFORM_BUFFER_CAMERA * VulkanInstance::MAX_FRAMES_IN_FLIGHT + vk_inst.current_frame;
		vkCmdPushConstants(command_buffer, data->lighting_pass.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &camera_ubo_index);

		// Bind descriptor buffers
		std::array<VkDescriptorBufferBindingInfoEXT, 5> descriptor_buffer_binding_infos =
		{
			data->descriptor_buffer_uniform.GetBindingInfo(),
			data->descriptor_buffer_storage.GetBindingInfo(),
			data->descriptor_buffer_storage_image.GetBindingInfo(),
			data->descriptor_buffer_sampled_image.GetBindingInfo(),
			data->descriptor_buffer_sampler.GetBindingInfo()
		};
		vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_infos.size(), descriptor_buffer_binding_infos.data());

		uint32_t buffer_indices[5] = { 0, 1, 2, 3, 4 };
		VkDeviceSize buffer_offsets[5] = { 0, 0, 0, 0, 0 };
		vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			data->lighting_pass.pipeline_layout, 0, 5, &buffer_indices[0], &buffer_offsets[0]);

		// Instance buffer
		const Vulkan::Buffer& instance_buffer = data->instance_buffers[vk_inst.current_frame];

		for (size_t i = 0; i < data->draw_list.current_entry; ++i)
		{
			const DrawList::Entry& entry = data->draw_list.entries[i];

			// TODO: Default mesh
			MeshResource* mesh = nullptr;
			if (VK_RESOURCE_HANDLE_VALID(entry.mesh_handle))
			{
				mesh = data->mesh_slotmap.Find(entry.mesh_handle);
			}
			VK_ASSERT(mesh && "Tried to render a mesh with an invalid mesh handle");

			// Check material handles for validity
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(entry.material_handle));
			MaterialResource* material = data->material_slotmap.Find(entry.material_handle);
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(material->base_color_texture_handle));
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(material->normal_texture_handle));
			VK_ASSERT(VK_RESOURCE_HANDLE_VALID(material->metallic_roughness_texture_handle));

			vkCmdPushConstants(command_buffer, data->lighting_pass.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 4, sizeof(uint32_t), &entry.material_handle.index);

			// Vertex and index buffers
			VkBuffer vertex_buffers[] = { mesh->vertex_buffer.buffer, instance_buffer.buffer };
			VkDeviceSize offsets[] = { 0, i * sizeof(glm::mat4) };
			vkCmdBindVertexBuffers(command_buffer, 0, 2, vertex_buffers, offsets);
			vkCmdBindIndexBuffer(command_buffer, mesh->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			// Draw call
			vkCmdDrawIndexed(command_buffer, mesh->index_buffer.size / sizeof(uint32_t), 1, 0, 0, 0);

			data->stats.total_vertex_count += mesh->vertex_buffer.size / sizeof(Vertex);
			data->stats.total_triangle_count += (mesh->index_buffer.size / sizeof(uint32_t)) / 3;
		}
		vkCmdEndRendering(command_buffer);

		// ----------------------------------------------------------------------------------------------------------------
		// Post-process pass

		std::vector<VkImageMemoryBarrier2> post_process_begin_transitions =
		{
			Vulkan::ImageMemoryBarrier(data->lighting_pass.color_attachments[0], VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL),
			Vulkan::ImageMemoryBarrier(data->post_process_pass.attachments[0], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL)
		};
		Vulkan::CmdTransitionImageLayouts(command_buffer, post_process_begin_transitions);

		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data->post_process_pass.pipeline);

		vk_inst.pFunc.cmd_bind_descriptor_buffers_ext(command_buffer, (uint32_t)descriptor_buffer_binding_infos.size(), descriptor_buffer_binding_infos.data());
		vk_inst.pFunc.cmd_set_descriptor_buffer_offsets_ext(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			data->post_process_pass.pipeline_layout, 0, 5, &buffer_indices[0], &buffer_offsets[0]);

		uint32_t src_dst_indices[2] = { RESERVED_DESCRIPTOR_STORAGE_IMAGE_HDR, RESERVED_DESCRIPTOR_STORAGE_IMAGE_SDR };
		vkCmdPushConstants(command_buffer, data->post_process_pass.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(uint32_t), src_dst_indices);
		uint32_t dispatch_x = VK_ALIGN_POW2(vk_inst.swapchain.extent.width, 8) / 8;
		uint32_t dispatch_y = VK_ALIGN_POW2(vk_inst.swapchain.extent.height, 8) / 8;
		vkCmdDispatch(command_buffer, dispatch_x, dispatch_y, 1);
	}

	void RenderUI()
	{
		ImGui::Begin("Renderer");

		ImGui::Text("Total vertex count: %u", data->stats.total_vertex_count);
		ImGui::Text("Total triangle count: %u", data->stats.total_triangle_count);

		ImGui::End();
	}

	void EndFrame()
	{
		VkCommandBuffer command_buffer = data->command_buffers[vk_inst.current_frame];

		//// Render ImGui
		Vulkan::CmdTransitionImageLayout(command_buffer, data->post_process_pass.attachments[0], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		VkRenderingInfo imgui_rendering_info = data->imgui.pass.GetRenderingInfo();
		vkCmdBeginRendering(command_buffer, &imgui_rendering_info);

		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer, nullptr);

		vkCmdEndRendering(command_buffer);

		// Copy final result to swapchain image
		VkImageCopy copy_region = {};
		copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.srcSubresource.mipLevel = 0;
		copy_region.srcSubresource.baseArrayLayer = 0;
		copy_region.srcSubresource.layerCount = 1;
		copy_region.srcOffset = { 0, 0, 0 };
		copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.dstSubresource.mipLevel = 0;
		copy_region.dstSubresource.baseArrayLayer = 0;
		copy_region.dstSubresource.layerCount = 1;
		copy_region.dstOffset = { 0, 0, 0 };
		copy_region.extent = { vk_inst.swapchain.extent.width, vk_inst.swapchain.extent.height, 1 };

		// Note: Temporary hack
		Vulkan::Image swapchain_image = Vulkan::SwapChainGetCurrentImage();

		std::vector<VkImageMemoryBarrier2> swapchain_copy_begin_transitions =
		{
			Vulkan::ImageMemoryBarrier(data->post_process_pass.attachments[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			Vulkan::ImageMemoryBarrier(swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		};
		Vulkan::CmdTransitionImageLayouts(command_buffer, swapchain_copy_begin_transitions);

		vkCmdCopyImage(command_buffer, data->post_process_pass.attachments[0].image, data->post_process_pass.attachments[0].layout,
			swapchain_image.image, swapchain_image.layout, 1, &copy_region);

		Vulkan::CmdTransitionImageLayout(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		VkCheckResult(vkEndCommandBuffer(command_buffer));

		// Submit the command buffer for execution
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore wait_semaphores[] = { vk_inst.swapchain.image_available_semaphores[vk_inst.current_frame] };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = wait_semaphores;
		submit_info.pWaitDstStageMask = wait_stages;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;

		std::vector<VkSemaphore> signal_semaphores = { data->render_finished_semaphores[vk_inst.current_frame] };
		submit_info.signalSemaphoreCount = (uint32_t)signal_semaphores.size();
		submit_info.pSignalSemaphores = signal_semaphores.data();

		VkCheckResult(vkQueueSubmit(vk_inst.queues.graphics, 1, &submit_info, data->in_flight_fences[vk_inst.current_frame]));

		// Present
		bool resized = Vulkan::SwapChainPresent(signal_semaphores);
		if (resized)
		{
			DestroyFramebuffers();
			CreateFramebuffers();
		}

		ImGui::EndFrame();

		// Reset/Update per-frame data
		data->stats.Reset();
		data->draw_list.Reset();
		vk_inst.current_frame = (vk_inst.current_frame + 1) % VulkanInstance::MAX_FRAMES_IN_FLIGHT;
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
		ResourceSlotmap<TextureResource>::ReservedResource reserved = data->texture_slotmap.Reserve();
		uint32_t num_mips = (uint32_t)std::floor(std::log2(std::max(args.width, args.height))) + 1;
		Vulkan::CreateImage(args.width, args.height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reserved.resource->image, num_mips);

		// Copy staging buffer data into final texture image memory (device local)
		Vulkan::TransitionImageLayoutImmediate(reserved.resource->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_mips);
		Vulkan::CopyBufferToImage(staging_buffer, reserved.resource->image, args.width, args.height);
		
		// Generate mips using blit
		Vulkan::GenerateMips(args.width, args.height, num_mips, VK_FORMAT_R8G8B8A8_SRGB, reserved.resource->image);

		// Clean up staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		// Create image view
		Vulkan::CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT, reserved.resource->image, num_mips);

		// Allocate and update image descriptor
		reserved.resource->descriptor = data->descriptor_buffer_sampled_image.Allocate();
		reserved.resource->descriptor.WriteDescriptor(reserved.resource->image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

		return reserved.handle;
	}

	void DestroyTexture(TextureHandle_t handle)
	{
		data->texture_slotmap.Delete(handle);
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
		ResourceSlotmap<MeshResource>::ReservedResource reserved = data->mesh_slotmap.Reserve();

		// Create vertex and index buffers
		Vulkan::CreateVertexBuffer(vertex_buffer_size, reserved.resource->vertex_buffer);
		Vulkan::CreateIndexBuffer(index_buffer_size, reserved.resource->index_buffer);

		// Copy staging buffer data into vertex and index buffers
		Vulkan::CopyBuffer(staging_buffer, reserved.resource->vertex_buffer, vertex_buffer_size);
		Vulkan::CopyBuffer(staging_buffer, reserved.resource->index_buffer, index_buffer_size, vertex_buffer_size);

		// Destroy staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		return reserved.handle;
	}

	void DestroyMesh(MeshHandle_t handle)
	{
		data->mesh_slotmap.Delete(handle);
	}

	MaterialHandle_t CreateMaterial(const CreateMaterialArgs& args)
	{
		ResourceSlotmap<MaterialResource>::ReservedResource reserved = data->material_slotmap.Reserve();

		// Base color factor and texture
		reserved.resource->data.base_color_factor = args.base_color_factor;
		if (VK_RESOURCE_HANDLE_VALID(args.base_color_texture_handle))
		{
			reserved.resource->base_color_texture_handle = args.base_color_texture_handle;
			reserved.resource->data.base_color_texture_index = args.base_color_texture_handle.index;
		}
		else
		{
			reserved.resource->base_color_texture_handle = data->default_white_texture_handle;
			reserved.resource->data.base_color_texture_index = data->default_white_texture_handle.index;
		}

		// Normal texture
		if (VK_RESOURCE_HANDLE_VALID(args.normal_texture_handle))
		{
			reserved.resource->normal_texture_handle = args.normal_texture_handle;
			reserved.resource->data.normal_texture_index = args.normal_texture_handle.index;
		}
		else
		{
			reserved.resource->normal_texture_handle = data->default_normal_texture_handle;
			reserved.resource->data.normal_texture_index = data->default_normal_texture_handle.index;
		}

		// Metallic roughness factors and texture
		reserved.resource->data.metallic_factor = args.metallic_factor;
		reserved.resource->data.roughness_factor = args.roughness_factor;
		if (VK_RESOURCE_HANDLE_VALID(args.metallic_roughness_texture_handle))
		{
			reserved.resource->metallic_roughness_texture_handle = args.metallic_roughness_texture_handle;
			reserved.resource->data.metallic_roughness_texture_index = args.metallic_roughness_texture_handle.index;
		}
		else
		{
			reserved.resource->metallic_roughness_texture_handle = data->default_white_texture_handle;
			reserved.resource->data.metallic_roughness_texture_index = data->default_white_texture_handle.index;
		}

		// Samplers
		// NOTE: Currently this is always the default sampler, ideally the model loading also creates samplers
		reserved.resource->data.sampler_index = 0;

		VkDeviceSize material_size = sizeof(MaterialData);

		// TODO: Suballocate from big upload buffer, having a staging buffer for every little upload is unnecessary
		// Create staging buffer
		Vulkan::Buffer staging_buffer;
		Vulkan::CreateStagingBuffer(material_size, staging_buffer);

		// Write data into staging buffer
		Vulkan::WriteBuffer(staging_buffer.ptr, (void*)&reserved.resource->data, material_size);

		// Copy staging buffer material resource into device local material buffer
		Vulkan::CopyBuffer(staging_buffer, data->material_buffer, material_size, 0, reserved.handle.index * material_size);

		// Destroy staging buffer
		Vulkan::DestroyBuffer(staging_buffer);

		return reserved.handle;
	}

	void DestroyMaterial(MaterialHandle_t handle)
	{
		data->material_slotmap.Delete(handle);
		// NOTE: We could update the material buffer entry here to be 0 or some invalid value,
		// but it will be overwritten anyways when the slot is re-used
	}

	void SubmitMesh(MeshHandle_t mesh_handle, MaterialHandle_t material_handle, const glm::mat4& transform)
	{
		Vulkan::Buffer& instance_buffer = data->instance_buffers[vk_inst.current_frame];
		memcpy((glm::mat4*)instance_buffer.ptr + data->draw_list.current_entry, &transform, sizeof(glm::mat4));

		DrawList::Entry& entry = data->draw_list.GetNextEntry();
		entry.mesh_handle = mesh_handle;
		entry.material_handle = material_handle;
		entry.transform = transform;
	}

}
