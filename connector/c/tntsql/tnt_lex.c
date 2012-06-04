
/*
 * Copyright (C) 2011 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_utf8.h>
#include <connector/c/include/tarantool/tnt_queue.h>
#include <connector/c/include/tarantool/tnt_lex.h>

bool tnt_lex_init(struct tnt_lex *l, unsigned char *buf, size_t size)
{
	if (!tnt_utf8_init(&l->buf, buf, size))
		return false;
	l->pos = 0;
	l->col = 1;
	l->line = 1;
	l->count = 0;
	l->countq = 0;
	SLIST_INIT(&l->stack);
	STAILQ_INIT(&l->q);
	l->error = NULL;
	l->idonly = false;
	return true;
}

void tnt_lex_free(struct tnt_lex *l)
{
	struct tnt_tk *tk, *tkn;
	STAILQ_FOREACH_SAFE(tk, &l->q, nextq, tkn) {
		if (tk->tk == TNT_TK_STRING || tk->tk == TNT_TK_ID)
			tnt_utf8_free(TNT_TK_S(tk));
		tnt_mem_free(tk);
	}
	tnt_utf8_free(&l->buf);
	if (l->error)
		tnt_mem_free(l->error);
}

void tnt_lex_push(struct tnt_lex *l, struct tnt_tk *tk)
{
	SLIST_INSERT_HEAD(&l->stack, tk, next);
	l->count++;
}

void
tnt_lex_idonly(struct tnt_lex *l, bool on)
{
	l->idonly = on;
}

static struct tnt_tk*
tnt_lex_pop(struct tnt_lex *l)
{
	if (l->count == 0)
		return NULL;
	struct tnt_tk *tk = SLIST_FIRST(&l->stack);
	SLIST_REMOVE_HEAD(&l->stack, next);
	l->count--;
	return tk;
}

static struct tnt_tk*
tnt_lex_tk(struct tnt_lex *l, int tk, int line, int col) {
	struct tnt_tk *t = tnt_mem_alloc(sizeof(struct tnt_tk));
	memset(t, 0, sizeof(struct tnt_tk));
	t->tk = tk;
	t->line = line;
	t->col = col;
	STAILQ_INSERT_TAIL(&l->q, t, nextq);
	l->countq++;
	return t;
}

static int
tnt_lex_error(struct tnt_lex *l, char *fmt, ...) {
	if (fmt == NULL)
		return TNT_TK_EOF;
	if (l->error)
		tnt_mem_free(l->error);
	char msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	l->error = tnt_mem_dup(msg);
	return TNT_TK_ERROR;
}

inline static ssize_t
tnt_lex_next(struct tnt_lex *l) {
	ssize_t r = tnt_utf8_next(&l->buf, l->pos);
	if (r > 0) {
		l->pos = r;
		l->col++;
	}
	return r;
}

/* keywords */

static struct {
	char *name;
	int size;
	int tk;
} tnt_keywords[] =
{
	{  "PING",    4, TNT_TK_PING },
	{  "UPDATE",  6, TNT_TK_UPDATE },
	{  "SET",     3, TNT_TK_SET },
	{  "WHERE",   5, TNT_TK_WHERE },
	{  "SPLICE",  6, TNT_TK_SPLICE },
	{  "DELETE",  6, TNT_TK_DELETE },
	{  "FROM",    4, TNT_TK_FROM },
	{  "INSERT",  6, TNT_TK_INSERT },
	{  "REPLACE", 7, TNT_TK_REPLACE },
	{  "INTO",    4, TNT_TK_INTO },
	{  "VALUES",  6, TNT_TK_VALUES },
	{  "SELECT",  6, TNT_TK_SELECT },
	{  "OR",      2, TNT_TK_OR },
	{  "AND",     3, TNT_TK_AND },
	{  "LIMIT",   5, TNT_TK_LIMIT },
	{  "CALL",    4, TNT_TK_CALL },
	{  NULL,      0, TNT_TK_NONE }
};

char*
tnt_lex_nameof(int tk)
{
	/* system tokens */
	switch (tk) {
	case TNT_TK_EOF: return "End-Of-Statement";
	case TNT_TK_ERROR: return "ERROR";
	case TNT_TK_NUM: return "NUM";
	case TNT_TK_STRING: return "STRING";
	case TNT_TK_ID: return "ID";
	case TNT_TK_KEY: return "KEY";
	case TNT_TK_TABLE: return "TABLE";
	case TNT_TK_PUNCT: return "PUNCT";
	}
	/* matching keyword */
	int i;
	for (i = 0 ; tnt_keywords[i].name ; i++)
		if (tnt_keywords[i].tk == tk)
			return tnt_keywords[i].name;
	return NULL;
}

#define tnt_lex_step(l) \
	do { \
		ssize_t r = tnt_lex_next(l); \
		if (r == -1) \
			return tnt_lex_error(l, "utf8 decoding error"); \
	} while (0)

#define tnt_lex_try(l, reason) \
	do { \
		ssize_t r = tnt_lex_next(l); \
		if (r == -1) \
			return tnt_lex_error(l, "utf8 decoding error"); \
		else \
		if (r == 0) \
			return tnt_lex_error(l, reason); \
	} while (0)

#define tnt_lex_chr(l) (*TNT_UTF8_CHAR(&l->buf, l->pos))

int
tnt_lex(struct tnt_lex *l, struct tnt_tk **tk)
{
	/* trying stack first */
	if (l->count) {
		*tk = tnt_lex_pop(l);
		if ((*tk)->tk == TNT_TK_PUNCT)
			return TNT_TK_I(*tk);
		return (*tk)->tk;
	}
	
	/* skipping spaces and comments */
	unsigned char ch;
	while (1) {
		if (l->pos == l->buf.size) {
			*tk = tnt_lex_tk(l, TNT_TK_EOF, l->line, l->col);
			return TNT_TK_EOF;
		}
		ch = tnt_lex_chr(l);
		if (isspace(ch)) {
			if (ch == '\n') {
				if (((l->pos + 1) != l->buf.size))
					l->line++;
				l->col = 0;
			}
			tnt_lex_step(l);
			continue;
		} else
		if (ch == '#') {
			while (1) {
				tnt_lex_step(l);
				if (tnt_lex_chr(l) == '\n') {
					if (((l->pos + 1) != l->buf.size))
						l->line++;
					l->col = 0;
					tnt_lex_step(l);
					break;
				}
			}
			continue;
		}
		break;
	}

	/* saving lexem position */
	int line = l->line;
	int col = l->col;
	ssize_t start = l->pos, size = 0;
	ch = tnt_lex_chr(l);

	/* string */
	if (ch == '\'') {
		start++;
		while (1) {
			tnt_lex_try(l, "bad string definition");
			ch = tnt_lex_chr(l);
			if (ch == '\'')
				break;
			if (ch == '\n')
				return tnt_lex_error(l, "bad string definition");
		}
		size = l->pos - start;
		tnt_lex_step(l);
		*tk = tnt_lex_tk(l, TNT_TK_STRING, line, col);
		if (size > 0)
			tnt_utf8_init(TNT_TK_S(*tk), TNT_UTF8_CHAR(&l->buf, start), size);
		return TNT_TK_STRING;
	}

	bool minus = false;
	/* punctuation */
	if (ispunct(ch) && ch != '_') {
		tnt_lex_step(l);
		if (ch == '-') {
			ch = tnt_lex_chr(l);
			if (isdigit(ch)) {
				minus = true;
				goto numeric;
			}
		}
		*tk = tnt_lex_tk(l, TNT_TK_PUNCT, line, col);
		TNT_TK_I(*tk) = ch;
		return ch;
	}

numeric: /* numeric value */
	if (isdigit(ch)) {
		int32_t num = 0;
		while (1) {
			if (isdigit(tnt_lex_chr(l)))
				num *= 10, num += tnt_lex_chr(l) - '0';
			else
				break;
			ssize_t r = tnt_lex_next(l);
			if (r == -1)
				return tnt_lex_error(l, "utf8 decoding error");
			if (r == 0)
				break;
		}
		if (minus)
			num *= -1;
		*tk = tnt_lex_tk(l, TNT_TK_NUM, line, col);
		TNT_TK_I(*tk) = num;
		return TNT_TK_NUM;
	}

	/* skipping to the end of lexem */
	while (1) {
		ch = tnt_lex_chr(l);
		if (isspace(ch) || (ispunct(ch) && ch != '_'))
			break;
		ssize_t r = tnt_lex_next(l);
		if (r == -1)
			return tnt_lex_error(l, "utf8 decoding error");
		else
		if (r == 0)
			break;
	}
	size = l->pos - start;

	/* handle to tell lexer that table's, key's and keyword's are id's */
	if (l->idonly)
		goto id;

	/* matching keyword */
	int i;
	for (i = 0 ; tnt_keywords[i].name ; i++) {
		if (tnt_keywords[i].size != size)
			continue;
		if (strncasecmp(tnt_keywords[i].name, (const char*)TNT_UTF8_CHAR(&l->buf, start), size) == 0) {
			*tk = tnt_lex_tk(l, tnt_keywords[i].tk, line, col);
			return tnt_keywords[i].tk;
		}
	}

	/* table or key id */
	ch = *TNT_UTF8_CHAR(&l->buf, start);
	if ((ch == 't' || ch == 'k') && size >= 2) {
		int idtk = (ch == 't') ? TNT_TK_TABLE : TNT_TK_KEY;
		int32_t id = 0;
		for (i = 1 ; i < size ; i++) {
			ch = *TNT_UTF8_CHAR(&l->buf, start + i);
			if (isdigit(ch))
				id *= 10, id += ch - '0';
			else
				goto id;
		}
		*tk = tnt_lex_tk(l, idtk, line, col);
		TNT_TK_I(*tk) = id;
		return idtk;
	}

id:	/* assuming id */
	*tk = tnt_lex_tk(l, TNT_TK_ID, line, col);
	tnt_utf8_init(TNT_TK_S(*tk), TNT_UTF8_CHAR(&l->buf, start), size);
	return TNT_TK_ID;
}

#undef tnt_lex_step
#undef tnt_lex_try
#undef tnt_lex_chr
