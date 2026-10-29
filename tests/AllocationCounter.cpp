// Replacement global allocation functions that count allocations and frees on
// threads that ask for it, so tests can prove the audio path never does either.
// With glibc (and no sanitizer, which has its own), malloc and free are replaced
// too, so what JUCE allocates with them directly counts as well.

#include "TestHelpers.h"

#include <cerrno>
#include <cstdlib>
#include <new>

#if defined (__GLIBC__) && ! defined (__SANITIZE_ADDRESS__) && ! defined (__SANITIZE_THREAD__)
 #define DRENCHED_COUNTS_MALLOC 1
#else
 #define DRENCHED_COUNTS_MALLOC 0
#endif

namespace
{
    thread_local bool counting = false;
    thread_local std::size_t allocations = 0;
    thread_local std::size_t frees = 0;

    void* allocate (std::size_t size)
    {
        if (counting && ! DRENCHED_COUNTS_MALLOC)
            ++allocations;

        if (auto* p = std::malloc (size == 0 ? 1 : size))
            return p;

        throw std::bad_alloc();
    }

    void* allocateAligned (std::size_t size, std::align_val_t alignment)
    {
        if (counting && ! DRENCHED_COUNTS_MALLOC)
            ++allocations;

        const auto align = static_cast<std::size_t> (alignment);
        const auto rounded = (size + align - 1) / align * align;

       #if defined (_MSC_VER)
        if (auto* p = _aligned_malloc (rounded == 0 ? align : rounded, align))
            return p;
       #else
        if (auto* p = std::aligned_alloc (align, rounded == 0 ? align : rounded))
            return p;
       #endif

        throw std::bad_alloc();
    }

    void release (void* p) noexcept
    {
        if (counting && p != nullptr && ! DRENCHED_COUNTS_MALLOC)
            ++frees;

        std::free (p);
    }

    void releaseAligned (void* p) noexcept
    {
        if (counting && p != nullptr && ! DRENCHED_COUNTS_MALLOC)
            ++frees;

       #if defined (_MSC_VER)
        _aligned_free (p);
       #else
        std::free (p);
       #endif
    }
}

testing::AllocationCounter::AllocationCounter() { allocations = 0; frees = 0; counting = true; }
testing::AllocationCounter::~AllocationCounter() { counting = false; }
std::size_t testing::AllocationCounter::count() const { return allocations; }
std::size_t testing::AllocationCounter::freed() const { return frees; }
bool testing::AllocationCounter::seesMalloc() noexcept { return DRENCHED_COUNTS_MALLOC != 0; }

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept { try { return allocate (size); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { try { return allocate (size); } catch (...) { return nullptr; } }
void* operator new (std::size_t size, std::align_val_t a) { return allocateAligned (size, a); }
void* operator new[] (std::size_t size, std::align_val_t a) { return allocateAligned (size, a); }

void operator delete (void* p) noexcept { release (p); }
void operator delete[] (void* p) noexcept { release (p); }
void operator delete (void* p, std::size_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t) noexcept { release (p); }
void operator delete (void* p, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { releaseAligned (p); }

#if DRENCHED_COUNTS_MALLOC
extern "C"
{
    void* __libc_malloc (std::size_t);
    void* __libc_calloc (std::size_t, std::size_t);
    void* __libc_realloc (void*, std::size_t);
    void* __libc_memalign (std::size_t, std::size_t);
    void __libc_free (void*);

    void* malloc (std::size_t size) noexcept
    {
        if (counting)
            ++allocations;

        return __libc_malloc (size);
    }

    void* calloc (std::size_t count, std::size_t size) noexcept
    {
        if (counting)
            ++allocations;

        return __libc_calloc (count, size);
    }

    void* realloc (void* p, std::size_t size) noexcept
    {
        if (counting)
        {
            ++allocations;

            if (p != nullptr)
                ++frees;
        }

        return __libc_realloc (p, size);
    }

    void* memalign (std::size_t alignment, std::size_t size) noexcept
    {
        if (counting)
            ++allocations;

        return __libc_memalign (alignment, size);
    }

    void* aligned_alloc (std::size_t alignment, std::size_t size) noexcept
    {
        return memalign (alignment, size);
    }

    int posix_memalign (void** out, std::size_t alignment, std::size_t size) noexcept
    {
        if (alignment < sizeof (void*) || (alignment & (alignment - 1)) != 0)
            return EINVAL;

        auto* p = memalign (alignment, size);

        if (p == nullptr && size != 0)
            return ENOMEM;

        *out = p;
        return 0;
    }

    void free (void* p) noexcept
    {
        if (counting && p != nullptr)
            ++frees;

        __libc_free (p);
    }
}
#endif
