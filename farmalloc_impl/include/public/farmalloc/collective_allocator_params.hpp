#pragma once

#include <farmalloc/page_size.hpp>

#include <memory>


namespace FarMalloc
{

inline constexpr size_t ArenaSize = PageSize * (size_t{1} << 8);

inline constexpr size_t PurelyLocalOffset = 0;
inline constexpr size_t SwappablePlainOffset = PurelyLocalOffset + ArenaSize;
inline constexpr size_t PerPageOffset = SwappablePlainOffset + ArenaSize;

inline constexpr size_t SubspaceInterval = ArenaSize * 4;
static_assert(PerPageOffset + ArenaSize <= SubspaceInterval);

}  // namespace FarMalloc
