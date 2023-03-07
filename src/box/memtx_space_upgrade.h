/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_SPACE_UPGRADE)
# include "memtx_space_upgrade_impl.h"
#else /* !defined(ENABLE_SPACE_UPGRADE) */

#include <stdbool.h>

#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct space;
struct space_upgrade;
struct space_upgrade_read_view;
struct tuple;

/** Memtx implementation of space_vtab::prepare_upgrade. */
int
memtx_space_prepare_upgrade(struct space *old_space, struct space *new_space);

/**
 * Adds a tuple to the upgraded tuple set.
 * The tuple must not be in the set.
 */
static inline void
memtx_space_upgrade_track_tuple(struct space_upgrade *upgrade,
				struct tuple *tuple)
{
	(void)upgrade;
	(void)tuple;
	unreachable();
}

/**
 * Removes a tuple from the upgraded tuple set.
 * Does nothing if the tuple isn't in the set.
 */
static inline void
memtx_space_upgrade_untrack_tuple(struct space_upgrade *upgrade,
				  struct tuple *tuple)
{
	(void)upgrade;
	(void)tuple;
	unreachable();
}

/**
 * Returns true if a tuple fetched from a read view needs to be upgraded.
 * See the comment to read_view_tuple::needs_upgrade.
 */
static inline bool
memtx_read_view_tuple_needs_upgrade(struct space_upgrade_read_view *rv,
				    struct tuple *tuple)
{
	(void)rv;
	(void)tuple;
	unreachable();
	return false;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_SPACE_UPGRADE) */
