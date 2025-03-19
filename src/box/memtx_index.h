#pragma once

#include "index.h"
#include "memtx_engine.h"

/** The base class for all memtx indexes with extra info and callbacks. */
struct memtx_index {
	/** The base index class. */
	struct index base;
};

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Initialize a memtx index instance.
 * Note, this function copies the given index definition.
 */
void
memtx_index_create(struct memtx_index *index, struct memtx_engine *memtx,
		   const struct index_vtab *vtab, struct index_def *def);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
