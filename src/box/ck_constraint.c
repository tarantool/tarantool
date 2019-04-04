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
#include "ck_constraint.h"
#include "errcode.h"
#include "sql.h"
#include "sql/sqlInt.h"

const char *ck_constraint_language_strs[] = {"SQL"};

struct ck_constraint_def *
ck_constraint_def_new(const char *name, uint32_t name_len, const char *expr_str,
		      uint32_t expr_str_len, uint32_t space_id,
		      enum ck_constraint_language language)
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
	rlist_create(&ck_constraint->link);
	ck_constraint->expr =
		sql_expr_compile(sql_get(), ck_constraint_def->expr_str,
				 strlen(ck_constraint_def->expr_str));
	if (ck_constraint->expr == NULL ||
	    ck_constraint_resolve_field_names(ck_constraint->expr,
					      space_def) != 0) {
		diag_set(ClientError, ER_CREATE_CK_CONSTRAINT,
			 ck_constraint_def->name,
			 box_error_message(box_error_last()));
		goto error;
	}

	ck_constraint->def = ck_constraint_def;
	return ck_constraint;
error:
	ck_constraint_delete(ck_constraint);
	return NULL;
}

void
ck_constraint_delete(struct ck_constraint *ck_constraint)
{
	sql_expr_delete(sql_get(), ck_constraint->expr, false);
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
