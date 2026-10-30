/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "read_view_util.h"

#include <stdbool.h>

#include "diag.h"
#include "index.h"
#include "index_def.h"
#include "space.h"
#include "user_def.h"

bool
box_read_view_filter_space_cb(struct space *space, void *arg)
{
	(void)arg;
	/* Silently ignore spaces not accessible by the current user. */
	if (access_check_space(space, PRIV_R) != 0) {
		diag_clear(diag_get());
		return false;
	}
	return true;
}

bool
box_read_view_filter_index_cb(struct space *space, struct index *index,
			      void *arg)
{
	(void)space;
	(void)arg;
	/* FIXME(gh-203): Include rtree indexes into read view. */
	if (index->def->type == RTREE)
		return false;
	/* FIXME(gh-204): Include bitset indexes into read view. */
	if (index->def->type == BITSET)
		return false;
	return true;
}
