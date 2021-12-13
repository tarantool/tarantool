/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "mp_compression.h"
#include "msgpuck.h"
#include "mp_extension_types.h"
#include "mp_uuid.h"
#include "trivia/util.h"
#include "unit.h"
#include "random.h"
#include "memory.h"
#include "fiber.h"
#include "msgpack.h"

#include <stdlib.h>

#define FIELD_SIZE_MAX 100
#define STRING_SIZE_MAX 1024

struct field {
	char *data_end;
	char *data_str;
};

static struct field
mp_encode_field(char *data, enum mp_type type)
{
	struct field field;
	struct tt_uuid uuid;
	field.data_end = data;
	switch (type) {
	case MP_NIL:
		field.data_end = mp_encode_nil(field.data_end);
		field.data_str = "null";
		break;
	case MP_UINT:
		field.data_end = mp_encode_uint(field.data_end, 123456789);
		field.data_str = "123456789";
		break;
	case MP_INT:
		field.data_end = mp_encode_int(field.data_end, -123456789);
		field.data_str = "-123456789";
		break;
	case MP_STR:
		field.data_end = mp_encode_str(field.data_end,
					       "tuple compression",
					       strlen("tuple compression"));
		field.data_str = "\"tuple compression\"";
		break;
	case MP_BIN:
		field.data_end = mp_encode_bin(data, "tuple compression",
					       strlen("tuple compression"));
		field.data_str = "\"tuple compression\"";
		break;
	case MP_ARRAY:
		field.data_end = mp_encode_array(field.data_end, 3);
		field.data_end = mp_encode_uint(field.data_end, 212);
		field.data_end = mp_encode_str(field.data_end, "tuple", 5);
		field.data_end = mp_encode_nil(field.data_end);
		field.data_str = "[212, \"tuple\", null]";
		break;
	case MP_MAP:
		field.data_end = mp_encode_map(field.data_end, 3);
		field.data_end = mp_encode_str(field.data_end, "1", 1);
		field.data_end = mp_encode_uint(field.data_end, 212);
		field.data_end = mp_encode_uint(field.data_end, 1);
		field.data_end = mp_encode_str(field.data_end, "tuple", 5);
		field.data_end = mp_encode_uint(field.data_end, 3);
		field.data_end = mp_encode_nil(field.data_end);
		field.data_str = "{\"1\": 212, 1: \"tuple\", 3: null}";
		break;
	case MP_BOOL:
		field.data_end = mp_encode_bool(field.data_end, true);
		field.data_str = "true";
		break;
	case MP_FLOAT:
		field.data_end = mp_encode_float(field.data_end, 1.375);
		field.data_str = "1.375";
		break;
	case MP_DOUBLE:
		field.data_end = mp_encode_double(field.data_end, 1.375);
		field.data_str = "1.375";
		break;
	case MP_EXT:
		tt_uuid_create(&uuid);
		field.data_end = mp_encode_uuid(field.data_end, &uuid);
		field.data_str = tt_uuid_str(&uuid);
		break;
	default:
		abort();
	}
	return field;
}

static int
mp_compression_snprintf_test(char *data, char *cdata, char *str)
{
	char data_str[STRING_SIZE_MAX], ttc_data_str[STRING_SIZE_MAX];
	plan(3);
	int s1 = mp_snprint(data_str, STRING_SIZE_MAX, data);
	int s2 = mp_snprint(ttc_data_str, STRING_SIZE_MAX, cdata);
	is(s1, s2, "string representation size");
	is(memcmp(data_str, ttc_data_str, s1),
	   0, "string representation");
	is(memcmp(data_str, str, s1),
	   0, "string representation");
	return check_plan();
}

static int
mp_compression_fprintf_test(char *data, char *cdata, char *str)
{
	int rc;
	char data_str[STRING_SIZE_MAX], ttc_data_str[STRING_SIZE_MAX];
	plan(3);
	FILE *f1 = tmpfile();
	FILE *f2 = tmpfile();
	assert(f1 != NULL && f2 != NULL);
	int s1 = mp_fprint(f1, data);
	int s2 = mp_fprint(f2, cdata);
	rewind(f1);
	rewind(f2);
	is(s1, s2, "file representation size");
	rc = fread(data_str, 1, sizeof(data_str), f1);
	rc = fread(ttc_data_str, 1, sizeof(ttc_data_str), f2);
	is(memcmp(data_str, ttc_data_str, s1),
	   0, "file representation");
	is(memcmp(data_str, str, s1),
	   0, "string representation");
	fclose(f1);
	fclose(f2);
	return check_plan();

}

static int
mp_compression_test(enum compression_type compression_type)
{
	char data[FIELD_SIZE_MAX];
	struct tt_compression *ttc;
	uint32_t size;

	plan ((MP_EXT + 1) * 10);
	for (enum mp_type type = MP_NIL; type <= MP_EXT; type++) {
		struct field field = mp_encode_field(data, type);
		char *data_end = field.data_end;
		ttc = tt_compression_new(data_end - data, compression_type);
		is(mp_set_data_for_compression(data, data_end - data, ttc), 0,
		   "set data for compression");
		is(mp_sizeof_for_compression(ttc, &size),
		   0, "calculate compressed data size");
		char *cdata = xcalloc(1, size), *cdata_end = cdata;
		cdata_end = mp_encode_compression(cdata_end, ttc);
		isnt(cdata_end, NULL, "compression");
		is(cdata_end - cdata, size, "compressed data size");
		tt_compression_delete(ttc);
		cdata_end = cdata;
		mp_compression_snprintf_test(data, cdata, field.data_str);
		mp_compression_fprintf_test(data, cdata, field.data_str);
		is(mp_sizeof_for_decompression((const char **)&cdata_end, &size),
		   0, "calculate decompressed data size");
		is(data_end - data, size, "decompressed data size");
		ttc = tt_compression_new(size, COMPRESSION_TYPE_NONE);
		ttc = mp_decode_compression((const char **)&cdata_end, ttc);
		isnt(ttc, NULL, "decompression");
		is(memcmp(data, ttc->data, data_end - data),
		   0, "compression/decompression");
		tt_compression_delete(ttc);
	}
	return check_plan();
}

int
mp_compression_invalid_test()
{
	struct tt_compression *ttc;
	char data[FIELD_SIZE_MAX];
	struct field field;

	plan(256 + (MP_EXT + 1) + 1);

	/* Invalid size of compression data array */
	ttc = tt_compression_new(0, 0);
	field = mp_encode_field(data, MP_NIL);
	isnt(mp_set_data_for_compression(data, field.data_end - data, ttc),
	     0, "invalid data size");
	tt_compression_delete(ttc);

	ttc = tt_compression_new(FIELD_SIZE_MAX, 0);
	for (unsigned int i = 0; i < 256; i++) {
		/* Invalid data for compression - data is not valid msgpack. */
		isnt(mp_set_data_for_compression(data, FIELD_SIZE_MAX, ttc),
		     0, "set invalid data for compression");
	}
	tt_compression_delete(ttc);

	char *first_field_end = mp_encode_nil(data);
	for (enum mp_type type = MP_NIL; type <= MP_EXT; type++) {
		field = mp_encode_field(first_field_end, type);
		char *data_end = field.data_end;
		ttc = tt_compression_new(data_end - data, type);
		/*
		 * Invalid data for compression, should be exactly
		 * one msgpack field.
		 */
		isnt(mp_set_data_for_compression(data, data_end - data, ttc),
		     0, "several msgpack fields banned");
		tt_compression_delete(ttc);
	}

	return check_plan();
}

int
main(void)
{
	plan(3);
	memory_init();
        fiber_init(fiber_c_invoke);
        random_init();
        msgpack_init();

	mp_compression_test(COMPRESSION_TYPE_NONE);
	mp_compression_test(COMPRESSION_TYPE_ZSTD5);
	mp_compression_invalid_test();

	return check_plan();
}