/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "sqlInt.h"

struct ast_id_list *
ast_id_list_append(struct Parse *parser, struct ast_id_list *list,
		   const struct Token *id)
{
	if (list == NULL) {
		list = xregion_alloc(&parser->region, sizeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	struct ast_id_entry *entry = xregion_alloc(&parser->region,
						   sizeof(*entry));
	entry->id = *id;
	stailq_add_tail(&list->head, &entry->link);
	list->len++;
	return list;
}

struct IdList *
id_list_from_ast(struct ast_id_list *list)
{
	if (list == NULL)
		return NULL;
	struct IdList *res = NULL;
	struct ast_id_entry *entry;
	stailq_foreach_entry(entry, &list->head, link) {
		res = sql_id_list_append(res, &entry->id);
	}
	return res;
}

struct ast_source *
ast_source_new(struct Parse *parser)
{
	struct ast_source *src = xregion_alloc(&parser->region, sizeof(*src));
	memset(src, 0, sizeof(*src));
	return src;
}

void
ast_source_destroy(struct ast_source *src)
{
	sql_expr_list_delete(src->func_args);
	sql_select_delete(src->select);
	sql_expr_delete(src->join_on);
}

struct ast_source_list *
ast_source_list_append(struct Parse *parser, struct ast_source_list *list,
		       struct ast_source *src)
{
	if (list == NULL) {
		list = xregion_alloc(&parser->region, sizeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	stailq_add_tail(&list->head, &src->link);
	list->len++;
	return list;
}

void
ast_source_list_destroy(struct ast_source_list *list)
{
	if (list == NULL)
		return;
	struct ast_source *src;
	stailq_foreach_entry(src, &list->head, link) {
		ast_source_destroy(src);
	}
}

struct SrcList *
src_list_from_ast(struct ast_source_list *list)
{
	if (list == NULL)
		return NULL;
	struct SrcList *res = NULL;
	struct ast_source *src;
	stailq_foreach_entry(src, &list->head, link) {
		struct Token *name = src->name.n > 0 ? &src->name: NULL;
		res = sqlSrcListAppendFromTerm(res, name, &src->alias,
					       src->select, src->join_on,
					       src->join_using,
					       src->disallow_scan);
		if (src->indexed_by.n != 0)
			sqlSrcListIndexedBy(res, &src->indexed_by);
		if (src->is_tab_func)
			sqlSrcListFuncArgs(res, src->func_args);
		res->a[res->nSrc - 1].fg.jointype = src->join_type;
	}
	return res;
}
