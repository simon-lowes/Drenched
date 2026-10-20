#pragma once

#include <cstddef>
#include <random>
#include <vector>

namespace testing
{
    inline std::vector<float> noise (std::size_t length, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> v (length);
        for (auto& s : v)
            s = dist (rng);
        return v;
    }

    /** Counts heap allocations made by the current thread while alive. Built on
        replacement global operator new (AllocationCounter.cpp). */
    class AllocationCounter
    {
    public:
        AllocationCounter();
        ~AllocationCounter();
        std::size_t count() const;
    };
}
