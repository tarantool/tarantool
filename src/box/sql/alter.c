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
	sqlite3VdbeAddRenameTableOp(v, pTab->tnum, zNewName);
}

/*
 * Parameter zName is the name of a table that is about to be altered
 * (either with ALTER TABLE ... RENAME TO or ALTER TABLE ... ADD COLUMN).
 * If the table is a system table, this function leaves an error message
 * in pParse->zErr (system tables may not be altered) and returns non-zero.
 *
 * Or, if zName is not a system table, zero is returned.
 */
static int
isSystemTable(Parse * pParse, const char *zName)
{
	if (0 == sqlite3StrNICmp(zName, "_", 1)) {
		sqlite3ErrorMsg(pParse, "table %s may not be altered", zName);
		return 1;
	}
	return 0;
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

	pTab = sqlite3LocateTableItem(pParse, 0, &pSrc->a[0]);
	if (!pTab)
		goto exit_rename_table;
	assert(sqlite3SchemaToIndex(pParse->db, pTab->pSchema) == 0);

	user_session->sql_flags |= SQLITE_PreferBuiltin;

	/* Get a NULL terminated version of the new table name. */
	zName = sqlite3NameFromToken(db, pName);
	if (!zName)
		goto exit_rename_table;

	/* Check that a table named 'zName' does not already exist
	 * in database. If so, this is an error.
	 */
	if (sqlite3FindTable(db, zName)) {
		sqlite3ErrorMsg(pParse,
				"there is already another table or index with this name: %s",
				zName);
		goto exit_rename_table;
	}

	/* Make sure it is not a system table being altered, or a reserved name
	 * that the table is being renamed to.
	 */
	if (SQLITE_OK != isSystemTable(pParse, pTab->zName)) {
		goto exit_rename_table;
	}
	if (SQLITE_OK != sqlite3CheckObjectName(pParse, zName)) {
		goto exit_rename_table;
	}
#ifndef SQLITE_OMIT_VIEW
	if (pTab->pSelect) {
		sqlite3ErrorMsg(pParse, "view %s may not be altered",
				pTab->zName);
		goto exit_rename_table;
	}
#endif

#ifndef SQLITE_OMIT_AUTHORIZATION
	/* Invoke the authorization callback. */
	if (sqlite3AuthCheck(pParse, SQLITE_ALTER_TABLE, zDb, pTab->zName, 0)) {
		goto exit_rename_table;
	}
#endif

	/* Begin a transaction for database.
	 * Then modify the schema cookie (since the ALTER TABLE modifies the
	 * schema).
	 */
	v = sqlite3GetVdbe(pParse);
	if (v == 0) {
		goto exit_rename_table;
	}
	sqlite3BeginWriteOperation(pParse, false);

#ifndef SQLITE_OMIT_AUTOINCREMENT
	/* If the sqlite_sequence table exists in this database, then update
	 * it with the new table name.
	 */
	if (sqlite3FindTable(db, "sqlite_sequence")) {
		sqlite3NestedParse(pParse,
				   "UPDATE sqlite_sequence set name = %Q WHERE name = %Q",
				   zName, pTab->zName);
	}
#endif

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
	Table *pNew;		/* Copy of pParse->pNewTable */
	Table *pTab;		/* Table being altered */
	const char *zTab;	/* Table name */
	Column *pCol;		/* The new column */
	Expr *pDflt;		/* Default value for the new column */
	sqlite3 *db;		/* The database connection; */
	Vdbe *v = pParse->pVdbe;	/* The prepared statement under construction */
	struct session *user_session = current_session();

	db = pParse->db;
	if (pParse->nErr || db->mallocFailed)
		return;
	assert(v != 0);
	pNew = pParse->pNewTable;
	assert(pNew);

	zTab = &pNew->zName[16];	/* Skip the "sqlite_altertab_" prefix on the name */
	pCol = &pNew->aCol[pNew->nCol - 1];
	pDflt = pCol->pDflt;
	pTab = sqlite3FindTable(db, zTab);
	assert(pTab);

#ifndef SQLITE_OMIT_AUTHORIZATION
	/* Invoke the authorization callback. */
	if (sqlite3AuthCheck(pParse, SQLITE_ALTER_TABLE, zDb, pTab->zName, 0)) {
		return;
	}
#endif

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
	if (pCol->colFlags & COLFLAG_PRIMKEY) {
		sqlite3ErrorMsg(pParse, "Cannot add a PRIMARY KEY column");
		return;
	}
	if (pNew->pIndex) {
		sqlite3ErrorMsg(pParse, "Cannot add a UNIQUE column");
		return;
	}
	if ((user_session->sql_flags & SQLITE_ForeignKeys) && pNew->pFKey
	    && pDflt) {
		sqlite3ErrorMsg(pParse,
				"Cannot add a REFERENCES column with non-NULL default value");
		return;
	}
	if (pCol->notNull && !pDflt) {
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
					  SQLITE_AFF_BLOB, &pVal);
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
	reloadTableSchema(pParse, pTab, pTab->zName);
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
	Table *pNew;
	Table *pTab;
	Vdbe *v;
	int i;
	int nAlloc;
	sqlite3 *db = pParse->db;

	/* Look up the table being altered. */
	assert(pParse->pNewTable == 0);
	if (db->mallocFailed)
		goto exit_begin_add_column;
	pTab = sqlite3LocateTableItem(pParse, 0, &pSrc->a[0]);
	if (!pTab)
		goto exit_begin_add_column;

	/* Make sure this is not an attempt to ALTER a view. */
	if (pTab->pSelect) {
		sqlite3ErrorMsg(pParse, "Cannot add a column to a view");
		goto exit_begin_add_column;
	}
	if (SQLITE_OK != isSystemTable(pParse, pTab->zName)) {
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
	pParse->pNewTable = pNew;
	pNew->nTabRef = 1;
	pNew->nCol = pTab->nCol;
	assert(pNew->nCol > 0);
	nAlloc = (((pNew->nCol - 1) / 8) * 8) + 8;
	assert(nAlloc >= pNew->nCol && nAlloc % 8 == 0
	       && nAlloc - pNew->nCol < 8);
	pNew->aCol =
	    (Column *) sqlite3DbMallocZero(db, sizeof(Column) * nAlloc);
	pNew->zName = sqlite3MPrintf(db, "sqlite_altertab_%s", pTab->zName);
	if (!pNew->aCol || !pNew->zName) {
		assert(db->mallocFailed);
		goto exit_begin_add_column;
	}
	memcpy(pNew->aCol, pTab->aCol, sizeof(Column) * pNew->nCol);
	for (i = 0; i < pNew->nCol; i++) {
		Column *pCol = &pNew->aCol[i];
		pCol->zName = sqlite3DbStrDup(db, pCol->zName);
		pCol->zColl = 0;
		pCol->pDflt = 0;
	}
	pNew->pSchema = db->mdb.pSchema;
	pNew->addColOffset = pTab->addColOffset;
	pNew->nTabRef = 1;

	/* Begin a transaction and increment the schema cookie.  */
	sqlite3BeginWriteOperation(pParse, 0);
	v = sqlite3GetVdbe(pParse);
	if (!v)
		goto exit_begin_add_column;
	sqlite3ChangeCookie(pParse);

 exit_begin_add_column:
	sqlite3SrcListDelete(db, pSrc);
	return;
}
#endif				/* SQLITE_ALTER_TABLE */
