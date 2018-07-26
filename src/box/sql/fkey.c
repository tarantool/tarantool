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
 * This file contains code used by the compiler to add foreign key
 * support to compiled SQL statements.
 */
#include "coll.h"
#include "sqliteInt.h"
#include "box/fkey.h"
#include "box/schema.h"
#include "box/session.h"

/*
 * Deferred and Immediate FKs
 * --------------------------
 *
 * Foreign keys in SQLite come in two flavours: deferred and immediate.
 * If an immediate foreign key constraint is violated,
 * SQLITE_CONSTRAINT_FOREIGNKEY is returned and the current
 * statement transaction rolled back. If a
 * deferred foreign key constraint is violated, no action is taken
 * immediately. However if the application attempts to commit the
 * transaction before fixing the constraint violation, the attempt fails.
 *
 * Deferred constraints are implemented using a simple counter associated
 * with the database handle. The counter is set to zero each time a
 * database transaction is opened. Each time a statement is executed
 * that causes a foreign key violation, the counter is incremented. Each
 * time a statement is executed that removes an existing violation from
 * the database, the counter is decremented. When the transaction is
 * committed, the commit fails if the current value of the counter is
 * greater than zero. This scheme has two big drawbacks:
 *
 *   * When a commit fails due to a deferred foreign key constraint,
 *     there is no way to tell which foreign constraint is not satisfied,
 *     or which row it is not satisfied for.
 *
 *   * If the database contains foreign key violations when the
 *     transaction is opened, this may cause the mechanism to malfunction.
 *
 * Despite these problems, this approach is adopted as it seems simpler
 * than the alternatives.
 *
 * INSERT operations:
 *
 *   I.1) For each FK for which the table is the child table, search
 *        the parent table for a match. If none is found increment the
 *        constraint counter.
 *
 *   I.2) For each FK for which the table is the parent table,
 *        search the child table for rows that correspond to the new
 *        row in the parent table. Decrement the counter for each row
 *        found (as the constraint is now satisfied).
 *
 * DELETE operations:
 *
 *   D.1) For each FK for which the table is the child table,
 *        search the parent table for a row that corresponds to the
 *        deleted row in the child table. If such a row is not found,
 *        decrement the counter.
 *
 *   D.2) For each FK for which the table is the parent table, search
 *        the child table for rows that correspond to the deleted row
 *        in the parent table. For each found increment the counter.
 *
 * UPDATE operations:
 *
 *   An UPDATE command requires that all 4 steps above are taken, but only
 *   for FK constraints for which the affected columns are actually
 *   modified (values must be compared at runtime).
 *
 * Note that I.1 and D.1 are very similar operations, as are I.2 and D.2.
 * This simplifies the implementation a bit.
 *
 * For the purposes of immediate FK constraints, the OR REPLACE conflict
 * resolution is considered to delete rows before the new row is inserted.
 * If a delete caused by OR REPLACE violates an FK constraint, an exception
 * is thrown, even if the FK constraint would be satisfied after the new
 * row is inserted.
 *
 * Immediate constraints are usually handled similarly. The only difference
 * is that the counter used is stored as part of each individual statement
 * object (struct Vdbe). If, after the statement has run, its immediate
 * constraint counter is greater than zero,
 * it returns SQLITE_CONSTRAINT_FOREIGNKEY
 * and the statement transaction is rolled back. An exception is an INSERT
 * statement that inserts a single row only (no triggers). In this case,
 * instead of using a counter, an exception is thrown immediately if the
 * INSERT violates a foreign key constraint. This is necessary as such
 * an INSERT does not open a statement transaction.
 *
 * TODO: How should dropping a table be handled? How should renaming a
 * table be handled?
 *
 *
 * Query API Notes
 * ---------------
 *
 * Before coding an UPDATE or DELETE row operation, the code-generator
 * for those two operations needs to know whether or not the operation
 * requires any FK processing and, if so, which columns of the original
 * row are required by the FK processing VDBE code (i.e. if FKs were
 * implemented using triggers, which of the old.* columns would be
 * accessed). No information is required by the code-generator before
 * coding an INSERT operation. The functions used by the UPDATE/DELETE
 * generation code to query for this information are:
 *
 *   fkey_is_required() - Test to see if FK processing is required.
 *
 * Externally accessible module functions
 * --------------------------------------
 *
 *   fkey_emit_check()   - Check for foreign key violations.
 *   fkey_emit_actions()  - Code triggers for ON UPDATE/ON DELETE actions.
 *
 * VDBE Calling Convention
 * -----------------------
 *
 * Example:
 *
 *   For the following INSERT statement:
 *
 *     CREATE TABLE t1(a, b INTEGER PRIMARY KEY, c);
 *     INSERT INTO t1 VALUES(1, 2, 3.1);
 *
 *   Register (x):        2    (type integer)
 *   Register (x+1):      1    (type integer)
 *   Register (x+2):      NULL (type NULL)
 *   Register (x+3):      3.1  (type real)
 */

/**
 * This function is called when a row is inserted into or deleted
 * from the child table of foreign key constraint. If an SQL
 * UPDATE is executed on the child table of fkey, this function is
 * invoked twice for each row affected - once to "delete" the old
 * row, and then again to "insert" the new row.
 *
 * Each time it is called, this function generates VDBE code to
 * locate the row in the parent table that corresponds to the row
 * being inserted into or deleted from the child table. If the
 * parent row can be found, no special action is taken. Otherwise,
 * if the parent row can *not* be found in the parent table:
 *
 *   Op   | FK type  | Action taken
 * ---------------------------------------------------------------
 * INSERT  immediate Increment the "immediate constraint counter".
 *
 * DELETE  immediate Decrement the "immediate constraint counter".
 *
 * INSERT  deferred  Increment the "deferred constraint counter".
 *
 * DELETE  deferred  Decrement the "deferred constraint counter".
 *
 * These operations are identified in the comment at the top of
 * this file as "I.1" and "D.1".
 *
 * @param parse_context Current parsing context.
 * @param parent Parent table of FK constraint.
 * @param fk_def FK constraint definition.
 * @param referenced_idx Id of referenced index.
 * @param reg_data Address of array containing child table row.
 * @param incr_count Increment constraint counter by this value.
 */
static void
fkey_lookup_parent(struct Parse *parse_context, struct space *parent,
		   struct fkey_def *fk_def, uint32_t referenced_idx,
		   int reg_data, int incr_count)
{
	assert(incr_count == -1 || incr_count == 1);
	struct Vdbe *v = sqlite3GetVdbe(parse_context);
	int cursor = parse_context->nTab - 1;
	int ok_label = sqlite3VdbeMakeLabel(v);
	/*
	 * If incr_count is less than zero, then check at runtime
	 * if there are any outstanding constraints to resolve.
	 * If there are not, there is no need to check if deleting
	 * this row resolves any outstanding violations.
	 *
	 * Check if any of the key columns in the child table row
	 * are NULL. If any are, then the constraint is considered
	 * satisfied. No need to search for a matching row in the
	 * parent table.
	 */
	if (incr_count < 0) {
		sqlite3VdbeAddOp2(v, OP_FkIfZero, fk_def->is_deferred,
				  ok_label);
	}
	struct field_link *link = fk_def->links;
	for (uint32_t i = 0; i < fk_def->field_count; ++i, ++link) {
		int reg = link->child_field + reg_data + 1;
		sqlite3VdbeAddOp2(v, OP_IsNull, reg, ok_label);
	}
	uint32_t field_count = fk_def->field_count;
	int temp_regs = sqlite3GetTempRange(parse_context, field_count);
	int rec_reg = sqlite3GetTempReg(parse_context);
	vdbe_emit_open_cursor(parse_context, cursor, referenced_idx, parent);
	link = fk_def->links;
	for (uint32_t i = 0; i < field_count; ++i, ++link) {
		sqlite3VdbeAddOp2(v, OP_Copy, link->child_field + 1 + reg_data,
				  temp_regs + i);
	}
	/*
	 * If the parent table is the same as the child table, and
	 * we are about to increment the constraint-counter (i.e.
	 * this is an INSERT operation), then check if the row
	 * being inserted matches itself. If so, do not increment
	 * the constraint-counter.
	 *
	 * If any of the parent-key values are NULL, then the row
	 * cannot match itself. So set JUMPIFNULL to make sure we
	 * do the OP_Found if any of the parent-key values are
	 * NULL (at this point it is known that none of the child
	 * key values are).
	 */
	if (fkey_is_self_referenced(fk_def) && incr_count == 1) {
		int jump = sqlite3VdbeCurrentAddr(v) + field_count + 1;
		link = fk_def->links;
		for (uint32_t i = 0; i < field_count; ++i, ++link) {
			int chcol = link->child_field + 1 + reg_data;
			int pcol = link->parent_field + 1 + reg_data;
			sqlite3VdbeAddOp3(v, OP_Ne, chcol, jump, pcol);
			sqlite3VdbeChangeP5(v, SQLITE_JUMPIFNULL);
		}
		sqlite3VdbeGoto(v, ok_label);
	}
	struct index *idx = space_index(parent, referenced_idx);
	assert(idx != NULL);
	sqlite3VdbeAddOp4(v, OP_MakeRecord, temp_regs, field_count, rec_reg,
			  sql_space_index_affinity_str(parse_context->db,
						       parent->def, idx->def),
			  P4_DYNAMIC);
	sqlite3VdbeAddOp4Int(v, OP_Found, cursor, ok_label, rec_reg, 0);
	sqlite3ReleaseTempReg(parse_context, rec_reg);
	sqlite3ReleaseTempRange(parse_context, temp_regs, field_count);
	struct session *session = current_session();
	if (!fk_def->is_deferred &&
	    (session->sql_flags & SQLITE_DeferFKs) == 0 &&
	    parse_context->pToplevel == NULL && !parse_context->isMultiWrite) {
		/*
		 * If this is an INSERT statement that will insert
		 * exactly one row into the table, raise a
		 * constraint immediately instead of incrementing
		 * a counter. This is necessary as the VM code is
		 * being generated for will not open a statement
		 * transaction.
		 */
		assert(incr_count == 1);
		sqlite3HaltConstraint(parse_context,
				      SQLITE_CONSTRAINT_FOREIGNKEY,
				      ON_CONFLICT_ACTION_ABORT, 0, P4_STATIC,
				      P5_ConstraintFK);
	} else {
		if (incr_count > 0 && !fk_def->is_deferred)
			sqlite3MayAbort(parse_context);
		sqlite3VdbeAddOp2(v, OP_FkCounter, fk_def->is_deferred,
				  incr_count);
	}
	sqlite3VdbeResolveLabel(v, ok_label);
	sqlite3VdbeAddOp1(v, OP_Close, cursor);
}

/*
 * Return an Expr object that refers to a memory register corresponding
 * to column iCol of table pTab.
 *
 * regBase is the first of an array of register that contains the data
 * for pTab.  regBase+1 holds the first column.
 * regBase+2 holds the second column, and so forth.
 */
static Expr *
exprTableRegister(Parse * pParse,	/* Parsing and code generating context */
		  Table * pTab,	/* The table whose content is at r[regBase]... */
		  int regBase,	/* Contents of table pTab */
		  i16 iCol	/* Which column of pTab is desired */
    )
{
	Expr *pExpr;
	sqlite3 *db = pParse->db;

	pExpr = sqlite3Expr(db, TK_REGISTER, 0);
	if (pExpr) {
		if (iCol >= 0) {
			pExpr->iTable = regBase + iCol + 1;
			char affinity = pTab->def->fields[iCol].affinity;
			pExpr->affinity = affinity;
			pExpr = sqlite3ExprAddCollateString(pParse, pExpr,
							    "binary");
		} else {
			pExpr->iTable = regBase;
			pExpr->affinity = AFFINITY_INTEGER;
		}
	}
	return pExpr;
}

/**
 * Return an Expr object that refers to column of space_def which
 * has cursor cursor.
 * @param db The database connection.
 * @param def space definition.
 * @param cursor The open cursor on the table.
 * @param column The column that is wanted.
 * @retval not NULL on success.
 * @retval NULL on error.
 */
static Expr *
exprTableColumn(sqlite3 * db, struct space_def *def, int cursor, i16 column)
{
	Expr *pExpr = sqlite3Expr(db, TK_COLUMN, 0);
	if (pExpr) {
		pExpr->space_def = def;
		pExpr->iTable = cursor;
		pExpr->iColumn = column;
	}
	return pExpr;
}

/*
 * This function is called to generate code executed when a row is
 * deleted from the parent table of foreign key constraint @a fkey
 * and, if @a fkey is deferred, when a row is inserted into the
 * same table. When generating code for an SQL UPDATE operation,
 * this function may be called twice - once to "delete" the old
 * row and once to "insert" the new row.
 *
 * Parameter incr_count is passed -1 when inserting a row (as this
 * may decrease the number of FK violations in the db) or +1 when
 * deleting one (as this may increase the number of FK constraint
 * problems).
 *
 * The code generated by this function scans through the rows in
 * the child table that correspond to the parent table row being
 * deleted or inserted. For each child row found, one of the
 * following actions is taken:
 *
 *   Op  | FK type  | Action taken
 * ---------------------------------------------------------------
 * DELETE immediate  Increment the "immediate constraint counter".
 *                   Or, if the ON (UPDATE|DELETE) action is
 *                   RESTRICT, throw a "FOREIGN KEY constraint
 *                   failed" exception.
 *
 * INSERT immediate  Decrement the "immediate constraint counter".
 *
 * DELETE deferred   Increment the "deferred constraint counter".
 *                   Or, if the ON (UPDATE|DELETE) action is
 *                   RESTRICT, throw a "FOREIGN KEY constraint
 *                   failed" exception.
 *
 * INSERT deferred   Decrement the "deferred constraint counter".
 *
 * These operations are identified in the comment at the top of
 * this file as "I.2" and "D.2".
 * @param parser SQL parser.
 * @param src The child table to be scanned.
 * @param tab Parent table.
 * @param fkey The foreign key linking src to tab.
 * @param reg_data Register from which parent row data starts.
 * @param incr_count Amount to increment deferred counter by.
 */
static void
fkey_scan_children(struct Parse *parser, struct SrcList *src, struct Table *tab,
		   struct fkey_def *fkey, int reg_data, int incr_count)
{
	assert(incr_count == -1 || incr_count == 1);
	struct sqlite3 *db = parser->db;
	struct Expr *where = NULL;
	/* Address of OP_FkIfZero. */
	int fkifzero_label = 0;
	struct Vdbe *v = sqlite3GetVdbe(parser);

	if (incr_count < 0) {
		fkifzero_label = sqlite3VdbeAddOp2(v, OP_FkIfZero,
						   fkey->is_deferred, 0);
		VdbeCoverage(v);
	}

	struct space *child_space = space_by_id(fkey->child_id);
	assert(child_space != NULL);
	/*
	 * Create an Expr object representing an SQL expression
	 * like:
	 *
	 * <parent-key1> = <child-key1> AND <parent-key2> = <child-key2> ...
	 *
	 * The collation sequence used for the comparison should
	 * be that of the parent key columns. The affinity of the
	 * parent key column should be applied to each child key
	 * value before the comparison takes place.
	 */
	for (uint32_t i = 0; i < fkey->field_count; i++) {
		uint32_t fieldno = fkey->links[i].parent_field;
		struct Expr *pexpr =
			exprTableRegister(parser, tab, reg_data, fieldno);
		fieldno = fkey->links[i].child_field;
		const char *field_name = child_space->def->fields[fieldno].name;
		struct Expr *chexpr = sqlite3Expr(db, TK_ID, field_name);
		struct Expr *eq = sqlite3PExpr(parser, TK_EQ, pexpr, chexpr);
		where = sqlite3ExprAnd(db, where, eq);
	}

	/*
	 * If the child table is the same as the parent table,
	 * then add terms to the WHERE clause that prevent this
	 * entry from being scanned. The added WHERE clause terms
	 * are like this:
	 *
	 *     NOT( $current_a==a AND $current_b==b AND ... )
	 *     The primary key is (a,b,...)
	 */
	if (tab->def->id == fkey->child_id && incr_count > 0) {
		struct Expr *expr = NULL, *pexpr, *chexpr, *eq;
		for (uint32_t i = 0; i < fkey->field_count; i++) {
			uint32_t fieldno = fkey->links[i].parent_field;
			pexpr = exprTableRegister(parser, tab, reg_data,
						  fieldno);
			chexpr = exprTableColumn(db, tab->def,
						 src->a[0].iCursor, fieldno);
			eq = sqlite3PExpr(parser, TK_EQ, pexpr, chexpr);
			expr = sqlite3ExprAnd(db, expr, eq);
		}
		struct Expr *pNe = sqlite3PExpr(parser, TK_NOT, expr, 0);
		where = sqlite3ExprAnd(db, where, pNe);
	}

	/* Resolve the references in the WHERE clause. */
	struct NameContext namectx;
	memset(&namectx, 0, sizeof(namectx));
	namectx.pSrcList = src;
	namectx.pParse = parser;
	sqlite3ResolveExprNames(&namectx, where);

	/*
	 * Create VDBE to loop through the entries in src that
	 * match the WHERE clause. For each row found, increment
	 * either the deferred or immediate foreign key constraint
	 * counter.
	 */
	struct WhereInfo *info =
		sqlite3WhereBegin(parser, src, where, NULL, NULL, 0, 0);
	sqlite3VdbeAddOp2(v, OP_FkCounter, fkey->is_deferred, incr_count);
	if (info != NULL)
		sqlite3WhereEnd(info);

	/* Clean up the WHERE clause constructed above. */
	sql_expr_delete(db, where, false);
	if (fkifzero_label != 0)
		sqlite3VdbeJumpHere(v, fkifzero_label);
}

/**
 * Detect if @a fkey columns of @a type intersect with @a changes.
 * @param fkey FK constraint definition.
 * @param changes Array indicating modified columns.
 *
 * @retval true, if any of the columns that are part of the key
 *         or @a type for FK constraint are modified.
 */
static bool
fkey_is_modified(const struct fkey_def *fkey, int type, const int *changes)
{
	for (uint32_t i = 0; i < fkey->field_count; ++i) {
		if (changes[fkey->links[i].fields[type]] >= 0)
			return true;
	}
	return false;
}

/**
 * Return true if the parser passed as the first argument is
 * used to code a trigger that is really a "SET NULL" action.
 */
static bool
fkey_action_is_set_null(struct Parse *parse_context, const struct fkey *fkey)
{
	struct Parse *top_parse = sqlite3ParseToplevel(parse_context);
	if (top_parse->pTriggerPrg != NULL) {
		struct sql_trigger *trigger = top_parse->pTriggerPrg->trigger;
		if ((trigger == fkey->on_delete_trigger &&
		     fkey->def->on_delete == FKEY_ACTION_SET_NULL) ||
		    (trigger == fkey->on_update_trigger &&
		     fkey->def->on_update == FKEY_ACTION_SET_NULL))
			return true;
	}
	return false;
}

void
fkey_emit_check(struct Parse *parser, struct Table *tab, int reg_old,
		int reg_new, const int *changed_cols)
{
	struct sqlite3 *db = parser->db;
	struct session *user_session = current_session();

	/*
	 * Exactly one of reg_old and reg_new should be non-zero.
	 */
	assert((reg_old == 0) != (reg_new == 0));

	/*
	 * If foreign-keys are disabled, this function is a no-op.
	 */
	if ((user_session->sql_flags & SQLITE_ForeignKeys) == 0)
		return;

	/*
	 * Loop through all the foreign key constraints for which
	 * tab is the child table.
	 */
	struct space *space = space_by_id(tab->def->id);
	assert(space != NULL);
	struct fkey *fk;
	rlist_foreach_entry(fk, &space->child_fkey, child_link) {
		struct fkey_def *fk_def = fk->def;
		if (changed_cols != NULL && !fkey_is_self_referenced(fk_def) &&
		    !fkey_is_modified(fk_def, FIELD_LINK_CHILD, changed_cols))
			continue;
		parser->nTab++;
		struct space *parent = space_by_id(fk_def->parent_id);
		assert(parent != NULL);
		if (reg_old != 0) {
			/*
			 * A row is being removed from the child
			 * table. Search for the parent. If the
			 * parent does not exist, removing the
			 * child row resolves an outstanding
			 * foreign key constraint violation.
			 */
			fkey_lookup_parent(parser, parent, fk_def, fk->index_id,
					   reg_old, -1);
		}
		if (reg_new != 0 && !fkey_action_is_set_null(parser, fk)) {
			/*
			 * A row is being added to the child
			 * table. If a parent row cannot be found,
			 * adding the child row has violated the
			 * FK constraint.
			 *
			 * If this operation is being performed as
			 * part of a trigger program that is
			 * actually a "SET NULL" action belonging
			 * to this very foreign key, then omit
			 * this scan altogether. As all child key
			 * values are guaranteed to be NULL, it is
			 * not possible for adding this row to
			 * cause an FK violation.
			 */
			fkey_lookup_parent(parser, parent, fk_def, fk->index_id,
					   reg_new, +1);
		}
	}
	/*
	 * Loop through all the foreign key constraints that
	 * refer to this table.
	 */
	rlist_foreach_entry(fk, &space->parent_fkey, parent_link) {
		struct fkey_def *fk_def = fk->def;
		if (changed_cols != NULL &&
		    !fkey_is_modified(fk_def, FIELD_LINK_PARENT, changed_cols))
			continue;
		if (!fk_def->is_deferred &&
		    (user_session->sql_flags & SQLITE_DeferFKs) == 0 &&
		    parser->pToplevel == NULL && !parser->isMultiWrite) {
			assert(reg_old == 0 && reg_new != 0);
			/*
			 * Inserting a single row into a parent
			 * table cannot cause (or fix) an
			 * immediate foreign key violation. So do
			 * nothing in this case.
			 */
			continue;
		}

		/*
		 * Create a SrcList structure containing the child
		 * table. We need the child table as a SrcList for
		 * sqlite3WhereBegin().
		 */
		struct SrcList *src = sqlite3SrcListAppend(db, NULL, NULL);
		if (src == NULL)
			continue;
		struct SrcList_item *item = src->a;
		struct space *child = space_by_id(fk->def->child_id);
		assert(child != NULL);
		struct Table *child_tab = sqlite3HashFind(&db->pSchema->tblHash,
							  child->def->name);
		item->pTab = child_tab;
		item->zName = sqlite3DbStrDup(db, child->def->name);
		item->pTab->nTabRef++;
		item->iCursor = parser->nTab++;

		if (reg_new != 0) {
			fkey_scan_children(parser, src, tab, fk->def, reg_new,
					   -1);
		}
		if (reg_old != 0) {
			enum fkey_action action = fk_def->on_update;
			fkey_scan_children(parser, src, tab, fk->def, reg_old,
					   1);
			/*
			 * If this is a deferred FK constraint, or
			 * a CASCADE or SET NULL action applies,
			 * then any foreign key violations caused
			 * by removing the parent key will be
			 * rectified by the action trigger. So do
			 * not set the "may-abort" flag in this
			 * case.
			 *
			 * Note 1: If the FK is declared "ON
			 * UPDATE CASCADE", then the may-abort
			 * flag will eventually be set on this
			 * statement anyway (when this function is
			 * called as part of processing the UPDATE
			 * within the action trigger).
			 *
			 * Note 2: At first glance it may seem
			 * like SQLite could simply omit all
			 * OP_FkCounter related scans when either
			 * CASCADE or SET NULL applies. The
			 * trouble starts if the CASCADE or SET
			 * NULL action trigger causes other
			 * triggers or action rules attached to
			 * the child table to fire. In these cases
			 * the fk constraint counters might be set
			 * incorrectly if any OP_FkCounter related
			 * scans are omitted.
			 */
			if (!fk_def->is_deferred &&
			    action != FKEY_ACTION_CASCADE &&
			    action != FKEY_ACTION_SET_NULL)
				sqlite3MayAbort(parser);
		}
		sqlite3SrcListDelete(db, src);
	}
}

bool
fkey_is_required(uint32_t space_id, const int *changes)
{
	struct session *user_session = current_session();
	if ((user_session->sql_flags & SQLITE_ForeignKeys) == 0)
		return false;
	struct space *space = space_by_id(space_id);
	if (changes == NULL) {
		/*
		 * A DELETE operation. FK processing is required
		 * if space is child or parent.
		 */
		return ! rlist_empty(&space->parent_fkey) ||
		       ! rlist_empty(&space->child_fkey);
	}
	/*
	 * This is an UPDATE. FK processing is only required if
	 * the operation modifies one or more child or parent key
	 * columns.
	 */
	struct fkey *fk;
	rlist_foreach_entry(fk, &space->child_fkey, child_link) {
		if (fkey_is_modified(fk->def, FIELD_LINK_CHILD, changes))
			return true;
	}
	rlist_foreach_entry(fk, &space->parent_fkey, parent_link) {
		if (fkey_is_modified(fk->def, FIELD_LINK_PARENT, changes))
			return true;
	}
	return false;
}

/**
 * This function is called when an UPDATE or DELETE operation is
 * being compiled on table pTab, which is the parent table of
 * foreign-key fkey.
 * If the current operation is an UPDATE, then the pChanges
 * parameter is passed a pointer to the list of columns being
 * modified. If it is a DELETE, pChanges is passed a NULL pointer.
 *
 * It returns a pointer to a sql_trigger structure containing a
 * trigger equivalent to the ON UPDATE or ON DELETE action
 * specified by fkey.
 * If the action is "NO ACTION" or "RESTRICT", then a NULL pointer
 * is returned (these actions require no special handling by the
 * triggers sub-system, code for them is created by
 * fkey_scan_children()).
 *
 * For example, if fkey is the foreign key and pTab is table "p"
 * in the following schema:
 *
 *   CREATE TABLE p(pk PRIMARY KEY);
 *   CREATE TABLE c(ck REFERENCES p ON DELETE CASCADE);
 *
 * then the returned trigger structure is equivalent to:
 *
 *   CREATE TRIGGER ... DELETE ON p BEGIN
 *     DELETE FROM c WHERE ck = old.pk;
 *   END;
 *
 * The returned pointer is cached as part of the foreign key
 * object. It is eventually freed along with the rest of the
 * foreign key object by fkey_delete().
 *
 * @param pParse Parse context.
 * @param pTab Table being updated or deleted from.
 * @param fkey Foreign key to get action for.
 * @param is_update True if action is on update.
 *
 * @retval not NULL on success.
 * @retval NULL on failure.
 */
static struct sql_trigger *
fkey_action_trigger(struct Parse *pParse, struct Table *pTab, struct fkey *fkey,
		    bool is_update)
{
	struct sqlite3 *db = pParse->db;
	struct fkey_def *fk_def = fkey->def;
	enum fkey_action action = is_update ? fk_def->on_update :
					      fk_def->on_delete;
	struct sql_trigger *trigger = is_update ? fkey->on_update_trigger :
						  fkey->on_delete_trigger;
	if (action == FKEY_NO_ACTION || trigger != NULL)
		return trigger;
	struct TriggerStep *step = NULL;
	struct Expr *where = NULL, *when = NULL;
	struct ExprList *list = NULL;
	struct Select *select = NULL;
	struct space *child_space = space_by_id(fk_def->child_id);
	assert(child_space != NULL);
	for (uint32_t i = 0; i < fk_def->field_count; ++i) {
		/* Literal "old" token. */
		struct Token t_old = { "old", 3, false };
		/* Literal "new" token. */
		struct Token t_new = { "new", 3, false };
		/* Name of column in child table. */
		struct Token t_from_col;
		/* Name of column in parent table. */
		struct Token t_to_col;
		struct field_def *child_fields = child_space->def->fields;

		uint32_t pcol = fk_def->links[i].parent_field;
		sqlite3TokenInit(&t_to_col, pTab->def->fields[pcol].name);

		uint32_t chcol = fk_def->links[i].child_field;
		sqlite3TokenInit(&t_from_col, child_fields[chcol].name);

		/*
		 * Create the expression "old.to_col = from_col".
		 * It is important that the "old.to_col" term is
		 * on the LHS of the = operator, so that the
		 * affinity and collation sequence associated with
		 * the parent table are used for the comparison.
		 */
		struct Expr *to_col =
			sqlite3PExpr(pParse, TK_DOT,
				     sqlite3ExprAlloc(db, TK_ID, &t_old, 0),
				     sqlite3ExprAlloc(db, TK_ID, &t_to_col, 0));
		struct Expr *from_col =
			sqlite3ExprAlloc(db, TK_ID, &t_from_col, 0);
		struct Expr *eq = sqlite3PExpr(pParse, TK_EQ, to_col, from_col);
		where = sqlite3ExprAnd(db, where, eq);

		/*
		 * For ON UPDATE, construct the next term of the
		 * WHEN clause. The final WHEN clause will be like
		 * this:
		 *
		 *    WHEN NOT(old.col1 = new.col1 AND ... AND
		 *             old.colN = new.colN)
		 */
		if (is_update) {
			struct Expr *l, *r;
			l = sqlite3PExpr(pParse, TK_DOT,
					 sqlite3ExprAlloc(db, TK_ID, &t_old, 0),
					 sqlite3ExprAlloc(db, TK_ID, &t_to_col,
							  0));
			r = sqlite3PExpr(pParse, TK_DOT,
					 sqlite3ExprAlloc(db, TK_ID, &t_new, 0),
					 sqlite3ExprAlloc(db, TK_ID, &t_to_col,
							  0));
			eq = sqlite3PExpr(pParse, TK_EQ, l, r);
			when = sqlite3ExprAnd(db, when, eq);
		}

		if (action != FKEY_ACTION_RESTRICT &&
		    (action != FKEY_ACTION_CASCADE || is_update)) {
			struct Expr *new, *d;
			if (action == FKEY_ACTION_CASCADE) {
				new = sqlite3PExpr(pParse, TK_DOT,
						   sqlite3ExprAlloc(db, TK_ID,
								    &t_new, 0),
						   sqlite3ExprAlloc(db, TK_ID,
								    &t_to_col,
								    0));
			} else if (action == FKEY_ACTION_SET_DEFAULT) {
				d = child_fields[chcol].default_value_expr;
				if (d != NULL) {
					new = sqlite3ExprDup(db, d, 0);
				} else {
					new = sqlite3ExprAlloc(db, TK_NULL,
							       NULL, 0);
				}
			} else {
				new = sqlite3ExprAlloc(db, TK_NULL, NULL, 0);
			}
			list = sql_expr_list_append(db, list, new);
			sqlite3ExprListSetName(pParse, list, &t_from_col, 0);
		}
	}

	const char *space_name = child_space->def->name;
	uint32_t name_len = strlen(space_name);

	if (action == FKEY_ACTION_RESTRICT) {
		struct Token err;
		err.z = space_name;
		err.n = name_len;
		struct Expr *r = sqlite3Expr(db, TK_RAISE, "FOREIGN KEY "\
					     "constraint failed");
		if (r != NULL)
			r->affinity = ON_CONFLICT_ACTION_ABORT;
		select = sqlite3SelectNew(pParse,
					  sql_expr_list_append(db, NULL, r),
					  sqlite3SrcListAppend(db, NULL, &err),
					  where, NULL, NULL, NULL, 0, NULL,
					  NULL);
		where = NULL;
	}

	trigger = (struct sql_trigger *) sqlite3DbMallocZero(db,
							     sizeof(*trigger));
	if (trigger != NULL) {
		size_t step_size = sizeof(TriggerStep) + name_len + 1;
		trigger->step_list = sqlite3DbMallocZero(db, step_size);
		step = trigger->step_list;
		step->zTarget = (char *) &step[1];
		memcpy((char *) step->zTarget, space_name, name_len);

		step->pWhere = sqlite3ExprDup(db, where, EXPRDUP_REDUCE);
		step->pExprList = sql_expr_list_dup(db, list, EXPRDUP_REDUCE);
		step->pSelect = sqlite3SelectDup(db, select, EXPRDUP_REDUCE);
		if (when != NULL) {
			when = sqlite3PExpr(pParse, TK_NOT, when, 0);
			trigger->pWhen =
				sqlite3ExprDup(db, when, EXPRDUP_REDUCE);
		}
	}

	sql_expr_delete(db, where, false);
	sql_expr_delete(db, when, false);
	sql_expr_list_delete(db, list);
	sql_select_delete(db, select);
	if (db->mallocFailed) {
		sql_trigger_delete(db, trigger);
		return NULL;
	}
	assert(step != NULL);

	switch (action) {
	case FKEY_ACTION_RESTRICT:
		step->op = TK_SELECT;
		break;
	case FKEY_ACTION_CASCADE:
		if (! is_update) {
			step->op = TK_DELETE;
			break;
		}
		FALLTHROUGH;
	default:
		step->op = TK_UPDATE;
	}

	step->trigger = trigger;
	if (is_update) {
		fkey->on_update_trigger = trigger;
		trigger->op = TK_UPDATE;
	} else {
		fkey->on_delete_trigger = trigger;
		trigger->op = TK_DELETE;
	}
	return trigger;
}

void
fkey_emit_actions(struct Parse *parser, struct Table *tab, int reg_old,
		  const int *changes)
{
	struct session *user_session = current_session();
	/*
	 * If foreign-key support is enabled, iterate through all
	 * FKs that refer to table tab. If there is an action
	 * associated with the FK for this operation (either
	 * update or delete), invoke the associated trigger
	 * sub-program.
	 */
	if ((user_session->sql_flags & SQLITE_ForeignKeys) == 0)
		return;
	struct space *space = space_by_id(tab->def->id);
	assert(space != NULL);
	struct fkey *fk;
	rlist_foreach_entry(fk, &space->parent_fkey, parent_link)  {
		if (changes != NULL &&
		    !fkey_is_modified(fk->def, FIELD_LINK_PARENT, changes))
			continue;
		struct sql_trigger *pAct =
			fkey_action_trigger(parser, tab, fk, changes != NULL);
		if (pAct == NULL)
			continue;
		vdbe_code_row_trigger_direct(parser, pAct, tab, reg_old,
					     ON_CONFLICT_ACTION_ABORT, 0);
	}
}
