#pragma once

#include "drenched/DirectConvolver.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace drenched
{

/** Zero-latency, non-uniformly partitioned FFT convolution of one input with
    one or more impulse responses that share it (for example the left and
    right outputs fed by one input of a true-stereo reverb).

    Layout of the impulse response:
      - taps [0, 64) run in the time domain (DirectConvolver), so there is no
        latency;
      - after that come uniformly partitioned overlap-save stages whose block
        sizes grow by a factor of eight (64, 512, 4096, 32768). A stage with
        block size B starts at tap B, which exactly hides its B samples of
        buffering, so the stages chain without any added delay.

    The frequency-domain multiply-accumulate of the older partitions of each
    stage is spread evenly across the samples of a block instead of being done
    all at once, so the CPU cost per audio callback stays flat rather than
    spiking every time a large block completes.

    With a latency L (a power of two, at least headLength) the output comes L
    samples late instead: the time-domain head and the stages smaller than L
    are left out, which is what costs the most per sample, so the convolution
    runs on far less CPU. It is the same response, delayed by exactly L.

    Construction allocates and transforms the impulse responses, so build
    instances away from the audio thread. process() and reset() never
    allocate, lock or wait. */
class PartitionedConvolver
{
public:
    static constexpr std::size_t headLength = 64;
    static constexpr std::size_t growthFactor = 8;

    /** One impulse response per output. Shorter responses are zero padded to
        the longest. At least one response is required. latency is 0 or a
        power of two from headLength up; anything else throws
        std::invalid_argument. */
    explicit PartitionedConvolver (const std::vector<std::span<const float>>& impulseResponses,
                                   std::size_t latency = 0);
    ~PartitionedConvolver();

    PartitionedConvolver (const PartitionedConvolver&) = delete;
    PartitionedConvolver& operator= (const PartitionedConvolver&) = delete;

    /** Clears all input history. Real-time safe. */
    void reset() noexcept;

    /** Convolves numSamples of input. outputs must hold numOutputs() pointers,
        each to numSamples writable floats; they are overwritten. The input may
        not alias an output. Any numSamples is allowed. */
    void process (const float* input, float* const* outputs, std::size_t numSamples) noexcept;

    std::size_t numOutputs() const noexcept { return outputs; }
    std::size_t length() const noexcept { return irLength; }
    std::size_t latency() const noexcept { return delay; }

private:
    struct Stage;

    std::size_t irLength = 0, outputs = 0, delay = 0;
    std::vector<DirectConvolver> heads; // none with a latency
    std::vector<std::unique_ptr<Stage>> stages;
};

} // namespace drenched
