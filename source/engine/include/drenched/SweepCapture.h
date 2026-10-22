#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace drenched
{

/** How a space is measured: a sine sweep played from a speaker, and how long
    to keep recording afterwards while the room rings out. */
struct SweepSettings
{
    double sampleRate = 48000.0;
    double sweepSeconds = 10.0;
    double tailSeconds = 6.0;   // should be longer than the room's decay
    double startHz = 20.0;
    double endHz = 20000.0;     // limited to 0.45 of the sample rate
    float levelDb = -12.0f;     // peak level of the sweep
};

/** An exponential sine sweep (Farina, 2000) and its deconvolution.

    The sweep's frequency rises exponentially, so it spends equal time in
    every octave. Deconvolving a recording of it gives the room's impulse
    response, and any harmonic distortion from the speaker comes out as
    separate copies that land before the impulse response rather than on top
    of it, where they are simply cut away.

    Construction and deconvolve() allocate; never on the audio thread. */
class SineSweep
{
public:
    explicit SineSweep (const SweepSettings& settings);

    const SweepSettings& settings() const noexcept { return used; }

    /** The sweep itself, faded in and out, at settings().levelDb. */
    const std::vector<float>& signal() const noexcept { return sweep; }

    std::size_t sweepLength() const noexcept { return sweep.size(); }
    std::size_t tailLength() const noexcept { return tail; }

    /** A sweep and the silence after it: what each speaker plays. */
    std::size_t segmentLength() const noexcept { return sweep.size() + tail; }

    /** Samples of the result that come before the moment of arrival, so the
        start of the direct sound is never cut. */
    std::size_t leadLength() const noexcept { return lead; }

    /** How far before the impulse response the distortion of a given
        harmonic order lands, in seconds: T ln(order) / ln(end / start). */
    double harmonicLeadSeconds (int order) const noexcept;

    /** Turns a recording of one sweep (starting when it started playing, and
        segmentLength() long; shorter is zero padded) into the impulse
        response: leadLength() + tailLength() samples, the direct sound
        leadLength() samples in, plus any delay in the recording. Outside the
        sweep's range the response is rolled off rather than amplified. */
    std::vector<float> deconvolve (std::span<const float> recording) const;

private:
    SweepSettings used;
    std::vector<float> sweep;
    std::size_t tail = 0, lead = 0;
};

/** Plays a sweep from each speaker in turn while recording every microphone,
    inside the audio callback; or, for a clap, records with the speakers
    silent.

    prepare() allocates and must not overlap process(): call it before
    start(), or when state() is not running. Everything else is real-time
    safe and may be called from any thread. */
class CaptureRecorder
{
public:
    enum class State { idle, running, finished, cancelled };

    /** Readies a take: speakers (1 or 2) play the sweep one after the other,
        microphones (1 or 2) are recorded throughout. */
    void prepare (std::shared_ptr<const SineSweep> sweep, int speakers, int microphones);

    /** Readies a take that only listens: `seconds` of the microphones (1 or
        2) with every speaker silent, for a clap (see extractClap()). */
    void prepareListening (double seconds, double sampleRate, int microphones);

    /** Starts the prepared take. */
    void start() noexcept;

    /** Stops a running take; process() moves it to cancelled. */
    void cancel() noexcept;

    /** Back to idle. Only when process() is not running, for example while
        the host is preparing to play. */
    void reset() noexcept;

    /** While a take runs, records the inputs, then overwrites the outputs
        with the sweep (silence on the speakers not playing) and returns true.
        Otherwise leaves the buffers alone and returns false. Inputs and
        outputs may be the same buffers. */
    bool process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                  std::size_t numSamples) noexcept;

    State state() const noexcept { return status.load (std::memory_order_acquire); }

    /** How far through the take, 0 to 1. */
    float progress() const noexcept;

    /** The loudest input sample so far; near 1 means the recording clipped. */
    float inputPeak() const noexcept { return peak.load (std::memory_order_relaxed); }

    int speakers() const noexcept { return numSpeakers; }

    /** The sweep that played, or null for a take that only listened. */
    const std::shared_ptr<const SineSweep>& sweep() const noexcept { return source; }

    double sampleRate() const noexcept { return rate; }

    /** One recording per microphone; read only once state() is finished. */
    const std::vector<std::vector<float>>& recordings() const noexcept { return takes; }

private:
    std::shared_ptr<const SineSweep> source;
    std::vector<std::vector<float>> takes;
    int numSpeakers = 1;
    double rate = 48000.0;
    std::size_t total = 0;
    std::atomic<std::size_t> position { 0 };
    std::atomic<State> status { State::idle };
    std::atomic<bool> stopRequested { false };
    std::atomic<float> peak { 0.0f };
};

/** Deconvolves a finished take into the channels prepareRecordedResponse()
    takes: one speaker gives one channel per microphone; two speakers and two
    microphones give the four true-stereo paths, left speaker first. */
std::vector<std::vector<float>> deconvolveCapture (const SineSweep& sweep,
                                                   const std::vector<std::vector<float>>& recordings,
                                                   int speakers);

} // namespace drenched
