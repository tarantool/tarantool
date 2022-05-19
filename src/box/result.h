/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>

#include "space.h"
#include "space_upgrade.h"
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Helpers that apply various transformations to tuples fetched from a space.
 * The procedure is split in two parts, because the read operation may yield,
 * which opens a time window during which the space struct can be deleted.
 * The 'prepare' phase is supposed to reference and store in result_processor
 * all data structures needed to apply transforamtions.
 *
 * Used by box methods that read tuples from a space and return them to
 * the user like this:
 *
 *   struct result_processor res_proc;
 *   result_process_prepare(&res_proc, space);
 *   rc = index_get(index, key, part_count, result);
 *   result_process_perform(&res_proc, &rc, result);
 *
 * Note, if result_process_prepare() was called, then result_process_perform()
 * must be called as well, because it may need to free some resources.
 */
struct result_processor {
	/** Space upgrade state or NULL. */
	struct space_upgrade *upgrade;
};

static inline void
result_process_prepare(struct result_processor *p, struct space *space)
{
	p->upgrade = space->upgrade;
	if (unlikely(p->upgrade != NULL))
		space_upgrade_ref(p->upgrade);
}

static inline void
result_process_perform(struct result_processor *p, int *rc,
		       struct tuple **result)
{
	if (likely(p->upgrade == NULL))
		return;
	if (*rc == 0 && *result != NULL) {
		*result = space_upgrade_apply(p->upgrade, *result);
		if (*result == NULL)
			*rc = -1;
	}
	space_upgrade_unref(p->upgrade);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
