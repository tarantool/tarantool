/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include "box/session.h"
#include "execute.h"
#include "bind.h"
#include "ck_constraint.h"
#include "errcode.h"
#include "schema.h"
#include "small/region.h"
#include "sql.h"
#include "sql/sqlInt.h"
#include "sql/vdbeInt.h"
#include "tuple.h"

const char *ck_constraint_language_strs[] = {"SQL"};

struct ck_constraint_def *
ck_constraint_def_new(const char *name, uint32_t name_len, const char *expr_str,
		      uint32_t expr_str_len, uint32_t space_id,
		      enum ck_constraint_language language, bool is_enabled)
{
	uint32_t expr_str_offset;
	uint32_t ck_def_sz = ck_constraint_def_sizeof(name_len, expr_str_len,
						      &expr_str_offset);
	struct ck_constraint_def *ck_def =
		(struct ck_constraint_def *) malloc(ck_def_sz);
	if (ck_def == NULL) {
		diag_set(OutOfMemory, ck_def_sz, "malloc", "ck_def");
		return NULL;
	}
	ck_def->is_enabled = is_enabled;
	ck_def->expr_str = (char *)ck_def + expr_str_offset;
	ck_def->language = language;
	ck_def->space_id = space_id;
	memcpy(ck_def->expr_str, expr_str, expr_str_len);
	ck_def->expr_str[expr_str_len] = '\0';
	memcpy(ck_def->name, name, name_len);
	ck_def->name[name_len] = '\0';
	return ck_def;
}

void
ck_constraint_def_delete(struct ck_constraint_def *ck_def)
{
	free(ck_def);
}

/**
 * Resolve space_def references for check constraint via AST
 * tree traversal.
 * @param expr Check constraint AST object to update.
 * @param space_def Space definition to use.
 * @retval 0 On success.
 * @retval -1 On error.
 */
static int
ck_constraint_resolve_field_names(struct Expr *expr,
				  struct space_def *space_def)
{
	struct Parse parser;
	sql_parser_create(&parser, sql_get(), default_flags);
	parser.parse_only = true;
	sql_resolve_self_reference(&parser, space_def, NC_IsCheck, expr);
	int rc = parser.is_aborted ? -1 : 0;
	sql_parser_destroy(&parser);
	return rc;
}

/**
 * Create a VDBE machine for the ck constraint by a given
 * definition and an expression AST. The generated instructions
 * consist of prologue code that maps vdbe_field_ref via binding
 * and ck constraint code that implements a given expression.
 * @param ck_constraint_def Check constraint definition to prepare
 *                          an error description.
 * @param expr Ck constraint expression AST built for a given
 *             @a ck_constraint_def, see for (sql_expr_compile and
 *             ck_constraint_resolve_space_def) implementation.
 * @param space_def The space definition of the space this check
 *                  constraint is constructed for.
 * @retval not NULL sql_stmt program pointer on success.
 * @retval NULL otherwise.
 */
static struct sql_stmt *
ck_constraint_program_compile(struct ck_constraint_def *ck_constraint_def,
			      struct Expr *expr)
{
	struct sql *db = sql_get();
	struct Parse parser;
	sql_parser_create(&parser, db, default_flags);
	struct Vdbe *v = sqlGetVdbe(&parser);
	if (v == NULL) {
		sql_parser_destroy(&parser);
		diag_set(OutOfMemory, sizeof(struct Vdbe), "sqlGetVdbe",
			 "vdbe");
		return NULL;
	}
	/*
	 * Generate a prologue code that introduces variables to
	 * bind vdbe_field_ref before execution.
	 */
	int vdbe_field_ref_reg = sqlGetTempReg(&parser);
	sqlVdbeAddOp2(v, OP_Variable, ++parser.nVar, vdbe_field_ref_reg);
	/* Generate ck constraint test code. */
	vdbe_emit_ck_constraint(&parser, expr, ck_constraint_def->name,
				ck_constraint_def->expr_str, vdbe_field_ref_reg);

	/* Clean-up and restore user-defined sql context. */
	bool is_error = parser.is_aborted;
	sql_finish_coding(&parser);
	sql_parser_destroy(&parser);

	if (is_error) {
		diag_set(ClientError, ER_CREATE_CK_CONSTRAINT,
			 ck_constraint_def->name,
			 box_error_message(box_error_last()));
		sql_stmt_finalize((struct sql_stmt *) v);
		return NULL;
	}
	return (struct sql_stmt *) v;
}

/**
 * Run bytecode implementing check constraint with given
 * vdbe_field_ref instance.
 * @param ck_constraint Ck constraint object to run.
 * @param field_ref The initialized vdbe_field_ref instance.
 * @retval 0 On success, when check constraint test is passed.
 * @retval -1 Otherwise. The diag message is set.
 */
static int
ck_constraint_program_run(struct ck_constraint *ck_constraint,
			  struct vdbe_field_ref *field_ref)
{
	if (sql_bind_ptr(ck_constraint->stmt, 1, field_ref) != 0) {
		diag_set(ClientError, ER_CK_CONSTRAINT_FAILED,
			 ck_constraint->def->name,
			 ck_constraint->def->expr_str);
		return -1;
	}
	/* Checks VDBE can't expire, reset expired flag and go. */
	struct Vdbe *v = (struct Vdbe *) ck_constraint->stmt;
	v->expired = 0;
	sql_step(ck_constraint->stmt);
	/*
	 * Get VDBE execution state and reset VM to run it
	 * next time.
	 */
	return sql_stmt_reset(ck_constraint->stmt);
}

int
ck_constraint_on_replace_trigger(struct trigger *trigger, void *event)
{
	(void) trigger;
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_current_stmt(txn);
	assert(stmt != NULL);
	struct tuple *new_tuple = stmt->new_tuple;
	if (new_tuple == NULL)
		return 0;

	struct space *space = stmt->space;
	assert(space != NULL);
	struct vdbe_field_ref *field_ref;
	size_t size = sizeof(field_ref->slots[0]) * space->def->field_count +
		      sizeof(*field_ref);
	field_ref = (struct vdbe_field_ref *)
		region_aligned_alloc(&fiber()->gc, size, alignof(*field_ref));
	if (field_ref == NULL) {
		diag_set(OutOfMemory, size, "region_aligned_alloc",
			 "field_ref");
		return -1;
	}
	vdbe_field_ref_prepare_tuple(field_ref, new_tuple);

	struct ck_constraint *ck_constraint;
	rlist_foreach_entry(ck_constraint, &space->ck_constraint, link) {
		if (ck_constraint->def->is_enabled &&
		    ck_constraint_program_run(ck_constraint, field_ref) != 0)
			return -1;
	}
	return 0;
}

struct ck_constraint *
ck_constraint_new(struct ck_constraint_def *ck_constraint_def,
		  struct space_def *space_def)
{
	if (space_def->field_count == 0) {
		diag_set(ClientError, ER_UNSUPPORTED, "Tarantool",
			 "CK constraint for space without format");
		return NULL;
	}
	struct ck_constraint *ck_constraint = malloc(sizeof(*ck_constraint));
	if (ck_constraint == NULL) {
		diag_set(OutOfMemory, sizeof(*ck_constraint), "malloc",
			 "ck_constraint");
		return NULL;
	}
	ck_constraint->def = NULL;
	ck_constraint->stmt = NULL;
	rlist_create(&ck_constraint->link);
	struct Expr *expr =
		sql_expr_compile(sql_get(), ck_constraint_def->expr_str,
				 strlen(ck_constraint_def->expr_str));
	if (expr == NULL ||
	    ck_constraint_resolve_field_names(expr, space_def) != 0) {
		diag_set(ClientError, ER_CREATE_CK_CONSTRAINT,
			 ck_constraint_def->name,
			 box_error_message(box_error_last()));
		goto error;
	}
	ck_constraint->stmt =
		ck_constraint_program_compile(ck_constraint_def, expr);
	if (ck_constraint->stmt == NULL)
		goto error;

	sql_expr_delete(sql_get(), expr, false);
	ck_constraint->def = ck_constraint_def;
	return ck_constraint;
error:
	sql_expr_delete(sql_get(), expr, false);
	ck_constraint_delete(ck_constraint);
	return NULL;
}

void
ck_constraint_delete(struct ck_constraint *ck_constraint)
{
	sql_stmt_finalize(ck_constraint->stmt);
	ck_constraint_def_delete(ck_constraint->def);
	TRASH(ck_constraint);
	free(ck_constraint);
}

struct ck_constraint *
space_ck_constraint_by_name(struct space *space, const char *name,
			    uint32_t name_len)
{
	struct ck_constraint *ck_constraint = NULL;
	rlist_foreach_entry(ck_constraint, &space->ck_constraint, link) {
		if (strlen(ck_constraint->def->name) == name_len &&
		    memcmp(ck_constraint->def->name, name, name_len) == 0)
			return ck_constraint;
	}
	return NULL;
}
