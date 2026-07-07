/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#include "parse_def.h"
#include "salad/stailq.h"
#include "small/rlist.h"

/** List of IDs received from parser. */
struct ast_id_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
};

/** Element of the IDs list. */
struct ast_id_entry {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/** ID from parser. */
	struct Token id;
};

/** List of sources received from parser. */
struct ast_source_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
};

/** Element of the sources list, describing one entry of a FROM clause. */
struct ast_source {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/** Name of the table. Not set for subqueries. */
	struct Token name;
	/** Alias of the source, if any. */
	struct Token alias;
	/** Name of the index specified via INDEXED BY, if any. */
	struct Token indexed_by;
	/** SELECT statement of a subquery. */
	struct ast_select *select;
	/** Expression of a join's ON clause. */
	struct ast_expr *join_on;
	/** Column names of a join's USING clause. */
	struct ast_id_list *join_using;
	/** Arguments of a table-valued function. */
	struct ast_expr_list *func_args;
	/** Type of join between this source and the previous one. */
	int join_type;
	/** True if the source is a table-valued function. */
	bool is_tab_func;
	/** True if scanning is not allowed for this source. */
	bool disallow_scan;
};

/** Structure that describes SELECT. */
struct ast_select {
	/** Link to other SELECTs. */
	struct rlist link;
	/** The FROM clause of the SELECT. */
	struct ast_source_list *sources;
	/** Resulting expressions of the SELECT. */
	struct ast_expr_list *columns;
	/** GROUP BY clause of the SELECT. */
	struct ast_expr_list *group_by;
	/** ORDER BY clause of the SELECT. */
	struct ast_expr_list *order_by;
	/** WHERE clause of the SELECT. */
	struct ast_expr *where;
	/** HAVING clause of the SELECT. */
	struct ast_expr *having;
	/** LIMIT clause of the SELECT. */
	struct ast_expr *limit;
	/** OFFSET clause of the SELECT. */
	struct ast_expr *offset;
	/** WITH clause of the SELECT. */
	struct ast_with_list *with;
	/** Flags of the SELECT. */
	uint32_t flags;
	/** Link type between linked SELECTs. */
	uint8_t op;
};

/** List of WITH clauses received from parser. */
struct ast_with_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
};

/** Element of the WITH clause list, describing one entry of a WITH clause. */
struct ast_with_entry {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/** Name of the table in the WITH clause. */
	struct Token name;
	/** Column names of the table, if specified explicitly. */
	struct ast_id_list *columns;
	/** SELECT statement of the WITH clause. */
	struct ast_select *select;
};

/** Description of list of expressions. */
struct ast_expr_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
	/** True if this is the column list of a SELECT statement. */
	bool is_select_list;
};

/** Description of a parsed expression. */
struct ast_expr {
	/** Left (or the only) operand of the expression. */
	struct ast_expr *left;
	union {
		/** Right operand of a binary expression. */
		struct ast_expr *right;
		/** Sub-expressions, e.g. function args or an IN list. */
		struct ast_expr_list *list;
		/** Subquery of an EXISTS, SELECT, or IN expression. */
		struct ast_select *select;
		/** Target type of a CAST expression. */
		enum field_type type;
		/** Conflict resolution action of a RAISE expression. */
		enum on_conflict_action on_conflict_action;
	};
	/** Pointer to the token text this expression is built from. */
	const char *str;
	/** Length of the token text pointed to by str. */
	uint32_t len;
	/** Parser token code identifying the kind of expression. */
	uint8_t op;
};

/** Element of the expressions list. */
struct ast_expr_list_entry {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/**
	 * Name linked to the expression, if any.
	 * This name can be an alias, a column name in the SET clause of
	 * an UPDATE statement, etc.
	 */
	struct Token name;
	/** The expression itself. */
	struct ast_expr *expr;
	/** Sort order of the entry, used for ORDER BY lists. */
	enum sort_order order;
};

/** Append an ID to ID list. */
struct ast_id_list *
ast_id_list_append(struct Parse *parser, struct ast_id_list *list,
		   const struct Token *id);

/** Convert `struct ast_id_list` to `struct IdList`. */
struct IdList *
id_list_from_ast(struct ast_id_list *list);

/** Allocate a new, zero-initialized source. */
struct ast_source *
ast_source_new(struct Parse *parser);

/** Append a source to the sources list, creating the list if needed. */
struct ast_source_list *
ast_source_list_append(struct Parse *parser, struct ast_source_list *list,
		       struct ast_source *src);

/** Convert `struct ast_source_list` to `struct SrcList`. */
struct SrcList *
src_list_from_ast(struct Parse *parser, struct ast_source_list *list);

/** Create new empty SELECT structure. */
struct ast_select *
ast_select_new(struct Parse *parser);

/** Build `struct Select` object from `struct ast_select` object. */
struct Select *
select_from_ast(struct Parse *parser, struct ast_select *select);

/**
 * Append a WITH clause to the WITH clause list, creating the list if needed.
 */
struct ast_with_list *
ast_with_list_append(struct Parse *parser, struct ast_with_list *list,
		     const struct Token *name, struct ast_id_list *columns,
		     struct ast_select *select);

/** Convert `struct ast_with_list` to `struct With`. */
struct With *
with_from_ast(struct Parse *parser, struct ast_with_list *list);

/** Allocate a new expression node from a token's text. */
struct ast_expr *
ast_expr_new(struct Parse *parser, const char *start, uint32_t len, uint8_t op);

/** Append an expression to the expressions list, creating it if needed. */
struct ast_expr_list *
ast_expr_list_append(struct Parse *parser, struct ast_expr_list *list,
		     struct ast_expr *expr);

/** Set the name of the last expression appended to the list. */
void
ast_expr_list_set_name(struct ast_expr_list *list, struct Token *name);

/** Set the sort order of the last expression appended to the list. */
void
ast_expr_list_set_order(struct ast_expr_list *list, enum sort_order order);

/** Convert `struct ast_expr` to `struct Expr`. */
struct Expr *
expr_from_ast(struct Parse *parser, struct ast_expr *expr);

/** Convert `struct ast_expr_list` to `struct ExprList`. */
struct ExprList *
expr_list_from_ast(struct Parse *parser, struct ast_expr_list *list);
