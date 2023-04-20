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
 * This file contains C code routines that used to generate VDBE code
 * that implements the ALTER TABLE command.
 */
#include "sqlInt.h"
#include "box/box.h"
#include "box/schema.h"

void
sql_alter_table_rename(struct Parse *parse)
{
	struct rename_entity_def *rename_def = &parse->rename_entity_def;
	struct SrcList *src_tab = rename_def->base.entity_name;
	assert(rename_def->base.entity_type == ENTITY_TYPE_TABLE);
	assert(rename_def->base.alter_action == ALTER_ACTION_RENAME);
	assert(src_tab->nSrc == 1);
	char *new_name = sql_name_from_token(&rename_def->new_name);
	/* Check that new name isn't occupied by another table. */
	if (space_by_name0(new_name) != NULL) {
		diag_set(ClientError, ER_SPACE_EXISTS, new_name);
		goto tnt_error;
	}
	const char *tbl_name = src_tab->a[0].zName;
	struct space *space = space_by_name0(tbl_name);
	if (space == NULL) {
		diag_set(ClientError, ER_NO_SUCH_SPACE, tbl_name);
		goto tnt_error;
	}
	sql_set_multi_write(parse, false);
	/* Drop and reload the internal table schema. */
	struct Vdbe *v = sqlGetVdbe(parse);
	sqlVdbeAddOp4(v, OP_RenameTable, space->def->id, 0, 0, new_name,
			  P4_DYNAMIC);
exit_rename_table:
	sqlSrcListDelete(src_tab);
	return;
tnt_error:
	sql_xfree(new_name);
	parse->is_aborted = true;
	goto exit_rename_table;
}

char *
rename_trigger(char const *sql_stmt, char const *table_name, bool *is_quoted)
{
	assert(sql_stmt);
	assert(table_name);
	assert(is_quoted);

	int token;
	Token tname;
	int dist = 3;
	char const *csr = (char const*)sql_stmt;
	int len = 0;
	bool unused;

	/* The principle used to locate the table name in the CREATE TRIGGER
	 * statement is that the table name is the first token that is immediately
	 * preceded by either TK_ON or TK_DOT and immediately followed by one
	 * of TK_WHEN, TK_BEGIN or TK_FOR.
	 */
	do {
		if (!*csr) {
			/* Ran out of input before finding the table name. */
			return NULL;
		}
		/* Store the token that csr points to in tname. */
		tname.z = (char *) csr;
		tname.n = len;
		/* Advance zCsr to the next token. Store that token type in 'token',
		 * and its length in 'len' (to be used next iteration of this loop).
		 */
		do {
			csr += len;
			len = sql_token(csr, &token, &unused);
		} while (token == TK_SPACE);
		assert(len > 0);
		/* Variable 'dist' stores the number of tokens read since the most
		 * recent TK_ON. This means that when a WHEN, FOR or BEGIN
		 * token is read and 'dist' equals 2, the condition stated above
		 * to be met.
		 *
		 * Note that ON cannot be a table or column name, so
		 * there is no need to worry about syntax like
		 * "CREATE TRIGGER ... ON ON BEGIN ..." etc.
		 */
		dist++;
		if (token == TK_ON) {
			dist = 0;
		}
	} while (dist != 2 ||
		 (token != TK_WHEN && token != TK_FOR && token != TK_BEGIN));

	if (*tname.z  == '"')
		*is_quoted = true;
	/* Variable tname now contains the token that is the old table-name
	 * in the CREATE TRIGGER statement.
	 */
	return sqlMPrintf("%.*s\"%w\"%s", (int)((tname.z) - sql_stmt), sql_stmt,
			  table_name, tname.z + tname.n);
}
