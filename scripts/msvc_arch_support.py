# "Pure" Python CPUID
import sys
from cpuid import CPUID
cpuid = CPUID()

def id_no():
	res = 0
	eax, ebx, ecx, edx = cpuid(0)
	# Number of available CPUID values
	base_available = eax
	
	if base_available >= 1:
		eax, ebx, ecx, edx = cpuid(1)
		# AVX feature bit
		if ((ecx >> 28) & 1) != 0:
			res = 1
		else:
			return res
	
	if base_available >= 7:
		eax, ebx, ecx, edx = cpuid(7, 0)
		# AVX2 feature bit
		if ((ebx >> 5) & 1) != 0:
			res = 2
		else:
			return res
		
		# AVX512F feature bit
		if ((ebx >> 16) & 1) != 0:
			res = 3
		else:
			return res
	
	return res

ID_LIST = ("", "/arch:AVX", "/arch:AVX2", "/arch:AVX512")

if __name__ == "__main__":
	id = id_no()
	print(ID_LIST[id])
	sys.exit(1 if id == 0 else 0)
	