#pragma once

#include <farmalloc/size_class.hpp>

#include <array>
#include <cstddef>
#include <cstdint>


namespace FarMalloc
{

struct SlabBitmap {
    inline static constexpr size_t NInteger = (SizeClass::MaxSlabNSlots + 63) / 64;
    std::array<uint64_t, NInteger> data;

    inline constexpr SlabBitmap() noexcept = default;
    // initialize bitmap and allocate the first slot
    inline constexpr SlabBitmap(int) noexcept;

    inline constexpr int find_unset_and_set(size_t n_slots) noexcept;
    inline constexpr void flip(const size_t slot_idx) noexcept;
    inline constexpr bool is_empty() const noexcept;
};
struct SlabMetadata;
struct SlabLink {
    SlabLink* next;
    SlabLink* prev;

    inline constexpr void insert_prev(SlabLink& to_be_prev) noexcept;
    inline constexpr void remove_from_list() noexcept;
    inline SlabMetadata& slab() noexcept;
};
struct SlabMetadata {
    SlabLink link;
    SlabBitmap allocated;
    unsigned idx_in_slab;

    inline constexpr SlabMetadata() noexcept = default;
    inline constexpr SlabMetadata(int) noexcept : allocated{0} {}
    SlabMetadata(const SlabMetadata&) = delete;
};


struct FreePageLink {
    FreePageLink* next;
    FreePageLink* prev;

    inline constexpr void insert_next(FreePageLink& to_be_next) noexcept;
    inline constexpr void remove_from_list() noexcept;
    inline size_t& n_pages() noexcept;
};
struct FreePageMetadata {
    FreePageLink link;
    size_t n_pages;
};


struct alignas(64) PlainSuballocatorPageMetadata {
    bool used;

    union {
        FreePageMetadata free;
        SlabMetadata slab;
    };
};

}  // namespace FarMalloc

#include <farmalloc/plain_suballoc_page_metadata.ipp>
