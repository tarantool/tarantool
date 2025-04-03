#pragma once

#include "index.h"
#include "memtx_engine.h"
#include "memtx_sort_data.h"

/** The base class for all memtx indexes with extra info and callbacks. */
struct memtx_index {
	/** The base index class. */
	struct index base;
	/**
	 * Whether the index had been successfully
	 * built using the memtx index sort data.
	 */
	bool built_presorted;
	/**
	 * Build the index using the memtx index sort data. Can
	 * be NULL if such a build is not supported by the index.
	 */
	bool (*build_presorted)(struct memtx_index *index,
				struct memtx_sort_data_reader *msdr);
};

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

static inline int
memtx_index_build_presorted(struct memtx_index *index,
			    struct memtx_sort_data_reader *msdr)
{
	assert(index->build_presorted != NULL);
	return index->build_presorted(index, msdr);
}

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
