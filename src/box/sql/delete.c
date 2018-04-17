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
 * This file contains C code routines that are called by the parser
 * in order to generate code for DELETE FROM statements.
 */
#include "sqliteInt.h"
#include "box/session.h"

/*
 * While a SrcList can in general represent multiple tables and subqueries
 * (as in the FROM clause of a SELECT statement) in this case it contains
 * the name of a single table, as one might find in an INSERT, DELETE,
 * or UPDATE statement.  Look up that table in the symbol table and
 * return a pointer.  Set an error message and return NULL if the table
 * name is not found or if any other error occurs.
 *
 * The following fields are initialized appropriate in pSrc:
 *
 *    pSrc->a[0].pTab       Pointer to the Table object
 *    pSrc->a[0].pIndex     Pointer to the INDEXED BY index, if there is one
 *
 */
Table *
sqlite3SrcListLookup(Parse * pParse, SrcList * pSrc)
{
	struct SrcList_item *pItem = pSrc->a;
	Table *pTab;
	assert(pItem && pSrc->nSrc == 1);
	pTab = sqlite3LocateTable(pParse, 0, pItem->zName);
	sqlite3DeleteTable(pParse->db, pItem->pTab);
	pItem->pTab = pTab;
	if (pTab != NULL)
		pTab->nTabRef++;
	if (sqlite3IndexedByLookup(pParse, pItem))
		pTab = NULL;
	return pTab;
}

/*
 * Check to make sure the given table is writable.  If it is not
 * writable, generate an error message and return 1.  If it is
 * writable return 0;
 */
int
sqlite3IsReadOnly(Parse * pParse, Table * pTab, int viewOk)
{
	/*
	 * A table is not writable if it is a system table
	 * (i.e. _sql_stat1), this call is not part of a
	 * nested parse. In either case leave an error message in
	 * pParse and return non-zero.
	 */
	if ((pTab->tabFlags & TF_Readonly) != 0 && pParse->nested == 0) {
		sqlite3ErrorMsg(pParse, "table %s may not be modified",
				pTab->zName);
		return 1;
	}
#ifndef SQLITE_OMIT_VIEW
	if (!viewOk && space_is_view(pTab)) {
		sqlite3ErrorMsg(pParse, "cannot modify %s because it is a view",
				pTab->zName);
		return 1;
	}
#endif
	return 0;
}

#if !defined(SQLITE_OMIT_VIEW) && !defined(SQLITE_OMIT_TRIGGER)
/*
 * Evaluate a view and store its result in an ephemeral table.  The
 * pWhere argument is an optional WHERE clause that restricts the
 * set of rows in the view that are to be added to the ephemeral table.
 */
void
sqlite3MaterializeView(Parse * pParse,	/* Parsing context */
		       Table * pView,	/* View definition */
		       Expr * pWhere,	/* Optional WHERE clause to be added */
		       int iCur)	/* Cursor number for ephemeral table */
{
	SelectDest dest;
	Select *pSel;
	SrcList *pFrom;
	sqlite3 *db = pParse->db;
	pWhere = sqlite3ExprDup(db, pWhere, 0);
	pFrom = sqlite3SrcListAppend(db, 0, 0);
	if (pFrom) {
		assert(pFrom->nSrc == 1);
		pFrom->a[0].zName = sqlite3DbStrDup(db, pView->zName);
		assert(pFrom->a[0].pOn == 0);
		assert(pFrom->a[0].pUsing == 0);
	}
	pSel = sqlite3SelectNew(pParse, 0, pFrom, pWhere, 0, 0, 0,
				0, 0, 0);
	sqlite3SelectDestInit(&dest, SRT_EphemTab, iCur);
	sqlite3Select(pParse, pSel, &dest);
	sqlite3SelectDelete(db, pSel);
}
#endif				/* !defined(SQLITE_OMIT_VIEW) && !defined(SQLITE_OMIT_TRIGGER) */

#if defined(SQLITE_ENABLE_UPDATE_DELETE_LIMIT)
/*
 * Generate an expression tree to implement the WHERE, ORDER BY,
 * and LIMIT/OFFSET portion of DELETE and UPDATE statements.
 *
 *     DELETE FROM table_wxyz WHERE a<5 ORDER BY a LIMIT 1;
 *                            \__________________________/
 *                               pLimitWhere (pInClause)
 */
Expr *
sqlite3LimitWhere(Parse * pParse,	/* The parser context */
		  SrcList * pSrc,	/* the FROM clause -- which tables to scan */
		  Expr * pWhere,	/* The WHERE clause.  May be null */
		  ExprList * pOrderBy,	/* The ORDER BY clause.  May be null */
		  Expr * pLimit,	/* The LIMIT clause.  May be null */
		  Expr * pOffset,	/* The OFFSET clause.  May be null */
		  char *zStmtType	/* Either DELETE or UPDATE.  For err msgs. */
    )
{
	Expr *pWhereRowid = NULL;	/* WHERE rowid .. */
	Expr *pInClause = NULL;	/* WHERE rowid IN ( select ) */
	Expr *pSelectRowid = NULL;	/* SELECT rowid ... */
	ExprList *pEList = NULL;	/* Expression list contaning only pSelectRowid */
	SrcList *pSelectSrc = NULL;	/* SELECT rowid FROM x ... (dup of pSrc) */
	Select *pSelect = NULL;	/* Complete SELECT tree */

	/* Check that there isn't an ORDER BY without a LIMIT clause.
	 */
	if (pOrderBy && (pLimit == 0)) {
		sqlite3ErrorMsg(pParse, "ORDER BY without LIMIT on %s",
				zStmtType);
		goto limit_where_cleanup;
	}

	/* We only need to generate a select expression if there
	 * is a limit/offset term to enforce.
	 */
	if (pLimit == 0) {
		/* if pLimit is null, pOffset will always be null as well. */
		assert(pOffset == 0);
		return pWhere;
	}

	/* Generate a select expression tree to enforce the limit/offset
	 * term for the DELETE or UPDATE statement.  For example:
	 *   DELETE FROM table_a WHERE col1=1 ORDER BY col2 LIMIT 1 OFFSET 1
	 * becomes:
	 *   DELETE FROM table_a WHERE rowid IN (
	 *     SELECT rowid FROM table_a WHERE col1=1 ORDER BY col2 LIMIT 1 OFFSET 1
	 *   );
	 */

	pSelectRowid = sqlite3PExpr(pParse, TK_ROW, 0, 0);
	if (pSelectRowid == 0)
		goto limit_where_cleanup;
	pEList = sqlite3ExprListAppend(pParse, 0, pSelectRowid);
	if (pEList == 0)
		goto limit_where_cleanup;

	/* duplicate the FROM clause as it is needed by both the DELETE/UPDATE tree
	 * and the SELECT subtree.
	 */
	pSelectSrc = sqlite3SrcListDup(pParse->db, pSrc, 0);
	if (pSelectSrc == 0) {
		sqlite3ExprListDelete(pParse->db, pEList);
		goto limit_where_cleanup;
	}

	/* generate the SELECT expression tree. */
	pSelect = sqlite3SelectNew(pParse, pEList, pSelectSrc, pWhere, 0, 0,
				   pOrderBy, 0, pLimit, pOffset);
	if (pSelect == 0)
		return 0;

	/* now generate the new WHERE rowid IN clause for the DELETE/UDPATE */
	pWhereRowid = sqlite3PExpr(pParse, TK_ROW, 0, 0);
	pInClause =
	    pWhereRowid ? sqlite3PExpr(pParse, TK_IN, pWhereRowid, 0) : 0;
	sqlite3PExprAddSelect(pParse, pInClause, pSelect);
	return pInClause;

 limit_where_cleanup:
	sql_expr_free(pParse->db, pWhere);
	sqlite3ExprListDelete(pParse->db, pOrderBy);
	sql_expr_free(pParse->db, pLimit);
	sql_expr_free(pParse->db, pOffset);
	return 0;
}
#endif				/* defined(SQLITE_ENABLE_UPDATE_DELETE_LIMIT) */

/*
 * Generate code for a DELETE FROM statement.
 *
 *     DELETE FROM table_wxyz WHERE a<5 AND b NOT NULL;
 *                 \________/       \________________/
 *                  pTabList              pWhere
 */
void
sqlite3DeleteFrom(Parse * pParse,	/* The parser context */
		  SrcList * pTabList,	/* The table from which we should delete things */
		  Expr * pWhere)	/* The WHERE clause.  May be null */
{
	Vdbe *v;		/* The virtual database engine */
	Table *pTab;		/* The table from which records will be deleted */
	int i;			/* Loop counter */
	WhereInfo *pWInfo;	/* Information about the WHERE clause */
	Index *pIdx;		/* For looping over indices of the table */
	int iTabCur;		/* Cursor number for the table */
	int iDataCur = 0;	/* VDBE cursor for the canonical data source */
	int iIdxCur = 0;	/* Cursor number of the first index */
	int nIdx;		/* Number of indices */
	sqlite3 *db;		/* Main database structure */
	NameContext sNC;	/* Name context to resolve expressions in */
	int memCnt = -1;	/* Memory cell used for change counting */
	int eOnePass;		/* ONEPASS_OFF or _SINGLE or _MULTI */
	int aiCurOnePass[2];	/* The write cursors opened by WHERE_ONEPASS */
	u8 *aToOpen = 0;	/* Open cursor iTabCur+j if aToOpen[j] is true */
	Index *pPk = 0;		/* The PRIMARY KEY index on the table */
	int iPk = 0;		/* First of nPk registers holding PRIMARY KEY value */
	i16 nPk;		/* Number of columns in the PRIMARY KEY */
	int iKey;		/* Memory cell holding key of row to be deleted */
	i16 nKey;		/* Number of memory cells in the row key */
	int iEphCur = 0;	/* Ephemeral table holding all primary key values */
	int addrBypass = 0;	/* Address of jump over the delete logic */
	int addrLoop = 0;	/* Top of the delete loop */
	int addrEphOpen = 0;	/* Instruction to open the Ephemeral table */
	int bComplex;		/* True if there are triggers or FKs or
				 * subqueries in the WHERE clause
				 */
	struct session *user_session = current_session();

#ifndef SQLITE_OMIT_TRIGGER
	int isView;		/* True if attempting to delete from a view */
	Trigger *pTrigger;	/* List of table triggers, if required */
#endif

	db = pParse->db;
	if (pParse->nErr || db->mallocFailed) {
		goto delete_from_cleanup;
	}
	assert(pTabList->nSrc == 1);

	/* Locate the table which we want to delete.  This table has to be
	 * put in an SrcList structure because some of the subroutines we
	 * will be calling are designed to work with multiple tables and expect
	 * an SrcList* parameter instead of just a Table* parameter.
	 */
	pTab = sqlite3SrcListLookup(pParse, pTabList);
	if (pTab == 0)
		goto delete_from_cleanup;

	/* Figure out if we have any triggers and if the table being
	 * deleted from is a view
	 */
#ifndef SQLITE_OMIT_TRIGGER
	pTrigger = sqlite3TriggersExist(pTab, TK_DELETE, 0, 0);
	isView = pTab->pSelect != 0;
	bComplex = pTrigger || sqlite3FkRequired(pTab, 0);
#else
#define pTrigger 0
#define isView 0
#endif
#ifdef SQLITE_OMIT_VIEW
#undef isView
#define isView 0
#endif

	/* If pTab is really a view, make sure it has been initialized.
	 */
	if (sqlite3ViewGetColumnNames(pParse, pTab)) {
		goto delete_from_cleanup;
	}

	if (sqlite3IsReadOnly(pParse, pTab, (pTrigger ? 1 : 0))) {
		goto delete_from_cleanup;
	}
	assert(!isView || pTrigger);

	/* Assign cursor numbers to the table and all its indices.
	 */
	assert(pTabList->nSrc == 1);
	iTabCur = pTabList->a[0].iCursor = pParse->nTab++;
	for (nIdx = 0, pIdx = pTab->pIndex; pIdx; pIdx = pIdx->pNext, nIdx++) {
		pParse->nTab++;
	}

	/* Begin generating code.
	 */
	v = sqlite3GetVdbe(pParse);
	if (v == 0) {
		goto delete_from_cleanup;
	}
	if (pParse->nested == 0)
		sqlite3VdbeCountChanges(v);
	sql_set_multi_write(pParse, true);

	/* If we are trying to delete from a view, realize that view into
	 * an ephemeral table.
	 */
#if !defined(SQLITE_OMIT_VIEW) && !defined(SQLITE_OMIT_TRIGGER)
	if (isView) {
		sqlite3MaterializeView(pParse, pTab, pWhere, iTabCur);
		iDataCur = iIdxCur = iTabCur;
	}
#endif

	/* Resolve the column names in the WHERE clause.
	 */
	memset(&sNC, 0, sizeof(sNC));
	sNC.pParse = pParse;
	sNC.pSrcList = pTabList;
	if (sqlite3ResolveExprNames(&sNC, pWhere)) {
		goto delete_from_cleanup;
	}

	/* Initialize the counter of the number of rows deleted, if
	 * we are counting rows.
	 */
	if (user_session->sql_flags & SQLITE_CountRows) {
		memCnt = ++pParse->nMem;
		sqlite3VdbeAddOp2(v, OP_Integer, 0, memCnt);
	}
#ifndef SQLITE_OMIT_TRUNCATE_OPTIMIZATION
	/* Special case: A DELETE without a WHERE clause deletes everything.
	 * It is easier just to erase the whole table. Prior to version 3.6.5,
	 * this optimization caused the row change count (the value returned by
	 * API function sqlite3_count_changes) to be set incorrectly.
	 */
	if (pWhere == 0 && !bComplex
#ifdef SQLITE_ENABLE_PREUPDATE_HOOK
	    && db->xPreUpdateCallback == 0
#endif
	    ) {
		assert(!isView);

		sqlite3VdbeAddOp1(v, OP_Clear, pTab->tnum);

		/* Do not start Tarantool's transaction in case of truncate optimization.
		   This is workaround until system tables cannot be changes inside a
		   transaction (_truncate).  */
		pParse->initiateTTrans = false;
	} else
#endif				/* SQLITE_OMIT_TRUNCATE_OPTIMIZATION */
	{
		u16 wcf =
		    WHERE_ONEPASS_DESIRED | WHERE_DUPLICATES_OK |
		    WHERE_SEEK_TABLE;
		if (sNC.ncFlags & NC_VarSelect)
			bComplex = 1;
		wcf |= (bComplex ? 0 : WHERE_ONEPASS_MULTIROW);
		/* Create an ephemeral table used to hold all primary keys for
		 * rows to be deleted. Since VIEW is held in ephemeral table,
		 * there is no PK for it, so columns should be loaded manually.
		 */
		if (isView) {
			nPk = pTab->nCol;
			iPk = pParse->nMem + 1;
			pParse->nMem += nPk;
			iEphCur = pParse->nTab++;
			KeyInfo *pKeyInfo = sqlite3KeyInfoAlloc(pParse->db, nPk, 0);
			addrEphOpen =
				sqlite3VdbeAddOp4(v, OP_OpenTEphemeral, iEphCur,
						  nPk, 0, (char*) pKeyInfo, P4_KEYINFO);
		} else {
			pPk = sqlite3PrimaryKeyIndex(pTab);
			assert(pPk != 0);
			nPk = index_column_count(pPk);
			iPk = pParse->nMem + 1;
			pParse->nMem += nPk;
			iEphCur = pParse->nTab++;
			addrEphOpen =
			    sqlite3VdbeAddOp2(v, OP_OpenTEphemeral, iEphCur,
					      nPk);
			sqlite3VdbeSetP4KeyInfo(pParse, pPk);
		}

		/* Construct a query to find the primary key for every row
		 * to be deleted, based on the WHERE clause. Set variable eOnePass
		 * to indicate the strategy used to implement this delete:
		 *
		 *  ONEPASS_OFF:    Two-pass approach - use a FIFO for PK values.
		 *  ONEPASS_SINGLE: One-pass approach - at most one row deleted.
		 *  ONEPASS_MULTI:  One-pass approach - any number of rows may be deleted.
		 */
		pWInfo =
		    sqlite3WhereBegin(pParse, pTabList, pWhere, 0, 0, wcf,
				      iTabCur + 1);
		if (pWInfo == 0)
			goto delete_from_cleanup;
		eOnePass = sqlite3WhereOkOnePass(pWInfo, aiCurOnePass);
		assert(eOnePass != ONEPASS_MULTI);
		/* Tarantool workaround: see comment in sqlite3WhereBegin.  */
		/* assert( bComplex || eOnePass!=ONEPASS_OFF ); */

		/* Keep track of the number of rows to be deleted */
		if (user_session->sql_flags & SQLITE_CountRows) {
			sqlite3VdbeAddOp2(v, OP_AddImm, memCnt, 1);
		}

		/* Extract the primary key for the current row */
		if (!isView) {
			for (i = 0; i < nPk; i++) {
				assert(pPk->aiColumn[i] >= 0);
				sqlite3ExprCodeGetColumnOfTable(v, pTab,
								iTabCur,
								pPk->
								aiColumn[i],
								iPk + i);
			}
		} else {
			for (i = 0; i < nPk; i++) {
				sqlite3VdbeAddOp3(v, OP_Column, iDataCur,
						  i, iPk + i);
			}
		}
		iKey = iPk;

		if (eOnePass != ONEPASS_OFF) {
			/* For ONEPASS, no need to store the primary-key. There is only
			 * one, so just keep it in its register(s) and fall through to the
			 * delete code.
			 */
			nKey = nPk;	/* OP_Found will use an unpacked key */
			aToOpen = sqlite3DbMallocRawNN(db, nIdx + 2);
			if (aToOpen == 0) {
				sqlite3WhereEnd(pWInfo);
				goto delete_from_cleanup;
			}
			memset(aToOpen, 1, nIdx + 1);
			aToOpen[nIdx + 1] = 0;
			if (aiCurOnePass[0] >= 0)
				aToOpen[aiCurOnePass[0] - iTabCur] = 0;
			if (aiCurOnePass[1] >= 0)
				aToOpen[aiCurOnePass[1] - iTabCur] = 0;
			if (addrEphOpen)
				sqlite3VdbeChangeToNoop(v, addrEphOpen);
		} else {
			/* Add the PK key for this row to the temporary table */
			iKey = ++pParse->nMem;
			nKey = 0;	/* Zero tells OP_Found to use a composite key */
			const char *zAff = isView ? 0 :
					  sqlite3IndexAffinityStr(pParse->db, pPk);
			sqlite3VdbeAddOp4(v, OP_MakeRecord, iPk, nPk, iKey, zAff, nPk);
			/* Set flag to save memory allocating one by malloc. */
			sqlite3VdbeChangeP5(v, 1);
			sqlite3VdbeAddOp2(v, OP_IdxInsert, iEphCur, iKey);
		}

		/* If this DELETE cannot use the ONEPASS strategy, this is the
		 * end of the WHERE loop
		 */
		if (eOnePass != ONEPASS_OFF) {
			addrBypass = sqlite3VdbeMakeLabel(v);
		} else {
			sqlite3WhereEnd(pWInfo);
		}

		/* Unless this is a view, open cursors for the table we are
		 * deleting from and all its indices. If this is a view, then the
		 * only effect this statement has is to fire the INSTEAD OF
		 * triggers.
		 */
		if (!isView) {
			int iAddrOnce = 0;
			if (eOnePass == ONEPASS_MULTI) {
				iAddrOnce = sqlite3VdbeAddOp0(v, OP_Once);
				VdbeCoverage(v);
			}
			sqlite3OpenTableAndIndices(pParse, pTab, OP_OpenWrite,
						   OPFLAG_FORDELETE, iTabCur,
						   aToOpen, &iDataCur,
						   &iIdxCur,
						   ON_CONFLICT_ACTION_NONE, 0);
			assert(pPk || iDataCur == iTabCur);
			assert(pPk || iIdxCur == iDataCur + 1);
			if (eOnePass == ONEPASS_MULTI)
				sqlite3VdbeJumpHere(v, iAddrOnce);
		}

		/* Set up a loop over the primary-keys that were found in the
		 * where-clause loop above.
		 */
		if (eOnePass != ONEPASS_OFF) {
			assert(nKey == nPk);	/* OP_Found will use an unpacked key */
			if (aToOpen[iDataCur - iTabCur]) {
				assert(pPk != 0 || pTab->pSelect != 0);
				sqlite3VdbeAddOp4Int(v, OP_NotFound, iDataCur,
						     addrBypass, iKey, nKey);

				VdbeCoverage(v);
			}
		} else {
			addrLoop = sqlite3VdbeAddOp1(v, OP_Rewind, iEphCur);
			VdbeCoverage(v);
			sqlite3VdbeAddOp2(v, OP_RowData, iEphCur, iKey);
		}

		/* Delete the row */
		{
			int count = (pParse->nested == 0);	/* True to count changes */
			int iIdxNoSeek = -1;
			if (bComplex == 0 && aiCurOnePass[1] != iDataCur
			    /* Tarantool: as far as ONEPASS is disabled, there's no index
			       w/o need of seeking.  */
			    && eOnePass != ONEPASS_OFF) {
				iIdxNoSeek = aiCurOnePass[1];
			}
			sqlite3GenerateRowDelete(pParse, pTab, pTrigger,
						 iDataCur, iIdxCur, iKey, nKey,
						 count,
						 ON_CONFLICT_ACTION_DEFAULT,
						 eOnePass,
						 iIdxNoSeek);
		}

		/* End of the loop over all primary-keys. */
		if (eOnePass != ONEPASS_OFF) {
			sqlite3VdbeResolveLabel(v, addrBypass);
			sqlite3WhereEnd(pWInfo);
		} else {
			sqlite3VdbeAddOp2(v, OP_Next, iEphCur, addrLoop + 1);
			VdbeCoverage(v);
			sqlite3VdbeJumpHere(v, addrLoop);
		}
	}			/* End non-truncate path */

	/* Return the number of rows that were deleted. If this routine is
	 * generating code because of a call to sqlite3NestedParse(), do not
	 * invoke the callback function.
	 */
	if ((user_session->sql_flags & SQLITE_CountRows) &&
	    !pParse->nested && !pParse->pTriggerTab) {
		sqlite3VdbeAddOp2(v, OP_ResultRow, memCnt, 1);
		sqlite3VdbeSetNumCols(v, 1);
		sqlite3VdbeSetColName(v, 0, COLNAME_NAME, "rows deleted",
				      SQLITE_STATIC);
	}

 delete_from_cleanup:
	sqlite3SrcListDelete(db, pTabList);
	sql_expr_free(db, pWhere, false);
	sqlite3DbFree(db, aToOpen);
	return;
}

/* Generate VDBE code for
 * DELETE FROM <pTab.z> WHERE
 *        <columns[0]> = <values[0]>
 *    AND  ...
 *    AND <columns[nPairs - 1]> = <values[nPairs - 1]>;
 *
 * This function does not increment the nested counter and is
 * faster than nested parsing of the request above.
 * @param pParse Parser context.
 * @param pTab Table name.
 * @param columns Column names array.
 * @param values Column values array.
 * @param nPairs Length of @columns and @values.
 *
 * In case of error the @values elements are deleted.
 */
void
sqlite3DeleteByKey(Parse *pParse, char *zTab, const char **columns,
		   Expr **values, int nPairs)
{
	Expr *where = NULL;
	SrcList *src;

		assert(nPairs > 0);
	if (pParse->nErr > 0 || pParse->db->mallocFailed)
		goto error;
	src = sql_alloc_src_list(pParse->db);
	src->a[0].zName = sqlite3DbStrDup(pParse->db, zTab);
	if (src == NULL)
		goto error;
	/* Dummy init of INDEXED BY clause. */
	Token t = { NULL, 0, false };
	sqlite3SrcListIndexedBy(pParse, src, &t);

	for (int i = 0; i < nPairs; ++i) {
		Expr *col_expr = sqlite3Expr(pParse->db, TK_ID, columns[i]);
		if (col_expr == NULL || values[i] == NULL)
			goto error;
		Expr *eq_expr =
		    sqlite3PExpr(pParse, TK_EQ, col_expr, values[i]);
		/* In case of error the values[i] had been deleted in
		 * sqlite3PExpr already. Do not delete it second time in the
		 * cycle below.
		 */
		values[i] = NULL;
		if (eq_expr == NULL)
			goto error;
		if (i == 0) {
			where = eq_expr;
		} else {
			where = sqlite3ExprAnd(pParse->db, where, eq_expr);
			if (where == NULL)
				goto error;
		}
	}
	/* DeleteFrom frees the src and exprs in case of error. */
	sqlite3DeleteFrom(pParse, src, where);
	return;

 error:
	sql_expr_free(pParse->db, where, false);
	for (int i = 0; i < nPairs; ++i)
		sql_expr_free(pParse->db, values[i], false);
}

/* Make sure "isView" and other macros defined above are undefined. Otherwise
 * they may interfere with compilation of other functions in this file
 * (or in another file, if this file becomes part of the amalgamation).
 */
#ifdef isView
#undef isView
#endif
#ifdef pTrigger
#undef pTrigger
#endif

/*
 * This routine generates VDBE code that causes a single row of a
 * single table to be deleted.  Both the original table entry and
 * all indices are removed.
 *
 * Preconditions:
 *
 *   1.  iDataCur is an open cursor on the btree that is the canonical data
 *       store for the table.  (This will be the PRIMARY KEY index)
 *
 *   2.  Read/write cursors for all indices of pTab must be open as
 *       cursor number iIdxCur+i for the i-th index.
 *
 *   3.  The primary key for the row to be deleted must be stored in a
 *       sequence of nPk memory cells starting at iPk.  If nPk==0 that means
 *       that a search record formed from OP_MakeRecord is contained in the
 *       single memory location iPk.
 *
 * eMode:
 *   Parameter eMode may be passed either ONEPASS_OFF (0), ONEPASS_SINGLE, or
 *   ONEPASS_MULTI.  If eMode is not ONEPASS_OFF, then the cursor
 *   iDataCur already points to the row to delete. If eMode is ONEPASS_OFF
 *   then this function must seek iDataCur to the entry identified by iPk
 *   and nPk before reading from it.
 *
 *   If eMode is ONEPASS_MULTI, then this call is being made as part
 *   of a ONEPASS delete that affects multiple rows. In this case, if
 *   iIdxNoSeek is a valid cursor number (>=0), then its position should
 *   be preserved following the delete operation. Or, if iIdxNoSeek is not
 *   a valid cursor number, the position of iDataCur should be preserved
 *   instead.
 *
 * iIdxNoSeek:
 *   If iIdxNoSeek is a valid cursor number (>=0), then it identifies an
 *   index cursor (from within array of cursors starting at iIdxCur) that
 *   already points to the index entry to be deleted.
 */
void
sqlite3GenerateRowDelete(Parse * pParse,	/* Parsing context */
			 Table * pTab,		/* Table containing the row to be deleted */
			 Trigger * pTrigger,	/* List of triggers to (potentially) fire */
			 int iDataCur,		/* Cursor from which column data is extracted */
			 int iIdxCur,		/* First index cursor */
			 int iPk,		/* First memory cell containing the PRIMARY KEY */
			 i16 nPk,		/* Number of PRIMARY KEY memory cells */
			 u8 count,		/* If non-zero, increment the row change counter */
			 enum on_conflict_action onconf,		/* Default ON CONFLICT policy for triggers */
			 u8 eMode,		/* ONEPASS_OFF, _SINGLE, or _MULTI.  See above */
			 int iIdxNoSeek)	/* Cursor number of cursor that does not need seeking */
{
	Vdbe *v = pParse->pVdbe;	/* Vdbe */
	int iOld = 0;		/* First register in OLD.* array */
	int iLabel;		/* Label resolved to end of generated code */

	/* Vdbe is guaranteed to have been allocated by this stage. */
	assert(v);
	(void)iIdxCur;
	VdbeModuleComment((v, "BEGIN: GenRowDel(%d,%d,%d,%d)",
			   iDataCur, iIdxCur, iPk, (int)nPk));

	/* Seek cursor iCur to the row to delete. If this row no longer exists
	 * (this can happen if a trigger program has already deleted it), do
	 * not attempt to delete it or fire any DELETE triggers.
	 */
	iLabel = sqlite3VdbeMakeLabel(v);
	if (eMode == ONEPASS_OFF) {
		sqlite3VdbeAddOp4Int(v, OP_NotFound, iDataCur, iLabel, iPk, nPk);
		VdbeCoverageIf(v, opSeek == OP_NotFound);
	}

	/* If there are any triggers to fire, allocate a range of registers to
	 * use for the old.* references in the triggers.
	 */
	if (sqlite3FkRequired(pTab, 0) || pTrigger) {
		u32 mask;	/* Mask of OLD.* columns in use */
		int iCol;	/* Iterator used while populating OLD.* */
		int addrStart;	/* Start of BEFORE trigger programs */

		/* TODO: Could use temporary registers here.
		 */
		mask =
		    sqlite3TriggerColmask(pParse, pTrigger, 0, 0,
					  TRIGGER_BEFORE | TRIGGER_AFTER, pTab,
					  onconf);
		mask |= sqlite3FkOldmask(pParse, pTab);
		iOld = pParse->nMem + 1;
		pParse->nMem += (1 + pTab->nCol);

		/* Populate the OLD.* pseudo-table register array. These values will be
		 * used by any BEFORE and AFTER triggers that exist.
		 */
		sqlite3VdbeAddOp2(v, OP_Copy, iPk, iOld);
		for (iCol = 0; iCol < pTab->nCol; iCol++) {
			testcase(mask != 0xffffffff && iCol == 31);
			testcase(mask != 0xffffffff && iCol == 32);
			if (mask == 0xffffffff
			    || (iCol <= 31 && (mask & MASKBIT32(iCol)) != 0)) {
				sqlite3ExprCodeGetColumnOfTable(v, pTab,
								iDataCur, iCol,
								iOld + iCol +
								1);
			}
		}

		/* Invoke BEFORE DELETE trigger programs. */
		addrStart = sqlite3VdbeCurrentAddr(v);
		sqlite3CodeRowTrigger(pParse, pTrigger,
				      TK_DELETE, 0, TRIGGER_BEFORE, pTab, iOld,
				      onconf, iLabel);

		/* If any BEFORE triggers were coded, then seek the cursor to the
		 * row to be deleted again. It may be that the BEFORE triggers moved
		 * the cursor or of already deleted the row that the cursor was
		 * pointing to.
		 */
		if (addrStart < sqlite3VdbeCurrentAddr(v)) {
			sqlite3VdbeAddOp4Int(v, OP_NotFound, iDataCur, iLabel, iPk,
					     nPk);
			VdbeCoverageIf(v, opSeek == OP_NotFound);
		}

		/* Do FK processing. This call checks that any FK constraints that
		 * refer to this table (i.e. constraints attached to other tables)
		 * are not violated by deleting this row.
		 */
		sqlite3FkCheck(pParse, pTab, iOld, 0, 0);
	}

	/* Delete the index and table entries. Skip this step if pTab is really
	 * a view (in which case the only effect of the DELETE statement is to
	 * fire the INSTEAD OF triggers).
	 *
	 * If variable 'count' is non-zero, then this OP_Delete instruction should
	 * invoke the update-hook. The pre-update-hook, on the other hand should
	 * be invoked unless table pTab is a system table. The difference is that
	 * the update-hook is not invoked for rows removed by REPLACE, but the
	 * pre-update-hook is.
	 */
	if (pTab->pSelect == 0) {
		u8 p5 = 0;
		/* kyukhin: Tarantool handles indices uypdate automatically.  */
		/* sqlite3GenerateRowIndexDelete(pParse, pTab, iDataCur, iIdxCur,0,iIdxNoSeek);  */
		sqlite3VdbeAddOp2(v, OP_Delete, iDataCur,
				  (count ? OPFLAG_NCHANGE : 0));
		if (eMode != ONEPASS_OFF) {
			sqlite3VdbeChangeP5(v, OPFLAG_AUXDELETE);
		}
		if (iIdxNoSeek >= 0) {
			sqlite3VdbeAddOp1(v, OP_Delete, iIdxNoSeek);
		}
		if (eMode == ONEPASS_MULTI)
			p5 |= OPFLAG_SAVEPOSITION;
		sqlite3VdbeChangeP5(v, p5);
	}

	/* Do any ON CASCADE, SET NULL or SET DEFAULT operations required to
	 * handle rows (possibly in other tables) that refer via a foreign key
	 * to the row just deleted.
	 */
	sqlite3FkActions(pParse, pTab, 0, iOld, 0);

	/* Invoke AFTER DELETE trigger programs. */
	sqlite3CodeRowTrigger(pParse, pTrigger,
			      TK_DELETE, 0, TRIGGER_AFTER, pTab, iOld, onconf,
			      iLabel);

	/* Jump here if the row had already been deleted before any BEFORE
	 * trigger programs were invoked. Or if a trigger program throws a
	 * RAISE(IGNORE) exception.
	 */
	sqlite3VdbeResolveLabel(v, iLabel);
	VdbeModuleComment((v, "END: GenRowDel()"));
}

/*
 * This routine generates VDBE code that causes the deletion of all
 * index entries associated with a single row of a single table, pTab
 *
 * Preconditions:
 *
 *   1.  A read/write cursor "iDataCur" must be open on the canonical storage
 *       btree for the table pTab.  (This will be primary key index)
 *
 *   2.  Read/write cursor for primary index of pTab must be open as
 *       cursor number iIdxCur.  (The pTab->pIndex index is the 0-th index.)
 *
 *   3.  The "iDataCur" cursor must be already positioned on the row
 *       that is to be deleted.
 */
void
sqlite3GenerateRowIndexDelete(Parse * pParse,	/* Parsing and code generating context */
			      Table * pTab,	/* Table containing the row to be deleted */
			      int iDataCur,	/* Cursor of table holding data. */
			      int iIdxCur)	/* Primary index cursor */
{
	int r1 = -1;		/* Register holding an index key */
	int iPartIdxLabel;	/* Jump destination for skipping partial index entries */
	Vdbe *v;		/* The prepared statement under construction */
	Index *pPk;		/* PRIMARY KEY index */

	v = pParse->pVdbe;
	pPk = sqlite3PrimaryKeyIndex(pTab);
	/* In Tarantool it is enough to delete row just from pk */
	VdbeModuleComment((v, "GenRowIdxDel for %s", pPk->zName));
	r1 = sqlite3GenerateIndexKey(pParse, pPk, iDataCur, 0, &iPartIdxLabel,
				     NULL, r1);
	sqlite3VdbeAddOp3(v, OP_IdxDelete, iIdxCur, r1, index_column_count(pPk));
	sqlite3ResolvePartIdxLabel(pParse, iPartIdxLabel);
}

/*
 * Generate code that will assemble an index key and stores it in register
 * regOut.  The key with be for index pIdx which is an index on pTab.
 * iCur is the index of a cursor open on the pTab table and pointing to
 * the entry that needs indexing.
 * iCur must be the cursor of the PRIMARY KEY index.
 *
 * Return a register number which is the first in a block of
 * registers that holds the elements of the index key.  The
 * block of registers has already been deallocated by the time
 * this routine returns.
 *
 * If *piPartIdxLabel is not NULL, fill it in with a label and jump
 * to that label if pIdx is a partial index that should be skipped.
 * The label should be resolved using sqlite3ResolvePartIdxLabel().
 * A partial index should be skipped if its WHERE clause evaluates
 * to false or null.  If pIdx is not a partial index, *piPartIdxLabel
 * will be set to zero which is an empty label that is ignored by
 * sqlite3ResolvePartIdxLabel().
 *
 * The pPrior and regPrior parameters are used to implement a cache to
 * avoid unnecessary register loads.  If pPrior is not NULL, then it is
 * a pointer to a different index for which an index key has just been
 * computed into register regPrior.  If the current pIdx index is generating
 * its key into the same sequence of registers and if pPrior and pIdx share
 * a column in common, then the register corresponding to that column already
 * holds the correct value and the loading of that register is skipped.
 * This optimization is helpful when doing a DELETE or an INTEGRITY_CHECK
 * on a table with multiple indices, and especially with
 * the PRIMARY KEY columns of the index.
 */
int
sqlite3GenerateIndexKey(Parse * pParse,	/* Parsing context */
			Index * pIdx,	/* The index for which to generate a key */
			int iDataCur,	/* Cursor number from which to take column data */
			int regOut,	/* Put the new key into this register if not 0 */
			int *piPartIdxLabel,	/* OUT: Jump to this label to skip partial index */
			Index * pPrior,	/* Previously generated index key */
			int regPrior)	/* Register holding previous generated key */
{
	Vdbe *v = pParse->pVdbe;
	int j;
	int regBase;
	int nCol;

	if (piPartIdxLabel) {
		if (pIdx->pPartIdxWhere) {
			*piPartIdxLabel = sqlite3VdbeMakeLabel(v);
			pParse->iSelfTab = iDataCur;
			sqlite3ExprCachePush(pParse);
			sqlite3ExprIfFalseDup(pParse, pIdx->pPartIdxWhere,
					      *piPartIdxLabel,
					      SQLITE_JUMPIFNULL);
		} else {
			*piPartIdxLabel = 0;
		}
	}
	nCol = index_column_count(pIdx);
	regBase = sqlite3GetTempRange(pParse, nCol);
	if (pPrior && (regBase != regPrior || pPrior->pPartIdxWhere))
		pPrior = 0;
	for (j = 0; j < nCol; j++) {
		if (pPrior && pPrior->aiColumn[j] == pIdx->aiColumn[j]
		    && pPrior->aiColumn[j] != XN_EXPR) {
			/* This column was already computed by the previous index */
			continue;
		}
		sqlite3ExprCodeLoadIndexColumn(pParse, pIdx, iDataCur, j,
					       regBase + j);
		/* If the column affinity is REAL but the number is an integer, then it
		 * might be stored in the table as an integer (using a compact
		 * representation) then converted to REAL by an OP_RealAffinity opcode.
		 * But we are getting ready to store this value back into an index, where
		 * it should be converted by to INTEGER again.  So omit the OP_RealAffinity
		 * opcode if it is present
		 */
		sqlite3VdbeDeletePriorOpcode(v, OP_RealAffinity);
	}
	if (regOut) {
		sqlite3VdbeAddOp3(v, OP_MakeRecord, regBase, nCol, regOut);
	}
	sqlite3ReleaseTempRange(pParse, regBase, nCol);
	return regBase;
}

/*
 * If a prior call to sqlite3GenerateIndexKey() generated a jump-over label
 * because it was a partial index, then this routine should be called to
 * resolve that label.
 */
void
sqlite3ResolvePartIdxLabel(Parse * pParse, int iLabel)
{
	if (iLabel) {
		sqlite3VdbeResolveLabel(pParse->pVdbe, iLabel);
		sqlite3ExprCachePop(pParse);
	}
}
