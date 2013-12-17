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

#include <lib/tarantool.h>

#include <client/tarantool/opt.h>
#include <client/tarantool/main.h>
#include <client/tarantool/print.h>
#include <client/tarantool/query.h>
#include <client/tarantool/store.h>

struct tarantool_client tc;

typedef int (*tc_iter_t)(struct tbfile *f);

static int tc_store_error(char *fmt, ...) {
	char msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	tc_printf("error: %s\n", msg);
	return -1;
}

static int tc_store_foreach(struct tbfile *f, tc_iter_t cb)
{
	int rc;
	while ((rc = tb_filenext(f)) > 0) {
		if (cb(f) == -1)
			return -1;
	}
	if (rc < 0)
		tc_store_error("parsing error: %s", tb_fileerror(f, rc));
	return rc;
}

static int
tc_store_foreach_xlog(tc_iter_t cb)
{
	struct tbfile f;
	int rc = tb_fileopen(&f, (char*)tc.opt.file);
	if (rc < 0)
		return 1;
	rc = tc_store_foreach(&f, cb);
	tb_fileclose(&f);
	return rc;
}

static int
tc_store_check_skip(struct tbfile *f)
{
#if 0
	if (tc.opt.space_set) {
		if (f->h.type == TNT_OP_CALL)
			return 1;
		uint32_t ns = *(uint32_t*)&r->r;
		if (ns != tc.opt.space)
			return 1;
	}
#endif
	if (tc.opt.lsn_from_set) {
		if (f->h.lsn < tc.opt.lsn_from)
			return 1;
	}
	if (tc.opt.lsn_to_set) {
		if (f->h.lsn > tc.opt.lsn_to)
			return 1;
	}
	return 0;
}

static int tc_store_xlog_printer(struct tbfile *f)
{
	if (tc_store_check_skip(f))
		return 0;
	/*((tc_printerf_xlog_t)tc.opt.xlog_printer)(&s->log.current, r);*/
	return 0;
}

int tc_store_cat(void)
{
#if 0
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
#endif

	int rc = tc_store_foreach_xlog(tc_store_xlog_printer);

#if 0
	if (rc == 0 && print_headers) {
		fwrite(&tnt_log_marker_eof_v11,
		       sizeof(tnt_log_marker_eof_v11), 1, stdout);
#endif

	return rc;
}

#if 0
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
#endif
