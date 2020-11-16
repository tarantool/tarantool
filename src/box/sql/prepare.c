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

int
sql_stmt_compile(const char *zSql, int nBytes, struct Vdbe *pReprepare,
		 sql_stmt **ppStmt, const char **pzTail)
{
	struct sql *db = sql_get();
	int rc = 0;	/* Result code */
	Parse sParse;		/* Parsing context */
	sql_parser_create(&sParse, db, current_session()->sql_flags);
	sParse.pReprepare = pReprepare;
	*ppStmt = NULL;
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
			diag_set(ClientError, ER_SQL_PARSER_LIMIT,
				 "SQL command length", nBytes, mxLen);
			rc = -1;
			goto end_prepare;
		}
		zSqlCopy = sqlDbStrNDup(db, zSql, nBytes);
		if (zSqlCopy) {
			sqlRunParser(&sParse, zSqlCopy);
			sParse.zTail = &zSql[sParse.zTail - zSqlCopy];
			sqlDbFree(db, zSqlCopy);
		} else {
			sParse.zTail = &zSql[nBytes];
		}
	} else {
		sqlRunParser(&sParse, zSql);
	}
	assert(0 == sParse.nQueryLoop);

	if (db->mallocFailed)
		sParse.is_aborted = true;
	if (pzTail) {
		*pzTail = sParse.zTail;
	}
	if (sParse.is_aborted)
		rc = -1;

	if (rc == 0 && sParse.pVdbe != NULL && sParse.explain) {
		static const char *const azColName[] = {
			/*  0 */ "addr",
			/*  1 */ "integer",
			/*  2 */ "opcode",
			/*  3 */ "text",
			/*  4 */ "p1",
			/*  5 */ "integer",
			/*  6 */ "p2",
			/*  7 */ "integer",
			/*  8 */ "p3",
			/*  9 */ "integer",
			/* 10 */ "p4",
			/* 11 */ "text",
			/* 12 */ "p5",
			/* 13 */ "text",
			/* 14 */ "comment",
			/* 15 */ "text",
			/* 16 */ "selectid",
			/* 17 */ "integer",
			/* 18 */ "order",
			/* 19 */ "integer",
			/* 20 */ "from",
			/* 21 */ "integer",
			/* 22 */ "detail",
			/* 23 */ "text",
		};

		int name_first, name_count;
		if (sParse.explain == 2) {
			name_first = 16;
			name_count = 4;
		} else {
			name_first = 0;
			name_count = 8;
		}
		sqlVdbeSetNumCols(sParse.pVdbe, name_count);
		for (int i = 0; i < name_count; i++) {
			int name_index = 2 * i + name_first;
			vdbe_metadata_set_col_name(sParse.pVdbe, i,
						   azColName[name_index]);
			vdbe_metadata_set_col_type(sParse.pVdbe, i,
						   azColName[name_index + 1]);
		}
	}

	if (db->init.busy == 0) {
		Vdbe *pVdbe = sParse.pVdbe;
		sqlVdbeSetSql(pVdbe, zSql, (int)(sParse.zTail - zSql));
	}
	if (sParse.pVdbe != NULL && (rc != 0 || db->mallocFailed)) {
		sqlVdbeFinalize(sParse.pVdbe);
		assert(!(*ppStmt));
	} else {
		*ppStmt = (sql_stmt *) sParse.pVdbe;
	}

	/* Delete any TriggerPrg structures allocated while parsing this statement. */
	while (sParse.pTriggerPrg) {
		TriggerPrg *pT = sParse.pTriggerPrg;
		sParse.pTriggerPrg = pT->pNext;
		sqlDbFree(db, pT);
	}

 end_prepare:

	sql_parser_destroy(&sParse);
	return rc;
}

/*
 * Rerun the compilation of a statement after a schema change.
 */
int
sqlReprepare(Vdbe * p)
{
	sql_stmt *pNew;
	const char *zSql;

	zSql = sql_sql((sql_stmt *) p);
	assert(zSql != 0);
	if (sql_stmt_compile(zSql, -1, p, &pNew, 0) != 0) {
		assert(pNew == 0);
		return -1;
	}
	assert(pNew != 0);
	sqlVdbeSwap((Vdbe *) pNew, p);
	sqlTransferBindings(pNew, (sql_stmt *) p);
	sqlVdbeResetStepResult((Vdbe *) pNew);
	sqlVdbeFinalize((Vdbe *) pNew);
	return 0;
}

void
sql_parser_create(struct Parse *parser, struct sql *db, uint32_t sql_flags)
{
	memset(parser, 0, sizeof(struct Parse));
	parser->db = db;
	parser->sql_flags = sql_flags;
	parser->line_count = 1;
	parser->line_pos = 1;
	parser->has_autoinc = false;
	region_create(&parser->region, &cord()->slabc);
}

void
sql_parsed_ast_destroy(struct sql *db, struct sql_parsed_ast *ast)
{
	if (ast->keep_ast != true) {
		switch (ast->ast_type) {
		case AST_TYPE_SELECT:
			sql_select_delete(db, ast->select);
			break;
		case AST_TYPE_EXPR:
			sql_expr_delete(db, ast->expr, false);
			break;
		case AST_TYPE_TRIGGER:
			sql_trigger_delete(db, ast->trigger);
			break;
		default:
			assert(ast->ast_type == AST_TYPE_UNDEFINED);
		}
	}

}

void
sql_parser_destroy(Parse *parser)
{
	assert(parser != NULL);
	sql *db = parser->db;
	sqlDbFree(db, parser->aLabel);
	sql_expr_list_delete(db, parser->pConstExpr);
	struct create_fk_constraint_parse_def *create_fk_constraint_parse_def =
		&parser->create_fk_constraint_parse_def;
	create_fk_constraint_parse_def_destroy(create_fk_constraint_parse_def);
	if (db != NULL) {
		assert(db->lookaside.bDisable >=
		       parser->disableLookaside);
		db->lookaside.bDisable -= parser->disableLookaside;
	}
	parser->disableLookaside = 0;
	sql_parsed_ast_destroy(db, &parser->parsed_ast);
	region_destroy(&parser->region);
}
