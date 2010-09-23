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
#include <tbuf.h>
#include <util.h>

static const char help[] =
	"available commands:\r\n"
	"help\r\n"
	"exit\r\n"
	"show info\r\n"
	"show fiber\r\n"
	"show configuration\r\n"
	"show slab\r\n"
	"show palloc\r\n"
	"show stat\r\n"
	"save coredump\r\n"
	"save snapshot\r\n"
	"exec module command\r\n"
	;


static const char unknown_command[] = "unknown command. try typing help.\r\n";

%%{
	machine admin;
	write data;
}%%

static void
ok(struct tbuf *out)
{
	tbuf_printf(out, "ok\r\n");
}

static void
end(struct tbuf *out)
{
	tbuf_printf(out, "---\r\n");
}

static int
admin_dispatch(void)
{
	struct tbuf *out = tbuf_alloc(fiber->pool);
	int cs;
	char *p, *pe;
	char *strstart, *strend;

	while ((pe = memchr(fiber->rbuf->data, '\n', fiber->rbuf->len)) == NULL) {
		if (fiber_bread(fiber->rbuf, 1) <= 0)
			return 0;
	}

	pe++;
	p = fiber->rbuf->data;

	%%{
		action show_configuration {
			tarantool_cfg_iterator_t *i;
			char *key, *value;

			tbuf_printf(out, "configuration:\n");
			i = tarantool_cfg_iterator_init();
			while ((key = tarantool_cfg_iterator_next(i, &cfg, &value)) != NULL) {
				if (value) {
					tbuf_printf(out, "  %s: \"%s\"\n", key, value);
					free(value);
				} else {
					tbuf_printf(out, "  %s: (null)\n", key);
				}
			}
			end(out);
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
		exec = "ex"("e"("c")?)?;
		string = [^\r\n]+ >{strstart = p;}  %{strend = p;};

		commands = (help			%{tbuf_append(out, help, sizeof(help));}		|
			    exit			%{return 0;}						|
			    show " "+ info		%{mod_info(out); end(out);}				|
			    show " "+ fiber		%{fiber_info(out);end(out);}				|
			    show " "+ configuration 	%show_configuration					|
			    show " "+ slab		%{slab_stat(out);end(out);}				|
			    show " "+ palloc		%{palloc_stat(out);end(out);}				|
			    show " "+ stat		%{stat_print(out);end(out);}				|
			    save " "+ coredump		%{coredump(60); ok(out);}				|
			    save " "+ snapshot		%{snapshot(NULL, 0); ok(out);}				|
			    exec " "+ string		%{mod_exec(strstart, strend - strstart, out); end(out);}|
			    check " "+ slab		%{slab_validate(); ok(out);});

	        main := commands eol;
		write init;
		write exec;
	}%%

	fiber->rbuf->len -= (void *)pe - (void *)fiber->rbuf->data;
	fiber->rbuf->data = pe;

	if (p != pe)
		tbuf_append(out, unknown_command, sizeof(unknown_command));

	return fiber_write(out->data, out->len);
}


static void
admin_handler(void *_data __unused__)
{
	for (;;) {
		if (admin_dispatch() <= 0)
			return;
		fiber_gc();
	}
}

int
admin_init(void)
{
	if (fiber_server(tcp_server, cfg.admin_port, admin_handler, NULL, NULL) == NULL) {
		say_syserror("can't bind to %d", cfg.admin_port);
		return -1;
	}
	return 0;
}



/*
 * Local Variables:
 * mode: c
 * End:
 */
