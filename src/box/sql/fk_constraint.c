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
#include "coll/coll.h"
#include "sqlInt.h"
#include "box/fk_constraint.h"
#include "box/schema.h"

/*
 * Deferred and Immediate FKs
 * --------------------------
 *
 * Foreign keys in sql come in two flavours: deferred and immediate.
 * If an immediate foreign key constraint is violated,
 * -1 is returned and the current
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
 * it returns -1.
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
 *   fk_constraint_is_required() - Test to see if FK processing is required.
 *
 * Externally accessible module functions
 * --------------------------------------
 *
 *   fk_constraint_emit_check()   - Check for foreign key violations.
 *   fk_constraint_emit_actions()  - Code triggers for ON UPDATE/ON DELETE actions.
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
 * UPDATE is executed on the child table of fk_constraint, this function is
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
fk_constraint_lookup_parent(struct Parse *parse_context, struct space *parent,
		   struct fk_constraint_def *fk_def, uint32_t referenced_idx,
		   int reg_data, int incr_count, bool is_update)
{
	assert(incr_count == -1 || incr_count == 1);
	struct Vdbe *v = sqlGetVdbe(parse_context);
	int cursor = parse_context->nTab - 1;
	int ok_label = sqlVdbeMakeLabel(v);
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
		sqlVdbeAddOp2(v, OP_FkIfZero, fk_def->is_deferred,
				  ok_label);
	}
	struct field_link *link = fk_def->links;
	for (uint32_t i = 0; i < fk_def->field_count; ++i, ++link) {
		int reg = link->child_field + reg_data + 1;
		sqlVdbeAddOp2(v, OP_IsNull, reg, ok_label);
	}
	uint32_t field_count = fk_def->field_count;
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
	if (fk_constraint_is_self_referenced(fk_def) && incr_count == 1) {
		int jump = sqlVdbeCurrentAddr(v) + field_count + 1;
		link = fk_def->links;
		for (uint32_t i = 0; i < field_count; ++i, ++link) {
			int chcol = link->child_field + 1 + reg_data;
			int pcol = link->parent_field + 1 + reg_data;
			sqlVdbeAddOp3(v, OP_Ne, chcol, jump, pcol);
			sqlVdbeChangeP5(v, SQL_JUMPIFNULL);
		}
		sqlVdbeGoto(v, ok_label);
	}
	/**
	 * Inspect a parent table with OP_Found.
	 * We mustn't make it for a self-referenced table since
	 * it's tuple will be modified by the update operation.
	 * And since the foreign key has already detected a
	 * conflict, fk counter must be increased.
	 */
	if (!(fk_constraint_is_self_referenced(fk_def) && is_update)) {
		int temp_regs = sqlGetTempRange(parse_context, field_count);
		int rec_reg = sqlGetTempReg(parse_context);
		vdbe_emit_open_cursor(parse_context, cursor, referenced_idx,
				      parent);
		link = fk_def->links;
		for (uint32_t i = 0; i < field_count; ++i, ++link) {
			sqlVdbeAddOp2(v, OP_Copy,
					  link->child_field + 1 + reg_data,
					  temp_regs + i);
		}
		struct index *idx = space_index(parent, referenced_idx);
		assert(idx != NULL);
		sqlVdbeAddOp4(v, OP_MakeRecord, temp_regs, field_count,
				  rec_reg,
				  (char *) sql_index_type_str(parse_context->db,
							      idx->def),
				  P4_DYNAMIC);
		sqlVdbeAddOp4Int(v, OP_Found, cursor, ok_label, rec_reg, 0);
		sqlReleaseTempReg(parse_context, rec_reg);
		sqlReleaseTempRange(parse_context, temp_regs, field_count);
	}
	if (!fk_def->is_deferred &&
	    (parse_context->sql_flags & SQL_DeferFKs) == 0 &&
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
		const char *err = tt_sprintf(tnt_errcode_desc(ER_SQL_EXECUTE),
					     "FOREIGN KEY constraint failed");
		sqlVdbeAddOp4(v, OP_SetDiag, ER_SQL_EXECUTE, 0, 0, err,
			      P4_STATIC);
		sqlVdbeAddOp1(v, OP_Halt, -1);
	} else {
		sqlVdbeAddOp2(v, OP_FkCounter, fk_def->is_deferred,
				  incr_count);
	}
	sqlVdbeResolveLabel(v, ok_label);
	sqlVdbeAddOp1(v, OP_Close, cursor);
}

/**
 * Build an expression that refers to a memory register
 * corresponding to @a column of given space.
 *
 * @param db SQL context.
 * @param def Definition of space whose content starts from
 *        @a reg_base register.
 * @param reg_base Index of a first element in an array of
 *        registers, containing data of a space. Register
 *        reg_base + i holds an i-th column, i >= 1.
 * @param column Index of a first table column to point at.
 * @retval Not NULL Success. An expression representing register.
 * @retval NULL Error. A diag message is set.
 */
static struct Expr *
sql_expr_new_register(struct sql *db, struct space_def *def, int reg_base,
		      uint32_t column)
{
	struct Expr *expr = sql_expr_new_anon(db, TK_REGISTER);
	if (expr == NULL)
		return NULL;
	expr->iTable = reg_base + column + 1;
	expr->type = def->fields[column].type;
	return expr;
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
static struct Expr *
sql_expr_new_column_by_cursor(struct sql *db, struct space_def *def,
			      int cursor, int column)
{
	struct Expr *expr = sql_expr_new_anon(db, TK_COLUMN_REF);
	if (expr == NULL)
		return NULL;
	expr->space_def = def;
	expr->iTable = cursor;
	expr->iColumn = column;
	return expr;
}

/*
 * This function is called to generate code executed when a row is
 * deleted from the parent table of foreign key constraint @a fk_constraint
 * and, if @a fk_constraint is deferred, when a row is inserted into the
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
 * @param def Parent space definition.
 * @param fk_constraint The foreign key linking src to tab.
 * @param reg_data Register from which parent row data starts.
 * @param incr_count Amount to increment deferred counter by.
 */
static void
fk_constraint_scan_children(struct Parse *parser, struct SrcList *src,
		   struct space_def *def, struct fk_constraint_def *fk_def,
		   int reg_data, int incr_count)
{
	assert(incr_count == -1 || incr_count == 1);
	struct sql *db = parser->db;
	struct Expr *where = NULL;
	/* Address of OP_FkIfZero. */
	int fkifzero_label = 0;
	struct Vdbe *v = sqlGetVdbe(parser);

	if (incr_count < 0) {
		fkifzero_label = sqlVdbeAddOp2(v, OP_FkIfZero,
						   fk_def->is_deferred, 0);
		VdbeCoverage(v);
	}

	struct space *child_space = src->a[0].space;
	assert(child_space != NULL);
	/*
	 * Create an Expr object representing an SQL expression
	 * like:
	 *
	 * <parent-key1> = <child-key1> AND <parent-key2> = <child-key2> ...
	 *
	 * The collation sequence used for the comparison should
	 * be that of the parent key columns. The type of the
	 * parent key column should be applied to each child key
	 * value before the comparison takes place.
	 */
	for (uint32_t i = 0; i < fk_def->field_count; i++) {
		uint32_t fieldno = fk_def->links[i].parent_field;
		struct Expr *pexpr =
			sql_expr_new_register(db, def, reg_data, fieldno);
		fieldno = fk_def->links[i].child_field;
		const char *field_name = child_space->def->fields[fieldno].name;
		struct Expr *chexpr = sql_expr_new_named(db, TK_ID, field_name);
		struct Expr *eq = sqlPExpr(parser, TK_EQ, pexpr, chexpr);
		where = sql_and_expr_new(db, where, eq);
		if (where == NULL || chexpr == NULL || pexpr == NULL)
			parser->is_aborted = true;
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
	if (def->id == fk_def->child_id && incr_count > 0) {
		struct Expr *expr = NULL, *pexpr, *chexpr, *eq;
		for (uint32_t i = 0; i < fk_def->field_count; i++) {
			uint32_t fieldno = fk_def->links[i].parent_field;
			pexpr = sql_expr_new_register(db, def, reg_data,
						      fieldno);
			int cursor = src->a[0].iCursor;
			chexpr = sql_expr_new_column_by_cursor(db, def, cursor,
							       fieldno);
			eq = sqlPExpr(parser, TK_EQ, pexpr, chexpr);
			expr = sql_and_expr_new(db, expr, eq);
			if (expr == NULL || chexpr == NULL || pexpr == NULL)
				parser->is_aborted = true;
		}
		struct Expr *pNe = sqlPExpr(parser, TK_NOT, expr, 0);
		where = sql_and_expr_new(db, where, pNe);
		if (where == NULL)
			parser->is_aborted = true;
	}

	/* Resolve the references in the WHERE clause. */
	struct NameContext namectx;
	memset(&namectx, 0, sizeof(namectx));
	namectx.pSrcList = src;
	namectx.pParse = parser;
	sqlResolveExprNames(&namectx, where);

	/*
	 * Create VDBE to loop through the entries in src that
	 * match the WHERE clause. For each row found, increment
	 * either the deferred or immediate foreign key constraint
	 * counter.
	 */
	struct WhereInfo *info =
		sqlWhereBegin(parser, src, where, NULL, NULL, 0, 0);
	sqlVdbeAddOp2(v, OP_FkCounter, fk_def->is_deferred, incr_count);
	if (info != NULL)
		sqlWhereEnd(info);

	/* Clean up the WHERE clause constructed above. */
	sql_expr_delete(db, where, false);
	if (fkifzero_label != 0)
		sqlVdbeJumpHere(v, fkifzero_label);
}

/**
 * Detect if @a fk_constraint columns of @a type intersect with @a changes.
 * @param fk_constraint FK constraint definition.
 * @param changes Array indicating modified columns.
 *
 * @retval true, if any of the columns that are part of the key
 *         or @a type for FK constraint are modified.
 */
static bool
fk_constraint_is_modified(const struct fk_constraint_def *fk_def, int type,
			  const int *changes)
{
	for (uint32_t i = 0; i < fk_def->field_count; ++i) {
		if (changes[fk_def->links[i].fields[type]] >= 0)
			return true;
	}
	return false;
}

/**
 * Return true if the parser passed as the first argument is
 * used to code a trigger that is really a "SET NULL" action.
 */
static bool
fk_constraint_action_is_set_null(struct Parse *parse_context,
				 const struct fk_constraint *fk)
{
	struct Parse *top_parse = sqlParseToplevel(parse_context);
	if (top_parse->pTriggerPrg != NULL) {
		struct sql_trigger *trigger = top_parse->pTriggerPrg->trigger;
		if ((trigger == fk->on_delete_trigger &&
		     fk->def->on_delete == FKEY_ACTION_SET_NULL) ||
		    (trigger == fk->on_update_trigger &&
		     fk->def->on_update == FKEY_ACTION_SET_NULL))
			return true;
	}
	return false;
}

void
fk_constraint_emit_check(struct Parse *parser, struct space *space, int reg_old,
			 int reg_new, const int *changed_cols)
{
	bool is_update = changed_cols != NULL;
	struct sql *db = parser->db;

	/*
	 * Exactly one of reg_old and reg_new should be non-zero.
	 */
	assert((reg_old == 0) != (reg_new == 0));

	/*
	 * Loop through all the foreign key constraints for which
	 * tab is the child table.
	 */
	assert(space != NULL);
	struct fk_constraint *fk;
	rlist_foreach_entry(fk, &space->child_fk_constraint, in_child_space) {
		struct fk_constraint_def *fk_def = fk->def;
		if (is_update && !fk_constraint_is_self_referenced(fk_def) &&
		    !fk_constraint_is_modified(fk_def, FIELD_LINK_CHILD, changed_cols))
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
			fk_constraint_lookup_parent(parser, parent, fk_def, fk->index_id,
						    reg_old, -1, is_update);
		}
		if (reg_new != 0 && !fk_constraint_action_is_set_null(parser, fk)) {
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
			fk_constraint_lookup_parent(parser, parent,
						    fk_def, fk->index_id,
						    reg_new, +1, is_update);
		}
	}
	/*
	 * Loop through all the foreign key constraints that
	 * refer to this table.
	 */
	rlist_foreach_entry(fk, &space->parent_fk_constraint, in_parent_space) {
		struct fk_constraint_def *fk_def = fk->def;
		if (is_update &&
		    !fk_constraint_is_modified(fk_def, FIELD_LINK_PARENT,
					       changed_cols))
			continue;
		if (!fk_def->is_deferred &&
		    (parser->sql_flags & SQL_DeferFKs) == 0 &&
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
		 * sqlWhereBegin().
		 */
		struct SrcList *src = sql_src_list_append(db, NULL, NULL);
		if (src == NULL) {
			parser->is_aborted = true;
			return;
		}
		struct SrcList_item *item = src->a;
		struct space *child = space_by_id(fk->def->child_id);
		assert(child != NULL);
		item->space = child;
		item->zName = sqlDbStrDup(db, child->def->name);
		item->iCursor = parser->nTab++;

		if (reg_new != 0) {
			fk_constraint_scan_children(parser, src, space->def,
						    fk->def, reg_new, -1);
		}
		if (reg_old != 0) {
			fk_constraint_scan_children(parser, src, space->def,
						    fk->def, reg_old, 1);
		}
		sqlSrcListDelete(db, src);
	}
}

bool
fk_constraint_is_required(struct space *space, const int *changes)
{
	if (changes == NULL) {
		/*
		 * A DELETE operation. FK processing is required
		 * if space is child or parent.
		 */
		return ! rlist_empty(&space->parent_fk_constraint) ||
		       ! rlist_empty(&space->child_fk_constraint);
	}
	/*
	 * This is an UPDATE. FK processing is only required if
	 * the operation modifies one or more child or parent key
	 * columns.
	 */
	struct fk_constraint *fk;
	rlist_foreach_entry(fk, &space->child_fk_constraint, in_child_space) {
		if (fk_constraint_is_modified(fk->def, FIELD_LINK_CHILD, changes))
			return true;
	}
	rlist_foreach_entry(fk, &space->parent_fk_constraint, in_parent_space) {
		if (fk_constraint_is_modified(fk->def, FIELD_LINK_PARENT, changes))
			return true;
	}
	return false;
}

/**
 * Create a new expression representing two-part path
 * '<main>.<sub>'.
 * @param parser SQL Parser.
 * @param main First and main part of a result path. For example,
 *        a table name.
 * @param sub Second and last part of a result path. For example,
 *        a column name.
 * @retval Not NULL Success. An expression with two-part path.
 * @retval NULL Error. A diag message is set.
 */
static inline struct Expr *
sql_expr_new_2part_id(struct Parse *parser, const struct Token *main,
		      const struct Token *sub)
{
	struct Expr *emain = sql_expr_new(parser->db, TK_ID, main);
	struct Expr *esub = sql_expr_new(parser->db, TK_ID, sub);
	if (emain == NULL || esub == NULL)
		parser->is_aborted = true;
	return sqlPExpr(parser, TK_DOT, emain, esub);
}

/**
 * This function is called when an UPDATE or DELETE operation is
 * being compiled on table pTab, which is the parent table of
 * foreign-key fk_constraint.
 * If the current operation is an UPDATE, then the pChanges
 * parameter is passed a pointer to the list of columns being
 * modified. If it is a DELETE, pChanges is passed a NULL pointer.
 *
 * It returns a pointer to a sql_trigger structure containing a
 * trigger equivalent to the ON UPDATE or ON DELETE action
 * specified by fk_constraint.
 * If the action is "NO ACTION" or "RESTRICT", then a NULL pointer
 * is returned (these actions require no special handling by the
 * triggers sub-system, code for them is created by
 * fk_constraint_scan_children()).
 *
 * For example, if fk_constraint is the foreign key and pTab is table "p"
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
 * foreign key object by fk_constraint_delete().
 *
 * @param pParse Parse context.
 * @param def Definition of space being updated or deleted from.
 * @param fk_constraint Foreign key to get action for.
 * @param is_update True if action is on update.
 *
 * @retval not NULL on success.
 * @retval NULL on failure.
 */
static struct sql_trigger *
fk_constraint_action_trigger(struct Parse *pParse, struct space_def *def,
		    struct fk_constraint *fk, bool is_update)
{
	struct sql *db = pParse->db;
	struct fk_constraint_def *fk_def = fk->def;
	enum fk_constraint_action action = (is_update ? fk_def->on_update :
					    fk_def->on_delete);
	struct sql_trigger *trigger = is_update ? fk->on_update_trigger :
						  fk->on_delete_trigger;
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
		sqlTokenInit(&t_to_col, def->fields[pcol].name);

		uint32_t chcol = fk_def->links[i].child_field;
		sqlTokenInit(&t_from_col, child_fields[chcol].name);

		/*
		 * Create the expression "old.to_col = from_col".
		 * It is important that the "old.to_col" term is
		 * on the LHS of the = operator, so that the
		 * type and collation sequence associated with
		 * the parent table are used for the comparison.
		 */
		struct Expr *new, *old =
			sql_expr_new_2part_id(pParse, &t_old, &t_to_col);
		struct Expr *from = sql_expr_new(db, TK_ID, &t_from_col);
		struct Expr *eq = sqlPExpr(pParse, TK_EQ, old, from);
		where = sql_and_expr_new(db, where, eq);
		if (where == NULL || from == NULL)
			pParse->is_aborted = true;
		/*
		 * For ON UPDATE, construct the next term of the
		 * WHEN clause, which should return false in case
		 * there is a reason to for a broken constrant in
		 * a parent table:
		 *     no_action_needed := `oldval` IS NULL OR
		 *         (`newval` IS NOT NULL AND
		 *             `newval` = `oldval`)
		 *
		 * The final WHEN clause will be like
		 * this:
		 *
		 *    WHEN NOT( no_action_needed(col1) AND ...
		 *        no_action_needed(colN))
		 */
		if (is_update) {
			old = sql_expr_new_2part_id(pParse, &t_old, &t_to_col);
			new = sql_expr_new_2part_id(pParse, &t_new, &t_to_col);
			struct Expr *old_is_null =
				sqlPExpr(pParse, TK_ISNULL,
					 sqlExprDup(db, old, 0), NULL);
			eq = sqlPExpr(pParse, TK_EQ, old,
				      sqlExprDup(db, new, 0));
			struct Expr *new_non_null =
				sqlPExpr(pParse, TK_NOTNULL, new, NULL);
			struct Expr *non_null_eq =
				sqlPExpr(pParse, TK_AND, new_non_null, eq);
			struct Expr *no_action_needed =
				sqlPExpr(pParse, TK_OR, old_is_null,
					     non_null_eq);
			when = sql_and_expr_new(db, when, no_action_needed);
			if (when == NULL)
				pParse->is_aborted = true;
		}

		if (action != FKEY_ACTION_RESTRICT &&
		    (action != FKEY_ACTION_CASCADE || is_update)) {
			struct Expr *d = child_fields[chcol].default_value_expr;
			if (action == FKEY_ACTION_CASCADE) {
				new = sql_expr_new_2part_id(pParse, &t_new,
							    &t_to_col);
			} else if (action == FKEY_ACTION_SET_DEFAULT &&
				   d != NULL) {
				new = sqlExprDup(db, d, 0);
			} else {
				new = sql_expr_new_anon(db, TK_NULL);
				if (new == NULL)
					pParse->is_aborted = true;
			}
			list = sql_expr_list_append(db, list, new);
			sqlExprListSetName(pParse, list, &t_from_col, 0);
		}
	}

	const char *space_name = child_space->def->name;
	uint32_t name_len = strlen(space_name);

	if (action == FKEY_ACTION_RESTRICT) {
		struct Token err;
		err.z = space_name;
		err.n = name_len;
		struct Expr *r = sql_expr_new_named(db, TK_RAISE, "FOREIGN "\
						    "KEY constraint failed");
		if (r == NULL)
			pParse->is_aborted = true;
		else
			r->on_conflict_action = ON_CONFLICT_ACTION_ABORT;
		struct SrcList *src_list = sql_src_list_append(db, NULL, &err);
		if (src_list == NULL)
			pParse->is_aborted = true;
		select = sqlSelectNew(pParse, sql_expr_list_append(db, NULL, r),
				      src_list, where, NULL, NULL, NULL, 0,
				      NULL, NULL);
		where = NULL;
	}

	trigger = (struct sql_trigger *) sqlDbMallocZero(db,
							     sizeof(*trigger));
	if (trigger != NULL) {
		size_t step_size = sizeof(TriggerStep) + name_len + 1;
		trigger->step_list = sqlDbMallocZero(db, step_size);
		step = trigger->step_list;
		step->zTarget = (char *) &step[1];
		memcpy((char *) step->zTarget, space_name, name_len);

		step->pWhere = sqlExprDup(db, where, EXPRDUP_REDUCE);
		step->pExprList = sql_expr_list_dup(db, list, EXPRDUP_REDUCE);
		step->pSelect = sqlSelectDup(db, select, EXPRDUP_REDUCE);
		if (when != NULL) {
			when = sqlPExpr(pParse, TK_NOT, when, 0);
			trigger->pWhen =
				sqlExprDup(db, when, EXPRDUP_REDUCE);
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

	if (is_update) {
		fk->on_update_trigger = trigger;
		trigger->op = TK_UPDATE;
	} else {
		fk->on_delete_trigger = trigger;
		trigger->op = TK_DELETE;
	}
	return trigger;
}

void
fk_constraint_emit_actions(struct Parse *parser, struct space *space,
			   int reg_old, const int *changes)
{
	/*
	 * Iterate through all FKs that refer to table tab.
	 * If there is an action associated with the FK for
	 * this operation (either update or delete),
	 * invoke the associated trigger sub-program.
	 */
	assert(space != NULL);
	struct fk_constraint *fk;
	rlist_foreach_entry(fk, &space->parent_fk_constraint, in_parent_space)  {
		if (changes != NULL &&
		    !fk_constraint_is_modified(fk->def, FIELD_LINK_PARENT, changes))
			continue;
		struct sql_trigger *pAct =
			fk_constraint_action_trigger(parser, space->def, fk,
					    changes != NULL);
		if (pAct == NULL)
			continue;
		vdbe_code_row_trigger_direct(parser, pAct, space, reg_old,
					     ON_CONFLICT_ACTION_ABORT, 0);
	}
}
