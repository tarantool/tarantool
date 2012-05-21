
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

#include <tarantool/tnt_mem.h>
#include <tarantool/tnt_proto.h>
#include <tarantool/tnt_enc.h>
#include <tarantool/tnt_tuple.h>
#include <tarantool/tnt_reply.h>
#include <tarantool/tnt_stream.h>
#include <tarantool/tnt_buf.h>
#include <tarantool/tnt_update.h>

static ssize_t
tnt_update_op(struct tnt_stream *s,
	      uint32_t field, uint8_t op, char *data, uint32_t size) 
{
	/* encoding size */
	int encs = tnt_enc_size(size);
	char enc[5];
	tnt_enc_write(enc, size);
	struct iovec iov[4];
	int iovc = 3;
	/* field */
	iov[0].iov_base = &field;
	iov[0].iov_len = 4;
	/* operation */
	iov[1].iov_base = &op;
	iov[1].iov_len = 1;
	/* encoding size */
	iov[2].iov_base = enc;
	iov[2].iov_len = encs;
	/* data */
	if (data) {
		iov[3].iov_base = data;
		iov[3].iov_len = size;
		iovc++;
	}
	return s->writev(s, iov, iovc);
}

/*
 * tnt_update_arith()
 *
 * write 32-bit arithmetic update operation to buffer stream;
 *
 * s     - stream buffer pointer
 * field - field number
 * op    - update operation type
 * value - update operation value
 * 
 * returns number of bytes written, or -1 on error.
*/
ssize_t
tnt_update_arith(struct tnt_stream *s, uint32_t field,
		 uint8_t op, uint32_t value)
{
	return tnt_update_op(s, field, op, (char*)&value, sizeof(value));
}

/*
 * tnt_update_arith_i32()
 *
 * write 32-bit arithmetic update operation to buffer stream;
 *
 * s     - stream buffer pointer
 * field - field number
 * op    - update operation type
 * value - update operation value
 * 
 * returns number of bytes written, or -1 on error.
*/
ssize_t
tnt_update_arith_i32(struct tnt_stream *s, uint32_t field,
		     uint8_t op, uint32_t value)
{
	return tnt_update_op(s, field, op, (char*)&value, sizeof(value));
}

/*
 * tnt_update_arith_i64()
 *
 * write 64-bit arithmetic update operation to buffer stream;
 *
 * s     - stream buffer pointer
 * field - field number
 * op    - update operation type
 * value - update operation value
 * 
 * returns number of bytes written, or -1 on error.
*/
ssize_t
tnt_update_arith_i64(struct tnt_stream *s, uint32_t field,
		     uint8_t op, uint64_t value)
{
	return tnt_update_op(s, field, op, (char*)&value, sizeof(value));
}

/*
 * tnt_update_assign()
 *
 * write assign update operation to buffer stream;
 *
 * s     - stream buffer pointer
 * field - field number
 * data  - update operation data
 * value - update operation data size
 * 
 * returns number of bytes written, or -1 on error.
*/
ssize_t
tnt_update_assign(struct tnt_stream *s, uint32_t field,
		  char *data, uint32_t size)
{
	return tnt_update_op(s, field, TNT_UPDATE_ASSIGN, data, size);
}

/*
 * tnt_update_splice()
 *
 * write update splice operation to buffer stream;
 *
 * s      - stream buffer pointer
 * field  - field number
 * offset - splice offset
 * length - splice length
 * data   - splice operation data
 * value  - splice operation data size
 * 
 * returns number of bytes written, or -1 on error.
*/
ssize_t
tnt_update_splice(struct tnt_stream *s, uint32_t field,
		  uint32_t offset,
		  int32_t length, char *data, size_t size)
{
	/* calculating splice data sizes */
	uint32_t offset_len = tnt_enc_size(sizeof(offset)),
	         length_len = tnt_enc_size(sizeof(length)),
	         data_len   = tnt_enc_size(size);
	uint32_t sz = offset_len + sizeof(offset) +
		      length_len + sizeof(length) + data_len + size;
	/* allocating splice request buffer */
	char *buf = tnt_mem_alloc(sz);
	if (buf == NULL)
		return -1;
	/* filling splice request data */
	char *p = buf;
	tnt_enc_write(p, sizeof(offset));
	p += offset_len;
	memcpy(p, &offset, sizeof(offset));
	p += sizeof(offset);
	tnt_enc_write(p, sizeof(length));
	p += length_len;
	memcpy(p, &length, sizeof(length));
	p += sizeof(length);
	tnt_enc_write(p, size);
	p += data_len;
	memcpy(p, data, size);
	p += size;
	/* writing splice request */
	ssize_t rc = tnt_update_op(s, field, TNT_UPDATE_SPLICE, buf, sz);
	tnt_mem_free(buf);
	return rc;
}

/*
 * tnt_update_delete()
 *
 * write update delete operation to buffer stream;
 *
 * s      - stream buffer pointer
 * field  - field number
 *
 * returns number of bytes written, or -1 on error.
*/
ssize_t
tnt_update_delete(struct tnt_stream *s, uint32_t field)
{
	return tnt_update_op(s, field, TNT_UPDATE_DELETE, NULL, 0);
}

ssize_t
tnt_update_insert(struct tnt_stream *s, uint32_t field,
			 char *data, uint32_t size)
{
	return tnt_update_op(s, field, TNT_UPDATE_INSERT, data, size);
}

struct tnt_header_update {
	uint32_t ns;
	uint32_t flags;
};

/*
 * tnt_update()
 *
 * write select request to stream;
 *
 * s     - stream pointer
 * ns    - space
 * flags - request flags
 * k     - update key tuple
 * ops   - stream buffer pointer
 * 
 * returns number of bytes written, or -1 on error.
*/
ssize_t
tnt_update(struct tnt_stream *s, uint32_t ns, uint32_t flags,
	   struct tnt_tuple *k,
	   struct tnt_stream *ops)
{
	/* filling major header */
	struct tnt_header hdr;
	hdr.type = TNT_OP_UPDATE;
	hdr.len = sizeof(struct tnt_header_update) +
		  k->size + 4 + TNT_SBUF_SIZE(ops);
	hdr.reqid = s->reqid;
	/* filling update header */
	struct tnt_header_update hdr_update;
	hdr_update.ns = ns;
	hdr_update.flags = flags;
	/* writing data to stream */
	struct iovec v[5];
	v[0].iov_base = &hdr;
	v[0].iov_len  = sizeof(struct tnt_header);
	v[1].iov_base = &hdr_update;
	v[1].iov_len  = sizeof(struct tnt_header_update);
	v[2].iov_base = k->data;
	v[2].iov_len  = k->size;
	v[3].iov_base = &ops->wrcnt;
	v[3].iov_len  = 4;
	v[4].iov_base = TNT_SBUF_DATA(ops);
	v[4].iov_len  = TNT_SBUF_SIZE(ops);
	return s->writev(s, v, 5);
}
