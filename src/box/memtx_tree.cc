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
#include "memtx_index.h"
#include "space.h"
#include "schema.h" /* space_cache_find() */
#include "errinj.h"
#include "memory.h"
#include "fiber.h"
#include <third_party/qsort_arg.h>

/* {{{ Utilities. *************************************************/

static int
memtx_tree_qcompare(const void* a, const void *b, void *c)
{
	return tuple_compare(*(struct tuple **)a,
		*(struct tuple **)b, (struct key_def *)c);
}

/* {{{ MemtxTree Iterators ****************************************/
struct tree_iterator {
	struct iterator base;
	const struct memtx_tree *tree;
	struct index_def *index_def;
	struct memtx_tree_iterator tree_iterator;
	enum iterator_type type;
	struct memtx_tree_key_data key_data;
	struct tuple *current_tuple;
};

static void
tree_iterator_free(struct iterator *iterator);

static inline struct tree_iterator *
tree_iterator(struct iterator *it)
{
	assert(it->free == tree_iterator_free);
	return (struct tree_iterator *) it;
}

static void
tree_iterator_free(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	if (it->current_tuple != NULL)
		tuple_unref(it->current_tuple);
	free(iterator);
}

static int
tree_iterator_dummie(struct iterator *iterator, struct tuple **ret)
{
	(void)iterator;
	*ret = NULL;
	return 0;
}

static int
tree_iterator_next(struct iterator *iterator, struct tuple **ret)
{
	tuple **res;
	struct tree_iterator *it = tree_iterator(iterator);
	assert(it->current_tuple != NULL);
	tuple **check = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (check == NULL || *check != it->current_tuple)
		it->tree_iterator =
			memtx_tree_upper_bound_elem(it->tree, it->current_tuple,
						    NULL);
	else
		memtx_tree_iterator_next(it->tree, &it->tree_iterator);
	tuple_unref(it->current_tuple);
	it->current_tuple = NULL;
	res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (res == NULL) {
		iterator->next = tree_iterator_dummie;
		*ret = NULL;
	} else {
		*ret = it->current_tuple = *res;
		tuple_ref(it->current_tuple);
	}
	return 0;
}

static int
tree_iterator_prev(struct iterator *iterator, struct tuple **ret)
{
	struct tree_iterator *it = tree_iterator(iterator);
	assert(it->current_tuple != NULL);
	tuple **check = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (check == NULL || *check != it->current_tuple)
		it->tree_iterator =
			memtx_tree_lower_bound_elem(it->tree, it->current_tuple,
						    NULL);
	memtx_tree_iterator_prev(it->tree, &it->tree_iterator);
	tuple_unref(it->current_tuple);
	it->current_tuple = NULL;
	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (!res) {
		iterator->next = tree_iterator_dummie;
		*ret = NULL;
	} else {
		*ret = it->current_tuple = *res;
		tuple_ref(it->current_tuple);
	}
	return 0;
}

static int
tree_iterator_next_equal(struct iterator *iterator, struct tuple **ret)
{
	struct tree_iterator *it = tree_iterator(iterator);
	assert(it->current_tuple != NULL);
	tuple **check = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (check == NULL || *check != it->current_tuple)
		it->tree_iterator =
			memtx_tree_upper_bound_elem(it->tree, it->current_tuple,
						    NULL);
	else
		memtx_tree_iterator_next(it->tree, &it->tree_iterator);
	tuple_unref(it->current_tuple);
	it->current_tuple = NULL;
	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	/* Use user key def to save a few loops. */
	if (!res || memtx_tree_compare_key(*res, &it->key_data,
					   it->index_def->key_def) != 0) {
		iterator->next = tree_iterator_dummie;
		*ret = NULL;
	} else {
		*ret = it->current_tuple = *res;
		tuple_ref(it->current_tuple);
	}
	return 0;
}

static int
tree_iterator_prev_equal(struct iterator *iterator, struct tuple **ret)
{
	struct tree_iterator *it = tree_iterator(iterator);
	assert(it->current_tuple != NULL);
	tuple **check = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (check == NULL || *check != it->current_tuple)
		it->tree_iterator =
			memtx_tree_lower_bound_elem(it->tree, it->current_tuple,
						    NULL);
	memtx_tree_iterator_prev(it->tree, &it->tree_iterator);
	tuple_unref(it->current_tuple);
	it->current_tuple = NULL;
	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	/* Use user key def to save a few loops. */
	if (!res || memtx_tree_compare_key(*res, &it->key_data,
					   it->index_def->key_def) != 0) {
		iterator->next = tree_iterator_dummie;
		*ret = NULL;
	} else {
		*ret = it->current_tuple = *res;
		tuple_ref(it->current_tuple);
	}
	return 0;
}

static void
tree_iterator_set_next_method(struct tree_iterator *it)
{
	assert(it->current_tuple != NULL);
	switch (it->type) {
	case ITER_EQ:
		it->base.next = tree_iterator_next_equal;
		break;
	case ITER_REQ:
		it->base.next = tree_iterator_prev_equal;
		break;
	case ITER_ALL:
		it->base.next = tree_iterator_next;
		break;
	case ITER_LT:
	case ITER_LE:
		it->base.next = tree_iterator_prev;
		break;
	case ITER_GE:
	case ITER_GT:
		it->base.next = tree_iterator_next;
		break;
	default:
		/* The type was checked in initIterator */
		assert(false);
	}
}

static int
tree_iterator_start(struct iterator *iterator, struct tuple **ret)
{
	*ret = NULL;
	struct tree_iterator *it = tree_iterator(iterator);
	it->base.next = tree_iterator_dummie;
	const memtx_tree *tree = it->tree;
	enum iterator_type type = it->type;
	bool exact = false;
	assert(it->current_tuple == NULL);
	if (it->key_data.key == 0) {
		if (iterator_type_is_reverse(it->type))
			it->tree_iterator = memtx_tree_iterator_last(tree);
		else
			it->tree_iterator = memtx_tree_iterator_first(tree);
	} else {
		if (type == ITER_ALL || type == ITER_EQ ||
		    type == ITER_GE || type == ITER_LT) {
			it->tree_iterator =
				memtx_tree_lower_bound(tree, &it->key_data,
						       &exact);
			if (type == ITER_EQ && !exact)
				return 0;
		} else { // ITER_GT, ITER_REQ, ITER_LE
			it->tree_iterator =
				memtx_tree_upper_bound(tree, &it->key_data,
						       &exact);
			if (type == ITER_REQ && !exact)
				return 0;
		}
		if (iterator_type_is_reverse(type)) {
			/*
			 * Because of limitations of tree search API we use use
			 * lower_bound for LT search and upper_bound for LE
			 * and REQ searches. Thus we found position to the
			 * right of the target one. Let's make a step to the
			 * left to reach target position.
			 * If we found an invalid iterator all the elements in
			 * the tree are less (less or equal) to the key, and
			 * iterator_next call will convert the iterator to the
			 * last position in the tree, that's what we need.
			 */
			memtx_tree_iterator_prev(it->tree, &it->tree_iterator);
		}
	}

	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (!res)
		return 0;
	*ret = it->current_tuple = *res;
	tuple_ref(it->current_tuple);
	tree_iterator_set_next_method(it);
	return 0;
}

/* }}} */

/* {{{ MemtxTree  **********************************************************/

MemtxTree::MemtxTree(struct index_def *index_def_arg)
	: index(index_def_arg),
	build_array(0),
	build_array_size(0),
	build_array_alloc_size(0)
{
	memtx_index_arena_init();
	/**
	 * Use extended key def for non-unique and nullable
	 * indexes. Unique, but nullable, index can store
	 * multiple NULLs. To correctly compare these NULLs
	 * extended key def must be used. For details @sa
	 * tuple_compare.cc.
	 */
	struct key_def *cmp_def;
	if (def->opts.is_unique && !def->key_def->is_nullable)
		cmp_def = def->key_def;
	else
		cmp_def = def->cmp_def;
	memtx_tree_create(&tree, cmp_def,
			  memtx_index_extent_alloc,
			  memtx_index_extent_free, NULL);
}

MemtxTree::~MemtxTree()
{
	memtx_tree_destroy(&tree);
	free(build_array);
}

size_t
MemtxTree::size()
{
	return memtx_tree_size(&tree);
}

size_t
MemtxTree::bsize()
{
	return memtx_tree_mem_used(&tree);
}

struct tuple *
MemtxTree::min(const char *key, uint32_t part_count)
{
	return memtx_index_min(this, key, part_count);
}

struct tuple *
MemtxTree::max(const char *key, uint32_t part_count)
{
	return memtx_index_max(this, key, part_count);
}

size_t
MemtxTree::count(enum iterator_type type, const char *key,
		 uint32_t part_count)
{
	return memtx_index_count(this, type, key, part_count);
}

struct tuple *
MemtxTree::random(uint32_t rnd)
{
	struct tuple **res = memtx_tree_random(&tree, rnd);
	return res ? *res : 0;
}

struct tuple *
MemtxTree::findByKey(const char *key, uint32_t part_count)
{
	assert(def->opts.is_unique && part_count == def->key_def->part_count);

	struct memtx_tree_key_data key_data;
	key_data.key = key;
	key_data.part_count = part_count;
	struct tuple **res = memtx_tree_find(&tree, &key_data);
	return res ? *res : 0;
}

struct tuple *
MemtxTree::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		   enum dup_replace_mode mode)
{
	uint32_t errcode;

	if (new_tuple) {
		struct tuple *dup_tuple = NULL;

		/* Try to optimistically replace the new_tuple. */
		int tree_res =
		memtx_tree_insert(&tree, new_tuple, &dup_tuple);
		if (tree_res) {
			tnt_raise(OutOfMemory, MEMTX_EXTENT_SIZE,
				  "MemtxTree", "replace");
		}

		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			memtx_tree_delete(&tree, new_tuple);
			if (dup_tuple)
				memtx_tree_insert(&tree, dup_tuple, 0);
			struct space *sp = space_cache_find(def->space_id);
			tnt_raise(ClientError, errcode, def->name,
				  space_name(sp));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		memtx_tree_delete(&tree, old_tuple);
	}
	return old_tuple;
}

struct iterator *
MemtxTree::allocIterator()
{
	struct tree_iterator *it = (struct tree_iterator *)
			calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct tree_iterator),
			  "MemtxTree", "iterator");
	}

	it->index_def = def;
	it->tree = &tree;
	it->base.free = tree_iterator_free;
	it->current_tuple = NULL;
	it->tree_iterator = memtx_tree_invalid_iterator();
	return (struct iterator *) it;
}

void
MemtxTree::initIterator(struct iterator *iterator, enum iterator_type type,
			const char *key, uint32_t part_count)
{
	assert(part_count == 0 || key != NULL);
	struct tree_iterator *it = tree_iterator(iterator);

	if (type < 0 || type > ITER_GT) /* Unsupported type */
		tnt_raise(UnsupportedIndexFeature, this,
			  "requested iterator type");

	if (part_count == 0) {
		/*
		 * If no key is specified, downgrade equality
		 * iterators to a full range.
		 */
		type = iterator_type_is_reverse(type) ? ITER_LE : ITER_GE;
		key = NULL;
	}
	if (it->current_tuple) {
		/*
		 * Free possible leftover tuple if the iterator
		 * is reused.
		 */
		tuple_unref(it->current_tuple);
		it->current_tuple = NULL;
	}
	it->type = type;
	it->key_data.key = key;
	it->key_data.part_count = part_count;
	it->base.next = tree_iterator_start;
	it->tree_iterator = memtx_tree_invalid_iterator();
}

void
MemtxTree::beginBuild()
{
	assert(memtx_tree_size(&tree) == 0);
}

void
MemtxTree::reserve(uint32_t size_hint)
{
	if (size_hint < build_array_alloc_size)
		return;
	struct tuple **tmp = (struct tuple**)
		realloc(build_array, size_hint * sizeof(*tmp));
	if (tmp == NULL)
		tnt_raise(OutOfMemory, size_hint * sizeof(*tmp),
			"MemtxTree", "reserve");
	build_array = tmp;
	build_array_alloc_size = size_hint;
}

void
MemtxTree::buildNext(struct tuple *tuple)
{
	if (build_array == NULL) {
		build_array = (struct tuple**) malloc(MEMTX_EXTENT_SIZE);
		if (build_array == NULL) {
			tnt_raise(OutOfMemory, MEMTX_EXTENT_SIZE,
				"MemtxTree", "buildNext");
		}
		build_array_alloc_size =
			MEMTX_EXTENT_SIZE / sizeof(struct tuple*);
	}
	assert(build_array_size <= build_array_alloc_size);
	if (build_array_size == build_array_alloc_size) {
		build_array_alloc_size = build_array_alloc_size +
					 build_array_alloc_size / 2;
		struct tuple **tmp = (struct tuple **)
			realloc(build_array, build_array_alloc_size *
				sizeof(*tmp));
		if (tmp == NULL) {
			tnt_raise(OutOfMemory, build_array_alloc_size *
				sizeof(*tmp), "MemtxTree", "buildNext");
		}
		build_array = tmp;
	}
	build_array[build_array_size++] = tuple;
}

void
MemtxTree::endBuild()
{
	/** Use extended key def only for non-unique indexes. */
	struct key_def *cmp_def = def->opts.is_unique ?
			def->key_def : def->cmp_def;
	qsort_arg(build_array, build_array_size,
		  sizeof(struct tuple *),
		  memtx_tree_qcompare, cmp_def);
	memtx_tree_build(&tree, build_array, build_array_size);

	free(build_array);
	build_array = 0;
	build_array_size = 0;
	build_array_alloc_size = 0;
}

struct tree_snapshot_iterator {
	struct snapshot_iterator base;
	struct memtx_tree *tree;
	struct memtx_tree_iterator tree_iterator;
};

static void
tree_snapshot_iterator_free(struct snapshot_iterator *iterator)
{
	assert(iterator->free == tree_snapshot_iterator_free);
	struct tree_snapshot_iterator *it =
		(struct tree_snapshot_iterator *)iterator;
	struct memtx_tree *tree = (struct memtx_tree *)it->tree;
	memtx_tree_iterator_destroy(tree, &it->tree_iterator);
	free(iterator);
}

static const char *
tree_snapshot_iterator_next(struct snapshot_iterator *iterator, uint32_t *size)
{
	assert(iterator->free == tree_snapshot_iterator_free);
	struct tree_snapshot_iterator *it =
		(struct tree_snapshot_iterator *)iterator;
	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (res == NULL)
		return NULL;
	memtx_tree_iterator_next(it->tree, &it->tree_iterator);
	return tuple_data_range(*res, size);
}

/**
 * Create an ALL iterator with personal read view so further
 * index modifications will not affect the iteration results.
 * Must be destroyed by iterator->free after usage.
 */
struct snapshot_iterator *
MemtxTree::createSnapshotIterator()
{
	struct tree_snapshot_iterator *it = (struct tree_snapshot_iterator *)
		calloc(1, sizeof(*it));
	if (it == NULL)
		tnt_raise(OutOfMemory, sizeof(struct tree_snapshot_iterator),
			  "MemtxTree", "iterator");

	it->base.free = tree_snapshot_iterator_free;
	it->base.next = tree_snapshot_iterator_next;
	it->tree = &tree;
	it->tree_iterator = memtx_tree_iterator_first(&tree);
	memtx_tree_iterator_freeze(&tree, &it->tree_iterator);
	return (struct snapshot_iterator *) it;
}
