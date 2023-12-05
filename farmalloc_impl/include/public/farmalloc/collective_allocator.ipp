#pragma once

#include <farmalloc/collective_allocator.hpp>

#include <farmalloc/collective_allocator_traits.hpp>
#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/per-page_suballocator.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <variant>


namespace FarMalloc
{

template <size_t BlockSize>
void CollectiveAllocatorImpl<BlockSize>::dec_ref(CollectiveAllocatorImpl* ptr) noexcept
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
auto CollectiveAllocatorImpl<BlockSize>::shallow_copy() noexcept -> std::unique_ptr<CollectiveAllocatorImpl, void (*)(CollectiveAllocatorImpl*)>
{
    ref_count++;
    return {this, dec_ref};
}

template <size_t BlockSize>
constexpr bool CollectiveAllocatorImpl<BlockSize>::SuballocatorImpl::contains(const void* ptr) noexcept
{
    return (reinterpret_cast<uintptr_t>(ptr) & contains_mask) == contains_cmp;
}

template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void* CollectiveAllocatorImpl<BlockSize>::SuballocatorImpl::allocate(const size_t n_elems)
{
    return std::visit([n_elems](auto& suballoc) { return suballoc.template allocate<ElemSize, Alignment>(n_elems); }, impl);
}
template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void CollectiveAllocatorImpl<BlockSize>::SuballocatorImpl::deallocate(void* const ptr, const size_t n_elems)
{
    return std::visit([ptr, n_elems](auto& suballoc) { return suballoc.template deallocate<ElemSize, Alignment>(ptr, n_elems); }, impl);
}

template <size_t BlockSize>
constexpr bool CollectiveAllocatorImpl<BlockSize>::SuballocatorImpl::is_occupancy_under(double threshold) noexcept
{
    return std::visit([threshold](auto& suballoc) { return suballoc.is_occupancy_under(threshold); }, impl);
}

template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void* CollectiveAllocatorImpl<BlockSize>::allocate(const size_t n_elems)
{
    return swappable_plain.allocate<ElemSize, Alignment>(n_elems);
}
template <size_t BlockSize>
template <size_t ElemSize, size_t Alignment>
void CollectiveAllocatorImpl<BlockSize>::deallocate(void* const ptr, const size_t n_elems)
{
    switch (reinterpret_cast<uintptr_t>(ptr) & AddrMaskArenaKind) {
    case PurelyLocalOffset:
        return purely_local.deallocate<ElemSize, Alignment>(ptr, n_elems);
    case SwappablePlainOffset:
        return swappable_plain.deallocate<ElemSize, Alignment>(ptr, n_elems);
    default:
    case PerPageOffset: {
        using Arena = PerPageSuballocatorArena<BlockSize>;
        return PerPageSuballocator{Arena::from_inside_ptr(ptr), Arena::data_ptr2idx(ptr)}.template deallocate<ElemSize, Alignment>(ptr, n_elems);
    }
    }
}

template <size_t BlockSize>
auto CollectiveAllocatorImpl<BlockSize>::get_suballocator(FarMalloc::suballocator_kind kind) -> SuballocatorImpl
{
    switch (kind) {
    case FarMalloc::purely_local:
        return {AddrMaskArenaKind, PurelyLocalOffset, PurelyLocalSuballocator{&purely_local}};
    case FarMalloc::swappable_plain:
        return {AddrMaskArenaKind, SwappablePlainOffset, SwappablePlainSuballocator{&swappable_plain}};
    default:
    case FarMalloc::new_per_page: {
        static_assert(std::has_single_bit(BlockSize));
        auto suballoc = block_allocator.allocate_block();
        const auto cmp = suballoc.p_arena->block_idx2head_ptr(suballoc.block_idx);
        return {~(BlockSize - 1u), cmp, std::move(suballoc)};
    }
    }
}
template <size_t BlockSize>
constexpr auto CollectiveAllocatorImpl<BlockSize>::get_suballocator(const void* const ptr) noexcept -> SuballocatorImpl
{
    switch (reinterpret_cast<uintptr_t>(ptr) & AddrMaskArenaKind) {
    case PurelyLocalOffset:
        return {AddrMaskArenaKind, PurelyLocalOffset, PurelyLocalSuballocator{&purely_local}};
    case SwappablePlainOffset:
        return {AddrMaskArenaKind, SwappablePlainOffset, SwappablePlainSuballocator{&swappable_plain}};
    default:
    case PerPageOffset: {
        using Arena = PerPageSuballocatorArena<BlockSize>;
        auto suballoc = PerPageSuballocator{Arena::from_inside_ptr(ptr), Arena::data_ptr2idx(ptr)};
        const auto cmp = suballoc.p_arena->block_idx2head_ptr(suballoc.block_idx);
        return {~(BlockSize - 1u), cmp, std::move(suballoc)};
    }
    }
}

template <class T, size_t BlockSize>
[[nodiscard]] T* Suballocator<T, BlockSize>::allocate(size_t n)
{
    void* result = impl.template allocate<sizeof(T), alignof(T)>(n);
    new (result) std::byte[sizeof(T) * n];
    return *std::launder(reinterpret_cast<T(*)[]>(result));
}
template <class T, size_t BlockSize>
void Suballocator<T, BlockSize>::deallocate(T* p, size_t n) noexcept
{
    try {
        impl.template deallocate<sizeof(T), alignof(T)>(p, n);
    } catch (...) {  // deallocation should not throw exception
    }
}

template <class T, size_t BlockSize>
constexpr bool Suballocator<T, BlockSize>::contains(const void* ptr) noexcept
{
    return impl.contains(ptr);
}
template <class T, size_t BlockSize>
constexpr bool Suballocator<T, BlockSize>::is_occupancy_under(double threshold) noexcept
{
    return impl.is_occupancy_under(threshold);
}

template <class T, size_t BlockSize>
[[nodiscard]] T* CollectiveAllocator<T, BlockSize>::allocate(size_t n)
{
    void* result = pimpl->template allocate<sizeof(T), alignof(T)>(n);
    new (result) std::byte[sizeof(T) * n];
    return *std::launder(reinterpret_cast<T(*)[]>(result));
}
template <class T, size_t BlockSize>
void CollectiveAllocator<T, BlockSize>::deallocate(T* p, size_t n) noexcept
{
    try {
        pimpl->template deallocate<sizeof(T), alignof(T)>(p, n);
    } catch (...) {  // deallocation should not throw exception
    }
}

}  // namespace FarMalloc
