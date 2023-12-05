#pragma once

#include <far_memory_container/page_aware/b_tree.hpp>

#include <cstddef>
#include <functional>
#include <utility>


namespace FarMemoryContainer::PageAware
{

template <class Node>
void BTreeIterBase<Node>::increment()
{
    const NodePtr& child = node->children[elem_idx + 1];
    if (child == nullptr) {  // the node is a leaf

        if (elem_idx + 1 != node->n_elems) {  // the next element is in the same node
            elem_idx++;                       // so point to it

        } else {  // having been pointing to the last element of a leaf
            NodePtr cursor = node;
            while (cursor->parent->children[cursor->parent->n_elems] == cursor) {  // loop until cursor is not the last child of its parent
                cursor = cursor->parent;
            }
            node = cursor->parent;
            elem_idx = std::ranges::find(node->children, cursor) - node->children.begin();
        }

    } else {  // the node is a inner node
        NodePtr cursor = child;
        while (cursor->children[0] != nullptr) {  // loop until leaf
            cursor = cursor->children[0];
        }
        node = cursor;
        elem_idx = 0;
    }
}

template <class Node>
void BTreeIterBase<Node>::decrement()
{
    const NodePtr& child = node->children[elem_idx];
    if (child == nullptr) {  // the node is a leaf

        if (elem_idx != 0) {  // the previous element is in the same node
            elem_idx--;       // so point to it

        } else {  // having been pointing to the first element of a leaf
            NodePtr cursor = node;
            while (cursor->parent->children[0] == cursor) {  // loop until cursor is not the first child of its parent
                cursor = cursor->parent;
            }
            node = cursor->parent;
            elem_idx = std::ranges::find(node->children, cursor) - node->children.begin() + 1;
        }

    } else {  // the node is a inner node
        NodePtr cursor = child;
        while (cursor->children[cursor->n_elems] != nullptr) {  // loop until leaf
            cursor = cursor->children[cursor->n_elems];
        }
        node = cursor;
        elem_idx = node->n_elems - 1;
    }
}


template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::iterator::operator++() -> iterator&
{
    Base::increment();
    return *this;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::iterator::operator++(int) -> iterator
{
    auto tmp = *this;
    ++(*this);
    return tmp;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::iterator::operator--() -> iterator&
{
    Base::decrement();
    return *this;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::iterator::operator--(int) -> iterator
{
    auto tmp = *this;
    --(*this);
    return tmp;
}

template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::const_iterator::operator++() -> const_iterator&
{
    Base::increment();
    return *this;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::const_iterator::operator++(int) -> const_iterator
{
    auto tmp = *this;
    ++(*this);
    return tmp;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::const_iterator::operator--() -> const_iterator&
{
    Base::decrement();
    return *this;
}
template <class Key, class T, size_t MaxNElems, class Compare, class Allocator>
auto BTreeMap<Key, T, MaxNElems, Compare, Allocator>::const_iterator::operator--(int) -> const_iterator
{
    auto tmp = *this;
    --(*this);
    return tmp;
}

}  // namespace FarMemoryContainer::PageAware
