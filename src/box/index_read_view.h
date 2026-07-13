/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct index_read_view;
struct index_read_view_iterator;
struct port;
struct read_view;
struct tuple;

/**
 * Activates a read view for exclusive use in the current thread.
 *
 * A read view must be activated before any of the box API functions declared
 * below may be used. After a read view is activated, it may only be used in
 * the thread that activated it. A read view must be deactivated in the same
 * thread before it may be closed.
 *
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
read_view_activate(struct read_view *rv);

/**
 * Deactivates a read view activated by read_view_activate().
 *
 * A read view may only be deactivated by the thread that activated it.
 */
void
read_view_deactivate(struct read_view *rv);

/**
 * Gets a tuple from an index read view by a key.
 * Returns 0 on success. On error, returns -1 and sets diag.
 * The read view must be activated, see read_view_activate().
 */
int
box_index_read_view_get(struct index_read_view *rv, const char *key,
			const char *key_end, struct tuple **result);

/**
 * Counts tuples matching the given key in an index read view.
 * On error, returns -1 and sets diag.
 * The read view must be activated, see read_view_activate().
 */
ssize_t
box_index_read_view_count(struct index_read_view *rv, int iterator,
			  const char *key, const char *key_end);

/**
 * Returns a quantile point in an indexed range allocated on the fiber region.
 * Returns 0 on success. On error, returns -1 and sets diag.
 * The read view must be activated, see read_view_activate().
 */
int
box_index_read_view_quantile(
	struct index_read_view *rv, double level, const char *begin_key,
	const char *begin_key_end, const char *end_key, const char *end_key_end,
	const char **quantile_key, const char **quantile_key_end);

/**
 * Selects tuples from an index read view.
 * If packed_pos is not NULL and *packed_pos is not NULL, selection begins
 * right after tuple with position, described by this argument.
 * If update_pos is true, packed_pos and packed_pos_end are updated to
 * position of last selected tuple. Returned position is allocated
 * on the fiber region. If update_pos is true, packed_pos and packed_pos_end
 * must not be NULL.
 * Returns 0 on success. On error, returns -1 and sets diag.
 * The read view must be activated, see read_view_activate().
 */
int
box_index_read_view_select(struct index_read_view *rv, int iterator,
			   uint32_t offset, uint32_t limit, const char *key,
			   const char *key_end, const char **packed_pos,
			   const char **packed_pos_end, bool update_pos,
			   struct port *port);

/**
 * Gets packed position of tuple in index, which can be passed to box_select
 * (multikey and func indexes are not supported). Returned position
 * is allocated on the fiber region.
 */
int
box_index_read_view_tuple_position(struct index_read_view *rv,
				   const char *tuple, const char *tuple_end,
				   const char **packed_pos,
				   const char **packed_pos_end);

/**
 * Creates an iterator over an index read view, positioned after passed
 * position, if it is not NULL.
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
box_index_read_view_create_iterator(struct index_read_view *rv, int iterator,
				    const char *key, const char *key_end,
				    const char *packed_pos,
				    const char *packed_pos_end,
				    struct index_read_view_iterator *it);

/**
 * Same as box_index_read_view_create_iterator, but skips the specified by the
 * offset parameter amount of tuples.
 */
int
box_index_read_view_create_iterator_with_offset(
	struct index_read_view *rv, int iterator, const char *key,
	const char *key_end, const char *packed_pos, const char *packed_pos_end,
	uint32_t offset, struct index_read_view_iterator *it);

/**
 * Retrieves the next tuple from an index read view iterator.
 * Returns 0 on success. On error, returns -1 and sets diag.
 * The tuple is returned in the 'result' argument. On EOF, it's set to NULL.
 * The read view must be activated, see read_view_activate().
 */
int
box_index_read_view_iterator_next(struct index_read_view_iterator *it,
				  struct tuple **result);

/**
 * Destroys an index read view iterator.
 */
void
box_index_read_view_iterator_destroy(struct index_read_view_iterator *it);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
