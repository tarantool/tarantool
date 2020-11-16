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

#include "box/session.h"
#include "box/schema.h"
#include "say.h"
#include "sqlInt.h"
#include "tarantoolInt.h"

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
#define CC_DOLLAR     4		/* '$' */
#define CC_VARALPHA   5		/* '@', '#', ':'.  Alphabetic SQL variables */
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

static const char sql_ascii_class[] = {
/*       x0  x1  x2  x3  x4  x5  x6  x7  x8 x9  xa xb  xc xd xe  xf */
/* 0x */ 27, 27, 27, 27, 27, 27, 27, 27, 27, 7, 28, 7, 7, 7, 27, 27,
/* 1x */ 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
/* 2x */ 7, 15, 9, 5, 4, 22, 24, 8, 17, 18, 21, 20, 23, 11, 26, 16,
/* 3x */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 5, 19, 12, 14, 13, 6,
/* 4x */ 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 5x */ 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 27, 27, 27, 27, 1,
/* 6x */ 27, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 7x */ 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 27, 10, 27, 25, 27,
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
	int i, n;
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
			*type = TK_FLOAT;
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
		*type = TK_VARIABLE;
		for (i = 1; sqlIsdigit(z[i]); i++) {
		}
		return i;
	case CC_DOLLAR:
	case CC_VARALPHA:
		n = 0;
		*type = TK_VARIABLE;
		for (i = 1; (c = z[i]) != 0; i++) {
			if (IdChar(c))
				n++;
			else
				break;
		}
		if (n == 0)
			*type = TK_ILLEGAL;
		return i;
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

/**
 * This function is called to release parsing artifacts
 * during table creation or column addition. The only objects
 * allocated using malloc are index defs.
 * Note that this functions can't be called on ordinary
 * space object. It's purpose is to clean-up parser->new_space.
 *
 * @param db Database handler.
 * @param space Space to be deleted.
 */
static void
parser_space_delete(struct sql *db, struct space *space)
{
	if (space == NULL || db == NULL)
		return;
	assert(space->def->opts.is_ephemeral);
	struct space *altered_space = space_by_name(space->def->name);
	uint32_t i = 0;
	/*
	 * Don't delete already existing defs and start from new
	 * ones.
	 */
	if (altered_space != NULL)
		i = altered_space->index_count;
	for (; i < space->index_count; ++i)
		index_def_delete(space->index[i]->def);
}

/**
 * Run the parser on the given SQL string.
 *
 * @param pParse Parser context.
 * @param zSql SQL string.
 * @retval 0 on success.
 * @retval -1 on error.
 */
int
sqlRunParser(Parse * pParse, const char *zSql)
{
	int i;			/* Loop counter */
	void *pEngine;		/* The LEMON-generated LALR(1) parser */
	int tokenType;		/* type of the next token */
	int lastTokenParsed = -1;	/* type of the previous token */
	sql *db = pParse->db;	/* The database connection */
	int mxSqlLen;		/* Max length of an SQL string */

	assert(zSql != 0);
	mxSqlLen = db->aLimit[SQL_LIMIT_SQL_LENGTH];
	pParse->zTail = zSql;
	i = 0;
	/* sqlParserTrace(stdout, "parser: "); */
	pEngine = sqlParserAlloc(sqlMalloc);
	if (pEngine == 0) {
		sqlOomFault(db);
		return -1;
	}
	assert(pParse->create_table_def.new_space == NULL);
	assert(pParse->parsed_ast.trigger == NULL);
	assert(pParse->nVar == 0);
	assert(pParse->pVList == 0);
	while (1) {
		assert(i >= 0);
		if (zSql[i] != 0) {
			pParse->sLastToken.z = &zSql[i];
			pParse->sLastToken.n =
			    sql_token(&zSql[i], &tokenType,
				      &pParse->sLastToken.isReserved);
			i += pParse->sLastToken.n;
			if (i > mxSqlLen) {
				diag_set(ClientError, ER_SQL_PARSER_LIMIT,
					 "SQL command length", i, mxSqlLen);
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
					 pParse->sLastToken.n,
					 pParse->sLastToken.z);
				pParse->is_aborted = true;
				break;
			}
		} else if (tokenType == TK_LINEFEED) {
			pParse->line_count++;
			pParse->line_pos = 1;
			continue;
		} else {
			sqlParser(pEngine, tokenType, pParse->sLastToken,
				      pParse);
			lastTokenParsed = tokenType;
			if (pParse->is_aborted || db->mallocFailed)
				break;
		}
		pParse->line_pos += pParse->sLastToken.n;
	}
	pParse->zTail = &zSql[i];
	sqlParserFree(pEngine, sql_free);
	if (db->mallocFailed)
		pParse->is_aborted = true;
	if (pParse->pVdbe != NULL && pParse->is_aborted) {
		sqlVdbeDelete(pParse->pVdbe);
		pParse->pVdbe = 0;
	}
	parser_space_delete(db, pParse->create_column_def.space);

	if (pParse->pWithToFree)
		sqlWithDelete(db, pParse->pWithToFree);
	sqlDbFree(db, pParse->pVList);
	return pParse->is_aborted ? -1 : 0;
}

struct Expr *
sql_expr_compile(sql *db, const char *expr, int expr_len)
{
	const char *outer = "SELECT ";
	int len = strlen(outer) + expr_len;

	struct Parse parser;
	sql_parser_create(&parser, db, default_flags);
	/*
	 * Since SELECT token is added to the original expression,
	 * to make error message display correct position we should
	 * account its length.
	 */
	parser.line_pos -= strlen(outer);
	parser.parse_only = true;

	struct Expr *expression = NULL;
	char *stmt = (char *)region_alloc(&parser.region, len + 1);
	if (stmt == NULL) {
		diag_set(OutOfMemory, len + 1, "region_alloc", "stmt");
		goto end;
	}
	sprintf(stmt, "%s%.*s", outer, expr_len, expr);

	if (sqlRunParser(&parser, stmt) == 0 &&
	    parser.parsed_ast.ast_type == AST_TYPE_EXPR) {
		expression = parser.parsed_ast.expr;
		parser.parsed_ast.expr = NULL;
	}
end:
	sql_parser_destroy(&parser);
	return expression;
}

struct Select *
sql_view_compile(struct sql *db, const char *view_stmt)
{
	struct Parse parser;
	sql_parser_create(&parser, db, default_flags);
	parser.parse_only = true;

	struct Select *select = NULL;

	if (sqlRunParser(&parser, view_stmt) != 0 ||
	    parser.parsed_ast.ast_type != AST_TYPE_SELECT) {
		diag_set(ClientError, ER_SQL_EXECUTE, view_stmt);
	} else {
		select = parser.parsed_ast.select;
		parser.parsed_ast.select = NULL;
	}

	sql_parser_destroy(&parser);
	return select;
}

struct sql_trigger *
sql_trigger_compile(struct sql *db, const char *sql)
{
	struct Parse parser;
	sql_parser_create(&parser, db, default_flags);
	parser.parse_only = true;
	struct sql_trigger *trigger = NULL;
	if (sqlRunParser(&parser, sql) == 0 &&
	    parser.parsed_ast.ast_type == AST_TYPE_TRIGGER) {
		trigger = parser.parsed_ast.trigger;
		parser.parsed_ast.trigger = NULL;
	}

	sql_parser_destroy(&parser);
	return trigger;
}
