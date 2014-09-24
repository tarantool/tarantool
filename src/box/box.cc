/*
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
#include <arpa/inet.h>
#include <sys/wait.h>

#include <errcode.h>
#include "recovery.h"
#include "log_io.h"
#include <say.h>
#include <iproto.h>
#include "replication.h"
#include <stat.h>
#include <tarantool.h>
#include "tuple.h"
#include "lua/call.h"
#include "session.h"
#include "schema.h"
#include "engine.h"
#include "engine_memtx.h"
#include "engine_sophia.h"
#include "space.h"
#include "port.h"
#include "request.h"
#include "txn.h"
#include "fiber.h"
#include "access.h"
#include "cfg.h"
#include "iobuf.h"

static void process_ro(struct port *port, struct request *request);
static void process_rw(struct port *port, struct request *request);
box_process_func box_process = process_ro;

static int stat_base;
int snapshot_pid = 0; /* snapshot processes pid */

static void
box_snapshot_cb(struct log_io *l);

/** The snapshot row metadata repeats the structure of REPLACE request. */
struct request_replace_body {
	uint8_t m_body;
	uint8_t k_space_id;
	uint8_t m_space_id;
	uint32_t v_space_id;
	uint8_t k_tuple;
} __attribute__((packed));

static void
process_rw(struct port *port, struct request *request)
{
	try {
		stat_collect(stat_base, request->type, 1);
		request->execute(request, port);
		port_eof(port);
	} catch (Exception *e) {
		txn_rollback_stmt();
		throw;
	}
}

static void
process_ro(struct port *port, struct request *request)
{
	if (!iproto_type_is_select(request->type))
		tnt_raise(LoggedError, ER_SECONDARY);
	return process_rw(port, request);
}

static void
recover_row(void *param __attribute__((unused)), struct iproto_header *row)
{
	assert(row->bodycnt == 1); /* always 1 for read */
	struct request request;
	request_create(&request, row->type);
	request_decode(&request, (const char *) row->body[0].iov_base,
		row->body[0].iov_len);
	request.header = row;
	process_rw(&null_port, &request);
}

/* {{{ configuration bindings */

void
box_check_replication_source(const char *source)
{
	if (source == NULL)
		return;
	struct uri uri;
	if (uri_parse(&uri, source)) {
		tnt_raise(ClientError, ER_CFG,
			  "incorrect replication source");
	}
}

static void
box_check_wal_mode(const char *mode_name)
{
	int mode = strindex(wal_mode_STRS, mode_name, WAL_MODE_MAX);
	if (mode == WAL_MODE_MAX) {
		tnt_raise(ClientError, ER_CFG,
			  "wal_mode is not recognized");
	}
}

static void
box_check_config()
{
	box_check_wal_mode(cfg_gets("wal_mode"));

	/* check rows_per_wal configuration */
	if (cfg_geti("rows_per_wal") <= 1) {
		tnt_raise(ClientError, ER_CFG,
			  "rows_per_wal must be greater than one");
	}
}

extern "C" void
box_set_replication_source(const char *source)
{
	box_check_replication_source(source);
	bool old_is_replica = recovery->remote.reader;
	bool new_is_replica = source != NULL;

	if (old_is_replica != new_is_replica ||
	    (old_is_replica &&
	     (strcmp(source, recovery->remote.source) != 0))) {

		if (recovery->finalize) {
			if (old_is_replica)
				recovery_stop_remote(recovery);
			recovery_set_remote(recovery, source);
			if (recovery_has_remote(recovery))
				recovery_follow_remote(recovery);
		} else {
			/*
			 * Do nothing, we're in local hot
			 * standby mode, the server
			 * will automatically begin following
			 * the remote when local hot standby
			 * mode is finished, see
			 * box_leave_local_hot_standby_mode()
			 */
		}
	}
}

extern "C" void
box_set_wal_mode(const char *mode_name)
{
	box_check_wal_mode(mode_name);
	enum wal_mode mode = (enum wal_mode)
		strindex(wal_mode_STRS, mode_name, WAL_MODE_MAX);
	if (mode != recovery->wal_mode &&
	    (mode == WAL_FSYNC || recovery->wal_mode == WAL_FSYNC)) {
		tnt_raise(ClientError, ER_CFG,
			  "wal_mode cannot switch to/from fsync");
	}
	recovery_update_mode(recovery, mode);
}

extern "C" void
box_set_log_level(int level)
{
	say_set_log_level(level);
}

extern "C" void
box_set_io_collect_interval(double interval)
{
	ev_set_io_collect_interval(loop(), interval);
}

extern "C" void
box_set_snap_io_rate_limit(double limit)
{
	recovery_update_io_rate_limit(recovery, limit);
}

extern "C" void
box_set_too_long_threshold(double threshold)
{
	too_long_threshold = threshold;
}

/* }}} configuration bindings */

void
box_leave_local_standby_mode(void *data __attribute__((unused)))
{
	if (recovery->finalize) {
		/*
		 * Nothing to do: this happens when the server
		 * binds to both ports, and one of the callbacks
		 * is called first.
		 */
		return;
	}
	recovery_finalize(recovery);

	/*
	 * notify engines about end of recovery.
	*/
	space_end_recover();

	stat_cleanup(stat_base, IPROTO_TYPE_DML_MAX);
	box_set_wal_mode(cfg_gets("wal_mode"));

	box_process = process_rw;
	if (recovery_has_remote(recovery))
		recovery_follow_remote(recovery);

	title("running", NULL);
	say_info("ready to accept requests");
}

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
	req.tuple = buf;
	req.tuple_end = data;
	process_rw(&null_port, &req);
}

/**
 * @brief Called when recovery/replication wants to add a new server
 * to cluster.
 * cluster_add_server() is called as a commit trigger on jcluster
 * space and actually adds the server to the cluster.
 * @param server_uuid
 */
static void
box_on_cluster_join(const tt_uuid *server_uuid)
{
	/** Find the largest existing server id. */
	struct space *space = space_cache_find(SC_CLUSTER_ID);
	class Index *index = index_find(space, 0);
	struct iterator *it = index->position();
	index->initIterator(it, ITER_LE, NULL, 0);
	struct tuple *tuple = it->next(it);
	/** Assign a new server id. */
	uint32_t server_id = tuple ? tuple_field_u32(tuple, 0) + 1 : 1;
	if (server_id >= VCLOCK_MAX)
		tnt_raise(ClientError, ER_REPLICA_MAX, server_id);

	boxk(IPROTO_INSERT, SC_CLUSTER_ID, "%u%s",
	     (unsigned) server_id, tt_uuid_str(server_uuid));
}

/** Replace the current server id in _cluster */
static void
box_set_server_uuid()
{
	struct recovery_state *r = recovery;
	tt_uuid_create(&r->server_uuid);
	assert(r->server_id == 0);
	if (vclock_has(&r->vclock, 1))
		vclock_del_server(&r->vclock, 1);
	boxk(IPROTO_REPLACE, SC_CLUSTER_ID, "%u%s",
	     1, tt_uuid_str(&r->server_uuid));
	/* Remove surrogate server */
	vclock_del_server(&r->vclock, 0);
	assert(r->server_id == 1);
	assert(vclock_has(&r->vclock, 1));
}

/** Insert a new cluster into _schema */
static void
box_set_cluster_uuid()
{
	tt_uuid uu;
	/* Generate a new cluster UUID */
	tt_uuid_create(&uu);
	/* Save cluster UUID in _schema */
	boxk(IPROTO_REPLACE, SC_SCHEMA_ID, "%s%s", "cluster",
	     tt_uuid_str(&uu));
}

void
box_free(void)
{
	user_cache_free();
	schema_free();
	tuple_free();
	recovery_free();
	engine_shutdown();
	stat_free();
	session_free();
}

static void
engine_init()
{
	MemtxFactory *memtx = new MemtxFactory();
	engine_register(memtx);

	SophiaFactory *sophia = new SophiaFactory();
	sophia->init();
	engine_register(sophia);
}

void
box_init()
{
	box_check_config();
	title("loading", NULL);

	session_init();
	replication_prefork(cfg_gets("snap_dir"), cfg_gets("wal_dir"));
	stat_init();

	tuple_init(cfg_getd("slab_alloc_arena"),
		   cfg_geti("slab_alloc_minimal"),
		   cfg_getd("slab_alloc_factor"));

	engine_init();

	schema_init();
	user_cache_init();

	/* recovery initialization */
	recovery_init(cfg_gets("snap_dir"), cfg_gets("wal_dir"),
		      recover_row, NULL, box_snapshot_cb, box_on_cluster_join,
		      cfg_geti("rows_per_wal"));
	recovery_set_remote(recovery, cfg_gets("replication_source"));
	recovery_update_io_rate_limit(recovery,
				      cfg_getd("snap_io_rate_limit"));
	recovery_setup_panic(recovery,
			     cfg_geti("panic_on_snap_error"),
			     cfg_geti("panic_on_wal_error"));

	stat_base = stat_register(iproto_type_strs, IPROTO_TYPE_DML_MAX);

	if (recovery_has_data(recovery)) {
		/* Process existing snapshot */
		recover_snap(recovery);
		space_end_recover_snapshot();
	} else if (recovery_has_remote(recovery)) {
		/* Initialize a new replica */
		replica_bootstrap(recovery);
		space_end_recover_snapshot();
		snapshot_save(recovery);
	} else {
		/* Initialize the first server of a new cluster */
		recovery_bootstrap(recovery);
		box_set_cluster_uuid();
		box_set_server_uuid();
		space_end_recover_snapshot();
		snapshot_save(recovery);
	}

	title("orphan", NULL);
	recovery_follow_local(recovery,
			      cfg_getd("wal_dir_rescan_delay"));
	title("hot_standby", NULL);

	const char *listen = cfg_gets("listen");

	/*
	 * application server configuration).
	 */
	if (listen) {
		iproto_init(listen);
	} else {
		box_leave_local_standby_mode(NULL);
	}
	if (cfg_getd("io_collect_interval") > 0) {
		ev_set_io_collect_interval(loop(),
					   cfg_getd("io_collect_interval"));
	}
	too_long_threshold = cfg_getd("too_long_threshold");
	iobuf_set_readahead(cfg_geti("readahead"));
}

static void
snapshot_write_tuple(struct log_io *l,
		     uint32_t n, struct tuple *tuple)
{
	struct request_replace_body body;
	body.m_body = 0x82; /* map of two elements. */
	body.k_space_id = IPROTO_SPACE_ID;
	body.m_space_id = 0xce; /* uint32 */
	body.v_space_id = mp_bswap_u32(n);
	body.k_tuple = IPROTO_TUPLE;

	struct iproto_header row;
	memset(&row, 0, sizeof(struct iproto_header));
	row.type = IPROTO_INSERT;

	row.bodycnt = 2;
	row.body[0].iov_base = &body;
	row.body[0].iov_len = sizeof(body);
	row.body[1].iov_base = tuple->data;
	row.body[1].iov_len = tuple->bsize;
	snapshot_write_row(l, &row);
}

static void
snapshot_space(struct space *sp, void *udata)
{
	if (space_is_temporary(sp))
		return;
	if (space_is_sophia(sp))
		return;
	struct tuple *tuple;
	struct log_io *l = (struct log_io *)udata;
	Index *pk = space_index(sp, 0);
	if (pk == NULL)
		return;
	struct iterator *it = pk->position();
	pk->initIterator(it, ITER_ALL, NULL, 0);

	while ((tuple = it->next(it)))
		snapshot_write_tuple(l, space_id(sp), tuple);
}

static void
box_snapshot_cb(struct log_io *l)
{
	space_foreach(snapshot_space, l);
}

int
box_snapshot(void)
{
	if (snapshot_pid)
		return EINPROGRESS;

	/* flush buffers to avoid multiple output */
	/* https://github.com/tarantool/tarantool/issues/366 */
	fflush(stdout);
	fflush(stderr);
	pid_t p = fork();
	if (p < 0) {
		say_syserror("fork");
		return -1;
	}
	if (p > 0) {
		snapshot_pid = p;
		/* increment snapshot version */
		tuple_begin_snapshot();
		int status = wait_for_child(p);
		tuple_end_snapshot();
		snapshot_pid = 0;
		return (WIFSIGNALED(status) ? EINTR : WEXITSTATUS(status));
	}

	slab_arena_mprotect(&tuple_arena);

	cord_set_name("snap");
	title("dumper", "%" PRIu32, getppid());
	fiber_set_name(fiber(), "dumper");
	/*
	 * Safety: make sure we don't double-write
	 * parent stdio buffers at exit().
	 */
	close_all_xcpt(1, log_fd);
	snapshot_save(recovery);

	exit(EXIT_SUCCESS);
	return 0;
}

void
box_info(struct tbuf *out)
{
	tbuf_printf(out, "  status: %s" CRLF, status);
}

const char *
box_status(void)
{
    return status;
}
