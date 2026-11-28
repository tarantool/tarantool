/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lib/core/mp_embedding.h"
#include "lib/core/mp_extension_types.h"

#include <fenv.h>
#include <float.h>
#include <math.h>
#include <string.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

int
main(void)
{
	plan(26);
	float value;
	ok(embedding_f32_from_double(1, &value) == 0 && value == 1,
	   "finite conversion");
	ok(embedding_f32_from_double(INFINITY, &value) != 0,
	   "reject infinity");
	ok(embedding_f32_from_double(NAN, &value) != 0, "reject NaN");
	ok(embedding_f32_from_double(DBL_MAX, &value) != 0,
	   "reject out of range");
	ok(embedding_f32_from_double(0, NULL) != 0, "reject missing output");
	int saved = fegetround();
	fesetround(FE_UPWARD);
	ok(embedding_f32_from_double(1.0 + ldexp(1.0, -24), &value) == 0 &&
	   value == 1,
	   "tie rounds to even independently of current rounding mode");
	ok(fegetround() == FE_UPWARD, "restore rounding mode");
	fesetround(saved);
	ok(embedding_f32_from_double(FLT_MAX, &value) == 0 && value == FLT_MAX,
	   "accept maximum float32");
	char payload[12];
	embedding_f32le_store(payload, 0, 1);
	embedding_f32le_store(payload, 1, -2);
	embedding_f32le_store(payload, 2, 0);
	ok(memcmp(payload, "\0\0\x80\x3f\0\0\0\xc0", 8) == 0,
	   "portable little-endian encoding");
	ok(embedding_f32le_load(payload + 1, 0) ==
	   embedding_f32le_load("\0\x80\x3f\0", 0), "unaligned load");
	ok(mp_validate_embedding(payload, sizeof(payload)) == 0,
	   "validate coordinates including zero");
	ok(mp_validate_embedding(NULL, 4) != 0, "reject missing payload");
	ok(mp_validate_embedding(payload, 0) != 0, "reject empty embedding");
	ok(mp_validate_embedding(payload, 3) != 0, "reject partial coordinate");
	ok(mp_validate_embedding(payload, 4097 * 4) != 0,
	   "reject oversized embedding before reading payload");
	embedding_f32le_store(payload, 2, INFINITY);
	ok(mp_validate_embedding(payload, sizeof(payload)) != 0,
	   "reject infinity in payload");
	embedding_f32le_store(payload, 2, NAN);
	ok(mp_validate_embedding(payload, sizeof(payload)) != 0,
	   "reject NaN in payload");
	embedding_f32le_store(payload, 2, 0);
	char encoded[32];
	char *end = mp_encode_embedding(encoded, payload, 3);
	ok(end - encoded == mp_sizeof_embedding(3), "encoded size");
	int8_t type;
	const char *cursor = encoded;
	ok(mp_decode_extl(&cursor, &type) == sizeof(payload) &&
	   type == MP_EMBEDDING, "wire extension and payload size");
	uint32_t dimension;
	cursor = encoded;
	const char *coordinates = mp_decode_embedding(&cursor, &dimension);
	ok(coordinates != NULL && dimension == 3 && cursor == end &&
	   memcmp(coordinates, payload, sizeof(payload)) == 0,
	   "decode payload");
	cursor = "\xc0";
	ok(mp_decode_embedding(&cursor, &dimension) == NULL, "reject nil");
	char *p = mp_encode_extl(encoded, MP_UUID, sizeof(payload));
	memcpy(p, payload, sizeof(payload));
	cursor = encoded;
	ok(mp_decode_embedding(&cursor, &dimension) == NULL,
	   "reject another extension");
	mp_encode_extl(encoded, MP_EMBEDDING, sizeof(payload));
	embedding_f32le_store(p, 0, INFINITY);
	cursor = encoded;
	ok(mp_decode_embedding(&cursor, &dimension) == NULL,
	   "decode rejects invalid payload");
	char buf[64];
	cursor = payload;
	int n = mp_snprint_embedding(buf, sizeof(buf), &cursor,
				     sizeof(payload));
	ok(n == (int)strlen("embedding([1, -2, 0])") &&
	   strcmp(buf, "embedding([1, -2, 0])") == 0 &&
	   cursor == payload + sizeof(payload), "print payload");
	cursor = payload;
	ok(mp_snprint_embedding(buf, 2, &cursor, sizeof(payload)) == n &&
	   buf[1] == '\0', "truncated print reports full length");
	FILE *file = tmpfile();
	cursor = payload;
	ok(mp_fprint_embedding(file, &cursor, sizeof(payload)) == n &&
	   cursor == payload + sizeof(payload), "print payload to stream");
	fclose(file);
	return check_plan();
}
