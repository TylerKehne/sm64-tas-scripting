#pragma once
#include <cstdint>
#include <memory>

#ifndef SEGMENT_H
#define SEGMENT_H

class Segment
{
public:
    std::shared_ptr<Segment> parent;
    uint64_t seed;
    uint8_t nScripts;
    uint8_t depth;
    uint16_t pipedDiff1Index = 0;

    bool operator==(const Segment&) const = default;

    Segment(std::shared_ptr<Segment> parent, uint64_t seed, uint8_t nScripts, uint16_t pipedDiff1Index)
        : parent(parent), seed(seed), nScripts(nScripts), pipedDiff1Index(pipedDiff1Index)
    {
        if (parent == nullptr)
            depth = 0;
        else
            depth = parent->depth + 1;
    }
};

#endif
