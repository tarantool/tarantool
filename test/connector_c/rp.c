
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
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_rpl.h>

static char *opname(uint32_t type) {
	switch (type) {
	case TNT_OP_PING:   return "Ping";
	case TNT_OP_INSERT: return "Insert";
	case TNT_OP_DELETE: return "Delete";
	case TNT_OP_UPDATE: return "Update";
	case TNT_OP_SELECT: return "Select";
	case TNT_OP_CALL:   return "Call";
	}
	return "Unknown";
}

#if 0
int
main(int argc, char * argv[])
{
	(void)argc, (void)argv;

	struct tnt_stream s;
	tnt_xlog(&s);
	tnt_xlog_open(&s, "./log");

	struct tnt_iter i;
	tnt_iter_request(&i, &s);

	while (tnt_next(&i)) {
		struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(&s);
		printf("%s lsn: %"PRIu64", time: %f, len: %d\n",
		       opname(sx->row.op),
		       sx->hdr.lsn,
		       sx->hdr.tm, sx->hdr.len);
	}
	if (i.status == TNT_ITER_FAIL)
		printf("parsing failed: %s\n", tnt_xlog_strerror(&s));

	tnt_iter_free(&i);
	tnt_stream_free(&s);
	return 0;
}
#endif

int
main(int argc, char * argv[])
{
	(void)argc, (void)argv;

	struct tnt_stream s;
	tnt_rpl(&s);

	struct tnt_stream *sn = tnt_rpl_net(&s);
	tnt_set(sn, TNT_OPT_HOSTNAME, "127.0.0.1");
	tnt_set(sn, TNT_OPT_PORT, 33018);
	tnt_set(sn, TNT_OPT_SEND_BUF, 0);
	tnt_set(sn, TNT_OPT_RECV_BUF, 0);
	if (tnt_rpl_open(&s, 2) == -1)
		return 1;

	struct tnt_iter i;
	tnt_iter_request(&i, &s);

	while (tnt_next(&i)) {
		struct tnt_stream_rpl *sr = TNT_RPL_CAST(&s);
		printf("%s lsn: %"PRIu64", time: %f, len: %d\n",
		       opname(sr->row.op),
		       sr->hdr.lsn,
		       sr->hdr.tm, sr->hdr.len);
	}
	if (i.status == TNT_ITER_FAIL)
		printf("parsing failed\n");

	tnt_iter_free(&i);
	tnt_stream_free(&s);
	return 0;
}
