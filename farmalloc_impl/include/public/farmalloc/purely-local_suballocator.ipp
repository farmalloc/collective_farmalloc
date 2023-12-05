#pragma once

#include <farmalloc/purely-local_suballocator.hpp>

#include <cstddef>
#include <new>


namespace FarMalloc
{

void PurelyLocalCustom::check_capacity(size_t size)
{
    if (capacity < size) {
        throw std::bad_alloc{};
    }
}
constexpr void PurelyLocalCustom::consume_capacity(size_t size) noexcept
{
    capacity -= size;
}
constexpr void PurelyLocalCustom::reclaim_capacity(size_t size) noexcept
{
    capacity += size;
}
constexpr void PurelyLocalCustom::occupy_space(size_t size) noexcept
{
    occupied += size;
}
constexpr void PurelyLocalCustom::reclaim_space(size_t size) noexcept
{
    occupied -= size;
}
constexpr bool PurelyLocalCustom::is_occupancy_under(double threshold) noexcept
{
    return static_cast<double>(occupied) < static_cast<double>(orig_capacity) * threshold;
}

}  // namespace FarMalloc
