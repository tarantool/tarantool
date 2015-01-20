#ifndef TARANTOOL_RECOVERY_H_INCLUDED
#define TARANTOOL_RECOVERY_H_INCLUDED
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
#include <stdbool.h>
#include <netinet/in.h>

#include "trivia/util.h"
#include "third_party/tarantool_ev.h"
#include "xlog.h"
#include "vclock.h"
#include "tt_uuid.h"
#include "uri.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct fiber;
struct tbuf;
struct recovery_state;

typedef void (apply_row_f)(struct recovery_state *, void *,
			   struct xrow_header *packet);

/** A "condition variable" that allows fibers to wait when a given
 * LSN makes it to disk.
 */

struct wal_writer;
struct wal_watcher;

enum wal_mode { WAL_NONE = 0, WAL_WRITE, WAL_FSYNC, WAL_MODE_MAX };

/** String constants for the supported modes. */
extern const char *wal_mode_STRS[];

/** State of a replication relay. */
struct relay {
	/** Replica connection */
	int sock;
	/* Request type - SUBSCRIBE or JOIN */
	uint32_t type;
	/* Request sync */
	uint64_t sync;
	/* Only used in SUBSCRIBE request */
	uint32_t server_id;
	struct vclock vclock;
};

enum { REMOTE_SOURCE_MAXLEN = 1024 }; /* enough to fit URI with passwords */

/** State of a replication connection to the master */
struct remote {
	struct fiber *reader;
	ev_tstamp recovery_lag, recovery_last_update_tstamp;
	bool warning_said;
	char source[REMOTE_SOURCE_MAXLEN];
	struct uri uri;
	union {
		struct sockaddr addr;
		struct sockaddr_storage addrstorage;
	};
	socklen_t addr_len;
};

/**
 * This is used in local hot standby or replication
 * relay mode: look for changes in the wal_dir and apply them
 * locally or send to the replica.
 */
struct wal_watcher {
	/**
	 * Rescan the WAL directory in search for new WAL files
	 * every wal_dir_rescan_delay seconds.
	 */
	ev_timer dir_timer;
	/**
	 * When the latest WAL does not contain a EOF marker,
	 * re-read its tail on every change in file metadata.
	 */
	ev_stat stat;
	/** Path to the file being watched with 'stat'. */
	char filename[PATH_MAX+1];
};

struct recovery_state {
	struct vclock vclock;
	/** The WAL we're currently reading/writing from/to. */
	struct xlog *current_wal;
	struct xdir snap_dir;
	struct xdir wal_dir;
	/** Used to find missing xlog files */
	int64_t signature;
	struct wal_writer *writer;
	struct wal_watcher watcher;
	union {
		/** slave->master state */
		struct remote remote;
		/** master->slave state */
		struct relay relay;
	};
	/**
	 * apply_row is a module callback invoked during initial
	 * recovery and when reading rows from the master.
	 */
	apply_row_f *apply_row;
	void *apply_row_param;
	uint64_t snap_io_rate_limit;
	enum wal_mode wal_mode;
	struct tt_uuid server_uuid;
	uint32_t server_id;

	bool finalize;
};

struct recovery_state *
recovery_new(const char *snap_dirname, const char *wal_dirname,
	     apply_row_f apply_row, void *apply_row_param);

void
recovery_delete(struct recovery_state *r);

void
recovery_atfork(struct recovery_state *r);

void recovery_update_mode(struct recovery_state *r, enum wal_mode mode);
void recovery_update_io_rate_limit(struct recovery_state *r,
				   double new_limit);

static inline bool
recovery_has_data(struct recovery_state *r)
{
	return vclockset_first(&r->snap_dir.index) != NULL ||
	       vclockset_first(&r->wal_dir.index) != NULL;
}
void recovery_bootstrap(struct recovery_state *r);
void recover_snap(struct recovery_state *r);
void recovery_follow_local(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay);
void recovery_finalize(struct recovery_state *r, int rows_per_wal);

int wal_write(struct recovery_state *r, struct xrow_header *packet);

void recovery_setup_panic(struct recovery_state *r, bool on_snap_error, bool on_wal_error);
void recovery_apply_row(struct recovery_state *r, struct xrow_header *packet);

struct fio_batch;

void
snapshot_write_row(struct recovery_state *r, struct xlog *l,
		   struct xrow_header *packet);

typedef void (snapshot_f)(struct xlog *);

void
snapshot_save(struct recovery_state *r, snapshot_f snapshot_handler);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_RECOVERY_H_INCLUDED */
