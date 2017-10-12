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
#include "memtx_rtree.h"

#include <small/small.h>

#include "errinj.h"
#include "fiber.h"
#include "trivia/util.h"

#include "tuple.h"
#include "space.h"
#include "memtx_engine.h"
#include "memtx_index.h"

/* {{{ Utilities. *************************************************/

static inline double
mp_decode_num(const char **data, uint32_t fieldno)
{
	double val;
	if (mp_read_double(data, &val) != 0) {
		tnt_raise(ClientError, ER_FIELD_TYPE,
			  fieldno + TUPLE_INDEX_BASE,
			  field_type_strs[FIELD_TYPE_NUMBER]);
	}
	return val;
}

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
		  struct index_def *index_def)
{
	assert(index_def->key_def->part_count == 1);
	const char *elems = tuple_field(tuple, index_def->key_def->parts[0].fieldno);
	unsigned dimension = index_def->opts.dimension;
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
	TRASH(itr);
	free(itr);
}

static int
index_rtree_iterator_next(struct iterator *i, struct tuple **ret)
{
	struct index_rtree_iterator *itr = (struct index_rtree_iterator *)i;
	*ret = (struct tuple *)rtree_iterator_next(&itr->impl);
	return 0;
}

/* }}} */

/* {{{ MemtxRTree  **********************************************************/

static void
memtx_rtree_index_destroy(struct index *base)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	rtree_destroy(&index->tree);
	free(index);
}

static size_t
memtx_rtree_index_size(struct index *base)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	return rtree_number_of_records(&index->tree);
}

static size_t
memtx_rtree_index_bsize(struct index *base)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	return rtree_used_size(&index->tree);
}

static struct tuple *
memtx_rtree_index_get(struct index *base, const char *key, uint32_t part_count)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);

	rtree_rect rect;
	if (mp_decode_rect_from_key(&rect, index->dimension, key, part_count))
		unreachable();

	struct tuple *result = NULL;
	if (rtree_search(&index->tree, &rect, SOP_OVERLAPS, &iterator))
		result = (struct tuple *)rtree_iterator_next(&iterator);
	rtree_iterator_destroy(&iterator);
	return result;
}

static struct tuple *
memtx_rtree_index_replace(struct index *base, struct tuple *old_tuple,
			  struct tuple *new_tuple, enum dup_replace_mode)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	struct rtree_rect rect;
	if (new_tuple) {
		extract_rectangle(&rect, new_tuple, base->def);
		rtree_insert(&index->tree, &rect, new_tuple);
	}
	if (old_tuple) {
		extract_rectangle(&rect, old_tuple, base->def);
		if (!rtree_remove(&index->tree, &rect, old_tuple))
			old_tuple = NULL;
	}
	return old_tuple;
}

static struct iterator *
memtx_rtree_index_alloc_iterator(void)
{
	struct index_rtree_iterator *it = (struct index_rtree_iterator *)
		malloc(sizeof(*it));
	if (it == NULL) {
		tnt_raise(OutOfMemory, sizeof(struct index_rtree_iterator),
			  "memtx_rtree_index", "iterator");
	}
	memset(it, 0, sizeof(*it));
	rtree_iterator_init(&it->impl);
	it->base.next = index_rtree_iterator_next;
	it->base.free = index_rtree_iterator_free;
	return &it->base;
}

static void
memtx_rtree_index_init_iterator(struct index *base, struct iterator *iterator,
				enum iterator_type type,
				const char *key, uint32_t part_count)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	struct index_rtree_iterator *it = (index_rtree_iterator *)iterator;

	struct rtree_rect rect;
	if (part_count == 0) {
		if (type != ITER_ALL) {
			tnt_raise(UnsupportedIndexFeature, base->def,
				  "empty keys for requested iterator type");
		}
	} else if (mp_decode_rect_from_key(&rect, index->dimension,
					   key, part_count)) {
		tnt_raise(ClientError, ER_RTREE_RECT,
			  "Key", index->dimension, index->dimension * 2);
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
		tnt_raise(UnsupportedIndexFeature, base->def,
			  "requested iterator type");
	}
	rtree_search(&index->tree, &rect, op, &it->impl);
}

static void
memtx_rtree_index_begin_build(struct index *base)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	rtree_purge(&index->tree);
}

static const struct index_vtab memtx_rtree_index_vtab = {
	/* .destroy = */ memtx_rtree_index_destroy,
	/* .commit_create = */ generic_index_commit_create,
	/* .commit_drop = */ generic_index_commit_drop,
	/* .size = */ memtx_rtree_index_size,
	/* .bsize = */ memtx_rtree_index_bsize,
	/* .min = */ memtx_index_min,
	/* .max = */ memtx_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ memtx_index_count,
	/* .get = */ memtx_rtree_index_get,
	/* .replace = */ memtx_rtree_index_replace,
	/* .alloc_iterator = */ memtx_rtree_index_alloc_iterator,
	/* .init_iterator = */ memtx_rtree_index_init_iterator,
	/* .create_snapshot_iterator = */
		generic_index_create_snapshot_iterator,
	/* .info = */ generic_index_info,
	/* .begin_build = */ memtx_rtree_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

struct memtx_rtree_index *
memtx_rtree_index_new(struct index_def *def)
{
	assert(def->key_def->part_count == 1);
	assert(def->key_def->parts[0].type == FIELD_TYPE_ARRAY);
	assert(def->opts.is_unique == false);

	if (def->opts.dimension < 1 ||
	    def->opts.dimension > RTREE_MAX_DIMENSION) {
		tnt_raise(UnsupportedIndexFeature, def,
			  tt_sprintf("dimension (%u): must belong to "
				     "range [%u, %u]", def->opts.dimension,
				     1, RTREE_MAX_DIMENSION));
	}

	assert((int)RTREE_EUCLID == (int)RTREE_INDEX_DISTANCE_TYPE_EUCLID);
	assert((int)RTREE_MANHATTAN == (int)RTREE_INDEX_DISTANCE_TYPE_MANHATTAN);
	enum rtree_distance_type distance_type =
		(enum rtree_distance_type)def->opts.distance;

	memtx_index_arena_init();

	struct memtx_rtree_index *index =
		(struct memtx_rtree_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		tnt_raise(OutOfMemory, sizeof(*index),
			  "malloc", "struct memtx_rtree_index");
	}
	if (index_create(&index->base, &memtx_rtree_index_vtab, def) != 0) {
		free(index);
		diag_raise();
	}

	index->dimension = def->opts.dimension;
	rtree_init(&index->tree, index->dimension, MEMTX_EXTENT_SIZE,
		   memtx_index_extent_alloc, memtx_index_extent_free, NULL,
		   distance_type);
	return index;
}
