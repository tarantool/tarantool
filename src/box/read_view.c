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

#include "engine.h"
#include "index.h"
#include "salad/grp_alloc.h"
#include "small/rlist.h"
#include "space.h"
#include "space_cache.h"
#include "trivia/util.h"
#include "tuple.h"

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
	opts->filter_space = default_space_filter;
	opts->filter_index = default_index_filter;
	opts->filter_arg = NULL;
	opts->needs_field_names = false;
}

static void
space_read_view_delete(struct space_read_view *space_rv)
{
	for (uint32_t i = 0; i <= space_rv->index_id_max; i++) {
		struct index_read_view *index_rv = space_rv->index_map[i];
		if (index_rv != NULL) {
			assert(index_rv->space == space_rv);
			index_read_view_delete(index_rv);
		}
	}
	tuple_format_unref(space_rv->format);
	TRASH(space_rv);
	free(space_rv);
}

static struct space_read_view *
space_read_view_new(struct space *space, const struct read_view_opts *opts)
{
	struct tuple_format *format = tuple_format_runtime;
	if (opts->needs_field_names) {
		/**
		 * Sic: Even though a tuple dictionary has a reference counter,
		 * we can't reuse the tuple dictionary used by the space tuple
		 * format, because it may change when the space is altered, see
		 * tuple_dictionary_swap.
		 */
		struct tuple_dictionary *dict = tuple_dictionary_new(
			space->def->fields, space->def->field_count);
		if (dict == NULL)
			return NULL;
		format = runtime_tuple_format_new(dict);
		tuple_dictionary_unref(dict);
		if (format == NULL)
			return NULL;
	}

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
	space_rv->format = format;
	tuple_format_ref(format);
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
	    !opts->filter_space(space, opts->filter_arg))
		return 0;
	struct space_read_view *space_rv = space_read_view_new(space, opts);
	if (space_rv == NULL)
		return -1;
	rlist_add_tail_entry(&rv->spaces, space_rv, link);
	return 0;
}

int
read_view_open(struct read_view *rv, const struct read_view_opts *opts)
{
	rlist_create(&rv->engines);
	rlist_create(&rv->spaces);
	struct engine *engine;
	engine_foreach(engine) {
		if ((engine->flags & ENGINE_SUPPORTS_READ_VIEW) == 0)
			continue;
		struct engine_read_view *engine_rv =
			engine_create_read_view(engine);
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
	struct space_read_view *space_rv, *next_space_rv;
	rlist_foreach_entry_safe(space_rv, &rv->spaces, link,
				 next_space_rv) {
		space_read_view_delete(space_rv);
	}
	struct engine_read_view *engine_rv, *next_engine_rv;
	rlist_foreach_entry_safe(engine_rv, &rv->engines, link,
				 next_engine_rv) {
		engine_read_view_delete(engine_rv);
	}
	TRASH(rv);
}
