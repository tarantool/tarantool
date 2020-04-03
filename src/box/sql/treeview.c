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
 *
 * This file contains C code to implement the TreeView debugging routines.
 * These routines print a parse tree to standard output for debugging and
 * analysis.
 *
 * The interfaces in this file is only available when compiling
 * with SQL_DEBUG.
 */
#include "sqlInt.h"
#ifdef SQL_DEBUG

/*
 * Add a new subitem to the tree.  The moreToFollow flag indicates that this
 * is not the last item in the tree.
 */
static TreeView *
sqlTreeViewPush(TreeView * p, u8 moreToFollow)
{
	if (p == 0) {
		p = sql_malloc64(sizeof(*p));
		if (p == 0)
			return 0;
		memset(p, 0, sizeof(*p));
	} else {
		p->iLevel++;
	}
	assert(moreToFollow == 0 || moreToFollow == 1);
	if ((unsigned int)p->iLevel < sizeof(p->bLine))
		p->bLine[p->iLevel] = moreToFollow;
	return p;
}

/*
 * Finished with one layer of the tree
 */
static void
sqlTreeViewPop(TreeView * p)
{
	if (p == 0)
		return;
	p->iLevel--;
	if (p->iLevel < 0)
		sql_free(p);
}

/*
 * Generate a single line of output for the tree, with a prefix that contains
 * all the appropriate tree lines
 */
static void
sqlTreeViewLine(TreeView * p, const char *zFormat, ...)
{
	va_list ap;
	int i;
	StrAccum acc;
	char zBuf[500];
	sqlStrAccumInit(&acc, 0, zBuf, sizeof(zBuf), 0);
	if (p) {
		for (i = 0;
		     i < p->iLevel && (unsigned int)i < sizeof(p->bLine) - 1;
		     i++) {
			sqlStrAccumAppend(&acc,
					      p->bLine[i] ? "|   " : "    ", 4);
		}
		sqlStrAccumAppend(&acc, p->bLine[i] ? "|-- " : "'-- ", 4);
	}
	va_start(ap, zFormat);
	sqlVXPrintf(&acc, zFormat, ap);
	va_end(ap);
	assert(acc.nChar > 0);
	if (zBuf[acc.nChar - 1] != '\n')
		sqlStrAccumAppend(&acc, "\n", 1);
	sqlStrAccumFinish(&acc);
	fprintf(stdout, "%s", zBuf);
	fflush(stdout);
}

/*
 * Shorthand for starting a new tree item that consists of a single label
 */
static void
sqlTreeViewItem(TreeView * p, const char *zLabel, u8 moreFollows)
{
	p = sqlTreeViewPush(p, moreFollows);
	sqlTreeViewLine(p, "%s", zLabel);
}

/*
 * Generate a human-readable description of a WITH clause.
 */
void
sqlTreeViewWith(TreeView * pView, const With * pWith)
{
	int i;
	if (pWith == 0)
		return;
	if (pWith->nCte == 0)
		return;
	if (pWith->pOuter) {
		sqlTreeViewLine(pView, "WITH (0x%p, pOuter=0x%p)", pWith,
				    pWith->pOuter);
	} else {
		sqlTreeViewLine(pView, "WITH (0x%p)", pWith);
	}
	if (pWith->nCte > 0) {
		pView = sqlTreeViewPush(pView, 1);
		for (i = 0; i < pWith->nCte; i++) {
			StrAccum x;
			char zLine[1000];
			const struct Cte *pCte = &pWith->a[i];
			sqlStrAccumInit(&x, 0, zLine, sizeof(zLine), 0);
			sqlXPrintf(&x, "%s", pCte->zName);
			if (pCte->pCols && pCte->pCols->nExpr > 0) {
				char cSep = '(';
				int j;
				for (j = 0; j < pCte->pCols->nExpr; j++) {
					sqlXPrintf(&x, "%c%s", cSep,
						       pCte->pCols->a[j].zName);
					cSep = ',';
				}
				sqlXPrintf(&x, ")");
			}
			sqlXPrintf(&x, " AS");
			sqlStrAccumFinish(&x);
			sqlTreeViewItem(pView, zLine, i < pWith->nCte - 1);
			sqlTreeViewSelect(pView, pCte->pSelect, 0);
			sqlTreeViewPop(pView);
		}
		sqlTreeViewPop(pView);
	}
}

/*
 * Generate a human-readable description of a Select object.
 */
void
sqlTreeViewSelect(TreeView * pView, const Select * p, u8 moreToFollow)
{
	int n = 0;
	int cnt = 0;
	pView = sqlTreeViewPush(pView, moreToFollow);
	if (p->pWith) {
		sqlTreeViewWith(pView, p->pWith);
		cnt = 1;
		sqlTreeViewPush(pView, 1);
	}
	do {
		sqlTreeViewLine(pView,
				    "SELECT%s%s (0x%p) selFlags=0x%x nSelectRow=%d",
				    ((p->
				      selFlags & SF_Distinct) ? " DISTINCT" :
				     ""),
				    ((p->
				      selFlags & SF_Aggregate) ? " agg_flag" :
				     ""), p, p->selFlags, (int)p->nSelectRow);
		if (cnt++)
			sqlTreeViewPop(pView);
		if (p->pPrior) {
			n = 1000;
		} else {
			n = 0;
			if (p->pSrc && p->pSrc->nSrc)
				n++;
			if (p->pWhere)
				n++;
			if (p->pGroupBy)
				n++;
			if (p->pHaving)
				n++;
			if (p->pOrderBy)
				n++;
			if (p->pLimit)
				n++;
			if (p->pOffset)
				n++;
		}
		sqlTreeViewExprList(pView, p->pEList, (n--) > 0,
					"result-set");
		if (p->pSrc && p->pSrc->nSrc) {
			int i;
			pView = sqlTreeViewPush(pView, (n--) > 0);
			sqlTreeViewLine(pView, "FROM");
			for (i = 0; i < p->pSrc->nSrc; i++) {
				struct SrcList_item *pItem = &p->pSrc->a[i];
				StrAccum x;
				char zLine[100];
				sqlStrAccumInit(&x, 0, zLine, sizeof(zLine),
						    0);
				sqlXPrintf(&x, "{%d,*}", pItem->iCursor);
				if (pItem->zName) {
					sqlXPrintf(&x, " %s", pItem->zName);
				}
				if (pItem->space != NULL) {
					sqlXPrintf(&x, " tabname=%Q",
						       pItem->space->def->name);
				}
				if (pItem->zAlias) {
					sqlXPrintf(&x, " (AS %s)",
						       pItem->zAlias);
				}
				if (pItem->fg.jointype & JT_LEFT) {
					sqlXPrintf(&x, " LEFT-JOIN");
				}
				sqlStrAccumFinish(&x);
				sqlTreeViewItem(pView, zLine,
						    i < p->pSrc->nSrc - 1);
				if (pItem->pSelect) {
					sqlTreeViewSelect(pView,
							      pItem->pSelect,
							      0);
				}
				if (pItem->fg.isTabFunc) {
					sqlTreeViewExprList(pView,
								pItem->u1.
								pFuncArg, 0,
								"func-args:");
				}
				sqlTreeViewPop(pView);
			}
			sqlTreeViewPop(pView);
		}
		if (p->pWhere) {
			sqlTreeViewItem(pView, "WHERE", (n--) > 0);
			sqlTreeViewExpr(pView, p->pWhere, 0);
			sqlTreeViewPop(pView);
		}
		if (p->pGroupBy) {
			sqlTreeViewExprList(pView, p->pGroupBy, (n--) > 0,
						"GROUPBY");
		}
		if (p->pHaving) {
			sqlTreeViewItem(pView, "HAVING", (n--) > 0);
			sqlTreeViewExpr(pView, p->pHaving, 0);
			sqlTreeViewPop(pView);
		}
		if (p->pOrderBy) {
			sqlTreeViewExprList(pView, p->pOrderBy, (n--) > 0,
						"ORDERBY");
		}
		if (p->pLimit) {
			sqlTreeViewItem(pView, "LIMIT", (n--) > 0);
			sqlTreeViewExpr(pView, p->pLimit, 0);
			sqlTreeViewPop(pView);
		}
		if (p->pOffset) {
			sqlTreeViewItem(pView, "OFFSET", (n--) > 0);
			sqlTreeViewExpr(pView, p->pOffset, 0);
			sqlTreeViewPop(pView);
		}
		if (p->pPrior) {
			const char *zOp = "UNION";
			switch (p->op) {
			case TK_ALL:
				zOp = "UNION ALL";
				break;
			case TK_INTERSECT:
				zOp = "INTERSECT";
				break;
			case TK_EXCEPT:
				zOp = "EXCEPT";
				break;
			}
			sqlTreeViewItem(pView, zOp, 1);
		}
		p = p->pPrior;
	} while (p != 0);
	sqlTreeViewPop(pView);
}

/*
 * Generate a human-readable explanation of an expression tree.
 */
void
sqlTreeViewExpr(TreeView * pView, const Expr * pExpr, u8 moreToFollow)
{
	const char *zBinOp = 0;	/* Binary operator */
	const char *zUniOp = 0;	/* Unary operator */
	char zFlgs[30];
	pView = sqlTreeViewPush(pView, moreToFollow);
	if (pExpr == 0) {
		sqlTreeViewLine(pView, "nil");
		sqlTreeViewPop(pView);
		return;
	}
	if (pExpr->flags) {
		sql_snprintf(sizeof(zFlgs), zFlgs, "  flags=0x%x",
				 pExpr->flags);
	} else {
		zFlgs[0] = 0;
	}
	switch (pExpr->op) {
	case TK_AGG_COLUMN:{
			sqlTreeViewLine(pView, "AGG{%d:%d}%s",
					    pExpr->iTable, pExpr->iColumn,
					    zFlgs);
			break;
		}
	case TK_COLUMN_REF:{
			if (pExpr->iTable < 0) {
				/* This only happens when coding check constraints */
				sqlTreeViewLine(pView, "COLUMN(%d)%s",
						    pExpr->iColumn, zFlgs);
			} else {
				sqlTreeViewLine(pView, "{%d:%d}%s",
						    pExpr->iTable,
						    pExpr->iColumn, zFlgs);
			}
			break;
		}
	case TK_INTEGER:{
			if (pExpr->flags & EP_IntValue) {
				sqlTreeViewLine(pView, "%d",
						    pExpr->u.iValue);
			} else {
				sqlTreeViewLine(pView, "%s",
						    pExpr->u.zToken);
			}
			break;
		}
	case TK_FLOAT:{
			sqlTreeViewLine(pView, "%s", pExpr->u.zToken);
			break;
		}
	case TK_STRING:{
			sqlTreeViewLine(pView, "%Q", pExpr->u.zToken);
			break;
		}
	case TK_NULL:{
			sqlTreeViewLine(pView, "NULL");
			break;
		}
#ifndef SQL_OMIT_BLOB_LITERAL
	case TK_BLOB:{
			sqlTreeViewLine(pView, "%s", pExpr->u.zToken);
			break;
		}
#endif
	case TK_VARIABLE:{
			sqlTreeViewLine(pView, "VARIABLE(%s,%d)",
					    pExpr->u.zToken, pExpr->iColumn);
			break;
		}
	case TK_REGISTER:{
			sqlTreeViewLine(pView, "REGISTER(%d)",
					    pExpr->iTable);
			break;
		}
	case TK_ID:{
			sqlTreeViewLine(pView, "ID \"%w\"",
					    pExpr->u.zToken);
			break;
		}

	case TK_CAST:{
			/* Expressions of the form:   CAST(pLeft AS token) */
			sqlTreeViewLine(pView, "CAST %Q", pExpr->u.zToken);
			sqlTreeViewExpr(pView, pExpr->pLeft, 0);
			break;
		}

	case TK_LT:
		zBinOp = "LT";
		break;
	case TK_LE:
		zBinOp = "LE";
		break;
	case TK_GT:
		zBinOp = "GT";
		break;
	case TK_GE:
		zBinOp = "GE";
		break;
	case TK_NE:
		zBinOp = "NE";
		break;
	case TK_EQ:
		zBinOp = "EQ";
		break;
	case TK_AND:
		zBinOp = "AND";
		break;
	case TK_OR:
		zBinOp = "OR";
		break;
	case TK_PLUS:
		zBinOp = "ADD";
		break;
	case TK_STAR:
		zBinOp = "MUL";
		break;
	case TK_MINUS:
		zBinOp = "SUB";
		break;
	case TK_REM:
		zBinOp = "REM";
		break;
	case TK_BITAND:
		zBinOp = "BITAND";
		break;
	case TK_BITOR:
		zBinOp = "BITOR";
		break;
	case TK_SLASH:
		zBinOp = "DIV";
		break;
	case TK_LSHIFT:
		zBinOp = "LSHIFT";
		break;
	case TK_RSHIFT:
		zBinOp = "RSHIFT";
		break;
	case TK_CONCAT:
		zBinOp = "CONCAT";
		break;
	case TK_DOT:
		zBinOp = "DOT";
		break;

	case TK_UMINUS:
		zUniOp = "UMINUS";
		break;
	case TK_UPLUS:
		zUniOp = "UPLUS";
		break;
	case TK_BITNOT:
		zUniOp = "BITNOT";
		break;
	case TK_NOT:
		zUniOp = "NOT";
		break;
	case TK_ISNULL:
		zUniOp = "IS NULL";
		break;
	case TK_NOTNULL:
		zUniOp = "NOT NULL";
		break;

	case TK_SPAN:{
			sqlTreeViewLine(pView, "SPAN %Q", pExpr->u.zToken);
			sqlTreeViewExpr(pView, pExpr->pLeft, 0);
			break;
		}

	case TK_COLLATE:{
			sqlTreeViewLine(pView, "COLLATE %Q",
					    pExpr->u.zToken);
			sqlTreeViewExpr(pView, pExpr->pLeft, 0);
			break;
		}

	case TK_AGG_FUNCTION:
	case TK_FUNCTION:{
			ExprList *pFarg;	/* List of function arguments */
			if (ExprHasProperty(pExpr, EP_TokenOnly)) {
				pFarg = 0;
			} else {
				pFarg = pExpr->x.pList;
			}
			if (pExpr->op == TK_AGG_FUNCTION) {
				sqlTreeViewLine(pView, "AGG_FUNCTION%d %Q",
						    pExpr->op2,
						    pExpr->u.zToken);
			} else {
				sqlTreeViewLine(pView, "FUNCTION %Q",
						    pExpr->u.zToken);
			}
			if (pFarg) {
				sqlTreeViewExprList(pView, pFarg, 0, 0);
			}
			break;
		}
	case TK_EXISTS:{
			sqlTreeViewLine(pView, "EXISTS-expr");
			sqlTreeViewSelect(pView, pExpr->x.pSelect, 0);
			break;
		}
	case TK_SELECT:{
			sqlTreeViewLine(pView, "SELECT-expr");
			sqlTreeViewSelect(pView, pExpr->x.pSelect, 0);
			break;
		}
	case TK_IN:{
			sqlTreeViewLine(pView, "IN");
			sqlTreeViewExpr(pView, pExpr->pLeft, 1);
			if (ExprHasProperty(pExpr, EP_xIsSelect)) {
				sqlTreeViewSelect(pView, pExpr->x.pSelect,
						      0);
			} else {
				sqlTreeViewExprList(pView, pExpr->x.pList,
							0, 0);
			}
			break;
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
			Expr *pX = pExpr->pLeft;
			Expr *pY = pExpr->x.pList->a[0].pExpr;
			Expr *pZ = pExpr->x.pList->a[1].pExpr;
			sqlTreeViewLine(pView, "BETWEEN");
			sqlTreeViewExpr(pView, pX, 1);
			sqlTreeViewExpr(pView, pY, 1);
			sqlTreeViewExpr(pView, pZ, 0);
			break;
		}
	case TK_TRIGGER:{
			/* If the opcode is TK_TRIGGER, then the expression is a reference
			 * to a column in the new.* or old.* pseudo-tables available to
			 * trigger programs. In this case Expr.iTable is set to 1 for the
			 * new.* pseudo-table, or 0 for the old.* pseudo-table. Expr.iColumn
			 * is set to the column of the pseudo-table to read, or to -1 to
			 * read the rowid field.
			 */
			sqlTreeViewLine(pView, "%s(%d)",
					    pExpr->iTable ? "NEW" : "OLD",
					    pExpr->iColumn);
			break;
		}
	case TK_CASE:{
			sqlTreeViewLine(pView, "CASE");
			sqlTreeViewExpr(pView, pExpr->pLeft, 1);
			sqlTreeViewExprList(pView, pExpr->x.pList, 0, 0);
			break;
		}
	case TK_RAISE:{
			const char *zType;
			switch (pExpr->on_conflict_action) {
			case ON_CONFLICT_ACTION_ROLLBACK:
				zType = "rollback";
				break;
			case ON_CONFLICT_ACTION_ABORT:
				zType = "abort";
				break;
			case ON_CONFLICT_ACTION_FAIL:
				zType = "fail";
				break;
			case ON_CONFLICT_ACTION_IGNORE:
				zType = "ignore";
				break;
			default:
				unreachable();
			}
			sqlTreeViewLine(pView, "RAISE %s(%Q)", zType,
					    pExpr->u.zToken);
			break;
		}
	case TK_MATCH:{
			sqlTreeViewLine(pView, "MATCH {%d:%d}%s",
					    pExpr->iTable, pExpr->iColumn,
					    zFlgs);
			sqlTreeViewExpr(pView, pExpr->pRight, 0);
			break;
		}
	case TK_VECTOR:{
			sqlTreeViewBareExprList(pView, pExpr->x.pList,
						    "VECTOR");
			break;
		}
	case TK_SELECT_COLUMN:{
			sqlTreeViewLine(pView, "SELECT-COLUMN %d",
					    pExpr->iColumn);
			sqlTreeViewSelect(pView, pExpr->pLeft->x.pSelect,
					      0);
			break;
		}
	default:{
			sqlTreeViewLine(pView, "op=%d", pExpr->op);
			break;
		}
	}
	if (zBinOp) {
		sqlTreeViewLine(pView, "%s%s", zBinOp, zFlgs);
		sqlTreeViewExpr(pView, pExpr->pLeft, 1);
		sqlTreeViewExpr(pView, pExpr->pRight, 0);
	} else if (zUniOp) {
		sqlTreeViewLine(pView, "%s%s", zUniOp, zFlgs);
		sqlTreeViewExpr(pView, pExpr->pLeft, 0);
	}
	sqlTreeViewPop(pView);
}

/*
 * Generate a human-readable explanation of an expression list.
 */
void
sqlTreeViewBareExprList(TreeView * pView,
			    const ExprList * pList, const char *zLabel)
{
	if (zLabel == 0 || zLabel[0] == 0)
		zLabel = "LIST";
	if (pList == 0) {
		sqlTreeViewLine(pView, "%s (empty)", zLabel);
	} else {
		int i;
		sqlTreeViewLine(pView, "%s", zLabel);
		for (i = 0; i < pList->nExpr; i++) {
			int j = pList->a[i].u.x.iOrderByCol;
			if (j) {
				sqlTreeViewPush(pView, 0);
				sqlTreeViewLine(pView, "iOrderByCol=%d", j);
			}
			sqlTreeViewExpr(pView, pList->a[i].pExpr,
					    i < pList->nExpr - 1);
			if (j)
				sqlTreeViewPop(pView);
		}
	}
}

void
sqlTreeViewExprList(TreeView * pView,
			const ExprList * pList,
			u8 moreToFollow, const char *zLabel)
{
	pView = sqlTreeViewPush(pView, moreToFollow);
	sqlTreeViewBareExprList(pView, pList, zLabel);
	sqlTreeViewPop(pView);
}

#endif				/* SQL_DEBUG */
