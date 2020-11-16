#include "diag.h"
#include "execute.h"
#include "lua/utils.h"
#include "sqlInt.h"

#include "sql/vdbe.h"
#include "sql/vdbeInt.h"
#include "execute.h"
#include "schema.h"
#include "session.h"
#include "box.h"

#include "sql/sqlparser.h"

#include <stdlib.h>
#include <strings.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

/*
 * Remember the SQL string for a prepared statement.
 * Looks same as sqlVdbeSetSql but for AST, not VDBE
 */
static void
sql_ast_set_sql(struct sql_parsed_ast *ast, const char *ps, int sz)
{
	if (ast == NULL)
		return;
	assert(ast->sql_query == NULL);
	ast->sql_query = sqlDbStrNDup(sql_get(), ps, sz);
}

int
sql_stmt_parse(const char *zSql, struct sql_stmt **ppStmt, struct sql_parsed_ast *ast)
{
	struct sql *db = sql_get();
	int rc = 0;	/* Result code */
	Parse sParse;
	sql_parser_create(&sParse, db, current_session()->sql_flags);

	sParse.parse_only = true;	// Parse and build AST only
	sParse.parsed_ast.keep_ast = true;

	*ppStmt = NULL;
	/* assert( !db->mallocFailed ); // not true with SQL_USE_ALLOCA */

	sqlRunParser(&sParse, zSql);
	assert(0 == sParse.nQueryLoop);

	if (sParse.is_aborted)
		rc = -1;

	if (rc != 0 || db->mallocFailed) {
		sqlVdbeFinalize(sParse.pVdbe);
		assert(!(*ppStmt));
		goto exit_cleanup;
	}
	// we have either AST or VDBE, but not both
	assert(SQL_PARSE_VALID_VDBE(&sParse) != SQL_PARSE_VALID_AST(&sParse));
	if (SQL_PARSE_VALID_VDBE(&sParse)) {
		if (db->init.busy == 0) {
			Vdbe *pVdbe = sParse.pVdbe;
			sqlVdbeSetSql(pVdbe, zSql, (int)(sParse.zTail - zSql));
		}
		*ppStmt = (sql_stmt *) sParse.pVdbe;

		/* Delete any TriggerPrg structures allocated while parsing this statement. */
		while (sParse.pTriggerPrg) {
			TriggerPrg *pT = sParse.pTriggerPrg;
			sParse.pTriggerPrg = pT->pNext;
			sqlDbFree(db, pT);
		}
	} else {	// AST constructed
		assert(SQL_PARSE_VALID_AST(&sParse));
		*ast = sParse.parsed_ast;
		assert(ast->keep_ast == true);
		sql_ast_set_sql(ast, zSql, (int)(sParse.zTail - zSql));
	}

exit_cleanup:
	sql_parser_destroy(&sParse);
	return rc;
}

static struct sql_stmt*
sql_ast_generate_vdbe(struct lua_State *L, struct sql_parsed_ast * ast)
{
	(void)L;

	// nothing to generate yet - this kind of statement is
	// not (yet) supported. Eventually this limitation
	// will be lifted
	if ( !AST_VALID(ast))
		return NULL;

	struct sql *db = sql_get();
	Parse sParse;
	bzero(&sParse, sizeof sParse);
	sql_parser_create(&sParse, db, current_session()->sql_flags);
	sParse.parse_only = false;

	struct Vdbe *v = sqlGetVdbe(&sParse);
	if (v == NULL) {
		sql_parser_destroy(&sParse);
		diag_set(OutOfMemory, sizeof(struct Vdbe), "sqlGetVdbe",
			 "sqlparser");
		return NULL;
	}

	switch (ast->ast_type) {
		case AST_TYPE_SELECT: 	// SELECT
		{
			Select *p = ast->select;
			SelectDest dest = {SRT_Output, NULL, 0, 0, 0, 0, NULL};

			int rc = sqlSelect(&sParse, p, &dest);
			if (rc != 0)
				return NULL;
			break;
		}

		default:		// FIXME
		{
			assert(0);
		}
	}
	sql_finish_coding(&sParse);
	sql_parser_destroy(&sParse);

	return (struct sql_stmt*)sParse.pVdbe;
}

int
sql_parser_ast_execute(struct lua_State *L,
		       struct sql_parsed_ast *ast,
		       struct sql_stmt *stmt)
{
	assert(ast != NULL || stmt != NULL); // FIXME - human readable error

	// generate:
	// a) for the supported case: SELECT
	if (AST_VALID(ast))
		stmt = sql_ast_generate_vdbe(L, ast);
	// b) unsupported cases (yet) - bailed down earlier to `box.execute`
	else
		assert(stmt);
	if (stmt == NULL)
		return 0;

	struct port port;
	struct region *region = &fiber()->gc;

	enum sql_serialization_format format =
		sql_column_count(stmt) > 0 ? DQL_EXECUTE : DML_EXECUTE;

	port_sql_create(&port, stmt, format, true);
	if (sql_execute(stmt, &port, region) != 0)
		goto return_error;

	sql_stmt_reset(stmt);
	port_dump_lua(&port, L, false);
	port_destroy(&port);

	return 1;

return_error:
	if (stmt != NULL)
		sql_stmt_reset(stmt);
	port_destroy(&port);
	return 0;

}
