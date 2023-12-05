#pragma once

#include <farmalloc/collective_allocator_traits.hpp>

#include <cstddef>
#include <new>
#include <utility>


namespace FarMemoryContainer
{
using namespace FarMalloc;

template <class T>
struct AlignedBuffer {
    alignas(T) std::byte buf[sizeof(T)];
    constexpr AlignedBuffer() {}  // default initialization

    template <class Alloc, class... Args>
    constexpr void construct(Alloc& allocator, Args&&... args)
    {
        collective_allocator_traits<Alloc>::construct(allocator, reinterpret_cast<T*>(buf), std::forward<Args>(args)...);
    }
    template <class Alloc>
    constexpr void destroy(Alloc& allocator)
    {
        collective_allocator_traits<Alloc>::destroy(allocator, get());
    }
    template <class Alloc>
    constexpr void move_from(Alloc& allocator, AlignedBuffer& other)
    {
        construct(allocator, std::move(*other.get()));
        other.destroy(allocator);
    }

    T* get() noexcept { return std::launder(reinterpret_cast<T*>(buf)); }
    const T* get() const noexcept { return std::launder(reinterpret_cast<const T*>(buf)); }
};

}  // namespace FarMemoryContainer
