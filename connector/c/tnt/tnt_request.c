
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

#include <tnt_mem.h>
#include <tnt_enc.h>
#include <tnt_tuple.h>
#include <tnt_proto.h>
#include <tnt_request.h>

void
tnt_request_init(struct tnt_request *r)
{
	memset(r, 0, sizeof(struct tnt_request));
}

void
tnt_request_free(struct tnt_request *r)
{
	switch (r->type) {
	case TNT_REQUEST_INSERT:
		tnt_tuple_free(&r->r.insert.t);
		break;
	case TNT_REQUEST_DELETE:
		tnt_tuple_free(&r->r.delete.t);
		break;
	case TNT_REQUEST_CALL:
		if (r->r.call.proc)
			tnt_mem_free(r->r.call.proc);
		tnt_tuple_free(&r->r.call.t);
		break;
	case TNT_REQUEST_SELECT:
		tnt_list_free(&r->r.select.l);
		break;
	case TNT_REQUEST_UPDATE:
		break;
	case TNT_REQUEST_PING:
	case TNT_REQUEST_NONE:
		break;
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
		free(buf);
		return -1;
	}
	if (tnt_tuple_set(&r->r.insert.t, buf, size) == NULL) {
		free(buf);
		return -1;
	}
	free(buf);
	r->type = TNT_REQUEST_INSERT;
	return 0;
}

static int
tnt_request_delete(struct tnt_request *r, tnt_request_t rcv, void *ptr)
{
	if (rcv(ptr, (char*)&r->r.delete, sizeof(struct tnt_header_delete)) == -1)
		return -1;
	uint32_t size = r->h.len - sizeof(struct tnt_header_delete);
	char *buf = tnt_mem_alloc(size);
	if (buf == NULL)
		return -1;
	if (rcv(ptr, buf, size) == -1) {
		free(buf);
		return -1;
	}
	if (tnt_tuple_set(&r->r.delete.t, buf, size) == NULL) {
		free(buf);
		return -1;
	}
	free(buf);
	r->type = TNT_REQUEST_DELETE;
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

	r->r.call.proc_enc_len = esize;
	r->r.call.proc = tnt_mem_alloc(r->r.call.proc_len + 1);
	if (r->r.call.proc == NULL)
		goto error;

	memcpy(r->r.call.proc, buf + esize, r->r.call.proc_len);
	r->r.call.proc[r->r.call.proc_len] = 0;

	size -= esize + r->r.call.proc_len;
	if (tnt_tuple_set(&r->r.call.t, buf + esize + r->r.call.proc_len, size) == NULL) {
		tnt_mem_free(r->r.call.proc);
		r->r.call.proc = NULL;
		goto error;
	}
	tnt_mem_free(buf);
	r->type = TNT_REQUEST_CALL;
	return 0;
error:
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

	uint32_t i, count = *(uint32_t*)buf;
	uint32_t off = 4;

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
	r->type = TNT_REQUEST_SELECT;
	tnt_mem_free(buf);
	return 0;
error:
	if (buf)
		tnt_mem_free(buf);
	tnt_list_free(&r->r.select.l);
	return -1;
}

int
tnt_request_from(struct tnt_request *r, tnt_request_t rcv, void *ptr)
{
	if (rcv(ptr, (char*)&r->h, sizeof(struct tnt_header)) == -1)
		return -1;
	switch (r->h.type) {
	case TNT_OP_INSERT: return tnt_request_insert(r, rcv, ptr);
	case TNT_OP_DELETE: return tnt_request_delete(r, rcv, ptr);
	case TNT_OP_CALL:   return tnt_request_call(r, rcv, ptr);
	case TNT_OP_SELECT: return tnt_request_select(r, rcv, ptr);
	case TNT_OP_PING:   return 0;
	default: return -1;
	}
	return 0;
}

static ssize_t tnt_request_cb(void *ptr[2], char *buf, ssize_t size) {
	char *src = ptr[0];
	ssize_t *off = ptr[1];
	memcpy(buf, src + *off, size);
	*off += size;
	return size;
}

int tnt_request(struct tnt_request *r, char *buf, size_t size, size_t *off) {
	if (size < (sizeof(struct tnt_header))) {
		if (off)
			*off = sizeof(struct tnt_header) - size;
		return 1;
	}
	struct tnt_header *hdr = (struct tnt_header*)buf;
	if (size < hdr->len) {
		if (off)
			*off = hdr->len - size;
		return 1;
	}
	size_t offv = 0;
	void *ptr[2] = { buf, &offv };
	int rc = tnt_request_from(r, (tnt_request_t)tnt_request_cb, ptr);
	if (off)
		*off = offv;
	return rc;
}
