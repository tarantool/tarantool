/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct index;
struct space;

/** read_view_opts::filter_space callback used by box API. */
bool
box_read_view_filter_space_cb(struct space *space, void *arg);

/** read_view_opts::filter_index callback used by box API. */
bool
box_read_view_filter_index_cb(struct space *space, struct index *index,
			      void *arg);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
