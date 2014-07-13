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
#include <box/box.h>

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
	char delim[SESSION_DELIM_SIZE + 1];
	/* \n must folow user-specified delimiter */
	int delim_len = snprintf(delim, sizeof(delim), "%s\n",
				 fiber()->session->delim);

	char *eol;
	while (in->pos == NULL ||
	       (eol = (char *) memmem(in->pos, in->end - in->pos, delim,
					 delim_len)) == NULL) {
		if (coio_bread(coio, in, 1) <= 0)
			return -1;
	}
	eol[0] = '\0';

	/* get interactive from package.loaded */
	try {
		lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
		lua_getfield(L, -1, "console");
		lua_getfield(L, -1, "eval");
		lua_pushlstring(L, in->pos, eol - in->pos);
		lua_call(L, lua_gettop(L) - 3, 1);
		size_t len;
		const char *out = lua_tolstring(L, -1, &len);
		coio_write(coio, out, len);
		in->pos = (eol + delim_len);
		lua_settop(L, 0);
	} catch (Exception *e) {
		throw;
	} catch (...) {
		/* Convert Lua error to a Tarantool exception. */
		const char *msg = lua_tostring(L, -1);
		tnt_raise(ClientError, ER_PROC_LUA, msg ? msg : "");
	}

	return 0;
}

static void
admin_handler(va_list ap)
{
	struct ev_io coio = va_arg(ap, struct ev_io);
	struct sockaddr *addr = va_arg(ap, struct sockaddr *);
	struct iobuf *iobuf = va_arg(ap, struct iobuf *);
	lua_State *L = lua_newthread(tarantool_L);
	LuarefGuard coro_guard(tarantool_L);
	/* Session stores authentication and transaction state.  */
	SessionGuardWithTriggers sesion_guard(coio.fd, *(uint64_t *) addr);

	auto scoped_guard = make_scoped_guard([&] {
		evio_close(loop(), &coio);
		iobuf_delete(iobuf);
	});

	for (;;) {
		if (admin_dispatch(&coio, iobuf, L) < 0)
			return;
		iobuf_reset(iobuf);
		fiber_gc();
	}
}

void
admin_init(const char *uri, void (*on_bind)(void *))
{
	if (!uri)
		return;
	static struct coio_service admin;
	coio_service_init(&admin, "admin", uri, admin_handler, NULL);
	if (on_bind)
		evio_service_on_bind(&admin.evio_service, on_bind, NULL);
	evio_service_start(&admin.evio_service);
}
