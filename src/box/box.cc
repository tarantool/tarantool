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
#include "replica.h"
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

static void
box_enter_master_or_replica_mode(const char *replication_source)
{
	box_process = process_rw;
	if (replication_source != NULL) {
		recovery_follow_remote(recovery_state, replication_source);
	} else {
		title("primary", NULL);
		say_info("I am primary");
	}
}

/* {{{ configuration bindings */

void
box_check_replication_source(const char *source)
{
	if (source == NULL)
		return;
	/* check replication port */
	char ip_addr[32];
	int port;
	if (sscanf(source, "%31[^:]:%i", ip_addr, &port) != 2) {
		tnt_raise(ClientError, ER_CFG,
			  "replication source IP address is not recognized");
	}
	if (port <= 0 || port >= USHRT_MAX) {
		tnt_raise(ClientError, ER_CFG,
			  "invalid replication source port");
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
	/* check replication mode */
	box_check_replication_source(cfg_gets("replication_source"));

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
	bool old_is_replica = recovery_state->remote;
	bool new_is_replica = source != NULL;

	if (old_is_replica != new_is_replica ||
	    (old_is_replica &&
	     (strcmp(source, recovery_state->remote->source) != 0))) {

		if (recovery_state->finalize) {
			if (recovery_state->remote)
				recovery_stop_remote(recovery_state);
			box_enter_master_or_replica_mode(source);
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

	box_set_wal_mode(cfg_gets("wal_mode"));

	box_enter_master_or_replica_mode(cfg_gets("replication_source"));
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
	struct space *space = space_cache_find(SC_CLUSTER_ID);
	class Index *index = index_find(space, 0);
	struct iterator *it = index->position();
	index->initIterator(it, ITER_LE, NULL, 0);
	struct tuple *tuple = it->next(it);
	uint32_t node_id = tuple ? tuple_field_u32(tuple, 0) + 1 : 1;

	struct request req;
	request_create(&req, IPROTO_INSERT);
	req.space_id = SC_CLUSTER_ID;
	char buf[128];
	char *data = buf;
	data = mp_encode_array(data, 2);
	data = mp_encode_uint(data, node_id);
	data = mp_encode_str(data, tt_uuid_str(node_uuid), UUID_STR_LEN);
	assert(data <= buf + sizeof(buf));
	req.tuple = buf;
	req.tuple_end = data;
	process_rw(&null_port, &req);
}

static void
box_set_cluster_uuid()
{
	/* Save Cluster-UUID to _schema space */
	tt_uuid cluster_uuid;
	tt_uuid_create(&cluster_uuid);

	const char *key = "cluster";
	struct request req;
	request_create(&req, IPROTO_INSERT);
	req.space_id = SC_SCHEMA_ID;
	char buf[128];
	char *data = buf;
	data = mp_encode_array(data, 2);
	data = mp_encode_str(data, key, strlen(key));
	data = mp_encode_str(data, tt_uuid_str(&cluster_uuid), UUID_STR_LEN);
	assert(data <= buf + sizeof(buf));
	req.tuple = buf;
	req.tuple_end = data;

	process_rw(&null_port, &req);
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
	recovery_update_io_rate_limit(recovery_state,
				      cfg_getd("snap_io_rate_limit"));
	recovery_setup_panic(recovery_state,
			     cfg_geti("panic_on_snap_error"),
			     cfg_geti("panic_on_wal_error"));

	stat_base = stat_register(iproto_request_type_strs,
				  IPROTO_DML_REQUEST_MAX);

	const char *replication_source = cfg_gets("replication_source");
	if (recovery_has_data(recovery_state)) {
		/* Process existing snapshot */
		recover_snap(recovery_state);
	} else if (replication_source != NULL) {
		/* Initialize a new replica */
		replica_bootstrap(recovery_state, replication_source);
		snapshot_save(recovery_state);
	} else {
		/* Initialize a master node of a new cluster */
		cluster_bootstrap(recovery_state);
		box_set_cluster_uuid();
		snapshot_save(recovery_state);
	}

	space_end_recover_snapshot();
	space_end_recover();

	stat_cleanup(stat_base, IPROTO_DML_REQUEST_MAX);
	title("orphan", NULL);
	recovery_follow_local(recovery_state,
			      cfg_getd("wal_dir_rescan_delay"));
	title("hot_standby", NULL);
	const char *bind_ipaddr = cfg_gets("bind_ipaddr");
	int primary_port = cfg_geti("primary_port");
	int admin_port = cfg_geti("admin_port");
	/*
	 * application server configuration).
	 */
	if (primary_port == 0 && admin_port == 0)
		box_leave_local_standby_mode(NULL);
	else {
		void (*on_bind)(void *) = NULL;
		if (primary_port) {
			iproto_init(bind_ipaddr, primary_port);
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
			admin_init(bind_ipaddr, admin_port, on_bind);
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
	close_all_xcpt(1, sayfd);
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
