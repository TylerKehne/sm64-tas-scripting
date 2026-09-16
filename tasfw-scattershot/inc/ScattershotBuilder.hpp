#pragma once
#ifndef SCATTERSHOT_H
#error "ScattershotBuilder.hpp is included by Scattershot.hpp, which it completes"
#else
#ifndef SCATTERSHOTBUILDER_H
#define SCATTERSHOTBUILDER_H

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    typename... TStateTrackerParams>
class ScattershotBuilder
{
public:
    ScattershotBuilder(const Configuration& config, const std::vector<ScattershotSolution<TOutputState>>* inputSolutions)
        : _config(config), _inputSolutions(inputSolutions) // Ignore warning, we want to leave callback uninitialized so it fails to compile if it's not
    {
        _stateTrackerParams = std::make_shared<std::tuple<>>();
    } 

    ScattershotBuilder(const Configuration& config, const std::vector<ScattershotSolution<TOutputState>>* inputSolutions, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
        : _config(config), _inputSolutions(inputSolutions), _stateTrackerParams(stateTrackerParams)  // Ignore warning, we want to leave callback uninitialized so it fails to compile if it's not
    { }

    template <class TResourceConfig, typename FResourceConfigGenerator>
        requires (std::same_as<std::invoke_result_t<FResourceConfigGenerator, int>, TResourceConfig>)
    ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, TStateTrackerParams...> ConfigureResourcePerThread(FResourceConfigGenerator callback)
    {
        return ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, TStateTrackerParams...>(_config, callback, _inputSolutions, _stateTrackerParams);
    }

    template <typename FResourceImportGenerator>
        requires (std::same_as<std::invoke_result_t<FResourceImportGenerator, int>, TResource*>)
    ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, TStateTrackerParams...> ImportResourcePerThread(FResourceImportGenerator callback)
    {
        return ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, TStateTrackerParams...>(_config, callback, _inputSolutions, _stateTrackerParams);
    }

    ScattershotBuilder<TState, TResource, TStateTracker, TOutputState> PipeFrom(const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    {
        return ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>(_config, &inputSolutions, _stateTrackerParams);
    }

    template <typename... UStateTrackerParams>
    ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, UStateTrackerParams...> ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
    {
        return ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, UStateTrackerParams...>(
            _config, _inputSolutions, std::make_shared(std::make_tuple(std::forward<UStateTrackerParams>(stateTrackerParams)...)));
    }

protected:
    const Configuration& _config;
    const std::vector<ScattershotSolution<TOutputState>>* _inputSolutions;
    std::shared_ptr<std::tuple<TStateTrackerParams...>> _stateTrackerParams = nullptr;
};

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    class TResourceConfig,
    typename FResourceConfigGenerator,
    typename... TStateTrackerParams>
class ScattershotBuilderConfig : public ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>
{
public:
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_config;
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_inputSolutions;
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_stateTrackerParams;

    ScattershotBuilderConfig(const Configuration& config, FResourceConfigGenerator callback,
        const std::vector<ScattershotSolution<TOutputState>>* inputSolutions, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
        : ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>(
            config, inputSolutions, stateTrackerParams), _resourceConfigGenerator(callback) {}

    template <class UResourceConfig, typename GResourceConfigGenerator>
        requires (std::same_as<std::invoke_result_t<GResourceConfigGenerator, int>, UResourceConfig>)
    ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, UResourceConfig, GResourceConfigGenerator, TStateTrackerParams...>
        ConfigureResourcePerThread(GResourceConfigGenerator callback)
    {
        return ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, UResourceConfig, GResourceConfigGenerator, TStateTrackerParams...>(
            _config, callback, _inputSolutions, _stateTrackerParams);
    }

    ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, TStateTrackerParams...>
        PipeFrom(const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    {
        return ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, TStateTrackerParams...>(
            _config, _resourceConfigGenerator, &inputSolutions, _stateTrackerParams);
    }

    template <typename... UStateTrackerParams>
    ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, UStateTrackerParams...>
        ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
    {
        std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
            std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
        return ScattershotBuilderConfig<TState, TResource, TStateTracker, TOutputState, TResourceConfig, FResourceConfigGenerator, UStateTrackerParams...>(
            _config, _resourceConfigGenerator, _inputSolutions, tuplePtr);
    }

    template <std::derived_from<ScattershotThread<TState, TResource, TStateTracker, TOutputState>> TScattershotThread, typename... TParams>
    std::vector<ScattershotSolution<TOutputState>> Run(TParams&&... params)
    {
        return Scattershot<TState, TResource, TStateTracker, TOutputState>::template RunConfig<TScattershotThread, TResourceConfig>(
            _config, _inputSolutions ? *_inputSolutions : std::vector<ScattershotSolution<TOutputState>>(),
            _resourceConfigGenerator, _stateTrackerParams, std::forward<TParams>(params)...);
    }

private:
    FResourceConfigGenerator _resourceConfigGenerator;
};

template <class TState,
    derived_from_specialization_of<Resource> TResource,
    std::derived_from<Script<TResource>> TStateTracker,
    class TOutputState,
    typename FResourceImportGenerator,
    typename... TStateTrackerParams>
class ScattershotBuilderImport : public ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>
{
public:
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_config;
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_inputSolutions;
    using ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>::_stateTrackerParams;

    ScattershotBuilderImport(const Configuration& config, FResourceImportGenerator callback,
        const std::vector<ScattershotSolution<TOutputState>>* inputSolutions, std::shared_ptr<std::tuple<TStateTrackerParams...>> stateTrackerParams)
        : ScattershotBuilder<TState, TResource, TStateTracker, TOutputState, TStateTrackerParams...>(
            config, inputSolutions, stateTrackerParams), _resourceImportGenerator(callback) {}

    ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, TStateTrackerParams...>
        PipeFrom(const std::vector<ScattershotSolution<TOutputState>>& inputSolutions)
    {
        return ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator>(
            _config, _resourceImportGenerator, &inputSolutions, _stateTrackerParams);
    }

    template <typename... UStateTrackerParams>
    ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, UStateTrackerParams...>
        ConfigureStateTracker(UStateTrackerParams&&... stateTrackerParams)
    {
        std::shared_ptr<std::tuple<UStateTrackerParams...>> tuplePtr =
            std::make_shared<std::tuple<UStateTrackerParams...>>(std::forward<UStateTrackerParams>(stateTrackerParams)...);
        return ScattershotBuilderImport<TState, TResource, TStateTracker, TOutputState, FResourceImportGenerator, UStateTrackerParams...>(
            _config, _resourceImportGenerator, _inputSolutions, tuplePtr);
    }

    template <std::derived_from<ScattershotThread<TState, TResource, TStateTracker, TOutputState>> TScattershotThread, typename... TParams>
    std::vector<ScattershotSolution<TOutputState>> Run(TParams&&... params)
    {
        return Scattershot<TState, TResource, TStateTracker, TOutputState>::template RunImport<TScattershotThread>(
            _config, _inputSolutions ? *_inputSolutions : std::vector<ScattershotSolution<TOutputState>>(),
            _resourceImportGenerator, _stateTrackerParams, std::forward<TParams>(params)...);
    }

private:
    FResourceImportGenerator _resourceImportGenerator;
};

#endif
#endif
