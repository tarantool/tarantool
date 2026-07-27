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
 * An tokenizer for SQL
 *
 * This file contains C code that splits an SQL input string up into
 * individual tokens and sends those tokens one-by-one over to the
 * parser for analysis.
 */
#include <unicode/utf8.h>
#include <unicode/uchar.h>

#include "say.h"
#include "sqlInt.h"

/* Character classes for tokenizing
 *
 * In the sql_token() function, a switch() on sql_ascii_class[c]
 * is implemented using a lookup table, whereas a switch()
 * directly on c uses a binary search. The lookup table is much
 * faster. To maximize speed, and to ensure that a lookup table is
 * used, all of the classes need to be small integers and all of
 * them need to be used within the switch.
 */
#define CC_X          0		/* The letter 'x', or start of BLOB literal */
#define CC_KYWD       1		/* Alphabetics or '_'.  Usable in a keyword */
#define CC_ID         2		/* unicode characters usable in IDs */
#define CC_DIGIT      3		/* Digits */
/** Character ':'. */
#define CC_COLON      4
/** SQL variable special characters: '@', '#', and '$'. */
#define CC_VARALPHA   5
#define CC_VARNUM     6		/* '?'.  Numeric SQL variables */
#define CC_SPACE      7		/* Space characters */
#define CC_QUOTE      8		/* '\''. String literals */
#define CC_DQUOTE     9		/* '"'. Identifiers*/
#define CC_PIPE      10		/* '|'.   Bitwise OR or concatenate */
#define CC_MINUS     11		/* '-'.  Minus or SQL-style comment */
#define CC_LT        12		/* '<'.  Part of < or <= or <> */
#define CC_GT        13		/* '>'.  Part of > or >= */
#define CC_EQ        14		/* '='.  Part of = or == */
#define CC_BANG      15		/* '!'.  Part of != */
#define CC_SLASH     16		/* '/'.  / or c-style comment */
#define CC_LP        17		/* '(' */
#define CC_RP        18		/* ')' */
#define CC_SEMI      19		/* ';' */
#define CC_PLUS      20		/* '+' */
#define CC_STAR      21		/* '*' */
#define CC_PERCENT   22		/* '%' */
#define CC_COMMA     23		/* ',' */
#define CC_AND       24		/* '&' */
#define CC_TILDA     25		/* '~' */
#define CC_DOT       26		/* '.' */
#define CC_ILLEGAL   27		/* Illegal character */
#define CC_LINEFEED  28		/* '\n' */
#define CC_LB        29		/* '[' */
#define CC_RB        30		/* ']' */
/** Character '{'. */
#define CC_LCB       31
/** Character '}'. */
#define CC_RCB       32

static const char sql_ascii_class[] = {
/*       x0  x1  x2  x3  x4  x5  x6  x7  x8 x9  xa xb  xc xd xe  xf */
/* 0x */ 27, 27, 27, 27, 27, 27, 27, 27, 27, 7, 28, 7, 7, 7, 27, 27,
/* 1x */ 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
/* 2x */ 7, 15, 9, 5, 5, 22, 24, 8, 17, 18, 21, 20, 23, 11, 26, 16,
/* 3x */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 19, 12, 14, 13, 6,
/* 4x */ 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 5x */ 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 29, 27, 30, 27, 1,
/* 6x */ 27, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 7x */ 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 31, 10, 32, 25, 27,
/* 8x */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* 9x */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* Ax */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* Bx */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* Cx */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* Dx */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* Ex */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* Fx */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
};

/**
 * The charMap() macro maps alphabetic characters (only) into
 * their lower-case ASCII equivalent.  On ASCII machines, this
 * is just an upper-to-lower case map.
 *
 * Used by keywordhash.h
 */
#define charMap(X) sqlUpperToLower[(unsigned char)X]

/*
 * The sqlKeywordCode function looks up an identifier to determine if
 * it is a keyword.  If it is a keyword, the token code of that keyword is
 * returned.  If the input is not a keyword, TK_ID is returned.
 *
 * The implementation of this routine was generated by a program,
 * mkkeywordhash.c, located in the tool subdirectory of the distribution.
 * The output of the mkkeywordhash.c program is written into a file
 * named keywordhash.h and then included into this source file by
 * the #include below.
 */
#include "keywordhash.h"

#define maybe_utf8(c) ((sqlCtypeMap[c] & 0x40) != 0)

/**
 * Return true if current symbol is space.
 *
 * @param z Input stream.
 * @retval True if current symbol space.
 */
static inline bool
sql_is_space_char(const char *z)
{
	if (sqlIsspace(z[0]))
		return true;
	if (maybe_utf8(*(unsigned char*)z)) {
		UChar32 c;
		int unused = 0;
		U8_NEXT_UNSAFE(z, unused, c);
		if (u_isspace(c))
			return true;
	}
	return false;
}

/**
 * Calculate length of continuous sequence of
 * space symbols.
 *
 * @param z Input stream.
 * @retval Number of bytes which constitute sequence of spaces.
 *         Can be 0 if first symbol in stram is not space.
 */
static inline int
sql_skip_spaces(const char *z)
{
	int idx = 0;
	while (true) {
		if (sqlIsspace(z[idx])) {
			idx += 1;
		} else if (maybe_utf8(*(unsigned char *)(z + idx))) {
			UChar32 c;
			int new_offset = idx;
			U8_NEXT_UNSAFE(z, new_offset, c);
			if (!u_isspace(c))
				break;
			idx = new_offset;
		} else {
			break;
		}
	}
	return idx;
}

int
sql_token(const char *z, int *type, bool *is_reserved)
{
	*is_reserved = false;
	int i;
	char c, delim;
	/* Switch on the character-class of the first byte
	 * of the token. See the comment on the CC_ defines
	 * above.
	 */
	switch (sql_ascii_class[*(unsigned char*)z]) {
	case CC_SPACE:
		i = 1 + sql_skip_spaces(z+1);
		*type = TK_SPACE;
		return i;
	case CC_LINEFEED:
		*type = TK_LINEFEED;
		return 1;
	case CC_MINUS:
		/*
		 * Ignore single-line comment started with "--"
		 * till the end of parsing string or next line.
		 */
		if (z[1] == '-') {
			for (i = 2; true; i++) {
				if (z[i] == '\0') {
					*type = TK_SPACE;
					return i;
				} else if (z[i] == '\n') {
					*type = TK_LINEFEED;
					return ++i;
				}
			}
		}
		*type = TK_MINUS;
		return 1;
	case CC_LP:
		*type = TK_LP;
		return 1;
	case CC_RP:
		*type = TK_RP;
		return 1;
	case CC_LB:
		*type = TK_LB;
		return 1;
	case CC_RB:
		*type = TK_RB;
		return 1;
	case CC_LCB:
		*type = TK_LCB;
		return 1;
	case CC_RCB:
		*type = TK_RCB;
		return 1;
	case CC_SEMI:
		*type = TK_SEMI;
		return 1;
	case CC_PLUS:
		*type = TK_PLUS;
		return 1;
	case CC_STAR:
		*type = TK_STAR;
		return 1;
	case CC_SLASH:
		if (z[1] != '*' || z[2] == 0) {
			*type = TK_SLASH;
			return 1;
		}
		for (i = 3, c = z[2];
		     (c != '*' || z[i] != '/') && (c = z[i]) != 0;
		     i++) {
		}
		if (c)
			i++;
		*type = TK_SPACE;
		return i;
	case CC_PERCENT:
		*type = TK_REM;
		return 1;
	case CC_EQ:
		*type = TK_EQ;
		return 1 + (z[1] == '=');
	case CC_LT:
		if ((c = z[1]) == '=') {
			*type = TK_LE;
			return 2;
		} else if (c == '>') {
			*type = TK_NE;
			return 2;
		} else if (c == '<') {
			*type = TK_LSHIFT;
			return 2;
		} else {
			*type = TK_LT;
			return 1;
		}
	case CC_GT:
		if ((c = z[1]) == '=') {
			*type = TK_GE;
			return 2;
		} else if (c == '>') {
			*type = TK_RSHIFT;
			return 2;
		} else {
			*type = TK_GT;
			return 1;
		}
	case CC_BANG:
		if (z[1] != '=') {
			*type = TK_ILLEGAL;
			return 1;
		} else {
			*type = TK_NE;
			return 2;
		}
	case CC_PIPE:
		if (z[1] != '|') {
			*type = TK_BITOR;
			return 1;
		} else {
			*type = TK_CONCAT;
			return 2;
		}
	case CC_COMMA:
		*type = TK_COMMA;
		return 1;
	case CC_AND:
		*type = TK_BITAND;
		return 1;
	case CC_TILDA:
		*type = TK_BITNOT;
		return 1;
	case CC_QUOTE:
	case CC_DQUOTE:
		delim = z[0];
		for (i = 1; (c = z[i]) != 0; i++) {
			if (c == delim) {
				if (z[i + 1] == delim)
					i++;
				else
					break;
			}
		}
		if (c == '\'') {
			*type = TK_STRING;
			return i + 1;
		} else if (c != 0) {
			*type = TK_ID;
			return i + 1;
		} else {
			*type = TK_ILLEGAL;
			return i;
		}
		FALLTHROUGH;
	case CC_DOT:
		if (!sqlIsdigit(z[1])) {
			*type = TK_DOT;
			return 1;
		}
		/* If the next character is a digit, this is a
		 * floating point number that begins with ".".
		 * Fall thru into the next case.
		 */
		FALLTHROUGH;
	case CC_DIGIT:
		*type = TK_INTEGER;
		if (z[0] == '0' && (z[1] == 'x' || z[1] == 'X') &&
		    sqlIsxdigit(z[2])) {
			for (i = 3; sqlIsxdigit(z[i]); i++) {
			}
			return i;
		}
		for (i = 0; sqlIsdigit(z[i]); i++) {
		}
		if (z[i] == '.') {
			while (sqlIsdigit(z[++i])) {
			}
			*type = TK_DECIMAL;
		}
		if ((z[i] == 'e' || z[i] == 'E') &&
		    (sqlIsdigit(z[i + 1])
		     || ((z[i + 1] == '+' || z[i + 1] == '-') &&
			 sqlIsdigit(z[i + 2])))) {
			i += 2;
			while (sqlIsdigit(z[i]))
				i++;
			*type = TK_FLOAT;
		}
		if (IdChar(z[i])) {
			*type = TK_ILLEGAL;
			while (IdChar(z[++i])) {
			}
		}
		return i;
	case CC_VARNUM:
		*type = TK_VARNUM;
		return 1;
	case CC_COLON:
		*type = TK_COLON;
		return 1;
	case CC_VARALPHA:
		*type = TK_VARIABLE;
		return 1;
	case CC_KYWD:
		for (i = 1; sql_ascii_class[*(unsigned char*)(z+i)] <= CC_KYWD;
		     i++) {
		}
		if (!sql_is_space_char(z + i) && IdChar(z[i])) {
			/* This token started out using characters
			 * that can appear in keywords, but z[i] is
			 * a character not allowed within keywords,
			 * so this must be an identifier instead.
			 */
			i++;
			break;
		}
		*type = TK_ID;
		return keywordCode(z, i, type, is_reserved);
	case CC_X:
		if (z[1] == '\'') {
			*type = TK_BLOB;
			for (i = 2; sqlIsxdigit(z[i]); i++) {
			}
			if (z[i] != '\'' || i % 2) {
				*type = TK_ILLEGAL;
				while (z[i] != 0 && z[i] != '\'')
					i++;
			}
			if (z[i] != 0)
				i++;
			return i;
		}
		/* If it is not a BLOB literal, then it must be an
		 * ID, since no SQL keywords start with the letter
		 * 'x'.  Fall through.
		 */
		FALLTHROUGH;
	case CC_ID:
		i = 1;
		break;
	default:
		*type = TK_ILLEGAL;
		return 1;
	}
	int spaces_len = sql_skip_spaces(z);
	if (spaces_len > 0) {
		*type = TK_SPACE;
		return spaces_len;
	}
	while (IdChar(z[i]))
		i++;
	*type = TK_ID;
	return i;
}

static void *
new_xmalloc(size_t n)
{
	return xmalloc(n);
}

/** Code AST for SELECT statement. */
static void
sql_code_select(struct Parse *parser, struct ast_select *select)
{
	struct Select *res = select_from_ast(parser, select);
	if (parser->is_aborted)
		return;
	struct SelectDest dest = {SRT_Output, NULL, 0, 0, 0, 0, NULL};
	sqlSelect(parser, res, &dest);
	sql_select_delete(res);
}

/** Code AST for DROP CONSTRAINT statements. */
static void
sql_code_ast_drop_constraint(struct Parse *parser, struct Token *name,
			     struct Token *column, struct Token *table,
			     enum ast_property_type type)
{
	parser->initiateTTrans = true;
	if (column->n == 0) {
		/* Table constraint. */
		switch (type) {
		case SQL_AST_PROPERTY_CHECK:
			sql_drop_tuple_check(parser, table, name);
			break;
		case SQL_AST_PROPERTY_UNIQUE:
			sql_drop_unique(parser, table, name);
			break;
		case SQL_AST_PROPERTY_PRIMARY_KEY:
			sql_drop_primary_key(parser, table, name);
			break;
		case SQL_AST_PROPERTY_FOREIGN_KEY:
			sql_drop_tuple_foreign_key(parser, table, name);
			break;
		default:
			assert(type == SQL_AST_PROPERTY_ANY);
			sql_drop_table_constraint(parser, table, name);
		}
		return;
	}
	switch (type) {
	case SQL_AST_PROPERTY_CHECK:
		sql_drop_field_check(parser, table, column, name);
		break;
	case SQL_AST_PROPERTY_FOREIGN_KEY:
		sql_drop_field_foreign_key(parser, table, column, name);
		break;
	default:
		assert(type == SQL_AST_PROPERTY_ANY);
		sql_drop_field_constraint(parser, table, column, name);
	}
}

/** Code AST for table or column property. */
static void
sql_code_ast_property(struct Parse *parser, struct Token *table,
		      struct ast_property *property, bool is_column)
{
	switch (property->type) {
	case SQL_AST_PROPERTY_CHECK:
		sql_create_check_constraint(parser, table, &property->name,
					    property->expr->str,
					    property->expr->len, is_column);
		break;
	case SQL_AST_PROPERTY_FOREIGN_KEY: {
		struct ast_foreign_key *fk = &property->foreign_key;
		struct ExprList *columns =
			expr_list_from_ids(parser, fk->columns);
		if (parser->is_aborted)
			break;
		struct ExprList *foreign_columns =
			expr_list_from_ids(parser, fk->foreign_columns);
		if (parser->is_aborted) {
			sql_expr_list_delete(columns);
			break;
		}
		sql_create_foreign_key(parser, table, &property->name, columns,
				       &fk->foreign_table, foreign_columns);
		break;
	}
	case SQL_AST_PROPERTY_UNIQUE: {
		struct ExprList *columns =
			expr_list_from_ast(parser, property->columns);
		sql_create_index(parser, table, &property->name, columns,
				 SQL_INDEX_TYPE_CONSTRAINT_UNIQUE,
				 SORT_ORDER_ASC, false);
		break;
	}
	case SQL_AST_PROPERTY_PRIMARY_KEY: {
		if (is_column) {
			sqlAddPrimaryKey(parser, &property->name, NULL,
					 property->order);
			break;
		}
		struct ExprList *columns =
			expr_list_from_ast(parser, property->columns);
		if (parser->is_aborted)
			break;
		if (table->n == 0) {
			sqlAddPrimaryKey(parser, &property->name, columns,
					 property->order);
			break;
		}
		sql_create_index(parser, table, &property->name, columns,
				 SQL_INDEX_TYPE_CONSTRAINT_PK,
				 SORT_ORDER_ASC, false);
		break;
	}
	case SQL_AST_PROPERTY_DEFAULT: {
		assert(is_column);
		struct Expr *expr = expr_from_ast(parser, property->expr);
		if (parser->is_aborted)
			break;
		sql_column_add_default(parser, expr, property->expr->str,
				       property->expr->len);
		break;
	}
	case SQL_AST_PROPERTY_COLLATE:
		sqlAddCollateType(parser, &property->collate);
		break;
	case SQL_AST_PROPERTY_NOT_NULL:
		sql_column_add_nullable_action(parser, property->action);
		break;
	case SQL_AST_PROPERTY_NULL:
		sql_column_add_nullable_action(parser, ON_CONFLICT_ACTION_NONE);
		break;
	case SQL_AST_PROPERTY_ANY:
		assert(false);
	}
}

/** Code AST for column. */
static void
sql_code_ast_column(struct Parse *parser, struct Token *table,
		    struct ast_column *col)
{
	sql_create_column_start(parser, table, &col->name, col->type);
	if (parser->is_aborted)
		return;
	if (col->properties != NULL) {
		struct ast_property *property;
		stailq_foreach_entry(property, &col->properties->head, link) {
			sql_code_ast_property(parser, table, property, true);
			if (parser->is_aborted)
				return;
		}
	}
	if (!col->is_autoinc)
		return;
	sql_add_autoincrement(parser, parser->space->def->field_count - 1);
}

/** Code AST for CREATE TABLE. */
static void
sql_code_create_table(struct Parse *parser, struct ast_create_table *stmt)
{
	parser->disableLookaside++;
	sql_get()->lookaside.bDisable++;
	rlist_create(&parser->create_ck_constraint_parse_def.checks);
	rlist_create(&parser->create_fk_constraint_parse_def.fkeys);
	parser->create_fk_constraint_parse_def.is_used = true;
	parser->new_space = sqlStartTable(parser, &stmt->name);
	parser->initiateTTrans = true;
	if (parser->is_aborted)
		return;

	struct Token tmp = Token_nil;
	struct ast_column *column;
	stailq_foreach_entry(column, &stmt->properties->columns, link) {
		sql_code_ast_column(parser, &tmp, column);
		if (parser->is_aborted)
			return;
	}

	struct ast_property *constraint;
	stailq_foreach_entry(constraint, &stmt->properties->constraints, link) {
		sql_code_ast_property(parser, &tmp, constraint, false);
		if (parser->is_aborted)
			return;
	}

	if (stmt->engine.n > 0) {
		if (stmt->engine.n > ENGINE_NAME_MAX) {
			diag_set(ClientError, ER_CREATE_SPACE,
				 parser->new_space->def->name,
				 "space engine name is too long");
			parser->is_aborted = true;
			return;
		}
		char *engine = sql_name_from_token(&stmt->engine);
		memcpy(parser->new_space->def->engine_name, engine,
		       strlen(engine) + 1);
		sql_xfree(engine);
	}
	vdbe_emit_create_table(parser, stmt->if_not_exists);
}

/** Code AST for CREATE VIEW. */
static void
sql_code_create_view(struct Parse *parser, struct ast_create_view *stmt,
		     const char *sql)
{
	parser->disableLookaside++;
	sql_get()->lookaside.bDisable++;
	parser->initiateTTrans = true;
	struct Select *select = select_from_ast(parser, stmt->select);
	struct ExprList *cols = expr_list_from_ids(parser, stmt->columns);
	if (parser->is_aborted) {
		sql_expr_list_delete(cols);
		sql_select_delete(select);
		return;
	}
	sql_create_view(parser, sql, &stmt->name, cols, select,
			stmt->if_not_exists);
}

/** Code AST for CREATE INDEX. */
static void
sql_code_create_index(struct Parse *parser, struct ast_create_index *stmt)
{
	parser->disableLookaside++;
	sql_get()->lookaside.bDisable++;
	parser->initiateTTrans = true;
	struct ExprList *columns = expr_list_from_ast(parser, stmt->columns);
	if (parser->is_aborted)
		return;
	int type = stmt->is_unique ? SQL_INDEX_TYPE_UNIQUE :
				     SQL_INDEX_TYPE_NON_UNIQUE;
	sql_create_index(parser, &stmt->table, &stmt->name, columns, type,
			 SORT_ORDER_ASC, stmt->if_not_exists);
}

/** Code AST for INSERT statement. */
static void
sql_code_insert(struct Parse *parser, struct ast_insert *insert)
{
	struct With *with = with_from_ast(parser, insert->with);
	if (parser->is_aborted)
		return;
	sqlWithPush(parser, with, 1);
	struct Select *select = select_from_ast(parser, insert->select);
	if (parser->is_aborted)
		return;
	struct SrcList *src = sql_src_list_append(NULL, &insert->table);
	sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
	parser->initiateTTrans = true;
	struct IdList *columns = id_list_from_ast(insert->columns);
	sqlInsert(parser, src, select, columns, insert->action);
}

/** Code AST for UPDATE statement. */
static void
sql_code_update(struct Parse *parser, struct ast_update *update)
{
	assert(update->set_list != NULL);
	struct With *with = with_from_ast(parser, update->with);
	if (parser->is_aborted)
		return;
	sqlWithPush(parser, with, 1);

	struct ExprList *set_list =
		expr_list_from_set_list(parser, update->set_list);
	if (parser->is_aborted)
		return;

	if (set_list->nExpr > SQL_MAX_COLUMN) {
		diag_set(ClientError, ER_SQL_PARSER_LIMIT,
			 "The number of columns in set list",
			 update->set_list->len, SQL_MAX_COLUMN);
		parser->is_aborted = true;
		sql_expr_list_delete(set_list);
		return;
	}

	struct Expr *where = expr_from_ast(parser, update->where);
	if (parser->is_aborted) {
		sql_expr_list_delete(set_list);
		return;
	}

	struct SrcList *src = sql_src_list_append(NULL, &update->table);
	sqlSrcListIndexedBy(src, &update->indexed_by);
	sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
	parser->initiateTTrans = true;
	sqlUpdate(parser, src, set_list, where, update->action);
}

/** Code AST for DELETE statement. */
static void
sql_code_delete(struct Parse *parser, struct ast_delete *del)
{
	struct With *with = with_from_ast(parser, del->with);
	if (parser->is_aborted)
		return;
	sqlWithPush(parser, with, 1);

	struct Expr *where = expr_from_ast(parser, del->where);
	if (parser->is_aborted)
		return;

	struct SrcList *src = sql_src_list_append(NULL, &del->table);
	sqlSrcListIndexedBy(src, &del->indexed_by);
	sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
	parser->initiateTTrans = true;
	sql_table_delete_from(parser, src, where);
}

/** Code AST for SET SESSION statement. */
static void
sql_code_set_session(struct Parse *parser, struct Token *name,
		     struct ast_expr *value)
{
	struct Expr *expr = expr_from_ast(parser, value);
	if (parser->is_aborted)
		return;
	sql_setting_set(parser, name, expr);
}

/** Code given AST. */
static void
sql_code_ast(struct Parse *parse, struct sql_ast *ast, const char *sql)
{
	if (parse->is_aborted)
		return;
	switch (ast->type) {
	case SQL_AST_TX_START:
		sql_transaction_begin(parse);
		break;
	case SQL_AST_TX_COMMIT:
		sql_transaction_commit(parse);
		break;
	case SQL_AST_TX_ROLLBACK:
		sql_transaction_rollback(parse);
		break;
	case SQL_AST_TX_SAVEPOINT_NEW:
		sqlSavepoint(parse, SAVEPOINT_BEGIN, &ast->savepoint);
		break;
	case SQL_AST_TX_SAVEPOINT_RELEASE:
		sqlSavepoint(parse, SAVEPOINT_RELEASE, &ast->savepoint);
		break;
	case SQL_AST_TX_SAVEPOINT_ROLLBACK:
		sqlSavepoint(parse, SAVEPOINT_ROLLBACK, &ast->savepoint);
		break;
	case SQL_AST_SELECT:
		sql_code_select(parse, ast->select);
		break;
	case SQL_AST_INSERT:
		sql_code_insert(parse, ast->insert);
		break;
	case SQL_AST_UPDATE:
		sql_code_update(parse, ast->update);
		break;
	case SQL_AST_DELETE:
		sql_code_delete(parse, ast->del);
		break;
	case SQL_AST_TRUNCATE:
		parse->initiateTTrans = true;
		sql_table_truncate(parse, &ast->truncate.table);
		break;
	case SQL_AST_CREATE_TABLE:
		sql_code_create_table(parse, &ast->create_table);
		break;
	case SQL_AST_CREATE_VIEW:
		sql_code_create_view(parse, &ast->create_view, sql);
		break;
	case SQL_AST_CREATE_INDEX:
		sql_code_create_index(parse, &ast->create_index);
		break;
	case SQL_AST_DROP_VIEW:
	case SQL_AST_DROP_TABLE:
		parse->initiateTTrans = true;
		sql_drop_table(parse, &ast->drop_table.name,
			       ast->drop_table.if_exists,
			       ast->type == SQL_AST_DROP_VIEW);
		break;
	case SQL_AST_DROP_TRIGGER:
		parse->initiateTTrans = true;
		sql_drop_trigger(parse, &ast->drop_trigger.name,
				 ast->drop_trigger.if_exists);
		break;
	case SQL_AST_DROP_INDEX:
		parse->initiateTTrans = true;
		sql_drop_index(parse, &ast->drop_index.name,
			       &ast->drop_index.table,
			       ast->drop_index.if_exists);
		break;
	case SQL_AST_ALTER_RENAME:
		parse->initiateTTrans = true;
		sql_alter_table_rename(parse, &ast->alter_rename.old_name,
				       &ast->alter_rename.new_name);
		break;
	case SQL_AST_ALTER_ADD_COLUMN:
		parse->initiateTTrans = true;
		rlist_create(&parse->create_ck_constraint_parse_def.checks);
		rlist_create(&parse->create_fk_constraint_parse_def.fkeys);
		parse->create_fk_constraint_parse_def.is_used = true;
		sql_code_ast_column(parse, &ast->alter_add_column.table,
				    ast->alter_add_column.col);
		if (parse->is_aborted)
			break;
		sql_create_column_end(parse);
		break;
	case SQL_AST_ALTER_DROP_CONSTRAINT:
		sql_code_ast_drop_constraint(parse,
					     &ast->alter_drop_constraint.name,
					     &ast->alter_drop_constraint.column,
					     &ast->alter_drop_constraint.table,
					     ast->alter_drop_constraint.type);
		break;
	case SQL_AST_ALTER_ADD_CONSTRAINT:
		parse->initiateTTrans = true;
		sql_code_ast_property(parse, &ast->alter_add_constraint.table,
				      ast->alter_add_constraint.con, false);
		break;
	case SQL_AST_SET_SESSION:
		sql_code_set_session(parse, &ast->set_session.name,
				     ast->set_session.value);
		break;
	case SQL_AST_PRAGMA:
		sqlPragma(parse, &ast->pragma.name, &ast->pragma.table_name,
			  &ast->pragma.index_name);
		break;
	case SQL_AST_VIEW:
		return;
	default:
		assert(parse->ast.type == SQL_AST_UNKNOWN);
	}
	if (!parse->is_aborted && parse->parsed_ast_type == AST_TYPE_UNDEFINED)
		sql_finish_coding(parse);
}

/**
 * Run the parser on the given SQL string.
 *
 * @param pParse Parser context.
 * @param zSql SQL string.
 * @param seed_token Token type to feed before `zSql`, or 0.
 * @retval 0 on success.
 * @retval -1 on error.
 */
static int
sql_run_parser(struct Parse *pParse, const char *zSql, int seed_token)
{
	int i;			/* Loop counter */
	void *pEngine;		/* The LEMON-generated LALR(1) parser */
	int tokenType;		/* type of the next token */
	int lastTokenParsed = -1;	/* type of the previous token */

	assert(zSql != 0);
	pParse->zTail = zSql;
	i = 0;
	/* sqlParserTrace(stdout, "parser: "); */
	pEngine = sqlParserAlloc(new_xmalloc);
	assert(pParse->new_space == NULL);
	assert(pParse->parsed_ast.trigger == NULL);
	assert(pParse->nVar == 0);
	assert(pParse->pVList == 0);
	struct Token last;
	memset(&last, 0, sizeof(last));
	if (seed_token != 0) {
		last.z = zSql;
		sqlParser(pEngine, seed_token, last, pParse);
	}
	while (1) {
		assert(i >= 0);
		if (zSql[i] != 0) {
			last.z = &zSql[i];
			last.n = sql_token(&zSql[i], &tokenType,
					   &last.isReserved);
			i += last.n;
			if (i > SQL_MAX_SQL_LENGTH) {
				diag_set(ClientError, ER_SQL_PARSER_LIMIT,
					 "SQL command length", i,
					 SQL_MAX_SQL_LENGTH);
				pParse->is_aborted = true;
				break;
			}
		} else {
			/* Upon reaching the end of input, call the parser two more times
			 * with tokens TK_SEMI and 0, in that order.
			 */
			if (lastTokenParsed == TK_SEMI) {
				tokenType = 0;
			} else if (lastTokenParsed == 0) {
				break;
			} else {
				tokenType = TK_SEMI;
			}
		}
		if (tokenType >= TK_SPACE) {
			assert(tokenType == TK_SPACE
			       || tokenType == TK_ILLEGAL);
			if (tokenType == TK_ILLEGAL) {
				diag_set(ClientError, ER_SQL_UNKNOWN_TOKEN,
					 pParse->line_count, pParse->line_pos,
					 tt_cstr(last.z, last.n));
				pParse->is_aborted = true;
				break;
			}
		} else if (tokenType == TK_LINEFEED) {
			pParse->line_count++;
			pParse->line_pos = 1;
			continue;
		} else {
			sqlParser(pEngine, tokenType, last, pParse);
			lastTokenParsed = tokenType;
			if (pParse->is_aborted)
				break;
		}
		pParse->line_pos += last.n;
	}
	sql_code_ast(pParse, &pParse->ast, pParse->zTail);
	pParse->zTail = &zSql[i];
	sqlParserFree(pEngine, free);
	return pParse->is_aborted ? -1 : 0;
}

int
sql_parse_statement(struct Parse *parser, const char *sql)
{
	return sql_run_parser(parser, sql, 0);
}

struct Expr *
sql_parse_function(struct Parse *parser, const char *sql)
{
	if (sql_run_parser(parser, sql, TK_FUNCTION_ENTRY) != 0)
		return NULL;
	assert(parser->parsed_ast_type == AST_TYPE_EXPR);
	struct Expr *res = parser->parsed_ast.expr;
	parser->parsed_ast.expr = NULL;
	if (parser->nVar > 0) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 "Parameters are not allowed in functions");
		parser->is_aborted = true;
		sql_expr_delete(res);
		return NULL;
	}
	return res;
}

struct Select *
sql_parse_view(struct Parse *parser, const char *sql)
{
	if (sql_run_parser(parser, sql, TK_VIEW_ENTRY) != 0)
		return NULL;
	struct sql_ast *ast = &parser->ast;
	assert(ast->type == SQL_AST_VIEW);
	struct Select *res = select_from_ast(parser, ast->select);
	if (res != NULL && parser->nVar > 0) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 "Parameters are not allowed in views");
		parser->is_aborted = true;
		sql_select_delete(res);
		return NULL;
	}
	return res;
}

struct sql_trigger *
sql_parse_trigger(struct Parse *parser, const char *sql)
{
	if (sql_run_parser(parser, sql, TK_TRIGGER_ENTRY) != 0)
		return NULL;
	assert(parser->parsed_ast_type == AST_TYPE_TRIGGER);
	struct sql_trigger *res = parser->parsed_ast.trigger;
	parser->parsed_ast.trigger = NULL;
	if (parser->nVar > 0) {
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 "Parameters are not allowed in triggers");
		parser->is_aborted = true;
		sql_trigger_delete(res);
		return NULL;
	}
	return res;
}
