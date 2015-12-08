#ifndef TARANTOOL_APPLIER_H_INCLUDED
#define TARANTOOL_APPLIER_H_INCLUDED
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
#include "tt_uuid.h"
#include "trigger.h"
#include "third_party/tarantool_ev.h"
#include "vclock.h"
#include "ipc.h"

struct recovery;

enum { APPLIER_SOURCE_MAXLEN = 1024 }; /* enough to fit URI with passwords */

#define applier_STATE(_)                                             \
	_(APPLIER_OFF, 0)                                            \
	_(APPLIER_CONNECT, 1)                                        \
	_(APPLIER_AUTH, 2)                                           \
	_(APPLIER_CONNECTED, 3)                                      \
	_(APPLIER_BOOTSTRAP, 4)                                      \
	_(APPLIER_FOLLOW, 5)                                         \
	_(APPLIER_STOPPED, 6)                                        \
	_(APPLIER_DISCONNECTED, 7)                                   \

/** States for the applier */
ENUM(applier_state, applier_STATE);
extern const char *applier_state_strs[];

/**
 * State of a replication connection to the master
 */
struct applier {
	struct fiber *reader;
	enum applier_state state;
	ev_tstamp lag, last_row_time;
	bool warning_said;
	uint32_t id;
	struct tt_uuid uuid;
	char source[APPLIER_SOURCE_MAXLEN];
	struct uri uri;
	uint32_t version_id; /* remote version */
	struct vclock vclock;
	union {
		struct sockaddr addr;
		struct sockaddr_storage addrstorage;
	};
	socklen_t addr_len;
	struct ev_io io;
	/** Input/output buffer for buffered IO */
	struct iobuf *iobuf;
	/** Triggers invoked on state change */
	struct rlist on_state;
	/* Channel used by applier_connect_all() and applier_resume() */
	struct ipc_channel pause;
};

/**
 * Start a client to a remote server using a background fiber.
 *
 * If recovery is finalized (i.e. r->writer != NULL) then the client
 * connect to a master and follow remote updates using SUBSCRIBE command.
 *
 * If recovery is not finalized (i.e. r->writer == NULL) then the client
 * connect to a master, download and process snapshot using JOIN command
 * and then switch to follow mode.
 *
 * \sa fiber_start()
 */
void
applier_start(struct applier *applier, struct recovery *r);

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
 * @error   throws OutOfMemory exception if out of memory.
 */
struct applier *
applier_new(const char *uri);

/**
 * Destroy and delete a applier.
 */
void
applier_delete(struct applier *applier);

/*
 * Connect all appliers to remote peer and receive UUID
 * \post appliers are connected to remote hosts and paused.
 * Use applier_resume(applier) to resume applier.
 */
void
applier_connect_all(struct applier **appliers, int count,
		   struct recovery *recovery);

/*
 * Download and process the data snapshot from master.
 * \pre applier is paused && applier->state == APPLIER_CONNECTED
 * \post applier is paused && applier->state == APPLIER_CONNECTED
 * \sa applier_connect_all
 */
void
applier_bootstrap(struct applier *master);

/*
 * Resume execution of applier returned by applier_connect_all() or
 * applier_bootstrap().
 */
void
applier_resume(struct applier *applier);

#endif /* TARANTOOL_APPLIER_H_INCLUDED */
