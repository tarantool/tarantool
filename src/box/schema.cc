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
#include "vclock/vclock.h"
#include "fiber.h"
#include "memtx_tx.h"
#include "txn.h"

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
static struct mh_strnptr_t *spaces_by_name;
static struct mh_i32ptr_t *funcs;
static struct mh_strnptr_t *funcs_by_name;
static struct mh_i32ptr_t *sequences;
/** Public change counter. On its update clients need to fetch
 *  new space data from the instance. */
uint32_t schema_version = 0;
/**
 * Internal change counter. Grows faster, than the public one,
 * because we need to remember when to update pointers to already
 * non-existent space objects on space:truncate() operation.
 */
uint32_t space_cache_version = 0;

struct rlist on_schema_init = RLIST_HEAD_INITIALIZER(on_schema_init);
struct rlist on_alter_space = RLIST_HEAD_INITIALIZER(on_alter_space);
struct rlist on_alter_sequence = RLIST_HEAD_INITIALIZER(on_alter_sequence);
struct rlist on_alter_func = RLIST_HEAD_INITIALIZER(on_alter_func);

struct entity_access entity_access;

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

/** Return space by its name */
struct space *
space_by_name(const char *name)
{
	mh_int_t space = mh_strnptr_find_inp(spaces_by_name, name,
					     strlen(name));
	if (space == mh_end(spaces_by_name))
		return NULL;
	return (struct space *)mh_strnptr_node(spaces_by_name, space)->val;
}

/** Return current schema version */
uint32_t
box_schema_version(void)
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

void
space_cache_replace(struct space *old_space, struct space *new_space)
{
	assert(new_space != NULL || old_space != NULL);
	if (new_space != NULL) {
		/*
		 * If the replaced space has a different name, we
		 * must explicitly delete it from @spaces_by_name
		 * cache. Note, since a space id never changes, we
		 * don't need to do so for @spaces cache.
		 */
		struct space *old_space_by_name = NULL;
		if (old_space != NULL && strcmp(space_name(old_space),
						space_name(new_space)) != 0) {
			const char *name = space_name(old_space);
			mh_int_t k = mh_strnptr_find_inp(spaces_by_name, name,
							 strlen(name));
			assert(k != mh_end(spaces_by_name));
			old_space_by_name = (struct space *)
				mh_strnptr_node(spaces_by_name, k)->val;
			mh_strnptr_del(spaces_by_name, k, NULL);
		}
		/*
		 * Insert @new_space into @spaces cache, replacing
		 * @old_space if it's not NULL.
		 */
		const struct mh_i32ptr_node_t node_p = { space_id(new_space),
							 new_space };
		struct mh_i32ptr_node_t old, *p_old = &old;
		mh_int_t k = mh_i32ptr_put(spaces, &node_p, &p_old, NULL);
		if (k == mh_end(spaces)) {
			panic_syserror("Out of memory for the data "
				       "dictionary cache.");
		}
		struct space *old_space_by_id = p_old != NULL ?
				(struct space *)p_old->val : NULL;
		assert(old_space_by_id == old_space);
		(void)old_space_by_id;
		/*
		 * Insert @new_space into @spaces_by_name cache.
		 */
		const char *name = space_name(new_space);
		uint32_t name_len = strlen(name);
		uint32_t name_hash = mh_strn_hash(name, name_len);
		const struct mh_strnptr_node_t node_s = { name, name_len,
							  name_hash, new_space };
		struct mh_strnptr_node_t old_s, *p_old_s = &old_s;
		k = mh_strnptr_put(spaces_by_name, &node_s, &p_old_s, NULL);
		if (k == mh_end(spaces_by_name)) {
			panic_syserror("Out of memory for the data "
				       "dictionary cache.");
		}
		if (old_space_by_name == NULL && p_old_s != NULL)
			old_space_by_name = (struct space *)p_old_s->val;
		assert(old_space_by_name == old_space);
		(void)old_space_by_name;
	} else {
		/*
		 * Delete @old_space from @spaces cache.
		 */
		mh_int_t k = mh_i32ptr_find(spaces, space_id(old_space), NULL);
		assert(k != mh_end(spaces));
		struct space *old_space_by_id =
			(struct space *)mh_i32ptr_node(spaces, k)->val;
		assert(old_space_by_id == old_space);
		(void)old_space_by_id;
		mh_i32ptr_del(spaces, k, NULL);
		/*
		 * Delete @old_space from @spaces_by_name cache.
		 */
		const char *name = space_name(old_space);
		k = mh_strnptr_find_inp(spaces_by_name, name, strlen(name));
		assert(k != mh_end(spaces_by_name));
		struct space *old_space_by_name =
			(struct space *)mh_strnptr_node(spaces_by_name, k)->val;
		assert(old_space_by_name == old_space);
		(void)old_space_by_name;
		mh_strnptr_del(spaces_by_name, k, NULL);
	}
	space_cache_version++;

	if (trigger_run(&on_alter_space, new_space != NULL ?
					 new_space : old_space) != 0) {
		diag_log();
		panic("Can't update space cache");
	}

	if (old_space != NULL)
		space_invalidate(old_space);
}

static int
on_replace_dd_system_space(struct trigger *trigger, void *event)
{
	(void) trigger;
	struct txn *txn = (struct txn *) event;
	if (txn->space_on_replace_triggers_depth > 1) {
		diag_set(ClientError, ER_UNSUPPORTED,
			 "Space on_replace trigger", "DDL operations");
		return -1;
	}
	memtx_tx_acquire_ddl(txn);
	return 0;
}

/** A wrapper around space_new() for data dictionary spaces. */
static void
sc_space_new(uint32_t id, const char *name,
	     struct key_part_def *key_parts,
	     uint32_t key_part_count,
	     struct trigger *replace_trigger)
{
	struct key_def *key_def = key_def_new(key_parts, key_part_count, false);
	if (key_def == NULL)
		diag_raise();
	auto key_def_guard =
		make_scoped_guard([=] { key_def_delete(key_def); });
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
	space_cache_replace(NULL, space);
	if (replace_trigger)
		trigger_add(&space->on_replace, replace_trigger);
	struct trigger *t = (struct trigger *) malloc(sizeof(*t));
	trigger_create(t, on_replace_dd_system_space, NULL, (trigger_f0) free);
	trigger_add(&space->on_replace, t);
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
}

int
schema_find_id(uint32_t system_space_id, uint32_t index_id,
	       const char *name, uint32_t len, uint32_t *object_id)
{
	if (len > BOX_NAME_MAX) {
		*object_id = BOX_ID_NIL;
		return 0;
	}
	struct space *space = space_cache_find(system_space_id);
	if (space == NULL)
		return -1;
	if (!space_is_memtx(space)) {
		diag_set(ClientError, ER_UNSUPPORTED,
			 space->engine->name, "system data");
		return -1;
	}
	struct index *index = index_find(space, index_id);
	if (index == NULL)
		return -1;
	uint32_t size = mp_sizeof_str(len);
	struct region *region = &fiber()->gc;
	uint32_t used = region_used(region);
	char *key = (char *)region_alloc(region, size);
	if (key == NULL) {
		diag_set(OutOfMemory, size, "region", "key");
		return -1;
	}
	mp_encode_str(key, name, len);
	struct iterator *it = index_create_iterator(index, ITER_EQ, key, 1);
	if (it == NULL) {
		region_truncate(region, used);
		return -1;
	}
	struct tuple *tuple;
	int rc = iterator_next(it, &tuple);
	if (rc == 0) {
		/* id is always field #1 */
		if (tuple == NULL)
			*object_id = BOX_ID_NIL;
		else if (tuple_field_u32(tuple, 0, object_id) != 0)
			rc = -1;
	}
	iterator_delete(it);
	region_truncate(region, used);
	return rc;
}

/**
 * Initialize a prototype for the two mandatory data
 * dictionary spaces and create a cache entry for them.
 * When restoring data from the snapshot these spaces
 * will get altered automatically to their actual format.
 */
void
schema_init(void)
{
	struct key_part_def key_parts[3];
	for (uint32_t i = 0; i < lengthof(key_parts); i++)
		key_parts[i] = key_part_def_default;

	/* Initialize the space cache. */
	spaces = mh_i32ptr_new();
	spaces_by_name = mh_strnptr_new();
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
	key_parts[0].fieldno = 0;
	key_parts[0].type = FIELD_TYPE_STRING;
	sc_space_new(BOX_SCHEMA_ID, "_schema", key_parts, 1,
		     &on_replace_schema);

	/* _collation - collation description. */
	key_parts[0].fieldno = 0;
	key_parts[0].type = FIELD_TYPE_UNSIGNED;
	sc_space_new(BOX_COLLATION_ID, "_collation", key_parts, 1,
		     &on_replace_collation);

	/* _space - home for all spaces. */
	sc_space_new(BOX_SPACE_ID, "_space", key_parts, 1,
		     &alter_space_on_replace_space);

	/* _truncate - auxiliary space for triggering space truncation. */
	sc_space_new(BOX_TRUNCATE_ID, "_truncate", key_parts, 1,
		     &on_replace_truncate);

	/* _sequence - definition of all sequence objects. */
	sc_space_new(BOX_SEQUENCE_ID, "_sequence", key_parts, 1,
		     &on_replace_sequence);

	/* _sequence_data - current sequence value. */
	sc_space_new(BOX_SEQUENCE_DATA_ID, "_sequence_data", key_parts, 1,
		     &on_replace_sequence_data);

	/* _space_seq - association space <-> sequence. */
	sc_space_new(BOX_SPACE_SEQUENCE_ID, "_space_sequence", key_parts, 1,
		     &on_replace_space_sequence);

	/* _user - all existing users */
	sc_space_new(BOX_USER_ID, "_user", key_parts, 1, &on_replace_user);

	/* _func - all executable objects on which one can have grants */
	sc_space_new(BOX_FUNC_ID, "_func", key_parts, 1, &on_replace_func);
	/*
	 * _priv - association user <-> object
	 * The real index is defined in the snapshot.
	 */
	sc_space_new(BOX_PRIV_ID, "_priv", key_parts, 1, &on_replace_priv);
	/*
	 * _cluster - association instance uuid <-> instance id
	 * The real index is defined in the snapshot.
	 */
	sc_space_new(BOX_CLUSTER_ID, "_cluster", key_parts, 1,
		     &on_replace_cluster);

	/* _trigger - all existing SQL triggers. */
	key_parts[0].fieldno = 0;
	key_parts[0].type = FIELD_TYPE_STRING;
	sc_space_new(BOX_TRIGGER_ID, "_trigger", key_parts, 1,
		     &on_replace_trigger);

	/* _index - definition of all space indexes. */
	key_parts[0].fieldno = 0; /* space id */
	key_parts[0].type = FIELD_TYPE_UNSIGNED;
	key_parts[1].fieldno = 1; /* index id */
	key_parts[1].type = FIELD_TYPE_UNSIGNED;
	sc_space_new(BOX_INDEX_ID, "_index", key_parts, 2,
		     &alter_space_on_replace_index);

	/* _fk_сonstraint - foreign keys constraints. */
	key_parts[0].fieldno = 0; /* constraint name */
	key_parts[0].type = FIELD_TYPE_STRING;
	key_parts[1].fieldno = 1; /* child space */
	key_parts[1].type = FIELD_TYPE_UNSIGNED;
	sc_space_new(BOX_FK_CONSTRAINT_ID, "_fk_constraint", key_parts, 2,
		     &on_replace_fk_constraint);

	/* _ck_сonstraint - check constraints. */
	key_parts[0].fieldno = 0; /* space id */
	key_parts[0].type = FIELD_TYPE_UNSIGNED;
	key_parts[1].fieldno = 1; /* constraint name */
	key_parts[1].type = FIELD_TYPE_STRING;
	sc_space_new(BOX_CK_CONSTRAINT_ID, "_ck_constraint", key_parts, 2,
		     &on_replace_ck_constraint);

	/* _func_index - check constraints. */
	key_parts[0].fieldno = 0; /* space id */
	key_parts[0].type = FIELD_TYPE_UNSIGNED;
	key_parts[1].fieldno = 1; /* index id */
	key_parts[1].type = FIELD_TYPE_UNSIGNED;
	sc_space_new(BOX_FUNC_INDEX_ID, "_func_index", key_parts, 2,
		     &on_replace_func_index);

	/*
	 * _vinyl_deferred_delete - blackhole that is needed
	 * for writing deferred DELETE statements generated by
	 * vinyl compaction tasks to WAL.
	 *
	 * There is an intricate ordering dependency between
	 * recovery of this system space and initialization of
	 * the vinyl engine, when we set an on_replace trigger
	 * on the space. To resolve this dependency, we create
	 * a space stub in schema_init(), then set a trigger in
	 * engine_begin_initial_recovery(), which is called next,
	 * then recover WAL rows, executing the trigger for each
	 * of them.
	 */
	{
		const char *engine = "blackhole";
		const char *name = "_vinyl_deferred_delete";
		struct space_opts opts = space_opts_default;
		opts.group_id = GROUP_LOCAL;
		struct space_def *def;
		def = space_def_new_xc(BOX_VINYL_DEFERRED_DELETE_ID, ADMIN, 0,
				       name, strlen(name), engine,
				       strlen(engine), &opts, NULL, 0);
		auto def_guard = make_scoped_guard([=] {
			space_def_delete(def);
		});
		RLIST_HEAD(key_list);
		struct space *space = space_new_xc(def, &key_list);
		space_cache_replace(NULL, space);
		init_system_space(space);
	}

	/*
	 * Run the triggers right after creating all the system
	 * space stubs.
	 */
	trigger_run(&on_schema_init, NULL);
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
		space_cache_replace(space, NULL);
		space_delete(space);
	}
	mh_i32ptr_delete(spaces);
	mh_strnptr_delete(spaces_by_name);
	while (mh_size(funcs) > 0) {
		mh_int_t i = mh_first(funcs);

		struct func *func = ((struct func *)
				     mh_i32ptr_node(funcs, i)->val);
		func_cache_delete(func->def->fid);
		func_delete(func);
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
func_cache_insert(struct func *func)
{
	assert(func_by_id(func->def->fid) == NULL);
	assert(func_by_name(func->def->name, strlen(func->def->name)) == NULL);
	const struct mh_i32ptr_node_t node = { func->def->fid, func };
	mh_int_t k1 = mh_i32ptr_put(funcs, &node, NULL, NULL);
	if (k1 == mh_end(funcs)) {
error:
		panic_syserror("Out of memory for the data "
			       "dictionary cache (stored function).");
	}
	size_t def_name_len = strlen(func->def->name);
	uint32_t name_hash = mh_strn_hash(func->def->name, def_name_len);
	const struct mh_strnptr_node_t strnode = {
		func->def->name, def_name_len, name_hash, func };
	mh_int_t k2 = mh_strnptr_put(funcs_by_name, &strnode, NULL, NULL);
	if (k2 == mh_end(funcs_by_name)) {
		mh_i32ptr_del(funcs, k1, NULL);
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

int
schema_find_grants(const char *type, uint32_t id, bool *out)
{
	struct space *priv = space_cache_find(BOX_PRIV_ID);
	if (priv == NULL)
		return -1;

	/** "object" index */
	if (!space_is_memtx(priv)) {
		diag_set(ClientError, ER_UNSUPPORTED,
			 priv->engine->name, "system data");
		return -1;
	}
	struct index *index = index_find(priv, 2);
	if (index == NULL)
		return -1;
	/*
	 * +10 = max(mp_sizeof_uint32) +
	 *       max(mp_sizeof_strl(uint32)).
	 */
	char key[GRANT_NAME_MAX + 10];
	assert(strlen(type) <= GRANT_NAME_MAX);
	mp_encode_uint(mp_encode_str(key, type, strlen(type)), id);
	struct iterator *it = index_create_iterator(index, ITER_EQ, key, 2);
	if (it == NULL)
		return -1;
	IteratorGuard iter_guard(it);
	struct tuple *tuple;
	if (iterator_next(it, &tuple) != 0)
		return -1;
	*out = (tuple != NULL);
	return 0;
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
		diag_set(ClientError, ER_NO_SUCH_SEQUENCE, int2str(id));
	return seq;
}

void
sequence_cache_insert(struct sequence *seq)
{
	assert(sequence_by_id(seq->def->id) == NULL);

	struct mh_i32ptr_node_t node = { seq->def->id, seq };
	mh_int_t k = mh_i32ptr_put(sequences, &node, NULL, NULL);
	if (k == mh_end(sequences)) {
		panic_syserror("Out of memory for the data "
			       "dictionary cache (sequence).");
	}
}

void
sequence_cache_delete(uint32_t id)
{
	mh_int_t k = mh_i32ptr_find(sequences, id, NULL);
	if (k != mh_end(sequences))
		mh_i32ptr_del(sequences, k, NULL);
}

const char *
schema_find_name(enum schema_object_type type, uint32_t object_id)
{
	switch (type) {
	case SC_UNIVERSE:
	case SC_ENTITY_SPACE:
	case SC_ENTITY_FUNCTION:
	case SC_ENTITY_SEQUENCE:
	case SC_ENTITY_ROLE:
	case SC_ENTITY_USER:
		return "";
	case SC_SPACE:
		{
			struct space *space = space_by_id(object_id);
			if (space != NULL)
				return space->def->name;
			diag_set(ClientError, ER_NO_SUCH_SPACE,
				 tt_sprintf("%d", object_id));
			break;
		}
	case SC_FUNCTION:
		{
			struct func *func = func_by_id(object_id);
			if (func != NULL)
				return func->def->name;
			diag_set(ClientError, ER_NO_SUCH_FUNCTION,
				 tt_sprintf("%d", object_id));
			break;
		}
	case SC_SEQUENCE:
		{
			struct sequence *seq = sequence_by_id(object_id);
			if (seq != NULL)
				return seq->def->name;
			diag_set(ClientError, ER_NO_SUCH_SEQUENCE,
				 tt_sprintf("%d", object_id));
			break;
		}
	case SC_ROLE:
		{
			struct user *role = user_by_id(object_id);
			if (role != NULL)
				return role->def->name;
			diag_set(ClientError, ER_NO_SUCH_ROLE,
				 tt_sprintf("%d", object_id));
			break;
		}
	case SC_USER:
		{
			struct user *user = user_by_id(object_id);
			if (user != NULL)
				return user->def->name;
			diag_set(ClientError, ER_NO_SUCH_USER,
				 tt_sprintf("%d", object_id));
			break;
		}
	default:
		unreachable();
	}
	return NULL;
}

