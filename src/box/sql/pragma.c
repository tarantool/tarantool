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
 * This file contains code used to implement the PRAGMA command.
 */
#include "box/index.h"
#include "box/box.h"
#include "box/tuple.h"
#include "box/fk_constraint.h"
#include "box/schema.h"
#include "box/coll_id_cache.h"
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "vdbeInt.h"
#include "box/schema.h"
#include "box/session.h"

/*
 ************************************************************************
 * pragma.h contains several pragmas, including utf's pragmas.
 * All that is not utf-8 should be omitted
 ************************************************************************
 */

/***************************************************************************
 * The "pragma.h" include file is an automatically generated file that
 * that includes the PragType_XXXX macro definitions and the aPragmaName[]
 * object.  This ensures that the aPragmaName[] table is arranged in
 * lexicographical order to facility a binary search of the pragma name.
 * Do not edit pragma.h directly.  Edit and rerun the script in at
 * ../tool/mkpragmatab.tcl.
 */
#include "pragma.h"
#include "tarantoolInt.h"

/*
 * Interpret the given string as a safety level.  Return 0 for OFF,
 * 1 for ON or NORMAL, 2 for FULL, and 3 for EXTRA.  Return 1 for an empty or
 * unrecognized string argument.  The FULL and EXTRA option is disallowed
 * if the omitFull parameter it 1.
 *
 * Note that the values returned are one less that the values that
 * should be passed into sqlBtreeSetSafetyLevel().  The is done
 * to support legacy SQL code.  The safety level used to be boolean
 * and older scripts may have used numbers 0 for OFF and 1 for ON.
 */
static u8
getSafetyLevel(const char *z, int omitFull, u8 dflt)
{
	/* 123456789 123456789 123 */
	static const char zText[] = "onoffalseyestruextrafull";
	static const u8 iOffset[] = { 0, 1, 2, 4, 9, 12, 15, 20 };
	static const u8 iLength[] = { 2, 2, 3, 5, 3, 4, 5, 4 };
	static const u8 iValue[] = { 1, 0, 0, 0, 1, 1, 3, 2 };
	/* on no off false yes true extra full */
	int i, n;
	if (sqlIsdigit(*z)) {
		return (u8) sqlAtoi(z);
	}
	n = sqlStrlen30(z);
	for (i = 0; i < ArraySize(iLength); i++) {
		if (iLength[i] == n
		    && sqlStrNICmp(&zText[iOffset[i]], z, n) == 0
		    && (!omitFull || iValue[i] <= 1)
		    ) {
			return iValue[i];
		}
	}
	return dflt;
}

/*
 * Interpret the given string as a boolean value.
 */
u8
sqlGetBoolean(const char *z, u8 dflt)
{
	return getSafetyLevel(z, 1, dflt) != 0;
}

/* The sqlGetBoolean() function is used by other modules but the
 * remainder of this file is specific to PRAGMA processing.  So omit
 * the rest of the file if PRAGMAs are omitted from the build.
 */

/** Set result column names and types for a pragma. */
static void
vdbe_set_pragma_result_columns(struct Vdbe *v, const struct PragmaName *pragma)
{
	int n = pragma->nPragCName;
	assert(n > 0);
	sqlVdbeSetNumCols(v, n);
	for (int i = 0, j = pragma->iPragCName; i < n; ++i) {
		sqlVdbeSetColName(v, i, COLNAME_NAME, pragCName[j++],
				  SQL_STATIC);
		sqlVdbeSetColName(v, i, COLNAME_DECLTYPE, pragCName[j++],
				  SQL_STATIC);
	}
}

/*
 * Generate code to return a single integer value.
 */
static void
returnSingleInt(Vdbe * v, i64 value)
{
	sqlVdbeAddOp4Dup8(v, OP_Int64, 0, 1, 0, (const u8 *)&value,
			  value < 0 ? P4_INT64 : P4_UINT64);
	sqlVdbeAddOp2(v, OP_ResultRow, 1, 1);
}

/*
 * Locate a pragma in the aPragmaName[] array.
 */
static const PragmaName *
pragmaLocate(const char *zName)
{
	int upr, lwr, mid, rc;
	lwr = 0;
	upr = ArraySize(aPragmaName) - 1;
	while (lwr <= upr) {
		mid = (lwr + upr) / 2;
		rc = sql_stricmp(zName, aPragmaName[mid].zName);
		if (rc == 0)
			break;
		if (rc < 0) {
			upr = mid - 1;
		} else {
			lwr = mid + 1;
		}
	}
	return lwr > upr ? 0 : &aPragmaName[mid];
}

static void
vdbe_emit_pragma_status(struct Parse *parse)
{
	struct Vdbe *v = sqlGetVdbe(parse);
	struct session *user_session = current_session();

	sqlVdbeSetNumCols(v, 2);
	sqlVdbeSetColName(v, 0, COLNAME_NAME, "pragma_name", SQL_STATIC);
	sqlVdbeSetColName(v, 0, COLNAME_DECLTYPE, "TEXT", SQL_STATIC);
	sqlVdbeSetColName(v, 1, COLNAME_NAME, "pragma_value", SQL_STATIC);
	sqlVdbeSetColName(v, 1, COLNAME_DECLTYPE, "INTEGER", SQL_STATIC);

	parse->nMem = 2;
	for (int i = 0; i < ArraySize(aPragmaName); ++i) {
		if (aPragmaName[i].ePragTyp != PragTyp_FLAG)
			continue;
		sqlVdbeAddOp4(v, OP_String8, 0, 1, 0, aPragmaName[i].zName, 0);
		int val = (user_session->sql_flags & aPragmaName[i].iArg) != 0;
		sqlVdbeAddOp2(v, OP_Integer, val, 2);
		sqlVdbeAddOp2(v, OP_ResultRow, 1, 2);
	}
}

/**
 * Set tarantool backend default engine for SQL interface.
 * @param engine_name to set default.
 * @retval -1 on error.
 * @retval 0 on success.
 */
static int
sql_default_engine_set(const char *engine_name)
{
	if (engine_name == NULL) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "'sql_default_engine' was not specified");
		return -1;
	}
	enum sql_storage_engine engine =
		STR2ENUM(sql_storage_engine, engine_name);
	if (engine == sql_storage_engine_MAX) {
		diag_set(ClientError, ER_NO_SUCH_ENGINE, engine_name);
		return -1;
	}
	current_session()->sql_default_engine = engine;
	return 0;
}

/**
 * This function handles PRAGMA TABLE_INFO(<table>).
 *
 * Return a single row for each column of the named table.
 * The columns of the returned data set are:
 *
 * - cid: Column id (numbered from left to right, starting at 0);
 * - name: Column name;
 * - type: Column declaration type;
 * - notnull: True if 'NOT NULL' is part of column declaration;
 * - dflt_value: The default value for the column, if any.
 *
 * @param parse Current parsing context.
 * @param tbl_name Name of table to be examined.
 */
static void
sql_pragma_table_info(struct Parse *parse, const char *tbl_name)
{
	if (tbl_name == NULL)
		return;
	struct space *space = space_by_name(tbl_name);
	if (space == NULL)
		return;
	struct index *pk = space_index(space, 0);
	parse->nMem = 6;
	if (space->def->opts.is_view)
		sql_view_assign_cursors(parse, space->def->opts.sql);
	struct Vdbe *v = sqlGetVdbe(parse);
	struct field_def *field = space->def->fields;
	for (uint32_t i = 0, k; i < space->def->field_count; ++i, ++field) {
		if (!sql_space_column_is_in_pk(space, i)) {
			k = 0;
		} else if (pk == NULL) {
			k = 1;
		} else {
			struct key_def *kdef = pk->def->key_def;
			k = key_def_find_by_fieldno(kdef, i) - kdef->parts + 1;
		}
		sqlVdbeMultiLoad(v, 1, "issisi", i, field->name,
				     field_type_strs[field->type],
				     !field->is_nullable, field->default_value,
				     k);
		sqlVdbeAddOp2(v, OP_ResultRow, 1, 6);
	}
}

/**
 * This function handles PRAGMA stats.
 * It displays estimate (log) number of tuples in space and
 * average size of tuple in each index.
 *
 * @param space Space to be examined.
 * @param data Parsing context passed as callback argument.
 */
static int
sql_pragma_table_stats(struct space *space, void *data)
{
	if (space->def->opts.is_view)
		return 0;
	struct Parse *parse = (struct Parse *) data;
	struct index *pk = space_index(space, 0);
	if (pk == NULL)
		return 0;
	struct Vdbe *v = sqlGetVdbe(parse);
	LogEst tuple_count_est = sqlLogEst(index_size(pk));
	size_t avg_tuple_size_pk = sql_index_tuple_size(space, pk);
	parse->nMem = 4;
	sqlVdbeMultiLoad(v, 1, "ssii", space->def->name, 0,
			     avg_tuple_size_pk, tuple_count_est);
	sqlVdbeAddOp2(v, OP_ResultRow, 1, 4);
	for (uint32_t i = 0; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		assert(idx != NULL);
		size_t avg_tuple_size_idx = sql_index_tuple_size(space, idx);
		sqlVdbeMultiLoad(v, 2, "sii", idx->def->name,
				     avg_tuple_size_idx,
				     index_field_tuple_est(idx->def, 0));
		sqlVdbeAddOp2(v, OP_ResultRow, 1, 4);
	}
	return 0;
}

/**
 * This function handles PRAGMA INDEX_INFO(<table>.<index>).
 *
 * Return a single row for each column of the index.
 * The columns of the returned data set are:
 *
 * - seqno: Zero-based column id within the index.
 * - cid: Zero-based column id within the table.
 * - name: Table column name.
 * - desc: Whether sorting by the column is descending (1 or 0).
 * - coll: Collation name.
 * - type: Type of a column value.
 *
 * @param parse Current parsing content.
 * @param pragma Definition of index_info pragma.
 * @param tbl_name Name of table index belongs to.
 * @param idx_name Name of index to display info about.
 */
static void
sql_pragma_index_info(struct Parse *parse,
		      MAYBE_UNUSED const PragmaName *pragma,
		      const char *tbl_name, const char *idx_name)
{
	if (idx_name == NULL || tbl_name == NULL)
		return;
	struct space *space = space_by_name(tbl_name);
	if (space == NULL)
		return;
	uint32_t iid = box_index_id_by_name(space->def->id, idx_name,
					    strlen(idx_name));
	if (iid == BOX_ID_NIL)
		return;
	struct index *idx = space_index(space, iid);
	assert(idx != NULL);
	parse->nMem = 6;
	struct Vdbe *v = sqlGetVdbe(parse);
	assert(v != NULL);
	uint32_t part_count = idx->def->key_def->part_count;
	assert(parse->nMem <= pragma->nPragCName);
	struct key_part *part = idx->def->key_def->parts;
	for (uint32_t i = 0; i < part_count; i++, part++) {
		const char *c_n;
		uint32_t id = part->coll_id;
		struct coll *coll = part->coll;
		if (coll != NULL)
			c_n = coll_by_id(id)->name;
		else
			c_n = "BINARY";
		uint32_t fieldno = part->fieldno;
		enum field_type type = space->def->fields[fieldno].type;
		sqlVdbeMultiLoad(v, 1, "iisiss", i, fieldno,
				     space->def->fields[fieldno].name,
				     part->sort_order, c_n,
				     field_type_strs[type]);
		sqlVdbeAddOp2(v, OP_ResultRow, 1, parse->nMem);
	}
}

/**
 * This function handles PRAGMA INDEX_LIST statement.
 *
 * @param parse Current parsing content.
 * @param table_name Name of table to display list of indexes.
 */
void
sql_pragma_index_list(struct Parse *parse, const char *tbl_name)
{
	if (tbl_name == NULL)
		return;
	struct space *space = space_by_name(tbl_name);
	if (space == NULL)
		return;
	parse->nMem = 3;
	struct Vdbe *v = sqlGetVdbe(parse);
	for (uint32_t i = 0; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		sqlVdbeMultiLoad(v, 1, "isi", i, idx->def->name,
				     idx->def->opts.is_unique);
		sqlVdbeAddOp2(v, OP_ResultRow, 1, 3);
	}
}

/*
 * @brief Check whether the specified token is a string or ID.
 * @param token - token to be examined
 * @return true - if the token value is enclosed into quotes (')
 * @return false in other cases
 * The empty value is considered to be a string.
 */
static bool
token_is_string(const struct Token* token)
{
	if (!token || token->n == 0)
		return true;
	return token->n >= 2 && token->z[0] == '\'' &&
	       token->z[token->n - 1] == '\'';
}

/*
 * Process a pragma statement.
 *
 * Pragmas are of this form:
 *
 *      PRAGMA [schema.]id [= value]
 *
 * The identifier might also be a string.  The value is a string, and
 * identifier, or a number.  If minusFlag is true, then the value is
 * a number that was preceded by a minus sign.
 *
 * If the left side is "database.id" then pId1 is the database name
 * and pId2 is the id.  If the left side is just "id" then pId1 is the
 * id and pId2 is any empty string.
 */
void
sqlPragma(Parse * pParse, Token * pId,	/* First part of [schema.]id field */
	      Token * pValue,	/* Token for <value>, or NULL */
	      Token * pValue2,	/* Token for <value2>, or NULL */
	      int minusFlag	/* True if a '-' sign preceded <value> */
    )
{
	char *zLeft = 0;	/* Nul-terminated UTF-8 string <id> */
	char *zRight = 0;	/* Nul-terminated UTF-8 string <value>, or NULL */
	char *zTable = 0;	/* Nul-terminated UTF-8 string <value2> or NULL */
	sql *db = pParse->db;	/* The database connection */
	Vdbe *v = sqlGetVdbe(pParse);	/* Prepared statement */
	const PragmaName *pPragma;	/* The pragma */
	struct session *user_session = current_session();

	if (v == 0)
		return;
	sqlVdbeRunOnlyOnce(v);
	pParse->nMem = 2;

	if (pId == NULL) {
		vdbe_emit_pragma_status(pParse);
		return;
	}
	zLeft = sql_name_from_token(db, pId);
	if (zLeft == NULL) {
		pParse->is_aborted = true;
		goto pragma_out;
	}
	if (minusFlag) {
		zRight = sqlMPrintf(db, "-%T", pValue);
	} else if (pValue != NULL) {
		zRight = sql_name_from_token(db, pValue);
		if (zRight == NULL) {
			pParse->is_aborted = true;
			goto pragma_out;
		}
	}
	if (pValue2 != NULL) {
		zTable = sql_name_from_token(db, pValue2);
		if (zTable == NULL) {
			pParse->is_aborted = true;
			goto pragma_out;
		}
	}
	/* Locate the pragma in the lookup table */
	pPragma = pragmaLocate(zLeft);
	if (pPragma == 0) {
		diag_set(ClientError, ER_SQL_NO_SUCH_PRAGMA, zLeft);
		pParse->is_aborted = true;
		goto pragma_out;
	}
	/* Register the result column names for pragmas that return results */
	if ((pPragma->mPragFlg & PragFlg_NoColumns) == 0 &&
	    ((pPragma->mPragFlg & PragFlg_NoColumns1) == 0 || zRight == NULL))
		vdbe_set_pragma_result_columns(v, pPragma);
	/* Jump to the appropriate pragma handler */
	switch (pPragma->ePragTyp) {

	case PragTyp_FLAG:{
		if (zRight == NULL) {
			vdbe_set_pragma_result_columns(v, pPragma);
			returnSingleInt(v, (user_session->sql_flags &
					    pPragma->iArg) != 0);
		} else {
			/* Mask of bits to set or clear. */
			int mask = pPragma->iArg;
			bool is_pragma_set = sqlGetBoolean(zRight, 0);

			if (is_pragma_set)
				user_session->sql_flags |= mask;
			else
				user_session->sql_flags &= ~mask;
#if defined(SQL_DEBUG)
			if (mask == PARSER_TRACE_FLAG) {
				if (is_pragma_set)
					sqlParserTrace(stdout, "parser: ");
				else
					sqlParserTrace(0, 0);
			}
#endif
			/*
			 * Reinstall the LIKE and functions. The
			 * variant of LIKE * used will be case
			 * sensitive or not depending on the RHS.
			 */
			if (mask == LIKE_CASE_SENS_FLAG)
				sqlRegisterLikeFunctions(db, !is_pragma_set);
		}
		break;
	}

	case PragTyp_TABLE_INFO:
		sql_pragma_table_info(pParse, zRight);
		break;
	case PragTyp_STATS:
		space_foreach(sql_pragma_table_stats, (void *) pParse);
		break;
	case PragTyp_INDEX_INFO:
		sql_pragma_index_info(pParse, pPragma, zTable, zRight);
		break;
	case PragTyp_INDEX_LIST:
		sql_pragma_index_list(pParse, zRight);
		break;

	case PragTyp_COLLATION_LIST:{
		int i = 0;
		struct space *space = space_by_name("_collation");
		char key_buf[16]; /* 16 is enough to encode 0 len array */
		char *key_end = key_buf;
		key_end = mp_encode_array(key_end, 0);
		box_tuple_t *tuple;
		box_iterator_t* iter;
		iter = box_index_iterator(space->def->id, 0,ITER_ALL, key_buf, key_end);
		if (iter == NULL) {
			pParse->is_aborted = true;
			goto pragma_out;
		}
		int rc = box_iterator_next(iter, &tuple);
		(void) rc;
		assert(rc == 0);
		for (i = 0; tuple!=NULL; i++, box_iterator_next(iter, &tuple)){
			/* 1 is name field number */
			const char *str = tuple_field_cstr(tuple, 1);
			assert(str != NULL);
			/* this procedure should reallocate and copy str */
			sqlVdbeMultiLoad(v, 1, "is", i, str);
			sqlVdbeAddOp2(v, OP_ResultRow, 1, 2);
		}
		box_iterator_free(iter);
		break;
	}

	case PragTyp_FOREIGN_KEY_LIST:{
		if (zRight == NULL)
			break;
		struct space *space = space_by_name(zRight);
		if (space == NULL)
			break;
		int i = 0;
		pParse->nMem = 8;
		struct fk_constraint *fk_c;
		rlist_foreach_entry(fk_c, &space->child_fk_constraint,
				    in_child_space) {

			struct fk_constraint_def *fk_def = fk_c->def;
			for (uint32_t j = 0; j < fk_def->field_count; j++) {
				struct space *parent =
					space_by_id(fk_def->parent_id);
				assert(parent != NULL);
				uint32_t ch_fl = fk_def->links[j].child_field;
				const char *child_col =
					space->def->fields[ch_fl].name;
				uint32_t pr_fl = fk_def->links[j].parent_field;
				const char *parent_col =
					parent->def->fields[pr_fl].name;
				sqlVdbeMultiLoad(v, 1, "iissssss", i, j,
						     parent->def->name,
						     child_col, parent_col,
						     fk_constraint_action_strs[fk_def->on_delete],
						     fk_constraint_action_strs[fk_def->on_update],
						     "NONE");
				sqlVdbeAddOp2(v, OP_ResultRow, 1, 8);
			}
			++i;
		}
		break;
	}

	case PragTyp_DEFAULT_ENGINE: {
		if (!token_is_string(pValue)) {
			diag_set(ClientError, ER_ILLEGAL_PARAMS,
				 "string value is expected");
			pParse->is_aborted = true;
			goto pragma_out;
		}
		if (zRight == NULL) {
			const char *engine_name =
				sql_storage_engine_strs[current_session()->
							sql_default_engine];
			sqlVdbeLoadString(v, 1, engine_name);
			sqlVdbeAddOp2(v, OP_ResultRow, 1, 1);
		} else {
			if (sql_default_engine_set(zRight) != 0) {
				pParse->is_aborted = true;
				goto pragma_out;
			}
			sqlVdbeAddOp0(v, OP_Expire);
		}
		break;
	}

	case PragTyp_COMPOUND_SELECT_LIMIT: {
		if (zRight != NULL) {
			sql_limit(db, SQL_LIMIT_COMPOUND_SELECT,
				      sqlAtoi(zRight));
		}
		int retval =
			sql_limit(db, SQL_LIMIT_COMPOUND_SELECT, -1);
		returnSingleInt(v, retval);
		break;
	}

	default:
		unreachable();
	}			/* End of the PRAGMA switch */

	/* The following block is a no-op unless SQL_DEBUG is
	 * defined. Its only * purpose is to execute assert()
	 * statements to verify that if the * PragFlg_NoColumns1 flag
	 * is set and the caller specified an argument * to the PRAGMA,
	 * the implementation has not added any OP_ResultRow *
	 * instructions to the VM.
	 */
	if ((pPragma->mPragFlg & PragFlg_NoColumns1) && zRight) {
		sqlVdbeVerifyNoResultRow(v);
	}
 pragma_out:
	sqlDbFree(db, zLeft);
	sqlDbFree(db, zRight);
	sqlDbFree(db, zTable);
}
