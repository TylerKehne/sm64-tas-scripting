#pragma once
#ifndef CONFIGURATION_H
#error "Configuration.t.hpp should only be included by Configuration.hpp"
#else

template <class TContainer, typename TElement>
    requires std::is_same_v<TElement, std::string>
void Configuration::SetResourcePaths(const TContainer& container)
{
    for (const std::string& item : container)
    {
        ResourcePaths.emplace_back(item);
    }
}

#endif
