/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/config.h"

#if defined(ENABLE_SPACE_UPGRADE)
# include "space_upgrade_impl.h"
#else /* !defined(ENABLE_SPACE_UPGRADE) */

#include <stddef.h>

#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct key_def;
struct region;
struct space;
struct space_def;
struct space_upgrade;
struct space_upgrade_def;
struct tuple;
struct tuple_format;

/**
 * Decodes space upgrade definition from MsgPack data.
 * Returns a space_upgrade_def object allocated on the region on success,
 * NULL on error.
 */
struct space_upgrade_def *
space_upgrade_def_decode(const char **data, struct region *region);

/**
 * Duplicates the given space_upgrade_def object and returns a copy.
 * The copy is allocated on malloc. It's okay to pass NULL to this
 * function, in which case it returns NULL. The function never fails.
 */
static inline struct space_upgrade_def *
space_upgrade_def_dup(const struct space_upgrade_def *def)
{
	assert(def == NULL);
	(void)def;
	return NULL;
}

/**
 * Frees memory occupied by a space_upgrade_def object.
 * It's okay to pass NULL to this function.
 */
static inline void
space_upgrade_def_delete(struct space_upgrade_def *def)
{
	assert(def == NULL);
	(void)def;
}

/**
 * Creates a space upgrade state from a definition, space name, primary key
 * definition and the new space format. Returns NULL and sets diag on error.
 * The reference count of the new state is set to 1.
 */
static inline struct space_upgrade *
space_upgrade_new(const struct space_upgrade_def *def, const char *space_name,
		  const struct key_def *pk_def, struct tuple_format *format)
{
	(void)def;
	(void)space_name;
	(void)pk_def;
	(void)format;
	unreachable();
	return NULL;
}

/**
 * Increments the reference counter of a space upgrade state,
 * thus preventing it from being freed.
 *
 * We need to reference a space upgrade state if we are going to yield,
 * because the ongoing space upgrade may complete and delete the space
 * struct.
 */
static inline void
space_upgrade_ref(struct space_upgrade *upgrade)
{
	(void)upgrade;
	unreachable();
}

/**
 * Decrements the reference counter of a space upgrade state.
 * The state is deleted when its reference count reaches zero.
 */
static inline void
space_upgrade_unref(struct space_upgrade *upgrade)
{
	(void)upgrade;
	unreachable();
}

/**
 * Applies the given space upgrade function to a tuple.
 * Returns the new tuple on success, NULL on error.
 * The new tuple is referenced with tuple_bless.
 */
static inline struct tuple *
space_upgrade_apply(struct space_upgrade *upgrade, struct tuple *tuple)
{
	(void)upgrade;
	(void)tuple;
	unreachable();
	return NULL;
}

/**
 * Checks if a space alter operation may proceed.
 * Returns -1 and sets diag if it may not.
 */
int
space_upgrade_check_alter(struct space *space, struct space_def *new_def);

/**
 * Starts space upgrade in the background if required.
 */
void
space_upgrade_run(struct space *space);

/**
 * Allocates a space upgrade function for a read view.
 * This function never fails (never returns NULL).
 *
 * The original space upgrade function may be dropped after calling this
 * function - the read view function doesn't depend on it. The read view
 * function may be used from any thread, but it must be activated with
 * space_upgrade_read_view_activate() in that thread first.
 */
static inline struct space_upgrade_read_view *
space_upgrade_read_view_new(struct space_upgrade *upgrade)
{
	(void)upgrade;
	unreachable();
	return NULL;
}

/**
 * Frees a space upgrade read view function.
 *
 * The function must be deactivated, see space_upgrade_read_view_deactivate().
 */
static inline void
space_upgrade_read_view_delete(struct space_upgrade_read_view *rv)
{
	(void)rv;
	unreachable();
}

/**
 * Activates a space upgrade read view function.
 * Returns 0 on success, -1 on error.
 *
 * A space upgrade read view function may not be used unless it's activated.
 * Once activated, it may only be used in the thread it was activated in.
 */
static inline int
space_upgrade_read_view_activate(struct space_upgrade_read_view *rv)
{
	(void)rv;
	unreachable();
	return 0;
}

/**
 * Deactivates a space upgrade read view function.
 *
 * This function must be called in the same thread where it was activated.
 * It's okay to call this function on an inactive read view - it's a no-op
 * then.
 */
static inline void
space_upgrade_read_view_deactivate(struct space_upgrade_read_view *rv)
{
	(void)rv;
	unreachable();
}

/**
 * Applies a space upgrade function to a tuple retrieved from a read view.
 * Returns the new tuple on success, NULL on error.
 * The new tuple is referenced with tuple_bless.
 *
 * The space upgrade read view function must be activated in the current
 * thread, see space_upgrade_read_view_activate().
 */
static inline struct tuple *
space_upgrade_read_view_apply(struct space_upgrade_read_view *rv,
			      struct tuple *tuple)
{
	(void)rv;
	(void)tuple;
	unreachable();
	return NULL;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_SPACE_UPGRADE) */
