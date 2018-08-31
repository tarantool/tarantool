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

/**
 * Make checks from msgpack.
 * @param str pointer to array of maps
 *         e.g. [{"expr": "x < y", "name": "ONE"}, ..].
 * @param len array items count.
 * @param[out] opt pointer to store parsing result.
 * @param errcode Code of error to set if something is wrong.
 * @param field_no Field number of an option in a parent element.
 * @retval 0 on success.
 * @retval not 0 on error. Also set diag message.
 */
static int
checks_array_decode(const char **str, uint32_t len, char *opt, uint32_t errcode,
		    uint32_t field_no);

const struct space_opts space_opts_default = {
	/* .group_id = */ 0,
	/* .is_temporary = */ false,
	/* .view = */ false,
	/* .sql        = */ NULL,
	/* .checks     = */ NULL,
};

const struct opt_def space_opts_reg[] = {
	OPT_DEF("group_id", OPT_UINT32, struct space_opts, group_id),
	OPT_DEF("temporary", OPT_BOOL, struct space_opts, is_temporary),
	OPT_DEF("view", OPT_BOOL, struct space_opts, is_view),
	OPT_DEF("sql", OPT_STRPTR, struct space_opts, sql),
	OPT_DEF_ARRAY("checks", struct space_opts, checks,
		      checks_array_decode),
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

	*fields_offset = sizeof(struct space_def) + name_len + 1;
	*names_offset = *fields_offset + field_count * sizeof(struct field_def);
	*def_expr_offset = *names_offset + field_strs_size;
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
	if (opts->checks != NULL) {
		def->opts.checks = sql_expr_list_dup(sql_get(), opts->checks, 0);
		if (def->opts.checks == NULL) {
			free(def->opts.sql);
			diag_set(OutOfMemory, 0, "sql_expr_list_dup",
				 "def->opts.checks");
			return -1;
		}
		sql_checks_update_space_def_reference(def->opts.checks, def);
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

/** Free a default value's syntax trees of @a defs. */
void
space_def_destroy_fields(struct field_def *fields, uint32_t field_count)
{
	for (uint32_t i = 0; i < field_count; ++i) {
		if (fields[i].default_value_expr != NULL) {
			sql_expr_delete(sql_get(), fields[i].default_value_expr,
					true);
		}
	}
}

void
space_def_delete(struct space_def *def)
{
	space_opts_destroy(&def->opts);
	tuple_dictionary_unref(def->dict);
	space_def_destroy_fields(def->fields, def->field_count);
	TRASH(def);
	free(def);
}

void
space_opts_destroy(struct space_opts *opts)
{
	free(opts->sql);
	sql_expr_list_delete(sql_get(), opts->checks);
	TRASH(opts);
}

static int
checks_array_decode(const char **str, uint32_t len, char *opt, uint32_t errcode,
		    uint32_t field_no)
{
	char *errmsg = tt_static_buf();
	struct ExprList *checks = NULL;
	const char **map = str;
	struct sqlite3 *db = sql_get();
	for (uint32_t i = 0; i < len; i++) {
		checks = sql_expr_list_append(db, checks, NULL);
		if (checks == NULL) {
			diag_set(OutOfMemory, 0, "sql_expr_list_append",
				 "checks");
			goto error;
		}
		const char *expr_name = NULL;
		const char *expr_str = NULL;
		uint32_t expr_name_len = 0;
		uint32_t expr_str_len = 0;
		uint32_t map_size = mp_decode_map(map);
		for (uint32_t j = 0; j < map_size; j++) {
			if (mp_typeof(**map) != MP_STR) {
				diag_set(ClientError, errcode, field_no,
					 "key must be a string");
				goto error;
			}
			uint32_t key_len;
			const char *key = mp_decode_str(map, &key_len);
			if (mp_typeof(**map) != MP_STR) {
				snprintf(errmsg, TT_STATIC_BUF_LEN,
					 "invalid MsgPack map field '%.*s' type",
					 key_len, key);
				diag_set(ClientError, errcode, field_no, errmsg);
				goto error;
			}
			if (key_len == 4 && memcmp(key, "expr", key_len) == 0) {
				expr_str = mp_decode_str(map, &expr_str_len);
			} else if (key_len == 4 &&
				   memcmp(key, "name", key_len) == 0) {
				expr_name = mp_decode_str(map, &expr_name_len);
			} else {
				snprintf(errmsg, TT_STATIC_BUF_LEN,
					 "invalid MsgPack map field '%.*s'",
					 key_len, key);
				diag_set(ClientError, errcode, field_no, errmsg);
				goto error;
			}
		}
		if (sql_check_list_item_init(checks, i, expr_name, expr_name_len,
					     expr_str, expr_str_len) != 0) {
			box_error_t *err = box_error_last();
			if (box_error_code(err) != ENOMEM) {
				snprintf(errmsg, TT_STATIC_BUF_LEN,
					 "invalid expression specified (%s)",
					 box_error_message(err));
				diag_set(ClientError, errcode, field_no,
					 errmsg);
			}
			goto error;
		}
	}
	*(struct ExprList **)opt = checks;
	return 0;
error:
	sql_expr_list_delete(db, checks);
	return  -1;
}
