/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>
#include "func.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct func;

/**
 * Type of a holder that can pin a func. @sa struct func_cache_holder.
 */
enum func_holder_type {
	FUNC_HOLDER_CONSTRAINT,
	FUNC_HOLDER_SPACE_UPGRADE,
	FUNC_HOLDER_FIELD_DEFAULT,
	FUNC_HOLDER_MAX,
};

/**
 * Lowercase name of each type.
 */
extern const char *func_cache_holder_type_strs[FUNC_HOLDER_MAX];

/**
 * Definition of a holder that pinned some func. Pinning of a func is
 * a mechanism that is designed for preventing of deletion of some func from
 * func cache by storing links to holders that prevented that.
 */
struct func_cache_holder {
	/** Holders of the same func are linked into ring list by this link. */
	struct rlist link;
	/** Actual pointer to func. */
	struct func *func;
	/**
	 * Type of holder, mostly for better error generation, but also can be
	 * used for proper container_of application.
	 */
	enum func_holder_type type;
};

/**
 * Initialize function cache storage.
 */
void
func_cache_init(void);

/**
 * Cleanup function cache storage.
 */
void
func_cache_destroy(void);

/**
 * Insert a new function object in the function cache.
 * @param func Function object to insert.
 */
void
func_cache_insert(struct func *func);

/**
 * Delete a function object from the function cache.
 * The function must not have any keepers (assert, @sa func_cache_is_kept),
 * so if there is no assurance that there are no pins, @sa func_cache_is_pinned
 * must be called before.
 * If the function is not found by five ID - do nothing.
 * @param fid ID of function object.
 */
void
func_cache_delete(uint32_t fid);

/**
 * Find function by ID or return NULL if not found.
 * @param fid ID of function object.
 */
struct func *
func_by_id(uint32_t fid);

/**
 * Find function by name or return NULL if not found.
 * @param name Name of function object.
 * @param name_len Length of the name of function object.
 */
struct func *
func_by_name(const char *name, uint32_t name_len);

/**
 * Register that there is a @a holder of type @a type that is dependent
 * on function @a func.
 * The function must be in cache (asserted).
 * If a function has holders, it must not be deleted (asserted).
 */
void
func_pin(struct func *func, struct func_cache_holder *holder,
	 enum func_holder_type type);

/**
 * Notify that a @a holder does not depend anymore on function.
 * The function must be in cache (asserted).
 * If a function has no holders, it can be deleted.
 */
void
func_unpin(struct func_cache_holder *holder);

/**
 * Check whether the function @a func has holders or not.
 * If it has, @a type argument is set to the first holder's type.
 * The function must be in cache (asserted).
 * If a function has holders, it must not be deleted (asserted).
 */
bool
func_is_pinned(struct func *func, enum func_holder_type *type);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
