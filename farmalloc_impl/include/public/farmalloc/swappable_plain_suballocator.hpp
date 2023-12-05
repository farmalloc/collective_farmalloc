#pragma once

#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/plain_suballoc.hpp>
#include <farmalloc/plain_suballoc_page_metadata.hpp>
#include <farmalloc/local_memory_store.hpp>

#include <cstddef>


namespace FarMalloc
{

struct SwappablePlainArena : PlainSuballocatorArena<LocalMemoryStoreBuffer, SwappablePlainOffset> {
    using Base = PlainSuballocatorArena<LocalMemoryStoreBuffer, SwappablePlainOffset>;

    inline SwappablePlainArena(FreePageLink& link);
    inline ~SwappablePlainArena();

    inline static SwappablePlainArena& create(FreePageLink& link);
    inline static SwappablePlainArena& from_inside_ptr(const void* ptr) noexcept;
};


struct SwappablePlainCustom {
    inline constexpr SwappablePlainCustom() noexcept = default;
    inline constexpr void check_capacity(size_t) noexcept {}
    inline constexpr void consume_capacity(size_t) noexcept {}
    inline constexpr void reclaim_capacity(size_t) noexcept {}
    inline constexpr void occupy_space(size_t) noexcept {}
    inline constexpr void reclaim_space(size_t) noexcept {}
    inline constexpr bool is_occupancy_under(double) noexcept { return false; }

    inline constexpr size_t large_alloc_size(size_t size) noexcept;
    inline void postprocess_large_alloc(void* ptr, size_t size);
    inline void preprocess_large_dealloc(void* ptr, size_t size);
};
using SwappablePlainSuballocatorImpl = PlainSuballocatorImplBase<SwappablePlainArena, SwappablePlainCustom>;

}  // namespace FarMalloc

#include <farmalloc/swappable_plain_suballocator.ipp>
