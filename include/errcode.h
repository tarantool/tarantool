#ifndef TARANTOOL_ERRCODE_H_INCLUDED
#define TARANTOOL_ERRCODE_H_INCLUDED
/*
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
#include <stdint.h>

#include <util.h>

struct errcode_record {
	const char *errstr;
	const char *errdesc;
	uint8_t errflags;
};

enum { TNT_ERRMSG_MAX = 512 };

/*
 * To add a new error code to Tarantool, extend this array. Please
 * try to reuse empty slots (ER_UNUSED*), if there are any left.
 *
 * !IMPORTANT! Currently you need to manually update the user
 * guide (doc/user/errcode.xml) with each added error code.
 * Please don't forget to do it!
 */

#define ERROR_CODES(_)					    \
	/*  0 */_(ER_OK=0,			0, "OK") \
	/*  1 */_(ER_NONMASTER,			2, "Attempt to modify data via a secondary port connection or on a replication slave") \
	/*  2 */_(ER_ILLEGAL_PARAMS,		2, "Illegal parameters, %s") \
	/*  3 */_(ER_UNUSED3,			2, "Unused3") \
	/*  4 */_(ER_TUPLE_IS_RO,		1, "Tuple is marked as read-only") \
	/*  5 */_(ER_UNUSED5,			2, "Unused5") \
	/*  6 */_(ER_UNUSED6,			2, "Unused6") \
	/*  7 */_(ER_MEMORY_ISSUE,		1, "Failed to allocate %u bytes in %s for %s") \
	/*  8 */_(ER_UNUSED8,			2, "Unused8") \
	/*  9 */_(ER_INJECTION,			2, "Error injection '%s'") \
	/* 10 */_(ER_UNSUPPORTED,		2, "%s does not support %s") \
		/* silverproxy error codes */ \
	/* 11 */_(ER_RESERVED11,		0, "Reserved11") \
	/* 12 */_(ER_RESERVED12,		0, "Reserved12") \
	/* 13 */_(ER_RESERVED13,		0, "Reserved13") \
	/* 14 */_(ER_RESERVED14,		0, "Reserved14") \
	/* 15 */_(ER_RESERVED15,		0, "Reserved15") \
	/* 16 */_(ER_RESERVED16,		0, "Reserved16") \
	/* 17 */_(ER_RESERVED17,		0, "Reserved17") \
	/* 18 */_(ER_RESERVED18,		0, "Reserved18") \
	/* 19 */_(ER_RESERVED19,		0, "Reserved19") \
	/* 20 */_(ER_RESERVED20,		0, "Reserved20") \
	/* 21 */_(ER_RESERVED21,		0, "Reserved21") \
	/* 22 */_(ER_RESERVED22,		0, "Reserved22") \
	/* 23 */_(ER_RESERVED23,		0, "Reserved23") \
		/* end of silverproxy error codes */ \
	/* 24 */_(ER_UNUSED24,			2, "Unused24") \
	/* 25 */_(ER_TUPLE_IS_EMPTY,		2, "UPDATE error: the new tuple has no fields") \
	/* 26 */_(ER_UNUSED26,			2, "Unused26") \
	/* 27 */_(ER_UNUSED27,			2, "Unused27") \
	/* 28 */_(ER_UNUSED28,			2, "Unused28") \
	/* 29 */_(ER_UNUSED29,			2, "Unused29") \
	/* 30 */_(ER_UNUSED30,			2, "Unused30") \
	/* 31 */_(ER_UNUSED31,			2, "Unused31") \
	/* 32 */_(ER_UNUSED32,			2, "Unused32") \
	/* 33 */_(ER_UNUSED33,			2, "Unused33") \
	/* 34 */_(ER_UNUSED34,			2, "Unused34") \
	/* 35 */_(ER_UNUSED35,			2, "Unused35") \
	/* 36 */_(ER_UNUSED36,			2, "Unused36") \
	/* 37 */_(ER_UNUSED37,			2, "Unused37") \
	/* 38 */_(ER_KEY_FIELD_TYPE,		2, "Supplied key field type does not match index type: expected %s") \
	/* 39 */_(ER_WAL_IO,			2, "Failed to write to disk") \
	/* 40 */_(ER_FIELD_TYPE,		2, "Field type does not match one required by operation: expected a %s") \
	/* 41 */_(ER_ARG_TYPE,			2, "Argument type in operation does not match field type: expected a %s") \
	/* 42 */_(ER_SPLICE,			2, "Field SPLICE error: %s") \
	/* 43 */_(ER_TUPLE_IS_TOO_LONG,		2, "Tuple is too long %u") \
	/* 44 */_(ER_UNKNOWN_UPDATE_OP,		2, "Unknown UPDATE operation") \
	/* 45 */_(ER_EXACT_MATCH,		2, "Partial key in an exact match (key field count: %d, expected: %d)") \
	/* 46 */_(ER_UNUSED46,			2, "Unused46") \
	/* 47 */_(ER_KEY_CARDINALITY,		2, "Key cardinality %d is greater than index cardinality %d") \
	/* 48 */_(ER_PROC_RET,			2, "Return type '%s' is not supported in the binary protocol") \
	/* 49 */_(ER_TUPLE_NOT_FOUND,		2, "Tuple doesn't exist") \
	/* 50 */_(ER_NO_SUCH_PROC,		2, "Procedure '%.*s' is not defined") \
	/* 51 */_(ER_PROC_LUA,			2, "Lua error: %s") \
	/* 52 */_(ER_SPACE_DISABLED,		2, "Space %u is disabled") \
	/* 53 */_(ER_NO_SUCH_INDEX,		2, "No index #%u is defined in space %u") \
	/* 54 */_(ER_NO_SUCH_FIELD,		2, "Field %u was not found in the tuple") \
	/* 55 */_(ER_TUPLE_FOUND,		2, "Tuple already exists") \
	/* 56 */_(ER_INDEX_VIOLATION,		2, "Duplicate key exists in a unique index") \
	/* 57 */_(ER_NO_SUCH_SPACE,		2, "Space %u does not exist")


/*
 * !IMPORTANT! Please follow instructions at start of the file
 * when adding new errors.
 */

ENUM0(tnt_error_codes_enum, ERROR_CODES);
extern struct errcode_record tnt_error_codes[];

/** Return a string representation of error name, e.g. "ER_OK".
 */

static inline const char *tnt_errcode_str(uint32_t errcode)
{
	return tnt_error_codes[errcode].errstr;
}


/** Return a 4-byte numeric error code, with status flags. */

static inline uint32_t tnt_errcode_val(uint32_t errcode)
{
	return (errcode << 8) | tnt_error_codes[errcode].errflags;
}


/** Return a description of the error. */

static inline const char *tnt_errcode_desc(uint32_t errcode)
{
	return tnt_error_codes[errcode].errdesc;
}


#endif /* TARANTOOL_ERRCODE_H_INCLUDED */
