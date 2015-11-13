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
#include "box/box.h"

#include <say.h>
#include "iproto.h"
#include "iproto_constants.h"
#include "recovery.h"
#include "relay.h"
#include "applier.h"
#include <rmean.h>
#include "main.h"
#include "tuple.h"
#include "lua/call.h"
#include "session.h"
#include "schema.h"
#include "engine.h"
#include "memtx_engine.h"
#include "memtx_index.h"
#include "sysview_engine.h"
#include "sophia_engine.h"
#include "space.h"
#include "port.h"
#include "request.h"
#include "txn.h"
#include "user.h"
#include "cfg.h"
#include "iobuf.h"
#include "coio.h"
#include "cluster.h" /* replica */
#include "title.h"

static char status[64] = "unknown";

static void title(const char *new_status)
{
	strncpy(status, new_status, sizeof status);
	title_set_status(new_status);
	title_update();
}

struct recovery *recovery;

/**
 * The context of initial recovery.
 */
static struct recover_row_ctx {
	/** How many rows have been recovered so far. */
	int rows;
	/** Yield once per 'yield' rows. */
	int yield;
} recover_row_ctx;

bool snapshot_in_progress = false;
static bool box_init_done = false;
bool is_ro = true;

void
recover_row_ctx_init(struct recover_row_ctx *ctx, int rows_per_wal)
{
	ctx->rows = 0;
	/**
	 * Make the yield logic covered by the functional test
	 * suite, which has a small setting for rows_per_wal.
	 * Each yield can take up to 1ms if there are no events,
	 * so we can't afford many of them during recovery.
	 */
	ctx->yield = (rows_per_wal >> 4)  + 1;
}

void
process_rw(struct request *request, struct tuple **result)
{
	assert(iproto_type_is_dml(request->type));
	rmean_collect(rmean_box, request->type, 1);
	try {
		struct space *space = space_cache_find(request->space_id);
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
			if (space->has_unique_secondary_key) {
				tnt_raise(ClientError,
					  ER_UPSERT_UNIQUE_SECONDARY_KEY,
					  space_name(space));
			}
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
				tuple_bless(tuple);
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

static void
recover_row(struct recovery *r, void *param, struct xrow_header *row)
{
	struct recover_row_ctx *ctx = (struct recover_row_ctx *) param;
	assert(r == ::recovery);
	assert(row->bodycnt == 1); /* always 1 for read */
	(void) r;

	struct request request;
	request_create(&request, row->type);
	request_decode(&request, (const char *) row->body[0].iov_base,
		row->body[0].iov_len);
	request.header = row;
	process_rw(&request, NULL);
	/**
	 * Yield once in a while, but not too often,
	 * mostly to allow signal handling to take place.
	 */
	if (++ctx->rows % ctx->yield == 0)
		fiber_sleep(0);
}

/* {{{ configuration bindings */

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
box_check_replication_source(void)
{
	int count = cfg_getarr_size("replication_source");
	for (int i = 0; i < count; i++) {
		const char *source = cfg_getarr_elem("replication_source", i);
		box_check_uri(source, "replication_source");
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
	enum { READAHEAD_MIN = 128, READAHEAD_MAX = 2147483648 };
	if (readahead < READAHEAD_MIN || readahead > READAHEAD_MAX) {
		tnt_raise(ClientError, ER_CFG, "readahead",
			  "specified value is out of bounds");
	}
}

static int
box_check_rows_per_wal(int rows_per_wal)
{
	/* check rows_per_wal configuration */
	if (rows_per_wal <= 1) {
		tnt_raise(ClientError, ER_CFG, "rows_per_wal",
			  "the value must be greater than one");
	}
	return rows_per_wal;
}

void
box_check_config()
{
	box_check_wal_mode(cfg_gets("wal_mode"));
	box_check_uri(cfg_gets("listen"), "listen");
	box_check_replication_source();
	box_check_readahead(cfg_geti("readahead"));
	box_check_rows_per_wal(cfg_geti("rows_per_wal"));
	box_check_wal_mode(cfg_gets("wal_mode"));
}

/*
 * Sync box.cfg.replication_source and cluster registry.
 */
static void
box_sync_replication_source(void)
{
	/* Reset cfg_merge_flag for all replicas */
	cluster_foreach_applier(applier) {
		applier->cfg_merge_flag = false;
	}

	/* Add new replicas and set cfg_merge_flag for existing */
	int count = cfg_getarr_size("replication_source");
	for (int i = 0; i < count; i++) {
		const char *source = cfg_getarr_elem("replication_source", i);
		/* Try to find applier with the same source in the registry */
		struct applier *applier = cluster_find_applier(source);
		if (applier == NULL) {
			/* Start new applier using specified source */
			applier = applier_new(source); /* may throw */
			cluster_add_applier(applier);
		}
		applier->cfg_merge_flag = true;
	}

	/* Remove replicas without cfg_merge_flag */
	struct applier *applier = cluster_applier_first();
	while (applier != NULL) {
		struct applier *next = cluster_applier_next(applier);
		if (!applier->cfg_merge_flag) {
			applier_stop(applier); /* cancels a background fiber */
			cluster_del_applier(applier);
			applier_delete(applier);
		}
		applier = next;
	}
}

extern "C" void
box_set_replication_source(void)
{
	if (recovery->writer == NULL) {
		/*
		 * Do nothing, we're in local hot standby mode, the server
		 * will automatically begin following the replica when local
		 * hot standby mode is finished, see box_init().
		 */
		return;
	}

	box_sync_replication_source();

	/* Start all replicas from the cluster registry */
	cluster_foreach_applier(applier) {
		if (applier->reader == NULL) {
			applier_start(applier, recovery);
		} else if (applier->state == APPLIER_OFF ||
			   applier->state == APPLIER_STOPPED) {
			/* Re-start faulted replicas */
			applier_stop(applier);
			applier_start(applier, recovery);
		}
	}
}

extern "C" void
box_set_listen(void)
{
	const char *uri = cfg_gets("listen");
	box_check_uri(uri, "listen");
	iproto_set_listen(uri);
}

/**
 * Check if (host, port) in box.cfg.listen is equal to (host, port) in uri.
 * Used to determine that an uri from box.cfg.replication_source is
 * actually points to the same address as box.cfg.listen.
 */
static bool
box_cfg_listen_eq(struct uri *what)
{
	const char *listen = cfg_gets("listen");
	if (listen == NULL)
		return false;

	struct uri uri;
	int rc = uri_parse(&uri, listen);
	assert(rc == 0 && uri.service);
	(void) rc;

	return (uri.service_len == what->service_len &&
		uri.host_len == what->host_len &&
		memcmp(uri.service, what->service, uri.service_len) == 0 &&
		memcmp(uri.host, what->host, uri.host_len) == 0);
}

extern "C" void
box_set_wal_mode(void)
{
	const char *mode_name = cfg_gets("wal_mode");
	enum wal_mode mode = box_check_wal_mode(mode_name);
	if (mode != recovery->wal_mode &&
	    (mode == WAL_FSYNC || recovery->wal_mode == WAL_FSYNC)) {
		tnt_raise(ClientError, ER_CFG, "wal_mode",
			  "cannot switch to/from fsync");
	}
	/**
	 * Really update WAL mode only after we left local hot standby,
	 * since local hot standby expects it to be NONE.
	 */
	if (recovery->writer)
		recovery_update_mode(recovery, mode);
}

extern "C" void
box_set_log_level(void)
{
	say_set_log_level(cfg_geti("log_level"));
}

extern "C" void
box_set_io_collect_interval(void)
{
	ev_set_io_collect_interval(loop(), cfg_getd("io_collect_interval"));
}

extern "C" void
box_set_snap_io_rate_limit(void)
{
	recovery_update_io_rate_limit(recovery, cfg_getd("snap_io_rate_limit"));
}

extern "C" void
box_set_too_long_threshold(void)
{
	too_long_threshold = cfg_getd("too_long_threshold");
}

extern "C" void
box_set_readahead(void)
{
	int readahead = cfg_geti("readahead");
	box_check_readahead(readahead);
	iobuf_set_readahead(readahead);
}

extern "C" void
box_set_panic_on_wal_error(void)
{
	recovery_setup_panic(recovery,
			     cfg_geti("panic_on_snap_error"),
			     cfg_geti("panic_on_wal_error"));
}

/* }}} configuration bindings */

/**
 * Execute a request against a given space id with
 * a variable-argument tuple described in format.
 *
 * @example: you want to insert 5 into space 1:
 * boxk(IPROTO_INSERT, 1, "%u", 5);
 *
 * @note Since this is for internal use, it has
 * no boundary or misuse checks.
 */
void
boxk(enum iproto_type type, uint32_t space_id, const char *format, ...)
{
	struct request req;
	va_list ap;
	request_create(&req, type);
	req.space_id = space_id;
	char buf[128];
	char *data = buf;
	data = mp_encode_array(data, strlen(format)/2);
	va_start(ap, format);
	while (*format) {
		switch (*format++) {
		case 'u':
			data = mp_encode_uint(data, va_arg(ap, unsigned));
			break;
		case 's':
		{
			char *arg = va_arg(ap, char *);
			data = mp_encode_str(data, arg, strlen(arg));
			break;
		}
		default:
			break;
		}
	}
	va_end(ap);
	assert(data <= buf + sizeof(buf));
	req.tuple = req.key = buf;
	req.tuple_end = req.key_end = data;
	process_rw(&req, NULL);
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
		if (is_ro)
			tnt_raise(LoggedError, ER_READONLY);
		process_rw(request, result);
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
		port_eof(port);
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
	struct request request;
	request_create(&request, IPROTO_INSERT);
	request.space_id = space_id;
	request.tuple = tuple;
	request.tuple_end = tuple_end;
	return box_process1(&request, result);
}

int
box_replace(uint32_t space_id, const char *tuple, const char *tuple_end,
	    box_tuple_t **result)
{
	mp_tuple_assert(tuple, tuple_end);
	struct request request;
	request_create(&request, IPROTO_REPLACE);
	request.space_id = space_id;
	request.tuple = tuple;
	request.tuple_end = tuple_end;
	return box_process1(&request, result);
}

int
box_delete(uint32_t space_id, uint32_t index_id, const char *key,
	   const char *key_end, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	struct request request;
	request_create(&request, IPROTO_DELETE);
	request.space_id = space_id;
	request.index_id = index_id;
	request.key = key;
	request.key_end = key_end;
	return box_process1(&request, result);
}

int
box_update(uint32_t space_id, uint32_t index_id, const char *key,
	   const char *key_end, const char *ops, const char *ops_end,
	   int index_base, box_tuple_t **result)
{
	mp_tuple_assert(key, key_end);
	mp_tuple_assert(ops, ops_end);
	struct request request;
	request_create(&request, IPROTO_UPDATE);
	request.space_id = space_id;
	request.index_id = index_id;
	request.key = key;
	request.key_end = key_end;
	request.index_base = index_base;
	/** Legacy: in case of update, ops are passed in in request tuple */
	request.tuple = ops;
	request.tuple_end = ops_end;
	return box_process1(&request, result);
}

int
box_upsert(uint32_t space_id, uint32_t index_id, const char *tuple,
	   const char *tuple_end, const char *ops, const char *ops_end,
	   int index_base, box_tuple_t **result)
{
	mp_tuple_assert(ops, ops_end);
	mp_tuple_assert(tuple, tuple_end);
	struct request request;
	request_create(&request, IPROTO_UPSERT);
	request.space_id = space_id;
	request.index_id = index_id;
	request.ops = ops;
	request.ops_end = ops_end;
	request.tuple = tuple;
	request.tuple_end = tuple_end;
	request.index_base = index_base;
	return box_process1(&request, result);
}

/**
 * @brief Called when recovery/replication wants to add a new server
 * to cluster.
 * cluster_add_server() is called as a commit trigger on cluster
 * space and actually adds the server to the cluster.
 * @param server_uuid
 */
static void
box_on_cluster_join(const tt_uuid *server_uuid)
{
	/** Find the largest existing server id. */
	struct space *space = space_cache_find(BOX_CLUSTER_ID);
	class MemtxIndex *index = index_find_system(space, 0);
	struct iterator *it = index->position();
	index->initIterator(it, ITER_LE, NULL, 0);
	struct tuple *tuple = it->next(it);
	/** Assign a new server id. */
	uint32_t server_id = tuple ? tuple_field_u32(tuple, 0) + 1 : 1;
	boxk(IPROTO_INSERT, BOX_CLUSTER_ID, "%u%s",
	     (unsigned) server_id, tt_uuid_str(server_uuid));
}

void
box_process_join(int fd, struct xrow_header *header)
{
	/* Check permissions */
	access_check_universe(PRIV_R);
	access_check_space(space_cache_find(BOX_CLUSTER_ID), PRIV_W);

	assert(header->type == IPROTO_JOIN);

	/* Process JOIN request via a replication relay */
	relay_join(fd, header, recovery->server_id,
		   box_on_cluster_join);
}

void
box_process_subscribe(int fd, struct xrow_header *header)
{
	/* Check permissions */
	access_check_universe(PRIV_R);

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
	relay_subscribe(fd, header, recovery->server_id,
			&recovery->vclock);
}

/** Replace the current server id in _cluster */
static void
box_set_server_uuid()
{
	struct recovery *r = recovery;

	assert(r->server_id == 0);

	/* Unregister local server if it was registered by bootstrap.bin */
	if (vclock_has(&r->vclock, 1))
		boxk(IPROTO_DELETE, BOX_CLUSTER_ID, "%u", 1);
	assert(!vclock_has(&r->vclock, 1));

	/* Register local server */
	tt_uuid_create(&r->server_uuid);
	boxk(IPROTO_INSERT, BOX_CLUSTER_ID, "%u%s",
	     1, tt_uuid_str(&r->server_uuid));
	assert(vclock_has(&r->vclock, 1));

	/* Remove surrogate server */
	vclock_del_server(&r->vclock, 0);
	assert(r->server_id == 1);
}

/** Insert a new cluster into _schema */
static void
box_set_cluster_uuid()
{
	tt_uuid uu;
	/* Generate a new cluster UUID */
	tt_uuid_create(&uu);
	/* Save cluster UUID in _schema */
	boxk(IPROTO_REPLACE, BOX_SCHEMA_ID, "%s%s", "cluster",
	     tt_uuid_str(&uu));
}

void
box_free(void)
{
	if (recovery) {
		recovery_exit(recovery);
		recovery = NULL;
	}
	/*
	 * See gh-584 "box_free() is called even if box is not
	 * initialized
	 */
	if (box_init_done) {
#if 0
		session_free();
		cluster_free();
		user_cache_free();
		schema_free();
		tuple_free();
		port_free();
		rmean_delete(rmean_error);
		rmean_delete(rmean_box);
#endif
		engine_shutdown();
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
	MemtxEngine *memtx = new MemtxEngine();
	engine_register(memtx);

	SysviewEngine *sysview = new SysviewEngine();
	engine_register(sysview);

	SophiaEngine *sophia = new SophiaEngine();
	sophia->init();
	engine_register(sophia);
}

/**
 * @brief Reduce the current number of threads in the thread pool to the
 * bare minimum. Doesn't prevent the pool from spawning new threads later
 * if demand mounts.
 */
static void
thread_pool_trim()
{
	/*
	 * Trim OpenMP thread pool.
	 * Though we lack the direct control the workaround below works for
	 * GNU OpenMP library. The library stops surplus threads on entering
	 * a parallel region. Can't go below 2 threads due to the
	 * implementation quirk.
	 */
#pragma omp parallel num_threads(2)
	;
}

static inline void
box_init(void)
{
	tuple_init(cfg_getd("slab_alloc_arena"),
		   cfg_geti("slab_alloc_minimal"),
		   cfg_geti("slab_alloc_maximal"),
		   cfg_getd("slab_alloc_factor"));

	rmean_box = rmean_new(iproto_type_strs, IPROTO_TYPE_STAT_MAX);
	rmean_error = rmean_new(rmean_error_strings, RMEAN_ERROR_LAST);

	engine_init();

	schema_init();
	user_cache_init();
	/*
	 * The order is important: to initialize sessions,
	 * we need to access the admin user, which is used
	 * as a default session user when running triggers.
	 */
	session_init();

	cluster_init();

	title("loading");

	/* recovery initialization */
	recover_row_ctx_init(&recover_row_ctx,
			     cfg_geti("rows_per_wal"));
	recovery = recovery_new(cfg_gets("snap_dir"),
				cfg_gets("wal_dir"),
				recover_row, &recover_row_ctx);
	recovery_setup_panic(recovery,
			     cfg_geti("panic_on_snap_error"),
			     cfg_geti("panic_on_wal_error"));

	/*
	 * Initialize the cluster registry using replication_source,
	 * but don't start replication right now.
	 */
	box_sync_replication_source();
	/* Use the first replica by URI as a bootstrap leader */
	struct applier *applier = cluster_applier_first();

	if (recovery_has_data(recovery)) {
		/* Tell Sophia engine LSN it must recover to. */
		int64_t checkpoint_id =
			recovery_last_checkpoint(recovery);
		engine_recover_to_checkpoint(checkpoint_id);
	} else if (applier != NULL && !box_cfg_listen_eq(&applier->uri)) {
		/* Generate Server-UUID */
		tt_uuid_create(&recovery->server_uuid);

		/* Initialize a new replica */
		engine_begin_join();

		/* Add a surrogate server id for snapshot rows */
		vclock_add_server(&recovery->vclock, 0);

		/* Bootstrap from the first master */
		applier_start(applier, recovery);
		applier_wait(applier); /* throws on failure */

		int64_t checkpoint_id = vclock_sum(&recovery->vclock);
		engine_checkpoint(checkpoint_id);
	} else {
		/* Initialize the first server of a new cluster */
		recovery_bootstrap(recovery);
		box_set_cluster_uuid();
		box_set_server_uuid();
		int64_t checkpoint_id = vclock_sum(&recovery->vclock);
		engine_checkpoint(checkpoint_id);
	}
	fiber_gc();

	title("orphan");
	recovery_follow_local(recovery, "hot_standby",
			      cfg_getd("wal_dir_rescan_delay"));
	title("hot_standby");

	port_init();
	iproto_init();
	box_set_listen();

	int rows_per_wal = box_check_rows_per_wal(cfg_geti("rows_per_wal"));
	enum wal_mode wal_mode = box_check_wal_mode(cfg_gets("wal_mode"));
	recovery_finalize(recovery, wal_mode, rows_per_wal);

	engine_end_recovery();

	/*
	 * Recovery inflates the thread pool quite a bit (due to parallel sort).
	 */
	thread_pool_trim();

	rmean_cleanup(rmean_box);

	/* Follow replica */
	box_set_replication_source();

	/* Enter read-write mode. */
	if (recovery->server_id > 0)
		box_set_ro(false);
	title("running");
	say_info("ready to accept requests");

	fiber_gc();
	box_init_done = true;
}

void
box_load_cfg()
{
	try {
		box_init();
	} catch (Exception *e) {
		e->log();
		panic("can't initialize storage: %s", e->get_errmsg());
	}
}

void
box_atfork()
{
	/* NULL when forking for box.cfg{background = true} */
	if (recovery == NULL)
		return;
	/* box.coredump() forks to save a core. */
	recovery_atfork(recovery);
}

int
box_snapshot()
{
	/* create snapshot file */
	int64_t checkpoint_id = vclock_sum(&recovery->vclock);
	return engine_checkpoint(checkpoint_id);
}

const char *
box_status(void)
{
    return status;
}
