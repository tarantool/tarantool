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
#include "memtx_tree.h"
#include "memtx_engine.h"
#include "memtx_tuple_compression.h"
#include "space.h"
#include "schema.h" /* space_by_id(), space_cache_find() */
#include "errinj.h"
#include "memory.h"
#include "fiber.h"
#include "key_list.h"
#include "tuple.h"
#include "txn.h"
#include "memtx_tx.h"
#include "trivia/config.h"
#include "trivia/util.h"
#include "tt_sort.h"
#include <small/mempool.h>

/**
 * Struct that is used as a key in BPS tree definition.
 */
struct memtx_tree_key_data_common {
	/** Sequence of msgpacked search fields. */
	const char *key;
	/** Number of msgpacked search fields. */
	uint32_t part_count;
};

template <bool USE_HINT>
struct memtx_tree_key_data;

template <>
struct memtx_tree_key_data<false> : memtx_tree_key_data_common {
	static constexpr hint_t hint = HINT_NONE;
	void set_hint(hint_t) { assert(false); }
};

template <>
struct memtx_tree_key_data<true> : memtx_tree_key_data_common {
	/** Comparison hint, see tuple_hint(). */
	hint_t hint;
	void set_hint(hint_t h) { hint = h; }
};

/**
 * Struct that is used as a elem in BPS tree definition.
 */
struct memtx_tree_data_common {
	/* Tuple that this node is represents. */
	struct tuple *tuple;
};

template <bool USE_HINT>
struct memtx_tree_data;

template <>
struct memtx_tree_data<false> : memtx_tree_data_common {
	static constexpr hint_t hint = HINT_NONE;
	void set_hint(hint_t) { assert(false); }
};

template <>
struct memtx_tree_data<true> :  memtx_tree_data<false> {
	/** Comparison hint, see key_hint(). */
	hint_t hint;
	void set_hint(hint_t h) { hint = h; }
};

/**
 * Test whether BPS tree elements are identical i.e. represent
 * the same tuple at the same position in the tree.
 * @param a - First BPS tree element to compare.
 * @param b - Second BPS tree element to compare.
 * @retval true - When elements a and b are identical.
 * @retval false - Otherwise.
 */
static bool
memtx_tree_data_is_equal(const struct memtx_tree_data_common *a,
			 const struct memtx_tree_data_common *b)
{
	return a->tuple == b->tuple;
}

#define BPS_INNER_CARD
#define BPS_TREE_NAME memtx_tree
#define BPS_TREE_BLOCK_SIZE (512)
#define BPS_TREE_EXTENT_SIZE MEMTX_EXTENT_SIZE
#define BPS_TREE_COMPARE(a, b, arg)\
	tuple_compare((&a)->tuple, (&a)->hint, (&b)->tuple, (&b)->hint, arg)
#define BPS_TREE_COMPARE_KEY(a, b, arg)\
	tuple_compare_with_key((&a)->tuple, (&a)->hint, (b)->key,\
			       (b)->part_count, (b)->hint, arg)
#define BPS_TREE_IS_IDENTICAL(a, b) memtx_tree_data_is_equal(&a, &b)
#define BPS_TREE_NO_DEBUG 1
#define bps_tree_arg_t struct key_def *

#define BPS_TREE_NAMESPACE NS_NO_HINT
#define bps_tree_elem_t struct memtx_tree_data<false>
#define bps_tree_key_t struct memtx_tree_key_data<false> *

#include "salad/bps_tree.h"

#undef BPS_TREE_NAMESPACE
#undef bps_tree_elem_t
#undef bps_tree_key_t

#define BPS_TREE_NAMESPACE NS_USE_HINT
#define bps_tree_elem_t struct memtx_tree_data<true>
#define bps_tree_key_t struct memtx_tree_key_data<true> *

#include "salad/bps_tree.h"

#undef BPS_TREE_NAMESPACE
#undef bps_tree_elem_t
#undef bps_tree_key_t

#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef BPS_TREE_IS_IDENTICAL
#undef BPS_TREE_NO_DEBUG
#undef bps_tree_arg_t

using namespace NS_NO_HINT;
using namespace NS_USE_HINT;

template <bool USE_HINT>
struct memtx_tree_selector;

template <>
struct memtx_tree_selector<false> : NS_NO_HINT::memtx_tree {};

template <>
struct memtx_tree_selector<true> : NS_USE_HINT::memtx_tree {};

template <bool USE_HINT>
using memtx_tree_t = struct memtx_tree_selector<USE_HINT>;

template <bool USE_HINT>
struct memtx_tree_view_selector;

template <>
struct memtx_tree_view_selector<false> : NS_NO_HINT::memtx_tree_view {};

template <>
struct memtx_tree_view_selector<true> : NS_USE_HINT::memtx_tree_view {};

template <bool USE_HINT>
using memtx_tree_view_t = struct memtx_tree_view_selector<USE_HINT>;

template <bool USE_HINT>
struct memtx_tree_iterator_selector;

template <>
struct memtx_tree_iterator_selector<false> {
	using type = NS_NO_HINT::memtx_tree_iterator;
};

template <>
struct memtx_tree_iterator_selector<true> {
	using type = NS_USE_HINT::memtx_tree_iterator;
};

template <bool USE_HINT>
using memtx_tree_iterator_t = typename memtx_tree_iterator_selector<USE_HINT>::type;

static void
invalidate_tree_iterator(NS_NO_HINT::memtx_tree_iterator *itr)
{
	*itr = NS_NO_HINT::memtx_tree_invalid_iterator();
}

static void
invalidate_tree_iterator(NS_USE_HINT::memtx_tree_iterator *itr)
{
	*itr = NS_USE_HINT::memtx_tree_invalid_iterator();
}

template <bool USE_HINT>
struct memtx_tree_index {
	struct index base;
	memtx_tree_t<USE_HINT> tree;
	struct memtx_tree_data<USE_HINT> *build_array;
	size_t build_array_size, build_array_alloc_size;
	struct memtx_gc_task gc_task;
	memtx_tree_iterator_t<USE_HINT> gc_iterator;
	/** Whether index is functional. */
	bool is_func;
};

/* {{{ Utilities. *************************************************/

/**
 * Verifies the lookup options, canonicalizes the iterator type and key.
 *
 * @retval 0 on success;
 * @retval -1 on verification failure.
 */
static int
canonicalize_lookup(struct index_def *def, enum iterator_type *type,
		    const char **key, uint32_t part_count)
{
	assert(part_count == 0 || *key != NULL);
	assert(*type >= 0 && *type < iterator_type_MAX);

	static_assert(iterator_type_MAX < 32, "Too big for bit logic");
	const uint32_t supported_mask = ((1u << (ITER_GT + 1)) - 1) |
		(1u << ITER_NP) | (1u << ITER_PP);
	if (((1u << *type) & supported_mask) == 0) {
		diag_set(UnsupportedIndexFeature, def,
			 "requested iterator type");
		return -1;
	}

	if ((*type == ITER_NP || *type == ITER_PP) && part_count > 0 &&
	    def->key_def->parts[part_count - 1].coll != NULL) {
		diag_set(UnsupportedIndexFeature, def,
			 "requested iterator type along with collation");
		return -1;
	}

	if (part_count == 0) {
		/*
		 * If no key is specified, downgrade equality
		 * iterators to a full range.
		 */
		*type = iterator_type_is_reverse(*type) ? ITER_LE : ITER_GE;
		*key = NULL;
	}

	if (*type == ITER_ALL)
		*type = ITER_GE;

	return 0;
}

template <class TREE>
static inline struct key_def *
memtx_tree_cmp_def(TREE *tree)
{
	return tree->common.arg;
}

template <bool USE_HINT>
static int
memtx_tree_qcompare(const void* a, const void *b, void *c)
{
	const struct memtx_tree_data<USE_HINT> *data_a =
		(struct memtx_tree_data<USE_HINT> *)a;
	const struct memtx_tree_data<USE_HINT> *data_b =
		(struct memtx_tree_data<USE_HINT> *)b;
	struct key_def *key_def = (struct key_def *)c;
	return tuple_compare(data_a->tuple, data_a->hint, data_b->tuple,
			     data_b->hint, key_def);
}

/* {{{ MemtxTree Iterators ****************************************/
template <bool USE_HINT>
struct tree_iterator {
	struct iterator base;

	/**
	 * TL;DR: don't rely on iterators and their underlying elements after
	 * the “MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND” line.
	 *
	 * MVCC transaction manager story garbage collection can cause removal of
	 * elements from the iterator's underlying block, prior to the
	 * iterator's position, thus shifting elements to the beginning of
	 * the block and effectively changing the iterator's underlying
	 * element (the iterator's position stays the same), breaking it. Hence,
	 * one must finish all iterator manipulations (including manipulations
	 * with its underlying element) before calling the MVCC transaction
	 * manager.
	 *
	 * We refer to the point after which iterators' can get broken by MVCC
	 * transaction story garbage collection as “MVCC TRANSACTION MANAGER
	 * STORY GARBAGE COLLECTION BOUND”.
	 *
	 * One need not care about the iterator's position: it will
	 * automatically get adjusted on iterator->next call.
	 */
	memtx_tree_iterator_t<USE_HINT> tree_iterator;
	enum iterator_type type;
	struct memtx_tree_key_data<USE_HINT> after_data;
	struct memtx_tree_key_data<USE_HINT> key_data;
	/** The amount of tuples to skip after the iterator start. */
	uint32_t offset;
	/**
	 * Data that was fetched last, needed to make iterators stable.
	 * Contains NULL as pointer to tuple only if there was no data fetched.
	 * Otherwise, tuple pointer is not NULL, even if iterator is
	 * exhausted - pagination relies on it.
	 */
	struct memtx_tree_data<USE_HINT> last;
	/**
	 * For functional indexes only: reference to the functional index key
	 * at the last iterator position.
	 *
	 * Since pinning a tuple doesn't prevent its functional keys from being
	 * deleted, we need to reference the key so that we can use it to
	 * restore the iterator position.
	 */
	struct tuple *last_func_key;
	/** Memory pool the iterator was allocated from. */
	struct mempool *pool;
};

static_assert(sizeof(struct tree_iterator<false>) <= MEMTX_ITERATOR_SIZE,
	      "sizeof(struct tree_iterator<false>) must be less than or equal "
	      "to MEMTX_ITERATOR_SIZE");
static_assert(sizeof(struct tree_iterator<true>) <= MEMTX_ITERATOR_SIZE,
	      "sizeof(struct tree_iterator<true>) must be less than or equal "
	      "to MEMTX_ITERATOR_SIZE");

/** Set last fetched tuple. */
template <bool USE_HINT>
static inline void
tree_iterator_set_last_tuple(struct tree_iterator<USE_HINT> *it,
			     struct tuple *tuple)
{
	assert(tuple != NULL);
	if (it->last.tuple != NULL)
		tuple_unref(it->last.tuple);
	it->last.tuple = tuple;
	tuple_ref(tuple);
}

/** Set hint of last fetched tuple. */
template <bool USE_HINT>
static inline void
tree_iterator_set_last_hint(struct tree_iterator<USE_HINT> *it, hint_t hint)
{
	if (!USE_HINT)
		return;
	struct index *index = index_weak_ref_get_index_checked(
		&it->base.index_ref);
	if (it->last_func_key != NULL)
		tuple_unref(it->last_func_key);
	it->last_func_key = NULL;
	if (hint != HINT_NONE && index->def->key_def->for_func_index) {
		it->last_func_key = (struct tuple *)hint;
		tuple_ref(it->last_func_key);
	}
	it->last.set_hint(hint);
}

/**
 * Set last fetched data to iterator to keep it stable. Do not set NULL
 * data or tuple to keep last actually fetched tuple for pagination.
 * Prerequisites: last is not NULL and last->tuple is not NULL.
 * Use set_last_tuple and set_last_hint manually to free occupied resources.
 */
template <bool USE_HINT>
static inline void
tree_iterator_set_last(struct tree_iterator<USE_HINT> *it,
		       struct memtx_tree_data<USE_HINT> *last)
{
	assert(last != NULL && last->tuple != NULL);
	tree_iterator_set_last_tuple(it, last->tuple);
	tree_iterator_set_last_hint(it, last->hint);
}

template <bool USE_HINT>
static void
tree_iterator_free(struct iterator *iterator);

template <bool USE_HINT>
static inline struct tree_iterator<USE_HINT> *
get_tree_iterator(struct iterator *it)
{
	assert(it->free == &tree_iterator_free<USE_HINT>);
	return (struct tree_iterator<USE_HINT> *) it;
}

template <bool USE_HINT>
static void
tree_iterator_free(struct iterator *iterator)
{
	struct tree_iterator<USE_HINT> *it = get_tree_iterator<USE_HINT>(iterator);
	if (it->last.tuple != NULL)
		tuple_unref(it->last.tuple);
	if (it->last_func_key != NULL)
		tuple_unref(it->last_func_key);
	mempool_free(it->pool, it);
}

/*
 * If the iterator's underlying tuple does not match its last tuple, it needs
 * to be repositioned.
 */
template <bool USE_HINT>
static void
tree_iterator_prev_reposition(struct tree_iterator<USE_HINT> *iterator,
			      struct memtx_tree_index<USE_HINT> *index)
{
	bool exact = false;
	iterator->tree_iterator =
		memtx_tree_lower_bound_elem(&index->tree, iterator->last,
					    &exact);
	if (exact) {
		struct memtx_tree_data<USE_HINT> *successor =
		memtx_tree_iterator_get_elem(&index->tree,
					     &iterator->tree_iterator);
		tree_iterator_set_last(iterator, successor);
	}
	/*
	 * Since we previously clarified a tuple from the iterator
	 * last tuple's story chain, a tuple with same primary key
	 * must always exist in the index.
	 */
	assert(exact || in_txn() == NULL || !memtx_tx_manager_use_mvcc_engine);
}

template <bool USE_HINT>
static int
tree_iterator_next_base(struct iterator *iterator, struct tuple **ret)
{
	struct space *space;
	struct index *index_base;
	index_weak_ref_get_checked(&iterator->index_ref, &space, &index_base);
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)index_base;
	struct tree_iterator<USE_HINT> *it = get_tree_iterator<USE_HINT>(iterator);
	assert(it->last.tuple != NULL);
	struct memtx_tree_data<USE_HINT> *check =
		memtx_tree_iterator_get_elem(&index->tree, &it->tree_iterator);
	if (check == NULL || !memtx_tree_data_is_equal(check, &it->last)) {
		it->tree_iterator = memtx_tree_upper_bound_elem(&index->tree,
								it->last, NULL);
	} else {
		memtx_tree_iterator_next(&index->tree, &it->tree_iterator);
	}
	struct memtx_tree_data<USE_HINT> *res =
		memtx_tree_iterator_get_elem(&index->tree, &it->tree_iterator);
	*ret = res != NULL ? res->tuple : NULL;
	if (*ret == NULL) {
		iterator->next_internal = exhausted_iterator_next;
	} else {
		tree_iterator_set_last<USE_HINT>(it, res);
		struct txn *txn = in_txn();
		bool is_multikey = index_base->def->key_def->is_multikey;
		uint32_t mk_index = is_multikey ? (uint32_t)res->hint : 0;
		*ret = memtx_tx_tuple_clarify(txn, space, res->tuple,
					      index_base, mk_index);
	}
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
	/*
	 * Pass no key because any write to the gap between that
	 * two tuples must lead to conflict.
	 */
	struct tuple *successor = res != NULL ? res->tuple : NULL;
	memtx_tx_track_gap(in_txn(), space, index_base, successor, ITER_GE,
			   NULL, 0);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/

	return 0;
}

template <bool USE_HINT>
static int
tree_iterator_prev_base(struct iterator *iterator, struct tuple **ret)
{
	struct space *space;
	struct index *index_base;
	index_weak_ref_get_checked(&iterator->index_ref, &space, &index_base);
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)index_base;
	struct tree_iterator<USE_HINT> *it = get_tree_iterator<USE_HINT>(iterator);
	assert(it->last.tuple != NULL);
	struct memtx_tree_data<USE_HINT> *check =
		memtx_tree_iterator_get_elem(&index->tree, &it->tree_iterator);
	if (check == NULL || !memtx_tree_data_is_equal(check, &it->last))
		tree_iterator_prev_reposition(it, index);
	memtx_tree_iterator_prev(&index->tree, &it->tree_iterator);
	struct tuple *successor = it->last.tuple;
	tuple_ref(successor);
	struct memtx_tree_data<USE_HINT> *res =
		memtx_tree_iterator_get_elem(&index->tree, &it->tree_iterator);
	*ret = res != NULL ? res->tuple : NULL;
	if (*ret == NULL) {
		iterator->next_internal = exhausted_iterator_next;
	} else {
		tree_iterator_set_last<USE_HINT>(it, res);
		struct txn *txn = in_txn();
		bool is_multikey = index_base->def->key_def->is_multikey;
		uint32_t mk_index = is_multikey ? (uint32_t)res->hint : 0;
		/*
		 * We need to clarify the result tuple before story garbage
		 * collection, otherwise it could get cleaned there.
		 */
		*ret = memtx_tx_tuple_clarify(txn, space, res->tuple,
					      index_base, mk_index);
	}
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
	/*
	 * Pass no key because any write to the gap between that
	 * two tuples must lead to conflict.
	 */
	memtx_tx_track_gap(in_txn(), space, index_base, successor, ITER_LE,
			   NULL, 0);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/

	tuple_unref(successor);
	return 0;
}

template <bool USE_HINT>
static int
tree_iterator_next_equal_base(struct iterator *iterator, struct tuple **ret)
{
	struct space *space;
	struct index *index_base;
	index_weak_ref_get_checked(&iterator->index_ref, &space, &index_base);
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)index_base;
	struct tree_iterator<USE_HINT> *it = get_tree_iterator<USE_HINT>(iterator);
	assert(it->last.tuple != NULL);
	struct memtx_tree_data<USE_HINT> *check =
		memtx_tree_iterator_get_elem(&index->tree, &it->tree_iterator);
	if (check == NULL || !memtx_tree_data_is_equal(check, &it->last)) {
		it->tree_iterator = memtx_tree_upper_bound_elem(&index->tree,
								it->last, NULL);
	} else {
		memtx_tree_iterator_next(&index->tree, &it->tree_iterator);
	}
	struct memtx_tree_data<USE_HINT> *res =
		memtx_tree_iterator_get_elem(&index->tree, &it->tree_iterator);
	/* Use user key def to save a few loops. */
	if (res == NULL ||
	    tuple_compare_with_key(res->tuple, res->hint,
				   it->key_data.key,
				   it->key_data.part_count,
				   it->key_data.hint,
				   index->base.def->key_def) != 0) {
		iterator->next_internal = exhausted_iterator_next;
		*ret = NULL;
		/*
		 * Got end of key. Store gap from the previous tuple to the
		 * key boundary in nearby tuple.
		 */
		struct tuple *nearby_tuple = res == NULL ? NULL : res->tuple;

/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		memtx_tx_track_gap(in_txn(), space, index_base, nearby_tuple,
				   ITER_EQ, it->key_data.key,
				   it->key_data.part_count);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
	} else {
		tree_iterator_set_last<USE_HINT>(it, res);
		struct txn *txn = in_txn();
		bool is_multikey = index_base->def->key_def->is_multikey;
		uint32_t mk_index = is_multikey ? (uint32_t)res->hint : 0;
		*ret = memtx_tx_tuple_clarify(txn, space, res->tuple,
					      index_base, mk_index);
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		/*
		 * Pass no key because any write to the gap between that
		 * two tuples must lead to conflict.
		 */
		memtx_tx_track_gap(in_txn(), space, index_base, res->tuple,
				   ITER_GE, NULL, 0);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
	}

	return 0;
}

template <bool USE_HINT>
static int
tree_iterator_prev_equal_base(struct iterator *iterator, struct tuple **ret)
{
	struct space *space;
	struct index *index_base;
	index_weak_ref_get_checked(&iterator->index_ref, &space, &index_base);
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)index_base;
	struct tree_iterator<USE_HINT> *it = get_tree_iterator<USE_HINT>(iterator);
	assert(it->last.tuple != NULL);
	struct memtx_tree_data<USE_HINT> *check =
		memtx_tree_iterator_get_elem(&index->tree, &it->tree_iterator);
	if (check == NULL || !memtx_tree_data_is_equal(check, &it->last))
		tree_iterator_prev_reposition(it, index);
	memtx_tree_iterator_prev(&index->tree, &it->tree_iterator);
	struct tuple *successor = it->last.tuple;
	tuple_ref(successor);
	struct memtx_tree_data<USE_HINT> *res =
		memtx_tree_iterator_get_elem(&index->tree, &it->tree_iterator);
	/* Use user key def to save a few loops. */
	if (res == NULL ||
	    tuple_compare_with_key(res->tuple, res->hint,
				   it->key_data.key,
				   it->key_data.part_count,
				   it->key_data.hint,
				   index->base.def->key_def) != 0) {
		iterator->next_internal = exhausted_iterator_next;
		*ret = NULL;

/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		/*
		 * Got end of key. Store gap from the key boundary to the
		 * previous tuple in nearby tuple.
		 */
		memtx_tx_track_gap(in_txn(), space, index_base, successor,
				   ITER_REQ, it->key_data.key,
				   it->key_data.part_count);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
	} else {
		tree_iterator_set_last<USE_HINT>(it, res);
		struct txn *txn = in_txn();
		bool is_multikey = index_base->def->key_def->is_multikey;
		uint32_t mk_index = is_multikey ? (uint32_t)res->hint : 0;
		/*
		 * We need to clarify the result tuple before story garbage
		 * collection, otherwise it could get cleaned there.
		 */
		*ret = memtx_tx_tuple_clarify(txn, space, res->tuple,
					      index_base, mk_index);
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		/*
		 * Pass no key because any write to the gap between that
		 * two tuples must lead to conflict.
		 */
		memtx_tx_track_gap(in_txn(), space, index_base, successor,
				   ITER_LE, NULL, 0);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
	}
	tuple_unref(successor);
	return 0;
}

#define WRAP_ITERATOR_METHOD(name)						\
template <bool USE_HINT>							\
static int									\
name(struct iterator *iterator, struct tuple **ret)				\
{										\
	do {									\
		int rc = name##_base<USE_HINT>(iterator, ret);			\
		if (rc != 0 ||							\
		    iterator->next_internal == exhausted_iterator_next)		\
			return rc;						\
	} while (*ret == NULL);							\
	return 0;								\
}										\
struct forgot_to_add_semicolon

WRAP_ITERATOR_METHOD(tree_iterator_next);
WRAP_ITERATOR_METHOD(tree_iterator_prev);
WRAP_ITERATOR_METHOD(tree_iterator_next_equal);
WRAP_ITERATOR_METHOD(tree_iterator_prev_equal);

#undef WRAP_ITERATOR_METHOD

template <bool USE_HINT>
static void
tree_iterator_set_next_method(struct tree_iterator<USE_HINT> *it)
{
	assert(it->last.tuple != NULL);
	switch (it->type) {
	case ITER_EQ:
		it->base.next_internal = tree_iterator_next_equal<USE_HINT>;
		break;
	case ITER_REQ:
		it->base.next_internal = tree_iterator_prev_equal<USE_HINT>;
		break;
	case ITER_LT:
	case ITER_LE:
	case ITER_PP:
		it->base.next_internal = tree_iterator_prev<USE_HINT>;
		break;
	case ITER_GE:
	case ITER_GT:
	case ITER_NP:
		it->base.next_internal = tree_iterator_next<USE_HINT>;
		break;
	default:
		/* The type was checked in initIterator */
		assert(false);
	}
	it->base.next = memtx_iterator_next;
}

/**
 * Having iterator @a type as ITER_NP or ITER_PP, transform initial search key
 * @a start_data and the @a type so that normal initial search in iterator would
 * find exactly what needed for next prefix or previous prefix iterator.
 * The resulting type is one of ITER_GT/ITER_LT/ITER_GE/ITER_LE.
 * In the most common case a new search key is allocated on @a region, so
 * region cleanup is needed after the key is no more needed.
 * @retval true if @a start_data and @a type are ready for search.
 * @retval false if the iteration must be stopped without an error.
 */
template <bool USE_HINT>
static bool
prepare_start_prefix_iterator(struct memtx_tree_key_data<USE_HINT> *start_data,
			      enum iterator_type *type, struct key_def *cmp_def,
			      struct region *region)
{
	assert(*type == ITER_NP || *type == ITER_PP);
	assert(start_data->part_count > 0);
	*type = (*type == ITER_NP) ? ITER_GT : ITER_LT;

	/* PP with ASC and NP with DESC works exactly as LT and GT. */
	bool part_order = cmp_def->parts[start_data->part_count - 1].sort_order;
	if ((*type == ITER_LT) == (part_order == SORT_ORDER_ASC))
		return true;

	/* Find the last part of given key. */
	const char *c = start_data->key;
	for (uint32_t i = 1; i < start_data->part_count; i++)
		mp_next(&c);
	/* If the last part is not a string the iterator degrades to GT/LT. */
	if (mp_typeof(*c) != MP_STR)
		return true;

	uint32_t str_size = mp_decode_strl(&c);
	/* Any string logically starts with empty string; iteration is over. */
	if (str_size == 0)
		return false;
	size_t prefix_size = c - start_data->key;
	size_t total_size = prefix_size + str_size;

	unsigned char *p = (unsigned char *)xregion_alloc(region, total_size);
	memcpy(p, start_data->key, total_size);

	/* Increase the key to the least greater value. */
	unsigned char *str = p + prefix_size;
	for (uint32_t i = str_size - 1; ; i--) {
		if (str[i] != UCHAR_MAX) {
			str[i]++;
			break;
		} else if (i == 0) {
			/* If prefix consists of CHAR_MAX, there's no next. */
			return false;
		}
		str[i] = 0;
	}

	/* With increased key we can continue the GE/LE search. */
	*type = (*type == ITER_GT) ? ITER_GE : ITER_LE;
	start_data->key = (char *)p;
	if (USE_HINT)
		start_data->set_hint(key_hint(start_data->key,
					      start_data->part_count, cmp_def));
	return true;
}

/**
 * Creates an iterator based on the given key, after data and iterator type.
 * Also updates @a start_data and iterator @a type as required.
 *
 * @param tree - the tree to lookup in;
 * @param start_data - the key to lookup with, may be updated;
 * @param after_data - the after key, can be empty if not required;
 * @param type - the lookup iterator type, may be updated;
 * @param region - the region to allocate a new @a start_data on if required.
 * @param[out] iterator - the result of the lookup;
 * @param[out] offset - the offset @a iterator points to;
 * @param[out] equals - true if the lookup gave the exact key match;
 * @param[out] initial_elem - the optional pointer to the element approached on
 *  the initial lookup, stepped over if the iterator is reverse, see the end of
 *  this function for more information.
 *
 * @retval true on success;
 * @retval false if the iteration must be stopped without an error.
 */
template<bool USE_HINT>
static bool
memtx_tree_lookup(memtx_tree_t<USE_HINT> *tree,
		  struct memtx_tree_key_data<USE_HINT> *start_data,
		  struct memtx_tree_key_data<USE_HINT> after_data,
		  enum iterator_type *type, struct region *region,
		  memtx_tree_iterator_t<USE_HINT> *iterator,
		  size_t *offset, bool *equals,
		  struct memtx_tree_data<USE_HINT> **initial_elem)
{
	struct key_def *cmp_def = memtx_tree_cmp_def(tree);

	if ((*type == ITER_NP || *type == ITER_PP) &&
	    after_data.key == NULL) {
		if (!prepare_start_prefix_iterator(start_data, type,
						   cmp_def, region))
			return false;
	}

	/*
	 * Since iteration with equality iterators returns first found tuple,
	 * we need a special flag for EQ and REQ if we want to start iteration
	 * after specified key (this flag will affect the choice between
	 * lower bound and upper bound for the above iterators).
	 * As for range iterators with equality, we can simply change them
	 * to their equivalents with inequality.
	 */
	bool skip_equal_tuple = after_data.key != NULL;
	if (skip_equal_tuple && *type != ITER_EQ && *type != ITER_REQ)
		*type = iterator_type_is_reverse(*type) ? ITER_LT : ITER_GT;

	/* Perform the initial lookup. */
	if (start_data->key == NULL) {
		assert(*type == ITER_GE || *type == ITER_LE);
		if (iterator_type_is_reverse(*type)) {
			/*
			 * For all reverse iterators we will step back,
			 * see the and explanation code below.
			 * BPS tree iterators have an interesting property:
			 * a back step from invalid iterator set its
			 * position to the last element. Let's use that.
			 */
			invalidate_tree_iterator(iterator);
			*offset = memtx_tree_size(tree);
		} else {
			*iterator = memtx_tree_first(tree);
			*offset = 0;
		}

		/* If there is at least one tuple in the tree, it is
		 * efficiently equals to the empty key. */
		*equals = memtx_tree_size(tree) != 0;
	} else {
		/*
		 * We use lower_bound on equality iterators instead of LE
		 * because if iterator is reversed, we will take a step back.
		 * Also it is used for LT iterator, and after a step back
		 * iterator will point to a tuple lower than key.
		 * So, lower_bound is used for EQ, GE and LT iterators,
		 * upper_bound is used for REQ, GT, LE iterators.
		 */
		bool need_lower_bound = *type == ITER_EQ || *type == ITER_GE ||
					*type == ITER_LT;

		/*
		 * If we need to skip first tuple in EQ and REQ iterators,
		 * let's just change lower_bound to upper_bound or vice-versa.
		 */
		if (skip_equal_tuple && (*type == ITER_EQ || *type == ITER_REQ))
			need_lower_bound = !need_lower_bound;

		if (need_lower_bound) {
			*iterator = memtx_tree_lower_bound_get_offset(
				tree, start_data, equals, offset);
		} else {
			*iterator = memtx_tree_upper_bound_get_offset(
				tree, start_data, equals, offset);
		}
	}

	/* Save the element we approached on the initial lookup. */
	*initial_elem = memtx_tree_iterator_get_elem(tree, iterator);

	if (iterator_type_is_reverse(*type)) {
		/*
		 * Because of limitations of tree search API we use
		 * lower_bound for LT search and upper_bound for LE and
		 * REQ searches. In both cases we find a position to the
		 * right of the target one. Let's make a step to the
		 * left to reach target position.
		 * If we found an invalid iterator all the elements in
		 * the tree are less (less or equal) to the key, and
		 * iterator_prev call will convert the iterator to the
		 * last position in the tree, that's what we need.
		 */
		memtx_tree_iterator_prev(tree, iterator);
		--*offset; /* Unsigned underflow possible. */
	}
	return true;
}

template<bool USE_HINT>
static int
tree_iterator_start(struct iterator *iterator, struct tuple **ret)
{
	struct region *region = &fiber()->gc;
	RegionGuard region_guard(region);

	*ret = NULL;
	iterator->next_internal = exhausted_iterator_next;

	struct tree_iterator<USE_HINT> *it =
		get_tree_iterator<USE_HINT>(iterator);
	assert(it->last.tuple == NULL);

	struct space *space;
	struct index *index_base;
	index_weak_ref_get_checked(&iterator->index_ref, &space, &index_base);
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)index_base;
	memtx_tree_t<USE_HINT> *tree = &index->tree;
	struct memtx_tree_key_data<USE_HINT> start_data =
		it->after_data.key != NULL ? it->after_data : it->key_data;
	enum iterator_type type = it->type;
	size_t curr_offset;
	/* The flag is true if the found tuple equals to the key. */
	bool equals;
	struct memtx_tree_data<USE_HINT> *initial_elem;
	if (!memtx_tree_lookup(tree, &start_data, it->after_data,
			       &type, region, &it->tree_iterator,
			       &curr_offset, &equals, &initial_elem))
		return 0;

	/*
	 * The initial element could potentially be a successor of the key: we
	 * need to track gap based on it.
	 */
	struct tuple *successor = initial_elem ? initial_elem->tuple : NULL;

	struct memtx_tree_data<USE_HINT> *res = initial_elem;

	/*
	 * If the iterator type is not reverse, the initial_elem is the result
	 * of the first iteration step. Otherwise the lookup function performs
	 * an extra step back, so we need to actualize the current element now.
	 */
	if (iterator_type_is_reverse(type))
		res = memtx_tree_iterator_get_elem(tree, &it->tree_iterator);

	/* Skip the amount of tuples required. */
	struct txn *txn = in_txn();
	if (it->offset != 0 && res != NULL) {
		/* Normalize the unsigned underflow to SIZE_MAX if expected. */
		size_t skip = it->offset;
		bool reverse = iterator_type_is_reverse(type);
		if (reverse && skip > curr_offset + 1)
			skip = curr_offset + 1;

		/* Skip raw tuples and actualize the current element. */
		curr_offset = reverse ? curr_offset - skip : curr_offset + skip;
		it->tree_iterator = memtx_tree_iterator_at(tree, curr_offset);
		res = memtx_tree_iterator_get_elem(tree, &it->tree_iterator);

		/*
		 * We have logarithmically skipped tuples, but some of them may
		 * be invisible to the current transaction. Let's skip further
		 * if required AND if we haven't reached the end of the index.
		 */
		size_t skip_more_visible = res == NULL ? 0 :
			memtx_tx_index_invisible_count_matching_until(
				txn, space, index_base, type, start_data.key,
				start_data.part_count, res->tuple, res->hint);
		memtx_tree_iterator_t<USE_HINT> *iterator = &it->tree_iterator;
		while (skip_more_visible != 0 && res != NULL) {
			if (memtx_tx_tuple_key_is_visible(txn, space,
							  index_base,
							  res->tuple))
				skip_more_visible--;
			if (reverse)
				memtx_tree_iterator_prev(tree, iterator);
			else
				memtx_tree_iterator_next(tree, iterator);
			res = memtx_tree_iterator_get_elem(tree, iterator);
		}
	}

	bool is_eq = type == ITER_EQ || type == ITER_REQ;

	/* If we skip tuple, flag equals is not actual - need to refresh it. */
	if (((it->after_data.key != NULL && is_eq) || it->offset != 0) &&
	    res != NULL) {
		equals = tuple_compare_with_key(res->tuple, res->hint,
						it->key_data.key,
						it->key_data.part_count,
						it->key_data.hint,
						index->base.def->key_def) == 0;
	}

	/*
	 * Equality iterators requires exact key match: if the result does not
	 * equal to the key, iteration ends.
	 */
	bool eq_match = equals || !is_eq;
	if (res != NULL && eq_match) {
		tree_iterator_set_last(it, res);
		tree_iterator_set_next_method(it);
		bool is_multikey = index_base->def->key_def->is_multikey;
		uint32_t mk_index = is_multikey ? (uint32_t)res->hint : 0;
		/*
		 * We need to clarify the result tuple before story garbage
		 * collection, otherwise it could get cleaned there.
		 */
		*ret = memtx_tx_tuple_clarify(txn, space, res->tuple,
					      index_base, mk_index);
	}

/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
	/*
	 * If the key is full then all parts present, so EQ and REQ iterators
	 * can return no more than one tuple.
	 */
	struct key_def *cmp_def = index->base.def->cmp_def;
	bool key_is_full = start_data.part_count == cmp_def->part_count;
	if (it->offset != 0) {
		if (res == NULL || !eq_match) {
			/*
			 * We have stepped over some amount of tuples and got to
			 * the end of the index or stepped over the matching set
			 * (if iterator is EQ or REQ). Lets inform MVCC like we
			 * have counted tuples in the index by our iterator and
			 * key. Insertion or deletion of any matching tuple into
			 * the index will conflict with us.
			 */
			memtx_tx_track_count(txn, space, index_base,
					     type, start_data.key,
					     start_data.part_count);

		} else {
			/*
			 * We have stepped over some amount of tuples and got to
			 * a tuple. Changing the amount of matching tuples prior
			 * to the approached one must conflict with us, so lets
			 * inform MVCC like we have counted tuples in the index
			 * by our key and iterator until the approached tuple.
			 *
			 * The approached tuple itself is read above, so its
			 * replacement or deletion is tracked already.
			 */
			memtx_tx_track_count_until(txn, space, index_base,
						   type, start_data.key,
						   start_data.part_count,
						   res->tuple, res->hint);
		}
		/*
		 * We track all the skipped tuples using one of count trackers,
		 * so no extra tracking is required in this case, insertion or
		 * deletion of a matching tuple will be caught.
		 */
		goto end;
	}
	if (key_is_full && !eq_match)
		memtx_tx_track_point(txn, space, index_base, it->key_data.key);
	/*
	 * Since MVCC operates with `key_def` of index but `start_data` can
	 * contain key extracted with `cmp_def`, we should crop it by passing
	 * `part_count` not greater than `key_def->part_count`.
	 */
	if (!key_is_full ||
	    ((type == ITER_GE || type == ITER_LE) && !equals) ||
	    (type == ITER_GT || type == ITER_LT))
		memtx_tx_track_gap(txn, space, index_base, successor, type,
				   start_data.key, MIN(start_data.part_count,
				   index_base->def->key_def->part_count));

end:
	memtx_tx_story_gc();
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/

	return res == NULL || !eq_match || *ret != NULL ? 0 :
	       iterator->next_internal(iterator, ret);
}

/* }}} */

/* {{{ MemtxTree  **********************************************************/

template <bool USE_HINT>
static void
memtx_tree_index_free(struct memtx_tree_index<USE_HINT> *index)
{
	memtx_tree_destroy(&index->tree);
	free(index->build_array);
	free(index);
}

template <bool USE_HINT>
static void
memtx_tree_index_gc_run(struct memtx_gc_task *task, bool *done)
{
	/*
	 * Yield every 1K tuples to keep latency < 0.1 ms.
	 * Yield more often in debug mode.
	 */
#ifdef NDEBUG
	enum { YIELD_LOOPS = 1000 };
#else
	enum { YIELD_LOOPS = 10 };
#endif

	struct memtx_tree_index<USE_HINT> *index = container_of(task,
			struct memtx_tree_index<USE_HINT>, gc_task);
	memtx_tree_t<USE_HINT> *tree = &index->tree;
	memtx_tree_iterator_t<USE_HINT> *itr = &index->gc_iterator;

	const bool is_func = index->is_func;
	unsigned int loops = 0;
	while (!memtx_tree_iterator_is_invalid(itr)) {
		struct memtx_tree_data<USE_HINT> *res =
			memtx_tree_iterator_get_elem(tree, itr);
		memtx_tree_iterator_next(tree, itr);
		if (is_func)
			tuple_unref((struct tuple *)res->hint);
		else
			tuple_unref(res->tuple);
		if (++loops >= YIELD_LOOPS) {
			*done = false;
			return;
		}
	}
	*done = true;
}

template <bool USE_HINT>
static void
memtx_tree_index_gc_free(struct memtx_gc_task *task)
{
	struct memtx_tree_index<USE_HINT> *index = container_of(task,
			struct memtx_tree_index<USE_HINT>, gc_task);
	memtx_tree_index_free(index);
}

template <bool USE_HINT>
static struct memtx_gc_task_vtab * get_memtx_tree_index_gc_vtab()
{
	static memtx_gc_task_vtab tab =
	{
		.run = memtx_tree_index_gc_run<USE_HINT>,
		.free = memtx_tree_index_gc_free<USE_HINT>,
	};
	return &tab;
};

template <bool USE_HINT>
static void
memtx_tree_index_destroy(struct index *base)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;
	if (base->def->iid == 0 || index->is_func) {
		/*
		 * Primary index. We need to free all tuples stored
		 * in the index, which may take a while. Schedule a
		 * background task in order not to block tx thread.
		 *
		 * Functional index. For every tuple we need to
		 * free all functional keys associated with this tuple.
		 * Let's do it in background also.
		 */
		index->gc_task.vtab = get_memtx_tree_index_gc_vtab<USE_HINT>();
		index->gc_iterator = memtx_tree_first(&index->tree);
		memtx_engine_schedule_gc(memtx, &index->gc_task);
	} else {
		/*
		 * Secondary index. Destruction is fast, no need to
		 * hand over to background fiber.
		 */
		memtx_tree_index_free(index);
	}
}

template <bool USE_HINT>
static void
memtx_tree_index_update_def(struct index *base)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct index_def *def = base->def;
	/*
	 * We use extended key def for non-unique and nullable
	 * indexes. Unique but nullable index can store multiple
	 * NULLs. To correctly compare these NULLs extended key
	 * def must be used. For details @sa tuple_compare.cc.
	 */
	index->tree.common.arg = def->opts.is_unique &&
				 !def->key_def->is_nullable ?
				 def->key_def : def->cmp_def;
}

static bool
memtx_tree_index_depends_on_pk(struct index *base)
{
	struct index_def *def = base->def;
	/* See comment to memtx_tree_index_update_def(). */
	return !def->opts.is_unique || def->key_def->is_nullable;
}

template <bool USE_HINT>
static ssize_t
memtx_tree_index_size(struct index *base)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct space *space = space_by_id(base->def->space_id);
	memtx_tx_story_gc();
	/* Substract invisible count. */
	return memtx_tree_size(&index->tree) -
	       memtx_tx_index_invisible_count(in_txn(), space, base);
}

template <bool USE_HINT>
static ssize_t
memtx_tree_index_bsize(struct index *base)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	return memtx_tree_mem_used(&index->tree);
}

template <bool USE_HINT>
static int
memtx_tree_index_random(struct index *base, uint32_t rnd, struct tuple **result)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct txn *txn = in_txn();
	struct space *space = space_by_id(base->def->space_id);
	bool is_multikey = base->def->key_def->is_multikey;
	if (memtx_tree_index_size<USE_HINT>(base) == 0) {
		*result = NULL;
		memtx_tx_track_gap(txn, space, base, NULL, ITER_GE, NULL, 0);
		return 0;
	}

	do {
		struct memtx_tree_data<USE_HINT> *res =
			memtx_tree_random(&index->tree, rnd++);
		assert(res != NULL);
		uint32_t mk_index = is_multikey ? (uint32_t)res->hint : 0;
		*result = memtx_tx_tuple_clarify(txn, space, res->tuple,
						 base, mk_index);
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		memtx_tx_story_gc();
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
	} while (*result == NULL);
	return memtx_prepare_result_tuple(space, result);
}

template <bool USE_HINT>
static ssize_t
memtx_tree_index_count(struct index *base, enum iterator_type type,
		       const char *key, uint32_t part_count)
{
	assert((base->def->opts.hint == INDEX_HINT_ON) == USE_HINT);

	struct region *region = &fiber()->gc;
	RegionGuard region_guard(region);

	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;

	if (canonicalize_lookup(base->def, &type, &key, part_count) == -1)
		return -1;

	memtx_tree_t<USE_HINT> *tree = &index->tree;
	struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);
	struct memtx_tree_key_data<USE_HINT> start_data;
	start_data.key = key;
	start_data.part_count = part_count;
	if (USE_HINT)
		start_data.set_hint(key_hint(key, part_count, cmp_def));
	struct memtx_tree_key_data<USE_HINT> null_after_data = {};
	memtx_tree_iterator_t<USE_HINT> unused;
	size_t begin_offset;
	bool equals;
	struct memtx_tree_data<USE_HINT> *initial_elem;
	if (!memtx_tree_lookup(tree, &start_data, null_after_data, &type,
			       region, &unused, &begin_offset, &equals,
			       &initial_elem))
		return 0;

	struct txn *txn = in_txn();
	struct space *space = space_by_id(base->def->space_id);
	size_t full_size = memtx_tree_size(tree);
	size_t end_offset;

	/* Fast path: not found equal with full key. */
	if (start_data.part_count == cmp_def->part_count &&
	    !equals && (type == ITER_EQ || type == ITER_REQ)) {
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		/*
		 * Inform MVCC like we have attempted to read a full key and
		 * found nothing. Insertion of this exact key into the tree
		 * will conflict with us.
		 */
		memtx_tx_track_point(txn, space, base, start_data.key);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
		return 0; /* No tuple matching the full key. */
	}

	/* Fast path: not found with reverse iterator. */
	if (begin_offset == (size_t)-1) {
		assert(iterator_type_is_reverse(type));
		struct tuple *successor =
			initial_elem ? initial_elem->tuple : NULL;
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		/*
		 * Inform MVCC that we have attempted to read a tuple prior
		 * to the successor (the first tuple in the tree or NULL if
		 * the tree is empty) and got nothing by our key and iterator.
		 * If someone writes a matching tuple at the beginning of the
		 * tree it will conflict with us.
		 */
		memtx_tx_track_gap(txn, space, base, successor, type,
				   start_data.key, start_data.part_count);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
		return 0; /* No tuples prior to the first one. */
	}

	/* Fast path: not found with forward iterator. */
	if (begin_offset == full_size) {
		assert(!iterator_type_is_reverse(type));
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		/*
		 * Inform MVCC that we have attempted to read a tuple right to
		 * the rightest one in the tree (NULL successor) and thus, got
		 * nothing. If someone writes a tuple matching our key+iterator
		 * pair at the end of the tree it will conflict with us. The
		 * tree can be empty here.
		 */
		memtx_tx_track_gap(txn, space, base, NULL, type, start_data.key,
				   start_data.part_count);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
		return 0; /* No tuples beyond the last one. */
	}

	/*
	 * Now, when we have the first tuple and its offset, let's find the
	 * boundary of the iteration.
	 */
	if (type == ITER_EQ) {
		memtx_tree_upper_bound_get_offset(tree, &start_data,
						  NULL, &end_offset);
	} else if (type == ITER_REQ) {
		memtx_tree_lower_bound_get_offset(tree, &start_data,
						  NULL, &end_offset);
		end_offset--; /* Unsigned underflow possible. */
	} else {
		end_offset = iterator_type_is_reverse(type) ? -1 : full_size;
	}

	size_t full_count = ((ssize_t)end_offset - begin_offset) *
			    iterator_direction(type);

/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
	/*
	 * Inform MVCC that we have counted tuples in the index by our key and
	 * iterator. Insertion or deletion of any matching tuple anywhere in the
	 * index will conflict with us.
	 *
	 * It returns the amount of invisible counted tuples BTW.
	 */
	size_t invisible_count = memtx_tx_track_count(
		txn, space, base, type, start_data.key, start_data.part_count);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/

	return full_count - invisible_count;
}

template <bool USE_HINT>
static int
memtx_tree_index_get_internal(struct index *base, const char *key,
			      uint32_t part_count, struct tuple **result)
{
	assert(base->def->opts.is_unique &&
	       part_count == base->def->key_def->part_count);
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);
	struct txn *txn = in_txn();
	struct space *space = space_by_id(base->def->space_id);
	struct memtx_tree_key_data<USE_HINT> key_data;
	key_data.key = key;
	key_data.part_count = part_count;
	if (USE_HINT)
		key_data.set_hint(key_hint(key, part_count, cmp_def));
	struct memtx_tree_data<USE_HINT> *res =
		memtx_tree_find(&index->tree, &key_data);
	if (res == NULL) {
		*result = NULL;
		assert(part_count == cmp_def->unique_part_count);
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
		memtx_tx_track_point(txn, space, base, key);
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
		return 0;
	}
	bool is_multikey = base->def->key_def->is_multikey;
	uint32_t mk_index = is_multikey ? (uint32_t)res->hint : 0;
	*result = memtx_tx_tuple_clarify(txn, space, res->tuple, base,
					 mk_index);
/********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND START*********/
	memtx_tx_story_gc();
/*********MVCC TRANSACTION MANAGER STORY GARBAGE COLLECTION BOUND END**********/
	return 0;
}

/**
 * Implementation of iterator position for general and multikey indexes.
 */
template <bool USE_HINT, bool IS_MULTIKEY>
static inline int
tree_iterator_position_impl(struct memtx_tree_data<USE_HINT> *last,
			    struct index_def *def,
			    const char **pos, uint32_t *size)
{
	static_assert(!IS_MULTIKEY || USE_HINT,
		      "Multikey index actually uses hint.");
	struct tuple *tuple = last != NULL ? last->tuple : NULL;
	if (tuple == NULL) {
		*pos = NULL;
		*size = 0;
		return 0;
	}
	int mk_idx = MULTIKEY_NONE;
	if (IS_MULTIKEY)
		mk_idx = (int)last->hint;
	const char *key = tuple_extract_key(tuple, def->cmp_def, mk_idx, size);
	if (key == NULL)
		return -1;
	*pos = key;
	return 0;
}

/**
 * Implementation of iterator position for general and multikey indexes.
 */
template <bool USE_HINT, bool IS_MULTIKEY>
static int
tree_iterator_position(struct iterator *it, const char **pos, uint32_t *size)
{
	static_assert(!IS_MULTIKEY || USE_HINT,
		      "Multikey index actually uses hint.");
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)
		index_weak_ref_get_index_checked(&it->index_ref);
	struct tree_iterator<USE_HINT> *tree_it =
		get_tree_iterator<USE_HINT>(it);
	return tree_iterator_position_impl<USE_HINT, IS_MULTIKEY>(
		&tree_it->last, index->base.def, pos, size);
}

/**
 * Implementation of iterator position for functional indexes.
 */
static int
tree_iterator_position_func_impl(struct memtx_tree_data<true> *last,
				 struct index_def *def,
				 const char **pos, uint32_t *size)
{
	/*
	 * Сmp_def in the functional index is the functional key and the
	 * primary key right after it. So to extract cmp_def in func index,
	 * we need to pack an array with concatenated func key and primary key.
	 */
	if (last == NULL || last->tuple == NULL) {
		*pos = NULL;
		*size = 0;
		return 0;
	}
	/* Extract func key. */
	uint32_t func_key_size;
	const char *func_key = tuple_data_range(
		(struct tuple *)last->hint, &func_key_size);
	uint32_t func_key_len = mp_decode_array(&func_key);
	/* Extract primary key. */
	struct key_def *pk_def = def->pk_def;
	uint32_t pk_size;
	const char *pk_key = tuple_extract_key(last->tuple, pk_def,
					       MULTIKEY_NONE, &pk_size);
	uint32_t pk_key_len = mp_decode_array(&pk_key);
	/* Calculate allocation size and allocate buffer. */
	func_key_size -= mp_sizeof_array(func_key_len);
	pk_size -= mp_sizeof_array(pk_key_len);
	uint32_t alloc_size = mp_sizeof_array(func_key_len + pk_key_len) +
		func_key_size + pk_size;
	char *data = (char *)xregion_alloc(&fiber()->gc, alloc_size);
	*size = alloc_size;
	*pos = data;
	/* Pack an array with concatenated func key and primary key. */
	data = mp_encode_array(data, func_key_len + pk_key_len);
	memcpy(data, func_key, func_key_size);
	data += func_key_size;
	memcpy(data, pk_key, pk_size);
	return 0;
}

/**
 * Implementation of iterator position for functional indexes.
 */
static int
tree_iterator_position_func(struct iterator *it, const char **pos,
			    uint32_t *size)
{
	struct index *index = index_weak_ref_get_index_checked(&it->index_ref);
	struct tree_iterator<true> *tree_it = get_tree_iterator<true>(it);
	return tree_iterator_position_func_impl(&tree_it->last, index->def,
						pos, size);
}

/**
 * Adds OOM injection and setting txn flag `TXN_STMT_ROLLBACK' on OOM
 * to `memtx_tree_insert'.
 */
template<bool USE_HINT>
static int
memtx_tree_index_insert_impl(struct memtx_tree_index<USE_HINT> *index,
			     struct memtx_tree_data<USE_HINT> new_data,
			     struct memtx_tree_data<USE_HINT> *dup_data,
			     struct memtx_tree_data<USE_HINT> *suc_data)
{
	if (index_inject_oom() != 0)
		goto fail;
	if (memtx_tree_insert(&index->tree, new_data, dup_data, suc_data) != 0)
		goto fail;
	return 0;
fail:
	txn_set_flags(in_txn(), TXN_STMT_ROLLBACK);
	return -1;
}

/**
 * Adds OOM injection and setting txn flag `TXN_STMT_ROLLBACK' on OOM
 * to `memtx_tree_delete'.
 */
template<bool USE_HINT>
static int
memtx_tree_index_delete_impl(struct memtx_tree_index<USE_HINT> *index,
			     struct memtx_tree_data<USE_HINT> elem_data,
			     struct memtx_tree_data<USE_HINT> *del_data)
{
	if (index_inject_oom() != 0)
		goto fail;
	if (memtx_tree_delete(&index->tree, elem_data, del_data) != 0)
		goto fail;
	return 0;
fail:
	txn_set_flags(in_txn(), TXN_STMT_ROLLBACK);
	return -1;
}

/**
 * Adds OOM injection and setting txn flag `TXN_STMT_ROLLBACK' on OOM
 * to `memtx_tree_delete_value'.
 */
template<bool USE_HINT>
static int
memtx_tree_index_delete_value_impl(struct memtx_tree_index<USE_HINT> *index,
				   struct memtx_tree_data<USE_HINT> elem_data,
				   struct memtx_tree_data<USE_HINT> *del_data)
{
	if (index_inject_oom() != 0)
		goto fail;
	if (memtx_tree_delete_value(&index->tree, elem_data, del_data) != 0)
		goto fail;
	return 0;
fail:
	txn_set_flags(in_txn(), TXN_STMT_ROLLBACK);
	return -1;
}

template <bool USE_HINT>
static int
memtx_tree_index_replace(struct index *base, struct tuple *old_tuple,
			 struct tuple *new_tuple, enum dup_replace_mode mode,
			 struct tuple **result, struct tuple **successor)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct key_def *key_def = base->def->key_def;
	struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);
	if (new_tuple != NULL &&
	    !tuple_key_is_excluded(new_tuple, key_def, MULTIKEY_NONE)) {
		struct memtx_tree_data<USE_HINT> new_data;
		new_data.tuple = new_tuple;
		if (USE_HINT)
			new_data.set_hint(tuple_hint(new_tuple, cmp_def));
		struct memtx_tree_data<USE_HINT> dup_data, suc_data;
		dup_data.tuple = suc_data.tuple = NULL;

		/* Try to optimistically replace the new_tuple. */
		if (memtx_tree_index_insert_impl(index, new_data, &dup_data,
						 &suc_data) != 0)
			return -1;

		if (index_check_dup(base, old_tuple, new_tuple,
				    dup_data.tuple, mode) != 0) {
			VERIFY(memtx_tree_index_delete_impl<USE_HINT>(
						index, new_data, NULL) == 0);
			if (dup_data.tuple != NULL)
				VERIFY(memtx_tree_index_insert_impl<USE_HINT>(
						index, dup_data, NULL,
						NULL) == 0);
			return -1;
		}
		*successor = suc_data.tuple;
		if (dup_data.tuple != NULL) {
			*result = dup_data.tuple;
			return 0;
		}
	}
	if (old_tuple != NULL &&
	    !tuple_key_is_excluded(old_tuple, key_def, MULTIKEY_NONE)) {
		struct memtx_tree_data<USE_HINT> old_data;
		old_data.tuple = old_tuple;
		if (USE_HINT)
			old_data.set_hint(tuple_hint(old_tuple, cmp_def));
		if (memtx_tree_index_delete_impl<USE_HINT>(
					index, old_data, NULL) != 0) {
			if (new_tuple != NULL &&
			    !tuple_key_is_excluded(new_tuple, key_def,
						   MULTIKEY_NONE)) {
				struct memtx_tree_data<USE_HINT> new_data;
				new_data.tuple = new_tuple;
				if (USE_HINT)
					new_data.set_hint(tuple_hint(new_tuple,
								     cmp_def));
				VERIFY(memtx_tree_index_delete_impl<USE_HINT>(
						index, new_data, NULL) == 0);
			}
			return -1;
		}
		*result = old_tuple;
	} else {
		*result = NULL;
	}
	return 0;
}

/**
 * Perform tuple insertion by given multikey index.
 * In case of replacement, all old tuple entries are deleted
 * by all it's multikey indexes.
 */
static int
memtx_tree_index_replace_multikey_one(struct memtx_tree_index<true> *index,
			struct tuple *old_tuple, struct tuple *new_tuple,
			enum dup_replace_mode mode, hint_t hint,
			struct memtx_tree_data<true> *replaced_data,
			bool *is_multikey_conflict)
{
	struct memtx_tree_data<true> new_data, dup_data;
	new_data.tuple = new_tuple;
	new_data.hint = hint;
	dup_data.tuple = NULL;
	*is_multikey_conflict = false;
	if (memtx_tree_index_insert_impl<true>(index, new_data, &dup_data,
					       NULL) != 0)
		return -1;
	if (dup_data.tuple == new_tuple) {
		/*
		 * When tuple contains the same key multiple
		 * times, the previous key occurrence is pushed
		 * out of the index.
		 */
		*is_multikey_conflict = true;
	} else if (index_check_dup(&index->base, old_tuple, new_tuple,
				   dup_data.tuple, mode) != 0) {
		/* Rollback replace. */
		VERIFY(memtx_tree_index_delete_impl<true>(
				index, new_data, NULL) == 0);
		if (dup_data.tuple != NULL)
			VERIFY(memtx_tree_index_insert_impl<true>(
					index, dup_data, NULL, NULL) == 0);
		return -1;
	}
	*replaced_data = dup_data;
	return 0;
}

/**
 * Rollback the sequence of memtx_tree_index_replace_multikey_one
 * insertions with multikey indexes [0, err_multikey_idx - 1]
 * where the err_multikey_idx is the first multikey index where
 * error has been raised.
 *
 * This routine can't fail because all replaced_tuple (when
 * specified) nodes in tree are already allocated (they might be
 * overridden with new_tuple, but they definitely present) and
 * delete operation is fault-tolerant.
 */
static void
memtx_tree_index_replace_multikey_rollback(struct memtx_tree_index<true> *index,
			struct tuple *new_tuple, struct tuple *replaced_tuple,
			int err_multikey_idx)
{
	struct key_def *key_def = index->base.def->key_def;
	struct memtx_tree_data<true> data;
	if (replaced_tuple != NULL) {
		/* Restore replaced tuple index occurrences. */
		struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);
		data.tuple = replaced_tuple;
		uint32_t multikey_count =
			tuple_multikey_count(replaced_tuple, cmp_def);
		for (int i = 0; (uint32_t) i < multikey_count; i++) {
			if (tuple_key_is_excluded(replaced_tuple, key_def, i))
				continue;
			data.hint = i;
			VERIFY(memtx_tree_index_insert_impl<true>(
					index, data, NULL, NULL) == 0);
		}
	}
	if (new_tuple == NULL)
		return;
	/*
	 * Rollback new_tuple insertion by multikey index
	 * [0, multikey_idx).
	 */
	data.tuple = new_tuple;
	for (int i = 0; i < err_multikey_idx; i++) {
		if (tuple_key_is_excluded(new_tuple, key_def, i))
			continue;
		data.hint = i;
		VERIFY(memtx_tree_index_delete_value_impl<true>(
					index, data, NULL) == 0);
	}
}

/**
 * :replace() function for a multikey index: replace old tuple
 * index entries with ones from the new tuple.
 *
 * In a multikey index a single tuple is associated with 0..N keys
 * of the b+*tree. Imagine old tuple key set is called "old_keys"
 * and a new tuple set is called "new_keys". This function must
 * 1) delete all removed keys: (new_keys - old_keys)
 * 2) update tuple pointer in all preserved keys: (old_keys ^ new_keys)
 * 3) insert data for all new keys (new_keys - old_keys).
 *
 * Compare with a standard unique or non-unique index, when a key
 * is present only once, so whenever we encounter a duplicate, it
 * is guaranteed to point at the old tuple (in non-unique indexes
 * we augment the secondary key parts with primary key parts, so
 * b+*tree still contains unique entries only).
 *
 * To reduce the number of insert and delete operations on the
 * tree, this function attempts to optimistically add all keys
 * from the new tuple to the tree first.
 *
 * When this step finds a duplicate, it's either of the following:
 * - for a unique multikey index, it may be the old tuple or
 *   some other tuple. Since unique index forbids duplicates,
 *   this branch ends with an error unless we found the old tuple.
 * - for a non-unique multikey index, both secondary and primary
 *   key parts must match, so it's guaranteed to be the old tuple.
 *
 * In other words, when an optimistic insert finds a duplicate,
 * it's either an error, in which case we roll back all the new
 * keys from the tree and abort the procedure, or the old tuple,
 * which we save to get back to, later.
 *
 * When adding new keys finishes, we have completed steps
 * 2) and 3):
 * - added set (new_keys - old_keys) to the index
 * - updated set (new_keys ^ old_keys) with a new tuple pointer.
 *
 * We now must perform 1), which is remove (old_keys - new_keys).
 *
 * This is done by using the old tuple pointer saved from the
 * previous step. To not accidentally delete the common key
 * set of the old and the new tuple, we don't using key parts alone
 * to compare - we also look at b+* tree value that has the tuple
 * pointer, and delete old tuple entries only.
 */
static int
memtx_tree_index_replace_multikey(struct index *base, struct tuple *old_tuple,
			struct tuple *new_tuple, enum dup_replace_mode mode,
			struct tuple **result, struct tuple **successor)
{
	struct memtx_tree_index<true> *index =
		(struct memtx_tree_index<true> *)base;

	/* MUTLIKEY doesn't support successor for now. */
	*successor = NULL;

	struct key_def *key_def = base->def->key_def;
	struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);
	*result = NULL;
	if (new_tuple != NULL) {
		int multikey_idx = 0, err = 0;
		uint32_t multikey_count =
			tuple_multikey_count(new_tuple, cmp_def);
		for (; (uint32_t) multikey_idx < multikey_count;
		     multikey_idx++) {
			if (tuple_key_is_excluded(new_tuple, key_def,
						  multikey_idx))
				continue;
			bool is_multikey_conflict;
			struct memtx_tree_data<true> replaced_data;
			err = memtx_tree_index_replace_multikey_one(index,
						old_tuple, new_tuple, mode,
						multikey_idx, &replaced_data,
						&is_multikey_conflict);
			if (err != 0)
				break;
			if (replaced_data.tuple != NULL &&
			    !is_multikey_conflict) {
				assert(*result == NULL ||
				       *result == replaced_data.tuple);
				*result = replaced_data.tuple;
			}
		}
		if (err != 0) {
			memtx_tree_index_replace_multikey_rollback(index,
					new_tuple, *result, multikey_idx);
			return -1;
		}
		if (*result != NULL) {
			assert(old_tuple == NULL || old_tuple == *result);
			old_tuple = *result;
		}
	}
	if (old_tuple != NULL) {
		struct memtx_tree_data<true> data;
		data.tuple = old_tuple;
		uint32_t multikey_count =
			tuple_multikey_count(old_tuple, cmp_def);
		for (int i = 0; (uint32_t) i < multikey_count; i++) {
			if (tuple_key_is_excluded(old_tuple, key_def, i))
				continue;
			data.hint = i;
			if (memtx_tree_index_delete_value_impl<true>(
					index, data, NULL) != 0) {
				uint32_t multikey_count = 0;
				if (new_tuple != 0)
					multikey_count =
						tuple_multikey_count(new_tuple,
								     cmp_def);
				memtx_tree_index_replace_multikey_rollback(
					index, new_tuple, old_tuple,
					multikey_count);
				return -1;
			}
		}
	}
	return 0;
}

/**
 * An undo entry for multikey functional index replace operation.
 * Used to roll back a failed insert/replace and restore the
 * original key_hint(s) and to commit a completed insert/replace
 * and destruct old tuple key_hint(s).
*/
struct func_key_undo {
	/** A link to organize entries in list. */
	struct rlist link;
	/** An inserted record copy. */
	struct memtx_tree_data<true> key;
};

/**
 * Rollback a sequence of memtx_tree_index_replace_multikey_one
 * insertions for functional index. Routine uses given list to
 * return a given index object in it's original state.
 */
static void
memtx_tree_func_index_replace_rollback(struct memtx_tree_index<true> *index,
				       struct rlist *old_keys,
				       struct rlist *new_keys)
{
	struct func_key_undo *entry;
	rlist_foreach_entry(entry, new_keys, link) {
		VERIFY(memtx_tree_index_delete_value_impl<true>(
					index, entry->key, NULL) == 0);
		tuple_unref((struct tuple *)entry->key.hint);
	}
	rlist_foreach_entry(entry, old_keys, link)
		VERIFY(memtx_tree_index_insert_impl<true>(
				index, entry->key, NULL, NULL) == 0);
}

/**
 * @sa memtx_tree_index_replace_multikey().
 * Use the functional index function from the key definition
 * to build a key list. Then each returned key is reallocated in
 * engine's memory as key_hint object and is used as comparison
 * hint.
 * To release key_hint memory in case of replace failure
 * we use a list of undo records which are allocated on region.
 * It is used to restore the original b+* entries with their
 * original key_hint(s) pointers in case of failure and release
 * the now useless hints of old items in case of success.
 */
static int
memtx_tree_func_index_replace(struct index *base, struct tuple *old_tuple,
			struct tuple *new_tuple, enum dup_replace_mode mode,
			struct tuple **result, struct tuple **successor)
{
	/* FUNC doesn't support successor for now. */
	*successor = NULL;

	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;
	struct memtx_tree_index<true> *index =
		(struct memtx_tree_index<true> *)base;
	struct index_def *index_def = index->base.def;
	assert(index_def->key_def->for_func_index);
	/* Make sure that key_def is not multikey - we rely on it below. */
	assert(!index_def->key_def->is_multikey);

	int rc = -1;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	*result = NULL;
	struct key_list_iterator it;
	struct rlist old_keys, new_keys;
	rlist_create(&old_keys);
	rlist_create(&new_keys);
	if (new_tuple != NULL) {
		if (key_list_iterator_create(&it, new_tuple, index_def, true,
					     memtx->func_key_format) != 0)
			goto end;
		int err = 0;
		struct tuple *key;
		struct func_key_undo *undo;
		struct key_def *key_def = index_def->key_def;
		while ((err = key_list_iterator_next(&it, &key)) == 0 &&
			key != NULL) {
			if (tuple_key_is_excluded(key, key_def, MULTIKEY_NONE))
				continue;
			/* Perform insertion, log it in list. */
			undo = xregion_alloc_object(region, typeof(*undo));
			tuple_ref(key);
			undo->key.tuple = new_tuple;
			undo->key.hint = (hint_t)key;
			rlist_add(&new_keys, &undo->link);
			bool is_multikey_conflict;
			struct memtx_tree_data<true> old_data;
			old_data.tuple = NULL;
			err = memtx_tree_index_replace_multikey_one(index,
						old_tuple, new_tuple,
						mode, (hint_t)key, &old_data,
						&is_multikey_conflict);
			if (err != 0)
				break;
			if (old_data.tuple != NULL && !is_multikey_conflict) {
				undo = xregion_alloc_object(region,
							    typeof(*undo));
				undo->key = old_data;
				rlist_add(&old_keys, &undo->link);
				*result = old_data.tuple;
			} else if (old_data.tuple != NULL &&
				   is_multikey_conflict) {
				/*
				 * Remove the replaced tuple undo
				 * from undo list.
				 */
				tuple_unref((struct tuple *)old_data.hint);
				rlist_foreach_entry(undo, &new_keys, link) {
					if (undo->key.hint == old_data.hint) {
						rlist_del(&undo->link);
						break;
					}
				}
			}
		}
		if (key != NULL || err != 0) {
			memtx_tree_func_index_replace_rollback(index,
						&old_keys, &new_keys);
			goto end;
		}
		if (*result != NULL) {
			assert(old_tuple == NULL || old_tuple == *result);
			old_tuple = *result;
		}
	}
	if (old_tuple != NULL) {
		/*
		 * Use the runtime format to avoid OOM while deleting a tuple
		 * from a space. It's okay, because we are not going to store
		 * the keys in the index.
		 */
		if (key_list_iterator_create(&it, old_tuple, index_def, false,
					     tuple_format_runtime) != 0)
			goto end;
		struct memtx_tree_data<true> data, deleted_data;
		data.tuple = old_tuple;
		struct tuple *key;
		while (key_list_iterator_next(&it, &key) == 0 && key != NULL) {
			data.hint = (hint_t) key;
			deleted_data.tuple = NULL;
			if (memtx_tree_index_delete_value_impl(
					index, data, &deleted_data) != 0) {
				memtx_tree_func_index_replace_rollback(
					index, &old_keys, &new_keys);
				return -1;
			}
			if (deleted_data.tuple != NULL) {
				struct func_key_undo *undo =
					xregion_alloc_object(region,
							     typeof(*undo));
				undo->key = deleted_data;
				rlist_add(&old_keys, &undo->link);
			}
		}
		assert(key == NULL);
	}
	/*
	 * Commit changes: release hints for
	 * replaced entries.
	 */
	struct func_key_undo *undo;
	rlist_foreach_entry(undo, &old_keys, link)
		tuple_unref((struct tuple *)undo->key.hint);
	rc = 0;
end:
	region_truncate(region, region_svp);
	return rc;
}

template <bool USE_HINT>
static struct iterator *
memtx_tree_index_create_iterator_with_offset(
	struct index *base, enum iterator_type type, const char *key,
	uint32_t part_count, const char *pos, uint32_t offset)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;
	struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);

	if (canonicalize_lookup(base->def, &type, &key, part_count) == -1)
		return NULL;

	ERROR_INJECT(ERRINJ_INDEX_ITERATOR_NEW, {
		diag_set(ClientError, ER_INJECTION, "iterator fail");
		return NULL;
	});

	struct tree_iterator<USE_HINT> *it = (struct tree_iterator<USE_HINT> *)
		mempool_alloc(&memtx->iterator_pool);
	if (it == NULL) {
		diag_set(OutOfMemory, sizeof(struct tree_iterator<USE_HINT>),
			 "memtx_tree_index", "iterator");
		return NULL;
	}
	iterator_create(&it->base, base);
	it->pool = &memtx->iterator_pool;
	it->base.next_internal = tree_iterator_start<USE_HINT>;
	it->base.next = memtx_iterator_next;
	it->base.free = tree_iterator_free<USE_HINT>;
	if (base->def->key_def->for_func_index) {
		assert(USE_HINT);
		it->base.position = tree_iterator_position_func;
	} else if (base->def->key_def->is_multikey) {
		assert(USE_HINT);
		it->base.position = tree_iterator_position<true, true>;
	} else {
		it->base.position = tree_iterator_position<USE_HINT, false>;
	}
	it->type = type;
	it->key_data.key = key;
	it->key_data.part_count = part_count;
	if (USE_HINT)
		it->key_data.set_hint(key_hint(key, part_count, cmp_def));
	invalidate_tree_iterator(&it->tree_iterator);
	it->last.tuple = NULL;
	if (USE_HINT)
		it->last.set_hint(HINT_NONE);
	it->last_func_key = NULL;
	if (pos != NULL) {
		it->after_data.key = pos;
		it->after_data.part_count = cmp_def->part_count;
		if (USE_HINT)
			it->after_data.set_hint(HINT_NONE);
	} else {
		it->after_data.key = NULL;
		it->after_data.part_count = 0;
	}
	it->offset = offset;
	return (struct iterator *)it;
}

template<bool USE_HINT>
static struct iterator *
memtx_tree_index_create_iterator(struct index *base, enum iterator_type type,
				 const char *key, uint32_t part_count,
				 const char *pos)
{
	return memtx_tree_index_create_iterator_with_offset<USE_HINT>(
		base, type, key, part_count, pos, 0);
}

template <bool USE_HINT>
static void
memtx_tree_index_begin_build(struct index *base)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	assert(memtx_tree_size(&index->tree) == 0);
	(void)index;
}

template <bool USE_HINT>
static int
memtx_tree_index_reserve(struct index *base, uint32_t size_hint)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	if (size_hint < index->build_array_alloc_size)
		return 0;
	struct memtx_tree_data<USE_HINT> *tmp =
		(struct memtx_tree_data<USE_HINT> *)
			realloc(index->build_array, size_hint * sizeof(*tmp));
	if (tmp == NULL) {
		diag_set(OutOfMemory, size_hint * sizeof(*tmp),
			 "memtx_tree_index", "reserve");
		return -1;
	}
	index->build_array = tmp;
	index->build_array_alloc_size = size_hint;
	return 0;
}

template <bool USE_HINT>
/** Initialize the next element of the index build_array. */
static int
memtx_tree_index_build_array_append(struct memtx_tree_index<USE_HINT> *index,
				    struct tuple *tuple, hint_t hint)
{
	if (index->build_array == NULL) {
		index->build_array =
			(struct memtx_tree_data<USE_HINT> *)malloc(MEMTX_EXTENT_SIZE);
		if (index->build_array == NULL) {
			diag_set(OutOfMemory, MEMTX_EXTENT_SIZE,
				 "memtx_tree_index", "build_next");
			return -1;
		}
		index->build_array_alloc_size =
			MEMTX_EXTENT_SIZE / sizeof(index->build_array[0]);
	}
	assert(index->build_array_size <= index->build_array_alloc_size);
	if (index->build_array_size == index->build_array_alloc_size) {
		index->build_array_alloc_size = index->build_array_alloc_size +
				DIV_ROUND_UP(index->build_array_alloc_size, 2);
		struct memtx_tree_data<USE_HINT> *tmp =
			(struct memtx_tree_data<USE_HINT> *)realloc(index->build_array,
				index->build_array_alloc_size * sizeof(*tmp));
		if (tmp == NULL) {
			diag_set(OutOfMemory, index->build_array_alloc_size *
				 sizeof(*tmp), "memtx_tree_index", "build_next");
			return -1;
		}
		index->build_array = tmp;
	}
	struct memtx_tree_data<USE_HINT> *elem =
		&index->build_array[index->build_array_size++];
	elem->tuple = tuple;
	if (USE_HINT)
		elem->set_hint(hint);
	return 0;
}

template <bool USE_HINT>
static int
memtx_tree_index_build_next(struct index *base, struct tuple *tuple)
{
	if (tuple_key_is_excluded(tuple, base->def->key_def, MULTIKEY_NONE))
		return 0;
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);
	return memtx_tree_index_build_array_append(index, tuple,
						   tuple_hint(tuple, cmp_def));
}

static int
memtx_tree_index_build_next_multikey(struct index *base, struct tuple *tuple)
{
	struct memtx_tree_index<true> *index = (struct memtx_tree_index<true> *)base;
	struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);
	uint32_t multikey_count = tuple_multikey_count(tuple, cmp_def);
	for (uint32_t multikey_idx = 0; multikey_idx < multikey_count;
	     multikey_idx++) {
		if (tuple_key_is_excluded(tuple, base->def->key_def,
					  multikey_idx))
			continue;
		if (memtx_tree_index_build_array_append(index, tuple,
							multikey_idx) != 0)
			return -1;
	}
	return 0;
}

static int
memtx_tree_func_index_build_next(struct index *base, struct tuple *tuple)
{
	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;
	struct memtx_tree_index<true> *index =
		(struct memtx_tree_index<true> *)base;
	struct index_def *index_def = index->base.def;
	assert(index_def->key_def->for_func_index);
	/* Make sure that key_def is not multikey - we rely on it below. */
	assert(!index_def->key_def->is_multikey);

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);

	struct key_list_iterator it;
	if (key_list_iterator_create(&it, tuple, index_def, false,
				     memtx->func_key_format) != 0)
		return -1;

	struct key_def *key_def = index_def->key_def;
	struct tuple *key;
	uint32_t insert_idx = index->build_array_size;
	while (key_list_iterator_next(&it, &key) == 0 && key != NULL) {
		if (tuple_key_is_excluded(key, key_def, MULTIKEY_NONE))
			continue;
		if (memtx_tree_index_build_array_append(index, tuple,
							(hint_t)key) != 0)
			goto error;
		tuple_ref(key);
	}
	assert(key == NULL);
	region_truncate(region, region_svp);
	return 0;
error:
	for (uint32_t i = insert_idx; i < index->build_array_size; i++) {
		tuple_unref((struct tuple *)index->build_array[i].hint);
	}
	region_truncate(region, region_svp);
	return -1;
}

/**
 * Process build_array of specified index and remove duplicates
 * of equal tuples (in terms of index's cmp_def and have same
 * tuple pointer). The build_array is expected to be sorted.
 */
template <bool USE_HINT>
static void
memtx_tree_index_build_array_deduplicate(
	struct memtx_tree_index<USE_HINT> *index)
{
	if (index->build_array_size == 0)
		return;
	struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);
	size_t w_idx = 0, r_idx = 1;
	while (r_idx < index->build_array_size) {
		if (index->build_array[w_idx].tuple !=
		    index->build_array[r_idx].tuple ||
		    tuple_compare(index->build_array[w_idx].tuple,
				  index->build_array[w_idx].hint,
				  index->build_array[r_idx].tuple,
				  index->build_array[r_idx].hint,
				  cmp_def) != 0) {
			/* Do not override the element itself. */
			if (++w_idx == r_idx)
				continue;
			SWAP(index->build_array[w_idx],
			     index->build_array[r_idx]);
		}
		r_idx++;
	}
	if (cmp_def->for_func_index) {
		/* Destroy deduplicated entries. */
		for (r_idx = w_idx + 1;
		     r_idx < index->build_array_size; r_idx++) {
			hint_t hint = index->build_array[r_idx].hint;
			tuple_unref((struct tuple *)hint);
		}
	}
	index->build_array_size = w_idx + 1;
}

template <bool USE_HINT>
static void
memtx_tree_index_end_build(struct index *base)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct key_def *cmp_def = memtx_tree_cmp_def(&index->tree);
	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;
	tt_sort(index->build_array, index->build_array_size,
		sizeof(index->build_array[0]), memtx_tree_qcompare<USE_HINT>,
		cmp_def, memtx->sort_threads);
	if (cmp_def->is_multikey || cmp_def->for_func_index) {
		/*
		 * Multikey index may have equal(in terms of
		 * cmp_def) keys inserted by different multikey
		 * offsets. We must deduplicate them because
		 * the following memtx_tree_build assumes that
		 * all keys are unique.
		 */
		memtx_tree_index_build_array_deduplicate<USE_HINT>(index);
	}
	memtx_tree_build(&index->tree, index->build_array,
			 index->build_array_size);

	free(index->build_array);
	index->build_array = NULL;
	index->build_array_size = 0;
	index->build_array_alloc_size = 0;
}

/** Read view implementation. */
template <bool USE_HINT>
struct tree_read_view {
	/** Base class. */
	struct index_read_view base;
	/** Read view index. Ref counter incremented. */
	struct memtx_tree_index<USE_HINT> *index;
	/** BPS tree read view. */
	memtx_tree_view_t<USE_HINT> tree_view;
	/** Used for clarifying read view tuples. */
	struct memtx_tx_snapshot_cleaner cleaner;
};

/** Read view iterator implementation. */
template <bool USE_HINT>
struct tree_read_view_iterator {
	/** Base class. */
	struct index_read_view_iterator_base base;
	/** Iterator key. */
	struct memtx_tree_key_data<USE_HINT> key_data;
	/** BPS tree iterator. */
	memtx_tree_iterator_t<USE_HINT> tree_iterator;
	/**
	 * Data that was fetched last. Is NULL only if there was no data
	 * fetched. Otherwise, tuple pointer is not NULL, even if iterator
	 * is exhausted - pagination relies on it.
	 */
	struct memtx_tree_data<USE_HINT> *last;
};

static_assert(sizeof(struct tree_read_view_iterator<false>) <=
	      INDEX_READ_VIEW_ITERATOR_SIZE,
	      "sizeof(struct tree_read_view_iterator<false>) must be less than "
	      "or equal to INDEX_READ_VIEW_ITERATOR_SIZE");
static_assert(sizeof(struct tree_read_view_iterator<true>) <=
	      INDEX_READ_VIEW_ITERATOR_SIZE,
	      "sizeof(struct tree_read_view_iterator<true>) must be less than "
	      "or equal to INDEX_READ_VIEW_ITERATOR_SIZE");

template <bool USE_HINT>
static void
tree_read_view_free(struct index_read_view *base)
{
	struct tree_read_view<USE_HINT> *rv =
		(struct tree_read_view<USE_HINT> *)base;
	memtx_tree_view_destroy(&rv->tree_view);
	index_unref(&rv->index->base);
	memtx_tx_snapshot_cleaner_destroy(&rv->cleaner);
	TRASH(rv);
	free(rv);
}

#if defined(ENABLE_READ_VIEW)
# include "memtx_tree_read_view.cc"
#else /* !defined(ENABLE_READ_VIEW) */

template<bool USE_HINT>
static ssize_t
tree_read_view_count(struct index_read_view *rv, enum iterator_type type,
		     const char *key, uint32_t part_count)
{
	return generic_index_read_view_count(rv, type, key, part_count);
}

template <bool USE_HINT>
static int
tree_read_view_get_raw(struct index_read_view *rv,
		       const char *key, uint32_t part_count,
		       struct read_view_tuple *result)
{
	(void)rv;
	(void)key;
	(void)part_count;
	(void)result;
	unreachable();
	return 0;
}

/** Implementation of next_raw index_read_view_iterator callback. */
template <bool USE_HINT>
static int
tree_read_view_iterator_next_raw(struct index_read_view_iterator *iterator,
				 struct read_view_tuple *result)
{
	struct tree_read_view_iterator<USE_HINT> *it =
		(struct tree_read_view_iterator<USE_HINT> *)iterator;
	struct tree_read_view<USE_HINT> *rv =
		(struct tree_read_view<USE_HINT> *)it->base.index;

	while (true) {
		struct memtx_tree_data<USE_HINT> *res =
			memtx_tree_view_iterator_get_elem(&rv->tree_view,
							  &it->tree_iterator);

		if (res == NULL) {
			*result = read_view_tuple_none();
			return 0;
		}

		memtx_tree_view_iterator_next(&rv->tree_view,
					      &it->tree_iterator);
		if (memtx_prepare_read_view_tuple(res->tuple, &rv->base,
						  &rv->cleaner, result) != 0)
			return -1;
		if (result->data != NULL)
			return 0;
	}
}

/** Positions the iterator to the given key. */
template <bool USE_HINT>
static int
tree_read_view_iterator_start(struct tree_read_view_iterator<USE_HINT> *it,
			      enum iterator_type type,
			      const char *key, uint32_t part_count,
			      const char *pos, uint32_t offset)
{
	assert(type == ITER_ALL);
	assert(key == NULL);
	assert(part_count == 0);
	assert(pos == NULL);
	assert(offset == 0);
	(void)type;
	(void)key;
	(void)part_count;
	(void)pos;
	(void)offset;
	struct tree_read_view<USE_HINT> *rv =
		(struct tree_read_view<USE_HINT> *)it->base.index;
	it->base.next_raw = tree_read_view_iterator_next_raw<USE_HINT>;
	it->tree_iterator = memtx_tree_view_first(&rv->tree_view);
	return 0;
}

template <bool USE_HINT>
static void
tree_read_view_reset_key_def(struct tree_read_view<USE_HINT> *rv)
{
	rv->tree_view.common.arg = NULL;
}

#endif /* !defined(ENABLE_READ_VIEW) */

/**
 * Implementation of iterator position for general and multikey read views.
 */
template <bool USE_HINT, bool IS_MULTIKEY>
static int
tree_read_view_iterator_position(struct index_read_view_iterator *it,
				 const char **pos, uint32_t *size)
{
	struct tree_read_view_iterator<USE_HINT> *tree_it =
		(struct tree_read_view_iterator<USE_HINT> *)it;
	return tree_iterator_position_impl<USE_HINT, IS_MULTIKEY>(
		tree_it->last, it->base.index->def, pos, size);
}

/**
 * Implementation of iterator position for functional index read views.
 */
static int
tree_read_view_iterator_position_func(struct index_read_view_iterator *it,
				      const char **pos, uint32_t *size)
{
	struct tree_read_view_iterator<true> *tree_it =
		(struct tree_read_view_iterator<true> *)it;
	return tree_iterator_position_func_impl(tree_it->last,
						it->base.index->def,
						pos, size);
}

/** Implementation of create_iterator_with_offset index_read_view callback. */
template <bool USE_HINT>
static int
tree_read_view_create_iterator_with_offset(
	struct index_read_view *base, enum iterator_type type, const char *key,
	uint32_t part_count, const char *pos, uint32_t offset,
	struct index_read_view_iterator *iterator)
{
	struct tree_read_view_iterator<USE_HINT> *it =
		(struct tree_read_view_iterator<USE_HINT> *)iterator;
	it->base.index = base;
	it->base.destroy = generic_index_read_view_iterator_destroy;
	it->base.next_raw = exhausted_index_read_view_iterator_next_raw;
	if (it->base.index->def->key_def->for_func_index)
		it->base.position =
			tree_read_view_iterator_position_func;
	else if (it->base.index->def->key_def->is_multikey)
		it->base.position =
			tree_read_view_iterator_position<true, true>;
	else
		it->base.position =
			tree_read_view_iterator_position<USE_HINT, false>;
	it->key_data.key = NULL;
	it->key_data.part_count = 0;
	if (USE_HINT)
		it->key_data.set_hint(HINT_NONE);
	it->last = NULL;
	invalidate_tree_iterator(&it->tree_iterator);
	return tree_read_view_iterator_start(it, type, key, part_count,
					     pos, offset);
}

/** Implementation of create_iterator index_read_view callback. */
template<bool USE_HINT>
static int
tree_read_view_create_iterator(struct index_read_view *base,
			       enum iterator_type type, const char *key,
			       uint32_t part_count, const char *pos,
			       struct index_read_view_iterator *iterator)
{
	return tree_read_view_create_iterator_with_offset<USE_HINT>(
		base, type, key, part_count, pos, 0, iterator);
}

/** Implementation of create_read_view index callback. */
template <bool USE_HINT>
static struct index_read_view *
memtx_tree_index_create_read_view(struct index *base)
{
	static const struct index_read_view_vtab vtab = {
		.free = tree_read_view_free<USE_HINT>,
		.count = tree_read_view_count<USE_HINT>,
		.get_raw = tree_read_view_get_raw<USE_HINT>,
		.create_iterator = tree_read_view_create_iterator<USE_HINT>,
		.create_iterator_with_offset =
			tree_read_view_create_iterator_with_offset<USE_HINT>,
	};
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)base;
	struct tree_read_view<USE_HINT> *rv =
		(struct tree_read_view<USE_HINT> *)xmalloc(sizeof(*rv));
	index_read_view_create(&rv->base, &vtab, base->def);
	struct space *space = space_by_id(base->def->space_id);
	assert(space != NULL);
	memtx_tx_snapshot_cleaner_create(&rv->cleaner, space, base);
	rv->index = index;
	index_ref(base);
	memtx_tree_view_create(&rv->tree_view, &index->tree);
	tree_read_view_reset_key_def(rv);
	return (struct index_read_view *)rv;
}

/**
 * A disabled index vtab provides safe dummy methods for
 * 'inactive' index. It is required to perform a fault-tolerant
 * recovery from snapshoot in case of func_index (because
 * key defintion is not completely initialized at that moment).
 */
static const struct index_vtab memtx_tree_disabled_index_vtab = {
	/* .destroy = */ memtx_tree_index_destroy<true>,
	/* .commit_create = */ generic_index_commit_create,
	/* .abort_create = */ generic_index_abort_create,
	/* .commit_modify = */ generic_index_commit_modify,
	/* .commit_drop = */ generic_index_commit_drop,
	/* .update_def = */ generic_index_update_def,
	/* .depends_on_pk = */ generic_index_depends_on_pk,
	/* .def_change_requires_rebuild = */
		generic_index_def_change_requires_rebuild,
	/* .size = */ generic_index_size,
	/* .bsize = */ generic_index_bsize,
	/* .min = */ generic_index_min,
	/* .max = */ generic_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ generic_index_count,
	/* .get_internal = */ generic_index_get_internal,
	/* .get = */ generic_index_get,
	/* .replace = */ disabled_index_replace,
	/* .create_iterator = */ generic_index_create_iterator,
	/* .create_iterator_with_offset = */
	generic_index_create_iterator_with_offset,
	/* .create_read_view = */ generic_index_create_read_view,
	/* .stat = */ generic_index_stat,
	/* .compact = */ generic_index_compact,
	/* .reset_stat = */ generic_index_reset_stat,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ disabled_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

/** Type of index in terms of different vtabs. */
enum memtx_tree_vtab_type {
	/** General index type. */
	MEMTX_TREE_VTAB_GENERAL,
	/** Multikey index type. */
	MEMTX_TREE_VTAB_MULTIKEY,
	/** Func index type. */
	MEMTX_TREE_VTAB_FUNC,
	/** Disabled index type. */
	MEMTX_TREE_VTAB_DISABLED,
	/** Count of types. */
	MEMTX_TREE_VTAB_TYPE_COUNT,
};

/**
 * Get index vtab by @a TYPE and @a USE_HINT, template version.
 * USE_HINT == false is only allowed for general index type.
 */
template <memtx_tree_vtab_type TYPE, bool USE_HINT = true>
static const struct index_vtab *
get_memtx_tree_index_vtab(void)
{
	static_assert(USE_HINT || TYPE == MEMTX_TREE_VTAB_GENERAL,
		      "Multikey and func indexes must use hints");

	if (TYPE == MEMTX_TREE_VTAB_DISABLED)
		return &memtx_tree_disabled_index_vtab;

	const bool is_mk = TYPE == MEMTX_TREE_VTAB_MULTIKEY;
	const bool is_func = TYPE == MEMTX_TREE_VTAB_FUNC;
	static const struct index_vtab vtab = {
		/* .destroy = */ memtx_tree_index_destroy<USE_HINT>,
		/* .commit_create = */ generic_index_commit_create,
		/* .abort_create = */ generic_index_abort_create,
		/* .commit_modify = */ generic_index_commit_modify,
		/* .commit_drop = */ generic_index_commit_drop,
		/* .update_def = */ memtx_tree_index_update_def<USE_HINT>,
		/* .depends_on_pk = */ memtx_tree_index_depends_on_pk,
		/* .def_change_requires_rebuild = */
			memtx_index_def_change_requires_rebuild,
		/* .size = */ memtx_tree_index_size<USE_HINT>,
		/* .bsize = */ memtx_tree_index_bsize<USE_HINT>,
		/* .min = */ generic_index_min,
		/* .max = */ generic_index_max,
		/* .random = */ memtx_tree_index_random<USE_HINT>,
		/* .count = */ memtx_tree_index_count<USE_HINT>,
		/* .get_internal */ memtx_tree_index_get_internal<USE_HINT>,
		/* .get = */ memtx_index_get,
		/* .replace = */ is_mk ? memtx_tree_index_replace_multikey :
				 is_func ? memtx_tree_func_index_replace :
				 memtx_tree_index_replace<USE_HINT>,
		/* .create_iterator = */
			memtx_tree_index_create_iterator<USE_HINT>,
		/* .create_iterator_with_offset = */
		memtx_tree_index_create_iterator_with_offset<USE_HINT>,
		/* .create_read_view = */
			memtx_tree_index_create_read_view<USE_HINT>,
		/* .stat = */ generic_index_stat,
		/* .compact = */ generic_index_compact,
		/* .reset_stat = */ generic_index_reset_stat,
		/* .begin_build = */ memtx_tree_index_begin_build<USE_HINT>,
		/* .reserve = */ memtx_tree_index_reserve<USE_HINT>,
		/* .build_next = */ is_mk ? memtx_tree_index_build_next_multikey :
				    is_func ? memtx_tree_func_index_build_next :
				    memtx_tree_index_build_next<USE_HINT>,
		/* .end_build = */ memtx_tree_index_end_build<USE_HINT>,
	};
	return &vtab;
}

template <bool USE_HINT>
static struct index *
memtx_tree_index_new_tpl(struct memtx_engine *memtx, struct index_def *def,
			 const struct index_vtab *vtab)
{
	struct memtx_tree_index<USE_HINT> *index =
		(struct memtx_tree_index<USE_HINT> *)
		xcalloc(1, sizeof(*index));
	index_create(&index->base, (struct engine *)memtx, vtab, def);

	/* See comment to memtx_tree_index_update_def(). */
	struct key_def *cmp_def;
	cmp_def = def->opts.is_unique && !def->key_def->is_nullable ?
			index->base.def->key_def : index->base.def->cmp_def;

	memtx_tree_create(&index->tree, cmp_def,
			  &memtx->index_extent_allocator,
			  &memtx->index_extent_stats);
	index->is_func = def->key_def->func_index_func != NULL;
	return &index->base;
}

struct index *
memtx_tree_index_new(struct memtx_engine *memtx, struct index_def *def)
{
	bool use_hint = false;
	const struct index_vtab *vtab;
	if (def->key_def->for_func_index) {
		if (def->key_def->func_index_func != NULL) {
			vtab = get_memtx_tree_index_vtab
				<MEMTX_TREE_VTAB_FUNC>();
		} else {
			vtab = get_memtx_tree_index_vtab
				<MEMTX_TREE_VTAB_DISABLED>();
		}
		use_hint = true;
	} else if (def->key_def->is_multikey) {
		vtab = get_memtx_tree_index_vtab<MEMTX_TREE_VTAB_MULTIKEY>();
		use_hint = true;
	} else if (def->opts.hint == INDEX_HINT_ON) {
		vtab = get_memtx_tree_index_vtab
			<MEMTX_TREE_VTAB_GENERAL, true>();
		use_hint = true;
	} else {
		vtab = get_memtx_tree_index_vtab
			<MEMTX_TREE_VTAB_GENERAL, false>();
	}
	if (use_hint)
		return memtx_tree_index_new_tpl<true>(memtx, def, vtab);
	else
		return memtx_tree_index_new_tpl<false>(memtx, def, vtab);
}
