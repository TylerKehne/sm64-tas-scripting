#pragma once
#ifndef SCRIPTMETADATA_H
#error "ScriptMetadata.t.hpp should only be included by ScriptMetadata.hpp"
#else

template <derived_from_specialization_of<Resource> TResource>
SlotHandle<TResource>* SaveMetadata<TResource>::GetSlotHandle()
{
	if (!script)
		return nullptr;

	if (isStartSave)
		return &script->startSaveHandle;

	if (script->saveBank.size() <= static_cast<uint64_t>(adhocLevel))
		return nullptr;

	if (!script->saveBank[adhocLevel].contains(frame))
		return nullptr;

	return &script->saveBank[adhocLevel].find(frame)->second;
}

template <derived_from_specialization_of<Resource> TResource>
bool SaveMetadata<TResource>::IsValid()
{
	SlotHandle<TResource>* slotHandle = GetSlotHandle();
	if (slotHandle == nullptr)
		return false;

	if (!slotHandle->isValid())
	{
		script->saveBank[adhocLevel].erase(frame);
		return false;
	}

	return true;
}

#endif
