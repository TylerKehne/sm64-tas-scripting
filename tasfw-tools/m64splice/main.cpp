// m64splice: a movie for one game version out of two.
//
//   m64splice <first libsm64> <first.m64> <second libsm64> <second.m64> <level> <out.m64>
//
// Plays each movie on its own DLL through the LevelTransitions script to find the first
// frame at which it is inside <level> (a decomp LevelNum or its name: 19 or bitfs), then runs
// the SpliceMovie script on the first DLL: the first movie up to its entry frame, then the
// second movie's inputs from its own entry frame to its end, exported as <out.m64>. Prints
// both entry frames and the offset that maps a frame of the second movie to the output, then
// traces the output on the first DLL and the second movie on its own from their entry frames
// (the MarioTrace script) and reports the first frame at which what the inputs produce
// differs, with the state each movie carried into the level. Exit 0 when the spliced part
// plays identically to the end of the second movie, 1 when a movie never enters the level
// or the traces part, 2 on usage or load errors. The output's header is what M64::save
// writes.
//
// --lead <frames>: splice that many frames before each entry frame instead of at it. The
// warp into a level is a stretch where the stick does nothing but the C buttons still turn
// the camera, and what they turned is carried into the level (the 8-directions camera's yaw
// offset persists across levels); when the first movie turns the camera during its warp and
// the second does not, cutting before those presses is what makes the two enter alike.
// `dllcheck --trace` shows both movies' camera state around their entry frames.
//
// A DLL image is loaded once per path, so two movies for the same version share the one
// resource; different versions need their own files (res/sm64_jp_0.dll, res/sm64_us_0.dll).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>

#include <LevelTransitions.hpp>
#include <LibSm64.hpp>
#include <MarioTrace.hpp>
#include <SpliceMovie.hpp>
#include <tasfw/Inputs.hpp>

namespace fs = std::filesystem;

namespace
{
	// A LevelNum from a number or the decomp's name (LevelTransitions::LevelName); -1 if neither.
	int ParseLevel(const std::string& text)
	{
		if (!text.empty() && std::isdigit((unsigned char)text[0]))
			return std::stoi(text);
		for (int level = 0; level < 64; level++)
			if (text == LevelTransitions::LevelName(int16_t(level)))
				return level;
		return -1;
	}

	int64_t LastFrame(const M64& movie)
	{
		return movie.frames.empty() ? 0 : int64_t(movie.frames.rbegin()->first) + 1;
	}

	void PrintTransitions(const std::vector<LevelTransitions::Transition>& transitions)
	{
		for (const LevelTransitions::Transition& t : transitions)
			std::printf("  frame %6lld: level %2d (%s), area %d, course %d\n", (long long)t.frame, int(t.level),
				LevelTransitions::LevelName(t.level), int(t.area), int(t.course));
	}

	// The first frame at which the movie is inside `level`, or -1. Prints the transitions.
	int64_t EntryFrame(LibSm64& resource, M64& movie, int level, const char* which)
	{
		auto status = TopLevelScriptBuilder<LevelTransitions>::Build(movie).ImportResource(&resource).Run(LastFrame(movie));
		std::printf("%s movie %s, %lld inputs:\n", which, movie.fileName.string().c_str(), (long long)LastFrame(movie));
		PrintTransitions(status.transitions);
		for (const LevelTransitions::Transition& t : status.transitions)
			if (t.level == level)
				return t.frame;
		return -1;
	}
}

int main(int argc, char** argv)
{
	if (argc < 7)
	{
		std::fprintf(stderr, "usage: m64splice <first libsm64> <first.m64> <second libsm64> <second.m64> <level> <out.m64> [--lead frames]\n");
		return 2;
	}
	fs::path firstDll = argv[1];
	fs::path firstPath = argv[2];
	fs::path secondDll = argv[3];
	fs::path secondPath = argv[4];
	int level = ParseLevel(argv[5]);
	fs::path outPath = argv[6];
	int64_t lead = 0;
	for (int i = 7; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--lead" && i + 1 < argc)
			lead = std::stoll(argv[++i]);
		else
		{
			std::fprintf(stderr, "unknown option %s\n", argv[i]);
			return 2;
		}
	}
	if (level < 0)
	{
		std::fprintf(stderr, "error: %s is not a level number or name (19 or bitfs)\n", argv[5]);
		return 2;
	}

	try
	{
		LibSm64Config config;
		config.dllPath = firstDll;
		config.countryCode = CountryCode::SUPER_MARIO_64_J;
		config.saveMode = LibSm64SaveMode::Dirty;
		LibSm64 first(config);
		std::optional<LibSm64> secondOwned;
		LibSm64* second = &first;
		if (!fs::equivalent(firstDll, secondDll))
		{
			config.dllPath = secondDll;
			secondOwned.emplace(config);
			second = &*secondOwned;
		}

		M64 firstMovie(firstPath);
		M64 secondMovie(secondPath);
		if (!firstMovie.load() || firstMovie.frames.empty())
		{
			std::fprintf(stderr, "error: could not load movie %s\n", firstPath.string().c_str());
			return 2;
		}
		if (!secondMovie.load() || secondMovie.frames.empty())
		{
			std::fprintf(stderr, "error: could not load movie %s\n", secondPath.string().c_str());
			return 2;
		}

		int64_t firstEntry = EntryFrame(first, firstMovie, level, "first");
		int64_t secondEntry = EntryFrame(*second, secondMovie, level, "second");
		if (firstEntry < 0 || secondEntry < 0)
		{
			std::fprintf(stderr, "error: %s never enters level %d (%s)\n",
				(firstEntry < 0 ? firstPath : secondPath).string().c_str(), level, LevelTransitions::LevelName(int16_t(level)));
			return 1;
		}
		std::printf("\nlevel %d (%s): first movie enters at frame %lld, second at frame %lld; frame f of the second movie is frame f%+lld of the output\n",
			level, LevelTransitions::LevelName(int16_t(level)), (long long)firstEntry, (long long)secondEntry, (long long)(firstEntry - secondEntry));
		if (lead > firstEntry || lead > secondEntry)
		{
			std::fprintf(stderr, "error: --lead %lld is more than the frames before an entry\n", (long long)lead);
			return 2;
		}
		int64_t firstCut = firstEntry - lead;
		int64_t secondCut = secondEntry - lead;
		if (lead > 0)
			std::printf("splicing %lld frames earlier, at frame %lld of the first movie and %lld of the second\n", (long long)lead, (long long)firstCut, (long long)secondCut);

		auto splice = TopLevelScriptBuilder<SpliceMovie>::Build(firstMovie).ImportResource(&first).Run(firstCut, secondMovie, secondCut, outPath);
		if (!splice.asserted)
		{
			std::fprintf(stderr, "error: could not write %s\n", outPath.string().c_str());
			return 2;
		}
		std::printf("wrote %s: %lld inputs (%lld from the first movie, %lld from the second)\n\n", outPath.string().c_str(),
			(long long)splice.frames, (long long)firstCut, (long long)(splice.frames - firstCut));

		// Does the spliced part play on the first DLL as the second movie plays on its own? Trace
		// both from their entry frames and compare what the inputs produce, frame by frame.
		M64 outMovie(outPath);
		if (!outMovie.load())
		{
			std::fprintf(stderr, "error: could not read back %s\n", outPath.string().c_str());
			return 2;
		}
		int64_t secondLast = LastFrame(secondMovie) - 1;
		auto donor = TopLevelScriptBuilder<MarioTrace>::Build(secondMovie).ImportResource(second).Run(secondEntry, secondLast);
		auto result = TopLevelScriptBuilder<MarioTrace>::Build(outMovie).ImportResource(&first).Run(firstEntry, firstEntry + (secondLast - secondEntry));
		std::printf("at the entry frame:\n  second movie: %s\n  output:       %s\n", donor.samples.front().Describe().c_str(), result.samples.front().Describe().c_str());
		size_t count = std::min(donor.samples.size(), result.samples.size());
		for (size_t k = 0; k < count; k++)
		{
			if (donor.samples[k].SameMovement(result.samples[k]))
				continue;
			std::printf("\nthe output parts from the second movie after %zu frames in the level:\n", k);
			if (k > 0)
				std::printf("  last same:    %s\n                %s\n", donor.samples[k - 1].Describe().c_str(), result.samples[k - 1].Describe().c_str());
			std::printf("  second movie: %s\n  output:       %s\n", donor.samples[k].Describe().c_str(), result.samples[k].Describe().c_str());
			return 1;
		}
		std::printf("\nthe output plays the second movie's %zu frames in the level identically (position, speed, action, facing)\n", count);
		return 0;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "error: %s\n", e.what());
		return 2;
	}
}
