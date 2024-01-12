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

struct region;
struct space;
struct space_upgrade;
struct space_upgrade_def;
struct space_upgrade_read_view;
struct tuple;

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
struct space_upgrade_def *
space_upgrade_def_dup(const struct space_upgrade_def *def);

/**
 * Frees memory occupied by a space_upgrade_def object.
 * It's okay to pass NULL to this function.
 */
void
space_upgrade_def_delete(struct space_upgrade_def *def);

/**
 * Deletes a space upgrade state.
 */
static inline void
space_upgrade_delete(struct space_upgrade *upgrade)
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
 * Starts space upgrade in the background if required.
 */
void
space_upgrade_run(struct space *space);

/**
 * Allocates a space upgrade function for a read view.
 * This function never fails (never returns NULL).
 *
 * The original space upgrade function may be dropped after calling this
 * function - the read view function doesn't depend on it.
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
 */
static inline void
space_upgrade_read_view_delete(struct space_upgrade_read_view *rv)
{
	(void)rv;
	unreachable();
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* !defined(ENABLE_SPACE_UPGRADE) */
