#include "LibSm64.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sm64/Camera.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Types.hpp>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

const char* LibSm64SaveModeName(LibSm64SaveMode mode)
{
	switch (mode)
	{
	case LibSm64SaveMode::Full: return "full";
	case LibSm64SaveMode::Fixed: return "fixed";
	case LibSm64SaveMode::Dirty: return "dirty";
	}
	return "?";
}

bool ParseLibSm64SaveMode(const std::string& name, LibSm64SaveMode& mode)
{
	for (LibSm64SaveMode candidate : {LibSm64SaveMode::Full, LibSm64SaveMode::Fixed, LibSm64SaveMode::Dirty})
	{
		if (name == LibSm64SaveModeName(candidate))
		{
			mode = candidate;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Dirty mode's fault handler (LibSm64DirtyPages in the header). The handler is process-wide
// and must find the set that owns a faulting address without taking a lock, so the live sets
// sit in a fixed table of atomics: registration writes an entry and then publishes the
// count; destruction nulls the entry. Each LibSm64 is used by one thread and only that thread
// writes to its DLL copy, so a set's bitmap is only ever touched from the thread that faulted
// on it (or from that thread's own load()).

namespace
{
	constexpr int kMaxDirtyPageSets = 64;
	std::atomic<LibSm64DirtyPages*> gDirtyPageSets[kMaxDirtyPageSets];
	std::atomic<int> gDirtyPageSetCount {0};
	std::mutex gDirtyPageSetMutex;

	// Called from the fault handler. Returns true if a set owned the address.
	bool DispatchWrite(void* address)
	{
		int n = gDirtyPageSetCount.load(std::memory_order_acquire);
		for (int i = 0; i < n; i++)
		{
			LibSm64DirtyPages* d = gDirtyPageSets[i].load(std::memory_order_acquire);
			size_t index;
			if (d != nullptr && d->Contains(address, index))
			{
				d->OnWrite(index);
				return true;
			}
		}
		return false;
	}

	// Platform API forks (AGENTS.md: forks are for platform APIs only).
#if defined(_WIN32)
	bool SetProtection(void* begin, size_t bytes, bool writable)
	{
		DWORD old;
		return VirtualProtect(begin, bytes, writable ? PAGE_READWRITE : PAGE_READONLY, &old) != 0;
	}

	LONG CALLBACK WriteFaultHandler(PEXCEPTION_POINTERS info)
	{
		const EXCEPTION_RECORD* rec = info->ExceptionRecord;
		if (rec->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || rec->NumberParameters < 2 || rec->ExceptionInformation[0] != 1)
			return EXCEPTION_CONTINUE_SEARCH;
		return DispatchWrite(reinterpret_cast<void*>(rec->ExceptionInformation[1])) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
	}

	// Vectored handlers form a chain that nothing else replaces, so once is enough. A
	// first-chance handler that continues execution never reaches a debugger's or doctest's
	// unhandled-exception filter.
	void EnsureHandlerInstalled()
	{
		static std::once_flag installed;
		std::call_once(installed, []
		{
			if (AddVectoredExceptionHandler(1, WriteFaultHandler) == nullptr)
				throw std::runtime_error("AddVectoredExceptionHandler failed; Dirty save mode needs it");
		});
	}
#else
	bool SetProtection(void* begin, size_t bytes, bool writable)
	{
		return mprotect(begin, bytes, writable ? (PROT_READ | PROT_WRITE) : PROT_READ) == 0;
	}

	struct sigaction gPreviousSegv {};

	void WriteFaultHandler(int /*sig*/, siginfo_t* si, void* /*context*/)
	{
		if (DispatchWrite(si->si_addr))
			return;
		// Not ours: reinstate the previous disposition and return. The faulting instruction
		// re-executes and the fault is delivered to that handler, or to the default action.
		sigaction(SIGSEGV, &gPreviousSegv, nullptr);
	}

	// Checked at every construction, not once: doctest installs its own SIGSEGV handler
	// around each test case and restores what it found afterwards, which silently removes a
	// handler installed during a test case. Whatever is current when we are not becomes the
	// handler we chain to.
	void EnsureHandlerInstalled()
	{
		struct sigaction current {};
		if (sigaction(SIGSEGV, nullptr, &current) != 0)
			throw std::runtime_error("sigaction(SIGSEGV) query failed; Dirty save mode needs it");
		if ((current.sa_flags & SA_SIGINFO) != 0 && current.sa_sigaction == WriteFaultHandler)
			return;
		struct sigaction sa {};
		sa.sa_flags = SA_SIGINFO;
		sigemptyset(&sa.sa_mask);
		sa.sa_sigaction = WriteFaultHandler;
		if (sigaction(SIGSEGV, &sa, &gPreviousSegv) != 0)
			throw std::runtime_error("sigaction(SIGSEGV) failed; Dirty save mode needs it");
	}
#endif

	void RegisterDirtyPages(LibSm64DirtyPages* d)
	{
		std::lock_guard<std::mutex> lock(gDirtyPageSetMutex);
		EnsureHandlerInstalled();
		int n = gDirtyPageSetCount.load(std::memory_order_relaxed);
		for (int i = 0; i < n; i++)
		{
			if (gDirtyPageSets[i].load(std::memory_order_relaxed) == nullptr)
			{
				gDirtyPageSets[i].store(d, std::memory_order_release);
				return;
			}
		}
		if (n == kMaxDirtyPageSets)
			throw std::runtime_error("too many LibSm64 instances in Dirty save mode in one process");
		gDirtyPageSets[n].store(d, std::memory_order_release);
		gDirtyPageSetCount.store(n + 1, std::memory_order_release);
	}

	void UnregisterDirtyPages(LibSm64DirtyPages* d)
	{
		std::lock_guard<std::mutex> lock(gDirtyPageSetMutex);
		int n = gDirtyPageSetCount.load(std::memory_order_relaxed);
		for (int i = 0; i < n; i++)
			if (gDirtyPageSets[i].load(std::memory_order_relaxed) == d)
				gDirtyPageSets[i].store(nullptr, std::memory_order_release);
	}
}

uint8_t* LibSm64DirtyPages::PageAddress(size_t index) const
{
	return index < range[0].pages ? range[0].begin + index * pagesize : range[1].begin + (index - range[0].pages) * pagesize;
}

bool LibSm64DirtyPages::Contains(const void* p, size_t& index) const
{
	const uint8_t* ptr = static_cast<const uint8_t*>(p);
	size_t base = 0;
	for (const Range& r : range)
	{
		if (ptr >= r.begin && ptr < r.begin + r.pages * pagesize)
		{
			index = base + size_t(ptr - r.begin) / pagesize;
			return true;
		}
		base += r.pages;
	}
	return false;
}

void LibSm64DirtyPages::OnWrite(size_t index)
{
	const uint64_t bit = uint64_t(1) << (index & 63);
	for (int b : live)
	{
		Baseline& base = baselines[size_t(b)];
		if (base.present[index >> 6] & bit)
			continue;
		std::memcpy(base.pages.get() + index * pagesize, PageAddress(index), pagesize); // its content as of that baseline's start
		base.present[index >> 6] |= bit;
	}
	faults++;
	SetProtection(PageAddress(index), pagesize, true); // cannot throw from a fault handler; a failure re-faults and is fatal
}

void LibSm64DirtyPages::AddBaseline()
{
	Baseline base;
	base.present.assign((pageCount + 63) / 64, 0);
	base.pages.reset(new uint8_t[pageCount * pagesize]); // not touched: resident only where a page gets copied
	baselines.push_back(std::move(base));
	baseline = int(baselines.size()) - 1;
	live.push_back(baseline);
}

void LibSm64DirtyPages::ProtectAll()
{
	for (const Range& r : range)
		if (r.pages > 0 && !SetProtection(r.begin, r.pages * pagesize, false))
			throw std::runtime_error("could not write-protect the game's data sections");
}

void LibSm64DirtyPages::UnprotectAll()
{
	for (const Range& r : range)
		if (r.pages > 0)
			SetProtection(r.begin, r.pages * pagesize, true);
}

size_t LibSm64DirtyPages::WrittenCount() const
{
	size_t count = 0;
	for (uint64_t w : Written())
		count += size_t(std::popcount(w));
	return count;
}

LibSm64::LibSm64(const LibSm64Config& config) : dll(config.dllPath), config(config)
{
	slotManager._saveMemLimit = int64_t(8000) * 1024 * 1024; //8 GB

	// constructor of SharedLib will throw if it can't load
	void* processID = dll.get("sm64_init");

	// Macro evalutes to nothing on Linux and __stdcall on Windows
	// looks cleaner
	using pICFUNC = int(TAS_FW_STDCALL*)();

	pICFUNC sm64_init = pICFUNC(processID);

	sm64_init();

	_sm64Update = UpdateFn(dll.get("sm64_update"));
	_controllerPads = static_cast<uint8_t*>(dll.get("gControllerPads"));
	_globalTimer = static_cast<const uint32_t*>(dll.get("gGlobalTimer"));

	auto sections = dll.readSections();
	segment = std::vector<SegVal>
	{
		SegVal {".data", sections[".data"].address, sections[".data"].length},
		SegVal {".bss", sections[".bss"].address, sections[".bss"].length},
	};

	if (config.saveMode == LibSm64SaveMode::Fixed)
	{
		// The slices were cut for the pinned build; on a build with smaller sections (the Linux
		// .so's .data is 330 KB and its .bss 3.6 MB, against 2.4 MB and 4.9 MB in the DLL) a
		// slice would read past the section and crash before anything could say why.
		// Refuse with the reason instead.
		for (const LibSm64FixedSlice& slice : LibSm64FixedSlices)
		{
			const SegVal& seg = segment[size_t(slice.segment)];
			if (slice.offset + slice.length > seg.length)
				throw std::runtime_error("fixed save mode: the slice at " + seg.name + "+" + std::to_string(slice.offset) + " ("
					+ std::to_string(slice.length) + " bytes) lies beyond the end of " + seg.name + " (" + std::to_string(seg.length)
					+ " bytes) in " + config.dllPath.string() + "; the slices fit the pinned build only, use the dirty or full save mode");
		}
	}

	if (config.saveMode == LibSm64SaveMode::Dirty)
	{
		// Whole pages covering each section. On Windows sections start on a page boundary; on
		// Linux .data and .bss can start mid-page, so an edge page may also hold a neighbouring
		// writable section (.got.plt, .data.rel), which is then saved and restored along with
		// the game state. Harmless: with RTLD_NOW nothing writes there after loading. If the
		// two spans share a page it belongs to the lower one.
		_dirtyPages = std::make_unique<LibSm64DirtyPages>();
		LibSm64DirtyPages& d = *_dirtyPages;
		for (int i = 0; i < 2; i++)
		{
			uintptr_t begin = reinterpret_cast<uintptr_t>(segment[size_t(i)].address) & ~uintptr_t(pagesize - 1);
			uintptr_t end = (reinterpret_cast<uintptr_t>(segment[size_t(i)].address) + segment[size_t(i)].length + pagesize - 1) & ~uintptr_t(pagesize - 1);
			d.range[i].begin = reinterpret_cast<uint8_t*>(begin);
			d.range[i].pages = (end - begin) / pagesize;
		}
		if (d.range[1].begin < d.range[0].begin)
			std::swap(d.range[0], d.range[1]);
		uint8_t* end0 = d.range[0].begin + d.range[0].pages * pagesize;
		if (d.range[1].begin < end0)
		{
			size_t overlap = size_t(end0 - d.range[1].begin) / pagesize;
			d.range[1].begin = end0;
			d.range[1].pages = overlap < d.range[1].pages ? d.range[1].pages - overlap : 0;
		}
		d.pageCount = d.range[0].pages + d.range[1].pages;
		d.AddBaseline(); // baseline 0: the state right after sm64_init
		RegisterDirtyPages(&d);
		d.ProtectAll();
	}
}

LibSm64::~LibSm64()
{
	if (_dirtyPages)
	{
		UnregisterDirtyPages(_dirtyPages.get());
		_dirtyPages->UnprotectAll();
	}
}

// Start a new baseline unless nothing was written since the current one began (then it is
// as good as new: construction followed by the start save costs nothing). Baselines no live
// state can refer to are released: a state refers to the baseline it was saved under, so
// every baseline a live slot's state names is kept, and the start save's unless the start
// save itself is what is being written. The state being written is skipped (a recycled state
// carries the baseline of a save long gone), so at the first slot of a run only the start
// save's baseline survives. Nothing is copied here: the new baseline fills in from the fault
// handler as pages get written.
void LibSm64::TakeBaseline(const LibSm64Mem& saving) const
{
	LibSm64DirtyPages& d = *_dirtyPages;
	if (d.WrittenCount() == 0)
		return;
	d.AddBaseline();

	std::vector<bool> referenced(d.baselines.size(), false);
	referenced.back() = true;
	if (&saving != &startSave)
		referenced[size_t(startSave.baseline)] = true;
	for (const auto& [id, state] : slotManager.slotsById)
		if (&state != &saving)
			referenced[size_t(state.baseline)] = true;
	d.live.clear();
	for (size_t b = 0; b < d.baselines.size(); b++)
	{
		if (!referenced[b])
		{
			d.baselines[b].pages.reset();
			d.baselines[b].present.clear();
			d.baselines[b].present.shrink_to_fit();
		}
		if (d.baselines[b].pages)
			d.live.push_back(int(b));
	}
	d.ProtectAll();
}

void LibSm64::save(LibSm64Mem& state) const
{
	switch (config.saveMode)
	{
	case LibSm64SaveMode::Dirty:
	{
		// "No live slots" seen from inside a save: SlotManager::CreateSlot emplaces the new
		// slot before calling save, so the first slot of a run is the map's only entry and is
		// the state being written; the start save is not in the map at all.
		const auto& slots = slotManager.slotsById;
		bool firstSlot = slots.empty() || (slots.size() == 1 && &slots.begin()->second == &state);
		if (firstSlot)
			TakeBaseline(state);
		const LibSm64DirtyPages& d = *_dirtyPages;
		const std::vector<uint64_t>& written = d.Written();
		state.baseline = d.baseline;
		state.written.assign(written.begin(), written.end());
		state.pages.resize(d.WrittenCount() * pagesize);
		uint8_t* dst = state.pages.data();
		for (size_t j = 0; j < written.size(); j++)
		{
			uint64_t w = written[j];
			while (w != 0)
			{
				int b = std::countr_zero(w);
				w &= w - 1;
				std::memcpy(dst, d.PageAddress(j * 64 + size_t(b)), pagesize);
				dst += pagesize;
			}
		}
		return;
	}
	case LibSm64SaveMode::Fixed:
		state.buf1.resize(LibSm64FixedBuf1Size);
		state.buf2.resize(LibSm64FixedBuf2Size);
		for (const LibSm64FixedSlice& slice : LibSm64FixedSlices)
		{
			const uint8_t* src = reinterpret_cast<const uint8_t*>(segment[size_t(slice.segment)].address) + slice.offset;
			uint8_t* dst = (slice.segment == 0 ? state.buf1.data() : state.buf2.data()) + slice.bufOffset;
			std::memcpy(dst, src, slice.length);
		}
		return;
	case LibSm64SaveMode::Full:
		state.buf1.resize(segment[0].length);
		state.buf2.resize(segment[1].length);
		std::memcpy(state.buf1.data(), segment[0].address, segment[0].length);
		std::memcpy(state.buf2.data(), segment[1].address, segment[1].length);
		return;
	}
}

void LibSm64::load(const LibSm64Mem& state)
{
	switch (config.saveMode)
	{
	case LibSm64SaveMode::Dirty:
	{
		// Every page written since the state's baseline began is either in the state (written
		// before the save) or still had the content it had when that baseline began, which the
		// baseline holds. Pages written since by nobody are untouched and equal already. Writing
		// into a page protected under the current baseline faults and gets recorded, which is
		// right: it now differs from what the current baseline began with.
		LibSm64DirtyPages& d = *_dirtyPages;
		if (state.baseline > d.baseline || state.written.size() != d.Written().size())
			throw std::runtime_error("LibSm64::load: the state was not saved by this resource");
		const LibSm64DirtyPages::Baseline& base = d.baselines[size_t(state.baseline)];
		if (!base.pages)
			throw std::runtime_error("LibSm64::load: the state's baseline was released while the state was still loadable (LibSm64 bug)");
		const uint8_t* src = state.pages.data();
		for (size_t j = 0; j < base.present.size(); j++)
		{
			uint64_t since = base.present[j];
			uint64_t saved = state.written[j];
			uint64_t u = since | saved;
			while (u != 0)
			{
				int bit = std::countr_zero(u);
				u &= u - 1;
				size_t index = j * 64 + size_t(bit);
				uint8_t* page = d.PageAddress(index);
				if ((saved >> bit) & 1)
				{
					std::memcpy(page, src, pagesize); // the state's pages are stored in index order
					src += pagesize;
				}
				else
					std::memcpy(page, base.pages.get() + index * pagesize, pagesize);
			}
		}
		return;
	}
	case LibSm64SaveMode::Fixed:
		for (const LibSm64FixedSlice& slice : LibSm64FixedSlices)
		{
			uint8_t* dst = reinterpret_cast<uint8_t*>(segment[size_t(slice.segment)].address) + slice.offset;
			const uint8_t* src = (slice.segment == 0 ? state.buf1.data() : state.buf2.data()) + slice.bufOffset;
			std::memcpy(dst, src, slice.length);
		}
		return;
	case LibSm64SaveMode::Full:
		std::memcpy(segment[0].address, state.buf1.data(), segment[0].length);
		std::memcpy(segment[1].address, state.buf2.data(), segment[1].length);
		return;
	}
}

void LibSm64::advance()
{
	_sm64Update();
}

void LibSm64::setInputs(const Inputs& inputs)
{
	// OSContPad layout: u16 button, s8 stick_x, s8 stick_y (then errno, unused here).
	memcpy(_controllerPads, &inputs.buttons, sizeof(uint16_t));
	_controllerPads[2] = static_cast<uint8_t>(inputs.stick_x);
	_controllerPads[3] = static_cast<uint8_t>(inputs.stick_y);
}

void* LibSm64::addr(const char* symbol) const
{
	if (void* p = dll.tryGet(symbol))
		return p;

	// Not exported under that name. If it is one the decomp has renamed, try the other
	// spelling so scripts written against the pinned DLL run on newer builds and vice versa.
	for (const LibSm64SymbolAlias& alias : LibSm64SymbolAliases)
	{
		const char* other = std::strcmp(symbol, alias.pinned) == 0 ? alias.current
			: std::strcmp(symbol, alias.current) == 0 ? alias.pinned
			: nullptr;
		if (other == nullptr)
			continue;
		if (void* p = dll.tryGet(other))
			return p;
		throw std::runtime_error(std::string("libsm64 exports neither ") + symbol + " nor " + other);
	}

	return dll.get(symbol); // throws with the loader's message
}

std::size_t LibSm64::getStateSize(const LibSm64Mem& state) const
{
	return state.buf1.capacity() + state.buf2.capacity() + state.pages.capacity() + state.written.capacity() * sizeof(uint64_t);
}

uint32_t LibSm64::getCurrentFrame() const
{
	return *_globalTimer - 1;
}

