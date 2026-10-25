#pragma once

// Internal to the engine.

#include <cstdint>

#if defined (__SSE__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 1)
 #include <xmmintrin.h>
 #define DRENCHED_SSE_DENORMALS 1
#endif

namespace drenched
{

/** Flushes denormal floats to zero on this thread while it lives, then puts
    the thread's setting back. The offline work (preparing a file, measuring
    decays, matching a room, building kernels) filters long tails that fade
    below 1e-38, and on x86 every operation on such a number is many times
    slower: unflushed, a 30-second response took four times as long to load.
    The audio thread gets the same from the plugin's processBlock. */
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept : saved (read()) { write (saved | mask); }
    ~ScopedFlushDenormals() { write (saved); }

    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
   #if DRENCHED_SSE_DENORMALS
    static constexpr std::uint64_t mask = 0x8040; // flush to zero, denormals are zero
    static std::uint64_t read() noexcept { return _mm_getcsr(); }
    static void write (std::uint64_t v) noexcept { _mm_setcsr (static_cast<unsigned int> (v)); }
   #elif defined (__aarch64__)
    static constexpr std::uint64_t mask = 1ull << 24; // FPCR.FZ
    static std::uint64_t read() noexcept { std::uint64_t v; asm volatile ("mrs %0, fpcr" : "=r" (v)); return v; }
    static void write (std::uint64_t v) noexcept { asm volatile ("msr fpcr, %0" : : "r" (v)); }
   #else
    static constexpr std::uint64_t mask = 0;
    static std::uint64_t read() noexcept { return 0; }
    static void write (std::uint64_t) noexcept {}
   #endif

    std::uint64_t saved;
};

} // namespace drenched
