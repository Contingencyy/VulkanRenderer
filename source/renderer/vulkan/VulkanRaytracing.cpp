#include "Precomp.h"
#include "renderer/vulkan/VulkanRaytracing.h"
#include "renderer/vulkan/VulkanUtils.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanBuffer.h"

namespace Vulkan
{

	namespace Raytracing
	{

		VulkanBuffer BuildBLAS(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& vertex_buffer, const VulkanBuffer& index_buffer, VulkanBuffer& scratch_buffer,
			uint32_t first_vertex, uint32_t num_vertices, uint32_t vertex_stride, VkIndexType index_type, const std::string& name)
		{
			uint32_t max_vertex = first_vertex + num_vertices;
			uint32_t max_triangles = num_vertices / 3;

			/*VkTransformMatrixKHR transform = {
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f
			};*/

			// Get device addresses for vertex and index buffers
			VkDeviceOrHostAddressConstKHR vertex_buffer_device_address = {};
			VkDeviceOrHostAddressConstKHR index_buffer_device_address = {};
			//VkDeviceOrHostAddressConstKHR transform_buffer_device_address = {};

			vertex_buffer_device_address.deviceAddress = vertex_buffer.vk_device_address;
			index_buffer_device_address.deviceAddress = index_buffer.vk_device_address;

			// BLAS geometry description
			VkAccelerationStructureGeometryKHR blas_geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			blas_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			blas_geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			blas_geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			blas_geometry.geometry.triangles.vertexData = vertex_buffer_device_address;
			blas_geometry.geometry.triangles.maxVertex = max_vertex;
			blas_geometry.geometry.triangles.vertexStride = vertex_stride;
			blas_geometry.geometry.triangles.indexType = index_type;
			blas_geometry.geometry.triangles.indexData = index_buffer_device_address;
			blas_geometry.geometry.triangles.transformData.deviceAddress = 0;
			blas_geometry.geometry.triangles.transformData.hostAddress = nullptr;
			//blas_geometry.geometry.triangles.transformData = ;
			blas_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; // VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR

			// BLAS build info
			VkAccelerationStructureBuildGeometryInfoKHR blas_build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			blas_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			blas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
			blas_build_info.geometryCount = 1;
			blas_build_info.pGeometries = &blas_geometry;

			// Get the required buffer sizes
			VkAccelerationStructureBuildSizesInfoKHR build_sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
			vk_inst.pFunc.raytracing.get_acceleration_structure_build_sizes(vk_inst.device,
				VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_build_info, &max_triangles, &build_sizes);

			// Create the BLAS buffer
			VulkanBuffer blas_buffer = Buffer::CreateBLAS(build_sizes.accelerationStructureSize, name);

			VkAccelerationStructureCreateInfoKHR acceleration_structure_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
			acceleration_structure_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			acceleration_structure_info.buffer = blas_buffer.vk_buffer;
			acceleration_structure_info.size = build_sizes.accelerationStructureSize;

			VkCheckResult(vk_inst.pFunc.raytracing.create_acceleration_structure(vk_inst.device, &acceleration_structure_info, nullptr, &blas_buffer.vk_acceleration_structure));

			// Create the scratch buffer
			scratch_buffer = Buffer::CreateScratch(build_sizes.buildScratchSize, name + " scratch");

			// Fill the rest of the BLAS build info
			blas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
			blas_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			blas_build_info.dstAccelerationStructure = blas_buffer.vk_acceleration_structure;
			blas_build_info.scratchData.deviceAddress = scratch_buffer.vk_device_address;

			// Build ranges
			VkAccelerationStructureBuildRangeInfoKHR build_range_info = {};
			build_range_info.primitiveCount = max_triangles;
			build_range_info.primitiveOffset = 0;
			build_range_info.firstVertex = 0;
			build_range_info.transformOffset = 0;
			std::vector<VkAccelerationStructureBuildRangeInfoKHR*> build_range_infos = { &build_range_info };

			// Do the actual command for building the acceleration structure
			vk_inst.pFunc.raytracing.cmd_build_acceleration_structures(
				command_buffer.vk_command_buffer,
				1,
				&blas_build_info,
				build_range_infos.data()
			);

			blas_buffer.vk_device_address = Util::GetAccelerationStructureDeviceAddress(blas_buffer.vk_acceleration_structure);

			return blas_buffer;
		}

		VulkanBuffer BuildTLAS(const VulkanCommandBuffer& command_buffer)
		{
			return {};
		}

	}

}
