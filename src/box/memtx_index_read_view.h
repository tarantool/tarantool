#pragma once

#include "index.h"
#include "memtx_sort_data.h"

/**
 * The base class for memtx index read views,
 * stores the engine-specific index data.
 */
struct memtx_index_read_view {
	/** The base index read view class. */
	struct index_read_view base;
	/** The index's sort data dumping method implementation. */
	int (*dump_sort_data)(
		struct memtx_index_read_view *rv, ssize_t tuple_count,
		struct memtx_sort_data *msd, bool *have_more);
};

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

static inline int
memtx_index_read_view_dump_sort_data(
	struct memtx_index_read_view *rv, ssize_t tuple_count,
	struct memtx_sort_data *msd, bool *have_more)
{
	assert(rv->dump_sort_data != NULL);
	return rv->dump_sort_data(rv, tuple_count, msd, have_more);
}

void
memtx_index_read_view_create(struct memtx_index_read_view *rv,
			     const struct index_read_view_vtab *vtab,
			     struct index_def *def);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
