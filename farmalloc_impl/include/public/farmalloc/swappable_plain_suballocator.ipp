#pragma once

#include <farmalloc/swappable_plain_suballocator.hpp>

#include <farmalloc/page_size.hpp>
#include <farmalloc/local_memory_store.hpp>
#include <farmalloc/plain_suballoc_page_metadata.hpp>

#include <cstddef>
#include <memory>
#include <new>


namespace FarMalloc
{

SwappablePlainArena::SwappablePlainArena(FreePageLink& link) : Base(link)
{
    auto* const store = this->appendix.construct(Base::DataNPages * PageSize);
    LocalMemoryStore::umap(reinterpret_cast<void*>(Base::page_idx2head_ptr(0)), Base::DataNPages * PageSize, store);
}
SwappablePlainArena::~SwappablePlainArena()
{
    LocalMemoryStore::uunmap(reinterpret_cast<void*>(Base::page_idx2head_ptr(0)), Base::DataNPages * PageSize);
    this->appendix.destroy(Base::DataNPages * PageSize);
}

SwappablePlainArena& SwappablePlainArena::create(FreePageLink& link)
{
    const auto arena_addr = allocate_memory();
    return *new (arena_addr) SwappablePlainArena{link};
}
SwappablePlainArena& SwappablePlainArena::from_inside_ptr(const void* ptr) noexcept
{
    return static_cast<SwappablePlainArena&>(Base::from_inside_ptr(ptr));
}


constexpr size_t SwappablePlainCustom::large_alloc_size(size_t size) noexcept
{
    static_assert(alignof(LocalMemoryStore) <= SwappablePlainArena::ArenaAlignment);
    return (size + alignof(LocalMemoryStore) - 1) / alignof(LocalMemoryStore) * alignof(LocalMemoryStore) + sizeof(LocalMemoryStore);
}
void SwappablePlainCustom::postprocess_large_alloc(void* ptr, size_t size)
{
    const auto store_addr = reinterpret_cast<uintptr_t>(ptr) + size - sizeof(LocalMemoryStore);
    const auto umap_size = (size - sizeof(LocalMemoryStore)) / PageSize * PageSize;
    auto* const store = std::construct_at(reinterpret_cast<LocalMemoryStore*>(store_addr), umap_size);
    LocalMemoryStore::umap(ptr, umap_size, store);
}
void SwappablePlainCustom::preprocess_large_dealloc(void* ptr, size_t size)
{
    const auto store_addr = reinterpret_cast<uintptr_t>(ptr) + size - sizeof(LocalMemoryStore);
    const auto umap_size = (size - sizeof(LocalMemoryStore)) / PageSize * PageSize;
    auto* store = std::launder(reinterpret_cast<LocalMemoryStore*>(store_addr));
    LocalMemoryStore::uunmap(ptr, umap_size);
    store->destroy(umap_size);
}

}  // namespace FarMalloc
