#ifndef TARANTOOL_XROW_H_INCLUDED
#define TARANTOOL_XROW_H_INCLUDED
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
#include <stddef.h>
#include <sys/uio.h> /* struct iovec */

#include "diag.h"
#include "iproto_features.h"
#include "node_name.h"
#include "tt_uuid.h"
#include "vclock/vclock.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum {
	XROW_HEADER_IOVMAX = 1,
	XROW_BODY_IOVMAX = 2,
	XROW_IOVMAX = XROW_HEADER_IOVMAX + XROW_BODY_IOVMAX,
	XROW_HEADER_LEN_MAX = 52,
	XROW_BODY_LEN_MAX = 512,
	IPROTO_HEADER_LEN = 32,
	/** 7 = sizeof(iproto_body_bin). */
	IPROTO_SELECT_HEADER_LEN = IPROTO_HEADER_LEN + 7,
};

struct iostream;
struct region;

struct xrow_header {
	/* (!) Please update txn_add_redo() after changing members */

	/** Request type. */
	uint32_t type;
	uint32_t replica_id;
	/**
	 * Replication group identifier. 0 - replicaset,
	 * 1 - replica-local.
         */
	uint32_t group_id;
	uint64_t sync;
	/** Log sequence number.
	 * LSN must be signed for correct comparison
	 */
	int64_t lsn;
	/** Timestamp. Used only when writing to the write ahead
	 * log.
	 */
	double tm;
	/*
	 * Transaction identifier. LSN of the first row in the
	 * transaction.
	 */
	int64_t tsn;
	/**
	 * Stream id. Used in iproto binary protocol to identify stream.
	 * Zero if stream is not used.
	 */
	uint64_t stream_id;
	/** Transaction meta flags set only in the last transaction row. */
	union {
		uint8_t flags;
		struct {
			/**
			 * Is only encoded in the write ahead log for
			 * multi-statement transactions. Single-statement
			 * transactions do not encode tsn and is_commit flag to
			 * save space.
			 */
			bool is_commit : 1;
			/**
			 * True for any transaction that would enter the limbo
			 * (not necessarily a synchronous one).
			 */
			bool wait_sync : 1;
			/**
			 * True for a synchronous transaction.
			 */
			bool wait_ack  : 1;
		};
	};

	int bodycnt;
	/* See `IPROTO_SCHEMA_VERSION`. */
	uint64_t schema_version;
	struct iovec body[XROW_BODY_IOVMAX];
	/**
	 * A pointer to the beginning of xrow header, is set on decoding.
	 * Is NULL by default.
	 */
	const char *header;
	/**
	 * A pointer to the end of xrow header, is set on decoding.
	 * Is NULL by default.
	 */
	const char *header_end;
};

/**
 * Return the max size which the given row is going to take when
 * encoded into a binary packet.
 */
static inline size_t
xrow_approx_len(const struct xrow_header *row)
{
	size_t len = XROW_HEADER_LEN_MAX;
	for (int i = 0; i < row->bodycnt; i++)
		len += row->body[i].iov_len;
	return len;
}

/**
 * Encode xrow into a binary packet
 *
 * @param header xrow
 * @param sync request sync number
 * @param fixheader_len number of bytes to reserve for fixheader
 * @param[out] out iovec to store encoded packet
 * @param[out] iovcnt size of the out iovec array
 *
 * @pre out iovec must have space at least for XROW_IOVMAX members
 * @post iovcnt <= XROW_IOVMAX
 */
void
xrow_encode(const struct xrow_header *header, uint64_t sync,
	    size_t fixheader_len, struct iovec *out, int *iovcnt);

/**
 * Encode xrow header into a given buffer.
 *
 * @param header xrow.
 * @param sync request sync number.
 * @param data buffer. When set to NULL, simply count the needed data size.
 *
 * @return the exact space taken for encoding.
 */
size_t
xrow_header_encode(const struct xrow_header *header, uint64_t sync, char *data);

/**
 * Decode xrow from a binary packet
 *
 * @param header[out] xrow to fill
 * @param pos[inout] the start of a packet
 * @param end the end of a packet
 * @param end_is_exact if set, raise an error in case the packet
 *                     ends before @end
 * @retval 0 on success
 * @retval -1 on error (check diag)
 * @post *pos <= end on success
 * @post *pos == end on success if @end_is_exact is set
 */
int
xrow_decode(struct xrow_header *header, const char **pos,
	    const char *end, bool end_is_exact);

/**
 * DML request.
 */
struct request {
	/*
	 * Either log row, or network header, or NULL, depending
	 * on where this packet originated from: the write ahead
	 * log/snapshot, repliation, or a client request.
	 */
	struct xrow_header *header;
	/**
	 * Request type - IPROTO type code
	 */
	uint16_t type;
	uint32_t space_id;
	uint32_t index_id;
	uint32_t offset;
	uint32_t limit;
	uint32_t iterator;
	/** Search key. */
	const char *key;
	const char *key_end;
	/** Insert/replace/upsert tuple or proc argument or update operations. */
	const char *tuple;
	const char *tuple_end;
	/** The data in in-memory Arrow format. */
	struct ArrowArray *arrow_array;
	/** Arrow schema for @arrow_array. */
	struct ArrowSchema *arrow_schema;
	/** The data in serialized Arrow format. */
	const char *arrow_ipc;
	/** End of @arrow_ipc. */
	const char *arrow_ipc_end;
	/** Upsert operations. */
	const char *ops;
	const char *ops_end;
	/** Tuple metadata. */
	const char *tuple_meta;
	const char *tuple_meta_end;
	/** Old tuple - tuple before request is applied. */
	const char *old_tuple;
	/** End of @old_tuple. */
	const char *old_tuple_end;
	/** Tuple which remains after request is applied (i.e. result). */
	const char *new_tuple;
	/** End of @new_tuple. */
	const char *new_tuple_end;
	/** Packed iterator_position with MP_STR header. */
	const char *after_position;
	/** End of @after_position. */
	const char *after_position_end;
	/** Last selected tuple to start iteration after it. */
	const char *after_tuple;
	/** End of @after_tuple. */
	const char *after_tuple_end;
	/** Base field offset for UPDATE/UPSERT, e.g. 0 for C and 1 for Lua. */
	int index_base;
	/** Send position of last selected tuple in response if true. */
	bool fetch_position;
	/** Name of requested space, points to the request's input buffer. */
	const char *space_name;
	/** Length of @space_name. */
	uint32_t space_name_len;
	/** Name of requested index, points to the request's input buffer. */
	const char *index_name;
	/** Length of @index_name. */
	uint32_t index_name_len;
};

/**
 * Create a JSON-like string representation of a request. */
const char *
request_str(const struct request *request);

/**
 * Decode DML request from a given MessagePack map.
 * @param row request header.
 * @param[out] request DML request to decode to.
 * @param key_map a bit map of keys that are required by the caller,
 *        @sa request_key_map().
 * @param accept_space_name space name is accepted instead of space identifier.
 * @retval 0 on success
 * @retval -1 on error
 */
int
xrow_decode_dml_internal(struct xrow_header *xrow, struct request *request,
			 uint64_t key_map, bool accept_space_name);

/**
 * Decode DML from system request (recovery or replication).
 */
static inline int
xrow_decode_dml(struct xrow_header *xrow, struct request *request,
		uint64_t key_map)
{
	return xrow_decode_dml_internal(xrow, request, key_map, false);
}

/**
 * Decode DML from IPROTO request.
 */
static inline int
xrow_decode_dml_iproto(struct xrow_header *xrow, struct request *request,
		       uint64_t key_map)
{
	return xrow_decode_dml_internal(xrow, request, key_map, true);
}

/**
 * Encode the request fields to iovec using region_alloc().
 * @param request request to encode
 * @param region region to encode
 * @param[out] iov iovec to fill
 * @param[out] iovcnt size of the out iovec array
 */
void
xrow_encode_dml(const struct request *request, struct region *region,
		struct iovec *iov, int *iovcnt);

/**
 * IPROTO_ID request/response.
 */
struct id_request {
	/** IPROTO protocol version. */
	uint64_t version;
	/** IPROTO protocol features. */
	struct iproto_features features;
	/**
	 * Name of the authentication type that is currently used on
	 * the server (value of box.cfg.auth_type). Not null-terminated.
	 * May be NULL. Set only in response.
	 */
	const char *auth_type;
	/** Length of auth_type. */
	uint32_t auth_type_len;
};

/**
 * Decode IPROTO_ID request from a given MessagePack map.
 * @param row request header.
 * @param[out] request IPROTO_ID request to decode to.
 * @retval 0 on success
 * @retval -1 on error
 */
int
xrow_decode_id(const struct xrow_header *xrow, struct id_request *request);

/**
 * Encode IPROTO_ID request on the fiber region.
 * @param[out] row request header.
 */
void
xrow_encode_id(struct xrow_header *row);

/**
 * Synchronous replication request - confirmation or rollback of
 * pending synchronous transactions.
 */
struct synchro_request {
	/**
	 * Operation type - either IPROTO_RAFT_ROLLBACK or IPROTO_RAFT_CONFIRM
	 * or IPROTO_RAFT_PROMOTE
	 */
	uint16_t type;
	/**
	 * ID of the instance owning the pending transactions.
	 * Note, it may be not the same instance, who created this
	 * request. An instance can make an operation on foreign
	 * synchronous transactions in case a new master tries to
	 * finish transactions of an old master.
	 */
	uint32_t replica_id;
	/**
	 * Id of the instance which has issued this request. Only filled on
	 * decoding, and left blank when encoding a request.
	 */
	uint32_t origin_id;
	/**
	 * Operation LSN.
	 * In case of CONFIRM it means 'confirm all
	 * transactions with lsn <= this value'.
	 * In case of ROLLBACK it means 'rollback all transactions
	 * with lsn >= this value'.
	 * In case of PROMOTE it means CONFIRM(lsn) + ROLLBACK(lsn+1)
	 */
	int64_t lsn;
	/**
	 * The new term the instance issuing this request is in. Only used for
	 * PROMOTE and DEMOTE requests.
	 */
	uint64_t term;
	/**
	 * Confirmed lsns of all the previous limbo owners. Only used for
	 * PROMOTE and DEMOTE requests.
	 */
	struct vclock *confirmed_vclock;
};

/**
 * Encode synchronous replication request.
 * @param row xrow header.
 * @param body Desination to use to encode the confirmation body.
 * @param req Request parameters.
 */
void
xrow_encode_synchro(struct xrow_header *row, char *body,
		    const struct synchro_request *req);

/**
 * Decode synchronous replication request.
 * @param row xrow header.
 * @param[out] req Request parameters.
 * @param[out] vclock Storage for request vclock.
 * @retval -1 on error.
 * @retval 0 success.
 */
int
xrow_decode_synchro(const struct xrow_header *row, struct synchro_request *req,
		    struct vclock *vclock);

/**
 * Raft request. It repeats Raft message to the letter, but can be extended in
 * future not depending on the Raft library.
 */
struct raft_request {
	uint64_t term;
	uint32_t vote;
	uint32_t leader_id;
	bool is_leader_seen;
	uint64_t state;
	const struct vclock *vclock;
};

void
xrow_encode_raft(struct xrow_header *row, struct region *region,
		 const struct raft_request *r);

int
xrow_decode_raft(const struct xrow_header *row, struct raft_request *r,
		 struct vclock *vclock);

/**
 * CALL/EVAL request.
 */
struct call_request {
	/** Function name for CALL request. MessagePack String. */
	const char *name;
	/** Expression for EVAL request. MessagePack String. */
	const char *expr;
	/** CALL/EVAL parameters. MessagePack Array. */
	const char *args;
	const char *args_end;
	/** Tuple formats of CALL/EVAL parameters. MessagePack Map. */
	const char *tuple_formats;
	/** End of tuple formats of CALL/EVAL parameters. */
	const char *tuple_formats_end;
};

/**
 * Decode CALL/EVAL request from a given MessagePack map.
 * @param[out] call_request Request to decode to.
 * @param type Request type - either CALL or CALL_16 or EVAL.
 * @param sync Request sync.
 * @param data Request MessagePack encoded body.
 * @param len @data length.
 */
int
xrow_decode_call(const struct xrow_header *row, struct call_request *request);

/**
 * WATCH/UNWATCH/NOTIFY request.
 */
struct watch_request {
	/** Notification key name. String, not null-terminated. */
	const char *key;
	/** Length of the notification key name string. */
	uint32_t key_len;
	/** Notification data. Any MessagePack. */
	const char *data;
	/** End of the data. */
	const char *data_end;
};

/**
 * Decode WATCH/UNWATCH request from MessagePack.
 * @param row Request header.
 * @param[out] request Request to decode to.
 * @retval  0 on success
 * @retval -1 on error
 */
int
xrow_decode_watch(const struct xrow_header *row, struct watch_request *request);

/**
 * Encode a WATCH/UNWATCH request.
 * @param[out] row Row to encode to.
 * @param key The key to start/stop watching for.
 * @param type Request type (WATCH or UNWATCH).
 */
void
xrow_encode_watch_key(struct xrow_header *row, const char *key, uint16_t type);

/**
 * AUTH request
 */
struct auth_request {
	/** MessagePack encoded name of the user to authenticate. */
	const char *user_name;
	/** Auth scramble. @sa scramble.h */
	const char *scramble;
};

/**
 * Decode AUTH request from MessagePack.
 * @param row request header.
 * @param[out] request Request to decode.
 * @retval  0 on success
 * @retval -1 on error
 */
int
xrow_decode_auth(const struct xrow_header *row, struct auth_request *request);

/**
 * Encode AUTH command.
 * @param[out] Row.
 * @param login User login.
 * @param login_len Length of @login.
 * @param method Authentication method.
 * @param method_len Length of @method.
 * @param data Authentication request data.
 * @param data_end End of @data.
 */
void
xrow_encode_auth(struct xrow_header *row,
		 const char *login, size_t login_len,
		 const char *method, size_t method_len,
		 const char *data, const char *data_end);

/** Reply to IPROTO_VOTE request. */
struct ballot {
	/** Set if the instance is configured in read-only mode. */
	bool is_ro_cfg;
	/** Set if the instance can become a Raft leader. */
	bool can_lead;
	/**
	 * A flag whether the instance is anonymous, not having an
	 * ID, and not going to request it.
	 */
	bool is_anon;
	/**
	 * Set if the instance is not writable due to any reason. Could be
	 * config read_only=true; being orphan; being a Raft follower; not
	 * finished recovery/bootstrap; or anything else.
	 */
	bool is_ro;
	/** Set if the instance has finished its bootstrap/recovery. */
	bool is_booted;
	/** Current instance vclock. */
	struct vclock vclock;
	/** Oldest vclock available on the instance. */
	struct vclock gc_vclock;
	/** The uuid of the instance this node considers a bootstrap leader. */
	struct tt_uuid bootstrap_leader_uuid;
	/** The name of this node. */
	char instance_name[NODE_NAME_SIZE_MAX];
	/** Replica uuids registered in the replica set. */
	struct tt_uuid registered_replica_uuids[VCLOCK_MAX];
	/** Number of replicas registered in the replica set. */
	int registered_replica_uuids_size;
};

/**
 * Calculate the size taken by an encoded ballot.
 * @param ballot The ballot to estimate the encoded size of.
 * @retval An upper bound on encoded ballot size.
 */
size_t
mp_sizeof_ballot_max(const struct ballot *ballot);

/**
 * Encode a ballot to the provided buffer.
 * @param data Buffer to encode to.
 * @param ballot Ballot to encode.
 * @retval A pointer after the end of encoded data.
 */
char *
mp_encode_ballot(char *data, const struct ballot *ballot);

/**
 * Decode ballot response to IPROTO_VOTE from MessagePack.
 * @param row Row to decode.
 * @param[out] ballot Where to store the decoded ballot.
 */
int
xrow_decode_ballot(const struct xrow_header *row, struct ballot *ballot);

/**
 * Decode ballot as received in response to an IPROTO_WATCH request.
 * @param req a decoded notification.
 * @param[out] ballot Where to store the decoded ballot.
 * @param[out] is_empty Whether the ballot is empty.
 */
int
xrow_decode_ballot_event(const struct watch_request *req,
			 struct ballot *ballot, bool *is_empty);

/**
 * Encode an instance vote request.
 * @param row[out] Row to encode into.
 */
void
xrow_encode_vote(struct xrow_header *row);

/** New replica REGISTER request. */
struct register_request {
	/** Replica's UUID. */
	struct tt_uuid instance_uuid;
	/** Replica's name. */
	char instance_name[NODE_NAME_SIZE_MAX];
	/** Replica's vclock. */
	struct vclock vclock;
};

/** Encode REGISTER request. */
void
xrow_encode_register(struct xrow_header *row,
		     const struct register_request *req);

/** Decode REGISTER request. */
int
xrow_decode_register(const struct xrow_header *row,
		     struct register_request *req);

/** SUBSCRIBE request from an already registered replica. */
struct subscribe_request {
	/** Replica's replicaset UUID. */
	struct tt_uuid replicaset_uuid;
	/** Replica's replicaset name. */
	char replicaset_name[NODE_NAME_SIZE_MAX];
	/** Replica's instance UUID. */
	struct tt_uuid instance_uuid;
	/** Replica's name. */
	char instance_name[NODE_NAME_SIZE_MAX];
	/** Replica's vclock. */
	struct vclock vclock;
	/** Mask to filter out replica IDs whose rows don't need to be sent. */
	uint32_t id_filter;
	/** Replica's version. */
	uint32_t version_id;
	/** Flag whether the replica is anon. */
	bool is_anon;
};

/** Encode SUBSCRIBE request. */
void
xrow_encode_subscribe(struct xrow_header *row,
		      const struct subscribe_request *req);

/** Decode SUBSCRIBE request. */
int
xrow_decode_subscribe(const struct xrow_header *row,
		      struct subscribe_request *req);

/** SUBSCRIBE response sent by master. */
struct subscribe_response {
	/** Master's replicaset UUID. */
	struct tt_uuid replicaset_uuid;
	/** Master's replicaset name. */
	char replicaset_name[NODE_NAME_SIZE_MAX];
	/** Master's vclock. */
	struct vclock vclock;
};

/** Encode SUBSCRIBE response. */
void
xrow_encode_subscribe_response(struct xrow_header *row,
			       const struct subscribe_response *rsp);

/** Decode SUBSCRIBE response. */
int
xrow_decode_subscribe_response(const struct xrow_header *row,
			       struct subscribe_response *rsp);

/** Request from a replica to JOIN the cluster. */
struct join_request {
	/** Replica's instance UUID. */
	struct tt_uuid instance_uuid;
	/** Replica's name. */
	char instance_name[NODE_NAME_SIZE_MAX];
	/** Replica's version. */
	uint32_t version_id;
};

/** Encode JOIN request. */
void
xrow_encode_join(struct xrow_header *row, const struct join_request *req);

/** Decode JOIN request. */
int
xrow_decode_join(const struct xrow_header *row, struct join_request *req);

struct fetch_snapshot_request {
	/** Replica's version. */
	uint32_t version_id;
	/** Flag indicating whether checkpoint join should be done. */
	bool is_checkpoint_join;
	/** Checkpoint's vclock, signature of the snapshot. */
	struct vclock checkpoint_vclock;
	/** Checkpoint's lsn, the row number to start from. */
	uint64_t checkpoint_lsn;
	/** Replica's UUID. */
	struct tt_uuid instance_uuid;
};

/** Encode FETCH_SNAPSHOT request. */
void
xrow_encode_fetch_snapshot(struct xrow_header *row,
			   const struct fetch_snapshot_request *req);

/** Decode FETCH_SNAPSHOT request. */
int
xrow_decode_fetch_snapshot(const struct xrow_header *row,
			   struct fetch_snapshot_request *req);

/**
 * Heartbeat from relay to applier. Follows the replication stream. Same
 * direction.
 */
struct relay_heartbeat {
	/**
	 * Relay's vclock sync. When applier receives the value, it should start
	 * sending it in all own heartbeats. Thus confirming its receipt.
	 */
	uint64_t vclock_sync;
};

/** Encode relay heartbeat. */
void
xrow_encode_relay_heartbeat(struct xrow_header *row,
			    const struct relay_heartbeat *req);

/** Decode relay heartbeat. */
int
xrow_decode_relay_heartbeat(const struct xrow_header *row,
			    struct relay_heartbeat *req);

/**
 * Heartbeat from applier to relay. Goes against the replication stream
 * direction.
 */
struct applier_heartbeat {
	/** Replica's vclock. */
	struct vclock vclock;
	/** Last vclock sync received from relay. */
	uint64_t vclock_sync;
	/** Replica's last known raft term. */
	uint64_t term;
};

/** Encode applier heartbeat. */
void
xrow_encode_applier_heartbeat(struct xrow_header *row,
			      const struct applier_heartbeat *req);

/** Decode applier heartbeat. */
int
xrow_decode_applier_heartbeat(const struct xrow_header *row,
			      struct applier_heartbeat *req);

/** Encode vclock including 0th component. */
/** Return the number of bytes an encoded vclock takes. */
uint32_t
mp_sizeof_vclock_ignore0(const struct vclock *vclock);

/** Encode a vclock to a buffer as MP_MAP. Never fails. */
char *
mp_encode_vclock_ignore0(char *data, const struct vclock *vclock);

/**
 * Decode a vclock from MsgPack data, it should be MP_MAP.
 * Returns -1 on error, diag is NOT set.
 */
int
mp_decode_vclock_ignore0(const char **data, struct vclock *vclock);

/** Encode vclock ignoring 0th component. */
void
xrow_encode_vclock_ignore0(struct xrow_header *row,
			   const struct vclock *vclock);

/** Encode vclock. */
void
xrow_encode_vclock(struct xrow_header *row, const struct vclock *vclock);

/** Decode vclock ignoring 0th component. */
int
xrow_decode_vclock_ignore0(const struct xrow_header *row,
			   struct vclock *vclock);

/**
 * Encode any bodyless message.
 * @param row[out] Row to encode into.
 * @param type Message type.
 */
void
xrow_encode_type(struct xrow_header *row, uint16_t type);

/**
 * Fast encode xrow header using the specified header fields.
 * It is faster than the xrow_encode, because uses
 * the predefined values for all fields of the header, defined
 * in the struct iproto_header_bin in iproto_port.cc. Because of
 * it, the implementation is placed in the same
 * file: iproto_port.cc.
 *
 * @param out Previously allocated memory of at least
 *        IPROTO_HEADER_LEN bytes.
 * @param type IPROTO_OK or iproto error code.
 * @param sync Sync of the response. Must be the same as the
 *        request sync.
 * @param schema_version Schema version.
 * @param body_length Length of the body of the iproto message.
 *        Please, pass it without IPROTO_HEADER_LEN.
 * @see xrow_encode()
 */
void
iproto_header_encode(char *data, uint16_t type, uint64_t sync,
		     uint64_t schema_version, uint32_t body_length);

struct obuf;
struct obuf_svp;

/**
 * Reserve obuf space for the header, which depends on the
 * response size.
 */
void
iproto_prepare_header(struct obuf *buf, struct obuf_svp *svp, size_t size);

/**
 * Prepare the iproto header for a select result set.
 * @param buf Out buffer.
 * @param svp Savepoint of the header beginning.
 */
static inline void
iproto_prepare_select(struct obuf *buf, struct obuf_svp *svp)
{
	iproto_prepare_header(buf, svp, IPROTO_SELECT_HEADER_LEN);
}

/**
 * Prepare the iproto header for a select result set and iterator position.
 * It is just an alias fot iproto_prepare_select, it is needed for better
 * code readability.
 * @param buf Out buffer.
 * @param svp Savepoint of the header beginning.
 */
static inline void
iproto_prepare_select_with_position(struct obuf *buf, struct obuf_svp *svp)
{
	iproto_prepare_select(buf, svp);
}

/**
 * Write select header to a preallocated buffer.
 */
void
iproto_reply_select(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		    uint64_t schema_version, uint32_t count,
		    bool box_tuple_as_ext);

/**
 * Write extended select header to a preallocated buffer.
 */
void
iproto_reply_select_with_position(struct obuf *buf, struct obuf_svp *svp,
				  uint64_t sync, uint32_t schema_version,
				  uint32_t count, const char *packed_pos,
				  const char *packed_pos_end,
				  bool box_tuple_as_ext);

/**
 * Encode iproto header with IPROTO_OK response code.
 * @param out Encode to.
 * @param sync Request sync.
 * @param schema_version.
 */
void
iproto_reply_ok(struct obuf *out, uint64_t sync, uint64_t schema_version);

/**
 * Encode iproto header with IPROTO_OK response code and protocol features
 * in the body.
 * @param out Encode to.
 * @param auth_type Authentication type.
 * @param sync Request sync.
 * @param schema_version.
 */
void
iproto_reply_id(struct obuf *out, const char *auth_type,
		uint64_t sync, uint64_t schema_version);

/**
 * Encode iproto header with IPROTO_OK response code and vclock
 * in the body.
 * @param out Encode to.
 * @param vclock Vclock to encode.
 * @param sync Request sync.
 * @param schema_version.
 */
void
iproto_reply_vclock(struct obuf *out, const struct vclock *vclock,
		    uint64_t sync, uint64_t schema_version);

/**
 * Encode a reply to an IPROTO_VOTE request.
 * @param out Buffer to write to.
 * @param ballot Ballot to encode.
 * @param sync Request sync.
 * @param schema_version Actual schema version.
 */
void
iproto_reply_vote(struct obuf *out, const struct ballot *ballot,
		  uint64_t sync, uint64_t schema_version);

/** Write an error packet int output buffer. */
void
iproto_reply_error(struct obuf *out, const struct error *e, uint64_t sync,
		   uint64_t schema_version);

/** EXECUTE/PREPARE request. */
struct sql_request {
	/** True for EXECUTE, false for PREPARE. */
	bool execute;
	/** SQL statement text. */
	const char *sql_text;
	/** MessagePack array of parameters. */
	const char *bind;
	/** ID of prepared statement. In this case @sql_text == NULL. */
	const char *stmt_id;
};

/**
 * Parse the EXECUTE request.
 * @param row Encoded data.
 * @param[out] request Request to decode to.
 *
 * @retval  0 Success.
 * @retval -1 Format or memory error.
 */
int
xrow_decode_sql(const struct xrow_header *row, struct sql_request *request);

/**
 * Write the SQL header.
 * @param buf Out buffer.
 * @param svp Savepoint of the header beginning.
 * @param sync Request sync.
 * @param schema_version Schema version.
 */
void
iproto_reply_sql(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		 uint64_t schema_version);

/**
 * Write an IPROTO_CHUNK header from a specified position in a
 * buffer.
 * @param buf Buffer to write to.
 * @param svp Position to write from.
 * @param sync Request sync.
 * @param schema_version Actual schema version.
 */
void
iproto_reply_chunk(struct obuf *buf, struct obuf_svp *svp, uint64_t sync,
		   uint64_t schema_version);

/**
 * Encode IPROTO_EVENT packet.
 * @param out Encode to.
 * @param sync Sync number.
 * @param key Notification key name.
 * @param key_len Length of the notification key name.
 * @param data Notification data (MsgPack).
 * @param data_end End of notification data.
 */
void
iproto_send_event(struct obuf *out, uint64_t sync,
		  const char *key, size_t key_len,
		  const char *data, const char *data_end);

/** Write error directly to a socket. */
void
iproto_do_write_error(struct iostream *io, const struct error *e,
		      uint64_t schema_version, uint64_t sync);

enum {
	/* Maximal length of protocol name in handshake */
	GREETING_PROTOCOL_LEN_MAX = 32,
	/* Maximal length of salt in handshake */
	GREETING_SALT_LEN_MAX = 44,
};

/**
 * The server sends a greeting into a newly established socket,
 * regardless of the socket protocol. This allows the connected
 * client identify the protocol, server version and instance uuid.
 * The greeting also contains a random salt which can be
 * used to encode a password.
 */
struct greeting {
	/** Peer version id. */
	uint32_t version_id;
	uint32_t salt_len;
	/** Peer protocol - Binary or Console */
	char protocol[GREETING_PROTOCOL_LEN_MAX + 1];
	/** Peer instance uuid */
	struct tt_uuid uuid;
	/** Random salt. */
	char salt[GREETING_SALT_LEN_MAX];
};

/**
 * \brief Format a text greeting sent by the instance during handshake.
 * This function encodes greeting for binary protocol (adds "(Binary)"
 * after version signature).
 *
 * \param[out] greetingbuf buffer to store result. Exactly
 * IPROTO_GREETING_SIZE bytes will be written.
 * \param version_id instance version_id created by version_id()
 * \param uuid instance UUID
 * \param salt random bytes that client should use to sign passwords.
 * \param salt_len size of \a salt. Up to GREETING_SALT_LEN_MAX bytes.
 *
 * \sa greeting_decode()
 */
void
greeting_encode(char *greetingbuf, uint32_t version_id,
		const struct tt_uuid *uuid, const char *salt,
		uint32_t salt_len);

/**
 * \brief Parse a text greeting send by the instance during handshake.
 * This function supports both binary and console protocol.
 *
 * \param greetingbuf a text greeting
 * \param[out] greeting parsed struct greeting.
 * \retval 0 on success
 * \retval -1 on failure due to mailformed greeting
 *
 * \sa greeting_encode()
 */
int
greeting_decode(const char *greetingbuf, struct greeting *greeting);

/**
 * Encode an xrow record into the specified iovec.
 *
 * @param row Record to encode.
 * @param[out] out Encoded record.
 * @param[out] iovcnt size of the out iovec array
 */
void
xrow_to_iovec(const struct xrow_header *row, struct iovec *out, int *iovcnt);

/**
 * Decode ERROR and set it to diagnostics area.
 * @param row Encoded error.
 */
void
xrow_decode_error(const struct xrow_header *row);

/**
 * BEGIN request.
 */
struct begin_request {
	/**
	 * Timeout for transaction. If timeout expired, transaction
	 * will be rolled back. Must be greater than zero.
	 */
	double timeout;
	/**
	 * Isolation level of beginning transaction.
	 * Must be ono of enum txn_isolation_level values.
	 */
	uint32_t txn_isolation;
	/**
	 * is_sync that determines the synchronism of transactions.
	 */
	bool is_sync;
};

/**
 * Parse the BEGIN request.
 * @param row Encoded data.
 * @param[out] request Request to decode to.
 *
 * @retval  0 Success.
 * @retval -1 Format error.
 */
int
xrow_decode_begin(const struct xrow_header *row, struct begin_request *request);

/**
 * COMMIT request.
 */
struct commit_request {
	/**
	 * is_sync that determines the synchronism of transactions.
	 */
	bool is_sync;
};

/**
 * Parse the COMMIT request.
 * @param row Encoded data.
 * @param[out] request Request to decode to.
 *
 * @retval  0 Success.
 * @retval -1 Format error.
 */
int
xrow_decode_commit(const struct xrow_header *row,
		   struct commit_request *request);

/**
 * Update vclock with the next LSN value for given replica id.
 * The function will cause panic if the next LSN happens to be
 * out of order. The details of provided row are included into
 * diagnostic message.
 *
 * @param vclock Vector clock.
 * @param row Data row.
 * @return Previous LSN value.
 */
static inline int64_t
vclock_follow_xrow(struct vclock* vclock, const struct xrow_header *row)
{
	assert(row);
	assert(row->replica_id < VCLOCK_MAX);
	if (row->lsn <= vclock_get(vclock, row->replica_id)) {
		struct request req;
		const char *req_str = "n/a";
		if (xrow_decode_dml((struct xrow_header *)row, &req, 0) == 0)
			req_str = request_str(&req);
		/* Never confirm LSN out of order. */
		panic("LSN for %u is used twice or COMMIT order is broken: "
		      "confirmed: %lld, new: %lld, req: %s",
		      (unsigned) row->replica_id,
		      (long long) vclock_get(vclock, row->replica_id),
		      (long long) row->lsn,
		      req_str);
	}
	return vclock_follow(vclock, row->replica_id, row->lsn);
}

#if defined(__cplusplus)
} /* extern "C" */

/** @copydoc xrow_decode. */
static inline void
xrow_decode_xc(struct xrow_header *header, const char **pos,
	       const char *end, bool end_is_exact)
{
	if (xrow_decode(header, pos, end, end_is_exact) < 0)
		diag_raise();
}

/** @copydoc xrow_decode_error. */
static inline void
xrow_decode_error_xc(const struct xrow_header *row)
{
	xrow_decode_error(row);
	diag_raise();
}

/** @copydoc xrow_decode_dml. */
static inline void
xrow_decode_dml_xc(struct xrow_header *row, struct request *request,
		   uint64_t key_map)
{
	if (xrow_decode_dml(row, request, key_map) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_id. */
static inline void
xrow_decode_id_xc(const struct xrow_header *row, struct id_request *request)
{
	if (xrow_decode_id(row, request) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_call. */
static inline void
xrow_decode_call_xc(const struct xrow_header *row,
		    struct call_request *request)
{
	if (xrow_decode_call(row, request) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_auth. */
static inline void
xrow_decode_auth_xc(const struct xrow_header *row,
		    struct auth_request *request)
{
	if (xrow_decode_auth(row, request) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_watch. */
static inline void
xrow_decode_watch_xc(const struct xrow_header *row,
		     struct watch_request *request)
{
	if (xrow_decode_watch(row, request) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_ballot. */
static inline void
xrow_decode_ballot_xc(const struct xrow_header *row, struct ballot *ballot)
{
	if (xrow_decode_ballot(row, ballot) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_ballot_event. */
static inline void
xrow_decode_ballot_event_xc(const struct watch_request *req,
			    struct ballot *ballot, bool *is_empty)
{
	if (xrow_decode_ballot_event(req, ballot, is_empty) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_subscribe. */
static inline void
xrow_decode_subscribe_xc(const struct xrow_header *row,
			 struct subscribe_request *req)
{
	if (xrow_decode_subscribe(row, req) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_join. */
static inline void
xrow_decode_join_xc(const struct xrow_header *row, struct join_request *req)
{
	if (xrow_decode_join(row, req) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_fetch_snapshot. */
static inline void
xrow_decode_fetch_snapshot_xc(const struct xrow_header *row,
			      struct fetch_snapshot_request *req)
{
	if (xrow_decode_fetch_snapshot(row, req) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_register. */
static inline void
xrow_decode_register_xc(const struct xrow_header *row,
			struct register_request *req)
{
	if (xrow_decode_register(row, req) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_applier_heartbeat. */
static inline void
xrow_decode_applier_heartbeat_xc(const struct xrow_header *row,
				 struct applier_heartbeat *req)
{
	if (xrow_decode_applier_heartbeat(row, req) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_relay_heartbeat. */
static inline void
xrow_decode_relay_heartbeat_xc(const struct xrow_header *row,
			       struct relay_heartbeat *req)
{
	if (xrow_decode_relay_heartbeat(row, req) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_vclock_ignore0. */
static inline void
xrow_decode_vclock_ignore0_xc(const struct xrow_header *row,
			      struct vclock *vclock)
{
	if (xrow_decode_vclock_ignore0(row, vclock) != 0)
		diag_raise();
}

/** @copydoc xrow_decode_subscribe_response. */
static inline void
xrow_decode_subscribe_response_xc(const struct xrow_header *row,
				  struct subscribe_response *rsp)
{
	if (xrow_decode_subscribe_response(row, rsp) != 0)
		diag_raise();
}

#endif

#endif /* TARANTOOL_XROW_H_INCLUDED */
