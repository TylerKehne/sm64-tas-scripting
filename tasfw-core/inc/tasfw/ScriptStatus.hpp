#pragma once
#include "tasfw/Resource.hpp"
#include <tasfw/Inputs.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

#ifndef SCRIPTSTATUS_H
#define SCRIPTSTATUS_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

template <typename F, typename R = std::invoke_result_t<F>>
concept AdhocScript = std::same_as<R, bool>;

template <typename F, typename TStatus>
concept AdhocCustomStatusScript = std::same_as<std::invoke_result_t<F, TStatus*>, bool>;

class BaseScriptStatus
{
public:
	bool validated = false;
	bool executed = false;
	bool asserted = false;
	uint64_t validationDuration = 0;
	uint64_t executionDuration = 0;
	uint64_t assertionDuration = 0;
	uint64_t totalDuration = 0;
	uint64_t saveDuration = 0;
	uint64_t loadDuration = 0;
	uint64_t advanceFrameDuration = 0;
	uint64_t nLoads = 0;
	uint64_t nSaves = 0;
	uint64_t nFrameAdvances = 0;
	M64Diff m64Diff = M64Diff();

	BaseScriptStatus() = default;

	// Back to the freshly constructed state without releasing the diff's storage. Script
	// keeps one of these per ad-hoc level and LevelStack calls this when a level is popped,
	// so the next push reuses the map instead of constructing a new one.
	void Reset()
	{
		validated = executed = asserted = false;
		validationDuration = executionDuration = assertionDuration = totalDuration = 0;
		saveDuration = loadDuration = advanceFrameDuration = 0;
		nLoads = nSaves = nFrameAdvances = 0;
		m64Diff.frames.clear();
	}
};

template <derived_from_specialization_of<Script> TScript>
class ScriptStatus : public BaseScriptStatus, public TScript::CustomScriptStatus
{
public:
	ScriptStatus() : BaseScriptStatus(), TScript::CustomScriptStatus() {}

	// Forwarding so that a finished script's status and CustomStatus are moved into the
	// result rather than copied (CustomStatus holds vectors in the real trackers).
	template <class TBase, class TCustom>
		requires(std::derived_from<std::remove_cvref_t<TBase>, BaseScriptStatus>
			&& std::same_as<std::remove_cvref_t<TCustom>, typename TScript::CustomScriptStatus>)
	ScriptStatus(TBase&& baseStatus, TCustom&& customStatus)
		: BaseScriptStatus(std::forward<TBase>(baseStatus)), TScript::CustomScriptStatus(std::forward<TCustom>(customStatus)) { }
};

class AdhocBaseScriptStatus
{
public:
	bool executed = false;
	uint64_t totalDuration = 0;
	uint64_t saveDuration = 0;
	uint64_t loadDuration = 0;
	uint64_t advanceFrameDuration = 0;
	uint64_t nLoads = 0;
	uint64_t nSaves = 0;
	uint64_t nFrameAdvances = 0;
	M64Diff m64Diff = M64Diff();

	AdhocBaseScriptStatus() = default;

	// Forwarding: an rvalue BaseScriptStatus gives up its diff instead of copying it.
	template <class TBase>
		requires std::derived_from<std::remove_cvref_t<TBase>, BaseScriptStatus>
	AdhocBaseScriptStatus(TBase&& baseStatus)
	{
		executed = baseStatus.executed;
		nLoads = baseStatus.nLoads;
		nSaves = baseStatus.nSaves;
		nFrameAdvances = baseStatus.nFrameAdvances;
		m64Diff = std::forward<TBase>(baseStatus).m64Diff;
		totalDuration = baseStatus.totalDuration;
		saveDuration = baseStatus.saveDuration;
		loadDuration = baseStatus.loadDuration;
		advanceFrameDuration = baseStatus.advanceFrameDuration;
	}
};

template <class TAdhocCustomScriptStatus>
class AdhocScriptStatus : public AdhocBaseScriptStatus, public TAdhocCustomScriptStatus
{
public:
	AdhocScriptStatus() : AdhocBaseScriptStatus(), TAdhocCustomScriptStatus() {}

	template <class TBase, class TCustom>
		requires(std::constructible_from<AdhocBaseScriptStatus, TBase&&>
			&& std::same_as<std::remove_cvref_t<TCustom>, TAdhocCustomScriptStatus>)
	AdhocScriptStatus(TBase&& baseStatus, TCustom&& customStatus)
		: AdhocBaseScriptStatus(std::forward<TBase>(baseStatus)), TAdhocCustomScriptStatus(std::forward<TCustom>(customStatus)) { }
};

template <derived_from_specialization_of<Script> TScript>
class StatusField
{
public:
	ScriptStatus<TScript> status;

	StatusField() = default;
};

class TestAdhocStatus
{
public:
	float maxSpeed = -1.0f;
};

#endif
