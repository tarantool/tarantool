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
 * This file contains the implementation of the sqlite3_prepare()
 * interface, and routines that contribute to loading the database schema
 * from disk.
 */
#include "sqliteInt.h"
#include "tarantoolInt.h"
#include "box/space.h"
#include "box/session.h"

/*
 * Fill the InitData structure with an error message that indicates
 * that the database is corrupt.
 */
static void
corruptSchema(struct init_data *data,	/* Initialization context */
	      const char *zObj,	/* Object being parsed at the point of error */
	      const char *zExtra	/* Error information */
    )
{
	sqlite3 *db = data->db;
	if (!db->mallocFailed) {
		char *z;
		if (zObj == 0)
			zObj = "?";
		z = sqlite3MPrintf(db, "malformed database schema (%s)", zObj);
		if (zExtra)
			z = sqlite3MPrintf(db, "%z - %s", z, zExtra);
		sqlite3DbFree(db, *data->pzErrMsg);
		*data->pzErrMsg = z;
	}
	data->rc = db->mallocFailed ? SQLITE_NOMEM_BKPT : SQLITE_CORRUPT_BKPT;
}

/* Necessary for appropriate value return in InitCallback.
 * Otherwise it will return uint32_t instead of 64 bit pointer.
 */
struct space *space_by_id(uint32_t id);

int
sql_init_callback(struct init_data *init, const char *name,
		  uint32_t space_id, uint32_t index_id, const char *sql)
{
	sqlite3 *db = init->db;
	if (db->mallocFailed) {
		corruptSchema(init, name, 0);
		return 1;
	}

	assert(space_id > 0);
	if ((strlen(sql) > 7) &&
	    sqlite3_strnicmp(sql, "create ", 7) == 0) {
		/* Call the parser to process a CREATE TABLE, INDEX or VIEW.
		 * But because db->init.busy is set to 1, no VDBE code is generated
		 * or executed.  All the parser does is build the internal data
		 * structures that describe the table, index, or view.
		 */
		int rc;
		sqlite3_stmt *pStmt;
		TESTONLY(int rcp);	/* Return code from sqlite3_prepare() */

		assert(db->init.busy);
		db->init.space_id = space_id;
		db->init.index_id = index_id;
		db->init.orphanTrigger = 0;
		TESTONLY(rcp =) sqlite3_prepare(db, sql,
						strlen(sql) + 1, &pStmt, 0);
		rc = db->errCode;
		assert((rc & 0xFF) == (rcp & 0xFF));
		if (SQLITE_OK != rc) {
			init->rc = rc;
			if (rc == SQLITE_NOMEM)
				sqlite3OomFault(db);
			else if (rc != SQLITE_INTERRUPT &&
				 (rc & 0xFF) != SQLITE_LOCKED)
				corruptSchema(init, name, sqlite3_errmsg(db));
		}
		sqlite3_finalize(pStmt);
	} else if (name == NULL || (sql != NULL && sql[0] != 0)) {
		corruptSchema(init, name, 0);
	} else {
		/* If the SQL column is blank it means this is an index that
		 * was created to be the PRIMARY KEY or to fulfill a UNIQUE
		 * constraint for a CREATE TABLE.  The index should have already
		 * been created when we processed the CREATE TABLE.  All we have
		 * to do here is record the root page number for that index.
		 */
		Index *pIndex;
		struct space *space = space_by_id(space_id);
		const char *zSpace = space_name(space);
		pIndex = sqlite3LocateIndex(db, name, zSpace);
		assert(pIndex != NULL);
		pIndex->def->iid = index_id;
	}
	return 0;
}

/*
 * Attempt to read the database schema and initialize internal
 * data structures for a single database file.
 * Return one of the SQLITE_ error codes to indicate or failure.
 */
extern int
sqlite3InitDatabase(sqlite3 * db)
{
	int rc;
	struct init_data init;

	assert(db->pSchema != NULL);

	memset(&init, 0, sizeof(init));
	init.db = db;

	/* Load schema from Tarantool - into the primary db only. */
	tarantoolSqlite3LoadSchema(&init);

	if (init.rc) {
		rc = init.rc;
		goto error_out;
	}
	/* Read the schema information out of the schema tables
	 */
	assert(db->init.busy);
	{
		rc = init.rc;
		if (rc == SQLITE_OK)
			sql_analysis_load(db);
	}
	if (db->mallocFailed) {
		rc = SQLITE_NOMEM_BKPT;
		sqlite3ResetAllSchemasOfConnection(db);
	}

 error_out:
	if (rc == SQLITE_NOMEM || rc == SQLITE_IOERR_NOMEM) {
		sqlite3OomFault(db);
	}
	return rc;
}


/*
 * Compile the UTF-8 encoded SQL statement zSql into a statement handle.
 */
static int
sqlite3Prepare(sqlite3 * db,	/* Database handle. */
	       const char *zSql,	/* UTF-8 encoded SQL statement. */
	       int nBytes,	/* Length of zSql in bytes. */
	       int saveSqlFlag,	/* True to copy SQL text into the sqlite3_stmt */
	       Vdbe * pReprepare,	/* VM being reprepared */
	       sqlite3_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
	       const char **pzTail	/* OUT: End of parsed string */
    )
{
	char *zErrMsg = 0;	/* Error message */
	int rc = SQLITE_OK;	/* Result code */
	int i;			/* Loop counter */
	Parse sParse;		/* Parsing context */
	sql_parser_create(&sParse, db);
	sParse.pReprepare = pReprepare;
	assert(ppStmt && *ppStmt == 0);
	/* assert( !db->mallocFailed ); // not true with SQLITE_USE_ALLOCA */

	/* Check to verify that it is possible to get a read lock on all
	 * database schemas.  The inability to get a read lock indicates that
	 * some other database connection is holding a write-lock, which in
	 * turn means that the other connection has made uncommitted changes
	 * to the schema.
	 *
	 * Were we to proceed and prepare the statement against the uncommitted
	 * schema changes and if those schema changes are subsequently rolled
	 * back and different changes are made in their place, then when this
	 * prepared statement goes to run the schema cookie would fail to detect
	 * the schema change.  Disaster would follow.
	 *
	 * Note that setting READ_UNCOMMITTED overrides most lock detection,
	 * but it does *not* override schema lock detection, so this all still
	 * works even if READ_UNCOMMITTED is set.
	 */
	if (nBytes >= 0 && (nBytes == 0 || zSql[nBytes - 1] != 0)) {
		char *zSqlCopy;
		int mxLen = db->aLimit[SQLITE_LIMIT_SQL_LENGTH];
		testcase(nBytes == mxLen);
		testcase(nBytes == mxLen + 1);
		if (nBytes > mxLen) {
			sqlite3ErrorWithMsg(db, SQLITE_TOOBIG,
					    "statement too long");
			rc = sqlite3ApiExit(db, SQLITE_TOOBIG);
			goto end_prepare;
		}
		zSqlCopy = sqlite3DbStrNDup(db, zSql, nBytes);
		if (zSqlCopy) {
			sqlite3RunParser(&sParse, zSqlCopy, &zErrMsg);
			sParse.zTail = &zSql[sParse.zTail - zSqlCopy];
			sqlite3DbFree(db, zSqlCopy);
		} else {
			sParse.zTail = &zSql[nBytes];
		}
	} else {
		sqlite3RunParser(&sParse, zSql, &zErrMsg);
	}
	assert(0 == sParse.nQueryLoop);

	if (sParse.rc == SQLITE_DONE)
		sParse.rc = SQLITE_OK;
	if (db->mallocFailed) {
		sParse.rc = SQLITE_NOMEM_BKPT;
	}
	if (pzTail) {
		*pzTail = sParse.zTail;
	}
	rc = sParse.rc;

	if (rc == SQLITE_OK && sParse.pVdbe && sParse.explain) {
		static const char *const azColName[] = {
			"addr", "opcode", "p1", "p2", "p3", "p4", "p5",
			    "comment",
			"selectid", "order", "from", "detail"
		};
		int iFirst, mx;
		if (sParse.explain == 2) {
			sqlite3VdbeSetNumCols(sParse.pVdbe, 4);
			iFirst = 8;
			mx = 12;
		} else {
			sqlite3VdbeSetNumCols(sParse.pVdbe, 8);
			iFirst = 0;
			mx = 8;
		}
		for (i = iFirst; i < mx; i++) {
			sqlite3VdbeSetColName(sParse.pVdbe, i - iFirst,
					      COLNAME_NAME, azColName[i],
					      SQLITE_STATIC);
		}
	}

	if (db->init.busy == 0) {
		Vdbe *pVdbe = sParse.pVdbe;
		sqlite3VdbeSetSql(pVdbe, zSql, (int)(sParse.zTail - zSql),
				  saveSqlFlag);
	}
	if (sParse.pVdbe && (rc != SQLITE_OK || db->mallocFailed)) {
		sqlite3VdbeFinalize(sParse.pVdbe);
		assert(!(*ppStmt));
	} else {
		*ppStmt = (sqlite3_stmt *) sParse.pVdbe;
	}

	if (zErrMsg) {
		sqlite3ErrorWithMsg(db, rc, "%s", zErrMsg);
		sqlite3DbFree(db, zErrMsg);
	} else {
		sqlite3Error(db, rc);
	}

	/* Delete any TriggerPrg structures allocated while parsing this statement. */
	while (sParse.pTriggerPrg) {
		TriggerPrg *pT = sParse.pTriggerPrg;
		sParse.pTriggerPrg = pT->pNext;
		sqlite3DbFree(db, pT);
	}

 end_prepare:

	sql_parser_destroy(&sParse);
	rc = sqlite3ApiExit(db, rc);
	assert((rc & db->errMask) == rc);
	return rc;
}

static int
sqlite3LockAndPrepare(sqlite3 * db,		/* Database handle. */
		      const char *zSql,		/* UTF-8 encoded SQL statement. */
		      int nBytes,		/* Length of zSql in bytes. */
		      int saveSqlFlag,		/* True to copy SQL text into the sqlite3_stmt */
		      Vdbe * pOld,		/* VM being reprepared */
		      sqlite3_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
		      const char **pzTail)	/* OUT: End of parsed string */
{
	int rc;

#ifdef SQLITE_ENABLE_API_ARMOR
	if (ppStmt == 0)
		return SQLITE_MISUSE_BKPT;
#endif
	*ppStmt = 0;
	if (!sqlite3SafetyCheckOk(db) || zSql == 0) {
		return SQLITE_MISUSE_BKPT;
	}
	rc = sqlite3Prepare(db, zSql, nBytes, saveSqlFlag, pOld, ppStmt,
			    pzTail);
	if (rc == SQLITE_SCHEMA) {
		sqlite3_finalize(*ppStmt);
		rc = sqlite3Prepare(db, zSql, nBytes, saveSqlFlag, pOld, ppStmt,
				    pzTail);
	}
	assert(rc == SQLITE_OK || *ppStmt == 0);
	return rc;
}

/*
 * Rerun the compilation of a statement after a schema change.
 *
 * If the statement is successfully recompiled, return SQLITE_OK. Otherwise,
 * if the statement cannot be recompiled because another connection has
 * locked the sqlite3_master table, return SQLITE_LOCKED. If any other error
 * occurs, return SQLITE_SCHEMA.
 */
int
sqlite3Reprepare(Vdbe * p)
{
	int rc;
	sqlite3_stmt *pNew;
	const char *zSql;
	sqlite3 *db;

	zSql = sqlite3_sql((sqlite3_stmt *) p);
	assert(zSql != 0);	/* Reprepare only called for prepare_v2() statements */
	db = sqlite3VdbeDb(p);
	rc = sqlite3LockAndPrepare(db, zSql, -1, 0, p, &pNew, 0);
	if (rc) {
		if (rc == SQLITE_NOMEM) {
			sqlite3OomFault(db);
		}
		assert(pNew == 0);
		return rc;
	} else {
		assert(pNew != 0);
	}
	sqlite3VdbeSwap((Vdbe *) pNew, p);
	sqlite3TransferBindings(pNew, (sqlite3_stmt *) p);
	sqlite3VdbeResetStepResult((Vdbe *) pNew);
	sqlite3VdbeFinalize((Vdbe *) pNew);
	return SQLITE_OK;
}

/*
 * Two versions of the official API.  Legacy and new use.  In the legacy
 * version, the original SQL text is not saved in the prepared statement
 * and so if a schema change occurs, SQLITE_SCHEMA is returned by
 * sqlite3_step().  In the new version, the original SQL text is retained
 * and the statement is automatically recompiled if an schema change
 * occurs.
 */
int
sqlite3_prepare(sqlite3 * db,		/* Database handle. */
		const char *zSql,	/* UTF-8 encoded SQL statement. */
		int nBytes,		/* Length of zSql in bytes. */
		sqlite3_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
		const char **pzTail)	/* OUT: End of parsed string */
{
	int rc;
	rc = sqlite3LockAndPrepare(db, zSql, nBytes, 0, 0, ppStmt, pzTail);
	assert(rc == SQLITE_OK || ppStmt == 0 || *ppStmt == 0);	/* VERIFY: F13021 */
	return rc;
}

int
sqlite3_prepare_v2(sqlite3 * db,	/* Database handle. */
		   const char *zSql,	/* UTF-8 encoded SQL statement. */
		   int nBytes,	/* Length of zSql in bytes. */
		   sqlite3_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
		   const char **pzTail	/* OUT: End of parsed string */
    )
{
	int rc;
	rc = sqlite3LockAndPrepare(db, zSql, nBytes, 1, 0, ppStmt, pzTail);
	assert(rc == SQLITE_OK || ppStmt == 0 || *ppStmt == 0);	/* VERIFY: F13021 */
	return rc;
}

void
sql_parser_create(struct Parse *parser, sqlite3 *db)
{
	memset(parser, 0, sizeof(struct Parse));
	parser->db = db;
	rlist_create(&parser->new_fkey);
	region_create(&parser->region, &cord()->slabc);
}

void
sql_parser_destroy(Parse *parser)
{
	assert(parser != NULL);
	sqlite3 *db = parser->db;
	sqlite3DbFree(db, parser->aLabel);
	sql_expr_list_delete(db, parser->pConstExpr);
	struct fkey_parse *fk;
	rlist_foreach_entry(fk, &parser->new_fkey, link)
		sql_expr_list_delete(db, fk->selfref_cols);
	if (db != NULL) {
		assert(db->lookaside.bDisable >=
		       parser->disableLookaside);
		db->lookaside.bDisable -= parser->disableLookaside;
	}
	parser->disableLookaside = 0;
	sqlite3DbFree(db, parser->zErrMsg);
	switch (parser->parsed_ast_type) {
	case AST_TYPE_SELECT:
		sql_select_delete(db, parser->parsed_ast.select);
		break;
	case AST_TYPE_EXPR:
		sql_expr_delete(db, parser->parsed_ast.expr, false);
		break;
	case AST_TYPE_TRIGGER:
		sql_trigger_delete(db, parser->parsed_ast.trigger);
		break;
	default:
		assert(parser->parsed_ast_type == AST_TYPE_UNDEFINED);
	}
	region_destroy(&parser->region);
}
