#ifndef TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED
#define TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED
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
#include <stdbool.h>
#include <stdint.h>
#include <trivia/util.h>

#include <msgpuck.h>

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
	IPROTO_REPLICA_ID = 0x02,
	IPROTO_LSN = 0x03,
	IPROTO_TIMESTAMP = 0x04,
	IPROTO_SCHEMA_VERSION = 0x05,
	IPROTO_SERVER_VERSION = 0x06,
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

	/*
	 * Replication keys (body).
	 * Unfortunately, there is no gap between request and
	 * replication keys (between USER_NAME and INSTANCE_UUID).
	 * So imagine, that OPS, EXPR and FIELD_NAME keys follows
	 * the USER_NAME key.
	 */
	IPROTO_INSTANCE_UUID = 0x24,
	IPROTO_CLUSTER_UUID = 0x25,
	IPROTO_VCLOCK = 0x26,

	/* Also request keys. See the comment above. */
	IPROTO_EXPR = 0x27, /* EVAL */
	IPROTO_OPS = 0x28, /* UPSERT but not UPDATE ops, because of legacy */

	/* Leave a gap between request keys and response keys */
	IPROTO_DATA = 0x30,
	IPROTO_ERROR = 0x31,
	/**
	 * IPROTO_METADATA: [
	 *      { IPROTO_FIELD_NAME: name },
	 *      { ... },
	 *      ...
	 * ]
	 */
	IPROTO_METADATA = 0x32,

	/* Leave a gap between response keys and SQL keys. */
	IPROTO_SQL_TEXT = 0x40,
	IPROTO_SQL_BIND = 0x41,
	IPROTO_SQL_OPTIONS = 0x42,
	/**
	 * IPROTO_SQL_INFO: {
	 *     IPROTO_SQL_ROW_COUNT: number
	 * }
	 */
	IPROTO_SQL_INFO = 0x43,
	IPROTO_SQL_ROW_COUNT = 0x44,
	IPROTO_KEY_MAX
};

/**
 * Keys, stored in IPROTO_METADATA. They can not be received
 * in a request. Only sent as response, so no necessity in _strs
 * or _key_type arrays.
 */
enum iproto_metadata_key {
	IPROTO_FIELD_NAME = 0,
};

#define bit(c) (1ULL<<IPROTO_##c)

#define IPROTO_HEAD_BMAP (bit(REQUEST_TYPE) | bit(SYNC) | bit(REPLICA_ID) |\
			  bit(LSN) | bit(SCHEMA_VERSION))
#define IPROTO_DML_BODY_BMAP (bit(SPACE_ID) | bit(INDEX_ID) | bit(LIMIT) |\
			      bit(OFFSET) | bit(ITERATOR) | bit(INDEX_BASE) |\
			      bit(KEY) | bit(TUPLE) | bit(OPS))

static inline bool
xrow_header_has_key(const char *pos, const char *end)
{
	unsigned char key = pos < end ? *pos : (unsigned char) IPROTO_KEY_MAX;
	return key < IPROTO_KEY_MAX && IPROTO_HEAD_BMAP & (1ULL<<key);
}

static inline bool
iproto_dml_body_has_key(const char *pos, const char *end)
{
	unsigned char key = pos < end ? *pos : (unsigned char) IPROTO_KEY_MAX;
	return key < IPROTO_KEY_MAX && IPROTO_DML_BODY_BMAP & (1ULL<<key);
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
	/** Acknowledgement that request or command is successful */
	IPROTO_OK = 0,

	/** SELECT request */
	IPROTO_SELECT = 1,
	/** INSERT request */
	IPROTO_INSERT = 2,
	/** REPLACE request */
	IPROTO_REPLACE = 3,
	/** UPDATE request */
	IPROTO_UPDATE = 4,
	/** DELETE request */
	IPROTO_DELETE = 5,
	/** CALL request - wraps result into [tuple, tuple, ...] format */
	IPROTO_CALL_16 = 6,
	/** AUTH request */
	IPROTO_AUTH = 7,
	/** EVAL request */
	IPROTO_EVAL = 8,
	/** UPSERT request */
	IPROTO_UPSERT = 9,
	/** CALL request - returns arbitrary MessagePack */
	IPROTO_CALL = 10,
	/** Execute an SQL statement. */
	IPROTO_EXECUTE = 11,
	/** No operation. Treated as DML, used to bump LSN. */
	IPROTO_NOP = 12,
	/** The maximum typecode used for box.stat() */
	IPROTO_TYPE_STAT_MAX,

	/** PING request */
	IPROTO_PING = 64,
	/** Replication JOIN command */
	IPROTO_JOIN = 65,
	/** Replication SUBSCRIBE command */
	IPROTO_SUBSCRIBE = 66,
	/** Vote request command for master election */
	IPROTO_REQUEST_VOTE = 67,

	/** Vinyl run info stored in .index file */
	VY_INDEX_RUN_INFO = 100,
	/** Vinyl page info stored in .index file */
	VY_INDEX_PAGE_INFO = 101,
	/** Vinyl row index stored in .run file */
	VY_RUN_ROW_INDEX = 102,

	/**
	 * Error codes = (IPROTO_TYPE_ERROR | ER_XXX from errcode.h)
	 */
	IPROTO_TYPE_ERROR = 1 << 15
};

/** IPROTO type name by code */
extern const char *iproto_type_strs[];

/**
 * Returns IPROTO type name by @a type code.
 * @param type IPROTO type.
 */
static inline const char *
iproto_type_name(uint32_t type)
{
	/*
	 * Sic: iptoto_type_strs[IPROTO_NOP] is NULL
	 * to suppress box.stat() output.
	 */
	if (type == IPROTO_NOP)
		return "NOP";

	if (type < IPROTO_TYPE_STAT_MAX)
		return iproto_type_strs[type];

	switch (type) {
	case VY_INDEX_RUN_INFO:
		return "RUNINFO";
	case VY_INDEX_PAGE_INFO:
		return "PAGEINFO";
	case VY_RUN_ROW_INDEX:
		return "ROWINDEX";
	default:
		return NULL;
	}
}

/**
 * Returns IPROTO key name by @a key code.
 * @param key IPROTO key.
 */
static inline const char *
iproto_key_name(enum iproto_key key)
{
	extern const char *iproto_key_strs[];
	if (key >= IPROTO_KEY_MAX)
		return NULL;
	return iproto_key_strs[key];
}

/** A data manipulation request. */
static inline bool
iproto_type_is_dml(uint32_t type)
{
	return (type >= IPROTO_SELECT && type <= IPROTO_DELETE) ||
		type == IPROTO_UPSERT || type == IPROTO_NOP;
}

/**
 * Returns a map of mandatory members of IPROTO DML request.
 * @param type iproto type.
 */
static inline uint64_t
dml_request_key_map(uint32_t type)
{
	/** Advanced requests don't have a defined key map. */
	assert(iproto_type_is_dml(type));
	extern const uint64_t iproto_body_key_map[];
	return iproto_body_key_map[type];
}

/**
 * A read only request, CALL is included since it
 * may be read-only, and there are separate checks
 * for all database requests issues from CALL.
 */
static inline bool
iproto_type_is_select(uint32_t type)
{
	return type <= IPROTO_SELECT || type == IPROTO_CALL || type == IPROTO_EVAL;
}

/** A common request with a mandatory and simple body (key, tuple, ops)  */
static inline bool
iproto_type_is_request(uint32_t type)
{
	return type > IPROTO_OK && type <= IPROTO_TYPE_STAT_MAX;
}

/**
 * The request is "synchronous": no other requests
 * on this connection should be taken before this one
 * ends.
 */
static inline bool
iproto_type_is_sync(uint32_t type)
{
	return type == IPROTO_JOIN || type == IPROTO_SUBSCRIBE;
}

/** This is an error. */
static inline bool
iproto_type_is_error(uint32_t type)
{
	return (type & IPROTO_TYPE_ERROR) != 0;
}

/** The snapshot row metadata repeats the structure of REPLACE request. */
struct PACKED request_replace_body {
	uint8_t m_body;
	uint8_t k_space_id;
	uint8_t m_space_id;
	uint32_t v_space_id;
	uint8_t k_tuple;
};

/**
 * Xrow keys for Vinyl run information.
 * @sa struct vy_run_info.
 */
enum vy_run_info_key {
	/** Min key in the run. */
	VY_RUN_INFO_MIN_KEY = 1,
	/** Max key in the run. */
	VY_RUN_INFO_MAX_KEY = 2,
	/** Min LSN over all statements in the run. */
	VY_RUN_INFO_MIN_LSN = 3,
	/** Max LSN over all statements in the run. */
	VY_RUN_INFO_MAX_LSN = 4,
	/** Number of pages in the run. */
	VY_RUN_INFO_PAGE_COUNT = 5,
	/** Bloom filter for keys. */
	VY_RUN_INFO_BLOOM = 6,
	/** The last key in this enum + 1 */
	VY_RUN_INFO_KEY_MAX
};

/**
 * Return vy_run_info key name by @a key code.
 * @param key key
 */
static inline const char *
vy_run_info_key_name(enum vy_run_info_key key)
{
	if (key <= 0 || key >= VY_RUN_INFO_KEY_MAX)
		return NULL;
	extern const char *vy_run_info_key_strs[];
	return vy_run_info_key_strs[key];
}

/**
 * Xrow keys for Vinyl page information.
 * @sa struct vy_run_info.
 */
enum vy_page_info_key {
	/** Offset of page data in the run file. */
	VY_PAGE_INFO_OFFSET = 1,
	/** Size of page data in the run file. */
	VY_PAGE_INFO_SIZE = 2,
	/** Size of page data in memory, i.e. unpacked. */
	VY_PAGE_INFO_UNPACKED_SIZE = 3,
	/* Number of statements in the page. */
	VY_PAGE_INFO_ROW_COUNT = 4,
	/* Minimal key stored in the page. */
	VY_PAGE_INFO_MIN_KEY = 5,
	/** Offset of the row index in the page. */
	VY_PAGE_INFO_ROW_INDEX_OFFSET = 6,
	/** The last key in this enum + 1 */
	VY_PAGE_INFO_KEY_MAX
};

/**
 * Return vy_page_info key name by @a key code.
 * @param key key
 */
static inline const char *
vy_page_info_key_name(enum vy_page_info_key key)
{
	if (key <= 0 || key >= VY_PAGE_INFO_KEY_MAX)
		return NULL;
	extern const char *vy_page_info_key_strs[];
	return vy_page_info_key_strs[key];
}

/**
 * Xrow keys for Vinyl row index.
 * @sa struct vy_page_info.
 */
enum vy_row_index_key {
	/** Array of row offsets. */
	VY_ROW_INDEX_DATA = 1,
	/** The last key in this enum + 1 */
	VY_ROW_INDEX_KEY_MAX
};

/**
 * Return vy_page_info key name by @a key code.
 * @param key key
 */
static inline const char *
vy_row_index_key_name(enum vy_row_index_key key)
{
	if (key <= 0 || key >= VY_ROW_INDEX_KEY_MAX)
		return NULL;
	extern const char *vy_row_index_key_strs[];
	return vy_row_index_key_strs[key];
}

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED */
