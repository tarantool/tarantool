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

const struct space_opts space_opts_default = {
	/* .temporary = */ false,
	/* .view = */ false,
	/* .sql        = */ NULL,
};

const struct opt_def space_opts_reg[] = {
	OPT_DEF("temporary", OPT_BOOL, struct space_opts, temporary),
	OPT_DEF("view", OPT_BOOL, struct space_opts, is_view),
	OPT_DEF("sql", OPT_STRPTR, struct space_opts, sql),
	OPT_END,
};

/**
 * Size of the space_def.
 * @param name_len Length of the space name.
 * @param fields Fields array of space format.
 * @param field_count Space field count.
 * @param[out] names_offset Offset from the beginning of a def to
 *             a field names memory.
 * @param[out] fields_offset Offset from the beginning of a def to
 *             a fields array.
 * @param[out] def_expr_offset Offset from the beginning of a def
 *             to a def_value_expr array.
 * @retval Size in bytes.
 */
static inline size_t
space_def_sizeof(uint32_t name_len, const struct field_def *fields,
		 uint32_t field_count, uint32_t *names_offset,
		 uint32_t *fields_offset, uint32_t *def_expr_offset)
{
	uint32_t field_strs_size = 0;
	uint32_t def_exprs_size = 0;
	for (uint32_t i = 0; i < field_count; ++i) {
		field_strs_size += strlen(fields[i].name) + 1;
		if (fields[i].default_value != NULL) {
			assert(fields[i].default_value_expr != NULL);
			int len = strlen(fields[i].default_value);
			field_strs_size += len + 1;
			struct Expr *e = fields[i].default_value_expr;
			def_exprs_size += sql_expr_sizeof(e, 0);
		}
	}

	*fields_offset = sizeof(struct space_def) + name_len + 1;
	*names_offset = *fields_offset + field_count * sizeof(struct field_def);
	*def_expr_offset = *names_offset + field_strs_size;
	return *def_expr_offset + def_exprs_size;
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
	if (src->opts.sql != NULL) {
		ret->opts.sql = strdup(src->opts.sql);
		if (ret->opts.sql == NULL) {
			diag_set(OutOfMemory, strlen(src->opts.sql) + 1,
				 "strdup", "ret->opts.sql");
			free(ret);
			return NULL;
		}
	}
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

				struct Expr *e =
					src->fields[i].default_value_expr;
				assert(e != NULL);
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
	def->opts = *opts;
	if (opts->sql != NULL) {
		def->opts.sql = strdup(opts->sql);
		if (def->opts.sql == NULL) {
			diag_set(OutOfMemory, strlen(opts->sql) + 1, "strdup",
				 "def->opts.sql");
			free(def);
			return NULL;
		}
	}
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

				struct Expr *e =
					fields[i].default_value_expr;
				assert(e != NULL);
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
	return def;
}

int
space_def_check_compatibility(const struct space_def *old_def,
			      const struct space_def *new_def,
			      bool is_space_empty)
{
	if (strcmp(new_def->engine_name, old_def->engine_name) != 0) {
		diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
			 "can not change space engine");
		return -1;
	}
	if (new_def->id != old_def->id) {
		diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
			 "space id is immutable");
		return -1;
	}
	if (new_def->opts.is_view != old_def->opts.is_view) {
		diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
			 "can not convert a space to a view and vice versa");
		return -1;
	}
	if (is_space_empty)
		return 0;

	if (new_def->exact_field_count != 0 &&
	    new_def->exact_field_count != old_def->exact_field_count) {
		diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
			 "can not change field count on a non-empty space");
		return -1;
	}
	if (new_def->opts.temporary != old_def->opts.temporary) {
		diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
			 "can not switch temporary flag on a non-empty space");
		return -1;
	}
	uint32_t field_count = MIN(new_def->field_count, old_def->field_count);
	for (uint32_t i = 0; i < field_count; ++i) {
		enum field_type old_type = old_def->fields[i].type;
		enum field_type new_type = new_def->fields[i].type;
		if (!field_type1_contains_type2(new_type, old_type) &&
		    !field_type1_contains_type2(old_type, new_type)) {
			const char *msg =
				tt_sprintf("Can not change a field type from "\
					   "%s to %s on a not empty space",
					   field_type_strs[old_type],
					   field_type_strs[new_type]);
			diag_set(ClientError, ER_ALTER_SPACE, old_def->name,
				 msg);
			return -1;
		}
	}
	return 0;
}

/** Free a default value's syntax trees of @a defs. */
void
space_def_destroy_fields(struct field_def *fields, uint32_t field_count)
{
	for (uint32_t i = 0; i < field_count; ++i) {
		if (fields[i].default_value_expr != NULL) {
			sql_expr_free(sql_get(), fields[i].default_value_expr,
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
