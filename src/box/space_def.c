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
#include "sql.h"
#include "msgpuck.h"
#include "tt_static.h"

const struct space_opts space_opts_default = {
	/* .group_id = */ 0,
	/* .is_temporary = */ false,
	/* .is_ephemeral = */ false,
	/* .view = */ false,
	/* .is_sync = */ false,
	/* .sql        = */ NULL,
};

const struct opt_def space_opts_reg[] = {
	OPT_DEF("group_id", OPT_UINT32, struct space_opts, group_id),
	OPT_DEF("temporary", OPT_BOOL, struct space_opts, is_temporary),
	OPT_DEF("view", OPT_BOOL, struct space_opts, is_view),
	OPT_DEF("is_sync", OPT_BOOL, struct space_opts, is_sync),
	OPT_DEF("sql", OPT_STRPTR, struct space_opts, sql),
	OPT_DEF_LEGACY("checks"),
	OPT_END,
};

size_t
space_def_sizeof(uint32_t name_len, const struct field_def *fields,
		 uint32_t field_count, uint32_t *names_offset,
		 uint32_t *fields_offset, uint32_t *def_expr_offset)
{
	uint32_t field_strs_size = 0;
	uint32_t def_exprs_size = 0;
	for (uint32_t i = 0; i < field_count; ++i) {
		field_strs_size += strlen(fields[i].name) + 1;
		if (fields[i].default_value != NULL) {
			int len = strlen(fields[i].default_value);
			field_strs_size += len + 1;
			if (fields[i].default_value_expr != NULL) {
				struct Expr *e = fields[i].default_value_expr;
				def_exprs_size += sql_expr_sizeof(e, 0);
			}
		}
	}
	*fields_offset = small_align(sizeof(struct space_def) + name_len + 1,
				     alignof(typeof(fields[0])));
	*names_offset = *fields_offset + field_count * sizeof(struct field_def);
	*def_expr_offset = small_align(*names_offset + field_strs_size,
				       alignof(uint64_t));
	return *def_expr_offset + def_exprs_size;
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
	return 0;
}

struct space_def *
space_def_dup(const struct space_def *src)
{
	uint32_t strs_offset, fields_offset, def_expr_offset;
	size_t size = space_def_sizeof(strlen(src->name), src->fields,
				       src->field_count, &strs_offset,
				       &fields_offset, &def_expr_offset);
	struct space_def *ret = (struct space_def *) malloc(size);
	if (ret == NULL) {
		diag_set(OutOfMemory, size, "malloc", "ret");
		return NULL;
	}
	memcpy(ret, src, size);
	memset(&ret->opts, 0, sizeof(ret->opts));
	char *strs_pos = (char *)ret + strs_offset;
	char *expr_pos = (char *)ret + def_expr_offset;
	if (src->field_count > 0) {
		ret->fields = (struct field_def *)((char *)ret + fields_offset);
		for (uint32_t i = 0; i < src->field_count; ++i) {
			ret->fields[i].name = strs_pos;
			strs_pos += strlen(strs_pos) + 1;
			if (src->fields[i].default_value != NULL) {
				ret->fields[i].default_value = strs_pos;
				strs_pos += strlen(strs_pos) + 1;
			}
			struct Expr *e = src->fields[i].default_value_expr;
			if (e != NULL) {
				char *expr_pos_old = expr_pos;
				(void) expr_pos_old;
				e = sql_expr_dup(sql_get(), e, 0, &expr_pos);
				assert(e != NULL);
				/* Note: due to SQL legacy
				 * duplicactor pointer is not
				 * promoted for REDUCED exprs.
				 * Will be fixed w/ Expt
				 * allocation refactoring.
				 */
				assert(expr_pos_old == expr_pos);
				expr_pos += sql_expr_sizeof(e, 0);
				ret->fields[i].default_value_expr = e;
			}
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
	uint32_t strs_offset, fields_offset, def_expr_offset;
	size_t size = space_def_sizeof(name_len, fields, field_count,
				       &strs_offset, &fields_offset,
				       &def_expr_offset);
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
		char *expr_pos = (char *)def + def_expr_offset;
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
			struct Expr *e = def->fields[i].default_value_expr;
			if (e != NULL) {
				char *expr_pos_old = expr_pos;
				(void) expr_pos_old;
				e = sql_expr_dup(sql_get(), e, 0, &expr_pos);
				assert(e != NULL);
				/* Note: due to SQL legacy
				 * duplicactor pointer is
				 * not promoted for
				 * REDUCED exprs. Will be
				 * fixed w/ Expt
				 * allocation refactoring.
				 */
				assert(expr_pos_old == expr_pos);
				expr_pos += sql_expr_sizeof(e, 0);
				def->fields[i].default_value_expr = e;
			}
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

/** Free a default value's syntax trees of @a defs. */
void
space_def_destroy_fields(struct field_def *fields, uint32_t field_count,
			 bool extern_alloc)
{
	for (uint32_t i = 0; i < field_count; ++i) {
		if (fields[i].default_value_expr != NULL) {
			sql_expr_delete(sql_get(), fields[i].default_value_expr,
					extern_alloc);
		}
	}
}

void
space_def_delete(struct space_def *def)
{
	space_opts_destroy(&def->opts);
	tuple_dictionary_unref(def->dict);
	space_def_destroy_fields(def->fields, def->field_count, true);
	TRASH(def);
	free(def);
}

void
space_opts_destroy(struct space_opts *opts)
{
	free(opts->sql);
	TRASH(opts);
}
