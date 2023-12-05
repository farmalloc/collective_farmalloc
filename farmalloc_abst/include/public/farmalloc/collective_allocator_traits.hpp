#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>


namespace FarMalloc
{

namespace request
{

template <typename T, size_t N>
struct constant {
    using type = T;
    static constexpr size_t size = N;
};

template <typename T>
using single = constant<T, 1>;

template <typename T>
struct dynamic {
    using type = T;
    size_t size;
};

template <typename T>
struct null {
    using type = T;
};

}  // namespace request


template <class T>
struct default_relocate {
    inline constexpr default_relocate() noexcept = default;
    inline constexpr ~default_relocate() noexcept = default;
    inline constexpr void operator()(T* from, size_t n, T* to) const
    {
        std::uninitialized_move_n(from, n, to);
        std::destroy_n(from, n);
    }
};


enum class suballocator_kind {
    purely_local,
    swappable_plain,
    new_per_page
};
using enum suballocator_kind;

template <class Alloc>
struct collective_allocator_traits : std::allocator_traits<Alloc> {
private:
    using Base = std::allocator_traits<Alloc>;

public:
    using typename Base::pointer, typename Base::const_void_pointer, typename Base::size_type;

    template <class U>
    using rebind_traits = collective_allocator_traits<typename Base::template rebind_alloc<U>>;

private:
    template <class = void>
    struct suballocator_impl {
        using type = Alloc;
    };
    template <class X>
        requires(requires {
                     typename Alloc::suballocator;
                 })
    struct suballocator_impl<X> {
        using type = Alloc::suballocator;
    };

public:
    using suballocator = typename suballocator_impl<>::type;
    using suballocator_traits = collective_allocator_traits<suballocator>;

    static inline constexpr suballocator get_suballocator(Alloc& alloc, suballocator_kind kind)
    {
        if constexpr (requires { alloc.get_suballocator(kind); }) {
            return alloc.get_suballocator(kind);
        } else {
            return alloc;
        }
    }

    static inline constexpr suballocator get_suballocator(Alloc& alloc, const_void_pointer p)
    {
        if constexpr (requires { alloc.get_suballocator(p); }) {
            return alloc.get_suballocator(p);
        } else {
            return alloc;
        }
    }
    static inline constexpr bool if_suballocator_contains(Alloc& alloc, suballocator& suballoc, const_void_pointer p)
    {
        if constexpr (requires { alloc.contains(suballoc, p); }) {
            return alloc.contains(suballoc, p);
        } else if constexpr (requires { suballoc.contains(p); }) {
            return suballoc.contains(p);
        } else {
            static_assert(std::same_as<suballocator, Alloc>);
            return true;
        }
    }

    static inline constexpr void delete_suballocator_if_empty(Alloc& alloc, suballocator&& suballoc)
    {
        if constexpr (requires { alloc.delete_suballocator_if_empty(std::move(suballoc)); }) {
            return alloc.delete_suballocator_if_empty(std::move(suballoc));
        } else {
            return;
        }
    }

    static inline constexpr bool is_occupancy_under(Alloc& alloc, double threshold)
    {
        if constexpr (requires { alloc.is_occupancy_under(threshold); }) {
            return alloc.is_occupancy_under(threshold);
        } else {
            return true;
        }
    }

private:
    template <size_t, class Ptrs>
    static inline constexpr void batch_allocate_helper(Alloc&, Ptrs&)
    {
    }

    template <size_t I, class Ptrs, class HeadReq, class... TailReq>
    static inline constexpr void batch_allocate_helper(Alloc& alloc, Ptrs& ptrs, HeadReq&& req, TailReq&&... tail)
    {
        using std::get;
        using RawReqType = std::remove_reference_t<HeadReq>;
        using AllocatedType = typename RawReqType::type;
        if constexpr (std::same_as<request::null<AllocatedType>, RawReqType>) {
            batch_allocate_helper<I + 1>(alloc, ptrs, std::forward<TailReq>(tail)...);
        } else {
            using ReboundTraits = rebind_traits<AllocatedType>;
            using ReboundType = typename ReboundTraits::allocator_type;
            ReboundType rebound(alloc);
            get<I>(ptrs) = ReboundTraits::allocate(rebound, req.size);
            try {
                return batch_allocate_helper<I + 1>(alloc, ptrs, std::forward<TailReq>(tail)...);
            } catch (...) {
                ReboundTraits::deallocate(rebound, get<I>(ptrs), req.size);
                throw;
            }
        }
    }

public:
    template <class... Requests>
    static inline constexpr std::optional<std::tuple<typename std::pointer_traits<pointer>::template rebind<typename std::remove_reference_t<Requests>::type>...>> batch_allocate(Alloc& alloc, Requests&&... req)
    {
        if constexpr (requires { alloc.batch_allocate(std::forward<Requests>(req)...); }) {
            return alloc.batch_allocate(std::forward<Requests>(req)...);
        } else {
            std::optional<std::tuple<typename std::pointer_traits<pointer>::template rebind<typename std::remove_reference_t<Requests>::type>...>> result(std::in_place);
            try {
                batch_allocate_helper<0>(alloc, *result, std::forward<Requests>(req)...);
            } catch (...) {
                result.reset();
            }
            return result;
        }
    }

    static inline constexpr void deallocate(Alloc& alloc, pointer p, size_type n)
    {
        if constexpr (requires { alloc.deallocate(p, n); }) {
            alloc.deallocate(p, n);
        } else {
            suballocator suballoc = get_suballocator(alloc, p);
            suballocator_traits::dellocate(suballoc, p, n);
        }
    }

private:
    template <size_t, class PtrRefs, class Ptrs, class Func>
    static inline constexpr void relocate_helper(Alloc&, PtrRefs&, Ptrs&&, Func&&)
    {
    }

    template <size_t I, class PtrRefs, class Ptrs, class Func, class HeadReq, class... TailReq>
    static inline constexpr void relocate_helper(Alloc& alloc, PtrRefs& from_ptrs, Ptrs&& to_ptrs, Func&& func, HeadReq&& req, TailReq&&... tail)
    {
        using std::get;
        using RawReqType = std::remove_reference_t<HeadReq>;
        using AllocatedType = typename RawReqType::type;
        if constexpr (std::same_as<request::null<AllocatedType>, RawReqType>) {
            relocate_helper<I + 1>(from_ptrs, to_ptrs, std::forward<TailReq>(tail)...);
        } else {
            auto &from = get<I>(from_ptrs), &to = get<I>(to_ptrs);

            func(std::addressof(*from), req.size, std::addressof(*to));
            auto orig_from = std::move(from);
            from = std::move(to);

            using ReboundTraits = rebind_traits<AllocatedType>;
            using ReboundType = typename ReboundTraits::allocator_type;
            ReboundType rebound(alloc);
            try {
                relocate_helper<I + 1>(alloc, from_ptrs, std::move(to_ptrs), std::forward<Func>(func), std::forward<TailReq>(tail)...);
                ReboundTraits::deallocate(rebound, std::move(orig_from), req.size);
            } catch (...) {
                func(std::addressof(*from), req.size, std::addressof(*orig_from));
                ReboundTraits::deallocate(rebound, std::move(from), req.size);
                from = std::move(orig_from);
                throw;
            }
        }
    }

public:
    template <class... Requests>
    static inline constexpr bool relocate(Alloc& alloc, suballocator& suballoc, std::tuple<typename std::pointer_traits<pointer>::template rebind<typename std::remove_reference_t<Requests>::type>&...> p, Requests&&... req)
    {
        if constexpr (requires { alloc.relocate(p, std::forward<Requests>(req)..., suballoc, default_relocate<typename Base::value_type>()); }) {
            return alloc.relocate(p, std::forward<Requests>(req)..., suballoc, default_relocate<typename Base::value_type>());
        } else {
            auto allocated = suballocator_traits::batch_allocate(suballoc, req...);
            if (allocated) {
                relocate_helper<0>(alloc, p, std::move(*allocated), default_relocate<typename Base::value_type>(), std::forward<Requests>(req)...);
                return true;
            } else {
                throw std::bad_alloc{};
            }
        }
    }

    template <class Func, class... Requests>
    static inline constexpr bool relocate(Alloc& alloc, suballocator& suballoc, Func&& func, std::tuple<typename std::pointer_traits<pointer>::template rebind<typename std::remove_reference_t<Requests>::type>&...> p, Requests&&... req)
    {
        if constexpr (requires { alloc.relocate(p, suballoc, std::forward<Func>(func), std::forward<Requests>(req)...); }) {
            return alloc.relocate(p, suballoc, std::forward<Func>(func), std::forward<Requests>(req)...);
        } else {
            auto allocated = suballocator_traits::batch_allocate(suballoc, req...);
            if (allocated) {
                relocate_helper<0>(alloc, p, std::move(*allocated), std::forward<Func>(func), std::forward<Requests>(req)...);
                return true;
            } else {
                throw std::bad_alloc{};
            }
        }
    }
};

}  // namespace FarMalloc
