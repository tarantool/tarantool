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

#include <cfg/warning.h>
#include <errcode.h>
#include "palloc.h"
#include <recovery.h>
#include <log_io.h>
#include <pickle.h>
#include <say.h>
#include <stat.h>
#include <tarantool.h>

#include <cfg/tarantool_box_cfg.h>
#include "tuple.h"
#include "memcached.h"
#include "box_lua.h"
#include "space.h"
#include "port.h"
#include "request.h"
#include "txn.h"

static void process_replica(struct port *port,
			    u32 op, struct tbuf *request_data);
static void process_ro(struct port *port,
		       u32 op, struct tbuf *request_data);
static void process_rw(struct port *port,
		       u32 op, struct tbuf *request_data);
box_process_func box_process = process_ro;
box_process_func box_process_ro = process_ro;

static char status[64] = "unknown";

static int stat_base;

struct box_snap_row {
	u32 space;
	u32 tuple_size;
	u32 data_size;
	u8 data[];
} __attribute__((packed));

static inline struct box_snap_row *
box_snap_row(const struct tbuf *t)
{
	return (struct box_snap_row *)t->data;
}

void
port_send_tuple(struct port *port, struct txn *txn, u32 flags)
{
	struct tuple *tuple;
	if ((tuple = txn->new_tuple) || (tuple = txn->old_tuple))
		port_add_tuple(port, tuple, flags);
}

static void
process_rw(struct port *port, u32 op, struct tbuf *data)
{
	struct txn *txn = txn_begin();
	ev_tstamp start = ev_now(), stop;

	@try {
		struct request *request = request_create(op, data);
		stat_collect(stat_base, op, 1);
		request_execute(request, txn, port);
		txn_commit(txn);
		port_send_tuple(port, txn, request->flags);
		port_eof(port);
		txn_finish(txn);
	} @catch (id e) {
		txn_rollback(txn);
		@throw;
	} @finally {
		stop = ev_now();
		if (stop - start > cfg.too_long_threshold)
			say_warn("too long %s: %.3f sec",
				 request_name(op), stop - start);
	}
}

static void
process_replica(struct port *port, u32 op, struct tbuf *request_data)
{
	if (!request_is_select(op)) {
		tnt_raise(ClientError, :ER_NONMASTER,
			  cfg.replication_source);
	}
	return process_rw(port, op, request_data);
}

static void
process_ro(struct port *port, u32 op, struct tbuf *request_data)
{
	if (!request_is_select(op))
		tnt_raise(LoggedError, :ER_SECONDARY);
	return process_rw(port, op, request_data);
}

static void
box_xlog_sprint(struct tbuf *buf, const struct tbuf *t)
{
	struct header_v11 *row = header_v11(t);

	struct tbuf *b = palloc(buf->pool, sizeof(*b));
	b->data = t->data + sizeof(struct header_v11);
	b->size = row->len;
	u16 tag, op;
	u64 cookie;
	struct sockaddr_in *peer = (void *)&cookie;

	u32 n, key_len;
	void *key;
	u32 field_count, field_no;
	u32 flags;
	u32 op_cnt;

	tbuf_printf(buf, "lsn:%" PRIi64 " ", row->lsn);

	say_debug("b->len:%" PRIu32, b->size);

	tag = read_u16(b);
	cookie = read_u64(b);
	op = read_u16(b);
	n = read_u32(b);

	tbuf_printf(buf, "tm:%.3f t:%" PRIu16 " %s:%d %s n:%i",
		    row->tm, tag, inet_ntoa(peer->sin_addr), ntohs(peer->sin_port),
		    requests_strs[op], n);

	switch (op) {
	case REPLACE:
		flags = read_u32(b);
		field_count = read_u32(b);
		if (b->size != valid_tuple(b, field_count))
			abort();
		tuple_print(buf, field_count, b->data);
		break;

	case DELETE:
		flags = read_u32(b);
	case DELETE_1_3:
		key_len = read_u32(b);
		key = read_field(b);
		if (b->size != 0)
			abort();
		tuple_print(buf, key_len, key);
		break;

	case UPDATE:
		flags = read_u32(b);
		key_len = read_u32(b);
		key = read_field(b);
		op_cnt = read_u32(b);

		tbuf_printf(buf, "flags:%08X ", flags);
		tuple_print(buf, key_len, key);

		while (op_cnt-- > 0) {
			field_no = read_u32(b);
			u8 op = read_u8(b);
			void *arg = read_field(b);

			tbuf_printf(buf, " [field_no:%i op:", field_no);
			switch (op) {
			case 0:
				tbuf_printf(buf, "set ");
				break;
			case 1:
				tbuf_printf(buf, "add ");
				break;
			case 2:
				tbuf_printf(buf, "and ");
				break;
			case 3:
				tbuf_printf(buf, "xor ");
				break;
			case 4:
				tbuf_printf(buf, "or ");
				break;
			}
			tuple_print(buf, 1, arg);
			tbuf_printf(buf, "] ");
		}
		break;
	default:
		tbuf_printf(buf, "unknown wal op %" PRIi32, op);
	}
}

static int
snap_print(void *param __attribute__((unused)), struct tbuf *t)
{
	@try {
		struct tbuf *out = tbuf_alloc(t->pool);
		struct header_v11 *raw_row = header_v11(t);
		struct tbuf *b = palloc(t->pool, sizeof(*b));
		b->data = t->data + sizeof(struct header_v11);
		b->size = raw_row->len;

		(void)read_u16(b); /* drop tag */
		(void)read_u64(b); /* drop cookie */

		struct box_snap_row *row =  box_snap_row(b);

		tuple_print(out, row->tuple_size, row->data);
		printf("n:%i %*s\n", row->space, (int) out->size,
		       (char *)out->data);
	} @catch (id e) {
		return -1;
	}
	return 0;
}

static int
xlog_print(void *param __attribute__((unused)), struct tbuf *t)
{
	@try {
		struct tbuf *out = tbuf_alloc(t->pool);
		box_xlog_sprint(out, t);
		printf("%*s\n", (int)out->size, (char *)out->data);
	} @catch (id e) {
		return -1;
	}
	return 0;
}

static void
recover_snap_row(struct tbuf *t)
{
	assert(primary_indexes_enabled == false);

	struct box_snap_row *row = box_snap_row(t);

	struct tuple *tuple = tuple_alloc(row->data_size);
	memcpy(tuple->data, row->data, row->data_size);
	tuple->field_count = row->tuple_size;

	struct space *space = space_find(row->space);
	Index *index = space_index(space, 0);
	/* Check to see if the tuple has a sufficient number of fields. */
	if (unlikely(tuple->field_count < space->max_fieldno)) {
		tnt_raise(IllegalParams, :"tuple must have all indexed fields");
	}
	[index buildNext: tuple];
	tuple_ref(tuple, 1);
}

static int
recover_row(void *param __attribute__((unused)), struct tbuf *t)
{
	/* drop wal header */
	if (tbuf_peek(t, sizeof(struct header_v11)) == NULL) {
		say_error("incorrect row header: expected %zd, got %zd bytes",
			  sizeof(struct header_v11), (size_t) t->size);
		return -1;
	}

	@try {
		u16 tag = read_u16(t);
		read_u64(t); /* drop cookie */
		if (tag == SNAP) {
			recover_snap_row(t);
		} else if (tag == XLOG) {
			u16 op = read_u16(t);
			process_rw(&port_null, op, t);
		} else {
			say_error("unknown row tag: %i", (int)tag);
			return -1;
		}
	} @catch (tnt_Exception *e) {
		[e log];
		return -1;
	} @catch (id e) {
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
		recovery_follow_remote(recovery_state, conf->replication_source);

		snprintf(status, sizeof(status), "replica/%s%s",
			 conf->replication_source, custom_proc_title);
		title("replica/%s%s", conf->replication_source, custom_proc_title);
	} else {
		box_process = process_rw;

		memcached_start_expire();

		snprintf(status, sizeof(status), "primary%s", custom_proc_title);
		title("primary%s", custom_proc_title);

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

i32
box_check_config(struct tarantool_cfg *conf)
{
	/* replication & hot standby modes can not work together */
	if (conf->replication_source != NULL && conf->local_hot_standby > 0) {
		out_warning(0, "replication and local hot standby modes "
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
			out_warning(0, "replication source IP address is not recognized");
			return -1;
		}
		if (port <= 0 || port >= USHRT_MAX) {
			out_warning(0, "invalid replication source port value: %i", port);
			return -1;
		}
	}

	/* check primary port */
	if (conf->primary_port != 0 &&
	    (conf->primary_port <= 0 || conf->primary_port >= USHRT_MAX)) {
		out_warning(0, "invalid primary port value: %i", conf->primary_port);
		return -1;
	}

	/* check secondary port */
	if (conf->secondary_port != 0 &&
	    (conf->secondary_port <= 0 || conf->secondary_port >= USHRT_MAX)) {
		out_warning(0, "invalid secondary port value: %i", conf->primary_port);
		return -1;
	}

	/* check if at least one space is defined */
	if (conf->space == NULL && conf->memcached_port == 0) {
		out_warning(0, "at least one space or memcached port must be defined");
		return -1;
	}

	/* check configured spaces */
	if (check_spaces(conf) != 0) {
		return -1;
	}

	/* check memcached configuration */
	if (memcached_check_config(conf) != 0) {
		return -1;
	}

	return 0;
}

i32
box_reload_config(struct tarantool_cfg *old_conf, struct tarantool_cfg *new_conf)
{
	bool old_is_replica = old_conf->replication_source != NULL;
	bool new_is_replica = new_conf->replication_source != NULL;

	if (old_is_replica != new_is_replica ||
	    (old_is_replica &&
	     (strcmp(old_conf->replication_source, new_conf->replication_source) != 0))) {

		if (recovery_state->finalize != true) {
			out_warning(0, "Could not propagate %s before local recovery finished",
				    old_is_replica == true ? "slave to master" :
				    "master to slave");

			return -1;
		}

		if (!old_is_replica && new_is_replica)
			memcached_stop_expire();

		if (recovery_state->remote)
			recovery_stop_remote(recovery_state);

		box_enter_master_or_replica_mode(new_conf);
	}

	return 0;
}

void
box_free(void)
{
	space_free();
}

void
box_init(void)
{
	title("loading");
	atexit(box_free);

	/* initialization spaces */
	space_init();
	/* configure memcached space */
	memcached_space_init();

	/* recovery initialization */
	recovery_init(cfg.snap_dir, cfg.wal_dir,
		      recover_row, NULL,
		      cfg.rows_per_wal,
		      init_storage ? RECOVER_READONLY : 0);
	recovery_update_io_rate_limit(recovery_state, cfg.snap_io_rate_limit);
	recovery_setup_panic(recovery_state, cfg.panic_on_snap_error, cfg.panic_on_wal_error);

	stat_base = stat_register(requests_strs, requests_MAX);

	if (init_storage)
		return;

	begin_build_primary_indexes();
	recover_snap(recovery_state);
	end_build_primary_indexes();
	recover_existing_wals(recovery_state);

	stat_cleanup(stat_base, requests_MAX);

	say_info("building secondary indexes");
	build_secondary_indexes();
	title("orphan");
	if (cfg.local_hot_standby) {
		say_info("starting local hot standby");
		recovery_follow_local(recovery_state, cfg.wal_dir_rescan_delay);
		snprintf(status, sizeof(status), "hot_standby");
		title("hot_standby");
	}
}

int
box_cat(const char *filename)
{
	return read_log(filename, xlog_print, snap_print, NULL);
}

static void
snapshot_write_tuple(struct log_io *l, struct fio_batch *batch,
		     unsigned n, struct tuple *tuple)
{
	if (tuple->flags & GHOST)	// do not save fictive rows
		return;

	struct box_snap_row header;
	header.space = n;
	header.tuple_size = tuple->field_count;
	header.data_size = tuple->bsize;

	snapshot_write_row(l, batch, (void *) &header, sizeof(header),
			   tuple->data, tuple->bsize);
}


static void
snapshot_space(struct space *sp, void *udata)
{
	struct tuple *tuple;
	struct { struct log_io *l; struct fio_batch *batch; } *ud = udata;
	Index *pk = space_index(sp, 0);
	struct iterator *it = pk->position;
	[pk initIterator: it :ITER_FORWARD];

	while ((tuple = it->next(it)))
		snapshot_write_tuple(ud->l, ud->batch, space_n(sp), tuple);
}

void
box_snapshot(struct log_io *l, struct fio_batch *batch)
{
	/* --init-storage switch */
	if (primary_indexes_enabled == false)
		return;

	struct { struct log_io *l; struct fio_batch *batch; } ud = { l, batch };

	space_foreach(snapshot_space, &ud);
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


uint32_t
box_sid()
{
	static uint32_t sid = 1;
	return sid++;
}
