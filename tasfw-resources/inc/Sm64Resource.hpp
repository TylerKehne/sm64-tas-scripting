#pragma once
#include <tasfw/Resource.hpp>
#include "Sm64Mem.hpp"

#ifndef SM64RESOURCE_H
#define SM64RESOURCE_H

// The SM64 game as a resource, whichever runs it: the game's own DLL (LibSm64) or the pyramid
// corner of BitFS reimplemented from the decomp (BitfsCorner, ROADMAP 4.13). Both serve the
// same symbols on the same structs and share the one state type (Sm64Mem), so a script is
// written once, as Script<Sm64Resource>, and runs on either; the operations a script reaches
// (advance, save, load, addr, setInputs) are Resource's virtuals already, so the script pays
// nothing for not knowing which resource it has. The class adds nothing of its own: it is the
// name a script and a builder hold a resource by (the maintainer, 2026-10-12).
class Sm64Resource : public Resource<Sm64Mem>
{
public:
	using Resource<Sm64Mem>::Resource;
};

#endif
