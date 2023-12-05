#pragma once

#include <farmalloc/plain_suballoc.hpp>

#include <farmalloc/collective_allocator_params.hpp>

#include <cstddef>


namespace FarMalloc
{

struct PurelyLocalArenaAppendix {
};
struct PurelyLocalCustom {
    size_t occupied = 0;
    size_t capacity;
    const size_t orig_capacity;

    inline constexpr PurelyLocalCustom(size_t capacity) noexcept : capacity{capacity}, orig_capacity{capacity} {}
    inline void check_capacity(size_t size);
    inline constexpr void consume_capacity(size_t size) noexcept;
    inline constexpr void reclaim_capacity(size_t size) noexcept;
    inline constexpr void occupy_space(size_t size) noexcept;
    inline constexpr void reclaim_space(size_t size) noexcept;
    inline constexpr bool is_occupancy_under(double threshold) noexcept;

    inline constexpr size_t large_alloc_size(size_t size) noexcept { return size; }
    inline constexpr void postprocess_large_alloc(void*, size_t) noexcept {}
    inline constexpr void preprocess_large_dealloc(void*, size_t) noexcept {}
};
using PurelyLocalSuballocatorImpl = PlainSuballocatorImplBase<PlainSuballocatorArena<PurelyLocalArenaAppendix, PurelyLocalOffset>, PurelyLocalCustom>;

}  // namespace FarMalloc

#include <farmalloc/purely-local_suballocator.ipp>
