#include <far_memory_container/baseline/skiplist.hpp>
#include <farmalloc/hint_allocator.hpp>

#include <farmalloc/local_memory_store.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>
#include <random>


namespace Tests::CollectiveAllocator
{

using namespace FarMalloc;


TEST_CASE("FarMemoryContainer::Baseline::SkiplistMap")
{
    using Alloc = FarMalloc::HintAllocator<std::pair<const size_t, size_t>, PageSize>;
    FarMemoryContainer::Baseline::SkiplistMap<size_t, size_t, std::less<size_t>, Alloc> map{};

    constexpr size_t NumElements = 100000;
    static_assert(NumElements >= 1);

    std::vector<size_t> vector(NumElements);
    {
        std::iota(vector.begin(), vector.end(), size_t{1});
        {
            std::minstd_rand rand_device;
            std::shuffle(vector.begin(), vector.end(), rand_device);
        }
        size_t max = 0, min = max - size_t{1}, cnt = 0;
        for (auto num : vector) {
            max = std::max(max, num);
            min = std::min(min, num);
            REQUIRE_NOTHROW(map.insert({num, num}));
            REQUIRE(map.crbegin()->first == max);
            REQUIRE(map.cbegin()->first == min);
            cnt++;
            REQUIRE(map.size() == cnt);
        }
        REQUIRE_FALSE(map.insert({1, 1}).second);
    }

    REQUIRE_NOTHROW(map.batch_block());
    REQUIRE_NOTHROW(LocalMemoryStore::mode_change());

    {
        auto iter = map.cbegin();
        size_t i = 1;
        for (; iter != map.cend(); iter++, i++) {
            REQUIRE(iter->first == i);
            REQUIRE(iter->second == i);
        }
        REQUIRE(i == NumElements + 1);
    }

    for (size_t i = 1; i <= NumElements; i++) {
        auto found = map.find(i);
        REQUIRE(found != map.end());
        REQUIRE(found->second == i);
    }

    for (size_t i = NumElements + 1; i < NumElements * 105 / 100 + 1; i++) {
        auto found = map.find(i);
        REQUIRE(found == map.end());
    }

    REQUIRE(map.size() == NumElements);

    for (size_t i = 1; i < NumElements / 2; i++) {
        REQUIRE(map.cbegin()->first == i);
        REQUIRE(map.erase(i) == 1);
        REQUIRE(map.size() == NumElements - i);
    }

    {
        std::minstd_rand rand_device;
        std::shuffle(vector.begin(), vector.end(), rand_device);
    }
    for (auto elem : vector) {
        REQUIRE_NOTHROW(map.insert({elem, elem}));
    }

    for (size_t i = 1; i < NumElements / 2; i++) {
        REQUIRE(map.cbegin()->first == i);
        REQUIRE(map.erase(i) == 1);
        REQUIRE(map.size() == NumElements - i);
    }

    REQUIRE_NOTHROW(map.clear());

    REQUIRE_NOTHROW(map.insert({1, 1}));
    REQUIRE(map.erase(1) == 1);

    REQUIRE(map.empty());
}

}  // namespace Tests::CollectiveAllocator
