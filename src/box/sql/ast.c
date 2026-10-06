/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "sqlInt.h"

struct ast_id_list *
ast_id_list_append(struct region *region, struct ast_id_list *list,
		   const struct Token *id)
{
	if (list == NULL) {
		list = xregion_alloc_object(region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	struct ast_id_entry *entry =
		xregion_alloc_object(region, typeof(*entry));
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
ast_source_new(struct region *region)
{
	struct ast_source *src = xregion_alloc_object(region, typeof(*src));
	memset(src, 0, sizeof(*src));
	return src;
}

struct ast_source_list *
ast_source_list_append(struct region *region, struct ast_source_list *list,
		       struct ast_source *src)
{
	if (list == NULL) {
		list = xregion_alloc_object(region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	stailq_add_tail(&list->head, &src->link);
	list->len++;
	return list;
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
		struct Expr *join_on = expr_from_ast(parser, src->join_on);
		struct Token *name = src->name.n > 0 ? &src->name: NULL;
		res = sqlSrcListAppendFromTerm(res, name, &src->alias, select,
					       join_on, src->join_using,
					       src->disallow_scan);
		if (src->indexed_by.n != 0)
			sqlSrcListIndexedBy(res, &src->indexed_by);
		res->a[res->nSrc - 1].fg.jointype = src->join_type;
		if ((src->join_type & JT_INNER) != 0 &&
		    (src->join_type & JT_OUTER) != 0) {
			diag_set(ClientError, ER_SQL_PARSER_GENERIC,
				 "JOIN cannot be both OUTER and INNER");
			parser->is_aborted = true;
			break;
		}
		if ((src->join_type & JT_OUTER) != 0 &&
		    (src->join_type & (JT_LEFT | JT_RIGHT)) != JT_LEFT) {
			diag_set(ClientError, ER_UNSUPPORTED, "Tarantool",
				 "RIGHT and FULL OUTER JOINs");
			parser->is_aborted = true;
			break;
		}
	}
	if (parser->is_aborted) {
		sqlSrcListDelete(res);
		return NULL;
	}
	return res;
}

struct ast_select *
ast_select_new(struct region *region)
{
	struct ast_select *res = xregion_alloc_object(region, typeof(*res));
	memset(res, 0, sizeof(*res));
	rlist_create(&res->link);
	res->op = TK_SELECT;
	return res;
}

/**
 * Build single `struct Select` object from `struct ast_select` object.
 *
 * Return NULL on error.
 */
static struct Select *
select_from_ast_single(struct Parse *parser, struct ast_select *select)
{
	if (select->op != TK_SELECT && select->op != TK_ALL)
		parser->hasCompound = 1;
	struct SrcList *list = src_list_from_ast(parser, select->sources);
	struct Expr *where = expr_from_ast(parser, select->where);
	struct Expr *having = expr_from_ast(parser, select->having);
	struct Expr *limit = expr_from_ast(parser, select->limit);
	struct Expr *offset = expr_from_ast(parser, select->offset);
	struct ExprList *columns = expr_list_from_ast(parser, select->columns);
	struct ExprList *group_by = expr_list_from_ast(parser,
						       select->group_by);
	struct ExprList *order_by = expr_list_from_ast(parser,
						       select->order_by);
	struct Select *res = sqlSelectNew(parser, columns, list, where,
					  group_by, having, order_by,
					  select->flags, limit, offset);
	res->op = select->op;
	res->pWith = with_from_ast(parser, select->with);
	if (parser->is_aborted) {
		sql_select_delete(res);
		return NULL;
	}
	return res;
}

struct Select *
select_from_ast(struct Parse *parser, struct ast_select *select)
{
	if (select == NULL)
		return NULL;
	/*
	 * Convert the compound parts from left to right, so that anonymous bind
	 * variables ("?") are numbered in the order they appear in the query.
	 */
	struct Select *prior = NULL;
	struct ast_select *part;
	int count = 1;
	rlist_foreach_entry(part, &select->link, link) {
		struct Select *cur = select_from_ast_single(parser, part);
		if (parser->is_aborted) {
			sql_select_delete(prior);
			return NULL;
		}
		if (prior != NULL) {
			cur->pPrior = prior;
			prior->pNext = cur;
		}
		prior = cur;
		count++;
	}
	struct Select *res = select_from_ast_single(parser, select);
	if (parser->is_aborted) {
		sql_select_delete(prior);
		return NULL;
	}
	if (prior != NULL) {
		res->pPrior = prior;
		prior->pNext = res;
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
ast_with_list_append(struct region *region, struct ast_with_list *list,
		     const struct Token *name, struct ast_id_list *columns,
		     struct ast_select *select)
{
	if (list == NULL) {
		list = xregion_alloc_object(region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	struct ast_with_entry *entry =
		xregion_alloc_object(region, typeof(*entry));
	entry->name = *name;
	entry->columns = columns;
	entry->select = select;
	stailq_add_tail(&list->head, &entry->link);
	list->len++;
	return list;
}

struct ExprList *
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
	if (parser->is_aborted) {
		sql_expr_list_delete(res);
		return NULL;
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

/** Allocate a new expression node with all operands set to zero. */
static struct ast_expr *
ast_expr_new(struct region *region, uint8_t op)
{
	struct ast_expr *expr = xregion_alloc_object(region, typeof(*expr));
	memset(expr, 0, sizeof(*expr));
	expr->op = op;
	return expr;
}

struct ast_expr *
ast_expr_new_leaf(struct region *region, uint8_t op, const char *str,
		  uint32_t len)
{
	struct ast_expr *expr = ast_expr_new(region, op);
	expr->val.str = str;
	expr->val.len = len;
	return expr;
}

struct ast_expr *
ast_expr_new_var(struct region *region, uint8_t op, const char *str,
		 uint32_t len)
{
	struct ast_expr *expr = ast_expr_new(region, op);
	expr->val.str = str;
	expr->val.len = len;
	return expr;
}

struct ast_expr *
ast_expr_new_asterisk(struct region *region)
{
	return ast_expr_new(region, TK_ASTERISK);
}

struct ast_expr *
ast_expr_new_unary(struct region *region, uint8_t op, struct ast_expr *operand)
{
	struct ast_expr *expr = ast_expr_new(region, op);
	expr->arg = operand;
	return expr;
}

struct ast_expr *
ast_expr_new_binary(struct region *region, uint8_t op, struct ast_expr *left,
		    struct ast_expr *right)
{
	struct ast_expr *expr = ast_expr_new(region, op);
	expr->bin.left = left;
	expr->bin.right = right;
	return expr;
}

struct ast_expr *
ast_expr_new_list(struct region *region, uint8_t op,
		  struct ast_expr_list *list)
{
	struct ast_expr *expr = ast_expr_new(region, op);
	expr->list = list;
	return expr;
}

struct ast_expr *
ast_expr_new_select(struct region *region, uint8_t op,
		    struct ast_select *select)
{
	struct ast_expr *expr = ast_expr_new(region, op);
	expr->select = select;
	return expr;
}

struct ast_expr *
ast_expr_new_cast(struct region *region, struct ast_expr *operand,
		  enum field_type type)
{
	struct ast_expr *expr = ast_expr_new(region, TK_CAST);
	expr->cast.expr = operand;
	expr->cast.type = type;
	return expr;
}

struct ast_expr *
ast_expr_new_collate(struct region *region, struct ast_expr *operand,
		     const struct Token *name)
{
	struct ast_expr *expr = ast_expr_new(region, TK_COLLATE);
	expr->coll.expr = operand;
	expr->coll.name = name->z;
	expr->coll.name_len = name->n;
	return expr;
}

struct ast_expr *
ast_expr_new_function(struct region *region, const struct Token *name,
		      bool is_distinct, struct ast_expr_list *args)
{
	struct ast_expr *expr = ast_expr_new(region, TK_FUNCTION);
	expr->func.name = name->z;
	expr->func.name_len = name->n;
	expr->func.is_distinct = is_distinct;
	expr->func.args = args;
	return expr;
}

struct ast_expr *
ast_expr_new_in(struct region *region, struct ast_expr *value,
		struct ast_expr_list *list, struct ast_select *select)
{
	assert(list == NULL || select == NULL);
	struct ast_expr *expr = ast_expr_new(region, TK_IN);
	expr->in.value = value;
	expr->in.list = list;
	expr->in.select = select;
	return expr;
}

struct ast_expr *
ast_expr_new_between(struct region *region, struct ast_expr *value,
		     struct ast_expr *lower, struct ast_expr *upper)
{
	struct ast_expr *expr = ast_expr_new(region, TK_BETWEEN);
	expr->between.value = value;
	expr->between.lower = lower;
	expr->between.upper = upper;
	return expr;
}

struct ast_expr *
ast_expr_new_case(struct region *region, struct ast_expr *value,
		  struct ast_expr_list *list)
{
	struct ast_expr *expr = ast_expr_new(region, TK_CASE);
	expr->cs.value = value;
	expr->cs.list = list;
	return expr;
}

struct ast_expr *
ast_expr_new_getitem(struct region *region, struct ast_expr *value,
		     struct ast_expr_list *keys)
{
	struct ast_expr *expr = ast_expr_new(region, TK_GETITEM);
	expr->getitem.value = value;
	expr->getitem.keys = keys;
	return expr;
}

struct ast_expr *
ast_expr_new_raise(struct region *region, const struct Token *message,
		   enum on_conflict_action action)
{
	struct ast_expr *expr = ast_expr_new(region, TK_RAISE);
	if (message != NULL) {
		expr->raise.str = message->z;
		expr->raise.len = message->n;
	}
	expr->raise.action = action;
	return expr;
}

struct ast_expr_list *
ast_expr_list_append(struct region *region, struct ast_expr_list *list,
		     struct ast_expr *expr)
{
	struct ast_expr_list_entry *entry =
		xregion_alloc_object(region, typeof(*entry));
	entry->name = Token_nil;
	entry->expr = expr;
	entry->span = NULL;
	entry->span_len = 0;
	entry->order = SORT_ORDER_ASC;
	entry->autoinc = false;
	if (list == NULL) {
		list = xregion_alloc_object(region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	stailq_add_tail(&list->head, &entry->link);
	list->len++;
	return list;
}

void
ast_expr_list_set_name(struct ast_expr_list *list, struct Token *name)
{
	struct ast_expr_list_entry *entry =
		stailq_last_entry(&list->head, typeof(*entry), link);
	entry->name = *name;
}

void
ast_expr_list_set_span(struct ast_expr_list *list, const char *start,
		       const char *end)
{
	struct ast_expr_list_entry *entry =
		stailq_last_entry(&list->head, typeof(*entry), link);
	entry->span = start;
	entry->span_len = end - start;
}

void
ast_expr_list_set_order(struct ast_expr_list *list, enum sort_order order)
{
	struct ast_expr_list_entry *entry =
		stailq_last_entry(&list->head, typeof(*entry), link);
	entry->order = order;
}

void
ast_expr_list_set_autoinc(struct ast_expr_list *list, bool autoinc)
{
	struct ast_expr_list_entry *entry =
		stailq_last_entry(&list->head, typeof(*entry), link);
	entry->autoinc = autoinc;
}

struct ExprList *
expr_list_from_ast(struct Parse *parser, struct ast_expr_list *list)
{
	if (list == NULL)
		return NULL;
	struct ExprList *res = NULL;
	struct ast_expr_list_entry *entry;
	stailq_foreach_entry(entry, &list->head, link) {
		struct ast_expr *ast_expr = entry->expr;
		struct Expr *expr = expr_from_ast(parser, ast_expr);
		if (expr == NULL)
			break;
		res = sql_expr_list_append(res, expr);
		if (entry->name.n > 0)
			sqlExprListSetName(parser, res, &entry->name, 1);
		if (entry->span != NULL)
			sqlExprListSetSpan(res, entry->span, entry->span_len);
		if (entry->order != SORT_ORDER_ASC)
			sqlExprListSetSortOrder(res, entry->order);
		if (entry->autoinc) {
			if (parser->autoinc_fieldno != NULL) {
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 "Table must feature at most one "
					 "AUTOINCREMENT field");
				parser->is_aborted = true;
				break;
			}
			if (expr != sqlExprSkipCollate(expr)) {
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 "AUTOINCREMENT cannot be used with "
					 "a non-integer column");
				parser->is_aborted = true;
				break;
			}
			parser->autoinc_fieldno = &expr->iColumn;
		}
	}
	if (parser->is_aborted) {
		sql_expr_list_delete(res);
		return NULL;
	}
	return res;
}

/** Build a `struct Expr` from the dequoted text of a token. */
static struct Expr *
expr_token(uint8_t op, const char *str, uint32_t len)
{
	struct Token t;
	t.z = str;
	t.n = len;
	t.isReserved = false;
	return sql_expr_new_dequoted(op, &t);
}

/** Build an identifier `struct Expr`, e.g. a function or collation name. */
static struct Expr *
expr_id(uint8_t op, const char *str, uint32_t len)
{
	struct Expr *res = expr_token(op, str, len);
	res->type = FIELD_TYPE_SCALAR;
	return res;
}

/** Build a leaf `struct Expr` (a literal) of the given field type. */
static struct Expr *
expr_leaf(struct ast_expr *expr, enum field_type type)
{
	struct Expr *res = expr_token(expr->op, expr->val.str, expr->val.len);
	res->type = type;
	res->flags |= EP_Leaf;
	return res;
}

/**
 * Build a `struct Expr` for a bound variable.
 *
 * Return NULL on error.
 */
static struct Expr *
expr_var(struct Parse *parser, struct ast_expr *expr)
{
	struct Token t;
	t.z = expr->val.str;
	t.n = expr->val.len;
	t.isReserved = false;
	/*
	 * The check exists only for the `:` case,
	 * because the other variants (`@`, `#`, `?`, `$`)
	 * are checked during tokenization.
	 */
	if (expr->val.str[0] == ':' && (IdChar(expr->val.str[1]) == 0)) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 tt_sprintf("Wrong bind variable name '%.*s'",
				    expr->val.len, expr->val.str));
		parser->is_aborted = true;
		return NULL;
	}
	struct Expr *res = sql_expr_new_dequoted(expr->op, &t);
	res->type = FIELD_TYPE_BOOLEAN;
	res->flags |= EP_Leaf;
	sqlExprAssignVarNumber(parser, res, expr->val.len);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

/**
 * Build a `struct Expr` for a unary operator applied to the operand.
 *
 * Return NULL on error.
 */
static struct Expr *
expr_unary(struct Parse *parser, uint8_t op, struct ast_expr *operand)
{
	struct Expr *left = expr_from_ast(parser, operand);
	if (parser->is_aborted)
		return NULL;
	struct Expr *res = sqlPExpr(parser, op, left, NULL);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

/**
 * Build a `struct Expr` for a binary operator applied to left and right.
 *
 * Return NULL on error.
 */
static struct Expr *
expr_binary(struct Parse *parser, struct ast_expr *expr)
{
	struct Expr *left = expr_from_ast(parser, expr->bin.left);
	if (parser->is_aborted)
		return NULL;
	struct Expr *right = expr_from_ast(parser, expr->bin.right);
	if (parser->is_aborted) {
		sql_expr_delete(left);
		return NULL;
	}
	struct Expr *res = sqlPExpr(parser, expr->op, left, right);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

/**
 * Build a `struct Expr` of the given type whose operand is the list.
 *
 * Return NULL on error.
 */
static struct Expr *
expr_list(struct Parse *parser, uint8_t op, struct ast_expr_list *list,
	  enum field_type type)
{
	struct Expr *res = sql_expr_new_anon(op);
	res->x.pList = expr_list_from_ast(parser, list);
	res->type = type;
	sqlExprSetHeightAndFlags(parser, res);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

/**
 * Build a `struct Expr` with a left operand and an expression list operand.
 *
 * Return NULL on error.
 */
static struct Expr *
expr_left_and_list(struct Parse *parser, uint8_t op, struct ast_expr *left_ast,
		   struct ast_expr_list *list)
{
	struct Expr *left = expr_from_ast(parser, left_ast);
	if (parser->is_aborted)
		return NULL;
	struct Expr *res = sqlPExpr(parser, op, left, NULL);
	res->x.pList = expr_list_from_ast(parser, list);
	sqlExprSetHeightAndFlags(parser, res);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

/**
 * Build a `struct Expr` for a BETWEEN expression.
 *
 * Return NULL on error.
 */
static struct Expr *
expr_between(struct Parse *parser, struct ast_expr *expr)
{
	struct Expr *value = expr_from_ast(parser, expr->between.value);
	if (parser->is_aborted)
		return NULL;
	struct Expr *res = sqlPExpr(parser, expr->op, value, NULL);
	struct Expr *lower = expr_from_ast(parser, expr->between.lower);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	res->x.pList = sql_expr_list_append(NULL, lower);
	struct Expr *upper = expr_from_ast(parser, expr->between.upper);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	res->x.pList = sql_expr_list_append(res->x.pList, upper);
	sqlExprSetHeightAndFlags(parser, res);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

/**
 * Build a `struct Expr` for a function call expression.
 *
 * Return NULL on error.
 */
static struct Expr *
expr_function(struct Parse *parser, struct ast_expr *expr)
{
	struct Expr *res = expr_id(TK_FUNCTION, expr->func.name,
				   expr->func.name_len);
	if (expr->func.is_distinct)
		res->flags |= EP_Distinct;
	if (expr->func.args == NULL)
		return res;
	if (expr->func.args->len > SQL_MAX_FUNCTION_ARG) {
		const char *err = tt_sprintf("Number of arguments to "
					     "function %s", res->u.zToken);
		diag_set(ClientError, ER_SQL_PARSER_LIMIT, err,
			 expr->func.args->len, SQL_MAX_FUNCTION_ARG);
		parser->is_aborted = true;
		sql_expr_delete(res);
		return NULL;
	}
	res->x.pList = expr_list_from_ast(parser, expr->func.args);
	sqlExprSetHeightAndFlags(parser, res);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

/**
 * Build a `struct Expr` for an IN expression (subquery or value list).
 *
 * Return NULL on error.
 */
static struct Expr *
expr_in(struct Parse *parser, struct ast_expr *expr)
{
	if (expr->in.select != NULL) {
		struct Expr *left = expr_from_ast(parser, expr->in.value);
		if (parser->is_aborted)
			return NULL;
		struct Select *select =
			select_from_ast(parser, expr->in.select);
		if (parser->is_aborted) {
			sql_expr_delete(left);
			return NULL;
		}
		struct Expr *res = sqlPExpr(parser, expr->op, left, NULL);
		sqlPExprAddSelect(parser, res, select);
		if (parser->is_aborted) {
			sql_expr_delete(res);
			return NULL;
		}
		return res;
	}
	if (expr->in.list == NULL || expr->in.list->len == 0) {
		struct Expr *res = sql_expr_new_anon(TK_FALSE);
		res->type = FIELD_TYPE_BOOLEAN;
		return res;
	}
	if (expr->in.list->len == 1) {
		struct Expr *left = expr_from_ast(parser, expr->in.value);
		if (parser->is_aborted)
			return NULL;
		struct ast_expr_list_entry *entry =
			stailq_first_entry(&expr->in.list->head,
					   typeof(*entry), link);
		struct Expr *right = expr_from_ast(parser, entry->expr);
		if (parser->is_aborted) {
			sql_expr_delete(left);
			return NULL;
		}
		struct Expr *res = sqlPExpr(parser, TK_EQ, left, right);
		if (parser->is_aborted) {
			sql_expr_delete(res);
			return NULL;
		}
		return res;
	}
	struct Expr *left = expr_from_ast(parser, expr->in.value);
	if (parser->is_aborted)
		return NULL;
	struct Expr *res = sqlPExpr(parser, expr->op, left, NULL);
	res->x.pList = expr_list_from_ast(parser, expr->in.list);
	sqlExprSetHeightAndFlags(parser, res);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

/**
 * Build a `struct Expr` for a subscripting operator expression.
 *
 * Return NULL on error.
 */
static struct Expr *
expr_getitem(struct Parse *parser, struct ast_expr *expr)
{
	struct ExprList *list = expr_list_from_ast(parser, expr->getitem.keys);
	if (parser->is_aborted)
		return NULL;
	struct Expr *left = expr_from_ast(parser, expr->getitem.value);
	if (parser->is_aborted) {
		sql_expr_list_delete(list);
		return NULL;
	}
	struct Expr *res = sql_expr_new_anon(expr->op);
	res->x.pList = sql_expr_list_append(list, left);
	res->type = FIELD_TYPE_ANY;
	sqlExprSetHeightAndFlags(parser, res);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

struct Expr *
expr_from_ast(struct Parse *parser, struct ast_expr *expr)
{
	if (expr == NULL)
		return NULL;
	struct Expr *res = NULL;
	switch (expr->op) {
	case TK_STRING:
		res = expr_leaf(expr, FIELD_TYPE_STRING);
		break;
	case TK_BLOB:
		res = expr_leaf(expr, FIELD_TYPE_VARBINARY);
		break;
	case TK_INTEGER:
		res = expr_leaf(expr, FIELD_TYPE_INTEGER);
		break;
	case TK_FLOAT:
		res = expr_leaf(expr, FIELD_TYPE_DOUBLE);
		break;
	case TK_DECIMAL:
		res = expr_leaf(expr, FIELD_TYPE_DECIMAL);
		break;
	case TK_TRUE:
	case TK_FALSE:
	case TK_UNKNOWN:
		res = expr_leaf(expr, FIELD_TYPE_BOOLEAN);
		break;
	case TK_VAR_ANON:
	case TK_VAR_NUM:
	case TK_VAR_NAME:
		res = expr_var(parser, expr);
		break;
	case TK_AND:
	case TK_OR:
	case TK_LT:
	case TK_LE:
	case TK_GT:
	case TK_GE:
	case TK_EQ:
	case TK_NE:
	case TK_BITAND:
	case TK_BITOR:
	case TK_LSHIFT:
	case TK_RSHIFT:
	case TK_PLUS:
	case TK_MINUS:
	case TK_STAR:
	case TK_SLASH:
	case TK_REM:
	case TK_CONCAT:
	case TK_DOT:
		res = expr_binary(parser, expr);
		break;
	case TK_COLLATE:
		res = expr_id(TK_COLLATE, expr->coll.name, expr->coll.name_len);
		res->flags |= EP_Collate | EP_Skip;
		res->pLeft = expr_from_ast(parser, expr->coll.expr);
		break;
	case TK_CAST:
		res = expr_unary(parser, expr->op, expr->cast.expr);
		if (res == NULL)
			break;
		res->type = expr->cast.type;
		break;
	case TK_NOT:
	case TK_BITNOT:
	case TK_UMINUS:
	case TK_UPLUS:
	case TK_NOTNULL:
	case TK_ISNULL:
		res = expr_unary(parser, expr->op, expr->arg);
		break;
	case TK_ARRAY:
		res = expr_list(parser, expr->op, expr->list, FIELD_TYPE_ARRAY);
		break;
	case TK_MAP:
		res = expr_list(parser, expr->op, expr->list, FIELD_TYPE_MAP);
		break;
	case TK_GETITEM:
		res = expr_getitem(parser, expr);
		break;
	case TK_FUNCTION:
		res = expr_function(parser, expr);
		break;
	case TK_BETWEEN:
		res = expr_between(parser, expr);
		break;
	case TK_VECTOR:
		res = expr_list(parser, expr->op, expr->list, FIELD_TYPE_ANY);
		break;
	case TK_IN:
		res = expr_in(parser, expr);
		break;
	case TK_ASTERISK:
		res = sql_expr_new_anon(TK_ASTERISK);
		break;
	case TK_EXISTS:
	case TK_SELECT: {
		struct Select *select = select_from_ast(parser, expr->select);
		if (parser->is_aborted)
			return NULL;
		res = sql_expr_new_anon(expr->op);
		sqlPExprAddSelect(parser, res, select);
		break;
	}
	case TK_RAISE:
		if (expr->raise.str != NULL) {
			res = expr_token(TK_RAISE, expr->raise.str,
					 expr->raise.len);
			res->type = FIELD_TYPE_STRING;
			res->flags |= EP_Leaf;
		} else {
			res = sql_expr_new_anon(TK_RAISE);
		}
		res->on_conflict_action = expr->raise.action;
		break;
	case TK_CASE:
		if (expr->cs.value == NULL) {
			res = expr_list(parser, expr->op, expr->cs.list,
					FIELD_TYPE_ANY);
		} else {
			res = expr_left_and_list(parser, expr->op,
						 expr->cs.value, expr->cs.list);
		}
		break;
	default:
		res = expr_leaf(expr, FIELD_TYPE_SCALAR);
		break;
	}
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

struct ast_insert *
ast_insert_new(struct region *region)
{
	struct ast_insert *res = xregion_alloc_object(region, typeof(*res));
	memset(res, 0, sizeof(*res));
	return res;
}

struct ast_set_list *
ast_set_list_append_expr(struct region *region, struct ast_set_list *list,
			 struct Token *name, struct ast_expr *expr)
{
	if (list == NULL) {
		list = xregion_alloc_object(region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	struct ast_set_list_entry *entry =
		xregion_alloc_object(region, typeof(*entry));
	memset(entry, 0, sizeof(*entry));
	entry->name = *name;
	entry->expr = expr;
	stailq_add_tail(&list->head, &entry->link);
	list->len++;
	return list;
}

struct ast_set_list *
ast_set_list_append_vector(struct region *region, struct ast_set_list *list,
			   struct ast_id_list *ids, struct ast_expr *expr)
{
	if (list == NULL) {
		list = xregion_alloc_object(region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	struct ast_set_list_entry *entry =
		xregion_alloc_object(region, typeof(*entry));
	memset(entry, 0, sizeof(*entry));
	entry->ids = ids;
	entry->expr = expr;
	stailq_add_tail(&list->head, &entry->link);
	list->len++;
	return list;
}

struct ExprList *
expr_list_from_set_list(struct Parse *parser, struct ast_set_list *list)
{
	assert(list != NULL);
	struct ExprList *res = NULL;
	struct ast_set_list_entry *entry;
	stailq_foreach_entry(entry, &list->head, link) {
		struct Expr *expr = expr_from_ast(parser, entry->expr);
		if (expr == NULL)
			break;
		if (entry->ids != NULL) {
			struct IdList *ids = id_list_from_ast(entry->ids);
			res = sqlExprListAppendVector(parser, res, ids, expr);
		} else {
			res = sql_expr_list_append(res, expr);
			sqlExprListSetName(parser, res, &entry->name, 1);
		}
	}
	if (parser->is_aborted) {
		sql_expr_list_delete(res);
		return NULL;
	}
	return res;
}

struct ast_update *
ast_update_new(struct region *region)
{
	struct ast_update *res = xregion_alloc_object(region, typeof(*res));
	memset(res, 0, sizeof(*res));
	return res;
}

struct ast_trigger_action_list *
ast_trigger_action_list_append(struct region *region,
			       struct ast_trigger_action_list *list,
			       struct ast_trigger_action *action)
{
	if (list == NULL) {
		list = xregion_alloc_object(region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	stailq_add_tail(&list->head, &action->link);
	list->len++;
	return list;
}

struct ast_delete *
ast_delete_new(struct region *region)
{
	struct ast_delete *res = xregion_alloc_object(region, typeof(*res));
	memset(res, 0, sizeof(*res));
	return res;
}

struct ast_trigger_action *
ast_trigger_action_new(struct region *region)
{
	struct ast_trigger_action *res =
		xregion_alloc_object(region, typeof(*res));
	memset(res, 0, sizeof(*res));
	return res;
}

struct ast_property *
ast_property_new(struct region *region)
{
	struct ast_property *res = xregion_alloc_object(region, typeof(*res));
	memset(res, 0, sizeof(*res));
	return res;
}

struct ast_property_list *
ast_property_list_append(struct region *region, struct ast_property_list *list,
			 struct ast_property *property)
{
	if (list == NULL) {
		list = xregion_alloc_object(region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	stailq_add_tail(&list->head, &property->link);
	list->len++;
	return list;
}

struct ast_column *
ast_column_new(struct region *region)
{
	struct ast_column *res = xregion_alloc_object(region, typeof(*res));
	memset(res, 0, sizeof(*res));
	return res;
}

struct ast_table_properties *
ast_table_properties_new(struct region *region)
{
	struct ast_table_properties *res =
		xregion_alloc_object(region, typeof(*res));
	stailq_create(&res->columns);
	stailq_create(&res->constraints);
	return res;
}

struct ast_table_properties *
ast_table_properties_append_column(struct ast_table_properties *properties,
				   struct ast_column *column)
{
	stailq_add_tail(&properties->columns, &column->link);
	return properties;
}

struct ast_table_properties *
ast_table_properties_append_constraint(struct ast_table_properties *properties,
				       struct ast_property *constraint)
{
	stailq_add_tail(&properties->constraints, &constraint->link);
	return properties;
}

/** Convert UPDATE statement to trigger UPDATE step. */
static struct TriggerStep *
sql_trigger_step_update(struct Parse *parser, struct ast_update *stmt)
{
	if (stmt->indexed_by.n > 0) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 "The INDEXED BY clause is not allowed on UPDATE or "
			 "DELETE statements within triggers");
		parser->is_aborted = true;
		return NULL;
	}
	struct ExprList *set_list =
		expr_list_from_set_list(parser, stmt->set_list);
	if (parser->is_aborted)
		return NULL;
	struct Expr *where = expr_from_ast(parser, stmt->where);
	if (parser->is_aborted) {
		sql_expr_list_delete(set_list);
		return NULL;
	}
	return sql_trigger_update_step(&stmt->table, set_list, where,
				       stmt->action);
}

/** Convert INSERT statement to trigger INSERT step. */
static struct TriggerStep *
sql_trigger_step_insert(struct Parse *parser, struct ast_insert *stmt)
{
	struct Select *select = select_from_ast(parser, stmt->select);
	if (parser->is_aborted) {
		sql_select_delete(select);
		return NULL;
	}
	return sql_trigger_insert_step(&stmt->table, stmt->columns, select,
				       stmt->action);
}

/** Convert DELETE statement to trigger DELETE step. */
static struct TriggerStep *
sql_trigger_step_delete(struct Parse *parser, struct ast_delete *stmt)
{
	if (stmt->indexed_by.n > 0) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 "The INDEXED BY clause is not allowed on UPDATE or "
			 "DELETE statements within triggers");
		parser->is_aborted = true;
		return NULL;
	}
	struct Expr *where = expr_from_ast(parser, stmt->where);
	if (parser->is_aborted)
		return NULL;
	return sql_trigger_delete_step(&stmt->table, where);
}

/** Convert SELECT statement to trigger SELECT step. */
static struct TriggerStep *
sql_trigger_step_select(struct Parse *parser, struct ast_select *stmt)
{
	struct Select *select = select_from_ast(parser, stmt);
	if (parser->is_aborted) {
		sql_select_delete(select);
		return NULL;
	}
	return sql_trigger_select_step(select);
}

struct sql_trigger *
sql_trigger_from_ast(struct Parse *parser, struct ast_trigger *def)
{
	parser->disableLookaside++;
	sql_get()->lookaside.bDisable++;
	if (!def->is_for_each_row) {
		diag_set(ClientError, ER_UNSUPPORTED, "Tarantool SQL",
			 "FOR EACH STATEMENT triggers, please supply "
			 "FOR EACH ROW clause");
		parser->is_aborted = true;
		return NULL;
	}

	struct Expr *when = expr_from_ast(parser, def->when);
	if (parser->is_aborted)
		return NULL;

	parser->initiateTTrans = true;
	struct TriggerStep *steps = NULL;
	struct ast_trigger_action *action;
	stailq_foreach_entry(action, &def->actions->head, link) {
		struct TriggerStep *step = NULL;
		switch (action->op) {
		case TK_UPDATE:
			step = sql_trigger_step_update(parser, action->update);
			break;
		case TK_INSERT:
			step = sql_trigger_step_insert(parser, action->insert);
			break;
		case TK_DELETE:
			step = sql_trigger_step_delete(parser, action->del);
			break;
		case TK_SELECT:
			step = sql_trigger_step_select(parser, action->select);
			break;
		default:
			assert(false);
		}
		if (parser->is_aborted)
			break;
		if (steps != NULL)
			steps->pLast->pNext = step;
		else
			steps = step;
		steps->pLast = step;
	}

	if (parser->is_aborted) {
		sqlDeleteTriggerStep(steps);
		sql_expr_delete(when);
		return NULL;
	}

	struct IdList *columns = id_list_from_ast(def->columns);
	return sql_trigger_new(parser, &def->name, &def->table, def->time,
			       def->event, columns, when, steps);
}

struct sql_ast *
sql_ast_new(struct region *region)
{
	struct sql_ast *res = xregion_alloc_object(region, typeof(*res));
	memset(res, 0, sizeof(*res));
	return res;
}
