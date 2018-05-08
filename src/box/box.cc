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

#include "lua/utils.h" /* lua_hash() */
#include "fiber_pool.h"
#include <say.h>
#include <scoped_guard.h>
#include "identifier.h"
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
#include "schema.h"
#include "engine.h"
#include "memtx_engine.h"
#include "sysview_engine.h"
#include "vinyl.h"
#include "space.h"
#include "index.h"
#include "port.h"
#include "txn.h"
#include "user.h"
#include "cfg.h"
#include "coio.h"
#include "replication.h" /* replica */
#include "title.h"
#include "xrow.h"
#include "xrow_io.h"
#include "xstream.h"
#include "authentication.h"
#include "path_lock.h"
#include "gc.h"
#include "checkpoint.h"
#include "sql.h"
#include "systemd.h"
#include "call.h"
#include "func.h"
#include "sequence.h"

static char status[64] = "unknown";

/** box.stat rmean */
struct rmean *rmean_box;

static void title(const char *new_status)
{
	snprintf(status, sizeof(status), "%s", new_status);
	title_set_status(new_status);
	title_update();
	systemd_snotify("STATUS=%s", status);
}

bool box_checkpoint_is_in_progress = false;

/**
 * If backup is in progress, this points to the gc consumer
 * object that prevents the garbage collector from deleting
 * the checkpoint files that are currently being backed up.
 */
static struct gc_consumer *backup_gc;

/**
 * The instance is in read-write mode: the local checkpoint
 * and all write ahead logs are processed. For a replica,
 * it also means we've successfully connected to the master
 * and began receiving updates from it.
 */
static bool is_box_configured = false;
static bool is_ro = true;
static fiber_cond ro_cond;

/**
 * The following flag is set if the instance failed to
 * synchronize to a sufficient number of replicas to form
 * a quorum and so was forced to switch to read-only mode.
 */
static bool is_orphan = true;

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

static int
box_check_writable(void)
{
	/* box is only writable if box.cfg.read_only == false and */
	if (is_ro || is_orphan) {
		diag_set(ClientError, ER_READONLY);
		diag_log();
		return -1;
	}
	return 0;
}

static void
box_check_writable_xc(void)
{
	if (box_check_writable() != 0)
		diag_raise();
}

static void
box_check_memtx_min_tuple_size(ssize_t memtx_min_tuple_size)
{

	if (memtx_min_tuple_size < 8 || memtx_min_tuple_size > 1048280)
	tnt_raise(ClientError, ER_CFG, "memtx_min_tuple_size",
		  "specified value is out of bounds");
}

int
box_process_rw(struct request *request, struct space *space,
	       struct tuple **result)
{
	assert(iproto_type_is_dml(request->type));
	rmean_collect(rmean_box, request->type, 1);
	if (access_check_space(space, PRIV_W) != 0)
		return -1;
	struct txn *txn = txn_begin_stmt(space);
	if (txn == NULL)
		return -1;
	struct tuple *tuple;
	if (space_execute_dml(space, txn, request, &tuple) != 0) {
		txn_rollback_stmt();
		return -1;
	}
	/*
	 * Pin the tuple locally before the commit,
	 * otherwise it may go away during yield in
	 * when WAL is written in autocommit mode.
	 */
	TupleRefNil ref(tuple);
	if (txn_commit_stmt(txn, request) != 0)
		return -1;
	if (result != NULL) {
		if (tuple != NULL && tuple_bless(tuple) == NULL)
			return -1;
		*result = tuple;
	}
	return 0;
}

void
box_set_ro(bool ro)
{
	is_ro = ro;
	fiber_cond_broadcast(&ro_cond);
}

bool
box_is_ro(void)
{
	return is_ro || is_orphan;
}

int
box_wait_ro(bool ro, double timeout)
{
	double deadline = ev_monotonic_now(loop()) + timeout;
	while (box_is_ro() != ro) {
		if (fiber_cond_wait_deadline(&ro_cond, deadline) != 0)
			return -1;
		if (fiber_is_cancelled()) {
			diag_set(FiberIsCancelled);
			return -1;
		}
	}
	return 0;
}

void
box_clear_orphan(void)
{
	if (!is_orphan)
		return; /* nothing to do */

	is_orphan = false;
	fiber_cond_broadcast(&ro_cond);

	/* Update the title to reflect the new status. */
	title("running");
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
static int64_t
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
	struct request request;
	xrow_decode_dml_xc(row, &request, dml_request_key_map(row->type));
	struct space *space = space_cache_find_xc(request.space_id);
	if (box_process_rw(&request, space, NULL) != 0) {
		say_error("error applying row: %s", request_str(&request));
		diag_raise();
	}
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
	struct request request;
	xrow_decode_dml_xc(row, &request, dml_request_key_map(row->type));
	struct space *space = space_cache_find_xc(request.space_id);
	/* no access checks here - applier always works with admin privs */
	space_apply_initial_join_row_xc(space, &request);
}

/* {{{ configuration bindings */

static void
box_check_say()
{
	const char *log = cfg_gets("log");
	if (log == NULL)
		return;
	enum say_logger_type type;
	if (say_parse_logger_type(&log, &type) < 0) {
		tnt_raise(ClientError, ER_CFG, "log",
			  diag_last_error(diag_get())->errmsg);
	}

	if (say_check_init_str(log) == -1) {

		diag_raise();
	}

	if (type == SAY_LOGGER_SYSLOG) {
		struct say_syslog_opts opts;
		if (say_parse_syslog_opts(log, &opts) < 0) {
			if (diag_last_error(diag_get())->type ==
			    &type_IllegalParams) {
				tnt_raise(ClientError, ER_CFG, "log",
					  diag_last_error(diag_get())->errmsg);
			}
		}
		say_free_syslog_opts(&opts);
		diag_raise();
	}

	const char *log_format = cfg_gets("log_format");
	enum say_format format = say_format_by_name(log_format);
	if (format == say_format_MAX)
		diag_set(ClientError, ER_CFG, "log_format",
			 "expected 'plain' or 'json'");
	if (type == SAY_LOGGER_SYSLOG && format == SF_JSON) {
		tnt_raise(ClientError, ER_ILLEGAL_PARAMS, "log, log_format");
	}
	int log_nonblock = cfg_getb("log_nonblock");
	if (log_nonblock == 1 && type == SAY_LOGGER_FILE) {
		tnt_raise(ClientError, ER_ILLEGAL_PARAMS, "log, log_nonblock");
	}
}

static enum say_format
box_check_log_format(const char *log_format)
{
	enum say_format format = say_format_by_name(log_format);
	if (format == say_format_MAX)
		tnt_raise(ClientError, ER_CFG, "log_format",
			  "expected 'plain' or 'json'");
	return format;
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

static double
box_check_replication_timeout(void)
{
	double timeout = cfg_getd("replication_timeout");
	if (timeout <= 0) {
		tnt_raise(ClientError, ER_CFG, "replication_timeout",
			  "the value must be greather than 0");
	}
	return timeout;
}

static double
box_check_replication_connect_timeout(void)
{
	double timeout = cfg_getd("replication_connect_timeout");
	if (timeout <= 0) {
		tnt_raise(ClientError, ER_CFG, "replication_connect_timeout",
			  "the value must be greather than 0");
	}
	return timeout;
}

static int
box_check_replication_connect_quorum(void)
{
	int quorum = cfg_geti_default("replication_connect_quorum",
				      REPLICATION_CONNECT_QUORUM_ALL);
	if (quorum < 0) {
		tnt_raise(ClientError, ER_CFG, "replication_connect_quorum",
			  "the value must be greater or equal to 0");
	}
	return quorum;
}

static double
box_check_replication_sync_lag(void)
{
	double lag = cfg_getd_default("replication_sync_lag", TIMEOUT_INFINITY);
	if (lag <= 0) {
		tnt_raise(ClientError, ER_CFG, "replication_sync_lag",
			  "the value must be greater than 0");
	}
	return lag;
}

static void
box_check_instance_uuid(struct tt_uuid *uuid)
{
	*uuid = uuid_nil;
	const char *uuid_str = cfg_gets("instance_uuid");
	if (uuid_str != NULL && tt_uuid_from_string(uuid_str, uuid) != 0)
		tnt_raise(ClientError, ER_CFG, "instance_uuid", uuid_str);
}

static void
box_check_replicaset_uuid(struct tt_uuid *uuid)
{
	*uuid = uuid_nil;
	const char *uuid_str = cfg_gets("replicaset_uuid");
	if (uuid_str != NULL && tt_uuid_from_string(uuid_str, uuid) != 0)
		tnt_raise(ClientError, ER_CFG, "replicaset_uuid", uuid_str);
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

static void
box_check_checkpoint_count(int checkpoint_count)
{
	if (checkpoint_count < 1) {
		tnt_raise(ClientError, ER_CFG, "checkpoint_count",
			  "the value must not be less than one");
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

static void
box_check_vinyl_options(void)
{
	int read_threads = cfg_geti("vinyl_read_threads");
	int write_threads = cfg_geti("vinyl_write_threads");
	int64_t range_size = cfg_geti64("vinyl_range_size");
	int64_t page_size = cfg_geti64("vinyl_page_size");
	int run_count_per_level = cfg_geti("vinyl_run_count_per_level");
	double run_size_ratio = cfg_getd("vinyl_run_size_ratio");
	double bloom_fpr = cfg_getd("vinyl_bloom_fpr");

	if (read_threads < 1) {
		tnt_raise(ClientError, ER_CFG, "vinyl_read_threads",
			  "must be greater than or equal to 1");
	}
	if (write_threads < 2) {
		tnt_raise(ClientError, ER_CFG, "vinyl_write_threads",
			  "must be greater than or equal to 2");
	}
	if (range_size <= 0) {
		tnt_raise(ClientError, ER_CFG, "vinyl_range_size",
			  "must be greater than 0");
	}
	if (page_size <= 0 || page_size > range_size) {
		tnt_raise(ClientError, ER_CFG, "vinyl_page_size",
			  "must be greater than 0 and less than "
			  "or equal to vinyl_range_size");
	}
	if (run_count_per_level <= 0) {
		tnt_raise(ClientError, ER_CFG, "vinyl_run_count_per_level",
			  "must be greater than 0");
	}
	if (run_size_ratio <= 1) {
		tnt_raise(ClientError, ER_CFG, "vinyl_run_size_ratio",
			  "must be greater than 1");
	}
	if (bloom_fpr <= 0 || bloom_fpr > 1) {
		tnt_raise(ClientError, ER_CFG, "vinyl_bloom_fpr",
			  "must be greater than 0 and less than or equal to 1");
	}
}

void
box_check_config()
{
	struct tt_uuid uuid;
	box_check_say();
	box_check_uri(cfg_gets("listen"), "listen");
	box_check_instance_uuid(&uuid);
	box_check_replicaset_uuid(&uuid);
	box_check_replication();
	box_check_replication_timeout();
	box_check_replication_connect_timeout();
	box_check_replication_connect_quorum();
	box_check_replication_sync_lag();
	box_check_readahead(cfg_geti("readahead"));
	box_check_checkpoint_count(cfg_geti("checkpoint_count"));
	box_check_wal_max_rows(cfg_geti64("rows_per_wal"));
	box_check_wal_max_size(cfg_geti64("wal_max_size"));
	box_check_wal_mode(cfg_gets("wal_mode"));
	box_check_memtx_min_tuple_size(cfg_geti64("memtx_min_tuple_size"));
	box_check_vinyl_options();
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
box_sync_replication(double timeout, bool connect_all)
{
	int count = 0;
	struct applier **appliers = cfg_get_replication(&count);
	if (appliers == NULL)
		diag_raise();

	auto guard = make_scoped_guard([=]{
		for (int i = 0; i < count; i++)
			applier_delete(appliers[i]); /* doesn't affect diag */
	});

	replicaset_connect(appliers, count, timeout, connect_all);

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
	box_sync_replication(replication_connect_timeout, true);
	/* Follow replica */
	replicaset_follow();
}

void
box_set_replication_timeout(void)
{
	replication_timeout = box_check_replication_timeout();
}

void
box_set_replication_connect_timeout(void)
{
	replication_connect_timeout = box_check_replication_connect_timeout();
}

void
box_set_replication_connect_quorum(void)
{
	replication_connect_quorum = box_check_replication_connect_quorum();
	if (is_box_configured)
		replicaset_check_quorum();
}

void
box_set_replication_skip_conflict(void)
{
	replication_skip_conflict = cfg_geti("replication_skip_conflict");
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
box_set_log_format(void)
{
	enum say_format format = box_check_log_format(cfg_gets("log_format"));
	say_set_log_format(format);
}

void
box_set_io_collect_interval(void)
{
	ev_set_io_collect_interval(loop(), cfg_getd("io_collect_interval"));
}

void
box_set_snap_io_rate_limit(void)
{
	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");
	assert(memtx != NULL);
	memtx_engine_set_snap_io_rate_limit(memtx,
			cfg_getd("snap_io_rate_limit"));
}

void
box_set_memtx_max_tuple_size(void)
{
	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");
	assert(memtx != NULL);
	memtx_engine_set_max_tuple_size(memtx,
			cfg_geti("memtx_max_tuple_size"));
}

void
box_set_too_long_threshold(void)
{
	too_long_threshold = cfg_getd("too_long_threshold");

	struct vinyl_engine *vinyl;
	vinyl = (struct vinyl_engine *)engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_too_long_threshold(vinyl, too_long_threshold);
}

void
box_set_readahead(void)
{
	int readahead = cfg_geti("readahead");
	box_check_readahead(readahead);
	iproto_readahead = readahead;
}

void
box_set_checkpoint_count(void)
{
	int checkpoint_count = cfg_geti("checkpoint_count");
	box_check_checkpoint_count(checkpoint_count);
	gc_set_checkpoint_count(checkpoint_count);
}

void
box_set_vinyl_max_tuple_size(void)
{
	struct vinyl_engine *vinyl;
	vinyl = (struct vinyl_engine *)engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_max_tuple_size(vinyl,
			cfg_geti("vinyl_max_tuple_size"));
}

void
box_set_vinyl_cache(void)
{
	struct vinyl_engine *vinyl;
	vinyl = (struct vinyl_engine *)engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_cache(vinyl, cfg_geti64("vinyl_cache"));
}

void
box_set_vinyl_timeout(void)
{
	struct vinyl_engine *vinyl;
	vinyl = (struct vinyl_engine *)engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_timeout(vinyl,	cfg_getd("vinyl_timeout"));
}

void
box_set_net_msg_max(void)
{
	int new_iproto_msg_max = cfg_geti("net_msg_max");
	iproto_set_msg_max(new_iproto_msg_max);
	fiber_pool_set_max_size(&tx_fiber_pool,
				new_iproto_msg_max *
				IPROTO_FIBER_POOL_SIZE_FACTOR);
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
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = type;
	request.space_id = space_id;
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
		request.tuple = data;
		request.tuple_end = data_end;
		break;
	case IPROTO_DELETE:
		request.key = data;
		request.key_end = data_end;
		break;
	case IPROTO_UPDATE:
		request.key = data;
		mp_next(&data);
		request.key_end = data;
		request.tuple = data;
		mp_next(&data);
		request.tuple_end = data;
		request.index_base = 0;
		break;
	default:
		unreachable();
	}
	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return -1;
	return box_process_rw(&request, space, NULL);
}

int
box_return_tuple(box_function_ctx_t *ctx, box_tuple_t *tuple)
{
	return port_tuple_add(ctx->port, tuple);
}

/* schema_find_id()-like method using only public API */
uint32_t
box_space_id_by_name(const char *name, uint32_t len)
{
	if (len > BOX_NAME_MAX)
		return BOX_ID_NIL;
	uint32_t size = mp_sizeof_array(1) + mp_sizeof_str(len);
	char *begin = (char *) region_alloc(&fiber()->gc, size);
	if (begin == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "begin");
		return BOX_ID_NIL;
	}
	char *end = mp_encode_array(begin, 1);
	end = mp_encode_str(end, name, len);

	/* NOTE: error and missing key cases are indistinguishable */
	box_tuple_t *tuple;
	if (box_index_get(BOX_VSPACE_ID, 2, begin, end, &tuple) != 0)
		return BOX_ID_NIL;
	if (tuple == NULL)
		return BOX_ID_NIL;
	uint32_t result = BOX_ID_NIL;
	(void) tuple_field_u32(tuple, BOX_SPACE_FIELD_ID, &result);
	return result;
}

uint32_t
box_index_id_by_name(uint32_t space_id, const char *name, uint32_t len)
{
	if (len > BOX_NAME_MAX)
		return BOX_ID_NIL;
	uint32_t size = mp_sizeof_array(2) + mp_sizeof_uint(space_id) +
			mp_sizeof_str(len);
	char *begin = (char *) region_alloc(&fiber()->gc, size);
	if (begin == NULL) {
		diag_set(OutOfMemory, size, "region_alloc", "begin");
		return BOX_ID_NIL;
	}
	char *end = mp_encode_array(begin, 2);
	end = mp_encode_uint(end, space_id);
	end = mp_encode_str(end, name, len);

	/* NOTE: error and missing key cases are indistinguishable */
	box_tuple_t *tuple;
	if (box_index_get(BOX_VINDEX_ID, 2, begin, end, &tuple) != 0)
		return BOX_ID_NIL;
	if (tuple == NULL)
		return BOX_ID_NIL;
	uint32_t result = BOX_ID_NIL;
	(void) tuple_field_u32(tuple, BOX_INDEX_FIELD_ID, &result);
	return result;
}
/** \endcond public */

int
box_process1(struct request *request, box_tuple_t **result)
{
	/* Allow to write to temporary spaces in read-only mode. */
	struct space *space = space_cache_find(request->space_id);
	if (space == NULL)
		return -1;
	if (!space->def->opts.temporary && box_check_writable() != 0)
		return -1;
	return box_process_rw(request, space, result);
}

int
box_select(uint32_t space_id, uint32_t index_id,
	   int iterator, uint32_t offset, uint32_t limit,
	   const char *key, const char *key_end,
	   struct port *port)
{
	(void)key_end;

	rmean_collect(rmean_box, IPROTO_SELECT, 1);

	if (iterator < 0 || iterator >= iterator_type_MAX) {
		diag_set(ClientError, ER_ILLEGAL_PARAMS,
			 "Invalid iterator type");
		diag_log();
		return -1;
	}

	struct space *space = space_cache_find(space_id);
	if (space == NULL)
		return -1;
	if (access_check_space(space, PRIV_R) != 0)
		return -1;
	struct index *index = index_find(space, index_id);
	if (index == NULL)
		return -1;

	enum iterator_type type = (enum iterator_type) iterator;
	uint32_t part_count = key ? mp_decode_array(&key) : 0;
	if (key_validate(index->def, type, key, part_count))
		return -1;

	ERROR_INJECT(ERRINJ_TESTING, {
		diag_set(ClientError, ER_INJECTION, "ERRINJ_TESTING");
		return -1;
	});

	struct txn *txn;
	if (txn_begin_ro_stmt(space, &txn) != 0)
		return -1;

	struct iterator *it = index_create_iterator(index, type,
						    key, part_count);
	if (it == NULL) {
		txn_rollback_stmt();
		return -1;
	}

	int rc = 0;
	uint32_t found = 0;
	struct tuple *tuple;
	port_tuple_create(port);
	while (found < limit) {
		rc = iterator_next(it, &tuple);
		if (rc != 0 || tuple == NULL)
			break;
		if (offset > 0) {
			offset--;
			continue;
		}
		rc = port_tuple_add(port, tuple);
		if (rc != 0)
			break;
		found++;
	}
	iterator_delete(it);

	if (rc != 0) {
		port_destroy(port);
		txn_rollback_stmt();
		return -1;
	}
	txn_commit_ro_stmt(txn);
	return 0;
}

int
box_insert(uint32_t space_id, const char *tuple, const char *tuple_end,
	   box_tuple_t **result)
{
	mp_tuple_assert(tuple, tuple_end);
	struct request request;
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_INSERT;
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
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_REPLACE;
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
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_DELETE;
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
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_UPDATE;
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
	memset(&request, 0, sizeof(request));
	request.type = IPROTO_UPSERT;
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
 * Trigger space truncation by bumping a counter
 * in _truncate space.
 */
static void
space_truncate(struct space *space)
{
	char tuple_buf[32];
	char *tuple_buf_end = tuple_buf;
	tuple_buf_end = mp_encode_array(tuple_buf_end, 2);
	tuple_buf_end = mp_encode_uint(tuple_buf_end, space_id(space));
	tuple_buf_end = mp_encode_uint(tuple_buf_end, 1);
	assert(tuple_buf_end < tuple_buf + sizeof(tuple_buf));

	char ops_buf[128];
	char *ops_buf_end = ops_buf;
	ops_buf_end = mp_encode_array(ops_buf_end, 1);
	ops_buf_end = mp_encode_array(ops_buf_end, 3);
	ops_buf_end = mp_encode_str(ops_buf_end, "+", 1);
	ops_buf_end = mp_encode_uint(ops_buf_end, 1);
	ops_buf_end = mp_encode_uint(ops_buf_end, 1);
	assert(ops_buf_end < ops_buf + sizeof(ops_buf));

	if (box_upsert(BOX_TRUNCATE_ID, 0, tuple_buf, tuple_buf_end,
		       ops_buf, ops_buf_end, 0, NULL) != 0)
		diag_raise();
}

int
box_truncate(uint32_t space_id)
{
	try {
		struct space *space = space_cache_find_xc(space_id);
		space_truncate(space);
		return 0;
	} catch (Exception *exc) {
		return -1;
	}
}

/** Update a record in _sequence_data space. */
static int
sequence_data_update(uint32_t seq_id, int64_t value)
{
	size_t tuple_buf_size = (mp_sizeof_array(2) +
				 2 * mp_sizeof_uint(UINT64_MAX));
	char *tuple_buf = (char *) region_alloc(&fiber()->gc, tuple_buf_size);
	if (tuple_buf == NULL) {
		diag_set(OutOfMemory, tuple_buf_size, "region", "tuple");
		return -1;
	}
	char *tuple_buf_end = tuple_buf;
	tuple_buf_end = mp_encode_array(tuple_buf_end, 2);
	tuple_buf_end = mp_encode_uint(tuple_buf_end, seq_id);
	tuple_buf_end = (value < 0 ?
			 mp_encode_int(tuple_buf_end, value) :
			 mp_encode_uint(tuple_buf_end, value));
	assert(tuple_buf_end < tuple_buf + tuple_buf_size);

	struct credentials *orig_credentials = effective_user();
	fiber_set_user(fiber(), &admin_credentials);

	int rc = box_replace(BOX_SEQUENCE_DATA_ID,
			     tuple_buf, tuple_buf_end, NULL);

	fiber_set_user(fiber(), orig_credentials);
	return rc;
}

/** Delete a record from _sequence_data space. */
static int
sequence_data_delete(uint32_t seq_id)
{
	size_t key_buf_size = mp_sizeof_array(1) + mp_sizeof_uint(UINT64_MAX);
	char *key_buf = (char *) region_alloc(&fiber()->gc, key_buf_size);
	if (key_buf == NULL) {
		diag_set(OutOfMemory, key_buf_size, "region", "key");
		return -1;
	}
	char *key_buf_end = key_buf;
	key_buf_end = mp_encode_array(key_buf_end, 1);
	key_buf_end = mp_encode_uint(key_buf_end, seq_id);
	assert(key_buf_end < key_buf + key_buf_size);

	struct credentials *orig_credentials = effective_user();
	fiber_set_user(fiber(), &admin_credentials);

	int rc = box_delete(BOX_SEQUENCE_DATA_ID, 0,
			    key_buf, key_buf_end, NULL);

	fiber_set_user(fiber(), orig_credentials);
	return rc;
}

int
box_sequence_next(uint32_t seq_id, int64_t *result)
{
	struct sequence *seq = sequence_cache_find(seq_id);
	if (seq == NULL)
		return -1;
	if (access_check_sequence(seq) != 0)
		return -1;
	int64_t value;
	if (sequence_next(seq, &value) != 0)
		return -1;
	if (sequence_data_update(seq_id, value) != 0)
		return -1;
	*result = value;
	return 0;
}
int
box_sequence_set(uint32_t seq_id, int64_t value)
{
	struct sequence *seq = sequence_cache_find(seq_id);
	if (seq == NULL)
		return -1;
	if (access_check_sequence(seq) != 0)
		return -1;
	if (sequence_set(seq, value) != 0)
		return -1;
	return sequence_data_update(seq_id, value);
}

int
box_sequence_reset(uint32_t seq_id)
{
	struct sequence *seq = sequence_cache_find(seq_id);
	if (seq == NULL)
		return -1;
	if (access_check_sequence(seq) != 0)
		return -1;
	sequence_reset(seq);
	return sequence_data_delete(seq_id);
}

static inline void
box_register_replica(uint32_t id, const struct tt_uuid *uuid)
{
	if (boxk(IPROTO_INSERT, BOX_CLUSTER_ID, "[%u%s]",
		 (unsigned) id, tt_uuid_str(uuid)) != 0)
		diag_raise();
	assert(replica_by_uuid(uuid)->id == id);
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
	struct replica *replica = replica_by_uuid(instance_uuid);
	if (replica != NULL && replica->id != REPLICA_ID_NIL)
		return; /* nothing to do - already registered */

	box_check_writable_xc();

	/** Find the largest existing replica id. */
	struct space *space = space_cache_find_xc(BOX_CLUSTER_ID);
	struct index *index = index_find_system_xc(space, 0);
	struct iterator *it = index_create_iterator_xc(index, ITER_ALL,
						       NULL, 0);
	IteratorGuard iter_guard(it);
	struct tuple *tuple;
	/** Assign a new replica id. */
	uint32_t replica_id = 1;
	while ((tuple = iterator_next_xc(it)) != NULL) {
		if (tuple_field_u32_xc(tuple,
				       BOX_CLUSTER_FIELD_ID) != replica_id)
			break;
		replica_id++;
	}
	box_register_replica(replica_id, instance_uuid);
}

void
box_process_auth(struct auth_request *request)
{
	rmean_collect(rmean_box, IPROTO_AUTH, 1);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	const char *user = request->user_name;
	uint32_t len = mp_decode_strl(&user);
	authenticate(user, len, request->scramble);
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
	xrow_decode_join_xc(header, &instance_uuid);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	/* Forbid connection to itself */
	if (tt_uuid_is_equal(&instance_uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Check permissions */
	access_check_universe_xc(PRIV_R);

	/*
	 * Unless already registered, the new replica will be
	 * added to _cluster space once the initial join stage
	 * is complete. Fail early if the caller does not have
	 * appropriate access privileges.
	 */
	struct replica *replica = replica_by_uuid(&instance_uuid);
	if (replica == NULL || replica->id == REPLICA_ID_NIL) {
		box_check_writable_xc();
		struct space *space = space_cache_find_xc(BOX_CLUSTER_ID);
		access_check_space_xc(space, PRIV_W);
	}

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
	if (checkpoint_last(&start_vclock) < 0)
		tnt_raise(ClientError, ER_MISSING_SNAPSHOT);

	/* Register the replica with the garbage collector. */
	struct gc_consumer *gc = gc_consumer_register(
		tt_sprintf("replica %s", tt_uuid_str(&instance_uuid)),
		vclock_sum(&start_vclock));
	if (gc == NULL)
		diag_raise();
	auto gc_guard = make_scoped_guard([=]{
		gc_consumer_unregister(gc);
	});

	/* Respond to JOIN request with start_vclock. */
	struct xrow_header row;
	xrow_encode_vclock_xc(&row, &start_vclock);
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

	replica = replica_by_uuid(&instance_uuid);
	assert(replica != NULL);
	replica->gc = gc;
	gc_guard.is_active = false;

	/* Remember master's vclock after the last request */
	struct vclock stop_vclock;
	wal_checkpoint(&stop_vclock, false);

	/* Send end of initial stage data marker */
	xrow_encode_vclock_xc(&row, &stop_vclock);
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
	xrow_encode_vclock_xc(&row, &current_vclock);
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
	uint32_t replica_version_id;
	vclock_create(&replica_clock);
	xrow_decode_subscribe_xc(header, &replicaset_uuid, &replica_uuid,
				 &replica_clock, &replica_version_id);

	/* Forbid connection to itself */
	if (tt_uuid_is_equal(&replica_uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/* Check permissions */
	access_check_universe_xc(PRIV_R);

	/**
	 * Check that the given UUID matches the UUID of the
	 * replica set this replica belongs to. Used to handshake
	 * replica connect, and refuse a connection from a replica
	 * which belongs to a different replica set.
	 */
	if (!tt_uuid_is_equal(&replicaset_uuid, &REPLICASET_UUID)) {
		tnt_raise(ClientError, ER_REPLICASET_UUID_MISMATCH,
			  tt_uuid_str(&REPLICASET_UUID),
			  tt_uuid_str(&replicaset_uuid));
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
	xrow_encode_vclock_xc(&row, &current_vclock);
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
	relay_subscribe(io->fd, header->sync, replica, &replica_clock,
			replica_version_id);
}

/** Insert a new cluster into _schema */
static void
box_set_replicaset_uuid(const struct tt_uuid *replicaset_uuid)
{
	tt_uuid uu;
	/* Use UUID from the config or generate a new one */
	if (!tt_uuid_is_nil(replicaset_uuid))
		uu = *replicaset_uuid;
	else
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
		sql_free();
		user_cache_free();
		schema_free();
		module_free();
		tuple_free();
		port_free();
#endif
		sequence_free();
		gc_free();
		engine_shutdown();
		wal_thread_stop();
	}

	fiber_cond_destroy(&ro_cond);
}

static void
engine_init()
{
	/*
	 * Sic: order is important here, since
	 * memtx must be the first to participate
	 * in checkpoints (in enigne_foreach order),
	 * so it must be registered first.
	 */
	struct memtx_engine *memtx;
	memtx = memtx_engine_new_xc(cfg_gets("memtx_dir"),
				    cfg_geti("force_recovery"),
				    cfg_getd("memtx_memory"),
				    cfg_geti("memtx_min_tuple_size"),
				    cfg_getd("slab_alloc_factor"));
	engine_register((struct engine *)memtx);
	box_set_memtx_max_tuple_size();

	struct sysview_engine *sysview = sysview_engine_new_xc();
	engine_register((struct engine *)sysview);

	struct vinyl_engine *vinyl;
	vinyl = vinyl_engine_new_xc(cfg_gets("vinyl_dir"),
				    cfg_geti64("vinyl_memory"),
				    cfg_geti("vinyl_read_threads"),
				    cfg_geti("vinyl_write_threads"),
				    cfg_geti("force_recovery"));
	engine_register((struct engine *)vinyl);
	box_set_vinyl_max_tuple_size();
	box_set_vinyl_cache();
	box_set_vinyl_timeout();
}

/**
 * Initialize the first replica of a new replica set.
 */
static void
bootstrap_master(const struct tt_uuid *replicaset_uuid)
{
	engine_bootstrap_xc();

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

	/* Set UUID of a new replica set */
	box_set_replicaset_uuid(replicaset_uuid);
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

	say_info("bootstrapping replica from %s",
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
	engine_begin_initial_recovery_xc(NULL);
	applier_resume_to_state(applier, APPLIER_FINAL_JOIN, TIMEOUT_INFINITY);

	/*
	 * Process final data (WALs).
	 */
	engine_begin_final_recovery_xc();
	struct recovery_journal journal;
	recovery_journal_create(&journal, &replicaset.vclock);
	journal_set(&journal.base);

	applier_resume_to_state(applier, APPLIER_JOINED, TIMEOUT_INFINITY);

	/* Clear the pointer to journal before it goes out of scope */
	journal_set(NULL);

	/* Finalize the new replica */
	engine_end_recovery_xc();

	/* Switch applier to initial state */
	applier_resume_to_state(applier, APPLIER_READY, TIMEOUT_INFINITY);
	assert(applier->state == APPLIER_READY);
}

/**
 * Bootstrap a new instance either as the first master in a
 * replica set or as a replica of an existing master.
 *
 * @param[out] is_bootstrap_leader  set if this instance is
 *                                  the leader of a new cluster
 */
static void
bootstrap(const struct tt_uuid *replicaset_uuid, bool *is_bootstrap_leader)
{
	/* Use the first replica by URI as a bootstrap leader */
	struct replica *master = replicaset_leader();
	assert(master == NULL || master->applier != NULL);

	if (master != NULL && !tt_uuid_is_equal(&master->uuid, &INSTANCE_UUID)) {
		bootstrap_from_master(master);
		/* Check replica set UUID */
		if (!tt_uuid_is_nil(replicaset_uuid) &&
		    !tt_uuid_is_equal(replicaset_uuid, &REPLICASET_UUID)) {
			tnt_raise(ClientError, ER_REPLICASET_UUID_MISMATCH,
				  tt_uuid_str(replicaset_uuid),
				  tt_uuid_str(&REPLICASET_UUID));
		}
	} else {
		bootstrap_master(replicaset_uuid);
		*is_bootstrap_leader = true;
	}
	if (engine_begin_checkpoint() ||
	    engine_commit_checkpoint(&replicaset.vclock))
		panic("failed to create a checkpoint");
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
	fiber_cond_create(&ro_cond);

	user_cache_init();
	/*
	 * The order is important: to initialize sessions,
	 * we need to access the admin user, which is used
	 * as a default session user when running triggers.
	 */
	session_init();

	if (tuple_init(lua_hash) != 0)
		diag_raise();

	sequence_init();
}

bool
box_is_configured(void)
{
	return is_box_configured;
}

static inline void
box_cfg_xc(void)
{
	/* Join the cord interconnect as "tx" endpoint. */
	fiber_pool_create(&tx_fiber_pool, "tx",
			  IPROTO_MSG_MAX_MIN * IPROTO_FIBER_POOL_SIZE_FACTOR,
			  FIBER_POOL_IDLE_TIMEOUT);
	/* Add an extra endpoint for WAL wake up/rollback messages. */
	cbus_endpoint_create(&tx_prio_endpoint, "tx_prio", tx_prio_cb, &tx_prio_endpoint);

	rmean_box = rmean_new(iproto_type_strs, IPROTO_TYPE_STAT_MAX);
	rmean_error = rmean_new(rmean_error_strings, RMEAN_ERROR_LAST);

	gc_init();
	engine_init();
	if (module_init() != 0)
		diag_raise();
	schema_init();
	replication_init();
	port_init();
	iproto_init();
	sql_init();
	wal_thread_start();

	title("loading");

	struct tt_uuid instance_uuid, replicaset_uuid;
	box_check_instance_uuid(&instance_uuid);
	box_check_replicaset_uuid(&replicaset_uuid);

	box_set_net_msg_max();
	box_set_checkpoint_count();
	box_set_too_long_threshold();
	box_set_replication_timeout();
	box_set_replication_connect_timeout();
	box_set_replication_connect_quorum();
	box_set_replication_skip_conflict();
	replication_sync_lag = box_check_replication_sync_lag();
	xstream_create(&join_stream, apply_initial_join_row);
	xstream_create(&subscribe_stream, apply_row);

	struct vclock last_checkpoint_vclock;
	int64_t last_checkpoint_lsn = checkpoint_last(&last_checkpoint_vclock);

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
		if (!cfg_geti("hot_standby") || last_checkpoint_lsn < 0)
			tnt_raise(ClientError, ER_ALREADY_RUNNING, cfg_gets("wal_dir"));
	} else {
		/*
		 * Try to bind the port before recovery, to fail
		 * early if the port is busy. In hot standby mode,
		 * the port is most likely busy.
		 */
		box_bind();
	}
	bool is_bootstrap_leader = false;
	if (last_checkpoint_lsn >= 0) {
		struct wal_stream wal_stream;
		wal_stream_create(&wal_stream, cfg_geti64("rows_per_wal"));

		struct recovery *recovery;
		recovery = recovery_new(cfg_gets("wal_dir"),
					cfg_geti("force_recovery"),
					&last_checkpoint_vclock);
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
		engine_begin_initial_recovery_xc(&recovery->vclock);

		struct memtx_engine *memtx;
		memtx = (struct memtx_engine *)engine_by_name("memtx");
		assert(memtx != NULL);

		struct recovery_journal journal;
		recovery_journal_create(&journal, &recovery->vclock);
		journal_set(&journal.base);

		/**
		 * We explicitly request memtx to recover its
		 * snapshot as a separate phase since it contains
		 * data for system spaces, and triggers on
		 * recovery of system spaces issue DDL events in
		 * other engines.
		 */
		memtx_engine_recover_snapshot_xc(memtx,
				&last_checkpoint_vclock);

		engine_begin_final_recovery_xc();
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
		engine_end_recovery_xc();

		/* Check replica set and instance UUID. */
		if (!tt_uuid_is_nil(&instance_uuid) &&
		    !tt_uuid_is_equal(&instance_uuid, &INSTANCE_UUID)) {
			tnt_raise(ClientError, ER_INSTANCE_UUID_MISMATCH,
				  tt_uuid_str(&instance_uuid),
				  tt_uuid_str(&INSTANCE_UUID));
		}
		if (!tt_uuid_is_nil(&replicaset_uuid) &&
		    !tt_uuid_is_equal(&replicaset_uuid, &REPLICASET_UUID)) {
			tnt_raise(ClientError, ER_REPLICASET_UUID_MISMATCH,
				  tt_uuid_str(&replicaset_uuid),
				  tt_uuid_str(&REPLICASET_UUID));
		}

		/* Clear the pointer to journal before it goes out of scope */
		journal_set(NULL);
		/*
		 * Initialize the replica set vclock from recovery.
		 * The local WAL may contain rows from remote masters,
		 * so we must reflect this in replicaset vclock to
		 * not attempt to apply these rows twice.
		 */
		vclock_copy(&replicaset.vclock, &recovery->vclock);

		/** Begin listening only when the local recovery is complete. */
		box_listen();

		title("orphan");

		/* Wait for the cluster to start up */
		box_sync_replication(replication_connect_timeout, false);
	} else {
		if (!tt_uuid_is_nil(&instance_uuid))
			INSTANCE_UUID = instance_uuid;
		else
			tt_uuid_create(&INSTANCE_UUID);
		/*
		 * Begin listening on the socket to enable
		 * master-master replication leader election.
		 */
		box_listen();

		title("orphan");

		/*
		 * Wait for the cluster to start up.
		 *
		 * Note, when bootstrapping a new instance, we have to
		 * connect to all masters to make sure all replicas
		 * receive the same replica set UUID when a new cluster
		 * is deployed.
		 */
		box_sync_replication(TIMEOUT_INFINITY, true);
		/* Bootstrap a new master */
		bootstrap(&replicaset_uuid, &is_bootstrap_leader);
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
		 &replicaset.vclock, wal_max_rows, wal_max_size);

	rmean_cleanup(rmean_box);

	/*
	 * If this instance is a leader of a newly bootstrapped
	 * cluster, it is uptodate by definition so leave the
	 * 'orphan' mode right away to let it initialize cluster
	 * schema.
	 */
	if (is_bootstrap_leader)
		box_clear_orphan();

	/* Follow replica */
	replicaset_follow();

	sql_load_schema();

	say_info("ready to accept requests");

	fiber_gc();
	is_box_configured = true;

	if (!is_bootstrap_leader)
		replicaset_sync();
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
box_checkpoint()
{
	/* Signal arrived before box.cfg{} */
	if (! is_box_configured)
		return 0;
	int rc = 0;
	if (box_checkpoint_is_in_progress) {
		diag_set(ClientError, ER_CHECKPOINT_IN_PROGRESS);
		return -1;
	}
	box_checkpoint_is_in_progress = true;
	/* create checkpoint files */
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
	else
		gc_run();
	latch_unlock(&schema_lock);
	box_checkpoint_is_in_progress = false;
	return rc;
}

int
box_backup_start(box_backup_cb cb, void *cb_arg)
{
	if (backup_gc != NULL) {
		diag_set(ClientError, ER_BACKUP_IN_PROGRESS);
		return -1;
	}
	struct vclock vclock;
	if (checkpoint_last(&vclock) < 0) {
		diag_set(ClientError, ER_MISSING_SNAPSHOT);
		return -1;
	}
	backup_gc = gc_consumer_register("backup", vclock_sum(&vclock));
	if (backup_gc == NULL)
		return -1;
	int rc = engine_backup(&vclock, cb, cb_arg);
	if (rc != 0) {
		gc_consumer_unregister(backup_gc);
		backup_gc = NULL;
	}
	return rc;
}

void
box_backup_stop(void)
{
	if (backup_gc != NULL) {
		gc_consumer_unregister(backup_gc);
		backup_gc = NULL;
	}
}

const char *
box_status(void)
{
    return status;
}

static int
box_reset_space_stat(struct space *space, void *arg)
{
	(void)arg;
	for (uint32_t i = 0; i < space->index_count; i++)
		index_reset_stat(space->index[i]);
	return 0;
}

void
box_reset_stat(void)
{
	rmean_cleanup(rmean_box);
	rmean_cleanup(rmean_error);
	engine_reset_stat();
	space_foreach(box_reset_space_stat, NULL);
}
