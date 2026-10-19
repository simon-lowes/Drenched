#include "drenched/DirectConvolver.h"

#include <algorithm>
#include <cassert>

namespace drenched
{

void DirectConvolver::setImpulseResponse (std::span<const float> impulseResponse)
{
    ir.assign (impulseResponse.begin(), impulseResponse.end());
    history.assign (ir.size(), 0.0f);
    writeIndex = 0;
}

void DirectConvolver::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    writeIndex = 0;
}

void DirectConvolver::process (std::span<const float> input, std::span<float> output) noexcept
{
    assert (input.size() == output.size());

    const auto taps = ir.size();

    if (taps == 0)
    {
        std::fill (output.begin(), output.end(), 0.0f);
        return;
    }

    for (std::size_t n = 0; n < input.size(); ++n)
    {
        history[writeIndex] = input[n];

        // Walk backwards through history: tap k pairs with the input k samples ago.
        float sum = 0.0f;
        auto readIndex = writeIndex;

        for (std::size_t k = 0; k < taps; ++k)
        {
            sum += ir[k] * history[readIndex];
            readIndex = (readIndex == 0 ? taps : readIndex) - 1;
        }

        output[n] = sum;
        writeIndex = (writeIndex + 1) % taps;
    }
}

} // namespace drenched
