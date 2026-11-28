#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "iterator_type.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct index;
struct index_def;
struct memtx_engine;
struct port;
struct rlist;
struct tuple;
struct iterator;
struct index_read_view;
struct index_read_view_iterator;
struct space_read_view;
struct ann_search_opts;
struct ann_candidate;
struct memtx_vector_snapshot;

/** Storage-neutral arguments of a local VECTOR search. */
struct memtx_vector_search_opts {
	/** Start of the MsgPack key. */
	const char *key;
	/** End of the MsgPack key. */
	const char *key_end;
	/** Optional MsgPack array of unsigned filter values. */
	const char *filter;
	/** End of the filter array, when supplied. */
	const char *filter_end;
	/** Maximum number of tuples to return. */
	uint32_t limit;
	/** Offset from the protocol, which must be zero. */
	uint32_t offset;
	/** Search width override, or zero to use the index default. */
	uint32_t ef_search;
	/** One-based filter field, or zero without a filter. */
	uint32_t filter_fieldno;
	/** Requested time budget in seconds. */
	double timeout;
};

/** Server resource policy, independent of any particular embedding model. */
enum { MEMTX_VECTOR_MAX_DIMENSION = 4096 };

/** Create one empty secondary index for an embedding field. */
struct index *
memtx_vector_index_new(struct memtx_engine *memtx, struct index_def *def);

/** Materialize visible tuples and optionally their distances. */
int
memtx_vector_index_search(struct index *index,
			  const struct memtx_vector_search_opts *opts,
			  struct port *port, double *distances);

/** Compare primary keys retained by the coordinate owners of two candidates. */
int
memtx_vector_compare_candidates(const struct ann_candidate *a,
				const struct ann_candidate *b, void *key_def);

/** Provide the primary-key predecessor without changing MVCC index links. */
void
memtx_vector_index_set_replace_source(struct index *index, struct tuple *tuple);

/** Publish a tuple replacement and append its undo record to results. */
int
memtx_vector_index_replace(struct index *index, struct tuple *old_tuple,
			   struct tuple *new_tuple, struct rlist *results);

/** Finish a statement or restore both graphs without allocating. */
void
memtx_vector_index_replace_finish(struct index *index, struct rlist *results,
				  bool rollback);

/** Replace the graph and tuple-label generation after a successful replay. */
int
memtx_vector_index_rebuild(struct index *index);

/** Count visible rows through the primary index; reject keyed counts. */
ssize_t
memtx_vector_index_count(struct index *index, enum iterator_type type,
			 const char *key, uint32_t part_count);

/** Create a bounded HNSW cursor with explicit per-request options. */
struct iterator *
memtx_vector_search_iterator_new(struct index *index,
				 const struct memtx_vector_search_opts *opts);

/** Freeze HNSW topology and primary tuple visibility through CoW. */
struct index_read_view *
memtx_vector_read_view_new(struct index *index);

/** Pin a backend snapshot and the coordinate allocation owner. */
struct memtx_vector_snapshot *
memtx_vector_snapshot_new(struct index *index);

/** Release a snapshot and its generation and coordinate pins. */
void
memtx_vector_snapshot_delete(struct memtx_vector_snapshot *view);

/** Search frozen topology through the backend-independent contract. */
int
memtx_vector_snapshot_search(struct memtx_vector_snapshot *view,
			     const float *query,
			     const struct ann_search_opts *opts,
			     struct ann_candidate *out, uint32_t *count);

/** Materialize a bounded NEIGHBOR result from a read-view HNSW snapshot. */
int
memtx_vector_read_view_select(struct index_read_view *view,
			      const char *key, uint32_t part_count,
			      uint32_t limit,
			      struct index_read_view_iterator *it);

/** Attach the parent space before the view is exposed to other threads. */
void
memtx_vector_read_view_set_space(struct index_read_view *view,
				 struct space_read_view *space);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
