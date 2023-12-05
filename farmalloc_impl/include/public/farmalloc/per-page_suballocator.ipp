#pragma once

#include <farmalloc/per-page_suballocator.ipp>

#include <farmalloc/aligned_mmap.hpp>
#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/local_memory_store.hpp>

#include <bit>
#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>


namespace FarMalloc
{

template <size_t BlockSize>
constexpr void PerPageArenaLink<BlockSize>::insert_prev(PerPageArenaLink& to_be_prev) noexcept
{
    to_be_prev.next = this;
    to_be_prev.prev = prev;
    prev->next = &to_be_prev;
    prev = &to_be_prev;
}
template <size_t BlockSize>
constexpr void PerPageArenaLink<BlockSize>::remove_from_list() noexcept
{
    prev->next = next;
    next->prev = prev;
}

template <size_t BlockSize>
auto PerPageArenaLink<BlockSize>::arena() noexcept -> Arena&
{
    static_assert(std::is_standard_layout_v<Arena>);
    static_assert(offsetof(Arena, link) == 0);
    return *reinterpret_cast<Arena*>(this);
}


template <size_t BlockSize, size_t Max>
consteval size_t NBlocksForPerPageSuballocatorArena()
{
    static_assert(Max >= 1);
    if constexpr (sizeof(PerPageArenaMetadata<BlockSize, Max>) + (BlockSize * Max + PageSize - 1) / PageSize * PageSize <= ArenaSize) {
        return Max;
    } else {
        return NBlocksForPerPageSuballocatorArena<BlockSize, Max - 1>();
    }
}


template <size_t BlockSize>
PerPageSuballocatorArena<BlockSize>::PerPageSuballocatorArena(Base::BlockAllocator& block_alloc) : Base{block_alloc}
{
    auto* const store = this->store_buf.construct(DataNPages * PageSize);
    LocalMemoryStore::umap(reinterpret_cast<void*>(block_idx2head_ptr(0)), DataNPages * PageSize, store);

    if (NBlocks % 64 != 0) {
        this->is_block_used.back() = ~uint64_t{0} << (NBlocks % 64);
    }
    this->is_block_used.front() ^= 1u;

    this->num_of_used_blocks = 1;
}

template <size_t BlockSize>
auto PerPageSuballocatorArena<BlockSize>::create(Base::BlockAllocator& block_alloc) -> PerPageSuballocatorArena&
{
    const auto arena_addr = AlignedMMap<SubspaceInterval, PerPageOffset>(ArenaSize);
    return *new (arena_addr) PerPageSuballocatorArena{block_alloc};
}

template <size_t BlockSize>
PerPageSuballocatorArena<BlockSize>::~PerPageSuballocatorArena()
{
    LocalMemoryStore::uunmap(reinterpret_cast<void*>(block_idx2head_ptr(0)), DataNPages * PageSize);
    this->store_buf.destroy(DataNPages * PageSize);
}

template <size_t BlockSize>
auto PerPageSuballocatorArena<BlockSize>::from_inside_ptr(const void* ptr) noexcept -> PerPageSuballocatorArena&
{
    return *reinterpret_cast<PerPageSuballocatorArena*>(reinterpret_cast<uintptr_t>(ptr) / ArenaAlignment * ArenaAlignment);
}
template <size_t BlockSize>
size_t PerPageSuballocatorArena<BlockSize>::data_ptr2idx(const void* ptr) noexcept
{
    return static_cast<size_t>((reinterpret_cast<uintptr_t>(ptr) % ArenaAlignment - MetadataNPages * PageSize) / BlockSize);
}
template <size_t BlockSize>
uintptr_t PerPageSuballocatorArena<BlockSize>::block_idx2head_ptr(size_t idx) noexcept
{
    return reinterpret_cast<uintptr_t>(this) + MetadataNPages * PageSize + idx * BlockSize;
}

template <size_t BlockSize>
constexpr int PerPageSuballocatorArena<BlockSize>::find_free_and_allocate() noexcept
{
    constexpr uint64_t AllOne = ~static_cast<uint64_t>(0);
    for (int idx_int = 0; idx_int < Base::BitmapWidth; idx_int++) {
        const auto datum = this->is_block_used[idx_int];
        if (datum != AllOne) {
            const auto pos = std::countr_one(datum);
            const auto lsb_zero = (datum + 1) & ~datum;
            this->is_block_used[idx_int] = datum ^ lsb_zero;
            this->num_of_used_blocks++;
            return idx_int * 64 + pos;
        }
    }
    return -1;
}
template <size_t BlockSize>
constexpr void PerPageSuballocatorArena<BlockSize>::free(const size_t block_idx) noexcept
{
    this->num_of_used_blocks--;
    this->is_block_used[block_idx / 64] ^= uint64_t{1} << (block_idx % 64);
}
template <size_t BlockSize>
constexpr bool PerPageSuballocatorArena<BlockSize>::is_empty() const noexcept
{
    return this->num_of_used_blocks == 0;
}


template <size_t BlockSize>
void PerPageSuballocatorTemplate<BlockSize>::initialize() noexcept
{
    p_arena->metadata(block_idx) = {0, 0};
    const auto head_addr = p_arena->block_idx2head_ptr(block_idx);
    std::construct_at(reinterpret_cast<FreeHeader*>(head_addr), sizeof(FreeHeader), 0);
    std::construct_at(reinterpret_cast<FreeHeader*>(head_addr + sizeof(FreeHeader)), 0, BlockSize - sizeof(FreeHeader));
}

template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void* PerPageSuballocatorTemplate<BlockSize>::allocate(const size_t n_elems)
{
    const auto raw_size = ElemSize * n_elems;
    const auto size = (raw_size + sizeof(FreeHeader) - 1) / sizeof(FreeHeader) * sizeof(FreeHeader);

    const auto head_addr = p_arena->block_idx2head_ptr(block_idx);
    const auto free_header = [&head_addr]<std::unsigned_integral T>(T idx) {
        return std::launder(reinterpret_cast<FreeHeader*>(head_addr + idx));
    };
    auto& metadata = p_arena->metadata(block_idx);

    const UInt freep = metadata.freep;
    size_t prev = freep;
    FreeHeader* prev_ptr = free_header(prev);
    size_t cursor = prev_ptr->next;
    FreeHeader* cursor_ptr = free_header(cursor);
    for (;; prev = cursor, prev_ptr = cursor_ptr, cursor = cursor_ptr->next, cursor_ptr = free_header(cursor)) {
        if (cursor_ptr->size >= size) {
            const auto pos_unaligned = cursor + cursor_ptr->size - size;
            const auto ptr_aligned = (alignof(FreeHeader) % Alignment == 0    ? head_addr + pos_unaligned
                                      : Arena::DataAlignment % Alignment == 0 ? head_addr + (pos_unaligned / Alignment * Alignment)
                                                                              : (head_addr + pos_unaligned) / Alignment * Alignment);
            const auto pos_aligned = ptr_aligned - head_addr;
            if (alignof(FreeHeader) % Alignment == 0 || pos_aligned >= cursor) {
                const auto padding_size = pos_unaligned - pos_aligned;
                if (alignof(FreeHeader) % Alignment == 0 || padding_size == 0) {
                    if (pos_aligned != cursor) {
                        cursor_ptr->size = static_cast<UInt>(cursor_ptr->size - size);
                    } else {
                        prev_ptr->next = cursor_ptr->next;
                    }
                } else {
                    const auto pad = pos_aligned + size;
                    std::construct_at(reinterpret_cast<FreeHeader*>(head_addr + pad), cursor_ptr->next, static_cast<UInt>(padding_size));
                    if (pos_aligned != cursor) {
                        cursor_ptr->next = static_cast<UInt>(pad);
                        cursor_ptr->size = static_cast<UInt>(pos_aligned - cursor);
                    } else {
                        prev_ptr->next = static_cast<UInt>(pad);
                    }
                }

                metadata.freep = static_cast<UInt>(prev);
                metadata.usage = static_cast<UInt>(metadata.usage + size);
                return reinterpret_cast<void*>(ptr_aligned);
            }
        }
        if (cursor == freep) {
            throw std::bad_alloc{};
        }
    }
}
template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void PerPageSuballocatorTemplate<BlockSize>::deallocate(void* const ptr, const size_t n_elems)
{
    const auto head_addr = p_arena->block_idx2head_ptr(block_idx);
    const auto free_header = [&head_addr]<std::unsigned_integral T>(T idx) {
        return std::launder(reinterpret_cast<FreeHeader*>(head_addr + idx));
    };
    auto& metadata = p_arena->metadata(block_idx);

    const auto raw_size = ElemSize * n_elems;
    const auto size = (raw_size + sizeof(FreeHeader) - 1) / sizeof(FreeHeader) * sizeof(FreeHeader);

    const auto cursor = reinterpret_cast<uintptr_t>(ptr) - head_addr;
    uintptr_t prev = metadata.freep;
    FreeHeader* prev_ptr = free_header(prev);
    for (; !(prev < cursor && cursor < prev_ptr->next); prev = prev_ptr->next, prev_ptr = free_header(prev)) {
        if (prev >= prev_ptr->next && (cursor > prev || cursor < prev_ptr->next)) {
            break;
        }
    }
    size_t next = prev_ptr->next;
    metadata.freep = static_cast<UInt>(prev);
    metadata.usage = static_cast<UInt>(metadata.usage - size);

    if (metadata.usage == 0) {
        return p_arena->block_alloc->deallocate_block(*p_arena, block_idx);
    }

    FreeHeader* cursor_ptr;
    size_t new_size;
    if (prev + prev_ptr->size == cursor) {
        cursor_ptr = prev_ptr;
        new_size = prev_ptr->size + size;
    } else {
        cursor_ptr = new (reinterpret_cast<void*>(head_addr + cursor)) FreeHeader;
        new_size = size;
        prev_ptr->next = static_cast<UInt>(cursor);
    }

    if (cursor + size == next) {
        const FreeHeader* const next_ptr = free_header(next);
        cursor_ptr->next = next_ptr->next;
        cursor_ptr->size = static_cast<UInt>(new_size + next_ptr->size);
    } else {
        cursor_ptr->next = static_cast<UInt>(next);
        cursor_ptr->size = static_cast<UInt>(new_size);
    }
}

template <size_t BlockSize>
constexpr bool PerPageSuballocatorTemplate<BlockSize>::is_occupancy_under(double threshold) noexcept
{
    auto& metadata = p_arena->metadata(block_idx);
    return metadata.usage < (BlockSize - sizeof(FreeHeader)) * threshold;
}


template <size_t BlockSize>
PerPageBlockAllocatorTemplate<BlockSize>::~PerPageBlockAllocatorTemplate()
{
    if (current_arena != nullptr) {
        auto& arena = *current_arena;
        arena.~Arena();
        MUnmap(&arena, ArenaSize);
    }
}

template <size_t BlockSize>
auto PerPageBlockAllocatorTemplate<BlockSize>::allocate_block() -> Suballocator
{
    auto res = [&]() -> Suballocator {
        if (current_arena != nullptr) {
            if (const auto block_idx = current_arena->find_free_and_allocate(); block_idx != -1) {
                return {*current_arena, static_cast<size_t>(block_idx)};
            }
            current_arena->link.next = nullptr;
        }
        if (const auto first = non_full_arenas.next; first != &non_full_arenas) {
            non_full_arenas.next = first->next;
            non_full_arenas.next->prev = &non_full_arenas;
            auto& arena = first->arena();
            current_arena = &arena;
            const auto block_idx = arena.find_free_and_allocate();
            return {arena, static_cast<size_t>(block_idx)};
        }
        auto& arena = Arena::create(*this);
        current_arena = &arena;
        return {arena, 0};
    }();

    res.initialize();
    return res;
}
template <size_t BlockSize>
void PerPageBlockAllocatorTemplate<BlockSize>::deallocate_block(Arena& arena, size_t block_idx)
{
    arena.free(block_idx);
    if (&arena != current_arena) {
        if (arena.is_empty()) {
            if (arena.link.next != nullptr) {
                arena.link.remove_from_list();
            }
            arena.~Arena();
            MUnmap(&arena, ArenaSize);
        } else if (arena.link.next == nullptr) {
            non_full_arenas.insert_prev(arena.link);
        }
    }
}

}  // namespace FarMalloc
