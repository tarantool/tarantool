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

#include "box/box.h"
#include "box/schema.h"
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "vdbeInt.h"

/* See comment in sqlInt.h */
int sqlSubProgramsRemaining;

/*
 * Delete a linked list of TriggerStep structures.
 */
void
sqlDeleteTriggerStep(sql * db, TriggerStep * pTriggerStep)
{
	while (pTriggerStep) {
		TriggerStep *pTmp = pTriggerStep;
		pTriggerStep = pTriggerStep->pNext;

		sql_expr_delete(db, pTmp->pWhere, false);
		sql_expr_list_delete(db, pTmp->pExprList);
		sql_select_delete(db, pTmp->pSelect);
		sqlIdListDelete(db, pTmp->pIdList);

		sqlDbFree(db, pTmp);
	}
}

void
sql_trigger_begin(struct Parse *parse)
{
	/* The new trigger. */
	struct sql_trigger *trigger = NULL;
	/* The database connection. */
	struct sql *db = parse->db;
	struct create_trigger_def *trigger_def = &parse->create_trigger_def;
	struct create_entity_def *create_def = &trigger_def->base;
	struct alter_entity_def *alter_def = &create_def->base;
	assert(alter_def->entity_type == ENTITY_TYPE_TRIGGER);
	assert(alter_def->alter_action == ALTER_ACTION_CREATE);

	char *trigger_name = NULL;
	if (alter_def->entity_name == NULL || db->mallocFailed)
		goto trigger_cleanup;
	assert(alter_def->entity_name->nSrc == 1);
	assert(create_def->name.n > 0);
	trigger_name = sql_name_from_token(db, &create_def->name);
	if (trigger_name == NULL)
		goto set_tarantool_error_and_cleanup;

	if (sqlCheckIdentifierName(parse, trigger_name) != 0)
		goto trigger_cleanup;

	const char *table_name = alter_def->entity_name->a[0].zName;
	uint32_t space_id = box_space_id_by_name(table_name,
						 strlen(table_name));
	if (space_id == BOX_ID_NIL) {
		diag_set(ClientError, ER_NO_SUCH_SPACE, table_name);
		goto set_tarantool_error_and_cleanup;
	}

	if (!parse->parse_only) {
		struct Vdbe *v = sqlGetVdbe(parse);
		if (v != NULL)
			sqlVdbeCountChanges(v);
		const char *error_msg =
			tt_sprintf(tnt_errcode_desc(ER_TRIGGER_EXISTS),
				   trigger_name);
		char *name_copy = sqlDbStrDup(db, trigger_name);
		if (name_copy == NULL)
			goto trigger_cleanup;
		int name_reg = ++parse->nMem;
		sqlVdbeAddOp4(parse->pVdbe, OP_String8, 0, name_reg, 0,
				  name_copy, P4_DYNAMIC);
		bool no_err = create_def->if_not_exist;
		if (vdbe_emit_halt_with_presence_test(parse, BOX_TRIGGER_ID, 0,
						      name_reg, 1,
						      ER_TRIGGER_EXISTS,
						      error_msg, (no_err != 0),
						      OP_NoConflict) != 0)
			goto trigger_cleanup;
	}

	/* Build the Trigger object. */
	trigger = (struct sql_trigger *)sqlDbMallocZero(db,
							    sizeof(struct
								   sql_trigger));
	if (trigger == NULL)
		goto trigger_cleanup;
	trigger->space_id = space_id;
	trigger->zName = trigger_name;
	trigger_name = NULL;
	assert(trigger_def->op == TK_INSERT || trigger_def->op == TK_UPDATE ||
	       trigger_def->op== TK_DELETE);
	trigger->op = (u8) trigger_def->op;
	trigger->tr_tm = trigger_def->tr_tm;
	trigger->pWhen = sqlExprDup(db, trigger_def->when, EXPRDUP_REDUCE);
	trigger->pColumns = sqlIdListDup(db, trigger_def->cols);
	if ((trigger->pWhen != NULL && trigger->pWhen == NULL) ||
	    (trigger->pColumns != NULL && trigger->pColumns == NULL))
		goto trigger_cleanup;
	assert(parse->parsed_ast.trigger == NULL);
	parse->parsed_ast.trigger = trigger;
	parse->parsed_ast.ast_type = AST_TYPE_TRIGGER;

 trigger_cleanup:
	sqlDbFree(db, trigger_name);
	sqlSrcListDelete(db, alter_def->entity_name);
	sqlIdListDelete(db, trigger_def->cols);
	sql_expr_delete(db, trigger_def->when, false);
	if (parse->parsed_ast.trigger == NULL)
		sql_trigger_delete(db, trigger);
	else
		assert(parse->parsed_ast.trigger == trigger);

	return;

set_tarantool_error_and_cleanup:
	parse->is_aborted = true;
	goto trigger_cleanup;
}

void
sql_trigger_finish(struct Parse *parse, struct TriggerStep *step_list,
		   struct Token *token)
{
	/* Trigger being finished. */
	struct sql_trigger *trigger = parse->parsed_ast.trigger;
	/* The database. */
	struct sql *db = parse->db;

	parse->parsed_ast.trigger = NULL;
	if (NEVER(parse->is_aborted) || trigger == NULL)
		goto cleanup;
	char *trigger_name = trigger->zName;
	trigger->step_list = step_list;
	while (step_list != NULL) {
		step_list = step_list->pNext;
	}

	/* Trigger name for error reporting. */
	struct Token trigger_name_token;
	sqlTokenInit(&trigger_name_token, trigger->zName);

	/*
	 * Generate byte code to insert a new trigger into
	 * Tarantool for non-parsing mode or export trigger.
	 */
	if (!parse->parse_only) {
		/* Make an entry in the _trigger space. */
		struct Vdbe *v = sqlGetVdbe(parse);
		if (v == 0)
			goto cleanup;

		char *sql_str =
			sqlMPrintf(db, "CREATE TRIGGER %s", token->z);
		if (db->mallocFailed)
			goto cleanup;

		struct space *_trigger = space_by_id(BOX_TRIGGER_ID);
		assert(_trigger != NULL);

		int first_col = parse->nMem + 1;
		parse->nMem += 3;
		int record = ++parse->nMem;
		int sql_str_len = strlen(sql_str);
		int sql_len = strlen("sql");

		uint32_t opts_buff_sz = mp_sizeof_map(1) +
					mp_sizeof_str(sql_len) +
					mp_sizeof_str(sql_str_len);
		char *opts_buff = (char *) sqlDbMallocRaw(db, opts_buff_sz);
		if (opts_buff == NULL)
			goto cleanup;

		char *data = mp_encode_map(opts_buff, 1);
		data = mp_encode_str(data, "sql", sql_len);
		data = mp_encode_str(data, sql_str, sql_str_len);
		sqlDbFree(db, sql_str);

		trigger_name = sqlDbStrDup(db, trigger_name);
		if (trigger_name == NULL) {
			sqlDbFree(db, opts_buff);
			goto cleanup;
		}

		sqlVdbeAddOp4(v, OP_String8, 0, first_col, 0,
				  trigger_name, P4_DYNAMIC);
		sqlVdbeAddOp2(v, OP_Integer, trigger->space_id,
				  first_col + 1);
		sqlVdbeAddOp4(v, OP_Blob, opts_buff_sz, first_col + 2,
				  SQL_SUBTYPE_MSGPACK, opts_buff, P4_DYNAMIC);
		sqlVdbeAddOp3(v, OP_MakeRecord, first_col, 3, record);
		sqlVdbeAddOp4(v, OP_IdxInsert, record, 0, 0,
				  (char *)_trigger, P4_SPACEPTR);

		sqlVdbeChangeP5(v, OPFLAG_NCHANGE);

		sql_set_multi_write(parse, false);
	} else {
		parse->parsed_ast.trigger = trigger;
		parse->parsed_ast.ast_type = AST_TYPE_TRIGGER;
		trigger = NULL;
	}

cleanup:
	sql_trigger_delete(db, trigger);
	assert(parse->parsed_ast.trigger == NULL || parse->parse_only);
	sqlDeleteTriggerStep(db, step_list);
}

struct TriggerStep *
sql_trigger_select_step(struct sql *db, struct Select *select)
{
	struct TriggerStep *trigger_step =
		sqlDbMallocZero(db, sizeof(struct TriggerStep));
	if (trigger_step == NULL) {
		sql_select_delete(db, select);
		diag_set(OutOfMemory, sizeof(struct TriggerStep),
			 "sqlDbMallocZero", "trigger_step");
		return NULL;
	}
	trigger_step->op = TK_SELECT;
	trigger_step->pSelect = select;
	trigger_step->orconf = ON_CONFLICT_ACTION_DEFAULT;
	return trigger_step;
}

/*
 * Allocate space to hold a new trigger step.  The allocated space
 * holds both the TriggerStep object and the TriggerStep.target.z
 * string.
 *
 * @param db The database connection.
 * @param op Trigger opcode.
 * @param target_name The target name token.
 * @retval Not NULL TriggerStep object on success.
 * @retval NULL Otherwise. The diag message is set.
 */
static struct TriggerStep *
sql_trigger_step_new(struct sql *db, u8 op, struct Token *target_name)
{
	int name_size = target_name->n + 1;
	int size = sizeof(struct TriggerStep) + name_size;
	struct TriggerStep *trigger_step = sqlDbMallocZero(db, size);
	if (trigger_step == NULL) {
		diag_set(OutOfMemory, size, "sqlDbMallocZero", "trigger_step");
		return NULL;
	}
	char *z = (char *)&trigger_step[1];
	int rc = sql_normalize_name(z, name_size, target_name->z,
				    target_name->n);
	if (rc > name_size) {
		name_size = rc;
		trigger_step = sqlDbReallocOrFree(db, trigger_step,
						  sizeof(*trigger_step) +
						  name_size);
		if (trigger_step == NULL)
			return NULL;
		z = (char *) &trigger_step[1];
		if (sql_normalize_name(z, name_size, target_name->z,
				       target_name->n) > name_size)
			unreachable();
	}
	trigger_step->zTarget = z;
	trigger_step->op = op;
	return trigger_step;
}

struct TriggerStep *
sql_trigger_insert_step(struct sql *db, struct Token *table_name,
			struct IdList *column_list, struct Select *select,
			enum on_conflict_action orconf)
{
	assert(select != NULL || db->mallocFailed);
	struct TriggerStep *trigger_step =
		sql_trigger_step_new(db, TK_INSERT, table_name);
	if (trigger_step != NULL) {
		trigger_step->pSelect =
			sqlSelectDup(db, select, EXPRDUP_REDUCE);
		trigger_step->pIdList = column_list;
		trigger_step->orconf = orconf;
	} else {
		sqlIdListDelete(db, column_list);
	}
	sql_select_delete(db, select);
	return trigger_step;
}

struct TriggerStep *
sql_trigger_update_step(struct sql *db, struct Token *table_name,
		        struct ExprList *new_list, struct Expr *where,
			enum on_conflict_action orconf)
{
	struct TriggerStep *trigger_step =
		sql_trigger_step_new(db, TK_UPDATE, table_name);
	if (trigger_step != NULL) {
		trigger_step->pExprList =
		    sql_expr_list_dup(db, new_list, EXPRDUP_REDUCE);
		trigger_step->pWhere = sqlExprDup(db, where, EXPRDUP_REDUCE);
		trigger_step->orconf = orconf;
	}
	sql_expr_list_delete(db, new_list);
	sql_expr_delete(db, where, false);
	return trigger_step;
}

struct TriggerStep *
sql_trigger_delete_step(struct sql *db, struct Token *table_name,
			struct Expr *where)
{
	struct TriggerStep *trigger_step =
		sql_trigger_step_new(db, TK_DELETE, table_name);
	if (trigger_step != NULL) {
		trigger_step->pWhere = sqlExprDup(db, where, EXPRDUP_REDUCE);
		trigger_step->orconf = ON_CONFLICT_ACTION_DEFAULT;
	}
	sql_expr_delete(db, where, false);
	return trigger_step;
}

void
sql_trigger_delete(struct sql *db, struct sql_trigger *trigger)
{
	if (trigger == NULL)
		return;
	sqlDeleteTriggerStep(db, trigger->step_list);
	sqlDbFree(db, trigger->zName);
	sql_expr_delete(db, trigger->pWhen, false);
	sqlIdListDelete(db, trigger->pColumns);
	sqlDbFree(db, trigger);
}

void
vdbe_code_drop_trigger(struct Parse *parser, const char *trigger_name,
		       bool account_changes)
{
	sql *db = parser->db;
	struct Vdbe *v = sqlGetVdbe(parser);
	if (v == NULL)
		return;
	/*
	 * Generate code to delete entry from _trigger and
	 * internal SQL structures.
	 */
	int trig_name_reg = ++parser->nMem;
	int record_to_delete = ++parser->nMem;
	sqlVdbeAddOp4(v, OP_String8, 0, trig_name_reg, 0,
			  sqlDbStrDup(db, trigger_name), P4_DYNAMIC);
	sqlVdbeAddOp3(v, OP_MakeRecord, trig_name_reg, 1,
			  record_to_delete);
	sqlVdbeAddOp2(v, OP_SDelete, BOX_TRIGGER_ID,
			  record_to_delete);
	if (account_changes)
		sqlVdbeChangeP5(v, OPFLAG_NCHANGE);
}

void
sql_drop_trigger(struct Parse *parser)
{
	struct drop_entity_def *drop_def = &parser->drop_trigger_def.base;
	struct alter_entity_def *alter_def = &drop_def->base;
	assert(alter_def->entity_type == ENTITY_TYPE_TRIGGER);
	assert(alter_def->alter_action == ALTER_ACTION_DROP);
	struct SrcList *name = alter_def->entity_name;
	bool no_err = drop_def->if_exist;
	sql *db = parser->db;
	if (db->mallocFailed)
		goto drop_trigger_cleanup;

	struct Vdbe *v = sqlGetVdbe(parser);
	if (v != NULL)
		sqlVdbeCountChanges(v);

	assert(name->nSrc == 1);
	const char *trigger_name = name->a[0].zName;
	const char *error_msg =
		tt_sprintf(tnt_errcode_desc(ER_NO_SUCH_TRIGGER),
			   trigger_name);
	char *name_copy = sqlDbStrDup(db, trigger_name);
	if (name_copy == NULL)
		goto drop_trigger_cleanup;
	int name_reg = ++parser->nMem;
	sqlVdbeAddOp4(v, OP_String8, 0, name_reg, 0, name_copy, P4_DYNAMIC);
	if (vdbe_emit_halt_with_presence_test(parser, BOX_TRIGGER_ID, 0,
					      name_reg, 1, ER_NO_SUCH_TRIGGER,
					      error_msg, no_err, OP_Found) != 0)
		goto drop_trigger_cleanup;

	vdbe_code_drop_trigger(parser, trigger_name, true);

 drop_trigger_cleanup:
	sqlSrcListDelete(db, name);
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
			diag_set(ClientError, ER_SQL_EXECUTE,
				 "cannot create trigger on system table");
			return -1;
		}
		/*
		 * INSTEAD of triggers are only for views and
		 * views only support INSTEAD of triggers.
		 */
		if (space->def->opts.is_view && trigger->tr_tm != TK_INSTEAD) {
			diag_set(ClientError, ER_SQL_EXECUTE,
				 tt_sprintf("cannot create %s "\
                         "trigger on view: %s", trigger->tr_tm == TK_BEFORE ?
						"BEFORE" : "AFTER",
					    space->def->name));
			return -1;
		}
		if (!space->def->opts.is_view && trigger->tr_tm == TK_INSTEAD) {
			diag_set(ClientError, ER_SQL_EXECUTE,
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
		if (sqlIdListIndex(pIdList, pEList->a[e].zName) >= 0)
			return 1;
	}
	return 0;
}

struct sql_trigger *
sql_triggers_exist(struct space_def *space_def, int op,
		   struct ExprList *changes_list, uint32_t sql_flags,
		   int *mask_ptr)
{
	int mask = 0;
	struct sql_trigger *trigger_list = NULL;
	if ((sql_flags & SQL_EnableTrigger) != 0)
		trigger_list = space_trigger_list(space_def->id);
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
	sql *db = pParse->db;
	SrcList *pSrc;		/* SrcList to be returned */

	pSrc = sql_src_list_append(db, 0, 0);
	if (pSrc == NULL) {
		pParse->is_aborted = true;
		return NULL;
	}
	assert(pSrc->nSrc > 0);
	pSrc->a[pSrc->nSrc - 1].zName = sqlDbStrDup(db, pStep->zTarget);
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
	sql *db = pParse->db;

	assert(pParse->triggered_space != NULL && pParse->pToplevel != NULL);
	assert(pStepList);
	assert(v != 0);

	/* Tarantool: check if compile chain is not too long.  */
	sqlSubProgramsRemaining--;

	if (sqlSubProgramsRemaining == 0) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC, "Maximum number "\
			 "of chained trigger activations exceeded.");
		pParse->is_aborted = true;
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
				sqlUpdate(pParse,
					      targetSrcList(pParse, pStep),
					      sql_expr_list_dup(db,
								pStep->pExprList,
								0),
					      sqlExprDup(db, pStep->pWhere,
							     0),
					      pParse->eOrconf);
				break;
			}
		case TK_INSERT:{
				sqlInsert(pParse,
					      targetSrcList(pParse, pStep),
					      sqlSelectDup(db,
							       pStep->pSelect,
							       0),
					      sqlIdListDup(db,
							       pStep->pIdList),
					      pParse->eOrconf);
				break;
			}
		case TK_DELETE:{
				sql_table_delete_from(pParse,
						      targetSrcList(pParse, pStep),
						      sqlExprDup(db,
								     pStep->pWhere,
								     0)
				    );
				break;
			}
		default:
			assert(pStep->op == TK_SELECT); {
				SelectDest sDest;
				Select *pSelect =
				    sqlSelectDup(db, pStep->pSelect, 0);
				sqlSelectDestInit(&sDest, SRT_Discard, 0, -1);
				sqlSelect(pParse, pSelect, &sDest);
				sql_select_delete(db, pSelect);
				break;
			}
		}
		if (pStep->op != TK_SELECT) {
			sqlVdbeAddOp0(v, OP_ResetCount);
		}
	}

	/* Tarantool: check if compile chain is not too long.  */
	sqlSubProgramsRemaining++;
	return 0;
}

#ifdef SQL_ENABLE_EXPLAIN_COMMENTS
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

/**
 * Create and populate a new TriggerPrg object with a sub-program
 * implementing trigger pTrigger with ON CONFLICT policy orconf.
 *
 * @param parser Current parse context.
 * @param trigger sql_trigger to code.
 * @param space Trigger is attached to.
 * @param orconf ON CONFLICT policy to code trigger program with.
 *
 * @retval not NULL on success.
 * @retval NULL on error.
 */
static TriggerPrg *
sql_row_trigger_program(struct Parse *parser, struct sql_trigger *trigger,
			struct space *space, int orconf)
{
	Parse *pTop = sqlParseToplevel(parser);
	/* Database handle. */
	sql *db = parser->db;
	TriggerPrg *pPrg;	/* Value to return */
	Expr *pWhen = 0;	/* Duplicate of trigger WHEN expression */
	NameContext sNC;	/* Name context for sub-vdbe */
	SubProgram *pProgram = 0;	/* Sub-vdbe for trigger program */
	Parse *pSubParse;	/* Parse context for sub-vdbe */
	int iEndTrigger = 0;	/* Label to jump to if WHEN is false */

	assert(trigger->zName == NULL || space->def->id == trigger->space_id);
	assert(pTop->pVdbe);

	/* Allocate the TriggerPrg and SubProgram objects. To ensure that they
	 * are freed if an error occurs, link them into the Parse.pTriggerPrg
	 * list of the top-level Parse object sooner rather than later.
	 */
	pPrg = sqlDbMallocZero(db, sizeof(TriggerPrg));
	if (!pPrg)
		return 0;
	pPrg->pNext = pTop->pTriggerPrg;
	pTop->pTriggerPrg = pPrg;
	pPrg->pProgram = pProgram = sqlDbMallocZero(db, sizeof(SubProgram));
	if (!pProgram)
		return 0;
	sqlVdbeLinkSubProgram(pTop->pVdbe, pProgram);
	pPrg->trigger = trigger;
	pPrg->orconf = orconf;
	pPrg->column_mask[0] = COLUMN_MASK_FULL;
	pPrg->column_mask[1] = COLUMN_MASK_FULL;

	/*
	 * Allocate and populate a new Parse context to use for
	 * coding the trigger sub-program.
	 */
	pSubParse = sqlStackAllocZero(db, sizeof(Parse));
	if (!pSubParse)
		return 0;
	sql_parser_create(pSubParse, db, parser->sql_flags);
	memset(&sNC, 0, sizeof(sNC));
	sNC.pParse = pSubParse;
	pSubParse->triggered_space = space;
	pSubParse->pToplevel = pTop;
	pSubParse->eTriggerOp = trigger->op;
	pSubParse->nQueryLoop = parser->nQueryLoop;

	/* Temporary VM. */
	struct Vdbe *v = sqlGetVdbe(pSubParse);
	if (v != NULL) {
		VdbeComment((v, "Start: %s.%s (%s %s%s%s ON %s)",
			     trigger->zName, onErrorText(orconf),
			     (trigger->tr_tm ==
			      TRIGGER_BEFORE ? "BEFORE" : "AFTER"),
			     (trigger->op == TK_UPDATE ? "UPDATE" : ""),
			     (trigger->op == TK_INSERT ? "INSERT" : ""),
			     (trigger->op == TK_DELETE ? "DELETE" : ""),
			      space->def->name));
		sqlVdbeChangeP4(v, -1,
				    sqlMPrintf(db, "-- TRIGGER %s",
						   trigger->zName),
				    P4_DYNAMIC);

		/*
		 * If one was specified, code the WHEN clause. If
		 * it evaluates to false (or NULL) the sub-vdbe is
		 * immediately halted by jumping to the OP_Halt
		 * inserted at the end of the program.
		 */
		if (trigger->pWhen != NULL) {
			pWhen = sqlExprDup(db, trigger->pWhen, 0);
			if (0 == sqlResolveExprNames(&sNC, pWhen)
			    && db->mallocFailed == 0) {
				iEndTrigger = sqlVdbeMakeLabel(v);
				sqlExprIfFalse(pSubParse, pWhen,
						   iEndTrigger,
						   SQL_JUMPIFNULL);
			}
			sql_expr_delete(db, pWhen, false);
		}

		/* Code the trigger program into the sub-vdbe. */
		codeTriggerProgram(pSubParse, trigger->step_list, orconf);

		/* Insert an OP_Halt at the end of the sub-program. */
		if (iEndTrigger)
			sqlVdbeResolveLabel(v, iEndTrigger);
		sqlVdbeAddOp0(v, OP_Halt);
		VdbeComment((v, "End: %s.%s", trigger->zName,
			     onErrorText(orconf)));

		if (!parser->is_aborted)
			parser->is_aborted = pSubParse->is_aborted;
		if (db->mallocFailed == 0) {
			pProgram->aOp =
			    sqlVdbeTakeOpArray(v, &pProgram->nOp,
						   &pTop->nMaxArg);
		}
		pProgram->nMem = pSubParse->nMem;
		pProgram->nCsr = pSubParse->nTab;
		pProgram->token = (void *)trigger;
		pPrg->column_mask[0] = pSubParse->oldmask;
		pPrg->column_mask[1] = pSubParse->newmask;
		sqlVdbeDelete(v);
	}

	assert(!pSubParse->pTriggerPrg && !pSubParse->nMaxArg);
	sql_parser_destroy(pSubParse);
	sqlStackFree(db, pSubParse);

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
		struct space *space, int orconf)
{
	Parse *pRoot = sqlParseToplevel(parser);
	TriggerPrg *pPrg;

	assert(trigger->zName == NULL || space->def->id == trigger->space_id);

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
		pPrg = sql_row_trigger_program(parser, trigger, space, orconf);

	return pPrg;
}

void
vdbe_code_row_trigger_direct(struct Parse *parser, struct sql_trigger *trigger,
			     struct space *space, int reg, int orconf,
			     int ignore_jump)
{
	/* Main VM. */
	struct Vdbe *v = sqlGetVdbe(parser);

	TriggerPrg *pPrg = sql_row_trigger(parser, trigger, space, orconf);
	assert(pPrg != NULL || parser->is_aborted ||
	       parser->db->mallocFailed != 0);

	/*
	 * Code the OP_Program opcode in the parent VDBE. P4 of
	 * the OP_Program is a pointer to the sub-vdbe containing
	 * the trigger program.
	 */
	if (pPrg == NULL)
		return;

	bool is_recursive =
		trigger->zName && (parser->sql_flags & SQL_RecTriggers) == 0;

	sqlVdbeAddOp4(v, OP_Program, reg, ignore_jump,
			  ++parser->nMem, (const char *)pPrg->pProgram,
			  P4_SUBPROGRAM);
	VdbeComment((v, "Call: %s.%s", (trigger->zName ? trigger->zName :
					"fk_constraint"),
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
	sqlVdbeChangeP5(v, (u8)is_recursive);
}

void
vdbe_code_row_trigger(struct Parse *parser, struct sql_trigger *trigger,
		      int op, struct ExprList *changes_list, int tr_tm,
		      struct space *space, int reg, int orconf, int ignore_jump)
{
	assert(op == TK_UPDATE || op == TK_INSERT || op == TK_DELETE);
	assert(tr_tm == TRIGGER_BEFORE || tr_tm == TRIGGER_AFTER);
	assert((op == TK_UPDATE) == (changes_list != NULL));

	for (struct sql_trigger *p = trigger; p != NULL; p = p->next) {
		/* Determine whether we should code trigger. */
		if (p->op == op && p->tr_tm == tr_tm &&
		    checkColumnOverlap(p->pColumns, changes_list)) {
			vdbe_code_row_trigger_direct(parser, p, space, reg,
						     orconf, ignore_jump);
		}
	}
}

uint64_t
sql_trigger_colmask(Parse *parser, struct sql_trigger *trigger,
		    ExprList *changes_list, int new, int tr_tm,
		    struct space *space, int orconf)
{
	const int op = changes_list != NULL ? TK_UPDATE : TK_DELETE;
	uint64_t mask = 0;

	assert(new == 1 || new == 0);
	for (struct sql_trigger *p = trigger; p != NULL; p = p->next) {
		if (p->op == op && (tr_tm & p->tr_tm)
		    && checkColumnOverlap(p->pColumns, changes_list)) {
			TriggerPrg *prg =
				sql_row_trigger(parser, p, space, orconf);
			if (prg != NULL)
				mask |= prg->column_mask[new];
		}
	}

	return mask;
}
