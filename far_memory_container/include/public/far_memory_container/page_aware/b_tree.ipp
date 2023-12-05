#pragma once

#include <far_memory_container/page_aware/b_tree.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>  // for analyze_locality_in_traversal
#include <utility>


namespace FarMemoryContainer::PageAware
{

template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
BTreeMap<Key, T, MaxNElems, Compare, Allocator>::~BTreeMap() noexcept
{
    clear();

    header->~Node();
    AllocTraits::deallocate(alloc, std::move(header), 1);
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
void BTreeMap<Key, T, MaxNElems, Compare, Allocator>::clear()
{
    if (header->children[0] != nullptr) {
        clear_step(header->children[0]);
    }
    header->n_elems = 1;
    std::ranges::fill(header->children, nullptr);
    header->parent = nullptr;

    size_cnt = 0;
    begin_node = header;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
void BTreeMap<Key, T, MaxNElems, Compare, Allocator>::clear_step(NodePtr node)
{
    if (/* inner node */ node->children[0] != nullptr) {
        for (size_t i = 0; i <= node->n_elems; i++) {
            clear_step(node->children[i]);
        }
    }

    for (size_t i = 0; i != node->n_elems; i++) {
        node->elems[i].destroy(alloc);
    }
    node->~Node();
    AllocTraits::deallocate(alloc, std::move(node), 1);
}


template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
template <class K>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::find_impl(const K& x) const -> iterator
{
    for (NodePtr node = header->children[0]; node != nullptr;) {
        const auto upper_bound = node->upper_bound(x, comp);
        if (upper_bound == 0 || comp(node->elems[upper_bound - 1].get()->first, x)) {
            node = node->children[upper_bound];
        } else {
            return iterator{{.node = node, .elem_idx = upper_bound - 1}};
        }
    }
    return iterator{{.node = header, .elem_idx = 0}};
}


template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::insert(value_type&& x) -> std::pair<iterator, bool>
{
    auto suballoc = AllocTraits::get_suballocator(alloc, header);
    NodePtr root = header->children[0];
    if (/* first element */ root == nullptr) {
        auto root = SuballocTraits::allocate(suballoc, 1);
        new (&(*root)) Node{.n_elems = 1, .children = {}, .parent = header};
        root->elems[0].construct(alloc, std::move(x));

        header->children[0] = begin_node = root;
        size_cnt = 1;

        return {{{.node = std::move(root), .elem_idx = 0}}, true};
    }

    auto res = insert_step(std::move(x), root, AllocTraits::get_suballocator(alloc, header));
    if (res.new_child != nullptr) {
        auto new_node = SuballocTraits::allocate(suballoc, 1);

        new (&(*new_node)) Node{.n_elems = 1, .children = {root, res.new_child}, .parent = header};
        header->children[0] = root->parent = res.new_child->parent = new_node;
        new_node->elems[0].construct(alloc, std::move(*res.pushed_up));

        if (res.result.first.node == nullptr) {
            res.result.first = {{.node = new_node, .elem_idx = 0}};
        }
    }

    return res.result;
}

template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::insert_step(value_type&& x, NodePtr node, Suballoc suballoc) -> InsertStepResult
{
    const auto upper_bound = node->upper_bound(x.first, comp);
    if (/* already exists */ upper_bound != 0 && !comp(node->elems[upper_bound - 1].get()->first, x.first)) {
        return {.result = {{{.node = std::move(node), .elem_idx = upper_bound - 1}}, false}};
    }

    auto node_suballoc = AllocTraits::get_suballocator(alloc, node);

    bool to_insert;
    InsertStepResult res = (/* leaf node */ node->children[upper_bound] == nullptr
        ? to_insert = true,
        size_cnt++,
        InsertStepResult{.result = {{{.node = nullptr}}, true}, .new_child = nullptr}
        : [&] {
              auto res = insert_step(std::move(x), node->children[upper_bound], std::move(node_suballoc));
              to_insert = (res.new_child != nullptr);
              return res;
          }());
    if (!to_insert) {
        return res;
    }
    auto&& inserted = std::move(node->children[upper_bound] == nullptr ? x : *res.pushed_up);

    if (/* not full */ node->n_elems != MaxNElems) {
        for (size_t i = node->n_elems; i != upper_bound; i--) {
            node->elems[i].move_from(alloc, node->elems[i - 1]);
            node->children[i + 1] = std::move(node->children[i]);
        }
        node->elems[upper_bound].construct(alloc, std::move(inserted));
        node->children[upper_bound + 1] = std::move(res.new_child);
        res.new_child = nullptr;
        node->n_elems++;

        if (res.result.first.node == nullptr) {
            res.result.first = {{.node = node, .elem_idx = upper_bound}};
        }

        return res;
    }

    auto allocated = SuballocTraits::batch_allocate(suballoc, request::single<Node>());
    if (!allocated) {
        suballoc = AllocTraits::get_suballocator(alloc, swappable_plain);
        allocated = SuballocTraits::batch_allocate(suballoc, request::single<Node>());
    }
    if (!allocated) {
        throw std::bad_alloc{};
    }
    auto& [new_node] = *allocated;

    new (&(*new_node)) Node{.n_elems = MaxNElems / 2, .children = {}, .parent = node->parent};
    node->n_elems = (MaxNElems + 1) / 2;

    if (/* pass through this node */ upper_bound == node->n_elems) {
        for (size_t i = MaxNElems - 1; i >= node->n_elems; i--) {
            new_node->elems[i - node->n_elems].move_from(alloc, node->elems[i]);
            new_node->children[i - node->n_elems + 1] = std::move(node->children[i + 1]);
            node->children[i + 1] = nullptr;
        }
        new_node->children[0] = std::move(res.new_child);
        if (/* leaf node */ node->children[0] == nullptr) {
            res.pushed_up.emplace(std::move(inserted));
        }

    } else if (/* inserted to the original node */ upper_bound < node->n_elems) {
        for (size_t i = MaxNElems - 1; i >= node->n_elems; i--) {
            new_node->elems[i - node->n_elems].move_from(alloc, node->elems[i]);
            new_node->children[i - node->n_elems + 1] = std::move(node->children[i + 1]);
            node->children[i + 1] = nullptr;
        }
        new_node->children[0] = std::move(node->children[node->n_elems]);
        value_type pushed_up = std::move(*node->elems[node->n_elems - 1].get());
        node->elems[node->n_elems - 1].destroy(alloc);
        for (size_t i = node->n_elems - 1; i != upper_bound; i--) {
            node->elems[i].move_from(alloc, node->elems[i - 1]);
            node->children[i + 1] = std::move(node->children[i]);
        }
        node->elems[upper_bound].construct(alloc, std::move(inserted));
        node->children[upper_bound + 1] = std::move(res.new_child);
        if (res.result.first.node == nullptr) {
            res.result.first = {{.node = node, .elem_idx = upper_bound}};
        }
        res.pushed_up.emplace(std::move(pushed_up));

    } else /* inserted to the new node */ {
        for (size_t i = MaxNElems; i != upper_bound; i--) {
            new_node->elems[i - node->n_elems - 1].move_from(alloc, node->elems[i - 1]);
            new_node->children[i - node->n_elems] = std::move(node->children[i]);
            node->children[i] = nullptr;
        }
        new_node->children[upper_bound - node->n_elems] = std::move(res.new_child);
        new_node->elems[upper_bound - node->n_elems - 1].construct(alloc, std::move(inserted));
        new_node->children[upper_bound - node->n_elems - 1] = std::move(node->children[upper_bound]);
        for (size_t i = upper_bound - 1; i != node->n_elems; i--) {
            new_node->elems[i - node->n_elems - 1].move_from(alloc, node->elems[i]);
            new_node->children[i - node->n_elems - 1] = std::move(node->children[i]);
            node->children[i] = nullptr;
        }
        if (res.result.first.node == nullptr) {
            res.result.first = {{.node = new_node, .elem_idx = upper_bound - node->n_elems}};
        }
        res.pushed_up.emplace(std::move(*node->elems[node->n_elems].get()));
        node->elems[node->n_elems].destroy(alloc);
    }

    if (/* inner node */ new_node->children[0] != nullptr) {
        for (size_t i = 0; i <= MaxNElems / 2; i++) {
            new_node->children[i]->parent = new_node;
        }
    }

    res.new_child = new_node;
    return res;
}

template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::erase(const key_type& key) -> size_type
{
    NodePtr root = header->children[0];
    if (root == nullptr) {
        return 0;
    }
    const auto result = erase_step(key, header->children[0], &header->elems[0]).result;

    if (root->n_elems == 0) {
        header->children[0] = std::move(root->children[0]);
        if (header->children[0] != nullptr) {
            header->children[0]->parent = header;
        }

        root->~Node();
        AllocTraits::deallocate(alloc, std::move(root), 1);
    }

    return result;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::erase_step(const key_type& key, NodePtr node, AlignedBuffer<value_type>* successor) -> EraseStepResult
{
    const auto upper_bound = node->upper_bound(key, comp);
    if (/* already exists */ upper_bound != 0 && !comp(node->elems[upper_bound - 1].get()->first, key)) {
        node->elems[upper_bound - 1].destroy(alloc);
        size_cnt--;

        if (/* leaf node */ node->children[upper_bound - 1] == nullptr) {
            return {.result = 1, .pulled_down = fill_hole(upper_bound - 1, node, successor)};
        }
        const auto* pulled_down = swap_predecessor(node->elems[upper_bound - 1], node->children[upper_bound - 1], &node->elems[upper_bound - 1]);
        return {.result = 1, .pulled_down = (pulled_down == nullptr ? nullptr : fill_hole(pulled_down - &node->elems[0], node, successor))};
    }

    if (/* leaf node */ node->children[upper_bound] == nullptr) {
        return {.result = 0};
    }
    auto res = erase_step(key, node->children[upper_bound], &node->elems[upper_bound]);
    if (res.pulled_down != nullptr) {
        res.pulled_down = fill_hole(res.pulled_down - &node->elems[0], node, successor);
    }
    return res;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::swap_predecessor(AlignedBuffer<value_type>& target, NodePtr node, AlignedBuffer<value_type>* successor) -> AlignedBuffer<value_type>*
{
    if (/* leaf node */ node->children[node->n_elems] == nullptr) {
        target.move_from(alloc, node->elems[node->n_elems - 1]);
        return fill_hole(node->n_elems - 1, node, successor);
    }
    const auto* pulled_down = swap_predecessor(target, node->children[node->n_elems], &node->elems[node->n_elems]);
    return (pulled_down == nullptr ? nullptr : fill_hole(pulled_down - &node->elems[0], node, successor));
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::fill_hole(const size_t idx_hole, NodePtr node, AlignedBuffer<value_type>* successor) -> AlignedBuffer<value_type>*
{
    for (size_t i = idx_hole + 1; i != node->n_elems; i++) {
        node->elems[i - 1].move_from(alloc, node->elems[i]);
        node->children[i] = std::move(node->children[i + 1]);
    }
    node->children[node->n_elems] = nullptr;

    if (/* not minimal */ node->n_elems != MinNElems || /* root */ node == header->children[0]) {
        node->n_elems--;
        return nullptr;
    }

    const auto child_iter_to_node = std::ranges::find(node->parent->children, node);
    NodePtr prev = nullptr;
    if (node->parent->children[0] != node) {
        prev = *(child_iter_to_node - 1);
        if (prev->n_elems != MinNElems) {
            for (size_t i = MinNElems - 1; i != 0; i--) {
                node->elems[i].move_from(alloc, node->elems[i - 1]);
                node->children[i + 1] = std::move(node->children[i]);
            }
            node->children[1] = std::move(node->children[0]);
            node->elems[0].move_from(alloc, *(successor - 1));
            (successor - 1)->move_from(alloc, prev->elems[prev->n_elems - 1]);
            node->children[0] = std::move(prev->children[prev->n_elems]);
            if (/* inner node */ node->children[0] != nullptr) {
                node->children[0]->parent = node;
            }
            prev->children[prev->n_elems] = nullptr;
            prev->n_elems--;
            return nullptr;
        }
    }

    NodePtr next = nullptr;
    if (node->parent->children[node->parent->n_elems] != node) {
        next = *(child_iter_to_node + 1);
        if (next->n_elems != MinNElems) {
            node->elems[MinNElems - 1].move_from(alloc, *successor);
            node->children[MinNElems] = std::move(next->children[0]);
            if (/* inner node */ node->children[MinNElems] != nullptr) {
                node->children[MinNElems]->parent = node;
            }
            successor->move_from(alloc, next->elems[0]);
            next->children[0] = std::move(next->children[1]);
            for (size_t i = 1; i != next->n_elems; i++) {
                next->elems[i - 1].move_from(alloc, next->elems[i]);
                next->children[i] = std::move(next->children[i + 1]);
            }
            next->children[next->n_elems] = nullptr;
            next->n_elems--;
            return nullptr;
        }
    }

    node->n_elems--;
    if (prev != nullptr) {
        next = std::move(node);
        node = std::move(prev);
        successor--;
    }

    node->elems[node->n_elems].move_from(alloc, *successor);
    for (size_t i = 0; i != next->n_elems; i++) {
        node->elems[node->n_elems + 1 + i].move_from(alloc, next->elems[i]);
    }
    if (/* inner node */ next->children[0] != nullptr) {
        for (size_t i = 0; i <= next->n_elems; i++) {
            node->children[node->n_elems + 1 + i] = std::move(next->children[i]);
            node->children[node->n_elems + 1 + i]->parent = node;
        }
    }
    node->n_elems = 2 * MinNElems;

    next->~Node();
    AllocTraits::deallocate(alloc, std::move(next), 1);
    return successor;
}

template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
void BTreeMap<Key, T, MaxNElems, Compare, Allocator>::relocate(NodePtr& node, Suballoc suballoc)
{
    const bool relocating_begin_node = (node == begin_node);
    const auto child_iter_to_node = std::ranges::find(node->parent->children, node);

    if (AllocTraits::relocate(
            alloc, suballoc, [this](Node* from, auto, Node* to) {
                new (to) Node{.n_elems = from->n_elems, .children = from->children, .parent = from->parent};
                for (size_t i = 0; i != to->n_elems; i++) {
                    to->elems[i].move_from(alloc, from->elems[i]);
                }
                from->~Node();
            },
            std::tie(node), request::single<Node>())) {

        if (child_iter_to_node != node->parent->children.end()) {
            *child_iter_to_node = node;
        }
        if (/* inner node */ node->children[0] != nullptr) {
            for (size_t i = 0; i <= node->n_elems; i++) {
                node->children[i]->parent = node;
            }
        }
        if (relocating_begin_node) {
            begin_node = node;
        }
    }
}


template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
void BTreeMap<Key, T, MaxNElems, Compare, Allocator>::batch_block()
{
    if (size_cnt == 0) {
        return;
    }
    auto block = AllocTraits::get_suballocator(alloc, new_per_page);
    batch_block_step(header->children[0], block);
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
void BTreeMap<Key, T, MaxNElems, Compare, Allocator>::batch_block_step(NodePtr node, Suballoc& block)
{
    if (/* inner node */ node->children[0] != nullptr) {
        for (size_t i = 0; i <= node->n_elems; i++) {
            batch_block_step(node->children[i], block);
        }
    }

    if (auto local = AllocTraits::get_suballocator(alloc, purely_local);
        !AllocTraits::if_suballocator_contains(alloc, local, node)) {
        if (!SuballocTraits::is_occupancy_under(block, 0.7)) {
            block = AllocTraits::get_suballocator(alloc, new_per_page);
        }
        relocate(node, block);
    }
}

template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
void BTreeMap<Key, T, MaxNElems, Compare, Allocator>::batch_vEB()
{
    if (size_cnt == 0) {
        return;
    }

    auto root = header->children[0];
    size_t height = 0;
    for (auto node = root; node != nullptr; node = node->children[0]) {
        height++;
    }
    auto block = AllocTraits::get_suballocator(alloc, new_per_page);
    batch_vEB_step(root, height, block);
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
void BTreeMap<Key, T, MaxNElems, Compare, Allocator>::batch_vEB_step(NodePtr& node, size_t height, Suballoc& block)
{
    switch (height) {
    case 0:
        return;
    case 1: {
        if (auto local = AllocTraits::get_suballocator(alloc, purely_local);
            !AllocTraits::if_suballocator_contains(alloc, local, node)) {
            if (!SuballocTraits::is_occupancy_under(block, 0.7)) {
                block = AllocTraits::get_suballocator(alloc, new_per_page);
            }
            relocate(node, block);
        }
    } break;

    default: {
        const auto upper_height = (height + 1) / 2, lower_height = height - upper_height;
        batch_vEB_step(node, upper_height, block);

        auto lower_first = node, lower_last = lower_first;
        for (auto down_cnt = upper_height; down_cnt != 0; down_cnt--) {
            lower_first = lower_first->children[0];
            lower_last = lower_last->children[lower_last->n_elems];
        }

        for (auto subtree = lower_first; subtree != lower_last;) {
            batch_vEB_step(subtree, lower_height, block);

            size_t down_cnt = 0;
            for (auto parent = subtree->parent; subtree == parent->children[parent->n_elems]; subtree = parent, parent = parent->parent, down_cnt++) {
            }
            subtree = *(std::ranges::find(subtree->parent->children, subtree) + 1);
            for (; down_cnt != 0; down_cnt--) {
                subtree = subtree->children[0];
            }
        }
        batch_vEB_step(lower_last, lower_height, block);
    } break;
    }
}

template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
template <size_t PageAlign>
std::array<size_t, 3> BTreeMap<Key, T, MaxNElems, Compare, Allocator>::analyze_edges()
{
    std::array<size_t, 3> res{};
    if (header->children[0] != nullptr) {
        analyze_edges_step<PageAlign>(header->children[0], res);
    }
    return res;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
template <size_t PageAlign>
void BTreeMap<Key, T, MaxNElems, Compare, Allocator>::analyze_edges_step(NodePtr node, std::array<size_t, 3>& acc)
{
    if (/* inner node */ node->children[0] != nullptr) {
        auto local = AllocTraits::get_suballocator(alloc, purely_local);
        const bool is_node_local = AllocTraits::if_suballocator_contains(alloc, local, node);

        for (size_t i = 0; i <= node->n_elems; i++) {
            NodePtr child = node->children[i];
            const bool is_child_local = AllocTraits::if_suballocator_contains(alloc, local, child);
            if (is_node_local && is_child_local) {
                acc[0] += 1;
            } else {
                bool are_on_diff_pages = (reinterpret_cast<uintptr_t>(&(*node)) / PageAlign != reinterpret_cast<uintptr_t>(&(*child)) / PageAlign);
                acc[are_on_diff_pages ? 2 : 1] += 1;
            }
            analyze_edges_step<PageAlign>(child, acc);
        }
    }
}

template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
template <size_t PageAlign>
std::array<size_t, 3> BTreeMap<Key, T, MaxNElems, Compare, Allocator>::analyze_locality_in_traversal(size_t cache_size)
{
    std::array<size_t, 3> res{};
    if (header->children[0] != nullptr) {
        auto cached_pages_ary = std::make_unique<uintptr_t[]>(cache_size);
        std::fill_n(&cached_pages_ary[0], cache_size, reinterpret_cast<uintptr_t>(nullptr) / PageAlign);
        size_t lr_cache_idx = 0;
        std::set<uintptr_t> cached_pages_set;

        analyze_locality_in_traversal_step<PageAlign>(header->children[0], cache_size, cached_pages_set, &cached_pages_ary[0], lr_cache_idx, res);
    }
    return res;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
template <size_t PageAlign>
void BTreeMap<Key, T, MaxNElems, Compare, Allocator>::analyze_locality_in_traversal_step(NodePtr node, const size_t cache_size, std::set<uintptr_t>& cached_pages_set, uintptr_t* const cached_pages_ary, size_t& lr_cache_idx, std::array<size_t, 3>& cnt)
{
    auto local = AllocTraits::get_suballocator(alloc, purely_local);
    const bool is_node_local = AllocTraits::if_suballocator_contains(alloc, local, node);

    const auto page_id = reinterpret_cast<uintptr_t>(&(*node)) / PageAlign;
    const auto touch_this_node = [&] {
        if (is_node_local) {
            cnt[0]++;
        } else {
            const bool is_cache_hit = cached_pages_set.contains(page_id);
            cnt[is_cache_hit ? 1 : 2]++;

            if (!is_cache_hit) {
                const auto iter = cached_pages_set.find(cached_pages_ary[lr_cache_idx]);
                if (iter != cached_pages_set.cend()) {
                    auto node = cached_pages_set.extract(iter);
                    node.value() = page_id;
                    cached_pages_set.insert(std::move(node));
                } else {
                    cached_pages_set.insert(page_id);
                }

                cached_pages_ary[lr_cache_idx] = page_id;
                lr_cache_idx++;
                if (lr_cache_idx == cache_size) {
                    lr_cache_idx = 0;
                }
            }
        }
    };

    if (/* is leaf */ node->children[0] == nullptr) {
        touch_this_node();
        return;
    }

    analyze_locality_in_traversal_step<PageAlign>(node->children[0], cache_size, cached_pages_set, cached_pages_ary, lr_cache_idx, cnt);
    for (size_t i = 1; i <= node->n_elems; i++) {
        touch_this_node();
        analyze_locality_in_traversal_step<PageAlign>(node->children[i], cache_size, cached_pages_set, cached_pages_ary, lr_cache_idx, cnt);
    }
}

}  // namespace FarMemoryContainer::PageAware
