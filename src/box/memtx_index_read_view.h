#pragma once

#include "index.h"

/**
 * The base class for memtx index read views,
 * stores the engine-specific index data.
 */
struct memtx_index_read_view {
	/** The base index read view class. */
	struct index_read_view base;
};

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

void
memtx_index_read_view_create(struct memtx_index_read_view *rv,
			     const struct index_read_view_vtab *vtab,
			     struct index_def *def);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
