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

static void
memtx_tree_index_destroy(struct index *base)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	memtx_tree_destroy(&index->tree);
	free(index->build_array);
	free(index);
}

static size_t
memtx_tree_index_size(struct index *base)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	return memtx_tree_size(&index->tree);
}

static size_t
memtx_tree_index_bsize(struct index *base)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	return memtx_tree_mem_used(&index->tree);
}

static struct tuple *
memtx_tree_index_random(struct index *base, uint32_t rnd)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	struct tuple **res = memtx_tree_random(&index->tree, rnd);
	return res != NULL ? *res : NULL;
}

static struct tuple *
memtx_tree_index_get(struct index *base, const char *key, uint32_t part_count)
{
	assert(base->def->opts.is_unique &&
	       part_count == base->def->key_def->part_count);
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	struct memtx_tree_key_data key_data;
	key_data.key = key;
	key_data.part_count = part_count;
	struct tuple **res = memtx_tree_find(&index->tree, &key_data);
	return res != NULL ? *res : NULL;
}

static struct tuple *
memtx_tree_index_replace(struct index *base, struct tuple *old_tuple,
			 struct tuple *new_tuple, enum dup_replace_mode mode)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	if (new_tuple) {
		struct tuple *dup_tuple = NULL;

		/* Try to optimistically replace the new_tuple. */
		int tree_res = memtx_tree_insert(&index->tree,
						 new_tuple, &dup_tuple);
		if (tree_res) {
			tnt_raise(OutOfMemory, MEMTX_EXTENT_SIZE,
				  "memtx_tree_index", "replace");
		}

		uint32_t errcode = replace_check_dup(old_tuple,
						     dup_tuple, mode);
		if (errcode) {
			memtx_tree_delete(&index->tree, new_tuple);
			if (dup_tuple)
				memtx_tree_insert(&index->tree, dup_tuple, 0);
			struct space *sp = space_cache_find_xc(base->def->space_id);
			tnt_raise(ClientError, errcode, base->def->name,
				  space_name(sp));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		memtx_tree_delete(&index->tree, old_tuple);
	}
	return old_tuple;
}

static struct iterator *
memtx_tree_index_alloc_iterator(void)
{
	struct tree_iterator *it = (struct tree_iterator *)
			calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct tree_iterator),
			  "memtx_tree_index", "iterator");
	}
	it->base.free = tree_iterator_free;
	return (struct iterator *) it;
}

static void
memtx_tree_index_init_iterator(struct index *base, struct iterator *iterator,
			       enum iterator_type type,
			       const char *key, uint32_t part_count)
{
	assert(part_count == 0 || key != NULL);
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	struct tree_iterator *it = tree_iterator(iterator);

	if (type < 0 || type > ITER_GT) /* Unsupported type */
		tnt_raise(UnsupportedIndexFeature, base->def,
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
	it->index_def = base->def;
	it->tree = &index->tree;
	it->base.next = tree_iterator_start;
	it->tree_iterator = memtx_tree_invalid_iterator();
}

static void
memtx_tree_index_begin_build(struct index *base)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	assert(memtx_tree_size(&index->tree) == 0);
	(void)index;
}

static void
memtx_tree_index_reserve(struct index *base, uint32_t size_hint)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	if (size_hint < index->build_array_alloc_size)
		return;
	struct tuple **tmp = (struct tuple **)realloc(index->build_array,
						      size_hint * sizeof(*tmp));
	if (tmp == NULL)
		tnt_raise(OutOfMemory, size_hint * sizeof(*tmp),
			  "memtx_tree_index", "reserve");
	index->build_array = tmp;
	index->build_array_alloc_size = size_hint;
}

static void
memtx_tree_index_build_next(struct index *base, struct tuple *tuple)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	if (index->build_array == NULL) {
		index->build_array = (struct tuple **)malloc(MEMTX_EXTENT_SIZE);
		if (index->build_array == NULL) {
			tnt_raise(OutOfMemory, MEMTX_EXTENT_SIZE,
				  "memtx_tree_index", "build_next");
		}
		index->build_array_alloc_size =
			MEMTX_EXTENT_SIZE / sizeof(struct tuple*);
	}
	assert(index->build_array_size <= index->build_array_alloc_size);
	if (index->build_array_size == index->build_array_alloc_size) {
		index->build_array_alloc_size = index->build_array_alloc_size +
					index->build_array_alloc_size / 2;
		struct tuple **tmp = (struct tuple **)
			realloc(index->build_array,
				index->build_array_alloc_size * sizeof(*tmp));
		if (tmp == NULL) {
			tnt_raise(OutOfMemory, index->build_array_alloc_size *
				sizeof(*tmp), "memtx_tree_index", "build_next");
		}
		index->build_array = tmp;
	}
	index->build_array[index->build_array_size++] = tuple;
}

static void
memtx_tree_index_end_build(struct index *base)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	/** Use extended key def only for non-unique indexes. */
	struct key_def *cmp_def = base->def->opts.is_unique ?
			base->def->key_def : base->def->cmp_def;
	qsort_arg(index->build_array, index->build_array_size,
		  sizeof(struct tuple *),
		  memtx_tree_qcompare, cmp_def);
	memtx_tree_build(&index->tree, index->build_array,
			 index->build_array_size);

	free(index->build_array);
	index->build_array = NULL;
	index->build_array_size = 0;
	index->build_array_alloc_size = 0;
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
static struct snapshot_iterator *
memtx_tree_index_create_snapshot_iterator(struct index *base)
{
	struct memtx_tree_index *index = (struct memtx_tree_index *)base;
	struct tree_snapshot_iterator *it = (struct tree_snapshot_iterator *)
		calloc(1, sizeof(*it));
	if (it == NULL)
		tnt_raise(OutOfMemory, sizeof(struct tree_snapshot_iterator),
			  "memtx_tree_index", "create_snapshot_iterator");

	it->base.free = tree_snapshot_iterator_free;
	it->base.next = tree_snapshot_iterator_next;
	it->tree = &index->tree;
	it->tree_iterator = memtx_tree_iterator_first(&index->tree);
	memtx_tree_iterator_freeze(&index->tree, &it->tree_iterator);
	return (struct snapshot_iterator *) it;
}

static const struct index_vtab memtx_tree_index_vtab = {
	/* .destroy = */ memtx_tree_index_destroy,
	/* .commit_create = */ generic_index_commit_create,
	/* .commit_drop = */ generic_index_commit_drop,
	/* .size = */ memtx_tree_index_size,
	/* .bsize = */ memtx_tree_index_bsize,
	/* .min = */ memtx_index_min,
	/* .max = */ memtx_index_max,
	/* .random = */ memtx_tree_index_random,
	/* .count = */ memtx_index_count,
	/* .get = */ memtx_tree_index_get,
	/* .replace = */ memtx_tree_index_replace,
	/* .alloc_iterator = */ memtx_tree_index_alloc_iterator,
	/* .init_iterator = */ memtx_tree_index_init_iterator,
	/* .create_snapshot_iterator = */
		memtx_tree_index_create_snapshot_iterator,
	/* .info = */ generic_index_info,
	/* .begin_build = */ memtx_tree_index_begin_build,
	/* .reserve = */ memtx_tree_index_reserve,
	/* .build_next = */ memtx_tree_index_build_next,
	/* .end_build = */ memtx_tree_index_end_build,
};

struct memtx_tree_index *
memtx_tree_index_new(struct index_def *def)
{
	memtx_index_arena_init();

	struct memtx_tree_index *index =
		(struct memtx_tree_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		tnt_raise(OutOfMemory, sizeof(*index),
			  "malloc", "struct memtx_tree_index");
	}
	if (index_create(&index->base, &memtx_tree_index_vtab, def) != 0) {
		free(index);
		diag_raise();
	}

	/**
	 * Use extended key def for non-unique and nullable
	 * indexes. Unique, but nullable, index can store
	 * multiple NULLs. To correctly compare these NULLs
	 * extended key def must be used. For details @sa
	 * tuple_compare.cc.
	 */
	struct key_def *cmp_def;
	if (def->opts.is_unique && !def->key_def->is_nullable)
		cmp_def = index->base.def->key_def;
	else
		cmp_def = index->base.def->cmp_def;
	memtx_tree_create(&index->tree, cmp_def,
			  memtx_index_extent_alloc,
			  memtx_index_extent_free, NULL);
	return index;
}
