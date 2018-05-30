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

#include "box/schema.h"
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

		sql_expr_delete(db, pTmp->pWhere, false);
		sql_expr_list_delete(db, pTmp->pExprList);
		sql_select_delete(db, pTmp->pSelect);
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
	char *zName = 0;	/* Name of the trigger */
	sqlite3 *db = pParse->db;	/* The database connection */
	DbFixer sFix;		/* State vector for the DB fixer */

	/*
	 * Do not account nested operations: the count of such
	 * operations depends on Tarantool data dictionary
	 * internals, such as data layout in system spaces.
	 */
	if (!pParse->nested) {
		Vdbe *v = sqlite3GetVdbe(pParse);
		if (v != NULL)
			sqlite3VdbeCountChanges(v);
	}
	/* pName->z might be NULL, but not pName itself. */
	assert(pName != NULL);
	assert(op == TK_INSERT || op == TK_UPDATE || op == TK_DELETE);
	assert(op > 0 && op < 0xff);

	if (pTableName == NULL || db->mallocFailed)
		goto trigger_cleanup;

	/*
	 * Ensure the table name matches database name and that
	 * the table exists.
	 */
	if (db->mallocFailed)
		goto trigger_cleanup;
	assert(pTableName->nSrc == 1);
	sqlite3FixInit(&sFix, pParse, "trigger", pName);
	if (sqlite3FixSrcList(&sFix, pTableName) != 0)
		goto trigger_cleanup;

	zName = sqlite3NameFromToken(db, pName);
	if (zName == NULL)
		goto trigger_cleanup;

	if (sqlite3CheckIdentifierName(pParse, zName) != SQLITE_OK)
		goto trigger_cleanup;

	if (!pParse->parse_only &&
	    sqlite3HashFind(&db->pSchema->trigHash, zName) != NULL) {
		if (!noErr) {
			diag_set(ClientError, ER_TRIGGER_EXISTS, zName);
			pParse->rc = SQL_TARANTOOL_ERROR;
			pParse->nErr++;
		} else {
			assert(!db->init.busy);
		}
		goto trigger_cleanup;
	}

	const char *table_name = pTableName->a[0].zName;
	uint32_t space_id;
	if (schema_find_id(BOX_SPACE_ID, 2, table_name, strlen(table_name),
			   &space_id) != 0)
		goto set_tarantool_error_and_cleanup;
	if (space_id == BOX_ID_NIL) {
		diag_set(ClientError, ER_NO_SUCH_SPACE, table_name);
		goto set_tarantool_error_and_cleanup;
	}

	/* Build the Trigger object. */
	pTrigger = (Trigger *)sqlite3DbMallocZero(db, sizeof(Trigger));
	if (pTrigger == NULL)
		goto trigger_cleanup;
	pTrigger->space_id = space_id;
	pTrigger->zName = zName;
	zName = NULL;

	pTrigger->op = (u8) op;
	pTrigger->tr_tm = tr_tm;
	pTrigger->pWhen = sqlite3ExprDup(db, pWhen, EXPRDUP_REDUCE);
	pTrigger->pColumns = sqlite3IdListDup(db, pColumns);
	if ((pWhen != NULL && pTrigger->pWhen == NULL) ||
	    (pColumns != NULL && pTrigger->pColumns == NULL))
		goto trigger_cleanup;
	assert(pParse->parsed_ast.trigger == NULL);
	pParse->parsed_ast.trigger = pTrigger;
	pParse->parsed_ast_type = AST_TYPE_TRIGGER;

 trigger_cleanup:
	sqlite3DbFree(db, zName);
	sqlite3SrcListDelete(db, pTableName);
	sqlite3IdListDelete(db, pColumns);
	sql_expr_delete(db, pWhen, false);
	if (pParse->parsed_ast.trigger == NULL)
		sql_trigger_delete(db, pTrigger);
	else
		assert(pParse->parsed_ast.trigger == pTrigger);

	return;

set_tarantool_error_and_cleanup:
	pParse->rc = SQL_TARANTOOL_ERROR;
	pParse->nErr++;
	goto trigger_cleanup;
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
	/* Trigger being finished. */
	Trigger *pTrig = pParse->parsed_ast.trigger;
	char *zName;		/* Name of trigger */
	char *zSql = 0;		/* SQL text */
	char *zOpts = 0;	/* MsgPack containing SQL options */
	sqlite3 *db = pParse->db;	/* The database */
	DbFixer sFix;		/* Fixer object */
	Token nameToken;	/* Trigger name for error reporting */

	pParse->parsed_ast.trigger = NULL;
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

	/*
	 * Generate byte code to insert a new trigger into
	 * Tarantool for non-parsing mode or export trigger.
	 */
	if (!pParse->parse_only) {
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
		pParse->nMem += 3;
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
		sqlite3VdbeAddOp2(v, OP_Integer, pTrig->space_id, iFirstCol + 1);
		sqlite3VdbeAddOp4(v, OP_Blob, zOptsSz, iFirstCol + 2,
				  MSGPACK_SUBTYPE, zOpts, P4_DYNAMIC);
		sqlite3VdbeAddOp3(v, OP_MakeRecord, iFirstCol, 3, iRecord);
		sqlite3VdbeAddOp2(v, OP_IdxInsert, iCursor, iRecord);
		/* Do not account nested operations: the count of such
		 * operations depends on Tarantool data dictionary internals,
		 * such as data layout in system spaces.
		 */
		if (!pParse->nested)
			sqlite3VdbeChangeP5(v, OPFLAG_NCHANGE);
		sqlite3VdbeAddOp1(v, OP_Close, iCursor);

		sql_set_multi_write(pParse, false);
		sqlite3ChangeCookie(pParse);
	} else {
		pParse->parsed_ast.trigger = pTrig;
		pParse->parsed_ast_type = AST_TYPE_TRIGGER;
		pTrig = NULL;
	}

 triggerfinish_cleanup:
	if (db->mallocFailed) {
		sqlite3DbFree(db, zSql);
		sqlite3DbFree(db, zOpts);
		/* No need to free zName sinceif we reach this point
		   alloc for it either wasn't called at all or failed.  */
	}
	sql_trigger_delete(db, pTrig);
	assert(pParse->parsed_ast.trigger == NULL || pParse->parse_only);
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
		sql_select_delete(db, pSelect);
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
	sql_select_delete(db, pSelect);

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
		    sql_expr_list_dup(db, pEList, EXPRDUP_REDUCE);
		pTriggerStep->pWhere =
		    sqlite3ExprDup(db, pWhere, EXPRDUP_REDUCE);
		pTriggerStep->orconf = orconf;
	}
	sql_expr_list_delete(db, pEList);
	sql_expr_delete(db, pWhere, false);
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
	sql_expr_delete(db, pWhere, false);
	return pTriggerStep;
}

void
sql_trigger_delete(struct sqlite3 *db, struct Trigger *trigger)
{
	if (trigger == NULL)
		return;
	sqlite3DeleteTriggerStep(db, trigger->step_list);
	sqlite3DbFree(db, trigger->zName);
	sql_expr_delete(db, trigger->pWhen, false);
	sqlite3IdListDelete(db, trigger->pColumns);
	sqlite3DbFree(db, trigger);
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
	}
}

int
sql_trigger_replace(struct sqlite3 *db, const char *name,
		    struct Trigger *trigger, struct Trigger **old_trigger)
{
	assert(db->pSchema != NULL);
	assert(trigger == NULL || strcmp(name, trigger->zName) == 0);

	struct Hash *hash = &db->pSchema->trigHash;

	struct Trigger *src_trigger =
		trigger != NULL ? trigger : sqlite3HashFind(hash, name);
	assert(src_trigger != NULL);
	struct space *space = space_cache_find(src_trigger->space_id);
	assert(space != NULL);

	if (trigger != NULL) {
		/* Do not create a trigger on a system space. */
		if (space_is_system(space)) {
			diag_set(ClientError, ER_SQL,
				 "cannot create trigger on system table");
			return -1;
		}
		/*
		 * INSTEAD of triggers are only for views and
		 * views only support INSTEAD of triggers.
		 */
		if (space->def->opts.is_view && trigger->tr_tm != TK_INSTEAD) {
			diag_set(ClientError, ER_SQL,
				 tt_sprintf("cannot create %s "\
                         "trigger on view: %s", trigger->tr_tm == TK_BEFORE ?
						"BEFORE" : "AFTER",
					    space->def->name));
			return -1;
		}
		if (!space->def->opts.is_view && trigger->tr_tm == TK_INSTEAD) {
			diag_set(ClientError, ER_SQL,
				 tt_sprintf("cannot create "\
                         "INSTEAD OF trigger on space: %s", space->def->name));
			return -1;
		}

		if (trigger->tr_tm == TK_BEFORE || trigger->tr_tm == TK_INSTEAD)
			trigger->tr_tm = TRIGGER_BEFORE;
		else if (trigger->tr_tm == TK_AFTER)
			trigger->tr_tm = TRIGGER_AFTER;
	}


	*old_trigger = sqlite3HashInsert(hash, name, trigger);
	if (*old_trigger == trigger) {
		diag_set(OutOfMemory, sizeof(struct HashElem),
			 "sqlite3HashInsert", "ret");
		return -1;
	}

	if (*old_trigger != NULL) {
		struct Trigger **pp;
		for (pp = &space->sql_triggers; *pp != *old_trigger;
		     pp = &((*pp)->pNext));
		*pp = (*pp)->pNext;
	}
	if (trigger != NULL) {
		trigger->pNext = space->sql_triggers;
		space->sql_triggers = trigger;
	}
	return 0;
}

const char *
sql_trigger_name(struct Trigger *trigger)
{
	return trigger->zName;
}

uint32_t
sql_trigger_space_id(struct Trigger *trigger)
{
	return trigger->space_id;
}

struct Trigger *
space_trigger_list(uint32_t space_id)
{
	struct space *space = space_cache_find(space_id);
	assert(space != NULL);
	assert(space->def != NULL);
	return space->sql_triggers;
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
	struct Trigger *trigger_list = NULL;
	struct session *user_session = current_session();
	if ((user_session->sql_flags & SQLITE_EnableTrigger) != 0)
		trigger_list = space_trigger_list(pTab->def->id);
	for (struct Trigger *p = trigger_list; p != NULL; p = p->pNext) {
		if (p->op == op && checkColumnOverlap(p->pColumns,
						      pChanges) != 0)
			mask |= p->tr_tm;
	}
	if (pMask != NULL)
		*pMask = mask;
	return mask != 0 ? trigger_list : NULL;
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

		switch (pStep->op) {
		case TK_UPDATE:{
				sqlite3Update(pParse,
					      targetSrcList(pParse, pStep),
					      sql_expr_list_dup(db,
								pStep->pExprList,
								0),
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
				sql_table_delete_from(pParse,
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
				sql_select_delete(db, pSelect);
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
	pFrom->zErrMsg = NULL;
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

	assert(pTrigger->zName == NULL || pTab->def->id == pTrigger->space_id);
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
	sql_parser_create(pSubParse, db);
	memset(&sNC, 0, sizeof(sNC));
	sNC.pParse = pSubParse;
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
			     pTab->def->name));
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
			sql_expr_delete(db, pWhen, false);
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
	sql_parser_destroy(pSubParse);
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

	assert(pTrigger->zName == NULL || pTab->def->id == pTrigger->space_id);

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
