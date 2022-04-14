#pragma once
#ifndef SCRIPT_H
#error "Script.t.hpp should only be included by Script.hpp"
#else

template <std::derived_from<Script> TScript, typename... Us>
	requires(std::constructible_from<TScript, Script*, Us...>)
ScriptStatus<TScript> Script::Execute(Us&&... params)
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
ScriptStatus<TScript> Script::Modify(Us&&... params)
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
ScriptStatus<TScript> Script::Test(Us&&... params)
{
	ScriptStatus<TScript> status =
		Execute<TScript>(std::forward<Us>(params)...);

	status.m64Diff = M64Diff();

	return status;
}

AdhocBaseScriptStatus Script::ExecuteAdhoc(AdhocScript auto adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	BaseScriptStatus status = ExecuteAdhocBase(adhocScript);
	Revert(initialFrame, status.m64Diff);

	return AdhocBaseScriptStatus(status);
}

template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script::ExecuteAdhoc(F adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
	BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });
	Revert(initialFrame, baseStatus.m64Diff);

	return AdhocScriptStatus<TAdhocCustomScriptStatus>(baseStatus, customStatus);
}

AdhocBaseScriptStatus Script::ModifyAdhoc(AdhocScript auto adhocScript)
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
AdhocScriptStatus<TAdhocCustomScriptStatus> Script::ModifyAdhoc(F adhocScript)
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

template <AdhocScript TAdhocScript>
AdhocBaseScriptStatus Script::TestAdhoc(TAdhocScript&& adhocScript)
{
	auto status = ExecuteAdhoc(std::forward<TAdhocScript>(adhocScript));
	status.m64Diff = M64Diff();

	return status;
}

template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script::TestAdhoc(F&& adhocScript)
{
	auto status = ExecuteAdhoc<TAdhocCustomScriptStatus>(std::forward<F>(adhocScript));
	status.m64Diff = M64Diff();

	return status;
}

//Root method for Compare() template recursion. This will be called when there are no scripts left to compare the incumbent to.
template <class TCompareStatus>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script::Compare(const AdhocScriptStatus<TCompareStatus>& status1)
{
	return status1;
}

//Main recursive method for comparing ad hoc scripts. Only the root and leaf use different methods
template <class TCompareStatus, AdhocCustomStatusScript<TCompareStatus> F, AdhocCustomStatusScript<TCompareStatus>... G>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script::Compare(const AdhocScriptStatus<TCompareStatus>& status1, F&& adhocScript2, G&&... adhocScripts)
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

//Leaf method for comparing ad-hoc scripts. This is the one the user will call.
template <class TCompareStatus,
	AdhocCustomStatusScript<TCompareStatus> F,
	AdhocCustomStatusScript<TCompareStatus> G,
	AdhocCustomStatusScript<TCompareStatus>... H>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script::Compare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts)
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

//Root method for ModifyCompare() template recursion. This will be called when there are no scripts left to compare the incumbent to.
template <class TCompareStatus>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script::ModifyCompare(int64_t initialFrame, const AdhocScriptStatus<TCompareStatus>& status1)
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
AdhocScriptStatus<TCompareStatus> Script::ModifyCompare(int64_t initialFrame, const AdhocScriptStatus<TCompareStatus>& status1, F&& adhocScript2, G&&... adhocScripts)
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

//Leaf method for comparing ad-hoc scripts and applying the result. This is the one the user will call.
template <class TCompareStatus,
	AdhocCustomStatusScript<TCompareStatus> F,
	AdhocCustomStatusScript<TCompareStatus> G,
	AdhocCustomStatusScript<TCompareStatus>... H>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script::ModifyCompare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts)
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
AdhocScriptStatus<TCompareStatus> Script::TestCompare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts)
{
	auto status = Compare<TCompareStatus>(std::forward<F>(adhocScript1), std::forward<G>(adhocScript2), std::forward<H>(adhocScripts)...);
	status.m64Diff = M64Diff();

	return status;
}

template <typename F>
BaseScriptStatus Script::ExecuteAdhocBase(F adhocScript)
{
	//Save state if performant
	OptionalSave();

	//Increment adhoc level
	_adhocLevel++;
	BaseStatus.emplace_back();
	saveBank.emplace_back();
	frameCounter.emplace_back();
	saveCache.emplace_back();
	inputsCache.emplace_back();

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
	saveCache.pop_back();
	inputsCache.pop_back();
	_adhocLevel--;

	BaseStatus[_adhocLevel].nLoads += status.nLoads;
	BaseStatus[_adhocLevel].nSaves += status.nSaves;
	BaseStatus[_adhocLevel].nFrameAdvances += status.nFrameAdvances;

	return status;
}

//Only to be used by ModifyCompare(). Will desync if used alone, as it assumes the caller will call Revert().
template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script::ExecuteAdhocNoRevert(F adhocScript)
{
	// Save state if performant
	OptionalSave();

	TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
	BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });

	return AdhocScriptStatus<TAdhocCustomScriptStatus>(baseStatus, customStatus);
}

template <std::derived_from<TopLevelScript> TTopLevelScript, std::derived_from<Game> TGame, typename... Ts>
	requires(std::constructible_from<TGame, Ts...>)
ScriptStatus<TTopLevelScript> TopLevelScript::Main(M64& m64, Ts&&... params)
{
	TGame game = TGame(std::forward<Ts>(params)...);

	TTopLevelScript script = TTopLevelScript(m64, &game);

	if (script.checkPreconditions() && script.execute())
		script.checkPostconditions();

	return ScriptStatus<TTopLevelScript>(
		script.BaseStatus[0], script.CustomStatus);
}

#endif