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
#include "tuple_format.h"
#include "trigger.h"
#include "user.h"
#include "session.h"
#include "request.h"
#include "xrow.h"
#include "iproto_constants.h"

int
access_check_space(struct space *space, user_access_t access)
{
	struct credentials *cr = effective_user();
	/* Any space access also requires global USAGE privilege. */
	access |= PRIV_U;
	/*
	 * If a user has a global permission, clear the respective
	 * privilege from the list of privileges required
	 * to execute the request.
	 * No special check for ADMIN user is necessary
	 * since ADMIN has universal access.
	 */
	user_access_t space_access = access & ~cr->universal_access;

	if (space_access && space->def->uid != cr->uid &&
	    space_access & ~space->access[cr->auth_token].effective) {
		/*
		 * Report access violation. Throw "no such user"
		 * error if there is  no user with this id.
		 * It is possible that the user was dropped
		 * from a different connection.
		 */
		struct user *user = user_find(cr->uid);
		if (user != NULL) {
			if (!(cr->universal_access & PRIV_U)) {
				diag_set(AccessDeniedError,
					 priv_name(PRIV_U),
					 schema_object_name(SC_UNIVERSE), "",
					 user->def->name);
			} else {
				diag_set(AccessDeniedError,
					 priv_name(access),
					 schema_object_name(SC_SPACE),
					 space->def->name, user->def->name);
			}
		}
		return -1;
	}
	return 0;
}

void
space_fill_index_map(struct space *space)
{
	uint32_t index_count = 0;
	for (uint32_t j = 0; j <= space->index_id_max; j++) {
		struct index *index = space->index_map[j];
		if (index) {
			assert(index_count < space->index_count);
			space->index[index_count++] = index;
		}
	}
}

int
space_create(struct space *space, struct engine *engine,
	     const struct space_vtab *vtab, struct space_def *def,
	     struct rlist *key_list, struct tuple_format *format)
{
	if (!rlist_empty(key_list)) {
		/* Primary key must go first. */
		struct index_def *pk = rlist_first_entry(key_list,
					struct index_def, link);
		assert(pk->iid == 0);
		(void)pk;
	}

	uint32_t index_id_max = 0;
	uint32_t index_count = 0;
	struct index_def *index_def;
	rlist_foreach_entry(index_def, key_list, link) {
		index_count++;
		index_id_max = MAX(index_id_max, index_def->iid);
	}

	memset(space, 0, sizeof(*space));
	space->vtab = vtab;
	space->engine = engine;
	space->index_count = index_count;
	space->index_id_max = index_id_max;
	rlist_create(&space->on_replace);
	rlist_create(&space->on_stmt_begin);
	space->run_triggers = true;

	space->format = format;
	if (format != NULL)
		tuple_format_ref(format);

	space->def = space_def_dup(def);
	if (space->def == NULL)
		goto fail;

	/* Create indexes and fill the index map. */
	space->index_map = (struct index **)
		calloc(index_count + index_id_max + 1, sizeof(struct index *));
	if (space->index_map == NULL) {
		diag_set(OutOfMemory, (index_count + index_id_max + 1) *
			 sizeof(struct index *), "malloc", "index_map");
		goto fail;
	}
	space->index = space->index_map + index_id_max + 1;
	rlist_foreach_entry(index_def, key_list, link) {
		struct index *index = space_create_index(space, index_def);
		if (index == NULL)
			goto fail_free_indexes;
		space->index_map[index_def->iid] = index;
	}
	space_fill_index_map(space);
	return 0;

fail_free_indexes:
	for (uint32_t i = 0; i <= index_id_max; i++) {
		struct index *index = space->index_map[i];
		if (index != NULL)
			index_delete(index);
	}
fail:
	free(space->index_map);
	if (space->def != NULL)
		space_def_delete(space->def);
	if (space->format != NULL)
		tuple_format_unref(space->format);
	return -1;
}

struct space *
space_new(struct space_def *def, struct rlist *key_list)
{
	struct engine *engine = engine_find(def->engine_name);
	if (engine == NULL)
		return NULL;
	return engine_create_space(engine, def, key_list);
}

void
space_delete(struct space *space)
{
	for (uint32_t j = 0; j <= space->index_id_max; j++) {
		struct index *index = space->index_map[j];
		if (index != NULL)
			index_delete(index);
	}
	free(space->index_map);
	if (space->format != NULL)
		tuple_format_unref(space->format);
	trigger_destroy(&space->on_replace);
	trigger_destroy(&space->on_stmt_begin);
	space_def_delete(space->def);
	space->vtab->destroy(space);
}

/** Do nothing if the space is already recovered. */
void
space_noop(struct space *space)
{
	(void)space;
}

void
space_dump_def(const struct space *space, struct rlist *key_list)
{
	rlist_create(key_list);

	/** Ensure the primary key is added first. */
	for (unsigned j = 0; j < space->index_count; j++)
		rlist_add_tail_entry(key_list, space->index[j]->def, link);
}

struct key_def *
space_index_key_def(struct space *space, uint32_t id)
{
	if (id <= space->index_id_max && space->index_map[id])
		return space->index_map[id]->def->key_def;
	return NULL;
}

void
space_swap_index(struct space *lhs, struct space *rhs,
		 uint32_t lhs_id, uint32_t rhs_id)
{
	struct index *tmp = lhs->index_map[lhs_id];
	lhs->index_map[lhs_id] = rhs->index_map[rhs_id];
	rhs->index_map[rhs_id] = tmp;
}

void
space_run_triggers(struct space *space, bool yesno)
{
	space->run_triggers = yesno;
}

size_t
space_bsize(struct space *space)
{
	return space->vtab->bsize(space);
}

struct index_def *
space_index_def(struct space *space, int n)
{
	return space->index[n]->def;
}

const char *
index_name_by_id(struct space *space, uint32_t id)
{
	struct index *index = space_index(space, id);
	if (index != NULL)
		return index->def->name;
	return NULL;
}

int
space_def_check_compatibility(const struct space_def *old_def,
			      const struct space_def *new_def,
			      bool is_space_empty)
{
	if (strcmp(new_def->engine_name, old_def->engine_name) != 0) {
		diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
			 "can not change space engine");
		return -1;
	}
	if (new_def->id != old_def->id) {
		diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
			 "space id is immutable");
		return -1;
	}
	if (is_space_empty)
		return 0;

	if (new_def->exact_field_count != 0 &&
	    new_def->exact_field_count != old_def->exact_field_count) {
		diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
			 "can not change field count on a non-empty space");
		return -1;
	}
	if (new_def->opts.temporary != old_def->opts.temporary) {
		diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
			 "can not switch temporary flag on a non-empty space");
		return -1;
	}
	uint32_t field_count = MIN(new_def->field_count, old_def->field_count);
	for (uint32_t i = 0; i < field_count; ++i) {
		enum field_type old_type = old_def->fields[i].type;
		enum field_type new_type = new_def->fields[i].type;
		if (! field_type_is_compatible(old_type, new_type)) {
			const char *msg =
				tt_sprintf("Can not change a field type from "\
					   "%s to %s on a not empty space",
					   field_type_strs[old_type],
					   field_type_strs[new_type]);
			diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
				 msg);
			return -1;
		}
		if (old_def->fields[i].is_nullable &&
		    !new_def->fields[i].is_nullable) {
			const char *msg =
				tt_sprintf("Can not disable is_nullable "\
					   "on a not empty space");
			diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
				 msg);
			return -1;
		}
	}
	return 0;
}

int
space_execute_dml(struct space *space, struct txn *txn,
		  struct request *request, struct tuple **result)
{
	switch (request->type) {
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		if (space->sequence != NULL &&
		    request_handle_sequence(request, space) != 0)
			return -1;
		if (space->vtab->execute_replace(space, txn,
						 request, result) != 0)
			return -1;
		break;
	case IPROTO_UPDATE:
		if (space->vtab->execute_update(space, txn,
						request, result) != 0)
			return -1;
		if (*result != NULL && request->index_id != 0) {
			/*
			 * XXX: this is going to break with sync replication
			 * for cases when tuple is NULL, since the leader
			 * will be unable to certify such updates correctly.
			 */
			request_rebind_to_primary_key(request, space, *result);
		}
		break;
	case IPROTO_DELETE:
		if (space->vtab->execute_delete(space, txn,
						request, result) != 0)
			return -1;
		if (*result != NULL && request->index_id != 0)
			request_rebind_to_primary_key(request, space, *result);
		break;
	case IPROTO_UPSERT:
		*result = NULL;
		if (space->vtab->execute_upsert(space, txn, request) != 0)
			return -1;
		break;
	default:
		*result = NULL;
	}
	return 0;
}
