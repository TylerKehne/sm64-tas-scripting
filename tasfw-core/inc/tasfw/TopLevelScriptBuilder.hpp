#pragma once
#ifndef SCRIPT_H
#error "TopLevelScriptBuilder.hpp is included by Script.hpp, which it completes"
#else
#ifndef TOPLEVELSCRIPTBUILDER_H
#define TOPLEVELSCRIPTBUILDER_H

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TState,
	class TResourceConfig,
	typename... TMetricScriptParams>
class TopLevelScriptBuilderConfigured;

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TResource,
	typename... TMetricScriptParams>
class TopLevelScriptBuilderImported;

class DefaultState {};

class DefaultResourceConfig {};

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript, typename... TMetricScriptParams>
class TopLevelScriptBuilder
{
public:
	TopLevelScriptBuilder(M64& m64) : _m64(m64) { _metricScriptParams = std::make_shared<std::tuple<>>(); }
	TopLevelScriptBuilder(M64& m64, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams)
		: _m64(m64), _metricScriptParams(metricScriptParams) {}

	static TopLevelScriptBuilder<TTopLevelScript> Build(M64& m64)
	{
		return TopLevelScriptBuilder<TTopLevelScript>(m64);
	}

	template <typename... UMetricScriptParams>
	TopLevelScriptBuilder<TTopLevelScript> ConfigureMetricScript(UMetricScriptParams&&... metricScriptParams)
	{
		std::shared_ptr<std::tuple<UMetricScriptParams...>> tuplePtr =
			std::make_shared<std::tuple<UMetricScriptParams...>>(std::forward<UMetricScriptParams>(metricScriptParams)...);
		return TopLevelScriptBuilder<TTopLevelScript, UMetricScriptParams...>(_m64, tuplePtr);
	}

	template <class TState, typename... TStateParams>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TMetricScriptParams...> ImportSave(
		uint64_t frame, TStateParams&&... stateParams)
	{
		ImportedSave<TState> importedSave = ImportedSave(TState(std::forward<TStateParams>(stateParams)...), frame);
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TMetricScriptParams...>(
			_m64, std::move(importedSave), DefaultResourceConfig(), _metricScriptParams);
	}

	// A save a script exported (Script::ExportSave), state and frame together.
	template <class TState>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TMetricScriptParams...> ImportSave(ImportedSave<TState> save)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, DefaultResourceConfig, TMetricScriptParams...>(
			_m64, std::move(save), DefaultResourceConfig(), _metricScriptParams);
	}

	template <typename TResourceConfig>
	TopLevelScriptBuilderConfigured<TTopLevelScript, DefaultState, TResourceConfig, TMetricScriptParams...> ConfigureResource(TResourceConfig resourceConfig)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, DefaultState, TResourceConfig, TMetricScriptParams...>(
			_m64, ImportedSave<DefaultState>(DefaultState(), -1), resourceConfig, _metricScriptParams);
	}

	template <class TResource>
	TopLevelScriptBuilderImported<TTopLevelScript, TResource, TMetricScriptParams...> ImportResource(TResource* resource)
	{
		return TopLevelScriptBuilderImported<TTopLevelScript, TResource, TMetricScriptParams...>(_m64, resource, _metricScriptParams);
	}

protected:
	M64& _m64;
	std::shared_ptr<std::tuple<TMetricScriptParams...>> _metricScriptParams;
};

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TState = DefaultState,
	class TResourceConfig = DefaultResourceConfig,
	typename... TMetricScriptParams>
class TopLevelScriptBuilderConfigured : public TopLevelScriptBuilder<TTopLevelScript, TMetricScriptParams...>
{
public:
	using TopLevelScriptBuilder<TTopLevelScript, TMetricScriptParams...>::_m64;
	using TopLevelScriptBuilder<TTopLevelScript, TMetricScriptParams...>::_metricScriptParams;

	TopLevelScriptBuilderConfigured(M64& m64, ImportedSave<TState> importedSave,
		TResourceConfig resourceConfig, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams)
		: TopLevelScriptBuilder<TTopLevelScript, TMetricScriptParams...>(m64, metricScriptParams), _importedSave(std::move(importedSave)), _resourceConfig(resourceConfig) {}

	template <typename... UMetricScriptParams>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, TResourceConfig, UMetricScriptParams...> ConfigureMetricScript(
		TMetricScriptParams&&... metricScriptParams)
	{
		std::shared_ptr<std::tuple<UMetricScriptParams...>> tuplePtr =
			std::make_shared<std::tuple<UMetricScriptParams...>>(std::forward<UMetricScriptParams>(metricScriptParams)...);
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, TResourceConfig, UMetricScriptParams...>(
			_m64, std::move(_importedSave), std::move(_resourceConfig), tuplePtr);
	}

	template <class UState, typename... TStateParams>
	TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TMetricScriptParams...> ImportSave(uint64_t frame, TStateParams&&... stateParams)
	{
		ImportedSave<UState> importedSave = ImportedSave(UState(std::forward<TStateParams>(stateParams)...), frame);
		return TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TMetricScriptParams...>(
			_m64, std::move(importedSave), std::move(_resourceConfig), _metricScriptParams);
	}

	template <class UState>
	TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TMetricScriptParams...> ImportSave(ImportedSave<UState> save)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, UState, TResourceConfig, TMetricScriptParams...>(
			_m64, std::move(save), std::move(_resourceConfig), _metricScriptParams);
	}

	template <typename UResourceConfig>
	TopLevelScriptBuilderConfigured<TTopLevelScript, TState, UResourceConfig, TMetricScriptParams...> ConfigureResource(UResourceConfig resourceConfig)
	{
		return TopLevelScriptBuilderConfigured<TTopLevelScript, TState, UResourceConfig, TMetricScriptParams...>(
			_m64, std::move(_importedSave), resourceConfig, _metricScriptParams);
	}

	template <typename... TScriptParams>
	ScriptStatus<TTopLevelScript> Run(TScriptParams&&... scriptParams)
	{
		if constexpr (std::is_same<TState, DefaultState>::value)
		{
			if constexpr (std::is_same<TResourceConfig, DefaultResourceConfig>::value)
				return TTopLevelScript::template Main<TTopLevelScript>(
					_m64, _metricScriptParams, std::forward<TScriptParams>(scriptParams)...);
			else
				return TTopLevelScript::template MainConfig<TTopLevelScript, TResourceConfig>(
					_m64, _metricScriptParams, std::move(_resourceConfig), std::forward<TScriptParams>(scriptParams)...);
		}
		else if constexpr (std::is_same<TResourceConfig, DefaultResourceConfig>::value)
			return TTopLevelScript::template MainFromSave<TTopLevelScript, TState>(
				_m64, _metricScriptParams, _importedSave, std::forward<TScriptParams>(scriptParams)...);
		else
			return TTopLevelScript::template MainFromSaveConfig<TTopLevelScript, TState, TResourceConfig>(
				_m64, _metricScriptParams, _importedSave, std::move(_resourceConfig), std::forward<TScriptParams>(scriptParams)...);
	}

private:
	ImportedSave<TState> _importedSave { TState(), -1 };
	TResourceConfig _resourceConfig;
};

template <derived_from_specialization_of<TopLevelScript> TTopLevelScript,
	class TResource,
	typename... TMetricScriptParams>
class TopLevelScriptBuilderImported : public TopLevelScriptBuilder<TTopLevelScript, TMetricScriptParams...>
{
public:
	using TopLevelScriptBuilder<TTopLevelScript, TMetricScriptParams...>::_m64;
	using TopLevelScriptBuilder<TTopLevelScript, TMetricScriptParams...>::_metricScriptParams;

	TopLevelScriptBuilderImported(M64& m64, TResource* resource, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams)
		: TopLevelScriptBuilder<TTopLevelScript, TMetricScriptParams...>(m64, metricScriptParams), _resource(resource) {}

	template <typename... UMetricScriptParams>
	TopLevelScriptBuilderImported<TTopLevelScript, TResource, UMetricScriptParams...> ConfigureMetricScript(UMetricScriptParams&&... metricScriptParams)
	{
		std::shared_ptr<std::tuple<UMetricScriptParams...>> tuplePtr =
			std::make_shared<std::tuple<UMetricScriptParams...>>(std::forward<UMetricScriptParams>(metricScriptParams)...);
		return TopLevelScriptBuilderImported<TTopLevelScript, TResource, UMetricScriptParams...>(
			_m64, _resource, tuplePtr);
	}

	template <typename... TScriptParams>
	ScriptStatus<TTopLevelScript> Run(TScriptParams&&... scriptParams)
	{
		return TTopLevelScript::template MainImport<TTopLevelScript>(
			_m64, _metricScriptParams, _resource, std::forward<TScriptParams>(scriptParams)...);
	}

private:
	TResource* _resource;
};

#endif
#endif
