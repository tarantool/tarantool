#ifndef TNT_LEX_H_INCLUDED
#define TNT_LEX_H_INCLUDED

/*
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

/* token id's */

enum {
	TNT_TK_ERROR = -1,
	TNT_TK_EOF = 0,
	TNT_TK_NONE = 1000,
	TNT_TK_NUM32,
	TNT_TK_NUM64,
	TNT_TK_ID,
	TNT_TK_KEY,
	TNT_TK_TABLE,
	TNT_TK_PUNCT,
	TNT_TK_STRING,
	TNT_TK_PING,
	TNT_TK_UPDATE,
	TNT_TK_SET,
	TNT_TK_WHERE,
	TNT_TK_SPLICE,
	TNT_TK_DELETE,
	TNT_TK_FROM,
	TNT_TK_INSERT,
	TNT_TK_REPLACE,
	TNT_TK_INTO,
	TNT_TK_VALUES,
	TNT_TK_SELECT,
	TNT_TK_LIMIT,
	TNT_TK_CALL,
	TNT_TK_OR,
	TNT_TK_AND,
	TNT_TK_CUSTOM = 2000
};

/* keyword descriptor */

struct tnt_lex_keyword {
	char *name;
	int size;
	int tk;
};

/* token object */

struct tnt_tk {
	int tk;
	union {
		int32_t i32;
		int64_t i64;
		struct tnt_utf8 s;
	} v;
	int line, col;
	SLIST_ENTRY(tnt_tk) next;
	STAILQ_ENTRY(tnt_tk) nextq;
};

/* token object accessors */

#define TNT_TK_S(TK) (&(TK)->v.s)
#define TNT_TK_I32(TK) ((TK)->v.i32)
#define TNT_TK_I64(TK) ((TK)->v.i64)

/* lexer object */

struct tnt_lex {
	struct tnt_utf8 buf;
	struct tnt_lex_keyword *keywords;
	size_t pos;
	int line, col;
	int count;
	SLIST_HEAD(,tnt_tk) stack;
	int countq;
	STAILQ_HEAD(,tnt_tk) q;
	bool idonly;
	char *error;
};

bool tnt_lex_init(struct tnt_lex *l, struct tnt_lex_keyword *keywords,
		  unsigned char *buf, size_t size);
void tnt_lex_free(struct tnt_lex *l);

char *tnt_lex_nameof(struct tnt_lex *l, int tk);
void tnt_lex_idonly(struct tnt_lex *l, bool on);

void tnt_lex_push(struct tnt_lex *l, struct tnt_tk *tk);
int tnt_lex(struct tnt_lex *l, struct tnt_tk **tk);

#endif /* TNT_LEX_H_INCLUDED */
