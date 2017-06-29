/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "space.h"
#include <stdlib.h>
#include <string.h>
#include "tuple.h"
#include "tuple_format.h"
#include "tuple_compare.h"
#include "scoped_guard.h"
#include "trigger.h"
#include "user.h"
#include "session.h"

void
access_check_space(struct space *space, uint8_t access)
{
	struct credentials *cr = current_user();
	/*
	 * If a user has a global permission, clear the respective
	 * privilege from the list of privileges required
	 * to execute the request.
	 * No special check for ADMIN user is necessary
	 * since ADMIN has universal access.
	 */
	access &= ~cr->universal_access;
	if (access && space->def.uid != cr->uid &&
	    access & ~space->access[cr->auth_token].effective) {
		/*
		 * Report access violation. Throw "no such user"
		 * error if there is  no user with this id.
		 * It is possible that the user was dropped
		 * from a different connection.
		 */
		struct user *user = user_find_xc(cr->uid);
		tnt_raise(ClientError, ER_SPACE_ACCESS_DENIED,
			  priv_name(access), user->def->name, space->def.name);
	}
}

void
space_fill_index_map(struct space *space)
{
	uint32_t index_count = 0;
	for (uint32_t j = 0; j <= space->index_id_max; j++) {
		Index *index = space->index_map[j];
		if (index) {
			assert(index_count < space->index_count);
			space->index[index_count++] = index;
		}
	}
}

struct space *
space_new(struct space_def *def, struct rlist *key_list)
{
	uint32_t index_id_max = 0;
	uint32_t index_count = 0;
	struct index_def *index_def;
	rlist_foreach_entry(index_def, key_list, link) {
		index_count++;
		index_id_max = MAX(index_id_max, index_def->iid);
	}
	size_t sz = sizeof(struct space) +
		(index_count + index_id_max + 1) * sizeof(Index *);
	struct space *space = (struct space *) calloc(1, sz);

	if (space == NULL)
		tnt_raise(OutOfMemory, sz, "malloc", "struct space");

	space->index_count = index_count;
	space->index_id_max = index_id_max;
	rlist_create(&space->on_replace);
	rlist_create(&space->on_stmt_begin);
	auto scoped_guard = make_scoped_guard([=] { space_delete(space); });

	space->index_map = (Index **)((char *) space + sizeof(*space) +
				      index_count * sizeof(Index *));
	space->def = *def;
	Engine *engine = engine_find(def->engine_name);
	struct key_def **keys;
	keys = (struct key_def **)region_alloc_xc(&fiber()->gc,
						  sizeof(*keys) * index_count);
	uint32_t key_no = 0;
	rlist_foreach_entry(index_def, key_list, link) {
		keys[key_no++] = &index_def->key_def;
	}
	space->format = tuple_format_new(engine->format, keys, index_count, 0);
	if (space->format == NULL)
		diag_raise();
	tuple_format_ref(space->format, 1);
	space->format->exact_field_count = def->exact_field_count;
	/* init space engine instance */
	space->handler = engine->open();
	/* Fill the space indexes. */
	rlist_foreach_entry(index_def, key_list, link) {
		space->index_map[index_def->iid] =
			space->handler->createIndex(space, index_def);
	}
	space_fill_index_map(space);
	space->run_triggers = true;
	scoped_guard.is_active = false;
	return space;
}

void
space_delete(struct space *space)
{
	for (uint32_t j = 0; j <= space->index_id_max; j++) {
		Index *index = space->index_map[j];
		if (index)
			delete index;
	}
	if (space->format)
		tuple_format_ref(space->format, -1);
	if (space->handler)
		delete space->handler;

	trigger_destroy(&space->on_replace);
	trigger_destroy(&space->on_stmt_begin);
	free(space);
}

/** Do nothing if the space is already recovered. */
void
space_noop(struct space * /* space */)
{}

uint32_t
space_size(struct space *space)
{
	return space_index(space, 0)->size();
}

void
space_dump_def(const struct space *space, struct rlist *key_list)
{
	rlist_create(key_list);

	for (unsigned j = 0; j < space->index_count; j++)
		rlist_add_tail_entry(key_list, space->index[j]->index_def,
				     link);
}

void
space_swap_index(struct space *lhs, struct space *rhs,
		 uint32_t lhs_id, uint32_t rhs_id)
{
	Index *tmp = lhs->index_map[lhs_id];
	lhs->index_map[lhs_id] = rhs->index_map[rhs_id];
	rhs->index_map[rhs_id] = tmp;
}

extern "C" void
space_run_triggers(struct space *space, bool yesno)
{
	space->run_triggers = yesno;
}

ptrdiff_t
space_bsize_update(struct space *space,
		   const struct tuple *old_tuple,
		   const struct tuple *new_tuple)
{
	ptrdiff_t old_bsize = old_tuple ? box_tuple_bsize(old_tuple) : 0,
	          new_bsize = new_tuple ? box_tuple_bsize(new_tuple) : 0;
	assert((ptrdiff_t)space->bsize >= old_bsize);
	space->bsize += new_bsize - old_bsize;
	return new_bsize - old_bsize;
}

void
space_bsize_rollback(struct space *space, ptrdiff_t bsize_change)
{
	assert(bsize_change < 0 || (ptrdiff_t)space->bsize >= bsize_change);
	space->bsize -= bsize_change;
}

size_t
space_bsize(struct space *space)
{
	return space->bsize;
}

struct index_def *
space_index_def(struct space *space, int n)
{
	return space->index[n]->index_def;
}

const char *
index_name_by_id(struct space *space, uint32_t id)
{
	struct Index *index = space_index(space, id);
	if (index != NULL)
		return index->index_def->name;
	return NULL;
}

/* vim: set fm=marker */
