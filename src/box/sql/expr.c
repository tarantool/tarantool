/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

/*
 * This file contains routines used for analyzing expressions and
 * for generating VDBE code that evaluates expressions in sql.
 */
#include "box/coll_id_cache.h"
#include "coll/coll.h"
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "box/schema.h"
#include "box/session.h"

/* Forward declarations */
static void exprCodeBetween(Parse *, Expr *, int,
			    void (*)(Parse *, Expr *, int, int), int);
static int exprCodeVector(Parse * pParse, Expr * p, int *piToFree);

enum field_type
sql_expr_type(struct Expr *pExpr)
{
	pExpr = sqlExprSkipCollate(pExpr);
	uint8_t op = pExpr->op;
	struct ExprList *el;
	if (op == TK_REGISTER)
		op = pExpr->op2;
	switch (op) {
	case TK_SELECT:
		assert(pExpr->flags & EP_xIsSelect);
		el = pExpr->x.pSelect->pEList;
		return sql_expr_type(el->a[0].pExpr);
	case TK_CAST:
		assert(!ExprHasProperty(pExpr, EP_IntValue));
		return pExpr->type;
	case TK_AGG_COLUMN:
	case TK_COLUMN_REF:
	case TK_TRIGGER:
		assert(pExpr->iColumn >= 0);
		return pExpr->space_def->fields[pExpr->iColumn].type;
	case TK_SELECT_COLUMN:
		assert(pExpr->pLeft->flags & EP_xIsSelect);
		el = pExpr->pLeft->x.pSelect->pEList;
		return sql_expr_type(el->a[pExpr->iColumn].pExpr);
	case TK_PLUS:
	case TK_MINUS:
	case TK_STAR:
	case TK_SLASH:
	case TK_REM:
	case TK_BITAND:
	case TK_BITOR:
	case TK_LSHIFT:
	case TK_RSHIFT:
		assert(pExpr->pRight != NULL && pExpr->pLeft != NULL);
		enum field_type lhs_type = sql_expr_type(pExpr->pLeft);
		enum field_type rhs_type = sql_expr_type(pExpr->pRight);
		return sql_type_result(rhs_type, lhs_type);
	case TK_CONCAT:
		return FIELD_TYPE_STRING;
	case TK_CASE: {
		struct ExprList *cs = pExpr->x.pList;
		assert(cs->nExpr >= 2);
		/*
		 * CASE expression comes at least with one
		 * WHEN and one THEN clauses. So, first
		 * expression always represents WHEN
		 * argument, and the second one - THEN.
		 * In case at least one type of THEN argument
		 * is different from others then we can't
		 * determine type of returning value at compiling
		 * stage and set SCALAR (i.e. most general) type.
		 */
		enum field_type ref_type = sql_expr_type(cs->a[1].pExpr);
		for (int i = 3; i < cs->nExpr; i += 2) {
			if (ref_type != sql_expr_type(cs->a[i].pExpr))
				return FIELD_TYPE_SCALAR;
		}
		/*
		 * ELSE clause is optional but we should check
		 * its type as well.
		 */
		if (cs->nExpr % 2 == 1 &&
		    ref_type != sql_expr_type(cs->a[cs->nExpr - 1].pExpr))
			return FIELD_TYPE_SCALAR;
		return ref_type;
	}
	case TK_LT:
	case TK_GT:
	case TK_EQ:
	case TK_LE:
	case TK_NE:
	case TK_NOT:
	case TK_AND:
	case TK_OR:
	case TK_ISNULL:
	case TK_NOTNULL:
	case TK_BETWEEN:
	case TK_EXISTS:
	case TK_IN:
	case TK_IS:
		return FIELD_TYPE_BOOLEAN;
	case TK_UMINUS:
	case TK_UPLUS:
	case TK_NO:
	case TK_BITNOT:
		assert(pExpr->pRight == NULL);
		return sql_expr_type(pExpr->pLeft);
	}
	return pExpr->type;
}

enum field_type *
field_type_sequence_dup(struct Parse *parse, enum field_type *types,
			uint32_t len)
{
	uint32_t sz = (len + 1) * sizeof(enum field_type);
	enum field_type *ret_types = sqlDbMallocRaw(parse->db, sz);
	if (ret_types == NULL)
		return NULL;
	memcpy(ret_types, types, sz);
	ret_types[len] = field_type_MAX;
	return ret_types;
}

/*
 * Set the collating sequence for expression pExpr to be the collating
 * sequence named by pToken.   Return a pointer to a new Expr node that
 * implements the COLLATE operator.
 *
 * If a memory allocation error occurs, that fact is recorded in pParse->db
 * and the pExpr parameter is returned unchanged.
 */
Expr *
sqlExprAddCollateToken(Parse * pParse,	/* Parsing context */
			   Expr * pExpr,	/* Add the "COLLATE" clause to this expression */
			   const Token * pCollName,	/* Name of collating sequence */
			   int dequote	/* True to dequote pCollName */
    )
{
	if (pCollName->n == 0)
		return pExpr;
	struct Expr *new_expr;
	struct sql *db = pParse->db;
	if (dequote)
		new_expr = sql_expr_new_dequoted(db, TK_COLLATE, pCollName);
	else
		new_expr = sql_expr_new(db, TK_COLLATE, pCollName);
	if (new_expr == NULL) {
		pParse->is_aborted = true;
		return pExpr;
	}
	new_expr->pLeft = pExpr;
	new_expr->flags |= EP_Collate | EP_Skip;
	return new_expr;
}

Expr *
sqlExprAddCollateString(Parse * pParse, Expr * pExpr, const char *zC)
{
	Token s;
	assert(zC != 0);
	sqlTokenInit(&s, (char *)zC);
	return sqlExprAddCollateToken(pParse, pExpr, &s, 0);
}

/*
 * Skip over any TK_COLLATE operators and any unlikely()
 * or likelihood() function at the root of an expression.
 */
Expr *
sqlExprSkipCollate(Expr * pExpr)
{
	while (pExpr && ExprHasProperty(pExpr, EP_Skip)) {
		if (ExprHasProperty(pExpr, EP_Unlikely)) {
			assert(!ExprHasProperty(pExpr, EP_xIsSelect));
			assert(pExpr->x.pList->nExpr > 0);
			assert(pExpr->op == TK_FUNCTION);
			pExpr = pExpr->x.pList->a[0].pExpr;
		} else {
			assert(pExpr->op == TK_COLLATE);
			pExpr = pExpr->pLeft;
		}
	}
	return pExpr;
}

/*
 * Check that left node of @a expr with the collation in the root
 * can be used with <COLLATE>. If it is not, leave an error
 * message in pParse.
 *
 * @param parse Parser context.
 * @param expr Expression for checking.
 *
 * @retval 0 on success.
 * @retval -1 on error.
 */
static int
check_collate_arg(struct Parse *parse, struct Expr *expr)
{
	struct Expr *left = expr->pLeft;
	while (left->op == TK_COLLATE)
		left = left->pLeft;
	enum field_type type = sql_expr_type(left);
	if (type != FIELD_TYPE_STRING && type != FIELD_TYPE_SCALAR) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 "COLLATE clause can't be used with non-string "
			 "arguments");
		parse->is_aborted = true;
		return -1;
	}
	return 0;
}

int
sql_expr_coll(Parse *parse, Expr *p, bool *is_explicit_coll, uint32_t *coll_id,
	      struct coll **coll)
{
	assert(coll != NULL);
	*is_explicit_coll = false;
	*coll_id = COLL_NONE;
	*coll = NULL;
	while (p != NULL) {
		int op = p->op;
		if (op == TK_CAST || op == TK_UPLUS) {
			p = p->pLeft;
			continue;
		}
		if (op == TK_COLLATE ||
		    (op == TK_REGISTER && p->op2 == TK_COLLATE)) {
			*coll = sql_get_coll_seq(parse, p->u.zToken, coll_id);
			if (coll == NULL)
				return -1;
			*is_explicit_coll = true;
			break;
		}
		if ((op == TK_AGG_COLUMN || op == TK_COLUMN_REF ||
		     op == TK_REGISTER || op == TK_TRIGGER) &&
		    p->space_def != NULL) {
			/*
			 * op==TK_REGISTER && p->space_def!=0
			 * happens when pExpr was originally
			 * a TK_COLUMN_REF but was previously
			 * evaluated and cached in a register.
			 */
			int j = p->iColumn;
			if (j >= 0) {
				*coll = sql_column_collation(p->space_def, j,
							     coll_id);
			}
			break;
		}
		if (op == TK_CONCAT) {
			/*
			 * Procedure below provides compatibility
			 * checks declared in ANSI SQL 2013:
			 * chapter 9.5 Result of data type
			 * combinations.
			 */
			bool is_lhs_forced;
			uint32_t lhs_coll_id;
			if (sql_expr_coll(parse, p->pLeft, &is_lhs_forced,
					  &lhs_coll_id, coll) != 0)
				return -1;
			bool is_rhs_forced;
			uint32_t rhs_coll_id;
			if (sql_expr_coll(parse, p->pRight, &is_rhs_forced,
					  &rhs_coll_id, coll) != 0)
				return -1;
			if (is_lhs_forced && is_rhs_forced) {
				if (lhs_coll_id != rhs_coll_id) {
					/*
					 * Don't set the same error
					 * several times: this
					 * function is recursive.
					 */
					if (!parse->is_aborted) {
						diag_set(ClientError,
							 ER_ILLEGAL_COLLATION_MIX);
						parse->is_aborted = true;
					}
					return -1;
				}
			}
			if (is_lhs_forced) {
				*coll_id = lhs_coll_id;
				*is_explicit_coll = true;
				break;
			}
			if (is_rhs_forced) {
				*coll_id = rhs_coll_id;
				*is_explicit_coll = true;
				break;
			}
			if (rhs_coll_id != lhs_coll_id)
				break;
			*coll_id = lhs_coll_id;
			break;
		}
		if (op == TK_FUNCTION) {
			uint32_t arg_count = p->x.pList == NULL ? 0 :
					     p->x.pList->nExpr;
			struct func *func =
				sql_func_by_signature(p->u.zToken, arg_count);
			if (func == NULL)
				break;
			if (sql_func_flag_is_set(func, SQL_FUNC_DERIVEDCOLL) &&
			    arg_count > 0) {
				/*
				 * Now we use quite straightforward
				 * approach assuming that resulting
				 * collation is derived from first
				 * argument. It is true at least for
				 * built-in functions: trim, upper,
				 * lower, replace, substr.
				 */
				assert(func->def->returns == FIELD_TYPE_STRING);
				p = p->x.pList->a->pExpr;
				continue;
			}
			break;
		}
		if (p->flags & EP_Collate) {
			if (p->pLeft && (p->pLeft->flags & EP_Collate) != 0) {
				p = p->pLeft;
			} else {
				Expr *next = p->pRight;
				/* The Expr.x union is never used at the same time as Expr.pRight */
				assert(p->x.pList == 0 || p->pRight == 0);
				/* p->flags holds EP_Collate and p->pLeft->flags does not.  And
				 * p->x.pSelect cannot.  So if p->x.pLeft exists, it must hold at
				 * least one EP_Collate. Thus the following two ALWAYS.
				 */
				if (p->x.pList != NULL &&
				    ALWAYS(!ExprHasProperty(p, EP_xIsSelect))) {
					for (int i = 0;
					     ALWAYS(i < p->x.pList->nExpr);
					     i++) {
						Expr *e =
							p->x.pList->a[i].pExpr;
						if (ExprHasProperty(e,
								    EP_Collate)) {
							next = e;
							break;
						}
					}
				}
				p = next;
			}
		} else {
			break;
		}
	}
	return 0;
}

enum field_type
sql_type_result(enum field_type lhs, enum field_type rhs)
{
	if (sql_type_is_numeric(lhs) || sql_type_is_numeric(rhs)) {
		if (lhs == FIELD_TYPE_NUMBER || rhs == FIELD_TYPE_NUMBER)
			return FIELD_TYPE_NUMBER;
		if (lhs == FIELD_TYPE_DOUBLE || rhs == FIELD_TYPE_DOUBLE)
			return FIELD_TYPE_DOUBLE;
		if (lhs == FIELD_TYPE_INTEGER || rhs == FIELD_TYPE_INTEGER)
			return FIELD_TYPE_INTEGER;
		assert(lhs == FIELD_TYPE_UNSIGNED ||
		       rhs == FIELD_TYPE_UNSIGNED);
		return FIELD_TYPE_UNSIGNED;
	}
	return FIELD_TYPE_SCALAR;
}

enum field_type
expr_cmp_mutual_type(struct Expr *pExpr)
{
	assert(pExpr->op == TK_EQ || pExpr->op == TK_IN || pExpr->op == TK_LT ||
	       pExpr->op == TK_GT || pExpr->op == TK_GE || pExpr->op == TK_LE ||
	       pExpr->op == TK_NE);
	assert(pExpr->pLeft);
	enum field_type type = sql_expr_type(pExpr->pLeft);
	if (pExpr->pRight) {
		enum field_type rhs_type = sql_expr_type(pExpr->pRight);
		type = sql_type_result(rhs_type, type);
	} else if (ExprHasProperty(pExpr, EP_xIsSelect)) {
		enum field_type rhs_type =
			sql_expr_type(pExpr->x.pSelect->pEList->a[0].pExpr);
		type = sql_type_result(rhs_type, type);
	} else {
		type = FIELD_TYPE_SCALAR;
	}
	return type;
}


/*
 * Return the P5 value that should be used for a binary comparison
 * opcode (OP_Eq, OP_Ge etc.) used to compare pExpr1 and pExpr2.
 */
static u8
binaryCompareP5(Expr * pExpr1, Expr * pExpr2, int jumpIfNull)
{
	enum field_type lhs = sql_expr_type(pExpr2);
	enum field_type rhs = sql_expr_type(pExpr1);
	u8 type_mask = sql_type_result(rhs, lhs) | (u8) jumpIfNull;
	return type_mask;
}

int
collations_check_compatibility(uint32_t lhs_id, bool is_lhs_forced,
			       uint32_t rhs_id, bool is_rhs_forced,
			       uint32_t *res_id)
{
	assert(res_id != NULL);
	if (is_lhs_forced && is_rhs_forced) {
		if (lhs_id != rhs_id)
			goto illegal_collation_mix;
	}
	if (is_lhs_forced) {
		*res_id = lhs_id;
		return 0;
	}
	if (is_rhs_forced) {
		*res_id = rhs_id;
		return 0;
	}
	if (lhs_id != rhs_id) {
		if (lhs_id == COLL_NONE) {
			*res_id = rhs_id;
			return 0;
		}
		if (rhs_id == COLL_NONE) {
			*res_id = lhs_id;
			return 0;
		}
		goto illegal_collation_mix;
	}
	*res_id = lhs_id;
	return 0;
illegal_collation_mix:
	diag_set(ClientError, ER_ILLEGAL_COLLATION_MIX);
	return -1;
}

int
sql_binary_compare_coll_seq(Parse *parser, Expr *left, Expr *right,
			    uint32_t *id)
{
	assert(left != NULL);
	assert(id != NULL);
	bool is_lhs_forced;
	bool is_rhs_forced;
	uint32_t lhs_coll_id;
	uint32_t rhs_coll_id;
	struct coll *unused;
	if (sql_expr_coll(parser, left, &is_lhs_forced, &lhs_coll_id,
			  &unused) != 0)
		return -1;
	if (sql_expr_coll(parser, right, &is_rhs_forced, &rhs_coll_id,
			  &unused) != 0)
		return -1;
	if (collations_check_compatibility(lhs_coll_id, is_lhs_forced,
					   rhs_coll_id, is_rhs_forced, id) != 0) {
		parser->is_aborted = true;
		return -1;
	}
	return 0;
}

/*
 * Generate code for a comparison operator.
 */
static int
codeCompare(Parse * pParse,	/* The parsing (and code generating) context */
	    Expr * pLeft,	/* The left operand */
	    Expr * pRight,	/* The right operand */
	    int opcode,		/* The comparison opcode */
	    int in1, int in2,	/* Register holding operands */
	    int dest,		/* Jump here if true.  */
	    int jumpIfNull	/* If true, jump if either operand is NULL */
    )
{
	uint32_t id;
	if (sql_binary_compare_coll_seq(pParse, pLeft, pRight, &id) != 0)
		return -1;
	struct coll *coll = coll_by_id(id)->coll;
	int p5 = binaryCompareP5(pLeft, pRight, jumpIfNull);
	int addr = sqlVdbeAddOp4(pParse->pVdbe, opcode, in2, dest, in1,
				     (void *)coll, P4_COLLSEQ);
	sqlVdbeChangeP5(pParse->pVdbe, (u8) p5);
	return addr;
}

/*
 * Return true if expression pExpr is a vector, or false otherwise.
 *
 * A vector is defined as any expression that results in two or more
 * columns of result.  Every TK_VECTOR node is an vector because the
 * parser will not generate a TK_VECTOR with fewer than two entries.
 * But a TK_SELECT might be either a vector or a scalar. It is only
 * considered a vector if it has two or more result columns.
 */
int
sqlExprIsVector(Expr * pExpr)
{
	return sqlExprVectorSize(pExpr) > 1;
}

/*
 * If the expression passed as the only argument is of type TK_VECTOR
 * return the number of expressions in the vector. Or, if the expression
 * is a sub-select, return the number of columns in the sub-select. For
 * any other type of expression, return 1.
 */
int
sqlExprVectorSize(Expr * pExpr)
{
	u8 op = pExpr->op;
	if (op == TK_REGISTER)
		op = pExpr->op2;
	if (op == TK_VECTOR) {
		return pExpr->x.pList->nExpr;
	} else if (op == TK_SELECT) {
		return pExpr->x.pSelect->pEList->nExpr;
	} else {
		return 1;
	}
}

/*
 * Return a pointer to a subexpression of pVector that is the i-th
 * column of the vector (numbered starting with 0).  The caller must
 * ensure that i is within range.
 *
 * If pVector is really a scalar (and "scalar" here includes subqueries
 * that return a single column!) then return pVector unmodified.
 *
 * pVector retains ownership of the returned subexpression.
 *
 * If the vector is a (SELECT ...) then the expression returned is
 * just the expression for the i-th term of the result set, and may
 * not be ready for evaluation because the table cursor has not yet
 * been positioned.
 */
Expr *
sqlVectorFieldSubexpr(Expr * pVector, int i)
{
	assert(i < sqlExprVectorSize(pVector));
	if (sqlExprIsVector(pVector)) {
		assert(pVector->op2 == 0 || pVector->op == TK_REGISTER);
		if (pVector->op == TK_SELECT || pVector->op2 == TK_SELECT) {
			return pVector->x.pSelect->pEList->a[i].pExpr;
		} else {
			return pVector->x.pList->a[i].pExpr;
		}
	}
	return pVector;
}

/*
 * Compute and return a new Expr object which when passed to
 * sqlExprCode() will generate all necessary code to compute
 * the iField-th column of the vector expression pVector.
 *
 * It is ok for pVector to be a scalar (as long as iField==0).
 * In that case, this routine works like sqlExprDup().
 *
 * The caller owns the returned Expr object and is responsible for
 * ensuring that the returned value eventually gets freed.
 *
 * The caller retains ownership of pVector.  If pVector is a TK_SELECT,
 * then the returned object will reference pVector and so pVector must remain
 * valid for the life of the returned object.  If pVector is a TK_VECTOR
 * or a scalar expression, then it can be deleted as soon as this routine
 * returns.
 *
 * A trick to cause a TK_SELECT pVector to be deleted together with
 * the returned Expr object is to attach the pVector to the pRight field
 * of the returned TK_SELECT_COLUMN Expr object.
 */
Expr *
sqlExprForVectorField(Parse * pParse,	/* Parsing context */
			  Expr * pVector,	/* The vector.  List of expressions or a sub-SELECT */
			  int iField	/* Which column of the vector to return */
    )
{
	Expr *pRet;
	if (pVector->op == TK_SELECT) {
		assert(pVector->flags & EP_xIsSelect);
		/* The TK_SELECT_COLUMN Expr node:
		 *
		 * pLeft:           pVector containing TK_SELECT.  Not deleted.
		 * pRight:          not used.  But recursively deleted.
		 * iColumn:         Index of a column in pVector
		 * iTable:          0 or the number of columns on the LHS of an assignment
		 * pLeft->iTable:   First in an array of register holding result, or 0
		 *                  if the result is not yet computed.
		 *
		 * sql_expr_delete() specifically skips the recursive delete of
		 * pLeft on TK_SELECT_COLUMN nodes.  But pRight is followed, so pVector
		 * can be attached to pRight to cause this node to take ownership of
		 * pVector.  Typically there will be multiple TK_SELECT_COLUMN nodes
		 * with the same pLeft pointer to the pVector, but only one of them
		 * will own the pVector.
		 */
		pRet = sqlPExpr(pParse, TK_SELECT_COLUMN, 0, 0);
		if (pRet) {
			pRet->iColumn = iField;
			pRet->pLeft = pVector;
		}
		assert(pRet == 0 || pRet->iTable == 0);
	} else {
		if (pVector->op == TK_VECTOR)
			pVector = pVector->x.pList->a[iField].pExpr;
		pRet = sqlExprDup(pParse->db, pVector, 0);
	}
	return pRet;
}

/*
 * If expression pExpr is of type TK_SELECT, generate code to evaluate
 * it. Return the register in which the result is stored (or, if the
 * sub-select returns more than one column, the first in an array
 * of registers in which the result is stored).
 *
 * If pExpr is not a TK_SELECT expression, return 0.
 */
static int
exprCodeSubselect(Parse * pParse, Expr * pExpr)
{
	if (pExpr->op == TK_SELECT)
		return sqlCodeSubselect(pParse, pExpr, 0);
	else
		return 0;
}

/*
 * Argument pVector points to a vector expression - either a TK_VECTOR
 * or TK_SELECT that returns more than one column. This function returns
 * the register number of a register that contains the value of
 * element iField of the vector.
 *
 * If pVector is a TK_SELECT expression, then code for it must have
 * already been generated using the exprCodeSubselect() routine. In this
 * case parameter regSelect should be the first in an array of registers
 * containing the results of the sub-select.
 *
 * If pVector is of type TK_VECTOR, then code for the requested field
 * is generated. In this case (*pRegFree) may be set to the number of
 * a temporary register to be freed by the caller before returning.
 *
 * Before returning, output parameter (*ppExpr) is set to point to the
 * Expr object corresponding to element iElem of the vector.
 */
static int
exprVectorRegister(Parse * pParse,	/* Parse context */
		   Expr * pVector,	/* Vector to extract element from */
		   int iField,	/* Field to extract from pVector */
		   int regSelect,	/* First in array of registers */
		   Expr ** ppExpr,	/* OUT: Expression element */
		   int *pRegFree	/* OUT: Temp register to free */
    )
{
	u8 op = pVector->op;
	assert(op == TK_VECTOR || op == TK_REGISTER || op == TK_SELECT);
	if (op == TK_REGISTER) {
		*ppExpr = sqlVectorFieldSubexpr(pVector, iField);
		return pVector->iTable + iField;
	}
	if (op == TK_SELECT) {
		*ppExpr = pVector->x.pSelect->pEList->a[iField].pExpr;
		return regSelect + iField;
	}
	*ppExpr = pVector->x.pList->a[iField].pExpr;
	return sqlExprCodeTemp(pParse, *ppExpr, pRegFree);
}

/*
 * Expression pExpr is a comparison between two vector values. Compute
 * the result of the comparison (1, 0, or NULL) and write that
 * result into register dest.
 */
static void
codeVectorCompare(Parse * pParse,	/* Code generator context */
		  Expr * pExpr,	/* The comparison operation */
		  int dest	/* Write results into this register */
    )
{
	Vdbe *v = pParse->pVdbe;
	Expr *pLeft = pExpr->pLeft;
	Expr *pRight = pExpr->pRight;
	int nLeft = sqlExprVectorSize(pLeft);
	int i;
	int regLeft = 0;
	int regRight = 0;
	u8 op = pExpr->op;
	int addrDone = sqlVdbeMakeLabel(v);

	/*
	 * Situation when vectors have different dimensions is
	 * filtred way before - during expr resolution:
	 * see resolveExprStep().
	 */
	assert(nLeft == sqlExprVectorSize(pRight));
	assert(pExpr->op == TK_EQ || pExpr->op == TK_NE
	       || pExpr->op == TK_LT || pExpr->op == TK_GT
	       || pExpr->op == TK_LE || pExpr->op == TK_GE);

	u8 opx;
	u8 p5 = SQL_STOREP2;
	if (op == TK_LE)
		opx = TK_LT;
	else if (op == TK_GE)
		opx = TK_GT;
	else
		opx = op;

	regLeft = exprCodeSubselect(pParse, pLeft);
	regRight = exprCodeSubselect(pParse, pRight);

	for (i = 0; 1 /*Loop exits by "break" */ ; i++) {
		int regFree1 = 0, regFree2 = 0;
		Expr *pL, *pR;
		int r1, r2;
		assert(i >= 0 && i < nLeft);
		if (i > 0)
			sqlExprCachePush(pParse);
		r1 = exprVectorRegister(pParse, pLeft, i, regLeft, &pL,
					&regFree1);
		r2 = exprVectorRegister(pParse, pRight, i, regRight, &pR,
					&regFree2);
		codeCompare(pParse, pL, pR, opx, r1, r2, dest, p5);
		testcase(op == OP_Lt);
		VdbeCoverageIf(v, op == OP_Lt);
		testcase(op == OP_Le);
		VdbeCoverageIf(v, op == OP_Le);
		testcase(op == OP_Gt);
		VdbeCoverageIf(v, op == OP_Gt);
		testcase(op == OP_Ge);
		VdbeCoverageIf(v, op == OP_Ge);
		testcase(op == OP_Eq);
		VdbeCoverageIf(v, op == OP_Eq);
		testcase(op == OP_Ne);
		VdbeCoverageIf(v, op == OP_Ne);
		sqlReleaseTempReg(pParse, regFree1);
		sqlReleaseTempReg(pParse, regFree2);
		if (i > 0)
			sqlExprCachePop(pParse);
		if (i == nLeft - 1) {
			break;
		}
		if (opx == TK_EQ) {
			sqlVdbeAddOp2(v, OP_IfNot, dest, addrDone);
			VdbeCoverage(v);
			p5 |= SQL_KEEPNULL;
		} else if (opx == TK_NE) {
			sqlVdbeAddOp2(v, OP_If, dest, addrDone);
			VdbeCoverage(v);
			p5 |= SQL_KEEPNULL;
		} else {
			assert(op == TK_LT || op == TK_GT || op == TK_LE
			       || op == TK_GE);
			sqlVdbeAddOp2(v, OP_ElseNotEq, 0, addrDone);
			VdbeCoverageIf(v, op == TK_LT);
			VdbeCoverageIf(v, op == TK_GT);
			VdbeCoverageIf(v, op == TK_LE);
			VdbeCoverageIf(v, op == TK_GE);
			if (i == nLeft - 2)
				opx = op;
		}
	}
	sqlVdbeResolveLabel(v, addrDone);
}

#if SQL_MAX_EXPR_DEPTH>0
/*
 * Check that argument nHeight is less than or equal to the maximum
 * expression depth allowed. If it is not, leave an error message in
 * pParse.
 *
 * @param pParse Parser context.
 * @param zName Depth to check.
 *
 * @retval 0 on success.
 * @retval -1 on error.
 */
int
sqlExprCheckHeight(Parse * pParse, int nHeight)
{
	int mxHeight = pParse->db->aLimit[SQL_LIMIT_EXPR_DEPTH];
	if (nHeight > mxHeight) {
		diag_set(ClientError, ER_SQL_PARSER_LIMIT, "Number of nodes "\
			 "in expression tree", nHeight, mxHeight);
		pParse->is_aborted = true;
		return -1;
	}
	return 0;
}

/* The following three functions, heightOfExpr(), heightOfExprList()
 * and heightOfSelect(), are used to determine the maximum height
 * of any expression tree referenced by the structure passed as the
 * first argument.
 *
 * If this maximum height is greater than the current value pointed
 * to by pnHeight, the second parameter, then set *pnHeight to that
 * value.
 */
static void
heightOfExpr(Expr * p, int *pnHeight)
{
	if (p) {
		if (p->nHeight > *pnHeight) {
			*pnHeight = p->nHeight;
		}
	}
}

static void
heightOfExprList(ExprList * p, int *pnHeight)
{
	if (p) {
		int i;
		for (i = 0; i < p->nExpr; i++) {
			heightOfExpr(p->a[i].pExpr, pnHeight);
		}
	}
}

static void
heightOfSelect(Select * p, int *pnHeight)
{
	if (p) {
		heightOfExpr(p->pWhere, pnHeight);
		heightOfExpr(p->pHaving, pnHeight);
		heightOfExpr(p->pLimit, pnHeight);
		heightOfExpr(p->pOffset, pnHeight);
		heightOfExprList(p->pEList, pnHeight);
		heightOfExprList(p->pGroupBy, pnHeight);
		heightOfExprList(p->pOrderBy, pnHeight);
		heightOfSelect(p->pPrior, pnHeight);
	}
}

/*
 * Set the Expr.nHeight variable in the structure passed as an
 * argument. An expression with no children, Expr.pList or
 * Expr.pSelect member has a height of 1. Any other expression
 * has a height equal to the maximum height of any other
 * referenced Expr plus one.
 *
 * Also propagate EP_Propagate flags up from Expr.x.pList to Expr.flags,
 * if appropriate.
 */
static void
exprSetHeight(Expr * p)
{
	int nHeight = 0;
	heightOfExpr(p->pLeft, &nHeight);
	heightOfExpr(p->pRight, &nHeight);
	if (ExprHasProperty(p, EP_xIsSelect)) {
		heightOfSelect(p->x.pSelect, &nHeight);
	} else if (p->x.pList) {
		heightOfExprList(p->x.pList, &nHeight);
		p->flags |= EP_Propagate & sqlExprListFlags(p->x.pList);
	}
	p->nHeight = nHeight + 1;
}

/*
 * Set the Expr.nHeight variable using the exprSetHeight() function. If
 * the height is greater than the maximum allowed expression depth,
 * leave an error in pParse.
 *
 * Also propagate all EP_Propagate flags from the Expr.x.pList into
 * Expr.flags.
 */
void
sqlExprSetHeightAndFlags(Parse * pParse, Expr * p)
{
	if (pParse->is_aborted)
		return;
	exprSetHeight(p);
	sqlExprCheckHeight(pParse, p->nHeight);
}

/*
 * Return the maximum height of any expression tree referenced
 * by the select statement passed as an argument.
 */
int
sqlSelectExprHeight(Select * p)
{
	int nHeight = 0;
	heightOfSelect(p, &nHeight);
	return nHeight;
}
#else				/* ABOVE:  Height enforcement enabled.  BELOW: Height enforcement off */
/*
 * Propagate all EP_Propagate flags from the Expr.x.pList into
 * Expr.flags.
 */
void
sqlExprSetHeightAndFlags(Parse * pParse, Expr * p)
{
	if (p && p->x.pList && !ExprHasProperty(p, EP_xIsSelect)) {
		p->flags |= EP_Propagate & sqlExprListFlags(p->x.pList);
	}
}

#define exprSetHeight(y)
#endif				/* SQL_MAX_EXPR_DEPTH>0 */

/**
 * Allocate a new empty expression object with reserved extra
 * memory.
 * @param db SQL context.
 * @param op Expression value type.
 * @param extra_size Extra size, needed to be allocated together
 *        with the expression.
 * @retval Not NULL Success. An empty expression.
 * @retval NULL Error. A diag message is set.
 */
static struct Expr *
sql_expr_new_empty(struct sql *db, int op, int extra_size)
{
	struct Expr *e = sqlDbMallocRawNN(db, sizeof(*e) + extra_size);
	if (e == NULL) {
		diag_set(OutOfMemory, sizeof(*e), "sqlDbMallocRawNN", "e");
		return NULL;
	}
	memset(e, 0, sizeof(*e));
	e->op = (u8)op;
	e->iAgg = -1;
#if SQL_MAX_EXPR_DEPTH > 0
	e->nHeight = 1;
#endif
	return e;
}

/**
 * Try to convert a token of a specified type to integer.
 * @param op Token type.
 * @param token Token itself.
 * @param[out] res Result integer.
 * @retval 0 Success. @A res stores a result.
 * @retval -1 Error. Can not be converted. No diag.
 */
static inline int
sql_expr_token_to_int(int op, const struct Token *token, int *res)
{
	if (op == TK_INTEGER && token->z != NULL &&
	    sqlGetInt32(token->z, res) > 0)
		return 0;
	return -1;
}

/** Create an expression of a constant integer. */
static inline struct Expr *
sql_expr_new_int(struct sql *db, int value)
{
	struct Expr *e = sql_expr_new_empty(db, TK_INTEGER, 0);
	if (e != NULL) {
		e->flags |= EP_IntValue;
		e->u.iValue = value;
	}
	return e;
}

struct Expr *
sql_expr_new(struct sql *db, int op, const struct Token *token)
{
	int extra_sz = 0;
	if (token != NULL) {
		int val;
		if (sql_expr_token_to_int(op, token, &val) == 0)
			return sql_expr_new_int(db, val);
		extra_sz = token->n + 1;
	}
	struct Expr *e = sql_expr_new_empty(db, op, extra_sz);
	if (e == NULL || token == NULL)
		return e;
	e->u.zToken = (char *) &e[1];
	assert(token->z != NULL || token->n == 0);
	memcpy(e->u.zToken, token->z, token->n);
	e->u.zToken[token->n] = '\0';
	return e;
}

struct Expr *
sql_expr_new_dequoted(struct sql *db, int op, const struct Token *token)
{
	int extra_size = 0, rc;
	if (token != NULL) {
		int val;
		assert(token->z != NULL || token->n == 0);
		if (sql_expr_token_to_int(op, token, &val) == 0)
			return sql_expr_new_int(db, val);
		extra_size = token->n + 1;
	}
	struct Expr *e = sql_expr_new_empty(db, op, extra_size);
	if (e == NULL || token == NULL || token->n == 0)
		return e;
	e->u.zToken = (char *) &e[1];
	if (token->z[0] == '"')
		e->flags |= EP_DblQuoted;
	if (op != TK_ID && op != TK_COLLATE && op != TK_FUNCTION) {
		memcpy(e->u.zToken, token->z, token->n);
		e->u.zToken[token->n] = '\0';
		sqlDequote(e->u.zToken);
	} else if ((rc = sql_normalize_name(e->u.zToken, extra_size, token->z,
					    token->n)) > extra_size) {
		extra_size = rc;
		e = sqlDbReallocOrFree(db, e, sizeof(*e) + extra_size);
		if (e == NULL)
			return NULL;
		e->u.zToken = (char *) &e[1];
		if (sql_normalize_name(e->u.zToken, extra_size, token->z,
				       token->n) > extra_size)
			unreachable();
	}
	return e;
}

/*
 * Attach subtrees pLeft and pRight to the Expr node pRoot.
 *
 * If pRoot==NULL that means that a memory allocation error has occurred.
 * In that case, delete the subtrees pLeft and pRight.
 */
void
sqlExprAttachSubtrees(sql * db,
			  Expr * pRoot, Expr * pLeft, Expr * pRight)
{
	if (pRoot == 0) {
		assert(db->mallocFailed);
		sql_expr_delete(db, pLeft, false);
		sql_expr_delete(db, pRight, false);
	} else {
		if (pRight) {
			pRoot->pRight = pRight;
			pRoot->flags |= EP_Propagate & pRight->flags;
		}
		if (pLeft) {
			pRoot->pLeft = pLeft;
			pRoot->flags |= EP_Propagate & pLeft->flags;
		}
		exprSetHeight(pRoot);
	}
}

/*
 * Allocate an Expr node which joins as many as two subtrees.
 *
 * One or both of the subtrees can be NULL.  Return a pointer to the new
 * Expr node.  Or, if an OOM error occurs, set pParse->db->mallocFailed,
 * free the subtrees and return NULL.
 */
Expr *
sqlPExpr(Parse * pParse,	/* Parsing context */
	     int op,		/* Expression opcode */
	     Expr * pLeft,	/* Left operand */
	     Expr * pRight	/* Right operand */
    )
{
	Expr *p;
	if (op == TK_AND && !pParse->is_aborted) {
		/*
		 * Take advantage of short-circuit false
		 * optimization for AND.
		 */
		p = sql_and_expr_new(pParse->db, pLeft, pRight);
		if (p == NULL)
			pParse->is_aborted = true;
	} else {
		p = sqlDbMallocRawNN(pParse->db, sizeof(Expr));
		if (p) {
			memset(p, 0, sizeof(Expr));
			p->op = op & TKFLG_MASK;
			p->iAgg = -1;
		}
		sqlExprAttachSubtrees(pParse->db, p, pLeft, pRight);
	}
	if (p) {
		sqlExprCheckHeight(pParse, p->nHeight);
	}
	return p;
}

/*
 * Add pSelect to the Expr.x.pSelect field.  Or, if pExpr is NULL (due
 * do a memory allocation failure) then delete the pSelect object.
 */
void
sqlPExprAddSelect(Parse * pParse, Expr * pExpr, Select * pSelect)
{
	if (pExpr) {
		pExpr->x.pSelect = pSelect;
		ExprSetProperty(pExpr, EP_xIsSelect | EP_Subquery);
		sqlExprSetHeightAndFlags(pParse, pExpr);
	} else {
		assert(pParse->db->mallocFailed);
		sql_select_delete(pParse->db, pSelect);
	}
}

/**
 * If the expression is always either TRUE or FALSE (respectively),
 * then return 1. If one cannot determine the truth value of the
 * expression at compile-time return 0.
 *
 * Note that if the expression is part of conditional for a
 * LEFT JOIN, then we cannot determine at compile-time whether or not
 * is it true or false, so always return 0.
 */
static inline bool
exprAlwaysTrue(Expr * p)
{
	return !ExprHasProperty(p, EP_FromJoin) && p->op == TK_TRUE;
}

static inline bool
exprAlwaysFalse(Expr * p)
{
	return !ExprHasProperty(p, EP_FromJoin) && p->op == TK_FALSE;
}

struct Expr *
sql_and_expr_new(struct sql *db, struct Expr *left_expr,
		 struct Expr *right_expr)
{
	if (left_expr == NULL) {
		return right_expr;
	} else if (right_expr == NULL) {
		return left_expr;
	} else if (exprAlwaysFalse(left_expr) || exprAlwaysFalse(right_expr)) {
		sql_expr_delete(db, left_expr, false);
		sql_expr_delete(db, right_expr, false);
		struct Expr *f = sql_expr_new_anon(db, TK_FALSE);
		if (f != NULL)
			f->type = FIELD_TYPE_BOOLEAN;
		return f;
	} else {
		struct Expr *new_expr = sql_expr_new_anon(db, TK_AND);
		sqlExprAttachSubtrees(db, new_expr, left_expr, right_expr);
		return new_expr;
	}
}

/*
 * Construct a new expression node for a function with multiple
 * arguments.
 */
Expr *
sqlExprFunction(Parse * pParse, ExprList * pList, Token * pToken)
{
	struct sql *db = pParse->db;
	assert(pToken != NULL);
	struct Expr *new_expr = sql_expr_new_dequoted(db, TK_FUNCTION, pToken);
	if (new_expr == NULL) {
		sql_expr_list_delete(db, pList);
		pParse->is_aborted = true;
		return NULL;
	}
	new_expr->x.pList = pList;
	assert(!ExprHasProperty(new_expr, EP_xIsSelect));
	sqlExprSetHeightAndFlags(pParse, new_expr);
	return new_expr;
}

/*
 * Assign a variable number to an expression that encodes a
 * wildcard in the original SQL statement.
 *
 * Wildcards consisting of a single "?" are assigned the next
 * sequential variable number.
 *
 * Wildcards of the form "$nnn" are assigned the number "nnn".
 * We make sure "nnn" is not too big to avoid a denial of service
 * attack when the SQL statement comes from an external source.
 *
 * Wildcards of the form ":aaa", "@aaa", are assigned the same
 * number as the previous instance of the same wildcard.  Or if
 * this is the first instance of the wildcard, the next sequential variable
 * number is assigned.
 */
void
sqlExprAssignVarNumber(Parse * pParse, Expr * pExpr, u32 n)
{
	sql *db = pParse->db;
	const char *z;
	ynVar x;

	if (pExpr == 0)
		return;
	assert(!ExprHasProperty
	       (pExpr, EP_IntValue | EP_Reduced | EP_TokenOnly));
	z = pExpr->u.zToken;
	assert(z != 0);
	assert(z[0] != 0);
	assert(n == sqlStrlen30(z));
	if (z[1] == 0) {
		/* Wildcard of the form "?".  Assign the next variable number */
		assert(z[0] == '?');
		x = (ynVar) (++pParse->nVar);
	} else {
		int doAdd = 0;
		assert(z[0] != '?');
		if (z[0] == '$') {
			/*
			 * Wildcard of the form "$nnn". Convert
			 * "nnn" to an integer and use it as the
			 * variable number
			 */
			int64_t i;
			bool is_neg;
			bool is_ok = 0 == sql_atoi64(&z[1], &i, &is_neg, n - 1);
			x = (ynVar) i;
			testcase(i == 0);
			testcase(i == 1);
			testcase(i == SQL_BIND_PARAMETER_MAX - 1);
			testcase(i == SQL_BIND_PARAMETER_MAX);
			if (is_neg || i < 1) {
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 "Index of binding slots must start "\
					 "from 1");
				pParse->is_aborted = true;
				return;
			}
			if (!is_ok || i > SQL_BIND_PARAMETER_MAX) {
				diag_set(ClientError, ER_SQL_BIND_PARAMETER_MAX,
					 SQL_BIND_PARAMETER_MAX);
				pParse->is_aborted = true;
				return;
			}
			if (x > pParse->nVar) {
				pParse->nVar = (int)x;
				doAdd = 1;
			} else if (sqlVListNumToName(pParse->pVList, x) ==
				   0) {
				doAdd = 1;
			}
		} else {
			/* Wildcards like ":aaa", or "@aaa".  Reuse the same variable
			 * number as the prior appearance of the same name, or if the name
			 * has never appeared before, reuse the same variable number
			 */
			x = (ynVar) sqlVListNameToNum(pParse->pVList, z, n);
			if (x == 0) {
				x = (ynVar) (++pParse->nVar);
				doAdd = 1;
			}
		}
		if (doAdd) {
			pParse->pVList =
			    sqlVListAdd(db, pParse->pVList, z, n, x);
		}
	}
	pExpr->iColumn = x;
	if (x > SQL_BIND_PARAMETER_MAX) {
		diag_set(ClientError, ER_SQL_BIND_PARAMETER_MAX,
			 SQL_BIND_PARAMETER_MAX);
		pParse->is_aborted = true;
	}
}

/*
 * Recursively delete an expression tree.
 */
static SQL_NOINLINE void
sqlExprDeleteNN(sql * db, Expr * p, bool extern_alloc)
{
	assert(p != 0);
	/* Sanity check: Assert that the IntValue is non-negative if it exists */
	assert(!ExprHasProperty(p, EP_IntValue) || p->u.iValue >= 0);
#ifdef SQL_DEBUG
	if (ExprHasProperty(p, EP_Leaf) && !ExprHasProperty(p, EP_TokenOnly)) {
		assert(p->pLeft == 0);
		assert(p->pRight == 0);
		assert(p->x.pSelect == 0);
	}
#endif
	if (!ExprHasProperty(p, (EP_TokenOnly | EP_Leaf))) {
		/* The Expr.x union is never used at the same time as Expr.pRight */
		assert(p->x.pList == 0 || p->pRight == 0);
		if (p->pLeft && p->op != TK_SELECT_COLUMN && !extern_alloc)
			sqlExprDeleteNN(db, p->pLeft, extern_alloc);
		if (!extern_alloc)
			sql_expr_delete(db, p->pRight, extern_alloc);
		if (ExprHasProperty(p, EP_xIsSelect)) {
			sql_select_delete(db, p->x.pSelect);
		} else {
			sql_expr_list_delete(db, p->x.pList);
		}
	}
	if (ExprHasProperty(p, EP_MemToken))
		sqlDbFree(db, p->u.zToken);
	if (!ExprHasProperty(p, EP_Static)) {
		sqlDbFree(db, p);
	}
}

void
sql_expr_delete(sql *db, Expr *expr, bool extern_alloc)
{
	if (expr != NULL)
		sqlExprDeleteNN(db, expr, extern_alloc);
}

/*
 * Return the number of bytes allocated for the expression structure
 * passed as the first argument. This is always one of EXPR_FULLSIZE,
 * EXPR_REDUCEDSIZE or EXPR_TOKENONLYSIZE.
 */
static int
exprStructSize(Expr * p)
{
	if (ExprHasProperty(p, EP_TokenOnly))
		return EXPR_TOKENONLYSIZE;
	if (ExprHasProperty(p, EP_Reduced))
		return EXPR_REDUCEDSIZE;
	return EXPR_FULLSIZE;
}

/*
 * The dupedExpr*Size() routines each return the number of bytes required
 * to store a copy of an expression or expression tree.  They differ in
 * how much of the tree is measured.
 *
 *     dupedExprStructSize()     Size of only the Expr structure
 *     dupedExprNodeSize()       Size of Expr + space for token
 *     dupedExprSize()           Expr + token + subtree components
 *
 **************************************************************************
 *
 * The dupedExprStructSize() function returns two values OR-ed together:
 * (1) the space required for a copy of the Expr structure only and
 * (2) the EP_xxx flags that indicate what the structure size should be.
 * The return values is always one of:
 *
 *      EXPR_FULLSIZE
 *      EXPR_REDUCEDSIZE   | EP_Reduced
 *      EXPR_TOKENONLYSIZE | EP_TokenOnly
 *
 * The size of the structure can be found by masking the return value
 * of this routine with 0xfff.  The flags can be found by masking the
 * return value with EP_Reduced|EP_TokenOnly.
 *
 * Note that with flags==EXPRDUP_REDUCE, this routines works on full-size
 * (unreduced) Expr objects as they or originally constructed by the parser.
 * During expression analysis, extra information is computed and moved into
 * later parts of teh Expr object and that extra information might get chopped
 * off if the expression is reduced.  Note also that it does not work to
 * make an EXPRDUP_REDUCE copy of a reduced expression.  It is only legal
 * to reduce a pristine expression tree from the parser.  The implementation
 * of dupedExprStructSize() contain multiple assert() statements that attempt
 * to enforce this constraint.
 */
static int
dupedExprStructSize(Expr * p, int flags)
{
	int nSize;
	assert(flags == EXPRDUP_REDUCE || flags == 0);	/* Only one flag value allowed */
	assert(EXPR_FULLSIZE <= 0xfff);
	assert((0xfff & (EP_Reduced | EP_TokenOnly)) == 0);
	if (0 == flags || p->op == TK_SELECT_COLUMN) {
		nSize = EXPR_FULLSIZE;
	} else {
		assert(!ExprHasProperty(p, EP_TokenOnly | EP_Reduced));
		assert(!ExprHasProperty(p, EP_FromJoin));
		assert(!ExprHasProperty(p, EP_MemToken));
		assert(!ExprHasProperty(p, EP_NoReduce));
		if (p->pLeft || p->x.pList) {
			nSize = EXPR_REDUCEDSIZE | EP_Reduced;
		} else {
			assert(p->pRight == 0);
			nSize = EXPR_TOKENONLYSIZE | EP_TokenOnly;
		}
	}
	return nSize;
}

/*
 * This function returns the space in bytes required to store the copy
 * of the Expr structure and a copy of the Expr.u.zToken string (if that
 * string is defined.)
 */
static int
dupedExprNodeSize(Expr * p, int flags)
{
	int nByte = dupedExprStructSize(p, flags) & 0xfff;
	if (!ExprHasProperty(p, EP_IntValue) && p->u.zToken) {
		nByte += sqlStrlen30(p->u.zToken) + 1;
	}
	return ROUND8(nByte);
}

int
sql_expr_sizeof(struct Expr *p, int flags)
{
	int size = 0;
	if (p != NULL) {
		size = dupedExprNodeSize(p, flags);
		if (flags & EXPRDUP_REDUCE) {
			size +=
			    sql_expr_sizeof(p->pLeft, flags) +
			    sql_expr_sizeof(p->pRight, flags);
		}
	}
	return size;
}

struct Expr *
sql_expr_dup(struct sql *db, struct Expr *p, int flags, char **buffer)
{
	Expr *pNew;		/* Value to return */
	u32 staticFlag;         /* EP_Static if space not obtained from malloc */
	char *zAlloc;		/* Memory space from which to build Expr object */

	assert(db != 0);
	assert(p);
	assert(flags == 0 || flags == EXPRDUP_REDUCE);

	/* Figure out where to write the new Expr structure. */
	if (buffer) {
		zAlloc = *buffer;
		staticFlag = EP_Static;
	} else {
		zAlloc = sqlDbMallocRawNN(db,
					      sql_expr_sizeof(p, flags));
		staticFlag = 0;
	}
	pNew = (Expr *) zAlloc;

	if (pNew) {
		/* Set nNewSize to the size allocated for the structure pointed to
		 * by pNew. This is either EXPR_FULLSIZE, EXPR_REDUCEDSIZE or
		 * EXPR_TOKENONLYSIZE. nToken is set to the number of bytes consumed
		 * by the copy of the p->u.zToken string (if any).
		 */
		const unsigned nStructSize = dupedExprStructSize(p, flags);
		const int nNewSize = nStructSize & 0xfff;
		int nToken;
		if (!ExprHasProperty(p, EP_IntValue) && p->u.zToken)
			nToken = sqlStrlen30(p->u.zToken) + 1;
		else
			nToken = 0;
		if (flags) {
			assert(ExprHasProperty(p, EP_Reduced) == 0);
			memcpy(zAlloc, p, nNewSize);
		} else {
			u32 nSize = (u32) exprStructSize(p);
			memcpy(zAlloc, p, nSize);
			if (nSize < EXPR_FULLSIZE) {
				memset(&zAlloc[nSize], 0,
				       EXPR_FULLSIZE - nSize);
			}
		}

		/* Set the EP_Reduced, EP_TokenOnly, and EP_Static flags appropriately. */
		pNew->flags &=
		    ~(EP_Reduced | EP_TokenOnly | EP_Static | EP_MemToken);
		pNew->flags |= nStructSize & (EP_Reduced | EP_TokenOnly);
		pNew->flags |= staticFlag;

		/* Copy the p->u.zToken string, if any. */
		if (nToken) {
			char *zToken = pNew->u.zToken =
			    (char *)&zAlloc[nNewSize];
			memcpy(zToken, p->u.zToken, nToken);
		}

		if (0 == ((p->flags | pNew->flags) & (EP_TokenOnly | EP_Leaf))) {
			/* Fill in the pNew->x.pSelect or pNew->x.pList member. */
			if (ExprHasProperty(p, EP_xIsSelect)) {
				pNew->x.pSelect =
				    sqlSelectDup(db, p->x.pSelect,
						     flags);
			} else {
				pNew->x.pList =
					sql_expr_list_dup(db, p->x.pList, flags);
			}
		}

		/* Fill in pNew->pLeft and pNew->pRight. */
		if (ExprHasProperty(pNew, EP_Reduced | EP_TokenOnly)) {
			zAlloc += dupedExprNodeSize(p, flags);
			if (!ExprHasProperty(pNew, EP_TokenOnly | EP_Leaf)) {
				pNew->pLeft = p->pLeft ?
				    sql_expr_dup(db, p->pLeft, EXPRDUP_REDUCE,
						 &zAlloc) : 0;
				pNew->pRight =
				    p->pRight ? sql_expr_dup(db, p->pRight,
							     EXPRDUP_REDUCE,
							     &zAlloc) : 0;
			}
			if (buffer)
				*buffer = zAlloc;
		} else {
			if (!ExprHasProperty(p, EP_TokenOnly | EP_Leaf)) {
				if (pNew->op == TK_SELECT_COLUMN) {
					pNew->pLeft = p->pLeft;
					assert(p->iColumn == 0
					       || p->pRight == 0);
					assert(p->pRight == 0
					       || p->pRight == p->pLeft);
				} else {
					pNew->pLeft =
					    sqlExprDup(db, p->pLeft, 0);
				}
				pNew->pRight = sqlExprDup(db, p->pRight, 0);
			}
		}
	}
	return pNew;
}

/*
 * Create and return a deep copy of the object passed as the second
 * argument. If an OOM condition is encountered, NULL is returned
 * and the db->mallocFailed flag set.
 */
static With *
withDup(sql * db, With * p)
{
	With *pRet = 0;
	if (p) {
		int nByte = sizeof(*p) + sizeof(p->a[0]) * (p->nCte - 1);
		pRet = sqlDbMallocZero(db, nByte);
		if (pRet) {
			int i;
			pRet->nCte = p->nCte;
			for (i = 0; i < p->nCte; i++) {
				pRet->a[i].pSelect =
				    sqlSelectDup(db, p->a[i].pSelect, 0);
				pRet->a[i].pCols =
				    sql_expr_list_dup(db, p->a[i].pCols, 0);
				pRet->a[i].zName =
				    sqlDbStrDup(db, p->a[i].zName);
			}
		}
	}
	return pRet;
}

/*
 * The following group of routines make deep copies of expressions,
 * expression lists, ID lists, and select statements.  The copies can
 * be deleted (by being passed to their respective ...Delete() routines)
 * without effecting the originals.
 *
 * The expression list, ID, and source lists return by sql_expr_list_dup(),
 * sqlIdListDup(), and sqlSrcListDup() can not be further expanded
 * by subsequent calls to sql*ListAppend() routines.
 *
 * Any tables that the SrcList might point to are not duplicated.
 *
 * The flags parameter contains a combination of the EXPRDUP_XXX flags.
 * If the EXPRDUP_REDUCE flag is set, then the structure returned is a
 * truncated version of the usual Expr structure that will be stored as
 * part of the in-memory representation of the database schema.
 */
Expr *
sqlExprDup(sql * db, Expr * p, int flags)
{
	assert(flags == 0 || flags == EXPRDUP_REDUCE);
	return p ? sql_expr_dup(db, p, flags, 0) : 0;
}

struct ExprList *
sql_expr_list_dup(struct sql *db, struct ExprList *p, int flags)
{
	struct ExprList_item *pItem, *pOldItem;
	int i;
	Expr *pPriorSelectCol = NULL;
	assert(db != NULL);
	if (p == NULL)
		return NULL;
	ExprList *pNew = sqlDbMallocRawNN(db, sizeof(*pNew));
	if (pNew == NULL)
		return NULL;
	pNew->nExpr = i = p->nExpr;
	if ((flags & EXPRDUP_REDUCE) == 0) {
		for (i = 1; i < p->nExpr; i += i) {
		}
	}
	pNew->a = pItem = sqlDbMallocRawNN(db, i * sizeof(p->a[0]));
	if (pItem == NULL) {
		sqlDbFree(db, pNew);
		return NULL;
	}
	pOldItem = p->a;
	for (i = 0; i < p->nExpr; i++, pItem++, pOldItem++) {
		Expr *pOldExpr = pOldItem->pExpr;
		Expr *pNewExpr;
		pItem->pExpr = sqlExprDup(db, pOldExpr, flags);
		if (pOldExpr != NULL && pOldExpr->op == TK_SELECT_COLUMN &&
		    (pNewExpr = pItem->pExpr) != NULL) {
			assert(pNewExpr->iColumn == 0 || i > 0);
			if (pNewExpr->iColumn == 0) {
				assert(pOldExpr->pLeft == pOldExpr->pRight);
				pPriorSelectCol = pNewExpr->pLeft =
					pNewExpr->pRight;
			} else {
				assert(i > 0);
				assert(pItem[-1].pExpr != 0);
				assert(pNewExpr->iColumn ==
				       pItem[-1].pExpr->iColumn + 1);
				assert(pPriorSelectCol ==
				       pItem[-1].pExpr->pLeft);
				pNewExpr->pLeft = pPriorSelectCol;
			}
		}
		pItem->zName = sqlDbStrDup(db, pOldItem->zName);
		pItem->zSpan = sqlDbStrDup(db, pOldItem->zSpan);
		pItem->sort_order = pOldItem->sort_order;
		pItem->done = 0;
		pItem->bSpanIsTab = pOldItem->bSpanIsTab;
		pItem->u = pOldItem->u;
	}
	return pNew;
}

/*
 * If cursors, triggers, views and subqueries are all omitted from
 * the build, then none of the following routines, except for
 * sqlSelectDup(), can be called. sqlSelectDup() is sometimes
 * called with a NULL argument.
 */
SrcList *
sqlSrcListDup(sql * db, SrcList * p, int flags)
{
	SrcList *pNew;
	int i;
	int nByte;
	assert(db != 0);
	if (p == 0)
		return 0;
	nByte =
	    sizeof(*p) + (p->nSrc > 0 ? sizeof(p->a[0]) * (p->nSrc - 1) : 0);
	pNew = sqlDbMallocRawNN(db, nByte);
	if (pNew == 0)
		return 0;
	pNew->nSrc = pNew->nAlloc = p->nSrc;
	for (i = 0; i < p->nSrc; i++) {
		struct SrcList_item *pNewItem = &pNew->a[i];
		struct SrcList_item *pOldItem = &p->a[i];
		pNewItem->zName = sqlDbStrDup(db, pOldItem->zName);
		pNewItem->zAlias = sqlDbStrDup(db, pOldItem->zAlias);
		pNewItem->fg = pOldItem->fg;
		pNewItem->iCursor = pOldItem->iCursor;
		pNewItem->addrFillSub = pOldItem->addrFillSub;
		pNewItem->regReturn = pOldItem->regReturn;
		if (pNewItem->fg.isIndexedBy) {
			pNewItem->u1.zIndexedBy =
			    sqlDbStrDup(db, pOldItem->u1.zIndexedBy);
		}
		pNewItem->pIBIndex = pOldItem->pIBIndex;
		if (pNewItem->fg.isTabFunc) {
			pNewItem->u1.pFuncArg =
			    sql_expr_list_dup(db, pOldItem->u1.pFuncArg, flags);
		}
		pNewItem->space = pOldItem->space;
		pNewItem->pSelect =
		    sqlSelectDup(db, pOldItem->pSelect, flags);
		pNewItem->pOn = sqlExprDup(db, pOldItem->pOn, flags);
		pNewItem->pUsing = sqlIdListDup(db, pOldItem->pUsing);
		pNewItem->colUsed = pOldItem->colUsed;
	}
	return pNew;
}

IdList *
sqlIdListDup(sql * db, IdList * p)
{
	IdList *pNew;
	int i;
	assert(db != 0);
	if (p == 0)
		return 0;
	pNew = sqlDbMallocRawNN(db, sizeof(*pNew));
	if (pNew == 0)
		return 0;
	pNew->nId = p->nId;
	pNew->a = sqlDbMallocRawNN(db, p->nId * sizeof(p->a[0]));
	if (pNew->a == 0) {
		sqlDbFree(db, pNew);
		return 0;
	}
	/*
	 * Note that because the size of the allocation for p->a[]
	 * is not necessarily a power of two, sql_id_list_append()
	 * may not be called on the duplicate created by this
	 * function.
	 */
	for (i = 0; i < p->nId; i++) {
		struct IdList_item *pNewItem = &pNew->a[i];
		struct IdList_item *pOldItem = &p->a[i];
		pNewItem->zName = sqlDbStrDup(db, pOldItem->zName);
		pNewItem->idx = pOldItem->idx;
	}
	return pNew;
}

Select *
sqlSelectDup(sql * db, Select * p, int flags)
{
	Select *pNew, *pPrior;
	assert(db != 0);
	if (p == 0)
		return 0;
	pNew = sqlDbMallocRawNN(db, sizeof(*p));
	if (pNew == 0)
		return 0;
	pNew->pEList = sql_expr_list_dup(db, p->pEList, flags);
	pNew->pSrc = sqlSrcListDup(db, p->pSrc, flags);
	pNew->pWhere = sqlExprDup(db, p->pWhere, flags);
	pNew->pGroupBy = sql_expr_list_dup(db, p->pGroupBy, flags);
	pNew->pHaving = sqlExprDup(db, p->pHaving, flags);
	pNew->pOrderBy = sql_expr_list_dup(db, p->pOrderBy, flags);
	pNew->op = p->op;
	pNew->pPrior = pPrior = sqlSelectDup(db, p->pPrior, flags);
	if (pPrior)
		pPrior->pNext = pNew;
	pNew->pNext = 0;
	pNew->pLimit = sqlExprDup(db, p->pLimit, flags);
	pNew->pOffset = sqlExprDup(db, p->pOffset, flags);
	pNew->iLimit = 0;
	pNew->iOffset = 0;
	pNew->selFlags = p->selFlags & ~SF_UsesEphemeral;
	pNew->addrOpenEphm[0] = -1;
	pNew->addrOpenEphm[1] = -1;
	pNew->nSelectRow = p->nSelectRow;
	pNew->pWith = withDup(db, p->pWith);
	sqlSelectSetName(pNew, p->zSelName);
	return pNew;
}

struct ExprList *
sql_expr_list_append(struct sql *db, struct ExprList *expr_list,
		     struct Expr *expr)
{
	assert(db != NULL);
	if (expr_list == NULL) {
		expr_list = sqlDbMallocRawNN(db, sizeof(ExprList));
		if (expr_list == NULL)
			goto no_mem;
		expr_list->nExpr = 0;
		expr_list->a =
			sqlDbMallocRawNN(db, sizeof(expr_list->a[0]));
		if (expr_list->a == NULL)
			goto no_mem;
	} else if ((expr_list->nExpr & (expr_list->nExpr - 1)) == 0) {
		struct ExprList_item *a;
		assert(expr_list->nExpr > 0);
		a = sqlDbRealloc(db, expr_list->a, expr_list->nExpr * 2 *
				     sizeof(expr_list->a[0]));
		if (a == NULL)
			goto no_mem;
		expr_list->a = a;
	}
	assert(expr_list->a != NULL);
	struct ExprList_item *pItem = &expr_list->a[expr_list->nExpr++];
	memset(pItem, 0, sizeof(*pItem));
	pItem->pExpr = expr;
	return expr_list;

 no_mem:
	/* Avoid leaking memory if malloc has failed. */
	sql_expr_delete(db, expr, false);
	sql_expr_list_delete(db, expr_list);
	return NULL;
}

/*
 * pColumns and pExpr form a vector assignment which is part of the SET
 * clause of an UPDATE statement.  Like this:
 *
 *        (a,b,c) = (expr1,expr2,expr3)
 * Or:    (a,b,c) = (SELECT x,y,z FROM ....)
 *
 * For each term of the vector assignment, append new entries to the
 * expression list pList.  In the case of a subquery on the LHS, append
 * TK_SELECT_COLUMN expressions.
 */
ExprList *
sqlExprListAppendVector(Parse * pParse,	/* Parsing context */
			    ExprList * pList,	/* List to which to append. Might be NULL */
			    IdList * pColumns,	/* List of names of LHS of the assignment */
			    Expr * pExpr	/* Vector expression to be appended. Might be NULL */
    )
{
	sql *db = pParse->db;
	int n;
	int i;
	int iFirst = pList ? pList->nExpr : 0;
	/* pColumns can only be NULL due to an OOM but an OOM will cause an
	 * exit prior to this routine being invoked
	 */
	if (NEVER(pColumns == 0))
		goto vector_append_error;
	if (pExpr == 0)
		goto vector_append_error;

	/* If the RHS is a vector, then we can immediately check to see that
	 * the size of the RHS and LHS match.  But if the RHS is a SELECT,
	 * wildcards ("*") in the result set of the SELECT must be expanded before
	 * we can do the size check, so defer the size check until code generation.
	 */
	if (pExpr->op != TK_SELECT
	    && pColumns->nId != (n = sqlExprVectorSize(pExpr))) {
		const char *err = tt_sprintf("%d columns assigned %d values",
					     pColumns->nId, n);
		diag_set(ClientError, ER_SQL_PARSER_GENERIC, err);
		pParse->is_aborted = true;
		goto vector_append_error;
	}

	for (i = 0; i < pColumns->nId; i++) {
		Expr *pSubExpr = sqlExprForVectorField(pParse, pExpr, i);
		pList = sql_expr_list_append(pParse->db, pList, pSubExpr);
		if (pList) {
			assert(pList->nExpr == iFirst + i + 1);
			pList->a[pList->nExpr - 1].zName = pColumns->a[i].zName;
			pColumns->a[i].zName = 0;
		}
	}

	if (pExpr->op == TK_SELECT) {
		if (pList && pList->a[iFirst].pExpr) {
			Expr *pFirst = pList->a[iFirst].pExpr;
			assert(pFirst->op == TK_SELECT_COLUMN);

			/* Store the SELECT statement in pRight so it will be deleted when
			 * sql_expr_list_delete() is called
			 */
			pFirst->pRight = pExpr;
			pExpr = 0;

			/* Remember the size of the LHS in iTable so that we can check that
			 * the RHS and LHS sizes match during code generation.
			 */
			pFirst->iTable = pColumns->nId;
		}
	}

 vector_append_error:
	sql_expr_delete(db, pExpr, false);
	sqlIdListDelete(db, pColumns);
	return pList;
}

void
sqlExprListSetSortOrder(struct ExprList *p, enum sort_order sort_order)
{
	if (p == 0)
		return;
	assert(p->nExpr > 0);
	if (sort_order == SORT_ORDER_UNDEF) {
		assert(p->a[p->nExpr - 1].sort_order == SORT_ORDER_ASC);
		return;
	}
	p->a[p->nExpr - 1].sort_order = sort_order;
}

void
sql_expr_check_sort_orders(struct Parse *parse,
			   const struct ExprList *expr_list)
{
	if(expr_list == NULL)
		return;
	enum sort_order reference_order = expr_list->a[0].sort_order;
	for (int i = 1; i < expr_list->nExpr; i++) {
		assert(expr_list->a[i].sort_order != SORT_ORDER_UNDEF);
		if (expr_list->a[i].sort_order != reference_order) {
			diag_set(ClientError, ER_UNSUPPORTED,
				 "ORDER BY with LIMIT",
				 "different sorting orders");
			parse->is_aborted = true;
			return;
		}
	}
}

/*
 * Set the ExprList.a[].zName element of the most recently added item
 * on the expression list.
 *
 * pList might be NULL following an OOM error.  But pName should never be
 * NULL.  If a memory allocation fails, the pParse->db->mallocFailed flag
 * is set.
 */
void
sqlExprListSetName(Parse * pParse,	/* Parsing context */
		       ExprList * pList,	/* List to which to add the span. */
		       Token * pName,	/* Name to be added */
		       int dequote	/* True to cause the name to be dequoted */
    )
{
	struct sql *db = pParse->db;
	assert(pList != NULL || db->mallocFailed != 0);
	if (pList == NULL || pName->n == 0)
		return;
	assert(pList->nExpr > 0);
	struct ExprList_item *item = &pList->a[pList->nExpr - 1];
	assert(item->zName == NULL);
	if (dequote) {
		item->zName = sql_normalized_name_db_new(db, pName->z, pName->n);
		if (item->zName == NULL)
			pParse->is_aborted = true;
	} else {
		item->zName = sqlDbStrNDup(db, pName->z, pName->n);
	}
	if (item->zName != NULL)
		sqlCheckIdentifierName(pParse, item->zName);
}

/*
 * Set the ExprList.a[].zSpan element of the most recently added item
 * on the expression list.
 *
 * pList might be NULL following an OOM error.  But pSpan should never be
 * NULL.  If a memory allocation fails, the pParse->db->mallocFailed flag
 * is set.
 */
void
sqlExprListSetSpan(Parse * pParse,	/* Parsing context */
		       ExprList * pList,	/* List to which to add the span. */
		       ExprSpan * pSpan	/* The span to be added */
    )
{
	sql *db = pParse->db;
	assert(pList != 0 || db->mallocFailed != 0);
	if (pList) {
		struct ExprList_item *pItem = &pList->a[pList->nExpr - 1];
		assert(pList->nExpr > 0);
		assert(db->mallocFailed || pItem->pExpr == pSpan->pExpr);
		sqlDbFree(db, pItem->zSpan);
		pItem->zSpan = sqlDbStrNDup(db, (char *)pSpan->zStart,
						(int)(pSpan->zEnd -
						      pSpan->zStart));
	}
}

/*
 * Delete an entire expression list.
 */
static SQL_NOINLINE void
exprListDeleteNN(sql * db, ExprList * pList)
{
	int i;
	struct ExprList_item *pItem;
	assert(pList->a != 0 || pList->nExpr == 0);
	for (pItem = pList->a, i = 0; i < pList->nExpr; i++, pItem++) {
		sql_expr_delete(db, pItem->pExpr, false);
		sqlDbFree(db, pItem->zName);
		sqlDbFree(db, pItem->zSpan);
	}
	sqlDbFree(db, pList->a);
	sqlDbFree(db, pList);
}

void
sql_expr_list_delete(sql *db, ExprList *expr_list)
{
	if (expr_list != NULL)
		exprListDeleteNN(db, expr_list);
}

/*
 * Return the bitwise-OR of all Expr.flags fields in the given
 * ExprList.
 */
u32
sqlExprListFlags(const ExprList * pList)
{
	int i;
	u32 m = 0;
	if (pList) {
		for (i = 0; i < pList->nExpr; i++) {
			Expr *pExpr = pList->a[i].pExpr;
			assert(pExpr != 0);
			m |= pExpr->flags;
		}
	}
	return m;
}

/*
 * These routines are Walker callbacks used to check expressions to
 * see if they are "constant" for some definition of constant.  The
 * Walker.eCode value determines the type of "constant" we are looking
 * for.
 *
 * These callback routines are used to implement the following:
 *
 *     sqlExprIsConstant()                  pWalker->eCode==1
 *     sqlExprIsConstantNotJoin()           pWalker->eCode==2
 *     sqlExprIsTableConstant()             pWalker->eCode==3
 *     sqlExprIsConstantOrFunction()        pWalker->eCode==4 or 5
 *
 * In all cases, the callbacks set Walker.eCode=0 and abort if the expression
 * is found to not be a constant.
 *
 * The sqlExprIsConstantOrFunction() is used for evaluating expressions
 * in a CREATE TABLE statement.  The Walker.eCode value is 4 when processing
 * a new statement.  A bound parameter raises an error for new statements,
 * but is silently converted to NULL for existing schemas.
 */
static int
exprNodeIsConstant(Walker * pWalker, Expr * pExpr)
{

	/* If pWalker->eCode is 2 then any term of the expression that comes from
	 * the ON or USING clauses of a left join disqualifies the expression
	 * from being considered constant.
	 */
	if (pWalker->eCode == 2 && ExprHasProperty(pExpr, EP_FromJoin)) {
		pWalker->eCode = 0;
		return WRC_Abort;
	}

	switch (pExpr->op) {
		/* Consider functions to be constant if all their arguments are constant
		 * and either pWalker->eCode==4 or 5 or the function has the
		 * SQL_FUNC_CONST flag.
		 */
	case TK_FUNCTION:
		if (pWalker->eCode >= 4 || ExprHasProperty(pExpr, EP_ConstFunc)) {
			return WRC_Continue;
		} else {
			pWalker->eCode = 0;
			return WRC_Abort;
		}
	case TK_ID:
	case TK_COLUMN_REF:
	case TK_AGG_FUNCTION:
	case TK_AGG_COLUMN:
		testcase(pExpr->op == TK_ID);
		testcase(pExpr->op == TK_COLUMN_REF);
		testcase(pExpr->op == TK_AGG_FUNCTION);
		testcase(pExpr->op == TK_AGG_COLUMN);
		if (pWalker->eCode == 3 && pExpr->iTable == pWalker->u.iCur) {
			return WRC_Continue;
		} else {
			pWalker->eCode = 0;
			return WRC_Abort;
		}
	case TK_VARIABLE:
		if (pWalker->eCode == 4) {
			/* A bound parameter in a CREATE statement that originates from
			 * sql_prepare() causes an error
			 */
			pWalker->eCode = 0;
			return WRC_Abort;
		}
		/* Fall through */
	default:
		testcase(pExpr->op == TK_SELECT);	/* selectNodeIsConstant will disallow */
		testcase(pExpr->op == TK_EXISTS);	/* selectNodeIsConstant will disallow */
		return WRC_Continue;
	}
}

static int
selectNodeIsConstant(Walker * pWalker, Select * NotUsed)
{
	UNUSED_PARAMETER(NotUsed);
	pWalker->eCode = 0;
	return WRC_Abort;
}

static int
exprIsConst(Expr * p, int initFlag, int iCur)
{
	Walker w;
	memset(&w, 0, sizeof(w));
	w.eCode = initFlag;
	w.xExprCallback = exprNodeIsConstant;
	w.xSelectCallback = selectNodeIsConstant;
	w.u.iCur = iCur;
	sqlWalkExpr(&w, p);
	return w.eCode;
}

/*
 * Walk an expression tree.  Return non-zero if the expression is constant
 * and 0 if it involves variables or function calls.
 *
 * For the purposes of this function, a double-quoted string (ex: "abc")
 * is considered a variable but a single-quoted string (ex: 'abc') is
 * a constant.
 */
int
sqlExprIsConstant(Expr * p)
{
	return exprIsConst(p, 1, 0);
}

/*
 * Walk an expression tree.  Return non-zero if the expression is constant
 * that does no originate from the ON or USING clauses of a join.
 * Return 0 if it involves variables or function calls or terms from
 * an ON or USING clause.
 */
int
sqlExprIsConstantNotJoin(Expr * p)
{
	return exprIsConst(p, 2, 0);
}

/*
 * Walk an expression tree.  Return non-zero if the expression is constant
 * for any single row of the table with cursor iCur.  In other words, the
 * expression must not refer to any non-deterministic function nor any
 * table other than iCur.
 */
int
sqlExprIsTableConstant(Expr * p, int iCur)
{
	return exprIsConst(p, 3, iCur);
}

/*
 * Walk an expression tree.  Return non-zero if the expression is constant
 * or a function call with constant arguments.  Return and 0 if there
 * are any variables.
 *
 * For the purposes of this function, a double-quoted string (ex: "abc")
 * is considered a variable but a single-quoted string (ex: 'abc') is
 * a constant.
 */
int
sqlExprIsConstantOrFunction(Expr * p, u8 isInit)
{
	assert(isInit == 0 || isInit == 1);
	return exprIsConst(p, 4 + isInit, 0);
}

/*
 * If the expression p codes a constant integer that is small enough
 * to fit in a 32-bit integer, return 1 and put the value of the integer
 * in *pValue.  If the expression is not an integer or if it is too big
 * to fit in a signed 32-bit integer, return 0 and leave *pValue unchanged.
 */
int
sqlExprIsInteger(Expr * p, int *pValue)
{
	int rc = 0;

	/* If an expression is an integer literal that fits in a signed 32-bit
	 * integer, then the EP_IntValue flag will have already been set
	 */
	assert(p->op != TK_INTEGER || (p->flags & EP_IntValue) != 0
	       || sqlGetInt32(p->u.zToken, &rc) == 0);

	if (p->flags & EP_IntValue) {
		*pValue = p->u.iValue;
		return 1;
	}
	switch (p->op) {
	case TK_UPLUS:{
			rc = sqlExprIsInteger(p->pLeft, pValue);
			break;
		}
	case TK_UMINUS:{
			int v;
			if (sqlExprIsInteger(p->pLeft, &v)) {
				assert(v != (-2147483647 - 1));
				*pValue = -v;
				rc = 1;
			}
			break;
		}
	default:
		break;
	}
	return rc;
}

/*
 * Return FALSE if there is no chance that the expression can be NULL.
 *
 * If the expression might be NULL or if the expression is too complex
 * to tell return TRUE.
 *
 * This routine is used as an optimization, to skip OP_IsNull opcodes
 * when we know that a value cannot be NULL.  Hence, a false positive
 * (returning TRUE when in fact the expression can never be NULL) might
 * be a small performance hit but is otherwise harmless.  On the other
 * hand, a false negative (returning FALSE when the result could be NULL)
 * will likely result in an incorrect answer.  So when in doubt, return
 * TRUE.
 */
int
sqlExprCanBeNull(const Expr * p)
{
	u8 op;
	while (p->op == TK_UPLUS || p->op == TK_UMINUS) {
		p = p->pLeft;
	}
	op = p->op;
	if (op == TK_REGISTER)
		op = p->op2;
	switch (op) {
	case TK_INTEGER:
	case TK_STRING:
	case TK_FLOAT:
	case TK_BLOB:
		return 0;
	case TK_COLUMN_REF:
		assert(p->space_def != 0);
		return ExprHasProperty(p, EP_CanBeNull) ||
		       (p->iColumn >= 0
		        && p->space_def->fields[p->iColumn].is_nullable);
	default:
		return 1;
	}
}

bool
sql_expr_needs_no_type_change(const struct Expr *p, enum field_type type)
{
	u8 op;
	if (type == FIELD_TYPE_SCALAR)
		return true;
	while (p->op == TK_UPLUS || p->op == TK_UMINUS) {
		p = p->pLeft;
	}
	op = p->op;
	if (op == TK_REGISTER)
		op = p->op2;
	switch (op) {
	case TK_INTEGER:
		return type == FIELD_TYPE_INTEGER;
	case TK_FLOAT:
		return type == FIELD_TYPE_DOUBLE;
	case TK_STRING:
		return type == FIELD_TYPE_STRING;
	case TK_BLOB:
		return type == FIELD_TYPE_VARBINARY;
	case TK_COLUMN_REF:
		/* p cannot be part of a CHECK constraint. */
		assert(p->iTable >= 0);
		return p->iColumn < 0 && sql_type_is_numeric(type);
	default:
		return false;
	}
}

/*
 * pX is the RHS of an IN operator.  If pX is a SELECT statement
 * that can be simplified to a direct table access, then return
 * a pointer to the SELECT statement.  If pX is not a SELECT statement,
 * or if the SELECT statement needs to be manifested into a transient
 * table, then return NULL.
 */
static Select *
isCandidateForInOpt(Expr * pX)
{
	Select *p;
	SrcList *pSrc;
	ExprList *pEList;
	int i;
	if (!ExprHasProperty(pX, EP_xIsSelect))
		return 0;	/* Not a subquery */
	if (ExprHasProperty(pX, EP_VarSelect))
		return 0;	/* Correlated subq */
	p = pX->x.pSelect;
	if (p->pPrior)
		return 0;	/* Not a compound SELECT */
	if (p->selFlags & (SF_Distinct | SF_Aggregate)) {
		testcase((p->selFlags & (SF_Distinct | SF_Aggregate)) ==
			 SF_Distinct);
		testcase((p->selFlags & (SF_Distinct | SF_Aggregate)) ==
			 SF_Aggregate);
		return 0;	/* No DISTINCT keyword and no aggregate functions */
	}
	assert(p->pGroupBy == 0);	/* Has no GROUP BY clause */
	if (p->pLimit)
		return 0;	/* Has no LIMIT clause */
	assert(p->pOffset == 0);	/* No LIMIT means no OFFSET */
	if (p->pWhere)
		return 0;	/* Has no WHERE clause */
	pSrc = p->pSrc;
	assert(pSrc != 0);
	if (pSrc->nSrc != 1)
		return 0;	/* Single term in FROM clause */
	if (pSrc->a[0].pSelect)
		return 0;	/* FROM is not a subquery or view */
	assert(pSrc->a[0].space != NULL);
	/* FROM clause is not a view */
	assert(!pSrc->a[0].space->def->opts.is_view);
	pEList = p->pEList;
	assert(pEList != 0);
	/* All SELECT results must be columns. */
	for (i = 0; i < pEList->nExpr; i++) {
		Expr *pRes = pEList->a[i].pExpr;
		if (pRes->op != TK_COLUMN_REF)
			return 0;
		assert(pRes->iTable == pSrc->a[0].iCursor);	/* Not a correlated subquery */
	}
	return p;
}

/*
 * Generate code that checks the left-most column of index table iCur to see if
 * it contains any NULL entries.  Cause the register at regHasNull to be set
 * to a non-NULL value if iCur contains no NULLs.  Cause register regHasNull
 * to be set to NULL if iCur contains one or more NULL values.
 *
 * TARANTOOL: Key field of index is not first column, it is column number
 * in the original table instead. So, to do proper check add argumment
 * to the function containing column number to check.
 */
static void
sqlSetHasNullFlag(Vdbe * v, int iCur, int iCol, int regHasNull)
{
	int addr1;
	sqlVdbeAddOp2(v, OP_Integer, 0, regHasNull);
	addr1 = sqlVdbeAddOp1(v, OP_Rewind, iCur);
	VdbeCoverage(v);
	sqlVdbeAddOp3(v, OP_Column, iCur, iCol, regHasNull);
	sqlVdbeChangeP5(v, OPFLAG_TYPEOFARG);
	VdbeComment((v, "first_entry_in(%d)", iCur));
	sqlVdbeJumpHere(v, addr1);
}

/*
 * The argument is an IN operator with a list (not a subquery) on the
 * right-hand side.  Return TRUE if that list is constant.
 */
static int
sqlInRhsIsConstant(Expr * pIn)
{
	Expr *pLHS;
	int res;
	assert(!ExprHasProperty(pIn, EP_xIsSelect));
	pLHS = pIn->pLeft;
	pIn->pLeft = 0;
	res = sqlExprIsConstant(pIn);
	pIn->pLeft = pLHS;
	return res;
}

/*
 * This function is used by the implementation of the IN (...) operator.
 * The pX parameter is the expression on the RHS of the IN operator, which
 * might be either a list of expressions or a subquery.
 *
 * The job of this routine is to find or create a b-tree object that can
 * be used either to test for membership in the RHS set or to iterate through
 * all members of the RHS set, skipping duplicates.
 *
 * A cursor is opened on the b-tree object that is the RHS of the IN operator
 * and pX->iTable is set to the index of that cursor.
 *
 * The returned value of this function indicates the b-tree type, as follows:
 *
 *   IN_INDEX_INDEX_ASC  - The cursor was opened on an ascending index.
 *   IN_INDEX_INDEX_DESC - The cursor was opened on a descending index.
 *   IN_INDEX_EPH        - The cursor was opened on a specially created and
 *                         populated epheremal table.
 *   IN_INDEX_NOOP       - No cursor was allocated.  The IN operator must be
 *                         implemented as a sequence of comparisons.
 *
 * An existing b-tree might be used if the RHS expression pX is a simple
 * subquery such as:
 *
 *     SELECT <column1>, <column2>... FROM <table>
 *
 * If the RHS of the IN operator is a list or a more complex subquery, then
 * an ephemeral table might need to be generated from the RHS and then
 * pX->iTable made to point to the ephemeral table instead of an
 * existing table.
 *
 * The inFlags parameter must contain exactly one of the bits
 * IN_INDEX_MEMBERSHIP or IN_INDEX_LOOP.  If inFlags contains
 * IN_INDEX_MEMBERSHIP, then the generated table will be used for a
 * fast membership test.  When the IN_INDEX_LOOP bit is set, the
 * IN index will be used to loop over all values of the RHS of the
 * IN operator.
 *
 * When IN_INDEX_LOOP is used (and the b-tree will be used to iterate
 * through the set members) then the b-tree must not contain duplicates.
 * An epheremal table must be used unless the selected columns are guaranteed
 * to be unique - either because it is an INTEGER PRIMARY KEY or due to
 * a UNIQUE constraint or index.
 *
 * When IN_INDEX_MEMBERSHIP is used (and the b-tree will be used
 * for fast set membership tests) then an epheremal table must
 * be used unless <columns> is a single INTEGER PRIMARY KEY column or an
 * index can be found with the specified <columns> as its left-most.
 *
 * If the IN_INDEX_NOOP_OK and IN_INDEX_MEMBERSHIP are both set and
 * if the RHS of the IN operator is a list (not a subquery) then this
 * routine might decide that creating an ephemeral b-tree for membership
 * testing is too expensive and return IN_INDEX_NOOP.  In that case, the
 * calling routine should implement the IN operator using a sequence
 * of Eq or Ne comparison operations.
 *
 * When the b-tree is being used for membership tests, the calling function
 * might need to know whether or not the RHS side of the IN operator
 * contains a NULL.  If prRhsHasNull is not a NULL pointer and
 * if there is any chance that the (...) might contain a NULL value at
 * runtime, then a register is allocated and the register number written
 * to *prRhsHasNull. If there is no chance that the (...) contains a
 * NULL value, then *prRhsHasNull is left unchanged.
 *
 * If a register is allocated and its location stored in *prRhsHasNull, then
 * the value in that register will be NULL if the b-tree contains one or more
 * NULL values, and it will be some non-NULL value if the b-tree contains no
 * NULL values.
 *
 * If the aiMap parameter is not NULL, it must point to an array containing
 * one element for each column returned by the SELECT statement on the RHS
 * of the IN(...) operator. The i'th entry of the array is populated with the
 * offset of the index column that matches the i'th column returned by the
 * SELECT. For example, if the expression and selected index are:
 *
 *   (?,?,?) IN (SELECT a, b, c FROM t1)
 *   CREATE INDEX i1 ON t1(b, c, a);
 *
 * then aiMap[] is populated with {2, 0, 1}.
 */
int
sqlFindInIndex(Parse * pParse,	/* Parsing context */
		   Expr * pX,	/* The right-hand side (RHS) of the IN operator */
		   u32 inFlags,	/* IN_INDEX_LOOP, _MEMBERSHIP, and/or _NOOP_OK */
		   int *prRhsHasNull,	/* Register holding NULL status.  See notes */
		   int *aiMap,	/* Mapping from Index fields to RHS fields */
		   int *pSingleIdxCol	/* Tarantool. In case (nExpr == 1) it is meant by sql that
					   column of interest is always 0, since index columns appear first
					   in index. This is not the case for Tarantool, where index columns
					   don't change order of appearance.
					   So, use this field to store single column index.  */
    )
{
	Select *p;		/* SELECT to the right of IN operator */
	int eType = 0;		/* Type of RHS table. IN_INDEX_* */
	int iTab = pParse->nTab++;	/* Cursor of the RHS table */
	int mustBeUnique;	/* True if RHS must be unique */
	Vdbe *v = sqlGetVdbe(pParse);	/* Virtual machine being coded */

	assert(pX->op == TK_IN);
	mustBeUnique = (inFlags & IN_INDEX_LOOP) != 0;

	/* If the RHS of this IN(...) operator is a SELECT, and if it matters
	 * whether or not the SELECT result contains NULL values, check whether
	 * or not NULL is actually possible (it may not be, for example, due
	 * to NOT NULL constraints in the schema). If no NULL values are possible,
	 * set prRhsHasNull to 0 before continuing.
	 */
	if (prRhsHasNull && (pX->flags & EP_xIsSelect)) {
		int i;
		ExprList *pEList = pX->x.pSelect->pEList;
		for (i = 0; i < pEList->nExpr; i++) {
			if (sqlExprCanBeNull(pEList->a[i].pExpr))
				break;
		}
		if (i == pEList->nExpr) {
			prRhsHasNull = 0;
		}
	}

	/* Check to see if an existing table or index can be used to
	 * satisfy the query.  This is preferable to generating a new
	 * ephemeral table.
	 */
	if (!pParse->is_aborted && (p = isCandidateForInOpt(pX)) != 0) {
		sql *db = pParse->db;	/* Database connection */
		ExprList *pEList = p->pEList;
		int nExpr = pEList->nExpr;

		assert(p->pEList != 0);	/* Because of isCandidateForInOpt(p) */
		assert(p->pEList->a[0].pExpr != 0);	/* Because of isCandidateForInOpt(p) */
		assert(p->pSrc != 0);	/* Because of isCandidateForInOpt(p) */
		assert(v);	/* sqlGetVdbe() has always been previously called */

		bool type_is_suitable = true;
		int i;

		struct space *space = p->pSrc->a[0].space;
		/* Check that the type that will be used to perform each
		 * comparison is the same as the type of each column in table
		 * on the RHS of the IN operator.  If it not, it is not possible to
		 * use any index of the RHS table.
		 */
		for (i = 0; i < nExpr && type_is_suitable; i++) {
			Expr *pLhs = sqlVectorFieldSubexpr(pX->pLeft, i);
			int iCol = pEList->a[i].pExpr->iColumn;
			/* RHS table */
			assert(iCol >= 0);
			enum field_type idx_type = space->def->fields[iCol].type;
			enum field_type lhs_type = sql_expr_type(pLhs);
			/*
			 * Index search is possible only if types
			 * of columns match.
			 */
			if (idx_type != lhs_type)
				type_is_suitable = false;
		}

		if (type_is_suitable) {
			/*
			 * Here we need real space since further
			 * it is used in cursor opening routine.
			 */

			/* Search for an existing index that will work for this IN operator */
			for (uint32_t k = 0; k < space->index_count &&
			     eType == 0; ++k) {
				struct index *idx = space->index[k];
				Bitmask colUsed; /* Columns of the index used */
				Bitmask mCol;	/* Mask for the current column */
				uint32_t part_count =
					idx->def->key_def->part_count;
				struct key_part *parts =
					idx->def->key_def->parts;
				if ((int)part_count < nExpr)
					continue;
				/* Maximum nColumn is BMS-2, not BMS-1, so that we can compute
				 * BITMASK(nExpr) without overflowing
				 */
				testcase(part_count == BMS - 2);
				testcase(part_count == BMS - 1);
				if (part_count >= BMS - 1)
					continue;
				if (mustBeUnique &&
				    ((int)part_count > nExpr ||
				     !idx->def->opts.is_unique)) {
					/*
					 * This index is not
					 * unique over the IN RHS
					 * columns.
					 */
					continue;
				}

				colUsed = 0;	/* Columns of index used so far */
				for (i = 0; i < nExpr; i++) {
					Expr *pLhs = sqlVectorFieldSubexpr(pX->pLeft, i);
					Expr *pRhs = pEList->a[i].pExpr;
					uint32_t id;
					if (sql_binary_compare_coll_seq(pParse, pLhs, pRhs, &id) != 0)
						break;
					int j;

					for (j = 0; j < nExpr; j++) {
						if ((int) parts[j].fieldno !=
						    pRhs->iColumn)
							continue;
						if (id != parts[j].coll_id)
							continue;
						break;
					}
					if (j == nExpr)
						break;
					mCol = MASKBIT(j);
					if (mCol & colUsed)
						break;	/* Each column used only once */
					colUsed |= mCol;
					if (aiMap)
						aiMap[i] = pRhs->iColumn;
					else if (pSingleIdxCol && nExpr == 1)
						*pSingleIdxCol = pRhs->iColumn;
					}

				assert(i == nExpr
				       || colUsed != (MASKBIT(nExpr) - 1));
				if (colUsed == (MASKBIT(nExpr) - 1)) {
					/* If we reach this point, that means the index pIdx is usable */
					int iAddr = sqlVdbeAddOp0(v, OP_Once);
					VdbeCoverage(v);
					sqlVdbeAddOp4(v, OP_Explain,
							  0, 0, 0,
							  sqlMPrintf(db,
							  "USING INDEX %s FOR IN-OPERATOR",
							  idx->def->name),
							  P4_DYNAMIC);
					vdbe_emit_open_cursor(pParse, iTab,
							      idx->def->iid,
							      space);
					VdbeComment((v, "%s", idx->def->name));
					assert(IN_INDEX_INDEX_DESC ==
					       IN_INDEX_INDEX_ASC + 1);
					eType = IN_INDEX_INDEX_ASC +
						parts[0].sort_order;

					if (prRhsHasNull) {
						*prRhsHasNull = ++pParse->nMem;
						if (nExpr == 1) {
							/* Tarantool: Check for null is performed on first key of the index.  */
							sqlSetHasNullFlag(v,
									      iTab,
									      parts[0].fieldno,
									      *prRhsHasNull);
						}
					}
					sqlVdbeJumpHere(v, iAddr);
				}
			}	/* End loop over indexes */
		}
	}

	/* End attempt to optimize using an index */
	/* If no preexisting index is available for the IN clause
	 * and IN_INDEX_NOOP is an allowed reply
	 * and the RHS of the IN operator is a list, not a subquery
	 * and the RHS is not constant or has two or fewer terms,
	 * then it is not worth creating an ephemeral table to evaluate
	 * the IN operator so return IN_INDEX_NOOP.
	 */
	if (eType == 0 && (inFlags & IN_INDEX_NOOP_OK)
	    && !ExprHasProperty(pX, EP_xIsSelect)
	    && (!sqlInRhsIsConstant(pX) || pX->x.pList->nExpr <= 2)
	    ) {
		eType = IN_INDEX_NOOP;
	}

	if (eType == 0) {
		/* Could not find an existing table or index to use as the RHS b-tree.
		 * We will have to generate an ephemeral table to do the job.
		 */
		u32 savedNQueryLoop = pParse->nQueryLoop;
		int rMayHaveNull = 0;
		eType = IN_INDEX_EPH;
		if (inFlags & IN_INDEX_LOOP) {
			pParse->nQueryLoop = 0;

		} else if (prRhsHasNull) {
			*prRhsHasNull = rMayHaveNull = ++pParse->nMem;
		}
		sqlCodeSubselect(pParse, pX, rMayHaveNull);
		pParse->nQueryLoop = savedNQueryLoop;
	} else {
		pX->iTable = iTab;
	}

	if (aiMap && eType != IN_INDEX_INDEX_ASC
	    && eType != IN_INDEX_INDEX_DESC) {
		int i, n;
		n = sqlExprVectorSize(pX->pLeft);
		for (i = 0; i < n; i++)
			aiMap[i] = i;
	}
	return eType;
}

/*
 * Argument pExpr is an (?, ?...) IN(...) expression. This
 * function allocates and returns a terminated string containing
 * the types to be used for each column of the comparison.
 *
 * It is the responsibility of the caller to ensure that the returned
 * string is eventually freed using sqlDbFree().
 */
static enum field_type *
expr_in_type(Parse *pParse, Expr *pExpr)
{
	Expr *pLeft = pExpr->pLeft;
	int nVal = sqlExprVectorSize(pLeft);
	Select *pSelect = (pExpr->flags & EP_xIsSelect) ? pExpr->x.pSelect : 0;

	assert(pExpr->op == TK_IN);
	uint32_t sz = (nVal + 1) * sizeof(enum field_type);
	enum field_type *zRet = sqlDbMallocZero(pParse->db, sz);
	if (zRet) {
		int i;
		for (i = 0; i < nVal; i++) {
			Expr *pA = sqlVectorFieldSubexpr(pLeft, i);
			enum field_type lhs = sql_expr_type(pA);
			if (pSelect) {
				struct Expr *e = pSelect->pEList->a[i].pExpr;
				enum field_type rhs = sql_expr_type(e);
				zRet[i] = sql_type_result(rhs, lhs);
			} else {
				zRet[i] = lhs;
			}
		}
		zRet[nVal] = field_type_MAX;
	}
	return zRet;
}

/*
 * Generate code for scalar subqueries used as a subquery expression, EXISTS,
 * or IN operators.  Examples:
 *
 *     (SELECT a FROM b)          -- subquery
 *     EXISTS (SELECT a FROM b)   -- EXISTS subquery
 *     x IN (4,5,11)              -- IN operator with list on right-hand side
 *     x IN (SELECT a FROM b)     -- IN operator with subquery on the right
 *
 * The pExpr parameter describes the expression that contains the IN
 * operator or subquery.
 *
 * If rMayHaveNull is non-zero, that means that the operation is an IN
 * (not a SELECT or EXISTS) and that the RHS might contains NULLs.
 * All this routine does is initialize the register given by rMayHaveNull
 * to NULL.  Calling routines will take care of changing this register
 * value to non-NULL if the RHS is NULL-free.
 *
 * For a SELECT or EXISTS operator, return the register that holds the
 * result.  For a multi-column SELECT, the result is stored in a contiguous
 * array of registers and the return value is the register of the left-most
 * result column.  Return 0 for IN operators or if an error occurs.
 */
int
sqlCodeSubselect(Parse * pParse,	/* Parsing context */
		     Expr * pExpr,	/* The IN, SELECT, or EXISTS operator */
		     int rHasNullFlag	/* Register that records whether NULLs exist in RHS */
    )
{
	int jmpIfDynamic = -1;	/* One-time test address */
	int rReg = 0;		/* Register storing resulting */
	Vdbe *v = sqlGetVdbe(pParse);
	if (NEVER(v == 0))
		return 0;
	sqlExprCachePush(pParse);

	/* The evaluation of the IN/EXISTS/SELECT must be repeated every time it
	 * is encountered if any of the following is true:
	 *
	 *    *  The right-hand side is a correlated subquery
	 *    *  The right-hand side is an expression list containing variables
	 *    *  We are inside a trigger
	 *
	 * If all of the above are false, then we can run this code just once
	 * save the results, and reuse the same result on subsequent invocations.
	 */
	if (!ExprHasProperty(pExpr, EP_VarSelect)) {
		jmpIfDynamic = sqlVdbeAddOp0(v, OP_Once);
		VdbeCoverage(v);
	}
	if (pParse->explain == 2) {
		char *zMsg =
		    sqlMPrintf(pParse->db, "EXECUTE %s%s SUBQUERY %d",
				   jmpIfDynamic >= 0 ? "" : "CORRELATED ",
				   pExpr->op == TK_IN ? "LIST" : "SCALAR",
				   pParse->iNextSelectId);
		sqlVdbeAddOp4(v, OP_Explain, pParse->iSelectId, 0, 0, zMsg,
				  P4_DYNAMIC);
	}

	switch (pExpr->op) {
	case TK_IN:{
			int addr;	/* Address of OP_OpenEphemeral instruction */
			Expr *pLeft = pExpr->pLeft;	/* the LHS of the IN operator */
			int nVal;	/* Size of vector pLeft */

			nVal = sqlExprVectorSize(pLeft);

			/* Whether this is an 'x IN(SELECT...)' or an 'x IN(<exprlist>)'
			 * expression it is handled the same way.  An ephemeral table is
			 * filled with index keys representing the results from the
			 * SELECT or the <exprlist>.
			 *
			 * If the 'x' expression is a column value, or the SELECT...
			 * statement returns a column value, then the type of that
			 * column is used to build the index keys. If both 'x' and the
			 * SELECT... statement are columns, then NUMBER type is used
			 * if either column has NUMBER or INTEGER type. If neither
			 * 'x' nor the SELECT... statement are columns,
			 * then NUMBER type is used.
			 */
			pExpr->iTable = pParse->nTab++;
			int reg_eph = ++pParse->nMem;
			addr = sqlVdbeAddOp2(v, OP_OpenTEphemeral,
						 reg_eph, nVal);
			sqlVdbeAddOp3(v, OP_IteratorOpen, pExpr->iTable, 0,
					  reg_eph);
			struct sql_key_info *key_info = sql_key_info_new(pParse->db, nVal);
			if (key_info == NULL)
				return 0;

			if (ExprHasProperty(pExpr, EP_xIsSelect)) {
				/* Case 1:     expr IN (SELECT ...)
				 *
				 * Generate code to write the results of the select into the temporary
				 * table allocated and opened above.
				 */
				Select *pSelect = pExpr->x.pSelect;
				ExprList *pEList = pSelect->pEList;

				/* If the LHS and RHS of the IN operator do not match, that
				 * error will have been caught long before we reach this point.
				 */
				if (ALWAYS(pEList->nExpr == nVal)) {
					SelectDest dest;
					int i;
					sqlSelectDestInit(&dest, SRT_Set,
							      pExpr->iTable, reg_eph);
					dest.dest_type =
						expr_in_type(pParse, pExpr);
					assert((pExpr->iTable & 0x0000FFFF) ==
					       pExpr->iTable);
					pSelect->iLimit = 0;
					testcase(pSelect->
						 selFlags & SF_Distinct);
					if (sqlSelect
					    (pParse, pSelect, &dest)) {
						sqlDbFree(pParse->db,
							      dest.dest_type);
						sql_key_info_unref(key_info);
						return 0;
					}
					sqlDbFree(pParse->db,
						      dest.dest_type);
					assert(pEList != 0);
					assert(pEList->nExpr > 0);
					for (i = 0; i < nVal; i++) {
						Expr *p =
						    sqlVectorFieldSubexpr
						    (pLeft, i);
						if (sql_binary_compare_coll_seq(pParse, p, pEList->a[i].pExpr,
										&key_info->parts[i].coll_id) != 0)
							return 0;
					}
				}
			} else if (ALWAYS(pExpr->x.pList != 0)) {
				/* Case 2:     expr IN (exprlist)
				 *
				 * For each expression, build an index key from the evaluation and
				 * store it in the temporary table. If <expr> is a column, then use
				 * that columns types when building index keys. If <expr> is not
				 * a column, use NUMBER type.
				 */
				int i;
				ExprList *pList = pExpr->x.pList;
				struct ExprList_item *pItem;
				int r1, r2, r3;

				enum field_type lhs_type =
					sql_expr_type(pLeft);
				bool unused;
				struct coll *unused_coll;
				if (sql_expr_coll(pParse, pExpr->pLeft, &unused,
						  &key_info->parts[0].coll_id,
						  &unused_coll) != 0)
					return 0;

				/* Loop through each expression in <exprlist>. */
				r1 = sqlGetTempReg(pParse);
				r2 = sqlGetTempReg(pParse);

				for (i = pList->nExpr, pItem = pList->a; i > 0;
				     i--, pItem++) {
					Expr *pE2 = pItem->pExpr;
					/* If the expression is not constant then we will need to
					 * disable the test that was generated above that makes sure
					 * this code only executes once.  Because for a non-constant
					 * expression we need to rerun this code each time.
					 */
					if (jmpIfDynamic >= 0
					    && !sqlExprIsConstant(pE2)) {
						sqlVdbeChangeToNoop(v, jmpIfDynamic);
						jmpIfDynamic = -1;
					}
					r3 = sqlExprCodeTarget(pParse, pE2, r1);
					enum field_type types[2] =
						{ lhs_type, field_type_MAX };
	 				sqlVdbeAddOp4(v, OP_MakeRecord, r3,
							  1, r2, (char *)types,
							  sizeof(types));
					sql_expr_type_cache_change(pParse,
								   r3, 1);
					sqlVdbeAddOp2(v, OP_IdxInsert, r2,
							  reg_eph);
				}
				sqlReleaseTempReg(pParse, r1);
				sqlReleaseTempReg(pParse, r2);
			}
			sqlVdbeChangeP4(v, addr, (void *)key_info,
					    P4_KEYINFO);
			break;
		}

	case TK_EXISTS:
	case TK_SELECT:
	default:{
			/* Case 3:    (SELECT ... FROM ...)
			 *     or:    EXISTS(SELECT ... FROM ...)
			 *
			 * For a SELECT, generate code to put the values for all columns of
			 * the first row into an array of registers and return the index of
			 * the first register.
			 *
			 * If this is an EXISTS, write an integer 0 (not exists) or 1 (exists)
			 * into a register and return that register number.
			 *
			 * In both cases, the query is augmented with "LIMIT 1".  Any
			 * preexisting limit is discarded in place of the new LIMIT 1.
			 */
			Select *pSel;	/* SELECT statement to encode */
			SelectDest dest;	/* How to deal with SELECT result */
			int nReg;	/* Registers to allocate */

			testcase(pExpr->op == TK_EXISTS);
			testcase(pExpr->op == TK_SELECT);
			assert(pExpr->op == TK_EXISTS
			       || pExpr->op == TK_SELECT);
			assert(ExprHasProperty(pExpr, EP_xIsSelect));

			pSel = pExpr->x.pSelect;
			nReg = pExpr->op == TK_SELECT ? pSel->pEList->nExpr : 1;
			sqlSelectDestInit(&dest, 0, pParse->nMem + 1, -1);
			pParse->nMem += nReg;
			if (pExpr->op == TK_SELECT) {
				dest.eDest = SRT_Mem;
				dest.iSdst = dest.iSDParm;
				dest.nSdst = nReg;
				sqlVdbeAddOp3(v, OP_Null, 0, dest.iSDParm,
						  dest.iSDParm + nReg - 1);
				VdbeComment((v, "Init subquery result"));
			} else {
				dest.eDest = SRT_Exists;
				sqlVdbeAddOp2(v, OP_Bool, false, dest.iSDParm);
				VdbeComment((v, "Init EXISTS result"));
			}
			if (pSel->pLimit == NULL) {
				pSel->pLimit =
					sql_expr_new(pParse->db, TK_INTEGER,
						     &sqlIntTokens[1]);
				if (pSel->pLimit == NULL) {
					pParse->is_aborted = true;
				} else {
					ExprSetProperty(pSel->pLimit,
							EP_System);
				}
			}
			pSel->selFlags |= SF_SingleRow;
			pSel->iLimit = 0;
			pSel->selFlags &= ~SF_MultiValue;
			if (sqlSelect(pParse, pSel, &dest)) {
				return 0;
			}
			rReg = dest.iSDParm;
			ExprSetVVAProperty(pExpr, EP_NoReduce);
			break;
		}
	}

	if (rHasNullFlag) {
		sqlSetHasNullFlag(v, pExpr->iTable, 0, rHasNullFlag);
	}

	if (jmpIfDynamic >= 0) {
		sqlVdbeJumpHere(v, jmpIfDynamic);
	}
	sqlExprCachePop(pParse);

	return rReg;
}

/*
 * Expr pIn is an IN(...) expression. This function checks that the
 * sub-select on the RHS of the IN() operator has the same number of
 * columns as the vector on the LHS. Or, if the RHS of the IN() is not
 * a sub-query, that the LHS is a vector of size 1.
 */
int
sqlExprCheckIN(Parse * pParse, Expr * pIn)
{
	int nVector = sqlExprVectorSize(pIn->pLeft);
	if ((pIn->flags & EP_xIsSelect)) {
		if (nVector != pIn->x.pSelect->pEList->nExpr) {
			int expr_count = pIn->x.pSelect->pEList->nExpr;
			diag_set(ClientError, ER_SQL_COLUMN_COUNT, nVector,
				 expr_count);
			pParse->is_aborted = true;
			return 1;
		}
	} else if (nVector != 1) {
		diag_set(ClientError, ER_SQL_COLUMN_COUNT, nVector, 1);
		pParse->is_aborted = true;
		return 1;
	}
	return 0;
}

/*
 * Generate code for an IN expression.
 *
 *      x IN (SELECT ...)
 *      x IN (value, value, ...)
 *
 * The left-hand side (LHS) is a scalar or vector expression.  The
 * right-hand side (RHS) is an array of zero or more scalar values, or a
 * subquery.  If the RHS is a subquery, the number of result columns must
 * match the number of columns in the vector on the LHS.  If the RHS is
 * a list of values, the LHS must be a scalar.
 *
 * The IN operator is true if the LHS value is contained within the RHS.
 * The result is false if the LHS is definitely not in the RHS.  The
 * result is NULL if the presence of the LHS in the RHS cannot be
 * determined due to NULLs.
 *
 * This routine generates code that jumps to destIfFalse if the LHS is not
 * contained within the RHS.  If due to NULLs we cannot determine if the LHS
 * is contained in the RHS then jump to destIfNull.  If the LHS is contained
 * within the RHS then fall through.
 *
 * See the separate in-operator.md documentation file in the canonical
 * sql source tree for additional information.
 */
static void
sqlExprCodeIN(Parse * pParse,	/* Parsing and code generating context */
		  Expr * pExpr,	/* The IN expression */
		  int destIfFalse,	/* Jump here if LHS is not contained in the RHS */
		  int destIfNull	/* Jump here if the results are unknown due to NULLs */
    )
{
	int rRhsHasNull = 0;	/* Register that is true if RHS contains NULL values */
	int eType;		/* Type of the RHS */
	int rLhs;		/* Register(s) holding the LHS values */
	int rLhsOrig;		/* LHS values prior to reordering by aiMap[] */
	Vdbe *v;		/* Statement under construction */
	int *aiMap = 0;		/* Map from vector field to index column */
	int nVector;		/* Size of vectors for this IN operator */
	int iDummy;		/* Dummy parameter to exprCodeVector() */
	Expr *pLeft;		/* The LHS of the IN operator */
	int i;			/* loop counter */
	int destStep2;		/* Where to jump when NULLs seen in step 2 */
	int destStep6 = 0;	/* Start of code for Step 6 */
	int addrTruthOp;	/* Address of opcode that determines the IN is true */
	int destNotNull;	/* Jump here if a comparison is not true in step 6 */
	int addrTop;		/* Top of the step-6 loop */

	pLeft = pExpr->pLeft;
	if (sqlExprCheckIN(pParse, pExpr))
		return;
	/* Type sequence for comparisons. */
	enum field_type *zAff = expr_in_type(pParse, pExpr);
	nVector = sqlExprVectorSize(pExpr->pLeft);
	aiMap =
	    (int *)sqlDbMallocZero(pParse->db,
				       nVector * (sizeof(int) + sizeof(char)) +
				       1);
	if (pParse->db->mallocFailed)
		goto sqlExprCodeIN_oom_error;

	/* Attempt to compute the RHS. After this step, if anything other than
	 * IN_INDEX_NOOP is returned, the table opened ith cursor pExpr->iTable
	 * contains the values that make up the RHS. If IN_INDEX_NOOP is returned,
	 * the RHS has not yet been coded.
	 */
	v = pParse->pVdbe;
	assert(v != 0);		/* OOM detected prior to this routine */
	VdbeNoopComment((v, "begin IN expr"));
	eType = sqlFindInIndex(pParse, pExpr,
				   IN_INDEX_MEMBERSHIP | IN_INDEX_NOOP_OK,
				   destIfFalse == destIfNull ? 0 : &rRhsHasNull,
				   aiMap, 0);

	assert(pParse->is_aborted || nVector == 1 || eType == IN_INDEX_EPH
	       || eType == IN_INDEX_INDEX_ASC || eType == IN_INDEX_INDEX_DESC);

	/* Code the LHS, the <expr> from "<expr> IN (...)". If the LHS is a
	 * vector, then it is stored in an array of nVector registers starting
	 * at r1.
	 *
	 * sqlFindInIndex() might have reordered the fields of the LHS vector
	 * so that the fields are in the same order as an existing index.   The
	 * aiMap[] array contains a mapping from the original LHS field order to
	 * the field order that matches the RHS index.
	 */
	sqlExprCachePush(pParse);
	rLhsOrig = exprCodeVector(pParse, pLeft, &iDummy);
	/* Tarantoool: Order is always preserved.  */
	rLhs = rLhsOrig;

	/* If sqlFindInIndex() did not find or create an index that is
	 * suitable for evaluating the IN operator, then evaluate using a
	 * sequence of comparisons.
	 *
	 * This is step (1) in the in-operator.md optimized algorithm.
	 */
	if (eType == IN_INDEX_NOOP) {
		bool unused;
		uint32_t id;
		ExprList *pList = pExpr->x.pList;
		struct coll *coll;
		if (sql_expr_coll(pParse, pExpr->pLeft, &unused, &id,
				  &coll) != 0)
			goto sqlExprCodeIN_finished;
		int labelOk = sqlVdbeMakeLabel(v);
		int r2, regToFree;
		int regCkNull = 0;
		int ii;
		assert(!ExprHasProperty(pExpr, EP_xIsSelect));
		if (destIfNull != destIfFalse) {
			regCkNull = sqlGetTempReg(pParse);
			sqlVdbeAddOp2(v, OP_Integer, 0, regCkNull);
			int lCheckNull = sqlVdbeMakeLabel(v);
			sqlVdbeAddOp2(v, OP_NotNull, rLhs, lCheckNull);
			sqlVdbeAddOp2(v, OP_Null, 0, regCkNull);
			sqlVdbeResolveLabel(v, lCheckNull);
		}
		for (ii = 0; ii < pList->nExpr; ii++) {
			r2 = sqlExprCodeTemp(pParse, pList->a[ii].pExpr,
						 &regToFree);
			if (regCkNull
			    && sqlExprCanBeNull(pList->a[ii].pExpr)) {
				int lCheckNull = sqlVdbeMakeLabel(v);
				sqlVdbeAddOp2(v, OP_NotNull, r2, lCheckNull);
				sqlVdbeAddOp2(v, OP_Null, 0, regCkNull);
				sqlVdbeResolveLabel(v, lCheckNull);
			}
			if (ii < pList->nExpr - 1 || destIfNull != destIfFalse) {
				sqlVdbeAddOp4(v, OP_Eq, rLhs, labelOk, r2,
						  (void *)coll, P4_COLLSEQ);
				VdbeCoverageIf(v, ii < pList->nExpr - 1);
				VdbeCoverageIf(v, ii == pList->nExpr - 1);
				sqlVdbeChangeP5(v, zAff[0]);
			} else {
				assert(destIfNull == destIfFalse);
				sqlVdbeAddOp4(v, OP_Ne, rLhs, destIfFalse,
						  r2, (void *)coll,
						  P4_COLLSEQ);
				VdbeCoverage(v);
				sqlVdbeChangeP5(v,
						    zAff[0] |
						    SQL_JUMPIFNULL);
			}
			sqlReleaseTempReg(pParse, regToFree);
		}
		if (regCkNull) {
			sqlVdbeAddOp2(v, OP_IsNull, regCkNull, destIfNull);
			VdbeCoverage(v);
			sqlVdbeGoto(v, destIfFalse);
		}
		sqlVdbeResolveLabel(v, labelOk);
		sqlReleaseTempReg(pParse, regCkNull);
		goto sqlExprCodeIN_finished;
	}

	/* Step 2: Check to see if the LHS contains any NULL columns.  If the
	 * LHS does contain NULLs then the result must be either FALSE or NULL.
	 * We will then skip the binary search of the RHS.
	 */
	if (destIfNull == destIfFalse) {
		destStep2 = destIfFalse;
	} else {
		destStep2 = destStep6 = sqlVdbeMakeLabel(v);
	}
	for (i = 0; i < nVector; i++) {
		Expr *p = sqlVectorFieldSubexpr(pExpr->pLeft, i);
		if (sqlExprCanBeNull(p)) {
			sqlVdbeAddOp2(v, OP_IsNull, rLhs + i, destStep2);
			VdbeCoverage(v);
		}
	}

	/* Step 3.  The LHS is now known to be non-NULL.  Do the binary search
	 * of the RHS using the LHS as a probe.  If found, the result is
	 * true.
	 */
	zAff[nVector] = field_type_MAX;
	sqlVdbeAddOp4(v, OP_ApplyType, rLhs, nVector, 0, (char*)zAff,
			  P4_DYNAMIC);
	/*
	 * zAff will be freed at the end of VDBE execution, since
	 * it was passed with P4_DYNAMIC flag.
	 */
	zAff = NULL;
	if (destIfFalse == destIfNull) {
		/* Combine Step 3 and Step 5 into a single opcode */
		sqlVdbeAddOp4Int(v, OP_NotFound, pExpr->iTable,
				     destIfFalse, rLhs, nVector);
		VdbeCoverage(v);
		goto sqlExprCodeIN_finished;
	}
	/* Ordinary Step 3, for the case where FALSE and NULL are distinct */
	addrTruthOp =
		sqlVdbeAddOp4Int(v, OP_Found, pExpr->iTable, 0, rLhs,
				     nVector);
	VdbeCoverage(v);

	/* Step 4.  If the RHS is known to be non-NULL and we did not find
	 * an match on the search above, then the result must be FALSE.
	 */
	if (rRhsHasNull && nVector == 1) {
		sqlVdbeAddOp2(v, OP_NotNull, rRhsHasNull, destIfFalse);
		VdbeCoverage(v);
	}

	/* Step 5.  If we do not care about the difference between NULL and
	 * FALSE, then just return false.
	 */
	if (destIfFalse == destIfNull)
		sqlVdbeGoto(v, destIfFalse);

	/* Step 6: Loop through rows of the RHS.  Compare each row to the LHS.
	 * If any comparison is NULL, then the result is NULL.  If all
	 * comparisons are FALSE then the final result is FALSE.
	 *
	 * For a scalar LHS, it is sufficient to check just the first row
	 * of the RHS.
	 */
	if (destStep6)
		sqlVdbeResolveLabel(v, destStep6);
	addrTop = sqlVdbeAddOp2(v, OP_Rewind, pExpr->iTable, destIfFalse);
	VdbeCoverage(v);
	if (nVector > 1) {
		destNotNull = sqlVdbeMakeLabel(v);
	} else {
		/* For nVector==1, combine steps 6 and 7 by immediately returning
		 * FALSE if the first comparison is not NULL
		 */
		destNotNull = destIfFalse;
	}
	for (i = 0; i < nVector; i++) {
		bool unused;
		uint32_t id;
		int r3 = sqlGetTempReg(pParse);
		Expr *p = sqlVectorFieldSubexpr(pLeft, i);
		struct coll *pColl;
		if (sql_expr_coll(pParse, p, &unused, &id, &pColl) != 0)
			goto sqlExprCodeIN_finished;
		/* Tarantool: Replace i -> aiMap [i], since original order of columns
		 * is preserved.
		 */
		sqlVdbeAddOp3(v, OP_Column, pExpr->iTable, aiMap[i], r3);
		sqlVdbeAddOp4(v, OP_Ne, rLhs + i, destNotNull, r3,
				  (void *)pColl, P4_COLLSEQ);
		VdbeCoverage(v);
		sqlReleaseTempReg(pParse, r3);
	}
	sqlVdbeAddOp2(v, OP_Goto, 0, destIfNull);
	if (nVector > 1) {
		sqlVdbeResolveLabel(v, destNotNull);
		sqlVdbeAddOp2(v, OP_Next, pExpr->iTable, addrTop + 1);
		VdbeCoverage(v);

		/* Step 7:  If we reach this point, we know that the result must
		 * be false.
		 */
		sqlVdbeAddOp2(v, OP_Goto, 0, destIfFalse);
	}

	/* Jumps here in order to return true. */
	sqlVdbeJumpHere(v, addrTruthOp);

 sqlExprCodeIN_finished:
	if (rLhs != rLhsOrig)
		sqlReleaseTempReg(pParse, rLhs);
	sqlExprCachePop(pParse);
	VdbeComment((v, "end IN expr"));
 sqlExprCodeIN_oom_error:
	sqlDbFree(pParse->db, aiMap);
	sqlDbFree(pParse->db, zAff);
}

/*
 * Generate an instruction that will put the floating point
 * value described by z[0..n-1] into register iMem.
 *
 * The z[] string will probably not be zero-terminated.  But the
 * z[n] character is guaranteed to be something that does not look
 * like the continuation of the number.
 */
static void
codeReal(Vdbe * v, const char *z, int negateFlag, int iMem)
{
	if (ALWAYS(z != 0)) {
		double value;
		sqlAtoF(z, &value, sqlStrlen30(z));
		assert(!sqlIsNaN(value));	/* The new AtoF never returns NaN */
		if (negateFlag)
			value = -value;
		sqlVdbeAddOp4Dup8(v, OP_Real, 0, iMem, 0, (u8 *) & value,
				      P4_REAL);
	}
}

/**
 * Generate an instruction that will put the integer describe by
 * text z[0..n-1] into register iMem.
 *
 * @param parse Parsing context.
 * @param expr Expression being parsed. Expr.u.zToken is always
 *             UTF8 and zero-terminated.
 * @param neg_flag True if value is negative.
 * @param mem Register to store parsed integer
 */
static void
expr_code_int(struct Parse *parse, struct Expr *expr, bool is_neg,
	      int mem)
{
	struct Vdbe *v = parse->pVdbe;
	if (expr->flags & EP_IntValue) {
		int i = expr->u.iValue;
		assert(i >= 0);
		if (is_neg)
			i = -i;
		sqlVdbeAddOp2(v, OP_Integer, i, mem);
		return;
	}
	int64_t value;
	const char *z = expr->u.zToken;
	assert(z != NULL);
	const char *sign = is_neg ? "-" : "";
	if (z[0] == '0' && (z[1] == 'x' || z[1] == 'X')) {
		errno = 0;
		if (is_neg) {
			value = strtoll(z, NULL, 16);
		} else {
			value = strtoull(z, NULL, 16);
			if (value > INT64_MAX)
				goto int_overflow;
		}
		if (errno != 0) {
			diag_set(ClientError, ER_HEX_LITERAL_MAX, sign, z,
				 strlen(z) - 2, 16);
			parse->is_aborted = true;
			return;
		}
	} else {
		size_t len = strlen(z);
		bool unused;
		if (sql_atoi64(z, &value, &unused, len) != 0 ||
		    (is_neg && (uint64_t) value > (uint64_t) INT64_MAX + 1)) {
int_overflow:
			diag_set(ClientError, ER_INT_LITERAL_MAX, sign, z);
			parse->is_aborted = true;
			return;
		}
	}
	if (is_neg)
		value = -value;
	sqlVdbeAddOp4Dup8(v, OP_Int64, 0, mem, 0, (u8 *) &value,
			  is_neg ? P4_INT64 : P4_UINT64);
}

/*
 * Erase column-cache entry number i
 */
static void
cacheEntryClear(Parse * pParse, int i)
{
	if (pParse->aColCache[i].tempReg) {
		if (pParse->nTempReg < ArraySize(pParse->aTempReg)) {
			pParse->aTempReg[pParse->nTempReg++] =
			    pParse->aColCache[i].iReg;
		}
	}
	pParse->nColCache--;
	if (i < pParse->nColCache) {
		pParse->aColCache[i] = pParse->aColCache[pParse->nColCache];
	}
}

/*
 * Record in the column cache that a particular column from a
 * particular table is stored in a particular register.
 */
void
sqlExprCacheStore(Parse * pParse, int iTab, int iCol, int iReg)
{
	int i;
	int minLru;
	int idxLru;
	struct yColCache *p;

	/* Unless an error has occurred, register numbers are always positive. */
	assert(iReg > 0 || pParse->is_aborted || pParse->db->mallocFailed);
	assert(iCol >= -1 && iCol < 32768);	/* Finite column numbers */

	/* The SQL_ColumnCache flag disables the column cache.  This is used
	 * for testing only - to verify that sql always gets the same answer
	 * with and without the column cache.
	 */
	if (OptimizationDisabled(pParse->db, SQL_ColumnCache))
		return;

	/* First replace any existing entry.
	 *
	 * Actually, the way the column cache is currently used, we are guaranteed
	 * that the object will never already be in cache.  Verify this guarantee.
	 */
#ifndef NDEBUG
	for (i = 0, p = pParse->aColCache; i < pParse->nColCache; i++, p++) {
		assert(p->iTable != iTab || p->iColumn != iCol);
	}
#endif

	/* If the cache is already full, delete the least recently used entry */
	if (pParse->nColCache >= SQL_N_COLCACHE) {
		minLru = 0x7fffffff;
		idxLru = -1;
		for (i = 0, p = pParse->aColCache; i < SQL_N_COLCACHE;
		     i++, p++) {
			if (p->lru < minLru) {
				idxLru = i;
				minLru = p->lru;
			}
		}
		p = &pParse->aColCache[idxLru];
	} else {
		p = &pParse->aColCache[pParse->nColCache++];
	}

	/* Add the new entry to the end of the cache */
	p->iLevel = pParse->iCacheLevel;
	p->iTable = iTab;
	p->iColumn = iCol;
	p->iReg = iReg;
	p->tempReg = 0;
	p->lru = pParse->iCacheCnt++;
}

/*
 * Indicate that registers between iReg..iReg+nReg-1 are being overwritten.
 * Purge the range of registers from the column cache.
 */
void
sqlExprCacheRemove(Parse * pParse, int iReg, int nReg)
{
	int i = 0;
	while (i < pParse->nColCache) {
		struct yColCache *p = &pParse->aColCache[i];
		if (p->iReg >= iReg && p->iReg < iReg + nReg) {
			cacheEntryClear(pParse, i);
		} else {
			i++;
		}
	}
}

/*
 * Remember the current column cache context.  Any new entries added
 * added to the column cache after this call are removed when the
 * corresponding pop occurs.
 */
void
sqlExprCachePush(Parse * pParse)
{
	struct session MAYBE_UNUSED *user_session;
	pParse->iCacheLevel++;
}

/*
 * Remove from the column cache any entries that were added since the
 * the previous sqlExprCachePush operation.  In other words, restore
 * the cache to the state it was in prior the most recent Push.
 */
void
sqlExprCachePop(Parse * pParse)
{
	int i = 0;
	struct session *user_session MAYBE_UNUSED;
	user_session = current_session();
	assert(pParse->iCacheLevel >= 1);
	pParse->iCacheLevel--;
	while (i < pParse->nColCache) {
		if (pParse->aColCache[i].iLevel > pParse->iCacheLevel) {
			cacheEntryClear(pParse, i);
		} else {
			i++;
		}
	}
}

/*
 * When a cached column is reused, make sure that its register is
 * no longer available as a temp register.  ticket #3879:  that same
 * register might be in the cache in multiple places, so be sure to
 * get them all.
 */
static void
sqlExprCachePinRegister(Parse * pParse, int iReg)
{
	int i;
	struct yColCache *p;
	for (i = 0, p = pParse->aColCache; i < pParse->nColCache; i++, p++) {
		if (p->iReg == iReg) {
			p->tempReg = 0;
		}
	}
}

int
sqlExprCodeGetColumn(Parse *pParse, int iColumn, int iTable, int iReg, u8 p5)
{
	Vdbe *v = pParse->pVdbe;
	int i;
	struct yColCache *p;
	assert(iColumn >= 0);

	for (i = 0, p = pParse->aColCache; i < pParse->nColCache; i++, p++) {
		if (p->iTable == iTable && p->iColumn == iColumn) {
			p->lru = pParse->iCacheCnt++;
			sqlExprCachePinRegister(pParse, p->iReg);
			return p->iReg;
		}
	}
	assert(v != 0);
	sqlVdbeAddOp3(v, OP_Column, iTable, iColumn, iReg);
	if (p5) {
		sqlVdbeChangeP5(v, p5);
	} else {
		sqlExprCacheStore(pParse, iTable, iColumn, iReg);
	}
	return iReg;
}

void
sqlExprCodeGetColumnToReg(Parse * pParse, int iColumn, int iTable, int iReg)
{
	int r1 =
		sqlExprCodeGetColumn(pParse, iColumn, iTable, iReg, 0);
	if (r1 != iReg)
		sqlVdbeAddOp2(pParse->pVdbe, OP_SCopy, r1, iReg);
}

/*
 * Clear all column cache entries.
 */
void
sqlExprCacheClear(Parse * pParse)
{
	int i;
	struct session MAYBE_UNUSED *user_session;
	user_session = current_session();

	for (i = 0; i < pParse->nColCache; i++) {
		if (pParse->aColCache[i].tempReg
		    && pParse->nTempReg < ArraySize(pParse->aTempReg)
		    ) {
			pParse->aTempReg[pParse->nTempReg++] =
			    pParse->aColCache[i].iReg;
		}
	}
	pParse->nColCache = 0;
}

/*
 * Record the fact that an type change has occurred on iCount
 * registers starting with iStart.
 */
void
sql_expr_type_cache_change(Parse *pParse, int iStart, int iCount)
{
	sqlExprCacheRemove(pParse, iStart, iCount);
}

/*
 * Generate code to move content from registers iFrom...iFrom+nReg-1
 * over to iTo..iTo+nReg-1. Keep the column cache up-to-date.
 */
void
sqlExprCodeMove(Parse * pParse, int iFrom, int iTo, int nReg)
{
	assert(iFrom >= iTo + nReg || iFrom + nReg <= iTo);
	sqlVdbeAddOp3(pParse->pVdbe, OP_Move, iFrom, iTo, nReg);
	sqlExprCacheRemove(pParse, iFrom, nReg);
}

#if defined(SQL_DEBUG)
/*
 * Return true if any register in the range iFrom..iTo (inclusive)
 * is used as part of the column cache.
 *
 * This routine is used within assert() and testcase() macros only
 * and does not appear in a normal build.
 */
static int
usedAsColumnCache(Parse * pParse, int iFrom, int iTo)
{
	int i;
	struct yColCache *p;
	for (i = 0, p = pParse->aColCache; i < pParse->nColCache; i++, p++) {
		int r = p->iReg;
		if (r >= iFrom && r <= iTo)
			return 1;	/*NO_TEST */
	}
	return 0;
}
#endif				/* SQL_DEBUG */

/*
 * Convert a scalar expression node to a TK_REGISTER referencing
 * register iReg.  The caller must ensure that iReg already contains
 * the correct value for the expression.
 */
static void
exprToRegister(Expr * p, int iReg)
{
	p->op2 = p->op;
	p->op = TK_REGISTER;
	p->iTable = iReg;
	ExprClearProperty(p, EP_Skip);
}

/*
 * Evaluate an expression (either a vector or a scalar expression) and store
 * the result in continguous temporary registers.  Return the index of
 * the first register used to store the result.
 *
 * If the returned result register is a temporary scalar, then also write
 * that register number into *piFreeable.  If the returned result register
 * is not a temporary or if the expression is a vector set *piFreeable
 * to 0.
 */
static int
exprCodeVector(Parse * pParse, Expr * p, int *piFreeable)
{
	int iResult;
	int nResult = sqlExprVectorSize(p);
	if (nResult == 1) {
		iResult = sqlExprCodeTemp(pParse, p, piFreeable);
	} else {
		*piFreeable = 0;
		if (p->op == TK_SELECT) {
			iResult = sqlCodeSubselect(pParse, p, 0);
		} else {
			int i;
			iResult = pParse->nMem + 1;
			pParse->nMem += nResult;
			for (i = 0; i < nResult; i++) {
				sqlExprCodeFactorable(pParse,
							  p->x.pList->a[i].
							  pExpr, i + iResult);
			}
		}
	}
	return iResult;
}

/*
 * Generate code into the current Vdbe to evaluate the given
 * expression.  Attempt to store the results in register "target".
 * Return the register where results are stored.
 *
 * With this routine, there is no guarantee that results will
 * be stored in target.  The result might be stored in some other
 * register if it is convenient to do so.  The calling function
 * must check the return code and move the results to the desired
 * register.
 */
int
sqlExprCodeTarget(Parse * pParse, Expr * pExpr, int target)
{
	Vdbe *v = pParse->pVdbe;	/* The VM under construction */
	int op;			/* The opcode being coded */
	int inReg = target;	/* Results stored in register inReg */
	int regFree1 = 0;	/* If non-zero free this temporary register */
	int regFree2 = 0;	/* If non-zero free this temporary register */
	int r1, r2;		/* Various register numbers */
	Expr tempX;		/* Temporary expression node */

	assert(target > 0 && target <= pParse->nMem);
	if (v == 0) {
		assert(pParse->db->mallocFailed);
		return 0;
	}

	if (pExpr == 0) {
		op = TK_NULL;
	} else {
		op = pExpr->op;
	}
	switch (op) {
	case TK_AGG_COLUMN:{
			AggInfo *pAggInfo = pExpr->pAggInfo;
			struct AggInfo_col *pCol = &pAggInfo->aCol[pExpr->iAgg];
			if (!pAggInfo->directMode) {
				assert(pCol->iMem > 0);
				return pCol->iMem;
			} else if (pAggInfo->useSortingIdx) {
				sqlVdbeAddOp3(v, OP_Column,
						  pAggInfo->sortingIdxPTab,
						  pCol->iSorterColumn, target);
				return target;
			}
			/*
			 * Otherwise, fall thru into the
			 * TK_COLUMN_REF case.
			 */
			FALLTHROUGH;
		}
	case TK_COLUMN_REF:{
			int iTab = pExpr->iTable;
			int col = pExpr->iColumn;
			if (iTab < 0) {
				if (pParse->vdbe_field_ref_reg > 0) {
					/*
					 * Generating CHECK
					 * constraints.
					 */
					assert(iTab < 0);
					sqlVdbeAddOp3(v, OP_Fetch,
						      pParse->vdbe_field_ref_reg,
						      col, target);
					return target;
				} else {
					/* Coding an expression that is part of an index where column names
					 * in the index refer to the table to which the index belongs
					 */
					iTab = pParse->iSelfTab;
				}
			}
			return sqlExprCodeGetColumn(pParse, col, iTab, target,
						    pExpr->op2);
		}
	case TK_INTEGER:{
			expr_code_int(pParse, pExpr, false, target);
			return target;
		}
	case TK_TRUE:
	case TK_FALSE: {
			sqlVdbeAddOp2(v, OP_Bool, op == TK_TRUE, target);
			return target;
		}
	case TK_FLOAT:{
			assert(!ExprHasProperty(pExpr, EP_IntValue));
			codeReal(v, pExpr->u.zToken, 0, target);
			return target;
		}
	case TK_STRING:{
			assert(!ExprHasProperty(pExpr, EP_IntValue));
			sqlVdbeLoadString(v, target, pExpr->u.zToken);
			return target;
		}
	case TK_NULL:{
			sqlVdbeAddOp2(v, OP_Null, 0, target);
			return target;
		}
#ifndef SQL_OMIT_BLOB_LITERAL
	case TK_BLOB:{
			int n;
			const char *z;
			char *zBlob;
			assert(!ExprHasProperty(pExpr, EP_IntValue));
			assert(pExpr->u.zToken[0] == 'x'
			       || pExpr->u.zToken[0] == 'X');
			assert(pExpr->u.zToken[1] == '\'');
			z = &pExpr->u.zToken[2];
			n = sqlStrlen30(z) - 1;
			assert(z[n] == '\'');
			zBlob = sqlHexToBlob(sqlVdbeDb(v), z, n);
			sqlVdbeAddOp4(v, OP_Blob, n / 2, target, 0, zBlob,
					  P4_DYNAMIC);
			return target;
		}
#endif
	case TK_VARIABLE:{
			assert(!ExprHasProperty(pExpr, EP_IntValue));
			assert(pExpr->u.zToken != 0);
			assert(pExpr->u.zToken[0] != 0);
			sqlVdbeAddOp2(v, OP_Variable, pExpr->iColumn,
					  target);
			if (pExpr->u.zToken[1] != 0) {
				const char *z =
				    sqlVListNumToName(pParse->pVList,
							  pExpr->iColumn);
				assert(pExpr->u.zToken[0] == '$'
				       || strcmp(pExpr->u.zToken, z) == 0);
				pParse->pVList[0] = 0;	/* Indicate VList may no longer be enlarged */
				sqlVdbeAppendP4(v, (char *)z, P4_STATIC);
			}
			return target;
		}
	case TK_REGISTER:{
			return pExpr->iTable;
		}

	case TK_CAST:{
			/* Expressions of the form:   CAST(pLeft AS token) */
			inReg =
			    sqlExprCodeTarget(pParse, pExpr->pLeft, target);
			if (inReg != target) {
				sqlVdbeAddOp2(v, OP_SCopy, inReg, target);
				inReg = target;
			}
			sqlVdbeAddOp2(v, OP_Cast, target, pExpr->type);
			testcase(usedAsColumnCache(pParse, inReg, inReg));
			sql_expr_type_cache_change(pParse, inReg, 1);
			return inReg;
		}

	case TK_LT:
	case TK_LE:
	case TK_GT:
	case TK_GE:
	case TK_NE:
	case TK_EQ:{
			Expr *pLeft = pExpr->pLeft;
			if (sqlExprIsVector(pLeft)) {
				codeVectorCompare(pParse, pExpr, target);
			} else {
				r1 = sqlExprCodeTemp(pParse, pLeft,
							 &regFree1);
				r2 = sqlExprCodeTemp(pParse, pExpr->pRight,
							 &regFree2);
				codeCompare(pParse, pLeft, pExpr->pRight, op,
					    r1, r2, inReg, SQL_STOREP2);
				assert(TK_LT == OP_Lt);
				testcase(op == OP_Lt);
				VdbeCoverageIf(v, op == OP_Lt);
				assert(TK_LE == OP_Le);
				testcase(op == OP_Le);
				VdbeCoverageIf(v, op == OP_Le);
				assert(TK_GT == OP_Gt);
				testcase(op == OP_Gt);
				VdbeCoverageIf(v, op == OP_Gt);
				assert(TK_GE == OP_Ge);
				testcase(op == OP_Ge);
				VdbeCoverageIf(v, op == OP_Ge);
				assert(TK_EQ == OP_Eq);
				testcase(op == OP_Eq);
				VdbeCoverageIf(v, op == OP_Eq);
				assert(TK_NE == OP_Ne);
				testcase(op == OP_Ne);
				VdbeCoverageIf(v, op == OP_Ne);
				testcase(regFree1 == 0);
				testcase(regFree2 == 0);
			}
			break;
		}
	case TK_AND:
	case TK_OR:
	case TK_PLUS:
	case TK_STAR:
	case TK_MINUS:
	case TK_REM:
	case TK_BITAND:
	case TK_BITOR:
	case TK_SLASH:
	case TK_LSHIFT:
	case TK_RSHIFT:
	case TK_CONCAT:{
			assert(TK_AND == OP_And);
			testcase(op == TK_AND);
			assert(TK_OR == OP_Or);
			testcase(op == TK_OR);
			assert(TK_PLUS == OP_Add);
			testcase(op == TK_PLUS);
			assert(TK_MINUS == OP_Subtract);
			testcase(op == TK_MINUS);
			assert(TK_REM == OP_Remainder);
			testcase(op == TK_REM);
			assert(TK_BITAND == OP_BitAnd);
			testcase(op == TK_BITAND);
			assert(TK_BITOR == OP_BitOr);
			testcase(op == TK_BITOR);
			assert(TK_SLASH == OP_Divide);
			testcase(op == TK_SLASH);
			assert(TK_LSHIFT == OP_ShiftLeft);
			testcase(op == TK_LSHIFT);
			assert(TK_RSHIFT == OP_ShiftRight);
			testcase(op == TK_RSHIFT);
			assert(TK_CONCAT == OP_Concat);
			testcase(op == TK_CONCAT);
			r1 = sqlExprCodeTemp(pParse, pExpr->pLeft,
						 &regFree1);
			r2 = sqlExprCodeTemp(pParse, pExpr->pRight,
						 &regFree2);
			sqlVdbeAddOp3(v, op, r2, r1, target);
			testcase(regFree1 == 0);
			testcase(regFree2 == 0);
			break;
		}
	case TK_UMINUS:{
			Expr *pLeft = pExpr->pLeft;
			assert(pLeft);
			if (pLeft->op == TK_INTEGER) {
				expr_code_int(pParse, pLeft, true, target);
				return target;
			} else if (pLeft->op == TK_FLOAT) {
				assert(!ExprHasProperty(pExpr, EP_IntValue));
				codeReal(v, pLeft->u.zToken, 1, target);
				return target;
			} else {
				tempX.op = TK_INTEGER;
				tempX.flags = EP_IntValue | EP_TokenOnly;
				tempX.u.iValue = 0;
				r1 = sqlExprCodeTemp(pParse, &tempX,
							 &regFree1);
				r2 = sqlExprCodeTemp(pParse, pExpr->pLeft,
							 &regFree2);
				sqlVdbeAddOp3(v, OP_Subtract, r2, r1,
						  target);
				testcase(regFree2 == 0);
			}
			break;
		}
	case TK_BITNOT:
	case TK_NOT:{
			assert(TK_BITNOT == OP_BitNot);
			testcase(op == TK_BITNOT);
			assert(TK_NOT == OP_Not);
			testcase(op == TK_NOT);
			r1 = sqlExprCodeTemp(pParse, pExpr->pLeft,
						 &regFree1);
			testcase(regFree1 == 0);
			sqlVdbeAddOp2(v, op, r1, inReg);
			break;
		}
	case TK_ISNULL:
	case TK_NOTNULL:{
			int addr;
			assert(TK_ISNULL == OP_IsNull);
			testcase(op == TK_ISNULL);
			assert(TK_NOTNULL == OP_NotNull);
			testcase(op == TK_NOTNULL);
			sqlVdbeAddOp2(v, OP_Bool, true, target);
			r1 = sqlExprCodeTemp(pParse, pExpr->pLeft,
						 &regFree1);
			testcase(regFree1 == 0);
			addr = sqlVdbeAddOp1(v, op, r1);
			VdbeCoverageIf(v, op == TK_ISNULL);
			VdbeCoverageIf(v, op == TK_NOTNULL);
			sqlVdbeAddOp2(v, OP_Bool, false, target);
			sqlVdbeJumpHere(v, addr);
			break;
		}
	case TK_AGG_FUNCTION:{
			AggInfo *pInfo = pExpr->pAggInfo;
			if (pInfo == 0) {
				assert(!ExprHasProperty(pExpr, EP_IntValue));
				const char *err = "misuse of aggregate: %s()";
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 tt_sprintf(err, pExpr->u.zToken));
				pParse->is_aborted = true;
			} else {
				return pInfo->aFunc[pExpr->iAgg].iMem;
			}
			break;
		}
	case TK_FUNCTION:{
			ExprList *pFarg;	/* List of function arguments */
			int nFarg;	/* Number of function arguments */
			const char *zId;	/* The function name */
			u32 constMask = 0;	/* Mask of function arguments that are constant */
			int i;	/* Loop counter */
			struct coll *coll = NULL;

			assert(!ExprHasProperty(pExpr, EP_xIsSelect));
			if (ExprHasProperty(pExpr, EP_TokenOnly)) {
				pFarg = 0;
			} else {
				pFarg = pExpr->x.pList;
			}
			nFarg = pFarg ? pFarg->nExpr : 0;
			assert(!ExprHasProperty(pExpr, EP_IntValue));
			zId = pExpr->u.zToken;
			struct func *func = sql_func_by_signature(zId, nFarg);
			if (func == NULL) {
				diag_set(ClientError, ER_NO_SUCH_FUNCTION,
					 zId);
				pParse->is_aborted = true;
				break;
			}
			/* Attempt a direct implementation of the built-in COALESCE() and
			 * IFNULL() functions.  This avoids unnecessary evaluation of
			 * arguments past the first non-NULL argument.
			 */
			if (sql_func_flag_is_set(func, SQL_FUNC_COALESCE)) {
				int endCoalesce = sqlVdbeMakeLabel(v);
				if (nFarg < 2) {
					diag_set(ClientError,
						 ER_FUNC_WRONG_ARG_COUNT,
						 func->def->name,
						 "at least two", nFarg);
					pParse->is_aborted = true;
					break;
				}
				sqlExprCode(pParse, pFarg->a[0].pExpr,
						target);
				for (i = 1; i < nFarg; i++) {
					sqlVdbeAddOp2(v, OP_NotNull, target,
							  endCoalesce);
					VdbeCoverage(v);
					sqlExprCacheRemove(pParse, target,
							       1);
					sqlExprCachePush(pParse);
					sqlExprCode(pParse,
							pFarg->a[i].pExpr,
							target);
					sqlExprCachePop(pParse);
				}
				sqlVdbeResolveLabel(v, endCoalesce);
				break;
			}

			/* The UNLIKELY() function is a no-op.  The result is the value
			 * of the first argument.
			 */
			if (sql_func_flag_is_set(func, SQL_FUNC_UNLIKELY)) {
				if (nFarg < 1) {
					diag_set(ClientError,
						 ER_FUNC_WRONG_ARG_COUNT,
						 func->def->name,
						 "at least one", nFarg);
					pParse->is_aborted = true;
					break;
				}
				return sqlExprCodeTarget(pParse,
							     pFarg->a[0].pExpr,
							     target);
			}

			for (i = 0; i < nFarg; i++) {
				if (i < 32
				    && sqlExprIsConstant(pFarg->a[i].
							     pExpr)) {
					testcase(i == 31);
					constMask |= MASKBIT32(i);
				}
			}
			/*
			 * Function arguments may have different
			 * collations. The following code
			 * checks if they are compatible and
			 * finds the collation to be used. This
			 * is done using ANSI rules from
			 * collations_check_compatibility().
			 */
			if (sql_func_flag_is_set(func, SQL_FUNC_NEEDCOLL) &&
			    nFarg > 0) {
				struct coll *unused = NULL;
				uint32_t curr_id = COLL_NONE;
				bool is_curr_forced = false;

				uint32_t next_id = COLL_NONE;
				bool is_next_forced = false;

				if (sql_expr_coll(pParse, pFarg->a[0].pExpr,
						  &is_curr_forced, &curr_id,
						  &unused) != 0)
					return 0;

				for (int j = 1; j < nFarg; j++) {
					if (sql_expr_coll(pParse,
							  pFarg->a[j].pExpr,
							  &is_next_forced,
							  &next_id,
							  &unused) != 0)
						return 0;

					if (collations_check_compatibility(
						curr_id, is_curr_forced,
						next_id, is_next_forced,
						&curr_id) != 0) {
						pParse->is_aborted = true;
						return 0;
					}
					is_curr_forced = curr_id == next_id ?
							 is_next_forced :
							 is_curr_forced;
				}
				coll = coll_by_id(curr_id)->coll;
			}
			if (pFarg) {
				if (constMask) {
					r1 = pParse->nMem + 1;
					pParse->nMem += nFarg;
				} else {
					r1 = sqlGetTempRange(pParse, nFarg);
				}

				/* For length() and typeof() functions with a column argument,
				 * set the P5 parameter to the OP_Column opcode to OPFLAG_LENGTHARG
				 * or OPFLAG_TYPEOFARG respectively, to avoid unnecessary data
				 * loading.
				 */
				if (sql_func_flag_is_set(func, SQL_FUNC_LENGTH |
							       SQL_FUNC_TYPEOF)) {
					u8 exprOp;
					assert(nFarg == 1);
					assert(pFarg->a[0].pExpr != 0);
					exprOp = pFarg->a[0].pExpr->op;
					if (exprOp == TK_COLUMN_REF
					    || exprOp == TK_AGG_COLUMN) {
						assert(SQL_FUNC_LENGTH ==
						       OPFLAG_LENGTHARG);
						assert(SQL_FUNC_TYPEOF ==
						       OPFLAG_TYPEOFARG);
						pFarg->a[0].pExpr->op2 = true;
					}
				}

				sqlExprCachePush(pParse);	/* Ticket 2ea2425d34be */
				sqlExprCodeExprList(pParse, pFarg, r1, 0,
							SQL_ECEL_DUP |
							SQL_ECEL_FACTOR);
				sqlExprCachePop(pParse);	/* Ticket 2ea2425d34be */
			} else {
				r1 = 0;
			}
			if (sql_func_flag_is_set(func, SQL_FUNC_NEEDCOLL)) {
				sqlVdbeAddOp4(v, OP_CollSeq, 0, 0, 0,
						  (char *)coll, P4_COLLSEQ);
			}
			if (func->def->language == FUNC_LANGUAGE_SQL_BUILTIN) {
				sqlVdbeAddOp4(v, OP_BuiltinFunction0, constMask,
					      r1, target, (char *)func,
					      P4_FUNC);
			} else {
				sqlVdbeAddOp4(v, OP_FunctionByName, constMask,
					      r1, target,
					      sqlDbStrNDup(pParse->db,
							   func->def->name,
							   func->def->name_len),
					      P4_DYNAMIC);
			}
			sqlVdbeChangeP5(v, (u8) nFarg);
			if (nFarg && constMask == 0) {
				sqlReleaseTempRange(pParse, r1, nFarg);
			}
			return target;
		}
	case TK_EXISTS:
	case TK_SELECT:{
			int nCol;
			testcase(op == TK_EXISTS);
			testcase(op == TK_SELECT);
			if (op == TK_SELECT
			    && (nCol = pExpr->x.pSelect->pEList->nExpr) != 1) {
				diag_set(ClientError, ER_SQL_COLUMN_COUNT,
					 nCol, 1);
				pParse->is_aborted = true;
			} else {
				return sqlCodeSubselect(pParse, pExpr, 0);
			}
			break;
		}
	case TK_SELECT_COLUMN:{
			int n;
			if (pExpr->pLeft->iTable == 0) {
				pExpr->pLeft->iTable =
				    sqlCodeSubselect(pParse, pExpr->pLeft, 0);
			}
			assert(pExpr->iTable == 0
			       || pExpr->pLeft->op == TK_SELECT);
			if (pExpr->iTable
			    && pExpr->iTable != (n =
						 sqlExprVectorSize(pExpr->
								       pLeft))
			    ) {
				const char *err =
					"%d columns assigned %d values";
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 tt_sprintf(err, pExpr->iTable, n));
				pParse->is_aborted = true;
			}
			return pExpr->pLeft->iTable + pExpr->iColumn;
		}
	case TK_IN:{
			int destIfFalse = sqlVdbeMakeLabel(v);
			int destIfNull = sqlVdbeMakeLabel(v);
			sqlVdbeAddOp2(v, OP_Null, 0, target);
			sqlExprCodeIN(pParse, pExpr, destIfFalse,
					  destIfNull);
			sqlVdbeAddOp2(v, OP_Bool, true, target);
			sqlVdbeGoto(v, destIfNull);
			sqlVdbeResolveLabel(v, destIfFalse);
			sqlVdbeAddOp2(v, OP_Bool, false, target);
			sqlVdbeResolveLabel(v, destIfNull);
			return target;
		}

		/*
		 *    x BETWEEN y AND z
		 *
		 * This is equivalent to
		 *
		 *    x>=y AND x<=z
		 *
		 * X is stored in pExpr->pLeft.
		 * Y is stored in pExpr->pList->a[0].pExpr.
		 * Z is stored in pExpr->pList->a[1].pExpr.
		 */
	case TK_BETWEEN:{
			exprCodeBetween(pParse, pExpr, target, 0, 0);
			return target;
		}
	case TK_SPAN:
	case TK_COLLATE:{
			if (check_collate_arg(pParse, pExpr) != 0)
				break;
			return sqlExprCodeTarget(pParse, pExpr->pLeft,
						     target);
		}
	case TK_UPLUS:{
			return sqlExprCodeTarget(pParse, pExpr->pLeft,
						     target);
		}

	case TK_TRIGGER:{
			/* If the opcode is TK_TRIGGER, then the expression is a reference
			 * to a column in the new.* or old.* pseudo-tables available to
			 * trigger programs. In this case Expr.iTable is set to 1 for the
			 * new.* pseudo-table, or 0 for the old.* pseudo-table. Expr.iColumn
			 * is set to the column of the pseudo-table to read.
			 *
			 * The expression is implemented using an OP_Param opcode. The p1
			 * parameter is set to (i+1)
			 * to reference another column of the old.* pseudo-table, where
			 * i is the index of the column.
			 * For a reference to any other column in the new.* pseudo-table, p1
			 * is set to (n+2+i), where n and i are as defined previously. For
			 * example, if the table on which triggers are being fired is
			 * declared as:
			 *
			 *   CREATE TABLE t1(a, b);
			 *
			 * Then p1 is interpreted as follows:
			 *
			 *   p1==1   ->    old.a         p1==4   ->    new.a
			 *   p1==2   ->    old.b         p1==5   ->    new.b
			 */
			struct space_def *def = pExpr->space_def;
			int p1 =
			    pExpr->iTable * (def->field_count + 1) + 1 +
			    pExpr->iColumn;

			assert(pExpr->iTable == 0 || pExpr->iTable == 1);
			assert(pExpr->iColumn >= 0
			       && pExpr->iColumn < (int)def->field_count);
			assert(p1 >= 0 && p1 < ((int)def->field_count * 2 + 2));

			sqlVdbeAddOp2(v, OP_Param, p1, target);
			VdbeComment((v, "%s.%s -> $%d",
				    (pExpr->iTable ? "new" : "old"),
				    pExpr->space_def->fields[
					pExpr->iColumn].name, target));
			break;
		}

	case TK_VECTOR:{
			diag_set(ClientError, ER_SQL_PARSER_GENERIC,
				 "row value misused");
			pParse->is_aborted = true;
			break;
		}

		/*
		 * Form A:
		 *   CASE x WHEN e1 THEN r1 WHEN e2 THEN r2 ... WHEN eN THEN rN ELSE y END
		 *
		 * Form B:
		 *   CASE WHEN e1 THEN r1 WHEN e2 THEN r2 ... WHEN eN THEN rN ELSE y END
		 *
		 * Form A is can be transformed into the equivalent form B as follows:
		 *   CASE WHEN x=e1 THEN r1 WHEN x=e2 THEN r2 ...
		 *        WHEN x=eN THEN rN ELSE y END
		 *
		 * X (if it exists) is in pExpr->pLeft.
		 * Y is in the last element of pExpr->x.pList if pExpr->x.pList->nExpr is
		 * odd.  The Y is also optional.  If the number of elements in x.pList
		 * is even, then Y is omitted and the "otherwise" result is NULL.
		 * Ei is in pExpr->pList->a[i*2] and Ri is pExpr->pList->a[i*2+1].
		 *
		 * The result of the expression is the Ri for the first matching Ei,
		 * or if there is no matching Ei, the ELSE term Y, or if there is
		 * no ELSE term, NULL.
		 */
	default:
		assert(op == TK_CASE); {
			int endLabel;	/* GOTO label for end of CASE stmt */
			int nextCase;	/* GOTO label for next WHEN clause */
			int nExpr;	/* 2x number of WHEN terms */
			int i;	/* Loop counter */
			ExprList *pEList;	/* List of WHEN terms */
			struct ExprList_item *aListelem;	/* Array of WHEN terms */
			Expr opCompare;	/* The X==Ei expression */
			Expr *pX;	/* The X expression */
			Expr *pTest = 0;	/* X==Ei (form A) or just Ei (form B) */
			VVA_ONLY(int iCacheLevel = pParse->iCacheLevel;
			    )

			    assert(!ExprHasProperty(pExpr, EP_xIsSelect)
				   && pExpr->x.pList);
			assert(pExpr->x.pList->nExpr > 0);
			pEList = pExpr->x.pList;
			aListelem = pEList->a;
			nExpr = pEList->nExpr;
			endLabel = sqlVdbeMakeLabel(v);
			if ((pX = pExpr->pLeft) != 0) {
				tempX = *pX;
				testcase(pX->op == TK_COLUMN_REF);
				exprToRegister(&tempX,
					       exprCodeVector(pParse, &tempX,
							      &regFree1));
				testcase(regFree1 == 0);
				memset(&opCompare, 0, sizeof(opCompare));
				opCompare.op = TK_EQ;
				opCompare.pLeft = &tempX;
				pTest = &opCompare;
				/* Ticket b351d95f9cd5ef17e9d9dbae18f5ca8611190001:
				 * The value in regFree1 might get SCopy-ed into the file result.
				 * So make sure that the regFree1 register is not reused for other
				 * purposes and possibly overwritten.
				 */
				regFree1 = 0;
			}
			for (i = 0; i < nExpr - 1; i = i + 2) {
				sqlExprCachePush(pParse);
				if (pX) {
					assert(pTest != 0);
					opCompare.pRight = aListelem[i].pExpr;
				} else {
					pTest = aListelem[i].pExpr;
				}
				nextCase = sqlVdbeMakeLabel(v);
				testcase(pTest->op == TK_COLUMN_REF);
				sqlExprIfFalse(pParse, pTest, nextCase,
						   SQL_JUMPIFNULL);
				testcase(aListelem[i + 1].pExpr->op ==
					 TK_COLUMN_REF);
				sqlExprCode(pParse, aListelem[i + 1].pExpr,
						target);
				sqlVdbeGoto(v, endLabel);
				sqlExprCachePop(pParse);
				sqlVdbeResolveLabel(v, nextCase);
			}
			if ((nExpr & 1) != 0) {
				sqlExprCachePush(pParse);
				sqlExprCode(pParse,
						pEList->a[nExpr - 1].pExpr,
						target);
				sqlExprCachePop(pParse);
			} else {
				sqlVdbeAddOp2(v, OP_Null, 0, target);
			}
			assert(pParse->db->mallocFailed || pParse->is_aborted
			       || pParse->iCacheLevel == iCacheLevel);
			sqlVdbeResolveLabel(v, endLabel);
			break;
		}
	case TK_RAISE:
		if (pParse->triggered_space == NULL) {
			diag_set(ClientError, ER_SQL_PARSER_GENERIC, "RAISE() "\
				 "may only be used within a trigger-program");
			pParse->is_aborted = true;
			return 0;
		}
		assert(!ExprHasProperty(pExpr, EP_IntValue));
		if (pExpr->on_conflict_action == ON_CONFLICT_ACTION_IGNORE) {
			sqlVdbeAddOp4(v, OP_Halt, 0,
					  ON_CONFLICT_ACTION_IGNORE, 0,
					  pExpr->u.zToken, 0);
		} else {
			const char *err =
				tt_sprintf(tnt_errcode_desc(ER_SQL_EXECUTE),
					   pExpr->u.zToken);
			sqlVdbeAddOp4(v, OP_SetDiag, ER_SQL_EXECUTE, 0, 0, err,
				      P4_STATIC);
			sqlVdbeAddOp2(v, OP_Halt, -1,
				      pExpr->on_conflict_action);
		}
		break;
	}
	sqlReleaseTempReg(pParse, regFree1);
	sqlReleaseTempReg(pParse, regFree2);
	return inReg;
}

/*
 * Factor out the code of the given expression to initialization time.
 */
void
sqlExprCodeAtInit(Parse * pParse,	/* Parsing context */
		      Expr * pExpr,	/* The expression to code when the VDBE initializes */
		      int regDest,	/* Store the value in this register */
		      u8 reusable	/* True if this expression is reusable */
    )
{
	ExprList *p;
	assert(ConstFactorOk(pParse));
	p = pParse->pConstExpr;
	pExpr = sqlExprDup(pParse->db, pExpr, 0);
	p = sql_expr_list_append(pParse->db, p, pExpr);
	if (p) {
		struct ExprList_item *pItem = &p->a[p->nExpr - 1];
		pItem->u.iConstExprReg = regDest;
		pItem->reusable = reusable;
	}
	pParse->pConstExpr = p;
}

/*
 * Generate code to evaluate an expression and store the results
 * into a register.  Return the register number where the results
 * are stored.
 *
 * If the register is a temporary register that can be deallocated,
 * then write its number into *pReg.  If the result register is not
 * a temporary, then set *pReg to zero.
 *
 * If pExpr is a constant, then this routine might generate this
 * code to fill the register in the initialization section of the
 * VDBE program, in order to factor it out of the evaluation loop.
 */
int
sqlExprCodeTemp(Parse * pParse, Expr * pExpr, int *pReg)
{
	int r2;
	if (ConstFactorOk(pParse)
	    && pExpr->op != TK_REGISTER && sqlExprIsConstantNotJoin(pExpr)
	    ) {
		ExprList *p = pParse->pConstExpr;
		int i;
		*pReg = 0;
		if (p) {
			struct ExprList_item *pItem;
			for (pItem = p->a, i = p->nExpr; i > 0; pItem++, i--) {
				if (pItem->reusable
				    && sqlExprCompare(pItem->pExpr, pExpr,
							  -1) == 0) {
					return pItem->u.iConstExprReg;
				}
			}
		}
		r2 = ++pParse->nMem;
		sqlExprCodeAtInit(pParse, pExpr, r2, 1);
	} else {
		int r1 = sqlGetTempReg(pParse);
		r2 = sqlExprCodeTarget(pParse, pExpr, r1);
		if (r2 == r1) {
			*pReg = r1;
		} else {
			sqlReleaseTempReg(pParse, r1);
			*pReg = 0;
		}
	}
	return r2;
}

/*
 * Generate code that will evaluate expression pExpr and store the
 * results in register target.  The results are guaranteed to appear
 * in register target.
 */
void
sqlExprCode(Parse * pParse, Expr * pExpr, int target)
{
	int inReg;

	assert(target > 0 && target <= pParse->nMem);
	if (pExpr && pExpr->op == TK_REGISTER) {
		sqlVdbeAddOp2(pParse->pVdbe, OP_Copy, pExpr->iTable,
				  target);
	} else {
		inReg = sqlExprCodeTarget(pParse, pExpr, target);
		assert(pParse->pVdbe != 0 || pParse->db->mallocFailed);
		if (inReg != target && pParse->pVdbe) {
			sqlVdbeAddOp2(pParse->pVdbe, OP_SCopy, inReg,
					  target);
		}
	}
}

/*
 * Generate code that will evaluate expression pExpr and store the
 * results in register target.  The results are guaranteed to appear
 * in register target.  If the expression is constant, then this routine
 * might choose to code the expression at initialization time.
 */
void
sqlExprCodeFactorable(Parse * pParse, Expr * pExpr, int target)
{
	if (pParse->okConstFactor && sqlExprIsConstant(pExpr)) {
		sqlExprCodeAtInit(pParse, pExpr, target, 0);
	} else {
		sqlExprCode(pParse, pExpr, target);
	}
}

/*
 * Generate code that evaluates the given expression and puts the result
 * in register target.
 *
 * Also make a copy of the expression results into another "cache" register
 * and modify the expression so that the next time it is evaluated,
 * the result is a copy of the cache register.
 *
 * This routine is used for expressions that are used multiple
 * times.  They are evaluated once and the results of the expression
 * are reused.
 */
void
sqlExprCodeAndCache(Parse * pParse, Expr * pExpr, int target)
{
	Vdbe *v = pParse->pVdbe;
	int iMem;

	assert(target > 0);
	assert(pExpr->op != TK_REGISTER);
	sqlExprCode(pParse, pExpr, target);
	iMem = ++pParse->nMem;
	sqlVdbeAddOp2(v, OP_Copy, target, iMem);
	exprToRegister(pExpr, iMem);
}

/*
 * Generate code that pushes the value of every element of the given
 * expression list into a sequence of registers beginning at target.
 *
 * Return the number of elements evaluated.
 *
 * The SQL_ECEL_DUP flag prevents the arguments from being
 * filled using OP_SCopy.  OP_Copy must be used instead.
 *
 * The SQL_ECEL_FACTOR argument allows constant arguments to be
 * factored out into initialization code.
 *
 * The SQL_ECEL_REF flag means that expressions in the list with
 * ExprList.a[].u.x.iOrderByCol>0 have already been evaluated and stored
 * in registers at srcReg, and so the value can be copied from there.
 */
int
sqlExprCodeExprList(Parse * pParse,	/* Parsing context */
			ExprList * pList,	/* The expression list to be coded */
			int target,	/* Where to write results */
			int srcReg,	/* Source registers if SQL_ECEL_REF */
			u8 flags	/* SQL_ECEL_* flags */
    )
{
	struct ExprList_item *pItem;
	int i, j, n;
	u8 copyOp = (flags & SQL_ECEL_DUP) ? OP_Copy : OP_SCopy;
	Vdbe *v = pParse->pVdbe;
	assert(pList != 0);
	assert(target > 0);
	assert(pParse->pVdbe != 0);	/* Never gets this far otherwise */
	n = pList->nExpr;
	if (!ConstFactorOk(pParse))
		flags &= ~SQL_ECEL_FACTOR;
	for (pItem = pList->a, i = 0; i < n; i++, pItem++) {
		Expr *pExpr = pItem->pExpr;
		if ((flags & SQL_ECEL_REF) != 0
		    && (j = pItem->u.x.iOrderByCol) > 0) {
			if (flags & SQL_ECEL_OMITREF) {
				i--;
				n--;
			} else {
				sqlVdbeAddOp2(v, copyOp, j + srcReg - 1,
						  target + i);
			}
		} else if ((flags & SQL_ECEL_FACTOR) != 0
			   && sqlExprIsConstant(pExpr)) {
			sqlExprCodeAtInit(pParse, pExpr, target + i, 0);
		} else {
			int inReg =
			    sqlExprCodeTarget(pParse, pExpr, target + i);
			if (inReg != target + i) {
				VdbeOp *pOp;
				if (copyOp == OP_Copy
				    && (pOp =
					sqlVdbeGetOp(v,
							 -1))->opcode == OP_Copy
				    && pOp->p1 + pOp->p3 + 1 == inReg
				    && pOp->p2 + pOp->p3 + 1 == target + i) {
					pOp->p3++;
				} else {
					sqlVdbeAddOp2(v, copyOp, inReg,
							  target + i);
				}
			}
		}
	}
	return n;
}

/*
 * Generate code for a BETWEEN operator.
 *
 *    x BETWEEN y AND z
 *
 * The above is equivalent to
 *
 *    x>=y AND x<=z
 *
 * Code it as such, taking care to do the common subexpression
 * elimination of x.
 *
 * The xJumpIf parameter determines details:
 *
 *    NULL:                   Store the boolean result in reg[dest]
 *    sqlExprIfTrue:      Jump to dest if true
 *    sqlExprIfFalse:     Jump to dest if false
 *
 * The jumpIfNull parameter is ignored if xJumpIf is NULL.
 */
static void
exprCodeBetween(Parse * pParse,	/* Parsing and code generating context */
		Expr * pExpr,	/* The BETWEEN expression */
		int dest,	/* Jump destination or storage location */
		void (*xJump) (Parse *, Expr *, int, int),	/* Action to take */
		int jumpIfNull	/* Take the jump if the BETWEEN is NULL */
    )
{
	Expr exprAnd;		/* The AND operator in  x>=y AND x<=z  */
	Expr compLeft;		/* The  x>=y  term */
	Expr compRight;		/* The  x<=z  term */
	Expr exprX;		/* The  x  subexpression */
	int regFree1 = 0;	/* Temporary use register */

	memset(&compLeft, 0, sizeof(Expr));
	memset(&compRight, 0, sizeof(Expr));
	memset(&exprAnd, 0, sizeof(Expr));

	assert(!ExprHasProperty(pExpr, EP_xIsSelect));
	exprX = *pExpr->pLeft;
	exprAnd.op = TK_AND;
	exprAnd.pLeft = &compLeft;
	exprAnd.pRight = &compRight;
	compLeft.op = TK_GE;
	compLeft.pLeft = &exprX;
	compLeft.pRight = pExpr->x.pList->a[0].pExpr;
	compRight.op = TK_LE;
	compRight.pLeft = &exprX;
	compRight.pRight = pExpr->x.pList->a[1].pExpr;
	exprToRegister(&exprX, exprCodeVector(pParse, &exprX, &regFree1));
	if (xJump) {
		xJump(pParse, &exprAnd, dest, jumpIfNull);
	} else {
		/* Mark the expression is being from the ON or USING clause of a join
		 * so that the sqlExprCodeTarget() routine will not attempt to move
		 * it into the Parse.pConstExpr list.  We should use a new bit for this,
		 * for clarity, but we are out of bits in the Expr.flags field so we
		 * have to reuse the EP_FromJoin bit.  Bummer.
		 */
		exprX.flags |= EP_FromJoin;
		sqlExprCodeTarget(pParse, &exprAnd, dest);
	}
	sqlReleaseTempReg(pParse, regFree1);

	/* Ensure adequate test coverage */
	testcase(xJump == sqlExprIfTrue && jumpIfNull == 0
		 && regFree1 == 0);
	testcase(xJump == sqlExprIfTrue && jumpIfNull == 0
		 && regFree1 != 0);
	testcase(xJump == sqlExprIfTrue && jumpIfNull != 0
		 && regFree1 == 0);
	testcase(xJump == sqlExprIfTrue && jumpIfNull != 0
		 && regFree1 != 0);
	testcase(xJump == sqlExprIfFalse && jumpIfNull == 0
		 && regFree1 == 0);
	testcase(xJump == sqlExprIfFalse && jumpIfNull == 0
		 && regFree1 != 0);
	testcase(xJump == sqlExprIfFalse && jumpIfNull != 0
		 && regFree1 == 0);
	testcase(xJump == sqlExprIfFalse && jumpIfNull != 0
		 && regFree1 != 0);
	testcase(xJump == 0);
}

/*
 * Generate code for a boolean expression such that a jump is made
 * to the label "dest" if the expression is true but execution
 * continues straight thru if the expression is false.
 *
 * If the expression evaluates to NULL (neither true nor false), then
 * take the jump if the jumpIfNull flag is SQL_JUMPIFNULL.
 *
 * This code depends on the fact that certain token values (ex: TK_EQ)
 * are the same as opcode values (ex: OP_Eq) that implement the corresponding
 * operation.  Special comments in vdbe.c and the mkopcodeh.awk script in
 * the make process cause these values to align.  Assert()s in the code
 * below verify that the numbers are aligned correctly.
 */
void
sqlExprIfTrue(Parse * pParse, Expr * pExpr, int dest, int jumpIfNull)
{
	Vdbe *v = pParse->pVdbe;
	int op = 0;
	int regFree1 = 0;
	int regFree2 = 0;
	int r1, r2;

	assert(jumpIfNull == SQL_JUMPIFNULL || jumpIfNull == 0);
	if (NEVER(v == 0))
		return;		/* Existence of VDBE checked by caller */
	if (NEVER(pExpr == 0))
		return;		/* No way this can happen */
	op = pExpr->op;
	switch (op) {
	case TK_AND:{
			int d2 = sqlVdbeMakeLabel(v);
			testcase(jumpIfNull == 0);
			sqlExprIfFalse(pParse, pExpr->pLeft, d2,
					   jumpIfNull ^ SQL_JUMPIFNULL);
			sqlExprCachePush(pParse);
			sqlExprIfTrue(pParse, pExpr->pRight, dest,
					  jumpIfNull);
			sqlVdbeResolveLabel(v, d2);
			sqlExprCachePop(pParse);
			break;
		}
	case TK_OR:{
			testcase(jumpIfNull == 0);
			sqlExprIfTrue(pParse, pExpr->pLeft, dest,
					  jumpIfNull);
			sqlExprCachePush(pParse);
			sqlExprIfTrue(pParse, pExpr->pRight, dest,
					  jumpIfNull);
			sqlExprCachePop(pParse);
			break;
		}
	case TK_NOT:{
			testcase(jumpIfNull == 0);
			sqlExprIfFalse(pParse, pExpr->pLeft, dest,
					   jumpIfNull);
			break;
		}
	case TK_LT:
	case TK_LE:
	case TK_GT:
	case TK_GE:
	case TK_NE:
	case TK_EQ:{
			if (sqlExprIsVector(pExpr->pLeft))
				goto default_expr;
			testcase(jumpIfNull == 0);
			r1 = sqlExprCodeTemp(pParse, pExpr->pLeft,
						 &regFree1);
			r2 = sqlExprCodeTemp(pParse, pExpr->pRight,
						 &regFree2);
			codeCompare(pParse, pExpr->pLeft, pExpr->pRight, op, r1,
				    r2, dest, jumpIfNull);
			assert(TK_LT == OP_Lt);
			testcase(op == OP_Lt);
			VdbeCoverageIf(v, op == OP_Lt);
			assert(TK_LE == OP_Le);
			testcase(op == OP_Le);
			VdbeCoverageIf(v, op == OP_Le);
			assert(TK_GT == OP_Gt);
			testcase(op == OP_Gt);
			VdbeCoverageIf(v, op == OP_Gt);
			assert(TK_GE == OP_Ge);
			testcase(op == OP_Ge);
			VdbeCoverageIf(v, op == OP_Ge);
			assert(TK_EQ == OP_Eq);
			testcase(op == OP_Eq);
			VdbeCoverageIf(v, op == OP_Eq
				       && jumpIfNull == SQL_NULLEQ);
			VdbeCoverageIf(v, op == OP_Eq
				       && jumpIfNull != SQL_NULLEQ);
			assert(TK_NE == OP_Ne);
			testcase(op == OP_Ne);
			VdbeCoverageIf(v, op == OP_Ne
				       && jumpIfNull == SQL_NULLEQ);
			VdbeCoverageIf(v, op == OP_Ne
				       && jumpIfNull != SQL_NULLEQ);
			testcase(regFree1 == 0);
			testcase(regFree2 == 0);
			break;
		}
	case TK_ISNULL:
	case TK_NOTNULL:{
			assert(TK_ISNULL == OP_IsNull);
			testcase(op == TK_ISNULL);
			assert(TK_NOTNULL == OP_NotNull);
			testcase(op == TK_NOTNULL);
			r1 = sqlExprCodeTemp(pParse, pExpr->pLeft,
						 &regFree1);
			sqlVdbeAddOp2(v, op, r1, dest);
			VdbeCoverageIf(v, op == TK_ISNULL);
			VdbeCoverageIf(v, op == TK_NOTNULL);
			testcase(regFree1 == 0);
			break;
		}
	case TK_BETWEEN:{
			testcase(jumpIfNull == 0);
			exprCodeBetween(pParse, pExpr, dest, sqlExprIfTrue,
					jumpIfNull);
			break;
		}
	case TK_IN:{
			int destIfFalse = sqlVdbeMakeLabel(v);
			int destIfNull = jumpIfNull ? dest : destIfFalse;
			sqlExprCodeIN(pParse, pExpr, destIfFalse,
					  destIfNull);
			sqlVdbeGoto(v, dest);
			sqlVdbeResolveLabel(v, destIfFalse);
			break;
		}
	default:{
 default_expr:
			if (exprAlwaysTrue(pExpr)) {
				sqlVdbeGoto(v, dest);
			} else if (exprAlwaysFalse(pExpr)) {
				/* No-op */
			} else {
				r1 = sqlExprCodeTemp(pParse, pExpr,
							 &regFree1);
				sqlVdbeAddOp3(v, OP_If, r1, dest,
						  jumpIfNull != 0);
				VdbeCoverage(v);
				testcase(regFree1 == 0);
				testcase(jumpIfNull == 0);
			}
			break;
		}
	}
	sqlReleaseTempReg(pParse, regFree1);
	sqlReleaseTempReg(pParse, regFree2);
}

/*
 * Generate code for a boolean expression such that a jump is made
 * to the label "dest" if the expression is false but execution
 * continues straight thru if the expression is true.
 *
 * If the expression evaluates to NULL (neither true nor false) then
 * jump if jumpIfNull is SQL_JUMPIFNULL or fall through if jumpIfNull
 * is 0.
 */
void
sqlExprIfFalse(Parse * pParse, Expr * pExpr, int dest, int jumpIfNull)
{
	Vdbe *v = pParse->pVdbe;
	int op = 0;
	int regFree1 = 0;
	int regFree2 = 0;
	int r1, r2;

	assert(jumpIfNull == SQL_JUMPIFNULL || jumpIfNull == 0);
	if (NEVER(v == 0))
		return;		/* Existence of VDBE checked by caller */
	if (pExpr == 0)
		return;

	/*
	 * The value of pExpr->op and op are related as follows:
	 *
	 *       pExpr->op            op
	 *       ---------          ----------
	 *       TK_NE              OP_Eq
	 *       TK_EQ              OP_Ne
	 *       TK_GT              OP_Le
	 *       TK_LE              OP_Gt
	 *       TK_LT              OP_Ge
	 *       TK_GE              OP_Lt
	 *        ...                ...
	 *       TK_ISNULL          OP_NotNull
	 *       TK_NOTNULL         OP_IsNull
	 *
	 * For other values of pExpr->op, op is undefined
	 * and unused. The value of TK_ and OP_ constants
	 * are arranged such that we can compute the mapping
	 * above using the following expression. The idea
	 * is that both for OP_'s and TK_'s the first elements
	 * in the given mapping ranges of codes and tokens are
	 * 'Not equal' and 'Is null'. Moreover the 'excluding'
	 * ones (like 'Greater than' and 'Lower than or Equal')
	 * are paired and follow one each other, hence have n
	 * and n + 1 numbers.
	 * Assert()s verify that the computation is correct.
	 */

	if (pExpr->op >= TK_NE && pExpr->op <= TK_GE)
		op = ((pExpr->op + (TK_NE & 1)) ^ 1) - (TK_NE & 1);
	if (pExpr->op == TK_ISNULL || pExpr->op == TK_NOTNULL)
		op = ((pExpr->op + (TK_ISNULL & 1)) ^ 1) - (TK_ISNULL & 1);

	/*
	 * Verify correct alignment of TK_ and OP_ constants.
	 */
	assert(pExpr->op != TK_NE || op == OP_Eq);
	assert(pExpr->op != TK_EQ || op == OP_Ne);
	assert(pExpr->op != TK_LT || op == OP_Ge);
	assert(pExpr->op != TK_LE || op == OP_Gt);
	assert(pExpr->op != TK_GT || op == OP_Le);
	assert(pExpr->op != TK_GE || op == OP_Lt);

	assert(pExpr->op != TK_ISNULL || op == OP_NotNull);
	assert(pExpr->op != TK_NOTNULL || op == OP_IsNull);

	switch (pExpr->op) {
	case TK_AND:{
			testcase(jumpIfNull == 0);
			sqlExprIfFalse(pParse, pExpr->pLeft, dest,
					   jumpIfNull);
			sqlExprCachePush(pParse);
			sqlExprIfFalse(pParse, pExpr->pRight, dest,
					   jumpIfNull);
			sqlExprCachePop(pParse);
			break;
		}
	case TK_OR:{
			int d2 = sqlVdbeMakeLabel(v);
			testcase(jumpIfNull == 0);
			sqlExprIfTrue(pParse, pExpr->pLeft, d2,
					  jumpIfNull ^ SQL_JUMPIFNULL);
			sqlExprCachePush(pParse);
			sqlExprIfFalse(pParse, pExpr->pRight, dest,
					   jumpIfNull);
			sqlVdbeResolveLabel(v, d2);
			sqlExprCachePop(pParse);
			break;
		}
	case TK_NOT:{
			testcase(jumpIfNull == 0);
			sqlExprIfTrue(pParse, pExpr->pLeft, dest,
					  jumpIfNull);
			break;
		}
	case TK_LT:
	case TK_LE:
	case TK_GT:
	case TK_GE:
	case TK_NE:
	case TK_EQ:{
			if (sqlExprIsVector(pExpr->pLeft))
				goto default_expr;
			testcase(jumpIfNull == 0);
			r1 = sqlExprCodeTemp(pParse, pExpr->pLeft,
						 &regFree1);
			r2 = sqlExprCodeTemp(pParse, pExpr->pRight,
						 &regFree2);
			codeCompare(pParse, pExpr->pLeft, pExpr->pRight, op, r1,
				    r2, dest, jumpIfNull);
			assert(TK_LT == OP_Lt);
			testcase(op == OP_Lt);
			VdbeCoverageIf(v, op == OP_Lt);
			assert(TK_LE == OP_Le);
			testcase(op == OP_Le);
			VdbeCoverageIf(v, op == OP_Le);
			assert(TK_GT == OP_Gt);
			testcase(op == OP_Gt);
			VdbeCoverageIf(v, op == OP_Gt);
			assert(TK_GE == OP_Ge);
			testcase(op == OP_Ge);
			VdbeCoverageIf(v, op == OP_Ge);
			assert(TK_EQ == OP_Eq);
			testcase(op == OP_Eq);
			VdbeCoverageIf(v, op == OP_Eq
				       && jumpIfNull != SQL_NULLEQ);
			VdbeCoverageIf(v, op == OP_Eq
				       && jumpIfNull == SQL_NULLEQ);
			assert(TK_NE == OP_Ne);
			testcase(op == OP_Ne);
			VdbeCoverageIf(v, op == OP_Ne
				       && jumpIfNull != SQL_NULLEQ);
			VdbeCoverageIf(v, op == OP_Ne
				       && jumpIfNull == SQL_NULLEQ);
			testcase(regFree1 == 0);
			testcase(regFree2 == 0);
			break;
		}
	case TK_ISNULL:
	case TK_NOTNULL:{
			r1 = sqlExprCodeTemp(pParse, pExpr->pLeft,
						 &regFree1);
			sqlVdbeAddOp2(v, op, r1, dest);
			testcase(op == TK_ISNULL);
			VdbeCoverageIf(v, op == TK_ISNULL);
			testcase(op == TK_NOTNULL);
			VdbeCoverageIf(v, op == TK_NOTNULL);
			testcase(regFree1 == 0);
			break;
		}
	case TK_BETWEEN:{
			testcase(jumpIfNull == 0);
			exprCodeBetween(pParse, pExpr, dest, sqlExprIfFalse,
					jumpIfNull);
			break;
		}
	case TK_IN:{
			if (jumpIfNull) {
				sqlExprCodeIN(pParse, pExpr, dest, dest);
			} else {
				int destIfNull = sqlVdbeMakeLabel(v);
				sqlExprCodeIN(pParse, pExpr, dest,
						  destIfNull);
				sqlVdbeResolveLabel(v, destIfNull);
			}
			break;
		}
	default:{
 default_expr:
			if (exprAlwaysFalse(pExpr)) {
				sqlVdbeGoto(v, dest);
			} else if (exprAlwaysTrue(pExpr)) {
				/* no-op */
			} else {
				r1 = sqlExprCodeTemp(pParse, pExpr,
							 &regFree1);
				sqlVdbeAddOp3(v, OP_IfNot, r1, dest,
						  jumpIfNull != 0);
				VdbeCoverage(v);
				testcase(regFree1 == 0);
				testcase(jumpIfNull == 0);
			}
			break;
		}
	}
	sqlReleaseTempReg(pParse, regFree1);
	sqlReleaseTempReg(pParse, regFree2);
}

/*
 * Do a deep comparison of two expression trees.  Return 0 if the two
 * expressions are completely identical.  Return 1 if they differ only
 * by a COLLATE operator at the top level.  Return 2 if there are differences
 * other than the top-level COLLATE operator.
 *
 * If any subelement of pB has Expr.iTable==(-1) then it is allowed
 * to compare equal to an equivalent element in pA with Expr.iTable==iTab.
 *
 * The pA side might be using TK_REGISTER.  If that is the case and pB is
 * not using TK_REGISTER but is otherwise equivalent, then still return 0.
 *
 * Sometimes this routine will return 2 even if the two expressions
 * really are equivalent.  If we cannot prove that the expressions are
 * identical, we return 2 just to be safe.  So if this routine
 * returns 2, then you do not really know for certain if the two
 * expressions are the same.  But if you get a 0 or 1 return, then you
 * can be sure the expressions are the same.  In the places where
 * this routine is used, it does not hurt to get an extra 2 - that
 * just might result in some slightly slower code.  But returning
 * an incorrect 0 or 1 could lead to a malfunction.
 */
int
sqlExprCompare(Expr * pA, Expr * pB, int iTab)
{
	u32 combinedFlags;
	if (pA == 0 || pB == 0) {
		return pB == pA ? 0 : 2;
	}
	combinedFlags = pA->flags | pB->flags;
	if (combinedFlags & EP_IntValue) {
		if ((pA->flags & pB->flags & EP_IntValue) != 0
		    && pA->u.iValue == pB->u.iValue) {
			return 0;
		}
		return 2;
	}
	if (pA->op != pB->op) {
		if (pA->op == TK_COLLATE
		    && sqlExprCompare(pA->pLeft, pB, iTab) < 2) {
			return 1;
		}
		if (pB->op == TK_COLLATE
		    && sqlExprCompare(pA, pB->pLeft, iTab) < 2) {
			return 1;
		}
		return 2;
	}
	if (pA->op != TK_COLUMN_REF && pA->op != TK_AGG_COLUMN &&
	    pA->u.zToken) {
		if (pA->op == TK_FUNCTION) {
			if (sqlStrICmp(pA->u.zToken, pB->u.zToken) != 0)
				return 2;
		} else if (strcmp(pA->u.zToken, pB->u.zToken) != 0) {
			return pA->op == TK_COLLATE ? 1 : 2;
		}
	}
	if ((pA->flags & EP_Distinct) != (pB->flags & EP_Distinct))
		return 2;
	if (ALWAYS((combinedFlags & EP_TokenOnly) == 0)) {
		if (combinedFlags & EP_xIsSelect)
			return 2;
		if (sqlExprCompare(pA->pLeft, pB->pLeft, iTab))
			return 2;
		if (sqlExprCompare(pA->pRight, pB->pRight, iTab))
			return 2;
		if (sqlExprListCompare(pA->x.pList, pB->x.pList, iTab))
			return 2;
		if (ALWAYS((combinedFlags & EP_Reduced) == 0)
		    && pA->op != TK_STRING) {
			if (pA->iColumn != pB->iColumn)
				return 2;
			if (pA->iTable != pB->iTable
			    && (pA->iTable != iTab || NEVER(pB->iTable >= 0)))
				return 2;
		}
	}
	return 0;
}

/*
 * Compare two ExprList objects.  Return 0 if they are identical and
 * non-zero if they differ in any way.
 *
 * If any subelement of pB has Expr.iTable==(-1) then it is allowed
 * to compare equal to an equivalent element in pA with Expr.iTable==iTab.
 *
 * This routine might return non-zero for equivalent ExprLists.  The
 * only consequence will be disabled optimizations.  But this routine
 * must never return 0 if the two ExprList objects are different, or
 * a malfunction will result.
 *
 * Two NULL pointers are considered to be the same.  But a NULL pointer
 * always differs from a non-NULL pointer.
 */
int
sqlExprListCompare(ExprList * pA, ExprList * pB, int iTab)
{
	int i;
	if (pA == 0 && pB == 0)
		return 0;
	if (pA == 0 || pB == 0)
		return 1;
	if (pA->nExpr != pB->nExpr)
		return 1;
	for (i = 0; i < pA->nExpr; i++) {
		Expr *pExprA = pA->a[i].pExpr;
		Expr *pExprB = pB->a[i].pExpr;
		if (pA->a[i].sort_order != pB->a[i].sort_order)
			return 1;
		if (sqlExprCompare(pExprA, pExprB, iTab))
			return 1;
	}
	return 0;
}

/*
 * Return true if we can prove the pE2 will always be true if pE1 is
 * true.  Return false if we cannot complete the proof or if pE2 might
 * be false.  Examples:
 *
 *     pE1: x==5       pE2: x==5             Result: true
 *     pE1: x>0        pE2: x==5             Result: false
 *     pE1: x=21       pE2: x=21 OR y=43     Result: true
 *     pE1: x!=123     pE2: x IS NOT NULL    Result: true
 *     pE1: x!=?1      pE2: x IS NOT NULL    Result: true
 *     pE1: x IS NULL  pE2: x IS NOT NULL    Result: false
 *     pE1: x IS ?2    pE2: x IS NOT NULL    Reuslt: false
 *
 * When comparing TK_COLUMN_REF nodes between pE1 and pE2, if
 * pE2 has Expr.iTable<0 then assume a table number given by iTab.
 *
 * When in doubt, return false.  Returning true might give a performance
 * improvement.  Returning false might cause a performance reduction, but
 * it will always give the correct answer and is hence always safe.
 */
int
sqlExprImpliesExpr(Expr * pE1, Expr * pE2, int iTab)
{
	if (sqlExprCompare(pE1, pE2, iTab) == 0) {
		return 1;
	}
	if (pE2->op == TK_OR && (sqlExprImpliesExpr(pE1, pE2->pLeft, iTab)
				 || sqlExprImpliesExpr(pE1, pE2->pRight,
							   iTab))
	    ) {
		return 1;
	}
	if (pE2->op == TK_NOTNULL && pE1->op != TK_ISNULL) {
		Expr *pX = sqlExprSkipCollate(pE1->pLeft);
		testcase(pX != pE1->pLeft);
		if (sqlExprCompare(pX, pE2->pLeft, iTab) == 0)
			return 1;
	}
	return 0;
}

/*
 * An instance of the following structure is used by the tree walker
 * to count references to table columns in the arguments of an
 * aggregate function, in order to implement the
 * sqlFunctionThisSrc() routine.
 */
struct SrcCount {
	SrcList *pSrc;		/* One particular FROM clause in a nested query */
	int nThis;		/* Number of references to columns in pSrcList */
	int nOther;		/* Number of references to columns in other FROM clauses */
};

/*
 * Count the number of references to columns.
 */
static int
exprSrcCount(Walker * pWalker, Expr * pExpr)
{
	/* The NEVER() on the second term is because sqlFunctionUsesThisSrc()
	 * is always called before sqlExprAnalyzeAggregates() and so the
	 * TK_COLUMN_REFs have not yet been converted into TK_AGG_COLUMN. If
	 * sqlFunctionUsesThisSrc() is used differently in the future, the
	 * NEVER() will need to be removed.
	 */
	if (pExpr->op == TK_COLUMN_REF || NEVER(pExpr->op == TK_AGG_COLUMN)) {
		int i;
		struct SrcCount *p = pWalker->u.pSrcCount;
		SrcList *pSrc = p->pSrc;
		int nSrc = pSrc ? pSrc->nSrc : 0;
		for (i = 0; i < nSrc; i++) {
			if (pExpr->iTable == pSrc->a[i].iCursor)
				break;
		}
		if (i < nSrc) {
			p->nThis++;
		} else {
			p->nOther++;
		}
	}
	return WRC_Continue;
}

/*
 * Determine if any of the arguments to the pExpr Function reference
 * pSrcList.  Return true if they do.  Also return true if the function
 * has no arguments or has only constant arguments.  Return false if pExpr
 * references columns but not columns of tables found in pSrcList.
 */
int
sqlFunctionUsesThisSrc(Expr * pExpr, SrcList * pSrcList)
{
	Walker w;
	struct SrcCount cnt;
	assert(pExpr->op == TK_AGG_FUNCTION);
	memset(&w, 0, sizeof(w));
	w.xExprCallback = exprSrcCount;
	w.u.pSrcCount = &cnt;
	cnt.pSrc = pSrcList;
	cnt.nThis = 0;
	cnt.nOther = 0;
	sqlWalkExprList(&w, pExpr->x.pList);
	return cnt.nThis > 0 || cnt.nOther == 0;
}

/*
 * Add a new element to the pAggInfo->aCol[] array.  Return the index of
 * the new element.  Return a negative number if malloc fails.
 */
static int
addAggInfoColumn(sql * db, AggInfo * pInfo)
{
	int i;
	pInfo->aCol = sqlArrayAllocate(db,
					   pInfo->aCol,
					   sizeof(pInfo->aCol[0]),
					   &pInfo->nColumn, &i);
	return i;
}

/*
 * Add a new element to the pAggInfo->aFunc[] array.  Return the index of
 * the new element.  Return a negative number if malloc fails.
 */
static int
addAggInfoFunc(sql * db, AggInfo * pInfo)
{
	int i;
	pInfo->aFunc = sqlArrayAllocate(db,
					    pInfo->aFunc,
					    sizeof(pInfo->aFunc[0]),
					    &pInfo->nFunc, &i);
	return i;
}

/*
 * This is the xExprCallback for a tree walker.  It is used to
 * implement sqlExprAnalyzeAggregates().  See sqlExprAnalyzeAggregates
 * for additional information.
 */
static int
analyzeAggregate(Walker * pWalker, Expr * pExpr)
{
	int i;
	NameContext *pNC = pWalker->u.pNC;
	Parse *pParse = pNC->pParse;
	SrcList *pSrcList = pNC->pSrcList;
	AggInfo *pAggInfo = pNC->pAggInfo;

	switch (pExpr->op) {
	case TK_AGG_COLUMN:
	case TK_COLUMN_REF:{
			testcase(pExpr->op == TK_AGG_COLUMN);
			testcase(pExpr->op == TK_COLUMN_REF);
			/* Check to see if the column is in one of the tables in the FROM
			 * clause of the aggregate query
			 */
			if (ALWAYS(pSrcList != 0)) {
				struct SrcList_item *pItem = pSrcList->a;
				for (i = 0; i < pSrcList->nSrc; i++, pItem++) {
					struct AggInfo_col *pCol;
					assert(!ExprHasProperty
					       (pExpr,
						EP_TokenOnly | EP_Reduced));
					if (pExpr->iTable == pItem->iCursor) {
						/* If we reach this point, it means that pExpr refers to a table
						 * that is in the FROM clause of the aggregate query.
						 *
						 * Make an entry for the column in pAggInfo->aCol[] if there
						 * is not an entry there already.
						 */
						int k;
						pCol = pAggInfo->aCol;
						for (k = 0;
						     k < pAggInfo->nColumn;
						     k++, pCol++) {
							if (pCol->iTable ==
							    pExpr->iTable
							    && pCol->iColumn ==
							    pExpr->iColumn) {
								break;
							}
						}
						if ((k >= pAggInfo->nColumn)
						    && (k =
							addAggInfoColumn
							(pParse->db,
							 pAggInfo)) >= 0) {
							pCol =
							    &pAggInfo->aCol[k];
							pCol->space_def =
							    pExpr->space_def;
							pCol->iTable =
							    pExpr->iTable;
							pCol->iColumn =
							    pExpr->iColumn;
							pCol->iMem =
							    ++pParse->nMem;
							pCol->iSorterColumn =
							    -1;
							pCol->pExpr = pExpr;
							if (pAggInfo->pGroupBy) {
								int j, n;
								ExprList *pGB =
								    pAggInfo->
								    pGroupBy;
								struct
								    ExprList_item
								    *pTerm =
								    pGB->a;
								n = pGB->nExpr;
								for (j = 0;
								     j < n;
								     j++,
								     pTerm++) {
									Expr *pE
									    =
									    pTerm->
									    pExpr;
									if (pE->
									    op
									    ==
									    TK_COLUMN_REF
									    &&
									    pE->
									    iTable
									    ==
									    pExpr->
									    iTable
									    &&
									    pE->
									    iColumn
									    ==
									    pExpr->
									    iColumn)
									{
										pCol->
										    iSorterColumn
										    =
										    j;
										break;
									}
								}
							}
							if (pCol->
							    iSorterColumn < 0) {
								pCol->
								    iSorterColumn
								    =
								    pAggInfo->
								    nSortingColumn++;
							}
						}
						/* There is now an entry for pExpr in pAggInfo->aCol[] (either
						 * because it was there before or because we just created it).
						 * Convert the pExpr to be a TK_AGG_COLUMN referring to that
						 * pAggInfo->aCol[] entry.
						 */
						ExprSetVVAProperty(pExpr,
								   EP_NoReduce);
						pExpr->pAggInfo = pAggInfo;
						pExpr->op = TK_AGG_COLUMN;
						pExpr->iAgg = (i16) k;
						break;
					}	/* endif pExpr->iTable==pItem->iCursor */
				}	/* end loop over pSrcList */
			}
			return WRC_Prune;
		}
	case TK_AGG_FUNCTION:{
			if ((pNC->ncFlags & NC_InAggFunc) == 0
			    && pWalker->walkerDepth == pExpr->op2) {
				/* Check to see if pExpr is a duplicate of another aggregate
				 * function that is already in the pAggInfo structure
				 */
				struct AggInfo_func *pItem = pAggInfo->aFunc;
				for (i = 0; i < pAggInfo->nFunc; i++, pItem++) {
					if (sqlExprCompare
					    (pItem->pExpr, pExpr, -1) == 0) {
						break;
					}
				}
				if (i >= pAggInfo->nFunc) {
					/* pExpr is original.  Make a new entry in pAggInfo->aFunc[]
					 */
					i = addAggInfoFunc(pParse->db,
							   pAggInfo);
					if (i >= 0) {
						assert(!ExprHasProperty
						       (pExpr, EP_xIsSelect));
						pItem = &pAggInfo->aFunc[i];
						pItem->pExpr = pExpr;
						pItem->iMem = ++pParse->nMem;
						assert(!ExprHasProperty
						       (pExpr, EP_IntValue));
						const char *name =
							pExpr->u.zToken;
						uint32_t argc =
							pExpr->x.pList != NULL ?
							pExpr->x.pList->nExpr : 0;
						pItem->func =
							sql_func_by_signature(
								name, argc);
						assert(pItem->func != NULL);
						assert(pItem->func->def->
						       language ==
						       FUNC_LANGUAGE_SQL_BUILTIN &&
						       pItem->func->def->
						       aggregate ==
						       FUNC_AGGREGATE_GROUP);
						if (pExpr->flags & EP_Distinct) {
							pItem->iDistinct =
								pParse->nTab++;
							pItem->reg_eph =
								++pParse->nMem;
						} else {
							pItem->iDistinct = -1;
						}
					}
				}
				/* Make pExpr point to the appropriate pAggInfo->aFunc[] entry
				 */
				assert(!ExprHasProperty
				       (pExpr, EP_TokenOnly | EP_Reduced));
				ExprSetVVAProperty(pExpr, EP_NoReduce);
				pExpr->iAgg = (i16) i;
				pExpr->pAggInfo = pAggInfo;
				return WRC_Prune;
			} else {
				return WRC_Continue;
			}
		}
	}
	return WRC_Continue;
}

static int
analyzeAggregatesInSelect(Walker * pWalker, Select * pSelect)
{
	UNUSED_PARAMETER(pWalker);
	UNUSED_PARAMETER(pSelect);
	return WRC_Continue;
}

/*
 * Analyze the pExpr expression looking for aggregate functions and
 * for variables that need to be added to AggInfo object that pNC->pAggInfo
 * points to.  Additional entries are made on the AggInfo object as
 * necessary.
 *
 * This routine should only be called after the expression has been
 * analyzed by sqlResolveExprNames().
 */
void
sqlExprAnalyzeAggregates(NameContext * pNC, Expr * pExpr)
{
	Walker w;
	memset(&w, 0, sizeof(w));
	w.xExprCallback = analyzeAggregate;
	w.xSelectCallback = analyzeAggregatesInSelect;
	w.u.pNC = pNC;
	assert(pNC->pSrcList != 0);
	sqlWalkExpr(&w, pExpr);
}

/*
 * Call sqlExprAnalyzeAggregates() for every expression in an
 * expression list.  Return the number of errors.
 *
 * If an error is found, the analysis is cut short.
 */
void
sqlExprAnalyzeAggList(NameContext * pNC, ExprList * pList)
{
	struct ExprList_item *pItem;
	int i;
	if (pList) {
		for (pItem = pList->a, i = 0; i < pList->nExpr; i++, pItem++) {
			sqlExprAnalyzeAggregates(pNC, pItem->pExpr);
		}
	}
}

/*
 * Allocate a single new register for use to hold some intermediate result.
 */
int
sqlGetTempReg(Parse * pParse)
{
	if (pParse->nTempReg == 0) {
		return ++pParse->nMem;
	}
	return pParse->aTempReg[--pParse->nTempReg];
}

/*
 * Deallocate a register, making available for reuse for some other
 * purpose.
 *
 * If a register is currently being used by the column cache, then
 * the deallocation is deferred until the column cache line that uses
 * the register becomes stale.
 */
void
sqlReleaseTempReg(Parse * pParse, int iReg)
{
	if (iReg && pParse->nTempReg < ArraySize(pParse->aTempReg)) {
		int i;
		struct yColCache *p;
		for (i = 0, p = pParse->aColCache; i < pParse->nColCache;
		     i++, p++) {
			if (p->iReg == iReg) {
				p->tempReg = 1;
				return;
			}
		}
		pParse->aTempReg[pParse->nTempReg++] = iReg;
	}
}

/*
 * Allocate or deallocate a block of nReg consecutive registers.
 */
int
sqlGetTempRange(Parse * pParse, int nReg)
{
	int i, n;
	if (nReg == 1)
		return sqlGetTempReg(pParse);
	i = pParse->iRangeReg;
	n = pParse->nRangeReg;
	if (nReg <= n) {
		assert(!usedAsColumnCache(pParse, i, i + n - 1));
		pParse->iRangeReg += nReg;
		pParse->nRangeReg -= nReg;
	} else {
		i = pParse->nMem + 1;
		pParse->nMem += nReg;
	}
	return i;
}

void
sqlReleaseTempRange(Parse * pParse, int iReg, int nReg)
{
	if (nReg == 1) {
		sqlReleaseTempReg(pParse, iReg);
		return;
	}
	sqlExprCacheRemove(pParse, iReg, nReg);
	if (nReg > pParse->nRangeReg) {
		pParse->nRangeReg = nReg;
		pParse->iRangeReg = iReg;
	}
}

/*
 * Mark all temporary registers as being unavailable for reuse.
 */
void
sqlClearTempRegCache(Parse * pParse)
{
	pParse->nTempReg = 0;
	pParse->nRangeReg = 0;
}

