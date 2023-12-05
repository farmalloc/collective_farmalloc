#pragma once

#include <farmalloc/collective_allocator_traits.hpp>
#include <far_memory_container/aligned_buffer.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <random>
#include <type_traits>


namespace FarMemoryContainer::Blocked
{

template <class PtrToVal>
struct SkiplistNode {
    using value_type = std::remove_reference_t<decltype(*std::declval<PtrToVal>())>;
    using NodePtr = typename std::pointer_traits<PtrToVal>::template rebind<SkiplistNode>;
    using level_type = uint8_t;

    struct Link {
        NodePtr prev = nullptr, next = nullptr;
    };
    using LinkPtr = typename std::pointer_traits<PtrToVal>::template rebind<Link>;

    LinkPtr links;
    AlignedBuffer<value_type> value;

private:
    level_type level_;

public:
    constexpr level_type level() const noexcept { return level_; }

    constexpr SkiplistNode(const NodePtr& ptr_to_this, LinkPtr&& links) noexcept;

    template <class Alloc, class... Args>
    inline constexpr SkiplistNode(Alloc& allocator, const LinkPtr& links, level_type level, Args&&... args);

    template <class Alloc>
    inline constexpr SkiplistNode(Alloc& allocator, SkiplistNode&& other);

    inline constexpr ~SkiplistNode() noexcept = default;
    SkiplistNode(const SkiplistNode&) = delete;
    SkiplistNode(SkiplistNode&&) = delete;
};

template <class Node_>
struct SkiplistIteratorBase {
    using Node = Node_;

    using value_type = typename Node::value_type;
    using NodePtr = typename Node::NodePtr;

private:
    NodePtr node = nullptr;

public:
    inline constexpr ~SkiplistIteratorBase() noexcept = default;

    friend constexpr bool operator==(const SkiplistIteratorBase& lhs, const SkiplistIteratorBase& rhs) noexcept { return lhs.node == rhs.node; }

protected:
    constexpr SkiplistIteratorBase() noexcept {}
    inline constexpr SkiplistIteratorBase(const SkiplistIteratorBase&) noexcept = default;
    inline constexpr SkiplistIteratorBase(SkiplistIteratorBase&&) noexcept = default;
    inline constexpr SkiplistIteratorBase& operator=(const SkiplistIteratorBase&) noexcept = default;
    inline constexpr SkiplistIteratorBase& operator=(SkiplistIteratorBase&&) noexcept = default;

    constexpr explicit SkiplistIteratorBase(const NodePtr& node) noexcept : node(node) {}

    constexpr value_type* get() const noexcept { return node->value.get(); }

    inline constexpr SkiplistIteratorBase& operator++() noexcept;
    inline constexpr SkiplistIteratorBase& operator--() noexcept;
};

struct SkiplistLevelDistribution {
    using result_type = uint8_t;
    inline static constexpr size_t MaxLevel = 64;

    template <class URBG>
    inline constexpr result_type operator()(URBG&& g);

private:
    std::uniform_int_distribution<uint64_t> impl_dist;
};

template <class Key, class T, class Compare = std::less<Key>, class Allocator = std::allocator<std::pair<const Key, T>>, std::uniform_random_bit_generator URBG = std::minstd_rand0>
struct SkiplistMap {
    using key_type = Key;
    using mapped_type = T;
    using key_compare = Compare;
    using allocator_type = Allocator;
    using urbg_type = URBG;

    using value_type = std::pair<const Key, T>;
    using reference = value_type&;
    using const_reference = const value_type&;

    static_assert(std::is_same_v<typename FarMalloc::collective_allocator_traits<allocator_type>::value_type, value_type>);

    struct value_compare {
    private:
        key_compare comp;
        constexpr explicit value_compare(const key_compare& compare) : comp(compare) {}

        friend struct SkiplistMap;

    public:
        constexpr bool operator()(const value_type& lhs, const value_type& rhs) const
        {
            return comp(lhs.first, rhs.first);
        }
    };

private:
    using Node = SkiplistNode<typename FarMalloc::collective_allocator_traits<allocator_type>::pointer>;
    using Link = typename Node::Link;
    using LinkPtr = typename Node::LinkPtr;
    using NodeAllocTraits = typename FarMalloc::collective_allocator_traits<allocator_type>::template rebind_traits<Node>;
    using NodeSuballocTraits = typename NodeAllocTraits::suballocator_traits;
    using NodeSuballoc = typename NodeSuballocTraits::allocator_type;
    using LinkAllocTraits = typename NodeAllocTraits::template rebind_traits<Link>;

public:
    using difference_type = std::common_type_t<typename NodeAllocTraits::difference_type, typename LinkAllocTraits::difference_type, ssize_t>;
    using size_type = std::make_unsigned_t<difference_type>;
    using level_type = typename Node::level_type;

    struct iterator : SkiplistIteratorBase<Node> {
        using itertor_concept = std::bidirectional_iterator_tag;

    private:
        using Base = SkiplistIteratorBase<Node>;
        using NodePtr = typename Base::NodePtr;

    public:
        using difference_type = SkiplistMap::difference_type;
        using value_type = typename Base::value_type;

        inline constexpr iterator() noexcept = default;
        inline constexpr iterator(const iterator&) noexcept = default;
        inline constexpr iterator(iterator&&) noexcept = default;
        inline constexpr iterator& operator=(const iterator&) noexcept = default;
        inline constexpr iterator& operator=(iterator&&) noexcept = default;

        constexpr value_type& operator*() const noexcept { return *Base::get(); }
        constexpr value_type* operator->() const noexcept { return Base::get(); }

        inline constexpr iterator& operator++();
        inline constexpr iterator operator++(int);
        inline constexpr iterator& operator--();
        inline constexpr iterator operator--(int);

    private:
        using Base::Base;

        friend struct SkiplistMap;
    };
    static_assert(std::bidirectional_iterator<iterator>);
    using reverse_iterator = std::reverse_iterator<iterator>;

    struct const_iterator : SkiplistIteratorBase<Node> {
        using itertor_concept = std::bidirectional_iterator_tag;

    private:
        using Base = SkiplistIteratorBase<Node>;
        using NodePtr = typename Base::NodePtr;

    public:
        using difference_type = SkiplistMap::difference_type;
        using value_type = typename Base::value_type;

        inline constexpr const_iterator() noexcept = default;
        inline constexpr const_iterator(const const_iterator&) noexcept = default;
        inline constexpr const_iterator(const_iterator&&) noexcept = default;
        inline constexpr const_iterator& operator=(const const_iterator&) noexcept = default;
        inline constexpr const_iterator& operator=(const_iterator&&) noexcept = default;

        constexpr const_iterator(const iterator& x) noexcept : Base(x) {}
        constexpr const_iterator(iterator&& x) noexcept : Base(std::move(x)) {}

        constexpr const value_type& operator*() const noexcept { return *Base::get(); }
        constexpr const value_type* operator->() const noexcept { return Base::get(); }

        inline constexpr const_iterator& operator++();
        inline constexpr const_iterator operator++(int);
        inline constexpr const_iterator& operator--();
        inline constexpr const_iterator operator--(int);

    private:
        using Base::Base;

        friend struct SkiplistMap;
    };
    static_assert(std::bidirectional_iterator<const_iterator>);
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
    using NodePtr = typename Node::NodePtr;

    size_type size_cnt = 0;
    [[no_unique_address]] urbg_type urbg = urbg_type();
    [[no_unique_address]] SkiplistLevelDistribution dist;
    [[no_unique_address]] key_compare comp = Compare();
    [[no_unique_address]] typename NodeAllocTraits::allocator_type node_alloc = allocator_type();
    [[no_unique_address]] typename LinkAllocTraits::allocator_type link_alloc = node_alloc;
    NodePtr header = [this] {
        using namespace FarMalloc::request;
        auto local = NodeAllocTraits::get_suballocator(node_alloc, purely_local);
        auto allocated = NodeSuballocTraits::batch_allocate(local, single<Node>(), dynamic<Link>(SkiplistLevelDistribution::MaxLevel + 1));
        if (!allocated) {
            throw std::bad_alloc{};
        }
        auto& [node, links] = *allocated;
        new (&(*node)) Node{node, std::move(links)};
        return node;
    }();
    NodePtr last_local_node = header;

public:
    constexpr SkiplistMap() {}
    constexpr SkiplistMap(const Allocator& alloc) : node_alloc(alloc) {}
    inline constexpr ~SkiplistMap() noexcept;

    constexpr iterator begin() noexcept { return iterator(header->links[0].next); }
    constexpr const_iterator begin() const noexcept { return const_iterator(header->links[0].next); }
    constexpr const_iterator cbegin() const noexcept { return begin(); }

    constexpr iterator end() noexcept { return iterator(header); }
    constexpr const_iterator end() const noexcept { return iterator(header); }
    constexpr const_iterator cend() const noexcept { return end(); }

    constexpr reverse_iterator rbegin() noexcept { return std::make_reverse_iterator(end()); }
    constexpr const_reverse_iterator rbegin() const noexcept { return std::make_reverse_iterator(end()); }
    constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }

    constexpr reverse_iterator rend() noexcept { return std::make_reverse_iterator(begin()); }
    constexpr const_reverse_iterator rend() const noexcept { return std::make_reverse_iterator(begin()); }
    constexpr const_reverse_iterator crend() const noexcept { return rend(); }

    constexpr size_type size() const noexcept { return size_cnt; }
    inline constexpr size_type max_size() { return std::numeric_limits<size_type>::max(); }
    constexpr bool empty() const noexcept { return size_cnt == 0; }

    constexpr allocator_type get_allocator() const noexcept { return allocator_type(node_alloc); }

    inline constexpr iterator find(const key_type& x) { return find_impl(x); }
    inline constexpr const_iterator find(const key_type& x) const { return find_impl(x); }

    inline constexpr std::pair<iterator, bool> insert(value_type&& x);
    inline constexpr size_type erase(const key_type& x);

    inline constexpr void clear() noexcept;

    inline void batch_block();

    //! @return [purely_local_edges, same_page_edges, diff_pages_edges]
    template <size_t PageAlign>
    inline std::array<size_t, 3> analyze_edges();
    //! @return [purely_local, cache_hit, cache_miss]
    template <size_t PageAlign>
    inline std::array<size_t, 3> analyze_locality_in_traversal(size_t cache_size_in_pages);

private:
    inline constexpr void clear_impl() noexcept;

    template <class K>
    inline constexpr NodePtr lower_bound_impl(const K& x) const;
    template <class K>
    inline constexpr iterator find_impl(const K& x) const;

    inline constexpr NodePtr prev_in_priority(NodePtr candidate, level_type level);
    inline constexpr std::optional<NodePtr> next_in_priority(NodePtr candidate, level_type level);
    inline NodePtr relocate_last_local_to_far();
    inline void relocate(NodePtr& node, NodeSuballoc suballoc);
};


template <class PtrToVal>
constexpr SkiplistNode<PtrToVal>::SkiplistNode(const NodePtr& ptr_to_this, LinkPtr&& links) noexcept
    : links(std::move(links)), level_(SkiplistLevelDistribution::MaxLevel)
{
    std::uninitialized_fill_n(links, level_ + 1, Link(ptr_to_this, ptr_to_this));
}
template <class PtrToVal>
template <class Alloc, class... Args>
constexpr SkiplistNode<PtrToVal>::SkiplistNode(Alloc& allocator, const LinkPtr& links, level_type level, Args&&... args)
    : links(links), level_(level)
{
    value.construct(allocator, std::forward<Args>(args)...);
}
template <class PtrToVal>
template <class Alloc>
constexpr SkiplistNode<PtrToVal>::SkiplistNode(Alloc& allocator, SkiplistNode&& other)
    : links(std::move(other.links)), level_(other.level_)
{
    value.move_from(allocator, other.value);
}


template <class Node_>
constexpr auto SkiplistIteratorBase<Node_>::operator++() noexcept -> SkiplistIteratorBase&
{
    node = node->links[0].next;
    return *this;
}
template <class Node_>
constexpr auto SkiplistIteratorBase<Node_>::operator--() noexcept -> SkiplistIteratorBase&
{
    node = node->links[0].prev;
    return *this;
}

template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::iterator::operator++() -> iterator&
{
    Base::operator++();
    return *this;
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::iterator::operator++(int) -> iterator
{
    iterator tmp = *this;
    ++(*this);
    return tmp;
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::iterator::operator--() -> iterator&
{
    Base::operator--();
    return *this;
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::iterator::operator--(int) -> iterator
{
    iterator tmp = *this;
    --(*this);
    return tmp;
}

template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::const_iterator::operator++() -> const_iterator&
{
    Base::operator++();
    return *this;
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::const_iterator::operator++(int) -> const_iterator
{
    const_iterator tmp = *this;
    ++(*this);
    return tmp;
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::const_iterator::operator--() -> const_iterator&
{
    Base::operator--();
    return *this;
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::const_iterator::operator--(int) -> const_iterator
{
    const_iterator tmp = *this;
    --(*this);
    return tmp;
}


template <class URBG>
constexpr auto SkiplistLevelDistribution::operator()(URBG&& g) -> result_type
{
    const auto rand_value = impl_dist(g);
    const auto increment = std::countl_zero(rand_value);
    return static_cast<result_type>(increment);
}


template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr SkiplistMap<Key, T, Compare, Allocator, URBG>::~SkiplistMap() noexcept
{
    clear_impl();

    std::destroy_n(header->links, SkiplistLevelDistribution::MaxLevel + 1);
    LinkAllocTraits::deallocate(link_alloc, std::move(header->links), SkiplistLevelDistribution::MaxLevel + 1);

    header->value.destroy(node_alloc);
    header->~Node();
    NodeAllocTraits::deallocate(node_alloc, std::move(header), 1);
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr void SkiplistMap<Key, T, Compare, Allocator, URBG>::clear() noexcept
{
    clear_impl();
    size_cnt = 0;
    std::fill_n(header->links, SkiplistLevelDistribution::MaxLevel + 1, Link(header, header));
    last_local_node = header;
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr void SkiplistMap<Key, T, Compare, Allocator, URBG>::clear_impl() noexcept
{
    NodePtr node = header->links[0].prev;
    while (node != header) {
        NodePtr deleted = std::move(node);
        node = deleted->links[0].prev;

        std::destroy_n(deleted->links, deleted->level() + 1);
        LinkAllocTraits::deallocate(link_alloc, std::move(deleted->links), deleted->level() + 1);

        deleted->value.destroy(node_alloc);
        deleted->~Node();
        NodeAllocTraits::deallocate(node_alloc, std::move(deleted), 1);
    }
}

template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
template <class K>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::lower_bound_impl(const K& key) const -> NodePtr
{
    NodePtr lower_bound = header, prev_in_upper_level = header;

    level_type level = lower_bound->level();
    do {
        for (;;) {
            NodePtr prev = lower_bound->links[level].prev;
            if (prev == prev_in_upper_level) {
                break;
            }

            if (comp(prev->value.get()->first, key)) {
                prev_in_upper_level = std::move(prev);
                break;
            }

            lower_bound = std::move(prev);
        }
    } while (level-- != 0);

    return lower_bound;
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
template <class K>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::find_impl(const K& key) const -> iterator
{
    auto lower_bound = lower_bound_impl(key);
    if (lower_bound != header && !comp(key, lower_bound->value.get()->first)) {
        return iterator(std::move(lower_bound));
    } else {
        return iterator(header);
    }
}

template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::insert(value_type&& x) -> std::pair<iterator, bool>
{
    using namespace FarMalloc::request;

    const level_type new_lv = dist(urbg);

    NodePtr cursor = header, prev_in_upper_level = header;

    level_type cursor_level = cursor->level();
    do {
        for (;;) {
            NodePtr prev = cursor->links[cursor_level].prev;
            if (prev == prev_in_upper_level) {
                break;
            }
            if (comp(prev->value.get()->first, x.first)) {
                prev_in_upper_level = std::move(prev);
                break;
            }
            cursor = std::move(prev);
        }
    } while (cursor_level-- != new_lv);
    NodePtr lbound_at_new_lv = cursor;

    if (new_lv != 0) {
        do {
            for (;;) {
                NodePtr prev = cursor->links[cursor_level].prev;
                if (prev == prev_in_upper_level) {
                    break;
                }
                if (comp(prev->value.get()->first, x.first)) {
                    prev_in_upper_level = std::move(prev);
                    break;
                }
                cursor = std::move(prev);
            }
        } while (cursor_level-- != 0);
    }

    if (cursor != header && !comp(x.first, cursor->value.get()->first)) {
        return {iterator(cursor), false};
    }

    // the previous node in priority of purely-local allocation
    NodePtr one_more_prioritized = prev_in_priority(lbound_at_new_lv, new_lv);
    std::optional<std::tuple<NodePtr, LinkPtr>> allocated;

    auto local = NodeAllocTraits::get_suballocator(node_alloc, purely_local);
    if (NodeAllocTraits::if_suballocator_contains(node_alloc, local, one_more_prioritized)) {
        for (;;) {
            allocated = NodeSuballocTraits::batch_allocate(local, single<Node>(), dynamic<Link>(new_lv + 1));
            if (allocated) {
                auto& [node, _] = *allocated;
                if (one_more_prioritized == last_local_node) {
                    last_local_node = node;
                }
                break;
            } else if (one_more_prioritized == last_local_node) {
                break;
            } else {
                const auto relocate_cursor = (last_local_node == cursor);
                auto relocated = relocate_last_local_to_far();
                if (relocate_cursor) {
                    cursor = relocated;
                }
            }
        }
    }
    if (!allocated) {
        if (!NodeAllocTraits::if_suballocator_contains(node_alloc, local, cursor)) {
            auto suballoc = NodeAllocTraits::get_suballocator(node_alloc, cursor);
            allocated = NodeSuballocTraits::batch_allocate(suballoc, single<Node>(), dynamic<Link>(new_lv + 1));
        }
    }
    if (!allocated) {
        auto suballoc = NodeAllocTraits::get_suballocator(node_alloc, swappable_plain);
        allocated = NodeSuballocTraits::batch_allocate(suballoc, single<Node>(), dynamic<Link>(new_lv + 1));
    }
    if (!allocated) {
        throw std::bad_alloc{};
    }

    NodePtr prev_in_the_level = cursor->links[0].prev;
    const auto& [node, links] = *allocated;
    new (&(*node)) Node{node_alloc, links, new_lv, std::move(x)};
    for (level_type level = 0; level <= new_lv; level++) {
        while (cursor->level() < level) {
            cursor = cursor->links[level - 1].next;
        }
        while (prev_in_the_level->level() < level) {
            prev_in_the_level = prev_in_the_level->links[level - 1].prev;
        }
        new (&links[level]) Link{prev_in_the_level, cursor};
        prev_in_the_level->links[level].next = cursor->links[level].prev = node;
    }

    size_cnt++;
    return std::make_pair(iterator(node), true);
}

template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::erase(const key_type& x) -> size_type
{
    auto deleted = lower_bound_impl(x);
    if (deleted != header && !comp(x, deleted->value.get()->first)) {
        if (deleted == last_local_node) {
            last_local_node = prev_in_priority(deleted->links[deleted->level()].next, deleted->level());
        }

        for (level_type level = 0; level <= deleted->level(); level++) {
            deleted->links[level].prev->links[level].next = deleted->links[level].next;
            deleted->links[level].next->links[level].prev = deleted->links[level].prev;
        }

        std::destroy_n(deleted->links, deleted->level() + 1);
        LinkAllocTraits::deallocate(link_alloc, std::move(deleted->links), deleted->level() + 1);

        deleted->value.destroy(node_alloc);
        deleted->~Node();
        NodeAllocTraits::deallocate(node_alloc, std::move(deleted), 1);

        try {
            for (;;) {
                auto one_less_prioritized = next_in_priority(last_local_node->links[last_local_node->level()].prev, last_local_node->level());
                if (!one_less_prioritized) {
                    break;
                }
                relocate(*one_less_prioritized, NodeAllocTraits::get_suballocator(node_alloc, purely_local));
                last_local_node = *one_less_prioritized;
            }
        } catch (std::bad_alloc&) {
        }

        size_cnt--;
        return 1;

    } else {
        return 0;
    }
}

template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::prev_in_priority(NodePtr candidate, level_type level) -> NodePtr
{
    for (;;) {
        for (; candidate != header; candidate = candidate->links[level].next) {
            if (candidate->level() == level) {
                return candidate;
            }
        }
        if (level++ == SkiplistLevelDistribution::MaxLevel) {
            return header;
        }
        candidate = candidate->links[level].next;
    }
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
constexpr auto SkiplistMap<Key, T, Compare, Allocator, URBG>::next_in_priority(NodePtr candidate, level_type level) -> std::optional<NodePtr>
{
    for (;;) {
        for (; candidate != header; candidate = candidate->links[level].prev) {
            if (candidate->level() == level) {
                std::optional<NodePtr> res{candidate};
                return res;
            }
        }
        if (level-- == 0) {
            std::optional<NodePtr> res{std::nullopt};
            return res;
            return std::nullopt;
        }
        candidate = candidate->links[level].prev;
    }
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
auto SkiplistMap<Key, T, Compare, Allocator, URBG>::relocate_last_local_to_far() -> NodePtr
{
    NodePtr node = std::move(last_local_node);
    last_local_node = prev_in_priority(node->links[node->level()].next, node->level());
    relocate(node, NodeAllocTraits::get_suballocator(node_alloc, swappable_plain));
    return node;
}
template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
void SkiplistMap<Key, T, Compare, Allocator, URBG>::relocate(NodePtr& node, NodeSuballoc suballoc)
{
    LinkPtr links = node->links;
    if (NodeAllocTraits::relocate(
            node_alloc, suballoc, [this](auto* from, auto n, auto* to) {
                if constexpr (std::same_as<decltype(from), Node*>) {
                    new (to) Node{node_alloc, std::move(*from)};
                    from->~Node();
                } else {
                    static_assert(std::same_as<decltype(from), Link*>);
                    std::uninitialized_move_n(from, n, to);
                    std::destroy_n(from, n);
                }
            },
            std::tie(node, links), request::single<Node>(), request::dynamic<Link>(node->level() + 1))) {

        for (level_type level = 0; level <= node->level(); level++) {
            links[level].prev->links[level].next = links[level].next->links[level].prev = node;
        }
        node->links = std::move(links);
    }
}

template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
void SkiplistMap<Key, T, Compare, Allocator, URBG>::batch_block()
{
    if (prev_in_priority(header->links[0].next, 0) == last_local_node) {
        return;
    }
    auto block = NodeAllocTraits::get_suballocator(node_alloc, new_per_page);
    auto local = NodeAllocTraits::get_suballocator(node_alloc, purely_local);

    NodePtr node = header->links[0].prev;
    while (node != header) {
        if (!NodeAllocTraits::if_suballocator_contains(node_alloc, local, node)) {
            if (!NodeSuballocTraits::is_occupancy_under(block, 0.7)) {
                block = NodeAllocTraits::get_suballocator(node_alloc, new_per_page);
            }
            try {
                relocate(node, block);
            } catch (std::bad_alloc&) {
                block = NodeAllocTraits::get_suballocator(node_alloc, new_per_page);
                relocate(node, block);
            }
        }
        node = node->links[0].prev;
    }
}

template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
template <size_t PageAlign>
std::array<size_t, 3> SkiplistMap<Key, T, Compare, Allocator, URBG>::analyze_edges()
{
    std::array<size_t, 3> res{};

    auto local = NodeAllocTraits::get_suballocator(node_alloc, purely_local);

    NodePtr node = header;
    do {
        const bool is_node_local = NodeAllocTraits::if_suballocator_contains(node_alloc, local, node);
        for (level_type i = 0; i <= node->level(); i++) {
            NodePtr adj = node->links[i].prev;
            const bool is_adj_local = NodeAllocTraits::if_suballocator_contains(node_alloc, local, adj);
            if (is_node_local && is_adj_local) {
                res[0] += 1;
            } else {
                bool are_on_diff_pages = (reinterpret_cast<uintptr_t>(&(*node)) / PageAlign != reinterpret_cast<uintptr_t>(&(*adj)) / PageAlign);
                res[are_on_diff_pages ? 2 : 1] += 1;
            }
        }

        node = node->links[0].prev;
    } while (node != header);

    return res;
}

template <class Key, class T, class Compare, class Allocator, std::uniform_random_bit_generator URBG>
template <size_t PageAlign>
std::array<size_t, 3> SkiplistMap<Key, T, Compare, Allocator, URBG>::analyze_locality_in_traversal(size_t cache_size)
{
    std::array<size_t, 3> res{};
    auto cached_pages = std::make_unique<uintptr_t[]>(cache_size);
    std::fill_n(&cached_pages[0], cache_size, reinterpret_cast<uintptr_t>(nullptr) / PageAlign);
    size_t lr_cache_idx = 0;

    auto local = NodeAllocTraits::get_suballocator(node_alloc, purely_local);

    for (NodePtr node = header->links[0].next; node != header; node = node->links[0].next) {
        const bool is_node_local = NodeAllocTraits::if_suballocator_contains(node_alloc, local, node);
        const auto page_id = reinterpret_cast<uintptr_t>(&(*node)) / PageAlign;
        const auto cache_end = &cached_pages[cache_size];

        if (is_node_local) {
            res[0]++;
        } else {
            const bool is_cache_hit = (std::find(&cached_pages[0], cache_end, page_id) != cache_end);
            res[is_cache_hit ? 1 : 2]++;

            if (!is_cache_hit) {
                cached_pages[lr_cache_idx] = page_id;
                lr_cache_idx++;
                if (lr_cache_idx == cache_size) {
                    lr_cache_idx = 0;
                }
            }
        }
    }

    return res;
}

}  // namespace FarMemoryContainer::Blocked
