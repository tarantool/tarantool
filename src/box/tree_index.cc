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

struct sptree_index_key_data
{
	const char *key;
	uint32_t part_count;
};

static inline struct tuple *
sptree_index_unfold(const void *node)
{
	return node ? *(struct tuple **) node : NULL;
}

static int
sptree_index_node_compare(const void *node_a, const void *node_b, void *arg)
{
	struct key_def *key_def = (struct key_def *) arg;
	const struct tuple *tuple_a = *(const struct tuple **) node_a;
	const struct tuple *tuple_b = *(const struct tuple **) node_b;

	return tuple_compare(tuple_a, tuple_b, key_def);
}

static int
sptree_index_node_compare_dup(const void *node_a, const void *node_b,
			      void *arg)
{
	struct key_def *key_def = (struct key_def *) arg;
	const struct tuple *tuple_a = *(const struct tuple **) node_a;
	const struct tuple *tuple_b = *(const struct tuple **) node_b;

	return tuple_compare_dup(tuple_a, tuple_b, key_def);
}

static int
sptree_index_node_compare_with_key(const void *key, const void *node,
				   void *arg)
{
	struct key_def *key_def = (struct key_def *) arg;
	const struct sptree_index_key_data *key_data =
			(const struct sptree_index_key_data *) key;
	const struct tuple *tuple = *(const struct tuple **) node;

	/* the result is inverted because arguments are swapped */
	return -tuple_compare_with_key(tuple, key_data->key,
				       key_data->part_count, key_def);
}

/* {{{ TreeIndex Iterators ****************************************/

struct tree_iterator {
	struct iterator base;
	struct key_def *key_def;
	sptree_index_compare compare;
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
	if (node && it->compare(&it->key_data, node, it->key_def) == 0)
		return *(struct tuple **) node;

	return NULL;
}

static struct tuple *
tree_iterator_req(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);

	void *node = sptree_index_iterator_reverse_next(it->iter);
	if (node && it->compare(&it->key_data, node, it->key_def) == 0)
		return *(struct tuple **) node;

	return NULL;
}

static struct tuple *
tree_iterator_lt(struct iterator *iterator)
{
	struct tree_iterator *it = tree_iterator(iterator);

	void *node ;
	while ((node = sptree_index_iterator_reverse_next(it->iter)) != NULL) {
		if (it->compare(&it->key_data, node, it->key_def) != 0) {
			it->base.next = tree_iterator_le;
			return *(struct tuple **) node;
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
		if (it->compare(&it->key_data, node, it->key_def) != 0) {
			it->base.next = tree_iterator_ge;
			return *(struct tuple **) node;
		}
	}

	return NULL;
}

/* }}} */

/* {{{ TreeIndex  **********************************************************/

TreeIndex::TreeIndex(struct key_def *key_def)
	: Index(key_def)
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
	uint32_t errcode;

	if (new_tuple) {
		struct tuple *dup_tuple = NULL;
		void *p_dup_node = &dup_tuple;

		/* Try to optimistically replace the new_tuple. */
		sptree_index_replace(&tree, &new_tuple, &p_dup_node);

		errcode = replace_check_dup(old_tuple, dup_tuple, mode);

		if (errcode) {
			sptree_index_delete(&tree, &new_tuple);
			if (dup_tuple)
				sptree_index_replace(&tree, &dup_tuple, NULL);
			tnt_raise(ClientError, errcode, index_id(this));
		}
		if (dup_tuple)
			return dup_tuple;
	}
	if (old_tuple) {
		sptree_index_delete(&tree, &old_tuple);
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
	it->compare = tree.compare;
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
	tree.size = 0;
	tree.max_size = 0;
	tree.members = NULL;
}

void
TreeIndex::reserve(uint32_t size_hint)
{
	assert(size_hint >= tree.size);
	size_hint = MAX(size_hint, SPTREE_MIN_SIZE);
	size_t sz = size_hint * sizeof(struct tuple *);
	void *members = realloc(tree.members, sz);
	if (members == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE, sz,
			  "TreeIndex::reserve()", "malloc");
	}
	tree.members = members;
	tree.max_size = size_hint;
}

void
TreeIndex::buildNext(struct tuple *tuple)
{
	if (tree.size >= tree.max_size)
		reserve(tree.max_size * 2);

	struct tuple **node = (struct tuple **) tree.members + tree.size;
	*node = tuple;
	tree.size++;
}

void
TreeIndex::endBuild()
{
	uint32_t n_tuples = tree.size;

	if (n_tuples) {
		say_info("Sorting %" PRIu32 " keys in %s index %" PRIu32 "...",
			 n_tuples, index_type_strs[key_def->type], index_id(this));
	}
	uint32_t estimated_tuples = tree.max_size;
	void *nodes = tree.members;

	/* If n_tuples == 0 then estimated_tuples = 0, elem == NULL, tree is empty */
	sptree_index_init(&tree, sizeof(struct tuple *),
			  nodes, n_tuples, estimated_tuples,
			  sptree_index_node_compare_with_key,
			  key_def->is_unique ? sptree_index_node_compare
					     : sptree_index_node_compare_dup,
			  key_def);
}

