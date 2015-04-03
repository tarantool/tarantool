#ifndef TARANTOOL_REPLICATION_H_INCLUDED
#define TARANTOOL_REPLICATION_H_INCLUDED
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
#include "evio.h"

struct xrow_header;

/** State of a replication relay. */
class Relay {
public:
	/** Replica connection */
	struct ev_io io;
	/* Request sync */
	uint64_t sync;
	struct recovery_state *r;
	ev_tstamp wal_dir_rescan_delay;
	Relay(int fd_arg, uint64_t sync_arg);
	~Relay();
};

void
replication_join(int fd, struct xrow_header *packet);

/**
 * Subscribe a replica to updates.
 *
 * @return None. On error, closes the socket.
 */
void
replication_subscribe(int fd, struct xrow_header *packet);

void
relay_send(Relay *relay, struct xrow_header *packet);

#endif // TARANTOOL_REPLICATION_H_INCLUDED

