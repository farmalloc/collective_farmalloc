#pragma once

#include <farmalloc/page_size.hpp>

#include <errno.h>     // errno
#include <sys/mman.h>  // mmap, munmap

#include <bit>
#include <cassert>
#include <cstddef>
#include <new>
#include <system_error>


namespace FarMalloc
{

// given some natural number k, allocate size bytes starting from Alignment * k + Offset
template <size_t Alignment, size_t Offset>
inline void* AlignedMMap(const size_t size)
{
    assert(size > 0 && size % PageSize == 0);
    static_assert(Alignment > 0 && Alignment % PageSize == 0 && std::has_single_bit(Alignment));
    static_assert(Offset % PageSize == 0);
    constexpr size_t MMapPadding = Alignment - PageSize;
    const size_t mmap_size = size + MMapPadding;

    const auto mmap_result = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mmap_result == MAP_FAILED) [[unlikely]] {
        if (errno == ENOMEM) [[likely]] {
            throw std::bad_alloc{};
        }
        throw std::system_error{errno, std::generic_category(), "mmap"};
    }
    const auto mmap_head_addr = reinterpret_cast<uintptr_t>(mmap_result);
    const auto aligned_addr = (mmap_head_addr - Offset + Alignment - 1) / Alignment * Alignment + Offset;

    if (const auto cut = aligned_addr - mmap_head_addr; cut != 0) {
        if (munmap(reinterpret_cast<void*>(mmap_result), cut) == -1) [[unlikely]] {
            if (errno == ENOMEM) [[likely]] {
                throw std::bad_alloc{};
            }
            throw std::system_error{errno, std::generic_category(), "munmap"};
        }
    }
    if (const auto tail = aligned_addr + size, cut = mmap_head_addr + mmap_size - tail; cut != 0) {
        if (munmap(reinterpret_cast<void*>(tail), cut) == -1) [[unlikely]] {
            if (errno == ENOMEM) [[likely]] {
                throw std::bad_alloc{};
            }
            throw std::system_error{errno, std::generic_category(), "munmap"};
        }
    }

    return reinterpret_cast<void*>(aligned_addr);
}

inline void MUnmap(void* const ptr, const size_t size)
{
    if (munmap(ptr, size) == -1) [[unlikely]] {
        if (errno == ENOMEM) [[likely]] {
            throw std::bad_alloc{};
        }
        throw std::system_error{errno, std::generic_category(), "munmap"};
    }
}

}  // namespace FarMalloc
