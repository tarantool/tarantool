#ifndef TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED
#define TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdbool.h>
#include <stdint.h>
#include <msgpuck/msgpuck.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum {
	/** Maximal iproto package body length (2GiB) */
	IPROTO_BODY_LEN_MAX = 2147483648UL,
	/* Maximal length of text handshake (greeting) */
	IPROTO_GREETING_SIZE = 128,
	/** marker + len + prev crc32 + cur crc32 + (padding) */
	XLOG_FIXHEADER_SIZE = 19
};

enum iproto_key {
	IPROTO_REQUEST_TYPE = 0x00,
	IPROTO_SYNC = 0x01,
	/* Replication keys (header) */
	IPROTO_SERVER_ID = 0x02,
	IPROTO_LSN = 0x03,
	IPROTO_TIMESTAMP = 0x04,
	IPROTO_SCHEMA_ID = 0x05,
	/* Leave a gap for other keys in the header. */
	IPROTO_SPACE_ID = 0x10,
	IPROTO_INDEX_ID = 0x11,
	IPROTO_LIMIT = 0x12,
	IPROTO_OFFSET = 0x13,
	IPROTO_ITERATOR = 0x14,
	IPROTO_INDEX_BASE = 0x15,
	/* Leave a gap between integer values and other keys */
	IPROTO_KEY = 0x20,
	IPROTO_TUPLE = 0x21,
	IPROTO_FUNCTION_NAME = 0x22,
	IPROTO_USER_NAME = 0x23,
	/* Replication keys (body) */
	IPROTO_SERVER_UUID = 0x24,
	IPROTO_CLUSTER_UUID = 0x25,
	IPROTO_VCLOCK = 0x26,
	IPROTO_EXPR = 0x27, /* EVAL */
	IPROTO_OPS = 0x28, /* UPSERT but not UPDATE ops, because of legacy */
	/* Leave a gap between request keys and response keys */
	IPROTO_DATA = 0x30,
	IPROTO_ERROR = 0x31,
	IPROTO_KEY_MAX
};

#define bit(c) (1ULL<<IPROTO_##c)

#define IPROTO_HEAD_BMAP (bit(REQUEST_TYPE) | bit(SYNC) | bit(SERVER_ID) |\
			  bit(LSN) | bit(SCHEMA_ID))
#define IPROTO_BODY_BMAP (bit(SPACE_ID) | bit(INDEX_ID) | bit(LIMIT) |\
			  bit(OFFSET) | bit(ITERATOR) | bit(INDEX_BASE) |\
			  bit(KEY) | bit(TUPLE) | bit(FUNCTION_NAME) | \
			  bit(USER_NAME) | bit(EXPR) | bit(OPS))

static inline bool
xrow_header_has_key(const char *pos, const char *end)
{
	unsigned char key = pos < end ? *pos : (unsigned char) IPROTO_KEY_MAX;
	return key < IPROTO_KEY_MAX && IPROTO_HEAD_BMAP & (1ULL<<key);
}

static inline bool
iproto_body_has_key(const char *pos, const char *end)
{
	unsigned char key = pos < end ? *pos : (unsigned char) IPROTO_KEY_MAX;
	return key < IPROTO_KEY_MAX && IPROTO_BODY_BMAP & (1ULL<<key);
}

#undef bit

static inline uint64_t
iproto_key_bit(unsigned char key)
{
	return 1ULL << key;
}

extern const unsigned char iproto_key_type[IPROTO_KEY_MAX];

/**
 * IPROTO command codes
 */
enum iproto_type {
	/* command is successful */
	IPROTO_OK = 0,
	/* dml command codes (see extra dml command codes) */
	IPROTO_SELECT = 1,
	IPROTO_INSERT = 2,
	IPROTO_REPLACE = 3,
	IPROTO_UPDATE = 4,
	IPROTO_DELETE = 5,
	IPROTO_CALL = 6,
	IPROTO_AUTH = 7,
	IPROTO_EVAL = 8,
	IPROTO_UPSERT = 9,
	IPROTO_TYPE_STAT_MAX = IPROTO_UPSERT + 1,
	/* admin command codes */
	IPROTO_PING = 64,
	IPROTO_JOIN = 65,
	IPROTO_SUBSCRIBE = 66,
	IPROTO_TYPE_ADMIN_MAX = IPROTO_SUBSCRIBE + 1,
	/* command failed = (IPROTO_TYPE_ERROR | ER_XXX from errcode.h) */
	IPROTO_TYPE_ERROR = 1 << 15
};

extern const char *iproto_type_strs[];
/** Key names. */
extern const char *iproto_key_strs[];
/** A map of mandatory members of an iproto DML request. */
extern const uint64_t iproto_body_key_map[];

static inline const char *
iproto_type_name(uint32_t type)
{
	if (type >= IPROTO_TYPE_STAT_MAX)
		return "unknown";
	return iproto_type_strs[type];
}

static inline bool
iproto_type_is_select(uint32_t type)
{
	return type <= IPROTO_SELECT || type == IPROTO_CALL;
}

static inline bool
iproto_type_is_dml(uint32_t type)
{
	return (type >= IPROTO_SELECT && type <= IPROTO_DELETE) ||
		type == IPROTO_UPSERT;
}

static inline bool
iproto_type_is_error(uint32_t type)
{
	return (type & IPROTO_TYPE_ERROR) != 0;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED */
