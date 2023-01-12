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

/**
 * IPROTO_FLAGS bitfield constants.
 * `box.iproto.flag` needs to be updated correspondingly.
 */
enum {
	/** Set for the last xrow in a transaction. */
	IPROTO_FLAG_COMMIT = 0x01,
	/** Set for the last row of a tx residing in limbo. */
	IPROTO_FLAG_WAIT_SYNC = 0x02,
	/** Set for the last row of a synchronous tx. */
	IPROTO_FLAG_WAIT_ACK = 0x04,
};

/**
 * `box.iproto.key` needs to be updated correspondingly.
 */
enum iproto_key {
	IPROTO_REQUEST_TYPE = 0x00,
	IPROTO_SYNC = 0x01,

	/* Replication keys (header) */
	IPROTO_REPLICA_ID = 0x02,
	IPROTO_LSN = 0x03,
	IPROTO_TIMESTAMP = 0x04,
	IPROTO_SCHEMA_VERSION = 0x05,
	IPROTO_SERVER_VERSION = 0x06,
	IPROTO_GROUP_ID = 0x07,
	IPROTO_TSN = 0x08,
	IPROTO_FLAGS = 0x09,
	IPROTO_STREAM_ID = 0x0a,
	/* Leave a gap for other keys in the header. */
	IPROTO_SPACE_ID = 0x10,
	IPROTO_INDEX_ID = 0x11,
	IPROTO_LIMIT = 0x12,
	IPROTO_OFFSET = 0x13,
	IPROTO_ITERATOR = 0x14,
	IPROTO_INDEX_BASE = 0x15,
	/* Leave a gap between integer values and other keys */
	/**
	 * Flag indicating the need to send position of
	 * last selected tuple in response.
	 */
	IPROTO_FETCH_POSITION = 0x1f,
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
	IPROTO_REPLICASET_UUID = 0x25,
	IPROTO_VCLOCK = 0x26,

	/* Also request keys. See the comment above. */
	IPROTO_EXPR = 0x27, /* EVAL */
	IPROTO_OPS = 0x28, /* UPSERT but not UPDATE ops, because of legacy */
	IPROTO_BALLOT = 0x29,
	IPROTO_TUPLE_META = 0x2a,
	IPROTO_OPTIONS = 0x2b,
	/** Old tuple (i.e. before DML request is applied). */
	IPROTO_OLD_TUPLE = 0x2c,
	/** New tuple (i.e. result of DML request). */
	IPROTO_NEW_TUPLE = 0x2d,
	/** Position of last selected tuple to start iteration after it. */
	IPROTO_AFTER_POSITION = 0x2e,
	/** Last selected tuple to start iteration after it. */
	IPROTO_AFTER_TUPLE = 0x2f,

	/** Response keys. */
	IPROTO_DATA = 0x30,
	IPROTO_ERROR_24 = 0x31,
	/**
	 * IPROTO_METADATA: [
	 *      { IPROTO_FIELD_NAME: name },
	 *      { ... },
	 *      ...
	 * ]
	 */
	IPROTO_METADATA = 0x32,
	IPROTO_BIND_METADATA = 0x33,
	IPROTO_BIND_COUNT = 0x34,
	/** Position of last selected tuple in response. */
	IPROTO_POSITION = 0x35,

	/* Leave a gap between response keys and SQL keys. */
	IPROTO_SQL_TEXT = 0x40,
	IPROTO_SQL_BIND = 0x41,
	/**
	 * IPROTO_SQL_INFO: {
	 *     SQL_INFO_ROW_COUNT: number
	 * }
	 */
	IPROTO_SQL_INFO = 0x42,
	IPROTO_STMT_ID = 0x43,
	/* Leave a gap between SQL keys and additional request keys */
	IPROTO_REPLICA_ANON = 0x50,
	IPROTO_ID_FILTER = 0x51,
	IPROTO_ERROR = 0x52,
	/**
	 * Term. Has the same meaning as IPROTO_RAFT_TERM, but is an iproto
	 * key, rather than a raft key. Used for PROMOTE request, which needs
	 * both iproto (e.g. REPLICA_ID) and raft (RAFT_TERM) keys.
	 */
	IPROTO_TERM = 0x53,
	/** Protocol version. */
	IPROTO_VERSION = 0x54,
	/** Protocol features. */
	IPROTO_FEATURES = 0x55,
	/** Operation timeout. Specific to request type. */
	IPROTO_TIMEOUT = 0x56,
	/** Key name and data sent to a remote watcher. */
	IPROTO_EVENT_KEY = 0x57,
	IPROTO_EVENT_DATA = 0x58,
	/** Isolation level, is used only by IPROTO_BEGIN request. */
	IPROTO_TXN_ISOLATION = 0x59,
	/** A vclock synchronisation request identifier. */
	IPROTO_VCLOCK_SYNC = 0x5a,
	/**
	 * Name of the authentication method that is currently used on
	 * the server (value of box.cfg.auth_type). It's sent in reply
	 * to IPROTO_ID request. A client can use it as the default
	 * authentication method.
	 */
	IPROTO_AUTH_TYPE = 0x5b,
	/*
	 * Be careful to not extend iproto_key values over 0x7f.
	 * iproto_keys are encoded in msgpack as positive fixnum, which ends at
	 * 0x7f, and we rely on this in some places by allocating a uint8_t to
	 * hold a msgpack-encoded key value.
	 */
	IPROTO_KEY_MAX
};

/**
 * Keys, stored in IPROTO_METADATA. They can not be received
 * in a request. Only sent as response, so no necessity in _strs
 * or _key_type arrays.
 * `box.iproto.metadata_key` needs to be updated correspondingly.
 */
enum iproto_metadata_key {
	IPROTO_FIELD_NAME = 0,
	IPROTO_FIELD_TYPE = 1,
	IPROTO_FIELD_COLL = 2,
	IPROTO_FIELD_IS_NULLABLE = 3,
	IPROTO_FIELD_IS_AUTOINCREMENT = 4,
	IPROTO_FIELD_SPAN = 5,
};

/**
 * `box.iproto.ballot_key` needs to be updated correspondingly.
 */
enum iproto_ballot_key {
	IPROTO_BALLOT_IS_RO_CFG = 0x01,
	IPROTO_BALLOT_VCLOCK = 0x02,
	IPROTO_BALLOT_GC_VCLOCK = 0x03,
	IPROTO_BALLOT_IS_RO = 0x04,
	IPROTO_BALLOT_IS_ANON = 0x05,
	IPROTO_BALLOT_IS_BOOTED = 0x06,
	IPROTO_BALLOT_CAN_LEAD = 0x07,
	IPROTO_BALLOT_BOOTSTRAP_LEADER_UUID = 0x08,
	IPROTO_BALLOT_REGISTERED_REPLICA_UUIDS = 0x09,
};

static inline uint64_t
iproto_key_bit(unsigned char key)
{
	return 1ULL << key;
}

extern const unsigned char iproto_key_type[IPROTO_KEY_MAX];

/**
 * IPROTO command codes.
 * `box.iproto.type` needs to be updated correspondingly.
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
	/** Prepare SQL statement. */
	IPROTO_PREPARE = 13,
	/* Begin transaction */
	IPROTO_BEGIN = 14,
	/* Commit transaction */
	IPROTO_COMMIT = 15,
	/* Rollback transaction */
	IPROTO_ROLLBACK = 16,
	/** The maximum typecode used for box.stat() */
	IPROTO_TYPE_STAT_MAX,

	IPROTO_RAFT = 30,
	/** PROMOTE request. */
	IPROTO_RAFT_PROMOTE = 31,
	/** DEMOTE request. */
	IPROTO_RAFT_DEMOTE = 32,

	/** A confirmation message for synchronous transactions. */
	IPROTO_RAFT_CONFIRM = 40,
	/** A rollback message for synchronous transactions. */
	IPROTO_RAFT_ROLLBACK = 41,

	/** PING request */
	IPROTO_PING = 64,
	/** Replication JOIN command */
	IPROTO_JOIN = 65,
	/** Replication SUBSCRIBE command */
	IPROTO_SUBSCRIBE = 66,
	/** DEPRECATED: use IPROTO_VOTE instead */
	IPROTO_VOTE_DEPRECATED = 67,
	/** Vote request command for master election */
	IPROTO_VOTE = 68,
	/** Anonymous replication FETCH SNAPSHOT. */
	IPROTO_FETCH_SNAPSHOT = 69,
	/** REGISTER request to leave anonymous replication. */
	IPROTO_REGISTER = 70,
	IPROTO_JOIN_META = 71,
	IPROTO_JOIN_SNAPSHOT = 72,
	/** Protocol features request. */
	IPROTO_ID = 73,
	/**
	 * The following three request types are used by the remote watcher
	 * protocol (box.watch over network), which operates as follows:
	 *
	 *  1. The client sends an IPROTO_WATCH packet to subscribe to changes
	 *     of a specified key defined on the server.
	 *  2. The server sends an IPROTO_EVENT packet to the subscribed client
	 *     with the key name and its current value unconditionally after
	 *     registration and then every time the key value is updated
	 *     provided the last notification was acknowledged (see below).
	 *  3. Upon receiving a notification, the client sends an IPROTO_WATCH
	 *     packet to acknowledge the notification.
	 *  4. When the client doesn't want to receive any more notifications,
	 *     it unsubscribes by sending an IPROTO_UNWATCH packet.
	 *
	 * All the three request types are fully asynchronous - a receiving end
	 * doesn't send a packet in reply to any of them (therefore neither of
	 * them has a sync number).
	 */
	IPROTO_WATCH = 74,
	IPROTO_UNWATCH = 75,
	IPROTO_EVENT = 76,

	/** Vinyl run info stored in .index file */
	VY_INDEX_RUN_INFO = 100,
	/** Vinyl page info stored in .index file */
	VY_INDEX_PAGE_INFO = 101,
	/** Vinyl row index stored in .run file */
	VY_RUN_ROW_INDEX = 102,

	/** Non-final response type. */
	IPROTO_CHUNK = 128,

	/**
	 * Error codes = (IPROTO_TYPE_ERROR | ER_XXX from errcode.h)
	 */
	IPROTO_TYPE_ERROR = 1 << 15,

	/**
	 * Used for overriding the unknown request handler.
	 */
	IPROTO_UNKNOWN = -1,
};

/** IPROTO type name by code */
extern const char *iproto_type_strs[];

/**
 * `box.iproto.raft_key` needs to be updated correspondingly.
 */
enum iproto_raft_keys {
	IPROTO_RAFT_TERM = 0,
	IPROTO_RAFT_VOTE = 1,
	IPROTO_RAFT_STATE = 2,
	IPROTO_RAFT_VCLOCK = 3,
	IPROTO_RAFT_LEADER_ID = 4,
	IPROTO_RAFT_IS_LEADER_SEEN = 5,
};

/**
 * Returns IPROTO type name by @a type code.
 * @param type IPROTO type.
 */
static inline const char *
iproto_type_name(uint16_t type)
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
	case IPROTO_JOIN:
		return "JOIN";
	case IPROTO_FETCH_SNAPSHOT:
		return "FETCH_SNAPSHOT";
	case IPROTO_REGISTER:
		return "REGISTER";
	case IPROTO_SUBSCRIBE:
		return "SUBSCRIBE";
	case IPROTO_RAFT:
		return "RAFT";
	case IPROTO_RAFT_PROMOTE:
		return "PROMOTE";
	case IPROTO_RAFT_DEMOTE:
		return "DEMOTE";
	case IPROTO_RAFT_CONFIRM:
		return "CONFIRM";
	case IPROTO_RAFT_ROLLBACK:
		return "ROLLBACK";
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

/** Predefined replication group identifiers. */
enum {
	/**
	 * Default replication group: changes made to the space
	 * are replicated throughout the entire cluster.
	 */
	GROUP_DEFAULT = 0,
	/**
	 * Replica local space: changes made to the space are
	 * not replicated.
	 */
	GROUP_LOCAL = 1,
};

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
iproto_type_is_dml(uint16_t type)
{
	return (type >= IPROTO_SELECT && type <= IPROTO_DELETE) ||
		type == IPROTO_UPSERT || type == IPROTO_NOP;
}

/**
 * Returns a map of mandatory members of IPROTO DML request.
 * @param type iproto type.
 */
static inline uint64_t
dml_request_key_map(uint16_t type)
{
	/** Advanced requests don't have a defined key map. */
	assert(iproto_type_is_dml(type));
	extern const uint64_t iproto_body_key_map[];
	return iproto_body_key_map[type];
}

/** Synchronous replication entries: CONFIRM/ROLLBACK/PROMOTE. */
static inline bool
iproto_type_is_synchro_request(uint16_t type)
{
	return type == IPROTO_RAFT_CONFIRM || type == IPROTO_RAFT_ROLLBACK ||
	       type == IPROTO_RAFT_PROMOTE || type == IPROTO_RAFT_DEMOTE;
}

/** PROMOTE/DEMOTE entry (synchronous replication and leader elections). */
static inline bool
iproto_type_is_promote_request(uint32_t type)
{
       return type == IPROTO_RAFT_PROMOTE || type == IPROTO_RAFT_DEMOTE;
}

static inline bool
iproto_type_is_raft_request(uint16_t type)
{
	return type == IPROTO_RAFT;
}

/** This is an error. */
static inline bool
iproto_type_is_error(uint16_t type)
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

static inline void
request_replace_body_create(struct request_replace_body *body,
			    uint32_t space_id)
{
	body->m_body = 0x82; /* map of two elements. */
	body->k_space_id = IPROTO_SPACE_ID;
	body->m_space_id = 0xce; /* uint32 */
	body->v_space_id = mp_bswap_u32(space_id);
	body->k_tuple = IPROTO_TUPLE;
}

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
	/** Legacy bloom filter implementation. */
	VY_RUN_INFO_BLOOM_LEGACY = 6,
	/** Bloom filter for keys. */
	VY_RUN_INFO_BLOOM = 7,
	/** Number of statements of each type (map). */
	VY_RUN_INFO_STMT_STAT = 8,
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
