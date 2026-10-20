#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace drenched
{

/** Time-domain (direct form) convolution of one channel with one impulse
    response.

    This is the reference every fast engine is null-tested against: it is
    slow (O(N) per sample) but obviously correct. It also serves as the
    zero-latency head of the partitioned engine, where N is small.

    Not real-time safe to construct or reset with a new IR (it allocates);
    process() is allocation-free. */
class DirectConvolver
{
public:
    DirectConvolver() = default;

    /** Replaces the impulse response and clears the history. */
    void setImpulseResponse (std::span<const float> impulseResponse);

    /** Clears the input history without changing the impulse response. */
    void reset() noexcept;

    /** Convolves input into output (they may be the same buffer).
        output[n] = sum over k of ir[k] * input[n - k]. */
    void process (std::span<const float> input, std::span<float> output) noexcept;

    std::size_t length() const noexcept { return ir.size(); }

private:
    std::vector<float> ir;
    std::vector<float> history; // circular buffer of the last ir.size() inputs
    std::size_t writeIndex = 0;
};

} // namespace drenched
