/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

/*
 * This file contains C code routines that are called by the sql parser
 * when syntax rules are reduced.  The routines in this file handle the
 * following kinds of SQL syntax:
 *
 *     CREATE TABLE
 *     DROP TABLE
 *     CREATE INDEX
 *     DROP INDEX
 *     creating ID lists
 *     BEGIN TRANSACTION
 *     COMMIT
 *     ROLLBACK
 */
#include <ctype.h>
#include "sqlInt.h"
#include "vdbeInt.h"
#include "tarantoolInt.h"
#include "box/ck_constraint.h"
#include "box/fk_constraint.h"
#include "box/sequence.h"
#include "box/session.h"
#include "box/identifier.h"
#include "box/schema.h"
#include "box/tuple_format.h"
#include "box/coll_id_cache.h"
#include "box/user.h"
#include "box/constraint_id.h"
#include "box/session_settings.h"

void
sql_finish_coding(struct Parse *parse_context)
{
	assert(parse_context->pToplevel == NULL);
	struct sql *db = parse_context->db;
	struct Vdbe *v = sqlGetVdbe(parse_context);
	sqlVdbeAddOp0(v, OP_Halt);

	if (db->mallocFailed)
		parse_context->is_aborted = true;
	if (parse_context->is_aborted)
		return;
	/*
	 * Begin by generating some termination code at the end
	 * of the vdbe program
	 */
	int last_instruction = v->nOp;
	if (parse_context->initiateTTrans)
		sqlVdbeAddOp0(v, OP_TTransaction);
	if (parse_context->pConstExpr != NULL) {
		assert(sqlVdbeGetOp(v, 0)->opcode == OP_Init);
		/*
		 * Code constant expressions that where
		 * factored out of inner loops.
		 */
		struct ExprList *exprs = parse_context->pConstExpr;
		parse_context->okConstFactor = 0;
		for (int i = 0; i < exprs->nExpr; ++i) {
			sqlExprCode(parse_context, exprs->a[i].pExpr,
					exprs->a[i].u. iConstExprReg);
		}
	}
	/*
	 * Finally, jump back to the beginning of
	 * the executable code. In fact, it is required
	 * only if some additional opcodes are generated.
	 * Otherwise, it would be useless jump:
	 *
	 * 0:        OP_Init 0 vdbe_end ...
	 * 1: ...
	 *    ...
	 * vdbe_end: OP_Goto 0 1 ...
	 */
	if (parse_context->initiateTTrans ||
	    parse_context->pConstExpr != NULL) {
		sqlVdbeChangeP2(v, 0, last_instruction);
		sqlVdbeGoto(v, 1);
	}
	/* Get the VDBE program ready for execution. */
	if (!parse_context->is_aborted && !db->mallocFailed) {
		assert(parse_context->iCacheLevel == 0);
		sqlVdbeMakeReady(v, parse_context);
	} else {
		parse_context->is_aborted = true;
	}
}

bool
sql_space_column_is_in_pk(struct space *space, uint32_t column)
{
	if (space->def->opts.is_view)
		return false;
	struct index *primary_idx = space_index(space, 0);
	assert(primary_idx != NULL);
	struct key_def *key_def = primary_idx->def->key_def;
	uint64_t pk_mask = key_def->column_mask;
	if (column < 63)
		return (pk_mask & (((uint64_t) 1) << column)) != 0;
	else if ((pk_mask & (((uint64_t) 1) << 63)) != 0)
		return key_def_find_by_fieldno(key_def, column) != NULL;
	return false;
}

/*
 * This routine is used to check if the UTF-8 string zName is a legal
 * unqualified name for an identifier.
 * Some objects may not be checked, because they are validated in Tarantool.
 * (e.g. table, index, column name of a real table)
 * All names are legal except those that cantain non-printable
 * characters or have length greater than BOX_NAME_MAX.
 *
 * @param pParse Parser context.
 * @param zName Identifier to check.
 *
 * @retval 0 on success.
 * @retval -1 on error.
 */
int
sqlCheckIdentifierName(Parse *pParse, char *zName)
{
	ssize_t len = strlen(zName);
	if (len > BOX_NAME_MAX) {
		diag_set(ClientError, ER_IDENTIFIER,
			 tt_cstr(zName, BOX_INVALID_NAME_MAX));
		pParse->is_aborted = true;
		return -1;
	}
	if (identifier_check(zName, len) != 0) {
		pParse->is_aborted = true;
		return -1;
	}
	return 0;
}

/**
 * Return the PRIMARY KEY index of a table.
 *
 * Note that during parsing routines this function is not equal
 * to space_index(space, 0); call since primary key can be added
 * after seconary keys:
 *
 * CREATE TABLE t (a INT UNIQUE, b PRIMARY KEY);
 *
 * In this particular case, after secondary index processing
 * space still lacks PK, but index[0] != NULL since index array
 * is filled in a straightforward way. Hence, function must
 * return NULL.
 */
static struct index *
sql_space_primary_key(const struct space *space)
{
	if (space->index_count == 0 || space->index[0]->def->iid != 0)
		return NULL;
	return space->index[0];
}

/*
 * Begin constructing a new table representation in memory.  This is
 * the first of several action routines that get called in response
 * to a CREATE TABLE statement.  In particular, this routine is called
 * after seeing tokens "CREATE" and "TABLE" and the table name. The isTemp
 * flag is true if the table should be stored in the auxiliary database
 * file instead of in the main database file.  This is normally the case
 * when the "TEMP" or "TEMPORARY" keyword occurs in between
 * CREATE and TABLE.
 *
 * The new table record is initialized and put in pParse->create_table_def.
 * As more of the CREATE TABLE statement is parsed, additional action
 * routines will be called to add more information to this record.
 * At the end of the CREATE TABLE statement, the sqlEndTable() routine
 * is called to complete the construction of the new table record.
 *
 * @param pParse Parser context.
 * @param pName1 First part of the name of the table or view.
 */
struct space *
sqlStartTable(Parse *pParse, Token *pName)
{
	char *zName = 0;	/* The name of the new table */
	sql *db = pParse->db;
	struct space *new_space = NULL;
	struct Vdbe *v = sqlGetVdbe(pParse);
	if (v == NULL)
		goto cleanup;
	sqlVdbeCountChanges(v);

	zName = sql_name_from_token(db, pName);
	if (zName == NULL) {
		pParse->is_aborted = true;
		goto cleanup;
	}

	if (sqlCheckIdentifierName(pParse, zName) != 0)
		goto cleanup;

	new_space = sql_ephemeral_space_new(pParse, zName);
	if (new_space == NULL)
		goto cleanup;

	strcpy(new_space->def->engine_name,
	       sql_storage_engine_strs[current_session()->sql_default_engine]);

	if (!db->init.busy && (v = sqlGetVdbe(pParse)) != 0)
		sql_set_multi_write(pParse, true);

 cleanup:
	sqlDbFree(db, zName);
	return new_space;
}

/**
 * Get field by id. Allocate memory if needed.
 * Useful in cases when initial field_count is unknown.
 * Allocated memory should by manually released.
 * @param parser SQL Parser object.
 * @param space_def Space definition.
 * @param id column identifier.
 * @retval not NULL on success.
 * @retval NULL on out of memory.
 */
static struct field_def *
sql_field_retrieve(Parse *parser, struct space_def *space_def, uint32_t id)
{
	struct field_def *field;
	assert(space_def != NULL);
	assert(id < SQL_MAX_COLUMN);

	if (id >= space_def->exact_field_count) {
		uint32_t columns_new = space_def->exact_field_count;
		columns_new = (columns_new > 0) ? 2 * columns_new : 1;
		struct region *region = &parser->region;
		size_t size;
		field = region_alloc_array(region, typeof(field[0]),
					   columns_new, &size);
		if (field == NULL) {
			diag_set(OutOfMemory, size, "region_alloc_array",
				 "field");
			parser->is_aborted = true;
			return NULL;
		}

		memcpy(field, space_def->fields,
		       sizeof(*field) * space_def->exact_field_count);
		for (uint32_t i = columns_new / 2; i < columns_new; i++) {
			memcpy(&field[i], &field_def_default,
			       sizeof(struct field_def));
		}

		space_def->fields = field;
		space_def->exact_field_count = columns_new;
	}

	field = &space_def->fields[id];
	return field;
}

/**
 * Make shallow copy of @a space on region.
 *
 * Function is used to add a new column to an existing space with
 * <ALTER TABLE ADD COLUMN> statement. Copy space def and index
 * array to create constraints appeared in the statement. The
 * index array copy will be modified by adding new elements to it.
 * It is necessary, because the statement may contain several
 * index definitions (constraints).
 */
static struct space *
sql_shallow_space_copy(struct Parse *parse, struct space *space)
{
	assert(space->def != NULL);
	struct space *ret = sql_ephemeral_space_new(parse, space->def->name);
	if (ret == NULL)
		goto error;
	ret->index_count = space->index_count;
	ret->index_id_max = space->index_id_max;
	size_t size = 0;
	ret->index = region_alloc_array(&parse->region, typeof(struct index *),
					ret->index_count, &size);
	if (ret->index == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "ret->index");
		goto error;
	}
	memcpy(ret->index, space->index, size);
	memcpy(ret->def, space->def, sizeof(struct space_def));
	ret->def->opts.is_temporary = true;
	ret->def->opts.is_ephemeral = true;
	if (ret->def->field_count != 0) {
		uint32_t fields_size = 0;
		ret->def->fields =
			region_alloc_array(&parse->region,
					   typeof(struct field_def),
					   ret->def->field_count, &fields_size);
		if (ret->def->fields == NULL) {
			diag_set(OutOfMemory, fields_size, "region_alloc",
				 "ret->def->fields");
			goto error;
		}
		memcpy(ret->def->fields, space->def->fields, fields_size);
	}

	return ret;
error:
	parse->is_aborted = true;
	return NULL;
}

void
sql_create_column_start(struct Parse *parse)
{
	struct create_column_def *create_column_def = &parse->create_column_def;
	struct alter_entity_def *alter_entity_def =
		&create_column_def->base.base;
	assert(alter_entity_def->entity_type == ENTITY_TYPE_COLUMN);
	assert(alter_entity_def->alter_action == ALTER_ACTION_CREATE);
	struct space *space = parse->create_table_def.new_space;
	bool is_alter = space == NULL;
	struct sql *db = parse->db;
	if (is_alter) {
		const char *space_name =
			alter_entity_def->entity_name->a[0].zName;
		space = space_by_name(space_name);
		if (space == NULL) {
			diag_set(ClientError, ER_NO_SUCH_SPACE, space_name);
			goto tnt_error;
		}
		space = sql_shallow_space_copy(parse, space);
		if (space == NULL)
			goto tnt_error;
	}
	create_column_def->space = space;
	struct space_def *def = space->def;
	assert(def->opts.is_ephemeral);

#if SQL_MAX_COLUMN
	if ((int)def->field_count + 1 > db->aLimit[SQL_LIMIT_COLUMN]) {
		diag_set(ClientError, ER_SQL_COLUMN_COUNT_MAX, def->name,
			 def->field_count + 1, db->aLimit[SQL_LIMIT_COLUMN]);
		goto tnt_error;
	}
#endif

	struct region *region = &parse->region;
	struct Token *name = &create_column_def->base.name;
	char *column_name =
		sql_normalized_name_region_new(region, name->z, name->n);
	if (column_name == NULL)
		goto tnt_error;

	/*
	 * Format can be set in Lua, then exact_field_count can be
	 * zero, but field_count is not.
	 */
	if (def->exact_field_count == 0)
		def->exact_field_count = def->field_count;
	if (sql_field_retrieve(parse, def, def->field_count) == NULL)
		return;

	struct field_def *column_def = &def->fields[def->field_count];
	memcpy(column_def, &field_def_default, sizeof(field_def_default));
	column_def->name = column_name;
	/*
	 * Marker ON_CONFLICT_ACTION_DEFAULT is used to detect
	 * attempts to define NULL multiple time or to detect
	 * invalid primary key definition.
	 */
	column_def->nullable_action = ON_CONFLICT_ACTION_DEFAULT;
	column_def->is_nullable = true;
	column_def->type = create_column_def->type_def->type;
	def->field_count++;

	sqlSrcListDelete(db, alter_entity_def->entity_name);
	return;
tnt_error:
	parse->is_aborted = true;
	sqlSrcListDelete(db, alter_entity_def->entity_name);
}

static void
vdbe_emit_create_constraints(struct Parse *parse, int reg_space_id);

void
sql_create_column_end(struct Parse *parse)
{
	struct space *space = parse->create_column_def.space;
	assert(space != NULL);
	struct space_def *def = space->def;
	struct field_def *field = &def->fields[def->field_count - 1];
	if (field->nullable_action == ON_CONFLICT_ACTION_DEFAULT) {
		field->nullable_action = ON_CONFLICT_ACTION_NONE;
		field->is_nullable = true;
	}
	/*
	 * Encode the format array and emit code to update _space.
	 */
	uint32_t table_stmt_sz = 0;
	struct region *region = &parse->region;
	char *table_stmt = sql_encode_table(region, def, &table_stmt_sz);
	char *raw = sqlDbMallocRaw(parse->db, table_stmt_sz);
	if (table_stmt == NULL || raw == NULL) {
		parse->is_aborted = true;
		return;
	}
	memcpy(raw, table_stmt, table_stmt_sz);

	struct Vdbe *v = sqlGetVdbe(parse);
	assert(v != NULL);

	struct space *s_space = space_by_id(BOX_SPACE_ID);
	assert(s_space != NULL);
	int cursor = parse->nTab++;
	vdbe_emit_open_cursor(parse, cursor, 0, s_space);
	sqlVdbeChangeP5(v, OPFLAG_SYSTEMSP);

	int key_reg = ++parse->nMem;
	sqlVdbeAddOp2(v, OP_Integer, def->id, key_reg);
	int addr = sqlVdbeAddOp4Int(v, OP_Found, cursor, 0, key_reg, 1);
	sqlVdbeAddOp2(v, OP_Halt, -1, ON_CONFLICT_ACTION_ABORT);
	sqlVdbeJumpHere(v, addr);

	int tuple_reg = sqlGetTempRange(parse, box_space_field_MAX + 1);
	for (int i = 0; i < box_space_field_MAX - 1; ++i)
		sqlVdbeAddOp3(v, OP_Column, cursor, i, tuple_reg + i);
	sqlVdbeAddOp1(v, OP_Close, cursor);

	sqlVdbeAddOp2(v, OP_Integer, def->field_count,
		      tuple_reg + BOX_SPACE_FIELD_FIELD_COUNT);
	sqlVdbeAddOp4(v, OP_Blob, table_stmt_sz,
		      tuple_reg + BOX_SPACE_FIELD_FORMAT,
		      SQL_SUBTYPE_MSGPACK, raw, P4_DYNAMIC);
	sqlVdbeAddOp3(v, OP_MakeRecord, tuple_reg, box_space_field_MAX,
		      tuple_reg + box_space_field_MAX);
	sqlVdbeAddOp4(v, OP_IdxReplace, tuple_reg + box_space_field_MAX, 0, 0,
		      (char *) s_space, P4_SPACEPTR);
	sqlVdbeCountChanges(v);
	sqlVdbeChangeP5(v, OPFLAG_NCHANGE);
	sqlReleaseTempRange(parse, tuple_reg, box_space_field_MAX + 1);
	vdbe_emit_create_constraints(parse, key_reg);
}

void
sql_column_add_nullable_action(struct Parse *parser,
			       enum on_conflict_action nullable_action)
{
	assert(parser->create_column_def.space != NULL);
	struct space_def *def = parser->create_column_def.space->def;
	assert(def->field_count > 0);
	struct field_def *field = &def->fields[def->field_count - 1];
	if (field->nullable_action != ON_CONFLICT_ACTION_DEFAULT &&
	    nullable_action != field->nullable_action) {
		/* Prevent defining nullable_action many times. */
		const char *err = "NULL declaration for column '%s' of table "
				  "'%s' has been already set to '%s'";
		const char *action =
			on_conflict_action_strs[field->nullable_action];
		err = tt_sprintf(err, field->name, def->name, action);
		diag_set(ClientError, ER_SQL_EXECUTE, err);
		parser->is_aborted = true;
		return;
	}
	field->nullable_action = nullable_action;
	field->is_nullable = action_is_nullable(nullable_action);
}

/*
 * The expression is the default value for the most recently added
 * column.
 *
 * Default value expressions must be constant.  Raise an exception if this
 * is not the case.
 *
 * This routine is called by the parser while in the middle of
 * parsing a <CREATE TABLE> or an <ALTER TABLE ADD COLUMN>
 * statement.
 */
void
sqlAddDefaultValue(Parse * pParse, ExprSpan * pSpan)
{
	sql *db = pParse->db;
	struct space *p = pParse->create_column_def.space;
	if (p != NULL) {
		assert(p->def->opts.is_ephemeral);
		struct space_def *def = p->def;
		if (!sqlExprIsConstantOrFunction
		    (pSpan->pExpr, db->init.busy)) {
			const char *column_name =
				def->fields[def->field_count - 1].name;
			diag_set(ClientError, ER_CREATE_SPACE, def->name,
				 tt_sprintf("default value of column '%s' is "\
					    "not constant", column_name));
			pParse->is_aborted = true;
		} else {
			assert(def != NULL);
			struct field_def *field =
				&def->fields[def->field_count - 1];
			struct region *region = &pParse->region;
			uint32_t default_length = (int)(pSpan->zEnd - pSpan->zStart);
			field->default_value = region_alloc(region,
							    default_length + 1);
			if (field->default_value == NULL) {
				diag_set(OutOfMemory, default_length + 1,
					 "region_alloc",
					 "field->default_value");
				pParse->is_aborted = true;
				return;
			}
			strncpy(field->default_value, pSpan->zStart,
				default_length);
			field->default_value[default_length] = '\0';
		}
	}
	sql_expr_delete(db, pSpan->pExpr, false);
}

static int
field_def_create_for_pk(struct Parse *parser, struct field_def *field,
			const char *space_name)
{
	if (field->nullable_action != ON_CONFLICT_ACTION_ABORT &&
	    field->nullable_action != ON_CONFLICT_ACTION_DEFAULT) {
		diag_set(ClientError, ER_NULLABLE_PRIMARY, space_name);
		parser->is_aborted = true;
		return -1;
	} else if (field->nullable_action == ON_CONFLICT_ACTION_DEFAULT) {
		field->nullable_action = ON_CONFLICT_ACTION_ABORT;
		field->is_nullable = false;
	}
	return 0;
}

/*
 * Designate the PRIMARY KEY for the table.  pList is a list of names
 * of columns that form the primary key.  If pList is NULL, then the
 * most recently added column of the table is the primary key.
 *
 * A table can have at most one primary key.  If the table already has
 * a primary key (and this is the second primary key) then create an
 * error.
 *
 * If the key is not an INTEGER PRIMARY KEY, then create a unique
 * index for the key.  No index is created for INTEGER PRIMARY KEYs.
 */
void
sqlAddPrimaryKey(struct Parse *pParse)
{
	int iCol = -1, i;
	int nTerm;
	struct ExprList *pList = pParse->create_index_def.cols;
	struct space *space = pParse->create_table_def.new_space;
	if (space == NULL)
		space = pParse->create_column_def.space;
	if (space == NULL)
		goto primary_key_exit;
	if (sql_space_primary_key(space) != NULL) {
		diag_set(ClientError, ER_CREATE_SPACE, space->def->name,
			 "primary key has been already declared");
		pParse->is_aborted = true;
		goto primary_key_exit;
	}
	if (pList == NULL) {
		iCol = space->def->field_count - 1;
		nTerm = 1;
	} else {
		nTerm = pList->nExpr;
		for (i = 0; i < nTerm; i++) {
			Expr *pCExpr =
			    sqlExprSkipCollate(pList->a[i].pExpr);
			assert(pCExpr != 0);
			if (pCExpr->op != TK_ID) {
				diag_set(ClientError, ER_INDEX_DEF_UNSUPPORTED,
					 "Expressions");
				pParse->is_aborted = true;
				goto primary_key_exit;
			}
			const char *name = pCExpr->u.zToken;
			struct space_def *def = space->def;
			for (uint32_t idx = 0; idx < def->field_count; idx++) {
				if (strcmp(name, def->fields[idx].name) == 0) {
					iCol = idx;
					break;
				}
			}
		}
	}
	if (nTerm == 1 && iCol != -1 &&
	    space->def->fields[iCol].type == FIELD_TYPE_INTEGER) {
		struct sql *db = pParse->db;
		struct ExprList *list;
		struct Token token;
		sqlTokenInit(&token, space->def->fields[iCol].name);
		struct Expr *expr = sql_expr_new(db, TK_ID, &token);
		if (expr == NULL) {
			pParse->is_aborted = true;
			goto primary_key_exit;
		}
		list = sql_expr_list_append(db, NULL, expr);
		if (list == NULL)
			goto primary_key_exit;
		pParse->create_index_def.cols = list;
		sql_create_index(pParse);
		if (db->mallocFailed)
			goto primary_key_exit;
	} else {
		sql_create_index(pParse);
		pList = NULL;
		if (pParse->is_aborted)
			goto primary_key_exit;
	}

	struct index *pk = sql_space_primary_key(space);
	assert(pk != NULL);
	struct key_def *pk_key_def = pk->def->key_def;
	for (uint32_t i = 0; i < pk_key_def->part_count; i++) {
		uint32_t idx = pk_key_def->parts[i].fieldno;
		field_def_create_for_pk(pParse, &space->def->fields[idx],
					space->def->name);
	}
primary_key_exit:
	sql_expr_list_delete(pParse->db, pList);
	return;
}

/**
 * Prepare a 0-terminated string in the wptr memory buffer that
 * does not contain a sequence of more than one whatespace
 * character. Routine enforces ' ' (space) as whitespace
 * delimiter. When character ' or " was met, the string is copied
 * without any changes until the next ' or " sign.
 * The wptr buffer is expected to have str_len + 1 bytes
 * (this is the expected scenario where no extra whitespace
 * characters in the source string).
 * @param wptr The destination memory buffer of size
 *             @a str_len + 1.
 * @param str The source string to be copied.
 * @param str_len The source string @a str length.
 */
static void
trim_space_snprintf(char *wptr, const char *str, uint32_t str_len)
{
	const char *str_end = str + str_len;
	char quote_type = '\0';
	bool is_prev_chr_space = false;
	while (str < str_end) {
		if (quote_type == '\0') {
			if (*str == '\'' || *str == '\"') {
				quote_type = *str;
			} else if (isspace((unsigned char)*str)) {
				if (!is_prev_chr_space)
					*wptr++ = ' ';
				is_prev_chr_space = true;
				str++;
				continue;
			}
		} else if (*str == quote_type) {
			quote_type = '\0';
		}
		is_prev_chr_space = false;
		*wptr++ = *str++;
	}
	*wptr = '\0';
}

static void
vdbe_emit_ck_constraint_create(struct Parse *parser,
			       const struct ck_constraint_def *ck_def,
			       uint32_t reg_space_id, const char *space_name);

void
sql_create_check_contraint(struct Parse *parser)
{
	struct create_ck_def *create_ck_def = &parser->create_ck_def;
	struct ExprSpan *expr_span = create_ck_def->expr;
	sql_expr_delete(parser->db, expr_span->pExpr, false);

	struct alter_entity_def *alter_def =
		(struct alter_entity_def *) create_ck_def;
	assert(alter_def->entity_type == ENTITY_TYPE_CK);
	(void) alter_def;
	struct space *space = parser->create_column_def.space;
	if (space == NULL)
		space = parser->create_table_def.new_space;
	bool is_alter_add_constr = space == NULL;

	/* Prepare payload for ck constraint definition. */
	struct region *region = &parser->region;
	struct Token *name_token = &create_ck_def->base.base.name;
	const char *name;
	if (name_token->n > 0) {
		name = sql_normalized_name_region_new(region, name_token->z,
						      name_token->n);
		if (name == NULL) {
			parser->is_aborted = true;
			return;
		}
	} else {
		assert(!is_alter_add_constr);
		uint32_t ck_idx =
			++parser->create_ck_constraint_parse_def.count;
		/*
		 * If it is <ALTER TABLE ADD COLUMN> we should
		 * count the existing CHECK constraints in the
		 * space and form a name based on this.
		 */
		if (parser->create_table_def.new_space == NULL) {
			struct space *original_space =
				space_by_name(space->def->name);
			assert(original_space != NULL);
			struct ck_constraint *ck;
			rlist_foreach_entry(ck, &original_space->ck_constraint,
					    link)
				ck_idx++;
		}
		name = tt_sprintf("ck_unnamed_%s_%u", space->def->name, ck_idx);
	}
	size_t name_len = strlen(name);

	uint32_t expr_str_len = (uint32_t)(expr_span->zEnd - expr_span->zStart);
	const char *expr_str = expr_span->zStart;

	/*
	 * Allocate memory for ck constraint parse structure and
	 * ck constraint definition as a single memory chunk on
	 * region:
	 *
	 *    [ck_parse][ck_def[name][expr_str]]
	 *         |_____^  |_________^
	 */
	uint32_t expr_str_offset;
	uint32_t ck_def_sz = ck_constraint_def_sizeof(name_len, expr_str_len,
						      &expr_str_offset);
	struct ck_constraint_parse *ck_parse;
	size_t total = sizeof(*ck_parse) + ck_def_sz;
	ck_parse = (struct ck_constraint_parse *)
		region_aligned_alloc(region, total, alignof(*ck_parse));
	if (ck_parse == NULL) {
		diag_set(OutOfMemory, total, "region_aligned_alloc",
			 "ck_parse");
		parser->is_aborted = true;
		return;
	}
	struct ck_constraint_def *ck_def =
		(struct ck_constraint_def *)((char *)ck_parse +
					     sizeof(*ck_parse));
	static_assert(alignof(*ck_def) == alignof(*ck_parse),
		      "allocated in one block and should have the same "
		      "alignment");
	ck_parse->ck_def = ck_def;
	rlist_create(&ck_parse->link);

	ck_def->expr_str = (char *)ck_def + expr_str_offset;
	ck_def->language = CK_CONSTRAINT_LANGUAGE_SQL;
	ck_def->space_id = BOX_ID_NIL;
	trim_space_snprintf(ck_def->expr_str, expr_str, expr_str_len);
	memcpy(ck_def->name, name, name_len);
	ck_def->name[name_len] = '\0';
	if (is_alter_add_constr) {
		const char *space_name = alter_def->entity_name->a[0].zName;
		struct space *space = space_by_name(space_name);
		if (space == NULL) {
			diag_set(ClientError, ER_NO_SUCH_SPACE, space_name);
			parser->is_aborted = true;
			return;
		}
		int space_id_reg = ++parser->nMem;
		struct Vdbe *v = sqlGetVdbe(parser);
		sqlVdbeAddOp2(v, OP_Integer, space->def->id,
			      space_id_reg);
		vdbe_emit_ck_constraint_create(parser, ck_def, space_id_reg,
					       space->def->name);
		assert(sqlVdbeGetOp(v, v->nOp - 1)->opcode == OP_SInsert);
		sqlVdbeCountChanges(v);
		sqlVdbeChangeP5(v, OPFLAG_NCHANGE);
	} else {
		rlist_add_entry(&parser->create_ck_constraint_parse_def.checks,
				ck_parse, link);
	}
}

/*
 * Set the collation function of the most recently parsed table column
 * to the CollSeq given.
 */
void
sqlAddCollateType(Parse * pParse, Token * pToken)
{
	struct space *space = pParse->create_column_def.space;
	assert(space != NULL);
	uint32_t i = space->def->field_count - 1;
	sql *db = pParse->db;
	char *coll_name = sql_name_from_token(db, pToken);
	if (coll_name == NULL) {
		pParse->is_aborted = true;
		return;
	}
	uint32_t *coll_id = &space->def->fields[i].coll_id;
	if (sql_get_coll_seq(pParse, coll_name, coll_id) != NULL) {
		/* If the column is declared as "<name> PRIMARY KEY COLLATE <type>",
		 * then an index may have been created on this column before the
		 * collation type was added. Correct this if it is the case.
		 */
		for (uint32_t i = 0; i < space->index_count; ++i) {
			struct index *idx = space->index[i];
			assert(idx->def->key_def->part_count == 1);
			if (idx->def->key_def->parts[0].fieldno == i) {
				coll_id = &idx->def->key_def->parts[0].coll_id;
				(void)sql_column_collation(space->def, i, coll_id);
			}
		}
	}
	sqlDbFree(db, coll_name);
}

struct coll *
sql_column_collation(struct space_def *def, uint32_t column, uint32_t *coll_id)
{
	assert(def != NULL);
	/*
	 * It is not always possible to fetch collation directly
	 * from struct space due to its absence in space cache.
	 * To be more precise when space is ephemeral or it is
	 * under construction.
	 *
	 * In cases mentioned above collation is fetched by id.
	 */
	if (def->opts.is_ephemeral) {
		assert(column < (uint32_t)def->field_count);
		*coll_id = def->fields[column].coll_id;
		struct coll_id *collation = coll_by_id(*coll_id);
		return collation != NULL ? collation->coll : NULL;
	}
	struct space *space = space_by_id(def->id);
	struct tuple_field *field = tuple_format_field(space->format, column);
	*coll_id = field->coll_id;
	return field->coll;
}

int
vdbe_emit_open_cursor(struct Parse *parse_context, int cursor, int index_id,
		      struct space *space)
{
	assert(space != NULL);
	struct index *idx = index_find(space, index_id);
	assert(idx != NULL);
	if (idx->def->type != TREE) {
		diag_set(ClientError, ER_UNSUPPORTED, "SQL",
			 "using non-TREE index type. Please, use " \
			 "INDEXED BY clause to force using proper index.");
		parse_context->is_aborted = true;
		return -1;
	}
	return sqlVdbeAddOp4(parse_context->pVdbe, OP_IteratorOpen, cursor,
				 index_id, 0, (void *) space, P4_SPACEPTR);
}

/*
 * Generate code to determine the new space Id.
 * Fetch the max space id seen so far from _schema and increment it.
 * Return register storing the result.
 */
static int
getNewSpaceId(Parse * pParse)
{
	Vdbe *v = sqlGetVdbe(pParse);
	int iRes = ++pParse->nMem;

	sqlVdbeAddOp1(v, OP_IncMaxid, iRes);
	return iRes;
}

/**
 * Generate VDBE code to create an Index. This is accomplished by
 * adding an entry to the _index table.
 *
 * @param parse Current parsing context.
 * @param def Definition of space which index belongs to.
 * @param idx_def Definition of index under construction.
 * @param pk_def Definition of primary key index.
 * @param space_id_reg Register containing generated space id.
 * @param index_id_reg Register containing generated index id.
 */
static void
vdbe_emit_create_index(struct Parse *parse, struct space_def *def,
		       const struct index_def *idx_def, int space_id_reg,
		       int index_id_reg)
{
	struct Vdbe *v = sqlGetVdbe(parse);
	int entry_reg = ++parse->nMem;
	/*
	 * Entry in _index space contains 6 fields.
	 * The last one contains encoded tuple.
	 */
	int tuple_reg = (parse->nMem += 6);
	/* Format "opts" and "parts" for _index entry. */
	struct region *region = &parse->region;
	uint32_t index_opts_sz = 0;
	char *index_opts = sql_encode_index_opts(region, &idx_def->opts,
						 &index_opts_sz);
	if (index_opts == NULL)
		goto error;
	uint32_t index_parts_sz = 0;
	char *index_parts = sql_encode_index_parts(region, def->fields, idx_def,
						   &index_parts_sz);
	if (index_parts == NULL)
		goto error;
	char *raw = sqlDbMallocRaw(parse->db,
				       index_opts_sz +index_parts_sz);
	if (raw == NULL)
		return;
	memcpy(raw, index_opts, index_opts_sz);
	index_opts = raw;
	raw += index_opts_sz;
	memcpy(raw, index_parts, index_parts_sz);
	index_parts = raw;

	if (parse->create_table_def.new_space != NULL ||
	    parse->create_column_def.space != NULL) {
		sqlVdbeAddOp2(v, OP_SCopy, space_id_reg, entry_reg);
		sqlVdbeAddOp2(v, OP_Integer, idx_def->iid, entry_reg + 1);
	} else {
		/*
		 * An existing table is being modified;
		 * space_id_reg is literal, but index_id_reg is
		 * register.
		 */
		sqlVdbeAddOp2(v, OP_Integer, space_id_reg, entry_reg);
		sqlVdbeAddOp2(v, OP_SCopy, index_id_reg, entry_reg + 1);
	}
	sqlVdbeAddOp4(v, OP_String8, 0, entry_reg + 2, 0,
			  sqlDbStrDup(parse->db, idx_def->name),
			  P4_DYNAMIC);
	sqlVdbeAddOp4(v, OP_String8, 0, entry_reg + 3, 0, "tree",
			  P4_STATIC);
	sqlVdbeAddOp4(v, OP_Blob, index_opts_sz, entry_reg + 4,
			  SQL_SUBTYPE_MSGPACK, index_opts, P4_DYNAMIC);
	/* opts and parts are co-located, hence STATIC. */
	sqlVdbeAddOp4(v, OP_Blob, index_parts_sz, entry_reg + 5,
			  SQL_SUBTYPE_MSGPACK, index_parts, P4_STATIC);
	sqlVdbeAddOp3(v, OP_MakeRecord, entry_reg, 6, tuple_reg);
	sqlVdbeAddOp2(v, OP_SInsert, BOX_INDEX_ID, tuple_reg);
	return;
error:
	parse->is_aborted = true;

}

/**
 * Generate code to create a new space.
 *
 * @param space_id_reg is a register storing the id of the space.
 * @param table Table containing meta-information of space to be
 *              created.
 */
static void
vdbe_emit_space_create(struct Parse *pParse, int space_id_reg,
		       int space_name_reg, struct space *space)
{
	Vdbe *v = sqlGetVdbe(pParse);
	int iFirstCol = ++pParse->nMem;
	int tuple_reg = (pParse->nMem += 7);
	struct region *region = &pParse->region;
	uint32_t table_opts_stmt_sz = 0;
	char *table_opts_stmt = sql_encode_table_opts(region, space->def,
						      &table_opts_stmt_sz);
	if (table_opts_stmt == NULL)
		goto error;
	uint32_t table_stmt_sz = 0;
	char *table_stmt = sql_encode_table(region, space->def, &table_stmt_sz);
	if (table_stmt == NULL)
		goto error;
	char *raw = sqlDbMallocRaw(pParse->db,
				       table_stmt_sz + table_opts_stmt_sz);
	if (raw == NULL)
		return;

	memcpy(raw, table_opts_stmt, table_opts_stmt_sz);
	table_opts_stmt = raw;
	raw += table_opts_stmt_sz;
	memcpy(raw, table_stmt, table_stmt_sz);
	table_stmt = raw;

	sqlVdbeAddOp2(v, OP_SCopy, space_id_reg, iFirstCol /* spaceId */ );
	sqlVdbeAddOp2(v, OP_Integer, effective_user()->uid,
			  iFirstCol + 1 /* owner */ );
	sqlVdbeAddOp2(v, OP_SCopy, space_name_reg, iFirstCol + 2);
	sqlVdbeAddOp4(v, OP_String8, 0, iFirstCol + 3 /* engine */ , 0,
			  sqlDbStrDup(pParse->db, space->def->engine_name),
			  P4_DYNAMIC);
	sqlVdbeAddOp2(v, OP_Integer, space->def->field_count,
			  iFirstCol + 4 /* field_count */ );
	sqlVdbeAddOp4(v, OP_Blob, table_opts_stmt_sz, iFirstCol + 5,
			  SQL_SUBTYPE_MSGPACK, table_opts_stmt, P4_DYNAMIC);
	/* zOpts and zFormat are co-located, hence STATIC */
	sqlVdbeAddOp4(v, OP_Blob, table_stmt_sz, iFirstCol + 6,
			  SQL_SUBTYPE_MSGPACK, table_stmt, P4_STATIC);
	sqlVdbeAddOp3(v, OP_MakeRecord, iFirstCol, 7, tuple_reg);
	sqlVdbeAddOp2(v, OP_SInsert, BOX_SPACE_ID, tuple_reg);
	sqlVdbeChangeP5(v, OPFLAG_NCHANGE);
	return;
error:
	pParse->is_aborted = true;
}

int
emitNewSysSequenceRecord(Parse *pParse, int reg_seq_id, const char *seq_name)
{
	Vdbe *v = sqlGetVdbe(pParse);
	sql *db = pParse->db;
	int first_col = pParse->nMem + 1;
	pParse->nMem += 10; /* 9 fields + new record pointer  */

	const long long int min_usigned_long_long = 0;
	const long long int max_usigned_long_long = LLONG_MAX;

	/* 1. New sequence id  */
	sqlVdbeAddOp2(v, OP_SCopy, reg_seq_id, first_col + 1);
	/* 2. user is  */
	sqlVdbeAddOp2(v, OP_Integer, effective_user()->uid, first_col + 2);
	/* 3. New sequence name  */
        sqlVdbeAddOp4(v, OP_String8, 0, first_col + 3, 0,
			  sqlDbStrDup(pParse->db, seq_name), P4_DYNAMIC);

	/* 4. Step  */
	sqlVdbeAddOp2(v, OP_Integer, 1, first_col + 4);

	/* 5. Minimum  */
	sqlVdbeAddOp4Dup8(v, OP_Int64, 0, first_col + 5, 0,
			  (unsigned char *) &min_usigned_long_long, P4_UINT64);
	/* 6. Maximum  */
	sqlVdbeAddOp4Dup8(v, OP_Int64, 0, first_col + 6, 0,
			  (unsigned char *) &max_usigned_long_long, P4_UINT64);
	/* 7. Start  */
	sqlVdbeAddOp2(v, OP_Integer, 1, first_col + 7);

	/* 8. Cache  */
	sqlVdbeAddOp2(v, OP_Integer, 0, first_col + 8);

	/* 9. Cycle  */
	sqlVdbeAddOp2(v, OP_Bool, false, first_col + 9);

	sqlVdbeAddOp3(v, OP_MakeRecord, first_col + 1, 9, first_col);

	if (db->mallocFailed)
		return -1;
	else
		return first_col;
}

static int
emitNewSysSpaceSequenceRecord(Parse *pParse, int reg_space_id, int reg_seq_id)
{
	uint32_t fieldno = pParse->autoinc_fieldno;

	Vdbe *v = sqlGetVdbe(pParse);
	int first_col = pParse->nMem + 1;
	pParse->nMem += 6; /* 5 fields + new record pointer  */

	/* 1. Space id  */
	sqlVdbeAddOp2(v, OP_SCopy, reg_space_id, first_col + 1);
	
	/* 2. Sequence id  */
	sqlVdbeAddOp2(v, OP_SCopy, reg_seq_id, first_col + 2);

	/* 3. Autogenerated. */
	sqlVdbeAddOp2(v, OP_Bool, true, first_col + 3);

	/* 4. Field id. */
	sqlVdbeAddOp2(v, OP_Integer, fieldno, first_col + 4);

	/* 5. Field path. */
	sqlVdbeAddOp4(v, OP_String8, 0, first_col + 5, 0, "", P4_STATIC);

	sqlVdbeAddOp3(v, OP_MakeRecord, first_col + 1, 5, first_col);
	return first_col;
}

/**
 * Generate opcodes to serialize check constraint definition into
 * MsgPack and insert produced tuple into _ck_constraint space.
 * @param parser Parsing context.
 * @param ck_def Check constraint definition to be serialized.
 * @param reg_space_id The VDBE register containing space id.
 * @param space_name Name of the space owning the CHECK. For error
 *     message.
 */
static void
vdbe_emit_ck_constraint_create(struct Parse *parser,
			       const struct ck_constraint_def *ck_def,
			       uint32_t reg_space_id, const char *space_name)
{
	struct sql *db = parser->db;
	struct Vdbe *v = sqlGetVdbe(parser);
	assert(v != NULL);
	/*
	 * Occupy registers for 5 fields: each member in
	 * _ck_constraint space plus one for final msgpack tuple.
	 */
	int ck_constraint_reg = sqlGetTempRange(parser, 7);
	sqlVdbeAddOp2(v, OP_SCopy, reg_space_id, ck_constraint_reg);
	sqlVdbeAddOp4(v, OP_String8, 0, ck_constraint_reg + 1, 0,
		      sqlDbStrDup(db, ck_def->name), P4_DYNAMIC);
	sqlVdbeAddOp2(v, OP_Bool, false, ck_constraint_reg + 2);
	sqlVdbeAddOp4(v, OP_String8, 0, ck_constraint_reg + 3, 0,
		      ck_constraint_language_strs[ck_def->language], P4_STATIC);
	sqlVdbeAddOp4(v, OP_String8, 0, ck_constraint_reg + 4, 0,
		      sqlDbStrDup(db, ck_def->expr_str), P4_DYNAMIC);
	sqlVdbeAddOp2(v, OP_Bool, true, ck_constraint_reg + 5);
	sqlVdbeAddOp3(v, OP_MakeRecord, ck_constraint_reg, 6,
		      ck_constraint_reg + 6);
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_CONSTRAINT_EXISTS),
			   constraint_type_strs[CONSTRAINT_TYPE_CK],
			   ck_def->name, space_name);
	if (vdbe_emit_halt_with_presence_test(parser, BOX_CK_CONSTRAINT_ID, 0,
					      ck_constraint_reg, 2,
					      ER_CONSTRAINT_EXISTS, error_msg,
					      false, OP_NoConflict) != 0)
		return;
	sqlVdbeAddOp2(v, OP_SInsert, BOX_CK_CONSTRAINT_ID,
		      ck_constraint_reg + 6);
	VdbeComment((v, "Create CK constraint %s", ck_def->name));
	sqlReleaseTempRange(parser, ck_constraint_reg, 7);
}

/**
 * Generate opcodes to serialize foreign key into MsgPack and
 * insert produced tuple into _fk_constraint space.
 *
 * @param parse_context Parsing context.
 * @param fk Foreign key to be created.
 * @param space_name Name of the space owning the FOREIGN KEY. For
 *     error message.
 */
static void
vdbe_emit_fk_constraint_create(struct Parse *parse_context,
			       const struct fk_constraint_def *fk,
			       const char *space_name)
{
	assert(parse_context != NULL);
	assert(fk != NULL);
	struct Vdbe *vdbe = sqlGetVdbe(parse_context);
	assert(vdbe != NULL);
	/*
	 * Occupy registers for 9 fields: each member in
	 * _fk_constraint space plus one for final msgpack tuple.
	 */
	int constr_tuple_reg = sqlGetTempRange(parse_context, 10);
	char *name_copy = sqlDbStrDup(parse_context->db, fk->name);
	if (name_copy == NULL)
		return;
	sqlVdbeAddOp4(vdbe, OP_String8, 0, constr_tuple_reg, 0, name_copy,
			  P4_DYNAMIC);
	/*
	 * In case we are adding FK constraints during execution
	 * of <CREATE TABLE ...> or <ALTER TABLE ADD COLUMN>
	 * statement, we don't have child id (we know it, but
	 * fk->child_id stores register because of code reuse in
	 * vdbe_emit_create_constraints()), but we know register
	 * where it will be stored.
	 */
	bool is_alter_add_constr =
		parse_context->create_table_def.new_space == NULL &&
		parse_context->create_column_def.space == NULL;
	if (!is_alter_add_constr) {
		sqlVdbeAddOp2(vdbe, OP_SCopy, fk->child_id,
				  constr_tuple_reg + 1);
	} else {
		sqlVdbeAddOp2(vdbe, OP_Integer, fk->child_id,
				  constr_tuple_reg + 1);
	}
	if (!is_alter_add_constr && fk_constraint_is_self_referenced(fk)) {
		sqlVdbeAddOp2(vdbe, OP_SCopy, fk->parent_id,
				  constr_tuple_reg + 2);
	} else {
		sqlVdbeAddOp2(vdbe, OP_Integer, fk->parent_id,
				  constr_tuple_reg + 2);
	}
	/*
	 * Lets check that constraint with this name hasn't
	 * been created before.
	 */
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_CONSTRAINT_EXISTS),
			   constraint_type_strs[CONSTRAINT_TYPE_FK], name_copy,
			   space_name);
	if (vdbe_emit_halt_with_presence_test(parse_context,
					      BOX_FK_CONSTRAINT_ID, 0,
					      constr_tuple_reg, 2,
					      ER_CONSTRAINT_EXISTS, error_msg,
					      false, OP_NoConflict) != 0)
		return;
	sqlVdbeAddOp2(vdbe, OP_Bool, fk->is_deferred, constr_tuple_reg + 3);
	sqlVdbeAddOp4(vdbe, OP_String8, 0, constr_tuple_reg + 4, 0,
			  fk_constraint_match_strs[fk->match], P4_STATIC);
	sqlVdbeAddOp4(vdbe, OP_String8, 0, constr_tuple_reg + 5, 0,
			  fk_constraint_action_strs[fk->on_delete], P4_STATIC);
	sqlVdbeAddOp4(vdbe, OP_String8, 0, constr_tuple_reg + 6, 0,
			  fk_constraint_action_strs[fk->on_update], P4_STATIC);
	struct region *region = &parse_context->region;
	uint32_t parent_links_size = 0;
	char *parent_links = fk_constraint_encode_links(region, fk, FIELD_LINK_PARENT,
					       &parent_links_size);
	if (parent_links == NULL)
		goto error;
	uint32_t child_links_size = 0;
	char *child_links = fk_constraint_encode_links(region, fk, FIELD_LINK_CHILD,
					      &child_links_size);
	if (child_links == NULL)
		goto error;
	/*
	 * We are allocating memory for both parent and child
	 * arrays in the same chunk. Thus, first OP_Blob opcode
	 * interprets it as static memory, and the second one -
	 * as dynamic and releases memory.
	 */
	char *raw = sqlDbMallocRaw(parse_context->db,
				       parent_links_size + child_links_size);
	if (raw == NULL)
		return;
	memcpy(raw, parent_links, parent_links_size);
	parent_links = raw;
	raw += parent_links_size;
	memcpy(raw, child_links, child_links_size);
	child_links = raw;

	sqlVdbeAddOp4(vdbe, OP_Blob, child_links_size, constr_tuple_reg + 7,
			  SQL_SUBTYPE_MSGPACK, child_links, P4_STATIC);
	sqlVdbeAddOp4(vdbe, OP_Blob, parent_links_size,
			  constr_tuple_reg + 8, SQL_SUBTYPE_MSGPACK,
			  parent_links, P4_DYNAMIC);
	sqlVdbeAddOp3(vdbe, OP_MakeRecord, constr_tuple_reg, 9,
			  constr_tuple_reg + 9);
	sqlVdbeAddOp2(vdbe, OP_SInsert, BOX_FK_CONSTRAINT_ID,
		      constr_tuple_reg + 9);
	if (is_alter_add_constr) {
		sqlVdbeCountChanges(vdbe);
		sqlVdbeChangeP5(vdbe, OPFLAG_NCHANGE);
	}
	sqlReleaseTempRange(parse_context, constr_tuple_reg, 10);
	return;
error:
	parse_context->is_aborted = true;
}

/**
 * Find fieldno by name.
 * @param parse_context Parser. Used for error reporting.
 * @param def Space definition to search field in.
 * @param field_name Field name to search by.
 * @param[out] link Result fieldno.
 * @param fk_name FK name. Used for error reporting.
 *
 * @retval 0 Success.
 * @retval -1 Error - field is not found.
 */
static int
resolve_link(struct Parse *parse_context, const struct space_def *def,
	     const char *field_name, uint32_t *link, const char *fk_name)
{
	assert(link != NULL);
	for (uint32_t j = 0; j < def->field_count; ++j) {
		if (strcmp(field_name, def->fields[j].name) == 0) {
			*link = j;
			return 0;
		}
	}
	diag_set(ClientError, ER_CREATE_FK_CONSTRAINT, fk_name,
		 tt_sprintf("unknown column %s in foreign key definition",
			    field_name));
	parse_context->is_aborted = true;
	return -1;
}

/**
 * Emit code to create sequences, indexes, check and foreign key
 * constraints appeared in <CREATE TABLE> or
 * <ALTER TABLE ADD COLUMN>.
 */
static void
vdbe_emit_create_constraints(struct Parse *parse, int reg_space_id)
{
	assert(reg_space_id != 0);
	struct space *space = parse->create_table_def.new_space;
	bool is_alter = space == NULL;
	uint32_t i = 0;
	/*
	 * If it is an <ALTER TABLE ADD COLUMN>, then we have to
	 * create all indexes added by this statement. These
	 * indexes are in the array, starting with old index_count
	 * (inside space object) and ending with new index_count
	 * (inside ephemeral space).
	 */
	if (is_alter) {
		space = parse->create_column_def.space;
		i = space_by_name(space->def->name)->index_count;
	}
	assert(space != NULL);
	for (; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		vdbe_emit_create_index(parse, space->def, idx->def,
				       reg_space_id, idx->def->iid);
	}

	/*
	 * Check to see if we need to create an _sequence table
	 * for keeping track of autoincrement keys.
	 */
	if (parse->has_autoinc) {
		/* Do an insertion into _sequence. */
		int reg_seq_id = ++parse->nMem;
		struct Vdbe *v = sqlGetVdbe(parse);
		assert(v != NULL);
		sqlVdbeAddOp2(v, OP_NextSequenceId, 0, reg_seq_id);
		int reg_seq_rec = emitNewSysSequenceRecord(parse, reg_seq_id,
							   space->def->name);
		if (is_alter) {
			int errcode = ER_SQL_CANT_ADD_AUTOINC;
			const char *error_msg =
				tt_sprintf(tnt_errcode_desc(errcode),
					   space->def->name);
			if (vdbe_emit_halt_with_presence_test(parse,
							      BOX_SEQUENCE_ID,
							      2,
							      reg_seq_rec + 3,
							      1, errcode,
							      error_msg, false,
							      OP_NoConflict) != 0)
				return;
		}
		sqlVdbeAddOp2(v, OP_SInsert, BOX_SEQUENCE_ID, reg_seq_rec);
		/* Do an insertion into _space_sequence. */
		int reg_space_seq_record =
			emitNewSysSpaceSequenceRecord(parse, reg_space_id,
						      reg_seq_id);
		sqlVdbeAddOp2(v, OP_SInsert, BOX_SPACE_SEQUENCE_ID,
			      reg_space_seq_record);
	}

	/* Code creation of FK constraints, if any. */
	struct fk_constraint_parse *fk_parse;
	rlist_foreach_entry(fk_parse,
			    &parse->create_fk_constraint_parse_def.fkeys,
			    link) {
		struct fk_constraint_def *fk_def = fk_parse->fk_def;
		if (fk_parse->selfref_cols != NULL) {
			struct ExprList *cols = fk_parse->selfref_cols;
			for (uint32_t i = 0; i < fk_def->field_count; ++i) {
				if (resolve_link(parse, space->def,
						 cols->a[i].zName,
						 &fk_def->links[i].parent_field,
						 fk_def->name) != 0)
					return;
			}
			fk_def->parent_id = reg_space_id;
		} else if (fk_parse->is_self_referenced) {
			struct key_def *pk_key_def =
				sql_space_primary_key(space)->def->key_def;
			if (pk_key_def->part_count != fk_def->field_count) {
				diag_set(ClientError, ER_CREATE_FK_CONSTRAINT,
					 fk_def->name, "number of columns in "\
					 "foreign key does not match the "\
					 "number of columns in the primary "\
					 "index of referenced table");
				parse->is_aborted = true;
				return;
			}
			for (uint32_t i = 0; i < fk_def->field_count; ++i) {
				fk_def->links[i].parent_field =
					pk_key_def->parts[i].fieldno;
			}
			fk_def->parent_id = reg_space_id;
		}
		fk_def->child_id = reg_space_id;
		vdbe_emit_fk_constraint_create(parse, fk_def, space->def->name);
	}

	/* Code creation of CK constraints, if any. */
	struct ck_constraint_parse *ck_parse;
	rlist_foreach_entry(ck_parse,
			    &parse->create_ck_constraint_parse_def.checks,
			    link) {
		vdbe_emit_ck_constraint_create(parse, ck_parse->ck_def,
					       reg_space_id, space->def->name);
	}
}

/*
 * This routine is called to report the final ")" that terminates
 * a CREATE TABLE statement.
 *
 * During this routine byte code for creation of new Tarantool
 * space and all necessary Tarantool indexes is emitted.
 */
void
sqlEndTable(struct Parse *pParse)
{
	assert(!pParse->db->mallocFailed);
	struct space *new_space = pParse->create_table_def.new_space;
	if (new_space == NULL)
		return;
	assert(!pParse->db->init.busy);
	assert(!new_space->def->opts.is_view);

	if (sql_space_primary_key(new_space) == NULL) {
		diag_set(ClientError, ER_CREATE_SPACE, new_space->def->name,
			 "PRIMARY KEY missing");
		pParse->is_aborted = true;
		return;
	}

	/*
	 * Actualize conflict action for NOT NULL constraint.
	 * Set defaults for columns having no separate
	 * NULL/NOT NULL specifiers.
	 */
	struct field_def *field = new_space->def->fields;
	for (uint32_t i = 0; i < new_space->def->field_count; ++i, ++field) {
		if (field->nullable_action == ON_CONFLICT_ACTION_DEFAULT) {
			/* Set default nullability NONE. */
			field->nullable_action = ON_CONFLICT_ACTION_NONE;
			field->is_nullable = true;
		}
	}
	/*
	 * If not initializing, then create new Tarantool space.
	 */
	struct Vdbe *v = sqlGetVdbe(pParse);
	if (NEVER(v == 0))
		return;

	/*
	 * Firstly, check if space with given name already exists.
	 * In case IF NOT EXISTS clause is specified and table
	 * exists, we will silently halt VDBE execution.
	 */
	char *space_name_copy = sqlDbStrDup(pParse->db, new_space->def->name);
	if (space_name_copy == NULL)
		return;
	int name_reg = ++pParse->nMem;
	sqlVdbeAddOp4(pParse->pVdbe, OP_String8, 0, name_reg, 0,
		      space_name_copy, P4_DYNAMIC);
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_SPACE_EXISTS), space_name_copy);
	bool no_err = pParse->create_table_def.base.if_not_exist;
	if (vdbe_emit_halt_with_presence_test(pParse, BOX_SPACE_ID, 2,
					      name_reg, 1, ER_SPACE_EXISTS,
					      error_msg, (no_err != 0),
					      OP_NoConflict) != 0)
		return;

	int reg_space_id = getNewSpaceId(pParse);
	vdbe_emit_space_create(pParse, reg_space_id, name_reg, new_space);
	vdbe_emit_create_constraints(pParse, reg_space_id);
}

void
sql_create_view(struct Parse *parse_context)
{
	struct create_view_def *view_def = &parse_context->create_view_def;
	struct create_entity_def *create_entity_def = &view_def->base;
	struct alter_entity_def *alter_entity_def = &create_entity_def->base;
	assert(alter_entity_def->entity_type == ENTITY_TYPE_VIEW);
	assert(alter_entity_def->alter_action == ALTER_ACTION_CREATE);
	(void) alter_entity_def;
	struct sql *db = parse_context->db;
	if (parse_context->nVar > 0) {
		diag_set(ClientError, ER_CREATE_SPACE,
			 sql_name_from_token(db, &create_entity_def->name),
			 "parameters are not allowed in views");
		parse_context->is_aborted = true;
		goto create_view_fail;
	}
	struct space *space = sqlStartTable(parse_context,
					    &create_entity_def->name);
	if (space == NULL || parse_context->is_aborted)
		goto create_view_fail;
	struct space *select_res_space =
		sqlResultSetOfSelect(parse_context, view_def->select);
	if (select_res_space == NULL)
		goto create_view_fail;
	struct ExprList *aliases = view_def->aliases;
	if (aliases != NULL) {
		if ((int)select_res_space->def->field_count != aliases->nExpr) {
			diag_set(ClientError, ER_CREATE_SPACE, space->def->name,
				 "number of aliases doesn't match provided "\
				 "columns");
			parse_context->is_aborted = true;
			goto create_view_fail;
		}
		sqlColumnsFromExprList(parse_context, aliases, space->def);
		sqlSelectAddColumnTypeAndCollation(parse_context, space->def,
						   view_def->select);
	} else {
		assert(select_res_space->def->opts.is_ephemeral);
		space->def->fields = select_res_space->def->fields;
		space->def->field_count = select_res_space->def->field_count;
		select_res_space->def->fields = NULL;
		select_res_space->def->field_count = 0;
	}
	space->def->opts.is_view = true;
	/*
	 * Locate the end of the CREATE VIEW statement.
	 * Make sEnd point to the end.
	 */
	struct Token end = parse_context->sLastToken;
	assert(end.z[0] != 0);
	if (end.z[0] != ';')
		end.z += end.n;
	end.n = 0;
	struct Token *begin = view_def->create_start;
	int n = end.z - begin->z;
	assert(n > 0);
	const char *z = begin->z;
	while (sqlIsspace(z[n - 1]))
		n--;
	end.z = &z[n - 1];
	end.n = 1;
	space->def->opts.sql = strndup(begin->z, n);
	if (space->def->opts.sql == NULL) {
		diag_set(OutOfMemory, n, "strndup", "opts.sql");
		parse_context->is_aborted = true;
		goto create_view_fail;
	}
	const char *space_name =
		sql_name_from_token(db, &create_entity_def->name);
	char *name_copy = sqlDbStrDup(db, space_name);
	if (name_copy == NULL)
		goto create_view_fail;
	int name_reg = ++parse_context->nMem;
	sqlVdbeAddOp4(parse_context->pVdbe, OP_String8, 0, name_reg, 0, name_copy,
		      P4_DYNAMIC);
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_SPACE_EXISTS), space_name);
	bool no_err = create_entity_def->if_not_exist;
	if (vdbe_emit_halt_with_presence_test(parse_context, BOX_SPACE_ID, 2,
					      name_reg, 1, ER_SPACE_EXISTS,
					      error_msg, (no_err != 0),
					      OP_NoConflict) != 0)
		goto create_view_fail;

	vdbe_emit_space_create(parse_context, getNewSpaceId(parse_context),
			       name_reg, space);

 create_view_fail:
	sql_expr_list_delete(db, view_def->aliases);
	sql_select_delete(db, view_def->select);
	return;
}

int
sql_view_assign_cursors(struct Parse *parse, const char *view_stmt)
{
	assert(view_stmt != NULL);
	struct sql *db = parse->db;
	struct Select *select = sql_view_compile(db, view_stmt);
	if (select == NULL)
		return -1;
	sqlSrcListAssignCursors(parse, select->pSrc);
	sql_select_delete(db, select);
	return 0;
}

void
sql_store_select(struct Parse *parse_context, struct Select *select)
{
	Select *select_copy = sqlSelectDup(parse_context->db, select, 0);
	parse_context->parsed_ast.ast_type = AST_TYPE_SELECT;
	parse_context->parsed_ast.select = select_copy;
}

/**
 * Create expression record "@col_name = '@col_value'".
 *
 * @param parse The parsing context.
 * @param col_name Name of column.
 * @param col_value Name of row.
 * @retval not NULL on success.
 * @retval NULL on failure.
 */
static struct Expr *
sql_id_eq_str_expr(struct Parse *parse, const char *col_name,
		   const char *col_value)
{
	struct sql *db = parse->db;
	struct Expr *col_name_expr = sql_expr_new_named(db, TK_ID, col_name);
	if (col_name_expr == NULL) {
		parse->is_aborted = true;
		return NULL;
	}
	struct Expr *col_value_expr =
		sql_expr_new_named(db, TK_STRING, col_value);
	if (col_value_expr == NULL) {
		sql_expr_delete(db, col_name_expr, false);
		parse->is_aborted = true;
		return NULL;
	}
	return sqlPExpr(parse, TK_EQ, col_name_expr, col_value_expr);
}

void
vdbe_emit_stat_space_clear(struct Parse *parse, const char *stat_table_name,
			   const char *idx_name, const char *table_name)
{
	assert(idx_name != NULL || table_name != NULL);
	struct sql *db = parse->db;
	assert(!db->mallocFailed);
	struct SrcList *src_list = sql_src_list_new(db);
	if (src_list == NULL) {
		parse->is_aborted = true;
		return;
	}
	src_list->a[0].zName = sqlDbStrDup(db, stat_table_name);
	struct Expr *expr, *where = NULL;
	if (idx_name != NULL) {
		expr = sql_id_eq_str_expr(parse, "idx", idx_name);
		where = sql_and_expr_new(db, expr, where);
	}
	if (table_name != NULL) {
		expr = sql_id_eq_str_expr(parse, "tbl", table_name);
		where = sql_and_expr_new(db, expr, where);
	}
	if (where == NULL)
		parse->is_aborted = true;
	/**
	 * On memory allocation error sql_table delete_from
	 * releases memory for its own.
	 */
	sql_table_delete_from(parse, src_list, where);
}

/**
 * Generate VDBE program to remove entry from _index space.
 *
 * @param parse_context Parsing context.
 * @param name Index name.
 * @param space_def Def of table which index belongs to.
 * @param errcode Type of printing error: "no such index" or
 *                "no such constraint".
 * @param if_exist True if there was <IF EXISTS> in the query.
 */
static void
vdbe_emit_index_drop(struct Parse *parse_context, const char *name,
		     struct space_def *space_def, int errcode, bool if_exist)
{
	assert(errcode == ER_NO_SUCH_INDEX_NAME ||
	       errcode == ER_NO_SUCH_CONSTRAINT);
	struct Vdbe *vdbe = sqlGetVdbe(parse_context);
	assert(vdbe != NULL);
	assert(parse_context->db != NULL);
	int key_reg = sqlGetTempRange(parse_context, 3);
	sqlVdbeAddOp2(vdbe, OP_Integer, space_def->id, key_reg);
	sqlVdbeAddOp4(vdbe, OP_String8, 0, key_reg + 1, 0,
		      sqlDbStrDup(parse_context->db, name), P4_DYNAMIC);
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(errcode), name, space_def->name);
	if (vdbe_emit_halt_with_presence_test(parse_context, BOX_INDEX_ID, 2,
					      key_reg, 2, errcode, error_msg,
					      if_exist, OP_Found) != 0)
		return;
	sqlVdbeAddOp3(vdbe, OP_MakeRecord, key_reg, 2, key_reg + 2);
	sqlVdbeAddOp3(vdbe, OP_SDelete, BOX_INDEX_ID, key_reg + 2, 2);
	sqlReleaseTempRange(parse_context, key_reg, 3);
}

/**
 * Generate VDBE program to remove entry from _fk_constraint space.
 *
 * @param parse_context Parsing context.
 * @param constraint_name Name of FK constraint to be dropped.
 * @param child_def Def of table which constraint belongs to.
 */
static void
vdbe_emit_fk_constraint_drop(struct Parse *parse_context,
			     const char *constraint_name,
			     struct space_def *child_def)
{
	struct Vdbe *vdbe = sqlGetVdbe(parse_context);
	assert(vdbe != NULL);
	int key_reg = sqlGetTempRange(parse_context, 3);
	const char *name_copy = sqlDbStrDup(parse_context->db, constraint_name);
	sqlVdbeAddOp4(vdbe, OP_String8, 0, key_reg, 0, name_copy,
			  P4_DYNAMIC);
	sqlVdbeAddOp2(vdbe, OP_Integer, child_def->id, key_reg + 1);
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_NO_SUCH_CONSTRAINT),
			   constraint_name, child_def->name);
	if (vdbe_emit_halt_with_presence_test(parse_context,
					      BOX_FK_CONSTRAINT_ID, 0,
					      key_reg, 2, ER_NO_SUCH_CONSTRAINT,
					      error_msg, false,
					      OP_Found) != 0)
		return;
	sqlVdbeAddOp3(vdbe, OP_MakeRecord, key_reg, 2, key_reg + 2);
	sqlVdbeAddOp2(vdbe, OP_SDelete, BOX_FK_CONSTRAINT_ID, key_reg + 2);
	VdbeComment((vdbe, "Delete FK constraint %s", constraint_name));
	sqlReleaseTempRange(parse_context, key_reg, 3);
}

/**
 * Generate VDBE program to remove entry from _ck_constraint space.
 *
 * @param parser Parsing context.
 * @param ck_name Name of CK constraint to be dropped.
 * @param space_def Def of table which constraint belongs to.
 */
static void
vdbe_emit_ck_constraint_drop(struct Parse *parser, const char *ck_name,
			     struct space_def *space_def)
{
	struct Vdbe *v = sqlGetVdbe(parser);
	struct sql *db = v->db;
	assert(v != NULL);
	int key_reg = sqlGetTempRange(parser, 3);
	sqlVdbeAddOp2(v, OP_Integer, space_def->id, key_reg);
	sqlVdbeAddOp4(v, OP_String8, 0, key_reg + 1, 0,
		      sqlDbStrDup(db, ck_name), P4_DYNAMIC);
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_NO_SUCH_CONSTRAINT), ck_name,
			   space_def->name);
	if (vdbe_emit_halt_with_presence_test(parser, BOX_CK_CONSTRAINT_ID, 0,
					      key_reg, 2, ER_NO_SUCH_CONSTRAINT,
					      error_msg, false, OP_Found) != 0)
		return;
	sqlVdbeAddOp3(v, OP_MakeRecord, key_reg, 2, key_reg + 2);
	sqlVdbeAddOp2(v, OP_SDelete, BOX_CK_CONSTRAINT_ID, key_reg + 2);
	VdbeComment((v, "Delete CK constraint %s", ck_name));
	sqlReleaseTempRange(parser, key_reg, 3);
}

/**
 * Generate VDBE program to revoke all
 * privileges associated with the given object.
 *
 * @param parser Parsing context.
 * @param object_type Object type.
 * @param object_id Object id.
 * @param access Access array associated with an object.
 */
static void
vdbe_emit_revoke_object(struct Parse *parser, const char *object_type,
			uint32_t object_id, struct access *access)
{
	struct Vdbe *v = sqlGetVdbe(parser);
	assert(v != NULL);
	/*
	 * Get uid of users through access array
	 * and generate code to delete corresponding
	 * entries from _priv.
	 */
	int key_reg = sqlGetTempRange(parser, 4);
	bool had_grants = false;
	for (uint8_t token = 0; token < BOX_USER_MAX; ++token) {
		if (!access[token].granted)
			continue;
		had_grants = true;
		const struct user *user = user_find_by_token(token);
		sqlVdbeAddOp2(v, OP_Integer, user->def->uid, key_reg);
		sqlVdbeAddOp4(v, OP_String8, 0, key_reg + 1, 0,
			      object_type, P4_STATIC);
		sqlVdbeAddOp2(v, OP_Integer, object_id, key_reg + 2);
		sqlVdbeAddOp3(v, OP_MakeRecord, key_reg, 3, key_reg + 3);
		sqlVdbeAddOp2(v, OP_SDelete, BOX_PRIV_ID, key_reg + 3);
	}
	if (had_grants)
		VdbeComment((v, "Remove %s grants", object_type));
	sqlReleaseTempRange(parser, key_reg, 4);
}

/**
 * Generate code to drop a table.
 * This routine includes dropping triggers, sequences,
 * all indexes and entry from _space space.
 *
 * @param parse_context Current parsing context.
 * @param space Space to be dropped.
 * @param is_view True, if space is
 */
static void
sql_code_drop_table(struct Parse *parse_context, struct space *space,
		    bool is_view)
{
	struct Vdbe *v = sqlGetVdbe(parse_context);
	assert(v != NULL);
	/*
	 * Remove all grants associated with
	 * the table being dropped.
	 */
	vdbe_emit_revoke_object(parse_context, "space", space->def->id,
				space->access);
	/*
	 * Drop all triggers associated with the table being
	 * dropped. Code is generated to remove entries from
	 * _trigger. on_replace_dd_trigger will remove it from
	 * internal SQL structures.
	 *
	 * Do not account triggers deletion - they will be
	 * accounted in DELETE from _space below.
	 */
	struct sql_trigger *trigger = space->sql_triggers;
	while (trigger != NULL) {
		vdbe_code_drop_trigger(parse_context, trigger->zName, false);
		trigger = trigger->next;
	}
	/*
	 * Remove any entries from the _sequence_data, _sequence
	 * and _space_sequence spaces associated with the table
	 * being dropped. This is done before the table is dropped
	 * from internal schema.
	 */
	int idx_rec_reg = ++parse_context->nMem;
	int space_id_reg = ++parse_context->nMem;
	int index_id_reg = ++parse_context->nMem;
	int space_id = space->def->id;
	sqlVdbeAddOp2(v, OP_Integer, space_id, space_id_reg);
	sqlVdbeAddOp1(v, OP_CheckViewReferences, space_id_reg);
	if (space->sequence != NULL) {
		/* Delete entry from _space_sequence. */
		sqlVdbeAddOp3(v, OP_MakeRecord, space_id_reg, 1,
				  idx_rec_reg);
		sqlVdbeAddOp2(v, OP_SDelete, BOX_SPACE_SEQUENCE_ID,
				  idx_rec_reg);
		VdbeComment((v, "Delete entry from _space_sequence"));
		if (space->sequence->is_generated) {
			/* Delete entry from _sequence_data. */
			int sequence_id_reg = ++parse_context->nMem;
			sqlVdbeAddOp2(v, OP_Integer, space->sequence->def->id,
				      sequence_id_reg);
			sqlVdbeAddOp3(v, OP_MakeRecord, sequence_id_reg, 1,
				      idx_rec_reg);
			sqlVdbeAddOp2(v, OP_SDelete, BOX_SEQUENCE_DATA_ID,
				      idx_rec_reg);
			VdbeComment((v, "Delete entry from _sequence_data"));
			/* Delete entries from _priv */
		        vdbe_emit_revoke_object(parse_context, "sequence",
						space->sequence->def->id,
						space->sequence->access);
			/* Delete entry by id from _sequence. */
			sqlVdbeAddOp3(v, OP_MakeRecord, sequence_id_reg, 1,
				      idx_rec_reg);
			sqlVdbeAddOp2(v, OP_SDelete, BOX_SEQUENCE_ID,
				      idx_rec_reg);
			VdbeComment((v, "Delete entry from _sequence"));
		}
	}
	/* Delete all child FK constraints. */
	struct fk_constraint *child_fk;
	rlist_foreach_entry(child_fk, &space->child_fk_constraint,
			    in_child_space) {
		vdbe_emit_fk_constraint_drop(parse_context, child_fk->def->name,
					     space->def);
	}
	/* Delete all CK constraints. */
	struct ck_constraint *ck_constraint;
	rlist_foreach_entry(ck_constraint, &space->ck_constraint, link) {
		vdbe_emit_ck_constraint_drop(parse_context,
					     ck_constraint->def->name,
					     space->def);
	}
	/*
	 * Drop all _space and _index entries that refer to the
	 * table.
	 */
	if (!is_view) {
		uint32_t index_count = space->index_count;
		if (index_count > 1) {
			/*
			 * Remove all indexes, except for primary.
			 * Tarantool won't allow remove primary when
			 * secondary exist.
			 */
			for (uint32_t i = 1; i < index_count; ++i) {
				sqlVdbeAddOp2(v, OP_Integer,
						  space->index[i]->def->iid,
						  index_id_reg);
				sqlVdbeAddOp3(v, OP_MakeRecord,
						  space_id_reg, 2, idx_rec_reg);
				sqlVdbeAddOp2(v, OP_SDelete, BOX_INDEX_ID,
						  idx_rec_reg);
				VdbeComment((v,
					     "Remove secondary index iid = %u",
					     space->index[i]->def->iid));
			}
		}
		sqlVdbeAddOp2(v, OP_Integer, 0, index_id_reg);
		sqlVdbeAddOp3(v, OP_MakeRecord, space_id_reg, 2,
				  idx_rec_reg);
		sqlVdbeAddOp2(v, OP_SDelete, BOX_INDEX_ID, idx_rec_reg);
		VdbeComment((v, "Remove primary index"));
	}
	/* Delete records about the space from the _truncate. */
	sqlVdbeAddOp3(v, OP_MakeRecord, space_id_reg, 1, idx_rec_reg);
	sqlVdbeAddOp2(v, OP_SDelete, BOX_TRUNCATE_ID, idx_rec_reg);
	VdbeComment((v, "Delete entry from _truncate"));
	/* Eventually delete entry from _space. */
	sqlVdbeAddOp3(v, OP_MakeRecord, space_id_reg, 1, idx_rec_reg);
	sqlVdbeAddOp2(v, OP_SDelete, BOX_SPACE_ID, idx_rec_reg);
	sqlVdbeChangeP5(v, OPFLAG_NCHANGE);
	VdbeComment((v, "Delete entry from _space"));
}

/**
 * This routine is called to do the work of a DROP TABLE and
 * DROP VIEW statements.
 *
 * @param parse_context Current parsing context.
 */
void
sql_drop_table(struct Parse *parse_context)
{
	struct drop_entity_def drop_def = parse_context->drop_table_def.base;
	assert(drop_def.base.alter_action == ALTER_ACTION_DROP);
	struct SrcList *table_name_list = drop_def.base.entity_name;
	struct Vdbe *v = sqlGetVdbe(parse_context);
	struct sql *db = parse_context->db;
	bool is_view = drop_def.base.entity_type == ENTITY_TYPE_VIEW;
	assert(is_view || drop_def.base.entity_type == ENTITY_TYPE_TABLE);
	if (v == NULL || db->mallocFailed) {
		goto exit_drop_table;
	}
	sqlVdbeCountChanges(v);
	assert(!parse_context->is_aborted);
	assert(table_name_list->nSrc == 1);
	const char *space_name = table_name_list->a[0].zName;
	struct space *space = space_by_name(space_name);
	if (space == NULL) {
		if (!drop_def.if_exist) {
			diag_set(ClientError, ER_NO_SUCH_SPACE, space_name);
			parse_context->is_aborted = true;
		}
		goto exit_drop_table;
	}
	/*
	 * Ensure DROP TABLE is not used on a view,
	 * and DROP VIEW is not used on a table.
	 */
	if (is_view && !space->def->opts.is_view) {
		diag_set(ClientError, ER_DROP_SPACE, space_name,
			 "use DROP TABLE");
		parse_context->is_aborted = true;
		goto exit_drop_table;
	}
	if (!is_view && space->def->opts.is_view) {
		diag_set(ClientError, ER_DROP_SPACE, space_name,
			 "use DROP VIEW");
		parse_context->is_aborted = true;
		goto exit_drop_table;
	}
	/*
	 * Generate code to remove the table from Tarantool
	 * and internal SQL tables. Basically, it consists
	 * from 2 stages:
	 * 1. In case of presence of FK constraints, i.e. current
	 *    table is child or parent, then start new transaction
	 *    and erase from table all data row by row. On each
	 *    deletion check whether any FK violations have
	 *    occurred. If ones take place, then rollback
	 *    transaction and halt VDBE.
	 * 2. Drop table by truncating (if step 1 was skipped),
	 *    removing indexes from _index space and eventually
	 *    tuple with corresponding space_id from _space.
	 */
	struct fk_constraint *fk;
	rlist_foreach_entry(fk, &space->parent_fk_constraint, in_parent_space) {

		if (! fk_constraint_is_self_referenced(fk->def)) {
			diag_set(ClientError, ER_DROP_SPACE, space_name,
				 "other objects depend on it");
			parse_context->is_aborted = true;
			goto exit_drop_table;
		}
	}
	sql_code_drop_table(parse_context, space, is_view);

 exit_drop_table:
	sqlSrcListDelete(db, table_name_list);
}

/**
 * Return ordinal number of column by name. In case of error,
 * set error message.
 *
 * @param parse_context Parsing context.
 * @param space Space which column belongs to.
 * @param column_name Name of column to investigate.
 * @param[out] colno Found name of column.
 * @param fk_name Name of FK constraint to be created.
 *
 * @retval 0 on success, -1 on fault.
 */
static int
columnno_by_name(struct Parse *parse_context, const struct space *space,
		 const char *column_name, uint32_t *colno, const char *fk_name)
{
	assert(colno != NULL);
	uint32_t column_len = strlen(column_name);
	if (tuple_fieldno_by_name(space->def->dict, column_name, column_len,
				  field_name_hash(column_name, column_len),
				  colno) != 0) {
		diag_set(ClientError, ER_CREATE_FK_CONSTRAINT, fk_name,
			 tt_sprintf("foreign key refers to nonexistent field %s",
				    column_name));
		parse_context->is_aborted = true;
		return -1;
	}
	return 0;
}

void
sql_create_foreign_key(struct Parse *parse_context)
{
	struct sql *db = parse_context->db;
	struct create_fk_def *create_fk_def = &parse_context->create_fk_def;
	struct create_constraint_def *create_constr_def = &create_fk_def->base;
	struct create_entity_def *create_def = &create_constr_def->base;
	struct alter_entity_def *alter_def = &create_def->base;
	assert(alter_def->entity_type == ENTITY_TYPE_FK);
	assert(alter_def->alter_action == ALTER_ACTION_CREATE);
	/*
	 * When this function is called second time during
	 * <CREATE TABLE ...> statement (i.e. at VDBE runtime),
	 * don't even try to do something.
	 */
	if (db->init.busy)
		return;
	/*
	 * Beforehand initialization for correct clean-up
	 * while emergency exiting in case of error.
	 */
	char *parent_name = NULL;
	char *constraint_name = NULL;
	bool is_self_referenced = false;
	struct space *space = parse_context->create_column_def.space;
	struct create_table_def *table_def = &parse_context->create_table_def;
	if (space == NULL)
		space = table_def->new_space;
	/*
	 * Space under construction during <CREATE TABLE>
	 * processing or shallow copy of space during <ALTER TABLE
	 * ... ADD COLUMN>. NULL for <ALTER TABLE ... ADD
	 * CONSTRAINT> statement handling.
	 */
	bool is_alter_add_constr = space == NULL;
	uint32_t child_cols_count;
	struct ExprList *child_cols = create_fk_def->child_cols;
	if (child_cols == NULL) {
		assert(!is_alter_add_constr);
		child_cols_count = 1;
	} else {
		child_cols_count = child_cols->nExpr;
	}
	struct ExprList *parent_cols = create_fk_def->parent_cols;
	struct space *child_space = NULL;
	if (is_alter_add_constr) {
		const char *child_name = alter_def->entity_name->a[0].zName;
		child_space = space_by_name(child_name);
		if (child_space == NULL) {
			diag_set(ClientError, ER_NO_SUCH_SPACE, child_name);
			goto tnt_error;
		}
	} else {
		size_t size;
		struct fk_constraint_parse *fk_parse =
			region_alloc_object(&parse_context->region,
					    typeof(*fk_parse), &size);
		if (fk_parse == NULL) {
			diag_set(OutOfMemory, size, "region_alloc_object",
				 "fk_parse");
			goto tnt_error;
		}
		memset(fk_parse, 0, sizeof(*fk_parse));
		/*
		 * Child space already exists if it is
		 * <ALTER TABLE ADD COLUMN>.
		 */
		if (table_def->new_space == NULL)
			child_space = space;
		struct rlist *fkeys =
			&parse_context->create_fk_constraint_parse_def.fkeys;
		rlist_add_entry(fkeys, fk_parse, link);
	}
	struct Token *parent = create_fk_def->parent_name;
	assert(parent != NULL);
	parent_name = sql_name_from_token(db, parent);
	if (parent_name == NULL)
		goto tnt_error;
	/*
	 * Within ALTER TABLE ADD CONSTRAINT FK also can be
	 * self-referenced, but in this case parent (which is
	 * also child) table will definitely exist.
	 */
	is_self_referenced = !is_alter_add_constr &&
			     strcmp(parent_name, space->def->name) == 0;
	struct space *parent_space = space_by_name(parent_name);
	if (parent_space == NULL && !is_self_referenced) {
		diag_set(ClientError, ER_NO_SUCH_SPACE, parent_name);
		goto tnt_error;
	}
	if (is_self_referenced) {
		struct rlist *fkeys =
			&parse_context->create_fk_constraint_parse_def.fkeys;
		struct fk_constraint_parse *fk =
			rlist_first_entry(fkeys, struct fk_constraint_parse,
					  link);
		fk->selfref_cols = parent_cols;
		fk->is_self_referenced = true;
	}
	if (!is_alter_add_constr) {
		if (create_def->name.n == 0) {
			struct create_fk_constraint_parse_def *parse_def =
				&parse_context->create_fk_constraint_parse_def;
			uint32_t idx = ++parse_def->count;
			/*
			 * If it is <ALTER TABLE ADD COLUMN> we
			 * should count the existing FK
			 * constraints in the space and form a
			 * name based on this.
			 */
			if (table_def->new_space == NULL) {
				struct space *original_space =
					space_by_name(space->def->name);
				assert(original_space != NULL);
				struct rlist *child_fk =
					&original_space->child_fk_constraint;
				struct fk_constraint *fk;
				rlist_foreach_entry(fk, child_fk,
						    in_child_space)
					idx++;
			}
			constraint_name = sqlMPrintf(db, "fk_unnamed_%s_%u",
						     space->def->name, idx);
		} else {
			constraint_name =
				sql_name_from_token(db, &create_def->name);
			if (constraint_name == NULL)
				parse_context->is_aborted = true;
		}
	} else {
		constraint_name = sql_name_from_token(db, &create_def->name);
		if (constraint_name == NULL)
			parse_context->is_aborted = true;
	}
	if (constraint_name == NULL)
		goto exit_create_fk;
	if (!is_self_referenced && parent_space->def->opts.is_view) {
		diag_set(ClientError, ER_CREATE_FK_CONSTRAINT, constraint_name,
			"referenced space can't be VIEW");
		goto tnt_error;
	}
	const char *error_msg = "number of columns in foreign key does not "
				"match the number of columns in the primary "
				"index of referenced table";
	if (parent_cols != NULL) {
		if (parent_cols->nExpr != (int) child_cols_count) {
			diag_set(ClientError, ER_CREATE_FK_CONSTRAINT,
				 constraint_name, error_msg);
			goto tnt_error;
		}
	} else if (!is_self_referenced) {
		/*
		 * If parent columns are not specified, then PK
		 * columns of parent table are used as referenced.
		 */
		struct index *parent_pk = space_index(parent_space, 0);
		if (parent_pk == NULL) {
			diag_set(ClientError, ER_CREATE_FK_CONSTRAINT,
				 constraint_name,
				 "referenced space doesn't feature PRIMARY KEY");
			goto tnt_error;
		}
		if (parent_pk->def->key_def->part_count != child_cols_count) {
			diag_set(ClientError, ER_CREATE_FK_CONSTRAINT,
				 constraint_name, error_msg);
			goto tnt_error;
		}
	}
	int name_len = strlen(constraint_name);
	uint32_t links_offset;
	size_t fk_def_sz = fk_constraint_def_sizeof(child_cols_count, name_len,
						    &links_offset);
	struct fk_constraint_def *fk_def = (struct fk_constraint_def *)
		region_aligned_alloc(&parse_context->region, fk_def_sz,
				     alignof(*fk_def));
	if (fk_def == NULL) {
		diag_set(OutOfMemory, fk_def_sz, "region_aligned_alloc",
			 "fk_def");
		goto tnt_error;
	}
	int actions = create_fk_def->actions;
	fk_def->field_count = child_cols_count;
	fk_def->child_id = child_space != NULL ? child_space->def->id : 0;
	fk_def->parent_id = parent_space != NULL ? parent_space->def->id : 0;
	fk_def->is_deferred = create_constr_def->is_deferred;
	fk_def->match = (enum fk_constraint_match) (create_fk_def->match);
	fk_def->on_update = (enum fk_constraint_action) ((actions >> 8) & 0xff);
	fk_def->on_delete = (enum fk_constraint_action) (actions & 0xff);
	fk_def->links = (struct field_link *)((char *)fk_def + links_offset);
	/* Fill links map. */
	for (uint32_t i = 0; i < fk_def->field_count; ++i) {
		if (!is_self_referenced && parent_cols == NULL) {
			struct key_def *pk_def =
				parent_space->index[0]->def->key_def;
			fk_def->links[i].parent_field = pk_def->parts[i].fieldno;
		} else if (!is_self_referenced &&
			   columnno_by_name(parse_context, parent_space,
					    parent_cols->a[i].zName,
					    &fk_def->links[i].parent_field,
					    constraint_name) != 0) {
			goto exit_create_fk;
		}
		if (!is_alter_add_constr) {
			if (child_cols == NULL) {
				assert(i == 0);
				/*
				 * In this case there must be only
				 * one link (the last column
				 * added), so we can break
				 * immediately.
				 */
				fk_def->links[0].child_field =
					space->def->field_count - 1;
				break;
			}
			if (resolve_link(parse_context, space->def,
					 child_cols->a[i].zName,
					 &fk_def->links[i].child_field,
					 constraint_name) != 0)
				goto exit_create_fk;
		/* In case of ALTER parent table must exist. */
		} else if (columnno_by_name(parse_context, child_space,
					    child_cols->a[i].zName,
					    &fk_def->links[i].child_field,
					    constraint_name) != 0) {
			goto exit_create_fk;
		}
	}
	memcpy(fk_def->name, constraint_name, name_len);
	fk_def->name[name_len] = '\0';
	/*
	 * In case of <СREATE TABLE> and <ALTER TABLE ADD COLUMN>
	 * processing, all foreign keys constraints must be
	 * created after space itself (or space altering), so let
	 * delay it until vdbe_emit_create_constraints() call and
	 * simply maintain list of all FK constraints inside
	 * parser.
	 */
	if (!is_alter_add_constr) {
		struct rlist *fkeys =
			&parse_context->create_fk_constraint_parse_def.fkeys;
		struct fk_constraint_parse *fk_parse =
			rlist_first_entry(fkeys, struct fk_constraint_parse,
					  link);
		fk_parse->fk_def = fk_def;
	} else {
		vdbe_emit_fk_constraint_create(parse_context, fk_def,
					       child_space->def->name);
	}

exit_create_fk:
	sql_expr_list_delete(db, child_cols);
	if (!is_self_referenced)
		sql_expr_list_delete(db, parent_cols);
	sqlDbFree(db, parent_name);
	sqlDbFree(db, constraint_name);
	return;
tnt_error:
	parse_context->is_aborted = true;
	goto exit_create_fk;
}

void
fk_constraint_change_defer_mode(struct Parse *parse_context, bool is_deferred)
{
	struct rlist *fkeys =
		&parse_context->create_fk_constraint_parse_def.fkeys;
	if (parse_context->db->init.busy || rlist_empty(fkeys))
		return;
	rlist_first_entry(fkeys, struct fk_constraint_parse,
			  link)->fk_def->is_deferred = is_deferred;
}

/**
 * Emit code to drop the entry from _index or _ck_contstraint or
 * _fk_constraint space corresponding with the constraint type.
 */
void
sql_drop_constraint(struct Parse *parse_context)
{
	struct drop_entity_def *drop_def =
		&parse_context->drop_constraint_def.base;
	assert(drop_def->base.entity_type == ENTITY_TYPE_CONSTRAINT);
	assert(drop_def->base.alter_action == ALTER_ACTION_DROP);
	const char *table_name = drop_def->base.entity_name->a[0].zName;
	assert(table_name != NULL);
	struct space *space = space_by_name(table_name);
	if (space == NULL) {
		diag_set(ClientError, ER_NO_SUCH_SPACE, table_name);
		parse_context->is_aborted = true;
		return;
	}
	char *name = sql_normalized_name_region_new(&parse_context->region,
						    drop_def->name.z,
						    drop_def->name.n);
	if (name == NULL) {
		parse_context->is_aborted = true;
		return;
	}
	struct constraint_id *id = space_find_constraint_id(space, name);
	if (id == NULL) {
		diag_set(ClientError, ER_NO_SUCH_CONSTRAINT, name, table_name);
		parse_context->is_aborted = true;
		return;
	}
	/*
	 * We account changes to row count only if drop of
	 * foreign keys take place in a separate
	 * ALTER TABLE DROP CONSTRAINT statement, since whole
	 * DROP TABLE always returns 1 (one) as a row count.
	 */
	struct Vdbe *v = sqlGetVdbe(parse_context);
	assert(v != NULL);
	assert(id->type < constraint_type_MAX);
	switch (id->type) {
	case CONSTRAINT_TYPE_PK:
	case CONSTRAINT_TYPE_UNIQUE: {
		vdbe_emit_index_drop(parse_context, name, space->def,
				     ER_NO_SUCH_CONSTRAINT, false);
		break;
	}
	case CONSTRAINT_TYPE_FK:
		vdbe_emit_fk_constraint_drop(parse_context, name, space->def);
		break;
	case CONSTRAINT_TYPE_CK:
		vdbe_emit_ck_constraint_drop(parse_context, name, space->def);
		break;
	default:
		unreachable();
	}
	sqlVdbeCountChanges(v);
	sqlVdbeChangeP5(v, OPFLAG_NCHANGE);
}

/**
 * Position @a _index_cursor onto a last record in _index space
 * with a specified @a space_id. It corresponds to the latest
 * created index with the biggest id.
 * @param parser SQL parser.
 * @param space_id Space identifier to use as a key for _index.
 * @param _index_cursor A cursor, opened on _index system space.
 * @param[out] not_found_addr A VDBE address from which a jump
 *       happens when a record was not found.
 *
 * @return A VDBE address from which a jump happens when a record
 *         was found.
 */
static int
vdbe_emit_space_index_search(struct Parse *parser, uint32_t space_id,
			     int _index_cursor, int *not_found_addr)
{
	struct Vdbe *v = sqlGetVdbe(parser);
	int key_reg = ++parser->nMem;

	sqlVdbeAddOp2(v, OP_Integer, space_id, key_reg);
	int not_found1 = sqlVdbeAddOp4Int(v, OP_SeekLE, _index_cursor, 0,
					  key_reg, 1);
	int not_found2 = sqlVdbeAddOp4Int(v, OP_IdxLT, _index_cursor, 0,
					  key_reg, 1);
	int found_addr = sqlVdbeAddOp0(v, OP_Goto);
	sqlVdbeJumpHere(v, not_found1);
	sqlVdbeJumpHere(v, not_found2);
	*not_found_addr = sqlVdbeAddOp0(v, OP_Goto);
	return found_addr;
}

/**
 * Generate code to determine next free secondary index id in the
 * space identified by @a space_id. Overall VDBE program logic is
 * following:
 *
 * 1 Seek for space id in _index, goto l1 if seeks fails.
 * 2 Fetch index id from _index record.
 * 3 Goto l2
 * 4 l1: Generate iid == 1..
 * 6 l2: Continue index creation.
 *
 * Note that we generate iid == 1 in case of index search on
 * purpose: it allows on_replace_dd_index() raise correct
 * error - "can not add a secondary key before primary".
 *
 * @return Register holding a new index id.
 */
static int
vdbe_emit_new_sec_index_id(struct Parse *parse, uint32_t space_id,
			   int _index_cursor)
{
	struct Vdbe *v = sqlGetVdbe(parse);
	int not_found_addr, found_addr =
		vdbe_emit_space_index_search(parse, space_id, _index_cursor,
					     &not_found_addr);
	int iid_reg = ++parse->nMem;
	sqlVdbeJumpHere(v, found_addr);
	/* Fetch iid from the row and increment it. */
	sqlVdbeAddOp3(v, OP_Column, _index_cursor, BOX_INDEX_FIELD_ID, iid_reg);
	sqlVdbeAddOp2(v, OP_AddImm, iid_reg, 1);
	/* Jump over block assigning wrong index id. */
	int skip_bad_iid = sqlVdbeAddOp0(v, OP_Goto);
	sqlVdbeJumpHere(v, not_found_addr);
	/*
	 * Absence of any records in _index for that space is
	 * handled here: to indicate that secondary index can't
	 * be created before primary.
	 */
	sqlVdbeAddOp2(v, OP_Integer, 1, iid_reg);
	sqlVdbeJumpHere(v, skip_bad_iid);
	return iid_reg;
}

/**
 * Add new index to ephemeral @a space's "index" array. Reallocate
 * memory on @a parse's region if needed.
 *
 * We follow convention that PK comes first in list.
 *
 * @param parse Parsing structure.
 * @param space Space to which belongs given index.
 * @param index Index to be added to list.
 */
static int
sql_space_add_index(struct Parse *parse, struct space *space,
		    struct index *index)
{
	uint32_t idx_count = space->index_count;
	size_t size = 0;
	struct index **idx = NULL;
	struct region *region = &parse->region;
	if (idx_count == 0) {
		idx = region_alloc_array(region, typeof(struct index *), 1,
					 &size);
		if (idx == NULL)
			goto alloc_error;
	/*
	 * Reallocate each time the idx_count becomes equal to the
	 * power of two.
	 */
	} else if ((idx_count & (idx_count - 1)) == 0) {
		idx = region_alloc_array(region, typeof(struct index *),
					 idx_count * 2, &size);
		if (idx == NULL)
			goto alloc_error;
		memcpy(idx, space->index, idx_count * sizeof(struct index *));
	}
	if (idx != NULL)
		space->index = idx;
	/* Make sure that PK always comes as first member. */
	if (index->def->iid == 0 && idx_count != 0)
		SWAP(space->index[0], index);
	space->index[space->index_count++] = index;
	space->index_id_max =  MAX(space->index_id_max, index->def->iid);;
	return 0;

alloc_error:
	diag_set(OutOfMemory, size, "region_alloc_array", "idx");
	parse->is_aborted = true;
	return -1;
}

int
sql_space_def_check_format(const struct space_def *space_def)
{
	assert(space_def != NULL);
	if (space_def->field_count == 0) {
		diag_set(ClientError, ER_UNSUPPORTED, "SQL",
			 "space without format");
		return -1;
	}
	return 0;
}

/**
 * Create and set index_def in the given Index.
 *
 * @param parse Parse context.
 * @param index Index for which index_def should be created. It is
 *              used only to set index_def at the end of the
 *              function.
 * @param table Table which is indexed by 'index' param.
 * @param iid Index ID.
 * @param name Index name.
 * @param name_len Index name length.
 * @param expr_list List of expressions, describe which columns
 *                  of 'table' are used in index and also their
 *                  collations, orders, etc.
 * @param idx_type Index type: non-unique index, unique index,
 *                 index implementing UNIQUE constraint or
 *                 index implementing PK constraint.
 * @retval 0 on success, -1 on error.
 */
static int
index_fill_def(struct Parse *parse, struct index *index,
	       struct space_def *space_def, uint32_t iid, const char *name,
	       uint32_t name_len, struct ExprList *expr_list,
	       enum sql_index_type idx_type)
{
	struct index_opts opts;
	index_opts_create(&opts);
	opts.is_unique = idx_type != SQL_INDEX_TYPE_NON_UNIQUE;
	index->def = NULL;
	int rc = -1;

	struct key_def *key_def = NULL;
	size_t size;
	struct key_part_def *key_parts =
		region_alloc_array(&fiber()->gc, typeof(key_parts[0]),
				   expr_list->nExpr, &size);
	if (key_parts == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array",
			 "key_parts");
		goto tnt_error;
	}
	for (int i = 0; i < expr_list->nExpr; i++) {
		struct Expr *expr = expr_list->a[i].pExpr;
		sql_resolve_self_reference(parse, space_def, NC_IdxExpr, expr);
		if (parse->is_aborted)
			goto cleanup;

		struct Expr *column_expr = sqlExprSkipCollate(expr);
		if (column_expr->op != TK_COLUMN_REF) {
			diag_set(ClientError, ER_UNSUPPORTED, "Tarantool",
				 "functional indexes");
			goto tnt_error;
		}

		uint32_t fieldno = column_expr->iColumn;
		uint32_t coll_id;
		if (expr->op == TK_COLLATE) {
			if (sql_get_coll_seq(parse, expr->u.zToken,
					     &coll_id) == NULL)
				goto tnt_error;
		} else {
			sql_column_collation(space_def, fieldno, &coll_id);
		}
		/*
		 * Tarantool: DESC indexes are not supported so
		 * far.
		 */
		struct key_part_def *part = &key_parts[i];
		part->fieldno = fieldno;
		part->type = space_def->fields[fieldno].type;
		part->nullable_action = space_def->fields[fieldno].nullable_action;
		part->is_nullable = part->nullable_action == ON_CONFLICT_ACTION_NONE;
		part->exclude_null = false;
		part->sort_order = SORT_ORDER_ASC;
		part->coll_id = coll_id;
		part->path = NULL;
	}
	key_def = key_def_new(key_parts, expr_list->nExpr, false);
	if (key_def == NULL)
		goto tnt_error;
	/*
	 * Index def of PK is set to be NULL since it matters
	 * only for comparison routine. Meanwhile on front-end
	 * side only definition is used.
	 */
	index->def = index_def_new(space_def->id, 0, name, name_len, TREE,
				   &opts, key_def, NULL);
	if (index->def == NULL)
		goto tnt_error;
	index->def->iid = iid;
	rc = 0;
cleanup:
	if (key_def != NULL)
		key_def_delete(key_def);
	return rc;
tnt_error:
	parse->is_aborted = true;
	goto cleanup;
}

/**
 * Simple attempt at figuring out whether constraint was created
 * with name or without.
 */
static bool
constraint_is_named(const char *name)
{
	return strncmp(name, "sql_autoindex_", strlen("sql_autoindex_")) &&
		strncmp(name, "pk_unnamed_", strlen("pk_unnamed_")) &&
		strncmp(name, "unique_unnamed_", strlen("unique_unnamed_"));
}

void
sql_create_index(struct Parse *parse) {
	/* The index to be created. */
	struct index *index = NULL;
	/* Name of the index. */
	char *name = NULL;
	struct sql *db = parse->db;
	assert(!db->init.busy);
	struct create_index_def *create_idx_def = &parse->create_index_def;
	struct create_entity_def *create_entity_def = &create_idx_def->base.base;
	struct alter_entity_def *alter_entity_def = &create_entity_def->base;
	assert(alter_entity_def->entity_type == ENTITY_TYPE_INDEX);
	assert(alter_entity_def->alter_action == ALTER_ACTION_CREATE);
	/*
	 * Get list of columns to be indexed. It will be NULL if
	 * this is a primary key or unique-constraint on the most
	 * recent column added to the table under construction.
	 */
	struct ExprList *col_list = create_idx_def->cols;
	struct SrcList *tbl_name = alter_entity_def->entity_name;

	if (db->mallocFailed || parse->is_aborted)
		goto exit_create_index;
	enum sql_index_type idx_type = create_idx_def->idx_type;
	if (idx_type == SQL_INDEX_TYPE_UNIQUE ||
	    idx_type == SQL_INDEX_TYPE_NON_UNIQUE) {
		Vdbe *v = sqlGetVdbe(parse);
		if (v == NULL)
			goto exit_create_index;
		sqlVdbeCountChanges(v);
	}

	/*
	 * Find the table that is to be indexed.
	 * Return early if not found.
	 */
	struct space *space = parse->create_table_def.new_space;
	if (space == NULL)
		space = parse->create_column_def.space;
	bool is_create_table_or_add_col = space != NULL;
	struct Token token = create_entity_def->name;
	if (tbl_name != NULL) {
		assert(token.n > 0 && token.z != NULL);
		const char *name = tbl_name->a[0].zName;
		space = space_by_name(name);
		if (space == NULL) {
			if (! create_entity_def->if_not_exist) {
				diag_set(ClientError, ER_NO_SUCH_SPACE, name);
				parse->is_aborted = true;
			}
			goto exit_create_index;
		}
	} else if (!is_create_table_or_add_col) {
		goto exit_create_index;
	}
	struct space_def *def = space->def;

	if (def->opts.is_view) {
		diag_set(ClientError, ER_MODIFY_INDEX,
			 sql_name_from_token(db, &token), def->name,
			 "views can not be indexed");
		parse->is_aborted = true;
		goto exit_create_index;
	}
	if (sql_space_def_check_format(def) != 0) {
		parse->is_aborted = true;
		goto exit_create_index;
	}
	/*
	 * Find the name of the index.  Make sure there is not
	 * already another index with the same name.
	 *
	 * Exception:  If we are reading the names of permanent
	 * indices from the Tarantool schema (because some other
	 * process changed the schema) and one of the index names
	 * collides with the name of index, then we will continue
	 * to process this index.
	 *
	 * If token == NULL it means that we are dealing with a
	 * primary key or UNIQUE constraint.  We have to invent
	 * our own name.
	 *
	 * In case of UNIQUE constraint we have two options:
	 * 1) UNIQUE constraint is named and this name will
	 *    be a part of index name.
	 * 2) UNIQUE constraint is non-named and standard
	 *    auto-index name will be generated.
	 */
	if (!is_create_table_or_add_col) {
		assert(token.z != NULL);
		name = sql_name_from_token(db, &token);
		if (name == NULL) {
			parse->is_aborted = true;
			goto exit_create_index;
		}
		if (space_index_by_name(space, name) != NULL) {
			if (! create_entity_def->if_not_exist) {
				diag_set(ClientError, ER_INDEX_EXISTS_IN_SPACE,
					 name, def->name);
				parse->is_aborted = true;
			}
			goto exit_create_index;
		}
	} else {
		char *constraint_name = NULL;
		if (create_entity_def->name.n > 0) {
			constraint_name =
				sql_name_from_token(db,
						    &create_entity_def->name);
			if (constraint_name == NULL) {
				parse->is_aborted = true;
				goto exit_create_index;
			}
		}

	       /*
		* This naming is temporary. Now it's not
		* possible (since we implement UNIQUE
		* and PK constraints with indexes and
		* indexes can not have same names), but
		* in future we would use names exactly
		* as they are set by user.
		*/
		assert(idx_type == SQL_INDEX_TYPE_CONSTRAINT_UNIQUE ||
		       idx_type == SQL_INDEX_TYPE_CONSTRAINT_PK);
		uint32_t idx_count = space->index_count;
		if (constraint_name == NULL) {
			if (idx_type == SQL_INDEX_TYPE_CONSTRAINT_UNIQUE) {
				name = sqlMPrintf(db, "unique_unnamed_%s_%d",
						  def->name, idx_count + 1);
			} else {
				name = sqlMPrintf(db, "pk_unnamed_%s_%d",
						  def->name, idx_count + 1);
			}
		} else {
			name = sqlDbStrDup(db, constraint_name);
		}
		sqlDbFree(db, constraint_name);
	}

	if (name == NULL || sqlCheckIdentifierName(parse, name) != 0)
		goto exit_create_index;

	if (tbl_name != NULL && space_is_system(space)) {
		diag_set(ClientError, ER_MODIFY_INDEX, name, def->name,
			 "can't create index on system space");
		parse->is_aborted = true;
		goto exit_create_index;
	}

	/*
	 * If col_list == NULL, it means this routine was called
	 * to make a primary key or unique constraint out of the
	 * last column added to the table under construction.
	 * So create a fake list to simulate this.
	 */
	if (col_list == NULL) {
		struct Token prev_col;
		uint32_t last_field = def->field_count - 1;
		sqlTokenInit(&prev_col, def->fields[last_field].name);
		struct Expr *expr = sql_expr_new(db, TK_ID, &prev_col);
		if (expr == NULL) {
			parse->is_aborted = true;
			goto exit_create_index;
		}
		col_list = sql_expr_list_append(db, NULL, expr);
		if (col_list == NULL)
			goto exit_create_index;
		assert(col_list->nExpr == 1);
		sqlExprListSetSortOrder(col_list, create_idx_def->sort_order);
	} else {
		if (col_list->nExpr > db->aLimit[SQL_LIMIT_COLUMN]) {
			diag_set(ClientError, ER_SQL_PARSER_LIMIT,
				 "The number of columns in index",
				 col_list->nExpr, db->aLimit[SQL_LIMIT_COLUMN]);
			parse->is_aborted = true;
		}
	}
	size_t size;
	index = region_alloc_object(&parse->region, typeof(*index), &size);
	if (index == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_object", "index");
		parse->is_aborted = true;
		goto exit_create_index;
	}
	memset(index, 0, sizeof(*index));

	/*
	 * TODO: Issue a warning if two or more columns of the
	 * index are identical.
	 * TODO: Issue a warning if the table primary key is used
	 * as part of the index key.
	 */
	uint32_t iid;
	if (idx_type != SQL_INDEX_TYPE_CONSTRAINT_PK)
		iid = space->index_id_max + 1;
	else
		iid = 0;
	if (index_fill_def(parse, index, def, iid, name, strlen(name),
			   col_list, idx_type) != 0)
		goto exit_create_index;
	/*
	 * Remove all redundant columns from the PRIMARY KEY.
	 * For example, change "PRIMARY KEY(a,b,a,b,c,b,c,d)" into
	 * just "PRIMARY KEY(a,b,c,d)". Later code assumes the
	 * PRIMARY KEY contains no repeated columns.
	 */
	struct key_part *parts = index->def->key_def->parts;
	uint32_t part_count = index->def->key_def->part_count;
	uint32_t new_part_count = 1;
	for(uint32_t i = 1; i < part_count; i++) {
		uint32_t j;
		for(j = 0; j < new_part_count; j++) {
			if(parts[i].fieldno == parts[j].fieldno)
				break;
		}

		if (j == new_part_count)
			parts[new_part_count++] = parts[i];
	}
	index->def->key_def->part_count = new_part_count;

	if (!index_def_is_valid(index->def, def->name))
		goto exit_create_index;

	/*
	 * Here we handle cases, when in CREATE TABLE statement
	 * some UNIQUE constraints are putted exactly on the same
	 * columns with PRIMARY KEY constraint. Our general
	 * intention is to omit creating indexes for non-named
	 * UNIQUE constraints if these constraints are putted on
	 * the same columns as the PRIMARY KEY constraint. In
	 * different cases it is implemented in different ways.
	 *
	 * 1) CREATE TABLE t(a UNIQUE PRIMARY KEY)
	 *    CREATE TABLE t(a, UNIQUE(a), PRIMARY KEY(a))
	 *    In these cases we firstly proceed UNIQUE(a)
	 *    and create index for it, then proceed PRIMARY KEY,
	 *    but don't create index for it. Instead of it we
	 *    change UNIQUE constraint index name and index_type,
	 *    so it becomes PRIMARY KEY index.
	 *
	 * 2) CREATE TABLE t(a, PRIMARY KEY(a), UNIQUE(a))
	 *    In such cases we simply do not create index for
	 *    UNIQUE constraint.
	 *
	 * Note 1: We always create new index for named UNIQUE
	 * constraints.
	 *
	 * Note 2: If UNIQUE constraint (no matter named or
	 * non-named) is putted on the same columns as PRIMARY KEY
	 * constraint, but has different onError (behavior on
	 * constraint violation), then an error is raised.
	 */
	if (is_create_table_or_add_col) {
		for (uint32_t i = 0; i < space->index_count; ++i) {
			struct index *existing_idx = space->index[i];
			uint32_t iid = existing_idx->def->iid;
			struct key_def *key_def = index->def->key_def;
			struct key_def *exst_key_def =
				existing_idx->def->key_def;

			if (key_def->part_count != exst_key_def->part_count)
				continue;

			uint32_t k;
			for (k = 0; k < key_def->part_count; k++) {
				if (key_def->parts[k].fieldno !=
				    exst_key_def->parts[k].fieldno)
					break;
				if (key_def->parts[k].coll !=
				    exst_key_def->parts[k].coll)
					break;
			}

			if (k != key_def->part_count)
				continue;

			bool is_named =
				constraint_is_named(existing_idx->def->name);
			/* CREATE TABLE t(a, UNIQUE(a), PRIMARY KEY(a)). */
			if (idx_type == SQL_INDEX_TYPE_CONSTRAINT_PK &&
			    iid != 0 && !is_named) {
				existing_idx->def->iid = 0;
				goto exit_create_index;
			}

			/* CREATE TABLE t(a, PRIMARY KEY(a), UNIQUE(a)). */
			if (idx_type == SQL_INDEX_TYPE_CONSTRAINT_UNIQUE &&
			    !constraint_is_named(index->def->name))
				goto exit_create_index;
		}
	}

	/*
	 * If this is the initial CREATE INDEX statement (or
	 * CREATE TABLE if the index is an implied index for a
	 * UNIQUE or PRIMARY KEY constraint) then emit code to
	 * insert new index into Tarantool. But, do not do this if
	 * we are simply parsing the schema, or if this index is
	 * the PRIMARY KEY index.
	 *
	 * If tbl_name == NULL it means this index is generated as
	 * an implied PRIMARY KEY or UNIQUE index in a CREATE
	 * TABLE statement.  Since the table has just been
	 * created, it contains no data and the index
	 * initialization step can be skipped.
	 */
	else if (tbl_name != NULL) {
		Vdbe *vdbe;
		int cursor = parse->nTab++;

		vdbe = sqlGetVdbe(parse);
		if (vdbe == 0)
			goto exit_create_index;

		sql_set_multi_write(parse, true);
		sqlVdbeAddOp4(vdbe, OP_IteratorOpen, cursor, 0, 0,
				  (void *)space_by_id(BOX_INDEX_ID),
				  P4_SPACEPTR);
		sqlVdbeChangeP5(vdbe, OPFLAG_SEEKEQ);
		int index_id;
		/*
		 * In case we are creating PRIMARY KEY constraint
		 * (via ALTER TABLE) we must ensure that table
		 * doesn't feature any indexes. Otherwise,
		 * we can immediately halt execution of VDBE.
		 */
		if (idx_type == SQL_INDEX_TYPE_CONSTRAINT_PK) {
			index_id = ++parse->nMem;
			sqlVdbeAddOp2(vdbe, OP_Integer, 0, index_id);
		} else {
			index_id = vdbe_emit_new_sec_index_id(parse, def->id,
							      cursor);
		}
		sqlVdbeAddOp1(vdbe, OP_Close, cursor);
		vdbe_emit_create_index(parse, def, index->def,
				       def->id, index_id);
		sqlVdbeChangeP5(vdbe, OPFLAG_NCHANGE);
		sqlVdbeAddOp0(vdbe, OP_Expire);
	}

	if (!is_create_table_or_add_col ||
	    sql_space_add_index(parse, space, index) != 0)
		goto exit_create_index;
	index = NULL;

	/* Clean up before exiting. */
 exit_create_index:
	if (index != NULL && index->def != NULL)
		index_def_delete(index->def);
	sql_expr_list_delete(db, col_list);
	sqlSrcListDelete(db, tbl_name);
	sqlDbFree(db, name);
}

void
sql_drop_index(struct Parse *parse_context)
{
	struct drop_entity_def *drop_def = &parse_context->drop_index_def.base;
	assert(drop_def->base.entity_type == ENTITY_TYPE_INDEX);
	assert(drop_def->base.alter_action == ALTER_ACTION_DROP);
	struct Vdbe *v = sqlGetVdbe(parse_context);
	assert(v != NULL);
	struct sql *db = parse_context->db;
	/* Never called with prior errors. */
	assert(!parse_context->is_aborted);
	struct SrcList *table_list = drop_def->base.entity_name;
	assert(table_list->nSrc == 1);
	char *table_name = table_list->a[0].zName;
	const char *index_name = NULL;
	if (db->mallocFailed) {
		goto exit_drop_index;
	}
	sqlVdbeCountChanges(v);
	struct space *space = space_by_name(table_name);
	bool if_exists = drop_def->if_exist;
	if (space == NULL) {
		if (!if_exists) {
			diag_set(ClientError, ER_NO_SUCH_SPACE, table_name);
			parse_context->is_aborted = true;
		}
		goto exit_drop_index;
	}
	index_name = sql_name_from_token(db, &drop_def->name);
	if (index_name == NULL) {
		parse_context->is_aborted = true;
		goto exit_drop_index;
	}

	vdbe_emit_index_drop(parse_context, index_name, space->def,
			     ER_NO_SUCH_INDEX_NAME, if_exists);
	sqlVdbeChangeP5(v, OPFLAG_NCHANGE);
 exit_drop_index:
	sqlSrcListDelete(db, table_list);
	sqlDbFree(db, (void *) index_name);
}

/*
 * pArray is a pointer to an array of objects. Each object in the
 * array is szEntry bytes in size. This routine uses sqlDbRealloc()
 * to extend the array so that there is space for a new object at the end.
 *
 * When this function is called, *pnEntry contains the current size of
 * the array (in entries - so the allocation is ((*pnEntry) * szEntry) bytes
 * in total).
 *
 * If the realloc() is successful (i.e. if no OOM condition occurs), the
 * space allocated for the new object is zeroed, *pnEntry updated to
 * reflect the new size of the array and a pointer to the new allocation
 * returned. *pIdx is set to the index of the new array entry in this case.
 *
 * Otherwise, if the realloc() fails, *pIdx is set to -1, *pnEntry remains
 * unchanged and a copy of pArray returned.
 */
void *
sqlArrayAllocate(sql * db,	/* Connection to notify of malloc failures */
		     void *pArray,	/* Array of objects.  Might be reallocated */
		     int szEntry,	/* Size of each object in the array */
		     int *pnEntry,	/* Number of objects currently in use */
		     int *pIdx	/* Write the index of a new slot here */
    )
{
	char *z;
	int n = *pnEntry;
	if ((n & (n - 1)) == 0) {
		int sz = (n == 0) ? 1 : 2 * n;
		void *pNew = sqlDbRealloc(db, pArray, sz * szEntry);
		if (pNew == 0) {
			*pIdx = -1;
			return pArray;
		}
		pArray = pNew;
	}
	z = (char *)pArray;
	memset(&z[n * szEntry], 0, szEntry);
	*pIdx = n;
	++*pnEntry;
	return pArray;
}

struct IdList *
sql_id_list_append(struct sql *db, struct IdList *list,
		   struct Token *name_token)
{
	if (list == NULL &&
	    (list = sqlDbMallocZero(db, sizeof(*list))) == NULL) {
		diag_set(OutOfMemory, sizeof(*list), "sqlDbMallocZero", "list");
		return NULL;
	}
	int i;
	list->a = sqlArrayAllocate(db, list->a, sizeof(list->a[0]),
				   &list->nId, &i);
	if (i >= 0) {
		list->a[i].zName = sql_name_from_token(db, name_token);
		if (list->a[i].zName != NULL)
			return list;
	}
	sqlIdListDelete(db, list);
	return NULL;
}

/*
 * Delete an IdList.
 */
void
sqlIdListDelete(sql * db, IdList * pList)
{
	int i;
	if (pList == 0)
		return;
	for (i = 0; i < pList->nId; i++) {
		sqlDbFree(db, pList->a[i].zName);
	}
	sqlDbFree(db, pList->a);
	sqlDbFree(db, pList);
}

/*
 * Return the index in pList of the identifier named zId.  Return -1
 * if not found.
 */
int
sqlIdListIndex(IdList * pList, const char *zName)
{
	int i;
	if (pList == 0)
		return -1;
	for (i = 0; i < pList->nId; i++) {
		if (strcmp(pList->a[i].zName, zName) == 0)
			return i;
	}
	return -1;
}

struct SrcList *
sql_src_list_enlarge(struct sql *db, struct SrcList *src_list, int new_slots,
		     int start_idx)
{
	assert(start_idx >= 0);
	assert(new_slots >= 1);
	assert(src_list != NULL);
	assert(start_idx <= src_list->nSrc);

	/* Allocate additional space if needed. */
	if (src_list->nSrc + new_slots > (int)src_list->nAlloc) {
		int to_alloc = src_list->nSrc * 2 + new_slots;
		int size = sizeof(*src_list) +
			   (to_alloc - 1) * sizeof(src_list->a[0]);
		src_list = sqlDbRealloc(db, src_list, size);
		if (src_list == NULL) {
			diag_set(OutOfMemory, size, "sqlDbRealloc", "src_list");
			return NULL;
		}
		src_list->nAlloc = to_alloc;
	}

	/*
	 * Move existing slots that come after the newly inserted
	 * slots out of the way.
	 */
	memmove(&src_list->a[start_idx + new_slots], &src_list->a[start_idx],
		(src_list->nSrc - start_idx) * sizeof(src_list->a[0]));
	src_list->nSrc += new_slots;

	/* Zero the newly allocated slots. */
	memset(&src_list->a[start_idx], 0, sizeof(src_list->a[0]) * new_slots);
	for (int i = start_idx; i < start_idx + new_slots; i++)
		src_list->a[i].iCursor = -1;

	/* Return a pointer to the enlarged SrcList. */
	return src_list;
}

struct SrcList *
sql_src_list_new(struct sql *db)
{
	struct SrcList *src_list = sqlDbMallocRawNN(db, sizeof(struct SrcList));
	if (src_list == NULL) {
		diag_set(OutOfMemory, sizeof(struct SrcList),
			 "sqlDbMallocRawNN", "src_list");
		return NULL;
	}
	src_list->nAlloc = 1;
	src_list->nSrc = 1;
	memset(&src_list->a[0], 0, sizeof(src_list->a[0]));
	src_list->a[0].iCursor = -1;
	return src_list;
}

struct SrcList *
sql_src_list_append(struct sql *db, struct SrcList *list,
		    struct Token *name_token)
{
	if (list == NULL) {
		list = sql_src_list_new(db);
		if (list == NULL)
			return NULL;
	} else {
		struct SrcList *new_list =
			sql_src_list_enlarge(db, list, 1, list->nSrc);
		if (new_list == NULL) {
			sqlSrcListDelete(db, list);
			return NULL;
		}
		list = new_list;
	}
	struct SrcList_item *item = &list->a[list->nSrc - 1];
	if (name_token != NULL) {
		item->zName = sql_name_from_token(db, name_token);
		if (item->zName == NULL) {
			sqlSrcListDelete(db, list);
			return NULL;
		}
	}
	return list;
}

/*
 * Assign VdbeCursor index numbers to all tables in a SrcList
 */
void
sqlSrcListAssignCursors(Parse * pParse, SrcList * pList)
{
	int i;
	struct SrcList_item *pItem;
	assert(pList || pParse->db->mallocFailed);
	if (pList) {
		for (i = 0, pItem = pList->a; i < pList->nSrc; i++, pItem++) {
			if (pItem->iCursor >= 0)
				break;
			pItem->iCursor = pParse->nTab++;
			if (pItem->pSelect) {
				sqlSrcListAssignCursors(pParse,
							    pItem->pSelect->
							    pSrc);
			}
		}
	}
}

void
sqlSrcListDelete(sql * db, SrcList * pList)
{
	int i;
	struct SrcList_item *pItem;
	if (pList == 0)
		return;
	for (pItem = pList->a, i = 0; i < pList->nSrc; i++, pItem++) {
		sqlDbFree(db, pItem->zName);
		sqlDbFree(db, pItem->zAlias);
		if (pItem->fg.isIndexedBy)
			sqlDbFree(db, pItem->u1.zIndexedBy);
		if (pItem->fg.isTabFunc)
			sql_expr_list_delete(db, pItem->u1.pFuncArg);
		/*
		* Space is either not ephemeral which means that
		* it came from space cache; or space is ephemeral
		* but has no indexes and check constraints.
		* The latter proves that it is not the space
		* which might come from CREATE TABLE routines.
		*/
		assert(pItem->space == NULL ||
			!pItem->space->def->opts.is_ephemeral ||
			pItem->space->index == NULL);
		sql_select_delete(db, pItem->pSelect);
		sql_expr_delete(db, pItem->pOn, false);
		sqlIdListDelete(db, pItem->pUsing);
	}
	sqlDbFree(db, pList);
}

/*
 * This routine is called by the parser to add a new term to the
 * end of a growing FROM clause.  The "p" parameter is the part of
 * the FROM clause that has already been constructed.  "p" is NULL
 * if this is the first term of the FROM clause.  pTable and pDatabase
 * are the name of the table and database named in the FROM clause term.
 * pDatabase is NULL if the database name qualifier is missing - the
 * usual case.  If the term has an alias, then pAlias points to the
 * alias token.  If the term is a subquery, then pSubquery is the
 * SELECT statement that the subquery encodes.  The pTable and
 * pDatabase parameters are NULL for subqueries.  The pOn and pUsing
 * parameters are the content of the ON and USING clauses.
 *
 * Return a new SrcList which encodes is the FROM with the new
 * term added.
 */
SrcList *
sqlSrcListAppendFromTerm(Parse * pParse,	/* Parsing context */
			     SrcList * p,	/* The left part of the FROM clause already seen */
			     Token * pTable,	/* Name of the table to add to the FROM clause */
			     Token * pAlias,	/* The right-hand side of the AS subexpression */
			     Select * pSubquery,	/* A subquery used in place of a table name */
			     Expr * pOn,	/* The ON clause of a join */
			     IdList * pUsing	/* The USING clause of a join */
    )
{
	struct SrcList_item *pItem;
	sql *db = pParse->db;
	if (!p && (pOn || pUsing)) {
		diag_set(ClientError, ER_SQL_SYNTAX_WITH_POS,
			 pParse->line_count, pParse->line_pos, "a JOIN clause "\
			 "is required before ON and USING");
		pParse->is_aborted = true;
		goto append_from_error;
	}
	p = sql_src_list_append(db, p, pTable);
	if (p == NULL) {
		pParse->is_aborted = true;
		goto append_from_error;
	}
	assert(p->nSrc != 0);
	pItem = &p->a[p->nSrc - 1];
	assert(pAlias != 0);
	if (pAlias->n != 0) {
		pItem->zAlias = sql_name_from_token(db, pAlias);
		if (pItem->zAlias == NULL) {
			pParse->is_aborted = true;
			goto append_from_error;
		}
	}
	pItem->pSelect = pSubquery;
	pItem->pOn = pOn;
	pItem->pUsing = pUsing;
	return p;

 append_from_error:
	assert(p == 0);
	sql_expr_delete(db, pOn, false);
	sqlIdListDelete(db, pUsing);
	sql_select_delete(db, pSubquery);
	return 0;
}

/*
 * Add an INDEXED BY or NOT INDEXED clause to the most recently added
 * element of the source-list passed as the second argument.
 */
void
sqlSrcListIndexedBy(Parse * pParse, SrcList * p, Token * pIndexedBy)
{
	assert(pIndexedBy != 0);
	if (p && ALWAYS(p->nSrc > 0)) {
		struct SrcList_item *pItem = &p->a[p->nSrc - 1];
		assert(pItem->fg.notIndexed == 0);
		assert(pItem->fg.isIndexedBy == 0);
		assert(pItem->fg.isTabFunc == 0);
		if (pIndexedBy->n == 1 && !pIndexedBy->z) {
			/* A "NOT INDEXED" clause was supplied. See parse.y
			 * construct "indexed_opt" for details.
			 */
			pItem->fg.notIndexed = 1;
		} else if (pIndexedBy->z != NULL) {
			pItem->u1.zIndexedBy =
				sql_name_from_token(pParse->db, pIndexedBy);
			if (pItem->u1.zIndexedBy == NULL) {
				pParse->is_aborted = true;
				return;
			}
			pItem->fg.isIndexedBy = true;
		}
	}
}

/*
 * Add the list of function arguments to the SrcList entry for a
 * table-valued-function.
 */
void
sqlSrcListFuncArgs(Parse * pParse, SrcList * p, ExprList * pList)
{
	if (p) {
		struct SrcList_item *pItem = &p->a[p->nSrc - 1];
		assert(pItem->fg.notIndexed == 0);
		assert(pItem->fg.isIndexedBy == 0);
		assert(pItem->fg.isTabFunc == 0);
		pItem->u1.pFuncArg = pList;
		pItem->fg.isTabFunc = 1;
	} else {
		sql_expr_list_delete(pParse->db, pList);
	}
}

/*
 * When building up a FROM clause in the parser, the join operator
 * is initially attached to the left operand.  But the code generator
 * expects the join operator to be on the right operand.  This routine
 * Shifts all join operators from left to right for an entire FROM
 * clause.
 *
 * Example: Suppose the join is like this:
 *
 *           A natural cross join B
 *
 * The operator is "natural cross join".  The A and B operands are stored
 * in p->a[0] and p->a[1], respectively.  The parser initially stores the
 * operator with A.  This routine shifts that operator over to B.
 */
void
sqlSrcListShiftJoinType(SrcList * p)
{
	if (p) {
		int i;
		for (i = p->nSrc - 1; i > 0; i--) {
			p->a[i].fg.jointype = p->a[i - 1].fg.jointype;
		}
		p->a[0].fg.jointype = 0;
	}
}

void
sql_transaction_begin(struct Parse *parse_context)
{
	assert(parse_context != NULL);
	struct Vdbe *v = sqlGetVdbe(parse_context);
	if (v != NULL)
		sqlVdbeAddOp0(v, OP_TransactionBegin);
}

void
sql_transaction_commit(struct Parse *parse_context)
{
	assert(parse_context != NULL);
	struct Vdbe *v = sqlGetVdbe(parse_context);
	if (v != NULL)
		sqlVdbeAddOp0(v, OP_TransactionCommit);
}

void
sql_transaction_rollback(Parse *pParse)
{
	assert(pParse != 0);
	struct Vdbe *v = sqlGetVdbe(pParse);
	if (v != NULL)
		sqlVdbeAddOp0(v, OP_TransactionRollback);
}

/*
 * This function is called by the parser when it parses a command to create,
 * release or rollback an SQL savepoint.
 */
void
sqlSavepoint(Parse * pParse, int op, Token * pName)
{
	struct sql *db = pParse->db;
	char *zName = sql_name_from_token(db, pName);
	if (zName) {
		Vdbe *v = sqlGetVdbe(pParse);
		if (!v) {
			sqlDbFree(db, zName);
			return;
		}
		if (op == SAVEPOINT_BEGIN &&
		    sqlCheckIdentifierName(pParse, zName) != 0)
			return;
		sqlVdbeAddOp4(v, OP_Savepoint, op, 0, 0, zName, P4_DYNAMIC);
	} else {
		pParse->is_aborted = true;
	}
}

/**
 * Set flag in parse context, which indicates that during query
 * execution multiple insertion/updates may occur.
 */
void
sql_set_multi_write(struct Parse *parse_context, bool is_set)
{
	Parse *pToplevel = sqlParseToplevel(parse_context);
	pToplevel->isMultiWrite |= is_set;
}

/*
 * This routine is invoked once per CTE by the parser while parsing a
 * WITH clause.
 */
With *
sqlWithAdd(Parse * pParse,	/* Parsing context */
	       With * pWith,	/* Existing WITH clause, or NULL */
	       Token * pName,	/* Name of the common-table */
	       ExprList * pArglist,	/* Optional column name list for the table */
	       Select * pQuery	/* Query used to initialize the table */
    )
{
	sql *db = pParse->db;
	With *pNew;

	/*
	 * Check that the CTE name is unique within this WITH
	 * clause. If not, store an error in the Parse structure.
	 */
	char *name = sql_name_from_token(db, pName);
	if (name == NULL) {
		pParse->is_aborted = true;
		goto error;
	}
	if (pWith != NULL) {
		int i;
		const char *err = "Ambiguous table name in WITH query: %s";
		for (i = 0; i < pWith->nCte; i++) {
			if (strcmp(name, pWith->a[i].zName) == 0) {
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 tt_sprintf(err, name));
				pParse->is_aborted = true;
			}
		}
	}

	if (pWith) {
		int nByte =
		    sizeof(*pWith) + (sizeof(pWith->a[1]) * pWith->nCte);
		pNew = sqlDbRealloc(db, pWith, nByte);
	} else {
		pNew = sqlDbMallocZero(db, sizeof(*pWith));
	}
	assert((pNew != NULL && name != NULL) || db->mallocFailed);

	if (db->mallocFailed) {
error:
		sql_expr_list_delete(db, pArglist);
		sql_select_delete(db, pQuery);
		sqlDbFree(db, name);
		pNew = pWith;
	} else {
		pNew->a[pNew->nCte].pSelect = pQuery;
		pNew->a[pNew->nCte].pCols = pArglist;
		pNew->a[pNew->nCte].zName = name;
		pNew->a[pNew->nCte].zCteErr = 0;
		pNew->nCte++;
	}

	return pNew;
}

/*
 * Free the contents of the With object passed as the second argument.
 */
void
sqlWithDelete(sql * db, With * pWith)
{
	if (pWith) {
		int i;
		for (i = 0; i < pWith->nCte; i++) {
			struct Cte *pCte = &pWith->a[i];
			sql_expr_list_delete(db, pCte->pCols);
			sql_select_delete(db, pCte->pSelect);
			sqlDbFree(db, pCte->zName);
		}
		sqlDbFree(db, pWith);
	}
}

int
vdbe_emit_halt_with_presence_test(struct Parse *parser, int space_id,
				  int index_id, int key_reg, uint32_t key_len,
				  int tarantool_error_code,
				  const char *error_src, bool no_error,
				  int cond_opcode)
{
	assert(cond_opcode == OP_NoConflict || cond_opcode == OP_Found);
	struct Vdbe *v = sqlGetVdbe(parser);
	assert(v != NULL);

	struct sql *db = parser->db;
	char *error = sqlDbStrDup(db, error_src);
	if (error == NULL)
		return -1;

	int cursor = parser->nTab++;
	vdbe_emit_open_cursor(parser, cursor, index_id, space_by_id(space_id));
	sqlVdbeChangeP5(v, OPFLAG_SYSTEMSP);
	int addr = sqlVdbeAddOp4Int(v, cond_opcode, cursor, 0, key_reg,
				    key_len);
	if (no_error) {
		sqlVdbeAddOp0(v, OP_Halt);
	} else {
		sqlVdbeAddOp4(v, OP_SetDiag, tarantool_error_code, 0, 0, error,
			      P4_DYNAMIC);
		sqlVdbeAddOp2(v, OP_Halt, -1, ON_CONFLICT_ACTION_ABORT);
	}
	sqlVdbeJumpHere(v, addr);
	sqlVdbeAddOp1(v, OP_Close, cursor);
	return 0;
}

int
sql_add_autoincrement(struct Parse *parse_context, uint32_t fieldno)
{
	if (parse_context->has_autoinc) {
		diag_set(ClientError, ER_SQL_SYNTAX_WITH_POS,
			 parse_context->line_count, parse_context->line_pos,
			 "table must feature at most one AUTOINCREMENT field");
		parse_context->is_aborted = true;
		return -1;
	}
	parse_context->has_autoinc = true;
	parse_context->autoinc_fieldno = fieldno;
	return 0;
}

int
sql_fieldno_by_name(struct Parse *parse_context, struct Expr *field_name,
		    uint32_t *fieldno)
{
	struct space_def *def = parse_context->create_table_def.new_space->def;
	struct Expr *name = sqlExprSkipCollate(field_name);
	if (name->op != TK_ID) {
		diag_set(ClientError, ER_INDEX_DEF_UNSUPPORTED, "Expressions");
		parse_context->is_aborted = true;
		return -1;
	}
	uint32_t i;
	for (i = 0; i < def->field_count; ++i) {
		if (strcmp(def->fields[i].name, name->u.zToken) == 0)
			break;
	}
	if (i == def->field_count) {
		diag_set(ClientError, ER_SQL_CANT_RESOLVE_FIELD, name->u.zToken);
		parse_context->is_aborted = true;
		return -1;
	}
	*fieldno = i;
	return 0;
}

/**
 * A local structure that allows to establish a connection between
 * parameter and its field type and mask, if it has one.
 */
struct sql_option_metadata
{
	uint32_t field_type;
	uint32_t mask;
};

/**
 * Variable that contains SQL session option field types and masks
 * if they have one or 0 if don't have.
 *
 * It is IMPORTANT that these options sorted by name.
 */
static struct sql_option_metadata sql_session_opts[] = {
	/** SESSION_SETTING_SQL_DEFAULT_ENGINE */
	{FIELD_TYPE_STRING, 0},
	/** SESSION_SETTING_SQL_DEFER_FOREIGN_KEYS */
	{FIELD_TYPE_BOOLEAN, SQL_DeferFKs},
	/** SESSION_SETTING_SQL_FULL_COLUMN_NAMES */
	{FIELD_TYPE_BOOLEAN, SQL_FullColNames},
	/** SESSION_SETTING_SQL_FULL_METADATA */
	{FIELD_TYPE_BOOLEAN, SQL_FullMetadata},
	/** SESSION_SETTING_SQL_PARSER_DEBUG */
	{FIELD_TYPE_BOOLEAN, SQL_SqlTrace | PARSER_TRACE_FLAG},
	/** SESSION_SETTING_SQL_RECURSIVE_TRIGGERS */
	{FIELD_TYPE_BOOLEAN, SQL_RecTriggers},
	/** SESSION_SETTING_SQL_REVERSE_UNORDERED_SELECTS */
	{FIELD_TYPE_BOOLEAN, SQL_ReverseOrder},
	/** SESSION_SETTING_SQL_SELECT_DEBUG */
	{FIELD_TYPE_BOOLEAN,
	 SQL_SqlTrace | SQL_SelectTrace | SQL_WhereTrace},
	/** SESSION_SETTING_SQL_VDBE_DEBUG */
	{FIELD_TYPE_BOOLEAN,
	 SQL_SqlTrace | SQL_VdbeListing | SQL_VdbeTrace},
};

static void
sql_session_setting_get(int id, const char **mp_pair, const char **mp_pair_end)
{
	assert(id >= SESSION_SETTING_SQL_BEGIN && id < SESSION_SETTING_SQL_END);
	struct session *session = current_session();
	uint32_t flags = session->sql_flags;
	struct sql_option_metadata *opt =
		&sql_session_opts[id - SESSION_SETTING_SQL_BEGIN];
	uint32_t mask = opt->mask;
	const char *name = session_setting_strs[id];
	size_t name_len = strlen(name);
	size_t engine_len;
	const char *engine;
	size_t size = mp_sizeof_array(2) + mp_sizeof_str(name_len);
	/*
	 * Currently, SQL session settings are of a boolean or
	 * string type.
	 */
	bool is_bool = opt->field_type == FIELD_TYPE_BOOLEAN;
	if (is_bool) {
		size += mp_sizeof_bool(true);
	} else {
		assert(id == SESSION_SETTING_SQL_DEFAULT_ENGINE);
		engine = sql_storage_engine_strs[session->sql_default_engine];
		engine_len = strlen(engine);
		size += mp_sizeof_str(engine_len);
	}

	char *pos = static_alloc(size);
	assert(pos != NULL);
	char *pos_end = mp_encode_array(pos, 2);
	pos_end = mp_encode_str(pos_end, name, name_len);
	if (is_bool)
		pos_end = mp_encode_bool(pos_end, (flags & mask) == mask);
	else
		pos_end = mp_encode_str(pos_end, engine, engine_len);
	*mp_pair = pos;
	*mp_pair_end = pos_end;
}

static int
sql_set_boolean_option(int id, bool value)
{
	struct session *session = current_session();
	struct sql_option_metadata *option =
		&sql_session_opts[id - SESSION_SETTING_SQL_BEGIN];
	assert(option->field_type == FIELD_TYPE_BOOLEAN);
#ifdef NDEBUG
	if ((session->sql_flags & SQL_SqlTrace) == 0) {
		if (value)
			session->sql_flags |= option->mask;
		else
			session->sql_flags &= ~option->mask;
	}
#else
	if (value)
		session->sql_flags |= option->mask;
	else
		session->sql_flags &= ~option->mask;
	if (id == SESSION_SETTING_SQL_PARSER_DEBUG) {
		if (value)
			sqlParserTrace(stdout, "parser: ");
		else
			sqlParserTrace(NULL, NULL);
	}
#endif
	return 0;
}

static int
sql_set_string_option(int id, const char *value)
{
	assert(sql_session_opts[id - SESSION_SETTING_SQL_BEGIN].field_type =
	       FIELD_TYPE_STRING);
	assert(id == SESSION_SETTING_SQL_DEFAULT_ENGINE);
	(void)id;
	enum sql_storage_engine engine = STR2ENUM(sql_storage_engine, value);
	if (engine == sql_storage_engine_MAX) {
		diag_set(ClientError, ER_NO_SUCH_ENGINE, value);
		return -1;
	}
	current_session()->sql_default_engine = engine;
	return 0;
}

static int
sql_session_setting_set(int id, const char *mp_value)
{
	assert(id >= SESSION_SETTING_SQL_BEGIN && id < SESSION_SETTING_SQL_END);
	enum mp_type mtype = mp_typeof(*mp_value);
	enum field_type stype =
		sql_session_opts[id - SESSION_SETTING_SQL_BEGIN].field_type;
	uint32_t len;
	const char *tmp;
	switch(stype) {
	case FIELD_TYPE_BOOLEAN:
		if (mtype != MP_BOOL)
			break;
		return sql_set_boolean_option(id, mp_decode_bool(&mp_value));
	case FIELD_TYPE_STRING:
		if (mtype != MP_STR)
			break;
		tmp = mp_decode_str(&mp_value, &len);
		tmp = tt_cstr(tmp, len);
		return sql_set_string_option(id, tmp);
	default:
		unreachable();
	}
	diag_set(ClientError, ER_SESSION_SETTING_INVALID_VALUE,
		 session_setting_strs[id], field_type_strs[stype]);
	return -1;
}

void
sql_session_settings_init(void)
{
	for (int i = 0, id = SESSION_SETTING_SQL_BEGIN;
	     id < SESSION_SETTING_SQL_END; ++id, ++i) {
		struct session_setting *setting = &session_settings[id];
		setting->field_type = sql_session_opts[i].field_type;
		setting->get = sql_session_setting_get;
		setting->set = sql_session_setting_set;
	}
}

void
sql_setting_set(struct Parse *parse_context, struct Token *name,
		struct Expr *expr)
{
	struct Vdbe *vdbe = sqlGetVdbe(parse_context);
	if (vdbe == NULL)
		goto abort;
	sqlVdbeCountChanges(vdbe);
	char *key = sql_name_from_token(parse_context->db, name);
	if (key == NULL)
		goto abort;
	int target = ++parse_context->nMem;
	sqlExprCode(parse_context, expr, target);
	sqlVdbeAddOp4(vdbe, OP_SetSession, target, 0, 0, key, P4_DYNAMIC);
	return;
abort:
	parse_context->is_aborted = true;
	return;
}
