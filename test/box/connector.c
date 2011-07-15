
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <connector/c/include/tnt.h>
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
	printf("%s failed: %s", name, tnt_perror(t));
	if (tnt_error(t) == TNT_ESYSTEM)
		printf("(%s)", strerror(tnt_error_errno(t)));
	printf("\n");
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
				tnt_perror(t), TNT_RECV_OP(rcv),
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
test_insert(struct tnt *t)
{
	char buf[] = "hello world";
	int buf_len = sizeof(buf) - 1;

	struct tnt_tuple tu;
	tnt_tuple_init(&tu, 2);
	tnt_tuple_set(&tu, 0, (char*)&key, key_len);
	tnt_tuple_set(&tu, 1, buf, buf_len);
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
test_select(struct tnt *t)
{
	struct tnt_tuples tuples;
	tnt_tuples_init(&tuples);
	struct tnt_tuple *tu = tnt_tuples_add(&tuples);
	tnt_tuple_init(tu, 1);
	tnt_tuple_set(tu, 0, (char*)&key, key_len);
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

int
main(void)
{
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
	test_insert(t);
	test_update(t);
	test_select(t);
	test_delete(t);

	tnt_free(t);
	return 0;
}
