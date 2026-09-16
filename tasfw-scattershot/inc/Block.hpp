#pragma once
#include <memory>
#include <Segment.hpp>

#ifndef BLOCK_H
#define BLOCK_H

template <class TState>
class Block
{
public:
    std::shared_ptr<Segment> tailSegment;
    TState stateBin;
    float fitness;
};

#endif
