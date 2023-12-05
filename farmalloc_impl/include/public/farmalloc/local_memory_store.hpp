#pragma once

#include <farmalloc/collective_allocator_params.hpp>

#include <umap/store/Store.hpp>

#include <atomic>
#include <cstddef>

#include <bit>
#include <unordered_map>
#include <utility>


namespace FarMalloc
{

struct LocalMemoryStore : Umap::Store {
    static std::atomic_uint64_t read_cnt;
    static std::atomic_uint64_t write_cnt;

    std::byte* backing_data;

    inline LocalMemoryStore(size_t size);
    inline void destroy(size_t size);

    inline ssize_t read_from_store(char* buf, size_t size_in_bytes, off_t off) noexcept override;
    inline ssize_t write_to_store(char* buf, size_t size_in_bytes, off_t off) noexcept override;

    inline static constexpr auto arena_ptr_hash(void* ptr) {
        return static_cast<size_t>(std::rotl(reinterpret_cast<uintptr_t>(ptr), std::bit_width(ArenaSize) - 1));
    }
    using MappingType = std::unordered_map<void*, std::pair<size_t, LocalMemoryStore*>, size_t(*)(void*)>;
    static MappingType mapping;
    static bool far_memory_mode;

    inline static void umap(void* ptr, size_t size, LocalMemoryStore* store);
    inline static bool mode_change();
    inline static void uunmap(void* ptr, size_t size);
};


struct LocalMemoryStoreBuffer {
    alignas(LocalMemoryStore) std::byte buf[sizeof(LocalMemoryStore)];
    inline LocalMemoryStore* construct(size_t size);
    inline void destroy(size_t size);
};

}  // namespace FarMalloc

#include <farmalloc/local_memory_store.ipp>
