/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
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
#include TARANTOOL_CONFIG
#include <tbuf.h>
#include <util.h>
#include <errinj.h>
#include "third_party/luajit/src/lua.h"
#include "third_party/luajit/src/lauxlib.h"
#include "third_party/luajit/src/lualib.h"

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
	mod_info(out);
	const char *path = cfg_filename_fullpath;
	if (path == NULL)
		path = cfg_filename;
	tbuf_printf(out, "  config: \"%s\"" CRLF, path);
}

static int
admin_dispatch(lua_State *L)
{
	struct tbuf *out = tbuf_alloc(fiber->gc_pool);
	struct tbuf *err = tbuf_alloc(fiber->gc_pool);
	int cs;
	char *p, *pe;
	char *strstart, *strend;
	bool state;

	while ((pe = memchr(fiber->rbuf->data, '\n', fiber->rbuf->size)) == NULL) {
		if (fiber_bread(fiber->rbuf, 1) <= 0)
			return 0;
	}

	pe++;
	p = fiber->rbuf->data;

	%%{
		action show_configuration {
			tarantool_cfg_iterator_t *i;
			char *key, *value;

			start(out);
			tbuf_printf(out, "configuration:" CRLF);
			i = tarantool_cfg_iterator_init();
			while ((key = tarantool_cfg_iterator_next(i, &cfg, &value)) != NULL) {
				if (value) {
					tbuf_printf(out, "  %s: \"%s\"" CRLF, key, value);
					free(value);
				} else {
					tbuf_printf(out, "  %s: (null)" CRLF, key);
				}
			}
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
			    exit			%{return 0;}					|
			    lua  " "+ string		%lua						|
			    show " "+ info		%{start(out); tarantool_info(out); end(out);}	|
			    show " "+ fiber		%{start(out); fiber_info(out); end(out);}	|
			    show " "+ configuration 	%show_configuration				|
			    show " "+ slab		%{start(out); slab_stat(out); end(out);}	|
			    show " "+ palloc		%{start(out); palloc_stat(out); end(out);}	|
			    show " "+ stat		%{start(out); stat_print(out);end(out);}	|
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

	tbuf_ltrim(fiber->rbuf, (void *)pe - (void *)fiber->rbuf->data);

	if (p != pe) {
		start(out);
		tbuf_append(out, unknown_command, strlen(unknown_command));
		end(out);
	}

	return fiber_write(out->data, out->size);
}

static void
admin_handler(void *data __attribute__((unused)))
{
	lua_State *L = lua_newthread(tarantool_L);
	int coro_ref = luaL_ref(tarantool_L, LUA_REGISTRYINDEX);
	/** Allow to interrupt/kill administrative connections. */
	fiber_setcancelstate(true);
	@try {
		for (;;) {
			if (admin_dispatch(L) <= 0)
				return;
			fiber_gc();
		}
	} @finally {
		luaL_unref(tarantool_L, LUA_REGISTRYINDEX, coro_ref);
	}
}

int
admin_init(void)
{
	if (fiber_server("admin", cfg.admin_port, admin_handler, NULL, NULL) == NULL) {
		say_syserror("can't bind to %d", cfg.admin_port);
		return -1;
	}
	return 0;
}



/*
 * Local Variables:
 * mode: c
 * End:
 * vim: syntax=objc
 */
