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
sql_parse_transaction_start(struct Parse *parse)
{
	parse->type = PARSE_TYPE_START_TRANSACTION;
}

void
sql_parse_transaction_commit(struct Parse *parse)
{
	parse->type = PARSE_TYPE_COMMIT;
}

void
sql_parse_transaction_rollback(struct Parse *parse)
{
	parse->type = PARSE_TYPE_ROLLBACK;
}

void
sql_parse_savepoint_create(struct Parse *parse, const struct Token *name)
{
	parse->type = PARSE_TYPE_SAVEPOINT;
	parse->savepoint.name = *name;
}

void
sql_parse_savepoint_release(struct Parse *parse, const struct Token *name)
{
	parse->type = PARSE_TYPE_RELEASE_SAVEPOINT;
	parse->savepoint.name = *name;
}

void
sql_parse_savepoint_rollback(struct Parse *parse, const struct Token *name)
{
	parse->type = PARSE_TYPE_ROLLBACK_TO_SAVEPOINT;
	parse->savepoint.name = *name;
}

/** Return the name of the last created column. */
static struct Token *
last_column_name(struct Parse *parse)
{
	return &parse->create_column_def.base.name;
}

void
sql_parse_create_table(struct Parse *parse)
{
	parse->type = PARSE_TYPE_CREATE_TABLE;
}

void
sql_parse_add_column(struct Parse *parse)
{
	parse->type = PARSE_TYPE_ADD_COLUMN;
}

/** Append a new FOREIGN KEY to FOREIGN KEY list. */
static void
foreign_key_list_append(struct Parse *parse, const struct Token *name,
			struct ExprList *child_cols,
			const struct Token *parent_name,
			struct ExprList *parent_cols, bool is_column_constraint)
{
	struct sql_parse_foreign_key_list *list = &parse->foreign_key_list;
	uint32_t id = list->n;
	++list->n;
	uint32_t size = list->n * sizeof(*list->a);
	list->a = sql_xrealloc(list->a, size);
	struct sql_parse_foreign_key *c = &list->a[id];
	c->name = *name;
	c->child_cols = child_cols;
	c->parent_cols = parent_cols;
	c->parent_name = *parent_name;
	c->is_column_constraint = is_column_constraint;
}

void
sql_parse_column_foreign_key(struct Parse *parse, const struct Token *name,
			     const struct Token *parent_name,
			     struct ExprList *parent_cols)
{
	struct ExprList *child_cols = sql_expr_list_append(NULL, NULL);
	sqlExprListSetName(parse, child_cols, last_column_name(parse), 1);
	foreign_key_list_append(parse, name, child_cols, parent_name,
				parent_cols, true);
}

void
sql_parse_table_foreign_key(struct Parse *parse, const struct Token *name,
			    struct ExprList *child_cols,
			    const struct Token *parent_name,
			    struct ExprList *parent_cols)
{
	foreign_key_list_append(parse, name, child_cols, parent_name,
				parent_cols, false);
}

void
sql_parse_add_foreign_key(struct Parse *parse, struct SrcList *table_name,
			  const struct Token *name, struct ExprList *child_cols,
			  const struct Token *parent_name,
			  struct ExprList *parent_cols)
{
	parse->type = PARSE_TYPE_ADD_FOREIGN_KEY;
	parse->src_list = table_name;
	foreign_key_list_append(parse, name, child_cols, parent_name,
				parent_cols, false);
}
