#pragma once
#ifndef SCATTERSHOT_H
#error "ScattershotBuilder.hpp is included by Scattershot.hpp, which it completes"
#else
#ifndef SCATTERSHOTBUILDER_H
#define SCATTERSHOTBUILDER_H

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TMetricScript,
    class TOutputState,
    typename... TMetricScriptParams>
class ScattershotBuilder
{
public:
    ScattershotBuilder(const Configuration& config, const std::vector<ScattershotSolution<TOutputState>>* inputSolutions)
        : _config(config), _inputSolutions(inputSolutions) // Ignore warning, we want to leave callback uninitialized so it fails to compile if it's not
    {
        _metricScriptParams = std::make_shared<std::tuple<>>();
    }

    ScattershotBuilder(const Configuration& config, const std::vector<ScattershotSolution<TOutputState>>* inputSolutions,
        std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams, std::optional<Visualization> visualization = std::nullopt)
        : _config(config), _inputSolutions(inputSolutions), _metricScriptParams(metricScriptParams), _visualization(std::move(visualization))  // Ignore warning, we want to leave callback uninitialized so it fails to compile if it's not
    { }

    template <class TResourceConfig, typename FResourceConfigGenerator>
        requires (std::same_as<std::invoke_result_t<FResourceConfigGenerator, int>, TResourceConfig>)
    ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, TResourceConfig, FResourceConfigGenerator, TMetricScriptParams...> ConfigureResourcePerThread(FResourceConfigGenerator callback)
    {
        return ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, TResourceConfig, FResourceConfigGenerator, TMetricScriptParams...>(
            _config, callback, _inputSolutions, _metricScriptParams, _visualization);
    }

    template <typename FResourceImportGenerator>
        requires (std::same_as<std::invoke_result_t<FResourceImportGenerator, int>, TResource*>)
    ScattershotBuilderImport<TState, TResource, TMetricScript, TOutputState, FResourceImportGenerator, TMetricScriptParams...> ImportResourcePerThread(FResourceImportGenerator callback)
    {
        return ScattershotBuilderImport<TState, TResource, TMetricScript, TOutputState, FResourceImportGenerator, TMetricScriptParams...>(
            _config, callback, _inputSolutions, _metricScriptParams, _visualization);
    }

    ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...> PipeFrom(const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    {
        return ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>(_config, &inputSolutions, _metricScriptParams, _visualization);
    }

    template <typename... UMetricScriptParams>
    ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, UMetricScriptParams...> ConfigureMetricScript(UMetricScriptParams&&... metricScriptParams)
    {
        return ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, UMetricScriptParams...>(
            _config, _inputSolutions, std::make_shared<std::tuple<UMetricScriptParams...>>(std::forward<UMetricScriptParams>(metricScriptParams)...), _visualization);
    }

    // The run's viewer (ROADMAP 4.4): launched when the run's CSV opens with the CSV's path and
    // these parameters (Visualization.hpp). Nothing without a CSV, or with nullopt.
    ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...> Visualize(std::optional<Visualization> visualization)
    {
        return ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>(_config, _inputSolutions, _metricScriptParams, std::move(visualization));
    }

protected:
    const Configuration& _config;
    const std::vector<ScattershotSolution<TOutputState>>* _inputSolutions;
    std::shared_ptr<std::tuple<TMetricScriptParams...>> _metricScriptParams = nullptr;
    std::optional<Visualization> _visualization;
};

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TMetricScript,
    class TOutputState,
    class TResourceConfig,
    typename FResourceConfigGenerator,
    typename... TMetricScriptParams>
class ScattershotBuilderConfig : public ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>
{
public:
    using ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>::_config;
    using ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>::_inputSolutions;
    using ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>::_metricScriptParams;
    using ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>::_visualization;

    ScattershotBuilderConfig(const Configuration& config, FResourceConfigGenerator callback,
        const std::vector<ScattershotSolution<TOutputState>>* inputSolutions, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams,
        std::optional<Visualization> visualization = std::nullopt)
        : ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>(
            config, inputSolutions, metricScriptParams, std::move(visualization)), _resourceConfigGenerator(callback) {}

    template <class UResourceConfig, typename GResourceConfigGenerator>
        requires (std::same_as<std::invoke_result_t<GResourceConfigGenerator, int>, UResourceConfig>)
    ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, UResourceConfig, GResourceConfigGenerator, TMetricScriptParams...>
        ConfigureResourcePerThread(GResourceConfigGenerator callback)
    {
        return ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, UResourceConfig, GResourceConfigGenerator, TMetricScriptParams...>(
            _config, callback, _inputSolutions, _metricScriptParams, _visualization);
    }

    ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, TResourceConfig, FResourceConfigGenerator, TMetricScriptParams...>
        PipeFrom(const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    {
        return ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, TResourceConfig, FResourceConfigGenerator, TMetricScriptParams...>(
            _config, _resourceConfigGenerator, &inputSolutions, _metricScriptParams, _visualization);
    }

    template <typename... UMetricScriptParams>
    ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, TResourceConfig, FResourceConfigGenerator, UMetricScriptParams...>
        ConfigureMetricScript(UMetricScriptParams&&... metricScriptParams)
    {
        std::shared_ptr<std::tuple<UMetricScriptParams...>> tuplePtr =
            std::make_shared<std::tuple<UMetricScriptParams...>>(std::forward<UMetricScriptParams>(metricScriptParams)...);
        return ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, TResourceConfig, FResourceConfigGenerator, UMetricScriptParams...>(
            _config, _resourceConfigGenerator, _inputSolutions, tuplePtr, _visualization);
    }

    ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, TResourceConfig, FResourceConfigGenerator, TMetricScriptParams...>
        Visualize(std::optional<Visualization> visualization)
    {
        return ScattershotBuilderConfig<TState, TResource, TMetricScript, TOutputState, TResourceConfig, FResourceConfigGenerator, TMetricScriptParams...>(
            _config, _resourceConfigGenerator, _inputSolutions, _metricScriptParams, std::move(visualization));
    }

    template <std::derived_from<ScattershotThread<TState, TResource, TMetricScript, TOutputState>> TScattershotThread, typename... TParams>
    std::vector<ScattershotSolution<TOutputState>> Run(TParams&&... params)
    {
        return Scattershot<TState, TResource, TMetricScript, TOutputState>::template RunConfig<TScattershotThread, TResourceConfig>(
            _config, _inputSolutions ? *_inputSolutions : std::vector<ScattershotSolution<TOutputState>>(),
            _resourceConfigGenerator, _metricScriptParams, _visualization, std::forward<TParams>(params)...);
    }

private:
    FResourceConfigGenerator _resourceConfigGenerator;
};

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TMetricScript,
    class TOutputState,
    typename FResourceImportGenerator,
    typename... TMetricScriptParams>
class ScattershotBuilderImport : public ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>
{
public:
    using ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>::_config;
    using ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>::_inputSolutions;
    using ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>::_metricScriptParams;
    using ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>::_visualization;

    ScattershotBuilderImport(const Configuration& config, FResourceImportGenerator callback,
        const std::vector<ScattershotSolution<TOutputState>>* inputSolutions, std::shared_ptr<std::tuple<TMetricScriptParams...>> metricScriptParams,
        std::optional<Visualization> visualization = std::nullopt)
        : ScattershotBuilder<TState, TResource, TMetricScript, TOutputState, TMetricScriptParams...>(
            config, inputSolutions, metricScriptParams, std::move(visualization)), _resourceImportGenerator(callback) {}

    ScattershotBuilderImport<TState, TResource, TMetricScript, TOutputState, FResourceImportGenerator, TMetricScriptParams...>
        PipeFrom(const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    {
        return ScattershotBuilderImport<TState, TResource, TMetricScript, TOutputState, FResourceImportGenerator, TMetricScriptParams...>(
            _config, _resourceImportGenerator, &inputSolutions, _metricScriptParams, _visualization);
    }

    template <typename... UMetricScriptParams>
    ScattershotBuilderImport<TState, TResource, TMetricScript, TOutputState, FResourceImportGenerator, UMetricScriptParams...>
        ConfigureMetricScript(UMetricScriptParams&&... metricScriptParams)
    {
        std::shared_ptr<std::tuple<UMetricScriptParams...>> tuplePtr =
            std::make_shared<std::tuple<UMetricScriptParams...>>(std::forward<UMetricScriptParams>(metricScriptParams)...);
        return ScattershotBuilderImport<TState, TResource, TMetricScript, TOutputState, FResourceImportGenerator, UMetricScriptParams...>(
            _config, _resourceImportGenerator, _inputSolutions, tuplePtr, _visualization);
    }

    ScattershotBuilderImport<TState, TResource, TMetricScript, TOutputState, FResourceImportGenerator, TMetricScriptParams...>
        Visualize(std::optional<Visualization> visualization)
    {
        return ScattershotBuilderImport<TState, TResource, TMetricScript, TOutputState, FResourceImportGenerator, TMetricScriptParams...>(
            _config, _resourceImportGenerator, _inputSolutions, _metricScriptParams, std::move(visualization));
    }

    template <std::derived_from<ScattershotThread<TState, TResource, TMetricScript, TOutputState>> TScattershotThread, typename... TParams>
    std::vector<ScattershotSolution<TOutputState>> Run(TParams&&... params)
    {
        return Scattershot<TState, TResource, TMetricScript, TOutputState>::template RunImport<TScattershotThread>(
            _config, _inputSolutions ? *_inputSolutions : std::vector<ScattershotSolution<TOutputState>>(),
            _resourceImportGenerator, _metricScriptParams, _visualization, std::forward<TParams>(params)...);
    }

private:
    FResourceImportGenerator _resourceImportGenerator;
};

#endif
#endif
