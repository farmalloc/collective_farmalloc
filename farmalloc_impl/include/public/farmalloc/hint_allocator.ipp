#pragma once

#include <farmalloc/hint_allocator.hpp>

#include <farmalloc/aligned_mmap.hpp>
#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/local_memory_store.hpp>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>


namespace FarMalloc
{

template <size_t BlockSize>
constexpr void HintAllocBlockLink<BlockSize>::insert_prev(HintAllocBlockLink& to_be_prev) noexcept
{
    to_be_prev.next = this;
    to_be_prev.prev = prev;
    prev->next = &to_be_prev;
    prev = &to_be_prev;
}
template <size_t BlockSize>
constexpr void HintAllocBlockLink<BlockSize>::remove_from_list() noexcept
{
    prev->next = next;
    next->prev = prev;
}

template <size_t BlockSize>
auto HintAllocBlockLink<BlockSize>::block() noexcept -> Block&
{
    static_assert(std::is_standard_layout_v<Block>);
    static_assert(offsetof(Block, link) == 0);
    return *reinterpret_cast<Block*>(this);
}


template <size_t BlockSize>
void HintAllocBlock<BlockSize>::initialize(const uintptr_t head_addr) noexcept
{
    freep = 0;
    usage = 0;
    std::construct_at(reinterpret_cast<FreeHeader*>(head_addr), sizeof(FreeHeader), 0);
    std::construct_at(reinterpret_cast<FreeHeader*>(head_addr + sizeof(FreeHeader)), 0, BlockSize - sizeof(FreeHeader));
}

template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
bool HintAllocBlock<BlockSize>::allocate(const size_t n_elems, const uintptr_t head_addr, void*& ret) noexcept
{
    const auto raw_size = ElemSize * n_elems;
    const auto size = (raw_size + sizeof(FreeHeader) - 1) / sizeof(FreeHeader) * sizeof(FreeHeader);

    const auto free_header = [&head_addr]<std::unsigned_integral T>(T idx) {
        return std::launder(reinterpret_cast<FreeHeader*>(head_addr + idx));
    };

    const UInt orig_freep = freep;
    size_t prev = orig_freep;
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

                freep = static_cast<UInt>(prev);
                usage = static_cast<UInt>(usage + size);
                ret = reinterpret_cast<void*>(ptr_aligned);
                return true;
            }
        }
        if (cursor == orig_freep) {
            ret = nullptr;
            return false;
        }
    }
}
template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void HintAllocBlock<BlockSize>::deallocate(void* const ptr, const size_t n_elems, const uintptr_t head_addr) noexcept
{
    const auto free_header = [&head_addr]<std::unsigned_integral T>(T idx) {
        return std::launder(reinterpret_cast<FreeHeader*>(head_addr + idx));
    };

    const auto raw_size = ElemSize * n_elems;
    const auto size = (raw_size + sizeof(FreeHeader) - 1) / sizeof(FreeHeader) * sizeof(FreeHeader);

    const auto cursor = reinterpret_cast<uintptr_t>(ptr) - head_addr;
    uintptr_t prev = freep;
    FreeHeader* prev_ptr = free_header(prev);
    for (; !(prev < cursor && cursor < prev_ptr->next); prev = prev_ptr->next, prev_ptr = free_header(prev)) {
        if (prev >= prev_ptr->next && (cursor > prev || cursor < prev_ptr->next)) {
            break;
        }
    }
    size_t next = prev_ptr->next;
    freep = static_cast<UInt>(prev);
    usage = static_cast<UInt>(usage - size);

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
constexpr void HintAllocArenaLink<BlockSize>::insert_prev(HintAllocArenaLink& to_be_prev) noexcept
{
    to_be_prev.next = this;
    to_be_prev.prev = prev;
    prev->next = &to_be_prev;
    prev = &to_be_prev;
}
template <size_t BlockSize>
constexpr void HintAllocArenaLink<BlockSize>::remove_from_list() noexcept
{
    prev->next = next;
    next->prev = prev;
}

template <size_t BlockSize>
auto HintAllocArenaLink<BlockSize>::arena() noexcept -> Arena&
{
    static_assert(std::is_standard_layout_v<Arena>);
    static_assert(offsetof(Arena, link) == 0);
    return *reinterpret_cast<Arena*>(this);
}


template <size_t BlockSize, size_t Max>
consteval size_t NBlocksForHintAllocArena()
{
    static_assert(Max >= 1);
    if constexpr (sizeof(HintAllocArenaMetadata<BlockSize, Max>) + HintAllocArenaMetadata<BlockSize, Max>::DataNPages * PageSize <= ArenaSize) {
        return Max;
    } else {
        return NBlocksForHintAllocArena<BlockSize, Max - 1>();
    }
}


template <size_t BlockSize>
HintAllocArena<BlockSize>::HintAllocArena()
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
auto HintAllocArena<BlockSize>::create() -> HintAllocArena&
{
    const auto arena_addr = AlignedMMap<ArenaSize, 0>(ArenaSize);
    return *new (arena_addr) HintAllocArena;
}

template <size_t BlockSize>
HintAllocArena<BlockSize>::~HintAllocArena()
{
    LocalMemoryStore::uunmap(reinterpret_cast<void*>(block_idx2head_ptr(0)), DataNPages * PageSize);
    this->store_buf.destroy(DataNPages * PageSize);
}

template <size_t BlockSize>
auto HintAllocArena<BlockSize>::from_inside_ptr(const void* ptr) noexcept -> HintAllocArena&
{
    return *reinterpret_cast<HintAllocArena*>(reinterpret_cast<uintptr_t>(ptr) / ArenaAlignment * ArenaAlignment);
}
template <size_t BlockSize>
size_t HintAllocArena<BlockSize>::data_ptr2idx(const void* ptr) noexcept
{
    return static_cast<size_t>((reinterpret_cast<uintptr_t>(ptr) % ArenaAlignment - MetadataNPages * PageSize) / BlockSize);
}
template <size_t BlockSize>
size_t HintAllocArena<BlockSize>::metadata_ptr2idx(const Block* ptr) noexcept
{
    return static_cast<size_t>(ptr - this->blocks_tab.data());
}
template <size_t BlockSize>
uintptr_t HintAllocArena<BlockSize>::block_idx2head_ptr(size_t idx) noexcept
{
    return reinterpret_cast<uintptr_t>(this) + MetadataNPages * PageSize + idx * BlockSize;
}

template <size_t BlockSize>
constexpr int HintAllocArena<BlockSize>::find_free_and_allocate() noexcept
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
constexpr void HintAllocArena<BlockSize>::free(const size_t block_idx) noexcept
{
    this->num_of_used_blocks--;
    this->is_block_used[block_idx / 64] ^= uint64_t{1} << (block_idx % 64);
}
template <size_t BlockSize>
constexpr bool HintAllocArena<BlockSize>::is_empty() const noexcept
{
    return this->num_of_used_blocks == 0;
}


template <size_t BlockSize>
void HintAllocatorImpl<BlockSize>::dec_ref(HintAllocatorImpl* ptr) noexcept
{
    ptr->ref_count--;
    if (ptr->ref_count == 0) {
        try {
            delete ptr;
        } catch (...) {  // deleter should not throw exception
        }
    }
}
template <size_t BlockSize>
auto HintAllocatorImpl<BlockSize>::shallow_copy() noexcept -> std::unique_ptr<HintAllocatorImpl, void (*)(HintAllocatorImpl*)>
{
    ref_count++;
    return {this, dec_ref};
}
template <size_t BlockSize>
HintAllocatorImpl<BlockSize>::~HintAllocatorImpl()
{
    if (current_arena != nullptr) {
        current_arena->~Arena();
        MUnmap(current_arena, ArenaSize);
    }
}

template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void* HintAllocatorImpl<BlockSize>::allocate(size_t n_elems)
{
    void* ret;
    if (current_arena != nullptr) {
        auto& block = current_arena->block(current_block_idx);
        if (block.template allocate<ElemSize, Alignment>(n_elems, current_arena->block_idx2head_ptr(current_block_idx), ret)) {
            return ret;
        }
        block.link.next = nullptr;
    }
    for (auto p_block_link = non_full_blocks.next; p_block_link != &non_full_blocks; p_block_link = p_block_link->next) {
        auto& block = p_block_link->block();
        auto& arena = Arena::from_inside_ptr(&block);
        const auto block_idx = arena.metadata_ptr2idx(&block);
        if (block.template allocate<ElemSize, Alignment>(n_elems, arena.block_idx2head_ptr(block_idx), ret)) {
            block.link.remove_from_list();
            current_arena = &arena;
            current_block_idx = block_idx;
            return ret;
        }
    }

    [this] {
        if (current_arena != nullptr) {
            if (const auto block_idx = current_arena->find_free_and_allocate(); block_idx != -1) {
                current_block_idx = block_idx;
                return;
            }
            current_arena->link.next = nullptr;
        }
        if (const auto first = non_full_arenas.next; first != &non_full_arenas) {
            non_full_arenas.next = first->next;
            non_full_arenas.next->prev = &non_full_arenas;
            current_arena = &first->arena();
            current_block_idx = static_cast<size_t>(current_arena->find_free_and_allocate());
        } else {
            current_arena = &Arena::create();
            current_block_idx = 0;
        }
    }();

    auto& block = current_arena->block(current_block_idx);
    const auto head_addr = current_arena->block_idx2head_ptr(current_block_idx);
    block.initialize(head_addr);
    block.template allocate<ElemSize, Alignment>(n_elems, head_addr, ret);
    return ret;
}
template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void* HintAllocatorImpl<BlockSize>::allocate(size_t n_elems, const void* hint)
{
    if (hint != nullptr) {
        auto& arena = Arena::from_inside_ptr(hint);
        const auto block_idx = Arena::data_ptr2idx(hint);

        void* ret;
        if (arena.block(block_idx).template allocate<ElemSize, Alignment>(n_elems, arena.block_idx2head_ptr(block_idx), ret)) {
            return ret;
        }
    }

    return allocate<ElemSize, Alignment>(n_elems);
}
template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void HintAllocatorImpl<BlockSize>::deallocate(void* ptr, size_t n_elems)
{
    auto& arena = Arena::from_inside_ptr(ptr);
    const auto block_idx = Arena::data_ptr2idx(ptr);
    auto& block = arena.block(block_idx);

    block.template deallocate<ElemSize, Alignment>(ptr, n_elems, arena.block_idx2head_ptr(block_idx));
    if (&arena != current_arena || block_idx != current_block_idx) {
        if (block.is_empty()) {
            if (block.link.next != nullptr) {
                block.link.remove_from_list();
            }
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

        } else if (block.link.next == nullptr) {
            non_full_blocks.insert_prev(block.link);
        }
    }
}


template <class T, size_t BlockSize>
[[nodiscard]] T* HintAllocator<T, BlockSize>::allocate(size_t n)
{
    if (alignof(T) > Impl::Arena::DataAlignment || sizeof(T) * n > Impl::Block::max_size()) {
        throw std::bad_alloc{};
    }

    void* result = pimpl->template allocate<sizeof(T), alignof(T)>(n);
    new (result) std::byte[sizeof(T) * n];
    return *std::launder(reinterpret_cast<T(*)[]>(result));
}
template <class T, size_t BlockSize>
[[nodiscard]] T* HintAllocator<T, BlockSize>::allocate(size_t n, const void* hint)
{
    if (alignof(T) > Impl::Arena::DataAlignment || sizeof(T) * n > Impl::Block::max_size()) {
        throw std::bad_alloc{};
    }

    void* result = pimpl->template allocate<sizeof(T), alignof(T)>(n, hint);
    new (result) std::byte[sizeof(T) * n];
    return *std::launder(reinterpret_cast<T(*)[]>(result));
}
template <class T, size_t BlockSize>
void HintAllocator<T, BlockSize>::deallocate(T* p, size_t n) noexcept
{
    try {
        pimpl->template deallocate<sizeof(T), alignof(T)>(p, n);
    } catch (...) {  // deallocation should not throw exception
    }
}

}  // namespace FarMalloc
