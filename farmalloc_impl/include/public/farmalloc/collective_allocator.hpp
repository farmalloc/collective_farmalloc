#pragma once

#include <farmalloc/collective_allocator_traits.hpp>
#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/per-page_suballocator.hpp>
#include <farmalloc/purely-local_suballocator.hpp>
#include <farmalloc/swappable_plain_suballocator.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>


namespace FarMalloc
{

template <size_t BlockSize>
struct CollectiveAllocatorImpl {
    using PerPageBlockAllocator = PerPageBlockAllocatorTemplate<BlockSize>;

    PurelyLocalSuballocatorImpl purely_local;
    SwappablePlainSuballocatorImpl swappable_plain;
    PerPageBlockAllocator block_allocator;

    size_t ref_count{0};

    inline CollectiveAllocatorImpl(size_t purely_local_capacity) : purely_local{purely_local_capacity} {}
    inline ~CollectiveAllocatorImpl() = default;
    CollectiveAllocatorImpl(const CollectiveAllocatorImpl&) = delete;
    CollectiveAllocatorImpl& operator=(const CollectiveAllocatorImpl&) = delete;
    CollectiveAllocatorImpl(CollectiveAllocatorImpl&&) = delete;
    CollectiveAllocatorImpl& operator=(CollectiveAllocatorImpl&&) = delete;

    inline static void dec_ref(CollectiveAllocatorImpl* ptr) noexcept;
    inline std::unique_ptr<CollectiveAllocatorImpl, void (*)(CollectiveAllocatorImpl*)> shallow_copy() noexcept;

    using PurelyLocalSuballocator = PlainSuballocator<PurelyLocalSuballocatorImpl>;
    using SwappablePlainSuballocator = PlainSuballocator<SwappablePlainSuballocatorImpl>;
    using PerPageSuballocator = PerPageSuballocatorTemplate<BlockSize>;
    struct SuballocatorImpl {
        uintptr_t contains_mask, contains_cmp;
        std::variant<PurelyLocalSuballocator, SwappablePlainSuballocator, PerPageSuballocator> impl;

        template <class Impl>
        SuballocatorImpl(uintptr_t contains_mask, uintptr_t contains_cmp, Impl&& impl)
            : contains_mask{contains_mask}, contains_cmp{contains_cmp}, impl{std::forward<Impl>(impl)}
        {
        }

        inline constexpr bool contains(const void* ptr) noexcept;

        template <size_t ElemSize, size_t Alignment>
        inline void* allocate(const size_t n_elems);
        template <size_t ElemSize, size_t Alignment>
        inline void deallocate(void* const ptr, const size_t n_elems);

        inline constexpr bool is_occupancy_under(double threshold) noexcept;
    };

    inline static constexpr size_t AddrMaskArenaKind = (SubspaceInterval - 1u) & ~(ArenaSize - 1u);

    template <size_t ElemSize, size_t Alignment>
    inline void* allocate(const size_t n_elems);
    template <size_t ElemSize, size_t Alignment>
    inline void deallocate(void* const ptr, const size_t n_elems);

    inline SuballocatorImpl get_suballocator(FarMalloc::suballocator_kind kind);
    inline constexpr SuballocatorImpl get_suballocator(const void* const ptr) noexcept;
};

template <class T, size_t BlockSize>
struct Suballocator {
    using Impl = CollectiveAllocatorImpl<BlockSize>::SuballocatorImpl;
    Impl impl;

    inline constexpr Suballocator(Impl&& impl) noexcept : impl{std::move(impl)} {}

    using value_type = T;

    template <class U>
    struct rebind {
        using other = Suballocator<U, BlockSize>;
    };

    template <class U>
    inline constexpr Suballocator(const Suballocator<U, BlockSize>& other) noexcept : impl{other.impl}
    {
    }
    template <class U>
    inline constexpr Suballocator(Suballocator<U, BlockSize>&& other) noexcept : impl{std::move(other.impl)}
    {
    }

    [[nodiscard]] inline T* allocate(size_t n);
    inline void deallocate(T* p, size_t n) noexcept;

    inline constexpr bool contains(const void* ptr) noexcept;
    inline constexpr bool is_occupancy_under(double threshold) noexcept;
};

template <class T, size_t BlockSize>
struct CollectiveAllocator {
    using Impl = CollectiveAllocatorImpl<BlockSize>;

    std::invoke_result_t<decltype(&Impl::shallow_copy), Impl*> pimpl;

    inline constexpr CollectiveAllocator(size_t purely_local_capacity)
        : pimpl{(new Impl{purely_local_capacity})->shallow_copy()} {}
    inline constexpr CollectiveAllocator(const CollectiveAllocator& other) noexcept : pimpl{other.pimpl->shallow_copy()} {}
    inline constexpr CollectiveAllocator& operator=(const CollectiveAllocator& other) noexcept { pimpl = other.pimpl->shallow_copy(); }
    inline constexpr CollectiveAllocator(CollectiveAllocator&&) noexcept = default;
    inline constexpr CollectiveAllocator& operator=(CollectiveAllocator&&) noexcept = default;

    using value_type = T;
    using suballocator = Suballocator<T, BlockSize>;

    template <class U>
    struct rebind {
        using other = CollectiveAllocator<U, BlockSize>;
    };

    template <class U>
    inline constexpr CollectiveAllocator(const CollectiveAllocator<U, BlockSize>& other) noexcept : pimpl{other.pimpl->shallow_copy()}
    {
    }
    template <class U>
    inline constexpr CollectiveAllocator(CollectiveAllocator<U, BlockSize>&& other) noexcept : pimpl{std::move(other.pimpl)}
    {
    }

    [[nodiscard]] inline T* allocate(size_t n);
    inline void deallocate(T* p, size_t n) noexcept;

    inline suballocator get_suballocator(FarMalloc::suballocator_kind kind) { return suballocator{pimpl->get_suballocator(kind)}; }
    inline suballocator get_suballocator(const void* ptr) const noexcept { return suballocator{pimpl->get_suballocator(ptr)}; }
};

}  // namespace FarMalloc

#include <farmalloc/collective_allocator.ipp>
