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

/** Append an ID to ID list. */
struct ast_id_list *
ast_id_list_append(struct Parse *parser, struct ast_id_list *list,
		   const struct Token *id);

/** Convert `struct ast_id_list` to `struct IdList`. */
struct IdList *
id_list_from_ast(struct ast_id_list *list);
