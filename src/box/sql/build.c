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
 * This file contains C code routines that are called by the SQLite parser
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
#include "sqliteInt.h"
#include "vdbeInt.h"
#include "tarantoolInt.h"
#include "box/box.h"
#include "box/fkey.h"
#include "box/sequence.h"
#include "box/session.h"
#include "box/identifier.h"
#include "box/schema.h"
#include "box/tuple_format.h"
#include "box/coll_id_cache.h"

void
sql_finish_coding(struct Parse *parse_context)
{
	assert(parse_context->pToplevel == NULL);
	struct sqlite3 *db = parse_context->db;
	struct Vdbe *v = sqlite3GetVdbe(parse_context);
	sqlite3VdbeAddOp0(v, OP_Halt);
	if (db->mallocFailed || parse_context->nErr != 0) {
		if (parse_context->rc == SQLITE_OK)
			parse_context->rc = SQLITE_ERROR;
		return;
	}
	/*
	 * Begin by generating some termination code at the end
	 * of the vdbe program
	 */
	assert(!parse_context->isMultiWrite ||
	       sqlite3VdbeAssertMayAbort(v, parse_context->mayAbort));
	int last_instruction = v->nOp;
	if (parse_context->initiateTTrans)
		sqlite3VdbeAddOp0(v, OP_TTransaction);
	if (parse_context->pConstExpr != NULL) {
		assert(sqlite3VdbeGetOp(v, 0)->opcode == OP_Init);
		/*
		 * Code constant expressions that where
		 * factored out of inner loops.
		 */
		struct ExprList *exprs = parse_context->pConstExpr;
		parse_context->okConstFactor = 0;
		for (int i = 0; i < exprs->nExpr; ++i) {
			sqlite3ExprCode(parse_context, exprs->a[i].pExpr,
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
		sqlite3VdbeChangeP2(v, 0, last_instruction);
		sqlite3VdbeGoto(v, 1);
	}
	/* Get the VDBE program ready for execution. */
	if (parse_context->nErr == 0 && !db->mallocFailed) {
		assert(parse_context->iCacheLevel == 0);
		sqlite3VdbeMakeReady(v, parse_context);
		parse_context->rc = SQLITE_DONE;
	} else {
		parse_context->rc = SQLITE_ERROR;
	}
}

/*
 * Locate the in-memory structure that describes a particular database
 * table given the name of that table. Return NULL if not found.
 * Also leave an error message in pParse->zErrMsg.
 */
Table *
sqlite3LocateTable(Parse * pParse,	/* context in which to report errors */
		   u32 flags,	/* LOCATE_VIEW or LOCATE_NOERR */
		   const char *zName	/* Name of the table we are looking for */
    )
{
	Table *p;

	assert(pParse->db->pSchema != NULL);

	p = sqlite3HashFind(&pParse->db->pSchema->tblHash, zName);
	if (p == NULL) {
		const char *zMsg =
		    flags & LOCATE_VIEW ? "no such view" : "no such table";
		if ((flags & LOCATE_NOERR) == 0)
			sqlite3ErrorMsg(pParse, "%s: %s", zMsg, zName);
	}

	return p;
}

Index *
sqlite3LocateIndex(sqlite3 * db, const char *zName, const char *zTable)
{
	assert(zName);
	assert(zTable);

	Table *pTab = sqlite3HashFind(&db->pSchema->tblHash, zTable);

	if (pTab == NULL)
		return NULL;
	for (struct Index *idx = pTab->pIndex; idx != NULL; idx = idx->pNext) {
		if (strcmp(zName, idx->def->name) == 0)
			return idx;
	}
	return NULL;
}

/*
 * Reclaim the memory used by an index
 */
static void
freeIndex(sqlite3 * db, Index * p)
{
	if (p->def != NULL)
		index_def_delete(p->def);
	sqlite3DbFree(db, p);
}

/*
 * For the index called zIdxName which is found in the database,
 * unlike that index from its Table then remove the index from
 * the index hash table and free all memory structures associated
 * with the index.
 */
void
sqlite3UnlinkAndDeleteIndex(sqlite3 * db, Index * pIndex)
{
	assert(pIndex != 0);

	struct session *user_session = current_session();
	if (ALWAYS(pIndex)) {
		if (pIndex->pTable->pIndex == pIndex) {
			pIndex->pTable->pIndex = pIndex->pNext;
		} else {
			Index *p;
			/* Justification of ALWAYS();  The index must be on the list of
			 * indices.
			 */
			p = pIndex->pTable->pIndex;
			while (ALWAYS(p) && p->pNext != pIndex) {
				p = p->pNext;
			}
			if (ALWAYS(p && p->pNext == pIndex)) {
				p->pNext = pIndex->pNext;
			}
		}
		freeIndex(db, pIndex);
	}

	user_session->sql_flags |= SQLITE_InternChanges;
}

/*
 * Erase all schema information from all attached databases (including
 * "main" and "temp") for a single database connection.
 */
void
sqlite3ResetAllSchemasOfConnection(sqlite3 * db)
{
	struct session *user_session = current_session();
	if (db->pSchema) {
		sqlite3SchemaClear(db);
	}
	user_session->sql_flags &= ~SQLITE_InternChanges;
}

/*
 * This routine is called when a commit occurs.
 */
void
sqlite3CommitInternalChanges()
{
	current_session()->sql_flags &= ~SQLITE_InternChanges;
}

/*
 * Return true if given column is part of primary key.
 * If field number is less than 63, corresponding bit
 * in column mask is tested. Otherwise, check whether 64-th bit
 * in mask is set or not. If it is set, then iterate through
 * key parts of primary index and check field number.
 * In case it isn't set, there are no key columns among
 * the rest of fields.
 */
bool
table_column_is_in_pk(Table *table, uint32_t column)
{
	struct space *space = space_by_id(table->def->id);
	assert(space != NULL);

	/* Primary key always has index 0. */
	struct index *primary_idx = space_index(space, 0);
	/* Views don't have any indexes. */
	if (primary_idx == NULL)
		return false;
	struct index_def *idx_def = primary_idx->def;
	uint64_t pk_mask = idx_def->key_def->column_mask;
	if (column < 63) {
		return pk_mask & (((uint64_t) 1) << column);
	} else if ((pk_mask & (((uint64_t) 1) << 63)) != 0) {
		for (uint32_t i = 0; i < idx_def->key_def->part_count; ++i) {
			struct key_part *part = &idx_def->key_def->parts[i];
			if (part->fieldno == column)
				return true;
		}
	}
	return false;
}

/*
 * Remove the memory data structures associated with the given
 * Table.  No changes are made to disk by this routine.
 *
 * This routine just deletes the data structure.  It does not unlink
 * the table data structure from the hash table.  But it does destroy
 * memory structures of the indices and foreign keys associated with
 * the table.
 *
 * The db parameter is optional.  It is needed if the Table object
 * contains lookaside memory.  (Table objects in the schema do not use
 * lookaside memory, but some ephemeral Table objects do.)  Or the
 * db parameter can be used with db->pnBytesFreed to measure the memory
 * used by the Table object.
 */
static void SQLITE_NOINLINE
deleteTable(sqlite3 * db, Table * pTable)
{
	Index *pIndex, *pNext;

	/* Delete all indices associated with this table. */
	for (pIndex = pTable->pIndex; pIndex; pIndex = pNext) {
		pNext = pIndex->pNext;
		freeIndex(db, pIndex);
	}
	assert(pTable->def != NULL);
	/* Do not delete pTable->def allocated on region. */
	if (!pTable->def->opts.is_temporary)
		space_def_delete(pTable->def);
	else
		sql_expr_list_delete(db, pTable->def->opts.checks);
	sqlite3DbFree(db, pTable);
}

void
sqlite3DeleteTable(sqlite3 * db, Table * pTable)
{
	/* Do not delete the table until the reference count reaches zero. */
	if (!pTable)
		return;
	if (((!db || db->pnBytesFreed == 0) && (--pTable->nTabRef) > 0))
		return;
	deleteTable(db, pTable);
}

/*
 * Unlink the given table from the hash tables and the delete the
 * table structure with all its indices and foreign keys.
 */
void
sqlite3UnlinkAndDeleteTable(sqlite3 * db, const char *zTabName)
{
	Table *p;

	assert(db != 0);
	assert(zTabName);
	testcase(zTabName[0] == 0);	/* Zero-length table names are allowed */
	p = sqlite3HashInsert(&db->pSchema->tblHash, zTabName, 0);
	sqlite3DeleteTable(db, p);
}

/*
 * Given a token, return a string that consists of the text of that
 * token.  Space to hold the returned string
 * is obtained from sqliteMalloc() and must be freed by the calling
 * function.
 *
 * Any quotation marks (ex:  "name", 'name', [name], or `name`) that
 * surround the body of the token are removed.
 *
 * Tokens are often just pointers into the original SQL text and so
 * are not \000 terminated and are not persistent.  The returned string
 * is \000 terminated and is persistent.
 */
char *
sqlite3NameFromToken(sqlite3 * db, Token * pName)
{
	char *zName;
	if (pName) {
		zName = sqlite3DbStrNDup(db, (char *)pName->z, pName->n);
		sqlite3NormalizeName(zName);
	} else {
		zName = 0;
	}
	return zName;
}

/*
 * This routine is used to check if the UTF-8 string zName is a legal
 * unqualified name for an identifier.
 * Some objects may not be checked, because they are validated in Tarantool.
 * (e.g. table, index, column name of a real table)
 * All names are legal except those that cantain non-printable
 * characters or have length greater than BOX_NAME_MAX.
 */
int
sqlite3CheckIdentifierName(Parse *pParse, char *zName)
{
	ssize_t len = strlen(zName);

	if (len > BOX_NAME_MAX || identifier_check(zName, len) != 0) {
		sqlite3ErrorMsg(pParse,
				"identifier name is invalid: %s",
				zName);
		return SQLITE_ERROR;
	}
	return SQLITE_OK;
}

/*
 * Return the PRIMARY KEY index of a table
 */
Index *
sqlite3PrimaryKeyIndex(Table * pTab)
{
	Index *p;
	for (p = pTab->pIndex; p != NULL && !sql_index_is_primary(p);
	     p = p->pNext);
	return p;
}

/**
 * Create and initialize a new SQL Table object.
 * All memory except table object itself is allocated on region.
 * @param parser SQL Parser object.
 * @param name Table to create name.
 * @retval NULL on memory allocation error, Parser state is
 *         changed.
 * @retval not NULL on success.
 */
static Table *
sql_table_new(Parse *parser, char *name)
{
	struct Table *table = sql_ephemeral_table_new(parser, name);
	if (table == NULL)
		return NULL;

	strcpy(table->def->engine_name,
	       sql_storage_engine_strs[current_session()->sql_default_engine]);

	table->nTabRef = 1;
	return table;
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
 * The new table record is initialized and put in pParse->pNewTable.
 * As more of the CREATE TABLE statement is parsed, additional action
 * routines will be called to add more information to this record.
 * At the end of the CREATE TABLE statement, the sqlite3EndTable() routine
 * is called to complete the construction of the new table record.
 *
 * @param pParse Parser context.
 * @param pName1 First part of the name of the table or view.
 * @param noErr Do nothing if table already exists.
 */
void
sqlite3StartTable(Parse *pParse, Token *pName, int noErr)
{
	Table *pTable;
	char *zName = 0;	/* The name of the new table */
	sqlite3 *db = pParse->db;
	struct Vdbe *v = sqlite3GetVdbe(pParse);
	if (v == NULL)
		goto cleanup;
	sqlite3VdbeCountChanges(v);

	zName = sqlite3NameFromToken(db, pName);

	pParse->sNameToken = *pName;
	if (zName == 0)
		return;
	if (sqlite3CheckIdentifierName(pParse, zName) != SQLITE_OK)
		goto cleanup;

	assert(db->pSchema != NULL);
	pTable = sqlite3HashFind(&db->pSchema->tblHash, zName);
	if (pTable != NULL) {
		if (!noErr) {
			sqlite3ErrorMsg(pParse,
					"table %s already exists",
					zName);
		} else {
			assert(!db->init.busy || CORRUPT_DB);
		}
		goto cleanup;
	}

	pTable = sql_table_new(pParse, zName);
	if (pTable == NULL)
		goto cleanup;

	assert(pParse->pNewTable == 0);
	pParse->pNewTable = pTable;

	/* Begin generating the code that will create a new table.
	 * Note in particular that we must go ahead and allocate the
	 * record number for the table entry now.  Before any
	 * PRIMARY KEY or UNIQUE keywords are parsed.  Those keywords will cause
	 * indices to be created and the table record must come before the
	 * indices.  Hence, the record number for the table must be allocated
	 * now.
	 */
	if (!db->init.busy && (v = sqlite3GetVdbe(pParse)) != 0)
		sql_set_multi_write(pParse, true);

 cleanup:
	sqlite3DbFree(db, zName);
	return;
}

/**
 * Get field by id. Allocate memory if needed.
 * Useful in cases when initial field_count is unknown.
 * Allocated memory should by manually released.
 * @param parser SQL Parser object.
 * @param table SQL Table object.
 * @param id column identifier.
 * @retval not NULL on success.
 * @retval NULL on out of memory.
 */
static struct field_def *
sql_field_retrieve(Parse *parser, Table *table, uint32_t id)
{
	struct field_def *field;
	assert(table->def != NULL);
	assert(id < SQLITE_MAX_COLUMN);

	if (id >= table->def->exact_field_count) {
		uint32_t columns_new = table->def->exact_field_count;
		columns_new = (columns_new > 0) ? 2 * columns_new : 1;
		struct region *region = &parser->region;
		field = region_alloc(region, columns_new *
				     sizeof(table->def->fields[0]));
		if (field == NULL) {
			diag_set(OutOfMemory, columns_new *
				sizeof(table->def->fields[0]),
				 "region_alloc", "sql_field_retrieve");
			parser->rc = SQL_TARANTOOL_ERROR;
			parser->nErr++;
			return NULL;
		}

		memcpy(field, table->def->fields,
		       sizeof(*field) * table->def->exact_field_count);
		for (uint32_t i = columns_new / 2; i < columns_new; i++) {
			memcpy(&field[i], &field_def_default,
			       sizeof(struct field_def));
		}

		table->def->fields = field;
		table->def->exact_field_count = columns_new;
	}

	field = &table->def->fields[id];
	return field;
}

/*
 * Add a new column to the table currently being constructed.
 *
 * The parser calls this routine once for each column declaration
 * in a CREATE TABLE statement.  sqlite3StartTable() gets called
 * first to get things going.  Then this routine is called for each
 * column.
 */
void
sqlite3AddColumn(Parse * pParse, Token * pName, Token * pType)
{
	Table *p;
	int i;
	char *z;
	char *zType;
	sqlite3 *db = pParse->db;
	if ((p = pParse->pNewTable) == 0)
		return;
#if SQLITE_MAX_COLUMN
	if ((int)p->def->field_count + 1 > db->aLimit[SQLITE_LIMIT_COLUMN]) {
		sqlite3ErrorMsg(pParse, "too many columns on %s",
				p->def->name);
		return;
	}
#endif
	/*
	 * As sql_field_retrieve will allocate memory on region
	 * ensure that p->def is also temporal and would be rebuilded or
	 * dropped.
	 */
	assert(p->def->opts.is_temporary);
	if (sql_field_retrieve(pParse, p,
			       (uint32_t) p->def->field_count) == NULL)
		return;
	struct region *region = &pParse->region;
	z = region_alloc(region, pName->n + 1);
	if (z == NULL) {
		diag_set(OutOfMemory, pName->n + 1,
			 "region_alloc", "z");
		pParse->rc = SQL_TARANTOOL_ERROR;
		pParse->nErr++;
		return;
	}
	memcpy(z, pName->z, pName->n);
	z[pName->n] = 0;
	sqlite3NormalizeName(z);
	for (i = 0; i < (int)p->def->field_count; i++) {
		if (strcmp(z, p->def->fields[i].name) == 0) {
			sqlite3ErrorMsg(pParse, "duplicate column name: %s", z);
			return;
		}
	}
	struct field_def *column_def = &p->def->fields[p->def->field_count];
	memcpy(column_def, &field_def_default, sizeof(field_def_default));
	column_def->name = z;
	/*
	 * Marker ON_CONFLICT_ACTION_DEFAULT is used to detect
	 * attempts to define NULL multiple time or to detect
	 * invalid primary key definition.
	 */
	column_def->nullable_action = ON_CONFLICT_ACTION_DEFAULT;
	column_def->is_nullable = true;

	if (pType->n == 0) {
		/* If there is no type specified, columns have the default affinity
		 * 'BLOB' and type SCALAR.
		 * TODO: since SQL standard prohibits column creation without
		 * specified type, the code below should emit an error.
		 */
		column_def->affinity = AFFINITY_BLOB;
		column_def->type = FIELD_TYPE_SCALAR;
	} else {
		/* TODO: convert string of type into runtime
		 * FIELD_TYPE value for other types.
		 */
		if ((sqlite3StrNICmp(pType->z, "INTEGER", 7) == 0 &&
		     pType->n == 7) ||
		    (sqlite3StrNICmp(pType->z, "INT", 3) == 0 &&
		     pType->n == 3)) {
			column_def->type = FIELD_TYPE_INTEGER;
			column_def->affinity = AFFINITY_INTEGER;
		} else {
			zType = sqlite3_malloc(pType->n + 1);
			memcpy(zType, pType->z, pType->n);
			zType[pType->n] = 0;
			sqlite3Dequote(zType);
			column_def->affinity = sqlite3AffinityType(zType, 0);
			column_def->type = FIELD_TYPE_SCALAR;
			sqlite3_free(zType);
		}
	}
	p->def->field_count++;
	pParse->constraintName.n = 0;
}

void
sql_column_add_nullable_action(struct Parse *parser,
			       enum on_conflict_action nullable_action)
{
	struct Table *p = parser->pNewTable;
	if (p == NULL || NEVER(p->def->field_count < 1))
		return;
	struct field_def *field = &p->def->fields[p->def->field_count - 1];
	if (field->nullable_action != ON_CONFLICT_ACTION_DEFAULT &&
	    nullable_action != field->nullable_action) {
		/* Prevent defining nullable_action many times. */
		const char *err_msg =
			tt_sprintf("NULL declaration for column '%s' of table "
				   "'%s' has been already set to '%s'",
				   field->name, p->def->name,
				   on_conflict_action_strs[field->
							   nullable_action]);
		diag_set(ClientError, ER_SQL, err_msg);
		parser->rc = SQL_TARANTOOL_ERROR;
		parser->nErr++;
		return;
	}
	field->nullable_action = nullable_action;
	field->is_nullable = action_is_nullable(nullable_action);
}

/*
 * Scan the column type name zType (length nType) and return the
 * associated affinity type.
 *
 * This routine does a case-independent search of zType for the
 * substrings in the following table. If one of the substrings is
 * found, the corresponding affinity is returned. If zType contains
 * more than one of the substrings, entries toward the top of
 * the table take priority. For example, if zType is 'BLOBINT',
 * AFFINITY_INTEGER is returned.
 *
 * Substring     | Affinity
 * --------------------------------
 * 'INT'         | AFFINITY_INTEGER
 * 'CHAR'        | AFFINITY_TEXT
 * 'CLOB'        | AFFINITY_TEXT
 * 'TEXT'        | AFFINITY_TEXT
 * 'BLOB'        | AFFINITY_BLOB
 * 'REAL'        | AFFINITY_REAL
 * 'FLOA'        | AFFINITY_REAL
 * 'DOUB'        | AFFINITY_REAL
 *
 * If none of the substrings in the above table are found,
 * AFFINITY_NUMERIC is returned.
 */
char
sqlite3AffinityType(const char *zIn, u8 * pszEst)
{
	u32 h = 0;
	char aff = AFFINITY_NUMERIC;
	const char *zChar = 0;

	assert(zIn != 0);
	while (zIn[0]) {
		h = (h << 8) + sqlite3UpperToLower[(*zIn) & 0xff];
		zIn++;
		if (h == (('c' << 24) + ('h' << 16) + ('a' << 8) + 'r')) {	/* CHAR */
			aff = AFFINITY_TEXT;
			zChar = zIn;
		} else if (h == (('c' << 24) + ('l' << 16) + ('o' << 8) + 'b')) {	/* CLOB */
			aff = AFFINITY_TEXT;
		} else if (h == (('t' << 24) + ('e' << 16) + ('x' << 8) + 't')) {	/* TEXT */
			aff = AFFINITY_TEXT;
		} else if (h == (('b' << 24) + ('l' << 16) + ('o' << 8) + 'b')	/* BLOB */
			   &&(aff == AFFINITY_NUMERIC
			      || aff == AFFINITY_REAL)) {
			aff = AFFINITY_BLOB;
			if (zIn[0] == '(')
				zChar = zIn;
#ifndef SQLITE_OMIT_FLOATING_POINT
		} else if (h == (('r' << 24) + ('e' << 16) + ('a' << 8) + 'l')	/* REAL */
			   &&aff == AFFINITY_NUMERIC) {
			aff = AFFINITY_REAL;
		} else if (h == (('f' << 24) + ('l' << 16) + ('o' << 8) + 'a')	/* FLOA */
			   &&aff == AFFINITY_NUMERIC) {
			aff = AFFINITY_REAL;
		} else if (h == (('d' << 24) + ('o' << 16) + ('u' << 8) + 'b')	/* DOUB */
			   &&aff == AFFINITY_NUMERIC) {
			aff = AFFINITY_REAL;
#endif
		} else if ((h & 0x00FFFFFF) == (('i' << 16) + ('n' << 8) + 't')) {	/* INT */
			aff = AFFINITY_INTEGER;
			break;
		}
	}

	/* If pszEst is not NULL, store an estimate of the field size.  The
	 * estimate is scaled so that the size of an integer is 1.
	 */
	if (pszEst) {
		*pszEst = 1;	/* default size is approx 4 bytes
		*/
		if (aff < AFFINITY_NUMERIC) {
			if (zChar) {
				while (zChar[0]) {
					if (sqlite3Isdigit(zChar[0])) {
						int v = 0;
						sqlite3GetInt32(zChar, &v);
						v = v / 4 + 1;
						if (v > 255)
							v = 255;
						*pszEst = v;	/* BLOB(k), VARCHAR(k), CHAR(k) -> r=(k/4+1)
						*/
						break;
					}
					zChar++;
				}
			} else {
				*pszEst = 5;	/* BLOB, TEXT, CLOB -> r=5  (approx 20 bytes)
				*/
			}
		}
	}
	return aff;
}

/*
 * The expression is the default value for the most recently added column
 * of the table currently under construction.
 *
 * Default value expressions must be constant.  Raise an exception if this
 * is not the case.
 *
 * This routine is called by the parser while in the middle of
 * parsing a CREATE TABLE statement.
 */
void
sqlite3AddDefaultValue(Parse * pParse, ExprSpan * pSpan)
{
	Table *p;
	sqlite3 *db = pParse->db;
	p = pParse->pNewTable;
	assert(p->def->opts.is_temporary);
	if (p != 0) {
		if (!sqlite3ExprIsConstantOrFunction
		    (pSpan->pExpr, db->init.busy)) {
			sqlite3ErrorMsg(pParse,
					"default value of column [%s] is not constant",
					p->def->fields[p->def->field_count - 1].name);
		} else {
			assert(p->def != NULL);
			struct field_def *field =
				&p->def->fields[p->def->field_count - 1];
			struct region *region = &pParse->region;
			uint32_t default_length = (int)(pSpan->zEnd - pSpan->zStart);
			field->default_value = region_alloc(region,
							    default_length + 1);
			if (field->default_value == NULL) {
				diag_set(OutOfMemory, default_length + 1,
					 "region_alloc",
					 "field->default_value");
				pParse->rc = SQL_TARANTOOL_ERROR;
				pParse->nErr++;
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
		parser->rc = SQL_TARANTOOL_ERROR;
		parser->nErr++;
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
sqlite3AddPrimaryKey(Parse * pParse,	/* Parsing context */
		     ExprList * pList,	/* List of field names to be indexed */
		     int autoInc,	/* True if the AUTOINCREMENT keyword is present */
		     enum sort_order sortOrder
    )
{
	Table *pTab = pParse->pNewTable;
	int iCol = -1, i;
	int nTerm;
	if (pTab == 0)
		goto primary_key_exit;
	if (sqlite3PrimaryKeyIndex(pTab) != NULL) {
		sqlite3ErrorMsg(pParse,
				"table \"%s\" has more than one primary key",
				pTab->def->name);
		goto primary_key_exit;
	}
	if (pList == 0) {
		iCol = pTab->def->field_count - 1;
		nTerm = 1;
	} else {
		nTerm = pList->nExpr;
		for (i = 0; i < nTerm; i++) {
			Expr *pCExpr =
			    sqlite3ExprSkipCollate(pList->a[i].pExpr);
			assert(pCExpr != 0);
			if (pCExpr->op != TK_ID) {
				sqlite3ErrorMsg(pParse, "expressions prohibited"
							" in PRIMARY KEY");
				goto primary_key_exit;
			}
			const char *name = pCExpr->u.zToken;
			struct space_def *def = pTab->def;
			for (uint32_t idx = 0; idx < def->field_count; idx++) {
				if (strcmp(name, def->fields[idx].name) == 0) {
					iCol = idx;
					break;
				}
			}
		}
	}
	if (nTerm == 1 && iCol != -1 &&
	    pTab->def->fields[iCol].type == FIELD_TYPE_INTEGER &&
	    sortOrder != SORT_ORDER_DESC) {
		assert(autoInc == 0 || autoInc == 1);
		pParse->is_new_table_autoinc = autoInc;
		struct sqlite3 *db = pParse->db;
		struct ExprList *list;
		struct Token token;
		sqlite3TokenInit(&token, pTab->def->fields[iCol].name);
		list = sql_expr_list_append(db, NULL,
					    sqlite3ExprAlloc(db, TK_ID,
							     &token, 0));
		if (list == NULL)
			goto primary_key_exit;
		sql_create_index(pParse, 0, 0, list, 0, SORT_ORDER_ASC,
				 false, SQL_INDEX_TYPE_CONSTRAINT_PK);
		if (db->mallocFailed)
			goto primary_key_exit;
	} else if (autoInc) {
		sqlite3ErrorMsg(pParse, "AUTOINCREMENT is only allowed on an "
				"INTEGER PRIMARY KEY or INT PRIMARY KEY");
		goto primary_key_exit;
	} else {
		sql_create_index(pParse, 0, 0, pList, 0, sortOrder, false,
				 SQL_INDEX_TYPE_CONSTRAINT_PK);
		pList = 0;
		if (pParse->nErr > 0)
			goto primary_key_exit;
	}

	struct Index *pk = sqlite3PrimaryKeyIndex(pTab);
	assert(pk != NULL);
	struct key_def *pk_key_def = pk->def->key_def;
	for (uint32_t i = 0; i < pk_key_def->part_count; i++) {
		uint32_t idx = pk_key_def->parts[i].fieldno;
		field_def_create_for_pk(pParse, &pTab->def->fields[idx],
					pTab->def->name);
	}
primary_key_exit:
	sql_expr_list_delete(pParse->db, pList);
	return;
}

void
sql_add_check_constraint(struct Parse *parser, struct ExprSpan *span)
{
	struct Expr *expr = span->pExpr;
	struct Table *table = parser->pNewTable;
	if (table != NULL) {
		expr->u.zToken =
			sqlite3DbStrNDup(parser->db, (char *)span->zStart,
					 (int)(span->zEnd - span->zStart));
		if (expr->u.zToken == NULL)
			goto release_expr;
		table->def->opts.checks =
			sql_expr_list_append(parser->db,
					     table->def->opts.checks, expr);
		if (table->def->opts.checks == NULL) {
			sqlite3DbFree(parser->db, expr->u.zToken);
			goto release_expr;
		}
		if (parser->constraintName.n) {
			sqlite3ExprListSetName(parser, table->def->opts.checks,
					       &parser->constraintName, 1);
		}
	} else {
release_expr:
		sql_expr_delete(parser->db, expr, false);
	}
}

/*
 * Set the collation function of the most recently parsed table column
 * to the CollSeq given.
 */
void
sqlite3AddCollateType(Parse * pParse, Token * pToken)
{
	Table *p = pParse->pNewTable;
	if (p == NULL)
		return;
	uint32_t i = p->def->field_count - 1;
	sqlite3 *db = pParse->db;
	char *zColl = sqlite3NameFromToken(db, pToken);
	if (!zColl)
		return;
	uint32_t *coll_id = &p->def->fields[i].coll_id;
	if (sql_get_coll_seq(pParse, zColl, coll_id) != NULL) {
		/* If the column is declared as "<name> PRIMARY KEY COLLATE <type>",
		 * then an index may have been created on this column before the
		 * collation type was added. Correct this if it is the case.
		 */
		for (struct Index *pIdx = p->pIndex; pIdx != NULL;
		     pIdx = pIdx->pNext) {
			assert(pIdx->def->key_def->part_count == 1);
			if (pIdx->def->key_def->parts[0].fieldno == i) {
				coll_id = &pIdx->def->key_def->parts[0].coll_id;
				(void)sql_column_collation(p->def, i, coll_id);
			}
		}
	}
	sqlite3DbFree(db, zColl);
}

struct coll *
sql_column_collation(struct space_def *def, uint32_t column, uint32_t *coll_id)
{
	assert(def != NULL);
	struct space *space = space_by_id(def->id);
	/*
	 * It is not always possible to fetch collation directly
	 * from struct space. To be more precise when:
	 * 1. space is ephemeral. Thus, its id is zero and
	 *    it can't be found in space cache.
	 * 2. space is a view. Hence, it lacks any functional
	 *    parts such as indexes or fields.
	 * 3. space is under construction. So, the same as p.1
	 *    it can't be found in space cache.
	 * In cases mentioned above collation is fetched from
	 * SQL specific structures.
	 */
	if (space == NULL || space_index(space, 0) == NULL) {
		assert(column < (uint32_t)def->field_count);
		*coll_id = def->fields[column].coll_id;
		struct coll_id *collation = coll_by_id(*coll_id);
		return collation != NULL ? collation->coll : NULL;
	}
	*coll_id = space->format->fields[column].coll_id;
	return space->format->fields[column].coll;
}

struct ExprList *
space_checks_expr_list(uint32_t space_id)
{
	struct space *space;
	space = space_by_id(space_id);
	assert(space != NULL);
	assert(space->def != NULL);
	return space->def->opts.checks;
}

int
vdbe_emit_open_cursor(struct Parse *parse_context, int cursor, int index_id,
		      struct space *space)
{
	assert(space != NULL);
	return sqlite3VdbeAddOp4(parse_context->pVdbe, OP_OpenWrite, cursor,
				 index_id, 0, (void *) space, P4_SPACEPTR);
}

/*
 * Measure the number of characters needed to output the given
 * identifier.  The number returned includes any quotes used
 * but does not include the null terminator.
 *
 * The estimate is conservative.  It might be larger that what is
 * really needed.
 */
static int
identLength(const char *z)
{
	int n;
	for (n = 0; *z; n++, z++) {
		if (*z == '"') {
			n++;
		}
	}
	return n + 2;
}

/*
 * The first parameter is a pointer to an output buffer. The second
 * parameter is a pointer to an integer that contains the offset at
 * which to write into the output buffer. This function copies the
 * nul-terminated string pointed to by the third parameter, zSignedIdent,
 * to the specified offset in the buffer and updates *pIdx to refer
 * to the first byte after the last byte written before returning.
 *
 * If the string zSignedIdent consists entirely of alpha-numeric
 * characters, does not begin with a digit and is not an SQL keyword,
 * then it is copied to the output buffer exactly as it is. Otherwise,
 * it is quoted using double-quotes.
 */
static void
identPut(char *z, int *pIdx, char *zSignedIdent)
{
	unsigned char *zIdent = (unsigned char *)zSignedIdent;
	int i, j, needQuote;
	i = *pIdx;

	for (j = 0; zIdent[j]; j++) {
		if (!sqlite3Isalnum(zIdent[j]) && zIdent[j] != '_')
			break;
	}
	needQuote = sqlite3Isdigit(zIdent[0])
	    || sqlite3KeywordCode(zIdent, j) != TK_ID
	    || zIdent[j] != 0 || j == 0;

	if (needQuote)
		z[i++] = '"';
	for (j = 0; zIdent[j]; j++) {
		z[i++] = zIdent[j];
		if (zIdent[j] == '"')
			z[i++] = '"';
	}
	if (needQuote)
		z[i++] = '"';
	z[i] = 0;
	*pIdx = i;
}

/*
 * Generate a CREATE TABLE statement appropriate for the given
 * table.  Memory to hold the text of the statement is obtained
 * from sqliteMalloc() and must be freed by the calling function.
 */
static char *
createTableStmt(sqlite3 * db, Table * p)
{
	char *zStmt;
	char *zSep, *zSep2, *zEnd;
	int n = 0;
	for (uint32_t i = 0; i < p->def->field_count; i++)
		n += identLength(p->def->fields[i].name) + 5;
	n += identLength(p->def->name);
	if (n < 50) {
		zSep = "";
		zSep2 = ",";
		zEnd = ")";
	} else {
		zSep = "\n  ";
		zSep2 = ",\n  ";
		zEnd = "\n)";
	}
	n += 35 + 6 * p->def->field_count;
	zStmt = sqlite3DbMallocRaw(0, n);
	if (zStmt == 0) {
		sqlite3OomFault(db);
		return 0;
	}
	sqlite3_snprintf(n, zStmt, "CREATE TABLE ");
	int k = sqlite3Strlen30(zStmt);
	identPut(zStmt, &k, p->def->name);
	zStmt[k++] = '(';
	for (uint32_t i = 0; i < p->def->field_count; i++) {
		static const char *const azType[] = {
			/* AFFINITY_BLOB    */ "",
			/* AFFINITY_TEXT    */ " TEXT",
			/* AFFINITY_NUMERIC */ " NUM",
			/* AFFINITY_INTEGER */ " INT",
			/* AFFINITY_REAL    */ " REAL"
		};
		int len;
		const char *zType;

		sqlite3_snprintf(n - k, &zStmt[k], zSep);
		k += sqlite3Strlen30(&zStmt[k]);
		zSep = zSep2;
		identPut(zStmt, &k, p->def->fields[i].name);
		char affinity = p->def->fields[i].affinity;
		assert(affinity - AFFINITY_BLOB >= 0);
		assert(affinity - AFFINITY_BLOB < ArraySize(azType));
		testcase(affinity == AFFINITY_BLOB);
		testcase(affinity == AFFINITY_TEXT);
		testcase(affinity == AFFINITY_NUMERIC);
		testcase(affinity == AFFINITY_INTEGER);
		testcase(affinity == AFFINITY_REAL);

		zType = azType[affinity - AFFINITY_BLOB];
		len = sqlite3Strlen30(zType);
		assert(affinity == AFFINITY_BLOB ||
		       affinity == sqlite3AffinityType(zType, 0));
		memcpy(&zStmt[k], zType, len);
		k += len;
		assert(k <= n);
	}
	sqlite3_snprintf(n - k, &zStmt[k], "%s", zEnd);
	return zStmt;
}

/*
 * Generate code to determine the new space Id.
 * Fetch the max space id seen so far from _schema and increment it.
 * Return register storing the result.
 */
static int
getNewSpaceId(Parse * pParse)
{
	Vdbe *v = sqlite3GetVdbe(pParse);
	int iRes = ++pParse->nMem;

	sqlite3VdbeAddOp1(v, OP_IncMaxid, iRes);
	return iRes;
}

/*
 * Generate VDBE code to create an Index. This is acomplished by adding
 * an entry to the _index table. ISpaceId either contains the literal
 * space id or designates a register storing the id.
 */
static void
createIndex(Parse * pParse, Index * pIndex, int iSpaceId, int iIndexId,
	    const char *zSql)
{
	Vdbe *v = sqlite3GetVdbe(pParse);
	int iFirstCol = ++pParse->nMem;
	int iRecord = (pParse->nMem += 6);	/* 6 total columns */
	struct index_opts opts = pIndex->def->opts;
	opts.sql = (char *)zSql;
	struct region *region = &pParse->region;
	uint32_t index_opts_sz = 0;
	char *index_opts =
		sql_encode_index_opts(region, &opts, &index_opts_sz);
	if (index_opts == NULL)
		goto error;
	uint32_t index_parts_sz = 0;
	char *index_parts =
		sql_encode_index_parts(region, pIndex, &index_parts_sz);
	if (index_parts == NULL)
		goto error;
	char *raw = sqlite3DbMallocRaw(pParse->db,
				       index_opts_sz + index_parts_sz);
	if (raw == NULL)
		return;

	memcpy(raw, index_opts, index_opts_sz);
	index_opts = raw;
	raw += index_opts_sz;
	memcpy(raw, index_parts, index_parts_sz);
	index_parts = raw;

	if (pParse->pNewTable) {
		int reg;
		/*
		 * A new table is being created, hence iSpaceId is a register, but
		 * iIndexId is literal.
		 */
		sqlite3VdbeAddOp2(v, OP_SCopy, iSpaceId, iFirstCol);
		sqlite3VdbeAddOp2(v, OP_Integer, iIndexId, iFirstCol + 1);

		/* Generate code to save new pageno into a register.
		 * This is runtime implementation of SQLITE_PAGENO_FROM_SPACEID_AND_INDEXID:
		 *   pageno = (spaceid << 10) | indexid
		 */
		pParse->regRoot = ++pParse->nMem;
		reg = ++pParse->nMem;
		sqlite3VdbeAddOp2(v, OP_Integer, 1 << 10, reg);
		sqlite3VdbeAddOp3(v, OP_Multiply, reg, iSpaceId,
				  pParse->regRoot);
		sqlite3VdbeAddOp3(v, OP_AddImm, pParse->regRoot, iIndexId,
				  pParse->regRoot);
	} else {
		/*
		 * An existing table is being modified; iSpaceId is literal, but
		 * iIndexId is a register.
		 */
		sqlite3VdbeAddOp2(v, OP_Integer, iSpaceId, iFirstCol);
		sqlite3VdbeAddOp2(v, OP_SCopy, iIndexId, iFirstCol + 1);
	}
	sqlite3VdbeAddOp4(v,
			  OP_String8, 0, iFirstCol + 2, 0,
			  sqlite3DbStrDup(pParse->db, pIndex->def->name),
			  P4_DYNAMIC);
	sqlite3VdbeAddOp4(v, OP_String8, 0, iFirstCol + 3, 0, "tree",
			  P4_STATIC);
	sqlite3VdbeAddOp4(v, OP_Blob, index_opts_sz, iFirstCol + 4,
			  SQL_SUBTYPE_MSGPACK, index_opts, P4_DYNAMIC);
	/* zOpts and zParts are co-located, hence STATIC */
	sqlite3VdbeAddOp4(v, OP_Blob, index_parts_sz, iFirstCol + 5,
			  SQL_SUBTYPE_MSGPACK, index_parts, P4_STATIC);
	sqlite3VdbeAddOp3(v, OP_MakeRecord, iFirstCol, 6, iRecord);
	sqlite3VdbeAddOp2(v, OP_SInsert, BOX_INDEX_ID, iRecord);
	/*
	 * Non-NULL value means that index has been created via
	 * separate CREATE INDEX statement.
	 */
	if (zSql != NULL)
		sqlite3VdbeChangeP5(v, OPFLAG_NCHANGE);
	return;
error:
	pParse->rc = SQL_TARANTOOL_ERROR;
	pParse->nErr++;
}

/*
 * Generate code to initialize register range with arguments for
 * ParseSchema2. Consumes zSql. Returns the first register used.
 */
static int
makeIndexSchemaRecord(Parse * pParse,
		      Index * pIndex,
		      int iSpaceId, int iIndexId, const char *zSql)
{
	Vdbe *v = sqlite3GetVdbe(pParse);
	int iP4Type;
	int iFirstCol = pParse->nMem + 1;
	pParse->nMem += 4;

	sqlite3VdbeAddOp4(v,
			  OP_String8, 0, iFirstCol, 0,
			  sqlite3DbStrDup(pParse->db, pIndex->def->name),
			  P4_DYNAMIC);

	if (pParse->pNewTable) {
		/*
		 * A new table is being created, hence iSpaceId is a register, but
		 * iIndexId is literal.
		 */
		sqlite3VdbeAddOp2(v, OP_SCopy, iSpaceId, iFirstCol + 1);
		sqlite3VdbeAddOp2(v, OP_Integer, iIndexId, iFirstCol + 2);
	} else {
		/*
		 * An existing table is being modified; iSpaceId is literal, but
		 * iIndexId is a register.
		 */
		sqlite3VdbeAddOp2(v, OP_Integer, iSpaceId, iFirstCol + 1);
		sqlite3VdbeAddOp2(v, OP_SCopy, iIndexId, iFirstCol + 2);
	}

	iP4Type = P4_DYNAMIC;
	if (zSql == 0) {
		zSql = "";
		iP4Type = P4_STATIC;
	}
	sqlite3VdbeAddOp4(v, OP_String8, 0, iFirstCol + 3, 0, zSql, iP4Type);
	return iFirstCol;
}

/*
 * Generate code to create a new space.
 * iSpaceId is a register storing the id of the space.
 * iCursor is a cursor to access _space.
 */
static void
createSpace(Parse * pParse, int iSpaceId, char *zStmt)
{
	struct Table *table = pParse->pNewTable;
	Vdbe *v = sqlite3GetVdbe(pParse);
	int iFirstCol = ++pParse->nMem;
	int iRecord = (pParse->nMem += 7);
	struct region *region = &pParse->region;
	uint32_t table_opts_stmt_sz = 0;
	char *table_opts_stmt = sql_encode_table_opts(region, table, zStmt,
						      &table_opts_stmt_sz);
	if (table_opts_stmt == NULL)
		goto error;
	uint32_t table_stmt_sz = 0;
	char *table_stmt = sql_encode_table(region, table, &table_stmt_sz);
	if (table_stmt == NULL)
		goto error;
	char *raw = sqlite3DbMallocRaw(pParse->db,
				       table_stmt_sz + table_opts_stmt_sz);
	if (raw == NULL)
		return;

	memcpy(raw, table_opts_stmt, table_opts_stmt_sz);
	table_opts_stmt = raw;
	raw += table_opts_stmt_sz;
	memcpy(raw, table_stmt, table_stmt_sz);
	table_stmt = raw;

	sqlite3VdbeAddOp2(v, OP_SCopy, iSpaceId, iFirstCol /* spaceId */ );
	sqlite3VdbeAddOp2(v, OP_Integer, effective_user()->uid,
			  iFirstCol + 1 /* owner */ );
	sqlite3VdbeAddOp4(v, OP_String8, 0, iFirstCol + 2 /* name */ , 0,
			  sqlite3DbStrDup(pParse->db, table->def->name),
			  P4_DYNAMIC);
	sqlite3VdbeAddOp4(v, OP_String8, 0, iFirstCol + 3 /* engine */ , 0,
			  sqlite3DbStrDup(pParse->db, table->def->engine_name),
			  P4_DYNAMIC);
	sqlite3VdbeAddOp2(v, OP_Integer, table->def->field_count,
			  iFirstCol + 4 /* field_count */ );
	sqlite3VdbeAddOp4(v, OP_Blob, table_opts_stmt_sz, iFirstCol + 5,
			  SQL_SUBTYPE_MSGPACK, table_opts_stmt, P4_DYNAMIC);
	/* zOpts and zFormat are co-located, hence STATIC */
	sqlite3VdbeAddOp4(v, OP_Blob, table_stmt_sz, iFirstCol + 6,
			  SQL_SUBTYPE_MSGPACK, table_stmt, P4_STATIC);
	sqlite3VdbeAddOp3(v, OP_MakeRecord, iFirstCol, 7, iRecord);
	sqlite3VdbeAddOp2(v, OP_SInsert, BOX_SPACE_ID, iRecord);
	sqlite3VdbeChangeP5(v, OPFLAG_NCHANGE);
	return;
error:
	pParse->nErr++;
	pParse->rc = SQL_TARANTOOL_ERROR;
}

/*
 * Generate code to emit and parse table schema record.
 * iSpaceId is a register storing the id of the space.
 * Consumes zStmt.
 */
static void
parseTableSchemaRecord(Parse * pParse, int iSpaceId, char *zStmt)
{
	Table *p = pParse->pNewTable;
	Vdbe *v = sqlite3GetVdbe(pParse);
	Index *pIdx, *pPrimaryIdx;
	int i, iTop = pParse->nMem + 1;
	pParse->nMem += 4;

	sqlite3VdbeAddOp4(v, OP_String8, 0, iTop, 0,
			  sqlite3DbStrDup(pParse->db, p->def->name), P4_DYNAMIC);
	sqlite3VdbeAddOp2(v, OP_SCopy, iSpaceId, iTop + 1);
	sqlite3VdbeAddOp2(v, OP_Integer, 0, iTop + 2);
	sqlite3VdbeAddOp4(v, OP_String8, 0, iTop + 3, 0, zStmt, P4_DYNAMIC);

	pPrimaryIdx = sqlite3PrimaryKeyIndex(p);
	for (pIdx = p->pIndex, i = 0; pIdx; pIdx = pIdx->pNext) {
		if (pIdx == pPrimaryIdx)
			continue;
		makeIndexSchemaRecord(pParse, pIdx, iSpaceId, ++i, NULL);
	}

	sqlite3VdbeAddParseSchema2Op(v, iTop, pParse->nMem - iTop + 1);
}

int
emitNewSysSequenceRecord(Parse *pParse, int reg_seq_id, const char *seq_name)
{
	Vdbe *v = sqlite3GetVdbe(pParse);
	sqlite3 *db = pParse->db;
	int first_col = pParse->nMem + 1;
	pParse->nMem += 10; /* 9 fields + new record pointer  */

	const long long int min_usigned_long_long = 0;
	const long long int max_usigned_long_long = LLONG_MAX;
	const bool const_false = false;

	/* 1. New sequence id  */
	sqlite3VdbeAddOp2(v, OP_SCopy, reg_seq_id, first_col + 1);
	/* 2. user is  */
	sqlite3VdbeAddOp2(v, OP_Integer, effective_user()->uid, first_col + 2);
	/* 3. New sequence name  */
        sqlite3VdbeAddOp4(v, OP_String8, 0, first_col + 3, 0,
			  sqlite3DbStrDup(pParse->db, seq_name), P4_DYNAMIC);

	/* 4. Step  */
	sqlite3VdbeAddOp2(v, OP_Integer, 1, first_col + 4);

	/* 5. Minimum  */
	sqlite3VdbeAddOp4Dup8(v, OP_Int64, 0, first_col + 5, 0,
			      (unsigned char*)&min_usigned_long_long, P4_INT64);
	/* 6. Maximum  */
	sqlite3VdbeAddOp4Dup8(v, OP_Int64, 0, first_col + 6, 0,
			      (unsigned char*)&max_usigned_long_long, P4_INT64);
	/* 7. Start  */
	sqlite3VdbeAddOp2(v, OP_Integer, 1, first_col + 7);

	/* 8. Cache  */
	sqlite3VdbeAddOp2(v, OP_Integer, 0, first_col + 8);

	/* 9. Cycle  */
	sqlite3VdbeAddOp2(v, OP_Bool, 0, first_col + 9);
	sqlite3VdbeChangeP4(v, -1, (char*)&const_false, P4_BOOL);

	sqlite3VdbeAddOp3(v, OP_MakeRecord, first_col + 1, 9, first_col);

	if (db->mallocFailed)
		return -1;
	else
		return first_col;
}

int
emitNewSysSpaceSequenceRecord(Parse *pParse, int space_id, const char reg_seq_id)
{
	Vdbe *v = sqlite3GetVdbe(pParse);
	const bool const_true = true;
	int first_col = pParse->nMem + 1;
	pParse->nMem += 4; /* 3 fields + new record pointer  */

	/* 1. Space id  */
	sqlite3VdbeAddOp2(v, OP_SCopy, space_id, first_col + 1);
	
	/* 2. Sequence id  */
	sqlite3VdbeAddOp2(v, OP_IntCopy, reg_seq_id, first_col + 2);

	/* 3. True, which is 1 in SQL  */
	sqlite3VdbeAddOp2(v, OP_Bool, 0, first_col + 3);
	sqlite3VdbeChangeP4(v, -1, (char*)&const_true, P4_BOOL);

	sqlite3VdbeAddOp3(v, OP_MakeRecord, first_col + 1, 3, first_col);

	return first_col;
}

/**
 * Generate opcodes to serialize foreign key into MsgPack and
 * insert produced tuple into _fk_constraint space.
 *
 * @param parse_context Parsing context.
 * @param fk Foreign key to be created.
 */
static void
vdbe_emit_fkey_create(struct Parse *parse_context, const struct fkey_def *fk)
{
	assert(parse_context != NULL);
	assert(fk != NULL);
	struct Vdbe *vdbe = sqlite3GetVdbe(parse_context);
	assert(vdbe != NULL);
	/*
	 * Occupy registers for 8 fields: each member in
	 * _constraint space plus one for final msgpack tuple.
	 */
	int constr_tuple_reg = sqlite3GetTempRange(parse_context, 10);
	char *name_copy = sqlite3DbStrDup(parse_context->db, fk->name);
	if (name_copy == NULL)
		return;
	sqlite3VdbeAddOp4(vdbe, OP_String8, 0, constr_tuple_reg, 0, name_copy,
			  P4_DYNAMIC);
	/*
	 * In case we are adding FK constraints during execution
	 * of <CREATE TABLE ...> statement, we don't have child
	 * id, but we know register where it will be stored.
	 */
	if (parse_context->pNewTable != NULL) {
		sqlite3VdbeAddOp2(vdbe, OP_SCopy, fk->child_id,
				  constr_tuple_reg + 1);
	} else {
		sqlite3VdbeAddOp2(vdbe, OP_Integer, fk->child_id,
				  constr_tuple_reg + 1);
	}
	if (parse_context->pNewTable != NULL && fkey_is_self_referenced(fk)) {
		sqlite3VdbeAddOp2(vdbe, OP_SCopy, fk->parent_id,
				  constr_tuple_reg + 2);
	} else {
		sqlite3VdbeAddOp2(vdbe, OP_Integer, fk->parent_id,
				  constr_tuple_reg + 2);
	}
	/*
	 * Lets check that constraint with this name hasn't
	 * been created before.
	 */
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_CONSTRAINT_EXISTS), name_copy);
	if (vdbe_emit_halt_with_presence_test(parse_context,
					      BOX_FK_CONSTRAINT_ID, 0,
					      constr_tuple_reg, 2,
					      ER_CONSTRAINT_EXISTS, error_msg,
					      false, OP_NoConflict) != 0)
		return;
	sqlite3VdbeAddOp2(vdbe, OP_Bool, 0, constr_tuple_reg + 3);
	sqlite3VdbeChangeP4(vdbe, -1, (char*)&fk->is_deferred, P4_BOOL);
	sqlite3VdbeAddOp4(vdbe, OP_String8, 0, constr_tuple_reg + 4, 0,
			  fkey_match_strs[fk->match], P4_STATIC);
	sqlite3VdbeAddOp4(vdbe, OP_String8, 0, constr_tuple_reg + 5, 0,
			  fkey_action_strs[fk->on_delete], P4_STATIC);
	sqlite3VdbeAddOp4(vdbe, OP_String8, 0, constr_tuple_reg + 6, 0,
			  fkey_action_strs[fk->on_update], P4_STATIC);
	struct region *region = &parse_context->region;
	uint32_t parent_links_size = 0;
	char *parent_links = fkey_encode_links(region, fk, FIELD_LINK_PARENT,
					       &parent_links_size);
	if (parent_links == NULL)
		goto error;
	uint32_t child_links_size = 0;
	char *child_links = fkey_encode_links(region, fk, FIELD_LINK_CHILD,
					      &child_links_size);
	if (child_links == NULL)
		goto error;
	/*
	 * We are allocating memory for both parent and child
	 * arrays in the same chunk. Thus, first OP_Blob opcode
	 * interprets it as static memory, and the second one -
	 * as dynamic and releases memory.
	 */
	char *raw = sqlite3DbMallocRaw(parse_context->db,
				       parent_links_size + child_links_size);
	if (raw == NULL)
		return;
	memcpy(raw, parent_links, parent_links_size);
	parent_links = raw;
	raw += parent_links_size;
	memcpy(raw, child_links, child_links_size);
	child_links = raw;

	sqlite3VdbeAddOp4(vdbe, OP_Blob, child_links_size, constr_tuple_reg + 7,
			  SQL_SUBTYPE_MSGPACK, child_links, P4_STATIC);
	sqlite3VdbeAddOp4(vdbe, OP_Blob, parent_links_size,
			  constr_tuple_reg + 8, SQL_SUBTYPE_MSGPACK,
			  parent_links, P4_DYNAMIC);
	sqlite3VdbeAddOp3(vdbe, OP_MakeRecord, constr_tuple_reg, 9,
			  constr_tuple_reg + 9);
	sqlite3VdbeAddOp2(vdbe, OP_SInsert, BOX_FK_CONSTRAINT_ID,
			  constr_tuple_reg + 9);
	sqlite3VdbeChangeP5(vdbe, OPFLAG_NCHANGE);
	sqlite3ReleaseTempRange(parse_context, constr_tuple_reg, 10);
	return;
error:
	parse_context->nErr++;
	parse_context->rc = SQL_TARANTOOL_ERROR;
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
	parse_context->rc = SQL_TARANTOOL_ERROR;
	parse_context->nErr++;
	return -1;
}

/*
 * This routine is called to report the final ")" that terminates
 * a CREATE TABLE statement.
 *
 * The table structure that other action routines have been building
 * is added to the internal hash tables, assuming no errors have
 * occurred.
 *
 * Insert is performed in two passes:
 *  1. When db->init.busy == 0. Byte code for creation of new Tarantool
 *     space and all necessary Tarantool indexes is emitted
 *  2. When db->init.busy == 1. This means that byte code for creation
 *     of new table is executing right now, and it's time to add new entry
 *     for the table into SQL memory represenation
 *
 * If the pSelect argument is not NULL, it means that this routine
 * was called to create a table generated from a
 * "CREATE TABLE ... AS SELECT ..." statement.  The column names of
 * the new table will match the result set of the SELECT.
 */
void
sqlite3EndTable(Parse * pParse,	/* Parse context */
		Token * pEnd,	/* The ')' before options in the CREATE TABLE */
		Select * pSelect	/* Select from a "CREATE ... AS SELECT" */
    )
{
	Table *p;		/* The new table */
	sqlite3 *db = pParse->db;	/* The database connection */

	if (pEnd == 0 && pSelect == 0) {
		return;
	}
	assert(!db->mallocFailed);
	p = pParse->pNewTable;
	if (p == 0)
		return;

	assert(!db->init.busy || !pSelect);

	/* If db->init.busy == 1, then we're called from
	 * OP_ParseSchema2 and is about to update in-memory
	 * schema.
	 */
	if (db->init.busy) {
		p->def->id = db->init.space_id;
		for(struct Index *idx = p->pIndex; idx != NULL;
		    idx = idx->pNext)
			idx->def->space_id = p->def->id;
	}

	if (!p->def->opts.is_view) {
		if (sqlite3PrimaryKeyIndex(p) == NULL) {
			sqlite3ErrorMsg(pParse,
					"PRIMARY KEY missing on table %s",
					p->def->name);
			goto cleanup;
		}
	}

	/*
	 * Actualize conflict action for NOT NULL constraint.
	 * Set defaults for columns having no separate
	 * NULL/NOT NULL specifiers.
	 */
	struct field_def *field = p->def->fields;
	for (uint32_t i = 0; i < p->def->field_count; ++i, ++field) {
		if (field->nullable_action == ON_CONFLICT_ACTION_DEFAULT) {
			/* Set default nullability NONE. */
			field->nullable_action = ON_CONFLICT_ACTION_NONE;
			field->is_nullable = true;
		}
	}

	if (db->init.busy) {
		/*
		 * As rebuild creates a new ExpList tree and
		 * old_def is allocated on region release old
		 * tree manually. This procedure is necessary
		 * only at second stage of table creation, i.e.
		 * before adding to table hash.
		 */
		struct ExprList *old_checks = p->def->opts.checks;
		if (sql_table_def_rebuild(db, p) != 0)
			goto cleanup;
		sql_expr_list_delete(db, old_checks);
		/*
		 * Add the table to the in-memory representation
		 * of the database.
		 */
		struct Table *pOld = sqlite3HashInsert(&db->pSchema->tblHash,
						       p->def->name, p);
		if (pOld != NULL) {
			assert(p == pOld);
			sqlite3OomFault(db);
			goto cleanup;
		}
		pParse->pNewTable = NULL;
		current_session()->sql_flags |= SQLITE_InternChanges;
		goto finish;
	}
	/*
	 * If not initializing, then create new Tarantool space.
	 */
	struct Vdbe *v = sqlite3GetVdbe(pParse);
	if (NEVER(v == 0))
		return;

	/* Text of the CREATE TABLE or CREATE VIEW statement. */
	char *stmt;
	if (pSelect) {
		stmt = createTableStmt(db, p);
	} else {
		struct Token *pEnd2 = p->def->opts.is_view ?
				      &pParse->sLastToken : pEnd;
		int n = pEnd2->z - pParse->sNameToken.z;
		if (pEnd2->z[0] != ';')
			n += pEnd2->n;
		stmt = sqlite3MPrintf(db, "CREATE %s %.*s",
				      p->def->opts.is_view ? "VIEW" : "TABLE",
				      n, pParse->sNameToken.z);
	}
	int reg_space_id = getNewSpaceId(pParse);
	createSpace(pParse, reg_space_id, stmt);
	/* Indexes aren't required for VIEW's.. */
	if (!p->def->opts.is_view) {
		struct Index *idx = sqlite3PrimaryKeyIndex(p);
		assert(idx != NULL);
		for (uint32_t i = 0; idx != NULL; idx = idx->pNext, i++)
			createIndex(pParse, idx, reg_space_id, i, NULL);
	}

	/*
	 * Check to see if we need to create an _sequence table
	 * for keeping track of autoincrement keys.
	 */
	if (pParse->is_new_table_autoinc) {
		assert(reg_space_id != 0);
		/* Do an insertion into _sequence. */
		int reg_seq_id = ++pParse->nMem;
		sqlite3VdbeAddOp2(v, OP_NextSequenceId, 0, reg_seq_id);
		int reg_seq_record =
			emitNewSysSequenceRecord(pParse, reg_seq_id,
						 p->def->name);
		sqlite3VdbeAddOp2(v, OP_SInsert, BOX_SEQUENCE_ID,
				  reg_seq_record);
		/* Do an insertion into _space_sequence. */
		int reg_space_seq_record =
			emitNewSysSpaceSequenceRecord(pParse, reg_space_id,
						      reg_seq_id);
		sqlite3VdbeAddOp2(v, OP_SInsert, BOX_SPACE_SEQUENCE_ID,
				  reg_space_seq_record);
	}

	/*
	 * Reparse everything to update our internal data
	 * structures.
	 */
	parseTableSchemaRecord(pParse, reg_space_id, stmt);
	/* 'stmt' is kept inside the call. */

	/* Code creation of FK constraints, if any. */
	struct fkey_parse *fk_parse;
	rlist_foreach_entry(fk_parse, &pParse->new_fkey, link) {
		struct fkey_def *fk = fk_parse->fkey;
		if (fk_parse->selfref_cols != NULL) {
			struct ExprList *cols = fk_parse->selfref_cols;
			for (uint32_t i = 0; i < fk->field_count; ++i) {
				if (resolve_link(pParse, p->def,
						 cols->a[i].zName,
						 &fk->links[i].parent_field,
						 fk->name) != 0)
					return;
			}
			fk->parent_id = reg_space_id;
		} else if (fk_parse->is_self_referenced) {
			struct Index *pk = sqlite3PrimaryKeyIndex(p);
			if (pk->def->key_def->part_count != fk->field_count) {
				diag_set(ClientError, ER_CREATE_FK_CONSTRAINT,
					 fk->name, "number of columns in "\
					 "foreign key does not match the "\
					 "number of columns in the primary "\
					 "index of referenced table");
				pParse->rc = SQL_TARANTOOL_ERROR;
				pParse->nErr++;
				return;
			}
			for (uint32_t i = 0; i < fk->field_count; ++i) {
				fk->links[i].parent_field =
					pk->def->key_def->parts[i].fieldno;
			}
			fk->parent_id = reg_space_id;
		}
		fk->child_id = reg_space_id;
		vdbe_emit_fkey_create(pParse, fk);
	}

finish:
	/*
	 * Checks are useless for now as all operations process
	 * with the server checks instance. Remove to do not
	 * consume extra memory, don't require make a copy on
	 * space_def_dup and to improve debuggability.
	 */
cleanup:
	sql_expr_list_delete(db, p->def->opts.checks);
	p->def->opts.checks = NULL;
}

void
sql_create_view(struct Parse *parse_context, struct Token *begin,
		struct Token *name, struct ExprList *aliases,
		struct Select *select, bool if_exists)
{
	struct sqlite3 *db = parse_context->db;
	struct Table *sel_tab = NULL;
	if (parse_context->nVar > 0) {
		sqlite3ErrorMsg(parse_context,
				"parameters are not allowed in views");
		goto create_view_fail;
	}
	sqlite3StartTable(parse_context, name, if_exists);
	struct Table *p = parse_context->pNewTable;
	if (p == NULL || parse_context->nErr != 0)
		goto create_view_fail;
	sel_tab = sqlite3ResultSetOfSelect(parse_context, select);
	if (sel_tab == NULL)
		goto create_view_fail;
	if (aliases != NULL) {
		if ((int)sel_tab->def->field_count != aliases->nExpr) {
			sqlite3ErrorMsg(parse_context, "expected %d columns "\
					"for '%s' but got %d", aliases->nExpr,
					p->def->name,
					sel_tab->def->field_count);
			goto create_view_fail;
		}
		sqlite3ColumnsFromExprList(parse_context, aliases, p);
		sqlite3SelectAddColumnTypeAndCollation(parse_context, p,
						       select);
	} else {
		assert(sel_tab->def->opts.is_temporary);
		p->def->fields = sel_tab->def->fields;
		p->def->field_count = sel_tab->def->field_count;
		sel_tab->def->fields = NULL;
		sel_tab->def->field_count = 0;
	}
	p->def->opts.is_view = true;
	/*
	 * Locate the end of the CREATE VIEW statement.
	 * Make sEnd point to the end.
	 */
	struct Token end = parse_context->sLastToken;
	assert(end.z[0] != 0);
	if (end.z[0] != ';')
		end.z += end.n;
	end.n = 0;
	int n = end.z - begin->z;
	assert(n > 0);
	const char *z = begin->z;
	while (sqlite3Isspace(z[n - 1]))
		n--;
	end.z = &z[n - 1];
	end.n = 1;
	p->def->opts.sql = strndup(begin->z, n);
	if (p->def->opts.sql == NULL) {
		diag_set(OutOfMemory, n, "strndup", "opts.sql");
		parse_context->rc = SQL_TARANTOOL_ERROR;
		parse_context->nErr++;
		goto create_view_fail;
	}

	/* Use sqlite3EndTable() to add the view to the Tarantool.  */
	sqlite3EndTable(parse_context, &end, 0);

 create_view_fail:
	sqlite3DbFree(db, sel_tab);
	sql_expr_list_delete(db, aliases);
	sql_select_delete(db, select);
	return;
}

int
sql_view_assign_cursors(struct Parse *parse, const char *view_stmt)
{
	assert(view_stmt != NULL);
	struct sqlite3 *db = parse->db;
	struct Select *select = sql_view_compile(db, view_stmt);
	if (select == NULL)
		return -1;
	sqlite3SrcListAssignCursors(parse, select->pSrc);
	sql_select_delete(db, select);
	return 0;
}

void
sql_store_select(struct Parse *parse_context, struct Select *select)
{
	Select *select_copy = sqlite3SelectDup(parse_context->db, select, 0);
	parse_context->parsed_ast_type = AST_TYPE_SELECT;
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
	struct sqlite3 *db = parse->db;

	struct Expr *col_name_expr = sqlite3Expr(db, TK_ID, col_name);
	if (col_name_expr == NULL)
		return NULL;
	struct Expr *col_value_expr = sqlite3Expr(db, TK_STRING, col_value);
	if (col_value_expr == NULL) {
		sql_expr_delete(db, col_name_expr, false);
		return NULL;
	}
	return sqlite3PExpr(parse, TK_EQ, col_name_expr, col_value_expr);
}

void
vdbe_emit_stat_space_clear(struct Parse *parse, const char *stat_table_name,
			   const char *idx_name, const char *table_name)
{
	assert(idx_name != NULL || table_name != NULL);
	struct sqlite3 *db = parse->db;
	assert(!db->mallocFailed);
	struct SrcList *src_list = sql_alloc_src_list(db);
	if (src_list != NULL)
		src_list->a[0].zName = sqlite3DbStrDup(db, stat_table_name);
	struct Expr *where = NULL;
	if (idx_name != NULL) {
		struct Expr *expr = sql_id_eq_str_expr(parse, "idx", idx_name);
		if (expr != NULL)
			where = sqlite3ExprAnd(db, expr, where);
	}
	if (table_name != NULL) {
		struct Expr *expr = sql_id_eq_str_expr(parse, "tbl", table_name);
		if (expr != NULL)
			where = sqlite3ExprAnd(db, expr, where);
	}
	/**
	 * On memory allocation error sql_table delete_from
	 * releases memory for its own.
	 */
	sql_table_delete_from(parse, src_list, where);
}

/**
 * Remove entries from the _sql_stat1 and _sql_stat4
 * system spaces after a DROP INDEX or DROP TABLE command.
 *
 * @param parse      The parsing context.
 * @param table_name The table to be dropped or
 *                   the table that contains index to be dropped.
 * @param idx_name   Index to be dropped.
 */
static void
sql_clear_stat_spaces(struct Parse *parse, const char *table_name,
		      const char *idx_name)
{
	vdbe_emit_stat_space_clear(parse, "_sql_stat4", idx_name, table_name);
	vdbe_emit_stat_space_clear(parse, "_sql_stat1", idx_name, table_name);
}

/**
 * Generate VDBE program to remove entry from _fk_constraint space.
 *
 * @param parse_context Parsing context.
 * @param constraint_name Name of FK constraint to be dropped.
 *        Must be allocated on head by sqlite3DbMalloc().
 *        It will be freed in VDBE.
 * @param child_id Id of table which constraint belongs to.
 */
static void
vdbe_emit_fkey_drop(struct Parse *parse_context, char *constraint_name,
		    uint32_t child_id)
{
	struct Vdbe *vdbe = sqlite3GetVdbe(parse_context);
	assert(vdbe != NULL);
	int key_reg = sqlite3GetTempRange(parse_context, 3);
	sqlite3VdbeAddOp4(vdbe, OP_String8, 0, key_reg, 0, constraint_name,
			  P4_DYNAMIC);
	sqlite3VdbeAddOp2(vdbe, OP_Integer, child_id,  key_reg + 1);
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_NO_SUCH_CONSTRAINT),
			   constraint_name);
	if (vdbe_emit_halt_with_presence_test(parse_context,
					      BOX_FK_CONSTRAINT_ID, 0,
					      key_reg, 2, ER_NO_SUCH_CONSTRAINT,
					      error_msg, false,
					      OP_Found) != 0) {
		sqlite3DbFree(parse_context->db, constraint_name);
		return;
	}
	sqlite3VdbeAddOp3(vdbe, OP_MakeRecord, key_reg, 2, key_reg + 2);
	sqlite3VdbeAddOp2(vdbe, OP_SDelete, BOX_FK_CONSTRAINT_ID, key_reg + 2);
	sqlite3VdbeChangeP5(vdbe, OPFLAG_NCHANGE);
	VdbeComment((vdbe, "Delete FK constraint %s", constraint_name));
	sqlite3ReleaseTempRange(parse_context, key_reg, 3);
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
	struct Vdbe *v = sqlite3GetVdbe(parse_context);
	assert(v != NULL);
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
	 * Remove any entries of the _sequence and _space_sequence
	 * space associated with the table being dropped. This is
	 * done before the table is dropped from internal schema.
	 */
	int idx_rec_reg = ++parse_context->nMem;
	int space_id_reg = ++parse_context->nMem;
	int space_id = space->def->id;
	sqlite3VdbeAddOp2(v, OP_Integer, space_id, space_id_reg);
	sqlite3VdbeAddOp1(v, OP_CheckViewReferences, space_id_reg);
	if (space->sequence != NULL) {
		/* Delete entry from _space_sequence. */
		sqlite3VdbeAddOp3(v, OP_MakeRecord, space_id_reg, 1,
				  idx_rec_reg);
		sqlite3VdbeAddOp2(v, OP_SDelete, BOX_SPACE_SEQUENCE_ID,
				  idx_rec_reg);
		VdbeComment((v, "Delete entry from _space_sequence"));
		/* Delete entry by id from _sequence. */
		int sequence_id_reg = ++parse_context->nMem;
		sqlite3VdbeAddOp2(v, OP_Integer, space->sequence->def->id,
				  sequence_id_reg);
		sqlite3VdbeAddOp3(v, OP_MakeRecord, sequence_id_reg, 1,
				  idx_rec_reg);
		sqlite3VdbeAddOp2(v, OP_SDelete, BOX_SEQUENCE_ID, idx_rec_reg);
		VdbeComment((v, "Delete entry from _sequence"));
	}
	/* Delete all child FK constraints. */
	struct fkey *child_fk;
	rlist_foreach_entry (child_fk, &space->child_fkey, child_link) {
		char *fk_name_dup = sqlite3DbStrDup(v->db, child_fk->def->name);
		if (fk_name_dup == NULL)
			return;
		vdbe_emit_fkey_drop(parse_context, fk_name_dup, space_id);
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
				sqlite3VdbeAddOp2(v, OP_Integer,
						  space->index[i]->def->iid,
						  space_id_reg + 1);
				sqlite3VdbeAddOp3(v, OP_MakeRecord,
						  space_id_reg, 2, idx_rec_reg);
				sqlite3VdbeAddOp2(v, OP_SDelete, BOX_INDEX_ID,
						  idx_rec_reg);
				VdbeComment((v,
					     "Remove secondary index iid = %u",
					     space->index[i]->def->iid));
			}
		}
		sqlite3VdbeAddOp2(v, OP_Integer, 0, space_id_reg + 1);
		sqlite3VdbeAddOp3(v, OP_MakeRecord, space_id_reg, 2,
				  idx_rec_reg);
		sqlite3VdbeAddOp2(v, OP_SDelete, BOX_INDEX_ID, idx_rec_reg);
		VdbeComment((v, "Remove primary index"));
	}
	/* Delete records about the space from the _truncate. */
	sqlite3VdbeAddOp3(v, OP_MakeRecord, space_id_reg, 1, idx_rec_reg);
	sqlite3VdbeAddOp2(v, OP_SDelete, BOX_TRUNCATE_ID, idx_rec_reg);
	VdbeComment((v, "Delete entry from _truncate"));
	/* Eventually delete entry from _space. */
	sqlite3VdbeAddOp3(v, OP_MakeRecord, space_id_reg, 1, idx_rec_reg);
	sqlite3VdbeAddOp2(v, OP_SDelete, BOX_SPACE_ID, idx_rec_reg);
	sqlite3VdbeChangeP5(v, OPFLAG_NCHANGE);
	VdbeComment((v, "Delete entry from _space"));
	/* Remove the table entry from SQLite's internal schema. */
	sqlite3VdbeAddOp4(v, OP_DropTable, 0, 0, 0, space->def->name, 0);
}

/**
 * This routine is called to do the work of a DROP TABLE statement.
 *
 * @param parse_context Current parsing context.
 * @param table_name_list List containing table name.
 * @param is_view True, if statement is really 'DROP VIEW'.
 * @param if_exists True, if statement contains 'IF EXISTS' clause.
 */
void
sql_drop_table(struct Parse *parse_context, struct SrcList *table_name_list,
	       bool is_view, bool if_exists)
{
	struct Vdbe *v = sqlite3GetVdbe(parse_context);
	struct sqlite3 *db = parse_context->db;
	if (v == NULL || db->mallocFailed) {
		goto exit_drop_table;
	}
	sqlite3VdbeCountChanges(v);
	assert(parse_context->nErr == 0);
	assert(table_name_list->nSrc == 1);
	assert(is_view == 0 || is_view == LOCATE_VIEW);
	const char *space_name = table_name_list->a[0].zName;
	uint32_t space_id = box_space_id_by_name(space_name,
						 strlen(space_name));
	if (space_id == BOX_ID_NIL) {
		if (!is_view && !if_exists)
			sqlite3ErrorMsg(parse_context, "no such table: %s",
					space_name);
		if (is_view && !if_exists)
			sqlite3ErrorMsg(parse_context, "no such view: %s",
					space_name);
		goto exit_drop_table;
	}
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	/*
	 * Ensure DROP TABLE is not used on a view,
	 * and DROP VIEW is not used on a table.
	 */
	if (is_view && !space->def->opts.is_view) {
		sqlite3ErrorMsg(parse_context, "use DROP TABLE to delete table %s",
				space_name);
		goto exit_drop_table;
	}
	if (!is_view && space->def->opts.is_view) {
		sqlite3ErrorMsg(parse_context, "use DROP VIEW to delete view %s",
				space_name);
		goto exit_drop_table;
	}
	/*
	 * Generate code to remove the table from Tarantool
	 * and internal SQL tables. Basically, it consists
	 * from 3 stages:
	 * 1. Delete statistics from _stat1 and _stat4 tables.
	 * 2. In case of presence of FK constraints, i.e. current
	 *    table is child or parent, then start new transaction
	 *    and erase from table all data row by row. On each
	 *    deletion check whether any FK violations have
	 *    occurred. If ones take place, then rollback
	 *    transaction and halt VDBE.
	 * 3. Drop table by truncating (if step 2 was skipped),
	 *    removing indexes from _index space and eventually
	 *    tuple with corresponding space_id from _space.
	 */
	struct fkey *fk;
	rlist_foreach_entry(fk, &space->parent_fkey, parent_link) {
		if (! fkey_is_self_referenced(fk->def)) {
			diag_set(ClientError, ER_DROP_SPACE, space_name,
				 "other objects depend on it");
			parse_context->rc = SQL_TARANTOOL_ERROR;
			parse_context->nErr++;
			goto exit_drop_table;
		}
	}
	sql_clear_stat_spaces(parse_context, space_name, NULL);
	sql_code_drop_table(parse_context, space, is_view);

 exit_drop_table:
	sqlite3SrcListDelete(db, table_name_list);
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
		parse_context->rc = SQL_TARANTOOL_ERROR;
		parse_context->nErr++;
		return -1;
	}
	return 0;
}

void
sql_create_foreign_key(struct Parse *parse_context, struct SrcList *child,
		       struct Token *constraint, struct ExprList *child_cols,
		       struct Token *parent, struct ExprList *parent_cols,
		       bool is_deferred, int actions)
{
	struct sqlite3 *db = parse_context->db;
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
	/*
	 * Table under construction during CREATE TABLE
	 * processing. NULL for ALTER TABLE statement handling.
	 */
	struct Table *new_tab = parse_context->pNewTable;
	/* Whether we are processing ALTER TABLE or CREATE TABLE. */
	bool is_alter = new_tab == NULL;
	uint32_t child_cols_count;
	if (child_cols == NULL) {
		assert(!is_alter);
		child_cols_count = 1;
	} else {
		child_cols_count = child_cols->nExpr;
	}
	assert(!is_alter || (child != NULL && child->nSrc == 1));
	struct space *child_space = NULL;
	uint32_t child_id = 0;
	if (is_alter) {
		const char *child_name = child->a[0].zName;
		child_id = box_space_id_by_name(child_name,
						strlen(child_name));
		if (child_id == BOX_ID_NIL) {
			diag_set(ClientError, ER_NO_SUCH_SPACE, child_name);
			goto tnt_error;
		}
		child_space = space_by_id(child_id);
		assert(child_space != NULL);
	} else {
		struct fkey_parse *fk = region_alloc(&parse_context->region,
						     sizeof(*fk));
		if (fk == NULL) {
			diag_set(OutOfMemory, sizeof(*fk), "region_alloc",
				 "fk");
			goto tnt_error;
		}
		memset(fk, 0, sizeof(*fk));
		rlist_add_entry(&parse_context->new_fkey, fk, link);
	}
	assert(parent != NULL);
	parent_name = sqlite3NameFromToken(db, parent);
	if (parent_name == NULL)
		goto exit_create_fk;
	uint32_t parent_id = box_space_id_by_name(parent_name,
						  strlen(parent_name));
	/*
	 * Within ALTER TABLE ADD CONSTRAINT FK also can be
	 * self-referenced, but in this case parent (which is
	 * also child) table will definitely exist.
	 */
	is_self_referenced = !is_alter &&
			     strcmp(parent_name, new_tab->def->name) == 0;
	struct space *parent_space;
	if (parent_id == BOX_ID_NIL) {
		parent_space = NULL;
		if (is_self_referenced) {
			struct fkey_parse *fk =
				rlist_first_entry(&parse_context->new_fkey,
						  struct fkey_parse, link);
			fk->selfref_cols = parent_cols;
			fk->is_self_referenced = true;
		} else {
			diag_set(ClientError, ER_NO_SUCH_SPACE, parent_name);;
			goto tnt_error;
		}
	} else {
		parent_space = space_by_id(parent_id);
		assert(parent_space != NULL);
		if (parent_space->def->opts.is_view) {
			sqlite3ErrorMsg(parse_context,
					"referenced table can't be view");
			goto exit_create_fk;
		}
	}
	if (constraint == NULL && !is_alter) {
		if (parse_context->constraintName.n == 0) {
			constraint_name =
				sqlite3MPrintf(db, "FK_CONSTRAINT_%d_%s",
					       ++parse_context->fkey_count,
					       new_tab->def->name);
		} else {
			struct Token *cnstr_nm = &parse_context->constraintName;
			constraint_name = sqlite3NameFromToken(db, cnstr_nm);
		}
	} else {
		constraint_name = sqlite3NameFromToken(db, constraint);
	}
	if (constraint_name == NULL)
		goto exit_create_fk;
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
		assert(parent_pk != NULL);
		if (parent_pk->def->key_def->part_count != child_cols_count) {
			diag_set(ClientError, ER_CREATE_FK_CONSTRAINT,
				 constraint_name, error_msg);
			goto tnt_error;
		}
	}
	int name_len = strlen(constraint_name);
	size_t fk_size = fkey_def_sizeof(child_cols_count, name_len);
	struct fkey_def *fk = region_alloc(&parse_context->region, fk_size);
	if (fk == NULL) {
		diag_set(OutOfMemory, fk_size, "region", "struct fkey");
		goto tnt_error;
	}
	fk->field_count = child_cols_count;
	fk->child_id = child_id;
	fk->parent_id = parent_id;
	fk->is_deferred = is_deferred;
	fk->match = (enum fkey_match) ((actions >> 16) & 0xff);
	fk->on_update = (enum fkey_action) ((actions >> 8) & 0xff);
	fk->on_delete = (enum fkey_action) (actions & 0xff);
	fk->links = (struct field_link *) ((char *) fk->name + name_len + 1);
	/* Fill links map. */
	for (uint32_t i = 0; i < fk->field_count; ++i) {
		if (!is_self_referenced && parent_cols == NULL) {
			struct key_def *pk_def =
				parent_space->index[0]->def->key_def;
			fk->links[i].parent_field = pk_def->parts[i].fieldno;
		} else if (!is_self_referenced &&
			   columnno_by_name(parse_context, parent_space,
					    parent_cols->a[i].zName,
					    &fk->links[i].parent_field,
					    constraint_name) != 0) {
			goto exit_create_fk;
		}
		if (!is_alter) {
			if (child_cols == NULL) {
				assert(i == 0);
				/*
				 * In this case there must be only
				 * one link (the last column
				 * added), so we can break
				 * immediately.
				 */
				fk->links[0].child_field =
					new_tab->def->field_count - 1;
				break;
			}
			if (resolve_link(parse_context, new_tab->def,
					 child_cols->a[i].zName,
					 &fk->links[i].child_field,
					 constraint_name) != 0)
				goto exit_create_fk;
		/* In case of ALTER parent table must exist. */
		} else if (columnno_by_name(parse_context, child_space,
					    child_cols->a[i].zName,
					    &fk->links[i].child_field,
					    constraint_name) != 0) {
			goto exit_create_fk;
		}
	}
	memcpy(fk->name, constraint_name, name_len);
	fk->name[name_len] = '\0';
	/*
	 * In case of CREATE TABLE processing, all foreign keys
	 * constraints must be created after space itself, so
	 * lets delay it until sqlite3EndTable() call and simply
	 * maintain list of all FK constraints inside parser.
	 */
	if (!is_alter) {
		struct fkey_parse *parse_fk =
			rlist_first_entry(&parse_context->new_fkey,
					  struct fkey_parse, link);
		parse_fk->fkey = fk;
	} else {
		vdbe_emit_fkey_create(parse_context, fk);
	}

exit_create_fk:
	sql_expr_list_delete(db, child_cols);
	if (!is_self_referenced)
		sql_expr_list_delete(db, parent_cols);
	sqlite3DbFree(db, parent_name);
	sqlite3DbFree(db, constraint_name);
	return;
tnt_error:
	parse_context->rc = SQL_TARANTOOL_ERROR;
	parse_context->nErr++;
	goto exit_create_fk;
}

void
fkey_change_defer_mode(struct Parse *parse_context, bool is_deferred)
{
	if (parse_context->db->init.busy ||
	    rlist_empty(&parse_context->new_fkey))
		return;
	rlist_first_entry(&parse_context->new_fkey, struct fkey_parse,
			  link)->fkey->is_deferred = is_deferred;
}

void
sql_drop_foreign_key(struct Parse *parse_context, struct SrcList *table,
		     struct Token *constraint)
{
	assert(table != NULL && table->nSrc == 1);
	const char *table_name = table->a[0].zName;
	uint32_t child_id = box_space_id_by_name(table_name,
						 strlen(table_name));
	if (child_id == BOX_ID_NIL) {
		diag_set(ClientError, ER_NO_SUCH_SPACE, table_name);
		parse_context->rc = SQL_TARANTOOL_ERROR;
		parse_context->nErr++;
		return;
	}
	char *constraint_name = sqlite3NameFromToken(parse_context->db,
						     constraint);
	if (constraint_name != NULL)
		vdbe_emit_fkey_drop(parse_context, constraint_name, child_id);
}

/*
 * Generate code that will erase and refill index *pIdx.  This is
 * used to initialize a newly created index or to recompute the
 * content of an index in response to a REINDEX command.
 */
static void
sqlite3RefillIndex(Parse * pParse, Index * pIndex)
{
	Table *pTab = pIndex->pTable;	/* The table that is indexed */
	int iTab = pParse->nTab++;	/* Btree cursor used for pTab */
	int iIdx = pParse->nTab++;	/* Btree cursor used for pIndex */
	int iSorter;		/* Cursor opened by OpenSorter (if in use) */
	int addr1;		/* Address of top of loop */
	int addr2;		/* Address to jump to for next iteration */
	Vdbe *v;		/* Generate code into this virtual machine */
	int regRecord;		/* Register holding assembled index record */
	sqlite3 *db = pParse->db;	/* The database connection */
	v = sqlite3GetVdbe(pParse);
	if (v == 0)
		return;
	struct key_def *def = key_def_dup(pIndex->def->key_def);
	if (def == NULL) {
		sqlite3OomFault(db);
		return;
	}
	/* Open the sorter cursor if we are to use one. */
	iSorter = pParse->nTab++;
	sqlite3VdbeAddOp4(v, OP_SorterOpen, iSorter, 0,
			  pIndex->def->key_def->part_count, (char *)def,
			  P4_KEYDEF);

	/* Open the table. Loop through all rows of the table, inserting index
	 * records into the sorter.
	 */
	sqlite3OpenTable(pParse, iTab, pTab, OP_OpenRead);
	addr1 = sqlite3VdbeAddOp2(v, OP_Rewind, iTab, 0);
	VdbeCoverage(v);
	regRecord = sqlite3GetTempReg(pParse);

	sql_generate_index_key(pParse, pIndex, iTab, regRecord, NULL, 0);
	sqlite3VdbeAddOp2(v, OP_SorterInsert, iSorter, regRecord);
	sqlite3VdbeAddOp2(v, OP_Next, iTab, addr1 + 1);
	VdbeCoverage(v);
	sqlite3VdbeJumpHere(v, addr1);
	sqlite3VdbeAddOp2(v, OP_Clear, pIndex->pTable->def->id, 0);
	struct space *space = space_by_id(pIndex->pTable->def->id);
	vdbe_emit_open_cursor(pParse, iIdx, pIndex->def->iid,
			      space);
	sqlite3VdbeChangeP5(v, 0);

	addr1 = sqlite3VdbeAddOp2(v, OP_SorterSort, iSorter, 0);
	VdbeCoverage(v);
	if (pIndex->def->opts.is_unique) {
		int j2 = sqlite3VdbeCurrentAddr(v) + 3;
		sqlite3VdbeGoto(v, j2);
		addr2 = sqlite3VdbeCurrentAddr(v);
		sqlite3VdbeAddOp4Int(v, OP_SorterCompare, iSorter, j2,
				     regRecord,
				     pIndex->def->key_def->part_count);
		VdbeCoverage(v);
		parser_emit_unique_constraint(pParse, ON_CONFLICT_ACTION_ABORT,
					      pIndex);
	} else {
		addr2 = sqlite3VdbeCurrentAddr(v);
	}
	sqlite3VdbeAddOp3(v, OP_SorterData, iSorter, regRecord, iIdx);
	sqlite3VdbeAddOp3(v, OP_Last, iIdx, 0, -1);
	sqlite3VdbeAddOp2(v, OP_IdxInsert, iIdx, regRecord);
	sqlite3ReleaseTempReg(pParse, regRecord);
	sqlite3VdbeAddOp2(v, OP_SorterNext, iSorter, addr2);
	VdbeCoverage(v);
	sqlite3VdbeJumpHere(v, addr1);

	sqlite3VdbeAddOp1(v, OP_Close, iTab);
	sqlite3VdbeAddOp1(v, OP_Close, iIdx);
	sqlite3VdbeAddOp1(v, OP_Close, iSorter);
}

/*
 * Generate code to determine next free Iid in the space identified by
 * the iSpaceId. Return register number holding the result.
 */
static int
getNewIid(Parse * pParse, int iSpaceId, int iCursor)
{
	Vdbe *v = sqlite3GetVdbe(pParse);
	int iRes = ++pParse->nMem;
	int iKey = ++pParse->nMem;
	int iSeekInst, iGotoInst;

	sqlite3VdbeAddOp2(v, OP_Integer, iSpaceId, iKey);
	iSeekInst = sqlite3VdbeAddOp4Int(v, OP_SeekLE, iCursor, 0, iKey, 1);
	sqlite3VdbeAddOp4Int(v, OP_IdxLT, iCursor, 0, iKey, 1);

	/*
	 * If SeekLE succeeds, the control falls through here, skipping
	 * IdxLt.
	 *
	 * If it fails (no entry with the given key prefix: invalid spaceId)
	 * VDBE jumps to the next code block (jump target is IMM, fixed up
	 * later with sqlite3VdbeJumpHere()).
	 */
	iGotoInst = sqlite3VdbeAddOp0(v, OP_Goto);	/* Jump over Halt */

	/* Invalid spaceId detected. Halt now. */
	sqlite3VdbeJumpHere(v, iSeekInst);
	sqlite3VdbeJumpHere(v, iSeekInst + 1);
	sqlite3VdbeAddOp4(v,
			  OP_Halt, SQLITE_ERROR, ON_CONFLICT_ACTION_FAIL, 0,
			  sqlite3MPrintf(pParse->db, "Invalid space id: %d",
					 iSpaceId), P4_DYNAMIC);

	/* Fetch iid from the row and ++it. */
	sqlite3VdbeJumpHere(v, iGotoInst);
	sqlite3VdbeAddOp3(v, OP_Column, iCursor, 1, iRes);
	sqlite3VdbeAddOp2(v, OP_AddImm, iRes, 1);
	return iRes;
}

/**
 * Add new index to table's indexes list.
 * We follow convention that PK comes first in list.
 *
 * @param index Index to be added to list.
 * @param tab Table to which belongs given index.
 */
static void
table_add_index(struct Table *tab, struct Index *index)
{
	struct Index *pk = sqlite3PrimaryKeyIndex(tab);
	if (pk != NULL) {
		index->pNext = pk->pNext;
		pk->pNext = index;
	} else {
		index->pNext = tab->pIndex;
		tab->pIndex = index;
	}
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
 * @param sql_stmt SQL statement, which creates the index.
 * @retval 0 on success, -1 on error.
 */
static int
index_fill_def(struct Parse *parse, struct Index *index,
	       struct Table *table, uint32_t iid, const char *name,
	       uint32_t name_len, struct ExprList *expr_list,
	       enum sql_index_type idx_type, char *sql_stmt)
{
	struct space_def *space_def = table->def;
	struct index_opts opts;
	index_opts_create(&opts);
	opts.is_unique = idx_type != SQL_INDEX_TYPE_NON_UNIQUE;
	opts.sql = sql_stmt;
	index->def = NULL;
	int rc = -1;

	struct key_def *key_def = key_def_new(expr_list->nExpr);
	if (key_def == NULL)
		goto tnt_error;

	for (int i = 0; i < expr_list->nExpr; i++) {
		struct Expr *expr = expr_list->a[i].pExpr;
		sql_resolve_self_reference(parse, table, NC_IdxExpr, expr, 0);
		if (parse->nErr > 0)
			goto cleanup;

		struct Expr *column_expr = sqlite3ExprSkipCollate(expr);
		if (column_expr->op != TK_COLUMN) {
			diag_set(ClientError, ER_UNSUPPORTED, "Tarantool",
				 "functional indexes");
			goto tnt_error;
		}

		uint32_t fieldno = column_expr->iColumn;
		uint32_t coll_id;
		struct coll *coll;
		if (expr->op == TK_COLLATE) {
			coll = sql_get_coll_seq(parse, expr->u.zToken,
						&coll_id);
			if (coll == NULL &&
			    strcasecmp(expr->u.zToken, "binary") != 0) {
				diag_set(ClientError, ER_NO_SUCH_COLLATION,
					 expr->u.zToken);
				goto tnt_error;
			}
		} else {
			coll = sql_column_collation(space_def, fieldno,
						    &coll_id);
		}
		/*
		 * Tarantool: DESC indexes are not supported so
		 * far.
		 */
		key_def_set_part(key_def, i, fieldno,
				 space_def->fields[fieldno].type,
				 space_def->fields[fieldno].nullable_action,
				 coll, coll_id, SORT_ORDER_ASC);
	}
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
	parse->rc = SQL_TARANTOOL_ERROR;
	++parse->nErr;
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

bool
sql_index_is_primary(const struct Index *idx)
{
	return idx->def->iid == 0;
}

void
sql_create_index(struct Parse *parse, struct Token *token,
		 struct SrcList *tbl_name, struct ExprList *col_list,
		 struct Token *start, enum sort_order sort_order,
		 bool if_not_exist, enum sql_index_type idx_type) {
	/* The index to be created. */
	struct Index *index = NULL;
	/* Name of the index. */
	char *name = NULL;
	struct sqlite3 *db = parse->db;
	struct session *user_session = current_session();

	if (db->mallocFailed || parse->nErr > 0)
		goto exit_create_index;
	if (idx_type == SQL_INDEX_TYPE_UNIQUE ||
	    idx_type == SQL_INDEX_TYPE_NON_UNIQUE) {
		Vdbe *v = sqlite3GetVdbe(parse);
		if (v == NULL)
			goto exit_create_index;
		sqlite3VdbeCountChanges(v);
	}
	assert(db->pSchema != NULL);

	/*
	 * Find the table that is to be indexed.
	 * Return early if not found.
	 */
	struct Table *table = NULL;
	if (tbl_name != NULL) {
		assert(token != NULL && token->z != NULL);
		table = sqlite3LocateTable(parse, 0, tbl_name->a[0].zName);
		assert(db->mallocFailed == 0 || table == NULL);
	} else {
		assert(token == NULL);
		assert(start == NULL);
		table = parse->pNewTable;
	}

	if (table == NULL)
		goto exit_create_index;

	if (table->def->opts.is_view) {
		sqlite3ErrorMsg(parse, "views can not be indexed");
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
	if (token != NULL) {
		name = sqlite3NameFromToken(db, token);
		if (name == NULL)
			goto exit_create_index;
		assert(token->z != NULL);
		if (sqlite3LocateIndex(db, name, table->def->name) != NULL) {
			if (!if_not_exist) {
				sqlite3ErrorMsg(parse,
						"index %s.%s already exists",
						table->def->name, name);
			}
			goto exit_create_index;
		}
	} else {
		char *constraint_name = NULL;
		if (parse->constraintName.z != NULL)
			constraint_name =
				sqlite3NameFromToken(db,
						     &parse->constraintName);

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
		const char *prefix = NULL;
		if (idx_type == SQL_INDEX_TYPE_CONSTRAINT_UNIQUE) {
			prefix = constraint_name == NULL ?
				"unique_unnamed_%s_%d" : "unique_%s_%d";
		}
		else { /* idx_type == SQL_INDEX_TYPE_CONSTRAINT_PK */
			prefix = constraint_name == NULL ?
				"pk_unnamed_%s_%d" : "pk_%s_%d";
		}

		uint32_t n = 1;
		for (struct Index *idx = table->pIndex; idx != NULL;
		     idx = idx->pNext, n++);

		if (constraint_name == NULL ||
		    strcmp(constraint_name, "") == 0) {
			name = sqlite3MPrintf(db, prefix,
					      table->def->name, n);
		} else {
			name = sqlite3MPrintf(db, prefix,
					      constraint_name, n);
		}
		sqlite3DbFree(db, constraint_name);
	}

	if (name == NULL || sqlite3CheckIdentifierName(parse, name) != 0)
		goto exit_create_index;

	bool is_system_space = BOX_SYSTEM_ID_MIN < table->def->id &&
			       table->def->id < BOX_SYSTEM_ID_MAX;
	if (is_system_space && (idx_type == SQL_INDEX_TYPE_NON_UNIQUE ||
				idx_type == SQL_INDEX_TYPE_UNIQUE)) {
		diag_set(ClientError, ER_MODIFY_INDEX, name,
			 table->def->name,
			 "can't create index on system space");
		parse->nErr++;
		parse->rc = SQL_TARANTOOL_ERROR;
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
		uint32_t last_field = table->def->field_count - 1;
		sqlite3TokenInit(&prev_col,
				 table->def->fields[last_field].name);
		col_list = sql_expr_list_append(parse->db, NULL,
						sqlite3ExprAlloc(db, TK_ID,
								 &prev_col, 0));
		if (col_list == NULL)
			goto exit_create_index;
		assert(col_list->nExpr == 1);
		sqlite3ExprListSetSortOrder(col_list, sort_order);
	} else {
		sqlite3ExprListCheckLength(parse, col_list, "index");
	}

	index = sqlite3DbMallocZero(db, sizeof(*index));
	if (index == NULL)
		goto exit_create_index;

	index->pTable = table;
	/*
	 * TODO: Issue a warning if two or more columns of the
	 * index are identical.
	 * TODO: Issue a warning if the table primary key is used
	 * as part of the index key.
	 */
	char *sql_stmt = "";
	if (!db->init.busy && tbl_name != NULL) {
		int n = (int) (parse->sLastToken.z - token->z) +
			parse->sLastToken.n;
		if (token->z[n - 1] == ';')
			n--;
		sql_stmt = sqlite3MPrintf(db, "CREATE%s INDEX %.*s",
			   		  idx_type == SQL_INDEX_TYPE_NON_UNIQUE ?
					  "" : " UNIQUE", n, token->z);
		if (sql_stmt == NULL)
			goto exit_create_index;
	}
	/*
	 * If it is parsing stage, then iid may have any value
	 * (for the simplicity sake we set it to 1), but PK
	 * still must have iid == 0.
	 */
	uint32_t iid = idx_type != SQL_INDEX_TYPE_CONSTRAINT_PK;
	if (index_fill_def(parse, index, table, iid, name, strlen(name),
			   col_list, idx_type, sql_stmt) != 0)
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

	if (!index_def_is_valid(index->def, table->def->name))
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
	if (table == parse->pNewTable) {
		for (struct Index *existing_idx = table->pIndex;
		     existing_idx != NULL; existing_idx = existing_idx->pNext) {
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
			    !sql_index_is_primary(existing_idx) && !is_named) {
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
	 * Link the new Index structure to its table and to the
	 * other in-memory database structures. If index created
	 * within CREATE TABLE statement, its iid is assigned
	 * in sql_init_callback().
	 */
	assert(parse->nErr == 0);
	if (db->init.busy && tbl_name != NULL) {
		user_session->sql_flags |= SQLITE_InternChanges;
		index->def->iid = db->init.index_id;
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
		int space_id, index_id, first_schema_col;

		vdbe = sqlite3GetVdbe(parse);
		if (vdbe == 0)
			goto exit_create_index;

		sql_set_multi_write(parse, true);
		sqlite3VdbeAddOp4(vdbe, OP_OpenWrite, cursor, 0, 0,
				  (void *)space_by_id(BOX_INDEX_ID),
				  P4_SPACEPTR);
		sqlite3VdbeChangeP5(vdbe, OPFLAG_SEEKEQ);

		assert(start != NULL);
		space_id = table->def->id;
		index_id = getNewIid(parse, space_id, cursor);
		sqlite3VdbeAddOp1(vdbe, OP_Close, cursor);
		createIndex(parse, index, space_id, index_id, sql_stmt);

		/* Consumes sql_stmt. */
		first_schema_col = makeIndexSchemaRecord(parse, index,
							 space_id, index_id,
							 sql_stmt);

		/*
		 * Reparse the schema. Code an OP_Expire
		 * to invalidate all pre-compiled statements.
		 */
		sqlite3VdbeAddParseSchema2Op(vdbe, first_schema_col, 4);
		sqlite3VdbeAddOp0(vdbe, OP_Expire);
	}

	if (!db->init.busy && tbl_name != NULL)
		goto exit_create_index;
	table_add_index(table, index);
	index = NULL;

	/* Clean up before exiting. */
 exit_create_index:
	if (index != NULL)
		freeIndex(db, index);
	sql_expr_list_delete(db, col_list);
	sqlite3SrcListDelete(db, tbl_name);
	sqlite3DbFree(db, name);
}

void
sql_drop_index(struct Parse *parse_context, struct SrcList *index_name_list,
	       struct Token *table_token, bool if_exists)
{
	struct Vdbe *v = sqlite3GetVdbe(parse_context);
	assert(v != NULL);
	struct sqlite3 *db = parse_context->db;
	/* Never called with prior errors. */
	assert(parse_context->nErr == 0);
	assert(table_token != NULL);
	const char *table_name = sqlite3NameFromToken(db, table_token);
	if (db->mallocFailed) {
		goto exit_drop_index;
	}
	sqlite3VdbeCountChanges(v);
	assert(index_name_list->nSrc == 1);
	assert(table_token->n > 0);
	uint32_t space_id = box_space_id_by_name(table_name,
						 strlen(table_name));
	if (space_id == BOX_ID_NIL) {
		if (!if_exists)
			sqlite3ErrorMsg(parse_context, "no such space: %s",
					table_name);
		goto exit_drop_index;
	}
	const char *index_name = index_name_list->a[0].zName;
	uint32_t index_id = box_index_id_by_name(space_id, index_name,
						 strlen(index_name));
	if (index_id == BOX_ID_NIL) {
		if (!if_exists)
			sqlite3ErrorMsg(parse_context, "no such index: %s.%s",
					table_name, index_name);
		goto exit_drop_index;
	}
	struct space *space = space_by_id(space_id);
	assert(space != NULL);
	struct index *index = space_index(space, index_id);
	assert(index != NULL);
	/*
	 * If index has been created by user, it has its SQL
	 * statement. Otherwise (i.e. PK and UNIQUE indexes,
	 * which are created alongside with table) it is NULL.
	 */
	if (index->def->opts.sql == NULL) {
		sqlite3ErrorMsg(parse_context, "index associated with UNIQUE "
				"or PRIMARY KEY constraint cannot be dropped",
				0);
		goto exit_drop_index;
	}

	/*
	 * Generate code to remove entry from _index space
	 * But firstly, delete statistics since schema
	 * changes after DDL.
	 */
	sql_clear_stat_spaces(parse_context, table_name, index->def->name);
	int record_reg = ++parse_context->nMem;
	int space_id_reg = ++parse_context->nMem;
	sqlite3VdbeAddOp2(v, OP_Integer, space_id, space_id_reg);
	sqlite3VdbeAddOp2(v, OP_Integer, index_id, space_id_reg + 1);
	sqlite3VdbeAddOp3(v, OP_MakeRecord, space_id_reg, 2, record_reg);
	sqlite3VdbeAddOp2(v, OP_SDelete, BOX_INDEX_ID, record_reg);
	sqlite3VdbeChangeP5(v, OPFLAG_NCHANGE);
	/*
	 * Still need to cleanup internal SQL structures.
	 * Should be removed when SQL and Tarantool
	 * data-dictionaries will be completely merged.
	 */
	Index *pIndex = sqlite3LocateIndex(db, index_name, table_name);
	assert(pIndex != NULL);
	sqlite3VdbeAddOp3(v, OP_DropIndex, 0, 0, 0);
	sqlite3VdbeAppendP4(v, pIndex, P4_INDEX);

 exit_drop_index:
	sqlite3SrcListDelete(db, index_name_list);
	sqlite3DbFree(db, (void *) table_name);
}

/*
 * pArray is a pointer to an array of objects. Each object in the
 * array is szEntry bytes in size. This routine uses sqlite3DbRealloc()
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
sqlite3ArrayAllocate(sqlite3 * db,	/* Connection to notify of malloc failures */
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
		void *pNew = sqlite3DbRealloc(db, pArray, sz * szEntry);
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

/*
 * Append a new element to the given IdList.  Create a new IdList if
 * need be.
 *
 * A new IdList is returned, or NULL if malloc() fails.
 */
IdList *
sqlite3IdListAppend(sqlite3 * db, IdList * pList, Token * pToken)
{
	int i;
	if (pList == 0) {
		pList = sqlite3DbMallocZero(db, sizeof(IdList));
		if (pList == 0)
			return 0;
	}
	pList->a = sqlite3ArrayAllocate(db,
					pList->a,
					sizeof(pList->a[0]), &pList->nId, &i);
	if (i < 0) {
		sqlite3IdListDelete(db, pList);
		return 0;
	}
	pList->a[i].zName = sqlite3NameFromToken(db, pToken);
	return pList;
}

/*
 * Delete an IdList.
 */
void
sqlite3IdListDelete(sqlite3 * db, IdList * pList)
{
	int i;
	if (pList == 0)
		return;
	for (i = 0; i < pList->nId; i++) {
		sqlite3DbFree(db, pList->a[i].zName);
	}
	sqlite3DbFree(db, pList->a);
	sqlite3DbFree(db, pList);
}

/*
 * Return the index in pList of the identifier named zId.  Return -1
 * if not found.
 */
int
sqlite3IdListIndex(IdList * pList, const char *zName)
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

/*
 * Expand the space allocazted for the given SrcList object by
 * creating nExtra new slots beginning at iStart.  iStart is zero based.
 * New slots are zeroed.
 *
 * For example, suppose a SrcList initially contains two entries: A,B.
 * To append 3 new entries onto the end, do this:
 *
 *    sqlite3SrcListEnlarge(db, pSrclist, 3, 2);
 *
 * After the call above it would contain:  A, B, nil, nil, nil.
 * If the iStart argument had been 1 instead of 2, then the result
 * would have been:  A, nil, nil, nil, B.  To prepend the new slots,
 * the iStart value would be 0.  The result then would
 * be: nil, nil, nil, A, B.
 *
 * If a memory allocation fails the SrcList is unchanged.  The
 * db->mallocFailed flag will be set to true.
 */
SrcList *
sqlite3SrcListEnlarge(sqlite3 * db,	/* Database connection to notify of OOM errors */
		      SrcList * pSrc,	/* The SrcList to be enlarged */
		      int nExtra,	/* Number of new slots to add to pSrc->a[] */
		      int iStart	/* Index in pSrc->a[] of first new slot */
    )
{
	int i;

	/* Sanity checking on calling parameters */
	assert(iStart >= 0);
	assert(nExtra >= 1);
	assert(pSrc != 0);
	assert(iStart <= pSrc->nSrc);

	/* Allocate additional space if needed */
	if ((u32) pSrc->nSrc + nExtra > pSrc->nAlloc) {
		SrcList *pNew;
		int nAlloc = pSrc->nSrc * 2 + nExtra;
		int nGot;
		pNew = sqlite3DbRealloc(db, pSrc,
					sizeof(*pSrc) + (nAlloc -
							 1) *
					sizeof(pSrc->a[0]));
		if (pNew == 0) {
			assert(db->mallocFailed);
			return pSrc;
		}
		pSrc = pNew;
		nGot =
		    (sqlite3DbMallocSize(db, pNew) -
		     sizeof(*pSrc)) / sizeof(pSrc->a[0]) + 1;
		pSrc->nAlloc = nGot;
	}

	/* Move existing slots that come after the newly inserted slots
	 * out of the way
	 */
	for (i = pSrc->nSrc - 1; i >= iStart; i--) {
		pSrc->a[i + nExtra] = pSrc->a[i];
	}
	pSrc->nSrc += nExtra;

	/* Zero the newly allocated slots */
	memset(&pSrc->a[iStart], 0, sizeof(pSrc->a[0]) * nExtra);
	for (i = iStart; i < iStart + nExtra; i++) {
		pSrc->a[i].iCursor = -1;
	}

	/* Return a pointer to the enlarged SrcList */
	return pSrc;
}

SrcList *
sql_alloc_src_list(sqlite3 *db)
{
	SrcList *pList;

	pList = sqlite3DbMallocRawNN(db, sizeof(SrcList));
	if (pList == 0)
		return NULL;
	pList->nAlloc = 1;
	pList->nSrc = 1;
	memset(&pList->a[0], 0, sizeof(pList->a[0]));
	pList->a[0].iCursor = -1;
	return pList;
}

/*
 * Append a new table name to the given SrcList.  Create a new SrcList if
 * need be.  A new entry is created in the SrcList even if pTable is NULL.
 *
 * A SrcList is returned, or NULL if there is an OOM error.  The returned
 * SrcList might be the same as the SrcList that was input or it might be
 * a new one.  If an OOM error does occurs, then the prior value of pList
 * that is input to this routine is automatically freed.
 *
 * If pDatabase is not null, it means that the table has an optional
 * database name prefix.  Like this:  "database.table".  The pDatabase
 * points to the table name and the pTable points to the database name.
 * The SrcList.a[].zName field is filled with the table name which might
 * come from pTable (if pDatabase is NULL) or from pDatabase.
 * SrcList.a[].zDatabase is filled with the database name from pTable,
 * or with NULL if no database is specified.
 *
 * In other words, if call like this:
 *
 *         sqlite3SrcListAppend(D,A,B,0);
 *
 * Then B is a table name and the database name is unspecified.  If called
 * like this:
 *
 *         sqlite3SrcListAppend(D,A,B,C);
 *
 * Then C is the table name and B is the database name.  If C is defined
 * then so is B.  In other words, we never have a case where:
 *
 *         sqlite3SrcListAppend(D,A,0,C);
 *
 * Both pTable and pDatabase are assumed to be quoted.  They are dequoted
 * before being added to the SrcList.
 */
SrcList *
sqlite3SrcListAppend(sqlite3 * db,	/* Connection to notify of malloc failures */
		     SrcList * pList,	/* Append to this SrcList. NULL creates a new SrcList */
		     Token * pTable	/* Table to append */
    )
{
	struct SrcList_item *pItem;
	assert(db != 0);
	if (pList == 0) {
		pList = sql_alloc_src_list(db);
		if (pList == 0)
			return 0;
	} else {
		pList = sqlite3SrcListEnlarge(db, pList, 1, pList->nSrc);
	}
	if (db->mallocFailed) {
		sqlite3SrcListDelete(db, pList);
		return 0;
	}
	pItem = &pList->a[pList->nSrc - 1];
	pItem->zName = sqlite3NameFromToken(db, pTable);
	return pList;
}

/*
 * Assign VdbeCursor index numbers to all tables in a SrcList
 */
void
sqlite3SrcListAssignCursors(Parse * pParse, SrcList * pList)
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
				sqlite3SrcListAssignCursors(pParse,
							    pItem->pSelect->
							    pSrc);
			}
		}
	}
}

/*
 * Delete an entire SrcList including all its substructure.
 */
void
sqlite3SrcListDelete(sqlite3 * db, SrcList * pList)
{
	int i;
	struct SrcList_item *pItem;
	if (pList == 0)
		return;
	for (pItem = pList->a, i = 0; i < pList->nSrc; i++, pItem++) {
		sqlite3DbFree(db, pItem->zName);
		sqlite3DbFree(db, pItem->zAlias);
		if (pItem->fg.isIndexedBy)
			sqlite3DbFree(db, pItem->u1.zIndexedBy);
		if (pItem->fg.isTabFunc)
			sql_expr_list_delete(db, pItem->u1.pFuncArg);
		sqlite3DeleteTable(db, pItem->pTab);
		sql_select_delete(db, pItem->pSelect);
		sql_expr_delete(db, pItem->pOn, false);
		sqlite3IdListDelete(db, pItem->pUsing);
	}
	sqlite3DbFree(db, pList);
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
sqlite3SrcListAppendFromTerm(Parse * pParse,	/* Parsing context */
			     SrcList * p,	/* The left part of the FROM clause already seen */
			     Token * pTable,	/* Name of the table to add to the FROM clause */
			     Token * pAlias,	/* The right-hand side of the AS subexpression */
			     Select * pSubquery,	/* A subquery used in place of a table name */
			     Expr * pOn,	/* The ON clause of a join */
			     IdList * pUsing	/* The USING clause of a join */
    )
{
	struct SrcList_item *pItem;
	sqlite3 *db = pParse->db;
	if (!p && (pOn || pUsing)) {
		sqlite3ErrorMsg(pParse, "a JOIN clause is required before %s",
				(pOn ? "ON" : "USING")
		    );
		goto append_from_error;
	}
	p = sqlite3SrcListAppend(db, p, pTable);
	if (p == 0 || NEVER(p->nSrc == 0)) {
		goto append_from_error;
	}
	pItem = &p->a[p->nSrc - 1];
	assert(pAlias != 0);
	if (pAlias->n) {
		pItem->zAlias = sqlite3NameFromToken(db, pAlias);
	}
	pItem->pSelect = pSubquery;
	pItem->pOn = pOn;
	pItem->pUsing = pUsing;
	return p;

 append_from_error:
	assert(p == 0);
	sql_expr_delete(db, pOn, false);
	sqlite3IdListDelete(db, pUsing);
	sql_select_delete(db, pSubquery);
	return 0;
}

/*
 * Add an INDEXED BY or NOT INDEXED clause to the most recently added
 * element of the source-list passed as the second argument.
 */
void
sqlite3SrcListIndexedBy(Parse * pParse, SrcList * p, Token * pIndexedBy)
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
		} else {
			pItem->u1.zIndexedBy =
			    sqlite3NameFromToken(pParse->db, pIndexedBy);
			pItem->fg.isIndexedBy = (pItem->u1.zIndexedBy != 0);
		}
	}
}

/*
 * Add the list of function arguments to the SrcList entry for a
 * table-valued-function.
 */
void
sqlite3SrcListFuncArgs(Parse * pParse, SrcList * p, ExprList * pList)
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
sqlite3SrcListShiftJoinType(SrcList * p)
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
	struct Vdbe *v = sqlite3GetVdbe(parse_context);
	if (v != NULL)
		sqlite3VdbeAddOp0(v, OP_TransactionBegin);
}

void
sql_transaction_commit(struct Parse *parse_context)
{
	assert(parse_context != NULL);
	struct Vdbe *v = sqlite3GetVdbe(parse_context);
	if (v != NULL)
		sqlite3VdbeAddOp0(v, OP_TransactionCommit);
}

void
sql_transaction_rollback(Parse *pParse)
{
	assert(pParse != 0);
	struct Vdbe *v = sqlite3GetVdbe(pParse);
	if (v != NULL)
		sqlite3VdbeAddOp0(v, OP_TransactionRollback);
}

/*
 * This function is called by the parser when it parses a command to create,
 * release or rollback an SQL savepoint.
 */
void
sqlite3Savepoint(Parse * pParse, int op, Token * pName)
{
	char *zName = sqlite3NameFromToken(pParse->db, pName);
	if (zName) {
		Vdbe *v = sqlite3GetVdbe(pParse);
		if (!v) {
			sqlite3DbFree(pParse->db, zName);
			return;
		}
		if (op == SAVEPOINT_BEGIN &&
			sqlite3CheckIdentifierName(pParse, zName)
				!= SQLITE_OK) {
			sqlite3ErrorMsg(pParse, "bad savepoint name");
			return;
		}
		sqlite3VdbeAddOp4(v, OP_Savepoint, op, 0, 0, zName, P4_DYNAMIC);
	}
}

/**
 * Set flag in parse context, which indicates that during query
 * execution multiple insertion/updates may occur.
 */
void
sql_set_multi_write(struct Parse *parse_context, bool is_set)
{
	Parse *pToplevel = sqlite3ParseToplevel(parse_context);
	pToplevel->isMultiWrite |= is_set;
}

/*
 * The code generator calls this routine if is discovers that it is
 * possible to abort a statement prior to completion.  In order to
 * perform this abort without corrupting the database, we need to make
 * sure that the statement is protected by a statement transaction.
 *
 * Technically, we only need to set the mayAbort flag if the
 * isMultiWrite flag was previously set.  There is a time dependency
 * such that the abort must occur after the multiwrite.  This makes
 * some statements involving the REPLACE conflict resolution algorithm
 * go a little faster.  But taking advantage of this time dependency
 * makes it more difficult to prove that the code is correct (in
 * particular, it prevents us from writing an effective
 * implementation of sqlite3AssertMayAbort()) and so we have chosen
 * to take the safe route and skip the optimization.
 */
void
sqlite3MayAbort(Parse * pParse)
{
	Parse *pToplevel = sqlite3ParseToplevel(pParse);
	pToplevel->mayAbort = 1;
}

/*
 * Code an OP_Halt that causes the vdbe to return an SQLITE_CONSTRAINT
 * error. The onError parameter determines which (if any) of the statement
 * and/or current transaction is rolled back.
 */
void
sqlite3HaltConstraint(Parse * pParse,	/* Parsing context */
		      int errCode,	/* extended error code */
		      int onError,	/* Constraint type */
		      char *p4,	/* Error message */
		      i8 p4type,	/* P4_STATIC or P4_TRANSIENT */
		      u8 p5Errmsg	/* P5_ErrMsg type */
    )
{
	Vdbe *v = sqlite3GetVdbe(pParse);
	assert((errCode & 0xff) == SQLITE_CONSTRAINT);
	if (onError == ON_CONFLICT_ACTION_ABORT) {
		sqlite3MayAbort(pParse);
	}
	sqlite3VdbeAddOp4(v, OP_Halt, errCode, onError, 0, p4, p4type);
	sqlite3VdbeChangeP5(v, p5Errmsg);
}

void
parser_emit_unique_constraint(struct Parse *parser,
			      enum on_conflict_action on_error,
			      const struct Index *index)
{
	const struct space_def *def = index->pTable->def;
	StrAccum err_accum;
	sqlite3StrAccumInit(&err_accum, parser->db, 0, 0, 200);
	struct key_part *part = index->def->key_def->parts;
	for (uint32_t j = 0; j < index->def->key_def->part_count; ++j, ++part) {
		const char *col_name = def->fields[part->fieldno].name;
		if (j != 0)
			sqlite3StrAccumAppend(&err_accum, ", ", 2);
		sqlite3XPrintf(&err_accum, "%s.%s", def->name, col_name);
	}
	char *err_msg = sqlite3StrAccumFinish(&err_accum);
	sqlite3HaltConstraint(parser, sql_index_is_primary(index) ?
			      SQLITE_CONSTRAINT_PRIMARYKEY :
			      SQLITE_CONSTRAINT_UNIQUE, on_error, err_msg,
			      P4_DYNAMIC, P5_ConstraintUnique);
}

/*
 * Check to see if pIndex uses the collating sequence pColl.  Return
 * true if it does and false if it does not.
 */
#ifndef SQLITE_OMIT_REINDEX
static bool
collationMatch(struct coll *coll, struct Index *index)
{
	assert(coll != NULL);
	struct key_part *part = index->def->key_def->parts;
	for (uint32_t i = 0; i < index->def->key_def->part_count; i++, part++) {
		struct coll *idx_coll = part->coll;
		assert(idx_coll != NULL);
		if (coll == idx_coll)
			return true;
	}
	return false;
}
#endif

/*
 * Recompute all indices of pTab that use the collating sequence pColl.
 * If pColl==0 then recompute all indices of pTab.
 */
#ifndef SQLITE_OMIT_REINDEX
static void
reindexTable(Parse * pParse, Table * pTab, struct coll *coll)
{
	Index *pIndex;		/* An index associated with pTab */

	for (pIndex = pTab->pIndex; pIndex; pIndex = pIndex->pNext) {
		if (coll == 0 || collationMatch(coll, pIndex)) {
			sql_set_multi_write(pParse, false);
			sqlite3RefillIndex(pParse, pIndex);
		}
	}
}
#endif

/*
 * Recompute all indices of all tables in all databases where the
 * indices use the collating sequence pColl.  If pColl==0 then recompute
 * all indices everywhere.
 */
#ifndef SQLITE_OMIT_REINDEX
static void
reindexDatabases(Parse * pParse, struct coll *coll)
{
	sqlite3 *db = pParse->db;	/* The database connection */
	HashElem *k;		/* For looping over tables in pSchema */
	Table *pTab;		/* A table in the database */

	assert(db->pSchema != NULL);
	for (k = sqliteHashFirst(&db->pSchema->tblHash); k;
	     k = sqliteHashNext(k)) {
		pTab = (Table *) sqliteHashData(k);
		reindexTable(pParse, pTab, coll);
	}
}
#endif

/*
 * Generate code for the REINDEX command.
 *
 *        REINDEX                             -- 1
 *        REINDEX  <collation>                -- 2
 *        REINDEX  <tablename>                -- 3
 *        REINDEX  <indexname> ON <tablename> -- 4
 *
 * Form 1 causes all indices in all attached databases to be rebuilt.
 * Form 2 rebuilds all indices in all databases that use the named
 * collating function.  Forms 3 and 4 rebuild the named index or all
 * indices associated with the named table.
 */
#ifndef SQLITE_OMIT_REINDEX
void
sqlite3Reindex(Parse * pParse, Token * pName1, Token * pName2)
{
	char *z = 0;		/* Name of index */
	char *zTable = 0;	/* Name of indexed table */
	Table *pTab;		/* A table in the database */
	sqlite3 *db = pParse->db;	/* The database connection */

	assert(db->pSchema != NULL);

	if (pName1 == 0) {
		reindexDatabases(pParse, 0);
		return;
	} else if (NEVER(pName2 == 0) || pName2->z == 0) {
		assert(pName1->z);
		char *zColl = sqlite3NameFromToken(pParse->db, pName1);
		if (zColl == NULL)
			return;
		if (strcasecmp(zColl, "binary") != 0) {
			struct coll_id *coll_id =
				coll_by_name(zColl, strlen(zColl));
			if (coll_id != NULL) {
				reindexDatabases(pParse, coll_id->coll);
				sqlite3DbFree(db, zColl);
				return;
			}
		}
		sqlite3DbFree(db, zColl);
	}
	z = sqlite3NameFromToken(db, pName1);
	if (z == 0)
		return;
	pTab = sqlite3HashFind(&db->pSchema->tblHash, z);
	if (pTab != NULL) {
		reindexTable(pParse, pTab, 0);
		sqlite3DbFree(db, z);
		return;
	}
	if (pName2->n > 0) {
		zTable = sqlite3NameFromToken(db, pName2);
	}

	pTab = sqlite3HashFind(&db->pSchema->tblHash, zTable);
	if (pTab == 0) {
		sqlite3ErrorMsg(pParse, "no such table: %s", zTable);
		goto exit_reindex;
	}

	sqlite3ErrorMsg(pParse,
			"unable to identify the object to be reindexed");

 exit_reindex:
	sqlite3DbFree(db, z);
	sqlite3DbFree(db, zTable);
}
#endif

#ifndef SQLITE_OMIT_CTE
/*
 * This routine is invoked once per CTE by the parser while parsing a
 * WITH clause.
 */
With *
sqlite3WithAdd(Parse * pParse,	/* Parsing context */
	       With * pWith,	/* Existing WITH clause, or NULL */
	       Token * pName,	/* Name of the common-table */
	       ExprList * pArglist,	/* Optional column name list for the table */
	       Select * pQuery	/* Query used to initialize the table */
    )
{
	sqlite3 *db = pParse->db;
	With *pNew;
	char *zName;

	/* Check that the CTE name is unique within this WITH clause. If
	 * not, store an error in the Parse structure.
	 */
	zName = sqlite3NameFromToken(pParse->db, pName);
	if (zName && pWith) {
		int i;
		for (i = 0; i < pWith->nCte; i++) {
			if (strcmp(zName, pWith->a[i].zName) == 0) {
				sqlite3ErrorMsg(pParse,
						"duplicate WITH table name: %s",
						zName);
			}
		}
	}

	if (pWith) {
		int nByte =
		    sizeof(*pWith) + (sizeof(pWith->a[1]) * pWith->nCte);
		pNew = sqlite3DbRealloc(db, pWith, nByte);
	} else {
		pNew = sqlite3DbMallocZero(db, sizeof(*pWith));
	}
	assert((pNew != 0 && zName != 0) || db->mallocFailed);

	if (db->mallocFailed) {
		sql_expr_list_delete(db, pArglist);
		sql_select_delete(db, pQuery);
		sqlite3DbFree(db, zName);
		pNew = pWith;
	} else {
		pNew->a[pNew->nCte].pSelect = pQuery;
		pNew->a[pNew->nCte].pCols = pArglist;
		pNew->a[pNew->nCte].zName = zName;
		pNew->a[pNew->nCte].zCteErr = 0;
		pNew->nCte++;
	}

	return pNew;
}

/*
 * Free the contents of the With object passed as the second argument.
 */
void
sqlite3WithDelete(sqlite3 * db, With * pWith)
{
	if (pWith) {
		int i;
		for (i = 0; i < pWith->nCte; i++) {
			struct Cte *pCte = &pWith->a[i];
			sql_expr_list_delete(db, pCte->pCols);
			sql_select_delete(db, pCte->pSelect);
			sqlite3DbFree(db, pCte->zName);
		}
		sqlite3DbFree(db, pWith);
	}
}

#endif				/* !defined(SQLITE_OMIT_CTE) */

int
vdbe_emit_halt_with_presence_test(struct Parse *parser, int space_id,
				  int index_id, int key_reg, uint32_t key_len,
				  int tarantool_error_code,
				  const char *error_src, bool no_error,
				  int cond_opcode)
{
	assert(cond_opcode == OP_NoConflict || cond_opcode == OP_Found);
	struct Vdbe *v = sqlite3GetVdbe(parser);
	assert(v != NULL);

	struct sqlite3 *db = parser->db;
	char *error = sqlite3DbStrDup(db, error_src);
	if (error == NULL)
		return -1;

	int cursor = parser->nTab++;
	vdbe_emit_open_cursor(parser, cursor, index_id, space_by_id(space_id));
	sqlite3VdbeChangeP5(v, OPFLAG_SYSTEMSP);
	int label = sqlite3VdbeCurrentAddr(v);
	sqlite3VdbeAddOp4Int(v, cond_opcode, cursor, label + 3, key_reg,
			     key_len);
	if (no_error) {
		sqlite3VdbeAddOp0(v, OP_Halt);
	} else {
		sqlite3VdbeAddOp4(v, OP_Halt, SQL_TARANTOOL_ERROR,0, 0, error,
				  P4_DYNAMIC);
		sqlite3VdbeChangeP5(v, tarantool_error_code);
	}
	sqlite3VdbeAddOp1(v, OP_Close, cursor);
	return 0;
}
