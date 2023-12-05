#pragma once

#include <cstdint>
#include <type_traits>


namespace FarMemory::Utility
{

template <uintmax_t BitWidth>
using EnoughUnsignedInteger = std::enable_if_t<(BitWidth <= 64),
    std::conditional_t<(BitWidth <= 16),
        std::conditional_t<(BitWidth <= 8),
            uint8_t,
            uint16_t>,
        std::conditional_t<(BitWidth <= 32),
            uint32_t,
            uint64_t>>>;

}  // namespace FarMemory::Utility
