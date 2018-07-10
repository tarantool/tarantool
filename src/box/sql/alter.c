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
 * This file contains C code routines that used to generate VDBE code
 * that implements the ALTER TABLE command.
 */
#include "sqliteInt.h"
#include "src/box/session.h"
#include "tarantoolInt.h"

/*
 * The code in this file only exists if we are not omitting the
 * ALTER TABLE logic from the build.
 */
#ifndef SQLITE_OMIT_ALTERTABLE

/*
 * Generate code to drop and reload the internal representation of table
 * pTab from the database, including triggers.
 * Argument zName is the name of the table in the database schema at
 * the time the generated code is executed. This can be different from
 * pTab->zName if this function is being called to code part of an
 * "ALTER TABLE RENAME TO" statement.
 */
static void
reloadTableSchema(Parse * pParse, Table * pTab, const char *zName)
{
	Vdbe *v;
	v = sqlite3GetVdbe(pParse);
	if (NEVER(v == 0))
		return;

	char *zNewName = sqlite3MPrintf(pParse->db, "%s", zName);
	sqlite3VdbeAddOp4(v, OP_RenameTable, pTab->def->id, 0, 0, zNewName,
			  P4_DYNAMIC);
}

/*
 * Generate code to implement the "ALTER TABLE xxx RENAME TO yyy"
 * command.
 */
void
sqlite3AlterRenameTable(Parse * pParse,	/* Parser context. */
			SrcList * pSrc,	/* The table to rename. */
			Token * pName)	/* The new table name. */
{
	Table *pTab;		/* Table being renamed */
	char *zName = 0;	/* NULL-terminated version of pName */
	sqlite3 *db = pParse->db;	/* Database connection */
	Vdbe *v;
	uint32_t savedDbFlags;	/* Saved value of db->flags */
	struct session *user_session = current_session();

	savedDbFlags = user_session->sql_flags;

	if (NEVER(db->mallocFailed))
		goto exit_rename_table;
	assert(pSrc->nSrc == 1);

	pTab = sqlite3LocateTable(pParse, 0, pSrc->a[0].zName);
	if (pTab == NULL)
		goto exit_rename_table;

	user_session->sql_flags |= SQLITE_PreferBuiltin;

	/* Get a NULL terminated version of the new table name. */
	zName = sqlite3NameFromToken(db, pName);
	if (!zName)
		goto exit_rename_table;

	/* Check that a table named 'zName' does not already exist
	 * in database. If so, this is an error.
	 */
	if (sqlite3HashFind(&db->pSchema->tblHash, zName) != NULL) {
		sqlite3ErrorMsg(pParse,
				"there is already another table or index with this name: %s",
				zName);
		goto exit_rename_table;
	}
	if (pTab->def->opts.is_view) {
		sqlite3ErrorMsg(pParse, "view %s may not be altered",
				pTab->def->name);
		goto exit_rename_table;
	}
	/* Begin a transaction for database. */
	v = sqlite3GetVdbe(pParse);
	if (v == 0) {
		goto exit_rename_table;
	}
	sql_set_multi_write(pParse, false);

	/* Drop and reload the internal table schema. */
	reloadTableSchema(pParse, pTab, zName);

 exit_rename_table:
	sqlite3SrcListDelete(db, pSrc);
	sqlite3DbFree(db, zName);
	user_session->sql_flags = savedDbFlags;
}

/*
 * This function is called after an "ALTER TABLE ... ADD" statement
 * has been parsed. Argument pColDef contains the text of the new
 * column definition.
 *
 * The Table structure pParse->pNewTable was extended to include
 * the new column during parsing.
 */
void
sqlite3AlterFinishAddColumn(Parse * pParse, Token * pColDef)
{
	/* This function is not implemented yet #3075. */
	unreachable();

	Table *pNew;		/* Copy of pParse->pNewTable */
	Table *pTab;		/* Table being altered */
	const char *zTab;	/* Table name */
	Expr *pDflt;		/* Default value for the new column */
	sqlite3 *db;		/* The database connection; */
	Vdbe *v = pParse->pVdbe;	/* The prepared statement under construction */

	db = pParse->db;
	if (pParse->nErr || db->mallocFailed)
		return;
	assert(v != 0);
	pNew = pParse->pNewTable;
	assert(pNew);

	/* Skip the "sqlite_altertab_" prefix on the name. */
	zTab = &pNew->def->name[16];
	assert(pNew->def != NULL);
	pDflt = space_column_default_expr(pNew->def->id,
					  pNew->def->field_count - 1);
	pTab = sqlite3HashFind(&db->pSchema->tblHash, zTab);;
	assert(pTab);

	/* If the default value for the new column was specified with a
	 * literal NULL, then set pDflt to 0. This simplifies checking
	 * for an SQL NULL default below.
	 */
	assert(pDflt == 0 || pDflt->op == TK_SPAN);
	if (pDflt && pDflt->pLeft->op == TK_NULL) {
		pDflt = 0;
	}

	/* Check that the new column is not specified as PRIMARY KEY or UNIQUE.
	 * If there is a NOT NULL constraint, then the default value for the
	 * column must not be NULL.
	 */
	struct Index *pk = sqlite3PrimaryKeyIndex(pTab);
	assert(pk != NULL);
	struct key_def *pk_key_def = pk->def->key_def;
	if (key_def_find(pk_key_def, pNew->def->field_count - 1) != NULL) {
		sqlite3ErrorMsg(pParse, "Cannot add a PRIMARY KEY column");
		return;
	}
	if (pNew->pIndex) {
		sqlite3ErrorMsg(pParse, "Cannot add a UNIQUE column");
		return;
	}
	assert(pNew->def->fields[pNew->def->field_count - 1].is_nullable ==
	       action_is_nullable(pNew->def->fields[
		pNew->def->field_count - 1].nullable_action));
	if (!pNew->def->fields[pNew->def->field_count - 1].is_nullable &&
	    pDflt == NULL) {
		sqlite3ErrorMsg(pParse,
				"Cannot add a NOT NULL column with default value NULL");
		return;
	}

	/* Ensure the default expression is something that sqlite3ValueFromExpr()
	 * can handle (i.e. not CURRENT_TIME etc.)
	 */
	if (pDflt) {
		sqlite3_value *pVal = 0;
		int rc;
		rc = sqlite3ValueFromExpr(db, pDflt,
					  AFFINITY_BLOB, &pVal);
		assert(rc == SQLITE_OK || rc == SQLITE_NOMEM);
		if (rc != SQLITE_OK) {
			assert(db->mallocFailed == 1);
			return;
		}
		if (!pVal) {
			sqlite3ErrorMsg(pParse,
					"Cannot add a column with non-constant default");
			return;
		}
		sqlite3ValueFree(pVal);
	}

	/* Modify the CREATE TABLE statement. */
	/* TODO: Adopt for Tarantool. */
	(void)pColDef;

	/* Reload the schema of the modified table. */
	reloadTableSchema(pParse, pTab, pTab->def->name);
}

/*
 * This function is called by the parser after the table-name in
 * an "ALTER TABLE <table-name> ADD" statement is parsed. Argument
 * pSrc is the full-name of the table being altered.
 *
 * This routine makes a (partial) copy of the Table structure
 * for the table being altered and sets Parse.pNewTable to point
 * to it. Routines called by the parser as the column definition
 * is parsed (i.e. sqlite3AddColumn()) add the new Column data to
 * the copy. The copy of the Table structure is deleted by tokenize.c
 * after parsing is finished.
 *
 * Routine sqlite3AlterFinishAddColumn() will be called to complete
 * coding the "ALTER TABLE ... ADD" statement.
 */
void
sqlite3AlterBeginAddColumn(Parse * pParse, SrcList * pSrc)
{
	/* This function is not implemented yet #3075. */
	unreachable();

	Table *pNew;
	Table *pTab;
	Vdbe *v;
	sqlite3 *db = pParse->db;

	/* Look up the table being altered. */
	assert(pParse->pNewTable == 0);
	if (db->mallocFailed)
		goto exit_begin_add_column;
	pTab = sqlite3LocateTable(pParse, 0, pSrc->a[0].zName);
	if (pTab == NULL)
		goto exit_begin_add_column;

	/* Make sure this is not an attempt to ALTER a view. */
	if (pTab->def->opts.is_view) {
		sqlite3ErrorMsg(pParse, "Cannot add a column to a view");
		goto exit_begin_add_column;
	}

	assert(pTab->addColOffset > 0);

	/* Put a copy of the Table struct in Parse.pNewTable for the
	 * sqlite3AddColumn() function and friends to modify.  But modify
	 * the name by adding an "sqlite_altertab_" prefix.  By adding this
	 * prefix, we insure that the name will not collide with an existing
	 * table because user table are not allowed to have the "sqlite_"
	 * prefix on their name.
	 */
	pNew = (Table *) sqlite3DbMallocZero(db, sizeof(Table));
	if (!pNew)
		goto exit_begin_add_column;
	pNew->def = space_def_dup(pTab->def);
	if (pNew->def == NULL) {
		sqlite3DbFree(db, pNew);
		sqlite3OomFault(db);
		goto exit_begin_add_column;
	}
	pParse->pNewTable = pNew;
	assert(pNew->def->field_count > 0);
	/* FIXME: pNew->zName = sqlite3MPrintf(db, "sqlite_altertab_%s", pTab->zName); */
	/* FIXME: if (!pNew->aCol || !pNew->zName) { */
	for (uint32_t i = 0; i < pNew->def->field_count; i++)
		pNew->def->fields[i] = field_def_default;
	pNew->pSchema = db->pSchema;
	pNew->addColOffset = pTab->addColOffset;
	pNew->nTabRef = 1;

	/* Begin a transaction. */
	sql_set_multi_write(pParse, false);
	v = sqlite3GetVdbe(pParse);
	if (!v)
		goto exit_begin_add_column;
 exit_begin_add_column:
	sqlite3SrcListDelete(db, pSrc);
	return;
}

/*
 * This function implements part of the ALTER TABLE command.
 * The first argument is the text of a CREATE TABLE or CREATE INDEX command.
 * The second is a table name. The table name in the CREATE TABLE or
 * CREATE INDEX statement is replaced with the second argument and
 * the result returned. There is no need to deallocate returned memory, since
 * it will be doe automatically by VDBE. Note that new statement always
 * contains quoted new table name.
 *
 * Examples:
 *
 * sqlite_rename_table('CREATE TABLE abc(a, b, c)', 'def')
 *     -> 'CREATE TABLE "def"(a, b, c)'
 *
 * sqlite_rename_table('CREATE INDEX i ON abc(a)', 'def')
 *     -> 'CREATE INDEX i ON "def"(a, b, c)'
 *
 * @param sql_stmt text of a CREATE TABLE or CREATE INDEX statement
 * @param table_name new table name
 * @param[out] is_quoted true if statement to be modified contains quoted name
 *
 * @retval new SQL statement on success, NULL otherwise.
 */
char*
rename_table(sqlite3 *db, const char *sql_stmt, const char *table_name,
	     bool *is_quoted)
{
	assert(sql_stmt);
	assert(table_name);
	assert(is_quoted);

	int token;
	Token old_name;
	const char *csr = sql_stmt;
	int len = 0;
	char *new_sql_stmt;
	bool unused;

	/* The principle used to locate the table name in the CREATE TABLE
	 * statement is that the table name is the first non-space token that
	 * is immediately followed by a TK_LP or TK_USING token.
	 */
	do {
		if (!*csr) {
			/* Ran out of input before finding a bracket. */
			return NULL;
		}
		/* Store the token that zCsr points to in tname. */
		old_name.z = (char *)csr;
		old_name.n = len;
		/* Advance zCsr to the next token.
		 * Store that token type in 'token', and its length
		 * in 'len' (to be used next iteration of this loop).
		 */
		do {
			csr += len;
			len = sql_token(csr, &token, &unused);
		} while (token == TK_SPACE);
		assert(len > 0);
	} while (token != TK_LP && token != TK_USING);

	if (*old_name.z == '"')
		*is_quoted = true;
	/* No need to care about deallocating zRet, since its memory
	 * will be automatically freed by VDBE.
	 */
	new_sql_stmt = sqlite3MPrintf(db, "%.*s\"%w\"%s",
				      (int)((old_name.z) - sql_stmt), sql_stmt,
				      table_name, old_name.z + old_name.n);
	return new_sql_stmt;
}

/* This function is used to implement the ALTER TABLE command.
 * The table name in the CREATE TRIGGER statement is replaced with the third
 * argument and the result returned. This is analagous to rename_table()
 * above, except for CREATE TRIGGER, not CREATE INDEX and CREATE TABLE.
 */
char*
rename_trigger(sqlite3 *db, char const *sql_stmt, char const *table_name,
	       bool *is_quoted)
{
	assert(sql_stmt);
	assert(table_name);
	assert(is_quoted);

	int token;
	Token tname;
	int dist = 3;
	char const *csr = (char const*)sql_stmt;
	int len = 0;
	char *new_sql_stmt;
	bool unused;

	/* The principle used to locate the table name in the CREATE TRIGGER
	 * statement is that the table name is the first token that is immediately
	 * preceded by either TK_ON or TK_DOT and immediately followed by one
	 * of TK_WHEN, TK_BEGIN or TK_FOR.
	 */
	do {
		if (!*csr) {
			/* Ran out of input before finding the table name. */
			return NULL;
		}
		/* Store the token that csr points to in tname. */
		tname.z = (char *) csr;
		tname.n = len;
		/* Advance zCsr to the next token. Store that token type in 'token',
		 * and its length in 'len' (to be used next iteration of this loop).
		 */
		do {
			csr += len;
			len = sql_token(csr, &token, &unused);
		} while (token == TK_SPACE);
		assert(len > 0);
		/* Variable 'dist' stores the number of tokens read since the most
		 * recent TK_ON. This means that when a WHEN, FOR or BEGIN
		 * token is read and 'dist' equals 2, the condition stated above
		 * to be met.
		 *
		 * Note that ON cannot be a table or column name, so
		 * there is no need to worry about syntax like
		 * "CREATE TRIGGER ... ON ON BEGIN ..." etc.
		 */
		dist++;
		if (token == TK_ON) {
			dist = 0;
		}
	} while (dist != 2 ||
		 (token != TK_WHEN && token != TK_FOR && token != TK_BEGIN));

	if (*tname.z  == '"')
		*is_quoted = true;
	/* Variable tname now contains the token that is the old table-name
	 * in the CREATE TRIGGER statement.
	 */
	new_sql_stmt = sqlite3MPrintf(db, "%.*s\"%w\"%s",
				      (int)((tname.z) - sql_stmt), sql_stmt,
				      table_name, tname.z + tname.n);
	return new_sql_stmt;
}

#endif				/* SQLITE_ALTER_TABLE */
