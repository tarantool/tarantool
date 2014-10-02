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
#include "box.h"
#include "cluster.h"
#include "recovery.h"
#include "exception.h"

/**
 * Globally unique identifier of this cluster.
 * A cluster is a set of connected replicas.
 */
tt_uuid cluster_id;

extern "C" struct vclock *
cluster_clock()
{
        return &recovery->vclock;
}

void
cluster_set_server(const tt_uuid *server_uuid, uint32_t server_id)
{
	struct recovery_state *r = recovery;
	/** Checked in the before-commit trigger */
	assert(!tt_uuid_is_nil(server_uuid));
	assert(!cserver_id_is_reserved(server_id));

	if (r->server_id == server_id) {
		if (tt_uuid_is_equal(&r->server_uuid, server_uuid))
			return;
		say_warn("server uuid changed to %s", tt_uuid_str(server_uuid));
		assert(vclock_has(&r->vclock, server_id));
		memcpy(&r->server_uuid, server_uuid, sizeof(*server_uuid));
		return;
	}

	/* Add server */
	vclock_add_server(&r->vclock, server_id);
	if (tt_uuid_is_equal(&r->server_uuid, server_uuid)) {
		/* Assign local server id */
		assert(r->server_id == 0);
		r->server_id = server_id;
		box_set_ro(false);
	}
}

void
cluster_del_server(uint32_t server_id)
{
	struct recovery_state *r = recovery;
	vclock_del_server(&r->vclock, server_id);
	if (r->server_id == server_id) {
		r->server_id = 0;
		box_set_ro(true);
	}
}
