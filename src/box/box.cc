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

extern "C" {
#include <cfg/warning.h>
#include <cfg/tarantool_box_cfg.h>
} /* extern "C" */

#include <errcode.h>
#include <recovery.h>
#include "replica.h"
#include <log_io.h>
#include <pickle.h>
#include <say.h>
#include <stat.h>
#include <tarantool.h>
#include "tuple.h"
#include "lua/call.h"
#include "schema.h"
#include "engine.h"
#include "engine_memtx.h"
#include "space.h"
#include "port.h"
#include "request.h"
#include "txn.h"
#include "fiber.h"
#include "access.h"

static void process_replica(struct port *port, struct request *request);
static void process_ro(struct port *port, struct request *request);
static void process_rw(struct port *port, struct request *request);
box_process_func box_process = process_ro;

static int stat_base;
int snapshot_pid = 0; /* snapshot processes pid */


/** The snapshot row metadata repeats the structure of REPLACE request. */
struct box_snap_row {
	uint16_t op;
	uint8_t m_body;
	uint8_t k_space_id;
	uint8_t m_space_id;
	uint32_t v_space_id;
	uint8_t k_tuple;
	char data[];
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
process_replica(struct port *port, struct request *request)
{
	if (!iproto_request_is_select(request->type)) {
		tnt_raise(ClientError, ER_NONMASTER,
			  cfg.replication_source);
	}
	return process_rw(port, request);
}

static void
process_ro(struct port *port, struct request *request)
{
	if (!iproto_request_is_select(request->type))
		tnt_raise(LoggedError, ER_SECONDARY);
	return process_rw(port, request);
}

static int
recover_row(void *param __attribute__((unused)), const struct log_row *row)
{
	try {
		const char *data = row->data;
		const char *end = data + row->len;
		uint16_t op = pick_u16(&data, end);
		struct request request;
		request_create(&request, op);
		request_decode(&request, data, end - data);
		process_rw(&null_port, &request);
	} catch (Exception *e) {
		e->log();
		return -1;
	}

	return 0;
}

static void
box_enter_master_or_replica_mode(struct tarantool_cfg *conf)
{
	if (conf->replication_source != NULL) {
		box_process = process_replica;

		recovery_wait_lsn(recovery_state, recovery_state->lsn);
		recovery_follow_remote(recovery_state,
				       conf->replication_source);

	} else {
		box_process = process_rw;
		title("primary", NULL);
		say_info("I am primary");
	}
}

void
box_leave_local_standby_mode(void *data __attribute__((unused)))
{
	recovery_finalize(recovery_state);

	recovery_update_mode(recovery_state, cfg.wal_mode,
			     cfg.wal_fsync_delay);

	box_enter_master_or_replica_mode(&cfg);
}

int
box_check_config(struct tarantool_cfg *conf)
{
	if (strindex(wal_mode_STRS, conf->wal_mode,
		     WAL_MODE_MAX) == WAL_MODE_MAX) {
		out_warning(CNF_OK, "wal_mode %s is not recognized", conf->wal_mode);
		return -1;
	}
	/* replication & hot standby modes can not work together */
	if (conf->replication_source != NULL && conf->local_hot_standby > 0) {
		out_warning(CNF_OK, "replication and local hot standby modes "
			       "can't be enabled simultaneously");
		return -1;
	}

	/* check replication mode */
	if (conf->replication_source != NULL) {
		/* check replication port */
		char ip_addr[32];
		int port;

		if (sscanf(conf->replication_source, "%31[^:]:%i",
			   ip_addr, &port) != 2) {
			out_warning(CNF_OK, "replication source IP address is not recognized");
			return -1;
		}
		if (port <= 0 || port >= USHRT_MAX) {
			out_warning(CNF_OK, "invalid replication source port value: %i", port);
			return -1;
		}
	}

	/* check primary port */
	if (conf->primary_port != 0 &&
	    (conf->primary_port <= 0 || conf->primary_port >= USHRT_MAX)) {
		out_warning(CNF_OK, "invalid primary port value: %i", conf->primary_port);
		return -1;
	}

	/* check rows_per_wal configuration */
	if (conf->rows_per_wal <= 1) {
		out_warning(CNF_OK, "rows_per_wal must be greater than one");
		return -1;
	}

	return 0;
}

int
box_reload_config(struct tarantool_cfg *old_conf, struct tarantool_cfg *new_conf)
{
	if (strcasecmp(old_conf->wal_mode, new_conf->wal_mode) != 0 ||
	    old_conf->wal_fsync_delay != new_conf->wal_fsync_delay) {

		double new_delay = new_conf->wal_fsync_delay;

		/* Mode has changed: */
		if (strcasecmp(old_conf->wal_mode, new_conf->wal_mode)) {
			if (strcasecmp(old_conf->wal_mode, "fsync") == 0 ||
			    strcasecmp(new_conf->wal_mode, "fsync") == 0) {
				out_warning(CNF_OK, "wal_mode cannot switch to/from fsync");
				return -1;
			}
		}

		/*
		 * Unless wal_mode=fsync_delay, wal_fsync_delay is
		 * irrelevant and must be 0.
		 */
		if (strcasecmp(new_conf->wal_mode, "fsync_delay") != 0)
			new_delay = 0.0;


		recovery_update_mode(recovery_state, new_conf->wal_mode, new_delay);
	}

	if (old_conf->snap_io_rate_limit != new_conf->snap_io_rate_limit)
		recovery_update_io_rate_limit(recovery_state, new_conf->snap_io_rate_limit);
	bool old_is_replica = old_conf->replication_source != NULL;
	bool new_is_replica = new_conf->replication_source != NULL;

	if (old_is_replica != new_is_replica ||
	    (old_is_replica &&
	     (strcmp(old_conf->replication_source, new_conf->replication_source) != 0))) {

		if (recovery_state->finalize != true) {
			out_warning(CNF_OK, "Could not propagate %s before local recovery finished",
				    old_is_replica == true ? "slave to master" :
				    "master to slave");

			return -1;
		}
		if (recovery_state->remote) {
			recovery_stop_remote(recovery_state);
		}

		box_enter_master_or_replica_mode(new_conf);
	}

	return 0;
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
box_engine_init()
{
	MEMTX *memtx = new MEMTX();
	engine_register(memtx);
}

void
box_init()
{
	title("loading", NULL);
	stat_init();

	tuple_init(cfg.slab_alloc_arena, cfg.slab_alloc_minimal,
		   cfg.slab_alloc_factor);

	box_engine_init();

	schema_init();
	user_cache_init();

	/* recovery initialization */
	recovery_init(cfg.snap_dir, cfg.wal_dir,
		      recover_row, NULL, cfg.rows_per_wal);
	recovery_update_io_rate_limit(recovery_state, cfg.snap_io_rate_limit);
	recovery_setup_panic(recovery_state, cfg.panic_on_snap_error, cfg.panic_on_wal_error);

	stat_base = stat_register(iproto_request_type_strs,
				  IPROTO_DML_REQUEST_MAX);

	recover_snap(recovery_state, cfg.replication_source);
	space_end_recover_snapshot();
	recover_existing_wals(recovery_state);
	space_end_recover();

	stat_cleanup(stat_base, IPROTO_DML_REQUEST_MAX);
	title("orphan", NULL);
	if (cfg.local_hot_standby) {
		say_info("starting local hot standby");
		recovery_follow_local(recovery_state, cfg.wal_dir_rescan_delay);
		title("hot_standby", NULL);
	}
}

static void
snapshot_write_tuple(struct log_io *l,
		     uint32_t n, struct tuple *tuple)
{
	struct box_snap_row header;
	header.op = IPROTO_INSERT;
	header.m_body = 0x82; /* map of two elements. */
	header.k_space_id = IPROTO_SPACE_ID;
	header.m_space_id = 0xce; /* uint32 */
	header.v_space_id = mp_bswap_u32(n);
	header.k_tuple = IPROTO_TUPLE;
	snapshot_write_row(l, (const char *) &header, sizeof(header),
	                   tuple->data, tuple->bsize);
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

void
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
	snapshot_save(recovery_state, box_snapshot_cb);

	exit(EXIT_SUCCESS);
	return 0;
}

void
box_init_storage(const char *dirname)
{
	struct log_dir dir = snap_dir;
	dir.dirname = (char *) dirname;
	init_storage(&dir, NULL);
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
