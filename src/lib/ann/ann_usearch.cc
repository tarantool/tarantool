/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "ann_usearch.h"
#include "ann_numeric.h"
#include "ann_vector.h"
#include "ann_hnsw.h"

#include <usearch/basic/transactional_ram_index.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <utility>

namespace usearch = unum::usearch::basic;
using usearch::status_t;

/** Rebindable allocator for USearch's non-throwing allocation interface. */
template<class T>
struct AnnUsearchAllocator {
	using value_type = T;

	/** All graph pages retain the same allocator across snapshots. */
	struct ann_memory memory;
	/** Charged payload bytes, or NULL for request-local scratch. */
	uint64_t *retained;

	AnnUsearchAllocator(struct ann_memory memory, uint64_t *retained)
		noexcept : memory(memory), retained(retained) {}

	template<class U>
	AnnUsearchAllocator(const AnnUsearchAllocator<U> &other) noexcept
		: memory(other.memory), retained(other.retained) {}

	/** Charge retained graph allocations to the owning generation. */
	T *
	allocate(size_t count) noexcept
	{
		static_assert(alignof(T) <= alignof(max_align_t));
		if (count == 0 || count > SIZE_MAX / sizeof(T))
			return nullptr;
		size_t bytes = count * sizeof(T);
		void *ptr = memory.alloc(memory.ctx, bytes);
		if (ptr != nullptr && retained != nullptr)
			*retained += bytes;
		return static_cast<T *>(ptr);
	}

	/** Release a charge with the same allocator as the allocation. */
	void
	deallocate(T *ptr, size_t count) noexcept
	{
		size_t bytes = count * sizeof(T);
		if (retained != nullptr)
			*retained -= bytes;
		memory.free(memory.ctx, ptr, bytes);
	}

	template<class U>
	bool
	operator==(const AnnUsearchAllocator<U> &other) const noexcept
	{
		return memory.ctx == other.memory.ctx &&
		       memory.alloc == other.memory.alloc &&
		       memory.free == other.memory.free &&
		       retained == other.retained;
	}
};

/** USearch retains one owner per payload even when it copies graph links. */
struct AnnUsearchValue {
	/** Coordinates immutable for the lifetime of the graph entry. */
	struct ann_vector *vector = nullptr;

	AnnUsearchValue() noexcept = default;
	explicit AnnUsearchValue(struct ann_vector *vector) noexcept
		: vector(vector)
	{
		ann_vector_ref(vector);
	}

	AnnUsearchValue &
	operator=(AnnUsearchValue &&other) noexcept
	{
		std::swap(vector, other.vector);
		return *this;
	}

	~AnnUsearchValue() noexcept
	{
		ann_vector_unref(vector);
	}
};

using AnnGraphAllocator = AnnUsearchAllocator<std::byte>;
using AnnGraph = usearch::transactional_ram_index<AnnUsearchValue, uint64_t,
	uint32_t, double, AnnGraphAllocator>;

/** Current visibility of a slot; the immutable graph owns its coordinates. */
struct ann_usearch_entry {
	/** Storage-layer label. */
	uint64_t label;
	/** Current-view membership. */
	bool live;
	/** True after GC releases the label while retaining navigation. */
	bool reclaimed;
};

/** Open-addressed label lookup with backward-shift deletion. */
struct ann_usearch_lookup_entry {
	/** Storage-layer label. */
	uint64_t label;
	/** Physical graph slot corresponding to the label. */
	uint32_t slot;
	/** Empty or occupied. */
	uint8_t state;
};

/** One working generation or a pinned snapshot of its graph. */
struct ann_backend {
	/** Scalar and dimensional configuration. */
	struct ann_config config;
	/** Allocation callbacks for this header and its owned storage. */
	struct ann_memory memory;
	/** Graph connectivity and default construction and search widths. */
	struct ann_hnsw_config options;
	/** Owned bytes including pages pinned by snapshots and undo records. */
	uint64_t retained_bytes = sizeof(*this);
	/** Mutable graph, released at generation retirement. */
	std::optional<AnnGraph> graph;
	/** Immutable root borrowed by readers without taking references. */
	std::optional<AnnGraph::snapshot_t> view;
	/** Source header retained to keep allocator counters alive. */
	struct ann_backend *source = nullptr;
	/** Index and snapshot references managed by the writer. */
	uint32_t refs = 1;
	/** Slot-indexed live flags. */
	struct ann_usearch_entry *entries = nullptr;
	/** Label-to-slot lookup. */
	struct ann_usearch_lookup_entry *lookup = nullptr;
	/** GC-safe slots available for replacement. */
	uint32_t *reclaim_queue = nullptr;
	/** Number of reusable slots. */
	uint32_t reclaim_count = 0;
	/** Power-of-two lookup table size. */
	size_t lookup_capacity = 0;
	/** Number of slots covered by preallocated Tarantool metadata. */
	uint32_t capacity = 0;
	/** Number of live records. */
	uint32_t live_count = 0;
	/** Top of the change stack. */
	struct ann_change *pending = nullptr;

	ann_backend(const struct ann_config &cfg, struct ann_memory mem,
		    struct ann_hnsw_config opts) noexcept
		: config{cfg.dimension, cfg.metric, nullptr}, memory(mem),
		  options(opts) {}
};

/** Prepared version change carrying an allocation-free graph savepoint. */
struct ann_change {
	/** Owning generation. */
	struct ann_backend *backend;
	/** Older applied change. */
	struct ann_change *previous;
	/** Insertion or retirement. */
	enum ann_change_kind kind;
	/** Storage-layer version label. */
	uint64_t label;
	/** Coordinates retained until finish; NULL for retirement. */
	struct ann_vector *vector;
	/** Physical graph slot affected by this change. */
	uint32_t slot;
	/** Whether apply published the change. */
	bool applied;
	/** Whether rollback consumed the change. */
	bool rolled_back;
	/** Whether preparation reserved a reclaimed slot. */
	bool reused;
	/** Visibility metadata before replacing a reclaimed slot. */
	struct ann_usearch_entry old_entry;
	/** Graph before an applied insertion or replacement. */
	std::optional<AnnGraph::snapshot_t> savepoint;
};

/** Bind a private allocation to the generation accounting. */
static void *
ann_usearch_alloc(void *ctx, size_t size)
{
	auto *backend = static_cast<struct ann_backend *>(ctx);
	void *ptr = backend->memory.alloc(backend->memory.ctx, size);
	if (ptr != nullptr)
		backend->retained_bytes += size;
	return ptr;
}

static void
ann_usearch_free(void *ctx, void *ptr, size_t size)
{
	if (ptr == nullptr)
		return;
	auto *backend = static_cast<struct ann_backend *>(ctx);
	backend->retained_bytes -= size;
	backend->memory.free(backend->memory.ctx, ptr, size);
}

/** Mix nearby labels before masking the lookup slot. */
static uint64_t
ann_usearch_hash(uint64_t value)
{
	value ^= value >> 30;
	value *= UINT64_C(0xbf58476d1ce4e5b9);
	value ^= value >> 27;
	value *= UINT64_C(0x94d049bb133111eb);
	return value ^ (value >> 31);
}

/** Find a label before reaching the first empty bucket. */
static size_t
ann_usearch_lookup_find(const struct ann_usearch_lookup_entry *lookup,
			size_t capacity, uint64_t label)
{
	if (capacity == 0)
		return SIZE_MAX;
	size_t offset = ann_usearch_hash(label) & (capacity - 1);
	for (size_t i = 0; i < capacity; ++i) {
		size_t at = (offset + i) & (capacity - 1);
		if (lookup[at].state == 0)
			return SIZE_MAX;
		if (lookup[at].state == 1 && lookup[at].label == label)
			return at;
	}
	return SIZE_MAX;
}

/** Insert into a table whose occupied load is at most one half. */
static void
ann_usearch_lookup_insert(struct ann_usearch_lookup_entry *lookup,
			  size_t capacity, uint64_t label, uint32_t slot)
{
	assert(capacity != 0);
	size_t at = ann_usearch_hash(label) & (capacity - 1);
	while (lookup[at].state != 0)
		at = (at + 1) & (capacity - 1);
	lookup[at].label = label;
	lookup[at].slot = slot;
	lookup[at].state = 1;
}

/** Finds a retained label; graph slots remain stable until rebuilding. */
static int64_t
ann_usearch_find(const struct ann_backend *backend, uint64_t label)
{
	size_t at = ann_usearch_lookup_find(backend->lookup,
					    backend->lookup_capacity, label);
	if (at == SIZE_MAX)
		return -1;
	return backend->lookup[at].slot;
}

/** Close the deleted probe gap without retaining historical tombstones. */
static void
ann_usearch_lookup_erase(struct ann_backend *backend, uint64_t label)
{
	struct ann_usearch_lookup_entry *lookup = backend->lookup;
	size_t capacity = backend->lookup_capacity;
	size_t mask = capacity - 1;
	size_t hole = ann_usearch_lookup_find(lookup, capacity, label);
	assert(hole != SIZE_MAX);
	for (size_t at = (hole + 1) & mask; lookup[at].state != 0;
	     at = (at + 1) & mask) {
		size_t home = ann_usearch_hash(lookup[at].label) & mask;
		if (((at - home) & mask) >= ((at - hole) & mask)) {
			lookup[hole] = lookup[at];
			hole = at;
		}
	}
	lookup[hole].state = 0;
}

/** Query coordinates and byte order independent of their lifetime owner. */
struct ann_coordinate_ref {
	/** Native floats or packed little-endian bytes. */
	const void *data;
	/** True for packed bytes. */
	bool packed;
};

/** Shared cancellation reason for the metric and USearch callback. */
struct ann_usearch_control {
	/** Tarantool deadline, work counter and stop callback. */
	struct ann_search_control *control;
	/** First terminal condition observed by this operation. */
	enum ann_status status = ANN_OK;

	/** Preserve the first cancellation or work-limit status. */
	bool
	check(bool step) noexcept
	{
		if (control != nullptr && status == ANN_OK) {
			status = step ? ann_search_control_step(control) :
				ann_search_control_poll(control);
		}
		return status == ANN_OK;
	}

	/** Convert the operation status into a native cancellation decision. */
	static usearch::continuation_t
	poll(void *ctx) noexcept
	{
		auto *state = static_cast<struct ann_usearch_control *>(ctx);
		if (state->check(true))
			return usearch::continuation_t::continue_k;
		return usearch::continuation_t::cancel_k;
	}
};

/** Request-local metric with bounded staging independent of graph size. */
struct ann_usearch_metric {
	using is_transparent = void;

	/** Common distance and numeric representation. */
	const struct ann_config &config;
	/** Cumulative work and cancellation for the whole operation. */
	struct ann_usearch_control &state;
	/** Query of the current scoring round. */
	struct ann_coordinate_ref query = {};
	/** Exact distances waiting for USearch to drain the round. */
	std::array<double, 64> distances{};
	/** Number of staged distances. */
	size_t count = 0;
	/** Offset of the next distance delivered to the result predicate. */
	size_t next_distance = 0;

	void
	reset(struct ann_coordinate_ref value) noexcept
	{
		query = value;
		count = 0;
	}

	/** Reuse the stored coordinate representation in construction. */
	void
	reset(const AnnUsearchValue &value) noexcept
	{
		struct ann_coordinate_ref coords = {
			ann_vector_data(value.vector),
			ann_vector_is_borrowed(value.vector),
		};
		reset(coords);
	}

	/** Score one member of the native batch using the shared budget. */
	usearch::metric_staging_t
	push(const AnnUsearchValue &value) noexcept
	{
		if (count == distances.size() || state.status != ANN_OK)
			return usearch::metric_staging_t::refused_k;
		state.status = ann_distance_raw(
			query.data, query.packed,
			ann_vector_data(value.vector),
			ann_vector_is_borrowed(value.vector),
			config.dimension, config.metric, state.control,
			&distances[count]);
		if (state.status != ANN_OK)
			return usearch::metric_staging_t::refused_k;
		++count;
		return usearch::metric_staging_t::staged_k;
	}

	/** Expose the batch while keeping distances available to admission. */
	void
	drain(std::span<double> out, double) noexcept
	{
		assert(out.size() == count);
		memcpy(out.data(), distances.data(), count * sizeof(double));
		next_distance = 0;
		count = 0;
	}

	/** Snapshot search admits every row of the drained batch in order. */
	double
	take_distance() noexcept
	{
		assert(next_distance < distances.size());
		return distances[next_distance++];
	}
};

/** Prefer a Tarantool terminal status to native cancellation. */
static enum ann_status
ann_usearch_status(status_t status, const struct ann_usearch_control &state)
{
	if (state.status != ANN_OK)
		return state.status;
	switch (status) {
	case status_t::success_k:
		return ANN_OK;
	case status_t::bad_alloc_k:
		return ANN_OUT_OF_MEMORY;
	case status_t::cancelled_k:
		return ANN_STOPPED;
	default:
		return ANN_INVALID;
	}
}

static enum ann_status
ann_usearch_create(const struct ann_config *config,
		   const struct ann_memory *memory,
		   struct ann_backend **out)
{
	if (out == nullptr)
		return ANN_INVALID;
	*out = nullptr;
	if (config == nullptr || memory == nullptr ||
	    memory->alloc == nullptr || memory->free == nullptr ||
	    config->dimension == 0 ||
	    ((size_t)config->dimension * sizeof(float)) / sizeof(float) !=
		config->dimension ||
	    (unsigned)config->metric > ANN_IP)
		return ANN_INVALID;
	struct ann_hnsw_config options = {16, 200, 64};
	if (config->algorithm != nullptr)
		options = *static_cast<const struct ann_hnsw_config *>(
			config->algorithm);
	if (options.connectivity < 2 || options.connectivity > UINT32_MAX / 2 ||
	    options.expansion_add == 0 || options.expansion_search == 0)
		return ANN_INVALID;
	void *storage = memory->alloc(memory->ctx, sizeof(struct ann_backend));
	if (storage == nullptr)
		return ANN_OUT_OF_MEMORY;
	auto *backend = new(storage) ann_backend(*config, *memory, options);
	backend->graph.emplace(
		AnnGraphAllocator{*memory, &backend->retained_bytes},
		AnnGraph::config_t{options.connectivity, AnnGraph::missing_k});
	*out = backend;
	return ANN_OK;
}

/** Keep the allocator's counters alive until the last graph root is gone. */
static void
ann_usearch_unref(struct ann_backend *backend)
{
	if (--backend->refs != 0)
		return;
	struct ann_memory memory = backend->memory;
	std::destroy_at(backend);
	memory.free(memory.ctx, backend, sizeof(*backend));
}

/** Release the completed change and its savepoint. */
static void
ann_usearch_finish(struct ann_change *change);

static void
ann_usearch_destroy(struct ann_backend *backend)
{
	if (backend == nullptr)
		return;
	if (backend->view) {
		auto *source = backend->source;
		ann_usearch_unref(backend);
		ann_usearch_unref(source);
		return;
	}
	while (backend->pending != nullptr)
		ann_usearch_finish(backend->pending);
	ann_usearch_free(backend, backend->entries,
			 (size_t)backend->capacity * sizeof(*backend->entries));
	ann_usearch_free(backend, backend->lookup,
			 backend->lookup_capacity * sizeof(*backend->lookup));
	ann_usearch_free(backend, backend->reclaim_queue,
			 (size_t)backend->capacity *
			 sizeof(*backend->reclaim_queue));
	backend->graph.reset();
	ann_usearch_unref(backend);
}

/** Freeze graph topology and coordinates with a single root reference. */
static enum ann_status
ann_usearch_snapshot(struct ann_backend *backend,
		     const struct ann_memory *memory,
		     struct ann_backend **out)
{
	if (out == nullptr)
		return ANN_INVALID;
	*out = nullptr;
	if (backend == nullptr || !backend->graph || memory == nullptr ||
	    memory->alloc == nullptr || memory->free == nullptr)
		return ANN_INVALID;
	void *storage = memory->alloc(memory->ctx, sizeof(struct ann_backend));
	if (storage == nullptr)
		return ANN_OUT_OF_MEMORY;
	auto *view = new(storage)
		ann_backend(backend->config, *memory, backend->options);
	view->view.emplace(backend->graph->snapshot());
	assert(backend->refs < UINT32_MAX);
	++backend->refs;
	view->source = backend;
	*out = view;
	return ANN_OK;
}

/**
 * The graph grows as rows are published. Reserve only Tarantool's slot
 * metadata. This does not raise the physical graph limit.
 */
static enum ann_status
ann_usearch_reserve(struct ann_backend *backend, uint32_t capacity)
{
	if (backend == nullptr || !backend->graph)
		return ANN_INVALID;
	if (backend->pending != nullptr && !backend->pending->applied)
		return ANN_BUSY;
	if (capacity <= backend->capacity)
		return ANN_OK;
	if ((size_t)capacity > SIZE_MAX / sizeof(*backend->entries))
		return ANN_INVALID;
	size_t size = (size_t)capacity * sizeof(*backend->entries);
	struct ann_usearch_entry *entries =
		(struct ann_usearch_entry *)ann_usearch_alloc(backend, size);
	if (entries == nullptr)
		return ANN_OUT_OF_MEMORY;
	memset(entries, 0, size);
	if (backend->graph->size() != 0)
		memcpy(entries, backend->entries, backend->graph->size() *
		       sizeof(*entries));
	size_t lookup_capacity = 8;
	while (lookup_capacity / 2 < capacity) {
		if (lookup_capacity > SIZE_MAX / 2) {
			ann_usearch_free(backend, entries, size);
			return ANN_INVALID;
		}
		lookup_capacity *= 2;
	}
	if (lookup_capacity > SIZE_MAX / sizeof(*backend->lookup)) {
		ann_usearch_free(backend, entries, size);
		return ANN_INVALID;
	}
	size_t lookup_size = lookup_capacity * sizeof(*backend->lookup);
	struct ann_usearch_lookup_entry *lookup =
		(struct ann_usearch_lookup_entry *)ann_usearch_alloc(
			backend, lookup_size);
	if (lookup == nullptr) {
		ann_usearch_free(backend, entries, size);
		return ANN_OUT_OF_MEMORY;
	}
	memset(lookup, 0, lookup_size);
	size_t queue_size = (size_t)capacity * sizeof(*backend->reclaim_queue);
	uint32_t *queue = (uint32_t *)ann_usearch_alloc(backend, queue_size);
	if (queue == nullptr) {
		ann_usearch_free(backend, lookup, lookup_size);
		ann_usearch_free(backend, entries, size);
		return ANN_OUT_OF_MEMORY;
	}
	if (backend->reclaim_count != 0)
		memcpy(queue, backend->reclaim_queue, backend->reclaim_count *
		       sizeof(*queue));
	for (uint32_t i = 0; i < backend->graph->size(); ++i) {
		if (!entries[i].reclaimed)
			ann_usearch_lookup_insert(lookup, lookup_capacity,
						  entries[i].label, i);
	}
	ann_usearch_free(backend, backend->entries,
			 (size_t)backend->capacity * sizeof(*entries));
	ann_usearch_free(backend, backend->lookup,
			 backend->lookup_capacity * sizeof(*lookup));
	ann_usearch_free(backend, backend->reclaim_queue,
			 (size_t)backend->capacity * sizeof(*queue));
	backend->entries = entries;
	backend->lookup = lookup;
	backend->reclaim_queue = queue;
	backend->lookup_capacity = lookup_capacity;
	backend->capacity = capacity;
	return ANN_OK;
}

/** Reserve publication metadata and retain the input vector. */
static enum ann_status
ann_usearch_prepare_impl(struct ann_backend *backend,
			 enum ann_change_kind kind, uint64_t label,
			 const void *vector, uint32_t dimension,
			 struct ann_change **out, struct ann_vector *shared)
{
	if (out == nullptr)
		return ANN_INVALID;
	*out = nullptr;
	if (backend == nullptr || !backend->graph ||
	    (kind != ANN_INSERT && kind != ANN_RETIRE))
		return ANN_INVALID;
	if (backend->pending != nullptr && !backend->pending->applied)
		return ANN_BUSY;
	int64_t slot = ann_usearch_find(backend, label);
	if (kind == ANN_INSERT) {
		if (slot >= 0)
			return ANN_EXISTS;
		enum ann_status valid = shared != nullptr &&
			ann_vector_is_borrowed(shared) ?
			ann_vector_validate_le(vector, dimension,
					       backend->config.metric) :
			ann_vector_validate_f32(
				(const float *)vector, dimension,
				backend->config.metric);
		if (dimension != backend->config.dimension || valid != ANN_OK)
			return ANN_INVALID;
		if (backend->reclaim_count == 0 &&
		    backend->graph->size() == AnnGraph::missing_k)
			return ANN_OUT_OF_MEMORY;
		if (backend->reclaim_count == 0 &&
		    backend->graph->size() == backend->capacity) {
			uint32_t next = backend->capacity == 0 ? 4 :
				backend->capacity > UINT32_MAX / 2 ?
				UINT32_MAX : backend->capacity * 2;
			enum ann_status status =
				ann_usearch_reserve(backend, next);
			if (status != ANN_OK)
				return status;
		}
	} else if (vector != nullptr || dimension != 0) {
		return ANN_INVALID;
	} else if (slot < 0 || !backend->entries[slot].live) {
		return ANN_NOT_FOUND;
	}
	void *storage = ann_usearch_alloc(backend, sizeof(struct ann_change));
	if (storage == nullptr)
		return ANN_OUT_OF_MEMORY;
	struct ann_change *change = new(storage) ann_change{};
	change->backend = backend;
	change->previous = backend->pending;
	change->kind = kind;
	change->label = label;
	change->reused = kind == ANN_INSERT && backend->reclaim_count != 0;
	change->slot = kind == ANN_RETIRE ? slot : change->reused ?
		backend->reclaim_queue[backend->reclaim_count - 1] :
		backend->graph->size();
	if (kind == ANN_INSERT && shared != nullptr) {
		ann_vector_ref(shared);
		change->vector = shared;
	} else if (kind == ANN_INSERT) {
		struct ann_memory memory = {backend, ann_usearch_alloc,
					    ann_usearch_free};
		change->vector = ann_vector_new(
			&memory, static_cast<const float *>(vector), dimension);
		if (change->vector == nullptr) {
			std::destroy_at(change);
			ann_usearch_free(backend, change, sizeof(*change));
			return ANN_OUT_OF_MEMORY;
		}
	}
	if (change->reused) {
		change->old_entry = backend->entries[change->slot];
		assert(change->old_entry.reclaimed && !change->old_entry.live);
		--backend->reclaim_count;
	}
	backend->pending = change;
	*out = change;
	return ANN_OK;
}

static enum ann_status
ann_usearch_prepare(struct ann_backend *backend, enum ann_change_kind kind,
		    uint64_t label, const float *vector, uint32_t dimension,
		    struct ann_change **out)
{
	return ann_usearch_prepare_impl(backend, kind, label, vector, dimension,
					out, nullptr);
}

/** Stage a write with an existing coordinate owner. */
static enum ann_status
ann_usearch_prepare_shared(struct ann_backend *backend, uint64_t label,
			   struct ann_vector *vector,
			   struct ann_change **out)
{
	if (vector == nullptr) {
		if (out != nullptr)
			*out = nullptr;
		return ANN_INVALID;
	}
	const void *data = ann_vector_data(vector);
	uint32_t dimension = ann_vector_dimension(vector);
	return ann_usearch_prepare_impl(backend, ANN_INSERT, label, data,
					dimension, out, vector);
}

static bool
ann_usearch_contains(const struct ann_backend *backend, uint64_t label)
{
	return backend != nullptr && backend->graph &&
	       ann_usearch_find(backend, label) >= 0;
}

static uint64_t
ann_usearch_reclaimable(const struct ann_backend *backend, uint64_t *size)
{
	*size = backend->view ? backend->view->size() : backend->graph->size();
	return backend->reclaim_count;
}

/** Publish the native write before updating label visibility. */
static enum ann_status
ann_usearch_apply(struct ann_change *change,
		  struct ann_search_control *control)
{
	if (change == nullptr || change->backend->pending != change ||
	    change->applied || change->rolled_back)
		return ANN_INVALID;
	struct ann_usearch_control state = {control};
	if (!state.check(false))
		return state.status;
	struct ann_backend *backend = change->backend;
	struct ann_usearch_entry *entry = &backend->entries[change->slot];
	if (change->kind == ANN_RETIRE) {
		entry->live = false;
		--backend->live_count;
		change->applied = true;
		return ANN_OK;
	}

	auto savepoint = backend->graph->snapshot();
	AnnUsearchValue value(change->vector);
	struct ann_usearch_metric metric = {backend->config, state};
	AnnGraph::traversal_context_t scratch(
		0, AnnGraphAllocator{backend->memory, nullptr});
	/*
	 * Construction cannot find more neighbors than existing slots. Native
	 * USearch still reserves scratch using the requested width on an empty
	 * graph, so bound it before drawing from a rebuild's quota reserve.
	 */
	uint32_t expansion = std::min(backend->options.expansion_add,
				      backend->graph->size());
	AnnGraph::operation_t options{
		expansion,
		{ann_usearch_control::poll, &state},
	};
	const status_t result = change->reused ?
		backend->graph->replace(change->slot, change->label,
					std::move(value), metric, scratch,
					options) :
		backend->graph->insert(change->label, std::move(value),
				       metric, scratch, options).status;
	if (result != status_t::success_k)
		return ann_usearch_status(result, state);
	if (!state.check(false)) {
		[[maybe_unused]] auto restored =
			backend->graph->restore(savepoint);
		assert(restored == status_t::success_k);
		return state.status;
	}

	/*
	 * The graph write has finished. Publish visibility and label lookup
	 * together without allocating or yielding.
	 */
	change->savepoint.emplace(std::move(savepoint));
	*entry = {change->label, true, false};
	ann_usearch_lookup_insert(backend->lookup, backend->lookup_capacity,
				  change->label, change->slot);
	++backend->live_count;
	change->applied = true;
	return ANN_OK;
}

static void
ann_usearch_return_slot(struct ann_change *change)
{
	struct ann_backend *backend = change->backend;
	assert(backend->reclaim_count < backend->capacity);
	backend->reclaim_queue[backend->reclaim_count++] = change->slot;
}

/** Restore the saved graph and metadata without allocations. */
static void
ann_usearch_rollback(struct ann_change *change)
{
	if (change == nullptr || change->rolled_back)
		return;
	struct ann_backend *backend = change->backend;
	assert(backend->pending == change);
	change->rolled_back = true;
	if (change->reused)
		ann_usearch_return_slot(change);
	if (!change->applied)
		return;
	if (change->kind == ANN_RETIRE) {
		backend->entries[change->slot].live = true;
		++backend->live_count;
	} else {
		ann_usearch_lookup_erase(backend, change->label);
		[[maybe_unused]] auto restored =
			backend->graph->restore(*change->savepoint);
		assert(restored == status_t::success_k);
		backend->entries[change->slot] = change->old_entry;
		--backend->live_count;
	}
	change->applied = false;
}

static void
ann_usearch_finish(struct ann_change *change)
{
	if (change == nullptr)
		return;
	struct ann_backend *backend = change->backend;
	assert(backend->pending == change);
	backend->pending = change->previous;
	if (change->reused && !change->applied && !change->rolled_back)
		ann_usearch_return_slot(change);
	ann_vector_unref(change->vector);
	std::destroy_at(change);
	ann_usearch_free(backend, change, sizeof(*change));
}

/** Change visibility without removing the routing node. */
static enum ann_status
ann_usearch_set_live(struct ann_backend *backend, uint64_t label, bool live)
{
	if (backend == nullptr || !backend->graph)
		return ANN_INVALID;
	int64_t slot = ann_usearch_find(backend, label);
	if (slot < 0)
		return ANN_NOT_FOUND;
	struct ann_usearch_entry *entry = &backend->entries[slot];
	backend->live_count -= entry->live;
	entry->live = live;
	backend->live_count += live;
	return ANN_OK;
}

/** Check membership in the working graph results. */
static bool
ann_usearch_is_live(const struct ann_backend *backend, uint64_t label)
{
	if (backend == nullptr || !backend->graph)
		return false;
	int64_t slot = ann_usearch_find(backend, label);
	return slot >= 0 && backend->entries[slot].live;
}

/** Release a label and queue its slot for replacement. */
static enum ann_status
ann_usearch_reclaim(struct ann_backend *backend, uint64_t label)
{
	if (backend == nullptr || !backend->graph)
		return ANN_INVALID;
	if (backend->pending != nullptr)
		return ANN_BUSY;
	int64_t slot = ann_usearch_find(backend, label);
	if (slot < 0 || backend->entries[slot].live)
		return ANN_NOT_FOUND;
	/* Navigation remains valid until a successful slot replacement. */
	ann_usearch_lookup_erase(backend, label);
	backend->entries[slot].reclaimed = true;
	assert(backend->reclaim_count < backend->capacity);
	backend->reclaim_queue[backend->reclaim_count++] = slot;
	return ANN_OK;
}

/** Retain the best distinct candidates among all rows scored by the walk. */
static void
ann_usearch_offer(struct ann_candidate *out, uint32_t &count, uint32_t limit,
		  const struct ann_candidate &candidate,
		  const struct ann_hnsw_search_opts &order)
{
	auto compare = [&order](const struct ann_candidate &a,
				const struct ann_candidate &b) {
		if (a.distance != b.distance)
			return a.distance < b.distance ? -1 : 1;
		return order.compare_equal(&a, &b, order.compare_ctx);
	};
	auto before = [&compare](const struct ann_candidate &a,
				 const struct ann_candidate &b) {
		return compare(a, b) < 0;
	};
	auto *pos = std::lower_bound(out, out + count, candidate, before);
	if (pos == out + limit ||
	    (pos != out + count && compare(*pos, candidate) == 0))
		return;
	if (count < limit)
		++count;
	std::move_backward(pos, out + count - 1, out + count);
	*pos = candidate;
}

/** Keep only one physical node per result in USearch's stopping width. */
static bool
ann_usearch_admit(const AnnGraph::snapshot_t &graph,
		  const AnnGraph::traversal_context_t &scratch,
		  uint32_t width, const struct ann_candidate &candidate,
		  const struct ann_hnsw_search_opts &order,
		  struct ann_usearch_control &state)
{
	const auto &admitted = scratch.admitted;
	/* A full native buffer won't retain a row tied with its worst one. */
	if (admitted.empty() || (admitted.size() == width &&
				 candidate.distance >= admitted.top().distance))
		return true;
	auto closer = [](const auto &item, double distance) {
		return item.distance < distance;
	};
	auto *match = std::lower_bound(admitted.begin(), admitted.end(),
				       candidate.distance, closer);
	for (; match != admitted.end() && match->distance == candidate.distance;
	     ++match) {
		if (!state.check(true))
			return false;
		const auto row = graph.at(match->node_id);
		assert(row.status == status_t::success_k);
		struct ann_candidate existing = {
			*row.value.key, match->distance,
			row.value.value->vector,
		};
		if (order.compare_equal(&existing, &candidate,
					order.compare_ctx) == 0)
			return false;
	}
	return true;
}

/** Walk a pinned root with request-local scratch and metric. */
static enum ann_status
ann_usearch_search(struct ann_backend *backend, const float *query,
		   const struct ann_search_opts *opts,
		   struct ann_candidate *out, uint32_t *out_count)
{
	if (out_count != nullptr)
		*out_count = 0;
	if (backend == nullptr || query == nullptr || opts == nullptr ||
	    opts->control == nullptr || out_count == nullptr ||
	    (opts->candidate_limit != 0 && out == nullptr) ||
	    opts->query_dimension != backend->config.dimension ||
	    (opts->filter != nullptr && opts->filter->accept == nullptr) ||
	    (backend->view && opts->filter == nullptr))
		return ANN_INVALID;
	enum ann_status status = ann_vector_validate_f32(
		query, backend->config.dimension, backend->config.metric);
	if (status != ANN_OK)
		return status;
	struct ann_usearch_control state = {opts->control};
	if (!state.check(false) || opts->candidate_limit == 0)
		return state.status;
	uint32_t expansion = backend->options.expansion_search;
	const auto *algorithm =
		static_cast<const struct ann_hnsw_search_opts *>(
			opts->algorithm);
	if (algorithm != nullptr) {
		expansion = algorithm->expansion;
		if (expansion == 0)
			return ANN_INVALID;
	}

	/*
	 * Only a writer takes a new root reference. Frozen readers use their
	 * pinned root directly, with an independent metric and scratch buffers.
	 */
	std::optional<AnnGraph::snapshot_t> current;
	if (!backend->view)
		current.emplace(backend->graph->snapshot());
	const auto &graph = backend->view ? *backend->view : *current;
	struct ann_memory memory = opts->memory == nullptr ? backend->memory :
		*opts->memory;
	if (memory.alloc == nullptr || memory.free == nullptr)
		return ANN_INVALID;
	AnnGraph::traversal_context_t scratch(
		0, AnnGraphAllocator{memory, nullptr});
	struct ann_usearch_metric metric = {backend->config, state};
	AnnGraph::operation_t options{
		expansion,
		{ann_usearch_control::poll, &state},
	};
	bool ordered = algorithm != nullptr &&
		       algorithm->compare_equal != nullptr;
	uint32_t width = std::max(expansion, opts->candidate_limit);
	uint32_t count = 0;
	auto predicate = [&](uint32_t slot, uint64_t label,
			     const AnnUsearchValue &value) noexcept {
		const double distance = ordered ? metric.take_distance() : 0;
		if (!backend->view && backend->entries[slot].reclaimed)
			return false;
		bool accepted = opts->filter == nullptr ?
			backend->entries[slot].live :
			opts->filter->accept(label, value.vector,
					     opts->filter->ctx);
		if (!accepted || !ordered)
			return accepted;
		struct ann_candidate candidate = {
			label, distance, value.vector,
		};
		if (!state.check(true) ||
		    !ann_usearch_admit(graph, scratch, width, candidate,
				       *algorithm, state))
			return false;
		ann_usearch_offer(out, count, opts->candidate_limit,
				  candidate, *algorithm);
		return true;
	};
	const status_t result = graph.search(
		ann_coordinate_ref{query, false}, opts->candidate_limit, metric,
		scratch, options, predicate);
	if (result != status_t::success_k)
		return ann_usearch_status(result, state);
	if (!ordered) {
		for (const auto &match : scratch.results) {
			if (!state.check(true))
				return state.status;
			const auto row = graph.at(match.node_id);
			assert(row.status == status_t::success_k);
			out[count++] = {*row.value.key, match.distance,
					row.value.value->vector};
		}
	}
	if (!state.check(false))
		return state.status;
	*out_count = count;
	return ANN_OK;
}

/** Report retained bytes and physical slot accounting. */
static void
ann_usearch_stat(const struct ann_backend *backend,
		 struct ann_backend_stats *out)
{
	memset(out, 0, sizeof(*out));
	out->resident_bytes = backend->retained_bytes;
	if (backend->graph) {
		out->capacity = backend->capacity;
		out->live = backend->live_count;
		out->retired = backend->graph->size() - backend->live_count;
		out->reclaimable = backend->reclaim_count;
		out->lookup_bytes = (uint64_t)backend->capacity *
			(sizeof(*backend->entries) +
			 sizeof(*backend->reclaim_queue)) +
			(uint64_t)backend->lookup_capacity *
			 sizeof(*backend->lookup);
	}
	out->graph_bytes = out->resident_bytes - out->lookup_bytes;
}

extern "C" const struct ann_backend_ops ann_usearch_ops = {
	"usearch", ann_usearch_create, ann_usearch_destroy,
	ann_usearch_reserve, ann_usearch_prepare, ann_usearch_prepare_shared,
	ann_usearch_contains, ann_usearch_reclaimable, ann_usearch_apply,
	ann_usearch_rollback, ann_usearch_finish, ann_usearch_set_live,
	ann_usearch_is_live, ann_usearch_reclaim, ann_usearch_search,
	ann_usearch_stat, ann_usearch_snapshot,
};
