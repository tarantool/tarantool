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
#include "box/tuple_format.h"

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
		sqlVdbeAddOp2(v, OP_FkIfZero, false, ok_label);
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
		sqlVdbeAddOp3(v, OP_MakeRecord, temp_regs, field_count,
			      rec_reg);
		sqlVdbeAddOp4Int(v, OP_Found, cursor, ok_label, rec_reg, 0);
		sqlReleaseTempReg(parse_context, rec_reg);
		sqlReleaseTempRange(parse_context, temp_regs, field_count);
	}
	if (parse_context->pToplevel == NULL && !parse_context->isMultiWrite) {
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
		sqlVdbeAddOp2(v, OP_FkCounter, false, incr_count);
	}
	sqlVdbeResolveLabel(v, ok_label);
	sqlVdbeAddOp1(v, OP_Close, cursor);
}

/**
 * Build an expression that refers to a memory register
 * corresponding to @a column of given space.
 *
 * @param def Definition of space whose content starts from
 *        @a reg_base register.
 * @param reg_base Index of a first element in an array of
 *        registers, containing data of a space. Register
 *        reg_base + i holds an i-th column, i >= 1.
 * @param column Index of a first table column to point at.
 * @retval Not NULL Success. An expression representing register.
 */
static struct Expr *
sql_expr_new_register(struct space_def *def, int reg_base, uint32_t column)
{
	struct Expr *expr = sql_expr_new_anon(TK_REGISTER);
	expr->iTable = reg_base + column + 1;
	expr->type = def->fields[column].type;
	return expr;
}

/**
 * Return an Expr object that refers to column of space_def which
 * has cursor cursor.
 *
 * @param def space definition.
 * @param cursor The open cursor on the table.
 * @param column The column that is wanted.
 * @retval not NULL on success.
 */
static struct Expr *
sql_expr_new_column_by_cursor(struct space_def *def, int cursor, int column)
{
	struct Expr *expr = sql_expr_new_anon(TK_COLUMN_REF);
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
	struct Expr *where = NULL;
	/* Address of OP_FkIfZero. */
	int fkifzero_label = 0;
	struct Vdbe *v = sqlGetVdbe(parser);

	if (incr_count < 0) {
		fkifzero_label = sqlVdbeAddOp2(v, OP_FkIfZero, false, 0);
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
			sql_expr_new_register(def, reg_data, fieldno);
		fieldno = fk_def->links[i].child_field;
		const char *field_name = child_space->def->fields[fieldno].name;
		struct Expr *chexpr = sql_expr_new_named(TK_ID, field_name);
		struct Expr *eq = sqlPExpr(parser, TK_EQ, pexpr, chexpr);
		where = sql_and_expr_new(where, eq);
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
			pexpr = sql_expr_new_register(def, reg_data, fieldno);
			int cursor = src->a[0].iCursor;
			chexpr = sql_expr_new_column_by_cursor(def, cursor,
							       fieldno);
			eq = sqlPExpr(parser, TK_EQ, pexpr, chexpr);
			expr = sql_and_expr_new(expr, eq);
		}
		struct Expr *pNe = sqlPExpr(parser, TK_NOT, expr, 0);
		where = sql_and_expr_new(where, pNe);
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
	sqlVdbeAddOp2(v, OP_FkCounter, false, incr_count);
	if (info != NULL)
		sqlWhereEnd(info);

	/* Clean up the WHERE clause constructed above. */
	sql_expr_delete(where);
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

void
fk_constraint_emit_check(struct Parse *parser, struct space *space, int reg_old,
			 int reg_new, const int *changed_cols)
{
	bool is_update = changed_cols != NULL;

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
		if (reg_new != 0) {
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
		if (parser->pToplevel == NULL && !parser->isMultiWrite) {
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
		struct SrcList *src = sql_src_list_append(NULL, NULL);
		struct SrcList_item *item = src->a;
		struct space *child = space_by_id(fk->def->child_id);
		assert(child != NULL);
		item->space = child;
		item->zName = sql_xstrdup(child->def->name);
		item->iCursor = parser->nTab++;

		if (reg_new != 0) {
			fk_constraint_scan_children(parser, src, space->def,
						    fk->def, reg_new, -1);
		}
		if (reg_old != 0) {
			fk_constraint_scan_children(parser, src, space->def,
						    fk->def, reg_old, 1);
		}
		sqlSrcListDelete(src);
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
