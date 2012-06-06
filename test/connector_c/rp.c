
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
#include <connector/c/include/tarantool/tnt_xlog.h>

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
		struct tnt_request *r = TNT_IREQUEST_PTR(&i);
		switch (r->type) {
		case TNT_REQUEST_NONE:
			printf("unknown?!\n");
			continue;
		case TNT_REQUEST_PING:
			printf("ping:");
			break;
		case TNT_REQUEST_INSERT:
			printf("insert:");
			break;
		case TNT_REQUEST_DELETE:
			printf("delete:");
			break;
		case TNT_REQUEST_UPDATE:
			printf("update:");
			break;
		case TNT_REQUEST_CALL:
			printf("call:");
			break;
		case TNT_REQUEST_SELECT:
			printf("select:");
			break;
		}
		struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(&s);
		printf(" lsn: %"PRIu64", time: %f, len: %d\n",
		       sx->hdr.lsn,
		       sx->hdr.tm, sx->hdr.len);
	}
	if (i.status == TNT_ITER_FAIL)
		printf("parsing failed: %s\n", tnt_xlog_strerror(&s));

	tnt_iter_free(&i);
	tnt_stream_free(&s);
	return 0;
}
