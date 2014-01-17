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
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

#include "fiber.h"
#include "tarantool.h"
#include "lua/init.h"
#include "tbuf.h"
#include "trivia/config.h"
#include "trivia/util.h"
#include "coio_buf.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "lua/utils.h"
#include "session.h"
#include "scoped_guard.h"

static int
admin_dispatch(struct ev_io *coio, struct iobuf *iobuf, lua_State *L)
{
	struct ibuf *in = &iobuf->in;
	struct tbuf *out = tbuf_new(&fiber()->gc);
	char *eol;
	while ((eol = (char *) memchr(in->pos, '\n', in->end - in->pos)) == NULL) {
		if (coio_bread(coio, in, 1) <= 0)
			return -1;
	}
	eol[0] = '\0';
	tarantool_lua(L, out, in->pos);
	in->pos = (eol + 1);
	coio_write(coio, out->data, out->size);
	return 0;
}

static void
admin_handler(va_list ap)
{
	struct ev_io coio = va_arg(ap, struct ev_io);
	struct sockaddr_in *addr = va_arg(ap, struct sockaddr_in *);
	struct iobuf *iobuf = va_arg(ap, struct iobuf *);
	lua_State *L = lua_newthread(tarantool_L);
	LuarefGuard coro_guard(tarantool_L);

	auto scoped_guard = make_scoped_guard([&] {
		evio_close(&coio);
		iobuf_delete(iobuf);
		session_destroy(fiber()->session);
	});

	/*
	 * Admin and iproto connections must have a
	 * session object, representing the state of
	 * a remote client: it's used in Lua
	 * stored procedures.
	 */
	(void) session_create(coio.fd, *(uint64_t *) addr);

	for (;;) {
		if (admin_dispatch(&coio, iobuf, L) < 0)
			return;
		iobuf_gc(iobuf);
		fiber_gc();
	}
}

void
admin_init(const char *bind_ipaddr, int admin_port)
{
	static struct coio_service admin;
	coio_service_init(&admin, "admin", bind_ipaddr,
			  admin_port, admin_handler, NULL);
	evio_service_start(&admin.evio_service);
}
