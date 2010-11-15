/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
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

#include <util.h>
#include <tbuf.h>
#include <palloc.h>
#include <fiber.h>
#include <iproto.h>		/* for err codes */
#include <pickle.h>

/* caller must ensure that there is space in target */
u8 *
save_varint32(u8 *target, u32 value)
{

	if (value >= (1 << 7)) {
		if (value >= (1 << 14)) {
			if (value >= (1 << 21)) {
				if (value >= (1 << 28))
					*(target++) = (u8)(value >> 28) | 0x80;
				*(target++) = (u8)(value >> 21) | 0x80;
			}
			*(target++) = (u8)((value >> 14) | 0x80);
		}
		*(target++) = (u8)((value >> 7) | 0x80);
	}
	*(target++) = (u8)((value) & 0x7F);

	return target;
}

inline static void
append_byte(struct tbuf *b, u8 byte)
{
	*((u8 *)b->data + b->len) = byte;
	b->len++;
}

void
write_varint32(struct tbuf *b, u32 value)
{
	tbuf_ensure(b, 5);
	if (value >= (1 << 7)) {
		if (value >= (1 << 14)) {
			if (value >= (1 << 21)) {
				if (value >= (1 << 28))
					append_byte(b, (u8)(value >> 28) | 0x80);
				append_byte(b, (u8)(value >> 21) | 0x80);
			}
			append_byte(b, (u8)((value >> 14) | 0x80));
		}
		append_byte(b, (u8)((value >> 7) | 0x80));
	}
	append_byte(b, (u8)((value) & 0x7F));
}

#define read_u(bits)							\
	u##bits read_u##bits(struct tbuf *b)				\
	{								\
		if (b->len < (bits)/8)					\
			raise(ERR_CODE_UNKNOWN_ERROR, "buffer too short"); \
		u##bits r = *(u##bits *)b->data;			\
		b->size -= (bits)/8;					\
		b->len -= (bits)/8;					\
		b->data += (bits)/8;					\
		return r;						\
	}

read_u(8)
read_u(16)
read_u(32)
read_u(64)

u32
read_varint32(struct tbuf *buf)
{
	u8 *b = buf->data;
	int len = buf->len;

	if (len < 1)
		raise(ERR_CODE_UNKNOWN_ERROR, "buffer too short");
	if (!(b[0] & 0x80)) {
		buf->data += 1;
		buf->size -= 1;
		buf->len -= 1;
		return (b[0] & 0x7f);
	}

	if (len < 2)
		raise(ERR_CODE_UNKNOWN_ERROR, "buffer too short");
	if (!(b[1] & 0x80)) {
		buf->data += 2;
		buf->size -= 2;
		buf->len -= 2;
		return (b[0] & 0x7f) << 7 | (b[1] & 0x7f);
	}
	if (len < 3)
		raise(ERR_CODE_UNKNOWN_ERROR, "buffer too short");
	if (!(b[2] & 0x80)) {
		buf->data += 3;
		buf->size -= 3;
		buf->len -= 3;
		return (b[0] & 0x7f) << 14 | (b[1] & 0x7f) << 7 | (b[2] & 0x7f);
	}

	if (len < 4)
		raise(ERR_CODE_UNKNOWN_ERROR, "buffer too short");
	if (!(b[3] & 0x80)) {
		buf->data += 4;
		buf->size -= 4;
		buf->len -= 4;
		return (b[0] & 0x7f) << 21 | (b[1] & 0x7f) << 14 |
			(b[2] & 0x7f) << 7 | (b[3] & 0x7f);
	}

	if (len < 5)
		raise(ERR_CODE_UNKNOWN_ERROR, "buffer too short");
	if (!(b[4] & 0x80)) {
		buf->data += 5;
		buf->size -= 5;
		buf->len -= 5;
		return (b[0] & 0x7f) << 28 | (b[1] & 0x7f) << 21 |
			(b[2] & 0x7f) << 14 | (b[3] & 0x7f) << 7 | (b[4] & 0x7f);
	}

	raise(ERR_CODE_UNKNOWN_ERROR, "imposible happend");
	return 0;
}

u32
pick_u32(void *data, void **rest)
{
	u32 *b = data;
	if (rest != NULL)
		*rest = b + 1;
	return *b;
}

void *
read_field(struct tbuf *buf)
{
	void *p = buf->data;
	u32 data_len = read_varint32(buf);

	if (data_len > buf->len)
		raise(ERR_CODE_UNKNOWN_ERROR, "buffer too short");

	buf->size -= data_len;
	buf->len -= data_len;
	buf->data += data_len;
	return p;
}

u32
valid_tuple(struct tbuf *buf, u32 cardinality)
{
	void *data = buf->data;
	u32 r, len = buf->len;

	for (int i = 0; i < cardinality; i++)
		read_field(buf);

	r = len - buf->len;
	buf->data = data;
	buf->len = len;
	return r;
}

size_t
varint32_sizeof(u32 value)
{
	if (value < (1 << 7))
		return 1;
	if (value < (1 << 14))
		return 2;
	if (value < (1 << 21))
		return 3;
	if (value < (1 << 28))
		return 4;
	return 5;
}
