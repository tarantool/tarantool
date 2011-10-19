
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <tnt.h>
#include <connector/c/sql/tnt_utf8.h>
#include <connector/c/sql/tnt_lex.h>

/** sql parsing context. */
struct tnt_sql {
	struct tnt *t;
	struct tnt_lex *l;
	int ops;
	char *error;
};

static bool
tnt_sql_error(struct tnt_sql *sql, struct tnt_tk *last, char *fmt, ...)
{
	char msgu[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msgu, sizeof(msgu), fmt, args);
	va_end(args);
	int line = (last) ? last->line : sql->l->line;
	int col = (last) ? last->col : sql->l->col;
	char msg[256];
	snprintf(msg, sizeof(msg), "%d:%d %s\n", line, col, msgu);
	if (sql->error == NULL)
		sql->error = tnt_mem_dup(msg);
	return false;
}

/** token validating routines. */
static bool
tnt_sql_tk(struct tnt_sql *sql, int tk, struct tnt_tk **tkp)
{
	struct tnt_tk *tkp_ = NULL;
	int tk_ = tnt_lex(sql->l, &tkp_);
	if (tk_ == TNT_TK_ERROR)
		return tnt_sql_error(sql, NULL, "%s", sql->l->error);
	if (tk_ != tk) {
		if (tk < 0xff && ispunct(tk))
			return tnt_sql_error(sql, tkp_, "expected '%c'", tk);
		return tnt_sql_error(sql, tkp_, "expected '%s'", tnt_lex_nameof(tk));
	}
	if (tkp)
		*tkp = tkp_;
	return true;
}
inline static bool 
tnt_sqltk(struct tnt_sql *sql,int tk) {
	return tnt_sql_tk(sql, tk, NULL);
}
inline static bool
tnt_sqltkv(struct tnt_sql *sql, int tk, struct tnt_tk **tkp) {
	return tnt_sql_tk(sql, tk, tkp);
}

static bool
tnt_sql_try(struct tnt_sql *sql, int tk, struct tnt_tk **tkp) {
	struct tnt_tk *tkp_ = NULL;
	int tk_ = tnt_lex(sql->l, &tkp_);
	if (tk_ == TNT_TK_ERROR) 
		return tnt_sql_error(sql, NULL, "%s", sql->l->error);
	if (tk_ != tk) {
		tnt_lex_push(sql->l, tkp_);
		return false;
	}
	if (tkp)
		*tkp = tkp_;
	return true;
}
inline static int
tnt_sqltry(struct tnt_sql *sql, int tk) {
	return tnt_sql_try(sql, tk, NULL);
}
inline static int
tnt_sqltryv(struct tnt_sql *sql, int tk, struct tnt_tk **tkp) {
	return tnt_sql_try(sql, tk, tkp);
}

/** key-value parsing for tuple operation. */
static bool
tnt_sql_kv(struct tnt_sql *sql, struct tnt_tuple *tu, bool key)
{
	/* key */
	struct tnt_tk *k = NULL;
	if (key && (!tnt_sqltkv(sql, TNT_TK_KEY, &k) || !tnt_sqltk(sql, '=')))
		return false;
	/* value */
	struct tnt_tk *v = NULL;
	if (tnt_lex(sql->l, &v) == TNT_TK_ERROR)
		return tnt_sql_error(sql, NULL, "%s", sql->l->error);
	if (v->tk != TNT_TK_NUM && v->tk != TNT_TK_STRING)
		return tnt_sql_error(sql, k, "expected NUM or STRING");
	/* tuple field operation */
	if (v->tk == TNT_TK_NUM)
		tnt_tuple_add(tu, (char*)&TNT_TK_I(v), 4);
	else
		tnt_tuple_add(tu, (char*)TNT_TK_S(v)->data, TNT_TK_S(v)->size);
	return true;
}

#define tnt_expect(a) \
	do { if (!(a)) goto error; } while (0)

/** parsing update statement. */
static bool
tnt_sql_stmt_update(struct tnt_sql *sql, struct tnt_tuple *tu, struct tnt_update *u)
{
	/* UPDATE TABLE SET operations WHERE predicate */
	bool rc = false;
	struct tnt_tk *tn = NULL;
	tnt_expect(tnt_sqltkv(sql, TNT_TK_TABLE, &tn));
	tnt_expect(tnt_sqltk(sql, TNT_TK_SET));
	while (1) {
		struct tnt_tk *k;
		tnt_expect(tnt_sqltkv(sql, TNT_TK_KEY, &k));
		tnt_expect(tnt_sqltk(sql, '='));
		struct tnt_tk *v;
		switch (tnt_lex(sql->l, &v)) {
		/* k = k op v */
		case TNT_TK_KEY:
			if (TNT_TK_I(k) != TNT_TK_I(v)) {
				tnt_sql_error(sql, k, "can't update on different keys");
				goto error;
			}
			enum tnt_update_type ut;
			switch (tnt_lex(sql->l, &v)) {
			case TNT_TK_ERROR:
				tnt_sql_error(sql, k, "%s", sql->l->error);
				goto error;
			case '+': ut = TNT_UPDATE_ADD;
				break;
			case '&': ut = TNT_UPDATE_AND;
				break;
			case '^': ut = TNT_UPDATE_XOR;
				break;
			case '|': ut = TNT_UPDATE_OR;
				break;
			default:
				tnt_sql_error(sql, k, "bad update operation");
				goto error;
			}
			tnt_expect(tnt_sqltkv(sql, TNT_TK_NUM, &v));
			tnt_update_arith(u, TNT_TK_I(k), ut, TNT_TK_I(v));
			break;
		/* k = string */
		case TNT_TK_STRING:
			tnt_update_assign(u, TNT_TK_I(k), (char*)TNT_TK_S(v)->data,
					  TNT_TK_S(v)->size);
			break;
		/* k = num */
		case TNT_TK_NUM:
			tnt_update_assign(u, TNT_TK_I(k), (char*)&TNT_TK_I(v), 4);
			break;
		/* k = splice(k, a, b) */
		case TNT_TK_SPLICE: {
			struct tnt_tk *field, *off, *len, *list;
			tnt_expect(tnt_sqltk(sql, '('));
			tnt_expect(tnt_sqltkv(sql, TNT_TK_KEY, &field));
			if (TNT_TK_I(k) != TNT_TK_I(field)) {
				tnt_sql_error(sql, k, "can't update on different keys");
				goto error;
			}
			tnt_expect(tnt_sqltk(sql, ','));
			tnt_expect(tnt_sqltkv(sql, TNT_TK_NUM, &off));
			tnt_expect(tnt_sqltk(sql, ','));
			tnt_expect(tnt_sqltkv(sql, TNT_TK_NUM, &len));
			tnt_expect(tnt_sqltk(sql, ','));
			tnt_expect(tnt_sqltkv(sql, TNT_TK_STRING, &list));
			tnt_expect(tnt_sqltk(sql, ')'));
			tnt_update_splice(u, TNT_TK_I(k), TNT_TK_I(off), TNT_TK_I(len),
					  TNT_TK_S(list)->data,
					  TNT_TK_S(list)->size);
			break;
		}
		case TNT_TK_ERROR:
			tnt_sql_error(sql, k, "%s", sql->l->error);
			goto error;
		}
		if (tnt_sqltry(sql, ','))
			continue;
		if (sql->error)
			goto error;
		break;
	}
	tnt_expect(tnt_sqltk(sql, TNT_TK_WHERE));
	/* predicate */
	tnt_expect(tnt_sql_kv(sql, tu, true));
	if (tnt_update_tuple(sql->t, 0, TNT_TK_I(tn), 0, tu, u) == -1) {
		tnt_sql_error(sql, tn, "update failed: %s",
			      tnt_strerror(sql->t)); 
		goto error;
	}
	rc = true;
error:
	return rc;
}

/** parsing single sql statement. */
static bool
tnt_sql_stmt(struct tnt_sql *sql)
{
	struct tnt_tuple tu;
	struct tnt_tuples tus;
	struct tnt_update u;
	tnt_tuple_init(&tu);
	tnt_tuples_init(&tus);
	tnt_update_init(&u);

	struct tnt_tk *tk = NULL, *tn = NULL;
	bool rc = false;
	switch (tnt_lex(sql->l, &tk)) {
	/* INSERT [INTO] TABLE VALUES ( list ) */
	case TNT_TK_INSERT:
		tnt_sqltry(sql, TNT_TK_INTO);
		if (sql->error)
			goto error;
		tnt_expect(tnt_sqltkv(sql, TNT_TK_TABLE, &tn));
		tnt_expect(tnt_sqltk(sql, TNT_TK_VALUES));
		tnt_expect(tnt_sqltk(sql, '('));
		while (1) {
			tnt_expect(tnt_sql_kv(sql, &tu, false));
			if (tnt_sqltry(sql, ','))
				continue;
			if (sql->error)
				goto error;
			break;
		}
		tnt_expect(tnt_sqltk(sql, ')'));
		if (tnt_insert(sql->t, 0, TNT_TK_I(tn), 0, &tu) == -1) {
			tnt_sql_error(sql, tk, "insert failed: %s",
				      tnt_strerror(sql->t)); 
			goto error;
		}
		sql->ops++;
		break;
	/* UPDATE TABLE SET operations WHERE predicate */
	case TNT_TK_UPDATE:
		if (!tnt_sql_stmt_update(sql, &tu, &u))
			goto error;
		sql->ops++;
		break;
	/* DELETE FROM TABLE WHERE predicate */
	case TNT_TK_DELETE:
		tnt_expect(tnt_sqltk(sql, TNT_TK_FROM));
		tnt_expect(tnt_sqltkv(sql, TNT_TK_TABLE, &tn));
		tnt_expect(tnt_sqltk(sql, TNT_TK_WHERE));
		/* predicate */
		tnt_expect(tnt_sql_kv(sql, &tu, true));
		if (tnt_delete_tuple(sql->t, 0, TNT_TK_I(tn), &tu) == -1) {
			tnt_sql_error(sql, tk, "delete failed: %s",
				      tnt_strerror(sql->t)); 
			goto error;
		}
		sql->ops++;
		break;
	/* SELECT * FROM TABLE WHERE predicate OR predicate... */
	case TNT_TK_SELECT:
		tnt_expect(tnt_sqltk(sql, '*'));
		tnt_expect(tnt_sqltk(sql, TNT_TK_FROM));
		tnt_expect(tnt_sqltkv(sql, TNT_TK_TABLE, &tn));
		tnt_expect(tnt_sqltk(sql, TNT_TK_WHERE));
		while (1) {
			struct tnt_tuple *tup = tnt_tuples_add(&tus);
			tnt_expect(tnt_sql_kv(sql, tup, true));
			if (tnt_sqltry(sql, TNT_TK_OR))
				continue;
			if (sql->error)
				goto error;
			break;
		}
		if (tnt_select(sql->t, 0, TNT_TK_I(tn), 0, 0, 1000, &tus) == -1) {
			tnt_sql_error(sql, tk, "select failed: %s",
				      tnt_strerror(sql->t)); 
			goto error;
		}
		sql->ops++;
		break;
	/* CALL NAME[{.NAME}+](STRING [{,STRING}+]) */
	case TNT_TK_CALL: {
		char proc[512];
		int len = 0;
		while (1) {
			struct tnt_tk *name;
			tnt_lex_idonly(sql->l, true);
			tnt_expect(tnt_sqltkv(sql, TNT_TK_ID, &name));
			tnt_lex_idonly(sql->l, false);
			len += snprintf(proc + len, sizeof(proc) - len, "%.*s",
				        TNT_TK_S(name)->size, TNT_TK_S(name)->data);
			if (!tnt_sqltry(sql, '.'))
				break;
			if (sql->error)
				goto error;
			len += snprintf(proc + len, sizeof(proc) - len, "%s", ".");
		}
		tnt_expect(tnt_sqltk(sql, '('));
		if (tnt_sqltry(sql, ')'))
			goto noargs;
		if (sql->error)
			goto error;
		int argc = 0;
		int argc_max = 16;
		char **argv = tnt_mem_alloc(sizeof(char*) * argc_max);
		while (1) {
			if (argc == argc_max) {
				argc_max += argc_max;
				argv = tnt_mem_realloc(argv, sizeof(char*) * argc_max);
			}
			struct tnt_tk *arg;
			tnt_expect(tnt_sqltkv(sql, TNT_TK_STRING, &arg));
			argv[argc] = TNT_TK_S(arg)->data;
			argc++;
			if (tnt_sqltry(sql, ','))
				continue;
			if (sql->error)
				goto error;
			break;
		}
		tnt_expect(tnt_sqltk(sql, ')'));
noargs:
		if (tnt_call(sql->t, 0, 0, proc, argc, argv) == -1) {
			tnt_sql_error(sql, tk, "call failed: %s", tnt_strerror(sql->t)); 
			tnt_mem_free(argv);
			goto error;
		}
		tnt_mem_free(argv);
		sql->ops++;
		break;
	}
	/* PING */
	case TNT_TK_PING:
		if (tnt_ping(sql->t, 0) == -1) {
			tnt_sql_error(sql, tk, "ping failed: %s",
				      tnt_strerror(sql->t)); 
			goto error;
		}
		sql->ops++;
		break;
	case TNT_TK_ERROR:
		return tnt_sql_error(sql, tk, "%s", sql->l->error);
	default:
		return tnt_sql_error(sql, tk,
			"insert, update, delete, select, call, ping are expected");
	}
	rc = true;
error:
	tnt_tuple_free(&tu);
	tnt_tuples_free(&tus);
	tnt_update_free(&u);
	return rc;
}

#undef tnt_expect

/** sql grammar parsing function. */
static bool
tnt_sql(struct tnt_sql *sql)
{
	struct tnt_tk *tk;
	while (1) {
		switch (tnt_lex(sql->l, &tk)) {
		case TNT_TK_ERROR:
			return tnt_sql_error(sql, NULL, "%s", sql->l->error);
		case TNT_TK_EOF:
			return true;
		default:
			tnt_lex_push(sql->l, tk);
			if (!tnt_sql_stmt(sql))
				return false;
			if (tnt_sqltry(sql, ';'))
				continue;
			if (sql->error)
				return false;
			break;
		}
	}
}

int
tnt_query(struct tnt *t, char *q, size_t qsize, char **e)
{
	struct tnt_lex l;
	if (!tnt_lex_init(&l, (unsigned char*)q, qsize))
		return -1;
	struct tnt_sql sql = { t, &l, 0, NULL};
	bool ret = tnt_sql(&sql);
	if (e) {
		*e = sql.error;
	} else {
		if (sql.error)
			tnt_mem_free(sql.error);
	}
	tnt_lex_free(&l);
	return (ret) ? sql.ops : -1;
}

int
tnt_query_is(char *q, size_t qsize)
{
	struct tnt_lex l;
	if (!tnt_lex_init(&l, (unsigned char*)q, qsize))
		return 0;
	int rc = 0;
	struct tnt_tk *tk;
	switch (tnt_lex(&l, &tk)) {
	case TNT_TK_ERROR:
	case TNT_TK_EOF:
		break;
	default:
		if (tk->tk == TNT_TK_PING ||
		    tk->tk == TNT_TK_INSERT ||
		    tk->tk == TNT_TK_UPDATE ||
		    tk->tk == TNT_TK_SELECT ||
		    tk->tk == TNT_TK_DELETE ||
		    tk->tk == TNT_TK_CALL)
			rc = 1;
		break;
	}
	tnt_lex_free(&l);
	return rc;
}
