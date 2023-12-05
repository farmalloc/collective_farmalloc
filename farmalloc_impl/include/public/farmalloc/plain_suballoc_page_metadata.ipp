#pragma once

#include <farmalloc/plain_suballoc_page_metadata.hpp>

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>


namespace FarMalloc
{

constexpr SlabBitmap::SlabBitmap(int) noexcept : data{}
{
    data[0] ^= 1u;
}
constexpr int SlabBitmap::find_unset_and_set(size_t n_slots) noexcept
{
    constexpr uint64_t AllOne = ~static_cast<uint64_t>(0);
    int idx_int = 0;
    for (; n_slots >= 64; idx_int++, n_slots -= 64) {
        const auto datum = data[idx_int];
        if (datum != AllOne) {
            const auto pos = std::countr_one(datum);
            const auto lsb_zero = (datum + 1) & ~datum;
            data[idx_int] = datum ^ lsb_zero;
            return idx_int * 64 + pos;
        }
    }
    if (n_slots != 0) {
        const auto datum = data[idx_int] | (AllOne << n_slots);
        if (datum != AllOne) {
            const auto pos = std::countr_one(datum);
            const auto lsb_zero = (datum + 1) & ~datum;
            data[idx_int] = datum ^ lsb_zero;
            return idx_int * 64 + pos;
        }
    }
    return -1;
}
constexpr void SlabBitmap::flip(const size_t slot_idx) noexcept
{
    data[slot_idx / 64] ^= uint64_t{1} << (slot_idx % 64);
}
constexpr bool SlabBitmap::is_empty() const noexcept
{
    uint64_t tmp = data[0];
    for (size_t idx_int = 1; idx_int < data.size(); idx_int++) {
        tmp |= data[idx_int];
    }
    return tmp == 0;
}


constexpr void SlabLink::insert_prev(SlabLink& to_be_prev) noexcept
{
    to_be_prev.next = this;
    to_be_prev.prev = prev;
    prev->next = &to_be_prev;
    prev = &to_be_prev;
}
constexpr void SlabLink::remove_from_list() noexcept
{
    prev->next = next;
    next->prev = prev;
}
SlabMetadata& SlabLink::slab() noexcept
{
    static_assert(std::is_standard_layout_v<SlabMetadata>);
    static_assert(offsetof(SlabMetadata, link) == 0);
    return *reinterpret_cast<SlabMetadata*>(this);
}


constexpr void FreePageLink::remove_from_list() noexcept
{
    prev->next = next;
    next->prev = prev;
}
constexpr void FreePageLink::insert_next(FreePageLink& to_be_next) noexcept
{
    to_be_next.prev = this;
    to_be_next.next = next;
    next->prev = &to_be_next;
    next = &to_be_next;
}
size_t& FreePageLink::n_pages() noexcept
{
    static_assert(std::is_standard_layout_v<FreePageMetadata>);
    static_assert(offsetof(FreePageMetadata, link) == 0);
    return reinterpret_cast<FreePageMetadata*>(this)->n_pages;
}

}  // namespace FarMalloc
