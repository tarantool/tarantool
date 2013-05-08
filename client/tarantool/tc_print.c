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
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>
#include <errno.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_rpl.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_print.h"
#include "client/tarantool/tc_query.h"

extern struct tc tc;

void tc_print_tee(char *buf, size_t size) {
	if (tc.tee_fd == -1)
		return;
	size_t off = 0;
	do {
		ssize_t r = write(tc.tee_fd, buf + off, size - off);
		if (r == -1) {
			printf("error: read(): %s\n", strerror(errno));
			return;
		}
		off += r;
	} while (off != size);
}

void tc_print_cmd2tee(char *prompt, char *cmd, int size) {
	if (tc.tee_fd == -1)
		return;
	if (prompt)
		tc_print_tee(prompt, strlen(prompt));
	tc_print_tee(cmd, size);
	tc_print_tee("\n", 1);
}

void tc_print_buf(char *buf, size_t size) {
	printf("%-.*s", (int)size, buf);
	fflush(stdout);
	tc_print_tee(buf, size);
}

void tc_printf(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	if (tc.tee_fd == -1) {
		vprintf(fmt, args);
		va_end(args);
		return;
	}
	char *buf;
	int size = vasprintf(&buf, fmt, args);
	va_end(args);
	if (size >= 0) {
		tc_print_buf(buf, size);
		free(buf);
	}
}

static void tc_print_fields(struct tnt_tuple *tu) {
	struct tnt_iter ifl;
	tnt_iter(&ifl, tu);
	while (tnt_next(&ifl)) {
		if (TNT_IFIELD_IDX(&ifl) != 0)
			tc_printf(", ");
		tc_printf("'");
		char *data = TNT_IFIELD_DATA(&ifl);
		uint32_t size = TNT_IFIELD_SIZE(&ifl);
		switch (size) {
		case 4:
			tc_printf("%"PRIu32, *((uint32_t*)data));
			break;
		case 8:
			tc_printf("%"PRIu64, *((uint64_t*)data));
			break;
		default:
			while (size-- > 0) {
				if (0x20 <= *data && *data < 0x7f)
					tc_printf("%c", *data);
				else
					tc_printf("\\x%2X", (unsigned char)*data);
				data++;
			}
		}
		tc_printf("'");
	}
	if (ifl.status == TNT_ITER_FAIL)
		tc_printf("<parsing error>");
	tnt_iter_free(&ifl);
}

void tc_print_tuple(struct tnt_tuple *tu)
{
	tc_printf("[");
	tc_print_fields(tu);
	tc_printf("]\n");
}

void tc_print_list(struct tnt_list *l)
{
	struct tnt_iter it;
	tnt_iter_list(&it, l);
	while (tnt_next(&it)) {
		struct tnt_tuple *tu = TNT_ILIST_TUPLE(&it);
		tc_print_tuple(tu);
	}
	tnt_iter_free(&it);
}

static void
tc_printer_tarantool(struct tnt_log_header_v11 *hdr,
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
	case TNT_OP_UPDATE:
		tc_print_tuple(&r->r.update.t);
		break;
	}
}

static void
tc_printer_raw(struct tnt_log_header_v11 *hdr, struct tnt_request *r)
{
	if (tc.opt.raw_with_headers) {
		fwrite(&tnt_log_marker_v11,
		       sizeof(tnt_log_marker_v11), 1, stdout);
	}
	fwrite(hdr, sizeof(*hdr), 1, stdout);
	fwrite(r->origin, r->origin_size, 1, stdout);
}

tc_printerf_t tc_print_getcb(const char *name)
{
	if (name == NULL)
		return tc_printer_tarantool;
	if (!strcasecmp(name, "tarantool"))
		return tc_printer_tarantool;
	if (!strcasecmp(name, "raw"))
		return tc_printer_raw;
	return NULL;
}
