#ifndef TARANTOOL_REPLICA_H_INCLUDED
#define TARANTOOL_REPLICA_H_INCLUDED
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
#include <netinet/in.h>
#include "tarantool_ev.h"
#include <uri.h>

enum { REMOTE_SOURCE_MAXLEN = 1024 }; /* enough to fit URI with passwords */

/** Master connection */
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

/** Connect to a master and request a snapshot.
 * Raises an exception on error.
 *
 * @return A connected socket, ready too receive
 * data.
 */
void
replica_bootstrap(struct recovery_state *r);

void
recovery_follow_remote(struct recovery_state *r);

void
recovery_stop_remote(struct recovery_state *r);

void
recovery_set_remote(struct recovery_state *r, const char *source);

bool
recovery_has_remote(struct recovery_state *r);

#endif /* TARANTOOL_REPLICA_H_INCLUDED */
