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

void
sqlite3ColumnDefault(Vdbe *v, struct space_def *def, int i, int ireg)
{
	assert(def != 0);
	if (!def->opts.is_view) {
		sqlite3_value *pValue = 0;
		char affinity = def->fields[i].affinity;
		VdbeComment((v, "%s.%s", def->name, def->fields[i].name));
		assert(i < (int)def->field_count);

		Expr *expr = NULL;
		assert(def->fields != NULL && i < (int)def->field_count);
		if (def->fields != NULL)
			expr = def->fields[i].default_value_expr;
		sqlite3ValueFromExpr(sqlite3VdbeDb(v), expr, affinity,
				     &pValue);
		if (pValue) {
			sqlite3VdbeAppendP4(v, pValue, P4_MEM);
		}
#ifndef SQLITE_OMIT_FLOATING_POINT
		if (affinity == AFFINITY_REAL) {
			sqlite3VdbeAddOp1(v, OP_RealAffinity, ireg);
		}
#endif
	}
}

/*
 * Process an UPDATE statement.
 *
 *   UPDATE OR IGNORE table_wxyz SET a=b, c=d WHERE e<5 AND f NOT NULL;
 *          \_______/ \________/     \______/       \________________/
*            on_error   pTabList      pChanges             pWhere
 */
void
sqlite3Update(Parse * pParse,		/* The parser context */
	      SrcList * pTabList,	/* The table in which we should change things */
	      ExprList * pChanges,	/* Things to be changed */
	      Expr * pWhere,		/* The WHERE clause.  May be null */
	      enum on_conflict_action on_error)
{
	int i, j;		/* Loop counters */
	Table *pTab;		/* The table to be updated */
	int addrTop = 0;	/* VDBE instruction address of the start of the loop */
	WhereInfo *pWInfo;	/* Information about the WHERE clause */
	Vdbe *v;		/* The virtual database engine */
	Index *pPk;		/* The PRIMARY KEY index */
	sqlite3 *db;		/* The database structure */
	int *aXRef = 0;		/* aXRef[i] is the index in pChanges->a[] of the
				 * an expression for the i-th column of the table.
				 * aXRef[i]==-1 if the i-th column is not changed.
				 */
	NameContext sNC;	/* The name-context to resolve expressions in */
	int okOnePass;		/* True for one-pass algorithm without the FIFO */
	int hasFK;		/* True if foreign key processing is required */
	int labelBreak;		/* Jump here to break out of UPDATE loop */
	int labelContinue;	/* Jump here to continue next step of UPDATE loop */
	struct session *user_session = current_session();

	bool is_view;		/* True when updating a view (INSTEAD OF trigger) */
	/* List of triggers on pTab, if required. */
	struct sql_trigger *trigger;
	int tmask;		/* Mask of TRIGGER_BEFORE|TRIGGER_AFTER */
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
	pTab = sql_list_lookup_table(pParse, pTabList);
	if (pTab == NULL)
		goto update_cleanup;

	/* Figure out if we have any triggers and if the table being
	 * updated is a view.
	 */
	trigger = sql_triggers_exist(pTab, TK_UPDATE, pChanges, &tmask);
	is_view = pTab->def->opts.is_view;
	assert(trigger != NULL || tmask == 0);

	if (is_view &&
	    sql_view_assign_cursors(pParse,pTab->def->opts.sql) != 0) {
		goto update_cleanup;
	}
	if (is_view && tmask == 0) {
		sqlite3ErrorMsg(pParse, "cannot modify %s because it is a view",
				pTab->def->name);
		goto update_cleanup;
	}

	struct space_def *def = pTab->def;
	/* Allocate cursor on primary index. */
	int pk_cursor = pParse->nTab++;
	pTabList->a[0].iCursor = pk_cursor;
	pPk = is_view ? NULL : sqlite3PrimaryKeyIndex(pTab);
	i = sizeof(int) * def->field_count;
	aXRef = (int *) region_alloc(&pParse->region, i);
	if (aXRef == NULL) {
		diag_set(OutOfMemory, i, "region_alloc", "aXRef");
		goto update_cleanup;
	}
	memset(aXRef, -1, i);

	/* Initialize the name-context */
	memset(&sNC, 0, sizeof(sNC));
	sNC.pParse = pParse;
	sNC.pSrcList = pTabList;

	/* Resolve the column names in all the expressions of the
	 * of the UPDATE statement.  Also find the column index
	 * for each column to be updated in the pChanges array.
	 */
	bool is_pk_modified = false;
	for (i = 0; i < pChanges->nExpr; i++) {
		if (sqlite3ResolveExprNames(&sNC, pChanges->a[i].pExpr)) {
			goto update_cleanup;
		}
		for (j = 0; j < (int)def->field_count; j++) {
			if (strcmp(def->fields[j].name,
				   pChanges->a[i].zName) == 0) {
				if (pPk && table_column_is_in_pk(pTab, j)) {
					is_pk_modified = true;
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
		if (j >= (int)def->field_count) {
			sqlite3ErrorMsg(pParse, "no such column: %s",
					pChanges->a[i].zName);
			goto update_cleanup;
		}
	}
	/*
	 * The SET expressions are not actually used inside the
	 * WHERE loop. So reset the colUsed mask.
	 */
	pTabList->a[0].colUsed = 0;

	hasFK = fkey_is_required(pTab->def->id, aXRef);

	/* Begin generating code. */
	v = sqlite3GetVdbe(pParse);
	if (v == NULL)
		goto update_cleanup;
	sqlite3VdbeCountChanges(v);
	sql_set_multi_write(pParse, true);

	/* Allocate required registers. */
	regOldPk = regNewPk = ++pParse->nMem;

	if (is_pk_modified || trigger != NULL || hasFK != 0) {
		regOld = pParse->nMem + 1;
		pParse->nMem += def->field_count;
		regNewPk = ++pParse->nMem;
	}
	regNew = pParse->nMem + 1;
	pParse->nMem += def->field_count;

	/* If we are trying to update a view, realize that view into
	 * an ephemeral table.
	 */
	uint32_t pk_part_count;
	if (is_view) {
		sql_materialize_view(pParse, def->name, pWhere, pk_cursor);
		/* Number of columns from SELECT plus ID.*/
		pk_part_count = nKey = def->field_count + 1;
	} else {
		vdbe_emit_open_cursor(pParse, pk_cursor, 0,
				      space_by_id(pTab->def->id));
		pk_part_count = pPk->def->key_def->part_count;
	}

	/* Resolve the column names in all the expressions in the
	 * WHERE clause.
	 */
	if (sqlite3ResolveExprNames(&sNC, pWhere)) {
		goto update_cleanup;
	}
	/* First of nPk memory cells holding PRIMARY KEY value. */
	int iPk = pParse->nMem + 1;
	pParse->nMem += pk_part_count;
	regKey = ++pParse->nMem;
	iEph = pParse->nTab++;
	sqlite3VdbeAddOp2(v, OP_Null, 0, iPk);

	/* Address of the OpenEphemeral instruction. */
	int addrOpen = sqlite3VdbeAddOp2(v, OP_OpenTEphemeral, iEph,
					 pk_part_count);
	pWInfo = sqlite3WhereBegin(pParse, pTabList, pWhere, 0, 0,
				   WHERE_ONEPASS_DESIRED, pk_cursor);
	if (pWInfo == 0)
		goto update_cleanup;
	okOnePass = sqlite3WhereOkOnePass(pWInfo, aiCurOnePass);
	if (is_view) {
		for (i = 0; i < (int) pk_part_count; i++) {
			sqlite3VdbeAddOp3(v, OP_Column, pk_cursor, i, iPk + i);
		}
	} else {
		for (i = 0; i < (int) pk_part_count; i++) {
			sqlite3ExprCodeGetColumnOfTable(v, def, pk_cursor,
							pPk->def->key_def->
								parts[i].fieldno,
							iPk + i);
		}
	}

	if (okOnePass) {
		sqlite3VdbeChangeToNoop(v, addrOpen);
		nKey = pk_part_count;
		regKey = iPk;
	} else {
		const char *zAff = is_view ? 0 :
				   sql_space_index_affinity_str(pParse->db, def,
								pPk->def);
		sqlite3VdbeAddOp4(v, OP_MakeRecord, iPk, pk_part_count,
				  regKey, zAff, pk_part_count);
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
	/* Top of the update loop */
	if (okOnePass) {
		labelContinue = labelBreak;
		sqlite3VdbeAddOp2(v, OP_IsNull, regKey, labelBreak);
		if (!is_view) {
			assert(pPk);
			sqlite3VdbeAddOp4Int(v, OP_NotFound, pk_cursor,
					     labelBreak, regKey, pk_part_count);
		}
	} else {
		labelContinue = sqlite3VdbeMakeLabel(v);
		sqlite3VdbeAddOp2(v, OP_Rewind, iEph, labelBreak);
		addrTop = sqlite3VdbeAddOp2(v, OP_RowData, iEph, regKey);
		sqlite3VdbeAddOp4Int(v, OP_NotFound, pk_cursor, labelContinue,
				     regKey, 0);
	}

	/* If the record number will change, set register regNewPk to
	 * contain the new value. If the record number is not being modified,
	 * then regNewPk is the same register as regOldPk, which is
	 * already populated.
	 */
	assert(is_pk_modified || trigger != NULL || hasFK != 0 ||
	       regOldPk == regNewPk);

	/* Compute the old pre-UPDATE content of the row being changed, if that
	 * information is needed
	 */
	if (is_pk_modified || hasFK != 0 || trigger != NULL) {
		struct space *space = space_by_id(pTab->def->id);
		assert(space != NULL);
		u32 oldmask = hasFK ? space->fkey_mask : 0;
		oldmask |= sql_trigger_colmask(pParse, trigger, pChanges, 0,
					       TRIGGER_BEFORE | TRIGGER_AFTER,
					       pTab, on_error);
		for (i = 0; i < (int)def->field_count; i++) {
			if (oldmask == 0xffffffff
			    || (i < 32 && (oldmask & MASKBIT32(i)) != 0)
			    || table_column_is_in_pk(pTab, i)) {
				testcase(oldmask != 0xffffffff && i == 31);
				sqlite3ExprCodeGetColumnOfTable(v, def,
								pk_cursor, i,
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
	newmask = sql_trigger_colmask(pParse, trigger, pChanges, 1,
				      TRIGGER_BEFORE, pTab, on_error);
	for (i = 0; i < (int)def->field_count; i++) {
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
			sqlite3ExprCodeGetColumnToReg(pParse, def, i,
						      pk_cursor, regNew + i);
		} else {
			sqlite3VdbeAddOp2(v, OP_Null, 0, regNew + i);
		}
	}

	/* Fire any BEFORE UPDATE triggers. This happens before constraints are
	 * verified. One could argue that this is wrong.
	 */
	if (tmask & TRIGGER_BEFORE) {
		sql_emit_table_affinity(v, pTab->def, regNew);
		vdbe_code_row_trigger(pParse, trigger, TK_UPDATE, pChanges,
				      TRIGGER_BEFORE, pTab, regOldPk,
				      on_error, labelContinue);

		/* The row-trigger may have deleted the row being updated. In this
		 * case, jump to the next row. No updates or AFTER triggers are
		 * required. This behavior - what happens when the row being updated
		 * is deleted or renamed by a BEFORE trigger - is left undefined in the
		 * documentation.
		 */
		if (!is_view) {
			sqlite3VdbeAddOp4Int(v, OP_NotFound, pk_cursor,
					     labelContinue, regKey, nKey);
		} else {
			sqlite3VdbeAddOp4Int(v, OP_NotFound, pk_cursor,
					     labelContinue,
					     regKey - pk_part_count,
					     pk_part_count);
		}

		/* If it did not delete it, the row-trigger may still have modified
		 * some of the columns of the row being updated. Load the values for
		 * all columns not modified by the update statement into their
		 * registers in case this has happened.
		 */
		for (i = 0; i < (int)def->field_count; i++) {
			if (aXRef[i] < 0) {
				sqlite3ExprCodeGetColumnOfTable(v, def,
								pk_cursor, i,
								regNew + i);
			}
		}
	}

	if (!is_view) {
		assert(regOldPk > 0);
		vdbe_emit_constraint_checks(pParse, pTab, regNewPk + 1,
					    on_error, labelContinue, aXRef);
		/* Do FK constraint checks. */
		if (hasFK)
			fkey_emit_check(pParse, pTab, regOldPk, 0, aXRef);
		/*
		 * Delete the index entries associated with the
		 * current record. It can be already removed by
		 * trigger or REPLACE conflict action.
		 */
		int addr1 = sqlite3VdbeAddOp4Int(v, OP_NotFound, pk_cursor, 0,
						 regKey, nKey);
		assert(regNew == regNewPk + 1);
		sqlite3VdbeAddOp2(v, OP_Delete, pk_cursor, 0);
		sqlite3VdbeJumpHere(v, addr1);
		if (hasFK)
			fkey_emit_check(pParse, pTab, 0, regNewPk, aXRef);
		vdbe_emit_insertion_completion(v, pk_cursor, regNew,
					       pTab->def->field_count,
					       on_error);
		/*
		 * Do any ON CASCADE, SET NULL or SET DEFAULT
		 * operations required to handle rows that refer
		 * via a foreign key to the row just updated.
		 */
		if (hasFK)
			fkey_emit_actions(pParse, pTab, regOldPk, aXRef);
	}

	/* Increment the row counter
	 */
	if ((user_session->sql_flags & SQLITE_CountRows)
	    && !pParse->pTriggerTab) {
		sqlite3VdbeAddOp2(v, OP_AddImm, regRowCount, 1);
	}

	vdbe_code_row_trigger(pParse, trigger, TK_UPDATE, pChanges,
			      TRIGGER_AFTER, pTab, regOldPk, on_error,
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

	/* Return the number of rows that were changed. */
	if (user_session->sql_flags & SQLITE_CountRows &&
	    pParse->pTriggerTab == NULL) {
		sqlite3VdbeAddOp2(v, OP_ResultRow, regRowCount, 1);
		sqlite3VdbeSetNumCols(v, 1);
		sqlite3VdbeSetColName(v, 0, COLNAME_NAME, "rows updated",
				      SQLITE_STATIC);
	}

 update_cleanup:
	sqlite3SrcListDelete(db, pTabList);
	sql_expr_list_delete(db, pChanges);
	sql_expr_delete(db, pWhere, false);
	return;
}
