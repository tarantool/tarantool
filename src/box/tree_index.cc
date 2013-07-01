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

/* {{{ Utilities. *************************************************/

struct sptree_index_node {
	struct tuple *tuple;
};

struct sptree_index_key_data
{
	const char *key;
	uint32_t part_count;
};

static inline struct tuple *
sptree_index_unfold(const void *node)
{
	if (node == NULL)
		return NULL;

	struct sptree_index_node *node_x = (struct sptree_index_node *) node;
	assert (node_x->tuple != NULL);
	return node_x->tuple;
}

static inline void
sptree_index_fold(void *node, struct tuple *tuple)
{
	assert (node != NULL);
	assert (tuple != NULL);

	struct sptree_index_node *node_x = (struct sptree_index_node *) node;
	node_x->tuple = tuple;
}

static int
sptree_index_node_compare(const void *node_a, const void *node_b, void *arg)
{
	TreeIndex *self = (TreeIndex *) arg;
	struct tuple *tuple_a = sptree_index_unfold(node_a);
	struct tuple *tuple_b = sptree_index_unfold(node_b);

	return tuple_compare(tuple_a, tuple_b, self->key_def);
}

static int
sptree_index_node_compare_dup(const void *node_a, const void *node_b, void *arg)
{
	TreeIndex *self = (TreeIndex *) arg;
	struct tuple *tuple_a = sptree_index_unfold(node_a);
	struct tuple *tuple_b = sptree_index_unfold(node_b);

	return tuple_compare_dup(tuple_a, tuple_b, self->key_def);
}

static int
sptree_index_node_compare_with_key(const void *key, const void *node, void *arg)
{
	TreeIndex *self = (TreeIndex *) arg;
	struct sptree_index_key_data *key_data =
			(struct sptree_index_key_data *) key;
	struct tuple *tuple = sptree_index_unfold(node);

	/* the result is inverted because arguments are swapped */
	return -tuple_compare_with_key(tuple, key_data->key,
				       key_data->part_count, self->key_def);
}

/* {{{ TreeIndex Iterators ****************************************/

struct tree_iterator {
	struct iterator base;
	const TreeIndex *index;
	struct sptree_index_iterator *iter;
	struct sptree_index_key_data key_data;
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
	if (it->iter)
		sptree_index_iterator_free(it->iter);
	free(it);
}

static struct tuple *
tree_iterator_ge(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	void *node = sptree_index_iterator_next(it->iter);
	return sptree_index_unfold(node);
}

static struct tuple *
tree_iterator_le(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);
	void *node = sptree_index_iterator_reverse_next(it->iter);
	return sptree_index_unfold(node);
}

static struct tuple *
tree_iterator_eq(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);

	void *node = sptree_index_iterator_next(it->iter);
	if (node && it->index->tree.compare(&it->key_data, node,
					    (void *) it->index) == 0)
		return sptree_index_unfold(node);

	return NULL;
}

static struct tuple *
tree_iterator_req(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);

	void *node = sptree_index_iterator_reverse_next(it->iter);
	if (node != NULL
	    && it->index->tree.compare(&it->key_data, node,
				       (void *) it->index) == 0) {
		return sptree_index_unfold(node);
	}

	return NULL;
}

static struct tuple *
tree_iterator_lt(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);

	void *node ;
	while ((node = sptree_index_iterator_reverse_next(it->iter)) != NULL) {
		if (it->index->tree.compare(&it->key_data, node,
					    (void *) it->index) != 0) {
			it->base.next = tree_iterator_le;
			return sptree_index_unfold(node);
		}
	}

	return NULL;
}

static struct tuple *
tree_iterator_gt(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);

	void *node;
	while ((node = sptree_index_iterator_next(it->iter)) != NULL) {
		if (it->index->tree.compare(&it->key_data, node,
					    (void *) it->index) != 0) {
			it->base.next = tree_iterator_ge;
			return sptree_index_unfold(node);
		}
	}

	return NULL;
}

/* }}} */

/* {{{ TreeIndex  **********************************************************/

TreeIndex::TreeIndex(struct key_def *key_def, struct space *space)
	: Index(key_def, space)
{
	memset(&tree, 0, sizeof tree);
}

TreeIndex::~TreeIndex()
{
	sptree_index_destroy(&tree);
}

size_t
TreeIndex::size() const
{
	return tree.size;
}

struct tuple *
TreeIndex::min() const
{
	void *node = sptree_index_first(&tree);
	return sptree_index_unfold(node);
}

struct tuple *
TreeIndex::max() const
{
	void *node = sptree_index_last(&tree);
	return sptree_index_unfold(node);
}

struct tuple *
TreeIndex::random(uint32_t rnd) const
{
	void *node = sptree_index_random(&tree, rnd);
	return sptree_index_unfold(node);
}

struct tuple *
TreeIndex::findByKey(const char *key, uint32_t part_count) const
{
	assert(key_def->is_unique && part_count == key_def->part_count);

	struct sptree_index_key_data key_data;
	key_data.key = key;
	key_data.part_count = part_count;
	void *node = sptree_index_find(&tree, &key_data);
	return sptree_index_unfold(node);
}

struct tuple *
TreeIndex::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		   enum dup_replace_mode mode)
{
	struct sptree_index_node new_node;
	struct sptree_index_node old_node;
	uint32_t errcode;

	if (new_tuple) {
		struct sptree_index_node *p_dup_node = &old_node;
		sptree_index_fold(&new_node, new_tuple);

		/* Try to optimistically replace the new_tuple. */
		sptree_index_replace(&tree, &new_node, (void **) &p_dup_node);

		struct tuple *dup_tuple = sptree_index_unfold(p_dup_node);
		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			sptree_index_delete(&tree, &new_node);
			if (p_dup_node != NULL)
				sptree_index_replace(&tree, p_dup_node, NULL);
			tnt_raise(ClientError, errcode, index_n(this));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		sptree_index_fold(&old_node, old_tuple);
		sptree_index_delete(&tree, &old_node);
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

	it->index = this;
	it->base.free = tree_iterator_free;
	return (struct iterator *) it;
}

void
TreeIndex::initIterator(struct iterator *iterator, enum iterator_type type,
			const char *key, uint32_t part_count) const
{
	assert (key != NULL || part_count == 0);
	struct tree_iterator *it = tree_iterator(iterator);

	if (part_count == 0) {
		/*
		 * If no key is specified, downgrade equality
		 * iterators to a full range.
		 */
		type = iterator_type_is_reverse(type) ? ITER_LE : ITER_GE;
		key = NULL;
	}
	it->key_data.key = key;
	it->key_data.part_count = part_count;

	if (iterator_type_is_reverse(type))
		sptree_index_iterator_reverse_init_set(&tree, &it->iter,
						       &it->key_data);
	else
		sptree_index_iterator_init_set(&tree, &it->iter,
					       &it->key_data);

	switch (type) {
	case ITER_EQ:
		it->base.next = tree_iterator_eq;
		break;
	case ITER_REQ:
		it->base.next = tree_iterator_req;
		break;
	case ITER_ALL:
	case ITER_GE:
		it->base.next = tree_iterator_ge;
		break;
	case ITER_GT:
		it->base.next = tree_iterator_gt;
		break;
	case ITER_LE:
		it->base.next = tree_iterator_le;
		break;
	case ITER_LT:
		it->base.next = tree_iterator_lt;
		break;
	default:
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  "Tree index", "requested iterator type");
	}
}

void
TreeIndex::beginBuild()
{
	assert(index_is_primary(this));

	tree.size = 0;
	tree.max_size = 64;

	size_t sz = tree.max_size * sizeof(struct sptree_index_node);
	tree.members = malloc(sz);
	if (tree.members == NULL) {
		panic("malloc(): failed to allocate %" PRI_SZ " bytes", sz);
	}
}

void
TreeIndex::buildNext(struct tuple *tuple)
{
	if (tree.size == tree.max_size) {
		tree.max_size *= 2;

		size_t sz = tree.max_size * sizeof(struct sptree_index_node);
		tree.members = realloc(tree.members, sz);
		if (tree.members == NULL) {
			panic("malloc(): failed to allocate %" PRI_SZ " bytes", sz);
		}
	}

	struct sptree_index_node *node = (struct sptree_index_node *)
			tree.members + tree.size;
	sptree_index_fold(node, tuple);
	tree.size++;
}

void
TreeIndex::endBuild()
{
	assert(index_is_primary(this));

	uint32_t n_tuples = tree.size;
	uint32_t estimated_tuples = tree.max_size;
	void *nodes = tree.members;

	sptree_index_init(&tree, sizeof(struct tuple *),
			  nodes, n_tuples, estimated_tuples,
			  sptree_index_node_compare_with_key,
			  sptree_index_node_compare,
			  this);
}

void
TreeIndex::build(Index *pk)
{
	uint32_t n_tuples = pk->size();
	uint32_t estimated_tuples = n_tuples * 1.2;

	void *nodes = NULL;
	if (n_tuples) {
		/*
		 * Allocate a little extra to avoid
		 * unnecessary realloc() when more data is
		 * inserted.
		*/
		size_t sz = estimated_tuples * sizeof(struct sptree_index_node);
		nodes = malloc(sz);
		if (nodes == NULL) {
			panic("malloc(): failed to allocate %" PRI_SZ " bytes", sz);
		}
	}

	struct iterator *it = pk->position();
	pk->initIterator(it, ITER_ALL, NULL, 0);

	struct tuple *tuple;

	for (uint32_t i = 0; (tuple = it->next(it)) != NULL; ++i) {
		struct sptree_index_node *node = (struct sptree_index_node *)
				nodes + i;
		sptree_index_fold(node, tuple);
	}

	if (n_tuples) {
		say_info("Sorting %" PRIu32 " keys in index %" PRIu32 "...", n_tuples,
			 index_n(this));
	}

	/* If n_tuples == 0 then estimated_tuples = 0, elem == NULL, tree is empty */
	sptree_index_init(&tree, sizeof(struct sptree_index_node),
			  nodes, n_tuples, estimated_tuples,
			  sptree_index_node_compare_with_key,
			  key_def->is_unique ? sptree_index_node_compare
					     : sptree_index_node_compare_dup,
			  this);
}
