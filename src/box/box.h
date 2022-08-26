#ifndef INCLUDES_TARANTOOL_BOX_H
#define INCLUDES_TARANTOOL_BOX_H
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
#include "trivia/util.h"

#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/*
 * Box - data storage (spaces, indexes) and query
 * processor (INSERT, UPDATE, DELETE, SELECT, Lua)
 * subsystem of Tarantool.
 */

struct port;
struct request;
struct xrow_header;
struct obuf;
struct iostream;
struct auth_request;
struct space;
struct vclock;
struct position;

/**
 * Pointer to TX thread local vclock.
 *
 * During recovery it points to the current recovery position.
 * Once recovery is complete, it is set to &replicaset.vclock.
 *
 * We need it for reporting the actual vclock in box.info while
 * the instance is in hot standby mode.
 */
extern const struct vclock *box_vclock;

/** Time to wait for shutdown triggers finished */
extern double on_shutdown_trigger_timeout;

/** Invoked on box shutdown. */
extern struct rlist box_on_shutdown_trigger_list;

/**
 * Timeout during which the transaction must complete,
 * otherwise it will be rolled back.
 */
extern double txn_timeout_default;

/*
 * Initialize box library
 * @throws C++ exception
 */
void
box_init(void);

/**
 * Cleanup box library
 */
void
box_free(void);

/**
 * Load configuration for box library.
 * Panics on error.
 */
void
box_cfg(void);

/**
 * Return true if box has been configured, i.e. box_cfg() was called.
 */
bool
box_is_configured(void);

/**
 * A pthread_atfork() callback for box
 */
void
box_atfork(void);

/** Check if the slice of main cord has expired. */
int
box_check_slice_slow(void);

/** Check periodically if the slice of main cord has expired. */
static inline int
box_check_slice(void)
{
	const uint32_t check_limit = 1000;
	static uint32_t check_count;
	check_count++;
	if (check_count == check_limit) {
		check_count = 0;
		return box_check_slice_slow();
	} else {
		return 0;
	}
}

void
box_set_ro(void);

bool
box_is_ro(void);

bool
box_is_orphan(void);

/**
 * Wait until the instance switches to a desired mode.
 * \param ro wait read-only if set or read-write if unset
 * \param timeout max time to wait
 * \retval -1 timeout or fiber is cancelled
 * \retval 0 success
 */
int
box_wait_ro(bool ro, double timeout);

/**
 * Switch this instance from 'orphan' to 'running' state or
 * vice versa depending on the value of the function argument.
 *
 * An instance enters 'orphan' state on returning from box.cfg()
 * if it failed to synchornize with 'quorum' replicas within a
 * specified timeout. It will keep trying to synchronize in the
 * background and leave 'orphan' state once it's done.
 */
void
box_set_orphan(bool orphan);

/**
 * Set orphan mode but don't update instance title.
 * \sa box_set_orphan
 */
void
box_do_set_orphan(bool orphan);

/**
 * Update the final RO flag based on the instance flags and state.
 */
void
box_update_ro_summary(void);

/**
 * Get the reason why the instance is read only if it is. Can't be called on a
 * writable instance.
 */
const char *
box_ro_reason(void);

/**
 * Iterate over all spaces and save them to the
 * snapshot file.
 */
int box_checkpoint(void);

typedef int (*box_backup_cb)(const char *path, void *arg);

/**
 * Start a backup. This function calls @cb for each file that
 * needs to be backed up to recover from the specified checkpoint.
 *
 * The checkpoint is given by @checkpoint_idx. If @checkpoint_idx
 * is 0, the last checkpoint will be backed up; if it is 1, next
 * to last, and so on.
 *
 * The caller is supposed to call box_backup_stop() after he's
 * done copying the files.
 */
int
box_backup_start(int checkpoint_idx, box_backup_cb cb, void *cb_arg);

/**
 * Finish backup started with box_backup_start().
 */
void
box_backup_stop(void);

/**
 * Spit out some basic module status (master/slave, etc.
 */
const char *box_status(void);

/**
 * Reset box statistics.
 */
void
box_reset_stat(void);

#if defined(__cplusplus)
} /* extern "C" */

void
box_process_auth(struct auth_request *request, const char *salt);

/** Send current read view to the replica. */
void
box_process_fetch_snapshot(struct iostream *io,
			   const struct xrow_header *header);

/** Register a replica */
void
box_process_register(struct iostream *io, const struct xrow_header *header);

/**
 * Join a replica.
 *
 * Register a replica and feed it with data.
 *
 * \param io I/O stream
 * \param JOIN packet header
 */
void
box_process_join(struct iostream *io, const struct xrow_header *header);

/**
 * Subscribe a replica.
 *
 * Perform necessary checks and start a relay thread.
 *
 * \param io I/O stream
 * \param SUBSCRIBE packet header
 */
void
box_process_subscribe(struct iostream *io, const struct xrow_header *header);

void
box_process_vote(struct ballot *ballot);

/**
 * Check Lua configuration before initialization or
 * in case of a configuration change.
 */
void
box_check_config(void);

int box_listen(void);
void box_set_replication(void);
void box_set_io_collect_interval(void);
void box_set_snap_io_rate_limit(void);
void box_set_too_long_threshold(void);
void box_set_readahead(void);
void box_set_checkpoint_count(void);
void box_set_checkpoint_interval(void);
void box_set_checkpoint_wal_threshold(void);
int box_set_wal_queue_max_size(void);
int box_set_wal_cleanup_delay(void);
void box_set_memtx_memory(void);
void box_set_memtx_max_tuple_size(void);
void box_set_vinyl_memory(void);
void box_set_vinyl_max_tuple_size(void);
void box_set_vinyl_cache(void);
void box_set_vinyl_timeout(void);
int box_set_election_mode(void);
int box_set_election_timeout(void);
int box_set_election_fencing_mode(void);
void box_set_replication_timeout(void);
void box_set_replication_connect_timeout(void);
void box_set_replication_connect_quorum(void);
void box_set_replication_sync_lag(void);
void box_update_replication_synchro_quorum(void);
int box_set_replication_synchro_quorum(void);
int box_set_replication_synchro_timeout(void);
void box_set_replication_sync_timeout(void);
void box_set_replication_skip_conflict(void);
void box_set_replication_anon(void);
void box_set_net_msg_max(void);
int box_set_crash(void);
int box_set_txn_timeout(void);
/**
 * Set default isolation level from cfg option txn_isolation.
 * @return 0 on success, -1 on error.
 */
int
box_set_txn_isolation(void);

int
box_set_prepared_stmt_cache_size(void);

extern "C" {
#endif /* defined(__cplusplus) */

typedef struct tuple box_tuple_t;

bool
box_in_promote(void);

int
box_promote(void);

int
box_demote(void);

int
box_promote_qsync(void);

/* box_select is private and used only by FFI */
API_EXPORT int
box_select(uint32_t space_id, uint32_t index_id,
	   int iterator, uint32_t offset, uint32_t limit,
	   const char *key, const char *key_end,
	   struct port *port);

/**
 * Version of box_select that supports pagination. If pos is NULL and
 * update_pos is false, box_select_after has the same behaviour as box_select.
 * Pre-requesites: if update_pos is true, pos must not be NULL.
 * box_select_after is private and used only by FFI.
 */
API_EXPORT int
box_select_after(uint32_t space_id, uint32_t index_id,
		 int iterator, uint32_t offset, uint32_t limit,
		 const char *key, const char *key_end,
		 struct position *pos, bool update_pos, struct port *port);

/** \cond public */

/*
 * Opaque structure passed to the stored C procedure
 */
typedef struct box_function_ctx box_function_ctx_t;

/**
 * Return a tuple from stored C procedure.
 *
 * Returned tuple is automatically reference counted by Tarantool.
 *
 * \param ctx an opaque structure passed to the stored C procedure by
 * Tarantool
 * \param tuple a tuple to return
 * \retval -1 on error (perhaps, out of memory; check box_error_last())
 * \retval 0 otherwise
 */
API_EXPORT int
box_return_tuple(box_function_ctx_t *ctx, box_tuple_t *tuple);

/**
 * Return MessagePack from a stored C procedure. The MessagePack
 * is copied, so it is safe to free/reuse the passed arguments
 * after the call.
 * MessagePack is not validated, for the sake of speed. It is
 * expected to be a single encoded object. An attempt to encode
 * and return multiple objects without wrapping them into an
 * MP_ARRAY or MP_MAP is undefined behaviour.
 *
 * \param ctx An opaque structure passed to the stored C procedure
 *        by Tarantool.
 * \param mp Begin of MessagePack.
 * \param mp_end End of MessagePack.
 * \retval -1 Error.
 * \retval 0 Success.
 */
API_EXPORT int
box_return_mp(box_function_ctx_t *ctx, const char *mp, const char *mp_end);

/**
 * Find space id by name.
 *
 * This function performs SELECT request to _vspace system space.
 * \param name space name
 * \param len length of \a name
 * \retval BOX_ID_NIL on error or if not found (check box_error_last())
 * \retval space_id otherwise
 * \sa box_index_id_by_name
 */
API_EXPORT uint32_t
box_space_id_by_name(const char *name, uint32_t len);

/**
 * Find index id by name.
 *
 * This function performs SELECT request to _vindex system space.
 * \param space_id space identifier
 * \param name index name
 * \param len length of \a name
 * \retval BOX_ID_NIL on error or if not found (check box_error_last())
 * \retval index_id otherwise
 * \sa box_space_id_by_name
 */
API_EXPORT uint32_t
box_index_id_by_name(uint32_t space_id, const char *name, uint32_t len);

/**
 * Execute an INSERT request.
 *
 * \param space_id space identifier
 * \param tuple encoded tuple in MsgPack Array format ([ field1, field2, ...])
 * \param tuple_end end of @a tuple
 * \param[out] result a new tuple. Can be set to NULL to discard result.
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \sa \code box.space[space_id]:insert(tuple) \endcode
 */
API_EXPORT int
box_insert(uint32_t space_id, const char *tuple, const char *tuple_end,
	   box_tuple_t **result);

/**
 * Execute an REPLACE request.
 *
 * \param space_id space identifier
 * \param tuple encoded tuple in MsgPack Array format ([ field1, field2, ...])
 * \param tuple_end end of @a tuple
 * \param[out] result a new tuple. Can be set to NULL to discard result.
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \sa \code box.space[space_id]:replace(tuple) \endcode
 */
API_EXPORT int
box_replace(uint32_t space_id, const char *tuple, const char *tuple_end,
	    box_tuple_t **result);

/**
 * Execute an DELETE request.
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param key encoded key in MsgPack Array format ([part1, part2, ...]).
 * \param key_end the end of encoded \a key.
 * \param[out] result an old tuple. Can be set to NULL to discard result.
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \sa \code box.space[space_id].index[index_id]:delete(key) \endcode
 */
API_EXPORT int
box_delete(uint32_t space_id, uint32_t index_id, const char *key,
	   const char *key_end, box_tuple_t **result);

/**
 * Execute an UPDATE request.
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param key encoded key in MsgPack Array format ([part1, part2, ...]).
 * \param key_end the end of encoded \a key.
 * \param ops encoded operations in MsgPack Array format, e.g.
 * [ [ '=', fieldno,  value ],  ['!', 2, 'xxx'] ]
 * \param ops_end the end of encoded \a ops
 * \param index_base 0 if fieldnos in update operations are zero-based
 * indexed (like C) or 1 if for one-based indexed field ids (like Lua).
 * \param[out] result a new tuple. Can be set to NULL to discard result.
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \sa \code box.space[space_id].index[index_id]:update(key, ops) \endcode
 * \sa box_upsert()
 */
API_EXPORT int
box_update(uint32_t space_id, uint32_t index_id, const char *key,
	   const char *key_end, const char *ops, const char *ops_end,
	   int index_base, box_tuple_t **result);

/**
 * Execute an UPSERT request.
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param ops encoded operations in MsgPack Array format, e.g.
 * [ [ '=', fieldno,  value ],  ['!', 2, 'xxx'] ]
 * \param ops_end the end of encoded \a ops
 * \param tuple encoded tuple in MsgPack Array format ([ field1, field2, ...])
 * \param tuple_end end of @a tuple
 * \param index_base 0 if fieldnos in update operations are zero-based
 * indexed (like C) or 1 if for one-based indexed field ids (like Lua).
 * \param[out] result a new tuple. Can be set to NULL to discard result.
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \sa \code box.space[space_id].index[index_id]:update(key, ops) \endcode
 * \sa box_update()
 */
API_EXPORT int
box_upsert(uint32_t space_id, uint32_t index_id, const char *tuple,
	   const char *tuple_end, const char *ops, const char *ops_end,
	   int index_base, box_tuple_t **result);

/**
 * Truncate space.
 *
 * \param space_id space identifier
 */
API_EXPORT int
box_truncate(uint32_t space_id);

/**
 * Advance a sequence.
 *
 * \param seq_id sequence identifier
 * \param[out] result pointer to a variable where the next sequence
 * value will be stored on success
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 */
API_EXPORT int
box_sequence_next(uint32_t seq_id, int64_t *result);

/**
 * Get the last value returned by a sequence.
 *
 * \param seq_id sequence identifier
 * \param[out] result pointer to a variable where the current sequence
 * value will be stored on success
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 */
API_EXPORT int
box_sequence_current(uint32_t seq_id, int64_t *result);

/**
 * Set a sequence value.
 *
 * \param seq_id sequence identifier
 * \param value new sequence value; on success the next call to
 * box_sequence_next() will return the value following \a value
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 */
API_EXPORT int
box_sequence_set(uint32_t seq_id, int64_t value);

/**
 * Reset a sequence.
 *
 * \param seq_id sequence identifier
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 */
API_EXPORT int
box_sequence_reset(uint32_t seq_id);

/**
 * Push MessagePack data into a session data channel - socket,
 * console or whatever is behind the session. Note, that
 * successful push does not guarantee delivery in case it was sent
 * into the network. Just like with write()/send() system calls.
 *
 * \param data begin of MessagePack to push
 * \param data_end end of MessagePack to push
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 */
API_EXPORT int
box_session_push(const char *data, const char *data_end);

/** \endcond public */

/**
 * Used to be entry point to the
 * Box: callbacks into the request processor.
 * These are function pointers since they can
 * change when entering/leaving read-only mode
 * (master->slave propagation).
 * However, it makes space lookup. If space is already obtained,
 * one can simply use internal box_process_rw().
 */
int
box_process1(struct request *request, box_tuple_t **result);

/**
 * Execute request on given space.
 *
 * \param request Request to be executed
 * \param space Space to be updated
 * \param result Result of executed request
 * \retval 0 in success, -1 otherwise
 */
int
box_process_rw(struct request *request, struct space *space,
	       struct tuple **result);

int
boxk(int type, uint32_t space_id, const char *format, ...);

/**
 * Broadcast the identification of the instance
 */
void
box_broadcast_id(void);

/**
 * Broadcast the current election state of RAFT machinery
 */
void
box_broadcast_election(void);

/**
 * Broadcast the current schema version
 */
void
box_broadcast_schema(void);

/**
 * True if FFI must not be used for calling box read operations from Lua, see
 * box/lua/schema.lua.
 *
 * Lua FFI is faster than Lua C API, but it must not be used if the executed C
 * function may yield or call a Lua function. In particular, we have to disable
 * FFI during online space upgrade, which installs a Lua handler for each read
 * operation to return updated data.
 *
 * See also box_read_ffi_enable(), box_read_ffi_disable().
 */
extern bool box_read_ffi_is_disabled;

/**
 * Disables Lua FFI for box read operations, see box_read_ffi_is_disabled.
 *
 * It's okay to call this function multiple times, because we use a counter
 * under the hood, not a boolean flag. To re-enable Lua FFI, one should call
 * box_read_ffi_enable() the same amount of times.
 */
void
box_read_ffi_disable(void);

/**
 * Re-enables Lua FFI for box read operations, which was disabled with
 * box_read_ffi_disable().
 */
void
box_read_ffi_enable(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_H */
