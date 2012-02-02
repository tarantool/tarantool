#ifndef TARANTOOL_ERRCODE_H
#define TARANTOOL_ERRCODE_H
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
	/*  1 */_(ER_NONMASTER,			2, "Non master connection, but it should be") \
	/*  2 */_(ER_ILLEGAL_PARAMS,		2, "Illegal parameters, %s") \
	/*  3 */_(ER_BAD_UID,			2, "Uid is not from this storage range") \
	/*  4 */_(ER_TUPLE_IS_RO,		1, "Tuple is marked as read-only") \
	/*  5 */_(ER_TUPLE_IS_NOT_LOCKED,	1, "Tuple isn't locked") \
	/*  6 */_(ER_TUPLE_IS_LOCKED,		1, "Tuple is locked") \
	/*  7 */_(ER_MEMORY_ISSUE,		1, "Failed to allocate %u bytes in %s for %s") \
	/*  8 */_(ER_BAD_INTEGRITY,		2, "Bad graph integrity") \
	/*  9 */_(ER_UNUSED9,			0, "Unused9") \
	/* 10 */_(ER_UNSUPPORTED,		2, "Unsupported") \
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
	/* 24 */_(ER_CANNOT_REGISTER,		1, "Can't register new user") \
	/* 25 */_(ER_UNUSED25,			0, "Unused25") \
	/* 26 */_(ER_CANNOT_INIT_ALERT_ID,	1, "Can't generate alert id") \
	/* 27 */_(ER_CANNOT_DEL,		2, "Can't del node") \
	/* 28 */_(ER_USER_NOT_REGISTERED,	2, "User isn't registered") \
		/* silversearch error codes */ \
	/* 29 */_(ER_SYNTAX_ERROR,		2, "Syntax error in query") \
	/* 30 */_(ER_WRONG_FIELD,		2, "Unknown field") \
	/* 31 */_(ER_WRONG_NUMBER,		2, "Number value is out of range") \
	/* 32 */_(ER_DUPLICATE,			2, "Insert already existing object") \
	/* 33 */_(ER_UNUSED32,			0, "Unused33") \
	/* 34 */_(ER_UNSUPPORTED_ORDER,		2, "Can not order result") \
	/* 35 */_(ER_MULTIWRITE,		2, "Multiple to update/delete") \
	/* 36 */_(ER_NOTHING,			0, "Nothing to do (not an error)") \
	/* 37 */_(ER_UPDATE_ID,			2, "Id's update") \
	/* 38 */_(ER_WRONG_VERSION,		2, "Unsupported version of protocol") \
		/* end of silversearch error codes */					\
	/* 39 */_(ER_WAL_IO,			2, "Failed to write to disk") \
	/* 40 */_(ER_FIELD_TYPE,		2, "Field type does not match one required by operation: expected a %s") \
	/* 41 */_(ER_TYPE_MISMATCH,		2, "Argument type in operation does not match field type: expected a %s") \
	/* 42 */_(ER_SPLICE,			2, "Field SPLICE error: %s") \
	/* 43 */_(ER_TUPLE_IS_TOO_LONG,		2, "Tuple is too long %u") \
	/* 44 */_(ER_UNKNOWN_UPDATE_OP,		2, "Unknown UPDATE operation") \
	/* 45 */_(ER_UNUSED45,			0, "Unused45") \
	/* 46 */_(ER_UNUSED46,			0, "Unused46") \
	/* 47 */_(ER_UNUSED47,			0, "Unused47") \
	/* 48 */_(ER_PROC_RET,			2, "Return type '%s' is not supported in the binary protocol") \
	/* 49 */_(ER_TUPLE_NOT_FOUND,		2, "Tuple doesn't exist") \
	/* 50 */_(ER_NO_SUCH_PROC,		2, "Procedure '%.*s' is not defined") \
	/* 51 */_(ER_PROC_LUA,			2, "Lua error: %s") \
	/* 52 */_(ER_SPACE_DISABLED,	2, "Space %u is disabled") \
	/* 53 */_(ER_NO_SUCH_INDEX,		2, "No index #%u is defined in space %u") \
	/* 54 */_(ER_NO_SUCH_FIELD,		2, "Field %u was not found in the tuple") \
	/* 55 */_(ER_TUPLE_FOUND,		2, "Tuple already exists") \
	/* 56 */_(ER_INDEX_VIOLATION,		2, "Duplicate key exists in a unique index") \
	/* 57 */_(ER_NO_SUCH_SPACE,		2, "Space %u does not exists")


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


#endif /* TARANTOOL_ERRCODE_H */
