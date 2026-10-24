// Measures what the engine costs: how long a kernel takes to build when the
// space or a control changes, and how much of one core the reverb takes to
// play, for a few layouts, block sizes and modes. Not a test; run it by hand:
//
//   build/tests/drenched_bench [seconds of audio per case, default 10]

#include "drenched/ConvolutionReverb.h"
#include "drenched/KernelBuilder.h"
#include "drenched/RecordedResponse.h"
#include "drenched/Spaces.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace drenched;
using Clock = std::chrono::steady_clock;

namespace
{

constexpr double rate = 48000.0;

double millisSince (Clock::time_point start)
{
    return std::chrono::duration<double, std::milli> (Clock::now() - start).count();
}

ImpulseResponseRequest requestFor (int space, const SpeakerLayout& outputs)
{
    ImpulseResponseRequest r;
    r.sampleRate = rate;
    r.space = space;
    r.decaySeconds = bundledSpaces[(size_t) space].decaySeconds;
    r.outputs = outputs;
    return r;
}

// A recorded-like true-stereo response: decaying noise, so the recorded path
// (trimming, measuring, shaping) runs as it does for a library space.
std::shared_ptr<const RecordedResponse> fakeRecording (double seconds)
{
    std::mt19937 noise (7);
    std::normal_distribution<float> gauss;
    const auto n = (size_t) (seconds * rate);
    std::vector<std::vector<float>> channels (4, std::vector<float> (n));

    for (auto& c : channels)
        for (size_t i = 0; i < n; ++i)
            c[i] = gauss (noise) * std::exp (-6.9f * (float) i / (float) (2.5 * rate));

    return prepareRecordedResponse (channels, rate, "bench");
}

void benchBuild (const char* label, ImpulseResponseRequest request)
{
    ConvolutionReverb reverb;
    reverb.prepare (rate, layouts::stereo(), request.outputs);
    KernelBuilder builder (reverb);

    double best = 1e9, total = 0.0;
    constexpr int runs = 3;

    for (int i = 0; i < runs; ++i)
    {
        request.decaySeconds[4] += 0.01f; // a new request every time
        const auto start = Clock::now();
        builder.buildNow (request);
        const auto ms = millisSince (start);
        best = std::min (best, ms);
        total += ms;
    }

    std::printf ("  %-44s best %7.1f ms   mean %7.1f ms\n", label, best, total / runs);
}

void benchPlay (const char* label, ImpulseResponseRequest request, size_t block, double seconds,
                bool living = false)
{
    const auto& outputs = request.outputs;
    ConvolutionReverb reverb;
    reverb.prepare (rate, layouts::stereo(), outputs);
    KernelBuilder builder (reverb);

    if (living)
        request.livingPercent = 50.0f;

    builder.buildNow (request);

    const auto channels = std::max<size_t> (2, outputs.size());
    std::vector<std::vector<float>> buffers (channels, std::vector<float> (block));
    std::vector<float*> pointers (channels);
    for (size_t c = 0; c < channels; ++c)
        pointers[c] = buffers[c].data();

    ReverbControls controls;
    controls.mix = 0.5f;
    controls.movement = living ? 0.3f : 0.0f;

    std::mt19937 noise (3);
    std::uniform_real_distribution<float> uniform (-0.5f, 0.5f);
    const auto blocks = (size_t) (seconds * rate / (double) block);

    // Warm up, then time.
    for (size_t b = 0; b < 50; ++b)
    {
        for (size_t c = 0; c < 2; ++c)
            for (auto& s : buffers[c]) s = uniform (noise);
        reverb.process (pointers.data(), channels, block, controls);
    }

    std::vector<double> times (blocks);
    double total = 0.0;

    for (size_t b = 0; b < blocks; ++b)
    {
        for (size_t c = 0; c < 2; ++c)
            for (auto& s : buffers[c]) s = uniform (noise);

        const auto t = Clock::now();
        reverb.process (pointers.data(), channels, block, controls);
        times[b] = millisSince (t);
        total += times[b];
    }

    // The share of each block's own time it took: the mean is the load, the
    // rare slow blocks are what can make a host drop out.
    std::sort (times.begin(), times.end());
    const auto blockMs = 1000.0 * (double) block / rate;
    const auto share = [&] (double ms) { return 100.0 * ms / blockMs; };
    std::printf ("  %-30s block %4zu   mean %5.1f%%   1 in 1000 %5.0f%%   worst %5.0f%%\n", label, block,
                 share (total / (double) blocks), share (times[blocks - 1 - blocks / 1000]), share (times.back()));
}

} // namespace

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 10.0;
    const auto stereo = layouts::stereo();
    const auto surround = layouts::sevenPointOnePointFour();

    auto recorded = requestFor (0, stereo);
    recorded.recorded = fakeRecording (6.0);
    auto recordedSurround = recorded;
    recordedSurround.outputs = surround;
    auto lowCpu = recorded;
    lowCpu.latencySamples = ConvolutionReverb::lowCpuLatency (rate);

    std::printf ("Building a kernel (48 kHz)\n");
    benchBuild ("Stone Chapel, modelled, stereo", requestFor (1, stereo));
    benchBuild ("6 s recording, stereo", recorded);
    auto sized = recorded;
    sized.sizePercent = 130.0f;
    benchBuild ("6 s recording, stereo, Size 130%", sized);
    benchBuild ("6 s recording, 7.1.4", recordedSurround);

    std::printf ("\nPlaying (48 kHz, %.0f s of audio per case; time taken as a share of each block's length)\n", seconds);
    for (size_t block : { 64, 256, 1024 })
        benchPlay ("6 s recording, stereo", recorded, block, seconds);
    benchPlay ("6 s recording, stereo, living", recorded, 256, seconds, true);
    benchPlay ("6 s recording, stereo, Low CPU", lowCpu, 256, seconds);
    benchPlay ("6 s recording, 7.1.4", recordedSurround, 256, seconds);
    return 0;
}
