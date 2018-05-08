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
struct ev_io;
struct auth_request;
struct space;

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

void
box_set_ro(bool ro);

bool
box_is_ro(void);

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
 * Switch this instance from 'orphan' to 'running' state.
 * Called on initial configuration as soon as this instance
 * synchronizes with enough replicas to form a quorum.
 */
void
box_clear_orphan(void);

/** True if snapshot is in progress. */
extern bool box_checkpoint_is_in_progress;
/** Incremented with each next snapshot. */
extern uint32_t snapshot_version;

/**
 * Iterate over all spaces and save them to the
 * snapshot file.
 */
int box_checkpoint(void);

typedef int (*box_backup_cb)(const char *path, void *arg);

/**
 * Start a backup. This function calls @cb for each file that
 * needs to be backed up to recover from the last checkpoint.
 * The caller is supposed to call box_backup_stop() after he's
 * done copying the files.
 */
int
box_backup_start(box_backup_cb cb, void *cb_arg);

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
box_process_auth(struct auth_request *request);

void
box_process_join(struct ev_io *io, struct xrow_header *header);

void
box_process_subscribe(struct ev_io *io, struct xrow_header *header);

/**
 * Check Lua configuration before initialization or
 * in case of a configuration change.
 */
void
box_check_config();

void box_bind(void);
void box_listen(void);
void box_set_replication(void);
void box_set_log_level(void);
void box_set_log_format(void);
void box_set_io_collect_interval(void);
void box_set_snap_io_rate_limit(void);
void box_set_too_long_threshold(void);
void box_set_readahead(void);
void box_set_checkpoint_count(void);
void box_set_memtx_max_tuple_size(void);
void box_set_vinyl_max_tuple_size(void);
void box_set_vinyl_cache(void);
void box_set_vinyl_timeout(void);
void box_set_replication_timeout(void);
void box_set_replication_connect_quorum(void);
void box_set_replication_skip_conflict(void);
void box_set_net_msg_max(void);

extern "C" {
#endif /* defined(__cplusplus) */

typedef struct tuple box_tuple_t;

/* box_select is private and used only by FFI */
API_EXPORT int
box_select(uint32_t space_id, uint32_t index_id,
	   int iterator, uint32_t offset, uint32_t limit,
	   const char *key, const char *key_end,
	   struct port *port);

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
 * \param ops encoded operations in MsgPack Arrat format, e.g.
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
 * \param ops encoded operations in MsgPack Arrat format, e.g.
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

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_H */
