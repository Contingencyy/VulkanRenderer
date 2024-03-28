#include "Precomp.h"
#include "renderer/vulkan/VulkanRaytracing.h"
#include "renderer/vulkan/VulkanUtils.h"
#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanDeviceMemory.h"

namespace Vulkan
{

	namespace Raytracing
	{

		VulkanBuffer BuildBLAS(const VulkanCommandBuffer& command_buffer, const VulkanBuffer& vertex_buffer, const VulkanBuffer& index_buffer, VulkanBuffer& scratch_buffer,
			uint32_t num_vertices, uint32_t vertex_stride, uint32_t num_triangles, VkIndexType index_type, const std::string& name)
		{
			/*VkTransformMatrixKHR transform = {
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f
			};*/

			// Get device addresses for vertex and index buffers
			VkDeviceOrHostAddressConstKHR vertex_buffer_device_address = {};
			VkDeviceOrHostAddressConstKHR index_buffer_device_address = {};
			//VkDeviceOrHostAddressConstKHR transform_buffer_device_address = {};

			vertex_buffer_device_address.deviceAddress = Vulkan::Util::GetBufferDeviceAddress(vertex_buffer);
			index_buffer_device_address.deviceAddress = Vulkan::Util::GetBufferDeviceAddress(index_buffer);
			//transform_buffer_device_address.deviceAddress = Vulkan::Util::GetBufferDeviceAddress(transform_buffer);

			// BLAS geometry description
			VkAccelerationStructureGeometryKHR blas_geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			blas_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			blas_geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			blas_geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			blas_geometry.geometry.triangles.vertexData = vertex_buffer_device_address;
			blas_geometry.geometry.triangles.maxVertex = num_vertices;
			blas_geometry.geometry.triangles.vertexStride = vertex_stride;
			blas_geometry.geometry.triangles.indexType = index_type;
			blas_geometry.geometry.triangles.indexData = index_buffer_device_address;
			blas_geometry.geometry.triangles.transformData.deviceAddress = 0;
			blas_geometry.geometry.triangles.transformData.hostAddress = nullptr;
			//blas_geometry.geometry.triangles.transformData = transform_buffer_device_address;
			blas_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; // VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR

			// BLAS build info
			VkAccelerationStructureBuildGeometryInfoKHR blas_build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			blas_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			blas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
			blas_build_info.geometryCount = 1;
			blas_build_info.pGeometries = &blas_geometry;

			// Get the required buffer sizes
			VkAccelerationStructureBuildSizesInfoKHR blas_build_sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
			vk_inst.pFunc.raytracing.get_acceleration_structure_build_sizes(vk_inst.device,
				VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_build_info, &num_triangles, &blas_build_sizes);

			// Create the BLAS buffer
			VulkanBuffer blas_buffer = Buffer::CreateAccelerationStructure(blas_build_sizes.accelerationStructureSize, name);

			VkAccelerationStructureCreateInfoKHR acceleration_structure_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
			acceleration_structure_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			acceleration_structure_info.buffer = blas_buffer.vk_buffer;
			acceleration_structure_info.size = blas_build_sizes.accelerationStructureSize;

			VkCheckResult(vk_inst.pFunc.raytracing.create_acceleration_structure(vk_inst.device, &acceleration_structure_info, nullptr, &blas_buffer.vk_acceleration_structure));

			// Create the scratch buffer
			scratch_buffer = Buffer::CreateAccelerationStructureScratch(blas_build_sizes.buildScratchSize, name + " scratch");

			// Fill the rest of the BLAS build info
			blas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
			blas_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			blas_build_info.dstAccelerationStructure = blas_buffer.vk_acceleration_structure;
			blas_build_info.scratchData.deviceAddress = Vulkan::Util::GetBufferDeviceAddress(scratch_buffer);

			// Build ranges
			VkAccelerationStructureBuildRangeInfoKHR build_range_info = {};
			build_range_info.primitiveCount = num_triangles;
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

			return blas_buffer;
		}

		VulkanBuffer BuildTLAS(const VulkanCommandBuffer& command_buffer, VulkanBuffer& scratch_buffer, VulkanBuffer& instance_buffer,
			uint32_t num_blas, const VulkanBuffer* const blas_buffers, const VkTransformMatrixKHR* const blas_transforms, const std::string& name)
		{
			// Get the acceleration structure geometry data for each BLAS
			std::vector<VkAccelerationStructureInstanceKHR> instances(num_blas);
			for (uint32_t i = 0; i < num_blas; ++i)
			{
				VkAccelerationStructureInstanceKHR& instance = instances[i];
				instance.transform = blas_transforms[i];
				instance.accelerationStructureReference = Vulkan::Util::GetAccelerationStructureDeviceAddress(blas_buffers[i].vk_acceleration_structure);
				instance.mask = 0xFF;
				instance.instanceCustomIndex = i;
				instance.instanceShaderBindingTableRecordOffset = 0;
				instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
			}
			
			// Create the instance buffer and write the BLAS instances
			uint64_t instance_buffer_byte_size = num_blas * sizeof(VkAccelerationStructureInstanceKHR);
			instance_buffer = Buffer::CreateAccelerationStructureInstances(instance_buffer_byte_size, name + " instance buffer");

			uint8_t* ptr_instance_buffer = reinterpret_cast<uint8_t*>(DeviceMemory::Map(instance_buffer.memory, instance_buffer_byte_size, 0));
			memcpy(ptr_instance_buffer, instances.data(), instance_buffer_byte_size);
			DeviceMemory::Unmap(instance_buffer.memory);

			VkDeviceOrHostAddressConstKHR instance_buffer_device_address = {};
			instance_buffer_device_address.deviceAddress = Vulkan::Util::GetBufferDeviceAddress(instance_buffer);

			// TLAS geometry info
			VkAccelerationStructureGeometryKHR tlas_geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			tlas_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
			tlas_geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
			tlas_geometry.geometry.instances.arrayOfPointers = VK_FALSE;
			tlas_geometry.geometry.instances.data = instance_buffer_device_address;
			tlas_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

			// TLAS build info
			VkAccelerationStructureBuildGeometryInfoKHR tlas_build_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			tlas_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
			tlas_build_info.geometryCount = 1;
			tlas_build_info.pGeometries = &tlas_geometry;
			tlas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

			// Get TLAS required build sizes
			uint32_t num_primitives = instances.size();
			VkAccelerationStructureBuildSizesInfoKHR tlas_build_sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
			vk_inst.pFunc.raytracing.get_acceleration_structure_build_sizes(vk_inst.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
				&tlas_build_info, &num_primitives, &tlas_build_sizes);

			// Create the TLAS buffer
			VulkanBuffer tlas_buffer = Buffer::CreateAccelerationStructure(tlas_build_sizes.accelerationStructureSize, name);

			// Create the vulkan acceleration structure
			VkAccelerationStructureCreateInfoKHR acceleration_structure_info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
			acceleration_structure_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
			acceleration_structure_info.buffer = tlas_buffer.vk_buffer;
			acceleration_structure_info.size = tlas_build_sizes.accelerationStructureSize;

			VkCheckResult(vk_inst.pFunc.raytracing.create_acceleration_structure(vk_inst.device, &acceleration_structure_info, nullptr, &tlas_buffer.vk_acceleration_structure));

			// Create the scratch buffer
			scratch_buffer = Buffer::CreateAccelerationStructureScratch(tlas_build_sizes.buildScratchSize, name + " scratch");

			// Build the acceleration structure
			tlas_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			tlas_build_info.dstAccelerationStructure = tlas_buffer.vk_acceleration_structure;
			tlas_build_info.scratchData.deviceAddress = Vulkan::Util::GetBufferDeviceAddress(scratch_buffer);

			VkAccelerationStructureBuildRangeInfoKHR build_range_info = {};
			build_range_info.primitiveCount = num_primitives;
			build_range_info.primitiveOffset = 0;
			build_range_info.firstVertex = 0;
			build_range_info.transformOffset = 0;
			std::vector<VkAccelerationStructureBuildRangeInfoKHR*> build_range_infos = { &build_range_info };

			vk_inst.pFunc.raytracing.cmd_build_acceleration_structures(
				command_buffer.vk_command_buffer,
				1,
				&tlas_build_info,
				build_range_infos.data()
			);

			return tlas_buffer;
		}

	}

}
