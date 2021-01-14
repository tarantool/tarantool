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
#include "tuple_format.h"
#include "session.h"
#include "schema.h"
#include "engine.h"
#include "memtx_engine.h"
#include "memtx_space.h"
#include "sysview.h"
#include "blackhole.h"
#include "service_engine.h"
#include "vinyl.h"
#include "space.h"
#include "index.h"
#include "port.h"
#include "txn.h"
#include "txn_limbo.h"
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
#include "sql.h"
#include "systemd.h"
#include "call.h"
#include "crash.h"
#include "func.h"
#include "sequence.h"
#include "sql_stmt_cache.h"
#include "msgpack.h"
#include "raft.h"
#include "trivia/util.h"

static char status[64] = "unknown";

/** box.stat rmean */
struct rmean *rmean_box;

struct rlist box_on_shutdown = RLIST_HEAD_INITIALIZER(box_on_shutdown);

static void title(const char *new_status)
{
	snprintf(status, sizeof(status), "%s", new_status);
	title_set_status(new_status);
	title_update();
	systemd_snotify("STATUS=%s", status);
}

const struct vclock *box_vclock = &replicaset.vclock;

/**
 * Set if backup is in progress, i.e. box_backup_start() was
 * called but box_backup_stop() hasn't been yet.
 */
static bool backup_is_in_progress;

/**
 * If backup is in progress, this points to the gc reference
 * object that prevents the garbage collector from deleting
 * the checkpoint files that are currently being backed up.
 */
static struct gc_checkpoint_ref backup_gc;

/**
 * The instance is in read-write mode: the local checkpoint
 * and all write ahead logs are processed. For a replica,
 * it also means we've successfully connected to the master
 * and began receiving updates from it.
 */
static bool is_box_configured = false;
static bool is_ro = true;
static fiber_cond ro_cond;
/** Set to true during recovery from local files. */
static bool is_local_recovery = false;

/**
 * The following flag is set if the instance failed to
 * synchronize to a sufficient number of replicas to form
 * a quorum and so was forced to switch to read-only mode.
 */
static bool is_orphan;

/**
 * Summary flag incorporating all the instance attributes,
 * affecting ability to write. Currently these are:
 * - is_ro;
 * - is_orphan;
 */
static bool is_ro_summary = true;

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

void
box_update_ro_summary(void)
{
	bool old_is_ro_summary = is_ro_summary;
	is_ro_summary = is_ro || is_orphan || raft_is_ro(box_raft()) ||
			txn_limbo_is_ro(&txn_limbo);
	/* In 99% nothing changes. Filter this out first. */
	if (is_ro_summary == old_is_ro_summary)
		return;

	if (is_ro_summary)
		engine_switch_to_ro();
	fiber_cond_broadcast(&ro_cond);
}

static int
box_check_writable(void)
{
	if (is_ro_summary) {
		/*
		 * XXX: return a special error when the node is not a leader to
		 * reroute to the leader node.
		 */
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
	struct tuple *tuple = NULL;
	bool return_tuple = false;
	struct txn *txn = in_txn();
	bool is_autocommit = txn == NULL;
	if (is_autocommit && (txn = txn_begin()) == NULL)
		return -1;
	assert(iproto_type_is_dml(request->type));
	rmean_collect(rmean_box, request->type, 1);
	if (access_check_space(space, PRIV_W) != 0)
		goto rollback;
	if (txn_begin_stmt(txn, space) != 0)
		goto rollback;
	if (space_execute_dml(space, txn, request, &tuple) != 0) {
		txn_rollback_stmt(txn);
		goto rollback;
	}
	if (result != NULL)
		*result = tuple;

	return_tuple = result != NULL && tuple != NULL;
	if (return_tuple) {
		/*
		 * Pin the tuple locally before the commit,
		 * otherwise it may go away during yield in
		 * when WAL is written in autocommit mode.
		 */
		tuple_ref(tuple);
	}

	if (txn_commit_stmt(txn, request))
		goto rollback;

	if (is_autocommit) {
		int res = 0;
		/*
		 * During local recovery the commit procedure
		 * should be async, otherwise the only fiber
		 * processing recovery will get stuck on the first
		 * synchronous tx it meets until confirm timeout
		 * is reached and the tx is rolled back, yielding
		 * an error.
		 * Moreover, txn_commit_async() doesn't hurt at
		 * all during local recovery, since journal_write
		 * is faked at this stage and returns immediately.
		 */
		if (is_local_recovery) {
			res = txn_commit_async(txn);
		} else {
			res = txn_commit(txn);
		}
		if (res < 0)
			goto error;
	        fiber_gc();
	}
	if (return_tuple) {
		tuple_bless(tuple);
		tuple_unref(tuple);
	}
	return 0;

rollback:
	if (is_autocommit) {
		txn_rollback(txn);
		fiber_gc();
	}
error:
	if (return_tuple)
		tuple_unref(tuple);
	return -1;
}

static bool
box_check_ro(void);

void
box_set_ro(void)
{
	is_ro = box_check_ro();
	box_update_ro_summary();
}

bool
box_is_ro(void)
{
	return is_ro_summary;
}

bool
box_is_orphan(void)
{
	return is_orphan;
}

int
box_wait_ro(bool ro, double timeout)
{
	double deadline = ev_monotonic_now(loop()) + timeout;
	while (is_box_configured == false || box_is_ro() != ro) {
		if (fiber_cond_wait_deadline(&ro_cond, deadline) != 0)
			return -1;
	}
	return 0;
}

void
box_do_set_orphan(bool orphan)
{
	is_orphan = orphan;
	box_update_ro_summary();
}

void
box_set_orphan(bool orphan)
{
	box_do_set_orphan(orphan);
	/* Update the title to reflect the new status. */
	if (is_orphan) {
		say_crit("entering orphan mode");
		title("orphan");
	} else {
		say_crit("leaving orphan mode");
		title("running");
	}
}

struct wal_stream {
	struct xstream base;
	/** How many rows have been recovered so far. */
	size_t rows;
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
static int
recovery_journal_write(struct journal *base,
		       struct journal_entry *entry)
{
	struct recovery_journal *journal = (struct recovery_journal *) base;
	entry->res = vclock_sum(journal->vclock);
	/*
	 * Since there're no actual writes, fire a
	 * journal_async_complete callback right away.
	 */
	journal_async_complete(entry);
	return 0;
}

static void
recovery_journal_create(struct vclock *v)
{
	static struct recovery_journal journal;
	journal_create(&journal.base, recovery_journal_write,
		       recovery_journal_write);
	journal.vclock = v;
	journal_set(&journal.base);
}

static void
apply_wal_row(struct xstream *stream, struct xrow_header *row)
{
	struct request request;
	if (iproto_type_is_synchro_request(row->type)) {
		struct synchro_request syn_req;
		if (xrow_decode_synchro(row, &syn_req) != 0)
			diag_raise();
		txn_limbo_process(&txn_limbo, &syn_req);
		return;
	}
	if (iproto_type_is_raft_request(row->type)) {
		struct raft_request raft_req;
		/* Vclock is never persisted in WAL by Raft. */
		if (xrow_decode_raft(row, &raft_req, NULL) != 0)
			diag_raise();
		box_raft_recover(&raft_req);
		return;
	}
	xrow_decode_dml_xc(row, &request, dml_request_key_map(row->type));
	if (request.type != IPROTO_NOP) {
		struct space *space = space_cache_find_xc(request.space_id);
		if (box_process_rw(&request, space, NULL) != 0) {
			say_error("error applying row: %s", request_str(&request));
			diag_raise();
		}
	}
	struct wal_stream *xstream =
		container_of(stream, struct wal_stream, base);
	/**
	 * Yield once in a while, but not too often,
	 * mostly to allow signal handling to take place.
	 */
	if (++xstream->rows % WAL_ROWS_PER_YIELD == 0)
		fiber_sleep(0);
}

static void
wal_stream_create(struct wal_stream *ctx)
{
	xstream_create(&ctx->base, apply_wal_row);
	ctx->rows = 0;
}

/* {{{ configuration bindings */

static void
box_check_say(void)
{
	enum say_logger_type type = SAY_LOGGER_STDERR; /* default */
	const char *log = cfg_gets("log");
	if (log != NULL && say_parse_logger_type(&log, &type) < 0) {
		tnt_raise(ClientError, ER_CFG, "log",
			  diag_last_error(diag_get())->errmsg);
	}
	if (type == SAY_LOGGER_SYSLOG) {
		struct say_syslog_opts opts;
		if (say_parse_syslog_opts(log, &opts) < 0) {
			if (diag_last_error(diag_get())->type ==
			    &type_IllegalParams) {
				tnt_raise(ClientError, ER_CFG, "log",
					  diag_last_error(diag_get())->errmsg);
			}
			diag_raise();
		}
		say_free_syslog_opts(&opts);
	}

	const char *log_format = cfg_gets("log_format");
	enum say_format format = say_format_by_name(log_format);
	if (format == say_format_MAX)
		tnt_raise(ClientError, ER_CFG, "log_format",
			 "expected 'plain' or 'json'");
	if (type == SAY_LOGGER_SYSLOG && format == SF_JSON) {
		tnt_raise(ClientError, ER_CFG, "log_format",
			  "'json' can't be used with syslog logger");
	}
	int log_nonblock = cfg_getb("log_nonblock");
	if (log_nonblock == 1 &&
	    (type == SAY_LOGGER_FILE || type == SAY_LOGGER_STDERR)) {
		tnt_raise(ClientError, ER_CFG, "log_nonblock",
			  "the option is incompatible with file/stderr logger");
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

static const char *
box_check_election_mode(void)
{
	const char *mode = cfg_gets("election_mode");
	if (mode == NULL || (strcmp(mode, "off") != 0 &&
	    strcmp(mode, "voter") != 0 && strcmp(mode, "candidate") != 0)) {
		diag_set(ClientError, ER_CFG, "election_mode", "the value must "
			 "be a string 'off' or 'voter' or 'candidate'");
		return NULL;
	}
	return mode;
}

static double
box_check_election_timeout(void)
{
	double d = cfg_getd("election_timeout");
	if (d <= 0) {
		diag_set(ClientError, ER_CFG, "election_timeout",
			 "the value must be a positive number");
		return -1;
	}
	return d;
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

/**
 * Evaluate replication syncro quorum number from a formula.
 */
static int
box_eval_replication_synchro_quorum(int nr_replicas)
{
	assert(nr_replicas > 0 && nr_replicas < VCLOCK_MAX);

	const char fmt[] =
		"local expr = [[%s]]\n"
		"local f, err = loadstring('return ('..expr..')')\n"
		"if not f then "
			"error(string.format('Failed to load \%\%s:"
			"\%\%s', expr, err)) "
		"end\n"
		"setfenv(f, {N = %d, math = {"
			"ceil = math.ceil,"
			"floor = math.floor,"
			"abs = math.abs,"
			"random = math.random,"
			"min = math.min,"
			"max = math.max,"
			"sqrt = math.sqrt,"
			"fmod = math.fmod,"
		"}})\n"
		"local res = f()\n"
		"if type(res) ~= 'number' then\n"
			"error('Expression should return a number')\n"
		"end\n"
		"return math.floor(res)\n";
	const char *expr = cfg_gets("replication_synchro_quorum");

	/*
	 * cfg_gets uses static buffer as well so we need a local
	 * one, 1K should be enough to carry arbitrary but sane
	 * formula.
	 */
	char buf[1024];
	int len = snprintf(buf, sizeof(buf), fmt, expr,
			   nr_replicas);
	if (len >= (int)sizeof(buf)) {
		diag_set(ClientError, ER_CFG,
			 "replication_synchro_quorum",
			 "the formula is too big");
		return -1;
	}

	luaL_loadstring(tarantool_L, buf);
	if (lua_pcall(tarantool_L, 0, 1, 0) != 0) {
		diag_set(ClientError, ER_CFG,
			 "replication_synchro_quorum",
			 lua_tostring(tarantool_L, -1));
		return -1;
	}

	int64_t quorum = -1;
	if (lua_isnumber(tarantool_L, -1))
		quorum = luaL_toint64(tarantool_L, -1);
	lua_pop(tarantool_L, 1);

	/*
	 * At least we should have 1 node to sync, the weird
	 * formulas such as N-2 do not guarantee quorums thus
	 * return an error.
	 */
	if (quorum <= 0 || quorum >= VCLOCK_MAX) {
		const char *msg =
			tt_sprintf("the formula is evaluated "
				   "to the quorum %lld for replica "
				   "number %d, which is out of range "
				   "[%d;%d]", (long long)quorum,
				   nr_replicas, 1, VCLOCK_MAX - 1);
		diag_set(ClientError, ER_CFG,
			 "replication_synchro_quorum", msg);
		return -1;
	}

	return quorum;
}

static int
box_check_replication_synchro_quorum(void)
{
	if (!cfg_isnumber("replication_synchro_quorum")) {
		/*
		 * The formula uses symbolic name 'N' as
		 * a number of currently registered replicas.
		 *
		 * When we're in "checking" mode we should walk
		 * over all possible number of replicas to make
		 * sure the formula is correct.
		 *
		 * Note that currently VCLOCK_MAX is pretty small
		 * value but if we gonna increase this limit make
		 * sure that the cycle won't take too much time.
		 */
		for (int i = 1; i < VCLOCK_MAX; i++) {
			if (box_eval_replication_synchro_quorum(i) < 0)
				return -1;
		}
		return 0;
	}

	int64_t quorum = cfg_geti64("replication_synchro_quorum");
	if (quorum <= 0 || quorum >= VCLOCK_MAX) {
		diag_set(ClientError, ER_CFG, "replication_synchro_quorum",
			 "the value must be greater than zero and less than "
			 "maximal number of replicas");
		return -1;
	}
	return 0;
}

static double
box_check_replication_synchro_timeout(void)
{
	double timeout = cfg_getd("replication_synchro_timeout");
	if (timeout <= 0) {
		diag_set(ClientError, ER_CFG, "replication_synchro_timeout",
			 "the value must be greater than zero");
		return -1;
	}
	return timeout;
}

static double
box_check_replication_sync_timeout(void)
{
	double timeout = cfg_getd("replication_sync_timeout");
	if (timeout <= 0) {
		tnt_raise(ClientError, ER_CFG, "replication_sync_timeout",
			  "the value must be greater than 0");
	}
	return timeout;
}

static inline void
box_check_uuid(struct tt_uuid *uuid, const char *name)
{
	*uuid = uuid_nil;
	const char *uuid_str = cfg_gets(name);
	if (uuid_str == NULL)
		return;
	if (tt_uuid_from_string(uuid_str, uuid) != 0)
		tnt_raise(ClientError, ER_CFG, name, uuid_str);
	if (tt_uuid_is_nil(uuid)) {
		tnt_raise(ClientError, ER_CFG, name,
			  tt_sprintf("nil UUID is reserved"));
	}
}

static bool
box_check_ro(void)
{
	bool ro = cfg_geti("read_only") != 0;
	bool anon = cfg_geti("replication_anon") != 0;
	if (anon && !ro) {
		tnt_raise(ClientError, ER_CFG, "read_only",
			  "the value may be set to false only when "
			  "replication_anon is false");
	}
	return ro;
}

static bool
box_check_replication_anon(void)
{
	bool anon = cfg_geti("replication_anon") != 0;
	bool ro = cfg_geti("read_only") != 0;
	if (anon && !ro) {
		tnt_raise(ClientError, ER_CFG, "replication_anon",
			  "the value may be set to true only when "
			  "the instance is read-only");
	}
	return anon;
}

static void
box_check_instance_uuid(struct tt_uuid *uuid)
{
	box_check_uuid(uuid, "instance_uuid");
}

static void
box_check_replicaset_uuid(struct tt_uuid *uuid)
{
	box_check_uuid(uuid, "replicaset_uuid");
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
box_check_wal_max_size(int64_t wal_max_size)
{
	/* check wal_max_bytes configuration */
	if (wal_max_size <= 1) {
		tnt_raise(ClientError, ER_CFG, "wal_max_size",
			  "the value must be greater than one");
	}
	return wal_max_size;
}

static ssize_t
box_check_memory_quota(const char *quota_name)
{
	int64_t size = cfg_geti64(quota_name);
	if (size >= 0 && (size_t) size <= QUOTA_MAX)
		return size;
	diag_set(ClientError, ER_CFG, quota_name,
		 tt_sprintf("must be >= 0 and <= %zu, but it is %lld",
		 QUOTA_MAX, size));
	return -1;
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

	if (box_check_memory_quota("vinyl_memory") < 0)
		diag_raise();

	if (read_threads < 1) {
		tnt_raise(ClientError, ER_CFG, "vinyl_read_threads",
			  "must be greater than or equal to 1");
	}
	if (write_threads < 2) {
		tnt_raise(ClientError, ER_CFG, "vinyl_write_threads",
			  "must be greater than or equal to 2");
	}
	if (page_size <= 0 || (range_size > 0 && page_size > range_size)) {
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

static int
box_check_sql_cache_size(int size)
{
	if (size < 0) {
		diag_set(ClientError, ER_CFG, "sql_cache_size",
			 "must be non-negative");
		return -1;
	}
	return 0;
}

static void
box_check_small_alloc_options(void)
{
	/*
	 * If we use the int type, we may get an incorrect
	 * result if the user enters a large value.
	 */
	int64_t granularity = cfg_geti64("granularity");
	/*
	 * Granularity must be exponent of two and >= 4.
	 * We can use granularity value == 4 only because we used small
	 * memory allocator only for struct tuple, which doesn't require
	 * aligment. Also added an upper bound for granularity, since if
	 * the user enters too large value, he will get incomprehensible
	 * errors later.
	 */
	if (granularity < 4 || granularity > 1024 * 16 ||
	    ! is_exp_of_two(granularity))
		tnt_raise(ClientError, ER_CFG, "granularity",
			  "must be greater than or equal to 4,"
			  " less than or equal"
			  " to 1024 * 16 and exponent of two");
}

void
box_check_config(void)
{
	struct tt_uuid uuid;
	box_check_say();
	box_check_uri(cfg_gets("listen"), "listen");
	box_check_instance_uuid(&uuid);
	box_check_replicaset_uuid(&uuid);
	if (box_check_election_mode() == NULL)
		diag_raise();
	if (box_check_election_timeout() < 0)
		diag_raise();
	box_check_replication();
	box_check_replication_timeout();
	box_check_replication_connect_timeout();
	box_check_replication_connect_quorum();
	box_check_replication_sync_lag();
	if (box_check_replication_synchro_quorum() != 0)
		diag_raise();
	if (box_check_replication_synchro_timeout() < 0)
		diag_raise();
	box_check_replication_sync_timeout();
	box_check_readahead(cfg_geti("readahead"));
	box_check_checkpoint_count(cfg_geti("checkpoint_count"));
	box_check_wal_max_size(cfg_geti64("wal_max_size"));
	box_check_wal_mode(cfg_gets("wal_mode"));
	if (box_check_memory_quota("memtx_memory") < 0)
		diag_raise();
	box_check_memtx_min_tuple_size(cfg_geti64("memtx_min_tuple_size"));
	box_check_small_alloc_options();
	box_check_vinyl_options();
	if (box_check_sql_cache_size(cfg_geti("sql_cache_size")) != 0)
		diag_raise();
}

int
box_set_election_mode(void)
{
	const char *mode = box_check_election_mode();
	if (mode == NULL)
		return -1;
	raft_cfg_is_candidate(box_raft(), strcmp(mode, "candidate") == 0);
	raft_cfg_is_enabled(box_raft(), strcmp(mode, "off") != 0);
	return 0;
}

int
box_set_election_timeout(void)
{
	double d = box_check_election_timeout();
	if (d < 0)
		return -1;
	raft_cfg_election_timeout(box_raft(), d);
	return 0;
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
		struct applier *applier = applier_new(source);
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
box_sync_replication(bool connect_quorum)
{
	int count = 0;
	struct applier **appliers = cfg_get_replication(&count);
	if (appliers == NULL)
		diag_raise();

	auto guard = make_scoped_guard([=]{
		for (int i = 0; i < count; i++)
			applier_delete(appliers[i]); /* doesn't affect diag */
	});

	replicaset_connect(appliers, count, connect_quorum);

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
	/*
	 * Try to connect to all replicas within the timeout period.
	 * Stay in orphan mode in case we fail to connect to at least
	 * 'replication_connect_quorum' remote instances.
	 */
	box_sync_replication(false);
	/* Follow replica */
	replicaset_follow();
	/* Wait until appliers are in sync */
	replicaset_sync();
}

void
box_set_replication_timeout(void)
{
	replication_timeout = box_check_replication_timeout();
	raft_cfg_death_timeout(box_raft(), replication_disconnect_timeout());
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
box_set_replication_sync_lag(void)
{
	replication_sync_lag = box_check_replication_sync_lag();
}

void
box_update_replication_synchro_quorum(void)
{
	int quorum = -1;

	if (!cfg_isnumber("replication_synchro_quorum")) {
		/*
		 * The formula has been verified already. For bootstrap
		 * stage pass 1 as a number of replicas to sync because
		 * we're at early stage and registering a new replica.
		 *
		 * This should cover the valid case where formula is plain
		 * "N", ie all replicas are to be synchro mode.
		 */
		int value = MAX(1, replicaset.registered_count);
		quorum = box_eval_replication_synchro_quorum(value);
		say_info("update replication_synchro_quorum = %d", quorum);
	} else {
		quorum = cfg_geti("replication_synchro_quorum");
	}

	/*
	 * This should never happen because the values were
	 * validated already but just to prevent from
	 * unexpected changes and because the value is too
	 * important for qsync, lets re-check (this is cheap).
	 */
	if (quorum <= 0 || quorum >= VCLOCK_MAX)
		panic("failed to eval/fetch replication_synchro_quorum");

	replication_synchro_quorum = quorum;
	txn_limbo_on_parameters_change(&txn_limbo);
	box_raft_update_election_quorum();
}

int
box_set_replication_synchro_quorum(void)
{
	if (box_check_replication_synchro_quorum() != 0)
		return -1;
	box_update_replication_synchro_quorum();
	return 0;
}

int
box_set_replication_synchro_timeout(void)
{
	double value = box_check_replication_synchro_timeout();
	if (value < 0)
		return -1;
	replication_synchro_timeout = value;
	txn_limbo_on_parameters_change(&txn_limbo);
	return 0;
}

void
box_set_replication_sync_timeout(void)
{
	replication_sync_timeout = box_check_replication_sync_timeout();
}

void
box_set_replication_skip_conflict(void)
{
	replication_skip_conflict = cfg_geti("replication_skip_conflict");
}

void
box_set_replication_anon(void)
{
	bool anon = box_check_replication_anon();
	if (anon == replication_anon)
		return;

	if (!anon) {
		auto guard = make_scoped_guard([&]{
			replication_anon = !anon;
		});
		/* Turn anonymous instance into a normal one. */
		replication_anon = anon;
		/*
		 * Reset all appliers. This will interrupt
		 * anonymous follow they're in so that one of
		 * them can register and others resend a
		 * non-anonymous subscribe.
		 */
		box_sync_replication(false);
		/*
		 * Wait until the master has registered this
		 * instance.
		 */
		struct replica *master = replicaset_leader();
		if (master == NULL || master->applier == NULL ||
		    master->applier->state != APPLIER_CONNECTED) {
			tnt_raise(ClientError, ER_CANNOT_REGISTER);
		}
		struct applier *master_applier = master->applier;

		applier_resume_to_state(master_applier, APPLIER_REGISTERED,
					TIMEOUT_INFINITY);
		applier_resume_to_state(master_applier, APPLIER_READY,
					TIMEOUT_INFINITY);
		/**
		 * Resume other appliers to
		 * resend non-anonymous subscribe.
		 */
		replicaset_follow();
		replicaset_sync();
		guard.is_active = false;
	} else if (!is_box_configured) {
		replication_anon = anon;
	} else {
		/*
		 * It is forbidden to turn a normal replica into
		 * an anonymous one.
		 */
		tnt_raise(ClientError, ER_CFG, "replication_anon",
			  "cannot be turned on after bootstrap"
			  " has finished");
	}

}

/** Trigger to catch ACKs from all nodes when need to wait for quorum. */
struct box_quorum_trigger {
	/** Inherit trigger. */
	struct trigger base;
	/** Minimal number of nodes who should confirm the target LSN. */
	int quorum;
	/** Target LSN to wait for. */
	int64_t target_lsn;
	/** Replica ID whose LSN is being waited. */
	uint32_t replica_id;
	/**
	 * All versions of the given replica's LSN as seen by other nodes. The
	 * same as in the txn limbo.
	 */
	struct vclock vclock;
	/** Number of nodes who confirmed the LSN. */
	int ack_count;
	/** Fiber to wakeup when quorum is reached. */
	struct fiber *waiter;
};

static int
box_quorum_on_ack_f(struct trigger *trigger, void *event)
{
	struct replication_ack *ack = (struct replication_ack *)event;
	struct box_quorum_trigger *t = (struct box_quorum_trigger *)trigger;
	int64_t new_lsn = vclock_get(ack->vclock, t->replica_id);
	int64_t old_lsn = vclock_get(&t->vclock, ack->source);
	if (new_lsn < t->target_lsn || old_lsn >= t->target_lsn)
		return 0;

	vclock_follow(&t->vclock, ack->source, new_lsn);
	++t->ack_count;
	if (t->ack_count >= t->quorum) {
		fiber_wakeup(t->waiter);
		trigger_clear(trigger);
	}
	return 0;
}

/**
 * Wait until at least @a quorum of nodes confirm @a target_lsn from the node
 * with id @a lead_id.
 */
static int
box_wait_quorum(uint32_t lead_id, int64_t target_lsn, int quorum,
		double timeout)
{
	struct box_quorum_trigger t;
	memset(&t, 0, sizeof(t));
	vclock_create(&t.vclock);

	/* Take this node into account immediately. */
	int ack_count = vclock_get(box_vclock, lead_id) >= target_lsn;
	replicaset_foreach(replica) {
		if (relay_get_state(replica->relay) != RELAY_FOLLOW ||
		    replica->anon)
			continue;

		assert(replica->id != REPLICA_ID_NIL);
		assert(!tt_uuid_is_equal(&INSTANCE_UUID, &replica->uuid));

		int64_t lsn = vclock_get(relay_vclock(replica->relay), lead_id);
		vclock_follow(&t.vclock, replica->id, lsn);
		if (lsn >= target_lsn) {
			ack_count++;
			continue;
		}
	}
	if (ack_count < quorum) {
		t.quorum = quorum;
		t.target_lsn = target_lsn;
		t.replica_id = lead_id;
		t.ack_count = ack_count;
		t.waiter = fiber();
		trigger_create(&t.base, box_quorum_on_ack_f, NULL, NULL);
		trigger_add(&replicaset.on_ack, &t.base);
		fiber_sleep(timeout);
		trigger_clear(&t.base);
		ack_count = t.ack_count;
	}
	/*
	 * No point to proceed after cancellation even if got the quorum. The
	 * quorum is waited by limbo clear function. Emptying the limbo involves
	 * a pair of blocking WAL writes, making the fiber sleep even longer,
	 * which isn't appropriate when it's canceled.
	 */
	if (fiber_is_cancelled()) {
		diag_set(ClientError, ER_QUORUM_WAIT, quorum,
			 "fiber is canceled");
		return -1;
	}
	if (ack_count < quorum) {
		diag_set(ClientError, ER_QUORUM_WAIT, quorum, tt_sprintf(
			 "timeout after %.2lf seconds, collected %d acks",
			 timeout, ack_count));
		return -1;
	}
	return 0;
}

int
box_clear_synchro_queue(bool try_wait)
{
	/* A guard to block multiple simultaneous function invocations. */
	static bool in_clear_synchro_queue = false;
	if (in_clear_synchro_queue) {
		diag_set(ClientError, ER_UNSUPPORTED, "clear_synchro_queue",
			 "simultaneous invocations");
		return -1;
	}
	/*
	 * XXX: we may want to write confirm + rollback even when the limbo is
	 * empty for the sake of limbo ownership transition.
	 */
	if (!is_box_configured || txn_limbo_is_empty(&txn_limbo))
		return 0;
	uint32_t former_leader_id = txn_limbo.owner_id;
	assert(former_leader_id != REPLICA_ID_NIL);
	if (former_leader_id == instance_id)
		return 0;

	in_clear_synchro_queue = true;

	if (try_wait) {
		/* Wait until pending confirmations/rollbacks reach us. */
		double timeout = 2 * replication_synchro_timeout;
		double start_tm = fiber_clock();
		while (!txn_limbo_is_empty(&txn_limbo)) {
			if (fiber_clock() - start_tm > timeout)
				break;
			fiber_sleep(0.001);
		}
		/*
		 * Our mission was to clear the limbo from former leader's
		 * transactions. Exit in case someone did that for us.
		 */
		if (txn_limbo_is_empty(&txn_limbo) ||
		    former_leader_id != txn_limbo.owner_id) {
			in_clear_synchro_queue = false;
			return 0;
		}
	}

	/*
	 * clear_synchro_queue() is a no-op on the limbo owner, so all the rows
	 * in the limbo must've come through the applier meaning they already
	 * have an lsn assigned, even if their WAL write hasn't finished yet.
	 */
	int64_t wait_lsn = txn_limbo_last_synchro_entry(&txn_limbo)->lsn;
	assert(wait_lsn > 0);

	int quorum = replication_synchro_quorum;
	int rc = box_wait_quorum(former_leader_id, wait_lsn, quorum,
				 replication_synchro_timeout);
	if (rc == 0) {
		if (quorum < replication_synchro_quorum) {
			diag_set(ClientError, ER_QUORUM_WAIT, quorum,
				 "quorum was increased while waiting");
			rc = -1;
		} else if (wait_lsn < txn_limbo_last_synchro_entry(&txn_limbo)->lsn) {
			diag_set(ClientError, ER_QUORUM_WAIT, quorum,
				 "new synchronous transactions appeared");
			rc = -1;
		} else {
			txn_limbo_force_empty(&txn_limbo, wait_lsn);
			assert(txn_limbo_is_empty(&txn_limbo));
		}
	}
	in_clear_synchro_queue = false;
	return rc;
}

void
box_listen(void)
{
	const char *uri = cfg_gets("listen");
	box_check_uri(uri, "listen");
	iproto_listen(uri);
}

void
box_set_log_level(void)
{
	say_set_log_level(cfg_geti("log_level"));
}

void
box_set_log_format(void)
{
	box_check_say();
	enum say_format format = say_format_by_name(cfg_gets("log_format"));
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
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_snap_io_rate_limit(vinyl,
			cfg_getd("snap_io_rate_limit"));
}

void
box_set_memtx_memory(void)
{
	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");
	assert(memtx != NULL);
	ssize_t size = box_check_memory_quota("memtx_memory");
	if (size < 0)
		diag_raise();
	memtx_engine_set_memory_xc(memtx, size);
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

	struct engine *vinyl = engine_by_name("vinyl");
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
	gc_set_min_checkpoint_count(checkpoint_count);
}

void
box_set_checkpoint_interval(void)
{
	double interval = cfg_getd("checkpoint_interval");
	gc_set_checkpoint_interval(interval);
}

void
box_set_checkpoint_wal_threshold(void)
{
	int64_t threshold = cfg_geti64("checkpoint_wal_threshold");
	wal_set_checkpoint_threshold(threshold);
}

void
box_set_vinyl_memory(void)
{
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	ssize_t size = box_check_memory_quota("vinyl_memory");
	if (size < 0)
		diag_raise();
	vinyl_engine_set_memory_xc(vinyl, size);
}

void
box_set_vinyl_max_tuple_size(void)
{
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_max_tuple_size(vinyl,
			cfg_geti("vinyl_max_tuple_size"));
}

void
box_set_vinyl_cache(void)
{
	struct engine *vinyl = engine_by_name("vinyl");
	assert(vinyl != NULL);
	vinyl_engine_set_cache(vinyl, cfg_geti64("vinyl_cache"));
}

void
box_set_vinyl_timeout(void)
{
	struct engine *vinyl = engine_by_name("vinyl");
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

int
box_set_prepared_stmt_cache_size(void)
{
	int cache_sz = cfg_geti("sql_cache_size");
	if (box_check_sql_cache_size(cache_sz) != 0)
		return -1;
	if (sql_stmt_cache_set_size(cache_sz) != 0)
		return -1;
	return 0;
}

int
box_set_crash(void)
{
	const char *host = cfg_gets("feedback_host");
	bool is_enabled_1 = cfg_getb("feedback_enabled");
	bool is_enabled_2 = cfg_getb("feedback_crashinfo");

	if (host != NULL && strlen(host) >= CRASH_FEEDBACK_HOST_MAX) {
		diag_set(ClientError, ER_CFG, "feedback_host",
			  "the address is too long");
		return -1;
	}

	crash_cfg(host, is_enabled_1 && is_enabled_2);
	return 0;
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

API_EXPORT int
box_return_tuple(box_function_ctx_t *ctx, box_tuple_t *tuple)
{
	return port_c_add_tuple(ctx->port, tuple);
}

API_EXPORT int
box_return_mp(box_function_ctx_t *ctx, const char *mp, const char *mp_end)
{
	return port_c_add_mp(ctx->port, mp, mp_end);
}

/* schema_find_id()-like method using only public API */
API_EXPORT uint32_t
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

API_EXPORT uint32_t
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
	if (!space_is_temporary(space) &&
	    space_group_id(space) != GROUP_LOCAL &&
	    box_check_writable() != 0)
		return -1;
	if (space_is_memtx(space)) {
		/*
		 * Due to on_init_schema triggers set on system spaces,
		 * we can insert data during recovery to local and temporary
		 * spaces. However, until recovery is finished, we can't
		 * check key uniqueness (since indexes are still not yet built).
		 * So reject any attempts to write into these spaces.
		 */
		if (memtx_space_is_recovering(space)) {
			diag_set(ClientError, ER_UNSUPPORTED, "Snapshot recovery",
				"write requests, use "
				"box.ctl.is_recovery_finished() "
				"to check that snapshot recovery was completed");
			diag_log();
			return -1;
		}
	}

	return box_process_rw(request, space, result);
}

API_EXPORT int
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
	struct txn_ro_savepoint svp;
	if (txn_begin_ro_stmt(space, &txn, &svp) != 0)
		return -1;

	struct iterator *it = index_create_iterator(index, type,
						    key, part_count);
	if (it == NULL) {
		txn_rollback_stmt(txn);
		return -1;
	}

	int rc = 0;
	uint32_t found = 0;
	struct tuple *tuple;
	port_c_create(port);
	while (found < limit) {
		rc = iterator_next(it, &tuple);
		if (rc != 0 || tuple == NULL)
			break;
		if (offset > 0) {
			offset--;
			continue;
		}
		rc = port_c_add_tuple(port, tuple);
		if (rc != 0)
			break;
		found++;
	}
	iterator_delete(it);

	if (rc != 0) {
		port_destroy(port);
		txn_rollback_stmt(txn);
		return -1;
	}
	txn_commit_ro_stmt(txn, &svp);
	return 0;
}

API_EXPORT int
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

API_EXPORT int
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

API_EXPORT int
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

API_EXPORT int
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

API_EXPORT int
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
	size_t buf_size = 3 * mp_sizeof_array(UINT32_MAX) +
			  4 * mp_sizeof_uint(UINT64_MAX) + mp_sizeof_str(1);
	char *buf = (char *)region_alloc_xc(&fiber()->gc, buf_size);

	char *tuple_buf = buf;
	char *tuple_buf_end = tuple_buf;
	tuple_buf_end = mp_encode_array(tuple_buf_end, 2);
	tuple_buf_end = mp_encode_uint(tuple_buf_end, space_id(space));
	tuple_buf_end = mp_encode_uint(tuple_buf_end, 1);
	assert(tuple_buf_end < buf + buf_size);

	char *ops_buf = tuple_buf_end;
	char *ops_buf_end = ops_buf;
	ops_buf_end = mp_encode_array(ops_buf_end, 1);
	ops_buf_end = mp_encode_array(ops_buf_end, 3);
	ops_buf_end = mp_encode_str(ops_buf_end, "+", 1);
	ops_buf_end = mp_encode_uint(ops_buf_end, 1);
	ops_buf_end = mp_encode_uint(ops_buf_end, 1);
	assert(ops_buf_end < buf + buf_size);

	if (box_upsert(BOX_TRUNCATE_ID, 0, tuple_buf, tuple_buf_end,
		       ops_buf, ops_buf_end, 0, NULL) != 0)
		diag_raise();
}

API_EXPORT int
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

API_EXPORT int
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

API_EXPORT int
box_sequence_current(uint32_t seq_id, int64_t *result)
{
	struct sequence *seq = sequence_cache_find(seq_id);
	if (seq == NULL)
		return -1;
	if (access_check_sequence(seq) != 0)
		return -1;
	if (sequence_get_value(seq, result) != 0)
		return -1;
	return 0;
}

API_EXPORT int
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

API_EXPORT int
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

API_EXPORT int
box_session_push(const char *data, const char *data_end)
{
	struct session *session = current_session();
	if (session == NULL)
		return -1;
	struct port_msgpack port;
	struct port *base = (struct port *)&port;
	port_msgpack_create(base, data, data_end - data);
	int rc = session_push(session, base);
	port_msgpack_destroy(base);
	return rc;
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
box_process_auth(struct auth_request *request, const char *salt)
{
	rmean_collect(rmean_box, IPROTO_AUTH, 1);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	const char *user = request->user_name;
	uint32_t len = mp_decode_strl(&user);
	authenticate(user, len, salt, request->scramble);
}

void
box_process_fetch_snapshot(struct ev_io *io, struct xrow_header *header)
{
	assert(header->type == IPROTO_FETCH_SNAPSHOT);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	/* Check permissions */
	access_check_universe_xc(PRIV_R);

	/* Forbid replication with disabled WAL */
	if (wal_mode() == WAL_NONE) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Replication",
			  "wal_mode = 'none'");
	}

	say_info("sending current read-view to replica at %s", sio_socketname(io->fd));

	/* Send the snapshot data to the instance. */
	struct vclock start_vclock;
	relay_initial_join(io->fd, header->sync, &start_vclock);
	say_info("read-view sent.");

	/* Remember master's vclock after the last request */
	struct vclock stop_vclock;
	vclock_copy(&stop_vclock, &replicaset.vclock);

	/* Send end of snapshot data marker */
	struct xrow_header row;
	xrow_encode_vclock_xc(&row, &stop_vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);
}

void
box_process_register(struct ev_io *io, struct xrow_header *header)
{
	assert(header->type == IPROTO_REGISTER);

	struct tt_uuid instance_uuid = uuid_nil;
	struct vclock vclock;
	xrow_decode_register_xc(header, &instance_uuid, &vclock);

	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	if (tt_uuid_is_equal(&instance_uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	access_check_universe_xc(PRIV_R);
	/* We only get register requests from anonymous instances. */
	struct replica *replica = replica_by_uuid(&instance_uuid);
	if (replica && replica->id != REPLICA_ID_NIL) {
		tnt_raise(ClientError, ER_REPLICA_NOT_ANON,
			  tt_uuid_str(&instance_uuid));
	}
	/* See box_process_join() */
	box_check_writable_xc();
	struct space *space = space_cache_find_xc(BOX_CLUSTER_ID);
	access_check_space_xc(space, PRIV_W);

	/* Forbid replication with disabled WAL */
	if (wal_mode() == WAL_NONE) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Replication",
			  "wal_mode = 'none'");
	}

	struct gc_consumer *gc = gc_consumer_register(&replicaset.vclock,
				"replica %s", tt_uuid_str(&instance_uuid));
	if (gc == NULL)
		diag_raise();
	auto gc_guard = make_scoped_guard([&] { gc_consumer_unregister(gc); });

	say_info("registering replica %s at %s",
		 tt_uuid_str(&instance_uuid), sio_socketname(io->fd));

	/* See box_process_join() */
	int64_t limbo_rollback_count = txn_limbo.rollback_count;
	struct vclock start_vclock;
	vclock_copy(&start_vclock, &replicaset.vclock);

	/**
	 * Call the server-side hook which stores the replica uuid
	 * in _cluster space.
	 */
	box_on_join(&instance_uuid);

	ERROR_INJECT_YIELD(ERRINJ_REPLICA_JOIN_DELAY);

	/* Remember master's vclock after the last request */
	struct vclock stop_vclock;
	vclock_copy(&stop_vclock, &replicaset.vclock);

	if (txn_limbo.rollback_count != limbo_rollback_count)
		tnt_raise(ClientError, ER_SYNC_ROLLBACK);

	if (txn_limbo_wait_confirm(&txn_limbo) != 0)
		diag_raise();

	/*
	 * Feed replica with WALs in range
	 * (start_vclock, stop_vclock) so that it gets its
	 * registration.
	 */
	relay_final_join(io->fd, header->sync, &start_vclock, &stop_vclock);
	say_info("final data sent.");

	struct xrow_header row;
	/* Send end of WAL stream marker */
	xrow_encode_vclock_xc(&row, &replicaset.vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	/*
	 * Advance the WAL consumer state to the position where
	 * registration was complete and assign it to the
	 * replica.
	 */
	gc_consumer_advance(gc, &stop_vclock);
	replica = replica_by_uuid(&instance_uuid);
	if (replica->gc != NULL)
		gc_consumer_unregister(replica->gc);
	replica->gc = gc;
	gc_guard.is_active = false;
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
	 *     - start_vclock - master's vclock at the time of join.
	 *
	 * <= INSERT
	 *    ...
	 *    Initial data: a stream of engine-specifc rows, e.g. snapshot
	 *    rows for memtx or dirty cursor data for Vinyl fed from a
	 *    read-view. Engine can use REPLICA_ID, LSN and other fields
	 *    for internal purposes.
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

	/*
	 * Register the replica as a WAL consumer so that
	 * it can resume FINAL JOIN where INITIAL JOIN ends.
	 */
	struct gc_consumer *gc = gc_consumer_register(&replicaset.vclock,
				"replica %s", tt_uuid_str(&instance_uuid));
	if (gc == NULL)
		diag_raise();
	auto gc_guard = make_scoped_guard([&] { gc_consumer_unregister(gc); });

	say_info("joining replica %s at %s",
		 tt_uuid_str(&instance_uuid), sio_socketname(io->fd));

	/*
	 * In order to join a replica, master has to make sure it
	 * doesn't send unconfirmed data. We have to check that
	 * there are no rolled back transactions between
	 * start_vclock and stop_vclock, and that the data right
	 * before stop_vclock is confirmed, before we can proceed
	 * to final join.
	 */
	int64_t limbo_rollback_count = txn_limbo.rollback_count;
	/*
	 * Initial stream: feed replica with dirty data from engines.
	 */
	struct vclock start_vclock;
	relay_initial_join(io->fd, header->sync, &start_vclock);
	say_info("initial data sent.");

	/**
	 * Call the server-side hook which stores the replica uuid
	 * in _cluster space after sending the last row but before
	 * sending OK - if the hook fails, the error reaches the
	 * client.
	 */
	box_on_join(&instance_uuid);

	ERROR_INJECT_YIELD(ERRINJ_REPLICA_JOIN_DELAY);

	/* Remember master's vclock after the last request */
	struct vclock stop_vclock;
	vclock_copy(&stop_vclock, &replicaset.vclock);

	if (txn_limbo.rollback_count != limbo_rollback_count)
		tnt_raise(ClientError, ER_SYNC_ROLLBACK);

	if (txn_limbo_wait_confirm(&txn_limbo) != 0)
		diag_raise();

	/* Send end of initial stage data marker */
	struct xrow_header row;
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
	xrow_encode_vclock_xc(&row, &replicaset.vclock);
	row.sync = header->sync;
	coio_write_xrow(io, &row);

	/*
	 * Advance the WAL consumer state to the position where
	 * FINAL JOIN ended and assign it to the replica.
	 */
	gc_consumer_advance(gc, &stop_vclock);
	replica = replica_by_uuid(&instance_uuid);
	if (replica->gc != NULL)
		gc_consumer_unregister(replica->gc);
	replica->gc = gc;
	gc_guard.is_active = false;
}

void
box_process_subscribe(struct ev_io *io, struct xrow_header *header)
{
	assert(header->type == IPROTO_SUBSCRIBE);

	/* Check that bootstrap has been finished */
	if (!is_box_configured)
		tnt_raise(ClientError, ER_LOADING);

	struct tt_uuid replica_uuid = uuid_nil;
	struct vclock replica_clock;
	uint32_t replica_version_id;
	vclock_create(&replica_clock);
	bool anon;
	uint32_t id_filter;
	xrow_decode_subscribe_xc(header, NULL, &replica_uuid, &replica_clock,
				 &replica_version_id, &anon, &id_filter);

	/* Forbid connection to itself */
	if (tt_uuid_is_equal(&replica_uuid, &INSTANCE_UUID))
		tnt_raise(ClientError, ER_CONNECTION_TO_SELF);

	/*
	 * Do not allow non-anonymous followers for anonymous
	 * instances.
	 */
	if (replication_anon && !anon) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Anonymous replica",
			  "non-anonymous followers.");
	}

	/* Check permissions */
	access_check_universe_xc(PRIV_R);

	/* Check replica uuid */
	struct replica *replica = replica_by_uuid(&replica_uuid);

	if (!anon && (replica == NULL || replica->id == REPLICA_ID_NIL)) {
		/*
		 * The instance is not anonymous, and is registered (at least it
		 * claims so), but its ID is not delivered to the current
		 * instance yet. Need to wait until its _cluster record arrives
		 * from some third node. Likely to happen on bootstrap, when
		 * there is a fullmesh and 1 leader doing all the _cluster
		 * registrations. Not all of them are delivered to the other
		 * nodes yet.
		 * Also can happen when the replica is deleted from _cluster,
		 * but still tries to subscribe. It won't have an ID here.
		 */
		tnt_raise(ClientError, ER_TOO_EARLY_SUBSCRIBE,
			  tt_uuid_str(&replica_uuid));
	}
	if (anon && replica != NULL && replica->id != REPLICA_ID_NIL) {
		tnt_raise(ClientError, ER_PROTOCOL, "Can't subscribe an "
			  "anonymous replica having an ID assigned");
	}
	if (replica == NULL)
		replica = replicaset_add_anon(&replica_uuid);

	/* Don't allow multiple relays for the same replica */
	if (relay_get_state(replica->relay) == RELAY_FOLLOW) {
		tnt_raise(ClientError, ER_CFG, "replication",
			  "duplicate connection with the same replica UUID");
	}

	/* Forbid replication with disabled WAL */
	if (wal_mode() == WAL_NONE) {
		tnt_raise(ClientError, ER_UNSUPPORTED, "Replication",
			  "wal_mode = 'none'");
	}

	struct vclock vclock;
	vclock_create(&vclock);
	vclock_copy(&vclock, &replicaset.vclock);
	/*
	 * Send a response to SUBSCRIBE request, tell
	 * the replica how many rows we have in stock for it,
	 * and identify ourselves with our own replica id.
	 *
	 * Tarantool > 2.1.1 master doesn't check that replica
	 * has the same cluster id. Instead it sends its cluster
	 * id to replica, and replica checks that its cluster id
	 * matches master's one. Older versions will just ignore
	 * the additional field.
	 */
	struct xrow_header row;
	xrow_encode_subscribe_response_xc(&row, &REPLICASET_UUID, &vclock);
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

	say_info("subscribed replica %s at %s",
		 tt_uuid_str(&replica_uuid), sio_socketname(io->fd));
	say_info("remote vclock %s local vclock %s",
		 vclock_to_string(&replica_clock), vclock_to_string(&vclock));
	if (raft_is_enabled(box_raft())) {
		/*
		 * Send out the current raft state of the instance. Don't do
		 * that if Raft is disabled. It can be that a part of the
		 * cluster still contains old versions, which can't handle Raft
		 * messages. So when it is disabled, its network footprint
		 * should be 0.
		 */
		struct raft_request req;
		box_raft_checkpoint_remote(&req);
		xrow_encode_raft(&row, &fiber()->gc, &req);
		coio_write_xrow(io, &row);
	}
	/*
	 * Replica clock is used in gc state and recovery
	 * initialization, so we need to replace the remote 0-th
	 * component with our own one. This doesn't break
	 * recovery: it finds the WAL with a vclock strictly less
	 * than replia clock in all components except the 0th one.
	 * This leads to finding the correct WAL, if it exists,
	 * since we do not need to recover local rows (the ones,
	 * that contribute to the 0-th vclock component).
	 * Note, that it would be bad to set 0-th vclock component
	 * to a smaller value, since it would unnecessarily
	 * require additional WALs, which may have already been
	 * deleted.
	 * Speaking of gc, remote instances' local vclock
	 * components are not used by consumers at all.
	 */
	vclock_reset(&replica_clock, 0, vclock_get(&replicaset.vclock, 0));
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
	relay_subscribe(replica, io->fd, header->sync, &replica_clock,
			replica_version_id, id_filter);
}

void
box_process_vote(struct ballot *ballot)
{
	ballot->is_ro = cfg_geti("read_only") != 0;
	ballot->is_anon = replication_anon;
	/*
	 * is_ro is true on initial load and is set to box.cfg.read_only
	 * after box_cfg() returns, during dynamic box.cfg parameters setting.
	 * We would like to prefer already bootstrapped instances to the ones
	 * still bootstrapping and the ones still bootstrapping, but writeable
	 * to the ones that have box.cfg.read_only = true.
	 */
	ballot->is_loading = is_ro;
	vclock_copy(&ballot->vclock, &replicaset.vclock);
	vclock_copy(&ballot->gc_vclock, &gc.vclock);
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
	if (boxk(IPROTO_INSERT, BOX_SCHEMA_ID, "[%s%s]", "cluster",
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
		user_cache_free();
		schema_free();
		module_free();
		tuple_free();
		port_free();
#endif
		box_raft_free();
		iproto_free();
		replication_free();
		sequence_free();
		gc_free();
		engine_shutdown();
		wal_free();
	}
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
				    cfg_geti("strip_core"),
				    cfg_geti("granularity"),
				    cfg_getd("slab_alloc_factor"));
	engine_register((struct engine *)memtx);
	box_set_memtx_max_tuple_size();

	struct sysview_engine *sysview = sysview_engine_new_xc();
	engine_register((struct engine *)sysview);

	struct engine *service_engine = service_engine_new_xc();
	engine_register(service_engine);

	struct engine *blackhole = blackhole_engine_new_xc();
	engine_register(blackhole);

	struct engine *vinyl;
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
 * Blindly apply whatever comes from bootstrap. This is either a
 * local snapshot, or received from a remote master. In both cases
 * it is not WAL - records are not sorted by their commit order,
 * and don't have headers.
 */
static int
bootstrap_journal_write(struct journal *base, struct journal_entry *entry)
{
	(void)base;

	entry->res = 0;
	journal_async_complete(entry);
	return 0;
}

/**
 * Initialize the first replica of a new replica set.
 */
static void
bootstrap_master(const struct tt_uuid *replicaset_uuid)
{
	/* Do not allow to bootstrap a readonly instance as master. */
	if (cfg_geti("read_only") == 1) {
		tnt_raise(ClientError, ER_BOOTSTRAP_READONLY);
	}
	engine_bootstrap_xc();

	uint32_t replica_id = 1;

	/* Register the first replica in the replica set */
	box_register_replica(replica_id, &INSTANCE_UUID);
	assert(replica_by_uuid(&INSTANCE_UUID)->id == 1);

	/* Set UUID of a new replica set */
	box_set_replicaset_uuid(replicaset_uuid);

	/* Enable WAL subsystem. */
	if (wal_enable() != 0)
		diag_raise();

	/* Make the initial checkpoint */
	if (gc_checkpoint() != 0)
		panic("failed to create a checkpoint");
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

	say_info("bootstrapping replica from %s at %s",
		 tt_uuid_str(&master->uuid),
		 sio_strfaddr(&applier->addr, applier->addr_len));

	/*
	 * Send JOIN request to master
	 * See box_process_join().
	 */

	assert(!tt_uuid_is_nil(&INSTANCE_UUID));
	enum applier_state wait_state = replication_anon ?
					APPLIER_FETCH_SNAPSHOT :
					APPLIER_INITIAL_JOIN;
	applier_resume_to_state(applier, wait_state, TIMEOUT_INFINITY);
	/*
	 * Process initial data (snapshot or dirty disk data).
	 */
	engine_begin_initial_recovery_xc(NULL);
	wait_state = replication_anon ? APPLIER_FETCHED_SNAPSHOT :
					APPLIER_FINAL_JOIN;
	applier_resume_to_state(applier, wait_state, TIMEOUT_INFINITY);

	/*
	 * Process final data (WALs).
	 */
	engine_begin_final_recovery_xc();
	recovery_journal_create(&replicaset.vclock);

	if (!replication_anon) {
		applier_resume_to_state(applier, APPLIER_JOINED,
					TIMEOUT_INFINITY);
	}
	/* Finalize the new replica */
	engine_end_recovery_xc();

	/* Switch applier to initial state */
	applier_resume_to_state(applier, APPLIER_READY, TIMEOUT_INFINITY);
	assert(applier->state == APPLIER_READY);

	/*
	 * An engine may write to WAL on its own during the join
	 * stage (e.g. Vinyl's deferred DELETEs). That's OK - those
	 * records will pass through the recovery journal and wind
	 * up in the initial checkpoint. However, we must enable
	 * the WAL right before starting checkpointing so that
	 * records written during and after the initial checkpoint
	 * go to the real WAL and can be recovered after restart.
	 * This also clears the recovery journal created on stack.
	 */
	if (wal_enable() != 0)
		diag_raise();

	/* Make the initial checkpoint */
	if (gc_checkpoint() != 0)
		panic("failed to create a checkpoint");
}

/**
 * Bootstrap a new instance either as the first master in a
 * replica set or as a replica of an existing master.
 *
 * @param[out] is_bootstrap_leader  set if this instance is
 *                                  the leader of a new cluster
 */
static void
bootstrap(const struct tt_uuid *instance_uuid,
	  const struct tt_uuid *replicaset_uuid,
	  bool *is_bootstrap_leader)
{
	/* Initialize instance UUID. */
	assert(tt_uuid_is_nil(&INSTANCE_UUID));
	if (!tt_uuid_is_nil(instance_uuid))
		INSTANCE_UUID = *instance_uuid;
	else
		tt_uuid_create(&INSTANCE_UUID);

	say_info("instance uuid %s", tt_uuid_str(&INSTANCE_UUID));

	/*
	 * Begin listening on the socket to enable
	 * master-master replication leader election.
	 */
	box_listen();
	/*
	 * Wait for the cluster to start up.
	 *
	 * Note, when bootstrapping a new instance, we try to
	 * connect to all masters during timeout to make sure
	 * all replicas recieve the same replica set UUID when
	 * a new cluster is deployed. If we fail to do so, settle
	 * with connecting to 'replication_connect_quorum' masters.
	 * If this also fails, throw an error.
	 */
	box_sync_replication(true);

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
}

/**
 * Recover the instance from the local directory.
 * Enter hot standby if the directory is locked.
 * Invoke rebootstrap if the instance fell too much
 * behind its peers in the replica set and needs
 * to be rebootstrapped.
 */
static void
local_recovery(const struct tt_uuid *instance_uuid,
	       const struct tt_uuid *replicaset_uuid,
	       const struct vclock *checkpoint_vclock)
{
	/* Check instance UUID. */
	assert(!tt_uuid_is_nil(&INSTANCE_UUID));
	if (!tt_uuid_is_nil(instance_uuid) &&
	    !tt_uuid_is_equal(instance_uuid, &INSTANCE_UUID)) {
		tnt_raise(ClientError, ER_INSTANCE_UUID_MISMATCH,
			  tt_uuid_str(instance_uuid),
			  tt_uuid_str(&INSTANCE_UUID));
	}

	say_info("instance uuid %s", tt_uuid_str(&INSTANCE_UUID));

	struct wal_stream wal_stream;
	wal_stream_create(&wal_stream);

	struct recovery *recovery;
	recovery = recovery_new(wal_dir(), cfg_geti("force_recovery"),
				checkpoint_vclock);

	/*
	 * Make sure we report the actual recovery position
	 * in box.info while local recovery is in progress.
	 */
	box_vclock = &recovery->vclock;
	auto guard = make_scoped_guard([&]{
		box_vclock = &replicaset.vclock;
		recovery_delete(recovery);
	});

	/*
	 * Initialize the replica set vclock from recovery.
	 * The local WAL may contain rows from remote masters,
	 * so we must reflect this in replicaset vclock to
	 * not attempt to apply these rows twice.
	 */
	recovery_scan(recovery, &replicaset.vclock, &gc.vclock);
	say_info("instance vclock %s", vclock_to_string(&replicaset.vclock));

	if (wal_dir_lock >= 0) {
		box_listen();
		box_sync_replication(false);

		struct replica *master;
		if (replicaset_needs_rejoin(&master)) {
			say_crit("replica is too old, initiating rebootstrap");
			return bootstrap_from_master(master);
		}
	}

	/*
	 * recovery->vclock is needed by Vinyl to filter
	 * WAL rows that were dumped before restart.
	 *
	 * XXX: Passing an internal member of the recovery
	 * object to an engine is an ugly hack. Instead we
	 * should introduce space_vtab::apply_wal_row method
	 * and explicitly pass the statement LSN to it.
	 */
	engine_begin_initial_recovery_xc(&recovery->vclock);

	struct memtx_engine *memtx;
	memtx = (struct memtx_engine *)engine_by_name("memtx");
	assert(memtx != NULL);

	is_local_recovery = true;
	recovery_journal_create(&recovery->vclock);

	/*
	 * We explicitly request memtx to recover its
	 * snapshot as a separate phase since it contains
	 * data for system spaces, and triggers on
	 * recovery of system spaces issue DDL events in
	 * other engines.
	 */
	memtx_engine_recover_snapshot_xc(memtx, checkpoint_vclock);

	engine_begin_final_recovery_xc();
	recover_remaining_wals(recovery, &wal_stream.base, NULL, false);
	engine_end_recovery_xc();
	/*
	 * Leave hot standby mode, if any, only after
	 * acquiring the lock.
	 */
	if (wal_dir_lock < 0) {
		title("hot_standby");
		say_info("Entering hot standby mode");
		recovery_follow_local(recovery, &wal_stream.base, "hot_standby",
				      cfg_getd("wal_dir_rescan_delay"));
		while (true) {
			if (path_lock(wal_dir(), &wal_dir_lock))
				diag_raise();
			if (wal_dir_lock >= 0)
				break;
			fiber_sleep(0.1);
		}
		recovery_stop_local(recovery);
		recover_remaining_wals(recovery, &wal_stream.base, NULL, true);
		/*
		 * Advance replica set vclock to reflect records
		 * applied in hot standby mode.
		 */
		vclock_copy(&replicaset.vclock, &recovery->vclock);
		box_listen();
		box_sync_replication(false);
	}
	recovery_finalize(recovery);
	is_local_recovery = false;

	/*
	 * We must enable WAL before finalizing engine recovery,
	 * because an engine may start writing to WAL right after
	 * this point (e.g. deferred DELETE statements in Vinyl).
	 * This also clears the recovery journal created on stack.
	 */
	if (wal_enable() != 0)
		diag_raise();

	/* Check replica set UUID. */
	if (!tt_uuid_is_nil(replicaset_uuid) &&
	    !tt_uuid_is_equal(replicaset_uuid, &REPLICASET_UUID)) {
		tnt_raise(ClientError, ER_REPLICASET_UUID_MISMATCH,
			  tt_uuid_str(replicaset_uuid),
			  tt_uuid_str(&REPLICASET_UUID));
	}
}

static void
tx_prio_cb(struct ev_loop *loop, ev_watcher *watcher, int events)
{
	(void) loop;
	(void) events;
	struct cbus_endpoint *endpoint = (struct cbus_endpoint *)watcher->data;
	cbus_process(endpoint);
}

static void
on_wal_garbage_collection(const struct vclock *vclock)
{
	gc_advance(vclock);
}

static void
on_wal_checkpoint_threshold(void)
{
	say_info("WAL threshold exceeded, triggering checkpoint");
	gc_trigger_checkpoint();
}

void
box_init(void)
{
	msgpack_init();
	fiber_cond_create(&ro_cond);

	user_cache_init();
	/*
	 * The order is important: to initialize sessions,
	 * we need to access the admin user, which is used
	 * as a default session user when running triggers.
	 */
	session_init();

	if (module_init() != 0)
		diag_raise();

	if (tuple_init(lua_hash) != 0)
		diag_raise();

	txn_limbo_init();
	sequence_init();
	box_raft_init();
}

bool
box_is_configured(void)
{
	return is_box_configured;
}

static void
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
	schema_init();
	replication_init();
	port_init();
	iproto_init();
	sql_init();

	int64_t wal_max_size = box_check_wal_max_size(cfg_geti64("wal_max_size"));
	enum wal_mode wal_mode = box_check_wal_mode(cfg_gets("wal_mode"));
	if (wal_init(wal_mode, cfg_gets("wal_dir"), wal_max_size,
		     &INSTANCE_UUID, on_wal_garbage_collection,
		     on_wal_checkpoint_threshold) != 0) {
		diag_raise();
	}

	title("loading");

	struct tt_uuid instance_uuid, replicaset_uuid;
	box_check_instance_uuid(&instance_uuid);
	box_check_replicaset_uuid(&replicaset_uuid);

	if (box_set_prepared_stmt_cache_size() != 0)
		diag_raise();
	box_set_net_msg_max();
	box_set_readahead();
	box_set_too_long_threshold();
	box_set_replication_timeout();
	box_set_replication_connect_timeout();
	box_set_replication_connect_quorum();
	box_set_replication_sync_lag();
	if (box_set_replication_synchro_quorum() != 0)
		diag_raise();
	if (box_set_replication_synchro_timeout() != 0)
		diag_raise();
	box_set_replication_sync_timeout();
	box_set_replication_skip_conflict();
	box_set_replication_anon();

	struct gc_checkpoint *checkpoint = gc_last_checkpoint();

	/*
	 * Lock the write ahead log directory to avoid multiple
	 * instances running in the same dir.
	 */
	if (path_lock(wal_dir(), &wal_dir_lock) < 0)
		diag_raise();
	if (wal_dir_lock < 0) {
		/**
		 * The directory is busy and hot standby mode is off:
		 * refuse to start. In hot standby mode, a busy
		 * WAL dir must contain at least one xlog.
		 */
		if (!cfg_geti("hot_standby") || checkpoint == NULL)
			tnt_raise(ClientError, ER_ALREADY_RUNNING, wal_dir());
	}

	struct journal bootstrap_journal;
	journal_create(&bootstrap_journal, NULL, bootstrap_journal_write);
	journal_set(&bootstrap_journal);
	auto bootstrap_journal_guard = make_scoped_guard([] {
		journal_set(NULL);
	});

	bool is_bootstrap_leader = false;
	if (checkpoint != NULL) {
		/* Recover the instance from the local directory */
		local_recovery(&instance_uuid, &replicaset_uuid,
			       &checkpoint->vclock);
	} else {
		/* Bootstrap a new instance */
		bootstrap(&instance_uuid, &replicaset_uuid,
			  &is_bootstrap_leader);
	}
	fiber_gc();

	bootstrap_journal_guard.is_active = false;
	assert(current_journal != &bootstrap_journal);

	/*
	 * Check for correct registration of the instance in _cluster
	 * The instance won't exist in _cluster space if it is an
	 * anonymous replica, add it manually.
	 */
	struct replica *self = replica_by_uuid(&INSTANCE_UUID);
	if (!replication_anon) {
		if (self == NULL || self->id == REPLICA_ID_NIL) {
			tnt_raise(ClientError, ER_UNKNOWN_REPLICA,
				  tt_uuid_str(&INSTANCE_UUID),
				  tt_uuid_str(&REPLICASET_UUID));
		}
	} else if (self == NULL) {
		replicaset_add_anon(&INSTANCE_UUID);
	}

	rmean_cleanup(rmean_box);

	/* Follow replica */
	replicaset_follow();

	fiber_gc();
	is_box_configured = true;
	/*
	 * Fill in leader election parameters after bootstrap. Before it is not
	 * possible - there may be relevant data to recover from WAL and
	 * snapshot. Also until recovery is done, it is not possible to write
	 * new records into WAL. Another reason - before recovery is done,
	 * instance_id is not known, so Raft simply can't work.
	 */
	if (!replication_anon)
		raft_cfg_instance_id(box_raft(), instance_id);
	raft_cfg_vclock(box_raft(), &replicaset.vclock);

	if (box_set_election_timeout() != 0)
		diag_raise();
	/*
	 * Election is enabled last. So as all the parameters are installed by
	 * that time.
	 */
	if (box_set_election_mode() != 0)
		diag_raise();

	title("running");
	say_info("ready to accept requests");

	if (!is_bootstrap_leader) {
		replicaset_sync();
	} else {
		/*
		 * When the cluster is just bootstrapped and this instance is a
		 * leader, it makes no sense to wait for a leader appearance.
		 * There is no one. Moreover this node *is* a leader, so it
		 * should take the control over the situation and start a new
		 * term immediately.
		 */
		raft_new_term(box_raft());
	}

	/* box.cfg.read_only is not read yet. */
	assert(box_is_ro());
	/* If anyone is waiting for ro mode. */
	fiber_cond_broadcast(&ro_cond);
	/*
	 * Yield to let ro condition waiters to handle the event.
	 * Without yield it may happen there won't be a context
	 * switch until the ro state is changed again, and as a
	 * result, some ro waiters may sleep forever. For example,
	 * when Tarantool is just started, it is expected it will
	 * enter ro=true state, and then ro=false. Without the
	 * yield the ro=true event may be lost. This affects
	 * box.ctl.wait_ro() call.
	 */
	fiber_sleep(0);
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
box_atfork(void)
{
	wal_atfork();
}

int
box_checkpoint(void)
{
	/* Signal arrived before box.cfg{} */
	if (! is_box_configured)
		return 0;

	return gc_checkpoint();
}

int
box_backup_start(int checkpoint_idx, box_backup_cb cb, void *cb_arg)
{
	assert(checkpoint_idx >= 0);
	if (backup_is_in_progress) {
		diag_set(ClientError, ER_BACKUP_IN_PROGRESS);
		return -1;
	}
	struct gc_checkpoint *checkpoint;
	gc_foreach_checkpoint_reverse(checkpoint) {
		if (checkpoint_idx-- == 0)
			break;
	}
	if (checkpoint_idx >= 0) {
		diag_set(ClientError, ER_MISSING_SNAPSHOT);
		return -1;
	}
	backup_is_in_progress = true;
	gc_ref_checkpoint(checkpoint, &backup_gc, "backup");
	int rc = engine_backup(&checkpoint->vclock, cb, cb_arg);
	if (rc != 0) {
		gc_unref_checkpoint(&backup_gc);
		backup_is_in_progress = false;
	}
	return rc;
}

void
box_backup_stop(void)
{
	if (backup_is_in_progress) {
		gc_unref_checkpoint(&backup_gc);
		backup_is_in_progress = false;
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
