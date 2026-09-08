#include "alloc_counter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

// Replacement global allocation functions that count calls. The unsized, sized and array
// forms are all replaced explicitly rather than relying on the library defaults forwarding
// to each other, because that forwarding is what the standard library is allowed to skip.
// The aligned (std::align_val_t) forms are left to the library; nothing in the framework
// over-aligns, and their defaults never call these functions.

namespace
{
	std::atomic<uint64_t> g_allocs { 0 };

	// Not fetch_add: a locked read-modify-write is a full fence on x86 and, issued right
	// after malloc has written heap metadata, it cost 10-20 ns per allocation in the suite
	// (+75% on the allocation-heavy benchmarks). The benchmarks are single-threaded, so a
	// relaxed load and store (two plain moves) is enough; a concurrent increment could be
	// lost, which would only ever under-count.
	inline void Count()
	{
		g_allocs.store(g_allocs.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
	}

	void* CountedAlloc(std::size_t size)
	{
		Count();
		if (size == 0)
			size = 1;
		void* p = std::malloc(size);
		if (!p)
			throw std::bad_alloc();
		return p;
	}
}

namespace tasfw_perf
{
	uint64_t AllocCount()
	{
		return g_allocs.load(std::memory_order_relaxed);
	}
}

void* operator new(std::size_t size) { return CountedAlloc(size); }
void* operator new[](std::size_t size) { return CountedAlloc(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	Count();
	return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
	Count();
	return std::malloc(size == 0 ? 1 : size);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
