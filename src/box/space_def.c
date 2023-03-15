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

#include "space_def.h"
#include "diag.h"
#include "error.h"
#include "msgpuck.h"
#include "space_upgrade.h"
#include "tt_static.h"
#include "tuple_constraint_def.h"
#include "tuple_format.h"

const struct space_opts space_opts_default = {
	/* .group_id = */ 0,
	/* .type = */ SPACE_TYPE_NORMAL,
	/* .is_ephemeral = */ false,
	/* .view = */ false,
	/* .is_sync = */ false,
	/* .defer_deletes = */ false,
	/* .sql        = */ NULL,
	/* .constraint_def = */ NULL,
	/* .constraint_count = */ 0,
	/* .upgrade_def = */ NULL,
};

/**
 * Callback to parse a value with 'constraint' key in msgpack space opts
 * definition. See function definition below.
 */
static int
space_opts_parse_constraint(const char **data, void *vopts,
			    struct region *region);

/**
 * Callback to parse a value with 'foreign_key' key in msgpack space opts
 * definition. See function definition below.
 */
static int
space_opts_parse_foreign_key(const char **data, void *vopts,
			     struct region *region);

/**
 * Callback to parse a value with 'upgrade' key in msgpack space opts
 * definition. See function definition below.
 */
static int
space_opts_parse_upgrade(const char **data, void *vopts,
			 struct region *region);

/**
 * Callback to parse a value with 'temporary' key in msgpack space opts
 * definition. See function definition below.
 */
static int
space_opts_parse_temporary(const char **data, void *vopts,
			   struct region *region);

/**
 * Callback to parse a value with 'type' key in msgpack space opts
 * definition. See function definition below.
 */
static int
space_opts_parse_type(const char **data, void *vopts, struct region *region);

const struct opt_def space_opts_reg[] = {
	OPT_DEF_CUSTOM("type", space_opts_parse_type),
	OPT_DEF("group_id", OPT_UINT32, struct space_opts, group_id),
	OPT_DEF_CUSTOM("temporary", space_opts_parse_temporary),
	OPT_DEF("view", OPT_BOOL, struct space_opts, is_view),
	OPT_DEF("is_sync", OPT_BOOL, struct space_opts, is_sync),
	OPT_DEF("defer_deletes", OPT_BOOL, struct space_opts, defer_deletes),
	OPT_DEF("sql", OPT_STRPTR, struct space_opts, sql),
	OPT_DEF_CUSTOM("constraint", space_opts_parse_constraint),
	OPT_DEF_CUSTOM("foreign_key", space_opts_parse_foreign_key),
	OPT_DEF_CUSTOM("upgrade", space_opts_parse_upgrade),
	OPT_DEF_LEGACY("checks"),
	OPT_END,
};

struct tuple_format *
space_tuple_format_new(struct tuple_format_vtab *vtab, void *engine,
		       struct key_def *const *keys, uint16_t key_count,
		       const struct space_def *def)
{
	return tuple_format_new(vtab, engine, keys, key_count,
				def->fields, def->field_count,
				def->exact_field_count, def->dict,
				space_opts_is_temporary(&def->opts),
				def->opts.is_ephemeral,
				def->opts.constraint_def,
				def->opts.constraint_count, def->format_data,
				def->format_data_len);
}

/**
 * Initialize def->opts with opts duplicate.
 * @param def  Def to initialize.
 * @param opts Opts to duplicate.
 */
static void
space_def_dup_opts(struct space_def *def, const struct space_opts *opts)
{
	def->opts = *opts;
	if (opts->sql != NULL)
		def->opts.sql = xstrdup(opts->sql);
	def->opts.constraint_count = opts->constraint_count;
	def->opts.constraint_def =
		tuple_constraint_def_array_dup(opts->constraint_def,
					       opts->constraint_count);
	def->opts.upgrade_def = space_upgrade_def_dup(opts->upgrade_def);
}

struct space_def *
space_def_dup(const struct space_def *src)
{
	size_t size = sizeof(struct space_def) + strlen(src->name) + 1;
	struct space_def *ret = xmalloc(size);
	memcpy(ret, src, size);
	memset(&ret->opts, 0, sizeof(ret->opts));
	ret->fields = field_def_array_dup(src->fields, src->field_count);
	tuple_dictionary_ref(ret->dict);
	space_def_dup_opts(ret, &src->opts);
	if (src->format_data != NULL) {
		ret->format_data = xmalloc(src->format_data_len);
		memcpy(ret->format_data, src->format_data,
		       src->format_data_len);
		ret->format_data_len = src->format_data_len;
	} else {
		ret->format_data = NULL;
		ret->format_data_len = 0;
	}
	return ret;
}

struct space_def *
space_def_new(uint32_t id, uint32_t uid, uint32_t exact_field_count,
	      const char *name, uint32_t name_len,
	      const char *engine_name, uint32_t engine_len,
	      const struct space_opts *opts, const struct field_def *fields,
	      uint32_t field_count, const char *format_data,
	      size_t format_data_len)
{
	size_t size = sizeof(struct space_def) + name_len + 1;
	struct space_def *def = xmalloc(size);
	assert(name_len <= BOX_NAME_MAX);
	assert(engine_len <= ENGINE_NAME_MAX);
	def->dict = tuple_dictionary_new(fields, field_count);
	if (def->dict == NULL) {
		free(def);
		return NULL;
	}
	def->id = id;
	def->uid = uid;
	def->exact_field_count = exact_field_count;
	memcpy(def->name, name, name_len);
	def->name[name_len] = 0;
	memcpy(def->engine_name, engine_name, engine_len);
	def->engine_name[engine_len] = 0;

	def->view_ref_count = 0;
	def->field_count = field_count;
	def->fields = field_def_array_dup(fields, field_count);
	space_def_dup_opts(def, opts);
	if (format_data != NULL) {
		def->format_data = xmalloc(format_data_len);
		memcpy(def->format_data, format_data, format_data_len);
		def->format_data_len = format_data_len;
	} else {
		def->format_data = NULL;
		def->format_data_len = 0;
	}
	return def;
}

struct space_def*
space_def_new_ephemeral(uint32_t exact_field_count, struct field_def *fields)
{
	struct space_opts opts = space_opts_default;
	opts.type = SPACE_TYPE_DATA_TEMPORARY;
	opts.is_ephemeral = true;
	uint32_t field_count = exact_field_count;
	if (fields == NULL) {
		fields = (struct field_def *)&field_def_default;
		field_count = 0;
	}
	struct space_def *space_def = space_def_new(0, 0, exact_field_count,
						    "ephemeral",
						    strlen("ephemeral"),
						    "memtx", strlen("memtx"),
						    &opts, fields, field_count,
						    NULL, 0);
	return space_def;
}

void
space_def_delete(struct space_def *def)
{
	field_def_array_delete(def->fields, def->field_count);
	tuple_dictionary_unref(def->dict);
	free(def->opts.sql);
	free(def->opts.constraint_def);
	space_upgrade_def_delete(def->opts.upgrade_def);
	free(def->format_data);
	TRASH(def);
	free(def);
}

/**
 * Parse constraint array from msgpack.
 * Used as callback to parse a value with 'constraint' key in space options.
 * Move @a data msgpack pointer to the end of msgpack value.
 * By convention @a opts must point to corresponding struct space_opts.
 * Allocate a temporary constraint array on @a region and set pointer to it
 *  as field_def->constraint, also setting field_def->constraint_count.
 * Return 0 on success or -1 on error (diag is set to IllegalParams).
 */
int
space_opts_parse_constraint(const char **data, void *vopts,
			    struct region *region)
{
	/* Expected normal form of constraints: {name1=func1, name2=func2..}. */
	struct space_opts *opts = (struct space_opts *)vopts;
	return tuple_constraint_def_decode(data, &opts->constraint_def,
					   &opts->constraint_count, region);
}

/**
 * Parse foreign key array from msgpack.
 * Used as callback to parse a value with 'foreign_key' key in space options.
 * Move @a data msgpack pointer to the end of msgpack value.
 * By convention @a opts must point to corresponding struct space_opts.
 * Allocate a temporary constraint array on @a region and set pointer to it
 *  as field_def->constraint, also setting field_def->constraint_count.
 * Return 0 on success or -1 on error (diag is set to IllegalParams).
 */
int
space_opts_parse_foreign_key(const char **data, void *vopts,
			     struct region *region)
{
	/* Expected normal form of constraints: {name1={space=.., field=..}.. */
	struct space_opts *opts = (struct space_opts *)vopts;
	return tuple_constraint_def_decode_fkey(data, &opts->constraint_def,
						&opts->constraint_count,
						region, true);
}

static int
space_opts_parse_upgrade(const char **data, void *vopts,
			 struct region *region)
{
	struct space_opts *opts = (struct space_opts *)vopts;
	opts->upgrade_def = space_upgrade_def_decode(data, region);
	return opts->upgrade_def == NULL ? -1 : 0;
}

static int
space_opts_parse_temporary(const char **data, void *vopts,
			   struct region *region)
{
	(void)region;
	if (mp_typeof(**data) != MP_BOOL) {
		diag_set(IllegalParams, "'temporary' must be boolean");
		return -1;
	}
	struct space_opts *opts = vopts;
	if (opts->type != SPACE_TYPE_DEFAULT) {
		/* This means 'type' was specified. */
		diag_set(IllegalParams,
			 "only one of 'type' or 'temporary' may be specified");
		return -1;
	}
	bool is_temporary = mp_decode_bool(data);
	opts->type = is_temporary ?
		SPACE_TYPE_DATA_TEMPORARY : SPACE_TYPE_NORMAL;
	return 0;
}

const char *space_type_strs[] = {
	/* [SPACE_TYPE_NORMAL]         = */ "normal",
	/* [SPACE_TYPE_DATA_TEMPORARY] = */ "data-temporary",
};

static int
space_opts_parse_type(const char **data, void *vopts, struct region *region)
{
	(void)region;
	if (mp_typeof(**data) != MP_STR) {
		diag_set(IllegalParams, "'type' must be a string");
		return -1;
	}
	uint32_t str_len = 0;
	const char *str = mp_decode_str(data, &str_len);
	enum space_type space_type = STRN2ENUM(space_type, str, str_len);
	if (space_type == SPACE_TYPE_DEFAULT) {
		diag_set(IllegalParams, "unknown space type");
		return -1;
	}
	struct space_opts *opts = vopts;
	if (opts->type != SPACE_TYPE_DEFAULT) {
		/* This means 'temporary' was specified. */
		diag_set(IllegalParams,
			 "only one of 'type' or 'temporary' may be specified");
		return -1;
	}
	opts->type = space_type;
	return 0;
}
