
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

#include <connector/c/include/tarantool/tnt_mem.h>
#include <connector/c/include/tarantool/tnt_proto.h>
#include <connector/c/include/tarantool/tnt_tuple.h>
#include <connector/c/include/tarantool/tnt_reply.h>
#include <connector/c/include/tarantool/tnt_stream.h>
#include <connector/c/include/tarantool/tnt_iter.h>
#include <connector/c/include/tarantool/tnt_select.h>

struct tnt_header_select {
	uint32_t ns;
	uint32_t index;
	uint32_t offset;
	uint32_t limit;
};

/*
 * tnt_select()
 *
 * write select request to stream;
 *
 * s     - stream pointer
 * ns    - space
 * index - request index
 * offset- request offset
 * limit - request limit
 * keys  - list of tuples keys
 * 
 * returns number of bytes written, or -1 on error.
*/
ssize_t
tnt_select(struct tnt_stream *s,
	   uint32_t ns,
	   uint32_t index, uint32_t offset, uint32_t limit,
	   struct tnt_list *keys)
{
	/* calculating tuples sizes */
	size_t size = 0;
	struct tnt_iter i;
	tnt_iter_list(&i, keys);
	while (tnt_next(&i)) {
		struct tnt_tuple *t = TNT_ILIST_TUPLE(&i);
		size += t->size;
	}
	/* filling major header */
	struct tnt_header hdr;
	hdr.type = TNT_OP_SELECT;
	hdr.len = sizeof(struct tnt_header_select) + 4 + size;
	hdr.reqid = s->reqid;
	/* filling select header */
	struct tnt_header_select hdr_sel;
	hdr_sel.ns = ns;
	hdr_sel.index = index;
	hdr_sel.offset = offset;
	hdr_sel.limit = limit;
	/* allocating write vector */
	int vc = 3 + keys->count;
	struct iovec *v = tnt_mem_alloc(sizeof(struct iovec) * vc);
	if (v == NULL) {
		tnt_iter_free(&i);
		return -1;
	}
	/* filling write vector */
	v[0].iov_base = &hdr;
	v[0].iov_len  = sizeof(struct tnt_header);
	v[1].iov_base = &hdr_sel;
	v[1].iov_len  = sizeof(struct tnt_header_select);
	v[2].iov_base = &keys->count;
	v[2].iov_len  = 4;
	int vi = 3;
	tnt_rewind(&i);
	while (tnt_next(&i)) {
		struct tnt_tuple *t = TNT_ILIST_TUPLE(&i);
		v[vi].iov_base = t->data;
		v[vi].iov_len  = t->size;
		vi++;
	}
	tnt_iter_free(&i);
	/* writing data to stream */
	ssize_t rc = s->writev(s, v, vc);
	tnt_mem_free(v);
	return rc;
}
