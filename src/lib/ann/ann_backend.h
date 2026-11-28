/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/** Status returned across the backend boundary. */
enum ann_status {
	ANN_OK,
	ANN_INVALID,
	ANN_OUT_OF_MEMORY,
	ANN_NOT_FOUND,
	ANN_EXISTS,
	ANN_TIMEOUT,
	ANN_WORK_LIMIT,
	ANN_STOPPED,
	ANN_BUSY,
};

/** Minimized distance function. */
enum ann_metric {
	ANN_L2,
	ANN_COSINE,
	ANN_IP,
};

/** Backend configuration fixed for its lifetime. */
struct ann_config {
	/** Number of float32 components in each vector. */
	uint32_t dimension;
	/** Distance function shared by all entries and queries. */
	enum ann_metric metric;
	/** Optional algorithm-specific immutable configuration. */
	const void *algorithm;
};

/** Allocator whose context owns all retained backend memory. */
struct ann_memory {
	/** Allocator owner supplied by the caller. */
	void *ctx;
	/** Allocate size bytes with max_align_t alignment, or return NULL. */
	void *(*alloc)(void *ctx, size_t size);
	/** Release a block previously charged for size bytes. */
	void (*free)(void *ctx, void *ptr, size_t size);
};

/** Opaque backend and a prepared, single-writer change. */
struct ann_backend;
struct ann_change;
struct ann_vector;

/** One candidate copied into caller-owned storage. */
struct ann_candidate {
	/** Label assigned by the storage integration layer. */
	uint64_t label;
	/** Finite distance computed from canonical float32 values. */
	double distance;
	/** Coordinates retained by the backend until its next mutation. */
	const struct ann_vector *vector;
};

/** Result predicate; rejected records may still aid ANN navigation. */
struct ann_filter {
	/** Decide whether a label may appear in the result. */
	bool (*accept)(uint64_t label, const struct ann_vector *vector,
		       void *ctx);
	/** Visibility and declarative-filter state. */
	void *ctx;
};

/** Shared control state across all rounds of one search. */
struct ann_search_control {
	/** Absolute monotonic deadline, or zero when absent. */
	uint64_t deadline_ns;
	/** Maximum number of component or candidate visits; positive. */
	uint64_t work_limit;
	/** Visits consumed by this operation so far. */
	uint64_t work_done;
	/** Current monotonic time; required when deadline_ns is set. */
	uint64_t (*now_ns)(void *ctx);
	/** Optional caller stop check that never yields. */
	bool (*should_stop)(void *ctx);
	/** Caller state shared by both callbacks. */
	void *ctx;
};

/** Traversal counters for one request, including work before failure. */
struct ann_search_stats {
	/** Candidates whose distances were computed. */
	uint64_t computed_distances;
	/** Graph nodes whose adjacency lists were expanded. */
	uint64_t expanded_nodes;
	/** Traversals restarted after the frontier ran out of candidates. */
	uint64_t restarts;
};

/** Backend-independent search parameters. */
struct ann_search_opts {
	/** Capacity of out and maximum candidate count. */
	uint32_t candidate_limit;
	/** Component count of the supplied query. */
	uint32_t query_dimension;
	/** Optional predicate; NULL selects only live records. */
	const struct ann_filter *filter;
	/** Required cumulative deadline and work control. */
	struct ann_search_control *control;
	/** Algorithm-specific options; NULL for Flat. */
	const void *algorithm;
	/** Optional request-local allocator for frozen graph search buffers. */
	const struct ann_memory *memory;
	/** Optional request-local output, reset at the start of search. */
	struct ann_search_stats *stats;
};

/** Type of a prepared logical mutation. */
enum ann_change_kind {
	ANN_INSERT,
	ANN_RETIRE,
};

/** Common retained-memory and version counters. */
struct ann_backend_stats {
	/** Searchable records for the current view. */
	uint64_t live;
	/** Retired records still available to older views. */
	uint64_t retired;
	/** GC-safe slots available for reuse or generation rebuild. */
	uint64_t reclaimable;
	/** Allocated entry slots, including unused capacity. */
	uint64_t capacity;
	/** Charged bytes currently held by the backend. */
	uint64_t resident_bytes;
	/** Canonical vector allocations within resident_bytes. */
	uint64_t vector_bytes;
	/** Entry, label lookup, and reclaim arrays within resident_bytes. */
	uint64_t lookup_bytes;
	/** Graph and backend metadata within resident_bytes. */
	uint64_t graph_bytes;
	/** Number of graph layers, or zero for a flat scan. */
	uint32_t graph_layers;
};

/**
 * Single-writer backend contract. Labels identify immutable coordinates.
 * Prepare takes a private copy or a reference and leaves the label invisible.
 * Apply publishes without yielding. Finish commits the change, or releases
 * it after rollback. Resolve nested changes in reverse application order.
 * Failed apply leaves the previous contents visible. Rollback and finish
 * require no allocation. Capacity reserved during preparation may persist.
 *
 * An external predicate can admit retained versions regardless of their
 * current live flag. Reclaim only after storage-level version GC permits
 * removing the label. The backend may retain its coordinates for navigation
 * until it reuses or drops the physical slot.
 *
 * Each allocation is freed through its original allocator, even when a
 * snapshot outlives its source. Shared vectors are charged to their owner.
 * The caller keeps each allocator context alive until its last allocation
 * is released and serializes snapshot creation and destruction with writes.
 * A snapshot may be searched on other threads with private control and
 * scratch while it remains pinned. Its result coordinates live until it
 * is destroyed. Stop callbacks and filters may not yield. An error
 * invalidates all results, including on deadline or work exhaustion.
 */
struct ann_backend_ops {
	/** Internal backend name; Flat is a test-only implementation. */
	const char *name;
	/** Create a backend with a copied allocator context. */
	enum ann_status (*create)(const struct ann_config *config,
				  const struct ann_memory *memory,
				  struct ann_backend **out);
	/** Destroy all backend-owned storage. */
	void (*destroy)(struct ann_backend *backend);
	/** Reserve entry capacity without changing logical contents. */
	enum ann_status (*reserve)(struct ann_backend *backend,
				   uint32_t capacity);
	/** Prepare a change; prior undo records may remain applied. */
	enum ann_status (*prepare)(struct ann_backend *backend,
				   enum ann_change_kind kind, uint64_t label,
				   const float *vector, uint32_t dimension,
				   struct ann_change **out);
	/** Prepare an insertion retaining shared coordinates until release. */
	enum ann_status (*prepare_shared)(struct ann_backend *backend,
					  uint64_t label,
					  struct ann_vector *vector,
					  struct ann_change **out);
	/** Test membership, including labels retained only for rollback. */
	bool (*contains)(const struct ann_backend *backend, uint64_t label);
	/** Count reusable slots and write physical size without allocation. */
	uint64_t (*reclaimable)(const struct ann_backend *backend,
				uint64_t *size);
	/** Publish; a failure must remain safely rollbackable. */
	enum ann_status (*apply)(struct ann_change *change,
				 struct ann_search_control *control);
	/** Restore the previous state without allocation. */
	void (*rollback)(struct ann_change *change);
	/** Commit or release the top undo record without allocation. */
	void (*finish)(struct ann_change *change);
	/** Change visibility of a retained label without allocation. */
	enum ann_status (*set_live)(struct ann_backend *backend,
				    uint64_t label, bool live);
	/** Query current logical visibility without allocating. */
	bool (*is_live)(const struct ann_backend *backend, uint64_t label);
	/** Release a retired label after external version GC permits it. */
	enum ann_status (*reclaim)(struct ann_backend *backend,
				   uint64_t label);
	/** Copy up to candidate_limit entries into caller-owned out. */
	enum ann_status (*search)(struct ann_backend *backend,
				  const float *query,
				  const struct ann_search_opts *opts,
				  struct ann_candidate *out,
				  uint32_t *out_count);
	/** Read current counters without allocating. */
	void (*stat)(const struct ann_backend *backend,
		     struct ann_backend_stats *out);
	/**
	 * Freeze topology and coordinates; search requires a visibility filter.
	 * Searches use independent buffers. Destroy the result with this vtab.
	 * Snapshot creation must not copy coordinates or walk graph nodes.
	 * NULL when the test-only backend doesn't implement snapshots.
	 */
	enum ann_status (*snapshot)(struct ann_backend *backend,
				    const struct ann_memory *memory,
				    struct ann_backend **out);
};

/** Find the compiled implementation of an algorithm; Flat is test-only. */
const struct ann_backend_ops *
ann_backend_find(const char *name);

/** Check one unit of work against deadline, work, and stop callback. */
enum ann_status
ann_search_control_step(struct ann_search_control *control);

/** Check control without consuming work, including for an empty search. */
enum ann_status
ann_search_control_poll(struct ann_search_control *control);

#if defined(__cplusplus)
} /* extern "C" */
#endif
