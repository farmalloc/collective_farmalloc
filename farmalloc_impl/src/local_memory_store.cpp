#include <farmalloc/local_memory_store.hpp>

#include <atomic>


namespace FarMalloc
{

std::atomic_uint64_t LocalMemoryStore::read_cnt = 0;
std::atomic_uint64_t LocalMemoryStore::write_cnt = 0;

LocalMemoryStore::MappingType LocalMemoryStore::mapping{0, &LocalMemoryStore::arena_ptr_hash};
bool LocalMemoryStore::far_memory_mode = false;

}  // namespace FarMalloc
