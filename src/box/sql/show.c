/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <ctype.h>

#include "sqlInt.h"
#include "mem.h"
#include "box/schema.h"
#include "box/sequence.h"
#include "box/coll_id_cache.h"
#include "box/tuple_constraint_def.h"

/** An objected used to accumulate a statement. */
struct sql_desc {
	/** Accumulate the string representation of the statement. */
	struct StrAccum acc;
	/** MEM where the array of compiled statements should be inserted. */
	struct Mem *ret;
	/** MEM where the array of compiled errors should be inserted. */
	struct Mem *err;
	/** Array of compiled but not encoded statements. */
	char **statements;
	/** Array of compiled but not encoded errors. */
	char **errors;
	/** Number of compiled statements. */
	uint32_t statement_count;
	/** Number of compiled errors. */
	uint32_t error_count;
};

/** Initialize the object used to accumulate a statement. */
static void
sql_desc_initialize(struct sql_desc *desc, struct Mem *ret, struct Mem *err)
{
	sqlStrAccumInit(&desc->acc, NULL, 0, SQL_MAX_LENGTH);
	desc->statements = NULL;
	desc->statement_count = 0;
	desc->errors = NULL;
	desc->error_count = 0;
	desc->ret = ret;
	desc->err = err;
}

/** Append a new string to the object used to accumulate a statement. */
CFORMAT(printf, 2, 3) static void
sql_desc_append(struct sql_desc *desc, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	sqlVXPrintf(&desc->acc, fmt, ap);
	va_end(ap);
}

/** Append a name to the object used to accumulate a statement. */
static void
sql_desc_append_name(struct sql_desc *desc, const char *name)
{
	char *escaped = sql_escaped_name_new(name);
	assert(escaped[0] == '"' && escaped[strlen(escaped) - 1] == '"');
	char *normalized = sql_normalized_name_new(name, strlen(name));
	if (isalpha(name[0]) && strlen(escaped) == strlen(name) + 2 &&
	    strcmp(normalized, name) == 0)
		sqlXPrintf(&desc->acc, "%s", normalized);
	else
		sqlXPrintf(&desc->acc, "%s", escaped);
	sql_xfree(normalized);
	sql_xfree(escaped);
}

/** Append a new error to the object used to accumulate a statement. */
static void
sql_desc_error(struct sql_desc *desc, const char *type, const char *name,
	       const char *error)
{
	char *str = sqlMPrintf("Problem with %s '%s': %s.", type, name, error);
	uint32_t id = desc->error_count;
	++desc->error_count;
	uint32_t size = desc->error_count * sizeof(desc->errors);
	desc->errors = sql_xrealloc(desc->errors, size);
	desc->errors[id] = str;
}

/** Complete the statement and add it to the array of compiled statements. */
static void
sql_desc_finish_statement(struct sql_desc *desc)
{
	char *str = sqlStrAccumFinish(&desc->acc);
	sqlStrAccumInit(&desc->acc, NULL, 0, SQL_MAX_LENGTH);
	uint32_t id = desc->statement_count;
	++desc->statement_count;
	uint32_t size = desc->statement_count * sizeof(desc->statements);
	desc->statements = sql_xrealloc(desc->statements, size);
	desc->statements[id] = str;
}

/** Finalize a described statement. */
static void
sql_desc_finalize(struct sql_desc *desc)
{
	sqlStrAccumReset(&desc->acc);
	if (desc->error_count > 0) {
		uint32_t size = mp_sizeof_array(desc->error_count);
		for (uint32_t i = 0; i < desc->error_count; ++i)
			size += mp_sizeof_str(strlen(desc->errors[i]));
		char *buf = sql_xmalloc(size);
		char *end = mp_encode_array(buf, desc->error_count);
		for (uint32_t i = 0; i < desc->error_count; ++i) {
			end = mp_encode_str0(end, desc->errors[i]);
			sql_xfree(desc->errors[i]);
		}
		sql_xfree(desc->errors);
		assert(end - buf == size);
		mem_set_array_allocated(desc->err, buf, size);
	} else {
		mem_set_null(desc->err);
	}

	uint32_t size = mp_sizeof_array(desc->statement_count);
	for (uint32_t i = 0; i < desc->statement_count; ++i)
		size += mp_sizeof_str(strlen(desc->statements[i]));
	char *buf = sql_xmalloc(size);
	char *end = mp_encode_array(buf, desc->statement_count);
	for (uint32_t i = 0; i < desc->statement_count; ++i) {
		end = mp_encode_str0(end, desc->statements[i]);
		sql_xfree(desc->statements[i]);
	}
	sql_xfree(desc->statements);
	assert(end - buf == size);
	mem_set_array_allocated(desc->ret, buf, size);
}

/** Add a field foreign key constraint to the statement description. */
static void
sql_describe_field_foreign_key(struct sql_desc *desc,
			       const struct tuple_constraint_def *cdef)
{
	assert(cdef->type == CONSTR_FKEY && cdef->fkey.field_mapping_size == 0);
	const struct space *foreign_space = space_by_id(cdef->fkey.space_id);
	assert(foreign_space != NULL);
	const struct tuple_constraint_field_id *field = &cdef->fkey.field;
	if (field->name_len == 0 &&
	    field->id >= foreign_space->def->field_count) {
		const char *err = "foreign field is unnamed";
		sql_desc_error(desc, "foreign key", cdef->name, err);
		return;
	}

	const char *field_name = field->name_len > 0 ? field->name :
				 foreign_space->def->fields[field->id].name;
	sql_desc_append(desc, " CONSTRAINT ");
	sql_desc_append_name(desc, cdef->name);
	sql_desc_append(desc, " REFERENCES ");
	sql_desc_append_name(desc, foreign_space->def->name);
	sql_desc_append(desc, "(");
	sql_desc_append_name(desc, field_name);
	sql_desc_append(desc, ")");
}

/** Add a tuple foreign key constraint to the statement description. */
static void
sql_describe_tuple_foreign_key(struct sql_desc *desc,
			       const struct space_def *def, int i)
{
	struct tuple_constraint_def *cdef = &def->opts.constraint_def[i];
	assert(cdef->type == CONSTR_FKEY && cdef->fkey.field_mapping_size > 0);
	const struct space *foreign_space = space_by_id(cdef->fkey.space_id);
	assert(foreign_space != NULL);
	bool is_error = false;
	for (uint32_t i = 0; i < cdef->fkey.field_mapping_size; ++i) {
		const struct tuple_constraint_field_id *field =
			&cdef->fkey.field_mapping[i].local_field;
		if (field->name_len == 0 &&
		    field->id >= foreign_space->def->field_count) {
			sql_desc_error(desc, "foreign key", cdef->name,
				       "local field is unnamed");
			is_error = true;
		}
		field = &cdef->fkey.field_mapping[i].foreign_field;
		if (field->name_len == 0 &&
		    field->id >= foreign_space->def->field_count) {
			sql_desc_error(desc, "foreign key", cdef->name,
				       "foreign field is unnamed");
			is_error = true;
		}
	}
	if (is_error)
		return;

	assert(def->field_count != 0);
	sql_desc_append(desc, ",\nCONSTRAINT ");
	sql_desc_append_name(desc, cdef->name);
	sql_desc_append(desc, " FOREIGN KEY(");
	for (uint32_t i = 0; i < cdef->fkey.field_mapping_size; ++i) {
		const struct tuple_constraint_field_id *field =
			&cdef->fkey.field_mapping[i].local_field;
		const char *field_name = field->name_len != 0 ? field->name :
					 def->fields[field->id].name;
		if (i > 0)
			sql_desc_append(desc, ", ");
		sql_desc_append_name(desc, field_name);
	}

	sql_desc_append(desc, ") REFERENCES ");
	sql_desc_append_name(desc, foreign_space->def->name);
	sql_desc_append(desc, "(");
	for (uint32_t i = 0; i < cdef->fkey.field_mapping_size; ++i) {
		const struct tuple_constraint_field_id *field =
			&cdef->fkey.field_mapping[i].foreign_field;
		assert(field->name_len != 0 || field->id < def->field_count);
		const char *field_name = field->name_len != 0 ? field->name :
					 def->fields[field->id].name;
		if (i > 0)
			sql_desc_append(desc, ", ");
		sql_desc_append_name(desc, field_name);
	}
	sql_desc_append(desc, ")");
}

/** Add a field check constraint to the statement description. */
static void
sql_describe_field_check(struct sql_desc *desc, const char *field_name,
			 const struct tuple_constraint_def *cdef)
{
	assert(cdef->type == CONSTR_FUNC);
	const struct func *func = func_by_id(cdef->func.id);
	if (func->def->language != FUNC_LANGUAGE_SQL_EXPR) {
		sql_desc_error(desc, "check constraint", cdef->name,
			       "wrong constraint expression");
		return;
	}
	if (!func_sql_expr_has_single_arg(func, field_name)) {
		sql_desc_error(desc, "check constraint", cdef->name,
			       "wrong field name in constraint expression");
		return;
	}

	sql_desc_append(desc, " CONSTRAINT ");
	sql_desc_append_name(desc, cdef->name);
	sql_desc_append(desc, " CHECK(%s)", func->def->body);
}

/** Add a tuple check constraint to the statement description. */
static void
sql_describe_tuple_check(struct sql_desc *desc, const struct space_def *def,
			 int i)
{
	struct tuple_constraint_def *cdef = &def->opts.constraint_def[i];
	assert(cdef->type == CONSTR_FUNC);
	const struct func *func = func_by_id(cdef->func.id);
	if (func->def->language != FUNC_LANGUAGE_SQL_EXPR) {
		sql_desc_error(desc, "check constraint", cdef->name,
			       "wrong constraint expression");
		return;
	}
	if (!func_sql_expr_check_fields(func, def)) {
		sql_desc_error(desc, "check constraint", cdef->name,
			       "wrong field name in constraint expression");
		return;
	}
	if (i != 0 || def->field_count != 0)
		sql_desc_append(desc, ",");
	sql_desc_append(desc, "\nCONSTRAINT ");
	sql_desc_append_name(desc, cdef->name);
	sql_desc_append(desc, " CHECK(%s)", func->def->body);
}

/** Add a field to the statement description. */
static void
sql_describe_field(struct sql_desc *desc, const struct field_def *field)
{
	sql_desc_append(desc, "\n");
	sql_desc_append_name(desc, field->name);
	char *field_type = strtoupperdup(field_type_strs[field->type]);
	sql_desc_append(desc, " %s", field_type);
	free(field_type);

	if (field->coll_id != 0) {
		struct coll_id *coll_id = coll_by_id(field->coll_id);
		if (coll_id == NULL) {
			sql_desc_error(desc, "collation",
				       tt_sprintf("%d", field->coll_id),
				       "collation does not exist");
		} else {
			sql_desc_append(desc, " COLLATE ");
			sql_desc_append_name(desc, coll_id->name);
		}
	}
	if (!field->is_nullable)
		sql_desc_append(desc, " NOT NULL");
	if (field->default_value != NULL)
		sql_desc_append(desc, " DEFAULT(%s)", field->default_value);
	for (uint32_t i = 0; i < field->constraint_count; ++i) {
		struct tuple_constraint_def *cdef = &field->constraint_def[i];
		assert(cdef->type == CONSTR_FKEY || cdef->type == CONSTR_FUNC);
		if (cdef->type == CONSTR_FKEY)
			sql_describe_field_foreign_key(desc, cdef);
		else
			sql_describe_field_check(desc, field->name, cdef);
	}
}

/** Add a primary key to the statement description. */
static void
sql_describe_primary_key(struct sql_desc *desc, const struct space *space)
{
	if (space->index_count == 0) {
		const char *err = "primary key is not defined";
		sql_desc_error(desc, "space", space->def->name, err);
		return;
	}

	const struct index *pk = space->index[0];
	assert(pk->def->opts.is_unique);
	bool is_error = false;
	if (pk->def->type != TREE) {
		const char *err = "primary key has unsupported index type";
		sql_desc_error(desc, "space", space->def->name, err);
		is_error = true;
	}

	for (uint32_t i = 0; i < pk->def->key_def->part_count; ++i) {
		uint32_t fieldno = pk->def->key_def->parts[i].fieldno;
		if (fieldno >= space->def->field_count) {
			const char *err = tt_sprintf("field %u is unnamed",
						     fieldno + 1);
			sql_desc_error(desc, "primary key", pk->def->name, err);
			is_error = true;
			continue;
		}
		struct field_def *field = &space->def->fields[fieldno];
		if (pk->def->key_def->parts[i].type != field->type) {
			const char *err =
				tt_sprintf("field '%s' and related part are of "
					   "different types", field->name);
			sql_desc_error(desc, "primary key", pk->def->name, err);
			is_error = true;
		}
		if (pk->def->key_def->parts[i].coll_id != field->coll_id) {
			const char *err =
				tt_sprintf("field '%s' and related part have "
					   "different collations", field->name);
			sql_desc_error(desc, "primary key", pk->def->name, err);
			is_error = true;
		}
	}
	if (is_error)
		return;

	bool has_sequence = false;
	if (space->sequence != NULL) {
		struct sequence_def *sdef = space->sequence->def;
		if (sdef->step != 1 || sdef->min != 0 || sdef->start != 1 ||
		    sdef->max != INT64_MAX || sdef->cache != 0 || sdef->cycle ||
		    strcmp(sdef->name, space->def->name) != 0) {
			const char *err = "unsupported sequence definition";
			sql_desc_error(desc, "sequence", sdef->name, err);
		} else if (space->sequence_fieldno > space->def->field_count) {
			const char *err =
				"sequence is attached to unnamed field";
			sql_desc_error(desc, "sequence", sdef->name, err);
		} else {
			has_sequence = true;
		}
	}

	sql_desc_append(desc, ",\nCONSTRAINT ");
	sql_desc_append_name(desc, pk->def->name);
	sql_desc_append(desc, " PRIMARY KEY(");
	for (uint32_t i = 0; i < pk->def->key_def->part_count; ++i) {
		uint32_t fieldno = pk->def->key_def->parts[i].fieldno;
		if (i > 0)
			sql_desc_append(desc, ", ");
		sql_desc_append_name(desc, space->def->fields[fieldno].name);
		if (has_sequence && fieldno == space->sequence_fieldno)
			sql_desc_append(desc, " AUTOINCREMENT");
	}
	sql_desc_append(desc, ")");
}

/** Add a index to the statement description. */
static void
sql_describe_index(struct sql_desc *desc, const struct space *space,
		   const struct index *index)
{
	assert(index != NULL);
	bool is_error = false;
	if (index->def->type != TREE) {
		const char *err = "unsupported index type";
		sql_desc_error(desc, "index", index->def->name, err);
		is_error = true;
	}
	for (uint32_t i = 0; i < index->def->key_def->part_count; ++i) {
		uint32_t fieldno = index->def->key_def->parts[i].fieldno;
		if (fieldno >= space->def->field_count) {
			const char *err = tt_sprintf("field %u is unnamed",
						     fieldno + 1);
			sql_desc_error(desc, "index", index->def->name, err);
			is_error = true;
			continue;
		}
		struct field_def *field = &space->def->fields[fieldno];
		if (index->def->key_def->parts[i].type != field->type) {
			const char *err =
				tt_sprintf("field '%s' and related part are of "
					   "different types", field->name);
			sql_desc_error(desc, "index", index->def->name, err);
			is_error = true;
		}
		if (index->def->key_def->parts[i].coll_id != field->coll_id) {
			const char *err =
				tt_sprintf("field '%s' and related part have "
					   "different collations", field->name);
			sql_desc_error(desc, "index", index->def->name, err);
			is_error = true;
		}
	}
	if (is_error)
		return;

	if (!index->def->opts.is_unique)
		sql_desc_append(desc, "CREATE INDEX ");
	else
		sql_desc_append(desc, "CREATE UNIQUE INDEX ");
	sql_desc_append_name(desc, index->def->name);
	sql_desc_append(desc, " ON ");
	sql_desc_append_name(desc, space->def->name);
	sql_desc_append(desc, "(");
	for (uint32_t i = 0; i < index->def->key_def->part_count; ++i) {
		uint32_t fieldno = index->def->key_def->parts[i].fieldno;
		if (i > 0)
			sql_desc_append(desc, ", ");
		sql_desc_append_name(desc, space->def->fields[fieldno].name);
	}
	sql_desc_append(desc, ");");
	sql_desc_finish_statement(desc);
}

/** Add the table to the statement description. */
static void
sql_describe_table(struct sql_desc *desc, const struct space *space)
{
	struct space_def *def = space->def;
	sql_desc_append(desc, "CREATE TABLE ");
	sql_desc_append_name(desc, def->name);

	if (def->field_count + def->opts.constraint_count > 0)
		sql_desc_append(desc, "(");

	if (def->field_count == 0)
		sql_desc_error(desc, "space", def->name, "format is missing");
	else
		sql_describe_field(desc, &def->fields[0]);
	for (uint32_t i = 1; i < def->field_count; ++i) {
		sql_desc_append(desc, ",");
		sql_describe_field(desc, &def->fields[i]);
	}

	sql_describe_primary_key(desc, space);

	for (uint32_t i = 0; i < def->opts.constraint_count; ++i) {
		assert(def->opts.constraint_def[i].type == CONSTR_FKEY ||
		       def->opts.constraint_def[i].type == CONSTR_FUNC);
		if (def->opts.constraint_def[i].type == CONSTR_FKEY)
			sql_describe_tuple_foreign_key(desc, def, i);
		else
			sql_describe_tuple_check(desc, def, i);
	}

	if (def->field_count + def->opts.constraint_count > 0)
		sql_desc_append(desc, ")");

	if (space_is_memtx(space))
		sql_desc_append(desc, "\nWITH ENGINE = 'memtx'");
	else if (space_is_vinyl(space))
		sql_desc_append(desc, "\nWITH ENGINE = 'vinyl'");
	else
		sql_desc_error(desc, "space", def->name, "wrong space engine");
	sql_desc_append(desc, ";");
	sql_desc_finish_statement(desc);
}

void
sql_show_create_table(uint32_t space_id, struct Mem *ret, struct Mem *err)
{
	struct space *space = space_by_id(space_id);
	assert(space != NULL);

	struct sql_desc desc;
	sql_desc_initialize(&desc, ret, err);
	sql_describe_table(&desc, space);
	for (uint32_t i = 1; i < space->index_count; ++i)
		sql_describe_index(&desc, space, space->index[i]);
	sql_desc_finalize(&desc);
}
