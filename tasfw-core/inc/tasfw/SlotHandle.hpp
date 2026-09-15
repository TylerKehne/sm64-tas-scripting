#pragma once
#include <cstdint>
#include <tasfw/Concepts.hpp>
#include <tasfw/Resource.hpp>

#ifndef SLOTHANDLE_H
#define SLOTHANDLE_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

// Owns one savestate slot: destroying the handle erases the slot. slotId -1 with a resource
// is the start save, which is never erased.
template <derived_from_specialization_of<Resource> TResource>
class SlotHandle
{
public:
	SlotHandle(TResource* resource, int64_t slotId) : resource(resource), slotId(slotId) { }

	// A move transfers ownership: the source forgets its resource, so its destructor
	// releases nothing. With the defaulted move the source kept the id and erased the slot
	// the destination had just received, which lost every save a child handed to its
	// parent on Modify (they were then replayed instead of loaded; see test_script.cpp).
	SlotHandle(SlotHandle<TResource>&& other) noexcept : resource(other.resource), slotId(other.slotId)
	{
		other.resource = nullptr;
		other.slotId = -1;
	}

	SlotHandle<TResource>& operator=(SlotHandle<TResource>&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			resource = other.resource;
			slotId = other.slotId;
			other.resource = nullptr;
			other.slotId = -1;
		}
		return *this;
	}

	SlotHandle(const SlotHandle<TResource>&) = delete;
	SlotHandle<TResource>& operator= (const SlotHandle<TResource>&) = delete;

	~SlotHandle() { Release(); }

	bool isValid();

private:
	friend class Script<TResource>; // reads the id to load the slot

	TResource* resource = nullptr;
	int64_t slotId = -1;

	void Release();
};

//Include template method implementations
#include "tasfw/SlotHandle.t.hpp"

#endif
