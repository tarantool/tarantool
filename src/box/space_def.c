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
#include "tt_static.h"
#include "tuple_constraint_def.h"
#include "tuple_format.h"

const struct space_opts space_opts_default = {
	/* .group_id = */ 0,
	/* .is_temporary = */ false,
	/* .is_ephemeral = */ false,
	/* .view = */ false,
	/* .is_sync = */ false,
	/* .defer_deletes = */ false,
	/* .sql        = */ NULL,
	/* .constraint_def = */ NULL,
	/* .constraint_count = */ 0,
};

/**
 * Callback to parse a value with 'constraint' key in msgpack space opts
 * definition. See function definition below.
 */
static int
space_opts_parse_constraint(const char **data, void *vopts,
			    struct region *region,
			    uint32_t errcode, uint32_t field_no);

/**
 * Callback to parse a value with 'foreign_key' key in msgpack space opts
 * definition. See function definition below.
 */
static int
space_opts_parse_foreign_key(const char **data, void *vopts,
			     struct region *region,
			     uint32_t errcode, uint32_t field_no);

const struct opt_def space_opts_reg[] = {
	OPT_DEF("group_id", OPT_UINT32, struct space_opts, group_id),
	OPT_DEF("temporary", OPT_BOOL, struct space_opts, is_temporary),
	OPT_DEF("view", OPT_BOOL, struct space_opts, is_view),
	OPT_DEF("is_sync", OPT_BOOL, struct space_opts, is_sync),
	OPT_DEF("defer_deletes", OPT_BOOL, struct space_opts, defer_deletes),
	OPT_DEF("sql", OPT_STRPTR, struct space_opts, sql),
	OPT_DEF_CUSTOM("constraint", space_opts_parse_constraint),
	OPT_DEF_CUSTOM("foreign_key", space_opts_parse_foreign_key),
	OPT_DEF_LEGACY("checks"),
	OPT_END,
};

size_t
space_def_sizeof(uint32_t name_len, const struct field_def *fields,
		 uint32_t field_count, uint32_t *names_offset,
		 uint32_t *fields_offset)
{
	uint32_t field_strs_size = 0;
	for (uint32_t i = 0; i < field_count; ++i) {
		field_strs_size += strlen(fields[i].name) + 1;
		if (fields[i].default_value != NULL) {
			int len = strlen(fields[i].default_value);
			field_strs_size += len + 1;
		}
	}
	*fields_offset = small_align(sizeof(struct space_def) + name_len + 1,
				     alignof(typeof(fields[0])));
	*names_offset = *fields_offset + field_count * sizeof(struct field_def);
	return *names_offset + field_strs_size;
}

struct tuple_format *
space_tuple_format_new(struct tuple_format_vtab *vtab, void *engine,
		       struct key_def *const *keys, uint16_t key_count,
		       const struct space_def *def)
{
	return tuple_format_new(vtab, engine, keys, key_count,
				def->fields, def->field_count,
				def->exact_field_count, def->dict,
				def->opts.is_temporary, def->opts.is_ephemeral,
				def->opts.constraint_def,
				def->opts.constraint_count);
}

/**
 * Initialize def->opts with opts duplicate.
 * @param def  Def to initialize.
 * @param opts Opts to duplicate.
 * @retval 0 on success.
 * @retval not 0 on error.
 */
static int
space_def_dup_opts(struct space_def *def, const struct space_opts *opts)
{
	def->opts = *opts;
	if (opts->sql != NULL) {
		def->opts.sql = strdup(opts->sql);
		if (def->opts.sql == NULL) {
			diag_set(OutOfMemory, strlen(opts->sql) + 1, "strdup",
				 "def->opts.sql");
			return -1;
		}
	}
	def->opts.constraint_count = opts->constraint_count;
	def->opts.constraint_def =
		tuple_constraint_def_array_dup(opts->constraint_def,
					       opts->constraint_count);
	return 0;
}

struct space_def *
space_def_dup(const struct space_def *src)
{
	uint32_t strs_offset, fields_offset;
	size_t size = space_def_sizeof(strlen(src->name), src->fields,
				       src->field_count, &strs_offset,
				       &fields_offset);
	struct space_def *ret = (struct space_def *) malloc(size);
	if (ret == NULL) {
		diag_set(OutOfMemory, size, "malloc", "ret");
		return NULL;
	}
	memcpy(ret, src, size);
	memset(&ret->opts, 0, sizeof(ret->opts));
	char *strs_pos = (char *)ret + strs_offset;
	if (src->field_count > 0) {
		ret->fields = (struct field_def *)((char *)ret + fields_offset);
		for (uint32_t i = 0; i < src->field_count; ++i) {
			ret->fields[i].name = strs_pos;
			strs_pos += strlen(strs_pos) + 1;
			if (src->fields[i].default_value != NULL) {
				ret->fields[i].default_value = strs_pos;
				strs_pos += strlen(strs_pos) + 1;
			}
			ret->fields[i].constraint_count =
				src->fields[i].constraint_count;
			ret->fields[i].constraint_def =
				tuple_constraint_def_array_dup(
					src->fields[i].constraint_def,
					src->fields[i].constraint_count);
		}
	}
	tuple_dictionary_ref(ret->dict);
	if (space_def_dup_opts(ret, &src->opts) != 0) {
		space_def_delete(ret);
		return NULL;
	}
	return ret;
}

struct space_def *
space_def_new(uint32_t id, uint32_t uid, uint32_t exact_field_count,
	      const char *name, uint32_t name_len,
	      const char *engine_name, uint32_t engine_len,
	      const struct space_opts *opts, const struct field_def *fields,
	      uint32_t field_count)
{
	uint32_t strs_offset, fields_offset;
	size_t size = space_def_sizeof(name_len, fields, field_count,
				       &strs_offset, &fields_offset);
	struct space_def *def = (struct space_def *) malloc(size);
	if (def == NULL) {
		diag_set(OutOfMemory, size, "malloc", "def");
		return NULL;
	}
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
	if (field_count == 0) {
		def->fields = NULL;
	} else {
		char *strs_pos = (char *)def + strs_offset;
		def->fields = (struct field_def *)((char *)def + fields_offset);
		for (uint32_t i = 0; i < field_count; ++i) {
			def->fields[i] = fields[i];
			def->fields[i].name = strs_pos;
			uint32_t len = strlen(fields[i].name);
			memcpy(def->fields[i].name, fields[i].name, len);
			def->fields[i].name[len] = 0;
			strs_pos += len + 1;

			if (fields[i].default_value != NULL) {
				def->fields[i].default_value = strs_pos;
				len = strlen(fields[i].default_value);
				memcpy(def->fields[i].default_value,
				       fields[i].default_value, len);
				def->fields[i].default_value[len] = 0;
				strs_pos += len + 1;
			}
			def->fields[i].constraint_def =
				tuple_constraint_def_array_dup(
					fields[i].constraint_def,
					fields[i].constraint_count);
		}
	}
	if (space_def_dup_opts(def, opts) != 0) {
		space_def_delete(def);
		return NULL;
	}
	return def;
}

struct space_def*
space_def_new_ephemeral(uint32_t exact_field_count, struct field_def *fields)
{
	struct space_opts opts = space_opts_default;
	opts.is_temporary = true;
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
						    &opts, fields, field_count);
	return space_def;
}

void
space_def_delete(struct space_def *def)
{
	for (uint32_t i = 0; i < def->field_count; ++i) {
		struct field_def *field = &def->fields[i];
		free(field->constraint_def);
		TRASH(field);
	}
	space_opts_destroy(&def->opts);
	tuple_dictionary_unref(def->dict);
	TRASH(def);
	free(def);
}

void
space_opts_destroy(struct space_opts *opts)
{
	free(opts->sql);
	free(opts->constraint_def);
	TRASH(opts);
}

/**
 * Parse constraint array from msgpack.
 * Used as callback to parse a value with 'constraint' key in space options.
 * Move @a data msgpack pointer to the end of msgpack value.
 * By convention @a opts must point to corresponding struct space_opts.
 * Allocate a temporary constraint array on @a region and set pointer to it
 *  as field_def->constraint, also setting field_def->constraint_count.
 * Return 0 on success or -1 on error (diag is set to @a errcode with
 *  reference to field by @a field_no).
 */
int
space_opts_parse_constraint(const char **data, void *vopts,
			    struct region *region,
			    uint32_t errcode, uint32_t field_no)
{
	/* Expected normal form of constraints: {name1=func1, name2=func2..}. */
	struct space_opts *opts = (struct space_opts *)vopts;
	return tuple_constraint_def_decode(data, &opts->constraint_def,
					   &opts->constraint_count, region,
					   errcode, field_no);
}

/**
 * Parse foreign key array from msgpack.
 * Used as callback to parse a value with 'foreign_key' key in space options.
 * Move @a data msgpack pointer to the end of msgpack value.
 * By convention @a opts must point to corresponding struct space_opts.
 * Allocate a temporary constraint array on @a region and set pointer to it
 *  as field_def->constraint, also setting field_def->constraint_count.
 * Return 0 on success or -1 on error (diag is set to @a errcode with
 *  reference to field by @a field_no).
 */
int
space_opts_parse_foreign_key(const char **data, void *vopts,
			     struct region *region,
			     uint32_t errcode, uint32_t field_no)
{
	/* Expected normal form of constraints: {name1={space=.., field=..}.. */
	struct space_opts *opts = (struct space_opts *)vopts;
	return tuple_constraint_def_decode_fkey(data, &opts->constraint_def,
						&opts->constraint_count,
						region, errcode, field_no,
						true);
}
