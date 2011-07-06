
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
#include <tnt_buf.h>
#include <tnt_opt.h>
#include <tnt.h>
#include <tnt_io.h>
#include <tnt_tuple.h>
#include <tnt_proto.h>
#include <tnt_leb128.h>
#include <tnt_update.h>

void
tnt_update_init(struct tnt_update *update)
{
	update->count = 0;
	update->size_enc = 0;
	STAILQ_INIT(&update->list);
}

void
tnt_update_free(struct tnt_update *update)
{
	struct tnt_update_op *op, *next;
	STAILQ_FOREACH_SAFE(op, &update->list, next, next) {
		tnt_mem_free(op->data);
		tnt_mem_free(op);
	}
}

static struct tnt_update_op*
tnt_update_alloc(struct tnt_update *update, int type, int field)
{
	struct tnt_update_op *op =
		tnt_mem_alloc(sizeof(struct tnt_update_op));
	if (op == NULL)
		return NULL;
	op->op = type;
	op->field = field;
	op->size = 0;
	op->size_leb = 0;
	op->data = NULL;
	update->count++;
	STAILQ_INSERT_TAIL(&update->list, op, next);
	return op;
}

static enum tnt_error
tnt_update_add(struct tnt_update *update,
	       enum tnt_update_type type, int field, char *data, int size)
{
	int tp;
	struct tnt_update_op *op;
	switch (type) {
	case TNT_UPDATE_ASSIGN:
		tp = TNT_PROTO_UPDATE_ASSIGN;
		break;
	case TNT_UPDATE_ADD:
		tp = TNT_PROTO_UPDATE_ADD;
		break;
	case TNT_UPDATE_AND:
		tp = TNT_PROTO_UPDATE_AND;
		break;
	case TNT_UPDATE_XOR:
		tp = TNT_PROTO_UPDATE_XOR;
		break;
	case TNT_UPDATE_OR:
		tp = TNT_PROTO_UPDATE_OR;
		break;
	case TNT_UPDATE_SPLICE:
		tp = TNT_PROTO_UPDATE_SPLICE;
		break;
	default:
		return TNT_EFAIL;
	}
	op = tnt_update_alloc(update, tp, field);
	if (op == NULL)
		return TNT_EMEMORY;

	op->size_leb = tnt_leb128_size(size);
	op->size = size;
	if (size > 0) {
		op->data = tnt_mem_alloc(size);
		if (op->data == NULL ) {
			tnt_mem_free(op);
			return TNT_EMEMORY;
		}
		memcpy(op->data, data, size);
	}

	update->size_enc += 4 + 1 + op->size_leb + op->size;
	return TNT_EOK;
}

enum tnt_error
tnt_update_assign(struct tnt_update *update, int field,
		  char *value, int value_size)
{
	return tnt_update_add(update, TNT_UPDATE_ASSIGN,
		field, value, value_size);
}

enum tnt_error
tnt_update_arith(struct tnt_update *update, int field,
		 enum tnt_update_type op, int value)
{
	if (op == TNT_UPDATE_ADD ||
	    op == TNT_UPDATE_AND ||
	    op == TNT_UPDATE_XOR ||
	    op == TNT_UPDATE_OR)
		return tnt_update_add(update, op,
			field, (char*)&value, sizeof(value));
	return TNT_EBADVAL;
}

enum tnt_error
tnt_update_splice(struct tnt_update *update, int field,
		  int offset, int length, char *list, int list_size)
{
	struct tnt_update_op *op =
		tnt_update_alloc(update, TNT_PROTO_UPDATE_SPLICE, field);
	if (op == NULL)
		return TNT_EMEMORY;

	int offset_len = tnt_leb128_size(sizeof(offset)),
	    length_len = tnt_leb128_size(sizeof(length)),
	    list_len   = tnt_leb128_size(sizeof(list_size));

	op->size = offset_len + sizeof(offset) +
		length_len + sizeof(length) +
		list_len + list_size;

	op->size_leb = tnt_leb128_size(op->size);

	op->data = tnt_mem_alloc(op->size);
	if (op->data == NULL ) {
		tnt_mem_free(op);
		return TNT_EMEMORY;
	}

	char *p = op->data;
	tnt_leb128_write(p, sizeof(offset));
	p += offset_len;
	memcpy(p, &offset, sizeof(offset));
	p += sizeof(offset);

	tnt_leb128_write(p, sizeof(length));
	p += length_len;
	memcpy(p, &length, sizeof(length));
	p += sizeof(length);

	tnt_leb128_write(p, list_size);
	p += list_len;
	memcpy(p, list, list_size);
	p += list_size;

	update->size_enc += 4 + 1 + op->size_leb + op->size;
	return TNT_EOK;
}

static enum tnt_error
tnt_update_pack(struct tnt_update *update, char **data, int *size)
{
	if (update->count == 0)
		return TNT_EEMPTY;

	/* <count><operation>+ */
	*size = 4 + update->size_enc;
	*data = tnt_mem_alloc(*size);
	if (*data == NULL)
		return TNT_EMEMORY;

	char *p = *data;
	memcpy(p, &update->count, sizeof(unsigned long));
	p += 4;

	/*  <operation> ::= <field_no><op_code><op_arg>
	    <field_no>  ::= <int32>
	    <op_code>   ::= <int8> 
	    <op_arg>    ::= <field>
	    <field>     ::= <int32_varint><data>
	    <data>      ::= <int8>+
	*/
	struct tnt_update_op *op;
	STAILQ_FOREACH(op, &update->list, next) {
		memcpy(p, (void*)&op->field, sizeof(unsigned long));
		p += sizeof(unsigned long);
		memcpy(p, (void*)&op->op, sizeof(unsigned char));
		p += sizeof(unsigned char);
		tnt_leb128_write(p, op->size);
		p += op->size_leb;
		memcpy(p, op->data, op->size); 
		p += op->size;
	}

	return TNT_EOK;
}

int
tnt_update_tuple(struct tnt *t, int reqid, int ns, int flags,
		 struct tnt_tuple *key, struct tnt_update *update)
{
	char *td;
	unsigned int ts;
	t->error = tnt_tuple_pack(key, &td, &ts);
	if (t->error != TNT_EOK)
		return -1;

	char *ud;
	int us;
	t->error = tnt_update_pack(update, &ud, &us);
	if (t->error != TNT_EOK) {
		tnt_mem_free(td);
		return -1;
	}

	struct tnt_proto_header hdr;
	hdr.type  = TNT_PROTO_TYPE_UPDATE;
	hdr.len   = sizeof(struct tnt_proto_update) + ts + us;
	hdr.reqid = reqid;

	struct tnt_proto_update hdr_update;
	hdr_update.ns = ns;
	hdr_update.flags = flags;

	struct iovec v[4];
	v[0].iov_base = &hdr;
	v[0].iov_len  = sizeof(struct tnt_proto_header);
	v[1].iov_base = &hdr_update;
	v[1].iov_len  = sizeof(struct tnt_proto_update);
	v[2].iov_base = td;
	v[2].iov_len  = ts;
	v[3].iov_base = ud;
	v[3].iov_len  = us;

	t->error = tnt_io_sendv(t, v, 4);
	tnt_mem_free(td);
	tnt_mem_free(ud);
	return (t->error == TNT_EOK) ? 0 : -1;
}

int
tnt_update(struct tnt *t, int reqid, int ns, int flags,
	   char *key, int key_size, struct tnt_update *update)
{
	struct tnt_tuple k;
	tnt_tuple_init(&k, 1);

	t->error = tnt_tuple_set(&k, 0, key, key_size);
	if (t->error != TNT_EOK)
		return -1;

	int result = tnt_update_tuple(t, reqid, ns, flags, &k, update);
	tnt_tuple_free(&k);
	return result;
}
