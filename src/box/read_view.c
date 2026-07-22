/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "read_view.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "assoc.h"
#include "box.h"
#include "diag.h"
#include "engine.h"
#include "error.h"
#include "errcode.h"
#include "fiber.h"
#include "field_def.h"
#include "index.h"
#include "iterator_type.h"
#include "msgpuck.h"
#include "port.h"
#include "salad/grp_alloc.h"
#include "small/region.h"
#include "small/rlist.h"
#include "space.h"
#include "space_cache.h"
#include "space_upgrade.h"
#include "tarantool_ev.h"
#include "trivia/util.h"
#include "tuple.h"
#include "tuple_dictionary.h"
#include "tuple_format.h"
#include "vclock/vclock.h"

/**
 * Map of all open read views: id -> struct read_view.
 *
 * Initialized on demand, when the first read view is opened.
 * Destroyed when the last read view is closed.
 */
static struct mh_i64ptr_t *read_views;

/**
 * Monotonically growing counter used for assigning unique ids to read views.
 */
static uint64_t next_read_view_id = 1;

static bool
default_space_filter(struct space *space, void *arg)
{
	(void)space;
	(void)arg;
	return true;
}

static bool
default_index_filter(struct space *space, struct index *index, void *arg)
{
	(void)space;
	(void)index;
	(void)arg;
	return true;
}

void
read_view_opts_create(struct read_view_opts *opts)
{
	opts->name = NULL;
	opts->is_system = false;
	opts->filter_space = default_space_filter;
	opts->filter_index = default_index_filter;
	opts->filter_arg = NULL;
	opts->enable_field_names = false;
	opts->enable_space_upgrade = false;
	opts->enable_data_temporary_spaces = false;
	opts->disable_decompression = false;
}

static void
space_read_view_delete(struct space_read_view *space_rv)
{
	assert(space_rv->format == NULL);
	if (space_rv->field_count > 0)
		field_def_array_delete(space_rv->fields, space_rv->field_count);
	free(space_rv->format_data);
	for (uint32_t i = 0; i <= space_rv->index_id_max; i++) {
		struct index_read_view *index_rv = space_rv->index_map[i];
		if (index_rv != NULL) {
			assert(index_rv->space == space_rv);
			index_read_view_delete(index_rv);
		}
	}
	if (space_rv->upgrade != NULL)
		space_upgrade_read_view_delete(space_rv->upgrade);
	TRASH(space_rv);
	free(space_rv);
}

static struct space_read_view *
space_read_view_new(struct space *space, const struct read_view_opts *opts)
{
	struct space_read_view *space_rv;
	size_t index_map_size = sizeof(*space_rv->index_map) *
				(space->index_id_max + 1);
	struct grp_alloc all = grp_alloc_initializer();
	grp_alloc_reserve_data(&all, sizeof(*space_rv));
	grp_alloc_reserve_str0(&all, space_name(space));
	grp_alloc_reserve_data(&all, index_map_size);
	grp_alloc_use(&all, xmalloc(grp_alloc_size(&all)));
	space_rv = grp_alloc_create_data(&all, sizeof(*space_rv));
	space_rv->name = grp_alloc_create_str0(&all, space_name(space));
	space_rv->index_map = grp_alloc_create_data(&all, index_map_size);
	assert(grp_alloc_size(&all) == 0);

	space_rv->id = space_id(space);
	space_rv->group_id = space_group_id(space);
	if (opts->enable_field_names && space->def->field_count > 0) {
		space_rv->fields = field_def_array_dup(space->def->fields,
						       space->def->field_count);
		assert(space_rv->fields != NULL);
		space_rv->field_count = space->def->field_count;
	} else {
		space_rv->fields = NULL;
		space_rv->field_count = 0;
	}
	if (opts->enable_field_names &&
	    space->def->format_data != NULL) {
		space_rv->format_data = xmalloc(space->def->format_data_len);
		memcpy(space_rv->format_data, space->def->format_data,
		       space->def->format_data_len);
		space_rv->format_data_len = space->def->format_data_len;
	} else {
		space_rv->format_data = NULL;
		space_rv->format_data_len = 0;
	}
	space_rv->format = NULL;
	if (opts->enable_space_upgrade && space->upgrade != NULL) {
		space_rv->upgrade = space_upgrade_read_view_new(space->upgrade);
		assert(space_rv->upgrade != NULL);
	} else {
		space_rv->upgrade = NULL;
	}
	space_rv->engine = space->engine;
	space_rv->index_id_max = space->index_id_max;
	memset(space_rv->index_map, 0, index_map_size);
	space_rv->index_count = 0;
	for (uint32_t i = 0; i <= space->index_id_max; i++) {
		struct index *index = space->index_map[i];
		if (index == NULL ||
		    !opts->filter_index(space, index, opts->filter_arg))
			continue;
		space_rv->index_map[i] = index_create_read_view(index);
		if (space_rv->index_map[i] == NULL)
			goto fail;
		space_rv->index_map[i]->space = space_rv;
		space_rv->index_count++;
	}
	return space_rv;
fail:
	space_read_view_delete(space_rv);
	return NULL;
}

/** Argument passed to read_view_add_space_cb(). */
struct read_view_add_space_cb_arg {
	/** Read view to add a space to. */
	struct read_view *rv;
	/** Read view creation options. */
	const struct read_view_opts *opts;
};

static int
read_view_add_space_cb(struct space *space, void *arg_raw)
{
	struct read_view_add_space_cb_arg *arg = arg_raw;
	struct read_view *rv = arg->rv;
	const struct read_view_opts *opts = arg->opts;
	if ((space->engine->flags & ENGINE_SUPPORTS_READ_VIEW) == 0 ||
	    (space_is_data_temporary(space) &&
	     !opts->enable_data_temporary_spaces) ||
	    !opts->filter_space(space, opts->filter_arg))
		return 0;
	struct space_read_view *space_rv = space_read_view_new(space, opts);
	if (space_rv == NULL)
		return -1;
	space_rv->rv = rv;
	rlist_add_tail_entry(&rv->spaces, space_rv, link);
	return 0;
}

/** Helper function that adds a read view object to the read_views map. */
static void
read_view_register(struct read_view *rv)
{
	if (read_views == NULL)
		read_views = mh_i64ptr_new();
	struct mh_i64ptr_node_t node = { rv->id, rv };
	struct mh_i64ptr_node_t old_node;
	struct mh_i64ptr_node_t *old_node_ptr = &old_node;
	mh_i64ptr_put(read_views, &node, &old_node_ptr, NULL);
	assert(old_node_ptr == NULL);
}

/** Helper function that removes a read view object from the read_views map. */
static void
read_view_unregister(struct read_view *rv)
{
	assert(read_views != NULL);
	struct mh_i64ptr_t *h = read_views;
	mh_int_t i = mh_i64ptr_find(h, rv->id, NULL);
	assert(i != mh_end(h));
	assert(mh_i64ptr_node(h, i)->val == rv);
	mh_i64ptr_del(h, i, NULL);
	if (mh_size(h) == 0) {
		mh_i64ptr_delete(h);
		read_views = NULL;
	}
}

int
read_view_open(struct read_view *rv, const struct read_view_opts *opts)
{
	rv->id = next_read_view_id++;
	assert(opts->name != NULL);
	rv->name = xstrdup(opts->name);
	rv->is_system = opts->is_system;
	rv->disable_decompression = opts->disable_decompression;
	rv->timestamp = ev_monotonic_now(loop());
	vclock_copy(&rv->vclock, box_vclock);
	rv->owner = NULL;
	rlist_create(&rv->engines);
	rlist_create(&rv->spaces);
	read_view_register(rv);
	struct engine *engine;
	engine_foreach(engine) {
		if ((engine->flags & ENGINE_SUPPORTS_READ_VIEW) == 0)
			continue;
		struct engine_read_view *engine_rv =
			engine_create_read_view(engine, opts);
		if (engine_rv == NULL)
			goto fail;
		rlist_add_tail_entry(&rv->engines, engine_rv, link);
	}
	struct read_view_add_space_cb_arg add_space_cb_arg = {
		.rv = rv,
		.opts = opts,
	};
	if (space_foreach(read_view_add_space_cb, &add_space_cb_arg) != 0)
		goto fail;
	return 0;
fail:
	read_view_close(rv);
	return -1;
}

void
read_view_close(struct read_view *rv)
{
	assert(rv->owner == NULL);
	read_view_unregister(rv);
	struct space_read_view *space_rv, *next_space_rv;
	rlist_foreach_entry_safe(space_rv, &rv->spaces, link,
				 next_space_rv) {
		assert(space_rv->rv == rv);
		space_read_view_delete(space_rv);
	}
	struct engine_read_view *engine_rv, *next_engine_rv;
	rlist_foreach_entry_safe(engine_rv, &rv->engines, link,
				 next_engine_rv) {
		engine_read_view_delete(engine_rv);
	}
	free(rv->name);
	TRASH(rv);
}

struct read_view *
read_view_by_id(uint64_t id)
{
	struct mh_i64ptr_t *h = read_views;
	if (h == NULL)
		return NULL;
	mh_int_t i = mh_i64ptr_find(h, id, NULL);
	if (i == mh_end(h))
		return NULL;
	return mh_i64ptr_node(h, i)->val;
}

bool
read_view_foreach(read_view_foreach_f cb, void *arg)
{
	struct mh_i64ptr_t *h = read_views;
	if (h == NULL)
		return true;
	mh_int_t i;
	mh_foreach(h, i) {
		struct read_view *rv = mh_i64ptr_node(h, i)->val;
		if (!cb(rv, arg))
			return false;
	}
	return true;
}

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
