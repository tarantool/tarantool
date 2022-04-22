/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

struct space;
struct tuple;

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
 *   result_process(&res_proc, &rc, result);
 *
 * Note, if result_process_prepare() was called, then result_process()
 * must be called as well, because it may need to free some resources.
 */
struct result_processor {
};

static inline void
result_process_prepare(struct result_processor *p, struct space *space)
{
	(void)p;
	(void)space;
}

static inline void
result_process(struct result_processor *p, int *rc, struct tuple **result)
{
	(void)p;
	(void)rc;
	(void)result;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
