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
 * This file contains routines used for walking the parser tree for
 * an SQL statement.
 */
#include "sqlInt.h"
#include <stdlib.h>
#include <string.h>

/*
 * Walk an expression tree.  Invoke the callback once for each node
 * of the expression, while descending.  (In other words, the callback
 * is invoked before visiting children.)
 *
 * The return value from the callback should be one of the WRC_*
 * constants to specify how to proceed with the walk.
 *
 *    WRC_Continue      Continue descending down the tree.
 *
 *    WRC_Prune         Do not descend into child nodes.  But allow
 *                      the walk to continue with sibling nodes.
 *
 *    WRC_Abort         Do no more callbacks.  Unwind the stack and
 *                      return the top-level walk call.
 *
 * The return value from this routine is WRC_Abort to abandon the tree walk
 * and WRC_Continue to continue.
 */
static SQL_NOINLINE int
walkExpr(Walker * pWalker, Expr * pExpr)
{
	int rc;
	testcase(ExprHasProperty(pExpr, EP_TokenOnly));
	testcase(ExprHasProperty(pExpr, EP_Reduced));
	rc = pWalker->xExprCallback(pWalker, pExpr);
	if (rc || ExprHasProperty(pExpr, (EP_TokenOnly | EP_Leaf))) {
		return rc & WRC_Abort;
	}
	if (pExpr->pLeft && walkExpr(pWalker, pExpr->pLeft))
		return WRC_Abort;
	if (pExpr->pRight && walkExpr(pWalker, pExpr->pRight))
		return WRC_Abort;
	if (ExprHasProperty(pExpr, EP_xIsSelect)) {
		if (sqlWalkSelect(pWalker, pExpr->x.pSelect))
			return WRC_Abort;
	} else if (pExpr->x.pList) {
		if (sqlWalkExprList(pWalker, pExpr->x.pList))
			return WRC_Abort;
	}
	return WRC_Continue;
}

int
sqlWalkExpr(Walker * pWalker, Expr * pExpr)
{
	return pExpr ? walkExpr(pWalker, pExpr) : WRC_Continue;
}

/*
 * Call sqlWalkExpr() for every expression in list p or until
 * an abort request is seen.
 */
int
sqlWalkExprList(Walker * pWalker, ExprList * p)
{
	int i;
	struct ExprList_item *pItem;
	if (p) {
		for (i = p->nExpr, pItem = p->a; i > 0; i--, pItem++) {
			if (sqlWalkExpr(pWalker, pItem->pExpr))
				return WRC_Abort;
		}
	}
	return WRC_Continue;
}

/*
 * Walk all expressions associated with SELECT statement p.  Do
 * not invoke the SELECT callback on p, but do (of course) invoke
 * any expr callbacks and SELECT callbacks that come from subqueries.
 * Return WRC_Abort or WRC_Continue.
 */
int
sqlWalkSelectExpr(Walker * pWalker, Select * p)
{
	if (sqlWalkExprList(pWalker, p->pEList))
		return WRC_Abort;
	if (sqlWalkExpr(pWalker, p->pWhere))
		return WRC_Abort;
	if (sqlWalkExprList(pWalker, p->pGroupBy))
		return WRC_Abort;
	if (sqlWalkExpr(pWalker, p->pHaving))
		return WRC_Abort;
	if (sqlWalkExprList(pWalker, p->pOrderBy))
		return WRC_Abort;
	if (sqlWalkExpr(pWalker, p->pLimit))
		return WRC_Abort;
	if (sqlWalkExpr(pWalker, p->pOffset))
		return WRC_Abort;
	return WRC_Continue;
}

/*
 * Walk the parse trees associated with all subqueries in the
 * FROM clause of SELECT statement p.  Do not invoke the select
 * callback on p, but do invoke it on each FROM clause subquery
 * and on any subqueries further down in the tree.  Return
 * WRC_Abort or WRC_Continue;
 */
int
sqlWalkSelectFrom(Walker * pWalker, Select * p)
{
	SrcList *pSrc;
	int i;
	struct SrcList_item *pItem;

	pSrc = p->pSrc;
	if (ALWAYS(pSrc)) {
		for (i = pSrc->nSrc, pItem = pSrc->a; i > 0; i--, pItem++) {
			if (sqlWalkSelect(pWalker, pItem->pSelect)) {
				return WRC_Abort;
			}
			if (pItem->fg.isTabFunc
			    && sqlWalkExprList(pWalker, pItem->u1.pFuncArg)
			    ) {
				return WRC_Abort;
			}
		}
	}
	return WRC_Continue;
}

/*
 * Call sqlWalkExpr() for every expression in Select statement p.
 * Invoke sqlWalkSelect() for subqueries in the FROM clause and
 * on the compound select chain, p->pPrior.
 *
 * If it is not NULL, the xSelectCallback() callback is invoked before
 * the walk of the expressions and FROM clause. The xSelectCallback2()
 * method, if it is not NULL, is invoked following the walk of the
 * expressions and FROM clause.
 *
 * Return WRC_Continue under normal conditions.  Return WRC_Abort if
 * there is an abort request.
 *
 * If the Walker does not have an xSelectCallback() then this routine
 * is a no-op returning WRC_Continue.
 */
int
sqlWalkSelect(Walker * pWalker, Select * p)
{
	int rc;
	if (p == 0
	    || (pWalker->xSelectCallback == 0
		&& pWalker->xSelectCallback2 == 0)) {
		return WRC_Continue;
	}
	rc = WRC_Continue;
	pWalker->walkerDepth++;
	while (p) {
		if (pWalker->xSelectCallback) {
			rc = pWalker->xSelectCallback(pWalker, p);
			if (rc)
				break;
		}
		if (sqlWalkSelectExpr(pWalker, p)
		    || sqlWalkSelectFrom(pWalker, p)
		    ) {
			pWalker->walkerDepth--;
			return WRC_Abort;
		}
		if (pWalker->xSelectCallback2) {
			pWalker->xSelectCallback2(pWalker, p);
		}
		p = p->pPrior;
	}
	pWalker->walkerDepth--;
	return rc & WRC_Abort;
}
