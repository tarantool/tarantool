/*
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
#include "tree_index.h"
#include "tuple.h"
#include "space.h"
#include "exception.h"
#include "errinj.h"
#include "memory.h"
#include "fiber.h"

static struct mempool tree_extent_pool;
static int tree_index_count = 0;

/* {{{ Utilities. *************************************************/

struct key_data
{
	const char *key;
	uint32_t part_count;
};

int
tree_index_compare(const tuple *a, const tuple *b, struct key_def *key_def)
{
	int r = tuple_compare(a, b, key_def);
	if (r == 0 && !key_def->is_unique)
		r = a < b ? -1 : a > b;
	return r;
}
int
tree_index_compare_key(const tuple *a, const key_data *key_data, struct key_def *key_def)
{
	return tuple_compare_with_key(a, key_data->key, key_data->part_count, key_def);
}

/* {{{ TreeIndex Iterators ****************************************/

struct tree_iterator {
	struct iterator base;
	const struct bps_tree *tree;
	struct key_def *key_def;
	struct bps_tree_iterator bsp_tree_iter;
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
	tuple **res = bps_tree_itr_get_elem(it->tree, &it->bsp_tree_iter);
	if (!res)
		return 0;
	bps_tree_itr_next(it->tree, &it->bsp_tree_iter);
	return *res;
}

static struct tuple *
tree_iterator_bwd(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	tuple **res = bps_tree_itr_get_elem(it->tree, &it->bsp_tree_iter);
	if (!res)
		return 0;
	bps_tree_itr_prev(it->tree, &it->bsp_tree_iter);
	return *res;
}

static struct tuple *
tree_iterator_fwd_check_equality(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	tuple **res = bps_tree_itr_get_elem(it->tree, &it->bsp_tree_iter);
	if (!res)
		return 0;
	if (tree_index_compare_key(*res, &it->key_data, it->key_def) != 0) {
		it->bsp_tree_iter = bps_tree_invalid_iterator();
		return 0;
	}
	bps_tree_itr_next(it->tree, &it->bsp_tree_iter);
	return *res;
}

static struct tuple *
tree_iterator_fwd_check_next_equality(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	tuple **res = bps_tree_itr_get_elem(it->tree, &it->bsp_tree_iter);
	if (!res)
		return 0;
	bps_tree_itr_next(it->tree, &it->bsp_tree_iter);
	iterator->next = tree_iterator_fwd_check_equality;
	return *res;
}

static struct tuple *
tree_iterator_bwd_skip_one(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	bps_tree_itr_prev(it->tree, &it->bsp_tree_iter);
	iterator->next = tree_iterator_bwd;
	return tree_iterator_bwd(iterator);
}

static struct tuple *
tree_iterator_bwd_check_equality(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	tuple **res = bps_tree_itr_get_elem(it->tree, &it->bsp_tree_iter);
	if (!res)
		return 0;
	if (tree_index_compare_key(*res, &it->key_data, it->key_def) != 0) {
		it->bsp_tree_iter = bps_tree_invalid_iterator();
		return 0;
	}
	bps_tree_itr_prev(it->tree, &it->bsp_tree_iter);
	return *res;
}

static struct tuple *
tree_iterator_bwd_skip_one_check_next_equality(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	bps_tree_itr_prev(it->tree, &it->bsp_tree_iter);
	iterator->next = tree_iterator_bwd_check_equality;
	return tree_iterator_bwd_check_equality(iterator);
}
/* }}} */

/* {{{ TreeIndex  **********************************************************/

static void *
extent_alloc()
{
#ifndef NDEBUG
	ERROR_INJECT(ERRINJ_TREE_ALLOC, return 0);
#endif
	return mempool_alloc(&tree_extent_pool);
}

static void
extent_free(void *extent)
{
	return mempool_free(&tree_extent_pool, extent);
}

TreeIndex::TreeIndex(struct key_def *key_def_arg)
	: Index(key_def_arg)
{
	if (tree_index_count == 0)
		mempool_create(&tree_extent_pool, &cord()->slabc, BPS_TREE_EXTENT_SIZE);
	tree_index_count++;
	bps_tree_create(&tree, key_def, extent_alloc, extent_free);
}

TreeIndex::~TreeIndex()
{
	tree_index_count--;
	if (tree_index_count == 0)
		mempool_destroy(&tree_extent_pool);
}

size_t
TreeIndex::size() const
{
	return bps_tree_size(&tree);
}

size_t
TreeIndex::memsize() const
{
	return bps_tree_mem_used(&tree);
}

struct tuple *
TreeIndex::random(uint32_t rnd) const
{
	struct tuple **res = bps_tree_random(&tree, rnd);
	return res ? *res : 0;
}

struct tuple *
TreeIndex::findByKey(const char *key, uint32_t part_count) const
{
	assert(key_def->is_unique && part_count == key_def->part_count);

	struct key_data key_data;
	key_data.key = key;
	key_data.part_count = part_count;
	struct tuple **res = bps_tree_find(&tree, &key_data);
	return res ? *res : 0;
}

struct tuple *
TreeIndex::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		   enum dup_replace_mode mode)
{
	uint32_t errcode;

	if (new_tuple) {
		struct tuple *dup_tuple = NULL;

		/* Try to optimistically replace the new_tuple. */
		bool tree_res =
		bps_tree_insert_or_replace(&tree, new_tuple, &dup_tuple);
		if (!tree_res) {
			tnt_raise(ClientError, ER_MEMORY_ISSUE,
				  BPS_TREE_EXTENT_SIZE, "TreeIndex", "replace");
		}

		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			bps_tree_delete(&tree, new_tuple);
			if (dup_tuple)
				bps_tree_insert_or_replace(&tree, dup_tuple, 0);
			tnt_raise(ClientError, errcode, index_id(this));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		bps_tree_delete(&tree, old_tuple);
	}
	return old_tuple;
}

struct iterator *
TreeIndex::allocIterator() const
{
	struct tree_iterator *it = (struct tree_iterator *)
			calloc(1, sizeof(*it));
	if (it == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE,
			  sizeof(struct tree_iterator),
			  "TreeIndex", "iterator");
	}

	it->key_def = key_def;
	it->tree = &tree;
	it->base.free = tree_iterator_free;
	it->bsp_tree_iter = bps_tree_invalid_iterator();
	return (struct iterator *) it;
}

void
TreeIndex::initIterator(struct iterator *iterator, enum iterator_type type,
			const char *key, uint32_t part_count) const
{
	assert(part_count == 0 || key != NULL);
	struct tree_iterator *it = tree_iterator(iterator);

	if (part_count == 0) {
		/*
		 * If no key is specified, downgrade equality
		 * iterators to a full range.
		 */
		if (type < 0 || type > ITER_GT)
			tnt_raise(ClientError, ER_UNSUPPORTED,
				  "Tree index", "requested iterator type");
		type = iterator_type_is_reverse(type) ? ITER_LE : ITER_GE;
		key = 0;
	}
	it->key_data.key = key;
	it->key_data.part_count = part_count;

	bool exact = false;
	if (key == 0) {
		if (iterator_type_is_reverse(type))
			it->bsp_tree_iter = bps_tree_invalid_iterator();
		else
			it->bsp_tree_iter = bps_tree_itr_first(&tree);
	} else {
		if (type == ITER_ALL || type == ITER_EQ || type == ITER_GE || type == ITER_LT) {
			it->bsp_tree_iter = bps_tree_lower_bound(&tree, &it->key_data, &exact);
			if (type == ITER_EQ && !exact) {
				it->base.next = tree_iterator_dummie;
				return;
			}
		} else { // ITER_GT, ITER_REQ, ITER_LE
			it->bsp_tree_iter = bps_tree_upper_bound(&tree, &it->key_data, &exact);
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
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  "Tree index", "requested iterator type");
	}
}

void
TreeIndex::beginBuild()
{
	assert(bps_tree_size(&tree) == 0);
}

void
TreeIndex::reserve(uint32_t size_hint)
{
	(void)size_hint;
}

void
TreeIndex::buildNext(struct tuple *tuple)
{
	bps_tree_insert_or_replace(&tree, tuple, 0);
}

void
TreeIndex::endBuild()
{
}

