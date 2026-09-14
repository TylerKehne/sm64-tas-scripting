#pragma once
#include <cstdint>
#include <utility>

// Friend of Scattershot (declared in Scattershot.hpp). Lets benchmarks and tests call the private
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

	// The run's totals (Scattershot prints them at the end; the mock scattershot test reads them).
	template <class TScattershot>
	static uint64_t TotalShots(const TScattershot& scattershot)
	{
		return scattershot.TotalShots;
	}

	template <class TScattershot>
	static uint64_t ScriptCount(const TScattershot& scattershot)
	{
		return scattershot.ScriptCount;
	}

	template <class TScattershot>
	static uint64_t ValidationFailures(const TScattershot& scattershot)
	{
		return scattershot.ValidationFailures;
	}
};
