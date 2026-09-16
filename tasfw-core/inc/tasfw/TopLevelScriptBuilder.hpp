#pragma once
#ifndef SCRIPT_H
#error "TopLevelScriptBuilder.hpp is included by Script.hpp, which it completes"
#else
#ifndef TOPLEVELSCRIPTBUILDER_H
#define TOPLEVELSCRIPTBUILDER_H

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TState,
	class TResourceConfig,
	typename... TStateTrackerParams>
class TopLevelScriptBuilderConfigured;

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TResource,
	typename... TStateTrackerParams>
class TopLevelScriptBuilderImported;

class DefaultState {};

class DefaultResourceConfig {};

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename... TStateTrackerParams>
class TopLevelScriptBuilder
{
public:
	TopLevelScriptBuilder(M64& m64) : _m64(m64) { _stateTrackerParams = std::make_shared<std::tuple<>>(); }
	TopLevelScriptBuilder(M64& m64, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
		: _m64(m64), _stateTrackerParams(stateTrackerParams) {}

	static TopLevelScriptBuilder<TTopLevelScript> Build(M64& m64)
	{
		return TopLevelScriptBuilder<TTopLevelScript>(m64);
	}

	template <typename... UStateTrackerParams>
	TopLevelScriptBuilder<TTopLevelScript> ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
	{
		std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
			std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
		return TopLevelScriptBuilder<TTopLevelScript, UStateTrackerParams...>(_m64, tuplePtr);
	}

	template <class TState, typename... TStateParams>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TStateTrackerParams...> ImportSave(
		uint64_t frame, TStateParams&&... stateParams)
	{
		ImportedSave<TState> importedSave = ImportedSave(TState(std::forward<TStateParams>(stateParams)...), frame);
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TStateTrackerParams...>(
			_m64, std::move(importedSave), DefaultResourceConfig(), _stateTrackerParams);
	}

	// A save a script exported (Script::ExportSave), state and frame together.
	template <class TState>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TStateTrackerParams...> ImportSave(ImportedSave<TState> save)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TStateTrackerParams...>(
			_m64, std::move(save), DefaultResourceConfig(), _stateTrackerParams);
	}

	template <typename TResourceConfig>
	TopLevelScriptBuilderConfigured<TTopLevelScript, DefaultState, TResourceConfig, TStateTrackerParams...> ConfigureResource(TResourceConfig resourceConfig)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, DefaultState, TResourceConfig, TStateTrackerParams...>(
			_m64, ImportedSave<DefaultState>(DefaultState(), -1), resourceConfig, _stateTrackerParams);
	}

	template <class TResource>
	TopLevelScriptBuilderImported<TTopLevelScript, TResource, TStateTrackerParams...> ImportResource(TResource* resource)
	{
		return TopLevelScriptBuilderImported<TTopLevelScript, TResource, TStateTrackerParams...>(_m64, resource, _stateTrackerParams);
	}

protected:
	M64& _m64;
	std::shared_ptr<std::tuple<TStateTrackerParams...>> _stateTrackerParams;
};

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TState = DefaultState,
	class TResourceConfig = DefaultResourceConfig,
	typename... TStateTrackerParams>
class TopLevelScriptBuilderConfigured : public TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>
{
public:
	using TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>::_m64;
	using TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>::_stateTrackerParams;

	TopLevelScriptBuilderConfigured(M64& m64, ImportedSave<TState> importedSave,
		TResourceConfig resourceConfig, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
		: TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>(m64, stateTrackerParams), _importedSave(std::move(importedSave)), _resourceConfig(resourceConfig) {}

	template <typename... UStateTrackerParams>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, TResourceConfig, UStateTrackerParams...> ConfigureStateTracker(
		TStateTrackerParams&&... stateTrackerParams)
	{
		std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
			std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, TResourceConfig, UStateTrackerParams...>(
			_m64, std::move(_importedSave), std::move(_resourceConfig), tuplePtr);
	}

	template <class UState, typename... TStateParams>
	TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TStateTrackerParams...> ImportSave(uint64_t frame, TStateParams&&... stateParams)
	{
		ImportedSave<UState> importedSave = ImportedSave(UState(std::forward<TStateParams>(stateParams)...), frame);
		return TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TStateTrackerParams...>(
			_m64, std::move(importedSave), std::move(_resourceConfig), _stateTrackerParams);
	}

	template <class UState>
	TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TStateTrackerParams...> ImportSave(ImportedSave<UState> save)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TStateTrackerParams...>(
			_m64, std::move(save), std::move(_resourceConfig), _stateTrackerParams);
	}

	template <typename UResourceConfig>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, UResourceConfig, TStateTrackerParams...> ConfigureResource(UResourceConfig resourceConfig)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, UResourceConfig, TStateTrackerParams...>(
			_m64, std::move(_importedSave), resourceConfig, _stateTrackerParams);
	}

	template <typename... TScriptParams>
	ScriptStatus<TTopLevelScript> Run(TScriptParams&&... scriptParams)
	{
		if constexpr (std::is_same<TState, DefaultState>::value)
		{
			if constexpr (std::is_same<TResourceConfig, DefaultResourceConfig>::value)
				return TTopLevelScript::template Main<TTopLevelScript>(
					_m64, _stateTrackerParams, std::forward<TScriptParams>(scriptParams)...);
			else
				return TTopLevelScript::template MainConfig<TTopLevelScript, TResourceConfig>(
					_m64, _stateTrackerParams, std::move(_resourceConfig), std::forward<TScriptParams>(scriptParams)...);
		}
		else if constexpr (std::is_same<TResourceConfig, DefaultResourceConfig>::value)
			return TTopLevelScript::template MainFromSave<TTopLevelScript, TState>(
				_m64, _stateTrackerParams, _importedSave, std::forward<TScriptParams>(scriptParams)...);
		else
			return TTopLevelScript::template MainFromSaveConfig<TTopLevelScript, TState, TResourceConfig>(
				_m64, _stateTrackerParams, _importedSave, std::move(_resourceConfig), std::forward<TScriptParams>(scriptParams)...);
	}

private:
	ImportedSave<TState> _importedSave { TState(), -1 };
	TResourceConfig _resourceConfig;
};

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TResource,
	typename... TStateTrackerParams>
class TopLevelScriptBuilderImported : public TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>
{
public:
	using TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>::_m64;
	using TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>::_stateTrackerParams;

	TopLevelScriptBuilderImported(M64& m64, TResource* resource, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
		: TopLevelScriptBuilder<TTopLevelScript, TStateTrackerParams...>(m64, stateTrackerParams), _resource(resource) {}

	template <typename... UStateTrackerParams>
	TopLevelScriptBuilderImported<TTopLevelScript, TResource, UStateTrackerParams...> ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
	{
		std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
			std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
		return TopLevelScriptBuilderImported<TTopLevelScript, TResource, UStateTrackerParams...>(
			_m64, _resource, tuplePtr);
	}

	template <typename... TScriptParams>
	ScriptStatus<TTopLevelScript> Run(TScriptParams&&... scriptParams)
	{
		return TTopLevelScript::template MainImport<TTopLevelScript>(
			_m64, _stateTrackerParams, _resource, std::forward<TScriptParams>(scriptParams)...);
	}

private:
	TResource* _resource;
};

#endif
#endif
