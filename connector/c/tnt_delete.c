
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
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <third_party/queue.h>

#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_opt.h>
#include <tnt_buf.h>
#include <tnt.h>
#include <tnt_io.h>
#include <tnt_tuple.h>
#include <tnt_proto.h>
#include <tnt_leb128.h>
#include <tnt_delete.h>

int
tnt_delete_tuple(struct tnt *t, int reqid, int ns, struct tnt_tuple *key)
{
	char *td;
	unsigned int ts;
	t->error = tnt_tuple_pack(key, &td, &ts);
	if (t->error != TNT_EOK)
		return -1;

	struct tnt_proto_header hdr;
	hdr.type  = TNT_PROTO_TYPE_DELETE;
	hdr.len   = sizeof(struct tnt_proto_delete) + ts;
	hdr.reqid = reqid;

	struct tnt_proto_delete hdr_del;
	hdr_del.ns = ns;

	struct iovec v[3];
	v[0].iov_base = &hdr;
	v[0].iov_len  = sizeof(struct tnt_proto_header);
	v[1].iov_base = &hdr_del;
	v[1].iov_len  = sizeof(struct tnt_proto_delete);
	v[2].iov_base = td;
	v[2].iov_len  = ts;

	t->error = tnt_io_sendv(t, v, 3);
	tnt_mem_free(td);
	return (t->error == TNT_EOK) ? 0 : -1;
}

int
tnt_delete(struct tnt *t, int reqid, int ns, char *key, int key_size)
{
	struct tnt_tuple k;
	tnt_tuple_init(&k, 1);

	t->error = tnt_tuple_set(&k, 0, key, key_size);
	if (t->error != TNT_EOK)
		return -1;

	int result = tnt_delete_tuple(t, reqid, ns, &k);
	tnt_tuple_free(&k);
	return result;
}
