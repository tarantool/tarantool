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
#include "box/fkey.h"
#include "box/schema.h"
#include "box/coll_id_cache.h"
#include "sqliteInt.h"
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
 * should be passed into sqlite3BtreeSetSafetyLevel().  The is done
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
	if (sqlite3Isdigit(*z)) {
		return (u8) sqlite3Atoi(z);
	}
	n = sqlite3Strlen30(z);
	for (i = 0; i < ArraySize(iLength); i++) {
		if (iLength[i] == n
		    && sqlite3StrNICmp(&zText[iOffset[i]], z, n) == 0
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
sqlite3GetBoolean(const char *z, u8 dflt)
{
	return getSafetyLevel(z, 1, dflt) != 0;
}

/* The sqlite3GetBoolean() function is used by other modules but the
 * remainder of this file is specific to PRAGMA processing.  So omit
 * the rest of the file if PRAGMAs are omitted from the build.
 */

/*
 * Set result column names for a pragma.
 */
static void
setPragmaResultColumnNames(Vdbe * v,	/* The query under construction */
			   const PragmaName * pPragma	/* The pragma */
    )
{
	u8 n = pPragma->nPragCName;
	sqlite3VdbeSetNumCols(v, n == 0 ? 1 : n);
	if (n == 0) {
		sqlite3VdbeSetColName(v, 0, COLNAME_NAME, pPragma->zName,
				      SQLITE_STATIC);
	} else {
		int i, j;
		for (i = 0, j = pPragma->iPragCName; i < n; i++, j++) {
			sqlite3VdbeSetColName(v, i, COLNAME_NAME, pragCName[j],
					      SQLITE_STATIC);
		}
	}
}

/*
 * Generate code to return a single integer value.
 */
static void
returnSingleInt(Vdbe * v, i64 value)
{
	sqlite3VdbeAddOp4Dup8(v, OP_Int64, 0, 1, 0, (const u8 *)&value,
			      P4_INT64);
	sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 1);
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
		rc = sqlite3_stricmp(zName, aPragmaName[mid].zName);
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

#ifdef PRINT_PRAGMA
#undef PRINT_PRAGMA
#endif
#define PRINT_PRAGMA(pragma_name, pragma_flag) do {			       \
	int nCoolSpaces = 30 - strlen(pragma_name);			       \
	if (user_session->sql_flags & (pragma_flag)) {			       \
		printf("%s %*c --  [true] \n", pragma_name, nCoolSpaces, ' '); \
	} else {							       \
		printf("%s %*c --  [false] \n", pragma_name, nCoolSpaces, ' ');\
	}								       \
} while (0)

#define PRINT_STR_PRAGMA(pragma_name, str_value) do {			       \
	int nCoolSpaces = 30 - strlen(pragma_name);			       \
	printf("%s %*c --  '%s' \n", pragma_name, nCoolSpaces, ' ', str_value);\
} while (0)

static void
printActivePragmas(struct session *user_session)
{
	int i;
	for (i = 0; i < ArraySize(aPragmaName); ++i) {
		switch (aPragmaName[i].ePragTyp) {
			case PragTyp_FLAG:
				PRINT_PRAGMA(aPragmaName[i].zName, aPragmaName[i].iArg);
				break;
			case PragTyp_DEFAULT_ENGINE: {
				const char *engine_name =
					sql_storage_engine_strs[
						current_session()->
							sql_default_engine];
				PRINT_STR_PRAGMA(aPragmaName[i].zName,
						 engine_name);
				break;
			}
		}
	}

	printf("Other available pragmas: \n");
	for (i = 0; i < ArraySize(aPragmaName); ++i) {
		if (aPragmaName[i].ePragTyp != PragTyp_FLAG)
			printf("-- %s \n", aPragmaName[i].zName);
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
	uint32_t space_id = box_space_id_by_name(tbl_name, strlen(tbl_name));
	if (space_id == BOX_ID_NIL)
		return;
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	struct index *pk = space_index(space, 0);
	parse->nMem = 6;
	if (space->def->opts.is_view)
		sql_view_assign_cursors(parse, space->def->opts.sql);
	struct Vdbe *v = sqlite3GetVdbe(parse);
	struct field_def *field = space->def->fields;
	for (uint32_t i = 0, k; i < space->def->field_count; ++i, ++field) {
		if (!sql_space_column_is_in_pk(space, i)) {
			k = 0;
		} else if (pk == NULL) {
			k = 1;
		} else {
			struct key_def *kdef = pk->def->key_def;
			k = key_def_find(kdef, i) - kdef->parts + 1;
		}
		sqlite3VdbeMultiLoad(v, 1, "issisi", i, field->name,
				     field_type_strs[field->type],
				     !field->is_nullable, field->default_value,
				     k);
		sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 6);
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
	if (space->def->opts.sql == NULL || space->def->opts.is_view)
		return 0;
	struct Parse *parse = (struct Parse *) data;
	struct index *pk = space_index(space, 0);
	if (pk == NULL)
		return 0;
	struct Vdbe *v = sqlite3GetVdbe(parse);
	LogEst tuple_count_est = sqlite3LogEst(index_size(pk));
	size_t avg_tuple_size_pk = sql_index_tuple_size(space, pk);
	parse->nMem = 4;
	sqlite3VdbeMultiLoad(v, 1, "ssii", space->def->name, 0,
			     avg_tuple_size_pk, tuple_count_est);
	sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 4);
	for (uint32_t i = 0; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		assert(idx != NULL);
		size_t avg_tuple_size_idx = sql_index_tuple_size(space, idx);
		sqlite3VdbeMultiLoad(v, 2, "sii", idx->def->name,
				     avg_tuple_size_idx,
				     index_field_tuple_est(idx->def, 0));
		sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 4);
	}
	return 0;
}

/**
 * This function handles PRAGMA INDEX_INFO and PRAGMA INDEX_XINFO
 * statements.
 *
 * @param parse Current parsing content.
 * @param pragma Definition of index_info pragma.
 * @param table_name Name of table index belongs to.
 * @param idx_name Name of index to display info about.
 */
static void
sql_pragma_index_info(struct Parse *parse, const PragmaName *pragma,
		      const char *tbl_name, const char *idx_name)
{
	if (idx_name == NULL || tbl_name == NULL)
		return;
	uint32_t space_id = box_space_id_by_name(tbl_name, strlen(tbl_name));
	if (space_id == BOX_ID_NIL)
		return;
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	if (space->def->opts.sql == NULL)
		return;
	uint32_t iid = box_index_id_by_name(space_id, idx_name,
					     strlen(idx_name));
	if (iid == BOX_ID_NIL)
		return;
	struct index *idx = space_index(space, iid);
	assert(idx != NULL);
	/* PRAGMA index_xinfo (more informative version). */
	if (pragma->iArg > 0) {
		parse->nMem = 6;
	} else {
		/* PRAGMA index_info ... */
		parse->nMem = 3;
	}
	struct Vdbe *v = sqlite3GetVdbe(parse);
	assert(v != NULL);
	uint32_t part_count = idx->def->key_def->part_count;
	assert(parse->nMem <= pragma->nPragCName);
	struct key_part *part = idx->def->key_def->parts;
	for (uint32_t i = 0; i < part_count; i++, part++) {
		sqlite3VdbeMultiLoad(v, 1, "iis", i, part->fieldno,
				     space->def->fields[part->fieldno].name);
		if (pragma->iArg > 0) {
			const char *c_n;
			uint32_t id = part->coll_id;
			struct coll *coll = part->coll;
			if (coll != NULL)
				c_n = coll_by_id(id)->name;
			else
				c_n = "BINARY";
			sqlite3VdbeMultiLoad(v, 4, "isi", part->sort_order,
					     c_n, i < part_count);
		}
		sqlite3VdbeAddOp2(v, OP_ResultRow, 1, parse->nMem);
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
	uint32_t space_id = box_space_id_by_name(tbl_name, strlen(tbl_name));
	if (space_id == BOX_ID_NIL)
		return;
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	parse->nMem = 5;
	struct Vdbe *v = sqlite3GetVdbe(parse);
	for (uint32_t i = 0; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		sqlite3VdbeMultiLoad(v, 1, "isisi", i, idx->def->name,
				     idx->def->opts.is_unique);
		sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 5);
	}
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
sqlite3Pragma(Parse * pParse, Token * pId,	/* First part of [schema.]id field */
	      Token * pValue,	/* Token for <value>, or NULL */
	      Token * pValue2,	/* Token for <value2>, or NULL */
	      int minusFlag	/* True if a '-' sign preceded <value> */
    )
{
	char *zLeft = 0;	/* Nul-terminated UTF-8 string <id> */
	char *zRight = 0;	/* Nul-terminated UTF-8 string <value>, or NULL */
	char *zTable = 0;	/* Nul-terminated UTF-8 string <value2> or NULL */
	int rc;			/* return value form SQLITE_FCNTL_PRAGMA */
	sqlite3 *db = pParse->db;	/* The database connection */
	Vdbe *v = sqlite3GetVdbe(pParse);	/* Prepared statement */
	const PragmaName *pPragma;	/* The pragma */
	struct session *user_session = current_session();

	if (v == 0)
		return;
	sqlite3VdbeRunOnlyOnce(v);
	pParse->nMem = 2;

	zLeft = sqlite3NameFromToken(db, pId);
	if (!zLeft) {
		printActivePragmas(user_session);
		return;
	}

	if (minusFlag) {
		zRight = sqlite3MPrintf(db, "-%T", pValue);
	} else {
		zRight = sqlite3NameFromToken(db, pValue);
	}
	zTable = sqlite3NameFromToken(db, pValue2);
	db->busyHandler.nBusy = 0;

	/* Locate the pragma in the lookup table */
	pPragma = pragmaLocate(zLeft);
	if (pPragma == 0) {
		sqlite3ErrorMsg(pParse, "no such pragma: %s", zLeft);
		goto pragma_out;
	}

	/* Make sure the database schema is loaded if the pragma requires that */
	if ((pPragma->mPragFlg & PragFlg_NeedSchema) != 0) {
		assert(db->pSchema != NULL);
	}
	/* Register the result column names for pragmas that return results */
	if ((pPragma->mPragFlg & PragFlg_NoColumns) == 0
	    && ((pPragma->mPragFlg & PragFlg_NoColumns1) == 0 || zRight == 0)
	    ) {
		setPragmaResultColumnNames(v, pPragma);
	}
	/* Jump to the appropriate pragma handler */
	switch (pPragma->ePragTyp) {

#ifndef SQLITE_OMIT_FLAG_PRAGMAS
	case PragTyp_FLAG:{
			if (zRight == 0) {
				setPragmaResultColumnNames(v, pPragma);
				returnSingleInt(v,
						(user_session->
						 sql_flags & pPragma->iArg) !=
						0);
			} else {
				int mask = pPragma->iArg;	/* Mask of bits to set
								 * or clear.
								 */

				if (sqlite3GetBoolean(zRight, 0)) {
					user_session->sql_flags |= mask;
				} else {
					user_session->sql_flags &= ~mask;
				}

				/* Many of the flag-pragmas modify the code
				 * generated by the SQL * compiler (eg.
				 * count_changes). So add an opcode to expire
				 * all * compiled SQL statements after
				 * modifying a pragma value.
				 */
				sqlite3VdbeAddOp0(v, OP_Expire);
			}
			break;
		}
#endif				/* SQLITE_OMIT_FLAG_PRAGMAS */

#ifndef SQLITE_OMIT_SCHEMA_PRAGMAS
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
		uint32_t space_id;
		space_id = box_space_id_by_name("_collation",
						(uint32_t) strlen("_collation"));
		char key_buf[16]; /* 16 is enough to encode 0 len array */
		char *key_end = key_buf;
		key_end = mp_encode_array(key_end, 0);
		box_tuple_t *tuple;
		box_iterator_t* iter;
		iter = box_index_iterator(space_id, 0,ITER_ALL, key_buf, key_end);
		rc = box_iterator_next(iter, &tuple);
		assert(rc==0);
		for (i = 0; tuple!=NULL; i++, box_iterator_next(iter, &tuple)){
			/* 1 is name field number */
			const char *str = tuple_field_cstr(tuple, 1);
			assert(str != NULL);
			/* this procedure should reallocate and copy str */
			sqlite3VdbeMultiLoad(v, 1, "is", i, str);
			sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 2);
		}
		box_iterator_free(iter);
		break;
	}
#endif				/* SQLITE_OMIT_SCHEMA_PRAGMAS */

	case PragTyp_FOREIGN_KEY_LIST:{
		if (zRight == NULL)
			break;
		uint32_t space_id = box_space_id_by_name(zRight,
							 strlen(zRight));
		if (space_id == BOX_ID_NIL)
			break;
		struct space *space = space_by_id(space_id);
		int i = 0;
		pParse->nMem = 8;
		struct fkey *fkey;
		rlist_foreach_entry(fkey, &space->child_fkey, child_link) {
			struct fkey_def *fdef = fkey->def;
			for (uint32_t j = 0; j < fdef->field_count; j++) {
				struct space *parent =
					space_by_id(fdef->parent_id);
				assert(parent != NULL);
				uint32_t ch_fl = fdef->links[j].child_field;
				const char *child_col =
					space->def->fields[ch_fl].name;
				uint32_t pr_fl = fdef->links[j].parent_field;
				const char *parent_col =
					parent->def->fields[pr_fl].name;
				sqlite3VdbeMultiLoad(v, 1, "iissssss", i, j,
						     parent->def->name,
						     child_col, parent_col,
						     fkey_action_strs[fdef->on_delete],
						     fkey_action_strs[fdef->on_update],
						     "NONE");
				sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 8);
			}
			++i;
		}
		break;
	}
#ifndef NDEBUG
	case PragTyp_PARSER_TRACE:{
			if (zRight) {
				if (sqlite3GetBoolean(zRight, 0)) {
					sqlite3ParserTrace(stdout, "parser: ");
				} else {
					sqlite3ParserTrace(0, 0);
				}
			}
			break;
		}
#endif

		/* Reinstall the LIKE and GLOB functions.  The variant of LIKE *
		 * used will be case sensitive or not depending on the RHS.
		 */
	case PragTyp_CASE_SENSITIVE_LIKE:{
			if (zRight) {
				sqlite3RegisterLikeFunctions(db,
							     sqlite3GetBoolean
							     (zRight, 0));
			}
			break;
		}

	case PragTyp_DEFAULT_ENGINE: {
		if (sql_default_engine_set(zRight) != 0) {
			pParse->rc = SQL_TARANTOOL_ERROR;
			pParse->nErr++;
			goto pragma_out;
		}
		sqlite3VdbeAddOp0(v, OP_Expire);
		break;
	}

	/* *   PRAGMA busy_timeout *   PRAGMA busy_timeout = N *
	 *
	 * Call sqlite3_busy_timeout(db, N).  Return the current
	 * timeout value * if one is set.  If no busy handler
	 * or a different busy handler is set * then 0 is
	 * returned.  Setting the busy_timeout to 0 or negative *
	 * disables the timeout.
	 */
	/* case PragTyp_BUSY_TIMEOUT */
	default:{
			assert(pPragma->ePragTyp == PragTyp_BUSY_TIMEOUT);
			if (zRight) {
				sqlite3_busy_timeout(db, sqlite3Atoi(zRight));
			}
			returnSingleInt(v, db->busyTimeout);
			break;
		}
	}			/* End of the PRAGMA switch */

	/* The following block is a no-op unless SQLITE_DEBUG is
	 * defined. Its only * purpose is to execute assert()
	 * statements to verify that if the * PragFlg_NoColumns1 flag
	 * is set and the caller specified an argument * to the PRAGMA,
	 * the implementation has not added any OP_ResultRow *
	 * instructions to the VM.
	 */
	if ((pPragma->mPragFlg & PragFlg_NoColumns1) && zRight) {
		sqlite3VdbeVerifyNoResultRow(v);
	}
 pragma_out:
	sqlite3DbFree(db, zLeft);
	sqlite3DbFree(db, zRight);
	sqlite3DbFree(db, zTable);
}
