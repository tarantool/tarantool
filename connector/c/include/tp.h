#ifndef TP_H_INCLUDED
#define TP_H_INCLUDED

/*
 * TP - Tarantool Protocol request constructor
 * (http://tarantool.org)
 *
 * Copyright (c) 2012-2013 Mail.Ru Group 
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define tp_function_unused __attribute__((unused))
#define tp_packed __attribute__((packed))
#define tp_noinline __attribute__((noinline))

#define tp_likely(expr)   __builtin_expect(!! (expr), 1)
#define tp_unlikely(expr) __builtin_expect(!! (expr), 0)

struct tp;

typedef char *(*tp_resizer)(struct tp *p, size_t req, size_t *size);

#define TP_PING   65280
#define TP_INSERT 13
#define TP_SELECT 17
#define TP_UPDATE 19
#define TP_DELETE 21
#define TP_CALL   22

#define TP_FRET   1
#define TP_FADD   2
#define TP_FREP   4
#define TP_FQUIET 8

#define TP_OPSET    0
#define TP_OPADD    1
#define TP_OPAND    2
#define TP_OPXOR    3
#define TP_OPOR     4
#define TP_OPSPLICE 5
#define TP_OPDELETE 6
#define TP_OPINSERT 7

struct tp_h {
	uint32_t type, len, reqid;
} tp_packed;

struct tp_hinsert {
	uint32_t s, flags;
} tp_packed;

struct tp_hdelete {
	uint32_t s, flags;
} tp_packed;

struct tp_hupdate {
	uint32_t s, flags;
} tp_packed;

struct tp_hcall {
	uint32_t flags;
} tp_packed;

struct tp_hselect {
	uint32_t s, i, o, l;
	uint32_t keyc;
} tp_packed;

struct tp {
	struct tp_h *h;
	char *s, *p, *e;
	char *t, *f, *u;
	char *c;
	uint32_t code;
	uint32_t cnt;
	tp_resizer resizer;
	void *obj;
};

static inline size_t
tp_size(struct tp *p) {
	return p->e - p->s;
}

static inline size_t
tp_used(struct tp *p) {
	return p->p - p->s;
}

static inline size_t
tp_unused(struct tp *p) {
	return p->e - p->p;
}

tp_function_unused static char*
tp_reallocator(struct tp *p, size_t req, size_t *size) {
	size_t nsz = tp_size(p) * 2;
	if (tp_unlikely(nsz < req))
		nsz = req;
	*size = nsz;
	return realloc(p->s, nsz);
}

static inline void
tp_init(struct tp *p, char *buf, size_t size,
        tp_resizer resizer, void *obj) {
	p->s = buf;
	p->p = p->s;
	p->e = p->s + size;
	p->t = NULL;
	p->f = NULL;
	p->u = NULL;
	p->c = NULL;
	p->h = NULL;
	p->cnt = 0;
	p->code = 0;
	p->resizer = resizer;
	p->obj = obj;
}

static tp_noinline ssize_t
tp_ensure(struct tp *p, size_t size) {
	if (tp_likely(tp_unused(p) >= size))
		return 0;
	if (tp_unlikely(p->resizer == NULL))
		return -1;
	size_t sz;
	register char *np = p->resizer(p, size, &sz);
	if (tp_unlikely(np == NULL))
		return -1;
	p->p = np + (p->p - p->s);
	p->t = np + (p->t - p->s);
	if (tp_likely(p->h))
		p->h = (struct tp_h*)(np + (((char*)p->h) - p->s));
	if (tp_unlikely(p->f))
		p->f = (np + (p->f - p->s));
	if (tp_unlikely(p->u))
		p->u = (np + (p->u - p->s));
	p->s = np;
	p->e = np + sz; 
	return sz;
}

static inline ssize_t
tp_append(struct tp *p, void *data, size_t size) {
	if (tp_unlikely(tp_ensure(p, size) == -1))
		return -1;
	memcpy(p->p, data, size);
	p->p += size;
	return tp_used(p);
}

static inline void
tp_reqid(struct tp *p, uint32_t reqid) {
	assert(p->h != NULL);
	p->h->reqid = reqid;
}

static inline uint32_t
tp_tuplecount(struct tp *p) {
	assert(p->t != NULL);
	return *(uint32_t*)(p->t);
}

static inline ssize_t
tp_tuple(struct tp *p) {
	assert(p->h != NULL);
	if (tp_unlikely(tp_ensure(p, sizeof(uint32_t)) == -1))
		return -1;
	*(uint32_t*)(p->t = p->p) = 0;
	p->p += sizeof(uint32_t);
	p->h->len += sizeof(uint32_t);
	if (p->h->type == TP_SELECT) {
		((struct tp_hselect*)
		((char*)p->h + sizeof(struct tp_h)))->keyc++;
	}
	return tp_used(p);
}

static inline size_t
tp_leb128sizeof(uint32_t value) {
	return (  tp_likely(value < (1 <<  7))) ? 1 :
	       (  tp_likely(value < (1 << 14))) ? 2 :
	       (tp_unlikely(value < (1 << 21))) ? 3 :
	       (tp_unlikely(value < (1 << 28))) ? 4 : 5;
}

static tp_noinline void
tp_leb128save_slowpath(struct tp *p, uint32_t value) {
	if (tp_unlikely(value >= (1 << 21))) {
		if (tp_unlikely(value >= (1 << 28)))
			*(p->p++) = (value >> 28) | 0x80;
		*(p->p++) = (value >> 21) | 0x80;
	}
	p->p[0] = ((value >> 14) | 0x80);
	p->p[1] = ((value >>  7) | 0x80);
	p->p[2] =   value & 0x7F;
	p->p += 3;
}

static inline void
tp_leb128save(struct tp *p, uint32_t value) {
	if (tp_unlikely(value >= (1 << 14))) {
		tp_leb128save_slowpath(p, value);
		return;
	}
	if (tp_likely(value >= (1 << 7)))
		*(p->p++) = ((value >> 7) | 0x80);
	*(p->p++) = ((value) & 0x7F);
}

static inline ssize_t
tp_field(struct tp *p, char *data, size_t size) {
	assert(p->h != NULL);
	assert(p->t != NULL);
	register int esz = tp_leb128sizeof(size);
	if (tp_unlikely(tp_ensure(p, esz + size) == -1))
		return -1;
	tp_leb128save(p, size);
	memcpy(p->p, data, size);
	p->p += size;
	(*(uint32_t*)p->t)++;
	p->h->len += esz + size;
	return tp_used(p);
}

static inline void
tp_setreq(struct tp *p) {
	p->h = (struct tp_h*)p->p;
	p->t = NULL;
	p->u = NULL;
}

static inline ssize_t
tp_appendreq(struct tp *p, void *h, size_t size) {
	tp_setreq(p);
	return tp_append(p, h, size);
}

static inline ssize_t
tp_ping(struct tp *p) {
	struct tp_h h = { TP_PING, 0, 0 };
	return tp_appendreq(p, &h, sizeof(h));
}

static inline ssize_t
tp_insert(struct tp *p, uint32_t space, uint32_t flags) {
	struct {
		struct tp_h h;
		struct tp_hinsert i;
	} h;
	h.h.type = TP_INSERT;
	h.h.len = sizeof(struct tp_hinsert);
	h.h.reqid = 0;
	h.i.s = space;
	h.i.flags = flags;
	return tp_appendreq(p, &h, sizeof(h));
}

static inline ssize_t
tp_delete(struct tp *p, uint32_t space, uint32_t flags) {
	struct {
		struct tp_h h;
		struct tp_hdelete d;
	} h;
	h.h.type = TP_DELETE;
	h.h.len = sizeof(struct tp_hdelete);
	h.h.reqid = 0;
	h.d.s = space;
	h.d.flags = flags;
	return tp_appendreq(p, &h, sizeof(h));
}

static inline ssize_t
tp_call(struct tp *p, uint32_t flags, char *name, size_t size) {
	struct {
		struct tp_h h;
		struct tp_hcall c;
	} h;
	size_t sz = tp_leb128sizeof(size);
	h.h.type = TP_CALL;
	h.h.len = sizeof(struct tp_hcall) + sz + size;
	h.h.reqid = 0;
	h.c.flags = flags;
	if (tp_unlikely(tp_ensure(p, sizeof(h) + sz + size) == -1))
		return -1;
	tp_setreq(p);
	memcpy(p->p, &h, sizeof(h));
	p->p += sizeof(h);
	tp_leb128save(p, size);
	memcpy(p->p, name, size);
	p->p += size;
	return tp_used(p);
}

static inline ssize_t
tp_select(struct tp *p, uint32_t space, uint32_t index,
          uint32_t offset, uint32_t limit) {
	struct {
		struct tp_h h;
		struct tp_hselect s;
	} h;
	h.h.type = TP_SELECT;
	h.h.len = sizeof(struct tp_hselect);
	h.h.reqid = 0;
	h.s.s = space;
	h.s.i = index;
	h.s.o = offset;
	h.s.l = limit;
	h.s.keyc = 0;
	return tp_appendreq(p, &h, sizeof(h));
}

static inline ssize_t
tp_update(struct tp *p, uint32_t space, uint32_t flags) {
	struct {
		struct tp_h h;
		struct tp_hupdate u;
	} h;
	h.h.type = TP_UPDATE;
	h.h.len = sizeof(struct tp_hupdate);
	h.h.reqid = 0;
	h.u.s = space;
	h.u.flags = flags;
	return tp_appendreq(p, &h, sizeof(h));
}

static inline ssize_t
tp_updatebegin(struct tp *p) {
	assert(p->h != NULL);
	assert(p->h->type == TP_UPDATE);
	if (tp_unlikely(tp_ensure(p, sizeof(uint32_t)) == -1))
		return -1;
	*(uint32_t*)(p->u = p->p) = 0;
	p->p += sizeof(uint32_t);
	p->h->len += sizeof(uint32_t);
	return tp_used(p);
}

static inline ssize_t
tp_op(struct tp *p, uint32_t field, uint8_t op, char *data,
      size_t size) {
	assert(p->h != NULL);
	assert(p->u != NULL);
	assert(p->h->type == TP_UPDATE);
	size_t sz = 4 + 1 + tp_leb128sizeof(size) + size;
	if (tp_unlikely(tp_ensure(p, sz)) == -1)
		return -1;
	/* field */
	*(uint32_t*)(p->p) = field;
	p->p += sizeof(uint32_t);
	/* operation */
	*(uint8_t*)(p->p) = op;
	p->p += sizeof(uint8_t);
	/* data */
	tp_leb128save(p, size);
	if (tp_likely(data))
		memcpy(p->p, data, size);
	p->p += size;
	/* update offset and count */
	p->h->len += sz;
	(*(uint32_t*)p->u)++;
	return tp_used(p);
}

static inline ssize_t
tp_opsplice(struct tp *p, uint32_t field, uint32_t off,
            uint32_t len, char *data, size_t size) {
	uint32_t olen = tp_leb128sizeof(sizeof(off)),
	         llen = tp_leb128sizeof(sizeof(len)),
	         dlen = tp_leb128sizeof(size);
	uint32_t sz = olen + sizeof(off) + llen + sizeof(len) +
	              dlen + size;
	ssize_t rc = tp_op(p, field, TP_OPSPLICE, NULL, sz);
	if (tp_unlikely(rc == -1))
		return -1;
	tp_leb128save(p, sizeof(off));
	memcpy(p->p, &off, sizeof(off));
	p->p += sizeof(off);
	tp_leb128save(p, sizeof(len));
	memcpy(p->p, &len, sizeof(len));
	p->p += sizeof(len);
	tp_leb128save(p, size);
	memcpy(p->p, data, size);
	p += size;
	return rc;
}

static inline ssize_t
tp_sz(struct tp *p, char *sz) {
	return tp_field(p, sz, strlen(sz));
}

static ssize_t
tp_required(struct tp *p) {
	size_t used = tp_used(p);
	if (tp_unlikely(used < sizeof(struct tp_h)))
		return sizeof(struct tp_h) - used;
	struct tp_h *h = (struct tp_h*)p->s;
	return (tp_likely(used < h->len)) ?
	                  h->len - used : used - h->len;
}

static inline size_t
tp_unfetched(struct tp *p) {
	return p->e - p->c;
}

static inline void*
tp_fetch(struct tp *p, int inc) {
	assert(tp_unfetched(p) >= inc);
	register char *po = p->c;
	p->c += inc;
	return po;
}

static inline char*
tp_replyerror(struct tp *p) {
	return p->c;
}

static inline uint32_t
tp_replycount(struct tp *p) {
	return p->cnt;
}

static inline uint32_t
tp_replycode(struct tp *p) {
	return p->code;
}

static inline uint32_t
tp_replyop(struct tp *p) {
	return p->h->type;
}

tp_function_unused static ssize_t
tp_reply(struct tp *p) {
	if (tp_unlikely(tp_required(p) > 0))
		return -1;
	p->c = p->s;
	p->h = tp_fetch(p, sizeof(struct tp_h));
	p->t = NULL;
	p->f = NULL;
	p->u = NULL;
	if (tp_unlikely(p->h->type == TP_PING))
		return TP_PING;
	if (tp_unlikely(p->h->type != TP_UPDATE &&
	                p->h->type != TP_INSERT &&
	                p->h->type != TP_DELETE &&
	                p->h->type != TP_SELECT &&
	                p->h->type != TP_CALL))
		return -1;
	p->code = *(uint32_t*)tp_fetch(p, sizeof(uint32_t));
	if (p->code != 0)
		return p->code;
	/* BOX_QUIET */
	if (tp_unlikely(tp_unfetched(p) == 0))
		return p->code;
	p->cnt = *(uint32_t*)tp_fetch(p, sizeof(uint32_t));
	return p->code;
}

static inline void
tp_rewind(struct tp *p) {
	p->t = p->c;
}

static inline void
tp_rewindfield(struct tp *p) {
	p->f = p->t;
}

static inline ssize_t
tp_next(struct tp *p) {
	(void)p;
	return 0;
}

static inline ssize_t
tp_nextfield(struct tp *p) {
	(void)p;
	return 0;
}

#endif /* TP_H_INCLUDED */
