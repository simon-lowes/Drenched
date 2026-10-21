#include "drenched/PartitionedConvolver.h"

#include <pffft/pffft.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>
#include <utility>

namespace drenched
{

namespace
{

/** Float storage aligned for PFFFT's SIMD transforms, zero initialised. */
class AlignedFloats
{
public:
    AlignedFloats() = default;

    explicit AlignedFloats (std::size_t count)
        : size_ (count),
          data_ (static_cast<float*> (pffft_aligned_malloc (count * sizeof (float))))
    {
        if (data_ == nullptr)
            throw std::bad_alloc();

        std::memset (data_, 0, count * sizeof (float));
    }

    ~AlignedFloats() { pffft_aligned_free (data_); }

    AlignedFloats (const AlignedFloats&) = delete;
    AlignedFloats& operator= (const AlignedFloats&) = delete;

    AlignedFloats (AlignedFloats&& other) noexcept
        : size_ (std::exchange (other.size_, 0)), data_ (std::exchange (other.data_, nullptr)) {}

    AlignedFloats& operator= (AlignedFloats&& other) noexcept
    {
        std::swap (size_, other.size_);
        std::swap (data_, other.data_);
        return *this;
    }

    float* data() noexcept { return data_; }
    const float* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    void clear() noexcept { if (data_ != nullptr) std::memset (data_, 0, size_ * sizeof (float)); }

private:
    std::size_t size_ = 0;
    float* data_ = nullptr;
};

struct SetupDeleter
{
    void operator() (PFFFT_Setup* setup) const noexcept { pffft_destroy_setup (setup); }
};

} // namespace

/** One uniformly partitioned overlap-save stage: block size B, FFT size 2B,
    P partitions covering taps [B, B + P * B) of the impulse response. */
struct PartitionedConvolver::Stage
{
    Stage (std::size_t blockSize, std::size_t numPartitions,
           const std::vector<std::span<const float>>& irs, std::size_t firstTap)
        : B (blockSize), P (numPartitions), N (2 * blockSize), numOut (irs.size()),
          setup (pffft_new_setup (static_cast<int> (2 * blockSize), PFFFT_REAL)),
          history (N), work (N), timeDomain (N),
          filters (numOut * P * N), fdl (P * N), acc (numOut * N), outBlock (numOut * B)
    {
        if (setup == nullptr)
            throw std::bad_alloc();

        // Transform each partition of each impulse response once, up front.
        AlignedFloats padded (N);

        for (std::size_t k = 0; k < numOut; ++k)
        {
            const auto ir = irs[k];

            for (std::size_t p = 0; p < P; ++p)
            {
                padded.clear();
                const auto begin = firstTap + p * B;

                if (begin < ir.size())
                {
                    const auto count = std::min (B, ir.size() - begin);
                    std::copy_n (ir.data() + begin, count, padded.data());
                }

                pffft_transform (setup.get(), padded.data(), filter (k, p), work.data(), PFFFT_FORWARD);
            }
        }
    }

    float* filter (std::size_t output, std::size_t partition) noexcept
    {
        return filters.data() + (output * P + partition) * N;
    }

    float* spectrum (std::size_t slot) noexcept { return fdl.data() + slot * N; }

    void reset() noexcept
    {
        history.clear();
        fdl.clear();
        acc.clear();
        outBlock.clear();
        fill = 0;
        macDone = 0;
        newest = 0;
    }

    /** Multiply-accumulates partitions 1 .. P-1 for the block now filling, up
        to the share that matches how much of the block has arrived. Partition
        p pairs with the spectrum p blocks before the one being filled, all of
        which already exist. */
    void accumulateOlderPartitions (std::size_t upTo) noexcept
    {
        const auto scale = 1.0f / static_cast<float> (N);

        while (macDone < upTo)
        {
            const auto p = macDone + 1;
            const auto slot = (newest + P - (p - 1)) % P;
            const auto* x = spectrum (slot);

            for (std::size_t k = 0; k < numOut; ++k)
                pffft_zconvolve_accumulate (setup.get(), x, filter (k, p), acc.data() + k * N, scale);

            ++macDone;
        }
    }

    /** Called when a block of B input samples is complete. */
    void finishBlock() noexcept
    {
        const auto scale = 1.0f / static_cast<float> (N);

        accumulateOlderPartitions (P - 1);

        newest = (newest + 1) % P;
        auto* x = spectrum (newest);
        pffft_transform (setup.get(), history.data(), x, work.data(), PFFFT_FORWARD);

        for (std::size_t k = 0; k < numOut; ++k)
        {
            auto* a = acc.data() + k * N;
            pffft_zconvolve_accumulate (setup.get(), x, filter (k, 0), a, scale);
            pffft_transform (setup.get(), a, timeDomain.data(), work.data(), PFFFT_BACKWARD);

            // Overlap-save: the second half is the alias-free part.
            std::copy_n (timeDomain.data() + B, B, outBlock.data() + k * B);
            std::fill_n (a, N, 0.0f);
        }

        // Slide the input window by one block.
        std::copy_n (history.data() + B, B, history.data());
        fill = 0;
        macDone = 0;
    }

    /** Adds this stage's contribution for numSamples into the outputs. */
    void process (const float* input, float* const* outputs, std::size_t numSamples) noexcept
    {
        std::size_t done = 0;

        while (done < numSamples)
        {
            const auto count = std::min (numSamples - done, B - fill);

            std::copy_n (input + done, count, history.data() + B + fill);

            // The block computed at the last boundary covers exactly the
            // samples arriving now, because this stage starts at tap B.
            for (std::size_t k = 0; k < numOut; ++k)
            {
                const auto* src = outBlock.data() + k * B + fill;
                auto* dst = outputs[k] + done;

                for (std::size_t n = 0; n < count; ++n)
                    dst[n] += src[n];
            }

            fill += count;
            done += count;

            if (P > 1)
                accumulateOlderPartitions ((P - 1) * fill / B);

            if (fill == B)
                finishBlock();
        }
    }

    const std::size_t B, P, N, numOut;
    std::unique_ptr<PFFFT_Setup, SetupDeleter> setup;
    AlignedFloats history, work, timeDomain, filters, fdl, acc, outBlock;
    std::size_t fill = 0, macDone = 0, newest = 0;
};

PartitionedConvolver::PartitionedConvolver (const std::vector<std::span<const float>>& impulseResponses)
{
    assert (! impulseResponses.empty());

    for (const auto& ir : impulseResponses)
        irLength = std::max (irLength, ir.size());

    heads.resize (impulseResponses.size());

    for (std::size_t k = 0; k < heads.size(); ++k)
    {
        const auto ir = impulseResponses[k];
        heads[k].setImpulseResponse (ir.first (std::min (headLength, ir.size())));
    }

    for (std::size_t block = headLength; block < irLength; block *= growthFactor)
    {
        // A larger stage only pays for itself when the rest fills at least one
        // of its blocks; otherwise this stage takes up to 15 partitions to the
        // end. Rounding a short remainder up to one huge block per output cost
        // 2 GB for a long 9.1.6 response.
        const auto next = block * growthFactor;
        const auto last = irLength < 2 * next;
        const auto partitions = last ? (irLength - block + block - 1) / block : growthFactor - 1;

        stages.push_back (std::make_unique<Stage> (block, partitions, impulseResponses, block));

        if (last)
            break;
    }
}

PartitionedConvolver::~PartitionedConvolver() = default;

void PartitionedConvolver::reset() noexcept
{
    for (auto& head : heads)
        head.reset();

    for (auto& stage : stages)
        stage->reset();
}

void PartitionedConvolver::process (const float* input, float* const* outputs, std::size_t numSamples) noexcept
{
    for (std::size_t k = 0; k < heads.size(); ++k)
        heads[k].process ({ input, numSamples }, { outputs[k], numSamples });

    for (auto& stage : stages)
        stage->process (input, outputs, numSamples);
}

} // namespace drenched
