/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ann_backend.h"
#include "ann_numeric.h"
#include "ann_vector.h"

#include <assert.h>
#include <math.h>
#include <string.h>

/** One stored version; retired records remain filter-addressable. */
struct ann_flat_entry {
	/** Opaque label supplied by the caller. */
	uint64_t label;
	/** Native or packed coordinates. */
	const void *vector;
	/** Shared coordinate owner, or NULL for a backend-owned copy. */
	struct ann_vector *shared;
	/** Whether the current view may return this version. */
	bool live;
};

/** Flat backend instance with a stack of prepared mutations. */
struct ann_backend {
	/** Immutable vector configuration. */
	struct ann_config config;
	/** Copied allocator callbacks and their owner. */
	struct ann_memory memory;
	/** Array of retained versions. */
	struct ann_flat_entry *entries;
	/** Number of occupied entries. */
	uint32_t count;
	/** Number of allocated entry slots. */
	uint32_t capacity;
	/** Top undo record; changes finish in reverse application order. */
	struct ann_change *pending;
	/** Current charged bytes, including a prepared change. */
	uint64_t resident_bytes;
};

/** Prepared insert or retire with enough state for allocation-free undo. */
struct ann_change {
	/** Backend that owns this change. */
	struct ann_backend *backend;
	/** Older applied undo record. */
	struct ann_change *previous;
	/** Mutation type. */
	enum ann_change_kind kind;
	/** Target label. */
	uint64_t label;
	/** Prepared vector, transferred to an entry on apply. */
	const void *vector;
	/** Shared owner transferred to an entry on apply. */
	struct ann_vector *shared;
	/** Target slot for a retire. */
	uint32_t slot;
	/** True after a successful application. */
	bool applied;
	/** Undo has consumed the change; it cannot be applied again. */
	bool rolled_back;
};

/** Charge allocations to this backend before publishing them. */
static void *
ann_flat_alloc(struct ann_backend *backend, size_t size)
{
	void *ptr = backend->memory.alloc(backend->memory.ctx, size);
	if (ptr != NULL)
		backend->resident_bytes += size;
	return ptr;
}

static void
ann_flat_free(struct ann_backend *backend, void *ptr, size_t size)
{
	if (ptr == NULL)
		return;
	backend->resident_bytes -= size;
	backend->memory.free(backend->memory.ctx, ptr, size);
}

static size_t
ann_flat_vector_size(const struct ann_backend *backend)
{
	return (size_t)backend->config.dimension * sizeof(float);
}

/** Shared payloads are charged to their coordinate owner. */
static void
ann_flat_release_vector(struct ann_backend *backend, const void *vector,
			struct ann_vector *shared)
{
	if (shared != NULL)
		ann_vector_unref(shared);
	else
		ann_flat_free(backend, (void *)vector,
			      ann_flat_vector_size(backend));
}

static enum ann_status
ann_flat_create(const struct ann_config *config,
		const struct ann_memory *memory, struct ann_backend **out)
{
	if (out == NULL)
		return ANN_INVALID;
	*out = NULL;
	if (config == NULL || memory == NULL ||
	    memory->alloc == NULL || memory->free == NULL ||
	    config->algorithm != NULL ||
	    config->dimension == 0 ||
	    ((size_t)config->dimension * sizeof(float)) / sizeof(float) !=
		config->dimension ||
	    (unsigned)config->metric > ANN_IP)
		return ANN_INVALID;
	struct ann_backend *backend =
		memory->alloc(memory->ctx, sizeof(*backend));
	if (backend == NULL)
		return ANN_OUT_OF_MEMORY;
	memset(backend, 0, sizeof(*backend));
	backend->config = *config;
	backend->memory = *memory;
	backend->resident_bytes = sizeof(*backend);
	*out = backend;
	return ANN_OK;
}

static void
ann_flat_destroy(struct ann_backend *backend)
{
	if (backend == NULL)
		return;
	for (struct ann_change *change = backend->pending; change != NULL;) {
		struct ann_change *previous = change->previous;
		ann_flat_release_vector(backend, change->vector,
					change->shared);
		ann_flat_free(backend, change, sizeof(*change));
		change = previous;
	}
	for (uint32_t i = 0; i < backend->count; ++i)
		ann_flat_release_vector(backend, backend->entries[i].vector,
					backend->entries[i].shared);
	ann_flat_free(backend, backend->entries,
		      (size_t)backend->capacity * sizeof(*backend->entries));
	backend->memory.free(backend->memory.ctx, backend, sizeof(*backend));
}

/** Grow the entry array without changing any stored labels. */
static enum ann_status
ann_flat_reserve(struct ann_backend *backend, uint32_t capacity)
{
	if (backend == NULL)
		return ANN_INVALID;
	if (backend->pending != NULL && !backend->pending->applied)
		return ANN_BUSY;
	if (capacity <= backend->capacity)
		return ANN_OK;
	size_t size = (size_t)capacity * sizeof(*backend->entries);
	if (size / sizeof(*backend->entries) != capacity)
		return ANN_INVALID;
	struct ann_flat_entry *entries = ann_flat_alloc(backend, size);
	if (entries == NULL)
		return ANN_OUT_OF_MEMORY;
	if (backend->count != 0)
		memcpy(entries, backend->entries,
		       (size_t)backend->count * sizeof(*entries));
	ann_flat_free(backend, backend->entries,
		      (size_t)backend->capacity * sizeof(*entries));
	backend->entries = entries;
	backend->capacity = capacity;
	return ANN_OK;
}

/** Return the physical slot of a label, including retired rows. */
static int64_t
ann_flat_find(const struct ann_backend *backend, uint64_t label)
{
	for (uint32_t i = 0; i < backend->count; ++i) {
		if (backend->entries[i].label == label)
			return i;
	}
	return -1;
}

/** Stage coordinates and keep the current entries readable. */
static enum ann_status
ann_flat_prepare_impl(struct ann_backend *backend, enum ann_change_kind kind,
		      uint64_t label, const void *vector, uint32_t dimension,
		      struct ann_change **out, struct ann_vector *shared)
{
	if (out == NULL)
		return ANN_INVALID;
	*out = NULL;
	if (backend == NULL ||
	    (kind != ANN_INSERT && kind != ANN_RETIRE))
		return ANN_INVALID;
	if (backend->pending != NULL && !backend->pending->applied)
		return ANN_BUSY;
	int64_t slot = ann_flat_find(backend, label);
	if (kind == ANN_INSERT) {
		if (dimension != backend->config.dimension)
			return ANN_INVALID;
		if (slot >= 0)
			return ANN_EXISTS;
		enum ann_status valid = shared != NULL &&
			ann_vector_is_borrowed(shared) ?
			ann_vector_validate_le(vector, dimension,
					       backend->config.metric) :
			ann_vector_validate_f32(vector, dimension,
						backend->config.metric);
		if (valid != ANN_OK)
			return ANN_INVALID;
		if (backend->count == UINT32_MAX)
			return ANN_INVALID;
		if (backend->count == backend->capacity) {
			uint32_t next = backend->capacity == 0 ? 4 :
				backend->capacity > UINT32_MAX / 2 ?
				UINT32_MAX : backend->capacity * 2;
			enum ann_status status =
				ann_flat_reserve(backend, next);
			if (status != ANN_OK)
				return status;
		}
	} else if (dimension != 0 || vector != NULL) {
		return ANN_INVALID;
	} else if (slot < 0 || !backend->entries[slot].live) {
		return ANN_NOT_FOUND;
	}
	struct ann_change *change = ann_flat_alloc(backend, sizeof(*change));
	if (change == NULL)
		return ANN_OUT_OF_MEMORY;
	memset(change, 0, sizeof(*change));
	change->backend = backend;
	change->previous = backend->pending;
	change->kind = kind;
	change->label = label;
	change->slot = kind == ANN_RETIRE ? (uint32_t)slot : backend->count;
	if (kind == ANN_INSERT && shared != NULL) {
		ann_vector_ref(shared);
		change->shared = shared;
		change->vector = vector;
	} else if (kind == ANN_INSERT) {
		size_t size = ann_flat_vector_size(backend);
		change->vector = ann_flat_alloc(backend, size);
		if (change->vector == NULL) {
			ann_flat_free(backend, change, sizeof(*change));
			return ANN_OUT_OF_MEMORY;
		}
		memcpy((void *)change->vector, vector, size);
	}
	backend->pending = change;
	*out = change;
	return ANN_OK;
}

static enum ann_status
ann_flat_prepare(struct ann_backend *backend, enum ann_change_kind kind,
		 uint64_t label, const float *vector, uint32_t dimension,
		 struct ann_change **out)
{
	return ann_flat_prepare_impl(backend, kind, label, vector, dimension,
				     out, NULL);
}

/** Retain the same owner across independently committed backend generations. */
static enum ann_status
ann_flat_prepare_shared(struct ann_backend *backend, uint64_t label,
			struct ann_vector *vector, struct ann_change **out)
{
	if (vector == NULL) {
		if (out != NULL)
			*out = NULL;
		return ANN_INVALID;
	}
	return ann_flat_prepare_impl(backend, ANN_INSERT, label,
				     ann_vector_data(vector),
				     ann_vector_dimension(vector), out, vector);
}

static bool
ann_flat_contains(const struct ann_backend *backend, uint64_t label)
{
	return backend != NULL && ann_flat_find(backend, label) >= 0;
}

static uint64_t
ann_flat_reclaimable(const struct ann_backend *backend, uint64_t *size)
{
	*size = backend->count;
	return 0;
}

/** Publish the staged record while keeping its undo state. */
static enum ann_status
ann_flat_apply(struct ann_change *change,
	       struct ann_search_control *control)
{
	if (change == NULL || change->backend->pending != change ||
	    change->applied || change->rolled_back)
		return ANN_INVALID;
	if (control != NULL) {
		enum ann_status status = ann_search_control_poll(control);
		if (status != ANN_OK)
			return status;
	}
	struct ann_backend *backend = change->backend;
	if (change->kind == ANN_INSERT) {
		struct ann_flat_entry *entry =
			&backend->entries[backend->count++];
		entry->label = change->label;
		entry->vector = change->vector;
		entry->shared = change->shared;
		entry->live = true;
		change->vector = NULL;
		change->shared = NULL;
	} else {
		backend->entries[change->slot].live = false;
	}
	change->applied = true;
	return ANN_OK;
}

/** Undo publication without allocating or reclaiming other rows. */
static void
ann_flat_rollback(struct ann_change *change)
{
	if (change == NULL || change->rolled_back)
		return;
	change->rolled_back = true;
	assert(change->backend->pending == change);
	if (!change->applied)
		return;
	struct ann_backend *backend = change->backend;
	if (change->kind == ANN_INSERT) {
		struct ann_flat_entry *entry =
			&backend->entries[--backend->count];
		ann_flat_release_vector(backend, entry->vector, entry->shared);
	} else {
		backend->entries[change->slot].live = true;
	}
	change->applied = false;
}

/** Release an undo record after its decision. */
static void
ann_flat_finish(struct ann_change *change)
{
	if (change == NULL)
		return;
	struct ann_backend *backend = change->backend;
	assert(backend->pending == change);
	backend->pending = change->previous;
	ann_flat_release_vector(backend, change->vector, change->shared);
	ann_flat_free(backend, change, sizeof(*change));
}

/** Change result visibility without releasing coordinates. */
static enum ann_status
ann_flat_set_live(struct ann_backend *backend, uint64_t label, bool live)
{
	if (backend == NULL)
		return ANN_INVALID;
	int64_t slot = ann_flat_find(backend, label);
	if (slot < 0)
		return ANN_NOT_FOUND;
	backend->entries[slot].live = live;
	return ANN_OK;
}

/** Check whether a label is visible in ordinary searches. */
static bool
ann_flat_is_live(const struct ann_backend *backend, uint64_t label)
{
	if (backend == NULL)
		return false;
	int64_t slot = ann_flat_find(backend, label);
	return slot >= 0 && backend->entries[slot].live;
}

/** Release an invisible entry and close the gap in storage. */
static enum ann_status
ann_flat_reclaim(struct ann_backend *backend, uint64_t label)
{
	if (backend == NULL)
		return ANN_INVALID;
	if (backend->pending != NULL)
		return ANN_BUSY;
	int64_t slot = ann_flat_find(backend, label);
	if (slot < 0 || backend->entries[slot].live)
		return ANN_NOT_FOUND;
	ann_flat_release_vector(backend, backend->entries[slot].vector,
				backend->entries[slot].shared);
	backend->entries[slot] = backend->entries[--backend->count];
	return ANN_OK;
}

static bool
ann_candidate_before(const struct ann_candidate *left,
		     const struct ann_candidate *right)
{
	return left->distance < right->distance ||
	       (left->distance == right->distance &&
		left->label < right->label);
}

/** Keep the nearest eligible rows from a complete scan. */
static enum ann_status
ann_flat_search(struct ann_backend *backend, const float *query,
		const struct ann_search_opts *opts,
		struct ann_candidate *out, uint32_t *out_count)
{
	if (out_count != NULL)
		*out_count = 0;
	if (backend == NULL || query == NULL || opts == NULL ||
	    out_count == NULL || opts->control == NULL ||
	    opts->query_dimension != backend->config.dimension ||
	    (opts->candidate_limit != 0 && out == NULL) ||
	    (opts->filter != NULL && opts->filter->accept == NULL) ||
	    opts->algorithm != NULL)
		return ANN_INVALID;
	enum ann_status status = ann_search_control_poll(opts->control);
	if (status != ANN_OK)
		return status;
	double query_norm2 = 0;
	for (uint32_t i = 0; i < backend->config.dimension; ++i) {
		status = ann_search_control_step(opts->control);
		if (status != ANN_OK)
			return status;
		if (!isfinite(query[i]))
			return ANN_INVALID;
		if (backend->config.metric == ANN_COSINE) {
			double x = query[i];
			query_norm2 += x * x;
		}
	}
	if (backend->config.metric == ANN_COSINE && query_norm2 == 0)
		return ANN_INVALID;
	if (opts->candidate_limit == 0)
		return ann_search_control_poll(opts->control);
	uint32_t count = 0;
	for (uint32_t i = 0; i < backend->count; ++i) {
		status = ann_search_control_step(opts->control);
		if (status != ANN_OK)
			return status;
		const struct ann_flat_entry *entry = &backend->entries[i];
		if (opts->filter == NULL) {
			if (!entry->live)
				continue;
		} else if (!opts->filter->accept(entry->label, entry->shared,
						 opts->filter->ctx)) {
			continue;
		}
		double distance;
		bool packed = entry->shared != NULL &&
			      ann_vector_is_borrowed(entry->shared);
		status = ann_distance_raw(query, false, entry->vector, packed,
					  backend->config.dimension,
					  backend->config.metric, opts->control,
					  &distance);
		if (status != ANN_OK)
			return status;
		struct ann_candidate candidate = {
			entry->label, distance, entry->shared,
		};
		if (count == opts->candidate_limit &&
		    !ann_candidate_before(&candidate, &out[count - 1]))
			continue;
		uint32_t pos = count < opts->candidate_limit ?
			count++ : count - 1;
		while (pos > 0 &&
		       ann_candidate_before(&candidate, &out[pos - 1])) {
			out[pos] = out[pos - 1];
			--pos;
		}
		out[pos] = candidate;
	}
	status = ann_search_control_poll(opts->control);
	if (status != ANN_OK)
		return status;
	*out_count = count;
	return ANN_OK;
}

/** Report coordinate and entry allocations from this backend. */
static void
ann_flat_stat(const struct ann_backend *backend, struct ann_backend_stats *out)
{
	memset(out, 0, sizeof(*out));
	out->capacity = backend->capacity;
	out->resident_bytes = backend->resident_bytes;
	out->lookup_bytes = (uint64_t)backend->capacity *
			    sizeof(*backend->entries);
	for (uint32_t i = 0; i < backend->count; ++i) {
		if (backend->entries[i].vector != NULL &&
		    backend->entries[i].shared == NULL)
			out->vector_bytes +=
				(uint64_t)backend->config.dimension *
				sizeof(float);
		if (backend->entries[i].live)
			++out->live;
		else
			++out->retired;
	}
	out->graph_bytes = out->resident_bytes - out->lookup_bytes -
			   out->vector_bytes;
}

/** Flat operations registered for the internal reference backend. */
const struct ann_backend_ops ann_flat_ops = {
	.name = "flat",
	.create = ann_flat_create,
	.destroy = ann_flat_destroy,
	.reserve = ann_flat_reserve,
	.prepare = ann_flat_prepare,
	.prepare_shared = ann_flat_prepare_shared,
	.contains = ann_flat_contains,
	.reclaimable = ann_flat_reclaimable,
	.apply = ann_flat_apply,
	.rollback = ann_flat_rollback,
	.finish = ann_flat_finish,
	.set_live = ann_flat_set_live,
	.is_live = ann_flat_is_live,
	.reclaim = ann_flat_reclaim,
	.search = ann_flat_search,
	.stat = ann_flat_stat,
};
