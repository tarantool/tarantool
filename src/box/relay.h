#ifndef TARANTOOL_REPLICATION_RELAY_H_INCLUDED
#define TARANTOOL_REPLICATION_RELAY_H_INCLUDED
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
#include "trivia/config.h"
#include "trivia/util.h"

#include "fiber.h"
#include "cbus.h"
#include "vclock.h"
#include "xstream.h"

struct replica;
struct tt_uuid;

struct relay;

/**
 * Cbus message to send status updates from Relay to TX
 */
struct relay_status_msg {
	/** Parent */
	struct cmsg msg;
	/** Relay instance */
	struct relay *relay;
	/** New vclock */
	struct vclock vclock;
};

/**
 * Cbus message to notify TX thread that relay is stopping.
 */
struct relay_exit_msg {
	/** Parent */
	struct cmsg msg;
	/** Relay instance */
	struct relay *relay;
};

/** State of a replication relay. */
struct relay {
	/** The thread in which we relay data to the replica. */
	struct cord cord;
	/** Replica connection */
	struct ev_io io;
	/** Request sync */
	uint64_t sync;
	/** Recovery instance to read xlog from the disk */
	struct recovery *r;
	/** Xstream argument to recovery */
	struct xstream stream;
	/** Vclock to stop playing xlogs */
	struct vclock stop_vclock;
	/** Directory rescan delay for recovery */
	ev_tstamp wal_dir_rescan_delay;
	/** Remote replica id */
	uint32_t replica_id;

	/** Relay endpoint */
	struct cbus_endpoint endpoint;
	/** A pipe from 'relay' thread to 'tx' */
	struct cpipe tx_pipe;
	/** A pipe from 'tx' thread to 'relay' */
	struct cpipe relay_pipe;
	/** Status message */
	struct relay_status_msg status_msg;
	/** A condition to signal when status message is handled. */
	struct ipc_cond status_cond;
	/** Relay exit orchestration message */
	struct relay_exit_msg exit_msg;

	struct {
		/* Align to prevent false-sharing with TX thread */
		alignas(CACHELINE_SIZE)
		/** Current vclock sent by relay */
		struct vclock vclock;
		/** The condition will be signaled if relay want to exit. */
		struct ipc_cond exit_cond;
	} tx;
};

/**
 * Send initial JOIN rows to the replica
 *
 * @param fd        client connection
 * @param sync      sync from incoming JOIN request
 * @param vclock    vclock of the last checkpoint
 */
void
relay_initial_join(int fd, uint64_t sync, struct vclock *vclock);

/**
 * Send final JOIN rows to the replica.
 *
 * @param fd        client connection
 * @param sync      sync from incoming JOIN request
 */
void
relay_final_join(int fd, uint64_t sync, struct vclock *start_vclock,
	         struct vclock *stop_vclock);

/**
 * Subscribe a replica to updates.
 *
 * @return none.
 */
void
relay_subscribe(int fd, uint64_t sync, struct replica *replica,
		struct vclock *replica_vclock);

#endif /* TARANTOOL_REPLICATION_RELAY_H_INCLUDED */
