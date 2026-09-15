#pragma once
#ifndef SLOTHANDLE_H
#error "SlotHandle.t.hpp should only be included by SlotHandle.hpp"
#else

template <derived_from_specialization_of<Resource> TResource>
void SlotHandle<TResource>::Release()
{
	if (resource && slotId != -1)
		resource->DisposeState(slotId);
	resource = nullptr;
	slotId = -1;
}

template <derived_from_specialization_of<Resource> TResource>
bool SlotHandle<TResource>::isValid()
{
	// No resource: default-constructed or moved-from.
	if (!resource)
		return false;

	//Start save handle is always valid
	if (slotId == -1)
		return true;

	return resource->HasState(slotId);
}

#endif
