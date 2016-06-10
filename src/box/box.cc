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
#include "phia_engine.h"
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
#include "lua/call.h" /* box_lua_call */
#include "iproto_port.h"
#include "xrow.h"
#include "xrow_io.h"
#include "authentication.h"

static char status[64] = "unknown";

static void title(const char *new_status)
{
	strncpy(status, new_status, sizeof status);
	title_set_status(new_status);
	title_update();
}

struct recovery *recovery;

bool box_snapshot_is_in_progress = false;
static bool box_init_done = false;
static bool is_ro = true;

/* Use the shared instance of xstream for all appliers */
static struct xstream initial_join_stream;
static struct xstream final_join_stream;
static struct xstream subscribe_stream;

static void
box_check_writable(void)
{
	/*
	 * box is only writable if
	 *   box.cfg.read_only == false and
	 *   server id is registered in _cluster table
	 */
	if (is_ro || recovery->server_id == 0)
		tnt_raise(LoggedError, ER_READONLY);
}

static void
box_check_slab_alloc_minimal(ssize_t slab_alloc_minimal)
{

	if (slab_alloc_minimal < 8 || slab_alloc_minimal > 1048280)
	tnt_raise(ClientError, ER_CFG, "slab_alloc_minimal",
		  "specified value is out of bounds");
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

static inline void
apply_row(struct xstream *stream, struct xrow_header *row)
{
	assert(row->bodycnt == 1); /* always 1 for read */
	(void) stream;
	struct request *request;
	request = region_alloc_object_xc(&fiber()->gc, struct request);
	request_create(request, row->type);
	request_decode(request, (const char *) row->body[0].iov_base,
		row->body[0].iov_len);
	request->header = row;
	process_rw(request, NULL);
}

struct wal_stream {
	struct xstream base;
	/** How many rows have been recovered so far. */
	size_t rows;
	/** Yield once per 'yield' rows. */
	size_t yield;
};

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
wal_stream_create(struct wal_stream *ctx, size_t rows_per_wal)
{
	xstream_create(&ctx->base, apply_wal_row);
	ctx->rows = 0;
	/**
	 * Make the yield logic covered by the functional test
	 * suite, which has a small setting for rows_per_wal.
	 * Each yield can take up to 1ms if there are no events,
	 * so we can't afford many of them during recovery.
	 */
	ctx->yield = (rows_per_wal >> 4)  + 1;
}

static void
apply_initial_join_row(struct xstream *stream, struct xrow_header *row)
{
	if (row->type != IPROTO_INSERT) {
		tnt_raise(ClientError, ER_UNKNOWN_REQUEST_TYPE,
				(uint32_t) row->type);
	}

	(void) stream;
	struct request *request;
	request = region_alloc_object_xc(&fiber()->gc, struct request);
	request_create(request, row->type);
	assert(row->bodycnt == 1); /* always 1 for read */
	request_decode(request, (const char *) row->body[0].iov_base,
			row->body[0].iov_len);
	request->header = row;
	struct space *space = space_cache_find(request->space_id);
	/* no access checks here - applier always works with admin privs */
	space->handler->applySnapshotRow(space, request);
}

static void
apply_subscribe_row(struct xstream *stream, struct xrow_header *row)
{
	/* Check lsn */
	int64_t current_lsn = vclock_get(&recovery->vclock, row->server_id);
	if (row->lsn <= current_lsn)
		return;
	apply_row(stream, row);
}

/* {{{ configuration bindings */

static void
box_check_logger(const char *logger)
{
	char *error_msg;
	if (logger == NULL)
		return;
	if (say_check_init_str(logger, &error_msg) == -1) {
		auto guard = make_scoped_guard([=]{ free(error_msg); });
		tnt_raise(ClientError, ER_CFG, "logger", error_msg);
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
	enum { READAHEAD_MIN = 128, READAHEAD_MAX = 2147483647 };
	if (readahead < (int) READAHEAD_MIN ||
	    readahead > (int) READAHEAD_MAX) {
		tnt_raise(ClientError, ER_CFG, "readahead",
			  "specified value is out of bounds");
	}
}

static int64_t
box_check_rows_per_wal(int64_t rows_per_wal)
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
	box_check_logger(cfg_gets("logger"));
	box_check_uri(cfg_gets("listen"), "listen");
	box_check_replication_source();
	box_check_readahead(cfg_geti("readahead"));
	box_check_rows_per_wal(cfg_geti64("rows_per_wal"));
	box_check_wal_mode(cfg_gets("wal_mode"));
	box_check_slab_alloc_minimal(cfg_geti64("slab_alloc_minimal"));
}

/*
 * Parse box.cfg.replication_source and create appliers.
 */
static struct applier **
cfg_get_replication_source(int *p_count)
{

	/* Use static buffer for result */
	static struct applier *appliers[VCLOCK_MAX];

	int count = cfg_getarr_size("replication_source");
	if (count >= VCLOCK_MAX) {
		tnt_raise(ClientError, ER_CFG, "replication_source",
				"too many replicas");
	}

	for (int i = 0; i < count; i++) {
		const char *source = cfg_getarr_elem("replication_source", i);
		struct applier *applier = applier_new(source,
						      &initial_join_stream,
						      &final_join_stream,
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
 * Sync box.cfg.replication_source with the cluster registry, but
 * don't start appliers.
 */
static void
box_sync_replication_source(void)
{
	int count = 0;
	struct applier **appliers = cfg_get_replication_source(&count);
	if (appliers == NULL)
		diag_raise();

	auto guard = make_scoped_guard([=]{
		for (int i = 0; i < count; i++)
			applier_delete(appliers[i]); /* doesn't affect diag */
	});

	applier_connect_all(appliers, count);
	cluster_set_appliers(appliers, count);

	guard.is_active = false;
}

extern "C" void
box_set_replication_source(void)
{
	if (!box_init_done) {
		/*
		 * Do nothing, we're in local hot standby mode, the server
		 * will automatically begin following the replica when local
		 * hot standby mode is finished, see box_init().
		 */
		return;
	}

	box_sync_replication_source();
	server_foreach(server) {
		if (server->applier != NULL)
			applier_resume(server->applier);
	}
}

extern "C" void
box_set_listen(void)
{
	const char *uri = cfg_gets("listen");
	box_check_uri(uri, "listen");
	iproto_set_listen(uri);
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
	MemtxEngine *memtx = (MemtxEngine *) engine_find("memtx");
	if (memtx)
		memtx->setSnapIoRateLimit(cfg_getd("snap_io_rate_limit"));
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
static void
boxk(enum iproto_type type, uint32_t space_id, const char *format, ...)
{
	va_list ap;
	struct request *request;
	request = region_alloc_object_xc(&fiber()->gc, struct request);
	request_create(request, type);
	request->space_id = space_id;
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
	switch (type) {
	case IPROTO_INSERT:
	case IPROTO_REPLACE:
		request->tuple = buf;
		request->tuple_end = data;
		break;
	case IPROTO_DELETE:
		request->key = buf;
		request->key_end = data;
		break;
	default:
		unreachable();
	}
	process_rw(request, NULL);
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
		box_check_writable();
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
	Index *index = index_find(space_index, 0);
	struct iterator *it = index->allocIterator();
	auto guard_it_free = make_scoped_guard([=]{
		it->free(it);
	});
	index->initIterator(it, ITER_EQ, key_buf, 1);
	int index_count = 0;
	struct tuple *indexes[BOX_INDEX_MAX]; /* max count of idexes*/
	struct tuple *tuple;

	/* select all indexes of given space */
	auto guard_indexes_unref = make_scoped_guard([=]{
		for (int i = 0; i < index_count; i++)
			tuple_unref(indexes[i]);
	});
	while ((tuple = it->next(it)) != NULL) {
		tuple_ref(tuple);
		indexes[index_count++] = tuple;
	}
	assert(index_count <= BOX_INDEX_MAX);

	/* drop all selected indexes */
	for (int i = index_count - 1; i >= 0; --i) {
		uint32_t index_id = tuple_field_u32(indexes[i], 1);
		key_buf_end = mp_encode_array(key_buf, 2);
		key_buf_end = mp_encode_uint(key_buf_end, space_id(space));
		key_buf_end = mp_encode_uint(key_buf_end, index_id);
		assert(key_buf_end <= key_buf + sizeof(key_buf));
		if (box_delete(BOX_INDEX_ID, 0, key_buf, key_buf_end, NULL))
			diag_raise();
	}

	/* create all indexes again, now they are empty */
	for (int i = 0; i < index_count; i++) {
		tuple = indexes[i];
		if (box_insert(BOX_INDEX_ID, tuple->data,
			       tuple->data + tuple->bsize, NULL)) {
			diag_raise();
		}
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
box_register_server(uint32_t id, const struct tt_uuid *uuid)
{
	boxk(IPROTO_INSERT, BOX_CLUSTER_ID, "%u%s",
	     (unsigned) id, tt_uuid_str(uuid));
	assert(server_by_uuid(uuid) != NULL);
}

/**
 * @brief Called when recovery/replication wants to add a new server
 * to cluster.
 * server_set_id() is called as a commit trigger on cluster
 * space and actually adds the server to the cluster.
 * @param server_uuid
 */
static void
box_on_cluster_join(const tt_uuid *server_uuid)
{
	box_check_writable();
	struct server *server = server_by_uuid(server_uuid);
	if (server != NULL)
		return; /* nothing to do - already registered */

	/** Find the largest existing server id. */
	struct space *space = space_cache_find(BOX_CLUSTER_ID);
	class MemtxIndex *index = index_find_system(space, 0);
	struct iterator *it = index->position();
	index->initIterator(it, ITER_ALL, NULL, 0);
	struct tuple *tuple;
	/** Assign a new server id. */
	uint32_t server_id = 1;
	while ((tuple = it->next(it))) {
		if (tuple_field_u32(tuple, 0) != server_id)
			break;
		server_id++;
	}
	box_register_server(server_id, server_uuid);
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

	for (struct port_entry *entry = port.first;
	     entry != NULL; entry = entry->next) {
		if (tuple_to_obuf(entry->tuple, out) != 0) {
			obuf_rollback_to_svp(out, &svp);
			goto error;
		}
	}
	iproto_reply_select(out, &svp, request->header->sync, port.size);

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
			credentials_init(&func->owner_credentials, owner);
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
	assert(request->type == IPROTO_AUTH);

	/* Check that bootstrap has been finished */
	if (!box_init_done)
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
	 * => JOIN { SERVER_UUID: replica_uuid }
	 * <= OK { VCLOCK: start_vclock }
	 *    Replica has enough permissions and master is ready for JOIN.
	 *     - start_vclock - vclock of the latest master's checkpoint.
	 *
	 * <= INSERT
	 *    ...
	 *    Initial data: a stream of engine-specifc rows, e.g. snapshot
	 *    rows for memtx or dirty cursor data for Phia. Engine can
	 *    use SERVER_ID, LSN and other fields for internal purposes.
	 *    ...
	 * <= INSERT
	 * <= OK { VCLOCK: stop_vclock } - end of initial JOIN stage.
	 *     - `stop_vclock` - master's vclock after initial stage.
	 *
	 * <= INSERT/REPLACE/UPDATE/UPSERT/DELETE { SERVER_ID, LSN }
	 *    ...
	 *    Final data: a stream of WAL rows from `start_vclock` to
	 *    `stop_vclock`, inclusive. SERVER_ID and LSN fields are
	 *    original values from WAL and master-master replication.
	 *    ...
	 * <= INSERT/REPLACE/UPDATE/UPSERT/DELETE { SERVER_ID, LSN }
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
	struct tt_uuid server_uuid = uuid_nil;
	xrow_decode_join(header, &server_uuid);

	/* Check that bootstrap has been finished */
	if (!box_init_done)
		tnt_raise(ClientError, ER_LOADING);

	/* Forbid connection to itself */
	if (tt_uuid_is_equal(&server_uuid, &SERVER_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Check permissions */
	access_check_universe(PRIV_R);
	access_check_space(space_cache_find(BOX_CLUSTER_ID), PRIV_W);

	/* Check that we actually can register a new replica */
	box_check_writable();

	/* Remember start vclock. */
	struct vclock start_vclock;
	recovery_last_checkpoint(&start_vclock);

	/* Respond to JOIN request with start_vclock. */
	struct xrow_header row;
	xrow_encode_vclock(&row, &start_vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	/*
	 * Initial stream: feed replica with dirty data from engines.
	 */
	relay_initial_join(io->fd, header->sync);
	say_info("initial data sent.");

	/**
	 * Call the server-side hook which stores the replica uuid
	 * in _cluster space after sending the last row but before
	 * sending OK - if the hook fails, the error reaches the
	 * client.
	 */
	box_on_cluster_join(&server_uuid);

	/* Remember master's vclock after the last request */
	struct vclock stop_vclock;
	wal_checkpoint(wal, &stop_vclock, false);

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
	wal_checkpoint(wal, &current_vclock, false);
	xrow_encode_vclock(&row, &current_vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);
}

void
box_process_subscribe(struct ev_io *io, struct xrow_header *header)
{
	assert(header->type == IPROTO_SUBSCRIBE);

	/* Check that bootstrap has been finished */
	if (!box_init_done)
		tnt_raise(ClientError, ER_LOADING);

	struct tt_uuid cluster_uuid = uuid_nil, replica_uuid = uuid_nil;
	struct vclock replica_clock;
	vclock_create(&replica_clock);
	xrow_decode_subscribe(header, &cluster_uuid, &replica_uuid,
			      &replica_clock);

	/* Forbid connection to itself */
	if (tt_uuid_is_equal(&replica_uuid, &SERVER_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Check permissions */
	access_check_universe(PRIV_R);

	/**
	 * Check that the given UUID matches the UUID of the
	 * cluster this server belongs to. Used to handshake
	 * replica connect, and refuse a connection from a replica
	 * which belongs to a different cluster.
	 */
	if (!tt_uuid_is_equal(&cluster_uuid, &CLUSTER_UUID)) {
		tnt_raise(ClientError, ER_CLUSTER_ID_MISMATCH,
			  tt_uuid_str(&cluster_uuid),
			  tt_uuid_str(&CLUSTER_UUID));
	}

	/* Check server uuid */
	struct server *server = server_by_uuid(&replica_uuid);
	if (server == NULL || server->id == SERVER_ID_NIL) {
		tnt_raise(ClientError, ER_UNKNOWN_SERVER,
			  tt_uuid_str(&replica_uuid));
	}

	/*
	 * Send a response to SUBSCRIBE request, tell
	 * the replica how many rows we have in stock for it,
	 * and identify ourselves with our own server id.
	 */
	struct xrow_header row;
	struct vclock current_vclock;
	wal_checkpoint(wal, &current_vclock, true);
	xrow_encode_vclock(&row, &current_vclock);
	/*
	 * Identify the message with the server id of this
	 * server, this is the only way for a replica to find
	 * out the id of the server it has connected to.
	 */
	row.server_id = recovery->server_id;
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
	relay_subscribe(io->fd, header->sync, server, &replica_clock);
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
		if (wal)
			wal_writer_stop();
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
	MemtxEngine *memtx = new MemtxEngine(cfg_gets("snap_dir"),
					     cfg_geti("panic_on_snap_error"),
					     cfg_geti("panic_on_wal_error"));
	engine_register(memtx);

	SysviewEngine *sysview = new SysviewEngine();
	engine_register(sysview);

	PhiaEngine *phia = new PhiaEngine();
	phia->init();
	engine_register(phia);
}

/**
 * Initialize the first server of a new cluster
 */
static void
bootstrap_cluster(void)
{
	engine_bootstrap();

	uint32_t server_id = 1;

	/* Unregister local server if it was registered by bootstrap.bin */
	assert(recovery->server_id == 0);
	boxk(IPROTO_DELETE, BOX_CLUSTER_ID, "%u", 1);

	/* Register local server */
	box_register_server(server_id, &SERVER_UUID);
	assert(recovery->server_id == 1);

	/* Register other cluster members */
	server_foreach(server) {
		if (tt_uuid_is_equal(&server->uuid, &SERVER_UUID))
			continue;
		assert(server->applier != NULL);
		box_register_server(++server_id, &server->uuid);
		assert(server->id == server_id);
	}

	/* Generate UUID of a new cluster */
	box_set_cluster_uuid();

	/* Ugly hack: bootstrap always starts from scratch */
	vclock_create(&recovery->vclock);
}

/**
 * Bootstrap from the remote master
 * \pre  master->applier->state == APPLIER_CONNECTED
 * \post master->applier->state == APPLIER_CONNECTED
 */
static void
bootstrap_from_master(struct server *master)
{
	struct applier *applier = master->applier;
	assert(applier != NULL);
	assert(applier->state == APPLIER_CONNECTED);

	say_info("bootstraping replica from %s",
		 sio_strfaddr(&applier->addr, applier->addr_len));

	/*
	 * Send JOIN request to master
	 * See box_process_join().
	 */

	assert(!tt_uuid_is_nil(&SERVER_UUID));
	applier_resume_to_state(applier, APPLIER_INITIAL_JOIN, TIMEOUT_INFINITY);

	/*
	 * Process initial data (snapshot or dirty disk data).
	 */
	engine_begin_initial_recovery();

	applier_resume_to_state(applier, APPLIER_FINAL_JOIN, TIMEOUT_INFINITY);

	/*
	 * Process final data (WALs).
	 */
	engine_begin_final_recovery();

	applier_resume_to_state(applier, APPLIER_JOINED, TIMEOUT_INFINITY);

	/* Replace server vclock using master's vclock */
	vclock_copy(&recovery->vclock, &applier->vclock);

	/* Finalize the new replica */
	engine_end_recovery();

	/* Switch applier to initial state */
	applier_resume_to_state(applier, APPLIER_CONNECTED, TIMEOUT_INFINITY);
	assert(applier->state == APPLIER_CONNECTED);
}

static void
bootstrap(void)
{
	/* Use the first replica by URI as a bootstrap leader */
	struct server *master = server_first();
	assert(master == NULL || master->applier != NULL);

	if (master != NULL && !tt_uuid_is_equal(&master->uuid, &SERVER_UUID)) {
		bootstrap_from_master(master);
	} else {
		bootstrap_cluster();
	}
	if (engine_begin_checkpoint() ||
	    engine_commit_checkpoint(&recovery->vclock))
		panic_syserror("failed to save a snapshot");
}

static inline void
box_init(void)
{
	error_init();


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

	box_set_too_long_threshold();
	struct wal_stream wal_stream;
	wal_stream_create(&wal_stream, cfg_geti64("rows_per_wal"));
	xstream_create(&initial_join_stream, apply_initial_join_row);
	xstream_create(&final_join_stream, apply_row);
	xstream_create(&subscribe_stream, apply_subscribe_row);

	struct vclock checkpoint_vclock;
	int64_t lsn = recovery_last_checkpoint(&checkpoint_vclock);
	if (lsn != -1) {
		recovery = recovery_new(cfg_gets("wal_dir"),
					cfg_geti("panic_on_wal_error"),
					&checkpoint_vclock);
		engine_begin_initial_recovery();

		/* Replace server vclock using the data from snapshot */
		vclock_copy(&recovery->vclock, &checkpoint_vclock);
		engine_begin_final_recovery();
		title("orphan");
		recovery_follow_local(recovery, &wal_stream.base, "hot_standby",
				      cfg_getd("wal_dir_rescan_delay"));
		title("hot_standby");

		/* Start network */
		assert(!tt_uuid_is_nil(&SERVER_UUID));
		port_init();
		iproto_init();
		box_set_listen();
		recovery_finalize(recovery, &wal_stream.base);

		box_sync_replication_source();

		engine_end_recovery();
	} else {
		/* TODO: don't create recovery for this case */
		vclock_create(&checkpoint_vclock);
		recovery = recovery_new(cfg_gets("wal_dir"),
					cfg_geti("panic_on_wal_error"),
					&checkpoint_vclock);

		/* Start network */
		tt_uuid_create(&SERVER_UUID);
		port_init();
		iproto_init();
		box_set_listen();
		box_sync_replication_source();

		/* Bootstrap cluster */
		bootstrap();
	}
	fiber_gc();

	int64_t rows_per_wal = box_check_rows_per_wal(cfg_geti64("rows_per_wal"));
	enum wal_mode wal_mode = box_check_wal_mode(cfg_gets("wal_mode"));
	if (wal_mode != WAL_NONE) {
		wal_writer_start(wal_mode, cfg_gets("wal_dir"), &SERVER_UUID,
				 &recovery->vclock, rows_per_wal);
	}

	rmean_cleanup(rmean_box);

	/* Follow replica */
	server_foreach(server) {
		if (server->applier != NULL)
			applier_resume(server->applier);
	}

	/* Enter read-write mode. */
	cluster_wait_for_id();

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
	int rc = 0;
	if (box_snapshot_is_in_progress)
		return EINPROGRESS;
	box_snapshot_is_in_progress = true;
	/* create snapshot file */
	latch_lock(&schema_lock);
	if ((rc = engine_begin_checkpoint()))
		goto end;

	struct vclock vclock;
	if (wal == NULL) {
		vclock_copy(&vclock, &recovery->vclock);
	} else {
		wal_checkpoint(wal, &vclock, true);
	}
	rc = engine_commit_checkpoint(&vclock);
end:
	if (rc)
		engine_abort_checkpoint();
	latch_unlock(&schema_lock);
	box_snapshot_is_in_progress = false;
	return rc;
}

const char *
box_status(void)
{
    return status;
}
