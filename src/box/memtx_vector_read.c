/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "memtx_vector.h"

#include "box.h"
#include "clock.h"
#include "fiber.h"
#include "index.h"
#include "memtx_engine.h"
#include "mp_embedding.h"
#include "port.h"
#include "space.h"
#include "space_cache.h"
#include "tuple.h"
#include "txn.h"
#include "small/region.h"
#include "lib/ann/ann_numeric.h"
#include "lib/ann/ann_vector.h"
#include "lib/ann/ann_hnsw.h"

/** Bounded top-k is exposed through the same next/free cursor contract. */
struct vector_search_iterator {
	/** Generic index iterator API. */
	struct iterator base;
	/** Pinned index owning the backend. */
	struct index *index;
	/** Options whose variable-sized buffers follow this object. */
	struct memtx_vector_search_opts opts;
	/** Retained top-k tuples. */
	struct port port;
	/** Next result in the materialized bounded batch. */
	struct port_c_entry *entry;
	/** Whether the port has been initialized. */
	bool started;
};

static void
vector_search_iterator_free(struct iterator *base)
{
	struct vector_search_iterator *it =
		(struct vector_search_iterator *)base;
	if (it->started)
		port_destroy(&it->port);
	index_unref(it->index);
	free(it);
}

/** The bounded batch is built on first next, not during cursor creation. */
static int
vector_search_iterator_next(struct iterator *base, struct tuple **out)
{
	struct vector_search_iterator *it =
		(struct vector_search_iterator *)base;
	*out = NULL;
	if (!it->started) {
		port_c_create(&it->port);
		it->started = true;
		if (memtx_vector_index_search(it->index, &it->opts,
					      &it->port, NULL) != 0)
			return -1;
		it->entry = ((struct port_c *)&it->port)->first;
	}
	if (it->entry != NULL) {
		*out = it->entry->tuple;
		it->entry = it->entry->next;
	}
	return 0;
}

struct iterator *
memtx_vector_search_iterator_new(struct index *index,
				 const struct memtx_vector_search_opts *opts)
{
	size_t key_size = opts->key_end - opts->key;
	size_t filter_size = opts->filter == NULL ? 0 :
		opts->filter_end - opts->filter;
	size_t size = sizeof(struct vector_search_iterator) + key_size +
		      filter_size;
	struct vector_search_iterator *it = xcalloc(1, size);
	iterator_create(&it->base, index);
	it->base.next_internal = vector_search_iterator_next;
	it->base.next = memtx_iterator_next;
	it->base.position = generic_iterator_position;
	it->base.free = vector_search_iterator_free;
	it->index = index;
	index_ref(index);
	it->opts = *opts;
	it->opts.key = (char *)(it + 1);
	memcpy((void *)it->opts.key, opts->key, key_size);
	it->opts.key_end = it->opts.key + key_size;
	if (filter_size != 0) {
		it->opts.filter = it->opts.key_end;
		memcpy((void *)it->opts.filter, opts->filter, filter_size);
		it->opts.filter_end = it->opts.filter + filter_size;
	}
	return &it->base;
}

ssize_t
memtx_vector_index_count(struct index *index, enum iterator_type type,
			 const char *key, uint32_t part_count)
{
	(void)key;
	struct txn *txn = in_txn();
	if ((txn != NULL && txn->isolation == TXN_ISOLATION_LINEARIZABLE) ||
	    type != ITER_ALL || part_count != 0) {
		diag_set(ClientError, ER_VECTOR_UNSUPPORTED);
		return -1;
	}
	struct space *space = space_by_id(index->def->space_id);
	return index_count(space->index[0], ITER_ALL, NULL, 0);
}

/** Frozen topology and primary tuple visibility form one read view. */
struct vector_read_view {
	/** Public index read-view interface. */
	struct index_read_view base;
	/** Point lookups resolve frozen tuple versions for graph candidates. */
	struct index_read_view *primary;
	/** Independently pinned CoW graph root and search contexts. */
	struct memtx_vector_snapshot *snapshot;
};

/** Private bounded search result; no primary-index scan is performed. */
struct vector_read_view_result {
	/** Number of retained tuple copies. */
	uint32_t count;
	/** Next row to return. */
	uint32_t pos;
	/** Owned copies of at most the explicitly requested limit rows. */
	struct read_view_tuple rows[];
};

/** Visibility is resolved through the frozen primary index, never live maps. */
struct vector_read_view_filter {
	/** Frozen index definitions and tuple lookup. */
	struct vector_read_view *view;
	/** Request budget shared by lookups and coordinate comparisons. */
	struct ann_search_control *control;
	/** First failure raised by the predicate. */
	int rc;
};

/** The opaque cursor holds either a bounded ANN result or a primary cursor. */
struct vector_read_view_iterator {
	/** Public read-view iterator interface. */
	struct index_read_view_iterator_base base;
	/** Materialized bounded search, or NULL for an ALL iterator. */
	struct vector_read_view_result *result;
	/** Primary ALL iterator when no distance order is requested. */
	struct index_read_view_iterator *source;
};

static_assert(sizeof(struct vector_read_view_iterator) <=
	      INDEX_READ_VIEW_ITERATOR_SIZE, "vector read-view iterator size");

/** Decode a search key using the frozen index configuration. */
static int
vector_read_view_decode(const char *data, uint32_t dimension,
			enum ann_metric metric, float *out)
{
	if (data == NULL)
		goto invalid;

	if (mp_typeof(*data) == MP_EXT) {
		uint32_t size;
		const char *bytes = mp_decode_embedding(&data, &size);
		if (bytes == NULL || size != dimension)
			goto invalid;
		for (uint32_t i = 0; i < dimension; ++i)
			out[i] = embedding_f32le_load(bytes, i);
	} else {
		if (mp_typeof(*data) != MP_ARRAY ||
		    mp_decode_array(&data) != dimension)
			goto invalid;
		for (uint32_t i = 0; i < dimension; ++i) {
			double value;
			if (mp_read_double(&data, &value) != 0 ||
			    ann_f32_from_double(value, &out[i]) != ANN_OK)
				goto invalid;
		}
	}
	if (ann_vector_validate_f32(out, dimension, metric) == ANN_OK)
		return 0;
invalid:
	diag_set(ClientError, ER_VECTOR_INVALID);
	return -1;
}

/** Resolve a coordinate owner to its tuple in this snapshot. */
static int
vector_read_view_lookup(struct vector_read_view_filter *filter,
			const struct ann_vector *vector,
			struct read_view_tuple *raw)
{
	*raw = read_view_tuple_none();
	struct tuple *owner = ann_vector_owner(vector);
	assert(owner != NULL);
	struct index_read_view *primary = filter->view->primary;
	uint32_t key_size;
	const char *key = tuple_extract_key(owner, primary->def->key_def,
					    MULTIKEY_NONE, &key_size);
	if (key == NULL)
		return -1;
	uint32_t part_count = mp_decode_array(&key);
	if (index_read_view_get_raw(primary, key, part_count, raw) != 0)
		return -1;
	if (raw->data == NULL)
		return 0;
	const char *field = tuple_field_raw_by_part(
		tuple_format_runtime, raw->data, NULL,
		filter->view->base.def->key_def->parts, MULTIKEY_NONE);
	uint32_t dimension;
	const char *bytes = field == NULL ? NULL :
		mp_decode_embedding(&field, &dimension);
	if (bytes == NULL || dimension != ann_vector_dimension(vector)) {
		*raw = read_view_tuple_none();
		return 0;
	}
	const char *expected = ann_vector_data(vector);
	for (uint32_t i = 0; i < dimension; ++i) {
		enum ann_status status =
			ann_search_control_step(filter->control);
		if (status != ANN_OK) {
			if (status == ANN_STOPPED) {
				diag_set(FiberIsCancelled);
			} else {
				int code = ER_VECTOR_WORK_LIMIT;
				if (status == ANN_TIMEOUT)
					code = ER_VECTOR_TIMEOUT;
				diag_set(ClientError, code);
			}
			return -1;
		}
		if (memcmp(bytes + i * sizeof(float), expected + i * sizeof(float),
			   sizeof(float)) != 0) {
			*raw = read_view_tuple_none();
			return 0;
		}
	}
	return 0;
}

/** Rejected versions may still be used to navigate the frozen graph. */
static bool
vector_read_view_accept(uint64_t label, const struct ann_vector *vector,
			void *ctx)
{
	(void)label;
	struct vector_read_view_filter *filter = ctx;
	if (filter->rc != 0)
		return false;
	size_t svp = region_used(&fiber()->gc);
	struct read_view_tuple raw;
	filter->rc = vector_read_view_lookup(filter, vector, &raw);
	bool accept = filter->rc == 0 && raw.data != NULL;
	region_truncate(&fiber()->gc, svp);
	return accept;
}

static void
vector_read_view_result_delete(struct vector_read_view_result *result)
{
	for (uint32_t i = 0; i < result->count; ++i)
		free((void *)result->rows[i].data);
	free(result);
}

static void
vector_read_view_free(struct index_read_view *base)
{
	struct vector_read_view *view = (struct vector_read_view *)base;
	index_read_view_delete(view->primary);
	memtx_vector_snapshot_delete(view->snapshot);
	free(view);
}

static void
vector_read_view_iterator_destroy(struct index_read_view_iterator *base)
{
	struct vector_read_view_iterator *it =
		(struct vector_read_view_iterator *)base;
	if (it->result != NULL)
		vector_read_view_result_delete(it->result);
	if (it->source != NULL) {
		index_read_view_iterator_destroy(it->source);
		free(it->source);
	}
}

/** Advance through the frozen scan or materialized neighbors. */
static int
vector_read_view_next(struct index_read_view_iterator *base,
		      struct read_view_tuple *out)
{
	struct vector_read_view_iterator *it =
		(struct vector_read_view_iterator *)base;
	if (it->source != NULL)
		return index_read_view_iterator_next_raw(it->source, out);
	struct vector_read_view_result *result = it->result;
	*out = result->pos == result->count ? read_view_tuple_none() :
		result->rows[result->pos++];
	return 0;
}

static void
vector_read_view_iterator_create(struct index_read_view *view,
				 struct index_read_view_iterator *iterator)
{
	struct vector_read_view_iterator *it =
		(struct vector_read_view_iterator *)iterator;
	memset(it, 0, sizeof(*it));
	it->base.index = view;
	it->base.destroy = vector_read_view_iterator_destroy;
	it->base.next_raw = vector_read_view_next;
	it->base.position = generic_index_read_view_iterator_position;
}

static uint64_t
vector_read_view_now_ns(void *ctx)
{
	(void)ctx;
	return clock_monotonic64();
}

int
memtx_vector_read_view_select(struct index_read_view *base,
			      const char *key, uint32_t part_count,
			      uint32_t limit,
			      struct index_read_view_iterator *iterator)
{
	if (part_count != 1 || limit > 1024) {
		diag_set(ClientError, ER_VECTOR_UNSUPPORTED);
		return -1;
	}
	struct vector_read_view *view = (struct vector_read_view *)base;
	struct ann_search_control control = {
		.deadline_ns = clock_monotonic64() + 1000000000,
		.work_limit = 1ULL << 32,
		.now_ns = vector_read_view_now_ns,
	};
	float query[MEMTX_VECTOR_MAX_DIMENSION];
	enum ann_metric metric =
		(enum ann_metric)base->def->opts.vector_distance;
	if (vector_read_view_decode(key, base->def->opts.dimension,
				    metric, query) != 0)
		return -1;
	vector_read_view_iterator_create(base, iterator);
	struct vector_read_view_iterator *it =
		(struct vector_read_view_iterator *)iterator;
	it->result = xcalloc(1, sizeof(*it->result) +
			     limit * sizeof(*it->result->rows));
	struct ann_candidate *candidates = xcalloc(MAX(limit, 1),
						   sizeof(*candidates));
	struct vector_read_view_filter ctx = {view, &control, 0};
	struct ann_filter filter = {vector_read_view_accept, &ctx};
	struct ann_hnsw_search_opts algorithm = {
		.expansion = MAX(limit, base->def->opts.vector_ef_search),
		.compare_equal = memtx_vector_compare_candidates,
		.compare_ctx = view->primary->def->key_def,
	};
	struct ann_search_opts opts = {
		.candidate_limit = limit,
		.query_dimension = base->def->opts.dimension,
		.filter = &filter,
		.control = &control,
		.algorithm = &algorithm,
	};
	uint32_t count;
	int rc = memtx_vector_snapshot_search(view->snapshot, query, &opts,
					      candidates, &count);
	if (rc != 0 || ctx.rc != 0)
		goto fail;
	for (uint32_t i = 0; i < count; ++i) {
		size_t svp = region_used(&fiber()->gc);
		struct read_view_tuple row;
		rc = vector_read_view_lookup(&ctx, candidates[i].vector, &row);
		if (rc != 0) {
			region_truncate(&fiber()->gc, svp);
			goto fail;
		}
		if (row.data == NULL) {
			region_truncate(&fiber()->gc, svp);
			continue;
		}
		char *copy = xmalloc(row.size);
		memcpy(copy, row.data, row.size);
		row.data = copy;
		row.ptr = NULL;
		region_truncate(&fiber()->gc, svp);
		it->result->rows[it->result->count++] = row;
	}
	free(candidates);
	return 0;
fail:
	free(candidates);
	vector_read_view_iterator_destroy(iterator);
	return -1;
}

/** Delegate unranked scans to the frozen primary index. */
static int
vector_read_view_create_iterator(struct index_read_view *base,
				 enum iterator_type type, const char *key,
				 uint32_t part_count, const char *pos,
				 struct index_read_view_iterator *iterator)
{
	(void)key;
	if (pos != NULL || type != ITER_ALL || part_count != 0) {
		diag_set(ClientError, ER_VECTOR_UNSUPPORTED);
		return -1;
	}
	struct vector_read_view *view = (struct vector_read_view *)base;
	vector_read_view_iterator_create(base, iterator);
	struct vector_read_view_iterator *it =
		(struct vector_read_view_iterator *)iterator;
	it->source = xmalloc(sizeof(*it->source));
	if (index_read_view_create_iterator(view->primary, ITER_ALL,
					    NULL, 0, it->source) != 0) {
		free(it->source);
		it->source = NULL;
		return -1;
	}
	return 0;
}

static int
vector_read_view_get_raw(struct index_read_view *base, const char *key,
			 uint32_t part_count, struct read_view_tuple *out)
{
	(void)key;
	(void)part_count;
	(void)out;
	diag_set(UnsupportedIndexFeature, base->def, "get()");
	return -1;
}

struct index_read_view *
memtx_vector_read_view_new(struct index *index)
{
	static const struct index_read_view_vtab vtab = {
		.free = vector_read_view_free,
		.count = generic_index_read_view_count,
		.quantile = generic_index_read_view_quantile,
		.get_raw = vector_read_view_get_raw,
		.create_iterator = vector_read_view_create_iterator,
		.create_iterator_with_offset =
			generic_index_read_view_create_iterator_with_offset,
		.create_arrow_stream =
			generic_index_read_view_create_arrow_stream,
	};
	struct vector_read_view *view = xmalloc(sizeof(*view));
	struct space *space = space_by_id(index->def->space_id);
	view->primary = index_create_read_view(space->index[0]);
	if (view->primary == NULL) {
		free(view);
		return NULL;
	}
	view->snapshot = memtx_vector_snapshot_new(index);
	if (view->snapshot == NULL) {
		index_read_view_delete(view->primary);
		free(view);
		return NULL;
	}
	index_read_view_create(&view->base, &vtab, index->def);
	return &view->base;
}

void
memtx_vector_read_view_set_space(struct index_read_view *base,
				 struct space_read_view *space)
{
	struct vector_read_view *view = (struct vector_read_view *)base;
	view->primary->space = space;
}
