/*
 * *No header guard*: the header is allowed to be included twice
 * with different sets of defines.
 */
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <string.h> /* memmove, memset */
#include <stdint.h>
#include <assert.h>
#include <stdio.h> /* printf */
#include "small/matras.h"

/* {{{ BPS-tree description */
/**
 * BPS-tree implementation.
 * BPS-tree is an in-memory B+*-tree, i.e. B-tree with (+) and (*)
 * variants.
 *
 * Useful links:
 * http://en.wikipedia.org/wiki/B-tree
 * http://en.wikipedia.org/wiki/B-tree#Variants
 * http://en.wikipedia.org/wiki/B%2B_tree
 * http://ru.wikipedia.org/wiki/B*-%D0%B4%D0%B5%D1%80%D0%B5%D0%B2%D0%BE
 *
 * BPS-tree stores specified elements orderly with specified
 * compare function.
 *
 * The tree can be used to insert, replace, delete elements and
 * search values by key.
 * Search/modification of elements has logarithmic complexity,
 * lg B (N).
 *
 * It also has iterator support, providing sequential access to
 * elements in ascending and descending order.  An iterator can be
 * initialized by the first or last element of the tree, or by the
 * lower/upper bound value of a key.  Iteration has constant
 * complexity.
 *
 * The main features of the tree are:
 *
 * 1) It could be very compact. BPS-tree consumes the amount of
 *    memory mostly proportional to (!) the maximal payload of the
 *    tree.  In other words, if a thee contains N elements of size
 *    S, and maximum of N over a lifetime
 *    of the tree is Nmax, then the consumed memory is asymptotically
 *    proportional to (Nmax*S).
 *
 *    In practice, a well configured BPS-tree consumes about 120%
 *    of payload asymptotically when the tree is randomly filled,
 *    i.e. has about 20% of memory overhead on big amounts of
 *    data.
 *
 *    In a rather bad case, when the tree is filled with
 *    monotonically increasing values, the asymptotic overhead is
 *    that about 40% of the payload, and the theoretical maximal
 *    asymptotic overhead is about 60% of the payload.
 *
 *    The  theoretical minimal asymptotic overhead is about 0% :)
 *
 *    However, and it could be important, if a tree is first
 *    filled up and then emptied (but not destroyed), it still
 *    consumes the amount of memory used to index the now
 *    deleted elements.
 *
 * 2) It has a low cache-miss rate. A look up in the tree boils
 *    down to to search in H blocks, where H is the height of the
 *    tree, and can be bound by log(N) / log(K), where N is the
 *    size of the tree and K is the average number of elements in
 *    a block. For example, with 8-byte values and 512-byte blocks,
 *    the tree with a million of elements will probably have height
 *    of 4, and the tree with a billion of elements will probably have
 *    height of 6.
 * 3) Successful insertion into the tree or deletion of an element
 *    can break any of this tree's active iterators.
 *    Nevertheless, dealing with broken iterators never leads to memory
 *    access violation; the element, returned by the iterator is always
 *    valid (the tree contains the value) and iteration never leads
 *    to an infinite loop.
 *    Note, that replacement of an element does not break an iterator
 *    at all.
 *    Note also, that using an uninitialised iterator indeed leads to
 *    memory access violation.
 *
 * Setup and usage:
 *
 * 1) Define all macros like in the example below before including
 *    this header. See "BPS-tree interface settings" section for
 *    details. Example:
 *
 * #define BPS_TREE_NAME
 * #define BPS_TREE_BLOCK_SIZE 512
 * #define BPS_TREE_EXTENT_SIZE 16*1024
 * #define BPS_TREE_COMPARE(a, b, context) my_compare(a, b, context)
 * #define BPS_TREE_COMPARE_KEY(a, b, context) my_compare_key(a, b, context)
 * #define bps_tree_elem_t struct tuple *
 * #define bps_tree_key_t struct key_t *
 * #define bps_tree_arg_t struct compare_context *
 *
 * 2) Use structs and functions from the list below.
 *   See "BPS-tree interface" section for details. Here is short list:
 * // types:
 * struct bps_tree;
 * struct bps_tree_iterator;
 * typedef void *(*bps_tree_extent_alloc_f)();
 * typedef void (*bps_tree_extent_free_f)(void *);
 * // base:
 * void bps_tree_create(tree, arg, extent_alloc_func, extent_free_func,
 *                      alloc_ctx);
 * void bps_tree_destroy(tree);
 * int bps_tree_build(tree, sorted_array, array_size);
 * bps_tree_elem_t *bps_tree_find(tree, key);
 * int bps_tree_insert(tree, new_elem, replaced_elem);
 * int bps_tree_delete(tree, elem);
 * size_t bps_tree_size(tree);
 * size_t bps_tree_mem_used(tree);
 * bps_tree_elem_t *bps_tree_random(tree, rnd);
 * int bps_tree_debug_check(tree);
 * void bps_tree_print(tree, "%p");
 * int bps_tree_debug_check_internal_functions(assert_on_error);
 * // iterators:
 * struct bps_tree_iterator bps_tree_invalid_iterator();
 * bool bps_tree_iterator_is_invalid(itr);
 * bool bps_tree_iterator_are_equal(tree, itr1, itr2);
 * struct bps_tree_iterator bps_tree_iterator_first(tree);
 * struct bps_tree_iterator bps_tree_iterator_last(tree);
 * struct bps_tree_iterator bps_tree_lower_bound(tree, key, exact);
 * struct bps_tree_iterator bps_tree_upper_bound(tree, key, exact);
 * size_t bps_tree_approxiamte_count(tree, key);
 * bps_tree_elem_t *bps_tree_iterator_get_elem(tree, itr);
 * bool bps_tree_iterator_next(tree, itr);
 * bool bps_tree_iterator_prev(tree, itr);
 * void bps_tree_iterator_freeze(tree, itr);
 * void bps_tree_iterator_destroy(tree, itr);
 */
/* }}} */

/* {{{ BPS-tree interface settings */
/**
 * Custom name for structs and functions.
 * Struct and functions will have bps_tree##BPS_TREE_NAME name or prefix.
 * For example one can #define BPS_TREE_NAME _test, and use then
 *  struct bps_tree_test my_tree;
 *  bps_tree_test_create(&my_tree, ...);
 * Allowed to be empty (just #define BPS_TREE_NAME)
 */
#ifndef BPS_TREE_NAME
#error "BPS_TREE_NAME must be defined"
#endif

/**
 * Size of a block of the tree. A block should be large enough to contain
 * dozens of elements and dozens of 32-bit identifiers.
 * Must be a power of 2, i.e. log2(BPS_TREE_BLOCK_SIZE) must be an integer.
 * Tests show that for 64-bit elements, an ideal block size is 512 bytes
 * if binary search is used, and 256 bytes if linear search is used.
 * (see below for the binary/linear search setting)
 * Example:
 * #define BPS_TREE_BLOCK_SIZE 512
 */
#ifndef BPS_TREE_BLOCK_SIZE
#error "BPS_TREE_BLOCK_SIZE must be defined"
#endif

/**
 * Allocation granularity. The tree allocates memory by extents of
 * that size.  Must be power of 2, i.e. log2(BPS_TREE_EXTENT_SIZE)
 * must be a whole number.
 * Two important things:
 *
 * 1) The maximal amount of memory, that particular btree instance
 *    can use, is
 *   ( (BPS_TREE_EXTENT_SIZE ^ 3) / (sizeof(void *) ^ 2) )
 *
 * 2) The first insertion of an element leads to immidiate
 *    allocation of three extents. Thus, memory overhead of almost
 *    empty tree is
 *  3 * BPS_TREE_EXTENT_SIZE
 *
 * Example:
 * #define BPS_TREE_EXTENT_SIZE 8*1024
 */
#ifndef BPS_TREE_EXTENT_SIZE
#error "BPS_TREE_EXTENT_SIZE must be defined"
#endif

/**
 * Type of the tree element. Must be POD. The implementation
 * copies elements by memmove and assignment operator and
 * compares them with comparators defined below, and also
 * could be compared with operator == Example:
 * #define bps_tree_elem_t struct tuple *
 */
#ifndef bps_tree_elem_t
#error "bps_tree_elem_t must be defined"
#endif

/**
 * Type of tree key. Must be POD. Used for finding an element in
 * the tree and in iterator initialization.
 * Example:
 * #define bps_tree_key_t struct key_data *
 */
#ifndef bps_tree_key_t
#error "bps_tree_key_t must be defined"
#endif

/**
 * Type of comparison additional argument. The argument of this
 * type is initialized during tree creation and then passed to
 * compare function. If it is non necessary, define as int and
 * forget. Example:
 *
 * #define bps_tree_arg_t struct key_def *
 */
#ifndef bps_tree_arg_t
#define bps_tree_arg_t int
#endif

/**
 * Function to compare elements.
 * Parameters: two elements and an additional argument, specified
 * for the tree instance. See struct bps_tree members for details.
 * Must return int-compatible value, like strcmp or memcmp
 * Examples:
 * #define BPS_TREE_COMPARE(a, b, arg) ((a) < (b) ? -1 : (a) > (b))
 * #define BPS_TREE_COMPARE(a, b, arg) my_compare(a, b, arg)
 */
#ifndef BPS_TREE_COMPARE
#error "BPS_TREE_COMPARE must be defined"
#endif

/**
 * Function to compare an element with a key.
 * Parameters: element, key and an additional argument, specified
 * for the tree instance. See struct bps_tree members for details.
 * Must return int-compatible value, like strcmp or memcmp
 * Examples:
 * #define BPS_TREE_COMPARE_KEY(a, b, arg) ((a) < (b) ? -1 : (a) > (b))
 * #define BPS_TREE_COMPARE_KEY(a, b, arg) BPS_TREE_COMPARE(a, b, arg)
 */
#ifndef BPS_TREE_COMPARE_KEY
#error "BPS_TREE_COMPARE_KEY must be defined"
#endif

/**
 * A switch to define the type of search in an array elements.
 * By default, bps_tree uses binary search to find a particular
 * element in a block. But if the element type is simple
 * (like an integer or float) it could be significantly faster to
 * use linear search. To turn on the linear search
 * #define BPS_BLOCK_LINEAR_SEARCH
 */

/**
 * A switch that enables collection of executions of different
 * branches of code. Used only for debug purposes, I hope you
 * will not use it. Nevertheless, to turn it on,
 * #define BPS_TREE_DEBUG_BRANCH_VISIT
 */

/* }}} */

/* {{{ BPS-tree internal settings */
typedef int16_t bps_tree_pos_t;
typedef uint32_t bps_tree_block_id_t;
/* }}} */

/* {{{ Compile time utils */
/**
 * Concatenation of name at compile time
 */
#ifndef CONCAT
#define CONCAT_R(a, b) a##b
#define CONCAT(a, b) CONCAT_R(a, b)
#define CONCAT3_R(a, b, c) a##b##c
#define CONCAT3(a, b, c) CONCAT3_R(a, b, c)
#define CONCAT4_R(a, b, c, d) a##b##c##d
#define CONCAT4(a, b, c, d) CONCAT4_R(a, b, c, d)
#define CONCAT5_R(a, b, c, d, e) a##b##c##d##e
#define CONCAT5(a, b, c, d, e) CONCAT5_R(a, b, c, d, e)
#endif
/**
 * Compile time assertion for use in function blocks
 */
#ifndef CT_ASSERT
#define CT_ASSERT(e) do { typedef char __ct_assert[(e) ? 1 : -1]; } while(0)
#endif
/**
 * Compile time assertion for use in global scope (and in class scope)
 */
#ifndef CT_ASSERT_G
#define CT_ASSERT_G(e) typedef char CONCAT(__ct_assert_, __LINE__)[(e) ? 1 :-1]
#endif
/* }}} */

/* {{{ Macros for custom naming of structs and functions */
#ifdef _
#error '_' must be undefinded!
#endif
#define _bps(postfix) CONCAT4(bps, BPS_TREE_NAME, _, postfix)
#define _api_name(postfix) CONCAT3(BPS_TREE_NAME, _, postfix)
#define _bps_tree(postfix) CONCAT5(bps, _tree_, BPS_TREE_NAME, _, postfix)
#define _BPS(postfix) CONCAT5(BPS, _, BPS_TREE_NAME, _, postfix)
#define _BPS_TREE(postfix) CONCAT5(BPS_TREE, _, BPS_TREE_NAME, _, postfix)
#define _bps_tree_name BPS_TREE_NAME

#define bps_tree BPS_TREE_NAME
#define bps_block _bps(block)
#define bps_leaf _bps(leaf)
#define bps_inner _bps(inner)
#define bps_garbage _bps(garbage)
#define bps_tree_iterator _api_name(iterator)
#define bps_inner_path_elem _bps(inner_path_elem)
#define bps_leaf_path_elem _bps(leaf_path_elem)

#define bps_tree_create _api_name(create)
#define bps_tree_build _api_name(build)
#define bps_tree_destroy _api_name(destroy)
#define bps_tree_find _api_name(find)
#define bps_tree_insert _api_name(insert)
#define bps_tree_delete _api_name(delete)
#define bps_tree_size _api_name(size)
#define bps_tree_mem_used _api_name(mem_used)
#define bps_tree_random _api_name(random)
#define bps_tree_invalid_iterator _api_name(invalid_iterator)
#define bps_tree_iterator_is_invalid _api_name(iterator_is_invalid)
#define bps_tree_iterator_are_equal _api_name(iterator_are_equal)
#define bps_tree_iterator_first _api_name(iterator_first)
#define bps_tree_iterator_last _api_name(iterator_last)
#define bps_tree_lower_bound _api_name(lower_bound)
#define bps_tree_upper_bound _api_name(upper_bound)
#define bps_tree_approximate_count _api_name(approximate_count)
#define bps_tree_iterator_get_elem _api_name(iterator_get_elem)
#define bps_tree_iterator_next _api_name(iterator_next)
#define bps_tree_iterator_prev _api_name(iterator_prev)
#define bps_tree_iterator_freeze _api_name(iterator_freeze)
#define bps_tree_iterator_destroy _api_name(iterator_destroy)
#define bps_tree_debug_check _api_name(debug_check)
#define bps_tree_print _api_name(print)
#define bps_tree_debug_check_internal_functions \
	_api_name(debug_check_internal_functions)

#define bps_tree_max_sizes _bps_tree(max_sizes)
#define BPS_TREE_MAX_COUNT_IN_LEAF _BPS_TREE(MAX_COUNT_IN_LEAF)
#define BPS_TREE_MAX_COUNT_IN_INNER _BPS_TREE(MAX_COUNT_IN_INNER)
#define BPS_TREE_MAX_DEPTH _BPS_TREE(MAX_DEPTH)
#define bps_block_type _bps(block_type)
#define BPS_TREE_BT_GARBAGE _BPS_TREE(BT_GARBAGE)
#define BPS_TREE_BT_INNER _BPS_TREE(BT_INNER)
#define BPS_TREE_BT_LEAF _BPS_TREE(BT_LEAF)

#define bps_tree_restore_block _bps_tree(restore_block)
#define bps_tree_restore_block_ver _bps_tree(restore_block_ver)
#define bps_tree_root _bps_tree(root)
#define bps_tree_touch_block _bps_tree(touch_block)
#define bps_tree_find_ins_point_key _bps_tree(find_ins_point_key)
#define bps_tree_find_ins_point_elem _bps_tree(find_ins_point_elem)
#define bps_tree_find_after_ins_point_key _bps_tree(find_after_ins_point_key)
#define bps_tree_get_leaf_safe _bps_tree(get_leaf_safe)
#define bps_tree_garbage_push _bps_tree(garbage_push)
#define bps_tree_garbage_pop _bps_tree(garbage_pop)
#define bps_tree_create_leaf _bps_tree(create_leaf)
#define bps_tree_create_inner _bps_tree(create_inner)
#define bps_tree_dispose_leaf _bps_tree(dispose_leaf)
#define bps_tree_dispose_inner _bps_tree(dispose_inner)
#define bps_tree_reserve_blocks _bps_tree(reserve_blocks)
#define bps_tree_insert_first_elem _bps_tree(insert_first_elem)
#define bps_tree_collect_path _bps_tree(collect_path)
#define bps_tree_touch_leaf_path_max_elem _bps_tree(touch_leaf_path_max_elem)
#define bps_tree_touch_path _bps_tree(touch_path_max_elem)
#define bps_tree_process_replace _bps_tree(process_replace)
#define bps_tree_debug_memmove _bps_tree(debug_memmove)
#define bps_tree_insert_into_leaf _bps_tree(insert_into_leaf)
#define bps_tree_insert_into_inner _bps_tree(insert_into_inner)
#define bps_tree_delete_from_leaf _bps_tree(delete_from_leaf)
#define bps_tree_delete_from_inner _bps_tree(delete_from_inner)
#define bps_tree_move_elems_to_right_leaf _bps_tree(move_elems_to_right_leaf)
#define bps_tree_move_elems_to_right_inner _bps_tree(move_elems_to_right_inner)
#define bps_tree_move_elems_to_left_leaf _bps_tree(move_elems_to_left_leaf)
#define bps_tree_move_elems_to_left_inner _bps_tree(move_elems_to_left_inner)
#define bps_tree_insert_and_move_elems_to_right_leaf \
	_bps_tree(insert_and_move_elems_to_right_leaf)
#define bps_tree_insert_and_move_elems_to_right_inner \
	_bps_tree(insert_and_move_elems_to_right_inner)
#define bps_tree_insert_and_move_elems_to_left_leaf \
	_bps_tree(insert_and_move_elems_to_left_leaf)
#define bps_tree_insert_and_move_elems_to_left_inner \
	_bps_tree(insert_and_move_elems_to_left_inner)
#define bps_tree_leaf_free_size _bps_tree(leaf_free_size)
#define bps_tree_inner_free_size _bps_tree(inner_free_size)
#define bps_tree_leaf_overmin_size _bps_tree(leaf_overmin_size)
#define bps_tree_inner_overmin_size _bps_tree(inner_overmin_size)
#define bps_tree_collect_left_path_elem_leaf \
	_bps_tree(collect_left_path_elem_leaf)
#define bps_tree_collect_left_path_elem_inner \
	_bps_tree(collect_left_path_elem_inner)
#define bps_tree_collect_right_ext_leaf _bps_tree(collect_right_ext_leaf)
#define bps_tree_collect_right_ext_inner _bps_tree(collect_right_ext_inner)
#define bps_tree_prepare_new_ext_leaf _bps_tree(prepare_new_ext_leaf)
#define bps_tree_prepare_new_ext_inner _bps_tree(prepare_new_ext_inner)
#define bps_tree_process_insert_leaf _bps_tree(process_insert_leaf)
#define bps_tree_process_insert_inner _bps_tree(process_insert_inner)
#define bps_tree_process_delete_leaf _bps_tree(process_delete_leaf)
#define bps_tree_process_delete_inner _bps_tree(process_delete_inner)
#define bps_tree_debug_find_max_elem _bps_tree(debug_find_max_elem)
#define bps_tree_debug_check_block _bps_tree(debug_check_block)
#define bps_tree_print_indent _bps_tree(print_indent)
#define bps_tree_print_block _bps_tree(print_block)
#define bps_tree_print_leaf _bps_tree(print_leaf)
#define bps_tree_print_inner _bps_tree(print_inner)
#define bps_tree_debug_set_elem _bps_tree(debug_set_elem)
#define bps_tree_debug_get_elem _bps_tree(debug_get_elem)
#define bps_tree_debug_set_elem_inner _bps_tree(debug_set_elem_inner)
#define bps_tree_debug_get_elem_inner _bps_tree(debug_get_elem_inner)
#define bps_tree_debug_check_insert_into_leaf \
	_bps_tree(debug_check_insert_into_leaf)
#define bps_tree_debug_check_delete_from_leaf \
	_bps_tree(debug_check_delete_from_leaf)
#define bps_tree_debug_check_move_to_right_leaf \
	_bps_tree(debug_check_move_to_right_leaf)
#define bps_tree_debug_check_move_to_left_leaf \
	_bps_tree(debug_check_move_to_left_leaf)
#define bps_tree_debug_check_insert_and_move_to_right_leaf \
	_bps_tree(debug_check_insert_and_move_to_right_leaf)
#define bps_tree_debug_check_insert_and_move_to_left_leaf \
	_bps_tree(debug_check_insert_and_move_to_left_leaf)
#define bps_tree_debug_check_insert_into_inner \
	_bps_tree(debug_check_insert_into_inner)
#define bps_tree_debug_check_delete_from_inner \
	_bps_tree(debug_check_delete_from_inner)
#define bps_tree_debug_check_move_to_right_inner \
	_bps_tree(debug_check_move_to_right_inner)
#define bps_tree_debug_check_move_to_left_inner \
	_bps_tree(debug_check_move_to_left_inner)
#define bps_tree_debug_check_insert_and_move_to_right_inner \
	_bps_tree(debug_check_insert_and_move_to_right_inner)
#define bps_tree_debug_check_insert_and_move_to_left_inner \
	_bps_tree(debug_check_insert_and_move_to_left_inner)
/* }}} */

/* {{{ BPS-tree interface (declaration) */

/**
 * struct bps_block forward declaration (Used in struct bps_tree)
 */struct bps_block;

#ifdef BPS_TREE_DEBUG_BRANCH_VISIT
#define BPS_TREE_BRANCH_TRACE(tree, type, branch_bit) \
	(tree)->debug_##type##_branches_mask |= (branch_bit)
#else
#define BPS_TREE_BRANCH_TRACE(tree, type, branch_bit) \
	((void)0)
#endif

/**
 * Main tree struct. One instance - one tree.
 */
struct bps_tree {
	/* ID of root block. (bps_tree_block_id_t)-1 in empty tree. */
	bps_tree_block_id_t root_id;
	/* IDs of first and last block. (-1) in empty tree. */
	bps_tree_block_id_t first_id, last_id;
	/* Counters of used blocks and garbaged blocks */
	bps_tree_block_id_t leaf_count, inner_count, garbage_count;
	/* Depth (height?) of a tree. Is 0 in empty tree. */
	bps_tree_block_id_t depth;
	/* Number of elements in tree */
	size_t size;
	/* Head of list of garbaged blocks */
	bps_tree_block_id_t garbage_head_id;
	/* User-provided argument for comparator */
	bps_tree_arg_t arg;
	/* Copy of maximal element in tree. Used for beauty */
	bps_tree_elem_t max_elem;
	/* Special allocator of blocks and their IDs */
	struct matras matras;
#ifdef BPS_TREE_DEBUG_BRANCH_VISIT
	/* Bit masks of different branches visits */
	uint32_t debug_insert_leaf_branches_mask;
	uint32_t debug_insert_inner_branches_mask;
	uint32_t debug_delete_leaf_branches_mask;
	uint32_t debug_delete_inner_branches_mask;
	/* Possible masks of different branches visits */
	uint32_t debug_insert_leaf_branches_max_mask;
	uint32_t debug_insert_inner_branches_max_mask;
	uint32_t debug_delete_leaf_branches_max_mask;
	uint32_t debug_delete_inner_branches_max_mask;
#endif
};

/**
 * Tree iterator. Points to an element in tree.
 * There are 4 possible states of iterator:
 * 1)Normal. Points to concrete element in tree.
 * 2)Invalid. Points to nothing. Safe.
 * 3)Broken. Normal can become broken during tree modification.
 *  Safe to use, but has undefined behavior.
 * 4)Uninitialized (or initialized in wrong way).
 *  Unsafe and undefined behaviour.
 */
struct bps_tree_iterator {
	/* ID of a block, containing element. -1 for an invalid iterator */
	bps_tree_block_id_t block_id;
	/* Position of an element in the block. Could be -1 for last in block*/
	bps_tree_pos_t pos;
	/* Version of matras memory for MVCC */
	struct matras_view view;
};

/**
 * Pointer to function that allocates extent of size BPS_TREE_EXTENT_SIZE
 * BPS-tree properly handles with NULL result but could leak memory
 *  in case of exception.
 */
typedef void *(*bps_tree_extent_alloc_f)(void *ctx);

/**
 * Pointer to function frees extent (of size BPS_TREE_EXTENT_SIZE)
 */
typedef void (*bps_tree_extent_free_f)(void *ctx, void *extent);

/**
 * @brief Tree construction. Fills struct bps_tree members.
 * @param tree - pointer to a tree
 * @param arg - user defined argument for comparator
 * @param extent_alloc_func - pointer to function that allocates extents,
 *  see bps_tree_extent_alloc_f description for details
 * @param extent_free_func - pointer to function that allocates extents,
 * @param alloc_ctx - argument passed to extent allocator
 *  see bps_tree_extent_free_f description for details
 */
static inline void
bps_tree_create(struct bps_tree *tree, bps_tree_arg_t arg,
		bps_tree_extent_alloc_f extent_alloc_func,
		bps_tree_extent_free_f extent_free_func,
		void *alloc_ctx);

/**
 * @brief Fills a new (asserted) tree with values from sorted array.
 *  Elements are copied from the array. Array is not checked to be sorted!
 * @param tree - pointer to a tree
 * @param sorted_array - pointer to the sorted array
 * @param array_size - size of the array (count of elements)
 * @return 0 on success, -1 on memory error
 */
static inline int
bps_tree_build(struct bps_tree *tree, bps_tree_elem_t *sorted_array,
	       size_t array_size);

/**
 * @brief Tree destruction. Frees allocated memory.
 * @param tree - pointer to a tree
 */
static inline void
bps_tree_destroy(struct bps_tree *tree);

/**
 * @brief Find the first element that is equal to the key (comparator returns 0)
 * @param tree - pointer to a tree
 * @param key - key that will be compared with elements
 * @return pointer to the first equal element or NULL if not found
 */
static inline bps_tree_elem_t *
bps_tree_find(const struct bps_tree *tree, bps_tree_key_t key);

/**
 * @brief Insert an element to the tree or replace an element in the tree
 * In case of replacing, if 'replaced' argument is not null,
 *  it'll be filled with replaced element. In case of inserting it's untoched.
 * Thus one can distinguish real insert or replace by passing to the function
 *  pointer to some value; and if it was changed during the function call,
 *  then the replace was happend; insert otherwise.
 * @param tree - pointer to a tree
 * @param new_elem - inserting or replacing element
 * @replaced - optional pointer for a replaces element
 * @return - 0 on success or -1 if memory allocation failed for insert
 */
static inline int
bps_tree_insert(struct bps_tree *tree, bps_tree_elem_t new_elem,
		bps_tree_elem_t *replaced);

/**
 * @brief Delete an element from a tree.
 * @param tree - pointer to a tree
 * @param elem - the element tot delete
 * @return - 0 on success or -1 if the element was not found in tree
 */
static inline int
bps_tree_delete(struct bps_tree *tree, bps_tree_elem_t elem);

/**
 * @brief Get size of tree, i.e. count of elements in tree
 * @param tree - pointer to a tree
 * @return - count count of elements in tree
 */
static inline size_t
bps_tree_size(const struct bps_tree *tree);

/**
 * @brief Get amount of memory in bytes that the tree is using
 *  (not including sizeof(struct bps_tree))
 * @param tree - pointer to a tree
 * @return - count count of elements in tree
 */
static inline size_t
bps_tree_mem_used(const struct bps_tree *tree);

/**
 * @brief Get a random element in a tree.
 * @param tree - pointer to a tree
 * @param rnd - some random value
 * @return - count count of elements in tree
 */
static inline bps_tree_elem_t *
bps_tree_random(const struct bps_tree *tree, size_t rnd);

/**
 * @brief Get an invalid iterator. See iterator description.
 * @return - Invalid iterator
 */
static inline struct bps_tree_iterator
bps_tree_invalid_iterator();

/**
 * @brief Check if an iterator is invalid. See iterator description.
 * @param itr - iterator to check
 * @return - true if iterator is invalid, false otherwise
 */
static inline bool
bps_tree_iterator_is_invalid(struct bps_tree_iterator *itr);

/**
 * @brief Compare two iterators and return true if trey points to same element.
 *  Two invalid iterators are equal and points to the same nowhere.
 *  Broken iterator is possibly not equal to any valid or invalid iterators.
 * @param tree - pointer to a tree
 * @param itr1 - first iterator
 * @param itr2 - second iterator
 * @return - true if iterators are equal, false otherwise
 */
static inline bool
bps_tree_iterator_are_equal(const struct bps_tree *tree,
		            struct bps_tree_iterator *itr1,
		            struct bps_tree_iterator *itr2);

/**
 * @brief Get an iterator to the first element of the tree
 * @param tree - pointer to a tree
 * @return - First iterator. Could be invalid if the tree is empty.
 */
static inline struct bps_tree_iterator
bps_tree_iterator_first(const struct bps_tree *tree);

/**
 * @brief Get an iterator to the last element of the tree
 * @param tree - pointer to a tree
 * @return - Last iterator. Could be invalid if the tree is empty.
 */
static inline struct bps_tree_iterator
bps_tree_iterator_last(const struct bps_tree *tree);

/**
 * @brief Get an iterator to the first element that is greater or
 * equal than key
 * @param tree - pointer to a tree
 * @param key - key that will be compared with elements
 * @param exact - pointer to a bool value, that will be set to true if
 *  and element pointed by the iterator is equal to the key, false otherwise
 *  Pass NULL if you don't need that info.
 * @return - Lower-bound iterator. Invalid if all elements are less than key.
 */
static inline struct bps_tree_iterator
bps_tree_lower_bound(const struct bps_tree *tree, bps_tree_key_t key,
		     bool *exact);

/**
 * @brief Get an iterator to the first element that is greater than key
 * @param tree - pointer to a tree
 * @param key - key that will be compared with elements
 * @param exact - pointer to a bool value, that will be set to true if
 *  and element pointed by the (!)previous iterator is equal to the key,
 *  false otherwise. Pass NULL if you don't need that info.
 * @return - Upper-bound iterator. Invalid if all elements are less or equal
 *  than the key.
 */
static inline struct bps_tree_iterator
bps_tree_upper_bound(const struct bps_tree *tree, bps_tree_key_t key,
		     bool *exact);

/**
 * @brief Get approximate number of entries that are equal to given key.
 * Accuracy limits:
 * If the result is less than BPS_TREE_name_MAX_COUNT_IN_LEAF * 5 / 6, the
 * result is precise. If not, let's define:
 * X = BPS_TREE_name_MAX_COUNT_IN_LEAF * 5 / 6
 * Y = BPS_TREE_name_MAX_COUNT_IN_INNER * 5 / 6
 * H = ceil(log(Result / X) / log(Y))
 * Then the true count is between:
 * [ Result * pow(0.8 - z, H), Result * pow(1.2 + z, H) ]
 * where z parameter is a small number due to rounding errors
 * @param tree - pointer to a tree
 * @param key - key that will be compared with elements
 * @return - approximate number of entries that are equal to given key.
 */
static inline size_t
bps_tree_approximate_count(const struct bps_tree *tree, bps_tree_key_t key);

/**
 * @brief Get a pointer to the element pointed by iterator.
 *  If iterator is detected as broken, it is invalidated and NULL returned.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 * @return - Pointer to the element. Null for invalid iterator
 */
static inline bps_tree_elem_t *
bps_tree_iterator_get_elem(const struct bps_tree *tree,
		           struct bps_tree_iterator *itr);

/**
 * @brief Increments an iterator, makes it point to the next element
 *  If the iterator is to last element, it will be invalidated
 *  If the iterator is detected as broken, it will be invalidated.
 *  If the iterator is invalid, then it will be set to first element.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 * @return - true on success, false if a resulted iterator is set to invalid
 */
static inline bool
bps_tree_iterator_next(const struct bps_tree *tree, struct bps_tree_iterator *itr);

/**
 * @brief Decrements an iterator, makes it point to the previous element
 *  If the iterator is to first element, it will be invalidated
 *  If the iterator is detected as broken, it will be invalidated.
 *  If the iterator is invalid, then it will be set to last element.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 * @return - true on success, false if a resulted iterator is set to invalid
 */
static inline bool
bps_tree_iterator_prev(const struct bps_tree *tree, struct bps_tree_iterator *itr);

/**
 * @brief Freezes tree state for given iterator. All following tree modification
 * will not apply to that iterator iteration. That iterator should be destroyed
 * with a bps_tree_iterator_destroy call after usage.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 */
static inline void
bps_tree_iterator_freeze(struct bps_tree *tree, struct bps_tree_iterator *itr);

/**
 * @brief Destroy an iterator that was frozen before. Useless for not frozen
 * iterators.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 */
static inline void
bps_tree_iterator_destroy(struct bps_tree *tree, struct bps_tree_iterator *itr);

#ifndef BPS_TREE_NO_DEBUG

/**
 * @brief Debug self-checking. Returns bitmask of found errors (0
 * on success).
 *  I hope you will not need it.
 * @param tree - pointer to a tree
 * @return - Bitwise-OR of all errors found
 */
static inline int
bps_tree_debug_check(const struct bps_tree *tree);

/**
 * @brief Debug print tree to output in readable form.
 *  I hope you will not need it.
 * @param tree - tree to print
 * @param elem_fmt - format for printing an element. "%d" or "%p" for example.
 */
static inline void
bps_tree_print(const struct bps_tree *tree, const char *elem_fmt);

/**
 * @brief Debug print tree to output in readable form.
 *  I hope you will not need it.
 * @param assertme - if true, errors will lead to assert call,
 *  if false, just error code will be returned.
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_internal_functions(bool assertme);

#endif /* BPS_TREE_NO_DEBUG */

/* }}} */

/* {{{ BPS-tree implementation (definition) */

/* Data moving */
#ifndef NDEBUG
/* Debug version checks buffer overflow an runtime */
#define BPS_TREE_MEMMOVE(dst, src, num, dst_block, src_block) \
	bps_tree_debug_memmove(dst, src, num, dst_block, src_block)
#else
/* Release version just moves memory */
#define BPS_TREE_MEMMOVE(dst, src, num, dst_block, src_block) \
	memmove(dst, src, num)
#endif
/* Same as BPS_TREE_MEMMOVE but takes count of values instead of memory size */
#define BPS_TREE_DATAMOVE(dst, src, num, dst_bck, src_bck) \
	BPS_TREE_MEMMOVE(dst, src, (num) * sizeof((dst)[0]), dst_bck, src_bck)

/**
 * Types of a block
 */
enum bps_block_type {
	BPS_TREE_BT_GARBAGE = 1,
	BPS_TREE_BT_INNER = 2,
	BPS_TREE_BT_LEAF = 4
};

/**
 * Header for bps_leaf, bps_inner or bps_garbage blocks
 */
struct bps_block {
	/* Type of a block. See bps_block_type. Used for iterators and debug */
	bps_tree_pos_t type;
	/* Count of elements for leaf, and of children for inner nodes */
	bps_tree_pos_t size;
};

/**
 * Calculation of max sizes (max count + 1)
 */
enum bps_tree_max_sizes {
	BPS_TREE_MAX_COUNT_IN_LEAF =
		(BPS_TREE_BLOCK_SIZE - sizeof(struct bps_block)
		 - 2 * sizeof(bps_tree_block_id_t) )
		/ sizeof(bps_tree_elem_t),
	BPS_TREE_MAX_COUNT_IN_INNER =
		(BPS_TREE_BLOCK_SIZE - sizeof(struct bps_block))
		/ (sizeof(bps_tree_elem_t) + sizeof(bps_tree_block_id_t)),
	BPS_TREE_MAX_DEPTH = 16
};

/**
 * B* tree modification makes most of blocks to be filled al least of 2/3
 * of the allocated space, so allocated space must be al least 3.
 */
CT_ASSERT_G(BPS_TREE_MAX_COUNT_IN_LEAF >= 3);
CT_ASSERT_G(BPS_TREE_MAX_COUNT_IN_INNER >= 3);

/**
 * Leaf block definition.
 * Contains array of element on the last level of the tree
 */
struct bps_leaf {
	/* Block header */
	struct bps_block header;
	/* Next leaf block ID in ordered linked list */
	bps_tree_block_id_t next_id;
	/* Previous leaf block ID in ordered linked list */
	bps_tree_block_id_t prev_id;
	/* Ordered array of elements */
	bps_tree_elem_t elems[BPS_TREE_MAX_COUNT_IN_LEAF];
};

/**
 * Stop compile if smth went terribly wrong
 */
CT_ASSERT_G(sizeof(struct bps_leaf) <= BPS_TREE_BLOCK_SIZE);

/**
 * Inner block definition.
 * Contains an array of child (inner of leaf) IDs, and array of
 * copies of maximal elements of the corresponding subtrees. Only
 * last child subtree does not have corresponding element copy in
 * this array (but it has a copy of maximal element somewhere in
 * parent's arrays on in tree struct)
 */
struct bps_inner {
	/* Block header */
	struct bps_block header;
	/* Ordered array of elements. Note -1 in size. See struct descr. */
	bps_tree_elem_t elems[BPS_TREE_MAX_COUNT_IN_INNER - 1];
	/* Corresponding child IDs */
	bps_tree_block_id_t child_ids[BPS_TREE_MAX_COUNT_IN_INNER];
};

/**
 * Stop compile if smth went terribly wrong
 */
CT_ASSERT_G(sizeof(struct bps_inner) <= BPS_TREE_BLOCK_SIZE);

/**
 * Garbaged block definition
 */
struct bps_garbage {
	/* Block header */
	struct bps_block header;
	/* Next garbaged block id in single-linked list */
	bps_tree_block_id_t next_id;
	/* If a leaf is pushed to garbage, next and previous blocks' IDs are
	 * saved in the garbage node in order to fix iterators pointing to
	 * that node. */
	/* Next leaf block ID in ordered linked list */
	bps_tree_block_id_t next_leaf_id;
	/* Previous leaf block ID in ordered linked list */
	bps_tree_block_id_t prev_leaf_id;
};

/**
 * Stop compile if smth went terribly wrong
 */
CT_ASSERT_G(sizeof(struct bps_garbage) <= BPS_TREE_BLOCK_SIZE);

/**
 * Struct for collecting path in tree, corresponds to one inner block
 */
struct bps_inner_path_elem {
	/* Pointer to block */
	struct bps_inner *block;
	/* ID of the block */
	bps_tree_block_id_t block_id;
	/* Position of next path element in block's child_ids array */
	bps_tree_pos_t insertion_point;
	/* Position of this path element in parent's child_ids array */
	bps_tree_pos_t pos_in_parent;
	/* Pointer to parent block (NULL for root) */
	struct bps_inner_path_elem *parent;
	/* Pointer to the sequent to the max element in the subtree */
	bps_tree_elem_t *max_elem_copy;
	/* Holder of max_elem_copy (block_id) */
	bps_tree_block_id_t max_elem_block_id;
	/* Holder of max_elem_copy (pos) */
	bps_tree_pos_t max_elem_pos;
};

/**
 * An auxiliary struct to collect a path in tree,
 * corresponds  to one leaf block/one element of the path.
 *
 */
struct bps_leaf_path_elem {
	/* A pointer to the block */
	struct bps_leaf *block;
	/* ID of the block */
	bps_tree_block_id_t block_id;
	/* Position of the next path element in block's child_ids array */
	bps_tree_pos_t insertion_point;
	/* Position of this path element in parent's child_ids array */
	bps_tree_pos_t pos_in_parent;
	/* A pointer to the parent block (NULL for root) */
	struct bps_inner_path_elem *parent;
	/* A pointer to the sequent to the max element in the subtree */
	bps_tree_elem_t *max_elem_copy;
	/* Holder of max_elem_copy (block_id) */
	bps_tree_block_id_t max_elem_block_id;
	/* Holder of max_elem_copy (pos) */
	bps_tree_pos_t max_elem_pos;
};

/**
 * @brief Tree construction. Fills struct bps_tree members.
 * @param tree - pointer to a tree
 * @param arg - user defined argument for comparator
 * @param extent_alloc_func - pointer to function that allocates extents,
 *  see bps_tree_extent_alloc_f description for details
 * @param extent_free_func - pointer to function that allocates extents,
 * @param alloc_ctx - argument passed to extent allocator
 *  see bps_tree_extent_free_f description for details
 */
static inline void
bps_tree_create(struct bps_tree *tree, bps_tree_arg_t arg,
		bps_tree_extent_alloc_f extent_alloc_func,
		bps_tree_extent_free_f extent_free_func,
		void *alloc_ctx)
{
	tree->root_id = (bps_tree_block_id_t)(-1);
	tree->first_id = (bps_tree_block_id_t)(-1);
	tree->last_id = (bps_tree_block_id_t)(-1);
	tree->leaf_count = 0;
	tree->inner_count = 0;
	tree->garbage_count = 0;
	tree->depth = 0;
	tree->size = 0;
	tree->garbage_head_id = (bps_tree_block_id_t)(-1);
	tree->arg = arg;
	memset(&tree->max_elem, 0, sizeof(tree->max_elem));

	matras_create(&tree->matras,
		      BPS_TREE_EXTENT_SIZE, BPS_TREE_BLOCK_SIZE,
		      extent_alloc_func, extent_free_func, alloc_ctx);

#ifdef BPS_TREE_DEBUG_BRANCH_VISIT
	/* Bit masks of different branches visits */
	tree->debug_insert_leaf_branches_mask = 0;
	tree->debug_insert_inner_branches_mask = 0;
	tree->debug_delete_leaf_branches_mask = 0;
	tree->debug_delete_inner_branches_mask = 0;
	/* Possible masks of different branches visits */
	tree->debug_insert_leaf_branches_max_mask = (1 << 0xE) - 1;
	tree->debug_insert_inner_branches_max_mask = (1 << 0xE) - 1;
	tree->debug_delete_leaf_branches_max_mask = (1 << 0x11) - 1;
	tree->debug_delete_inner_branches_max_mask = (1 << 0x11) - 1;
#endif
}

/**
 * @brief Fills a new (asserted) tree with values from sorted array.
 *  Elements are copied from the array. Array is not checked to be sorted!
 * @param tree - pointer to a tree
 * @param sorted_array - pointer to the sorted array
 * @param array_size - size of the array (count of elements)
 * @return 0 on success, -1 on memory error
 */
static inline int
bps_tree_build(struct bps_tree *tree, bps_tree_elem_t *sorted_array,
	       size_t array_size)
{
	assert(tree->size == 0);
	assert(tree->root_id == (bps_tree_block_id_t)(-1));
	assert(tree->garbage_head_id == (bps_tree_block_id_t)(-1));
	assert(tree->matras.head.block_count == 0);
	if (array_size == 0)
		return 0;
	bps_tree_block_id_t leaf_count = (array_size +
		BPS_TREE_MAX_COUNT_IN_LEAF - 1) / BPS_TREE_MAX_COUNT_IN_LEAF;

	bps_tree_block_id_t depth = 1;
	bps_tree_block_id_t level_count = leaf_count;
	while (level_count > 1) {
		level_count = (level_count + BPS_TREE_MAX_COUNT_IN_INNER - 1)
			      / BPS_TREE_MAX_COUNT_IN_INNER;
		depth++;
	}

	/* Initializing by {0} to suppress compile warnings (gh-1287) */
	bps_tree_block_id_t level_block_count[BPS_TREE_MAX_DEPTH] = {0};
	bps_tree_block_id_t level_child_count[BPS_TREE_MAX_DEPTH] = {0};
	struct bps_inner *parents[BPS_TREE_MAX_DEPTH];
	level_count = leaf_count;
	for (bps_tree_block_id_t i = 0; i < depth - 1; i++) {
		level_child_count[i] = level_count;
		level_count = (level_count + BPS_TREE_MAX_COUNT_IN_INNER - 1)
			      / BPS_TREE_MAX_COUNT_IN_INNER;
		level_block_count[i] = level_count;
		parents[i] = 0;
	}

	bps_tree_block_id_t leaf_left = leaf_count;
	size_t elems_left = array_size;
	bps_tree_elem_t *current = sorted_array;
	struct bps_leaf *leaf = 0;
	bps_tree_block_id_t prev_leaf_id = (bps_tree_block_id_t)-1;
	bps_tree_block_id_t first_leaf_id = (bps_tree_block_id_t)-1;
	bps_tree_block_id_t last_leaf_id = (bps_tree_block_id_t)-1;
	bps_tree_block_id_t inner_count = 0;
	bps_tree_block_id_t root_if_inner_id = (bps_tree_block_id_t)-1;
	do {
		bps_tree_block_id_t id;
		struct bps_leaf *new_leaf = (struct bps_leaf *)
			matras_alloc(&tree->matras, &id);
		if (!new_leaf) {
			matras_reset(&tree->matras);
			return -1;
		}
		if (first_leaf_id == (bps_tree_block_id_t)-1)
			first_leaf_id = id;
		last_leaf_id = id;
		if (leaf)
			leaf->next_id = id;

		leaf = new_leaf;
		leaf->header.type = BPS_TREE_BT_LEAF;
		leaf->header.size = elems_left / leaf_left;
		leaf->prev_id = prev_leaf_id;
		prev_leaf_id = id;
		memmove(leaf->elems, current,
			leaf->header.size * sizeof(*current));

		bps_tree_block_id_t insert_id = id;
		for (bps_tree_block_id_t i = 0; i < depth - 1; i++) {
			bps_tree_block_id_t new_id = (bps_tree_block_id_t)-1;
			if (!parents[i]) {
				parents[i] = (struct bps_inner *)
					matras_alloc(&tree->matras, &new_id);
				if (!parents[i]) {
					matras_reset(&tree->matras);
					return -1;
				}
				parents[i]->header.type = BPS_TREE_BT_INNER;
				parents[i]->header.size = 0;
				inner_count++;
			}
			parents[i]->child_ids[parents[i]->header.size] =
				insert_id;
			if (new_id == (bps_tree_block_id_t)-1)
				break;
			if (i == depth - 2) {
				root_if_inner_id = new_id;
			} else {
				insert_id = new_id;
			}
		}

		bps_tree_elem_t insert_value = current[leaf->header.size - 1];
		for (bps_tree_block_id_t i = 0; i < depth - 1; i++) {
			parents[i]->header.size++;
			bps_tree_block_id_t max_size = level_child_count[i] /
						       level_block_count[i];
			if ((uint32_t)parents[i]->header.size != max_size) {
				parents[i]->elems[parents[i]->header.size - 1] =
					insert_value;
				break;
			} else {
				parents[i] = 0;
				level_child_count[i] -= max_size;
				level_block_count[i]--;
			}
		}

		leaf_left--;
		elems_left -= leaf->header.size;
		current += leaf->header.size;
	} while (leaf_left);
	leaf->next_id = (bps_tree_block_id_t)-1;

	assert(elems_left == 0);
	for (bps_tree_block_id_t i = 0; i < depth - 1; i++) {
		assert(level_child_count[i] == 0);
		assert(level_block_count[i] == 0);
		assert(parents[i] == 0);
	}

	tree->first_id = first_leaf_id;
	tree->last_id = last_leaf_id;
	tree->leaf_count = leaf_count;
	tree->inner_count = inner_count;
	tree->depth = depth;
	tree->size = array_size;
	tree->max_elem = sorted_array[array_size - 1];
	if (depth == 1) {
		tree->root_id = first_leaf_id;
	} else {
		tree->root_id = root_if_inner_id;
	}
	return 0;
}

/**
 * @brief Tree destruction. Frees allocated memory.
 * @param tree - pointer to a tree
 */
static inline void
bps_tree_destroy(struct bps_tree *tree)
{
	matras_destroy(&tree->matras);
}

/**
 * @brief Get size of tree, i.e. count of elements in tree
 * @param tree - pointer to a tree
 * @return - count count of elements in tree
 */
static inline size_t
bps_tree_size(const struct bps_tree *tree)
{
	return tree->size;
}

/**
 * @brief Get amount of memory in bytes that the tree is using
 *  (not including sizeof(struct bps_tree))
 * @param tree - pointer to a tree
 * @return - count count of elements in tree
 */
static inline size_t
bps_tree_mem_used(const struct bps_tree *tree)
{
	size_t res = matras_extent_count(&tree->matras);
	res *= BPS_TREE_EXTENT_SIZE;
	return res;
}

/**
 * @brief Get a pointer to block by it's ID.
 */
static inline struct bps_block *
bps_tree_restore_block(const struct bps_tree *tree, bps_tree_block_id_t id)
{
	return (struct bps_block *)matras_get(&tree->matras, id);
}

/**
 * @brief Get a pointer to block by it's ID and provided read view.
 */
static inline struct bps_block *
bps_tree_restore_block_ver(const struct bps_tree *tree, bps_tree_block_id_t id,
			   struct matras_view *view)
{
	return (struct bps_block *)matras_view_get(&tree->matras, view, id);
}

/**
 * @brief Get a pointer to block by it's ID.
 */
static inline struct bps_block *
bps_tree_root(const struct bps_tree *tree)
{
	return (struct bps_block *)matras_get(&tree->matras, tree->root_id);
}

/**
 * @brief Get a pointer to block by it's ID.
 */
static inline struct bps_block *
bps_tree_touch_block(struct bps_tree *tree, bps_tree_block_id_t id)
{
	return (struct bps_block *)matras_touch(&tree->matras, id);
}

/**
 * @brief Get a random element in a tree.
 * @param tree - pointer to a tree
 * @param rnd - some random value
 * @return - count count of elements in tree
 */
static inline bps_tree_elem_t *
bps_tree_random(const struct bps_tree *tree, size_t rnd)
{
	if (tree->root_id == (bps_tree_block_id_t)(-1))
		return 0;

	struct bps_block *block = bps_tree_root(tree);

	for (bps_tree_block_id_t i = 0; i < tree->depth - 1; i++) {
		struct bps_inner *inner = (struct bps_inner *)block;
		bps_tree_pos_t pos = rnd % inner->header.size;
		rnd /= inner->header.size;
		block = bps_tree_restore_block(tree, inner->child_ids[pos]);
	}

	struct bps_leaf *leaf = (struct bps_leaf *)block;
	bps_tree_pos_t pos = rnd % leaf->header.size;
	return leaf->elems + pos;
}

/**
 * @brief Find the lowest element in sorted array that is >= than the key
 * @param tree - pointer to a tree
 * @param arr - array of elements
 * @param size - size of the array
 * @param key - key to find
 * @param exact - point to bool that receives true if equal element was found
 */
static inline bps_tree_pos_t
bps_tree_find_ins_point_key(const struct bps_tree *tree, bps_tree_elem_t *arr,
			    size_t size, bps_tree_key_t key, bool *exact)
{
	(void)tree;
	bps_tree_elem_t *begin = arr;
	bps_tree_elem_t *end = arr + size;
	*exact = false;
#ifdef BPS_BLOCK_LINEAR_SEARCH
	while (begin != end) {
		int res = BPS_TREE_COMPARE_KEY(*begin, key, tree->arg);
		if (res >= 0) {
			*exact = res == 0;
			return (bps_tree_pos_t)(begin - arr);
		}
		++begin;
	}
	return (bps_tree_pos_t)(begin - arr);
#else
	while (begin != end) {
		bps_tree_elem_t *mid = begin + (end - begin) / 2;
		int res = BPS_TREE_COMPARE_KEY(*mid, key, tree->arg);
		if (res > 0) {
			end = mid;
		} else if (res < 0) {
			begin = mid + 1;
		} else {
			*exact = true;
			end = mid;
			/* Equal found, continue search for lowest equal */
		}
	}
	return (bps_tree_pos_t)(end - arr);
#endif
}

/**
 * @brief Find the lowest element in sorted array that is >= than the elem
 * @param tree - pointer to a tree
 * @param arr - array of elements
 * @param size - size of the array
 * @param elem - element to find
 * @param exact - point to bool that receives true if equal
 * element was found
 */
static inline bps_tree_pos_t
bps_tree_find_ins_point_elem(const struct bps_tree *tree, bps_tree_elem_t *arr,
			     size_t size, bps_tree_elem_t elem, bool *exact)
{
	(void)tree;
	bps_tree_elem_t *begin = arr;
	bps_tree_elem_t *end = arr + size;
	*exact = false;
#ifdef BPS_BLOCK_LINEAR_SEARCH
	while (begin != end) {
		int res = BPS_TREE_COMPARE(*begin, elem, tree->arg);
		if (res >= 0) {
			*exact = res == 0;
			return (bps_tree_pos_t)(begin - arr);
		}
		++begin;
	}
	return (bps_tree_pos_t)(begin - arr);
#else
	while (begin != end) {
		bps_tree_elem_t *mid = begin + (end - begin) / 2;
		int res = BPS_TREE_COMPARE(*mid, elem, tree->arg);
		if (res > 0) {
			end = mid;
		} else if (res < 0) {
			begin = mid + 1;
		} else {
			*exact = true;
			/* Since elements are unique in array, stop search */
			return (bps_tree_pos_t)(mid - arr);
		}
	}
	return (bps_tree_pos_t)(end - arr);
#endif
}

/**
 * @brief Find the lowest element in sorted array that is greater
 * than the key.
 * @param tree - pointer to a tree
 * @param arr - array of elements
 * @param size - size of the array
 * @param key - key to find
 * @param exact - point to bool that receives true if equal
 *                element is present
 */
static inline bps_tree_pos_t
bps_tree_find_after_ins_point_key(const struct bps_tree *tree,
				  bps_tree_elem_t *arr, size_t size,
				  bps_tree_key_t key, bool *exact)
{
	(void)tree;
	bps_tree_elem_t *begin = arr;
	bps_tree_elem_t *end = arr + size;
	*exact = false;
#ifdef BPS_BLOCK_LINEAR_SEARCH
	while (begin != end) {
		int res = BPS_TREE_COMPARE_KEY(*begin, key, tree->arg);
		if (res == 0)
			*exact = true;
		else if (res > 0)
			return (bps_tree_pos_t)(begin - arr);
		++begin;
	}
	return (bps_tree_pos_t)(begin - arr);
#else
	while (begin != end) {
		bps_tree_elem_t *mid = begin + (end - begin) / 2;
		int res = BPS_TREE_COMPARE_KEY(*mid, key, tree->arg);
		if (res > 0) {
			end = mid;
		} else if (res < 0) {
			begin = mid + 1;
		} else {
			*exact = true;
			begin = mid + 1;
		}
	}
	return (bps_tree_pos_t)(end - arr);
#endif
}

/**
 * @brief Get an invalid iterator. See iterator description.
 * @return - Invalid iterator
 */
static inline struct bps_tree_iterator
bps_tree_invalid_iterator()
{
	struct bps_tree_iterator res;
	res.block_id = (bps_tree_block_id_t)(-1);
	res.pos = 0;
	matras_head_read_view(&res.view);
	return res;
}

/**
 * @brief Check if an iterator is invalid. See iterator
 * description.
 * @param itr - iterator to check
 * @return - true if iterator is invalid, false otherwise
 */
static inline bool
bps_tree_iterator_is_invalid(struct bps_tree_iterator *itr)
{
	return itr->block_id == (bps_tree_block_id_t)(-1);
}

/**
 * @brief Check for a validity of an iterator and return pointer
 * to the leaf.  Position is also checked an (-1) is converted to
 * position to last element.  If smth is wrong, iterator is
 * invalidated and NULL returned.
 */
static inline struct bps_leaf *
bps_tree_get_leaf_safe(const struct bps_tree *tree,
		       struct bps_tree_iterator *itr)
{
	if (itr->block_id == (bps_tree_block_id_t)(-1))
		return 0;

	struct bps_block *block =
		bps_tree_restore_block_ver(tree, itr->block_id, &itr->view);
	if (block->type == BPS_TREE_BT_GARBAGE) {
		struct bps_garbage *garbage = (struct bps_garbage *)block;
		while (garbage->next_leaf_id != (bps_tree_block_id_t)(-1)
		       && garbage->next_leaf_id != itr->block_id) {
			block = bps_tree_restore_block_ver(tree,
				garbage->next_leaf_id, &itr->view);
			if (block->type == BPS_TREE_BT_LEAF) {
				itr->block_id = garbage->next_leaf_id;
				itr->pos = 0;
				break;
			} else if (block->type != BPS_TREE_BT_GARBAGE) {
				break;
			}
			garbage = (struct bps_garbage *)block;
		}
	}
	if (block->type != BPS_TREE_BT_LEAF) {
		itr->block_id = (bps_tree_block_id_t)(-1);
		return 0;
	}
	if (itr->pos == (bps_tree_pos_t)(-1)) {
		itr->pos = block->size - 1;
	} else if (itr->pos >= block->size) {
		struct bps_leaf *leaf = (struct bps_leaf *)block;
		if (leaf->next_id == (bps_tree_block_id_t)(-1)) {
			itr->block_id = (bps_tree_block_id_t)(-1);
			return 0;
		}
		itr->block_id = leaf->next_id;
		itr->pos = 0;
		block = bps_tree_restore_block_ver(tree, itr->block_id,
						   &itr->view);
	}
	return (struct bps_leaf *)block;
}

/**
 * @brief Compare two iterators and return true if trey point to
 * the same element.
 * Two invalid iterators are equal and point to the same nowhere.
 * A broken iterator is possibly not equal to any valid or invalid
 * iterators.
 * @param tree - pointer to a tree
 * @param itr1 - first iterator
 * @param itr2 - second iterator
 * @return - true if iterators are equal, false otherwise
 */
static inline bool
bps_tree_iterator_are_equal(const struct bps_tree *tree,
		            struct bps_tree_iterator *itr1,
		            struct bps_tree_iterator *itr2)
{
	if (bps_tree_iterator_is_invalid(itr1) && bps_tree_iterator_is_invalid(itr2))
		return true;
	if (bps_tree_iterator_is_invalid(itr1) || bps_tree_iterator_is_invalid(itr2))
		return false;
	if (itr1->block_id == itr2->block_id && itr1->pos == itr2->pos)
		return true;
	if (itr1->pos == (bps_tree_pos_t)(-1)) {
		struct bps_leaf *leaf = bps_tree_get_leaf_safe(tree, itr1);
		if (!leaf)
			return false;
		itr1->pos = leaf->header.size - 1;
		if (itr1->block_id == itr2->block_id && itr1->pos == itr2->pos)
			return true;
	}
	if (itr2->pos == (bps_tree_pos_t)(-1)) {
		struct bps_leaf *leaf = bps_tree_get_leaf_safe(tree, itr2);
		if (!leaf)
			return false;
		itr2->pos = leaf->header.size - 1;
		if (itr1->block_id == itr2->block_id && itr1->pos == itr2->pos)
			return true;
	}
	return false;
}

/**
 * @brief Get an iterator to the first element of the tree
 * @param tree - pointer to a tree
 * @return - First iterator. Could be invalid if the tree is empty.
 */
static inline struct bps_tree_iterator
bps_tree_iterator_first(const struct bps_tree *tree)
{
	struct bps_tree_iterator itr;
	itr.block_id = tree->first_id;
	itr.pos = 0;
	matras_head_read_view(&itr.view);
	return itr;
}

/**
 * @brief Get an iterator to the last element of the tree.
 * @param tree - pointer to a tree
 * @return - Last iterator. Could be invalid if the tree is empty.
 */
static inline struct bps_tree_iterator
bps_tree_iterator_last(const struct bps_tree *tree)
{
	struct bps_tree_iterator itr;
	itr.block_id = tree->last_id;
	itr.pos = (bps_tree_pos_t)(-1);
	matras_head_read_view(&itr.view);
	return itr;
}

/**
 * @brief Get an iterator to the first element that is greater
 * than or equal to the key.
 * @param tree - pointer to a tree
 * @param key - key that will be compared with elements
 * @param exact - pointer to a bool value, that will be set to true if
 *  and element pointed by the iterator is equal to the key, false otherwise
 *  Pass NULL if you don't need that info.
 * @return - Lower-bound iterator. Invalid if all elements are less than key.
 */
static inline struct bps_tree_iterator
bps_tree_lower_bound(const struct bps_tree *tree, bps_tree_key_t key,
		     bool *exact)
{
	struct bps_tree_iterator res;
	matras_head_read_view(&res.view);
	bool local_result;
	if (!exact)
		exact = &local_result;
	*exact = false;
	if (tree->root_id == (bps_tree_block_id_t)(-1)) {
		res.block_id = (bps_tree_block_id_t)(-1);
		res.pos = 0;
		return res;
	}
	struct bps_block *block = bps_tree_root(tree);
	bps_tree_block_id_t block_id = tree->root_id;
	for (bps_tree_block_id_t i = 0; i < tree->depth - 1; i++) {
		struct bps_inner *inner = (struct bps_inner *)block;
		bps_tree_pos_t pos;
		pos = bps_tree_find_ins_point_key(tree, inner->elems,
						  inner->header.size - 1,
						  key, exact);
		block_id = inner->child_ids[pos];
		block = bps_tree_restore_block(tree, block_id);
	}

	struct bps_leaf *leaf = (struct bps_leaf *)block;
	bps_tree_pos_t pos;
	pos = bps_tree_find_ins_point_key(tree, leaf->elems, leaf->header.size,
					  key, exact);
	if (pos >= leaf->header.size) {
		res.block_id = leaf->next_id;
		res.pos = 0;
	} else {
		res.block_id = block_id;
		res.pos = pos;
	}
	return res;
}

/**
 * @brief Get an iterator to the first element that is greater than key
 * @param tree - pointer to a tree
 * @param key - key that will be compared with elements
 * @param exact - pointer to a bool value, that will be set to true if
 *  and element pointed by the (!)previous iterator is equal to the key,
 *  false otherwise. Pass NULL if you don't need that info.
 * @return - Upper-bound iterator. Invalid if all elements are less or equal
 *  than the key.
 */
static inline struct bps_tree_iterator
bps_tree_upper_bound(const struct bps_tree *tree, bps_tree_key_t key,
		     bool *exact)
{
	struct bps_tree_iterator res;
	matras_head_read_view(&res.view);
	bool local_result;
	if (!exact)
		exact = &local_result;
	*exact = false;
	bool exact_test;
	if (tree->root_id == (bps_tree_block_id_t)(-1)) {
		res.block_id = (bps_tree_block_id_t)(-1);
		res.pos = 0;
		return res;
	}
	struct bps_block *block = bps_tree_root(tree);
	bps_tree_block_id_t block_id = tree->root_id;
	for (bps_tree_block_id_t i = 0; i < tree->depth - 1; i++) {
		struct bps_inner *inner = (struct bps_inner *)block;
		bps_tree_pos_t pos;
		pos = bps_tree_find_after_ins_point_key(tree, inner->elems,
							inner->header.size - 1,
							key, &exact_test);
		if (exact_test)
			*exact = true;
		block_id = inner->child_ids[pos];
		block = bps_tree_restore_block(tree, block_id);
	}

	struct bps_leaf *leaf = (struct bps_leaf *)block;
	bps_tree_pos_t pos;
	pos = bps_tree_find_after_ins_point_key(tree, leaf->elems,
						leaf->header.size,
						key, &exact_test);
	if (exact_test)
		*exact = true;
	if (pos >= leaf->header.size) {
		res.block_id = leaf->next_id;
		res.pos = 0;
	} else {
		res.block_id = block_id;
		res.pos = pos;
	}
	return res;
}

/**
 * @brief Get approximate number of entries that are equal to given key.
 * Accuracy limits:
 * If the result is less than BPS_TREE_name_MAX_COUNT_IN_LEAF * 5 / 6, the
 * result is precise. If not, let's define:
 * X = BPS_TREE_name_MAX_COUNT_IN_LEAF * 5 / 6
 * Y = BPS_TREE_name_MAX_COUNT_IN_INNER * 5 / 6
 * H = ceil(log(Result / X) / log(Y))
 * Then the true count is between:
 * [ Result * pow(0.8 - z, H), Result * pow(1.2 + z, H) ]
 * where z parameter is a small number due to rounding errors
 * @param tree - pointer to a tree
 * @param key - key that will be compared with elements
 * @return - approximate number of entries that are equal to given key.
 */
static inline size_t
bps_tree_approximate_count(const struct bps_tree *tree, bps_tree_key_t key)
{
	if (tree->root_id == (bps_tree_block_id_t)(-1))
		return 0;

	size_t result = 0;
	bool exact;
	struct bps_block *lower_block = bps_tree_root(tree);
	struct bps_block *upper_block = bps_tree_root(tree);
	for (bps_tree_block_id_t i = 1; i < tree->depth; i++) {
		/* average occupancy in B+* block is 5/6 */
		result *= BPS_TREE_MAX_COUNT_IN_INNER * 5 / 6;

		struct bps_inner *lower_inner = (struct bps_inner *)lower_block;
		bps_tree_pos_t lower_pos =
			bps_tree_find_ins_point_key(tree, lower_inner->elems,
						    lower_inner->header.size - 1,
						    key, &exact);
		struct bps_inner *upper_inner = (struct bps_inner *)upper_block;
		bps_tree_pos_t upper_pos =
			bps_tree_find_after_ins_point_key(tree,
							  upper_inner->elems,
							  upper_inner->header.size - 1,
							  key, &exact);

		if (lower_inner == upper_inner) {
			if (upper_pos > lower_pos)
				result += upper_pos - lower_pos - 1;
		} else {
			result += lower_inner->header.size - 1 - lower_pos;
			result += upper_pos;
		}

		bps_tree_block_id_t lower_block_id =
			lower_inner->child_ids[lower_pos];
		lower_block = bps_tree_restore_block(tree, lower_block_id);
		bps_tree_block_id_t upper_block_id =
			upper_inner->child_ids[upper_pos];
		upper_block = bps_tree_restore_block(tree, upper_block_id);
	}

	/* average occupancy in B+* block is 5/6 */
	result *= BPS_TREE_MAX_COUNT_IN_LEAF * 5 / 6;
	struct bps_leaf *lower_leaf = (struct bps_leaf *)lower_block;
	bps_tree_pos_t lower_pos =
		bps_tree_find_ins_point_key(tree, lower_leaf->elems,
					    lower_leaf->header.size,
					    key, &exact);

	struct bps_leaf *upper_leaf = (struct bps_leaf *)upper_block;
	bps_tree_pos_t upper_pos =
		bps_tree_find_after_ins_point_key(tree, upper_leaf->elems,
						  upper_leaf->header.size,
						  key, &exact);

	if (lower_leaf == upper_leaf) {
		result += upper_pos - lower_pos;
	} else {
		result += lower_leaf->header.size - 1 - lower_pos;
		result += upper_pos;
		result++;
	}

	return result;
}

/**
 * @brief Get a pointer to the element pointed by iterator.
 *  If iterator is detected as broken, it is invalidated and NULL returned.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 * @return - Pointer to the element. Null for invalid iterator
 */
static inline bps_tree_elem_t *
bps_tree_iterator_get_elem(const struct bps_tree *tree,
		           struct bps_tree_iterator *itr)
{
	struct bps_leaf *leaf = bps_tree_get_leaf_safe(tree, itr);
	if (!leaf)
		return 0;
	return leaf->elems + itr->pos;
}

/**
 * @brief Increments an iterator, makes it point to the next element
 *  If the iterator is to last element, it will be invalidated
 *  If the iterator is detected as broken, it will be invalidated.
 *  If the iterator is invalid, then it will be set to first element.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 * @return - true on success, false if a resulted iterator is set to invalid
 */
static inline bool
bps_tree_iterator_next(const struct bps_tree *tree, struct bps_tree_iterator *itr)
{
	if (itr->block_id == (bps_tree_block_id_t)(-1)) {
		if (matras_is_read_view_created(&itr->view))
			return false;
		itr->block_id = tree->first_id;
		itr->pos = 0;
		return itr->block_id != (bps_tree_block_id_t)(-1);
	}
	struct bps_leaf *leaf = bps_tree_get_leaf_safe(tree, itr);
	if (!leaf)
		return false;
	itr->pos++;
	if (itr->pos >= leaf->header.size) {
		itr->block_id = leaf->next_id;
		itr->pos = 0;
		return itr->block_id != (bps_tree_block_id_t)(-1);
	}
	return true;
}

/**
 * @brief Decrements an iterator, makes it point to the previous element
 *  If the iterator is to first element, it will be invalidated
 *  If the iterator is detected as broken, it will be invalidated.
 *  If the iterator is invalid, then it will be set to last element.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 * @return - true on success, false if a resulted iterator is set to invalid
 */
static inline bool
bps_tree_iterator_prev(const struct bps_tree *tree, struct bps_tree_iterator *itr)
{
	if (itr->block_id == (bps_tree_block_id_t)(-1)) {
		if (matras_is_read_view_created(&itr->view))
			return false;
		itr->block_id = tree->last_id;
		itr->pos = (bps_tree_pos_t)(-1);
		return itr->block_id != (bps_tree_block_id_t)(-1);
	}
	struct bps_leaf *leaf = bps_tree_get_leaf_safe(tree, itr);
	if (!leaf)
		return false;
	if (itr->pos == 0) {
		itr->block_id = leaf->prev_id;
		itr->pos = (bps_tree_pos_t)(-1);
		return itr->block_id != (bps_tree_block_id_t)(-1);
	} else {
		itr->pos--;
	}
	return true;
}

/**
 * @brief Freezes tree state for given iterator. All following tree modification
 * will not apply to that iterator iteration. That iterator should be destroyed
 * with a bps_tree_iterator_destroy call after usage.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 */
static inline void
bps_tree_iterator_freeze(struct bps_tree *tree, struct bps_tree_iterator *itr)
{
	assert(!matras_is_read_view_created(&itr->view));
	matras_create_read_view(&tree->matras, &itr->view);
}

/**
 * @brief Destroy an iterator that was frozen before. Useless for not frozen
 * iterators.
 * @param tree - pointer to a tree
 * @param itr - pointer to tree iterator
 */
static inline void
bps_tree_iterator_destroy(struct bps_tree *tree, struct bps_tree_iterator *itr)
{
	matras_destroy_read_view(&tree->matras, &itr->view);
}

/**
 * @brief Find the first element that is equal to the key (comparator returns 0)
 * @param tree - pointer to a tree
 * @param key - key that will be compared with elements
 * @return pointer to the first equal element or NULL if not found
 */
static inline bps_tree_elem_t *
bps_tree_find(const struct bps_tree *tree, bps_tree_key_t key)
{
	if (tree->root_id == (bps_tree_block_id_t)(-1))
		return 0;
	struct bps_block *block = bps_tree_root(tree);
	bool exact = false;
	for (bps_tree_block_id_t i = 0; i < tree->depth - 1; i++) {
		struct bps_inner *inner = (struct bps_inner *)block;
		bps_tree_pos_t pos;
		pos = bps_tree_find_ins_point_key(tree, inner->elems,
						  inner->header.size - 1,
						  key, &exact);
		block = bps_tree_restore_block(tree, inner->child_ids[pos]);
	}

	struct bps_leaf *leaf = (struct bps_leaf *)block;
	bps_tree_pos_t pos;
	pos = bps_tree_find_ins_point_key(tree, leaf->elems, leaf->header.size,
					  key, &exact);
	if (exact)
		return leaf->elems + pos;
	else
		return 0;
}

/**
 * @brief Add a block to the garbage for future reuse
 */
static inline void
bps_tree_garbage_push(struct bps_tree *tree, struct bps_block *block,
		      bps_tree_block_id_t id)
{
	assert(block); (void) block;
	bps_tree_block_id_t next_leaf_id = (bps_tree_block_id_t)(-1);
	bps_tree_block_id_t prev_leaf_id = (bps_tree_block_id_t)(-1);
	if (block->type == BPS_TREE_BT_LEAF) {
		struct bps_leaf *leaf = (struct bps_leaf *)block;
		next_leaf_id = leaf->next_id;
		prev_leaf_id = leaf->prev_id;
	}

	struct bps_garbage *garbage = (struct bps_garbage *)
		bps_tree_touch_block(tree, id);
	garbage->header.type = BPS_TREE_BT_GARBAGE;
	garbage->next_id = tree->garbage_head_id;
	garbage->next_leaf_id = next_leaf_id;
	garbage->prev_leaf_id = prev_leaf_id;
	tree->garbage_head_id = id;
	tree->garbage_count++;
}

/**
 * @brief Reclaim a block from the garbage for reuse
 */
static inline struct bps_block *
bps_tree_garbage_pop(struct bps_tree *tree, bps_tree_block_id_t *id)
{
	if (tree->garbage_head_id != (bps_tree_block_id_t)(-1)) {
		*id = tree->garbage_head_id;
		struct bps_garbage *result = (struct bps_garbage *)
		bps_tree_touch_block(tree, tree->garbage_head_id);
		tree->garbage_head_id = result->next_id;
		tree->garbage_count--;
		return (struct bps_block *) result;
	} else {
		return 0;
	}
}

/**
 * @brief Reclaim from garbage of create new block and convert it to leaf
 */
static inline struct bps_leaf *
bps_tree_create_leaf(struct bps_tree *tree, bps_tree_block_id_t *id)
{
	struct bps_leaf *res = (struct bps_leaf *)
			       bps_tree_garbage_pop(tree, id);
	if (!res)
		res = (struct bps_leaf *)matras_alloc(&tree->matras, id);
	res->header.type = BPS_TREE_BT_LEAF;
	tree->leaf_count++;
	return res;
}

/**
 * @brief Reclaim from garbage of create new block and convert it to inner
 */
static inline struct bps_inner *
bps_tree_create_inner(struct bps_tree *tree, bps_tree_block_id_t *id)
{
	struct bps_inner *res = (struct bps_inner *)
				bps_tree_garbage_pop(tree, id);
	if (!res)
		res = (struct bps_inner *)matras_alloc(&tree->matras, id);
	res->header.type = BPS_TREE_BT_INNER;
	tree->inner_count++;
	return res;
}

/**
 * @brief Dispose leaf block (to garbage and decrement counter)
 */
static inline void
bps_tree_dispose_leaf(struct bps_tree *tree, struct bps_leaf *leaf,
		      bps_tree_block_id_t id)
{
	tree->leaf_count--;
	bps_tree_garbage_push(tree, (struct bps_block *)leaf, id);
}

/**
 * @brief Dispose inner block (to garbage and decrement counter)
 */
static inline void
bps_tree_dispose_inner(struct bps_tree *tree, struct bps_inner *inner,
		       bps_tree_block_id_t id)
{
	tree->inner_count--;
	bps_tree_garbage_push(tree, (struct bps_block *)inner, id);
}

/**
 * @brief Reserve a number of block, return false if failed.
 */
static inline bool
bps_tree_reserve_blocks(struct bps_tree *tree, bps_tree_block_id_t count)
{
	while (tree->garbage_count < count) {
		bps_tree_block_id_t id;
		struct bps_block *block = (struct bps_block *)
			matras_alloc(&tree->matras, &id);
		if (!block)
			return false;
		block->type = BPS_TREE_BT_GARBAGE;
		bps_tree_garbage_push(tree, block, id);
	}
	return true;
}

/**
 * @brief Insert first element to and empty tree.
 */
static inline int
bps_tree_insert_first_elem(struct bps_tree *tree, bps_tree_elem_t new_elem)
{
	assert(tree->depth == 0);
	assert(tree->size == 0);
	assert(tree->leaf_count == 0);
	tree->max_elem = new_elem;
	struct bps_leaf *leaf = bps_tree_create_leaf(tree, &tree->root_id);
	if (!leaf)
		return -1;
	leaf->header.size = 1;
	leaf->elems[0] = new_elem;
	tree->first_id = tree->root_id;
	tree->last_id = tree->root_id;
	leaf->prev_id = (bps_tree_block_id_t)(-1);
	leaf->next_id = (bps_tree_block_id_t)(-1);
	tree->depth = 1;
	tree->size = 1;
	return 0;
}

/**
 * @brief Collect path to an element or to the place where it can be inserted
 */
static inline void
bps_tree_collect_path(struct bps_tree *tree, bps_tree_elem_t new_elem,
		      struct bps_inner_path_elem *path,
		      struct bps_leaf_path_elem *leaf_path_elem, bool *exact)
{
	*exact = false;

	struct bps_inner_path_elem *prev_ext = 0;
	bps_tree_pos_t prev_pos = 0;
	struct bps_block *block = bps_tree_root(tree);
	bps_tree_block_id_t block_id = tree->root_id;
	bps_tree_elem_t *max_elem_copy = &tree->max_elem;
	bps_tree_block_id_t max_elem_block_id = (bps_tree_block_id_t)-1;
	bps_tree_pos_t max_elem_pos = (bps_tree_pos_t)-1;
	for (bps_tree_block_id_t i = 0; i < tree->depth - 1; i++) {
		struct bps_inner *inner = (struct bps_inner *)block;
		bps_tree_pos_t pos;
		if (*exact)
			pos = inner->header.size - 1;
		else
			pos = bps_tree_find_ins_point_elem(tree, inner->elems,
							   inner->header.size - 1,
							   new_elem, exact);

		path[i].block = inner;
		path[i].block_id = block_id;
		path[i].insertion_point = pos;
		path[i].pos_in_parent = prev_pos;
		path[i].parent = prev_ext;
		path[i].max_elem_copy = max_elem_copy;
		path[i].max_elem_block_id = max_elem_block_id;
		path[i].max_elem_pos = max_elem_pos;

		if (pos < inner->header.size - 1) {
			max_elem_copy = inner->elems + pos;
			max_elem_block_id = block_id;
			max_elem_pos = pos;
		}
		block_id = inner->child_ids[pos];
		block = bps_tree_restore_block(tree, block_id);
		prev_pos = pos;
		prev_ext = path + i;
	}

	struct bps_leaf *leaf = (struct bps_leaf *)block;
	bps_tree_pos_t pos;
	if (*exact)
		pos = leaf->header.size - 1;
	else
		pos = bps_tree_find_ins_point_elem(tree, leaf->elems,
						   leaf->header.size,
						   new_elem, exact);

	leaf_path_elem->block = leaf;
	leaf_path_elem->block_id = block_id;
	leaf_path_elem->insertion_point = pos;
	leaf_path_elem->pos_in_parent = prev_pos;
	leaf_path_elem->parent = prev_ext;
	leaf_path_elem->max_elem_copy = max_elem_copy;
	leaf_path_elem->max_elem_block_id = max_elem_block_id;
	leaf_path_elem->max_elem_pos = max_elem_pos;
}

/**
 * @brief Get new COW link to max_elem_copy member of leaf path
 */
static inline void
bps_tree_touch_leaf_path_max_elem(struct bps_tree *tree,
				  struct bps_leaf_path_elem *leaf_path_elem)
{
	if (leaf_path_elem->max_elem_block_id == (bps_tree_block_id_t)-1)
		return;
	struct bps_inner *holder = (struct bps_inner *)
		bps_tree_touch_block(tree, leaf_path_elem->max_elem_block_id);
	leaf_path_elem->max_elem_copy =
		holder->elems + leaf_path_elem->max_elem_pos;
}

/**
 * @brief Get new COW link to blocks and max_elem_copys
 */
static inline void
bps_tree_touch_path(struct bps_tree *tree,
			     struct bps_leaf_path_elem *leaf_path_elem)
{
	bps_tree_touch_leaf_path_max_elem(tree, leaf_path_elem);
	for (struct bps_inner_path_elem *path = leaf_path_elem->parent;
	     path; path = path->parent) {
		path->block = (struct bps_inner *)
			bps_tree_touch_block(tree, path->block_id);
		if (path->max_elem_block_id == (bps_tree_block_id_t)-1)
			continue;
		struct bps_inner *holder = (struct bps_inner *)
			bps_tree_touch_block(tree, path->max_elem_block_id);
		path->max_elem_copy = holder->elems + path->max_elem_pos;
	}
}

/**
 * @brief Replace element by it's path and fill the *replaced argument
 */
static inline bool
bps_tree_process_replace(struct bps_tree *tree,
			 struct bps_leaf_path_elem *leaf_path_elem,
			 bps_tree_elem_t new_elem, bps_tree_elem_t *replaced)
{
	leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, leaf_path_elem->block_id);
	struct bps_leaf *leaf = leaf_path_elem->block;
	assert(leaf_path_elem->insertion_point < leaf->header.size);

	if (replaced)
		*replaced = leaf->elems[leaf_path_elem->insertion_point];

	leaf->elems[leaf_path_elem->insertion_point] = new_elem;
	if (leaf_path_elem->insertion_point == leaf->header.size - 1) {
		bps_tree_touch_leaf_path_max_elem(tree, leaf_path_elem);
		*leaf_path_elem->max_elem_copy =
			leaf->elems[leaf->header.size - 1];
	}
	return true;
}

#ifndef NDEBUG
/**
 * @brief Debug memmove, checks for overflow
 */
static inline void
bps_tree_debug_memmove(void *dst_arg, void *src_arg, size_t num,
		       void *dst_block_arg, void *src_block_arg)
{
	char *dst = (char *)dst_arg;
	char *src = (char *)src_arg;
	struct bps_block *dst_block = (struct bps_block *)dst_block_arg;
	struct bps_block *src_block = (struct bps_block *)src_block_arg;
	(void) dst_block;
	(void) src_block;

	assert(dst_block->type == src_block->type);
	assert(dst_block->type == BPS_TREE_BT_LEAF ||
	       dst_block->type == BPS_TREE_BT_INNER);
	if (dst_block->type == BPS_TREE_BT_LEAF) {
		struct bps_leaf *dst_leaf = (struct bps_leaf *)dst_block_arg;
		struct bps_leaf *src_leaf = (struct bps_leaf *)src_block_arg;
		(void) dst_leaf;
		(void) src_leaf;
		if (num) {
			assert(dst >= ((char *)dst_leaf->elems));
			assert(dst < ((char *)dst_leaf->elems) +
			       BPS_TREE_MAX_COUNT_IN_LEAF *
			       sizeof(bps_tree_elem_t));
			assert(src >= (char *)src_leaf->elems);
			assert(src < ((char *)src_leaf->elems) +
			       BPS_TREE_MAX_COUNT_IN_LEAF *
			       sizeof(bps_tree_elem_t));
		} else {
			assert(dst >= ((char *)dst_leaf->elems));
			assert(dst <= ((char *)dst_leaf->elems) +
			       BPS_TREE_MAX_COUNT_IN_LEAF *
			       sizeof(bps_tree_elem_t));
			assert(src >= (char *)src_leaf->elems);
			assert(src <= ((char *)src_leaf->elems) +
			       BPS_TREE_MAX_COUNT_IN_LEAF *
			       sizeof(bps_tree_elem_t));
		}
	} else {
		struct bps_inner *dst_inner = (struct bps_inner *)
					      dst_block_arg;
		struct bps_inner *src_inner = (struct bps_inner *)
					      src_block_arg;
		if (num) {
			if (dst >= ((char *)dst_inner->elems) && dst <
			    ((char *)dst_inner->elems) +
			    (BPS_TREE_MAX_COUNT_IN_INNER - 1) *
			    sizeof(bps_tree_elem_t)) {
				assert(dst >= ((char *)dst_inner->elems));
				assert(dst < ((char *)dst_inner->elems) +
				       (BPS_TREE_MAX_COUNT_IN_INNER - 1) *
				       sizeof(bps_tree_elem_t));
				assert(src >= (char *)src_inner->elems);
				assert(src < ((char *)src_inner->elems) +
				       (BPS_TREE_MAX_COUNT_IN_INNER - 1) *
				       sizeof(bps_tree_elem_t));
			} else {
				assert(dst >= ((char *)dst_inner->child_ids));
				assert(dst < ((char *)dst_inner->child_ids) +
				       BPS_TREE_MAX_COUNT_IN_INNER *
				       sizeof(bps_tree_block_id_t));
				assert(src >= (char *)src_inner->child_ids);
				assert(src < ((char *)src_inner->child_ids) +
				       BPS_TREE_MAX_COUNT_IN_INNER *
				       sizeof(bps_tree_block_id_t));
			}
		} else {
			if (dst >= ((char *)dst_inner->elems)
					&& dst <= ((char *)dst_inner->elems) +
					(BPS_TREE_MAX_COUNT_IN_INNER - 1) *
					sizeof(bps_tree_elem_t)
					&& src >= (char *)src_inner->elems
					&& src <= ((char *)src_inner->elems) +
					(BPS_TREE_MAX_COUNT_IN_INNER - 1) *
					sizeof(bps_tree_elem_t)) {
				/* nothing to do due to if condition */
			} else {
				assert(dst >= ((char *)dst_inner->child_ids));
				assert(dst <= ((char *)dst_inner->child_ids) +
				       BPS_TREE_MAX_COUNT_IN_INNER *
				       sizeof(bps_tree_block_id_t));
				assert(src >= (char *)src_inner->child_ids);
				assert(src <= ((char *)src_inner->child_ids) +
				       BPS_TREE_MAX_COUNT_IN_INNER *
				       sizeof(bps_tree_block_id_t));
			}
		}
	}
	/* oh, useful work at last */
	memmove(dst, src, num);
}
#endif

/**
 * @breif Insert an element into leaf block. There must be enough space.
 */
static inline void
bps_tree_insert_into_leaf(struct bps_tree *tree,
			  struct bps_leaf_path_elem *leaf_path_elem,
			  bps_tree_elem_t new_elem)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1)
		leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, leaf_path_elem->block_id);
	struct bps_leaf *leaf = leaf_path_elem->block;
	bps_tree_pos_t pos = leaf_path_elem->insertion_point;

	assert(pos >= 0);
	assert(pos <= leaf->header.size);
	assert(leaf->header.size < BPS_TREE_MAX_COUNT_IN_LEAF);

	BPS_TREE_DATAMOVE(leaf->elems + pos + 1, leaf->elems + pos,
			  leaf->header.size - pos, leaf, leaf);
	leaf->elems[pos] = new_elem;

	if (pos == leaf->header.size) {
		bps_tree_touch_leaf_path_max_elem(tree, leaf_path_elem);
		*leaf_path_elem->max_elem_copy = leaf->elems[leaf->header.size];
	}
	leaf->header.size++;
	tree->size++;
}

/**
 * @breif Insert a child into inner block. There must be enough space.
 */
static inline void
bps_tree_insert_into_inner(struct bps_tree *tree,
			   struct bps_inner_path_elem *inner_path_elem,
			   bps_tree_block_id_t block_id, bps_tree_pos_t pos,
			   bps_tree_elem_t max_elem)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1)
		inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, inner_path_elem->block_id);
	struct bps_inner *inner = inner_path_elem->block;

	assert(pos >= 0);
	assert(pos <= inner->header.size);
	assert(inner->header.size < BPS_TREE_MAX_COUNT_IN_INNER);

	if (pos < inner->header.size) {
		BPS_TREE_DATAMOVE(inner->elems + pos + 1, inner->elems + pos,
				  inner->header.size - pos - 1, inner, inner);
		inner->elems[pos] = max_elem;
		BPS_TREE_DATAMOVE(inner->child_ids + pos + 1,
				  inner->child_ids + pos,
				  inner->header.size - pos, inner, inner);
	} else {
		if (pos > 0)
			inner->elems[pos - 1] = *inner_path_elem->max_elem_copy;
		*inner_path_elem->max_elem_copy = max_elem;
	}
	inner->child_ids[pos] = block_id;

	inner->header.size++;
}

/**
 * @breif Delete element from leaf block.
 */
static inline void
bps_tree_delete_from_leaf(struct bps_tree *tree,
			  struct bps_leaf_path_elem *leaf_path_elem)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1)
		leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, leaf_path_elem->block_id);
	struct bps_leaf *leaf = leaf_path_elem->block;
	bps_tree_pos_t pos = leaf_path_elem->insertion_point;

	assert(pos >= 0);
	assert(pos < leaf->header.size);

	BPS_TREE_DATAMOVE(leaf->elems + pos, leaf->elems + pos + 1,
			  leaf->header.size - 1 - pos, leaf, leaf);

	leaf->header.size--;

	if (leaf->header.size > 0 && pos == leaf->header.size) {
		bps_tree_touch_leaf_path_max_elem(tree, leaf_path_elem);
		*leaf_path_elem->max_elem_copy =
			leaf->elems[leaf->header.size - 1];
	}

	tree->size--;
}

/**
 * @breif Delete a child from inner block.
 */
static inline void
bps_tree_delete_from_inner(struct bps_tree *tree,
			   struct bps_inner_path_elem *inner_path_elem)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1)
		inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, inner_path_elem->block_id);
	struct bps_inner *inner = inner_path_elem->block;
	bps_tree_pos_t pos = inner_path_elem->insertion_point;

	assert(pos >= 0);
	assert(pos < inner->header.size);

	if (pos < inner->header.size - 1) {
		BPS_TREE_DATAMOVE(inner->elems + pos, inner->elems + pos + 1,
				  inner->header.size - 2 - pos, inner, inner);
		BPS_TREE_DATAMOVE(inner->child_ids + pos,
				  inner->child_ids + pos + 1,
				  inner->header.size - 1 - pos, inner, inner);
	} else if (pos > 0) {
		*inner_path_elem->max_elem_copy = inner->elems[pos - 1];
	}

	inner->header.size--;
}

/**
 * @breif Move a number of elements from left leaf to right leaf
 */
static inline void
bps_tree_move_elems_to_right_leaf(struct bps_tree *tree,
				  struct bps_leaf_path_elem *a_leaf_path_elem,
				  struct bps_leaf_path_elem *b_leaf_path_elem,
				  bps_tree_pos_t num)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1) {
		a_leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, a_leaf_path_elem->block_id);
		b_leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, b_leaf_path_elem->block_id);
	}
	struct bps_leaf *a = a_leaf_path_elem->block;
	struct bps_leaf *b = b_leaf_path_elem->block;
	bool move_all = a->header.size == num;

	assert(num > 0);
	assert(a->header.size >= num);
	assert(b->header.size + num <= BPS_TREE_MAX_COUNT_IN_LEAF);

	BPS_TREE_DATAMOVE(b->elems + num, b->elems, b->header.size, b, b);
	BPS_TREE_DATAMOVE(b->elems, a->elems + a->header.size - num, num,
			  b, a);

	a->header.size -= num;
	b->header.size += num;

	if (!move_all)
		*a_leaf_path_elem->max_elem_copy =
			a->elems[a->header.size - 1];
	*b_leaf_path_elem->max_elem_copy = b->elems[b->header.size - 1];
}

/**
 * @breif Move a number of children from left inner to right inner block
 */
static inline void
bps_tree_move_elems_to_right_inner(struct bps_tree *tree,
				   struct bps_inner_path_elem *a_inner_path_elem,
				   struct bps_inner_path_elem *b_inner_path_elem,
				   bps_tree_pos_t num)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1) {
		a_inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, a_inner_path_elem->block_id);
		b_inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, b_inner_path_elem->block_id);
	}
	struct bps_inner *a = a_inner_path_elem->block;
	struct bps_inner *b = b_inner_path_elem->block;
	bool move_to_empty = b->header.size == 0;
	bool move_all = a->header.size == num;

	assert(num > 0);
	assert(a->header.size >= num);
	assert(b->header.size + num <= BPS_TREE_MAX_COUNT_IN_INNER);

	BPS_TREE_DATAMOVE(b->child_ids + num, b->child_ids,
			  b->header.size, b, b);
	BPS_TREE_DATAMOVE(b->child_ids, a->child_ids + a->header.size - num,
			  num, b, a);

	if (!move_to_empty)
		BPS_TREE_DATAMOVE(b->elems + num, b->elems,
				  b->header.size - 1, b, b);
	BPS_TREE_DATAMOVE(b->elems, a->elems + a->header.size - num,
			  num - 1, b, a);
	if (move_to_empty)
		*b_inner_path_elem->max_elem_copy =
			*a_inner_path_elem->max_elem_copy;
	else
		b->elems[num - 1] = *a_inner_path_elem->max_elem_copy;
	if (!move_all)
		*a_inner_path_elem->max_elem_copy =
			a->elems[a->header.size - num - 1];

	a->header.size -= num;
	b->header.size += num;
}

/**
 * @breif Move a number of elements from right leaf to left leaf
 */
static inline void
bps_tree_move_elems_to_left_leaf(struct bps_tree *tree,
				 struct bps_leaf_path_elem *a_leaf_path_elem,
				 struct bps_leaf_path_elem *b_leaf_path_elem,
				 bps_tree_pos_t num)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1) {
		a_leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, a_leaf_path_elem->block_id);
		b_leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, b_leaf_path_elem->block_id);
	}
	struct bps_leaf *a = a_leaf_path_elem->block;
	struct bps_leaf *b = b_leaf_path_elem->block;

	assert(num > 0);
	assert(b->header.size >= num);
	assert(a->header.size + num <= BPS_TREE_MAX_COUNT_IN_LEAF);

	BPS_TREE_DATAMOVE(a->elems + a->header.size, b->elems, num, a, b);
	BPS_TREE_DATAMOVE(b->elems, b->elems + num, b->header.size - num,
			  b, b);

	a->header.size += num;
	b->header.size -= num;
	*a_leaf_path_elem->max_elem_copy = a->elems[a->header.size - 1];
}

/**
 * @breif Move a number of children from right inner to left inner block
 */
static inline void
bps_tree_move_elems_to_left_inner(struct bps_tree *tree,
				  struct bps_inner_path_elem *a_inner_path_elem,
				  struct bps_inner_path_elem *b_inner_path_elem,
				  bps_tree_pos_t num)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1) {
		a_inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, a_inner_path_elem->block_id);
		b_inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, b_inner_path_elem->block_id);
	}
	struct bps_inner *a = a_inner_path_elem->block;
	struct bps_inner *b = b_inner_path_elem->block;
	bool move_to_empty = a->header.size == 0;
	bool move_all = b->header.size == num;

	assert(num > 0);
	assert(b->header.size >= num);
	assert(a->header.size + num <= BPS_TREE_MAX_COUNT_IN_INNER);

	BPS_TREE_DATAMOVE(a->child_ids + a->header.size, b->child_ids,
			  num, a, b);
	BPS_TREE_DATAMOVE(b->child_ids, b->child_ids + num,
			  b->header.size - num, b, b);

	if (!move_to_empty)
		a->elems[a->header.size - 1] =
			*a_inner_path_elem->max_elem_copy;
	BPS_TREE_DATAMOVE(a->elems + a->header.size, b->elems, num - 1, a, b);
	if (move_all) {
		*a_inner_path_elem->max_elem_copy =
			*b_inner_path_elem->max_elem_copy;
	} else {
		*a_inner_path_elem->max_elem_copy = b->elems[num - 1];
		BPS_TREE_DATAMOVE(b->elems, b->elems + num,
				  b->header.size - num - 1, b, b);
	}

	a->header.size += num;
	b->header.size -= num;
}

/**
 * @breif Insert into leaf and move a number of elements to the right
 * Works like if bps_tree_insert_into_leaf and
 *  bps_tree_move_elems_to_right_leaf was consequentially called,
 *  BUT(!) insertion is allowed into full block, so one can consider
 *  insertion as a virtual insertion into virtual block of greater maximum size
 */
static inline void
bps_tree_insert_and_move_elems_to_right_leaf(struct bps_tree *tree,
		struct bps_leaf_path_elem *a_leaf_path_elem,
		struct bps_leaf_path_elem *b_leaf_path_elem,
		bps_tree_pos_t num, bps_tree_elem_t new_elem)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1) {
		a_leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, a_leaf_path_elem->block_id);
		b_leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, b_leaf_path_elem->block_id);
	}
	struct bps_leaf *a = a_leaf_path_elem->block;
	struct bps_leaf *b = b_leaf_path_elem->block;
	bps_tree_pos_t pos = a_leaf_path_elem->insertion_point;
	bool move_to_empty = b->header.size == 0;
	bool move_all = a->header.size == num - 1;

	assert(num > 0);
	assert(a->header.size >= num - 1);
	assert(b->header.size + num <= BPS_TREE_MAX_COUNT_IN_LEAF);
	assert(pos <= a->header.size);
	assert(pos >= 0);

	BPS_TREE_DATAMOVE(b->elems + num, b->elems, b->header.size, b, b);

	bps_tree_pos_t mid_part_size = a->header.size - pos;
	if (mid_part_size >= num) {
		/* In fact insert to 'a' block */
		BPS_TREE_DATAMOVE(b->elems, a->elems + a->header.size - num,
				  num, b, a);
		BPS_TREE_DATAMOVE(a->elems + pos + 1, a->elems + pos,
				  mid_part_size - num, a, a);
		a->elems[pos] = new_elem;
	} else {
		/* In fact insert to 'b' block */
		bps_tree_pos_t new_pos = num - mid_part_size - 1;/* Can be 0 */
		BPS_TREE_DATAMOVE(b->elems,
				  a->elems + a->header.size - num + 1,
				  new_pos, b, a);
		b->elems[new_pos] = new_elem;
		BPS_TREE_DATAMOVE(b->elems + new_pos + 1, a->elems + pos,
				  mid_part_size, b, a);
	}

	a->header.size -= (num - 1);
	b->header.size += num;
	if (!move_all)
		*a_leaf_path_elem->max_elem_copy =
			a->elems[a->header.size - 1];
	if (move_to_empty)
		*b_leaf_path_elem->max_elem_copy =
			b->elems[b->header.size - 1];
	tree->size++;
}

/**
 * @breif Insert into inner and move a number of children to the right
 * Works like if bps_tree_insert_into_inner and
 *  bps_tree_move_elems_to_right_inner was consequentially called,
 *  BUT(!) insertion is allowed into full block, so one can consider
 *  insertion as a virtual insertion into virtual block of greater maximum size
 */
static inline void
bps_tree_insert_and_move_elems_to_right_inner(struct bps_tree *tree,
		struct bps_inner_path_elem *a_inner_path_elem,
		struct bps_inner_path_elem *b_inner_path_elem,
		bps_tree_pos_t num, bps_tree_block_id_t block_id,
		bps_tree_pos_t pos, bps_tree_elem_t max_elem)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1) {
		a_inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, a_inner_path_elem->block_id);
		b_inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, b_inner_path_elem->block_id);
	}
	struct bps_inner *a = a_inner_path_elem->block;
	struct bps_inner *b = b_inner_path_elem->block;
	bool move_to_empty = b->header.size == 0;
	bool move_all = a->header.size == num - 1;

	assert(num > 0);
	assert(a->header.size >= num - 1);
	assert(b->header.size + num <= BPS_TREE_MAX_COUNT_IN_INNER);
	assert(pos <= a->header.size);
	assert(pos >= 0);

	if (!move_to_empty) {
		BPS_TREE_DATAMOVE(b->child_ids + num, b->child_ids,
				  b->header.size, b, b);
		BPS_TREE_DATAMOVE(b->elems + num, b->elems,
				  b->header.size - 1, b, b);
	}

	bps_tree_pos_t mid_part_size = a->header.size - pos;
	if (mid_part_size > num) {
		/* In fact insert to 'a' block, to the internal position */
		BPS_TREE_DATAMOVE(b->child_ids,
				  a->child_ids + a->header.size - num,
				  num, b, a);
		BPS_TREE_DATAMOVE(a->child_ids + pos + 1, a->child_ids + pos,
				  mid_part_size - num, a, a);
		a->child_ids[pos] = block_id;

		BPS_TREE_DATAMOVE(b->elems, a->elems + a->header.size - num,
				  num - 1, b, a);
		if (move_to_empty)
			*b_inner_path_elem->max_elem_copy =
				*a_inner_path_elem->max_elem_copy;
		else
			b->elems[num - 1] = *a_inner_path_elem->max_elem_copy;

		*a_inner_path_elem->max_elem_copy =
			a->elems[a->header.size - num - 1];
		BPS_TREE_DATAMOVE(a->elems + pos + 1, a->elems + pos,
				  mid_part_size - num - 1, a, a);
		a->elems[pos] = max_elem;
	} else if (mid_part_size == num) {
		/* In fact insert to 'a' block, to the last position */
		BPS_TREE_DATAMOVE(b->child_ids,
				  a->child_ids + a->header.size - num,
				  num, b, a);
		BPS_TREE_DATAMOVE(a->child_ids + pos + 1, a->child_ids + pos,
				  mid_part_size - num, a, a);
		a->child_ids[pos] = block_id;

		BPS_TREE_DATAMOVE(b->elems, a->elems + a->header.size - num,
				  num - 1, b, a);
		if (move_to_empty)
			*b_inner_path_elem->max_elem_copy =
				*a_inner_path_elem->max_elem_copy;
		else
			b->elems[num - 1] = *a_inner_path_elem->max_elem_copy;

		*a_inner_path_elem->max_elem_copy = max_elem;
	} else {
		/* In fact insert to 'b' block */
		bps_tree_pos_t new_pos = num - mid_part_size - 1;/* Can be 0 */
		BPS_TREE_DATAMOVE(b->child_ids,
				  a->child_ids + a->header.size - num + 1,
				  new_pos, b, a);
		b->child_ids[new_pos] = block_id;
		BPS_TREE_DATAMOVE(b->child_ids + new_pos + 1,
				  a->child_ids + pos, mid_part_size, b, a);

		if (pos == a->header.size) {
			/* +1 */
			if (move_to_empty)
				*b_inner_path_elem->max_elem_copy = max_elem;
			else
				b->elems[num - 1] = max_elem;
			if (num > 1) {
				/* +(num - 2) */
				BPS_TREE_DATAMOVE(b->elems,
						  a->elems + a->header.size
						   - num + 1, num - 2, b, a);
				/* +1 */
				b->elems[num - 2] =
					*a_inner_path_elem->max_elem_copy;

				if (!move_all)
					*a_inner_path_elem->max_elem_copy =
						a->elems[a->header.size - num];
			}
		} else {
			assert(num > 1);

			BPS_TREE_DATAMOVE(b->elems,
					  a->elems + a->header.size - num + 1,
					  num - mid_part_size - 1, b, a);
			b->elems[new_pos] = max_elem;
			BPS_TREE_DATAMOVE(b->elems + new_pos + 1,
					  a->elems + pos, mid_part_size - 1, b, a);
			if (move_to_empty)
				*b_inner_path_elem->max_elem_copy =
					*a_inner_path_elem->max_elem_copy;
			else
				b->elems[num - 1] =
					*a_inner_path_elem->max_elem_copy;

			if (!move_all)
				*a_inner_path_elem->max_elem_copy =
					a->elems[a->header.size - num];
		}
	}

	a->header.size -= (num - 1);
	b->header.size += num;
}

/**
 * @breif Insert into leaf and move a number of elements to the left
 * Works like if bps_tree_insert_into_leaf and
 *  bps_tree_move_elems_to_right_left was consequentially called,
 *  BUT(!) insertion is allowed into full block, so one can consider
 *  insertion as a virtual insertion into virtual block of greater maximum size
 */
static inline void
bps_tree_insert_and_move_elems_to_left_leaf(struct bps_tree *tree,
		struct bps_leaf_path_elem *a_leaf_path_elem,
		struct bps_leaf_path_elem *b_leaf_path_elem,
		bps_tree_pos_t num, bps_tree_elem_t new_elem)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1) {
		a_leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, a_leaf_path_elem->block_id);
		b_leaf_path_elem->block = (struct bps_leaf *)
		bps_tree_touch_block(tree, b_leaf_path_elem->block_id);
	}
	struct bps_leaf *a = a_leaf_path_elem->block;
	struct bps_leaf *b = b_leaf_path_elem->block;
	bps_tree_pos_t pos = b_leaf_path_elem->insertion_point;
	bool move_all = b->header.size == num - 1;

	assert(num > 0);
	assert(b->header.size >= num - 1);
	assert(a->header.size + num <= BPS_TREE_MAX_COUNT_IN_LEAF);
	assert(pos >= 0);
	assert(pos <= b->header.size);

	if (pos >= num) {
		/* In fact insert to 'b' block */
		bps_tree_pos_t new_pos = pos - num; /* Can be 0 */
		BPS_TREE_DATAMOVE(a->elems + a->header.size, b->elems,
				  num, a, b);
		BPS_TREE_DATAMOVE(b->elems, b->elems + num, new_pos, b, b);
		b->elems[new_pos] = new_elem;
		BPS_TREE_DATAMOVE(b->elems + new_pos + 1, b->elems + pos,
				  b->header.size - pos, b, b);

	} else {
		/* In fact insert to 'a' block */
		bps_tree_pos_t new_pos = a->header.size + pos; /* Can be 0 */
		BPS_TREE_DATAMOVE(a->elems + a->header.size, b->elems,
				  pos, a, b);
		a->elems[new_pos] = new_elem;
		BPS_TREE_DATAMOVE(a->elems + new_pos + 1, b->elems + pos,
				  num - 1 - pos, a, b);
		if (!move_all)
			BPS_TREE_DATAMOVE(b->elems, b->elems + num - 1,
					  b->header.size - num + 1, b, b);
	}

	a->header.size += num;
	b->header.size -= (num - 1);
	*a_leaf_path_elem->max_elem_copy = a->elems[a->header.size - 1];
	if (!move_all)
		*b_leaf_path_elem->max_elem_copy =
			b->elems[b->header.size - 1];
	tree->size++;
}

/**
 * @breif Insert into inner and move a number of children to the left
 * Works like if bps_tree_insert_into_inner and
 *  bps_tree_move_elems_to_right_inner was consequentially called,
 *  BUT(!) insertion is allowed into full block, so one can consider
 *  insertion as a virtual insertion into virtual block of greater maximum size
 */
static inline void
bps_tree_insert_and_move_elems_to_left_inner(struct bps_tree *tree,
		struct bps_inner_path_elem *a_inner_path_elem,
		struct bps_inner_path_elem *b_inner_path_elem, bps_tree_pos_t num,
		bps_tree_block_id_t block_id, bps_tree_pos_t pos,
		bps_tree_elem_t max_elem)
{
	/* exclusive behaviuor for debug checks */
	if (tree->root_id != (bps_tree_block_id_t) -1) {
		a_inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, a_inner_path_elem->block_id);
		b_inner_path_elem->block = (struct bps_inner *)
		bps_tree_touch_block(tree, b_inner_path_elem->block_id);
	}
	struct bps_inner *a = a_inner_path_elem->block;
	struct bps_inner *b = b_inner_path_elem->block;
	bool move_to_empty = a->header.size == 0;
	bool move_all = b->header.size == num - 1;

	assert(num > 0);
	assert(b->header.size >= num - 1);
	assert(a->header.size + num <= BPS_TREE_MAX_COUNT_IN_INNER);
	assert(pos >= 0);
	assert(pos <= b->header.size);

	if (pos >= num) {
		/* In fact insert to 'b' block */
		bps_tree_pos_t new_pos = pos - num; /* Can be 0 */
		BPS_TREE_DATAMOVE(a->child_ids + a->header.size, b->child_ids,
				  num, a, b);
		BPS_TREE_DATAMOVE(b->child_ids, b->child_ids + num,
				  new_pos, b, b);
		b->child_ids[new_pos] = block_id;
		BPS_TREE_DATAMOVE(b->child_ids + new_pos + 1,
				  b->child_ids + pos,
				  b->header.size - pos, b, b);

		if (!move_to_empty)
			a->elems[a->header.size - 1] =
				*a_inner_path_elem->max_elem_copy;

		BPS_TREE_DATAMOVE(a->elems + a->header.size, b->elems,
				  num - 1, a, b);
		if (num < b->header.size)
			*a_inner_path_elem->max_elem_copy = b->elems[num - 1];
		else
			*a_inner_path_elem->max_elem_copy =
				*b_inner_path_elem->max_elem_copy;

		if (pos == b->header.size) { /* arrow is righter than star */
			if (num < b->header.size) {
				BPS_TREE_DATAMOVE(b->elems, b->elems + num,
						  b->header.size - num - 1,
						  b, b);
				b->elems[b->header.size - num - 1] =
					*b_inner_path_elem->max_elem_copy;
			}
			*b_inner_path_elem->max_elem_copy = max_elem;
		} else { /* star is righter than arrow */
			BPS_TREE_DATAMOVE(b->elems, b->elems + num,
					  new_pos, b, b);
			b->elems[new_pos] = max_elem;
			BPS_TREE_DATAMOVE(b->elems + new_pos + 1,
					  b->elems + pos,
					  b->header.size - pos - 1, b, b);
		}
	} else {
		/* In fact insert to 'a' block */
		bps_tree_pos_t new_pos = a->header.size + pos; /* Can be 0 */
		BPS_TREE_DATAMOVE(a->child_ids + a->header.size,
				  b->child_ids, pos, a, b);
		a->child_ids[new_pos] = block_id;
		BPS_TREE_DATAMOVE(a->child_ids + new_pos + 1,
				  b->child_ids + pos, num - 1 - pos, a, b);
		if (!move_all)
			BPS_TREE_DATAMOVE(b->child_ids, b->child_ids + num - 1,
					  b->header.size - num + 1, b, b);

		if (!move_to_empty)
			a->elems[a->header.size - 1] =
				*a_inner_path_elem->max_elem_copy;

		if (!move_all) {
			BPS_TREE_DATAMOVE(a->elems + a->header.size, b->elems,
					  pos, a, b);
		} else {
			if (pos == b->header.size) {
				if (pos > 0) { /* why? */
					BPS_TREE_DATAMOVE(a->elems +
							  a->header.size,
							  b->elems, pos - 1,
							  a, b);
					a->elems[new_pos - 1] =
					*b_inner_path_elem->max_elem_copy;
				}
			} else {
				BPS_TREE_DATAMOVE(a->elems + a->header.size,
						  b->elems, pos, a, b);
			}
		}
		if (new_pos == a->header.size + num - 1) {
			*a_inner_path_elem->max_elem_copy = max_elem;
		} else {
			a->elems[new_pos] = max_elem;
			BPS_TREE_DATAMOVE(a->elems + new_pos + 1,
					  b->elems + pos, num - 1 - pos - 1,
					  a, b);
			if (move_all)
				*a_inner_path_elem->max_elem_copy =
					*b_inner_path_elem->max_elem_copy;
			else
				*a_inner_path_elem->max_elem_copy =
					b->elems[num - 2];
		}
		if (!move_all)
			BPS_TREE_DATAMOVE(b->elems, b->elems + num - 1,
					  b->header.size - num, b, b);
	}

	a->header.size += num;
	b->header.size -= (num - 1);
}

/**
 * @brieaf Difference between maximum possible and current size of the leaf
 */
static inline bps_tree_pos_t
bps_tree_leaf_free_size(struct bps_leaf *leaf)
{
	return BPS_TREE_MAX_COUNT_IN_LEAF - leaf->header.size;
}

/**
 * @brieaf Difference between maximum possible and current size of the inner
 */
static inline bps_tree_pos_t
bps_tree_inner_free_size(struct bps_inner *inner)
{
	return BPS_TREE_MAX_COUNT_IN_INNER - inner->header.size;
}

/**
 * @brieaf Difference between current size of the leaf and minumum allowed
 */
static inline bps_tree_pos_t
bps_tree_leaf_overmin_size(struct bps_leaf *leaf)
{
	return leaf->header.size - BPS_TREE_MAX_COUNT_IN_LEAF * 2 / 3;
}
/**
 * @brieaf Difference between current size of the inner and minumum allowed
 */

static inline bps_tree_pos_t
bps_tree_inner_overmin_size(struct bps_inner *inner)
{
	return inner->header.size - BPS_TREE_MAX_COUNT_IN_INNER * 2 / 3;
}

/**
 * @brief Fill path element structure of the left leaf
 */
static inline bool
bps_tree_collect_left_path_elem_leaf(struct bps_tree *tree,
				     struct bps_leaf_path_elem *path_elem,
				     struct bps_leaf_path_elem *new_path_elem)
{
	struct bps_inner_path_elem *parent = path_elem->parent;
	if (!parent)
		return false;
	if (path_elem->pos_in_parent == 0)
		return false;

	new_path_elem->parent = path_elem->parent;
	new_path_elem->pos_in_parent = path_elem->pos_in_parent - 1;
	new_path_elem->block_id =
		parent->block->child_ids[new_path_elem->pos_in_parent];
	new_path_elem->block = (struct bps_leaf *)
		bps_tree_restore_block(tree, new_path_elem->block_id);
	new_path_elem->max_elem_copy =
		parent->block->elems + new_path_elem->pos_in_parent;
	new_path_elem->insertion_point = (bps_tree_pos_t)(-1); /* unused */
	return true;
}

/**
 * @brief Fill path element structure of the left inner
 *  almost exact copy of collect_tree_left_ext_leaf
 */
static inline bool
bps_tree_collect_left_path_elem_inner(struct bps_tree *tree,
				      struct bps_inner_path_elem *path_elem,
				      struct bps_inner_path_elem *new_path_elem)
{
	struct bps_inner_path_elem * parent = path_elem->parent;
	if (!parent)
		return false;
	if (path_elem->pos_in_parent == 0)
		return false;

	new_path_elem->parent = path_elem->parent;
	new_path_elem->pos_in_parent = path_elem->pos_in_parent - 1;
	new_path_elem->block_id =
		parent->block->child_ids[new_path_elem->pos_in_parent];
	new_path_elem->block = (struct bps_inner *)
		bps_tree_restore_block(tree, new_path_elem->block_id);
	new_path_elem->max_elem_copy = parent->block->elems +
		new_path_elem->pos_in_parent;
	new_path_elem->insertion_point = (bps_tree_pos_t)(-1); /* unused */
	return true;
}

/**
 * @brief Fill path element structure of the right leaf
 */
static inline bool
bps_tree_collect_right_ext_leaf(struct bps_tree *tree,
				struct bps_leaf_path_elem *path_elem,
				struct bps_leaf_path_elem *new_path_elem)
{
	struct bps_inner_path_elem *parent = path_elem->parent;
	if (!parent)
		return false;
	if (path_elem->pos_in_parent >= parent->block->header.size - 1)
		return false;

	new_path_elem->parent = path_elem->parent;
	new_path_elem->pos_in_parent = path_elem->pos_in_parent + 1;
	new_path_elem->block_id =
		parent->block->child_ids[new_path_elem->pos_in_parent];
	new_path_elem->block = (struct bps_leaf *)
		bps_tree_restore_block(tree, new_path_elem->block_id);
	if (new_path_elem->pos_in_parent >= parent->block->header.size - 1)
		new_path_elem->max_elem_copy = parent->max_elem_copy;
	else
		new_path_elem->max_elem_copy = parent->block->elems +
			new_path_elem->pos_in_parent;
	new_path_elem->insertion_point = (bps_tree_pos_t)(-1); /* unused */
	return true;
}

/**
 * @brief Fill path element structure of the right inner
 * almost exact copy of bps_tree_collect_right_ext_leaf
 */
static inline bool
bps_tree_collect_right_ext_inner(struct bps_tree *tree,
				 struct bps_inner_path_elem *path_elem,
				 struct bps_inner_path_elem *new_path_elem)
{
	struct bps_inner_path_elem *parent = path_elem->parent;
	if (!parent)
		return false;
	if (path_elem->pos_in_parent >= parent->block->header.size - 1)
		return false;

	new_path_elem->parent = path_elem->parent;
	new_path_elem->pos_in_parent = path_elem->pos_in_parent + 1;
	new_path_elem->block_id =
		parent->block->child_ids[new_path_elem->pos_in_parent];
	new_path_elem->block = (struct bps_inner *)
		bps_tree_restore_block(tree, new_path_elem->block_id);
	if (new_path_elem->pos_in_parent >= parent->block->header.size - 1)
		new_path_elem->max_elem_copy = parent->max_elem_copy;
	else
		new_path_elem->max_elem_copy = parent->block->elems +
			new_path_elem->pos_in_parent;
	new_path_elem->insertion_point = (bps_tree_pos_t)(-1); /* unused */
	return true;
}

/**
 * @brief Fill path element structure of the new leaf
 */
static inline void
bps_tree_prepare_new_ext_leaf(struct bps_leaf_path_elem *path_elem,
			      struct bps_leaf_path_elem *new_path_elem,
			      struct bps_leaf* new_leaf,
			      bps_tree_block_id_t new_leaf_id,
			      bps_tree_elem_t *max_elem_copy)
{
	new_path_elem->parent = path_elem->parent;
	new_path_elem->pos_in_parent = path_elem->pos_in_parent + 1;
	new_path_elem->block_id = new_leaf_id;
	new_path_elem->block = new_leaf;
	new_path_elem->max_elem_copy = max_elem_copy;
	new_path_elem->insertion_point = (bps_tree_pos_t)(-1); /* unused */
}

/**
 * @brief Fill path element structure of the new inner
 */
static inline void
bps_tree_prepare_new_ext_inner(struct bps_inner_path_elem *path_elem,
			       struct bps_inner_path_elem *new_path_elem,
			       struct bps_inner* new_inner,
			       bps_tree_block_id_t new_inner_id,
			       bps_tree_elem_t *max_elem_copy)
{
	new_path_elem->parent = path_elem->parent;
	new_path_elem->pos_in_parent = path_elem->pos_in_parent + 1;
	new_path_elem->block_id = new_inner_id;
	new_path_elem->block = new_inner;
	new_path_elem->max_elem_copy = max_elem_copy;
	new_path_elem->insertion_point = (bps_tree_pos_t)(-1); /* unused */
}

/**
 * bps_tree_process_insert_inner declaration. See definition for details.
 */
static int
bps_tree_process_insert_inner(struct bps_tree *tree,
			      struct bps_inner_path_elem *inner_path_elem,
			      bps_tree_block_id_t block_id, bps_tree_pos_t pos,
			      bps_tree_elem_t max_elem);

/**
 * Basic inserted into leaf, dealing with spliting, merging and moving data
 * to neighbour blocks if necessary
 */
static inline int
bps_tree_process_insert_leaf(struct bps_tree *tree,
			     struct bps_leaf_path_elem *leaf_path_elem,
			     bps_tree_elem_t new_elem)
{
	if (bps_tree_leaf_free_size(leaf_path_elem->block)) {
		bps_tree_insert_into_leaf(tree, leaf_path_elem, new_elem);
		BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x0);
		return 0;
	}
	bps_tree_touch_path(tree, leaf_path_elem);

	struct bps_leaf_path_elem left_ext = {0, 0, 0, 0, 0, 0, 0, 0},
			right_ext = {0, 0, 0, 0, 0, 0, 0, 0},
			left_left_ext = {0, 0, 0, 0, 0, 0, 0, 0},
			right_right_ext = {0, 0, 0, 0, 0, 0, 0, 0};
	bool has_left_ext =
		bps_tree_collect_left_path_elem_leaf(tree, leaf_path_elem,
						     &left_ext);
	bool has_right_ext =
		bps_tree_collect_right_ext_leaf(tree, leaf_path_elem,
						&right_ext);
	bool has_left_left_ext = false;
	bool has_right_right_ext = false;
	if (has_left_ext && has_right_ext) {
		if (bps_tree_leaf_free_size(left_ext.block) >
		    bps_tree_leaf_free_size(right_ext.block)) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_leaf_free_size(left_ext.block) / 2;
			bps_tree_insert_and_move_elems_to_left_leaf(tree,
					&left_ext, leaf_path_elem,
					move_count, new_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x1);
			return 0;
		} else if (bps_tree_leaf_free_size(right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_leaf_free_size(right_ext.block) / 2;
			bps_tree_insert_and_move_elems_to_right_leaf(tree,
					leaf_path_elem, &right_ext,
					move_count, new_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x2);
			return 0;
		}
	} else if (has_left_ext) {
		if (bps_tree_leaf_free_size(left_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_leaf_free_size(left_ext.block) / 2;
			bps_tree_insert_and_move_elems_to_left_leaf(tree,
					&left_ext, leaf_path_elem,
					move_count, new_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x3);
			return 0;
		}
		has_left_left_ext = bps_tree_collect_left_path_elem_leaf(tree,
				&left_ext, &left_left_ext);
		if (has_left_left_ext &&
		    bps_tree_leaf_free_size(left_left_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 + (2 *
				bps_tree_leaf_free_size(left_left_ext.block)
				- 1) / 3;
			bps_tree_move_elems_to_left_leaf(tree,
					&left_left_ext, &left_ext, move_count);
			move_count = 1 + move_count / 2;
			bps_tree_insert_and_move_elems_to_left_leaf(tree,
					&left_ext, leaf_path_elem,
					move_count, new_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x4);
			return 0;
		}
	} else if (has_right_ext) {
		if (bps_tree_leaf_free_size(right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_leaf_free_size(right_ext.block) / 2;
			bps_tree_insert_and_move_elems_to_right_leaf(tree,
					leaf_path_elem, &right_ext,
					move_count, new_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x5);
			return 0;
		}
		has_right_right_ext = bps_tree_collect_right_ext_leaf(tree,
				&right_ext, &right_right_ext);
		if (has_right_right_ext &&
		    bps_tree_leaf_free_size(right_right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 + (2 *
				bps_tree_leaf_free_size(right_right_ext.block)
				- 1) / 3;
			bps_tree_move_elems_to_right_leaf(tree, &right_ext,
					&right_right_ext, move_count);
			move_count = 1 + move_count / 2;
			bps_tree_insert_and_move_elems_to_right_leaf(tree,
					leaf_path_elem, &right_ext,
					move_count, new_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x6);
			return 0;
		}
	}

	if (!bps_tree_reserve_blocks(tree, tree->depth + 1)) {
		return -1;
	}
	bps_tree_block_id_t new_block_id = (bps_tree_block_id_t)(-1);
	struct bps_leaf *new_leaf = bps_tree_create_leaf(tree, &new_block_id);

	leaf_path_elem->block = (struct bps_leaf *)
	bps_tree_touch_block(tree, leaf_path_elem->block_id);

	if (leaf_path_elem->block->next_id != (bps_tree_block_id_t)(-1)) {
		struct bps_leaf *next_leaf = (struct bps_leaf *)
		bps_tree_touch_block(tree, leaf_path_elem->block->next_id);
		assert(next_leaf->prev_id == leaf_path_elem->block_id);
		next_leaf->prev_id = new_block_id;
	} else {
		tree->last_id = new_block_id;
	}
	new_leaf->next_id = leaf_path_elem->block->next_id;
	leaf_path_elem->block->next_id = new_block_id;
	new_leaf->prev_id = leaf_path_elem->block_id;

	new_leaf->header.size = 0;
	struct bps_leaf_path_elem new_path_elem;
	bps_tree_elem_t new_max_elem = tree->max_elem;
	bps_tree_prepare_new_ext_leaf(leaf_path_elem, &new_path_elem, new_leaf,
				      new_block_id, &new_max_elem);
	if (has_left_ext && has_right_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * left and right has MAX too. Split: insert new node at right
		 * and evenly rearrange elements.
		 *  Blocks:
		 *  [   MAX   ]  [ MAX + 1 ]  [    0    ]  [   MAX   ]
		 *  Moving:
		 *                      ----mc1--->
		 *         ----mc2--->               <---mc3----
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]  [   nc4   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_LEAF;
		const bps_tree_pos_t total = max * 3 + 1;
		const bps_tree_pos_t nc2 = total / 4;
		const bps_tree_pos_t nc3 = (total - nc2) / 3;
		const bps_tree_pos_t nc4 = (total - nc2 - nc3) / 2;
		const bps_tree_pos_t nc1 = (total - nc2 - nc3 - nc4);

		const bps_tree_pos_t mc2 = max - nc1;
		const bps_tree_pos_t mc3 = max - nc4;
		const bps_tree_pos_t mc1 = nc3 - mc3;

		bps_tree_insert_and_move_elems_to_right_leaf(tree,
				leaf_path_elem, &new_path_elem, mc1, new_elem);
		bps_tree_move_elems_to_right_leaf(tree,
				&left_ext, leaf_path_elem, mc2);
		bps_tree_move_elems_to_left_leaf(tree,
				&new_path_elem, &right_ext, mc3);

		BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x7);
	} else if (has_left_ext && has_left_left_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there no right block, and left and left-left has MAX too.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [   MAX   ]  [   MAX   ]  [ MAX + 1 ]  [    0    ]
		 *  Moving:
		 *                                   ----mc1--->
		 *                      ----mc2--->
		 *         ----mc3--->
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]  [   nc4   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_LEAF;
		const bps_tree_pos_t total = max * 3 + 1;
		const bps_tree_pos_t nc4 = total / 4;
		const bps_tree_pos_t nc3 = (total - nc4) / 3;
		const bps_tree_pos_t nc2 = (total - nc4 - nc3) / 2;
		const bps_tree_pos_t nc1 = (total - nc4 - nc3 - nc2);

		const bps_tree_pos_t mc1 = nc4;
		const bps_tree_pos_t mc3 = max - nc1;
		const bps_tree_pos_t mc2 = max + mc3 - nc2;

		bps_tree_insert_and_move_elems_to_right_leaf(tree,
				leaf_path_elem, &new_path_elem, mc1, new_elem);
		bps_tree_move_elems_to_right_leaf(tree,
				&left_ext, leaf_path_elem, mc2);
		bps_tree_move_elems_to_right_leaf(tree,
				&left_left_ext, &left_ext, mc3);

		BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x8);
	} else if (has_right_ext && has_right_right_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there no left block, and right and right-right has MAX too.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [ MAX + 1 ]  [    0    ]  [   MAX   ]  [   MAX   ]
		 *  Moving:
		 *         ----mc1--->  <---mc2----
		 *                                   <---mc3----
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]  [   nc4   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_LEAF;
		const bps_tree_pos_t total = max * 3 + 1;
		const bps_tree_pos_t nc1 = total / 4;
		const bps_tree_pos_t nc2 = (total - nc1) / 3;
		const bps_tree_pos_t nc3 = (total - nc1 - nc2) / 2;
		const bps_tree_pos_t nc4 = (total - nc1 - nc2 - nc3);

		const bps_tree_pos_t mc1 = max + 1 - nc1;
		const bps_tree_pos_t mc3 = max - nc4;
		const bps_tree_pos_t mc2 = max + mc3 - nc3;

		bps_tree_insert_and_move_elems_to_right_leaf(tree,
				leaf_path_elem, &new_path_elem, mc1, new_elem);
		bps_tree_move_elems_to_left_leaf(tree,
				&new_path_elem, &right_ext, mc2);
		bps_tree_move_elems_to_left_leaf(tree,
				&right_ext, &right_right_ext, mc3);

		BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0x9);
	} else if (has_left_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there only left block, and it has MAX too.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [   MAX   ]  [ MAX + 1 ]  [    0    ]
		 *  Moving:
		 *                      ----mc1--->
		 *         ----mc2--->
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_LEAF;
		const bps_tree_pos_t total = max * 2 + 1;
		const bps_tree_pos_t nc3 = total / 3;
		const bps_tree_pos_t nc2 = (total - nc3) / 2;
		const bps_tree_pos_t nc1 = (total - nc3 - nc2);

		const bps_tree_pos_t mc1 = nc3;
		const bps_tree_pos_t mc2 = max - nc1;

		bps_tree_insert_and_move_elems_to_right_leaf(tree,
				leaf_path_elem, &new_path_elem, mc1, new_elem);
		bps_tree_move_elems_to_right_leaf(tree,
				&left_ext, leaf_path_elem, mc2);

		BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0xA);
	} else if (has_right_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there only right block, and it has MAX too.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [ MAX + 1 ]  [    0    ]  [   MAX   ]
		 *  Moving:
		 *         ----mc1--->  <---mc2----
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_LEAF;
		const bps_tree_pos_t total = max * 2 + 1;
		const bps_tree_pos_t nc1 = total / 3;
		const bps_tree_pos_t nc2 = (total - nc1) / 2;
		const bps_tree_pos_t nc3 = (total - nc1 - nc2);

		const bps_tree_pos_t mc1 = max + 1 - nc1;
		const bps_tree_pos_t mc2 = max - nc3;

		bps_tree_insert_and_move_elems_to_right_leaf(tree,
				leaf_path_elem, &new_path_elem, mc1, new_elem);
		bps_tree_move_elems_to_left_leaf(tree, &new_path_elem,
				&right_ext, mc2);

		BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0xB);
	} else {
		assert(!leaf_path_elem->parent);
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there no siblings.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [ MAX + 1 ]  [    0    ]
		 *  Moving:
		 *         ----mc1--->
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_LEAF;
		const bps_tree_pos_t total = max + 1;
		const bps_tree_pos_t nc2 = total / 2;
		/* const bps_tree_pos_t nc1 = (total - nc2); */

		const bps_tree_pos_t mc1 = nc2;

		bps_tree_insert_and_move_elems_to_right_leaf(tree,
				leaf_path_elem, &new_path_elem, mc1, new_elem);

		bps_tree_block_id_t new_root_id = (bps_tree_block_id_t)(-1);
		struct bps_inner *new_root = bps_tree_create_inner(tree,
				&new_root_id);
		new_root->header.size = 2;
		new_root->child_ids[0] = tree->root_id;
		new_root->child_ids[1] = new_block_id;
		new_root->elems[0] = tree->max_elem;
		tree->root_id = new_root_id;
		tree->max_elem = new_max_elem;
		tree->depth++;

		BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0xC);
		return 0;
	}
	assert(leaf_path_elem->parent);
	BPS_TREE_BRANCH_TRACE(tree, insert_leaf, 1 << 0xD);
	return bps_tree_process_insert_inner(tree, leaf_path_elem->parent,
			new_block_id, new_path_elem.pos_in_parent,
			new_max_elem);
}

/**
 * Basic inserted into inner, dealing with spliting, merging and moving data
 * to neighbour blocks if necessary
 */
static inline int
bps_tree_process_insert_inner(struct bps_tree *tree,
			      struct bps_inner_path_elem *inner_path_elem,
			      bps_tree_block_id_t block_id,
			      bps_tree_pos_t pos, bps_tree_elem_t max_elem)
{
	if (bps_tree_inner_free_size(inner_path_elem->block)) {
		bps_tree_insert_into_inner(tree, inner_path_elem,
					   block_id, pos, max_elem);
		BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x0);
		return 0;
	}
	struct bps_inner_path_elem left_ext = {0, 0, 0, 0, 0, 0, 0, 0},
		right_ext = {0, 0, 0, 0, 0, 0, 0, 0},
		left_left_ext = {0, 0, 0, 0, 0, 0, 0, 0},
		right_right_ext = {0, 0, 0, 0, 0, 0, 0, 0};
	bool has_left_ext =
		bps_tree_collect_left_path_elem_inner(tree, inner_path_elem,
						      &left_ext);
	bool has_right_ext =
		bps_tree_collect_right_ext_inner(tree, inner_path_elem,
						 &right_ext);
	bool has_left_left_ext = false;
	bool has_right_right_ext = false;
	if (has_left_ext && has_right_ext) {
		if (bps_tree_inner_free_size(left_ext.block) >
		    bps_tree_inner_free_size(right_ext.block)) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_inner_free_size(left_ext.block) / 2;
			bps_tree_insert_and_move_elems_to_left_inner(tree,
					&left_ext, inner_path_elem, move_count,
					block_id, pos, max_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x1);
			return 0;
		} else if (bps_tree_inner_free_size(right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_inner_free_size(right_ext.block) / 2;
			bps_tree_insert_and_move_elems_to_right_inner(tree,
					inner_path_elem, &right_ext,
					move_count, block_id, pos, max_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x2);
			return 0;
		}
	} else if (has_left_ext) {
		if (bps_tree_inner_free_size(left_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_inner_free_size(left_ext.block) / 2;
			bps_tree_insert_and_move_elems_to_left_inner(tree,
					&left_ext, inner_path_elem,
					move_count, block_id, pos, max_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x3);
			return 0;
		}
		has_left_left_ext = bps_tree_collect_left_path_elem_inner(tree,
				&left_ext, &left_left_ext);
		if (has_left_left_ext &&
		    bps_tree_inner_free_size(left_left_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 + (2 *
				bps_tree_inner_free_size(left_left_ext.block)
				- 1) / 3;
			bps_tree_move_elems_to_left_inner(tree, &left_left_ext,
					&left_ext, move_count);
			move_count = 1 + move_count / 2;
			bps_tree_insert_and_move_elems_to_left_inner(tree,
					&left_ext, inner_path_elem, move_count,
					block_id, pos, max_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x4);
			return 0;
		}
	} else if (has_right_ext) {
		if (bps_tree_inner_free_size(right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_inner_free_size(right_ext.block) / 2;
			bps_tree_insert_and_move_elems_to_right_inner(tree,
					inner_path_elem, &right_ext,
					move_count, block_id, pos, max_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x5);
			return 0;
		}
		has_right_right_ext = bps_tree_collect_right_ext_inner(tree,
					&right_ext, &right_right_ext);
		if (has_right_right_ext &&
		    bps_tree_inner_free_size(right_right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 + (2 *
				bps_tree_inner_free_size(right_right_ext.block)
				- 1) / 3;
			bps_tree_move_elems_to_right_inner(tree, &right_ext,
					&right_right_ext, move_count);
			move_count = 1 + move_count / 2;
			bps_tree_insert_and_move_elems_to_right_inner(tree,
					inner_path_elem, &right_ext,
					move_count, block_id, pos, max_elem);
			BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x6);
			return 0;
		}
	}
	bps_tree_block_id_t new_block_id = (bps_tree_block_id_t)(-1);
	struct bps_inner *new_inner = bps_tree_create_inner(tree,
			&new_block_id);

	new_inner->header.size = 0;
	struct bps_inner_path_elem new_path_elem;
	bps_tree_elem_t new_max_elem = tree->max_elem;
	bps_tree_prepare_new_ext_inner(inner_path_elem, &new_path_elem,
				       new_inner, new_block_id, &new_max_elem);
	if (has_left_ext && has_right_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * left and right has MAX too. Split: insert new node at right
		 * and evenly rearrange elements.
		 *  Blocks:
		 *  [   MAX   ]  [ MAX + 1 ]  [    0    ]  [   MAX   ]
		 *  Moving:
		 *                      ----mc1--->
		 *         ----mc2--->               <---mc3----
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]  [   nc4   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_INNER;
		const bps_tree_pos_t total = max * 3 + 1;
		const bps_tree_pos_t nc2 = total / 4;
		const bps_tree_pos_t nc3 = (total - nc2) / 3;
		const bps_tree_pos_t nc4 = (total - nc2 - nc3) / 2;
		const bps_tree_pos_t nc1 = (total - nc2 - nc3 - nc4);

		const bps_tree_pos_t mc2 = max - nc1;
		const bps_tree_pos_t mc3 = max - nc4;
		const bps_tree_pos_t mc1 = nc3 - mc3;

		bps_tree_insert_and_move_elems_to_right_inner(tree,
				inner_path_elem, &new_path_elem,
				mc1, block_id, pos, max_elem);
		bps_tree_move_elems_to_right_inner(tree,
				&left_ext, inner_path_elem, mc2);
		bps_tree_move_elems_to_left_inner(tree,
				&new_path_elem, &right_ext, mc3);

		BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x7);
	} else if (has_left_ext && has_left_left_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there no right block, and left and left-left has MAX too.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [   MAX   ]  [   MAX   ]  [ MAX + 1 ]  [    0    ]
		 *  Moving:
		 *                                   ----mc1--->
		 *                      ----mc2--->
		 *         ----mc3--->
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]  [   nc4   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_INNER;
		const bps_tree_pos_t total = max * 3 + 1;
		const bps_tree_pos_t nc4 = total / 4;
		const bps_tree_pos_t nc3 = (total - nc4) / 3;
		const bps_tree_pos_t nc2 = (total - nc4 - nc3) / 2;
		const bps_tree_pos_t nc1 = (total - nc4 - nc3 - nc2);

		const bps_tree_pos_t mc1 = nc4;
		const bps_tree_pos_t mc3 = max - nc1;
		const bps_tree_pos_t mc2 = max + mc3 - nc2;

		bps_tree_insert_and_move_elems_to_right_inner(tree,
				inner_path_elem, &new_path_elem,
				mc1, block_id, pos, max_elem);
		bps_tree_move_elems_to_right_inner(tree,
				&left_ext, inner_path_elem, mc2);
		bps_tree_move_elems_to_right_inner(tree,
				&left_left_ext, &left_ext, mc3);

		BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x8);
	} else if (has_right_ext && has_right_right_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there no left block, and right and right-right has MAX too.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [ MAX + 1 ]  [    0    ]  [   MAX   ]  [   MAX   ]
		 *  Moving:
		 *         ----mc1--->  <---mc2----
		 *                                   <---mc3----
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]  [   nc4   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_INNER;
		const bps_tree_pos_t total = max * 3 + 1;
		const bps_tree_pos_t nc1 = total / 4;
		const bps_tree_pos_t nc2 = (total - nc1) / 3;
		const bps_tree_pos_t nc3 = (total - nc1 - nc2) / 2;
		const bps_tree_pos_t nc4 = (total - nc1 - nc2 - nc3);

		const bps_tree_pos_t mc1 = max + 1 - nc1;
		const bps_tree_pos_t mc3 = max - nc4;
		const bps_tree_pos_t mc2 = max + mc3 - nc3;

		bps_tree_insert_and_move_elems_to_right_inner(tree,
				inner_path_elem, &new_path_elem,
				mc1, block_id, pos, max_elem);
		bps_tree_move_elems_to_left_inner(tree,
				&new_path_elem, &right_ext, mc2);
		bps_tree_move_elems_to_left_inner(tree,
				&right_ext, &right_right_ext, mc3);

		BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0x9);
	} else if (has_left_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there only left block, and it has MAX too.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [   MAX   ]  [ MAX + 1 ]  [    0    ]
		 *  Moving:
		 *                      ----mc1--->
		 *         ----mc2--->
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_INNER;
		const bps_tree_pos_t total = max * 2 + 1;
		const bps_tree_pos_t nc3 = total / 3;
		const bps_tree_pos_t nc2 = (total - nc3) / 2;
		const bps_tree_pos_t nc1 = (total - nc3 - nc2);

		const bps_tree_pos_t mc1 = nc3;
		const bps_tree_pos_t mc2 = max - nc1;

		bps_tree_insert_and_move_elems_to_right_inner(tree,
				inner_path_elem, &new_path_elem,
				mc1, block_id, pos, max_elem);
		bps_tree_move_elems_to_right_inner(tree,
				&left_ext, inner_path_elem, mc2);

		BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0xA);
	} else if (has_right_ext) {
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there only right block, and it has MAX too.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [ MAX + 1 ]  [    0    ]  [   MAX   ]
		 *  Moving:
		 *         ----mc1--->  <---mc2----
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]  [   nc3   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_INNER;
		const bps_tree_pos_t total = max * 2 + 1;
		const bps_tree_pos_t nc1 = total / 3;
		const bps_tree_pos_t nc2 = (total - nc1) / 2;
		const bps_tree_pos_t nc3 = (total - nc1 - nc2);

		const bps_tree_pos_t mc1 = max + 1 - nc1;
		const bps_tree_pos_t mc2 = max - nc3;

		bps_tree_insert_and_move_elems_to_right_inner(tree,
				inner_path_elem, &new_path_elem,
				mc1, block_id, pos, max_elem);
		bps_tree_move_elems_to_left_inner(tree,
				&new_path_elem, &right_ext, mc2);

		BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0xB);
	} else {
		assert(!inner_path_elem->parent);
		/*
		 * The block has MAX elems and +1 elem is inserted,
		 * there no siblings.
		 * Split: insert new node at right and evenly rearrange
		 * elements.
		 *  Blocks:
		 *  [ MAX + 1 ]  [    0    ]
		 *  Moving:
		 *         ----mc1--->
		 *  To become:
		 *  [   nc1   ]  [   nc2   ]
		 */
		const bps_tree_pos_t max = BPS_TREE_MAX_COUNT_IN_INNER;
		const bps_tree_pos_t total = max + 1;
		const bps_tree_pos_t nc2 = total / 2;
		/* const bps_tree_pos_t nc1 = (total - nc2); */

		const bps_tree_pos_t mc1 = nc2;

		bps_tree_insert_and_move_elems_to_right_inner(tree,
				inner_path_elem, &new_path_elem,
				mc1, block_id, pos, max_elem);

		bps_tree_block_id_t new_root_id = (bps_tree_block_id_t)(-1);
		struct bps_inner *new_root =
			bps_tree_create_inner(tree, &new_root_id);
		new_root->header.size = 2;
		new_root->child_ids[0] = tree->root_id;
		new_root->child_ids[1] = new_block_id;
		new_root->elems[0] = tree->max_elem;
		tree->root_id = new_root_id;
		tree->max_elem = new_max_elem;
		tree->depth++;
		BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0xC);
		return 0;
	}
	assert(inner_path_elem->parent);
	BPS_TREE_BRANCH_TRACE(tree, insert_inner, 1 << 0xD);
	return bps_tree_process_insert_inner(tree, inner_path_elem->parent,
			new_block_id, new_path_elem.pos_in_parent,
			new_max_elem);
}

/**
 * bps_tree_process_delete_inner declaration. See definition for details.
 */
static void
bps_tree_process_delete_inner(struct bps_tree *tree,
			      struct bps_inner_path_elem *inner_path_elem);

/**
 * Basic deleting from leaf, dealing with spliting, merging and moving data
 * to neighbour blocks if necessary
 */
static inline void
bps_tree_process_delete_leaf(struct bps_tree *tree,
			     struct bps_leaf_path_elem *leaf_path_elem)
{
	bps_tree_delete_from_leaf(tree, leaf_path_elem);

	if (leaf_path_elem->block->header.size >=
	    BPS_TREE_MAX_COUNT_IN_LEAF * 2 / 3) {
		BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x0);
		return;
	}

	bps_tree_touch_path(tree, leaf_path_elem);

	struct bps_leaf_path_elem left_ext = {0, 0, 0, 0, 0, 0, 0, 0},
		right_ext = {0, 0, 0, 0, 0, 0, 0, 0},
		left_left_ext = {0, 0, 0, 0, 0, 0, 0, 0},
		right_right_ext = {0, 0, 0, 0, 0, 0, 0, 0};
	bool has_left_ext =
		bps_tree_collect_left_path_elem_leaf(tree, leaf_path_elem,
						     &left_ext);
	bool has_right_ext =
		bps_tree_collect_right_ext_leaf(tree, leaf_path_elem,
						&right_ext);
	bool has_left_left_ext = false;
	bool has_right_right_ext = false;
	if (has_left_ext && has_right_ext) {
		if (bps_tree_leaf_overmin_size(left_ext.block) >
		    bps_tree_leaf_overmin_size(right_ext.block)) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_leaf_overmin_size(left_ext.block) / 2;
			bps_tree_move_elems_to_right_leaf(tree, &left_ext,
					leaf_path_elem, move_count);
			BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x1);
			return;
		} else if (bps_tree_leaf_overmin_size(right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_leaf_overmin_size(right_ext.block) / 2;
			bps_tree_move_elems_to_left_leaf(tree, leaf_path_elem,
					&right_ext, move_count);
			BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x2);
			return;
		}
	} else if (has_left_ext) {
		if (bps_tree_leaf_overmin_size(left_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_leaf_overmin_size(left_ext.block) / 2;
			bps_tree_move_elems_to_right_leaf(tree, &left_ext,
					leaf_path_elem, move_count);
			BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x3);
			return;
		}
		has_left_left_ext = bps_tree_collect_left_path_elem_leaf(tree,
				&left_ext, &left_left_ext);
		if (has_left_left_ext &&
		    bps_tree_leaf_overmin_size(left_left_ext.block) > 0) {
			bps_tree_pos_t move_count2 = 1 + (2 *
				bps_tree_leaf_overmin_size(left_left_ext.block)
				- 1) / 3;
			bps_tree_pos_t move_count1 = 1 + move_count2 / 2;
			bps_tree_move_elems_to_right_leaf(tree, &left_ext,
					leaf_path_elem, move_count1);
			bps_tree_move_elems_to_right_leaf(tree, &left_left_ext,
					&left_ext, move_count2);
			BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x4);
			return;
		}
	} else if (has_right_ext) {
		if (bps_tree_leaf_overmin_size(right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_leaf_overmin_size(right_ext.block)
				/ 2;
			bps_tree_move_elems_to_left_leaf(tree, leaf_path_elem,
					&right_ext, move_count);
			BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x5);
			return;
		}
		has_right_right_ext = bps_tree_collect_right_ext_leaf(tree,
				&right_ext, &right_right_ext);
		if (has_right_right_ext &&
		    bps_tree_leaf_overmin_size(right_right_ext.block) > 0) {
			bps_tree_pos_t move_count2 = 1 + (2 *
				bps_tree_leaf_overmin_size(right_right_ext.block)
				- 1) / 3;
			bps_tree_pos_t move_count1 = 1 + move_count2 / 2;
			bps_tree_move_elems_to_left_leaf(tree, leaf_path_elem,
					&right_ext, move_count1);
			bps_tree_move_elems_to_left_leaf(tree, &right_ext,
					&right_right_ext, move_count2);
			BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x6);
			return;
		}
	}

	if (has_left_ext && has_right_ext) {
		bps_tree_pos_t move_count =
			(leaf_path_elem->block->header.size + 1) / 2;
		bps_tree_move_elems_to_right_leaf(tree, leaf_path_elem,
				&right_ext, move_count);
		move_count = leaf_path_elem->block->header.size;
		bps_tree_move_elems_to_left_leaf(tree, &left_ext,
				leaf_path_elem, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x7);
	} else if (has_left_ext && has_left_left_ext) {
		bps_tree_pos_t move_count =
			(leaf_path_elem->block->header.size + 1) / 2;
		bps_tree_move_elems_to_left_leaf(tree, &left_left_ext,
				&left_ext, move_count);
		move_count = leaf_path_elem->block->header.size;
		bps_tree_move_elems_to_left_leaf(tree, &left_ext,
				leaf_path_elem, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x8);
	} else if (has_right_ext && has_right_right_ext) {
		bps_tree_pos_t move_count =
			(leaf_path_elem->block->header.size + 1) / 2;
		bps_tree_move_elems_to_right_leaf(tree, &right_ext,
				&right_right_ext, move_count);
		move_count = leaf_path_elem->block->header.size;
		bps_tree_move_elems_to_right_leaf(tree, leaf_path_elem,
				&right_ext, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x9);
	} else if (has_left_ext) {
		if (leaf_path_elem->block->header.size +
		    left_ext.block->header.size > BPS_TREE_MAX_COUNT_IN_LEAF) {
			BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0xA);
			return;
		}
		bps_tree_pos_t move_count = leaf_path_elem->block->header.size;
		bps_tree_move_elems_to_left_leaf(tree, &left_ext,
				leaf_path_elem, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0xB);
	} else if (has_right_ext) {
		if (leaf_path_elem->block->header.size +
		    right_ext.block->header.size > BPS_TREE_MAX_COUNT_IN_LEAF) {
			BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0xC);
			return;
		}
		bps_tree_pos_t move_count = leaf_path_elem->block->header.size;
		bps_tree_move_elems_to_right_leaf(tree, leaf_path_elem,
				&right_ext, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0xD);
	} else {
		if (leaf_path_elem->block->header.size > 0) {
			BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0xE);
			return;
		}
		assert(leaf_path_elem->parent == 0);
		assert(tree->depth == 1);
		assert(tree->size == 0);
		tree->root_id = (bps_tree_block_id_t)(-1);
		tree->depth = 0;
		tree->first_id = (bps_tree_block_id_t)(-1);
		tree->last_id = (bps_tree_block_id_t)(-1);
		bps_tree_dispose_leaf(tree, leaf_path_elem->block,
				leaf_path_elem->block_id);
		BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0xF);
		return;
	}

	assert(leaf_path_elem->block->header.size == 0);

	struct bps_leaf *leaf = (struct bps_leaf*)leaf_path_elem->block;
	if (leaf->prev_id == (bps_tree_block_id_t)(-1)) {
		tree->first_id = leaf->next_id;
	} else {
		struct bps_leaf *prev_block = (struct bps_leaf *)
			bps_tree_touch_block(tree, leaf->prev_id);
		prev_block->next_id = leaf->next_id;
	}
	if (leaf->next_id == (bps_tree_block_id_t)(-1)) {
		tree->last_id = leaf->prev_id;
	} else {
		struct bps_leaf *next_block = (struct bps_leaf *)
			bps_tree_touch_block(tree, leaf->next_id);
		next_block->prev_id = leaf->prev_id;
	}

	bps_tree_dispose_leaf(tree, leaf_path_elem->block,
			leaf_path_elem->block_id);
	assert(leaf_path_elem->parent);
	bps_tree_process_delete_inner(tree, leaf_path_elem->parent);
	BPS_TREE_BRANCH_TRACE(tree, delete_leaf, 1 << 0x10);
}

/**
 * Basic deletion from a leaf, deals with possible splitting,
 * merging and moving of elements data to neighbouring blocks.
 */
static inline void
bps_tree_process_delete_inner(struct bps_tree *tree,
			      struct bps_inner_path_elem *inner_path_elem)
{
	bps_tree_delete_from_inner(tree, inner_path_elem);

	if (inner_path_elem->block->header.size >=
	    BPS_TREE_MAX_COUNT_IN_INNER * 2 / 3) {
		BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x0);
		return;
	}

	struct bps_inner_path_elem left_ext = {0, 0, 0, 0, 0, 0, 0, 0},
		right_ext = {0, 0, 0, 0, 0, 0, 0, 0},
		left_left_ext = {0, 0, 0, 0, 0, 0, 0, 0},
		right_right_ext = {0, 0, 0, 0, 0, 0, 0, 0};
	bool has_left_ext =
		bps_tree_collect_left_path_elem_inner(tree, inner_path_elem,
						      &left_ext);
	bool has_right_ext =
		bps_tree_collect_right_ext_inner(tree, inner_path_elem,
						      &right_ext);
	bool has_left_left_ext = false;
	bool has_right_right_ext = false;
	if (has_left_ext && has_right_ext) {
		if (bps_tree_inner_overmin_size(left_ext.block) >
		    bps_tree_inner_overmin_size(right_ext.block)) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_inner_overmin_size(left_ext.block)
				/ 2;
			bps_tree_move_elems_to_right_inner(tree, &left_ext,
					inner_path_elem, move_count);
			BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x1);
			return;
		} else if (bps_tree_inner_overmin_size(right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_inner_overmin_size(right_ext.block)
				/ 2;
			bps_tree_move_elems_to_left_inner(tree,
					inner_path_elem, &right_ext,
					move_count);
			BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x2);
			return;
		}
	} else if (has_left_ext) {
		if (bps_tree_inner_overmin_size(left_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_inner_overmin_size(left_ext.block)
				/ 2;
			bps_tree_move_elems_to_right_inner(tree, &left_ext,
					inner_path_elem, move_count);
			BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x3);
			return;
		}
		has_left_left_ext =
			bps_tree_collect_left_path_elem_inner(tree, &left_ext,
							      &left_left_ext);
		if (has_left_left_ext &&
		    bps_tree_inner_overmin_size(left_left_ext.block) > 0) {
			bps_tree_pos_t move_count2 = 1 + (2 *
				bps_tree_inner_overmin_size(left_left_ext.block)
				- 1) / 3;
			bps_tree_pos_t move_count1 = 1 + move_count2 / 2;
			bps_tree_move_elems_to_right_inner(tree, &left_ext,
					inner_path_elem, move_count1);
			bps_tree_move_elems_to_right_inner(tree,
					&left_left_ext, &left_ext, move_count2);
			BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x4);
			return;
		}
	} else if (has_right_ext) {
		if (bps_tree_inner_overmin_size(right_ext.block) > 0) {
			bps_tree_pos_t move_count = 1 +
				bps_tree_inner_overmin_size(right_ext.block)
				/ 2;
			bps_tree_move_elems_to_left_inner(tree,
					inner_path_elem, &right_ext,
					move_count);
			BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x5);
			return;
		}
		has_right_right_ext =
			bps_tree_collect_right_ext_inner(tree, &right_ext,
							 &right_right_ext);
		if (has_right_right_ext &&
		    bps_tree_inner_overmin_size(right_right_ext.block) > 0) {
			bps_tree_pos_t move_count2 = 1 + (2 *
				bps_tree_inner_overmin_size(right_right_ext.block)
				- 1) / 3;
			bps_tree_pos_t move_count1 = 1 + move_count2 / 2;
			bps_tree_move_elems_to_left_inner(tree, inner_path_elem,
					&right_ext, move_count1);
			bps_tree_move_elems_to_left_inner(tree, &right_ext,
					&right_right_ext, move_count2);
			BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x6);
			return;
		}
	}

	if (has_left_ext && has_right_ext) {
		bps_tree_pos_t move_count =
			(inner_path_elem->block->header.size + 1) / 2;
		bps_tree_move_elems_to_right_inner(tree, inner_path_elem,
				&right_ext, move_count);
		move_count = inner_path_elem->block->header.size;
		bps_tree_move_elems_to_left_inner(tree, &left_ext,
				inner_path_elem, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x7);
	} else if (has_left_ext && has_left_left_ext) {
		bps_tree_pos_t move_count =
			(inner_path_elem->block->header.size + 1) / 2;
		bps_tree_move_elems_to_left_inner(tree, &left_left_ext,
				&left_ext, move_count);
		move_count = inner_path_elem->block->header.size;
		bps_tree_move_elems_to_left_inner(tree, &left_ext,
				inner_path_elem, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x8);
	} else if (has_right_ext && has_right_right_ext) {
		bps_tree_pos_t move_count =
			(inner_path_elem->block->header.size + 1) / 2;
		bps_tree_move_elems_to_right_inner(tree, &right_ext,
				&right_right_ext, move_count);
		move_count = inner_path_elem->block->header.size;
		bps_tree_move_elems_to_right_inner(tree, inner_path_elem,
				&right_ext, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x9);
	} else if (has_left_ext) {
		if (inner_path_elem->block->header.size +
		    left_ext.block->header.size > BPS_TREE_MAX_COUNT_IN_INNER) {
			BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0xA);
			//throw 1;
			return;
		}
		bps_tree_pos_t move_count = inner_path_elem->block->header.size;
		bps_tree_move_elems_to_left_inner(tree, &left_ext,
				inner_path_elem, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0xB);
	} else if (has_right_ext) {
		if (inner_path_elem->block->header.size +
		    right_ext.block->header.size >
		    BPS_TREE_MAX_COUNT_IN_INNER) {
			BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0xC);
			//throw 2;
			return;
		}
		bps_tree_pos_t move_count = inner_path_elem->block->header.size;
		bps_tree_move_elems_to_right_inner(tree, inner_path_elem,
				&right_ext, move_count);
		BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0xD);
	} else {
		if (inner_path_elem->block->header.size > 1) {
			BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0xE);
			return;
		}
		assert(tree->depth > 1);
		assert(inner_path_elem->parent == 0);
		tree->depth--;
		tree->root_id = inner_path_elem->block->child_ids[0];
		bps_tree_dispose_inner(tree, inner_path_elem->block,
				inner_path_elem->block_id);
		BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0xF);
		return;
	}
	assert(inner_path_elem->block->header.size == 0);

	bps_tree_dispose_inner(tree, inner_path_elem->block,
			inner_path_elem->block_id);
	assert(inner_path_elem->parent);
	bps_tree_process_delete_inner(tree, inner_path_elem->parent);
	BPS_TREE_BRANCH_TRACE(tree, delete_inner, 1 << 0x10);
}

/**
 * @brief Insert an element to the tree or replace an element in the tree
 * In case of replacing, if 'replaced' argument is not null, it'll
 * be filled with replaced element. In case of inserting it's left
 * intact.
 * Thus one can distinguish a real insert or replace by passing to
 * the function a pointer to some value; and if it was changed
 * during the function call, then the replace has happened.
 * Otherwise, it was an insert.
 * @param tree - pointer to a tree
 * @param new_elem - inserting or replacing element
 * @replaced - optional pointer for a replaces element
 * @return - 0 on success or -1 if memory allocation failed for insert
 */
static inline int
bps_tree_insert(struct bps_tree *tree, bps_tree_elem_t new_elem,
			   bps_tree_elem_t *replaced)
{
	if (tree->root_id == (bps_tree_block_id_t)(-1))
		return bps_tree_insert_first_elem(tree, new_elem);

	struct bps_inner_path_elem path[BPS_TREE_MAX_DEPTH];
	struct bps_leaf_path_elem leaf_path_elem;
	bool exact;
	bps_tree_collect_path(tree, new_elem, path, &leaf_path_elem, &exact);
	if (exact) {
		bps_tree_process_replace(tree, &leaf_path_elem, new_elem,
					 replaced);
		return 0;
	} else {
		return bps_tree_process_insert_leaf(tree, &leaf_path_elem,
						    new_elem);
	}
}

/**
 * @brief Delete an element from a tree.
 * @param tree - pointer to a tree
 * @param elem - the element tot delete
 * @return - true on success or false if the element was not found in tree
 */
static inline int
bps_tree_delete(struct bps_tree *tree, bps_tree_elem_t elem)
{
	if (tree->root_id == (bps_tree_block_id_t)(-1))
		return -1;
	struct bps_inner_path_elem path[BPS_TREE_MAX_DEPTH];
	struct bps_leaf_path_elem leaf_path_elem;
	bool exact;
	bps_tree_collect_path(tree, elem, path, &leaf_path_elem, &exact);

	if (!exact)
		return -1;

	bps_tree_process_delete_leaf(tree, &leaf_path_elem);
	return 0;
}

/**
 * @brief Recursively find a maximum element in subtree.
 * Used only for debug purposes
 */
static inline bps_tree_elem_t
bps_tree_debug_find_max_elem(const struct bps_tree *tree,
			     struct bps_block *block)
{
	assert(block->size);
	if (block->type == BPS_TREE_BT_LEAF) {
		struct bps_leaf *leaf = (struct bps_leaf *)block;
		return leaf->elems[block->size - 1];
	} else {
		assert(block->type == BPS_TREE_BT_INNER);
		struct bps_inner *inner = (struct bps_inner *)block;
		bps_tree_block_id_t next_block_id =
			inner->child_ids[block->size - 1];
		struct bps_block *next_block =
			bps_tree_restore_block(tree, next_block_id);
		return bps_tree_debug_find_max_elem(tree, next_block);
	}
}

#ifndef BPS_TREE_NO_DEBUG

/**
 * @brief Recursively checks the block and the corresponding subtree
 * Used by bps_tree_debug_check
 */
static inline int
bps_tree_debug_check_block(const struct bps_tree *tree, struct bps_block *block,
			   bps_tree_block_id_t id, int level,
			   size_t *calc_count,
			   bps_tree_block_id_t *expected_prev_id,
			   bps_tree_block_id_t *expected_this_id,
			   bool check_fullness)
{
	if (block->type != BPS_TREE_BT_LEAF && block->type != BPS_TREE_BT_INNER)
		return 0x10;
	if (block->type == BPS_TREE_BT_LEAF) {
		struct bps_leaf *leaf = (struct bps_leaf *)(block);
		int result = 0;
		if (check_fullness)
			if (block->size < BPS_TREE_MAX_COUNT_IN_LEAF * 2 / 3)
				result |= 0x1000000;
		*calc_count += block->size;
		if (id != *expected_this_id)
			result |= 0x10000;
		if (leaf->prev_id != *expected_prev_id)
			result |= 0x20000;
		*expected_prev_id = id;
		*expected_this_id = leaf->next_id;

		if (level != 1)
			result |= 0x100;
		if (block->size == 0)
			result |= 0x200;
		if (block->size > BPS_TREE_MAX_COUNT_IN_LEAF)
			result |= 0x200;
		for (bps_tree_pos_t i = 1; i < block->size; i++)
			if (BPS_TREE_COMPARE(leaf->elems[i - 1],
					     leaf->elems[i], tree->arg) >= 0)
				result |= 0x400;
		return result;
	} else {
		struct bps_inner *inner = (struct bps_inner *)(block);
		int result = 0;
		if (check_fullness)
			if (block->size < BPS_TREE_MAX_COUNT_IN_INNER * 2 / 3)
				result |= 0x2000000;
		if (block->size < 2)
			result |= 0x1000;
		if (block->size > BPS_TREE_MAX_COUNT_IN_INNER)
			result |= 0x1000;
		for (bps_tree_pos_t i = 1; i < block->size - 1; i++)
			if (BPS_TREE_COMPARE(inner->elems[i - 1],
					     inner->elems[i], tree->arg) >= 0)
				result |= 0x2000;
		for (bps_tree_pos_t i = 0; i < block->size - 1; i++) {
			struct bps_block *tmp_block =
				bps_tree_restore_block(tree,
						       inner->child_ids[i]);
			bps_tree_elem_t calc_max_elem =
				bps_tree_debug_find_max_elem(tree, tmp_block);
			if (inner->elems[i] != calc_max_elem)
				result |= 0x4000;
		}
		if (block->size > 1) {
			bps_tree_elem_t calc_max_elem =
				bps_tree_debug_find_max_elem(tree, block);
			if (BPS_TREE_COMPARE(inner->elems[block->size - 2],
					     calc_max_elem, tree->arg) >= 0)
				result |= 0x8000;
		}
		bool check_fullness_next = block->size > 2;
		if (block->size == 2) {
			bps_tree_pos_t block_max_size;
			if (level == 2)
				block_max_size = BPS_TREE_MAX_COUNT_IN_LEAF;
			else
				block_max_size = BPS_TREE_MAX_COUNT_IN_INNER;
			struct bps_block *child1 =
				bps_tree_restore_block(tree,
						       inner->child_ids[0]);
			struct bps_block *child2 =
				bps_tree_restore_block(tree,
						       inner->child_ids[1]);
			if (child1->size + child2->size <= block_max_size)
				result |= 0x4000000;
		}

		for (bps_tree_pos_t i = 0; i < block->size; i++)
			result |= bps_tree_debug_check_block(tree,
				bps_tree_restore_block(tree,
						       inner->child_ids[i]),
				inner->child_ids[i], level - 1, calc_count,
				expected_prev_id, expected_this_id,
				check_fullness_next);
		return result;
	}
}

/**
 * @brief A debug self-check.
 * Returns a bitmask of found errors (0 on success).
 * I hope you will not need it.
 * @param tree - pointer to a tree
 * @return - Bitwise-OR of all errors found
 */
static inline int
bps_tree_debug_check(const struct bps_tree *tree)
{
	int result = 0;
	if (tree->root_id == (bps_tree_block_id_t)(-1)) {
		if (tree->depth != 0)
			result |= 0x1;
		if (tree->size != 0)
			result |= 0x1;
		if (tree->leaf_count != 0 || tree->inner_count != 0)
			result |= 0x1;
		return result;
	}
	struct bps_block *root = bps_tree_root(tree);
	if (tree->max_elem != bps_tree_debug_find_max_elem(tree, root))
		result |= 0x8;
	size_t calc_count = 0;
	bps_tree_block_id_t expected_prev_id = (bps_tree_block_id_t)(-1);
	bps_tree_block_id_t expected_this_id = tree->first_id;
	result |= bps_tree_debug_check_block(tree, root, tree->root_id,
					     tree->depth, &calc_count,
					     &expected_prev_id,
					     &expected_this_id,
					     false);
	if (expected_this_id != (bps_tree_block_id_t)(-1))
		result |= 0x40000;
	if (expected_prev_id != tree->last_id)
		result |= 0x80000;
	if (tree->size != calc_count)
		result |= 0x4;
	return result;
}

/**
 * @brief Print an indent to distinguish levels of the tree in output.
 * @param level - current printing level of a tree.
 */
static inline void
bps_tree_print_indent(int level)
{
	for (int i = 0; i < level; i++)
		printf("  ");
}

/**
 * @brief Print a block of a tree.
 * @param tree - printing tree
 * @param block - block to print
 * @param level - current printing level
 * @param elem_fmt - printing format of elements
 */
static void
bps_tree_print_block(const struct bps_tree *tree,
		     const struct bps_block *block,
		     int level, const char *elem_fmt);

/**
 * @brief Print a leaf block of a tree.
 * @param block - block to print
 * @param level - current printing level
 * @param elem_fmt - printing format of elements
 */
static inline void
bps_tree_print_leaf(const struct bps_leaf* block, int indent,
		    const char *elem_fmt)
{
	bps_tree_print_indent(indent);
	printf("[(%d)", (int)block->header.size);
	for (bps_tree_pos_t i = 0; i < block->header.size; i++) {
		printf(" ");
		printf(elem_fmt, block->elems[i]);
	}
	printf("]\n");
}

/**
 * @brief Print an inner block of a tree. Recursively prints children.
 * @param tree - printing tree
 * @param block - block to print
 * @param level - current printing level
 * @param elem_fmt - printing format of elements
 */
static inline void
bps_tree_print_inner(const struct bps_tree *tree,
		     const struct bps_inner* block,
		     int indent, const char *elem_fmt)
{
	struct bps_block *next = bps_tree_restore_block(tree,
							block->child_ids[0]);
	bps_tree_print_block(tree, next, indent + 1, elem_fmt);
	for (bps_tree_pos_t i = 0; i < block->header.size - 1; i++) {
		bps_tree_print_indent(indent);
		printf(elem_fmt, block->elems[i]);
		printf("\n");
		next = bps_tree_restore_block(tree, block->child_ids[i + 1]);
		bps_tree_print_block(tree, next, indent + 1, elem_fmt);
	}
}

/**
 * @brief Print a block of a tree.
 * @param tree - printing tree
 * @param block - block to print
 * @param level - current printing level
 * @param elem_fmt - printing format of elements
 */
static inline void
bps_tree_print_block(const struct bps_tree *tree,
		     const struct bps_block *block,
		     int indent, const char *elem_fmt)
{
	if (block->type == BPS_TREE_BT_INNER)
		bps_tree_print_inner(tree,
				     (const struct bps_inner *)block,
				     indent, elem_fmt);
	else
		bps_tree_print_leaf((const struct bps_leaf *)block,
				    indent, elem_fmt);
}

/**
 * @brief Debug print tree to output in readable form.
 *  I hope you will not need it.
 * @param tree - tree to print
 * @param elem_fmt - format for printing an element. "%d" or "%p" for example.
 */
static inline void
bps_tree_print(const struct bps_tree *tree, const char *elem_fmt)
{
	if (tree->root_id == (bps_tree_block_id_t)(-1)) {
		printf("Empty\n");
		return;
	}
	bps_tree_print_block(tree, bps_tree_root(tree), 0, elem_fmt);
}

/*
 * Debug utilities for testing base operation on blocks:
 * inserting, deleting, moving to left and right blocks,
 * and (inserting and moving)
 */

/**
 * @brief Assign a value to an element.
 * Used for debug self-check
 */
static inline void
bps_tree_debug_set_elem(bps_tree_elem_t *elem, unsigned char c)
{
	memset(elem, 0, sizeof(bps_tree_elem_t));
	*(unsigned char *)elem = c;
}

/**
 * @brief Get previously assigned value from an element.
 * Used for debug self-check
 */
static inline unsigned char
bps_tree_debug_get_elem(bps_tree_elem_t *elem)
{
	return *(unsigned char *)elem;
}

/**
 * @brief Assign a value to an element in inner block.
 * Used for debug self-check
 */
static inline void
bps_tree_debug_set_elem_inner(struct bps_inner_path_elem *path_elem,
			      bps_tree_pos_t pos, unsigned char c)
{
	assert(pos >= 0);
	assert(pos < path_elem->block->header.size);
	if (pos < path_elem->block->header.size - 1
	    && pos < BPS_TREE_MAX_COUNT_IN_INNER - 1) /* fix gcc-4.9 warning */
		bps_tree_debug_set_elem(path_elem->block->elems + pos, c);
	else
		bps_tree_debug_set_elem(path_elem->max_elem_copy, c);
}

/**
 * @brief Get previously assigned value from an element in inner block.
 * Used for debug self-check
 */
static inline unsigned char
bps_tree_debug_get_elem_inner(const struct bps_inner_path_elem *path_elem,
			      bps_tree_pos_t pos)
{
	assert(pos >= 0);
	assert(pos < path_elem->block->header.size);
	if (pos < path_elem->block->header.size - 1
	    && pos < BPS_TREE_MAX_COUNT_IN_INNER - 1) /* fix gcc-4.9 warning */
		return bps_tree_debug_get_elem(path_elem->block->elems + pos);
	else
		return bps_tree_debug_get_elem(path_elem->max_elem_copy);
}

/**
 * @brief Check all possible insertions into a leaf.
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_insert_into_leaf(struct bps_tree *tree, bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_LEAF;
	for (unsigned int i = 0; i < szlim; i++) {
		for (unsigned int j = 0; j <= i; j++) {
			tree->size = 0;
			struct bps_leaf block;
			block.header.type = BPS_TREE_BT_LEAF;
			block.header.size = i;
			for (unsigned int k = 0; k < szlim; k++)
				if (k < j)
					bps_tree_debug_set_elem(block.elems + k,
						k);
				else
					bps_tree_debug_set_elem(block.elems + k,
						k + 1);
			struct bps_leaf_path_elem path_elem;
			bps_tree_elem_t max;
			bps_tree_elem_t ins;
			bps_tree_debug_set_elem(&max, i);
			bps_tree_debug_set_elem(&ins, j);
			path_elem.block = &block;
			path_elem.block_id = 0;
			path_elem.insertion_point = j;
			path_elem.max_elem_copy = &max;
			path_elem.max_elem_block_id = -1;
			path_elem.max_elem_pos = -1;

			bps_tree_insert_into_leaf(tree, &path_elem, ins);

			if (block.header.size != bps_tree_pos_t(i + 1)
				|| tree->size != bps_tree_pos_t(1)) {
				result |= (1 << 0);
				assert(!assertme);
			}
			if (bps_tree_debug_get_elem(&max)
				!= bps_tree_debug_get_elem(
					block.elems + block.header.size - 1)) {
				result |= (1 << 1);
				assert(!assertme);
			}
			for (unsigned int k = 0; k <= i; k++) {
				if (bps_tree_debug_get_elem(block.elems + k)
					!= (unsigned char) k) {
					result |= (1 << 1);
					assert(!assertme);
				}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible deleting from a leaf.
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_delete_from_leaf(struct bps_tree *tree, bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_LEAF;
	for (unsigned int i = 1; i <= szlim; i++) {
		for (unsigned int j = 0; j < i; j++) {
			tree->size = 1;
			struct bps_leaf block;
			block.header.type = BPS_TREE_BT_LEAF;
			block.header.size = i;
			for (unsigned int k = 0; k < i; k++)
				bps_tree_debug_set_elem(block.elems + k, k);
			struct bps_leaf_path_elem path_elem;
			bps_tree_elem_t max;
			bps_tree_debug_set_elem(&max,
				j == i - 1 ? i - 2 : i - 1);
			path_elem.block = &block;
			path_elem.block_id = 0;
			path_elem.insertion_point = j;
			path_elem.max_elem_copy = &max;
			path_elem.max_elem_block_id = -1;
			path_elem.max_elem_pos = -1;

			bps_tree_delete_from_leaf(tree, &path_elem);

			if (block.header.size != bps_tree_pos_t(i - 1)
				|| tree->size != bps_tree_pos_t(0)) {
				result |= (1 << 2);
				assert(!assertme);
			}
			if (i > 1
				&& bps_tree_debug_get_elem(&max)
					!= bps_tree_debug_get_elem(
						block.elems + block.header.size
							- 1)) {
				result |= (1 << 3);
				assert(!assertme);
			}
			for (unsigned int k = 0; k < i - 1; k++) {
				if (bps_tree_debug_get_elem(block.elems + k)
					!= (unsigned char) (k < j ? k : k + 1)) {
					result |= (1 << 3);
					assert(!assertme);
				}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible moving right of leafs.
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_move_to_right_leaf(struct bps_tree *tree, bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_LEAF;
	for (unsigned int i = 0; i <= szlim; i++) {
		for (unsigned int j = 0; j <= szlim; j++) {
			unsigned int max_move = i < szlim - j ? i : szlim - j;
			for (unsigned int k = 1; k <= max_move; k++) {
				struct bps_leaf a, b;
				a.header.type = BPS_TREE_BT_LEAF;
				a.header.size = i;
				b.header.type = BPS_TREE_BT_LEAF;
				b.header.size = j;
				memset(a.elems, 0xFF, sizeof(a.elems));
				memset(b.elems, 0xFF, sizeof(b.elems));
				unsigned char c = 0;
				for (unsigned int u = 0; u < i; u++)
					bps_tree_debug_set_elem(a.elems + u,
						c++);
				for (unsigned int u = 0; u < j; u++)
					bps_tree_debug_set_elem(b.elems + u,
						c++);
				bps_tree_elem_t ma;
				bps_tree_debug_set_elem(&ma, 0xFF);
				bps_tree_elem_t mb;
				bps_tree_debug_set_elem(&mb, 0xFF);
				if (i)
					ma = a.elems[i - 1];
				if (j)
					mb = b.elems[j - 1];

				struct bps_leaf_path_elem a_path_elem,
					b_path_elem;
				a_path_elem.block = &a;
				a_path_elem.max_elem_copy = &ma;
				a_path_elem.max_elem_block_id = -1;
				a_path_elem.max_elem_pos = -1;
				b_path_elem.block = &b;
				b_path_elem.max_elem_copy = &mb;
				b_path_elem.max_elem_block_id = -1;
				b_path_elem.max_elem_pos = -1;
				a_path_elem.block_id = 0;
				b_path_elem.block_id = 0;

				bps_tree_move_elems_to_right_leaf(tree,
					&a_path_elem, &b_path_elem,
					(bps_tree_pos_t) k);

				if (a.header.size != (bps_tree_pos_t) (i - k)) {
					result |= (1 << 4);
					assert(!assertme);
				}
				if (b.header.size != (bps_tree_pos_t) (j + k)) {
					result |= (1 << 4);
					assert(!assertme);
				}

				if (a.header.size)
					if (ma != a.elems[a.header.size - 1]) {
						result |= (1 << 5);
						assert(!assertme);
					}
				if (b.header.size)
					if (mb != b.elems[b.header.size - 1]) {
						result |= (1 << 5);
						assert(!assertme);
					}

				c = 0;
				for (unsigned int u = 0;
					u < (unsigned int) a.header.size; u++)
					if (bps_tree_debug_get_elem(a.elems + u)
						!= c++) {
						result |= (1 << 5);
						assert(!assertme);
					}
				for (unsigned int u = 0;
					u < (unsigned int) b.header.size; u++)
					if (bps_tree_debug_get_elem(b.elems + u)
						!= c++) {
						result |= (1 << 5);
						assert(!assertme);
					}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible moving left of leafs.
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_move_to_left_leaf(struct bps_tree *tree, bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_LEAF;
	for (unsigned int i = 0; i <= szlim; i++) {
		for (unsigned int j = 0; j <= szlim; j++) {
			unsigned int max_move = j < szlim - i ? j : szlim - i;
			for (unsigned int k = 1; k <= max_move; k++) {
				struct bps_leaf a, b;
				a.header.type = BPS_TREE_BT_LEAF;
				a.header.size = i;
				b.header.type = BPS_TREE_BT_LEAF;
				b.header.size = j;

				memset(a.elems, 0xFF, sizeof(a.elems));
				memset(b.elems, 0xFF, sizeof(b.elems));
				unsigned char c = 0;
				for (unsigned int u = 0; u < i; u++)
					bps_tree_debug_set_elem(a.elems + u,
						c++);
				for (unsigned int u = 0; u < j; u++)
					bps_tree_debug_set_elem(b.elems + u,
						c++);
				bps_tree_elem_t ma;
				bps_tree_debug_set_elem(&ma, 0xFF);
				bps_tree_elem_t mb;
				bps_tree_debug_set_elem(&mb, 0xFF);
				if (i)
					ma = a.elems[i - 1];
				if (j)
					mb = b.elems[j - 1];

				struct bps_leaf_path_elem a_path_elem,
					b_path_elem;
				a_path_elem.block = &a;
				a_path_elem.max_elem_copy = &ma;
				a_path_elem.max_elem_block_id = -1;
				a_path_elem.max_elem_pos = -1;
				b_path_elem.block = &b;
				b_path_elem.max_elem_copy = &mb;
				b_path_elem.max_elem_block_id = -1;
				b_path_elem.max_elem_pos = -1;
				a_path_elem.block_id = 0;
				b_path_elem.block_id = 0;

				bps_tree_move_elems_to_left_leaf(tree,
					&a_path_elem, &b_path_elem,
					(bps_tree_pos_t) k);

				if (a.header.size != (bps_tree_pos_t) (i + k)) {
					result |= (1 << 6);
					assert(!assertme);
				}
				if (b.header.size != (bps_tree_pos_t) (j - k)) {
					result |= (1 << 6);
					assert(!assertme);
				}

				if (a.header.size)
					if (ma != a.elems[a.header.size - 1]) {
						result |= (1 << 7);
						assert(!assertme);
					}
				if (b.header.size)
					if (mb != b.elems[b.header.size - 1]) {
						result |= (1 << 7);
						assert(!assertme);
					}

				c = 0;
				for (unsigned int u = 0;
					u < (unsigned int) a.header.size; u++)
					if (bps_tree_debug_get_elem(a.elems + u)
						!= c++) {
						result |= (1 << 7);
						assert(!assertme);
					}
				for (unsigned int u = 0;
					u < (unsigned int) b.header.size; u++)
					if (bps_tree_debug_get_elem(b.elems + u)
						!= c++) {
						result |= (1 << 7);
						assert(!assertme);
					}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible insertion and moving right of leafs.
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_insert_and_move_to_right_leaf(struct bps_tree *tree,
						   bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_LEAF;
	for (unsigned int i = 0; i <= szlim; i++) {
		for (unsigned int j = 0; j <= szlim; j++) {
			unsigned int max_move =
				i + 1 < szlim - j ? i + 1 : szlim - j;
			for (unsigned int k = 0; k <= i; k++) {
				for (unsigned int u = 1; u <= max_move; u++) {
					struct bps_leaf a, b;
					a.header.type = BPS_TREE_BT_LEAF;
					a.header.size = i;
					b.header.type = BPS_TREE_BT_LEAF;
					b.header.size = j;
					memset(a.elems, 0xFF, sizeof(a.elems));
					memset(b.elems, 0xFF, sizeof(b.elems));
					unsigned char c = 0;
					unsigned char ic = i + j;
					for (unsigned int v = 0; v < i; v++) {
						if (v == k)
							ic = c++;
						bps_tree_debug_set_elem(
							a.elems + v, c++);
					}
					if (k == i)
						ic = c++;
					for (unsigned int v = 0; v < j; v++)
						bps_tree_debug_set_elem(
							b.elems + v, c++);
					bps_tree_elem_t ma;
					bps_tree_debug_set_elem(&ma, 0xFF);
					bps_tree_elem_t mb;
					bps_tree_debug_set_elem(&mb, 0xFF);
					if (i)
						ma = a.elems[i - 1];
					if (j)
						mb = b.elems[j - 1];

					struct bps_leaf_path_elem a_path_elem,
						b_path_elem;
					a_path_elem.block = &a;
					a_path_elem.max_elem_copy = &ma;
					a_path_elem.max_elem_block_id = -1;
					a_path_elem.max_elem_pos = -1;
					b_path_elem.block = &b;
					b_path_elem.max_elem_copy = &mb;
					b_path_elem.max_elem_block_id = -1;
					b_path_elem.max_elem_pos = -1;
					a_path_elem.insertion_point = k;
					a_path_elem.block_id = 0;
					b_path_elem.block_id = 0;
					bps_tree_elem_t ins;
					bps_tree_debug_set_elem(&ins, ic);

					bps_tree_insert_and_move_elems_to_right_leaf(
						tree, &a_path_elem,
						&b_path_elem,
						(bps_tree_pos_t) u, ins);

					if (a.header.size
						!= (bps_tree_pos_t) (i - u + 1)) {
						result |= (1 << 8);
						assert(!assertme);
					}
					if (b.header.size
						!= (bps_tree_pos_t) (j + u)) {
						result |= (1 << 8);
						assert(!assertme);
					}

					if (i - u + 1)
						if (ma
							!= a.elems[a.header.size
								- 1]) {
							result |= (1 << 9);
							assert(!assertme);
						}
					if (j + u)
						if (mb
							!= b.elems[b.header.size
								- 1]) {
							result |= (1 << 9);
							assert(!assertme);
						}

					c = 0;
					for (unsigned int v = 0;
						v < (unsigned int) a.header.size;
						v++)
						if (bps_tree_debug_get_elem(
							a.elems + v) != c++) {
							result |= (1 << 9);
							assert(!assertme);
						}
					for (unsigned int v = 0;
						v < (unsigned int) b.header.size;
						v++)
						if (bps_tree_debug_get_elem(
							b.elems + v) != c++) {
							result |= (1 << 9);
							assert(!assertme);
						}
				}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible insertion and moving left of leafs.
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_insert_and_move_to_left_leaf(struct bps_tree *tree,
						  bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_LEAF;
	for (unsigned int i = 0; i <= szlim; i++) {
		for (unsigned int j = 0; j <= szlim; j++) {
			unsigned int max_move =
				j + 1 < szlim - i ? j + 1 : szlim - i;
			for (unsigned int k = 0; k <= j; k++) {
				for (unsigned int u = 1; u <= max_move; u++) {
					struct bps_leaf a, b;
					a.header.type = BPS_TREE_BT_LEAF;
					a.header.size = i;
					b.header.type = BPS_TREE_BT_LEAF;
					b.header.size = j;
					memset(a.elems, 0xFF, sizeof(a.elems));
					memset(b.elems, 0xFF, sizeof(b.elems));
					unsigned char c = 0;
					unsigned char ic = i + j;
					for (unsigned int v = 0; v < i; v++)
						bps_tree_debug_set_elem(
							a.elems + v, c++);
					for (unsigned int v = 0; v < j; v++) {
						if (v == k)
							ic = c++;
						bps_tree_debug_set_elem(
							b.elems + v, c++);
					}
					bps_tree_elem_t ma;
					bps_tree_debug_set_elem(&ma, 0xFF);
					bps_tree_elem_t mb;
					bps_tree_debug_set_elem(&mb, 0xFF);
					if (i)
						ma = a.elems[i - 1];
					if (j)
						mb = b.elems[j - 1];

					struct bps_leaf_path_elem a_path_elem,
						b_path_elem;
					a_path_elem.block = &a;
					a_path_elem.max_elem_copy = &ma;
					a_path_elem.max_elem_block_id = -1;
					a_path_elem.max_elem_pos = -1;
					b_path_elem.block = &b;
					b_path_elem.max_elem_copy = &mb;
					b_path_elem.max_elem_block_id = -1;
					b_path_elem.max_elem_pos = -1;
					b_path_elem.insertion_point = k;
					a_path_elem.block_id = 0;
					b_path_elem.block_id = 0;
					bps_tree_elem_t ins;
					bps_tree_debug_set_elem(&ins, ic);

					bps_tree_insert_and_move_elems_to_left_leaf(
						tree, &a_path_elem,
						&b_path_elem,
						(bps_tree_pos_t) u, ins);

					if (a.header.size
						!= (bps_tree_pos_t) (i + u)) {
						result |= (1 << 10);
						assert(!assertme);
					}
					if (b.header.size
						!= (bps_tree_pos_t) (j - u + 1)) {
						result |= (1 << 10);
						assert(!assertme);
					}

					if (i + u)
						if (ma
							!= a.elems[a.header.size
								- 1]) {
							result |= (1 << 11);
							assert(!assertme);
						}
					if (j - u + 1)
						if (mb
							!= b.elems[b.header.size
								- 1]) {
							result |= (1 << 11);
							assert(!assertme);
						}

					c = 0;
					for (unsigned int v = 0;
						v < (unsigned int) a.header.size;
						v++)
						if (bps_tree_debug_get_elem(
							a.elems + v) != c++) {
							result |= (1 << 11);
							assert(!assertme);
						}
					for (unsigned int v = 0;
						v < (unsigned int) b.header.size;
						v++)
						if (bps_tree_debug_get_elem(
							b.elems + v) != c++) {
							result |= (1 << 11);
							assert(!assertme);
						}
				}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible insertion to an inner
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_insert_into_inner(struct bps_tree *tree, bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_INNER;
	for (unsigned int i = 0; i < szlim; i++) {
		for (unsigned int j = 0; j <= i; j++) {
			tree->size = 0;

			struct bps_inner block;
			block.header.type = BPS_TREE_BT_INNER;
			block.header.size = i;
			memset(block.elems, 0xFF, sizeof(block.elems));
			memset(block.child_ids, 0xFF, sizeof(block.child_ids));

			bps_tree_elem_t max;
			bps_tree_elem_t ins;
			bps_tree_debug_set_elem(&ins, j);

			struct bps_inner_path_elem path_elem;
			path_elem.block = &block;
			path_elem.block_id = 0;
			path_elem.max_elem_copy = &max;
			path_elem.max_elem_block_id = -1;
			path_elem.max_elem_pos = -1;

			for (unsigned int k = 0; k < i; k++) {
				if (k < j)
					bps_tree_debug_set_elem_inner(
						&path_elem, k, k);
				else
					bps_tree_debug_set_elem_inner(
						&path_elem, k, k + 1);
			}
			for (unsigned int k = 0; k < i; k++)
				if (k < j)
					block.child_ids[k] =
						(bps_tree_block_id_t) k;
				else
					block.child_ids[k] =
						(bps_tree_block_id_t) (k + 1);

			bps_tree_insert_into_inner(tree, &path_elem,
				(bps_tree_block_id_t) j, (bps_tree_pos_t) j,
				ins);

			for (unsigned int k = 0; k <= i; k++) {
				if (bps_tree_debug_get_elem_inner(&path_elem, k)
					!= (unsigned char) k) {
					result |= (1 << 12);
					assert(!assertme);
				}
			}
			for (unsigned int k = 0; k <= i; k++) {
				if (block.child_ids[k] != k) {
					result |= (1 << 13);
					assert(!assertme);
				}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible deletions from an inner
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_delete_from_inner(struct bps_tree *tree, bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_INNER;
	for (unsigned int i = 1; i <= szlim; i++) {
		for (unsigned int j = 0; j < i; j++) {
			struct bps_inner block;
			block.header.type = BPS_TREE_BT_INNER;
			block.header.size = i;
			for (unsigned int k = 0; k < szlim - 1; k++)
				bps_tree_debug_set_elem(block.elems + k, k);
			for (unsigned int k = 0; k < szlim; k++)
				block.child_ids[k] = k;
			struct bps_inner_path_elem path_elem;
			bps_tree_elem_t max;
			bps_tree_debug_set_elem(&max, i - 1);
			path_elem.block = &block;
			path_elem.block_id = 0;
			path_elem.insertion_point = j;
			path_elem.max_elem_copy = &max;
			path_elem.max_elem_block_id = -1;
			path_elem.max_elem_pos = -1;

			bps_tree_delete_from_inner(tree, &path_elem);

			unsigned char c = 0;
			bps_tree_block_id_t kk = 0;
			for (unsigned int k = 0; k < i - 1; k++) {
				if (k == j) {
					c++;
					kk++;
				}
				if (bps_tree_debug_get_elem_inner(&path_elem, k)
					!= c++) {
					result |= (1 << 14);
					assert(!assertme);
				}
				if (block.child_ids[k] != kk++) {
					result |= (1 << 15);
					assert(!assertme);
				}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible moving right of inners
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_move_to_right_inner(struct bps_tree *tree, bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_INNER;
	for (unsigned int i = 0; i <= szlim; i++) {
		for (unsigned int j = 0; j <= szlim; j++) {
			unsigned int max_move = i < szlim - j ? i : szlim - j;
			for (unsigned int k = 1; k <= max_move; k++) {
				struct bps_inner a, b;
				a.header.type = BPS_TREE_BT_INNER;
				a.header.size = i;
				b.header.type = BPS_TREE_BT_INNER;
				b.header.size = j;
				memset(a.elems, 0xFF, sizeof(a.elems));
				memset(b.elems, 0xFF, sizeof(b.elems));
				memset(a.child_ids, 0xFF, sizeof(a.child_ids));
				memset(b.child_ids, 0xFF, sizeof(b.child_ids));

				bps_tree_elem_t ma;
				bps_tree_debug_set_elem(&ma, 0xFF);
				bps_tree_elem_t mb;
				bps_tree_debug_set_elem(&mb, 0xFF);

				struct bps_inner_path_elem a_path_elem, b_path_elem;
				a_path_elem.block = &a;
				a_path_elem.max_elem_copy = &ma;
				a_path_elem.max_elem_block_id = -1;
				a_path_elem.max_elem_pos = -1;
				b_path_elem.block = &b;
				b_path_elem.max_elem_copy = &mb;
				b_path_elem.max_elem_block_id = -1;
				b_path_elem.max_elem_pos = -1;
				a_path_elem.block_id = 0;
				b_path_elem.block_id = 0;

				unsigned char c = 0;
				bps_tree_block_id_t kk = 0;
				for (unsigned int u = 0; u < i; u++) {
					bps_tree_debug_set_elem_inner(
						&a_path_elem, u, c++);
					a.child_ids[u] = kk++;
				}
				for (unsigned int u = 0; u < j; u++) {
					bps_tree_debug_set_elem_inner(
						&b_path_elem, u, c++);
					b.child_ids[u] = kk++;
				}

				bps_tree_move_elems_to_right_inner(tree,
					&a_path_elem, &b_path_elem,
					(bps_tree_pos_t) k);

				if (a.header.size != (bps_tree_pos_t) (i - k)) {
					result |= (1 << 16);
					assert(!assertme);
				}
				if (b.header.size != (bps_tree_pos_t) (j + k)) {
					result |= (1 << 16);
					assert(!assertme);
				}

				c = 0;
				kk = 0;
				for (unsigned int u = 0;
					u < (unsigned int) a.header.size; u++) {
					if (bps_tree_debug_get_elem_inner(
						&a_path_elem, u) != c++) {
						result |= (1 << 17);
						assert(!assertme);
					}
					if (a.child_ids[u] != kk++) {
						result |= (1 << 17);
						assert(!assertme);
					}
				}
				for (unsigned int u = 0;
					u < (unsigned int) b.header.size; u++) {
					if (bps_tree_debug_get_elem_inner(
						&b_path_elem, u) != c++) {
						result |= (1 << 17);
						assert(!assertme);
					}
					if (b.child_ids[u] != kk++) {
						result |= (1 << 17);
						assert(!assertme);
					}
				}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible moving left of inners
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_move_to_left_inner(struct bps_tree *tree, bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_INNER;
	for (unsigned int i = 0; i <= szlim; i++) {
		for (unsigned int j = 0; j <= szlim; j++) {
			unsigned int max_move = j < szlim - i ? j : szlim - i;
			for (unsigned int k = 1; k <= max_move; k++) {
				struct bps_inner a, b;
				a.header.type = BPS_TREE_BT_INNER;
				a.header.size = i;
				b.header.type = BPS_TREE_BT_INNER;
				b.header.size = j;
				memset(a.elems, 0xFF, sizeof(a.elems));
				memset(b.elems, 0xFF, sizeof(b.elems));
				memset(a.child_ids, 0xFF, sizeof(a.child_ids));
				memset(b.child_ids, 0xFF, sizeof(b.child_ids));

				bps_tree_elem_t ma;
				bps_tree_debug_set_elem(&ma, 0xFF);
				bps_tree_elem_t mb;
				bps_tree_debug_set_elem(&mb, 0xFF);

				struct bps_inner_path_elem a_path_elem, b_path_elem;
				a_path_elem.block = &a;
				a_path_elem.max_elem_copy = &ma;
				a_path_elem.max_elem_block_id = -1;
				a_path_elem.max_elem_pos = -1;
				b_path_elem.block = &b;
				b_path_elem.max_elem_copy = &mb;
				b_path_elem.max_elem_block_id = -1;
				b_path_elem.max_elem_pos = -1;
				a_path_elem.block_id = 0;
				b_path_elem.block_id = 0;

				unsigned char c = 0;
				bps_tree_block_id_t kk = 0;
				for (unsigned int u = 0; u < i; u++) {
					bps_tree_debug_set_elem_inner(
						&a_path_elem, u, c++);
					a.child_ids[u] = kk++;
				}
				for (unsigned int u = 0; u < j; u++) {
					bps_tree_debug_set_elem_inner(
						&b_path_elem, u, c++);
					b.child_ids[u] = kk++;
				}

				bps_tree_move_elems_to_left_inner(tree,
					&a_path_elem, &b_path_elem,
					(bps_tree_pos_t) k);

				if (a.header.size != (bps_tree_pos_t) (i + k)) {
					result |= (1 << 18);
					assert(!assertme);
				}
				if (b.header.size != (bps_tree_pos_t) (j - k)) {
					result |= (1 << 18);
					assert(!assertme);
				}

				c = 0;
				kk = 0;
				for (unsigned int u = 0;
					u < (unsigned int) a.header.size; u++) {
					if (bps_tree_debug_get_elem_inner(
						&a_path_elem, u) != c++) {
						result |= (1 << 19);
						assert(!assertme);
					}
					if (a.child_ids[u] != kk++) {
						result |= (1 << 19);
						assert(!assertme);
					}
				}
				for (unsigned int u = 0;
					u < (unsigned int) b.header.size; u++) {
					if (bps_tree_debug_get_elem_inner(
						&b_path_elem, u) != c++) {
						result |= (1 << 19);
						assert(!assertme);
					}
					if (b.child_ids[u] != kk++) {
						result |= (1 << 19);
						assert(!assertme);
					}
				}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible insertion and moving right of inners
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_insert_and_move_to_right_inner(struct bps_tree *tree,
						    bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_INNER;
	for (unsigned int i = 0; i <= szlim; i++) {
		for (unsigned int j = 0; j <= szlim; j++) {
			unsigned int max_move =
				i + 1 < szlim - j ? i + 1 : szlim - j;
			for (unsigned int k = 0; k <= i; k++) {
				for (unsigned int u = 1; u <= max_move; u++) {
					struct bps_inner a, b;
					a.header.type = BPS_TREE_BT_INNER;
					a.header.size = i;
					b.header.type = BPS_TREE_BT_INNER;
					b.header.size = j;
					memset(a.elems, 0xFF, sizeof(a.elems));
					memset(b.elems, 0xFF, sizeof(b.elems));
					memset(a.child_ids, 0xFF,
						sizeof(a.child_ids));
					memset(b.child_ids, 0xFF,
						sizeof(b.child_ids));

					bps_tree_elem_t ma;
					bps_tree_debug_set_elem(&ma, 0xFF);
					bps_tree_elem_t mb;
					bps_tree_debug_set_elem(&mb, 0xFF);

					struct bps_inner_path_elem a_path_elem,
						b_path_elem;
					a_path_elem.block = &a;
					a_path_elem.max_elem_copy = &ma;
					a_path_elem.max_elem_block_id = -1;
					a_path_elem.max_elem_pos = -1;
					b_path_elem.block = &b;
					b_path_elem.max_elem_copy = &mb;
					b_path_elem.max_elem_block_id = -1;
					b_path_elem.max_elem_pos = -1;
					a_path_elem.block_id = 0;
					b_path_elem.block_id = 0;

					unsigned char c = 0;
					bps_tree_block_id_t kk = 0;
					unsigned char ic = i + j;
					bps_tree_block_id_t ikk =
						(bps_tree_block_id_t) (i + j);

					for (unsigned int v = 0; v < i; v++) {
						if (v == k) {
							ic = c++;
							ikk = kk++;
						}
						bps_tree_debug_set_elem_inner(
							&a_path_elem, v, c++);
						a.child_ids[v] = kk++;
					}
					if (k == i) {
						ic = c++;
						ikk = kk++;
					}
					for (unsigned int v = 0; v < j; v++) {
						bps_tree_debug_set_elem_inner(
							&b_path_elem, v, c++);
						b.child_ids[v] = kk++;
					}

					a_path_elem.insertion_point = -1;
					bps_tree_elem_t ins;
					bps_tree_debug_set_elem(&ins, ic);

					bps_tree_insert_and_move_elems_to_right_inner(
						tree, &a_path_elem,
						&b_path_elem,
						(bps_tree_pos_t) u, ikk,
						(bps_tree_pos_t) k, ins);

					if (a.header.size
						!= (bps_tree_pos_t) (i - u + 1)) {
						result |= (1 << 20);
						assert(!assertme);
					}
					if (b.header.size
						!= (bps_tree_pos_t) (j + u)) {
						result |= (1 << 20);
						assert(!assertme);
					}

					c = 0;
					kk = 0;
					for (unsigned int v = 0;
						v < (unsigned int) a.header.size;
						v++) {
						if (bps_tree_debug_get_elem_inner(
							&a_path_elem, v)
							!= c++) {
							result |= (1 << 21);
							assert(!assertme);
						}
						if (a.child_ids[v] != kk++) {
							result |= (1 << 21);
							assert(!assertme);
						}
					}
					for (unsigned int v = 0;
						v < (unsigned int) b.header.size;
						v++) {
						if (bps_tree_debug_get_elem_inner(
							&b_path_elem, v)
							!= c++) {
							result |= (1 << 21);
							assert(!assertme);
						}
						if (b.child_ids[v] != kk++) {
							result |= (1 << 21);
							assert(!assertme);
						}
					}

				}
			}
		}
	}
	return result;
}

/**
 * @brief Check all possible insertion and moving left of inners
 * Used for debug self-check
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_insert_and_move_to_left_inner(struct bps_tree *tree,
						   bool assertme)
{
	(void) assertme;
	int result = 0;
	const unsigned int szlim = BPS_TREE_MAX_COUNT_IN_INNER;
	for (unsigned int i = 0; i <= szlim; i++) {
		for (unsigned int j = 0; j <= szlim; j++) {
			unsigned int max_move =
				j + 1 < szlim - i ? j + 1 : szlim - i;
			for (unsigned int k = 0; k <= j; k++) {
				for (unsigned int u = 1; u <= max_move; u++) {
					struct bps_inner a, b;
					a.header.type = BPS_TREE_BT_INNER;
					a.header.size = i;
					b.header.type = BPS_TREE_BT_INNER;
					b.header.size = j;
					memset(a.elems, 0xFF, sizeof(a.elems));
					memset(b.elems, 0xFF, sizeof(b.elems));
					memset(a.child_ids, 0xFF,
						sizeof(a.child_ids));
					memset(b.child_ids, 0xFF,
						sizeof(b.child_ids));

					bps_tree_elem_t ma;
					bps_tree_debug_set_elem(&ma, 0xFF);
					bps_tree_elem_t mb;
					bps_tree_debug_set_elem(&mb, 0xFF);

					struct bps_inner_path_elem a_path_elem,
						b_path_elem;
					a_path_elem.block = &a;
					a_path_elem.max_elem_copy = &ma;
					a_path_elem.max_elem_block_id = -1;
					a_path_elem.max_elem_pos = -1;
					b_path_elem.block = &b;
					b_path_elem.max_elem_copy = &mb;
					b_path_elem.max_elem_block_id = -1;
					b_path_elem.max_elem_pos = -1;
					a_path_elem.block_id = 0;
					b_path_elem.block_id = 0;

					unsigned char c = 0;
					bps_tree_block_id_t kk = 0;
					unsigned char ic = i + j;
					bps_tree_block_id_t ikk =
						(bps_tree_block_id_t) (i + j);
					for (unsigned int v = 0; v < i; v++) {
						bps_tree_debug_set_elem_inner(
							&a_path_elem, v, c++);
						a.child_ids[v] = kk++;
					}
					for (unsigned int v = 0; v < j; v++) {
						if (v == k) {
							ic = c++;
							ikk = kk++;
						}
						bps_tree_debug_set_elem_inner(
							&b_path_elem, v, c++);
						b.child_ids[v] = kk++;
					}

					b_path_elem.insertion_point = -1;
					bps_tree_elem_t ins;
					bps_tree_debug_set_elem(&ins, ic);

					bps_tree_insert_and_move_elems_to_left_inner(
						tree, &a_path_elem,
						&b_path_elem,
						(bps_tree_pos_t) u, ikk,
						(bps_tree_pos_t) k, ins);

					if (a.header.size
						!= (bps_tree_pos_t) (i + u)) {
						result |= (1 << 22);
						assert(!assertme);
					}
					if (b.header.size
						!= (bps_tree_pos_t) (j - u + 1)) {
						result |= (1 << 22);
						assert(!assertme);
					}

					c = 0;
					kk = 0;
					for (unsigned int v = 0;
						v < (unsigned int) a.header.size;
						v++) {
						if (bps_tree_debug_get_elem_inner(
							&a_path_elem, v)
							!= c++) {
							result |= (1 << 23);
							assert(!assertme);
						}
						if (a.child_ids[v] != kk++) {
							result |= (1 << 23);
							assert(!assertme);
						}
					}
					for (unsigned int v = 0;
						v < (unsigned int) b.header.size;
						v++) {
						if (bps_tree_debug_get_elem_inner(
							&b_path_elem, v)
							!= c++) {
							result |= (1 << 23);
							assert(!assertme);
						}
						if (b.child_ids[v] != kk++) {
							result |= (1 << 23);
							assert(!assertme);
						}
					}

				}
			}
		}
	}
	return result;
}

/**
 * @brief Debug print tree to output in readable form.
 *  I hope you will not need it.
 * @param assertme - if true, errors will lead to assert call,
 *  if false, just error code will be returned.
 * @return 0 if OK; bit mask of errors otherwise.
 */
static inline int
bps_tree_debug_check_internal_functions(bool assertme)
{
	int result = 0;

	struct bps_tree tree;
	tree.root_id = (bps_tree_block_id_t) -1;

	result |= bps_tree_debug_check_insert_into_leaf(&tree, assertme);
	result |= bps_tree_debug_check_delete_from_leaf(&tree, assertme);
	result |= bps_tree_debug_check_move_to_right_leaf(&tree, assertme);
	result |= bps_tree_debug_check_move_to_left_leaf(&tree, assertme);
	result |= bps_tree_debug_check_insert_and_move_to_right_leaf(&tree,
								     assertme);
	result |= bps_tree_debug_check_insert_and_move_to_left_leaf(&tree,
								    assertme);

	result |= bps_tree_debug_check_insert_into_inner(&tree, assertme);
	result |= bps_tree_debug_check_delete_from_inner(&tree, assertme);
	result |= bps_tree_debug_check_move_to_right_inner(&tree, assertme);
	result |= bps_tree_debug_check_move_to_left_inner(&tree, assertme);
	result |= bps_tree_debug_check_insert_and_move_to_right_inner(&tree,
								     assertme);
	result |= bps_tree_debug_check_insert_and_move_to_left_inner(&tree,
								     assertme);
	return result;
}
#endif //#ifndef BPS_TREE_NO_DEBUG
/* }}} */

#undef BPS_TREE_MEMMOVE
#undef BPS_TREE_DATAMOVE
#undef BPS_TREE_BRANCH_TRACE

/* {{{ Macros for custom naming of structs and functions */
#undef _bps
#undef _bps_tree
#undef _BPS
#undef _BPS_TREE
#undef _bps_tree_name

#undef bps_tree
#undef bps_block
#undef bps_leaf
#undef bps_inner
#undef bps_garbage
#undef bps_tree_iterator
#undef bps_inner_path_elem
#undef bps_leaf_path_elem

#undef bps_tree_create
#undef bps_tree_build
#undef bps_tree_destroy
#undef bps_tree_find
#undef bps_tree_insert
#undef bps_tree_delete
#undef bps_tree_size
#undef bps_tree_mem_used
#undef bps_tree_random
#undef bps_tree_invalid_iterator
#undef bps_tree_iterator_is_invalid
#undef bps_tree_iterator_are_equal
#undef bps_tree_iterator_first
#undef bps_tree_iterator_last
#undef bps_tree_lower_bound
#undef bps_tree_upper_bound
#undef bps_tree_approximate_count
#undef bps_tree_iterator_get_elem
#undef bps_tree_iterator_next
#undef bps_tree_iterator_prev
#undef bps_tree_iterator_freeze
#undef bps_tree_iterator_destroy
#undef bps_tree_debug_check
#undef bps_tree_print
#undef bps_tree_debug_check_internal_functions

#undef bps_tree_max_sizes
#undef BPS_TREE_MAX_COUNT_IN_LEAF
#undef BPS_TREE_MAX_COUNT_IN_INNER
#undef BPS_TREE_MAX_DEPTH
#undef bps_block_type
#undef BPS_TREE_BT_GARBAGE
#undef BPS_TREE_BT_INNER
#undef BPS_TREE_BT_LEAF

#undef bps_tree_restore_block
#undef bps_tree_restore_block_ver
#undef bps_tree_root
#undef bps_tree_touch_block
#undef bps_tree_find_ins_point_key
#undef bps_tree_find_ins_point_elem
#undef bps_tree_find_after_ins_point_key
#undef bps_tree_get_leaf_safe
#undef bps_tree_garbage_push
#undef bps_tree_garbage_pop
#undef bps_tree_create_leaf
#undef bps_tree_create_inner
#undef bps_tree_dispose_leaf
#undef bps_tree_dispose_inner
#undef bps_tree_reserve_blocks
#undef bps_tree_insert_first_elem
#undef bps_tree_collect_path
#undef bps_tree_touch_leaf_path_max_elem
#undef bps_tree_touch_path
#undef bps_tree_process_replace
#undef bps_tree_debug_memmove
#undef bps_tree_insert_into_leaf
#undef bps_tree_insert_into_inner
#undef bps_tree_delete_from_leaf
#undef bps_tree_delete_from_inner
#undef bps_tree_move_elems_to_right_leaf
#undef bps_tree_move_elems_to_right_inner
#undef bps_tree_move_elems_to_left_leaf
#undef bps_tree_move_elems_to_left_inner
#undef bps_tree_insert_and_move_elems_to_right_leaf
#undef bps_tree_insert_and_move_elems_to_right_inner
#undef bps_tree_insert_and_move_elems_to_left_leaf
#undef bps_tree_insert_and_move_elems_to_left_inner
#undef bps_tree_leaf_free_size
#undef bps_tree_inner_free_size
#undef bps_tree_leaf_overmin_size
#undef bps_tree_inner_overmin_size
#undef bps_tree_collect_left_path_elem_leaf
#undef bps_tree_collect_left_path_elem_inner
#undef bps_tree_collect_right_ext_leaf
#undef bps_tree_collect_right_ext_inner
#undef bps_tree_prepare_new_ext_leaf
#undef bps_tree_prepare_new_ext_inner
#undef bps_tree_process_insert_leaf
#undef bps_tree_process_insert_inner
#undef bps_tree_process_delete_leaf
#undef bps_tree_process_delete_inner
#undef bps_tree_debug_find_max_elem
#undef bps_tree_debug_check_block
#undef bps_tree_print_indent
#undef bps_tree_print_block
#undef bps_tree_print_leaf
#undef bps_tree_print_inner
#undef bps_tree_debug_set_elem
#undef bps_tree_debug_get_elem
#undef bps_tree_debug_set_elem_inner
#undef bps_tree_debug_get_elem_inner
#undef bps_tree_debug_check_insert_into_leaf
#undef bps_tree_debug_check_delete_from_leaf
#undef bps_tree_debug_check_move_to_right_leaf
#undef bps_tree_debug_check_move_to_left_leaf
#undef bps_tree_debug_check_insert_and_move_to_right_leaf
#undef bps_tree_debug_check_insert_and_move_to_left_leaf
#undef bps_tree_debug_check_insert_into_inner
#undef bps_tree_debug_check_delete_from_inner
#undef bps_tree_debug_check_move_to_right_inner
#undef bps_tree_debug_check_move_to_left_inner
#undef bps_tree_debug_check_insert_and_move_to_right_inner
#undef bps_tree_debug_check_insert_and_move_to_left_inner
/* }}} */
