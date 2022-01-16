#pragma once

#include <cstdlib>
#include <stdio.h>
#include <string.h>

#ifndef SLOT_H
#define SLOT_H

class Slot
{
public:
	char* buf1;
	char* buf2;

	Slot() { }

	Slot(size_t size1, size_t size2) {
		buf1 = (char*)malloc(size1);
		buf2 = (char*)malloc(size2);

		memset(buf1, 0, size1);
		memset(buf2, 0, size2);
	}
};

#endif