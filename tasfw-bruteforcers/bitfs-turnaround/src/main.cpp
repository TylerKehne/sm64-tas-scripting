
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>

#include <tasfw/Inputs.hpp>
#include <tasfw/Script.hpp>
#include <sm64/Camera.hpp>
#include <sm64/Trig.hpp>
#include <sm64/Types.hpp>
#include <sm64/ObjectFields.hpp>

#include <General.hpp>
#include <BitFSPyramidOscillation.hpp>

#include <omp.h>
#include "BitFsConfig.hpp"
#include <BitFsScApproach.hpp>
#include <sm64/Sm64.hpp>
#include "Scattershot_BitfsDr.hpp"
#include "Scattershot_BitfsDrApproach.hpp"
#include "Scattershot_BitfsDrRecover.hpp"
#include <range/v3/all.hpp>
#include "TiltTargetShot.hpp"

#pragma comment(lib, "Ws2_32.lib")

class TestScript : public TopLevelScript<LibSm64>
{
public:
	bool validation() { return true; }

	bool execution()
	{
		LongLoad(3300); //3277, 3536

		//const BehaviorScript* trackPlatformBehavior = (const BehaviorScript*)(resource->addr("bhvPlatformOnTrack"));
		//Object* objectPool = (Object*)(resource->addr("gObjectPool"));
		//Object* trackPlatform = &objectPool[85];
		//if (trackPlatform->behavior != trackPlatformBehavior)
		//	return false;

		//! UNSAFE
		//trackPlatform->oPosX = -1945.0f;
		//AdvanceFrameRead();
		//Save();
		Camera* camera = *(Camera**)(resource->addr("gCamera"));
		MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
		auto stick = Inputs::GetClosestInputByYawExact(-16384, 32, camera->yaw);
		AdvanceFrameWrite(Inputs(0, stick.first, stick.second));

		while (marioState->action != ACT_IDLE)
			AdvanceFrameWrite(Inputs(0, 0, 0));

		auto status = Modify<BitFsPyramidOscillation>(0.69f, 3, false);
		auto status2 = Modify<BitFsScApproach>(0, 3, 0.69f, status);
		return true;
	}

	bool assertion()
	{
		// Save m64Diff to M64
		M64Diff diff = GetBaseDiff();
		for (auto& [frame, inputs]: diff.frames)
		{
			_m64->frames[frame] = inputs;
		}

		return true;
	}
};

template <class TOutputState>
class ExportSolutions : public TopLevelScript<LibSm64>
{
public:
	ExportSolutions(int startFrame, const std::vector<ScattershotSolution<TOutputState>>& solutions) : _startFrame(startFrame), _solutions(solutions) {}

	bool validation() { return true; }

	bool execution()
	{
		MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
		Camera* camera = *(Camera**)(resource->addr("gCamera"));
		const BehaviorScript* pyramidmBehavior = (const BehaviorScript*)(resource->addr("bhvLllTiltingInvertedPyramid"));
		Object* objectPool = (Object*)(resource->addr("gObjectPool"));
		Object* pyramid = &objectPool[84];

		LongLoad(_startFrame);

		for (auto& solution : _solutions)
		{
			ExecuteAdhoc([&]()
				{
					Apply(solution.m64Diff);

					char fileName[128];
					sprintf(fileName, "C:\\repos\\sm64-tas-scripting\\res\\bitfs_nut_%f_%f_%f_%f_%f.m64",
						pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ, marioState->forwardVel, marioState->vel[1]);
					ExportM64(fileName);

					return true;
				});
		}

		return true;
	}

	bool assertion() { return true; }

private:
	int _startFrame = 0;
	const std::vector<ScattershotSolution<TOutputState>>& _solutions;
};

void InitConfiguration(Configuration& configuration)
{
	configuration.StartFrame = 3330;//3515;//3317;
	configuration.PelletMaxScripts = 20;
	configuration.PelletMaxFrameDistance = 50;
	configuration.MaxBlocks = 1000000;
	configuration.TotalThreads = 16;
	configuration.MaxShots = 3000;
	configuration.PelletsPerShot = 200;
	configuration.ShotsPerUpdate = 10;
	configuration.StartFromRootEveryNShots = 100;
	configuration.CsvSamplePeriod = 0;
	configuration.MaxConsecutiveFailedPellets = 10;
	configuration.MaxSolutions = 100;
	configuration.Seed = 6;
	configuration.FitnessTieGoesToNewBlock = false;
	configuration.Deterministic = false;
	configuration.CsvOutputDirectory = std::string("C:/repos/sm64-tas-scripting/analysis/");
	configuration.M64Path = std::filesystem::path("C:/repos/sm64-tas-scripting/res/comissonPyra2-Fanart_XZ.m64");

	configuration.SetResourcePaths(std::vector<std::string>
		{
			"C:/repos/sm64-tas-scripting/res/sm64_jp_0.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_1.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_2.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_3.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_4.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_5.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_6.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_7.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_8.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_9.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_10.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_11.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_12.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_13.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_14.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_15.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_16.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_17.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_18.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_19.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_20.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_21.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_22.dll",
			"C:/repos/sm64-tas-scripting/res/sm64_jp_23.dll"
		});
}

template<typename... Args>
auto SortBy(Args... args) {
	return ranges::make_action_closure([=](auto&& range) mutable {
		using value_type = ranges::range_value_t<decltype(range)>;
		ranges::actions::sort(range, [=](const value_type& a, const value_type& b) {
			bool result;
			auto apply_criteria = [&](auto&& func)
			{
				result = func(a) <= func(b);
				return func(a) == func(b);
			};

			// Short-circuit when elements are not equal
			(apply_criteria(args) && ...);
			return result;
			});
		});
}

int main(int argc, const char* argv[])
{
	namespace fs = std::filesystem;
	fs::path cfgPath =
		((argc >= 2) ? fs::path(argv[1]) : getPathToSelf().parent_path() / "config.json");

	BitFs_ConfigData cfg = BitFs_ConfigData::load(cfgPath);

	auto lib_path = cfg.libSM64;
	auto m64_path = cfg.m64File;

	Configuration config;
	InitConfiguration(config);

	M64 m64 = M64(std::filesystem::path("C:/repos/sm64-tas-scripting/res/comissonPyra2-Fanart_x-Z.m64"));
	//M64 m64 = M64(std::filesystem::path("C:/repos/sm64-tas-scripting/res/comissonPyra2-Fanart_x-Z.m64"));
	m64.load();

	std::vector<LibSm64> resources;
	resources.reserve(config.ResourcePaths.size());
	for (auto& path : config.ResourcePaths)
	{
		LibSm64Config resourceConfig;
		resourceConfig.dllPath = path;
		resourceConfig.lightweight = true;
		resourceConfig.countryCode = CountryCode::SUPER_MARIO_64_J;

		resources.emplace_back(resourceConfig);
	}

	auto tiltTargetShotArgs = TiltTargetShotArgs();
	tiltTargetShotArgs.InitialFrame = 3330;
	tiltTargetShotArgs.Neighborhood = 0;
	tiltTargetShotArgs.TargetNx = 0.355f;
	tiltTargetShotArgs.TargetNz = 0.355f;
	tiltTargetShotArgs.ErrorType = (int)TiltTargetShot::ErrorType::ADJUSTED;
	tiltTargetShotArgs.TargetXDimension = true;
	tiltTargetShotArgs.FixNonTargetDimensionARE = false;

	config.MaxShots = 30000;
	config.MaxSolutions = 1;//41;
	//config.CsvSamplePeriod = 1;
	config.FitnessTieGoesToNewBlock = false;
	config.Deterministic = true;
	config.Seed = 70;
	auto tiltSolutions = TiltTargetShot::ConfigureScattershot(config)
		.ImportResourcePerThread([&](auto threadId) { return &resources[threadId]; })
		.ConfigureStateTracker(tiltTargetShotArgs)
		.Run<TiltTargetShot>(tiltTargetShotArgs);

	tiltTargetShotArgs.FixNonTargetDimensionARE = true;
	tiltTargetShotArgs.TargetARE = tiltSolutions[0].data.adjustedRemainderError[0];
	tiltTargetShotArgs.TargetXDimension = false;

	config.MaxSolutions = 1;// *41;
	config.MaxShots = 1000000;
	config.ShotsPerUpdate = 300;
	//config.CsvSamplePeriod = 1;
	config.Deterministic = false;
	config.Seed = 4;
	tiltSolutions = TiltTargetShot::ConfigureScattershot(config)
		.ImportResourcePerThread([&](auto threadId) { return &resources[threadId]; })
		.PipeFrom(tiltSolutions)
		.ConfigureStateTracker(tiltTargetShotArgs)
		.Run<TiltTargetShot>(tiltTargetShotArgs);

	tiltSolutions |= SortBy([](const auto& x) { return x.data.adjustedRemainderError[0]; },
		[](const auto& x) { return x.data.adjustedRemainderError[2]; });

	M64 m643 = M64(config.M64Path);
	m643.load();

	TopLevelScriptBuilder<ExportSolutions<TiltTargetShotSolution>>::Build(m643)
		.ImportResource(&resources[0])
		.Run(config.StartFrame, tiltSolutions);

	std::vector<ScattershotSolution<Scattershot_BitfsDr_Solution>> solutions;
	solutions.reserve(config.MaxSolutions);

	NormalSpecsDto normalSpecsDto;
	normalSpecsDto.onlyMinMajor = true;
	normalSpecsDto.minXzSum = 0.65f;
	normalSpecsDto.minMajor = 0.4f;
	normalSpecsDto.maxMajor = 0.601f;
	normalSpecsDto.regionsMajor = 10000;
	normalSpecsDto.minMinor = 0.19f;
	normalSpecsDto.maxMinor = 0.2f;
	normalSpecsDto.regionsMinor = 10000;

	//config.CsvSamplePeriod = 1;
	//config.MaxShots = 20000;
	int maxOscillations = 4;
	config.MaxShots = 30000;
	config.MaxSolutions = 1000;
	config.CsvSamplePeriod = 0;
	config.FitnessTieGoesToNewBlock = false;
	//config.StartFromRootEveryNShots = 10;
	for (int targetOscillation = 1; targetOscillation < maxOscillations; targetOscillation++)
	{
		if (targetOscillation == maxOscillations - 1)
		{
			//normalSpecsDto.onlyMinMajor = false;
			//normalSpecsDto.minMajor = 0.5f;
			config.MaxSolutions = 1000000;
			config.MaxShots = 50000;
			normalSpecsDto.minXzSum = 0.705f;
		}

		solutions = Scattershot_BitfsDr::ConfigureScattershot(config)
			.ImportResourcePerThread([&](auto threadId) { return &resources[threadId]; })
			.PipeFrom(solutions)
			.ConfigureStateTracker(config.StartFrame, 4, normalSpecsDto, 15)
			.Run<Scattershot_BitfsDr>(targetOscillation, normalSpecsDto);

		if (targetOscillation == 1)
		{
			config.MaxShots = 30000;
			normalSpecsDto.minXzSum = 0.705f;
			normalSpecsDto.minMajor = 0.5f;
			//config.StartFromRootEveryNShots = 50;
		}

		if (solutions.empty())
			return false;

		if (targetOscillation == 1)
		{
			if (solutions[0].data.roughTargetAngle != -24576)
			{
				solutions |= ranges::actions::remove_if([](const auto& x) { return x.data.roughTargetAngle == -24576; });
				maxOscillations++;
			}
			else
				solutions |= ranges::actions::remove_if([](const auto& x) { return x.data.roughTargetAngle == 8192; });
		}

		if (targetOscillation < 3)
			solutions |= SortBy([](const auto& x) { return -x.data.fSpd; }) | ranges::actions::take(10);
		else if (targetOscillation < maxOscillations - 1)
		{
			int parityCheck = maxOscillations % 2;

			if (targetOscillation % 2 == parityCheck)
				solutions |= SortBy([](const auto& x) { return -fabs(x.data.pyraNormZ); }) | ranges::actions::take(100);
			else
				solutions |= SortBy([](const auto& x) { return -fabs(x.data.pyraNormX); }) | ranges::actions::take(100);
		}
	}

	M64 m642 = M64(config.M64Path);
	m642.load();

	// TODO: only pipe in m64diff container
	std::vector<ScattershotSolution<Scattershot_BitfsDrApproach_Solution>> inputSolutions;
	inputSolutions.reserve(solutions.size());
	for (auto& solution : solutions)
	{
		inputSolutions.push_back(ScattershotSolution<Scattershot_BitfsDrApproach_Solution>(
			Scattershot_BitfsDrApproach_Solution(), solution.m64Diff));
	}

	config.CsvSamplePeriod = 10;
	config.MaxShots = 50000;
	config.MaxSolutions = 1000;
	auto diveSolutions = Scattershot_BitfsDrApproach::ConfigureScattershot(config)
		.ImportResourcePerThread([&](auto threadId) { return &resources[threadId]; })
		.PipeFrom(inputSolutions)
		.ConfigureStateTracker(config.StartFrame, 4, 1, normalSpecsDto.minXzSum)
		.Run<Scattershot_BitfsDrApproach>();

	TopLevelScriptBuilder<ExportSolutions<Scattershot_BitfsDrApproach_Solution>>::Build(m642)
		.ImportResource(&resources[0])
		.Run(config.StartFrame, diveSolutions);

	// TODO: only pipe in m64diff container
	std::vector<ScattershotSolution<Scattershot_BitfsDrRecover_Solution>> inputSolutions2;
	inputSolutions2.reserve(diveSolutions.size());
	for (auto& solution : diveSolutions)
	{
		inputSolutions2.push_back(ScattershotSolution<Scattershot_BitfsDrRecover_Solution>(
			Scattershot_BitfsDrRecover_Solution(), solution.m64Diff));
	}

	config.CsvSamplePeriod = 1;
	config.MaxSolutions = 100;
	auto drLandSolutions = Scattershot_BitfsDrRecover::ConfigureScattershot(config)
		.ImportResourcePerThread([&](auto threadId) { return &resources[threadId]; })
		.PipeFrom(inputSolutions2)
		.ConfigureStateTracker(config.StartFrame, 4, 1, normalSpecsDto.minXzSum)
		.Run<Scattershot_BitfsDrRecover>(StateTracker_BitfsDrRecover::Phase::ATTEMPT_DR);

	config.MaxShots = 10000;
	auto nutSolutions = Scattershot_BitfsDrRecover::ConfigureScattershot(config)
		.ImportResourcePerThread([&](auto threadId) { return &resources[threadId]; })
		.PipeFrom(drLandSolutions)
		.ConfigureStateTracker(config.StartFrame, 4, 1, normalSpecsDto.minXzSum)
		.Run<Scattershot_BitfsDrRecover>(StateTracker_BitfsDrRecover::Phase::C_UP_TRICK);

	TopLevelScriptBuilder<ExportSolutions<Scattershot_BitfsDrRecover_Solution>>::Build(m642)
		.ImportResource(&resources[0])
		.Run(config.StartFrame, nutSolutions);

	//auto status = MainScript::MainConfig<MainScript>(m64, lib_path);

	//m64.save();

	return 0;
}
