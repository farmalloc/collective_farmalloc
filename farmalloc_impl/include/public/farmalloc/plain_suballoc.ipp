#pragma once

#include <farmalloc/plain_suballoc.hpp>

#include <farmalloc/aligned_mmap.hpp>
#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/size_class.hpp>
#include <util/ssize_t.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <numeric>
#include <type_traits>
#include <utility>


namespace FarMalloc
{

template <class Appendix, size_t Max>
inline consteval size_t DataNPagesForEachPlainSuballocatorArena()
{
    static_assert(Max >= 1);
    if constexpr (sizeof(PlainSuballocatorArenaMetadata<Max, Appendix>) + Max * PageSize <= ArenaSize) {
        return Max;
    } else {
        return DataNPagesForEachPlainSuballocatorArena<Appendix, Max - 1>();
    }
}


template <class Appendix, size_t AlignOffset>
constexpr PlainSuballocatorArena<Appendix, AlignOffset>::PlainSuballocatorArena(FreePageLink& link) noexcept
{
    this->metadata_tab.front().used = true;
    metadata(0).used = false;
    metadata(0).free.n_pages = DataNPages;
    metadata(DataNPages - 1).used = false;
    metadata(DataNPages - 1).free.n_pages = DataNPages;
    link.insert_next(metadata(0).free.link);
    this->metadata_tab.back().used = true;
}
template <class Appendix, size_t AlignOffset>
void* PlainSuballocatorArena<Appendix, AlignOffset>::allocate_memory(size_t size)
{
    return AlignedMMap<SubspaceInterval, AlignOffset>(size);
}
template <class Appendix, size_t AlignOffset>
auto PlainSuballocatorArena<Appendix, AlignOffset>::create(FreePageLink& link) -> PlainSuballocatorArena&
{
    const auto arena_addr = allocate_memory();
    return *new (arena_addr) PlainSuballocatorArena{link};
}

template <class Appendix, size_t AlignOffset>
auto PlainSuballocatorArena<Appendix, AlignOffset>::from_inside_ptr(const void* ptr) noexcept -> PlainSuballocatorArena&
{
    return *reinterpret_cast<PlainSuballocatorArena*>(reinterpret_cast<uintptr_t>(ptr) / ArenaAlignment * ArenaAlignment);
}
template <class Appendix, size_t AlignOffset>
SSizeT PlainSuballocatorArena<Appendix, AlignOffset>::metadata_ptr2idx(const void* ptr) noexcept
{
    static_assert(std::is_standard_layout_v<PlainSuballocatorArena>);
    return static_cast<SSizeT>((reinterpret_cast<uintptr_t>(ptr) % ArenaAlignment) / sizeof(PlainSuballocatorPageMetadata)) - 1;
}
template <class Appendix, size_t AlignOffset>
SSizeT PlainSuballocatorArena<Appendix, AlignOffset>::data_ptr2idx(const void* ptr) noexcept
{
    return static_cast<SSizeT>((reinterpret_cast<uintptr_t>(ptr) % ArenaAlignment) / PageSize - MetadataNPages);
}
template <class Appendix, size_t AlignOffset>
uintptr_t PlainSuballocatorArena<Appendix, AlignOffset>::page_idx2head_ptr(SSizeT idx) noexcept
{
    return reinterpret_cast<uintptr_t>(this) + (MetadataNPages + idx) * PageSize;
}


template <class Arena, class Custom>
template <class... Args>
constexpr PlainSuballocatorImplBase<Arena, Custom>::PlainSuballocatorImplBase(Args&&... args) : current_slabs{}, custom(std::forward<Args>(args)...)
{
    for (auto& list : non_full_slabs) {
        list.prev = list.next = &list;
    }
    for (auto& list : free_pages) {
        list.prev = list.next = &list;
    }
}
template <class Arena, class Custom>
PlainSuballocatorImplBase<Arena, Custom>::~PlainSuballocatorImplBase()
{
    for (size_t class_idx = 0; class_idx < current_slabs.size(); class_idx++) {
        if (const auto current = current_slabs[class_idx]; current) {
            auto& arena = Arena::from_inside_ptr(current);
            auto page_idx = Arena::metadata_ptr2idx(current);
            deallocate_page(arena, page_idx, SizeClass::alloc_class_idx2n_pages(class_idx));
        }
    }
}

template <class Arena, class Custom>
template <size_t Alignment>
auto PlainSuballocatorImplBase<Arena, Custom>::allocate_page(const size_t n_pages) -> std::pair<Arena*, SSizeT>
{
    static_assert(0 < Alignment && Alignment <= Arena::ArenaAlignment);
    constexpr auto PageAlign = std::lcm(Alignment, PageSize) / PageSize;

    assert(0 < n_pages && n_pages <= Arena::MaxMediumAllocSize / PageSize);
    const auto size = n_pages * PageSize;
    custom.check_capacity(size);

    std::pair<Arena*, SSizeT> res;

    auto try_allocate = [&]<bool AlignCheck = false>(size_t class_idx)
    {
        if (const auto free_list = &free_pages[class_idx], first = free_list->next; first != free_list) {
            auto& arena = Arena::from_inside_ptr(first);

            const auto page_idx = Arena::metadata_ptr2idx(first);
            const auto succ_idx = page_idx + first->n_pages();
            const auto idx_unaligned = succ_idx - n_pages;
            const auto n_padding_pages = (idx_unaligned + Arena::MetadataNPages) % PageAlign;
            const auto idx_aligned = idx_unaligned - n_padding_pages;
            if (!AlignCheck || idx_aligned >= static_cast<size_t>(page_idx)) {
                const auto idx_tail = idx_aligned + n_pages - 1;
                if (n_padding_pages != 0) {
                    arena.metadata(succ_idx - 1).free.n_pages = n_padding_pages;
                    auto& new_succ = arena.metadata(idx_tail + 1);
                    new_succ.used = false;
                    new_succ.free.n_pages = n_padding_pages;
                    free_pages[SizeClass::page_free_size2class_idx(n_padding_pages * PageSize)].insert_next(new_succ.free.link);
                }
                free_list->next = first->next;
                free_list->next->prev = free_list;
                const auto left_n_pages = idx_aligned - page_idx;
                if (left_n_pages != 0) {
                    first->n_pages() = left_n_pages;
                    auto& left_tail = arena.metadata(idx_aligned - 1);
                    left_tail.used = false;
                    left_tail.free.n_pages = left_n_pages;
                    free_pages[SizeClass::page_free_size2class_idx(left_n_pages * PageSize)].insert_next(*first);
                }
                arena.metadata(idx_tail).used = true;
                arena.metadata(idx_aligned).used = true;
                res = {&arena, static_cast<SSizeT>(idx_aligned)};
                custom.consume_capacity(size);
                return true;
            }
        }
        return false;
    };

    if (PageAlign != 1) {
        if (try_allocate.template operator()<true>(SizeClass::page_alloc_size2class_idx(size))) {
            return res;
        }
    }
    for (auto class_idx = SizeClass::page_alloc_size2class_idx(size + (PageAlign - 1) * PageSize); class_idx < free_pages.size(); class_idx++) {
        if (try_allocate(class_idx)) {
            return res;
        }
    }
    // custom.check_capacity(size + Arena::MetadataNPages * PageSize);
    // custom.consume_capacity(Arena::MetadataNPages * PageSize);
    // custom.occupy_space(Arena::MetadataNPages * PageSize);
    Arena::create(free_pages.back());
    [[maybe_unused]] const auto success = try_allocate(Arena::NPageClasses - 1);
    assert(success);
    return res;
}
template <class Arena, class Custom>
void PlainSuballocatorImplBase<Arena, Custom>::deallocate_page(Arena& arena, SSizeT idx, size_t n_pages) noexcept
{
    custom.reclaim_capacity(n_pages * PageSize);
    if (auto& next = arena.metadata(idx + n_pages); !next.used) {
        next.free.link.remove_from_list();
        n_pages += next.free.n_pages;
    }
    if (auto& prev_tail = arena.metadata(idx - 1); !prev_tail.used) {
        auto& prev = arena.metadata(idx - prev_tail.free.n_pages);
        prev.free.link.remove_from_list();
        idx -= prev_tail.free.n_pages;
        n_pages += prev.free.n_pages;
    }
    if (idx == 0 && n_pages == Arena::DataNPages) {
        arena.~Arena();
        custom.reclaim_capacity(Arena::MetadataNPages * PageSize);
        custom.reclaim_space(Arena::MetadataNPages * PageSize);
        MUnmap(&arena, ArenaSize);
    } else {
        auto &self = arena.metadata(idx), &self_tail = arena.metadata(idx + n_pages - 1);
        self.used = self_tail.used = false;
        self.free.n_pages = self_tail.free.n_pages = n_pages;
        free_pages[SizeClass::page_free_size2class_idx(n_pages * PageSize)].insert_next(self.free.link);
    }
}

template <class Arena, class Custom>
template <size_t ElemSize, size_t Alignment>
void* PlainSuballocatorImplBase<Arena, Custom>::allocate(const size_t n_elems)
{
    const auto size = ElemSize * n_elems;
    if (size <= SizeClass::MaxSmallAllocSize) {
        const auto class_idx = SizeClass::alloc_size2class_idx(size);
        auto res = [&] {
            do {
                if (const auto current = current_slabs[class_idx]; current != nullptr) {
                    if (const auto slot_idx = current->allocated.find_unset_and_set(SizeClass::alloc_class_idx2n_slots(class_idx)); slot_idx != -1) {
                        const auto current_idx = Arena::metadata_ptr2idx(current);
                        if constexpr (Alignment > PageSize) {
                            static_assert(Alignment == PageSize * 2);
                            static_assert(ElemSize == PageSize * 2);
                            assert(n_elems == 1);
                            static_assert(SizeClass::alloc_class_idx2n_slots(SizeClass::alloc_size2class_idx(ElemSize)) == 1);
                            if ((current_idx + Arena::MetadataNPages) % 2 != 0) {
                                deallocate_page(Arena::from_inside_ptr(current), current_idx, 2);
                                break;
                            }
                        }
                        auto& arena = Arena::from_inside_ptr(current);
                        return reinterpret_cast<void*>(arena.page_idx2head_ptr(current_idx)
                                                       + SizeClass::alloc_class_idx2size(class_idx) * slot_idx);
                    }
                    current->link.next = nullptr;
                }
                if (const auto non_full_list = &non_full_slabs[class_idx], first = non_full_list->next; first != non_full_list) {
                    non_full_list->next = first->next;
                    non_full_list->next->prev = non_full_list;
                    auto& slab = first->slab();
                    current_slabs[class_idx] = &slab;
                    const auto slot_idx = slab.allocated.find_unset_and_set(SizeClass::alloc_class_idx2n_slots(class_idx));
                    auto& arena = Arena::from_inside_ptr(first);
                    return reinterpret_cast<void*>(arena.page_idx2head_ptr(Arena::metadata_ptr2idx(first))
                                                   + SizeClass::alloc_class_idx2size(class_idx) * slot_idx);
                }
            } while (false);
            const auto n_pages = SizeClass::alloc_class_idx2n_pages(class_idx);
            auto [p_arena, page_idx] = allocate_page<Alignment>(n_pages);
            auto* const p_slab = std::construct_at(&p_arena->metadata(page_idx).slab, 0);
            current_slabs[class_idx] = p_slab;
            for (unsigned idx = 0; idx < n_pages; idx++) {
                p_arena->metadata(page_idx + idx).slab.idx_in_slab = idx;
            }
            return reinterpret_cast<void*>(p_arena->page_idx2head_ptr(page_idx));
        }();
        custom.occupy_space(SizeClass::alloc_class_idx2size(class_idx));
        return res;

    } else if (size <= Arena::MaxMediumAllocSize) {
        const size_t n_pages = (size + PageSize - 1) / PageSize;
        auto [p_arena, page_idx] = allocate_page<Alignment>(n_pages);
        custom.occupy_space(n_pages * PageSize);
        return reinterpret_cast<void*>(p_arena->page_idx2head_ptr(page_idx));

    } else {
        if (Alignment > Arena::ArenaAlignment) {
            throw std::bad_alloc{};
        }
        const size_t aug_size = custom.large_alloc_size(size);
        const auto page_aligned_size = (aug_size + PageSize - 1) / PageSize * PageSize;
        custom.check_capacity(page_aligned_size);
        custom.consume_capacity(page_aligned_size);
        custom.occupy_space(page_aligned_size);
        const auto res = Arena::allocate_memory(page_aligned_size);
        custom.postprocess_large_alloc(res, aug_size);
        return res;
    }
}
template <class Arena, class Custom>
template <size_t ElemSize, size_t Alignment>
void PlainSuballocatorImplBase<Arena, Custom>::deallocate(void* const ptr, const size_t n_elems)
{
    const auto size = ElemSize * n_elems;
    if (size <= SizeClass::MaxSmallAllocSize) {
        auto& arena = Arena::from_inside_ptr(ptr);
        auto page_idx = Arena::data_ptr2idx(ptr);
        page_idx -= arena.metadata(page_idx).slab.idx_in_slab;
        const auto class_idx = SizeClass::alloc_size2class_idx(size);
        const auto slot_idx = (reinterpret_cast<uintptr_t>(ptr) - arena.page_idx2head_ptr(page_idx)) / SizeClass::alloc_class_idx2size(class_idx);
        auto& slab = arena.metadata(page_idx).slab;
        slab.allocated.flip(slot_idx);
        custom.reclaim_space(SizeClass::alloc_class_idx2size(class_idx));
        if (&slab != current_slabs[class_idx]) {
            if (slab.allocated.is_empty()) {
                if (slab.link.next != nullptr) {
                    slab.link.remove_from_list();
                }
                deallocate_page(arena, page_idx, SizeClass::alloc_class_idx2n_pages(class_idx));
            } else if (slab.link.next == nullptr) {
                non_full_slabs[class_idx].insert_prev(slab.link);
            }
        }

    } else if (size <= Arena::MaxMediumAllocSize) {
        auto& arena = Arena::from_inside_ptr(ptr);
        auto page_idx = Arena::data_ptr2idx(ptr);
        const size_t n_pages = (size + PageSize - 1) / PageSize;
        custom.reclaim_space(n_pages * PageSize);
        deallocate_page(arena, page_idx, n_pages);

    } else {
        const size_t aug_size = custom.large_alloc_size(size);
        custom.preprocess_large_dealloc(ptr, aug_size);
        const auto page_aligned_size = (aug_size + PageSize - 1) / PageSize * PageSize;
        custom.reclaim_capacity(page_aligned_size);
        custom.reclaim_space(page_aligned_size);
        MUnmap(ptr, page_aligned_size);
    }
}

template <class Arena, class Custom>
constexpr bool PlainSuballocatorImplBase<Arena, Custom>::is_occupancy_under(double threshold) noexcept
{
    return custom.is_occupancy_under(threshold);
}


template <class Impl>
template <size_t ElemSize, size_t Alignment>
void* PlainSuballocator<Impl>::allocate(const size_t n_elems)
{
    return pimpl->template allocate<ElemSize, Alignment>(n_elems);
}
template <class Impl>
template <size_t ElemSize, size_t Alignment>
void PlainSuballocator<Impl>::deallocate(void* const p, const size_t n_elems)
{
    return pimpl->template deallocate<ElemSize, Alignment>(p, n_elems);
}
template <class Impl>
constexpr bool PlainSuballocator<Impl>::is_occupancy_under(double threshold) noexcept
{
    return pimpl->is_occupancy_under(threshold);
}

}  // namespace FarMalloc
