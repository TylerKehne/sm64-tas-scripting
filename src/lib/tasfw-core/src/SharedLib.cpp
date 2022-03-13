#include <tasfw/SharedLib.hpp>

#include <codecvt>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>

#if defined(_WIN32)
	#include <windows.h>

SharedLib::SharedLib(const std::filesystem::path& fileName) :
	libFileName(fileName.string()),
	handle(
		[&]() -> HMODULE
		{
	HMODULE res = LoadLibraryW(fileName.c_str());
	if (res == nullptr)
	{
		DWORD lastError = GetLastError();
		throw std::system_error(lastError, std::system_category());
	}
	return res;
		}())
{
}
SharedLib::~SharedLib()
{
	bool good = FreeLibrary(handle);
	if (!good)
	{
		DWORD lastError = GetLastError();
		std::cerr << "FreeLibrary error: "
							<< std::system_error(lastError, std::system_category()).what()
							<< '\n';
		std::cerr << "terminating...\n";
		std::terminate();
	}
}
void* SharedLib::get(const char* symbol)
{
	FARPROC res = GetProcAddress(handle, symbol);
	if (res == nullptr)
	{
		DWORD lastError = GetLastError();
		throw std::system_error(lastError, std::system_category());
	}

	return reinterpret_cast<void*>(res);
}
std::unordered_map<std::string, SectionInfo> SharedLib::readSections()
{
	using std::ios_base;
	std::unordered_map<std::string, SectionInfo> sectionMap;

	std::ifstream file(libFileName);
	uint16_t numSections;
	std::unique_ptr<IMAGE_SECTION_HEADER[]> sections;
	std::unique_ptr<char[]> strTable;
	// Unlike Linux ELF, the PE format is incredibly convoluted.
	// I could use a library, but all the ones I've found are humongous.
	{
		// Locate PE signature offset
		file.seekg(0x3C, ios_base::beg);
		uint32_t nextOffset;
		file.read(reinterpret_cast<char*>(&nextOffset), sizeof(uint32_t));
		// PE signature is 4 bytes, so skip those
		file.seekg(nextOffset + 4, ios_base::beg);
		IMAGE_FILE_HEADER fileHeader;
		// Read out file header
		file.read(reinterpret_cast<char*>(&fileHeader), sizeof(IMAGE_FILE_HEADER));
		numSections = fileHeader.NumberOfSections;
		// calculate string table offset
		// According to MS it's deprecated, but the DLLs still use it
		nextOffset = fileHeader.PointerToSymbolTable +
			sizeof(IMAGE_SYMBOL) * fileHeader.NumberOfSymbols;
		// Jump past optional header
		file.seekg(fileHeader.SizeOfOptionalHeader, ios_base::cur);
		// Read out sections
		sections.reset(new IMAGE_SECTION_HEADER[fileHeader.NumberOfSections]);
		file.read(
			reinterpret_cast<char*>(sections.get()),
			fileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER));
		// Read string table size
		uint32_t strTableSize;
		file.seekg(nextOffset, ios_base::beg);
		file.read(reinterpret_cast<char*>(&strTableSize), sizeof(uint32_t));
		// Read out string table
		file.seekg(nextOffset, ios_base::beg);
		strTable.reset(new char[strTableSize]);
		file.read(strTable.get(), strTableSize);
	}
	std::string name;
	for (size_t i = 0; i < numSections; i++)
	{
		// Copy the name into a std::string
		for (size_t j = 0; j < 8; j++)
		{
			if (sections[i].Name[j] == '\0')
			{
				name = std::string(
					reinterpret_cast<char*>(static_cast<BYTE*>(sections[i].Name)), j);
				break;
			}
		}
		// If the name begins with /, it's an offset into the string table, so
		// find it
		if (name[0] == '/')
		{
			uint32_t off = strtoul(name.c_str() + 1, nullptr, 10);
			name				 = std::string(&strTable[off]);
		}
		// MS also put the section size in a union
		sectionMap[name] = SectionInfo {
			reinterpret_cast<char*>(handle) + sections[i].VirtualAddress,
			sections[i].Misc.VirtualSize};
	}
	return sectionMap;
}
#elif defined(__linux__)

	#include <dlfcn.h>
	#include <elf.h>
	#include <link.h>

SharedLib::SharedLib(const std::filesystem::path& fileName) :
	libFileName(fileName.string()),
	handle(
		[&]() -> void*
		{
	void* res = dlopen(fileName.c_str(), RTLD_NOW);
	if (res == nullptr)
	{
		throw std::runtime_error(dlerror());
	}
	return res;
		}())
{
}
SharedLib::~SharedLib()
{
	int res = dlclose(handle);
	if (res != 0)
	{
		std::cerr << "dlclose error: " << dlerror() << '\n';
		std::cerr << "terminating...\n";
		std::terminate();
	}
}
void* SharedLib::get(const char* symbol)
{
	dlerror();
	void* res = dlsym(handle, symbol);
	char* err = dlerror();
	if (err != nullptr)
	{
		throw std::runtime_error(err);
	}
	return res;
}
std::unordered_map<std::string, SectionInfo> SharedLib::readSections()
{
	using std::ios_base;

	std::unordered_map<std::string, SectionInfo> sectionMap;

	intptr_t baseAddress;
	{
		link_map* linkMap;
		int returnCode = dlinfo(handle, RTLD_DI_LINKMAP, &linkMap);
		if (returnCode == -1)
		{
			throw std::runtime_error(dlerror());
		}
		baseAddress = linkMap->l_addr;
	}

	std::ifstream file(libFileName);
	file.seekg(0, ios_base::beg);
	std::unique_ptr<Elf64_Shdr[]> sections;
	std::unique_ptr<char[]> strTable;
	uint16_t numSections;
	{
		// read header
		Elf64_Ehdr header;
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (header.e_shoff == 0)
		{
			throw std::runtime_error("No section table");
		}
		numSections = header.e_shnum;
		// read section headers
		file.seekg(header.e_shoff, ios_base::beg);
		sections.reset(new Elf64_Shdr[header.e_shnum]);
		file.read(
			reinterpret_cast<char*>(sections.get()),
			header.e_shnum * sizeof(Elf64_Shdr));

		const Elf64_Shdr& strtab_sect = sections[header.e_shstrndx];

		file.seekg(strtab_sect.sh_offset, ios_base::beg);
		strTable.reset(new char[strtab_sect.sh_size]);
		file.read(strTable.get(), strtab_sect.sh_size);
	}

	for (uint16_t i = 0; i < numSections; i++)
	{
		const auto& sect										= sections[i];
		sectionMap[&strTable[sect.sh_name]] = SectionInfo {
			reinterpret_cast<void*>(baseAddress + sect.sh_addr), sect.sh_size};
	}
	return sectionMap;
}
#endif