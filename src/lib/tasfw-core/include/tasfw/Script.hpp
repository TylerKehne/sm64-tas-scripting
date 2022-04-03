#pragma once
#include <unordered_map>
#include <tasfw/Game.hpp>
#include <tasfw/Inputs.hpp>
#include <sm64/Types.hpp>

class Script;
class SlotHandle;
class Game;
class TopLevelScript;
class BaseScriptStatus;

class BaseScriptStatus
{
public:
	bool validated = false;
	bool executed = false;
	bool asserted = false;
	bool validationThrew = false;
	bool executionThrew = false;
	bool assertionThrew = false;
	uint64_t validationDuration = 0;
	uint64_t executionDuration = 0;
	uint64_t assertionDuration = 0;
	uint64_t nLoads = 0;
	uint64_t nSaves = 0;
	uint64_t nFrameAdvances = 0;
	M64Diff m64Diff = M64Diff();

	BaseScriptStatus() = default;
};

template <std::derived_from<Script> TScript>
class ScriptStatus : public BaseScriptStatus, public TScript::CustomScriptStatus
{
public:
	ScriptStatus() : BaseScriptStatus(), TScript::CustomScriptStatus() {}

	ScriptStatus(BaseScriptStatus baseStatus, typename TScript::CustomScriptStatus customStatus)
		: BaseScriptStatus(baseStatus), TScript::CustomScriptStatus(customStatus) { }
};

class AdhocBaseScriptStatus
{
public:
	bool executed = false;
	bool executionThrew = false;
	uint64_t executionDuration = 0;
	uint64_t nLoads = 0;
	uint64_t nSaves = 0;
	uint64_t nFrameAdvances = 0;
	M64Diff m64Diff = M64Diff();

	AdhocBaseScriptStatus() = default;

	AdhocBaseScriptStatus(BaseScriptStatus baseStatus)
	{
		executed = baseStatus.executed;
		executionThrew = baseStatus.executionThrew;
		executionDuration = baseStatus.executionDuration;
		nLoads = baseStatus.nLoads;
		nSaves = baseStatus.nSaves;
		nFrameAdvances = baseStatus.nFrameAdvances;
		m64Diff = baseStatus.m64Diff;
	}
};

template <class TAdhocCustomScriptStatus>
class AdhocScriptStatus : public AdhocBaseScriptStatus, public TAdhocCustomScriptStatus
{
public:
	AdhocScriptStatus() : AdhocBaseScriptStatus(), TAdhocCustomScriptStatus() {}

	AdhocScriptStatus(AdhocBaseScriptStatus baseStatus, typename TAdhocCustomScriptStatus customStatus)
	: AdhocBaseScriptStatus(baseStatus), TAdhocCustomScriptStatus(customStatus) { }
};

template <class TCompareStatus>
class CompareStatus
{
public:
	virtual const AdhocScriptStatus<TCompareStatus>& Comparator(
		const AdhocScriptStatus<TCompareStatus>& a,
		const AdhocScriptStatus<TCompareStatus>& b) const  = 0;

	virtual bool Terminator(const AdhocScriptStatus<TCompareStatus>& status) const { return false; }
};

template <typename F, typename R = std::invoke_result_t<F>>
concept AdhocScript = std::same_as<R, bool>;

template <typename F, typename T>
concept AdhocCustomStatusScript = std::same_as<std::invoke_result_t<F, T&>, bool>;

/// <summary>
/// Execute a state-changing operation on the game. Parameters should correspond
/// to the script's class constructor.
/// </summary>
class Script
{
public:
	class CustomScriptStatus {};
	CustomScriptStatus CustomStatus = {};

	// TODO: make private
	Game* game = nullptr;

	Script(Script* parentScript) : _parentScript(parentScript)
	{
		if (_parentScript)
		{
			game = _parentScript->game;
			_initialFrame = GetCurrentFrame();
		}
	}

	// TODO: move this method to some utility class
	static void CopyVec3f(Vec3f dest, Vec3f source);

protected:
	template <std::derived_from<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Execute(Us&&... params)
	{
		// Save state if performant
		uint64_t initialFrame = GetCurrentFrame();
		OptionalSave();

		TScript script = TScript(this, std::forward<Us>(params)...);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		// Load if necessary
		Revert(initialFrame, script.BaseStatus[0].m64Diff);

		BaseStatus[_adhocLevel].nLoads += script.BaseStatus[0].nLoads;
		BaseStatus[_adhocLevel].nSaves += script.BaseStatus[0].nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += script.BaseStatus[0].nFrameAdvances;

		return ScriptStatus<TScript>(script.BaseStatus[0], script.CustomStatus);
	}

	template <std::derived_from<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Modify(Us&&... params)
	{
		// Save state if performant
		uint64_t initialFrame = GetCurrentFrame();
		OptionalSave();

		TScript script = TScript(this, std::forward<Us>(params)...);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		// Revert state if assertion fails, otherwise apply diff
		if (!script.BaseStatus[0].asserted || script.BaseStatus[0].m64Diff.frames.empty())
			Revert(initialFrame, script.BaseStatus[0].m64Diff);
		else
			ApplyChildDiff(script.BaseStatus[0], initialFrame);

		BaseStatus[_adhocLevel].nLoads += script.BaseStatus[0].nLoads;
		BaseStatus[_adhocLevel].nSaves += script.BaseStatus[0].nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += script.BaseStatus[0].nFrameAdvances;

		return ScriptStatus<TScript>(script.BaseStatus[0], script.CustomStatus);
	}

	template <std::derived_from<Script> TScript, typename... Us>
		requires(std::constructible_from<TScript, Script*, Us...>)
	ScriptStatus<TScript> Test(Us&&... params)
	{
		ScriptStatus<TScript> status =
			Execute<TScript>(std::forward<Us>(params)...);

		status.m64Diff = M64Diff();

		return status;
	}

	AdhocBaseScriptStatus ExecuteAdhoc(AdhocScript auto adhocScript)
	{
		int64_t initialFrame = GetCurrentFrame();

		BaseScriptStatus status = ExecuteAdhocBase(adhocScript);
		Revert(initialFrame, status.m64Diff);

		return AdhocBaseScriptStatus(status);
	}

	template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
	AdhocScriptStatus<TAdhocCustomScriptStatus> ExecuteAdhoc(F adhocScript)
	{
		int64_t initialFrame = GetCurrentFrame();

		TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
		BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });
		Revert(initialFrame, baseStatus.m64Diff);

		return AdhocScriptStatus<TAdhocCustomScriptStatus>(baseStatus, customStatus);
	}

	AdhocBaseScriptStatus ModifyAdhoc(AdhocScript auto adhocScript)
	{
		int64_t initialFrame = GetCurrentFrame();

		auto status = ExecuteAdhocBase(adhocScript);

		// Revert state if assertion fails, otherwise apply diff
		if (!status.asserted || status.m64Diff.frames.empty())
			Revert(initialFrame, status.m64Diff);
		else
			ApplyChildDiff(status, initialFrame);

		return AdhocBaseScriptStatus(status);
	}

	template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
	AdhocScriptStatus<TAdhocCustomScriptStatus> ModifyAdhoc(F adhocScript)
	{
		int64_t initialFrame = GetCurrentFrame();

		TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
		BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });
		
		// Revert state if assertion fails, otherwise apply diff
		if (!baseStatus.asserted || baseStatus.m64Diff.frames.empty())
			Revert(initialFrame, baseStatus.m64Diff);
		else
			ApplyChildDiff(baseStatus, initialFrame);

		return AdhocScriptStatus<TAdhocCustomScriptStatus>(baseStatus, customStatus);
	}

	AdhocBaseScriptStatus TestAdhoc(AdhocScript auto&& adhocScript)
	{
		auto status = ExecuteAdhoc(std::forward<AdhocScript auto>(adhocScript));
		status.m64Diff = M64Diff();

		return status;
	}

	template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
	AdhocScriptStatus<TAdhocCustomScriptStatus> TestAdhoc(F&& adhocScript)
	{
		auto status = ExecuteAdhoc<TAdhocCustomScriptStatus>(std::forward<F>(adhocScript));
		status.m64Diff = M64Diff();

		return status;
	}

	//Leaf method for comparing ad-hoc scripts. This is the one the user will call.
	template <class TCompareStatus,
		AdhocCustomStatusScript<TCompareStatus> F,
		AdhocCustomStatusScript<TCompareStatus> G,
		AdhocCustomStatusScript<TCompareStatus>... H>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> Compare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts)
	{
		auto status1 = ExecuteAdhoc<TCompareStatus>(std::forward<F>(adhocScript1));
		if (status1.executed && status1.Terminator(status1))
			return status1;

		auto status2 = ExecuteAdhoc<TCompareStatus>(std::forward<G>(adhocScript2));
		if (status2.executed && status2.Terminator(status2))
			return status2;

		if (!status1.executed)
		{
			if (!status2.executed)
				return Compare<TCompareStatus>(status1, std::forward<H>(adhocScripts)...);

			return Compare<TCompareStatus>(status2, std::forward<H>(adhocScripts)...);
		}

		return Compare<TCompareStatus>(status1.Comparator(status1, status2), std::forward<H>(adhocScripts)...);
	}

	//Leaf method for comparing ad-hoc scripts and applying the result. This is the one the user will call.
	template <class TCompareStatus,
		AdhocCustomStatusScript<TCompareStatus> F,
		AdhocCustomStatusScript<TCompareStatus> G,
		AdhocCustomStatusScript<TCompareStatus>... H>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> ModifyCompare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts)
	{
		int64_t initialFrame = GetCurrentFrame();
		auto status1 = ExecuteAdhocNoRevert<TCompareStatus>(std::forward<F>(adhocScript1));
		if (status1.executed && status1.Terminator(status1))
		{
			// Revert state if diff is empty, otherwise apply diff
			if (status1.m64Diff.frames.empty())
				Revert(initialFrame, status1.m64Diff);
			else
				ApplyChildDiff(status1, initialFrame);

			return status1;
		}

		//Revert state before executing new script
		Revert(initialFrame, status1.m64Diff);
		auto status2 = ExecuteAdhocNoRevert<TCompareStatus>(std::forward<G>(adhocScript2));
		if (status2.executed && status2.Terminator(status2))
		{
			// Revert state if diff is empty, otherwise apply diff
			if (status2.m64Diff.frames.empty())
				Revert(initialFrame, status2.m64Diff);
			else
				ApplyChildDiff(status2, initialFrame);

			return status2;
		}	

		if (!status1.executed)
		{
			if (!status2.executed)
				return ModifyCompare<TCompareStatus>(initialFrame, status1, std::forward<H>(adhocScripts)...);

			return ModifyCompare<TCompareStatus>(initialFrame, status2, std::forward<H>(adhocScripts)...);
		}

		return ModifyCompare<TCompareStatus>(initialFrame, status1.Comparator(status1, status2), std::forward<H>(adhocScripts)...);
	}

	//Same as Compare(), but with no diff returned
	template <class TCompareStatus,
		AdhocCustomStatusScript<TCompareStatus> F,
		AdhocCustomStatusScript<TCompareStatus> G,
		AdhocCustomStatusScript<TCompareStatus>... H>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> TestCompare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts)
	{
		auto status = Compare<TCompareStatus>(std::forward<F>(adhocScript1), std::forward<G>(adhocScript2), std::forward<H>(adhocScripts)...);
		status.m64Diff = M64Diff();

		return status;
	}

	// TODO: move this method to some utility class
	template <typename T>
	int sign(T val)
	{
		return (T(0) < val) - (val < T(0));
	}

	uint64_t GetCurrentFrame();
	bool IsDiffEmpty();
	M64Diff GetDiff();
	void Apply(const M64Diff& m64Diff);
	void AdvanceFrameRead();
	void AdvanceFrameWrite(Inputs inputs);
	void OptionalSave();
	void Save();
	void Save(uint64_t frame);
	void Load(uint64_t frame);
	void Rollback(uint64_t frame);
	void RollForward(int64_t frame);
	void Restore(int64_t frame);
	virtual Inputs GetInputs(int64_t frame);

	virtual bool validation() = 0;
	virtual bool execution() = 0;
	virtual bool assertion() = 0;

private:
	friend class SlotManager;
	friend class TopLevelScript;

	int64_t _adhocLevel = 0;
	int32_t _initialFrame = 0;
	std::vector<BaseScriptStatus> BaseStatus = { BaseScriptStatus() };
	std::vector<std::map<uint64_t, SlotHandle>> saveBank = { std::map<uint64_t, SlotHandle>() };
	std::vector<std::map<int64_t, uint64_t>> frameCounter = { std::map<int64_t, uint64_t>() };
	Script* _parentScript;

	bool checkPreconditions();
	bool execute();
	bool checkPostconditions();

	std::pair<uint64_t, SlotHandle*> GetLatestSave(uint64_t frame);
	void DeleteSave(int64_t frame, int64_t adhocLevel);
	void SetInputs(Inputs inputs);
	void Revert(uint64_t frame, const M64Diff& m64);
	virtual Inputs GetInputsTracked(uint64_t frame, uint64_t& counter);
	void AdvanceFrameRead(uint64_t& counter);
	virtual uint64_t GetFrameCounter(int64_t frame);
	void ApplyChildDiff(const BaseScriptStatus& status, int64_t initialFrame);

	template <typename F>
	BaseScriptStatus ExecuteAdhocBase(F adhocScript)
	{
		//Save state if performant
		OptionalSave();

		//Increment adhoc level
		_adhocLevel++;
		BaseStatus.emplace_back();
		saveBank.emplace_back();
		frameCounter.emplace_back();

		BaseStatus[_adhocLevel].validated = true;

		auto start = std::chrono::high_resolution_clock::now();
		try
		{
			BaseStatus[_adhocLevel].executed = adhocScript();
		}
		catch (std::exception& e)
		{
			BaseStatus[_adhocLevel].executionThrew = true;
		}
		auto finish = std::chrono::high_resolution_clock::now();

		BaseStatus[_adhocLevel].executionDuration =
			std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
			.count();

		BaseStatus[_adhocLevel].asserted = BaseStatus[_adhocLevel].executed;

		//Decrement adhoc level, revert state and return status
		BaseScriptStatus status = BaseStatus[_adhocLevel];
		BaseStatus.pop_back();
		saveBank.pop_back();
		frameCounter.pop_back();
		_adhocLevel--;

		BaseStatus[_adhocLevel].nLoads += status.nLoads;
		BaseStatus[_adhocLevel].nSaves += status.nSaves;
		BaseStatus[_adhocLevel].nFrameAdvances += status.nFrameAdvances;

		return status;
	}

	//Root method for Compare() template recursion. This will be called when there are no scripts left to compare the incumbent to.
	template <class TCompareStatus>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> Compare(const AdhocScriptStatus<TCompareStatus>& status1)
	{
		return status1;
	}

	//Main recursive method for comparing ad hoc scripts. Only the root and leaf use different methods
	template <class TCompareStatus, AdhocCustomStatusScript<TCompareStatus> F, AdhocCustomStatusScript<TCompareStatus>... G>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> Compare(const AdhocScriptStatus<TCompareStatus>& status1, F&& adhocScript2, G&&... adhocScripts)
	{
		auto status2 = ExecuteAdhoc<TCompareStatus>(std::forward<F>(adhocScript2));
		if (status2.executed && status2.Terminator(status2))
			return status2;

		if (!status1.executed)
		{
			if (!status2.executed)
				return Compare<TCompareStatus>(status1, std::forward<G>(adhocScripts)...);

			return Compare<TCompareStatus>(status2, std::forward<G>(adhocScripts)...);
		}

		return Compare<TCompareStatus>(status1.Comparator(status1, status2), std::forward<G>(adhocScripts)...);
	}

	//Only to be used by ModifyCompare(). Will desync if used alone, as it assumes the caller will call Revert().
	template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
	AdhocScriptStatus<TAdhocCustomScriptStatus> ExecuteAdhocNoRevert(F adhocScript)
	{
		// Save state if performant
		OptionalSave();

		TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
		BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });

		return AdhocScriptStatus<TAdhocCustomScriptStatus>(baseStatus, customStatus);
	}

	//Root method for ModifyCompare() template recursion. This will be called when there are no scripts left to compare the incumbent to.
	template <class TCompareStatus>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> ModifyCompare(int64_t initialFrame, const AdhocScriptStatus<TCompareStatus>& status1)
	{
		// Revert state if execution failed or diff is empty, otherwise apply diff
		if (!status1.executed && status1.m64Diff.frames.empty())
			Revert(initialFrame, status1.m64Diff);
		else
			ApplyChildDiff(status1, initialFrame);

		return status1;
	}

	//Main recursive method for ModifyCompare(). Only the root and leaf use different methods
	template <class TCompareStatus, AdhocCustomStatusScript<TCompareStatus> F, AdhocCustomStatusScript<TCompareStatus>... G>
		requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
	AdhocScriptStatus<TCompareStatus> ModifyCompare(int64_t initialFrame, const AdhocScriptStatus<TCompareStatus>& status1, F&& adhocScript2, G&&... adhocScripts)
	{
		//Revert state before executing new script
		Revert(initialFrame, status1.m64Diff);
		auto status2 = ExecuteAdhoc<TCompareStatus>(std::forward<F>(adhocScript2));
		if (status2.executed && status2.Terminator(status2))
		{
			// Revert state if execution failed or diff is empty, otherwise apply diff
			if (!status2.executed && status2.m64Diff.frames.empty())
				Revert(initialFrame, status2.m64Diff);
			else
				ApplyChildDiff(status2, initialFrame);

			return status2;
		}

		if (!status1.executed)
		{
			if (!status2.executed)
				return ModifyCompare<TCompareStatus>(initialFrame, status1, std::forward<G>(adhocScripts)...);

			return ModifyCompare<TCompareStatus>(initialFrame, status2, std::forward<G>(adhocScripts)...);
		}

		return ModifyCompare<TCompareStatus>(initialFrame, status1.Comparator(status1, status2), std::forward<G>(adhocScripts)...);
	}
};

class TopLevelScript : public Script
{
public:
	TopLevelScript(M64& m64, Game* game) : Script(nullptr), _m64(m64)
	{
		this->game = game;
	}

	template <std::derived_from<TopLevelScript> TTopLevelScript, std::derived_from<Game> TGame, typename... Ts>
		requires(std::constructible_from<TGame, Ts...>)
	static ScriptStatus<TTopLevelScript> Main(M64& m64, Ts&&... params)
	{
		TGame game = TGame(std::forward<Ts>(params)...);

		TTopLevelScript script = TTopLevelScript(m64, &game);

		if (script.checkPreconditions() && script.execute())
			script.checkPostconditions();

		return ScriptStatus<TTopLevelScript>(
			script.BaseStatus[0], script.CustomStatus);
	}

	virtual bool validation() override = 0;
	virtual bool execution() override = 0;
	virtual bool assertion() override = 0;

	Inputs GetInputs(int64_t frame) override;

protected:
	M64& _m64;

private:
	Inputs GetInputsTracked(uint64_t frame, uint64_t& counter) override;
	uint64_t GetFrameCounter(int64_t frame) override;
};
