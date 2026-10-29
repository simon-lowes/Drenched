#pragma once

#include <drenched/Biquad.h>

#include <cmath>
#include <cstddef>
#include <numeric>
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

    /** RT60 of one octave band, from the Schroeder backward integral fitted
        between -5 and -25 dB (T20), the standard ISO 3382 measurement. */
    /** One octave band: fourth-order high-pass and low-pass at the band edges,
        steep enough that a neighbouring band's longer decay does not leak in. */
    inline std::vector<float> octaveBand (std::vector<float> band, double sampleRate, double centre)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            drenched::Biquad hp, lp;
            hp.setCoefficients (drenched::Biquad::highPass (sampleRate, centre / std::sqrt (2.0)));
            lp.setCoefficients (drenched::Biquad::lowPass (sampleRate, centre * std::sqrt (2.0)));
            hp.process (band.data(), band.size());
            lp.process (band.data(), band.size());
        }

        return band;
    }

    inline double measureRt60 (const std::vector<float>& ir, double sampleRate, double centre)
    {
        const auto band = octaveBand (ir, sampleRate, centre);

        std::vector<double> schroeder (band.size());
        double sum = 0.0;
        for (std::size_t n = band.size(); n-- > 0;)
        {
            sum += static_cast<double> (band[n]) * band[n];
            schroeder[n] = sum;
        }

        const auto total = schroeder[0];
        std::vector<double> t, db;

        for (std::size_t n = 0; n < band.size(); ++n)
        {
            const auto level = 10.0 * std::log10 (schroeder[n] / total);

            if (level <= -5.0 && level >= -25.0)
            {
                t.push_back (static_cast<double> (n) / sampleRate);
                db.push_back (level);
            }
        }

        const auto meanT = std::accumulate (t.begin(), t.end(), 0.0) / static_cast<double> (t.size());
        const auto meanD = std::accumulate (db.begin(), db.end(), 0.0) / static_cast<double> (db.size());
        double num = 0.0, den = 0.0;

        for (std::size_t i = 0; i < t.size(); ++i)
        {
            num += (t[i] - meanT) * (db[i] - meanD);
            den += (t[i] - meanT) * (t[i] - meanT);
        }

        return -60.0 / (num / den);
    }

    /** Counts heap allocations and frees made by the current thread while alive. Built
        on replacement global operator new and delete (AllocationCounter.cpp). */
    class AllocationCounter
    {
    public:
        AllocationCounter();
        ~AllocationCounter();
        std::size_t count() const;
        std::size_t freed() const;

        /** Whether malloc and free are counted too (glibc, without a sanitizer). */
        static bool seesMalloc() noexcept;
    };
}
