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
	struct Expr *join_on;
	/** Column names of a join's USING clause. */
	struct ast_id_list *join_using;
	/** Arguments of a table-valued function. */
	struct ExprList *func_args;
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
	struct ExprList *columns;
	/** GROUP BY clause of the SELECT. */
	struct ExprList *group_by;
	/** ORDER BY clause of the SELECT. */
	struct ExprList *order_by;
	/** WHERE clause of the SELECT. */
	struct Expr *where;
	/** HAVING clause of the SELECT. */
	struct Expr *having;
	/** LIMIT clause of the SELECT. */
	struct Expr *limit;
	/** OFFSET clause of the SELECT. */
	struct Expr *offset;
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

/** Destroy the resources owned by the source. */
void
ast_source_destroy(struct ast_source *src);

/** Append a source to the sources list, creating the list if needed. */
struct ast_source_list *
ast_source_list_append(struct Parse *parser, struct ast_source_list *list,
		       struct ast_source *src);

/** Destroy the resources owned by the sources in the list. */
void
ast_source_list_destroy(struct ast_source_list *list);

/** Convert `struct ast_source_list` to `struct SrcList`. */
struct SrcList *
src_list_from_ast(struct Parse *parser, struct ast_source_list *list);

/** Create new empty SELECT structure. */
struct ast_select *
ast_select_new(struct Parse *parser);

/** Clear the SELECT and all linked SELECTs. */
void
ast_select_list_destroy(struct ast_select *select);

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

/** Destroy the resources owned by the WITH clauses in the list. */
void
ast_with_destroy(struct ast_with_list *list);
