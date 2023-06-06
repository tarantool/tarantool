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

/** IPROTO_FLAGS bitfield constants. */
#define IPROTO_FLAGS(_)							\
	/** Set for the last xrow in a transaction. */			\
	_(COMMIT, 0)							\
	/** Set for the last row of a tx residing in limbo. */		\
	_(WAIT_SYNC, 1)							\
	/** Set for the last row of a synchronous tx. */		\
	_(WAIT_ACK, 2)							\

#define IPROTO_FLAG_MEMBER(s, v) IPROTO_FLAG_ ## s = 1ULL << (v),

enum iproto_flag {
	IPROTO_FLAGS(IPROTO_FLAG_MEMBER)
};

#define IPROTO_FLAG_BIT_MEMBER(s, v) IPROTO_FLAG_BIT_ ## s = v,

enum iproto_flag_bit {
	IPROTO_FLAGS(IPROTO_FLAG_BIT_MEMBER)
	iproto_flag_bit_MAX,
};

/** IPROTO flag name by bit number. */
extern const char *iproto_flag_bit_strs[];

/**
 * IPROTO key name, code, and MsgPack value type.
 */
#define IPROTO_KEYS(_)							\
	_(REQUEST_TYPE, 0x00, MP_UINT)					\
	_(SYNC, 0x01, MP_UINT)						\
									\
	/* Replication keys (header) */					\
	_(REPLICA_ID, 0x02, MP_UINT)					\
	_(LSN, 0x03, MP_UINT)						\
	_(TIMESTAMP, 0x04, MP_DOUBLE)					\
	_(SCHEMA_VERSION, 0x05, MP_UINT)				\
	_(SERVER_VERSION, 0x06, MP_UINT)				\
	_(GROUP_ID, 0x07, MP_UINT)					\
	_(TSN, 0x08, MP_UINT)						\
	_(FLAGS, 0x09, MP_UINT)						\
	_(STREAM_ID, 0x0a, MP_UINT)					\
	/* Leave a gap for other keys in the header. */			\
	_(SPACE_ID, 0x10, MP_UINT)					\
	_(INDEX_ID, 0x11, MP_UINT)					\
	_(LIMIT, 0x12, MP_UINT)						\
	_(OFFSET, 0x13, MP_UINT)					\
	_(ITERATOR, 0x14, MP_UINT)					\
	_(INDEX_BASE, 0x15, MP_UINT)					\
	/* Leave a gap between integer values and other keys */		\
	/**
	 * Flag indicating the need to send position of
	 * last selected tuple in response.
	 */								\
	_(FETCH_POSITION, 0x1f, MP_BOOL)				\
	_(KEY, 0x20, MP_ARRAY)						\
	_(TUPLE, 0x21, MP_ARRAY)					\
	_(FUNCTION_NAME, 0x22, MP_STR)					\
	_(USER_NAME, 0x23, MP_STR)					\
									\
	/*
	 * Replication keys (body).
	 * Unfortunately, there is no gap between request and
	 * replication keys (between USER_NAME and INSTANCE_UUID).
	 * So imagine, that OPS, EXPR and FIELD_NAME keys follows
	 * the USER_NAME key.
	 */								\
	_(INSTANCE_UUID, 0x24, MP_STR)					\
	_(REPLICASET_UUID, 0x25, MP_STR)				\
	_(VCLOCK, 0x26, MP_MAP)						\
									\
	/* Also request keys. See the comment above. */			\
	_(EXPR,  0x27, MP_STR) /* EVAL */				\
	/* UPSERT but not UPDATE ops, because of legacy */		\
	_(OPS, 0x28, MP_ARRAY)						\
	_(BALLOT, 0x29, MP_MAP)						\
	_(TUPLE_META, 0x2a, MP_MAP)					\
	_(OPTIONS, 0x2b, MP_MAP)					\
	/** Old tuple (i.e. before DML request is applied). */		\
	_(OLD_TUPLE, 0x2c, MP_ARRAY)					\
	/** New tuple (i.e. result of DML request). */			\
	_(NEW_TUPLE, 0x2d, MP_ARRAY)					\
	/**
	 * Position of last selected tuple to start iteration after it.
	 */								\
	_(AFTER_POSITION, 0x2e, MP_STR)					\
	/** Last selected tuple to start iteration after it. */		\
	_(AFTER_TUPLE, 0x2f, MP_ARRAY)					\
									\
	/** Response keys. */						\
	_(DATA, 0x30, MP_ARRAY)						\
	_(ERROR_24, 0x31, MP_STR)					\
	/**
	 * IPROTO_METADATA: [
	 *      { IPROTO_FIELD_NAME: name },
	 *      { ... },
	 *      ...
	 * ]
	 */								\
	_(METADATA, 0x32, MP_ARRAY)					\
	_(BIND_METADATA, 0x33, MP_ARRAY)				\
	_(BIND_COUNT, 0x34, MP_UINT)					\
	/** Position of last selected tuple in response. */		\
	_(POSITION, 0x35, MP_STR)					\
									\
	/* Leave a gap between response keys and SQL keys. */		\
	_(SQL_TEXT, 0x40, MP_STR)					\
	_(SQL_BIND, 0x41, MP_ARRAY)					\
	/**
	 * IPROTO_SQL_INFO: {
	 *      SQL_INFO_ROW_COUNT: number
	 * }
	 */								\
	_(SQL_INFO, 0x42, MP_MAP)					\
	_(STMT_ID, 0x43, MP_UINT)					\
	/* Leave a gap between SQL keys and additional request keys */	\
	_(REPLICA_ANON, 0x50, MP_BOOL)					\
	_(ID_FILTER, 0x51, MP_ARRAY)					\
	_(ERROR, 0x52, MP_MAP)						\
	/**
	 * Term. Has the same meaning as IPROTO_RAFT_TERM, but is an iproto
	 * key, rather than a raft key. Used for PROMOTE request, which needs
	 * both iproto (e.g. REPLICA_ID) and raft (RAFT_TERM) keys.
	 */								\
	_(TERM, 0x53, MP_UINT)						\
	/** Protocol version. */					\
	_(VERSION, 0x54, MP_UINT)					\
	/** Protocol features. */					\
	_(FEATURES, 0x55, MP_ARRAY)					\
	/** Operation timeout. Specific to request type. */		\
	_(TIMEOUT, 0x56, MP_DOUBLE)					\
	/** Key name and data sent to a remote watcher. */		\
	_(EVENT_KEY, 0x57, MP_STR)					\
	_(EVENT_DATA, 0x58, MP_NIL)					\
	/** Isolation level, is used only by IPROTO_BEGIN request. */	\
	_(TXN_ISOLATION, 0x59, MP_UINT)					\
	/** A vclock synchronisation request identifier. */		\
	_(VCLOCK_SYNC, 0x5a, MP_UINT)					\
	/**
	 * Name of the authentication method that is currently used on
	 * the server (value of box.cfg.auth_type). It's sent in reply
	 * to IPROTO_ID request. A client can use it as the default
	 * authentication method.
	 */								\
	_(AUTH_TYPE, 0x5b, MP_STR)					\
	_(REPLICASET_NAME, 0x5c, MP_STR)				\
	_(INSTANCE_NAME, 0x5d, MP_STR)					\
	/**
	 * Space name used instead of identifier (IPROTO_SPACE_ID) in DML
	 * requests. Preferred when identifier is present (i.e., the identifier
	 * is ignored).
	 */								\
	_(SPACE_NAME, 0x5e, MP_STR)					\
	/**
	 * Index name used instead of identifier (IPROTO_INDEX_ID) in
	 * IPROTO_SELECT, IPROTO_UPDATE, and IPROTO_DELETE requests. Preferred
	 * when identifier is present (i.e., the identifier is ignored).
	 */								\
	_(INDEX_NAME, 0x5f, MP_STR)					\

#define IPROTO_KEY_MEMBER(s, v, ...) IPROTO_ ## s = v,

enum iproto_key {
	IPROTO_KEYS(IPROTO_KEY_MEMBER)
	iproto_key_MAX
};

/**
 * Be careful not to extend iproto_key values over 0x7f.
 * iproto_keys are encoded in msgpack as positive fixnum, which ends at
 * 0x7f, and we rely on this in some places by allocating a uint8_t to
 * hold a msgpack-encoded key value.
 */
static_assert(iproto_key_MAX <= 0x80, "iproto_key_MAX must be <= 0x80");

/** IPROTO key name by code. */
extern const char *iproto_key_strs[];

/** MsgPack value type by IPROTO key. */
extern const unsigned char iproto_key_type[];

/**
 * Keys, stored in IPROTO_METADATA. They can not be received
 * in a request. Only sent as response, so no necessity in _strs
 * or _key_type arrays.
 */
#define IPROTO_METADATA_KEYS(_)						\
	_(NAME, 0)							\
	_(TYPE, 1)							\
	_(COLL, 2)							\
	_(IS_NULLABLE, 3)						\
	_(IS_AUTOINCREMENT, 4)						\
	_(SPAN, 5)							\

#define IPROTO_METADATA_KEY_MEMBER(s, v) IPROTO_FIELD_ ## s = v,

enum iproto_metadata_key {
	IPROTO_METADATA_KEYS(IPROTO_METADATA_KEY_MEMBER)
	iproto_metadata_key_MAX
};

/** IPROTO metadata key name by code */
extern const char *iproto_metadata_key_strs[];

#define IPROTO_BALLOT_KEYS(_)						\
	_(IS_RO_CFG, 0x01)						\
	_(VCLOCK, 0x02)							\
	_(GC_VCLOCK, 0x03)						\
	_(IS_RO, 0x04)							\
	_(IS_ANON, 0x05)						\
	_(IS_BOOTED, 0x06)						\
	_(CAN_LEAD, 0x07)						\
	_(BOOTSTRAP_LEADER_UUID, 0x08)					\
	_(REGISTERED_REPLICA_UUIDS, 0x09)				\

#define IPROTO_BALLOT_KEY_MEMBER(s, v) IPROTO_BALLOT_ ## s = v,

enum iproto_ballot_key {
	IPROTO_BALLOT_KEYS(IPROTO_BALLOT_KEY_MEMBER)
	iproto_ballot_key_MAX
};

/** IPROTO ballot key name by code */
extern const char *iproto_ballot_key_strs[];

static inline uint64_t
iproto_key_bit(unsigned char key)
{
	return 1ULL << key;
}

/** IPROTO command codes. */
#define IPROTO_TYPES(_)							\
	/** Acknowledgement that request or command is successful */	\
	_(OK, 0)							\
									\
	/** SELECT request */						\
	_(SELECT, 1)							\
	/** INSERT request */						\
	_(INSERT, 2)							\
	/** REPLACE request */						\
	_(REPLACE, 3)							\
	/** UPDATE request */						\
	_(UPDATE, 4)							\
	/** DELETE request */						\
	_(DELETE, 5)							\
	/**
	 * CALL request - wraps result into [tuple, tuple, ...] format
	 */								\
	_(CALL_16, 6)							\
	/** AUTH request */						\
	_(AUTH, 7)							\
	/** EVAL request */						\
	_(EVAL, 8)							\
	/** UPSERT request */						\
	_(UPSERT, 9)							\
	/** CALL request - returns arbitrary MessagePack */		\
	_(CALL, 10)							\
	/** Execute an SQL statement. */				\
	_(EXECUTE, 11)							\
	/** No operation. Treated as DML, used to bump LSN. */		\
	_(NOP, 12)							\
	/** Prepare SQL statement. */					\
	_(PREPARE, 13)							\
	/* Begin transaction */						\
	_(BEGIN, 14)							\
	/* Commit transaction */					\
	_(COMMIT, 15)							\
	/* Rollback transaction */					\
	_(ROLLBACK, 16)							\
									\
	_(RAFT, 30)							\
	/** PROMOTE request. */						\
	_(RAFT_PROMOTE, 31)						\
	/** DEMOTE request. */						\
	_(RAFT_DEMOTE, 32)						\
									\
	/** A confirmation message for synchronous transactions. */	\
	_(RAFT_CONFIRM, 40)						\
	/** A rollback message for synchronous transactions. */		\
	_(RAFT_ROLLBACK, 41)						\
									\
	/** PING request */						\
	_(PING, 64)							\
	/** Replication JOIN command */					\
	_(JOIN, 65)							\
	/** Replication SUBSCRIBE command */				\
	_(SUBSCRIBE, 66)						\
	/** DEPRECATED: use IPROTO_VOTE instead */			\
	_(VOTE_DEPRECATED, 67)						\
	/** Vote request command for master election */			\
	_(VOTE, 68)							\
	/** Anonymous replication FETCH SNAPSHOT. */			\
	_(FETCH_SNAPSHOT, 69)						\
	/** REGISTER request to leave anonymous replication. */		\
	_(REGISTER, 70)							\
	_(JOIN_META, 71)						\
	_(JOIN_SNAPSHOT, 72)						\
	/** Protocol features request. */				\
	_(ID, 73)							\
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
	 * doesn't send a packet in reply to any of them. Still, the server
	 * sends the same sync number in an IPROTO_EVENT packet as the one sent
	 * by the client in the last corresponding IPROTO_WATCH request.
	 */								\
	_(WATCH, 74)							\
	_(UNWATCH, 75)							\
	_(EVENT, 76)							\
	/**
	 * Synchronous request to fetch the data that is currently attached to
	 * a notification key without subscribing to changes.
	 */								\
	_(WATCH_ONCE, 77)						\
									\
	/**
	 * The following three requests are reserved for vinyl types.
	 *
	 * VY_INDEX_RUN_INFO = 100
	 * VY_INDEX_PAGE_INFO = 101
	 * VY_RUN_ROW_INDEX = 102
	 */								\
									\
	/** Non-final response type. */					\
	_(CHUNK, 128)							\

#define IPROTO_TYPE_MEMBER(s, v) IPROTO_ ## s = v,

enum iproto_type {
	IPROTO_TYPES(IPROTO_TYPE_MEMBER)
	iproto_type_MAX,

	/** Error codes = (IPROTO_TYPE_ERROR | ER_XXX from errcode.h) */
	IPROTO_TYPE_ERROR = 1 << 15,

	/** Used for overriding the unknown request handler */
	IPROTO_UNKNOWN = -1,

	/** The maximum typecode used for box.stat() */
	IPROTO_TYPE_STAT_MAX = IPROTO_ROLLBACK + 1,

	/** Vinyl run info stored in .index file */
	VY_INDEX_RUN_INFO = 100,
	/** Vinyl page info stored in .index file */
	VY_INDEX_PAGE_INFO = 101,
	/** Vinyl row index stored in .run file */
	VY_RUN_ROW_INDEX = 102,
};

/** IPROTO type name by code */
extern const char *iproto_type_strs[];

#define IPROTO_RAFT_KEYS(_)						\
	_(TERM, 0)							\
	_(VOTE, 1)							\
	_(STATE, 2)							\
	_(VCLOCK, 3)							\
	_(LEADER_ID, 4)							\
	_(IS_LEADER_SEEN, 5)						\

#define IPROTO_RAFT_KEY_MEMBER(s, v) IPROTO_RAFT_ ## s = v,

enum iproto_raft_key {
	IPROTO_RAFT_KEYS(IPROTO_RAFT_KEY_MEMBER)
	iproto_raft_key_MAX
};

/** IPROTO raft key name by code */
extern const char *iproto_raft_key_strs[];

/**
 * Returns IPROTO type name by @a type code.
 * @param type IPROTO type.
 */
static inline const char *
iproto_type_name(uint16_t type)
{
	if (type < iproto_type_MAX &&
	    iproto_type_strs[type] != NULL)
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
	if (key >= iproto_key_MAX)
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
#define VY_RUN_INFO_KEYS(_)						\
	/** Min key in the run. */					\
	_(MIN_KEY, 1)							\
	/** Max key in the run. */					\
	_(MAX_KEY, 2)							\
	/** Min LSN over all statements in the run. */			\
	_(MIN_LSN, 3)							\
	/** Max LSN over all statements in the run. */			\
	_(MAX_LSN, 4)							\
	/** Number of pages in the run. */				\
	_(PAGE_COUNT, 5)						\
	/** Legacy bloom filter implementation. */			\
	_(BLOOM_FILTER_LEGACY, 6)					\
	/** Bloom filter for keys. */					\
	_(BLOOM_FILTER, 7)						\
	/** Number of statements of each type (map). */			\
	_(STMT_STAT, 8)							\

#define VY_RUN_INFO_KEY_MEMBER(s, v) VY_RUN_INFO_ ## s = v,

enum vy_run_info_key {
	VY_RUN_INFO_KEYS(VY_RUN_INFO_KEY_MEMBER)
	vy_run_info_key_MAX
};

/**
 * Return vy_run_info key name by @a key code.
 * @param key key
 */
static inline const char *
vy_run_info_key_name(enum vy_run_info_key key)
{
	if (key <= 0 || key >= vy_run_info_key_MAX)
		return NULL;
	extern const char *vy_run_info_key_strs[];
	return vy_run_info_key_strs[key];
}

/**
 * Xrow keys for Vinyl page information.
 * @sa struct vy_run_info.
 */
#define VY_PAGE_INFO_KEYS(_)						\
	/** Offset of page data in the run file. */			\
	_(OFFSET, 1)							\
	/** Size of page data in the run file. */			\
	_(SIZE, 2)							\
	/** Size of page data in memory, i.e. unpacked. */		\
	_(UNPACKED_SIZE, 3)						\
	/* Number of statements in the page. */				\
	_(ROW_COUNT, 4)							\
	/* Minimal key stored in the page. */				\
	_(MIN_KEY, 5)							\
	/** Offset of the row index in the page. */			\
	_(ROW_INDEX_OFFSET, 6)						\

#define VY_PAGE_INFO_KEY_MEMBER(s, v) VY_PAGE_INFO_ ## s = v,

enum vy_page_info_key {
	VY_PAGE_INFO_KEYS(VY_PAGE_INFO_KEY_MEMBER)
	vy_page_info_key_MAX
};

/**
 * Return vy_page_info key name by @a key code.
 * @param key key
 */
static inline const char *
vy_page_info_key_name(enum vy_page_info_key key)
{
	if (key <= 0 || key >= vy_page_info_key_MAX)
		return NULL;
	extern const char *vy_page_info_key_strs[];
	return vy_page_info_key_strs[key];
}

/**
 * Xrow keys for Vinyl row index.
 * @sa struct vy_page_info.
 */
#define VY_ROW_INDEX_KEYS(_)						\
	/** Array of row offsets. */					\
	_(DATA, 1)							\

#define VY_ROW_INDEX_KEY_MEMBER(s, v) VY_ROW_INDEX_ ## s = v,

enum vy_row_index_key {
	VY_ROW_INDEX_KEYS(VY_ROW_INDEX_KEY_MEMBER)
	vy_row_index_key_MAX
};

/**
 * Return vy_page_info key name by @a key code.
 * @param key key
 */
static inline const char *
vy_row_index_key_name(enum vy_row_index_key key)
{
	if (key <= 0 || key >= vy_row_index_key_MAX)
		return NULL;
	extern const char *vy_row_index_key_strs[];
	return vy_row_index_key_strs[key];
}

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED */
