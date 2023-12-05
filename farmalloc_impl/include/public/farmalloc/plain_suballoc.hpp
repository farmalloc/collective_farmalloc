#pragma once

#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/plain_suballoc_page_metadata.hpp>
#include <farmalloc/size_class.hpp>
#include <util/ssize_t.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <utility>


namespace FarMalloc
{

template <size_t DataNPages, class Appendix>
struct PlainSuballocatorArenaMetadata {
protected:
    std::array<PlainSuballocatorPageMetadata, DataNPages + 2> metadata_tab;
    [[no_unique_address]] Appendix appendix;
};

template <class Appendix, size_t Max = SizeClass::MaxNPages>
inline consteval size_t DataNPagesForEachPlainSuballocatorArena();

using SSizeT = FarMemory::Utility::SSizeT;
template <class Appendix, size_t AlignOffset>
struct PlainSuballocatorArena : PlainSuballocatorArenaMetadata<DataNPagesForEachPlainSuballocatorArena<Appendix>(), Appendix> {
    inline static constexpr size_t ArenaAlignment = std::gcd(SubspaceInterval - AlignOffset, SubspaceInterval);
    inline static constexpr size_t DataNPages = DataNPagesForEachPlainSuballocatorArena<Appendix>(),
                                   MetadataNPages = ArenaSize / PageSize - DataNPages;
    inline static constexpr size_t NPageClasses = SizeClass::page_free_size2class_idx(DataNPages * PageSize) + 1;
    inline static constexpr size_t MaxMediumAllocSize = (std::bit_floor(SizeClass::page_class_idx2size(NPageClasses - 1) / PageSize) - 1) * PageSize;

    constexpr PlainSuballocatorPageMetadata& metadata(SSizeT idx) noexcept { return this->metadata_tab[idx + 1]; }

protected:
    inline constexpr PlainSuballocatorArena(FreePageLink& link) noexcept;

public:
    inline static void* allocate_memory(size_t size = ArenaSize);
    inline static PlainSuballocatorArena& create(FreePageLink& link);
    PlainSuballocatorArena(const PlainSuballocatorArena&) = delete;

    inline static PlainSuballocatorArena& from_inside_ptr(const void* ptr) noexcept;
    inline static SSizeT metadata_ptr2idx(const void* ptr) noexcept;
    inline static SSizeT data_ptr2idx(const void* ptr) noexcept;
    inline uintptr_t page_idx2head_ptr(SSizeT idx) noexcept;
};


template <class Arena, class Custom>
struct PlainSuballocatorImplBase {
    std::array<SlabMetadata*, SizeClass::NAllocClasses> current_slabs;
    std::array<SlabLink, SizeClass::NAllocClasses> non_full_slabs;
    std::array<FreePageLink, Arena::NPageClasses> free_pages;
    [[no_unique_address]] Custom custom;

    template <class... Args>
    inline constexpr PlainSuballocatorImplBase(Args&&... args);
    inline ~PlainSuballocatorImplBase();

    template <size_t Alignment>
    inline std::pair<Arena*, SSizeT> allocate_page(size_t size);
    inline void deallocate_page(Arena& arena, SSizeT idx, size_t n_pages) noexcept;

    template <size_t ElemSize, size_t Alignment>
    inline void* allocate(size_t n_elems);
    template <size_t ElemSize, size_t Alignment>
    inline void deallocate(void* const ptr, const size_t n_elems);

    inline constexpr bool is_occupancy_under(double threshold) noexcept;
};


template <class Impl>
struct PlainSuballocator {
    Impl* pimpl;

    inline constexpr PlainSuballocator(Impl* pimpl) noexcept : pimpl{pimpl} {}
    inline constexpr PlainSuballocator(const PlainSuballocator&) noexcept = default;
    inline constexpr PlainSuballocator& operator=(const PlainSuballocator&) noexcept = default;
    inline constexpr PlainSuballocator(PlainSuballocator&&) noexcept = default;
    inline constexpr PlainSuballocator& operator=(PlainSuballocator&&) noexcept = default;

    template <size_t ElemSize, size_t Alignment>
    inline void* allocate(size_t n_elems);
    template <size_t ElemSize, size_t Alignment>
    inline void deallocate(void* const ptr, size_t n_elems);

    inline constexpr bool is_occupancy_under(double threshold) noexcept;
};

}  // namespace FarMalloc

#include <farmalloc/plain_suballoc.ipp>
