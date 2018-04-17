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
 * This file contains the implementation for TRIGGERs
 */

#include "sqliteInt.h"
#include "tarantoolInt.h"
#include "vdbeInt.h"
#include "box/session.h"

/* See comment in sqliteInt.h */
int sqlSubProgramsRemaining;

#ifndef SQLITE_OMIT_TRIGGER
/*
 * Delete a linked list of TriggerStep structures.
 */
void
sqlite3DeleteTriggerStep(sqlite3 * db, TriggerStep * pTriggerStep)
{
	while (pTriggerStep) {
		TriggerStep *pTmp = pTriggerStep;
		pTriggerStep = pTriggerStep->pNext;

		sql_expr_free(db, pTmp->pWhere, false);
		sqlite3ExprListDelete(db, pTmp->pExprList);
		sqlite3SelectDelete(db, pTmp->pSelect);
		sqlite3IdListDelete(db, pTmp->pIdList);

		sqlite3DbFree(db, pTmp);
	}
}

/*
 * This is called by the parser when it sees a CREATE TRIGGER statement
 * up to the point of the BEGIN before the trigger actions.  A Trigger
 * structure is generated based on the information available and stored
 * in pParse->pNewTrigger.  After the trigger actions have been parsed, the
 * sqlite3FinishTrigger() function is called to complete the trigger
 * construction process.
 */
void
sqlite3BeginTrigger(Parse * pParse,	/* The parse context of the CREATE TRIGGER statement */
		    Token * pName,	/* The name of the trigger */
		    int tr_tm,	/* One of TK_BEFORE, TK_AFTER, TK_INSTEAD */
		    int op,	/* One of TK_INSERT, TK_UPDATE, TK_DELETE */
		    IdList * pColumns,	/* column list if this is an UPDATE OF trigger */
		    SrcList * pTableName,	/* The name of the table/view the trigger applies to */
		    Expr * pWhen,	/* WHEN clause */
		    int noErr	/* Suppress errors if the trigger already exists */
    )
{
	Trigger *pTrigger = 0;	/* The new trigger */
	Table *pTab;		/* Table that the trigger fires off of */
	char *zName = 0;	/* Name of the trigger */
	sqlite3 *db = pParse->db;	/* The database connection */
	DbFixer sFix;		/* State vector for the DB fixer */

	/* Do not account nested operations: the count of such
	 * operations depends on Tarantool data dictionary internals,
	 * such as data layout in system spaces.
	 */
	if (!pParse->nested) {
		Vdbe *v = sqlite3GetVdbe(pParse);
		if (v != NULL)
			sqlite3VdbeCountChanges(v);
	}
	assert(pName != 0);	/* pName->z might be NULL, but not pName itself */
	assert(op == TK_INSERT || op == TK_UPDATE || op == TK_DELETE);
	assert(op > 0 && op < 0xff);

	if (!pTableName || db->mallocFailed) {
		goto trigger_cleanup;
	}

	/* Ensure the table name matches database name and that the table exists */
	if (db->mallocFailed)
		goto trigger_cleanup;
	assert(pTableName->nSrc == 1);
	sqlite3FixInit(&sFix, pParse, "trigger", pName);
	if (sqlite3FixSrcList(&sFix, pTableName)) {
		goto trigger_cleanup;
	}
	pTab = sqlite3SrcListLookup(pParse, pTableName);
	if (!pTab) {
		goto trigger_cleanup;
	}

	/* Check that the trigger name is not reserved and that no trigger of the
	 * specified name exists
	 */
	zName = sqlite3NameFromToken(db, pName);
	if (!zName || SQLITE_OK != sqlite3CheckIdentifierName(pParse, zName)) {
		goto trigger_cleanup;
	}
	if (sqlite3HashFind(&(db->pSchema->trigHash), zName)) {
		if (!noErr) {
			sqlite3ErrorMsg(pParse, "trigger %s already exists",
					zName);
		} else {
			assert(!db->init.busy);
		}
		goto trigger_cleanup;
	}

	/* Do not create a trigger on a system table */
	if (sqlite3StrNICmp(pTab->zName, "sqlite_", 7) == 0) {
		sqlite3ErrorMsg(pParse,
				"cannot create trigger on system table");
		goto trigger_cleanup;
	}

	/* INSTEAD of triggers are only for views and views only support INSTEAD
	 * of triggers.
	 */
	if (space_is_view(pTab) && tr_tm != TK_INSTEAD) {
		sqlite3ErrorMsg(pParse, "cannot create %s trigger on view: %S",
				(tr_tm == TK_BEFORE) ? "BEFORE" : "AFTER",
				pTableName, 0);
		goto trigger_cleanup;
	}
	if (!space_is_view(pTab) && tr_tm == TK_INSTEAD) {
		sqlite3ErrorMsg(pParse, "cannot create INSTEAD OF"
				" trigger on table: %S", pTableName, 0);
		goto trigger_cleanup;
	}

	/* INSTEAD OF triggers can only appear on views and BEFORE triggers
	 * cannot appear on views.  So we might as well translate every
	 * INSTEAD OF trigger into a BEFORE trigger.  It simplifies code
	 * elsewhere.
	 */
	if (tr_tm == TK_INSTEAD) {
		tr_tm = TK_BEFORE;
	}

	/* Build the Trigger object */
	pTrigger = (Trigger *) sqlite3DbMallocZero(db, sizeof(Trigger));
	if (pTrigger == 0)
		goto trigger_cleanup;
	pTrigger->zName = zName;
	zName = 0;
	pTrigger->table = sqlite3DbStrDup(db, pTableName->a[0].zName);
	pTrigger->pSchema = db->pSchema;
	pTrigger->pTabSchema = pTab->pSchema;
	pTrigger->op = (u8) op;
	pTrigger->tr_tm = tr_tm == TK_BEFORE ? TRIGGER_BEFORE : TRIGGER_AFTER;
	pTrigger->pWhen = sqlite3ExprDup(db, pWhen, EXPRDUP_REDUCE);
	pTrigger->pColumns = sqlite3IdListDup(db, pColumns);
	assert(pParse->pNewTrigger == 0);
	pParse->pNewTrigger = pTrigger;

 trigger_cleanup:
	sqlite3DbFree(db, zName);
	sqlite3SrcListDelete(db, pTableName);
	sqlite3IdListDelete(db, pColumns);
	sql_expr_free(db, pWhen, false);
	if (!pParse->pNewTrigger) {
		sqlite3DeleteTrigger(db, pTrigger);
	} else {
		assert(pParse->pNewTrigger == pTrigger);
	}
}

/*
 * This routine is called after all of the trigger actions have been parsed
 * in order to complete the process of building the trigger.
 */
void
sqlite3FinishTrigger(Parse * pParse,	/* Parser context */
		     TriggerStep * pStepList,	/* The triggered program */
		     Token * pAll	/* Token that describes the complete CREATE TRIGGER */
    )
{
	Trigger *pTrig = pParse->pNewTrigger;	/* Trigger being finished */
	char *zName;		/* Name of trigger */
	char *zSql = 0;		/* SQL text */
	char *zOpts = 0;	/* MsgPack containing SQL options */
	sqlite3 *db = pParse->db;	/* The database */
	DbFixer sFix;		/* Fixer object */
	Token nameToken;	/* Trigger name for error reporting */

	pParse->pNewTrigger = 0;
	if (NEVER(pParse->nErr) || !pTrig)
		goto triggerfinish_cleanup;
	zName = pTrig->zName;
	pTrig->step_list = pStepList;
	while (pStepList) {
		pStepList->pTrig = pTrig;
		pStepList = pStepList->pNext;
	}
	sqlite3TokenInit(&nameToken, pTrig->zName);
	sqlite3FixInit(&sFix, pParse, "trigger", &nameToken);
	if (sqlite3FixTriggerStep(&sFix, pTrig->step_list)
	    || sqlite3FixExpr(&sFix, pTrig->pWhen)
	    ) {
		goto triggerfinish_cleanup;
	}

	/* if we are not initializing,
	 * generate byte code to insert a new trigger into Tarantool.
	 */
	if (!db->init.busy) {
		Vdbe *v;
		int zOptsSz;
		Table *pSysTrigger;
		int iFirstCol;
		int iCursor = pParse->nTab++;
		int iRecord;

		/* Make an entry in the _trigger space.  */
		v = sqlite3GetVdbe(pParse);
		if (v == 0)
			goto triggerfinish_cleanup;

		pSysTrigger = sqlite3HashFind(&pParse->db->pSchema->tblHash,
					      TARANTOOL_SYS_TRIGGER_NAME);
		if (NEVER(!pSysTrigger))
			goto triggerfinish_cleanup;

		zSql = sqlite3MPrintf(db, "CREATE TRIGGER %s", pAll->z);
		if (db->mallocFailed)
			goto triggerfinish_cleanup;

		sqlite3OpenTable(pParse, iCursor, pSysTrigger, OP_OpenWrite);

		/* makerecord(cursor(iRecord), [reg(iFirstCol), reg(iFirstCol+1)])  */
		iFirstCol = pParse->nMem + 1;
		pParse->nMem += 2;
		iRecord = ++pParse->nMem;

		zOpts = sqlite3DbMallocRaw(pParse->db,
					   tarantoolSqlite3MakeTableOpts(0,
									 zSql,
									 NULL) +
					   1);
		if (db->mallocFailed)
			goto triggerfinish_cleanup;

		zOptsSz = tarantoolSqlite3MakeTableOpts(0, zSql, zOpts);

		zName = sqlite3DbStrDup(pParse->db, zName);
		if (db->mallocFailed)
			goto triggerfinish_cleanup;

		sqlite3VdbeAddOp4(v,
				  OP_String8, 0, iFirstCol, 0,
				  zName, P4_DYNAMIC);
		sqlite3VdbeAddOp4(v, OP_Blob, zOptsSz, iFirstCol + 1,
				  MSGPACK_SUBTYPE, zOpts, P4_DYNAMIC);
		sqlite3VdbeAddOp3(v, OP_MakeRecord, iFirstCol, 2, iRecord);
		sqlite3VdbeAddOp2(v, OP_IdxInsert, iCursor, iRecord);
		/* Do not account nested operations: the count of such
		 * operations depends on Tarantool data dictionary internals,
		 * such as data layout in system spaces.
		 */
		if (!pParse->nested)
			sqlite3VdbeChangeP5(v, OPFLAG_NCHANGE);
		sqlite3VdbeAddOp1(v, OP_Close, iCursor);

		/* parseschema3(reg(iFirstCol), ref(iFirstCol)+1) */
		iFirstCol = pParse->nMem + 1;
		pParse->nMem += 2;

		sql_set_multi_write(pParse, false);
		sqlite3VdbeAddOp4(v,
				  OP_String8, 0, iFirstCol, 0,
				  zName, P4_STATIC);

		sqlite3VdbeAddOp4(v,
				  OP_String8, 0, iFirstCol + 1, 0,
				  zSql, P4_DYNAMIC);
		sqlite3ChangeCookie(pParse);
		sqlite3VdbeAddParseSchema3Op(v, iFirstCol);
	}

	if (db->init.busy) {
		Trigger *pLink = pTrig;
		Hash *pHash = &db->pSchema->trigHash;
		pTrig = sqlite3HashInsert(pHash, zName, pTrig);
		if (pTrig) {
			sqlite3OomFault(db);
		} else if (pLink->pSchema == pLink->pTabSchema) {
			Table *pTab;
			pTab =
			    sqlite3HashFind(&pLink->pTabSchema->tblHash,
					    pLink->table);
			assert(pTab != 0);
			pLink->pNext = pTab->pTrigger;
			pTab->pTrigger = pLink;
		}
	}

 triggerfinish_cleanup:
	if (db->mallocFailed) {
		sqlite3DbFree(db, zSql);
		sqlite3DbFree(db, zOpts);
		/* No need to free zName sinceif we reach this point
		   alloc for it either wasn't called at all or failed.  */
	}
	sqlite3DeleteTrigger(db, pTrig);
	assert(!pParse->pNewTrigger);
	sqlite3DeleteTriggerStep(db, pStepList);
}

/*
 * Turn a SELECT statement (that the pSelect parameter points to) into
 * a trigger step.  Return a pointer to a TriggerStep structure.
 *
 * The parser calls this routine when it finds a SELECT statement in
 * body of a TRIGGER.
 */
TriggerStep *
sqlite3TriggerSelectStep(sqlite3 * db, Select * pSelect)
{
	TriggerStep *pTriggerStep =
	    sqlite3DbMallocZero(db, sizeof(TriggerStep));
	if (pTriggerStep == 0) {
		sqlite3SelectDelete(db, pSelect);
		return 0;
	}
	pTriggerStep->op = TK_SELECT;
	pTriggerStep->pSelect = pSelect;
	pTriggerStep->orconf = ON_CONFLICT_ACTION_DEFAULT;
	return pTriggerStep;
}

/*
 * Allocate space to hold a new trigger step.  The allocated space
 * holds both the TriggerStep object and the TriggerStep.target.z string.
 *
 * If an OOM error occurs, NULL is returned and db->mallocFailed is set.
 */
static TriggerStep *
triggerStepAllocate(sqlite3 * db,	/* Database connection */
		    u8 op,	/* Trigger opcode */
		    Token * pName	/* The target name */
    )
{
	TriggerStep *pTriggerStep;

	pTriggerStep =
	    sqlite3DbMallocZero(db, sizeof(TriggerStep) + pName->n + 1);
	if (pTriggerStep) {
		char *z = (char *)&pTriggerStep[1];
		memcpy(z, pName->z, pName->n);
		sqlite3NormalizeName(z);
		pTriggerStep->zTarget = z;
		pTriggerStep->op = op;
	}
	return pTriggerStep;
}

/*
 * Build a trigger step out of an INSERT statement.  Return a pointer
 * to the new trigger step.
 *
 * The parser calls this routine when it sees an INSERT inside the
 * body of a trigger.
 */
TriggerStep *
sqlite3TriggerInsertStep(sqlite3 * db,	/* The database connection */
			 Token * pTableName,	/* Name of the table into which we insert */
			 IdList * pColumn,	/* List of columns in pTableName to insert into */
			 Select * pSelect,	/* A SELECT statement that supplies values */
			 u8 orconf	/* The conflict algorithm
					 * (ON_CONFLICT_ACTION_ABORT, _REPLACE,
					 * etc.)
					 */
    )
{
	TriggerStep *pTriggerStep;

	assert(pSelect != 0 || db->mallocFailed);

	pTriggerStep = triggerStepAllocate(db, TK_INSERT, pTableName);
	if (pTriggerStep) {
		pTriggerStep->pSelect =
		    sqlite3SelectDup(db, pSelect, EXPRDUP_REDUCE);
		pTriggerStep->pIdList = pColumn;
		pTriggerStep->orconf = orconf;
	} else {
		sqlite3IdListDelete(db, pColumn);
	}
	sqlite3SelectDelete(db, pSelect);

	return pTriggerStep;
}

/*
 * Construct a trigger step that implements an UPDATE statement and return
 * a pointer to that trigger step.  The parser calls this routine when it
 * sees an UPDATE statement inside the body of a CREATE TRIGGER.
 */
TriggerStep *
sqlite3TriggerUpdateStep(sqlite3 * db,	/* The database connection */
			 Token * pTableName,	/* Name of the table to be updated */
			 ExprList * pEList,	/* The SET clause: list of column and new values */
			 Expr * pWhere,	/* The WHERE clause */
			 u8 orconf	/* The conflict algorithm.
					 * (ON_CONFLICT_ACTION_ABORT, _IGNORE,
					 * etc)
					 */
    )
{
	TriggerStep *pTriggerStep;

	pTriggerStep = triggerStepAllocate(db, TK_UPDATE, pTableName);
	if (pTriggerStep) {
		pTriggerStep->pExprList =
		    sqlite3ExprListDup(db, pEList, EXPRDUP_REDUCE);
		pTriggerStep->pWhere =
		    sqlite3ExprDup(db, pWhere, EXPRDUP_REDUCE);
		pTriggerStep->orconf = orconf;
	}
	sqlite3ExprListDelete(db, pEList);
	sql_expr_free(db, pWhere, false);
	return pTriggerStep;
}

/*
 * Construct a trigger step that implements a DELETE statement and return
 * a pointer to that trigger step.  The parser calls this routine when it
 * sees a DELETE statement inside the body of a CREATE TRIGGER.
 */
TriggerStep *
sqlite3TriggerDeleteStep(sqlite3 * db,	/* Database connection */
			 Token * pTableName,	/* The table from which rows are deleted */
			 Expr * pWhere	/* The WHERE clause */
    )
{
	TriggerStep *pTriggerStep;

	pTriggerStep = triggerStepAllocate(db, TK_DELETE, pTableName);
	if (pTriggerStep) {
		pTriggerStep->pWhere =
		    sqlite3ExprDup(db, pWhere, EXPRDUP_REDUCE);
		pTriggerStep->orconf = ON_CONFLICT_ACTION_DEFAULT;
	}
	sql_expr_free(db, pWhere, false);
	return pTriggerStep;
}

/*
 * Recursively delete a Trigger structure
 */
void
sqlite3DeleteTrigger(sqlite3 * db, Trigger * pTrigger)
{
	if (pTrigger == 0)
		return;
	sqlite3DeleteTriggerStep(db, pTrigger->step_list);
	sqlite3DbFree(db, pTrigger->zName);
	sqlite3DbFree(db, pTrigger->table);
	sql_expr_free(db, pTrigger->pWhen, false);
	sqlite3IdListDelete(db, pTrigger->pColumns);
	sqlite3DbFree(db, pTrigger);
}

/*
 * This function is called to drop a trigger from the database schema.
 *
 * This may be called directly from the parser and therefore identifies
 * the trigger by name.  The sqlite3DropTriggerPtr() routine does the
 * same job as this routine except it takes a pointer to the trigger
 * instead of the trigger name.
 */
void
sqlite3DropTrigger(Parse * pParse, SrcList * pName, int noErr)
{
	Trigger *pTrigger = 0;
	const char *zName;
	sqlite3 *db = pParse->db;

	if (db->mallocFailed)
		goto drop_trigger_cleanup;
	assert(db->pSchema != NULL);

	/* Do not account nested operations: the count of such
	 * operations depends on Tarantool data dictionary internals,
	 * such as data layout in system spaces. Activate the counter
	 * here to account DROP TRIGGER IF EXISTS case if the trigger
	 * actually does not exist.
	 */
	if (!pParse->nested) {
		Vdbe *v = sqlite3GetVdbe(pParse);
		if (v != NULL)
			sqlite3VdbeCountChanges(v);
	}

	assert(pName->nSrc == 1);
	zName = pName->a[0].zName;
	pTrigger = sqlite3HashFind(&(db->pSchema->trigHash), zName);
	if (!pTrigger) {
		if (!noErr) {
			sqlite3ErrorMsg(pParse, "no such trigger: %S", pName,
					0);
		}
		pParse->checkSchema = 1;
		goto drop_trigger_cleanup;
	}
	sqlite3DropTriggerPtr(pParse, pTrigger);

 drop_trigger_cleanup:
	sqlite3SrcListDelete(db, pName);
}

/*
 * Return a pointer to the Table structure for the table that a trigger
 * is set on.
 */
static Table *
tableOfTrigger(Trigger * pTrigger)
{
	return sqlite3HashFind(&pTrigger->pTabSchema->tblHash, pTrigger->table);
}

/*
 * Drop a trigger given a pointer to that trigger.
 */
void
sqlite3DropTriggerPtr(Parse * pParse, Trigger * pTrigger)
{
	Vdbe *v;
	/* Generate code to delete entry from _trigger and
	 * internal SQL structures.
	 */
	if ((v = sqlite3GetVdbe(pParse)) != 0) {
		int trig_name_reg = ++pParse->nMem;
		int record_to_delete = ++pParse->nMem;
		sqlite3VdbeAddOp4(v, OP_String8, 0, trig_name_reg, 0,
				  pTrigger->zName, P4_STATIC);
		sqlite3VdbeAddOp3(v, OP_MakeRecord, trig_name_reg, 1,
				  record_to_delete);
		sqlite3VdbeAddOp2(v, OP_SDelete, BOX_TRIGGER_ID,
				  record_to_delete);
		if (!pParse->nested)
			sqlite3VdbeChangeP5(v, OPFLAG_NCHANGE);

		sqlite3ChangeCookie(pParse);
		sqlite3VdbeAddOp4(v, OP_DropTrigger, 0, 0, 0, pTrigger->zName,
				  0);
	}
}

/*
 * Remove a trigger from the hash tables of the sqlite* pointer.
 */
void
sqlite3UnlinkAndDeleteTrigger(sqlite3 * db, const char *zName)
{
	Trigger *pTrigger;
	Hash *pHash;
	struct session *user_session = current_session();

	pHash = &(db->pSchema->trigHash);
	pTrigger = sqlite3HashInsert(pHash, zName, 0);
	if (ALWAYS(pTrigger)) {
		if (pTrigger->pSchema == pTrigger->pTabSchema) {
			Table *pTab = tableOfTrigger(pTrigger);
			Trigger **pp;
			for (pp = &pTab->pTrigger; *pp != pTrigger;
			     pp = &((*pp)->pNext)) ;
			*pp = (*pp)->pNext;
		}
		sqlite3DeleteTrigger(db, pTrigger);
		user_session->sql_flags |= SQLITE_InternChanges;
	}
}

/*
 * pEList is the SET clause of an UPDATE statement.  Each entry
 * in pEList is of the format <id>=<expr>.  If any of the entries
 * in pEList have an <id> which matches an identifier in pIdList,
 * then return TRUE.  If pIdList==NULL, then it is considered a
 * wildcard that matches anything.  Likewise if pEList==NULL then
 * it matches anything so always return true.  Return false only
 * if there is no match.
 */
static int
checkColumnOverlap(IdList * pIdList, ExprList * pEList)
{
	int e;
	if (pIdList == 0 || NEVER(pEList == 0))
		return 1;
	for (e = 0; e < pEList->nExpr; e++) {
		if (sqlite3IdListIndex(pIdList, pEList->a[e].zName) >= 0)
			return 1;
	}
	return 0;
}

/*
 * Return a list of all triggers on table pTab if there exists at least
 * one trigger that must be fired when an operation of type 'op' is
 * performed on the table, and, if that operation is an UPDATE, if at
 * least one of the columns in pChanges is being modified.
 */
Trigger *
sqlite3TriggersExist(Table * pTab,	/* The table the contains the triggers */
		     int op,	/* one of TK_DELETE, TK_INSERT, TK_UPDATE */
		     ExprList * pChanges,	/* Columns that change in an UPDATE statement */
		     int *pMask	/* OUT: Mask of TRIGGER_BEFORE|TRIGGER_AFTER */
    )
{
	int mask = 0;
	Trigger *pList = 0;
	Trigger *p;
	struct session *user_session = current_session();

	if ((user_session->sql_flags & SQLITE_EnableTrigger) != 0) {
		pList = pTab->pTrigger;
	}
	for (p = pList; p; p = p->pNext) {
		if (p->op == op && checkColumnOverlap(p->pColumns, pChanges)) {
			mask |= p->tr_tm;
		}
	}
	if (pMask) {
		*pMask = mask;
	}
	return (mask ? pList : 0);
}

/*
 * Convert the pStep->zTarget string into a SrcList and return a pointer
 * to that SrcList.
 */
static SrcList *
targetSrcList(Parse * pParse,	/* The parsing context */
	      TriggerStep * pStep	/* The trigger containing the target token */
    )
{
	sqlite3 *db = pParse->db;
	SrcList *pSrc;		/* SrcList to be returned */

	pSrc = sqlite3SrcListAppend(db, 0, 0);
	if (pSrc) {
		assert(pSrc->nSrc > 0);
		pSrc->a[pSrc->nSrc - 1].zName =
		    sqlite3DbStrDup(db, pStep->zTarget);
	}
	return pSrc;
}

/*
 * Generate VDBE code for the statements inside the body of a single
 * trigger.
 */
static int
codeTriggerProgram(Parse * pParse,	/* The parser context */
		   TriggerStep * pStepList,	/* List of statements inside the trigger body */
		   int orconf	/* Conflict algorithm.
				 * (ON_CONFLICT_ACTION_ABORT,
				 * etc)
				 */
    )
{
	TriggerStep *pStep;
	Vdbe *v = pParse->pVdbe;
	sqlite3 *db = pParse->db;

	assert(pParse->pTriggerTab && pParse->pToplevel);
	assert(pStepList);
	assert(v != 0);

	/* Tarantool: check if compile chain is not too long.  */
	sqlSubProgramsRemaining--;

	if (sqlSubProgramsRemaining == 0) {
		sqlite3ErrorMsg(pParse,
				"Maximum number of chained trigger activations exceeded.");
	}

	for (pStep = pStepList; pStep; pStep = pStep->pNext) {
		/* Figure out the ON CONFLICT policy that will be used for this step
		 * of the trigger program. If the statement that caused this trigger
		 * to fire had an explicit ON CONFLICT, then use it. Otherwise, use
		 * the ON CONFLICT policy that was specified as part of the trigger
		 * step statement. Example:
		 *
		 *   CREATE TRIGGER AFTER INSERT ON t1 BEGIN;
		 *     INSERT OR REPLACE INTO t2 VALUES(new.a, new.b);
		 *   END;
		 *
		 *   INSERT INTO t1 ... ;            -- insert into t2 uses REPLACE policy
		 *   INSERT OR IGNORE INTO t1 ... ;  -- insert into t2 uses IGNORE policy
		 */
		pParse->eOrconf =
		    (orconf == ON_CONFLICT_ACTION_DEFAULT) ? pStep->orconf : (u8) orconf;
		assert(pParse->okConstFactor == 0);

		switch (pStep->op) {
		case TK_UPDATE:{
				sqlite3Update(pParse,
					      targetSrcList(pParse, pStep),
					      sqlite3ExprListDup(db,
								 pStep->
								 pExprList, 0),
					      sqlite3ExprDup(db, pStep->pWhere,
							     0),
					      pParse->eOrconf);
				break;
			}
		case TK_INSERT:{
				sqlite3Insert(pParse,
					      targetSrcList(pParse, pStep),
					      sqlite3SelectDup(db,
							       pStep->pSelect,
							       0),
					      sqlite3IdListDup(db,
							       pStep->pIdList),
					      pParse->eOrconf);
				break;
			}
		case TK_DELETE:{
				sqlite3DeleteFrom(pParse,
						  targetSrcList(pParse, pStep),
						  sqlite3ExprDup(db,
								 pStep->pWhere,
								 0)
				    );
				break;
			}
		default:
			assert(pStep->op == TK_SELECT); {
				SelectDest sDest;
				Select *pSelect =
				    sqlite3SelectDup(db, pStep->pSelect, 0);
				sqlite3SelectDestInit(&sDest, SRT_Discard, 0);
				sqlite3Select(pParse, pSelect, &sDest);
				sqlite3SelectDelete(db, pSelect);
				break;
			}
		}
		if (pStep->op != TK_SELECT) {
			sqlite3VdbeAddOp0(v, OP_ResetCount);
		}
	}

	/* Tarantool: check if compile chain is not too long.  */
	sqlSubProgramsRemaining++;
	return 0;
}

#ifdef SQLITE_ENABLE_EXPLAIN_COMMENTS
/*
 * This function is used to add VdbeComment() annotations to a VDBE
 * program. It is not used in production code, only for debugging.
 */
static const char *
onErrorText(int onError)
{
	switch (onError) {
	case ON_CONFLICT_ACTION_ABORT:
		return "abort";
	case ON_CONFLICT_ACTION_ROLLBACK:
		return "rollback";
	case ON_CONFLICT_ACTION_FAIL:
		return "fail";
	case ON_CONFLICT_ACTION_REPLACE:
		return "replace";
	case ON_CONFLICT_ACTION_IGNORE:
		return "ignore";
	case ON_CONFLICT_ACTION_DEFAULT:
		return "default";
	}
	return "n/a";
}
#endif

/*
 * Parse context structure pFrom has just been used to create a sub-vdbe
 * (trigger program). If an error has occurred, transfer error information
 * from pFrom to pTo.
 */
static void
transferParseError(Parse * pTo, Parse * pFrom)
{
	assert(pFrom->zErrMsg == 0 || pFrom->nErr);
	assert(pTo->zErrMsg == 0 || pTo->nErr);
	if (pTo->nErr == 0) {
		pTo->zErrMsg = pFrom->zErrMsg;
		pTo->nErr = pFrom->nErr;
		pTo->rc = pFrom->rc;
	} else {
		sqlite3DbFree(pFrom->db, pFrom->zErrMsg);
	}
}

/*
 * Create and populate a new TriggerPrg object with a sub-program
 * implementing trigger pTrigger with ON CONFLICT policy orconf.
 */
static TriggerPrg *
codeRowTrigger(Parse * pParse,	/* Current parse context */
	       Trigger * pTrigger,	/* Trigger to code */
	       Table * pTab,	/* The table pTrigger is attached to */
	       int orconf	/* ON CONFLICT policy to code trigger program with */
    )
{
	Parse *pTop = sqlite3ParseToplevel(pParse);
	sqlite3 *db = pParse->db;	/* Database handle */
	TriggerPrg *pPrg;	/* Value to return */
	Expr *pWhen = 0;	/* Duplicate of trigger WHEN expression */
	Vdbe *v;		/* Temporary VM */
	NameContext sNC;	/* Name context for sub-vdbe */
	SubProgram *pProgram = 0;	/* Sub-vdbe for trigger program */
	Parse *pSubParse;	/* Parse context for sub-vdbe */
	int iEndTrigger = 0;	/* Label to jump to if WHEN is false */

	assert(pTrigger->zName == 0 || pTab == tableOfTrigger(pTrigger));
	assert(pTop->pVdbe);

	/* Allocate the TriggerPrg and SubProgram objects. To ensure that they
	 * are freed if an error occurs, link them into the Parse.pTriggerPrg
	 * list of the top-level Parse object sooner rather than later.
	 */
	pPrg = sqlite3DbMallocZero(db, sizeof(TriggerPrg));
	if (!pPrg)
		return 0;
	pPrg->pNext = pTop->pTriggerPrg;
	pTop->pTriggerPrg = pPrg;
	pPrg->pProgram = pProgram = sqlite3DbMallocZero(db, sizeof(SubProgram));
	if (!pProgram)
		return 0;
	sqlite3VdbeLinkSubProgram(pTop->pVdbe, pProgram);
	pPrg->pTrigger = pTrigger;
	pPrg->orconf = orconf;
	pPrg->aColmask[0] = 0xffffffff;
	pPrg->aColmask[1] = 0xffffffff;

	/* Allocate and populate a new Parse context to use for coding the
	 * trigger sub-program.
	 */
	pSubParse = sqlite3StackAllocZero(db, sizeof(Parse));
	if (!pSubParse)
		return 0;
	memset(&sNC, 0, sizeof(sNC));
	sNC.pParse = pSubParse;
	pSubParse->db = db;
	pSubParse->pTriggerTab = pTab;
	pSubParse->pToplevel = pTop;
	pSubParse->eTriggerOp = pTrigger->op;
	pSubParse->nQueryLoop = pParse->nQueryLoop;

	v = sqlite3GetVdbe(pSubParse);
	if (v) {
		VdbeComment((v, "Start: %s.%s (%s %s%s%s ON %s)",
			     pTrigger->zName, onErrorText(orconf),
			     (pTrigger->tr_tm ==
			      TRIGGER_BEFORE ? "BEFORE" : "AFTER"),
			     (pTrigger->op == TK_UPDATE ? "UPDATE" : ""),
			     (pTrigger->op == TK_INSERT ? "INSERT" : ""),
			     (pTrigger->op == TK_DELETE ? "DELETE" : ""),
			     pTab->zName));
#ifndef SQLITE_OMIT_TRACE
		sqlite3VdbeChangeP4(v, -1,
				    sqlite3MPrintf(db, "-- TRIGGER %s",
						   pTrigger->zName),
				    P4_DYNAMIC);
#endif

		/* If one was specified, code the WHEN clause. If it evaluates to false
		 * (or NULL) the sub-vdbe is immediately halted by jumping to the
		 * OP_Halt inserted at the end of the program.
		 */
		if (pTrigger->pWhen) {
			pWhen = sqlite3ExprDup(db, pTrigger->pWhen, 0);
			if (SQLITE_OK == sqlite3ResolveExprNames(&sNC, pWhen)
			    && db->mallocFailed == 0) {
				iEndTrigger = sqlite3VdbeMakeLabel(v);
				sqlite3ExprIfFalse(pSubParse, pWhen,
						   iEndTrigger,
						   SQLITE_JUMPIFNULL);
			}
			sql_expr_free(db, pWhen, false);
		}

		/* Code the trigger program into the sub-vdbe. */
		codeTriggerProgram(pSubParse, pTrigger->step_list, orconf);

		/* Insert an OP_Halt at the end of the sub-program. */
		if (iEndTrigger) {
			sqlite3VdbeResolveLabel(v, iEndTrigger);
		}
		sqlite3VdbeAddOp0(v, OP_Halt);
		VdbeComment((v, "End: %s.%s", pTrigger->zName,
			     onErrorText(orconf)));

		transferParseError(pParse, pSubParse);
		if (db->mallocFailed == 0) {
			pProgram->aOp =
			    sqlite3VdbeTakeOpArray(v, &pProgram->nOp,
						   &pTop->nMaxArg);
		}
		pProgram->nMem = pSubParse->nMem;
		pProgram->nCsr = pSubParse->nTab;
		pProgram->token = (void *)pTrigger;
		pPrg->aColmask[0] = pSubParse->oldmask;
		pPrg->aColmask[1] = pSubParse->newmask;
		sqlite3VdbeDelete(v);
	}

	assert(!pSubParse->pZombieTab);
	assert(!pSubParse->pTriggerPrg && !pSubParse->nMaxArg);
	sqlite3ParserReset(pSubParse);
	sqlite3StackFree(db, pSubParse);

	return pPrg;
}

/*
 * Return a pointer to a TriggerPrg object containing the sub-program for
 * trigger pTrigger with default ON CONFLICT algorithm orconf. If no such
 * TriggerPrg object exists, a new object is allocated and populated before
 * being returned.
 */
static TriggerPrg *
getRowTrigger(Parse * pParse,	/* Current parse context */
	      Trigger * pTrigger,	/* Trigger to code */
	      Table * pTab,	/* The table trigger pTrigger is attached to */
	      int orconf	/* ON CONFLICT algorithm. */
    )
{
	Parse *pRoot = sqlite3ParseToplevel(pParse);
	TriggerPrg *pPrg;

	assert(pTrigger->zName == 0 || pTab == tableOfTrigger(pTrigger));

	/* It may be that this trigger has already been coded (or is in the
	 * process of being coded). If this is the case, then an entry with
	 * a matching TriggerPrg.pTrigger field will be present somewhere
	 * in the Parse.pTriggerPrg list. Search for such an entry.
	 */
	for (pPrg = pRoot->pTriggerPrg;
	     pPrg && (pPrg->pTrigger != pTrigger || pPrg->orconf != orconf);
	     pPrg = pPrg->pNext) ;

	/* If an existing TriggerPrg could not be located, create a new one. */
	if (!pPrg) {
		pPrg = codeRowTrigger(pParse, pTrigger, pTab, orconf);
	}

	return pPrg;
}

/*
 * Generate code for the trigger program associated with trigger p on
 * table pTab. The reg, orconf and ignoreJump parameters passed to this
 * function are the same as those described in the header function for
 * sqlite3CodeRowTrigger()
 */
void
sqlite3CodeRowTriggerDirect(Parse * pParse,	/* Parse context */
			    Trigger * p,	/* Trigger to code */
			    Table * pTab,	/* The table to code triggers from */
			    int reg,	/* Reg array containing OLD.* and NEW.* values */
			    int orconf,	/* ON CONFLICT policy */
			    int ignoreJump	/* Instruction to jump to for RAISE(IGNORE) */
    )
{
	Vdbe *v = sqlite3GetVdbe(pParse);	/* Main VM */
	TriggerPrg *pPrg;
	struct session *user_session = current_session();

	pPrg = getRowTrigger(pParse, p, pTab, orconf);
	assert(pPrg || pParse->nErr || pParse->db->mallocFailed);

	/* Code the OP_Program opcode in the parent VDBE. P4 of the OP_Program
	 * is a pointer to the sub-vdbe containing the trigger program.
	 */
	if (pPrg) {
		int bRecursive = (p->zName &&
				  0 ==
				  (user_session->
				   sql_flags & SQLITE_RecTriggers));

		sqlite3VdbeAddOp4(v, OP_Program, reg, ignoreJump,
				  ++pParse->nMem, (const char *)pPrg->pProgram,
				  P4_SUBPROGRAM);
		VdbeComment((v, "Call: %s.%s", (p->zName ? p->zName : "fkey"),
			     onErrorText(orconf)));

		/* Set the P5 operand of the OP_Program instruction to non-zero if
		 * recursive invocation of this trigger program is disallowed. Recursive
		 * invocation is disallowed if (a) the sub-program is really a trigger,
		 * not a foreign key action, and (b) the flag to enable recursive triggers
		 * is clear.
		 */
		sqlite3VdbeChangeP5(v, (u8) bRecursive);
	}
}

/*
 * This is called to code the required FOR EACH ROW triggers for an operation
 * on table pTab. The operation to code triggers for (INSERT, UPDATE or DELETE)
 * is given by the op parameter. The tr_tm parameter determines whether the
 * BEFORE or AFTER triggers are coded. If the operation is an UPDATE, then
 * parameter pChanges is passed the list of columns being modified.
 *
 * If there are no triggers that fire at the specified time for the specified
 * operation on pTab, this function is a no-op.
 *
 * The reg argument is the address of the first in an array of registers
 * that contain the values substituted for the new.* and old.* references
 * in the trigger program. If N is the number of columns in table pTab
 * (a copy of pTab->nCol), then registers are populated as follows:
 *
 *   Register       Contains
 *   ------------------------------------------------------
 *   reg+0          OLD.PK
 *   reg+1          OLD.* value of left-most column of pTab
 *   ...            ...
 *   reg+N          OLD.* value of right-most column of pTab
 *   reg+N+1        NEW.PK
 *   reg+N+2        OLD.* value of left-most column of pTab
 *   ...            ...
 *   reg+N+N+1      NEW.* value of right-most column of pTab
 *
 * For ON DELETE triggers, the registers containing the NEW.* values will
 * never be accessed by the trigger program, so they are not allocated or
 * populated by the caller (there is no data to populate them with anyway).
 * Similarly, for ON INSERT triggers the values stored in the OLD.* registers
 * are never accessed, and so are not allocated by the caller. So, for an
 * ON INSERT trigger, the value passed to this function as parameter reg
 * is not a readable register, although registers (reg+N) through
 * (reg+N+N+1) are.
 *
 * Parameter orconf is the default conflict resolution algorithm for the
 * trigger program to use (REPLACE, IGNORE etc.). Parameter ignoreJump
 * is the instruction that control should jump to if a trigger program
 * raises an IGNORE exception.
 */
void
sqlite3CodeRowTrigger(Parse * pParse,	/* Parse context */
		      Trigger * pTrigger,	/* List of triggers on table pTab */
		      int op,	/* One of TK_UPDATE, TK_INSERT, TK_DELETE */
		      ExprList * pChanges,	/* Changes list for any UPDATE OF triggers */
		      int tr_tm,	/* One of TRIGGER_BEFORE, TRIGGER_AFTER */
		      Table * pTab,	/* The table to code triggers from */
		      int reg,	/* The first in an array of registers (see above) */
		      int orconf,	/* ON CONFLICT policy */
		      int ignoreJump	/* Instruction to jump to for RAISE(IGNORE) */
    )
{
	Trigger *p;		/* Used to iterate through pTrigger list */

	assert(op == TK_UPDATE || op == TK_INSERT || op == TK_DELETE);
	assert(tr_tm == TRIGGER_BEFORE || tr_tm == TRIGGER_AFTER);
	assert((op == TK_UPDATE) == (pChanges != 0));

	for (p = pTrigger; p; p = p->pNext) {

		/* Sanity checking:  The schema for the trigger and for the table are
		 * always defined.  The trigger must be in the same schema as the table
		 * or else it must be a TEMP trigger.
		 */
		assert(p->pSchema != 0);
		assert(p->pTabSchema != 0);
		assert(p->pSchema == p->pTabSchema);

		/* Determine whether we should code this trigger */
		if (p->op == op
		    && p->tr_tm == tr_tm
		    && checkColumnOverlap(p->pColumns, pChanges)
		    ) {
			sqlite3CodeRowTriggerDirect(pParse, p, pTab, reg,
						    orconf, ignoreJump);
		}
	}
}

/*
 * Triggers may access values stored in the old.* or new.* pseudo-table.
 * This function returns a 32-bit bitmask indicating which columns of the
 * old.* or new.* tables actually are used by triggers. This information
 * may be used by the caller, for example, to avoid having to load the entire
 * old.* record into memory when executing an UPDATE or DELETE command.
 *
 * Bit 0 of the returned mask is set if the left-most column of the
 * table may be accessed using an [old|new].<col> reference. Bit 1 is set if
 * the second leftmost column value is required, and so on. If there
 * are more than 32 columns in the table, and at least one of the columns
 * with an index greater than 32 may be accessed, 0xffffffff is returned.
 *
 * It is not possible to determine if the old.PK or new.PK column is
 * accessed by triggers. The caller must always assume that it is.
 *
 * Parameter isNew must be either 1 or 0. If it is 0, then the mask returned
 * applies to the old.* table. If 1, the new.* table.
 *
 * Parameter tr_tm must be a mask with one or both of the TRIGGER_BEFORE
 * and TRIGGER_AFTER bits set. Values accessed by BEFORE triggers are only
 * included in the returned mask if the TRIGGER_BEFORE bit is set in the
 * tr_tm parameter. Similarly, values accessed by AFTER triggers are only
 * included in the returned mask if the TRIGGER_AFTER bit is set in tr_tm.
 */
u32
sqlite3TriggerColmask(Parse * pParse,	/* Parse context */
		      Trigger * pTrigger,	/* List of triggers on table pTab */
		      ExprList * pChanges,	/* Changes list for any UPDATE OF triggers */
		      int isNew,	/* 1 for new.* ref mask, 0 for old.* ref mask */
		      int tr_tm,	/* Mask of TRIGGER_BEFORE|TRIGGER_AFTER */
		      Table * pTab,	/* The table to code triggers from */
		      int orconf	/* Default ON CONFLICT policy for trigger steps */
    )
{
	const int op = pChanges ? TK_UPDATE : TK_DELETE;
	u32 mask = 0;
	Trigger *p;

	assert(isNew == 1 || isNew == 0);
	for (p = pTrigger; p; p = p->pNext) {
		if (p->op == op && (tr_tm & p->tr_tm)
		    && checkColumnOverlap(p->pColumns, pChanges)
		    ) {
			TriggerPrg *pPrg;
			pPrg = getRowTrigger(pParse, p, pTab, orconf);
			if (pPrg) {
				mask |= pPrg->aColmask[isNew];
			}
		}
	}

	return mask;
}

#endif				/* !defined(SQLITE_OMIT_TRIGGER) */
