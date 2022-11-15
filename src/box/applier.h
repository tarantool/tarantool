#ifndef TARANTOOL_APPLIER_H_INCLUDED
#define TARANTOOL_APPLIER_H_INCLUDED
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

#include <netinet/in.h>
#include <sys/socket.h>
#include <tarantool_ev.h>

#include <small/ibuf.h>

#include "fiber_cond.h"
#include "iostream.h"
#include "trigger.h"
#include "trivia/util.h"
#include "tt_uuid.h"
#include "uri/uri.h"
#include "small/lsregion.h"
#include "cbus.h"

#include "xrow.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum { APPLIER_SOURCE_MAXLEN = 1024 }; /* enough to fit URI with passwords */

#define applier_STATE(_)                                             \
	_(APPLIER_OFF, 0)                                            \
	_(APPLIER_CONNECT, 1)                                        \
	_(APPLIER_CONNECTED, 2)                                      \
	_(APPLIER_AUTH, 3)                                           \
	_(APPLIER_READY, 4)                                          \
	_(APPLIER_FINAL_JOIN, 5)                                     \
	_(APPLIER_JOINED, 6)                                         \
	_(APPLIER_SYNC, 7)                                           \
	_(APPLIER_FOLLOW, 8)                                         \
	_(APPLIER_STOPPED, 9)                                        \
	_(APPLIER_DISCONNECTED, 10)                                  \
	_(APPLIER_LOADING, 11)                                       \
	_(APPLIER_FETCH_SNAPSHOT, 12)                                \
	_(APPLIER_FETCHED_SNAPSHOT, 13)                              \
	_(APPLIER_WAIT_SNAPSHOT, 14)                                 \
	_(APPLIER_REGISTER, 15)                                      \
	_(APPLIER_REGISTERED, 16)                                    \

/** States for the applier */
ENUM(applier_state, applier_STATE);
extern const char *applier_state_strs[];

/** A base message used in applier-thread <-> tx communication. */
struct applier_msg {
	struct cmsg base;
	/**
	 * The function to execute in applier fiber. cmsg->f refers to a simple
	 * callback which wakes up the destination applier, so a separate
	 * function pointer is needed to tell applier which function to execute
	 * upon the message receipt.
	 */
	cmsg_f f;
	/** The target applier this message is for. */
	struct applier *applier;
};

/** A message sent from applier thread to notify tx that applier is exiting. */
struct applier_exit_msg {
	struct applier_msg base;
	/**
	 * The thread's saved diag. To propagate exception to applier fiber in
	 * tx thread.
	 */
	struct diag diag;
};

/** A message carrying parsed transactions from applier thread to tx. */
struct applier_data_msg {
	struct applier_msg base;
	/** A list of read up transactions to be processed in tx thread. */
	struct stailq txs;
	/** A lsregion identifier of the last row allocated for this message. */
	int64_t lsr_id;
	/** A counter to limit the count of transactions per message. */
	int tx_cnt;
};

/** A message sent from tx to applier thread to trigger ACK. */
struct applier_ack_msg {
	struct cmsg base;
	/**
	 * Last written transaction timestamp.
	 * Set to replica::applier_txn_last_tm.
	 */
	double txn_last_tm;
	/** Last known raft term. */
	uint64_t term;
	/** Replicaset vclock. */
	struct vclock vclock;
	/** The vclock sync this message corresponds to. */
	uint64_t vclock_sync;
};

/** The underlying thread behind a number of appliers. */
struct applier_thread {
	struct cord cord;
	/** The single thread endpoint. */
	struct cbus_endpoint endpoint;
	/** A pipe from the applier thread to tx. */
	struct cpipe tx_pipe;
	/** A pipe from tx to the applier thread. */
	struct cpipe thread_pipe;
};

/**
 * State of a replication connection to the master
 */
struct applier {
	/** Background fiber */
	struct fiber *fiber;
	/** Finite-state machine */
	enum applier_state state;
	/** Local time of this replica when the last row has been received */
	ev_tstamp last_row_time;
	/** Number of seconds this replica is behind the remote master */
	ev_tstamp lag;
	/** The last box_error_code() logged to avoid log flooding */
	uint32_t last_logged_errcode;
	/** Remote instance ID. */
	uint32_t instance_id;
	/** Remote instance UUID */
	struct tt_uuid uuid;
	/** Remote URI (parsed) */
	struct uri uri;
	/** Remote version encoded as a number, see version_id() macro */
	uint32_t version_id;
	/** Remote instance features. */
	struct iproto_features features;
	/** Remote ballot at the time of connect. */
	struct ballot ballot;
	/** The fiber responsible for ballot updates. */
	struct fiber *ballot_watcher;
	/** Last requested vclock sync. */
	uint64_t last_vclock_sync;
	/** Remote address */
	union {
		struct sockaddr addr;
		struct sockaddr_storage addrstorage;
	};
	/** Length of addr */
	socklen_t addr_len;
	/** I/O stream context */
	struct iostream_ctx io_ctx;
	/** I/O stream */
	struct iostream io;
	/** Input buffer */
	struct ibuf ibuf;
	/** Triggers invoked on state change */
	struct rlist on_state;
	/** Triggers invoked on ballot update. */
	struct rlist on_ballot_update;
	/**
	 * Set if the applier was paused (see applier_pause()) and is now
	 * waiting on resume_cond to be resumed (see applier_resume()).
	 */
	bool is_paused;
	/** Condition variable signaled to resume the applier. */
	struct fiber_cond resume_cond;
	/* Diag to raise an error. */
	struct diag diag;
	/* Master's vclock at the time of SUBSCRIBE. */
	struct vclock remote_vclock_at_subscribe;
	/** A pointer to the thread handling this applier's data stream. */
	struct applier_thread *applier_thread;
	/**
	 * A conditional variable used by applier fiber in tx thread waiting for
	 * the next message.
	 */
	struct fiber_cond msg_cond;
	/**
	 * A tx thread queue for messages delivered from applier thread but not
	 * yet picked up by a corresponding applier.
	 * Applier thread has a pair of rotating messages used to deliver parsed
	 * transactions to the tx thread. It is possible, though, that an exit
	 * notification message arrives right after both data messages are
	 * pushed. Hence the queue size is 3.
	 */
	struct applier_msg *pending_msgs[3];
	/** Pending message count. */
	int pending_msg_cnt;
	/** Message sent to applier thread to trigger ACK. */
	struct applier_ack_msg ack_msg;
	struct cmsg_hop ack_route[2];
	/** True if ack_msg was sent and hasn't returned yet. */
	bool is_ack_sent;
	/** True if ACK was signalled in tx while ack_msg was en route. */
	bool is_ack_pending;
	/** Fields used only by applier thread. */
	struct {
		alignas(CACHELINE_SIZE)
		/**
		 * A pair of rotating messages. While one of the messages is
		 * processed in the tx thread the other is used to store
		 * incoming rows.
		 */
		struct applier_data_msg msgs[2];
		/** The input buffer used in thread to read rows. */
		struct ibuf ibuf;
		/** The lsregion for allocating rows in thread. */
		struct lsregion lsr;
		/** A growing identifier to track lsregion allocations. */
		int64_t lsr_id;
		/** An index of the message currently used in thread. */
		int msg_ptr;
		/**
		 * A preallocated message used to notify tx that the applier
		 * should exit due to a network error.
		 */
		struct applier_exit_msg exit_msg;
		/** The reader fiber, reading and parsing incoming rows. */
		struct fiber *reader;
		/** Background fiber to reply with vclock */
		struct fiber *writer;
		/** Writer cond. */
		struct fiber_cond writer_cond;
		/**
		 * True if the applier has vclocks not sent to the remote
		 * master. The flag is needed because during sending one
		 * vclock (ACK), it can be updated again. So just one
		 * condition variable is not enough.
		 */
		bool has_acks_to_send;
		/**
		 * Applier thread's view of the last written transaction
		 * timestamp. Sent in ACK messages. Updated by applier_ack_msg.
		 */
		double txn_last_tm;
		/**
		 * Appler's next ACK to send to relay. Updated by
		 * applier_ack_msg.
		 */
		struct applier_heartbeat next_ack;
	} thread;
};

/** Initialize the applier subsystem. */
void
applier_init(void);

/** Free the applier subsystem. */
void
applier_free(void);

/**
 * Start a client to a remote master using a background fiber.
 *
 * If recovery is finalized (i.e. r->writer != NULL) then the client
 * connect to a master and follow remote updates using SUBSCRIBE command.
 *
 * If recovery is not finalized (i.e. r->writer == NULL) then the
 * client connects to a master, downloads and processes
 * a checkpoint using JOIN command and then switches to 'follow'
 * mode.
 *
 * \sa fiber_start()
 */
void
applier_start(struct applier *applier);

/**
 * Stop a client.
 */
void
applier_stop(struct applier *applier);

/**
 * Allocate an instance of applier object, create applier and initialize
 * remote uri (copied to struct applier).
 *
 * @pre     the uri is a valid and checked one
 * @error   throws exception
 */
struct applier *
applier_new(struct uri *uri);

/**
 * Destroy and delete a applier.
 */
void
applier_delete(struct applier *applier);

/*
 * Resume execution of applier until \a state.
 */
void
applier_resume_to_state(struct applier *applier, enum applier_state state,
			double timeout);

/*
 * Resume execution of applier.
 */
void
applier_resume(struct applier *applier);

/*
 * Pause execution of applier.
 *
 * Note, in contrast to applier_resume() this function may
 * only be called by the applier fiber (e.g. from on_state
 * trigger).
 */
void
applier_pause(struct applier *applier);

/** Wait until the remote node's bootstrap leader uuid is known. */
void
applier_wait_bootstrap_leader_uuid_is_set(struct applier *applier);

/**
 * Return string, which represent remote URI for this
 * @a applier. This function uses `tt_static_buf` to
 * store the result.
 */
const char *
applier_uri_str(const struct applier *applier);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_APPLIER_H_INCLUDED */
