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
#include "schema.h"
#include "func.h"
#include "sequence.h"
#include "tuple.h"
#include "assoc.h"
#include "alter.h"
#include "scoped_guard.h"
#include "user.h"
#include <stdio.h>
/**
 * @module Data Dictionary
 *
 * The data dictionary is responsible for storage and caching
 * of system metadata, such as information about existing
 * spaces, indexes, tuple formats. Space and index metadata
 * is called in dedicated spaces, _space and _index respectively.
 * The contents of these spaces is fully cached in a cache of
 * struct space objects.
 *
 * struct space is an in-memory instance representing a single
 * space with its metadata, space data, and methods to manage
 * it.
 */

/** All existing spaces. */
static struct mh_i32ptr_t *spaces;
static struct mh_i32ptr_t *funcs;
static struct mh_strnptr_t *funcs_by_name;
static struct mh_i32ptr_t *sequences;
uint32_t schema_version = 0;

struct rlist on_alter_space = RLIST_HEAD_INITIALIZER(on_alter_space);
struct rlist on_alter_sequence = RLIST_HEAD_INITIALIZER(on_alter_sequence);

/**
 * Lock of scheme modification
 */
struct latch schema_lock = LATCH_INITIALIZER(schema_lock);

bool
space_is_system(struct space *space)
{
	return space->def->id > BOX_SYSTEM_ID_MIN &&
		space->def->id < BOX_SYSTEM_ID_MAX;
}

/** Return space by its number */
struct space *
space_by_id(uint32_t id)
{
	mh_int_t space = mh_i32ptr_find(spaces, id, NULL);
	if (space == mh_end(spaces))
		return NULL;
	return (struct space *) mh_i32ptr_node(spaces, space)->val;
}

/** Return current schema version */
uint32_t
box_schema_version()
{
	return schema_version;
}

/**
 * Visit all spaces and apply 'func'.
 */
int
space_foreach(int (*func)(struct space *sp, void *udata), void *udata)
{
	mh_int_t i;
	struct space *space;
	char key[6];
	assert(mp_sizeof_uint(BOX_SYSTEM_ID_MIN) <= sizeof(key));
	mp_encode_uint(key, BOX_SYSTEM_ID_MIN);

	/*
	 * Make sure we always visit system spaces first,
	 * in order from lowest space id to the highest..
	 * This is essential for correctly recovery from the
	 * snapshot, and harmless otherwise.
	 */
	space = space_by_id(BOX_SPACE_ID);
	struct index *pk = space ? space_index(space, 0) : NULL;
	if (pk) {
		struct iterator *it = index_create_iterator(pk, ITER_GE,
							    key, 1);
		if (it == NULL)
			return -1;
		int rc;
		struct tuple *tuple;
		while ((rc = iterator_next(it, &tuple)) == 0 && tuple != NULL) {
			uint32_t id;
			if (tuple_field_u32(tuple, BOX_SPACE_FIELD_ID, &id) != 0)
				continue;
			space = space_cache_find(id);
			if (space == NULL)
				continue;
			if (! space_is_system(space))
				break;
			rc = func(space, udata);
			if (rc != 0)
				break;
		}
		iterator_delete(it);
		if (rc != 0)
			return -1;
	}

	mh_foreach(spaces, i) {
		space = (struct space *) mh_i32ptr_node(spaces, i)->val;
		if (space_is_system(space))
			continue;
		if (func(space, udata) != 0)
			return -1;
	}

	return 0;
}

/** Delete a space from the space cache and Lua. */
struct space *
space_cache_delete(uint32_t id)
{
	mh_int_t k = mh_i32ptr_find(spaces, id, NULL);
	assert(k != mh_end(spaces));
	struct space *space = (struct space *)mh_i32ptr_node(spaces, k)->val;
	mh_i32ptr_del(spaces, k, NULL);
	schema_version++;
	return space;
}

/**
 * Update the space in the space cache and in Lua. Returns
 * the old space instance, if any, or NULL if it's a new space.
 */
struct space *
space_cache_replace(struct space *space)
{
	const struct mh_i32ptr_node_t node = { space_id(space), space };
	struct mh_i32ptr_node_t old, *p_old = &old;
	mh_int_t k = mh_i32ptr_put(spaces, &node, &p_old, NULL);
	if (k == mh_end(spaces)) {
		panic_syserror("Out of memory for the data "
			       "dictionary cache.");
	}
	schema_version++;
	return p_old ? (struct space *) p_old->val : NULL;
}

/** A wrapper around space_new() for data dictionary spaces. */
static void
sc_space_new(uint32_t id, const char *name, struct key_def *key_def,
	     struct trigger *replace_trigger,
	     struct trigger *stmt_begin_trigger)
{
	struct index_def *index_def = index_def_new(id, /* space id */
						    0 /* index id */,
						    "primary", /* name */
						    strlen("primary"),
						    TREE /* index type */,
						    &index_opts_default,
						    key_def, NULL);
	if (index_def == NULL)
		diag_raise();
	auto index_def_guard =
		make_scoped_guard([=] { index_def_delete(index_def); });
	struct space_def *def =
		space_def_new_xc(id, ADMIN, 0, name, strlen(name), "memtx",
				 strlen("memtx"), &space_opts_default, NULL, 0);
	auto def_guard = make_scoped_guard([=] { space_def_delete(def); });
	struct rlist key_list;
	rlist_create(&key_list);
	rlist_add_entry(&key_list, index_def, link);
	struct space *space = space_new_xc(def, &key_list);
	(void) space_cache_replace(space);
	if (replace_trigger)
		trigger_add(&space->on_replace, replace_trigger);
	if (stmt_begin_trigger)
		trigger_add(&space->on_stmt_begin, stmt_begin_trigger);
	/*
	 * Data dictionary spaces are fully built since:
	 * - they contain data right from the start
	 * - they are fully operable already during recovery
	 * - if there is a record in the snapshot which mandates
	 *   addition of a new index to a system space, this
	 *   index is built tuple-by-tuple, not in bulk, which
	 *   ensures validation of tuples when starting from
	 *   a snapshot of older version.
	 */
	init_system_space(space);

	trigger_run_xc(&on_alter_space, space);
}

uint32_t
schema_find_id(uint32_t system_space_id, uint32_t index_id,
	       const char *name, uint32_t len)
{
	if (len > BOX_NAME_MAX)
		return BOX_ID_NIL;
	struct space *space = space_cache_find_xc(system_space_id);
	struct index *index = index_find_system_xc(space, index_id);
	uint32_t size = mp_sizeof_str(len);
	struct region *region = &fiber()->gc;
	uint32_t used = region_used(region);
	char *key = (char *) region_alloc_xc(region, size);
	auto guard = make_scoped_guard([=] { region_truncate(region, used); });
	mp_encode_str(key, name, len);

	struct iterator *it = index_create_iterator_xc(index, ITER_EQ, key, 1);
	IteratorGuard iter_guard(it);

	struct tuple *tuple = iterator_next_xc(it);
	if (tuple) {
		/* id is always field #1 */
		return tuple_field_u32_xc(tuple, 0);
	}
	return BOX_ID_NIL;
}

/**
 * Initialize a prototype for the two mandatory data
 * dictionary spaces and create a cache entry for them.
 * When restoring data from the snapshot these spaces
 * will get altered automatically to their actual format.
 */
void
schema_init()
{
	/* Initialize the space cache. */
	spaces = mh_i32ptr_new();
	funcs = mh_i32ptr_new();
	funcs_by_name = mh_strnptr_new();
	sequences = mh_i32ptr_new();
	/*
	 * Create surrogate space objects for the mandatory system
	 * spaces (the primal eggs from which we get all the
	 * chicken). Their definitions will be overwritten by the
	 * data in the snapshot, and they will thus be
	 * *re-created* during recovery.  Note, the index type
	 * must be TREE and space identifiers must be the smallest
	 * one to ensure that these spaces are always recovered
	 * (and re-created) first.
	 */
	/* _schema - key/value space with schema description */
	struct key_def *key_def = key_def_new(1); /* part count */
	if (key_def == NULL)
		diag_raise();
	auto key_def_guard = make_scoped_guard([&] { key_def_delete(key_def); });

	key_def_set_part(key_def, 0 /* part no */, 0 /* field no */,
			 FIELD_TYPE_STRING, ON_CONFLICT_ACTION_ABORT, NULL);
	sc_space_new(BOX_SCHEMA_ID, "_schema", key_def, &on_replace_schema,
		     NULL);

	/* _space - home for all spaces. */
	key_def_set_part(key_def, 0 /* part no */, 0 /* field no */,
			 FIELD_TYPE_UNSIGNED, ON_CONFLICT_ACTION_ABORT, NULL);

	/* _collation - collation description. */
	sc_space_new(BOX_COLLATION_ID, "_collation", key_def,
		     &on_replace_collation, NULL);

	sc_space_new(BOX_SPACE_ID, "_space", key_def,
		     &alter_space_on_replace_space, &on_stmt_begin_space);

	/* _truncate - auxiliary space for triggering space truncation. */
	sc_space_new(BOX_TRUNCATE_ID, "_truncate", key_def,
		     &on_replace_truncate, &on_stmt_begin_truncate);

	/* _sequence - definition of all sequence objects. */
	sc_space_new(BOX_SEQUENCE_ID, "_sequence", key_def,
		     &on_replace_sequence, NULL);

	/* _sequence_data - current sequence value. */
	sc_space_new(BOX_SEQUENCE_DATA_ID, "_sequence_data", key_def,
		     &on_replace_sequence_data, NULL);

	/* _space_seq - association space <-> sequence. */
	sc_space_new(BOX_SPACE_SEQUENCE_ID, "_space_sequence", key_def,
		     &on_replace_space_sequence, NULL);

	/* _user - all existing users */
	sc_space_new(BOX_USER_ID, "_user", key_def, &on_replace_user, NULL);

	/* _func - all executable objects on which one can have grants */
	sc_space_new(BOX_FUNC_ID, "_func", key_def, &on_replace_func, NULL);
	/*
	 * _priv - association user <-> object
	 * The real index is defined in the snapshot.
	 */
	sc_space_new(BOX_PRIV_ID, "_priv", key_def, &on_replace_priv, NULL);
	/*
	 * _cluster - association instance uuid <-> instance id
	 * The real index is defined in the snapshot.
	 */
	sc_space_new(BOX_CLUSTER_ID, "_cluster", key_def, &on_replace_cluster,
		     NULL);

	/* _trigger - all existing SQL triggers. */
	key_def_set_part(key_def, 0 /* part no */, 0 /* field no */,
			 FIELD_TYPE_STRING, ON_CONFLICT_ACTION_ABORT, NULL);
	sc_space_new(BOX_TRIGGER_ID, "_trigger", key_def, &on_replace_trigger, NULL);

	key_def_delete(key_def);
	key_def = key_def_new(2); /* part count */
	if (key_def == NULL)
		diag_raise();
	/* space no */
	key_def_set_part(key_def, 0 /* part no */, 0 /* field no */,
			 FIELD_TYPE_UNSIGNED, ON_CONFLICT_ACTION_ABORT, NULL);
	/* index no */
	key_def_set_part(key_def, 1 /* part no */, 1 /* field no */,
			 FIELD_TYPE_UNSIGNED, ON_CONFLICT_ACTION_ABORT, NULL);
	sc_space_new(BOX_INDEX_ID, "_index", key_def,
		     &alter_space_on_replace_index, &on_stmt_begin_index);

	/* space name */
	key_def_set_part(key_def, 0 /* part no */, 0 /* field no */,
			 FIELD_TYPE_STRING, ON_CONFLICT_ACTION_ABORT, NULL);
	/* index name */
	key_def_set_part(key_def, 1 /* part no */, 1 /* field no */,
			 FIELD_TYPE_STRING, ON_CONFLICT_ACTION_ABORT, NULL);
	/* _sql_stat1 - a simpler statistics on space, seen in SQL. */
	sc_space_new(BOX_SQL_STAT1_ID, "_sql_stat1", key_def, NULL, NULL);

	free(key_def);
	key_def = key_def_new(3); /* part count */
	if (key_def == NULL)
		diag_raise();

	/* space name */
	key_def_set_part(key_def, 0 /* part no */, 0 /* field no */,
			 FIELD_TYPE_STRING, ON_CONFLICT_ACTION_ABORT, NULL);
	/* index name */
	key_def_set_part(key_def, 1 /* part no */, 1 /* field no */,
			 FIELD_TYPE_STRING, ON_CONFLICT_ACTION_ABORT, NULL);
	/* sample */
	key_def_set_part(key_def, 2 /* part no */, 5 /* field no */,
			 FIELD_TYPE_SCALAR, ON_CONFLICT_ACTION_ABORT, NULL);
	/* _sql_stat4 - extensive statistics on space, seen in SQL. */
	sc_space_new(BOX_SQL_STAT4_ID, "_sql_stat4", key_def, NULL, NULL);
}

void
schema_free(void)
{
	if (spaces == NULL)
		return;
	while (mh_size(spaces) > 0) {
		mh_int_t i = mh_first(spaces);

		struct space *space = (struct space *)
				mh_i32ptr_node(spaces, i)->val;
		space_cache_delete(space_id(space));
		space_delete(space);
	}
	mh_i32ptr_delete(spaces);
	while (mh_size(funcs) > 0) {
		mh_int_t i = mh_first(funcs);

		struct func *func = ((struct func *)
				     mh_i32ptr_node(funcs, i)->val);
		func_cache_delete(func->def->fid);
	}
	mh_i32ptr_delete(funcs);
	while (mh_size(sequences) > 0) {
		mh_int_t i = mh_first(sequences);

		struct sequence *seq = ((struct sequence *)
					mh_i32ptr_node(sequences, i)->val);
		sequence_cache_delete(seq->def->id);
	}
	mh_i32ptr_delete(sequences);
}

void
func_cache_replace(struct func_def *def)
{
	struct func *old = func_by_id(def->fid);
	if (old) {
		func_update(old, def);
		return;
	}
	if (mh_size(funcs) >= BOX_FUNCTION_MAX)
		tnt_raise(ClientError, ER_FUNCTION_MAX, BOX_FUNCTION_MAX);
	struct func *func = func_new(def);
	if (func == NULL) {
error:
		panic_syserror("Out of memory for the data "
			       "dictionary cache (stored function).");
	}
	const struct mh_i32ptr_node_t node = { def->fid, func };
	mh_int_t k1 = mh_i32ptr_put(funcs, &node, NULL, NULL);
	if (k1 == mh_end(funcs)) {
		func->def = NULL;
		func_delete(func);
		goto error;
	}
	size_t def_name_len = strlen(func->def->name);
	uint32_t name_hash = mh_strn_hash(func->def->name, def_name_len);
	const struct mh_strnptr_node_t strnode = {
		func->def->name, def_name_len, name_hash, func };

	mh_int_t k2 = mh_strnptr_put(funcs_by_name, &strnode, NULL, NULL);
	if (k2 == mh_end(funcs_by_name)) {
		mh_i32ptr_del(funcs, k1, NULL);
		func->def = NULL;
		func_delete(func);
		goto error;
	}
}

void
func_cache_delete(uint32_t fid)
{
	mh_int_t k = mh_i32ptr_find(funcs, fid, NULL);
	if (k == mh_end(funcs))
		return;
	struct func *func = (struct func *)
		mh_i32ptr_node(funcs, k)->val;
	mh_i32ptr_del(funcs, k, NULL);
	k = mh_strnptr_find_inp(funcs_by_name, func->def->name,
				strlen(func->def->name));
	if (k != mh_end(funcs))
		mh_strnptr_del(funcs_by_name, k, NULL);
	func_delete(func);
}

struct func *
func_by_id(uint32_t fid)
{
	mh_int_t func = mh_i32ptr_find(funcs, fid, NULL);
	if (func == mh_end(funcs))
		return NULL;
	return (struct func *) mh_i32ptr_node(funcs, func)->val;
}

struct func *
func_by_name(const char *name, uint32_t name_len)
{
	mh_int_t func = mh_strnptr_find_inp(funcs_by_name, name, name_len);
	if (func == mh_end(funcs_by_name))
		return NULL;
	return (struct func *) mh_strnptr_node(funcs_by_name, func)->val;
}

bool
schema_find_grants(const char *type, uint32_t id)
{
	struct space *priv = space_cache_find_xc(BOX_PRIV_ID);
	/** "object" index */
	struct index *index = index_find_system_xc(priv, 2);
	/*
	 * +10 = max(mp_sizeof_uint32) +
	 *       max(mp_sizeof_strl(uint32)).
	 */
	char key[GRANT_NAME_MAX + 10];
	assert(strlen(type) <= GRANT_NAME_MAX);
	mp_encode_uint(mp_encode_str(key, type, strlen(type)), id);
	struct iterator *it = index_create_iterator_xc(index, ITER_EQ, key, 2);
	IteratorGuard iter_guard(it);
	return iterator_next_xc(it);
}

struct sequence *
sequence_by_id(uint32_t id)
{
	mh_int_t k = mh_i32ptr_find(sequences, id, NULL);
	if (k == mh_end(sequences))
		return NULL;
	return (struct sequence *) mh_i32ptr_node(sequences, k)->val;
}

struct sequence *
sequence_cache_find(uint32_t id)
{
	struct sequence *seq = sequence_by_id(id);
	if (seq == NULL)
		tnt_raise(ClientError, ER_NO_SUCH_SEQUENCE, int2str(id));
	return seq;
}

void
sequence_cache_replace(struct sequence_def *def)
{
	struct sequence *seq = sequence_by_id(def->id);
	if (seq == NULL) {
		/* Create a new sequence. */
		seq = (struct sequence *) calloc(1, sizeof(*seq));
		if (seq == NULL)
			goto error;
		struct mh_i32ptr_node_t node = { def->id, seq };
		if (mh_i32ptr_put(sequences, &node, NULL, NULL) ==
		    mh_end(sequences))
			goto error;
	} else {
		/* Update an existing sequence. */
		free(seq->def);
	}
	seq->def = def;
	return;
error:
	panic_syserror("Out of memory for the data "
		       "dictionary cache (sequence).");
}

void
sequence_cache_delete(uint32_t id)
{
	struct sequence *seq = sequence_by_id(id);
	if (seq != NULL) {
		/* Delete sequence data. */
		sequence_reset(seq);
		mh_i32ptr_del(sequences, seq->def->id, NULL);
		free(seq->def);
		TRASH(seq);
		free(seq);
	}
}

const char *
schema_find_name(enum schema_object_type type, uint32_t object_id)
{
	switch (type) {
	case SC_UNIVERSE:
		return "";
	case SC_SPACE:
		{
			struct space *space = space_by_id(object_id);
			if (space == NULL)
				break;
			return space->def->name;
		}
	case SC_FUNCTION:
		{
			struct func *func = func_by_id(object_id);
			if (func == NULL)
				break;
			return func->def->name;
		}
	case SC_SEQUENCE:
		{
			struct sequence *seq = sequence_by_id(object_id);
			if (seq == NULL)
				break;
			return seq->def->name;
		}
	case SC_ROLE:
	case SC_USER:
		{
			struct user *role = user_by_id(object_id);
			if (role == NULL)
				break;
			return role->def->name;
		}
	default:
		break;
	}
	assert(false);
	return "(nil)";
}

