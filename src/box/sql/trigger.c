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

void
sql_trigger_begin(struct Parse *parse, struct Token *name, int tr_tm,
		  int op, struct IdList *columns, struct SrcList *table,
		  struct Expr *when, int no_err)
{
	/* The new trigger. */
	struct sql_trigger *trigger = NULL;
	/* The database connection. */
	struct sqlite3 *db = parse->db;
	/* The name of the Trigger. */
	char *trigger_name = NULL;

	struct Vdbe *v = sqlite3GetVdbe(parse);
	if (v != NULL)
		sqlite3VdbeCountChanges(v);

	/* pName->z might be NULL, but not pName itself. */
	assert(name != NULL);
	assert(op == TK_INSERT || op == TK_UPDATE || op == TK_DELETE);
	assert(op > 0 && op < 0xff);

	if (table == NULL || db->mallocFailed)
		goto trigger_cleanup;

	/*
	 * Ensure the table name matches database name and that
	 * the table exists.
	 */
	if (db->mallocFailed)
		goto trigger_cleanup;
	assert(table->nSrc == 1);

	trigger_name = sqlite3NameFromToken(db, name);
	if (trigger_name == NULL)
		goto trigger_cleanup;

	if (sqlite3CheckIdentifierName(parse, trigger_name) != SQLITE_OK)
		goto trigger_cleanup;

	const char *table_name = table->a[0].zName;
	uint32_t space_id;
	if (schema_find_id(BOX_SPACE_ID, 2, table_name, strlen(table_name),
			   &space_id) != 0)
		goto set_tarantool_error_and_cleanup;
	if (space_id == BOX_ID_NIL) {
		diag_set(ClientError, ER_NO_SUCH_SPACE, table_name);
		goto set_tarantool_error_and_cleanup;
	}

	if (!parse->parse_only) {
		const char *error_msg =
			tt_sprintf(tnt_errcode_desc(ER_TRIGGER_EXISTS),
				   trigger_name);
		char *name_copy = sqlite3DbStrDup(db, trigger_name);
		if (name_copy == NULL)
			goto trigger_cleanup;
		int name_reg = ++parse->nMem;
		sqlite3VdbeAddOp4(parse->pVdbe, OP_String8, 0, name_reg, 0,
				  name_copy, P4_DYNAMIC);
		if (vdbe_emit_halt_with_presence_test(parse, BOX_TRIGGER_ID, 0,
						      name_reg, 1,
						      ER_TRIGGER_EXISTS,
						      error_msg, (no_err != 0),
						      OP_NoConflict) != 0)
			goto trigger_cleanup;
	}

	/* Build the Trigger object. */
	trigger = (struct sql_trigger *)sqlite3DbMallocZero(db,
							    sizeof(struct
								   sql_trigger));
	if (trigger == NULL)
		goto trigger_cleanup;
	trigger->space_id = space_id;
	trigger->zName = trigger_name;
	trigger_name = NULL;

	trigger->op = (u8) op;
	trigger->tr_tm = tr_tm;
	trigger->pWhen = sqlite3ExprDup(db, when, EXPRDUP_REDUCE);
	trigger->pColumns = sqlite3IdListDup(db, columns);
	if ((when != NULL && trigger->pWhen == NULL) ||
	    (columns != NULL && trigger->pColumns == NULL))
		goto trigger_cleanup;
	assert(parse->parsed_ast.trigger == NULL);
	parse->parsed_ast.trigger = trigger;
	parse->parsed_ast_type = AST_TYPE_TRIGGER;

 trigger_cleanup:
	sqlite3DbFree(db, trigger_name);
	sqlite3SrcListDelete(db, table);
	sqlite3IdListDelete(db, columns);
	sql_expr_delete(db, when, false);
	if (parse->parsed_ast.trigger == NULL)
		sql_trigger_delete(db, trigger);
	else
		assert(parse->parsed_ast.trigger == trigger);

	return;

set_tarantool_error_and_cleanup:
	parse->rc = SQL_TARANTOOL_ERROR;
	parse->nErr++;
	goto trigger_cleanup;
}

void
sql_trigger_finish(struct Parse *parse, struct TriggerStep *step_list,
		   struct Token *token)
{
	/* Trigger being finished. */
	struct sql_trigger *trigger = parse->parsed_ast.trigger;
	/* The database. */
	struct sqlite3 *db = parse->db;

	parse->parsed_ast.trigger = NULL;
	if (NEVER(parse->nErr) || trigger == NULL)
		goto cleanup;
	char *trigger_name = trigger->zName;
	trigger->step_list = step_list;
	while (step_list != NULL) {
		step_list->trigger = trigger;
		step_list = step_list->pNext;
	}

	/* Trigger name for error reporting. */
	struct Token trigger_name_token;
	sqlite3TokenInit(&trigger_name_token, trigger->zName);

	/*
	 * Generate byte code to insert a new trigger into
	 * Tarantool for non-parsing mode or export trigger.
	 */
	if (!parse->parse_only) {
		/* Make an entry in the _trigger space. */
		struct Vdbe *v = sqlite3GetVdbe(parse);
		if (v == 0)
			goto cleanup;

		char *sql_str =
			sqlite3MPrintf(db, "CREATE TRIGGER %s", token->z);
		if (db->mallocFailed)
			goto cleanup;

		int cursor = parse->nTab++;
		struct space *_trigger = space_by_id(BOX_TRIGGER_ID);
		assert(_trigger != NULL);
		vdbe_emit_open_cursor(parse, cursor, 0, _trigger);

		/*
		 * makerecord(cursor(iRecord),
		 * [reg(first_col), reg(first_col+1)]).
		 */
		int first_col = parse->nMem + 1;
		parse->nMem += 3;
		int record = ++parse->nMem;

		uint32_t opts_buff_sz = 0;
		char *data = sql_encode_table_opts(&fiber()->gc, NULL, sql_str,
						   &opts_buff_sz);
		sqlite3DbFree(db, sql_str);
		if (data == NULL) {
			parse->nErr++;
			parse->rc = SQL_TARANTOOL_ERROR;
			goto cleanup;
		}
		char *opts_buff = sqlite3DbMallocRaw(db, opts_buff_sz);
		if (opts_buff == NULL)
			goto cleanup;
		memcpy(opts_buff, data, opts_buff_sz);

		trigger_name = sqlite3DbStrDup(db, trigger_name);
		if (trigger_name == NULL) {
			sqlite3DbFree(db, opts_buff);
			goto cleanup;
		}

		sqlite3VdbeAddOp4(v, OP_String8, 0, first_col, 0,
				  trigger_name, P4_DYNAMIC);
		sqlite3VdbeAddOp2(v, OP_Integer, trigger->space_id,
				  first_col + 1);
		sqlite3VdbeAddOp4(v, OP_Blob, opts_buff_sz, first_col + 2,
				  SQL_SUBTYPE_MSGPACK, opts_buff, P4_DYNAMIC);
		sqlite3VdbeAddOp3(v, OP_MakeRecord, first_col, 3, record);
		sqlite3VdbeAddOp2(v, OP_IdxInsert, cursor, record);

		sqlite3VdbeChangeP5(v, OPFLAG_NCHANGE);
		sqlite3VdbeAddOp1(v, OP_Close, cursor);

		sql_set_multi_write(parse, false);
	} else {
		parse->parsed_ast.trigger = trigger;
		parse->parsed_ast_type = AST_TYPE_TRIGGER;
		trigger = NULL;
	}

cleanup:
	sql_trigger_delete(db, trigger);
	assert(parse->parsed_ast.trigger == NULL || parse->parse_only);
	sqlite3DeleteTriggerStep(db, step_list);
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
sql_trigger_delete(struct sqlite3 *db, struct sql_trigger *trigger)
{
	if (trigger == NULL)
		return;
	sqlite3DeleteTriggerStep(db, trigger->step_list);
	sqlite3DbFree(db, trigger->zName);
	sql_expr_delete(db, trigger->pWhen, false);
	sqlite3IdListDelete(db, trigger->pColumns);
	sqlite3DbFree(db, trigger);
}

void
vdbe_code_drop_trigger(struct Parse *parser, const char *trigger_name,
		       bool account_changes)
{
	sqlite3 *db = parser->db;
	struct Vdbe *v = sqlite3GetVdbe(parser);
	if (v == NULL)
		return;
	/*
	 * Generate code to delete entry from _trigger and
	 * internal SQL structures.
	 */
	int trig_name_reg = ++parser->nMem;
	int record_to_delete = ++parser->nMem;
	sqlite3VdbeAddOp4(v, OP_String8, 0, trig_name_reg, 0,
			  sqlite3DbStrDup(db, trigger_name), P4_DYNAMIC);
	sqlite3VdbeAddOp3(v, OP_MakeRecord, trig_name_reg, 1,
			  record_to_delete);
	sqlite3VdbeAddOp2(v, OP_SDelete, BOX_TRIGGER_ID,
			  record_to_delete);
	if (account_changes)
		sqlite3VdbeChangeP5(v, OPFLAG_NCHANGE);
}

void
sql_drop_trigger(struct Parse *parser, struct SrcList *name, bool no_err)
{

	sqlite3 *db = parser->db;
	if (db->mallocFailed)
		goto drop_trigger_cleanup;

	struct Vdbe *v = sqlite3GetVdbe(parser);
	if (v != NULL)
		sqlite3VdbeCountChanges(v);

	assert(name->nSrc == 1);
	const char *trigger_name = name->a[0].zName;
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_NO_SUCH_TRIGGER),
			   trigger_name);
	char *name_copy = sqlite3DbStrDup(db, trigger_name);
	if (name_copy == NULL)
		goto drop_trigger_cleanup;
	int name_reg = ++parser->nMem;
	sqlite3VdbeAddOp4(v, OP_String8, 0, name_reg, 0, name_copy, P4_DYNAMIC);
	if (vdbe_emit_halt_with_presence_test(parser, BOX_TRIGGER_ID, 0,
					      name_reg, 1, ER_NO_SUCH_TRIGGER,
					      error_msg, no_err, OP_Found) != 0)
		goto drop_trigger_cleanup;

	vdbe_code_drop_trigger(parser, trigger_name, true);

 drop_trigger_cleanup:
	sqlite3SrcListDelete(db, name);
}

int
sql_trigger_replace(const char *name, uint32_t space_id,
		    struct sql_trigger *trigger,
		    struct sql_trigger **old_trigger)
{
	assert(trigger == NULL || strcmp(name, trigger->zName) == 0);

	struct space *space = space_cache_find(space_id);
	assert(space != NULL);
	*old_trigger = NULL;

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

	struct sql_trigger **ptr = &space->sql_triggers;
	while (*ptr != NULL && strcmp((*ptr)->zName, name) != 0)
		ptr = &((*ptr)->next);
	if (*ptr != NULL) {
		*old_trigger = *ptr;
		*ptr = (*ptr)->next;
	}

	if (trigger != NULL) {
		trigger->next = space->sql_triggers;
		space->sql_triggers = trigger;
	}
	return 0;
}

const char *
sql_trigger_name(struct sql_trigger *trigger)
{
	return trigger->zName;
}

uint32_t
sql_trigger_space_id(struct sql_trigger *trigger)
{
	return trigger->space_id;
}

struct sql_trigger *
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

struct sql_trigger *
sql_triggers_exist(struct Table *table, int op, struct ExprList *changes_list,
		   int *mask_ptr)
{
	int mask = 0;
	struct sql_trigger *trigger_list = NULL;
	struct session *user_session = current_session();
	if ((user_session->sql_flags & SQLITE_EnableTrigger) != 0)
		trigger_list = space_trigger_list(table->def->id);
	for (struct sql_trigger *p = trigger_list; p != NULL; p = p->next) {
		if (p->op == op && checkColumnOverlap(p->pColumns,
						      changes_list) != 0)
			mask |= p->tr_tm;
	}
	if (mask_ptr != NULL)
		*mask_ptr = mask;
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

/**
 * Create and populate a new TriggerPrg object with a sub-program
 * implementing trigger pTrigger with ON CONFLICT policy orconf.
 *
 * @param parser Current parse context.
 * @param trigger sql_trigger to code.
 * @param table trigger is attached to.
 * @param orconf ON CONFLICT policy to code trigger program with.
 *
 * @retval not NULL on success.
 * @retval NULL on error.
 */
static TriggerPrg *
sql_row_trigger_program(struct Parse *parser, struct sql_trigger *trigger,
			struct Table *table, int orconf)
{
	Parse *pTop = sqlite3ParseToplevel(parser);
	/* Database handle. */
	sqlite3 *db = parser->db;
	TriggerPrg *pPrg;	/* Value to return */
	Expr *pWhen = 0;	/* Duplicate of trigger WHEN expression */
	NameContext sNC;	/* Name context for sub-vdbe */
	SubProgram *pProgram = 0;	/* Sub-vdbe for trigger program */
	Parse *pSubParse;	/* Parse context for sub-vdbe */
	int iEndTrigger = 0;	/* Label to jump to if WHEN is false */

	assert(trigger->zName == NULL || table->def->id == trigger->space_id);
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
	pPrg->trigger = trigger;
	pPrg->orconf = orconf;
	pPrg->aColmask[0] = 0xffffffff;
	pPrg->aColmask[1] = 0xffffffff;

	/*
	 * Allocate and populate a new Parse context to use for
	 * coding the trigger sub-program.
	 */
	pSubParse = sqlite3StackAllocZero(db, sizeof(Parse));
	if (!pSubParse)
		return 0;
	sql_parser_create(pSubParse, db);
	memset(&sNC, 0, sizeof(sNC));
	sNC.pParse = pSubParse;
	pSubParse->pTriggerTab = table;
	pSubParse->pToplevel = pTop;
	pSubParse->eTriggerOp = trigger->op;
	pSubParse->nQueryLoop = parser->nQueryLoop;

	/* Temporary VM. */
	struct Vdbe *v = sqlite3GetVdbe(pSubParse);
	if (v != NULL) {
		VdbeComment((v, "Start: %s.%s (%s %s%s%s ON %s)",
			     trigger->zName, onErrorText(orconf),
			     (trigger->tr_tm ==
			      TRIGGER_BEFORE ? "BEFORE" : "AFTER"),
			     (trigger->op == TK_UPDATE ? "UPDATE" : ""),
			     (trigger->op == TK_INSERT ? "INSERT" : ""),
			     (trigger->op == TK_DELETE ? "DELETE" : ""),
			      table->def->name));
#ifndef SQLITE_OMIT_TRACE
		sqlite3VdbeChangeP4(v, -1,
				    sqlite3MPrintf(db, "-- TRIGGER %s",
						   trigger->zName),
				    P4_DYNAMIC);
#endif

		/*
		 * If one was specified, code the WHEN clause. If
		 * it evaluates to false (or NULL) the sub-vdbe is
		 * immediately halted by jumping to the OP_Halt
		 * inserted at the end of the program.
		 */
		if (trigger->pWhen != NULL) {
			pWhen = sqlite3ExprDup(db, trigger->pWhen, 0);
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
		codeTriggerProgram(pSubParse, trigger->step_list, orconf);

		/* Insert an OP_Halt at the end of the sub-program. */
		if (iEndTrigger)
			sqlite3VdbeResolveLabel(v, iEndTrigger);
		sqlite3VdbeAddOp0(v, OP_Halt);
		VdbeComment((v, "End: %s.%s", trigger->zName,
			     onErrorText(orconf)));

		transferParseError(parser, pSubParse);
		if (db->mallocFailed == 0) {
			pProgram->aOp =
			    sqlite3VdbeTakeOpArray(v, &pProgram->nOp,
						   &pTop->nMaxArg);
		}
		pProgram->nMem = pSubParse->nMem;
		pProgram->nCsr = pSubParse->nTab;
		pProgram->token = (void *)trigger;
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

/**
 * Return a pointer to a TriggerPrg object containing the
 * sub-program for trigger with default ON CONFLICT algorithm
 * orconf. If no such TriggerPrg object exists, a new object is
 * allocated and populated before being returned.
 *
 * @param parser Current parse context.
 * @param trigger Trigger to code.
 * @param table table trigger is attached to.
 * @param orconf ON CONFLICT algorithm.
 *
 * @retval not NULL on success.
 * @retval NULL on error.
 */
static TriggerPrg *
sql_row_trigger(struct Parse *parser, struct sql_trigger *trigger,
		struct Table *table, int orconf)
{
	Parse *pRoot = sqlite3ParseToplevel(parser);
	TriggerPrg *pPrg;

	assert(trigger->zName == NULL || table->def->id == trigger->space_id);

	/*
	 * It may be that this trigger has already been coded (or
	 * is in the process of being coded). If this is the case,
	 * then an entry with a matching TriggerPrg.pTrigger
	 * field will be present somewhere in the
	 * Parse.pTriggerPrg list. Search for such an entry.
	 */
	for (pPrg = pRoot->pTriggerPrg;
	     pPrg && (pPrg->trigger != trigger || pPrg->orconf != orconf);
	     pPrg = pPrg->pNext) ;

	/*
	 * If an existing TriggerPrg could not be located, create
	 * a new one.
	 */
	if (pPrg == NULL)
		pPrg = sql_row_trigger_program(parser, trigger, table, orconf);

	return pPrg;
}

void
vdbe_code_row_trigger_direct(struct Parse *parser, struct sql_trigger *trigger,
			     struct Table *table, int reg, int orconf,
			     int ignore_jump)
{
	/* Main VM. */
	struct Vdbe *v = sqlite3GetVdbe(parser);

	TriggerPrg *pPrg = sql_row_trigger(parser, trigger, table, orconf);
	assert(pPrg != NULL || parser->nErr != 0 ||
	       parser->db->mallocFailed != 0);

	/*
	 * Code the OP_Program opcode in the parent VDBE. P4 of
	 * the OP_Program is a pointer to the sub-vdbe containing
	 * the trigger program.
	 */
	if (pPrg == NULL)
		return;

	struct session *user_session = current_session();
	bool is_recursive = (trigger->zName && !(user_session->sql_flags &
						 SQLITE_RecTriggers));

	sqlite3VdbeAddOp4(v, OP_Program, reg, ignore_jump,
			  ++parser->nMem, (const char *)pPrg->pProgram,
			  P4_SUBPROGRAM);
	VdbeComment((v, "Call: %s.%s", (trigger->zName ? trigger->zName :
					"fkey"),
		     onErrorText(orconf)));

	/*
	 * Set the P5 operand of the OP_Program
	 * instruction to non-zero if recursive invocation
	 * of this trigger program is disallowed.
	 * Recursive invocation is disallowed if (a) the
	 * sub-program is really a trigger, not a foreign
	 * key action, and (b) the flag to enable
	 * recursive triggers is clear.
	 */
	sqlite3VdbeChangeP5(v, (u8)is_recursive);
}

void
vdbe_code_row_trigger(struct Parse *parser, struct sql_trigger *trigger,
		      int op, struct ExprList *changes_list, int tr_tm,
		      struct Table *table, int reg, int orconf, int ignore_jump)
{
	assert(op == TK_UPDATE || op == TK_INSERT || op == TK_DELETE);
	assert(tr_tm == TRIGGER_BEFORE || tr_tm == TRIGGER_AFTER);
	assert((op == TK_UPDATE) == (changes_list != NULL));

	for (struct sql_trigger *p = trigger; p != NULL; p = p->next) {
		/* Determine whether we should code trigger. */
		if (p->op == op && p->tr_tm == tr_tm &&
		    checkColumnOverlap(p->pColumns, changes_list)) {
			vdbe_code_row_trigger_direct(parser, p, table, reg,
						     orconf, ignore_jump);
		}
	}
}

u32
sql_trigger_colmask(Parse *parser, struct sql_trigger *trigger,
		    ExprList *changes_list, int new, int tr_tm,
		    Table *table, int orconf)
{
	const int op = changes_list != NULL ? TK_UPDATE : TK_DELETE;
	u32 mask = 0;

	assert(new == 1 || new == 0);
	for (struct sql_trigger *p = trigger; p != NULL; p = p->next) {
		if (p->op == op && (tr_tm & p->tr_tm)
		    && checkColumnOverlap(p->pColumns, changes_list)) {
			TriggerPrg *prg =
				sql_row_trigger(parser, p, table, orconf);
			if (prg != NULL)
				mask |= prg->aColmask[new];
		}
	}

	return mask;
}
