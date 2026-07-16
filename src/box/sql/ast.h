/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#include "parse_def.h"
#include "salad/stailq.h"

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
	struct Select *select;
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
src_list_from_ast(struct ast_source_list *list);
