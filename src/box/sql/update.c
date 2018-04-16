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
 * to handle UPDATE statements.
 */
#include "sqliteInt.h"
#include "box/session.h"
#include "tarantoolInt.h"
#include "box/schema.h"

/*
 * The most recently coded instruction was an OP_Column to retrieve the
 * i-th column of table pTab. This routine sets the P4 parameter of the
 * OP_Column to the default value, if any.
 *
 * The default value of a column is specified by a DEFAULT clause in the
 * column definition. This was either supplied by the user when the table
 * was created, or added later to the table definition by an ALTER TABLE
 * command. If the latter, then the row-records in the table btree on disk
 * may not contain a value for the column and the default value, taken
 * from the P4 parameter of the OP_Column instruction, is returned instead.
 * If the former, then all row-records are guaranteed to include a value
 * for the column and the P4 value is not required.
 *
 * Column definitions created by an ALTER TABLE command may only have
 * literal default values specified: a number, null or a string. (If a more
 * complicated default expression value was provided, it is evaluated
 * when the ALTER TABLE is executed and one of the literal values written
 * into the schema.)
 *
 * Therefore, the P4 parameter is only required if the default value for
 * the column is a literal number, string or null. The sqlite3ValueFromExpr()
 * function is capable of transforming these types of expressions into
 * sqlite3_value objects.
 *
 * If parameter iReg is not negative, code an OP_RealAffinity instruction
 * on register iReg. This is used when an equivalent integer value is
 * stored in place of an 8-byte floating point value in order to save
 * space.
 */
void
sqlite3ColumnDefault(Vdbe * v, Table * pTab, int i, int iReg)
{
	assert(pTab != 0);
	if (!pTab->pSelect) {
		sqlite3_value *pValue = 0;
		Column *pCol = &pTab->aCol[i];
		VdbeComment((v, "%s.%s", pTab->zName, pCol->zName));
		assert(i < pTab->nCol);

		Expr *expr = NULL;
		struct space *space =
			space_cache_find(SQLITE_PAGENO_TO_SPACEID(pTab->tnum));
		if (space != NULL && space->def->fields != NULL)
			expr = space->def->fields[i].default_value_expr;
		sqlite3ValueFromExpr(sqlite3VdbeDb(v),
				     expr,
				     pCol->affinity, &pValue);
		if (pValue) {
			sqlite3VdbeAppendP4(v, pValue, P4_MEM);
		}
#ifndef SQLITE_OMIT_FLOATING_POINT
		if (pTab->aCol[i].affinity == SQLITE_AFF_REAL) {
			sqlite3VdbeAddOp1(v, OP_RealAffinity, iReg);
		}
#endif
	}
}

/*
 * Process an UPDATE statement.
 *
 *   UPDATE OR IGNORE table_wxyz SET a=b, c=d WHERE e<5 AND f NOT NULL;
 *          \_______/ \________/     \______/       \________________/
*            onError   pTabList      pChanges             pWhere
 */
void
sqlite3Update(Parse * pParse,		/* The parser context */
	      SrcList * pTabList,	/* The table in which we should change things */
	      ExprList * pChanges,	/* Things to be changed */
	      Expr * pWhere,		/* The WHERE clause.  May be null */
	      int onError)		/* How to handle constraint errors */
{
	int i, j;		/* Loop counters */
	Table *pTab;		/* The table to be updated */
	int addrTop = 0;	/* VDBE instruction address of the start of the loop */
	WhereInfo *pWInfo;	/* Information about the WHERE clause */
	Vdbe *v;		/* The virtual database engine */
	Index *pIdx;		/* For looping over indices */
	Index *pPk;		/* The PRIMARY KEY index */
	int nIdx;		/* Number of indices that need updating */
	int iBaseCur;		/* Base cursor number */
	int iDataCur;		/* Cursor for the canonical data btree */
	int iIdxCur;		/* Cursor for the first index */
	sqlite3 *db;		/* The database structure */
	int *aRegIdx = 0;	/* One register assigned to each index to be updated */
	int *aXRef = 0;		/* aXRef[i] is the index in pChanges->a[] of the
				 * an expression for the i-th column of the table.
				 * aXRef[i]==-1 if the i-th column is not changed.
				 */
	u8 *aToOpen;		/* 1 for tables and indices to be opened */
	u8 chngPk;		/* PRIMARY KEY changed */
	NameContext sNC;	/* The name-context to resolve expressions in */
	int okOnePass;		/* True for one-pass algorithm without the FIFO */
	int hasFK;		/* True if foreign key processing is required */
	int labelBreak;		/* Jump here to break out of UPDATE loop */
	int labelContinue;	/* Jump here to continue next step of UPDATE loop */
	struct session *user_session = current_session();

#ifndef SQLITE_OMIT_TRIGGER
	int isView;		/* True when updating a view (INSTEAD OF trigger) */
	Trigger *pTrigger;	/* List of triggers on pTab, if required */
	int tmask;		/* Mask of TRIGGER_BEFORE|TRIGGER_AFTER */
#endif
	int newmask;		/* Mask of NEW.* columns accessed by BEFORE triggers */
	int iEph = 0;		/* Ephemeral table holding all primary key values */
	int nKey = 0;		/* Number of elements in regKey */
	int aiCurOnePass[2];	/* The write cursors opened by WHERE_ONEPASS */

	/* Register Allocations */
	int regRowCount = 0;	/* A count of rows changed */
	int regOldPk = 0;
	int regNewPk = 0;
	int regNew = 0;		/* Content of the NEW.* table in triggers */
	int regOld = 0;		/* Content of OLD.* table in triggers */
	int regKey = 0;		/* composite PRIMARY KEY value */

	db = pParse->db;
	if (pParse->nErr || db->mallocFailed) {
		goto update_cleanup;
	}
	assert(pTabList->nSrc == 1);

	/* Locate the table which we want to update.
	 */
	pTab = sqlite3SrcListLookup(pParse, pTabList);
	if (pTab == 0)
		goto update_cleanup;

	/* Figure out if we have any triggers and if the table being
	 * updated is a view.
	 */
#ifndef SQLITE_OMIT_TRIGGER
	pTrigger = sqlite3TriggersExist(pTab, TK_UPDATE, pChanges, &tmask);
	isView = space_is_view(pTab);
	assert(pTrigger || tmask == 0);
#else
#define pTrigger 0
#define isView 0
#define tmask 0
#endif
#ifdef SQLITE_OMIT_VIEW
#undef isView
#define isView 0
#endif

	if (sqlite3ViewGetColumnNames(pParse, pTab)) {
		goto update_cleanup;
	}
	if (sqlite3IsReadOnly(pParse, pTab, tmask)) {
		goto update_cleanup;
	}

	/* Allocate a cursors for the main database table and for all indices.
	 * The index cursors might not be used, but if they are used they
	 * need to occur right after the database cursor.  So go ahead and
	 * allocate enough space, just in case.
	 */
	pTabList->a[0].iCursor = iBaseCur = iDataCur = pParse->nTab++;
	iIdxCur = iDataCur + 1;
	pPk = isView ? 0 : sqlite3PrimaryKeyIndex(pTab);
	for (nIdx = 0, pIdx = pTab->pIndex; pIdx; pIdx = pIdx->pNext, nIdx++) {
		if (IsPrimaryKeyIndex(pIdx) && pPk != 0) {
			iDataCur = pParse->nTab;
			pTabList->a[0].iCursor = iDataCur;
		}
		pParse->nTab++;
	}

	/* Allocate space for aXRef[], aRegIdx[], and aToOpen[].
	 * Initialize aXRef[] and aToOpen[] to their default values.
	 */
	aXRef =
	    sqlite3DbMallocRawNN(db,
				 sizeof(int) * (pTab->nCol + nIdx) + nIdx + 2);
	if (aXRef == 0)
		goto update_cleanup;
	aRegIdx = aXRef + pTab->nCol;
	aToOpen = (u8 *) (aRegIdx + nIdx);
	memset(aToOpen, 1, nIdx + 1);
	aToOpen[nIdx + 1] = 0;
	for (i = 0; i < pTab->nCol; i++)
		aXRef[i] = -1;

	/* Initialize the name-context */
	memset(&sNC, 0, sizeof(sNC));
	sNC.pParse = pParse;
	sNC.pSrcList = pTabList;

	/* Resolve the column names in all the expressions of the
	 * of the UPDATE statement.  Also find the column index
	 * for each column to be updated in the pChanges array.
	 */
	chngPk = 0;
	for (i = 0; i < pChanges->nExpr; i++) {
		if (sqlite3ResolveExprNames(&sNC, pChanges->a[i].pExpr)) {
			goto update_cleanup;
		}
		for (j = 0; j < pTab->nCol; j++) {
			if (strcmp(pTab->aCol[j].zName,
				   pChanges->a[i].zName) == 0) {
				if (pPk && table_column_is_in_pk(pTab, j)) {
					chngPk = 1;
				}
				if (aXRef[j] != -1) {
					sqlite3ErrorMsg(pParse,
							"set id list: duplicate"
							" column name %s",
							pChanges->a[i].zName);
					goto update_cleanup;
				}
				aXRef[j] = i;
				break;
			}
		}
		if (j >= pTab->nCol) {
			sqlite3ErrorMsg(pParse, "no such column: %s",
					pChanges->a[i].zName);
			pParse->checkSchema = 1;
			goto update_cleanup;
		}
	}
	assert(chngPk == 0 || chngPk == 1);

	/*
	 * The SET expressions are not actually used inside the
	 * WHERE loop. So reset the colUsed mask.
	 */
	pTabList->a[0].colUsed = 0;

	hasFK = sqlite3FkRequired(pTab, aXRef);

	/* There is one entry in the aRegIdx[] array for each index on the table
	 * being updated.  Fill in aRegIdx[] with a register number that will hold
	 * the key for accessing each index.
	 *
	 * FIXME:  Be smarter about omitting indexes that use expressions.
	 */
	for (j = 0, pIdx = pTab->pIndex; pIdx; pIdx = pIdx->pNext, j++) {
		int reg;
		int nIdxCol = index_column_count(pIdx);
		if (chngPk || hasFK || pIdx->pPartIdxWhere || pIdx == pPk) {
			reg = ++pParse->nMem;
			pParse->nMem += nIdxCol;
		} else {
			reg = 0;
			for (i = 0; i < nIdxCol; i++) {
				i16 iIdxCol = pIdx->aiColumn[i];
				if (iIdxCol < 0 || aXRef[iIdxCol] >= 0) {
					reg = ++pParse->nMem;
					pParse->nMem += nIdxCol;
					break;
				}
			}
		}
		if (reg == 0)
			aToOpen[j + 1] = 0;
		aRegIdx[j] = reg;
	}

	/* Begin generating code. */
	v = sqlite3GetVdbe(pParse);
	if (v == 0)
		goto update_cleanup;
	if (pParse->nested == 0)
		sqlite3VdbeCountChanges(v);
	sql_set_multi_write(pParse, true);

	/* Allocate required registers. */
	regOldPk = regNewPk = ++pParse->nMem;

	if (chngPk || pTrigger || hasFK) {
		regOld = pParse->nMem + 1;
		pParse->nMem += pTab->nCol;
	}
	if (chngPk || pTrigger || hasFK) {
		regNewPk = ++pParse->nMem;
	}
	regNew = pParse->nMem + 1;
	pParse->nMem += pTab->nCol;

	/* If we are trying to update a view, realize that view into
	 * an ephemeral table.
	 */
#if !defined(SQLITE_OMIT_VIEW) && !defined(SQLITE_OMIT_TRIGGER)
	if (isView) {
		sqlite3MaterializeView(pParse, pTab, pWhere, iDataCur);
		/* Number of columns from SELECT plus ID.*/
		nKey = pTab->nCol + 1;
	}
#endif

	/* Resolve the column names in all the expressions in the
	 * WHERE clause.
	 */
	if (sqlite3ResolveExprNames(&sNC, pWhere)) {
		goto update_cleanup;
	}
	/* Begin the database scan
	 * The only difference between VIEW and ordinary table is the fact that
	 * VIEW is held in ephemeral table and doesn't have explicit PK.
	 * In this case we have to manually load columns in order to make tuple.
	 */
	int iPk;	/* First of nPk memory cells holding PRIMARY KEY value */
	i16 nPk;	/* Number of components of the PRIMARY KEY */
	int addrOpen;	/* Address of the OpenEphemeral instruction */

	if (isView) {
		nPk = nKey;
	} else {
		assert(pPk != 0);
		nPk = index_column_count(pPk);
	}
	iPk = pParse->nMem + 1;
	pParse->nMem += nPk;
	regKey = ++pParse->nMem;
	iEph = pParse->nTab++;
	sqlite3VdbeAddOp2(v, OP_Null, 0, iPk);

	if (isView) {
		KeyInfo *pKeyInfo = sqlite3KeyInfoAlloc(pParse->db, nKey, 0);
		addrOpen = sqlite3VdbeAddOp4(v, OP_OpenTEphemeral, iEph,
					     nKey, 0, (char*)pKeyInfo, P4_KEYINFO);
	} else {
		addrOpen = sqlite3VdbeAddOp2(v, OP_OpenTEphemeral, iEph, nPk);
		sqlite3VdbeSetP4KeyInfo(pParse, pPk);
	}

	pWInfo = sqlite3WhereBegin(pParse, pTabList, pWhere, 0, 0,
				   WHERE_ONEPASS_DESIRED, iIdxCur);
	if (pWInfo == 0)
		goto update_cleanup;
	okOnePass = sqlite3WhereOkOnePass(pWInfo, aiCurOnePass);
	if (isView){
		for (i = 0; i < nPk; i++) {
			sqlite3VdbeAddOp3(v, OP_Column, iDataCur, i, iPk + i);
		}
	} else {
		for (i = 0; i < nPk; i++) {
			assert(pPk->aiColumn[i] >= 0);
			sqlite3ExprCodeGetColumnOfTable(v, pTab, iDataCur,
							pPk->aiColumn[i],
							iPk + i);
		}
	}

	if (okOnePass) {
		sqlite3VdbeChangeToNoop(v, addrOpen);
		nKey = nPk;
		regKey = iPk;
	} else {
		const char *zAff = isView ? 0 :
				   sqlite3IndexAffinityStr(pParse->db, pPk);
		sqlite3VdbeAddOp4(v, OP_MakeRecord, iPk, nPk, regKey,
					  zAff, nPk);
		sqlite3VdbeAddOp2(v, OP_IdxInsert, iEph, regKey);
		/* Set flag to save memory allocating one by malloc. */
		sqlite3VdbeChangeP5(v, 1);
	}
	/* End the database scan loop.
	 */
	sqlite3WhereEnd(pWInfo);


	/* Initialize the count of updated rows
	 */
	if ((user_session->sql_flags & SQLITE_CountRows)
	    && !pParse->pTriggerTab) {
		regRowCount = ++pParse->nMem;
		sqlite3VdbeAddOp2(v, OP_Integer, 0, regRowCount);
	}

	labelBreak = sqlite3VdbeMakeLabel(v);
	if (!isView) {
		/*
		 * Open every index that needs updating.  Note that if any
		 * index could potentially invoke a REPLACE conflict resolution
		 * action, then we need to open all indices because we might need
		 * to be deleting some records.
		 */
		if (onError == ON_CONFLICT_ACTION_REPLACE) {
			memset(aToOpen, 1, nIdx + 1);
		} else {
			for (pIdx = pTab->pIndex; pIdx; pIdx = pIdx->pNext) {
				if (pIdx->onError == ON_CONFLICT_ACTION_REPLACE) {
					memset(aToOpen, 1, nIdx + 1);
					break;
				}
			}
		}
		if (okOnePass) {
			if (aiCurOnePass[0] >= 0)
				aToOpen[aiCurOnePass[0] - iBaseCur] = 0;
			if (aiCurOnePass[1] >= 0)
				aToOpen[aiCurOnePass[1] - iBaseCur] = 0;
		}
		sqlite3OpenTableAndIndices(pParse, pTab, OP_OpenWrite, 0,
					   iBaseCur, aToOpen, 0, 0, onError, 1);
	}

	/* Top of the update loop */
	if (okOnePass) {
		labelContinue = labelBreak;
		sqlite3VdbeAddOp2(v, OP_IsNull, regKey,
				  labelBreak);
		if (aToOpen[iDataCur - iBaseCur] && !isView) {
			assert(pPk);
			sqlite3VdbeAddOp4Int(v, OP_NotFound, iDataCur,
					     labelBreak, regKey, nKey);
			VdbeCoverageNeverTaken(v);
		}
		VdbeCoverageIf(v, pPk == 0);
		VdbeCoverageIf(v, pPk != 0);
	} else {
		labelContinue = sqlite3VdbeMakeLabel(v);
		sqlite3VdbeAddOp2(v, OP_Rewind, iEph, labelBreak);
		VdbeCoverage(v);
		addrTop = sqlite3VdbeAddOp2(v, OP_RowData, iEph, regKey);
		sqlite3VdbeAddOp4Int(v, OP_NotFound, iDataCur, labelContinue,
				     regKey, 0);
		VdbeCoverage(v);
	}

	/* If the record number will change, set register regNewPk to
	 * contain the new value. If the record number is not being modified,
	 * then regNewPk is the same register as regOldPk, which is
	 * already populated.
	 */
	assert(chngPk || pTrigger || hasFK || regOldPk == regNewPk);


	/* Compute the old pre-UPDATE content of the row being changed, if that
	 * information is needed
	 */
	if (chngPk || hasFK || pTrigger) {
		u32 oldmask = (hasFK ? sqlite3FkOldmask(pParse, pTab) : 0);
		oldmask |= sqlite3TriggerColmask(pParse,
						 pTrigger, pChanges, 0,
						 TRIGGER_BEFORE | TRIGGER_AFTER,
						 pTab, onError);
		for (i = 0; i < pTab->nCol; i++) {
			if (oldmask == 0xffffffff
			    || (i < 32 && (oldmask & MASKBIT32(i)) != 0)
			    || table_column_is_in_pk(pTab, i)) {
				testcase(oldmask != 0xffffffff && i == 31);
				sqlite3ExprCodeGetColumnOfTable(v, pTab,
								iDataCur, i,
								regOld + i);
			} else {
				sqlite3VdbeAddOp2(v, OP_Null, 0, regOld + i);
			}
		}

	}

	/* Populate the array of registers beginning at regNew with the new
	 * row data. This array is used to check constants, create the new
	 * table and index records, and as the values for any new.* references
	 * made by triggers.
	 *
	 * If there are one or more BEFORE triggers, then do not populate the
	 * registers associated with columns that are (a) not modified by
	 * this UPDATE statement and (b) not accessed by new.* references. The
	 * values for registers not modified by the UPDATE must be reloaded from
	 * the database after the BEFORE triggers are fired anyway (as the trigger
	 * may have modified them). So not loading those that are not going to
	 * be used eliminates some redundant opcodes.
	 */
	newmask =
	    sqlite3TriggerColmask(pParse, pTrigger, pChanges, 1, TRIGGER_BEFORE,
				  pTab, onError);
	for (i = 0; i < pTab->nCol; i++) {
		if (i == pTab->iPKey) {
			sqlite3VdbeAddOp2(v, OP_Null, 0, regNew + i);
		} else {
			j = aXRef[i];
			if (j >= 0) {
				sqlite3ExprCode(pParse, pChanges->a[j].pExpr,
						regNew + i);
			} else if (0 == (tmask & TRIGGER_BEFORE) || i > 31
				   || (newmask & MASKBIT32(i))) {
				/* This branch loads the value of a column that will not be changed
				 * into a register. This is done if there are no BEFORE triggers, or
				 * if there are one or more BEFORE triggers that use this value via
				 * a new.* reference in a trigger program.
				 */
				testcase(i == 31);
				testcase(i == 32);
				sqlite3ExprCodeGetColumnToReg(pParse, pTab, i,
							      iDataCur,
							      regNew + i);
			} else {
				sqlite3VdbeAddOp2(v, OP_Null, 0, regNew + i);
			}
		}
	}

	/* Fire any BEFORE UPDATE triggers. This happens before constraints are
	 * verified. One could argue that this is wrong.
	 */
	if (tmask & TRIGGER_BEFORE) {
		sqlite3TableAffinity(v, pTab, regNew);
		sqlite3CodeRowTrigger(pParse, pTrigger, TK_UPDATE, pChanges,
				      TRIGGER_BEFORE, pTab, regOldPk,
				      onError, labelContinue);

		/* The row-trigger may have deleted the row being updated. In this
		 * case, jump to the next row. No updates or AFTER triggers are
		 * required. This behavior - what happens when the row being updated
		 * is deleted or renamed by a BEFORE trigger - is left undefined in the
		 * documentation.
		 */
		if (!isView) {
			sqlite3VdbeAddOp4Int(v, OP_NotFound, iDataCur,
					     labelContinue, regKey, nKey);
			VdbeCoverage(v);
		} else {
			sqlite3VdbeAddOp4Int(v, OP_NotFound, iDataCur,
					     labelContinue, regKey - nKey, nKey);
			VdbeCoverage(v);
		}

		/* If it did not delete it, the row-trigger may still have modified
		 * some of the columns of the row being updated. Load the values for
		 * all columns not modified by the update statement into their
		 * registers in case this has happened.
		 */
		for (i = 0; i < pTab->nCol; i++) {
			if (aXRef[i] < 0 && i != pTab->iPKey) {
				sqlite3ExprCodeGetColumnOfTable(v, pTab,
								iDataCur, i,
								regNew + i);
			}
		}
	}

	if (!isView) {
		int addr1 = 0;	/* Address of jump instruction */
		int bReplace = 0;	/* True if REPLACE conflict resolution might happen */

		/* Do constraint checks. */
		assert(regOldPk > 0);
		sqlite3GenerateConstraintChecks(pParse, pTab, aRegIdx, iDataCur,
						iIdxCur, regNewPk,
						regOldPk, chngPk, onError,
						labelContinue, &bReplace,
						aXRef);

		/* Do FK constraint checks. */
		if (hasFK) {
			sqlite3FkCheck(pParse, pTab, regOldPk, 0, aXRef);
		}

		/* Delete the index entries associated with the current record.  */
		if (bReplace || chngPk) {
			addr1 =
				sqlite3VdbeAddOp4Int(v, OP_NotFound,
						     iDataCur, 0, regKey,
						     nKey);
			VdbeCoverageNeverTaken(v);
		}
		sqlite3GenerateRowIndexDelete(pParse, pTab, iDataCur, iIdxCur);

		/* If changing the PK value, or if there are foreign key constraints
		 * to process, delete the old record. Otherwise, add a noop OP_Delete
		 * to invoke the pre-update hook.
		 *
		 * That (regNew==regnewPk+1) is true is also important for the
		 * pre-update hook. If the caller invokes preupdate_new(), the returned
		 * value is copied from memory cell (regNewPk+1+iCol), where iCol
		 * is the column index supplied by the user.
		 */
		assert(regNew == regNewPk + 1);
#ifdef SQLITE_ENABLE_PREUPDATE_HOOK
		sqlite3VdbeAddOp3(v, OP_Delete, iDataCur,
				  OPFLAG_ISUPDATE |
				  ((hasFK || chngKey
				    || pPk != 0) ? 0 : OPFLAG_ISNOOP),
				  regNewRowid);
#else
		if (hasFK || chngPk || pPk != 0) {
			sqlite3VdbeAddOp2(v, OP_Delete, iDataCur, 0);
		}
#endif
		if (bReplace || chngPk) {
			sqlite3VdbeJumpHere(v, addr1);
		}

		if (hasFK) {
			sqlite3FkCheck(pParse, pTab, 0, regNewPk, aXRef);
		}

		/* Insert the new index entries and the new record. */
		vdbe_emit_insertion_completion(v, iIdxCur, aRegIdx[0], onError);

		/* Do any ON CASCADE, SET NULL or SET DEFAULT operations required to
		 * handle rows (possibly in other tables) that refer via a foreign key
		 * to the row just updated.
		 */
		if (hasFK) {
			sqlite3FkActions(pParse, pTab, pChanges, regOldPk, aXRef);
		}
	}

	/* Increment the row counter
	 */
	if ((user_session->sql_flags & SQLITE_CountRows)
	    && !pParse->pTriggerTab) {
		sqlite3VdbeAddOp2(v, OP_AddImm, regRowCount, 1);
	}

	sqlite3CodeRowTrigger(pParse, pTrigger, TK_UPDATE, pChanges,
			      TRIGGER_AFTER, pTab, regOldPk, onError,
			      labelContinue);

	/* Repeat the above with the next record to be updated, until
	 * all record selected by the WHERE clause have been updated.
	 */
	if (okOnePass) {
		/* Nothing to do at end-of-loop for a single-pass */
	} else {
		sqlite3VdbeResolveLabel(v, labelContinue);
		sqlite3VdbeAddOp2(v, OP_Next, iEph, addrTop);
		VdbeCoverage(v);
	}
	sqlite3VdbeResolveLabel(v, labelBreak);

	/*
	 * Return the number of rows that were changed. If this routine is
	 * generating code because of a call to sqlite3NestedParse(), do not
	 * invoke the callback function.
	 */
	if ((user_session->sql_flags & SQLITE_CountRows) &&
	    !pParse->pTriggerTab && !pParse->nested) {
		sqlite3VdbeAddOp2(v, OP_ResultRow, regRowCount, 1);
		sqlite3VdbeSetNumCols(v, 1);
		sqlite3VdbeSetColName(v, 0, COLNAME_NAME, "rows updated",
				      SQLITE_STATIC);
	}

 update_cleanup:
	sqlite3DbFree(db, aXRef);	/* Also frees aRegIdx[] and aToOpen[] */
	sqlite3SrcListDelete(db, pTabList);
	sqlite3ExprListDelete(db, pChanges);
	sql_expr_free(db, pWhere, false);
	return;
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

