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
	return &parse->column_list.a[parse->column_list.n - 1].name;
}

/** Return the name of the table from CREATE TABLE. */
static const char *
new_table_name(struct Parse *parse)
{
	if (parse->type != PARSE_TYPE_CREATE_TABLE)
		return NULL;
	struct Token *name = &parse->create_table.name;
	return sql_normalized_name_region_new(&parse->region, name->z, name->n);
}

void
sql_parse_create_table(struct Parse *parse, struct Token *name,
		       bool if_not_exists)
{
	parse->type = PARSE_TYPE_CREATE_TABLE;
	parse->create_table.name = *name;
	parse->create_table.if_not_exists = if_not_exists;
}

/** Append a new column to column list. */
static void
column_list_append(struct Parse *parse, struct Token *name,
		   enum field_type type)
{
	struct sql_parse_column_list *list = &parse->column_list;
	uint32_t id = list->n;
	++list->n;
	uint32_t size = list->n * sizeof(*list->a);
	list->a = sql_xrealloc(list->a, size);
	struct sql_parse_column *c = &list->a[id];
	memset(c, 0, sizeof(*c));
	c->name = *name;
	c->type = type;
}

void
sql_parse_add_column(struct Parse *parse, struct SrcList *table_name,
		     struct Token *name, enum field_type type)
{
	parse->type = PARSE_TYPE_ADD_COLUMN;
	parse->src_list = table_name;
	column_list_append(parse, name, type);
}

void
sql_parse_table_column(struct Parse *parse, struct Token *name,
		       enum field_type type)
{
	column_list_append(parse, name, type);
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

/** Append a new CHECK to CHECK list. */
static void
check_list_append(struct Parse *parse, const struct Token *name,
		  struct ExprSpan *expr, const struct Token *column_name)
{
	struct sql_parse_check_list *list = &parse->check_list;
	uint32_t id = list->n;
	++list->n;
	uint32_t size = list->n * sizeof(*list->a);
	list->a = sql_xrealloc(list->a, size);
	struct sql_parse_check *c = &list->a[id];
	c->name = *name;
	c->expr = *expr;
	c->column_name = *column_name;
}

void
sql_parse_column_check(struct Parse *parse, const struct Token *name,
		       struct ExprSpan *expr)
{
	check_list_append(parse, name, expr, last_column_name(parse));
}

void
sql_parse_table_check(struct Parse *parse, const struct Token *name,
		      struct ExprSpan *expr)
{
	check_list_append(parse, name, expr, &Token_nil);
}

void
sql_parse_add_check(struct Parse *parse, struct SrcList *table_name,
		    const struct Token *name, struct ExprSpan *expr)
{
	parse->type = PARSE_TYPE_ADD_CHECK;
	parse->src_list = table_name;
	check_list_append(parse, name, expr, &Token_nil);
}

/** Append a new UNIQUE to UNIQUE list. */
static void
unique_list_append(struct Parse *parse, const struct Token *name,
		   struct ExprList *cols)
{
	struct sql_parse_unique_list *list = &parse->unique_list;
	uint32_t id = list->n;
	++list->n;
	uint32_t size = list->n * sizeof(*list->a);
	list->a = sql_xrealloc(list->a, size);
	struct sql_parse_unique *c = &list->a[id];
	c->name = *name;
	c->cols = cols;
}

void
sql_parse_column_unique(struct Parse *parse, const struct Token *name)
{
	struct Token *column_name = last_column_name(parse);
	struct Expr *expr = sql_expr_new_dequoted(TK_ID, column_name);
	struct ExprList *cols = sql_expr_list_append(NULL, expr);
	unique_list_append(parse, name, cols);
}

void
sql_parse_table_unique(struct Parse *parse, const struct Token *name,
		       struct ExprList *cols)
{
	unique_list_append(parse, name, cols);
}

void
sql_parse_add_unique(struct Parse *parse, struct SrcList *table_name,
		     const struct Token *name, struct ExprList *cols)
{
	parse->type = PARSE_TYPE_ADD_UNIQUE;
	parse->src_list = table_name;
	unique_list_append(parse, name, cols);
}

/** Fill in the description of the PRIMARY KEY. */
static void
primary_key_fill(struct Parse *parse, const struct Token *name,
		 struct ExprList *cols)
{
	if (parse->primary_key.cols != NULL) {
		const char *space_name;
		if (parse->src_list == NULL)
			space_name = new_table_name(parse);
		else
			space_name = parse->src_list->a[0].zName;
		diag_set(ClientError, ER_CREATE_SPACE, space_name,
			 "primary key has been already declared");
		parse->is_aborted = true;
		sql_expr_list_delete(cols);
		sql_expr_list_delete(parse->primary_key.cols);
		return;
	}
	parse->primary_key.cols = cols;
	parse->primary_key.name = *name;
}

void
sql_parse_column_primary_key(struct Parse *parse, const struct Token *name,
			     enum sort_order sort_order)
{
	struct Token *column_name = last_column_name(parse);
	struct Expr *expr = sql_expr_new_dequoted(TK_ID, column_name);
	struct ExprList *cols = sql_expr_list_append(NULL, expr);
	sqlExprListSetSortOrder(cols, sort_order);
	primary_key_fill(parse, name, cols);
}

void
sql_parse_table_primary_key(struct Parse *parse, const struct Token *name,
			    struct ExprList *cols)
{
	primary_key_fill(parse, name, cols);
}

void
sql_parse_add_primary_key(struct Parse *parse, struct SrcList *table_name,
			  const struct Token *name, struct ExprList *cols)
{
	parse->type = PARSE_TYPE_ADD_PRIMARY_KEY;
	parse->src_list = table_name;
	primary_key_fill(parse, name, cols);
}

void
sql_parse_create_index(struct Parse *parse, struct Token *table_name,
		       const struct Token *index_name, struct ExprList *cols,
		       bool is_unique, bool if_not_exists)
{
	parse->type = PARSE_TYPE_CREATE_INDEX;
	parse->src_list = sql_src_list_append(NULL, table_name);
	parse->create_index.name = *index_name;
	parse->create_index.cols = cols;
	parse->create_index.is_unique = is_unique;
	parse->create_index.if_not_exists = if_not_exists;
}

/** Set the AUTOINCREMENT column name. */
static void
autoincrement_add(struct Parse *parse, struct Expr *column_name)
{
	if (parse->has_autoinc) {
		diag_set(ClientError, ER_SQL_SYNTAX_WITH_POS, parse->line_count,
			 parse->line_pos,
			 "table must feature at most one AUTOINCREMENT field");
		parse->is_aborted = true;
		return;
	}
	parse->has_autoinc = true;
	parse->autoinc_name = column_name;
}

void
sql_parse_column_autoincrement(struct Parse *parse)
{
	struct Token *column_name = last_column_name(parse);
	autoincrement_add(parse, sql_expr_new_dequoted(TK_ID, column_name));
}

void
sql_parse_table_autoincrement(struct Parse *parse, struct Expr *column_name)
{
	autoincrement_add(parse, column_name);
}

void
sql_parse_column_collate(struct Parse *parse, struct Token *collate_name)
{
	uint32_t id = parse->column_list.n - 1;
	parse->column_list.a[id].collate_name = *collate_name;
}

void
sql_parse_column_nullable_action(struct Parse *parse, int action,
				 int on_conflict)
{
	uint32_t id = parse->column_list.n - 1;
	struct sql_parse_column *c = &parse->column_list.a[id];
	const char *action_str = NULL;
	if (c->is_action_set && c->action != (enum on_conflict_action)action) {
		action_str = on_conflict_action_strs[c->action];
	} else if ((on_conflict != ON_CONFLICT_ACTION_ABORT ||
		    action != ON_CONFLICT_ACTION_NONE) &&
		   action != on_conflict) {
		action_str = on_conflict_action_strs[ON_CONFLICT_ACTION_NONE];
	} else {
		c->action = action;
		c->is_action_set = true;
		return;
	}
	const char *err = "NULL declaration for column '%s' of table '%s' has "
			  "been already set to '%s'";
	const char *space_name;
	if (parse->src_list == NULL)
		space_name = new_table_name(parse);
	else
		space_name = parse->src_list->a[0].zName;
	char *column_name = sql_name_from_token(&c->name);
	err = tt_sprintf(err, column_name, space_name, action_str);
	diag_set(ClientError, ER_SQL_EXECUTE, err);
	parse->is_aborted = true;
	sql_xfree(column_name);
}

void
sql_parse_column_default(struct Parse *parse, struct ExprSpan *expr)
{
	parse->column_list.a[parse->column_list.n - 1].default_expr = *expr;
}

void
sql_parse_table_engine(struct Parse *parse, struct Token *engine_name)
{
	parse->create_table.engine_name = *engine_name;
}

void
sql_parse_create_view(struct Parse *parse, struct Token *name,
		      struct Token *create_start, struct ExprList *aliases,
		      struct Select *select, bool if_not_exists)
{
	parse->type = PARSE_TYPE_CREATE_VIEW;
	parse->create_view.name = *name;
	parse->create_view.aliases = aliases;
	parse->create_view.select = select;
	parse->create_view.if_not_exists = if_not_exists;

	struct Token end = parse->sLastToken;
	assert(end.z[0] != '\0');
	if (end.z[0] != ';')
		end.z += end.n;
	struct Token *begin = create_start;
	int len = end.z - begin->z;
	assert(len > 0);
	while (sqlIsspace(begin->z[len - 1]))
		len--;
	parse->create_view.str.z = begin->z;
	parse->create_view.str.n = len;
}

void
sql_parse_create_trigger(struct Parse *parse, struct SrcList *table_name,
			 struct Token *name, int time, int op,
			 struct IdList *cols, struct Expr *when,
			 struct TriggerStep *step, struct Token *all,
			 bool if_not_exists)
{
	parse->type = PARSE_TYPE_CREATE_TRIGGER;
	parse->src_list = table_name;
	parse->create_trigger.name = *name;
	parse->create_trigger.time = time;
	parse->create_trigger.op = op;
	parse->create_trigger.cols = cols;
	parse->create_trigger.when = when;
	parse->create_trigger.step = step;
	parse->create_trigger.all = *all;
	parse->create_trigger.if_not_exists = if_not_exists;
}

void
sql_parse_table_rename(struct Parse *parse, struct SrcList *table_name,
		       struct Token *new_name)
{
	parse->type = PARSE_TYPE_RENAME_TABLE;
	parse->src_list = table_name;
	parse->table_new_name = *new_name;
}

void
sql_parse_drop_constraint(struct Parse *parse, struct SrcList *table_name,
			  struct Token *name)
{
	parse->type = PARSE_TYPE_DROP_CONSTRAINT;
	parse->src_list = table_name;
	parse->drop_object.name = *name;
}

void
sql_parse_drop_index(struct Parse *parse, struct SrcList *table_name,
		     struct Token *name, bool if_exists)
{
	parse->type = PARSE_TYPE_DROP_INDEX;
	parse->src_list = table_name;
	parse->drop_object.name = *name;
	parse->drop_object.if_exists = if_exists;
}
