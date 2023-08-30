/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "read_view.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "assoc.h"
#include "box.h"
#include "engine.h"
#include "fiber.h"
#include "field_def.h"
#include "index.h"
#include "salad/grp_alloc.h"
#include "small/rlist.h"
#include "space.h"
#include "space_cache.h"
#include "space_upgrade.h"
#include "tarantool_ev.h"
#include "trivia/util.h"
#include "tuple.h"
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
	space_rv->index_id_max = space->index_id_max;
	memset(space_rv->index_map, 0, index_map_size);
	for (uint32_t i = 0; i <= space->index_id_max; i++) {
		struct index *index = space->index_map[i];
		if (index == NULL ||
		    !opts->filter_index(space, index, opts->filter_arg))
			continue;
		space_rv->index_map[i] = index_create_read_view(index);
		if (space_rv->index_map[i] == NULL)
			goto fail;
		space_rv->index_map[i]->space = space_rv;
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
