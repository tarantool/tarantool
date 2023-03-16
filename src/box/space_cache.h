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
 * Type of a holder that can pin a space. @sa struct space_cache_holder.
 */
enum space_cache_holder_type {
	SPACE_HOLDER_FOREIGN_KEY,
	SPACE_HOLDER_MAX,
};

/**
 * Lowercase name of each type.
 */
extern const char *space_cache_holder_type_strs[SPACE_HOLDER_MAX];

struct space_cache_holder;
typedef void
(*space_cache_on_replace)(struct space_cache_holder *holder,
			  struct space *old_space);

/**
 * Definition of a holder that pinned some space. Pinning of a space is
 * a mechanism that is designed for preventing of deletion of some space from
 * space cache by storing links to holders that prevented that. On the other
 * hand it is allowed to replace a space with another - the new space becomes
 * pinned after this point.
 */
struct space_cache_holder {
	/** Holders of the same func are linked into ring list by this link. */
	struct rlist link;
	/** Actual pointer to space. */
	struct space *space;
	/** Callback that is called when the space is replaced in cache. */
	space_cache_on_replace on_replace;
	/**
	 * Type of holder, mostly for better error generation, but also can be
	 * used for proper container_of application.
	 */
	enum space_cache_holder_type type;
	/** True for a space that had pinned itself. */
	bool selfpin;
};

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
 * Find minimal unused id, which is greater than cur_id.
 * If there is no available id, BOX_SPACE_MAX + 1 is returned.
 */
uint32_t
space_cache_find_next_unused_id(uint32_t cur_id);

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

/** No-op callback for space_cache_pin */
void
space_cache_on_replace_noop(struct space_cache_holder *holder,
			    struct space *old_space);

/**
 * Register that there is a @a holder of type @a type that is dependent
 * on @a space.
 * The space must be in cache (asserted).
 * If a space has holders, it must not be deleted (asserted). It can be
 * replaced though, the holder will hold the new space in that case and
 * @a on_replace callback is called.
 * @a selfpin expected to be true if a space pins itself.
 */
void
space_cache_pin(struct space *space, struct space_cache_holder *holder,
		space_cache_on_replace on_replace,
		enum space_cache_holder_type type, bool selfpin);

/**
 * Notify that a @a holder does not depend anymore on @a space.
 * The space must be in cache (asserted).
 * If a space has no holders, it can be deleted.
 */
void
space_cache_unpin(struct space_cache_holder *holder);

/**
 * Check whether the @a space has holders or not.
 * If it has, @a type argument is set to the first holder's type.
 * The function must be in cache (asserted).
 * If a space has holders, it must not be deleted (asserted).
 */
bool
space_cache_is_pinned(struct space *space, enum space_cache_holder_type *type);

#if defined(__cplusplus)
} /* extern "C" */

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

#endif /* defined(__cplusplus) */
