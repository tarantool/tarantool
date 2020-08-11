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
 * This file was split off from where.c on 2015-06-06 in order to reduce the
 * size of where.c and make it easier to edit.  This file contains the routines
 * that actually generate the bulk of the WHERE loop code.  The original where.c
 * file retains the code that does query planning and analysis.
 */
#include "box/schema.h"
#include "sqlInt.h"
#include "whereInt.h"

/*
 * Return the name of the i-th column of the pIdx index.
 */
static const char *
explainIndexColumnName(const struct index_def *idx_def, int i)
{
	i = idx_def->key_def->parts[i].fieldno;
	struct space *space = space_by_id(idx_def->space_id);
	assert(space != NULL);
	return space->def->fields[i].name;
}

/*
 * This routine is a helper for explainIndexRange() below
 *
 * pStr holds the text of an expression that we are building up one term
 * at a time.  This routine adds a new term to the end of the expression.
 * Terms are separated by AND so add the "AND" text for second and subsequent
 * terms only.
 */
static void
explainAppendTerm(StrAccum * pStr,	/* The text expression being built */
		  struct index_def *def,
		  int nTerm,		/* Number of terms */
		  int iTerm,		/* Zero-based index of first term. */
		  int bAnd,		/* Non-zero to append " AND " */
		  const char *zOp)	/* Name of the operator */
{
	int i;

	assert(nTerm >= 1);
	if (bAnd)
		sqlStrAccumAppend(pStr, " AND ", 5);

	if (nTerm > 1)
		sqlStrAccumAppend(pStr, "(", 1);
	for (i = 0; i < nTerm; i++) {
		if (i)
			sqlStrAccumAppend(pStr, ",", 1);
		const char *name = "";
		if (def != NULL)
			name = explainIndexColumnName(def, iTerm + i);
		sqlStrAccumAppendAll(pStr, name);
	}
	if (nTerm > 1)
		sqlStrAccumAppend(pStr, ")", 1);

	sqlStrAccumAppend(pStr, zOp, 1);

	if (nTerm > 1)
		sqlStrAccumAppend(pStr, "(", 1);
	for (i = 0; i < nTerm; i++) {
		if (i)
			sqlStrAccumAppend(pStr, ",", 1);
		sqlStrAccumAppend(pStr, "?", 1);
	}
	if (nTerm > 1)
		sqlStrAccumAppend(pStr, ")", 1);
}

/*
 * Argument pLevel describes a strategy for scanning table pTab. This
 * function appends text to pStr that describes the subset of table
 * rows scanned by the strategy in the form of an SQL expression.
 *
 * For example, if the query:
 *
 *   SELECT * FROM t1 WHERE a=1 AND b>2;
 *
 * is run and there is an index on (a, b), then this function returns a
 * string similar to:
 *
 *   "a=? AND b>?"
 */
static void
explainIndexRange(StrAccum * pStr, WhereLoop * pLoop)
{
	struct index_def *def = pLoop->index_def;
	u16 nEq = pLoop->nEq;
	u16 nSkip = pLoop->nSkip;
	int i, j;

	assert(def != NULL);

	if (nEq == 0
	    && (pLoop->wsFlags & (WHERE_BTM_LIMIT | WHERE_TOP_LIMIT)) == 0)
		return;
	sqlStrAccumAppend(pStr, " (", 2);
	for (i = 0; i < nEq; i++) {
		const char *z;
		if (def != NULL) {
			z = explainIndexColumnName(def, i);
		} else {
			struct space *space = space_cache_find(def->space_id);
			assert(space != NULL);
			uint32_t fieldno = def->key_def->parts[i].fieldno;
			z = space->def->fields[fieldno].name;
		}
		if (i)
			sqlStrAccumAppend(pStr, " AND ", 5);
		sqlXPrintf(pStr, i >= nSkip ? "%s=?" : "ANY(%s)", z);
	}

	j = i;
	if (pLoop->wsFlags & WHERE_BTM_LIMIT) {
		explainAppendTerm(pStr, def, pLoop->nBtm, j, i, ">");
		i = 1;
	}
	if (pLoop->wsFlags & WHERE_TOP_LIMIT) {
		explainAppendTerm(pStr, def, pLoop->nTop, j, i, "<");
	}
	sqlStrAccumAppend(pStr, ")", 1);
}

/*
 * This function is a no-op unless currently processing an EXPLAIN QUERY PLAN
 * command, or if SQL_DEBUG was
 * defined at compile-time. If it is not a no-op, a single OP_Explain opcode
 * is added to the output to describe the table scan strategy in pLevel.
 *
 * If an OP_Explain opcode is added to the VM, its address is returned.
 * Otherwise, if no OP_Explain is coded, zero is returned.
 */
int
sqlWhereExplainOneScan(Parse * pParse,	/* Parse context */
			   SrcList * pTabList,	/* Table list this loop refers to */
			   WhereLevel * pLevel,	/* Scan to write OP_Explain opcode for */
			   int iLevel,		/* Value for "level" column of output */
			   int iFrom,		/* Value for "from" column of output */
			   u16 wctrlFlags)	/* Flags passed to sqlWhereBegin() */
{
	int ret = 0;
#if !defined(SQL_DEBUG)
	if (pParse->explain == 2)
#endif
	{
		struct SrcList_item *pItem = &pTabList->a[pLevel->iFrom];
		Vdbe *v = pParse->pVdbe;	/* VM being constructed */
		sql *db = pParse->db;	/* Database handle */
		int iId = pParse->iSelectId;	/* Select id (left-most output column) */
		int isSearch;	/* True for a SEARCH. False for SCAN. */
		WhereLoop *pLoop;	/* The controlling WhereLoop object */
		u32 flags;	/* Flags that describe this loop */
		char *zMsg;	/* Text to add to EQP output */
		StrAccum str;	/* EQP output string */
		char zBuf[100];	/* Initial space for EQP output string */

		pLoop = pLevel->pWLoop;
		flags = pLoop->wsFlags;
		if ((flags & WHERE_MULTI_OR)
		    || (wctrlFlags & WHERE_OR_SUBCLAUSE))
			return 0;

		isSearch = (flags & (WHERE_BTM_LIMIT | WHERE_TOP_LIMIT)) != 0
		    || (pLoop->nEq > 0)
		    || (wctrlFlags & (WHERE_ORDERBY_MIN | WHERE_ORDERBY_MAX));

		sqlStrAccumInit(&str, db, zBuf, sizeof(zBuf),
				    SQL_MAX_LENGTH);
		sqlStrAccumAppendAll(&str, isSearch ? "SEARCH" : "SCAN");
		if (pItem->pSelect) {
			sqlXPrintf(&str, " SUBQUERY %d", pItem->iSelectId);
		} else {
			sqlXPrintf(&str, " TABLE %s", pItem->zName);
		}

		if (pItem->zAlias) {
			sqlXPrintf(&str, " AS %s", pItem->zAlias);
		}
		if ((flags & WHERE_IPK) == 0) {
			const char *zFmt = 0;
			struct index_def *idx_def = pLoop->index_def;
			if (idx_def == NULL)
				return 0;

			assert(!(flags & WHERE_AUTO_INDEX)
			       || (flags & WHERE_IDX_ONLY));
			if ((flags & WHERE_AUTO_INDEX) != 0) {
				zFmt = "EPHEMERAL INDEX";
			} else if (idx_def->iid == 0) {
				if (isSearch) {
					zFmt = "PRIMARY KEY";
				}
			} else if (flags & WHERE_IDX_ONLY) {
				zFmt = "COVERING INDEX %s";
			} else {
				zFmt = "INDEX %s";
			}
			if (zFmt) {
				sqlStrAccumAppend(&str, " USING ", 7);
				sqlXPrintf(&str, zFmt, idx_def->name);
				explainIndexRange(&str, pLoop);
			}
		} else if ((flags & WHERE_IPK) != 0
			   && (flags & WHERE_CONSTRAINT) != 0) {
			const char *zRangeOp;
			if (flags & (WHERE_COLUMN_EQ | WHERE_COLUMN_IN)) {
				zRangeOp = "=";
			} else if ((flags & WHERE_BOTH_LIMIT) ==
				   WHERE_BOTH_LIMIT) {
				zRangeOp = ">? AND rowid<";
			} else if (flags & WHERE_BTM_LIMIT) {
				zRangeOp = ">";
			} else {
				assert(flags & WHERE_TOP_LIMIT);
				zRangeOp = "<";
			}
			sqlXPrintf(&str,
				       " USING INTEGER PRIMARY KEY (rowid%s?)",
				       zRangeOp);
		}

		if (pLoop->nOut >= 10) {
			sqlXPrintf(&str, " (~%llu rows)",
				       sqlLogEstToInt(pLoop->nOut));
		} else {
			sqlStrAccumAppend(&str, " (~1 row)", 9);
		}

		zMsg = sqlStrAccumFinish(&str);
		ret =
		    sqlVdbeAddOp4(v, OP_Explain, iId, iLevel, iFrom, zMsg,
				      P4_DYNAMIC);
	}
	return ret;
}

/*
 * Disable a term in the WHERE clause.  Except, do not disable the term
 * if it controls a LEFT OUTER JOIN and it did not originate in the ON
 * or USING clause of that join.
 *
 * Consider the term t2.z='ok' in the following queries:
 *
 *   (1)  SELECT * FROM t1 LEFT JOIN t2 ON t1.a=t2.x WHERE t2.z='ok'
 *   (2)  SELECT * FROM t1 LEFT JOIN t2 ON t1.a=t2.x AND t2.z='ok'
 *   (3)  SELECT * FROM t1, t2 WHERE t1.a=t2.x AND t2.z='ok'
 *
 * The t2.z='ok' is disabled in the in (2) because it originates
 * in the ON clause.  The term is disabled in (3) because it is not part
 * of a LEFT OUTER JOIN.  In (1), the term is not disabled.
 *
 * Disabling a term causes that term to not be tested in the inner loop
 * of the join.  Disabling is an optimization.  When terms are satisfied
 * by indices, we disable them to prevent redundant tests in the inner
 * loop.  We would get the correct results if nothing were ever disabled,
 * but joins might run a little slower.  The trick is to disable as much
 * as we can without disabling too much.  If we disabled in (1), we'd get
 * the wrong answer.  See ticket #813.
 *
 * If all the children of a term are disabled, then that term is also
 * automatically disabled.  In this way, terms get disabled if derived
 * virtual terms are tested first.  For example:
 *
 *      x LIKE 'abc%' AND x>='abc' AND x<'acd'
 *      \___________/     \______/     \_____/
 *         parent          child1       child2
 *
 * Only the parent term was in the original WHERE clause.  The child1
 * and child2 terms were added by the LIKE optimization.  If both of
 * the virtual child terms are valid, then testing of the parent can be
 * skipped.
 *
 * Usually the parent term is marked as TERM_CODED.  But if the parent
 * term was originally TERM_LIKE, then the parent gets TERM_LIKECOND instead.
 * The TERM_LIKECOND marking indicates that the term should be coded inside
 * a conditional such that is only evaluated on the second pass of a
 * LIKE-optimization loop, when scanning BLOBs instead of strings.
 */
static void
disableTerm(WhereLevel * pLevel, WhereTerm * pTerm)
{
	int nLoop = 0;
	while (ALWAYS(pTerm != 0)
	       && (pTerm->wtFlags & TERM_CODED) == 0
	       && (pLevel->iLeftJoin == 0
		   || ExprHasProperty(pTerm->pExpr, EP_FromJoin))
	       && (pLevel->notReady & pTerm->prereqAll) == 0) {
		if (nLoop && (pTerm->wtFlags & TERM_LIKE) != 0) {
			pTerm->wtFlags |= TERM_LIKECOND;
		} else {
			pTerm->wtFlags |= TERM_CODED;
		}
		if (pTerm->iParent < 0)
			break;
		pTerm = &pTerm->pWC->a[pTerm->iParent];
		pTerm->nChild--;
		if (pTerm->nChild != 0)
			break;
		nLoop++;
	}
}

/**
 * Code an OP_ApplyType opcode to apply the column type string
 * @types to the n registers starting at @base.
 *
 * As an optimization, SCALAR entries (which are no-ops) at the
 * beginning and end of @types are ignored.  If all entries in
 * @types are SCALAR, then no code gets generated.
 *
 * This routine makes its own copy of @types so that the caller is
 * free to modify @types after this routine returns.
 */
static void
emit_apply_type(Parse *pParse, int base, int n, enum field_type *types)
{
	Vdbe *v = pParse->pVdbe;
	if (types == NULL) {
		assert(pParse->db->mallocFailed);
		return;
	}
	assert(v != 0);

	/*
	 * Adjust base and n to skip over SCALAR entries at the
	 * beginning and end of the type sequence.
	 */
	while (n > 0 && types[0] == FIELD_TYPE_SCALAR) {
		n--;
		base++;
		types++;
	}
	while (n > 1 && types[n - 1] == FIELD_TYPE_SCALAR) {
		n--;
	}

	if (n > 0) {
		enum field_type *types_dup = field_type_sequence_dup(pParse,
								     types, n);
		sqlVdbeAddOp4(v, OP_ApplyType, base, n, 0,
				  (char *) types_dup, P4_DYNAMIC);
		sql_expr_type_cache_change(pParse, base, n);
	}
}

/**
 * Expression @rhs, which is the RHS of a comparison operation, is
 * either a vector of n elements or, if n==1, a scalar expression.
 * Before the comparison operation, types @types are to be applied
 * to the @rhs values. This function modifies entries within the
 * field sequence to SCALAR if either:
 *
 *   * the comparison will be performed with no type, or
 *   * the type change in @types is guaranteed not to change the value.
 */
static void
expr_cmp_update_rhs_type(struct Expr *rhs, int n, enum field_type *types)
{
	for (int i = 0; i < n; i++) {
		Expr *p = sqlVectorFieldSubexpr(rhs, i);
		enum field_type expr_type = sql_expr_type(p);
		if (sql_type_result(expr_type, types[i]) == FIELD_TYPE_SCALAR ||
		    sql_expr_needs_no_type_change(p, types[i])) {
			types[i] = FIELD_TYPE_SCALAR;
		}
	}
}

/*
 * Generate code for a single equality term of the WHERE clause.  An equality
 * term can be either X=expr or X IN (...).   pTerm is the term to be
 * coded.
 *
 * The current value for the constraint is left in a register, the index
 * of which is returned.  An attempt is made store the result in iTarget but
 * this is only guaranteed for TK_ISNULL and TK_IN constraints.  If the
 * constraint is a TK_EQ, then the current value might be left in
 * some other register and it is the caller's responsibility to compensate.
 *
 * For a constraint of the form X=expr, the expression is evaluated in
 * straight-line code.  For constraints of the form X IN (...)
 * this routine sets up a loop that will iterate over all values of X.
 */
static int
codeEqualityTerm(Parse * pParse,	/* The parsing context */
		 WhereTerm * pTerm,	/* The term of the WHERE clause to be coded */
		 WhereLevel * pLevel,	/* The level of the FROM clause we are working on */
		 int iEq,		/* Index of the equality term within this level */
		 int bRev,		/* True for reverse-order IN operations */
		 int iTarget)		/* Attempt to leave results in this register */
{
	Expr *pX = pTerm->pExpr;
	Vdbe *v = pParse->pVdbe;
	int iReg;		/* Register holding results */
	int iSingleIdxCol = 0;	/* Tarantool. In case of (nExpr == 1) store column index here.  */

	assert(pLevel->pWLoop->aLTerm[iEq] == pTerm);
	assert(iTarget > 0);
	if (pX->op == TK_EQ) {
		iReg = sqlExprCodeTarget(pParse, pX->pRight, iTarget);
	} else if (pX->op == TK_ISNULL) {
		iReg = iTarget;
		sqlVdbeAddOp2(v, OP_Null, 0, iReg);
	} else {
		int eType = IN_INDEX_NOOP;
		int iTab;
		struct InLoop *pIn;
		WhereLoop *pLoop = pLevel->pWLoop;
		int i;
		int nEq = 0;
		int *aiMap = 0;

		if (pLoop->index_def != NULL &&
		    pLoop->index_def->key_def->parts[iEq].sort_order) {
			testcase(iEq == 0);
			testcase(bRev);
			bRev = !bRev;
		}
		assert(pX->op == TK_IN);
		iReg = iTarget;

		for (i = 0; i < iEq; i++) {
			if (pLoop->aLTerm[i] && pLoop->aLTerm[i]->pExpr == pX) {
				disableTerm(pLevel, pTerm);
				return iTarget;
			}
		}
		for (i = iEq; i < pLoop->nLTerm; i++) {
			if (ALWAYS(pLoop->aLTerm[i])
			    && pLoop->aLTerm[i]->pExpr == pX)
				nEq++;
		}

		if ((pX->flags & EP_xIsSelect) == 0
		    || pX->x.pSelect->pEList->nExpr == 1) {
			eType =
			    sqlFindInIndex(pParse, pX, IN_INDEX_LOOP, 0, 0,
					       &iSingleIdxCol);
		} else {
			Select *pSelect = pX->x.pSelect;
			sql *db = pParse->db;
			u16 savedDbOptFlags = db->dbOptFlags;
			ExprList *pOrigRhs = pSelect->pEList;
			ExprList *pOrigLhs = pX->pLeft->x.pList;
			ExprList *pRhs = 0;	/* New Select.pEList for RHS */
			ExprList *pLhs = 0;	/* New pX->pLeft vector */

			for (i = iEq; i < pLoop->nLTerm; i++) {
				if (pLoop->aLTerm[i]->pExpr == pX) {
					int iField =
					    pLoop->aLTerm[i]->iField - 1;
					Expr *pNewRhs =
					    sqlExprDup(db,
							   pOrigRhs->a[iField].
							   pExpr, 0);
					Expr *pNewLhs =
					    sqlExprDup(db,
							   pOrigLhs->a[iField].
							   pExpr, 0);

					pRhs =
					    sql_expr_list_append(pParse->db,
								 pRhs, pNewRhs);
					pLhs =
					    sql_expr_list_append(pParse->db,
								 pLhs, pNewLhs);
				}
			}
			if (!db->mallocFailed) {
				Expr *pLeft = pX->pLeft;

				if (pSelect->pOrderBy) {
					/* If the SELECT statement has an ORDER BY clause, zero the
					 * iOrderByCol variables. These are set to non-zero when an
					 * ORDER BY term exactly matches one of the terms of the
					 * result-set. Since the result-set of the SELECT statement may
					 * have been modified or reordered, these variables are no longer
					 * set correctly.  Since setting them is just an optimization,
					 * it's easiest just to zero them here.
					 */
					ExprList *pOrderBy = pSelect->pOrderBy;
					for (i = 0; i < pOrderBy->nExpr; i++) {
						pOrderBy->a[i].u.x.iOrderByCol =
						    0;
					}
				}

				/* Take care here not to generate a TK_VECTOR containing only a
				 * single value. Since the parser never creates such a vector, some
				 * of the subroutines do not handle this case.
				 */
				if (pLhs->nExpr == 1) {
					pX->pLeft = pLhs->a[0].pExpr;
				} else {
					pLeft->x.pList = pLhs;
					aiMap =
					    (int *)sqlDbMallocZero(pParse->db,
								       sizeof(int) * nEq);
					testcase(aiMap == 0);
				}
				pSelect->pEList = pRhs;
				db->dbOptFlags |= SQL_QueryFlattener;
				eType =
				    sqlFindInIndex(pParse, pX,
						       IN_INDEX_LOOP, 0, aiMap,
						       0);
				db->dbOptFlags = savedDbOptFlags;
				testcase(aiMap != 0 && aiMap[0] != 0);
				pSelect->pEList = pOrigRhs;
				pLeft->x.pList = pOrigLhs;
				pX->pLeft = pLeft;
			}
			sql_expr_list_delete(pParse->db, pLhs);
			sql_expr_list_delete(pParse->db, pRhs);
		}

		if (eType == IN_INDEX_INDEX_DESC) {
			testcase(bRev);
			bRev = !bRev;
		}
		iTab = pX->iTable;
		sqlVdbeAddOp2(v, bRev ? OP_Last : OP_Rewind, iTab, 0);
		VdbeCoverageIf(v, bRev);
		VdbeCoverageIf(v, !bRev);
		assert((pLoop->wsFlags & WHERE_MULTI_OR) == 0);

		pLoop->wsFlags |= WHERE_IN_ABLE;
		if (pLevel->u.in.nIn == 0) {
			pLevel->addrNxt = sqlVdbeMakeLabel(v);
		}

		i = pLevel->u.in.nIn;
		pLevel->u.in.nIn += nEq;
		pLevel->u.in.aInLoop =
		    sqlDbReallocOrFree(pParse->db, pLevel->u.in.aInLoop,
					   sizeof(pLevel->u.in.aInLoop[0]) *
					   pLevel->u.in.nIn);
		pIn = pLevel->u.in.aInLoop;
		if (pIn) {
			int iMap = 0;	/* Index in aiMap[] */
			pIn += i;
			for (i = iEq; i < pLoop->nLTerm; i++) {
				if (pLoop->aLTerm[i]->pExpr == pX) {
					int iOut = iReg + i - iEq;
					int iCol =
						aiMap ? aiMap[iMap++] :
						iSingleIdxCol;
					pIn->addrInTop =
						sqlVdbeAddOp3(v, OP_Column,
								  iTab, iCol,
								  iOut);
					sqlVdbeAddOp1(v, OP_IsNull, iOut);
					VdbeCoverage(v);
					if (i == iEq) {
						pIn->iCur = iTab;
						pIn->eEndLoopOp =
						    bRev ? OP_PrevIfOpen : OP_NextIfOpen;
					} else {
						pIn->eEndLoopOp = OP_Noop;
					}
					pIn++;
				}
			}
		} else {
			pLevel->u.in.nIn = 0;
		}
		sqlDbFree(pParse->db, aiMap);
	}
	disableTerm(pLevel, pTerm);
	return iReg;
}

/*
 * Generate code that will evaluate all == and IN constraints for an
 * index scan.
 *
 * For example, consider table t1(a,b,c,d,e,f) with index i1(a,b,c).
 * Suppose the WHERE clause is this:  a==5 AND b IN (1,2,3) AND c>5 AND c<10
 * The index has as many as three equality constraints, but in this
 * example, the third "c" value is an inequality.  So only two
 * constraints are coded.  This routine will generate code to evaluate
 * a==5 and b IN (1,2,3).  The current values for a and b will be stored
 * in consecutive registers and the index of the first register is returned.
 *
 * In the example above nEq==2.  But this subroutine works for any value
 * of nEq including 0.  If nEq==0, this routine is nearly a no-op.
 * The only thing it does is allocate the pLevel->iMem memory cell and
 * compute the types array.
 *
 * The nExtraReg parameter is 0 or 1.  It is 0 if all WHERE clause constraints
 * are == or IN and are covered by the nEq.  nExtraReg is 1 if there is
 * an inequality constraint (such as the "c>=5 AND c<10" in the example) that
 * occurs after the nEq quality constraints.
 *
 * This routine allocates a range of nEq+nExtraReg memory cells and returns
 * the index of the first memory cell in that range. The code that
 * calls this routine will use that memory range to store keys for
 * start and termination conditions of the loop.
 * key value of the loop.  If one or more IN operators appear, then
 * this routine allocates an additional nEq memory cells for internal
 * use.
 *
 * Before returning, @types is set to point to a buffer containing a
 * copy of the column types array of the index allocated using
 * sqlDbMalloc(). This array is passed to OP_ApplyType to provide
 * correct implicit conversions.
 */
static int
codeAllEqualityTerms(Parse * pParse,	/* Parsing context */
		     WhereLevel * pLevel,	/* Which nested loop of the FROM we are coding */
		     int bRev,		/* Reverse the order of IN operators */
		     int nExtraReg,	/* Number of extra registers to allocate */
		     enum field_type **res_type)
{
	u16 nEq;		/* The number of == or IN constraints to code */
	u16 nSkip;		/* Number of left-most columns to skip */
	Vdbe *v = pParse->pVdbe;	/* The vm under construction */
	WhereTerm *pTerm;	/* A single constraint term */
	WhereLoop *pLoop;	/* The WhereLoop object */
	int j;			/* Loop counter */
	int regBase;		/* Base register */
	int nReg;		/* Number of registers to allocate */

	/* This module is only called on query plans that use an index. */
	pLoop = pLevel->pWLoop;
	nEq = pLoop->nEq;
	nSkip = pLoop->nSkip;
	struct index_def *idx_def = pLoop->index_def;
	assert(idx_def != NULL);

	/* Figure out how many memory cells we will need then allocate them.
	 */
	regBase = pParse->nMem + 1;
	nReg = pLoop->nEq + nExtraReg;
	pParse->nMem += nReg;

	enum field_type *type = sql_index_type_str(pParse->db, idx_def);
	assert(type != NULL || pParse->db->mallocFailed);

	if (nSkip) {
		int iIdxCur = pLevel->iIdxCur;
		sqlVdbeAddOp1(v, (bRev ? OP_Last : OP_Rewind), iIdxCur);
		VdbeCoverageIf(v, bRev == 0);
		VdbeCoverageIf(v, bRev != 0);
		VdbeComment((v, "begin skip-scan on %s", idx_def->name));
		j = sqlVdbeAddOp0(v, OP_Goto);
		pLevel->addrSkip =
		    sqlVdbeAddOp4Int(v, (bRev ? OP_SeekLT : OP_SeekGT),
					 iIdxCur, 0, regBase, nSkip);
		VdbeCoverageIf(v, bRev == 0);
		VdbeCoverageIf(v, bRev != 0);
		sqlVdbeJumpHere(v, j);
		for (j = 0; j < nSkip; j++) {
			sqlVdbeAddOp3(v, OP_Column, iIdxCur,
					  idx_def->key_def->parts[j].fieldno,
					  regBase + j);
			VdbeComment((v, "%s", explainIndexColumnName(idx_def, j)));
		}
	}

	/* Evaluate the equality constraints
	 */
	for (j = nSkip; j < nEq; j++) {
		int r1;
		pTerm = pLoop->aLTerm[j];
		assert(pTerm != 0);
		/* The following testcase is true for indices with redundant columns.
		 * Ex: CREATE INDEX i1 ON t1(a,b,a); SELECT * FROM t1 WHERE a=0 AND b=0;
		 */
		testcase((pTerm->wtFlags & TERM_CODED) != 0);
		testcase(pTerm->wtFlags & TERM_VIRTUAL);
		r1 = codeEqualityTerm(pParse, pTerm, pLevel, j, bRev,
				      regBase + j);
		if (r1 != regBase + j) {
			if (nReg == 1) {
				sqlReleaseTempReg(pParse, regBase);
				regBase = r1;
			} else {
				sqlVdbeAddOp2(v, OP_SCopy, r1, regBase + j);
			}
		}
		if (pTerm->eOperator & WO_IN) {
			if (pTerm->pExpr->flags & EP_xIsSelect) {
				/* No type ever needs to be (or should be) applied to a value
				 * from the RHS of an "? IN (SELECT ...)" expression. The
				 * sqlFindInIndex() routine has already ensured that the
				 * type of the comparison has been applied to the value.
				 */
				if (type != NULL)
					type[j] = FIELD_TYPE_SCALAR;
			}
		} else if ((pTerm->eOperator & WO_ISNULL) == 0) {
			Expr *pRight = pTerm->pExpr->pRight;
			if (sqlExprCanBeNull(pRight)) {
				sqlVdbeAddOp2(v, OP_IsNull, regBase + j,
						  pLevel->addrBrk);
				VdbeCoverage(v);
			}
		}
	}
	*res_type = type;
	return regBase;
}

/*
 * If the expression passed as the second argument is a vector, generate
 * code to write the first nReg elements of the vector into an array
 * of registers starting with iReg.
 *
 * If the expression is not a vector, then nReg must be passed 1. In
 * this case, generate code to evaluate the expression and leave the
 * result in register iReg.
 */
static void
codeExprOrVector(Parse * pParse, Expr * p, int iReg, int nReg)
{
	assert(nReg > 0);
	if (sqlExprIsVector(p)) {
		if ((p->flags & EP_xIsSelect)) {
			Vdbe *v = pParse->pVdbe;
			int iSelect = sqlCodeSubselect(pParse, p, 0);
			sqlVdbeAddOp3(v, OP_Copy, iSelect, iReg, nReg - 1);
		} else {
			int i;
			ExprList *pList = p->x.pList;
			assert(nReg <= pList->nExpr);
			for (i = 0; i < nReg; i++) {
				sqlExprCode(pParse, pList->a[i].pExpr,
						iReg + i);
			}
		}
	} else {
		assert(nReg == 1);
		sqlExprCode(pParse, p, iReg);
	}
}

/*
 * Generate code for the start of the iLevel-th loop in the WHERE clause
 * implementation described by pWInfo.
 */
Bitmask
sqlWhereCodeOneLoopStart(WhereInfo * pWInfo,	/* Complete information about the WHERE clause */
			     int iLevel,	/* Which level of pWInfo->a[] should be coded */
			     Bitmask notReady)	/* Which tables are currently available */
{
	int j, k;		/* Loop counters */
	int iCur;		/* The VDBE cursor for the table */
	int addrNxt;		/* Where to jump to continue with the next IN case */
	int omitTable;		/* True if we use the index only */
	int bRev;		/* True if we need to scan in reverse order */
	WhereLevel *pLevel;	/* The where level to be coded */
	WhereLoop *pLoop;	/* The WhereLoop object being coded */
	WhereClause *pWC;	/* Decomposition of the entire WHERE clause */
	WhereTerm *pTerm;	/* A WHERE clause term */
	Parse *pParse;		/* Parsing context */
	sql *db;		/* Database connection */
	Vdbe *v;		/* The prepared stmt under constructions */
	struct SrcList_item *pTabItem;	/* FROM clause term being coded */
	int addrBrk;		/* Jump here to break out of the loop */
	int addrCont;		/* Jump here to continue with next cycle */

	pParse = pWInfo->pParse;
	v = pParse->pVdbe;
	pWC = &pWInfo->sWC;
	db = pParse->db;
	pLevel = &pWInfo->a[iLevel];
	pLoop = pLevel->pWLoop;
	pTabItem = &pWInfo->pTabList->a[pLevel->iFrom];
	iCur = pTabItem->iCursor;
	pLevel->notReady =
	    notReady & ~sqlWhereGetMask(&pWInfo->sMaskSet, iCur);
	bRev = (pWInfo->revMask >> iLevel) & 1;
	omitTable = (pLoop->wsFlags & WHERE_IDX_ONLY) != 0
	    && ((pWInfo->wctrlFlags & WHERE_OR_SUBCLAUSE) == 0 ||
		(pLoop->wsFlags & WHERE_AUTO_INDEX) != 0);
	VdbeModuleComment((v, "Begin WHERE-loop%d: %s", iLevel,
			   pTabItem->pTab->zName));

	/* Create labels for the "break" and "continue" instructions
	 * for the current loop.  Jump to addrBrk to break out of a loop.
	 * Jump to cont to go immediately to the next iteration of the
	 * loop.
	 *
	 * When there is an IN operator, we also have a "addrNxt" label that
	 * means to continue with the next IN value combination.  When
	 * there are no IN operators in the constraints, the "addrNxt" label
	 * is the same as "addrBrk".
	 */
	addrBrk = pLevel->addrBrk = pLevel->addrNxt = sqlVdbeMakeLabel(v);
	addrCont = pLevel->addrCont = sqlVdbeMakeLabel(v);

	/* If this is the right table of a LEFT OUTER JOIN, allocate and
	 * initialize a memory cell that records if this table matches any
	 * row of the left table of the join.
	 */
	if (pLevel->iFrom > 0 && (pTabItem[0].fg.jointype & JT_LEFT) != 0) {
		pLevel->iLeftJoin = ++pParse->nMem;
		sqlVdbeAddOp2(v, OP_Integer, 0, pLevel->iLeftJoin);
		VdbeComment((v, "init LEFT JOIN no-match flag"));
	}

	/* Special case of a FROM clause subquery implemented as a co-routine */
	if (pTabItem->fg.viaCoroutine) {
		int regYield = pTabItem->regReturn;
		sqlVdbeAddOp3(v, OP_InitCoroutine, regYield, 0,
				  pTabItem->addrFillSub);
		pLevel->p2 = sqlVdbeAddOp2(v, OP_Yield, regYield, addrBrk);
		VdbeCoverage(v);
		VdbeComment((v, "next row of \"%s\"", pTabItem->space->def->name));
		pLevel->op = OP_Goto;
	} else if (pLoop->wsFlags & WHERE_INDEXED) {
		/* Case 4: A scan using an index.
		 *
		 *         The WHERE clause may contain zero or more equality
		 *         terms ("==" or "IN" operators) that refer to the N
		 *         left-most columns of the index. It may also contain
		 *         inequality constraints (>, <, >= or <=) on the indexed
		 *         column that immediately follows the N equalities. Only
		 *         the right-most column can be an inequality - the rest must
		 *         use the "==" and "IN" operators. For example, if the
		 *         index is on (x,y,z), then the following clauses are all
		 *         optimized:
		 *
		 *            x=5
		 *            x=5 AND y=10
		 *            x=5 AND y<10
		 *            x=5 AND y>5 AND y<10
		 *            x=5 AND y=5 AND z<=10
		 *
		 *         The z<10 term of the following cannot be used, only
		 *         the x=5 term:
		 *
		 *            x=5 AND z<10
		 *
		 *         N may be zero if there are inequality constraints.
		 *         If there are no inequality constraints, then N is at
		 *         least one.
		 *
		 *         This case is also used when there are no WHERE clause
		 *         constraints but an index is selected anyway, in order
		 *         to force the output order to conform to an ORDER BY.
		 */
		static const u8 aStartOp[] = {
			0,
			0,
			OP_Rewind,	/* 2: (!start_constraints && startEq &&  !bRev) */
			OP_Last,	/* 3: (!start_constraints && startEq &&   bRev) */
			OP_SeekGT,	/* 4: (start_constraints  && !startEq && !bRev) */
			OP_SeekLT,	/* 5: (start_constraints  && !startEq &&  bRev) */
			OP_SeekGE,	/* 6: (start_constraints  &&  startEq && !bRev) */
			OP_SeekLE	/* 7: (start_constraints  &&  startEq &&  bRev) */
		};
		static const u8 aEndOp[] = {
			OP_IdxGE,	/* 0: (end_constraints && !bRev && !endEq) */
			OP_IdxGT,	/* 1: (end_constraints && !bRev &&  endEq) */
			OP_IdxLE,	/* 2: (end_constraints &&  bRev && !endEq) */
			OP_IdxLT,	/* 3: (end_constraints &&  bRev &&  endEq) */
		};
		u16 nEq = pLoop->nEq;	/* Number of == or IN terms */
		u16 nBtm = pLoop->nBtm;	/* Length of BTM vector */
		u16 nTop = pLoop->nTop;	/* Length of TOP vector */
		int regBase;	/* Base register holding constraint values */
		WhereTerm *pRangeStart = 0;	/* Inequality constraint at range start */
		WhereTerm *pRangeEnd = 0;	/* Inequality constraint at range end */
		int startEq;	/* True if range start uses ==, >= or <= */
		int endEq;	/* True if range end uses ==, >= or <= */
		int start_constraints;	/* Start of range is constrained */
		int nConstraint;	/* Number of constraint terms */
		int iIdxCur;	/* The VDBE cursor for the index */
		int nExtraReg = 0;	/* Number of extra registers needed */
		int op;		/* Instruction opcode */
		/* Types for start of range constraint. */
		enum field_type *start_types;
		/* Types for end of range constraint */
		enum field_type *end_types = NULL;
		u8 bSeekPastNull = 0;	/* True to seek past initial nulls */
		u8 bStopAtNull = 0;	/* Add condition to terminate at NULLs */
		int force_integer_reg = -1;  /* If non-negative: number of
					      * column which must be converted
					      * to integer type, used for IPK.
					      */

		struct index_def *idx_def = pLoop->index_def;
		assert(idx_def != NULL);
		struct space *space = space_by_id(idx_def->space_id);
		assert(space != NULL);
		assert(space->def->field_count != 0);
		iIdxCur = pLevel->iIdxCur;
		assert(nEq >= pLoop->nSkip);

		/* If this loop satisfies a sort order (pOrderBy) request that
		 * was passed to this function to implement a "SELECT min(x) ..."
		 * query, then the caller will only allow the loop to run for
		 * a single iteration. This means that the first row returned
		 * should not have a NULL value stored in 'x'. If column 'x' is
		 * the first one after the nEq equality constraints in the index,
		 * this requires some special handling.
		 */
		assert(pWInfo->pOrderBy == 0
		       || pWInfo->pOrderBy->nExpr == 1
		       || (pWInfo->wctrlFlags & WHERE_ORDERBY_MIN) == 0);
		uint32_t part_count = idx_def->key_def->part_count;
		if ((pWInfo->wctrlFlags & WHERE_ORDERBY_MIN) != 0 &&
		    pWInfo->nOBSat > 0 && part_count > nEq) {
			j = idx_def->key_def->parts[nEq].fieldno;
			/* Allow seek for column with `NOT NULL` == false attribute.
			 * If a column may contain NULL-s, the comparator installed
			 * by Tarantool is prepared to seek using a NULL value.
			 * Otherwise, the seek will ultimately fail. Fortunately,
			 * if the column MUST NOT contain NULL-s, it suffices to
			 * fetch the very first/last value to obtain min/max.
			 *
			 * FYI: entries in an index are ordered as follows:
			 *      NULL, ... NULL, min_value, ...
			 */
			if (space->def->fields[j].is_nullable) {
				assert(pLoop->nSkip == 0);
				bSeekPastNull = 1;
				nExtraReg = 1;
			}
		}

		/* Find any inequality constraint terms for the start and end
		 * of the range.
		 */
		j = nEq;
		if (pLoop->wsFlags & WHERE_BTM_LIMIT) {
			pRangeStart = pLoop->aLTerm[j++];
			nExtraReg = MAX(nExtraReg, pLoop->nBtm);
			/* Like optimization range constraints always occur in pairs */
			assert((pRangeStart->wtFlags & TERM_LIKEOPT) == 0 ||
			       (pLoop->wsFlags & WHERE_TOP_LIMIT) != 0);
		}
		if (pLoop->wsFlags & WHERE_TOP_LIMIT) {
			pRangeEnd = pLoop->aLTerm[j++];
			nExtraReg = MAX(nExtraReg, pLoop->nTop);
			if (pRangeStart == 0) {
				j = idx_def->key_def->parts[nEq].fieldno;
				if (space->def->fields[j].is_nullable)
					bSeekPastNull = 1;
			}
		}
		assert(pRangeEnd == 0
		       || (pRangeEnd->wtFlags & TERM_VNULL) == 0);

		/* If we are doing a reverse order scan on an ascending index, or
		 * a forward order scan on a descending index, interchange the
		 * start and end terms (pRangeStart and pRangeEnd).
		 */
		if ((nEq < part_count &&
		     bRev == (idx_def->key_def->parts[nEq].sort_order ==
			      SORT_ORDER_ASC)) || (bRev && part_count == nEq)) {
			SWAP(pRangeEnd, pRangeStart);
			SWAP(bSeekPastNull, bStopAtNull);
			SWAP(nBtm, nTop);
		}

		/* Generate code to evaluate all constraint terms using == or IN
		 * and store the values of those terms in an array of registers
		 * starting at regBase.
		 */
		regBase =
		    codeAllEqualityTerms(pParse, pLevel, bRev, nExtraReg,
					 &start_types);
		if (start_types != NULL && nTop) {
			uint32_t len = 0;
			for (enum field_type *tmp = &start_types[nEq];
			     *tmp != field_type_MAX; tmp++, len++);
			uint32_t sz = len * sizeof(enum field_type);
			end_types = sqlDbMallocRaw(db, sz);
			memcpy(end_types, &start_types[nEq], sz);
		}
		addrNxt = pLevel->addrNxt;

		testcase(pRangeStart && (pRangeStart->eOperator & WO_LE) != 0);
		testcase(pRangeStart && (pRangeStart->eOperator & WO_GE) != 0);
		testcase(pRangeEnd && (pRangeEnd->eOperator & WO_LE) != 0);
		testcase(pRangeEnd && (pRangeEnd->eOperator & WO_GE) != 0);
		startEq = !pRangeStart
		    || pRangeStart->eOperator & (WO_LE | WO_GE);
		endEq = !pRangeEnd || pRangeEnd->eOperator & (WO_LE | WO_GE);
		start_constraints = pRangeStart || nEq > 0;

		/* Seek the index cursor to the start of the range. */
		nConstraint = nEq;
		if (pRangeStart) {
			Expr *pRight = pRangeStart->pExpr->pRight;
			codeExprOrVector(pParse, pRight, regBase + nEq, nBtm);

			if ((pRangeStart->wtFlags & TERM_VNULL) == 0
			    && sqlExprCanBeNull(pRight)) {
				sqlVdbeAddOp2(v, OP_IsNull, regBase + nEq,
						  addrNxt);
				VdbeCoverage(v);
			}

			if (start_types) {
				expr_cmp_update_rhs_type(pRight, nBtm,
							 &start_types[nEq]);
			}
			nConstraint += nBtm;
			testcase(pRangeStart->wtFlags & TERM_VIRTUAL);
			if (sqlExprIsVector(pRight) == 0) {
				disableTerm(pLevel, pRangeStart);
			} else {
				startEq = 1;
			}
			bSeekPastNull = 0;
		} else if (bSeekPastNull) {
			sqlVdbeAddOp2(v, OP_Null, 0, regBase + nEq);
			nConstraint++;
			startEq = 0;
			start_constraints = 1;
		}
		/*
		 * Tarantool's iterator over integer fields doesn't
		 * tolerate floating point values. Hence, if term
		 * is equality comparison and value of operand is
		 * not integer, we can skip it since it always
		 * results in false: INT a == 0.5 -> false;
		 * It is done using OP_MustBeInt facilities.
		 * In case term is greater comparison (a > ?), we
		 * should notify OP_SeekGT to process truncation of
		 * floating point value: a > 0.5 -> a >= 1;
		 * It is done by setting P5 flag for OP_Seek*.
		 * It is worth mentioning that we do not need
		 * this step when it comes for less (<) comparison
		 * of nullable field. Key is NULL in this case:
		 * values are ordered as  NULL, ... NULL, min_value,
		 * so to fetch min value we pass NULL to GT iterator.
		 * The only exception is less comparison in
		 * conjunction with ORDER BY DESC clause:
		 * in such situation we use LE iterator and
		 * truncated value to compare. But then
		 * pRangeStart == NULL.
		 * This procedure is correct for compound index:
		 * only one comparison of less/greater type can be
		 * used at the same time. For instance,
		 * a < 1.5 AND b > 0.5 is handled by SeekGT using
		 * column a and fetching column b from tuple and
		 * OP_Le comparison.
		 *
		 * Note that OP_ApplyType, which is emitted before
		 * OP_Seek** doesn't truncate floating point to
		 * integer. That's why we need this routine.
		 * Also, note that terms are separated by OR
		 * predicates, so we consider term as sequence
		 * of AND'ed predicates.
		 */
		size_t addrs_sz;
		int *seek_addrs = region_alloc_array(&pParse->region,
						     typeof(seek_addrs[0]), nEq,
						     &addrs_sz);
		if (seek_addrs == NULL) {
			diag_set(OutOfMemory, addrs_sz, "region_alloc_array",
				 "seek_addrs");
			pParse->is_aborted = true;
			return 0;
		}
		memset(seek_addrs, 0, addrs_sz);
		for (int i = 0; i < nEq; i++) {
			enum field_type type = idx_def->key_def->parts[i].type;
			if (type == FIELD_TYPE_INTEGER ||
			    type == FIELD_TYPE_UNSIGNED) {
				/*
				 * OP_MustBeInt consider NULLs as
				 * non-integer values, so firstly
				 * check whether value is NULL or not.
				 */
				seek_addrs[i] = sqlVdbeAddOp1(v, OP_IsNull,
							      regBase);
				sqlVdbeAddOp2(v, OP_MustBeInt, regBase + i,
					      addrNxt);
				start_types[i] = FIELD_TYPE_SCALAR;
				/*
				 * We need to notify column cache
				 * that type of value may change
				 * so we should fetch value from
				 * tuple again rather then copy
				 * from register.
				 */
				sql_expr_type_cache_change(pParse, regBase + i,
							   1);
			}
		}
		/* Inequality constraint comes always at the end of list. */
		part_count = idx_def->key_def->part_count;
		if (pRangeStart != NULL) {
			/*
			 * nEq == 0 means that filter condition
			 * contains only inequality.
			 */
			uint32_t ineq_idx = nEq == 0 ? 0 : nEq - 1;
			assert(ineq_idx < part_count);
			enum field_type ineq_type =
				idx_def->key_def->parts[ineq_idx].type;
			if (ineq_type == FIELD_TYPE_INTEGER ||
			    ineq_type == FIELD_TYPE_UNSIGNED)
				force_integer_reg = regBase + nEq;
		}
		emit_apply_type(pParse, regBase, nConstraint - bSeekPastNull,
				start_types);
		if (pLoop->nSkip > 0 && nConstraint == pLoop->nSkip) {
			/* The skip-scan logic inside the call to codeAllEqualityConstraints()
			 * above has already left the cursor sitting on the correct row,
			 * so no further seeking is needed
			 */
		} else {
			op = aStartOp[(start_constraints << 2) +
				      (startEq << 1) + bRev];
			assert(op != 0);
			for (uint32_t i = 0; i < nEq; ++i) {
				if (seek_addrs[i] != 0)
					sqlVdbeJumpHere(v, seek_addrs[i]);
			}
			sqlVdbeAddOp4Int(v, op, iIdxCur, addrNxt, regBase,
					     nConstraint);
			/* If this is Seek* opcode, and IPK is detected in the
			 * constraints vector: force it to be integer.
			 */
			if ((op == OP_SeekGE || op == OP_SeekGT
			    || op == OP_SeekLE || op == OP_SeekLT)
			    && force_integer_reg > 0) {
				sqlVdbeChangeP5(v, force_integer_reg);
			}
			VdbeCoverage(v);
			VdbeCoverageIf(v, op == OP_Rewind);
			testcase(op == OP_Rewind);
			VdbeCoverageIf(v, op == OP_Last);
			testcase(op == OP_Last);
			VdbeCoverageIf(v, op == OP_SeekGT);
			testcase(op == OP_SeekGT);
			VdbeCoverageIf(v, op == OP_SeekGE);
			testcase(op == OP_SeekGE);
			VdbeCoverageIf(v, op == OP_SeekLE);
			testcase(op == OP_SeekLE);
			VdbeCoverageIf(v, op == OP_SeekLT);
			testcase(op == OP_SeekLT);
		}

		/* Load the value for the inequality constraint at the end of the
		 * range (if any).
		 */
		nConstraint = nEq;
		if (pRangeEnd) {
			Expr *pRight = pRangeEnd->pExpr->pRight;
			sqlExprCacheRemove(pParse, regBase + nEq, 1);
			codeExprOrVector(pParse, pRight, regBase + nEq, nTop);
			if ((pRangeEnd->wtFlags & TERM_VNULL) == 0
			    && sqlExprCanBeNull(pRight)) {
				sqlVdbeAddOp2(v, OP_IsNull, regBase + nEq,
						  addrNxt);
				VdbeCoverage(v);
			}
			if (end_types) {
				expr_cmp_update_rhs_type(pRight, nTop, end_types);
				emit_apply_type(pParse, regBase + nEq, nTop,
						end_types);
			} else {
				assert(pParse->db->mallocFailed);
			}
			nConstraint += nTop;
			testcase(pRangeEnd->wtFlags & TERM_VIRTUAL);

			if (sqlExprIsVector(pRight) == 0) {
				disableTerm(pLevel, pRangeEnd);
			} else {
				endEq = 1;
			}
		} else if (bStopAtNull) {
			sqlVdbeAddOp2(v, OP_Null, 0, regBase + nEq);
			endEq = 0;
			nConstraint++;
		}
		sqlDbFree(db, start_types);
		sqlDbFree(db, end_types);

		/* Top of the loop body */
		pLevel->p2 = sqlVdbeCurrentAddr(v);

		/* Check if the index cursor is past the end of the range. */
		if (nConstraint) {
			op = aEndOp[bRev * 2 + endEq];
			sqlVdbeAddOp4Int(v, op, iIdxCur, addrNxt, regBase,
					     nConstraint);
			testcase(op == OP_IdxGT);
			VdbeCoverageIf(v, op == OP_IdxGT);
			testcase(op == OP_IdxGE);
			VdbeCoverageIf(v, op == OP_IdxGE);
			testcase(op == OP_IdxLT);
			VdbeCoverageIf(v, op == OP_IdxLT);
			testcase(op == OP_IdxLE);
			VdbeCoverageIf(v, op == OP_IdxLE);
		}

		/* Seek the table cursor, if required */
		if (omitTable) {
			/* pIdx is a covering index.  No need to access the main table. */
		}  else if (iCur != iIdxCur) {
			struct index_def *idx_pk = space->index[0]->def;
			uint32_t pk_part_count = idx_pk->key_def->part_count;
			int iKeyReg = sqlGetTempRange(pParse, pk_part_count);
			for (j = 0; j < (int) pk_part_count; j++) {
				k = idx_pk->key_def->parts[j].fieldno;
				sqlVdbeAddOp3(v, OP_Column, iIdxCur, k,
						  iKeyReg + j);
			}
			sqlVdbeAddOp4Int(v, OP_NotFound, iCur, addrCont,
					     iKeyReg, pk_part_count);
			VdbeCoverage(v);
			sqlReleaseTempRange(pParse, iKeyReg, pk_part_count);
		}

		/* Record the instruction used to terminate the loop. */
		if (pLoop->wsFlags & WHERE_ONEROW) {
			pLevel->op = OP_Noop;
		} else if (bRev) {
			pLevel->op = OP_Prev;
		} else {
			pLevel->op = OP_Next;
		}
		pLevel->p1 = iIdxCur;
		pLevel->p3 = (pLoop->wsFlags & WHERE_UNQ_WANTED) != 0 ? 1 : 0;
		if ((pLoop->wsFlags & WHERE_CONSTRAINT) == 0) {
			pLevel->p5 = SQL_STMTSTATUS_FULLSCAN_STEP;
		} else {
			assert(pLevel->p5 == 0);
		}
	} else if (pLoop->wsFlags & WHERE_MULTI_OR) {
		/* Case 5:  Two or more separately indexed terms connected by OR
		 *
		 * Example:
		 *
		 *   CREATE TABLE t1(a,b,c,d);
		 *   CREATE INDEX i1 ON t1(a);
		 *   CREATE INDEX i2 ON t1(b);
		 *   CREATE INDEX i3 ON t1(c);
		 *
		 *   SELECT * FROM t1 WHERE a=5 OR b=7 OR (c=11 AND d=13)
		 *
		 * In the example, there are three indexed terms connected by OR.
		 * In this case, use an ephemeral index to record the primary
		 * keys of the rows we have already seen.
		 */
		WhereClause *pOrWc;	/* The OR-clause broken out into subterms */
		SrcList *pOrTab;	/* Shortened table list or OR-clause generation */
		struct index_def *cov = NULL;	/* Potential covering index (or NULL) */
		int iCovCur = pParse->nTab++;	/* Cursor used for index scans (if any) */

		int regReturn = ++pParse->nMem;	/* Register used with OP_Gosub */
		int cur_row_set = 0;
		int reg_row_set = 0;
		int regPk = 0;	/* Register holding PK */
		int iLoopBody = sqlVdbeMakeLabel(v);	/* Start of loop body */
		int iRetInit;	/* Address of regReturn init */
		int untestedTerms = 0;	/* Some terms not completely tested */
		int ii;		/* Loop counter */
		u16 wctrlFlags;	/* Flags for sub-WHERE clause */
		Expr *pAndExpr = 0;	/* An ".. AND (...)" expression */
		struct space *space = pTabItem->space;
		struct key_def *pk_key_def =
			space_index(space, 0)->def->key_def;
		uint32_t pk_part_count = pk_key_def->part_count;

		pTerm = pLoop->aLTerm[0];
		assert(pTerm != 0);
		assert(pTerm->eOperator & WO_OR);
		assert((pTerm->wtFlags & TERM_ORINFO) != 0);
		pOrWc = &pTerm->u.pOrInfo->wc;
		pLevel->op = OP_Return;
		pLevel->p1 = regReturn;

		/* Set up a new SrcList in pOrTab containing the table being scanned
		 * by this loop in the a[0] slot and all notReady tables in a[1..] slots.
		 * This becomes the SrcList in the recursive call to sqlWhereBegin().
		 */
		if (pWInfo->nLevel > 1) {
			int nNotReady;	/* The number of notReady tables */
			struct SrcList_item *origSrc;	/* Original list of tables */
			nNotReady = pWInfo->nLevel - iLevel - 1;
			pOrTab = sqlStackAllocRaw(db,
						      sizeof(*pOrTab) +
						      nNotReady *
						      sizeof(pOrTab->a[0]));
			if (pOrTab == 0)
				return notReady;
			pOrTab->nAlloc = (u8) (nNotReady + 1);
			pOrTab->nSrc = pOrTab->nAlloc;
			memcpy(pOrTab->a, pTabItem, sizeof(*pTabItem));
			origSrc = pWInfo->pTabList->a;
			for (k = 1; k <= nNotReady; k++) {
				memcpy(&pOrTab->a[k], &origSrc[pLevel[k].iFrom],
				       sizeof(pOrTab->a[k]));
			}
		} else {
			pOrTab = pWInfo->pTabList;
		}

		/* Create an ephemeral index capable of holding primary keys.
		 *
		 * Also initialize regReturn to contain the address of the instruction
		 * immediately following the OP_Return at the bottom of the loop. This
		 * is required in a few obscure LEFT JOIN cases where control jumps
		 * over the top of the loop into the body of it. In this case the
		 * correct response for the end-of-loop code (the OP_Return) is to
		 * fall through to the next instruction, just as an OP_Next does if
		 * called on an uninitialized cursor.
		 */
		if ((pWInfo->wctrlFlags & WHERE_DUPLICATES_OK) == 0) {
			cur_row_set = pParse->nTab++;
			reg_row_set = ++pParse->nMem;
			sqlVdbeAddOp2(v, OP_OpenTEphemeral,
					  reg_row_set, pk_part_count);
			sqlVdbeAddOp3(v, OP_IteratorOpen, cur_row_set, 0,
					  reg_row_set);
			sql_vdbe_set_p4_key_def(pParse, pk_key_def);
			regPk = ++pParse->nMem;
		}
		iRetInit = sqlVdbeAddOp2(v, OP_Integer, 0, regReturn);

		/* If the original WHERE clause is z of the form:  (x1 OR x2 OR ...) AND y
		 * Then for every term xN, evaluate as the subexpression: xN AND z
		 * That way, terms in y that are factored into the disjunction will
		 * be picked up by the recursive calls to sqlWhereBegin() below.
		 *
		 * Actually, each subexpression is converted to "xN AND w" where w is
		 * the "interesting" terms of z - terms that did not originate in the
		 * ON or USING clause of a LEFT JOIN, and terms that are usable as
		 * indices.
		 *
		 * This optimization also only applies if the (x1 OR x2 OR ...) term
		 * is not contained in the ON clause of a LEFT JOIN.
		 */
		if (pWC->nTerm > 1) {
			int iTerm;
			for (iTerm = 0; iTerm < pWC->nTerm; iTerm++) {
				Expr *pExpr = pWC->a[iTerm].pExpr;
				if (&pWC->a[iTerm] == pTerm)
					continue;
				if (ExprHasProperty(pExpr, EP_FromJoin))
					continue;
				testcase(pWC->a[iTerm].wtFlags & TERM_VIRTUAL);
				testcase(pWC->a[iTerm].wtFlags & TERM_CODED);
				if ((pWC->a[iTerm].
				     wtFlags & (TERM_VIRTUAL | TERM_CODED)) !=
				    0)
					continue;
				if ((pWC->a[iTerm].eOperator & WO_ALL) == 0)
					continue;
				testcase(pWC->a[iTerm].wtFlags & TERM_ORINFO);
				pExpr = sqlExprDup(db, pExpr, 0);
				pAndExpr = sql_and_expr_new(db, pAndExpr,
							    pExpr);
				if (pAndExpr == NULL)
					pParse->is_aborted = true;
			}
			if (pAndExpr) {
				pAndExpr =
				    sqlPExpr(pParse,
						 TK_AND | TKFLG_DONTFOLD, 0,
						 pAndExpr);
			}
		}

		/* Run a separate WHERE clause for each term of the OR clause.  After
		 * eliminating duplicates from other WHERE clauses, the action for each
		 * sub-WHERE clause is to to invoke the main loop body as a subroutine.
		 */
		wctrlFlags =
		    WHERE_OR_SUBCLAUSE | (pWInfo->
					  wctrlFlags & WHERE_SEEK_TABLE);
		for (ii = 0; ii < pOrWc->nTerm; ii++) {
			WhereTerm *pOrTerm = &pOrWc->a[ii];
			if (pOrTerm->leftCursor == iCur
			    || (pOrTerm->eOperator & WO_AND) != 0) {
				WhereInfo *pSubWInfo;	/* Info for single OR-term scan */
				Expr *pOrExpr = pOrTerm->pExpr;	/* Current OR clause term */
				int jmp1 = 0;	/* Address of jump operation */
				if (pAndExpr
				    && !ExprHasProperty(pOrExpr, EP_FromJoin)) {
					pAndExpr->pLeft = pOrExpr;
					pOrExpr = pAndExpr;
				}
				/* Loop through table entries that match term pOrTerm. */
				WHERETRACE(0xffff,
					   ("Subplan for OR-clause:\n"));
				pSubWInfo =
				    sqlWhereBegin(pParse, pOrTab, pOrExpr,
						      0, 0, wctrlFlags,
						      iCovCur);
				assert(pSubWInfo || pParse->is_aborted
				       || db->mallocFailed);
				if (pSubWInfo) {
					WhereLoop *pSubLoop;
				    	sqlWhereExplainOneScan(pParse,
							       pOrTab,
							       &pSubWInfo->a[0],
							       iLevel,
							       pLevel->iFrom,
							       0);

					/* This is the sub-WHERE clause body.  First skip over
					 * duplicate rows from prior sub-WHERE clauses, and record the
					 * PRIMARY KEY for the current row so that the same
					 * row will be skipped in subsequent sub-WHERE clauses.
					 */
					if ((pWInfo->
					     wctrlFlags & WHERE_DUPLICATES_OK)
					    == 0) {
						int r;
						int iSet =
						    ((ii == pOrWc->nTerm - 1) ? -1 : ii);

						/* Read the PK into an array of temp registers. */
						r = sqlGetTempRange(pParse,
									pk_part_count);
						for (uint32_t iPk = 0;
						     iPk < pk_part_count;
						     iPk++) {
							uint32_t fieldno =
								pk_key_def->parts[iPk].
								fieldno;
							sqlExprCodeGetColumnToReg
								(pParse,
								 fieldno,
								 iCur,
								 r + iPk);
						}

						/* Check if the temp table already contains this key. If so,
						 * the row has already been included in the result set and
						 * can be ignored (by jumping past the Gosub below). Otherwise,
						 * insert the key into the temp table and proceed with processing
						 * the row.
						 *
						 * Use optimizations: If iSet
						 * is zero, assume that the key cannot already be present in
						 * the temp table. And if iSet is -1, assume that there is no
						 * need to insert the key into the temp table, as it will never
						 * be tested for.
						 */
						if (iSet) {
							jmp1 = sqlVdbeAddOp4Int
								(v, OP_Found,
								 cur_row_set, 0,
								 r,
								 pk_part_count);
							VdbeCoverage(v);
						}
						if (iSet >= 0) {
							sqlVdbeAddOp3
								(v, OP_MakeRecord,
								 r, pk_part_count, regPk);
							sqlVdbeAddOp2
								(v, OP_IdxInsert,
								 regPk, reg_row_set);
						}

						/* Release the array of temp registers */
						sqlReleaseTempRange(pParse, r, pk_part_count);
					}

					/* Invoke the main loop body as a subroutine */
					sqlVdbeAddOp2(v, OP_Gosub,
							  regReturn, iLoopBody);

					/* Jump here (skipping the main loop body subroutine) if the
					 * current sub-WHERE row is a duplicate from prior sub-WHEREs.
					 */
					if (jmp1)
						sqlVdbeJumpHere(v, jmp1);

					/* The pSubWInfo->untestedTerms flag means that this OR term
					 * contained one or more AND term from a notReady table.  The
					 * terms from the notReady table could not be tested and will
					 * need to be tested later.
					 */
					if (pSubWInfo->untestedTerms)
						untestedTerms = 1;

					/* If all of the OR-connected terms are optimized using the same
					 * index, and the index is opened using the same cursor number
					 * by each call to sqlWhereBegin() made by this loop, it may
					 * be possible to use that index as a covering index.
					 *
					 * If the call to sqlWhereBegin() above resulted in a scan that
					 * uses an index, and this is either the first OR-connected term
					 * processed or the index is the same as that used by all previous
					 * terms, set cov to the candidate covering index. Otherwise, set
					 * cov to NULL to indicate that no candidate covering index will
					 * be available.
					 */
					pSubLoop = pSubWInfo->a[0].pWLoop;
					assert((pSubLoop->wsFlags & WHERE_AUTO_INDEX) == 0);
					if ((pSubLoop->wsFlags & WHERE_INDEXED) != 0
					    && (ii == 0 || (cov != NULL &&
						pSubLoop->index_def->iid == cov->iid))
					    && (pSubLoop->index_def->iid != 0)) {
						assert(pSubWInfo->a[0].
						       iIdxCur == iCovCur);
						cov = pSubLoop->index_def;
					} else {
						cov = 0;
					}

					/* Finish the loop through table entries that match term pOrTerm. */
					sqlWhereEnd(pSubWInfo);
				}
			}
		}
		pLevel->u.pCovidx = cov;
		if (cov)
			pLevel->iIdxCur = iCovCur;
		if (pAndExpr) {
			pAndExpr->pLeft = 0;
			sql_expr_delete(db, pAndExpr, false);
		}
		sqlVdbeChangeP1(v, iRetInit, sqlVdbeCurrentAddr(v));
		sqlVdbeGoto(v, pLevel->addrBrk);
		sqlVdbeResolveLabel(v, iLoopBody);

		if (pWInfo->nLevel > 1)
			sqlStackFree(db, pOrTab);
		if (!untestedTerms)
			disableTerm(pLevel, pTerm);
	} else

	{
		/* Case 6:  There is no usable index.  We must do a complete
		 *          scan of the entire table.
		 */
		static const u8 aStep[] = { OP_Next, OP_Prev };
		static const u8 aStart[] = { OP_Rewind, OP_Last };
		assert(bRev == 0 || bRev == 1);
		if (pTabItem->fg.isRecursive) {
			/* Tables marked isRecursive have only a single row that is stored in
			 * a pseudo-cursor.  No need to Rewind or Next such cursors.
			 */
			pLevel->op = OP_Noop;
		} else {
			pLevel->op = aStep[bRev];
			pLevel->p1 = iCur;
			pLevel->p2 =
			    1 + sqlVdbeAddOp2(v, aStart[bRev], iCur,
						  addrBrk);
			VdbeCoverageIf(v, bRev == 0);
			VdbeCoverageIf(v, bRev != 0);
			pLevel->p5 = SQL_STMTSTATUS_FULLSCAN_STEP;
		}
	}

	/* Insert code to test every subexpression that can be completely
	 * computed using the current set of tables.
	 */
	for (pTerm = pWC->a, j = pWC->nTerm; j > 0; j--, pTerm++) {
		Expr *pE;
		int skipLikeAddr = 0;
		testcase(pTerm->wtFlags & TERM_VIRTUAL);
		testcase(pTerm->wtFlags & TERM_CODED);
		if (pTerm->wtFlags & (TERM_VIRTUAL | TERM_CODED))
			continue;
		if ((pTerm->prereqAll & pLevel->notReady) != 0) {
			testcase(pWInfo->untestedTerms == 0
				 && (pWInfo->wctrlFlags & WHERE_OR_SUBCLAUSE) !=
				 0);
			pWInfo->untestedTerms = 1;
			continue;
		}
		pE = pTerm->pExpr;
		assert(pE != 0);
		if (pLevel->iLeftJoin && !ExprHasProperty(pE, EP_FromJoin)) {
			continue;
		}
		if (pTerm->wtFlags & TERM_LIKECOND) {
			/* If the TERM_LIKECOND flag is set, that means that the range search
			 * is sufficient to guarantee that the LIKE operator is true, so we
			 * can skip the call to the like(A,B) function.  But this only works
			 * for strings.  So do not skip the call to the function on the pass
			 * that compares BLOBs.
			 */
			continue;
		}
		sqlExprIfFalse(pParse, pE, addrCont, SQL_JUMPIFNULL);
		if (skipLikeAddr)
			sqlVdbeJumpHere(v, skipLikeAddr);
		pTerm->wtFlags |= TERM_CODED;
	}

	/* Insert code to test for implied constraints based on transitivity
	 * of the "==" operator.
	 *
	 * Example: If the WHERE clause contains "t1.a=t2.b" and "t2.b=123"
	 * and we are coding the t1 loop and the t2 loop has not yet coded,
	 * then we cannot use the "t1.a=t2.b" constraint, but we can code
	 * the implied "t1.a=123" constraint.
	 */
	for (pTerm = pWC->a, j = pWC->nTerm; j > 0; j--, pTerm++) {
		Expr *pE, sEAlt;
		WhereTerm *pAlt;
		if (pTerm->wtFlags & (TERM_VIRTUAL | TERM_CODED))
			continue;
		if ((pTerm->eOperator & WO_EQ) == 0)
			continue;
		if ((pTerm->eOperator & WO_EQUIV) == 0)
			continue;
		if (pTerm->leftCursor != iCur)
			continue;
		if (pLevel->iLeftJoin)
			continue;
		pE = pTerm->pExpr;
		assert(!ExprHasProperty(pE, EP_FromJoin));
		assert((pTerm->prereqRight & pLevel->notReady) != 0);
		pAlt =
		    sqlWhereFindTerm(pWC, iCur, pTerm->u.leftColumn,
					 notReady, WO_EQ | WO_IN, 0);
		if (pAlt == 0)
			continue;
		if (pAlt->wtFlags & (TERM_CODED))
			continue;
		testcase(pAlt->eOperator & WO_EQ);
		testcase(pAlt->eOperator & WO_IN);
		VdbeModuleComment((v, "begin transitive constraint"));
		sEAlt = *pAlt->pExpr;
		sEAlt.pLeft = pE->pLeft;
		sqlExprIfFalse(pParse, &sEAlt, addrCont, SQL_JUMPIFNULL);
	}

	/* For a LEFT OUTER JOIN, generate code that will record the fact that
	 * at least one row of the right table has matched the left table.
	 */
	if (pLevel->iLeftJoin) {
		pLevel->addrFirst = sqlVdbeCurrentAddr(v);
		sqlVdbeAddOp2(v, OP_Integer, 1, pLevel->iLeftJoin);
		VdbeComment((v, "record LEFT JOIN hit"));
		sqlExprCacheClear(pParse);
		for (pTerm = pWC->a, j = 0; j < pWC->nTerm; j++, pTerm++) {
			testcase(pTerm->wtFlags & TERM_VIRTUAL);
			testcase(pTerm->wtFlags & TERM_CODED);
			if (pTerm->wtFlags & (TERM_VIRTUAL | TERM_CODED))
				continue;
			if ((pTerm->prereqAll & pLevel->notReady) != 0) {
				assert(pWInfo->untestedTerms);
				continue;
			}
			assert(pTerm->pExpr);
			sqlExprIfFalse(pParse, pTerm->pExpr, addrCont,
					   SQL_JUMPIFNULL);
			pTerm->wtFlags |= TERM_CODED;
		}
	}

	return pLevel->notReady;
}
