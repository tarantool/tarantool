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
#include "box/box.h"

#include "trivia/config.h"
#include "fiber_pool.h"
#include <say.h>
#include <scoped_guard.h>
#include "iproto.h"
#include "iproto_constants.h"
#include "recovery.h"
#include "wal.h"
#include "relay.h"
#include "applier.h"
#include <rmean.h>
#include "main.h"
#include "tuple.h"
#include "session.h"
#include "func.h"
#include "schema.h"
#include "engine.h"
#include "memtx_engine.h"
#include "memtx_index.h"
#include "sysview_engine.h"
#include "vinyl_engine.h"
#include "space.h"
#include "port.h"
#include "request.h"
#include "txn.h"
#include "user.h"
#include "cfg.h"
#include "iobuf.h"
#include "coio.h"
#include "replication.h" /* replica */
#include "title.h"
#include "lua/call.h" /* box_lua_call */
#include "iproto_port.h"
#include "xrow.h"
#include "xrow_io.h"
#include "xstream.h"
#include "authentication.h"
#include "path_lock.h"
#include "gc.h"
#include "systemd.h"

static char status[64] = "unknown";

static void title(const char *new_status)
{
	snprintf(status, sizeof(status), "%s", new_status);
	title_set_status(new_status);
	title_update();
	systemd_snotify("STATUS=%s", status);
}

bool box_snapshot_is_in_progress = false;
bool box_backup_is_in_progress = false;

/*
 * vclock of the checkpoint that is currently being backed up.
 */
static struct vclock box_backup_vclock;

/**
 * The instance is in read-write mode: the local checkpoint
 * and all write ahead logs are processed. For a replica,
 * it also means we've successfully connected to the master
 * and began receiving updates from it.
 */
static bool is_box_configured = false;
static bool is_ro = true;

/**
 * box.cfg{} will fail if one or more replicas can't be reached
 * within the given period.
 */
static const int REPLICATION_CFG_TIMEOUT = 10; /* seconds */

/* Use the shared instance of xstream for all appliers */
static struct xstream join_stream;
static struct xstream subscribe_stream;

/**
 * The pool of fibers in the transaction processor thread
 * working on incoming messages from net, wal and other
 * threads.
 */
static struct fiber_pool tx_fiber_pool;
/**
 * A separate endpoint for WAL wakeup messages, to
 * ensure that WAL messages are delivered even
 * if all fibers in tx_fiber_pool are used. Without
 * this endpoint, tx thread could deadlock when there
 * are too many messages in flight (gh-1892).
 */
static struct cbus_endpoint tx_prio_endpoint;

static void
box_check_writable(void)
{
	/* box is only writable if box.cfg.read_only == false and */
	if (is_ro)
		tnt_raise(LoggedError, ER_READONLY);
}

static void
box_check_memtx_min_tuple_size(ssize_t memtx_min_tuple_size)
{

	if (memtx_min_tuple_size < 8 || memtx_min_tuple_size > 1048280)
	tnt_raise(ClientError, ER_CFG, "memtx_min_tuple_size",
		  "specified value is out of bounds");
}

/**
 * Convert a request accessing a secondary key to a primary key undo
 * record, given it found a tuple.
 * Flush iproto header of the request to be reconstructed in txn_add_redo().
 *
 * @param request - request to fix
 * @param space - space corresponding to request
 * @param found_tuple - tuple found by secondary key
 */
static void
request_rebind_to_primary_key(struct request *request, struct space *space,
			      struct tuple *found_tuple)
{
	Index *primary = index_find_xc(space, 0);
	uint32_t key_len;
	char *key = tuple_extract_key(found_tuple,
			&primary->index_def->key_def, &key_len);
	if (key == NULL)
		diag_raise();
	request->key = key;
	request->key_end = key + key_len;
	request->index_id = 0;
	/* Clear the *body* to ensure it's rebuilt at commit. */
	request->header = NULL;
}

static void
process_rw(struct request *request, struct space *space, struct tuple **result)
{
	assert(iproto_type_is_dml(request->type));
	rmean_collect(rmean_box, request->type, 1);
	try {
		struct txn *txn = txn_begin_stmt(space);
		access_check_space(space, PRIV_W);
		struct tuple *tuple;
		switch (request->type) {
		case IPROTO_INSERT:
		case IPROTO_REPLACE:
			tuple = space->handler->executeReplace(txn, space,
							       request);


			break;
		case IPROTO_UPDATE:
			tuple = space->handler->executeUpdate(txn, space,
							      request);
			if (tuple && request->index_id != 0) {
				/*
				 * XXX: this is going to break with
				 * sync replication for cases when
				 * tuple is NULL, since the leader
				 * will be unable to certify such
				 * updates correctly.
				 */
				request_rebind_to_primary_key(request, space,
							      tuple);
			}
			break;
		case IPROTO_DELETE:
			tuple = space->handler->executeDelete(txn, space,
							      request);
			if (tuple && request->index_id != 0) {
				request_rebind_to_primary_key(request, space,
							      tuple);
			}
			break;
		case IPROTO_UPSERT:
			space->handler->executeUpsert(txn, space, request);
			tuple = NULL;
			break;
		default:
			tuple = NULL;
		}
		/*
		 * Pin the tuple locally before the commit,
		 * otherwise it may go away during yield in
		 * when WAL is written in autocommit mode.
		 */
		TupleRefNil ref(tuple);
		txn_commit_stmt(txn, request);
		if (result) {
			if (tuple)
				tuple_bless_xc(tuple);
			*result = tuple;
		}
	} catch (Exception *e) {
		txn_rollback_stmt();
		throw;
	}
}

void
box_set_ro(bool ro)
{
	is_ro = ro;
}

bool
box_is_ro(void)
{
	return is_ro;
}

struct wal_stream {
	struct xstream base;
	/** How many rows have been recovered so far. */
	size_t rows;
	/** Yield once per 'yield' rows. */
	size_t yield;
};

/**
 * A stub used in txn_commit() during local recovery. We "replay"
 * transactions during local recovery, with WAL turned off.
 * Since each transaction attempts to write itself to WAL at
 * commit, we need an implementation which would fake WAL write.
 */
struct recovery_journal {
	struct journal base;
	struct vclock *vclock;
};

/**
 * Use the current row LSN as commit LSN - vinyl needs to see the
 * exact same signature during local recovery to properly mark
 * min/max LSN of created LSM levels.
 */
int64_t
recovery_journal_write(struct journal *base,
		       struct journal_entry * /* entry */)
{
	struct recovery_journal *journal = (struct recovery_journal *) base;
	return vclock_sum(journal->vclock);
}

static inline void
recovery_journal_create(struct recovery_journal *journal, struct vclock *v)
{
	journal_create(&journal->base, recovery_journal_write, NULL);
	journal->vclock = v;
}

static inline void
apply_row(struct xstream *stream, struct xrow_header *row)
{
	assert(row->bodycnt == 1); /* always 1 for read */
	(void) stream;
	struct request *request = xrow_decode_request(row);
	struct space *space = space_cache_find(request->space_id);
	process_rw(request, space, NULL);
}

static void
apply_wal_row(struct xstream *stream, struct xrow_header *row)
{
	apply_row(stream, row);

	struct wal_stream *xstream =
		container_of(stream, struct wal_stream, base);
	/**
	 * Yield once in a while, but not too often,
	 * mostly to allow signal handling to take place.
	 */
	if (++xstream->rows % xstream->yield == 0)
		fiber_sleep(0);
}

static void
wal_stream_create(struct wal_stream *ctx, size_t wal_max_rows)
{
	xstream_create(&ctx->base, apply_wal_row);
	ctx->rows = 0;
	/**
	 * Make the yield logic covered by the functional test
	 * suite, which has a small setting for rows_per_wal.
	 * Each yield can take up to 1ms if there are no events,
	 * so we can't afford many of them during recovery.
	 */
	ctx->yield = (wal_max_rows >> 4)  + 1;
}

static void
apply_initial_join_row(struct xstream *stream, struct xrow_header *row)
{
	(void) stream;
	struct request *request = xrow_decode_request(row);
	struct space *space = space_cache_find(request->space_id);
	/* no access checks here - applier always works with admin privs */
	space->handler->applyInitialJoinRow(space, request);
}

/* {{{ configuration bindings */

static void
box_check_log(const char *log)
{
	char *error_msg;
	if (log == NULL)
		return;
	if (say_check_init_str(log, &error_msg) == -1) {
		auto guard = make_scoped_guard([=]{ free(error_msg); });
		tnt_raise(ClientError, ER_CFG, "log", error_msg);
	}
}

static void
box_check_uri(const char *source, const char *option_name)
{
	if (source == NULL)
		return;
	struct uri uri;

	/* URI format is [host:]service */
	if (uri_parse(&uri, source) || !uri.service) {
		tnt_raise(ClientError, ER_CFG, option_name,
			  "expected host:service or /unix.socket");
	}
}

static void
box_check_replication(void)
{
	int count = cfg_getarr_size("replication");
	for (int i = 0; i < count; i++) {
		const char *source = cfg_getarr_elem("replication", i);
		box_check_uri(source, "replication");
	}
}

static enum wal_mode
box_check_wal_mode(const char *mode_name)
{
	assert(mode_name != NULL); /* checked in Lua */
	int mode = strindex(wal_mode_STRS, mode_name, WAL_MODE_MAX);
	if (mode == WAL_MODE_MAX)
		tnt_raise(ClientError, ER_CFG, "wal_mode", mode_name);
	return (enum wal_mode) mode;
}

static void
box_check_readahead(int readahead)
{
	enum { READAHEAD_MIN = 128, READAHEAD_MAX = 2147483647 };
	if (readahead < (int) READAHEAD_MIN ||
	    readahead > (int) READAHEAD_MAX) {
		tnt_raise(ClientError, ER_CFG, "readahead",
			  "specified value is out of bounds");
	}
}

static int64_t
box_check_wal_max_rows(int64_t wal_max_rows)
{
	/* check rows_per_wal configuration */
	if (wal_max_rows <= 1) {
		tnt_raise(ClientError, ER_CFG, "rows_per_wal",
			  "the value must be greater than one");
	}
	return wal_max_rows;
}

static int64_t
box_check_wal_max_size(int64_t wal_max_size)
{
	/* check wal_max_bytes configuration */
	if (wal_max_size <= 1) {
		tnt_raise(ClientError, ER_CFG, "wal_max_size",
			  "the value must be greater than one");
	}
	return wal_max_size;
}

void
box_check_config()
{
	box_check_log(cfg_gets("log"));
	box_check_uri(cfg_gets("listen"), "listen");
	box_check_replication();
	box_check_readahead(cfg_geti("readahead"));
	box_check_wal_max_rows(cfg_geti64("rows_per_wal"));
	box_check_wal_max_size(cfg_geti64("wal_max_size"));
	box_check_wal_mode(cfg_gets("wal_mode"));
	box_check_memtx_min_tuple_size(cfg_geti64("memtx_min_tuple_size"));
	if (cfg_geti64("vinyl_page_size") > cfg_geti64("vinyl_range_size"))
		tnt_raise(ClientError, ER_CFG, "vinyl_page_size",
			  "can't be greater than vinyl_range_size");
	if (cfg_geti("vinyl_threads") < 2)
		tnt_raise(ClientError, ER_CFG, "vinyl_threads", "must be >= 2");
}

/*
 * Parse box.cfg.replication and create appliers.
 */
static struct applier **
cfg_get_replication(int *p_count)
{

	/* Use static buffer for result */
	static struct applier *appliers[VCLOCK_MAX];

	int count = cfg_getarr_size("replication");
	if (count >= VCLOCK_MAX) {
		tnt_raise(ClientError, ER_CFG, "replication",
				"too many replicas");
	}

	for (int i = 0; i < count; i++) {
		const char *source = cfg_getarr_elem("replication", i);
		struct applier *applier = applier_new(source,
						      &join_stream,
						      &subscribe_stream);
		if (applier == NULL) {
			/* Delete created appliers */
			while (--i >= 0)
				applier_delete(appliers[i]);
			return NULL;
		}
		appliers[i] = applier; /* link to the list */
	}

	*p_count = count;

	return appliers;
}

/*
 * Sync box.cfg.replication with the cluster registry, but
 * don't start appliers.
 */
static void
box_sync_replication(double timeout)
{
	int count = 0;
	struct applier **appliers = cfg_get_replication(&count);
	if (appliers == NULL)
		diag_raise();

	auto guard = make_scoped_guard([=]{
		for (int i = 0; i < count; i++)
			applier_delete(appliers[i]); /* doesn't affect diag */
	});

	applier_connect_all(appliers, count, timeout);
	replicaset_update(appliers, count);

	guard.is_active = false;
}

void
box_set_replication(void)
{
	if (!is_box_configured) {
		/*
		 * Do nothing, we're in local hot standby mode, this instance
		 * will automatically begin following the replica when local
		 * hot standby mode is finished, see box_cfg().
		 */
		return;
	}

	box_check_replication();
	/* Try to connect to all replicas within the timeout period */
	box_sync_replication(REPLICATION_CFG_TIMEOUT);
	replicaset_foreach(replica) {
		if (replica->applier != NULL)
			applier_resume(replica->applier);
	}
}

void
box_bind(void)
{
	const char *uri = cfg_gets("listen");
	box_check_uri(uri, "listen");
	iproto_bind(uri);
}

void
box_listen(void)
{
	iproto_listen();
}

void
box_set_log_level(void)
{
	say_set_log_level(cfg_geti("log_level"));
}

void
box_set_io_collect_interval(void)
{
	ev_set_io_collect_interval(loop(), cfg_getd("io_collect_interval"));
}

void
box_set_snap_io_rate_limit(void)
{
	MemtxEngine *memtx = (MemtxEngine *) engine_find("memtx");
	if (memtx)
		memtx->setSnapIoRateLimit(cfg_getd("snap_io_rate_limit"));
}

void
box_set_too_long_threshold(void)
{
	too_long_threshold = cfg_getd("too_long_threshold");
}

void
box_set_readahead(void)
{
	int readahead = cfg_geti("readahead");
	box_check_readahead(readahead);
	iobuf_set_readahead(readahead);
}

void
box_update_vinyl_options(void)
{
	VinylEngine *vinyl = (VinylEngine *) engine_find("vinyl");
	vinyl->updateOptions();
}

/* }}} configuration bindings */

/**
 * Execute a request against a given space id with
 * a variable-argument tuple described in format.
 *
 * @example: you want to insert 5 into space 1:
 * boxk(IPROTO_INSERT, 1, "[%u]", 5);
 *
 * @example: you want to set field 3 (base 0) of
 * a tuple with key [10, 20] in space 1 to 1000:
 * boxk(IPROTO_UPDATE, 1, "[%u%u][[%s%u%u]]", 10, 20, "=", 3, 1000);
 *
 * @note Since this is for internal use, it has
 * no boundary or misuse checks.
 */
int
boxk(int type, uint32_t space_id, const char *format, ...)
{
	va_list ap;
	struct request *request;
	request = region_alloc_object(&fiber()->gc, struct request);
	if (request == NULL)
		return -1;
	request_create(request, type);
	request->space_id = space_id;
	va_start(ap, format);
	size_t buf_size = mp_vformat(NULL, 0, format, ap);
	char *buf = (char *)region_alloc(&fiber()->gc, buf_size);
	va_end(ap);
	if (buf == NULL)
		return -1;
	va_start(ap, format);
	if (mp_vformat(buf, buf_size, format, ap) != buf_size)
		assert(0);
	va_end(ap);
	const char *data = buf;
	const char *data_end = buf + buf_size;
	switch (type) {
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		request->tuple = data;
		request->tuple_end = data_end;
		break;
	case IPROTO_DELETE:
		request->key = data;
		request->key_end = data_end;
		break;
	case IPROTO_UPDATE:
		request->key = data;
		mp_next(&data);
		request->key_end = data;
		request->tuple = data;
		mp_next(&data);
		request->tuple_end = data;
		request->index_base = 0;
		break;
	default:
		unreachable();
	}
	try {
		struct space *space = space_cache_find(space_id);
		process_rw(request, space, NULL);
	} catch (Exception *e) {
		return -1;
	}
	return 0;
}

int
box_return_tuple(box_function_ctx_t *ctx, box_tuple_t *tuple)
{
	try {
		port_add_tuple(ctx->port, tuple);
		return 0;
	} catch (Exception *e) {
		return -1;
	}
}

/* schema_find_id()-like method using only public API */
uint32_t
box_space_id_by_name(const char *name, uint32_t len)
{
	char buf[1 + 5 + BOX_NAME_MAX + 5];
	if (len > BOX_NAME_MAX)
		return BOX_ID_NIL;

	char *p = buf;
	p = mp_encode_array(p, 1);
	p = mp_encode_str(p, name, len);
	assert(p < buf + sizeof(buf));

	/* NOTE: error and missing key cases are indistinguishable */
	box_tuple_t *tuple;
	if (box_index_get(BOX_VSPACE_ID, 2, buf, p, &tuple) != 0)
		return BOX_ID_NIL;
	if (tuple == NULL)
		return BOX_ID_NIL;
	return box_tuple_field_u32(tuple, 0, BOX_ID_NIL);
}

uint32_t
box_index_id_by_name(uint32_t space_id, const char *name, uint32_t len)
{
	char buf[1 + 5 + BOX_NAME_MAX + 5];
	if (len > BOX_NAME_MAX)
		return BOX_ID_NIL;

	char *p = buf;
	p = mp_encode_array(p, 2);
	p = mp_encode_uint(p, space_id);
	p = mp_encode_str(p, name, len);
	assert(p < buf + sizeof(buf));

	/* NOTE: error and missing key cases are indistinguishable */
	box_tuple_t *tuple;
	if (box_index_get(BOX_VINDEX_ID, 2, buf, p, &tuple) != 0)
		return BOX_ID_NIL;
	if (tuple == NULL)
		return BOX_ID_NIL;
	return box_tuple_field_u32(tuple, 1, BOX_ID_NIL);
}
/** \endcond public */

int
box_process1(struct request *request, box_tuple_t **result)
{
	try {
		/* Allow to write to temporary spaces in read-only mode. */
		struct space *space = space_cache_find(request->space_id);
		if (!space->def.opts.temporary)
			box_check_writable();
		process_rw(request, space, result);
		return 0;
	} catch (Exception *e) {
		return -1;
	}
}

int
box_select(struct port *port, uint32_t space_id, uint32_t index_id,
	   int iterator, uint32_t offset, uint32_t limit,
	   const char *key, const char *key_end)
{
	rmean_collect(rmean_box, IPROTO_SELECT, 1);

	try {
		struct space *space = space_cache_find(space_id);
		access_check_space(space, PRIV_R);
		struct txn *txn = txn_begin_ro_stmt(space);
		space->handler->executeSelect(txn, space, index_id, iterator,
					      offset, limit, key, key_end, port);
		txn_commit_ro_stmt(txn);
		return 0;
	} catch (Exception *e) {
		txn_rollback_stmt();
		/* will be hanled by box.error() in Lua */
		return -1;
	}
}

int
box_insert(uint32_t space_id, const char *tuple, const char *tuple_end,
	   box_tuple_t **result)
{
	mp_tuple_assert(tuple, tuple_end);
	struct request *request;
	request = region_alloc_object_xc(&fiber()->gc, struct request);
	request_create(request, IPROTO_INSERT);
	request->space_id = space_id;
	request->tuple = tuple;
	request->tuple_end = tuple_end;
	return box_process1(request, result);
}

int
box_replace(uint32_t space_id, const char *tuple, const char *tuple_end,
	    box_tuple_t **result)
{
	mp_tuple_assert(tuple, tuple_end);
	struct request *request;
	request = region_alloc_object_xc(&fiber()->gc, struct request);
	request_create(request, IPROTO_REPLACE);
	request->space_id = space_id;
	request->tuple = tuple;
	request->tuple_end = tuple_end;
	return box_process1(request, result);
}

int
box_delete(uint32_t space_id, uint32_t index_id, const char *key,
	   const char *key_end, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	struct request *request;
	request = region_alloc_object_xc(&fiber()->gc, struct request);
	request_create(request, IPROTO_DELETE);
	request->space_id = space_id;
	request->index_id = index_id;
	request->key = key;
	request->key_end = key_end;
	return box_process1(request, result);
}

int
box_update(uint32_t space_id, uint32_t index_id, const char *key,
	   const char *key_end, const char *ops, const char *ops_end,
	   int index_base, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	mp_tuple_assert(ops, ops_end);
	struct request *request;
	request = region_alloc_object_xc(&fiber()->gc, struct request);
	request_create(request, IPROTO_UPDATE);
	request->space_id = space_id;
	request->index_id = index_id;
	request->key = key;
	request->key_end = key_end;
	request->index_base = index_base;
	/** Legacy: in case of update, ops are passed in in request tuple */
	request->tuple = ops;
	request->tuple_end = ops_end;
	return box_process1(request, result);
}

int
box_upsert(uint32_t space_id, uint32_t index_id, const char *tuple,
	   const char *tuple_end, const char *ops, const char *ops_end,
	   int index_base, box_tuple_t **result)
{
	mp_tuple_assert(ops, ops_end);
	mp_tuple_assert(tuple, tuple_end);
	struct request *request;
	request = region_alloc_object_xc(&fiber()->gc, struct request);
	request_create(request, IPROTO_UPSERT);
	request->space_id = space_id;
	request->index_id = index_id;
	request->ops = ops;
	request->ops_end = ops_end;
	request->tuple = tuple;
	request->tuple_end = tuple_end;
	request->index_base = index_base;
	return box_process1(request, result);
}

static void
space_truncate(struct space *space)
{
	if (!space_index(space, 0)) {
		/* empty space without indexes, nothing to truncate */
		return;
	}

	char key_buf[20];
	char *key_buf_end;
	key_buf_end = mp_encode_uint(key_buf, space_id(space));
	assert(key_buf_end <= key_buf + sizeof(key_buf));

	/* BOX_INDEX_ID is id of _index space, we need 0 index of that space */
	struct space *space_index = space_cache_find(BOX_INDEX_ID);
	Index *index = index_find_xc(space_index, 0);
	struct iterator *it = index->allocIterator();
	auto guard_it_free = make_scoped_guard([=]{
		it->free(it);
	});
	index->initIterator(it, ITER_EQ, key_buf, 1);
	int index_count = 0;
	struct tuple *indexes[BOX_INDEX_MAX] = { NULL };
	struct tuple *tuple;

	/* select all indexes of given space */
	auto guard_indexes_unref = make_scoped_guard([=]{
		for (int i = 0; i < index_count; i++)
			tuple_unref(indexes[i]);
	});
	while ((tuple = it->next(it)) != NULL) {
		tuple_ref_xc(tuple);
		indexes[index_count++] = tuple;
	}
	assert(index_count <= BOX_INDEX_MAX);

	/* box_delete() invalidates space pointer */
	uint32_t x_space_id = space_id(space);
	space = NULL;

	/* drop all selected indexes */
	for (int i = index_count - 1; i >= 0; --i) {
		uint32_t index_id = tuple_field_u32_xc(indexes[i], 1);
		key_buf_end = mp_encode_array(key_buf, 2);
		key_buf_end = mp_encode_uint(key_buf_end, x_space_id);
		key_buf_end = mp_encode_uint(key_buf_end, index_id);
		assert(key_buf_end <= key_buf + sizeof(key_buf));
		if (box_delete(BOX_INDEX_ID, 0, key_buf, key_buf_end, NULL))
			diag_raise();
	}

	/* create all indexes again, now they are empty */
	for (int i = 0; i < index_count; i++) {
		int64_t lsn = vclock_sum(&replicaset_vclock);
		/*
		 * The returned tuple is blessed and will be
		 * collected automatically.
		 */
		tuple = index_def_tuple_update_lsn(indexes[i], lsn);
		TupleRefNil ref(tuple);
		uint32_t bsize;
		const char *data = tuple_data_range(tuple, &bsize);
		if (box_insert(BOX_INDEX_ID, data, data + bsize, NULL))
			diag_raise();
	}
}

int
box_truncate(uint32_t space_id)
{
	try {
		struct space *space = space_cache_find(space_id);
		space_truncate(space);
		return 0;
	} catch (Exception *exc) {
		return -1;
	}
}

static inline void
box_register_replica(uint32_t id, const struct tt_uuid *uuid)
{
	if (boxk(IPROTO_INSERT, BOX_CLUSTER_ID, "[%u%s]",
		 (unsigned) id, tt_uuid_str(uuid)) != 0)
		diag_raise();
	assert(replica_by_uuid(uuid) != NULL);
}

/**
 * @brief Called when recovery/replication wants to add a new
 * replica to the replica set.
 * replica_set_id() is called as a commit trigger on _cluster
 * space and actually adds the replica to the replica set.
 * @param instance_uuid
 */
static void
box_on_join(const tt_uuid *instance_uuid)
{
	box_check_writable();
	struct replica *replica = replica_by_uuid(instance_uuid);
	if (replica != NULL)
		return; /* nothing to do - already registered */

	/** Find the largest existing replica id. */
	struct space *space = space_cache_find(BOX_CLUSTER_ID);
	class MemtxIndex *index = index_find_system(space, 0);
	struct iterator *it = index->position();
	index->initIterator(it, ITER_ALL, NULL, 0);
	struct tuple *tuple;
	/** Assign a new replica id. */
	uint32_t replica_id = 1;
	while ((tuple = it->next(it))) {
		if (tuple_field_u32_xc(tuple, 0) != replica_id)
			break;
		replica_id++;
	}
	box_register_replica(replica_id, instance_uuid);
}

static inline struct func *
access_check_func(const char *name, uint32_t name_len)
{
	struct func *func = func_by_name(name, name_len);
	struct credentials *credentials = current_user();
	/*
	 * If the user has universal access, don't bother with checks.
	 * No special check for ADMIN user is necessary
	 * since ADMIN has universal access.
	 */
	if ((credentials->universal_access & PRIV_ALL) == PRIV_ALL)
		return func;
	uint8_t access = PRIV_X & ~credentials->universal_access;
	if (func == NULL || (func->def.uid != credentials->uid &&
	     access & ~func->access[credentials->auth_token].effective)) {
		/* Access violation, report error. */
		char name_buf[BOX_NAME_MAX + 1];
		snprintf(name_buf, sizeof(name_buf), "%.*s", name_len, name);
		struct user *user = user_find_xc(credentials->uid);
		tnt_raise(ClientError, ER_FUNCTION_ACCESS_DENIED,
			  priv_name(access), user->def.name, name_buf);
	}

	return func;
}

int
func_call(struct func *func, struct request *request, struct obuf *out)
{
	assert(func != NULL && func->def.language == FUNC_LANGUAGE_C);
	if (func->func == NULL)
		func_load(func);

	/* Create a call context */
	struct port port;
	port_create(&port);
	box_function_ctx_t ctx = { request, &port };

	/* Clear all previous errors */
	diag_clear(&fiber()->diag);
	assert(!in_txn()); /* transaction is not started */
	/* Call function from the shared library */
	int rc = func->func(&ctx, request->tuple, request->tuple_end);
	if (rc != 0) {
		if (diag_last_error(&fiber()->diag) == NULL) {
			/* Stored procedure forget to set diag  */
			diag_set(ClientError, ER_PROC_C, "unknown error");
		}
		goto error;
	}

	/* Push results to obuf */
	struct obuf_svp svp;
	if (iproto_prepare_select(out, &svp) != 0)
		goto error;

	if (request->type == IPROTO_CALL_16) {
		/* Tarantool < 1.7.1 compatibility */
		for (struct port_entry *entry = port.first;
		     entry != NULL; entry = entry->next) {
			if (tuple_to_obuf(entry->tuple, out) != 0) {
				obuf_rollback_to_svp(out, &svp);
				goto error;
			}
		}
		iproto_reply_select(out, &svp, request->header->sync, port.size);
	} else {
		assert(request->type == IPROTO_CALL);
		char *size_buf = (char *)
			obuf_alloc(out, mp_sizeof_array(port.size));
		if (size_buf == NULL)
			goto error;
		mp_encode_array(size_buf, port.size);
		for (struct port_entry *entry = port.first;
		     entry != NULL; entry = entry->next) {
			if (tuple_to_obuf(entry->tuple, out) != 0) {
				obuf_rollback_to_svp(out, &svp);
				goto error;
			}
		}
		iproto_reply_select(out, &svp, request->header->sync, 1);
	}

	port_destroy(&port);

	return 0;

error:
	port_destroy(&port);
	txn_rollback();
	return -1;
}

void
box_process_call(struct request *request, struct obuf *out)
{
	rmean_collect(rmean_box, IPROTO_CALL, 1);
	/**
	 * Find the function definition and check access.
	 */
	const char *name = request->key;
	uint32_t name_len = mp_decode_strl(&name);
	struct func *func = access_check_func(name, name_len);
	/*
	 * Sic: func == NULL means that perhaps the user has a global
	 * "EXECUTE" privilege, so no specific grant to a function.
	 */

	/**
	 * Change the current user id if the function is
	 * a set-definer-uid one. If the function is not
	 * defined, it's obviously not a setuid one.
	 */
	struct credentials *orig_credentials = NULL;
	if (func && func->def.setuid) {
		orig_credentials = current_user();
		/* Remember and change the current user id. */
		if (func->owner_credentials.auth_token >= BOX_USER_MAX) {
			/*
			 * Fill the cache upon first access, since
			 * when func is created, no user may
			 * be around to fill it (recovery of
			 * system spaces from a snapshot).
			 */
			struct user *owner = user_find_xc(func->def.uid);
			credentials_init(&func->owner_credentials,
					 owner->auth_token,
					 owner->def.uid);
		}
		fiber_set_user(fiber(), &func->owner_credentials);
	}

	int rc;
	if (func && func->def.language == FUNC_LANGUAGE_C) {
		rc = func_call(func, request, out);
	} else {
		rc = box_lua_call(request, out);
	}
	/* Restore the original user */
	if (orig_credentials)
		fiber_set_user(fiber(), orig_credentials);

	if (rc != 0) {
		txn_rollback();
		diag_raise();
	}

	if (in_txn()) {
		/* The procedure forgot to call box.commit() */
		say_warn("a transaction is active at return from '%.*s'",
			name_len, name);
		txn_rollback();
	}
}

void
box_process_eval(struct request *request, struct obuf *out)
{
	rmean_collect(rmean_box, IPROTO_EVAL, 1);
	/* Check permissions */
	access_check_universe(PRIV_X);
	if (box_lua_eval(request, out) != 0) {
		txn_rollback();
		diag_raise();
	}

	if (in_txn()) {
		/* The procedure forgot to call box.commit() */
		const char *expr = request->key;
		uint32_t expr_len = mp_decode_strl(&expr);
		say_warn("a transaction is active at return from EVAL '%.*s'",
			expr_len, expr);
		txn_rollback();
	}
}

void
box_process_auth(struct request *request, struct obuf *out)
{
	rmean_collect(rmean_box, IPROTO_AUTH, 1);
	assert(request->type == IPROTO_AUTH);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	const char *user = request->key;
	uint32_t len = mp_decode_strl(&user);
	authenticate(user, len, request->tuple, request->tuple_end);
	assert(request->header != NULL);
	iproto_reply_ok(out, request->header->sync);
}

void
box_process_join(struct ev_io *io, struct xrow_header *header)
{
	/*
	 * Tarantool 1.7 JOIN protocol diagram (gh-1113)
	 * =============================================
	 *
	 * Replica => Master
	 *
	 * => JOIN { INSTANCE_UUID: replica_uuid }
	 * <= OK { VCLOCK: start_vclock }
	 *    Replica has enough permissions and master is ready for JOIN.
	 *     - start_vclock - vclock of the latest master's checkpoint.
	 *
	 * <= INSERT
	 *    ...
	 *    Initial data: a stream of engine-specifc rows, e.g. snapshot
	 *    rows for memtx or dirty cursor data for Vinyl. Engine can
	 *    use REPLICA_ID, LSN and other fields for internal purposes.
	 *    ...
	 * <= INSERT
	 * <= OK { VCLOCK: stop_vclock } - end of initial JOIN stage.
	 *     - `stop_vclock` - master's vclock when it's done
	 *     done sending rows from the snapshot (i.e. vclock
	 *     for the end of final join).
	 *
	 * <= INSERT/REPLACE/UPDATE/UPSERT/DELETE { REPLICA_ID, LSN }
	 *    ...
	 *    Final data: a stream of WAL rows from `start_vclock` to
	 *    `stop_vclock`, inclusive. REPLICA_ID and LSN fields are
	 *    original values from WAL and master-master replication.
	 *    ...
	 * <= INSERT/REPLACE/UPDATE/UPSERT/DELETE { REPLICA_ID, LSN }
	 * <= OK { VCLOCK: current_vclock } - end of final JOIN stage.
	 *      - `current_vclock` - master's vclock after final stage.
	 *
	 * All packets must have the same SYNC value as initial JOIN request.
	 * Master can send ERROR at any time. Replica doesn't confirm rows
	 * by OKs. Either initial or final stream includes:
	 *  - Cluster UUID in _schema space
	 *  - Registration of master in _cluster space
	 *  - Registration of the new replica in _cluster space
	 */

	assert(header->type == IPROTO_JOIN);

	/* Decode JOIN request */
	struct tt_uuid instance_uuid = uuid_nil;
	xrow_decode_join(header, &instance_uuid);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	/* Forbid connection to itself */
	if (tt_uuid_is_equal(&instance_uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Check permissions */
	access_check_universe(PRIV_R);
	access_check_space(space_cache_find(BOX_CLUSTER_ID), PRIV_W);

	/* Check that we actually can register a new replica */
	box_check_writable();

	/* Forbid replication with disabled WAL */
	if (wal_mode() == WAL_NONE) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Replication",
			  "wal_mode = 'none'");
	}

	/* Remember start vclock. */
	struct vclock start_vclock;
	/*
	 * The only case when the directory index is empty is
	 * when someone has deleted a snapshot and tries to join
	 * as a replica. Our best effort is to not crash in such
	 * case: raise ER_MISSING_SNAPSHOT.
	 */
	if (gc_last_checkpoint(&start_vclock) < 0)
		tnt_raise(ClientError, ER_MISSING_SNAPSHOT);

	/* Respond to JOIN request with start_vclock. */
	struct xrow_header row;
	xrow_encode_vclock(&row, &start_vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	/*
	 * Initial stream: feed replica with dirty data from engines.
	 */
	relay_initial_join(io->fd, header->sync, &start_vclock);
	say_info("initial data sent.");

	/**
	 * Call the server-side hook which stores the replica uuid
	 * in _cluster space after sending the last row but before
	 * sending OK - if the hook fails, the error reaches the
	 * client.
	 */
	box_on_join(&instance_uuid);

	/* Remember master's vclock after the last request */
	struct vclock stop_vclock;
	wal_checkpoint(&stop_vclock, false);

	/* Send end of initial stage data marker */
	xrow_encode_vclock(&row, &stop_vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	/*
	 * Final stage: feed replica with WALs in range
	 * (start_vclock, stop_vclock).
	 */
	relay_final_join(io->fd, header->sync, &start_vclock, &stop_vclock);
	say_info("final data sent.");

	/* Send end of WAL stream marker */
	struct vclock current_vclock;
	wal_checkpoint(&current_vclock, false);
	xrow_encode_vclock(&row, &current_vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);
}

void
box_process_subscribe(struct ev_io *io, struct xrow_header *header)
{
	assert(header->type == IPROTO_SUBSCRIBE);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	struct tt_uuid replicaset_uuid = uuid_nil, replica_uuid = uuid_nil;
	struct vclock replica_clock;
	vclock_create(&replica_clock);
	xrow_decode_subscribe(header, &replicaset_uuid, &replica_uuid,
			      &replica_clock);

	/* Forbid connection to itself */
	if (tt_uuid_is_equal(&replica_uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Check permissions */
	access_check_universe(PRIV_R);

	/**
	 * Check that the given UUID matches the UUID of the
	 * replica set this replica belongs to. Used to handshake
	 * replica connect, and refuse a connection from a replica
	 * which belongs to a different replica set.
	 */
	if (!tt_uuid_is_equal(&replicaset_uuid, &REPLICASET_UUID)) {
		tnt_raise(ClientError, ER_REPLICASET_UUID_MISMATCH,
			  tt_uuid_str(&replicaset_uuid),
			  tt_uuid_str(&REPLICASET_UUID));
	}

	/* Check replica uuid */
	struct replica *replica = replica_by_uuid(&replica_uuid);
	if (replica == NULL || replica->id == REPLICA_ID_NIL) {
		tnt_raise(ClientError, ER_UNKNOWN_REPLICA,
			  tt_uuid_str(&replica_uuid),
			  tt_uuid_str(&REPLICASET_UUID));
	}

	/* Forbid replication with disabled WAL */
	if (wal_mode() == WAL_NONE) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Replication",
			  "wal_mode = 'none'");
	}

	/*
	 * Send a response to SUBSCRIBE request, tell
	 * the replica how many rows we have in stock for it,
	 * and identify ourselves with our own replica id.
	 */
	struct xrow_header row;
	struct vclock current_vclock;
	wal_checkpoint(&current_vclock, true);
	xrow_encode_vclock(&row, &current_vclock);
	/*
	 * Identify the message with the replica id of this
	 * instance, this is the only way for a replica to find
	 * out the id of the instance it has connected to.
	 */
	struct replica *self = replica_by_uuid(&INSTANCE_UUID);
	assert(self != NULL); /* the local registration is read-only */
	row.replica_id = self->id;
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	/*
	 * Process SUBSCRIBE request via replication relay
	 * Send current recovery vector clock as a marker
	 * of the "current" state of the master. When
	 * replica fetches rows up to this position,
	 * it enters read-write mode.
	 *
	 * @todo: this is not implemented, this is imperfect, and
	 * this is buggy in case there is rollback followed by
	 * a stall in updates (in this case replica may hang
	 * indefinitely).
	 */
	relay_subscribe(io->fd, header->sync, replica, &replica_clock);
}

/** Insert a new cluster into _schema */
static void
box_set_replicaset_uuid()
{
	tt_uuid uu;
	/* Generate a new replica set UUID */
	tt_uuid_create(&uu);
	/* Save replica set UUID in _schema */
	if (boxk(IPROTO_REPLACE, BOX_SCHEMA_ID, "[%s%s]", "cluster",
		 tt_uuid_str(&uu)))
		diag_raise();
}

void
box_free(void)
{
	/*
	 * See gh-584 "box_free() is called even if box is not
	 * initialized
	 */
	if (is_box_configured) {
#if 0
		session_free();
		replication_free();
		user_cache_free();
		schema_free();
		tuple_free();
		port_free();
#endif
		gc_free();
		engine_shutdown();
		wal_thread_stop();
	}
}

static void
engine_init()
{
	/*
	 * Sic: order is important here, since
	 * memtx must be the first to participate
	 * in snapshotting (in enigne_foreach order),
	 * so it must be registered first.
	 */
	MemtxEngine *memtx = new MemtxEngine(cfg_gets("memtx_dir"),
					     cfg_geti("force_recovery"),
					     cfg_getd("memtx_memory"),
					     cfg_geti("memtx_min_tuple_size"),
					     cfg_geti("memtx_max_tuple_size"),
					     cfg_getd("slab_alloc_factor"));
	engine_register(memtx);

	SysviewEngine *sysview = new SysviewEngine();
	engine_register(sysview);

	VinylEngine *vinyl = new VinylEngine();
	vinyl->init();
	engine_register(vinyl);
}

/**
 * Initialize the first replica of a new replica set.
 */
static void
bootstrap_master(void)
{
	engine_bootstrap();

	uint32_t replica_id = 1;

	/* Unregister a local replica if it was registered by bootstrap.bin */
	if (boxk(IPROTO_DELETE, BOX_CLUSTER_ID, "[%u]", 1) != 0)
		diag_raise();

	/* Register the first replica in the replica set */
	box_register_replica(replica_id, &INSTANCE_UUID);
	assert(replica_by_uuid(&INSTANCE_UUID)->id == 1);

	/* Register other cluster members */
	replicaset_foreach(replica) {
		if (tt_uuid_is_equal(&replica->uuid, &INSTANCE_UUID))
			continue;
		assert(replica->applier != NULL);
		box_register_replica(++replica_id, &replica->uuid);
		assert(replica->id == replica_id);
	}

	/* Generate UUID of a new replica set */
	box_set_replicaset_uuid();
}

/**
 * Bootstrap from the remote master
 * \pre  master->applier->state == APPLIER_CONNECTED
 * \post master->applier->state == APPLIER_READY
 *
 * @param[out] start_vclock  the vector time of the master
 *                           at the moment of replica bootstrap
 */
static void
bootstrap_from_master(struct replica *master)
{
	struct applier *applier = master->applier;
	assert(applier != NULL);
	applier_resume_to_state(applier, APPLIER_READY, TIMEOUT_INFINITY);
	assert(applier->state == APPLIER_READY);

	say_info("bootstraping replica from %s",
		 sio_strfaddr(&applier->addr, applier->addr_len));

	/*
	 * Send JOIN request to master
	 * See box_process_join().
	 */

	assert(!tt_uuid_is_nil(&INSTANCE_UUID));
	applier_resume_to_state(applier, APPLIER_INITIAL_JOIN, TIMEOUT_INFINITY);

	/*
	 * Process initial data (snapshot or dirty disk data).
	 */
	engine_begin_initial_recovery(NULL);


	applier_resume_to_state(applier, APPLIER_FINAL_JOIN, TIMEOUT_INFINITY);
	/*
	 * Process final data (WALs).
	 */
	engine_begin_final_recovery();
	struct recovery_journal journal;
	recovery_journal_create(&journal, &replicaset_vclock);
	journal_set(&journal.base);

	applier_resume_to_state(applier, APPLIER_JOINED, TIMEOUT_INFINITY);

	/* Clear the pointer to journal before it goes out of scope */
	journal_set(NULL);

	/* Finalize the new replica */
	engine_end_recovery();

	/* Switch applier to initial state */
	applier_resume_to_state(applier, APPLIER_READY, TIMEOUT_INFINITY);
	assert(applier->state == APPLIER_READY);
}

/**
 * Bootstrap a new instance either as the first master in a
 * replica set or as a replica of an existing master.
 *
 * @param[out] start_vclock  the start vector time of the new
 * instance
 */
static void
bootstrap()
{
	/* Use the first replica by URI as a bootstrap leader */
	struct replica *master = replicaset_first();
	assert(master == NULL || master->applier != NULL);

	if (master != NULL && !tt_uuid_is_equal(&master->uuid, &INSTANCE_UUID)) {
		bootstrap_from_master(master);
	} else {
		bootstrap_master();
	}
	if (engine_begin_checkpoint() ||
	    engine_commit_checkpoint(&replicaset_vclock))
		panic("failed to save a snapshot");
}

static void
tx_prio_cb(struct ev_loop *loop, ev_watcher *watcher, int events)
{
	(void) loop;
	(void) events;
	struct cbus_endpoint *endpoint = (struct cbus_endpoint *)watcher->data;
	cbus_process(endpoint);
}

void
box_init(void)
{
	user_cache_init();
	/*
	 * The order is important: to initialize sessions,
	 * we need to access the admin user, which is used
	 * as a default session user when running triggers.
	 */
	session_init();
}

bool
box_is_configured(void)
{
	return is_box_configured;
}

static inline void
box_cfg_xc(void)
{
	tuple_init();

	/* Join the cord interconnect as "tx" endpoint. */
	fiber_pool_create(&tx_fiber_pool, "tx", FIBER_POOL_SIZE,
			  FIBER_POOL_IDLE_TIMEOUT);
	/* Add an extra endpoint for WAL wake up/rollback messages. */
	cbus_endpoint_create(&tx_prio_endpoint, "tx_prio", tx_prio_cb, &tx_prio_endpoint);

	rmean_box = rmean_new(iproto_type_strs, IPROTO_TYPE_STAT_MAX);
	rmean_error = rmean_new(rmean_error_strings, RMEAN_ERROR_LAST);

	if (gc_init(cfg_gets("memtx_dir")) < 0)
		diag_raise();

	engine_init();

	schema_init();

	replication_init();
	port_init();
	iproto_init();
	wal_thread_start();

	title("loading");

	box_set_too_long_threshold();
	xstream_create(&join_stream, apply_initial_join_row);
	xstream_create(&subscribe_stream, apply_row);

	struct vclock checkpoint_vclock;
	vclock_create(&checkpoint_vclock);
	int64_t lsn = gc_last_checkpoint(&checkpoint_vclock);
	/*
	 * Lock the write ahead log directory to avoid multiple
	 * instances running in the same dir.
	 */
	if (path_lock(cfg_gets("wal_dir"), &wal_dir_lock) < 0)
		diag_raise();
	if (wal_dir_lock < 0) {
		/**
		 * The directory is busy and hot standby mode is off:
		 * refuse to start. In hot standby mode, a busy
		 * WAL dir must contain at least one xlog.
		 */
		if (!cfg_geti("hot_standby") || lsn == -1)
			tnt_raise(ClientError, ER_ALREADY_RUNNING, cfg_gets("wal_dir"));
	} else {
		/*
		 * Try to bind the port before recovery, to fail
		 * early if the port is busy. In hot standby mode,
		 * the port is most likely busy.
		 */
		box_bind();
	}
	if (lsn != -1) {
		struct wal_stream wal_stream;
		wal_stream_create(&wal_stream, cfg_geti64("rows_per_wal"));

		struct recovery *recovery;
		recovery = recovery_new(cfg_gets("wal_dir"),
					cfg_geti("force_recovery"),
					&checkpoint_vclock);
		auto guard = make_scoped_guard([=]{ recovery_delete(recovery); });

		/*
		 * recovery->vclock is needed by Vinyl to filter
		 * WAL rows that were dumped before restart.
		 *
		 * XXX: Passing an internal member of the recovery
		 * object to an engine is an ugly hack. Instead we
		 * should introduce Engine::applyWALRow method and
		 * explicitly pass the statement LSN to it.
		 */
		engine_begin_initial_recovery(&recovery->vclock);
		MemtxEngine *memtx = (MemtxEngine *) engine_find("memtx");
		/**
		 * We explicitly request memtx to recover its
		 * snapshot as a separate phase since it contains
		 * data for system spaces, and triggers on
		 * recovery of system spaces issue DDL events in
		 * other engines.
		 */
		memtx->recoverSnapshot(&checkpoint_vclock);

		struct recovery_journal journal;
		recovery_journal_create(&journal, &recovery->vclock);
		journal_set(&journal.base);

		engine_begin_final_recovery();
		title("orphan");
		recovery_follow_local(recovery, &wal_stream.base, "hot_standby",
				      cfg_getd("wal_dir_rescan_delay"));
		title("hot_standby");

		assert(!tt_uuid_is_nil(&INSTANCE_UUID));
		/*
		 * Leave hot standby mode, if any, only
		 * after acquiring the lock.
		 */
		if (wal_dir_lock < 0) {
			say_info("Entering hot standby mode");
			while (true) {
				if (path_lock(cfg_gets("wal_dir"),
					      &wal_dir_lock))
					diag_raise();
				if (wal_dir_lock >= 0)
					break;
				fiber_sleep(0.1);
			}
			box_bind();
		}
		recovery_finalize(recovery, &wal_stream.base);
		engine_end_recovery();

		/* Clear the pointer to journal before it goes out of scope */
		journal_set(NULL);
		/*
		 * Initialize the replica set vclock from recovery.
		 * The local WAL may contain rows from remote masters,
		 * so we must reflect this in replicaset_vclock to
		 * not attempt to apply these rows twice.
		 */
		vclock_copy(&replicaset_vclock, &recovery->vclock);

		/** Begin listening only when the local recovery is complete. */
		box_listen();
		/* Wait for the cluster to start up */
		box_sync_replication(TIMEOUT_INFINITY);
	} else {
		tt_uuid_create(&INSTANCE_UUID);
		/*
		 * Begin listening on the socket to enable
		 * master-master replication leader election.
		 */
		box_listen();

		/* Wait for the  cluster to start up */
		box_sync_replication(TIMEOUT_INFINITY);

		/* Bootstrap a new master */
		bootstrap();
	}
	fiber_gc();

	/* Check for correct registration of the instance in _cluster */
	{
		struct replica *self = replica_by_uuid(&INSTANCE_UUID);
		if (self == NULL || self->id == REPLICA_ID_NIL) {
			tnt_raise(ClientError, ER_UNKNOWN_REPLICA,
				  tt_uuid_str(&INSTANCE_UUID),
				  tt_uuid_str(&REPLICASET_UUID));
		}
	}

	/* Start WAL writer */
	int64_t wal_max_rows = box_check_wal_max_rows(cfg_geti64("rows_per_wal"));
	int64_t wal_max_size = box_check_wal_max_size(cfg_geti64("wal_max_size"));
	enum wal_mode wal_mode = box_check_wal_mode(cfg_gets("wal_mode"));
	wal_init(wal_mode, cfg_gets("wal_dir"), &INSTANCE_UUID,
		 &replicaset_vclock, wal_max_rows, wal_max_size);

	rmean_cleanup(rmean_box);

	/* Follow replica */
	replicaset_foreach(replica) {
		if (replica->applier != NULL)
			applier_resume(replica->applier);
	}

	title("running");
	say_info("ready to accept requests");

	fiber_gc();
	is_box_configured = true;
}

void
box_cfg(void)
{
	try {
		box_cfg_xc();
	} catch (Exception *e) {
		e->log();
		panic("can't initialize storage: %s", e->get_errmsg());
	}
}

/**
 * box.coredump() forks to save a core. The entire
 * server forks in box.cfg{} if background=true.
 */
void
box_atfork()
{
	wal_atfork();
}

int
box_snapshot()
{
	/* Signal arrived before box.cfg{} */
	if (! is_box_configured)
		return 0;
	int rc = 0;
	if (box_snapshot_is_in_progress) {
		diag_set(ClientError, ER_CHECKPOINT_IN_PROGRESS);
		return -1;
	}
	box_snapshot_is_in_progress = true;
	/* create snapshot file */
	latch_lock(&schema_lock);
	if ((rc = engine_begin_checkpoint()))
		goto end;

	struct vclock vclock;
	if ((rc = wal_checkpoint(&vclock, true))) {
		tnt_error(ClientError, ER_CHECKPOINT_ROLLBACK);
		goto end;
	}
	rc = engine_commit_checkpoint(&vclock);
end:
	if (rc)
		engine_abort_checkpoint();
	latch_unlock(&schema_lock);
	box_snapshot_is_in_progress = false;
	return rc;
}

int
box_backup_start(box_backup_cb cb, void *cb_arg)
{
	if (box_backup_is_in_progress) {
		diag_set(ClientError, ER_BACKUP_IN_PROGRESS);
		return -1;
	}
	if (gc_ref_last_checkpoint(&box_backup_vclock) < 0) {
		diag_set(ClientError, ER_MISSING_SNAPSHOT);
		return -1;
	}
	box_backup_is_in_progress = true;
	int rc = engine_backup(&box_backup_vclock, cb, cb_arg);
	if (rc != 0) {
		gc_unref_checkpoint(&box_backup_vclock);
		box_backup_is_in_progress = false;
	}
	return rc;
}

void
box_backup_stop(void)
{
	if (box_backup_is_in_progress) {
		gc_unref_checkpoint(&box_backup_vclock);
		box_backup_is_in_progress = false;
	}
}

const char *
box_status(void)
{
    return status;
}
