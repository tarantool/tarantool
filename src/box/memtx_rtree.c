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
#include <small/mempool.h>

#include "errinj.h"
#include "fiber.h"
#include "trivia/util.h"

#include "tuple.h"
#include "space.h"
#include "memtx_engine.h"

/* {{{ Utilities. *************************************************/

static inline int
mp_decode_num(const char **data, uint32_t fieldno, double *ret)
{
	if (mp_read_double(data, ret) != 0) {
		diag_set(ClientError, ER_FIELD_TYPE,
			 fieldno + TUPLE_INDEX_BASE,
			 field_type_strs[FIELD_TYPE_NUMBER]);
		return -1;
	}
	return 0;
}

/**
 * Extract coordinates of rectangle from message packed string.
 * There must be <count> or <count * 2> numbers in that string.
 */
static inline int
mp_decode_rect(struct rtree_rect *rect, unsigned dimension,
	       const char *mp, unsigned count, const char *what)
{
	coord_t c;
	if (count == dimension) { /* point */
		for (unsigned i = 0; i < dimension; i++) {
			if (mp_decode_num(&mp, i, &c) < 0)
				return -1;
			rect->coords[i * 2] = c;
			rect->coords[i * 2 + 1] = c;
		}
	} else if (count == dimension * 2) { /* box */
		for (unsigned i = 0; i < dimension; i++) {
			if (mp_decode_num(&mp, i, &c) < 0)
				return -1;
			rect->coords[i * 2] = c;
		}
		for (unsigned i = 0; i < dimension; i++) {
			if (mp_decode_num(&mp, i + dimension, &c) < 0)
				return -1;
			rect->coords[i * 2 + 1] = c;
		}
	} else {
		diag_set(ClientError, ER_RTREE_RECT,
			 what, dimension, dimension * 2);
		return -1;
	}
	rtree_rect_normalize(rect, dimension);
	return 0;
}

/**
 * Extract rectangle from message packed key.
 * Due to historical issues,
 * in key a rectangle could be written in two variants:
 * a)array with appropriate number of coordinates
 * b)array with on element - array with appropriate number of coordinates
 */
static inline int
mp_decode_rect_from_key(struct rtree_rect *rect, unsigned dimension,
			const char *mp, uint32_t part_count)
{
	if (part_count == 1)
		part_count = mp_decode_array(&mp);
	return mp_decode_rect(rect, dimension, mp, part_count, "Key");
}

static inline int
extract_rectangle(struct rtree_rect *rect, const struct tuple *tuple,
		  struct index_def *index_def)
{
	assert(index_def->key_def->part_count == 1);
	const char *elems = tuple_field(tuple, index_def->key_def->parts[0].fieldno);
	unsigned dimension = index_def->opts.dimension;
	uint32_t count = mp_decode_array(&elems);
	return mp_decode_rect(rect, dimension, elems, count, "Field");
}
/* {{{ MemtxRTree Iterators ****************************************/

struct index_rtree_iterator {
        struct iterator base;
        struct rtree_iterator impl;
	/** Memory pool the iterator was allocated from. */
	struct mempool *pool;
};

static void
index_rtree_iterator_free(struct iterator *i)
{
	struct index_rtree_iterator *itr = (struct index_rtree_iterator *)i;
	rtree_iterator_destroy(&itr->impl);
	mempool_free(itr->pool, itr);
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

static bool
memtx_rtree_index_def_change_requires_rebuild(struct index *index,
					      const struct index_def *new_def)
{
	if (memtx_index_def_change_requires_rebuild(index, new_def))
		return true;
	if (index->def->opts.distance != new_def->opts.distance ||
	    index->def->opts.dimension != new_def->opts.dimension)
		return true;
	return false;

}

static ssize_t
memtx_rtree_index_size(struct index *base)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	return rtree_number_of_records(&index->tree);
}

static ssize_t
memtx_rtree_index_bsize(struct index *base)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	return rtree_used_size(&index->tree);
}

static ssize_t
memtx_rtree_index_count(struct index *base, enum iterator_type type,
			const char *key, uint32_t part_count)
{
	if (type == ITER_ALL)
		return memtx_rtree_index_size(base); /* optimization */
	return generic_index_count(base, type, key, part_count);
}

static int
memtx_rtree_index_get(struct index *base, const char *key,
		      uint32_t part_count, struct tuple **result)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	struct rtree_iterator iterator;
	rtree_iterator_init(&iterator);

	struct rtree_rect rect;
	if (mp_decode_rect_from_key(&rect, index->dimension, key, part_count))
		unreachable();

	*result = NULL;
	if (rtree_search(&index->tree, &rect, SOP_OVERLAPS, &iterator))
		*result = (struct tuple *)rtree_iterator_next(&iterator);
	rtree_iterator_destroy(&iterator);
	return 0;
}

static int
memtx_rtree_index_replace(struct index *base, struct tuple *old_tuple,
			  struct tuple *new_tuple, enum dup_replace_mode mode,
			  struct tuple **result)
{
	(void)mode;
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	struct rtree_rect rect;
	if (new_tuple) {
		if (extract_rectangle(&rect, new_tuple, base->def) != 0)
			return -1;
		rtree_insert(&index->tree, &rect, new_tuple);
	}
	if (old_tuple) {
		if (extract_rectangle(&rect, old_tuple, base->def) != 0)
			return -1;
		if (!rtree_remove(&index->tree, &rect, old_tuple))
			old_tuple = NULL;
	}
	*result = old_tuple;
	return 0;
}

static struct iterator *
memtx_rtree_index_create_iterator(struct index *base,  enum iterator_type type,
				  const char *key, uint32_t part_count)
{
	struct memtx_rtree_index *index = (struct memtx_rtree_index *)base;
	struct memtx_engine *memtx = (struct memtx_engine *)base->engine;

	struct rtree_rect rect;
	if (part_count == 0) {
		if (type != ITER_ALL) {
			diag_set(UnsupportedIndexFeature, base->def,
				 "empty keys for requested iterator type");
			return NULL;
		}
	} else if (mp_decode_rect_from_key(&rect, index->dimension,
					   key, part_count)) {
		return NULL;
	}

	enum spatial_search_op op;
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
		diag_set(UnsupportedIndexFeature, base->def,
			 "requested iterator type");
		return NULL;
	}

	struct index_rtree_iterator *it = mempool_alloc(&memtx->rtree_iterator_pool);
	if (it == NULL) {
		diag_set(OutOfMemory, sizeof(struct index_rtree_iterator),
			 "memtx_rtree_index", "iterator");
		return NULL;
	}
	iterator_create(&it->base, base);
	it->pool = &memtx->rtree_iterator_pool;
	it->base.next = index_rtree_iterator_next;
	it->base.free = index_rtree_iterator_free;
	rtree_iterator_init(&it->impl);
	rtree_search(&index->tree, &rect, op, &it->impl);
	return (struct iterator *)it;
}

static const struct index_vtab memtx_rtree_index_vtab = {
	/* .destroy = */ memtx_rtree_index_destroy,
	/* .commit_create = */ generic_index_commit_create,
	/* .abort_create = */ memtx_index_abort_create,
	/* .commit_modify = */ generic_index_commit_modify,
	/* .commit_drop = */ memtx_index_commit_drop,
	/* .update_def = */ generic_index_update_def,
	/* .depends_on_pk = */ generic_index_depends_on_pk,
	/* .def_change_requires_rebuild = */
		memtx_rtree_index_def_change_requires_rebuild,
	/* .size = */ memtx_rtree_index_size,
	/* .bsize = */ memtx_rtree_index_bsize,
	/* .min = */ generic_index_min,
	/* .max = */ generic_index_max,
	/* .random = */ generic_index_random,
	/* .count = */ memtx_rtree_index_count,
	/* .get = */ memtx_rtree_index_get,
	/* .replace = */ memtx_rtree_index_replace,
	/* .create_iterator = */ memtx_rtree_index_create_iterator,
	/* .create_snapshot_iterator = */
		generic_index_create_snapshot_iterator,
	/* .info = */ generic_index_info,
	/* .reset_stat = */ generic_index_reset_stat,
	/* .begin_build = */ generic_index_begin_build,
	/* .reserve = */ generic_index_reserve,
	/* .build_next = */ generic_index_build_next,
	/* .end_build = */ generic_index_end_build,
};

struct memtx_rtree_index *
memtx_rtree_index_new(struct memtx_engine *memtx, struct index_def *def)
{
	assert(def->key_def->part_count == 1);
	assert(def->key_def->parts[0].type == FIELD_TYPE_ARRAY);
	assert(def->opts.is_unique == false);

	if (def->opts.dimension < 1 ||
	    def->opts.dimension > RTREE_MAX_DIMENSION) {
		diag_set(UnsupportedIndexFeature, def,
			 tt_sprintf("dimension (%lld): must belong to "
				    "range [%u, %u]", def->opts.dimension,
				    1, RTREE_MAX_DIMENSION));
		return NULL;
	}

	assert((int)RTREE_EUCLID == (int)RTREE_INDEX_DISTANCE_TYPE_EUCLID);
	assert((int)RTREE_MANHATTAN == (int)RTREE_INDEX_DISTANCE_TYPE_MANHATTAN);
	enum rtree_distance_type distance_type =
		(enum rtree_distance_type)def->opts.distance;

	memtx_index_arena_init();

	if (!mempool_is_initialized(&memtx->rtree_iterator_pool)) {
		mempool_create(&memtx->rtree_iterator_pool, cord_slab_cache(),
			       sizeof(struct index_rtree_iterator));
	}

	struct memtx_rtree_index *index =
		(struct memtx_rtree_index *)calloc(1, sizeof(*index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct memtx_rtree_index");
		return NULL;
	}
	if (index_create(&index->base, (struct engine *)memtx,
			 &memtx_rtree_index_vtab, def) != 0) {
		free(index);
		return NULL;
	}

	index->dimension = def->opts.dimension;
	rtree_init(&index->tree, index->dimension, MEMTX_EXTENT_SIZE,
		   memtx_index_extent_alloc, memtx_index_extent_free, NULL,
		   distance_type);
	return index;
}
