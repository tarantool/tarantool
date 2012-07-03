
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

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_io.h>
#include <connector/c/include/tarantool/tnt_queue.h>
#include <connector/c/include/tarantool/tnt_utf8.h>
#include <connector/c/include/tarantool/tnt_lex.h>
#include <connector/c/include/tarantool/tnt_sql.h>

struct tt_test;

typedef void (*tt_testf_t)(struct tt_test *t);

struct tt_test {
	char *name;
	tt_testf_t cb;
	struct tt_test *next;
};

struct tt_list {
	struct tt_test *head, *tail;
	int count;
};

static struct tt_test* tt_test(struct tt_list *list, char *name, tt_testf_t cb) {
	struct tt_test *t = malloc(sizeof(struct tt_test));
	if (t == NULL)
		return NULL;
	t->name = strdup(name);
	t->cb = cb;
	t->next = NULL;
	if (list->head == NULL)
		list->head = t;
	else
		list->tail->next = t;
	list->tail = t;
	list->count++;
	return t;
}

static void tt_free(struct tt_list *list) {
	struct tt_test *i = list->head;
	struct tt_test *n = NULL;
	while (i) {
		n = i->next;
		free(i->name);
		free(i);
		i = n;
	}
}

static void tt_run(struct tt_list *list) {
	struct tt_test *i = list->head;
	while (i) {
		printf("> %-30s", i->name);
		fflush(NULL);
		i->cb(i);
		printf("[OK]\n");
		i = i->next;
	}
}

static void
tt_assert(struct tt_test *t, char *file, int line, int expr, char *exprsz) {
	if (expr)
		return;
	(void)t;
	printf("[%s:%d] %s\n", file, line, exprsz);
	abort();
}

#define TT_ASSERT(EXPR) \
	tt_assert(test, __FILE__, __LINE__, (EXPR), #EXPR)

/* basic tuple creation */
static void tt_tnt_tuple1(struct tt_test *test) {
	struct tnt_tuple t;
	tnt_tuple_init(&t);
	tnt_tuple(&t, "%s%d", "foo", 123);
	TT_ASSERT(t.alloc == 0);
	TT_ASSERT(t.cardinality == 2);
	TT_ASSERT(t.data != NULL);
	TT_ASSERT(t.size != 0);
	tnt_tuple_free(&t);
	struct tnt_tuple *tp = tnt_tuple(NULL, "%s%d", "foo", 123);
	TT_ASSERT(tp->alloc == 1);
	TT_ASSERT(tp->cardinality == 2);
	TT_ASSERT(tp->data != NULL);
	TT_ASSERT(tp->size != 0);
	tnt_tuple_free(tp);
}

/* basic tuple field manipulation */
static void tt_tnt_tuple2(struct tt_test *test) {
	struct tnt_tuple t;
	tnt_tuple_init(&t);
	tnt_tuple_add(&t, "foo", 4);
	TT_ASSERT(t.alloc == 0);
	TT_ASSERT(t.cardinality == 1);
	TT_ASSERT(t.data != NULL);
	TT_ASSERT(t.size != 0);
	tnt_tuple_add(&t, "bar", 4);
	TT_ASSERT(t.cardinality == 2);
	tnt_tuple_add(&t, "baz", 4);
	TT_ASSERT(t.cardinality == 3);
	tnt_tuple(&t, "%s%d", "xyz", 123);
	TT_ASSERT(t.cardinality == 5);
	tnt_tuple_free(&t);
}

/* basic list operations */
static void tt_tnt_list(struct tt_test *test) {
	struct tnt_list list;
	tnt_list_init(&list);
	tnt_list(&list, tnt_tuple(NULL, "%s", "foo"), NULL);
	TT_ASSERT(list.list != NULL);
	TT_ASSERT(list.alloc == 0);
	TT_ASSERT(list.count == 1);
	tnt_list(&list, tnt_tuple(NULL, "%s", "foo"), NULL);
	tnt_list(&list, tnt_tuple(NULL, "%s", "foo"), NULL);
	TT_ASSERT(list.count == 3);
	struct tnt_tuple t;
	tnt_tuple_init(&t);
	tnt_list_at(&list, &t);
	TT_ASSERT(list.count == 4);
	tnt_list_free(&list);
	struct tnt_list *l =
		tnt_list(NULL,
			 tnt_tuple(NULL, "%s", "foo"),
			 tnt_tuple(NULL, "%s", "bar"),
			 tnt_tuple(NULL, "%s", "baz"), NULL);
	TT_ASSERT(l->alloc == 1);
	TT_ASSERT(l->list != NULL);
	TT_ASSERT(l->count == 3);
	tnt_list_free(l);
}

/* stream buffer */
static void tt_tnt_sbuf(struct tt_test *test) {
	struct tnt_stream s;
	tnt_buf(&s);
	TT_ASSERT(s.alloc == 0);
	struct tnt_stream_buf *sb = TNT_SBUF_CAST(&s);
	TT_ASSERT(sb->data == NULL);
	TT_ASSERT(sb->size == 0);
	TT_ASSERT(sb->rdoff == 0);
	TT_ASSERT(s.wrcnt == 0);
	struct tnt_tuple *kv = tnt_tuple(NULL, "%s%d", "key", 123);
	tnt_insert(&s, 0, 0, kv);
	TT_ASSERT(sb->data != NULL);
	TT_ASSERT(sb->size != 0);
	TT_ASSERT(sb->rdoff == 0);
	TT_ASSERT(s.wrcnt == 1);
	tnt_insert(&s, 0, 0, kv);
	TT_ASSERT(s.wrcnt == 2);
	tnt_tuple_free(kv);
	tnt_stream_free(&s);
}

/* tuple set */
static void tt_tnt_tuple_set(struct tt_test *test) {
	char buf[75];
	*((uint32_t*)buf) = 2; /* cardinality */
	/* 4 + 1 + 5 + 1 + 64 = 75 */
	uint32_t off = sizeof(uint32_t);
	int esize = tnt_enc_size(5);
	tnt_enc_write(buf + off, 5);
	off += esize + 5;
	esize = tnt_enc_size(64);
	tnt_enc_write(buf + off, 64);
	off += esize + 64;
	struct tnt_tuple t;
	tnt_tuple_init(&t);
	TT_ASSERT(tnt_tuple_set(&t, buf, 70) == NULL);
	TT_ASSERT(tnt_tuple_set(&t, buf, sizeof(buf)) != NULL);
	tnt_tuple_free(&t);
}

/* iterator tuple */
static void tt_tnt_iter1(struct tt_test *test) {
	struct tnt_tuple *t = tnt_tuple(NULL, "%s%d%s", "foo", 123, "bar");
	TT_ASSERT(t->cardinality == 3);
	struct tnt_iter i;
	tnt_iter(&i, t);
	TT_ASSERT(tnt_next(&i) == 1);
	TT_ASSERT(TNT_IFIELD_IDX(&i) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(&i) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(&i), "foo", 3) == 0);
	TT_ASSERT(tnt_next(&i) == 1);
	TT_ASSERT(TNT_IFIELD_SIZE(&i) == 4);
	TT_ASSERT(TNT_IFIELD_IDX(&i) == 1);
	TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(&i) == 123);
	TT_ASSERT(tnt_next(&i) == 1);
	TT_ASSERT(TNT_IFIELD_IDX(&i) == 2);
	TT_ASSERT(TNT_IFIELD_SIZE(&i) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(&i), "bar", 3) == 0);
	TT_ASSERT(tnt_next(&i) == 0);
	tnt_iter_free(&i);
	tnt_tuple_free(t);
}

/* iterator tuple single field */
static void tt_tnt_iter11(struct tt_test *test) {
	struct tnt_tuple *t = tnt_tuple(NULL, "%s", "foo");
	TT_ASSERT(t->cardinality == 1);
	struct tnt_iter i;
	tnt_iter(&i, t);
	TT_ASSERT(tnt_next(&i) == 1);
	TT_ASSERT(TNT_IFIELD_IDX(&i) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(&i) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(&i), "foo", 3) == 0);
	TT_ASSERT(tnt_next(&i) == 0);
	tnt_iter_free(&i);
	tnt_tuple_free(t);
}

/* iterator tuple field */
static void tt_tnt_iter2(struct tt_test *test) {
	struct tnt_tuple *t = tnt_tuple(NULL, "%s%d%s", "foo", 123, "bar");
	TT_ASSERT(t->cardinality == 3);
	struct tnt_iter *i = tnt_field(NULL, t, 0);
	TT_ASSERT(i->alloc != 0);
	TT_ASSERT(tnt_field(i, NULL, 0) != NULL);
	TT_ASSERT(TNT_IFIELD_IDX(i) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(i) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(i), "foo", 3) == 0);
	TT_ASSERT(tnt_field(i, NULL, 1) != NULL);
	TT_ASSERT(TNT_IFIELD_SIZE(i) == 4);
	TT_ASSERT(TNT_IFIELD_IDX(i) == 1);
	TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(i) == 123);
	TT_ASSERT(tnt_field(i, NULL, 2) != NULL);
	TT_ASSERT(TNT_IFIELD_IDX(i) == 2);
	TT_ASSERT(TNT_IFIELD_SIZE(i) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(i), "bar", 3) == 0);
	TT_ASSERT(tnt_field(i, NULL, 3) == NULL);
	tnt_iter_free(i);
	tnt_tuple_free(t);
}

/* iterator list */
static void tt_tnt_iter3(struct tt_test *test) {
	struct tnt_tuple t1, t2, t3;
	tnt_tuple_init(&t1);
	tnt_tuple_init(&t2);
	tnt_tuple_init(&t3);
	tnt_tuple(&t1, "%s", "foo");
	tnt_tuple(&t2, "%s", "bar");
	tnt_tuple(&t3, "%s", "baz");
	struct tnt_list *l = tnt_list(NULL, &t1, &t2, &t3, NULL);
	TT_ASSERT(l->count == 3);
	struct tnt_iter i;
	tnt_iter_list(&i, l);
	TT_ASSERT(tnt_next(&i) == 1);
	TT_ASSERT(TNT_ILIST_TUPLE(&i) == &t1);
	TT_ASSERT(tnt_next(&i) == 1);
	TT_ASSERT(TNT_ILIST_TUPLE(&i) == &t2);
	TT_ASSERT(tnt_next(&i) == 1);
	TT_ASSERT(TNT_ILIST_TUPLE(&i) == &t3);
	TT_ASSERT(tnt_next(&i) == 0);
	tnt_tuple_free(&t1);
	tnt_tuple_free(&t2);
	tnt_tuple_free(&t3);
	tnt_iter_free(&i);
	tnt_list_free(l);
}

/* marshal ping */
static void tt_tnt_marshal_ping(struct tt_test *test) {
	struct tnt_stream s;
	tnt_buf(&s);
	tnt_ping(&s);
	tnt_ping(&s);
	struct tnt_iter i;
	tnt_iter_request(&i, &s);
	TT_ASSERT(tnt_next(&i) == 1);
	struct tnt_request *r = TNT_IREQUEST_PTR(&i);
	TT_ASSERT(r->h.type == TNT_OP_PING);
	TT_ASSERT(tnt_next(&i) == 1);
	TT_ASSERT(r->h.type == TNT_OP_PING);
	TT_ASSERT(tnt_next(&i) == 0);
	tnt_iter_free(&i);
	tnt_stream_free(&s);
}

/* marshal insert */
static void tt_tnt_marshal_insert(struct tt_test *test) {
	struct tnt_stream s;
	tnt_buf(&s);
	struct tnt_tuple t;
	tnt_tuple_init(&t);
	tnt_tuple(&t, "%s%d", "foo", 123);
	tnt_insert(&s, 0, 0, &t);
	tnt_insert(&s, 0, 0, &t);
	struct tnt_iter i;
	tnt_iter_request(&i, &s);
	TT_ASSERT(tnt_next(&i) == 1);
	struct tnt_request *r = TNT_IREQUEST_PTR(&i);
	TT_ASSERT(r->h.type == TNT_OP_INSERT);
	struct tnt_iter *f = tnt_field(NULL, &r->r.insert.t, 0);
	TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "foo", 3) == 0);
	TT_ASSERT(tnt_field(f, NULL, 1) != NULL);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 4);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 1);
	TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(f) == 123);
	TT_ASSERT(tnt_next(&i) == 1);
	r = TNT_IREQUEST_PTR(&i);
	TT_ASSERT(r->h.type == TNT_OP_INSERT);
	TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "foo", 3) == 0);
	TT_ASSERT(tnt_field(f, NULL, 1) != NULL);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 4);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 1);
	TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(f) == 123);
	TT_ASSERT(tnt_next(&i) == 0);
	tnt_tuple_free(&t);
	tnt_iter_free(&i);
	tnt_stream_free(&s);
}

/* marshal delete */
static void tt_tnt_marshal_delete(struct tt_test *test) {
	struct tnt_stream s;
	tnt_buf(&s);
	struct tnt_tuple t;
	tnt_tuple_init(&t);
	tnt_tuple(&t, "%s", "foo");
	tnt_delete(&s, 0, 0, &t);
	tnt_delete(&s, 0, 0, &t);
	struct tnt_iter i;
	tnt_iter_request(&i, &s);
	TT_ASSERT(tnt_next(&i) == 1);
	struct tnt_request *r = TNT_IREQUEST_PTR(&i);
	TT_ASSERT(r->h.type == TNT_OP_DELETE);
	struct tnt_iter *f = tnt_field(NULL, &r->r.del.t, 0);
	TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "foo", 3) == 0);
	TT_ASSERT(tnt_next(&i) == 1);
	r = TNT_IREQUEST_PTR(&i);
	TT_ASSERT(r->h.type == TNT_OP_DELETE);
	TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "foo", 3) == 0);
	TT_ASSERT(tnt_next(&i) == 0);
	tnt_tuple_free(&t);
	tnt_iter_free(&i);
	tnt_stream_free(&s);
}

/* marshal call */
static void tt_tnt_marshal_call(struct tt_test *test) {
	struct tnt_stream s;
	tnt_buf(&s);
	struct tnt_tuple t;
	tnt_tuple_init(&t);
	tnt_tuple(&t, "%s%d", "foo", 123);
	tnt_call(&s, 0, "box.select", &t);
	tnt_call(&s, 0, "box.select", &t);
	struct tnt_iter i;
	tnt_iter_request(&i, &s);
	TT_ASSERT(tnt_next(&i) == 1);
	struct tnt_request *r = TNT_IREQUEST_PTR(&i);
	TT_ASSERT(r->h.type == TNT_OP_CALL);
	TT_ASSERT(strcmp(r->r.call.proc, "box.select") == 0);
	struct tnt_iter *f = tnt_field(NULL, &r->r.call.t, 0);
	TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "foo", 3) == 0);
	TT_ASSERT(tnt_field(f, NULL, 1) != NULL);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 4);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 1);
	TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(f) == 123);
	TT_ASSERT(tnt_next(&i) == 1);
	r = TNT_IREQUEST_PTR(&i);
	TT_ASSERT(r->h.type == TNT_OP_CALL);
	TT_ASSERT(strcmp(r->r.call.proc, "box.select") == 0);
	TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "foo", 3) == 0);
	TT_ASSERT(tnt_field(f, NULL, 1) != NULL);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 4);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 1);
	TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(f) == 123);
	TT_ASSERT(tnt_next(&i) == 0);
	tnt_tuple_free(&t);
	tnt_iter_free(&i);
	tnt_stream_free(&s);
}

/* marshal select */
static void tt_tnt_marshal_select(struct tt_test *test) {
	struct tnt_stream s;
	tnt_buf(&s);
	struct tnt_list list;
	tnt_list_init(&list);
	tnt_list(&list, tnt_tuple(NULL, "%s", "foo"),
			tnt_tuple(NULL, "%s%d", "bar", 444),
			tnt_tuple(NULL, "%s%d%d", "baz", 1, 2),
			NULL);
	tnt_select(&s, 0, 0, 0, 1, &list);
	struct tnt_iter i;
	tnt_iter_request(&i, &s);
	TT_ASSERT(tnt_next(&i) == 1);
	struct tnt_request *r = TNT_IREQUEST_PTR(&i);
	TT_ASSERT(r->h.type == TNT_OP_SELECT);
	struct tnt_iter il;
	tnt_iter_list(&il, &r->r.select.l);
	TT_ASSERT(tnt_next(&il) == 1);
		struct tnt_tuple *t = TNT_ILIST_TUPLE(&il);
		struct tnt_iter *f = tnt_field(NULL, t, 0);
		TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
		TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
		TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
		TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "foo", 3) == 0);
		tnt_iter_free(f);
	TT_ASSERT(tnt_next(&il) == 1);
		t = TNT_ILIST_TUPLE(&il);
		f = tnt_field(NULL, t, 0);
		TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
		TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
		TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
		TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "bar", 3) == 0);
		TT_ASSERT(tnt_field(f, NULL, 1) != NULL);
		TT_ASSERT(TNT_IFIELD_SIZE(f) == 4);
		TT_ASSERT(TNT_IFIELD_IDX(f) == 1);
		TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(f) == 444);
		tnt_iter_free(f);
	TT_ASSERT(tnt_next(&il) == 1);
		t = TNT_ILIST_TUPLE(&il);
		f = tnt_field(NULL, t, 0);
		TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
		TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
		TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
		TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "baz", 3) == 0);
		TT_ASSERT(tnt_field(f, NULL, 1) != NULL);
		TT_ASSERT(TNT_IFIELD_SIZE(f) == 4);
		TT_ASSERT(TNT_IFIELD_IDX(f) == 1);
		TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(f) == 1);
		TT_ASSERT(tnt_field(f, NULL, 2) != NULL);
		TT_ASSERT(TNT_IFIELD_SIZE(f) == 4);
		TT_ASSERT(TNT_IFIELD_IDX(f) == 2);
		TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(f) == 2);
		tnt_iter_free(f);
	TT_ASSERT(tnt_next(&il) == 0);
	tnt_iter_free(&i);
	tnt_iter_free(&il);
	tnt_list_free(&list);
	tnt_stream_free(&s);
}

/* marshal update */
static void tt_tnt_marshal_update(struct tt_test *test) {
	struct tnt_stream s, ops;
	tnt_buf(&s);
	tnt_buf(&ops);
	struct tnt_tuple t;
	tnt_tuple_init(&t);
	tnt_tuple(&t, "%s", "foo");
	tnt_update_assign(&ops, 444, "FOO", 3);
	tnt_update_arith(&ops, 2, TNT_UPDATE_ADD, 7);
	TT_ASSERT(tnt_update(&s, 0, 0, &t, &ops) > 0);
	struct tnt_iter i;
	tnt_iter_request(&i, &s);
	TT_ASSERT(tnt_next(&i) == 1);
	struct tnt_request *r = TNT_IREQUEST_PTR(&i);
	TT_ASSERT(r->h.type == TNT_OP_UPDATE);
	TT_ASSERT(r->r.update.opc == 2);
	struct tnt_iter *f = tnt_field(NULL, &r->r.update.t, 0);
	TT_ASSERT(tnt_field(f, NULL, 0) != NULL);
	TT_ASSERT(TNT_IFIELD_IDX(f) == 0);
	TT_ASSERT(TNT_IFIELD_SIZE(f) == 3);
	TT_ASSERT(memcmp(TNT_IFIELD_DATA(f), "foo", 3) == 0);
	TT_ASSERT(r->r.update.opv[0].op == TNT_UPDATE_ASSIGN);
	TT_ASSERT(r->r.update.opv[0].field == 444);
	TT_ASSERT(r->r.update.opv[0].size == 3);
	TT_ASSERT(memcmp(r->r.update.opv[0].data, "FOO", 3) == 0);
	TT_ASSERT(r->r.update.opv[1].op == TNT_UPDATE_ADD);
	TT_ASSERT(r->r.update.opv[1].field == 2);
	TT_ASSERT(r->r.update.opv[1].size == 4);
	TT_ASSERT(*(uint32_t*)r->r.update.opv[1].data == 7);
	TT_ASSERT(tnt_next(&i) == 0);
	tnt_tuple_free(&t);
	tnt_stream_free(&s);
	tnt_stream_free(&ops);
	tnt_iter_free(&i);
}

static struct tnt_stream net;

/* network connection */
static void tt_tnt_net_connect(struct tt_test *test) {
	TT_ASSERT(tnt_net(&net) != NULL);
	TT_ASSERT(tnt_set(&net, TNT_OPT_HOSTNAME, "localhost") == 0);
	TT_ASSERT(tnt_set(&net, TNT_OPT_PORT, 33013) == 0);
	TT_ASSERT(tnt_init(&net) == 0);
	TT_ASSERT(tnt_connect(&net) == 0);
}

/* ping */
static void tt_tnt_net_ping(struct tt_test *test) {
	TT_ASSERT(tnt_ping(&net) > 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_PING);
	}
	tnt_iter_free(&i);
}

/* insert */
static void tt_tnt_net_insert(struct tt_test *test) {
	tnt_stream_reqid(&net, 777);
	struct tnt_tuple kv1;
	tnt_tuple_init(&kv1);
	tnt_tuple(&kv1, "%d%s", 123, "foo");
	TT_ASSERT(tnt_insert(&net, 0, 0, &kv1) > 0);
	struct tnt_tuple kv2;
	tnt_tuple_init(&kv2);
	tnt_tuple(&kv2, "%d%s", 321, "bar");
	TT_ASSERT(tnt_insert(&net, 0, 0, &kv2) > 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	tnt_tuple_free(&kv1);
	tnt_tuple_free(&kv2);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->reqid == 777);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_INSERT);
		TT_ASSERT(r->count == 1);
	}
	tnt_iter_free(&i);
}

/* update */
static void tt_tnt_net_update(struct tt_test *test) {
	struct tnt_stream ops;
	TT_ASSERT(tnt_buf(&ops) != NULL);
	tnt_update_arith(&ops, 0, TNT_UPDATE_ADD, 7);
	tnt_update_assign(&ops, 1, "FOO", 3);
	struct tnt_tuple *k = tnt_tuple(NULL, "%d", 123);
	TT_ASSERT(tnt_update(&net, 0, 0, k, &ops) > 0);
	tnt_tuple_free(k);
	tnt_stream_free(&ops);
	TT_ASSERT(tnt_flush(&net) > 0);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_UPDATE);
		TT_ASSERT(r->count == 1);
	}
}

/* select */
static void tt_tnt_net_select(struct tt_test *test) {
	struct tnt_list *search =
		tnt_list(NULL, tnt_tuple(NULL, "%d", 130), NULL);
	TT_ASSERT(tnt_select(&net, 0, 0, 0, 1, search) > 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	tnt_list_free(search);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_SELECT);
		TT_ASSERT(r->count == 1);
		struct tnt_iter il;
		tnt_iter_list(&il, TNT_REPLY_LIST(r));
		TT_ASSERT(tnt_next(&il) == 1);
		struct tnt_tuple *tp;
		tp = TNT_ILIST_TUPLE(&il);
		TT_ASSERT(tp->cardinality == 2);
		TT_ASSERT(tp->alloc == 1);
		TT_ASSERT(tp->data != NULL);
		TT_ASSERT(tp->size != 0);
		struct tnt_iter ifl;
		tnt_iter(&ifl, tp);
		TT_ASSERT(tnt_next(&ifl) == 1);
		TT_ASSERT(TNT_IFIELD_IDX(&ifl) == 0);
		TT_ASSERT(TNT_IFIELD_SIZE(&ifl) == 4);
		TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(&ifl) == 130);
		TT_ASSERT(tnt_next(&ifl) == 1);
		TT_ASSERT(TNT_IFIELD_IDX(&ifl) == 1);
		TT_ASSERT(TNT_IFIELD_SIZE(&ifl) == 3);
		TT_ASSERT(memcmp(TNT_IFIELD_DATA(&ifl), "FOO", 3) == 0);
		TT_ASSERT(tnt_next(&ifl) == 0);
		tnt_iter_free(&ifl);
		tnt_iter_free(&il);
	}
}

/* delete */
static void tt_tnt_net_delete(struct tt_test *test) {
	struct tnt_tuple k;
	tnt_tuple_init(&k);
	tnt_tuple(&k, "%d", 321);
	TT_ASSERT(tnt_delete(&net, 0, 0, &k) > 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	tnt_tuple_free(&k);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_DELETE);
		TT_ASSERT(r->count == 1);
	}
	tnt_iter_free(&i);
}

/* call */
static void tt_tnt_net_call(struct tt_test *test) {
	struct tnt_tuple args;
	tnt_tuple_init(&args);
	tnt_tuple(&args, "%d%d%s%s", 0, 333, "B", "C");
	TT_ASSERT(tnt_call(&net, 0, "box.insert", &args) > 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	tnt_tuple_free(&args);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_CALL);
		TT_ASSERT(r->count == 1);
	}
	tnt_iter_free(&i);
}

/* call (no args) */
static void tt_tnt_net_call_na(struct tt_test *test) {
	struct tnt_tuple args;
	tnt_tuple_init(&args);
	TT_ASSERT(tnt_call(&net, 0, "box.insert", &args) > 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	tnt_tuple_free(&args);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code != 0);
		TT_ASSERT(strcmp(r->error, "Illegal parameters, tuple field count is 0") == 0);
	}
	tnt_iter_free(&i);
}

/* reply */
static void tt_tnt_net_reply(struct tt_test *test) {
	struct tnt_tuple kv1, kv2;
	tnt_tuple_init(&kv1);
	tnt_tuple(&kv1, "%d%s", 587, "foo");
	TT_ASSERT(tnt_insert(&net, 0, TNT_FLAG_RETURN, &kv1) > 0);
	tnt_tuple_free(&kv1);
	tnt_tuple_init(&kv2);
	tnt_tuple(&kv2, "%d%s", 785, "bar");
	TT_ASSERT(tnt_insert(&net, 0, TNT_FLAG_RETURN, &kv2) > 0);
	tnt_tuple_free(&kv2);
	TT_ASSERT(tnt_flush(&net) > 0);

	struct tnt_stream_net *s = TNT_SNET_CAST(&net);
	int current = 0;
	size_t off = 0;
	ssize_t size = 0;
	char buffer[512];

	while (current != 2) {
		struct tnt_reply r;
		tnt_reply_init(&r);
		int rc = tnt_reply(&r, buffer, size, &off);
		TT_ASSERT(rc != -1);
		if (rc == 1) {
			ssize_t res = tnt_io_recv_raw(s, buffer + size, off, 1);
			TT_ASSERT(res > 0);
			size += off;
			continue;
		}
		TT_ASSERT(rc == 0);
		TT_ASSERT(r.code == 0);
		TT_ASSERT(r.op == TNT_OP_INSERT);
		TT_ASSERT(r.count == 1);
		if (current == 0) {
			struct tnt_iter il;
			tnt_iter_list(&il, TNT_REPLY_LIST(&r));
			TT_ASSERT(tnt_next(&il) == 1);
			struct tnt_tuple *tp = TNT_ILIST_TUPLE(&il);
			TT_ASSERT(tp->cardinality == 2);
			TT_ASSERT(tp->alloc == 1);
			TT_ASSERT(tp->data != NULL);
			TT_ASSERT(tp->size != 0);
			struct tnt_iter ifl;
			tnt_iter(&ifl, tp);
			TT_ASSERT(tnt_next(&ifl) == 1);
			TT_ASSERT(TNT_IFIELD_IDX(&ifl) == 0);
			TT_ASSERT(TNT_IFIELD_SIZE(&ifl) == 4);
			TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(&ifl) == 587);
			TT_ASSERT(tnt_next(&ifl) == 1);
			TT_ASSERT(TNT_IFIELD_IDX(&ifl) == 1);
			TT_ASSERT(TNT_IFIELD_SIZE(&ifl) == 3);
			TT_ASSERT(memcmp(TNT_IFIELD_DATA(&ifl), "foo", 3) == 0);
			TT_ASSERT(tnt_next(&ifl) == 0);
			tnt_iter_free(&ifl);
			tnt_iter_free(&il);
			off = 0;
			size = 0;
		} else
		if (current == 1) {
			struct tnt_iter il;
			tnt_iter_list(&il, TNT_REPLY_LIST(&r));
			TT_ASSERT(tnt_next(&il) == 1);
			struct tnt_tuple *tp = TNT_ILIST_TUPLE(&il);
			TT_ASSERT(tp->cardinality == 2);
			TT_ASSERT(tp->alloc == 1);
			TT_ASSERT(tp->data != NULL);
			TT_ASSERT(tp->size != 0);
			struct tnt_iter ifl;
			tnt_iter(&ifl, tp);
			TT_ASSERT(tnt_next(&ifl) == 1);
			TT_ASSERT(TNT_IFIELD_IDX(&ifl) == 0);
			TT_ASSERT(TNT_IFIELD_SIZE(&ifl) == 4);
			TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(&ifl) == 785);
			TT_ASSERT(tnt_next(&ifl) == 1);
			TT_ASSERT(TNT_IFIELD_IDX(&ifl) == 1);
			TT_ASSERT(TNT_IFIELD_SIZE(&ifl) == 3);
			TT_ASSERT(memcmp(TNT_IFIELD_DATA(&ifl), "bar", 3) == 0);
			TT_ASSERT(tnt_next(&ifl) == 0);
			tnt_iter_free(&ifl);
			tnt_iter_free(&il);
		}
		tnt_reply_free(&r);
		current++;
	}

	net.wrcnt -= 2;
}

/* lex ws */
static void tt_tnt_lex_ws(struct tt_test *test) {
	unsigned char sz[] = " 	# abcde fghjk ## hh\n   # zzz\n";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk;
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
	tnt_lex_free(&l);
}

/* lex integer */
static void tt_tnt_lex_int(struct tt_test *test) {
	unsigned char sz[] = "\f\r\n 123 34\n\t\r56 888L56 2147483646 2147483647 "
		             "-2147483648 -2147483649 72057594037927935";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk;
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == 123);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == 34);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == 56);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM64 && TNT_TK_I64(tk) == 888);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == 56);

	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == INT_MAX - 1);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM64 && TNT_TK_I64(tk) == INT_MAX);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == INT_MIN);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM64 && TNT_TK_I64(tk) == INT_MIN - 1LL);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM64 && TNT_TK_I64(tk) == 72057594037927935LL);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
	tnt_lex_free(&l);
}

/* lex punctuation */
static void tt_tnt_lex_punct(struct tt_test *test) {
	unsigned char sz[] = "123,34\n-10\t:\r(56)";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk;
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == 123);
	TT_ASSERT(tnt_lex(&l, &tk) == ',' && TNT_TK_I32(tk) == ',');
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == 34);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == -10);
	TT_ASSERT(tnt_lex(&l, &tk) == ':' && TNT_TK_I32(tk) == ':');
	TT_ASSERT(tnt_lex(&l, &tk) == '('&& TNT_TK_I32(tk) == '(');
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_NUM32 && TNT_TK_I32(tk) == 56);
	TT_ASSERT(tnt_lex(&l, &tk) == ')' && TNT_TK_I32(tk) == ')');
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
	tnt_lex_free(&l);
}

/* lex string */
static void tt_tnt_lex_str(struct tt_test *test) {
	unsigned char sz[] = "  'hello'\n\t  'world'  'всем привет!'";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk;
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_STRING &&
	       TNT_TK_S(tk)->size == 5 &&
	       memcmp(TNT_TK_S(tk)->data, "hello", 5) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_STRING &&
	       TNT_TK_S(tk)->size == 5 &&
	       memcmp(TNT_TK_S(tk)->data, "world", 5) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_STRING &&
	       TNT_TK_S(tk)->size == 22 &&
	       memcmp(TNT_TK_S(tk)->data, "всем привет!", 22) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
	tnt_lex_free(&l);
}

/* lex id's */
static void tt_tnt_lex_ids(struct tt_test *test) {
	unsigned char sz[] = "  hello\nэтот безумный безумный мир\t  world  ";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk;
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
	       TNT_TK_S(tk)->size == 5 &&
	       memcmp(TNT_TK_S(tk)->data, "hello", 5) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
	       TNT_TK_S(tk)->size == 8 &&
	       memcmp(TNT_TK_S(tk)->data, "этот", 8) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
	       TNT_TK_S(tk)->size == 16 &&
	       memcmp(TNT_TK_S(tk)->data, "безумный", 16) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
	       TNT_TK_S(tk)->size == 16 &&
	       memcmp(TNT_TK_S(tk)->data, "безумный", 16) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
	       TNT_TK_S(tk)->size == 6 &&
	       memcmp(TNT_TK_S(tk)->data, "мир", 6) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ID &&
	       TNT_TK_S(tk)->size == 5 &&
	       memcmp(TNT_TK_S(tk)->data, "world", 5) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
	tnt_lex_free(&l);
}

/* lex keys and tables */
static void tt_tnt_lex_kt(struct tt_test *test) {
	unsigned char sz[] = "  k0\n\tk20 t0 k1000 t55 k001 t8";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk;
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_KEY && TNT_TK_I32(tk) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_KEY && TNT_TK_I32(tk) == 20);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_TABLE && TNT_TK_I32(tk) == 0);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_KEY && TNT_TK_I32(tk) == 1000);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_TABLE && TNT_TK_I32(tk) == 55);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_KEY && TNT_TK_I32(tk) == 1);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_TABLE && TNT_TK_I32(tk) == 8);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
	tnt_lex_free(&l);
}

/* lex keywords */
static void tt_tnt_lex_kw(struct tt_test *test) {
	unsigned char sz[] = "  INSERT UPDATE INTO OR FROM WHERE VALUES";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk;
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_INSERT);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_UPDATE);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_INTO);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_OR);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_FROM);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_WHERE);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_VALUES);
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_EOF);
	tnt_lex_free(&l);
}

/* lex stack */
static void tt_tnt_lex_stack(struct tt_test *test) {
	unsigned char sz[] = "  1 'hey' ,.55";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk1, *tk2, *tk3, *tk4, *tk5, *tk6;
	TT_ASSERT(tnt_lex(&l, &tk1) == TNT_TK_NUM32);
	TT_ASSERT(tnt_lex(&l, &tk2) == TNT_TK_STRING);
	TT_ASSERT(tnt_lex(&l, &tk3) == ',');
	TT_ASSERT(tnt_lex(&l, &tk4) == '.');
	TT_ASSERT(tnt_lex(&l, &tk5) == TNT_TK_NUM32);
	TT_ASSERT(tnt_lex(&l, &tk6) == TNT_TK_EOF);
	tnt_lex_push(&l, tk5);
	tnt_lex_push(&l, tk4);
	tnt_lex_push(&l, tk3);
	tnt_lex_push(&l, tk2);
	tnt_lex_push(&l, tk1);
	TT_ASSERT(tnt_lex(&l, &tk1) == TNT_TK_NUM32);
	TT_ASSERT(tnt_lex(&l, &tk2) == TNT_TK_STRING);
	TT_ASSERT(tnt_lex(&l, &tk3) == ',');
	TT_ASSERT(tnt_lex(&l, &tk4) == '.');
	TT_ASSERT(tnt_lex(&l, &tk5) == TNT_TK_NUM32);
	TT_ASSERT(tnt_lex(&l, &tk6) == TNT_TK_EOF);
	tnt_lex_free(&l);
}

/* lex bad string 1 */
static void tt_tnt_lex_badstr1(struct tt_test *test) {
	unsigned char sz[] = "  '";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk;
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ERROR);
	tnt_lex_free(&l);

}

/* lex bad string 2 */
static void tt_tnt_lex_badstr2(struct tt_test *test) {
	unsigned char sz[] = "  '\n'";
	struct tnt_lex l;
	tnt_lex_init(&l, sz, sizeof(sz) - 1);
	struct tnt_tk *tk;
	TT_ASSERT(tnt_lex(&l, &tk) == TNT_TK_ERROR);
	tnt_lex_free(&l);
}

/* sql ping */
static void tt_tnt_sql_ping(struct tt_test *test) {
	char *e = NULL;
	char q[] = "PING";
	TT_ASSERT(tnt_query(&net, q, sizeof(q) - 1, &e) == 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_PING);
	}
	tnt_iter_free(&i);
}

/* sql insert */
static void tt_tnt_sql_insert(struct tt_test *test) {
	char *e = NULL;
	char q[] = "insert into t0 values (222, 'baz')";
	TT_ASSERT(tnt_query(&net, q, sizeof(q) - 1, &e) == 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_INSERT);
		TT_ASSERT(r->count == 1);
	}
	tnt_iter_free(&i);
}

/* sql update */
static void tt_tnt_sql_update(struct tt_test *test) {
	char *e;
	char q1[] = "update t0 set k0 = 7 where k0 = 222";
	TT_ASSERT(tnt_query(&net, q1, sizeof(q1) - 1, &e) == 0);
	/* 7 + 1 = 8 */
	char q2[] = "update t0 set k0 = k0 + 1 where k0 = 7";
	TT_ASSERT(tnt_query(&net, q2, sizeof(q2) - 1, &e) == 0);
	/* 8 | 2 = 10 */
	char q3[] = "update t0 set k0 = k0 | 2 where k0 = 8";
	TT_ASSERT(tnt_query(&net, q3, sizeof(q3) - 1, &e) == 0);
	/* 10 & 2 = 2 */
	char q4[] = "update t0 set k0 = k0 & 2 where k0 = 10";
	TT_ASSERT(tnt_query(&net, q4, sizeof(q4) - 1, &e) == 0);
	/* 2 ^ 123 = 121 */
	char q5[] = "update t0 set k0 = k0 ^ 123 where k0 = 2";
	TT_ASSERT(tnt_query(&net, q5, sizeof(q5) - 1, &e) == 0);
	/* assign */
	char q6[] = "update t0 set k0 = 222, k1 = 'hello world' where k0 = 121";
	TT_ASSERT(tnt_query(&net, q6, sizeof(q6) - 1, &e) == 0);
	/* splice */
	char q7[] = "update t0 set k1 = splice(k1, 0, 2, 'AB') where k0 = 222";
	TT_ASSERT(tnt_query(&net, q7, sizeof(q7) - 1, &e) == 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_UPDATE);
		TT_ASSERT(r->count == 1);
	}
	tnt_iter_free(&i);
}

/* sql select */
static void tt_tnt_sql_select(struct tt_test *test) {
	char *e = NULL;
	char q[] = "select * from t0 where k0 = 222 or k0 = 222";
	TT_ASSERT(tnt_query(&net, q, sizeof(q) - 1, &e) == 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_SELECT);
		TT_ASSERT(r->count == 2);
		struct tnt_iter il;
		tnt_iter_list(&il, TNT_REPLY_LIST(r));
		TT_ASSERT(tnt_next(&il) == 1);
		struct tnt_tuple *tp;
		tp = TNT_ILIST_TUPLE(&il);
		TT_ASSERT(tp->cardinality == 2);
		TT_ASSERT(tp->alloc == 1);
		TT_ASSERT(tp->data != NULL);
		TT_ASSERT(tp->size != 0);
		struct tnt_iter ifl;
		tnt_iter(&ifl, tp);
		TT_ASSERT(tnt_next(&ifl) == 1);
		TT_ASSERT(TNT_IFIELD_IDX(&ifl) == 0);
		TT_ASSERT(TNT_IFIELD_SIZE(&ifl) == 4);
		TT_ASSERT(*(uint32_t*)TNT_IFIELD_DATA(&ifl) == 222);
		TT_ASSERT(tnt_next(&ifl) == 1);
		TT_ASSERT(TNT_IFIELD_IDX(&ifl) == 1);
		TT_ASSERT(TNT_IFIELD_SIZE(&ifl) == 11);
		TT_ASSERT(memcmp(TNT_IFIELD_DATA(&ifl), "ABllo world", 11) == 0);
		TT_ASSERT(tnt_next(&ifl) == 0);
		tnt_iter_free(&ifl);
		tnt_iter_free(&il);
	}
	tnt_iter_free(&i);
}

/* sql select limit */
static void tt_tnt_sql_select_limit(struct tt_test *test) {
	char *e = NULL;
	char q[] = "select * from t0 where k0 = 222 limit 0";
	TT_ASSERT(tnt_query(&net, q, sizeof(q) - 1, &e) == 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_SELECT);
		TT_ASSERT(r->count == 0);
	}
	tnt_iter_free(&i);
}

/* sql delete */
static void tt_tnt_sql_delete(struct tt_test *test) {
	char *e = NULL;
	char q[] = "delete from t0 where k0 = 222";
	TT_ASSERT(tnt_query(&net, q, sizeof(q) - 1, &e) == 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_DELETE);
		TT_ASSERT(r->count == 1);
	}
	tnt_iter_free(&i);
}

/* sql call */
static void tt_tnt_sql_call(struct tt_test *test) {
	char *e = NULL;
	char q[] = "call box.insert(0, 454, 'abc', 'cba')";
	TT_ASSERT(tnt_query(&net, q, sizeof(q) - 1, &e) == 0);
	TT_ASSERT(tnt_flush(&net) > 0);
	struct tnt_iter i;
	tnt_iter_reply(&i, &net);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		TT_ASSERT(r->code == 0);
		TT_ASSERT(r->op == TNT_OP_CALL);
		TT_ASSERT(r->count == 1);
	}
	tnt_iter_free(&i);
}

int
main(int argc, char * argv[])
{
	(void)argc, (void)argv;

	struct tt_list t;
	memset(&t, 0, sizeof(t));

	/* common data manipulation */
	tt_test(&t, "tuple1", tt_tnt_tuple1);
	tt_test(&t, "tuple2", tt_tnt_tuple2);
	tt_test(&t, "list", tt_tnt_list);
	tt_test(&t, "stream buffer", tt_tnt_sbuf);
	tt_test(&t, "tuple set", tt_tnt_tuple_set);
	tt_test(&t, "iterator tuple", tt_tnt_iter1);
	tt_test(&t, "iterator tuple (single field)", tt_tnt_iter11);
	tt_test(&t, "iterator tuple (tnt_field)", tt_tnt_iter2);
	tt_test(&t, "iterator list", tt_tnt_iter3);
	/* marshaling */
	tt_test(&t, "marshaling ping", tt_tnt_marshal_ping);
	tt_test(&t, "marshaling insert", tt_tnt_marshal_insert);
	tt_test(&t, "marshaling delete", tt_tnt_marshal_delete);
	tt_test(&t, "marshaling call", tt_tnt_marshal_call);
	tt_test(&t, "marshaling select", tt_tnt_marshal_select);
	tt_test(&t, "marshaling update", tt_tnt_marshal_update);
	/* common operations */
	tt_test(&t, "connect", tt_tnt_net_connect);
	tt_test(&t, "ping", tt_tnt_net_ping);
	tt_test(&t, "insert", tt_tnt_net_insert);
	tt_test(&t, "update", tt_tnt_net_update);
	tt_test(&t, "select", tt_tnt_net_select);
	tt_test(&t, "delete", tt_tnt_net_delete);
	tt_test(&t, "call", tt_tnt_net_call);
	tt_test(&t, "call (no args)", tt_tnt_net_call_na);
	tt_test(&t, "reply", tt_tnt_net_reply);
	/* sql lexer */
	tt_test(&t, "lex ws", tt_tnt_lex_ws);
	tt_test(&t, "lex integer", tt_tnt_lex_int);
	tt_test(&t, "lex string", tt_tnt_lex_str);
	tt_test(&t, "lex punctuation", tt_tnt_lex_punct);
	tt_test(&t, "lex ids", tt_tnt_lex_ids);
	tt_test(&t, "lex keywords", tt_tnt_lex_kw);
	tt_test(&t, "lex keys and tables", tt_tnt_lex_kt);
	tt_test(&t, "lex stack", tt_tnt_lex_stack);
	tt_test(&t, "lex bad string1", tt_tnt_lex_badstr1);
	tt_test(&t, "lex bad string2", tt_tnt_lex_badstr2);
	/* sql stmts */
	tt_test(&t, "sql ping", tt_tnt_sql_ping);
	tt_test(&t, "sql insert", tt_tnt_sql_insert);
	tt_test(&t, "sql update", tt_tnt_sql_update);
	tt_test(&t, "sql select", tt_tnt_sql_select);
	tt_test(&t, "sql select limit", tt_tnt_sql_select_limit);
	tt_test(&t, "sql delete", tt_tnt_sql_delete);
	tt_test(&t, "sql call", tt_tnt_sql_call);

	tt_run(&t);
	tt_free(&t);

	tnt_stream_free(&net);
	return 0;
}
