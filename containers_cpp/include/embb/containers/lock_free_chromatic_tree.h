/*
 * Copyright (c) 2014-2015, Siemens AG. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef EMBB_CONTAINERS_LOCK_FREE_CHROMATIC_TREE_H_
#define EMBB_CONTAINERS_LOCK_FREE_CHROMATIC_TREE_H_

#include <stddef.h>
#include <functional>

#include <embb/base/c/errors.h>
#include <embb/base/mutex.h>
#include <embb/containers/internal/hazard_pointer.h>

namespace embb {
namespace containers {
namespace internal {

/**
 * Tree node
 * 
 * Stores the key-value pair, as well as the weight value (used for rebalancing)
 * and two pointers to child nodes (left and right).
 * 
 * \tparam Key   Key type
 * \tparam Value Value type
 */
template<typename Key, typename Value>
class ChromaticTreeNode {
 public:
  typedef ChromaticTreeNode<Key, Value> Node;
  typedef embb::base::Atomic<Node*>     AtomicNodePtr;

  /**
   * Creates a node with given parameters.
   * 
   * \param[IN] key    Key of the new node
   * \param[IN] value  Value of the new node
   * \param[IN] weight Weight of the new node
   * \param[IN] left   Pointer to the left child node
   * \param[IN] right  Pointer to the right child node
   */
  ChromaticTreeNode(const Key& key, const Value& value, int weight,
                    Node* left, Node* right);
  
  /**
   * Creates a node given only a key-value pair. Node will have no child nodes
   * and a default weight (1).
   * 
   * \param[IN] key    Key of the new node
   * \param[IN] value  Value of the new node
   * \param[IN] weight Weight of the new node
   */
  ChromaticTreeNode(const Key& key, const Value& value, int weight = 1);
  
  /**
   * Accessor for the stored key.
   * 
   * \return Stored key
   */
  const Key&   GetKey() const;
  
  /**
   * Accessor for the stored value.
   * 
   * \return Stored value
   */
  const Value& GetValue() const;
  
  /**
   * Accessor for the weight of the node.
   * 
   * \return Weight of the node
   */
  int GetWeight() const;
  
  /**
   * Accessor for the left child pointer.
   * 
   * \return Reference to the left child pointer
   */
  AtomicNodePtr& GetLeft();
  Node* GetLeft() const;
  
  /**
   * Accessor for the right child pointer.
   * 
   * \return Reference to the right child pointer
   */
  AtomicNodePtr& GetRight();
  Node* GetRight() const;

  /**
   * Tries to replace one of the child pointers that compares equal to
   * \c old_child with the \c new_child using an atomic compare-and-swap
   * operation. If neither left nor right child pointer is pointing to
   * \c old_child, returns \c false.
   *
   * \param old_child[IN] Pointer to an old child node to compare against
   * \param new_child[IN] Pointer to the new child node
   *
   * \return \c true if one of the child pointers is now pointing to
   *         \c new_child, \c false otherwise
   */
  bool ReplaceChild(Node* old_child, Node* new_child);

  /**
   * Marks node for deletion from the tree
   */
  void Retire();

  /**
   * Checks whether the node is marked for deletion from the tree
   *
   * \return \c true if node is retired, \c false otherwise
   */
  bool IsRetired() const;

  /**
   * Accessor for the FGL mutex
   *
   * \return Reference to this node's mutex
   */
  embb::base::Mutex& GetMutex();

 private:
  /**
   * Disable copy construction and assignment.
   */
  ChromaticTreeNode(const ChromaticTreeNode&);
  ChromaticTreeNode& operator=(const ChromaticTreeNode&);
  
  const Key     key_;    /**< Stored key */
  const Value   value_;  /**< Stored value */
  const int     weight_; /**< Weight of the node */
  AtomicNodePtr left_;   /**< Pointer to left child node */
  AtomicNodePtr right_;  /**< Pointer to right child node */
  embb::base::Atomic<bool> retired_; /**< Retired (marked for deletion) flag */

  embb::base::Mutex  mutex_; /**< Fine-grained locking tree: per node mutex */
};

} // namespace internal

namespace test {
/**
 * Forward declaration of the test class
 */
template<typename Tree>
class TreeTest;

} // namespace test

/**
 * Chromatic balanced binary search tree
 * 
 * Implements a balanced BST with support for \c Get, \c Insert and \c Delete
 * operations.
 * 
 * \tparam Key       Key type
 * \tparam Value     Value type
 * \tparam Compare   Custom comparator type for the keys. An object of the
 *                   type \c Compare must must be a functor taking two
 *                   arguments \c rhs and \c lhs of type \c Key and
 *                   returning \c true if and only if <tt>(rhs < lhs)</tt> holds
 * \tparam ValuePool Type of the value pool to be used inside object pools for
 *                   tree nodes and operation objects.
 */
template<typename Key,
         typename Value,
         typename Compare = ::std::less<Key>,
         typename ValuePool = LockFreeTreeValuePool<bool, false>
         >
class ChromaticTree {
 public:
  /**
   * Exposing the \c Key template parameter back to the user
   */
  typedef Key   KeyType;
  /**
   * Exposing the \c Value template parameter back to the user
   */
  typedef Value ValueType;

  /**
   * Creates a new tree with given capacity.
   * 
   * \memory Allocates <tt>(2 * capacity + 7)</tt> tree nodes each of size
   *         <tt>sizeof(internal::ChromaticTreeNode<Key, Value>)</tt>.
   * 
   * \notthreadsafe
   * 
   * \param[IN] capacity        Required capacity of the tree
   * \param[IN] undefined_key   Object of type \c Key to be used as a dummy key.
   *                            Defaults to <tt>Key()</tt>
   * \param[IN] undefined_value Object of type \c Value to be used as a dummy 
   *                            value. Defaults to <tt>Value()</tt>
   * \param[IN] compare         Custom comparator object for managed keys.
   *                            Defaults to <tt>Compare()</tt>
   * 
   * \note If any of \c undefined_key, \c undefined_value or \c compare is not
   *       provided, that will require the corresponding type (\c Key, \c Value
   *       or \c Compare) to support value-initialization.
   */
  explicit ChromaticTree(size_t capacity, Key undefined_key = Key(),
                Value undefined_value = Value(), Compare compare = Compare());
  
  /**
   * Destroys the tree.
   * 
   * \notthreadsafe
   */
  ~ChromaticTree();
  
  
  /**
   * Tries to find a value for the given key.
   * 
   * \param[IN]     key    Key to search for
   * \param[IN,OUT] value  Reference to the found value. Unchanged if the given
   *                       key is not stored in the tree
   * 
   * \return \c true if the given key was found in the tree, \c false otherwise
   */
  bool Get(const Key& key, Value& value);
  
  /**
   * Tries to insert a new key-value pair into the tree. If a value for the
   * given key is already stored in the tree, tries to replace the stored value
   * with the new one.
   * 
   * \param[IN] key    New key to be inserted
   * \param[IN] value  New value to be inserted
   * 
   * \return \c true if the given key-value pair was successfully inserted into
   *         the tree, \c false if the tree has reached its capacity
   */
  bool TryInsert(const Key& key, const Value& value);
  
  /**
   * Tries to insert a new key-value pair into the tree. If a value for the
   * given key is already stored in the tree, tries to replace the stored value
   * with the new one. Also returns the original value stored in the tree for
   * the given \c key, or the \c undefined_value if the key was not present in
   * the tree.
   * 
   * \param[IN]     key       New key to be inserted
   * \param[IN]     value     New value to be inserted
   * \param[IN,OUT] old_value Reference to the value previously stored in the
   *                          tree for the given key
   * 
   * \return \c true if the given key-value pair was successfully inserted into
   *         the tree, \c false if the tree has reached its capacity
   */
  bool TryInsert(const Key& key, const Value& value, Value& old_value);
  
  /**
   * Tries to remove a given key-value pair from the tree.
   * 
   * \param[IN] key    Key to be removed
   * 
   * \return \c true if the given key-value pair was successfully deleted from
   *         the tree, \c false if there is not enough memory
   */
  bool TryDelete(const Key& key);
  
  /**
   * Tries to remove a given key-value pair from the tree, and returns the value
   * that was stored in the tree for the given key (or \c undefined_value if 
   * the key was not present in the tree).
   * 
   * \param[IN]     key       Key to be removed
   * \param[IN,OUT] old_value Reference to the value previously stored in the
   *                          tree for the given key
   * 
   * \return \c true if the given key-value pair was successfully deleted from
   *         the tree, \c false if there is not enough memory
   */
  bool TryDelete(const Key& key, Value& old_value);
  
  
  /**
   * Accessor for the capacity of the tree.
   * 
   * \return Number of key-value pairs the tree can store
   */
  size_t GetCapacity() const;
  
  /**
   * Accessor for the dummy value used by the tree
   * 
   * \return Object of type \c Value that is used by the tree as a dummy value
   */
  const Value& GetUndefinedValue() const;
  
  /**
   * Checks whether the tree is currently empty.
   * 
   * \return \c true if the tree stores no key-value pairs, \c false otherwise
   */
  bool IsEmpty() const;

 private:
  /** Typedef for a node of the tree. */
  typedef internal::ChromaticTreeNode<Key, Value>   Node;
  /** Typedef for an atomic pointer to a node of the tree. */
  typedef embb::base::Atomic<Node*>                 AtomicNodePtr;
  /** Typedef for an pointer to a node protected by a Hazard Pointer. */
  typedef internal::UniqueHazardPointer<Node>       HazardNodePtr;
  /** Typedef for the UniqueLock class. */
  typedef embb::base::UniqueLock<embb::base::Mutex> UniqueLock;
  /** Typedef for an object pool for tree nodes. */
  typedef ObjectPool<Node, ValuePool>               NodePool;


  /**
   * Follows a path from the root of the tree to some leaf searching for the 
   * given key (the leaf found by this method may or may not contain the given
   * key). Returns the reached leaf together with its ancestors.
   * 
   * \param[IN]     key    Key to be searched for
   * \param[IN,OUT] leaf   Reference to the reached leaf
   * \param[IN,OUT] parent Reference to the parent of the reached leaf
   */
  void Search(const Key& key, HazardNodePtr& leaf, HazardNodePtr& parent);
  
  /**
   * Follows a path from the root of the tree to some leaf searching for the 
   * given key (the leaf found by this method may or may not contain the given
   * key). Returns the reached leaf together with its ancestors.
   * 
   * \param[IN]     key         Key to be searched for
   * \param[IN,OUT] leaf        Reference to the reached leaf
   * \param[IN,OUT] parent      Reference to the parent of the reached leaf
   * \param[IN,OUT] grandparent Reference to the grandparent of the reached leaf
   */
  void Search(const Key& key, HazardNodePtr& leaf, HazardNodePtr& parent,
              HazardNodePtr& grandparent);
  
  /**
   * Checks whether the given node is a leaf.
   * 
   * \param[IN] node Node to be checked
   * 
   * \return \c true if the given node is a leaf, \c false otherwise
   */
  bool IsLeaf(const Node* node) const;
  
  /**
   * Checks whether the given node is a sentinel node.
   * 
   * \param[IN] node Node to be checked
   * 
   * \return \c true if the given node is a sentinel node, \c false otherwise
   */
  bool IsSentinel(const Node* node) const;
  
  /**
   * Checks whether the given node has a specified child node.
   * 
   * \param[IN] parent Parent node
   * \param[IN] child  Node that is supposed to be a child of \c parent
   * 
   * \return \c true if \c child is a child node of \c parent, \c false
   *         otherwise
   */
  bool HasChild(const Node* parent, const Node* child) const;

  /**
   * Destroys all the nodes of a subtree rooted at the given node, including the
   * node itself.
   * 
   * \notthreadsafe
   * 
   * \param node Root of the subtree to be destroyed
   */
  void Destruct(Node* node);

  /**
   * Computes the hight of the subtree rooted at the given node.
   *
   * \notthreadsafe
   *
   * \param[IN] node Root of the subtree for which the height is requested
   *
   * \return The height of a subtree rooted at node \c node. (The height of a
   *         leaf node is defined to be zero).
   */
  int GetHeight(const Node* node) const;

  /**
   * Check whether the tree is currently in a balanced state (if it is a valid
   * red-black tree).
   *
   * \return \c true if the tree is balanced, \c false otherwise
   */
  bool IsBalanced() const;

  /**
   * Check whether a subtree rooted at the given node is balanced.
   *
   * \param[IN] node Root of the subtree for which the balance is checked
   *
   * \return \c true if the tree is balanced, \c false otherwise
   */
  bool IsBalanced(const Node* node) const;

  /**
   * Free a tree node using the Hazard Pointers memory reclamation routines.
   *
   * \param[IN] node A node to be freed.
   * \param[IN] node_lock A lock holding the mutex of the \c node to be freed.
   */
  void RetireHazardousNode(HazardNodePtr& node, UniqueLock& node_lock);

  /**
   * Free a tree node by returning it to the node pool.
   *
   * \param[IN] node A node to be freed.
   */
  void FreeNode(Node* node);

  /**
   * Follows the path from the root to some leaf (directed by the given key) and
   * checks for any tree balancing violations. If a violation is found, tries
   * to fix it by using a set of rebalancing rotations.
   * 
   * \param[IN] key Key to be searched for
   * 
   * \return \c true if the tree was successfully rebalanced, \c false otherwise
   */
  bool CleanUp(const Key& key);
  
  /**
   * Next block of methods is used internally to keep the balance of the tree.
   */
  embb_errors_t Rebalance(HazardNodePtr& u, HazardNodePtr& ux,
                          HazardNodePtr& uxx, HazardNodePtr& uxxx);

  embb_errors_t OverweightLeft(HazardNodePtr& u, UniqueLock& u_lock,
                               HazardNodePtr& ux, UniqueLock& ux_lock,
                               HazardNodePtr& uxx, UniqueLock& uxx_lock,
                               HazardNodePtr& uxl, HazardNodePtr& uxr,
                               HazardNodePtr& uxxl, UniqueLock& uxxl_lock,
                               HazardNodePtr& uxxr, bool uxx_is_left);

  embb_errors_t OverweightRight(HazardNodePtr& u, UniqueLock& u_lock,
                                HazardNodePtr& ux, UniqueLock& ux_lock,
                                HazardNodePtr& uxx, UniqueLock& uxx_lock,
                                HazardNodePtr& uxl, HazardNodePtr& uxr,
                                HazardNodePtr& uxxl, HazardNodePtr& uxxr,
                                UniqueLock& uxxr_lock, bool uxx_is_right);

  // The following included header contains the class methods implementing
  // tree rotations. It is generated automatically and must be included
  // directly inside the class definition.
# include <embb/containers/internal/lock_free_chromatic_tree-rebalance.h>

  /** Hazard pointer instance for protection of node pointers */
  internal::HazardPointer<Node*> node_hazard_manager_;

  const Key     undefined_key_;   /**< A dummy key used by the tree */
  const Value   undefined_value_; /**< A dummy value used by the tree */
  const Compare compare_;         /**< Comparator object for the keys */
  size_t        capacity_;        /**< User-requested capacity of the tree */
  NodePool      node_pool_;       /**< Comparator object for the keys */
  Node* const   entry_;           /**< Pointer to the sentinel node used as
                                   *   the entry point into the tree */

  /**
   * Friending the test class for white-box testing
   */
  friend class test::TreeTest<ChromaticTree>;
};

} // namespace containers
} // namespace embb

#include <embb/containers/internal/lock_free_chromatic_tree-inl.h>

#endif // EMBB_CONTAINERS_LOCK_FREE_CHROMATIC_TREE_H_
