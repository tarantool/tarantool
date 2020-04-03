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
 * This file contains routines used for walking the parser tree and
 * resolve all identifiers by associating them with a particular
 * table and column.
 */
#include "sqlInt.h"
#include <stdlib.h>
#include <string.h>
#include "box/schema.h"

/*
 * Walk the expression tree pExpr and increase the aggregate function
 * depth (the Expr.op2 field) by N on every TK_AGG_FUNCTION node.
 * This needs to occur when copying a TK_AGG_FUNCTION node from an
 * outer query into an inner subquery.
 *
 * incrAggFunctionDepth(pExpr,n) is the main routine.  incrAggDepth(..)
 * is a helper function - a callback for the tree walker.
 */
static int
incrAggDepth(Walker * pWalker, Expr * pExpr)
{
	if (pExpr->op == TK_AGG_FUNCTION)
		pExpr->op2 += pWalker->u.n;
	return WRC_Continue;
}

static void
incrAggFunctionDepth(Expr * pExpr, int N)
{
	if (N > 0) {
		Walker w;
		memset(&w, 0, sizeof(w));
		w.xExprCallback = incrAggDepth;
		w.u.n = N;
		sqlWalkExpr(&w, pExpr);
	}
}

/*
 * Turn the pExpr expression into an alias for the iCol-th column of the
 * result set in pEList.
 *
 * If the reference is followed by a COLLATE operator, then make sure
 * the COLLATE operator is preserved.  For example:
 *
 *     SELECT a+b, c+d FROM t1 ORDER BY 1 COLLATE nocase;
 *
 * Should be transformed into:
 *
 *     SELECT a+b, c+d FROM t1 ORDER BY (a+b) COLLATE nocase;
 *
 * The nSubquery parameter specifies how many levels of subquery the
 * alias is removed from the original expression.  The usual value is
 * zero but it might be more if the alias is contained within a subquery
 * of the original expression.  The Expr.op2 field of TK_AGG_FUNCTION
 * structures must be increased by the nSubquery amount.
 */
static void
resolveAlias(Parse * pParse,	/* Parsing context */
	     ExprList * pEList,	/* A result set */
	     int iCol,		/* A column in the result set.  0..pEList->nExpr-1 */
	     Expr * pExpr,	/* Transform this into an alias to the result set */
	     const char *zType,	/* "GROUP" or "ORDER" or "" */
	     int nSubquery	/* Number of subqueries that the label is moving */
    )
{
	Expr *pOrig;		/* The iCol-th column of the result set */
	Expr *pDup;		/* Copy of pOrig */
	sql *db;		/* The database connection */

	assert(iCol >= 0 && iCol < pEList->nExpr);
	pOrig = pEList->a[iCol].pExpr;
	assert(pOrig != 0);
	db = pParse->db;
	pDup = sqlExprDup(db, pOrig, 0);
	if (pDup == 0)
		return;
	if (zType[0] != 'G')
		incrAggFunctionDepth(pDup, nSubquery);
	if (pExpr->op == TK_COLLATE) {
		pDup =
		    sqlExprAddCollateString(pParse, pDup, pExpr->u.zToken);
	}
	ExprSetProperty(pDup, EP_Alias);

	/* Before calling sql_expr_delete(), set the EP_Static flag. This
	 * prevents ExprDelete() from deleting the Expr structure itself,
	 * allowing it to be repopulated by the memcpy() on the following line.
	 * The pExpr->u.zToken might point into memory that will be freed by the
	 * sqlDbFree(db, pDup) on the last line of this block, so be sure to
	 * make a copy of the token before doing the sqlDbFree().
	 */
	ExprSetProperty(pExpr, EP_Static);
	sql_expr_delete(db, pExpr, false);
	memcpy(pExpr, pDup, sizeof(*pExpr));
	if (!ExprHasProperty(pExpr, EP_IntValue) && pExpr->u.zToken != 0) {
		assert((pExpr->flags & (EP_Reduced | EP_TokenOnly)) == 0);
		pExpr->u.zToken = sqlDbStrDup(db, pExpr->u.zToken);
		pExpr->flags |= EP_MemToken;
	}
	sqlDbFree(db, pDup);
}

/*
 * Return TRUE if the name zCol occurs anywhere in the USING clause.
 *
 * Return FALSE if the USING clause is NULL or if it does not contain
 * zCol.
 */
static int
nameInUsingClause(IdList * pUsing, const char *zCol)
{
	if (pUsing) {
		int k;
		for (k = 0; k < pUsing->nId; k++) {
			if (strcmp(pUsing->a[k].zName, zCol) == 0)
				return 1;
		}
	}
	return 0;
}

/*
 * Subqueries stores the original database, table and column names for their
 * result sets in ExprList.a[].zSpan, in the form "DATABASE.TABLE.COLUMN".
 * Check to see if the zSpan given to this routine matches the zTab,
 * and zCol.  If any of zTab, and zCol are NULL then those fields will
 * match anything.
 */
int
sqlMatchSpanName(const char *zSpan,
		     const char *zCol, const char *zTab
	)
{
	int n;
	for (n = 0; ALWAYS(zSpan[n]) && zSpan[n] != '.'; n++) {
	}
	if (zTab && (sqlStrNICmp(zSpan, zTab, n) != 0 || zTab[n] != 0)) {
		return 0;
	}
	zSpan += n + 1;
	if (zCol && strcmp(zSpan, zCol) != 0) {
		return 0;
	}
	return 1;
}

/*
 * Given the name of a column of the form X.Y.Z or Y.Z or just Z, look up
 * that name in the set of source tables in pSrcList and make the pExpr
 * expression node refer back to that source column.  The following changes
 * are made to pExpr:
 *
 *    pExpr->iTable        Set to the cursor number for the table obtained
 *                         from pSrcList.
 *    pExpr->space_def     Points to the space_def structure of X.Y
 *                         (even if X and/or Y are implied.)
 *    pExpr->iColumn       Set to the column number within the table.
 *    pExpr->op            Set to TK_COLUMN_REF.
 *    pExpr->pLeft         Any expression this points to is deleted
 *    pExpr->pRight        Any expression this points to is deleted.
 *
 * Name is of the form Y.Z or Z.
 * The zTable variable is the name of the table (the "Y").  This
 * value can be NULL.  If zTable is NULL it
 * means that the form of the name is Z and that columns from any table
 * can be used.
 *
 * If the name cannot be resolved unambiguously, leave an error message
 * in pParse and return WRC_Abort.  Return WRC_Prune on success.
 */
static int
lookupName(Parse * pParse,	/* The parsing context */
	   const char *zTab,	/* Name of table containing column, or NULL */
	   const char *zCol,	/* Name of the column. */
	   NameContext * pNC,	/* The name context used to resolve the name */
	   Expr * pExpr		/* Make this EXPR node point to the selected column */
    )
{
	int i, j;		/* Loop counters */
	int cnt = 0;		/* Number of matching column names */
	int cntTab = 0;		/* Number of matching table names */
	int nSubquery = 0;	/* How many levels of subquery */
	sql *db = pParse->db;	/* The database connection */
	struct SrcList_item *pItem;	/* Use for looping over pSrcList items */
	struct SrcList_item *pMatch = 0;	/* The matching pSrcList item */
	NameContext *pTopNC = pNC;	/* First namecontext in the list */
	int isTrigger = 0;	/* True if resolved to a trigger column */

	assert(pNC);		/* the name context cannot be NULL. */
	assert(zCol);		/* The Z in X.Y.Z cannot be NULL */
	assert(!ExprHasProperty(pExpr, EP_TokenOnly | EP_Reduced));

	/* Initialize the node to no-match */
	pExpr->iTable = -1;
	pExpr->space_def = NULL;
	ExprSetVVAProperty(pExpr, EP_NoReduce);

	/* Start at the inner-most context and move outward until a match is found */
	while (pNC && cnt == 0) {
		ExprList *pEList;
		SrcList *pSrcList = pNC->pSrcList;

		if (pSrcList) {
			for (i = 0, pItem = pSrcList->a; i < pSrcList->nSrc;
			     i++, pItem++) {
				assert(pItem->space != NULL &&
				       pItem->space->def->name != NULL);
				struct space_def *space_def = pItem->space->def;
				assert(space_def->field_count > 0);
				if (pItem->pSelect
				    && (pItem->pSelect->
					selFlags & SF_NestedFrom) != 0) {
					int hit = 0;
					pEList = pItem->pSelect->pEList;
					for (j = 0; j < pEList->nExpr; j++) {
						if (sqlMatchSpanName
						    (pEList->a[j].zSpan, zCol,
						     zTab)) {
							cnt++;
							cntTab = 2;
							pMatch = pItem;
							pExpr->iColumn = j;
							hit = 1;
						}
					}
					if (hit || zTab == 0)
						continue;
				}
				if (zTab) {
					const char *zTabName =
					    pItem->zAlias ? pItem->
					    zAlias : space_def->name;
					assert(zTabName != 0);
					if (strcmp(zTabName, zTab) != 0) {
						continue;
					}
				}
				if (0 == (cntTab++)) {
					pMatch = pItem;
				}
				for (j = 0; j < (int)space_def->field_count;
				     j++) {
					if (strcmp(space_def->fields[j].name,
						   zCol) == 0) {
						/* If there has been exactly one prior match and this match
						 * is for the right-hand table of a NATURAL JOIN or is in a
						 * USING clause, then skip this match.
						 */
						if (cnt == 1) {
							if (pItem->fg.
							    jointype &
							    JT_NATURAL)
								continue;
							if (nameInUsingClause
							    (pItem->pUsing,
							     zCol))
								continue;
						}
						cnt++;
						pMatch = pItem;
						pExpr->iColumn = (i16) j;
						break;
					}
				}
			}
			if (pMatch) {
				pExpr->iTable = pMatch->iCursor;
				pExpr->space_def = pMatch->space->def;
				/* RIGHT JOIN not (yet) supported */
				assert((pMatch->fg.jointype & JT_RIGHT) == 0);
				if ((pMatch->fg.jointype & JT_LEFT) != 0) {
					ExprSetProperty(pExpr, EP_CanBeNull);
				}
			}
		}
		/* if( pSrcList ) */
		/* If we have not already resolved the name, then maybe
		 * it is a new.* or old.* trigger argument reference
		 */
		if (zTab != NULL && cntTab == 0 &&
		    pParse->triggered_space != NULL) {
			int op = pParse->eTriggerOp;
			assert(op == TK_DELETE || op == TK_UPDATE
			       || op == TK_INSERT);
			struct space_def *space_def = NULL;
			if (op != TK_DELETE && sqlStrICmp("new", zTab) == 0) {
				pExpr->iTable = 1;
				space_def = pParse->triggered_space->def;
			} else if (op != TK_INSERT
				   && sqlStrICmp("old", zTab) == 0) {
				pExpr->iTable = 0;
				space_def = pParse->triggered_space->def;
			}

			if (space_def != NULL) {
				int iCol;
				cntTab++;
				for (iCol = 0; iCol <
				     (int)space_def->field_count; iCol++) {
					if (strcmp(space_def->fields[iCol].name,
						   zCol) == 0) {
						break;
					}
				}
				if (iCol < (int)space_def->field_count) {
					cnt++;
					uint64_t *mask = pExpr->iTable == 0 ?
							 &pParse->oldmask :
							 &pParse->newmask;
					column_mask_set_fieldno(mask, iCol);
					pExpr->iColumn = iCol;
					pExpr->space_def = space_def;
					isTrigger = 1;
				}
			}
		}

		/*
		 * If the input is of the form Z (not Y.Z or X.Y.Z) then the name Z
		 * might refer to an result-set alias.  This happens, for example, when
		 * we are resolving names in the WHERE clause of the following command:
		 *
		 *     SELECT a+b AS x FROM table WHERE x<10;
		 *
		 * In cases like this, replace pExpr with a copy of the expression that
		 * forms the result set entry ("a+b" in the example) and return immediately.
		 * Note that the expression in the result set should have already been
		 * resolved by the time the WHERE clause is resolved.
		 *
		 * The ability to use an output result-set column in the WHERE, GROUP BY,
		 * or HAVING clauses, or as part of a larger expression in the ORDER BY
		 * clause is not standard SQL.  This is a (goofy) sql extension, that
		 * is supported for backwards compatibility only.
		 */
		if ((pEList = pNC->pEList) != 0 && zTab == 0 && cnt == 0) {
			for (j = 0; j < pEList->nExpr; j++) {
				char *zAs = pEList->a[j].zName;
				if (zAs != 0 && strcmp(zAs, zCol) == 0) {
					Expr *pOrig;
					assert(pExpr->pLeft == 0
					       && pExpr->pRight == 0);
					assert(pExpr->x.pList == 0);
					assert(pExpr->x.pSelect == 0);
					pOrig = pEList->a[j].pExpr;
					const char *err = "misuse of aliased "\
							  "aggregate %s";
					if ((pNC->ncFlags & NC_AllowAgg) == 0
					    && ExprHasProperty(pOrig, EP_Agg)) {
						diag_set(ClientError,
							 ER_SQL_PARSER_GENERIC,
							 tt_sprintf(err, zAs));
						pParse->is_aborted = true;
						return WRC_Abort;
					}
					if (sqlExprVectorSize(pOrig) != 1) {
						diag_set(ClientError,
							 ER_SQL_PARSER_GENERIC,
							 "row value misused");
						pParse->is_aborted = true;
						return WRC_Abort;
					}
					resolveAlias(pParse, pEList, j, pExpr,
						     "", nSubquery);
					cnt = 1;
					pMatch = 0;
					assert(zTab == 0);
					goto lookupname_end;
				}
			}
		}

		/* Advance to the next name context.  The loop will exit when either
		 * we have a match (cnt>0) or when we run out of name contexts.
		 */
		if (cnt == 0) {
			pNC = pNC->pNext;
			nSubquery++;
		}
	}

	/*
	 * cnt==0 means there was not match.  cnt>1 means there were two or
	 * more matches.  Either way, we have an error.
	 */
	if (cnt > 1) {
		const char *err;
		if (zTab) {
			err = tt_sprintf("ambiguous column name: %s.%s", zTab,
					 zCol);
		} else {
			err = tt_sprintf("ambiguous column name: %s", zCol);
		}
		diag_set(ClientError, ER_SQL_PARSER_GENERIC, err);
		pParse->is_aborted = true;
		pTopNC->nErr++;
	}
	if (cnt == 0) {
		if (zTab == NULL) {
			diag_set(ClientError, ER_SQL_CANT_RESOLVE_FIELD, zCol);
		} else {
			diag_set(ClientError, ER_NO_SUCH_FIELD_NAME_IN_SPACE,
				 zCol, zTab);
		}
		pParse->is_aborted = true;
		pTopNC->nErr++;
	}

	/* If a column from a table in pSrcList is referenced, then record
	 * this fact in the pSrcList.a[].colUsed bitmask.  Column 0 causes
	 * bit 0 to be set.  Column 1 sets bit 1.  And so forth.  If the
	 * column number is greater than the number of bits in the bitmask
	 * then set the high-order bit of the bitmask.
	 */
	if (pExpr->iColumn >= 0 && pMatch != 0) {
		int n = pExpr->iColumn;
		testcase(n == BMS - 1);
		if (n >= BMS) {
			n = BMS - 1;
		}
		assert(pMatch->iCursor == pExpr->iTable);
		pMatch->colUsed |= ((Bitmask) 1) << n;
	}

	/* Clean up and return
	 */
	sql_expr_delete(db, pExpr->pLeft, false);
	pExpr->pLeft = 0;
	sql_expr_delete(db, pExpr->pRight, false);
	pExpr->pRight = 0;
	pExpr->op = (isTrigger ? TK_TRIGGER : TK_COLUMN_REF);
 lookupname_end:
	if (cnt == 1) {
		assert(pNC != 0);
		/* Increment the nRef value on all name contexts from TopNC up to
		 * the point where the name matched.
		 */
		for (;;) {
			assert(pTopNC != 0);
			pTopNC->nRef++;
			if (pTopNC == pNC)
				break;
			pTopNC = pTopNC->pNext;
		}
		return WRC_Prune;
	} else {
		return WRC_Abort;
	}
}

struct Expr *
sql_expr_new_column(struct sql *db, struct SrcList *src_list, int src_idx,
		    int column)
{
	struct Expr *expr = sql_expr_new_anon(db, TK_COLUMN_REF);
	if (expr == NULL)
		return NULL;
	struct SrcList_item *item = &src_list->a[src_idx];
	expr->space_def = item->space->def;
	expr->iTable = item->iCursor;
	expr->iColumn = column;
	item->colUsed |= ((Bitmask) 1) << (column >= BMS ? BMS - 1 : column);
	ExprSetProperty(expr, EP_Resolved);
	return expr;
}

/*
 * Expression p should encode a floating point value between 1.0 and 0.0.
 * Return 1024 times this value.  Or return -1 if p is not a floating point
 * value between 1.0 and 0.0.
 */
static int
exprProbability(Expr * p)
{
	double r = -1.0;
	if (p->op != TK_FLOAT)
		return -1;
	sqlAtoF(p->u.zToken, &r, sqlStrlen30(p->u.zToken));
	assert(r >= 0.0);
	if (r > 1.0)
		return -1;
	return (int)(r * 134217728.0);
}

/*
 * This routine is callback for sqlWalkExpr().
 *
 * Resolve symbolic names into TK_COLUMN_REF operators for the current
 * node in the expression tree.  Return 0 to continue the search down
 * the tree or 2 to abort the tree walk.
 *
 * This routine also does error checking and name resolution for
 * function names.  The operator for aggregate functions is changed
 * to TK_AGG_FUNCTION.
 */
static int
resolveExprStep(Walker * pWalker, Expr * pExpr)
{
	NameContext *pNC;
	Parse *pParse;

	pNC = pWalker->u.pNC;
	assert(pNC != 0);
	pParse = pNC->pParse;
	assert(pParse == pWalker->pParse);

	if (ExprHasProperty(pExpr, EP_Resolved))
		return WRC_Prune;
	ExprSetProperty(pExpr, EP_Resolved);
#ifndef NDEBUG
	if (pNC->pSrcList && pNC->pSrcList->nAlloc > 0) {
		SrcList *pSrcList = pNC->pSrcList;
		int i;
		for (i = 0; i < pNC->pSrcList->nSrc; i++) {
			assert(pSrcList->a[i].iCursor >= 0
			       && pSrcList->a[i].iCursor < pParse->nTab);
		}
	}
#endif
	switch (pExpr->op) {
		/* A lone identifier is the name of a column.
		 */
	case TK_ID:{
			if ((pNC->ncFlags & NC_AllowAgg) != 0)
				pNC->ncFlags |= NC_HasUnaggregatedId;
			return lookupName(pParse, 0, pExpr->u.zToken, pNC,
					  pExpr);
		}

		/* A table name and column name:     ID.ID
		 * Or a database, table and column:  ID.ID.ID
		 */
	case TK_DOT:{
			const char *zColumn;
			const char *zTable;
			Expr *pRight;

			/* if( pSrcList==0 ) break; */
			if (pNC->ncFlags & NC_IdxExpr) {
				diag_set(ClientError, ER_INDEX_DEF_UNSUPPORTED,
					 "Expressions");
				pParse->is_aborted = true;
			}
			pRight = pExpr->pRight;
			if (pRight->op == TK_ID) {
				zTable = pExpr->pLeft->u.zToken;
				zColumn = pRight->u.zToken;
			} else {
				assert(pRight->op == TK_DOT);
				zTable = pRight->pLeft->u.zToken;
				zColumn = pRight->pRight->u.zToken;
			}
			return lookupName(pParse, zTable, zColumn, pNC,
					  pExpr);
		}

		/* Resolve function names
		 */
	case TK_FUNCTION:{
			ExprList *pList = pExpr->x.pList;	/* The argument list */
			int n = pList ? pList->nExpr : 0;	/* Number of arguments */
			int nId;	/* Number of characters in function name */
			const char *zId;	/* The function name. */

			assert(!ExprHasProperty(pExpr, EP_xIsSelect));
			zId = pExpr->u.zToken;
			nId = sqlStrlen30(zId);
			struct func *func = func_by_name(zId, nId);
			if (func == NULL) {
				diag_set(ClientError, ER_NO_SUCH_FUNCTION, zId);
				pParse->is_aborted = true;
				pNC->nErr++;
				return WRC_Abort;
			}
			if (!func->def->exports.sql) {
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 tt_sprintf("function %.*s() is not "
						    "available in SQL",
						     nId, zId));
				pParse->is_aborted = true;
				pNC->nErr++;
				return WRC_Abort;
			}
			if (func->def->param_count != -1 &&
			    func->def->param_count != n) {
				uint32_t argc = func->def->param_count;
				const char *err = tt_sprintf("%d", argc);
				diag_set(ClientError, ER_FUNC_WRONG_ARG_COUNT,
					 func->def->name, err, n);
				pParse->is_aborted = true;
				pNC->nErr++;
				return WRC_Abort;
			}
			bool is_agg = func->def->aggregate ==
				      FUNC_AGGREGATE_GROUP;
			assert(!is_agg || func->def->language ==
					  FUNC_LANGUAGE_SQL_BUILTIN);
			pExpr->type = func->def->returns;
			if (sql_func_flag_is_set(func, SQL_FUNC_UNLIKELY) &&
			    n == 2) {
				ExprSetProperty(pExpr, EP_Unlikely | EP_Skip);
				pExpr->iTable =
					exprProbability(pList->a[1].pExpr);
				if (pExpr->iTable < 0) {
					diag_set(ClientError, ER_ILLEGAL_PARAMS,
						"second argument to "
						"likelihood() must be a "
						"constant between 0.0 and 1.0");
					pParse->is_aborted = true;
					pNC->nErr++;
					return WRC_Abort;
				}
			} else if (sql_func_flag_is_set(func,
							SQL_FUNC_UNLIKELY)) {
				ExprSetProperty(pExpr, EP_Unlikely | EP_Skip);
				/*
				 * unlikely() probability is
				 * 0.0625, likely() is 0.9375
				 */
				pExpr->iTable = func->def->name[0] == 'u' ?
						8388608 : 125829120;
			}
			assert(!func->def->is_deterministic ||
			       (pNC->ncFlags & NC_IdxExpr) == 0);
			if (func->def->is_deterministic)
				ExprSetProperty(pExpr, EP_ConstFunc);
			if (is_agg && (pNC->ncFlags & NC_AllowAgg) == 0) {
				const char *err =
					tt_sprintf("misuse of aggregate "\
						   "function %.*s()", nId, zId);
				diag_set(ClientError, ER_SQL_PARSER_GENERIC, err);
				pParse->is_aborted = true;
				pNC->nErr++;
				is_agg = 0;
			}
			if (is_agg)
				pNC->ncFlags &= ~NC_AllowAgg;
			sqlWalkExprList(pWalker, pList);
			if (is_agg) {
				NameContext *pNC2 = pNC;
				pExpr->op = TK_AGG_FUNCTION;
				pExpr->op2 = 0;
				while (pNC2
				       && !sqlFunctionUsesThisSrc(pExpr,
								      pNC2->
								      pSrcList))
				{
					pExpr->op2++;
					pNC2 = pNC2->pNext;
				}
				assert(func != NULL);
				if (pNC2) {
					pNC2->ncFlags |= NC_HasAgg;
					if (sql_func_flag_is_set(func,
							         SQL_FUNC_MIN |
								 SQL_FUNC_MAX))
						pNC2->ncFlags |= NC_MinMaxAgg;
				}
				pNC->ncFlags |= NC_AllowAgg;
			}
			return WRC_Prune;
		}
	case TK_SELECT:
	case TK_EXISTS:
		testcase(pExpr->op == TK_EXISTS);
	case TK_IN:{
			testcase(pExpr->op == TK_IN);
			if (ExprHasProperty(pExpr, EP_xIsSelect)) {
				int nRef = pNC->nRef;
				assert((pNC->ncFlags & NC_IdxExpr) == 0);
				if (pNC->ncFlags & NC_IsCheck) {
					diag_set(ClientError,
						 ER_CK_DEF_UNSUPPORTED,
						 "Subqueries");
					pParse->is_aborted = true;
				}
				sqlWalkSelect(pWalker, pExpr->x.pSelect);
				assert(pNC->nRef >= nRef);
				if (nRef != pNC->nRef) {
					ExprSetProperty(pExpr, EP_VarSelect);
					pNC->ncFlags |= NC_VarSelect;
				}
			}
			break;
		}
	case TK_VARIABLE:{
			assert((pNC->ncFlags & NC_IsCheck) == 0);
			if (pNC->ncFlags & NC_IdxExpr) {
				diag_set(ClientError, ER_INDEX_DEF_UNSUPPORTED,
					 "Parameter markers");
				pParse->is_aborted = true;
			}
			break;
		}
	case TK_BETWEEN:
	case TK_EQ:
	case TK_NE:
	case TK_LT:
	case TK_LE:
	case TK_GT:
	case TK_GE:{
			int nLeft, nRight;
			if (pParse->db->mallocFailed)
				break;
			assert(pExpr->pLeft != 0);
			nLeft = sqlExprVectorSize(pExpr->pLeft);
			if (pExpr->op == TK_BETWEEN) {
				nRight =
				    sqlExprVectorSize(pExpr->x.pList->a[0].
							  pExpr);
				if (nRight == nLeft) {
					nRight =
					    sqlExprVectorSize(pExpr->x.
								  pList->a[1].
								  pExpr);
				}
			} else {
				assert(pExpr->pRight != 0);
				nRight = sqlExprVectorSize(pExpr->pRight);
			}
			if (nLeft != nRight) {
				diag_set(ClientError, ER_SQL_COLUMN_COUNT,
					 nLeft, nRight);
				pParse->is_aborted = true;
			}
			break;
		}
	}
	return (pParse->is_aborted
		|| pParse->db->mallocFailed) ? WRC_Abort : WRC_Continue;
}

/*
 * pEList is a list of expressions which are really the result set of the
 * a SELECT statement.  pE is a term in an ORDER BY or GROUP BY clause.
 * This routine checks to see if pE is a simple identifier which corresponds
 * to the AS-name of one of the terms of the expression list.  If it is,
 * this routine return an integer between 1 and N where N is the number of
 * elements in pEList, corresponding to the matching entry.  If there is
 * no match, or if pE is not a simple identifier, then this routine
 * return 0.
 *
 * pEList has been resolved.  pE has not.
 */
static int
resolveAsName(Parse * pParse,	/* Parsing context for error messages */
	      ExprList * pEList,	/* List of expressions to scan */
	      Expr * pE		/* Expression we are trying to match */
    )
{
	int i;			/* Loop counter */

	UNUSED_PARAMETER(pParse);

	if (pE->op == TK_ID) {
		char *zCol = pE->u.zToken;
		for (i = 0; i < pEList->nExpr; i++) {
			char *zAs = pEList->a[i].zName;
			if (zAs != 0 && strcmp(zAs, zCol) == 0) {
				return i + 1;
			}
		}
	}
	return 0;
}

/*
 * pE is a pointer to an expression which is a single term in the
 * ORDER BY of a compound SELECT.  The expression has not been
 * name resolved.
 *
 * At the point this routine is called, we already know that the
 * ORDER BY term is not an integer index into the result set.  That
 * case is handled by the calling routine.
 *
 * Attempt to match pE against result set columns in the left-most
 * SELECT statement.  Return the index i of the matching column,
 * as an indication to the caller that it should sort by the i-th column.
 * The left-most column is 1.  In other words, the value returned is the
 * same integer value that would be used in the SQL statement to indicate
 * the column.
 *
 * If there is no match, return 0.  Return -1 if an error occurs.
 */
static int
resolveOrderByTermToExprList(Parse * pParse,	/* Parsing context for error messages */
			     Select * pSelect,	/* The SELECT statement with the ORDER BY clause */
			     Expr * pE	/* The specific ORDER BY term */
    )
{
	int i;			/* Loop counter */
	ExprList *pEList;	/* The columns of the result set */
	NameContext nc;		/* Name context for resolving pE */
	int rc;			/* Return code from subprocedures */

	assert(sqlExprIsInteger(pE, &i) == 0);
	pEList = pSelect->pEList;

	/* Resolve all names in the ORDER BY term expression
	 */
	memset(&nc, 0, sizeof(nc));
	nc.pParse = pParse;
	nc.pSrcList = pSelect->pSrc;
	nc.pEList = pEList;
	nc.ncFlags = NC_AllowAgg;
	nc.nErr = 0;
	rc = sqlResolveExprNames(&nc, pE);
	if (rc)
		return 0;

	/* Try to match the ORDER BY expression against an expression
	 * in the result set.  Return an 1-based index of the matching
	 * result-set entry.
	 */
	for (i = 0; i < pEList->nExpr; i++) {
		if (sqlExprCompare(pEList->a[i].pExpr, pE, -1) < 2) {
			return i + 1;
		}
	}

	/* If no match, return 0. */
	return 0;
}

/*
 * Analyze the ORDER BY clause in a compound SELECT statement.   Modify
 * each term of the ORDER BY clause is a constant integer between 1
 * and N where N is the number of columns in the compound SELECT.
 *
 * ORDER BY terms that are already an integer between 1 and N are
 * unmodified.  ORDER BY terms that are integers outside the range of
 * 1 through N generate an error.  ORDER BY terms that are expressions
 * are matched against result set expressions of compound SELECT
 * beginning with the left-most SELECT and working toward the right.
 * At the first match, the ORDER BY expression is transformed into
 * the integer column number.
 *
 * Return the number of errors seen.
 */
static int
resolveCompoundOrderBy(Parse * pParse,	/* Parsing context.  Leave error messages here */
		       Select * pSelect	/* The SELECT statement containing the ORDER BY */
    )
{
	int i;
	ExprList *pOrderBy;
	ExprList *pEList;
	sql *db;
	int moreToDo = 1;

	pOrderBy = pSelect->pOrderBy;
	if (pOrderBy == 0)
		return 0;
	db = pParse->db;
#if SQL_MAX_COLUMN
	if (pOrderBy->nExpr > db->aLimit[SQL_LIMIT_COLUMN]) {
		diag_set(ClientError, ER_SQL_PARSER_LIMIT,
			 "The number of terms in ORDER BY clause",
			 pOrderBy->nExpr, db->aLimit[SQL_LIMIT_COLUMN]);
		pParse->is_aborted = true;
		return 1;
	}
#endif
	for (i = 0; i < pOrderBy->nExpr; i++) {
		pOrderBy->a[i].done = 0;
	}
	pSelect->pNext = 0;
	while (pSelect->pPrior) {
		pSelect->pPrior->pNext = pSelect;
		pSelect = pSelect->pPrior;
	}
	while (pSelect && moreToDo) {
		struct ExprList_item *pItem;
		moreToDo = 0;
		pEList = pSelect->pEList;
		assert(pEList != 0);
		for (i = 0, pItem = pOrderBy->a; i < pOrderBy->nExpr;
		     i++, pItem++) {
			int iCol = -1;
			Expr *pE, *pDup;
			if (pItem->done)
				continue;
			pE = sqlExprSkipCollate(pItem->pExpr);
			if (sqlExprIsInteger(pE, &iCol)) {
				if (iCol <= 0 || iCol > pEList->nExpr) {
					const char *err =
						"Error at ORDER BY in place "\
						"%d: term out of range - "\
						"should be between 1 and %d";
					err = tt_sprintf(err, i + 1,
							 pEList->nExpr);
					diag_set(ClientError,
						 ER_SQL_PARSER_GENERIC, err);
					pParse->is_aborted = true;
					return 1;
				}
			} else {
				iCol = resolveAsName(pParse, pEList, pE);
				if (iCol == 0) {
					pDup = sqlExprDup(db, pE, 0);
					if (!db->mallocFailed) {
						assert(pDup);
						iCol =
						    resolveOrderByTermToExprList
						    (pParse, pSelect, pDup);
					}
					sql_expr_delete(db, pDup, false);
				}
			}
			if (iCol > 0) {
				/* Convert the ORDER BY term into an integer column number iCol,
				 * taking care to preserve the COLLATE clause if it exists
				 */
				struct Expr *pNew =
					sql_expr_new_anon(db, TK_INTEGER);
				if (pNew == NULL) {
					pParse->is_aborted = true;
					return 1;
				}
				pNew->flags |= EP_IntValue;
				pNew->u.iValue = iCol;
				if (pItem->pExpr == pE) {
					pItem->pExpr = pNew;
				} else {
					Expr *pParent = pItem->pExpr;
					assert(pParent->op == TK_COLLATE);
					while (pParent->pLeft->op == TK_COLLATE)
						pParent = pParent->pLeft;
					assert(pParent->pLeft == pE);
					pParent->pLeft = pNew;
				}
				sql_expr_delete(db, pE, false);
				pItem->u.x.iOrderByCol = (u16) iCol;
				pItem->done = 1;
			} else {
				moreToDo = 1;
			}
		}
		pSelect = pSelect->pNext;
	}
	for (i = 0; i < pOrderBy->nExpr; i++) {
		if (pOrderBy->a[i].done == 0) {
			const char *err = "Error at ORDER BY in place %d: "\
					  "term does not match any column in "\
					  "the result set";
			diag_set(ClientError, ER_SQL_PARSER_GENERIC,
				 tt_sprintf(err, i + 1));
			pParse->is_aborted = true;
			return 1;
		}
	}
	return 0;
}

/*
 * Check every term in the ORDER BY or GROUP BY clause pOrderBy of
 * the SELECT statement pSelect.  If any term is reference to a
 * result set expression (as determined by the ExprList.a.u.x.iOrderByCol
 * field) then convert that term into a copy of the corresponding result set
 * column.
 *
 * @retval 0 On success, not 0 elsewhere.
 */
int
sqlResolveOrderGroupBy(Parse * pParse,	/* Parsing context.  Leave error messages here */
			   Select * pSelect,	/* The SELECT statement containing the clause */
			   ExprList * pOrderBy,	/* The ORDER BY or GROUP BY clause to be processed */
			   const char *zType	/* "ORDER" or "GROUP" */
    )
{
	int i;
	sql *db = pParse->db;
	ExprList *pEList;
	struct ExprList_item *pItem;

	if (pOrderBy == 0 || pParse->db->mallocFailed)
		return 0;
#if SQL_MAX_COLUMN
	if (pOrderBy->nExpr > db->aLimit[SQL_LIMIT_COLUMN]) {
		const char *err = tt_sprintf("The number of terms in %s BY "\
					     "clause", zType);
		diag_set(ClientError, ER_SQL_PARSER_LIMIT, err,
			 pOrderBy->nExpr, db->aLimit[SQL_LIMIT_COLUMN]);
		pParse->is_aborted = true;
		return 1;
	}
#endif
	pEList = pSelect->pEList;
	assert(pEList != 0);	/* sqlSelectNew() guarantees this */
	for (i = 0, pItem = pOrderBy->a; i < pOrderBy->nExpr; i++, pItem++) {
		if (pItem->u.x.iOrderByCol) {
			if (pItem->u.x.iOrderByCol > pEList->nExpr) {
				const char *err = "Error at %s BY in place "\
						  "%d: term out of range - "\
						  "should be between 1 and %d";
				err = tt_sprintf(err, zType, i + 1,
						 pEList->nExpr);
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 err);
				pParse->is_aborted = true;
				return 1;
			}
			resolveAlias(pParse, pEList, pItem->u.x.iOrderByCol - 1,
				     pItem->pExpr, zType, 0);
		}
	}
	return 0;
}

/*
 * pOrderBy is an ORDER BY or GROUP BY clause in SELECT statement pSelect.
 * The Name context of the SELECT statement is pNC.  zType is either
 * "ORDER" or "GROUP" depending on which type of clause pOrderBy is.
 *
 * This routine resolves each term of the clause into an expression.
 * If the order-by term is an integer I between 1 and N (where N is the
 * number of columns in the result set of the SELECT) then the expression
 * in the resolution is a copy of the I-th result-set expression.  If
 * the order-by term is an identifier that corresponds to the AS-name of
 * a result-set expression, then the term resolves to a copy of the
 * result-set expression.  Otherwise, the expression is resolved in
 * the usual way - using sqlResolveExprNames().
 *
 * @retval 0 On success, not 0 elsewhere.
 */
static int
resolveOrderGroupBy(NameContext * pNC,	/* The name context of the SELECT statement */
		    Select * pSelect,	/* The SELECT statement holding pOrderBy */
		    ExprList * pOrderBy,	/* An ORDER BY or GROUP BY clause to resolve */
		    const char *zType	/* Either "ORDER" or "GROUP", as appropriate */
    )
{
	int i, j;		/* Loop counters */
	int iCol;		/* Column number */
	struct ExprList_item *pItem;	/* A term of the ORDER BY clause */
	Parse *pParse;		/* Parsing context */
	int nResult;		/* Number of terms in the result set */

	if (pOrderBy == 0)
		return 0;
	nResult = pSelect->pEList->nExpr;
	pParse = pNC->pParse;
	for (i = 0, pItem = pOrderBy->a; i < pOrderBy->nExpr; i++, pItem++) {
		Expr *pE = pItem->pExpr;
		Expr *pE2 = sqlExprSkipCollate(pE);
		if (zType[0] != 'G') {
			iCol = resolveAsName(pParse, pSelect->pEList, pE2);
			if (iCol > 0) {
				/* If an AS-name match is found, mark this ORDER BY column as being
				 * a copy of the iCol-th result-set column.  The subsequent call to
				 * sqlResolveOrderGroupBy() will convert the expression to a
				 * copy of the iCol-th result-set expression.
				 */
				pItem->u.x.iOrderByCol = (u16) iCol;
				continue;
			}
		}
		if (sqlExprIsInteger(pE2, &iCol)) {
			/* The ORDER BY term is an integer constant.  Again, set the column
			 * number so that sqlResolveOrderGroupBy() will convert the
			 * order-by term to a copy of the result-set expression
			 */
			if (iCol < 1 || iCol > 0xffff) {
				const char *err = "Error at %s BY in place "\
						  "%d: term out of range - "\
						  "should be between 1 and %d";
				err = tt_sprintf(err, zType, i + 1, nResult);
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 err);
				pParse->is_aborted = true;
				return 1;
			}
			pItem->u.x.iOrderByCol = (u16) iCol;
			continue;
		}

		/* Otherwise, treat the ORDER BY term as an ordinary expression */
		pItem->u.x.iOrderByCol = 0;
		if (sqlResolveExprNames(pNC, pE)) {
			return 1;
		}
		for (j = 0; j < pSelect->pEList->nExpr; j++) {
			if (sqlExprCompare
			    (pE, pSelect->pEList->a[j].pExpr, -1) == 0) {
				pItem->u.x.iOrderByCol = j + 1;
			}
		}
	}
	return sqlResolveOrderGroupBy(pParse, pSelect, pOrderBy, zType);
}

/*
 * Resolve names in the SELECT statement p and all of its descendants.
 */
static int
resolveSelectStep(Walker * pWalker, Select * p)
{
	NameContext *pOuterNC;	/* Context that contains this SELECT */
	NameContext sNC;	/* Name context of this SELECT */
	int isCompound;		/* True if p is a compound select */
	int nCompound;		/* Number of compound terms processed so far */
	Parse *pParse;		/* Parsing context */
	int i;			/* Loop counter */
	ExprList *pGroupBy;	/* The GROUP BY clause */
	Select *pLeftmost;	/* Left-most of SELECT of a compound */
	sql *db;		/* Database connection */

	assert(p != 0);
	if (p->selFlags & SF_Resolved) {
		return WRC_Prune;
	}
	pOuterNC = pWalker->u.pNC;
	pParse = pWalker->pParse;
	db = pParse->db;

	/* Normally sqlSelectExpand() will be called first and will have
	 * already expanded this SELECT.  However, if this is a subquery within
	 * an expression, sqlResolveExprNames() will be called without a
	 * prior call to sqlSelectExpand().  When that happens, let
	 * sqlSelectPrep() do all of the processing for this SELECT.
	 * sqlSelectPrep() will invoke both sqlSelectExpand() and
	 * this routine in the correct order.
	 */
	if ((p->selFlags & SF_Expanded) == 0) {
		sqlSelectPrep(pParse, p, pOuterNC);
		return (pParse->is_aborted
			|| db->mallocFailed) ? WRC_Abort : WRC_Prune;
	}

	isCompound = p->pPrior != 0;
	nCompound = 0;
	pLeftmost = p;
	while (p) {
		assert((p->selFlags & SF_Expanded) != 0);
		assert((p->selFlags & SF_Resolved) == 0);
		p->selFlags |= SF_Resolved;

		/* Resolve the expressions in the LIMIT and OFFSET clauses. These
		 * are not allowed to refer to any names, so pass an empty NameContext.
		 */
		memset(&sNC, 0, sizeof(sNC));
		sNC.pParse = pParse;
		if (sqlResolveExprNames(&sNC, p->pLimit) ||
		    sqlResolveExprNames(&sNC, p->pOffset)) {
			return WRC_Abort;
		}

		/* If the SF_Converted flags is set, then this Select object was
		 * was created by the convertCompoundSelectToSubquery() function.
		 * In this case the ORDER BY clause (p->pOrderBy) should be resolved
		 * as if it were part of the sub-query, not the parent. This block
		 * moves the pOrderBy down to the sub-query. It will be moved back
		 * after the names have been resolved.
		 */
		if (p->selFlags & SF_Converted) {
			Select *pSub = p->pSrc->a[0].pSelect;
			assert(p->pSrc->nSrc == 1 && p->pOrderBy);
			assert(pSub->pPrior && pSub->pOrderBy == 0);
			pSub->pOrderBy = p->pOrderBy;
			p->pOrderBy = 0;
		}

		/* Recursively resolve names in all subqueries
		 */
		for (i = 0; i < p->pSrc->nSrc; i++) {
			struct SrcList_item *pItem = &p->pSrc->a[i];
			if (pItem->pSelect) {
				NameContext *pNC;	/* Used to iterate name contexts */
				int nRef = 0;	/* Refcount for pOuterNC and outer contexts */

				/* Count the total number of references to pOuterNC and all of its
				 * parent contexts. After resolving references to expressions in
				 * pItem->pSelect, check if this value has changed. If so, then
				 * SELECT statement pItem->pSelect must be correlated. Set the
				 * pItem->fg.isCorrelated flag if this is the case.
				 */
				for (pNC = pOuterNC; pNC; pNC = pNC->pNext)
					nRef += pNC->nRef;

				sqlResolveSelectNames(pParse,
							  pItem->pSelect,
							  pOuterNC);
				if (pParse->is_aborted || db->mallocFailed)
					return WRC_Abort;

				for (pNC = pOuterNC; pNC; pNC = pNC->pNext)
					nRef -= pNC->nRef;
				assert(pItem->fg.isCorrelated == 0
				       && nRef <= 0);
				pItem->fg.isCorrelated = (nRef != 0);
			}
		}

		/* Set up the local name-context to pass to sqlResolveExprNames() to
		 * resolve the result-set expression list.
		 */
		bool is_all_select_agg = true;
		sNC.ncFlags = NC_AllowAgg;
		sNC.pSrcList = p->pSrc;
		sNC.pNext = pOuterNC;
		struct ExprList_item *item = p->pEList->a;
		/* Resolve names in the result set. */
		for (i = 0; i < p->pEList->nExpr; ++i, ++item) {
			u16 has_agg_flag = sNC.ncFlags & NC_HasAgg;
			sNC.ncFlags &= ~NC_HasAgg;
			if (sqlResolveExprNames(&sNC, item->pExpr) != 0)
				return WRC_Abort;
			if ((sNC.ncFlags & NC_HasAgg) == 0 &&
			    !sqlExprIsConstantOrFunction(item->pExpr, 0)) {
				is_all_select_agg = false;
				sNC.ncFlags |= has_agg_flag;
				break;
			}
			sNC.ncFlags |= has_agg_flag;
		}
		/*
		 * Finish iteration for is_all_select_agg == false
		 * and do not care about flags anymore.
		 */
		for (; i < p->pEList->nExpr; ++i, ++item) {
			assert(! is_all_select_agg);
			if (sqlResolveExprNames(&sNC, item->pExpr) != 0)
				return WRC_Abort;
		}

		/*
		 * If there are no aggregate functions in the
		 * result-set, and no GROUP BY or HAVING
		 * expression, do not allow aggregates in any
		 * of the other expressions.
		 */
		assert((p->selFlags & SF_Aggregate) == 0);
		pGroupBy = p->pGroupBy;
		if (pGroupBy != NULL || p->pHaving != NULL ||
		    (sNC.ncFlags & NC_HasAgg) != 0) {
			assert(NC_MinMaxAgg == SF_MinMaxAgg);
			p->selFlags |=
			    SF_Aggregate | (sNC.ncFlags & NC_MinMaxAgg);
		} else {
			sNC.ncFlags &= ~NC_AllowAgg;
		}

		/*
		 * Add the output column list to the name-context
		 * before parsing the other expressions in the
		 * SELECT statement. This is so that expressions
		 * in the WHERE clause (etc.) can refer to
		 * expressions by aliases in the result set.
		 *
		 * Minor point: If this is the case, then the
		 * expression will be re-evaluated for each
		 * reference to it.
		 */
		sNC.pEList = p->pEList;
		/*
		 * If a HAVING clause is present, then there must
		 * be a GROUP BY clause or aggregate function
		 * should be specified.
		 */
		if (p->pHaving != NULL && pGroupBy == NULL) {
			sNC.ncFlags |= NC_AllowAgg;
			if (is_all_select_agg &&
			    sqlResolveExprNames(&sNC, p->pHaving) != 0)
				return WRC_Abort;
			if ((sNC.ncFlags & NC_HasAgg) == 0 ||
			    (sNC.ncFlags & NC_HasUnaggregatedId) != 0) {
				diag_set(ClientError, ER_SQL_EXECUTE, "HAVING "
					 "argument must appear in the GROUP BY "
					 "clause or be used in an aggregate "
					 "function");
				pParse->is_aborted = true;
				return WRC_Abort;
			}
			/*
			 * Aggregate functions may return only
			 * one tuple, so user-defined LIMITs have
			 * no sense (most DBs don't support such
			 * LIMIT but there is no reason to
			 * restrict it directly).
			 */
			sql_expr_delete(db, p->pLimit, false);
			p->pLimit = sql_expr_new(db, TK_INTEGER,
						 &sqlIntTokens[1]);
			if (p->pLimit == NULL)
				pParse->is_aborted = true;
		} else {
			if (sqlResolveExprNames(&sNC, p->pHaving))
				return WRC_Abort;
		}
		if (sqlResolveExprNames(&sNC, p->pWhere))
			return WRC_Abort;

		/* Resolve names in table-valued-function arguments */
		for (i = 0; i < p->pSrc->nSrc; i++) {
			struct SrcList_item *pItem = &p->pSrc->a[i];
			if (pItem->fg.isTabFunc
			    && sqlResolveExprListNames(&sNC,
							   pItem->u1.pFuncArg)
			    ) {
				return WRC_Abort;
			}
		}

		/* The ORDER BY and GROUP BY clauses may not refer to terms in
		 * outer queries
		 */
		sNC.pNext = 0;
		sNC.ncFlags |= NC_AllowAgg;

		/* If this is a converted compound query, move the ORDER BY clause from
		 * the sub-query back to the parent query. At this point each term
		 * within the ORDER BY clause has been transformed to an integer value.
		 * These integers will be replaced by copies of the corresponding result
		 * set expressions by the call to resolveOrderGroupBy() below.
		 */
		if (p->selFlags & SF_Converted) {
			Select *pSub = p->pSrc->a[0].pSelect;
			p->pOrderBy = pSub->pOrderBy;
			pSub->pOrderBy = 0;
		}

		/* Process the ORDER BY clause for singleton SELECT statements.
		 * The ORDER BY clause for compounds SELECT statements is handled
		 * below, after all of the result-sets for all of the elements of
		 * the compound have been resolved.
		 *
		 * If there is an ORDER BY clause on a term of a compound-select other
		 * than the right-most term, then that is a syntax error.  But the error
		 * is not detected until much later, and so we need to go ahead and
		 * resolve those symbols on the incorrect ORDER BY for consistency.
		 */
		if (isCompound <= nCompound	/* Defer right-most ORDER BY of a compound */
		    && resolveOrderGroupBy(&sNC, p, p->pOrderBy, "ORDER")
		    ) {
			return WRC_Abort;
		}
		if (db->mallocFailed) {
			return WRC_Abort;
		}

		/* Resolve the GROUP BY clause.  At the same time, make sure
		 * the GROUP BY clause does not contain aggregate functions.
		 */
		if (pGroupBy) {
			struct ExprList_item *pItem;

			if (resolveOrderGroupBy(&sNC, p, pGroupBy, "GROUP")
			    || db->mallocFailed) {
				return WRC_Abort;
			}
			const char *err_msg = "aggregate functions are not "\
					      "allowed in the GROUP BY clause";
			for (i = 0, pItem = pGroupBy->a; i < pGroupBy->nExpr;
			     i++, pItem++) {
				if (ExprHasProperty(pItem->pExpr, EP_Agg)) {
					diag_set(ClientError,
						 ER_SQL_PARSER_GENERIC,
						 err_msg);
					pParse->is_aborted = true;
					return WRC_Abort;
				}
			}
		}

		/* If this is part of a compound SELECT, check that it has the right
		 * number of expressions in the select list.
		 */
		if (p->pNext && p->pEList->nExpr != p->pNext->pEList->nExpr) {
			if (p->pNext->selFlags & SF_Values) {
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 "all VALUES must have the same "\
					 "number of terms");
			} else {
				const char *err =
					"SELECTs to the left and right of %s "\
					"do not have the same number of "\
					"result columns";
				const char *op =
					sql_select_op_name(p->pNext->op);
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 tt_sprintf(err, op));
			}
			pParse->is_aborted = true;
			return WRC_Abort;
		}
		/* Advance to the next term of the compound
		 */
		p = p->pPrior;
		nCompound++;
	}

	/* Resolve the ORDER BY on a compound SELECT after all terms of
	 * the compound have been resolved.
	 */
	if (isCompound && resolveCompoundOrderBy(pParse, pLeftmost)) {
		return WRC_Abort;
	}

	return WRC_Prune;
}

/*
 * This routine walks an expression tree and resolves references to
 * table columns and result-set columns.  At the same time, do error
 * checking on function usage and set a flag if any aggregate functions
 * are seen.
 *
 * To resolve table columns references we look for nodes (or subtrees) of the
 * form X.Y.Z or Y.Z or just Z where
 *
 *      X:   The name of a database.  Ex:  "main" or "temp" or
 *           the symbolic name assigned to an ATTACH-ed database.
 *
 *      Y:   The name of a table in a FROM clause.  Or in a trigger
 *           one of the special names "old" or "new".
 *
 *      Z:   The name of a column in table Y.
 *
 * The node at the root of the subtree is modified as follows:
 *
 *    Expr.op        Changed to TK_COLUMN_REF
 *    Expr.pTab      Points to the Table object for X.Y
 *    Expr.iColumn   The column index in X.Y.  -1 for the rowid.
 *    Expr.iTable    The VDBE cursor number for X.Y
 *
 *
 * To resolve result-set references, look for expression nodes of the
 * form Z (with no X and Y prefix) where the Z matches the right-hand
 * size of an AS clause in the result-set of a SELECT.  The Z expression
 * is replaced by a copy of the left-hand side of the result-set expression.
 * Table-name and function resolution occurs on the substituted expression
 * tree.  For example, in:
 *
 *      SELECT a+b AS x, c+d AS y FROM t1 ORDER BY x;
 *
 * The "x" term of the order by is replaced by "a+b" to render:
 *
 *      SELECT a+b AS x, c+d AS y FROM t1 ORDER BY a+b;
 *
 * Function calls are checked to make sure that the function is
 * defined and that the correct number of arguments are specified.
 * If the function is an aggregate function, then the NC_HasAgg flag is
 * set and the opcode is changed from TK_FUNCTION to TK_AGG_FUNCTION.
 * If an expression contains aggregate functions then the EP_Agg
 * property on the expression is set.
 *
 * An error message is left in pParse if anything is amiss.  The number
 * if errors is returned.
 */
int
sqlResolveExprNames(NameContext * pNC,	/* Namespace to resolve expressions in. */
			Expr * pExpr	/* The expression to be analyzed. */
    )
{
	u16 savedHasAgg;
	Walker w;

	if (pExpr == 0)
		return 0;
#if SQL_MAX_EXPR_DEPTH>0
	{
		Parse *pParse = pNC->pParse;
		if (sqlExprCheckHeight
		    (pParse, pExpr->nHeight + pNC->pParse->nHeight)) {
			return 1;
		}
		pParse->nHeight += pExpr->nHeight;
	}
#endif
	savedHasAgg = pNC->ncFlags & (NC_HasAgg | NC_MinMaxAgg);
	pNC->ncFlags &= ~(NC_HasAgg | NC_MinMaxAgg);
	w.pParse = pNC->pParse;
	w.xExprCallback = resolveExprStep;
	w.xSelectCallback = resolveSelectStep;
	w.xSelectCallback2 = 0;
	w.walkerDepth = 0;
	w.eCode = 0;
	w.u.pNC = pNC;
	sqlWalkExpr(&w, pExpr);
#if SQL_MAX_EXPR_DEPTH>0
	pNC->pParse->nHeight -= pExpr->nHeight;
#endif
	if (pNC->nErr > 0 || w.pParse->is_aborted) {
		ExprSetProperty(pExpr, EP_Error);
	}
	if (pNC->ncFlags & NC_HasAgg) {
		ExprSetProperty(pExpr, EP_Agg);
	}
	pNC->ncFlags |= savedHasAgg;
	return ExprHasProperty(pExpr, EP_Error);
}

/*
 * Resolve all names for all expression in an expression list.  This is
 * just like sqlResolveExprNames() except that it works for an expression
 * list rather than a single expression.
 */
int
sqlResolveExprListNames(NameContext * pNC,	/* Namespace to resolve expressions in. */
			    ExprList * pList	/* The expression list to be analyzed. */
    )
{
	int i;
	if (pList) {
		for (i = 0; i < pList->nExpr; i++) {
			if (sqlResolveExprNames(pNC, pList->a[i].pExpr))
				return WRC_Abort;
		}
	}
	return WRC_Continue;
}

/*
 * Resolve all names in all expressions of a SELECT and in all
 * decendents of the SELECT, including compounds off of p->pPrior,
 * subqueries in expressions, and subqueries used as FROM clause
 * terms.
 *
 * See sqlResolveExprNames() for a description of the kinds of
 * transformations that occur.
 *
 * All SELECT statements should have been expanded using
 * sqlSelectExpand() prior to invoking this routine.
 */
void
sqlResolveSelectNames(Parse * pParse,	/* The parser context */
			  Select * p,	/* The SELECT statement being coded. */
			  NameContext * pOuterNC	/* Name context for parent SELECT statement */
    )
{
	Walker w;

	assert(p != 0);
	memset(&w, 0, sizeof(w));
	w.xExprCallback = resolveExprStep;
	w.xSelectCallback = resolveSelectStep;
	w.pParse = pParse;
	w.u.pNC = pOuterNC;
	sqlWalkSelect(&w, p);
}

void
sql_resolve_self_reference(struct Parse *parser, struct space_def *def,
			   int type, struct Expr *expr)
{
	/* Fake SrcList for parser->create_table_def */
	SrcList sSrc;
	/* Name context for parser->create_table_def  */
	NameContext sNC;

	assert(type == NC_IsCheck || type == NC_IdxExpr);
	memset(&sNC, 0, sizeof(sNC));
	memset(&sSrc, 0, sizeof(sSrc));
	sSrc.nSrc = 1;
	sSrc.a[0].zName = def->name;
	struct space tmp_space;
	memset(&tmp_space, 0, sizeof(tmp_space));
	tmp_space.def = def;
	sSrc.a[0].space = &tmp_space;
	sSrc.a[0].iCursor = -1;
	sNC.pParse = parser;
	sNC.pSrcList = &sSrc;
	sNC.ncFlags = type;
	sqlResolveExprNames(&sNC, expr);
}
