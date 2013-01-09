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
#include "session.h"
#include "fiber.h"

#include "assoc.h"
#include "exception.h"

uint32_t sid_max;

static struct mh_i32ptr_t *session_registry;

struct session_trigger session_on_connect;
struct session_trigger session_on_disconnect;

uint32_t
session_create(int fd)
{
	/* Return the next sid rolling over the reserved value of 0. */
	while (++sid_max == 0)
		;

	uint32_t sid = sid_max;

	mh_int_t k = mh_i32ptr_put(session_registry, sid,
				   (void *) (intptr_t) fd, NULL);

	if (k == mh_end(session_registry)) {
		tnt_raise(ClientError, :ER_MEMORY_ISSUE,
			  "session hash", "new session");
	}
	/*
	 * Run the trigger *after* setting the current
	 * fiber sid.
	 */
	fiber_set_sid(fiber, sid);
	if (session_on_connect.trigger) {
		void *param = session_on_connect.param;
		@try {
			session_on_connect.trigger(param);
		} @catch (tnt_Exception *e) {
			fiber_set_sid(fiber, 0);
			mh_i32ptr_remove(session_registry, sid);
			@throw;
		}
	}

	return sid;
}

void
session_destroy(uint32_t sid)
{
	if (sid == 0) /* no-op for a dead session. */
		return;

	if (session_on_disconnect.trigger) {
		void *param = session_on_disconnect.param;
		@try {
			session_on_disconnect.trigger(param);
		} @catch (tnt_Exception *e) {
			[e log];
		} @catch (id e) {
			/* catch all. */
		}
	}
	mh_i32ptr_remove(session_registry, sid);
}

int
session_fd(uint32_t sid)
{
	mh_int_t k = mh_i32ptr_get(session_registry, sid);
	return k == mh_end(session_registry) ?
		-1 : (intptr_t) mh_value(session_registry, k);
}

void
session_init()
{
	session_registry = mh_i32ptr_init();
	if (session_registry == NULL)
		panic("out of memory");
}

void
session_free()
{
	if (session_registry)
		mh_i32ptr_destroy(session_registry);
}
