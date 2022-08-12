/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct index;
struct index_read_view;
struct space;

/** Read view of a space. */
struct space_read_view {
	/** Link in read_view::spaces. */
	struct rlist link;
	/** Space id. */
	uint32_t id;
	/** Replication group id. See space_opts::group_id. */
	uint32_t group_id;
	/**
	 * Max index id.
	 *
	 * (The number of entries in index_map is index_id_max + 1.)
	 */
	uint32_t index_id_max;
	/**
	 * Sparse (may contain nulls) array of index read views,
	 * indexed by index id.
	 */
	struct index_read_view **index_map;
};

static inline struct index_read_view *
space_read_view_index(struct space_read_view *space_rv, uint32_t id)
{
	return id <= space_rv->index_id_max ? space_rv->index_map[id] : NULL;
}

/** Read view of the entire database. */
struct read_view {
	/** List of engine read views, linked by engine_read_view::link. */
	struct rlist engines;
	/** List of space read views, linked by space_read_view::link. */
	struct rlist spaces;
};

#define read_view_foreach_space(space_rv, rv) \
	rlist_foreach_entry(space_rv, &(rv)->spaces, link)

/** Read view creation options. */
struct read_view_opts {
	/**
	 * Space filter: should return true if the space should be included
	 * into the read view.
	 *
	 * Default: include all spaces (return true, ignore arguments).
	 */
	bool
	(*filter_space)(struct space *space, void *arg);
	/**
	 * Index filter: should return true if the index should be included
	 * into the read view.
	 *
	 * Default: include all indexes (return true, ignore arguments).
	 */
	bool
	(*filter_index)(struct space *space, struct index *index, void *arg);
	/**
	 * Argument passed to filter functions.
	 */
	void *filter_arg;
};

/** Sets read view options to default values. */
void
read_view_opts_create(struct read_view_opts *opts);

/**
 * Opens a database read view: all changes done to the database after a read
 * view was open will not be visible from the read view.
 *
 * Engines that don't support read view creation are silently skipped.
 *
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
read_view_open(struct read_view *rv, const struct read_view_opts *opts);

/**
 * Closes a database read view.
 */
void
read_view_close(struct read_view *rv);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
