#pragma once
#include "tasfw/Resource.hpp"
#include <tasfw/Inputs.hpp>

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
	uint64_t nLoads = 0;
	uint64_t nSaves = 0;
	uint64_t nFrameAdvances = 0;
	M64Diff m64Diff = M64Diff();

	BaseScriptStatus() = default;
};

template <derived_from_specialization_of<Script> TScript>
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
	uint64_t executionDuration = 0;
	uint64_t nLoads = 0;
	uint64_t nSaves = 0;
	uint64_t nFrameAdvances = 0;
	M64Diff m64Diff = M64Diff();

	AdhocBaseScriptStatus() = default;

	AdhocBaseScriptStatus(BaseScriptStatus baseStatus)
	{
		executed = baseStatus.executed;
		executionDuration = baseStatus.executionDuration;
		nLoads = baseStatus.nLoads;
		nSaves = baseStatus.nSaves;
		nFrameAdvances = baseStatus.nFrameAdvances;
		m64Diff = baseStatus.m64Diff;
	}

	//AdhocBaseScriptStatus(const AdhocBaseScriptStatus&) = default;

	//AdhocBaseScriptStatus(AdhocBaseScriptStatus&&) = default;
};

template <class TAdhocCustomScriptStatus>
class AdhocScriptStatus : public AdhocBaseScriptStatus, public TAdhocCustomScriptStatus
{
public:
	AdhocScriptStatus() : AdhocBaseScriptStatus(), TAdhocCustomScriptStatus() {}

	AdhocScriptStatus(AdhocBaseScriptStatus baseStatus, TAdhocCustomScriptStatus customStatus)
		: AdhocBaseScriptStatus(baseStatus), TAdhocCustomScriptStatus(customStatus) { }

	//AdhocScriptStatus(AdhocScriptStatus&&) = default;
};

template <class TCompareStatus>
class CompareStatus
{
public:
	virtual const AdhocScriptStatus<TCompareStatus>& Comparator(
		const AdhocScriptStatus<TCompareStatus>& a,
		const AdhocScriptStatus<TCompareStatus>& b) const = 0;

	virtual bool Terminator([[maybe_unused]] const AdhocScriptStatus<TCompareStatus>& status) const { return false; }
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
