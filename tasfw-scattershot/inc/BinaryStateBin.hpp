#pragma once
#include <cstdint>

template <int nBytes>
class BinaryStateBin
{
public:
    std::uint8_t bytes[nBytes];

    BinaryStateBin()
    {
        for (int i = 0; i < nBytes; i++)
            bytes[i] = 0;
    }

    bool operator==(const BinaryStateBin& toCompare) const = default;

    void AddValueBits(uint8_t& bitCursor, uint8_t bitsToAllocate, uint64_t value)
    {
        if (bitCursor < 0 || bitCursor >= nBytes * 8)
            throw std::runtime_error("Bit cursor out of range.");

        uint64_t maxValue = uint64_t(1) << uint64_t(bitsToAllocate);
        if (value >= maxValue)
            throw std::runtime_error("Value too large for bits allocated.");

        AddBits(bitCursor, bitsToAllocate, value);
    }

    template <typename T>
        requires std::integral<T> || std::floating_point<T>
    void AddRegionBitsByNRegions(uint8_t& bitCursor, uint8_t bitsToAllocate, T value, T min, T max, uint64_t nRegions)
    {
        if (bitCursor < 0 || bitCursor >= nBytes * 8)
            throw std::runtime_error("Bit cursor out of range.");

        if (value < min || value > max)
            throw std::runtime_error("Value out of range.");

        if (nRegions == 0)
            throw std::runtime_error("Invalid number of regions.");

        uint64_t maxRegions = uint64_t(1) << uint64_t(bitsToAllocate);
        if (nRegions > maxRegions)
            nRegions = maxRegions;

        double regionSize = double(max - min) / double(nRegions);
        uint64_t region = double(value - min) / regionSize;
        if (region == nRegions) // Could happen if value is close to max?
            region--;

        // Sanity check
        if (region >= nRegions)
            throw std::runtime_error("Region out of range.");

        AddBits(bitCursor, bitsToAllocate, region);
    }

    template <typename T>
        requires std::integral<T> || std::floating_point<T>
    void AddRegionBitsByRegionSize(uint8_t & bitCursor, uint8_t bitsToAllocate, T value, T min, T max, T regionSize)
    {
        if (bitCursor < 0 || bitCursor >= nBytes * 8)
            throw std::runtime_error("Bit cursor out of range.");

        if (value < min || value > max)
            throw std::runtime_error("Value out of range.");

        if (regionSize == 0)
            throw std::runtime_error("Invalid number of regions.");

        uint64_t nRegions = std::ceil(double(max - min) / double(regionSize));
        uint64_t maxRegions = uint64_t(1) << uint64_t(bitsToAllocate);
        if (nRegions > maxRegions)
            nRegions = maxRegions;

        uint64_t region = double(value - min) / double(regionSize);
        if (region == nRegions) // Could happen if value is close to max?
            region--;

        // Sanity check
        if (region >= nRegions)
            throw std::runtime_error("Region out of range.");

        AddBits(bitCursor, bitsToAllocate, region);
    }

private:
    void AddBits(uint8_t& bitCursor, uint8_t bitsToAllocate, uint64_t value)
    {
        for (int bit = 0; bit < bitsToAllocate; bit++)
        {
            int trueBit = bitCursor + bit;
            int byteIndex = trueBit >> 3;
            int byteBit = trueBit % 8;

            uint64_t bitMask = uint64_t(1) << uint64_t(bit);
            if (bitMask & value)
                bytes[byteIndex] |= 1 << byteBit;
        }

        bitCursor += bitsToAllocate;
    }
};
