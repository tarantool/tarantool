
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

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/uio.h>

#include <tnt_queue.h>
#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_opt.h>
#include <tnt_buf.h>
#include <tnt_main.h>
#include <tnt_io.h>
#include <tnt_tuple.h>
#include <tnt_proto.h>
#include <tnt_leb128.h>
#include <tnt_call.h>

int
tnt_call_tuple(struct tnt *t, int reqid, int flags, char *proc,
	       struct tnt_tuple *args)
{
	char *data_enc;
	unsigned int data_enc_size;
	t->error = tnt_tuple_pack(args, &data_enc, &data_enc_size);
	if (t->error != TNT_EOK)
		return -1;

	int proc_len = strlen(proc);
	int proc_enc_size = tnt_leb128_size(proc_len);
	char proc_enc[5];
	tnt_leb128_write(proc_enc, proc_len);

	struct tnt_proto_header hdr;
	hdr.type = TNT_PROTO_TYPE_CALL;
	hdr.len = sizeof(struct tnt_proto_call) + proc_enc_size + proc_len +
		  sizeof(uint32_t) + data_enc_size - 4;
	hdr.reqid = reqid;

	struct tnt_proto_call hdr_call;
	hdr_call.flags = flags;

	struct iovec v[6];
	v[0].iov_base = &hdr;
	v[0].iov_len  = sizeof(struct tnt_proto_header);
	v[1].iov_base = &hdr_call;
	v[1].iov_len  = sizeof(struct tnt_proto_call);
	v[2].iov_base = proc_enc;
	v[2].iov_len  = proc_enc_size;
	v[3].iov_base = proc;
	v[3].iov_len  = proc_len;
	v[4].iov_base = &TNT_TUPLE_COUNT(args);
	v[4].iov_len  = sizeof(uint32_t);
	/* skipping tuple cardinality */
	v[5].iov_base = data_enc + 4;
	v[5].iov_len  = data_enc_size - 4;

	t->error = tnt_io_sendv(t, v, 6);
	tnt_mem_free(data_enc);
	return (t->error == TNT_EOK) ? 0 : -1;
}

int tnt_call(struct tnt *t, int reqid, int flags, char *proc,
	     char *fmt, ...)
{
	struct tnt_tuple args;
	tnt_tuple_init(&args);

	va_list va;
	va_start(va, fmt);
	char *p = fmt;
	while (*p) {
		if (isspace(*p)) {
			p++;
			continue;
		} else
		if (*p != '%')
			return -1;
		p++;
		switch (*p) {
		case '*': {
			if (*(p + 1) == 's') {
				int len = va_arg(va, int);
				char *s = va_arg(va, char*);
				tnt_tuple_add(&args, s, len);
				p += 2;
			} else {
				goto error;
			}
			break;
		}
		case 's': {
			char *s = va_arg(va, char*);
			tnt_tuple_add(&args, s, strlen(s));
			p++;
			break;
		}
		case 'd': {
			int i = va_arg(va, int);
			tnt_tuple_add(&args, (char*)&i, sizeof(int));
			p++;
			break;
		}	
		case 'u':
			if (*(p + 1) == 'l') {
				if (*(p + 2) == 'l') {
					unsigned long long int ull = va_arg(va, unsigned long long);
					tnt_tuple_add(&args, (char*)&ull, sizeof(unsigned long long int));
					p += 3;
				} else {
					unsigned long int ul = va_arg(va, unsigned long int);
					tnt_tuple_add(&args, (char*)&ul, sizeof(unsigned long int));
					p += 2;
				}
			} else {
				goto error;
			}
			break;
		case 'l':
			if (*(p + 1) == 'l') {
				long long int ll = va_arg(va, int);
				tnt_tuple_add(&args, (char*)&ll, sizeof(long long int));
				p += 2;
			} else {
				long int l = va_arg(va, int);
				tnt_tuple_add(&args, (char*)&l, sizeof(long int));
				p++;
			}
			break;
		default:
			goto error;
		}
	}
	va_end(va);

	int ret = tnt_call_tuple(t, reqid, flags, proc, &args);
	tnt_tuple_free(&args);
	return ret;
error:
	tnt_tuple_free(&args);
	return -1;
}
