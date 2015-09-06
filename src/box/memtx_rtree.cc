/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "memtx_rtree.h"
#include "tuple.h"
#include "space.h"
#include "memtx_engine.h"
#include "errinj.h"
#include "fiber.h"
#include "small/small.h"

/* {{{ Utilities. *************************************************/

/**
 * Extract coordinates of rectangle from message packed string.
 * There must be <count> or <count * 2> numbers in that string.
 */
inline int
mp_decode_rect(struct rtree_rect *rect, unsigned dimension,
	       const char *mp, unsigned count)
{
	if (count == dimension) { /* point */
		for (unsigned i = 0; i < dimension; i++) {
			coord_t c = mp_decode_num(&mp, i);
			rect->coords[i * 2] = c;
			rect->coords[i * 2 + 1] = c;
		}
	} else if (count == dimension * 2) { /* box */
		for (unsigned i = 0; i < dimension; i++) {
			coord_t c = mp_decode_num(&mp, i);
			rect->coords[i * 2] = c;
		}
		for (unsigned i = 0; i < dimension; i++) {
			coord_t c = mp_decode_num(&mp, i + dimension);
			rect->coords[i * 2 + 1] = c;
		}
	} else {
		return -1;
	}
	rtree_rect_normalize(rect, dimension);
	return 0;
}

/**
 * Extract rectangle from message packed string.
 * There must be an array with appropriate number of coordinates in
 * that string.
 */
inline int
mp_decode_rect(struct rtree_rect *rect, unsigned dimension,
	       const char *mp)
{
	uint32_t size = mp_decode_array(&mp);
	return mp_decode_rect(rect, dimension, mp, size);
}

/**
 * Extract rectangle from message packed key.
 * Due to historical issues,
 * in key a rectangle could be written in two variants:
 * a)array with appropriate number of coordinates
 * b)array with on element - array with appropriate number of coordinates
 */
inline int
mp_decode_rect_from_key(struct rtree_rect *rect, unsigned dimension,
			const char *mp, uint32_t part_count)
{
	if (part_count != 1) /* variant a */
		return mp_decode_rect(rect, dimension, mp, part_count);
	else /* variant b */
		return mp_decode_rect(rect, dimension, mp);
}

inline void
extract_rectangle(struct rtree_rect *rect, const struct tuple *tuple,
		  struct key_def *key_def)
{
	assert(key_def->part_count == 1);
	const char *elems = tuple_field(tuple, key_def->parts[0].fieldno);
	unsigned dimension = key_def->opts.dimension;
	if (mp_decode_rect(rect, dimension, elems)) {
		tnt_raise(ClientError, ER_RTREE_RECT,
			  "Field", dimension, dimension * 2);
	}
}
/* {{{ MemtxRTree Iterators ****************************************/

struct index_rtree_iterator {
        struct iterator base;
        struct rtree_iterator impl;
};

static void
index_rtree_iterator_free(struct iterator *i)
{
	struct index_rtree_iterator *itr = (struct index_rtree_iterator *)i;
	rtree_iterator_destroy(&itr->impl);
	delete itr;
}

static struct tuple *
index_rtree_iterator_next(struct iterator *i)
{
	struct index_rtree_iterator *itr = (struct index_rtree_iterator *)i;
	return (struct tuple *)rtree_iterator_next(&itr->impl);
}

/* }}} */

/* {{{ MemtxRTree  **********************************************************/

MemtxRTree::~MemtxRTree()
{
	// Iterator has to be destroyed prior to tree
	if (m_position != NULL) {
		index_rtree_iterator_free(m_position);
		m_position = NULL;
	}
	rtree_destroy(&m_tree);
}

MemtxRTree::MemtxRTree(struct key_def *key_def)
	: MemtxIndex(key_def)
{
	assert(key_def->part_count == 1);
	assert(key_def->parts[0].type = ARRAY);
	assert(key_def->opts.is_unique == false);

	m_dimension = key_def->opts.dimension;
	if (m_dimension < 1 || m_dimension > RTREE_MAX_DIMENSION) {
		char message[64];
		snprintf(message, 64, "dimension (%u) must belong to range "
			 "[%u, %u]", m_dimension, 1, RTREE_MAX_DIMENSION);
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  "RTREE index", message);
	}

	memtx_index_arena_init();
	assert((int)RTREE_EUCLID == (int)RTREE_INDEX_DISTANCE_TYPE_EUCLID);
	assert((int)RTREE_MANHATTAN == (int)RTREE_INDEX_DISTANCE_TYPE_MANHATTAN);
	enum rtree_distance_type distance_type =
		(enum rtree_distance_type)(int)key_def->opts.distance;
	rtree_init(&m_tree, m_dimension, MEMTX_EXTENT_SIZE,
		   memtx_index_extent_alloc, memtx_index_extent_free,
		   distance_type);
}

size_t
MemtxRTree::size() const
{
	return rtree_number_of_records(&m_tree);
}

size_t
MemtxRTree::bsize() const
{
	return rtree_used_size(&m_tree);
}

struct tuple *
MemtxRTree::findByKey(const char *key, uint32_t part_count) const
{
	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);

	rtree_rect rect;
	if (mp_decode_rect_from_key(&rect, m_dimension, key, part_count))
		assert(false);

	struct tuple *result = NULL;
	if (rtree_search(&m_tree, &rect, SOP_OVERLAPS, &iterator))
		result = (struct tuple *)rtree_iterator_next(&iterator);
	rtree_iterator_destroy(&iterator);
	return result;
}

struct tuple *
MemtxRTree::replace(struct tuple *old_tuple, struct tuple *new_tuple,
		    enum dup_replace_mode)
{
	struct rtree_rect rect;
	if (new_tuple) {
		extract_rectangle(&rect, new_tuple, key_def);
		rtree_insert(&m_tree, &rect, new_tuple);
	}
	if (old_tuple) {
		extract_rectangle(&rect, old_tuple, key_def);
		if (!rtree_remove(&m_tree, &rect, old_tuple))
			old_tuple = NULL;
	}
	return old_tuple;
}

struct iterator *
MemtxRTree::allocIterator() const
{
	index_rtree_iterator *it = new index_rtree_iterator;
	memset(it, 0, sizeof(*it));
	rtree_iterator_init(&it->impl);
	if (it == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE,
			  sizeof(struct index_rtree_iterator),
			  "MemtxRTree", "iterator");
	}
	it->base.next = index_rtree_iterator_next;
	it->base.free = index_rtree_iterator_free;
	return &it->base;
}

void
MemtxRTree::initIterator(struct iterator *iterator, enum iterator_type type,
			 const char *key, uint32_t part_count) const
{
	index_rtree_iterator *it = (index_rtree_iterator *)iterator;

	struct rtree_rect rect;
	if (part_count == 0) {
		if (type != ITER_ALL) {
			tnt_raise(ClientError, ER_UNSUPPORTED,
				  "R-Tree index",
				  "It is possible to omit "
				  "key only for ITER_ALL");
		}
	} else if (mp_decode_rect_from_key(&rect, m_dimension,
					   key, part_count)) {
		tnt_raise(ClientError, ER_RTREE_RECT,
			  "Key", m_dimension, m_dimension * 2);
	}

	spatial_search_op op;
	switch (type) {
	case ITER_ALL:
		op = SOP_ALL;
		break;
	case ITER_EQ:
		op = SOP_EQUALS;
		break;
	case ITER_GT:
		op = SOP_STRICT_CONTAINS;
		break;
	case ITER_GE:
		op = SOP_CONTAINS;
		break;
	case ITER_LT:
		op = SOP_STRICT_BELONGS;
		break;
	case ITER_LE:
		op = SOP_BELONGS;
		break;
	case ITER_OVERLAPS:
		op = SOP_OVERLAPS;
		break;
	case ITER_NEIGHBOR:
		op = SOP_NEIGHBOR;
		break;
	default:
		tnt_raise(ClientError, ER_UNSUPPORTED,
			  "RTREE index", "Unsupported search operation for RTREE");
	}
	rtree_search(&m_tree, &rect, op, &it->impl);
}

void
MemtxRTree::beginBuild()
{
	rtree_purge(&m_tree);
}

