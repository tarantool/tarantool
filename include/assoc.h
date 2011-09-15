/*
 * Copyright (C) 2011 Mail.RU
 * Copyright (C) 2011 Yuriy Vostrikov
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

#include <pickle.h>
#include <string.h>
#include <stdlib.h>

#if !MH_SOURCE
#define MH_UNDEF
#endif

typedef void* ptr_t;

#define mh_name _i32ptr
#define mh_key_t i32
#define mh_val_t ptr_t
#define mh_hash(a) ({ (a); })
#define mh_eq(a, b) ({ (a) == (b); })
#include <mhash.h>

#define mh_name _i64ptr
#define mh_key_t i64
#define mh_val_t ptr_t
#define mh_hash(a) ({ (mh_int_t)((a)>>33^(a)^(a)<<11); })
#define mh_eq(a, b) ({ (a) == (b); })
#include <mhash.h>

static inline int lstrcmp(void *a, void *b)
{
	unsigned int al, bl;

	al = load_varint32(&a);
	bl = load_varint32(&b);

	if (al != bl)
		return bl - al;
	return memcmp(a, b, al);
}
#include <third_party/murmur_hash2.c>
#define mh_name _lstrptr
#define mh_key_t ptr_t
#define mh_val_t ptr_t
#define mh_hash(key) ({ void *_k = (key); unsigned l = load_varint32(&_k); MurmurHash2(_k, l, 13); })
#define mh_eq(a, b) ({ lstrcmp((a), (b)) == 0; })
#include <mhash.h>
