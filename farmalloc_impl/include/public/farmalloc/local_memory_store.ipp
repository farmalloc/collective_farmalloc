#pragma once

#include <farmalloc/local_memory_store.hpp>

#include <farmalloc/collective_allocator_params.hpp>

#include <umap/umap.h>

#include <errno.h>     // errno
#include <sys/mman.h>  // mmap, munmap

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <system_error>
#include <utility>


namespace FarMalloc
{

LocalMemoryStore::LocalMemoryStore(size_t size)
{
    const auto mmap_result = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mmap_result == MAP_FAILED) [[unlikely]] {
        if (errno == ENOMEM) [[likely]] {
            throw std::bad_alloc{};
        }
        throw std::system_error{errno, std::generic_category(), "mmap"};
    }
    backing_data = reinterpret_cast<std::byte*>(mmap_result);
}
void LocalMemoryStore::destroy(size_t size)
{
    if (munmap(backing_data, size) == -1) [[unlikely]] {
        if (errno == ENOMEM) [[likely]] {
            throw std::bad_alloc{};
        }
        throw std::system_error{errno, std::generic_category(), "munmap"};
    }
}

ssize_t LocalMemoryStore::read_from_store(char* buf, size_t size_in_bytes, off_t off) noexcept
{
    read_cnt++;
    std::memcpy(buf, backing_data + off, size_in_bytes);
    return size_in_bytes;
}
ssize_t LocalMemoryStore::write_to_store(char* buf, size_t size_in_bytes, off_t off) noexcept
{
    write_cnt++;
    std::memcpy(backing_data + off, buf, size_in_bytes);
    return size_in_bytes;
}


void LocalMemoryStore::umap(void* ptr, size_t size, LocalMemoryStore* store)
{
    mapping.insert({ptr, {size, store}});
    if (far_memory_mode) {
        void* const mapped = Umap::umap_ex(ptr, size, PROT_READ | PROT_WRITE, UMAP_PRIVATE | UMAP_FIXED, -1, 0, store);
        if (mapped == UMAP_FAILED) [[unlikely]] {
            throw std::bad_alloc{};
        }
    }
}
bool LocalMemoryStore::mode_change()
{
    if (far_memory_mode) {
        for (const auto& [ptr, map] : mapping) {
            const auto& [size, store] = map;
            ::uunmap(ptr, size);
        }
        far_memory_mode = false;
    } else {
        for (const auto& [ptr, map] : mapping) {
            const auto& [size, store] = map;
            std::memcpy(store->backing_data, ptr, size);
            if (madvise(ptr, size, MADV_DONTNEED) != 0) {
                throw std::system_error{errno, std::generic_category(), "madvise(MADV_DONTNEED)"};
            }
            void* const mapped = Umap::umap_ex(ptr, size, PROT_READ | PROT_WRITE, UMAP_PRIVATE | UMAP_FIXED, -1, 0, store);
            if (mapped == UMAP_FAILED) [[unlikely]] {
                throw std::bad_alloc{};
            }
        }
        far_memory_mode = true;
    }
    return far_memory_mode;
}
void LocalMemoryStore::uunmap(void* ptr, size_t size)
{
    mapping.erase(ptr);
    if (far_memory_mode) {
        ::uunmap(ptr, size);
    }
}


LocalMemoryStore* LocalMemoryStoreBuffer::construct(size_t size)
{
    return std::construct_at(reinterpret_cast<LocalMemoryStore*>(buf), size);
}
void LocalMemoryStoreBuffer::destroy(size_t size)
{
    std::launder(reinterpret_cast<LocalMemoryStore*>(buf))->destroy(size);
}

}  // namespace FarMalloc
