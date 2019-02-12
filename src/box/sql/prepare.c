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
 * This file contains the implementation of the sql_prepare()
 * interface, and routines that contribute to loading the database schema
 * from disk.
 */
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "box/space.h"
#include "box/session.h"

/*
 * Compile the UTF-8 encoded SQL statement zSql into a statement handle.
 */
static int
sqlPrepare(sql * db,	/* Database handle. */
	       const char *zSql,	/* UTF-8 encoded SQL statement. */
	       int nBytes,	/* Length of zSql in bytes. */
	       int saveSqlFlag,	/* True to copy SQL text into the sql_stmt */
	       Vdbe * pReprepare,	/* VM being reprepared */
	       sql_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
	       const char **pzTail	/* OUT: End of parsed string */
    )
{
	char *zErrMsg = 0;	/* Error message */
	int rc = SQL_OK;	/* Result code */
	int i;			/* Loop counter */
	Parse sParse;		/* Parsing context */
	sql_parser_create(&sParse, db);
	sParse.pReprepare = pReprepare;
	assert(ppStmt && *ppStmt == 0);
	/* assert( !db->mallocFailed ); // not true with SQL_USE_ALLOCA */

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
		int mxLen = db->aLimit[SQL_LIMIT_SQL_LENGTH];
		testcase(nBytes == mxLen);
		testcase(nBytes == mxLen + 1);
		if (nBytes > mxLen) {
			sqlErrorWithMsg(db, SQL_TOOBIG,
					    "statement too long");
			rc = sqlApiExit(db, SQL_TOOBIG);
			goto end_prepare;
		}
		zSqlCopy = sqlDbStrNDup(db, zSql, nBytes);
		if (zSqlCopy) {
			sqlRunParser(&sParse, zSqlCopy, &zErrMsg);
			sParse.zTail = &zSql[sParse.zTail - zSqlCopy];
			sqlDbFree(db, zSqlCopy);
		} else {
			sParse.zTail = &zSql[nBytes];
		}
	} else {
		sqlRunParser(&sParse, zSql, &zErrMsg);
	}
	assert(0 == sParse.nQueryLoop);

	if (sParse.rc == SQL_DONE)
		sParse.rc = SQL_OK;
	if (db->mallocFailed) {
		sParse.rc = SQL_NOMEM_BKPT;
	}
	if (pzTail) {
		*pzTail = sParse.zTail;
	}
	rc = sParse.rc;

	if (rc == SQL_OK && sParse.pVdbe && sParse.explain) {
		static const char *const azColName[] = {
			"addr", "opcode", "p1", "p2", "p3", "p4", "p5",
			    "comment",
			"selectid", "order", "from", "detail"
		};
		int iFirst, mx;
		if (sParse.explain == 2) {
			sqlVdbeSetNumCols(sParse.pVdbe, 4);
			iFirst = 8;
			mx = 12;
		} else {
			sqlVdbeSetNumCols(sParse.pVdbe, 8);
			iFirst = 0;
			mx = 8;
		}
		for (i = iFirst; i < mx; i++) {
			sqlVdbeSetColName(sParse.pVdbe, i - iFirst,
					      COLNAME_NAME, azColName[i],
					      SQL_STATIC);
		}
	}

	if (db->init.busy == 0) {
		Vdbe *pVdbe = sParse.pVdbe;
		sqlVdbeSetSql(pVdbe, zSql, (int)(sParse.zTail - zSql),
				  saveSqlFlag);
	}
	if (sParse.pVdbe && (rc != SQL_OK || db->mallocFailed)) {
		sqlVdbeFinalize(sParse.pVdbe);
		assert(!(*ppStmt));
	} else {
		*ppStmt = (sql_stmt *) sParse.pVdbe;
	}

	if (zErrMsg) {
		sqlErrorWithMsg(db, rc, "%s", zErrMsg);
	} else {
		sqlError(db, rc);
	}

	/* Delete any TriggerPrg structures allocated while parsing this statement. */
	while (sParse.pTriggerPrg) {
		TriggerPrg *pT = sParse.pTriggerPrg;
		sParse.pTriggerPrg = pT->pNext;
		sqlDbFree(db, pT);
	}

 end_prepare:

	sql_parser_destroy(&sParse);
	rc = sqlApiExit(db, rc);
	assert((rc & db->errMask) == rc);
	return rc;
}

static int
sqlLockAndPrepare(sql * db,		/* Database handle. */
		      const char *zSql,		/* UTF-8 encoded SQL statement. */
		      int nBytes,		/* Length of zSql in bytes. */
		      int saveSqlFlag,		/* True to copy SQL text into the sql_stmt */
		      Vdbe * pOld,		/* VM being reprepared */
		      sql_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
		      const char **pzTail)	/* OUT: End of parsed string */
{
	int rc;

#ifdef SQL_ENABLE_API_ARMOR
	if (ppStmt == 0)
		return SQL_MISUSE_BKPT;
#endif
	*ppStmt = 0;
	if (!sqlSafetyCheckOk(db) || zSql == 0) {
		return SQL_MISUSE_BKPT;
	}
	rc = sqlPrepare(db, zSql, nBytes, saveSqlFlag, pOld, ppStmt,
			    pzTail);
	if (rc == SQL_SCHEMA) {
		sql_finalize(*ppStmt);
		rc = sqlPrepare(db, zSql, nBytes, saveSqlFlag, pOld, ppStmt,
				    pzTail);
	}
	assert(rc == SQL_OK || *ppStmt == 0);
	return rc;
}

/*
 * Rerun the compilation of a statement after a schema change.
 *
 * If the statement is successfully recompiled, return SQL_OK. Otherwise,
 * if the statement cannot be recompiled because another connection has
 * locked the sql_master table, return SQL_LOCKED. If any other error
 * occurs, return SQL_SCHEMA.
 */
int
sqlReprepare(Vdbe * p)
{
	int rc;
	sql_stmt *pNew;
	const char *zSql;
	sql *db;

	zSql = sql_sql((sql_stmt *) p);
	assert(zSql != 0);	/* Reprepare only called for prepare_v2() statements */
	db = sqlVdbeDb(p);
	rc = sqlLockAndPrepare(db, zSql, -1, 0, p, &pNew, 0);
	if (rc) {
		if (rc == SQL_NOMEM) {
			sqlOomFault(db);
		}
		assert(pNew == 0);
		return rc;
	} else {
		assert(pNew != 0);
	}
	sqlVdbeSwap((Vdbe *) pNew, p);
	sqlTransferBindings(pNew, (sql_stmt *) p);
	sqlVdbeResetStepResult((Vdbe *) pNew);
	sqlVdbeFinalize((Vdbe *) pNew);
	return SQL_OK;
}

/*
 * Two versions of the official API.  Legacy and new use.  In the legacy
 * version, the original SQL text is not saved in the prepared statement
 * and so if a schema change occurs, SQL_SCHEMA is returned by
 * sql_step().  In the new version, the original SQL text is retained
 * and the statement is automatically recompiled if an schema change
 * occurs.
 */
int
sql_prepare(sql * db,		/* Database handle. */
		const char *zSql,	/* UTF-8 encoded SQL statement. */
		int nBytes,		/* Length of zSql in bytes. */
		sql_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
		const char **pzTail)	/* OUT: End of parsed string */
{
	int rc;
	rc = sqlLockAndPrepare(db, zSql, nBytes, 0, 0, ppStmt, pzTail);
	assert(rc == SQL_OK || ppStmt == 0 || *ppStmt == 0);	/* VERIFY: F13021 */
	return rc;
}

int
sql_prepare_v2(sql * db,	/* Database handle. */
		   const char *zSql,	/* UTF-8 encoded SQL statement. */
		   int nBytes,	/* Length of zSql in bytes. */
		   sql_stmt ** ppStmt,	/* OUT: A pointer to the prepared statement */
		   const char **pzTail	/* OUT: End of parsed string */
    )
{
	int rc;
	rc = sqlLockAndPrepare(db, zSql, nBytes, 1, 0, ppStmt, pzTail);
	assert(rc == SQL_OK || ppStmt == 0 || *ppStmt == 0);	/* VERIFY: F13021 */
	return rc;
}

void
sql_parser_create(struct Parse *parser, sql *db)
{
	memset(parser, 0, sizeof(struct Parse));
	parser->db = db;
	rlist_create(&parser->new_fkey);
	rlist_create(&parser->record_list);
	region_create(&parser->region, &cord()->slabc);
}

void
sql_parser_destroy(Parse *parser)
{
	assert(parser != NULL);
	assert(!parser->parse_only || parser->pVdbe == NULL);
	sql *db = parser->db;
	sqlDbFree(db, parser->aLabel);
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
	sqlDbFree(db, parser->zErrMsg);
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
