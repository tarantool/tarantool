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
#include "alter.h"
#include "assoc.h"
#include "column_mask.h"
#include "schema.h"
#include "user.h"
#include "space.h"
#include "index.h"
#include "func.h"
#include "coll_id_cache.h"
#include "coll_id_def.h"
#include "txn.h"
#include "txn_limbo.h"
#include "tuple.h"
#include "tuple_constraint.h"
#include "fiber.h" /* for gc_pool */
#include "scoped_guard.h"
#include <base64.h>
#include <new> /* for placement new */
#include <stdio.h> /* snprintf() */
#include <ctype.h>
#include "replication.h" /* for replica_set_id() */
#include "session.h" /* to fetch the current user. */
#include "xrow.h"
#include "iproto_constants.h"
#include "identifier.h"
#include "version.h"
#include "sequence.h"
#include "sql.h"
#include "space_upgrade.h"
#include "box.h"
#include "authentication.h"
#include "node_name.h"

/* {{{ Auxiliary functions and methods. */

static void
box_schema_version_bump(void)
{
	++schema_version;
	box_broadcast_schema();
}

/**
 * Checks if the current user can perform a DDL operation on an object with
 * the given name, owner id, and cached runtime access information.
 * Returns 0 on success, -1 on failure.
 */
static int
access_check_ddl(const char *name, uint32_t owner_uid, struct access *object,
		 enum schema_object_type type, enum priv_type priv_type)
{
	struct credentials *cr = effective_user();
	user_access_t has_access = cr->universal_access;

	user_access_t access = ((PRIV_U | (user_access_t) priv_type) &
				~has_access);
	bool is_owner = owner_uid == cr->uid || cr->uid == ADMIN;
	if (access == 0)
		return 0; /* Access granted. */
	/* Check for specific entity access. */
	struct access *entity = entity_access_get(type);
	if (entity != NULL)
		access &= ~entity[cr->auth_token].effective;
	/*
	 * Only the owner of the object or someone who has
	 * specific DDL privilege on the object can execute
	 * DDL. If a user has no USAGE access and is owner,
	 * deny access as well.
	 * If a user wants to CREATE an object, they're of course
	 * the owner of the object, but this should be ignored --
	 * CREATE privilege is required.
	 */
	if (access == 0 || (is_owner && !(access & (PRIV_U | PRIV_C))))
		return 0; /* Access granted. */
	/*
	 * USAGE can be granted only globally.
	 */
	if (!(access & PRIV_U)) {
		/* Check for privileges on a single object. */
		if (object != NULL)
			access &= ~object[cr->auth_token].effective;
		if (access == 0)
			return 0; /* Access granted. */
	}
	/* Create a meaningful error message. */
	struct user *user = user_find(cr->uid);
	if (user == NULL)
		return -1;
	const char *object_name;
	const char *pname;
	if (access & PRIV_U) {
		object_name = schema_object_name(SC_UNIVERSE);
		pname = priv_name(PRIV_U);
		name = "";
	} else {
		object_name = schema_object_name(type);
		pname = priv_name(access);
	}
	diag_set(AccessDeniedError, pname, object_name, name, user->def->name);
	return -1;
}

/**
 * Return an error if the given index definition
 * is incompatible with a sequence.
 */
static int
index_def_check_sequence(struct index_def *index_def, uint32_t sequence_fieldno,
			 const char *sequence_path, uint32_t sequence_path_len,
			 const char *space_name)
{
	struct key_def *key_def = index_def->key_def;
	struct key_part *sequence_part = NULL;
	for (uint32_t i = 0; i < key_def->part_count; ++i) {
		struct key_part *part = &key_def->parts[i];
		if (part->fieldno != sequence_fieldno)
			continue;
		if ((part->path == NULL && sequence_path == NULL) ||
		    (part->path != NULL && sequence_path != NULL &&
		     json_path_cmp(part->path, part->path_len,
				   sequence_path, sequence_path_len,
				   TUPLE_INDEX_BASE) == 0)) {
			sequence_part = part;
			break;
		}
	}
	if (sequence_part == NULL) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			 space_name, "sequence field must be a part of "
				     "the index");
		return -1;
	}
	enum field_type type = sequence_part->type;
	if (type != FIELD_TYPE_UNSIGNED && type != FIELD_TYPE_INTEGER) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			 space_name, "sequence cannot be used with "
				     "a non-integer key");
		return -1;
	}
	return 0;
}

/**
 * Support function for index_def_new_from_tuple(..)
 * Checks tuple (of _index space) and returns an error if it is invalid
 * Checks only types of fields and their count!
 */
static int
index_def_check_tuple(struct tuple *tuple)
{
	const mp_type common_template[] =
		{MP_UINT, MP_UINT, MP_STR, MP_STR, MP_MAP, MP_ARRAY};
	const char *data = tuple_data(tuple);
	uint32_t field_count = mp_decode_array(&data);
	const char *field_start = data;
	if (field_count != 6)
		goto err;
	for (size_t i = 0; i < lengthof(common_template); i++) {
		enum mp_type type = mp_typeof(*data);
		if (type != common_template[i])
			goto err;
		mp_next(&data);
	}
	return 0;

err:
	char got[DIAG_ERRMSG_MAX];
	char *p = got, *e = got + sizeof(got);
	data = field_start;
	for (uint32_t i = 0; i < field_count && p < e; i++) {
		enum mp_type type = mp_typeof(*data);
		mp_next(&data);
		p += snprintf(p, e - p, i ? ", %s" : "%s", mp_type_strs[type]);
	}
	diag_set(ClientError, ER_WRONG_INDEX_RECORD, got,
		  "space id (unsigned), index id (unsigned), name (string), "\
		  "type (string), options (map), parts (array)");
	return -1;
}

/**
 * Fill index_opts structure from opts field in tuple of space _index
 * Return an error if option is unrecognized.
 */
static int
index_opts_decode(struct index_opts *opts, const char *map,
		  struct region *region)
{
	index_opts_create(opts);
	if (opts_decode(opts, index_opts_reg, &map, region) != 0) {
		diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
			 diag_last_error(diag_get())->errmsg);
		return -1;
	}
	if (opts->distance == rtree_index_distance_type_MAX) {
		diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
			 "distance must be either 'euclid' or 'manhattan'");
		return -1;
	}
	if (opts->page_size <= 0 || (opts->range_size > 0 &&
				     opts->page_size > opts->range_size)) {
		diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
			 "page_size must be greater than 0 and "
			 "less than or equal to range_size");
		return -1;
	}
	if (opts->run_count_per_level <= 0) {
		diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
			 "run_count_per_level must be greater than 0");
		return -1;
	}
	if (opts->run_size_ratio <= 1) {
		diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
			 "run_size_ratio must be greater than 1");
		return -1;
	}
	if (opts->bloom_fpr <= 0 || opts->bloom_fpr > 1) {
		diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
			 "bloom_fpr must be greater than 0 and "
			 "less than or equal to 1");
		return -1;
	}
	return 0;
}

/** Decode an optional node name field from the tuple. */
static int
tuple_field_node_name(char *out, struct tuple *tuple, uint32_t fieldno,
		      const char *field_name)
{
	const char *name, *field;
	uint32_t len;
	if (tuple == NULL)
		goto nil;
	field = tuple_field(tuple, fieldno);
	if (field == NULL || mp_typeof(*field) == MP_NIL)
		goto nil;
	if (mp_typeof(*field) != MP_STR)
		goto error;
	name = mp_decode_str(&field, &len);
	if (!node_name_is_valid_n(name, len))
		goto error;
	memcpy(out, name, len);
	out[len] = 0;
	return 0;
nil:
	*out = 0;
	return 0;
error:
	diag_set(ClientError, ER_FIELD_TYPE, field_name, "a valid name",
		 "a bad name");
	return -1;
}

/**
 * Helper routine for functional index function verification:
 * only a deterministic persistent Lua function may be used in
 * functional index for now.
 */
static int
func_index_check_func(struct func *func) {
	assert(func != NULL);
	if (func->def->language != FUNC_LANGUAGE_LUA ||
	    func->def->body == NULL || !func->def->is_deterministic ||
	    !func->def->is_sandboxed) {
		diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
			 "referenced function doesn't satisfy "
			 "functional index function constraints");
		return -1;
	}
	return 0;
}

/**
 * Create a index_def object from a record in _index
 * system space.
 *
 * Check that:
 * - index id is within range
 * - index type is supported
 * - part count > 0
 * - there are parts for the specified part count
 * - types of parts in the parts array are known to the system
 * - fieldno of each part in the parts array is within limits
 */
static struct index_def *
index_def_new_from_tuple(struct tuple *tuple, struct space *space)
{
	if (index_def_check_tuple(tuple) != 0)
		return NULL;

	struct index_opts opts;
	uint32_t id;
	if (tuple_field_u32(tuple, BOX_INDEX_FIELD_SPACE_ID, &id) != 0)
		return NULL;
	uint32_t index_id;
	if (tuple_field_u32(tuple, BOX_INDEX_FIELD_ID, &index_id) != 0)
		return NULL;
	const char *out = tuple_field_cstr(tuple, BOX_INDEX_FIELD_TYPE);
	if (out == NULL)
		return NULL;
	enum index_type type = STR2ENUM(index_type, out);
	uint32_t name_len;
	const char *name = tuple_field_str(tuple, BOX_INDEX_FIELD_NAME,
					   &name_len);
	if (name == NULL)
		return NULL;
	const char *opts_field = tuple_field_with_type(tuple,
				 BOX_INDEX_FIELD_OPTS, MP_MAP);
	if (opts_field == NULL)
		return NULL;
	if (index_opts_decode(&opts, opts_field, &fiber()->gc) != 0)
		return NULL;
	if (name_len > BOX_NAME_MAX) {
		diag_set(ClientError, ER_MODIFY_INDEX,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  space_name(space), "index name is too long");
		return NULL;
	}
	if (identifier_check(name, name_len) != 0)
		return NULL;

	const char *parts = tuple_field(tuple, BOX_INDEX_FIELD_PARTS);
	uint32_t part_count = mp_decode_array(&parts);
	if (part_count == 0) {
		diag_set(ClientError, ER_MODIFY_INDEX, tt_cstr(name, name_len),
			 space_name(space), "part count must be positive");
		return NULL;
	}
	if (part_count > BOX_INDEX_PART_MAX) {
		diag_set(ClientError, ER_MODIFY_INDEX, tt_cstr(name, name_len),
			 space_name(space), "too many key parts");
		return NULL;
	}
	struct key_def *key_def = NULL;
	struct key_part_def *part_def = (struct key_part_def *)
		malloc(sizeof(*part_def) * part_count);
	if (part_def == NULL) {
		diag_set(OutOfMemory, sizeof(*part_def) * part_count,
			 "malloc", "key_part_def");
		return NULL;
	}
	auto key_def_guard = make_scoped_guard([&] {
		free(part_def);
		if (key_def != NULL)
			key_def_delete(key_def);
	});
	RegionGuard region_guard(&fiber()->gc);
	if (key_def_decode_parts(part_def, part_count, &parts,
				 space->def->fields,
				 space->def->field_count, &fiber()->gc) != 0)
		return NULL;
	bool for_func_index = opts.func_id > 0;
	key_def = key_def_new(part_def, part_count,
			      (type != TREE ? KEY_DEF_UNORDERED : 0) |
			      (for_func_index ? KEY_DEF_FOR_FUNC_INDEX : 0));
	if (key_def == NULL)
		return NULL;
	struct index_def *index_def =
		index_def_new(id, index_id, name, name_len, type,
			      &opts, key_def, space_index_key_def(space, 0));
	if (index_def == NULL)
		return NULL;
	auto index_def_guard = make_scoped_guard([=] { index_def_delete(index_def); });
	if (index_def_check(index_def, space_name(space)) != 0)
		return NULL;
	if (space_check_index_def(space, index_def) != 0)
		return NULL;
	/*
	 * Set opts.hint to the unambiguous ON or OFF value. This
	 * allows to compare opts.hint like in index_opts_cmp()
	 * or memtx_index_def_change_requires_rebuild().
	 */
	if (index_def->opts.hint == INDEX_HINT_DEFAULT) {
		if (space_is_memtx(space) && type == TREE)
			index_def->opts.hint = INDEX_HINT_ON;
		else
			index_def->opts.hint = INDEX_HINT_OFF;
	}
	/*
	 * In case of functional index definition, resolve a
	 * function pointer to perform a complete index build
	 * (istead of initializing it in inactive state) in
	 * on_replace_dd_index trigger. This allows wrap index
	 * creation operation into transaction: only the first
	 * opperation in transaction is allowed to yeld.
	 *
	 * The initialisation during recovery is slightly
	 * different, because function cache is not initialized
	 * during _index space loading. Therefore the completion
	 * of a functional index creation is performed in
	 * _func_index space's trigger, via IndexRebuild
	 * operation.
	 */
	struct func *func = NULL;
	if (for_func_index && (func = func_by_id(opts.func_id)) != NULL) {
		if (func_access_check(func) != 0)
			return NULL;
		if (func_index_check_func(func) != 0)
			return NULL;
		index_def_set_func(index_def, func);
	}
	index_def_guard.is_active = false;
	return index_def;
}

/**
 * Fill space opts from the msgpack stream (MP_MAP field in the
 * tuple).
 */
static int
space_opts_decode(struct space_opts *opts, const char *map,
		  struct region *region)
{
	space_opts_create(opts);
	opts->type = SPACE_TYPE_DEFAULT;
	if (opts_decode(opts, space_opts_reg, &map, region) != 0) {
		diag_set(ClientError, ER_WRONG_SPACE_OPTIONS,
			 diag_last_error(diag_get())->errmsg);
		return -1;
	}
	/*
	 * This can only be SPACE_TYPE_DEFAULT if neither 'type' nor 'temporary'
	 * was specified, which means the space type is normal.
	 */
	if (opts->type == SPACE_TYPE_DEFAULT)
		opts->type = SPACE_TYPE_NORMAL;
	return 0;
}

/**
 * Fill space_def structure from struct tuple.
 */
static struct space_def *
space_def_new_from_tuple(struct tuple *tuple, uint32_t errcode,
			 struct region *region)
{
	uint32_t name_len;
	const char *name = tuple_field_str(tuple, BOX_SPACE_FIELD_NAME,
					   &name_len);
	if (name == NULL)
		return NULL;
	if (name_len > BOX_NAME_MAX) {
		diag_set(ClientError, errcode,
			 tt_cstr(name, BOX_INVALID_NAME_MAX),
			 "space name is too long");
		return NULL;
	}
	if (identifier_check(name, name_len) != 0)
		return NULL;
	uint32_t id;
	if (tuple_field_u32(tuple, BOX_SPACE_FIELD_ID, &id) != 0)
		return NULL;
	if (id > BOX_SPACE_MAX) {
		diag_set(ClientError, errcode, tt_cstr(name, name_len),
			 "space id is too big");
		return NULL;
	}
	if (id == 0) {
		diag_set(ClientError, errcode, tt_cstr(name, name_len),
			 "space id 0 is reserved");
		return NULL;
	}
	uint32_t uid;
	if (tuple_field_u32(tuple, BOX_SPACE_FIELD_UID, &uid) != 0)
		return NULL;
	uint32_t exact_field_count;
	if (tuple_field_u32(tuple, BOX_SPACE_FIELD_FIELD_COUNT,
			    &exact_field_count) != 0)
		return NULL;
	uint32_t engine_name_len;
	const char *engine_name = tuple_field_str(tuple,
		BOX_SPACE_FIELD_ENGINE, &engine_name_len);
	if (engine_name == NULL)
		return NULL;
	/*
	 * Engines are compiled-in so their names are known in
	 * advance to be shorter than names of other identifiers.
	 */
	if (engine_name_len > ENGINE_NAME_MAX) {
		diag_set(ClientError, errcode, tt_cstr(name, name_len),
			 "space engine name is too long");
		return NULL;
	}
	if (identifier_check(engine_name, engine_name_len) != 0)
		return NULL;
	/* Check space opts. */
	const char *space_opts = tuple_field_with_type(tuple,
		BOX_SPACE_FIELD_OPTS, MP_MAP);
	if (space_opts == NULL)
		return NULL;
	/* Check space format */
	const char *format = tuple_field_with_type(tuple,
		BOX_SPACE_FIELD_FORMAT, MP_ARRAY);
	if (format == NULL)
		return NULL;
	const char *format_ptr = format;
	struct field_def *fields = NULL;
	uint32_t field_count;
	RegionGuard region_guard(&fiber()->gc);
	if (field_def_array_decode(&format_ptr, &fields, &field_count,
				   region, false) != 0)
		return NULL;
	size_t format_len = format_ptr - format;
	if (exact_field_count != 0 &&
	    exact_field_count < field_count) {
		diag_set(ClientError, errcode, tt_cstr(name, name_len),
			 "exact_field_count must be either 0 or >= "\
			  "formatted field count");
		return NULL;
	}
	struct space_opts opts;
	if (space_opts_decode(&opts, space_opts, region) != 0)
		return NULL;
	/*
	 * Currently, only predefined replication groups
	 * are supported.
	 */
	if (opts.group_id != GROUP_DEFAULT &&
	    opts.group_id != GROUP_LOCAL) {
		diag_set(ClientError, ER_NO_SUCH_GROUP,
			 int2str(opts.group_id));
		return NULL;
	}
	if (opts.is_view && opts.sql == NULL) {
		diag_set(ClientError, ER_VIEW_MISSING_SQL);
		return NULL;
	}
	if (opts.is_sync && opts.group_id == GROUP_LOCAL) {
		diag_set(ClientError, errcode, tt_cstr(name, name_len),
			 "local space can't be synchronous");
		return NULL;
	}
	if (space_opts_is_temporary(&opts) && opts.constraint_count > 0) {
		diag_set(ClientError, ER_UNSUPPORTED, "temporary space",
			 "constraints");
		return NULL;
	}
	struct space_def *def =
		space_def_new(id, uid, exact_field_count, name, name_len,
			      engine_name, engine_name_len, &opts, fields,
			      field_count, format, format_len);
	if (def == NULL)
		return NULL;
	auto def_guard = make_scoped_guard([=] { space_def_delete(def); });
	struct engine *engine = engine_find(def->engine_name);
	if (engine == NULL)
		return NULL;
	if (engine_check_space_def(engine, def) != 0)
		return NULL;
	def_guard.is_active = false;
	return def;
}

/**
 * Space old and new space triggers (move the original triggers
 * to the new space, or vice versa, restore the original triggers
 * in the old space).
 */
static void
space_swap_triggers(struct space *new_space, struct space *old_space)
{
	rlist_swap(&new_space->before_replace, &old_space->before_replace);
	rlist_swap(&new_space->on_replace, &old_space->on_replace);
	/** Swap SQL Triggers pointer. */
	struct sql_trigger *new_value = new_space->sql_triggers;
	new_space->sql_triggers = old_space->sql_triggers;
	old_space->sql_triggers = new_value;
}

/**
 * True if the space has records identified by key 'uid'.
 * Uses 'iid' index.
 */
int
space_has_data(uint32_t id, uint32_t iid, uint32_t uid, bool *out)
{
	struct space *space = space_by_id(id);
	if (space == NULL) {
		*out = false;
		return 0;
	}

	if (space_index(space, iid) == NULL) {
		*out = false;
		return 0;
	}

	if (!space_is_memtx(space)) {
		diag_set(ClientError, ER_UNSUPPORTED,
			 space->engine->name, "system data");
		return -1;
	}
	struct index *index = index_find(space, iid);
	if (index == NULL)
		return -1;

	char key[6];
	assert(mp_sizeof_uint(BOX_SYSTEM_ID_MIN) <= sizeof(key));
	mp_encode_uint(key, uid);
	struct iterator *it = index_create_iterator(index, ITER_EQ, key, 1);
	if (it == NULL)
		return -1;
	IteratorGuard iter_guard(it);
	struct tuple *tuple;
	if (iterator_next(it, &tuple) != 0)
		return -1;
	*out = (tuple != NULL);
	return 0;
}

/* }}} */

/* {{{ struct alter_space - the body of a full blown alter */
struct alter_space;

class AlterSpaceOp {
public:
	AlterSpaceOp(struct alter_space *alter);

	/** Link in alter_space::ops. */
	struct rlist link;
	/**
	 * Called before creating the new space. Used to update
	 * the space definition and/or key list that will be used
	 * for creating the new space. Must not yield or fail.
	 */
	virtual void alter_def(struct alter_space * /* alter */) {}
	/**
	 * Called after creating a new space. Used for performing
	 * long-lasting operations, such as index rebuild or format
	 * check. May yield. May throw an exception. Must not modify
	 * the old space.
	 */
	virtual void prepare(struct alter_space * /* alter */) {}
	/**
	 * Called after all registered operations have completed
	 * the preparation phase. Used to propagate the old space
	 * state to the new space (e.g. move unchanged indexes).
	 * Must not yield or fail.
	 */
	virtual void alter(struct alter_space * /* alter */) {}
	/**
	 * Called after the change has been successfully written
	 * to WAL. Must not fail.
	 */
	virtual void commit(struct alter_space * /* alter */,
			    int64_t /* signature */) {}
	/**
	 * Called in case a WAL error occurred. It is supposed to undo
	 * the effect of AlterSpaceOp::prepare and AlterSpaceOp::alter.
	 * Must not fail.
	 */
	virtual void rollback(struct alter_space * /* alter */) {}

	virtual ~AlterSpaceOp() {}

	void *operator new(size_t size)
	{
		void *ptr = xregion_aligned_alloc(&in_txn()->region, size,
						  alignof(uint64_t));
		memset(ptr, 0, size);
		return ptr;
	}
	void operator delete(void * /* ptr */) {}
};

/**
 * A trigger installed on transaction commit/rollback events of
 * the transaction which initiated the alter.
 */
static struct trigger *
txn_alter_trigger_new(trigger_f run, void *data)
{
	size_t size = sizeof(struct trigger);
	struct trigger *trigger = (struct trigger *)
		region_aligned_alloc(&in_txn()->region, size,
				     alignof(struct trigger));
	if (trigger == NULL) {
		diag_set(OutOfMemory, size, "region", "struct trigger");
		return NULL;
	}
	trigger_create(trigger, run, data, NULL);
	return trigger;
}

struct alter_space {
	/** List of alter operations */
	struct rlist ops;
	/** Definition of the new space - space_def. */
	struct space_def *space_def;
	/** Definition of the new space - keys. */
	struct rlist key_list;
	/** Old space. */
	struct space *old_space;
	/** New space. */
	struct space *new_space;
	/**
	 * Assigned to the new primary key definition if we're
	 * rebuilding the primary key, i.e. changing its key parts
	 * substantially.
	 */
	struct key_def *pk_def;
	/**
	 * Min field count of a new space. It is calculated before
	 * the new space is created and used to update optionality
	 * of key_defs and key_parts.
	 */
	uint32_t new_min_field_count;
	/**
	 * Number of rows in the transaction at the time when this
	 * DDL operation was performed. It is used to compute this
	 * operation signature on commit, which is needed to keep
	 * xlog in sync with vylog, see alter_space_commit().
	 */
	int n_rows;
};

static struct alter_space *
alter_space_new(struct space *old_space)
{
	struct txn *txn = in_txn();
	size_t size = sizeof(struct alter_space);
	struct alter_space *alter = (struct alter_space *)
		region_aligned_alloc(&in_txn()->region, size,
				     alignof(struct alter_space));
	if (alter == NULL) {
		diag_set(OutOfMemory, size, "region", "struct alter_space");
		return NULL;
	}
	alter = (struct alter_space *)memset(alter, 0, size);
	rlist_create(&alter->ops);
	alter->old_space = old_space;
	alter->space_def = space_def_dup(alter->old_space->def);
	if (old_space->format != NULL)
		alter->new_min_field_count = old_space->format->min_field_count;
	else
		alter->new_min_field_count = 0;
	alter->n_rows = txn_n_rows(txn);
	return alter;
}

/** Destroy alter. */
static void
alter_space_delete(struct alter_space *alter)
{
	/* Destroy the ops. */
	while (! rlist_empty(&alter->ops)) {
		AlterSpaceOp *op = rlist_shift_entry(&alter->ops,
						     AlterSpaceOp, link);
		delete op;
	}
	/* Delete the new space, if any. */
	if (alter->new_space)
		space_delete(alter->new_space);
	space_def_delete(alter->space_def);
}

AlterSpaceOp::AlterSpaceOp(struct alter_space *alter)
{
	/* Add to the tail: operations must be processed in order. */
	rlist_add_tail_entry(&alter->ops, this, link);
}

/**
 * This is a per-space lock which protects the space from
 * concurrent DDL. The current algorithm template for DDL is:
 * 1) Capture change of a system table in a on_replace
 *    trigger
 * 2) Build new schema object, e..g new struct space, and insert
 *    it in the cache - all the subsequent transactions will begin
 *    using this object
 * 3) Write the operation to WAL; this yields, giving a window to
 *    concurrent transactions to use the object, but if there is
 *    a rollback of WAL write, the roll back is *cascading*, so all
 *    subsequent transactions are rolled back first.
 * Step 2 doesn't yield most of the time - e.g. rename of
 * a column, or a compatible change of the format builds a new
 * space objects immediately. Some long operations run in
 * background, after WAL write: this is drop index, and, transitively,
 * drop space, so these don't yield either. But a few operations
 * need to do a long job *before* WAL write: this is create index and
 * deploy of the new format, which checks each space row to
 * conform with index/format constraints, row by row. So this lock
 * is here exactly for these operations. If we allow another DDL
 * against the same space to get in while these operations are in
 * progress, it will use old space object in the space cache, and
 * thus overwrite this transaction's space object, or, worse yet,
 * will get overwritten itself when a long-running DDL completes.
 *
 * Since we consider such concurrent operations to be rare, this
 * lock is optimistic: if there is a lock already, we simply throw
 * an exception.
 */
class AlterSpaceLock {
	/** Set of all taken locks. */
	static struct mh_i32_t *registry;
	/** Identifier of the space this lock is for. */
	uint32_t space_id;
public:
	/** Take a lock for the altered space. */
	AlterSpaceLock(struct alter_space *alter) {
		if (registry == NULL) {
			registry = mh_i32_new();
		}
		space_id = alter->old_space->def->id;
		if (mh_i32_find(registry, space_id, NULL) != mh_end(registry)) {
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(alter->old_space),
				  "the space is already being modified");
		}
		mh_i32_put(registry, &space_id, NULL, NULL);
	}
	~AlterSpaceLock() {
		mh_int_t k = mh_i32_find(registry, space_id, NULL);
		assert(k != mh_end(registry));
		mh_i32_del(registry, k, NULL);
	}
};

struct mh_i32_t *AlterSpaceLock::registry;

/**
 * Commit the alter.
 *
 * Move all unchanged indexes from the old space to the new space.
 * Set the newly built indexes in the new space, or free memory
 * of the dropped indexes.
 * Replace the old space with a new one in the space cache.
 */
static int
alter_space_commit(struct trigger *trigger, void *event)
{
	(void)event;
	struct txn *txn = in_txn();
	struct alter_space *alter = (struct alter_space *) trigger->data;
	/*
	 * The engine (vinyl) expects us to pass the signature of
	 * the row that performed this operation, not the signature
	 * of the transaction itself (this is needed to sync vylog
	 * with xlog on recovery). It's trivial to get this given
	 * the number of rows in the transaction at the time when
	 * the operation was performed.
	 */
	int64_t signature = txn->signature - txn_n_rows(txn) + alter->n_rows;
	/*
	 * Commit alter ops, this will move the changed
	 * indexes into their new places.
	 */
	class AlterSpaceOp *op;
	try {
		rlist_foreach_entry(op, &alter->ops, link)
			op->commit(alter, signature);
	} catch (Exception *e) {
		return -1;
	}

	struct space *space = alter->new_space;
	alter->new_space = NULL; /* for alter_space_delete(). */
	/*
	 * Delete the old version of the space, we are not
	 * going to use it.
	 */
	space_delete(alter->old_space);
	alter->old_space = NULL;
	alter_space_delete(alter);

	space_upgrade_run(space);
	return 0;
}

/**
 * Rollback all effects of space alter. This is
 * a transaction trigger, and it fires most likely
 * upon a failed write to the WAL.
 *
 * Keep in mind that we may end up here in case of
 * alter_space_commit() failure (unlikely)
 */
static int
alter_space_rollback(struct trigger *trigger, void * /* event */)
{
	struct alter_space *alter = (struct alter_space *) trigger->data;
	/* Rollback alter ops */
	class AlterSpaceOp *op;
	try {
		rlist_foreach_entry(op, &alter->ops, link) {
			op->rollback(alter);
		}
	} catch (Exception *e) {
		return -1;
	}
	/* Rebuild index maps once for all indexes. */
	space_fill_index_map(alter->old_space);
	space_fill_index_map(alter->new_space);
	/*
	 * Don't forget about space triggers, foreign keys and
	 * constraints.
	 */
	space_swap_triggers(alter->new_space, alter->old_space);
	space_reattach_constraints(alter->old_space);
	space_pin_collations(alter->old_space);
	space_pin_defaults(alter->old_space);
	space_cache_replace(alter->new_space, alter->old_space);
	alter_space_delete(alter);
	return 0;
}

/**
 * alter_space_do() - do all the work necessary to
 * create a new space.
 *
 * If something may fail during alter, it must be done here,
 * before a record is written to the Write Ahead Log.  Only
 * trivial and infallible actions are left to the commit phase
 * of the alter.
 *
 * The implementation of this function follows "Template Method"
 * pattern, providing a skeleton of the alter, while all the
 * details are encapsulated in AlterSpaceOp methods.
 *
 * These are the major steps of alter defining the structure of
 * the algorithm and performed regardless of what is altered:
 *
 * - a copy of the definition of the old space is created
 * - the definition of the old space is altered, to get
 *   definition of a new space
 * - an instance of the new space is created, according to the new
 *   definition; the space is so far empty
 * - data structures of the new space are built; sometimes, it
 *   doesn't need to happen, e.g. when alter only changes the name
 *   of a space or an index, or other accidental property.
 *   If any data structure needs to be built, e.g. a new index,
 *   only this index is built, not the entire space with all its
 *   indexes.
 * - at commit, the new space is coalesced with the old one.
 *   On rollback, the new space is deleted.
 */
static void
alter_space_do(struct txn_stmt *stmt, struct alter_space *alter)
{
	struct space_alter_stmt alter_stmt;
	alter_stmt.old_tuple = stmt->old_tuple;
	alter_stmt.new_tuple = stmt->new_tuple;
	rlist_add_entry(&stmt->space->alter_stmts, &alter_stmt, link);
	auto alter_stmt_guard = make_scoped_guard([&] {
		rlist_del_entry(&alter_stmt, link);
	});
	/**
	 * AlterSpaceOp::prepare() may perform a potentially long
	 * lasting operation that may yield, e.g. building of a new
	 * index. We really don't want the space to be replaced by
	 * another DDL operation while this one is in progress so
	 * we lock out all concurrent DDL for this space.
	 */
	AlterSpaceLock lock(alter);
	/*
	 * Prepare triggers while we may fail. Note, we don't have to
	 * free them in case of failure, because they are allocated on
	 * the region.
	 */
	struct trigger *on_commit, *on_rollback;
	on_commit = txn_alter_trigger_new(alter_space_commit, alter);
	on_rollback = txn_alter_trigger_new(alter_space_rollback, alter);
	if (on_commit == NULL || on_rollback == NULL)
		diag_raise();

	/* Create a definition of the new space. */
	space_dump_def(alter->old_space, &alter->key_list);
	class AlterSpaceOp *op;
	/*
	 * Alter the definition of the old space, so that
	 * a new space can be created with a new definition.
	 */
	rlist_foreach_entry(op, &alter->ops, link)
		op->alter_def(alter);
	/*
	 * Create a new (empty) space for the new definition.
	 * Sic: the triggers are not moved over yet.
	 */
	alter->new_space = space_new_xc(alter->space_def, &alter->key_list);
	/*
	 * Copy the replace function, the new space is at the same recovery
	 * phase as the old one. This hack is especially necessary for
	 * system spaces, which may be altered in some row in the
	 * snapshot/xlog, but needs to continue staying "fully
	 * built".
	 */
	space_prepare_alter_xc(alter->old_space, alter->new_space);

	alter->new_space->sequence = alter->old_space->sequence;
	alter->new_space->sequence_fieldno = alter->old_space->sequence_fieldno;
	alter->new_space->sequence_path = alter->old_space->sequence_path;
	memcpy(alter->new_space->access, alter->old_space->access,
	       sizeof(alter->old_space->access));

	space_prepare_upgrade_xc(alter->old_space, alter->new_space);

	/*
	 * Build new indexes, check if tuples conform to
	 * the new space format.
	 */
	rlist_foreach_entry(op, &alter->ops, link)
		op->prepare(alter);

	/*
	 * This function must not throw exceptions or yield after
	 * this point.
	 */

	/* Move old indexes, update space format. */
	rlist_foreach_entry(op, &alter->ops, link)
		op->alter(alter);

	/* Rebuild index maps once for all indexes. */
	space_fill_index_map(alter->old_space);
	space_fill_index_map(alter->new_space);

	/*
	 * Don't forget about space triggers, foreign keys and
	 * constraints.
	 */
	space_swap_triggers(alter->new_space, alter->old_space);
	/*
	 * The new space is ready. Time to update the space
	 * cache with it.
	 */
	space_finish_alter(alter->old_space, alter->new_space);
	space_cache_replace(alter->old_space, alter->new_space);
	space_detach_constraints(alter->old_space);
	space_unpin_collations(alter->old_space);
	space_unpin_defaults(alter->old_space);
	/*
	 * Install transaction commit/rollback triggers to either
	 * finish or rollback the DDL depending on the results of
	 * writing to WAL.
	 */
	txn_stmt_on_commit(stmt, on_commit);
	txn_stmt_on_rollback(stmt, on_rollback);
}

/* }}}  */

/* {{{ AlterSpaceOp descendants - alter operations, such as Add/Drop index */

/**
 * This operation does not modify the space, it just checks that
 * tuples stored in it conform to the new format.
 */
class CheckSpaceFormat: public AlterSpaceOp
{
public:
	CheckSpaceFormat(struct alter_space *alter)
		:AlterSpaceOp(alter) {}
	virtual void prepare(struct alter_space *alter);
};

static inline void
space_check_format_with_yield(struct space *space,
			      struct tuple_format *format)
{
	struct txn *txn = in_txn();
	assert(txn != NULL);
	(void) txn_can_yield(txn, true);
	auto yield_guard =
		make_scoped_guard([=] { txn_can_yield(txn, false); });
	space_check_format_xc(space, format);
}

void
CheckSpaceFormat::prepare(struct alter_space *alter)
{
	struct space *new_space = alter->new_space;
	struct space *old_space = alter->old_space;
	struct tuple_format *new_format = new_space->format;
	struct tuple_format *old_format = old_space->format;
	if (new_space->upgrade != NULL) {
		/*
		 * Tuples stored in the space will be checked against
		 * the new format during space upgrade.
		 */
		return;
	}
	if (old_format != NULL) {
		assert(new_format != NULL);
		for (uint32_t i = 0; i < old_space->index_count; i++) {
			struct key_def *key_def =
				alter->old_space->index[i]->def->key_def;
			if (!tuple_format_is_compatible_with_key_def(new_format,
								     key_def))
				diag_raise();
		}
		space_check_format_with_yield(old_space, new_format);
	}
}

/** Change non-essential properties of a space. */
class ModifySpace: public AlterSpaceOp
{
	void space_swap_dictionaries(struct space_def *old_def,
				     struct space_def *new_def,
				     struct tuple_format *new_format);
public:
	ModifySpace(struct alter_space *alter, struct space_def *def)
		: AlterSpaceOp(alter), new_def(def) {}
	/* New space definition. */
	struct space_def *new_def;
	virtual void alter_def(struct alter_space *alter);
	virtual void alter(struct alter_space *alter);
	virtual void rollback(struct alter_space *alter);
	virtual ~ModifySpace();
};

void
ModifySpace::space_swap_dictionaries(struct space_def *old_def,
				     struct space_def *new_def,
				     struct tuple_format *new_format)
{
	/*
	 * When new space_def is created, it allocates new dictionary.
	 * Move new names into an old dictionary, which already is referenced
	 * by existing tuple formats. New dictionary object is deleted later,
	 * in alter_space_delete.
	 */
	tuple_dictionary_unref(new_format->dict);
	new_format->dict = old_def->dict;
	tuple_dictionary_ref(new_format->dict);

	SWAP(old_def->dict, new_def->dict);
	tuple_dictionary_swap(old_def->dict, new_def->dict);
}

/** Amend the definition of the new space. */
void
ModifySpace::alter_def(struct alter_space *alter)
{
	new_def->view_ref_count = alter->old_space->def->view_ref_count;
	space_def_delete(alter->space_def);
	alter->space_def = new_def;
	/* Now alter owns the def. */
	new_def = NULL;
}

void
ModifySpace::alter(struct alter_space *alter)
{
	/*
	 * Unless it's online space upgrade, use the old dictionary for the new
	 * space, because it's already referenced by existing tuple formats.
	 *
	 * For online space upgrade, all tuples fetched from the space will be
	 * upgraded to the new format before returning to the user so it isn't
	 * necessary. Moreover, the old tuples may be incompatible with the new
	 * format so using the new dictionary for them would be wrong and could
	 * result in error accessing fields by name from the space upgrade
	 * function.
	 */
	if (alter->space_def->opts.upgrade_def == NULL) {
		space_swap_dictionaries(alter->old_space->def,
					alter->new_space->def,
					alter->new_space->format);
	}
}

void
ModifySpace::rollback(struct alter_space *alter)
{
	if (alter->space_def->opts.upgrade_def == NULL) {
		space_swap_dictionaries(alter->new_space->def,
					alter->old_space->def,
					alter->new_space->format);
	}
}

ModifySpace::~ModifySpace()
{
	if (new_def != NULL)
		space_def_delete(new_def);
}

/** DropIndex - remove an index from space. */

class DropIndex: public AlterSpaceOp
{
public:
	DropIndex(struct alter_space *alter, struct index *index)
		:AlterSpaceOp(alter), old_index(index) {}
	struct index *old_index;
	virtual void alter_def(struct alter_space *alter);
	virtual void prepare(struct alter_space *alter);
	virtual void commit(struct alter_space *alter, int64_t lsn);
};

/*
 * Alter the definition of the new space and remove
 * the new index from it.
 */
void
DropIndex::alter_def(struct alter_space * /* alter */)
{
	rlist_del_entry(old_index->def, link);
}

/* Do the drop. */
void
DropIndex::prepare(struct alter_space *alter)
{
	if (old_index->def->iid == 0)
		space_drop_primary_key(alter->new_space);
}

void
DropIndex::commit(struct alter_space *alter, int64_t signature)
{
	(void)alter;
	index_commit_drop(old_index, signature);
}

/**
 * A no-op to preserve the old index data in the new space.
 * Added to the alter specification when the index at hand
 * is not affected by alter in any way.
 */
class MoveIndex: public AlterSpaceOp
{
public:
	MoveIndex(struct alter_space *alter, uint32_t iid_arg)
		:AlterSpaceOp(alter), iid(iid_arg) {}
	/** id of the index on the move. */
	uint32_t iid;
	virtual void alter(struct alter_space *alter);
	virtual void rollback(struct alter_space *alter);
};

void
MoveIndex::alter(struct alter_space *alter)
{
	space_swap_index(alter->old_space, alter->new_space, iid, iid);
}

void
MoveIndex::rollback(struct alter_space *alter)
{
	space_swap_index(alter->old_space, alter->new_space, iid, iid);
}

/**
 * Change non-essential properties of an index, i.e.
 * properties not involving index data or layout on disk.
 */
class ModifyIndex: public AlterSpaceOp
{
public:
	ModifyIndex(struct alter_space *alter,
		    struct index *index, struct index_def *def)
		: AlterSpaceOp(alter), old_index(index),
		  new_index(NULL), new_index_def(def) {
	        if (new_index_def->iid == 0 &&
	            key_part_cmp(new_index_def->key_def->parts,
	                         new_index_def->key_def->part_count,
	                         old_index->def->key_def->parts,
	                         old_index->def->key_def->part_count) != 0) {
	                /*
	                 * Primary parts have been changed -
	                 * update secondary indexes.
	                 */
	                alter->pk_def = new_index_def->key_def;
	        }
	}
	struct index *old_index;
	struct index *new_index;
	struct index_def *new_index_def;
	virtual void alter_def(struct alter_space *alter);
	virtual void alter(struct alter_space *alter);
	virtual void commit(struct alter_space *alter, int64_t lsn);
	virtual void rollback(struct alter_space *alter);
	virtual ~ModifyIndex();
};

/** Update the definition of the new space */
void
ModifyIndex::alter_def(struct alter_space *alter)
{
	rlist_del_entry(old_index->def, link);
	index_def_list_add(&alter->key_list, new_index_def);
}

void
ModifyIndex::alter(struct alter_space *alter)
{
	new_index = space_index(alter->new_space, new_index_def->iid);
	assert(old_index->def->iid == new_index->def->iid);
	/*
	 * Move the old index to the new space to preserve the
	 * original data, but use the new definition.
	 */
	space_swap_index(alter->old_space, alter->new_space,
			 old_index->def->iid, new_index->def->iid);
	SWAP(old_index, new_index);
	SWAP(old_index->def, new_index->def);
	index_update_def(new_index);
}

void
ModifyIndex::commit(struct alter_space *alter, int64_t signature)
{
	(void)alter;
	index_commit_modify(new_index, signature);
}

void
ModifyIndex::rollback(struct alter_space *alter)
{
	/*
	 * Restore indexes.
	 */
	space_swap_index(alter->old_space, alter->new_space,
			 old_index->def->iid, new_index->def->iid);
	SWAP(old_index, new_index);
	SWAP(old_index->def, new_index->def);
	index_update_def(old_index);
}

ModifyIndex::~ModifyIndex()
{
	index_def_delete(new_index_def);
}

/** CreateIndex - add a new index to the space. */
class CreateIndex: public AlterSpaceOp
{
	/** New index. */
	struct index *new_index;
	/** New index index_def. */
	struct index_def *new_index_def;
public:
	CreateIndex(struct alter_space *alter, struct index_def *def)
		:AlterSpaceOp(alter), new_index(NULL), new_index_def(def)
	{}
	virtual void alter_def(struct alter_space *alter);
	virtual void prepare(struct alter_space *alter);
	virtual void commit(struct alter_space *alter, int64_t lsn);
	virtual ~CreateIndex();
};

/** Add definition of the new key to the new space def. */
void
CreateIndex::alter_def(struct alter_space *alter)
{
	index_def_list_add(&alter->key_list, new_index_def);
}

static inline void
space_build_index_with_yield(struct space *old_space, struct space *new_space,
			     struct index *new_index)
{
	struct txn *txn = in_txn();
	assert(txn != NULL);
	(void) txn_can_yield(txn, true);
	auto yield_guard =
		make_scoped_guard([=] { txn_can_yield(txn, false); });
	space_build_index_xc(old_space, new_space, new_index);
}

/**
 * Optionally build the new index.
 *
 * During recovery the space is often not fully constructed yet
 * anyway, so there is no need to fully populate index with data,
 * it is done at the end of recovery.
 *
 * Note, that system spaces are exception to this, since
 * they are fully enabled at all times.
 */
void
CreateIndex::prepare(struct alter_space *alter)
{
	/* Get the new index and build it.  */
	new_index = space_index(alter->new_space, new_index_def->iid);
	assert(new_index != NULL);

	struct key_def *key_def = new_index->def->key_def;
	if (!tuple_format_is_compatible_with_key_def(alter->new_space->format,
						     key_def))
		diag_raise();
	if (new_index_def->iid == 0) {
		/*
		 * Adding a primary key: bring the space
		 * up to speed with the current recovery
		 * state. During snapshot recovery it
		 * means preparing the primary key for
		 * build (beginBuild()). During xlog
		 * recovery, it means building the primary
		 * key. After recovery, it means building
		 * all keys.
		 */
		space_add_primary_key_xc(alter->new_space);
		return;
	}
	space_build_index_with_yield(alter->old_space, alter->new_space,
				     new_index);
}

void
CreateIndex::commit(struct alter_space *alter, int64_t signature)
{
	(void) alter;
	assert(new_index != NULL);
	index_commit_create(new_index, signature);
	new_index = NULL;
}

CreateIndex::~CreateIndex()
{
	if (new_index != NULL)
		index_abort_create(new_index);
	if (new_index_def != NULL)
		index_def_delete(new_index_def);
}

/**
 * RebuildIndex - drop the old index data and rebuild index
 * from by reading the primary key. Used when key_def of
 * an index is changed.
 */
class RebuildIndex: public AlterSpaceOp
{
public:
	RebuildIndex(struct alter_space *alter,
		     struct index_def *new_index_def_arg,
		     struct index_def *old_index_def_arg)
		:AlterSpaceOp(alter), new_index(NULL),
		new_index_def(new_index_def_arg),
		old_index_def(old_index_def_arg)
	{
		/* We may want to rebuild secondary keys as well. */
		if (new_index_def->iid == 0)
			alter->pk_def = new_index_def->key_def;
	}
	/** New index. */
	struct index *new_index;
	/** New index index_def. */
	struct index_def *new_index_def;
	/** Old index index_def. */
	struct index_def *old_index_def;
	virtual void alter_def(struct alter_space *alter);
	virtual void prepare(struct alter_space *alter);
	virtual void commit(struct alter_space *alter, int64_t signature);
	virtual ~RebuildIndex();
};

/** Add definition of the new key to the new space def. */
void
RebuildIndex::alter_def(struct alter_space *alter)
{
	rlist_del_entry(old_index_def, link);
	index_def_list_add(&alter->key_list, new_index_def);
}

void
RebuildIndex::prepare(struct alter_space *alter)
{
	/* Get the new index and build it.  */
	new_index = space_index(alter->new_space, new_index_def->iid);
	assert(new_index != NULL);
	space_build_index_with_yield(alter->old_space, alter->new_space,
				     new_index);
}

void
RebuildIndex::commit(struct alter_space *alter, int64_t signature)
{
	struct index *old_index = space_index(alter->old_space,
					      old_index_def->iid);
	assert(old_index != NULL);
	index_commit_drop(old_index, signature);
	assert(new_index != NULL);
	index_commit_create(new_index, signature);
	new_index = NULL;
}

RebuildIndex::~RebuildIndex()
{
	if (new_index != NULL)
		index_abort_create(new_index);
	if (new_index_def != NULL)
		index_def_delete(new_index_def);
}

/**
 * RebuildFuncIndex - prepare func index definition,
 * drop the old index data and rebuild index from by reading the
 * primary key.
 */
class RebuildFuncIndex: public RebuildIndex
{
	/*
	 * This method is used before the class is built (is used to calculate
	 * an argument for base class constructor), so it should be static.
	 */
	static struct index_def *
	func_index_def_new(struct index_def *index_def, struct func *func)
	{
		struct index_def *new_index_def = index_def_dup(index_def);
		index_def_set_func(new_index_def, func);
		return new_index_def;
	}
public:
	RebuildFuncIndex(struct alter_space *alter,
			 struct index_def *old_index_def_arg, struct func *func) :
		RebuildIndex(alter, func_index_def_new(old_index_def_arg, func),
			     old_index_def_arg) {}
};

/**
 * Drop the old index with data and create a disabled index in place of it.
 */
class DisableFuncIndex: public AlterSpaceOp
{
	struct index_def *new_index_def;
	struct index_def *old_index_def;
public:
	DisableFuncIndex(struct alter_space *alter,
			 struct index_def *old_index_def_arg)
		: AlterSpaceOp(alter), old_index_def(old_index_def_arg)
	{
		/* Functional indexes are only implemented in memtx. */
		assert(!strcmp(alter->old_space->engine->name, "memtx"));
		new_index_def = index_def_dup(old_index_def);
		index_def_set_func(new_index_def, NULL);
	}

	virtual void alter_def(struct alter_space *alter);
	virtual ~DisableFuncIndex();
};

void
DisableFuncIndex::alter_def(struct alter_space *alter)
{
	rlist_del_entry(old_index_def, link);
	index_def_list_add(&alter->key_list, new_index_def);
}

DisableFuncIndex::~DisableFuncIndex()
{
	if (new_index_def != NULL)
		index_def_delete(new_index_def);
}

/** TruncateIndex - truncate an index. */
class TruncateIndex: public AlterSpaceOp
{
	/** id of the index to truncate. */
	uint32_t iid;
	/**
	 * In case TRUNCATE fails, we need to clean up the new
	 * index data in the engine.
	 */
	struct index *old_index;
	struct index *new_index;
public:
	TruncateIndex(struct alter_space *alter, uint32_t iid)
		: AlterSpaceOp(alter), iid(iid),
		  old_index(NULL), new_index(NULL) {}
	virtual void prepare(struct alter_space *alter);
	virtual void commit(struct alter_space *alter, int64_t signature);
	virtual ~TruncateIndex();
};

void
TruncateIndex::prepare(struct alter_space *alter)
{
	old_index = space_index(alter->old_space, iid);
	new_index = space_index(alter->new_space, iid);

	if (iid == 0) {
		/*
		 * Notify the engine that the primary index
		 * was truncated.
		 */
		space_drop_primary_key(alter->new_space);
		space_add_primary_key_xc(alter->new_space);
		return;
	}

	/*
	 * Although the new index is empty, we still need to call
	 * space_build_index() to let the engine know that the
	 * index was recreated. For example, Vinyl uses this
	 * callback to load indexes during local recovery.
	 */
	assert(new_index != NULL);
	space_build_index_with_yield(alter->new_space, alter->new_space,
				     new_index);
}

void
TruncateIndex::commit(struct alter_space *alter, int64_t signature)
{
	(void)alter;
	index_commit_drop(old_index, signature);
	index_commit_create(new_index, signature);
	new_index = NULL;
}

TruncateIndex::~TruncateIndex()
{
	if (new_index == NULL)
		return;
	index_abort_create(new_index);
}

/**
 * UpdateSchemaVersion - increment schema_version. Used on
 * in alter_space_do(), i.e. when creating or dropping
 * an index, altering a space.
 */
class UpdateSchemaVersion: public AlterSpaceOp
{
public:
	UpdateSchemaVersion(struct alter_space * alter)
		:AlterSpaceOp(alter) {}
	virtual void alter(struct alter_space *alter);
};

void
UpdateSchemaVersion::alter(struct alter_space *alter)
{
    (void)alter;
    box_schema_version_bump();
}

/* }}} */

/**
 * Delete the space. It is already removed from the space cache.
 */
static int
on_drop_space_commit(struct trigger *trigger, void *event)
{
	(void) event;
	struct space *space = (struct space *)trigger->data;
	space_remove_temporary_triggers(space);
	space_delete(space);
	return 0;
}

/**
 * Return the original space back into the cache. The effect
 * of all other events happened after the space was removed were
 * reverted by the cascading rollback.
 */
static int
on_drop_space_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct space *space = (struct space *)trigger->data;
	space_cache_replace(NULL, space);
	space_reattach_constraints(space);
	space_pin_collations(space);
	space_pin_defaults(space);
	return 0;
}

/**
 * A trigger invoked on commit/rollback of DROP/ADD space.
 * The trigger removes the space from the space cache.
 *
 * By the time the space is removed, it should be empty: we
 * rely on cascading rollback.
 */
static int
on_create_space_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct space *space = (struct space *)trigger->data;
	space_cache_replace(space, NULL);
	space_delete(space);
	return 0;
}

/**
 * Create MoveIndex operation for a range of indexes in a space
 * for range [begin, end)
 */
int
alter_space_move_indexes(struct alter_space *alter, uint32_t begin,
			 uint32_t end)
{
	struct space *old_space = alter->old_space;
	bool is_min_field_count_changed;
	if (old_space->format != NULL) {
		is_min_field_count_changed =
			old_space->format->min_field_count !=
			alter->new_min_field_count;
	} else {
		is_min_field_count_changed = false;
	}
	for (uint32_t index_id = begin; index_id < end; ++index_id) {
		struct index *old_index = space_index(old_space, index_id);
		if (old_index == NULL)
			continue;
		struct index_def *old_def = old_index->def;
		struct index_def *new_def;
		uint32_t min_field_count = alter->new_min_field_count;
		if (alter->pk_def == NULL || !index_depends_on_pk(old_index)) {
			if (is_min_field_count_changed) {
				new_def = index_def_dup(old_def);
				index_def_update_optionality(new_def,
							     min_field_count);
				try {
					(void) new ModifyIndex(alter, old_index, new_def);
				} catch (Exception *e) {
					return -1;
				}
			} else {
				try {
					(void) new MoveIndex(alter, old_def->iid);
				} catch (Exception *e) {
					return -1;
				}
			}
			continue;
		}
		/*
		 * Rebuild secondary indexes that depend on the
		 * primary key since primary key parts have changed.
		 */
		new_def = index_def_new(old_def->space_id, old_def->iid,
					old_def->name, strlen(old_def->name),
					old_def->type, &old_def->opts,
					old_def->key_def, alter->pk_def);
		index_def_update_optionality(new_def, min_field_count);
		auto guard = make_scoped_guard([=] { index_def_delete(new_def); });
		if (!index_def_change_requires_rebuild(old_index, new_def))
			try {
				(void) new ModifyIndex(alter, old_index, new_def);
			} catch (Exception *e) {
				return -1;
			}
		else
			try {
				(void) new RebuildIndex(alter, new_def, old_def);
			} catch (Exception *e) {
				return -1;
			}
		guard.is_active = false;
	}
	return 0;
}

/**
 * Walk through all spaces from 'FROM' clause of given select,
 * and update their view reference counters.
 *
 * @param select Tables from this select to be updated.
 * @param update_value +1 on view creation, -1 on drop.
 * @retval 0 on success, -1 on error (diag is set).
 */
static int
update_view_references(struct Select *select, int update_value)
{
	assert(update_value == 1 || update_value == -1);
	struct SrcList *list = sql_select_expand_from_tables(select);
	int from_tables_count = sql_src_list_entry_count(list);
	/* Firstly check that everything is correct. */
	for (int i = 0; i < from_tables_count; ++i) {
		const char *space_name = sql_src_list_entry_name(list, i);
		assert(space_name != NULL);
		/*
		 * Views are allowed to contain CTEs. CTE is a
		 * temporary object, created and destroyed at SQL
		 * runtime (it is represented by an ephemeral
		 * table). So, it is absent in space cache and as
		 * a consequence we can't increment its reference
		 * counter. Skip iteration.
		 */
		if (sql_select_constains_cte(select, space_name))
			continue;
		struct space *space = space_by_name0(space_name);
		if (space == NULL) {
			diag_set(ClientError, ER_NO_SUCH_SPACE, space_name);
			goto error;
		}
		if (space_is_temporary(space)) {
			diag_set(ClientError, ER_UNSUPPORTED,
				 "CREATE VIEW", "temporary spaces");
			goto error;
		}
	}
	/* Secondly do the job. */
	for (int i = 0; i < from_tables_count; ++i) {
		const char *space_name = sql_src_list_entry_name(list, i);
		/* See comment before sql_select_constains_cte call above. */
		if (sql_select_constains_cte(select, space_name))
			continue;
		struct space *space = space_by_name0(space_name);
		assert(space->def->view_ref_count > 0 || update_value > 0);
		space->def->view_ref_count += update_value;
	}
	sqlSrcListDelete(list);
	return 0;
error:
	sqlSrcListDelete(list);
	return -1;
}

/**
 * Trigger which is fired to commit creation of new SQL view.
 * Its purpose is to release memory of SELECT.
 */
static int
on_create_view_commit(struct trigger *trigger, void *event)
{
	(void) event;
	struct Select *select = (struct Select *)trigger->data;
	sql_select_delete(select);
	return 0;
}

/**
 * Trigger which is fired to rollback creation of new SQL view.
 * Decrements view reference counters of dependent spaces and
 * releases memory for SELECT.
 */
static int
on_create_view_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct Select *select = (struct Select *)trigger->data;
	int rc = update_view_references(select, -1);
	assert(rc == 0); (void)rc;
	sql_select_delete(select);
	return 0;
}

/**
 * Trigger which is fired to commit drop of SQL view.
 * Its purpose is to decrement view reference counters of
 * dependent spaces and release memory for SELECT.
 */
static int
on_drop_view_commit(struct trigger *trigger, void *event)
{
	(void) event;
	struct Select *select = (struct Select *)trigger->data;
	sql_select_delete(select);
	return 0;
}

/**
 * This trigger is invoked to rollback drop of SQL view.
 * Release memory for struct SELECT compiled in
 * on_replace_dd_space trigger.
 */
static int
on_drop_view_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct Select *select = (struct Select *)trigger->data;
	int rc = update_view_references(select, 1);
	assert(rc == 0); (void)rc;
	sql_select_delete(select);
	return 0;
}

/**
 * Return -1 and set diag if the space is pinned by someone.
 */
static int
space_check_pinned(struct space *space)
{
	enum space_cache_holder_type pinned_type;
	if (space_cache_is_pinned(space, &pinned_type)) {
		const char *type_str =
			space_cache_holder_type_strs[pinned_type];
		diag_set(ClientError, ER_ALTER_SPACE,
			 space_name(space),
			 tt_sprintf("space is referenced by %s", type_str));
		return -1;
	}
	return 0;
}

/**
 * Check whether @a space holders prohibit truncate of the space.
 * For example truncation in not allowed if another non-empty space refers
 * to this space via foreign key link.
 * Return 0 if allowed, or -1 if not allowed (diag is set).
 */
static int
space_check_truncate(struct space *space)
{
	/* Check for foreign keys that refers to this space. */
	struct space_cache_holder *h;
	rlist_foreach_entry(h, &space->space_cache_pin_list, link) {
		if (h->selfpin)
			continue;
		if (h->type != SPACE_HOLDER_FOREIGN_KEY)
			continue;
		struct tuple_constraint *constr =
			container_of(h, struct tuple_constraint,
				     space_cache_holder);
		struct space *other_space = constr->space;
		/*
		 * If the referring space is empty then the truncate can't
		 * break foreign key consistency.
		 */
		if (space_bsize(other_space) == 0)
			continue;
		const char *type_str =
			space_cache_holder_type_strs[h->type];
		diag_set(ClientError, ER_ALTER_SPACE,
			 space_name(space),
			 tt_sprintf("space is referenced by %s", type_str));
		return -1;
	}
	return 0;
}

/**
 * Check whether @a old_space holders prohibit alter to @a new_space_def.
 * For example if the space becomes data-temporary, there can be foreign keys
 * from non-data-temporary space, so this alter must not be allowed.
 * Return 0 if allowed, or -1 if not allowed (diag is set).
 */
static int
space_check_alter(struct space *old_space, struct space_def *new_space_def)
{
	/*
	 * group_id, which is currently used for defining local spaces, is
	 * now can't be changed; if it could, an additional check would be
	 * required below.
	 */
	assert(old_space->def->opts.group_id == new_space_def->opts.group_id);
	/* Only alter from non-data-temporary to data-temporary can cause
	 * problems.
	 */
	if (space_is_data_temporary(old_space) ||
	    !space_opts_is_data_temporary(&new_space_def->opts))
		return 0;
	/* Check for foreign keys that refers to this space. */
	struct space_cache_holder *h;
	rlist_foreach_entry(h, &old_space->space_cache_pin_list, link) {
		if (h->selfpin)
			continue;
		if (h->type != SPACE_HOLDER_FOREIGN_KEY)
			continue;
		struct tuple_constraint *constr =
			container_of(h, struct tuple_constraint,
				     space_cache_holder);
		struct space *other_space = constr->space;
		/*
		 * If the referring space is data-temporary too then the alter
		 * can't break foreign key consistency after restart.
		 */
		if (space_opts_is_data_temporary(&other_space->def->opts))
			continue;
		diag_set(ClientError, ER_ALTER_SPACE,
			 space_name(old_space),
			 tt_sprintf("foreign key '%s' from non-data-temporary"
				    " space '%s' can't refer to data-temporary"
				    " space",
				    constr->def.name, space_name(other_space)));
		return -1;
	}
	return 0;
}

/*
 * box_process1() bypasses the read-only check for the _space system space
 * because there it's not yet known if the related space is temporary. Perform
 * the check here if the space isn't temporary and the statement was issued by
 * this replica.
 */
static int
filter_temporary_ddl_stmt(struct txn *txn, const struct space_def *def)
{
	if (def == NULL)
		return 0;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	if (space_opts_is_temporary(&def->opts)) {
		txn_stmt_mark_as_temporary(txn, stmt);
		return 0;
	}
	if (stmt->row->replica_id == 0 && recovery_state != INITIAL_RECOVERY)
		return box_check_writable();
	return 0;
}

/**
 * A trigger which is invoked on replace in a data dictionary
 * space _space.
 *
 * Generally, whenever a data dictionary change occurs
 * 2 things should be done:
 *
 * - space cache should be updated
 *
 * - the space which is changed should be rebuilt according
 *   to the nature of the modification, i.e. indexes added/dropped,
 *   tuple format changed, etc.
 *
 * When dealing with an update of _space space, we have 3 major
 * cases:
 *
 * 1) insert a new tuple: creates a new space
 *    The trigger prepares a space structure to insert
 *    into the  space cache and registers an on commit
 *    hook to perform the registration. Should the statement
 *    itself fail, transaction is rolled back, the transaction
 *    rollback hook must be there to delete the created space
 *    object, avoiding a memory leak. The hooks are written
 *    in a way that excludes the possibility of a failure.
 *
 * 2) delete a tuple: drops an existing space.
 *
 *    A space can be dropped only if it has no indexes.
 *    The only reason for this restriction is that there
 *    must be no tuples in _index without a corresponding tuple
 *    in _space. It's not possible to delete such tuples
 *    automatically (this would require multi-statement
 *    transactions), so instead the trigger verifies that the
 *    records have been deleted by the user.
 *
 *    Then the trigger registers transaction commit hook to
 *    perform the deletion from the space cache.  No rollback hook
 *    is required: if the transaction is rolled back, nothing is
 *    done.
 *
 * 3) modify an existing tuple: some space
 *    properties are immutable, but it's OK to change
 *    space name or field count. This is done in WAL-error-
 *    safe mode.
 *
 * A note about memcached_space: Tarantool 1.4 had a check
 * which prevented re-definition of memcached_space. With
 * dynamic space configuration such a check would be particularly
 * clumsy, so it is simply not done.
 */
static int
on_replace_dd_space(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	struct region *region = &fiber()->gc;
	/*
	 * Things to keep in mind:
	 * - old_tuple is set only in case of UPDATE.  For INSERT
	 *   or REPLACE it is NULL.
	 * - the trigger may be called inside recovery from a snapshot,
	 *   when index look up is not possible
	 * - _space, _index and other metaspaces initially don't
	 *   have a tuple which represents it, this tuple is only
	 *   created during recovery from a snapshot.
	 *
	 * Let's establish whether an old space exists. Use
	 * old_tuple ID field, if old_tuple is set, since UPDATE
	 * may have changed space id.
	 */
	uint32_t old_id;
	if (tuple_field_u32(old_tuple ? old_tuple : new_tuple,
			    BOX_SPACE_FIELD_ID, &old_id) != 0)
		return -1;
	struct space *old_space = space_by_id(old_id);
	struct space_def *def = NULL;
	if (new_tuple != NULL) {
		uint32_t errcode = (old_tuple == NULL) ?
			ER_CREATE_SPACE : ER_ALTER_SPACE;
		def = space_def_new_from_tuple(new_tuple, errcode, region);
	}
	if (filter_temporary_ddl_stmt(txn, old_space != NULL ?
				      old_space->def : def) != 0)
		return -1;
	if (new_tuple != NULL && old_space == NULL) { /* INSERT */
		if (def == NULL)
			return -1;
		auto def_guard =
			make_scoped_guard([=] { space_def_delete(def); });
		if (access_check_ddl(def->name, def->uid, NULL,
				     SC_SPACE, PRIV_C) != 0)
			return -1;
		RLIST_HEAD(empty_list);
		struct space *space = space_new(def, &empty_list);
		if (space == NULL)
			return -1;
		/**
		 * The new space must be inserted in the space
		 * cache right away to achieve linearisable
		 * execution on a replica.
		 */
		space_cache_replace(NULL, space);
		/*
		 * Do not forget to update schema_version right after
		 * inserting the space to the space_cache, since no
		 * AlterSpaceOps are registered in case of space
		 * create.
		 */
		box_schema_version_bump();
		/*
		 * So may happen that until the DDL change record
		 * is written to the WAL, the space is used for
		 * insert/update/delete. All these updates are
		 * rolled back by the pipelined rollback mechanism,
		 * so it's safe to simply drop the space on
		 * rollback.
		 */
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_create_space_rollback, space);
		if (on_rollback == NULL)
			return -1;
		txn_stmt_on_rollback(stmt, on_rollback);
		if (def->opts.is_view) {
			struct Select *select = sql_view_compile(def->opts.sql);
			if (select == NULL)
				return -1;
			auto select_guard = make_scoped_guard([=] {
				sql_select_delete(select);
			});
			if (update_view_references(select, 1) != 0)
				return -1;
			struct trigger *on_commit_view =
				txn_alter_trigger_new(on_create_view_commit,
						      select);
			if (on_commit_view == NULL)
				return -1;
			txn_stmt_on_commit(stmt, on_commit_view);
			struct trigger *on_rollback_view =
				txn_alter_trigger_new(on_create_view_rollback,
						      select);
			if (on_rollback_view == NULL)
				return -1;
			txn_stmt_on_rollback(stmt, on_rollback_view);
			select_guard.is_active = false;
		}
	} else if (new_tuple == NULL) { /* DELETE */
		if (access_check_ddl(old_space->def->name, old_space->def->uid,
				     old_space->access, SC_SPACE, PRIV_D) != 0)
			return -1;
		/* Verify that the space is empty (has no indexes) */
		if (old_space->index_count) {
			diag_set(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "the space has indexes");
			return -1;
		}
		bool out;
		if (schema_find_grants("space", old_space->def->id, &out) != 0) {
			return -1;
		}
		if (out) {
			diag_set(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "the space has grants");
			return -1;
		}
		if (space_has_data(BOX_TRUNCATE_ID, 0, old_space->def->id, &out) != 0)
			return -1;
		if (out) {
			diag_set(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "the space has truncate record");
			return -1;
		}
		if (old_space->def->view_ref_count > 0) {
			diag_set(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "other views depend on this space");
			return -1;
		}
		/* Check whether old_space is used somewhere. */
		if (space_check_pinned(old_space) != 0)
			return -1;
		/* One can't just remove a system space. */
		if (!dd_check_is_disabled() &&
		    space_is_system(old_space)) {
			diag_set(ClientError, ER_DROP_SPACE,
				 space_name(old_space),
				 "the space is a system space");
			return -1;
		}
		/**
		 * We need to unpin spaces that are referenced by deleted one.
		 * Let's detach space constraints - they will be deleted
		 * on commit or reattached on rollback.
		 */
		space_detach_constraints(old_space);
		space_unpin_collations(old_space);
		space_unpin_defaults(old_space);
		/**
		 * The space must be deleted from the space
		 * cache right away to achieve linearisable
		 * execution on a replica.
		 */
		space_cache_replace(old_space, NULL);
		/*
		 * Do not forget to update schema_version right after
		 * deleting the space from the space_cache, since no
		 * AlterSpaceOps are registered in case of space drop.
		 */
		box_schema_version_bump();
		struct trigger *on_commit =
			txn_alter_trigger_new(on_drop_space_commit, old_space);
		if (on_commit == NULL)
			return -1;
		txn_stmt_on_commit(stmt, on_commit);
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_drop_space_rollback, old_space);
		if (on_rollback == NULL)
			return -1;
		txn_stmt_on_rollback(stmt, on_rollback);
		if (old_space->def->opts.is_view) {
			struct Select *select =
				sql_view_compile(old_space->def->opts.sql);
			if (select == NULL)
				return -1;
			auto select_guard = make_scoped_guard([=] {
				sql_select_delete(select);
			});
			struct trigger *on_commit_view =
				txn_alter_trigger_new(on_drop_view_commit,
						      select);
			if (on_commit_view == NULL)
				return -1;
			txn_stmt_on_commit(stmt, on_commit_view);
			struct trigger *on_rollback_view =
				txn_alter_trigger_new(on_drop_view_rollback,
						      select);
			if (on_rollback_view == NULL)
				return -1;
			txn_stmt_on_rollback(stmt, on_rollback_view);
			int rc = update_view_references(select, -1);
			assert(rc == 0); (void)rc;
			select_guard.is_active = false;
		}
	} else { /* UPDATE, REPLACE */
		assert(old_space != NULL && new_tuple != NULL);
		if (old_space->def->opts.is_view) {
			diag_set(ClientError, ER_ALTER_SPACE,
				 space_name(old_space),
				 "view can not be altered");
			return -1;
		}
		if (def == NULL)
			return -1;
		auto def_guard =
			make_scoped_guard([=] { space_def_delete(def); });
		if (access_check_ddl(def->name, def->uid, old_space->access,
				     SC_SPACE, PRIV_A) != 0)
			return -1;
		if (def->id != space_id(old_space)) {
			diag_set(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "space id is immutable");
			return -1;
		}
		if (strcmp(def->engine_name, old_space->def->engine_name) != 0) {
			diag_set(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "can not change space engine");
			return -1;
		}
		if (def->opts.group_id != space_group_id(old_space)) {
			diag_set(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "replication group is immutable");
			return -1;
		}
		if (space_is_temporary(old_space) !=
		     space_opts_is_temporary(&def->opts)) {
			diag_set(ClientError, ER_ALTER_SPACE,
				 old_space->def->name,
				 "temporariness cannot change");
			return -1;
		}
		if (def->opts.is_view != old_space->def->opts.is_view) {
			diag_set(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "can not convert a space to "
				  "a view and vice versa");
			return -1;
		}
		if (strcmp(def->name, old_space->def->name) != 0 &&
		    old_space->def->view_ref_count > 0) {
			diag_set(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "can not rename space which is referenced by "
				  "view");
			return -1;
		}

		if (space_check_alter(old_space, def) != 0)
			return -1;

		/*
		 * Allow change of space properties, but do it
		 * in WAL-error-safe mode.
		 */
		struct alter_space *alter = alter_space_new(old_space);
		if (alter == NULL)
			return -1;
		auto alter_guard =
			make_scoped_guard([=] {alter_space_delete(alter);});
		/*
		 * Calculate a new min_field_count. It can be
		 * changed by resetting space:format(), if an old
		 * format covers some nullable indexed fields in
		 * the format tail. And when the format is reset,
		 * these fields become optional - index
		 * comparators must be updated.
		 */
		struct key_def **keys = NULL;
		RegionGuard region_guard(&fiber()->gc);
		if (old_space->index_count > 0)
			keys = xregion_alloc_array(&fiber()->gc,
						   typeof(keys[0]),
						   old_space->index_count);
		for (uint32_t i = 0; i < old_space->index_count; ++i)
			keys[i] = old_space->index[i]->def->key_def;
		alter->new_min_field_count =
			tuple_format_min_field_count(keys,
						     old_space->index_count,
						     def->fields,
						     def->field_count);
		try {
			(void) new CheckSpaceFormat(alter);
			(void) new ModifySpace(alter, def);
		} catch (Exception *e) {
			return -1;
		}
		def_guard.is_active = false;
		/* Create MoveIndex ops for all space indexes. */
		if (alter_space_move_indexes(alter, 0,
		    old_space->index_id_max + 1) != 0)
			return -1;
		try {
			/* Remember to update schema_version. */
			(void) new UpdateSchemaVersion(alter);
			alter_space_do(stmt, alter);
		} catch (Exception *e) {
			return -1;
		}
		alter_guard.is_active = false;
	}
	return 0;
}

/**
 * Just like with _space, 3 major cases:
 *
 * - insert a tuple = addition of a new index. The
 *   space should exist.
 *
 * - delete a tuple - drop index.
 *
 * - update a tuple - change of index type or key parts.
 *   Change of index type is the same as deletion of the old
 *   index and addition of the new one.
 *
 *   A new index needs to be built before we attempt to commit
 *   a record to the write ahead log, since:
 *
 *   1) if it fails, it's not good to end up with a corrupt index
 *   which is already committed to WAL
 *
 *   2) Tarantool indexes also work as constraints (min number of
 *   fields in the space, field uniqueness), and it's not good to
 *   commit to WAL a constraint which is not enforced in the
 *   current data set.
 *
 *   When adding a new index, ideally we'd also need to rebuild
 *   all tuple formats in all tuples, since the old format may not
 *   be ideal for the new index. We, however, do not do that,
 *   since that would entail rebuilding all indexes at once.
 *   Instead, the default tuple format of the space is changed,
 *   and as tuples get updated/replaced, all tuples acquire a new
 *   format.
 *
 *   The same is the case with dropping an index: nothing is
 *   rebuilt right away, but gradually the extra space reserved
 *   for offsets is relinquished to the slab allocator as tuples
 *   are modified.
 */
static int
on_replace_dd_index(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	uint32_t id, iid;
	if (tuple_field_u32(old_tuple ? old_tuple : new_tuple,
			    BOX_INDEX_FIELD_SPACE_ID, &id) != 0)
		return -1;
	if (tuple_field_u32(old_tuple ? old_tuple : new_tuple,
			    BOX_INDEX_FIELD_ID, &iid) != 0)
		return -1;
	struct space *old_space = space_cache_find(id);
	if (old_space == NULL)
		return -1;
	if (filter_temporary_ddl_stmt(txn, old_space->def) != 0)
		return -1;
	if (old_space->def->opts.is_view) {
		diag_set(ClientError, ER_ALTER_SPACE, space_name(old_space),
			  "can not add index on a view");
		return -1;
	}
	enum priv_type priv_type = new_tuple ? PRIV_C : PRIV_D;
	if (old_tuple && new_tuple)
		priv_type = PRIV_A;
	if (access_check_ddl(old_space->def->name, old_space->def->uid,
			     old_space->access, SC_SPACE, priv_type) != 0)
		return -1;
	struct index *old_index = space_index(old_space, iid);

	/*
	 * Deal with various cases of dropping of the primary key.
	 */
	if (iid == 0 && new_tuple == NULL) {
		/*
		 * Dropping the primary key in a system space: off limits.
		 */
		if (!dd_check_is_disabled() &&
		    space_is_system(old_space)) {
			diag_set(ClientError, ER_LAST_DROP,
				  space_name(old_space));
			return -1;
		}
		/*
		 * Can't drop primary key before secondary keys.
		 */
		if (old_space->index_count > 1) {
			diag_set(ClientError, ER_DROP_PRIMARY_KEY,
				  space_name(old_space));
			return -1;
		}
		/*
		 * Can't drop primary key before space sequence.
		 */
		if (old_space->sequence != NULL) {
			diag_set(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "can not drop primary key while "
				  "space sequence exists");
			return -1;
		}
		/*
		 * Check space's holders.
		 */
		if (space_check_truncate(old_space) != 0)
			return -1;
	}

	if (iid != 0 && space_index(old_space, 0) == NULL) {
		/*
		 * A secondary index can not be created without
		 * a primary key.
		 */
		diag_set(ClientError, ER_ALTER_SPACE,
			  space_name(old_space),
			  "can not add a secondary key before primary");
		return -1;
	}

	struct alter_space *alter = alter_space_new(old_space);
	if (alter == NULL)
		return -1;
	auto scoped_guard =
		make_scoped_guard([=] { alter_space_delete(alter); });

	/*
	 * Handle the following 4 cases:
	 * 1. Simple drop of an index.
	 * 2. Creation of a new index: primary or secondary.
	 * 3. Change of an index which does not require a rebuild.
	 * 4. Change of an index which does require a rebuild.
	 */
	/* Case 1: drop the index, if it is dropped. */
	if (old_index != NULL && new_tuple == NULL) {
		if (alter_space_move_indexes(alter, 0, iid) != 0)
			return -1;
		try {
			(void) new DropIndex(alter, old_index);
		} catch (Exception *e) {
			return -1;
		}
	}
	/* Case 2: create an index, if it is simply created. */
	if (old_index == NULL && new_tuple != NULL) {
		if (alter_space_move_indexes(alter, 0, iid))
			return -1;
		struct index_def *def =
			index_def_new_from_tuple(new_tuple, old_space);
		if (def == NULL)
			return -1;
		index_def_update_optionality(def, alter->new_min_field_count);
		try {
			(void) new CreateIndex(alter, def);
		} catch (Exception *e) {
			index_def_delete(def);
			return -1;
		}
	}
	/* Case 3 and 4: check if we need to rebuild index data. */
	if (old_index != NULL && new_tuple != NULL) {
		struct index_def *index_def;
		index_def = index_def_new_from_tuple(new_tuple, old_space);
		if (index_def == NULL)
			return -1;
		auto index_def_guard =
			make_scoped_guard([=] { index_def_delete(index_def); });
		/*
		 * To detect which key parts are optional,
		 * min_field_count is required. But
		 * min_field_count from the old space format can
		 * not be used. For example, consider the case,
		 * when a space has no format, has a primary index
		 * on the first field and has a single secondary
		 * index on a non-nullable second field. Min field
		 * count here is 2. Now alter the secondary index
		 * to make its part be nullable. In the
		 * 'old_space' min_field_count is still 2, but
		 * actually it is already 1. Actual
		 * min_field_count must be calculated using old
		 * unchanged indexes, NEW definition of an updated
		 * index and a space format, defined by a user.
		 */
		struct key_def **keys;
		size_t bsize;
		RegionGuard region_guard(&fiber()->gc);
		keys = region_alloc_array(&fiber()->gc, typeof(keys[0]),
					  old_space->index_count, &bsize);
		if (keys == NULL) {
			diag_set(OutOfMemory, bsize, "region_alloc_array",
				 "keys");
			return -1;
		}
		for (uint32_t i = 0, j = 0; i < old_space->index_count; ++i) {
			struct index_def *d = old_space->index[i]->def;
			if (d->iid != index_def->iid)
				keys[j++] = d->key_def;
			else
				keys[j++] = index_def->key_def;
		}
		struct space_def *def = old_space->def;
		alter->new_min_field_count =
			tuple_format_min_field_count(keys,
						     old_space->index_count,
						     def->fields,
						     def->field_count);
		index_def_update_optionality(index_def,
					     alter->new_min_field_count);
		if (alter_space_move_indexes(alter, 0, iid))
			return -1;
		if (index_def_cmp(index_def, old_index->def) == 0) {
			/* Index is not changed so just move it. */
			try {
				(void) new MoveIndex(alter, old_index->def->iid);
			} catch (Exception *e) {
				return -1;
			}

		} else if (index_def_change_requires_rebuild(old_index,
							     index_def)) {
			/*
			 * Operation demands an index rebuild.
			 */
			try {
				(void) new RebuildIndex(alter, index_def,
							old_index->def);
			} catch (Exception *e) {
				return -1;
			}
			index_def_guard.is_active = false;
		} else {
			/*
			 * Operation can be done without index rebuild,
			 * but we still need to check that tuples stored
			 * in the space conform to the new format.
			 */
			try {
				(void) new CheckSpaceFormat(alter);
				(void) new ModifyIndex(alter, old_index, index_def);
			} catch (Exception *e) {
				return -1;
			}
			index_def_guard.is_active = false;
		}
	}
	/*
	 * Create MoveIndex ops for the remaining indexes in the
	 * old space.
	 */
	if (alter_space_move_indexes(alter, iid + 1, old_space->index_id_max + 1) != 0)
		return -1;
	try {
		/* Add an op to update schema_version on commit. */
		(void) new UpdateSchemaVersion(alter);
		alter_space_do(stmt, alter);
	} catch (Exception *e) {
		return -1;
	}
	scoped_guard.is_active = false;
	return 0;
}

/**
 * A trigger invoked on replace in space _truncate.
 *
 * In a nutshell, we truncate a space by replacing it with
 * a new empty space with the same definition and indexes.
 * Note, although we instantiate the new space before WAL
 * write, we don't propagate changes to the old space in
 * case a WAL write error happens and we have to rollback.
 * This is OK, because a WAL write error implies cascading
 * rollback of all transactions following this one.
 */
static int
on_replace_dd_truncate(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *new_tuple = stmt->new_tuple;

	if (recovery_state == INITIAL_RECOVERY) {
		/* Space creation during initial recovery - nothing to do. */
		return 0;
	}

	struct tuple *any_tuple = new_tuple;
	if (any_tuple == NULL)
		any_tuple = stmt->old_tuple;
	uint32_t space_id;
	if (tuple_field_u32(any_tuple, BOX_TRUNCATE_FIELD_SPACE_ID,
			    &space_id) != 0)
		return -1;
	struct space *old_space = space_cache_find(space_id);
	if (old_space == NULL)
		return -1;
	if (space_is_temporary(old_space))
		txn_stmt_mark_as_temporary(txn, stmt);

	if (new_tuple == NULL) {
		/* Space drop - nothing else to do. */
		return 0;
	}

	/*
	 * box_process1() bypasses the read-only check for the _truncate system
	 * space because there the space that is going to be truncated isn't yet
	 * known. Perform the check here if this statement was issued by this
	 * replica and the space isn't data-temporary or local.
	 */
	bool is_temp = space_is_data_temporary(old_space) ||
		       space_is_local(old_space);
	if (!is_temp && stmt->row->replica_id == 0 &&
	    box_check_writable() != 0)
		return -1;

	/*
	 * Check if a write privilege was given, return an error if not.
	 * The check should precede initial recovery check to correctly
	 * handle direct insert into _truncate systable.
	 */
	if (access_check_space(old_space, PRIV_W) != 0)
		return -1;

	/*
	 * System spaces use triggers to keep records in sync
	 * with internal objects. Since space truncation doesn't
	 * invoke triggers, we don't permit it for system spaces.
	 */
	if (space_is_system(old_space)) {
		diag_set(ClientError, ER_TRUNCATE_SYSTEM_SPACE,
			  space_name(old_space));
		return -1;
	}

	/* Check space's holders. */
	if (space_check_truncate(old_space) != 0)
		return -1;

	struct alter_space *alter = alter_space_new(old_space);
	if (alter == NULL)
		return -1;
	auto scoped_guard =
		make_scoped_guard([=] { alter_space_delete(alter); });

	/*
	 * Modify the WAL header to prohibit
	 * replication of local & data-temporary
	 * spaces truncation
	 * unless it's a temporary space
	 * in which case the header doesn't exist.
	 */
	if (is_temp && !space_is_temporary(old_space)) {
		stmt->row->group_id = GROUP_LOCAL;
		/*
		 * The trigger is invoked after txn->n_local_rows
		 * is counted, so don't forget to update it here.
		 */
		++txn->n_local_rows;
	}

	try {
		/*
		 * Recreate all indexes of the truncated space.
		 */
		for (uint32_t i = 0; i < old_space->index_count; i++) {
			struct index *old_index = old_space->index[i];
			(void) new TruncateIndex(alter, old_index->def->iid);
		}

		alter_space_do(stmt, alter);
	} catch (Exception *e) {
		return -1;
	}
	scoped_guard.is_active = false;
	return 0;
}

/* {{{ access control */

int
user_has_data(struct user *user, bool *has_data)
{
	uint32_t uid = user->def->uid;
	uint32_t spaces[] = { BOX_SPACE_ID, BOX_FUNC_ID, BOX_SEQUENCE_ID,
			      BOX_PRIV_ID, BOX_PRIV_ID };
	/*
	 * owner index id #1 for _space and _func and _priv.
	 * For _priv also check that the user has no grants.
	 */
	uint32_t indexes[] = { 1, 1, 1, 1, 0 };
	uint32_t count = sizeof(spaces)/sizeof(*spaces);
	bool out;
	for (uint32_t i = 0; i < count; i++) {
		if (space_has_data(spaces[i], indexes[i], uid, &out) != 0)
			return -1;
		if (out) {
			*has_data = true;
			return 0;
		}
	}
	if (! user_map_is_empty(&user->users)) {
		*has_data = true;
		return 0;
	}
	/*
	 * If there was a role, the previous check would have
	 * returned true.
	 */
	assert(user_map_is_empty(&user->roles));
	*has_data = false;
	return 0;
}

/**
 * Initialize the user authenticator from the _user space data.
 */
static int
user_def_fill_auth_data(struct user_def *user, const char *auth_data)
{
	uint8_t type = mp_typeof(*auth_data);
	if (type == MP_ARRAY || type == MP_NIL) {
		/*
		 * Nothing useful.
		 * MP_ARRAY is a special case since Lua arrays are
		 * indistinguishable from tables, so an empty
		 * table may well be encoded as an msgpack array.
		 * Treat as no data.
		 */
		return 0;
	}
	if (mp_typeof(*auth_data) != MP_MAP) {
		/** Prevent users from making silly mistakes */
		diag_set(ClientError, ER_CREATE_USER,
			  user->name, "invalid password format, "
			  "use box.schema.user.passwd() to reset password");
		return -1;
	}
	uint32_t method_count = mp_decode_map(&auth_data);
	for (uint32_t i = 0; i < method_count; i++) {
		if (mp_typeof(*auth_data) != MP_STR) {
			mp_next(&auth_data);
			mp_next(&auth_data);
			continue;
		}
		uint32_t method_name_len;
		const char *method_name = mp_decode_str(&auth_data,
							&method_name_len);
		const char *auth_data_end = auth_data;
		mp_next(&auth_data_end);
		const struct auth_method *method = auth_method_by_name(
						method_name, method_name_len);
		if (method == NULL) {
			auth_data = auth_data_end;
			continue;
		}
		struct authenticator *auth = authenticator_new(
				method, auth_data, auth_data_end);
		if (auth == NULL)
			return -1;
		/* The guest user may only have an empty password. */
		if (user->uid == GUEST &&
		    !authenticate_password(auth, "", 0)) {
			authenticator_delete(auth);
			diag_set(ClientError, ER_GUEST_USER_PASSWORD);
			return -1;
		}
		user->auth = auth;
		break;
	}
	return 0;
}

static struct user_def *
user_def_new_from_tuple(struct tuple *tuple)
{
	uint32_t name_len;
	const char *name = tuple_field_str(tuple, BOX_USER_FIELD_NAME,
					   &name_len);
	if (name == NULL)
		return NULL;
	if (name_len > BOX_NAME_MAX) {
		diag_set(ClientError, ER_CREATE_USER,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  "user name is too long");
		return NULL;
	}
	uint32_t uid;
	if (tuple_field_u32(tuple, BOX_USER_FIELD_ID, &uid) != 0)
		return NULL;
	uint32_t owner;
	if (tuple_field_u32(tuple, BOX_USER_FIELD_UID, &owner) != 0)
		return NULL;
	const char *type_str = tuple_field_cstr(tuple, BOX_USER_FIELD_TYPE);
	if (type_str == NULL)
		return NULL;
	enum schema_object_type type = schema_object_type(type_str);
	if (type != SC_ROLE && type != SC_USER) {
		diag_set(ClientError, ER_CREATE_USER,
			 tt_cstr(name, name_len), "unknown user type");
		return NULL;
	}
	if (identifier_check(name, name_len) != 0)
		return NULL;
	struct user_def *user = user_def_new(uid, owner, type, name, name_len);
	auto def_guard = make_scoped_guard([=] { user_def_delete(user); });
	/*
	 * AUTH_DATA field in _user space should contain
	 * chap-sha1 -> base64_encode(sha1(sha1(password), 0).
	 * Check for trivial errors when a plain text
	 * password is saved in this field instead.
	 */
	if (tuple_field_count(tuple) > BOX_USER_FIELD_AUTH) {
		const char *auth_data = tuple_field(tuple, BOX_USER_FIELD_AUTH);
		const char *tmp = auth_data;
		bool is_auth_empty;
		if (mp_typeof(*auth_data) == MP_ARRAY &&
		    mp_decode_array(&tmp) == 0) {
			is_auth_empty = true;
		} else if (mp_typeof(*auth_data) == MP_MAP &&
			   mp_decode_map(&tmp) == 0) {
			is_auth_empty = true;
		} else {
			is_auth_empty = false;
		}
		if (!is_auth_empty && user->type == SC_ROLE) {
			diag_set(ClientError, ER_CREATE_ROLE, user->name,
				  "authentication data can not be set for a "\
				  "role");
			return NULL;
		}
		if (user_def_fill_auth_data(user, auth_data) != 0)
			return NULL;
	}
	if (tuple_field_count(tuple) > BOX_USER_FIELD_LAST_MODIFIED &&
	    tuple_field_u64(tuple, BOX_USER_FIELD_LAST_MODIFIED,
			    &user->last_modified) != 0)
		return NULL;
	def_guard.is_active = false;
	return user;
}

static int
user_cache_remove_user(struct trigger *trigger, void * /* event */)
{
	struct tuple *tuple = (struct tuple *)trigger->data;
	uint32_t uid;
	if (tuple_field_u32(tuple, BOX_USER_FIELD_ID, &uid) != 0)
		return -1;
	user_cache_delete(uid);
	return 0;
}

static int
user_cache_alter_user(struct trigger *trigger, void * /* event */)
{
	struct tuple *tuple = (struct tuple *)trigger->data;
	struct user_def *user = user_def_new_from_tuple(tuple);
	if (user == NULL)
		return -1;
	auto def_guard = make_scoped_guard([=] { user_def_delete(user); });
	/* Can throw if, e.g. too many users. */
	try {
		user_cache_replace(user);
	} catch (Exception *e) {
		return -1;
	}
	def_guard.is_active = false;
	return 0;
}

/**
 * A trigger invoked on replace in the user table.
 */
static int
on_replace_dd_user(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	uint32_t uid;
	if (tuple_field_u32(old_tuple ? old_tuple : new_tuple,
			    BOX_USER_FIELD_ID, &uid) != 0)
		return -1;
	struct user *old_user = user_by_id(uid);
	if (new_tuple != NULL && old_user == NULL) { /* INSERT */
		struct user_def *user = user_def_new_from_tuple(new_tuple);
		if (user == NULL)
			return -1;
		if (access_check_ddl(user->name, user->owner, NULL,
				     user->type, PRIV_C) != 0)
			return -1;
		auto def_guard = make_scoped_guard([=] {
			user_def_delete(user);
		});
		try {
			(void) user_cache_replace(user);
		} catch (Exception *e) {
			return -1;
		}
		def_guard.is_active = false;
		struct trigger *on_rollback =
			txn_alter_trigger_new(user_cache_remove_user, new_tuple);
		if (on_rollback == NULL)
			return -1;
		txn_stmt_on_rollback(stmt, on_rollback);
	} else if (new_tuple == NULL) { /* DELETE */
		if (access_check_ddl(old_user->def->name, old_user->def->owner,
				     old_user->access, old_user->def->type,
				     PRIV_D) != 0)
			return -1;
		/* Can't drop guest or super user */
		if (uid <= (uint32_t) BOX_SYSTEM_USER_ID_MAX || uid == SUPER) {
			diag_set(ClientError, ER_DROP_USER,
				  old_user->def->name,
				  "the user or the role is a system");
			return -1;
		}
		/*
		 * Can only delete user if it has no spaces,
		 * no functions and no grants.
		 */
		bool has_data;
		if (user_has_data(old_user, &has_data) != 0) {
			return -1;
		}
		if (has_data) {
			diag_set(ClientError, ER_DROP_USER,
				  old_user->def->name, "the user has objects");
			return -1;
		}
		user_cache_delete(uid);
		struct trigger *on_rollback =
			txn_alter_trigger_new(user_cache_alter_user, old_tuple);
		if (on_rollback == NULL)
			return -1;
		txn_stmt_on_rollback(stmt, on_rollback);
	} else { /* UPDATE, REPLACE */
		assert(old_user != NULL && new_tuple != NULL);
		/*
		 * Allow change of user properties (name,
		 * password) but first check that the change is
		 * correct.
		 */
		struct user_def *user = user_def_new_from_tuple(new_tuple);
		if (user == NULL)
			return -1;
		if (access_check_ddl(user->name, user->uid, old_user->access,
				     old_user->def->type, PRIV_A) != 0)
			return -1;
		auto def_guard = make_scoped_guard([=] {
			user_def_delete(user);
		});
		try {
			user_cache_replace(user);
		} catch (Exception *e) {
			return -1;
		}
		def_guard.is_active = false;
		struct trigger *on_rollback =
			txn_alter_trigger_new(user_cache_alter_user, old_tuple);
		if (on_rollback == NULL)
			return -1;
		txn_stmt_on_rollback(stmt, on_rollback);
	}
	return 0;
}

/**
 * Get function identifiers from a tuple.
 *
 * @param tuple Tuple to get ids from.
 * @param[out] fid Function identifier.
 * @param[out] uid Owner identifier.
 */
static inline int
func_def_get_ids_from_tuple(struct tuple *tuple, uint32_t *fid, uint32_t *uid)
{
	if (tuple_field_u32(tuple, BOX_FUNC_FIELD_ID, fid) != 0)
		return -1;
	return tuple_field_u32(tuple, BOX_FUNC_FIELD_UID, uid);
}

/** Create a function definition from tuple. */
static struct func_def *
func_def_new_from_tuple(struct tuple *tuple)
{
	uint32_t field_count = tuple_field_count(tuple);
	uint32_t name_len;
	const char *name = tuple_field_str(tuple, BOX_FUNC_FIELD_NAME,
					   &name_len);
	if (name == NULL)
		return NULL;
	if (name_len > BOX_NAME_MAX) {
		diag_set(ClientError, ER_CREATE_FUNCTION,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  "function name is too long");
		return NULL;
	}
	if (identifier_check(name, name_len) != 0)
		return NULL;
	enum func_language language = FUNC_LANGUAGE_LUA;
	if (field_count > BOX_FUNC_FIELD_LANGUAGE) {
		const char *language_str =
			tuple_field_cstr(tuple, BOX_FUNC_FIELD_LANGUAGE);
		if (language_str == NULL)
			return NULL;
		language = STR2ENUM(func_language, language_str);
		/*
		 * 'SQL_BUILTIN' was dropped in 2.9, but to support upgrade
		 * from previous versions, we allow to create such functions.
		 */
		if (language == func_language_MAX ||
		    language == FUNC_LANGUAGE_SQL ||
		    (language == FUNC_LANGUAGE_SQL_BUILTIN &&
		     !dd_check_is_disabled())) {
			diag_set(ClientError, ER_FUNCTION_LANGUAGE,
				 language_str, tt_cstr(name, name_len));
			return NULL;
		}
	}
	uint32_t body_len = 0;
	const char *body = NULL;
	uint32_t comment_len = 0;
	const char *comment = NULL;
	if (field_count > BOX_FUNC_FIELD_BODY) {
		body = tuple_field_str(tuple, BOX_FUNC_FIELD_BODY, &body_len);
		if (body == NULL)
			return NULL;
		comment = tuple_field_str(tuple, BOX_FUNC_FIELD_COMMENT,
					  &comment_len);
		if (comment == NULL)
			return NULL;
		uint32_t len;
		const char *routine_type = tuple_field_str(tuple,
					BOX_FUNC_FIELD_ROUTINE_TYPE, &len);
		if (routine_type == NULL)
			return NULL;
		if (len != strlen("function") ||
		    strncasecmp(routine_type, "function", len) != 0) {
			diag_set(ClientError, ER_CREATE_FUNCTION, name,
				  "unsupported routine_type value");
			return NULL;
		}
		const char *sql_data_access = tuple_field_str(tuple,
					BOX_FUNC_FIELD_SQL_DATA_ACCESS, &len);
		if (sql_data_access == NULL)
			return NULL;
		if (len != strlen("none") ||
		    strncasecmp(sql_data_access, "none", len) != 0) {
			diag_set(ClientError, ER_CREATE_FUNCTION, name,
				  "unsupported sql_data_access value");
			return NULL;
		}
		bool is_null_call;
		if (tuple_field_bool(tuple, BOX_FUNC_FIELD_IS_NULL_CALL,
				     &is_null_call) != 0)
			return NULL;
		if (is_null_call != true) {
			diag_set(ClientError, ER_CREATE_FUNCTION, name,
				  "unsupported is_null_call value");
			return NULL;
		}
	}
	uint32_t fid, uid;
	if (func_def_get_ids_from_tuple(tuple, &fid, &uid) != 0)
		return NULL;
	if (fid > BOX_FUNCTION_MAX) {
		diag_set(ClientError, ER_CREATE_FUNCTION,
			  tt_cstr(name, name_len), "function id is too big");
		return NULL;
	}
	struct func_def *def = func_def_new(fid, uid, name, name_len,
					    language, body, body_len,
					    comment, comment_len);
	auto def_guard = make_scoped_guard([=] { func_def_delete(def); });
	if (field_count > BOX_FUNC_FIELD_SETUID) {
		uint32_t out;
		if (tuple_field_u32(tuple, BOX_FUNC_FIELD_SETUID, &out) != 0)
			return NULL;
		def->setuid = out;
	}
	if (field_count > BOX_FUNC_FIELD_BODY) {
		if (tuple_field_bool(tuple, BOX_FUNC_FIELD_IS_DETERMINISTIC,
				     &(def->is_deterministic)) != 0)
			return NULL;
		if (tuple_field_bool(tuple, BOX_FUNC_FIELD_IS_SANDBOXED,
				     &(def->is_sandboxed)) != 0)
			return NULL;
		const char *returns =
			tuple_field_cstr(tuple, BOX_FUNC_FIELD_RETURNS);
		if (returns == NULL)
			return NULL;
		def->returns = STR2ENUM(field_type, returns);
		if (def->returns == field_type_MAX) {
			diag_set(ClientError, ER_CREATE_FUNCTION,
				  def->name, "invalid returns value");
			return NULL;
		}
		def->exports.all = 0;
		const char *exports = tuple_field_with_type(tuple,
			BOX_FUNC_FIELD_EXPORTS, MP_ARRAY);
		if (exports == NULL)
			return NULL;
		uint32_t cnt = mp_decode_array(&exports);
		for (uint32_t i = 0; i < cnt; i++) {
			enum mp_type actual_type = mp_typeof(*exports);
			if (actual_type != MP_STR) {
				diag_set(ClientError, ER_FIELD_TYPE,
					 int2str(BOX_FUNC_FIELD_EXPORTS + 1),
					 mp_type_strs[MP_STR], mp_type_strs[actual_type]);
				return NULL;
			}
			uint32_t len;
			const char *str = mp_decode_str(&exports, &len);
			switch (STRN2ENUM(func_language, str, len)) {
			case FUNC_LANGUAGE_LUA:
				def->exports.lua = true;
				break;
			case FUNC_LANGUAGE_SQL:
				def->exports.sql = true;
				break;
			default:
				diag_set(ClientError, ER_CREATE_FUNCTION,
					  def->name, "invalid exports value");
				return NULL;
			}
		}
		const char *aggregate =
			tuple_field_cstr(tuple, BOX_FUNC_FIELD_AGGREGATE);
		if (aggregate == NULL)
			return NULL;
		def->aggregate = STR2ENUM(func_aggregate, aggregate);
		if (def->aggregate == func_aggregate_MAX) {
			diag_set(ClientError, ER_CREATE_FUNCTION,
				  def->name, "invalid aggregate value");
			return NULL;
		}
		if (def->aggregate == FUNC_AGGREGATE_GROUP &&
		    def->exports.lua) {
			diag_set(ClientError, ER_CREATE_FUNCTION, def->name,
				 "aggregate function can only be accessed in "
				 "SQL");
			return NULL;
		}
		const char *param_list = tuple_field_with_type(tuple,
			BOX_FUNC_FIELD_PARAM_LIST, MP_ARRAY);
		if (param_list == NULL)
			return NULL;
		uint32_t argc = mp_decode_array(&param_list);
		for (uint32_t i = 0; i < argc; i++) {
			enum mp_type actual_type = mp_typeof(*param_list);
			if (actual_type != MP_STR) {
				diag_set(ClientError, ER_FIELD_TYPE,
					 int2str(BOX_FUNC_FIELD_PARAM_LIST + 1),
					 mp_type_strs[MP_STR],
					 mp_type_strs[actual_type]);
				return NULL;
			}
			uint32_t len;
			const char *str = mp_decode_str(&param_list, &len);
			if (STRN2ENUM(field_type, str, len) == field_type_MAX) {
				diag_set(ClientError, ER_CREATE_FUNCTION,
					  def->name, "invalid argument type");
				return NULL;
			}
		}
		if (def->aggregate == FUNC_AGGREGATE_GROUP && argc == 0) {
			diag_set(ClientError, ER_CREATE_FUNCTION, def->name,
				 "aggregate function must have at least one "
				 "argument");
			return NULL;
		}
		def->param_count = argc;
		const char *opts = tuple_field(tuple, BOX_FUNC_FIELD_OPTS);
		if (opts_decode(&def->opts, func_opts_reg, &opts, NULL) != 0) {
			diag_set(ClientError, ER_WRONG_FUNCTION_OPTIONS,
				 diag_last_error(diag_get())->errmsg);
			return NULL;
		}
	} else {
		/* By default export to Lua, but not other frontends. */
		def->exports.lua = true;
	}
	if (field_count > BOX_FUNC_FIELD_TRIGGER) {
		const char *triggers =
			tuple_field_with_type(tuple, BOX_FUNC_FIELD_TRIGGER,
					      MP_ARRAY);
		if (triggers == NULL)
			return NULL;
		const char *triggers_begin = triggers;
		uint32_t trigger_count = mp_decode_array(&triggers);
		for (uint32_t i = 0; i < trigger_count; i++) {
			enum mp_type actual_type = mp_typeof(*triggers);
			if (actual_type != MP_STR) {
				diag_set(ClientError, ER_FIELD_TYPE,
					 int2str(BOX_FUNC_FIELD_TRIGGER + 1),
					 mp_type_strs[MP_STR],
					 mp_type_strs[actual_type]);
				return NULL;
			}
			mp_next(&triggers);
		};
		/** Set a field only if array is not empty. */
		if (trigger_count > 0)
			def->triggers = triggers_begin;
	}
	if (func_def_check(def) != 0)
		return NULL;
	def_guard.is_active = false;
	return def;
}

static int
on_create_func_rollback(struct trigger *trigger, void * /* event */)
{
	/* Remove the new function from the cache and delete it. */
	struct func *func = (struct func *)trigger->data;
	func_cache_delete(func->def->fid);
	if (trigger_run(&on_alter_func, func) != 0)
		return -1;
	func_delete(func);
	return 0;
}

static int
on_drop_func_commit(struct trigger *trigger, void * /* event */)
{
	/* Delete the old function. */
	struct func *func = (struct func *)trigger->data;
	func_delete(func);
	return 0;
}

static int
on_drop_func_rollback(struct trigger *trigger, void * /* event */)
{
	/* Insert the old function back into the cache. */
	struct func *func = (struct func *)trigger->data;
	func_cache_insert(func);
	if (trigger_run(&on_alter_func, func) != 0)
		return -1;
	return 0;
}

/**
 * A trigger invoked on replace in a space containing
 * functions on which there were defined any grants.
 */
static int
on_replace_dd_func(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	uint32_t fid;
	if (tuple_field_u32(old_tuple ? old_tuple : new_tuple,
			    BOX_FUNC_FIELD_ID, &fid) != 0)
		return -1;
	struct func *old_func = func_by_id(fid);
	if (new_tuple != NULL && old_func == NULL) { /* INSERT */
		struct func_def *def = func_def_new_from_tuple(new_tuple);
		if (def == NULL)
			return -1;
		auto def_guard = make_scoped_guard([=] {
			func_def_delete(def);
		});
		if (access_check_ddl(def->name, def->uid, NULL,
				     SC_FUNCTION, PRIV_C) != 0)
			return -1;
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_create_func_rollback, NULL);
		if (on_rollback == NULL)
			return -1;
		struct func *func = func_new(def);
		if (func == NULL)
			return -1;
		func_cache_insert(func);
		on_rollback->data = func;
		txn_stmt_on_rollback(stmt, on_rollback);
		if (trigger_run(&on_alter_func, func) != 0)
			return -1;
	} else if (new_tuple == NULL) {         /* DELETE */
		uint32_t uid;
		if (func_def_get_ids_from_tuple(old_tuple, &fid, &uid) != 0)
			return -1;
		/*
		 * Can only delete func if you're the one
		 * who created it or a superuser.
		 */
		if (access_check_ddl(old_func->def->name, uid, old_func->access,
				     SC_FUNCTION, PRIV_D) != 0)
			return -1;
		/* Can only delete func if it has no grants. */
		bool out;
		if (schema_find_grants("function", old_func->def->fid, &out) != 0) {
			return -1;
		}
		if (out) {
			diag_set(ClientError, ER_DROP_FUNCTION,
				 (unsigned)old_func->def->fid,
				 "function has grants");
			return -1;
		}
		if (space_has_data(BOX_FUNC_INDEX_ID, 1, old_func->def->fid, &out) != 0)
			return -1;
		if (old_func != NULL && out) {
			diag_set(ClientError, ER_DROP_FUNCTION,
				 (unsigned)old_func->def->fid,
				 "function has references");
			return -1;
		}
		/* Check whether old_func is used somewhere. */
		enum func_holder_type pinned_type;
		if (func_is_pinned(old_func, &pinned_type)) {
			const char *type_str =
				func_cache_holder_type_strs[pinned_type];
			diag_set(ClientError, ER_DROP_FUNCTION,
				 (unsigned)old_func->def->fid,
				 tt_sprintf("function is referenced by %s",
					    type_str));
			return -1;
		}
		struct trigger *on_commit =
			txn_alter_trigger_new(on_drop_func_commit, old_func);
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_drop_func_rollback, old_func);
		if (on_commit == NULL || on_rollback == NULL)
			return -1;
		func_cache_delete(old_func->def->fid);
		txn_stmt_on_commit(stmt, on_commit);
		txn_stmt_on_rollback(stmt, on_rollback);
		if (trigger_run(&on_alter_func, old_func) != 0)
			return -1;
	} else {                                /* UPDATE, REPLACE */
		assert(new_tuple != NULL && old_tuple != NULL);
		/**
		 * Allow an alter that doesn't change the
		 * definition to support upgrade script.
		 */
		struct func_def *old_def = NULL, *new_def = NULL;
		auto guard = make_scoped_guard([&old_def, &new_def] {
			free(old_def);
			free(new_def);
		});
		old_def = func_def_new_from_tuple(old_tuple);
		new_def = func_def_new_from_tuple(new_tuple);
		if (old_def == NULL || new_def == NULL)
			return -1;
		if (func_def_cmp(new_def, old_def) != 0) {
			diag_set(ClientError, ER_UNSUPPORTED, "function",
				  "alter");
			return -1;
		}
	}
	return 0;
}

/** Create a collation identifier definition from tuple. */
int
coll_id_def_new_from_tuple(struct tuple *tuple, struct coll_id_def *def)
{
	memset(def, 0, sizeof(*def));
	uint32_t name_len, locale_len, type_len;
	if (tuple_field_u32(tuple, BOX_COLLATION_FIELD_ID, &(def->id)) != 0)
		return -1;
	def->name = tuple_field_str(tuple, BOX_COLLATION_FIELD_NAME, &name_len);
	if (def->name == NULL)
		return -1;
	def->name_len = name_len;
	if (name_len > BOX_NAME_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "collation name is too long");
		return -1;
	}
	if (identifier_check(def->name, name_len) != 0)
		return -1;
	if (tuple_field_u32(tuple, BOX_COLLATION_FIELD_UID, &(def->owner_id)) != 0)
		return -1;
	const char *type = tuple_field_str(tuple, BOX_COLLATION_FIELD_TYPE,
			       &type_len);
	if (type == NULL)
		return -1;
	struct coll_def *base = &def->base;
	base->type = STRN2ENUM(coll_type, type, type_len);
	if (base->type == coll_type_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "unknown collation type");
		return -1;
	}
	const char *locale = tuple_field_str(tuple, BOX_COLLATION_FIELD_LOCALE,
					     &locale_len);
	if (locale == NULL)
		return -1;
	if (locale_len > COLL_LOCALE_LEN_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "collation locale is too long");
		return -1;
	}
	if (locale_len > 0)
		if (identifier_check(locale, locale_len) != 0)
			return -1;
	snprintf(base->locale, sizeof(base->locale), "%.*s", locale_len,
		 locale);
	const char *options = tuple_field_with_type(tuple,
					BOX_COLLATION_FIELD_OPTIONS, MP_MAP);
	if (options == NULL)
		return -1;
	if (opts_decode(&base->icu, coll_icu_opts_reg, &options, NULL) != 0) {
		diag_set(ClientError, ER_WRONG_COLLATION_OPTIONS,
			 diag_last_error(diag_get())->errmsg);
		return -1;
	}

	if (base->icu.french_collation == coll_icu_on_off_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong french_collation option setting, "
				  "expected ON | OFF");
		return -1;
	}

	if (base->icu.alternate_handling == coll_icu_alternate_handling_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong alternate_handling option setting, "
				  "expected NON_IGNORABLE | SHIFTED");
		return -1;
	}

	if (base->icu.case_first == coll_icu_case_first_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong case_first option setting, "
				  "expected OFF | UPPER_FIRST | LOWER_FIRST");
		return -1;
	}

	if (base->icu.case_level == coll_icu_on_off_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong case_level option setting, "
				  "expected ON | OFF");
		return -1;
	}

	if (base->icu.normalization_mode == coll_icu_on_off_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong normalization_mode option setting, "
				  "expected ON | OFF");
		return -1;
	}

	if (base->icu.strength == coll_icu_strength_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong strength option setting, "
				  "expected PRIMARY | SECONDARY | "
				  "TERTIARY | QUATERNARY | IDENTICAL");
		return -1;
	}

	if (base->icu.numeric_collation == coll_icu_on_off_MAX) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong numeric_collation option setting, "
				  "expected ON | OFF");
		return -1;
	}
	return 0;
}

/** Delete the new collation identifier. */
static int
on_create_collation_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct coll_id *coll_id = (struct coll_id *) trigger->data;
	coll_id_cache_delete(coll_id);
	coll_id_delete(coll_id);
	return 0;
}


/** Free a deleted collation identifier on commit. */
static int
on_drop_collation_commit(struct trigger *trigger, void *event)
{
	(void) event;
	struct coll_id *coll_id = (struct coll_id *) trigger->data;
	coll_id_delete(coll_id);
	return 0;
}

/** Put the collation identifier back on rollback. */
static int
on_drop_collation_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct coll_id *coll_id = (struct coll_id *) trigger->data;
	struct coll_id *replaced_id;
	if (coll_id_cache_replace(coll_id, &replaced_id) != 0)
		panic("Out of memory on insertion into collation cache");
	assert(replaced_id == NULL);
	return 0;
}

/**
 * A trigger invoked on replace in a space containing
 * collations that a user defined.
 */
static int
on_replace_dd_collation(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	if (new_tuple == NULL && old_tuple != NULL) {
		/* DELETE */
		struct trigger *on_commit =
			txn_alter_trigger_new(on_drop_collation_commit, NULL);
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_drop_collation_rollback, NULL);
		if (on_commit == NULL || on_rollback == NULL)
			return -1;
		uint32_t out;
		if (tuple_field_u32(old_tuple, BOX_COLLATION_FIELD_ID, &out) != 0)
			return -1;
		int32_t old_id = out;
		/*
		 * Don't allow user to drop "none" collation
		 * since it is very special and vastly used
		 * under the hood. Hence, we can rely on the
		 * fact that "none" collation features id == 0.
		 */
		if (old_id == COLL_NONE) {
			diag_set(ClientError, ER_DROP_COLLATION, "none",
				  "system collation");
			return -1;
		}
		struct coll_id *old_coll_id = coll_by_id(old_id);
		assert(old_coll_id != NULL);
		if (access_check_ddl(old_coll_id->name, old_coll_id->owner_id,
				     NULL, SC_COLLATION, PRIV_D) != 0)
			return -1;
		/*
		 * Don't allow user to drop a collation identifier that is
		 * currently used.
		 */
		enum coll_id_holder_type pinned_type;
		if (coll_id_is_pinned(old_coll_id, &pinned_type)) {
			const char *type_str =
				coll_id_holder_type_strs[pinned_type];
			diag_set(ClientError, ER_DROP_COLLATION,
				 old_coll_id->name,
				 tt_sprintf("collation is referenced by %s",
					    type_str));
			return -1;
		}
		/*
		 * Set on_commit/on_rollback triggers after
		 * deletion from the cache to make trigger logic
		 * simple.
		 */
		coll_id_cache_delete(old_coll_id);
		on_rollback->data = old_coll_id;
		on_commit->data = old_coll_id;
		txn_stmt_on_rollback(stmt, on_rollback);
		txn_stmt_on_commit(stmt, on_commit);
	} else if (new_tuple != NULL && old_tuple == NULL) {
		/* INSERT */
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_create_collation_rollback, NULL);
		if (on_rollback == NULL)
			return -1;
		struct coll_id_def new_def;
		if (coll_id_def_new_from_tuple(new_tuple, &new_def) != 0)
			return -1;
		if (access_check_ddl(new_def.name, new_def.owner_id,
				     NULL, SC_COLLATION, PRIV_C) != 0)
			return -1;
		struct coll_id *new_coll_id = coll_id_new(&new_def);
		if (new_coll_id == NULL)
			return -1;
		struct coll_id *replaced_id;
		if (coll_id_cache_replace(new_coll_id, &replaced_id) != 0) {
			coll_id_delete(new_coll_id);
			return -1;
		}
		assert(replaced_id == NULL);
		on_rollback->data = new_coll_id;
		txn_stmt_on_rollback(stmt, on_rollback);
	} else {
		/* UPDATE */
		assert(new_tuple != NULL && old_tuple != NULL);
		diag_set(ClientError, ER_UNSUPPORTED, "collation", "alter");
		return -1;
	}
	return 0;
}

/**
 * Create a privilege definition from tuple.
 */
int
priv_def_create_from_tuple(struct priv_def *priv, struct tuple *tuple)
{
	if (tuple_field_u32(tuple, BOX_PRIV_FIELD_ID, &(priv->grantor_id)) != 0 ||
	    tuple_field_u32(tuple, BOX_PRIV_FIELD_UID, &(priv->grantee_id)) != 0)
		return -1;

	const char *object_type =
		tuple_field_cstr(tuple, BOX_PRIV_FIELD_OBJECT_TYPE);
	if (object_type == NULL)
		return -1;
	priv->object_type = schema_object_type(object_type);
	assert(priv->object_type < schema_object_type_MAX);

	const char *data = tuple_field(tuple, BOX_PRIV_FIELD_OBJECT_ID);
	if (data == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FIELD_NO,
			  BOX_PRIV_FIELD_OBJECT_ID + TUPLE_INDEX_BASE);
		return -1;
	}
	/*
	 * When granting or revoking privileges on a whole entity
	 * we pass empty string ('') to object_id to indicate
	 * grant on every object of that entity.
	 * So check for that first.
	 */
	switch (mp_typeof(*data)) {
	case MP_STR:
		priv->object_name = mp_decode_str(&data,
						  &priv->object_name_len);
		if (priv->object_name_len == 0) {
			/* Entity-wide privilege. */
			priv->is_entity_access = true;
			priv->object_id = 0;
			priv->object_name = NULL;
			break;
		} else if (priv->object_type == SC_LUA_CALL) {
			/*
			 * lua_call objects are global Lua functions.
			 * They aren't stored in the database hence
			 * don't have numeric ids. They are identified
			 * by string names.
			 */
			priv->is_entity_access = false;
			priv->object_id = 0;
			break;
		}
		FALLTHROUGH;
	default:
		priv->is_entity_access = false;
		if (tuple_field_u32(tuple,
		    BOX_PRIV_FIELD_OBJECT_ID, &(priv->object_id)) != 0)
			return -1;
		priv->object_name = NULL;
		priv->object_name_len = 0;
	}
	if (priv->object_type == SC_UNKNOWN) {
		diag_set(ClientError, ER_UNKNOWN_SCHEMA_OBJECT,
			  object_type);
		return -1;
	}
	uint32_t out;
	if (tuple_field_u32(tuple, BOX_PRIV_FIELD_ACCESS, &out) != 0)
		return -1;
	priv->access = out;
	return 0;
}

/*
 * This function checks that:
 * - a privilege is granted from an existing user to an existing
 *   user on an existing object
 * - the grantor has the right to grant (is the owner of the object)
 *
 * @XXX Potentially there is a race in case of rollback, since an
 * object can be changed during WAL write.
 * In the future we must protect grant/revoke with a logical lock.
 */
static int
priv_def_check(struct priv_def *priv, enum priv_type priv_type)
{
	struct user *grantor = user_find(priv->grantor_id);
	if (grantor == NULL)
		return -1;
	/* May be a role */
	struct user *grantee = user_by_id(priv->grantee_id);
	if (grantee == NULL) {
		diag_set(ClientError, ER_NO_SUCH_USER,
			  int2str(priv->grantee_id));
		return -1;
	}
	const char *name = "";
	struct access *object = NULL;
	switch (priv->object_type) {
	case SC_SPACE:
	{
		if (priv->is_entity_access)
			break;
		struct space *space = space_cache_find(priv->object_id);
		if (space == NULL)
			return -1;
		name = space_name(space);
		object = space->access;
		if (space->def->uid != grantor->def->uid &&
		    grantor->def->uid != ADMIN) {
			diag_set(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_SPACE), name,
				  grantor->def->name);
			return -1;
		}
		if (space_is_temporary(space)) {
			diag_set(ClientError, ER_UNSUPPORTED,
				 "temporary space", "privileges");
			return -1;
		}
		break;
	}
	case SC_FUNCTION:
	{
		if (priv->is_entity_access)
			break;
		struct func *func = func_by_id(priv->object_id);
		if (func == NULL) {
			diag_set(ClientError, ER_NO_SUCH_FUNCTION, int2str(priv->object_id));
			return -1;
		}
		name = func->def->name;
		object = func->access;
		if (func->def->uid != grantor->def->uid &&
		    grantor->def->uid != ADMIN) {
			diag_set(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_FUNCTION), name,
				  grantor->def->name);
			return -1;
		}
		break;
	}
	case SC_SEQUENCE:
	{
		if (priv->is_entity_access)
			break;
		struct sequence *seq = sequence_by_id(priv->object_id);
		if (seq == NULL) {
			diag_set(ClientError, ER_NO_SUCH_SEQUENCE, int2str(priv->object_id));
			return -1;
		}
		name = seq->def->name;
		object = seq->access;
		if (seq->def->uid != grantor->def->uid &&
		    grantor->def->uid != ADMIN) {
			diag_set(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_SEQUENCE), name,
				  grantor->def->name);
			return -1;
		}
		break;
	}
	case SC_ROLE:
	{
		if (priv->is_entity_access)
			break;
		struct user *role = user_by_id(priv->object_id);
		if (role == NULL || role->def->type != SC_ROLE) {
			diag_set(ClientError, ER_NO_SUCH_ROLE,
				  role ? role->def->name :
				  int2str(priv->object_id));
			return -1;
		}
		name = role->def->name;
		object = role->access;
		/*
		 * Only the creator of the role can grant or revoke it.
		 * Everyone can grant 'PUBLIC' role.
		 */
		if (role->def->owner != grantor->def->uid &&
		    grantor->def->uid != ADMIN &&
		    (role->def->uid != PUBLIC || priv->access != PRIV_X)) {
			diag_set(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_ROLE), name,
				  grantor->def->name);
			return -1;
		}
		/* Not necessary to do during revoke, but who cares. */
		if (role_check(grantee, role) != 0)
			return -1;
		break;
	}
	case SC_USER:
	{
		if (priv->is_entity_access)
			break;
		struct user *user = user_by_id(priv->object_id);
		if (user == NULL || user->def->type != SC_USER) {
			diag_set(ClientError, ER_NO_SUCH_USER,
				  user ? user->def->name :
				  int2str(priv->object_id));
			return -1;
		}
		name = user->def->name;
		object = user->access;
		if (user->def->owner != grantor->def->uid &&
		    grantor->def->uid != ADMIN) {
			diag_set(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_USER), name,
				  grantor->def->name);
			return -1;
		}
		break;
	}
	default:
		break;
	}
	/* Only admin may grant privileges on an entire entity. */
	if (object == NULL && grantor->def->uid != ADMIN) {
		diag_set(AccessDeniedError, priv_name(priv_type),
			 schema_object_name(priv->object_type), name,
			 grantor->def->name);
		return -1;
	}
	if (access_check_ddl(name, grantor->def->uid, object,
			     priv->object_type, priv_type) != 0)
		return -1;
	if (priv->access == 0) {
		diag_set(ClientError, ER_GRANT,
			  "the grant tuple has no privileges");
		return -1;
	}
	return 0;
}

/**
 * Update a metadata cache object with the new access
 * data.
 */
static int
grant_or_revoke(struct priv_def *priv)
{
	struct user *grantee = user_by_id(priv->grantee_id);
	if (grantee == NULL)
		return 0;
	/*
	 * Grant a role to a user only when privilege type is 'execute'
	 * and the role is specified.
	 */
	if (priv->object_type == SC_ROLE && !(priv->access & ~PRIV_X)) {
		struct user *role = user_by_id(priv->object_id);
		if (role == NULL || role->def->type != SC_ROLE)
			return 0;
		if (priv->access) {
			if (role_grant(grantee, role) != 0)
				return -1;
		} else {
			if (role_revoke(grantee, role) != 0)
				return -1;
		}
	} else {
		if (priv_grant(grantee, priv) != 0)
			return -1;
	}
	return 0;
}

/** A trigger called on rollback of grant. */
static int
revoke_priv(struct trigger *trigger, void *event)
{
	(void) event;
	struct tuple *tuple = (struct tuple *)trigger->data;
	struct priv_def priv;
	if (priv_def_create_from_tuple(&priv, tuple) != 0)
		return -1;
	priv.access = 0;
	if (grant_or_revoke(&priv) != 0)
		return -1;
	return 0;
}

/** A trigger called on rollback of revoke or modify. */
static int
modify_priv(struct trigger *trigger, void *event)
{
	(void) event;
	struct tuple *tuple = (struct tuple *)trigger->data;
	struct priv_def priv;
	if (priv_def_create_from_tuple(&priv, tuple) != 0 ||
	    grant_or_revoke(&priv) != 0)
		return -1;
	return 0;
}

/**
 * A trigger invoked on replace in the space containing
 * all granted privileges.
 */
static int
on_replace_dd_priv(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	struct priv_def priv;

	if (new_tuple != NULL && old_tuple == NULL) {	/* grant */
		if (priv_def_create_from_tuple(&priv, new_tuple) != 0 ||
		    priv_def_check(&priv, PRIV_GRANT) != 0 ||
		    grant_or_revoke(&priv) != 0)
			return -1;
		struct trigger *on_rollback =
			txn_alter_trigger_new(revoke_priv, new_tuple);
		if (on_rollback == NULL)
			return -1;
		txn_stmt_on_rollback(stmt, on_rollback);
	} else if (new_tuple == NULL) {                /* revoke */
		assert(old_tuple);
		if (priv_def_create_from_tuple(&priv, old_tuple) != 0 ||
		    priv_def_check(&priv, PRIV_REVOKE) != 0)
			return -1;
		priv.access = 0;
		if (grant_or_revoke(&priv) != 0)
			return -1;
		struct trigger *on_rollback =
			txn_alter_trigger_new(modify_priv, old_tuple);
		if (on_rollback == NULL)
			return -1;
		txn_stmt_on_rollback(stmt, on_rollback);
	} else {                                       /* modify */
		if (priv_def_create_from_tuple(&priv, new_tuple) != 0 ||
		    priv_def_check(&priv, PRIV_GRANT) != 0 ||
		    grant_or_revoke(&priv) != 0)
			return -1;
		struct trigger *on_rollback =
			txn_alter_trigger_new(modify_priv, old_tuple);
		if (on_rollback == NULL)
			return -1;
		txn_stmt_on_rollback(stmt, on_rollback);
	}
	return 0;
}

/* }}} access control */

/* {{{ cluster configuration */

/** Set replicaset UUID on _schema commit. */
static int
on_commit_replicaset_uuid(struct trigger *trigger, void * /* event */)
{
	const struct tt_uuid *uuid = (typeof(uuid))trigger->data;
	if (tt_uuid_is_equal(&REPLICASET_UUID, uuid))
		return 0;
	REPLICASET_UUID = *uuid;
	box_broadcast_id();
	say_info("replicaset uuid %s", tt_uuid_str(uuid));
	return 0;
}

/** Set replicaset name on _schema commit. */
static int
on_commit_replicaset_name(struct trigger *trigger, void * /* event */)
{
	const char *name = (typeof(name))trigger->data;
	if (strcmp(REPLICASET_NAME, name) == 0)
		return 0;
	strlcpy(REPLICASET_NAME, name, NODE_NAME_SIZE_MAX);
	box_broadcast_id();
	say_info("replicaset name: %s", node_name_str(name));
	return 0;
}

static int
start_synchro_filtering(va_list /* ap */)
{
	txn_limbo_filter_enable(&txn_limbo);
	return 0;
}

static int
stop_synchro_filtering(va_list /* ap */)
{
	txn_limbo_filter_disable(&txn_limbo);
	return 0;
}

/** Data passed to on_commit_dd_version trigger. */
struct on_commit_dd_version_data {
	/** A fiber to perform async work after commit. */
	struct fiber *fiber;
	/** New version. */
	uint32_t version_id;
};

/**
 * Update the cached schema version and enable version-dependent features, like
 * split-brain detection. Reenabling is done asynchronously by a separate fiber
 * prepared by on_replace trigger.
 */
static int
on_commit_dd_version(struct trigger *trigger, void * /* event */)
{
	struct on_commit_dd_version_data *data =
		(struct on_commit_dd_version_data *)trigger->data;
	dd_version_id = data->version_id;
	struct fiber *fiber = data->fiber;
	if (fiber != NULL)
		fiber_wakeup(fiber);
	return 0;
}

/** Set cluster name on _schema commit. */
static int
on_commit_cluster_name(struct trigger *trigger, void * /* event */)
{
	const char *name = (typeof(name))trigger->data;
	if (strcmp(CLUSTER_NAME, name) == 0)
		return 0;
	strlcpy(CLUSTER_NAME, name, NODE_NAME_SIZE_MAX);
	box_broadcast_id();
	say_info("cluster name: %s", node_name_str(name));
	return 0;
}

/**
 * This trigger implements the "last write wins" strategy for
 * "bootstrap_leader_uuid" tuple of space _schema. Comparison is performed by
 * a timestamp and replica_id of the node which authored the change.
 */
static int
before_replace_dd_schema(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *)event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	const char *key = tuple_field_cstr(new_tuple != NULL ?
					   new_tuple : old_tuple,
					   BOX_SCHEMA_FIELD_KEY);
	if (key == NULL)
		return -1;
	if (strcmp(key, "bootstrap_leader_uuid") == 0) {
		uint64_t old_ts = 0;
		uint32_t old_id = 0;
		uint64_t new_ts = UINT64_MAX;
		uint32_t new_id = UINT32_MAX;
		int ts_field = BOX_SCHEMA_FIELD_VALUE + 1;
		int id_field = BOX_SCHEMA_FIELD_VALUE + 2;
		/*
		 * Assume anything can be stored in old_tuple, so do not require
		 * it to have a timestamp or replica_id. In contrary to that,
		 * always require new tuple to have a valid timestamp and
		 * replica_id.
		 */
		if (old_tuple != NULL) {
			const char *field = tuple_field(old_tuple, ts_field);
			if (field != NULL && mp_typeof(*field) == MP_UINT)
				old_ts = mp_decode_uint(&field);
			field = tuple_field(old_tuple, id_field);
			if (field != NULL && mp_typeof(*field) == MP_UINT)
				old_id = mp_decode_uint(&field);
		}
		if (new_tuple != NULL &&
		    (tuple_field_u64(new_tuple, ts_field, &new_ts) != 0 ||
		     tuple_field_u32(new_tuple, id_field, &new_id) != 0)) {
			return -1;
		}
		if (new_ts < old_ts || (new_ts == old_ts && new_id < old_id)) {
			say_info("Ignore the replace of tuple %s with %s in "
				 "space _schema: the former has a newer "
				 "timestamp", tuple_str(old_tuple),
				 tuple_str(new_tuple));
			goto return_old;
		}
	}
	return 0;
return_old:
	if (new_tuple != NULL)
		tuple_unref(new_tuple);
	if (old_tuple != NULL)
		tuple_ref(old_tuple);
	stmt->new_tuple = old_tuple;
	return 0;
}

/** An on_commit trigger to update bootstrap leader uuid. */
static int
on_commit_schema_set_bootstrap_leader_uuid(struct trigger *trigger, void *event)
{
	(void)event;
	struct tt_uuid *uuid = (struct tt_uuid *)trigger->data;
	bootstrap_leader_uuid = *uuid;
	box_broadcast_ballot();
	return 0;
}

/**
 * This trigger is invoked only upon initial recovery, when
 * reading contents of the system spaces from the snapshot.
 *
 * Before a cluster is assigned a cluster id it's read only.
 * Since during recovery state of the WAL doesn't
 * concern us, we can safely change the cluster id in before-replace
 * event, not in after-replace event.
 */
static int
on_replace_dd_schema(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	const char *key = tuple_field_cstr(new_tuple ? new_tuple : old_tuple,
					   BOX_SCHEMA_FIELD_KEY);
	if (key == NULL)
		return -1;
	if (strcmp(key, "cluster") == 0 ||
	    strcmp(key, "replicaset_uuid") == 0) {
		if (new_tuple == NULL) {
			/*
			 * At least one of the keys has to stay. Or the
			 * replicaset UUID would be lost after restart.
			 */
			const char *other_key = strcmp(key, "cluster") == 0 ?
				"replicaset_uuid" : "cluster";
			char mpkey[64];
			char *mpkey_end = mp_encode_array(mpkey, 1);
			mpkey_end = mp_encode_str0(mpkey_end, other_key);
			struct tuple *other = NULL;
			if (box_index_get(BOX_SCHEMA_ID, 0, mpkey, mpkey_end,
					  &other) != 0)
				return -1;
			if (other == NULL) {
				diag_set(ClientError, ER_REPLICASET_UUID_IS_RO);
				return -1;
			}
			/*
			 * Deletion of the old key is allowed for upgrade.
			 * Deletion of the new one is needed for downgrade.
			 * Can't ban either.
			 */
			return 0;
		}
		tt_uuid uu;
		if (tuple_field_uuid(new_tuple, BOX_SCHEMA_FIELD_VALUE, &uu) != 0)
			return -1;
		if (!tt_uuid_is_nil(&REPLICASET_UUID) &&
		    !tt_uuid_is_equal(&REPLICASET_UUID, &uu)) {
			diag_set(ClientError, ER_REPLICASET_UUID_IS_RO);
			return -1;
		}
		struct tt_uuid *uuid_copy = xregion_alloc_object(
			&txn->region, typeof(*uuid_copy));
		*uuid_copy = uu;
		struct trigger *on_commit = txn_alter_trigger_new(
			on_commit_replicaset_uuid, (void *)uuid_copy);
		if (on_commit == NULL)
			return -1;
		txn_stmt_on_commit(stmt, on_commit);
	} else if (strcmp(key, "version") == 0) {
		uint32_t version = 0;
		if (new_tuple != NULL) {
			uint32_t major, minor, patch;
			if (tuple_field_u32(new_tuple, 1, &major) != 0 ||
			    tuple_field_u32(new_tuple, 2, &minor) != 0) {
				diag_set(ClientError, ER_WRONG_DD_VERSION);
				return -1;
			}
			/* Version can be major.minor with no patch. */
			if (tuple_field_u32(new_tuple, 3, &patch) != 0)
				patch = 0;
			version = version_id(major, minor, patch);
		} else {
			assert(old_tuple != NULL);
			/*
			 * _schema:delete({'version'}) for
			 * example, for box.internal.bootstrap().
			 */
			version = tarantool_version_id();
		}
		struct on_commit_dd_version_data *data = xregion_alloc_object(
			&txn->region, typeof(*data));
		data->version_id = version;
		data->fiber = NULL;
		struct trigger *on_commit = txn_alter_trigger_new(
			on_commit_dd_version, data);
		if (on_commit == NULL)
			return -1;
		txn_stmt_on_commit(stmt, on_commit);
		if (recovery_state != FINISHED_RECOVERY) {
			return 0;
		}
		/*
		 * Set data->fiber after on_commit is created, because we can't
		 * remove a not-yet-run fiber in case of on_commit creation
		 * failure.
		 */
		struct fiber *fiber = NULL;
		if (version > version_id(2, 10, 1) &&
		    recovery_state == FINISHED_RECOVERY) {
			fiber = fiber_new_system("synchro_filter_enabler",
						 start_synchro_filtering);
			if (fiber == NULL)
				return -1;
		} else if (version <= version_id(2, 10, 1) &&
			   recovery_state == FINISHED_RECOVERY) {
			fiber = fiber_new_system("synchro_filter_disabler",
						 stop_synchro_filtering);
			if (fiber == NULL)
				return -1;
		}
		data->fiber = fiber;
	} else if (strcmp(key, "bootstrap_leader_uuid") == 0) {
		struct tt_uuid *uuid = xregion_alloc_object(&txn->region,
							    typeof(*uuid));
		if (!new_tuple) {
			*uuid = uuid_nil;
		} else if (tuple_field_uuid(new_tuple, BOX_SCHEMA_FIELD_VALUE,
					    uuid) != 0) {
			return -1;
		}
		struct trigger *on_commit = txn_alter_trigger_new(
			on_commit_schema_set_bootstrap_leader_uuid, uuid);
		if (on_commit == NULL)
			return -1;
		txn_on_commit(txn, on_commit);
	} else if (strcmp(key, "cluster_name") == 0) {
		char name[NODE_NAME_SIZE_MAX];
		const char *field_name = "_schema['cluster_name'].value";
		if (tuple_field_node_name(name, new_tuple,
					  BOX_SCHEMA_FIELD_VALUE,
					  field_name) != 0)
			return -1;
		if (box_is_configured() && *CLUSTER_NAME != 0 &&
		    strcmp(name, CLUSTER_NAME) != 0) {
			if (!box_is_force_recovery) {
				diag_set(ClientError, ER_UNSUPPORTED,
					 "Tarantool", "cluster name change "
					 "(without 'force_recovery')");
				return -1;
			}
			say_info("cluster rename allowed by 'force_recovery'");
		}
		size_t size = strlen(name) + 1;
		char *name_copy = (char *)xregion_alloc(&txn->region, size);
		memcpy(name_copy, name, size);
		struct trigger *on_commit = txn_alter_trigger_new(
			on_commit_cluster_name, name_copy);
		if (on_commit == NULL)
			return -1;
		txn_stmt_on_commit(stmt, on_commit);
	} else if (strcmp(key, "replicaset_name") == 0) {
		char name[NODE_NAME_SIZE_MAX];
		const char *field_name = "_schema['replicaset_name'].value";
		if (tuple_field_node_name(name, new_tuple,
					  BOX_SCHEMA_FIELD_VALUE,
					  field_name) != 0)
			return -1;
		if (box_is_configured() && *REPLICASET_NAME != 0 &&
		    strcmp(name, REPLICASET_NAME) != 0) {
			if (!box_is_force_recovery) {
				diag_set(ClientError, ER_UNSUPPORTED,
					 "Tarantool", "replicaset name change "
					 "(without 'force_recovery')");
				return -1;
			}
			say_info("replicaset name mismatch, "
				 "ignore due to 'force_recovery'");
		}
		size_t size = strlen(name) + 1;
		char *name_copy = (char *)xregion_alloc(&txn->region, size);
		memcpy(name_copy, name, size);
		struct trigger *on_commit = txn_alter_trigger_new(
			on_commit_replicaset_name, name_copy);
		if (on_commit == NULL)
			return -1;
		txn_stmt_on_commit(stmt, on_commit);
	}
	return 0;
}

/** Unregister the replica affected by the change. */
static int
on_replace_cluster_clear_id(struct trigger *trigger, void * /* event */)
{
	replica_clear_id((struct replica *)trigger->data);
	return 0;
}

/** Replica definition. */
struct replica_def {
	/** Instance ID. */
	uint32_t id;
	/** Instance UUID. */
	struct tt_uuid uuid;
	/** Instance name. */
	char name[NODE_NAME_SIZE_MAX];
};

/** Build replica definition from a _cluster's tuple. */
static struct replica_def *
replica_def_new_from_tuple(struct tuple *tuple, struct region *region)
{
	struct replica_def *def = xregion_alloc_object(region, typeof(*def));
	memset(def, 0, sizeof(*def));
	if (tuple_field_u32(tuple, BOX_CLUSTER_FIELD_ID, &def->id) != 0)
		return NULL;
	if (replica_check_id(def->id) != 0)
		return NULL;
	if (tuple_field_uuid(tuple, BOX_CLUSTER_FIELD_UUID, &def->uuid) != 0)
		return NULL;
	if (tt_uuid_is_nil(&def->uuid)) {
		diag_set(ClientError, ER_INVALID_UUID, tt_uuid_str(&def->uuid));
		return NULL;
	}
	if (tuple_field_node_name(def->name, tuple, BOX_CLUSTER_FIELD_NAME,
				  "_cluster.name") != 0)
		return NULL;
	return def;
}

/** Add an instance on commit/rollback. */
static int
on_replace_cluster_add_replica(struct trigger *trigger, void * /* event */)
{
	const struct replica_def *def = (typeof(def))trigger->data;
	struct replica *r = replicaset_add(def->id, &def->uuid);
	replica_set_name(r, def->name);
	return 0;
}

/** Set instance name on commit/rollback. */
static int
on_replace_cluster_set_name(struct trigger *trigger, void * /* event */)
{
	const struct replica_def *def = (typeof(def))trigger->data;
	struct replica *replica = replica_by_id(def->id);
	if (replica == NULL)
		panic("Couldn't find a replica in _cluster in txn trigger");
	const struct replica *other = replica_by_name(def->name);
	if (replica == other)
		return 0;
	/*
	 * The old name shouldn't be possible to be occupied. It would mean some
	 * other _cluster txn took it and then yielded, allowing this trigger to
	 * run. But _cluster txns are rolled back on yield. So this other txn
	 * would be rolled back before this trigger. Hence this situation is not
	 * reachable.
	 */
	if (other != NULL)
		panic("Duplicate replica name managed to slip through txns");
	replica_set_name(replica, def->name);
	return 0;
}

/** Set instance name on _cluster update. */
static int
on_replace_dd_cluster_set_name(struct replica *replica,
			       const char *new_name)
{
	struct txn_stmt *stmt = txn_current_stmt(in_txn());
	assert(replica->id != REPLICA_ID_NIL);
	if (strcmp(replica->name, new_name) == 0)
		return 0;
	if (tt_uuid_is_equal(&replica->uuid, &INSTANCE_UUID) &&
	    box_is_configured() && *INSTANCE_NAME != 0 &&
	    strcmp(new_name, INSTANCE_NAME) != 0) {
		if (!box_is_force_recovery) {
			diag_set(ClientError, ER_UNSUPPORTED, "Tarantool",
				 "replica rename or name drop");
			return -1;
		}
		say_info("replica rename allowed due to 'force_recovery'");
	}
	const struct replica *other = replica_by_name(new_name);
	if (other != NULL) {
		diag_set(ClientError, ER_INSTANCE_NAME_DUPLICATE,
			 node_name_str(new_name), tt_uuid_str(&other->uuid));
		return -1;
	}
	struct replica_def *def = xregion_alloc_object(
		&in_txn()->region, typeof(*def));
	memset(def, 0, sizeof(*def));
	def->id = replica->id;
	strlcpy(def->name, replica->name, NODE_NAME_SIZE_MAX);
	struct trigger *on_rollback = txn_alter_trigger_new(
		on_replace_cluster_set_name, (void *)def);
	if (on_rollback == NULL)
		return -1;
	txn_stmt_on_rollback(stmt, on_rollback);
	/*
	 * Set the new name now so as newer transactions couldn't take it too
	 * while this one is going to WAL.
	 */
	replica_set_name(replica, new_name);
	return 0;
}

/** Set instance UUID on _cluster update. */
static int
on_replace_dd_cluster_set_uuid(struct replica *replica,
			       const struct replica_def *new_def)
{
	struct replica *old_replica = replica;
	if (replica_has_connections(old_replica)) {
		diag_set(ClientError, ER_UNSUPPORTED, "Replica",
			 "UUID update when the old replica is still here");
		return -1;
	}
	if (*old_replica->name == 0) {
		diag_set(ClientError, ER_UNSUPPORTED, "Replica without a name",
			 "UUID update");
		return -1;
	}
	struct replica *new_replica = replica_by_uuid(&new_def->uuid);
	if (new_replica != NULL && new_replica->id != REPLICA_ID_NIL) {
		diag_set(ClientError, ER_UNSUPPORTED, "Replica",
			 "UUID update when the new UUID is already registered");
		return -1;
	}
	if (strcmp(new_def->name, replica->name) != 0) {
		diag_set(ClientError, ER_UNSUPPORTED, "Replica",
			 "UUID and name update together");
		return -1;
	}
	struct trigger *on_rollback_drop_new = txn_alter_trigger_new(
		on_replace_cluster_clear_id, NULL);
	struct trigger *on_rollback_add_old = txn_alter_trigger_new(
		on_replace_cluster_add_replica, NULL);
	if (on_rollback_drop_new == NULL || on_rollback_add_old == NULL)
		return -1;
	struct replica_def *old_def = xregion_alloc_object(
		&in_txn()->region, typeof(*old_def));
	memset(old_def, 0, sizeof(*old_def));
	old_def->id = old_replica->id;
	strlcpy(old_def->name, old_replica->name, NODE_NAME_SIZE_MAX);
	old_def->uuid = old_replica->uuid;

	replica_clear_id(old_replica);
	if (replica_by_uuid(&old_def->uuid) != NULL)
		panic("Replica with old UUID wasn't deleted");
	if (new_replica == NULL)
		new_replica = replicaset_add(new_def->id, &new_def->uuid);
	else
		replica_set_id(new_replica, new_def->id);
	replica_set_name(new_replica, old_def->name);
	on_rollback_drop_new->data = new_replica;
	on_rollback_add_old->data = old_def;
	struct txn_stmt *stmt = txn_current_stmt(in_txn());
	txn_stmt_on_rollback(stmt, on_rollback_drop_new);
	txn_stmt_on_rollback(stmt, on_rollback_add_old);
	return 0;
}

/** _cluster update - both old and new tuples are present. */
static int
on_replace_dd_cluster_update(const struct replica_def *old_def,
			     const struct replica_def *new_def)
{
	assert(new_def->id == old_def->id);
	struct replica *replica = replica_by_id(new_def->id);
	if (replica == NULL)
		panic("Found a _cluster tuple not having a replica");
	if (!tt_uuid_is_equal(&new_def->uuid, &old_def->uuid)) {
		if (tt_uuid_is_equal(&old_def->uuid, &INSTANCE_UUID)) {
			diag_set(ClientError, ER_UNSUPPORTED, "Replica",
				 "own UUID update in _cluster");
			return -1;
		}
		if (on_replace_dd_cluster_set_uuid(replica, new_def) != 0)
			return -1;
		/* The replica was re-created. */
		replica = replica_by_id(new_def->id);
	}
	return on_replace_dd_cluster_set_name(replica, new_def->name);
}

/** _cluster insert - only a new tuple is present. */
static int
on_replace_dd_cluster_insert(const struct replica_def *new_def)
{
	struct txn_stmt *stmt = txn_current_stmt(in_txn());
	struct replica *replica = replica_by_id(new_def->id);
	/*
	 * With read-views enabled there might be already a replica whose
	 * registration is in progress in another transaction. With the same
	 * replica ID.
	 */
	if (replica != NULL) {
		const char *msg = tt_sprintf(
			"more than 1 replica with the same ID %u: "
			"new uuid - %s, old uuid - %s", new_def->id,
			tt_uuid_str(&new_def->uuid),
			tt_uuid_str(&replica->uuid));
		diag_set(ClientError, ER_UNSUPPORTED, "Tarantool", msg);
		return -1;
	}
	replica = replica_by_name(new_def->name);
	if (replica != NULL) {
		diag_set(ClientError, ER_INSTANCE_NAME_DUPLICATE,
			 node_name_str(new_def->name),
			 tt_uuid_str(&replica->uuid));
		return -1;
	}
	struct trigger *on_rollback = txn_alter_trigger_new(
		on_replace_cluster_clear_id, NULL);
	if (on_rollback == NULL)
		return -1;
	/*
	 * Register the replica before commit so as to occupy the replica ID
	 * now. While WAL write is in progress, new replicas might come, they
	 * should see the ID is already in use.
	 */
	replica = replica_by_uuid(&new_def->uuid);
	if (replica != NULL)
		replica_set_id(replica, new_def->id);
	else
		replica = replicaset_add(new_def->id, &new_def->uuid);
	on_rollback->data = replica;
	txn_stmt_on_rollback(stmt, on_rollback);
	return on_replace_dd_cluster_set_name(replica, new_def->name);
}

/** _cluster delete - only the old tuple is present. */
static int
on_replace_dd_cluster_delete(const struct replica_def *old_def)
{
	/*
	 * It's okay to delete the instance id while it is joining to a cluster
	 * as long as the id is set by the time bootstrap is complete. Deletion
	 * can be coming from the master when the latter tried to purge dead
	 * replicas from _cluster.
	 */
	if (!replicaset.is_joining && old_def->id == instance_id) {
		diag_set(ClientError, ER_LOCAL_INSTANCE_ID_IS_READ_ONLY,
			 (unsigned)old_def->id);
		return -1;
	}
	struct txn_stmt *stmt = txn_current_stmt(in_txn());
	struct replica *replica = replica_by_id(old_def->id);
	if (replica == NULL) {
		/*
		 * Impossible, but it is important not to leave undefined
		 * behaviour if there is a bug. Too sensitive subsystem is
		 * affected.
		 */
		panic("Tried to unregister a replica not stored in "
		      "replica_by_id map, id is %u, uuid is %s",
		      old_def->id, tt_uuid_str(&old_def->uuid));
	}
	if (!tt_uuid_is_equal(&replica->uuid, &old_def->uuid)) {
		panic("Tried to unregister a replica with id %u, but its uuid "
		      "is different from stored internally: in space - %s, "
		      "internally - %s", old_def->id,
		      tt_uuid_str(&old_def->uuid), tt_uuid_str(&replica->uuid));
	}
	/*
	 * Unregister only after commit. Otherwise if the transaction would be
	 * rolled back, there might be already another replica taken the freed
	 * ID.
	 */
	struct trigger *on_commit = txn_alter_trigger_new(
		on_replace_cluster_clear_id, replica);
	if (on_commit == NULL)
		return -1;
	txn_stmt_on_commit(stmt, on_commit);
	return 0;
}

/** Space _cluster on-replace trigger. */
static int
on_replace_dd_cluster(struct trigger *trigger, void *event)
{
	(void) trigger;
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct replica_def *new_def = NULL;
	struct replica_def *old_def = NULL;
	if (stmt->new_tuple != NULL) {
		new_def = replica_def_new_from_tuple(stmt->new_tuple,
						     &txn->region);
		if (new_def == NULL)
			return -1;
	}
	if (stmt->old_tuple != NULL) {
		old_def = replica_def_new_from_tuple(stmt->old_tuple,
						     &txn->region);
		if (old_def == NULL)
			return -1;
	}
	if (new_def != NULL) {
		if (old_def != NULL)
			return on_replace_dd_cluster_update(old_def, new_def);
		return on_replace_dd_cluster_insert(new_def);
	}
	assert(old_def != NULL);
	return on_replace_dd_cluster_delete(old_def);
}

/* }}} cluster configuration */

/* {{{ sequence */

/** Create a sequence definition from a tuple. */
static struct sequence_def *
sequence_def_new_from_tuple(struct tuple *tuple, uint32_t errcode)
{
	uint32_t name_len;
	const char *name = tuple_field_str(tuple, BOX_USER_FIELD_NAME,
					   &name_len);
	if (name == NULL)
		return NULL;
	if (name_len > BOX_NAME_MAX) {
		diag_set(ClientError, errcode,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  "sequence name is too long");
		return NULL;
	}
	if (identifier_check(name, name_len) != 0)
		return NULL;
	size_t sz = sequence_def_sizeof(name_len);
	struct sequence_def *def = (struct sequence_def *) malloc(sz);
	if (def == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "sequence");
		return NULL;
	}
	auto def_guard = make_scoped_guard([=] { free(def); });
	memcpy(def->name, name, name_len);
	def->name[name_len] = '\0';
	if (tuple_field_u32(tuple, BOX_SEQUENCE_FIELD_ID, &(def->id)) != 0)
		return NULL;
	if (tuple_field_u32(tuple, BOX_SEQUENCE_FIELD_UID, &(def->uid)) != 0)
		return NULL;
	if (tuple_field_i64(tuple, BOX_SEQUENCE_FIELD_STEP, &(def->step)) != 0)
		return NULL;
	if (tuple_field_i64(tuple, BOX_SEQUENCE_FIELD_MIN, &(def->min)) != 0)
		return NULL;
	if (tuple_field_i64(tuple, BOX_SEQUENCE_FIELD_MAX, &(def->max)) != 0)
		return NULL;
	if (tuple_field_i64(tuple, BOX_SEQUENCE_FIELD_START, &(def->start)) != 0)
		return NULL;
	if (tuple_field_i64(tuple, BOX_SEQUENCE_FIELD_CACHE, &(def->cache)) != 0)
		return NULL;
	if (tuple_field_bool(tuple, BOX_SEQUENCE_FIELD_CYCLE, &(def->cycle)) != 0)
		return NULL;
	if (def->step == 0) {
		diag_set(ClientError, errcode, def->name,
			 "step option must be non-zero");
		return NULL;
	}
	if (def->min > def->max) {
		diag_set(ClientError, errcode, def->name,
			 "max must be greater than or equal to min");
		return NULL;
	}
	if (def->start < def->min || def->start > def->max) {
		diag_set(ClientError, errcode, def->name,
			 "start must be between min and max");
		return NULL;
	}
	def_guard.is_active = false;
	return def;
}

static int
on_create_sequence_rollback(struct trigger *trigger, void * /* event */)
{
	/* Remove the new sequence from the cache and delete it. */
	struct sequence *seq = (struct sequence *)trigger->data;
	sequence_cache_delete(seq->def->id);
	if (trigger_run(&on_alter_sequence, seq) != 0)
		return -1;
	sequence_delete(seq);
	return 0;
}

static int
on_drop_sequence_commit(struct trigger *trigger, void * /* event */)
{
	/* Delete the old sequence. */
	struct sequence *seq = (struct sequence *)trigger->data;
	sequence_delete(seq);
	return 0;
}

static int
on_drop_sequence_rollback(struct trigger *trigger, void * /* event */)
{
	/* Insert the old sequence back into the cache. */
	struct sequence *seq = (struct sequence *)trigger->data;
	sequence_cache_insert(seq);
	if (trigger_run(&on_alter_sequence, seq) != 0)
		return -1;
	return 0;
}


static int
on_alter_sequence_commit(struct trigger *trigger, void * /* event */)
{
	/* Delete the old old sequence definition. */
	struct sequence_def *def = (struct sequence_def *)trigger->data;
	free(def);
	return 0;
}

static int
on_alter_sequence_rollback(struct trigger *trigger, void * /* event */)
{
	/* Restore the old sequence definition. */
	struct sequence_def *def = (struct sequence_def *)trigger->data;
	struct sequence *seq = sequence_by_id(def->id);
	assert(seq != NULL);
	free(seq->def);
	seq->def = def;
	if (trigger_run(&on_alter_sequence, seq) != 0)
		return -1;
	return 0;
}

/**
 * A trigger invoked on replace in space _sequence.
 * Used to alter a sequence definition.
 */
static int
on_replace_dd_sequence(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	struct sequence_def *new_def = NULL;
	auto def_guard = make_scoped_guard([&new_def] { free(new_def); });

	struct sequence *seq;
	if (old_tuple == NULL && new_tuple != NULL) {		/* INSERT */
		new_def = sequence_def_new_from_tuple(new_tuple,
						      ER_CREATE_SEQUENCE);
		if (new_def == NULL)
			return -1;
		if (access_check_ddl(new_def->name, new_def->uid, NULL,
				     SC_SEQUENCE, PRIV_C) != 0)
			return -1;
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_create_sequence_rollback, NULL);
		if (on_rollback == NULL)
			return -1;
		seq = sequence_new(new_def);
		sequence_cache_insert(seq);
		on_rollback->data = seq;
		txn_stmt_on_rollback(stmt, on_rollback);
	} else if (old_tuple != NULL && new_tuple == NULL) {	/* DELETE */
		uint32_t id;
		if (tuple_field_u32(old_tuple, BOX_SEQUENCE_DATA_FIELD_ID, &id) != 0)
			return -1;
		seq = sequence_by_id(id);
		assert(seq != NULL);
		if (access_check_ddl(seq->def->name, seq->def->uid, seq->access,
				     SC_SEQUENCE, PRIV_D) != 0)
			return -1;
		bool out;
		if (space_has_data(BOX_SEQUENCE_DATA_ID, 0, id, &out) != 0)
			return -1;
		if (out) {
			diag_set(ClientError, ER_DROP_SEQUENCE,
				  seq->def->name, "the sequence has data");
			return -1;
		}
		if (space_has_data(BOX_SPACE_SEQUENCE_ID, 1, id, &out) != 0)
			return -1;
		if (out) {
			diag_set(ClientError, ER_DROP_SEQUENCE,
				  seq->def->name, "the sequence is in use");
			return -1;
		}
		if (schema_find_grants("sequence", seq->def->id, &out) != 0) {
			return -1;
		}
		if (out) {
			diag_set(ClientError, ER_DROP_SEQUENCE,
				  seq->def->name, "the sequence has grants");
			return -1;
		}
		struct trigger *on_commit =
			txn_alter_trigger_new(on_drop_sequence_commit, seq);
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_drop_sequence_rollback, seq);
		if (on_commit == NULL || on_rollback == NULL)
			return -1;
		sequence_cache_delete(seq->def->id);
		txn_stmt_on_commit(stmt, on_commit);
		txn_stmt_on_rollback(stmt, on_rollback);
	} else {						/* UPDATE */
		new_def = sequence_def_new_from_tuple(new_tuple,
						      ER_ALTER_SEQUENCE);
		if (new_def == NULL)
			return -1;
		seq = sequence_by_id(new_def->id);
		assert(seq != NULL);
		if (access_check_ddl(seq->def->name, seq->def->uid, seq->access,
				     SC_SEQUENCE, PRIV_A) != 0)
			return -1;
		struct trigger *on_commit =
			txn_alter_trigger_new(on_alter_sequence_commit, seq->def);
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_alter_sequence_rollback, seq->def);
		if (on_commit == NULL || on_rollback == NULL)
			return -1;
		seq->def = new_def;
		txn_stmt_on_commit(stmt, on_commit);
		txn_stmt_on_rollback(stmt, on_rollback);
	}

	def_guard.is_active = false;
	if (trigger_run(&on_alter_sequence, seq) != 0)
		return -1;
	return 0;
}

/** Restore the old sequence value on rollback. */
static int
on_drop_sequence_data_rollback(struct trigger *trigger, void * /* event */)
{
	struct tuple *tuple = (struct tuple *)trigger->data;
	uint32_t id;
	if (tuple_field_u32(tuple, BOX_SEQUENCE_DATA_FIELD_ID, &id) != 0)
		return -1;
	int64_t val;
	if (tuple_field_i64(tuple, BOX_SEQUENCE_DATA_FIELD_VALUE, &val) != 0)
		return -1;
	struct sequence *seq = sequence_by_id(id);
	assert(seq != NULL);
	if (sequence_set(seq, val) != 0)
		panic("Can't restore sequence value");
	return 0;
}

/**
 * A trigger invoked on replace in space _sequence_data.
 * Used to update a sequence value.
 */
static int
on_replace_dd_sequence_data(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	uint32_t id;
	if (tuple_field_u32(old_tuple ?: new_tuple, BOX_SEQUENCE_DATA_FIELD_ID,
			    &id) != 0)
		return -1;
	struct sequence *seq = sequence_by_id(id);
	if (seq == NULL) {
		diag_set(ClientError, ER_NO_SUCH_SEQUENCE, int2str(id));
		return -1;
	}
	if (new_tuple != NULL) {			/* INSERT, UPDATE */
		int64_t value;
		if (tuple_field_i64(new_tuple, BOX_SEQUENCE_DATA_FIELD_VALUE,
				    &value) != 0)
			return -1;
		if (sequence_set(seq, value) != 0)
			return -1;
	} else {					/* DELETE */
		/*
		 * A sequence isn't supposed to roll back to the old
		 * value if the transaction it was used in is aborted
		 * for some reason. However, if a sequence is dropped,
		 * we do want to restore the original sequence value
		 * on rollback.
		 */
		struct trigger *on_rollback = txn_alter_trigger_new(
				on_drop_sequence_data_rollback, old_tuple);
		if (on_rollback == NULL)
			return -1;
		txn_stmt_on_rollback(stmt, on_rollback);
		sequence_reset(seq);
	}
	return 0;
}

/**
 * Extract field number and path from _space_sequence tuple.
 * The path is allocated using malloc().
 */
static int
sequence_field_from_tuple(struct space *space, struct tuple *tuple,
			  char **path_ptr, uint32_t *out)
{
	struct index *pk = index_find(space, 0);
	if (pk == NULL) {
		return -1;
	}
	struct key_part *part = &pk->def->key_def->parts[0];
	uint32_t fieldno = part->fieldno;
	const char *path_raw = part->path;
	uint32_t path_len = part->path_len;

	/* Sequence field was added in 2.2.1. */
	if (tuple_field_count(tuple) > BOX_SPACE_SEQUENCE_FIELD_FIELDNO) {
		if (tuple_field_u32(tuple, BOX_SPACE_SEQUENCE_FIELD_FIELDNO,
				    &fieldno) != 0)
			return -1;
		path_raw = tuple_field_str(tuple, BOX_SPACE_SEQUENCE_FIELD_PATH,
					   &path_len);
		if (path_raw == NULL)
			return -1;
		if (path_len == 0)
			path_raw = NULL;
	}
	if (index_def_check_sequence(pk->def, fieldno, path_raw, path_len,
				     space_name(space)) != 0)
		return -1;
	char *path = NULL;
	if (path_raw != NULL) {
		path = (char *)malloc(path_len + 1);
		if (path == NULL) {
			diag_set(OutOfMemory, path_len + 1,
				  "malloc", "sequence path");
			return -1;
		}
		memcpy(path, path_raw, path_len);
		path[path_len] = 0;
	}
	*path_ptr = path;
	*out = fieldno;
	return 0;
}

/** Attach a sequence to a space on rollback in _space_sequence. */
static int
set_space_sequence(struct trigger *trigger, void * /* event */)
{
	struct tuple *tuple = (struct tuple *)trigger->data;
	uint32_t space_id;
	if (tuple_field_u32(tuple, BOX_SPACE_SEQUENCE_FIELD_ID, &space_id) != 0)
		return -1;
	uint32_t sequence_id;
	if (tuple_field_u32(tuple, BOX_SPACE_SEQUENCE_FIELD_SEQUENCE_ID,
			    &sequence_id) != 0)
		return -1;
	bool is_generated;
	if (tuple_field_bool(tuple, BOX_SPACE_SEQUENCE_FIELD_IS_GENERATED,
		&is_generated) != 0)
		return -1;
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	struct sequence *seq = sequence_by_id(sequence_id);
	assert(seq != NULL);
	char *path;
	uint32_t fieldno;
	if (sequence_field_from_tuple(space, tuple, &path, &fieldno) != 0)
		return -1;
	seq->is_generated = is_generated;
	space->sequence = seq;
	space->sequence_fieldno = fieldno;
	free(space->sequence_path);
	space->sequence_path = path;
	if (trigger_run(&on_alter_space, space) != 0)
		return -1;
	return 0;
}

/** Detach a sequence from a space on rollback in _space_sequence. */
static int
clear_space_sequence(struct trigger *trigger, void * /* event */)
{
	struct tuple *tuple = (struct tuple *)trigger->data;
	uint32_t space_id;
	if (tuple_field_u32(tuple, BOX_SPACE_SEQUENCE_FIELD_ID, &space_id) != 0)
		return -1;
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	assert(space->sequence != NULL);
	space->sequence->is_generated = false;
	space->sequence = NULL;
	space->sequence_fieldno = 0;
	free(space->sequence_path);
	space->sequence_path = NULL;
	if (trigger_run(&on_alter_space, space) != 0)
		return -1;
	return 0;
}

/**
 * A trigger invoked on replace in space _space_sequence.
 * Used to update space <-> sequence mapping.
 */
static int
on_replace_dd_space_sequence(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *tuple = stmt->new_tuple ? stmt->new_tuple : stmt->old_tuple;
	uint32_t space_id;
	if (tuple_field_u32(tuple, BOX_SPACE_SEQUENCE_FIELD_ID, &space_id) != 0)
		return -1;
	uint32_t sequence_id;
	if (tuple_field_u32(tuple, BOX_SPACE_SEQUENCE_FIELD_SEQUENCE_ID,
			    &sequence_id) != 0)
		return -1;
	bool is_generated;
	if (tuple_field_bool(tuple, BOX_SPACE_SEQUENCE_FIELD_IS_GENERATED,
			     &is_generated) != 0)
		return -1;
	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return -1;
	if (space_is_temporary(space)) {
		diag_set(ClientError, ER_SQL_EXECUTE,
			 "sequences are not supported for temporary spaces");
		return -1;
	}
	struct sequence *seq = sequence_by_id(sequence_id);
	if (seq == NULL) {
		diag_set(ClientError, ER_NO_SUCH_SEQUENCE, int2str(sequence_id));
		return -1;
	}
	if (stmt->new_tuple != NULL && stmt->old_tuple != NULL) {
		/*
		 * Makes no sense to support update, it would
		 * complicate the code, and won't simplify
		 * anything else.
		 */
		diag_set(ClientError, ER_UNSUPPORTED,
			 "space \"_space_sequence\"", "update");
		return -1;
	}
	enum priv_type priv_type = stmt->new_tuple ? PRIV_C : PRIV_D;

	/* Check we have the correct access type on the sequence.  * */
	if (is_generated || !stmt->new_tuple) {
		if (access_check_ddl(seq->def->name, seq->def->uid, seq->access,
				     SC_SEQUENCE, priv_type) != 0)
			return -1;
	} else {
		/*
		 * In case user wants to attach an existing sequence,
		 * check that it has read and write access.
		 */
		if (access_check_ddl(seq->def->name, seq->def->uid, seq->access,
				     SC_SEQUENCE, PRIV_R) != 0)
			return -1;
		if (access_check_ddl(seq->def->name, seq->def->uid, seq->access,
				     SC_SEQUENCE, PRIV_W) != 0)
			return -1;
	}
	/** Check we have alter access on space. */
	if (access_check_ddl(space->def->name, space->def->uid, space->access,
			     SC_SPACE, PRIV_A) != 0)
		return -1;

	if (stmt->new_tuple != NULL) {			/* INSERT, UPDATE */
		char *sequence_path;
		uint32_t sequence_fieldno;
		if (sequence_field_from_tuple(space, tuple, &sequence_path,
					      &sequence_fieldno) != 0)
			return -1;
		auto sequence_path_guard = make_scoped_guard([=] {
			free(sequence_path);
		});
		if (seq->is_generated) {
			diag_set(ClientError, ER_ALTER_SPACE,
				  space_name(space),
				  "can not attach generated sequence");
			return -1;
		}
		struct trigger *on_rollback;
		if (stmt->old_tuple != NULL)
			on_rollback = txn_alter_trigger_new(set_space_sequence,
							    stmt->old_tuple);
		else
			on_rollback = txn_alter_trigger_new(clear_space_sequence,
							    stmt->new_tuple);
		if (on_rollback == NULL)
			return -1;
		seq->is_generated = is_generated;
		space->sequence = seq;
		space->sequence_fieldno = sequence_fieldno;
		free(space->sequence_path);
		space->sequence_path = sequence_path;
		sequence_path_guard.is_active = false;
		txn_stmt_on_rollback(stmt, on_rollback);
	} else {					/* DELETE */
		struct trigger *on_rollback;
		on_rollback = txn_alter_trigger_new(set_space_sequence,
						    stmt->old_tuple);
		if (on_rollback == NULL)
			return -1;
		assert(space->sequence == seq);
		seq->is_generated = false;
		space->sequence = NULL;
		space->sequence_fieldno = 0;
		free(space->sequence_path);
		space->sequence_path = NULL;
		txn_stmt_on_rollback(stmt, on_rollback);
	}
	if (trigger_run(&on_alter_space, space) != 0)
		return -1;
	return 0;
}

/* }}} sequence */

/** Delete the new trigger on rollback of an INSERT statement. */
static int
on_create_trigger_rollback(struct trigger *trigger, void * /* event */)
{
	struct sql_trigger *old_trigger = (struct sql_trigger *)trigger->data;
	struct sql_trigger *new_trigger;
	int rc = sql_trigger_replace(sql_trigger_name(old_trigger),
				     sql_trigger_space_id(old_trigger),
				     NULL, &new_trigger);
	(void)rc;
	assert(rc == 0);
	assert(new_trigger == old_trigger);
	sql_trigger_delete(new_trigger);
	return 0;
}

/** Restore the old trigger on rollback of a DELETE statement. */
static int
on_drop_trigger_rollback(struct trigger *trigger, void * /* event */)
{
	struct sql_trigger *old_trigger = (struct sql_trigger *)trigger->data;
	struct sql_trigger *new_trigger;
	if (old_trigger == NULL)
		return 0;
	if (sql_trigger_replace(sql_trigger_name(old_trigger),
				sql_trigger_space_id(old_trigger),
				old_trigger, &new_trigger) != 0)
		panic("Out of memory on insertion into trigger hash");
	assert(new_trigger == NULL);
	return 0;
}

/**
 * Restore the old trigger and delete the new trigger on rollback
 * of a REPLACE statement.
 */
static int
on_replace_trigger_rollback(struct trigger *trigger, void * /* event */)
{
	struct sql_trigger *old_trigger = (struct sql_trigger *)trigger->data;
	struct sql_trigger *new_trigger;
	if (sql_trigger_replace(sql_trigger_name(old_trigger),
				sql_trigger_space_id(old_trigger),
				old_trigger, &new_trigger) != 0)
		panic("Out of memory on insertion into trigger hash");
	sql_trigger_delete(new_trigger);
	return 0;
}

/**
 * Trigger invoked on commit in the _trigger space.
 * Drop useless old sql_trigger AST object if any.
 */
static int
on_replace_trigger_commit(struct trigger *trigger, void * /* event */)
{
	struct sql_trigger *old_trigger = (struct sql_trigger *)trigger->data;
	sql_trigger_delete(old_trigger);
	return 0;
}

/**
 * A trigger invoked on replace in a space containing
 * SQL triggers.
 */
static int
on_replace_dd_trigger(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	struct trigger *on_rollback = txn_alter_trigger_new(NULL, NULL);
	struct trigger *on_commit =
		txn_alter_trigger_new(on_replace_trigger_commit, NULL);
	if (on_commit == NULL || on_rollback == NULL)
		return -1;

	if (old_tuple != NULL && new_tuple == NULL) {
		/* DROP trigger. */
		uint32_t trigger_name_len;
		const char *trigger_name_src = tuple_field_str(old_tuple,
			BOX_TRIGGER_FIELD_NAME, &trigger_name_len);
		if (trigger_name_src == NULL)
			return -1;
		uint32_t space_id;
		if (tuple_field_u32(old_tuple, BOX_TRIGGER_FIELD_SPACE_ID,
				    &space_id) != 0)
			return -1;
		RegionGuard region_guard(&fiber()->gc);
		char *trigger_name = (char *)region_alloc(&fiber()->gc,
							  trigger_name_len + 1);
		if (trigger_name == NULL)
			return -1;
		memcpy(trigger_name, trigger_name_src, trigger_name_len);
		trigger_name[trigger_name_len] = 0;

		struct sql_trigger *old_trigger;
		int rc = sql_trigger_replace(trigger_name, space_id, NULL,
					     &old_trigger);
		(void)rc;
		assert(rc == 0);

		on_commit->data = old_trigger;
		on_rollback->data = old_trigger;
		on_rollback->run = on_drop_trigger_rollback;
	} else {
		/* INSERT, REPLACE trigger. */
		uint32_t trigger_name_len;
		const char *trigger_name_src = tuple_field_str(new_tuple,
			BOX_TRIGGER_FIELD_NAME, &trigger_name_len);
		if (trigger_name_src == NULL)
			return -1;
		const char *space_opts = tuple_field_with_type(new_tuple,
				BOX_TRIGGER_FIELD_OPTS,MP_MAP);
		if (space_opts == NULL)
			return -1;
		struct space_opts opts;
		struct region *region = &fiber()->gc;
		RegionGuard region_guard(region);
		if (space_opts_decode(&opts, space_opts, region) != 0)
			return -1;
		struct sql_trigger *new_trigger = sql_trigger_compile(opts.sql);
		if (new_trigger == NULL)
			return -1;

		auto new_trigger_guard = make_scoped_guard([=] {
			sql_trigger_delete(new_trigger);
		});

		const char *trigger_name = sql_trigger_name(new_trigger);
		if (strlen(trigger_name) != trigger_name_len ||
		    memcmp(trigger_name_src, trigger_name,
			   trigger_name_len) != 0) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				  "trigger name does not match extracted "
				  "from SQL");
			return -1;
		}
		uint32_t space_id;
		if (tuple_field_u32(new_tuple, BOX_TRIGGER_FIELD_SPACE_ID,
				    &space_id) != 0)
			return -1;
		if (space_id != sql_trigger_space_id(new_trigger)) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				  "trigger space_id does not match the value "
				  "resolved on AST building from SQL");
			return -1;
		}
		struct space *space = space_cache_find(space_id);
		if (space != NULL && space_is_temporary(space)) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "triggers are not supported for "
				 "temporary spaces");
			return -1;
		}

		struct sql_trigger *old_trigger;
		if (sql_trigger_replace(trigger_name,
					sql_trigger_space_id(new_trigger),
					new_trigger, &old_trigger) != 0)
			return -1;

		on_commit->data = old_trigger;
		if (old_tuple != NULL) {
			on_rollback->data = old_trigger;
			on_rollback->run = on_replace_trigger_rollback;
		} else {
			on_rollback->data = new_trigger;
			on_rollback->run = on_create_trigger_rollback;
		}
		new_trigger_guard.is_active = false;
	}

	txn_stmt_on_rollback(stmt, on_rollback);
	txn_stmt_on_commit(stmt, on_commit);
	box_schema_version_bump();
	return 0;
}

/** A trigger invoked on replace in the _func_index space. */
static int
on_replace_dd_func_index(struct trigger *trigger, void *event)
{
	(void) trigger;
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	struct alter_space *alter = NULL;
	struct func *func = NULL;
	struct index *index;
	struct space *space;
	if (old_tuple == NULL && new_tuple != NULL) {
		uint32_t space_id;
		uint32_t index_id;
		uint32_t fid;
		if (tuple_field_u32(new_tuple, BOX_FUNC_INDEX_FIELD_SPACE_ID,
				    &space_id) != 0)
			return -1;
		if (tuple_field_u32(new_tuple, BOX_FUNC_INDEX_FIELD_INDEX_ID,
				    &index_id) != 0)
			return -1;
		if (tuple_field_u32(new_tuple, BOX_FUNC_INDEX_FUNCTION_ID,
	       			    &fid) != 0)
			return -1;
		space = space_cache_find(space_id);
		if (space == NULL)
			return -1;
		if (space_is_temporary(space)) {
			diag_set(ClientError, ER_UNSUPPORTED,
				 "temporary space", "functional indexes");
			return -1;
		}
		index = index_find(space, index_id);
		if (index == NULL)
			return -1;
		func = func_by_id(fid);
		if (func == NULL) {
			diag_set(ClientError, ER_NO_SUCH_FUNCTION, int2str(fid));
			return -1;
		}
		/*
		 * These checks are duplicated from the _index's on_replace
		 * trigger in order to perform required checks during recovery.
		 *
		 * See the comment above the same checks in the
		 * index_def_new_from_tuple function.
		 */
		if (func_access_check(func) != 0)
			return -1;
		if (func_index_check_func(func) != 0)
			return -1;
		if (index->def->opts.func_id != func->def->fid) {
			diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
				 "Function ids defined in _index and "
				 "_func_index don't match");
			return -1;
		}
	} else if (old_tuple != NULL && new_tuple == NULL) {
		uint32_t space_id;
		uint32_t index_id;
		if (tuple_field_u32(old_tuple, BOX_FUNC_INDEX_FIELD_SPACE_ID,
				    &space_id) != 0)
			return -1;
		if (tuple_field_u32(old_tuple, BOX_FUNC_INDEX_FIELD_INDEX_ID,
				    &index_id) != 0)
			return -1;
		space = space_cache_find(space_id);
		if (space == NULL)
			return -1;
		index = index_find(space, index_id);
		if (index == NULL)
			return -1;
		func = NULL;
	} else {
		assert(old_tuple != NULL && new_tuple != NULL);
		diag_set(ClientError, ER_UNSUPPORTED, "functional index", "alter");
		return -1;
	}

	/**
	 * Index is already initialized for corresponding
	 * function. Index rebuild is not required.
	 */
	if (index_def_get_func(index->def) == func)
		return 0;

	alter = alter_space_new(space);
	if (alter == NULL)
		return -1;
	auto scoped_guard = make_scoped_guard([=] {alter_space_delete(alter);});
	if (alter_space_move_indexes(alter, 0, index->def->iid) != 0)
		return -1;
	if (func != NULL) {
		/* Set func, rebuild the functional index. */
		try {
			(void)new RebuildFuncIndex(alter, index->def, func);
		} catch (Exception *e) {
			return -1;
		}
	} else {
		/* Reset func, disable the functional index. */
		try {
			(void)new DisableFuncIndex(alter, index->def);
		} catch (Exception *e) {
			return -1;
		}
	}
	if (alter_space_move_indexes(alter, index->def->iid + 1,
				     space->index_id_max + 1) != 0)
		return -1;
	try {
		(void) new UpdateSchemaVersion(alter);
		alter_space_do(stmt, alter);
	} catch (Exception *e) {
		return -1;
	}

	scoped_guard.is_active = false;
	return 0;
}

TRIGGER(alter_space_on_replace_space, on_replace_dd_space);
TRIGGER(alter_space_on_replace_index, on_replace_dd_index);
TRIGGER(on_replace_truncate, on_replace_dd_truncate);
TRIGGER(on_replace_schema, on_replace_dd_schema);
TRIGGER(before_replace_schema, before_replace_dd_schema);
TRIGGER(on_replace_user, on_replace_dd_user);
TRIGGER(on_replace_func, on_replace_dd_func);
TRIGGER(on_replace_collation, on_replace_dd_collation);
TRIGGER(on_replace_priv, on_replace_dd_priv);
TRIGGER(on_replace_cluster, on_replace_dd_cluster);
TRIGGER(on_replace_sequence, on_replace_dd_sequence);
TRIGGER(on_replace_sequence_data, on_replace_dd_sequence_data);
TRIGGER(on_replace_space_sequence, on_replace_dd_space_sequence);
TRIGGER(on_replace_trigger, on_replace_dd_trigger);
TRIGGER(on_replace_func_index, on_replace_dd_func_index);
/* vim: set foldmethod=marker */
