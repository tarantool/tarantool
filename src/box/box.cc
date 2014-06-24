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
#include <admin.h>
#include <iproto.h>
#include "replication.h"
#include <stat.h>
#include <tarantool.h>
#include "tuple.h"
#include "lua/call.h"
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

void
port_send_tuple(struct port *port, struct txn *txn)
{
	struct tuple *tuple;
	if ((tuple = txn->new_tuple) || (tuple = txn->old_tuple))
		port_add_tuple(port, tuple);
}

static void
process_rw(struct port *port, struct request *request)
{
	struct txn *txn = txn_begin();
	try {
		stat_collect(stat_base, request->type, 1);
		request->execute(request, txn, port);
		txn_commit(txn);
		port_send_tuple(port, txn);
		port_eof(port);
		txn_finish(txn);
	} catch (Exception *e) {
		txn_rollback(txn);
		throw;
	}
}

static void
process_ro(struct port *port, struct request *request)
{
	if (!iproto_request_is_select(request->type))
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
	struct port_uri uri;
	if (port_uri_parse(&uri, source)) {
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

	/* check primary port */
	int primary_port = cfg_geti("primary_port");
	if (primary_port < 0 || primary_port >= USHRT_MAX)
		tnt_raise(ClientError, ER_CFG,
			  "invalid primary port value");

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
	bool old_is_replica = recovery_state->remote.reader;
	bool new_is_replica = source != NULL;

	if (old_is_replica != new_is_replica ||
	    (old_is_replica &&
	     (strcmp(source, recovery_state->remote.source) != 0))) {

		if (recovery_state->finalize) {
			if (old_is_replica)
				recovery_stop_remote(recovery_state);
			recovery_set_remote(recovery_state, source);
			if (recovery_has_remote(recovery_state))
				recovery_follow_remote(recovery_state);
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
	if (mode != recovery_state->wal_mode &&
	    (mode == WAL_FSYNC || recovery_state->wal_mode == WAL_FSYNC)) {
		tnt_raise(ClientError, ER_CFG,
			  "wal_mode cannot switch to/from fsync");
	}
	recovery_update_mode(recovery_state, mode);
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
	recovery_update_io_rate_limit(recovery_state, limit);
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
	if (recovery_state->finalize) {
		/*
		 * Nothing to do: this happens when the server
		 * binds to both ports, and one of the callbacks
		 * is called first.
		 */
		return;
	}

	recovery_finalize(recovery_state);
	stat_cleanup(stat_base, IPROTO_DML_REQUEST_MAX);

	box_set_wal_mode(cfg_gets("wal_mode"));

	box_process = process_rw;
	if (recovery_has_remote(recovery_state))
		recovery_follow_remote(recovery_state);

	title("primary", NULL);
	say_info("I am primary");
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
boxk(enum iproto_request_type type, uint32_t space_id,
     const char *format, ...)
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
 * @brief Called when recovery/replication wants to add a new node
 * to cluster.
 * cluster_add_node() is called as a commit trigger on _cluster
 * space and actually adds the node to the cluster.
 * @param node_uuid
 */
static void
box_on_cluster_join(const tt_uuid *node_uuid)
{
	/** Find the largest existing node id. */
	struct space *space = space_cache_find(SC_CLUSTER_ID);
	class Index *index = index_find(space, 0);
	struct iterator *it = index->position();
	index->initIterator(it, ITER_LE, NULL, 0);
	struct tuple *tuple = it->next(it);
	/** Assign a new node id. */
	uint32_t server_id = tuple ? tuple_field_u32(tuple, 0) + 1 : 1;
	if (server_id >= VCLOCK_MAX)
		tnt_raise(ClientError, ER_REPLICA_MAX, server_id);

	boxk(IPROTO_INSERT, SC_CLUSTER_ID, "%u%s",
	     (unsigned) server_id, tt_uuid_str(node_uuid));
}

/** Replace the current node id in _cluster */
static void
box_set_node_uuid()
{
	tt_uuid_create(&recovery_state->node_uuid);
	vclock_del_server(&recovery_state->vclock, recovery_state->server_id);
	recovery_state->server_id = 0;            /* please the assert */
	boxk(IPROTO_REPLACE, SC_CLUSTER_ID, "%u%s",
	     1, tt_uuid_str(&recovery_state->node_uuid));
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
	recovery_set_remote(recovery_state, cfg_gets("replication_source"));
	recovery_update_io_rate_limit(recovery_state,
				      cfg_getd("snap_io_rate_limit"));
	recovery_setup_panic(recovery_state,
			     cfg_geti("panic_on_snap_error"),
			     cfg_geti("panic_on_wal_error"));

	stat_base = stat_register(iproto_request_type_strs,
				  IPROTO_DML_REQUEST_MAX);

	if (recovery_has_data(recovery_state)) {
		/* Process existing snapshot */
		recover_snap(recovery_state);
		recovery_end_recover_snapshot(recovery_state);
	} else if (recovery_has_remote(recovery_state)) {
		/* Initialize a new replica */
		replica_bootstrap(recovery_state);
		snapshot_save(recovery_state);
	} else {
		/* Initialize a master node of a new cluster */
		recovery_bootstrap(recovery_state);
		box_set_cluster_uuid();
		box_set_node_uuid();
		recovery_end_recover_snapshot(recovery_state);
		snapshot_save(recovery_state);
	}

	space_end_recover_snapshot();
	space_end_recover();

	title("orphan", NULL);
	recovery_follow_local(recovery_state,
			      cfg_getd("wal_dir_rescan_delay"));
	title("hot_standby", NULL);

	const char *primary_port = cfg_gets("primary_port");
	const char *admin_port = cfg_gets("admin_port");

	/*
	 * application server configuration).
	 */
	if (!primary_port && !admin_port)
		box_leave_local_standby_mode(NULL);
	else {
		void (*on_bind)(void *) = NULL;
		if (primary_port) {
			iproto_init(primary_port);
		} else {
			/*
			 * If no prmary port is given, leave local
			 * host standby mode as soon as bound to the
			 * admin port. Otherwise, wait till we're
			 * bound to the master port.
			 */
			on_bind = box_leave_local_standby_mode;
		}
		if (admin_port)
			admin_init(admin_port, on_bind);
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
	snapshot_save(recovery_state);

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
