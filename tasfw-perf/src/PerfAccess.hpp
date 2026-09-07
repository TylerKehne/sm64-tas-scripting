#pragma once
#include <utility>

// Friend of Scattershot (declared in Scattershot.hpp). Lets benchmarks call the private
// hashing and block-table methods directly without changing their visibility.
class PerfAccess
{
public:
	template <class TScattershot, class T>
	static uint64_t GetHash(TScattershot& scattershot, const T& value, bool ignoreFillerBytes)
	{
		return scattershot.GetHash(value, ignoreFillerBytes);
	}

	template <class TScattershot, typename... Args>
	static bool UpsertBlock(TScattershot& scattershot, Args&&... args)
	{
		return scattershot.UpsertBlock(std::forward<Args>(args)...);
	}

	template <class TScattershot>
	static std::size_t BlockCount(TScattershot& scattershot)
	{
		return scattershot.Blocks.size();
	}
};
