#include "drenched/SweepCapture.h"

#include <pffft/pffft.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace drenched
{

namespace
{
    constexpr double leadSeconds = 0.005;
    constexpr double fadeInOctaves = 1.0 / 3.0;
    constexpr double fadeOutOctaves = 1.0 / 24.0;
    constexpr double minimumFadeSeconds = 0.005;

    // Regularisation of the spectral division, relative to the sweep's
    // loudest bin: tiny inside the sweep's range so the response there is
    // exact, large outside it so frequencies the sweep never played are
    // rolled off instead of amplified noise.
    constexpr double inBandRegularisation = 1.0e-5;
    constexpr double outOfBandRegularisation = 0.1;

    double raisedCosine (double t) noexcept
    {
        t = std::clamp (t, 0.0, 1.0);
        return 0.5 - 0.5 * std::cos (std::numbers::pi * t);
    }

    std::size_t fftSizeFor (std::size_t length)
    {
        std::size_t n = 32;

        while (n < length)
            n *= 2;

        return n;
    }

    struct Fft
    {
        explicit Fft (std::size_t n)
            : size (n),
              setup (pffft_new_setup (static_cast<int> (n), PFFFT_REAL), pffft_destroy_setup),
              work (static_cast<float*> (pffft_aligned_malloc (n * sizeof (float))), pffft_aligned_free)
        {
            if (setup == nullptr || work == nullptr)
                throw std::bad_alloc();
        }

        using Buffer = std::unique_ptr<float, void (*) (void*)>;

        Buffer buffer() const
        {
            Buffer b (static_cast<float*> (pffft_aligned_malloc (size * sizeof (float))), pffft_aligned_free);

            if (b == nullptr)
                throw std::bad_alloc();

            std::fill (b.get(), b.get() + size, 0.0f);
            return b;
        }

        void forward (float* data) { pffft_transform_ordered (setup.get(), data, data, work.get(), PFFFT_FORWARD); }
        void backward (float* data) { pffft_transform_ordered (setup.get(), data, data, work.get(), PFFFT_BACKWARD); }

        std::size_t size;
        std::unique_ptr<PFFFT_Setup, void (*) (PFFFT_Setup*)> setup;
        Buffer work;
    };
}

//==============================================================================
SineSweep::SineSweep (const SweepSettings& settings)
    : used (settings)
{
    if (! (used.sampleRate > 0.0) || ! (used.sweepSeconds > 0.0) || used.tailSeconds < 0.0)
        throw std::invalid_argument ("The sweep needs a sample rate and a length.");

    used.endHz = std::min (used.endHz, used.sampleRate * 0.45);
    used.startHz = std::clamp (used.startHz, 1.0, used.endHz / 2.0);

    const auto sr = used.sampleRate;
    const auto length = static_cast<std::size_t> (std::round (used.sweepSeconds * sr));
    const auto rate = std::log (used.endHz / used.startHz);
    const auto octaveSeconds = used.sweepSeconds * std::numbers::ln2 / rate;
    const auto fadeIn = std::max (minimumFadeSeconds, fadeInOctaves * octaveSeconds) * sr;
    const auto fadeOut = std::max (minimumFadeSeconds, fadeOutOctaves * octaveSeconds) * sr;
    const auto scale = used.sweepSeconds / rate;
    const auto level = std::pow (10.0, used.levelDb / 20.0);

    sweep.resize (length);

    for (std::size_t n = 0; n < length; ++n)
    {
        const auto t = static_cast<double> (n) / sr;
        const auto phase = 2.0 * std::numbers::pi * used.startHz * scale * (std::exp (t / scale) - 1.0);
        const auto fade = raisedCosine (static_cast<double> (n) / fadeIn)
                        * raisedCosine (static_cast<double> (length - 1 - n) / fadeOut);
        sweep[n] = static_cast<float> (level * fade * std::sin (phase));
    }

    tail = static_cast<std::size_t> (std::round (used.tailSeconds * sr));
    lead = static_cast<std::size_t> (std::min (leadSeconds, harmonicLeadSeconds (2) / 4.0) * sr);
}

double SineSweep::harmonicLeadSeconds (int order) const noexcept
{
    return used.sweepSeconds * std::log (static_cast<double> (std::max (order, 1))) / std::log (used.endHz / used.startHz);
}

std::vector<float> SineSweep::deconvolve (std::span<const float> recording) const
{
    // Every lag from -sweepLength (where distortion lands) to segmentLength
    // fits without wrapping onto the response.
    const auto n = fftSizeFor (segmentLength() + sweepLength());
    Fft fft (n);

    auto played = fft.buffer();
    std::copy (sweep.begin(), sweep.end(), played.get());
    fft.forward (played.get());

    auto heard = fft.buffer();
    std::copy_n (recording.begin(), std::min (recording.size(), segmentLength()), heard.get());
    fft.forward (heard.get());

    const auto binHz = used.sampleRate / static_cast<double> (n);
    const auto nyquist = used.sampleRate / 2.0;
    const auto upperEdge = std::min (used.endHz * 1.25, nyquist);

    const auto powerAt = [&] (std::size_t k)
    {
        const auto* x = played.get();

        if (k == 0)
            return static_cast<double> (x[0]) * x[0];

        if (k == n / 2)
            return static_cast<double> (x[1]) * x[1];

        return static_cast<double> (x[2 * k]) * x[2 * k] + static_cast<double> (x[2 * k + 1]) * x[2 * k + 1];
    };

    double loudest = 0.0;

    for (std::size_t k = 0; k <= n / 2; ++k)
        loudest = std::max (loudest, powerAt (k));

    const auto regularisation = [&] (double f)
    {
        double inBand = 1.0;

        if (f < used.startHz)
            inBand = raisedCosine ((f - used.startHz / 2.0) / (used.startHz / 2.0));
        else if (f > used.endHz)
            inBand = upperEdge > used.endHz ? 1.0 - raisedCosine ((f - used.endHz) / (upperEdge - used.endHz)) : 0.0;

        return loudest * (inBandRegularisation + outOfBandRegularisation * (1.0 - inBand));
    };

    // Y conj(X) / (|X|^2 + e), scaled for pffft's unnormalised inverse.
    const auto norm = 1.0 / static_cast<double> (n);
    auto* y = heard.get();
    const auto* x = played.get();

    y[0] = static_cast<float> (y[0] * x[0] / (powerAt (0) + regularisation (0.0)) * norm);
    y[1] = static_cast<float> (y[1] * x[1] / (powerAt (n / 2) + regularisation (nyquist)) * norm);

    for (std::size_t k = 1; k < n / 2; ++k)
    {
        const double yr = y[2 * k], yi = y[2 * k + 1];
        const double xr = x[2 * k], xi = x[2 * k + 1];
        const auto g = norm / (powerAt (k) + regularisation (static_cast<double> (k) * binHz));
        y[2 * k] = static_cast<float> ((yr * xr + yi * xi) * g);
        y[2 * k + 1] = static_cast<float> ((yi * xr - yr * xi) * g);
    }

    fft.backward (y);

    std::vector<float> response (lead + tail);

    for (std::size_t m = 0; m < response.size(); ++m)
        response[m] = y[(m + n - lead) % n];

    return response;
}

//==============================================================================
void CaptureRecorder::prepare (std::shared_ptr<const SineSweep> sweep, int speakers, int microphones)
{
    if (sweep == nullptr || speakers < 1 || speakers > 2 || microphones < 1 || microphones > 2)
        throw std::invalid_argument ("A capture needs a sweep, one or two speakers and one or two microphones.");

    total = static_cast<std::size_t> (speakers) * sweep->segmentLength();
    takes.assign (static_cast<std::size_t> (microphones), std::vector<float> (total, 0.0f));
    rate = sweep->settings().sampleRate;
    source = std::move (sweep);
    numSpeakers = speakers;
    position.store (0, std::memory_order_relaxed);
    peak.store (0.0f, std::memory_order_relaxed);
    stopRequested.store (false, std::memory_order_relaxed);
    status.store (State::idle, std::memory_order_release);
}

void CaptureRecorder::prepareListening (double seconds, double sampleRate, int microphones)
{
    if (! (seconds > 0.0 && seconds <= 60.0) || ! (sampleRate >= 8000.0) || microphones < 1 || microphones > 2)
        throw std::invalid_argument ("Listening needs up to a minute, a sample rate and one or two microphones.");

    total = static_cast<std::size_t> (std::ceil (seconds * sampleRate));
    takes.assign (static_cast<std::size_t> (microphones), std::vector<float> (total, 0.0f));
    source.reset();
    numSpeakers = 0;
    rate = sampleRate;
    position.store (0, std::memory_order_relaxed);
    peak.store (0.0f, std::memory_order_relaxed);
    stopRequested.store (false, std::memory_order_relaxed);
    status.store (State::idle, std::memory_order_release);
}

void CaptureRecorder::start() noexcept
{
    if (total == 0 || state() == State::running)
        return;

    position.store (0, std::memory_order_relaxed);
    peak.store (0.0f, std::memory_order_relaxed);
    stopRequested.store (false, std::memory_order_relaxed);
    status.store (State::running, std::memory_order_release);
}

void CaptureRecorder::cancel() noexcept
{
    stopRequested.store (true, std::memory_order_release);
}

void CaptureRecorder::reset() noexcept
{
    stopRequested.store (false, std::memory_order_relaxed);
    status.store (State::idle, std::memory_order_release);
}

float CaptureRecorder::progress() const noexcept
{
    if (total == 0)
        return 0.0f;

    return static_cast<float> (position.load (std::memory_order_relaxed)) / static_cast<float> (total);
}

bool CaptureRecorder::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                               std::size_t numSamples) noexcept
{
    if (status.load (std::memory_order_acquire) != State::running)
        return false;

    if (stopRequested.load (std::memory_order_acquire))
    {
        status.store (State::cancelled, std::memory_order_release);
        return false;
    }

    const auto start = position.load (std::memory_order_relaxed);
    const auto count = std::min (numSamples, total - start);

    // Record first: the outputs may be the input buffers.
    auto loudest = peak.load (std::memory_order_relaxed);

    for (std::size_t mic = 0; mic < takes.size(); ++mic)
    {
        if (static_cast<int> (mic) >= numInputs)
            continue;

        const auto* in = inputs[mic];
        auto* out = takes[mic].data() + start;

        for (std::size_t i = 0; i < count; ++i)
        {
            out[i] = in[i];
            loudest = std::max (loudest, std::abs (in[i]));
        }
    }

    peak.store (loudest, std::memory_order_relaxed);

    if (source == nullptr)
    {
        // Listening for a clap: the speakers stay silent.
        for (int channel = 0; channel < numOutputs; ++channel)
            std::fill_n (outputs[channel], numSamples, 0.0f);
    }
    else
    {
        const auto& signal = source->signal();
        const auto segment = source->segmentLength();

        for (int channel = 0; channel < numOutputs; ++channel)
        {
            auto* out = outputs[channel];

            for (std::size_t i = 0; i < numSamples; ++i)
            {
                const auto at = start + i;
                const auto playing = i < count && static_cast<std::size_t> (channel) == at / segment;
                const auto offset = at % segment;
                out[i] = playing && offset < signal.size() ? signal[offset] : 0.0f;
            }
        }
    }

    position.store (start + count, std::memory_order_relaxed);

    if (start + count == total)
        status.store (State::finished, std::memory_order_release);

    return true;
}

//==============================================================================
std::vector<std::vector<float>> deconvolveCapture (const SineSweep& sweep,
                                                   const std::vector<std::vector<float>>& recordings,
                                                   int speakers)
{
    const auto segment = sweep.segmentLength();
    const auto mics = recordings.size();

    if (speakers < 1 || speakers > 2 || mics < 1 || mics > 2)
        throw std::invalid_argument ("A capture needs one or two speakers and one or two microphones.");

    for (const auto& r : recordings)
        if (r.size() < segment * static_cast<std::size_t> (speakers))
            throw std::invalid_argument ("The recording stopped before the sweep finished.");

    std::vector<std::vector<float>> channels;

    for (std::size_t s = 0; s < static_cast<std::size_t> (speakers); ++s)
        for (std::size_t m = 0; m < mics; ++m)
            channels.push_back (sweep.deconvolve (std::span<const float> (recordings[m]).subspan (s * segment, segment)));

    // Two speakers heard by one microphone: each speaker feeds both sides.
    if (speakers == 2 && mics == 1)
        channels = { channels[0], channels[0], channels[1], channels[1] };

    return channels;
}

} // namespace drenched
