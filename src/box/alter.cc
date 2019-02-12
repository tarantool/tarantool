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
#include "schema.h"
#include "user.h"
#include "space.h"
#include "index.h"
#include "fkey.h"
#include "func.h"
#include "coll_id_cache.h"
#include "coll_id_def.h"
#include "txn.h"
#include "tuple.h"
#include "fiber.h" /* for gc_pool */
#include "scoped_guard.h"
#include "third_party/base64.h"
#include <new> /* for placement new */
#include <stdio.h> /* snprintf() */
#include <ctype.h>
#include "replication.h" /* for replica_set_id() */
#include "session.h" /* to fetch the current user. */
#include "vclock.h" /* VCLOCK_MAX */
#include "xrow.h"
#include "iproto_constants.h"
#include "identifier.h"
#include "version.h"
#include "sequence.h"
#include "sql.h"

/**
 * chap-sha1 of empty string, i.e.
 * base64_encode(sha1(sha1(""), 0)
 */
#define CHAP_SHA1_EMPTY_PASSWORD "vhvewKp0tNyweZQ+cFKAlsyphfg="

/* {{{ Auxiliary functions and methods. */

static void
access_check_ddl(const char *name, uint32_t object_id, uint32_t owner_uid,
		 enum schema_object_type type, enum priv_type priv_type)
{
	struct credentials *cr = effective_user();
	user_access_t has_access = cr->universal_access;

	user_access_t access = ((PRIV_U | (user_access_t) priv_type) &
				~has_access);
	bool is_owner = owner_uid == cr->uid || cr->uid == ADMIN;
	if (access == 0)
		return; /* Access granted. */
	/* Check for specific entity access. */
	struct access *object = entity_access_get(type);
	if (object) {
		access &= ~object[cr->auth_token].effective;
	}
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
		return; /* Access granted. */
	/*
	 * USAGE can be granted only globally.
	 */
	if (!(access & PRIV_U)) {
		/* Check for privileges on a single object. */
		struct access *object = access_find(type, object_id);
		if (object != NULL)
			access &= ~object[cr->auth_token].effective;
		if (access == 0)
			return; /* Access granted. */
	}
	/* Create a meaningful error message. */
	struct user *user = user_find_xc(cr->uid);
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
	tnt_raise(AccessDeniedError, pname, object_name, name,
		  user->def->name);
}

/**
 * Throw an exception if the given index definition
 * is incompatible with a sequence.
 */
static void
index_def_check_sequence(struct index_def *index_def, const char *space_name)
{
	enum field_type type = index_def->key_def->parts[0].type;
	if (type != FIELD_TYPE_UNSIGNED && type != FIELD_TYPE_INTEGER) {
		tnt_raise(ClientError, ER_MODIFY_INDEX, index_def->name,
			  space_name, "sequence cannot be used with "
			  "a non-integer key");
	}
}

/**
 * Support function for index_def_new_from_tuple(..)
 * Checks tuple (of _index space) and throws a nice error if it is invalid
 * Checks only types of fields and their count!
 */
static void
index_def_check_tuple(const struct tuple *tuple)
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
	return;

err:
	char got[DIAG_ERRMSG_MAX];
	char *p = got, *e = got + sizeof(got);
	data = field_start;
	for (uint32_t i = 0; i < field_count && p < e; i++) {
		enum mp_type type = mp_typeof(*data);
		mp_next(&data);
		p += snprintf(p, e - p, i ? ", %s" : "%s", mp_type_strs[type]);
	}
	tnt_raise(ClientError, ER_WRONG_INDEX_RECORD, got,
		  "space id (unsigned), index id (unsigned), name (string), "\
		  "type (string), options (map), parts (array)");
}

/**
 * Fill index_opts structure from opts field in tuple of space _index
 * Throw an error is unrecognized option.
 */
static void
index_opts_decode(struct index_opts *opts, const char *map,
		  struct region *region)
{
	index_opts_create(opts);
	if (opts_decode(opts, index_opts_reg, &map, ER_WRONG_INDEX_OPTIONS,
			BOX_INDEX_FIELD_OPTS, region) != 0)
		diag_raise();
	if (opts->distance == rtree_index_distance_type_MAX) {
		tnt_raise(ClientError, ER_WRONG_INDEX_OPTIONS,
			  BOX_INDEX_FIELD_OPTS, "distance must be either "\
			  "'euclid' or 'manhattan'");
	}
	if (opts->page_size <= 0 || (opts->range_size > 0 &&
				     opts->page_size > opts->range_size)) {
		tnt_raise(ClientError, ER_WRONG_INDEX_OPTIONS,
			  BOX_INDEX_FIELD_OPTS,
			  "page_size must be greater than 0 and "
			  "less than or equal to range_size");
	}
	if (opts->run_count_per_level <= 0) {
		tnt_raise(ClientError, ER_WRONG_INDEX_OPTIONS,
			  BOX_INDEX_FIELD_OPTS,
			  "run_count_per_level must be greater than 0");
	}
	if (opts->run_size_ratio <= 1) {
		tnt_raise(ClientError, ER_WRONG_INDEX_OPTIONS,
			  BOX_INDEX_FIELD_OPTS,
			  "run_size_ratio must be greater than 1");
	}
	if (opts->bloom_fpr <= 0 || opts->bloom_fpr > 1) {
		tnt_raise(ClientError, ER_WRONG_INDEX_OPTIONS,
			  BOX_INDEX_FIELD_OPTS,
			  "bloom_fpr must be greater than 0 and "
			  "less than or equal to 1");
	}
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
	index_def_check_tuple(tuple);

	struct index_opts opts;
	uint32_t id = tuple_field_u32_xc(tuple, BOX_INDEX_FIELD_SPACE_ID);
	uint32_t index_id = tuple_field_u32_xc(tuple, BOX_INDEX_FIELD_ID);
	enum index_type type =
		STR2ENUM(index_type, tuple_field_cstr_xc(tuple,
							 BOX_INDEX_FIELD_TYPE));
	uint32_t name_len;
	const char *name = tuple_field_str_xc(tuple, BOX_INDEX_FIELD_NAME,
					      &name_len);
	const char *opts_field =
		tuple_field_with_type_xc(tuple, BOX_INDEX_FIELD_OPTS,
					 MP_MAP);
	index_opts_decode(&opts, opts_field, &fiber()->gc);
	const char *parts = tuple_field(tuple, BOX_INDEX_FIELD_PARTS);
	uint32_t part_count = mp_decode_array(&parts);
	if (name_len > BOX_NAME_MAX) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  space_name(space), "index name is too long");
	}
	identifier_check_xc(name, name_len);
	struct key_def *key_def = NULL;
	struct key_part_def *part_def = (struct key_part_def *)
			malloc(sizeof(*part_def) * part_count);
	if (part_def == NULL) {
		tnt_raise(OutOfMemory, sizeof(*part_def) * part_count,
			  "malloc", "key_part_def");
	}
	auto key_def_guard = make_scoped_guard([&] {
		free(part_def);
		if (key_def != NULL)
			key_def_delete(key_def);
	});
	if (key_def_decode_parts(part_def, part_count, &parts,
				 space->def->fields,
				 space->def->field_count, &fiber()->gc) != 0)
		diag_raise();
	key_def = key_def_new(part_def, part_count);
	if (key_def == NULL)
		diag_raise();
	struct index_def *index_def =
		index_def_new(id, index_id, name, name_len, type,
			      &opts, key_def, space_index_key_def(space, 0));
	if (index_def == NULL)
		diag_raise();
	auto index_def_guard = make_scoped_guard([=] { index_def_delete(index_def); });
	index_def_check_xc(index_def, space_name(space));
	space_check_index_def_xc(space, index_def);
	if (index_def->iid == 0 && space->sequence != NULL)
		index_def_check_sequence(index_def, space_name(space));
	index_def_guard.is_active = false;
	return index_def;
}

/**
 * Fill space opts from the msgpack stream (MP_MAP field in the
 * tuple).
 */
static void
space_opts_decode(struct space_opts *opts, const char *map,
		  struct region *region)
{
	space_opts_create(opts);
	if (opts_decode(opts, space_opts_reg, &map, ER_WRONG_SPACE_OPTIONS,
			BOX_SPACE_FIELD_OPTS, region) != 0)
		diag_raise();
	if (opts->sql != NULL) {
		char *sql = strdup(opts->sql);
		if (sql == NULL) {
			opts->sql = NULL;
			tnt_raise(OutOfMemory, strlen(opts->sql) + 1, "strdup",
				  "sql");
		}
		opts->sql = sql;
	}
}

/**
 * Decode field definition from MessagePack map. Format:
 * {name: <string>, type: <string>}. Type is optional.
 * @param[out] field Field to decode to.
 * @param data MessagePack map to decode.
 * @param space_name Name of a space, from which the field is got.
 *        Used in error messages.
 * @param name_len Length of @a space_name.
 * @param errcode Error code to use for client errors. Either
 *        create or modify space errors.
 * @param fieldno Field number to decode. Used in error messages.
 * @param region Region to allocate field name.
 */
static void
field_def_decode(struct field_def *field, const char **data,
		 const char *space_name, uint32_t name_len,
		 uint32_t errcode, uint32_t fieldno, struct region *region)
{
	if (mp_typeof(**data) != MP_MAP) {
		tnt_raise(ClientError, errcode, tt_cstr(space_name, name_len),
			  tt_sprintf("field %d is not map",
				     fieldno + TUPLE_INDEX_BASE));
	}
	int count = mp_decode_map(data);
	*field = field_def_default;
	bool is_action_missing = true;
	uint32_t action_literal_len = strlen("nullable_action");
	for (int i = 0; i < count; ++i) {
		if (mp_typeof(**data) != MP_STR) {
			tnt_raise(ClientError, errcode,
				  tt_cstr(space_name, name_len),
				  tt_sprintf("field %d format is not map"\
					     " with string keys",
					     fieldno + TUPLE_INDEX_BASE));
		}
		uint32_t key_len;
		const char *key = mp_decode_str(data, &key_len);
		if (opts_parse_key(field, field_def_reg, key, key_len, data,
				   ER_WRONG_SPACE_FORMAT,
				   fieldno + TUPLE_INDEX_BASE, region,
				   true) != 0)
			diag_raise();
		if (is_action_missing &&
		    key_len == action_literal_len &&
		    memcmp(key, "nullable_action", action_literal_len) == 0)
			is_action_missing = false;
	}
	if (is_action_missing) {
		field->nullable_action = field->is_nullable ?
			ON_CONFLICT_ACTION_NONE
			: ON_CONFLICT_ACTION_DEFAULT;
	}
	if (field->name == NULL) {
		tnt_raise(ClientError, errcode, tt_cstr(space_name, name_len),
			  tt_sprintf("field %d name is not specified",
				     fieldno + TUPLE_INDEX_BASE));
	}
	size_t field_name_len = strlen(field->name);
	if (field_name_len > BOX_NAME_MAX) {
		tnt_raise(ClientError, errcode, tt_cstr(space_name, name_len),
			  tt_sprintf("field %d name is too long",
				     fieldno + TUPLE_INDEX_BASE));
	}
	identifier_check_xc(field->name, field_name_len);
	if (field->type == field_type_MAX) {
		tnt_raise(ClientError, errcode, tt_cstr(space_name, name_len),
			  tt_sprintf("field %d has unknown field type",
				     fieldno + TUPLE_INDEX_BASE));
	}
	if (field->nullable_action == on_conflict_action_MAX) {
		tnt_raise(ClientError, errcode, tt_cstr(space_name, name_len),
			  tt_sprintf("field %d has unknown field on conflict "
				     "nullable action",
				     fieldno + TUPLE_INDEX_BASE));
	}
	if (!((field->is_nullable && field->nullable_action ==
	       ON_CONFLICT_ACTION_NONE)
	      || (!field->is_nullable
		  && field->nullable_action != ON_CONFLICT_ACTION_NONE))) {
		tnt_raise(ClientError, errcode, tt_cstr(space_name, name_len),
			  tt_sprintf("field %d has conflicting nullability and "
				     "nullable action properties", fieldno +
				     TUPLE_INDEX_BASE));
	}
	if (field->coll_id != COLL_NONE &&
	    field->type != FIELD_TYPE_STRING &&
	    field->type != FIELD_TYPE_SCALAR &&
	    field->type != FIELD_TYPE_ANY) {
		tnt_raise(ClientError, errcode, tt_cstr(space_name, name_len),
			  tt_sprintf("collation is reasonable only for "
				     "string, scalar and any fields"));
	}

	const char *dv = field->default_value;
	if (dv != NULL) {
		field->default_value_expr = sql_expr_compile(sql_get(), dv,
							     strlen(dv));
		if (field->default_value_expr == NULL)
			diag_raise();
	}
}

/**
 * Decode MessagePack array of fields.
 * @param data MessagePack array of fields.
 * @param[out] out_count Length of a result array.
 * @param space_name Space name to use in error messages.
 * @param errcode Errcode for client errors.
 * @param region Region to allocate result array.
 *
 * @retval Array of fields.
 */
static struct field_def *
space_format_decode(const char *data, uint32_t *out_count,
		    const char *space_name, uint32_t name_len,
		    uint32_t errcode, struct region *region)
{
	/* Type is checked by _space format. */
	assert(mp_typeof(*data) == MP_ARRAY);
	uint32_t count = mp_decode_array(&data);
	*out_count = count;
	if (count == 0)
		return NULL;
	size_t size = count * sizeof(struct field_def);
	struct field_def *region_defs =
		(struct field_def *) region_alloc_xc(region, size);
	/*
	 * Nullify to prevent a case when decoding will fail in
	 * the middle and space_def_destroy_fields() below will
	 * work with garbage pointers.
	 */
	memset(region_defs, 0, size);
	auto fields_guard = make_scoped_guard([=] {
		space_def_destroy_fields(region_defs, count, false);
	});
	for (uint32_t i = 0; i < count; ++i) {
		field_def_decode(&region_defs[i], &data, space_name, name_len,
				 errcode, i, region);
	}
	fields_guard.is_active = false;
	return region_defs;
}

/**
 * Fill space_def structure from struct tuple.
 */
static struct space_def *
space_def_new_from_tuple(struct tuple *tuple, uint32_t errcode,
			 struct region *region)
{
	uint32_t name_len;
	const char *name =
		tuple_field_str_xc(tuple, BOX_SPACE_FIELD_NAME, &name_len);
	if (name_len > BOX_NAME_MAX)
		tnt_raise(ClientError, errcode,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  "space name is too long");
	identifier_check_xc(name, name_len);
	uint32_t id = tuple_field_u32_xc(tuple, BOX_SPACE_FIELD_ID);
	if (id > BOX_SPACE_MAX) {
		tnt_raise(ClientError, errcode, tt_cstr(name, name_len),
			  "space id is too big");
	}
	if (id == 0) {
		tnt_raise(ClientError, errcode, tt_cstr(name, name_len),
			  "space id 0 is reserved");
	}
	uint32_t uid = tuple_field_u32_xc(tuple, BOX_SPACE_FIELD_UID);
	uint32_t exact_field_count =
		tuple_field_u32_xc(tuple, BOX_SPACE_FIELD_FIELD_COUNT);
	uint32_t engine_name_len;
	const char *engine_name =
		tuple_field_str_xc(tuple, BOX_SPACE_FIELD_ENGINE,
				   &engine_name_len);
	/*
	 * Engines are compiled-in so their names are known in
	 * advance to be shorter than names of other identifiers.
	 */
	if (engine_name_len > ENGINE_NAME_MAX) {
		tnt_raise(ClientError, errcode, tt_cstr(name, name_len),
			  "space engine name is too long");
	}
	identifier_check_xc(engine_name, engine_name_len);
	struct field_def *fields;
	uint32_t field_count;
	/* Check space opts. */
	const char *space_opts =
		tuple_field_with_type_xc(tuple, BOX_SPACE_FIELD_OPTS,
					 MP_MAP);
	/* Check space format */
	const char *format =
		tuple_field_with_type_xc(tuple, BOX_SPACE_FIELD_FORMAT,
					 MP_ARRAY);
	fields = space_format_decode(format, &field_count, name,
				     name_len, errcode, region);
	auto fields_guard = make_scoped_guard([=] {
		space_def_destroy_fields(fields, field_count, false);
	});
	if (exact_field_count != 0 &&
	    exact_field_count < field_count) {
		tnt_raise(ClientError, errcode, tt_cstr(name, name_len),
			  "exact_field_count must be either 0 or >= "\
			  "formatted field count");
	}
	struct space_opts opts;
	space_opts_decode(&opts, space_opts, region);
	/*
	 * Currently, only predefined replication groups
	 * are supported.
	 */
	if (opts.group_id != GROUP_DEFAULT &&
	    opts.group_id != GROUP_LOCAL) {
		tnt_raise(ClientError, ER_NO_SUCH_GROUP,
			  int2str(opts.group_id));
	}
	if (opts.is_view && opts.sql == NULL)
		tnt_raise(ClientError, ER_VIEW_MISSING_SQL);
	struct space_def *def =
		space_def_new_xc(id, uid, exact_field_count, name, name_len,
				 engine_name, engine_name_len, &opts, fields,
				 field_count);
	auto def_guard = make_scoped_guard([=] { space_def_delete(def); });
	if (def->opts.checks != NULL &&
	    sql_checks_resolve_space_def_reference(def->opts.checks,
						   def) != 0) {
		box_error_t *err = box_error_last();
		if (box_error_code(err) != ENOMEM) {
			tnt_raise(ClientError, errcode, def->name,
				  box_error_message(err));
		} else {
			diag_raise();
		}
	}
	struct engine *engine = engine_find_xc(def->engine_name);
	engine_check_space_def_xc(engine, def);
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
	rlist_swap(&new_space->on_stmt_begin, &old_space->on_stmt_begin);
	/** Swap SQL Triggers pointer. */
	struct sql_trigger *new_value = new_space->sql_triggers;
	new_space->sql_triggers = old_space->sql_triggers;
	old_space->sql_triggers = new_value;
}

/** The same as for triggers - swap lists of FK constraints. */
static void
space_swap_fkeys(struct space *new_space, struct space *old_space)
{
	rlist_swap(&new_space->child_fkey, &old_space->child_fkey);
	rlist_swap(&new_space->parent_fkey, &old_space->parent_fkey);
}

/**
 * True if the space has records identified by key 'uid'.
 * Uses 'iid' index.
 */
bool
space_has_data(uint32_t id, uint32_t iid, uint32_t uid)
{
	struct space *space = space_by_id(id);
	if (space == NULL)
		return false;

	if (space_index(space, iid) == NULL)
		return false;

	struct index *index = index_find_system_xc(space, iid);
	char key[6];
	assert(mp_sizeof_uint(BOX_SYSTEM_ID_MIN) <= sizeof(key));
	mp_encode_uint(key, uid);
	struct iterator *it = index_create_iterator_xc(index, ITER_EQ, key, 1);
	IteratorGuard iter_guard(it);
	if (iterator_next_xc(it) != NULL)
		return true;
	return false;
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
		return region_aligned_calloc_xc(&fiber()->gc, size,
						alignof(uint64_t));
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
	struct trigger *trigger = (struct trigger *)
		region_calloc_object_xc(&fiber()->gc, struct trigger);
	trigger->run = run;
	trigger->data = data;
	trigger->destroy = NULL;
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
};

static struct alter_space *
alter_space_new(struct space *old_space)
{
	struct alter_space *alter =
		region_calloc_object_xc(&fiber()->gc, struct alter_space);
	rlist_create(&alter->ops);
	alter->old_space = old_space;
	alter->space_def = space_def_dup_xc(alter->old_space->def);
	if (old_space->format != NULL)
		alter->new_min_field_count = old_space->format->min_field_count;
	else
		alter->new_min_field_count = 0;
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
 * Commit the alter.
 *
 * Move all unchanged indexes from the old space to the new space.
 * Set the newly built indexes in the new space, or free memory
 * of the dropped indexes.
 * Replace the old space with a new one in the space cache.
 */
static void
alter_space_commit(struct trigger *trigger, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct alter_space *alter = (struct alter_space *) trigger->data;
	/*
	 * Commit alter ops, this will move the changed
	 * indexes into their new places.
	 */
	class AlterSpaceOp *op;
	rlist_foreach_entry(op, &alter->ops, link) {
		op->commit(alter, txn->signature);
	}

	trigger_run_xc(&on_alter_space, alter->new_space);

	alter->new_space = NULL; /* for alter_space_delete(). */
	/*
	 * Delete the old version of the space, we are not
	 * going to use it.
	 */
	space_delete(alter->old_space);
	alter_space_delete(alter);
}

/**
 * Rollback all effects of space alter. This is
 * a transaction trigger, and it fires most likely
 * upon a failed write to the WAL.
 *
 * Keep in mind that we may end up here in case of
 * alter_space_commit() failure (unlikely)
 */
static void
alter_space_rollback(struct trigger *trigger, void * /* event */)
{
	struct alter_space *alter = (struct alter_space *) trigger->data;
	/* Rollback alter ops */
	class AlterSpaceOp *op;
	rlist_foreach_entry(op, &alter->ops, link) {
		op->rollback(alter);
	}
	/* Rebuild index maps once for all indexes. */
	space_fill_index_map(alter->old_space);
	space_fill_index_map(alter->new_space);
	/*
	 * Don't forget about space triggers and foreign keys.
	 */
	space_swap_triggers(alter->new_space, alter->old_space);
	space_swap_fkeys(alter->new_space, alter->old_space);
	space_cache_replace(alter->new_space, alter->old_space);
	alter_space_delete(alter);
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
alter_space_do(struct txn *txn, struct alter_space *alter)
{
	/*
	 * Prepare triggers while we may fail. Note, we don't have to
	 * free them in case of failure, because they are allocated on
	 * the region.
	 */
	struct trigger *on_commit, *on_rollback;
	on_commit = txn_alter_trigger_new(alter_space_commit, alter);
	on_rollback = txn_alter_trigger_new(alter_space_rollback, alter);

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
	memcpy(alter->new_space->access, alter->old_space->access,
	       sizeof(alter->old_space->access));

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
	 * Don't forget about space triggers and foreign keys.
	 */
	space_swap_triggers(alter->new_space, alter->old_space);
	space_swap_fkeys(alter->new_space, alter->old_space);
	/*
	 * The new space is ready. Time to update the space
	 * cache with it.
	 */
	space_cache_replace(alter->old_space, alter->new_space);

	/*
	 * Install transaction commit/rollback triggers to either
	 * finish or rollback the DDL depending on the results of
	 * writing to WAL.
	 */
	txn_on_commit(txn, on_commit);
	txn_on_rollback(txn, on_rollback);
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

void
CheckSpaceFormat::prepare(struct alter_space *alter)
{
	struct space *new_space = alter->new_space;
	struct space *old_space = alter->old_space;
	struct tuple_format *new_format = new_space->format;
	struct tuple_format *old_format = old_space->format;
	if (old_format != NULL) {
		assert(new_format != NULL);
		if (!tuple_format1_can_store_format2_tuples(new_format,
							    old_format))
		    space_check_format_xc(old_space, new_format);
	}
}

/** Change non-essential properties of a space. */
class ModifySpace: public AlterSpaceOp
{
public:
	ModifySpace(struct alter_space *alter, struct space_def *def)
		:AlterSpaceOp(alter), new_def(def), new_dict(NULL) {}
	/* New space definition. */
	struct space_def *new_def;
	/**
	 * Newely created field dictionary. When new space_def is
	 * created, it allocates new dictionary. Alter moves new
	 * names into an old dictionary and deletes new one.
	 */
	struct tuple_dictionary *new_dict;
	virtual void alter_def(struct alter_space *alter);
	virtual void alter(struct alter_space *alter);
	virtual void rollback(struct alter_space *alter);
	virtual ~ModifySpace();
};

/** Amend the definition of the new space. */
void
ModifySpace::alter_def(struct alter_space *alter)
{
	/*
	 * Use the old dictionary for the new space, because
	 * it is already referenced by existing tuple formats.
	 * We will update it in place in ModifySpace::alter.
	 */
	new_dict = new_def->dict;
	new_def->dict = alter->old_space->def->dict;
	tuple_dictionary_ref(new_def->dict);
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
	 * Move new names into an old dictionary, which already is
	 * referenced by existing tuple formats. New dictionary
	 * object is deleted later, in destructor.
	 */
	tuple_dictionary_swap(alter->new_space->def->dict, new_dict);
}

void
ModifySpace::rollback(struct alter_space *alter)
{
	tuple_dictionary_swap(alter->new_space->def->dict, new_dict);
}

ModifySpace::~ModifySpace()
{
	if (new_dict != NULL)
		tuple_dictionary_unref(new_dict);
	if (new_def != NULL)
		space_def_delete(new_def);
}

/** DropIndex - remove an index from space. */

class DropIndex: public AlterSpaceOp
{
public:
	DropIndex(struct alter_space *alter, struct index_def *def_arg)
		:AlterSpaceOp(alter), old_index_def(def_arg) {}
	/** A reference to the definition of the dropped index. */
	struct index_def *old_index_def;
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
	rlist_del_entry(old_index_def, link);
}

/* Do the drop. */
void
DropIndex::prepare(struct alter_space *alter)
{
	if (old_index_def->iid == 0)
		space_drop_primary_key(alter->new_space);
}

void
DropIndex::commit(struct alter_space *alter, int64_t signature)
{
	struct index *index = space_index(alter->old_space,
					  old_index_def->iid);
	assert(index != NULL);
	index_commit_drop(index, signature);
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
		    struct index_def *new_index_def_arg,
		    struct index_def *old_index_def_arg)
		: AlterSpaceOp(alter),new_index_def(new_index_def_arg),
		  old_index_def(old_index_def_arg) {
	        if (new_index_def->iid == 0 &&
	            key_part_cmp(new_index_def->key_def->parts,
	                         new_index_def->key_def->part_count,
	                         old_index_def->key_def->parts,
	                         old_index_def->key_def->part_count) != 0) {
	                /*
	                 * Primary parts have been changed -
	                 * update secondary indexes.
	                 */
	                alter->pk_def = new_index_def->key_def;
	        }
	}
	struct index_def *new_index_def;
	struct index_def *old_index_def;
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
	rlist_del_entry(old_index_def, link);
	index_def_list_add(&alter->key_list, new_index_def);
}

void
ModifyIndex::alter(struct alter_space *alter)
{
	assert(old_index_def->iid == new_index_def->iid);
	/*
	 * Move the old index to the new space to preserve the
	 * original data, but use the new definition.
	 */
	space_swap_index(alter->old_space, alter->new_space,
			 old_index_def->iid, new_index_def->iid);
	struct index *old_index = space_index(alter->old_space,
					      old_index_def->iid);
	assert(old_index != NULL);
	struct index *new_index = space_index(alter->new_space,
					      new_index_def->iid);
	assert(new_index != NULL);
	SWAP(old_index->def, new_index->def);
	index_update_def(new_index);
}

void
ModifyIndex::commit(struct alter_space *alter, int64_t signature)
{
	struct index *new_index = space_index(alter->new_space,
					      new_index_def->iid);
	assert(new_index != NULL);
	index_commit_modify(new_index, signature);
}

void
ModifyIndex::rollback(struct alter_space *alter)
{
	assert(old_index_def->iid == new_index_def->iid);
	/*
	 * Restore indexes.
	 */
	space_swap_index(alter->old_space, alter->new_space,
			 old_index_def->iid, new_index_def->iid);
	struct index *old_index = space_index(alter->old_space,
					      old_index_def->iid);
	assert(old_index != NULL);
	struct index *new_index = space_index(alter->new_space,
					      new_index_def->iid);
	assert(new_index != NULL);
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
public:
	CreateIndex(struct alter_space *alter)
		:AlterSpaceOp(alter), new_index(NULL), new_index_def(NULL)
	{}
	/** New index. */
	struct index *new_index;
	/** New index index_def. */
	struct index_def *new_index_def;
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
	space_build_index_xc(alter->old_space, new_index,
			     alter->new_space->format);
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
	space_build_index_xc(alter->old_space, new_index,
			     alter->new_space->format);
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

/** TruncateIndex - truncate an index. */
class TruncateIndex: public AlterSpaceOp
{
public:
	TruncateIndex(struct alter_space *alter, uint32_t iid)
		: AlterSpaceOp(alter), iid(iid) {}
	/** id of the index to truncate. */
	uint32_t iid;
	virtual void prepare(struct alter_space *alter);
	virtual void commit(struct alter_space *alter, int64_t signature);
};

void
TruncateIndex::prepare(struct alter_space *alter)
{
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
	struct index *new_index = space_index(alter->new_space, iid);
	assert(new_index != NULL);
	space_build_index_xc(alter->new_space, new_index,
			     alter->new_space->format);
}

void
TruncateIndex::commit(struct alter_space *alter, int64_t signature)
{
	struct index *old_index = space_index(alter->old_space, iid);
	struct index *new_index = space_index(alter->new_space, iid);

	index_commit_drop(old_index, signature);
	index_commit_create(new_index, signature);
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
    ++schema_version;
}

/* }}} */

/**
 * Delete the space. It is already removed from the space cache.
 */
static void
on_drop_space_commit(struct trigger *trigger, void *event)
{
	(void) event;
	struct space *space = (struct space *)trigger->data;
	trigger_run_xc(&on_alter_space, space);
	space_delete(space);
}

/**
 * Return the original space back into the cache. The effect
 * of all other events happened after the space was removed were
 * reverted by the cascading rollback.
 */
static void
on_drop_space_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct space *space = (struct space *)trigger->data;
	space_cache_replace(NULL, space);
}

/**
 * Run the triggers registered on commit of a change in _space.
 */
static void
on_create_space_commit(struct trigger *trigger, void *event)
{
	(void) event;
	struct space *space = (struct space *)trigger->data;
	trigger_run_xc(&on_alter_space, space);
}

/**
 * A trigger invoked on commit/rollback of DROP/ADD space.
 * The trigger removes the space from the space cache.
 *
 * By the time the space is removed, it should be empty: we
 * rely on cascading rollback.
 */
static void
on_create_space_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct space *space = (struct space *)trigger->data;
	space_cache_replace(space, NULL);
	space_delete(space);
}

/**
 * Create MoveIndex operation for a range of indexes in a space
 * for range [begin, end)
 */
void
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
				(void) new ModifyIndex(alter, new_def, old_def);
			} else {
				(void) new MoveIndex(alter, old_def->iid);
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
			(void) new ModifyIndex(alter, new_def, old_def);
		else
			(void) new RebuildIndex(alter, new_def, old_def);
		guard.is_active = false;
	}
}

/**
 * Walk through all spaces from 'FROM' clause of given select,
 * and update their view reference counters.
 *
 * @param select Tables from this select to be updated.
 * @param update_value +1 on view creation, -1 on drop.
 * @param suppress_error If true, silently skip nonexistent
 *                       spaces from 'FROM' clause.
 * @param[out] not_found_space Name of a disappeared space.
 * @retval 0 on success, -1 if suppress_error is false and space
 *         from 'FROM' clause doesn't exist.
 */
static int
update_view_references(struct Select *select, int update_value,
		       bool suppress_error, const char **not_found_space)
{
	assert(update_value == 1 || update_value == -1);
	struct SrcList *list = sql_select_expand_from_tables(select);
	if (list == NULL)
		return -1;
	int from_tables_count = sql_src_list_entry_count(list);
	for (int i = 0; i < from_tables_count; ++i) {
		const char *space_name = sql_src_list_entry_name(list, i);
		if (space_name == NULL)
			continue;
		uint32_t space_id;
		if (schema_find_id(BOX_SPACE_ID, 2, space_name,
				   strlen(space_name), &space_id) != 0) {
			sqlSrcListDelete(sql_get(), list);
			return -1;
		}
		if (space_id == BOX_ID_NIL) {
			if (! suppress_error) {
				assert(not_found_space != NULL);
				*not_found_space = tt_sprintf("%s", space_name);
				sqlSrcListDelete(sql_get(), list);
				return -1;
			}
			continue;
		}
		struct space *space = space_by_id(space_id);
		assert(space->def->view_ref_count > 0 || update_value > 0);
		space->def->view_ref_count += update_value;
	}
	sqlSrcListDelete(sql_get(), list);
	return 0;
}

/**
 * Trigger which is fired to commit creation of new SQL view.
 * Its purpose is to release memory of SELECT.
 */
static void
on_create_view_commit(struct trigger *trigger, void *event)
{
	(void) event;
	struct Select *select = (struct Select *)trigger->data;
	sql_select_delete(sql_get(), select);
}

/**
 * Trigger which is fired to rollback creation of new SQL view.
 * Decrements view reference counters of dependent spaces and
 * releases memory for SELECT.
 */
static void
on_create_view_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct Select *select = (struct Select *)trigger->data;
	update_view_references(select, -1, true, NULL);
	sql_select_delete(sql_get(), select);
}

/**
 * Trigger which is fired to commit drop of SQL view.
 * Its purpose is to decrement view reference counters of
 * dependent spaces and release memory for SELECT.
 */
static void
on_drop_view_commit(struct trigger *trigger, void *event)
{
	(void) event;
	struct Select *select = (struct Select *)trigger->data;
	update_view_references(select, -1, true, NULL);
	sql_select_delete(sql_get(), select);
}

/**
 * This trigger is invoked to rollback drop of SQL view.
 * Release memory for struct SELECT compiled in
 * on_replace_dd_space trigger.
 */
static void
on_drop_view_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct Select *select = (struct Select *)trigger->data;
	sql_select_delete(sql_get(), select);
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
static void
on_replace_dd_space(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _space");
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
	uint32_t old_id = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					     BOX_SPACE_FIELD_ID);
	struct space *old_space = space_by_id(old_id);
	if (new_tuple != NULL && old_space == NULL) { /* INSERT */
		struct space_def *def =
			space_def_new_from_tuple(new_tuple, ER_CREATE_SPACE,
						 region);
		auto def_guard =
			make_scoped_guard([=] { space_def_delete(def); });
		access_check_ddl(def->name, def->id, def->uid, SC_SPACE,
				 PRIV_C);
		RLIST_HEAD(empty_list);
		struct space *space = space_new_xc(def, &empty_list);
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
		++schema_version;
		/*
		 * So may happen that until the DDL change record
		 * is written to the WAL, the space is used for
		 * insert/update/delete. All these updates are
		 * rolled back by the pipelined rollback mechanism,
		 * so it's safe to simply drop the space on
		 * rollback.
		 */
		struct trigger *on_commit =
			txn_alter_trigger_new(on_create_space_commit, space);
		txn_on_commit(txn, on_commit);
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_create_space_rollback, space);
		txn_on_rollback(txn, on_rollback);
		if (def->opts.is_view) {
			struct Select *select = sql_view_compile(sql_get(),
								 def->opts.sql);
			if (select == NULL)
				diag_raise();
			auto select_guard = make_scoped_guard([=] {
				sql_select_delete(sql_get(), select);
			});
			const char *disappeared_space;
			if (update_view_references(select, 1, false,
						   &disappeared_space) != 0) {
				/*
				 * Decrement counters which have
				 * been increased by previous call.
				 */
				update_view_references(select, -1, false,
						       &disappeared_space);
				tnt_raise(ClientError, ER_NO_SUCH_SPACE,
					  disappeared_space);
			}
			struct trigger *on_commit_view =
				txn_alter_trigger_new(on_create_view_commit,
						      select);
			txn_on_commit(txn, on_commit_view);
			struct trigger *on_rollback_view =
				txn_alter_trigger_new(on_create_view_rollback,
						      select);
			txn_on_rollback(txn, on_rollback_view);
			select_guard.is_active = false;
		}
	} else if (new_tuple == NULL) { /* DELETE */
		access_check_ddl(old_space->def->name, old_space->def->id,
				 old_space->def->uid, SC_SPACE, PRIV_D);
		/* Verify that the space is empty (has no indexes) */
		if (old_space->index_count) {
			tnt_raise(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "the space has indexes");
		}
		if (schema_find_grants("space", old_space->def->id)) {
			tnt_raise(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "the space has grants");
		}
		if (space_has_data(BOX_TRUNCATE_ID, 0, old_space->def->id))
			tnt_raise(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "the space has truncate record");
		if (old_space->def->view_ref_count > 0) {
			tnt_raise(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "other views depend on this space");
		}
		/*
		 * No need to check existence of parent keys,
		 * since if we went so far, space would'n have
		 * any indexes. But referenced space has at least
		 * one referenced index which can't be dropped
		 * before constraint itself.
		 */
		if (! rlist_empty(&old_space->child_fkey)) {
			tnt_raise(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "the space has foreign key constraints");
		}
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
		++schema_version;
		struct trigger *on_commit =
			txn_alter_trigger_new(on_drop_space_commit, old_space);
		txn_on_commit(txn, on_commit);
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_drop_space_rollback, old_space);
		txn_on_rollback(txn, on_rollback);
		if (old_space->def->opts.is_view) {
			struct Select *select =
				sql_view_compile(sql_get(),
						 old_space->def->opts.sql);
			if (select == NULL)
				diag_raise();
			auto select_guard = make_scoped_guard([=] {
				sql_select_delete(sql_get(), select);
			});
			struct trigger *on_commit_view =
				txn_alter_trigger_new(on_drop_view_commit,
						      select);
			txn_on_commit(txn, on_commit_view);
			struct trigger *on_rollback_view =
				txn_alter_trigger_new(on_drop_view_rollback,
						      select);
			txn_on_rollback(txn, on_rollback_view);
			select_guard.is_active = false;
		}
	} else { /* UPDATE, REPLACE */
		assert(old_space != NULL && new_tuple != NULL);
		struct space_def *def =
			space_def_new_from_tuple(new_tuple, ER_ALTER_SPACE,
						 region);
		auto def_guard =
			make_scoped_guard([=] { space_def_delete(def); });
		access_check_ddl(def->name, def->id, def->uid, SC_SPACE,
				 PRIV_A);
		if (def->id != space_id(old_space))
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "space id is immutable");
		if (strcmp(def->engine_name, old_space->def->engine_name) != 0)
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "can not change space engine");
		if (def->opts.group_id != space_group_id(old_space))
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "replication group is immutable");
		if (def->opts.is_view != old_space->def->opts.is_view)
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "can not convert a space to "
				  "a view and vice versa");
		if (strcmp(def->name, old_space->def->name) != 0 &&
		    old_space->def->view_ref_count > 0)
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "can not rename space which is referenced by "
				  "view");
		/*
		 * Allow change of space properties, but do it
		 * in WAL-error-safe mode.
		 */
		struct alter_space *alter = alter_space_new(old_space);
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
		struct key_def **keys;
		size_t bsize = old_space->index_count * sizeof(keys[0]);
		keys = (struct key_def **) region_alloc_xc(&fiber()->gc,
							   bsize);
		for (uint32_t i = 0; i < old_space->index_count; ++i)
			keys[i] = old_space->index[i]->def->key_def;
		alter->new_min_field_count =
			tuple_format_min_field_count(keys,
						     old_space->index_count,
						     def->fields,
						     def->field_count);
		(void) new CheckSpaceFormat(alter);
		(void) new ModifySpace(alter, def);
		def_guard.is_active = false;
		/* Create MoveIndex ops for all space indexes. */
		alter_space_move_indexes(alter, 0, old_space->index_id_max + 1);
		/* Remember to update schema_version. */
		(void) new UpdateSchemaVersion(alter);
		alter_space_do(txn, alter);
		alter_guard.is_active = false;
	}
}

/**
 * Check whether given index is referenced by some foreign key
 * constraint or not.
 * @param fkey_head List of FK constraints belonging to parent
 *        space.
 * @param iid Index id which belongs to parent space and to be
 *        tested.
 *
 * @retval True if at least one FK constraint references this
 *         index; false otherwise.
 */
static inline bool
index_is_fkey_referenced(struct rlist *fkey_head, uint32_t iid)
{
	struct fkey *fk;
	rlist_foreach_entry(fk, fkey_head, parent_link) {
		if (fk->index_id == iid)
			return true;
	}
	return false;
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
static void
on_replace_dd_index(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _index");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	uint32_t id = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					 BOX_INDEX_FIELD_SPACE_ID);
	uint32_t iid = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					  BOX_INDEX_FIELD_ID);
	struct space *old_space = space_cache_find_xc(id);
	if (old_space->def->opts.is_view) {
		tnt_raise(ClientError, ER_ALTER_SPACE, space_name(old_space),
			  "can not add index on a view");
	}
	enum priv_type priv_type = new_tuple ? PRIV_C : PRIV_D;
	if (old_tuple && new_tuple)
		priv_type = PRIV_A;
	access_check_ddl(old_space->def->name, old_space->def->id,
			 old_space->def->uid, SC_SPACE, priv_type);
	struct index *old_index = space_index(old_space, iid);

	/*
	 * Deal with various cases of dropping of the primary key.
	 */
	if (iid == 0 && new_tuple == NULL) {
		/*
		 * Dropping the primary key in a system space: off limits.
		 */
		if (space_is_system(old_space))
			tnt_raise(ClientError, ER_LAST_DROP,
				  space_name(old_space));
		/*
		 * Can't drop primary key before secondary keys.
		 */
		if (old_space->index_count > 1) {
			tnt_raise(ClientError, ER_DROP_PRIMARY_KEY,
				  space_name(old_space));
		}
		/*
		 * Can't drop primary key before space sequence.
		 */
		if (old_space->sequence != NULL) {
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "can not drop primary key while "
				  "space sequence exists");
		}
	}

	if (iid != 0 && space_index(old_space, 0) == NULL) {
		/*
		 * A secondary index can not be created without
		 * a primary key.
		 */
		tnt_raise(ClientError, ER_ALTER_SPACE,
			  space_name(old_space),
			  "can not add a secondary key before primary");
	}

	struct alter_space *alter = alter_space_new(old_space);
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
		/*
		 * Can't drop index if foreign key constraints
		 * references this index.
		 */
		if (index_is_fkey_referenced(&old_space->parent_fkey,
					     iid)) {
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(old_space),
				  "can not drop referenced index");
		}
		alter_space_move_indexes(alter, 0, iid);
		(void) new DropIndex(alter, old_index->def);
	}
	/* Case 2: create an index, if it is simply created. */
	if (old_index == NULL && new_tuple != NULL) {
		alter_space_move_indexes(alter, 0, iid);
		CreateIndex *create_index = new CreateIndex(alter);
		create_index->new_index_def =
			index_def_new_from_tuple(new_tuple, old_space);
		index_def_update_optionality(create_index->new_index_def,
					     alter->new_min_field_count);
	}
	/* Case 3 and 4: check if we need to rebuild index data. */
	if (old_index != NULL && new_tuple != NULL) {
		struct index_def *index_def;
		index_def = index_def_new_from_tuple(new_tuple, old_space);
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
		size_t bsize = old_space->index_count * sizeof(keys[0]);
		keys = (struct key_def **) region_alloc_xc(&fiber()->gc,
							   bsize);
		for (uint32_t i = 0, j = 0; i < old_space->index_count;
		     ++i) {
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
		alter_space_move_indexes(alter, 0, iid);
		if (index_def_cmp(index_def, old_index->def) == 0) {
			/* Index is not changed so just move it. */
			(void) new MoveIndex(alter, old_index->def->iid);
		} else if (index_def_change_requires_rebuild(old_index,
							     index_def)) {
			if (index_is_fkey_referenced(&old_space->parent_fkey,
						     iid)) {
				tnt_raise(ClientError, ER_ALTER_SPACE,
					  space_name(old_space),
					  "can not alter referenced index");
			}
			/*
			 * Operation demands an index rebuild.
			 */
			(void) new RebuildIndex(alter, index_def,
						old_index->def);
			index_def_guard.is_active = false;
		} else {
			/*
			 * Operation can be done without index rebuild,
			 * but we still need to check that tuples stored
			 * in the space conform to the new format.
			 */
			(void) new CheckSpaceFormat(alter);
			(void) new ModifyIndex(alter, index_def,
					       old_index->def);
			index_def_guard.is_active = false;
		}
	}
	/*
	 * Create MoveIndex ops for the remaining indexes in the
	 * old space.
	 */
	alter_space_move_indexes(alter, iid + 1, old_space->index_id_max + 1);
	/* Add an op to update schema_version on commit. */
	(void) new UpdateSchemaVersion(alter);
	alter_space_do(txn, alter);
	scoped_guard.is_active = false;
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
static void
on_replace_dd_truncate(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	txn_check_singlestatement_xc(txn, "Space _truncate");
	struct tuple *new_tuple = stmt->new_tuple;

	if (new_tuple == NULL) {
		/* Space drop - nothing to do. */
		return;
	}

	uint32_t space_id =
		tuple_field_u32_xc(new_tuple, BOX_TRUNCATE_FIELD_SPACE_ID);
	struct space *old_space = space_cache_find_xc(space_id);

	if (stmt->row->type == IPROTO_INSERT) {
		/*
		 * Space creation during initial recovery -
		 * nothing to do.
		 */
		return;
	}

	/*
	 * System spaces use triggers to keep records in sync
	 * with internal objects. Since space truncation doesn't
	 * invoke triggers, we don't permit it for system spaces.
	 */
	if (space_is_system(old_space))
		tnt_raise(ClientError, ER_TRUNCATE_SYSTEM_SPACE,
			  space_name(old_space));

	/*
	 * Check if a write privilege was given, raise an error if not.
	 */
	access_check_space_xc(old_space, PRIV_W);

	struct alter_space *alter = alter_space_new(old_space);
	auto scoped_guard =
		make_scoped_guard([=] { alter_space_delete(alter); });

	/*
	 * Recreate all indexes of the truncated space.
	 */
	for (uint32_t i = 0; i < old_space->index_count; i++) {
		struct index *old_index = old_space->index[i];
		(void) new TruncateIndex(alter, old_index->def->iid);
	}

	alter_space_do(txn, alter);
	scoped_guard.is_active = false;
}

/* {{{ access control */

bool
user_has_data(struct user *user)
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
	for (uint32_t i = 0; i < count; i++) {
		if (space_has_data(spaces[i], indexes[i], uid))
			return true;
	}
	if (! user_map_is_empty(&user->users))
		return true;
	/*
	 * If there was a role, the previous check would have
	 * returned true.
	 */
	assert(user_map_is_empty(&user->roles));
	return false;
}

/**
 * Supposedly a user may have many authentication mechanisms
 * defined, but for now we only support chap-sha1. Get
 * password of chap-sha1 from the _user space.
 */
void
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
		return;
	}
	if (mp_typeof(*auth_data) != MP_MAP) {
		/** Prevent users from making silly mistakes */
		tnt_raise(ClientError, ER_CREATE_USER,
			  user->name, "invalid password format, "
			  "use box.schema.user.passwd() to reset password");
	}
	uint32_t mech_count = mp_decode_map(&auth_data);
	for (uint32_t i = 0; i < mech_count; i++) {
		if (mp_typeof(*auth_data) != MP_STR) {
			mp_next(&auth_data);
			mp_next(&auth_data);
			continue;
		}
		uint32_t len;
		const char *mech_name = mp_decode_str(&auth_data, &len);
		if (strncasecmp(mech_name, "chap-sha1", 9) != 0) {
			mp_next(&auth_data);
			continue;
		}
		const char *hash2_base64 = mp_decode_str(&auth_data, &len);
		if (len != 0 && len != SCRAMBLE_BASE64_SIZE) {
			tnt_raise(ClientError, ER_CREATE_USER,
				  user->name, "invalid user password");
		}
		if (user->uid == GUEST) {
		    /** Guest user is permitted to have empty password */
		    if (strncmp(hash2_base64, CHAP_SHA1_EMPTY_PASSWORD, len))
			tnt_raise(ClientError, ER_GUEST_USER_PASSWORD);
		}

		base64_decode(hash2_base64, len, user->hash2,
			      sizeof(user->hash2));
		break;
	}
}

static struct user_def *
user_def_new_from_tuple(struct tuple *tuple)
{
	uint32_t name_len;
	const char *name = tuple_field_str_xc(tuple, BOX_USER_FIELD_NAME,
					      &name_len);
	if (name_len > BOX_NAME_MAX) {
		tnt_raise(ClientError, ER_CREATE_USER,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  "user name is too long");
	}
	size_t size = user_def_sizeof(name_len);
	/* Use calloc: in case user password is empty, fill it with \0 */
	struct user_def *user = (struct user_def *) malloc(size);
	if (user == NULL)
		tnt_raise(OutOfMemory, size, "malloc", "user");
	auto def_guard = make_scoped_guard([=] { free(user); });
	user->uid = tuple_field_u32_xc(tuple, BOX_USER_FIELD_ID);
	user->owner = tuple_field_u32_xc(tuple, BOX_USER_FIELD_UID);
	const char *user_type =
		tuple_field_cstr_xc(tuple, BOX_USER_FIELD_TYPE);
	user->type= schema_object_type(user_type);
	memcpy(user->name, name, name_len);
	user->name[name_len] = 0;
	if (user->type != SC_ROLE && user->type != SC_USER) {
		tnt_raise(ClientError, ER_CREATE_USER,
			  user->name, "unknown user type");
	}
	identifier_check_xc(user->name, name_len);
	/*
	 * AUTH_DATA field in _user space should contain
	 * chap-sha1 -> base64_encode(sha1(sha1(password), 0).
	 * Check for trivial errors when a plain text
	 * password is saved in this field instead.
	 */
	if (tuple_field_count(tuple) > BOX_USER_FIELD_AUTH_MECH_LIST) {
		const char *auth_data =
			tuple_field(tuple, BOX_USER_FIELD_AUTH_MECH_LIST);
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
		if (!is_auth_empty && user->type == SC_ROLE)
			tnt_raise(ClientError, ER_CREATE_ROLE, user->name,
				  "authentication data can not be set for a "\
				  "role");
		user_def_fill_auth_data(user, auth_data);
	}
	def_guard.is_active = false;
	return user;
}

static void
user_cache_remove_user(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_last_stmt(txn);
	uint32_t uid = tuple_field_u32_xc(stmt->old_tuple ?
				       stmt->old_tuple : stmt->new_tuple,
				       BOX_USER_FIELD_ID);
	user_cache_delete(uid);
}

static void
user_cache_alter_user(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_last_stmt(txn);
	struct user_def *user = user_def_new_from_tuple(stmt->new_tuple);
	auto def_guard = make_scoped_guard([=] { free(user); });
	/* Can throw if, e.g. too many users. */
	user_cache_replace(user);
	def_guard.is_active = false;
}

/**
 * A trigger invoked on replace in the user table.
 */
static void
on_replace_dd_user(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	txn_check_singlestatement_xc(txn, "Space _user");
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	uint32_t uid = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					  BOX_USER_FIELD_ID);
	struct user *old_user = user_by_id(uid);
	if (new_tuple != NULL && old_user == NULL) { /* INSERT */
		struct user_def *user = user_def_new_from_tuple(new_tuple);
		access_check_ddl(user->name, user->uid, user->owner, user->type,
				 PRIV_C);
		auto def_guard = make_scoped_guard([=] { free(user); });
		(void) user_cache_replace(user);
		def_guard.is_active = false;
		struct trigger *on_rollback =
			txn_alter_trigger_new(user_cache_remove_user, NULL);
		txn_on_rollback(txn, on_rollback);
	} else if (new_tuple == NULL) { /* DELETE */
		access_check_ddl(old_user->def->name, old_user->def->uid,
				 old_user->def->owner, old_user->def->type,
				 PRIV_D);
		/* Can't drop guest or super user */
		if (uid <= (uint32_t) BOX_SYSTEM_USER_ID_MAX || uid == SUPER) {
			tnt_raise(ClientError, ER_DROP_USER,
				  old_user->def->name,
				  "the user or the role is a system");
		}
		/*
		 * Can only delete user if it has no spaces,
		 * no functions and no grants.
		 */
		if (user_has_data(old_user)) {
			tnt_raise(ClientError, ER_DROP_USER,
				  old_user->def->name, "the user has objects");
		}
		struct trigger *on_commit =
			txn_alter_trigger_new(user_cache_remove_user, NULL);
		txn_on_commit(txn, on_commit);
	} else { /* UPDATE, REPLACE */
		assert(old_user != NULL && new_tuple != NULL);
		/*
		 * Allow change of user properties (name,
		 * password) but first check that the change is
		 * correct.
		 */
		struct user_def *user = user_def_new_from_tuple(new_tuple);
		access_check_ddl(user->name, user->uid, user->uid,
			         old_user->def->type, PRIV_A);
		auto def_guard = make_scoped_guard([=] { free(user); });
		struct trigger *on_commit =
			txn_alter_trigger_new(user_cache_alter_user, NULL);
		txn_on_commit(txn, on_commit);
	}
}

/**
 * Get function identifiers from a tuple.
 *
 * @param tuple Tuple to get ids from.
 * @param[out] fid Function identifier.
 * @param[out] uid Owner identifier.
 */
static inline void
func_def_get_ids_from_tuple(const struct tuple *tuple, uint32_t *fid,
			    uint32_t *uid)
{
	*fid = tuple_field_u32_xc(tuple, BOX_FUNC_FIELD_ID);
	*uid = tuple_field_u32_xc(tuple, BOX_FUNC_FIELD_UID);
}

/** Create a function definition from tuple. */
static struct func_def *
func_def_new_from_tuple(const struct tuple *tuple)
{
	uint32_t len;
	const char *name = tuple_field_str_xc(tuple, BOX_FUNC_FIELD_NAME,
					      &len);
	if (len > BOX_NAME_MAX)
		tnt_raise(ClientError, ER_CREATE_FUNCTION,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  "function name is too long");
	identifier_check_xc(name, len);
	struct func_def *def = (struct func_def *) malloc(func_def_sizeof(len));
	if (def == NULL)
		tnt_raise(OutOfMemory, func_def_sizeof(len), "malloc", "def");
	auto def_guard = make_scoped_guard([=] { free(def); });
	func_def_get_ids_from_tuple(tuple, &def->fid, &def->uid);
	memcpy(def->name, name, len);
	def->name[len] = 0;
	if (tuple_field_count(tuple) > BOX_FUNC_FIELD_SETUID)
		def->setuid = tuple_field_u32_xc(tuple, BOX_FUNC_FIELD_SETUID);
	else
		def->setuid = false;
	if (tuple_field_count(tuple) > BOX_FUNC_FIELD_LANGUAGE) {
		const char *language =
			tuple_field_cstr_xc(tuple, BOX_FUNC_FIELD_LANGUAGE);
		def->language = STR2ENUM(func_language, language);
		if (def->language == func_language_MAX) {
			tnt_raise(ClientError, ER_FUNCTION_LANGUAGE,
				  language, def->name);
		}
	} else {
		/* Lua is the default. */
		def->language = FUNC_LANGUAGE_LUA;
	}
	def_guard.is_active = false;
	return def;
}

/** Remove a function from function cache */
static void
func_cache_remove_func(struct trigger * /* trigger */, void *event)
{
	struct txn_stmt *stmt = txn_last_stmt((struct txn *) event);
	uint32_t fid = tuple_field_u32_xc(stmt->old_tuple ?
				       stmt->old_tuple : stmt->new_tuple,
				       BOX_FUNC_FIELD_ID);
	func_cache_delete(fid);
}

/** Replace a function in the function cache */
static void
func_cache_replace_func(struct trigger * /* trigger */, void *event)
{
	struct txn_stmt *stmt = txn_last_stmt((struct txn*) event);
	struct func_def *def = func_def_new_from_tuple(stmt->new_tuple);
	auto def_guard = make_scoped_guard([=] { free(def); });
	func_cache_replace(def);
	def_guard.is_active = false;
}

/**
 * A trigger invoked on replace in a space containing
 * functions on which there were defined any grants.
 */
static void
on_replace_dd_func(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _func");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	uint32_t fid = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					  BOX_FUNC_FIELD_ID);
	struct func *old_func = func_by_id(fid);
	if (new_tuple != NULL && old_func == NULL) { /* INSERT */
		struct func_def *def = func_def_new_from_tuple(new_tuple);
		access_check_ddl(def->name, def->fid, def->uid, SC_FUNCTION,
				 PRIV_C);
		auto def_guard = make_scoped_guard([=] { free(def); });
		func_cache_replace(def);
		def_guard.is_active = false;
		struct trigger *on_rollback =
			txn_alter_trigger_new(func_cache_remove_func, NULL);
		txn_on_rollback(txn, on_rollback);
	} else if (new_tuple == NULL) {         /* DELETE */
		uint32_t uid;
		func_def_get_ids_from_tuple(old_tuple, &fid, &uid);
		/*
		 * Can only delete func if you're the one
		 * who created it or a superuser.
		 */
		access_check_ddl(old_func->def->name, fid, uid, SC_FUNCTION,
				 PRIV_D);
		/* Can only delete func if it has no grants. */
		if (schema_find_grants("function", old_func->def->fid)) {
			tnt_raise(ClientError, ER_DROP_FUNCTION,
				  (unsigned) old_func->def->uid,
				  "function has grants");
		}
		struct trigger *on_commit =
			txn_alter_trigger_new(func_cache_remove_func, NULL);
		txn_on_commit(txn, on_commit);
	} else {                                /* UPDATE, REPLACE */
		struct func_def *def = func_def_new_from_tuple(new_tuple);
		auto def_guard = make_scoped_guard([=] { free(def); });
		access_check_ddl(def->name, def->fid, def->uid, SC_FUNCTION,
				 PRIV_A);
		struct trigger *on_commit =
			txn_alter_trigger_new(func_cache_replace_func, NULL);
		txn_on_commit(txn, on_commit);
	}
}

/** Create a collation identifier definition from tuple. */
void
coll_id_def_new_from_tuple(const struct tuple *tuple, struct coll_id_def *def)
{
	memset(def, 0, sizeof(*def));
	uint32_t name_len, locale_len, type_len;
	def->id = tuple_field_u32_xc(tuple, BOX_COLLATION_FIELD_ID);
	def->name = tuple_field_str_xc(tuple, BOX_COLLATION_FIELD_NAME, &name_len);
	def->name_len = name_len;
	if (name_len > BOX_NAME_MAX)
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "collation name is too long");
	identifier_check_xc(def->name, name_len);

	def->owner_id = tuple_field_u32_xc(tuple, BOX_COLLATION_FIELD_UID);
	struct coll_def *base = &def->base;
	const char *type = tuple_field_str_xc(tuple, BOX_COLLATION_FIELD_TYPE,
					      &type_len);
	base->type = STRN2ENUM(coll_type, type, type_len);
	if (base->type == coll_type_MAX)
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "unknown collation type");
	const char *locale =
		tuple_field_str_xc(tuple, BOX_COLLATION_FIELD_LOCALE,
				   &locale_len);
	if (locale_len > COLL_LOCALE_LEN_MAX)
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "collation locale is too long");
	if (locale_len > 0)
		identifier_check_xc(locale, locale_len);
	snprintf(base->locale, sizeof(base->locale), "%.*s", locale_len,
		 locale);
	const char *options =
		tuple_field_with_type_xc(tuple, BOX_COLLATION_FIELD_OPTIONS,
					 MP_MAP);

	if (opts_decode(&base->icu, coll_icu_opts_reg, &options,
			ER_WRONG_COLLATION_OPTIONS,
			BOX_COLLATION_FIELD_OPTIONS, NULL) != 0)
		diag_raise();

	if (base->icu.french_collation == coll_icu_on_off_MAX) {
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong french_collation option setting, "
				  "expected ON | OFF");
	}

	if (base->icu.alternate_handling == coll_icu_alternate_handling_MAX) {
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong alternate_handling option setting, "
				  "expected NON_IGNORABLE | SHIFTED");
	}

	if (base->icu.case_first == coll_icu_case_first_MAX) {
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong case_first option setting, "
				  "expected OFF | UPPER_FIRST | LOWER_FIRST");
	}

	if (base->icu.case_level == coll_icu_on_off_MAX) {
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong case_level option setting, "
				  "expected ON | OFF");
	}

	if (base->icu.normalization_mode == coll_icu_on_off_MAX) {
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong normalization_mode option setting, "
				  "expected ON | OFF");
	}

	if (base->icu.strength == coll_icu_strength_MAX) {
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong strength option setting, "
				  "expected PRIMARY | SECONDARY | "
				  "TERTIARY | QUATERNARY | IDENTICAL");
	}

	if (base->icu.numeric_collation == coll_icu_on_off_MAX) {
		tnt_raise(ClientError, ER_CANT_CREATE_COLLATION,
			  "ICU wrong numeric_collation option setting, "
				  "expected ON | OFF");
	}
}

/**
 * Rollback a change in collation space.
 * A change is only INSERT or DELETE, UPDATE is not supported.
 */
static void
coll_id_cache_rollback(struct trigger *trigger, void *event)
{
	struct coll_id *coll_id = (struct coll_id *) trigger->data;
	struct txn_stmt *stmt = txn_last_stmt((struct txn*) event);

	if (stmt->new_tuple == NULL) {
		/* DELETE: put the collation identifier back. */
		assert(stmt->old_tuple != NULL);
		struct coll_id *replaced_id;
		if (coll_id_cache_replace(coll_id, &replaced_id) != 0) {
			panic("Out of memory on insertion into collation "\
			      "cache");
		}
		assert(replaced_id == NULL);
	} else {
		/* INSERT: delete the new collation identifier. */
		assert(stmt->old_tuple == NULL);
		coll_id_cache_delete(coll_id);
		coll_id_delete(coll_id);
	}
}


/** Free a deleted collation identifier on commit. */
static void
coll_id_cache_commit(struct trigger *trigger, void *event)
{
	(void) event;
	struct coll_id *coll_id = (struct coll_id *) trigger->data;
	coll_id_delete(coll_id);
}

/**
 * A trigger invoked on replace in a space containing
 * collations that a user defined.
 */
static void
on_replace_dd_collation(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	txn_check_singlestatement_xc(txn, "Space _collation");
	struct trigger *on_rollback =
		txn_alter_trigger_new(coll_id_cache_rollback, NULL);
	struct trigger *on_commit =
		txn_alter_trigger_new(coll_id_cache_commit, NULL);
	if (new_tuple == NULL && old_tuple != NULL) {
		/* DELETE */
		/*
		 * TODO: Check that no index uses the collation
		 * identifier.
		 */
		int32_t old_id = tuple_field_u32_xc(old_tuple,
						    BOX_COLLATION_FIELD_ID);
		/*
		 * Don't allow user to drop "none" collation
		 * since it is very special and vastly used
		 * under the hood. Hence, we can rely on the
		 * fact that "none" collation features id == 0.
		 */
		if (old_id == COLL_NONE) {
			tnt_raise(ClientError, ER_DROP_COLLATION, "none",
				  "system collation");
		}
		struct coll_id *old_coll_id = coll_by_id(old_id);
		assert(old_coll_id != NULL);
		access_check_ddl(old_coll_id->name, old_coll_id->id,
				 old_coll_id->owner_id, SC_COLLATION,
				 PRIV_D);
		/*
		 * Set on_commit/on_rollback triggers after
		 * deletion from the cache to make trigger logic
		 * simple.
		 */
		coll_id_cache_delete(old_coll_id);
		on_rollback->data = old_coll_id;
		on_commit->data = old_coll_id;
		txn_on_rollback(txn, on_rollback);
		txn_on_commit(txn, on_commit);
	} else if (new_tuple != NULL && old_tuple == NULL) {
		/* INSERT */
		struct coll_id_def new_def;
		coll_id_def_new_from_tuple(new_tuple, &new_def);
		access_check_ddl(new_def.name, new_def.id, new_def.owner_id,
				 SC_COLLATION, PRIV_C);
		struct coll_id *new_coll_id = coll_id_new(&new_def);
		if (new_coll_id == NULL)
			diag_raise();
		struct coll_id *replaced_id;
		if (coll_id_cache_replace(new_coll_id, &replaced_id) != 0) {
			coll_id_delete(new_coll_id);
			diag_raise();
		}
		assert(replaced_id == NULL);
		on_rollback->data = new_coll_id;
		txn_on_rollback(txn, on_rollback);
	} else {
		/* UPDATE */
		assert(new_tuple != NULL && old_tuple != NULL);
		tnt_raise(ClientError, ER_UNSUPPORTED, "collation", "alter");
	}
}

/**
 * Create a privilege definition from tuple.
 */
void
priv_def_create_from_tuple(struct priv_def *priv, struct tuple *tuple)
{
	priv->grantor_id = tuple_field_u32_xc(tuple, BOX_PRIV_FIELD_ID);
	priv->grantee_id = tuple_field_u32_xc(tuple, BOX_PRIV_FIELD_UID);

	const char *object_type =
		tuple_field_cstr_xc(tuple, BOX_PRIV_FIELD_OBJECT_TYPE);
	priv->object_type = schema_object_type(object_type);

	const char *data = tuple_field(tuple, BOX_PRIV_FIELD_OBJECT_ID);
	if (data == NULL) {
		tnt_raise(ClientError, ER_NO_SUCH_FIELD,
			  BOX_PRIV_FIELD_OBJECT_ID + TUPLE_INDEX_BASE);
	}
	/*
	 * When granting or revoking privileges on a whole entity
	 * we pass empty string ('') to object_id to indicate
	 * grant on every object of that entity.
	 * So check for that first.
	 */
	switch (mp_typeof(*data)) {
	case MP_STR:
		if (mp_decode_strl(&data) == 0) {
			/* Entity-wide privilege. */
			priv->object_id = 0;
			priv->object_type = schema_entity_type(priv->object_type);
			break;
		}
		FALLTHROUGH;
	default:
		priv->object_id = tuple_field_u32_xc(tuple,
						     BOX_PRIV_FIELD_OBJECT_ID);
	}
	if (priv->object_type == SC_UNKNOWN) {
		tnt_raise(ClientError, ER_UNKNOWN_SCHEMA_OBJECT,
			  object_type);
	}
	priv->access = tuple_field_u32_xc(tuple, BOX_PRIV_FIELD_ACCESS);
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
static void
priv_def_check(struct priv_def *priv, enum priv_type priv_type)
{
	struct user *grantor = user_find_xc(priv->grantor_id);
	/* May be a role */
	struct user *grantee = user_by_id(priv->grantee_id);
	if (grantee == NULL) {
		tnt_raise(ClientError, ER_NO_SUCH_USER,
			  int2str(priv->grantee_id));
	}
	const char *name = schema_find_name(priv->object_type, priv->object_id);
	access_check_ddl(name, priv->object_id, grantor->def->uid,
			 priv->object_type, priv_type);
	switch (priv->object_type) {
	case SC_UNIVERSE:
		if (grantor->def->uid != ADMIN) {
			tnt_raise(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_UNIVERSE),
				  name,
				  grantor->def->name);
		}
		break;
	case SC_SPACE:
	{
		struct space *space = space_cache_find_xc(priv->object_id);
		if (space->def->uid != grantor->def->uid &&
		    grantor->def->uid != ADMIN) {
			tnt_raise(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_SPACE), name,
				  grantor->def->name);
		}
		break;
	}
	case SC_FUNCTION:
	{
		struct func *func = func_cache_find(priv->object_id);
		if (func->def->uid != grantor->def->uid &&
		    grantor->def->uid != ADMIN) {
			tnt_raise(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_FUNCTION), name,
				  grantor->def->name);
		}
		break;
	}
	case SC_SEQUENCE:
	{
		struct sequence *seq = sequence_cache_find(priv->object_id);
		if (seq->def->uid != grantor->def->uid &&
		    grantor->def->uid != ADMIN) {
			tnt_raise(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_SEQUENCE), name,
				  grantor->def->name);
		}
		break;
	}
	case SC_ROLE:
	{
		struct user *role = user_by_id(priv->object_id);
		if (role == NULL || role->def->type != SC_ROLE) {
			tnt_raise(ClientError, ER_NO_SUCH_ROLE,
				  role ? role->def->name :
				  int2str(priv->object_id));
		}
		/*
		 * Only the creator of the role can grant or revoke it.
		 * Everyone can grant 'PUBLIC' role.
		 */
		if (role->def->owner != grantor->def->uid &&
		    grantor->def->uid != ADMIN &&
		    (role->def->uid != PUBLIC || priv->access != PRIV_X)) {
			tnt_raise(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_ROLE), name,
				  grantor->def->name);
		}
		/* Not necessary to do during revoke, but who cares. */
		role_check(grantee, role);
		break;
	}
	case SC_USER:
	{
		struct user *user = user_by_id(priv->object_id);
		if (user == NULL || user->def->type != SC_USER) {
			tnt_raise(ClientError, ER_NO_SUCH_USER,
				  user ? user->def->name :
				  int2str(priv->object_id));
		}
		if (user->def->owner != grantor->def->uid &&
		    grantor->def->uid != ADMIN) {
			tnt_raise(AccessDeniedError,
				  priv_name(priv_type),
				  schema_object_name(SC_USER), name,
				  grantor->def->name);
		}
		break;
	}
	case SC_ENTITY_SPACE:
	case SC_ENTITY_FUNCTION:
	case SC_ENTITY_SEQUENCE:
	case SC_ENTITY_ROLE:
	case SC_ENTITY_USER:
	{
		/* Only admin may grant privileges on an entire entity. */
		if (grantor->def->uid != ADMIN) {
			tnt_raise(AccessDeniedError, priv_name(priv_type),
				  schema_object_name(priv->object_type), name,
				  grantor->def->name);
		}
	}
	default:
		break;
	}
	if (priv->access == 0) {
		tnt_raise(ClientError, ER_GRANT,
			  "the grant tuple has no privileges");
	}
}

/**
 * Update a metadata cache object with the new access
 * data.
 */
static void
grant_or_revoke(struct priv_def *priv)
{
	struct user *grantee = user_by_id(priv->grantee_id);
	if (grantee == NULL)
		return;
	/*
	 * Grant a role to a user only when privilege type is 'execute'
	 * and the role is specified.
	 */
	if (priv->object_type == SC_ROLE && !(priv->access & ~PRIV_X)) {
		struct user *role = user_by_id(priv->object_id);
		if (role == NULL || role->def->type != SC_ROLE)
			return;
		if (priv->access)
			role_grant(grantee, role);
		else
			role_revoke(grantee, role);
	} else {
		priv_grant(grantee, priv);
	}
}

/** A trigger called on rollback of grant, or on commit of revoke. */
static void
revoke_priv(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_last_stmt(txn);
	struct tuple *tuple = (stmt->new_tuple ?
			       stmt->new_tuple : stmt->old_tuple);
	struct priv_def priv;
	priv_def_create_from_tuple(&priv, tuple);
	/*
	 * Access to the object has been removed altogether so
	 * there should be no grants at all. If only some grants
	 * were removed, modify_priv trigger would have been
	 * invoked.
	 */
	priv.access = 0;
	grant_or_revoke(&priv);
}

/** A trigger called on rollback of grant, or on commit of revoke. */
static void
modify_priv(struct trigger * /* trigger */, void *event)
{
	struct txn_stmt *stmt = txn_last_stmt((struct txn *) event);
	struct priv_def priv;
	priv_def_create_from_tuple(&priv, stmt->new_tuple);
	grant_or_revoke(&priv);
}

/**
 * A trigger invoked on replace in the space containing
 * all granted privileges.
 */
static void
on_replace_dd_priv(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _priv");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	struct priv_def priv;

	if (new_tuple != NULL && old_tuple == NULL) {	/* grant */
		priv_def_create_from_tuple(&priv, new_tuple);
		priv_def_check(&priv, PRIV_GRANT);
		grant_or_revoke(&priv);
		struct trigger *on_rollback =
			txn_alter_trigger_new(revoke_priv, NULL);
		txn_on_rollback(txn, on_rollback);
	} else if (new_tuple == NULL) {                /* revoke */
		assert(old_tuple);
		priv_def_create_from_tuple(&priv, old_tuple);
		priv_def_check(&priv, PRIV_REVOKE);
		struct trigger *on_commit =
			txn_alter_trigger_new(revoke_priv, NULL);
		txn_on_commit(txn, on_commit);
	} else {                                       /* modify */
		priv_def_create_from_tuple(&priv, new_tuple);
		priv_def_check(&priv, PRIV_GRANT);
		struct trigger *on_commit =
			txn_alter_trigger_new(modify_priv, NULL);
		txn_on_commit(txn, on_commit);
	}
}

/* }}} access control */

/* {{{ cluster configuration */

/**
 * This trigger is invoked only upon initial recovery, when
 * reading contents of the system spaces from the snapshot.
 *
 * Before a cluster is assigned a cluster id it's read only.
 * Since during recovery state of the WAL doesn't
 * concern us, we can safely change the cluster id in before-replace
 * event, not in after-replace event.
 */
static void
on_replace_dd_schema(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _schema");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	const char *key = tuple_field_cstr_xc(new_tuple ? new_tuple : old_tuple,
					      BOX_SCHEMA_FIELD_KEY);
	if (strcmp(key, "cluster") == 0) {
		if (new_tuple == NULL)
			tnt_raise(ClientError, ER_REPLICASET_UUID_IS_RO);
		tt_uuid uu;
		tuple_field_uuid_xc(new_tuple, BOX_CLUSTER_FIELD_UUID, &uu);
		REPLICASET_UUID = uu;
	}
}

/**
 * A record with id of the new instance has been synced to the
 * write ahead log. Update the cluster configuration cache
 * with it.
 */
static void
on_commit_dd_cluster(struct trigger *trigger, void *event)
{
	(void) trigger;
	struct txn_stmt *stmt = txn_last_stmt((struct txn *) event);
	struct tuple *new_tuple = stmt->new_tuple;
	struct tuple *old_tuple = stmt->old_tuple;

	if (new_tuple == NULL) {
		struct tt_uuid old_uuid;
		tuple_field_uuid_xc(stmt->old_tuple, BOX_CLUSTER_FIELD_UUID,
				    &old_uuid);
		struct replica *replica = replica_by_uuid(&old_uuid);
		assert(replica != NULL);
		replica_clear_id(replica);
		return;
	} else if (old_tuple != NULL) {
		return; /* nothing to change */
	}

	uint32_t id = tuple_field_u32_xc(new_tuple, BOX_CLUSTER_FIELD_ID);
	tt_uuid uuid;
	tuple_field_uuid_xc(new_tuple, BOX_CLUSTER_FIELD_UUID, &uuid);
	struct replica *replica = replica_by_uuid(&uuid);
	if (replica != NULL) {
		replica_set_id(replica, id);
	} else {
		try {
			replica = replicaset_add(id, &uuid);
			/* Can't throw exceptions from on_commit trigger */
		} catch(Exception *e) {
			panic("Can't register replica: %s", e->errmsg);
		}
	}
}

/**
 * A trigger invoked on replace in the space _cluster,
 * which contains cluster configuration.
 *
 * This space is modified by JOIN command in IPROTO
 * protocol.
 *
 * The trigger updates the cluster configuration cache
 * with uuid of the newly joined instance.
 *
 * During recovery, it acts the same way, loading identifiers
 * of all instances into the cache. Instance globally unique
 * identifiers are used to keep track of cluster configuration,
 * so that a replica that previously joined a replica set can
 * follow updates, and a replica that belongs to a different
 * replica set can not by mistake join/follow another replica
 * set without first being reset (emptied).
 */
static void
on_replace_dd_cluster(struct trigger *trigger, void *event)
{
	(void) trigger;
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _cluster");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	if (new_tuple != NULL) { /* Insert or replace */
		/* Check fields */
		uint32_t replica_id =
			tuple_field_u32_xc(new_tuple, BOX_CLUSTER_FIELD_ID);
		replica_check_id(replica_id);
		tt_uuid replica_uuid;
		tuple_field_uuid_xc(new_tuple, BOX_CLUSTER_FIELD_UUID,
				    &replica_uuid);
		if (tt_uuid_is_nil(&replica_uuid))
			tnt_raise(ClientError, ER_INVALID_UUID,
				  tt_uuid_str(&replica_uuid));
		if (old_tuple != NULL) {
			/*
			 * Forbid changes of UUID for a registered instance:
			 * it requires an extra effort to keep _cluster
			 * in sync with appliers and relays.
			 */
			tt_uuid old_uuid;
			tuple_field_uuid_xc(old_tuple, BOX_CLUSTER_FIELD_UUID,
					    &old_uuid);
			if (!tt_uuid_is_equal(&replica_uuid, &old_uuid)) {
				tnt_raise(ClientError, ER_UNSUPPORTED,
					  "Space _cluster",
					  "updates of instance uuid");
			}
		}
	} else {
		/*
		 * Don't allow deletion of the record for this instance
		 * from _cluster.
		 */
		assert(old_tuple != NULL);
		uint32_t replica_id =
			tuple_field_u32_xc(old_tuple, BOX_CLUSTER_FIELD_ID);
		replica_check_id(replica_id);
	}

	struct trigger *on_commit =
			txn_alter_trigger_new(on_commit_dd_cluster, NULL);
	txn_on_commit(txn, on_commit);
}

/* }}} cluster configuration */

/* {{{ sequence */

/** Create a sequence definition from a tuple. */
static struct sequence_def *
sequence_def_new_from_tuple(struct tuple *tuple, uint32_t errcode)
{
	uint32_t name_len;
	const char *name = tuple_field_str_xc(tuple, BOX_USER_FIELD_NAME,
					      &name_len);
	if (name_len > BOX_NAME_MAX) {
		tnt_raise(ClientError, errcode,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  "sequence name is too long");
	}
	identifier_check_xc(name, name_len);
	size_t sz = sequence_def_sizeof(name_len);
	struct sequence_def *def = (struct sequence_def *) malloc(sz);
	if (def == NULL)
		tnt_raise(OutOfMemory, sz, "malloc", "sequence");
	auto def_guard = make_scoped_guard([=] { free(def); });
	memcpy(def->name, name, name_len);
	def->name[name_len] = '\0';
	def->id = tuple_field_u32_xc(tuple, BOX_SEQUENCE_FIELD_ID);
	def->uid = tuple_field_u32_xc(tuple, BOX_SEQUENCE_FIELD_UID);
	def->step = tuple_field_i64_xc(tuple, BOX_SEQUENCE_FIELD_STEP);
	def->min = tuple_field_i64_xc(tuple, BOX_SEQUENCE_FIELD_MIN);
	def->max = tuple_field_i64_xc(tuple, BOX_SEQUENCE_FIELD_MAX);
	def->start = tuple_field_i64_xc(tuple, BOX_SEQUENCE_FIELD_START);
	def->cache = tuple_field_i64_xc(tuple, BOX_SEQUENCE_FIELD_CACHE);
	def->cycle = tuple_field_bool_xc(tuple, BOX_SEQUENCE_FIELD_CYCLE);
	if (def->step == 0)
		tnt_raise(ClientError, errcode, def->name,
			  "step option must be non-zero");
	if (def->min > def->max)
		tnt_raise(ClientError, errcode, def->name,
			  "max must be greater than or equal to min");
	if (def->start < def->min || def->start > def->max)
		tnt_raise(ClientError, errcode, def->name,
			  "start must be between min and max");
	def_guard.is_active = false;
	return def;
}

/** Argument passed to on_commit_dd_sequence() trigger. */
struct alter_sequence {
	/** Trigger invoked on commit in the _sequence space. */
	struct trigger on_commit;
	/** Trigger invoked on rollback in the _sequence space. */
	struct trigger on_rollback;
	/** Old sequence definition or NULL if create. */
	struct sequence_def *old_def;
	/** New sequence defitition or NULL if drop. */
	struct sequence_def *new_def;
};

/**
 * Trigger invoked on commit in the _sequence space.
 */
static void
on_commit_dd_sequence(struct trigger *trigger, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct alter_sequence *alter = (struct alter_sequence *) trigger->data;

	if (alter->new_def != NULL && alter->old_def != NULL) {
		/* Alter a sequence. */
		sequence_cache_replace(alter->new_def);
	} else if (alter->new_def == NULL) {
		/* Drop a sequence. */
		sequence_cache_delete(alter->old_def->id);
	}

	trigger_run_xc(&on_alter_sequence, txn_last_stmt(txn));
}

/**
 * Trigger invoked on rollback in the _sequence space.
 */
static void
on_rollback_dd_sequence(struct trigger *trigger, void * /* event */)
{
	struct alter_sequence *alter = (struct alter_sequence *) trigger->data;

	if (alter->new_def != NULL && alter->old_def == NULL) {
		/* Rollback creation of a sequence. */
		sequence_cache_delete(alter->new_def->id);
	}
}

/**
 * A trigger invoked on replace in space _sequence.
 * Used to alter a sequence definition.
 */
static void
on_replace_dd_sequence(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _sequence");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	struct alter_sequence *alter =
		region_calloc_object_xc(&fiber()->gc, struct alter_sequence);

	struct sequence_def *new_def = NULL;
	auto def_guard = make_scoped_guard([=] { free(new_def); });

	if (old_tuple == NULL && new_tuple != NULL) {		/* INSERT */
		new_def = sequence_def_new_from_tuple(new_tuple,
						      ER_CREATE_SEQUENCE);
		assert(sequence_by_id(new_def->id) == NULL);
		access_check_ddl(new_def->name, new_def->id, new_def->uid,
				 SC_SEQUENCE, PRIV_C);
		sequence_cache_replace(new_def);
		alter->new_def = new_def;
	} else if (old_tuple != NULL && new_tuple == NULL) {	/* DELETE */
		uint32_t id = tuple_field_u32_xc(old_tuple,
						 BOX_SEQUENCE_DATA_FIELD_ID);
		struct sequence *seq = sequence_by_id(id);
		assert(seq != NULL);
		access_check_ddl(seq->def->name, seq->def->id, seq->def->uid,
				 SC_SEQUENCE, PRIV_D);
		if (space_has_data(BOX_SEQUENCE_DATA_ID, 0, id))
			tnt_raise(ClientError, ER_DROP_SEQUENCE,
				  seq->def->name, "the sequence has data");
		if (space_has_data(BOX_SPACE_SEQUENCE_ID, 1, id))
			tnt_raise(ClientError, ER_DROP_SEQUENCE,
				  seq->def->name, "the sequence is in use");
		if (schema_find_grants("sequence", seq->def->id))
			tnt_raise(ClientError, ER_DROP_SEQUENCE,
				  seq->def->name, "the sequence has grants");
		alter->old_def = seq->def;
	} else {						/* UPDATE */
		new_def = sequence_def_new_from_tuple(new_tuple,
						      ER_ALTER_SEQUENCE);
		struct sequence *seq = sequence_by_id(new_def->id);
		assert(seq != NULL);
		access_check_ddl(seq->def->name, seq->def->id, seq->def->uid,
				 SC_SEQUENCE, PRIV_A);
		alter->old_def = seq->def;
		alter->new_def = new_def;
	}

	def_guard.is_active = false;

	trigger_create(&alter->on_commit,
		       on_commit_dd_sequence, alter, NULL);
	txn_on_commit(txn, &alter->on_commit);
	trigger_create(&alter->on_rollback,
		       on_rollback_dd_sequence, alter, NULL);
	txn_on_rollback(txn, &alter->on_rollback);
}

/**
 * A trigger invoked on replace in space _sequence_data.
 * Used to update a sequence value.
 */
static void
on_replace_dd_sequence_data(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	uint32_t id = tuple_field_u32_xc(old_tuple ?: new_tuple,
					 BOX_SEQUENCE_DATA_FIELD_ID);
	struct sequence *seq = sequence_cache_find(id);
	if (seq == NULL)
		diag_raise();
	if (new_tuple != NULL) {			/* INSERT, UPDATE */
		int64_t value = tuple_field_i64_xc(new_tuple,
				BOX_SEQUENCE_DATA_FIELD_VALUE);
		if (sequence_set(seq, value) != 0)
			diag_raise();
	} else {					/* DELETE */
		sequence_reset(seq);
	}
}

/**
 * Run the triggers registered on commit of a change in _space.
 */
static void
on_commit_dd_space_sequence(struct trigger *trigger, void * /* event */)
{
	struct space *space = (struct space *) trigger->data;
	trigger_run_xc(&on_alter_space, space);
}

/**
 * A trigger invoked on replace in space _space_sequence.
 * Used to update space <-> sequence mapping.
 */
static void
on_replace_dd_space_sequence(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _space_sequence");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *tuple = stmt->new_tuple ? stmt->new_tuple : stmt->old_tuple;

	uint32_t space_id = tuple_field_u32_xc(tuple,
					       BOX_SPACE_SEQUENCE_FIELD_ID);
	uint32_t sequence_id = tuple_field_u32_xc(tuple,
				BOX_SPACE_SEQUENCE_FIELD_SEQUENCE_ID);
	bool is_generated = tuple_field_bool_xc(tuple,
				BOX_SPACE_SEQUENCE_FIELD_IS_GENERATED);

	struct space *space = space_cache_find_xc(space_id);
	struct sequence *seq = sequence_cache_find(sequence_id);

	enum priv_type priv_type = stmt->new_tuple ? PRIV_C : PRIV_D;
	if (stmt->new_tuple && stmt->old_tuple)
		priv_type = PRIV_A;

	/* Check we have the correct access type on the sequence.  * */
	if (is_generated || !stmt->new_tuple) {
		access_check_ddl(seq->def->name, seq->def->id, seq->def->uid,
				 SC_SEQUENCE, priv_type);
	} else {
		/*
		 * In case user wants to attach an existing sequence,
		 * check that it has read and write access.
		 */
		access_check_ddl(seq->def->name, seq->def->id, seq->def->uid,
				 SC_SEQUENCE, PRIV_R);
		access_check_ddl(seq->def->name, seq->def->id, seq->def->uid,
				 SC_SEQUENCE, PRIV_W);
	}
	/** Check we have alter access on space. */
	access_check_ddl(space->def->name, space->def->id, space->def->uid,
			 SC_SPACE, PRIV_A);

	struct trigger *on_commit =
		txn_alter_trigger_new(on_commit_dd_space_sequence, space);
	txn_on_commit(txn, on_commit);

	if (stmt->new_tuple != NULL) {			/* INSERT, UPDATE */
		struct index *pk = index_find_xc(space, 0);
		index_def_check_sequence(pk->def, space_name(space));
		if (seq->is_generated) {
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(space),
				  "can not attach generated sequence");
		}
		seq->is_generated = is_generated;
		space->sequence = seq;
	} else {					/* DELETE */
		assert(space->sequence == seq);
		space->sequence = NULL;
	}
}

/* }}} sequence */

static void
unlock_after_dd(struct trigger *trigger, void *event)
{
	(void) trigger;
	(void) event;
	latch_unlock(&schema_lock);
	/*
	 * There can be a some count of other latch awaiting fibers. All of
	 * these fibers should continue their job before current fiber fires
	 * next request. It is important especially for replication - if some
	 * rows are applied out of order then lsn order will be broken. This
	 * can be done with locking latch one more time - it guarantees that
	 * all "queued" fibers did their job before current fiber wakes next
	 * time. If there is no waiting fibers then locking will be done without
	 * any yields.
	 */
	latch_lock(&schema_lock);
	latch_unlock(&schema_lock);
}

static void
lock_before_dd(struct trigger *trigger, void *event)
{
	(void) trigger;
	if (fiber() == latch_owner(&schema_lock))
		return;
	struct txn *txn = (struct txn *)event;
	/*
	 * This trigger is executed before any check and may yield
	 * on the latch lock. But a yield in a non-autocommit
	 * memtx transaction will roll it back silently, rather
	 * than produce an error, which is very confusing.
	 * So don't try to lock a latch if there is
	 * a multi-statement transaction.
	 */
	txn_check_singlestatement_xc(txn, "DDL");
	struct trigger *on_commit =
		txn_alter_trigger_new(unlock_after_dd, NULL);
	struct trigger *on_rollback =
		txn_alter_trigger_new(unlock_after_dd, NULL);
	/*
	 * Setting triggers doesn't fail. Lock the latch last
	 * to avoid leaking the latch in case of exception.
	 */
	txn_on_commit(txn, on_commit);
	txn_on_rollback(txn, on_rollback);
	latch_lock(&schema_lock);
}

/**
 * Trigger invoked on rollback in the _trigger space.
 * Since rollback trigger is invoked after insertion to hash and space,
 * we have to delete it from those structures and release memory.
 * Vice versa, after deletion of trigger we must return it back to hash and space*.
 */
static void
on_replace_trigger_rollback(struct trigger *trigger, void *event)
{
	struct txn_stmt *stmt = txn_last_stmt((struct txn*) event);
	struct sql_trigger *old_trigger = (struct sql_trigger *)trigger->data;
	struct sql_trigger *new_trigger;

	if (stmt->old_tuple != NULL && stmt->new_tuple == NULL) {
		/* Rollback DELETE trigger. */
		if (old_trigger == NULL)
			return;
		if (sql_trigger_replace(sql_trigger_name(old_trigger),
					sql_trigger_space_id(old_trigger),
					old_trigger, &new_trigger) != 0)
			panic("Out of memory on insertion into trigger hash");
		assert(new_trigger == NULL);
	}  else if (stmt->new_tuple != NULL && stmt->old_tuple == NULL) {
		/* Rollback INSERT trigger. */
		int rc = sql_trigger_replace(sql_trigger_name(old_trigger),
					     sql_trigger_space_id(old_trigger),
					     NULL, &new_trigger);
		(void)rc;
		assert(rc == 0);
		assert(new_trigger == old_trigger);
		sql_trigger_delete(sql_get(), new_trigger);
	} else {
		/* Rollback REPLACE trigger. */
		if (sql_trigger_replace(sql_trigger_name(old_trigger),
					sql_trigger_space_id(old_trigger),
					old_trigger, &new_trigger) != 0)
			panic("Out of memory on insertion into trigger hash");
		sql_trigger_delete(sql_get(), new_trigger);
	}
}

/**
 * Trigger invoked on commit in the _trigger space.
 * Drop useless old sql_trigger AST object if any.
 */
static void
on_replace_trigger_commit(struct trigger *trigger, void * /* event */)
{
	struct sql_trigger *old_trigger = (struct sql_trigger *)trigger->data;
	sql_trigger_delete(sql_get(), old_trigger);
}

/**
 * A trigger invoked on replace in a space containing
 * SQL triggers.
 */
static void
on_replace_dd_trigger(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _trigger");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	struct trigger *on_rollback =
		txn_alter_trigger_new(on_replace_trigger_rollback, NULL);
	struct trigger *on_commit =
		txn_alter_trigger_new(on_replace_trigger_commit, NULL);

	if (old_tuple != NULL && new_tuple == NULL) {
		/* DROP trigger. */
		uint32_t trigger_name_len;
		const char *trigger_name_src =
			tuple_field_str_xc(old_tuple, BOX_TRIGGER_FIELD_NAME,
					   &trigger_name_len);
		uint32_t space_id =
			tuple_field_u32_xc(old_tuple,
					   BOX_TRIGGER_FIELD_SPACE_ID);
		char *trigger_name =
			(char *)region_alloc_xc(&fiber()->gc,
						trigger_name_len + 1);
		memcpy(trigger_name, trigger_name_src, trigger_name_len);
		trigger_name[trigger_name_len] = 0;

		struct sql_trigger *old_trigger;
		int rc = sql_trigger_replace(trigger_name, space_id, NULL,
					     &old_trigger);
		(void)rc;
		assert(rc == 0);

		on_commit->data = old_trigger;
		on_rollback->data = old_trigger;
	} else {
		/* INSERT, REPLACE trigger. */
		uint32_t trigger_name_len;
		const char *trigger_name_src =
			tuple_field_str_xc(new_tuple, BOX_TRIGGER_FIELD_NAME,
					   &trigger_name_len);

		const char *space_opts =
			tuple_field_with_type_xc(new_tuple,
						 BOX_TRIGGER_FIELD_OPTS,
						 MP_MAP);
		struct space_opts opts;
		struct region *region = &fiber()->gc;
		space_opts_decode(&opts, space_opts, region);
		struct sql_trigger *new_trigger =
			sql_trigger_compile(sql_get(), opts.sql);
		if (new_trigger == NULL)
			diag_raise();

		auto new_trigger_guard = make_scoped_guard([=] {
		    sql_trigger_delete(sql_get(), new_trigger);
		});

		const char *trigger_name = sql_trigger_name(new_trigger);
		if (strlen(trigger_name) != trigger_name_len ||
		    memcmp(trigger_name_src, trigger_name,
			   trigger_name_len) != 0) {
			tnt_raise(ClientError, ER_SQL,
				  "trigger name does not match extracted "
				  "from SQL");
		}
		uint32_t space_id =
			tuple_field_u32_xc(new_tuple,
					   BOX_TRIGGER_FIELD_SPACE_ID);
		if (space_id != sql_trigger_space_id(new_trigger)) {
			tnt_raise(ClientError, ER_SQL,
				  "trigger space_id does not match the value "
				  "resolved on AST building from SQL");
		}

		struct sql_trigger *old_trigger;
		if (sql_trigger_replace(trigger_name,
					sql_trigger_space_id(new_trigger),
					new_trigger, &old_trigger) != 0)
			diag_raise();

		on_commit->data = old_trigger;
		on_rollback->data = new_trigger;
		new_trigger_guard.is_active = false;
	}

	txn_on_rollback(txn, on_rollback);
	txn_on_commit(txn, on_commit);
}

/**
 * Decode MsgPack arrays of links. They are stored as two
 * separate arrays filled with unsigned fields numbers.
 *
 * @param tuple Tuple to be inserted into _fk_constraints.
 * @param[out] out_count Count of links.
 * @param constraint_name Constraint name to use in error
 *        messages.
 * @param constraint_len Length of constraint name.
 * @param errcode Errcode for client errors.
 * @retval Array of links.
 */
static struct field_link *
fkey_links_decode(const struct tuple *tuple, uint32_t *out_count,
		  const char *constraint_name, uint32_t constraint_len,
		  uint32_t errcode)
{
	const char *parent_cols =
		tuple_field_with_type_xc(tuple,
					 BOX_FK_CONSTRAINT_FIELD_PARENT_COLS,
					 MP_ARRAY);
	uint32_t count = mp_decode_array(&parent_cols);
	if (count == 0) {
		tnt_raise(ClientError, errcode,
			  tt_cstr(constraint_name, constraint_len),
			  "at least one link must be specified");
	}
	const char *child_cols =
		tuple_field_with_type_xc(tuple,
					 BOX_FK_CONSTRAINT_FIELD_CHILD_COLS,
					 MP_ARRAY);
	if (mp_decode_array(&child_cols) != count) {
		tnt_raise(ClientError, errcode,
			  tt_cstr(constraint_name, constraint_len),
			  "number of referenced and referencing fields "
			  "must be the same");
	}
	*out_count = count;
	size_t size = count * sizeof(struct field_link);
	struct field_link *region_links =
		(struct field_link *) region_alloc_xc(&fiber()->gc, size);
	memset(region_links, 0, size);
	for (uint32_t i = 0; i < count; ++i) {
		if (mp_typeof(*parent_cols) != MP_UINT ||
		    mp_typeof(*child_cols) != MP_UINT) {
			tnt_raise(ClientError, errcode,
				  tt_cstr(constraint_name, constraint_len),
				  tt_sprintf("value of %d link is not unsigned",
					     i));
		}
		region_links[i].parent_field = mp_decode_uint(&parent_cols);
		region_links[i].child_field = mp_decode_uint(&child_cols);
	}
	return region_links;
}

/** Create an instance of foreign key def constraint from tuple. */
static struct fkey_def *
fkey_def_new_from_tuple(const struct tuple *tuple, uint32_t errcode)
{
	uint32_t name_len;
	const char *name =
		tuple_field_str_xc(tuple, BOX_FK_CONSTRAINT_FIELD_NAME,
				   &name_len);
	if (name_len > BOX_NAME_MAX) {
		tnt_raise(ClientError, errcode,
			  tt_cstr(name, BOX_INVALID_NAME_MAX),
			  "constraint name is too long");
	}
	identifier_check_xc(name, name_len);
	uint32_t link_count;
	struct field_link *links = fkey_links_decode(tuple, &link_count, name,
						     name_len, errcode);
	size_t fkey_sz = fkey_def_sizeof(link_count, name_len);
	struct fkey_def *fk_def = (struct fkey_def *) malloc(fkey_sz);
	if (fk_def == NULL)
		tnt_raise(OutOfMemory, fkey_sz, "malloc", "fk_def");
	auto def_guard = make_scoped_guard([=] { free(fk_def); });
	memcpy(fk_def->name, name, name_len);
	fk_def->name[name_len] = '\0';
	fk_def->links = (struct field_link *)((char *)&fk_def->name +
					      name_len + 1);
	memcpy(fk_def->links, links, link_count * sizeof(struct field_link));
	fk_def->field_count = link_count;
	fk_def->child_id = tuple_field_u32_xc(tuple,
					      BOX_FK_CONSTRAINT_FIELD_CHILD_ID);
	fk_def->parent_id =
		tuple_field_u32_xc(tuple, BOX_FK_CONSTRAINT_FIELD_PARENT_ID);
	fk_def->is_deferred =
		tuple_field_bool_xc(tuple, BOX_FK_CONSTRAINT_FIELD_DEFERRED);
	const char *match = tuple_field_str_xc(tuple,
					       BOX_FK_CONSTRAINT_FIELD_MATCH,
					       &name_len);
	fk_def->match = STRN2ENUM(fkey_match, match, name_len);
	if (fk_def->match == fkey_match_MAX) {
		tnt_raise(ClientError, errcode, fk_def->name,
			  "unknown MATCH clause");
	}
	const char *on_delete_action =
		tuple_field_str_xc(tuple, BOX_FK_CONSTRAINT_FIELD_ON_DELETE,
				   &name_len);
	fk_def->on_delete = STRN2ENUM(fkey_action, on_delete_action, name_len);
	if (fk_def->on_delete == fkey_action_MAX) {
		tnt_raise(ClientError, errcode, fk_def->name,
			  "unknown ON DELETE action");
	}
	const char *on_update_action =
		tuple_field_str_xc(tuple, BOX_FK_CONSTRAINT_FIELD_ON_UPDATE,
				   &name_len);
	fk_def->on_update = STRN2ENUM(fkey_action, on_update_action, name_len);
	if (fk_def->on_update == fkey_action_MAX) {
		tnt_raise(ClientError, errcode, fk_def->name,
			  "unknown ON UPDATE action");
	}
	def_guard.is_active = false;
	return fk_def;
}

/**
 * Remove FK constraint from child's and parent's lists and
 * return it. Entries in child list are supposed to be
 * unique by their name.
 *
 * @param list List of child FK constraints.
 * @param fkey_name Name of constraint to be removed.
 * @retval FK being removed.
 */
static struct fkey *
fkey_grab_by_name(struct rlist *list, const char *fkey_name)
{
	struct fkey *fk;
	rlist_foreach_entry(fk, list, child_link) {
		if (strcmp(fkey_name, fk->def->name) == 0) {
			rlist_del_entry(fk, child_link);
			rlist_del_entry(fk, parent_link);
			return fk;
		}
	}
	unreachable();
	return NULL;
}

/**
 * FIXME: as sql legacy temporary we use such mask throught
 * SQL code. It should be replaced later with regular
 * mask from column_mask.h
 */
#define FKEY_MASK(x) (((x)>31) ? 0xffffffff : ((uint64_t)1<<(x)))

/**
 * Set bits of @mask which correspond to fields involved in
 * given foreign key constraint.
 *
 * @param fk Links of this FK constraint are used to update mask.
 * @param[out] mask Mask to be updated.
 * @param type Type of links to be used to update mask:
 *             parent or child.
 */
static inline void
fkey_set_mask(const struct fkey *fk, uint64_t *mask, int type)
{
	for (uint32_t i = 0; i < fk->def->field_count; ++i)
		*mask |= FKEY_MASK(fk->def->links[i].fields[type]);
}

/**
 * When we discard FK constraint (due to drop or rollback
 * trigger), we can't simply unset appropriate bits in mask,
 * since other constraints may refer to them as well. Thus,
 * we have nothing left to do but completely rebuild mask.
 */
static void
space_reset_fkey_mask(struct space *space)
{
	space->fkey_mask = 0;
	struct fkey *fk;
	rlist_foreach_entry(fk, &space->child_fkey, child_link)
		fkey_set_mask(fk, &space->fkey_mask, FIELD_LINK_CHILD);
	rlist_foreach_entry(fk, &space->parent_fkey, parent_link)
		fkey_set_mask(fk, &space->fkey_mask, FIELD_LINK_PARENT);
}

/**
 * On rollback of creation we remove FK constraint from DD, i.e.
 * from parent's and child's lists of constraints and
 * release memory.
 */
static void
on_create_fkey_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct fkey *fk = (struct fkey *)trigger->data;
	rlist_del_entry(fk, parent_link);
	rlist_del_entry(fk, child_link);
	space_reset_fkey_mask(space_by_id(fk->def->parent_id));
	space_reset_fkey_mask(space_by_id(fk->def->child_id));
	fkey_delete(fk);
}

/** Return old FK and release memory for the new one. */
static void
on_replace_fkey_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct fkey *old_fk = (struct fkey *)trigger->data;
	struct space *parent = space_by_id(old_fk->def->parent_id);
	struct space *child = space_by_id(old_fk->def->child_id);
	struct fkey *new_fkey = fkey_grab_by_name(&child->child_fkey,
						  old_fk->def->name);
	fkey_delete(new_fkey);
	rlist_add_entry(&child->child_fkey, old_fk, child_link);
	rlist_add_entry(&parent->parent_fkey, old_fk, parent_link);
	space_reset_fkey_mask(parent);
	space_reset_fkey_mask(child);
}

/** On rollback of drop simply return back FK to DD. */
static void
on_drop_fkey_rollback(struct trigger *trigger, void *event)
{
	(void) event;
	struct fkey *fk_to_restore = (struct fkey *)trigger->data;
	struct space *parent = space_by_id(fk_to_restore->def->parent_id);
	struct space *child = space_by_id(fk_to_restore->def->child_id);
	rlist_add_entry(&child->child_fkey, fk_to_restore, child_link);
	rlist_add_entry(&parent->parent_fkey, fk_to_restore, parent_link);
	fkey_set_mask(fk_to_restore, &child->fkey_mask, FIELD_LINK_CHILD);
	fkey_set_mask(fk_to_restore, &parent->fkey_mask, FIELD_LINK_PARENT);
}

/**
 * On commit of drop or replace we have already deleted old
 * foreign key entry from both (parent's and child's) lists,
 * so just release memory.
 */
static void
on_drop_or_replace_fkey_commit(struct trigger *trigger, void *event)
{
	(void) event;
	fkey_delete((struct fkey *) trigger->data);
}

/**
 * ANSI SQL doesn't allow list of referenced fields to contain
 * duplicates. Firstly, we try to follow the easiest way:
 * if all referenced fields numbers are less than 63, we can
 * use bit mask. Otherwise, fall through slow check where we
 * use O(field_cont^2) simple nested cycle iterations.
 */
static void
fkey_links_check_duplicates(struct fkey_def *fk_def)
{
	uint64_t field_mask = 0;
	for (uint32_t i = 0; i < fk_def->field_count; ++i) {
		uint32_t parent_field = fk_def->links[i].parent_field;
		if (parent_field > 63)
			goto slow_check;
		parent_field = ((uint64_t) 1) << parent_field;
		if ((field_mask & parent_field) != 0)
			goto error;
		field_mask |= parent_field;
	}
	return;
slow_check:
	for (uint32_t i = 0; i < fk_def->field_count; ++i) {
		uint32_t parent_field = fk_def->links[i].parent_field;
		for (uint32_t j = i + 1; j < fk_def->field_count; ++j) {
			if (parent_field == fk_def->links[j].parent_field)
				goto error;
		}
	}
	return;
error:
	tnt_raise(ClientError, ER_CREATE_FK_CONSTRAINT, fk_def->name,
		  "referenced fields can not contain duplicates");
}

/** A trigger invoked on replace in the _fk_constraint space. */
static void
on_replace_dd_fk_constraint(struct trigger * /* trigger*/, void *event)
{
	struct txn *txn = (struct txn *) event;
	txn_check_singlestatement_xc(txn, "Space _fk_constraint");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	if (new_tuple != NULL) {
		/* Create or replace foreign key. */
		struct fkey_def *fk_def =
			fkey_def_new_from_tuple(new_tuple,
						ER_CREATE_FK_CONSTRAINT);
		auto fkey_def_guard = make_scoped_guard([=] { free(fk_def); });
		struct space *child_space =
			space_cache_find_xc(fk_def->child_id);
		if (child_space->def->opts.is_view) {
			tnt_raise(ClientError, ER_CREATE_FK_CONSTRAINT,
				  fk_def->name,
				  "referencing space can't be VIEW");
		}
		struct space *parent_space =
			space_cache_find_xc(fk_def->parent_id);
		if (parent_space->def->opts.is_view) {
			tnt_raise(ClientError, ER_CREATE_FK_CONSTRAINT,
				  fk_def->name,
				  "referenced space can't be VIEW");
		}
		/*
		 * FIXME: until SQL triggers are completely
		 * integrated into server (i.e. we are able to
		 * invoke triggers even if DML occurred via Lua
		 * interface), it makes no sense to provide any
		 * checks on existing data in space.
		 */
		struct index *pk = space_index(child_space, 0);
		if (index_size(pk) > 0) {
			tnt_raise(ClientError, ER_CREATE_FK_CONSTRAINT,
				  fk_def->name,
				  "referencing space must be empty");
		}
		/* Check types of referenced fields. */
		for (uint32_t i = 0; i < fk_def->field_count; ++i) {
			uint32_t child_fieldno = fk_def->links[i].child_field;
			uint32_t parent_fieldno = fk_def->links[i].parent_field;
			if (child_fieldno >= child_space->def->field_count ||
			    parent_fieldno >= parent_space->def->field_count) {
				tnt_raise(ClientError, ER_CREATE_FK_CONSTRAINT,
					  fk_def->name, "foreign key refers to "
						        "nonexistent field");
			}
			struct field_def *child_field =
				&child_space->def->fields[child_fieldno];
			struct field_def *parent_field =
				&parent_space->def->fields[parent_fieldno];
			if (! field_type1_contains_type2(parent_field->type,
							 child_field->type)) {
				tnt_raise(ClientError, ER_CREATE_FK_CONSTRAINT,
					  fk_def->name, "field type mismatch");
			}
			if (child_field->coll_id != parent_field->coll_id) {
				tnt_raise(ClientError, ER_CREATE_FK_CONSTRAINT,
					  fk_def->name,
					  "field collation mismatch");
			}
		}
		fkey_links_check_duplicates(fk_def);
		/*
		 * Search for suitable index in parent space:
		 * it must be unique and consist exactly from
		 * referenced columns (but order may be
		 * different).
		 */
		struct index *fk_index = NULL;
		for (uint32_t i = 0; i < parent_space->index_count; ++i) {
			struct index *idx = space_index(parent_space, i);
			if (!idx->def->opts.is_unique)
				continue;
			if (idx->def->key_def->part_count !=
			    fk_def->field_count)
				continue;
			uint32_t j;
			for (j = 0; j < fk_def->field_count; ++j) {
				if (key_def_find_by_fieldno(idx->def->key_def,
							    fk_def->links[j].
							    parent_field) ==
							    NULL)
					break;
			}
			if (j != fk_def->field_count)
				continue;
			fk_index = idx;
			break;
		}
		if (fk_index == NULL) {
			tnt_raise(ClientError, ER_CREATE_FK_CONSTRAINT,
				  fk_def->name, "referenced fields don't "
						"compose unique index");
		}
		struct fkey *fkey = (struct fkey *) malloc(sizeof(*fkey));
		if (fkey == NULL)
			tnt_raise(OutOfMemory, sizeof(*fkey), "malloc", "fkey");
		auto fkey_guard = make_scoped_guard([=] { free(fkey); });
		memset(fkey, 0, sizeof(*fkey));
		fkey->def = fk_def;
		fkey->index_id = fk_index->def->iid;
		if (old_tuple == NULL) {
			rlist_add_entry(&child_space->child_fkey, fkey,
					child_link);
			rlist_add_entry(&parent_space->parent_fkey, fkey,
					parent_link);
			struct trigger *on_rollback =
				txn_alter_trigger_new(on_create_fkey_rollback,
						      fkey);
			txn_on_rollback(txn, on_rollback);
			fkey_set_mask(fkey, &parent_space->fkey_mask,
				      FIELD_LINK_PARENT);
			fkey_set_mask(fkey, &child_space->fkey_mask,
				      FIELD_LINK_CHILD);
		} else {
			struct fkey *old_fk =
				fkey_grab_by_name(&child_space->child_fkey,
						  fk_def->name);
			rlist_add_entry(&child_space->child_fkey, fkey,
					child_link);
			rlist_add_entry(&parent_space->parent_fkey, fkey,
					parent_link);
			struct trigger *on_rollback =
				txn_alter_trigger_new(on_replace_fkey_rollback,
						      old_fk);
			txn_on_rollback(txn, on_rollback);
			struct trigger *on_commit =
				txn_alter_trigger_new(on_drop_or_replace_fkey_commit,
						      old_fk);
			txn_on_commit(txn, on_commit);
			space_reset_fkey_mask(child_space);
			space_reset_fkey_mask(parent_space);
		}
		fkey_def_guard.is_active = false;
		fkey_guard.is_active = false;
	} else if (new_tuple == NULL && old_tuple != NULL) {
		/* Drop foreign key. */
		struct fkey_def *fk_def =
			fkey_def_new_from_tuple(old_tuple,
						ER_DROP_FK_CONSTRAINT);
		auto fkey_guard = make_scoped_guard([=] { free(fk_def); });
		struct space *child_space =
			space_cache_find_xc(fk_def->child_id);
		struct space *parent_space =
			space_cache_find_xc(fk_def->parent_id);
		struct fkey *old_fkey =
			fkey_grab_by_name(&child_space->child_fkey,
					  fk_def->name);
		struct trigger *on_commit =
			txn_alter_trigger_new(on_drop_or_replace_fkey_commit,
					      old_fkey);
		txn_on_commit(txn, on_commit);
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_drop_fkey_rollback, old_fkey);
		txn_on_rollback(txn, on_rollback);
		space_reset_fkey_mask(child_space);
		space_reset_fkey_mask(parent_space);
	}
}

struct trigger alter_space_on_replace_space = {
	RLIST_LINK_INITIALIZER, on_replace_dd_space, NULL, NULL
};

struct trigger alter_space_on_replace_index = {
	RLIST_LINK_INITIALIZER, on_replace_dd_index, NULL, NULL
};

struct trigger on_replace_truncate = {
	RLIST_LINK_INITIALIZER, on_replace_dd_truncate, NULL, NULL
};

struct trigger on_replace_schema = {
	RLIST_LINK_INITIALIZER, on_replace_dd_schema, NULL, NULL
};

struct trigger on_replace_user = {
	RLIST_LINK_INITIALIZER, on_replace_dd_user, NULL, NULL
};

struct trigger on_replace_func = {
	RLIST_LINK_INITIALIZER, on_replace_dd_func, NULL, NULL
};

struct trigger on_replace_collation = {
	RLIST_LINK_INITIALIZER, on_replace_dd_collation, NULL, NULL
};

struct trigger on_replace_priv = {
	RLIST_LINK_INITIALIZER, on_replace_dd_priv, NULL, NULL
};

struct trigger on_replace_cluster = {
	RLIST_LINK_INITIALIZER, on_replace_dd_cluster, NULL, NULL
};

struct trigger on_replace_sequence = {
	RLIST_LINK_INITIALIZER, on_replace_dd_sequence, NULL, NULL
};

struct trigger on_replace_sequence_data = {
	RLIST_LINK_INITIALIZER, on_replace_dd_sequence_data, NULL, NULL
};

struct trigger on_replace_space_sequence = {
	RLIST_LINK_INITIALIZER, on_replace_dd_space_sequence, NULL, NULL
};

struct trigger on_stmt_begin_space = {
	RLIST_LINK_INITIALIZER, lock_before_dd, NULL, NULL
};

struct trigger on_stmt_begin_index = {
	RLIST_LINK_INITIALIZER, lock_before_dd, NULL, NULL
};

struct trigger on_stmt_begin_truncate = {
	RLIST_LINK_INITIALIZER, lock_before_dd, NULL, NULL
};

struct trigger on_replace_trigger = {
	RLIST_LINK_INITIALIZER, on_replace_dd_trigger, NULL, NULL
};

struct trigger on_replace_fk_constraint = {
	RLIST_LINK_INITIALIZER, on_replace_dd_fk_constraint, NULL, NULL
};

/* vim: set foldmethod=marker */
