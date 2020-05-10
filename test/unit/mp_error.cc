/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and iproto forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in iproto form must reproduce the above
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
#include "unit.h"
#include "exception.h"
#include "fiber.h"
#include "memory.h"
#include "msgpuck.h"
#include "mp_extension_types.h"
#include "tt_static.h"
#include "small/ibuf.h"
#include "mpstream/mpstream.h"

#include "box/error.h"
#include "box/mp_error.h"

enum {
	MP_ERROR_STACK = 0x00
};

enum {
	MP_ERROR_TYPE = 0x00,
	MP_ERROR_FILE = 0x01,
	MP_ERROR_LINE = 0x02,
	MP_ERROR_MESSAGE = 0x03,
	MP_ERROR_ERRNO = 0x04,
	MP_ERROR_CODE = 0x05,
	MP_ERROR_FIELDS = 0x06
};

struct mp_test_error {
	uint32_t code;
	uint32_t line;
	int32_t saved_errno;
	uint32_t unknown_uint_field;
	const char *type;
	const char *file;
	const char *message;
	const char *custom_type;
	const char *ad_object_type;
	const char *ad_object_name;
	const char *ad_access_type;
	const char *unknown_str_field;
};

const char *standard_errors[] = {
	"XlogError",
	"XlogGapError",
	"SystemError",
	"SocketError",
	"OutOfMemory",
	"TimedOut",
	"ChannelIsClosed",
	"FiberIsCancelled",
	"LuajitError",
	"IllegalParams",
	"CollationError",
	"SwimError",
	"CryptoError",
};

enum {
	TEST_STANDARD_ERRORS_NUM =
		sizeof(standard_errors) / sizeof(standard_errors[0]),
};

static inline char *
mp_encode_str0(char *data, const char *str)
{
	return mp_encode_str(data, str, strlen(str));
}

/** Note, not the same as mp_encode_error(). */
static char *
mp_encode_mp_error(const struct mp_test_error *e, char *data)
{
	uint32_t fields_num = 6;
	int field_count = (e->custom_type != NULL) +
			  (e->ad_object_type != NULL) +
			  (e->ad_object_name != NULL) +
			  (e->ad_access_type != NULL) +
			  (e->unknown_str_field != NULL);
	fields_num += (field_count != 0);
	fields_num += (e->unknown_uint_field != 0);

	data = mp_encode_map(data, fields_num);
	data = mp_encode_uint(data, MP_ERROR_TYPE);
	data = mp_encode_str0(data, e->type);
	data = mp_encode_uint(data, MP_ERROR_FILE);
	data = mp_encode_str0(data, e->file);
	data = mp_encode_uint(data, MP_ERROR_LINE);
	data = mp_encode_uint(data, e->line);
	data = mp_encode_uint(data, MP_ERROR_MESSAGE);
	data = mp_encode_str0(data, e->message);
	data = mp_encode_uint(data, MP_ERROR_ERRNO);
	data = mp_encode_uint(data, e->saved_errno);
	data = mp_encode_uint(data, MP_ERROR_CODE);
	data = mp_encode_uint(data, e->code);
	if (e->unknown_uint_field != 0) {
		data = mp_encode_uint(data, UINT64_MAX);
		data = mp_encode_uint(data, e->unknown_uint_field);
	}
	if (field_count != 0) {
		data = mp_encode_uint(data, MP_ERROR_FIELDS);
		data = mp_encode_map(data, field_count);
		if (e->custom_type != NULL) {
			data = mp_encode_str0(data, "custom_type");
			data = mp_encode_str0(data, e->custom_type);
		}
		if (e->ad_object_type != NULL) {
			data = mp_encode_str0(data, "object_type");
			data = mp_encode_str0(data, e->ad_object_type);
		}
		if (e->ad_object_name != NULL) {
			data = mp_encode_str0(data, "object_name");
			data = mp_encode_str0(data, e->ad_object_name);
		}
		if (e->ad_access_type != NULL) {
			data = mp_encode_str0(data, "access_type");
			data = mp_encode_str0(data, e->ad_access_type);
		}
		if (e->unknown_str_field != NULL) {
			data = mp_encode_str0(data, "unknown_field");
			data = mp_encode_str0(data, e->unknown_str_field);
		}
	}
	return data;
}

static char *
mp_encode_error_header(char *data, int stack_size)
{
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, MP_ERROR_STACK);
	data = mp_encode_array(data, stack_size);
	return data;
}

static char *
mp_encode_test_error_stack(char *data)
{
	data = mp_encode_error_header(data, TEST_STANDARD_ERRORS_NUM + 3);
	/*
	 * CustomError
	 */
	struct mp_test_error err;
	memset(&err, 0, sizeof(err));
	err.code = 123;
	err.line = 1;
	err.saved_errno = 2;
	err.type = "CustomError";
	err.file = "File1";
	err.message = "Message1";
	err.custom_type = "MyType";
	data = mp_encode_mp_error(&err, data);
	/*
	 * AccessDeniedError
	 */
	memset(&err, 0, sizeof(err));
	err.code = 42;
	err.line = 3;
	err.saved_errno = 4;
	err.type = "AccessDeniedError";
	err.file = "File2";
	err.message = "Message2";
	err.ad_object_type = "ObjectType";
	err.ad_object_name = "ObjectName";
	err.ad_access_type = "AccessType";
	data = mp_encode_mp_error(&err, data);
	/*
	 * ClientError
	 */
	memset(&err, 0, sizeof(err));
	err.code = 123;
	err.line = 5;
	err.saved_errno = 6;
	err.type = "ClientError";
	err.file = "File3";
	err.message = "Message4";
	data = mp_encode_mp_error(&err, data);

	/*
	 * All errors with standard fields only.
	 */
	for (uint8_t i = 0; i < TEST_STANDARD_ERRORS_NUM; ++i) {
		memset(&err, 0, sizeof(err));
		err.code = i;
		err.line = i;
		err.saved_errno = i;
		err.type = standard_errors[i];
		err.file = tt_sprintf("File%d", i);
		err.message = tt_sprintf("Message%d", i);
		data = mp_encode_mp_error(&err, data);
	}

	return data;
}

static bool
error_is_eq_mp_error(struct error *err, struct mp_test_error *check)
{
	if (err->saved_errno != check->saved_errno)
		return false;
	if (strcmp(err->type->name, check->type) != 0)
		return false;
	if (strcmp(err->file, check->file) != 0)
		return false;
	if (err->line != check->line)
		return false;
	if (strcmp(err->errmsg, check->message) != 0)
		return false;

	if (strcmp(check->type, "ClientError") == 0) {
		if (box_error_code(err) != check->code)
			return false;
	} else if (strcmp(check->type, "CustomError") == 0) {
		CustomError *cust_err = type_cast(CustomError, err);
		if (box_error_code(err) != check->code ||
		    strcmp(cust_err->custom_type(), check->custom_type) != 0)
			return false;
	} else if (strcmp(check->type, "AccessDeniedError") == 0) {
		AccessDeniedError *ad_err = type_cast(AccessDeniedError, err);
		if (box_error_code(err) != check->code ||
		    strcmp(ad_err->access_type(), check->ad_access_type) != 0 ||
		    strcmp(ad_err->object_name(), check->ad_object_name) != 0 ||
		    strcmp(ad_err->object_type(), check->ad_object_type) != 0)
			return false;
	}
	return true;
}

void
test_stack_error_decode()
{
	header();
	plan(TEST_STANDARD_ERRORS_NUM + 4);

	char buffer[2048];
	memset(buffer, 0, sizeof(buffer));
	char *end = mp_encode_test_error_stack(buffer);

	uint32_t len = end - buffer;
	const char *pos = buffer;
	struct error *err1 = error_unpack(&pos, len);
	error_ref(err1);
	struct error *err2 = err1->cause;
	struct error *err3 = err2->cause;

	struct mp_test_error check;
	memset(&check, 0, sizeof(check));
	check.code = 123;
	check.line = 1;
	check.saved_errno = 2;
	check.type = "CustomError";
	check.file = "File1";
	check.message = "Message1";
	check.custom_type = "MyType";
	ok(error_is_eq_mp_error(err1, &check), "check CustomError");

	memset(&check, 0, sizeof(check));
	check.code = 42;
	check.line = 3;
	check.saved_errno = 4;
	check.type = "AccessDeniedError";
	check.file = "File2";
	check.message = "Message2";
	check.ad_object_type = "ObjectType";
	check.ad_object_name = "ObjectName";
	check.ad_access_type = "AccessType";
	ok(error_is_eq_mp_error(err2, &check), "check AccessDeniedError");

	memset(&check, 0, sizeof(check));
	check.code = 123;
	check.line = 5;
	check.saved_errno = 6;
	check.type = "ClientError";
	check.file = "File3";
	check.message = "Message4";
	ok(error_is_eq_mp_error(err3, &check), "check ClientError");

	struct error *cur_err = err3;
	int i = 0;
	while(cur_err->cause) {
		cur_err = cur_err->cause;
		memset(&check, 0, sizeof(check));
		check.code = i;
		check.line = i;
		check.saved_errno = i;
		check.type = standard_errors[i];
		check.file = tt_sprintf("File%d", i);
		check.message = tt_sprintf("Message%d", i);
		ok(error_is_eq_mp_error(cur_err, &check), "check %s",
					standard_errors[i]);
		++i;
	}
	is(i, TEST_STANDARD_ERRORS_NUM, "stack size");
	error_unref(err1);
	check_plan();
	footer();
}

void
test_decode_unknown_type()
{
	header();
	plan(1);
	char buffer[2048];
	memset(buffer, 0, sizeof(buffer));

	char *data = mp_encode_error_header(buffer, 1);
	struct mp_test_error err;
	memset(&err, 0, sizeof(err));
	err.code = 1;
	err.line = 2;
	err.saved_errno = 3;
	err.type = "SomeNewError";
	err.file = "File1";
	err.message = "Message1";
	data = mp_encode_mp_error(&err, data);

	uint32_t len = data - buffer;
	const char *pos = buffer;
	struct error *unpacked = error_unpack(&pos, len);
	error_ref(unpacked);
	err.code = 0;
	err.type = "ClientError";
	ok(error_is_eq_mp_error(unpacked, &err), "check SomeNewError");
	error_unref(unpacked);

	check_plan();
	footer();
}

void
test_fail_not_enough_fields()
{
	header();
	plan(2);
	char buffer[2048];
	memset(buffer, 0, sizeof(buffer));

	char *data = mp_encode_error_header(buffer, 1);
	struct mp_test_error err;
	memset(&err, 0, sizeof(err));
	err.code = 42;
	err.line = 3;
	err.saved_errno = 4;
	err.type = "AccessDeniedError";
	err.file = "File1";
	err.message = "Message1";
	err.ad_object_type = "ObjectType";
	err.ad_access_type = "AccessType";
	data = mp_encode_mp_error(&err, data);

	uint32_t len = data - buffer;
	const char *pos = buffer;
	struct error *unpacked = error_unpack(&pos, len);

	is(unpacked, NULL, "check not enough additional fields");
	ok(!diag_is_empty(diag_get()), "error about parsing problem is set");
	check_plan();
	footer();
}

void
test_unknown_fields()
{
	header();
	plan(1);
	char buffer[2048];
	memset(buffer, 0, sizeof(buffer));

	char *data = mp_encode_error_header(buffer, 1);
	struct mp_test_error err;
	memset(&err, 0, sizeof(err));
	err.code = 0;
	err.line = 1;
	err.saved_errno = 0;
	err.type = "SystemError";
	err.file = "File";
	err.message = "Message";
	err.unknown_uint_field = 55;
	data = mp_encode_mp_error(&err, data);

	uint32_t len = data - buffer;
	const char *pos = buffer;
	struct error *unpacked = error_unpack(&pos, len);
	error_ref(unpacked);
	data = mp_encode_mp_error(&err, data);

	ok(error_is_eq_mp_error(unpacked, &err), "check unknown fields");
	error_unref(unpacked);
	check_plan();
}

void
test_unknown_additional_fields()
{
	header();
	plan(1);
	char buffer[2048];
	memset(buffer, 0, sizeof(buffer));

	char *data = mp_encode_error_header(buffer, 1);
	struct mp_test_error err;
	memset(&err, 0, sizeof(err));
	err.code = 42;
	err.line = 3;
	err.saved_errno = 4;
	err.type = "AccessDeniedError";
	err.file = "File";
	err.message = "Message";
	err.ad_object_type = "ObjectType";
	err.ad_object_name = "ObjectName";
	err.ad_access_type = "AccessType";
	err.unknown_str_field = "unknown_field";
	data = mp_encode_mp_error(&err, data);

	uint32_t len = data - buffer;
	const char *pos = buffer;
	struct error *unpacked = error_unpack(&pos, len);
	error_ref(unpacked);
	ok(error_is_eq_mp_error(unpacked, &err),
	   "check unknown additional field");
	error_unref(unpacked);

	check_plan();
	footer();
}

static int
mp_fprint_ext_test(FILE *file, const char **data, int depth)
{
	int8_t type;
	mp_decode_extl(data, &type);
	if (type != MP_ERROR)
		return fprintf(file, "undefined");
	return mp_fprint_error(file, data, depth);
}

static int
mp_snprint_ext_test(char *buf, int size, const char **data, int depth)
{
	int8_t type;
	mp_decode_extl(data, &type);
	if (type != MP_ERROR)
		return snprintf(buf, size, "undefined");
	return mp_snprint_error(buf, size, data, depth);
}

static void
test_mp_print_check_str(int depth, const char *str, int len,
			const char *expected, const char *method)
{
	is(len, (int) strlen(str), "%s depth %d correct returned value", method,
	   depth);
	int expected_len = strlen(expected);
	is(len, depth * 2 + expected_len, "%s depth %d correct length", method,
	   depth);
	bool is_error = false;
	/*
	 * Deep encoding is simulated with a number of nested
	 * arrays. In string representation they look like:
	 *
	 *   [[[[[[[ ... object ... ]]]]]]]
	 *
	 */
	for (int i = 0; i < depth && !is_error; ++i)
		is_error = str[i] != '[' || str[len - 1 - i] != ']';
	ok(!is_error, "%s depth %d correct prefix and suffix", method, depth);
	str += depth;
	is(strncmp(str, expected, expected_len), 0, "%s depth %d correct "
	   "object in the middle", method, depth);
}

static void
mpstream_error_test(void *ctx)
{
	*((bool *)ctx) = true;
}

static void
test_mp_print_encode_error(struct ibuf *buf, struct error *e, int depth)
{
	struct mpstream stream;
	bool is_error = false;
	mpstream_init(&stream, buf, ibuf_reserve_cb, ibuf_alloc_cb,
		      mpstream_error_test, &is_error);
	for (int i = 0; i < depth; ++i)
		mpstream_encode_array(&stream, 1);
	error_to_mpstream(e, &stream);
	mpstream_flush(&stream);
	fail_if(is_error);
}

static void
test_mp_print_check(const char *data, int depth, const char *expected)
{
	char str[2048];
	int rc = mp_snprint(str, sizeof(str), data);
	test_mp_print_check_str(depth, str, rc, expected, "mp_snprint");
	int rc2 = mp_snprint(NULL, 0, data);
	is(rc, rc2, "mp_snprint depth %d correct with NULL buffer", depth);

	FILE *f = tmpfile();
	rc = mp_fprint(f, data);
	rewind(f);
	rc2 = fread(str, 1, TT_STATIC_BUF_LEN, f);
	is(rc, rc2, "mp_fprint depth %d result and the actual file size "
	   "are equal", depth);
	str[rc] = 0;
	fclose(f);
	test_mp_print_check_str(depth, str, rc, expected, "mp_fprint");
}

static void
test_mp_print(void)
{
	header();
	plan(60);

	mp_snprint_ext = mp_snprint_ext_test;
	mp_fprint_ext = mp_fprint_ext_test;

	struct error *e1 = BuildClientError("file1", 1, 0);
	error_ref(e1);
	struct error *e2 = BuildCustomError("file2", 4, "type", 5);
	struct error *e3 = BuildAccessDeniedError("file3", 6, "field1",
						  "field2", "field3", "field4");
	error_set_prev(e1, e2);
	error_set_prev(e2, e3);

	struct ibuf buf;
	ibuf_create(&buf, &cord()->slabc, 1024);

	note("zero depth, normal error");
	int depth = 0;
	const char *expected = "{"
		"\"stack\": ["
			"{"
				"\"type\": \"ClientError\", "
				"\"line\": 1, "
				"\"file\": \"file1\", "
				"\"message\": \"Unknown error\", "
				"\"errno\": 0, "
				"\"code\": 0"
			"}, "
			"{"
				"\"type\": \"CustomError\", "
				"\"line\": 4, "
				"\"file\": \"file2\", "
				"\"message\": \"\", "
				"\"errno\": 0, "
				"\"code\": 5, "
				"\"fields\": {"
					"\"custom_type\": \"type\""
				"}"
			"}, "
			"{"
				"\"type\": \"AccessDeniedError\", "
				"\"line\": 6, "
				"\"file\": \"file3\", "
				"\"message\": \"field1 access to field2 "
					"'field3' is denied for user "
					"'field4'\", "
				"\"errno\": 0, "
				"\"code\": 42, "
				"\"fields\": {"
					"\"object_type\": \"field2\", "
					"\"object_name\": \"field3\", "
					"\"access_type\": \"field1\""
				"}"
			"}"
		"]"
	"}";
	test_mp_print_encode_error(&buf, e1, depth);
	test_mp_print_check(buf.rpos, depth, expected);
	ibuf_reset(&buf);

	note("max depth, all is truncated");
	depth = MP_PRINT_MAX_DEPTH;
	expected = "{...}";
	test_mp_print_encode_error(&buf, e1, depth);
	test_mp_print_check(buf.rpos, depth, expected);
	ibuf_reset(&buf);

	note("max depth - 1, top level of keys is visible");
	depth = MP_PRINT_MAX_DEPTH - 1;
	expected = "{\"stack\": [...]}";
	test_mp_print_encode_error(&buf, e1, depth);
	test_mp_print_check(buf.rpos, depth, expected);
	ibuf_reset(&buf);

	note("max depth - 2, top level of keys and error count are visible");
	depth = MP_PRINT_MAX_DEPTH - 2;
	expected = "{\"stack\": [{...}, {...}, {...}]}";
	test_mp_print_encode_error(&buf, e1, depth);
	test_mp_print_check(buf.rpos, depth, expected);
	ibuf_reset(&buf);

	note("max depth - 3, all except additional fields is visible");
	depth = MP_PRINT_MAX_DEPTH - 3;
	expected = "{"
		"\"stack\": ["
			"{"
				"\"type\": \"ClientError\", "
				"\"line\": 1, "
				"\"file\": \"file1\", "
				"\"message\": \"Unknown error\", "
				"\"errno\": 0, "
				"\"code\": 0"
			"}, "
			"{"
				"\"type\": \"CustomError\", "
				"\"line\": 4, "
				"\"file\": \"file2\", "
				"\"message\": \"\", "
				"\"errno\": 0, "
				"\"code\": 5, "
				"\"fields\": {...}"
			"}, "
			"{"
				"\"type\": \"AccessDeniedError\", "
				"\"line\": 6, "
				"\"file\": \"file3\", "
				"\"message\": \"field1 access to field2 "
					"'field3' is denied for user "
					"'field4'\", "
				"\"errno\": 0, "
				"\"code\": 42, "
				"\"fields\": {...}"
			"}"
		"]"
	"}";
	test_mp_print_encode_error(&buf, e1, depth);
	test_mp_print_check(buf.rpos, depth, expected);
	ibuf_reset(&buf);

	note("zero depth, error with unknown fields");
	ibuf_reserve(&buf, 2048);
	char *begin = buf.rpos + 10;
	char *data = mp_encode_map(begin, 2);
	data = mp_encode_uint(data, 4096);
	data = mp_encode_double(data, 1.234);
	data = mp_encode_uint(data, MP_ERROR_STACK);
	data = mp_encode_array(data, 1);
	struct mp_test_error err;
	memset(&err, 0, sizeof(err));
	err.code = 42;
	err.line = 3;
	err.saved_errno = 4;
	err.type = "AccessDeniedError";
	err.file = "File";
	err.message = "Message";
	err.ad_object_type = "ObjectType";
	err.ad_object_name = "ObjectName";
	err.ad_access_type = "AccessType";
	err.unknown_uint_field = 300;
	err.unknown_str_field = "unknown_field";
	data = mp_encode_mp_error(&err, data);
	uint32_t size = data - begin;
	begin -= mp_sizeof_extl(size);
	mp_encode_extl(begin, MP_ERROR, size);
	expected = "{"
		"4096: 1.234, "
		"\"stack\": ["
			"{"
				"\"type\": \"AccessDeniedError\", "
				"\"file\": \"File\", "
				"\"line\": 3, "
				"\"message\": \"Message\", "
				"\"errno\": 4, "
				"\"code\": 42, "
				"18446744073709551615: 300, "
				"\"fields\": {"
					"\"object_type\": \"ObjectType\", "
					"\"object_name\": \"ObjectName\", "
					"\"access_type\": \"AccessType\", "
					"\"unknown_field\": \"unknown_field\""
				"}"
			"}"
		"]"
	"}";
	test_mp_print_check(begin, 0, expected);

	error_unref(e1);
	ibuf_destroy(&buf);
	mp_snprint_ext = mp_snprint_ext_default;
	mp_fprint_ext = mp_fprint_ext_default;

	check_plan();
	footer();
}

int
main(void)
{
	header();
	plan(6);
	memory_init();
	fiber_init(fiber_c_invoke);

	test_stack_error_decode();
	test_decode_unknown_type();
	test_fail_not_enough_fields();
	test_unknown_fields();
	test_unknown_additional_fields();
	test_mp_print();

	fiber_free();
	memory_free();
	footer();
	return check_plan();
}
