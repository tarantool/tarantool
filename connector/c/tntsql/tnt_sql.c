
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

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_queue.h>
#include <connector/c/include/tarantool/tnt_utf8.h>
#include <connector/c/include/tarantool/tnt_lex.h>

/* sql parsing context. */

struct tnt_sql {
	struct tnt_stream *s;
	struct tnt_lex *l;
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

/* token validating routines. */

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

/* key-value parsing for tuple operation. */

static bool
tnt_sql_keyval(struct tnt_sql *sql, struct tnt_tuple *tu, bool key, struct tnt_tk **kt)
{
	/* key */
	struct tnt_tk *k = NULL;
	if (key && (!tnt_sqltkv(sql, TNT_TK_KEY, &k) || !tnt_sqltk(sql, '=')))
		return false;
	if (kt)
		*kt = k;
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

static bool
tnt_sql_kv(struct tnt_sql *sql, struct tnt_tuple *tu, bool key) {
	return tnt_sql_keyval(sql, tu, key, NULL);
}

static bool
tnt_sql_kv_select(struct tnt_sql *sql, struct tnt_tuple *tu, int32_t *index)
{
	struct tnt_tk *key = NULL;
	bool rc = tnt_sql_keyval(sql, tu, true, &key);
	if (rc == false)
		return false;
	if (*index == -1)
		*index = TNT_TK_I(key);
	else
	if (*index != TNT_TK_I(key))
		return tnt_sql_error(sql, key,
				     "select key values must refer to the same index");
	return true;
}

#define tnt_expect(a) \
	do { if (!(a)) goto error; } while (0)

/* parsing update statement. */

static bool
tnt_sql_stmt_update(struct tnt_sql *sql, struct tnt_tuple *tu, struct tnt_stream *u)
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
			int ut;
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
					  (char*)TNT_TK_S(list)->data,
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
	tnt_expect(tnt_sqltk(sql, TNT_TK_EOF));
	if (tnt_update(sql->s, TNT_TK_I(tn), 0, tu, u) == -1) {
		tnt_sql_error(sql, tn, "update failed");
		goto error;
	}
	rc = true;
error:
	return rc;
}

/* parsing single sql statement. */

static bool
tnt_sql_stmt(struct tnt_sql *sql)
{
	struct tnt_tuple tu;
	struct tnt_list tuples;
	struct tnt_stream update;
	tnt_tuple_init(&tu);
	tnt_list_init(&tuples);
	tnt_buf(&update);

	int flags = 0;
	struct tnt_tk *tk = NULL, *tn = NULL;
	bool rc = false;
	switch (tnt_lex(sql->l, &tk)) {
	/* <INSERT|REPLACE> [INTO] TABLE VALUES ( list ) */
	case TNT_TK_INSERT:
	case TNT_TK_REPLACE:
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
		flags = TNT_FLAG_ADD;
		if (tk->tk == TNT_TK_REPLACE)
			flags = TNT_FLAG_REPLACE;
		tnt_expect(tnt_sqltk(sql, ')'));
		tnt_expect(tnt_sqltk(sql, TNT_TK_EOF));
		if (tnt_insert(sql->s, TNT_TK_I(tn), flags, &tu) == -1) {
			tnt_sql_error(sql, tk, "insert failed");
			goto error;
		}
		break;
	/* UPDATE TABLE SET operations WHERE predicate */
	case TNT_TK_UPDATE:
		if (!tnt_sql_stmt_update(sql, &tu, &update))
			goto error;
		break;
	/* DELETE FROM TABLE WHERE predicate */
	case TNT_TK_DELETE:
		tnt_expect(tnt_sqltk(sql, TNT_TK_FROM));
		tnt_expect(tnt_sqltkv(sql, TNT_TK_TABLE, &tn));
		tnt_expect(tnt_sqltk(sql, TNT_TK_WHERE));
		/* predicate */
		tnt_expect(tnt_sql_kv(sql, &tu, true));
		tnt_expect(tnt_sqltk(sql, TNT_TK_EOF));
		if (tnt_delete(sql->s, TNT_TK_I(tn), 0, &tu) == -1) {
			tnt_sql_error(sql, tk, "delete failed"); 
			goto error;
		}
		break;
	/* SELECT * FROM TABLE WHERE predicate OR predicate... LIMIT NUM */
	case TNT_TK_SELECT: {
		tnt_expect(tnt_sqltk(sql, '*'));
		tnt_expect(tnt_sqltk(sql, TNT_TK_FROM));
		tnt_expect(tnt_sqltkv(sql, TNT_TK_TABLE, &tn));
		tnt_expect(tnt_sqltk(sql, TNT_TK_WHERE));
		int32_t index = -1;
		while (1) {
			struct tnt_tuple *tup = tnt_list_at(&tuples, NULL);
			while (1) {
				tnt_expect(tnt_sql_kv_select(sql, tup, &index));
				if (tnt_sqltry(sql, TNT_TK_AND))
					continue;
				if (sql->error)
					goto error;
				break;
			}
			if (tnt_sqltry(sql, TNT_TK_OR))
				continue;
			if (sql->error)
				goto error;
			break;
		}
		uint32_t limit = UINT32_MAX;
		if (tnt_sqltry(sql, TNT_TK_LIMIT)) {
			struct tnt_tk *ltk;
			tnt_expect(tnt_sqltkv(sql, TNT_TK_NUM, &ltk));
			limit = TNT_TK_I(ltk);
		} else
		if (sql->error)
			goto error;
		tnt_expect(tnt_sqltk(sql, TNT_TK_EOF));
		if (tnt_select(sql->s, TNT_TK_I(tn), index, 0, limit, &tuples) == -1) {
			tnt_sql_error(sql, tk, "select failed");
			goto error;
		}
		break;
	}
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
				        (int)TNT_TK_S(name)->size, TNT_TK_S(name)->data);
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
		while (1) {
			tnt_expect(tnt_sql_kv(sql, &tu, false));
			if (tnt_sqltry(sql, ','))
				continue;
			if (sql->error)
				goto error;
			break;
		}
		tnt_expect(tnt_sqltk(sql, ')'));
noargs:
		tnt_expect(tnt_sqltk(sql, TNT_TK_EOF));
		if (tnt_call(sql->s, 0, proc, &tu) == -1) {
			tnt_sql_error(sql, tk, "call failed"); 
			goto error;
		}
		break;
	}
	/* PING */
	case TNT_TK_PING:
		tnt_expect(tnt_sqltk(sql, TNT_TK_EOF));
		if (tnt_ping(sql->s) == -1) {
			tnt_sql_error(sql, tk, "ping failed"); 
			goto error;
		}
		break;
	case TNT_TK_EOF:
		break;
	case TNT_TK_ERROR:
		return tnt_sql_error(sql, tk, "%s", sql->l->error);
	default:
		return tnt_sql_error(sql, tk,
			"insert, replace, update, delete, select, call, ping are expected");
	}
	rc = true;
error:
	tnt_tuple_free(&tu);
	tnt_list_free(&tuples);
	tnt_stream_free(&update);
	return rc;
}

#undef tnt_expect

/* primary sql grammar parsing function. */

static bool tnt_sql(struct tnt_sql *sql) {
	return tnt_sql_stmt(sql);
}

/*
 * tnt_query()
 *
 * Parses and processes supplied SQL query;
 *
 * s     - stream pointer
 * q     - sql query string
 * qsize - query size
 * e     - error description string
 * 
 * returns 0 on success, or -1 on error
 * and string description returned (must be freed after use).
*/
int
tnt_query(struct tnt_stream *s, char *q, size_t qsize, char **e)
{
	struct tnt_lex l;
	if (!tnt_lex_init(&l, (unsigned char*)q, qsize))
		return -1;
	struct tnt_sql sql = { s, &l, NULL };
	bool ret = tnt_sql(&sql);
	if (e) {
		*e = sql.error;
	} else {
		if (sql.error)
			tnt_mem_free(sql.error);
	}
	tnt_lex_free(&l);
	return (ret) ? 0 : -1;
}

/*
 * tnt_query_is()
 *
 * Tells if the supplied query should be processed as SQL.
 *
 * q     - sql query string
 * qsize - query size
 * 
 * returns 1 if yes, 0 otherwise.
*/
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
		if (tk->tk == TNT_TK_PING    ||
		    tk->tk == TNT_TK_INSERT  ||
		    tk->tk == TNT_TK_REPLACE ||
		    tk->tk == TNT_TK_UPDATE  ||
		    tk->tk == TNT_TK_SELECT  ||
		    tk->tk == TNT_TK_DELETE  ||
		    tk->tk == TNT_TK_CALL)
			rc = 1;
		break;
	}
	tnt_lex_free(&l);
	return rc;
}
