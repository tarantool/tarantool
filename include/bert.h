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

#ifndef TARANTOOL_BERT_H
#define TARANTOOL_BERT_H

#include <arpa/inet.h>
#include <string.h>
#include <stdbool.h>

#include <tbuf.h>
#include <util.h>

#define ERL_VERSION       131
#define ERL_SMALL_INT     97
#define ERL_INT           98
#define ERL_SMALL_BIGNUM  110
#define ERL_LARGE_BIGNUM  111
#define ERL_FLOAT         99
#define ERL_ATOM          100
#define ERL_SMALL_TUPLE   104
#define ERL_LARGE_TUPLE   105
#define ERL_NIL           106
#define ERL_STRING        107
#define ERL_LIST          108
#define ERL_BIN           109

extern struct tbuf bert_saved_state, bert_last_packet;

char *bert_sprint(struct tbuf *b);

static inline void bert_save_state(struct tbuf *b)
{
	memcpy(&bert_saved_state, b, sizeof(*b));
}

static inline void bert_restore_state(struct tbuf *b)
{
	memcpy(b, &bert_saved_state, sizeof(*b));
}

static inline void bert_take_bytes(struct tbuf *b, size_t n)
{
	b->len -= n;
	b->size -= n;
	b->data += n;
}

#define bert_check_bytes(b, n) if (b->len < n) goto bert_match_failure;

#define bert_peek(b, type, size)			\
	({						\
		bert_check_bytes(b, size);		\
		type *p = b->data;			\
		bert_take_bytes(b, size);		\
		p;					\
	})

#define bert_match_prim(b, v, fail, type)				\
	({								\
		bool r = 0;						\
		bert_check_bytes(b, sizeof(type));			\
		type *p = b->data;					\
		if (*p != v) {						\
			fail;						\
		} else {						\
			r = 1;						\
			bert_take_bytes(b, sizeof(type));		\
		}							\
		r;							\
	})

#define bert_peek_u8(b) *bert_peek(b, uint8_t, sizeof(uint8_t))
#define bert_peek_n16(b) ntohs(*bert_peek(b, uint16_t, sizeof(uint16_t)))
#define bert_peek_n32(b) ntohl(*bert_peek(b, uint32_t, sizeof(uint32_t)))
#define bert_peek_bytes(b, n) bert_peek(b, uint8_t, n)

#define bert_match_u8(b, v) bert_match_prim(b, v, goto bert_match_failure, uint8_t)
#define bert_match_n16(b, v) bert_match_prim(b, htons(v), goto bert_match_failure, uint16_t)
#define bert_match_n32(b, v) bert_match_prim(b, htonl(v), goto bert_match_failure, uint32_t)

#define bert_cmp_u8(b, v) bert_match_prim(b, v, , uint8_t)
#define bert_cmp_n16(b, v) bert_match_prim(b, htons(v), , uint16_t)
#define bert_cmp_n32(b, v) bert_match_prim(b, htonl(v), , uint32_t)

#define bert_match_header(b)					\
	({							\
		bert_match_u8(b, ERL_VERSION);			\
		memcpy(&bert_last_packet, b, sizeof(*b));	\
	})

#define bert_cmp_atom(b, v)						\
	({								\
		bert_save_state(b);					\
		bert_match_u8(b, ERL_ATOM);				\
		size_t atom_len = bert_peek_n16(b);			\
		uint8_t *atom = bert_peek_bytes(b, atom_len);		\
		bool r = (strlen(v) == atom_len &&			\
			  memcmp(atom, v, atom_len) == 0);		\
		if (!r) bert_restore_state(b);				\
		r;							\
	})

#define bert_match_atom(b, v)						\
	({								\
		bert_match_u8(b, ERL_ATOM);				\
		size_t atom_len = bert_peek_n16(b);			\
		uint8_t *atom = bert_peek_bytes(b, atom_len);		\
		if (strlen(v) != atom_len &&				\
		    memcmp(atom, v, atom_len) != 0)			\
			goto bert_match_failure;			\
	})

#define bert_peek_int(b)					\
	({							\
		int64_t r = 0;					\
		uint8_t tag;					\
		tag = bert_peek_u8(b);				\
		switch (tag) {					\
		case ERL_SMALL_INT: r = bert_peek_u8(b); break;	\
		case ERL_INT: r = bert_peek_n32(b); break;	\
		case ERL_SMALL_BIGNUM: {			\
			uint8_t n = bert_peek_u8(b);		\
			uint8_t sign = bert_peek_u8(b);		\
			uint8_t *bytes = bert_peek_bytes(b, n);	\
			if (n > 8) goto bert_match_failure;	\
			for(int i = 0; i < n; i++)		\
				r += ((int64_t)bytes[i] << (i * 8));	\
			if (sign) r = -r;			\
			break;					\
		}						\
		default: goto bert_match_failure;		\
		}						\
		r;						\
	})

#define bert_peek_string(b)				\
	({						\
		match_u8(b, ERL_STRING);		\
		int len = peek_n16(b);			\
		tbuf_peek(p, len);			\
	})

#define bert_peek_bin(b)					\
	({							\
		bert_match_u8(b, ERL_BIN);				\
		int len = bert_peek_n16(b);				\
		struct tbuf *r = tbuf_alloc(b->pool, NULL, 0);		\
		r->len = r->size = len;					\
		r->data = bert_peek_bytes(b, len);			\
		r;							\
	})

#define bert_peek_tuple(b)						\
	({								\
		uint32_t arity;						\
		uint8_t tag;						\
		tag = bert_peek_u8(b);					\
		switch (tag) {						\
		case ERL_SMALL_TUPLE: arity = bert_peek_u8(b);	break;	\
		case ERL_LARGE_TUPLE: arity = bert_peek_n32(b);	break;	\
		default: goto bert_match_failure;			\
		}							\
		arity;							\
	})

#define bert_match_tuple(b, a)					\
	({							\
		uint32_t arity = bert_peek_tuple(b);		\
		if (arity != a) goto bert_match_failure;	\
	})

static inline void bert_pack_u8(struct tbuf *b, uint8_t v)
{
	size_t o = tbuf_reserve(b, sizeof(uint8_t));
	*(uint8_t *)(b->data + o) = v;
}

static inline void bert_pack_n16(struct tbuf *b, uint16_t v)
{
	size_t o = tbuf_reserve(b, sizeof(uint16_t));
	*(uint16_t *)(b->data + o) = htons(v);
}

static inline void bert_pack_n32(struct tbuf *b, uint32_t v)
{
	size_t o = tbuf_reserve(b, sizeof(uint32_t));
	*(uint32_t *)(b->data + o) = htonl(v);
}

static inline void bert_pack_header(struct tbuf *b)
{
	bert_pack_u8(b, ERL_VERSION);
}

static inline void bert_pack_tuple(struct tbuf *b, uint8_t arity)
{
	bert_pack_u8(b, ERL_SMALL_TUPLE);
	bert_pack_u8(b, arity);
}

#define bert_pack_atom(b, str) bert_pack_atom_((b), (str), strlen((str)))
static inline void bert_pack_atom_(struct tbuf *b, const char *atom, size_t atom_len)
{
	bert_pack_u8(b, ERL_ATOM);
	bert_pack_n16(b, atom_len);
	size_t offt = tbuf_reserve(b, atom_len);
	memcpy(b->data + offt, atom, atom_len);
}

static inline void bert_pack_int(struct tbuf *b, int64_t v)
{
	if (0 <= v && v <= 255) {
		bert_pack_u8(b, ERL_SMALL_INT);
		bert_pack_u8(b, (uint8_t)v);
		return;
	}

	if (-(1 << 27) <= v && v <= (1 << 27) - 1) {
		bert_pack_u8(b, ERL_INT);
		bert_pack_n32(b, v);
		return;
	}

	bert_pack_u8(b, ERL_SMALL_BIGNUM);
	size_t n = tbuf_reserve(b, sizeof(uint8_t));
	*(uint8_t *)(b->data + n) = 0;
	bert_pack_u8(b, v < 0);
	if (v < 0)
		v = -v;
	while (v) {
		size_t o = tbuf_reserve(b, sizeof(uint8_t));
		*(uint8_t *)(b->data + o) = v;
		v >>= 8;
		(*(uint8_t *)(b->data + n))++;
	}
}

static inline void bert_pack_bin(struct tbuf *b, const struct tbuf *v)
{
	bert_pack_u8(b, ERL_BIN);
	bert_pack_n16(b, v->len);
	size_t o = tbuf_reserve(b, v->len);
	memcpy(b->data + o, v->data, v->len);
}

#define bert_panic(msg) panic(msg "can't parse bert packet: %s", bert_sprint(&bert_last_packet))
#endif
