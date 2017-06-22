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
#include "memtx_index.h"
#include "func.h"
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
#include "memtx_tuple.h"

/**
 * chap-sha1 of empty string, i.e.
 * base64_encode(sha1(sha1(""))
 */
#define CHAP_SHA1_EMPTY_PASSWORD "vhvewKp0tNyweZQ+cFKAlsyphfg="

/* {{{ Auxiliary functions and methods. */

void
access_check_ddl(uint32_t owner_uid, enum schema_object_type type)
{
	struct credentials *cr = current_user();
	/*
	 * Only the owner of the object can be the grantor
	 * of the privilege on the object. This means that
	 * for universe/space/func/other persistent object,
	 * only the creator of the object can be the grantor,
	 * since Tarantool lacks separate CREATE/DROP/GRANT OPTION
	 * privileges.
	 */
	if (owner_uid != cr->uid && cr->uid != ADMIN) {
		struct user *user = user_find_xc(cr->uid);
		tnt_raise(ClientError, ER_ACCESS_DENIED,
			  "Create, drop or alter", schema_object_name(type),
			  user->def.name);
	}
}

/**
 * Support function for index_def_new_from_tuple(..)
 * Checks tuple (of _index space) and throws a nice error if it is invalid
 * Checks only types of fields and their count!
 * Additionally determines version of tuple structure
 * is_166plus is set as true if tuple structure is 1.6.6+
 * is_166plus is set as false if tuple structure is 1.6.5-
 */
static void
index_def_check_tuple(const struct tuple *tuple, bool *is_166plus)
{
	*is_166plus = true;
	const mp_type common_template[] = {MP_UINT, MP_UINT, MP_STR, MP_STR};
	const char *data = tuple_data(tuple);
	uint32_t field_count = mp_decode_array(&data);
	const char *field_start = data;
	if (field_count < 6)
		goto err;
	for (size_t i = 0; i < lengthof(common_template); i++) {
		enum mp_type type = mp_typeof(*data);
		if (type != common_template[i])
			goto err;
		mp_next(&data);
	}
	if (mp_typeof(*data) == MP_UINT) {
		/* old 1.6.5- version */
		/* TODO: removed it in newer versions, find all 1.6.5- */
		*is_166plus = false;
		mp_next(&data);
		if (mp_typeof(*data) != MP_UINT)
			goto err;
		if (field_count % 2)
			goto err;
		mp_next(&data);
		for (uint32_t i = 6; i < field_count; i += 2) {
			if (mp_typeof(*data) != MP_UINT)
				goto err;
			mp_next(&data);
			if (mp_typeof(*data) != MP_STR)
				goto err;
			mp_next(&data);
		}
	} else {
		if (field_count != 6)
			goto err;
		if (mp_typeof(*data) != MP_MAP)
			goto err;
		mp_next(&data);
		if (mp_typeof(*data) != MP_ARRAY)
			goto err;
	}
	return;

err:
	char got[DIAG_ERRMSG_MAX];
	char *p = got, *e = got + sizeof(got);
	data = field_start;
	for (uint32_t i = 0; i < field_count && p < e; i++) {
		enum mp_type type = mp_typeof(*data);
		mp_next(&data);
		const char *type_name;
		switch (type) {
		case MP_UINT:
			type_name = "number";
			break;
		case MP_STR:
			type_name = "string";
			break;
		case MP_ARRAY:
			type_name = "array";
			break;
		case MP_MAP:
			type_name = "map";
			break;
		default:
			type_name = "unknown";
			break;
		}
		p += snprintf(p, e - p, i ? ", %s" : "%s", type_name);
	}
	const char *expected;
	if (*is_166plus) {
		expected = "space id (number), index id (number), "
			"name (string), type (string), "
			"options (map), parts (array)";
	} else {
		expected = "space id (number), index id (number), "
			"name (string), type (string), "
			"is_unique (number), part count (number) "
			"part0 field no (number), "
			"part0 field type (string), ...";
	}
	tnt_raise(ClientError, ER_WRONG_INDEX_RECORD, got, expected);
}

static int
opt_set(void *opts, const struct opt_def *def, const char **val)
{
	int64_t ival;
	double dval;
	uint32_t str_len;
	const char *str;
	char *opt = ((char *) opts) + def->offset;
	switch (def->type) {
	case OPT_BOOL:
		if (mp_typeof(**val) != MP_BOOL)
			return -1;
		store_bool(opt, mp_decode_bool(val));
		break;
	case OPT_INT:
		if (mp_read_int64(val, &ival) != 0)
			return -1;
		store_u64(opt, ival);
		break;
	case OPT_FLOAT:
		if (mp_read_double(val, &dval) != 0)
			return -1;
		store_double(opt, dval);
		break;
	case OPT_STR:
		if (mp_typeof(**val) != MP_STR)
			return -1;
		str = mp_decode_str(val, &str_len);
		str_len = MIN(str_len, def->len - 1);
		memcpy(opt, str, str_len);
		opt[str_len] = '\0';
		break;
	default:
		unreachable();
	}
	return 0;
}

/**
 * Populate key options from their msgpack-encoded representation
 * (msgpack map)
 *
 * @return   the end of the msgpack map in the stream
 */
static const char *
opts_create_from_field(void *opts, const struct opt_def *reg, const char *map,
		       uint32_t errcode, uint32_t field_no)
{
	char errmsg[DIAG_ERRMSG_MAX];

	if (mp_typeof(*map) == MP_NIL)
		return map;
	if (mp_typeof(*map) != MP_MAP)
		tnt_raise(ClientError, errcode, field_no,
			  "expected a map with options");

	/*
	 * The implementation below has O(map_size * reg_size) complexity.
	 * DDL is not performance-critical, so this is not a problem.
	 */
	uint32_t map_size = mp_decode_map(&map);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*map) != MP_STR) {
			tnt_raise(ClientError, errcode, field_no,
				  "key must be a string");
		}
		uint32_t key_len;
		const char *key = mp_decode_str(&map, &key_len);
		bool found = false;
		for (const struct opt_def *def = reg; def->name != NULL; def++) {
			if (key_len != strlen(def->name) ||
			    memcmp(key, def->name, key_len) != 0)
				continue;

			if (opt_set(opts, def, &map) != 0) {
				snprintf(errmsg, sizeof(errmsg),
					"'%.*s' must be %s", key_len, key,
					opt_type_strs[def->type]);
				tnt_raise(ClientError, errcode, field_no,
					  errmsg);
			}

			found = true;
			break;
		}
		if (!found) {
			snprintf(errmsg, sizeof(errmsg),
				"unexpected option '%.*s'",
				key_len, key);
			tnt_raise(ClientError, errcode, field_no, errmsg);
		}
	}
	return map;
}

/**
 * Support function for index_def_new_from_tuple(..)
 * 1.6.6+
 * Decode distance type from message pached string to enum
 * Does not check message type, MP_STRING expected
 * Throws an error if the the value does not correspond to any enum value
 */
static enum rtree_index_distance_type
index_opts_decode_distance(const char *str)
{
	uint32_t len = strlen(str);
	if (len == strlen("euclid") &&
	    strncasecmp(str, "euclid", len) == 0) {
		return RTREE_INDEX_DISTANCE_TYPE_EUCLID;
	} else if (len == strlen("manhattan") &&
		   strncasecmp(str, "manhattan", len) == 0) {
		return RTREE_INDEX_DISTANCE_TYPE_MANHATTAN;
	} else {
		tnt_raise(ClientError,
			  ER_WRONG_INDEX_OPTIONS,
			  BOX_INDEX_FIELD_OPTS,
			  "distance must be either 'euclid' or 'manhattan'");
	}
	return RTREE_INDEX_DISTANCE_TYPE_EUCLID; /* unreachabe */
}

/**
 * Support function for index_def_new_from_tuple(..)
 * 1.6.6+
 * Fill index_opts structure from opts field in tuple of space _index
 * Throw an error is unrecognized option.
 *
 * @return  the end of the map in the msgpack stream
 */
static const char *
index_opts_create(struct index_opts *opts, const char *map)
{
	*opts = index_opts_default;
	map = opts_create_from_field(opts, index_opts_reg, map,
				     ER_WRONG_INDEX_OPTIONS,
				     BOX_INDEX_FIELD_OPTS);
	if (opts->distancebuf[0] != '\0')
		opts->distance = index_opts_decode_distance(opts->distancebuf);
	if (opts->run_count_per_level <= 0)
		tnt_raise(ClientError, ER_WRONG_INDEX_OPTIONS,
			  BOX_INDEX_FIELD_OPTS,
			  "run_count_per_level must be > 0");
	if (opts->run_size_ratio <= 1)
		tnt_raise(ClientError, ER_WRONG_SPACE_OPTIONS,
			  BOX_INDEX_FIELD_OPTS, "run_size_ratio must be > 1");
	return map;
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
index_def_new_from_tuple(struct tuple *tuple, struct space *old_space)
{
	bool is_166plus;
	index_def_check_tuple(tuple, &is_166plus);

	struct index_def *index_def;
	struct index_opts opts;
	uint32_t id = tuple_field_u32_xc(tuple, BOX_INDEX_FIELD_SPACE_ID);
	uint32_t index_id = tuple_field_u32_xc(tuple, BOX_INDEX_FIELD_ID);
	enum index_type type =
		STR2ENUM(index_type, tuple_field_cstr_xc(tuple,
							 BOX_INDEX_FIELD_TYPE));
	const char *name = tuple_field_cstr_xc(tuple, BOX_INDEX_FIELD_NAME);
	uint32_t part_count;
	const char *parts;
	if (is_166plus) {
		/* 1.6.6+ _index space structure */
		const char *opts_field =
			tuple_field(tuple, BOX_INDEX_FIELD_OPTS);
		index_opts_create(&opts, opts_field);
		parts = tuple_field(tuple, BOX_INDEX_FIELD_PARTS);
		part_count = mp_decode_array(&parts);
	} else {
		/* 1.6.5- _index space structure */
		/* TODO: remove it in newer versions, find all 1.6.5- */
		opts = index_opts_default;
		opts.is_unique =
			tuple_field_u32_xc(tuple,
					   BOX_INDEX_FIELD_IS_UNIQUE_165);
		part_count = tuple_field_u32_xc(tuple,
						BOX_INDEX_FIELD_PART_COUNT_165);
		parts = tuple_field(tuple, BOX_INDEX_FIELD_PARTS_165);
	}

	index_def = index_def_new(id, space_name(old_space), index_id, name,
				  type, &opts, part_count);
	if (index_def == NULL)
		diag_raise();
	auto scoped_guard = make_scoped_guard([=] { index_def_delete(index_def); });

	if (is_166plus) {
		/* 1.6.6+ */
		if (key_def_decode_parts(&index_def->key_def, &parts) != 0)
			diag_raise();
	} else {
		/* 1.6.5- TODO: remove it in newer versions, find all 1.6.5- */
		if (key_def_decode_parts_165(&index_def->key_def, &parts) != 0)
			diag_raise();
	}
	index_def_check(index_def, space_name(old_space));
	old_space->handler->checkIndexDef(old_space, index_def);
	scoped_guard.is_active = false;
	return index_def;
}

/**
 * Fill space opts from the msgpack stream (MP_MAP field in the
 * tuple).
 */
static void
space_opts_create(struct space_opts *opts, struct tuple *tuple)
{
	/* default values of opts */
	*opts = space_opts_default;

	/* there is no property in the space */
	if (tuple_field_count(tuple) <= BOX_SPACE_FIELD_OPTS)
		return;

	const char *data = tuple_field(tuple, BOX_SPACE_FIELD_OPTS);
	bool is_170_plus = (mp_typeof(*data) == MP_MAP);
	if (!is_170_plus) {
		/* Tarantool < 1.7.0 compatibility */
		const char *flags =
			tuple_field_cstr_xc(tuple, BOX_SPACE_FIELD_OPTS);
		while (flags && *flags) {
			while (isspace(*flags)) /* skip space */
				flags++;
			if (strncmp(flags, "temporary", strlen("temporary")) == 0)
				opts->temporary = true;
			flags = strchr(flags, ',');
			if (flags)
				flags++;
		}
	} else {
		opts_create_from_field(opts, space_opts_reg, data,
				       ER_WRONG_SPACE_OPTIONS,
				       BOX_SPACE_FIELD_OPTS);
	}
}

/**
 * Fill space_def structure from struct tuple.
 */
void
space_def_create_from_tuple(struct space_def *def, struct tuple *tuple,
			    uint32_t errcode)
{
	def->id = tuple_field_u32_xc(tuple, BOX_SPACE_FIELD_ID);
	def->uid = tuple_field_u32_xc(tuple, BOX_SPACE_FIELD_UID);
	def->exact_field_count =
		tuple_field_u32_xc(tuple, BOX_SPACE_FIELD_FIELD_COUNT);
	int namelen =
		snprintf(def->name, sizeof(def->name), "%s",
			 tuple_field_cstr_xc(tuple, BOX_SPACE_FIELD_NAME));
	int engine_namelen =
		snprintf(def->engine_name, sizeof(def->engine_name),
			 "%s", tuple_field_cstr_xc(tuple,
						   BOX_SPACE_FIELD_ENGINE));

	space_opts_create(&def->opts, tuple);
	space_def_check(def, namelen, engine_namelen, errcode);
	Engine *engine = engine_find(def->engine_name);
	engine->checkSpaceDef(def);
	access_check_ddl(def->uid, SC_SPACE);
}

/* }}} */

/* {{{ struct alter_space - the body of a full blown alter */
struct alter_space;

class AlterSpaceOp {
public:
	struct rlist link;
	virtual void prepare(struct alter_space * /* alter */) {}
	virtual void alter_def(struct alter_space * /* alter */) {}
	virtual void alter(struct alter_space * /* alter */) {}
	virtual void commit(struct alter_space * /* alter */) {}
	virtual ~AlterSpaceOp() {}
	template <typename T> static T *create();
	static void destroy(AlterSpaceOp *op);
};

template <typename T> T *
AlterSpaceOp::create()
{
	return new (region_calloc_object_xc(&fiber()->gc, T)) T;
}

void
AlterSpaceOp::destroy(AlterSpaceOp *op)
{
	op->~AlterSpaceOp();
}

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
	struct space_def space_def;
	/** Definition of the new space - keys. */
	struct rlist key_list;
	/** Old space. */
	struct space *old_space;
	/** New space. */
	struct space *new_space;
};

struct alter_space *
alter_space_new()
{
	struct alter_space *alter =
		region_calloc_object_xc(&fiber()->gc, struct alter_space);
	rlist_create(&alter->ops);
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
		AlterSpaceOp::destroy(op);
	}
	/* Delete the new space, if any. */
	if (alter->new_space)
		space_delete(alter->new_space);
}

/** Add a single operation to the list of alter operations. */
static void
alter_space_add_op(struct alter_space *alter, AlterSpaceOp *op)
{
	/* Add to the tail: operations must be processed in order. */
	rlist_add_tail_entry(&alter->ops, op, link);
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
alter_space_commit(struct trigger *trigger, void * /* event */)
{
	struct alter_space *alter = (struct alter_space *) trigger->data;
	/*
	 * If an index is unchanged, all its properties, including
	 * ID are intact. Move this index here. If an index is
	 * changed, even if this is a minor change, there is a
	 * ModifyIndex instance which will move the index from an
	 * old position to the new one.
	 */
	for (uint32_t i = 0; i < alter->new_space->index_count; i++) {
		Index *new_index = alter->new_space->index[i];
		Index *old_index = space_index(alter->old_space,
					       index_id(new_index));
		/*
		 * Move unchanged index from the old space to the
		 * new one.
		 */
		if (old_index != NULL &&
		    index_def_cmp(new_index->index_def,
				  old_index->index_def) == 0) {
			space_swap_index(alter->old_space,
					 alter->new_space,
					 index_id(old_index),
					 index_id(new_index));
		}
	}
	/*
	 * Commit alter ops, this will move the changed
	 * indexes into their new places.
	 */
	class AlterSpaceOp *op;
	rlist_foreach_entry(op, &alter->ops, link) {
		op->commit(alter);
	}
	/* Rebuild index maps once for all indexes. */
	space_fill_index_map(alter->old_space);
	space_fill_index_map(alter->new_space);
	/*
	 * Don't forget about space triggers.
	 */
	rlist_swap(&alter->new_space->on_replace,
		   &alter->old_space->on_replace);
	rlist_swap(&alter->new_space->on_stmt_begin,
		   &alter->old_space->on_stmt_begin);
	/*
	 * Init space bsize.
	 */
	if (alter->new_space->index_count != 0)
		alter->new_space->bsize = alter->old_space->bsize;
	/*
	 * The new space is ready. Time to update the space
	 * cache with it.
	 */
	alter->old_space->handler->commitAlterSpace(alter->old_space,
						    alter->new_space);

	struct space *old_space = space_cache_replace(alter->new_space);
	alter->new_space = NULL; /* for alter_space_delete(). */
	assert(old_space == alter->old_space);
	space_delete(old_space);
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
 * - the input is checked for validity; each check is
 *   encapsulated in AlterSpaceOp::prepare() method.
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
alter_space_do(struct txn *txn, struct alter_space *alter,
	       struct space *old_space)
{
#if 0
	/*
	 * Mark the space as being altered, to abort
	 * concurrent alter operations from while this
	 * alter is being written to the write ahead log.
	 * Must be the last, to make sure we reach commit and
	 * remove it. It's removed only in comit/rollback.
	 *
	 * @todo This is, essentially, an implicit pessimistic
	 * metadata lock on the space (ugly!), and it should be
	 * replaced with an explicit lock, since there is nothing
	 * worse than having to retry your alter -- usually alter
	 * is done in a script without error-checking.
	 * Plus, implicit locks are evil.
	 */
	if (space->on_replace == space_alter_on_replace)
		tnt_raise(ER_ALTER_SPACE, space_name(space));
#endif
	alter->old_space = old_space;
	alter->space_def = old_space->def;
	/* Create a definition of the new space. */
	space_dump_def(old_space, &alter->key_list);
	/*
	 * Allow for a separate prepare step so that some ops
	 * can be optimized.
	 */
	class AlterSpaceOp *op, *tmp;
	rlist_foreach_entry_safe(op, &alter->ops, link, tmp)
		op->prepare(alter);
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
	alter->new_space = space_new(&alter->space_def, &alter->key_list);
	/*
	 * Copy the replace function, the new space is at the same recovery
	 * phase as the old one. This hack is especially necessary for
	 * system spaces, which may be altered in some row in the
	 * snapshot/xlog, but needs to continue staying "fully
	 * built".
	 */
	alter->new_space->handler->prepareAlterSpace(alter->old_space,
						     alter->new_space);

	memcpy(alter->new_space->access, alter->old_space->access,
	       sizeof(alter->old_space->access));
	/*
	 * Change the new space: build the new index, rename,
	 * change the fixed field count.
	 */
	rlist_foreach_entry(op, &alter->ops, link)
		op->alter(alter);
	/*
	 * Install transaction commit/rollback triggers to either
	 * finish or rollback the DDL depending on the results of
	 * writing to WAL.
	 */
	struct trigger *on_commit =
		txn_alter_trigger_new(alter_space_commit, alter);
	txn_on_commit(txn, on_commit);
	struct trigger *on_rollback =
		txn_alter_trigger_new(alter_space_rollback, alter);
	txn_on_rollback(txn, on_rollback);
}

/* }}}  */

/* {{{ AlterSpaceOp descendants - alter operations, such as Add/Drop index */

/** Change non-essential properties of a space. */
class ModifySpace: public AlterSpaceOp
{
public:
	/* New space definition. */
	struct space_def def;
	virtual void prepare(struct alter_space *alter);
	virtual void alter_def(struct alter_space *alter);
};

/** Check that space properties are OK to change. */
void
ModifySpace::prepare(struct alter_space *alter)
{
	if (def.id != space_id(alter->old_space))
		tnt_raise(ClientError, ER_ALTER_SPACE,
			  space_name(alter->old_space),
			  "space id is immutable");

	if (strcmp(def.engine_name, alter->old_space->def.engine_name) != 0)
		tnt_raise(ClientError, ER_ALTER_SPACE,
			  space_name(alter->old_space),
			  "can not change space engine");

	if (def.exact_field_count != 0 &&
	    def.exact_field_count != alter->old_space->def.exact_field_count &&
	    space_index(alter->old_space, 0) != NULL &&
	    space_size(alter->old_space) > 0) {

		tnt_raise(ClientError, ER_ALTER_SPACE,
			  space_name(alter->old_space),
			  "can not change field count on a non-empty space");
	}

	if (def.opts.temporary != alter->old_space->def.opts.temporary &&
	    space_index(alter->old_space, 0) != NULL &&
	    space_size(alter->old_space) > 0) {
		tnt_raise(ClientError, ER_ALTER_SPACE,
			  space_name(alter->old_space),
			  "can not switch temporary flag on a non-empty space");
	}
}

/** Amend the definition of the new space. */
void
ModifySpace::alter_def(struct alter_space *alter)
{
	alter->space_def = def;
}

/** DropIndex - remove an index from space. */

class AddIndex;

class DropIndex: public AlterSpaceOp {
public:
	/** A reference to Index key def of the dropped index. */
	struct index_def *old_index_def;
	virtual void alter_def(struct alter_space *alter);
	virtual void alter(struct alter_space *alter);
	virtual void commit(struct alter_space *alter);
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
DropIndex::alter(struct alter_space *alter)
{
	/*
	 * If it's not the primary key, nothing to do --
	 * the dropped index didn't exist in the new space
	 * definition, so does not exist in the created space.
	 */
	if (space_index(alter->new_space, 0) != NULL)
		return;
	/*
	 * Deal with various cases of dropping of the primary key.
	 */
	/*
	 * Dropping the primary key in a system space: off limits.
	 */
	if (space_is_system(alter->new_space))
		tnt_raise(ClientError, ER_LAST_DROP,
			  space_name(alter->new_space));
	/*
	 * Can't drop primary key before secondary keys.
	 */
	if (alter->new_space->index_count) {
		tnt_raise(ClientError, ER_DROP_PRIMARY_KEY,
			  space_name(alter->new_space));
	}
	/*
	 * OK to drop the primary key. Inform the engine about it,
	 * since it may have to reset handler->replace function,
	 * so that:
	 * - DML returns proper errors rather than crashes the
	 *   program
	 * - when a new primary key is finally added, the space
	 *   can be put back online properly.
	 */
	alter->new_space->handler->dropPrimaryKey(alter->new_space);
}

void
DropIndex::commit(struct alter_space *alter)
{
	if (space_index(alter->new_space, old_index_def->iid) != NULL)
		return;
	Index *index = index_find_xc(alter->old_space, old_index_def->iid);
	index->commitDrop();
}

/** Change non-essential (no data change) properties of an index. */
class ModifyIndex: public AlterSpaceOp
{
public:
	struct index_def *new_index_def;
	struct index_def *old_index_def;
	virtual void alter_def(struct alter_space *alter);
	virtual void commit(struct alter_space *alter);
	virtual ~ModifyIndex();
};

/** Update the definition of the new space */
void
ModifyIndex::alter_def(struct alter_space *alter)
{
	rlist_del_entry(old_index_def, link);
	rlist_add_entry(&alter->key_list, new_index_def, link);
}

/** Move the index from the old space to the new one. */
void
ModifyIndex::commit(struct alter_space *alter)
{
	uint32_t old_id = old_index_def->iid, new_id = new_index_def->iid;
	Index *old_index = index_find_xc(alter->old_space, old_id);
	index_def_delete(old_index->index_def);
	old_index->index_def = new_index_def;
	new_index_def = NULL;
	/* Move the old index to the new place but preserve */
	space_swap_index(alter->old_space, alter->new_space, old_id, new_id);
}

ModifyIndex::~ModifyIndex()
{
	/* new_index_def is NULL if exception is raised before it's set. */
	if (new_index_def)
		index_def_delete(new_index_def);
}

/**
 * Add to index trigger -- invoked on any change in the old space,
 * while the AddIndex tuple is being written to the WAL. The job
 * of this trigger is to keep the added index up to date with the
 * state of the primary key in the old space.
 *
 * Initially it's installed as old_space->on_replace trigger, and
 * for each successfully replaced tuple in the new index,
 * a trigger is added to txn->on_rollback list to remove the tuple
 * from the new index if the transaction rolls back.
 *
 * The trigger is removed when alter operation commits/rolls back.
 */

/** AddIndex - add a new index to the space. */
class AddIndex: public AlterSpaceOp {
public:
	/** New index index_def. */
	struct index_def *new_index_def;
	struct trigger *on_replace;
	virtual void prepare(struct alter_space *alter);
	virtual void alter_def(struct alter_space *alter);
	virtual void alter(struct alter_space *alter);
	virtual void commit(struct alter_space *alter);
	virtual ~AddIndex();
};

/**
 * Optimize addition of a new index: try to either completely
 * remove it or at least avoid building from scratch.
 */
void
AddIndex::prepare(struct alter_space *alter)
{
	AlterSpaceOp *prev_op = rlist_prev_entry_safe(this, &alter->ops,
						      link);
	DropIndex *drop = dynamic_cast<DropIndex *>(prev_op);

	if (drop == NULL ||
	    index_def_change_requires_rebuild(drop->old_index_def,
						   new_index_def)) {
		/*
		 * The new index is too distinct from the old one,
		 * have to rebuild.
		 */
		return;
	}
	/* Only index meta has changed, no data change. */
	rlist_del_entry(drop, link);
	rlist_del_entry(this, link);
	/* Add ModifyIndex only if the there is a change. */
	if (index_def_cmp(drop->old_index_def, new_index_def) != 0) {
		ModifyIndex *modify = AlterSpaceOp::create<ModifyIndex>();
		alter_space_add_op(alter, modify);
		modify->new_index_def = new_index_def;
		new_index_def = NULL;
		modify->old_index_def = drop->old_index_def;
	}
	AlterSpaceOp::destroy(drop);
	AlterSpaceOp::destroy(this);
}

/** Add definition of the new key to the new space def. */
void
AddIndex::alter_def(struct alter_space *alter)
{
	rlist_add_tail_entry(&alter->key_list, new_index_def, link);
}

/**
 * A trigger invoked on rollback in old space while the record
 * about alter is being written to the WAL.
 */
static void
on_rollback_in_old_space(struct trigger *trigger, void *event)
{
	struct txn *txn = (struct txn *) event;
	Index *new_index = (Index *) trigger->data;
	/* Remove the failed tuple from the new index. */
	struct txn_stmt *stmt;
	stailq_foreach_entry(stmt, &txn->stmts, next) {
		if (stmt->space->def.id != new_index->index_def->space_id)
			continue;
		new_index->replace(stmt->new_tuple, stmt->old_tuple,
				   DUP_INSERT);
	}
}

/**
 * A trigger invoked on replace in old space while
 * the record about alter is being written to the WAL.
 */
static void
on_replace_in_old_space(struct trigger *trigger, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	Index *new_index = (Index *) trigger->data;
	/*
	 * First set a rollback trigger, then do replace, since
	 * creating the trigger may fail.
	 */
	struct trigger *on_rollback =
		txn_alter_trigger_new(on_rollback_in_old_space, new_index);
	/*
	 * In a multi-statement transaction the same space
	 * may be modified many times, but we need only one
	 * on_rollback trigger.
	 */
	txn_init_triggers(txn);
	trigger_add_unique(&txn->on_rollback, on_rollback);
	/* Put the tuple into the new index. */
	(void) new_index->replace(stmt->old_tuple, stmt->new_tuple,
				  DUP_INSERT);
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
AddIndex::alter(struct alter_space *alter)
{
	Handler *handler = alter->new_space->handler;

	if (space_index(alter->old_space, 0) == NULL) {
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
			handler->addPrimaryKey(alter->new_space);
		} else {
			/*
			 * Adding a secondary key.
			 */
			tnt_raise(ClientError, ER_ALTER_SPACE,
				  space_name(alter->new_space),
				  "can not add a secondary key before primary");
		}
		return;
	}
	/**
	 * Get the new index and build it.
	 */
	Index *new_index = index_find_xc(alter->new_space, new_index_def->iid);
	handler->buildSecondaryKey(alter->old_space, alter->new_space, new_index);
	on_replace = txn_alter_trigger_new(on_replace_in_old_space,
					   new_index);
	trigger_add(&alter->old_space->on_replace, on_replace);
}

void
AddIndex::commit(struct alter_space *alter)
{
	Index *new_index = index_find_xc(alter->new_space, new_index_def->iid);
	new_index->commitCreate();
}

AddIndex::~AddIndex()
{
	/*
	 * The trigger by now may reside in the new space (on
	 * commit) or in the old space (rollback). Remove it
	 * from the list, wherever it is.
	 */
	if (on_replace)
		trigger_clear(on_replace);
	if (new_index_def)
		index_def_delete(new_index_def);
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
	space_cache_replace(space);
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
	struct space *cached = space_cache_delete(space_id(space));
	(void) cached;
	assert(cached == space);
	space_delete(space);
}

/**
 * A trigger which is invoked on replace in a data dictionary
 * space _space.
 *
 * Generally, whenever a data dictionary change occurs
 * 2 things should be done:
 *
 * - space cache should be updated, and changes in the space
 *   cache should be reflected in Lua bindings
 *   (this is done in space_cache_replace() and
 *   space_cache_delete())
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
	txn_check_autocommit(txn, "Space _space");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	/*
	 * Things to keep in mind:
	 * - old_tuple is set only in case of UPDATE.  For INSERT
	 *   or REPLACE it is NULL.
	 * - the trigger may be called inside recovery from a snapshot,
	 *   when index look up is not possible
	 * - _space, _index and other metaspaces initially don't
	 *   have a tuple which represents it, this tuple is only
	 *   created during recovery from
	 *   a snapshot.
	 *
	 * Let's establish whether an old space exists. Use
	 * old_tuple ID field, if old_tuple is set, since UPDATE
	 * may have changed space id.
	 */
	uint32_t old_id = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					     BOX_SPACE_FIELD_ID);
	struct space *old_space = space_by_id(old_id);
	if (new_tuple != NULL && old_space == NULL) { /* INSERT */
		struct space_def def;
		space_def_create_from_tuple(&def, new_tuple, ER_CREATE_SPACE);
		RLIST_HEAD(empty_list);
		struct space *space = space_new(&def, &empty_list);
		/**
		 * The new space must be inserted in the space
		 * cache right away to achieve linearisable
		 * execution on a replica.
		 */
		(void) space_cache_replace(space);
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
		txn_on_rollback(txn, on_rollback);
	} else if (new_tuple == NULL) { /* DELETE */
		access_check_ddl(old_space->def.uid, SC_SPACE);
		/* Verify that the space is empty (has no indexes) */
		if (old_space->index_count) {
			tnt_raise(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "the space has indexes");
		}
		if (schema_find_grants("space", old_space->def.id)) {
			tnt_raise(ClientError, ER_DROP_SPACE,
				  space_name(old_space),
				  "the space has grants");
		}
		/**
		 * The space must be deleted from the space
		 * cache right away to achieve linearisable
		 * execution on a replica.
		 */
		struct space *space = space_cache_delete(space_id(old_space));
		struct trigger *on_commit =
			txn_alter_trigger_new(on_drop_space_commit, space);
		txn_on_commit(txn, on_commit);
		struct trigger *on_rollback =
			txn_alter_trigger_new(on_drop_space_rollback, space);
		txn_on_rollback(txn, on_rollback);
	} else { /* UPDATE, REPLACE */
		assert(old_space != NULL && new_tuple != NULL);
		/*
		 * Allow change of space properties, but do it
		 * in WAL-error-safe mode.
		 */
		struct alter_space *alter = alter_space_new();
		auto scoped_guard =
			make_scoped_guard([=] {alter_space_delete(alter);});
		ModifySpace *modify =
			AlterSpaceOp::create<ModifySpace>();
		alter_space_add_op(alter, modify);
		space_def_create_from_tuple(&modify->def, new_tuple,
					    ER_ALTER_SPACE);
		alter_space_do(txn, alter, old_space);
		scoped_guard.is_active = false;
	}
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
	txn_check_autocommit(txn, "Space _index");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	uint32_t id = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					 BOX_INDEX_FIELD_SPACE_ID);
	uint32_t iid = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					  BOX_INDEX_FIELD_ID);
	struct space *old_space = space_cache_find(id);
	access_check_ddl(old_space->def.uid, SC_SPACE);
	Index *old_index = space_index(old_space, iid);
	struct alter_space *alter = alter_space_new();
	auto scoped_guard =
		make_scoped_guard([=] { alter_space_delete(alter); });
	/*
	 * The order of checks is important, DropIndex most be added
	 * first, so that AddIndex::prepare() can change
	 * Drop + Add to a Modify.
	 */
	if (old_index != NULL) {
		DropIndex *drop_index = AlterSpaceOp::create<DropIndex>();
		alter_space_add_op(alter, drop_index);
		drop_index->old_index_def = old_index->index_def;
	}
	if (new_tuple != NULL) {
		AddIndex *add_index = AlterSpaceOp::create<AddIndex>();
		alter_space_add_op(alter, add_index);
		add_index->new_index_def = index_def_new_from_tuple(new_tuple,
								    old_space);
	}
	alter_space_do(txn, alter, old_space);
	scoped_guard.is_active = false;
}

/* {{{ space truncate */

struct truncate_space {
	/** Space being truncated. */
	struct space *old_space;
	/** Space created as a result of truncation. */
	struct space *new_space;
	/** Trigger executed to commit truncation. */
	struct trigger on_commit;
	/** Trigger executed to rollback truncation. */
	struct trigger on_rollback;
};

/**
 * Call the engine specific method to commit truncation
 * and delete the old space.
 */
static void
truncate_space_commit(struct trigger *trigger, void * /* event */)
{
	struct truncate_space *truncate =
		(struct truncate_space *) trigger->data;
	truncate->new_space->handler->commitTruncateSpace(truncate->old_space,
							  truncate->new_space);
	space_delete(truncate->old_space);
}

/**
 * Move the old space back to the cache and delete
 * the new space.
 */
static void
truncate_space_rollback(struct trigger *trigger, void * /* event */)
{
	struct truncate_space *truncate =
		(struct truncate_space *) trigger->data;
	if (space_cache_replace(truncate->old_space) != truncate->new_space)
		unreachable();
	space_delete(truncate->new_space);
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
	txn_check_autocommit(txn, "Space _truncate");
	struct tuple *new_tuple = stmt->new_tuple;

	if (new_tuple == NULL) {
		/* Space drop - nothing to do. */
		return;
	}

	uint32_t space_id =
		tuple_field_u32_xc(new_tuple, BOX_TRUNCATE_FIELD_SPACE_ID);
	uint64_t truncate_count =
		tuple_field_u64_xc(new_tuple, BOX_TRUNCATE_FIELD_COUNT);
	struct space *old_space = space_cache_find(space_id);

	if (stmt->row->type == IPROTO_INSERT) {
		/*
		 * Space creation during initial recovery -
		 * initialize truncate_count.
		 */
		old_space->truncate_count = truncate_count;
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
	 * Truncate counter is updated - truncate the space.
	 */
	struct truncate_space *truncate =
		region_calloc_object_xc(&fiber()->gc, struct truncate_space);

	/* Create an empty copy of the old space. */
	struct rlist key_list;
	space_dump_def(old_space, &key_list);
	struct space *new_space = space_new(&old_space->def, &key_list);
	new_space->truncate_count = truncate_count;
	auto guard = make_scoped_guard([=] { space_delete(new_space); });

	/* Notify the engine about upcoming space truncation. */
	new_space->handler->prepareTruncateSpace(old_space, new_space);

	guard.is_active = false;

	/*
	 * Replace the old space with the new one in the space
	 * cache. Requests processed after this point will see
	 * the space as truncated.
	 */
	if (space_cache_replace(new_space) != old_space)
		unreachable();

	/*
	 * Register the trigger that will commit or rollback
	 * truncation depending on whether WAL write succeeds
	 * or fails.
	 */
	truncate->old_space = old_space;
	truncate->new_space = new_space;

	trigger_create(&truncate->on_commit,
		       truncate_space_commit, truncate, NULL);
	txn_on_commit(txn, &truncate->on_commit);

	trigger_create(&truncate->on_rollback,
		       truncate_space_rollback, truncate, NULL);
	txn_on_rollback(txn, &truncate->on_rollback);
}

/* }}} */

/* {{{ access control */

/** True if the space has records identified by key 'uid'
 * Uses 'owner' index.
 */
bool
space_has_data(uint32_t id, uint32_t iid, uint32_t uid)
{
	struct space *space = space_by_id(id);
	if (space == NULL)
		return false;

	if (space_index(space, iid) == NULL)
		return false;

	MemtxIndex *index = index_find_system(space, iid);
	char key[6];
	assert(mp_sizeof_uint(BOX_SYSTEM_ID_MIN) <= sizeof(key));
	mp_encode_uint(key, uid);
	struct iterator *it = index->position();

	index->initIterator(it, ITER_EQ, key, 1);
	if (it->next(it))
		return true;
	return false;
}

bool
user_has_data(struct user *user)
{
	uint32_t uid = user->def.uid;
	uint32_t spaces[] = { BOX_SPACE_ID, BOX_FUNC_ID, BOX_PRIV_ID, BOX_PRIV_ID };
	/*
	 * owner index id #1 for _space and _func and _priv.
	 * For _priv also check that the user has no grants.
	 */
	uint32_t indexes[] = { 1, 1, 1, 0 };
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

void
user_def_create_from_tuple(struct user_def *user, struct tuple *tuple)
{
	/* In case user password is empty, fill it with \0 */
	memset(user, 0, sizeof(*user));
	user->uid = tuple_field_u32_xc(tuple, BOX_USER_FIELD_ID);
	user->owner = tuple_field_u32_xc(tuple, BOX_USER_FIELD_UID);
	const char *user_type =
		tuple_field_cstr_xc(tuple, BOX_USER_FIELD_TYPE);
	user->type= schema_object_type(user_type);
	const char *name = tuple_field_cstr_xc(tuple, BOX_USER_FIELD_NAME);
	uint32_t len = snprintf(user->name, sizeof(user->name), "%s", name);
	if (len >= sizeof(user->name)) {
		tnt_raise(ClientError, ER_CREATE_USER,
			  name, "user name is too long");
	}
	if (user->type != SC_ROLE && user->type != SC_USER) {
		tnt_raise(ClientError, ER_CREATE_USER,
			  user->name, "unknown user type");
	}
	identifier_check(name);
	access_check_ddl(user->owner, SC_USER);
	/*
	 * AUTH_DATA field in _user space should contain
	 * chap-sha1 -> base64_encode(sha1(sha1(password)).
	 * Check for trivial errors when a plain text
	 * password is saved in this field instead.
	 */
	if (tuple_field_count(tuple) > BOX_USER_FIELD_AUTH_MECH_LIST) {
		const char *auth_data =
			tuple_field(tuple, BOX_USER_FIELD_AUTH_MECH_LIST);
		if (strlen(auth_data)) {
			if (user->type == SC_ROLE)
				tnt_raise(ClientError, ER_CREATE_ROLE,
					  user->name, "authentication "
					  "data can not be set for a role");
		}
		user_def_fill_auth_data(user, auth_data);
	}
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
	struct user_def user;
	user_def_create_from_tuple(&user, stmt->new_tuple);
	user_cache_replace(&user);
}

/**
 * A trigger invoked on replace in the user table.
 */
static void
on_replace_dd_user(struct trigger * /* trigger */, void *event)
{
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	txn_check_autocommit(txn, "Space _user");
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	uint32_t uid = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					  BOX_USER_FIELD_ID);
	struct user *old_user = user_by_id(uid);
	if (new_tuple != NULL && old_user == NULL) { /* INSERT */
		struct user_def user;
		user_def_create_from_tuple(&user, new_tuple);
		(void) user_cache_replace(&user);
		struct trigger *on_rollback =
			txn_alter_trigger_new(user_cache_remove_user, NULL);
		txn_on_rollback(txn, on_rollback);
	} else if (new_tuple == NULL) { /* DELETE */
		access_check_ddl(old_user->def.owner, SC_USER);
		/* Can't drop guest or super user */
		if (uid <= (uint32_t) BOX_SYSTEM_USER_ID_MAX) {
			tnt_raise(ClientError, ER_DROP_USER,
				  old_user->def.name,
				  "the user or the role is a system");
		}
		/*
		 * Can only delete user if it has no spaces,
		 * no functions and no grants.
		 */
		if (user_has_data(old_user)) {
			tnt_raise(ClientError, ER_DROP_USER,
				  old_user->def.name, "the user has objects");
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
		struct user_def user;
		user_def_create_from_tuple(&user, new_tuple);
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
			  tt_cstr(name, len), "function name is too long");
	struct func_def *def = (struct func_def *) malloc(func_def_sizeof(len));
	if (def == NULL)
		tnt_raise(OutOfMemory, func_def_sizeof(len), "malloc", "def");
	auto def_guard = make_scoped_guard([=] { free(def); });
	func_def_get_ids_from_tuple(tuple, &def->fid, &def->uid);
	snprintf(def->name, len + 1, "%s", name);
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
	txn_check_autocommit(txn, "Space _func");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;

	uint32_t fid = tuple_field_u32_xc(old_tuple ? old_tuple : new_tuple,
					  BOX_FUNC_FIELD_ID);
	struct func *old_func = func_by_id(fid);
	if (new_tuple != NULL && old_func == NULL) { /* INSERT */
		struct func_def *def = func_def_new_from_tuple(new_tuple);
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
		access_check_ddl(uid, SC_FUNCTION);
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
		access_check_ddl(def->uid, SC_FUNCTION);
		struct trigger *on_commit =
			txn_alter_trigger_new(func_cache_replace_func, NULL);
		txn_on_commit(txn, on_commit);
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
	priv->object_id = tuple_field_u32_xc(tuple, BOX_PRIV_FIELD_OBJECT_ID);
	priv->object_type = schema_object_type(object_type);
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
priv_def_check(struct priv_def *priv)
{
	struct user *grantor = user_find_xc(priv->grantor_id);
	/* May be a role */
	struct user *grantee = user_by_id(priv->grantee_id);
	if (grantee == NULL) {
		tnt_raise(ClientError, ER_NO_SUCH_USER,
			  int2str(priv->grantee_id));
	}
	access_check_ddl(grantor->def.uid, priv->object_type);
	switch (priv->object_type) {
	case SC_UNIVERSE:
		if (grantor->def.uid != ADMIN) {
			tnt_raise(ClientError, ER_ACCESS_DENIED,
				  "Grant", schema_object_name(priv->object_type),
				  grantor->def.name);
		}
		break;
	case SC_SPACE:
	{
		struct space *space = space_cache_find(priv->object_id);
		if (space->def.uid != grantor->def.uid &&
		    grantor->def.uid != ADMIN) {
			tnt_raise(ClientError, ER_ACCESS_DENIED,
				  "Grant", schema_object_name(priv->object_type),
				  grantor->def.name);
		}
		break;
	}
	case SC_FUNCTION:
	{
		struct func *func = func_cache_find(priv->object_id);
		if (func->def->uid != grantor->def.uid &&
		    grantor->def.uid != ADMIN) {
			tnt_raise(ClientError, ER_ACCESS_DENIED,
				  "Grant", schema_object_name(priv->object_type),
				  grantor->def.name);
		}
		break;
	}
	case SC_ROLE:
	{
		struct user *role = user_by_id(priv->object_id);
		if (role == NULL || role->def.type != SC_ROLE) {
			tnt_raise(ClientError, ER_NO_SUCH_ROLE,
				  role ? role->def.name :
				  int2str(priv->object_id));
		}
		/*
		 * Only the creator of the role can grant or revoke it.
		 * Everyone can grant 'PUBLIC' role.
		 */
		if (role->def.owner != grantor->def.uid &&
		    grantor->def.uid != ADMIN &&
		    (role->def.uid != PUBLIC || priv->access < PRIV_X)) {
			tnt_raise(ClientError, ER_ACCESS_DENIED,
				  "Grant", role->def.name, grantor->def.name);
		}
		/* Not necessary to do during revoke, but who cares. */
		role_check(grantee, role);
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
	if (priv->object_type == SC_ROLE) {
		struct user *role = user_by_id(priv->object_id);
		if (role == NULL || role->def.type != SC_ROLE)
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
	txn_check_autocommit(txn, "Space _priv");
	struct txn_stmt *stmt = txn_current_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	struct priv_def priv;

	if (new_tuple != NULL && old_tuple == NULL) {	/* grant */
		priv_def_create_from_tuple(&priv, new_tuple);
		priv_def_check(&priv);
		grant_or_revoke(&priv);
		struct trigger *on_rollback =
			txn_alter_trigger_new(revoke_priv, NULL);
		txn_on_rollback(txn, on_rollback);
	} else if (new_tuple == NULL) {                /* revoke */
		assert(old_tuple);
		priv_def_create_from_tuple(&priv, old_tuple);
		access_check_ddl(priv.grantor_id, priv.object_type);
		struct trigger *on_commit =
			txn_alter_trigger_new(revoke_priv, NULL);
		txn_on_commit(txn, on_commit);
	} else {                                       /* modify */
		priv_def_create_from_tuple(&priv, new_tuple);
		priv_def_check(&priv);
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
	txn_check_autocommit(txn, "Space _schema");
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
	txn_check_autocommit(txn, "Space _cluster");
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

static void
unlock_after_dd(struct trigger *trigger, void *event)
{
	(void) trigger;
	(void) event;
	latch_unlock(&schema_lock);
}

static void
lock_before_dd(struct trigger *trigger, void *event)
{
	(void) trigger;
	if (fiber() == latch_owner(&schema_lock))
		return;
	struct txn *txn = (struct txn *)event;
	latch_lock(&schema_lock);
	struct trigger *on_commit =
		txn_alter_trigger_new(unlock_after_dd, NULL);
	txn_on_commit(txn, on_commit);
	struct trigger *on_rollback =
		txn_alter_trigger_new(unlock_after_dd, NULL);
	txn_on_rollback(txn, on_rollback);
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

struct trigger on_replace_priv = {
	RLIST_LINK_INITIALIZER, on_replace_dd_priv, NULL, NULL
};

struct trigger on_replace_cluster = {
	RLIST_LINK_INITIALIZER, on_replace_dd_cluster, NULL, NULL
};

struct trigger on_stmt_begin_space = {
	RLIST_LINK_INITIALIZER, lock_before_dd, NULL, NULL
};

struct trigger on_stmt_begin_index = {
	RLIST_LINK_INITIALIZER, lock_before_dd, NULL, NULL
};

/* vim: set foldmethod=marker */
