// Copyright 2017 The BerryDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BERRYDB_UTIL_LINKED_LIST_H_
#define BERRYDB_UTIL_LINKED_LIST_H_

#include <utility>

#include "berrydb/platform.h"
#include "berrydb/types.h"
#include "./checks.h"

namespace berrydb {

template <typename Embedder>
class LinkedListNode;
template <typename Embedder>
class LinkedListBridge;

/**
 * A doubly linked list with embeddable nodes.
 *
 * This custom solution reduces dynamic memory allocations by embedding the
 * list's noeds into the host data structure. If memory isn't an issue,
 * std::list<Host*> should be preferred.
 *
 * The code for an embedder hosting a single list should look as follows:
 *
 *     class Embedder {
 *      private:
 *       LinkedList<Embedder>::Node linked_list_node_;
 *       friend class LinkedListBridge<Embedder>;
 *     }
 *
 * If an embedder wishes to host multiple lists, only one of them can use the
 * default bridge, which uses the linked_list_node_ member variable. Other lists
 * must declare their own bridges.
 *
 *     class CustomEmbedder {
 *      private:
 *       friend class CustomLinkedListBridge;
 *       LinkedList<CustomEmbedder>::Node custom_list_node_;
 *     };
 *     class CustomLinkedListBridge {
 *      public:
 *       using Embedder = CustomEmbedder;
 *       using Node = LinkedListNode<CustomEmbedder>;
 *
 *       static inline Node* NodeForHost(Embedder* host) noexcept {
 *         return &host->custom_list_node_;
 *       }
 *       static inline Embedder* HostForNode(Node* node) noexcept {
 *         static_assert(
 *             std::is_standard_layout<Embedder>::value,
 *             "Linked list embedders must be standard layout types");
 *
 *         Embedder* const host = reinterpret_cast<Embedder*>(
 *             reinterpret_cast<char*>(node) -
 *             offsetof(Embedder, custom_list_node_));
 *         BERRYDB_ASSUME_EQ(node, &host->custom_list_node_);
 *         return host;
 *       }
 *     };
 *
 * The embedder class must be a standard layout type (can be checked using
 * std::is_standard_layout). Linked list operations will be a bit faster when
 * the LinkedList::Node is the first member of the embedder class.
 *
 * This class implements the std::list subset used in the project. The subset
 * may grow over time. However, the following will never be implemented, as a
 * consequence of having embedded nodes.
 * 1) copy constructor and assignment - impossible, because each embedded node
 *        can be in at most one list at a time
 * 2) emplace_* - doesn't really make sense, given that the embedders contain
 *        the nodes, not the other way around
 */
template <typename Embedder, typename Bridge = LinkedListBridge<Embedder>>
class LinkedList {
 public:
  using value_type = Embedder*;
  using size_type = size_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  class iterator;

  using Node = LinkedListNode<Embedder>;

  // Exported mostly for testing.
  using EmbedderType = Embedder;
  using BridgeType = Bridge;

  inline constexpr LinkedList() noexcept
      : sentinel_(Node::kSentinelNodeTag), size_(0) {}
  inline LinkedList(LinkedList&& other) noexcept
      : sentinel_(std::move(other.sentinel_)), size_(other.size_) {
    other.size_ = 0;
  }
  inline ~LinkedList() noexcept = default;

  LinkedList(const LinkedList&) = delete;
  LinkedList& operator=(const LinkedList&) = delete;

  inline constexpr bool empty() const noexcept {
    return sentinel_.next() == &sentinel_;
  }
  inline constexpr size_t size() const noexcept { return size_; }

  inline constexpr iterator begin() noexcept {
    return iterator{sentinel_.next()};
  }
  inline constexpr iterator end() noexcept {
    return iterator{&sentinel_};
  }

  inline constexpr value_type front() noexcept { return *begin(); }
  inline constexpr value_type back() noexcept { return *(--end()); }

  inline void insert(iterator pos, value_type value) noexcept {
    BERRYDB_ASSUME(value != nullptr);

    Node* const node = Bridge::NodeForHost(value);
    BERRYDB_ASSUME(node != nullptr);
    BERRYDB_ASSUME_EQ(value, Bridge::HostForNode(node));

    node->InsertBefore(pos.node_);
    ++size_;
  }

  inline void erase(iterator pos) noexcept {
    Node* const node = pos.node_;
    BERRYDB_ASSUME(node != nullptr);

#if BERRYDB_CHECK_IS_ON()
    BERRYDB_CHECK(!node->is_sentinel());
    BERRYDB_CHECK_EQ(&sentinel_, node->list_sentinel_);
#endif  // BERRYDB_CHECK_IS_ON()

    node->Remove();
    BERRYDB_ASSUME_GT(size_, 0U);
    --size_;
  }

  /** Like std::erase(value_type), but the value must be in the list.
   *
   * This method has undefiend results if this list does not contain the given
   * value. */
  inline void erase(value_type value) noexcept {
    Node* const node = Bridge::NodeForHost(value);

#if BERRYDB_CHECK_IS_ON()
    BERRYDB_CHECK(!node->is_sentinel());
    BERRYDB_CHECK_EQ(&sentinel_, node->list_sentinel_);
#endif  // BERRYDB_CHECK_IS_ON()

    node->Remove();
    BERRYDB_ASSUME_GT(size_, 0U);
    --size_;
  }

  inline void push_front(value_type value) noexcept { insert(begin(), value); }
  inline void push_back(value_type value) noexcept { insert(end(), value); }
  inline void pop_front() noexcept { erase(begin()); }
  inline void pop_back() noexcept { erase(--end()); }

  /** BidirectionalIterator wrapper around a Node pointer. */
  class iterator {
   public:
    inline constexpr iterator(const iterator&) noexcept = default;
    inline constexpr iterator& operator=(const iterator&) noexcept = default;
    inline ~iterator() noexcept = default;

    inline constexpr bool operator==(const iterator& other) const noexcept {
      return node_ == other.node_;
    }
    inline constexpr bool operator!=(const iterator& other) const noexcept {
      return node_ != other.node_;
    }

    inline constexpr iterator& operator++() noexcept {
#if BERRYDB_CHECK_IS_ON()
      BERRYDB_CHECK(!node_->is_sentinel());  // Already at cend().
#endif  // BERRYDB_CHECK_IS_ON()
      node_ = node_->next();
      return *this;
    }
    inline constexpr iterator operator++(int)noexcept {
#if BERRYDB_CHECK_IS_ON()
      BERRYDB_CHECK(!node_->is_sentinel());  // Already at cend().
#endif  // BERRYDB_CHECK_IS_ON()
      Node* const old_node = node_;
      node_ = node_->next();
      return iterator(old_node);
    }
    inline constexpr iterator& operator--() noexcept {
#if BERRYDB_CHECK_IS_ON()
      BERRYDB_CHECK(!node_->prev()->is_sentinel());  // Already at cbegin().
#endif  // BERRYDB_CHECK_IS_ON()
      node_ = node_->prev();
      return *this;
    }
    inline constexpr iterator operator--(int) noexcept {
#if BERRYDB_CHECK_IS_ON()
      BERRYDB_CHECK(!node_->prev()->is_sentinel());  // Already at cbegin().
#endif  // BERRYDB_CHECK_IS_ON()
      Node* const old_node = node_;
      node_ = node_->prev();
      return iterator(old_node);
    }

    inline value_type operator*() const noexcept {
      return Bridge::HostForNode(node_);
    }

   private:
    /** Constructor used by the list. */
    constexpr iterator(Node* node) : node_(node) {
      BERRYDB_ASSUME(node != nullptr);
    }

    friend class LinkedList;

    Node* node_;
  };

 private:
  Node sentinel_;
  // TODO(pwnall): Consider making size_ and size() CHECK-only. If we can get
  //               away with it, this would shave some code and Transaction
  //               memory, and would improve cache locality.
  size_t size_;
};

/** A node in a linked list. */
template <typename Embedder>
class LinkedListNode {
 public:
  struct SentinelNodeTag {};
  static constexpr const SentinelNodeTag kSentinelNodeTag{};

  /** Constructor for non-sentinel nodes. */
  constexpr inline LinkedListNode() noexcept
#if BERRYDB_CHECK_IS_ON()
      : next_(nullptr),
        prev_(nullptr),
        list_sentinel_(nullptr)
#endif  // BERRYDB_CHECK_IS_ON()
  {
  }

#if BERRYDB_CHECK_IS_ON()
  /** Only intended for use in CHECKs. */
  inline constexpr LinkedListNode* list_sentinel() const noexcept {
    return list_sentinel_;
  }
  /** Only intended for use in CHECKs. */
  inline constexpr bool is_sentinel() const noexcept {
    return this == list_sentinel_;
  }
#endif  // BERRYDB_CHECK_IS_ON()

 private:
  /** Constructor for sentinel nodes. */
  explicit inline constexpr LinkedListNode(SentinelNodeTag) noexcept
      : next_(this),
        prev_(this)
#if BERRYDB_CHECK_IS_ON()
        ,
        list_sentinel_(this)
#endif  // BERRYDB_CHECK_IS_ON()
  {
  }

  // Nodes cannot be copied, and can only be move-constructed.
  LinkedListNode(const LinkedListNode&) = delete;
  LinkedListNode& operator=(const LinkedListNode&) = delete;
  LinkedListNode& operator=(LinkedListNode&&) = delete;

  /** Used by the list move-constructor for sentinel nodes. */
  inline LinkedListNode(LinkedListNode&& other_sentinel) noexcept
#if BERRYDB_CHECK_IS_ON()
      : list_sentinel_(this)
#endif  // BERRYDB_CHECK_IS_ON()
  {
#if BERRYDB_CHECK_IS_ON()
    BERRYDB_CHECK(other_sentinel.is_sentinel());
#endif  // BERRYDB_CHECK_IS_ON()

    if (other_sentinel.next() == &other_sentinel) {
      // Empty list.
      next_ = prev_ = this;
      return;
    }

    this->next_ = other_sentinel.next_;
    other_sentinel.next_ = &other_sentinel;
    this->prev_ = other_sentinel.prev_;
    other_sentinel.prev_ = &other_sentinel;
    this->next_->prev_ = this;
    this->prev_->next_ = this;

// This is actually O(list size) when CHECKs are enabled. This is only mildly
// unfortunate, because list-moves are generally used when a list's items are
// destroyed.
#if BERRYDB_CHECK_IS_ON()
    for (LinkedListNode* node = next_; node != this; node = node->next_) {
      BERRYDB_CHECK(!node->is_sentinel());
      BERRYDB_CHECK_EQ(&other_sentinel, node->list_sentinel_);
      BERRYDB_CHECK(node->next_ != nullptr);
      BERRYDB_CHECK(node->prev_ != nullptr);

      node->list_sentinel_ = this;
    }
#endif  // BERRYDB_CHECK_IS_ON()
  }

  template <typename AnyEmbedder, typename Bridge>
  friend class LinkedList;
  template <typename AnyEmbedder, typename Bridge>
  friend class iterator;

  /** The node's successor in the linked list.
   *
   * This must not be called while the node is not in a linked list. */
  inline constexpr LinkedListNode* next() const noexcept {
#if BERRYDB_CHECK_IS_ON()
    BERRYDB_CHECK(list_sentinel_ != nullptr);
#endif  // BERRYDB_CHECK_IS_ON()

    // Redundant with check above, might trigger if memory gets corrupted.
    BERRYDB_ASSUME(next_ != nullptr);
    BERRYDB_ASSUME_EQ(next_->prev_, this);

    return next_;
  }

  /** The node's predecessor in the linked list.
   *
   * This must not be called while the node is not in a linked list. */
  inline constexpr LinkedListNode* prev() const noexcept {
#if BERRYDB_CHECK_IS_ON()
    BERRYDB_CHECK(list_sentinel_ != nullptr);
#endif  // BERRYDB_CHECK_IS_ON()

    // Redundant with check above, might trigger if memory gets corrupted.
    BERRYDB_ASSUME(prev_ != nullptr);
    BERRYDB_ASSUME_EQ(prev_->next_, this);

    return prev_;
  }

  /** Inserts this node in a list, before a given node.
   *
   * The node must not already be in a list. */
  inline void InsertBefore(LinkedListNode* next) noexcept {
    DCHECK(next != nullptr);

#if BERRYDB_CHECK_IS_ON()
    BERRYDB_CHECK(!is_sentinel());

    // This node cannot already be in a list.
    BERRYDB_CHECK(list_sentinel_ == nullptr);

    // Redundant with check above, might trigger if memory gets corrupted. These
    // are not outside the BERRYDB_CHECK_IS_ON() block because they're only
    // guaranteed to be valid when CHECKs are enabled.
    BERRYDB_CHECK(next_ == nullptr);
    BERRYDB_CHECK(prev_ == nullptr);

    // The given node must be in a list.
    BERRYDB_CHECK(next->list_sentinel_ != nullptr);

    list_sentinel_ = next->list_sentinel_;
#endif  // BERRYDB_CHECK_IS_ON()

    // Redundant with check above, might trigger if memory gets corrupted.
    BERRYDB_ASSUME(next->next_ != nullptr);
    BERRYDB_ASSUME(next->prev_ != nullptr);

    this->prev_ = next->prev_;
    this->prev_->next_ = this;
    this->next_ = next;
    this->next_->prev_ = this;
  }

  /** Removes this node from the list that it is in.
   *
   * The node must be in a list. */
  inline void Remove() noexcept {
#if BERRYDB_CHECK_IS_ON()
    BERRYDB_CHECK(!is_sentinel());
    BERRYDB_CHECK(list_sentinel_ != nullptr);  // This node must be in a list.

    list_sentinel_ = nullptr;
#endif  // BERRYDB_CHECK_IS_ON()

    // Redundant with check above, might trigger if memory gets corrupted.
    BERRYDB_ASSUME(next_ != nullptr);
    BERRYDB_ASSUME(prev_ != nullptr);

    this->next_->prev_ = this->prev_;
    this->prev_->next_ = this->next_;
#if BERRYDB_CHECK_IS_ON()
    this->next_ = nullptr;
    this->prev_ = nullptr;
#endif  // BERRYDB_CHECK_IS_ON()
  }

  // When CHECKs are enabled, these fields are guaranteed to be null when the
  // node is not in a list. When CHECKs are disabled, the fields are only
  // written when the node is added to a list, so their values are undefined
  // while the node is not in a list.
  LinkedListNode* next_;
  LinkedListNode* prev_;

#if BERRYDB_CHECK_IS_ON()
  /** The sentinel node for list that this node currently belongs to.
   *
   * It is tempting to track the linked list that this node belongs to, rather
   * than its sentinel. However, that requires a dependency on LinkedList, which
   * includes the embedder bridge type. The current setup avoids having
   * LinkedListNode depend on the bridge type. */
  LinkedListNode* list_sentinel_;
#endif  // BERRYDB_CHECK_IS_ON()
};

/** Bridge that extracts the linked_list_node_ field from the embedder. */
template <typename Embedder>
class LinkedListBridge {
 public:
  /** Extracts the LinkedListNode from an embedder object. */
  static inline LinkedListNode<Embedder>* NodeForHost(
      Embedder* host) noexcept {
    return &host->linked_list_node_;
  }

  /** Converts a LinkedListNode pointer back to an embedder pointer. */
  static inline Embedder* HostForNode(
      LinkedListNode<Embedder>* node) noexcept {
    static_assert(std::is_standard_layout<Embedder>::value,
                  "Linked list embedders must be standard layout types");
#if BERRYDB_CHECK_IS_ON()
    BERRYDB_CHECK(!node->is_sentinel());
#endif  // BERRYDB_CHECK_IS_ON()

    Embedder* const host = reinterpret_cast<Embedder*>(
        reinterpret_cast<char*>(node) - offsetof(Embedder, linked_list_node_));
    BERRYDB_ASSUME_EQ(node, &host->linked_list_node_);
    return host;
  }
};

}  // namespace berrydb

#endif  // BERRYDB_UTIL_LINKED_LIST_H_
