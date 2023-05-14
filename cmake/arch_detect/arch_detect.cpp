#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__)
[[gnu::always_inline]] void __cpuid(int regs[4], int x) {
  __asm__(
    "cpuid;" :
    "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) :
    "a"(x)
  );
}
[[gnu::always_inline]] void __cpuidex(int regs[4], int x, int y) {
  __asm__(
    "cpuid;" :
    "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) :
    "a"(x), "c"(y)
  );
}
#endif
#include <cstdio>

const char* arch_flags[] = {
	"",
	"/arch:AVX",
	"/arch:AVX2",
	"/arch:AVX512F"
};

int main() {
	int regs[4];
	int max_cpuid;
	int result = 0;
	
	__cpuid(regs, 0);
	max_cpuid = regs[0];
	if (max_cpuid > 7) {
		__cpuidex(regs, 7, 0);
		// AVX512F
		if (regs[1] & (1 << 16))
			result = 3;
		// AVX2
		else if (regs[1] & (1 << 5))
			result = 2;
	}
	if (max_cpuid >= 1 && result == 0) {
		__cpuid(regs, 1);
		// AVX
		if (regs[2] & (1 << 28))
			result = 1;
	}
	
	puts(arch_flags[result]);
	
	return result == 0? 1 : 0;
}