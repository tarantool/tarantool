#ifndef TP_H_INCLUDED
#define TP_H_INCLUDED

/*
 * TP - Tarantool Protocol library.
 * (http://tarantool.org)
 *
 * protocol description:
 * https://github.com/mailru/tarantool/blob/master/doc/box-protocol.txt
 * -------------------
 *
 * TP - is a C library designed to create requests and process
 * replies to or from a Tarantool server.
 *
 * The library is highly optimized and designed to be used
 * by a C/C++ application which require sophisticated memory
 * control and performance.
 *
 * Library does not support network operations. All operations
 * are done in-memory using specified allocator.
 *
 * Library mostly designed to be easly use by protocol drivers
 * written for a interpret languages that need to avoid double-buffering
 * and write directly to language objects memory (such as strings,
 * scalars, etc).
 *
 * REQUEST COMPILATION
 * -------------------
 *
 * (1) initialize struct tp object by tp_init call, using specified
 * buffer and allocator function.
 *
 * (2) sequentially call necessary operations, like tp_insert,
 * tp_delete, tp_update, tp_call. Every request operations is always
 * a append to a buffer. That is, tp_insert will put a insert header
 * only, to complete request a tuple must be also placed, containing
 * a key and a value:
 *
 * char buf[256];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_insert(&req, 0, 0);
 * tp_tuple(&req);
 * tp_sz(&req, "key");
 * tp_sz(&req, "value");
 *
 * write(1, buf, tp_used(&req)); // write buffer to the stdout
 *
 * (3) buffer can be used right away when all requests are
 * pushed to it. tp_used can be used to get current buffer size written.
 *
 * (4) After finish, buffer must be freed manually.
 *
 * See operations for a example.
 *
 * REPLY PROCESSING
 * ----------------
 *
 * (1) tp_init must be initialized with a fully read reply.
 * Functions tp_reqbuf, tp_req can be used to examine if buffer
 * contains it.
 *
 * Following example of tp_req usage (reading from stdin while
 * reply is completly read):
 *
 * struct tp rep;
 * tp_init(&rep, NULL, 0, tp_reallocator_noloss, NULL);
 *
 * while (1) {
 *  ssize_t to_read = tp_req(&rep);
 *  printf("to_read: %zu\n", to_read);
 *  if (to_read <= 0)
 *    break;
 *  ssize_t new_size = tp_ensure(&rep, to_read);
 *  printf("new_size: %zu\n", new_size);
 *  if (new_size == -1)
 *    return -1;
 *  int rc = fread(rep.p, to_read, 1, stdin);
 *  if (rc != 1)
 *    return 1;
 *  tp_use(&rep, to_read);
 * }
 * tp_reply(&rep)
 *
 * (2) tp_reply function is used:
 * server_code = tp_reply(&reply) function is used.
 *
 * if (server_code != 0) {
 *   printf("error: %-.*s\n", tp_replyerrorlen(&rep),
 *          tp_replyerror(&rep));
 * }
 *
 * (3) replied data can be accessed by tp_next, tp_nextfield, tp_gettuple,
 * tp_getfield functions.
 *
 * See tp_reply and tp_next/tp_nextfield for a example.
 *
 * RETURN POLICY
 * -------------
 *
 * all request functions: total size written to a buffer or -1 on error.
 * common functions: 0 on success, -1 on error.
 *
 * EXAMPLE
 * -------
 *
 * TP is used by Tarantool Perl driver:
 * https://github.com/dr-co/dr-tarantool/blob/master/Tarantool.xs
*/

/*
 * Copyright (c) 2012-2013 Tarantool/Box AUTHORS
 * (https://github.com/mailru/tarantool/blob/master/AUTHORS)
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
#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define tp_function_unused __attribute__((unused))
#define tp_packed __attribute__((packed))
#define tp_inline __attribute__((forceinline))
#define tp_noinline __attribute__((noinline))
#if defined(__GNUC__)
#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)
#define tp_hot __attribute__((hot))
#endif
#endif
#if !defined(tp_hot)
#define tp_hot
#endif

#define tp_likely(expr)   __builtin_expect(!! (expr), 1)
#define tp_unlikely(expr) __builtin_expect(!! (expr), 0)

struct tp;

/* resizer function, can be customized for own use */
typedef char *(*tp_resizer)(struct tp *p, size_t req, size_t *size);

#define TP_PING   65280
#define TP_INSERT 13
#define TP_SELECT 17
#define TP_UPDATE 19
#define TP_DELETE 21
#define TP_CALL   22

/* requests flags */
#define TP_FRET   1
#define TP_FADD   2
#define TP_FREP   4
#define TP_FQUIET 8

/* update operations */
#define TP_OPSET    0
#define TP_OPADD    1
#define TP_OPAND    2
#define TP_OPXOR    3
#define TP_OPOR     4
#define TP_OPSPLICE 5
#define TP_OPDELETE 6
#define TP_OPINSERT 7

/* internal protocol headers */
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

/*
 * main tp object.
 *
 * object contains private fields, that should
 * not be accessed directly. Appropriate accessor
 * functions should be used.
*/
struct tp {
	struct tp_h *h;  /* current headers */
	char *s, *p, *e; /* start, pos, end */
	char *t, *f, *u; /* tuple, field, update */
	char *c;
	uint32_t tsz, fsz, tc;
	uint32_t code;
	uint32_t cnt;
	tp_resizer resizer;
	void *obj;
};

/* get allocated buffer size */
static inline size_t
tp_size(struct tp *p) {
	return p->e - p->s;
}

/* get actual buffer size, written with data */
static inline size_t
tp_used(struct tp *p) {
	return p->p - p->s;
}

/* get size available for write */
static inline size_t
tp_unused(struct tp *p) {
	return p->e - p->p;
}

/* common reallocation function.
 * resizes buffer twice a size larger than a previous one.
 *
 * struct tp req;
 * tp_init(&req, NULL, tp_reallocator, NULL);
 * tp_ping(&req); // will call reallocator
 *
 * data must be manually freed on finish.
 * (eg. free(p->s));
*/
tp_function_unused static char*
tp_reallocator(struct tp *p, size_t req, size_t *size) {
	size_t toalloc = tp_size(p) * 2;
	if (tp_unlikely(toalloc < req))
		toalloc = req;
	*size = toalloc;
	return realloc(p->s, toalloc);
}

/* common reallocation function.
 * resizes buffer exact size as required.
 *
 * struct tp req;
 * tp_init(&req, NULL, tp_reallocator_noloss, NULL);
 * tp_ping(&req); // will call reallocator
 *
 * data must be manually freed on finish.
*/
tp_function_unused static char*
tp_reallocator_noloss(struct tp *p, size_t req, size_t *size) {
	*size = tp_size(p) + (req - tp_unused(p));
	return realloc(p->s, *size);
}

/* main initialization function.
 *
 * resizer - reallocation function, may be NULL
 * obj     - pointer to be passed to resizer function
 * buf     - current buffer, may be NULL
 * size    - buffer size
*/
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
	p->tsz = 0;
	p->fsz = 0;
	p->cnt = 0;
	p->code = 0;
	p->resizer = resizer;
	p->obj = obj;
}

/* ensure that buffer has enough space to fill size bytes, resize
 * buffer if needed. */
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
	if (tp_likely(p->h))
		p->h = (struct tp_h*)(np + (((char*)p->h) - p->s));
	if (tp_likely(p->t))
		p->t = np + (p->t - p->s);
	if (tp_unlikely(p->f))
		p->f = (np + (p->f - p->s));
	if (tp_unlikely(p->u))
		p->u = (np + (p->u - p->s));
	p->s = np;
	p->e = np + sz; 
	return sz;
}

/* mark size bytes as used.
 * can be used while appending data to complete reply. */
static inline ssize_t
tp_use(struct tp *p, size_t size) {
	p->p += size;
	return tp_used(p);
}

static inline ssize_t
tp_append(struct tp *p, const void *data, size_t size) {
	if (tp_unlikely(tp_ensure(p, size) == -1))
		return -1;
	memcpy(p->p, data, size);
	return tp_use(p, size);
}

/* set current request id.
 *
 * tp_ping(&req);
 * tp_reqid(&req, 777);
 */
static inline void
tp_reqid(struct tp *p, uint32_t reqid) {
	assert(p->h != NULL);
	p->h->reqid = reqid;
}

/* get current request id */
static inline uint32_t
tp_getreqid(struct tp *p) {
	assert(p->h != NULL);
	return p->h->reqid;
}

/* get tuple count */
static inline uint32_t
tp_tuplecount(struct tp *p) {
	assert(p->t != NULL);
	return *(uint32_t*)(p->t);
}

/* write tuple header */
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

static tp_noinline void tp_hot
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

static inline void tp_hot
tp_leb128save(struct tp *p, uint32_t value) {
	if (tp_unlikely(value >= (1 << 14))) {
		tp_leb128save_slowpath(p, value);
		return;
	}
	if (tp_likely(value >= (1 << 7)))
		*(p->p++) = ((value >> 7) | 0x80);
	*(p->p++) = ((value) & 0x7F);
}

static tp_noinline int tp_hot
tp_leb128load_slowpath(struct tp *p, uint32_t *value) {
	if (tp_likely(! (p->f[2] & 0x80))) {
		*value = (p->f[0] & 0x7f) << 14 |
		         (p->f[1] & 0x7f) << 7  |
		         (p->f[2] & 0x7f);
		p->f += 3;
	} else
	if (! (p->f[3] & 0x80)) {
		*value = (p->f[0] & 0x7f) << 21 |
		         (p->f[1] & 0x7f) << 14 |
		         (p->f[2] & 0x7f) << 7  |
		         (p->f[3] & 0x7f);
		p->f += 4;
	} else
	if (! (p->f[4] & 0x80)) {
		*value = (p->f[0] & 0x7f) << 28 |
		         (p->f[1] & 0x7f) << 21 |
		         (p->f[2] & 0x7f) << 14 |
		         (p->f[3] & 0x7f) << 7  |
		         (p->f[4] & 0x7f);
		p->f += 5;
	} else
		return -1;
	return 0;
}

static inline int tp_hot
tp_leb128load(struct tp *p, uint32_t *value) {
	if (tp_likely(! (p->f[0] & 0x80))) {
		*value = *(p->f++) & 0x7f;
	} else
	if (tp_likely(! (p->f[1] & 0x80))) {
		*value = (p->f[0] & 0x7f) << 7 | (p->f[1] & 0x7f);
		p->f += 2;
	} else
		return tp_leb128load_slowpath(p, value);
	return 0;
}

/* write tuple field, usable after tp_tuple call. */
static inline ssize_t
tp_field(struct tp *p, const char *data, size_t size) {
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

/* write ping request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_ping(&req);
 */
static inline ssize_t
tp_ping(struct tp *p) {
	struct tp_h h = { TP_PING, 0, 0 };
	return tp_appendreq(p, &h, sizeof(h));
}

/* write insert request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_insert(&req, 0, TP_FRET);
 * tp_tuple(&req);
 * tp_sz(&req, "key");
 * tp_sz(&req, "value");
 */
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

/* write delete request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_delete(&req, 0, 0);
 * tp_tuple(&req);
 * tp_sz(&req, "key");
 */
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

/* write call request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 *
 * char proc[] = "hello_proc";
 * tp_call(&req, 0, proc, sizeof(proc) - 1);
 * tp_tuple(&req);
 * tp_sz(&req, "arg1");
 * tp_sz(&req, "arg2");
 */
static inline ssize_t
tp_call(struct tp *p, uint32_t flags, const char *name, size_t size) {
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

/* write select request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_select(&req, 0, 0, 0, 0);
 * tp_tuple(&req);
 * tp_sz(&req, "key");
 */
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

/* write update request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_update(&req, 0, 0);
 * tp_tuple(&req);
 * tp_sz(&req, "key");
 * tp_updatebegin(&req);
 * tp_op(&req, 1, TP_OPSET, "VALUE", 5);
 */
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

/* write operation counter,
 * must be called after update key tuple. */
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

/* write update operation.
 *
 * may be called after tp_updatebegin.
 *
 * can be used to request TP_OPSET, TP_OPADD, TP_OPAND,
 * TP_OPXOR, TP_OPOR operations.
 *
 * see tp_update for example.
 */
static inline ssize_t
tp_op(struct tp *p, uint32_t field, uint8_t op, const char *data,
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

/* splice update operation. */
static inline ssize_t
tp_opsplice(struct tp *p, uint32_t field, uint32_t off, uint32_t len,
            const char *data, size_t size) {
	uint32_t olen = tp_leb128sizeof(sizeof(off)),
	         llen = tp_leb128sizeof(sizeof(len)),
	         dlen = tp_leb128sizeof(size);
	uint32_t sz = olen + sizeof(off) + llen + sizeof(len) +
	              dlen + size;
	ssize_t rc = tp_op(p, field, TP_OPSPLICE, NULL, sz);
	if (tp_unlikely(rc == -1))
		return -1;
	p->p -= sz;
	tp_leb128save(p, sizeof(off));
	memcpy(p->p, &off, sizeof(off));
	p->p += sizeof(off);
	tp_leb128save(p, sizeof(len));
	memcpy(p->p, &len, sizeof(len));
	p->p += sizeof(len);
	tp_leb128save(p, size);
	memcpy(p->p, data, size);
	p->p += size;
	return rc;
}

/* write tuple field as string. */
static inline ssize_t
tp_sz(struct tp *p, const char *sz) {
	return tp_field(p, sz, strlen(sz));
}

/* get how many bytes is still required to process reply.
 * return value can be negative. */
static inline ssize_t
tp_reqbuf(const char *buf, size_t size) {
	if (tp_unlikely(size < sizeof(struct tp_h)))
		return sizeof(struct tp_h) - size;
	register int sz =
		((struct tp_h*)buf)->len + sizeof(struct tp_h);
	return (tp_likely(size < sz)) ?
	                  sz - size : -(size - sz);
}

/* same as tp_reqbuf, but tp initialized with buffer */
static inline ssize_t
tp_req(struct tp *p) {
	return tp_reqbuf(p->s, tp_size(p));
}

static inline size_t
tp_unfetched(struct tp *p) {
	return p->p - p->c;
}

static inline void*
tp_fetch(struct tp *p, int inc) {
	assert(tp_unfetched(p) >= inc);
	register char *po = p->c;
	p->c += inc;
	return po;
}

/* get current reply error */
static inline char*
tp_replyerror(struct tp *p) {
	return p->c;
}

/* get current reply error length */
static inline int
tp_replyerrorlen(struct tp *p) {
	return tp_unfetched(p);
}

/* get current reply tuple count */
static inline uint32_t
tp_replycount(struct tp *p) {
	return p->cnt;
}

/* get current reply returned coded */
static inline uint32_t
tp_replycode(struct tp *p) {
	return p->code;
}

/* get current reply operation */
static inline uint32_t
tp_replyop(struct tp *p) {
	return p->h->type;
}

/*
 * process reply.
 *
 * struct tp rep;
 * tp_init(&rep, reply_buf, reply_size, NULL, NULL);
 *
 * ssize_t server_code = tp_reply(&rep);
 *
 * printf("op:    %d\n", tp_replyop(&rep));
 * printf("count: %d\n", tp_replycount(&rep));
 * printf("code:  %zu\n", server_code);
 *
 * if (server_code != 0) {
 *   printf("error: %-.*s\n", tp_replyerrorlen(&rep),
 *          tp_replyerror(&rep));
 * }
 */
tp_function_unused static ssize_t
tp_reply(struct tp *p) {
	ssize_t used = tp_req(p);
	if (tp_unlikely(used > 0))
		return -1;
	/* this is end of packet in continious buffer */
	p->p = p->e + used; /* end - used */
	p->c = p->s;
	p->h = tp_fetch(p, sizeof(struct tp_h));
	p->t = p->f = p->u = NULL;
	p->cnt = 0;
	p->code = 0;
	if (tp_unlikely(p->h->type == TP_PING))
		return 0; 
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

/* example: iteration on returned tuples.
 *
 * while (tp_next(&rep)) {
 *   printf("tuple fields: %d\n", tp_tuplecount(&rep));
 *   printf("tuple size: %d\n", tp_tuplesize(&rep));
 *   printf("[");
 *   while (tp_nextfield(&rep)) {
 *     printf("%-.*s", tp_getfieldsize(rep), tp_getfield(&rep));
 *     if (tp_hasnextfield(&rep))
 *       printf(", ");
 *   }
 *   printf("]\n");
 * }
*/

/* rewind iteration to a first tuple */
static inline void
tp_rewind(struct tp *p) {
	p->t = NULL;
	p->f = NULL;
}

/* rewind iteration to a first field */
static inline void
tp_rewindfield(struct tp *p) {
	p->f = NULL;
}

/* get current tuple data */
static inline char*
tp_gettuple(struct tp *p) {
	return p->t;
}

/* get current tuple size */
static inline uint32_t
tp_tuplesize(struct tp *p) {
	return p->tsz;
}

/* get current field data */
static inline char*
tp_getfield(struct tp *p) {
	return p->f;
}

/* get current field size */
static inline uint32_t
tp_getfieldsize(struct tp *p) {
	return p->fsz;
}

static inline char*
tp_tupleend(struct tp *p) {
	/* tuple_size + p->t + cardinaltiy_size +
	 * fields_size */
	return p->t + 4 + p->tsz;
}

static inline int
tp_hasdata(struct tp *p) {
	return tp_replyop(p) != TP_PING && tp_unfetched(p) > 0;
}

/* check if there is more tuple */
static inline int
tp_hasnext(struct tp *p) {
	assert(p->t != NULL);
	return (p->p - tp_tupleend(p)) >= 4;
}

/* check if tuple has next field */
static inline int
tp_hasnextfield(struct tp *p) {
	assert(p->t != NULL);
	register char *f = p->f + p->fsz;
	if (tp_unlikely(p->f == NULL))
		f = p->t + 4;
	return (tp_tupleend(p) - f) >= 1;
}

/* skip to next tuple.
 * tuple can be accessed using:
 * tp_tuplecount, tp_tuplesize, tp_gettuple. */
static inline int
tp_next(struct tp *p) {
	if (tp_unlikely(p->t == NULL)) {
		if (tp_unlikely(! tp_hasdata(p)))
			return 0;
		p->t = p->c + 4;
		goto fetch;
	}
	if (tp_unlikely(! tp_hasnext(p)))
		return 0;
	p->t = tp_tupleend(p) + 4;
fetch:
	p->tsz = *(uint32_t*)(p->t - 4);
	p->f = NULL;
	return 1;
}

/* skip to next field.
 * data can be accessed using: tp_getfieldsize, tp_getfield. */
static inline int
tp_nextfield(struct tp *p) {
	assert(p->t != NULL);
	if (tp_unlikely(p->f == NULL)) {
		if (tp_unlikely(! tp_hasnextfield(p)))
			return 0;
		p->f = p->t + 4;
		goto fetch;
	}
	if (tp_unlikely(! tp_hasnextfield(p)))
		return 0;
	p->f += p->fsz;
fetch:;
	register int rc = tp_leb128load(p, &p->fsz);
	if (tp_unlikely(rc == -1))
		return -1;
	return 1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TP_H_INCLUDED */
