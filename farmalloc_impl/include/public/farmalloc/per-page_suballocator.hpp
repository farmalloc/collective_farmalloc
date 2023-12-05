#pragma once

#include <farmalloc/local_memory_store.hpp>
#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/size_class.hpp>
#include <util/enough_unsigned_integer.hpp>

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numeric>


namespace FarMalloc
{

template <size_t BlockSize>
struct PerPageSuballocatorArena;
template <size_t BlockSize>
struct PerPageArenaLink {
    PerPageArenaLink* next;
    PerPageArenaLink* prev;

    inline constexpr void insert_prev(PerPageArenaLink& to_be_prev) noexcept;
    inline constexpr void remove_from_list() noexcept;

    using Arena = PerPageSuballocatorArena<BlockSize>;
    inline Arena& arena() noexcept;
};


template <std::unsigned_integral UInt>
struct alignas(sizeof(UInt) * 2) KRFreeHeader {
    UInt next;
    UInt size;
};
template <std::unsigned_integral UInt>
struct PerPageMetadata {
    UInt freep;
    UInt usage;
};


template <size_t BlockSize>
struct PerPageBlockAllocatorTemplate;
template <size_t BlockSize, size_t NBlocks_>
struct PerPageArenaMetadata {
    inline static constexpr size_t NBlocks = NBlocks_;
    using Link = PerPageArenaLink<BlockSize>;
    using BlockAllocator = PerPageBlockAllocatorTemplate<BlockSize>;
    using UInt = FarMemory::Utility::EnoughUnsignedInteger<std::bit_width(BlockSize - 1)>;
    using BlockMetadata = PerPageMetadata<UInt>;
    inline static constexpr int BitmapWidth = (NBlocks + 63) / 64;

    Link link;
    BlockAllocator* const block_alloc;
    size_t num_of_used_blocks;
    std::array<uint64_t, (NBlocks + 63) / 64> is_block_used;
    LocalMemoryStoreBuffer store_buf;
    std::array<BlockMetadata, NBlocks> block_metadata_tab;

    PerPageArenaMetadata(BlockAllocator& block_alloc) : block_alloc{&block_alloc} {}
};


template <size_t BlockSize, size_t Max = SizeClass::MaxNPages>
inline consteval size_t NBlocksForPerPageSuballocatorArena();


template <size_t BlockSize>
struct PerPageSuballocatorArena : PerPageArenaMetadata<BlockSize, NBlocksForPerPageSuballocatorArena<BlockSize>()> {
    using Base = PerPageArenaMetadata<BlockSize, NBlocksForPerPageSuballocatorArena<BlockSize>()>;

    inline static constexpr size_t ArenaAlignment = std::gcd(SubspaceInterval - PerPageOffset, SubspaceInterval),
                                   DataAlignment = size_t{1} << std::countr_zero(BlockSize);
    inline static constexpr size_t NBlocks = Base::NBlocks,
                                   DataNPages = (BlockSize * NBlocks + PageSize - 1) / PageSize,
                                   MetadataNPages = ArenaSize / PageSize - DataNPages;

    constexpr Base::BlockMetadata& metadata(size_t idx) noexcept { return this->block_metadata_tab[idx]; }

protected:
    inline PerPageSuballocatorArena(Base::BlockAllocator& block_alloc);

public:
    inline static PerPageSuballocatorArena& create(Base::BlockAllocator& block_alloc);
    PerPageSuballocatorArena(const PerPageSuballocatorArena&) = delete;

    ~PerPageSuballocatorArena();

    inline static PerPageSuballocatorArena& from_inside_ptr(const void* ptr) noexcept;
    inline static size_t data_ptr2idx(const void* ptr) noexcept;
    inline uintptr_t block_idx2head_ptr(size_t idx) noexcept;

    inline constexpr int find_free_and_allocate() noexcept;
    inline constexpr void free(const size_t block_idx) noexcept;
    inline constexpr bool is_empty() const noexcept;
};


template <size_t BlockSize>
struct PerPageSuballocatorTemplate {
    using Arena = PerPageSuballocatorArena<BlockSize>;
    using BlockMetadata = Arena::BlockMetadata;
    using UInt = Arena::UInt;
    using FreeHeader = KRFreeHeader<UInt>;
    static_assert(BlockSize >= sizeof(FreeHeader) * 2);
    static_assert(BlockSize % sizeof(FreeHeader) == 0);

    Arena* p_arena;
    size_t block_idx;

    inline constexpr PerPageSuballocatorTemplate(Arena& arena, size_t block_idx) noexcept : p_arena{&arena}, block_idx{block_idx} {}
    inline constexpr PerPageSuballocatorTemplate(const PerPageSuballocatorTemplate&) noexcept = default;
    inline constexpr PerPageSuballocatorTemplate& operator=(const PerPageSuballocatorTemplate&) noexcept = default;
    inline constexpr PerPageSuballocatorTemplate(PerPageSuballocatorTemplate&&) noexcept = default;
    inline constexpr PerPageSuballocatorTemplate& operator=(PerPageSuballocatorTemplate&&) noexcept = default;

    void initialize() noexcept;

    template <size_t ElemSize, size_t Alignment>
    inline void* allocate(const size_t n_elems);
    template <size_t ElemSize, size_t Alignment>
    inline void deallocate(void* const ptr, const size_t n_elems);

    inline constexpr bool is_occupancy_under(double threshold) noexcept;
};


template <size_t BlockSize>
struct PerPageBlockAllocatorTemplate {
    using Arena = PerPageSuballocatorArena<BlockSize>;
    using Link = PerPageArenaLink<BlockSize>;
    using Suballocator = PerPageSuballocatorTemplate<BlockSize>;

    Arena* current_arena{};
    Link non_full_arenas{&non_full_arenas, &non_full_arenas};

    inline constexpr PerPageBlockAllocatorTemplate() {}
    inline ~PerPageBlockAllocatorTemplate();

    inline Suballocator allocate_block();
    inline void deallocate_block(Arena& arena, size_t block_idx);
};

}  // namespace FarMalloc

#include <farmalloc/per-page_suballocator.ipp>
