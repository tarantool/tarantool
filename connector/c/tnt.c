
/*
 * Copyright (C) 2011 Mail.RU
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

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_opt.h>
#include <tnt_buf.h>
#include <tnt.h>
#include <tnt_io.h>

struct tnt*
tnt_alloc(void)
{
	struct tnt *t = malloc(sizeof(struct tnt));
	if (t == NULL)
		return NULL;
	memset(t, 0, sizeof(struct tnt));
	tnt_opt_init(&t->opt);
	return t;
}

int
tnt_init(struct tnt *t)
{
	tnt_mem_init(t->opt.malloc,
		t->opt.realloc, t->opt.dup, t->opt.free);
	if (tnt_buf_init(&t->sbuf, t->opt.send_buf, t->opt.send_cb,
		t->opt.send_cbv, t->opt.send_cb_arg) == -1) {
		t->error = TNT_EMEMORY;
		return -1;
	}
	if (tnt_buf_init(&t->rbuf, t->opt.recv_buf, t->opt.recv_cb, NULL, 
		t->opt.recv_cb_arg) == -1) {
		t->error = TNT_EMEMORY;
		return -1;
	}
	return 0;
}

void
tnt_free(struct tnt *t)
{
	tnt_io_close(t);
	tnt_buf_free(&t->sbuf);
	tnt_buf_free(&t->rbuf);
	tnt_opt_free(&t->opt);
	free(t);
}

int
tnt_set(struct tnt *t, enum tnt_opt_type name, ...)
{
	va_list args;
	va_start(args, name);
	t->error = tnt_opt_set(&t->opt, name, (void*)&args);
	va_end(args);
	return (t->error == TNT_EOK) ? 0 : -1;
}

int
tnt_connect(struct tnt *t)
{
	if (t->opt.hostname == NULL) {
		t->error = TNT_EBADVAL;
		return -1;
	}
	if (t->opt.port == 0) {
		t->error = TNT_EBADVAL;
		return -1;
	}
	t->error = tnt_io_connect(t, t->opt.hostname, t->opt.port);
	if (t->error != TNT_EOK)
		return -1;
	t->connected = 1;
	return 0;
}

int
tnt_flush(struct tnt *t)
{
	t->error = tnt_io_flush(t);
	return (t->error == TNT_EOK) ? 0 : -1;
}

void
tnt_close(struct tnt *t)
{
	tnt_io_close(t);
}

enum tnt_error
tnt_error(struct tnt *t)
{
	return t->error;
}

int
tnt_error_errno(struct tnt *t)
{
	return t->error_errno;
}

struct tnt_error_desc {
	enum tnt_error type;
	char * desc;
};

/* must be in sync with enum tnt_error */
static
struct tnt_error_desc tnt_error_list[] = 
{
	{ TNT_EFAIL,    "fail"                     },
	{ TNT_EOK,      "ok"                       },
	{ TNT_EMEMORY,  "memory allocation failed" },
	{ TNT_ESYSTEM,  "system error"             },
	{ TNT_EBADVAL,  "bad argument"             },
	{ TNT_EBIG,     "buffer is too big"        },
	{ TNT_ESIZE,    "bad buffer size"          },
	{ TNT_ERESOLVE, "gethostbyname(2) failed"  },
	{ TNT_ETMOUT,   "operation timeout"        },
	{ TNT_EPROTO,   "protocol error"           },
	{ TNT_EAUTH,    "authentication failed"    },
	{ TNT_EEMPTY,   "object is empty"          },
	{ TNT_EERROR,   "server error"             },
	{ TNT_LAST,      NULL                      }
};

char*
tnt_perror(struct tnt *t)
{
	if (t->error == TNT_ESYSTEM) {
		static char msg[256];
		snprintf(msg, sizeof(msg), "%s: %s",
			tnt_error_list[TNT_ESYSTEM].desc,
				strerror(t->error_errno));
		return msg;
	}
	return tnt_error_list[(int)t->error].desc;
}
