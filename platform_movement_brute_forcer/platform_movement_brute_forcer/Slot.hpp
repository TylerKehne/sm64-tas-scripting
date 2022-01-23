#pragma once

#include <cstdlib>
#include <stdio.h>
#include <string.h>

#ifndef SLOT_H
#define SLOT_H

class Slot
{
public:
	std::vector<uint8_t> buf1;
	std::vector<uint8_t> buf2;

	Slot () {}

	Slot(size_t size1, size_t size2) {
		buf1 = std::vector<uint8_t>(size1, 0);
		buf2 = std::vector<uint8_t>(size2, 0);
	}
};

#endif