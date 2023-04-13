/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include <string.h>

#include "sqlInt.h"

const struct Token sqlIntTokens[] = {
	{"0", 1, false},
	{"1", 1, false},
	{"2", 1, false},
	{"3", 1, false},
};

void
sqlTokenInit(struct Token *p, char *z)
{
	p->z = z;
	p->n = z == NULL ? 0 : strlen(z);
}

void
sql_ast_init_start_transaction(struct Parse *parse)
{
	assert(parse->ast.type == SQL_AST_TYPE_UNKNOWN);
	parse->ast.type = SQL_AST_TYPE_START_TRANSACTION;
}

void
sql_ast_init_commit(struct Parse *parse)
{
	assert(parse->ast.type == SQL_AST_TYPE_UNKNOWN);
	parse->ast.type = SQL_AST_TYPE_COMMIT;
}

void
sql_ast_init_rollback(struct Parse *parse)
{
	assert(parse->ast.type == SQL_AST_TYPE_UNKNOWN);
	parse->ast.type = SQL_AST_TYPE_ROLLBACK;
}

void
sql_ast_init_savepoint(struct Parse *parse, const struct Token *name)
{
	assert(parse->ast.type == SQL_AST_TYPE_UNKNOWN);
	parse->ast.type = SQL_AST_TYPE_SAVEPOINT;
	parse->ast.savepoint.name = *name;
}

void
sql_ast_init_release_savepoint(struct Parse *parse, const struct Token *name)
{
	assert(parse->ast.type == SQL_AST_TYPE_UNKNOWN);
	parse->ast.type = SQL_AST_TYPE_RELEASE_SAVEPOINT;
	parse->ast.savepoint.name = *name;
}

void
sql_ast_init_rollback_to_savepoint(struct Parse *parse,
				   const struct Token *name)
{
	assert(parse->ast.type == SQL_AST_TYPE_UNKNOWN);
	parse->ast.type = SQL_AST_TYPE_ROLLBACK_TO_SAVEPOINT;
	parse->ast.savepoint.name = *name;
}

void
sql_ast_init_table_rename(struct Parse *parse, const struct Token *old_name,
			  const struct Token *new_name)
{
	assert(parse->ast.type == SQL_AST_TYPE_UNKNOWN);
	parse->ast.type = SQL_AST_TYPE_TABLE_RENAME;
	parse->ast.rename.old_name = *old_name;
	parse->ast.rename.new_name = *new_name;
}

void
sql_ast_init_constraint_drop(struct Parse *parse,
			     const struct Token *table_name,
			     const struct Token *name)
{
	assert(parse->ast.type == SQL_AST_TYPE_UNKNOWN);
	parse->ast.type = SQL_AST_TYPE_DROP_CONSTRAINT;
	parse->ast.drop_constraint.table_name = *table_name;
	parse->ast.drop_constraint.name = *name;
}
