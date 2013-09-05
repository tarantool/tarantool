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
#include <connector/c/include/tarantool/tnt_snapshot.h>
#include <connector/c/include/tarantool/tnt_rpl.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_print.h"
#include "client/tarantool/tc_print_xlog.h"
#include "client/tarantool/tc_print_snap.h"
#include "client/tarantool/tc_query.h"
#include "client/tarantool/tc_store.h"

extern struct tc tc;

typedef int (*tc_iter_t)(struct tnt_iter *i);

static int tc_store_error(char *fmt, ...) {
	char msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	tc_printf("error: %s\n", msg);
	return -1;
}

static int tc_store_foreach(struct tnt_iter *i, tc_iter_t cb) {
	while (tnt_next(i)) {
		if (cb(i) == -1) {
			tnt_iter_free(i);
			return -1;
		}
	}
	int rc = 0;
	if (i->status == TNT_ITER_FAIL)
		rc = tc_store_error("parsing failed");
	return rc;
}

static void tc_store_print(struct tnt_log_header_v11 *hdr,
		         struct tnt_request *r)
{
	tc_printf("%s lsn: %"PRIu64", time: %f, len: %"PRIu32"\n",
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
	case TNT_OP_DELETE_1_3:
		tc_print_tuple(&r->r.del_1_3.t);
		break;
	case TNT_OP_UPDATE:
		tc_print_tuple(&r->r.update.t);
		break;
	case TNT_OP_CALL:
		tc_print_tuple(&r->r.call.t);
		break;
	}
}

static int tc_store_check_skip(struct tnt_iter *i, struct tnt_request *r) {
	struct tnt_stream_xlog *s =
		TNT_SXLOG_CAST(TNT_IREQUEST_STREAM(i));
	if (tc.opt.space_set) {
		if (r->h.type == TNT_OP_CALL)
			return 1;
		uint32_t ns = *(uint32_t*)&r->r;
		if (ns != tc.opt.space)
			return 1;
	}
	if (tc.opt.lsn_from_set) {
		if (s->log.current.hdr.lsn < tc.opt.lsn_from)
			return 1;
	}
	if (tc.opt.lsn_to_set) {
		if (s->log.current.hdr.lsn > tc.opt.lsn_to)
			return 1;
	}
	return 0;
}

static int tc_store_xlog_printer(struct tnt_iter *i) {
	struct tnt_request *r = TNT_IREQUEST_PTR(i);
	if (tc_store_check_skip(i, r))
		return 0;
	struct tnt_stream_xlog *s =
		TNT_SXLOG_CAST(TNT_IREQUEST_STREAM(i));
	((tc_printerf_xlog_t)tc.opt.xlog_printer)(&s->log.current.hdr, r);
	return 0;
}

static int tc_store_snap_printer(struct tnt_iter *i) {
	struct tnt_tuple *tu = TNT_ISTORAGE_TUPLE(i);
	struct tnt_stream_snapshot *ss =
		TNT_SSNAPSHOT_CAST(TNT_ISTORAGE_STREAM(i));
	if (tc.opt.space_set) {
		if (ss->log.current.row_snap.space != tc.opt.space)
			return 0;
	}
	((tc_printerf_snap_t)tc.opt.snap_printer)(&ss->log.current.row_snap, tu);
	return 0;
}

static int tc_store_foreach_request(struct tnt_stream *s, tc_iter_t cb) {
	struct tnt_iter i;
	tnt_iter_request(&i, s);
	int rc = tc_store_foreach(&i, cb);
	tnt_iter_free(&i);
	return rc;
}

static int tc_store_foreach_xlog(tc_iter_t cb) {
	struct tnt_stream s;
	tnt_xlog(&s);
	if (tnt_xlog_open(&s, (char*)tc.opt.file) == -1) {
		tnt_stream_free(&s);
		return 1;
	}
	int rc = tc_store_foreach_request(&s, cb);
	tnt_stream_free(&s);
	return rc;
}

static int tc_store_foreach_snap(tc_iter_t cb) {
	struct tnt_stream s;
	tnt_snapshot(&s);
	if (tnt_snapshot_open(&s, (char*)tc.opt.file) == -1) {
		tnt_stream_free(&s);
		return 1;
	}
	struct tnt_iter i;
	tnt_iter_storage(&i, &s);
	int rc = tc_store_foreach(&i, cb);
	tnt_iter_free(&i);
	tnt_stream_free(&s);
	return rc;
}

int tc_store_cat(void)
{
	enum tnt_log_type type = tnt_log_guess((char*)tc.opt.file);
	if (type == TNT_LOG_NONE)
		return 1;
	int print_headers = tc.opt.raw && tc.opt.raw_with_headers;
	if (print_headers) {
		char *h = (type == TNT_LOG_SNAPSHOT ?
		           TNT_LOG_MAGIC_SNAP : TNT_LOG_MAGIC_XLOG);
		fputs(h, stdout);
		fputs(TNT_LOG_VERSION, stdout);
		fputs("\n", stdout);
	}
	int rc;

	switch (type) {
	case TNT_LOG_SNAPSHOT:
		rc = tc_store_foreach_snap(tc_store_snap_printer);
		break;
	case TNT_LOG_XLOG:
		rc = tc_store_foreach_xlog(tc_store_xlog_printer);
		break;
	case TNT_LOG_NONE:
		rc = 1;
		break;
	default:
		return -1;
	}
	if (rc == 0 && print_headers) {
		fwrite(&tnt_log_marker_eof_v11,
		       sizeof(tnt_log_marker_eof_v11), 1, stdout);
	}
	return rc;
}
static int
tc_store_snap_resender(struct tnt_iter *i) {
	struct tnt_tuple *tu = TNT_ISTORAGE_TUPLE(i);
	struct tnt_stream_snapshot *ss =
		TNT_SSNAPSHOT_CAST(TNT_ISTORAGE_STREAM(i));
	if (tc.opt.space_set) {
		if (ss->log.current.row_snap.space != tc.opt.space)
			return 0;
	}
	if (tnt_insert(tc.net, ss->log.current.row_snap.space,
		       TNT_FLAG_ADD, tu) == -1)
		return tc_store_error("failed to write request");
	char *e = NULL;
	if (tc_query_foreach(NULL, NULL, &e) == -1) {
		tc_store_error("%s", e);
		free(e);
		return -1;
	}
	return 0;
}

static int
tc_store_xlog_resender(struct tnt_iter *i) {
	struct tnt_request *r = TNT_IREQUEST_PTR(i);
	if (tc_store_check_skip(i, r))
		return 0;
	if (tc.net->write_request(tc.net, r) == -1)
		return tc_store_error("failed to write request");
	char *e = NULL;
	if (tc_query_foreach(NULL, NULL, &e) == -1) {
		tc_store_error("%s", e);
		free(e);
		return -1;
	}
	return 0;
}

int tc_store_play(void)
{
	enum tnt_log_type type = tnt_log_guess((char *)tc.opt.file);
	if (type == TNT_LOG_NONE)
		return 1;
	int rc;
	switch (type) {
	case TNT_LOG_SNAPSHOT:
		rc = tc_store_foreach_snap(tc_store_snap_resender);
		break;
	case TNT_LOG_XLOG:
		rc = tc_store_foreach_xlog(tc_store_xlog_resender);
		break;
	case TNT_LOG_NONE:
		rc = 1;
		break;
	default:
		return -1;
	}
	return rc;
}

static int tc_store_printer_from_rpl(struct tnt_iter *i) {
	struct tnt_request *r = TNT_IREQUEST_PTR(i);
	struct tnt_stream_rpl *s =
		TNT_RPL_CAST(TNT_IREQUEST_STREAM(i));
	tc_store_print(&s->hdr, r);
	return 0;
}

int tc_store_remote(void)
{
	if (tc.opt.lsn == LLONG_MAX ||
	    tc.opt.lsn == LLONG_MIN) {
		tc_store_error("bad lsn number");
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

	if (tc_store_foreach_request(&s, tc_store_printer_from_rpl) == -1)
		rc = 1;
done:
	tnt_stream_free(&s);
	return rc;
}
