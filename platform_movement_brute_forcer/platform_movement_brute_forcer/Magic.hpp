#include <windows.h>
#pragma once
#include <map>
#include <string>
#include <vector>
using namespace std;

#ifndef MAGIC_H
#define MAGIC_H

class SegVal {
public:
	string name;
	intptr_t virtual_address;
	int64_t virtual_size;

	SegVal(string nm, intptr_t virt_addr, int64_t virt_size) {
		name = nm;
		virtual_address = virt_addr;
		virtual_size = virt_size;
	}
};

/*
extern map<string, vector<SegVal>> SEGMENTS;*/

uint16_t atan2_lookup(float z, float x);
int16_t atan2s(float z, float x);

bool check_if_inbounds(float x, float z);

#endif