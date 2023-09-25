#pragma once
#include "renderer/RenderTypes.h"
#include "Common.h"

#include <type_traits>

template<typename TResource>
class ResourceSlotmap
{
public:
	struct ReservedResource
	{
		ResourceHandle_t handle;
		TResource* resource;
	};

	static constexpr size_t DEFAULT_SLOTMAP_CAPACITY = 1000;

private:
	static constexpr uint32_t INVALID_SLOT_INDEX = ~0u;

public:
	ResourceSlotmap(size_t capacity = DEFAULT_SLOTMAP_CAPACITY)
	{
		m_slots.resize(capacity);

		for (size_t i = 0; i < capacity - 1; ++i)
		{
			m_slots[i].next_free = i + 1;
			m_slots[i].version = 0;
		}
	}

	~ResourceSlotmap()
	{
	}

	ResourceSlotmap(const ResourceSlotmap& other) = delete;
	ResourceSlotmap(ResourceSlotmap&& other) = delete;
	const ResourceSlotmap& operator=(const ResourceSlotmap& other) = delete;
	ResourceSlotmap&& operator=(ResourceSlotmap&& other) = delete;

	ReservedResource Reserve()
	{
		ReservedResource result = {};
		result.handle = AllocateSlot();
		result.resource = &m_slots[result.handle.index].resource;
		
		return result;
	}

	TResource* Find(ResourceHandle_t handle)
	{
		TResource* resource = nullptr;

		if (VK_RESOURCE_HANDLE_VALID(handle))
		{
			Slot& slot = m_slots[handle.index];

			if (slot.version == handle.version)
			{
				resource = &slot.resource;
			}
		}

		return resource;
	}

	void Delete(ResourceHandle_t handle)
	{
		if (VK_RESOURCE_HANDLE_VALID(handle))
		{
			Slot& slot = m_slots[handle.index];

			if (slot.version == handle.version)
			{
				slot.version++;

				slot.next_free = m_next_free;
				m_next_free = handle.index;
				
				if constexpr (!std::is_trivially_destructible_v<TResource>)
				{
					slot.resource.~TResource();
				}
			}
		}
	}

private:
	ResourceHandle_t AllocateSlot()
	{
		if (m_next_free == INVALID_SLOT_INDEX)
		{
			// If this happens, we need to abort, or we resize the slotmap
			VK_EXCEPT("ResourceSlotmap", "Slotmap ran out of space");
		}
		// Should really not happen, this is a programmatic error
		VK_ASSERT(m_next_free < (uint32_t)m_slots.size());

		ResourceHandle_t handle;
		Slot& slot = m_slots[m_next_free];

		handle.index = m_next_free;
		handle.version = slot.version++;

		m_next_free = slot.next_free;
		slot.next_free = INVALID_SLOT_INDEX;

		return handle;
	}

private:
	struct Slot
	{
		uint32_t next_free = INVALID_SLOT_INDEX;
		uint32_t version = 0;

		TResource resource;
	};

	std::vector<Slot> m_slots;
	uint32_t m_next_free = 0;

};
