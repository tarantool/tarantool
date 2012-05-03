
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
#include <string.h>

#include <connector/c/include/tarantool/tnt_proto.h>
#include <connector/c/include/tarantool/tnt_tuple.h>
#include <connector/c/include/tarantool/tnt_reply.h>
#include <connector/c/include/tarantool/tnt_stream.h>
#include <connector/c/include/tarantool/tnt_delete.h>

struct tnt_header_delete {
	uint32_t ns;
};

/*
 * tnt_delete()
 *
 * write delete request to stream;
 *
 * s     - stream pointer
 * ns    - space
 * flags - request flags
 * k     - tuple key
 * 
 * returns number of bytes written, or -1 on error.
*/
ssize_t
tnt_delete(struct tnt_stream *s, uint32_t ns, uint32_t flags, struct tnt_tuple *k)
{
	/* filling major header */
	struct tnt_header hdr;
	hdr.type  = TNT_OP_DELETE;
	hdr.len = sizeof(struct tnt_header_delete) + k->size;
	hdr.reqid = s->reqid;
	/* filling delete header */
	struct tnt_header_delete hdr_del;
	hdr_del.ns = ns;
	(void)flags;
	/* writing data to stream */
	struct iovec v[3];
	v[0].iov_base = &hdr;
	v[0].iov_len  = sizeof(struct tnt_header);
	v[1].iov_base = &hdr_del;
	v[1].iov_len  = sizeof(struct tnt_header_delete);
	v[2].iov_base = k->data;
	v[2].iov_len  = k->size;
	return s->writev(s, v, 3);
}
