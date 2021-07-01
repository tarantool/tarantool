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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "bit/bit.h"
#include "tuple_format.h"
#include "trigger.h"
#include "user.h"
#include "session.h"
#include "txn.h"
#include "memtx_tx.h"
#include "tuple.h"
#include "xrow_update.h"
#include "request.h"
#include "xrow.h"
#include "iproto_constants.h"
#include "schema.h"
#include "ck_constraint.h"
#include "assoc.h"
#include "constraint_id.h"

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
	/*
	 * Similarly to global access, subtract entity-level access
	 * (access to all spaces) if it is present.
	 */
	space_access &= ~entity_access_get(SC_SPACE)[cr->auth_token].effective;

	if (space_access &&
	    /* Check for missing USAGE access, ignore owner rights. */
	    (space_access & PRIV_U ||
	     /* Check for missing specific access, respect owner rights. */
	    (space->def->uid != cr->uid &&
	     space_access & ~space->access[cr->auth_token].effective))) {
		/*
		 * Report access violation. Throw "no such user"
		 * error if there is no user with this id.
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
			index->dense_id = index_count;
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
	rlist_create(&space->before_replace);
	rlist_create(&space->on_replace);
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
	size_t size = bitmap_size(index_id_max + 1);
	space->check_unique_constraint_map = calloc(size, 1);
	if (space->check_unique_constraint_map == NULL) {
		diag_set(OutOfMemory, size, "malloc",
			 "check_unique_constraint_map");
		goto fail;
	}
	rlist_foreach_entry(index_def, key_list, link) {
		struct index *index = space_create_index(space, index_def);
		if (index == NULL)
			goto fail_free_indexes;
		space->index_map[index_def->iid] = index;
		if (index_def->opts.is_unique)
			bit_set(space->check_unique_constraint_map,
				index_def->iid);
	}
	space_fill_index_map(space);
	rlist_create(&space->parent_fk_constraint);
	rlist_create(&space->child_fk_constraint);
	rlist_create(&space->ck_constraint);

	/*
	 * Check if there are unique indexes that are contained
	 * by other unique indexes. For them, we can skip check
	 * for duplicates on INSERT. Prefer indexes with higher
	 * ids for uniqueness check optimization as they are
	 * likelier to have a "colder" cache.
	 */
	for (int i = space->index_count - 1; i >= 0; i--) {
		struct index *index = space->index[i];
		if (!bit_test(space->check_unique_constraint_map,
			      index->def->iid))
			continue;
		for (int j = 0; j < (int)space->index_count; j++) {
			struct index *other = space->index[j];
			if (i != j && bit_test(space->check_unique_constraint_map,
					       other->def->iid) &&
			    key_def_contains(index->def->key_def,
					     other->def->key_def)) {
				bit_clear(space->check_unique_constraint_map,
					  index->def->iid);
				break;
			}
		}
	}
	space->constraint_ids = mh_strnptr_new();
	if (space->constraint_ids == NULL) {
		diag_set(OutOfMemory, sizeof(*space->constraint_ids), "malloc",
			 "constraint_ids");
		goto fail;
	}
	rlist_create(&space->memtx_stories);
	return 0;

fail_free_indexes:
	for (uint32_t i = 0; i <= index_id_max; i++) {
		struct index *index = space->index_map[i];
		if (index != NULL)
			index_unref(index);
	}
fail:
	free(space->index_map);
	free(space->check_unique_constraint_map);
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

struct space *
space_new_ephemeral(struct space_def *def, struct rlist *key_list)
{
	assert(def->opts.is_temporary);
	assert(def->opts.is_ephemeral);
	struct space *space = space_new(def, key_list);
	if (space == NULL)
		return NULL;
	space->vtab->init_ephemeral_space(space);
	return space;
}

void
space_delete(struct space *space)
{
	memtx_tx_on_space_delete(space);
	assert(space->ck_constraint_trigger == NULL);
	for (uint32_t j = 0; j <= space->index_id_max; j++) {
		struct index *index = space->index_map[j];
		if (index != NULL)
			index_unref(index);
	}
	free(space->index_map);
	free(space->check_unique_constraint_map);
	if (space->format != NULL)
		tuple_format_unref(space->format);
	trigger_destroy(&space->before_replace);
	trigger_destroy(&space->on_replace);
	space_def_delete(space->def);
	/*
	 * SQL triggers and constraints should be deleted with
	 * on_replace_dd_ triggers on deletion from corresponding
	 * system space.
	 */
	assert(mh_size(space->constraint_ids) == 0);
	mh_strnptr_delete(space->constraint_ids);
	assert(space->sql_triggers == NULL);
	assert(rlist_empty(&space->parent_fk_constraint));
	assert(rlist_empty(&space->child_fk_constraint));
	assert(rlist_empty(&space->ck_constraint));
	space->vtab->destroy(space);
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
generic_space_swap_index(struct space *old_space, struct space *new_space,
			 uint32_t old_index_id, uint32_t new_index_id)
{
	SWAP(old_space->index_map[old_index_id],
	     new_space->index_map[new_index_id]);
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

/**
 * Run BEFORE triggers registered for a space. If a trigger
 * changes the current statement, this function updates the
 * request accordingly.
 */
static int
space_before_replace(struct space *space, struct txn *txn,
		     struct request *request)
{
	enum iproto_type type = request->type;
	struct index *pk = space_index(space, 0);

	const char *key = NULL;
	uint32_t part_count = 0;
	struct index *index = NULL;
	struct tuple *old_tuple = NULL;

	/*
	 * Lookup the old tuple.
	 */
	switch (type) {
	case IPROTO_UPDATE:
	case IPROTO_DELETE:
		index = index_find_unique(space, request->index_id);
		if (index == NULL)
			return -1;
		key = request->key;
		part_count = mp_decode_array(&key);
		break;
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
	case IPROTO_UPSERT:
		if (pk == NULL)
			goto after_old_tuple_lookup;
		index = pk;
		key = tuple_extract_key_raw(request->tuple, request->tuple_end,
					    index->def->key_def, MULTIKEY_NONE,
					    NULL);
		if (key == NULL)
			return -1;
		part_count = mp_decode_array(&key);
		break;
	default:
		/* Unknown request type, nothing to do. */
		return 0;
	}

	if (exact_key_validate(index->def->key_def, key, part_count) != 0)
		return -1;

	if (index_get(index, key, part_count, &old_tuple) != 0)
		return -1;

after_old_tuple_lookup:;

	/*
	 * Create the new tuple.
	 */
	uint32_t new_size, old_size;
	const char *new_data, *new_data_end;
	const char *old_data, *old_data_end;

	switch (request->type) {
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		new_data = request->tuple;
		new_data_end = request->tuple_end;
		break;
	case IPROTO_UPDATE:
		if (old_tuple == NULL) {
			/* Nothing to update. */
			return 0;
		}
		old_data = tuple_data_range(old_tuple, &old_size);
		old_data_end = old_data + old_size;
		new_data = xrow_update_execute(request->tuple,
					       request->tuple_end, old_data,
					       old_data_end,
					       space->format, &new_size,
					       request->index_base, NULL);
		if (new_data == NULL)
			return -1;
		new_data_end = new_data + new_size;
		break;
	case IPROTO_DELETE:
		if (old_tuple == NULL) {
			/* Nothing to delete. */
			return 0;
		}
		new_data = new_data_end = NULL;
		break;
	case IPROTO_UPSERT:
		if (old_tuple == NULL) {
			/*
			 * Turn UPSERT into INSERT, but still check
			 * provided operations.
			 */
			new_data = request->tuple;
			new_data_end = request->tuple_end;
			if (xrow_update_check_ops(request->ops,
						  request->ops_end,
						  space->format,
						  request->index_base) != 0)
				return -1;
			break;
		}
		old_data = tuple_data_range(old_tuple, &old_size);
		old_data_end = old_data + old_size;
		new_data = xrow_upsert_execute(request->ops, request->ops_end,
					       old_data, old_data_end,
					       space->format, &new_size,
					       request->index_base, false,
					       NULL);
		new_data_end = new_data + new_size;
		break;
	default:
		unreachable();
	}

	struct tuple *new_tuple = NULL;
	if (new_data != NULL) {
		new_tuple = tuple_new(tuple_format_runtime,
				      new_data, new_data_end);
		if (new_tuple == NULL)
			return -1;
		tuple_ref(new_tuple);
	}

	assert(old_tuple != NULL || new_tuple != NULL);

	/*
	 * Execute all registered BEFORE triggers.
	 *
	 * We pass the old and new tuples to the triggers in
	 * txn_current_stmt(), which should be empty, because
	 * the engine method (execute_replace or similar) has
	 * not been called yet. Triggers may update new_tuple
	 * in place so the next trigger sees the result of the
	 * previous one. After we are done, we clear old_tuple
	 * and new_tuple in txn_current_stmt() to be set by
	 * the engine.
	 */
	struct txn_stmt *stmt = txn_current_stmt(txn);
	assert(stmt->old_tuple == NULL && stmt->new_tuple == NULL);
	stmt->old_tuple = old_tuple;
	stmt->new_tuple = new_tuple;

	int rc = trigger_run(&space->before_replace, txn);

	/*
	 * BEFORE riggers cannot change the old tuple,
	 * but they may replace the new tuple.
	 */
	bool request_changed = (stmt->new_tuple != new_tuple);
	new_tuple = stmt->new_tuple;
	assert(stmt->old_tuple == old_tuple);
	stmt->old_tuple = NULL;
	stmt->new_tuple = NULL;

	if (rc != 0)
		goto out;

	/*
	 * We don't allow to change the value of the primary key
	 * in the same statement.
	 */
	if (pk != NULL && request_changed &&
	    old_tuple != NULL && new_tuple != NULL &&
	    tuple_compare(old_tuple, HINT_NONE, new_tuple, HINT_NONE,
			  pk->def->key_def) != 0) {
		diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			 pk->def->name, space->def->name);
		rc = -1;
		goto out;
	}

	/*
	 * BEFORE triggers changed the resulting tuple.
	 * Fix the request to conform.
	 */
	if (request_changed)
		rc = request_create_from_tuple(request, space,
					       old_tuple, new_tuple);
out:
	if (new_tuple != NULL)
		tuple_unref(new_tuple);
	return rc;
}

int
space_execute_dml(struct space *space, struct txn *txn,
		  struct request *request, struct tuple **result)
{
	if (unlikely(space->sequence != NULL) &&
	    (request->type == IPROTO_INSERT ||
	     request->type == IPROTO_REPLACE)) {
		/*
		 * The space has a sequence associated with it.
		 * If the tuple has 'nil' for the primary key,
		 * we should replace it with the next sequence
		 * value.
		 */
		if (request_handle_sequence(request, space) != 0)
			return -1;
	}

	if (unlikely(!rlist_empty(&space->before_replace) &&
		     space->run_triggers)) {
		/*
		 * Call BEFORE triggers if any before dispatching
		 * the request. Note, it may change the request
		 * type and arguments.
		 */
		if (space_before_replace(space, txn, request) != 0)
			return -1;
	}

	switch (request->type) {
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
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

int
space_add_ck_constraint(struct space *space, struct ck_constraint *ck)
{
	rlist_add_entry(&space->ck_constraint, ck, link);
	if (space->ck_constraint_trigger == NULL) {
		struct trigger *ck_trigger =
			(struct trigger *) malloc(sizeof(*ck_trigger));
		if (ck_trigger == NULL) {
			diag_set(OutOfMemory, sizeof(*ck_trigger), "malloc",
				 "ck_trigger");
			return -1;
		}
		trigger_create(ck_trigger, ck_constraint_on_replace_trigger,
			       NULL, (trigger_f0) free);
		trigger_add(&space->on_replace, ck_trigger);
		space->ck_constraint_trigger = ck_trigger;
	}
	return 0;
}

void
space_remove_ck_constraint(struct space *space, struct ck_constraint *ck)
{
	rlist_del_entry(ck, link);
	if (rlist_empty(&space->ck_constraint)) {
		struct trigger *ck_trigger = space->ck_constraint_trigger;
		trigger_clear(ck_trigger);
		ck_trigger->destroy(ck_trigger);
		space->ck_constraint_trigger = NULL;
	}
}

struct constraint_id *
space_find_constraint_id(struct space *space, const char *name)
{
	struct mh_strnptr_t *ids = space->constraint_ids;
	uint32_t len = strlen(name);
	mh_int_t pos = mh_strnptr_find_inp(ids, name, len);
	if (pos == mh_end(ids))
		return NULL;
	return (struct constraint_id *) mh_strnptr_node(ids, pos)->val;
}

int
space_add_constraint_id(struct space *space, struct constraint_id *id)
{
	assert(space_find_constraint_id(space, id->name) == NULL);
	struct mh_strnptr_t *ids = space->constraint_ids;
	uint32_t len = strlen(id->name);
	uint32_t hash = mh_strn_hash(id->name, len);
	const struct mh_strnptr_node_t name_node = {id->name, len, hash, id};
	if (mh_strnptr_put(ids, &name_node, NULL, NULL) == mh_end(ids)) {
		diag_set(OutOfMemory, sizeof(name_node), "malloc", "node");
		return -1;
	}
	return 0;
}

struct constraint_id *
space_pop_constraint_id(struct space *space, const char *name)
{
	struct mh_strnptr_t *ids = space->constraint_ids;
	uint32_t len = strlen(name);
	mh_int_t pos = mh_strnptr_find_inp(ids, name, len);
	assert(pos != mh_end(ids));
	struct constraint_id *id = (struct constraint_id *)
		mh_strnptr_node(ids, pos)->val;
	mh_strnptr_del(ids, pos, NULL);
	return id;
}

/* {{{ Virtual method stubs */

size_t
generic_space_bsize(struct space *space)
{
	(void)space;
	return 0;
}

int
generic_space_ephemeral_replace(struct space *space, const char *tuple,
				const char *tuple_end)
{
	(void)space;
	(void)tuple;
	(void)tuple_end;
	unreachable();
	return -1;
}

int
generic_space_ephemeral_delete(struct space *space, const char *key)
{
	(void)space;
	(void)key;
	unreachable();
	return -1;
}

int
generic_space_ephemeral_rowid_next(struct space *space, uint64_t *rowid)
{
	(void)space;
	(void)rowid;
	unreachable();
	return 0;
}

void
generic_init_system_space(struct space *space)
{
	(void)space;
}

void
generic_init_ephemeral_space(struct space *space)
{
	(void)space;
}

int
generic_space_check_index_def(struct space *space, struct index_def *index_def)
{
	(void)space;
	(void)index_def;
	return 0;
}

int
generic_space_add_primary_key(struct space *space)
{
	(void)space;
	return 0;
}

void
generic_space_drop_primary_key(struct space *space)
{
	(void)space;
}

int
generic_space_check_format(struct space *space, struct tuple_format *format)
{
	(void)space;
	(void)format;
	return 0;
}

int
generic_space_build_index(struct space *src_space, struct index *new_index,
			  struct tuple_format *new_format,
			  bool check_unique_constraint)
{
	(void)src_space;
	(void)new_index;
	(void)new_format;
	(void)check_unique_constraint;
	return 0;
}

int
generic_space_prepare_alter(struct space *old_space, struct space *new_space)
{
	(void)old_space;
	(void)new_space;
	return 0;
}

void
generic_space_invalidate(struct space *space)
{
	(void)space;
}

/* }}} */
