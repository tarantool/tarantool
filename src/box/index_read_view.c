/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "index_read_view.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "box/box.h"
#include "box/port.h"
#include "diag.h"
#include "error.h"
#include "errcode.h"
#include "fiber.h"
#include "index.h"
#include "iterator_type.h"
#include "msgpuck.h"
#include "port.h"
#include "read_view.h"
#include "space_upgrade.h"
#include "small/region.h"
#include "tuple.h"
#include "tuple_dictionary.h"
#include "tuple_format.h"

int
read_view_activate(struct read_view *rv)
{
	assert(rv->owner == NULL);
	rv->owner = cord();
	struct space_read_view *space_rv;
	rlist_foreach_entry(space_rv, &rv->spaces, link) {
		assert(space_rv->format == NULL);
		if (space_rv->format_data != NULL) {
			space_rv->format =
				runtime_tuple_format_new(
					space_rv->format_data,
					space_rv->format_data_len,
					/*names_only=*/true);
			if (space_rv->format == NULL)
				goto fail;
		} else {
			space_rv->format = tuple_format_runtime;
		}
		tuple_format_ref(space_rv->format);
		if (space_rv->upgrade != NULL) {
			if (space_upgrade_read_view_activate(
					space_rv->upgrade,
					space_rv->format) != 0) {
				read_view_deactivate(rv);
				goto fail;
			}
		}
	}
	return 0;
fail:
	read_view_deactivate(rv);
	return -1;
}

void
read_view_deactivate(struct read_view *rv)
{
	assert(rv->owner == cord());
	rv->owner = NULL;
	struct space_read_view *space_rv;
	rlist_foreach_entry(space_rv, &rv->spaces, link) {
		if (space_rv->format != NULL) {
			tuple_format_unref(space_rv->format);
			space_rv->format = NULL;
		}
		if (space_rv->upgrade != NULL)
			space_upgrade_read_view_deactivate(space_rv->upgrade);
	}
}

/**
 * Prepares a tuple retrieved from a read view to be returned to the user.
 * Returns a tuple object pinned with tuple_bless on success. On error,
 * sets diag and returns NULL.
 *
 * This function applies the space upgrade function if the read view was open
 * while the space upgrade was in progress. It may only be called in the thread
 * that activated the read view, see read_view_activate().
 */
static struct tuple *
read_view_process_result(struct space_read_view *space_rv,
			 const struct read_view_tuple *rv_tuple)
{
	assert(rv_tuple->data != NULL);
	assert(rv_tuple->size != 0);
	assert(space_rv->rv->owner == cord());
	if (space_rv->upgrade != NULL) {
		return space_upgrade_read_view_apply(space_rv->upgrade,
						     rv_tuple);
	} else {
		struct tuple *tuple = tuple_new(
				space_rv->format, rv_tuple->data,
				rv_tuple->data + rv_tuple->size);
		if (tuple == NULL)
			return NULL;
		return tuple_bless(tuple);
	}
}

/**
 * Wrapper around index_read_view_get_raw() that returns a tuple allocated from
 * the runtime arena.
 */
static int
index_read_view_get(struct index_read_view *rv, const char *key,
		    uint32_t part_count, struct tuple **result)
{
	assert(rv->space->rv->owner == cord());
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct read_view_tuple result_raw;
	int rc = index_read_view_get_raw(rv, key, part_count, &result_raw);
	if (rc != 0)
		goto out;
	if (result_raw.data != NULL) {
		*result = read_view_process_result(rv->space, &result_raw);
		if (*result == NULL)
			rc = -1;
	} else {
		*result = NULL;
	}
out:
	region_truncate(region, region_svp);
	return rc;
}

/**
 * Wrapper around index_read_view_iterator_next_raw() that returns a tuple
 * allocated from the runtime arena.
 */
static int
index_read_view_iterator_next(struct index_read_view_iterator *it,
			      struct tuple **result)
{
	struct index_read_view *rv = it->base.index;
	assert(rv->space->rv->owner == cord());
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	struct read_view_tuple result_raw;
	int rc = index_read_view_iterator_next_raw(it, &result_raw);
	if (rc != 0)
		goto out;
	if (result_raw.data != NULL) {
		*result = read_view_process_result(rv->space, &result_raw);
		if (*result == NULL)
			rc = -1;
	} else {
		*result = NULL;
	}
out:
	region_truncate(region, region_svp);
	return rc;
}

int
box_index_read_view_tuple_position(struct index_read_view *rv,
				   const char *tuple, const char *tuple_end,
				   const char **packed_pos,
				   const char **packed_pos_end)
{
	return box_iterator_position_from_tuple(tuple, tuple_end,
						rv->def->cmp_def, packed_pos,
						packed_pos_end);
}

int
box_index_read_view_get(struct index_read_view *rv, const char *key,
			const char *key_end, struct tuple **result)
{
	assert(rv->space->rv->owner == cord());
	assert(key != NULL);
	assert(key_end != NULL);
	assert(result != NULL);
	mp_tuple_assert(key, key_end);
	if (box_check_slice() != 0)
		return -1;
	if (!rv->def->opts.is_unique) {
		diag_set(ClientError, ER_MORE_THAN_ONE_TUPLE);
		return -1;
	}
	uint32_t part_count = mp_decode_array(&key);
	if (exact_key_validate(rv->def, key, part_count) != 0)
		return -1;
	if (index_read_view_get(rv, key, part_count, result) != 0)
		return -1;
	return 0;
}

ssize_t
box_index_read_view_count(struct index_read_view *rv, int iterator,
			  const char *key, const char *key_end)
{
	assert(key != NULL);
	assert(key_end != NULL);
	mp_tuple_assert(key, key_end);
	if (iterator < 0 || iterator >= iterator_type_MAX) {
		diag_set(IllegalParams, "Invalid iterator type");
		return -1;
	}
	enum iterator_type type = iterator;
	uint32_t part_count = mp_decode_array(&key);
	if (iterator_validate(rv->def, type, key, part_count) != 0)
		return -1;
	return index_read_view_count(rv, type, key, part_count);
}

int
box_index_read_view_quantile(
	struct index_read_view *rv, double level, const char *begin_key,
	const char *begin_key_end, const char *end_key, const char *end_key_end,
	const char **quantile_key, const char **quantile_key_end)
{
	mp_tuple_assert(begin_key, begin_key_end);
	mp_tuple_assert(end_key, end_key_end);
	assert(quantile_key != NULL);
	assert(quantile_key_end != NULL);

	uint32_t begin_part_count = mp_decode_array(&begin_key);
	uint32_t end_part_count = mp_decode_array(&end_key);
	if (quantile_validate(rv->def, level, begin_key, begin_part_count,
			      end_key, end_part_count) != 0)
		return -1;

	uint32_t quantile_key_size;
	if (index_read_view_quantile(rv, level, begin_key, begin_part_count,
				     end_key, end_part_count, quantile_key,
				     &quantile_key_size) != 0)
		return -1;

	*quantile_key_end = *quantile_key != NULL ?
			    *quantile_key + quantile_key_size : NULL;
	return 0;
}

int
box_index_read_view_select(struct index_read_view *rv, int iterator,
			   uint32_t offset, uint32_t limit, const char *key,
			   const char *key_end, const char **packed_pos,
			   const char **packed_pos_end, bool update_pos,
			   struct port *port)
{
	assert(rv->space->rv->owner == cord());
	assert(key != NULL);
	assert(key_end != NULL);
	assert(port != NULL);
	assert(!update_pos || (packed_pos != NULL && packed_pos_end != NULL));
	assert(packed_pos == NULL || packed_pos_end != NULL);
	mp_tuple_assert(key, key_end);
	if (iterator < 0 || iterator >= iterator_type_MAX) {
		diag_set(IllegalParams, "Invalid iterator type");
		return -1;
	}
	enum iterator_type type = iterator;
	uint32_t part_count = mp_decode_array(&key);
	if (iterator_validate(rv->def, type, key, part_count) != 0)
		return -1;
	const char *pos, *pos_end;
	if (box_iterator_position_unpack(*packed_pos, *packed_pos_end,
					 rv->def, key, part_count,
					 type, &pos, &pos_end) != 0)
		return -1;
	struct index_read_view_iterator it;
	if (index_read_view_create_iterator_with_offset(rv, type, key,
							part_count, pos,
							offset, &it) != 0)
		return -1;
	int rc = 0;
	uint32_t found = 0;
	port_c_create(port);
	while (found < limit) {
		rc = box_check_slice();
		if (rc != 0)
			break;
		struct tuple *tuple;
		rc = index_read_view_iterator_next(&it, &tuple);
		if (rc != 0 || tuple == NULL)
			break;
		port_c_add_tuple(port, tuple);
		found++;
	}

	if (rc != 0)
		goto fail;

	if (update_pos) {
		uint32_t pos_size;
		/*
		 * Iterator position is extracted even if no tuples were found
		 * to check if pagination is supported by index.
		 */
		if (index_read_view_iterator_position(&it, &pos,
						      &pos_size) != 0)
			goto fail;
		box_iterator_position_pack(pos, pos + pos_size, found,
					   packed_pos, packed_pos_end);
	}
	index_read_view_iterator_destroy(&it);
	return 0;
fail:
	port_destroy(port);
	index_read_view_iterator_destroy(&it);
	return -1;
}

int
box_index_read_view_create_iterator_with_offset(
	struct index_read_view *rv, int iterator, const char *key,
	const char *key_end, const char *packed_pos, const char *packed_pos_end,
	uint32_t offset, struct index_read_view_iterator *it)
{
	assert(key != NULL);
	assert(key_end != NULL);
	mp_tuple_assert(key, key_end);
	if (iterator < 0 || iterator >= iterator_type_MAX) {
		diag_set(IllegalParams, "Invalid iterator type");
		return -1;
	}
	enum iterator_type type = iterator;
	uint32_t part_count = mp_decode_array(&key);
	if (iterator_validate(rv->def, type, key, part_count) != 0)
		return -1;
	const char *pos, *pos_end;
	uint32_t region_svp = region_used(&fiber()->gc);
	/*
	 * Position is unpacked to a buffer, allocated on region. It is OK,
	 * because tree index uses position only on iterator creation, which
	 * will happen before the region will be truncated.
	 */
	if (box_iterator_position_unpack(packed_pos, packed_pos_end,
					 rv->def, key, part_count,
					 type, &pos, &pos_end) != 0)
		goto error;
	if (index_read_view_create_iterator_with_offset(rv, type, key,
							part_count, pos,
							offset, it) != 0)
		goto error;
	region_truncate(&fiber()->gc, region_svp);
	return 0;
error:
	region_truncate(&fiber()->gc, region_svp);
	return -1;
}

int
box_index_read_view_create_iterator(struct index_read_view *rv, int iterator,
				    const char *key, const char *key_end,
				    const char *packed_pos,
				    const char *packed_pos_end,
				    struct index_read_view_iterator *it)
{
	return box_index_read_view_create_iterator_with_offset(
		rv, iterator, key, key_end, packed_pos, packed_pos_end, 0, it);
}

int
box_index_read_view_iterator_next(struct index_read_view_iterator *it,
				  struct tuple **result)
{
	if (box_check_slice() != 0)
		return -1;
	return index_read_view_iterator_next(it, result);
}

void
box_index_read_view_iterator_destroy(struct index_read_view_iterator *it)
{
	index_read_view_iterator_destroy(it);
}
