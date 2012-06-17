
/*
 * Copyright (C) 2012 Mail.RU
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
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_sql.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_rpl.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_print.h"
#include "client/tarantool/tc_query.h"
#include "client/tarantool/tc_wal.h"

extern struct tc tc;

typedef int (*tc_wal_t)(struct tnt_iter *i);

static int tc_wal_error(char *fmt, ...) {
	char msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	printf("error: %s\n", msg);
	return -1;
}

static int tc_wal_foreach(struct tnt_stream *s, tc_wal_t cb) {
	struct tnt_iter i;
	tnt_iter_request(&i, s);
	while (tnt_next(&i)) {
		if (cb(&i) == -1) {
			tnt_iter_free(&i);
			return -1;
		}
	}
	int rc = 0;
	if (i.status == TNT_ITER_FAIL)
		rc = tc_wal_error("parsing failed");
	tnt_iter_free(&i);
	return rc;
}

static void tc_wal_print(struct tnt_xlog_header_v11 *hdr,
		         struct tnt_request *r)
{
	printf("%s lsn: %"PRIu64", time: %f, len: %"PRIu32"\n",
	       tc_query_type(r->h.type),
	       hdr->lsn,
	       hdr->tm,
	       hdr->len);
	switch (r->h.type) {
	case TNT_OP_INSERT:
		tc_print_tuple(&r->r.insert.t);
		break;
	case TNT_OP_DELETE:
		tc_print_tuple(&r->r.del.t);
		break;
	case TNT_OP_UPDATE:
		tc_print_tuple(&r->r.update.t);
		break;
	case TNT_OP_CALL:
		tc_print_tuple(&r->r.call.t);
		break;
	}
}

static int tc_wal_printer(struct tnt_iter *i) {
	struct tnt_request *r = TNT_IREQUEST_PTR(i);
	struct tnt_stream_xlog *s =
		TNT_SXLOG_CAST(TNT_IREQUEST_STREAM(i));
	tc_wal_print(&s->hdr, r);
	return 0;
}

static int tc_wal_foreach_xlog(tc_wal_t cb) {
	struct tnt_stream s;
	tnt_xlog(&s);
	if (tnt_xlog_open(&s, (char*)tc.opt.xlog) == -1) {
		tnt_stream_free(&s);
		return 1;
	}
	if (tc_wal_foreach(&s, cb) == -1) {
		tnt_stream_free(&s);
		return 1;
	}
	tnt_stream_free(&s);
	return 0;
}

int tc_wal_cat(void)
{
	return tc_wal_foreach_xlog(tc_wal_printer);
}

static int tc_wal_resender(struct tnt_iter *i) {
	struct tnt_request *r = TNT_IREQUEST_PTR(i);
	if (tc.net->write_request(tc.net, r) == -1)
		return tc_wal_error("failed to write request");
	char *e = NULL;
	if (tc_query_foreach(NULL, NULL, &e) == -1) {
		tc_wal_error("%s", e);
		free(e);
		return -1;
	}
	return 0;
}

int tc_wal_play(void)
{
	return tc_wal_foreach_xlog(tc_wal_resender);
}

static int tc_wal_printer_from_rpl(struct tnt_iter *i) {
	struct tnt_request *r = TNT_IREQUEST_PTR(i);
	struct tnt_stream_rpl *s =
		TNT_RPL_CAST(TNT_IREQUEST_STREAM(i));
	tc_wal_print(&s->hdr, r);
	return 0;
}

int tc_wal_remote(void)
{
	if (tc.opt.lsn == LLONG_MAX ||
	    tc.opt.lsn == LLONG_MIN) {
		tc_wal_error("bad lsn number");
		return 1;
	}
	struct tnt_stream s;
	tnt_rpl(&s);
	tnt_rpl_attach(&s, tc.net);
	int rc = 0;
	if (tnt_rpl_open(&s, tc.opt.lsn) == -1) {
		rc = 1;
		goto done;
	}
	if (tc_wal_foreach(&s, tc_wal_printer_from_rpl) == -1)
		rc = 1;
done:
	tnt_stream_free(&s);
	return rc;
}
