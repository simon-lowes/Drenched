#pragma once

/*  Marks for Clang's RealtimeSanitizer (-fsanitize=realtime, Clang 20 and later; CI builds
    the tests with it, see .github/workflows/build.yml).

    DRENCHED_NONBLOCKING goes after a function's parameter list, before override:

        void process (...) noexcept DRENCHED_NONBLOCKING;

    In a RealtimeSanitizer build every call into such a function is checked as it runs:
    taking or freeing memory, a lock, or a system call that can block, anywhere beneath
    it, stops the program with the stack that did it. In every other build it is nothing,
    so the compilers that don't know the attribute never see it.

    ScopedRealtimeCheckOff turns the checks off on this thread while it lives, for the
    work a real-time function does only when it isn't real time: bouncing offline, where
    the host waits for every block and building a room in it is the point. */

#if defined (__has_feature)
 #if __has_feature (realtime_sanitizer)
  #define DRENCHED_REALTIME_SANITIZER 1
 #endif
#endif

#if defined (DRENCHED_REALTIME_SANITIZER)
 #include <sanitizer/rtsan_interface.h>
 #define DRENCHED_NONBLOCKING [[clang::nonblocking]]
#else
 #define DRENCHED_NONBLOCKING
#endif

namespace drenched
{

#if defined (DRENCHED_REALTIME_SANITIZER)
using ScopedRealtimeCheckOff = __rtsan::ScopedDisabler;
#else
struct ScopedRealtimeCheckOff
{
    ScopedRealtimeCheckOff() noexcept {}
    ScopedRealtimeCheckOff (const ScopedRealtimeCheckOff&) = delete;
    ScopedRealtimeCheckOff& operator= (const ScopedRealtimeCheckOff&) = delete;
};
#endif

} // namespace drenched
