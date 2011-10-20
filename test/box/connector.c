
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <connector/c/include/tnt.h>
#include <connector/c/sql/tnt_sql.h>
#include <connector/c/sql/tnt_utf8.h>
#include <connector/c/sql/tnt_lex.h>
#include <util.h>
#include <errcode.h>

static int key = 0xdeadbeef;
static int key_len = sizeof(key);

static int
test_assert(char *file, int line, int expr, char *exprsz)
{
	if (expr)
		return 1;
	printf("[%s:%d] %s\n", file, line, exprsz);
	return 0;
}

#define TEST_ASSERT(EXPR) \
	test_assert(__FILE__, __LINE__, (EXPR), #EXPR)

static void
test_error(struct tnt *t, char *name)
{
	printf("%s failed: %s\n", name, tnt_strerror(t));
}

static int
test_recv(struct tnt *t, struct tnt_recv *rcv, char *name)
{
	if (tnt_recv(t, rcv) == -1) {
		test_error(t, "recv");
		return -1;
	} else {
		if (tnt_error(t) != TNT_EOK) {
			printf("%s: respond %s (op: %d, reqid: %d, code: %d, count: %d)\n",
				name,
				tnt_strerror(t), TNT_RECV_OP(rcv),
				TNT_RECV_ID(rcv),
				TNT_RECV_CODE(rcv),
				TNT_RECV_COUNT(rcv));
			if (tnt_error(t) == TNT_EERROR)
				printf("%s: %s\n", name, tnt_recv_error(rcv));
			return -1;
		}
	}
	return 0;
}

static void
test_ping(struct tnt *t)
{
	if (tnt_ping(t, 0x1234) == -1)
		test_error(t, "ping");
	tnt_flush(t);

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "ping") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_PING))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_ID(&rcv) == 0x1234))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 0))
		goto done;
done:
	tnt_recv_free(&rcv);
}

static void
test_ping_sql(struct tnt *t)
{
	char *e;
	char q[] = "ping";
	if (tnt_query(t, q, sizeof(q) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}
	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "ping") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_PING))
		goto done;
done:
	tnt_recv_free(&rcv);
}

static void
test_insert(struct tnt *t)
{
	char buf[] = "hello world";
	int buf_len = sizeof(buf) - 1;

	struct tnt_tuple tu;
	tnt_tuple_init(&tu);
	tnt_tuple_add(&tu, (char*)&key, key_len);
	tnt_tuple_add(&tu, buf, buf_len);
	if (tnt_insert(t, 0xFAFA, 0, TNT_PROTO_FLAG_RETURN, &tu) == -1)
		test_error(t, "insert");
	tnt_flush(t);
	tnt_tuple_free(&tu);

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "insert") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_INSERT))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_ID(&rcv) == 0xFAFA))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
		goto done;
	struct tnt_tuple *tp;
	TNT_RECV_FOREACH(&rcv, tp) {
		if (!TEST_ASSERT(TNT_TUPLE_COUNT(tp) == 2))
			goto done;
		struct tnt_tuple_field *k = tnt_tuple_get(tp, 0);
		if (!TEST_ASSERT(*((int*)TNT_TUPLE_FIELD_DATA(k)) == key))
			goto done;
		struct tnt_tuple_field *v = tnt_tuple_get(tp, 1);
		if (!TEST_ASSERT(!strncmp(TNT_TUPLE_FIELD_DATA(v), buf,
				TNT_TUPLE_FIELD_SIZE(v))))
			goto done;
	}
done:
	tnt_recv_free(&rcv);
}

static void
test_insert_sql(struct tnt *t)
{
	char *e;
	char q[] = "insert into t0 values(222, 'world', 'abc')";
	if (tnt_query(t, q, sizeof(q) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "insert") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_INSERT))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
		goto done;
done:
	tnt_recv_free(&rcv);
}

static void
test_update(struct tnt *t)
{
	char buf[] = "world hello";
	int buf_len = sizeof(buf) - 1;

	struct tnt_update u;
	tnt_update_init(&u);
	tnt_update_assign(&u, 1, buf, buf_len);
	if (tnt_update(t, 0xAAFF, 0,
		TNT_PROTO_FLAG_RETURN, (char*)&key, key_len, &u) == -1)
		test_error(t, "update");
	tnt_update_free(&u);
	tnt_flush(t);

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "update") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_UPDATE))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_ID(&rcv) == 0xAAFF))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
		goto done;
	struct tnt_tuple *tp;
	TNT_RECV_FOREACH(&rcv, tp) {
		if (!TEST_ASSERT(TNT_TUPLE_COUNT(tp) == 2))
			goto done;
		struct tnt_tuple_field *k = tnt_tuple_get(tp, 0);
		if (!TEST_ASSERT(*((int*)TNT_TUPLE_FIELD_DATA(k)) == key))
			goto done;
		struct tnt_tuple_field *v = tnt_tuple_get(tp, 1);
		if (!TEST_ASSERT(!strncmp(TNT_TUPLE_FIELD_DATA(v), buf,
			TNT_TUPLE_FIELD_SIZE(v))))
			goto done;
	}
done:
	tnt_recv_free(&rcv);
}

static void
test_update_sql(struct tnt *t)
{
	char *e;
	char q1[] = "update t0 set k0 = 7 where k0 = 222";
	if (tnt_query(t, q1, sizeof(q1) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}
	/* 7 + 1 = 8 */
	char q2[] = "update t0 set k0 = k0 + 1 where k0 = 7";
	if (tnt_query(t, q2, sizeof(q2) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}
	/* 8 | 2 = 10 */
	char q3[] = "update t0 set k0 = k0 | 2 where k0 = 8";
	if (tnt_query(t, q3, sizeof(q3) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}
	/* 10 & 2 = 2 */
	char q4[] = "update t0 set k0 = k0 & 2 where k0 = 10";
	if (tnt_query(t, q4, sizeof(q4) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}
	/* 2 ^ 123 = 121 */
	char q5[] = "update t0 set k0 = k0 ^ 123 where k0 = 2";
	if (tnt_query(t, q5, sizeof(q5) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}
	/* assign */
	char q6[] = "update t0 set k0 = 222, k1 = 'hello world' where k0 = 121";
	if (tnt_query(t, q6, sizeof(q6) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}
	/* splice */
	char q7[] = "update t0 set k2 = splice(k2, 0, 2, 'AB') where k0 = 222";
	if (tnt_query(t, q7, sizeof(q7) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}
	tnt_flush(t);

	int i;
	for (i = 0 ; i < 7 ; i++) {
		struct tnt_recv rcv; 
		tnt_recv_init(&rcv);
		if (test_recv(t, &rcv, "update") == -1)
			goto done;
		if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_UPDATE))
			goto done;
		if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
			goto done;
		if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
			goto done;
		tnt_recv_free(&rcv);
	}
done:;
}

static void
test_select(struct tnt *t)
{
	struct tnt_tuples tuples;
	tnt_tuples_init(&tuples);
	struct tnt_tuple *tu = tnt_tuples_add(&tuples);
	tnt_tuple_init(tu);
	tnt_tuple_add(tu, (char*)&key, key_len);
	if (tnt_select(t, 0x444, 0, 0, 0, 100, &tuples) == -1)
		test_error(t, "select");
	tnt_tuples_free(&tuples);
	tnt_flush(t);

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "select") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_SELECT))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_ID(&rcv) == 0x444))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
		goto done;
	struct tnt_tuple *tp;
	TNT_RECV_FOREACH(&rcv, tp) {
		if (!TEST_ASSERT(TNT_TUPLE_COUNT(tp) == 2))
			goto done;
		struct tnt_tuple_field *k = tnt_tuple_get(tp, 0);
		if (!TEST_ASSERT(*((int*)TNT_TUPLE_FIELD_DATA(k)) == key))
			goto done;
		struct tnt_tuple_field *v = tnt_tuple_get(tp, 1);
		if (!TEST_ASSERT(!strncmp(TNT_TUPLE_FIELD_DATA(v), "world hello",
			TNT_TUPLE_FIELD_SIZE(v))))
			goto done;
	}
done:
	tnt_recv_free(&rcv);
}

static void
test_select_sql(struct tnt *t)
{
	char *e;
	char q[] = "select * from t0 where k0 = 222";
	if (tnt_query(t, q, sizeof(q) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "select") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_SELECT))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
		goto done;
	struct tnt_tuple *tp;
	TNT_RECV_FOREACH(&rcv, tp) {
		if (!TEST_ASSERT(TNT_TUPLE_COUNT(tp) == 3))
			goto done;
		struct tnt_tuple_field *k = tnt_tuple_get(tp, 0);
		if (!TEST_ASSERT(*((int*)TNT_TUPLE_FIELD_DATA(k)) == 222))
			goto done;
		struct tnt_tuple_field *v = tnt_tuple_get(tp, 1);
		if (!TEST_ASSERT(!strncmp(TNT_TUPLE_FIELD_DATA(v), "hello world",
			TNT_TUPLE_FIELD_SIZE(v))))
			goto done;
	}
done:
	tnt_recv_free(&rcv);
}

static void
test_delete(struct tnt *t)
{
	if (tnt_delete(t, 0x777, 0, (char*)&key, key_len) == -1)
		test_error(t, "delete");
	tnt_flush(t);

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "delete") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_DELETE))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_ID(&rcv) == 0x777))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
		goto done;
done:
	tnt_recv_free(&rcv);
}

static void
test_delete_sql(struct tnt *t)
{
	char *e;
	char q[] = "delete from t0 where k0 = 222";
	if (tnt_query(t, q, sizeof(q) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "delete") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_DELETE))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
		goto done;
done:
	tnt_recv_free(&rcv);
}

static void
test_call(struct tnt *t)
{
	if (tnt_call(t, 0, 0, "box.insert", "%d%d%s%s", 0, 333, "abc", "bca") == -1)
		test_error(t, "call");
	tnt_flush(t);

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "call") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_CALL))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
		goto done;
done:
	tnt_recv_free(&rcv);
}

static void
test_call_sql(struct tnt *t)
{
	char *e;
	char q[] = "call box.insert(0, 444, 'abc', 'bca')";
	if (tnt_query(t, q, sizeof(q) - 1, &e) == -1) {
		printf("%s\n", e);
		return;
	}

	struct tnt_recv rcv; 
	tnt_recv_init(&rcv);
	if (test_recv(t, &rcv, "call") == -1)
		goto done;
	if (!TEST_ASSERT(TNT_RECV_OP(&rcv) == TNT_RECV_CALL))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_CODE(&rcv) == 0))
		goto done;
	if (!TEST_ASSERT(TNT_RECV_COUNT(&rcv) == 1))
		goto done;
done:
	tnt_recv_free(&rcv);
}

static void
test_sql_lexer(void)
{
	/* white spaces and comments */
	{
		unsigned char sz[] = " 	# abcde fghjk ## hh\n   # zzz\n";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk;
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
		tnt_lex_free(&l);
	}

	/* integer */
	{
		unsigned char sz[] = "\f\r\n 123 34\n\t\r56";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk;
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM && TNT_TK_I(tk) == 123);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM && TNT_TK_I(tk) == 34);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM && TNT_TK_I(tk) == 56);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
		tnt_lex_free(&l);
	}

	/* punct */
	{
		unsigned char sz[] = "123,34\n-10\t:\r(56)";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk;
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM && TNT_TK_I(tk) == 123);
		TEST_ASSERT(tnt_lex(&l, &tk) == ',' && TNT_TK_I(tk) == ',');
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM && TNT_TK_I(tk) == 34);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM && TNT_TK_I(tk) == -10);
		TEST_ASSERT(tnt_lex(&l, &tk) == ':' && TNT_TK_I(tk) == ':');
		TEST_ASSERT(tnt_lex(&l, &tk) == '('&& TNT_TK_I(tk) == '(');
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM && TNT_TK_I(tk) == 56);
		TEST_ASSERT(tnt_lex(&l, &tk) == ')' && TNT_TK_I(tk) == ')');
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
		tnt_lex_free(&l);
	}

	/* string */
	{
		unsigned char sz[] = "  'hello'\n\t  'world'  'всем привет!'";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk;
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_STRING &&
		       TNT_TK_S(tk)->size == 5 &&
		       memcmp(TNT_TK_S(tk)->data, "hello", 5) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_STRING &&
		       TNT_TK_S(tk)->size == 5 &&
		       memcmp(TNT_TK_S(tk)->data, "world", 5) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_STRING &&
		       TNT_TK_S(tk)->size == 22 &&
		       memcmp(TNT_TK_S(tk)->data, "всем привет!", 22) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
		tnt_lex_free(&l);
	}

	/* ids */
	{
		unsigned char sz[] = "  hello\nэтот безумный безумный мир\t  world  ";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk;
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
		       TNT_TK_S(tk)->size == 5 &&
		       memcmp(TNT_TK_S(tk)->data, "hello", 5) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
		       TNT_TK_S(tk)->size == 8 &&
		       memcmp(TNT_TK_S(tk)->data, "этот", 8) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
		       TNT_TK_S(tk)->size == 16 &&
		       memcmp(TNT_TK_S(tk)->data, "безумный", 16) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
		       TNT_TK_S(tk)->size == 16 &&
		       memcmp(TNT_TK_S(tk)->data, "безумный", 16) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
		       TNT_TK_S(tk)->size == 6 &&
		       memcmp(TNT_TK_S(tk)->data, "мир", 6) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
		       TNT_TK_S(tk)->size == 5 &&
		       memcmp(TNT_TK_S(tk)->data, "world", 5) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
		tnt_lex_free(&l);
	}

	/* keys and tables */
	{
		unsigned char sz[] = "  k0\n\tk20 t0 k1000 t55 k001 t8";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk;
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_KEY && TNT_TK_I(tk) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_KEY && TNT_TK_I(tk) == 20);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_TABLE && TNT_TK_I(tk) == 0);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_KEY && TNT_TK_I(tk) == 1000);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_TABLE && TNT_TK_I(tk) == 55);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_KEY && TNT_TK_I(tk) == 1);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_TABLE && TNT_TK_I(tk) == 8);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
		tnt_lex_free(&l);
	}

	/* keywords */
	{
		unsigned char sz[] = "  INSERT UPDATE INTO OR FROM WHERE VALUES";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk;
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_INSERT);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_UPDATE);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_INTO);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_OR);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_FROM);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_WHERE);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_VALUES);
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
		tnt_lex_free(&l);
	}

	/* stack */
	{
		unsigned char sz[] = "  1 'hey' ,.55";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk1, *tk2, *tk3, *tk4, *tk5, *tk6;

		TEST_ASSERT(tnt_lex(&l, &tk1) == TNT_TK_NUM);
		TEST_ASSERT(tnt_lex(&l, &tk2) == TNT_TK_STRING);
		TEST_ASSERT(tnt_lex(&l, &tk3) == ',');
		TEST_ASSERT(tnt_lex(&l, &tk4) == '.');
		TEST_ASSERT(tnt_lex(&l, &tk5) == TNT_TK_NUM);
		TEST_ASSERT(tnt_lex(&l, &tk6) == TNT_TK_EOF);

		tnt_lex_push(&l, tk5);
		tnt_lex_push(&l, tk4);
		tnt_lex_push(&l, tk3);
		tnt_lex_push(&l, tk2);
		tnt_lex_push(&l, tk1);

		TEST_ASSERT(tnt_lex(&l, &tk1) == TNT_TK_NUM);
		TEST_ASSERT(tnt_lex(&l, &tk2) == TNT_TK_STRING);
		TEST_ASSERT(tnt_lex(&l, &tk3) == ',');
		TEST_ASSERT(tnt_lex(&l, &tk4) == '.');
		TEST_ASSERT(tnt_lex(&l, &tk5) == TNT_TK_NUM);
		TEST_ASSERT(tnt_lex(&l, &tk6) == TNT_TK_EOF);
		tnt_lex_free(&l);
	}

	/* error - bad string 1 */
	{
		unsigned char sz[] = "  '";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk;
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ERROR);
		tnt_lex_free(&l);

	}

	/* error - bad string 2 */
	{
		unsigned char sz[] = "  '\n'";
		struct tnt_lex l;
		tnt_lex_init(&l, sz, sizeof(sz) - 1);
		struct tnt_tk *tk;
		TEST_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ERROR);
		tnt_lex_free(&l);
	}
}

int
main(void)
{
	test_sql_lexer();

	struct tnt *t = tnt_alloc();
	if (t == NULL)
		return 1;
	tnt_set(t, TNT_OPT_HOSTNAME, "localhost");
	tnt_set(t, TNT_OPT_PORT, 33013); 
	if (tnt_init(t) == -1)
		return 1;
	if (tnt_connect(t) == -1)
		return 1;

	test_ping(t);
	test_ping_sql(t);
	test_insert(t);
	test_insert_sql(t);
	test_update(t);
	test_update_sql(t);
	test_select(t);
	test_select_sql(t);
	test_delete(t);
	test_delete_sql(t);
	test_call(t);
	test_call_sql(t);

	tnt_free(t);
	return 0;
}
