// The compare family of Script: the 32 entry points of Compare, ModifyCompare, DynamicCompare,
// DynamicModifyCompare and their ad-hoc forms, each forwarding to ScriptCompareHelper
// (ScriptCompareHelper.hpp, which also declares the concepts they take). Included inside the
// class body of Script by Script.hpp and nowhere else: a constrained member template has to be
// defined in its class (docs/compilers.md, MSVC), so the family cannot live in Script.t.hpp,
// and this keeps Script.hpp readable. Tabs and access are the class body's; a .hpp rather
// than a .inc so the editors treat it as C++.
#pragma once
#ifndef SCRIPT_H
#error "Script.compare.hpp is included inside the class body of Script by Script.hpp"
#endif

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F,
		ScriptTerminator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(const TTupleContainer& paramsList, F&& comparator, G&& terminator)
	{
		return compareHelper.template Compare<TScript>(paramsList, std::forward<F>(comparator), std::forward<G>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F>
	requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(const TTupleContainer& paramsList, F&& comparator)
	{
		return compareHelper.template Compare<TScript>(paramsList, std::forward<F>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(F&& paramsGenerator, G&& comparator, H&& terminator)
	{
		return compareHelper.template Compare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> Compare(F&& paramsGenerator, G&& comparator)
	{
		return compareHelper.template Compare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F,
		ScriptTerminator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(const TTupleContainer& paramsList, F&& comparator, G&& terminator)
	{
		return compareHelper.template ModifyCompare<TScript>(paramsList, std::forward<F>(comparator), std::forward<G>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		ScriptComparator<TScript> F>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(const TTupleContainer& paramsList, F&& comparator)
	{
		return compareHelper.template ModifyCompare<TScript>(paramsList, std::forward<F>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(F&& paramsGenerator, G&& comparator, H&& terminator)
	{
		return compareHelper.template ModifyCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		ScriptComparator<TScript> G>
	requires (constructible_from_tuple<TScript, TTuple>)
	ScriptStatus<TScript> ModifyCompare(F&& paramsGenerator, G&& comparator)
	{
		return compareHelper.template ModifyCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator, H&& terminator)
	{
		return compareHelper.template DynamicCompare<TScript>(paramsList, std::forward<F>(mutator), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator)
	{
		return compareHelper.template DynamicCompare<TScript>(paramsList, std::forward<F>(mutator), std::forward<G>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H,
		ScriptTerminator<TScript> I>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(F&& paramsGenerator, G&& mutator, H&& comparator, I&& terminator)
	{
		return compareHelper.template DynamicCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(mutator), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicCompare(F&& paramsGenerator, G&& mutator, H&& comparator)
	{
		return compareHelper.template DynamicCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(mutator), std::forward<H>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G,
		ScriptTerminator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator, H&& terminator)
	{
		return compareHelper.template DynamicModifyCompare<TScript>(paramsList, std::forward<F>(mutator), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocScript F,
		ScriptComparator<TScript> G>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(const TTupleContainer& paramsList, F&& mutator, G&& comparator)
	{
		return compareHelper.template DynamicModifyCompare<TScript>(paramsList, std::forward<F>(mutator), std::forward<G>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H,
		ScriptTerminator<TScript> I>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(F&& paramsGenerator, G&& mutator, H&& comparator, I&& terminator)
	{
		return compareHelper.template DynamicModifyCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(mutator), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <derived_from_specialization_of<Script> TScript,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocScript G,
		ScriptComparator<TScript> H>
		requires (constructible_from_tuple<TScript, TTuple>)
	AdhocScriptStatus<Substatus<TScript>> DynamicModifyCompare(F&& paramsGenerator, G&& mutator, H&& comparator)
	{
		return compareHelper.template DynamicModifyCompare<TScript, TTuple>(std::forward<F>(paramsGenerator), std::forward<G>(mutator), std::forward<H>(comparator), [](const ScriptStatus<TScript>*) { return false; });
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G,
		AdhocScriptTerminator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator, H&& terminator)
	{
		return compareHelper.template CompareAdhoc<TCompareStatus>(paramsList, std::forward<F>(adhocScript), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator)
	{
		return compareHelper.template CompareAdhoc<TCompareStatus>(paramsList, std::forward<F>(adhocScript), std::forward<G>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator, I&& terminator)
	{
		return compareHelper.template CompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> CompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator)
	{
		return compareHelper.template CompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G,
		AdhocScriptTerminator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator, H&& terminator)
	{
		return compareHelper.template ModifyCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(comparator), std::forward<H>(terminator));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScriptComparator<TCompareStatus> G>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& comparator)
	{
		return compareHelper.template ModifyCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator, I&& terminator)
	{
		return compareHelper.template ModifyCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScriptComparator<TCompareStatus> H>
	AdhocScriptStatus<TCompareStatus> ModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& comparator)
	{
		return compareHelper.template ModifyCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator, I&& terminator)
	{
		return compareHelper.template DynamicCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(mutator), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator)
	{
		return compareHelper.template DynamicCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(mutator), std::forward<H>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I,
		AdhocScriptTerminator<TCompareStatus> J>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator, J&& terminator)
	{
		return compareHelper.template DynamicCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(mutator), std::forward<I>(comparator), std::forward<J>(terminator));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator)
	{
		return compareHelper.template DynamicCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(mutator), std::forward<I>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H,
		AdhocScriptTerminator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator, I&& terminator)
	{
		return compareHelper.template DynamicModifyCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(mutator), std::forward<H>(comparator), std::forward<I>(terminator));
	}

	template <class TCompareStatus,
		class TTupleContainer,
		typename TTuple = typename TTupleContainer::value_type,
		AdhocCompareScript<TCompareStatus, TTuple> F,
		AdhocScript G,
		AdhocScriptComparator<TCompareStatus> H>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(const TTupleContainer& paramsList, F&& adhocScript, G&& mutator, H&& comparator)
	{
		return compareHelper.template DynamicModifyCompareAdhoc<TCompareStatus>(
			paramsList, std::forward<F>(adhocScript), std::forward<G>(mutator), std::forward<H>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I,
		AdhocScriptTerminator<TCompareStatus> J>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator, J&& terminator)
	{
		return compareHelper.template DynamicModifyCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(mutator), std::forward<I>(comparator), std::forward<J>(terminator));
	}

	template <class TCompareStatus,
		typename TTuple,
		ScriptParamsGenerator<TTuple> F,
		AdhocCompareScript<TCompareStatus, TTuple> G,
		AdhocScript H,
		AdhocScriptComparator<TCompareStatus> I>
	AdhocScriptStatus<AdhocSubstatus<TCompareStatus>> DynamicModifyCompareAdhoc(F&& paramsGenerator, G&& adhocScript, H&& mutator, I&& comparator)
	{
		return compareHelper.template DynamicModifyCompareAdhoc<TCompareStatus, TTuple>(
			std::forward<F>(paramsGenerator), std::forward<G>(adhocScript), std::forward<H>(mutator), std::forward<I>(comparator), [](const AdhocScriptStatus<TCompareStatus>*) { return false; });
	}
