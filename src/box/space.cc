/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "scoped_guard.h"
#include "trigger.h"
#include "user_def.h"
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
			  priv_name(access), user->def.name, space->def.name);
	}
}


void
space_fill_index_map(struct space *space)
{
	space->index_count = 0;
	for (uint32_t j = 0; j <= space->index_id_max; j++) {
		Index *index = space->index_map[j];
		if (index)
			space->index[space->index_count++] = index;
	}
}

struct space *
space_new(struct space_def *def, struct rlist *key_list)
{
	uint32_t index_id_max = 0;
	uint32_t index_count = 0;
	/**
	 * UPSERT can't run in presence of unique
	 * secondary keys, since they would be impossible
	 * to check at recovery. MemTX recovers from
	 * the binary log with no secondary keys, and does
	 * not validate them, it assumes that the binary
	 * log has no records which validate secondary
	 * unique index constraint.
	 */
	bool has_unique_secondary_key = false;
	struct key_def *key_def;
	rlist_foreach_entry(key_def, key_list, link) {
		index_count++;
		if (key_def->iid > 0 && key_def->opts.is_unique == true)
			has_unique_secondary_key = true;
		index_id_max = MAX(index_id_max, key_def->iid);
	}
	size_t sz = sizeof(struct space) +
		(index_count + index_id_max + 1) * sizeof(Index *);
	struct space *space = (struct space *) calloc(1, sz);

	if (space == NULL)
		tnt_raise(LoggedError, ER_MEMORY_ISSUE,
			  sz, "struct space", "malloc");

	rlist_create(&space->on_replace);
	auto scoped_guard = make_scoped_guard([=]
	{
		/** Ensure space_delete deletes all indexes. */
		space_fill_index_map(space);
		space_delete(space);
	});

	space->index_map = (Index **)((char *) space + sizeof(*space) +
				      index_count * sizeof(Index *));
	space->def = *def;
	space->format = tuple_format_new(key_list);
	space->has_unique_secondary_key = has_unique_secondary_key;
	tuple_format_ref(space->format, 1);
	space->index_id_max = index_id_max;
	/* init space engine instance */
	Engine *engine = engine_find(def->engine_name);
	space->handler = engine->open();
	/* fill space indexes */
	rlist_foreach_entry(key_def, key_list, link) {
		space->index_map[key_def->iid] =
			space->handler->engine->createIndex(key_def);
	}
	space_fill_index_map(space);
	space->run_triggers = true;
	scoped_guard.is_active = false;
	return space;
}

void
space_delete(struct space *space)
{
	for (uint32_t j = 0; j < space->index_count; j++)
		delete space->index[j];
	if (space->format)
		tuple_format_ref(space->format, -1);
	if (space->handler)
		delete space->handler;

	trigger_destroy(&space->on_replace);
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

static inline void
space_validate_field_count(struct space *sp, uint32_t field_count)
{
	if (sp->def.field_count > 0 && sp->def.field_count != field_count)
		tnt_raise(ClientError, ER_SPACE_FIELD_COUNT,
		          field_count, space_name(sp), sp->def.field_count);
}

void
space_validate_tuple_raw(struct space *sp, const char *data)
{
	uint32_t field_count = mp_decode_array(&data);
	space_validate_field_count(sp, field_count);
}

void
space_validate_tuple(struct space *sp, struct tuple *new_tuple)
{
	uint32_t field_count = tuple_field_count(new_tuple);
	space_validate_field_count(sp, field_count);
}

void
space_dump_def(const struct space *space, struct rlist *key_list)
{
	rlist_create(key_list);

	for (int j = 0; j < space->index_count; j++)
		rlist_add_tail_entry(key_list, space->index[j]->key_def,
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

struct space_stat *
space_stat(struct space *sp)
{
	static __thread struct space_stat space_stat;

	space_stat.id = space_id(sp);
	int i = 0;
	for (; i < sp->index_id_max; i++) {
		Index *index = space_index(sp, i);
		if (index) {
			space_stat.index[i].id      = i;
			space_stat.index[i].keys    = index->size();
			space_stat.index[i].bsize   = index->bsize();
		} else
			space_stat.index[i].id = -1;
	}
	space_stat.index[i].id = -1;
	return &space_stat;
}

/**
 * We do not allow changes of the primary key during
 * update.
 * The syntax of update operation allows the user to primary
 * key of a tuple, which is prohibited, to avoid funny
 * effects during replication. Some engines can
 * track down this situation and abort the operation;
 * such engines (memtx) don't use this function.
 * Other engines can't do it, so they ask the server to
 * verify that the primary key of the tuple has not changed.
 */
void
space_check_update(struct space *space,
		   struct tuple *old_tuple,
		   struct tuple *new_tuple)
{
	assert(space->index_count > 0);
	Index *index = space->index[0];
	if (tuple_compare(old_tuple, new_tuple, index->key_def))
		tnt_raise(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			  index_name(index), space_name(space));
}

/* vim: set fm=marker */
