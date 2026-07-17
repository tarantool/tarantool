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
	ast_select_list_destroy(src->select);
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
src_list_from_ast(struct Parse *parser, struct ast_source_list *list)
{
	if (list == NULL)
		return NULL;
	struct SrcList *res = NULL;
	struct ast_source *src;
	stailq_foreach_entry(src, &list->head, link) {
		struct Select *select = select_from_ast(parser, src->select);
		struct Token *name = src->name.n > 0 ? &src->name: NULL;
		res = sqlSrcListAppendFromTerm(res, name, &src->alias, select,
					       src->join_on, src->join_using,
					       src->disallow_scan);
		if (src->indexed_by.n != 0)
			sqlSrcListIndexedBy(res, &src->indexed_by);
		if (src->is_tab_func)
			sqlSrcListFuncArgs(res, src->func_args);
		res->a[res->nSrc - 1].fg.jointype = src->join_type;
	}
	if (parser->is_aborted) {
		sqlSrcListDelete(res);
		return NULL;
	}
	return res;
}

struct ast_select *
ast_select_new(struct Parse *parser)
{
	struct ast_select *res = xregion_alloc(&parser->region, sizeof(*res));
	memset(res, 0, sizeof(*res));
	rlist_create(&res->link);
	res->op = TK_SELECT;
	return res;
}

/** Clear single `struct Select` object. */
static void
ast_select_destroy(struct ast_select *select)
{
	ast_source_list_destroy(select->sources);
	sql_expr_list_delete(select->columns);
	sql_expr_list_delete(select->group_by);
	sql_expr_list_delete(select->order_by);
	sql_expr_delete(select->where);
	sql_expr_delete(select->having);
	sql_expr_delete(select->limit);
	sql_expr_delete(select->offset);
	ast_with_destroy(select->with);
}

void
ast_select_list_destroy(struct ast_select *select)
{
	if (select == NULL)
		return;
	ast_select_destroy(select);
	struct ast_select *next;
	struct ast_select *tmp;
	rlist_foreach_entry_safe(next, &select->link, link, tmp) {
		ast_select_destroy(select);
	}
}

/** Build single `struct Select` object from `struct ast_select` object. */
static struct Select *
select_from_ast_single(struct Parse *parser, struct ast_select *select)
{
	struct SrcList *list = src_list_from_ast(parser, select->sources);
	struct Select *res = sqlSelectNew(parser, select->columns, list,
					  select->where, select->group_by,
					  select->having, select->order_by,
					  select->flags, select->limit,
					  select->offset);
	res->op = select->op;
	res->pWith = with_from_ast(parser, select->with);
	return res;
}

struct Select *
select_from_ast(struct Parse *parser, struct ast_select *select)
{
	if (select == NULL)
		return NULL;
	struct Select *res = select_from_ast_single(parser, select);
	struct Select *next = res;
	struct ast_select *prev;
	int count = 1;
	rlist_foreach_entry_reverse(prev, &select->link, link) {
		struct Select *prior = select_from_ast_single(parser, prev);
		next->pPrior = prior;
		prior->pNext = next;
		next = prior;
		count++;
	}
	if ((res->selFlags & SF_MultiValue) == 0 &&
	    count > SQL_MAX_COMPOUND_SELECT) {
		diag_set(ClientError, ER_SQL_PARSER_LIMIT, "The number of "
			 "UNION or EXCEPT or INTERSECT operations", count,
			 SQL_MAX_COMPOUND_SELECT);
		parser->is_aborted = true;
		sql_select_delete(res);
		return NULL;
	}
	return res;
}

struct ast_with_list *
ast_with_list_append(struct Parse *parser, struct ast_with_list *list,
		     const struct Token *name, struct ast_id_list *columns,
		     struct ast_select *select)
{
	if (list == NULL) {
		list = xregion_alloc(&parser->region, sizeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	struct ast_with_entry *entry = xregion_alloc(&parser->region,
						     sizeof(*entry));
	entry->name = *name;
	entry->columns = columns;
	entry->select = select;
	stailq_add_tail(&list->head, &entry->link);
	list->len++;
	return list;
}

/** Convert `struct ast_id_list` to `struct ExprList` of column names. */
static struct ExprList *
expr_list_from_ids(struct Parse *parser, struct ast_id_list *list)
{
	if (list == NULL)
		return NULL;
	struct ExprList *res = NULL;
	struct ast_id_entry *entry;
	stailq_foreach_entry(entry, &list->head, link) {
		res = sql_expr_list_append(res, NULL);
		sqlExprListSetName(parser, res, &entry->id, 1);
	}
	return res;
}

struct With *
with_from_ast(struct Parse *parser, struct ast_with_list *list)
{
	if (list == NULL)
		return NULL;
	struct With *res = NULL;
	struct ast_with_entry *entry;
	stailq_foreach_entry(entry, &list->head, link) {
		struct ExprList *cols =
			expr_list_from_ids(parser, entry->columns);
		struct Select *select = select_from_ast(parser, entry->select);
		res = sqlWithAdd(parser, res, &entry->name, cols, select);
	}
	if (parser->is_aborted) {
		sqlWithDelete(res);
		return NULL;
	}
	return res;
}

void
ast_with_destroy(struct ast_with_list *list)
{
	if (list == NULL)
		return;
	struct ast_with_entry *entry;
	stailq_foreach_entry(entry, &list->head, link) {
		ast_select_list_destroy(entry->select);
	}
}
