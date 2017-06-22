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
#include "user_def.h"
#include "engine.h"
#include "memtx_index.h"
#include "func.h"
#include "tuple.h"
#include "assoc.h"
#include "lua/utils.h"
#include "lua/space.h"
#include "key_def.h"
#include "alter.h"
#include "scoped_guard.h"
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
uint32_t schema_version = 0;
/**
 * Lock of scheme modification
 */
struct latch schema_lock = LATCH_INITIALIZER(schema_lock);

bool
space_is_system(struct space *space)
{
	return space->def.id > BOX_SYSTEM_ID_MIN &&
		space->def.id < BOX_SYSTEM_ID_MAX;
}

/** Return space by its number */
extern "C" struct space *
space_by_id(uint32_t id)
{
	mh_int_t space = mh_i32ptr_find(spaces, id, NULL);
	if (space == mh_end(spaces))
		return NULL;
	return (struct space *) mh_i32ptr_node(spaces, space)->val;
}

/** Return current schema version */
extern "C" uint32_t
box_schema_version()
{
	return schema_version;
}

/**
 * Visit all spaces and apply 'func'.
 */
void
space_foreach(void (*func)(struct space *sp, void *udata), void *udata)
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
	Index *pk = space ? space_index(space, 0) : NULL;
	if (pk) {
		struct iterator *it = pk->allocIterator();
		auto scoped_guard = make_scoped_guard([=] { it->free(it); });
		pk->initIterator(it, ITER_GE, key, 1);

		struct tuple *tuple;
		while ((tuple = it->next(it))) {
			/* Get space id, primary key, field 0. */
			uint32_t id =
				tuple_field_u32_xc(tuple, BOX_SPACE_FIELD_ID);
			space = space_cache_find(id);
			if (! space_is_system(space))
				break;
			func(space, udata);
		}
	}

	mh_foreach(spaces, i) {
		space = (struct space *) mh_i32ptr_node(spaces, i)->val;
		if (space_is_system(space))
			continue;
		func(space, udata);
	}
}

/** Delete a space from the space cache and Lua. */
struct space *
space_cache_delete(uint32_t id)
{
	if (tarantool_L)
		box_lua_space_delete(tarantool_L, id);
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
	/*
	 * Must be after the space is put into the hash, since
	 * box.schema.space.bless() uses hash look up to find the
	 * space and create userdata objects for space objects.
	 */
	box_lua_space_new(tarantool_L, space);
	return p_old ? (struct space *) p_old->val : NULL;
}

/** A wrapper around space_new() for data dictionary spaces. */
struct space *
sc_space_new(struct space_def *space_def,
	     struct index_def *index_def,
	     struct trigger *replace_trigger,
	     struct trigger *stmt_begin_trigger)
{
	struct rlist key_list;
	rlist_create(&key_list);
	rlist_add_entry(&key_list, index_def, link);
	struct space *space = space_new(space_def, &key_list);
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
	space->handler->initSystemSpace(space);
	return space;
}

uint32_t
schema_find_id(uint32_t system_space_id, uint32_t index_id,
	       const char *name, uint32_t len)
{
	struct space *space = space_cache_find(system_space_id);
	MemtxIndex *index = index_find_system(space, index_id);
	char buf[BOX_NAME_MAX * 2];
	/**
	 * This is an internal-only method, we should know the
	 * max length in advance.
	 */
	if (len + 5 > sizeof(buf))
		return BOX_ID_NIL;

	mp_encode_str(buf, name, len);

	struct iterator *it = index->position();
	index->initIterator(it, ITER_EQ, buf, 1);

	struct tuple *tuple = it->next(it);
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
	struct space_def def = {
		BOX_SCHEMA_ID, ADMIN, 0, "_schema", "memtx", space_opts_default
	};
	struct index_def *index_def = index_def_new(def.id,
						    "_schema",
						    0 /* index id */,
						    "primary", /* name */
						    strlen("primary"),
						    TREE /* index type */,
						    &index_opts_default,
						    1); /* part count */
	if (index_def == NULL)
		diag_raise();
	struct key_def *key_def = &index_def->key_def;
	key_def_set_part(key_def, 0 /* part no */, 0 /* field no */,
			 FIELD_TYPE_STRING);
	(void) sc_space_new(&def, index_def, &on_replace_schema, NULL);

	/* _space - home for all spaces. */
	index_def->space_id = def.id = BOX_SPACE_ID;
	snprintf(def.name, sizeof(def.name), "_space");
	key_def_set_part(key_def, 0 /* part no */, 0 /* field no */,
			 FIELD_TYPE_UNSIGNED);

	(void) sc_space_new(&def, index_def, &alter_space_on_replace_space,
			    &on_stmt_begin_space);

	/* _truncate - auxiliary space for triggering space truncation. */
	index_def->space_id = def.id = BOX_TRUNCATE_ID;
	snprintf(def.name, sizeof(def.name), "_truncate");
	(void) sc_space_new(&def, index_def, &on_replace_truncate, NULL);

	/* _user - all existing users */
	index_def->space_id = def.id = BOX_USER_ID;
	snprintf(def.name, sizeof(def.name), "_user");
	(void) sc_space_new(&def, index_def, &on_replace_user, NULL);

	/* _func - all executable objects on which one can have grants */
	index_def->space_id = def.id = BOX_FUNC_ID;
	snprintf(def.name, sizeof(def.name), "_func");
	(void) sc_space_new(&def, index_def, &on_replace_func, NULL);
	/*
	 * _priv - association user <-> object
	 * The real index is defined in the snapshot.
	 */
	index_def->space_id = def.id = BOX_PRIV_ID;
	snprintf(def.name, sizeof(def.name), "_priv");
	(void) sc_space_new(&def, index_def, &on_replace_priv, NULL);
	/*
	 * _cluster - association instance uuid <-> instance id
	 * The real index is defined in the snapshot.
	 */
	index_def->space_id = def.id = BOX_CLUSTER_ID;
	snprintf(def.name, sizeof(def.name), "_cluster");
	(void) sc_space_new(&def, index_def, &on_replace_cluster, NULL);
	index_def_delete(index_def);

	/* _index - definition of indexes in all spaces */
	def.id = BOX_INDEX_ID;
	snprintf(def.name, sizeof(def.name), "_index");
	index_def = index_def_new(def.id,
				  "_index",
				  0 /* index id */,
				  "primary",
				  strlen("primary"),
				  TREE /* index type */,
				  &index_opts_default,
				  2); /* part count */
	if (index_def == NULL)
		diag_raise();
	key_def = &index_def->key_def;
	/* space no */
	key_def_set_part(key_def, 0 /* part no */, 0 /* field no */,
			 FIELD_TYPE_UNSIGNED);
	/* index no */
	key_def_set_part(key_def, 1 /* part no */, 1 /* field no */,
			 FIELD_TYPE_UNSIGNED);
	(void) sc_space_new(&def, index_def, &alter_space_on_replace_index,
			    &on_stmt_begin_index);
	index_def_delete(index_def);
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
	struct space *priv = space_cache_find(BOX_PRIV_ID);
	/** "object" index */
	MemtxIndex *index = index_find_system(priv, 2);
	struct iterator *it = index->position();
	char key[10 + BOX_NAME_MAX];
	mp_encode_uint(mp_encode_str(key, type, strlen(type)), id);
	index->initIterator(it, ITER_EQ, key, 2);
	return it->next(it);
}
