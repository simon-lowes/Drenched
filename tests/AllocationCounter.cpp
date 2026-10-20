// Replacement global allocation functions that count allocations on threads
// that ask for it, so tests can prove the audio path never allocates.

#include "TestHelpers.h"

#include <cstdlib>
#include <new>

namespace
{
    thread_local bool counting = false;
    thread_local std::size_t allocations = 0;

    void* allocate (std::size_t size)
    {
        if (counting)
            ++allocations;

        if (auto* p = std::malloc (size == 0 ? 1 : size))
            return p;

        throw std::bad_alloc();
    }

    void* allocateAligned (std::size_t size, std::align_val_t alignment)
    {
        if (counting)
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

    void releaseAligned (void* p) noexcept
    {
       #if defined (_MSC_VER)
        _aligned_free (p);
       #else
        std::free (p);
       #endif
    }
}

testing::AllocationCounter::AllocationCounter() { allocations = 0; counting = true; }
testing::AllocationCounter::~AllocationCounter() { counting = false; }
std::size_t testing::AllocationCounter::count() const { return allocations; }

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept { try { return allocate (size); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { try { return allocate (size); } catch (...) { return nullptr; } }
void* operator new (std::size_t size, std::align_val_t a) { return allocateAligned (size, a); }
void* operator new[] (std::size_t size, std::align_val_t a) { return allocateAligned (size, a); }

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { releaseAligned (p); }
