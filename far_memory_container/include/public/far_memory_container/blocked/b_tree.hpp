#pragma once

#include <farmalloc/collective_allocator_traits.hpp>
#include <far_memory_container/aligned_buffer.hpp>
#include <util/enough_unsigned_integer.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>


namespace FarMemoryContainer
{

using namespace FarMalloc;

namespace Blocked
{

template <class PtrToVal, size_t MaxNElems>
struct BTreeNode {
    using value_type = std::remove_reference_t<decltype(*std::declval<PtrToVal>())>;
    using NodePtr = typename std::pointer_traits<PtrToVal>::template rebind<BTreeNode>;

    size_t n_elems;
    std::array<NodePtr, MaxNElems + 1> children;
    NodePtr parent;
    // All the nodes in a tree also compose a doubly-linked list.
    // The first and the last are *header, the second is the root if exists, and the other nodes follow in ascending order of depth.
    // The nodes with the same depth are ordered by the key.
    NodePtr prev, next;
    std::array<AlignedBuffer<value_type>, MaxNElems> elems{};

    template <class Key, class key_compare>
    inline size_t upper_bound(const Key& key, key_compare& comp) const;
};

template <class Node>
struct BTreeIterBase {
    using value_type = typename Node::value_type;
    using NodePtr = typename Node::NodePtr;

    NodePtr node = nullptr;
    size_t elem_idx = 0;

    friend bool operator==(const BTreeIterBase& lhs, const BTreeIterBase& rhs) noexcept { return lhs.elem_idx == rhs.elem_idx && lhs.node == rhs.node; }

    value_type* get() const noexcept { return node->elems[elem_idx].get(); }

    inline void increment();
    inline void decrement();
};

template <class Key, class T, size_t MaxNElems, class Compare = std::less<Key>, class Allocator = std::allocator<std::pair<const Key, T>>>
struct BTreeMap {
    static_assert(MaxNElems >= 2);
    static constexpr size_t MinNElems = MaxNElems / 2;

    using key_type = Key;
    using mapped_type = T;
    using key_compare = Compare;
    using allocator_type = Allocator;

    using value_type = std::pair<const Key, T>;
    using reference = value_type&;
    using const_reference = const value_type&;

    static_assert(std::is_same_v<typename collective_allocator_traits<allocator_type>::value_type, value_type>);

    struct value_compare {
    private:
        friend struct BTreeMap;
        key_compare comp;
        explicit value_compare(const key_compare& compare) : comp(compare) {}

    public:
        bool operator()(const value_type& lhs, const value_type& rhs) const
        {
            return comp(lhs.first, rhs.first);
        }
    };

private:
    using Node = BTreeNode<typename collective_allocator_traits<allocator_type>::pointer, MaxNElems>;
    using AllocTraits = typename collective_allocator_traits<allocator_type>::template rebind_traits<Node>;
    using Alloc = typename AllocTraits::allocator_type;
    using SuballocTraits = typename AllocTraits::suballocator_traits;
    using Suballoc = typename SuballocTraits::allocator_type;

public:
    using difference_type = std::common_type_t<typename AllocTraits::difference_type, ssize_t>;
    using size_type = std::make_unsigned_t<difference_type>;

    struct const_iterator : BTreeIterBase<Node> {
        using Base = BTreeIterBase<Node>;

        using difference_type = BTreeMap::difference_type;
        using value_type = typename Base::value_type;

        const value_type& operator*() const noexcept { return *Base::get(); }
        const value_type* operator->() const noexcept { return Base::get(); }

        inline const_iterator& operator++();
        inline const_iterator operator++(int);
        inline const_iterator& operator--();
        inline const_iterator operator--(int);
    };
    static_assert(std::bidirectional_iterator<const_iterator>);
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    struct iterator : BTreeIterBase<Node> {
        using Base = BTreeIterBase<Node>;

        using difference_type = BTreeMap::difference_type;
        using value_type = typename Base::value_type;

        value_type& operator*() const noexcept { return *Base::get(); }
        value_type* operator->() const noexcept { return Base::get(); }

        operator const_iterator() const& noexcept { return *this; }
        operator const_iterator() && noexcept { return *this; }

        inline iterator& operator++();
        inline iterator operator++(int);
        inline iterator& operator--();
        inline iterator operator--(int);
    };
    static_assert(std::bidirectional_iterator<iterator>);
    using reverse_iterator = std::reverse_iterator<iterator>;

private:
    using NodePtr = typename Node::NodePtr;

    size_type size_cnt = 0;
    [[no_unique_address]] key_compare comp = Compare();
    [[no_unique_address]] Alloc alloc = allocator_type();
    // invariant: header->parent == nullptr
    // invariant: size_cnt == 0 || header->children[0] == (the root node)
    // invariant: header->children[1] == nullptr
    NodePtr header = [this] {
        auto suballoc = AllocTraits::get_suballocator(alloc, purely_local);
        NodePtr tmp = SuballocTraits::allocate(suballoc, 1);
        new (&(*tmp)) Node{.n_elems = 1, .children = {}, .parent = nullptr, .prev = tmp, .next = tmp};
        return tmp;
    }();
    NodePtr begin_node = header;
    NodePtr last_local_node = header;

public:
    BTreeMap() {}
    BTreeMap(const Alloc& alloc) : alloc(alloc) {}
    inline ~BTreeMap() noexcept;

    iterator begin() noexcept { return iterator{{.node = begin_node, .elem_idx = 0}}; }
    const_iterator begin() const noexcept { return const_iterator{{.node = begin_node, .elem_idx = 0}}; }
    const_iterator cbegin() const noexcept { return begin(); }

    iterator end() noexcept { return iterator{{.node = header, .elem_idx = 0}}; }
    const_iterator end() const noexcept { return const_iterator{{.node = header, .elem_idx = 0}}; }
    const_iterator cend() const noexcept { return end(); }

    reverse_iterator rbegin() noexcept { return std::make_reverse_iterator(end()); }
    const_reverse_iterator rbegin() const noexcept { return std::make_reverse_iterator(end()); }
    const_reverse_iterator crbegin() const noexcept { return rbegin(); }

    reverse_iterator rend() noexcept { return std::make_reverse_iterator(begin()); }
    const_reverse_iterator rend() const noexcept { return std::make_reverse_iterator(begin()); }
    const_reverse_iterator crend() const noexcept { return rend(); }

    size_type size() const noexcept { return size_cnt; }
    inline size_type max_size();
    bool empty() const noexcept { return size_cnt == 0; }

    allocator_type get_allocator() const noexcept { return allocator_type(alloc); }

    iterator find(const key_type& x) { return find_impl(x); }
    const_iterator find(const key_type& x) const { return find_impl(x); }

    inline std::pair<iterator, bool> insert(value_type&& x);
    inline size_type erase(const key_type& x);

    inline void clear();

    inline void batch_block();
    inline void batch_vEB();

    //! @return [purely_local_edges, same_page_edges, diff_pages_edges]
    template <size_t PageAlign>
    inline std::array<size_t, 3> analyze_edges();
    //! @return [purely_local, cache_hit, cache_miss]
    template <size_t PageAlign>
    inline std::array<size_t, 3> analyze_locality_in_traversal(size_t cache_size_in_pages);

private:
    template <class K>
    inline iterator find_impl(const K& x) const;

    struct InsertStepResult {
        std::pair<iterator, bool> result;
        NodePtr new_child = nullptr;
        std::optional<value_type> pushed_up{};
    };

    inline InsertStepResult insert_step(value_type&& x, NodePtr node, Suballoc parent_suballoc);
    inline void relocate_last_local_to_far();

    struct EraseStepResult {
        size_type result;
        AlignedBuffer<value_type>* pulled_down = nullptr;
    };
    inline EraseStepResult erase_step(const key_type& key, NodePtr node, AlignedBuffer<value_type>* successor);
    inline AlignedBuffer<value_type>* fill_hole(size_t idx_hole, NodePtr node, AlignedBuffer<value_type>* successor);
    inline AlignedBuffer<value_type>* swap_predecessor(AlignedBuffer<value_type>&, NodePtr node, AlignedBuffer<value_type>* successor);
    inline void relocate_first_far_to_local();

    inline void relocate(NodePtr& node, Suballoc suballoc);

    inline void batch_block_step(NodePtr node, Suballoc& swappable_block);
    inline void batch_vEB_step(NodePtr& node, size_t height, Suballoc& swappable_block);

    template <size_t PageAlign>
    inline void analyze_edges_step(NodePtr node, std::array<size_t, 3>& acc);
    template <size_t PageAlign>
    inline void analyze_locality_in_traversal_step(NodePtr node, uintptr_t* cached_pages, size_t cache_size, size_t& lr_cache_idx, std::array<size_t, 3>& cnt);
};

}  // namespace Blocked
}  // namespace FarMemoryContainer

#include <far_memory_container/blocked/b_tree_node.ipp>

#include <far_memory_container/blocked/b_tree_iterator.ipp>

#include <far_memory_container/blocked/b_tree.ipp>
