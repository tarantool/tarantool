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

#include <fiber.h>
#include <palloc.h>
#include <salloc.h>
#include <say.h>
#include <stat.h>
#include <tarantool.h>
#include "lua/init.h"
#include <recovery.h>
#include <tbuf.h>
#include "tarantool/util.h"
#include <errinj.h>
#include "coio_buf.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "box/box.h"
#include "lua/init.h"
#include "session.h"
#include "scoped_guard.h"
#include "box/space.h"

static const char *help =
	"available commands:" CRLF
	" - help" CRLF
	" - exit" CRLF
	" - show info" CRLF
	" - show fiber" CRLF
	" - show configuration" CRLF
	" - show index" CRLF
	" - show slab" CRLF
	" - show palloc" CRLF
	" - show stat" CRLF
	" - save coredump" CRLF
	" - save snapshot" CRLF
	" - lua command" CRLF
	" - reload configuration" CRLF
	" - show injections (debug mode only)" CRLF
	" - set injection <name> <state> (debug mode only)" CRLF;

static const char *unknown_command = "unknown command. try typing help." CRLF;

%%{
	machine admin;
	write data;
}%%

struct salloc_stat_admin_cb_ctx {
	int64_t total_used;
	int64_t total_used_real;
	int64_t total_alloc_real;
	struct tbuf *out;
};

static int
salloc_stat_admin_cb(const struct slab_cache_stats *cstat, void *cb_ctx)
{
	struct salloc_stat_admin_cb_ctx *ctx = (struct salloc_stat_admin_cb_ctx *) cb_ctx;

	tbuf_printf(ctx->out,
		    "     - { item_size: %6i, slabs: %6i, items: %11" PRIi64
		    ", bytes_used: %12" PRIi64 ", waste: %5.2f%%"
		    ", bytes_free: %12" PRIi64 " }" CRLF,
		    (int)cstat->item_size,
		    (int)cstat->slabs,
		    cstat->items,
		    cstat->bytes_used,
		    (double)(cstat->bytes_alloc_real - cstat->bytes_used_real)*100 /
		    (cstat->bytes_alloc_real + 0.001),
		    cstat->bytes_free);

	ctx->total_used += cstat->bytes_used;
	ctx->total_alloc_real += cstat->bytes_alloc_real;
	ctx->total_used_real += cstat->bytes_used_real;
	return 0;
}

static void
show_slab(struct tbuf *out)
{
	struct salloc_stat_admin_cb_ctx cb_ctx;
	struct slab_arena_stats astat;

	cb_ctx.total_used = 0;
	cb_ctx.total_used_real = 0;
	cb_ctx.total_alloc_real = 0;
	cb_ctx.out = out;

	tbuf_printf(out, "slab statistics:\n  classes:" CRLF);

	salloc_stat(salloc_stat_admin_cb, &astat, &cb_ctx);

	tbuf_printf(out, "  items_used: %.2f%%" CRLF,
		(double)cb_ctx.total_used / astat.size * 100);
	tbuf_printf(out, "  arena_used: %.2f%%" CRLF,
		(double)astat.used / astat.size * 100);
	tbuf_printf(out, "  waste: %.2f%%" CRLF,
		    (double)(cb_ctx.total_alloc_real - cb_ctx.total_used_real) / (cb_ctx.total_alloc_real + 0.001) * 100);
	tbuf_printf(out, "  bytes_waste: %12" PRIi64 CRLF,
		    (int64_t)((double)cb_ctx.total_used*(cb_ctx.total_alloc_real - cb_ctx.total_used_real) /
			      (cb_ctx.total_alloc_real + 0.001)));
}

static void
end(struct tbuf *out)
{
	tbuf_printf(out, "..." CRLF);
}

static void
start(struct tbuf *out)
{
	tbuf_printf(out, "---" CRLF);
}

static void
ok(struct tbuf *out)
{
	start(out);
	tbuf_printf(out, "ok" CRLF);
	end(out);
}

static void
fail(struct tbuf *out, struct tbuf *err)
{
	start(out);
	tbuf_printf(out, "fail:%.*s" CRLF, err->size, (char *)err->data);
	end(out);
}

static void
index_info(struct tbuf *out)
{
	tbuf_printf(out, "index:" CRLF);
	struct space_stat *stat = space_stat();
	int sp_i = 0;
	int64_t total_size = 0;
	while (stat[sp_i].n >= 0) {
		tbuf_printf(out, "  - space: %" PRIi32 CRLF, stat[sp_i].n);
		int64_t sp_size = 0;
		int i;
		for (i = 0; stat[sp_i].index[i].n >= 0; ++i)
			sp_size += stat[sp_i].index[i].memsize;

		tbuf_printf(out, "    memsize: %15" PRIi64 CRLF, sp_size);
		total_size += sp_size;
		tbuf_printf(out, "    index: " CRLF);
		for (i = 0; stat[sp_i].index[i].n >= 0; ++i) {
			tbuf_printf(out, "      - { n: %3d, keys: %15" PRIi64 ", memsize: %15" PRIi64 " }" CRLF,
				    stat[sp_i].index[i].n, stat[sp_i].index[i].keys, stat[sp_i].index[i].memsize);
		}
		++sp_i;
	}
	tbuf_printf(out, "memsize:     %15" PRIi64 CRLF, total_size);
}

static void
tarantool_info(struct tbuf *out)
{
	tbuf_printf(out, "info:" CRLF);
	tbuf_printf(out, "  version: \"%s\"" CRLF, tarantool_version());
	tbuf_printf(out, "  uptime: %i" CRLF, (int)tarantool_uptime());
	tbuf_printf(out, "  pid: %i" CRLF, getpid());
	tbuf_printf(out, "  logger_pid: %i" CRLF, logger_pid);
	tbuf_printf(out, "  snapshot_pid: %i" CRLF, snapshot_pid);
	tbuf_printf(out, "  lsn: %" PRIi64 CRLF,
		    recovery_state->confirmed_lsn);
	tbuf_printf(out, "  recovery_lag: %.3f" CRLF,
		    recovery_state->remote ?
		    recovery_state->remote->recovery_lag : 0);
	tbuf_printf(out, "  recovery_last_update: %.3f" CRLF,
		    recovery_state->remote ?
		    recovery_state->remote->recovery_last_update_tstamp :0);
	box_info(out);
	const char *path = cfg_filename_fullpath;
	if (path == NULL)
		path = cfg_filename;
	tbuf_printf(out, "  config: \"%s\"" CRLF, path);
}

static int
show_stat_item(const char *name, int rps, int64_t total, void *ctx)
{
	struct tbuf *buf = (struct tbuf *) ctx;
	int name_len = strlen(name);
	tbuf_printf(buf,
		    "  %s:%*s{ rps: %- 6i, total: %- 12" PRIi64 " }" CRLF,
		    name, 1 + stat_max_name_len - name_len, " ", rps, total);
	return 0;
}

void
show_stat(struct tbuf *buf)
{
	tbuf_printf(buf, "statistics:" CRLF);
	stat_foreach(show_stat_item, buf);
}

static int
admin_dispatch(struct ev_io *coio, struct iobuf *iobuf, lua_State *L)
{
	struct ibuf *in = &iobuf->in;
	struct tbuf *out = tbuf_new(fiber->gc_pool);
	struct tbuf *err = tbuf_new(fiber->gc_pool);
	int cs;
	char *p, *pe;
	char *strstart, *strend;
	bool state;

	while ((pe = (char *) memchr(in->pos, '\n', in->end - in->pos)) == NULL) {
		if (coio_bread(coio, in, 1) <= 0)
			return -1;
	}

	pe++;
	p = in->pos;

	%%{
		action show_configuration {
			start(out);
			show_cfg(out);
			end(out);
		}

		action show_injections {
			start(out);
			errinj_info(out);
			end(out);
		}

		action help {
			start(out);
			tbuf_append(out, help, strlen(help));
			end(out);
		}

		action lua {
			strstart[strend-strstart]='\0';
			start(out);
			tarantool_lua(L, out, strstart);
			end(out);
		}

		action reload_configuration {
			if (reload_cfg(err))
				fail(out, err);
			else
				ok(out);
		}

		action save_snapshot {
			int ret = snapshot();

			if (ret == 0)
				ok(out);
			else {
				tbuf_printf(err, " can't save snapshot, errno %d (%s)",
					    ret, strerror(ret));

				fail(out, err);
			}
		}

		action set_injection {
			strstart[strend-strstart] = '\0';
			if (errinj_set_byname(strstart, state)) {
				tbuf_printf(err, "can't find error injection '%s'", strstart);
				fail(out, err);
			} else {
				ok(out);
			}
		}

		eol = "\n" | "\r\n";
		show = "sh"("o"("w")?)?;
		info = "in"("f"("o")?)?;
		index = ("ind"("e"("x")?)? | "idx");
		check = "ch"("e"("c"("k")?)?)?;
		configuration = "co"("n"("f"("i"("g"("u"("r"("a"("t"("i"("o"("n")?)?)?)?)?)?)?)?)?)?)?;
		fiber = "fi"("b"("e"("r")?)?)?;
		slab = "sl"("a"("b")?)?;
		mod = "mo"("d")?;
		palloc = "pa"("l"("l"("o"("c")?)?)?)?;
		stat = "st"("a"("t")?)?;
		plugins = "plugins";

		help = "h"("e"("l"("p")?)?)?;
		exit = "e"("x"("i"("t")?)?)? | "q"("u"("i"("t")?)?)?;
		save = "sa"("v"("e")?)?;
		coredump = "co"("r"("e"("d"("u"("m"("p")?)?)?)?)?)?;
		snapshot = "sn"("a"("p"("s"("h"("o"("t")?)?)?)?)?)?;
		string = [^\r\n]+ >{strstart = p;}  %{strend = p;};
		reload = "re"("l"("o"("a"("d")?)?)?)?;
		lua = "lu"("a")?;

		set = "se"("t")?;
		injection = "in"("j"("e"("c"("t"("i"("o"("n")?)?)?)?)?)?)?;
		injections = injection"s";
		namech = alnum | punct;
		name = namech+ >{ strstart = p; }  %{ strend = p; };
		state_on = "on" %{ state = true; };
		state_off = "of"("f")? %{ state = false; };
		state = state_on | state_off;

		commands = (help			%help						|
			    exit			%{return -1;}					|
			    lua  " "+ string		%lua						|
			    show " "+ info		%{start(out); tarantool_info(out); end(out);}	|
			    show " "+ index		%{start(out); index_info(out); end(out);}	|
			    show " "+ fiber		%{start(out); fiber_info(out); end(out);}	|
			    show " "+ configuration 	%show_configuration				|
			    show " "+ slab		%{start(out); show_slab(out); end(out);}	|
			    show " "+ palloc		%{start(out); palloc_stat(out); end(out);}	|
			    show " "+ stat		%{start(out); show_stat(out);end(out);}		|
			    show " "+ injections	%show_injections                                |
			    set " "+ injection " "+ name " "+ state	%set_injection                  |
			    save " "+ coredump		%{coredump(60); ok(out);}			|
			    save " "+ snapshot		%save_snapshot					|
			    check " "+ slab		%{slab_validate(); ok(out);}			|
			    reload " "+ configuration	%reload_configuration);

		main := commands eol;
		write init;
		write exec;
	}%%

	in->pos = pe;

	if (p != pe) {
		start(out);
		tbuf_append(out, unknown_command, strlen(unknown_command));
		end(out);
	}

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
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);

	auto scoped_guard = make_scoped_guard([&] {
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
		evio_close(&coio);
		iobuf_delete(iobuf);
		session_destroy(fiber->sid);
	});

	/*
	 * Admin and iproto connections must have a
	 * session object, representing the state of
	 * a remote client: it's used in Lua
	 * stored procedures.
	 */
	session_create(coio.fd, *(uint64_t *) addr);
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

/*
 * Local Variables:
 * mode: c
 * End:
 * vim: syntax=objc
 */
