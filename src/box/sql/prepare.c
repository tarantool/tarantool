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
#include "box/schema.h"

struct Vdbe *
sql_stmt_compile(const char *zSql, struct Vdbe *pReprepare)
{
	Parse sParse;		/* Parsing context */
	sql_parser_create(&sParse, current_session()->sql_flags);
	sParse.pReprepare = pReprepare;

	sql_parse_statement(&sParse, zSql);
	if (sParse.is_aborted) {
		sql_parser_destroy(&sParse);
		return NULL;
	}
	assert(sParse.nQueryLoop == 0);

	if (sParse.explain != 0) {
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

	struct Vdbe *res = sParse.pVdbe;
	sParse.pVdbe = NULL;
	sqlVdbeSetSql(res, zSql);
	sql_parser_destroy(&sParse);
	return res;
}

struct Expr *
sql_expr_compile(const char *sql)
{
	if (sql == NULL || sql[0] == '\0') {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 "Function definition cannot be empty");
		return NULL;
	}

	struct Parse parser;
	sql_parser_create(&parser, SQL_DEFAULT_FLAGS);
	struct Expr *res = sql_parse_function(&parser, sql);
	sql_parser_destroy(&parser);
	return res;
}

struct Select *
sql_view_compile(const char *sql)
{
	assert(sql != NULL && sql[0] != '\0');

	struct Parse parser;
	sql_parser_create(&parser, SQL_DEFAULT_FLAGS);
	struct Select *res = sql_parse_view(&parser, sql);
	sql_parser_destroy(&parser);
	return res;
}

struct sql_trigger *
sql_trigger_compile(const char *sql)
{
	if (sql == NULL || sql[0] == '\0') {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 "Trigger definition cannot be empty");
		return NULL;
	}

	struct Parse parser;
	sql_parser_create(&parser, SQL_DEFAULT_FLAGS);
	struct sql_trigger *res = sql_parse_trigger(&parser, sql);
	sql_parser_destroy(&parser);
	return res;
}

/*
 * Rerun the compilation of a statement after a schema change.
 */
int
sqlReprepare(Vdbe * p)
{
	const char *zSql;

	zSql = sql_sql(p);
	assert(zSql != 0);
	struct Vdbe *pNew = sql_stmt_compile(zSql, p);
	if (pNew == NULL)
		return -1;
	sqlVdbeSwap(pNew, p);
	sqlTransferBindings(pNew, p);
	sqlVdbeResetStepResult(pNew);
	sqlVdbeFinalize(pNew);
	return 0;
}

void
sql_parser_create(struct Parse *parser, uint32_t sql_flags)
{
	memset(parser, 0, sizeof(struct Parse));
	parser->sql_flags = sql_flags;
	parser->line_count = 1;
	parser->line_pos = 1;
	region_create(&parser->region, &cord()->slabc);
}

/**
 * This function is called to release parsing artifacts
 * during table creation or column addition. The only objects
 * allocated using malloc are index defs.
 * Note that this functions can't be called on ordinary
 * space object. It's purpose is to clean-up parser->new_space.
 *
 * @param parser Parser context.
 */
static void
parser_space_delete(struct Parse *parser)
{
	struct space *space = parser->space;
	if (space == NULL)
		return;
	assert(space->def->opts.is_ephemeral);
	uint32_t i = 0;
	/* If new_space is NULL, the query is ALTER TABLE ADD COLUMNS. */
	if (parser->new_space == NULL) {
		/*
		 * Don't delete already existing defs and start from new
		 * ones.
		 */
		struct space *altered_space = space_by_name0(space->def->name);
		if (altered_space != NULL)
			i = altered_space->index_count;
	}
	for (; i < space->index_count; ++i)
		index_def_delete(space->index[i]->def);
}

void
sql_parser_destroy(Parse *parser)
{
	assert(parser != NULL);
	sqlVdbeDelete(parser->pVdbe);
	parser_space_delete(parser);
	while (parser->pTriggerPrg != NULL) {
		TriggerPrg *pT = parser->pTriggerPrg;
		parser->pTriggerPrg = pT->pNext;
		sql_xfree(pT);
	}
	if (parser->pWithToFree)
		sqlWithDelete(parser->pWithToFree);
	sql_xfree(parser->pVList);
	sql_xfree(parser->default_funcs);
	sql_xfree(parser->aLabel);
	sql_expr_list_delete(parser->pConstExpr);
	struct create_fk_constraint_parse_def *create_fk_constraint_parse_def =
		&parser->create_fk_constraint_parse_def;
	create_fk_constraint_parse_def_destroy(create_fk_constraint_parse_def);
	assert(sql_get()->lookaside.bDisable >= parser->disableLookaside);
	sql_get()->lookaside.bDisable -= parser->disableLookaside;
	parser->disableLookaside = 0;
	region_destroy(&parser->region);
}
