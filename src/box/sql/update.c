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
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "box/tuple_format.h"
#include "box/schema.h"

/*
 * Process an UPDATE statement.
 *
 *   UPDATE OR IGNORE table_wxyz SET a=b, c=d WHERE e<5 AND f NOT NULL;
 *          \_______/ \________/     \______/       \________________/
*            on_error   pTabList      pChanges             pWhere
 */
void
sqlUpdate(Parse * pParse,		/* The parser context */
	      SrcList * pTabList,	/* The table in which we should change things */
	      ExprList * pChanges,	/* Things to be changed */
	      Expr * pWhere,		/* The WHERE clause.  May be null */
	      enum on_conflict_action on_error)
{
	int i, j;		/* Loop counters */
	int addrTop = 0;	/* VDBE instruction address of the start of the loop */
	WhereInfo *pWInfo;	/* Information about the WHERE clause */
	Vdbe *v;		/* The virtual database engine */
	sql *db;		/* The database structure */
	int *aXRef = 0;		/* aXRef[i] is the index in pChanges->a[] of the
				 * an expression for the i-th column of the table.
				 * aXRef[i]==-1 if the i-th column is not changed.
				 */
	NameContext sNC;	/* The name-context to resolve expressions in */
	int okOnePass;		/* True for one-pass algorithm without the FIFO */
	int hasFK;		/* True if foreign key processing is required */
	int labelBreak;		/* Jump here to break out of UPDATE loop */
	int labelContinue;	/* Jump here to continue next step of UPDATE loop */

	bool is_view;		/* True when updating a view (INSTEAD OF trigger) */
	/* List of triggers on pTab, if required. */
	struct sql_trigger *trigger;
	int tmask;		/* Mask of TRIGGER_BEFORE|TRIGGER_AFTER */
	int iEph = 0;		/* Ephemeral table holding all primary key values */
	int nKey = 0;		/* Number of elements in regKey */
	int aiCurOnePass[2];	/* The write cursors opened by WHERE_ONEPASS */

	/* Register Allocations */
	int regOldPk = 0;
	int regNewPk = 0;
	int regNew = 0;		/* Content of the NEW.* table in triggers */
	int regOld = 0;		/* Content of OLD.* table in triggers */
	int regKey = 0;		/* composite PRIMARY KEY value */
	/* Count of changed rows. Match aXRef items != -1. */
	int upd_cols_cnt = 0;

	db = pParse->db;
	if (pParse->is_aborted || db->mallocFailed) {
		goto update_cleanup;
	}
	assert(pTabList->nSrc == 1);

	/* Locate the table which we want to update.
	 */
	struct space *space = sql_lookup_space(pParse, pTabList->a);
	if (space == NULL)
		goto update_cleanup;

	/* Figure out if we have any triggers and if the table being
	 * updated is a view.
	 */
	trigger = sql_triggers_exist(space->def, TK_UPDATE, pChanges,
				     pParse->sql_flags, &tmask);
	is_view = space->def->opts.is_view;
	assert(trigger != NULL || tmask == 0);

	if (is_view &&
	    sql_view_assign_cursors(pParse, space->def->opts.sql) != 0) {
		goto update_cleanup;
	}
	if (is_view && tmask == 0) {
		diag_set(ClientError, ER_ALTER_SPACE, space->def->name,
			 "space is a view");
		pParse->is_aborted = true;
		goto update_cleanup;
	}

	struct space_def *def = space->def;
	/* Allocate cursor on primary index. */
	int pk_cursor = pParse->nTab++;
	pTabList->a[0].iCursor = pk_cursor;
	struct index *pPk = space_index(space, 0);
	aXRef = region_alloc_array(&pParse->region, typeof(aXRef[0]),
				   def->field_count, &i);
	if (aXRef == NULL) {
		diag_set(OutOfMemory, i, "region_alloc_array", "aXRef");
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
		if (sqlResolveExprNames(&sNC, pChanges->a[i].pExpr)) {
			goto update_cleanup;
		}
		for (j = 0; j < (int)def->field_count; j++) {
			if (strcmp(def->fields[j].name,
				   pChanges->a[i].zName) == 0) {
				if (pPk &&
				    sql_space_column_is_in_pk(space, j))
					is_pk_modified = true;
				if (aXRef[j] != -1) {
					const char *err =
						"set id list: duplicate "\
						"column name %s";
					err = tt_sprintf(err,
							 pChanges->a[i].zName);
					diag_set(ClientError,
						 ER_SQL_PARSER_GENERIC,
						 err);
					pParse->is_aborted = true;
					goto update_cleanup;
				}
				aXRef[j] = i;
				upd_cols_cnt++;
				break;
			}
		}
		if (j >= (int)def->field_count) {
			diag_set(ClientError, ER_NO_SUCH_FIELD_NAME_IN_SPACE,
				 pChanges->a[i].zName, def->name);
			pParse->is_aborted = true;
			goto update_cleanup;
		}
	}
	/*
	 * The SET expressions are not actually used inside the
	 * WHERE loop. So reset the colUsed mask.
	 */
	pTabList->a[0].colUsed = 0;

	hasFK = fk_constraint_is_required(space, aXRef);

	/* Begin generating code. */
	v = sqlGetVdbe(pParse);
	if (v == NULL)
		goto update_cleanup;
	sqlVdbeCountChanges(v);
	sql_set_multi_write(pParse, true);

	/* Allocate required registers. */
	regOldPk = regNewPk = ++pParse->nMem;

	if (is_pk_modified || trigger != NULL || hasFK != 0) {
		regOld = pParse->nMem + 1;
		pParse->nMem += def->field_count;
		regNewPk = ++pParse->nMem;
	}
	regNew = pParse->nMem + 1;
	pParse->nMem += def->field_count + 1;

	/* If we are trying to update a view, realize that view into
	 * an ephemeral table.
	 */
	uint32_t pk_part_count;
	if (is_view) {
		sql_materialize_view(pParse, def->name, pWhere, pk_cursor);
		/* Number of columns from SELECT plus ID.*/
		pk_part_count = nKey = def->field_count + 1;
	} else {
		assert(space != NULL);
		vdbe_emit_open_cursor(pParse, pk_cursor, 0, space);
		pk_part_count = pPk->def->key_def->part_count;
	}

	/* Resolve the column names in all the expressions in the
	 * WHERE clause.
	 */
	if (sqlResolveExprNames(&sNC, pWhere)) {
		goto update_cleanup;
	}
	/* First of nPk memory cells holding PRIMARY KEY value. */
	int iPk = pParse->nMem + 1;
	pParse->nMem += pk_part_count;
	regKey = ++pParse->nMem;
	int reg_eph = ++pParse->nMem;
	iEph = pParse->nTab++;
	sqlVdbeAddOp2(v, OP_Null, 0, iPk);

	/* Address of the OpenEphemeral instruction. */
	int addrOpen = sqlVdbeAddOp2(v, OP_OpenTEphemeral, reg_eph,
					 pk_part_count);
	pWInfo = sqlWhereBegin(pParse, pTabList, pWhere, 0, 0,
				   WHERE_ONEPASS_DESIRED, pk_cursor);
	if (pWInfo == 0)
		goto update_cleanup;
	okOnePass = sqlWhereOkOnePass(pWInfo, aiCurOnePass);
	if (is_view) {
		for (i = 0; i < (int) pk_part_count; i++) {
			sqlVdbeAddOp3(v, OP_Column, pk_cursor, i, iPk + i);
		}
	} else {
		for (i = 0; i < (int) pk_part_count; i++) {
			sqlVdbeAddOp3(v, OP_Column, pk_cursor,
				      pPk->def->key_def->parts[i].fieldno,
				      iPk + i);
		}
	}

	if (okOnePass) {
		sqlVdbeChangeToNoop(v, addrOpen);
		nKey = pk_part_count;
		regKey = iPk;
	} else {
		enum field_type *types = is_view ? NULL :
					 sql_index_type_str(pParse->db,
							    pPk->def);
		sqlVdbeAddOp4(v, OP_MakeRecord, iPk, pk_part_count,
				  regKey, (char *) types, P4_DYNAMIC);
		/*
		 * Set flag to save memory allocating one by
		 * malloc.
		 */
		sqlVdbeChangeP5(v, 1);
		sqlVdbeAddOp2(v, OP_IdxInsert, regKey, reg_eph);
	}
	/* End the database scan loop.
	 */
	sqlWhereEnd(pWInfo);

	labelBreak = sqlVdbeMakeLabel(v);
	/* Top of the update loop */
	if (okOnePass) {
		labelContinue = labelBreak;
		sqlVdbeAddOp2(v, OP_IsNull, regKey, labelBreak);
		if (!is_view) {
			assert(pPk);
			sqlVdbeAddOp4Int(v, OP_NotFound, pk_cursor,
					     labelBreak, regKey, pk_part_count);
		}
	} else {
		labelContinue = sqlVdbeMakeLabel(v);
		sqlVdbeAddOp3(v, OP_IteratorOpen, iEph, 0, reg_eph);
		sqlVdbeAddOp2(v, OP_Rewind, iEph, labelBreak);
		addrTop = sqlVdbeAddOp2(v, OP_RowData, iEph, regKey);
		sqlVdbeAddOp4Int(v, OP_NotFound, pk_cursor, labelContinue,
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
		assert(space != NULL);
		uint64_t oldmask = hasFK ? space->fk_constraint_mask : 0;
		oldmask |= sql_trigger_colmask(pParse, trigger, pChanges, 0,
					       TRIGGER_BEFORE | TRIGGER_AFTER,
					       space, on_error);
		for (i = 0; i < (int)def->field_count; i++) {
			if (column_mask_fieldno_is_set(oldmask, i) ||
			    sql_space_column_is_in_pk(space, i)) {
				sqlVdbeAddOp3(v, OP_Column, pk_cursor, i,
					      regOld + i);
			} else {
				sqlVdbeAddOp2(v, OP_Null, 0, regOld + i);
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
	uint64_t newmask = sql_trigger_colmask(pParse, trigger, pChanges, 1,
					       TRIGGER_BEFORE, space, on_error);
	for (i = 0; i < (int)def->field_count; i++) {
		j = aXRef[i];
		if (j >= 0) {
			sqlExprCode(pParse, pChanges->a[j].pExpr,
					regNew + i);
		} else if ((tmask & TRIGGER_BEFORE) == 0 ||
			   column_mask_fieldno_is_set(newmask, i) != 0) {
			/* This branch loads the value of a column that will not be changed
			 * into a register. This is done if there are no BEFORE triggers, or
			 * if there are one or more BEFORE triggers that use this value via
			 * a new.* reference in a trigger program.
			 */
			sqlExprCodeGetColumnToReg(pParse, i, pk_cursor,
						  regNew + i);
		} else {
			sqlVdbeAddOp2(v, OP_Null, 0, regNew + i);
		}
	}

	/* Fire any BEFORE UPDATE triggers. This happens before constraints are
	 * verified. One could argue that this is wrong.
	 */
	if (tmask & TRIGGER_BEFORE) {
		sql_emit_table_types(v, space->def, regNew);
		vdbe_code_row_trigger(pParse, trigger, TK_UPDATE, pChanges,
				      TRIGGER_BEFORE, space, regOldPk,
				      on_error, labelContinue);

		/* The row-trigger may have deleted the row being updated. In this
		 * case, jump to the next row. No updates or AFTER triggers are
		 * required. This behavior - what happens when the row being updated
		 * is deleted or renamed by a BEFORE trigger - is left undefined in the
		 * documentation.
		 */
		if (!is_view) {
			sqlVdbeAddOp4Int(v, OP_NotFound, pk_cursor,
					     labelContinue, regKey, nKey);
		} else {
			sqlVdbeAddOp4Int(v, OP_NotFound, pk_cursor,
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
				sqlVdbeAddOp3(v, OP_Column, pk_cursor, i,
					      regNew + i);
			}
		}
	}

	if (!is_view) {
		assert(regOldPk > 0);
		vdbe_emit_constraint_checks(pParse, space, regNewPk + 1,
					    on_error, labelContinue, aXRef);
		/* Do FK constraint checks. */
		if (hasFK) {
			fk_constraint_emit_check(pParse, space, regOldPk, 0, aXRef);
		}
		if (on_error == ON_CONFLICT_ACTION_REPLACE) {
			/*
			 * Delete the index entries associated with the
			 * current record. It can be already removed by
			 * trigger or REPLACE conflict action.
			 */
			int not_found_lbl =
				sqlVdbeAddOp4Int(v, OP_NotFound, pk_cursor,
						     0, regKey, nKey);
			assert(regNew == regNewPk + 1);
			sqlVdbeAddOp2(v, OP_Delete, pk_cursor, 0);
			sqlVdbeJumpHere(v, not_found_lbl);
		}
		if (hasFK) {
			fk_constraint_emit_check(pParse, space, 0, regNewPk, aXRef);
		}
		if (on_error == ON_CONFLICT_ACTION_REPLACE) {
			 vdbe_emit_insertion_completion(v, space, regNew,
							space->def->field_count,
							on_error, 0);

		} else {
			int key_reg;
			if (okOnePass) {
				key_reg = sqlGetTempReg(pParse);
				enum field_type *types =
					sql_index_type_str(pParse->db,
							   pPk->def);
				sqlVdbeAddOp4(v, OP_MakeRecord, iPk,
						  pk_part_count, key_reg,
						  (char *) types, P4_DYNAMIC);
			} else {
				assert(nKey == 0);
				key_reg = regKey;
			}

			/* Prepare array of changed fields. */
			uint32_t upd_cols_sz = upd_cols_cnt * sizeof(uint32_t);
			uint32_t *upd_cols = sqlDbMallocRaw(db, upd_cols_sz);
			if (upd_cols == NULL)
				goto update_cleanup;
			upd_cols_cnt = 0;
			for (uint32_t i = 0; i < def->field_count; i++) {
				if (aXRef[i] == -1)
					continue;
				upd_cols[upd_cols_cnt++] = i;
			}
			int upd_cols_reg = sqlGetTempReg(pParse);
			sqlVdbeAddOp4(v, OP_Blob, upd_cols_sz, upd_cols_reg,
					0, (const char *)upd_cols, P4_DYNAMIC);
			u16 pik_flags = OPFLAG_NCHANGE;
			SET_CONFLICT_FLAG(pik_flags, on_error);
			sqlVdbeAddOp4(v, OP_Update, regNew, key_reg,
					  upd_cols_reg, (char *)space,
					  P4_SPACEPTR);
			sqlVdbeChangeP5(v, pik_flags);
		}
		/*
		 * Do any ON CASCADE, SET NULL or SET DEFAULT
		 * operations required to handle rows that refer
		 * via a foreign key to the row just updated.
		 */
		if (hasFK)
			fk_constraint_emit_actions(pParse, space, regOldPk, aXRef);
	}

	vdbe_code_row_trigger(pParse, trigger, TK_UPDATE, pChanges,
			      TRIGGER_AFTER, space, regOldPk, on_error,
			      labelContinue);

	/* Repeat the above with the next record to be updated, until
	 * all record selected by the WHERE clause have been updated.
	 */
	if (okOnePass) {
		/* Nothing to do at end-of-loop for a single-pass */
	} else {
		sqlVdbeResolveLabel(v, labelContinue);
		sqlVdbeAddOp2(v, OP_Next, iEph, addrTop);
		VdbeCoverage(v);
	}
	sqlVdbeResolveLabel(v, labelBreak);

 update_cleanup:
	sqlSrcListDelete(db, pTabList);
	sql_expr_list_delete(db, pChanges);
	sql_expr_delete(db, pWhere, false);
	return;
}
