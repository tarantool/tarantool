
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
#include <connector/c/include/tarantool/tnt_tuple.h>
#include <connector/c/include/tarantool/tnt_proto.h>
#include <connector/c/include/tarantool/tnt_reply.h>

/*
 * tnt_reply_init()
 *
 * initialize reply object;
 *
 * r - reply object pointer
*/
void tnt_reply_init(struct tnt_reply *r) {
	memset(r, 0, sizeof(struct tnt_reply));
}

/*
 * tnt_reply_free()
 *
 * free reply object;
 *
 * r - reply object pointer
*/
void tnt_reply_free(struct tnt_reply *r) {
	if (r->error)
		tnt_mem_free(r->error);
	tnt_list_free(&r->tuples);
}

/*
 * tnt_reply_from()
 *
 * process iproto reply with supplied recv function;
 *
 * r   - reply object pointer
 * rcv - supplied recv function
 * ptr - recv function argument
 * 
 * returns zero on fully read reply, or -1 on error.
*/
int tnt_reply_from(struct tnt_reply *r, tnt_reply_t rcv, void *ptr) {
	/* reading iproto header */
	struct tnt_header hdr;
	if (rcv(ptr, (char*)&hdr, sizeof(struct tnt_header)) == -1)
		return -1;
	uint32_t size = hdr.len;

	tnt_list_init(&r->tuples);
	r->count = 0;
	r->error = NULL;
	r->reqid = hdr.reqid;
	r->code = 0;
	r->op = hdr.type;

	/* ping is the simplest case */
	if (r->op == TNT_OP_PING)
		return 0;

	/* checking validity of operation */
	if (r->op != TNT_OP_INSERT &&
	    r->op != TNT_OP_UPDATE &&
	    r->op != TNT_OP_DELETE &&
	    r->op != TNT_OP_SELECT &&
	    r->op != TNT_OP_CALL)
		return -1;

	/* reading code */
	if (rcv(ptr, (char*)&r->code, sizeof(r->code)) == -1)
		return -1;
	size -= 4;

	/* error handling */
	if (r->code != 0) {
		r->error = tnt_mem_alloc(size);
		if (r->error == NULL)
			return -1;
		if (rcv(ptr, r->error, size) == -1) {
			tnt_mem_free(r->error);
			return -1;
		}
		return 0;
	}

	/* code only (BOX_QUIET flag) */
	if (size == 0)
		return 0;

	/* reading count */
	if (rcv(ptr, (char*)&r->count, sizeof(r->count)) == -1)
		return -1;
	size -= 4;

	/* count only */
	if (size == 0)
		return 0;

	/* allocating and reading data */
	char *buf = tnt_mem_alloc(size);
	if (buf == NULL)
		return -1;
	if (rcv(ptr, buf, size) == -1) {
		tnt_mem_free(buf);
		return -1;
	}
	char *p = buf;

	/* processing tuples */
	uint32_t total = 0;
	uint32_t i;
	for (i = 0 ; i < r->count ; i++) {
		uint32_t tsize = *(uint32_t*)(p); /* tuple size */
		if (tsize > (size - total))
			goto rollback;
		p += 4;
		/* [count, enc0, data0, ...] */
		struct tnt_tuple *t = tnt_tuple_set(NULL, p, tsize + 4 /* count */);
		if (t == NULL)
			goto rollback;
		tnt_list_at(&r->tuples, t);
		p += tsize + 4;
		total += (4 + 4 + tsize); /* length + cardinality + tuple size */
	}
	tnt_mem_free(buf);
	return 0;

rollback:
	tnt_list_free(&r->tuples);
	tnt_mem_free(buf);
	return -1;
}

/*
 * tnt_reply()
 *
 * process buffer as iproto reply;
 *
 * r    - reply object pointer
 * buf  - buffer data pointer
 * size - buffer data size
 * off  - returned offset, maybe NULL
 * 
 * if reply is fully read, then zero is returned and offset set to the
 * end of reply data in buffer.
 *
 * if reply is not complete, then 1 is returned and offset set to the
 * size needed to read.
 *
 * if there were error while parsing reply, -1 is returned.
 *
 * returns zero on fully read reply, or NULL on error.
*/
static ssize_t tnt_reply_cb(void *ptr[2], char *buf, ssize_t size) {
	char *src = ptr[0];
	ssize_t *off = ptr[1];
	memcpy(buf, src + *off, size);
	*off += size;
	return size;
}

int
tnt_reply(struct tnt_reply *r, char *buf, size_t size, size_t *off) {
	/* supplied buffer must contain full reply,
	 * if it doesn't then returning count of bytes
	 * needed to process */
	if (size < (sizeof(struct tnt_header))) {
		if (off)
			*off = sizeof(struct tnt_header) - size;
		return 1;
	}
	struct tnt_header *hdr = (struct tnt_header*)buf;
	if (size < sizeof(struct tnt_header) + hdr->len) {
		if (off)
			*off = (sizeof(struct tnt_header) + hdr->len) - size;
		return 1;
	}
	size_t offv = 0;
	void *ptr[2] = { buf, &offv };
	int rc = tnt_reply_from(r, (tnt_reply_t)tnt_reply_cb, ptr);
	if (off)
		*off = offv;
	return rc;
}
