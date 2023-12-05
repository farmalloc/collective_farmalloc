#pragma once

#include <farmalloc/collective_allocator_params.hpp>
#include <farmalloc/local_memory_store.hpp>
#include <farmalloc/per-page_suballocator.hpp>  // KRFreeHeader
#include <farmalloc/size_class.hpp>
#include <util/enough_unsigned_integer.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>


namespace FarMalloc
{

template <size_t BlockSize>
struct HintAllocArena;


template <size_t BlockSize>
struct HintAllocBlock;
template <size_t BlockSize>
struct HintAllocBlockLink {
    HintAllocBlockLink* next;
    HintAllocBlockLink* prev;

    inline constexpr void insert_prev(HintAllocBlockLink& to_be_prev) noexcept;
    inline constexpr void remove_from_list() noexcept;

    using Block = HintAllocBlock<BlockSize>;
    inline Block& block() noexcept;
};
template <size_t BlockSize>
struct HintAllocBlock {
    using Link = HintAllocBlockLink<BlockSize>;
    using UInt = FarMemory::Utility::EnoughUnsignedInteger<std::bit_width(BlockSize - 1)>;
    using FreeHeader = KRFreeHeader<UInt>;
    using Arena = HintAllocArena<BlockSize>;
    static_assert(BlockSize >= sizeof(FreeHeader) * 2);
    static_assert(BlockSize % sizeof(FreeHeader) == 0);

    Link link;
    UInt freep;
    UInt usage;

    void initialize(uintptr_t head_addr) noexcept;

    // return true if succeeded
    template <size_t ElemSize, size_t Alignment>
    inline bool allocate(size_t n_elems, uintptr_t head_addr, void*& ret) noexcept;
    template <size_t ElemSize, size_t Alignment>
    inline void deallocate(void* ptr, size_t n_elems, uintptr_t head_addr) noexcept;

    inline constexpr bool is_empty() noexcept { return usage == 0; }
    inline static constexpr size_t max_size() { return BlockSize - sizeof(FreeHeader); }
};


template <size_t BlockSize>
struct HintAllocArenaLink {
    HintAllocArenaLink* next;
    HintAllocArenaLink* prev;

    inline constexpr void insert_prev(HintAllocArenaLink& to_be_prev) noexcept;
    inline constexpr void remove_from_list() noexcept;

    using Arena = HintAllocArena<BlockSize>;
    inline Arena& arena() noexcept;
};


template <size_t BlockSize, size_t NBlocks_>
struct HintAllocArenaMetadata {
    inline static constexpr size_t NBlocks = NBlocks_,
                                   DataNPages = (BlockSize * NBlocks + PageSize - 1) / PageSize;
    using Link = HintAllocArenaLink<BlockSize>;
    using Block = HintAllocBlock<BlockSize>;
    inline static constexpr int BitmapWidth = (NBlocks + 63) / 64;

    Link link;
    size_t num_of_used_blocks;
    std::array<uint64_t, (NBlocks + 63) / 64> is_block_used;
    LocalMemoryStoreBuffer store_buf;
    std::array<Block, NBlocks> blocks_tab;
};


template <size_t BlockSize, size_t Max = SizeClass::MaxNPages>
inline consteval size_t NBlocksForHintAllocArena();


template <size_t BlockSize>
struct HintAllocArena : HintAllocArenaMetadata<BlockSize, NBlocksForHintAllocArena<BlockSize>()> {
    using Base = HintAllocArenaMetadata<BlockSize, NBlocksForHintAllocArena<BlockSize>()>;
    using Block = Base::Block;

    inline static constexpr size_t ArenaAlignment = ArenaSize,
                                   DataAlignment = size_t{1} << std::countr_zero(BlockSize);
    inline static constexpr size_t NBlocks = Base::NBlocks, DataNPages = Base::DataNPages,
                                   MetadataNPages = ArenaSize / PageSize - DataNPages;

    constexpr Block& block(size_t idx) noexcept { return this->blocks_tab[idx]; }

protected:
    inline HintAllocArena();

public:
    inline static HintAllocArena& create();
    HintAllocArena(const HintAllocArena&) = delete;

    ~HintAllocArena();

    inline static HintAllocArena& from_inside_ptr(const void* ptr) noexcept;
    inline static size_t data_ptr2idx(const void* ptr) noexcept;
    inline size_t metadata_ptr2idx(const Block* ptr) noexcept;
    inline uintptr_t block_idx2head_ptr(size_t idx) noexcept;

    inline constexpr int find_free_and_allocate() noexcept;
    inline constexpr void free(const size_t block_idx) noexcept;
    inline constexpr bool is_empty() const noexcept;
};


template <size_t BlockSize>
struct HintAllocatorImpl {
    using Arena = HintAllocArena<BlockSize>;
    using ArenaLink = HintAllocArenaLink<BlockSize>;
    using Block = Arena::Block;
    using BlockLink = Block::Link;

    Arena* current_arena{};
    ArenaLink non_full_arenas{&non_full_arenas, &non_full_arenas};
    size_t current_block_idx;
    BlockLink non_full_blocks{&non_full_blocks, &non_full_blocks};

    size_t ref_count{0};

    inline static void dec_ref(HintAllocatorImpl* ptr) noexcept;
    inline std::unique_ptr<HintAllocatorImpl, void (*)(HintAllocatorImpl*)> shallow_copy() noexcept;
    ~HintAllocatorImpl();

    template <size_t ElemSize, size_t Alignment>
    inline void* allocate(size_t n_elems);
    template <size_t ElemSize, size_t Alignment>
    inline void* allocate(size_t n_elems, const void* hint);
    template <size_t ElemSize, size_t Alignment>
    inline void deallocate(void* ptr, size_t n_elems);
};


template <class T, size_t BlockSize>
struct HintAllocator {
    using Impl = HintAllocatorImpl<BlockSize>;

    std::invoke_result_t<decltype(&Impl::shallow_copy), Impl*> pimpl;

    inline constexpr HintAllocator() : pimpl{(new Impl)->shallow_copy()} {}
    inline constexpr HintAllocator(const HintAllocator& other) noexcept : pimpl{other.pimpl->shallow_copy()} {}
    inline constexpr HintAllocator& operator=(const HintAllocator& other) noexcept { pimpl = other.pimpl->shallow_copy(); }
    inline constexpr HintAllocator(HintAllocator&&) noexcept = default;
    inline constexpr HintAllocator& operator=(HintAllocator&&) noexcept = default;

    using value_type = T;

    template <class U>
    struct rebind {
        using other = HintAllocator<U, BlockSize>;
    };

    template <class U>
    inline constexpr HintAllocator(const HintAllocator<U, BlockSize>& other) noexcept : pimpl{other.pimpl->shallow_copy()}
    {
    }
    template <class U>
    inline constexpr HintAllocator(HintAllocator<U, BlockSize>&& other) noexcept : pimpl{std::move(other.pimpl)}
    {
    }

    [[nodiscard]] inline T* allocate(size_t n);
    [[nodiscard]] inline T* allocate(size_t n, const void* hint);
    inline void deallocate(T* p, size_t n) noexcept;
};

}  // namespace FarMalloc


#include <farmalloc/hint_allocator.ipp>
