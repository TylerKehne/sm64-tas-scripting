#pragma once
#include <vector>
#include "tasfw/Resource.hpp"

#ifndef LIBSM64_H
	#define LIBSM64_H

class LibSm64Mem
{
public:
	std::vector<uint8_t> buf1;
	std::vector<uint8_t> buf2;
};

class LibSm64 : public Resource<LibSm64Mem>
{
public:
	SharedLib dll;
	std::vector<SegVal> segment;

	LibSm64(const std::filesystem::path& dllPath);
	void save(LibSm64Mem& state) const;
	void load(const LibSm64Mem& state);
	void advance();
	void* addr(const char* symbol) const;
	std::size_t getStateSize(const LibSm64Mem& state) const;
	uint32_t getCurrentFrame() const;
};

#endif