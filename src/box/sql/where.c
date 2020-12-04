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
 * This module contains C code that generates VDBE code used to process
 * the WHERE clause of SQL statements.  This module is responsible for
 * generating the code that loops through a table looking for applicable
 * rows.  Indices are selected and used to speed the search when doing
 * so is applicable.  Because this module is responsible for selecting
 * indices, you might also think of this module as the "query optimizer".
 */
#include "coll/coll.h"
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "vdbeInt.h"
#include "whereInt.h"
#include "box/coll_id_cache.h"
#include "box/schema.h"

/* Forward declaration of methods */
static int whereLoopResize(sql *, WhereLoop *, int);

/* Test variable that can be set to enable WHERE tracing */
#ifdef SQL_DEBUG
/***/ int sqlWhereTrace = 0; /* -1; */
#endif

/*
 * Return the estimated number of output rows from a WHERE clause
 */
LogEst
sqlWhereOutputRowCount(WhereInfo * pWInfo)
{
	return pWInfo->nRowOut;
}

/*
 * Return one of the WHERE_DISTINCT_xxxxx values to indicate how this
 * WHERE clause returns outputs for DISTINCT processing.
 */
int
sqlWhereIsDistinct(WhereInfo * pWInfo)
{
	return pWInfo->eDistinct;
}

/*
 * Return TRUE if the WHERE clause returns rows in ORDER BY order.
 * Return FALSE if the output needs to be sorted.
 */
int
sqlWhereIsOrdered(WhereInfo * pWInfo)
{
	return pWInfo->nOBSat;
}

/*
 * Return TRUE if the innermost loop of the WHERE clause implementation
 * returns rows in ORDER BY order for complete run of the inner loop.
 *
 * Across multiple iterations of outer loops, the output rows need not be
 * sorted.  As long as rows are sorted for just the innermost loop, this
 * routine can return TRUE.
 */
int
sqlWhereOrderedInnerLoop(WhereInfo * pWInfo)
{
	return pWInfo->bOrderedInnerLoop;
}

/*
 * Return the VDBE address or label to jump to in order to continue
 * immediately with the next row of a WHERE clause.
 */
int
sqlWhereContinueLabel(WhereInfo * pWInfo)
{
	assert(pWInfo->iContinue != 0);
	return pWInfo->iContinue;
}

/*
 * Return the VDBE address or label to jump to in order to break
 * out of a WHERE loop.
 */
int
sqlWhereBreakLabel(WhereInfo * pWInfo)
{
	return pWInfo->iBreak;
}

/*
 * Return ONEPASS_OFF (0) if an UPDATE or DELETE statement is unable to
 * operate directly on the rowis returned by a WHERE clause.  Return
 * ONEPASS_SINGLE (1) if the statement can operation directly because only
 * a single row is to be changed.  Return ONEPASS_MULTI (2) if the one-pass
 * optimization can be used on multiple
 *
 * If the ONEPASS optimization is used (if this routine returns true)
 * then also write the indices of open cursors used by ONEPASS
 * into aiCur[0] and aiCur[1].  iaCur[0] gets the cursor of the data
 * table and iaCur[1] gets the cursor used by an auxiliary index.
 * Either value may be -1, indicating that cursor is not used.
 * Any cursors returned will have been opened for writing.
 *
 * aiCur[0] and aiCur[1] both get -1 if the where-clause logic is
 * unable to use the ONEPASS optimization.
 */
int
sqlWhereOkOnePass(WhereInfo * pWInfo, int *aiCur)
{
	memcpy(aiCur, pWInfo->aiCurOnePass, sizeof(int) * 2);
	/*  Tarantool workaround: one pass is not working right now, since deleting tuple
	 *  invalidates pointing iterator (which is used to go through table).
	 */
	if (pWInfo->eOnePass == ONEPASS_MULTI) {
		pWInfo->eOnePass = ONEPASS_OFF;
	}
#ifdef SQL_DEBUG
	if (sqlWhereTrace && pWInfo->eOnePass != ONEPASS_OFF) {
		sqlDebugPrintf("%s cursors: %d %d\n",
				   pWInfo->eOnePass ==
				   ONEPASS_SINGLE ? "ONEPASS_SINGLE" :
				   "ONEPASS_MULTI", aiCur[0], aiCur[1]);
	}
#endif
	return pWInfo->eOnePass;
}

/*
 * Move the content of pSrc into pDest
 */
static void
whereOrMove(WhereOrSet * pDest, WhereOrSet * pSrc)
{
	pDest->n = pSrc->n;
	memcpy(pDest->a, pSrc->a, pDest->n * sizeof(pDest->a[0]));
}

/*
 * Try to insert a new prerequisite/cost entry into the WhereOrSet pSet.
 *
 * The new entry might overwrite an existing entry, or it might be
 * appended, or it might be discarded.  Do whatever is the right thing
 * so that pSet keeps the N_OR_COST best entries seen so far.
 */
static int
whereOrInsert(WhereOrSet * pSet,	/* The WhereOrSet to be updated */
	      Bitmask prereq,	/* Prerequisites of the new entry */
	      LogEst rRun,	/* Run-cost of the new entry */
	      LogEst nOut)	/* Number of outputs for the new entry */
{
	u16 i;
	WhereOrCost *p;
	for (i = pSet->n, p = pSet->a; i > 0; i--, p++) {
		if (rRun <= p->rRun && (prereq & p->prereq) == prereq) {
			goto whereOrInsert_done;
		}
		if (p->rRun <= rRun && (p->prereq & prereq) == p->prereq) {
			return 0;
		}
	}
	if (pSet->n < N_OR_COST) {
		p = &pSet->a[pSet->n++];
		p->nOut = nOut;
	} else {
		p = pSet->a;
		for (i = 1; i < pSet->n; i++) {
			if (p->rRun > pSet->a[i].rRun)
				p = pSet->a + i;
		}
		if (p->rRun <= rRun)
			return 0;
	}
 whereOrInsert_done:
	p->prereq = prereq;
	p->rRun = rRun;
	if (p->nOut > nOut)
		p->nOut = nOut;
	return 1;
}

/*
 * Return the bitmask for the given cursor number.  Return 0 if
 * iCursor is not in the set.
 */
Bitmask
sqlWhereGetMask(WhereMaskSet * pMaskSet, int iCursor)
{
	int i;
	assert(pMaskSet->n <= (int)sizeof(Bitmask) * 8);
	for (i = 0; i < pMaskSet->n; i++) {
		if (pMaskSet->ix[i] == iCursor) {
			return MASKBIT(i);
		}
	}
	return 0;
}

/*
 * Create a new mask for cursor iCursor.
 *
 * There is one cursor per table in the FROM clause.  The number of
 * tables in the FROM clause is limited by a test early in the
 * sqlWhereBegin() routine.  So we know that the pMaskSet->ix[]
 * array will never overflow.
 */
static void
createMask(WhereMaskSet * pMaskSet, int iCursor)
{
	assert(pMaskSet->n < ArraySize(pMaskSet->ix));
	pMaskSet->ix[pMaskSet->n++] = iCursor;
}

/*
 * Advance to the next WhereTerm that matches according to the criteria
 * established when the pScan object was initialized by whereScanInit().
 * Return NULL if there are no more matching WhereTerms.
 */
static WhereTerm *
whereScanNext(WhereScan * pScan)
{
	int iCur;		/* The cursor on the LHS of the term */
	i16 iColumn;		/* The column on the LHS of the term.  -1 for IPK */
	Expr *pX;		/* An expression being tested */
	WhereClause *pWC;	/* Shorthand for pScan->pWC */
	WhereTerm *pTerm;	/* The term being tested */
	int k = pScan->k;	/* Where to start scanning */

	assert(pScan->iEquiv <= pScan->nEquiv);
	pWC = pScan->pWC;
	while (1) {
		iColumn = pScan->aiColumn[pScan->iEquiv - 1];
		iCur = pScan->aiCur[pScan->iEquiv - 1];
		assert(pWC != 0);
		do {
			for (pTerm = pWC->a + k; k < pWC->nTerm; k++, pTerm++) {
				if (pTerm->leftCursor == iCur
				    && pTerm->u.leftColumn == iColumn
				    && (pScan->iEquiv <= 1
					|| !ExprHasProperty(pTerm->pExpr,
							    EP_FromJoin))
				    ) {
					if ((pTerm->eOperator & WO_EQUIV) != 0
					    && pScan->nEquiv <
					    ArraySize(pScan->aiCur)
					    && (pX =
						sqlExprSkipCollate(pTerm->
								       pExpr->
								       pRight))->
					    op == TK_COLUMN) {
						int j;
						for (j = 0; j < pScan->nEquiv; j++) {
							if (pScan->aiCur[j] == pX->iTable
							    && pScan->aiColumn[j] == pX->iColumn) {
								break;
							}
						}
						if (j == pScan->nEquiv) {
							pScan->aiCur[j] =
							    pX->iTable;
							pScan->aiColumn[j] =
							    pX->iColumn;
							pScan->nEquiv++;
						}
					}
					if ((pTerm->eOperator & pScan->
					     opMask) != 0) {
						/* Verify the type and collating sequence match */
						if ((pTerm->eOperator & WO_ISNULL) == 0) {
							pX = pTerm->pExpr;
							enum field_type expr_type =
								expr_cmp_mutual_type(pX);
							if (!field_type1_contains_type2(expr_type, pScan->idx_type))
								continue;
							if (pScan->is_column_seen) {
								Parse *pParse =
									pWC->pWInfo->pParse;
								assert(pX->pLeft);
								uint32_t id;
								if (sql_binary_compare_coll_seq(
									pParse, pX->pLeft,
									pX->pRight, &id) != 0)
									break;
								struct coll *coll =
									id != COLL_NONE ?
									coll_by_id(id)->coll :
									NULL;
								if (coll != pScan->coll)
									continue;
							}
						}
						if ((pTerm->eOperator & WO_EQ) != 0
						    && (pX = pTerm->pExpr->pRight)->op == TK_COLUMN
						    && pX->iTable == pScan->aiCur[0]
						    && pX->iColumn == pScan->aiColumn[0]) {
							continue;
						}
						pScan->pWC = pWC;
						pScan->k = k + 1;
						return pTerm;
					}
				}
			}
			pWC = pWC->pOuter;
			k = 0;
		} while (pWC != 0);
		if (pScan->iEquiv >= pScan->nEquiv)
			break;
		pWC = pScan->pOrigWC;
		k = 0;
		pScan->iEquiv++;
	}
	return 0;
}

/*
 * Initialize a WHERE clause scanner object.  Return a pointer to the
 * first match.  Return NULL if there are no matches.
 *
 * The scanner will be searching the WHERE clause pWC.  It will look
 * for terms of the form "X <op> <expr>" where X is column iColumn of table
 * iCur.   Or if pIdx!=0 then X is column iColumn of index pIdx.  pIdx
 * must be one of the indexes of table iCur.
 *
 * The <op> must be one of the operators described by opMask.
 *
 * If the search is for X and the WHERE clause contains terms of the
 * form X=Y then this routine might also return terms of the form
 * "Y <op> <expr>".  The number of levels of transitivity is limited,
 * but is enough to handle most commonly occurring SQL statements.
 *
 * If X is not the INTEGER PRIMARY KEY then X must be compatible with
 * index pIdx.
 */
static WhereTerm *
whereScanInit(WhereScan * pScan,	/* The WhereScan object being initialized */
	      WhereClause * pWC,	/* The WHERE clause to be scanned */
	      int iCur,		/* Cursor to scan for */
	      int iColumn,	/* Column to scan for */
	      u32 opMask,	/* Operator(s) to scan for */
	      struct index_def *idx_def)
{
	pScan->pOrigWC = pWC;
	pScan->pWC = pWC;
	pScan->pIdxExpr = 0;
	pScan->idx_type = FIELD_TYPE_SCALAR;
	pScan->coll = NULL;
	pScan->is_column_seen = false;
	if (idx_def != NULL) {
		int j = iColumn;
		/*
		 * pIdx->def->iid == UINT32_MAX means that
		 * pIdx is a fake integer primary key index.
		 */
		if (idx_def->iid != UINT32_MAX) {
			iColumn = idx_def->key_def->parts[iColumn].fieldno;
			struct space *sp = space_by_id(idx_def->space_id);
			assert(sp != NULL);
			if (sp->def->field_count != 0)
				pScan->idx_type = sp->def->fields[iColumn].type;
			pScan->coll = idx_def->key_def->parts[j].coll;
			pScan->is_column_seen = true;
		} else {
			iColumn = -1;
		}
	}
	pScan->opMask = opMask;
	pScan->k = 0;
	pScan->aiCur[0] = iCur;
	pScan->aiColumn[0] = iColumn;
	pScan->nEquiv = 1;
	pScan->iEquiv = 1;
	return whereScanNext(pScan);
}

/**
 * Analogue of whereScanInit() but also can be called for spaces
 * created via Lua interface. This function doesn't rely on
 * regular sql structures representing data dictionary.
 *
 * @param scan The WhereScan object being initialized.
 * @param clause The WHERE clause to be scanned.
 * @param cursor Cursor to scan for.
 * @param column Column to scan for.
 * @param op_mask Operator(s) to scan for.
 * @param space_def Def of the space related to WHERE clause.
 * @param key_def Def of the index to be used to satisfy WHERE
 *                clause. May be NULL.
 *
 * @retval Return a pointer to the first match. Return NULL if
 *         there are no matches.
 */
static WhereTerm *
where_scan_init(struct WhereScan *scan, struct WhereClause *clause,
		    int cursor, int column, uint32_t op_mask,
		    struct space_def *space_def, struct key_def *key_def)
{
	scan->pOrigWC = clause;
	scan->pWC = clause;
	scan->pIdxExpr = NULL;
	scan->idx_type = FIELD_TYPE_SCALAR;
	scan->coll = NULL;
	scan->is_column_seen = false;
	if (key_def != NULL) {
		int j = column;
		column = key_def->parts[j].fieldno;
		scan->idx_type = space_def->fields[column].type;
		uint32_t coll_id = space_def->fields[column].coll_id;
		struct coll_id *coll = coll_by_id(coll_id);
		scan->coll = coll != NULL ? coll->coll : NULL;
		scan->is_column_seen = true;
	}
	scan->opMask = op_mask;
	scan->k = 0;
	scan->aiCur[0] = cursor;
	scan->aiColumn[0] = column;
	scan->nEquiv = 1;
	scan->iEquiv = 1;
	return whereScanNext(scan);
}

/*
 * Search for a term in the WHERE clause that is of the form "X <op> <expr>"
 * where X is a reference to the iColumn of table iCur or of index pIdx
 * if pIdx!=0 and <op> is one of the WO_xx operator codes specified by
 * the op parameter.  Return a pointer to the term.  Return 0 if not found.
 *
 * If pIdx!=0 then it must be one of the indexes of table iCur.
 * Search for terms matching the iColumn-th column of pIdx
 * rather than the iColumn-th column of table iCur.
 *
 * The term returned might by Y=<expr> if there is another constraint in
 * the WHERE clause that specifies that X=Y.  Any such constraints will be
 * identified by the WO_EQUIV bit in the pTerm->eOperator field.  The
 * aiCur[]/iaColumn[] arrays hold X and all its equivalents. There are 11
 * slots in aiCur[]/aiColumn[] so that means we can look for X plus up to 10
 * other equivalent values.  Hence a search for X will return <expr> if X=A1
 * and A1=A2 and A2=A3 and ... and A9=A10 and A10=<expr>.
 *
 * If there are multiple terms in the WHERE clause of the form "X <op> <expr>"
 * then try for the one with no dependencies on <expr> - in other words where
 * <expr> is a constant expression of some kind.  Only return entries of
 * the form "X <op> Y" where Y is a column in another table if no terms of
 * the form "X <op> <const-expr>" exist.   If no terms with a constant RHS
 * exist, try to return a term that does not use WO_EQUIV.
 */
WhereTerm *
sqlWhereFindTerm(WhereClause * pWC,	/* The WHERE clause to be searched */
		     int iCur,		/* Cursor number of LHS */
		     int iColumn,	/* Column number of LHS */
		     Bitmask notReady,	/* RHS must not overlap with this mask */
		     u32 op,		/* Mask of WO_xx values describing operator */
		     struct index_def *idx_def)
{
	WhereTerm *pResult = 0;
	WhereTerm *p;
	WhereScan scan;

	p = whereScanInit(&scan, pWC, iCur, iColumn, op, idx_def);
	op &= WO_EQ;
	while (p) {
		if ((p->prereqRight & notReady) == 0) {
			if (p->prereqRight == 0 && (p->eOperator & op) != 0)
				return p;
			if (pResult == 0)
				pResult = p;
		}
		p = whereScanNext(&scan);
	}
	return pResult;
}

/**
 * Analogue of sqlWhereFindTerm() but also can be called
 * for spaces created via Lua interface. This function doesn't
 * rely on regular sql structures representing data
 * dictionary.
 *
 * @param where_clause The WHERE clause to be examined.
 * @param cursor Cursor number of LHS.
 * @param column Column number of LHS
 * @param is_ready RHS must not overlap with this mask.
 * @param op Mask of WO_xx values describing operator.
 * @param space_def Def of the space related to WHERE clause.
 * @param key_def Def of the index to be used to satisfy WHERE
 *                clause. May be NULL.
 *
 * @retval New struct describing WHERE term.
 */
static inline struct WhereTerm *
where_clause_find_term(struct WhereClause *where_clause, int cursor, int column,
		       Bitmask is_ready, u32 op, struct space_def *space_def,
		       struct key_def *key_def)
{
	struct WhereTerm *result = NULL;
	struct WhereScan scan;
	struct WhereTerm *p = where_scan_init(&scan, where_clause, cursor,
					      column, op, space_def, key_def);
	op &= WO_EQ;
	while (p != NULL) {
		if ((p->prereqRight & is_ready) == 0) {
			if (p->prereqRight == 0 && (p->eOperator & op) != 0)
				return p;
			if (result == NULL)
				result = p;
		}
		p = whereScanNext(&scan);
	}
	return result;
}

/*
 * This function searches pList for an entry that matches the iCol-th column
 * of index pIdx.
 *
 * If such an expression is found, its index in pList->a[] is returned. If
 * no expression is found, -1 is returned.
 */
static int
findIndexCol(Parse * pParse,	/* Parse context */
	     ExprList * pList,	/* Expression list to search */
	     int iBase,		/* Cursor for table associated with pIdx */
	     struct index_def *idx_def,
	     int iCol)		/* Column of index to match */
{
	struct key_part *part_to_match = &idx_def->key_def->parts[iCol];
	for (int i = 0; i < pList->nExpr; i++) {
		Expr *p = sqlExprSkipCollate(pList->a[i].pExpr);
		if (p->op == TK_COLUMN && p->iTable == iBase &&
		    p->iColumn == (int) part_to_match->fieldno) {
			bool is_found;
			uint32_t id;
			struct coll *unused;
			if (sql_expr_coll(pParse, pList->a[i].pExpr,
					  &is_found, &id, &unused) != 0)
				return -1;
			if (id == part_to_match->coll_id)
				return i;
		}
	}

	return -1;
}

/*
 * Return true if the DISTINCT expression-list passed as the third argument
 * is redundant.
 *
 * A DISTINCT list is redundant if any subset of the columns in the
 * DISTINCT list are collectively unique and individually non-null.
 */
static int
isDistinctRedundant(Parse * pParse,		/* Parsing context */
		    SrcList * pTabList,		/* The FROM clause */
		    WhereClause * pWC,		/* The WHERE clause */
		    ExprList * pDistinct)	/* The result set that needs to be DISTINCT */
{
	int iBase;

	/* If there is more than one table or sub-select in the FROM clause of
	 * this query, then it will not be possible to show that the DISTINCT
	 * clause is redundant.
	 */
	if (pTabList->nSrc != 1)
		return 0;
	iBase = pTabList->a[0].iCursor;
	struct space *space = pTabList->a[0].space;

	/* If any of the expressions is an IPK column on table iBase, then return
	 * true. Note: The (p->iTable==iBase) part of this test may be false if the
	 * current SELECT is a correlated sub-query.
	 */
	for (int i = 0; i < pDistinct->nExpr; i++) {
		Expr *p = sqlExprSkipCollate(pDistinct->a[i].pExpr);
		if (p->op == TK_COLUMN && p->iTable == iBase && p->iColumn < 0)
			return 1;
	}
	if (space == NULL)
		return 0;
	/* Loop through all indices on the table, checking each to see if it makes
	 * the DISTINCT qualifier redundant. It does so if:
	 *
	 *   1. The index is itself UNIQUE, and
	 *
	 *   2. All of the columns in the index are either part of the pDistinct
	 *      list, or else the WHERE clause contains a term of the form "col=X",
	 *      where X is a constant value. The collation sequences of the
	 *      comparison and select-list expressions must match those of the index.
	 *
	 *   3. All of those index columns for which the WHERE clause does not
	 *      contain a "col=X" term are subject to a NOT NULL constraint.
	 */
	for (uint32_t j = 0; j < space->index_count; ++j) {
		struct index_def *def = space->index[j]->def;
		if (!def->opts.is_unique)
			continue;
		uint32_t col_count = def->key_def->part_count;
		uint32_t i;
		for (i = 0; i < col_count; i++) {
			if (sqlWhereFindTerm(pWC, iBase, i, ~(Bitmask) 0,
						 WO_EQ, def) == 0) {
				if (findIndexCol(pParse, pDistinct, iBase, def,
						 i) < 0)
					break;
				uint32_t x = def->key_def->parts[i].fieldno;
				if (space->def->fields[x].is_nullable)
					break;
			}
		}
		/*
		 * This index implies that the DISTINCT
		 * qualifier is redundant.
		 */
		if (i == col_count)
			return 1;
	}

	return 0;
}

/*
 * Estimate the logarithm of the input value to base 2.
 */
static LogEst
estLog(LogEst N)
{
	return N <= 10 ? 0 : sqlLogEst(N) - 33;
}

/*
 * Convert OP_Column opcodes to OP_Copy in previously generated code.
 *
 * This routine runs over generated VDBE code and translates OP_Column
 * opcodes into OP_Copy when the table is being accessed via co-routine
 * instead of via table lookup.
 */
static void
translateColumnToCopy(Vdbe * v,		/* The VDBE containing code to translate */
		      int iStart,	/* Translate from this opcode to the end */
		      int iTabCur,	/* OP_Column references to this table */
		      int iRegister)	/* The first column is in this register */
{
	VdbeOp *pOp = sqlVdbeGetOp(v, iStart);
	int iEnd = sqlVdbeCurrentAddr(v);
	for (; iStart < iEnd; iStart++, pOp++) {
		if (pOp->p1 != iTabCur)
			continue;
		if (pOp->opcode == OP_Column) {
			pOp->opcode = OP_Copy;
			pOp->p1 = pOp->p2 + iRegister;
			pOp->p2 = pOp->p3;
			pOp->p3 = 0;
		}
	}
}

/*
 * Return TRUE if the WHERE clause term pTerm is of a form where it
 * could be used with an index to access pSrc, assuming an appropriate
 * index existed.
 */
static int
termCanDriveIndex(WhereTerm * pTerm,	/* WHERE clause term to check */
		  struct SrcList_item *pSrc,	/* Table we are trying to access */
		  Bitmask notReady	/* Tables in outer loops of the join */
    )
{
	if (pTerm->leftCursor != pSrc->iCursor)
		return 0;
	if ((pTerm->eOperator & WO_EQ) == 0)
		return 0;
	if ((pTerm->prereqRight & notReady) != 0)
		return 0;
	if (pTerm->u.leftColumn < 0)
		return 0;
	enum field_type type = pSrc->space->def->fields[pTerm->u.leftColumn].type;
	enum field_type expr_type = expr_cmp_mutual_type(pTerm->pExpr);
	if (!field_type1_contains_type2(expr_type, type))
		return 0;
	return 1;
}

/**
 * Generate a code that will create a tuple, which is supposed to be inserted
 * in the ephemeral index space. The created tuple consists of rowid and
 * fields described in the index key description.
 *
 * @param parse Parsing context.
 * @param key_def The index key description.
 * @param cursor Cursor of source space from which values for tuple are fetched.
 * @param reg_out Register to contain the created tuple.
 * @param reg_eph Register holding pointer to ephemeral index.
 */
static void
vdbe_emit_ephemeral_index_tuple(struct Parse *parse,
				const struct key_def *key_def, int cursor,
				int reg_out, int reg_eph)
{
	assert(reg_out != 0);
	struct Vdbe *v = parse->pVdbe;
	int col_cnt = key_def->part_count;
	int reg_base = sqlGetTempRange(parse, col_cnt + 1);
	for (int j = 0; j < col_cnt; j++) {
		uint32_t tabl_col = key_def->parts[j].fieldno;
		sqlVdbeAddOp3(v, OP_Column, cursor, tabl_col, reg_base + j);
	}
	sqlVdbeAddOp2(v, OP_NextIdEphemeral, reg_eph, reg_base + col_cnt);
	sqlVdbeAddOp3(v, OP_MakeRecord, reg_base, col_cnt + 1, reg_out);
	sqlReleaseTempRange(parse, reg_base, col_cnt + 1);
}

/*
 * Generate code to construct the ephemeral space that contains all used in
 * query fields of one of the tables that participate in the query. The source
 * table is determined by query planner. This ephemeral space will be known as
 * an "ephemeral index". The PK definition of ephemeral index contains all of
 * its fields. Also, this functions set up the WhereLevel object pLevel so
 * that the code generator makes use of ephemeral index.
 */
static void
constructAutomaticIndex(Parse * pParse,			/* The parsing context */
			WhereClause * pWC,		/* The WHERE clause */
			struct SrcList_item *pSrc,	/* The FROM clause term to get the next index */
			Bitmask notReady,		/* Mask of cursors that are not available */
			WhereLevel * pLevel)		/* Write new index here */
{
	int nKeyCol;		/* Number of columns in the constructed index */
	WhereTerm *pTerm;	/* A single term of the WHERE clause */
	WhereTerm *pWCEnd;	/* End of pWC->a[] */
	Vdbe *v;		/* Prepared statement under construction */
	int addrInit;		/* Address of the initialization bypass jump */
	int addrTop;		/* Top of the index fill loop */
	int regRecord;		/* Register holding an index record */
	int n;			/* Column counter */
	int i;			/* Loop counter */
	int mxBitCol;		/* Maximum column in pSrc->colUsed */
	WhereLoop *pLoop;	/* The Loop object */
	Bitmask idxCols;	/* Bitmap of columns used for indexing */
	Bitmask extraCols;	/* Bitmap of additional columns */

	/* Generate code to skip over the creation and initialization of the
	 * transient index on 2nd and subsequent iterations of the loop.
	 */
	v = pParse->pVdbe;
	assert(v != 0);
	addrInit = sqlVdbeAddOp0(v, OP_Once);
	VdbeCoverage(v);

	/* Count the number of columns that will be added to the index
	 * and used to match WHERE clause constraints
	 */
	nKeyCol = 0;
	pWCEnd = &pWC->a[pWC->nTerm];
	pLoop = pLevel->pWLoop;
	idxCols = 0;
	for (pTerm = pWC->a; pTerm < pWCEnd; pTerm++) {
		if (termCanDriveIndex(pTerm, pSrc, notReady)) {
			int iCol = pTerm->u.leftColumn;
			Bitmask cMask =
			    iCol >= BMS ? MASKBIT(BMS - 1) : MASKBIT(iCol);
			if ((idxCols & cMask) == 0) {
				if (whereLoopResize
				    (pParse->db, pLoop, nKeyCol + 1)) {
					pParse->is_aborted = true;
					return;
				}
				pLoop->aLTerm[nKeyCol++] = pTerm;
				idxCols |= cMask;
			}
		}
	}
	assert(nKeyCol > 0);
	pLoop->nEq = pLoop->nLTerm = nKeyCol;
	pLoop->wsFlags = WHERE_COLUMN_EQ | WHERE_IDX_ONLY | WHERE_INDEXED
	    | WHERE_AUTO_INDEX;

	/* Count the number of additional columns needed to create a
	 * covering index.  A "covering index" is an index that contains all
	 * columns that are needed by the query.  With a covering index, the
	 * original table never needs to be accessed.  Automatic indices must
	 * be a covering index because the index will not be updated if the
	 * original table changes and the index and table cannot both be used
	 * if they go out of sync.
	 */
	extraCols = pSrc->colUsed & (~idxCols | MASKBIT(BMS - 1));
	struct space *space = pSrc->space;
	mxBitCol = MIN(BMS - 1, space->def->field_count);
	for (i = 0; i < mxBitCol; i++) {
		if (extraCols & MASKBIT(i))
			nKeyCol++;
	}
	if (pSrc->colUsed & MASKBIT(BMS - 1)) {
		nKeyCol += space->def->field_count - BMS + 1;
	}

	n = 0;
	idxCols = 0;
	size_t size;
	struct key_part_def *parts = region_alloc_array(&pParse->region,
							typeof(parts[0]),
							nKeyCol, &size);
	if (parts == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "parts");
		pParse->is_aborted = true;
		return;
	}
	for (pTerm = pWC->a; pTerm < pWCEnd; pTerm++) {
		if (termCanDriveIndex(pTerm, pSrc, notReady)) {
			int iCol = pTerm->u.leftColumn;
			Bitmask cMask =
			    iCol >= BMS ? MASKBIT(BMS - 1) : MASKBIT(iCol);
			testcase(iCol == BMS - 1);
			testcase(iCol == BMS);
			if ((idxCols & cMask) == 0) {
				idxCols |= cMask;
				struct field_def *field =
					&space->def->fields[iCol];
				struct key_part_def *part = &parts[n];
				part->fieldno = iCol;
				part->type = field->type;
				part->nullable_action = field->nullable_action;
				part->is_nullable = field->is_nullable;
				part->sort_order = SORT_ORDER_ASC;
				part->coll_id = field->coll_id;
				part->path = NULL;
				n++;
			}
		}
	}
	assert((u32) n == pLoop->nEq);

	/* Add additional columns needed to make the automatic index into
	 * a covering index
	 */
	for (i = 0; i < mxBitCol; i++) {
		if (extraCols & MASKBIT(i)) {
			struct field_def *field = &space->def->fields[i];
			struct key_part_def *part = &parts[n];
			part->fieldno = i;
			part->type = field->type;
			part->nullable_action = field->nullable_action;
			part->is_nullable = field->is_nullable;
			part->sort_order = SORT_ORDER_ASC;
			part->coll_id = field->coll_id;
			part->path = NULL;
			n++;
		}
	}
	if (pSrc->colUsed & MASKBIT(BMS - 1)) {
		for (i = BMS - 1; i < (int)space->def->field_count; i++) {
			struct field_def *field = &space->def->fields[i];
			struct key_part_def *part = &parts[n];
			part->fieldno = i;
			part->type = field->type;
			part->nullable_action = field->nullable_action;
			part->is_nullable = field->is_nullable;
			part->sort_order = SORT_ORDER_ASC;
			part->coll_id = field->coll_id;
			part->path = NULL;
			n++;
		}
	}
	assert(n == nKeyCol);

	struct key_def *key_def = key_def_new(parts, nKeyCol, false);
	if (key_def == NULL) {
		pParse->is_aborted = true;
		return;
	}

	/* Construct the index definition to describe this index. */
	struct index_opts opts;
	index_opts_create(&opts);
	const char *idx_name = "ephemeral index";
	struct index_def *idx_def = index_def_new(space->def->id, 0, idx_name,
						  strlen(idx_name), TREE, &opts,
						  key_def, NULL);
	key_def_delete(key_def);
	if (idx_def == NULL) {
		pParse->is_aborted = true;
		return;
	}
	pLoop->index_def = idx_def;

	/* Create the automatic index */
	assert(pLevel->iIdxCur >= 0);
	pLevel->iIdxCur = pParse->nTab++;
	struct sql_key_info *pk_info =
		sql_key_info_new_from_key_def(pParse->db, idx_def->key_def);
	if (pk_info == NULL) {
		pParse->is_aborted = true;
		return;
	}
	int reg_eph = sqlGetTempReg(pParse);
	sqlVdbeAddOp4(v, OP_OpenTEphemeral, reg_eph, nKeyCol + 1, 0,
		      (char *)pk_info, P4_KEYINFO);
	sqlVdbeAddOp3(v, OP_IteratorOpen, pLevel->iIdxCur, 0, reg_eph);
	VdbeComment((v, "for %s", space->def->name));

	/* Fill the automatic index with content */
	sqlExprCachePush(pParse);
	assert(pWC->pWInfo->pTabList->a[pLevel->iFrom].fg.viaCoroutine == 0);
	int cursor = pLevel->iTabCur;
	addrTop = sqlVdbeAddOp1(v, OP_Rewind, cursor);
	VdbeCoverage(v);
	regRecord = sqlGetTempReg(pParse);
	vdbe_emit_ephemeral_index_tuple(pParse, idx_def->key_def, cursor,
					regRecord, reg_eph);
	sqlVdbeAddOp2(v, OP_IdxInsert, regRecord, reg_eph);
	sqlVdbeAddOp2(v, OP_Next, cursor, addrTop + 1);
	VdbeCoverage(v);
	sqlVdbeChangeP5(v, SQL_STMTSTATUS_AUTOINDEX);
	sqlVdbeJumpHere(v, addrTop);
	sqlReleaseTempReg(pParse, regRecord);
	sqlReleaseTempReg(pParse, reg_eph);
	sqlExprCachePop(pParse);

	/* Jump here when skipping the initialization */
	sqlVdbeJumpHere(v, addrInit);
}

/*
 * Estimate the location of a particular key among all keys in an
 * index.  Store the results in aStat as follows:
 *
 *    aStat[0]      Est. number of rows less than pRec
 *    aStat[1]      Est. number of rows equal to pRec
 *
 * Return the index of the sample that is the smallest sample that
 * is greater than or equal to pRec. Note that this index is not an index
 * into the aSample[] array - it is an index into a virtual set of samples
 * based on the contents of aSample[] and the number of fields in record
 * pRec.
 */
static int
whereKeyStats(Parse * pParse,	/* Database connection */
	      struct index_def *idx_def,
	      UnpackedRecord * pRec,	/* Vector of values to consider */
	      int roundUp,	/* Round up if true.  Round down if false */
	      tRowcnt * aStat)	/* OUT: stats written here */
{
	struct space *space = space_by_id(idx_def->space_id);
	assert(space != NULL);
	struct index *idx = space_index(space, idx_def->iid);
	assert(idx != NULL && idx->def->opts.stat != NULL);
	struct index_sample *samples = idx->def->opts.stat->samples;
	assert(idx->def->opts.stat->sample_count > 0);
	assert(idx->def->opts.stat->samples != NULL);
	assert(idx->def->opts.stat->sample_field_count >= pRec->nField);
	int iCol;		/* Index of required stats in anEq[] etc. */
	int i;			/* Index of first sample >= pRec */
	int iSample;		/* Smallest sample larger than or equal to pRec */
	int iMin = 0;		/* Smallest sample not yet tested */
	int iTest;		/* Next sample to test */
	int res;		/* Result of comparison operation */
	int nField;		/* Number of fields in pRec */
	tRowcnt iLower = 0;	/* anLt[] + anEq[] of largest sample pRec is > */

#ifndef SQL_DEBUG
	UNUSED_PARAMETER(pParse);
#endif
	assert(pRec != 0);
	assert(pRec->nField > 0);

	/* Do a binary search to find the first sample greater than or equal
	 * to pRec. If pRec contains a single field, the set of samples to search
	 * is simply the aSample[] array. If the samples in aSample[] contain more
	 * than one fields, all fields following the first are ignored.
	 *
	 * If pRec contains N fields, where N is more than one, then as well as the
	 * samples in aSample[] (truncated to N fields), the search also has to
	 * consider prefixes of those samples. For example, if the set of samples
	 * in aSample is:
	 *
	 *     aSample[0] = (a, 5)
	 *     aSample[1] = (a, 10)
	 *     aSample[2] = (b, 5)
	 *     aSample[3] = (c, 100)
	 *     aSample[4] = (c, 105)
	 *
	 * Then the search space should ideally be the samples above and the
	 * unique prefixes [a], [b] and [c]. But since that is hard to organize,
	 * the code actually searches this set:
	 *
	 *     0: (a)
	 *     1: (a, 5)
	 *     2: (a, 10)
	 *     3: (a, 10)
	 *     4: (b)
	 *     5: (b, 5)
	 *     6: (c)
	 *     7: (c, 100)
	 *     8: (c, 105)
	 *     9: (c, 105)
	 *
	 * For each sample in the aSample[] array, N samples are present in the
	 * effective sample array. In the above, samples 0 and 1 are based on
	 * sample aSample[0]. Samples 2 and 3 on aSample[1] etc.
	 *
	 * Often, sample i of each block of N effective samples has (i+1) fields.
	 * Except, each sample may be extended to ensure that it is greater than or
	 * equal to the previous sample in the array. For example, in the above,
	 * sample 2 is the first sample of a block of N samples, so at first it
	 * appears that it should be 1 field in size. However, that would make it
	 * smaller than sample 1, so the binary search would not work. As a result,
	 * it is extended to two fields. The duplicates that this creates do not
	 * cause any problems.
	 */
	nField = pRec->nField;
	iCol = 0;
	uint32_t sample_count = idx->def->opts.stat->sample_count;
	iSample = sample_count * nField;
	do {
		int iSamp;	/* Index in aSample[] of test sample */
		int n;		/* Number of fields in test sample */

		iTest = (iMin + iSample) / 2;
		iSamp = iTest / nField;
		if (iSamp > 0) {
			/* The proposed effective sample is a prefix of sample aSample[iSamp].
			 * Specifically, the shortest prefix of at least (1 + iTest%nField)
			 * fields that is greater than the previous effective sample.
			 */
			for (n = (iTest % nField) + 1; n < nField; n++) {
				if (samples[iSamp - 1].lt[n - 1] !=
				    samples[iSamp].lt[n - 1])
					break;
			}
		} else {
			n = iTest + 1;
		}

		pRec->nField = n;
		res =
		    sqlVdbeRecordCompareMsgpack(samples[iSamp].sample_key,
						    pRec);
		if (res < 0) {
			iLower =
			    samples[iSamp].lt[n - 1] + samples[iSamp].eq[n - 1];
			iMin = iTest + 1;
		} else if (res == 0 && n < nField) {
			iLower = samples[iSamp].lt[n - 1];
			iMin = iTest + 1;
			res = -1;
		} else {
			iSample = iTest;
			iCol = n - 1;
		}
	} while (res && iMin < iSample);
	i = iSample / nField;

#ifdef SQL_DEBUG
	/* The following assert statements check that the binary search code
	 * above found the right answer. This block serves no purpose other
	 * than to invoke the asserts.
	 */
	if (pParse->db->mallocFailed == 0) {
		if (res == 0) {
			/* If (res==0) is true, then pRec must be equal to sample i. */
			assert(i < (int) sample_count);
			assert(iCol == nField - 1);
			pRec->nField = nField;
			assert(0 ==
			       sqlVdbeRecordCompareMsgpack(samples[i].sample_key,
							       pRec)
			       || pParse->db->mallocFailed);
		} else {
			/* Unless i==pIdx->nSample, indicating that pRec is larger than
			 * all samples in the aSample[] array, pRec must be smaller than the
			 * (iCol+1) field prefix of sample i.
			 */
			assert(i <= (int) sample_count && i >= 0);
			pRec->nField = iCol + 1;
			assert(i == (int) sample_count ||
			       sqlVdbeRecordCompareMsgpack(samples[i].sample_key,
							       pRec) > 0
			       || pParse->db->mallocFailed);

			/* if i==0 and iCol==0, then record pRec is smaller than all samples
			 * in the aSample[] array. Otherwise, if (iCol>0) then pRec must
			 * be greater than or equal to the (iCol) field prefix of sample i.
			 * If (i>0), then pRec must also be greater than sample (i-1).
			 */
			if (iCol > 0) {
				pRec->nField = iCol;
				assert(sqlVdbeRecordCompareMsgpack
				       (samples[i].sample_key, pRec) <= 0
				       || pParse->db->mallocFailed);
			}
			if (i > 0) {
				pRec->nField = nField;
				assert(sqlVdbeRecordCompareMsgpack
				       (samples[i - 1].sample_key, pRec) < 0 ||
				       pParse->db->mallocFailed);
			}
		}
	}
#endif				/* ifdef SQL_DEBUG */

	if (res == 0) {
		/* Record pRec is equal to sample i */
		assert(iCol == nField - 1);
		aStat[0] = samples[i].lt[iCol];
		aStat[1] = samples[i].eq[iCol];
	} else {
		/* At this point, the (iCol+1) field prefix of aSample[i] is the first
		 * sample that is greater than pRec. Or, if i==pIdx->nSample then pRec
		 * is larger than all samples in the array.
		 */
		tRowcnt iUpper, iGap;
		if (i >= (int) sample_count) {
			iUpper = sqlLogEstToInt(idx->def->opts.stat->tuple_log_est[0]);
		} else {
			iUpper = samples[i].lt[iCol];
		}

		if (iLower >= iUpper) {
			iGap = 0;
		} else {
			iGap = iUpper - iLower;
		}
		if (roundUp) {
			iGap = (iGap * 2) / 3;
		} else {
			iGap = iGap / 3;
		}
		aStat[0] = iLower + iGap;
		aStat[1] = idx->def->opts.stat->avg_eq[iCol];
	}

	/* Restore the pRec->nField value before returning.  */
	pRec->nField = nField;
	return i;
}

/*
 * If it is not NULL, pTerm is a term that provides an upper or lower
 * bound on a range scan. Without considering pTerm, it is estimated
 * that the scan will visit nNew rows. This function returns the number
 * estimated to be visited after taking pTerm into account.
 *
 * If the user explicitly specified a likelihood() value for this term,
 * then the return value is the likelihood multiplied by the number of
 * input rows. Otherwise, this function assumes that an "IS NOT NULL" term
 * has a likelihood of 0.50, and any other term a likelihood of 0.25.
 */
static LogEst
whereRangeAdjust(WhereTerm * pTerm, LogEst nNew)
{
	LogEst nRet = nNew;
	if (pTerm) {
		if (pTerm->truthProb <= 0) {
			nRet += pTerm->truthProb;
		} else if ((pTerm->wtFlags & TERM_VNULL) == 0) {
			nRet -= 20;
			assert(20 == sqlLogEst(4));
		}
	}
	return nRet;
}

/*
 * This function is called to estimate the number of rows visited by a
 * range-scan on a skip-scan index. For example:
 *
 *   CREATE INDEX i1 ON t1(a, b, c);
 *   SELECT * FROM t1 WHERE a=? AND c BETWEEN ? AND ?;
 *
 * Value pLoop->nOut is currently set to the estimated number of rows
 * visited for scanning (a=? AND b=?). This function reduces that estimate
 * by some factor to account for the (c BETWEEN ? AND ?) expression based
 * on the stat4 data for the index. this scan will be peformed multiple
 * times (once for each (a,b) combination that matches a=?) is dealt with
 * by the caller.
 *
 * It does this by scanning through all stat4 samples, comparing values
 * extracted from pLower and pUpper with the corresponding column in each
 * sample. If L and U are the number of samples found to be less than or
 * equal to the values extracted from pLower and pUpper respectively, and
 * N is the total number of samples, the pLoop->nOut value is adjusted
 * as follows:
 *
 *   nOut = nOut * ( min(U - L, 1) / N )
 *
 * If pLower is NULL, or a value cannot be extracted from the term, L is
 * set to zero. If pUpper is NULL, or a value cannot be extracted from it,
 * U is set to N.
 *
 * Normally, this function sets *pbDone to 1 before returning. However,
 * if no value can be extracted from either pLower or pUpper (and so the
 * estimate of the number of rows delivered remains unchanged), *pbDone
 * is left as is.
 *
 * If an error occurs, an sql error code is returned. Otherwise,
 * 0.
 */
static int
whereRangeSkipScanEst(Parse * pParse,		/* Parsing & code generating context */
		      WhereTerm * pLower,	/* Lower bound on the range. ex: "x>123" Might be NULL */
		      WhereTerm * pUpper,	/* Upper bound on the range. ex: "x<455" Might be NULL */
		      WhereLoop * pLoop,	/* Update the .nOut value of this loop */
		      int *pbDone)		/* Set to true if at least one expr. value extracted */
{
	struct index_def *p = pLoop->index_def;
	struct space *space = space_by_id(p->space_id);
	assert(space != NULL);
	struct index *index = space_index(space, p->iid);
	assert(index != NULL && index->def->opts.stat != NULL);
	int nEq = pLoop->nEq;
	sql *db = pParse->db;
	int nLower = -1;
	int nUpper = index->def->opts.stat->sample_count + 1;
	int rc = 0;
	enum field_type type = p->key_def->parts[nEq].type;

	sql_value *p1 = 0;	/* Value extracted from pLower */
	sql_value *p2 = 0;	/* Value extracted from pUpper */
	sql_value *pVal = 0;	/* Value extracted from record */

	struct coll *coll = p->key_def->parts[nEq].coll;
	if (pLower) {
		rc = sqlStat4ValueFromExpr(pParse, pLower->pExpr->pRight,
					       type, &p1);
		nLower = 0;
	}
	if (pUpper != NULL && rc == 0) {
		rc = sqlStat4ValueFromExpr(pParse, pUpper->pExpr->pRight,
					       type, &p2);
		nUpper = p2 ? 0 : index->def->opts.stat->sample_count;
	}

	if (p1 || p2) {
		int i;
		int nDiff;
		struct index_sample *samples = index->def->opts.stat->samples;
		uint32_t sample_count = index->def->opts.stat->sample_count;
		for (i = 0; rc == 0 && i < (int) sample_count; i++) {
			rc = sql_stat4_column(db, samples[i].sample_key, nEq,
					      &pVal);
			if (rc == 0 && p1 != NULL) {
				int res = sqlMemCompare(p1, pVal, coll);
				if (res >= 0)
					nLower++;
			}
			if (rc == 0 && p2 != NULL) {
				int res = sqlMemCompare(p2, pVal, coll);
				if (res >= 0)
					nUpper++;
			}
		}
		nDiff = (nUpper - nLower);
		if (nDiff <= 0)
			nDiff = 1;

		/* If there is both an upper and lower bound specified, and the
		 * comparisons indicate that they are close together, use the fallback
		 * method (assume that the scan visits 1/64 of the rows) for estimating
		 * the number of rows visited. Otherwise, estimate the number of rows
		 * using the method described in the header comment for this function.
		 */
		if (nDiff != 1 || pUpper == 0 || pLower == 0) {
			int nAdjust =
			    (sqlLogEst(sample_count) -
			     sqlLogEst(nDiff));
			pLoop->nOut -= nAdjust;
			*pbDone = 1;
			WHERETRACE(0x10,
				   ("range skip-scan regions: %u..%u  adjust=%d est=%d\n",
				    nLower, nUpper, nAdjust * -1, pLoop->nOut));
		}

	} else {
		assert(*pbDone == 0);
	}

	sqlValueFree(p1);
	sqlValueFree(p2);
	sqlValueFree(pVal);

	return rc;
}

/*
 * This function is used to estimate the number of rows that will be visited
 * by scanning an index for a range of values. The range may have an upper
 * bound, a lower bound, or both. The WHERE clause terms that set the upper
 * and lower bounds are represented by pLower and pUpper respectively. For
 * example, assuming that index p is on t1(a):
 *
 *   ... FROM t1 WHERE a > ? AND a < ? ...
 *                    |_____|   |_____|
 *                       |         |
 *                     pLower    pUpper
 *
 * If either of the upper or lower bound is not present, then NULL is passed in
 * place of the corresponding WhereTerm.
 *
 * The value in (pBuilder->pNew->nEq) is the number of the index
 * column subject to the range constraint. Or, equivalently, the number of
 * equality constraints optimized by the proposed index scan. For example,
 * assuming index p is on t1(a, b), and the SQL query is:
 *
 *   ... FROM t1 WHERE a = ? AND b > ? AND b < ? ...
 *
 * then nEq is set to 1 (as the range restricted column, b, is the second
 * left-most column of the index). Or, if the query is:
 *
 *   ... FROM t1 WHERE a > ? AND a < ? ...
 *
 * then nEq is set to 0.
 *
 * When this function is called, *pnOut is set to the sqlLogEst() of the
 * number of rows that the index scan is expected to visit without
 * considering the range constraints. If nEq is 0, then *pnOut is the number of
 * rows in the index. Assuming no error occurs, *pnOut is adjusted (reduced)
 * to account for the range constraints pLower and pUpper.
 *
 * In the absence of _sql_stat4 ANALYZE data, or if such data cannot be
 * used, a single range inequality reduces the search space by a factor of 4.
 * and a pair of constraints (x>? AND x<?) reduces the expected number of
 * rows visited by a factor of 64.
 */
static int
whereRangeScanEst(Parse * pParse,	/* Parsing & code generating context */
		  WhereLoopBuilder * pBuilder, WhereTerm * pLower,	/* Lower bound on the range. ex: "x>123" Might be NULL */
		  WhereTerm * pUpper,	/* Upper bound on the range. ex: "x<455" Might be NULL */
		  WhereLoop * pLoop)	/* Modify the .nOut and maybe .rRun fields */
{
	int rc = 0;
	int nOut = pLoop->nOut;
	LogEst nNew;

	struct index_def *p = pLoop->index_def;
	int nEq = pLoop->nEq;
	struct space *space = space_by_id(p->space_id);
	assert(space != NULL);
	struct index *idx = space_index(space, p->iid);
	assert(idx != NULL);
	struct index_stat *stat = idx->def->opts.stat;
	/*
	 * Create surrogate stat in case ANALYZE command hasn't
	 * been ran. Simply fill it with zeros.
	 */
	struct index_stat surrogate_stat;
	memset(&surrogate_stat, 0, sizeof(surrogate_stat));
	if (stat == NULL)
		stat = &surrogate_stat;
	if (stat->sample_count > 0 && nEq < (int) stat->sample_field_count) {
		if (nEq == pBuilder->nRecValid) {
			UnpackedRecord *pRec = pBuilder->pRec;
			tRowcnt a[2];
			int nBtm = pLoop->nBtm;
			int nTop = pLoop->nTop;

			/* Variable iLower will be set to the estimate of the number of rows in
			 * the index that are less than the lower bound of the range query. The
			 * lower bound being the concatenation of $P and $L, where $P is the
			 * key-prefix formed by the nEq values matched against the nEq left-most
			 * columns of the index, and $L is the value in pLower.
			 *
			 * Or, if pLower is NULL or $L cannot be extracted from it (because it
			 * is not a simple variable or literal value), the lower bound of the
			 * range is $P. Due to a quirk in the way whereKeyStats() works, even
			 * if $L is available, whereKeyStats() is called for both ($P) and
			 * ($P:$L) and the larger of the two returned values is used.
			 *
			 * Similarly, iUpper is to be set to the estimate of the number of rows
			 * less than the upper bound of the range query. Where the upper bound
			 * is either ($P) or ($P:$U). Again, even if $U is available, both values
			 * of iUpper are requested of whereKeyStats() and the smaller used.
			 *
			 * The number of rows between the two bounds is then just iUpper-iLower.
			 */
			tRowcnt iLower;	/* Rows less than the lower bound */
			tRowcnt iUpper;	/* Rows less than the upper bound */
			int iLwrIdx = -2;	/* aSample[] for the lower bound */
			int iUprIdx = -1;	/* aSample[] for the upper bound */

			if (pRec) {
				testcase(pRec->nField != pBuilder->nRecValid);
				pRec->nField = pBuilder->nRecValid;
			}
			/* Determine iLower and iUpper using ($P) only. */
			if (nEq == 0) {
				/*
				 * In this simple case, there are no any
				 * equality constraints, so initially all rows
				 * are in range.
				 */
				iLower = 0;
				iUpper = index_size(idx);
			} else {
				/* Note: this call could be optimized away - since the same values must
				 * have been requested when testing key $P in whereEqualScanEst().
				 */
				whereKeyStats(pParse, p, pRec, 0, a);
				iLower = a[0];
				iUpper = a[0] + a[1];
			}

			assert(pLower == 0
			       || (pLower->eOperator & (WO_GT | WO_GE)) != 0);
			assert(pUpper == 0
			       || (pUpper->eOperator & (WO_LT | WO_LE)) != 0);
			if (p->key_def->parts[nEq].sort_order !=
			    SORT_ORDER_ASC) {
				/* The roles of pLower and pUpper are swapped for a DESC index */
				SWAP(pLower, pUpper);
				SWAP(nBtm, nTop);
			}

			/* If possible, improve on the iLower estimate using ($P:$L). */
			if (pLower) {
				int n;	/* Values extracted from pExpr */
				Expr *pExpr = pLower->pExpr->pRight;
				rc = sqlStat4ProbeSetValue(pParse, p, &pRec,
							       pExpr, nBtm, nEq,
							       &n);
				if (rc == 0 && n != 0) {
					tRowcnt iNew;
					u16 mask = WO_GT | WO_LE;
					if (sqlExprVectorSize(pExpr) > n)
						mask = (WO_LE | WO_LT);
					iLwrIdx =
					    whereKeyStats(pParse, p, pRec, 0,
							  a);
					iNew =
					    a[0] +
					    ((pLower->
					      eOperator & mask) ? a[1] : 0);
					if (iNew > iLower)
						iLower = iNew;
					nOut--;
					pLower = 0;
				}
			}

			/* If possible, improve on the iUpper estimate using ($P:$U). */
			if (pUpper) {
				int n;	/* Values extracted from pExpr */
				Expr *pExpr = pUpper->pExpr->pRight;
				rc = sqlStat4ProbeSetValue(pParse, p, &pRec,
							       pExpr, nTop, nEq,
							       &n);
				if (rc == 0 && n != 0) {
					tRowcnt iNew;
					u16 mask = WO_GT | WO_LE;
					if (sqlExprVectorSize(pExpr) > n)
						mask = (WO_LE | WO_LT);
					iUprIdx =
					    whereKeyStats(pParse, p, pRec, 1,
							  a);
					iNew =
					    a[0] +
					    ((pUpper->
					      eOperator & mask) ? a[1] : 0);
					if (iNew < iUpper)
						iUpper = iNew;
					nOut--;
					pUpper = 0;
				}
			}

			pBuilder->pRec = pRec;
			if (rc == 0) {
				if (iUpper > iLower) {
					nNew = sqlLogEst(iUpper - iLower);
					/* TUNING:  If both iUpper and iLower are derived from the same
					 * sample, then assume they are 4x more selective.  This brings
					 * the estimated selectivity more in line with what it would be
					 * if estimated without the use of STAT4 table.
					 */
					if (iLwrIdx == iUprIdx)
						nNew -= 20;
					assert(20 == sqlLogEst(4));
				} else {
					nNew = 10;
					assert(10 == sqlLogEst(2));
				}
				if (nNew < nOut) {
					nOut = nNew;
				}
				WHERETRACE(0x10,
					   ("STAT4 range scan: %u..%u  est=%d\n",
					    (u32) iLower, (u32) iUpper, nOut));
			}
		} else {
			int bDone = 0;
			rc = whereRangeSkipScanEst(pParse, pLower, pUpper,
						   pLoop, &bDone);
			if (bDone)
				return rc;
		}
	}
	assert(pUpper == 0 || (pUpper->wtFlags & TERM_VNULL) == 0);
	nNew = whereRangeAdjust(pLower, nOut);
	nNew = whereRangeAdjust(pUpper, nNew);

	/* TUNING: If there is both an upper and lower limit and neither limit
	 * has an application-defined likelihood(), assume the range is
	 * reduced by an additional 75%. This means that, by default, an open-ended
	 * range query (e.g. col > ?) is assumed to match 1/4 of the rows in the
	 * index. While a closed range (e.g. col BETWEEN ? AND ?) is estimated to
	 * match 1/64 of the index.
	 */
	if (pLower && pLower->truthProb > 0 && pUpper && pUpper->truthProb > 0) {
		nNew -= 20;
	}

	nOut -= (pLower != 0) + (pUpper != 0);
	if (nNew < 10)
		nNew = 10;
	if (nNew < nOut)
		nOut = nNew;
#if defined(SQL_DEBUG)
	if (pLoop->nOut > nOut) {
		WHERETRACE(0x10, ("Range scan lowers nOut from %d to %d\n",
				  pLoop->nOut, nOut));
	}
#endif
	pLoop->nOut = (LogEst) nOut;
	return rc;
}

/*
 * Estimate the number of rows that will be returned based on
 * an equality constraint x=VALUE and where that VALUE occurs in
 * the histogram data.  This only works when x is the left-most
 * column of an index and sql_stat4 histogram data is available
 * for that index.  When pExpr==NULL that means the constraint is
 * "x IS NULL" instead of "x=VALUE".
 *
 * Write the estimated row count into *pnRow and return 0.
 * If unable to make an estimate, leave *pnRow unchanged and return
 * non-zero.
 *
 * This routine can fail if it is unable to load a collating sequence
 * required for string comparison, or if unable to allocate memory
 * for a UTF conversion required for comparison.  The error is stored
 * in the pParse structure.
 */
static int
whereEqualScanEst(Parse * pParse,	/* Parsing & code generating context */
		  WhereLoopBuilder * pBuilder, Expr * pExpr,	/* Expression for VALUE in the x=VALUE constraint */
		  tRowcnt * pnRow)	/* Write the revised row estimate here */
{
	struct index_def *p = pBuilder->pNew->index_def;
	int nEq = pBuilder->pNew->nEq;
	UnpackedRecord *pRec = pBuilder->pRec;
	int rc;			/* Subfunction return code */
	tRowcnt a[2];		/* Statistics */
	int bOk;

	assert(nEq >= 1);
	assert(nEq <= (int) p->key_def->part_count);
	assert(pBuilder->nRecValid == (nEq - 1));

	rc = sqlStat4ProbeSetValue(pParse, p, &pRec, pExpr, 1, nEq - 1,
				       &bOk);
	pBuilder->pRec = pRec;
	if (rc != 0)
		return rc;
	assert(bOk != 0);
	pBuilder->nRecValid = nEq;

	whereKeyStats(pParse, p, pRec, 0, a);
	WHERETRACE(0x10, ("equality scan regions %s(%d): %d\n", p->name,
		   nEq - 1, (int)a[1]));
	*pnRow = a[1];

	return rc;
}

/*
 * Estimate the number of rows that will be returned based on
 * an IN constraint where the right-hand side of the IN operator
 * is a list of values.  Example:
 *
 *        WHERE x IN (1,2,3,4)
 *
 * Write the estimated row count into *pnRow and return 0.
 * If unable to make an estimate, leave *pnRow unchanged and return
 * non-zero.
 *
 * This routine can fail if it is unable to load a collating sequence
 * required for string comparison, or if unable to allocate memory
 * for a UTF conversion required for comparison.  The error is stored
 * in the pParse structure.
 */
static int
whereInScanEst(Parse * pParse,	/* Parsing & code generating context */
	       WhereLoopBuilder * pBuilder, ExprList * pList,	/* The value list on the RHS of "x IN (v1,v2,v3,...)" */
	       tRowcnt * pnRow)	/* Write the revised row estimate here */
{
	struct index_def *p = pBuilder->pNew->index_def;
	i64 nRow0 = sqlLogEstToInt(index_field_tuple_est(p, 0));
	int nRecValid = pBuilder->nRecValid;
	int rc = 0;	/* Subfunction return code */
	tRowcnt nEst;		/* Number of rows for a single term */
	tRowcnt nRowEst = 0;	/* New estimate of the number of rows */
	int i;			/* Loop counter */

	for (i = 0; rc == 0 && i < pList->nExpr; i++) {
		nEst = nRow0;
		rc = whereEqualScanEst(pParse, pBuilder, pList->a[i].pExpr,
				       &nEst);
		nRowEst += nEst;
		pBuilder->nRecValid = nRecValid;
	}

	if (rc == 0) {
		if (nRowEst > nRow0)
			nRowEst = nRow0;
		*pnRow = nRowEst;
		WHERETRACE(0x10, ("IN row estimate: est=%d\n", nRowEst));
	}
	assert(pBuilder->nRecValid == nRecValid);
	return rc;
}

#ifdef SQL_DEBUG
/*
 * Print the content of a WhereTerm object
 */
static void
whereTermPrint(WhereTerm * pTerm, int iTerm)
{
	if (pTerm == 0) {
		sqlDebugPrintf("TERM-%-3d NULL\n", iTerm);
	} else {
		char zType[4];
		char zLeft[50];
		memcpy(zType, "...", 4);
		if (pTerm->wtFlags & TERM_VIRTUAL)
			zType[0] = 'V';
		if (pTerm->eOperator & WO_EQUIV)
			zType[1] = 'E';
		if (ExprHasProperty(pTerm->pExpr, EP_FromJoin))
			zType[2] = 'L';
		if (pTerm->eOperator & WO_SINGLE) {
			sql_snprintf(sizeof(zLeft), zLeft, "left={%d:%d}",
					 pTerm->leftCursor,
					 pTerm->u.leftColumn);
		} else if ((pTerm->eOperator & WO_OR) != 0
			   && pTerm->u.pOrInfo != 0) {
			sql_snprintf(sizeof(zLeft), zLeft,
					 "indexable=0x%lld",
					 pTerm->u.pOrInfo->indexable);
		} else {
			sql_snprintf(sizeof(zLeft), zLeft, "left=%d",
					 pTerm->leftCursor);
		}
		sqlDebugPrintf
		    ("TERM-%-3d %p %s %-12s prob=%-3d op=0x%03x wtFlags=0x%04x",
		     iTerm, pTerm, zType, zLeft, pTerm->truthProb,
		     pTerm->eOperator, pTerm->wtFlags);
		if (pTerm->iField) {
			sqlDebugPrintf(" iField=%d\n", pTerm->iField);
		} else {
			sqlDebugPrintf("\n");
		}
		sqlTreeViewExpr(0, pTerm->pExpr, 0);
	}
}

/*
 * Show the complete content of a WhereClause
 */
void
sqlWhereClausePrint(WhereClause * pWC)
{
	int i;
	for (i = 0; i < pWC->nTerm; i++) {
		whereTermPrint(&pWC->a[i], i);
	}
}

/*
 * Print a WhereLoop object for debugging purposes
 */
static void
whereLoopPrint(WhereLoop * p, WhereClause * pWC)
{
	WhereInfo *pWInfo = pWC->pWInfo;
	int nb = 1 + (pWInfo->pTabList->nSrc + 3) / 4;
	struct SrcList_item *pItem = pWInfo->pTabList->a + p->iTab;
	struct space_def *space_def = pItem->space->def;
	Bitmask mAll = (((Bitmask) 1) << (nb * 4)) - 1;
	sqlDebugPrintf("%c%2d.%0*llx.%0*llx", p->cId,
			   p->iTab, nb, p->maskSelf, nb, p->prereq & mAll);
	sqlDebugPrintf(" %12s",
			   pItem->zAlias ? pItem->zAlias : space_def->name);
	const char *zName;
	if (p->index_def != NULL && (zName = p->index_def->name) != NULL) {
		if (strncmp(zName, "sql_autoindex_", 17) == 0) {
			int i = sqlStrlen30(zName) - 1;
			while (zName[i] != '_')
				i--;
			zName += i;
		}
		sqlDebugPrintf(".%-16s %2d", zName, p->nEq);
	} else {
		sqlDebugPrintf("%20s", "");
	}
	if (p->wsFlags & WHERE_SKIPSCAN) {
		sqlDebugPrintf(" f %05x %d-%d", p->wsFlags, p->nLTerm,
				   p->nSkip);
	} else {
		sqlDebugPrintf(" f %05x N %d", p->wsFlags, p->nLTerm);
	}
	sqlDebugPrintf(" cost %d,%d,%d\n", p->rSetup, p->rRun, p->nOut);
	if (p->nLTerm && (sqlWhereTrace & 0x100) != 0) {
		int i;
		for (i = 0; i < p->nLTerm; i++) {
			whereTermPrint(p->aLTerm[i], i);
		}
	}
}
#endif

/*
 * Convert bulk memory into a valid WhereLoop that can be passed
 * to whereLoopClear harmlessly.
 */
static void
whereLoopInit(WhereLoop * p)
{
	p->aLTerm = p->aLTermSpace;
	p->nLTerm = 0;
	p->nLSlot = ArraySize(p->aLTermSpace);
	p->wsFlags = 0;
	p->index_def = NULL;
}

/*
 * Clear the WhereLoop.u union.  Leave WhereLoop.pLTerm intact.
 */
static void
whereLoopClearUnion(WhereLoop * p)
{
	if ((p->wsFlags & WHERE_AUTO_INDEX) != 0 && p->index_def != NULL) {
		index_def_delete(p->index_def);
		p->index_def = NULL;
	}
}

/*
 * Deallocate internal memory used by a WhereLoop object
 */
static void
whereLoopClear(sql * db, WhereLoop * p)
{
	if (p->aLTerm != p->aLTermSpace)
		sqlDbFree(db, p->aLTerm);
	whereLoopClearUnion(p);
	whereLoopInit(p);
}

/*
 * Increase the memory allocation for pLoop->aLTerm[] to be at least n.
 */
static int
whereLoopResize(sql * db, WhereLoop * p, int n)
{
	WhereTerm **paNew;
	if (p->nLSlot >= n)
		return 0;
	n = (n + 7) & ~7;
	paNew = sqlDbMallocRawNN(db, sizeof(p->aLTerm[0]) * n);
	if (paNew == 0)
		return -1;
	memcpy(paNew, p->aLTerm, sizeof(p->aLTerm[0]) * p->nLSlot);
	if (p->aLTerm != p->aLTermSpace)
		sqlDbFree(db, p->aLTerm);
	p->aLTerm = paNew;
	p->nLSlot = n;
	return 0;
}

/*
 * Transfer content from the second pLoop into the first.
 */
static int
whereLoopXfer(sql * db, WhereLoop * pTo, WhereLoop * pFrom)
{
	whereLoopClearUnion(pTo);
	if (whereLoopResize(db, pTo, pFrom->nLTerm)) {
		pTo->nEq = 0;
		pTo->nBtm = 0;
		pTo->nTop = 0;
		pTo->index_def = NULL;
		return -1;
	}
	memcpy(pTo, pFrom, WHERE_LOOP_XFER_SZ);
	memcpy(pTo->aLTerm, pFrom->aLTerm,
	       pTo->nLTerm * sizeof(pTo->aLTerm[0]));
	if ((pFrom->wsFlags & WHERE_AUTO_INDEX) != 0)
		pFrom->index_def = NULL;
	return 0;
}

/*
 * Delete a WhereLoop object
 */
static void
whereLoopDelete(sql * db, WhereLoop * p)
{
	whereLoopClear(db, p);
	sqlDbFree(db, p);
}

/*
 * Free a WhereInfo structure
 */
static void
whereInfoFree(sql * db, WhereInfo * pWInfo)
{
	if (ALWAYS(pWInfo)) {
		int i;
		for (i = 0; i < pWInfo->nLevel; i++) {
			WhereLevel *pLevel = &pWInfo->a[i];
			if (pLevel->pWLoop
			    && (pLevel->pWLoop->wsFlags & WHERE_IN_ABLE)) {
				sqlDbFree(db, pLevel->u.in.aInLoop);
			}
		}
		sqlWhereClauseClear(&pWInfo->sWC);
		while (pWInfo->pLoops) {
			WhereLoop *p = pWInfo->pLoops;
			pWInfo->pLoops = p->pNextLoop;
			whereLoopDelete(db, p);
		}
		sqlDbFree(db, pWInfo);
	}
}

/*
 * Return TRUE if all of the following are true:
 *
 *   (1)  X has the same or lower cost that Y
 *   (2)  X uses fewer WHERE clause terms than Y
 *   (3)  Every WHERE clause term used by X is also used by Y
 *   (4)  X skips at least as many columns as Y
 *   (5)  If X is a covering index, than Y is too
 *
 * Conditions (2) and (3) mean that X is a "proper subset" of Y.
 *
 * If X is a proper subset of Y then Y is a better choice and ought
 * to have a lower cost.  This routine returns TRUE when that cost
 * relationship is inverted and needs to be adjusted.  Constraint (4)
 * was added because if X uses skip-scan less than Y it still might
 * deserve a lower cost even if it is a proper subset of Y.
 */
static int
whereLoopCheaperProperSubset(const WhereLoop * pX,	/* First WhereLoop to compare */
			     const WhereLoop * pY)	/* Compare against this WhereLoop */
{
	int i, j;
	if (pX->nLTerm - pX->nSkip >= pY->nLTerm - pY->nSkip) {
		return 0;	/* X is not a subset of Y */
	}
	if (pY->nSkip > pX->nSkip)
		return 0;
	if (pX->rRun >= pY->rRun) {
		if (pX->rRun > pY->rRun)
			return 0;	/* X costs more than Y */
		if (pX->nOut > pY->nOut)
			return 0;	/* X costs more than Y */
	}
	for (i = pX->nLTerm - 1; i >= 0; i--) {
		if (pX->aLTerm[i] == 0)
			continue;
		for (j = pY->nLTerm - 1; j >= 0; j--) {
			if (pY->aLTerm[j] == pX->aLTerm[i])
				break;
		}
		if (j < 0)
			return 0;	/* X not a subset of Y since term X[i] not used by Y */
	}
  	if ((pX->wsFlags & WHERE_IDX_ONLY) != 0 
		&& (pY->wsFlags & WHERE_IDX_ONLY) == 0) {
    	return 0;  /* Constraint (5) */
  	}

	return 1;		/* All conditions meet */
}

/*
 * Try to adjust the cost of WhereLoop pTemplate upwards or downwards so
 * that:
 *
 *   (1) pTemplate costs less than any other WhereLoops that are a proper
 *       subset of pTemplate
 *
 *   (2) pTemplate costs more than any other WhereLoops for which pTemplate
 *       is a proper subset.
 *
 * To say "WhereLoop X is a proper subset of Y" means that X uses fewer
 * WHERE clause terms than Y and that every WHERE clause term used by X is
 * also used by Y.
 */
static void
whereLoopAdjustCost(const WhereLoop * p, WhereLoop * pTemplate)
{
	if ((pTemplate->wsFlags & WHERE_INDEXED) == 0)
		return;
	for (; p; p = p->pNextLoop) {
		if (p->iTab != pTemplate->iTab)
			continue;
		if ((p->wsFlags & WHERE_INDEXED) == 0)
			continue;
		if (whereLoopCheaperProperSubset(p, pTemplate)) {
			/* Adjust pTemplate cost downward so that it is cheaper than its
			 * subset p.
			 */
			WHERETRACE(0x80,
				   ("subset cost adjustment %d,%d to %d,%d\n",
				    pTemplate->rRun, pTemplate->nOut, p->rRun,
				    p->nOut - 1));
			pTemplate->rRun = p->rRun;
			pTemplate->nOut = p->nOut - 1;
		} else if (whereLoopCheaperProperSubset(pTemplate, p)) {
			/* Adjust pTemplate cost upward so that it is costlier than p since
			 * pTemplate is a proper subset of p
			 */
			WHERETRACE(0x80,
				   ("subset cost adjustment %d,%d to %d,%d\n",
				    pTemplate->rRun, pTemplate->nOut, p->rRun,
				    p->nOut + 1));
			pTemplate->rRun = p->rRun;
			pTemplate->nOut = p->nOut + 1;
		}
	}
}

/*
 * Search the list of WhereLoops in *ppPrev looking for one that can be
 * supplanted by pTemplate.
 *
 * Return NULL if the WhereLoop list contains an entry that can supplant
 * pTemplate, in other words if pTemplate does not belong on the list.
 *
 * If pX is a WhereLoop that pTemplate can supplant, then return the
 * link that points to pX.
 *
 * If pTemplate cannot supplant any existing element of the list but needs
 * to be added to the list, then return a pointer to the tail of the list.
 */
static WhereLoop **
whereLoopFindLesser(WhereLoop ** ppPrev, const WhereLoop * pTemplate)
{
	WhereLoop *p;
	for (p = (*ppPrev); p; ppPrev = &p->pNextLoop, p = *ppPrev) {
		if (p->iTab != pTemplate->iTab
		    || p->iSortIdx != pTemplate->iSortIdx) {
			/* If either the iTab or iSortIdx values for two WhereLoop are different
			 * then those WhereLoops need to be considered separately.  Neither is
			 * a candidate to replace the other.
			 */
			continue;
		}
		/* In the current implementation, the rSetup value is either zero
		 * or the cost of building an automatic index (NlogN) and the NlogN
		 * is the same for compatible WhereLoops.
		 */
		assert(p->rSetup == 0 || pTemplate->rSetup == 0
		       || p->rSetup == pTemplate->rSetup);

		/* whereLoopAddBtree() always generates and inserts the automatic index
		 * case first.  Hence compatible candidate WhereLoops never have a larger
		 * rSetup. Call this SETUP-INVARIANT
		 */
		assert(p->rSetup >= pTemplate->rSetup);

		/* Any loop using an appliation-defined index (or PRIMARY KEY or
		 * UNIQUE constraint) with one or more == constraints is better
		 * than an automatic index. Unless it is a skip-scan.
		 */
		if ((p->wsFlags & WHERE_AUTO_INDEX) != 0
		    && (pTemplate->nSkip) == 0
		    && (pTemplate->wsFlags & WHERE_INDEXED) != 0
		    && (pTemplate->wsFlags & WHERE_COLUMN_EQ) != 0
		    && (p->prereq & pTemplate->prereq) == pTemplate->prereq) {
			break;
		}

		/* If existing WhereLoop p is better than pTemplate, pTemplate can be
		 * discarded.  WhereLoop p is better if:
		 *   (1)  p has no more dependencies than pTemplate, and
		 *   (2)  p has an equal or lower cost than pTemplate
		 */
		if ((p->prereq & pTemplate->prereq) == p->prereq	/* (1)  */
		    && p->rSetup <= pTemplate->rSetup	/* (2a) */
		    && p->rRun <= pTemplate->rRun	/* (2b) */
		    && p->nOut <= pTemplate->nOut	/* (2c) */
		    ) {
			return 0;	/* Discard pTemplate */
		}

		/* If pTemplate is always better than p, then cause p to be overwritten
		 * with pTemplate.  pTemplate is better than p if:
		 *   (1)  pTemplate has no more dependences than p, and
		 *   (2)  pTemplate has an equal or lower cost than p.
		 */
		if ((p->prereq & pTemplate->prereq) == pTemplate->prereq	/* (1)  */
		    && p->rRun >= pTemplate->rRun	/* (2a) */
		    && p->nOut >= pTemplate->nOut	/* (2b) */
		    ) {
			assert(p->rSetup >= pTemplate->rSetup);	/* SETUP-INVARIANT above */
			break;	/* Cause p to be overwritten by pTemplate */
		}
	}
	return ppPrev;
}

/*
 * Insert or replace a WhereLoop entry using the template supplied.
 *
 * An existing WhereLoop entry might be overwritten if the new template
 * is better and has fewer dependencies.  Or the template will be ignored
 * and no insert will occur if an existing WhereLoop is faster and has
 * fewer dependencies than the template.  Otherwise a new WhereLoop is
 * added based on the template.
 *
 * If pBuilder->pOrSet is not NULL then we care about only the
 * prerequisites and rRun and nOut costs of the N best loops.  That
 * information is gathered in the pBuilder->pOrSet object.  This special
 * processing mode is used only for OR clause processing.
 *
 * When accumulating multiple loops (when pBuilder->pOrSet is NULL) we
 * still might overwrite similar loops with the new template if the
 * new template is better.  Loops may be overwritten if the following
 * conditions are met:
 *
 *    (1)  They have the same iTab.
 *    (2)  They have the same iSortIdx.
 *    (3)  The template has same or fewer dependencies than the current loop
 *    (4)  The template has the same or lower cost than the current loop
 */
static int
whereLoopInsert(WhereLoopBuilder * pBuilder, WhereLoop * pTemplate)
{
	WhereLoop **ppPrev, *p;
	WhereInfo *pWInfo = pBuilder->pWInfo;
	sql *db = pWInfo->pParse->db;
	int rc;

	/* If pBuilder->pOrSet is defined, then only keep track of the costs
	 * and prereqs.
	 */
	if (pBuilder->pOrSet != 0) {
		if (pTemplate->nLTerm) {
#ifdef SQL_DEBUG
			u16 n = pBuilder->pOrSet->n;
			int x =
#endif
			    whereOrInsert(pBuilder->pOrSet, pTemplate->prereq,
					  pTemplate->rRun,
					  pTemplate->nOut);
#ifdef SQL_DEBUG		/* 0x8 */
			if (sqlWhereTrace & 0x8) {
				sqlDebugPrintf(x ? "   or-%d:  " :
						   "   or-X:  ", n);
				whereLoopPrint(pTemplate, pBuilder->pWC);
			}
#endif
		}
		return 0;
	}

	/* Look for an existing WhereLoop to replace with pTemplate
	 */
	whereLoopAdjustCost(pWInfo->pLoops, pTemplate);
	ppPrev = whereLoopFindLesser(&pWInfo->pLoops, pTemplate);

	if (ppPrev == 0) {
		/* There already exists a WhereLoop on the list that is better
		 * than pTemplate, so just ignore pTemplate
		 */
#ifdef SQL_DEBUG		/* 0x8 */
		if (sqlWhereTrace & 0x8) {
			sqlDebugPrintf("   skip: ");
			whereLoopPrint(pTemplate, pBuilder->pWC);
		}
#endif
		return 0;
	} else {
		p = *ppPrev;
	}

	/* If we reach this point it means that either p[] should be overwritten
	 * with pTemplate[] if p[] exists, or if p==NULL then allocate a new
	 * WhereLoop and insert it.
	 */
#ifdef SQL_DEBUG		/* 0x8 */
	if (sqlWhereTrace & 0x8) {
		if (p != 0) {
			sqlDebugPrintf("replace: ");
			whereLoopPrint(p, pBuilder->pWC);
		}
		sqlDebugPrintf("    add: ");
		whereLoopPrint(pTemplate, pBuilder->pWC);
	}
#endif
	if (p == 0) {
		/* Allocate a new WhereLoop to add to the end of the list */
		*ppPrev = p = sqlDbMallocRawNN(db, sizeof(WhereLoop));
		if (p == 0)
			return -1;
		whereLoopInit(p);
		p->pNextLoop = 0;
	} else {
		/* We will be overwriting WhereLoop p[].  But before we do, first
		 * go through the rest of the list and delete any other entries besides
		 * p[] that are also supplated by pTemplate
		 */
		WhereLoop **ppTail = &p->pNextLoop;
		WhereLoop *pToDel;
		while (*ppTail) {
			ppTail = whereLoopFindLesser(ppTail, pTemplate);
			if (ppTail == 0)
				break;
			pToDel = *ppTail;
			if (pToDel == 0)
				break;
			*ppTail = pToDel->pNextLoop;
#ifdef SQL_DEBUG		/* 0x8 */
			if (sqlWhereTrace & 0x8) {
				sqlDebugPrintf(" delete: ");
				whereLoopPrint(pToDel, pBuilder->pWC);
			}
#endif
			whereLoopDelete(db, pToDel);
		}
	}
	rc = whereLoopXfer(db, p, pTemplate);
	struct index_def *idx = p->index_def;
	if (idx != NULL && idx->space_id == 0)
		p->index_def = NULL;
	return rc;
}

/*
 * Adjust the WhereLoop.nOut value downward to account for terms of the
 * WHERE clause that reference the loop but which are not used by an
 * index.
*
 * For every WHERE clause term that is not used by the index
 * and which has a truth probability assigned by one of the likelihood(),
 * likely(), or unlikely() SQL functions, reduce the estimated number
 * of output rows by the probability specified.
 *
 * TUNING:  For every WHERE clause term that is not used by the index
 * and which does not have an assigned truth probability, heuristics
 * described below are used to try to estimate the truth probability.
 * TODO --> Perhaps this is something that could be improved by better
 * table statistics.
 *
 * Heuristic 1:  Estimate the truth probability as 93.75%.  The 93.75%
 * value corresponds to -1 in LogEst notation, so this means decrement
 * the WhereLoop.nOut field for every such WHERE clause term.
 *
 * Heuristic 2:  If there exists one or more WHERE clause terms of the
 * form "x==EXPR" and EXPR is not a constant 0 or 1, then make sure the
 * final output row estimate is no greater than 1/4 of the total number
 * of rows in the table.  In other words, assume that x==EXPR will filter
 * out at least 3 out of 4 rows.  If EXPR is -1 or 0 or 1, then maybe the
 * "x" column is boolean or else -1 or 0 or 1 is a common default value
 * on the "x" column and so in that case only cap the output row estimate
 * at 1/2 instead of 1/4.
 */
static void
whereLoopOutputAdjust(WhereClause * pWC,	/* The WHERE clause */
		      WhereLoop * pLoop,	/* The loop to adjust downward */
		      LogEst nRow)		/* Number of rows in the entire table */
{
	WhereTerm *pTerm, *pX;
	Bitmask notAllowed = ~(pLoop->prereq | pLoop->maskSelf);
	int i, j, k;
	LogEst iReduce = 0;	/* pLoop->nOut should not exceed nRow-iReduce */

	assert((pLoop->wsFlags & WHERE_AUTO_INDEX) == 0);
	for (i = pWC->nTerm, pTerm = pWC->a; i > 0; i--, pTerm++) {
		if ((pTerm->wtFlags & TERM_VIRTUAL) != 0)
			break;
		if ((pTerm->prereqAll & pLoop->maskSelf) == 0)
			continue;
		if ((pTerm->prereqAll & notAllowed) != 0)
			continue;
		for (j = pLoop->nLTerm - 1; j >= 0; j--) {
			pX = pLoop->aLTerm[j];
			if (pX == 0)
				continue;
			if (pX == pTerm)
				break;
			if (pX->iParent >= 0 && (&pWC->a[pX->iParent]) == pTerm)
				break;
		}
		if (j < 0) {
			if (pTerm->truthProb <= 0) {
				/* If a truth probability is specified using the likelihood() hints,
				 * then use the probability provided by the application.
				 */
				pLoop->nOut += pTerm->truthProb;
			} else {
				/* In the absence of explicit truth probabilities, use heuristics to
				 * guess a reasonable truth probability.
				 */
				pLoop->nOut--;
				if ((pTerm->eOperator & WO_EQ) != 0) {
					Expr *pRight = pTerm->pExpr->pRight;
					if (sqlExprIsInteger(pRight, &k)
					    && k >= (-1) && k <= 1) {
						k = 10;
					} else {
						k = 20;
					}
					if (iReduce < k)
						iReduce = k;
				}
			}
		}
	}
	if (pLoop->nOut > nRow - iReduce)
		pLoop->nOut = nRow - iReduce;
}

/*
 * Term pTerm is a vector range comparison operation. The first comparison
 * in the vector can be optimized using column nEq of the index. This
 * function returns the total number of vector elements that can be used
 * as part of the range comparison.
 *
 * For example, if the query is:
 *
 *   WHERE a = ? AND (b, c, d) > (?, ?, ?)
 *
 * and the index:
 *
 *   CREATE INDEX ... ON (a, b, c, d, e)
 *
 * then this function would be invoked with nEq=1. The value returned in
 * this case is 3.
 */
static int
whereRangeVectorLen(Parse * pParse,	/* Parsing context */
		    int iCur,		/* Cursor open on pIdx */
		    struct index_def *idx_def,
		    int nEq,		/* Number of prior equality constraints on same index */
		    WhereTerm * pTerm)	/* The vector inequality constraint */
{
	int nCmp = sqlExprVectorSize(pTerm->pExpr->pLeft);
	int i;
	struct space *space = space_by_id(idx_def->space_id);
	assert(space != NULL);
	nCmp = MIN(nCmp, (int)(idx_def->key_def->part_count - nEq));
	for (i = 1; i < nCmp; i++) {
		/* Test if comparison i of pTerm is compatible with column (i+nEq)
		 * of the index. If not, exit the loop.
		 */
		Expr *pLhs = pTerm->pExpr->pLeft->x.pList->a[i].pExpr;
		Expr *pRhs = pTerm->pExpr->pRight;
		if (pRhs->flags & EP_xIsSelect) {
			pRhs = pRhs->x.pSelect->pEList->a[i].pExpr;
		} else {
			pRhs = pRhs->x.pList->a[i].pExpr;
		}

		/* Check that the LHS of the comparison is a column reference to
		 * the right column of the right source table. And that the sort
		 * order of the index column is the same as the sort order of the
		 * leftmost index column.
		 */
		struct key_part *parts = idx_def->key_def->parts;
		if (pLhs->op != TK_COLUMN || pLhs->iTable != iCur ||
		    pLhs->iColumn != (int)parts[i + nEq].fieldno ||
		    parts[i + nEq].sort_order != parts[nEq].sort_order)
			break;
		enum field_type rhs_type = sql_expr_type(pRhs);
		enum field_type type =
			sql_type_result(rhs_type, sql_expr_type(pLhs));
		enum field_type idx_type = pLhs->iColumn >= 0 ?
			space->def->fields[pLhs->iColumn].type : FIELD_TYPE_INTEGER;
		if (type != idx_type)
			break;
		uint32_t id;
		if (sql_binary_compare_coll_seq(pParse, pLhs, pRhs, &id) != 0)
			break;
		if (id == COLL_NONE)
			break;
		if (idx_def->key_def->parts[i + nEq].coll_id != id)
			break;
	}
	return i;
}

/*
 * We have so far matched pBuilder->pNew->nEq terms of the
 * index pIndex. Try to match one more.
 *
 * When this function is called, pBuilder->pNew->nOut contains the
 * number of rows expected to be visited by filtering using the
 * nEq terms only. If it is modified, this value is restored before
 * this function returns.
 *
 * If pProbe->def->space_id==UIN32_MAX, that means pIndex is a
 * fake index used for the INTEGER PRIMARY KEY.
 */
static int
whereLoopAddBtreeIndex(WhereLoopBuilder * pBuilder,	/* The WhereLoop factory */
		       struct SrcList_item *pSrc,	/* FROM clause term being analyzed */
		       struct index_def *probe,
		       LogEst nInMul)			/* log(Number of iterations due to IN) */
{
	WhereInfo *pWInfo = pBuilder->pWInfo;	/* WHERE analyse context */
	Parse *pParse = pWInfo->pParse;	/* Parsing context */
	sql *db = pParse->db;	/* Database connection malloc context */
	WhereLoop *pNew;	/* Template WhereLoop under construction */
	WhereTerm *pTerm;	/* A WhereTerm under consideration */
	int opMask;		/* Valid operators for constraints */
	WhereScan scan;		/* Iterator for WHERE terms */
	Bitmask saved_prereq;	/* Original value of pNew->prereq */
	u16 saved_nLTerm;	/* Original value of pNew->nLTerm */
	u16 saved_nEq;		/* Original value of pNew->nEq */
	u16 saved_nBtm;		/* Original value of pNew->nBtm */
	u16 saved_nTop;		/* Original value of pNew->nTop */
	u16 saved_nSkip;	/* Original value of pNew->nSkip */
	u32 saved_wsFlags;	/* Original value of pNew->wsFlags */
	LogEst saved_nOut;	/* Original value of pNew->nOut */
	int rc = 0;	/* Return code */
	LogEst rSize;		/* Number of rows in the table */
	LogEst rLogSize;	/* Logarithm of table size */
	WhereTerm *pTop = 0, *pBtm = 0;	/* Top and bottom range constraints */
	uint32_t probe_part_count = probe->key_def->part_count;

	pNew = pBuilder->pNew;
	if (db->mallocFailed)
		return -1;
	WHERETRACE(0x800, ("BEGIN addBtreeIdx(%s), nEq=%d\n",
			   probe->name, pNew->nEq));

	assert((pNew->wsFlags & WHERE_TOP_LIMIT) == 0);
	if (pNew->wsFlags & WHERE_BTM_LIMIT) {
		opMask = WO_LT | WO_LE;
	} else {
		assert(pNew->nBtm == 0);
		opMask =
		    WO_EQ | WO_IN | WO_GT | WO_GE | WO_LT | WO_LE | WO_ISNULL;
	}
	struct space *space = space_by_id(probe->space_id);
	struct index_stat *stat = NULL;
	if (space != NULL && probe->iid != UINT32_MAX) {
		struct index *idx = space_index(space, probe->iid);
		assert(idx != NULL);
		stat = idx->def->opts.stat;
	}
	/*
	 * Create surrogate stat in case ANALYZE command hasn't
	 * been ran. Simply fill it with zeros.
	 */
	struct index_stat surrogate_stat;
	memset(&surrogate_stat, 0, sizeof(surrogate_stat));
	if (stat == NULL)
		stat = &surrogate_stat;
	if (stat->is_unordered)
		opMask &= ~(WO_GT | WO_GE | WO_LT | WO_LE);
	assert(pNew->nEq < probe_part_count);

	saved_nEq = pNew->nEq;
	saved_nBtm = pNew->nBtm;
	saved_nTop = pNew->nTop;
	saved_nSkip = pNew->nSkip;
	saved_nLTerm = pNew->nLTerm;
	saved_wsFlags = pNew->wsFlags;
	saved_prereq = pNew->prereq;
	saved_nOut = pNew->nOut;
	pTerm = whereScanInit(&scan, pBuilder->pWC, pSrc->iCursor, saved_nEq,
			      opMask, probe);
	pNew->rSetup = 0;
	rSize = index_field_tuple_est(probe, 0);
	rLogSize = estLog(rSize);
	for (; rc == 0 && pTerm != NULL; pTerm = whereScanNext(&scan)) {
		u16 eOp = pTerm->eOperator;	/* Shorthand for pTerm->eOperator */
		LogEst rCostIdx;
		LogEst nOutUnadjusted;	/* nOut before IN() and WHERE adjustments */
		int nIn = 0;
		int nRecValid = pBuilder->nRecValid;
		uint32_t j = probe->key_def->parts[saved_nEq].fieldno;
		if ((eOp == WO_ISNULL || (pTerm->wtFlags & TERM_VNULL) != 0) &&
		    !space->def->fields[j].is_nullable) {
			/*
			 * Ignore IS [NOT] NULL constraints on NOT
			 * NULL columns.
			 */
			continue;
		}
		if (pTerm->prereqRight & pNew->maskSelf)
			continue;

		/* Do not allow the upper bound of a LIKE optimization range constraint
		 * to mix with a lower range bound from some other source
		 */
		if (pTerm->wtFlags & TERM_LIKEOPT && pTerm->eOperator == WO_LT)
			continue;

		/* Do not allow IS constraints from the WHERE clause to be used by the
		 * right table of a LEFT JOIN.  Only constraints in the ON clause are
		 * allowed
		 */
		if ((pSrc->fg.jointype & JT_LEFT) != 0
		    && !ExprHasProperty(pTerm->pExpr, EP_FromJoin)
		    && (eOp & WO_ISNULL) != 0) {
			testcase(eOp & WO_ISNULL);
			continue;
		}

		pNew->wsFlags = saved_wsFlags;
		pNew->nEq = saved_nEq;
		pNew->nBtm = saved_nBtm;
		pNew->nTop = saved_nTop;
		pNew->nLTerm = saved_nLTerm;
		if (whereLoopResize(db, pNew, pNew->nLTerm + 1))
			break;	/* OOM */
		pNew->aLTerm[pNew->nLTerm++] = pTerm;
		pNew->prereq =
		    (saved_prereq | pTerm->prereqRight) & ~pNew->maskSelf;

		assert(nInMul == 0
		       || (pNew->wsFlags & WHERE_COLUMN_NULL) != 0
		       || (pNew->wsFlags & WHERE_COLUMN_IN) != 0
		       || (pNew->wsFlags & WHERE_SKIPSCAN) != 0);

		if (eOp & WO_IN) {
			Expr *pExpr = pTerm->pExpr;
			pNew->wsFlags |= WHERE_COLUMN_IN;
			if (ExprHasProperty(pExpr, EP_xIsSelect)) {
				/* "x IN (SELECT ...)":  TUNING: the SELECT returns 25 rows */
				int i;
				nIn = 46;
				assert(46 == sqlLogEst(25));

				/* The expression may actually be of the form (x, y) IN (SELECT...).
				 * In this case there is a separate term for each of (x) and (y).
				 * However, the nIn multiplier should only be applied once, not once
				 * for each such term. The following loop checks that pTerm is the
				 * first such term in use, and sets nIn back to 0 if it is not.
				 */
				for (i = 0; i < pNew->nLTerm - 1; i++) {
					if (pNew->aLTerm[i]
					    && pNew->aLTerm[i]->pExpr == pExpr)
						nIn = 0;
				}
			} else
			    if (ALWAYS(pExpr->x.pList && pExpr->x.pList->nExpr))
			{
				/* "x IN (value, value, ...)" */
				nIn = sqlLogEst(pExpr->x.pList->nExpr);
				assert(nIn > 0);	/* RHS always has 2 or more terms...  The parser
							 * changes "x IN (?)" into "x=?".
							 */
			}
		} else if (eOp & WO_EQ) {
			int iCol = probe->key_def->parts[saved_nEq].fieldno;
			pNew->wsFlags |= WHERE_COLUMN_EQ;
			assert(saved_nEq == pNew->nEq);
			if (iCol > 0 && nInMul == 0 &&
			    saved_nEq == probe_part_count - 1) {
				bool index_is_unique_not_null =
					probe->key_def->is_nullable &&
					probe->opts.is_unique;
				if (probe->space_id != 0 &&
				    !index_is_unique_not_null) {
					pNew->wsFlags |= WHERE_UNQ_WANTED;
				} else {
					pNew->wsFlags |= WHERE_ONEROW;
				}
			}
		} else if (eOp & WO_ISNULL) {
			pNew->wsFlags |= WHERE_COLUMN_NULL;
		} else if (eOp & (WO_GT | WO_GE)) {
			testcase(eOp & WO_GT);
			testcase(eOp & WO_GE);
			pNew->wsFlags |= WHERE_COLUMN_RANGE | WHERE_BTM_LIMIT;
			pNew->nBtm =
			    whereRangeVectorLen(pParse, pSrc->iCursor, probe,
						saved_nEq, pTerm);
			pBtm = pTerm;
			pTop = 0;
			if (pTerm->wtFlags & TERM_LIKEOPT) {
				/* Range contraints that come from the LIKE optimization are
				 * always used in pairs.
				 */
				pTop = &pTerm[1];
				assert((pTop - (pTerm->pWC->a)) <
				       pTerm->pWC->nTerm);
				assert(pTop->wtFlags & TERM_LIKEOPT);
				assert(pTop->eOperator == WO_LT);
				if (whereLoopResize(db, pNew, pNew->nLTerm + 1))
					break;	/* OOM */
				pNew->aLTerm[pNew->nLTerm++] = pTop;
				pNew->wsFlags |= WHERE_TOP_LIMIT;
				pNew->nTop = 1;
			}
		} else {
			assert(eOp & (WO_LT | WO_LE));
			testcase(eOp & WO_LT);
			testcase(eOp & WO_LE);
			pNew->wsFlags |= WHERE_COLUMN_RANGE | WHERE_TOP_LIMIT;
			pNew->nTop =
			    whereRangeVectorLen(pParse, pSrc->iCursor, probe,
						saved_nEq, pTerm);
			pTop = pTerm;
			pBtm = (pNew->wsFlags & WHERE_BTM_LIMIT) != 0 ?
			    pNew->aLTerm[pNew->nLTerm - 2] : 0;
		}

		/* At this point pNew->nOut is set to the number of rows expected to
		 * be visited by the index scan before considering term pTerm, or the
		 * values of nIn and nInMul. In other words, assuming that all
		 * "x IN(...)" terms are replaced with "x = ?". This block updates
		 * the value of pNew->nOut to account for pTerm (but not nIn/nInMul).
		 */
		assert(pNew->nOut == saved_nOut);
		if (pNew->wsFlags & WHERE_COLUMN_RANGE) {
			/* Adjust nOut using stat4 data. Or, if there is no stat4
			 * data, using some other estimate.
			 */
			whereRangeScanEst(pParse, pBuilder, pBtm, pTop, pNew);
		} else {
			int nEq = ++pNew->nEq;
			assert(eOp & (WO_ISNULL | WO_EQ | WO_IN));

			assert(pNew->nOut == saved_nOut);
			if (pTerm->truthProb <= 0 && probe->space_id != 0) {
				assert((eOp & WO_IN) || nIn == 0);
				testcase(eOp & WO_IN);
				pNew->nOut += pTerm->truthProb;
				pNew->nOut -= nIn;
			} else {
				tRowcnt nOut = 0;
				if (nInMul == 0
				    && stat->sample_count
				    && pNew->nEq <= stat->sample_field_count
				    && ((eOp & WO_IN) == 0
					|| !ExprHasProperty(pTerm->pExpr,
							    EP_xIsSelect))
				    ) {
					Expr *pExpr = pTerm->pExpr;
					if ((eOp & (WO_EQ | WO_ISNULL))
					    != 0) {
						testcase(eOp & WO_EQ);
						testcase(eOp & WO_ISNULL);
						rc = whereEqualScanEst(pParse,
								       pBuilder,
								       pExpr->pRight,
								       &nOut);
					} else {
						rc = whereInScanEst(pParse,
								    pBuilder,
								    pExpr->x.pList,
								    &nOut);
					}
					if (rc != 0)
						break;	/* Jump out of the pTerm loop */
					if (nOut) {
						pNew->nOut =
						    sqlLogEst(nOut);
						if (pNew->nOut > saved_nOut)
							pNew->nOut = saved_nOut;
						pNew->nOut -= nIn;
					}
				}
				if (nOut == 0) {
					pNew->nOut +=
						(index_field_tuple_est(probe, nEq) -
						 index_field_tuple_est(probe, nEq -1));
					if (eOp & WO_ISNULL) {
						/* TUNING: If there is no likelihood() value, assume that a
						 * "col IS NULL" expression matches twice as many rows
						 * as (col=?).
						 */
						pNew->nOut += 10;
					}
				}
			}
		}

		/* Set rCostIdx to the cost of visiting selected rows in index. Add
		 * it to pNew->rRun, which is currently set to the cost of the index
		 * seek only. Then, if this is a non-covering index, add the cost of
		 * visiting the rows in the main table.
		 */
		struct index *idx = space_index(space, probe->iid);
		assert(idx != NULL);
		/*
		 * FIXME: currently, the procedure below makes no
		 * sense, since there are no partial indexes, so
		 * all indexes in the space feature the same
		 * average tuple size. Moreover, secondary
		 * indexes in Vinyl engine may contain different
		 * tuple count of different sizes.
		 */
		ssize_t avg_tuple_size = sql_index_tuple_size(space, idx);
		struct index *pk = space_index(space, 0);
		ssize_t avg_tuple_size_pk = sql_index_tuple_size(space, pk);
		uint32_t partial_index_cost =
			avg_tuple_size_pk != 0 ?
			(15 * avg_tuple_size) / avg_tuple_size_pk : 0;
		rCostIdx = pNew->nOut + 1 + partial_index_cost;
		pNew->rRun = sqlLogEstAdd(rLogSize, rCostIdx);
		if ((pNew->wsFlags & (WHERE_IDX_ONLY | WHERE_IPK)) == 0) {
			pNew->rRun =
			    sqlLogEstAdd(pNew->rRun, pNew->nOut + 16);
		}

		nOutUnadjusted = pNew->nOut;
		pNew->rRun += nInMul + nIn;
		pNew->nOut += nInMul + nIn;
		whereLoopOutputAdjust(pBuilder->pWC, pNew, rSize);
		rc = whereLoopInsert(pBuilder, pNew);

		if (pNew->wsFlags & WHERE_COLUMN_RANGE) {
			pNew->nOut = saved_nOut;
		} else {
			pNew->nOut = nOutUnadjusted;
		}

		if ((pNew->wsFlags & WHERE_TOP_LIMIT) == 0 &&
		    pNew->nEq < probe_part_count) {
			whereLoopAddBtreeIndex(pBuilder, pSrc, probe,
					       nInMul + nIn);
		}
		pNew->nOut = saved_nOut;
		pBuilder->nRecValid = nRecValid;
	}
	pNew->prereq = saved_prereq;
	pNew->nEq = saved_nEq;
	pNew->nBtm = saved_nBtm;
	pNew->nTop = saved_nTop;
	pNew->nSkip = saved_nSkip;
	pNew->wsFlags = saved_wsFlags;
	pNew->nOut = saved_nOut;
	pNew->nLTerm = saved_nLTerm;

	/* Consider using a skip-scan if there are no WHERE clause constraints
	 * available for the left-most terms of the index, and if the average
	 * number of repeats in the left-most terms is at least 18.
	 *
	 * The magic number 18 is selected on the basis that scanning 17 rows
	 * is almost always quicker than an index seek (even though if the index
	 * contains fewer than 2^17 rows we assume otherwise in other parts of
	 * the code). And, even if it is not, it should not be too much slower.
	 * On the other hand, the extra seeks could end up being significantly
	 * more expensive.
	 */
	assert(42 == sqlLogEst(18));
	if (saved_nEq == saved_nSkip && saved_nEq + 1U < probe_part_count &&
	    stat->skip_scan_enabled == true &&
	    /* TUNING: Minimum for skip-scan */
	    index_field_tuple_est(probe, saved_nEq + 1) >= 42 &&
	    (rc = whereLoopResize(db, pNew, pNew->nLTerm + 1)) == 0) {
		LogEst nIter;
		pNew->nEq++;
		pNew->nSkip++;
		pNew->aLTerm[pNew->nLTerm++] = 0;
		pNew->wsFlags |= WHERE_SKIPSCAN;
		nIter = index_field_tuple_est(probe, saved_nEq) -
			index_field_tuple_est(probe, saved_nEq + 1);
		pNew->nOut -= nIter;
		/* TUNING:  Because uncertainties in the estimates for skip-scan queries,
		 * add a 1.375 fudge factor to make skip-scan slightly less likely.
		 */
		nIter += 5;
		whereLoopAddBtreeIndex(pBuilder, pSrc, probe, nIter + nInMul);
		pNew->nOut = saved_nOut;
		pNew->nEq = saved_nEq;
		pNew->nSkip = saved_nSkip;
		pNew->wsFlags = saved_wsFlags;
	}

	WHERETRACE(0x800, ("END addBtreeIdx(%s), nEq=%d, rc=%d\n",
			   probe->name, saved_nEq, rc));
	return rc;
}

/*
 * Return True if it is possible that pIndex might be useful in
 * implementing the ORDER BY clause in pBuilder.
 *
 * Return False if pBuilder does not contain an ORDER BY clause or
 * if there is no way for pIndex to be useful in implementing that
 * ORDER BY clause.
 */
static int
indexMightHelpWithOrderBy(WhereLoopBuilder * pBuilder,
			  const struct index_def *idx_def, int iCursor)
{
	ExprList *pOB;
	int ii, jj;
	int part_count = idx_def->key_def->part_count;
	if (idx_def->opts.stat != NULL && idx_def->opts.stat->is_unordered)
		return 0;
	if ((pOB = pBuilder->pWInfo->pOrderBy) == 0)
		return 0;
	for (ii = 0; ii < pOB->nExpr; ii++) {
		Expr *pExpr = sqlExprSkipCollate(pOB->a[ii].pExpr);
		if (pExpr->op == TK_COLUMN && pExpr->iTable == iCursor) {
			if (pExpr->iColumn < 0)
				return 1;
			for (jj = 0; jj < part_count; jj++) {
				if (pExpr->iColumn == (int)
				    idx_def->key_def->parts[jj].fieldno)
					return 1;
			}
		}
	}
	return 0;
}

/*
 * Add all WhereLoop objects for a single table of the join where the table
 * is identified by pBuilder->pNew->iTab.
 *
 * The costs (WhereLoop.rRun) of the b-tree loops added by this function
 * are calculated as follows:
 * rRun = log2(cost) * 10
 *
 * For a full scan, assuming the table (or index) contains nRow rows:
 *
 *     cost = nRow * 3.0                          // full-table scan
 *     cost = nRow * K -> 4.0 for Tarantool       // scan of covering index
 *     cost = nRow * (K+3.0) -> 4.0 for Tarantool // scan of non-covering index
 *
 * This formula forces usage of pk for full-table scan for Tarantool
 *
 * where K is a value between 1.1 and 3.0 set based on the relative
 * estimated average size of the index and table records.
 *
 * For an index scan, where nVisit is the number of index rows visited
 * by the scan, and nSeek is the number of seek operations required on
 * the index b-tree:
 *
 *     cost = nSeek * (log(nRow) + K * nVisit)          // covering index
 *     cost = nSeek * (log(nRow) + (K+3.0) * nVisit)    // non-covering index
 *
 * Normally, nSeek is 1. nSeek values greater than 1 come about if the
 * WHERE clause includes "x IN (....)" terms used in place of "x=?". Or when
 * implicit "x IN (SELECT x FROM tbl)" terms are added for skip-scans.
 *
 * The estimated values (nRow, nVisit, nSeek) often contain a large amount
 * of uncertainty.  For this reason, scoring is designed to pick plans that
 * "do the least harm" if the estimates are inaccurate.  For example, a
 * log(nRow) factor is omitted from a non-covering index scan in order to
 * bias the scoring in favor of using an index, since the worst-case
 * performance of using an index is far better than the worst-case performance
 * of a full table scan.
 */
static int
whereLoopAddBtree(WhereLoopBuilder * pBuilder,	/* WHERE clause information */
		  Bitmask mPrereq)		/* Extra prerequesites for using this table */
{
	WhereInfo *pWInfo;	/* WHERE analysis context */
	/* An index we are evaluating. */
	struct index_def *probe;
	/* A fake index object for the primary key. */
	struct index_def *fake_index = NULL;
	SrcList *pTabList;	/* The FROM clause */
	struct SrcList_item *pSrc;	/* The FROM clause btree term to add */
	WhereLoop *pNew;	/* Template WhereLoop object */
	int rc = 0;	/* Return code */
	int iSortIdx = 1;	/* Index number */
	int b;			/* A boolean value */
	LogEst rSize;		/* number of rows in the table */
	WhereClause *pWC;	/* The parsed WHERE clause */

	pNew = pBuilder->pNew;
	pWInfo = pBuilder->pWInfo;
	pTabList = pWInfo->pTabList;
	pSrc = pTabList->a + pNew->iTab;
	pWC = pBuilder->pWC;

	struct space *space = pSrc->space;
	if (pSrc->pIBIndex) {
		/* An INDEXED BY clause specifies a particular index to use */
		probe = pSrc->pIBIndex;
	} else if (space->index_count != 0) {
		probe = space->index[0]->def;
	} else {
		/* There is no INDEXED BY clause.  Create a fake Index object in local
		 * variable fake_index to represent the primary key index.  Make this
		 * fake index the first in a chain of Index objects with all of the real
		 * indices to follow
		 */
		memset(&fake_index, 0, sizeof(fake_index));

		struct key_part_def part;
		part.fieldno = 0;
		part.type = space->def->fields[0].type;
		part.nullable_action = ON_CONFLICT_ACTION_ABORT;
		part.is_nullable = false;
		part.sort_order = SORT_ORDER_ASC;
		part.coll_id = COLL_NONE;
		part.path = NULL;

		struct key_def *key_def = key_def_new(&part, 1, false);
		if (key_def == NULL) {
tnt_error:
			pWInfo->pParse->is_aborted = true;
			return -1;
		}

		struct index_opts opts;
		index_opts_create(&opts);
		fake_index = index_def_new(space->def->id, 0,"fake_autoindex",
					   sizeof("fake_autoindex") - 1,
					   TREE, &opts, key_def, NULL);
		key_def_delete(key_def);
		if (fake_index == NULL)
			goto tnt_error;
		/* Special marker for  non-existent index. */
		fake_index->iid = UINT32_MAX;
		int size = sizeof(struct index_stat) + sizeof(log_est_t) * 2;

		struct index_stat *stat = (struct index_stat *) calloc(1, size);
		if (stat == NULL) {
			diag_set(OutOfMemory, size, "calloc", "stat");
			goto tnt_error;
		}
		stat->tuple_log_est = (log_est_t *) ((char *) (stat + 1));
		stat->tuple_log_est[0] = sql_space_tuple_log_count(pSrc->space);
		stat->tuple_log_est[1] = 0;
		fake_index->opts.stat = stat;

		probe = fake_index;
	}

	/* Automatic indexes */
	rSize = DEFAULT_TUPLE_LOG_COUNT;
	/*
	 * Increase cost of ephemeral index if number of tuples in space is less
	 * then 10240.
	 */
	if (!space->def->opts.is_view && sql_space_tuple_log_count(space) < 133)
		rSize += DEFAULT_TUPLE_LOG_COUNT;
	LogEst rLogSize = estLog(rSize);
	if (!pBuilder->pOrSet && /* Not pqart of an OR optimization */
	    (pWInfo->wctrlFlags & WHERE_OR_SUBCLAUSE) == 0 &&
	    (pWInfo->pParse->sql_flags & SQL_AutoIndex) != 0 &&
	    pSrc->pIBIndex == 0	/* Has no INDEXED BY clause */
	    && !pSrc->fg.notIndexed	/* Has no NOT INDEXED clause */
	    &&!pSrc->fg.isCorrelated	/* Not a correlated subquery */
	    && !pSrc->fg.isRecursive	/* Not a recursive common table expression. */
	    ) {
		/* Generate auto-index WhereLoops */
		WhereTerm *pTerm;
		WhereTerm *pWCEnd = pWC->a + pWC->nTerm;
		for (pTerm = pWC->a; rc == 0 && pTerm < pWCEnd; pTerm++) {
			if (pTerm->prereqRight & pNew->maskSelf)
				continue;
			if (termCanDriveIndex(pTerm, pSrc, 0)) {
				pNew->nEq = 1;
				pNew->nSkip = 0;
				pNew->index_def = NULL;
				pNew->nLTerm = 1;
				pNew->aLTerm[0] = pTerm;
				/*
				 * TODO: At the moment we have decided to use
				 * this formula, but it is quite aggressive and
				 * needs tuning.
				 */
				pNew->rSetup = rLogSize + rSize;
				/* TUNING: Each index lookup yields 20 rows in the table.  This
				 * is more than the usual guess of 10 rows, since we have no way
				 * of knowing how selective the index will ultimately be.  It would
				 * not be unreasonable to make this value much larger.
				 */
				pNew->nOut = 43;
				assert(43 == sqlLogEst(20));
				pNew->rRun =
				    sqlLogEstAdd(rLogSize, pNew->nOut);
				pNew->wsFlags = WHERE_AUTO_INDEX;
				pNew->prereq = mPrereq | pTerm->prereqRight;
				rc = whereLoopInsert(pBuilder, pNew);
			}
		}
	}
	/*
	 * If there was an INDEXED BY clause, then only that one
	 * index is considered.
	 */
	uint32_t idx_count = 0;
	if (pSrc->pIBIndex != NULL || fake_index != NULL)
		idx_count = 1;
	else
		idx_count = space->index_count;
	for (uint32_t i = 0; i < idx_count; iSortIdx++, i++) {
		if (i > 0)
			probe = space->index[i]->def;
		rSize = index_field_tuple_est(probe, 0);
		pNew->nEq = 0;
		pNew->nBtm = 0;
		pNew->nTop = 0;
		pNew->nSkip = 0;
		pNew->nLTerm = 0;
		pNew->iSortIdx = 0;
		pNew->rSetup = 0;
		pNew->prereq = mPrereq;
		pNew->nOut = rSize;
		pNew->index_def = probe;
		b = indexMightHelpWithOrderBy(pBuilder, probe, pSrc->iCursor);
		/* The ONEPASS_DESIRED flags never occurs together with ORDER BY */
		assert((pWInfo->wctrlFlags & WHERE_ONEPASS_DESIRED) == 0
		       || b == 0);
		pNew->iSortIdx = b ? iSortIdx : 0;
		if (probe->iid == UINT32_MAX) {
			/* Integer primary key index */
			pNew->wsFlags = WHERE_IPK;
			/* TUNING: Cost of full table scan is (N*3.0). */
			pNew->rRun = rSize + 16;
			whereLoopOutputAdjust(pWC, pNew, rSize);
			rc = whereLoopInsert(pBuilder, pNew);
			pNew->nOut = rSize;
			if (rc)
				break;
		} else {
			pNew->wsFlags = WHERE_IDX_ONLY | WHERE_INDEXED;
			/* The cost of visiting the index rows is N*K, where K is
			 * between 1.1 and 3.0 (3.0 and 4.0 for tarantool),
			 * depending on the relative sizes of the
			 * index and table rows.
			 *
			 * In Tarantool we prefer perform full scan over pk instead
			 * of secondary indexes, because secondary indexes
			 * are not really store any data (only pointers to tuples).
			 */
			int notPkPenalty = probe->iid == 0 ? 0 : 4;
			pNew->rRun = rSize + 16 + notPkPenalty;
			whereLoopOutputAdjust(pWC, pNew, rSize);
			rc = whereLoopInsert(pBuilder, pNew);
			pNew->nOut = rSize;
			if (rc)
				break;
		}

		rc = whereLoopAddBtreeIndex(pBuilder, pSrc, probe, 0);
		sqlStat4ProbeFree(pBuilder->pRec);
		pBuilder->nRecValid = 0;
		pBuilder->pRec = 0;
	}
	if (fake_index != NULL)
		index_def_delete(fake_index);
	return rc;
}

/*
 * Add WhereLoop entries to handle OR terms.
 */
static int
whereLoopAddOr(WhereLoopBuilder * pBuilder, Bitmask mPrereq, Bitmask mUnusable)
{
	WhereInfo *pWInfo = pBuilder->pWInfo;
	WhereClause *pWC;
	WhereLoop *pNew;
	WhereTerm *pTerm, *pWCEnd;
	int rc = 0;
	int iCur;
	WhereClause tempWC;
	WhereLoopBuilder sSubBuild;
	WhereOrSet sSum, sCur;
	struct SrcList_item *pItem;

	pWC = pBuilder->pWC;
	pWCEnd = pWC->a + pWC->nTerm;
	pNew = pBuilder->pNew;
	memset(&sSum, 0, sizeof(sSum));
	pItem = pWInfo->pTabList->a + pNew->iTab;
	iCur = pItem->iCursor;

	for (pTerm = pWC->a; pTerm < pWCEnd && rc == 0; pTerm++) {
		if ((pTerm->eOperator & WO_OR) != 0
		    && (pTerm->u.pOrInfo->indexable & pNew->maskSelf) != 0) {
			WhereClause *const pOrWC = &pTerm->u.pOrInfo->wc;
			WhereTerm *const pOrWCEnd = &pOrWC->a[pOrWC->nTerm];
			WhereTerm *pOrTerm;
			int once = 1;
			int i, j;

			sSubBuild = *pBuilder;
			sSubBuild.pOrderBy = 0;
			sSubBuild.pOrSet = &sCur;

			WHERETRACE(0x200,
				   ("Begin processing OR-clause %p\n", pTerm));
			for (pOrTerm = pOrWC->a; pOrTerm < pOrWCEnd; pOrTerm++) {
				if ((pOrTerm->eOperator & WO_AND) != 0) {
					sSubBuild.pWC =
					    &pOrTerm->u.pAndInfo->wc;
				} else if (pOrTerm->leftCursor == iCur) {
					tempWC.pWInfo = pWC->pWInfo;
					tempWC.pOuter = pWC;
					tempWC.op = TK_AND;
					tempWC.nTerm = 1;
					tempWC.a = pOrTerm;
					sSubBuild.pWC = &tempWC;
				} else {
					continue;
				}
				sCur.n = 0;
#ifdef SQL_DEBUG
				WHERETRACE(0x200,
					   ("OR-term %d of %p has %d subterms:\n",
					    (int)(pOrTerm - pOrWC->a), pTerm,
					    sSubBuild.pWC->nTerm));
				if (sqlWhereTrace & 0x400) {
					sqlWhereClausePrint(sSubBuild.pWC);
				}
#endif
				{
					rc = whereLoopAddBtree(&sSubBuild,
							       mPrereq);
				}
				if (rc == 0) {
					rc = whereLoopAddOr(&sSubBuild, mPrereq,
							    mUnusable);
				}
				assert(rc == 0 || sCur.n == 0);
				if (sCur.n == 0) {
					sSum.n = 0;
					break;
				} else if (once) {
					whereOrMove(&sSum, &sCur);
					once = 0;
				} else {
					WhereOrSet sPrev;
					whereOrMove(&sPrev, &sSum);
					sSum.n = 0;
					for (i = 0; i < sPrev.n; i++) {
						for (j = 0; j < sCur.n; j++) {
							whereOrInsert(&sSum,
								      sPrev.a[i].prereq
								      | sCur.a[j].prereq,
								      sqlLogEstAdd(sPrev.a[i].rRun,
										       sCur.a[j].rRun),
								      sqlLogEstAdd(sPrev.a[i].nOut,
										       sCur.a[j].nOut));
						}
					}
				}
			}
			pNew->nLTerm = 1;
			pNew->aLTerm[0] = pTerm;
			pNew->wsFlags = WHERE_MULTI_OR;
			pNew->rSetup = 0;
			pNew->iSortIdx = 0;
			pNew->nEq = 0;
			pNew->nBtm = 0;
			pNew->nTop = 0;
			pNew->index_def = NULL;
			for (i = 0; rc == 0 && i < sSum.n; i++) {
				/* TUNING: Currently sSum.a[i].rRun is set to the sum of the costs
				 * of all sub-scans required by the OR-scan. However, due to rounding
				 * errors, it may be that the cost of the OR-scan is equal to its
				 * most expensive sub-scan. Add the smallest possible penalty
				 * (equivalent to multiplying the cost by 1.07) to ensure that
				 * this does not happen. Otherwise, for WHERE clauses such as the
				 * following where there is an index on "y":
				 *
				 *     WHERE likelihood(x=?, 0.99) OR y=?
				 *
				 * the planner may elect to "OR" together a full-table scan and an
				 * index lookup. And other similarly odd results.
				 */
				pNew->rRun = sSum.a[i].rRun + 1;
				pNew->nOut = sSum.a[i].nOut;
				pNew->prereq = sSum.a[i].prereq;
				rc = whereLoopInsert(pBuilder, pNew);
			}
			WHERETRACE(0x200,
				   ("End processing OR-clause %p\n", pTerm));
		}
	}
	return rc;
}

/*
 * Add all WhereLoop objects for all tables
 */
static int
whereLoopAddAll(WhereLoopBuilder * pBuilder)
{
	WhereInfo *pWInfo = pBuilder->pWInfo;
	Bitmask mPrereq = 0;
	Bitmask mPrior = 0;
	int iTab;
	SrcList *pTabList = pWInfo->pTabList;
	struct SrcList_item *pItem;
	struct SrcList_item *pEnd = &pTabList->a[pWInfo->nLevel];
	sql *db = pWInfo->pParse->db;
	int rc = 0;
	WhereLoop *pNew;
	u8 priorJointype = 0;

	/* Loop over the tables in the join, from left to right */
	pNew = pBuilder->pNew;
	whereLoopInit(pNew);
	for (iTab = 0, pItem = pTabList->a; pItem < pEnd; iTab++, pItem++) {
		Bitmask mUnusable = 0;
		pNew->iTab = iTab;
		pNew->maskSelf =
		    sqlWhereGetMask(&pWInfo->sMaskSet, pItem->iCursor);
		if (((pItem->fg.
		      jointype | priorJointype) & (JT_LEFT | JT_CROSS)) != 0) {
			/* This condition is true when pItem is the FROM clause term on the
			 * right-hand-side of a LEFT or CROSS JOIN.
			 */
			mPrereq = mPrior;
		}
		priorJointype = pItem->fg.jointype;
		{
			rc = whereLoopAddBtree(pBuilder, mPrereq);
		}
		if (rc == 0)
			rc = whereLoopAddOr(pBuilder, mPrereq, mUnusable);
		mPrior |= pNew->maskSelf;
		if (rc || db->mallocFailed)
			break;
	}

	whereLoopClear(db, pNew);
	return rc;
}

/*
 * Examine a WherePath (with the addition of the extra WhereLoop of the 5th
 * parameters) to see if it outputs rows in the requested ORDER BY
 * (or GROUP BY) without requiring a separate sort operation.  Return N:
 *
 *   N>0:   N terms of the ORDER BY clause are satisfied
 *   N==0:  No terms of the ORDER BY clause are satisfied
 *   N<0:   Unknown yet how many terms of ORDER BY might be satisfied.
 *
 * Note that processing for WHERE_GROUPBY and WHERE_DISTINCTBY is not as
 * strict.  With GROUP BY and DISTINCT the only requirement is that
 * equivalent rows appear immediately adjacent to one another.  GROUP BY
 * and DISTINCT do not require rows to appear in any particular order as long
 * as equivalent rows are grouped together.  Thus for GROUP BY and DISTINCT
 * the pOrderBy terms can be matched in any order.  With ORDER BY, the
 * pOrderBy terms must be matched in strict left-to-right order.
 */
static i8
wherePathSatisfiesOrderBy(WhereInfo * pWInfo,	/* The WHERE clause */
			  ExprList * pOrderBy,	/* ORDER BY or GROUP BY or DISTINCT clause to check */
			  WherePath * pPath,	/* The WherePath to check */
			  u16 wctrlFlags,	/* WHERE_GROUPBY or _DISTINCTBY or _ORDERBY_LIMIT */
			  u16 nLoop,		/* Number of entries in pPath->aLoop[] */
			  WhereLoop * pLast,	/* Add this WhereLoop to the end of pPath->aLoop[] */
			  Bitmask * pRevMask)	/* OUT: Mask of WhereLoops to run in reverse order */
{
	u8 revSet;		/* True if rev is known */
	u8 rev;			/* Composite sort order */
	u8 revIdx;		/* Index sort order */
	u8 isOrderDistinct;	/* All prior WhereLoops are order-distinct */
	u8 distinctColumns;	/* True if the loop has UNIQUE NOT NULL columns */
	u8 isMatch;		/* iColumn matches a term of the ORDER BY clause */
	u16 eqOpMask;		/* Allowed equality operators */
	u16 nColumn;		/* Total number of ordered columns in the index */
	u16 nOrderBy;		/* Number terms in the ORDER BY clause */
	int iLoop;		/* Index of WhereLoop in pPath being processed */
	int i, j;		/* Loop counters */
	int iCur;		/* Cursor number for current WhereLoop */
	int iColumn;		/* A column number within table iCur */
	WhereLoop *pLoop = 0;	/* Current WhereLoop being processed. */
	WhereTerm *pTerm;	/* A single term of the WHERE clause */
	Expr *pOBExpr;		/* An expression from the ORDER BY clause */
	struct index_def *idx_def;
	sql *db = pWInfo->pParse->db;	/* Database connection */
	Bitmask obSat = 0;	/* Mask of ORDER BY terms satisfied so far */
	Bitmask obDone;		/* Mask of all ORDER BY terms */
	Bitmask orderDistinctMask;	/* Mask of all well-ordered loops */
	Bitmask ready;		/* Mask of inner loops */

	/*
	 * We say the WhereLoop is "one-row" if it generates no more than one
	 * row of output.  A WhereLoop is one-row if all of the following are true:
	 *  (a) All index columns match with WHERE_COLUMN_EQ.
	 *  (b) The index is unique
	 * Any WhereLoop with an WHERE_COLUMN_EQ constraint on the PK is one-row.
	 * Every one-row WhereLoop will have the WHERE_ONEROW bit set in wsFlags.
	 *
	 * We say the WhereLoop is "order-distinct" if the set of columns from
	 * that WhereLoop that are in the ORDER BY clause are different for every
	 * row of the WhereLoop.  Every one-row WhereLoop is automatically
	 * order-distinct.   A WhereLoop that has no columns in the ORDER BY clause
	 * is not order-distinct. To be order-distinct is not quite the same as being
	 * UNIQUE since a UNIQUE column or index can have multiple rows that
	 * are NULL and NULL values are equivalent for the purpose of order-distinct.
	 * To be order-distinct, the columns must be UNIQUE and NOT NULL.
	 */

	assert(pOrderBy != 0);
	if (nLoop && OptimizationDisabled(db, SQL_OrderByIdxJoin))
		return 0;

	nOrderBy = pOrderBy->nExpr;
	testcase(nOrderBy == BMS - 1);
	if (nOrderBy > BMS - 1)
		return 0;	/* Cannot optimize overly large ORDER BYs */
	isOrderDistinct = 1;
	obDone = MASKBIT(nOrderBy) - 1;
	orderDistinctMask = 0;
	ready = 0;
	eqOpMask = WO_EQ | WO_ISNULL;
	if (wctrlFlags & WHERE_ORDERBY_LIMIT)
		eqOpMask |= WO_IN;
	for (iLoop = 0; isOrderDistinct && obSat < obDone && iLoop <= nLoop;
	     iLoop++) {
		if (iLoop > 0)
			ready |= pLoop->maskSelf;
		if (iLoop < nLoop) {
			pLoop = pPath->aLoop[iLoop];
			if (wctrlFlags & WHERE_ORDERBY_LIMIT)
				continue;
		} else {
			pLoop = pLast;
		}
		iCur = pWInfo->pTabList->a[pLoop->iTab].iCursor;

		/* Mark off any ORDER BY term X that is a column in the table of
		 * the current loop for which there is term in the WHERE
		 * clause of the form X IS NULL or X=? that reference only outer
		 * loops.
		 */
		for (i = 0; i < nOrderBy; i++) {
			if (MASKBIT(i) & obSat)
				continue;
			pOBExpr = sqlExprSkipCollate(pOrderBy->a[i].pExpr);
			if (pOBExpr->op != TK_COLUMN)
				continue;
			if (pOBExpr->iTable != iCur)
				continue;
			pTerm =
			    sqlWhereFindTerm(&pWInfo->sWC, iCur,
						 pOBExpr->iColumn, ~ready,
						 eqOpMask, 0);
			if (pTerm == 0)
				continue;
			if (pTerm->eOperator == WO_IN) {
				/* IN terms are only valid for sorting in the ORDER BY LIMIT
				 * optimization, and then only if they are actually used
				 * by the query plan
				 */
				assert(wctrlFlags & WHERE_ORDERBY_LIMIT);
				for (j = 0;
				     j < pLoop->nLTerm
				     && pTerm != pLoop->aLTerm[j]; j++) {
				}
				if (j >= pLoop->nLTerm)
					continue;
			}
			if ((pTerm->eOperator & WO_EQ) != 0
			    && pOBExpr->iColumn >= 0) {
				bool unused;
				uint32_t lhs_id;
				uint32_t rhs_id;
				struct coll *unused_coll;
				if (sql_expr_coll(pWInfo->pParse,
						  pOrderBy->a[i].pExpr, &unused,
						  &lhs_id, &unused_coll) != 0)
					return 0;
				if (sql_expr_coll(pWInfo->pParse,
						  pTerm->pExpr, &unused,
						  &rhs_id, &unused_coll) != 0)
					return 0;
				if (lhs_id != rhs_id)
					continue;
			}
			obSat |= MASKBIT(i);
		}

		if ((pLoop->wsFlags & WHERE_ONEROW) == 0) {
			if (pLoop->wsFlags & WHERE_IPK) {
				idx_def = NULL;
				nColumn = 1;
			} else if ((idx_def = pLoop->index_def) == NULL ||
				   (idx_def->opts.stat != NULL &&
				    idx_def->opts.stat->is_unordered)) {
				return 0;
			} else {
				nColumn = idx_def->key_def->part_count;
				isOrderDistinct = idx_def->opts.is_unique;
			}

			/* Loop through all columns of the index and deal with the ones
			 * that are not constrained by == or IN.
			 */
			rev = revSet = 0;
			distinctColumns = 0;
			for (j = 0; j < nColumn; j++) {
				u8 bOnce = 1;	/* True to run the ORDER BY search loop */

				assert(j >= pLoop->nEq
				       || (pLoop->aLTerm[j] == 0) == (j < pLoop->nSkip)
				    );
				if (j < pLoop->nEq && j >= pLoop->nSkip) {
					u16 eOp = pLoop->aLTerm[j]->eOperator;

					/* Skip over == and IS NULL terms.  (Also skip IN terms when
					 * doing WHERE_ORDERBY_LIMIT processing).
					 *
					 * If the current term is a column of an ((?,?) IN (SELECT...))
					 * expression for which the SELECT returns more than one column,
					 * check that it is the only column used by this loop. Otherwise,
					 * if it is one of two or more, none of the columns can be
					 * considered to match an ORDER BY term.
					 */
					if ((eOp & eqOpMask) != 0) {
						if (eOp & WO_ISNULL) {
							testcase(isOrderDistinct);
							isOrderDistinct = 0;
						}
						continue;
					} else if (ALWAYS(eOp & WO_IN)) {
						/* ALWAYS() justification: eOp is an equality operator due to the
						 * j<pLoop->nEq constraint above.  Any equality other
						 * than WO_IN is captured by the previous "if".  So this one
						 * always has to be WO_IN.
						 */
						Expr *pX =
						    pLoop->aLTerm[j]->pExpr;
						for (i = j + 1; i < pLoop->nEq;
						     i++) {
							if (pLoop->aLTerm[i]->
							    pExpr == pX) {
								assert((pLoop->
									aLTerm
									[i]->
									eOperator
									&
									WO_IN));
								bOnce = 0;
								break;
							}
						}
					}
				}

				/* Get the column number in the table (iColumn) and sort order
				 * (revIdx) for the j-th column of the index.
				 */
				if (idx_def != NULL) {
					struct key_def *def = idx_def->key_def;
					iColumn = def->parts[j].fieldno;
					revIdx = def->parts[j].sort_order;
				} else {
					iColumn = -1;
					revIdx = 0;
				}

				/* An unconstrained column that might be NULL means that this
				 * WhereLoop is not well-ordered
				 */
				if (isOrderDistinct && iColumn >= 0 &&
				    j >= pLoop->nEq && idx_def != NULL) {
					struct space *space =
						space_by_id(idx_def->space_id);
					assert(space != NULL);
					if (space->def->fields[iColumn].is_nullable)
						isOrderDistinct = 0;
				}

				/* Find the ORDER BY term that corresponds to the j-th column
				 * of the index and mark that ORDER BY term off
				 */
				isMatch = 0;
				for (i = 0; bOnce && i < nOrderBy; i++) {
					if (MASKBIT(i) & obSat)
						continue;
					pOBExpr =
					    sqlExprSkipCollate(pOrderBy-> a[i].pExpr);
					testcase(wctrlFlags & WHERE_GROUPBY);
					testcase(wctrlFlags & WHERE_DISTINCTBY);
					if ((wctrlFlags & (WHERE_GROUPBY | WHERE_DISTINCTBY)) == 0)
						bOnce = 0;
					if (iColumn >= (-1)) {
						if (pOBExpr->op != TK_COLUMN)
							continue;
						if (pOBExpr->iTable != iCur)
							continue;
						if (pOBExpr->iColumn != iColumn)
							continue;
					} else {
						continue;
					}
					if (iColumn >= 0) {
						bool is_found;
						uint32_t id;
						struct coll *unused;
						if (sql_expr_coll(pWInfo->pParse,
								  pOrderBy->a[i].pExpr,
								  &is_found, &id, &unused) != 0)
							return 0;
						if (idx_def->key_def->parts[j].coll_id != id)
							continue;
					}
					isMatch = 1;
					break;
				}
				if (isMatch
				    && (wctrlFlags & WHERE_GROUPBY) == 0) {
					/* Make sure the sort order is compatible in an ORDER BY clause.
					 * Sort order is irrelevant for a GROUP BY clause.
					 */
					if (revSet) {
						if ((rev ^ revIdx) !=
						    pOrderBy->a[i].sort_order)
							isMatch = 0;
					} else {
						rev =
						    revIdx ^ pOrderBy->a[i].
						    sort_order;
						if (rev)
							*pRevMask |=
							    MASKBIT(iLoop);
						revSet = 1;
					}
				}
				if (isMatch) {
					obSat |= MASKBIT(i);
				} else {
					/* No match found */
					if (j == 0 || j < nColumn) {
						testcase(isOrderDistinct != 0);
						isOrderDistinct = 0;
					}
					break;
				}
			}	/* end Loop over all index columns */
			if (distinctColumns) {
				testcase(isOrderDistinct == 0);
				isOrderDistinct = 1;
			}
		}

		/* end-if not one-row */
		/* Mark off any other ORDER BY terms that reference pLoop */
		if (isOrderDistinct) {
			orderDistinctMask |= pLoop->maskSelf;
			for (i = 0; i < nOrderBy; i++) {
				Expr *p;
				Bitmask mTerm;
				if (MASKBIT(i) & obSat)
					continue;
				p = pOrderBy->a[i].pExpr;
				mTerm =
				    sqlWhereExprUsage(&pWInfo->sMaskSet, p);
				if (mTerm == 0 && !sqlExprIsConstant(p))
					continue;
				if ((mTerm & ~orderDistinctMask) == 0) {
					obSat |= MASKBIT(i);
				}
			}
		}
	}			/* End the loop over all WhereLoops from outer-most down to inner-most */
	if (obSat == obDone)
		return (i8) nOrderBy;
	if (!isOrderDistinct) {
		for (i = nOrderBy - 1; i > 0; i--) {
			Bitmask m = MASKBIT(i) - 1;
			if ((obSat & m) == m)
				return i;
		}
		return 0;
	}
	return -1;
}

/*
 * If the WHERE_GROUPBY flag is set in the mask passed to sqlWhereBegin(),
 * the planner assumes that the specified pOrderBy list is actually a GROUP
 * BY clause - and so any order that groups rows as required satisfies the
 * request.
 *
 * Normally, in this case it is not possible for the caller to determine
 * whether or not the rows are really being delivered in sorted order, or
 * just in some other order that provides the required grouping. However,
 * if the WHERE_SORTBYGROUP flag is also passed to sqlWhereBegin(), then
 * this function may be called on the returned WhereInfo object. It returns
 * true if the rows really will be sorted in the specified order, or false
 * otherwise.
 *
 * For example, assuming:
 *
 *   CREATE INDEX i1 ON t1(x, Y);
 *
 * then
 *
 *   SELECT * FROM t1 GROUP BY x,y ORDER BY x,y;   -- IsSorted()==1
 *   SELECT * FROM t1 GROUP BY y,x ORDER BY y,x;   -- IsSorted()==0
 */
int
sqlWhereIsSorted(WhereInfo * pWInfo)
{
	assert(pWInfo->wctrlFlags & WHERE_GROUPBY);
	assert(pWInfo->wctrlFlags & WHERE_SORTBYGROUP);
	return pWInfo->sorted;
}

#ifdef SQL_DEBUG
/* For debugging use only: */
static const char *
wherePathName(WherePath * pPath, int nLoop, WhereLoop * pLast)
{
	static char zName[65];
	int i;
	for (i = 0; i < nLoop; i++) {
		zName[i] = pPath->aLoop[i]->cId;
	}
	if (pLast)
		zName[i++] = pLast->cId;
	zName[i] = 0;
	return zName;
}
#endif

/*
 * Return the cost of sorting nRow rows, assuming that the keys have
 * nOrderby columns and that the first nSorted columns are already in
 * order.
 */
static LogEst
whereSortingCost(WhereInfo * pWInfo, LogEst nRow, int nOrderBy, int nSorted)
{
	/* TUNING: Estimated cost of a full external sort, where N is
	 * the number of rows to sort is:
	 *
	 *   cost = (3.0 * N * log(N)).
	 *
	 * Or, if the order-by clause has X terms but only the last Y
	 * terms are out of order, then block-sorting will reduce the
	 * sorting cost to:
	 *
	 *   cost = (3.0 * N * log(N)) * (Y/X)
	 *
	 * The (Y/X) term is implemented using stack variable rScale
	 * below.
	 */
	LogEst rScale, rSortCost;
	assert(nOrderBy > 0 && 66 == sqlLogEst(100));
	rScale = sqlLogEst((nOrderBy - nSorted) * 100 / nOrderBy) - 66;
	rSortCost = nRow + rScale + 16;

	/* Multiple by log(M) where M is the number of output rows.
	 * Use the LIMIT for M if it is smaller
	 */
	if ((pWInfo->wctrlFlags & WHERE_USE_LIMIT) != 0
	    && pWInfo->iLimit < nRow) {
		nRow = pWInfo->iLimit;
	}
	rSortCost += estLog(nRow);
	return rSortCost;
}

/*
 * Given the list of WhereLoop objects at pWInfo->pLoops, this routine
 * attempts to find the lowest cost path that visits each WhereLoop
 * once.  This path is then loaded into the pWInfo->a[].pWLoop fields.
 *
 * Assume that the total number of output rows that will need to be sorted
 * will be nRowEst (in the 10*log2 representation).  Or, ignore sorting
 * costs if nRowEst==0.
 *
 * Return 0 on success or -1 of a memory allocation
 * error occurs.
 */
static int
wherePathSolver(WhereInfo * pWInfo, LogEst nRowEst)
{
	int mxChoice;		/* Maximum number of simultaneous paths tracked */
	int nLoop;		/* Number of terms in the join */
	Parse *pParse;		/* Parsing context */
	sql *db;		/* The database connection */
	int iLoop;		/* Loop counter over the terms of the join */
	int ii, jj;		/* Loop counters */
	int mxI = 0;		/* Index of next entry to replace */
	int nOrderBy;		/* Number of ORDER BY clause terms */
	LogEst mxCost = 0;	/* Maximum cost of a set of paths */
	LogEst mxUnsorted = 0;	/* Maximum unsorted cost of a set of path */
	int nTo, nFrom;		/* Number of valid entries in aTo[] and aFrom[] */
	WherePath *aFrom;	/* All nFrom paths at the previous level */
	WherePath *aTo;		/* The nTo best paths at the current level */
	WherePath *pFrom;	/* An element of aFrom[] that we are working on */
	WherePath *pTo;		/* An element of aTo[] that we are working on */
	WhereLoop *pWLoop;	/* One of the WhereLoop objects */
	WhereLoop **pX;		/* Used to divy up the pSpace memory */
	LogEst *aSortCost = 0;	/* Sorting and partial sorting costs */
	char *pSpace;		/* Temporary memory used by this routine */
	int nSpace;		/* Bytes of space allocated at pSpace */

	pParse = pWInfo->pParse;
	db = pParse->db;
	nLoop = pWInfo->nLevel;
	/* TUNING: For simple queries, only the best path is tracked.
	 * For 2-way joins, the 5 best paths are followed.
	 * For joins of 3 or more tables, track the 10 best paths
	 */
	mxChoice = (nLoop <= 1) ? 1 : (nLoop == 2 ? 5 : 10);
	assert(nLoop <= pWInfo->pTabList->nSrc);
	WHERETRACE(0x002, ("---- begin solver.  (nRowEst=%d)\n", nRowEst));

	/* If nRowEst is zero and there is an ORDER BY clause, ignore it. In this
	 * case the purpose of this call is to estimate the number of rows returned
	 * by the overall query. Once this estimate has been obtained, the caller
	 * will invoke this function a second time, passing the estimate as the
	 * nRowEst parameter.
	 */
	if (pWInfo->pOrderBy == 0 || nRowEst == 0) {
		nOrderBy = 0;
	} else {
		nOrderBy = pWInfo->pOrderBy->nExpr;
	}

	/* Allocate and initialize space for aTo, aFrom and aSortCost[] */
	nSpace =
	    (sizeof(WherePath) + sizeof(WhereLoop *) * nLoop) * mxChoice * 2;
	nSpace += sizeof(LogEst) * nOrderBy;
	pSpace = sqlDbMallocRawNN(db, nSpace);
	if (pSpace == 0)
		return -1;
	aTo = (WherePath *) pSpace;
	aFrom = aTo + mxChoice;
	memset(aFrom, 0, sizeof(aFrom[0]));
	pX = (WhereLoop **) (aFrom + mxChoice);
	for (ii = mxChoice * 2, pFrom = aTo; ii > 0; ii--, pFrom++, pX += nLoop) {
		pFrom->aLoop = pX;
	}
	if (nOrderBy) {
		/* If there is an ORDER BY clause and it is not being ignored, set up
		 * space for the aSortCost[] array. Each element of the aSortCost array
		 * is either zero - meaning it has not yet been initialized - or the
		 * cost of sorting nRowEst rows of data where the first X terms of
		 * the ORDER BY clause are already in order, where X is the array
		 * index.
		 */
		aSortCost = (LogEst *) pX;
		memset(aSortCost, 0, sizeof(LogEst) * nOrderBy);
	}
	assert(aSortCost == 0
	       || &pSpace[nSpace] == (char *)&aSortCost[nOrderBy]);
	assert(aSortCost != 0 || &pSpace[nSpace] == (char *)pX);

	/* Seed the search with a single WherePath containing zero WhereLoops.
	 *
	 * TUNING: Do not let the number of iterations go above 28.  If the cost
	 * of computing an automatic index is not paid back within the first 28
	 * rows, then do not use the automatic index.
	 */
	aFrom[0].nRow = MIN(pParse->nQueryLoop, 48);
	assert(48 == sqlLogEst(28));
	nFrom = 1;
	assert(aFrom[0].isOrdered == 0);
	if (nOrderBy) {
		/* If nLoop is zero, then there are no FROM terms in the query. Since
		 * in this case the query may return a maximum of one row, the results
		 * are already in the requested order. Set isOrdered to nOrderBy to
		 * indicate this. Or, if nLoop is greater than zero, set isOrdered to
		 * -1, indicating that the result set may or may not be ordered,
		 * depending on the loops added to the current plan.
		 */
		aFrom[0].isOrdered = nLoop > 0 ? -1 : nOrderBy;
	}

	/* Compute successively longer WherePaths using the previous generation
	 * of WherePaths as the basis for the next.  Keep track of the mxChoice
	 * best paths at each generation
	 */
	for (iLoop = 0; iLoop < nLoop; iLoop++) {
		nTo = 0;
		for (ii = 0, pFrom = aFrom; ii < nFrom; ii++, pFrom++) {
			for (pWLoop = pWInfo->pLoops; pWLoop;
			     pWLoop = pWLoop->pNextLoop) {
				LogEst nOut;	/* Rows visited by (pFrom+pWLoop) */
				LogEst rCost;	/* Cost of path (pFrom+pWLoop) */
				LogEst rUnsorted;	/* Unsorted cost of (pFrom+pWLoop) */
				i8 isOrdered = pFrom->isOrdered;	/* isOrdered for (pFrom+pWLoop) */
				Bitmask maskNew;	/* Mask of src visited by (..) */
				Bitmask revMask = 0;	/* Mask of rev-order loops for (..) */

				if ((pWLoop->prereq & ~pFrom->maskLoop) != 0)
					continue;
				if ((pWLoop->maskSelf & pFrom->maskLoop) != 0)
					continue;
				if ((pWLoop->wsFlags & WHERE_AUTO_INDEX) != 0
				    && pFrom->nRow < 10) {
					/* Do not use an automatic index if the this loop is expected
					 * to run less than 2 times.
					 */
					assert(10 == sqlLogEst(2));
					continue;
				}
				/* At this point, pWLoop is a candidate to be the next loop.
				 * Compute its cost
				 */
				rUnsorted =
				    sqlLogEstAdd(pWLoop->rSetup,
						     pWLoop->rRun + pFrom->nRow);
				rUnsorted =
				    sqlLogEstAdd(rUnsorted,
						     pFrom->rUnsorted);
				nOut = pFrom->nRow + pWLoop->nOut;
				maskNew = pFrom->maskLoop | pWLoop->maskSelf;
				if (isOrdered < 0) {
					isOrdered =
					    wherePathSatisfiesOrderBy(pWInfo,
								      pWInfo->pOrderBy,
								      pFrom,
								      pWInfo->wctrlFlags,
								      iLoop,
								      pWLoop,
								      &revMask);
				} else {
					revMask = pFrom->revLoop;
				}
				if (isOrdered >= 0 && isOrdered < nOrderBy) {
					if (aSortCost[isOrdered] == 0) {
						aSortCost[isOrdered] =
						    whereSortingCost(pWInfo,
								     nRowEst,
								     nOrderBy,
								     isOrdered);
					}
					rCost =
					    sqlLogEstAdd(rUnsorted,
							     aSortCost
							     [isOrdered]);

					WHERETRACE(0x002,
						   ("---- sort cost=%-3d (%d/%d) increases cost %3d to %-3d\n",
						    aSortCost[isOrdered],
						    (nOrderBy - isOrdered),
						    nOrderBy, rUnsorted,
						    rCost));
				} else {
					rCost = rUnsorted;
				}

				/* Check to see if pWLoop should be added to the set of
				 * mxChoice best-so-far paths.
				 *
				 * First look for an existing path among best-so-far paths
				 * that covers the same set of loops and has the same isOrdered
				 * setting as the current path candidate.
				 *
				 * The term "((pTo->isOrdered^isOrdered)&0x80)==0" is equivalent
				 * to (pTo->isOrdered==(-1))==(isOrdered==(-1))" for the range
				 * of legal values for isOrdered, -1..64.
				 */
				for (jj = 0, pTo = aTo; jj < nTo; jj++, pTo++) {
					if (pTo->maskLoop == maskNew
					    && ((pTo->isOrdered ^ isOrdered) &
						0x80) == 0) {
						testcase(jj == nTo - 1);
						break;
					}
				}
				if (jj >= nTo) {
					/* None of the existing best-so-far paths match the candidate. */
					if (nTo >= mxChoice
					    && (rCost > mxCost
						|| (rCost == mxCost
						    && rUnsorted >= mxUnsorted))
					    ) {
						/* The current candidate is no better than any of the mxChoice
						 * paths currently in the best-so-far buffer.  So discard
						 * this candidate as not viable.
						 */
#ifdef SQL_DEBUG	/* 0x4 */
						if (sqlWhereTrace & 0x4) {
							sqlDebugPrintf("Skip   %s cost=%-3d,%3d order=%c\n",
									   wherePathName(pFrom, iLoop,
											 pWLoop),
									   rCost,
									   nOut,
									   isOrdered >= 0 ? isOrdered + '0' : '?');
						}
#endif
						continue;
					}
					/* If we reach this points it means that the new candidate path
					 * needs to be added to the set of best-so-far paths.
					 */
					if (nTo < mxChoice) {
						/* Increase the size of the aTo set by one */
						jj = nTo++;
					} else {
						/* New path replaces the prior worst to keep count below mxChoice */
						jj = mxI;
					}
					pTo = &aTo[jj];
#ifdef SQL_DEBUG	/* 0x4 */
					if (sqlWhereTrace & 0x4) {
						sqlDebugPrintf
						    ("New    %s cost=%-3d,%3d order=%c\n",
						     wherePathName(pFrom, iLoop,
								   pWLoop),
						     rCost, nOut,
						     isOrdered >=
						     0 ? isOrdered + '0' : '?');
					}
#endif
				} else {
					/* Control reaches here if best-so-far path pTo=aTo[jj] covers the
					 * same set of loops and has the sam isOrdered setting as the
					 * candidate path.  Check to see if the candidate should replace
					 * pTo or if the candidate should be skipped
					 */
					if (pTo->rCost < rCost
					    || (pTo->rCost == rCost
						&& pTo->nRow <= nOut)) {
#ifdef SQL_DEBUG	/* 0x4 */
						if (sqlWhereTrace & 0x4) {
							sqlDebugPrintf("Skip   %s cost=%-3d,%3d order=%c",
									   wherePathName(pFrom, iLoop,
											 pWLoop),
									   rCost,
									   nOut,
									   isOrdered >= 0 ? isOrdered + '0' : '?');
							sqlDebugPrintf("   vs %s cost=%-3d,%d order=%c\n",
									   wherePathName(pTo,
											 iLoop + 1,
											 0),
									   pTo->rCost,
									   pTo->nRow,
									   pTo->isOrdered >= 0 ? pTo->isOrdered +'0' : '?');
						}
#endif
						/* Discard the candidate path from further consideration */
						testcase(pTo->rCost == rCost);
						continue;
					}
					testcase(pTo->rCost == rCost + 1);
					/* Control reaches here if the candidate path is better than the
					 * pTo path.  Replace pTo with the candidate.
					 */
#ifdef SQL_DEBUG	/* 0x4 */
					if (sqlWhereTrace & 0x4) {
						sqlDebugPrintf("Update %s cost=%-3d,%3d order=%c",
								   wherePathName(pFrom, iLoop,
										 pWLoop),
								   rCost,
								   nOut,
								   isOrdered >= 0 ? isOrdered + '0' : '?');
						sqlDebugPrintf("  was %s cost=%-3d,%3d order=%c\n",
								   wherePathName(pTo,
										 iLoop + 1,
										 0),
								   pTo->rCost,
								   pTo->nRow,
								   pTo->isOrdered >= 0 ? pTo->isOrdered + '0' : '?');
					}
#endif
				}
				/* pWLoop is a winner.  Add it to the set of best so far */
				pTo->maskLoop = pFrom->maskLoop | pWLoop->maskSelf;
				pTo->revLoop = revMask;
				pTo->nRow = nOut;
				pTo->rCost = rCost;
				pTo->rUnsorted = rUnsorted;
				pTo->isOrdered = isOrdered;
				memcpy(pTo->aLoop, pFrom->aLoop,
				       sizeof(WhereLoop *) * iLoop);
				pTo->aLoop[iLoop] = pWLoop;
				if (nTo >= mxChoice) {
					mxI = 0;
					mxCost = aTo[0].rCost;
					mxUnsorted = aTo[0].nRow;
					for (jj = 1, pTo = &aTo[1];
					     jj < mxChoice; jj++, pTo++) {
						if (pTo->rCost > mxCost
						    || (pTo->rCost == mxCost
							&& pTo->rUnsorted >
							mxUnsorted)
						    ) {
							mxCost = pTo->rCost;
							mxUnsorted =
							    pTo->rUnsorted;
							mxI = jj;
						}
					}
				}
			}
		}

#ifdef SQL_DEBUG	/* >=2 */
		if (sqlWhereTrace & 0x02) {
			sqlDebugPrintf("---- after round %d ----\n", iLoop);
			for (ii = 0, pTo = aTo; ii < nTo; ii++, pTo++) {
				sqlDebugPrintf(" %s cost=%-3d nrow=%-3d order=%c",
						   wherePathName(pTo, iLoop + 1, 0),
						   pTo->rCost, pTo->nRow,
						   pTo->isOrdered >=
						   0 ? (pTo->isOrdered + '0') : '?');
				if (pTo->isOrdered > 0) {
					sqlDebugPrintf(" rev=0x%llx\n",
							   pTo->revLoop);
				} else {
					sqlDebugPrintf("\n");
				}
			}
		}
#endif

		/* Swap the roles of aFrom and aTo for the next generation */
		pFrom = aTo;
		aTo = aFrom;
		aFrom = pFrom;
		nFrom = nTo;
	}

	assert(nFrom != 0);

	/* Find the lowest cost path.  pFrom will be left pointing to that path */
	pFrom = aFrom;
	for (ii = 1; ii < nFrom; ii++) {
		if (pFrom->rCost > aFrom[ii].rCost)
			pFrom = &aFrom[ii];
	}
	assert(pWInfo->nLevel == nLoop);
	/* Load the lowest cost path into pWInfo */
	for (iLoop = 0; iLoop < nLoop; iLoop++) {
		WhereLevel *pLevel = pWInfo->a + iLoop;
		pLevel->pWLoop = pWLoop = pFrom->aLoop[iLoop];
		pLevel->iFrom = pWLoop->iTab;
		pLevel->iTabCur = pWInfo->pTabList->a[pLevel->iFrom].iCursor;
	}
	if ((pWInfo->wctrlFlags & WHERE_WANT_DISTINCT) != 0
	    && (pWInfo->wctrlFlags & WHERE_DISTINCTBY) == 0
	    && pWInfo->eDistinct == WHERE_DISTINCT_NOOP && nRowEst) {
		Bitmask notUsed;
		int rc =
		    wherePathSatisfiesOrderBy(pWInfo, pWInfo->pDistinctSet,
					      pFrom,
					      WHERE_DISTINCTBY, nLoop - 1,
					      pFrom->aLoop[nLoop - 1],
					      &notUsed);
		if (rc == pWInfo->pDistinctSet->nExpr) {
			pWInfo->eDistinct = WHERE_DISTINCT_ORDERED;
		}
	}
	if (pWInfo->pOrderBy) {
		if (pWInfo->wctrlFlags & WHERE_DISTINCTBY) {
			if (pFrom->isOrdered == pWInfo->pOrderBy->nExpr) {
				pWInfo->eDistinct = WHERE_DISTINCT_ORDERED;
			}
		} else {
			pWInfo->nOBSat = pFrom->isOrdered;
			pWInfo->revMask = pFrom->revLoop;
			if (pWInfo->nOBSat <= 0) {
				pWInfo->nOBSat = 0;
				if (nLoop > 0) {
					u32 wsFlags =
					    pFrom->aLoop[nLoop - 1]->wsFlags;
					if ((wsFlags & WHERE_ONEROW) == 0
					    && (wsFlags & (WHERE_IPK | WHERE_COLUMN_IN)) != (WHERE_IPK | WHERE_COLUMN_IN)) {
						Bitmask m = 0;
						int rc =
						    wherePathSatisfiesOrderBy
						    (pWInfo, pWInfo->pOrderBy,
						     pFrom,
						     WHERE_ORDERBY_LIMIT,
						     nLoop - 1,
						     pFrom->aLoop[nLoop - 1],
						     &m);
						testcase(wsFlags & WHERE_IPK);
						testcase(wsFlags &
							 WHERE_COLUMN_IN);
						if (rc ==
						    pWInfo->pOrderBy->nExpr) {
							pWInfo->
							    bOrderedInnerLoop =
							    1;
							pWInfo->revMask = m;
						}
					}
				}
			}
		}
		if ((pWInfo->wctrlFlags & WHERE_SORTBYGROUP)
		    && pWInfo->nOBSat == pWInfo->pOrderBy->nExpr && nLoop > 0) {
			Bitmask revMask = 0;
			int nOrder =
			    wherePathSatisfiesOrderBy(pWInfo, pWInfo->pOrderBy,
						      pFrom, 0, nLoop - 1,
						      pFrom->aLoop[nLoop - 1],
						      &revMask);
			assert(pWInfo->sorted == 0);
			if (nOrder == pWInfo->pOrderBy->nExpr) {
				pWInfo->sorted = 1;
				pWInfo->revMask = revMask;
			}
		}
	}

	pWInfo->nRowOut = pFrom->nRow;

	/* Free temporary memory and return success */
	sqlDbFree(db, pSpace);
	return 0;
}

/**
 * Attempt at finding appropriate terms in WHERE clause.
 *
 * @param loop The loop @where belongs to.
 * @param where The WHERE clause to be examined..
 * @param cursor Cursor number of LHS.
 * @param space_def Def of the space related to WHERE clause.
 * @param index_def Def of the index to be used to satisfy WHERE
 *                  clause. May be NULL.
 * @retval 0 on success, -1 otherwise.
 */
static int
where_loop_assign_terms(struct WhereLoop *loop, struct WhereClause *where,
			int cursor, struct space_def *space_def,
			struct index_def *idx_def)
{
	uint32_t column_count = idx_def != NULL ? idx_def->key_def->part_count :
				space_def->field_count;
	if (column_count > ArraySize(loop->aLTermSpace))
		return -1;
	uint32_t i;
	for (i = 0; i < column_count; ++i) {
		struct WhereTerm *term =
			where_clause_find_term(where, cursor, i, 0, WO_EQ,
					       space_def, idx_def != NULL ?
					       idx_def->key_def : NULL);
		if (term == NULL)
			break;
		testcase(pTerm->eOperator & WO_IS);
		loop->aLTerm[i] = term;
	}
	if (i != column_count)
		return -1;
	loop->wsFlags = WHERE_COLUMN_EQ | WHERE_ONEROW | WHERE_INDEXED |
			WHERE_IDX_ONLY;
	loop->nLTerm = i;
	loop->nEq = i;
	loop->index_def = idx_def;
	/* TUNING: Cost of a unique index lookup is 15. */
	assert(39 == sqlLogEst(15));
	loop->rRun = 39;
	return 0;
}

/**
 * Most queries use only a single table (they are not joins) and
 * have simple == constraints against indexed fields. This
 * routine attempts to plan those simple cases using much less
 * ceremony than the general-purpose query planner, and thereby
 * yield faster sql_prepare() times for the common case.
 *
 * @param builder Where-Loop Builder.
 * @retval Return non-zero on success, i.e. if this query can be
 *         handled by this no-frills query planner. Return zero
 *         if this query needs the general-purpose query planner.
 */
static int
where_loop_builder_shortcut(struct WhereLoopBuilder *builder)
{
	struct WhereInfo *where_info = builder->pWInfo;
	if (where_info->wctrlFlags & WHERE_OR_SUBCLAUSE)
		return 0;
	assert(where_info->pTabList->nSrc >= 1);
	struct SrcList_item *item = where_info->pTabList->a;
	struct space_def *space_def = item->space->def;
	assert(space_def != NULL);
	if (item->fg.isIndexedBy)
		return 0;
	int cursor = item->iCursor;
	struct WhereClause *clause = &where_info->sWC;
	struct WhereLoop *loop = builder->pNew;
	loop->wsFlags = 0;
	loop->nSkip = 0;
	loop->index_def = NULL;
	struct WhereTerm *term = sqlWhereFindTerm(clause, cursor, -1, 0,
						      WO_EQ, 0);
	if (term != NULL) {
		loop->wsFlags = WHERE_COLUMN_EQ | WHERE_IPK | WHERE_ONEROW;
		loop->aLTerm[0] = term;
		loop->nLTerm = 1;
		loop->nEq = 1;
		/* TUNING: Cost of a PK lookup is 10. */
		assert(33 == sqlLogEst(10));
		loop->rRun = 33;
	} else {
		assert(loop->aLTermSpace == loop->aLTerm);
		struct space *space = item->space;
		if (space != NULL) {
			for (uint32_t i = 0; i < space->index_count; ++i) {
				struct index_def *idx_def =
					space->index[i]->def;
				if (!idx_def->opts.is_unique)
					continue;
				if (where_loop_assign_terms(loop, clause,
							    cursor, space_def,
							    idx_def) == 0)
					break;
			}
		} else {
			/* Space is ephemeral. */
			assert(space_def->id == 0);
			where_loop_assign_terms(loop, clause, cursor,
						space_def, NULL);
		}
	}
	if (loop->wsFlags) {
		loop->nOut = (LogEst) 1;
		where_info->a[0].pWLoop = loop;
		loop->maskSelf = sqlWhereGetMask(&where_info->sMaskSet,
						     cursor);
		where_info->a[0].iTabCur = cursor;
		where_info->nRowOut = 1;
		if (where_info->pOrderBy)
			where_info->nOBSat = where_info->pOrderBy->nExpr;
		if (where_info->wctrlFlags & WHERE_WANT_DISTINCT) {
			where_info->eDistinct = WHERE_DISTINCT_UNIQUE;
		}
#ifdef SQL_DEBUG
		loop->cId = '0';
#endif
		return 1;
	}
	return 0;
}

/*
 * Generate the beginning of the loop used for WHERE clause processing.
 * The return value is a pointer to an opaque structure that contains
 * information needed to terminate the loop.  Later, the calling routine
 * should invoke sqlWhereEnd() with the return value of this function
 * in order to complete the WHERE clause processing.
 *
 * If an error occurs, this routine returns NULL.
 *
 * The basic idea is to do a nested loop, one loop for each table in
 * the FROM clause of a select.  (INSERT and UPDATE statements are the
 * same as a SELECT with only a single table in the FROM clause.)  For
 * example, if the SQL is this:
 *
 *       SELECT * FROM t1, t2, t3 WHERE ...;
 *
 * Then the code generated is conceptually like the following:
 *
 *      foreach row1 in t1 do       \    Code generated
 *        foreach row2 in t2 do      |-- by sqlWhereBegin()
 *          foreach row3 in t3 do   /
 *            ...
 *          end                     \    Code generated
 *        end                        |-- by sqlWhereEnd()
 *      end                         /
 *
 * Note that the loops might not be nested in the order in which they
 * appear in the FROM clause if a different order is better able to make
 * use of indices.  Note also that when the IN operator appears in
 * the WHERE clause, it might result in additional nested loops for
 * scanning through all values on the right-hand side of the IN.
 *
 * There are Btree cursors associated with each table.  t1 uses cursor
 * number pTabList->a[0].iCursor.  t2 uses the cursor pTabList->a[1].iCursor.
 * And so forth.  This routine generates code to open those VDBE cursors
 * and sqlWhereEnd() generates the code to close them.
 *
 * The code that sqlWhereBegin() generates leaves the cursors named
 * in pTabList pointing at their appropriate entries.  The [...] code
 * can use OP_Column opcode on these cursors to extract
 * data from the various tables of the loop.
 *
 * If the WHERE clause is empty, the foreach loops must each scan their
 * entire tables.  Thus a three-way join is an O(N^3) operation.  But if
 * the tables have indices and there are terms in the WHERE clause that
 * refer to those indices, a complete table scan can be avoided and the
 * code will run much faster.  Most of the work of this routine is checking
 * to see if there are indices that can be used to speed up the loop.
 *
 * Terms of the WHERE clause are also used to limit which rows actually
 * make it to the "..." in the middle of the loop.  After each "foreach",
 * terms of the WHERE clause that use only terms in that loop and outer
 * loops are evaluated and if false a jump is made around all subsequent
 * inner loops (or around the "..." if the test occurs within the inner-
 * most loop)
 *
 * OUTER JOINS
 *
 * An outer join of tables t1 and t2 is conceptally coded as follows:
 *
 *    foreach row1 in t1 do
 *      flag = 0
 *      foreach row2 in t2 do
 *        start:
 *          ...
 *          flag = 1
 *      end
 *      if flag==0 then
 *        move the row2 cursor to a null row
 *        goto start
 *      fi
 *    end
 *
 * ORDER BY CLAUSE PROCESSING
 *
 * pOrderBy is a pointer to the ORDER BY clause (or the GROUP BY clause
 * if the WHERE_GROUPBY flag is set in wctrlFlags) of a SELECT statement
 * if there is one.  If there is no ORDER BY clause or if this routine
 * is called from an UPDATE or DELETE statement, then pOrderBy is NULL.
 *
 * The iIdxCur parameter is the cursor number of an index.  If
 * WHERE_OR_SUBCLAUSE is set, iIdxCur is the cursor number of an index
 * to use for OR clause processing.  The WHERE clause should use this
 * specific cursor.  If WHERE_ONEPASS_DESIRED is set, then iIdxCur is
 * the first cursor in an array of cursors for all indices.  iIdxCur should
 * be used to compute the appropriate cursor depending on which index is
 * used.
 */
WhereInfo *
sqlWhereBegin(Parse * pParse,	/* The parser context */
		  SrcList * pTabList,	/* FROM clause: A list of all tables to be scanned */
		  Expr * pWhere,	/* The WHERE clause */
		  ExprList * pOrderBy,	/* An ORDER BY (or GROUP BY) clause, or NULL */
		  ExprList * pDistinctSet,	/* Try not to output two rows that duplicate these */
		  u16 wctrlFlags,	/* The WHERE_* flags defined in sqlInt.h */
		  int iAuxArg)		/* If WHERE_OR_SUBCLAUSE is set, index cursor number
					 * If WHERE_USE_LIMIT, then the limit amount
					 */
{
	int nByteWInfo;		/* Num. bytes allocated for WhereInfo struct */
	int nTabList;		/* Number of elements in pTabList */
	WhereInfo *pWInfo;	/* Will become the return value of this function */
	Vdbe *v = pParse->pVdbe;	/* The virtual database engine */
	Bitmask notReady;	/* Cursors that are not yet positioned */
	WhereLoopBuilder sWLB;	/* The WhereLoop builder */
	WhereMaskSet *pMaskSet;	/* The expression mask set */
	WhereLevel *pLevel;	/* A single level in pWInfo->a[] */
	WhereLoop *pLoop;	/* Pointer to a single WhereLoop object */
	int ii;			/* Loop counter */
	sql *db;		/* Database connection */
	int rc;			/* Return code */
	u8 bFordelete = 0;	/* OPFLAG_FORDELETE or zero, as appropriate */

#ifdef SQL_DEBUG
	if ((pParse->sql_flags & SQL_WhereTrace) != 0)
		sqlWhereTrace = 0xfff;
	else
		sqlWhereTrace = 0;
#endif
	assert((wctrlFlags & WHERE_ONEPASS_MULTIROW) == 0 || ((wctrlFlags &
							       WHERE_ONEPASS_DESIRED)
							      != 0
							      && (wctrlFlags &
								  WHERE_OR_SUBCLAUSE)
							      == 0));

	/* Only one of WHERE_OR_SUBCLAUSE or WHERE_USE_LIMIT */
	assert((wctrlFlags & WHERE_OR_SUBCLAUSE) == 0
	       || (wctrlFlags & WHERE_USE_LIMIT) == 0);

	/* Variable initialization */
	db = pParse->db;
	memset(&sWLB, 0, sizeof(sWLB));

	/* An ORDER/GROUP BY clause of more than 63 terms cannot be optimized */
	testcase(pOrderBy && pOrderBy->nExpr == BMS - 1);
	if (pOrderBy && pOrderBy->nExpr >= BMS)
		pOrderBy = 0;
	sWLB.pOrderBy = pOrderBy;

	/* Disable the DISTINCT optimization if SQL_DistinctOpt is set via
	 * sql_test_ctrl(SQL_TESTCTRL_OPTIMIZATIONS,...)
	 */
	if (OptimizationDisabled(db, SQL_DistinctOpt)) {
		wctrlFlags &= ~WHERE_WANT_DISTINCT;
	}

	/* The number of tables in the FROM clause is limited by the number of
	 * bits in a Bitmask
	 */
	testcase(pTabList->nSrc == BMS);
	if (pTabList->nSrc > BMS) {
		diag_set(ClientError, ER_SQL_PARSER_LIMIT, "The number of "\
			 "tables in a join", pTabList->nSrc, BMS);
		pParse->is_aborted = true;
		return 0;
	}

	/* This function normally generates a nested loop for all tables in
	 * pTabList.  But if the WHERE_OR_SUBCLAUSE flag is set, then we should
	 * only generate code for the first table in pTabList and assume that
	 * any cursors associated with subsequent tables are uninitialized.
	 */
	nTabList = (wctrlFlags & WHERE_OR_SUBCLAUSE) ? 1 : pTabList->nSrc;

	/* Allocate and initialize the WhereInfo structure that will become the
	 * return value. A single allocation is used to store the WhereInfo
	 * struct, the contents of WhereInfo.a[], the WhereClause structure
	 * and the WhereMaskSet structure. Since WhereClause contains an 8-byte
	 * field (type Bitmask) it must be aligned on an 8-byte boundary on
	 * some architectures. Hence the ROUND8() below.
	 */
	nByteWInfo =
	    ROUND8(sizeof(WhereInfo) + (nTabList - 1) * sizeof(WhereLevel));
	pWInfo = sqlDbMallocRawNN(db, nByteWInfo + sizeof(WhereLoop));
	if (db->mallocFailed) {
		sqlDbFree(db, pWInfo);
		pWInfo = 0;
		goto whereBeginError;
	}
	pWInfo->pParse = pParse;
	pWInfo->pTabList = pTabList;
	pWInfo->pOrderBy = pOrderBy;
	pWInfo->pDistinctSet = pDistinctSet;
	pWInfo->aiCurOnePass[0] = pWInfo->aiCurOnePass[1] = -1;
	pWInfo->nLevel = nTabList;
	pWInfo->iBreak = pWInfo->iContinue = sqlVdbeMakeLabel(v);
	pWInfo->wctrlFlags = wctrlFlags;
	pWInfo->iLimit = iAuxArg;
	pWInfo->savedNQueryLoop = pParse->nQueryLoop;
	memset(&pWInfo->nOBSat, 0,
	       offsetof(WhereInfo, sWC) - offsetof(WhereInfo, nOBSat));
	memset(&pWInfo->a[0], 0,
	       sizeof(WhereLoop) + nTabList * sizeof(WhereLevel));
	assert(pWInfo->eOnePass == ONEPASS_OFF);	/* ONEPASS defaults to OFF */
	pMaskSet = &pWInfo->sMaskSet;
	sWLB.pWInfo = pWInfo;
	sWLB.pWC = &pWInfo->sWC;
	sWLB.pNew = (WhereLoop *) (((char *)pWInfo) + nByteWInfo);
	assert(EIGHT_BYTE_ALIGNMENT(sWLB.pNew));
	whereLoopInit(sWLB.pNew);
#ifdef SQL_DEBUG
	sWLB.pNew->cId = '*';
#endif

	/* Split the WHERE clause into separate subexpressions where each
	 * subexpression is separated by an AND operator.
	 */
	initMaskSet(pMaskSet);
	sqlWhereClauseInit(&pWInfo->sWC, pWInfo);
	sqlWhereSplit(&pWInfo->sWC, pWhere, TK_AND);

	/* Special case: a WHERE clause that is constant.  Evaluate the
	 * expression and either jump over all of the code or fall thru.
	 */
	for (ii = 0; ii < sWLB.pWC->nTerm; ii++) {
		if (nTabList == 0
		    || sqlExprIsConstantNotJoin(sWLB.pWC->a[ii].pExpr)) {
			sqlExprIfFalse(pParse, sWLB.pWC->a[ii].pExpr,
					   pWInfo->iBreak, SQL_JUMPIFNULL);
			sWLB.pWC->a[ii].wtFlags |= TERM_CODED;
		}
	}

	/* Special case: No FROM clause
	 */
	if (nTabList == 0) {
		if (pOrderBy)
			pWInfo->nOBSat = pOrderBy->nExpr;
		if (wctrlFlags & WHERE_WANT_DISTINCT) {
			pWInfo->eDistinct = WHERE_DISTINCT_UNIQUE;
		}
	}

	/* Assign a bit from the bitmask to every term in the FROM clause.
	 *
	 * The N-th term of the FROM clause is assigned a bitmask of 1<<N.
	 *
	 * The rule of the previous sentence ensures thta if X is the bitmask for
	 * a table T, then X-1 is the bitmask for all other tables to the left of T.
	 * Knowing the bitmask for all tables to the left of a left join is
	 * important.  Ticket #3015.
	 *
	 * Note that bitmasks are created for all pTabList->nSrc tables in
	 * pTabList, not just the first nTabList tables.  nTabList is normally
	 * equal to pTabList->nSrc but might be shortened to 1 if the
	 * WHERE_OR_SUBCLAUSE flag is set.
	 */
	for (ii = 0; ii < pTabList->nSrc; ii++) {
		createMask(pMaskSet, pTabList->a[ii].iCursor);
		sqlWhereTabFuncArgs(pParse, &pTabList->a[ii], &pWInfo->sWC);
	}
#ifdef SQL_DEBUG
	for (ii = 0; ii < pTabList->nSrc; ii++) {
		Bitmask m =
		    sqlWhereGetMask(pMaskSet, pTabList->a[ii].iCursor);
		assert(m == MASKBIT(ii));
	}
#endif

	/* Analyze all of the subexpressions. */
	sqlWhereExprAnalyze(pTabList, &pWInfo->sWC);
	if (db->mallocFailed)
		goto whereBeginError;

	if (wctrlFlags & WHERE_WANT_DISTINCT) {
		if (isDistinctRedundant
		    (pParse, pTabList, &pWInfo->sWC, pDistinctSet)) {
			/* The DISTINCT marking is pointless.  Ignore it. */
			pWInfo->eDistinct = WHERE_DISTINCT_UNIQUE;
		} else if (pOrderBy == 0) {
			/* Try to ORDER BY the result set to make distinct processing easier */
			pWInfo->wctrlFlags |= WHERE_DISTINCTBY;
			pWInfo->pOrderBy = pDistinctSet;
		}
	}

	/* Construct the WhereLoop objects */
#if defined(SQL_DEBUG)
	if (sqlWhereTrace & 0xffff) {
		sqlDebugPrintf("*** Optimizer Start *** (wctrlFlags: 0x%x",
				   wctrlFlags);
		if (wctrlFlags & WHERE_USE_LIMIT) {
			sqlDebugPrintf(", limit: %d", iAuxArg);
		}
		sqlDebugPrintf(")\n");
	}
	if (sqlWhereTrace & 0x100) {	/* Display all terms of the WHERE clause */
		sqlWhereClausePrint(sWLB.pWC);
	}
#endif

	if (nTabList != 1 || where_loop_builder_shortcut(&sWLB) == 0) {
		rc = whereLoopAddAll(&sWLB);
		if (rc)
			goto whereBeginError;

#ifdef SQL_DEBUG
		if (sqlWhereTrace) {	/* Display all of the WhereLoop objects */
			WhereLoop *p;
			int i;
			static const char zLabel[] =
			    "0123456789abcdefghijklmnopqrstuvwyxz"
			    "ABCDEFGHIJKLMNOPQRSTUVWYXZ";
			for (p = pWInfo->pLoops, i = 0; p;
			     p = p->pNextLoop, i++) {
				p->cId = zLabel[i % sizeof(zLabel)];
				whereLoopPrint(p, sWLB.pWC);
			}
		}
#endif

		wherePathSolver(pWInfo, 0);
		if (db->mallocFailed)
			goto whereBeginError;
		if (pWInfo->pOrderBy) {
			wherePathSolver(pWInfo, pWInfo->nRowOut + 1);
			if (db->mallocFailed)
				goto whereBeginError;
		}
	}
	if (pWInfo->pOrderBy == 0 &&
	    (pParse->sql_flags & SQL_ReverseOrder) != 0) {
		pWInfo->revMask = ALLBITS;
	}
	if (pParse->is_aborted || NEVER(db->mallocFailed)) {
		goto whereBeginError;
	}
#ifdef SQL_DEBUG
	if (sqlWhereTrace) {
		sqlDebugPrintf("---- Solution nRow=%d", pWInfo->nRowOut);
		if (pWInfo->nOBSat > 0) {
			sqlDebugPrintf(" ORDERBY=%d,0x%llx", pWInfo->nOBSat,
					   pWInfo->revMask);
		}
		switch (pWInfo->eDistinct) {
		case WHERE_DISTINCT_UNIQUE:{
				sqlDebugPrintf("  DISTINCT=unique");
				break;
			}
		case WHERE_DISTINCT_ORDERED:{
				sqlDebugPrintf("  DISTINCT=ordered");
				break;
			}
		case WHERE_DISTINCT_UNORDERED:{
				sqlDebugPrintf("  DISTINCT=unordered");
				break;
			}
		}
		sqlDebugPrintf("\n");
		for (ii = 0; ii < pWInfo->nLevel; ii++) {
			whereLoopPrint(pWInfo->a[ii].pWLoop, sWLB.pWC);
		}
	}
#endif
	/* Attempt to omit tables from the join that do not effect the result */
	if (pWInfo->nLevel >= 2
	    && pDistinctSet != 0 && OptimizationEnabled(db, SQL_OmitNoopJoin)
	    ) {
		Bitmask tabUsed =
		    sqlWhereExprListUsage(pMaskSet, pDistinctSet);
		if (sWLB.pOrderBy) {
			tabUsed |=
			    sqlWhereExprListUsage(pMaskSet, sWLB.pOrderBy);
		}
		while (pWInfo->nLevel >= 2) {
			WhereTerm *pTerm, *pEnd;
			pLoop = pWInfo->a[pWInfo->nLevel - 1].pWLoop;
			if ((pWInfo->pTabList->a[pLoop->iTab].fg.
			     jointype & JT_LEFT) == 0)
				break;
			if ((wctrlFlags & WHERE_WANT_DISTINCT) == 0
			    && (pLoop->wsFlags & WHERE_ONEROW) == 0) {
				break;
			}
			if ((tabUsed & pLoop->maskSelf) != 0)
				break;
			pEnd = sWLB.pWC->a + sWLB.pWC->nTerm;
			for (pTerm = sWLB.pWC->a; pTerm < pEnd; pTerm++) {
				if ((pTerm->prereqAll & pLoop->maskSelf) != 0
				    && !ExprHasProperty(pTerm->pExpr,
							EP_FromJoin)
				    ) {
					break;
				}
			}
			if (pTerm < pEnd)
				break;
			WHERETRACE(0xffff,
				   ("-> drop loop %c not used\n", pLoop->cId));
			pWInfo->nLevel--;
			nTabList--;
		}
	}
	WHERETRACE(0xffff, ("*** Optimizer Finished ***\n"));
	pWInfo->pParse->nQueryLoop += pWInfo->nRowOut;

	/* If the caller is an UPDATE or DELETE statement that is requesting
	 * to use a one-pass algorithm, determine if this is appropriate.
	 */
	assert((wctrlFlags & WHERE_ONEPASS_DESIRED) == 0
	       || pWInfo->nLevel == 1);
	if ((wctrlFlags & WHERE_ONEPASS_DESIRED) != 0) {
		int wsFlags = pWInfo->a[0].pWLoop->wsFlags;
		int bOnerow = (wsFlags & WHERE_ONEROW) != 0;
		if (bOnerow || (wctrlFlags & WHERE_ONEPASS_MULTIROW) != 0) {
			pWInfo->eOnePass =
			    bOnerow ? ONEPASS_SINGLE : ONEPASS_MULTI;
		}
	}

	/* Open all tables in the pTabList and any indices selected for
	 * searching those tables.
	 */
	for (ii = 0, pLevel = pWInfo->a; ii < nTabList; ii++, pLevel++) {
		struct SrcList_item *pTabItem = &pTabList->a[pLevel->iFrom];
		struct space_def *space_def = pTabItem->space->def;
		pLoop = pLevel->pWLoop;
		struct space *space = pTabItem->space;
		if (space_def->id == 0 || space_def->opts.is_view) {
			/* Do nothing */
		} else if ((pLoop->wsFlags & WHERE_IDX_ONLY) == 0 &&
			   (wctrlFlags & WHERE_OR_SUBCLAUSE) == 0) {
			if (pWInfo->eOnePass != ONEPASS_OFF)
				pWInfo->aiCurOnePass[0] = pTabItem->iCursor;
			assert(space->index_count > 0);
			vdbe_emit_open_cursor(pParse, pTabItem->iCursor, 0,
					      space);
			VdbeComment((v, "%s", space->def->name));
			assert(pTabItem->iCursor == pLevel->iTabCur);
			sqlVdbeChangeP5(v, bFordelete);
		}
		if (pLoop->wsFlags & WHERE_INDEXED) {
			struct index_def *idx_def = pLoop->index_def;
			int iIndexCur;
			int op = OP_IteratorOpen;
			/* Check if index is primary. Either of
			 * points should be true:
			 * 1. struct Index is non-NULL and is
			 *    primary
			 * 2. idx_def is non-NULL and it is
			 *    primary
			 * 3. (goal of this comment) both pIx and
			 *    idx_def are NULL in which case it is
			 *    ephemeral table, but not in Tnt sense.
			 *    It is something w/ defined space_def
			 *    and nothing else. Skip such loops.
			 */
			if (idx_def == NULL)
				continue;
			if (idx_def->iid == 0
			    && (wctrlFlags & WHERE_OR_SUBCLAUSE) != 0) {
				/* This is one term of an OR-optimization using
				 * the PRIMARY KEY.  No need for a separate index
				 */
				iIndexCur = pLevel->iTabCur;
				op = 0;
			} else if (pWInfo->eOnePass != ONEPASS_OFF) {
				iIndexCur = iAuxArg;
				if (pTabItem->space->index_count != 0) {
					uint32_t iid = 0;
					struct index *pJ =
						pTabItem->space->index[iid];
					assert(wctrlFlags &
					       WHERE_ONEPASS_DESIRED);
					while (pJ->def->iid != idx_def->iid) {
						iIndexCur++;
						iid++;
						pJ = pTabItem->space->index[iid];
					}
				} else {
					for(uint32_t i = 0;
					    i < space->index_count; ++i) {
						if (space->index[i]->def ==
						    idx_def) {
							iIndexCur = iAuxArg + i;
							break;
						}
					}
				}
				assert(wctrlFlags & WHERE_ONEPASS_DESIRED);
				pWInfo->aiCurOnePass[1] = iIndexCur;
			} else if (iAuxArg
				   && (wctrlFlags & WHERE_OR_SUBCLAUSE) != 0) {
				iIndexCur = iAuxArg;
				op = OP_IteratorReopen;
			} else {
				iIndexCur = pParse->nTab++;
			}
			pLevel->iIdxCur = iIndexCur;
			assert(iIndexCur >= 0);
			if (op) {
				uint32_t space_id = idx_def->space_id;
				struct space *space = space_by_id(space_id);
				vdbe_emit_open_cursor(pParse, iIndexCur,
						      idx_def->iid, space);
				if ((pLoop->wsFlags & WHERE_CONSTRAINT) != 0
				    && (pLoop->
					wsFlags & (WHERE_COLUMN_RANGE |
						   WHERE_SKIPSCAN)) == 0
				    && (pWInfo->
					wctrlFlags & WHERE_ORDERBY_MIN) == 0) {
					sqlVdbeChangeP5(v, OPFLAG_SEEKEQ);	/* Hint to COMDB2 */
				}
				VdbeComment((v, "%s", idx_def->name));
			}
		}
	}
	pWInfo->iTop = sqlVdbeCurrentAddr(v);
	if (db->mallocFailed)
		goto whereBeginError;

	/* Generate the code to do the search.  Each iteration of the for
	 * loop below generates code for a single nested loop of the VM
	 * program.
	 */
	notReady = ~(Bitmask) 0;
	for (ii = 0; ii < nTabList; ii++) {
		pLevel = &pWInfo->a[ii];
		if ((pLevel->pWLoop->wsFlags & WHERE_AUTO_INDEX) != 0) {
			constructAutomaticIndex(pParse, &pWInfo->sWC,
						&pTabList->a[pLevel->iFrom],
						notReady, pLevel);
			if (db->mallocFailed)
				goto whereBeginError;
		}
		sqlWhereExplainOneScan(pParse, pTabList, pLevel, ii,
					       pLevel->iFrom, wctrlFlags);
		pLevel->addrBody = sqlVdbeCurrentAddr(v);
		notReady = sqlWhereCodeOneLoopStart(pWInfo, ii, notReady);
		pWInfo->iContinue = pLevel->addrCont;
	}

	/* Done. */
	VdbeModuleComment((v, "Begin WHERE-core"));
	return pWInfo;

	/* Jump here if malloc fails */
 whereBeginError:
	if (pWInfo) {
		pParse->nQueryLoop = pWInfo->savedNQueryLoop;
		whereInfoFree(db, pWInfo);
	}
	return 0;
}

/*
 * Generate the end of the WHERE loop.  See comments on
 * sqlWhereBegin() for additional information.
 */
void
sqlWhereEnd(WhereInfo * pWInfo)
{
	Parse *pParse = pWInfo->pParse;
	Vdbe *v = pParse->pVdbe;
	int i;
	WhereLevel *pLevel;
	WhereLoop *pLoop;
	SrcList *pTabList = pWInfo->pTabList;
	sql *db = pParse->db;

	/* Generate loop termination code.
	 */
	VdbeModuleComment((v, "End WHERE-core"));
	sqlExprCacheClear(pParse);
	for (i = pWInfo->nLevel - 1; i >= 0; i--) {
		int addr;
		pLevel = &pWInfo->a[i];
		pLoop = pLevel->pWLoop;
		sqlVdbeResolveLabel(v, pLevel->addrCont);
		if (pLevel->op != OP_Noop) {
			sqlVdbeAddOp3(v, pLevel->op, pLevel->p1, pLevel->p2,
					  pLevel->p3);
			sqlVdbeChangeP5(v, pLevel->p5);
			VdbeCoverage(v);
			VdbeCoverageIf(v, pLevel->op == OP_Next);
			VdbeCoverageIf(v, pLevel->op == OP_Prev);
		}
		if (pLoop->wsFlags & WHERE_IN_ABLE && pLevel->u.in.nIn > 0) {
			struct InLoop *pIn;
			int j;
			sqlVdbeResolveLabel(v, pLevel->addrNxt);
			for (j = pLevel->u.in.nIn, pIn =
			     &pLevel->u.in.aInLoop[j - 1]; j > 0; j--, pIn--) {
				sqlVdbeJumpHere(v, pIn->addrInTop + 1);
				if (pIn->eEndLoopOp != OP_Noop) {
					sqlVdbeAddOp2(v, pIn->eEndLoopOp,
							  pIn->iCur,
							  pIn->addrInTop);
					VdbeCoverage(v);
					VdbeCoverageIf(v, pIn->eEndLoopOp == OP_PrevIfOpen);
					VdbeCoverageIf(v, pIn->eEndLoopOp == OP_NextIfOpen);
				}
				sqlVdbeJumpHere(v, pIn->addrInTop - 1);
			}
		}
		sqlVdbeResolveLabel(v, pLevel->addrBrk);
		if (pLevel->addrSkip) {
			sqlVdbeGoto(v, pLevel->addrSkip);
			VdbeComment((v, "next skip-scan on %s",
				     pLoop->index_def->name));
			sqlVdbeJumpHere(v, pLevel->addrSkip);
			sqlVdbeJumpHere(v, pLevel->addrSkip - 2);
		}
		if (pLevel->iLeftJoin) {
			int ws = pLoop->wsFlags;
			addr =
			    sqlVdbeAddOp1(v, OP_IfPos, pLevel->iLeftJoin);
			VdbeCoverage(v);
			assert((ws & WHERE_IDX_ONLY) == 0
			       || (ws & WHERE_INDEXED) != 0);
			if ((ws & WHERE_IDX_ONLY) == 0) {
				sqlVdbeAddOp1(v, OP_NullRow,
						  pTabList->a[i].iCursor);
			}
			if ((ws & WHERE_INDEXED)
			    || ((ws & WHERE_MULTI_OR) && pLevel->u.pCovidx)
			    ) {
				sqlVdbeAddOp1(v, OP_NullRow,
						  pLevel->iIdxCur);
			}
			if (pLevel->op == OP_Return) {
				sqlVdbeAddOp2(v, OP_Gosub, pLevel->p1,
						  pLevel->addrFirst);
			} else {
				sqlVdbeGoto(v, pLevel->addrFirst);
			}
			sqlVdbeJumpHere(v, addr);
		}
		VdbeModuleComment((v, "End WHERE-loop%d: %s", i,
				   pWInfo->pTabList->a[pLevel->iFrom].space->
				   def->name));
	}

	/* The "break" point is here, just past the end of the outer loop.
	 * Set it.
	 */
	sqlVdbeResolveLabel(v, pWInfo->iBreak);

	assert(pWInfo->nLevel <= pTabList->nSrc);
	for (i = 0, pLevel = pWInfo->a; i < pWInfo->nLevel; i++, pLevel++) {
		int k, last;
		VdbeOp *pOp;
		struct SrcList_item *pTabItem = &pTabList->a[pLevel->iFrom];
		assert(pTabItem->space != NULL);
		pLoop = pLevel->pWLoop;

		/* For a co-routine, change all OP_Column references to the table of
		 * the co-routine into OP_Copy of result contained in a register.
		 */
		if (pTabItem->fg.viaCoroutine && !db->mallocFailed) {
			translateColumnToCopy(v, pLevel->addrBody,
					      pLevel->iTabCur,
					      pTabItem->regResult);
			continue;
		}

		/* If this scan uses an index, make VDBE code substitutions to read data
		 * from the index instead of from the table where possible.  In some cases
		 * this optimization prevents the table from ever being read, which can
		 * yield a significant performance boost.
		 *
		 * Calls to the code generator in between sqlWhereBegin and
		 * sqlWhereEnd will have created code that references the table
		 * directly.  This loop scans all that code looking for opcodes
		 * that reference the table and converts them into opcodes that
		 * reference the index.
		 */
		struct index_def *def = NULL;
		if (pLoop->wsFlags & (WHERE_INDEXED | WHERE_IDX_ONLY)) {
			def = pLoop->index_def;
		} else if (pLoop->wsFlags & WHERE_MULTI_OR) {
			def = pLevel->u.pCovidx;
		}
		if (def != NULL && !db->mallocFailed) {
			last = sqlVdbeCurrentAddr(v);
			k = pLevel->addrBody;
			pOp = sqlVdbeGetOp(v, k);
			for (; k < last; k++, pOp++) {
				if (pOp->p1 != pLevel->iTabCur)
					continue;
				if (pOp->opcode != OP_Column)
					continue;
				assert(def == NULL || def->space_id ==
						      pTabItem->space->def->id);
				int x = pOp->p2;
				assert(x >= 0);
				pOp->p1 = pLevel->iIdxCur;
				if ((pLoop->wsFlags & WHERE_AUTO_INDEX) == 0) {
					pOp->p2 = x;
					continue;
				}
				/*
				 * In case we are using ephemeral index, the
				 * space that will be used to get the values
				 * will be ephemeral index. Since the opcode
				 * OP_Column uses the position of the fields
				 * according to the original space, and the
				 * fields may be in other positions in the
				 * ephemeral index, we must correct the P2 of
				 * OP_Column. To get the positions of these
				 * fields in ephemeral index, we use the index
				 * definition we created.
				 */
				struct key_def *key_def =
					pLevel->pWLoop->index_def->key_def;
				uint32_t part_count = key_def->part_count;
				for (uint32_t i = 0; i < part_count; ++i) {
					if ((int)key_def->parts[i].fieldno == x)
						pOp->p2 = i;
				}
			}
		}
	}

	/* Final cleanup
	 */
	pParse->nQueryLoop = pWInfo->savedNQueryLoop;
	whereInfoFree(db, pWInfo);
	return;
}
