
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

#include <tnt_queue.h>
#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_opt.h>
#include <tnt_buf.h>
#include <tnt_main.h>
#include <tnt_io.h>
#include <tnt_tuple.h>
#include <tnt_proto.h>
#include <tnt_recv.h>

void
tnt_recv_init(struct tnt_recv *rcv)
{
	rcv->count = 0;
	rcv->reqid = 0;
	rcv->code = 0;
	rcv->error = NULL;
	tnt_tuples_init(&rcv->tuples);
}

void
tnt_recv_free(struct tnt_recv *rcv)
{
	tnt_tuples_free(&rcv->tuples);
	if (rcv->error)
		tnt_mem_free(rcv->error);
}

char*
tnt_recv_error(struct tnt_recv *rcv)
{
	return rcv->error;
}

static enum tnt_error
tnt_recv_fqtuple(struct tnt_recv *rcv, char *data, unsigned long size,
                 unsigned long count)
{
	char *p = data;
	unsigned long i, off = 0;
	for (i = 0 ; i < count ; i++) {
		unsigned long s = *(unsigned long*)p;
		if (s > (unsigned long)(size - off)) {
			tnt_tuples_free(&rcv->tuples);
			return TNT_EPROTO;
		}
		off += 4, p += 4;
		s   += 4;
		enum tnt_error r = tnt_tuples_unpack(&rcv->tuples, p, s);
		if (r != TNT_EOK) {
			tnt_tuples_free(&rcv->tuples);
			return r;
		}
		off += s, p += s;
	}

	return TNT_EOK;
}

int
tnt_recv(struct tnt *t, struct tnt_recv *rcv)
{
	char buffer[sizeof(struct tnt_proto_header_resp) + 4];
	struct tnt_proto_header_resp *hdr =
		(struct tnt_proto_header_resp*)buffer;

	t->error = tnt_io_recv(t, buffer, sizeof(struct tnt_proto_header));
	if (t->error != TNT_EOK)
		return -1;
	int size = hdr->hdr.len;

	rcv->reqid = hdr->hdr.reqid;
	switch (hdr->hdr.type) {
	case TNT_PROTO_TYPE_PING:
		rcv->op = TNT_RECV_PING;
		return TNT_EOK;
	case TNT_PROTO_TYPE_INSERT:
		rcv->op = TNT_RECV_INSERT;
		break;
	case TNT_PROTO_TYPE_UPDATE:
		rcv->op = TNT_RECV_UPDATE;
		break;
	case TNT_PROTO_TYPE_DELETE:
		rcv->op = TNT_RECV_DELETE;
		break;
	case TNT_PROTO_TYPE_SELECT:
		rcv->op = TNT_RECV_SELECT;
		break;
	default:
		return TNT_EPROTO;
	}

	t->error = tnt_io_recv(t, buffer + sizeof(struct tnt_proto_header),
		sizeof(struct tnt_proto_header_resp) -
		sizeof(struct tnt_proto_header));
	if (t->error != TNT_EOK)
		return -1;
	size -= 4;

	/* error handling */
	rcv->code = hdr->code;
	if (!TNT_PROTO_IS_OK(hdr->code)) {
		t->error = TNT_EERROR;
		rcv->error = tnt_mem_alloc(size);
		if (rcv->error == NULL) {
			t->error = TNT_EMEMORY;
			return -1;
		}
		t->error = tnt_io_recv(t, rcv->error, size);
		if (t->error != TNT_EOK) {
			tnt_mem_free(rcv->error);
			rcv->error = NULL;
			return -1;
		}
		return 0;
	}

	/* code only (BOX_QUIET flag) */
	if (size == 0)
		return 0;

	if ((rcv->op != TNT_RECV_SELECT) && (size == 4)) {
		/* count only (insert, update, delete) */
		t->error = tnt_io_recv(t, buffer +
			sizeof(struct tnt_proto_header_resp), 4);
		if (t->error != TNT_EOK)
			return -1;
		rcv->count = *(unsigned long*)(buffer +
			sizeof(struct tnt_proto_header_resp));
		return 0;
	} 

	char *data = tnt_mem_alloc(size);
	if (data == NULL) {
		t->error = TNT_EMEMORY;
		return -1;
	}
	char *p = data;
	t->error = tnt_io_recv(t, p, size);
	if (t->error != TNT_EOK) {
		tnt_mem_free(data);
		return -1;
	}

	rcv->count = *(unsigned long*)(p);
	p += sizeof(unsigned long);
	size -= 4;

	switch (rcv->op) {
	/* <insert_response_body> ::= <count> | <count><fq_tuple> 
	   <update_response_body> ::= <insert_response_body>
	*/
	case TNT_RECV_INSERT:
	case TNT_RECV_UPDATE:
		t->error = tnt_recv_fqtuple(rcv, p, size, 1);
		break;
	/* <delete_response_body> ::= <count> */
	case TNT_RECV_DELETE:
		/* unreach */
		break;
	/* <select_response_body> ::= <count><fq_tuple>* */
	case TNT_RECV_SELECT:
		/* fq_tuple* */
		t->error = tnt_recv_fqtuple(rcv, p, size, rcv->count);
		break;
	default:
		t->error = TNT_EPROTO;
		break;
	}
	if (data)
		tnt_mem_free(data);
	return (t->error == TNT_EOK) ? 0 : -1;
}
