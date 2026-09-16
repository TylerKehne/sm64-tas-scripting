#pragma once
#include <utility>
#include <tasfw/M64.hpp>

#ifndef SCATTERSHOTSOLUTION_H
#define SCATTERSHOTSOLUTION_H

template <class TOutputState>
class ScattershotSolution
{
public:
    TOutputState data;
    M64Diff m64Diff;

    ScattershotSolution(TOutputState data, M64Diff m64Diff) : data(data), m64Diff(m64Diff) {}

    ScattershotSolution() = default;
    ScattershotSolution(const ScattershotSolution&) = default;
    ScattershotSolution& operator=(const ScattershotSolution&) = default;
    ScattershotSolution(ScattershotSolution&&) noexcept = default;
    ScattershotSolution& operator=(ScattershotSolution&&) noexcept = default;
};

#endif
