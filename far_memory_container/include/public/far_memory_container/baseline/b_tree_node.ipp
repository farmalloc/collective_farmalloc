#pragma once

#include <far_memory_container/baseline/b_tree.hpp>

#include <bit>
#include <cstddef>


namespace FarMemoryContainer::Baseline
{

template <class PtrToVal, size_t MaxNElems>
template <class Key, class key_compare>
size_t BTreeNode<PtrToVal, MaxNElems>::upper_bound(const Key& key, key_compare& comp) const
{
    const auto num_comp = std::bit_width(n_elems);
    const size_t first_idx = n_elems - (size_t(1) << (num_comp - 1));
    size_t pos = (comp(key, elems[first_idx].get()->first) ? 0 : first_idx + 1);

    for (size_t i = num_comp - 1; i != 0; i--) {
        const size_t step = size_t(1) << (i - 1);
        pos = (comp(key, elems[pos + step - 1].get()->first) ? pos : pos + step);
    }

    return pos;
}

}  // namespace FarMemoryContainer::Baseline
