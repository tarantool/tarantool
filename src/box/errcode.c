#include "errcode.h"
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
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
#include "errcode.h"
#include "trivia/util.h"

#define FIELDS_0() \
		NULL, 0
#define FIELDS_1(n1, t1) \
		(const struct errcode_field[]) \
		{{n1, t1}}, 1
#define FIELDS_2(n1, t1, n2, t2) \
		(const struct errcode_field[]) \
		{{n1, t1}, {n2, t2}}, 2
#define FIELDS_3(n1, t1, n2, t2, n3, t3) \
		(const struct errcode_field[]) \
		{{n1, t1}, {n2, t2}, {n3, t3}}, 3
#define FIELDS_4(n1, t1, n2, t2, n3, t3, n4, t4) \
		(const struct errcode_field[]) \
		{{n1, t1}, {n2, t2}, {n3, t3}, {n4, t4}}, 4
#define FIELDS_5(n1, t1, n2, t2, n3, t3, n4, t4, n5, t5) \
		(const struct errcode_field[]) \
		{{n1, t1}, {n2, t2}, {n3, t3}, {n4, t4}, {n5, t5}}, 5

#define GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, NAME, ...) NAME
#undef INCOMPLETE

/**
 * Without stub argument 'x' ##__VA_ARGS__ magic does not work with -std=c11.
 */
#define ERRCODE_FIELDS_AND_COUNT(x, ...) \
		GET_MACRO(, ##__VA_ARGS__, \
			  FIELDS_5, INCOMPLETE, \
			  FIELDS_4, INCOMPLETE, \
			  FIELDS_3, INCOMPLETE, \
			  FIELDS_2, INCOMPLETE, \
			  FIELDS_1, INCOMPLETE, \
			  FIELDS_0)(__VA_ARGS__)

/** Up to 5 fields are supported. */
#define ERRCODE_RECORD_MEMBER(t, c, d, ...) \
	[c] = {#t, d, ERRCODE_FIELDS_AND_COUNT(1, ##__VA_ARGS__)},

/** Helper macro to describe field type of the error code. */
#define CHAR ERRCODE_FIELD_TYPE_CHAR
#define INT ERRCODE_FIELD_TYPE_INT
#define UINT ERRCODE_FIELD_TYPE_UINT
#define LONG ERRCODE_FIELD_TYPE_LONG
#define ULONG ERRCODE_FIELD_TYPE_ULONG
#define LLONG ERRCODE_FIELD_TYPE_LLONG
#define ULLONG ERRCODE_FIELD_TYPE_ULLONG
#define STRING ERRCODE_FIELD_TYPE_STRING
#define MSGPACK ERRCODE_FIELD_TYPE_MSGPACK
#define TUPLE ERRCODE_FIELD_TYPE_TUPLE

const struct errcode_record box_error_codes[box_error_code_MAX] = {
	ERROR_CODES(ERRCODE_RECORD_MEMBER)
};

const struct errcode_record errcode_record_unknown = {
	.errstr = "ER_UNKNOWN",
	.errdesc = "Unknown error",
};
