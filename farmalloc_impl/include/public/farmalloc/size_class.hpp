#pragma once

#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/page_size.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <ranges>


// almost same as jemalloc
namespace FarMalloc::SizeClass
{

inline constexpr size_t SmallestAllocSize = 16;
inline constexpr size_t NAllocClassesInDoublingSize = 4;  // every 4 classes, doubles in size
static_assert(std::has_single_bit(SmallestAllocSize));
static_assert(std::has_single_bit(NAllocClassesInDoublingSize));
static_assert(SmallestAllocSize <= PageSize);

inline constexpr size_t NAllocClasses = NAllocClassesInDoublingSize  // SmallestAllocSize * [1, 2, ..., NAllocClassesInDoublingSize]
                                        + (std::bit_width(PageSize / SmallestAllocSize) - 1) * NAllocClassesInDoublingSize
                                        - 1;  // exclude (PageSize * NAllocClassesInDoublingSize)

using SmallAllocSizeType = uint16_t;
constexpr std::array<SmallAllocSizeType, NAllocClasses> AllocClassIdx2SizeTab = [] {
    std::array<SmallAllocSizeType, NAllocClasses> res;
    size_t idx = 0;
    for (; idx < NAllocClassesInDoublingSize; idx++) {
        size_t size = SmallestAllocSize * (idx + 1);
        assert(size <= std::numeric_limits<SmallAllocSizeType>::max());
        res.at(idx) = static_cast<SmallAllocSizeType>(size);
    }
    for (size_t base = SmallestAllocSize * NAllocClassesInDoublingSize,
                delta = SmallestAllocSize,
                size = base;
         idx < NAllocClasses; base *= 2, delta *= 2) {

        for (size_t j = 0; j < NAllocClassesInDoublingSize && idx < NAllocClasses; j++, idx++) {
            size += delta;
            assert(size <= std::numeric_limits<SmallAllocSizeType>::max());
            res.at(idx) = static_cast<SmallAllocSizeType>(size);
        }
    }
    return res;
}();

inline constexpr size_t MaxSmallAllocSize = AllocClassIdx2SizeTab.back();

inline constexpr size_t alloc_class_idx2size(size_t class_idx) noexcept
{
    return AllocClassIdx2SizeTab[class_idx];
}


static_assert(NAllocClasses <= std::numeric_limits<uint8_t>::max());
constexpr std::array<uint8_t, MaxSmallAllocSize / SmallestAllocSize> Size2AllocClassIdxTab = [] {
    std::array<uint8_t, MaxSmallAllocSize / SmallestAllocSize> res;
    for (size_t idx = 0; idx < res.size(); idx++) {
        res.at(idx) = static_cast<uint8_t>(std::ranges::lower_bound(AllocClassIdx2SizeTab, SmallestAllocSize * (idx + 1)) - AllocClassIdx2SizeTab.begin());
    }
    return res;
}();

inline constexpr size_t alloc_size2class_idx(size_t size) noexcept
{
    assert(size != 0 && size <= MaxSmallAllocSize);
    return Size2AllocClassIdxTab[(size + SmallestAllocSize - 1) / SmallestAllocSize - 1];
}


using SlabNPagesType = uint8_t;
constexpr std::array<SlabNPagesType, NAllocClasses> AllocClassIdx2NPagesTab = [] {
    std::array<SlabNPagesType, NAllocClasses> res;
    for (size_t idx = 0; idx < res.size(); idx++) {
        size_t n_pages = std::lcm(AllocClassIdx2SizeTab.at(idx), PageSize) / PageSize;
        assert(n_pages <= std::numeric_limits<SlabNPagesType>::max());
        res.at(idx) = static_cast<SlabNPagesType>(n_pages);
    }
    return res;
}();

inline constexpr size_t alloc_class_idx2n_pages(size_t class_idx) noexcept
{
    return AllocClassIdx2NPagesTab[class_idx];
}


using SlabNSlotsType = uint16_t;
constexpr std::array<SlabNSlotsType, NAllocClasses> AllocClassIdx2NSlotsTab = [] {
    std::array<SlabNSlotsType, NAllocClasses> res;
    for (size_t idx = 0; idx < res.size(); idx++) {
        assert(AllocClassIdx2NPagesTab.at(idx) * PageSize % AllocClassIdx2SizeTab.at(idx) == 0);
        size_t n_slots = AllocClassIdx2NPagesTab.at(idx) * PageSize / AllocClassIdx2SizeTab.at(idx);
        assert(n_slots <= std::numeric_limits<SlabNSlotsType>::max());
        res.at(idx) = static_cast<SlabNSlotsType>(n_slots);
    }
    return res;
}();

inline constexpr size_t MaxSlabNSlots = std::ranges::max(AllocClassIdx2NSlotsTab);

inline constexpr size_t alloc_class_idx2n_slots(size_t class_idx) noexcept
{
    return AllocClassIdx2NSlotsTab[class_idx];
}


inline constexpr size_t page_class_idx2size(size_t class_idx) noexcept
{
    return AllocClassIdx2SizeTab[class_idx] * PageSize / SmallestAllocSize;
}
inline constexpr size_t page_alloc_size2class_idx(size_t size) noexcept
{
    assert(size != 0 && size <= MaxSmallAllocSize / SmallestAllocSize * PageSize && size % PageSize == 0);
    return Size2AllocClassIdxTab[size / PageSize - 1];
}


inline constexpr size_t MaxNPages = ArenaSize / PageSize - 1;  // at least 1 page for metadata

constexpr std::array<uint8_t, MaxNPages> PageFreeSize2ClassIdxTab = [] {
    std::array<uint8_t, MaxNPages> res;
    for (size_t class_idx = 0, n_pages = 1; n_pages <= res.size(); n_pages++) {
        if (page_class_idx2size(class_idx + 1) <= n_pages * PageSize) {
            class_idx++;
        }
        res.at(n_pages - 1) = static_cast<uint8_t>(class_idx);
    }
    return res;
}();

inline constexpr size_t page_free_size2class_idx(size_t size) noexcept
{
    assert(size != 0 && size <= MaxNPages * PageSize && size % PageSize == 0);
    return PageFreeSize2ClassIdxTab[size / PageSize - 1];
}

}  // namespace FarMalloc::SizeClass
