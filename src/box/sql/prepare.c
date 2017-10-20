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
corruptSchema(InitData * pData,	/* Initialization context */
	      const char *zObj,	/* Object being parsed at the point of error */
	      const char *zExtra	/* Error information */
    )
{
	sqlite3 *db = pData->db;
	if (!db->mallocFailed) {
		char *z;
		if (zObj == 0)
			zObj = "?";
		z = sqlite3MPrintf(db, "malformed database schema (%s)", zObj);
		if (zExtra)
			z = sqlite3MPrintf(db, "%z - %s", z, zExtra);
		sqlite3DbFree(db, *pData->pzErrMsg);
		*pData->pzErrMsg = z;
	}
	pData->rc = db->mallocFailed ? SQLITE_NOMEM_BKPT : SQLITE_CORRUPT_BKPT;
}

/* Necessary for appropriate value return in InitCallback.
 * Otherwise it will return uint32_t instead of 64 bit pointer.
 */
struct space *space_by_id(uint32_t id);
/*
 * This is the callback routine for the code that initializes the
 * database.  See sqlite3Init() below for additional information.
 * This routine is also called from the OP_ParseSchema opcode of the VDBE.
 *
 * Each callback contains the following information:
 *
 *     argv[0] = name of thing being created
 *     argv[1] = root page number address.
 *     argv[2] = SQL text for the CREATE statement.
 *
 */
int
sqlite3InitCallback(void *pInit, int argc, char **argv, char **NotUsed)
{
	InitData *pData = (InitData *) pInit;
	sqlite3 *db = pData->db;
	assert(argc == 3);
	UNUSED_PARAMETER2(NotUsed, argc);
	assert(sqlite3_mutex_held(db->mutex));
	DbClearProperty(db, DB_Empty);
	if (db->mallocFailed) {
		corruptSchema(pData, argv[0], 0);
		return 1;
	}

	if (argv == 0)
		return 0;	/* Might happen if EMPTY_RESULT_CALLBACKS are on */
	if (argv[1] == 0) {
		corruptSchema(pData, argv[0], 0);
	} else if ((strlen(argv[2]) > 7) &&
		   sqlite3_strnicmp(argv[2], "create ", 7) == 0) {
		/* Call the parser to process a CREATE TABLE, INDEX or VIEW.
		 * But because db->init.busy is set to 1, no VDBE code is generated
		 * or executed.  All the parser does is build the internal data
		 * structures that describe the table, index, or view.
		 */
		int rc;
		sqlite3_stmt *pStmt;
		TESTONLY(int rcp);	/* Return code from sqlite3_prepare() */

		assert(db->init.busy);
		db->init.newTnum = *((int *)argv[1]);
		db->init.orphanTrigger = 0;
		TESTONLY(rcp =) sqlite3_prepare(db, argv[2],
						strlen(argv[2]) + 1, &pStmt, 0);
		rc = db->errCode;
		assert((rc & 0xFF) == (rcp & 0xFF));
		if (SQLITE_OK != rc) {
			pData->rc = rc;
			if (rc == SQLITE_NOMEM) {
				sqlite3OomFault(db);
			} else if (rc != SQLITE_INTERRUPT
				   && (rc & 0xFF) != SQLITE_LOCKED) {
				corruptSchema(pData, argv[0],
					      sqlite3_errmsg(db));
			}
		}
		sqlite3_finalize(pStmt);
	} else if (argv[0] == 0 || (argv[2] != 0 && argv[2][0] != 0)) {
		corruptSchema(pData, argv[0], 0);
	} else {
		/* If the SQL column is blank it means this is an index that
		 * was created to be the PRIMARY KEY or to fulfill a UNIQUE
		 * constraint for a CREATE TABLE.  The index should have already
		 * been created when we processed the CREATE TABLE.  All we have
		 * to do here is record the root page number for that index.
		 */
		Index *pIndex;
		long pageNo = *((long *)argv[1]);
		int iSpace = (int)SQLITE_PAGENO_TO_SPACEID(pageNo);
		struct space *pSpace = space_by_id(iSpace);
		const char *zSpace = space_name(pSpace);
		pIndex = sqlite3LocateIndex(db, argv[0], zSpace);
		if (pIndex == 0) {
			/* This can occur if there exists an index on a TEMP table which
			 * has the same name as another index on a permanent index.  Since
			 * the permanent table is hidden by the TEMP table, we can also
			 * safely ignore the index on the permanent table.
			 */
			/* Do Nothing */ ;
		}
		pIndex->tnum = pageNo;
	}
	return 0;
}

/*
 * Attempt to read the database schema and initialize internal
 * data structures for a single database file.
 * Return one of the SQLITE_ error codes to indicate or failure.
 */
extern int
sqlite3InitDatabase(sqlite3 * db, char **pzErrMsg)
{
	int rc;
	int i;
#ifndef SQLITE_OMIT_DEPRECATED
	int size;
#endif
	Db *pDb;
	int meta[5];
	InitData initData;
	int openedTransaction = 0;

	assert(db->mdb.pSchema);
	assert(sqlite3_mutex_held(db->mutex));
	assert(sqlite3BtreeHoldsMutex(db->mdb.pBt));

	memset(&initData, 0, sizeof(InitData));
	initData.db = db;

	/* Load schema from Tarantool - into the primary db only. */
	tarantoolSqlite3LoadSchema(&initData);

	if (initData.rc) {
		rc = initData.rc;
		goto error_out;
	}

	/* Create a cursor to hold the database open
	 */
	pDb = &db->mdb;
	if (pDb->pBt == 0) {
		return SQLITE_OK;
	}

	/* If there is not already a read-only (or read-write) transaction opened
	 * on the b-tree database, open one now. If a transaction is opened, it
	 * will be closed before this function returns.
	 */
	sqlite3BtreeEnter(pDb->pBt);
	if (!sqlite3BtreeIsInReadTrans(pDb->pBt)) {
		rc = sqlite3BtreeBeginTrans(pDb->pBt, 0, 0);
		if (rc != SQLITE_OK) {
			sqlite3SetString(pzErrMsg, db, sqlite3ErrStr(rc));
			goto initone_error_out;
		}
		openedTransaction = 1;
	}

	/* Get the database meta information.
	 *
	 * Meta values are as follows:
	 *    meta[0]   Schema cookie.  Changes with each schema change.
	 *    meta[1]   File format of schema layer.
	 *    meta[2]   Size of the page cache.
	 *    meta[3]   Db text encoding. 1:UTF-8 2:UTF-16LE 3:UTF-16BE
	 *    meta[4]   User version
	 *
	 * Note: The #defined SQLITE_UTF* symbols in sqliteInt.h correspond to
	 * the possible values of meta[4].
	 */
	for (i = 0; i < ArraySize(meta); i++) {
		sqlite3BtreeGetMeta(pDb->pBt, i + 1, (u32 *) & meta[i]);
	}
	pDb->pSchema->schema_cookie = meta[BTREE_SCHEMA_VERSION - 1];

	/* If opening a non-empty database, check the text encoding. For the
	 * main database, set sqlite3.enc to the encoding of the main database.
	 * For an attached db, it is an error if the encoding is not the same
	 * as sqlite3.enc.
	 */
	if (meta[BTREE_TEXT_ENCODING - 1]) {	/* text encoding */
		ENC(db) = SQLITE_UTF8;
	} else {
		DbSetProperty(db, DB_Empty);
	}
	pDb->pSchema->enc = ENC(db);

	if (pDb->pSchema->cache_size == 0) {
#ifndef SQLITE_OMIT_DEPRECATED
		size = sqlite3AbsInt32(meta[BTREE_DEFAULT_CACHE_SIZE - 1]);
		if (size == 0) {
			size = SQLITE_DEFAULT_CACHE_SIZE;
		}
		pDb->pSchema->cache_size = size;
#else
		pDb->pSchema->cache_size = SQLITE_DEFAULT_CACHE_SIZE;
#endif
		sqlite3BtreeSetCacheSize(pDb->pBt, pDb->pSchema->cache_size);
	}

	/*
	 * file_format==1    Version 3.0.0.
	 * file_format==2    Version 3.1.3.  // ALTER TABLE ADD COLUMN
	 * file_format==3    Version 3.1.4.  // ditto but with non-NULL defaults
	 * file_format==4    Version 3.3.0.  // DESC indices.  Boolean constants
	 */
	pDb->pSchema->file_format = (u8) meta[BTREE_FILE_FORMAT - 1];
	if (pDb->pSchema->file_format == 0) {
		pDb->pSchema->file_format = 1;
	}
	if (pDb->pSchema->file_format > SQLITE_MAX_FILE_FORMAT) {
		sqlite3SetString(pzErrMsg, db, "unsupported file format");
		rc = SQLITE_ERROR;
		goto initone_error_out;
	}

	/* Read the schema information out of the schema tables
	 */
	assert(db->init.busy);
	{
#ifndef SQLITE_OMIT_AUTHORIZATION
		{
			sqlite3_xauth xAuth;
			xAuth = db->xAuth;
			db->xAuth = 0;
#endif
			rc = SQLITE_OK;
#ifndef SQLITE_OMIT_AUTHORIZATION
			db->xAuth = xAuth;
		}
#endif
		rc = initData.rc;
#ifndef SQLITE_OMIT_ANALYZE
		if (rc == SQLITE_OK) {
			sqlite3AnalysisLoad(db);
		}
#endif
	}
	if (db->mallocFailed) {
		rc = SQLITE_NOMEM_BKPT;
		sqlite3ResetAllSchemasOfConnection(db);
	}
	if (rc == SQLITE_OK) {
		DbSetProperty(db, DB_SchemaLoaded);
	}

	/* Jump here for an error that occurs after successfully allocating
	 * curMain and calling sqlite3BtreeEnter(). For an error that occurs
	 * before that point, jump to error_out.
	 */
 initone_error_out:
	if (openedTransaction) {
		sqlite3BtreeCommit(pDb->pBt);
	}
	sqlite3BtreeLeave(pDb->pBt);

 error_out:
	if (rc == SQLITE_NOMEM || rc == SQLITE_IOERR_NOMEM) {
		sqlite3OomFault(db);
	}
	return rc;
}

/*
 * Initialize all database files - the main database file
 * Return a success code.  If an
 * error occurs, write an error message into *pzErrMsg.
 * After a database is initialized, the DB_SchemaLoaded bit is set
 * bit is set in the flags field of the Db structure. If the database
 * file was of zero-length, then the DB_Empty flag is also set.
 */
int
sqlite3Init(sqlite3 * db, char **pzErrMsg)
{
	int rc;
	struct session *user_session = current_session();
	int commit_internal = !(user_session->sql_flags & SQLITE_InternChanges);

	assert(sqlite3_mutex_held(db->mutex));
	assert(sqlite3BtreeHoldsMutex(db->mdb.pBt));
	assert(db->init.busy == 0);
	rc = SQLITE_OK;
	db->init.busy = 1;
	ENC(db) = SCHEMA_ENC(db);
	if (!DbHasProperty(db, DB_SchemaLoaded)) {
		rc = sqlite3InitDatabase(db, pzErrMsg);
		if (rc) {
			sqlite3ResetOneSchema(db);
		}
	}

	db->init.busy = 0;
	if (rc == SQLITE_OK && commit_internal) {
		sqlite3CommitInternalChanges(db);
	}

	return rc;
}

/*
 * This routine is a no-op if the database schema is already initialized.
 * Otherwise, the schema is loaded. An error code is returned.
 */
int
sqlite3ReadSchema(Parse * pParse)
{
	int rc = SQLITE_OK;
	sqlite3 *db = pParse->db;
	assert(sqlite3_mutex_held(db->mutex));
	if (!db->init.busy) {
		rc = sqlite3Init(db, &pParse->zErrMsg);
	}
	if (rc != SQLITE_OK) {
		pParse->rc = rc;
		pParse->nErr++;
	}
	return rc;
}

/*
 * Check schema cookies in all databases.  If any cookie is out
 * of date set pParse->rc to SQLITE_SCHEMA.  If all schema cookies
 * make no changes to pParse->rc.
 */
static void
schemaIsValid(Parse * pParse)
{
	sqlite3 *db = pParse->db;
	int rc;
	int cookie;

	assert(pParse->checkSchema);
	assert(sqlite3_mutex_held(db->mutex));
	int openedTransaction = 0;	/* True if a transaction is opened */
	Btree *pBt = db->mdb.pBt;	/* Btree database to read cookie from */

	/* If there is not already a read-only (or read-write) transaction opened
	 * on the b-tree database, open one now. If a transaction is opened, it
	 * will be closed immediately after reading the meta-value.
	 */
	if (!sqlite3BtreeIsInReadTrans(pBt)) {
		rc = sqlite3BtreeBeginTrans(pBt, 0, 0);
		if (rc == SQLITE_NOMEM || rc == SQLITE_IOERR_NOMEM) {
			sqlite3OomFault(db);
		}
		if (rc != SQLITE_OK)
			return;
		openedTransaction = 1;
	}

	/* Read the schema cookie from the database. If it does not match the
	 * value stored as part of the in-memory schema representation,
	 * set Parse.rc to SQLITE_SCHEMA.
	 */
	sqlite3BtreeGetMeta(pBt, BTREE_SCHEMA_VERSION, (u32 *) & cookie);
	assert(sqlite3SchemaMutexHeld(db, 0));
	if (cookie != db->mdb.pSchema->schema_cookie) {
		sqlite3ResetOneSchema(db);
		pParse->rc = SQLITE_SCHEMA;
	}

	/* Close the transaction, if one was opened. */
	if (openedTransaction) {
		sqlite3BtreeCommit(pBt);
	}
}

/*
 * Convert a schema pointer into the 0 index that indicates
 * that schema refers to a single database.
 * This method is inherited from SQLite, which has several dbs.
 * But we have only one, so it is used only in assertions.
 */
int
sqlite3SchemaToIndex(sqlite3 * db, Schema * pSchema)
{
	int i = -1000000;

	/* If pSchema is NULL, then return -1000000. This happens when code in
	 * expr.c is trying to resolve a reference to a transient table (i.e. one
	 * created by a sub-select). In this case the return value of this
	 * function should never be used.
	 *
	 * We return -1000000 instead of the more usual -1 simply because using
	 * -1000000 as the incorrect index into db->aDb[] is much
	 * more likely to cause a segfault than -1 (of course there are assert()
	 * statements too, but it never hurts to play the odds).
	 */
	assert(sqlite3_mutex_held(db->mutex));
	if (pSchema) {
		if (db->mdb.pSchema == pSchema) {
			i = 0;
		}
		assert(i == 0);
	}
	return i;
}

/*
 * Free all memory allocations in the pParse object
 */
void
sqlite3ParserReset(Parse * pParse)
{
	if (pParse) {
		sqlite3 *db = pParse->db;
		sqlite3DbFree(db, pParse->aLabel);
		sqlite3ExprListDelete(db, pParse->pConstExpr);
		if (db) {
			assert(db->lookaside.bDisable >=
			       pParse->disableLookaside);
			db->lookaside.bDisable -= pParse->disableLookaside;
		}
		pParse->disableLookaside = 0;
	}
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

	memset(&sParse, 0, PARSE_HDR_SZ);
	memset(PARSE_TAIL(&sParse), 0, PARSE_TAIL_SZ);
	sParse.pReprepare = pReprepare;
	assert(ppStmt && *ppStmt == 0);
	/* assert( !db->mallocFailed ); // not true with SQLITE_USE_ALLOCA */
	assert(sqlite3_mutex_held(db->mutex));

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
	 * This thread is currently holding mutexes on all Btrees (because
	 * of the sqlite3BtreeEnterAll() in sqlite3LockAndPrepare()) so it
	 * is not possible for another thread to start a new schema change
	 * while this routine is running.  Hence, we do not need to hold
	 * locks on the schema, we just need to make sure nobody else is
	 * holding them.
	 *
	 * Note that setting READ_UNCOMMITTED overrides most lock detection,
	 * but it does *not* override schema lock detection, so this all still
	 * works even if READ_UNCOMMITTED is set.
	 */
	Btree *pBt = db->mdb.pBt;
	assert(pBt);
	assert(sqlite3BtreeHoldsMutex(pBt));
	sParse.db = db;
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
	if (sParse.checkSchema) {
		schemaIsValid(&sParse);
	}
	if (db->mallocFailed) {
		sParse.rc = SQLITE_NOMEM_BKPT;
	}
	if (pzTail) {
		*pzTail = sParse.zTail;
	}
	rc = sParse.rc;

#ifndef SQLITE_OMIT_EXPLAIN
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
#endif

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

	sqlite3ParserReset(&sParse);
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
	sqlite3_mutex_enter(db->mutex);
	sqlite3BtreeEnterAll(db);
	rc = sqlite3Prepare(db, zSql, nBytes, saveSqlFlag, pOld, ppStmt,
			    pzTail);
	if (rc == SQLITE_SCHEMA) {
		sqlite3_finalize(*ppStmt);
		rc = sqlite3Prepare(db, zSql, nBytes, saveSqlFlag, pOld, ppStmt,
				    pzTail);
	}
	sqlite3BtreeLeaveAll(db);
	sqlite3_mutex_leave(db->mutex);
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

	assert(sqlite3_mutex_held(sqlite3VdbeDb(p)->mutex));
	zSql = sqlite3_sql((sqlite3_stmt *) p);
	assert(zSql != 0);	/* Reprepare only called for prepare_v2() statements */
	db = sqlite3VdbeDb(p);
	assert(sqlite3_mutex_held(db->mutex));
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

#ifndef SQLITE_OMIT_UTF16
/*
 * Compile the UTF-16 encoded SQL statement zSql into a statement handle.
 */
static int
sqlite3Prepare16(sqlite3 * db,		/* Database handle. */
		 const void *zSql,	/* UTF-16 encoded SQL statement. */
		 int nBytes,		/* Length of zSql in bytes. */
		 int saveSqlFlag,	/* True to save SQL text into the sqlite3_stmt */
		 sqlite3_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
		 const void **pzTail)	/* OUT: End of parsed string */
{
	/* This function currently works by first transforming the UTF-16
	 * encoded string to UTF-8, then invoking sqlite3_prepare(). The
	 * tricky bit is figuring out the pointer to return in *pzTail.
	 */
	char *zSql8;
	const char *zTail8 = 0;
	int rc = SQLITE_OK;

#ifdef SQLITE_ENABLE_API_ARMOR
	if (ppStmt == 0)
		return SQLITE_MISUSE_BKPT;
#endif
	*ppStmt = 0;
	if (!sqlite3SafetyCheckOk(db) || zSql == 0) {
		return SQLITE_MISUSE_BKPT;
	}
	if (nBytes >= 0) {
		int sz;
		const char *z = (const char *)zSql;
		for (sz = 0; sz < nBytes && (z[sz] != 0 || z[sz + 1] != 0);
		     sz += 2) {
		}
		nBytes = sz;
	}
	sqlite3_mutex_enter(db->mutex);
	zSql8 = sqlite3Utf16to8(db, zSql, nBytes, SQLITE_UTF16NATIVE);
	if (zSql8) {
		rc = sqlite3LockAndPrepare(db, zSql8, -1, saveSqlFlag, 0,
					   ppStmt, &zTail8);
	}

	if (zTail8 && pzTail) {
		/* If sqlite3_prepare returns a tail pointer, we calculate the
		 * equivalent pointer into the UTF-16 string by counting the unicode
		 * characters between zSql8 and zTail8, and then returning a pointer
		 * the same number of characters into the UTF-16 string.
		 */
		int chars_parsed =
		    sqlite3Utf8CharLen(zSql8, (int)(zTail8 - zSql8));
		*pzTail = (u8 *) zSql + sqlite3Utf16ByteLen(zSql, chars_parsed);
	}
	sqlite3DbFree(db, zSql8);
	rc = sqlite3ApiExit(db, rc);
	sqlite3_mutex_leave(db->mutex);
	return rc;
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
sqlite3_prepare16(sqlite3 * db,		/* Database handle. */
		  const void *zSql,	/* UTF-16 encoded SQL statement. */
		  int nBytes,		/* Length of zSql in bytes. */
		  sqlite3_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
		  const void **pzTail)	/* OUT: End of parsed string */
{
	int rc;
	rc = sqlite3Prepare16(db, zSql, nBytes, 0, ppStmt, pzTail);
	assert(rc == SQLITE_OK || ppStmt == 0 || *ppStmt == 0);	/* VERIFY: F13021 */
	return rc;
}

int
sqlite3_prepare16_v2(sqlite3 * db,	/* Database handle. */
		     const void *zSql,	/* UTF-16 encoded SQL statement. */
		     int nBytes,	/* Length of zSql in bytes. */
		     sqlite3_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
		     const void **pzTail)	/* OUT: End of parsed string */
{
	int rc;
	rc = sqlite3Prepare16(db, zSql, nBytes, 1, ppStmt, pzTail);
	assert(rc == SQLITE_OK || ppStmt == 0 || *ppStmt == 0);	/* VERIFY: F13021 */
	return rc;
}

#endif				/* SQLITE_OMIT_UTF16 */
