#pragma once

#include <vector>

using namespace std;

#ifndef TRIG_H
#define TRIG_H

extern vector<float> gSineTable;
extern vector<float> gCosineTable;
extern vector<int> gArctanTable;

uint16_t atan2_lookup(float z, float x);
int16_t atan2s(float z, float x);

#endif

