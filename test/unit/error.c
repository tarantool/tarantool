/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/error.h"
#include "diag.h"
#include "error_payload.h"
#include "fiber.h"
#include "memory.h"
#include "mp_uuid.h"
#include "msgpuck.h"
#include "random.h"
#include "ssl_error.h"
#include "vclock/vclock.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#include <float.h>
#include <pthread.h>

static void
test_payload_field_str(void)
{
	header();
	plan(15);

	struct error_payload p;
	error_payload_create(&p);
	is(p.count, 0, "no fields in the beginning");
	is(error_payload_get_str(&p, "key"), NULL, "get_str() empty");

	error_payload_set_str(&p, "key1", "value1");
	is(p.count, 1, "++count");
	is(strcmp(error_payload_get_str(&p, "key1"), "value1"), 0,
	   "get_str() finds");

	error_payload_set_str(&p, "key2", "value2");
	is(p.count, 2, "++count");
	is(strcmp(error_payload_get_str(&p, "key1"), "value1"), 0,
	   "get_str() finds old");
	is(strcmp(error_payload_get_str(&p, "key2"), "value2"), 0,
	   "get_str() finds new");
	is(error_payload_find(&p, "key1")->size,
	   mp_sizeof_str(strlen("value1")),
	   "size does not include terminating zero");

	error_payload_set_str(&p, "key1", "new_value1");
	is(p.count, 2, "field count is same");
	is(strcmp(error_payload_get_str(&p, "key1"), "new_value1"), 0,
	   "get_str() finds new value");
	is(strcmp(error_payload_get_str(&p, "key2"), "value2"), 0,
	   "get_str() finds other key old value");

	error_payload_clear(&p, "key2");
	is(p.count, 1, "--count");
	is(strcmp(error_payload_get_str(&p, "key1"), "new_value1"), 0,
	   "get_str() finds new value");
	is(error_payload_get_str(&p, "key2"), NULL,
	   "get_str() can't find deleted value");

	error_payload_set_uint(&p, "key2", 1);
	is(error_payload_get_str(&p, "key2"), NULL, "wrong type");

	error_payload_destroy(&p);

	check_plan();
	footer();
}

static void
test_payload_field_uint(void)
{
	header();
	plan(17);

	struct error_payload p;
	error_payload_create(&p);
	uint64_t val = 1;
	ok(!error_payload_get_uint(&p, "key", &val) && val == 0,
	   "get_uint() empty");

	error_payload_set_uint(&p, "key1", 1);
	is(p.count, 1, "++count");
	ok(error_payload_get_uint(&p, "key1", &val), "get_uint() finds");
	is(val, 1, "value match");

	val = 100;
	ok(!error_payload_get_uint(&p, "key2", &val), "get_uint() fails");
	is(val, 0, "value is default");

	is(error_payload_find(&p, "key1")->size, mp_sizeof_uint(1),
	   "small number size");

	error_payload_set_uint(&p, "key2", UINT32_MAX);
	ok(error_payload_get_uint(&p, "key2", &val), "get_uint() 32 bit");
	is(val, UINT32_MAX, "value match");
	is(error_payload_find(&p, "key2")->size, mp_sizeof_uint(UINT32_MAX),
	   "middle number size");
	is(p.count, 2, "field count is same");

	error_payload_set_uint(&p, "key1", UINT64_MAX);
	ok(error_payload_get_uint(&p, "key1", &val) && val == UINT64_MAX,
	   "value 1");
	ok(error_payload_get_uint(&p, "key2", &val) && val == UINT32_MAX,
	   "value 2");

	error_payload_clear(&p, "key2");
	is(p.count, 1, "--count");
	ok(error_payload_get_uint(&p, "key1", &val) && val == UINT64_MAX,
	   "remained value");
	ok(!error_payload_get_uint(&p, "key2", &val) && val == 0,
	   "deleted value");

	error_payload_set_str(&p, "key2", "1");
	ok(!error_payload_get_uint(&p, "key2", &val) && val == 0, "wrong type");

	error_payload_destroy(&p);

	check_plan();
	footer();
}

static void
test_payload_field_int(void)
{
	header();
	plan(20);

	struct error_payload p;
	error_payload_create(&p);
	int64_t val = 1;
	ok(!error_payload_get_int(&p, "key", &val) && val == 0,
	   "get_int() empty");

	error_payload_set_int(&p, "key1", 1);
	is(p.count, 1, "++count");
	ok(error_payload_get_int(&p, "key1", &val), "get_int() finds");
	is(val, 1, "value match");

	val = 100;
	ok(!error_payload_get_int(&p, "key2", &val), "get_int() fails");
	is(val, 0, "value is default");

	is(error_payload_find(&p, "key1")->size, mp_sizeof_uint(1),
	   "small number size");

	error_payload_set_int(&p, "key2", UINT32_MAX);
	ok(error_payload_get_int(&p, "key2", &val), "get_int() 32 bit");
	is(val, UINT32_MAX, "value match");
	is(error_payload_find(&p, "key2")->size, mp_sizeof_uint(UINT32_MAX),
	   "middle number size");
	is(p.count, 2, "field count is same");

	error_payload_set_int(&p, "key1", INT64_MAX);
	ok(error_payload_get_int(&p, "key1", &val) && val == INT64_MAX,
	   "value 1");
	ok(error_payload_get_int(&p, "key2", &val) && val == UINT32_MAX,
	   "value 2");

	error_payload_clear(&p, "key2");
	is(p.count, 1, "--count");
	ok(error_payload_get_int(&p, "key1", &val) && val == INT64_MAX,
	   "remained value");
	ok(!error_payload_get_int(&p, "key2", &val) && val == 0,
	   "deleted value");

	error_payload_set_str(&p, "key2", "1");
	ok(!error_payload_get_int(&p, "key2", &val) && val == 0, "wrong type");

	error_payload_set_uint(&p, "key2", (uint64_t)INT64_MAX + 1);
	ok(!error_payload_get_int(&p, "key2", &val) && val == 0, "overflow");

	error_payload_set_uint(&p, "key2", 100);
	ok(error_payload_get_int(&p, "key2", &val) && val == 100, "conversion");

	error_payload_set_int(&p, "key2", INT64_MIN);
	ok(error_payload_get_int(&p, "key2", &val) && val == INT64_MIN,
	   "min value");

	error_payload_destroy(&p);

	check_plan();
	footer();
}

static void
test_payload_field_double(void)
{
	header();
	plan(14);

	struct error_payload p;
	error_payload_create(&p);
	double val = 1;
	ok(!error_payload_get_double(&p, "key", &val) && val == 0,
	   "get_double() empty");

	error_payload_set_double(&p, "key1", 1.5);
	is(p.count, 1, "++count");
	ok(error_payload_get_double(&p, "key1", &val), "get_double() finds");
	is(val, 1.5, "value match");

	val = 1;
	ok(!error_payload_get_double(&p, "key2", &val), "get_double() fails");
	is(val, 0, "value is default");

	is(error_payload_find(&p, "key1")->size, mp_sizeof_double(1.5), "size");

	error_payload_set_double(&p, "key2", DBL_MAX);
	ok(error_payload_get_double(&p, "key1", &val) && val == 1.5, "value 1");
	ok(error_payload_get_double(&p, "key2", &val) && val == DBL_MAX,
	   "value 2");

	error_payload_clear(&p, "key2");
	is(p.count, 1, "--count");
	ok(error_payload_get_double(&p, "key1", &val) && val == 1.5,
	   "remained value");
	ok(!error_payload_get_double(&p, "key2", &val) && val == 0,
	   "deleted value");

	error_payload_set_str(&p, "key2", "1");
	ok(!error_payload_get_double(&p, "key2", &val) && val == 0,
	   "wrong type");

	char buffer[16];
	char *data = mp_encode_float(buffer, 0.5);
	error_payload_set_mp(&p, "key2", buffer, data - buffer);
	ok(error_payload_get_double(&p, "key2", &val) && val == 0.5,
	   "float 0.5");

	error_payload_destroy(&p);

	check_plan();
	footer();
}

static void
test_payload_field_bool(void)
{
	header();
	plan(13);

	struct error_payload p;
	error_payload_create(&p);
	bool val = true;
	ok(!error_payload_get_bool(&p, "key", &val) && !val,
	   "get_bool() empty");

	error_payload_set_bool(&p, "key1", true);
	is(p.count, 1, "++count");
	ok(error_payload_get_bool(&p, "key1", &val), "get_bool() finds");
	ok(val, "value match");

	val = true;
	ok(!error_payload_get_bool(&p, "key2", &val), "get_bool() fails");
	ok(!val, "value is default");

	error_payload_set_bool(&p, "key2", false);
	ok(error_payload_get_bool(&p, "key2", &val), "get_bool() finds");
	ok(!val, "value match");

	is(error_payload_find(&p, "key1")->size, mp_sizeof_bool(true), "size");

	error_payload_clear(&p, "key2");
	is(p.count, 1, "--count");
	ok(error_payload_get_bool(&p, "key1", &val) && val, "remained value");
	ok(!error_payload_get_bool(&p, "key2", &val) && !val, "deleted value");

	error_payload_set_str(&p, "key2", "true");
	ok(!error_payload_get_bool(&p, "key2", &val) && !val, "wrong type");

	error_payload_destroy(&p);

	check_plan();
	footer();
}

static void
test_payload_field_uuid(void)
{
	header();
	plan(17);

	struct error_payload p;
	error_payload_create(&p);
	struct tt_uuid val1;
	tt_uuid_create(&val1);
	ok(!error_payload_get_uuid(&p, "key", &val1), "get_uuid() empty");
	ok(tt_uuid_is_nil(&val1), "default value");

	tt_uuid_create(&val1);
	error_payload_set_uuid(&p, "key1", &val1);
	is(p.count, 1, "++count");
	struct tt_uuid val2;
	ok(error_payload_get_uuid(&p, "key1", &val2), "get_uuid() finds");
	ok(tt_uuid_is_equal(&val1, &val2), "value match");

	ok(!error_payload_get_uuid(&p, "key2", &val2), "get_uuid() fails");
	ok(tt_uuid_is_nil(&val2), "value is default");

	tt_uuid_create(&val2);
	error_payload_set_uuid(&p, "key2", &val2);
	struct tt_uuid val3;
	ok(error_payload_get_uuid(&p, "key2", &val3), "get_uuid() finds");
	ok(tt_uuid_is_equal(&val3, &val2), "value match");

	is(error_payload_find(&p, "key1")->size, mp_sizeof_uuid(), "size");

	error_payload_clear(&p, "key2");
	is(p.count, 1, "--count");
	ok(error_payload_get_uuid(&p, "key1", &val3), "remained value");
	ok(tt_uuid_is_equal(&val1, &val3), "value match");
	ok(!error_payload_get_uuid(&p, "key2", &val3), "deleted value");
	ok(tt_uuid_is_nil(&val3), "value match");

	error_payload_set_str(&p, "key2", "1");
	ok(!error_payload_get_uuid(&p, "key2", &val3), "wrong type");
	ok(tt_uuid_is_nil(&val3), "value match");

	error_payload_destroy(&p);

	check_plan();
	footer();
}

static void
test_payload_field_mp(void)
{
	header();
	plan(6);
	char buf[1024];
	char *data;
	const char *cdata;
	uint32_t size;

	struct error_payload p;
	error_payload_create(&p);

	data = mp_encode_str(buf, "value1", 6);
	error_payload_set_mp(&p, "key1", buf, data - buf);
	is(strcmp(error_payload_get_str(&p, "key1"), "value1"), 0, "mp str");

	cdata = error_payload_get_mp(&p, "key1", &size);
	is(memcmp(cdata, buf, size), 0, "mp str cmp");

	data = mp_encode_uint(buf, 100);
	error_payload_set_mp(&p, "key2", buf, data - buf);
	uint64_t val;
	ok(error_payload_get_uint(&p, "key2", &val) && val == 100, "mp uint");

	cdata = error_payload_get_mp(&p, "key2", &size);
	is(memcmp(cdata, buf, size), 0, "mp uint cmp");

	data = mp_encode_array(buf, 1);
	data = mp_encode_uint(data, 2);
	error_payload_set_mp(&p, "key3", buf, data - buf);

	cdata = error_payload_get_mp(&p, "key3", &size);
	is(memcmp(cdata, buf, size), 0, "mp array");

	ok(!error_payload_get_uint(&p, "key3", &val) && val == 0,
	   "mp uint from array");

	error_payload_destroy(&p);

	check_plan();
	footer();
}

static void
test_payload_clear(void)
{
	header();
	plan(13);

	struct error_payload p;
	error_payload_create(&p);

	error_payload_set_uint(&p, "key1", 1);
	error_payload_set_uint(&p, "key2", 2);
	error_payload_set_uint(&p, "key3", 3);
	error_payload_set_uint(&p, "key4", 4);
	error_payload_set_uint(&p, "key5", 5);

	error_payload_clear(&p, "key5");
	is(p.count, 4, "clear last, count");
	is(error_payload_find(&p, "key5"), NULL, "clear last, key");

	error_payload_clear(&p, "key1");
	is(p.count, 3, "clear first, count");
	is(error_payload_find(&p, "key1"), NULL, "clear first, key");

	uint64_t val;
	ok(error_payload_get_uint(&p, "key2", &val) && val == 2, "check key2");
	ok(error_payload_get_uint(&p, "key3", &val) && val == 3, "check key3");
	ok(error_payload_get_uint(&p, "key4", &val) && val == 4, "check key4");

	is(strcmp(p.fields[0]->name, "key4"), 0, "deletion is cyclic");

	error_payload_clear(&p, "key2");
	is(p.count, 2, "clear middle, count");
	is(error_payload_find(&p, "key2"), NULL, "clear middle, key");
	ok(error_payload_get_uint(&p, "key3", &val) && val == 3, "check key3");
	ok(error_payload_get_uint(&p, "key4", &val) && val == 4, "check key4");

	error_payload_clear(&p, "key3");
	error_payload_clear(&p, "key4");

	is(p.count, 0, "clear all");

	error_payload_destroy(&p);

	check_plan();
	footer();
}

static void
test_payload_move(void)
{
	header();
	plan(7);

	struct error_payload p1, p2;
	error_payload_create(&p1);
	error_payload_create(&p2);

	error_payload_move(&p1, &p2);
	ok(p1.count == 0 && p1.fields == NULL, "empty");

	error_payload_set_str(&p1, "key", "value");
	error_payload_move(&p1, &p2);
	ok(p1.count == 0 && p1.fields == NULL, "emptied on move");

	error_payload_set_str(&p1, "key", "value");
	error_payload_set_str(&p2, "key1", "value1");
	error_payload_set_str(&p2, "key2", "value2");
	error_payload_move(&p1, &p2);
	is(p1.count, 2, "got 2 fields");
	isnt(p1.fields, NULL, "got 2 fields");
	is(strcmp(error_payload_get_str(&p1, "key1"), "value1"), 0, "key1");
	is(strcmp(error_payload_get_str(&p1, "key2"), "value2"), 0, "key2");
	is(error_payload_get_str(&p1, "key"), NULL, "key");

	error_payload_destroy(&p2);
	error_payload_destroy(&p1);

	check_plan();
	footer();
}

static void
test_error_code(void)
{
	header();
	plan(9);

	diag_set(ClientError, ER_READONLY);
	is(box_error_code(box_error_last()), ER_READONLY, "ClientError");
	diag_set(OutOfMemory, 42, "foo", "bar");
	is(box_error_code(box_error_last()), ER_MEMORY_ISSUE, "OutOfMemory");
	diag_set(SystemError, "foo");
	is(box_error_code(box_error_last()), ER_SYSTEM, "SystemError");
	diag_set(SocketError, "foo", "bar");
	is(box_error_code(box_error_last()), ER_SYSTEM, "SocketError");
	diag_set(TimedOut);
	is(box_error_code(box_error_last()), ER_SYSTEM, "TimedOut");
	diag_set(SSLError, "foo");
	is(box_error_code(box_error_last()), ER_SSL, "SSLError");
	diag_set(CollationError, "foo");
	is(box_error_code(box_error_last()), ER_CANT_CREATE_COLLATION,
	   "CollationError");
	struct vclock vclock;
	vclock_create(&vclock);
	diag_set(XlogGapError, &vclock, &vclock);
	is(box_error_code(box_error_last()), ER_XLOG_GAP, "XlogGapError");
	diag_set(FiberIsCancelled);
	is(box_error_code(box_error_last()), ER_PROC_LUA, "FiberIsCancelled");

	check_plan();
	footer();
}

static void
error_destroy(struct error *e)
{
	/* Intentionally left blank. */
}

static void
test_error_format_msg(void)
{
	header();
	plan(6);

	char msg[DIAG_ERRMSG_MAX + 1];
	struct error e;
	error_create(&e, error_destroy, NULL, NULL, NULL, NULL, 0);
	error_ref(&e);

	/* Test largest message that fits into statically allocated buffer. */
	for (size_t i = 0; i < DIAG_ERRMSG_MAX - 1; i++)
		msg[i] = pseudo_random_in_range('a', 'z');
	msg[DIAG_ERRMSG_MAX - 1] = '\0';
	error_format_msg(&e, msg);
	is(strcmp(box_error_message(&e), msg), 0, "errmsg is correct");
	is(box_error_message(&e), e.errmsg_buf,
	   "errmsg is statically allocated (%d characters)", strlen(msg));

	/* This message doesn't fit into the static buffer. */
	msg[DIAG_ERRMSG_MAX - 1] = '.';
	msg[DIAG_ERRMSG_MAX] = '\0';
	error_format_msg(&e, msg);
	is(strcmp(box_error_message(&e), msg), 0, "errmsg is correct");
	isnt(box_error_message(&e), e.errmsg_buf,
	     "errmsg is dynamically allocated (%d characters)", strlen(msg));

	/* This message fits into the static buffer again. */
	msg[17] = '\0';
	error_format_msg(&e, msg);
	is(strcmp(box_error_message(&e), msg), 0, "errmsg is correct");
	is(box_error_message(&e), e.errmsg_buf,
	   "errmsg is statically allocated (%d characters)", strlen(msg));

	error_unref(&e);

	check_plan();
	footer();
}

static void
test_error_append_msg(void)
{
	header();
	plan(5);

	char msg[DIAG_ERRMSG_MAX];
	struct error e;
	error_create(&e, error_destroy, NULL, NULL, NULL, NULL, 0);
	error_ref(&e);

	error_format_msg(&e, "Message");
	is(box_error_message(&e), e.errmsg_buf,
	   "errmsg is statically allocated (%d characters)",
	   strlen(box_error_message(&e)));

	error_append_msg(&e, "/%s/%s/%d/", "foo", "bar", 123);
	is(strcmp(box_error_message(&e), "Message/foo/bar/123/"), 0,
	   "errmsg is correct");
	is(box_error_message(&e), e.errmsg_buf,
	   "errmsg is statically allocated (%d characters)",
	   strlen(box_error_message(&e)));

	for (size_t i = 0; i < DIAG_ERRMSG_MAX - 1; i++)
		msg[i] = pseudo_random_in_range('a', 'z');
	msg[DIAG_ERRMSG_MAX - 1] = '\0';
	error_append_msg(&e, msg);
	isnt(box_error_message(&e), e.errmsg_buf,
	     "errmsg is dynamically allocated (%d characters)",
	     strlen(box_error_message(&e)));

	error_append_msg(&e, "%d/%d/%d", 1, 2, 3);
	isnt(box_error_message(&e), e.errmsg_buf,
	     "errmsg is dynamically allocated (%d characters)",
	     strlen(box_error_message(&e)));

	error_unref(&e);

	check_plan();
	footer();
}

static void *
test_pthread_f(void *arg)
{
	(void)arg;
	is(box_error_last(), NULL, "last error before set");
	box_error_raise(ER_ILLEGAL_PARAMS, "Test %d", 42);
	box_error_t *err = box_error_last();
	isnt(err, NULL, "last error after set");
	is(strcmp(box_error_type(err), "ClientError"), 0, "last error type");
	is(box_error_code(err), ER_ILLEGAL_PARAMS, "last error code");
	is(strcmp(box_error_message(err), "Test 42"), 0, "last error message");
	box_error_clear();
	is(box_error_last(), NULL, "last error after clear");
	return NULL;
}

static void
test_pthread(void)
{
	header();
	plan(6);

	pthread_attr_t attr;
	fail_unless(pthread_attr_init(&attr) == 0);
	fail_unless(pthread_attr_setdetachstate(
			&attr, PTHREAD_CREATE_JOINABLE) == 0);

	pthread_t thread;
	fail_unless(pthread_create(&thread, &attr, test_pthread_f, NULL) == 0);
	fail_unless(pthread_join(thread, NULL) == 0);

	check_plan();
	footer();
}

int
main(void)
{
	header();
	plan(13);

	random_init();
	memory_init();
	fiber_init(fiber_c_invoke);

	test_payload_field_str();
	test_payload_field_uint();
	test_payload_field_int();
	test_payload_field_double();
	test_payload_field_bool();
	test_payload_field_uuid();
	test_payload_field_mp();
	test_payload_clear();
	test_payload_move();
	test_error_code();
	test_error_format_msg();
	test_error_append_msg();
	test_pthread();

	fiber_free();
	memory_free();
	random_free();

	footer();
	return check_plan();
}
