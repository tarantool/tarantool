#ifndef TARANTOOL_REPLICA_H_INCLUDED
#define TARANTOOL_REPLICA_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include "trivia/util.h"
#include "uri.h"
#include "third_party/tarantool_ev.h"
#define RB_COMPACT 1
#include <third_party/rb.h>

struct recovery_state;

enum { REPLICA_SOURCE_MAXLEN = 1024 }; /* enough to fit URI with passwords */

#define replica_STATE(_)                                             \
	_(REPLICA_OFF, 0)                                            \
	_(REPLICA_CONNECT, 1)                                        \
	_(REPLICA_AUTH, 2)                                           \
	_(REPLICA_CONNECTED, 3)                                      \
	_(REPLICA_BOOTSTRAP, 4)                                      \
	_(REPLICA_FOLLOW, 5)                                         \
	_(REPLICA_STOPPED, 6)                                        \
	_(REPLICA_DISCONNECTED, 7)                                   \

/** States for the replica */
ENUM(replica_state, replica_STATE);
extern const char *replica_state_strs[];

/**
 * State of a replication connection to the master
 */
struct replica {
	struct fiber *reader;
	enum replica_state state;
	ev_tstamp lag, last_row_time;
	bool warning_said;
	char source[REPLICA_SOURCE_MAXLEN];
	rb_node(struct replica) link; /* a set by source in cluster.cc */
	struct uri uri;
	union {
		struct sockaddr addr;
		struct sockaddr_storage addrstorage;
	};
	socklen_t addr_len;
	/** Save master fd to re-use a connection between JOIN and SUBSCRIBE */
	struct ev_io io;
	/** Input/output buffer for buffered IO */
	struct iobuf *iobuf;
};

/**
 * Start a client to a remote server using a background fiber.
 *
 * If recovery is finalized (i.e. r->writer != NULL) then the client
 * connect to a master and follow remote updates using SUBSCRIBE command.
 *
 * If recovery is not finalized (i.e. r->writer == NULL) then the client
 * connect to a master, download and process snapshot using JOIN command
 * and then exits. The background fiber can be joined to get exit status
 * using replica_join().
 *
 * \pre A connection from io->fd is re-used.
 * \sa fiber_start()
 */
void
replica_start(struct replica *replica, struct recovery_state *r);

/**
 * Stop a client.
 */
void
replica_stop(struct replica *replica);

/**
 * Wait replication client to finish and rethrow exception (if any).
 * Use this function to wait until bootstrap.
 *
 * \post This function keeps a open connection in io->fd.
 * \sa replica_start()
 * \sa fiber_join()
 */
void
replica_wait(struct replica *replica);

/**
 * Allocate an instance of replica object, create replica and initialize
 * remote uri (copied to struct replica).
 *
 * @pre     the uri is a valid and checked one
 * @error   throws OutOfMemory exception if out of memory.
 */
struct replica *
replica_new(const char *uri);

/**
 * Destroy and delete a replica.
 */
void
replica_delete(struct replica *replica);

#endif /* TARANTOOL_REPLICA_H_INCLUDED */
