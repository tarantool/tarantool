
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
#include <connector/c/include/tarantool/tnt_enc.h>
#include <connector/c/include/tarantool/tnt_tuple.h>
#include <connector/c/include/tarantool/tnt_proto.h>
#include <connector/c/include/tarantool/tnt_request.h>

/*
 * tnt_request_init()
 *
 * initialize request object;
 *
 * r - reply pointer
*/
void
tnt_request_init(struct tnt_request *r)
{
	memset(r, 0, sizeof(struct tnt_request));
}

/*
 * tnt_request_free()
 *
 * free request object;
 *
 * r - request object pointer
*/
void
tnt_request_free(struct tnt_request *r)
{
	switch (r->h.type) {
	case TNT_OP_INSERT:
		tnt_tuple_free(&r->r.insert.t);
		break;
	case TNT_OP_DELETE:
		tnt_tuple_free(&r->r.del.t);
		break;
	case TNT_OP_CALL:
		if (r->r.call.proc) {
			tnt_mem_free(r->r.call.proc);
			r->r.call.proc = NULL;
		}
		tnt_tuple_free(&r->r.call.t);
		break;
	case TNT_OP_SELECT:
		tnt_list_free(&r->r.select.l);
		break;
	case TNT_OP_UPDATE:
		tnt_tuple_free(&r->r.update.t);
		if (r->r.update.ops) {
			tnt_mem_free(r->r.update.ops);
			r->r.update.ops = NULL;
		}
		if (r->r.update.opv) {
			tnt_mem_free(r->r.update.opv);
			r->r.update.opv = NULL;
		}
		break;
	case TNT_OP_PING:
		break;
	}
	if (r->v) {
		tnt_mem_free(r->v);
		r->v = NULL;
	}
}

static int
tnt_request_insert(struct tnt_request *r, tnt_request_t rcv, void *ptr)
{
	if (rcv(ptr, (char*)&r->r.insert, sizeof(struct tnt_header_insert)) == -1)
		return -1;
	uint32_t size = r->h.len - sizeof(struct tnt_header_insert);
	char *buf = tnt_mem_alloc(size);
	if (buf == NULL)
		return -1;
	if (rcv(ptr, buf, size) == -1) {
		tnt_mem_free(buf);
		return -1;
	}
	if (tnt_tuple_set(&r->r.insert.t, buf, size) == NULL) {
		tnt_mem_free(buf);
		return -1;
	}
	/* creating resend io vector */
	r->vc = 3;
	r->v = tnt_mem_alloc(r->vc * sizeof(struct iovec));
	if (r->v == NULL) {
		tnt_tuple_free(&r->r.insert.t);
		tnt_mem_free(buf);
		return -1;
	}
	r->v[0].iov_base = &r->h;
	r->v[0].iov_len  = sizeof(struct tnt_header);
	r->v[1].iov_base = &r->r.insert.h;
	r->v[1].iov_len  = sizeof(struct tnt_header_insert);
	r->v[2].iov_base = r->r.insert.t.data;
	r->v[2].iov_len  = r->r.insert.t.size;
	tnt_mem_free(buf);
	return 0;
}

static int
tnt_request_delete(struct tnt_request *r, tnt_request_t rcv, void *ptr)
{
	if (rcv(ptr, (char*)&r->r.del, sizeof(struct tnt_header_delete)) == -1)
		return -1;
	uint32_t size = r->h.len - sizeof(struct tnt_header_delete);
	char *buf = tnt_mem_alloc(size);
	if (buf == NULL)
		return -1;
	if (rcv(ptr, buf, size) == -1) {
		tnt_mem_free(buf);
		return -1;
	}
	if (tnt_tuple_set(&r->r.del.t, buf, size) == NULL) {
		tnt_mem_free(buf);
		return -1;
	}
	/* creating resend io vector */
	r->vc = 3;
	r->v = tnt_mem_alloc(r->vc * sizeof(struct iovec));
	if (r->v == NULL) {
		tnt_tuple_free(&r->r.del.t);
		tnt_mem_free(buf);
		return -1;
	}
	r->v[0].iov_base = &r->h;
	r->v[0].iov_len  = sizeof(struct tnt_header);
	r->v[1].iov_base = &r->r.del.h;
	r->v[1].iov_len  = sizeof(struct tnt_header_delete);
	r->v[2].iov_base = r->r.del.t.data;
	r->v[2].iov_len  = r->r.del.t.size;
	tnt_mem_free(buf);
	return 0;
}

static int
tnt_request_call(struct tnt_request *r, tnt_request_t rcv, void *ptr)
{
	if (rcv(ptr, (char*)&r->r.call, sizeof(struct tnt_header_call)) == -1)
		return -1;
	uint32_t size = r->h.len - sizeof(struct tnt_header_call);
	char *buf = tnt_mem_alloc(size);
	if (buf == NULL)
		goto error;
	if (rcv(ptr, buf, size) == -1)
		goto error;
	int esize = tnt_enc_read(buf, &r->r.call.proc_len);
	if (esize == -1 || esize >= 5)
		goto error;
	memcpy(r->r.call.proc_enc, buf, esize);
	/* function name */
	r->r.call.proc_enc_len = esize;
	r->r.call.proc = tnt_mem_alloc(r->r.call.proc_len + 1);
	if (r->r.call.proc == NULL)
		goto error;
	memcpy(r->r.call.proc, buf + esize, r->r.call.proc_len);
	r->r.call.proc[r->r.call.proc_len] = 0;
	/* function arguments */
	size -= esize + r->r.call.proc_len;
	if (tnt_tuple_set(&r->r.call.t, buf + esize + r->r.call.proc_len, size) == NULL) {
		tnt_mem_free(r->r.call.proc);
		r->r.call.proc = NULL;
		goto error;
	}
	/* creating resend io vector */
	r->vc = 5;
	r->v = tnt_mem_alloc(r->vc * sizeof(struct iovec));
	if (r->v == NULL)
		goto error;
	r->v[0].iov_base = &r->h;
	r->v[0].iov_len  = sizeof(struct tnt_header);
	r->v[1].iov_base = &r->r.call.h;
	r->v[1].iov_len  = sizeof(struct tnt_header_call);
	r->v[2].iov_base = r->r.call.proc_enc;
	r->v[2].iov_len  = r->r.call.proc_enc_len;
	r->v[3].iov_base = r->r.call.proc;
	r->v[3].iov_len  = r->r.call.proc_len;
	r->v[4].iov_base = r->r.call.t.data;
	r->v[4].iov_len  = r->r.call.t.size;
	tnt_mem_free(buf);
	return 0;
error:
	tnt_tuple_free(&r->r.call.t);
	if (buf)
		tnt_mem_free(buf);
	return -1;
}

static int
tnt_request_select(struct tnt_request *r, tnt_request_t rcv, void *ptr)
{
	if (rcv(ptr, (char*)&r->r.select, sizeof(struct tnt_header_select)) == -1)
		return -1;
	uint32_t size = r->h.len - sizeof(struct tnt_header_select);
	char *buf = tnt_mem_alloc(size);
	if (buf == NULL)
		goto error;
	if (rcv(ptr, buf, size) == -1)
		goto error;
	/* count of tuples */
	uint32_t i, count = *(uint32_t*)buf;
	uint32_t off = 4;
	/* processing tuples */
	tnt_list_init(&r->r.select.l);
	for (i = 0 ; i < count ; i++) {
		/* calculating tuple size */
		uint32_t j, cardinality = *(uint32_t*)(buf + off);
		uint32_t size = 4;
		for (j = 0 ; j < cardinality ; j++) {
			uint32_t fld_size = 0;
			int fld_esize = tnt_enc_read(buf + off + size, &fld_size);
			if (fld_esize == -1)
				goto error;
			size += fld_esize + fld_size;
		}
		/* initializing tuple and adding to list */
		struct tnt_tuple *tu = tnt_list_at(&r->r.select.l, NULL);
		if (tnt_tuple_set(tu, buf + off, size) == NULL)
			goto error;
		off += size;
	}
	tnt_mem_free(buf);
	return 0;
error:
	tnt_list_free(&r->r.select.l);
	if (buf)
		tnt_mem_free(buf);
	return -1;
}

static int
tnt_request_update(struct tnt_request *r, tnt_request_t rcv, void *ptr)
{
	if (rcv(ptr, (char*)&r->r.update, sizeof(struct tnt_header_update)) == -1)
		return -1;
	r->r.update.opc = 0;
	r->r.update.opv = NULL;
	uint32_t size = r->h.len - sizeof(struct tnt_header_update);
	char *buf = tnt_mem_alloc(size);
	if (buf == NULL)
		goto error;
	if (rcv(ptr, buf, size) == -1)
		goto error;
	/* calculating key tuple size */
	uint32_t i, cardinality = *(uint32_t*)(buf);
	uint32_t ks = 4;
	for (i = 0 ; i < cardinality ; i++) {
		uint32_t fld_size = 0;
		int fld_esize = tnt_enc_read(buf + ks, &fld_size);
		if (fld_esize == -1)
			goto error;
		ks += fld_esize + fld_size;
	}
	/* initializing tuple */
	if (tnt_tuple_set(&r->r.update.t, buf, ks) == NULL)
		goto error;
	size -= ks - 4;

	/* ops data */
	r->r.update.opc = *(uint32_t*)(buf + ks);
	uint32_t opvsz = sizeof(struct tnt_request_update_op) * r->r.update.opc;
	r->r.update.opv = tnt_mem_alloc(opvsz);
	if (r->r.update.opv == NULL)
		goto error;
	memset(r->r.update.opv, 0, sizeof(opvsz));

	/* allocating ops buffer */
	r->r.update.ops_size = 0;
	r->r.update.ops = tnt_mem_alloc(size);
	if (r->r.update.ops == NULL)
		goto error;
	memcpy(r->r.update.ops, buf + ks + 4, size);

	/* parsing operations */
	char *p = r->r.update.ops;
	for (i = 0 ; i < r->r.update.opc ; i++) {
		struct tnt_request_update_op *op = &r->r.update.opv[i];
		/* field */
		op->field = *(uint32_t*)(p);
		p += 4;
		/* operation */
		op->op = *(uint8_t*)(p);
		p += 1;
		/* enc_size */
		int esize = tnt_enc_read(p, &op->size);
		if (esize == -1 || esize >= 5)
			goto error;
		op->size_enc_len = esize;
		memcpy(op->size_enc, p, op->size_enc_len);
		p += op->size_enc_len;
		op->data = p;
		p += op->size;
		r->r.update.ops_size += 4 + 1 + op->size_enc_len + op->size;
	}

	/* creating resend io vector */
	r->vc = 5;
	r->v = tnt_mem_alloc(r->vc * sizeof(struct iovec));
	if (r->v == NULL)
		goto error;
	r->v[0].iov_base = &r->h;
	r->v[0].iov_len  = sizeof(struct tnt_header);
	r->v[1].iov_base = &r->r.update.h;
	r->v[1].iov_len  = sizeof(struct tnt_header_update);
	r->v[2].iov_base = r->r.update.t.data;
	r->v[2].iov_len  = r->r.update.t.size;
	r->v[3].iov_base = &r->r.update.opc;
	r->v[3].iov_len  = 4;
	r->v[4].iov_base = r->r.update.ops;
	r->v[4].iov_len  = r->r.update.ops_size;
	tnt_mem_free(buf);
	return 0;
error:
	tnt_tuple_free(&r->r.update.t);
	if (r->r.update.ops) {
		tnt_mem_free(r->r.update.ops);
		r->r.update.ops = NULL;
	}
	if (r->r.update.opv) {
		tnt_mem_free(r->r.update.opv);
		r->r.update.opv = NULL;
	}
	if (buf)
		tnt_mem_free(buf);
	return -1;
}

/*
 * tnt_request_from()
 *
 * process iproto request with supplied recv function;
 *
 * r   - request object pointer
 * rcv - supplied recv function
 * ptr - recv function argument
 * hdr - pointer to iproto header, may be NULL
 * 
 * returns zero on fully read reply, or -1 on error.
*/
int
tnt_request_from(struct tnt_request *r, tnt_request_t rcv, void *ptr,
		 struct tnt_header *hdr)
{
	if (hdr) {
		memcpy(&r->h, hdr, sizeof(struct tnt_header));
	} else {
		if (rcv(ptr, (char*)&r->h, sizeof(struct tnt_header)) == -1)
			return -1;
	}
	switch (r->h.type) {
	case TNT_OP_INSERT: return tnt_request_insert(r, rcv, ptr);
	case TNT_OP_DELETE: return tnt_request_delete(r, rcv, ptr);
	case TNT_OP_CALL:   return tnt_request_call(r, rcv, ptr);
	case TNT_OP_SELECT: return tnt_request_select(r, rcv, ptr);
	case TNT_OP_UPDATE: return tnt_request_update(r, rcv, ptr);
	case TNT_OP_PING:   return 0;
	}
	return -1;
}

/*
 * tnt_request()
 *
 * process buffer as iproto request (deserilization);
 *
 * r    - request object pointer
 * buf  - buffer data pointer
 * size - buffer data size
 * off  - returned offset, maybe NULL
 * hdr  - iproto header, maybe NULL
 * 
 * if request is fully read, then zero is returned and offset set to the
 * end of reply data in buffer.
 *
 * if request is not complete, then 1 is returned and offset set to the
 * size needed to read.
 *
 * if there were error while parsing reply, -1 is returned.
 *
 * returns zero on fully read reply, or NULL on error.
*/
static ssize_t tnt_request_cb(void *ptr[2], char *buf, ssize_t size) {
	char *src = ptr[0];
	ssize_t *off = ptr[1];
	memcpy(buf, src + *off, size);
	*off += size;
	return size;
}

int
tnt_request(struct tnt_request *r, char *buf, size_t size, size_t *off,
	    struct tnt_header *hdr)
{
	if (hdr == NULL) {
		if (size < (sizeof(struct tnt_header))) {
			if (off)
				*off = sizeof(struct tnt_header) - size;
			return 1;
		}
		struct tnt_header *hdr_ = (struct tnt_header*)buf;
		if (size < hdr_->len) {
			if (off)
				*off = hdr_->len - size;
			return 1;
		}
	}
	size_t offv = 0;
	void *ptr[2] = { buf, &offv };
	int rc = tnt_request_from(r, (tnt_request_t)tnt_request_cb, ptr, hdr);
	if (off)
		*off = offv;
	return rc;
}
