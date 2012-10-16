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
#include <util.h>
#include <errinj.h>
#include "coio_buf.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static const char *help =
	"available commands:" CRLF
	" - help" CRLF
	" - exit" CRLF
	" - show info" CRLF
	" - show fiber" CRLF
	" - show configuration" CRLF
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
	i64 total_used;
	struct tbuf *out;
};

static int
salloc_stat_admin_cb(const struct slab_class_stats *cstat, void *cb_ctx)
{
	struct salloc_stat_admin_cb_ctx *ctx = cb_ctx;

	tbuf_printf(ctx->out,
		    "     - { item_size: %- 5i, slabs: %- 3i, items: %- 11" PRIi64
		    ", bytes_used: %- 12" PRIi64
		    ", bytes_free: %- 12" PRIi64 " }" CRLF,
		    (int)cstat->item_size,
		    (int)cstat->slabs,
		    cstat->items,
		    cstat->bytes_used,
		    cstat->bytes_free);

	ctx->total_used += cstat->bytes_used;
	return 0;
}

static void
show_slab(struct tbuf *out)
{
	struct salloc_stat_admin_cb_ctx cb_ctx;
	struct slab_arena_stats astat;

	cb_ctx.total_used = 0;
	cb_ctx.out = out;

	tbuf_printf(out, "slab statistics:\n  classes:" CRLF);

	salloc_stat(salloc_stat_admin_cb, &astat, &cb_ctx);

	tbuf_printf(out, "  items_used: %.2f%%" CRLF,
		(double)cb_ctx.total_used / astat.size * 100);
	tbuf_printf(out, "  arena_used: %.2f%%" CRLF,
		(double)astat.used / astat.size * 100);
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
	mod_info(out);
	const char *path = cfg_filename_fullpath;
	if (path == NULL)
		path = cfg_filename;
	tbuf_printf(out, "  config: \"%s\"" CRLF, path);
}

static int
show_stat_item(const char *name, int rps, i64 total, void *ctx)
{
	struct tbuf *buf = ctx;
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
admin_dispatch(struct coio *coio, struct iobuf *iobuf, lua_State *L)
{
	struct ibuf *in = &iobuf->in;
	struct tbuf *out = tbuf_alloc(fiber->gc_pool);
	struct tbuf *err = tbuf_alloc(fiber->gc_pool);
	int cs;
	char *p, *pe;
	char *strstart, *strend;
	bool state;

	while ((pe = memchr(in->pos, '\n', in->end - in->pos)) == NULL) {
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
			int ret = snapshot(NULL, 0);

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
		check = "ch"("e"("c"("k")?)?)?;
		configuration = "co"("n"("f"("i"("g"("u"("r"("a"("t"("i"("o"("n")?)?)?)?)?)?)?)?)?)?)?;
		fiber = "fi"("b"("e"("r")?)?)?;
		slab = "sl"("a"("b")?)?;
		mod = "mo"("d")?;
		palloc = "pa"("l"("l"("o"("c")?)?)?)?;
		stat = "st"("a"("t")?)?;

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
	struct coio coio = va_arg(ap, struct coio);
	struct iobuf *iobuf = va_arg(ap, struct iobuf *);
	lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	@try {
		for (;;) {
			if (admin_dispatch(&coio, iobuf, L) < 0)
				return;
			iobuf_gc(iobuf);
			fiber_gc();
		}
	} @finally {
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
		coio_close(&coio);
		iobuf_destroy(iobuf);
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
