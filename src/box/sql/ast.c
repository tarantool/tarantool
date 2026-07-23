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
		list = xregion_alloc_object(&parser->region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	struct ast_id_entry *entry =
		xregion_alloc_object(&parser->region, typeof(*entry));
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
	struct ast_source *src =
		xregion_alloc_object(&parser->region, typeof(*src));
	memset(src, 0, sizeof(*src));
	return src;
}

struct ast_source_list *
ast_source_list_append(struct Parse *parser, struct ast_source_list *list,
		       struct ast_source *src)
{
	if (list == NULL) {
		list = xregion_alloc_object(&parser->region, typeof(*list));
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
		if (src->is_tab_func) {
			struct ExprList *func_args =
				expr_list_from_ast(parser, src->func_args);
			sqlSrcListFuncArgs(res, func_args);
		}
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
	struct ast_select *res =
		xregion_alloc_object(&parser->region, typeof(*res));
	memset(res, 0, sizeof(*res));
	rlist_create(&res->link);
	res->op = TK_SELECT;
	return res;
}

/** Build single `struct Select` object from `struct ast_select` object. */
static struct Select *
select_from_ast_single(struct Parse *parser, struct ast_select *select)
{
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
		list = xregion_alloc_object(&parser->region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
	}
	struct ast_with_entry *entry =
		xregion_alloc_object(&parser->region, typeof(*entry));
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

struct ast_expr *
ast_expr_new(struct Parse *parser, const char *str, uint32_t len, uint8_t op)
{
	struct ast_expr *expr =
		xregion_alloc_object(&parser->region, typeof(*expr));
	expr->left = NULL;
	expr->right = NULL;
	expr->str = str;
	expr->len = len;
	expr->op = op;
	return expr;
}

struct ast_expr_list *
ast_expr_list_append(struct Parse *parser, struct ast_expr_list *list,
		     struct ast_expr *expr)
{
	struct ast_expr_list_entry *entry =
		xregion_alloc_object(&parser->region, typeof(*entry));
	entry->name = Token_nil;
	entry->expr = expr;
	entry->order = SORT_ORDER_ASC;
	if (list == NULL) {
		list = xregion_alloc_object(&parser->region, typeof(*list));
		stailq_create(&list->head);
		list->len = 0;
		list->is_select_list = false;
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
ast_expr_list_set_order(struct ast_expr_list *list, enum sort_order order)
{
	struct ast_expr_list_entry *entry =
		stailq_last_entry(&list->head, typeof(*entry), link);
	entry->order = order;
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
		if (list->is_select_list)
			sqlExprListSetSpan(res, ast_expr->str, ast_expr->len);
		if (entry->order != SORT_ORDER_ASC)
			sqlExprListSetSortOrder(res, entry->order);
	}
	if (parser->is_aborted) {
		sql_expr_list_delete(res);
		return NULL;
	}
	return res;
}

/** Build an identifier `struct Expr`, e.g. a function or collation name. */
static struct Expr *
expr_id(struct ast_expr *expr)
{
	struct Token t;
	t.z = expr->str;
	t.n = expr->len;
	t.isReserved = false;
	struct Expr *res = sql_expr_new_dequoted(expr->op, &t);
	res->type = FIELD_TYPE_SCALAR;
	if (expr->str[0] != '"')
		res->flags |= EP_Lookup2;
	return res;
}

/** Build a leaf `struct Expr` (a literal) of the given field type. */
static struct Expr *
expr_leaf(struct ast_expr *expr, enum field_type type)
{
	struct Token t;
	t.z = expr->str;
	t.n = expr->len;
	t.isReserved = false;
	struct Expr *res = sql_expr_new_dequoted(expr->op, &t);
	res->type = type;
	res->flags |= EP_Leaf;
	return res;
}

/** Build a `struct Expr` for a bound variable (`?`, `:name`, etc). */
static struct Expr *
expr_var(struct Parse *parser, struct ast_expr *expr)
{
	struct Token t;
	t.z = expr->str;
	t.n = expr->len;
	t.isReserved = false;
	if (parser->parse_only) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC_WITH_POS,
			 parser->line_count, parser->line_pos,
			 "bindings are not allowed in DDL");
		parser->is_aborted = true;
		return NULL;
	}
	if (expr->len > 1) {
		assert(expr->str[0] != '?');
		if (!IdChar(expr->str[1])) {
			diag_set(ClientError, ER_SQL_UNKNOWN_TOKEN,
				 parser->line_count,
				 expr->str - parser->zTail + 1,
				 tt_cstr(expr->str, 1));
			parser->is_aborted = true;
			return NULL;
		}
		if (expr->str[0] == '#' && sqlIsdigit(expr->str[1])) {
			diag_set(ClientError, ER_SQL_SYNTAX_NEAR_TOKEN,
				 parser->line_count, tt_cstr(expr->str, 1));
			parser->is_aborted = true;
			return NULL;
		}
	}
	struct Expr *res = sql_expr_new_dequoted(expr->op, &t);
	res->type = FIELD_TYPE_BOOLEAN;
	res->flags |= EP_Leaf;
	sqlExprAssignVarNumber(parser, res, expr->len);
	return res;
}

/** Build a `struct Expr` for a unary operator applied to `expr->left`. */
static struct Expr *
expr_unary(struct Parse *parser, struct ast_expr *expr)
{
	struct Expr *left = expr_from_ast(parser, expr->left);
	if (parser->is_aborted)
		return NULL;
	return sqlPExpr(parser, expr->op, left, NULL);
}

/** Build a `struct Expr` for a binary operator applied to left and right. */
static struct Expr *
expr_binary(struct Parse *parser, struct ast_expr *expr)
{
	struct Expr *left = expr_from_ast(parser, expr->left);
	if (parser->is_aborted)
		return NULL;
	struct Expr *right = expr_from_ast(parser, expr->right);
	if (parser->is_aborted) {
		sql_expr_delete(left);
		return NULL;
	}
	return sqlPExpr(parser, expr->op, left, right);
}

/** Build a `struct Expr` of the given type whose operand is expr->list. */
static struct Expr *
expr_list(struct Parse *parser, struct ast_expr *expr, enum field_type type)
{
	struct Expr *res = sql_expr_new_anon(expr->op);
	res->x.pList = expr_list_from_ast(parser, expr->list);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	res->type = type;
	sqlExprSetHeightAndFlags(parser, res);
	return res;
}

/** Build a `struct Expr` with a left operand and an expression list operand. */
static struct Expr *
expr_left_and_list(struct Parse *parser, struct ast_expr *expr)
{
	struct Expr *left = expr_from_ast(parser, expr->left);
	if (parser->is_aborted)
		return NULL;
	struct Expr *res = sqlPExpr(parser, expr->op, left, NULL);
	res->x.pList = expr_list_from_ast(parser, expr->list);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	sqlExprSetHeightAndFlags(parser, res);
	return res;
}

/** Build a `struct Expr` for a function call expression. */
static struct Expr *
expr_function(struct Parse *parser, struct ast_expr *expr)
{
	struct Expr *res = expr_id(expr->left);
	res->op = TK_FUNCTION;
	if (expr->right == NULL)
		return res;
	if (expr->right->op == TK_DISTINCT)
		res->flags |= EP_Distinct;
	if (expr->right->list == NULL)
		return res;
	if (expr->right->list->len > SQL_MAX_FUNCTION_ARG) {
		const char *err = tt_sprintf("Number of arguments to "
					     "function %s", res->u.zToken);
		diag_set(ClientError, ER_SQL_PARSER_LIMIT, err,
			 expr->right->list->len, SQL_MAX_FUNCTION_ARG);
		parser->is_aborted = true;
		sql_expr_delete(res);
		return NULL;
	}
	res->x.pList = expr_list_from_ast(parser, expr->right->list);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	sqlExprSetHeightAndFlags(parser, res);
	return res;
}

/** Build a `struct Expr` for an IN expression (subquery or value list). */
static struct Expr *
expr_in(struct Parse *parser, struct ast_expr *expr)
{
	if (expr->right->op == TK_SELECT) {
		struct Expr *left = expr_from_ast(parser, expr->left);
		if (parser->is_aborted)
			return NULL;
		struct Select *select = select_from_ast(parser,
							expr->right->select);
		if (parser->is_aborted) {
			sql_expr_delete(left);
			return NULL;
		}
		struct Expr *res = sqlPExpr(parser, expr->op, left, NULL);
		sqlPExprAddSelect(parser, res, select);
		return res;
	}
	assert(expr->right->op == TK_VECTOR);
	if (expr->right->list == NULL || expr->right->list->len == 0) {
		struct Expr *res = sql_expr_new_anon(TK_FALSE);
		res->type = FIELD_TYPE_BOOLEAN;
		return res;
	}
	if (expr->right->list->len == 1) {
		struct Expr *left = expr_from_ast(parser, expr->left);
		if (parser->is_aborted)
			return NULL;
		struct ast_expr_list_entry *entry =
			stailq_first_entry(&expr->right->list->head,
					   typeof(*entry), link);
		struct Expr *right = expr_from_ast(parser, entry->expr);
		if (parser->is_aborted) {
			sql_expr_delete(left);
			return NULL;
		}
		return sqlPExpr(parser, TK_EQ, left, right);
	}
	struct Expr *left = expr_from_ast(parser, expr->left);
	if (parser->is_aborted)
		return NULL;
	struct Expr *res = sqlPExpr(parser, expr->op, left, NULL);
	res->x.pList = expr_list_from_ast(parser, expr->right->list);
	if (parser->is_aborted) {
		sql_expr_delete(res);
		return NULL;
	}
	sqlExprSetHeightAndFlags(parser, res);
	return res;
}

/** Build a `struct Expr` for a subscripting operator expression. */
static struct Expr *
expr_getitem(struct Parse *parser, struct ast_expr *expr)
{
	struct ExprList *list = expr_list_from_ast(parser, expr->list);
	if (parser->is_aborted)
		return NULL;
	struct Expr *left = expr_from_ast(parser, expr->left);
	if (parser->is_aborted)
		return NULL;
	struct Expr *res = sql_expr_new_anon(expr->op);
	res->x.pList = sql_expr_list_append(list, left);
	res->type = FIELD_TYPE_ANY;
	sqlExprSetHeightAndFlags(parser, res);
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
	case TK_VARIABLE:
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
	case TK_PARENTHESES:
		while (expr->op == TK_PARENTHESES)
			expr = expr->left;
		res = expr_from_ast(parser, expr);
		break;
	case TK_COLLATE:
		res = expr_id(expr->right);
		assert(res != NULL);
		res->op = TK_COLLATE;
		res->flags |= EP_Collate | EP_Skip;
		res->pLeft = expr_from_ast(parser, expr->left);
		break;
	case TK_CAST:
		res = expr_unary(parser, expr);
		if (res == NULL)
			break;
		res->type = expr->type;
		break;
	case TK_NOT:
	case TK_BITNOT:
	case TK_UMINUS:
	case TK_UPLUS:
	case TK_NOTNULL:
	case TK_ISNULL:
		res = expr_unary(parser, expr);
		break;
	case TK_ARRAY:
		res = expr_list(parser, expr, FIELD_TYPE_ARRAY);
		break;
	case TK_MAP:
		res = expr_list(parser, expr, FIELD_TYPE_MAP);
		break;
	case TK_GETITEM:
		res = expr_getitem(parser, expr);
		break;
	case TK_FUNCTION:
		res = expr_function(parser, expr);
		break;
	case TK_BETWEEN:
		res = expr_left_and_list(parser, expr);
		break;
	case TK_VECTOR:
		res = expr_list(parser, expr, FIELD_TYPE_ANY);
		break;
	case TK_IN:
		res = expr_in(parser, expr);
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
		if (expr->on_conflict_action != ON_CONFLICT_ACTION_IGNORE)
			res = expr_leaf(expr->left, FIELD_TYPE_STRING);
		else
			res = sql_expr_new_anon(expr->op);
		res->op = TK_RAISE;
		res->on_conflict_action = expr->on_conflict_action;
		break;
	case TK_CASE:
		if (expr->left != NULL)
			res = expr_left_and_list(parser, expr);
		else
			res = expr_list(parser, expr, FIELD_TYPE_ANY);
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
