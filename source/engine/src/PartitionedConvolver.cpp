#include "drenched/PartitionedConvolver.h"

#include <pffft/pffft.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>
#include <stdexcept>
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
          data_ (count == 0 ? nullptr : static_cast<float*> (pffft_aligned_malloc (count * sizeof (float))))
    {
        if (count == 0)
            return;

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
    P partitions covering taps [firstTap, firstTap + P * B) of the impulse
    response.

    A stage that starts at tap B must have each block's output ready the moment
    the block's input is complete, so its transforms all run in that one audio
    callback. A stage that starts at tap 2B (with slack) has a whole block of
    time instead: while block k+1 arrives it works out block k's output a share
    at a time, transforms included, and plays it from the boundary after. That
    keeps the large stages from spiking the CPU every time a block completes. */
struct PartitionedConvolver::Stage
{
    Stage (std::size_t blockSize, std::size_t numPartitions,
           const std::vector<std::span<const float>>& irs, std::size_t firstTap, bool withSlack)
        : B (blockSize), P (numPartitions), N (2 * blockSize), numOut (irs.size()), slack (withSlack),
          setup (pffft_new_setup (static_cast<int> (2 * blockSize), PFFFT_REAL)),
          history (N), work (N), timeDomain (N),
          filters (numOut * P * N), fdl (P * N), acc (numOut * N), outBlock (numOut * B),
          nextBlock (slack ? numOut * B : 0), pendingInput (slack ? N : 0)
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

        // A transform costs about as much as this many spectrum multiplies.
        auto bits = std::size_t { 0 };
        while ((std::size_t { 1 } << bits) < N)
            ++bits;

        transformCost = std::max<std::size_t> (1, bits * 3 / 5);
        numTasks = 1 + numOut * (P + 1);
        totalCost = transformCost + numOut * (P + transformCost);
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
        nextBlock.clear();
        pendingInput.clear();
        fill = 0;
        macDone = 0;
        newest = 0;
        tasksDone = 0;
        costDone = 0;
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

    /** With slack: the work for the last completed block's output is a list of
        tasks, the forward transform of its input first, then for each output
        its P multiplies and its inverse transform. Runs them up to the share
        of their cost that matches how much of the current block has arrived. */
    void runTasks (std::size_t costTarget) noexcept
    {
        const auto scale = 1.0f / static_cast<float> (N);

        while (tasksDone < numTasks)
        {
            const auto t = tasksDone;
            const auto isTransform = t == 0 || (t - 1) % (P + 1) == P;
            const auto cost = isTransform ? transformCost : 1;

            if (costDone + cost > costTarget)
                break;

            if (t == 0)
            {
                pffft_transform (setup.get(), pendingInput.data(), spectrum (newest), work.data(), PFFFT_FORWARD);
            }
            else
            {
                const auto k = (t - 1) / (P + 1), p = (t - 1) % (P + 1);
                auto* a = acc.data() + k * N;

                if (p < P)
                {
                    pffft_zconvolve_accumulate (setup.get(), spectrum ((newest + P - p) % P), filter (k, p), a, scale);
                }
                else
                {
                    pffft_transform (setup.get(), a, timeDomain.data(), work.data(), PFFFT_BACKWARD);
                    std::copy_n (timeDomain.data() + B, B, nextBlock.data() + k * B);
                    std::fill_n (a, N, 0.0f);
                }
            }

            ++tasksDone;
            costDone += cost;
        }
    }

    /** With slack, at the end of a block: finishes the previous block's output
        and plays it from now, and queues the block just completed. */
    void finishBlockWithSlack() noexcept
    {
        runTasks (totalCost);
        std::swap (outBlock, nextBlock);

        newest = (newest + 1) % P;
        std::copy_n (history.data(), N, pendingInput.data());
        std::copy_n (history.data() + B, B, history.data());
        fill = 0;
        tasksDone = 0;
        costDone = 0;
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
            // samples arriving now, because this stage starts at tap B (or
            // 2B with slack, where it was worked out during the block before).
            for (std::size_t k = 0; k < numOut; ++k)
            {
                const auto* src = outBlock.data() + k * B + fill;
                auto* dst = outputs[k] + done;

                for (std::size_t n = 0; n < count; ++n)
                    dst[n] += src[n];
            }

            fill += count;
            done += count;

            if (slack)
            {
                runTasks (totalCost * fill / B);

                if (fill == B)
                    finishBlockWithSlack();
            }
            else
            {
                if (P > 1)
                    accumulateOlderPartitions ((P - 1) * fill / B);

                if (fill == B)
                    finishBlock();
            }
        }
    }

    const std::size_t B, P, N, numOut;
    const bool slack;
    std::unique_ptr<PFFFT_Setup, SetupDeleter> setup;
    AlignedFloats history, work, timeDomain, filters, fdl, acc, outBlock, nextBlock, pendingInput;
    std::size_t fill = 0, macDone = 0, newest = 0;
    std::size_t transformCost = 1, numTasks = 0, totalCost = 0, tasksDone = 0, costDone = 0;
};

PartitionedConvolver::PartitionedConvolver (const std::vector<std::span<const float>>& impulseResponses,
                                            std::size_t latency)
    : outputs (impulseResponses.size()), delay (latency)
{
    assert (! impulseResponses.empty());

    if (latency != 0 && (latency < headLength || (latency & (latency - 1)) != 0))
        throw std::invalid_argument ("A convolution latency must be 0 or a power of two from 64 samples.");

    for (const auto& ir : impulseResponses)
        irLength = std::max (irLength, ir.size());

    // With a latency the response is treated as delayed by that much: its
    // first latency taps are silent, so neither the head nor the stages
    // smaller than the latency are needed, and each stage reads the real
    // response latency taps earlier.
    if (latency == 0)
    {
        heads.resize (impulseResponses.size());

        for (std::size_t k = 0; k < heads.size(); ++k)
        {
            const auto ir = impulseResponses[k];
            heads[k].setImpulseResponse (ir.first (std::min (headLength, ir.size())));
        }
    }

    const auto total = irLength + latency;
    const auto startOf = [] (std::size_t block) { return block >= slackFrom ? 2 * block : block; };
    auto begin = latency == 0 ? headLength : latency; // where the next stage starts

    for (std::size_t block = begin; begin < total; block *= growthFactor)
    {
        // A larger stage only pays for itself when the rest fills at least one
        // of its blocks; otherwise this stage takes the rest. Rounding a short
        // remainder up to one huge block per output cost 2 GB for a long 9.1.6
        // response.
        const auto next = block * growthFactor;
        const auto nextBegin = startOf (next);
        const auto last = total < nextBegin + next;
        const auto end = last ? total : nextBegin;
        const auto partitions = (end - begin + block - 1) / block;

        // Only a stage that starts at 2B has the slack to spread its transforms
        // (the first one starts straight after the head or the latency).
        stages.push_back (std::make_unique<Stage> (block, partitions, impulseResponses, begin - latency,
                                                   begin >= 2 * block));

        if (last)
            break;

        begin = nextBegin;
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
    if (heads.empty())
        for (std::size_t k = 0; k < numOutputs(); ++k)
            std::fill_n (outputs[k], numSamples, 0.0f);

    for (std::size_t k = 0; k < heads.size(); ++k)
        heads[k].process ({ input, numSamples }, { outputs[k], numSamples });

    for (auto& stage : stages)
        stage->process (input, outputs, numSamples);
}

} // namespace drenched
