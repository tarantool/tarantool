/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>
#include "space.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Internal change counter. Grows faster, than public schema_version,
 * because we need to remember when to update pointers to already
 * non-existent space objects on space:truncate() operation.
 */
extern uint32_t space_cache_version;

/**
 * Triggers fired after committing a change in space definition.
 * The space is passed to the trigger callback in the event
 * argument. It is the new space in case of create/update or
 * the old space in case of drop.
 */
extern struct rlist on_alter_space;

/**
 * Initialize space cache storage.
 */
void
space_cache_init(void);

/**
 * Cleanup space cache storage.
 */
void
space_cache_destroy(void);

/**
 * Try to look up a space by space number in the space cache.
 * FFI-friendly no-exception-thrown space lookup function.
 *
 * @return NULL if space not found, otherwise space object.
 */
struct space *
space_by_id(uint32_t id);

/**
 * Try to look up a space by space name in the space name cache.
 *
 * @return NULL if space not found, otherwise space object.
 */
struct space *
space_by_name(const char *name);

/**
 * Find a space by given ID. Return NULL and set diag if not found.
 */
static inline struct space *
space_cache_find(uint32_t id)
{
	static uint32_t prev_space_cache_version;
	static struct space *space;
	if (prev_space_cache_version != space_cache_version)
		space = NULL;
	if (space && space->def->id == id)
		return space;
	space = space_by_id(id);
	if (space != NULL) {
		prev_space_cache_version = space_cache_version;
		return space;
	}
	diag_set(ClientError, ER_NO_SUCH_SPACE, int2str(id));
	return NULL;
}

/**
 * Exception-throwing version of space_cache_find(..)
 */
static inline struct space *
space_cache_find_xc(uint32_t id)
{
	struct space *space = space_cache_find(id);
	if (space == NULL)
		diag_raise();
	return space;
}

/**
 * Call a visitor function on every space in the space cache.
 * Traverse system spaces before other.
 */
int
space_foreach(int (*func)(struct space *sp, void *udata), void *udata);

/**
 * Update contents of the space cache.
 *
 * If @old_space is NULL, insert @new_space into the cache.
 * If @new_space is NULL, delete @old_space from the cache.
 * If neither @old_space nor @new_space is NULL, replace
 * @old_space with @new_space in the cache (both spaces must
 * have the same id).
 */
void
space_cache_replace(struct space *old_space, struct space *new_space);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
