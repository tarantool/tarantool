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
#include "tuple_compare.h"
#include "space.h"
#include "schema.h" /* space_cache_find() */
#include "errinj.h"
#include "memory.h"
#include "fiber.h"
#include <third_party/qsort_arg.h>

/* {{{ Utilities. *************************************************/

struct key_data
{
	const char *key;
	uint32_t part_count;
};

int
memtx_tree_compare(const tuple *a, const tuple *b, struct index_def *index_def)
{
	int r = tuple_compare(a, b, &index_def->key_def);
	if (r == 0 && !index_def->opts.is_unique)
		r = a < b ? -1 : a > b;
	return r;
}

int
memtx_tree_compare_key(const tuple *a, const struct key_data *key_data,
		       struct index_def *index_def)
{
	return tuple_compare_with_key(a, key_data->key,
				      key_data->part_count, &index_def->key_def);
}

int
memtx_tree_qcompare(const void* a, const void *b, void *c)
{
	return memtx_tree_compare(*(struct tuple **)a,
		*(struct tuple **)b, (struct index_def *)c);
}

/* {{{ MemtxTree Iterators ****************************************/
struct tree_iterator {
	struct iterator base;
	const struct memtx_tree *tree;
	struct index_def *index_def;
	struct memtx_tree_iterator tree_iterator;
	struct key_data key_data;
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
	free(iterator);
}

static struct tuple *
tree_iterator_dummie(struct iterator *iterator)
{
	(void)iterator;
	return 0;
}

static struct tuple *
tree_iterator_fwd(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (!res)
		return 0;
	memtx_tree_iterator_next(it->tree, &it->tree_iterator);
	return *res;
}

static struct tuple *
tree_iterator_bwd(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (!res)
		return 0;
	memtx_tree_iterator_prev(it->tree, &it->tree_iterator);
	return *res;
}

static struct tuple *
tree_iterator_fwd_check_equality(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (!res)
		return 0;
	if (memtx_tree_compare_key(*res, &it->key_data, it->index_def) != 0) {
		it->tree_iterator = memtx_tree_invalid_iterator();
		return 0;
	}
	memtx_tree_iterator_next(it->tree, &it->tree_iterator);
	return *res;
}

static struct tuple *
tree_iterator_fwd_check_next_equality(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (!res)
		return 0;
	memtx_tree_iterator_next(it->tree, &it->tree_iterator);
	iterator->next = tree_iterator_fwd_check_equality;
	return *res;
}

static struct tuple *
tree_iterator_bwd_skip_one(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	memtx_tree_iterator_prev(it->tree, &it->tree_iterator);
	iterator->next = tree_iterator_bwd;
	return tree_iterator_bwd(iterator);
}

static struct tuple *
tree_iterator_bwd_check_equality(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	tuple **res = memtx_tree_iterator_get_elem(it->tree, &it->tree_iterator);
	if (!res)
		return 0;
	if (memtx_tree_compare_key(*res, &it->key_data, it->index_def) != 0) {
		it->tree_iterator = memtx_tree_invalid_iterator();
		return 0;
	}
	memtx_tree_iterator_prev(it->tree, &it->tree_iterator);
	return *res;
}

static struct tuple *
tree_iterator_bwd_skip_one_check_next_equality(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	memtx_tree_iterator_prev(it->tree, &it->tree_iterator);
	iterator->next = tree_iterator_bwd_check_equality;
	return tree_iterator_bwd_check_equality(iterator);
}
/* }}} */

/* {{{ MemtxTree  **********************************************************/

MemtxTree::MemtxTree(struct index_def *index_def_arg)
	: MemtxIndex(index_def_arg), build_array(0), build_array_size(0),
	  build_array_alloc_size(0)
{
	memtx_index_arena_init();
	memtx_tree_create(&tree, index_def,
			      memtx_index_extent_alloc,
			      memtx_index_extent_free, NULL);
}

MemtxTree::~MemtxTree()
{
	memtx_tree_destroy(&tree);
	free(build_array);
}

size_t
MemtxTree::size() const
{
	return memtx_tree_size(&tree);
}

size_t
MemtxTree::bsize() const
{
	return memtx_tree_mem_used(&tree);
}

struct tuple *
MemtxTree::random(uint32_t rnd) const
{
	struct tuple **res = memtx_tree_random(&tree, rnd);
	return res ? *res : 0;
}

struct tuple *
MemtxTree::findByKey(const char *key, uint32_t part_count) const
{
	assert(index_def->opts.is_unique && part_count == index_def->key_def.part_count);

	struct key_data key_data;
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
			tnt_raise(OutOfMemory, BPS_TREE_EXTENT_SIZE,
				  "MemtxTree", "replace");
		}

		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			memtx_tree_delete(&tree, new_tuple);
			if (dup_tuple)
				memtx_tree_insert(&tree, dup_tuple, 0);
			struct space *sp = space_cache_find(index_def->space_id);
			tnt_raise(ClientError, errcode, index_name(this),
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
MemtxTree::allocIterator() const
{
	struct tree_iterator *it = (struct tree_iterator *)
			calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct tree_iterator),
			  "MemtxTree", "iterator");
	}

	it->index_def = index_def;
	it->tree = &tree;
	it->base.free = tree_iterator_free;
	it->tree_iterator = memtx_tree_invalid_iterator();
	return (struct iterator *) it;
}

void
MemtxTree::initIterator(struct iterator *iterator, enum iterator_type type,
			const char *key, uint32_t part_count) const
{
	assert(part_count == 0 || key != NULL);
	struct tree_iterator *it = tree_iterator(iterator);

	if (part_count == 0) {
		/*
		 * If no key is specified, downgrade equality
		 * iterators to a full range.
		 */
		if (type < 0 || type > ITER_GT) {
			return Index::initIterator(iterator, type, key,
						   part_count);
		}
		type = iterator_type_is_reverse(type) ? ITER_LE : ITER_GE;
		key = 0;
	}
	it->key_data.key = key;
	it->key_data.part_count = part_count;

	bool exact = false;
	if (key == 0) {
		if (iterator_type_is_reverse(type))
			it->tree_iterator = memtx_tree_invalid_iterator();
		else
			it->tree_iterator = memtx_tree_iterator_first(&tree);
	} else {
		if (type == ITER_ALL || type == ITER_EQ || type == ITER_GE || type == ITER_LT) {
			it->tree_iterator = memtx_tree_lower_bound(&tree, &it->key_data, &exact);
			if (type == ITER_EQ && !exact) {
				it->base.next = tree_iterator_dummie;
				return;
			}
		} else { // ITER_GT, ITER_REQ, ITER_LE
			it->tree_iterator = memtx_tree_upper_bound(&tree, &it->key_data, &exact);
			if (type == ITER_REQ && !exact) {
				it->base.next = tree_iterator_dummie;
				return;
			}
		}
	}

	switch (type) {
	case ITER_EQ:
		it->base.next = tree_iterator_fwd_check_next_equality;
		break;
	case ITER_REQ:
		it->base.next = tree_iterator_bwd_skip_one_check_next_equality;
		break;
	case ITER_ALL:
	case ITER_GE:
		it->base.next = tree_iterator_fwd;
		break;
	case ITER_GT:
		it->base.next = tree_iterator_fwd;
		break;
	case ITER_LE:
		it->base.next = tree_iterator_bwd_skip_one;
		break;
	case ITER_LT:
		it->base.next = tree_iterator_bwd_skip_one;
		break;
	default:
		return Index::initIterator(iterator, type, key, part_count);
	}
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
	build_array = (struct tuple**)
		realloc(build_array, size_hint * sizeof(struct tuple *));
	build_array_alloc_size = size_hint;
}

void
MemtxTree::buildNext(struct tuple *tuple)
{
	if (!build_array) {
		build_array = (struct tuple**)malloc(BPS_TREE_EXTENT_SIZE);
		build_array_alloc_size =
			BPS_TREE_EXTENT_SIZE / sizeof(struct tuple*);
	}
	assert(build_array_size <= build_array_alloc_size);
	if (build_array_size == build_array_alloc_size) {
		build_array_alloc_size = build_array_alloc_size +
					 build_array_alloc_size / 2;
		build_array = (struct tuple**)
			realloc(build_array,
				build_array_alloc_size *
				sizeof(struct tuple *));
	}
	build_array[build_array_size++] = tuple;
}

void
MemtxTree::endBuild()
{
	qsort_arg(build_array, build_array_size, sizeof(struct tuple *), memtx_tree_qcompare, index_def);
	memtx_tree_build(&tree, build_array, build_array_size);

	free(build_array);
	build_array = 0;
	build_array_size = 0;
	build_array_alloc_size = 0;
}

/**
 * Create a read view for iterator so further index modifications
 * will not affect the iterator iteration.
 */
void
MemtxTree::createReadViewForIterator(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	struct memtx_tree *tree = (struct memtx_tree *)it->tree;
	memtx_tree_iterator_freeze(tree, &it->tree_iterator);
}

/**
 * Destroy a read view of an iterator. Must be called for iterators,
 * for which createReadViewForIterator was called.
 */
void
MemtxTree::destroyReadViewForIterator(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	struct memtx_tree *tree = (struct memtx_tree *)it->tree;
	memtx_tree_iterator_destroy(tree, &it->tree_iterator);
}
