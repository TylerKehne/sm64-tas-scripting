#include <tasfw/M64.hpp>
#include <fstream>
#include <ios>
#include <iostream>
#include <system_error>

static uint16_t byteswap(uint16_t x)
{
	return (x >> 8U) | (x << 8U);
}

static uint32_t byteswap(uint32_t x)
{
	return (x >> 24U) | (((x << 8U) >> 24U ) << 8U) | (((x >> 8U) << 24U) >> 8U) | (x << 24U);
}

int M64::load()
{
	std::ifstream f(fileName.c_str(), std::ios_base::binary);
	f.exceptions(std::ios_base::failbit | std::ios_base::badbit);

	uint16_t buttons;
	int8_t stick_x, stick_y;


	try
	{
		f.seekg(0, std::ios::end);
		int length = int(f.tellg());
		if (length == 0)
		{
			std::cerr << "empty M64\n";
			return 0;
		}

		// The game the movie is for (the members); a file too short for a header keeps the defaults.
		if (length >= 0xEA)
		{
			uint32_t bigEndianRom;
			uint16_t bigEndianCountry;
			f.seekg(0xE4, std::ios_base::beg);
			f.read(reinterpret_cast<char*>(&bigEndianRom), sizeof(uint32_t));
			f.read(reinterpret_cast<char*>(&bigEndianCountry), sizeof(uint16_t));
			metadata.rom = Rom(byteswap(bigEndianRom));
			metadata.countryCode = CountryCode(byteswap(bigEndianCountry));
		}
		f.seekg(0x400, std::ios_base::beg);

		uint64_t index = 0;
		while (true)
		{
			uint16_t bigEndianButtons;
			if (f.peek() == std::ifstream::traits_type::eof())
				break;
			f.read(reinterpret_cast<char*>(&bigEndianButtons), sizeof(uint16_t));
			if (f.eof()) break;

			buttons = byteswap(bigEndianButtons);

			f.read(reinterpret_cast<char*>(&stick_x), sizeof(uint8_t));
			if (f.eof()) break;

			f.read(reinterpret_cast<char*>(&stick_y), sizeof(uint8_t));
			if (f.eof()) break;

			frames[index] = Inputs(buttons, stick_x, stick_y);
			index++;
		}
	}
	catch (std::invalid_argument& e)
	{
		std::cerr << e.what() << std::endl;
		return 0;
	}

	return 1;
}

int M64::save(long initFrame)
{
	if (fileName.empty())
		return 0;

	if (frames.empty())
		return 1;

	std::ofstream f;
	bool newFile = !std::filesystem::exists(fileName);
	if (!newFile)
		f = std::ofstream(fileName, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
	else
		f = std::ofstream(fileName, std::ios_base::trunc | std::ios_base::binary);

	// An open failure (missing directory, locked file) must be a false return, not an exception:
	// callers export from inside OpenMP regions, where an escaping exception aborts the process.
	if (!f.is_open())
		return 0;

	f.exceptions(std::ios_base::failbit | std::ios_base::badbit);

	uint64_t lastFrame = frames.rbegin()->first;

	try
	{
		// Write signature/version number (see https://tasvideos.org/EmulatorResources/Mupen/M64)
		if (newFile)
		{
			f.seekp(0x0, std::ios_base::beg);
			uint32_t signature = byteswap(uint32_t(0x4D36341A));
			uint8_t versionNumber = 3;
			f.write(reinterpret_cast<char*>(&signature), sizeof(uint32_t));
			f.write(reinterpret_cast<char*>(&versionNumber), sizeof(uint8_t));
		}

		// Write number of frames
		f.seekp(0xC, std::ios_base::beg);
		uint32_t value = std::numeric_limits<uint32_t>::max();
		f.write(reinterpret_cast<char*>(&value), sizeof(uint32_t));

		// Write ROM signature + country code
		if (newFile)
		{
			// FPS, number of controllers
			f.seekp(0x14, std::ios_base::beg);
			uint8_t fps = 60;
			uint8_t nControllers = 1;
			f.write(reinterpret_cast<char*>(&fps), sizeof(uint8_t));
			f.write(reinterpret_cast<char*>(&nControllers), sizeof(uint8_t));

			// number of inputs
			f.seekp(0x18, std::ios_base::beg);
			uint32_t samples = uint32_t(lastFrame + 1);
			f.write(reinterpret_cast<char*>(&samples), sizeof(uint32_t));

			// m64 type
			uint16_t m64Type = byteswap((uint16_t)2);
			f.write(reinterpret_cast<char*>(&m64Type), sizeof(uint16_t));

			// Enable Controller 1
			f.seekp(0x20, std::ios_base::beg);
			uint8_t controller1Enabled = 1;
			f.write(reinterpret_cast<char*>(&controller1Enabled), sizeof(uint8_t));

			// ROM name
			f.seekp(0xC4, std::ios_base::beg);
			char romName[32] = "SUPER MARIO 64";
			f.write(reinterpret_cast<char*>(romName), 14);

		}

		// The game the movie is for, for new and existing files alike: the members are the
		// movie's own (loaded from this file, or copied from the source movie by ExportM64).
		f.seekp(0xE4, std::ios_base::beg);
		uint32_t bigEndianRom = byteswap((uint32_t)metadata.rom);
		uint16_t bigEndianCountry = byteswap((uint16_t)metadata.countryCode);
		f.write(reinterpret_cast<char*>(&bigEndianRom), sizeof(uint32_t));
		f.write(reinterpret_cast<char*>(&bigEndianCountry), sizeof(uint16_t));

		// Write frames: one walk over the sorted frames, a zero input for every frame not in
		// it. A lookup per frame was four binary searches, which clang-cl does not inline into
		// this loop (a third of the save's samples, ROADMAP 3.16).
		f.seekp(0x400 + 4 * initFrame, std::ios_base::beg);
		auto next = frames.begin();
		for (uint64_t i = 0; i <= lastFrame; i++)
		{
			uint16_t bigEndianButtons = 0;
			int8_t stickX = 0;
			int8_t stickY = 0;

			if (next != frames.end() && next->first == i)
			{
				bigEndianButtons = byteswap(next->second.buttons);
				stickX = next->second.stick_x;
				stickY = next->second.stick_y;
				++next;
			}

			f.write(reinterpret_cast<char*>(&bigEndianButtons), sizeof(uint16_t));
			f.write(reinterpret_cast<char*>(&stickX), sizeof(uint8_t));
			f.write(reinterpret_cast<char*>(&stickY), sizeof(uint8_t));
		}
	}
	catch (std::invalid_argument& e)
	{
		std::cerr << e.what() << std::endl;
		return 0;
	}

	return 1;
}
