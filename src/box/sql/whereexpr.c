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
 * the WHERE clause of SQL statements.
 *
 * This file was originally part of where.c but was split out to improve
 * readability and editabiliity.  This file contains utility routines for
 * analyzing Expr objects in the WHERE clause.
 */
#include "box/coll_id_cache.h"
#include "coll/coll.h"
#include "sqlInt.h"
#include "mem.h"
#include "whereInt.h"

/* Forward declarations */
static void exprAnalyze(SrcList *, WhereClause *, int);

/*
 * Deallocate all memory associated with a WhereOrInfo object.
 */
static void
whereOrInfoDelete(struct WhereOrInfo *p)
{
	sqlWhereClauseClear(&p->wc);
	sql_xfree(p);
}

/*
 * Deallocate all memory associated with a WhereAndInfo object.
 */
static void
whereAndInfoDelete(struct WhereAndInfo *p)
{
	sqlWhereClauseClear(&p->wc);
	sql_xfree(p);
}

/*
 * Add a single new WhereTerm entry to the WhereClause object pWC.
 * The new WhereTerm object is constructed from Expr p and with wtFlags.
 * The index in pWC->a[] of the new WhereTerm is returned.
 *
 * This routine will increase the size of the pWC->a[] array as necessary.
 *
 * If the wtFlags argument includes TERM_DYNAMIC, then responsibility
 * for freeing the expression p is assumed by the WhereClause object pWC.
 * This is true even if this routine fails to allocate a new WhereTerm.
 *
 * WARNING:  This routine might reallocate the space used to store
 * WhereTerms.  All pointers to WhereTerms should be invalidated after
 * calling this routine.  Such pointers may be reinitialized by referencing
 * the pWC->a[] array.
 */
static int
whereClauseInsert(WhereClause * pWC, Expr * p, u16 wtFlags)
{
	WhereTerm *pTerm;
	int idx;
	if (pWC->nTerm >= pWC->nSlot) {
		WhereTerm *pOld = pWC->a;
		size_t size = sizeof(pWC->a[0]) * pWC->nSlot * 2;
		pWC->a = sql_xmalloc(size);
		memcpy(pWC->a, pOld, sizeof(pWC->a[0]) * pWC->nTerm);
		if (pOld != pWC->aStatic) {
			sql_xfree(pOld);
		}
		assert(size / sizeof(pWC->a[0]) == (size_t)pWC->nSlot * 2);
		pWC->nSlot *= 2;
	}
	pTerm = &pWC->a[idx = pWC->nTerm++];
	if (p && ExprHasProperty(p, EP_Unlikely)) {
		pTerm->truthProb = sqlLogEst(p->iTable) - 270;
	} else {
		pTerm->truthProb = 1;
	}
	pTerm->pExpr = sqlExprSkipCollate(p);
	pTerm->wtFlags = wtFlags;
	pTerm->pWC = pWC;
	pTerm->iParent = -1;
	memset(&pTerm->eOperator, 0,
	       sizeof(WhereTerm) - offsetof(WhereTerm, eOperator));
	return idx;
}

/*
 * Return TRUE if the given operator is one of the operators that is
 * allowed for an indexable WHERE clause term.  The allowed operators are
 * "=", "<", ">", "<=", ">=", "IN", "IS", and "IS NULL"
 */
static int
allowedOp(int op)
{
	assert(TK_GT > TK_EQ && TK_GT < TK_GE);
	assert(TK_LT > TK_EQ && TK_LT < TK_GE);
	assert(TK_LE > TK_EQ && TK_LE < TK_GE);
	assert(TK_GE == TK_EQ + 4);
	return op == TK_IN || (op >= TK_EQ && op <= TK_GE) || op == TK_ISNULL;
}

/*
 * Commute a comparison operator.  Expressions of the form "X op Y"
 * are converted into "Y op X".
 *
 * If left/right precedence rules come into play when determining the
 * collating sequence, then COLLATE operators are adjusted to ensure
 * that the collating sequence does not change.  For example:
 * "Y collate NOCASE op X" becomes "X op Y" because any collation sequence on
 * the left hand side of a comparison overrides any collation sequence
 * attached to the right. For the same reason the EP_Collate flag
 * is not commuted.
 */
static void
exprCommute(Parse * pParse, Expr * pExpr)
{
	u16 expRight = (pExpr->pRight->flags & EP_Collate);
	u16 expLeft = (pExpr->pLeft->flags & EP_Collate);
	assert(allowedOp(pExpr->op) && pExpr->op != TK_IN);
	if (expRight == expLeft) {
		/* Either X and Y both have COLLATE operator or neither do */
		if (expRight) {
			/* Both X and Y have COLLATE operators.  Make sure X is always
			 * used by clearing the EP_Collate flag from Y.
			 */
			pExpr->pRight->flags &= ~EP_Collate;
		} else {
			bool is_found;
			uint32_t id;
			struct coll *unused;
			if (sql_expr_coll(pParse, pExpr->pLeft, &is_found, &id,
					  &unused) != 0)
				return;
			if (id != COLL_NONE) {
				/*
				 * Neither X nor Y have COLLATE
				 * operators, but X has a
				 * non-default collating sequence.
				 * So add the EP_Collate marker on
				 * X to cause it to be searched
				 * first.
				 */
				pExpr->pLeft->flags |= EP_Collate;
			}
		}
	}
	SWAP(pExpr->pRight, pExpr->pLeft);
	if (pExpr->op >= TK_GT) {
		assert(TK_LT == TK_GT + 2);
		assert(TK_GE == TK_LE + 2);
		assert(TK_GT > TK_EQ);
		assert(TK_GT < TK_LE);
		assert(pExpr->op >= TK_GT && pExpr->op <= TK_GE);
		pExpr->op = ((pExpr->op - TK_GT) ^ 2) + TK_GT;
	}
}

/*
 * Translate from TK_xx operator to WO_xx bitmask.
 */
static u16
operatorMask(int op)
{
	u16 c;
	assert(allowedOp(op));
	if (op == TK_IN) {
		c = WO_IN;
	} else if (op == TK_ISNULL) {
		c = WO_ISNULL;
	} else {
		assert((WO_EQ << (op - TK_EQ)) < 0x7fff);
		c = (u16) (WO_EQ << (op - TK_EQ));
	}
	assert(op != TK_ISNULL || c == WO_ISNULL);
	assert(op != TK_IN || c == WO_IN);
	assert(op != TK_EQ || c == WO_EQ);
	assert(op != TK_LT || c == WO_LT);
	assert(op != TK_LE || c == WO_LE);
	assert(op != TK_GT || c == WO_GT);
	assert(op != TK_GE || c == WO_GE);
	return c;
}

/**
 * Check to see if the given expression is a LIKE operator that
 * can be optimized using inequality constraints.
 *
 * In order for the operator to be optimizible, the RHS must be a
 * string literal that does not begin with a wildcard. The LHS
 * must be a column that may only be NULL, a string, or a BLOB,
 * never a number.
 *
 * FIXME: this optimization is currently available only for
 * "binary" and "unicode_ci" collations. Reason for that is
 * determining the next symbol to be replaced in pattern.
 * Consider example: a LIKE %A; (collation is binary). After
 * optimization is applied, LIKE is transformed to a >= 'A' AND
 * a < 'B' since 'B' is the smallest string that is the same
 * length as 'A' but which compares greater than 'A'. In this case
 * 'B' can be found as a literally next character to 'A'
 * ('B' == 'A' + 1) However, if custom collation is used (e.g.
 * "unicode" featuring tertiary strength) next symbol (i.e. upper
 * bound) should be found using weight table that is not such
 * trivial case.
 *
 * @param pParse      Parsing and code generating context.
 * @param pExpr       Test this expression.
 * @param ppPrefix    Pointer to expression with pattern prefix.
 * @param pisComplete True if the only wildcard is '%' in the
 *                    last character.
 * @retval True if the given expr is a LIKE operator & is
 *         optimizable using inequality constraints.
 */
static int
like_optimization_is_valid(Parse *pParse, Expr *pExpr, Expr **ppPrefix,
			   int *pisComplete)
{
	/* String on RHS of LIKE operator. */
	const char *z = 0;
	/* Right and left size of LIKE operator. */
	Expr *pRight, *pLeft;
	/* List of operands to the LIKE operator. */
	ExprList *pList;
	/* One character in z[]. */
	int c;
	/* Number of non-wildcard prefix characters. */
	int cnt;
	/* Opcode of pRight. */
	int op;
	/* Result code to return. */
	int rc;

	if (!sql_is_like_func(pExpr)) {
		return 0;
	}
	pList = pExpr->x.pList;
	pLeft = pList->a[1].pExpr;
	/* Value might be numeric */
	if (pLeft->op != TK_COLUMN_REF ||
	    sql_expr_type(pLeft) != FIELD_TYPE_STRING) {
		/* IMP: R-02065-49465 The left-hand side of the
		 * LIKE operator must be the name of an indexed
		 * column with STRING type.
		 */
		return 0;
	}
	assert(pLeft->iColumn != (-1));	/* Because IPK never has AFF_TEXT */

	pRight = pList->a[0].pExpr;

	/*
	 * Only for "binary" and "unicode_ci" collations. If
	 * explicit collation is specified and doesn't match with
	 * implicit - index search won't be used. Ergo, we fail
	 * get so far.
	 */
	struct field_def *fd = &pLeft->space_def->fields[pLeft->iColumn];
	uint32_t u_ci_id = coll_by_name("unicode_ci", strlen("unicode_ci"))->id;
	uint32_t bin_id = coll_by_name("binary", strlen("binary"))->id;
	if (fd->coll_id != COLL_NONE && fd->coll_id != u_ci_id &&
	    fd->coll_id != bin_id)
		return 0;

	op = pRight->op;
	struct region *region = &pParse->region;
	size_t svp = region_used(region);
	if (op == TK_VARIABLE) {
		Vdbe *pReprepare = pParse->pReprepare;
		int iCol = pRight->iColumn;
		const struct sql_mem *var = vdbe_get_bound_value(pReprepare,
								 iCol);
		if (var != NULL && mem_is_str(var)) {
			uint32_t size = var->u.n + 1;
			char *str = region_alloc(region, size);
			if (str == NULL) {
				diag_set(OutOfMemory, size, "region", "str");
				return -1;
			}
			memcpy(str, var->u.z, var->u.n);
			str[var->u.n] = '\0';
			z = str;
		}
		assert(pRight->op == TK_VARIABLE || pRight->op == TK_REGISTER);
	} else if (op == TK_STRING) {
		z = pRight->u.zToken;
	}
	if (z) {
		cnt = 0;
		while ((c = z[cnt]) != 0 && c != MATCH_ONE_WILDCARD &&
		       c != MATCH_ALL_WILDCARD)
			cnt++;
		if (cnt != 0 && 255 != (u8) z[cnt - 1]) {
			Expr *pPrefix;
			*pisComplete = c == MATCH_ALL_WILDCARD &&
				       z[cnt + 1] == 0;
			pPrefix = sql_expr_new_named(TK_STRING, z);
			pPrefix->u.zToken[cnt] = 0;
			*ppPrefix = pPrefix;
			if (op == TK_VARIABLE) {
				Vdbe *v = pParse->pVdbe;
				if (*pisComplete && pRight->u.zToken[1]) {
					/* If the rhs of the LIKE expression is a variable, and the current
					 * value of the variable means there is no need to invoke the LIKE
					 * function, then no OP_Variable will be added to the program.
					 * This causes problems for the sql_bind_parameter_name()
					 * API. To work around them, add a dummy OP_Variable here.
					 */
					int r1 = sqlGetTempReg(pParse);
					sqlExprCodeTarget(pParse, pRight,
							      r1);
					sqlVdbeChangeP3(v,
							    sqlVdbeCurrentAddr
							    (v) - 1, 0);
					sqlReleaseTempReg(pParse, r1);
				}
			}
		} else {
			z = 0;
		}
	}

	region_truncate(region, svp);
	rc = (z != 0);
	return rc;
}

/*
 * If the pBase expression originated in the ON or USING clause of
 * a join, then transfer the appropriate markings over to derived.
 */
static void
transferJoinMarkings(Expr * pDerived, Expr * pBase)
{
	if (pDerived) {
		pDerived->flags |= pBase->flags & EP_FromJoin;
		pDerived->iRightJoinTable = pBase->iRightJoinTable;
	}
}

/*
 * Mark term iChild as being a child of term iParent
 */
static void
markTermAsChild(WhereClause * pWC, int iChild, int iParent)
{
	pWC->a[iChild].iParent = iParent;
	pWC->a[iChild].truthProb = pWC->a[iParent].truthProb;
	pWC->a[iParent].nChild++;
}

/*
 * Return the N-th AND-connected subterm of pTerm.  Or if pTerm is not
 * a conjunction, then return just pTerm when N==0.  If N is exceeds
 * the number of available subterms, return NULL.
 */
static WhereTerm *
whereNthSubterm(WhereTerm * pTerm, int N)
{
	if (pTerm->eOperator != WO_AND) {
		return N == 0 ? pTerm : 0;
	}
	if (N < pTerm->u.pAndInfo->wc.nTerm) {
		return &pTerm->u.pAndInfo->wc.a[N];
	}
	return 0;
}

/*
 * Subterms pOne and pTwo are contained within WHERE clause pWC.  The
 * two subterms are in disjunction - they are OR-ed together.
 *
 * If these two terms are both of the form:  "A op B" with the same
 * A and B values but different operators and if the operators are
 * compatible (if one is = and the other is <, for example) then
 * add a new virtual AND term to pWC that is the combination of the
 * two.
 *
 * Some examples:
 *
 *    x<y OR x=y    -->     x<=y
 *    x=y OR x=y    -->     x=y
 *    x<=y OR x<y   -->     x<=y
 *
 * The following is NOT generated:
 *
 *    x<y OR x>y    -->     x!=y
 */
static void
whereCombineDisjuncts(SrcList * pSrc,	/* the FROM clause */
		      WhereClause * pWC,	/* The complete WHERE clause */
		      WhereTerm * pOne,	/* First disjunct */
		      WhereTerm * pTwo	/* Second disjunct */
    )
{
	u16 eOp = pOne->eOperator | pTwo->eOperator;
	Expr *pNew;		/* New virtual expression */
	int op;			/* Operator for the combined expression */
	int idxNew;		/* Index in pWC of the next virtual term */

	if ((pOne->eOperator & (WO_EQ | WO_LT | WO_LE | WO_GT | WO_GE)) == 0)
		return;
	if ((pTwo->eOperator & (WO_EQ | WO_LT | WO_LE | WO_GT | WO_GE)) == 0)
		return;
	if ((eOp & (WO_EQ | WO_LT | WO_LE)) != eOp
	    && (eOp & (WO_EQ | WO_GT | WO_GE)) != eOp)
		return;
	assert(pOne->pExpr->pLeft != 0 && pOne->pExpr->pRight != 0);
	assert(pTwo->pExpr->pLeft != 0 && pTwo->pExpr->pRight != 0);
	if (sqlExprCompare(pOne->pExpr->pLeft, pTwo->pExpr->pLeft, -1))
		return;
	if (sqlExprCompare(pOne->pExpr->pRight, pTwo->pExpr->pRight, -1))
		return;
	/* If we reach this point, it means the two subterms can be combined */
	if ((eOp & (eOp - 1)) != 0) {
		if (eOp & (WO_LT | WO_LE)) {
			eOp = WO_LE;
		} else {
			assert(eOp & (WO_GT | WO_GE));
			eOp = WO_GE;
		}
	}
	pNew = sqlExprDup(pOne->pExpr, 0);
	if (pNew == 0)
		return;
	for (op = TK_EQ; eOp != (WO_EQ << (op - TK_EQ)); op++) {
		assert(op < TK_GE);
	}
	pNew->op = op;
	idxNew = whereClauseInsert(pWC, pNew, TERM_VIRTUAL | TERM_DYNAMIC);
	exprAnalyze(pSrc, pWC, idxNew);
}

/*
 * Analyze a term that consists of two or more OR-connected
 * subterms.  So in:
 *
 *     ... WHERE  (a=5) AND (b=7 OR c=9 OR d=13) AND (d=13)
 *                          ^^^^^^^^^^^^^^^^^^^^
 *
 * This routine analyzes terms such as the middle term in the above example.
 * A WhereOrTerm object is computed and attached to the term under
 * analysis, regardless of the outcome of the analysis.  Hence:
 *
 *     WhereTerm.wtFlags   |=  TERM_ORINFO
 *     WhereTerm.u.pOrInfo  =  a dynamically allocated WhereOrTerm object
 *
 * The term being analyzed must have two or more of OR-connected subterms.
 * A single subterm might be a set of AND-connected sub-subterms.
 * Examples of terms under analysis:
 *
 *     (A)     t1.x=t2.y OR t1.x=t2.z OR t1.y=15 OR t1.z=t3.a+5
 *     (B)     x=expr1 OR expr2=x OR x=expr3
 *     (C)     t1.x=t2.y OR (t1.x=t2.z AND t1.y=15)
 *     (D)     x=expr1 OR (y>11 AND y<22 AND z LIKE '*hello*')
 *     (E)     (p.a=1 AND q.b=2 AND r.c=3) OR (p.x=4 AND q.y=5 AND r.z=6)
 *     (F)     x>A OR (x=A AND y>=B)
 *
 * CASE 1:
 *
 * If all subterms are of the form T.C=expr for some single column of C and
 * a single table T (as shown in example B above) then create a new virtual
 * term that is an equivalent IN expression.  In other words, if the term
 * being analyzed is:
 *
 *      x = expr1  OR  expr2 = x  OR  x = expr3
 *
 * then create a new virtual term like this:
 *
 *      x IN (expr1,expr2,expr3)
 *
 * CASE 2:
 *
 * If there are exactly two disjuncts and one side has x>A and the other side
 * has x=A (for the same x and A) then add a new virtual conjunct term to the
 * WHERE clause of the form "x>=A".  Example:
 *
 *      x>A OR (x=A AND y>B)    adds:    x>=A
 *
 * The added conjunct can sometimes be helpful in query planning.
 *
 * CASE 3:
 *
 * If all subterms are indexable by a single table T, then set
 *
 *     WhereTerm.eOperator              =  WO_OR
 *     WhereTerm.u.pOrInfo->indexable  |=  the cursor number for table T
 *
 * A subterm is "indexable" if it is of the form
 * "T.C <op> <expr>" where C is any column of table T and
 * <op> is one of "=", "<", "<=", ">", ">=", "IS NULL", or "IN".
 * A subterm is also indexable if it is an AND of two or more
 * subsubterms at least one of which is indexable.  Indexable AND
 * subterms have their eOperator set to WO_AND and they have
 * u.pAndInfo set to a dynamically allocated WhereAndTerm object.
 *
 * From another point of view, "indexable" means that the subterm could
 * potentially be used with an index if an appropriate index exists.
 * This analysis does not consider whether or not the index exists; that
 * is decided elsewhere.  This analysis only looks at whether subterms
 * appropriate for indexing exist.
 *
 * All examples A through E above satisfy case 3.  But if a term
 * also satisfies case 1 (such as B) we know that the optimizer will
 * always prefer case 1, so in that case we pretend that case 3 is not
 * satisfied.
 *
 * It might be the case that multiple tables are indexable.  For example,
 * (E) above is indexable on tables P, Q, and R.
 *
 * OTHERWISE:
 *
 * If none of cases 1, 2, or 3 apply, then leave the eOperator set to
 * zero.  This term is not useful for search.
 */
static void
exprAnalyzeOrTerm(SrcList * pSrc,	/* the FROM clause */
		  WhereClause * pWC,	/* the complete WHERE clause */
		  int idxTerm	/* Index of the OR-term to be analyzed */
    )
{
	WhereInfo *pWInfo = pWC->pWInfo;	/* WHERE clause processing context */
	Parse *pParse = pWInfo->pParse;	/* Parser context */
	WhereTerm *pTerm = &pWC->a[idxTerm];	/* The term to be analyzed */
	Expr *pExpr = pTerm->pExpr;	/* The expression of the term */
	int i;			/* Loop counters */
	WhereClause *pOrWc;	/* Breakup of pTerm into subterms */
	WhereTerm *pOrTerm;	/* A Sub-term within the pOrWc */
	WhereOrInfo *pOrInfo;	/* Additional information associated with pTerm */
	Bitmask chngToIN;	/* Tables that might satisfy case 1 */
	Bitmask indexable;	/* Tables that are indexable, satisfying case 2 */

	/*
	 * Break the OR clause into its separate subterms.  The subterms are
	 * stored in a WhereClause structure containing within the WhereOrInfo
	 * object that is attached to the original OR clause term.
	 */
	assert((pTerm->wtFlags & (TERM_DYNAMIC | TERM_ORINFO | TERM_ANDINFO)) ==
	       0);
	assert(pExpr->op == TK_OR);
	pOrInfo = sql_xmalloc0(sizeof(*pOrInfo));
	pTerm->u.pOrInfo = pOrInfo;
	pTerm->wtFlags |= TERM_ORINFO;
	pOrWc = &pOrInfo->wc;
	memset(pOrWc->aStatic, 0, sizeof(pOrWc->aStatic));
	sqlWhereClauseInit(pOrWc, pWInfo);
	sqlWhereSplit(pOrWc, pExpr, TK_OR);
	sqlWhereExprAnalyze(pSrc, pOrWc);
	assert(pOrWc->nTerm >= 2);

	/*
	 * Compute the set of tables that might satisfy cases 1 or 3.
	 */
	indexable = ~(Bitmask) 0;
	chngToIN = ~(Bitmask) 0;
	for (i = pOrWc->nTerm - 1, pOrTerm = pOrWc->a; i >= 0 && indexable;
	     i--, pOrTerm++) {
		if ((pOrTerm->eOperator & WO_SINGLE) == 0) {
			WhereAndInfo *pAndInfo;
			assert((pOrTerm->
				wtFlags & (TERM_ANDINFO | TERM_ORINFO)) == 0);
			chngToIN = 0;
			pAndInfo = sql_xmalloc(sizeof(*pAndInfo));
			struct WhereClause *pAndWC;
			struct WhereTerm *pAndTerm;
			int j;
			Bitmask b = 0;
			pOrTerm->u.pAndInfo = pAndInfo;
			pOrTerm->wtFlags |= TERM_ANDINFO;
			pOrTerm->eOperator = WO_AND;
			pAndWC = &pAndInfo->wc;
			memset(pAndWC->aStatic, 0,
			       sizeof(pAndWC->aStatic));
			sqlWhereClauseInit(pAndWC, pWC->pWInfo);
			sqlWhereSplit(pAndWC, pOrTerm->pExpr, TK_AND);
			sqlWhereExprAnalyze(pSrc, pAndWC);
			pAndWC->pOuter = pWC;
			for (j = 0, pAndTerm = pAndWC->a; j < pAndWC->nTerm;
			     j++, pAndTerm++) {
				assert(pAndTerm->pExpr != NULL);
				if (allowedOp(pAndTerm->pExpr->op) ||
				    pAndTerm->eOperator == WO_MATCH) {
					b |= sqlWhereGetMask(&pWInfo->sMaskSet,
						pAndTerm->leftCursor);
				}
			}
			indexable &= b;
		} else if (pOrTerm->wtFlags & TERM_COPIED) {
			/* Skip this term for now.  We revisit it when we process the
			 * corresponding TERM_VIRTUAL term
			 */
		} else {
			Bitmask b;
			b = sqlWhereGetMask(&pWInfo->sMaskSet,
						pOrTerm->leftCursor);
			if (pOrTerm->wtFlags & TERM_VIRTUAL) {
				WhereTerm *pOther = &pOrWc->a[pOrTerm->iParent];
				b |= sqlWhereGetMask(&pWInfo->sMaskSet,
							 pOther->leftCursor);
			}
			indexable &= b;
			if ((pOrTerm->eOperator & WO_EQ) == 0) {
				chngToIN = 0;
			} else {
				chngToIN &= b;
			}
		}
	}

	/*
	 * Record the set of tables that satisfy case 3.  The set might be
	 * empty.
	 */
	pOrInfo->indexable = indexable;
	pTerm->eOperator = indexable == 0 ? 0 : WO_OR;

	/* For a two-way OR, attempt to implementation case 2.
	 */
	if (indexable && pOrWc->nTerm == 2) {
		int iOne = 0;
		WhereTerm *pOne;
		while ((pOne = whereNthSubterm(&pOrWc->a[0], iOne++)) != 0) {
			int iTwo = 0;
			WhereTerm *pTwo;
			while ((pTwo =
				whereNthSubterm(&pOrWc->a[1], iTwo++)) != 0) {
				whereCombineDisjuncts(pSrc, pWC, pOne, pTwo);
			}
		}
	}

	/*
	 * chngToIN holds a set of tables that *might* satisfy case 1.  But
	 * we have to do some additional checking to see if case 1 really
	 * is satisfied.
	 *
	 * chngToIN will hold either 0, 1, or 2 bits.  The 0-bit case means
	 * that there is no possibility of transforming the OR clause into an
	 * IN operator because one or more terms in the OR clause contain
	 * something other than == on a column in the single table.  The 1-bit
	 * case means that every term of the OR clause is of the form
	 * "table.column=expr" for some single table.  The one bit that is set
	 * will correspond to the common table.  We still need to check to make
	 * sure the same column is used on all terms.  The 2-bit case is when
	 * the all terms are of the form "table1.column=table2.column".  It
	 * might be possible to form an IN operator with either table1.column
	 * or table2.column as the LHS if either is common to every term of
	 * the OR clause.
	 *
	 * Note that terms of the form "table.column1=table.column2" (the
	 * same table on both sizes of the ==) cannot be optimized.
	 */
	if (chngToIN) {
		int okToChngToIN = 0;	/* True if the conversion to IN is valid */
		int iColumn = -1;	/* Column index on lhs of IN operator */
		int iCursor = -1;	/* Table cursor common to all terms */
		int j = 0;	/* Loop counter */

		/* Search for a table and column that appears on one side or the
		 * other of the == operator in every subterm.  That table and column
		 * will be recorded in iCursor and iColumn.  There might not be any
		 * such table and column.  Set okToChngToIN if an appropriate table
		 * and column is found but leave okToChngToIN false if not found.
		 */
		for (j = 0; j < 2 && !okToChngToIN; j++) {
			pOrTerm = pOrWc->a;
			for (i = pOrWc->nTerm - 1; i >= 0; i--, pOrTerm++) {
				assert(pOrTerm->eOperator & WO_EQ);
				pOrTerm->wtFlags &= ~TERM_OR_OK;
				if (pOrTerm->leftCursor == iCursor) {
					/* This is the 2-bit case and we are on the second iteration and
					 * current term is from the first iteration.  So skip this term.
					 */
					assert(j == 1);
					continue;
				}
				if ((chngToIN &
				     sqlWhereGetMask(&pWInfo->sMaskSet,
							 pOrTerm->
							 leftCursor)) == 0) {
					/* This term must be of the form t1.a==t2.b where t2 is in the
					 * chngToIN set but t1 is not.  This term will be either preceded
					 * or follwed by an inverted copy (t2.b==t1.a).  Skip this term
					 * and use its inversion.
					 */
					assert(pOrTerm->
					       wtFlags & (TERM_COPIED |
							  TERM_VIRTUAL));
					continue;
				}
				iColumn = pOrTerm->u.leftColumn;
				iCursor = pOrTerm->leftCursor;
				break;
			}
			if (i < 0) {
				/* No candidate table+column was found.  This can only occur
				 * on the second iteration
				 */
				assert(j == 1);
				assert(IsPowerOfTwo(chngToIN));
				assert(chngToIN ==
				       sqlWhereGetMask(&pWInfo->sMaskSet,
							   iCursor));
				break;
			}

			/* We have found a candidate table and column.  Check to see if that
			 * table and column is common to every term in the OR clause
			 */
			okToChngToIN = 1;
			for (; i >= 0 && okToChngToIN; i--, pOrTerm++) {
				assert(pOrTerm->eOperator & WO_EQ);
				if (pOrTerm->leftCursor != iCursor) {
					pOrTerm->wtFlags &= ~TERM_OR_OK;
				} else if (pOrTerm->u.leftColumn != iColumn) {
					okToChngToIN = 0;
				} else {
					/* If the right-hand side is also a column, then the types
					 * of both right and left sides must be such that no type
					 * conversions are required on the right.  (Ticket #2249)
					 */
					enum field_type rhs =
						sql_expr_type(pOrTerm->pExpr->
							pRight);
					enum field_type lhs =
						sql_expr_type(pOrTerm->pExpr->
							pLeft);
					if (rhs != FIELD_TYPE_SCALAR &&
					    rhs != lhs) {
						okToChngToIN = 0;
					} else {
						pOrTerm->wtFlags |= TERM_OR_OK;
					}
				}
			}
		}

		/* At this point, okToChngToIN is true if original pTerm satisfies
		 * case 1.  In that case, construct a new virtual term that is
		 * pTerm converted into an IN operator.
		 */
		if (okToChngToIN) {
			Expr *pDup;	/* A transient duplicate expression */
			ExprList *pList = 0;	/* The RHS of the IN operator */
			Expr *pLeft = 0;	/* The LHS of the IN operator */
			Expr *pNew;	/* The complete IN operator */

			for (i = pOrWc->nTerm - 1, pOrTerm = pOrWc->a; i >= 0;
			     i--, pOrTerm++) {
				if ((pOrTerm->wtFlags & TERM_OR_OK) == 0)
					continue;
				assert(pOrTerm->eOperator & WO_EQ);
				assert(pOrTerm->leftCursor == iCursor);
				assert(pOrTerm->u.leftColumn == iColumn);
				pDup = sqlExprDup(pOrTerm->pExpr->pRight, 0);
				pList = sql_expr_list_append(pList, pDup);
				pLeft = pOrTerm->pExpr->pLeft;
			}
			assert(pLeft != 0);
			pDup = sqlExprDup(pLeft, 0);
			pNew = sqlPExpr(pParse, TK_IN, pDup, 0);
			if (pNew) {
				int idxNew;
				transferJoinMarkings(pNew, pExpr);
				assert(!ExprHasProperty(pNew, EP_xIsSelect));
				pNew->x.pList = pList;
				idxNew =
				    whereClauseInsert(pWC, pNew,
						      TERM_VIRTUAL |
						      TERM_DYNAMIC);
				exprAnalyze(pSrc, pWC, idxNew);
				pTerm = &pWC->a[idxTerm];
				markTermAsChild(pWC, idxNew, idxTerm);
			} else {
				sql_expr_list_delete(pList);
			}
			pTerm->eOperator = WO_NOOP;	/* case 1 trumps case 3 */
		}
	}
}

/*
 * We already know that pExpr is a binary operator where both operands are
 * column references.  This routine checks to see if pExpr is an equivalence
 * relation:
 *   1.  The SQL_Transitive optimization must be enabled
 *   2.  Must be either an == or an IS operator
 *   3.  Not originating in the ON clause of an OUTER JOIN
 *   4.  The types of A and B must be compatible
 *   5a. Both operands use the same collating sequence OR
 *   5b. The overall collating sequence is BINARY
 * If this routine returns TRUE, that means that the RHS can be substituted
 * for the LHS anyplace else in the WHERE clause where the LHS column occurs.
 * This is an optimization.  No harm comes from returning 0.  But if 1 is
 * returned when it should not be, then incorrect answers might result.
 */
static int
termIsEquivalence(Parse * pParse, Expr * pExpr)
{
	if (!OptimizationEnabled(SQL_Transitive))
		return 0;
	if (pExpr->op != TK_EQ)
		return 0;
	if (ExprHasProperty(pExpr, EP_FromJoin))
		return 0;
	enum field_type lhs_type = sql_expr_type(pExpr->pLeft);
	enum field_type rhs_type = sql_expr_type(pExpr->pRight);
	if (lhs_type != rhs_type && (!sql_type_is_numeric(lhs_type) ||
				     !sql_type_is_numeric(rhs_type)))
		return 0;
	uint32_t id;
	if (sql_binary_compare_coll_seq(pParse, pExpr->pLeft, pExpr->pRight,
					&id) != 0)
		return 0;
	if (id == COLL_NONE)
		return 1;
	bool unused1;
	uint32_t lhs_id;
	uint32_t rhs_id;
	struct coll *unused;
	if (sql_expr_coll(pParse, pExpr->pLeft, &unused1, &lhs_id,
			  &unused) != 0)
		return 0;
	if (sql_expr_coll(pParse, pExpr->pRight, &unused1, &rhs_id,
			  &unused) != 0)
		return 0;
	return lhs_id != COLL_NONE && lhs_id == rhs_id;
}

/*
 * Recursively walk the expressions of a SELECT statement and generate
 * a bitmask indicating which tables are used in that expression
 * tree.
 */
static Bitmask
exprSelectUsage(WhereMaskSet * pMaskSet, Select * pS)
{
	Bitmask mask = 0;
	while (pS) {
		SrcList *pSrc = pS->pSrc;
		mask |= sqlWhereExprListUsage(pMaskSet, pS->pEList);
		mask |= sqlWhereExprListUsage(pMaskSet, pS->pGroupBy);
		mask |= sqlWhereExprListUsage(pMaskSet, pS->pOrderBy);
		mask |= sqlWhereExprUsage(pMaskSet, pS->pWhere);
		mask |= sqlWhereExprUsage(pMaskSet, pS->pHaving);
		if (ALWAYS(pSrc != 0)) {
			int i;
			for (i = 0; i < pSrc->nSrc; i++) {
				mask |=
				    exprSelectUsage(pMaskSet,
						    pSrc->a[i].pSelect);
				mask |=
				    sqlWhereExprUsage(pMaskSet,
							  pSrc->a[i].pOn);
			}
		}
		pS = pS->pPrior;
	}
	return mask;
}

/*
 * Expression pExpr is one operand of a comparison operator that might
 * be useful for indexing.  This routine checks to see if pExpr appears
 * in any index.  Return TRUE (1) if pExpr is an indexed term and return
 * FALSE (0) if not.  If TRUE is returned, also set *piCur to the cursor
 * number of the table that is indexed and *piColumn to the column number
 * of the column that is indexed.
 *
 * If pExpr is a TK_COLUMN_REF column reference, then this routine always returns
 * true even if that particular column is not indexed, because the column
 * might be added to an automatic index later.
 */
static int
exprMightBeIndexed(int op,	/* The specific comparison operator */
		   Expr * pExpr,	/* An operand of a comparison operator */
		   int *piCur,	/* Write the referenced table cursor number here */
		   int *piColumn	/* Write the referenced table column number here */
    )
{
	/* If this expression is a vector to the left or right of a
	 * inequality constraint (>, <, >= or <=), perform the processing
	 * on the first element of the vector.
	 */
	assert(TK_GT + 1 == TK_LE && TK_GT + 2 == TK_LT && TK_GT + 3 == TK_GE);
	assert(TK_IN < TK_GE);
	assert(op <= TK_GE || op == TK_ISNULL || op == TK_NOTNULL);
	if (pExpr->op == TK_VECTOR && (op >= TK_GT && op <= TK_GE)) {
		pExpr = pExpr->x.pList->a[0].pExpr;
	}

	if (pExpr->op == TK_COLUMN_REF) {
		*piCur = pExpr->iTable;
		*piColumn = pExpr->iColumn;
		return 1;
	}
	return 0;
}

/*
 * The input to this routine is an WhereTerm structure with only the
 * "pExpr" field filled in.  The job of this routine is to analyze the
 * subexpression and populate all the other fields of the WhereTerm
 * structure.
 *
 * If the expression is of the form "<expr> <op> X" it gets commuted
 * to the standard form of "X <op> <expr>".
 *
 * If the expression is of the form "X <op> Y" where both X and Y are
 * columns, then the original expression is unchanged and a new virtual
 * term of the form "Y <op> X" is added to the WHERE clause and
 * analyzed separately.  The original term is marked with TERM_COPIED
 * and the new term is marked with TERM_DYNAMIC (because it's pExpr
 * needs to be freed with the WhereClause) and TERM_VIRTUAL (because it
 * is a commuted copy of a prior term.)  The original term has nChild=1
 * and the copy has idxParent set to the index of the original term.
 */
static void
exprAnalyze(SrcList * pSrc,	/* the FROM clause */
	    WhereClause * pWC,	/* the WHERE clause */
	    int idxTerm		/* Index of the term to be analyzed */
    )
{
	/* WHERE clause processing context. */
	WhereInfo *pWInfo = pWC->pWInfo;
	/* The term to be analyzed. */
	WhereTerm *pTerm;
	/* Set of table index masks. */
	WhereMaskSet *pMaskSet;
	/* The expression to be analyzed. */
	Expr *pExpr;
	/* Prerequesites of the pExpr->pLeft. */
	Bitmask prereqLeft;
	/* Prerequesites of pExpr. */
	Bitmask prereqAll;
	/* Extra dependencies on LEFT JOIN. */
	Bitmask extraRight = 0;
	/* RHS of LIKE operator. */
	Expr *pStr1 = 0;
	/* RHS of LIKE ends with wildcard. */
	int isComplete = 0;
	/* Top-level operator. pExpr->op. */
	int op;
	/* Parsing context. */
	Parse *pParse = pWInfo->pParse;

	pTerm = &pWC->a[idxTerm];
	pMaskSet = &pWInfo->sMaskSet;
	pExpr = pTerm->pExpr;
	assert(pExpr->op != TK_AS && pExpr->op != TK_COLLATE);
	prereqLeft = sqlWhereExprUsage(pMaskSet, pExpr->pLeft);
	op = pExpr->op;
	if (op == TK_IN) {
		assert(pExpr->pRight == 0);
		if (sqlExprCheckIN(pParse, pExpr))
			return;
		if (ExprHasProperty(pExpr, EP_xIsSelect)) {
			pTerm->prereqRight =
			    exprSelectUsage(pMaskSet, pExpr->x.pSelect);
		} else {
			pTerm->prereqRight =
			    sqlWhereExprListUsage(pMaskSet, pExpr->x.pList);
		}
	} else if (op == TK_ISNULL) {
		pTerm->prereqRight = 0;
	} else {
		pTerm->prereqRight =
		    sqlWhereExprUsage(pMaskSet, pExpr->pRight);
	}
	prereqAll = sqlWhereExprUsage(pMaskSet, pExpr);
	if (ExprHasProperty(pExpr, EP_FromJoin)) {
		Bitmask x =
		    sqlWhereGetMask(pMaskSet, pExpr->iRightJoinTable);
		prereqAll |= x;
		extraRight = x - 1;	/* ON clause terms may not be used with an index
					 * on left table of a LEFT JOIN.  Ticket #3015
					 */
	}
	pTerm->prereqAll = prereqAll;
	pTerm->leftCursor = -1;
	pTerm->iParent = -1;
	pTerm->eOperator = 0;
	if (allowedOp(op)) {
		int iCur, iColumn;
		Expr *pLeft = sqlExprSkipCollate(pExpr->pLeft);
		Expr *pRight = sqlExprSkipCollate(pExpr->pRight);
		u16 opMask =
		    (pTerm->prereqRight & prereqLeft) == 0 ? WO_ALL : WO_EQUIV;

		if (pTerm->iField > 0) {
			assert(op == TK_IN);
			assert(pLeft->op == TK_VECTOR);
			pLeft = pLeft->x.pList->a[pTerm->iField - 1].pExpr;
		}

		if (exprMightBeIndexed(op, pLeft, &iCur, &iColumn)) {
			pTerm->leftCursor = iCur;
			pTerm->u.leftColumn = iColumn;
			pTerm->eOperator = operatorMask(op) & opMask;
		}
		if (pRight != NULL &&
		    exprMightBeIndexed(op, pRight, &iCur, &iColumn)) {
			WhereTerm *pNew;
			Expr *pDup;
			u16 eExtraOp = 0;	/* Extra bits for pNew->eOperator */
			assert(pTerm->iField == 0);
			if (pTerm->leftCursor >= 0) {
				int idxNew;
				pDup = sqlExprDup(pExpr, 0);
				idxNew =
				    whereClauseInsert(pWC, pDup,
						      TERM_VIRTUAL |
						      TERM_DYNAMIC);
				if (idxNew == 0)
					return;
				pNew = &pWC->a[idxNew];
				markTermAsChild(pWC, idxNew, idxTerm);
				pTerm = &pWC->a[idxTerm];
				pTerm->wtFlags |= TERM_COPIED;

				if (termIsEquivalence(pParse, pDup)) {
					pTerm->eOperator |= WO_EQUIV;
					eExtraOp = WO_EQUIV;
				}
			} else {
				pDup = pExpr;
				pNew = pTerm;
			}
			exprCommute(pParse, pDup);
			pNew->leftCursor = iCur;
			pNew->u.leftColumn = iColumn;
			pNew->prereqRight = prereqLeft | extraRight;
			pNew->prereqAll = prereqAll;
			pNew->eOperator =
			    (operatorMask(pDup->op) + eExtraOp) & opMask;
		}
	}

	/* If a term is the BETWEEN operator, create two new virtual terms
	 * that define the range that the BETWEEN implements.  For example:
	 *
	 *      a BETWEEN b AND c
	 *
	 * is converted into:
	 *
	 *      (a BETWEEN b AND c) AND (a>=b) AND (a<=c)
	 *
	 * The two new terms are added onto the end of the WhereClause object.
	 * The new terms are "dynamic" and are children of the original BETWEEN
	 * term.  That means that if the BETWEEN term is coded, the children are
	 * skipped.  Or, if the children are satisfied by an index, the original
	 * BETWEEN term is skipped.
	 */
	else if (pExpr->op == TK_BETWEEN && pWC->op == TK_AND) {
		ExprList *pList = pExpr->x.pList;
		int i;
		static const u8 ops[] = { TK_GE, TK_LE };
		assert(pList != 0);
		assert(pList->nExpr == 2);
		for (i = 0; i < 2; i++) {
			Expr *pNewExpr;
			int idxNew;
			pNewExpr = sqlPExpr(pParse, ops[i],
					    sqlExprDup(pExpr->pLeft, 0),
					    sqlExprDup(pList->a[i].pExpr, 0));
			transferJoinMarkings(pNewExpr, pExpr);
			idxNew =
			    whereClauseInsert(pWC, pNewExpr,
					      TERM_VIRTUAL | TERM_DYNAMIC);
			exprAnalyze(pSrc, pWC, idxNew);
			pTerm = &pWC->a[idxTerm];
			markTermAsChild(pWC, idxNew, idxTerm);
		}
	}

	/* Analyze a term that is composed of two or more subterms connected by
	 * an OR operator.
	 */
	else if (pExpr->op == TK_OR) {
		assert(pWC->op == TK_AND);
		exprAnalyzeOrTerm(pSrc, pWC, idxTerm);
		pTerm = &pWC->a[idxTerm];
	}

	/*
	 * Add constraints to reduce the search space on a LIKE
	 * operator.
	 *
	 * A like pattern of the form "x LIKE 'aBc%'" is changed
	 * into constraints:
	 *
	 *          x>='aBc' AND x<'aBd' AND x LIKE 'aBc%'
	 *
	 * The last character of the prefix "aBc" is incremented
	 * to form the termination condition "abd". Such
	 * optimization allows to use index search and reduce
	 * amount of scanned entries.
	 */
	if (pWC->op == TK_AND &&
	    like_optimization_is_valid(pParse, pExpr, &pStr1,
				       &isComplete)) {
		Expr *pLeft;
		/* Copy of pStr1 - RHS of LIKE operator. */
		Expr *pStr2;
		Expr *pNewExpr1;
		Expr *pNewExpr2;
		int idxNew1;
		int idxNew2;
		const u16 wtFlags = TERM_LIKEOPT | TERM_VIRTUAL | TERM_DYNAMIC;

		pLeft = pExpr->x.pList->a[1].pExpr;
		pStr2 = sqlExprDup(pStr1, 0);

		/* Last character before the first wildcard */
		u8 c, *pC;
		pC = (u8 *)&pStr2->u.zToken[sqlStrlen30(pStr2->u.zToken) - 1];
		c = *pC;
		*pC = c + 1;
		pNewExpr1 = sqlExprDup(pLeft, 0);
		pNewExpr1 = sqlPExpr(pParse, TK_GE, pNewExpr1, pStr1);
		transferJoinMarkings(pNewExpr1, pExpr);
		idxNew1 = whereClauseInsert(pWC, pNewExpr1, wtFlags);
		exprAnalyze(pSrc, pWC, idxNew1);
		pNewExpr2 = sqlExprDup(pLeft, 0);
		pNewExpr2 = sqlPExpr(pParse, TK_LT, pNewExpr2, pStr2);
		transferJoinMarkings(pNewExpr2, pExpr);
		idxNew2 = whereClauseInsert(pWC, pNewExpr2, wtFlags);
		exprAnalyze(pSrc, pWC, idxNew2);
		pTerm = &pWC->a[idxTerm];
		if (isComplete) {
			markTermAsChild(pWC, idxNew1, idxTerm);
			markTermAsChild(pWC, idxNew2, idxTerm);
		}
	}

	/* If there is a vector == or IS term - e.g. "(a, b) == (?, ?)" - create
	 * new terms for each component comparison - "a = ?" and "b = ?".  The
	 * new terms completely replace the original vector comparison, which is
	 * no longer used.
	 *
	 * This is only required if at least one side of the comparison operation
	 * is not a sub-select.
	 */
	if (pWC->op == TK_AND && pExpr->op == TK_EQ
	    && sqlExprIsVector(pExpr->pLeft)
	    && ((pExpr->pLeft->flags & EP_xIsSelect) == 0
		|| (pExpr->pRight->flags & EP_xIsSelect) == 0)) {
		int nLeft = sqlExprVectorSize(pExpr->pLeft);
		int i;
		assert(nLeft == sqlExprVectorSize(pExpr->pRight));
		for (i = 0; i < nLeft; i++) {
			int idxNew;
			Expr *pNew;
			Expr *pLeft =
			    sqlExprForVectorField(pParse, pExpr->pLeft, i);
			Expr *pRight =
			    sqlExprForVectorField(pParse, pExpr->pRight, i);

			pNew = sqlPExpr(pParse, pExpr->op, pLeft, pRight);
			transferJoinMarkings(pNew, pExpr);
			idxNew = whereClauseInsert(pWC, pNew, TERM_DYNAMIC);
			exprAnalyze(pSrc, pWC, idxNew);
		}
		pTerm = &pWC->a[idxTerm];
		pTerm->wtFlags = TERM_CODED | TERM_VIRTUAL;	/* Disable the original */
		pTerm->eOperator = 0;
	}

	/* If there is a vector IN term - e.g. "(a, b) IN (SELECT ...)" - create
	 * a virtual term for each vector component. The expression object
	 * used by each such virtual term is pExpr (the full vector IN(...)
	 * expression). The WhereTerm.iField variable identifies the index within
	 * the vector on the LHS that the virtual term represents.
	 *
	 * This only works if the RHS is a simple SELECT, not a compound
	 */
	if (pWC->op == TK_AND && pExpr->op == TK_IN && pTerm->iField == 0
	    && pExpr->pLeft->op == TK_VECTOR && pExpr->x.pSelect->pPrior == 0) {
		int i;
		for (i = 0; i < sqlExprVectorSize(pExpr->pLeft); i++) {
			int idxNew;
			idxNew = whereClauseInsert(pWC, pExpr, TERM_VIRTUAL);
			pWC->a[idxNew].iField = i + 1;
			exprAnalyze(pSrc, pWC, idxNew);
			markTermAsChild(pWC, idxNew, idxTerm);
		}
	}
	/* When sql_stat4 histogram data is available an operator of the
	 * form "x IS NOT NULL" can sometimes be evaluated more efficiently
	 * as "x>NULL" if x is not an INTEGER PRIMARY KEY.  So construct a
	 * virtual term of that form.
	 *
	 * Note that the virtual term must be tagged with TERM_VNULL.
	 */
	if (pExpr->op == TK_NOTNULL
	    && pExpr->pLeft->op == TK_COLUMN_REF
	    && pExpr->pLeft->iColumn >= 0) {
		Expr *pNewExpr;
		Expr *pLeft = pExpr->pLeft;
		int idxNew;
		WhereTerm *pNewTerm;
		struct Expr *expr = sql_expr_new_anon(TK_NULL);
		pNewExpr = sqlPExpr(pParse, TK_GT, sqlExprDup(pLeft, 0), expr);

		idxNew = whereClauseInsert(pWC, pNewExpr,
					   TERM_VIRTUAL | TERM_DYNAMIC |
					   TERM_VNULL);
		if (idxNew) {
			pNewTerm = &pWC->a[idxNew];
			pNewTerm->prereqRight = 0;
			pNewTerm->leftCursor = pLeft->iTable;
			pNewTerm->u.leftColumn = pLeft->iColumn;
			pNewTerm->eOperator = WO_GT;
			markTermAsChild(pWC, idxNew, idxTerm);
			pTerm = &pWC->a[idxTerm];
			pTerm->wtFlags |= TERM_COPIED;
			pNewTerm->prereqAll = pTerm->prereqAll;
		}
	}

	/* Prevent ON clause terms of a LEFT JOIN from being used to drive
	 * an index for tables to the left of the join.
	 */
	pTerm = &pWC->a[idxTerm];
	pTerm->prereqRight |= extraRight;
}

/***************************************************************************
 * Routines with file scope above.  Interface to the rest of the where.c
 * subsystem follows.
 **************************************************************************/

/*
 * This routine identifies subexpressions in the WHERE clause where
 * each subexpression is separated by the AND operator or some other
 * operator specified in the op parameter.  The WhereClause structure
 * is filled with pointers to subexpressions.  For example:
 *
 *    WHERE  a=='hello' AND coalesce(b,11)<10 AND (c+12!=d OR c==22)
 *           \________/     \_______________/     \________________/
 *            slot[0]            slot[1]               slot[2]
 *
 * The original WHERE clause in pExpr is unaltered.  All this routine
 * does is make slot[] entries point to substructure within pExpr.
 *
 * In the previous sentence and in the diagram, "slot[]" refers to
 * the WhereClause.a[] array.  The slot[] array grows as needed to contain
 * all terms of the WHERE clause.
 */
void
sqlWhereSplit(WhereClause * pWC, Expr * pExpr, u8 op)
{
	Expr *pE2 = sqlExprSkipCollate(pExpr);
	pWC->op = op;
	if (pE2 == 0)
		return;
	if (pE2->op != op) {
		whereClauseInsert(pWC, pExpr, 0);
	} else {
		sqlWhereSplit(pWC, pE2->pLeft, op);
		sqlWhereSplit(pWC, pE2->pRight, op);
	}
}

/*
 * Initialize a preallocated WhereClause structure.
 */
void
sqlWhereClauseInit(WhereClause * pWC,	/* The WhereClause to be initialized */
		       WhereInfo * pWInfo	/* The WHERE processing context */
    )
{
	pWC->pWInfo = pWInfo;
	pWC->pOuter = 0;
	pWC->nTerm = 0;
	pWC->nSlot = ArraySize(pWC->aStatic);
	pWC->a = pWC->aStatic;
}

/*
 * Deallocate a WhereClause structure.  The WhereClause structure
 * itself is not freed.  This routine is the inverse of
 * sqlWhereClauseInit().
 */
void
sqlWhereClauseClear(WhereClause * pWC)
{
	int i;
	WhereTerm *a;
	for (i = pWC->nTerm - 1, a = pWC->a; i >= 0; i--, a++) {
		if (a->wtFlags & TERM_DYNAMIC)
			sql_expr_delete(a->pExpr);
		if (a->wtFlags & TERM_ORINFO) {
			whereOrInfoDelete(a->u.pOrInfo);
		} else if (a->wtFlags & TERM_ANDINFO) {
			whereAndInfoDelete(a->u.pAndInfo);
		}
	}
	if (pWC->a != pWC->aStatic) {
		sql_xfree(pWC->a);
	}
}

/*
 * These routines walk (recursively) an expression tree and generate
 * a bitmask indicating which tables are used in that expression
 * tree.
 */
Bitmask
sqlWhereExprUsage(WhereMaskSet * pMaskSet, Expr * p)
{
	Bitmask mask;
	if (p == 0)
		return 0;
	if (p->op == TK_COLUMN_REF) {
		mask = sqlWhereGetMask(pMaskSet, p->iTable);
		return mask;
	}
	assert(!ExprHasProperty(p, EP_TokenOnly));
	mask = p->pRight ? sqlWhereExprUsage(pMaskSet, p->pRight) : 0;
	if (p->pLeft)
		mask |= sqlWhereExprUsage(pMaskSet, p->pLeft);
	if (ExprHasProperty(p, EP_xIsSelect)) {
		mask |= exprSelectUsage(pMaskSet, p->x.pSelect);
	} else if (p->x.pList) {
		mask |= sqlWhereExprListUsage(pMaskSet, p->x.pList);
	}
	return mask;
}

Bitmask
sqlWhereExprListUsage(WhereMaskSet * pMaskSet, ExprList * pList)
{
	int i;
	Bitmask mask = 0;
	if (pList) {
		for (i = 0; i < pList->nExpr; i++) {
			mask |=
			    sqlWhereExprUsage(pMaskSet, pList->a[i].pExpr);
		}
	}
	return mask;
}

/*
 * Call exprAnalyze on all terms in a WHERE clause.
 *
 * Note that exprAnalyze() might add new virtual terms onto the
 * end of the WHERE clause.  We do not want to analyze these new
 * virtual terms, so start analyzing at the end and work forward
 * so that the added virtual terms are never processed.
 */
void
sqlWhereExprAnalyze(SrcList * pTabList,	/* the FROM clause */
			WhereClause * pWC	/* the WHERE clause to be analyzed */
    )
{
	int i;
	for (i = pWC->nTerm - 1; i >= 0; i--) {
		exprAnalyze(pTabList, pWC, i);
	}
}

/*
 * For table-valued-functions, transform the function arguments into
 * new WHERE clause terms.
 */
void
sqlWhereTabFuncArgs(Parse * pParse,	/* Parsing context */
			struct SrcList_item *pItem,	/* The FROM clause term to process */
			WhereClause * pWC	/* Xfer function arguments to here */
    )
{
	int j, k;
	ExprList *pArgs;
	Expr *pColRef;
	Expr *pTerm;
	if (pItem->fg.isTabFunc == 0)
		return;
	struct space_def *space_def = pItem->space->def;
	pArgs = pItem->u1.pFuncArg;
	if (pArgs == 0)
		return;
	for (j = k = 0; j < pArgs->nExpr; j++) {
		while (k < (int)space_def->field_count)
			k++;
		/*
		 * This assert replaces error. At the moment, this
		 * error cannot appear due to this function being
		 * unused.
		 */
		assert(k < (int)space_def->field_count);
		pColRef = sql_expr_new_anon(TK_COLUMN_REF);
		pColRef->iTable = pItem->iCursor;
		pColRef->iColumn = k++;
		pColRef->space_def = space_def;
		pTerm = sqlPExpr(pParse, TK_EQ, pColRef,
				 sqlExprDup(pArgs->a[j].pExpr, 0));
		whereClauseInsert(pWC, pTerm, TERM_DYNAMIC);
	}
}
