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
 * The library is designed to be used by a C/C++ application which
 * requires sophisticated memory control and/or performance.
 *
 * The library does not support network operations. All operations
 * are done in a user supplied buffer and with help of
 * a user-supplied allocator.
 *
 * The primary design goal of the library was to spare implementors
 * of Tarantool/Box drivers in other languages, such as Perl,
 * Ruby, Python, etc, from the details of the binary protocol, and
 * also to make it possible to avoid double-buffering by writing
 * directly to/from language objects from/to the serialized binary
 * packet stream. This allows efficient data transfer from domain
 * language types (such as strings, scalars, numbers, etc) to
 * the network format, or directly to the socket.
 *
 * As a side effect of this design goal, the library has become
 * usable in any kind of networking environment: synchronous with
 * buffered sockets, or asynchronous event-based, as well as with
 * cooperative multitasking.
 *
 * Before using the library, it's highly recommended to get
 * acquainted with Tarnatool/Box binary protocol, documented
 * at https://github.com/mailru/tarantool/blob/master/doc/box-protocol.txt
 *
 * In a nutshell, any request in Tarnatool/Box consists of
 * a 12-byte header, containing request type, id and length,
 * and an optional tuple or tuples. Similarly, a response
 * carries back the same request type and id, and contains a
 * tuple or tuples.
 *
 * Below follows a step-by-step tutorial for the library.
 *
 * ASSEMBLING A REQUEST
 * --------------------
 *
 * (1) initialize an instance of struct tp with tp_init().
 * Provide tp_init() with a buffer object and an (optional)
 * allocator function.
 *
 * (2) construct requests by sequentially calling necessary
 * operations, such as tp_insert(), tp_delete(), tp_update(),
 * tp_call(). Note: these operations only construct request
 * header, the body of the request, which is usually a tuple,
 * must be appended to the buffer with a separate call.
 * Each next call of tp_*() API appends a request to
 * the tail of the buffer. If the buffer becomes too small to
 * contain the binary packet, the reallocation function is
 * invoked, to enlarge the buffer.
 * A buffer can contain multiple requests: Tarantool/Box will
 * handle them all asynchronously, request id can be then
 * used to associate responses with requests.
 *
 * For example:
 *
 * char buf[256];
 * struct tp req;
 * // initialize request buffer
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * // append INSERT packet header to the buffer
 * // request flags are empty, request id is 0
 * tp_insert(&req, 0, 0);
 * // begin appending a tuple to the request
 * tp_tuple(&req);
 * // append one tuple field
 * tp_sz(&req, "key");
 * // one more tuple field
 * tp_sz(&req, "value");
 *
 * write(1, buf, tp_used(&req)); // write the buffer to stdout
 *
 * (3) the buffer can be used right after all requests are
 * appended to it. tp_used() can be used to get the current buffer size.
 *
 * (4) In the end, the buffer must be freed manually.
 *
 * For additional examples, please read the documentation for various
 * buffer operations.
 *
 * REPLY PROCESSING
 * ----------------
 *
 * (1) tp_init() must be called with a pointer to a buffer which
 * contains a fully read reply.
 * Functions tp_reqbuf(), tp_req() can be then used to examine if
 * a network buffer contains a full reply or not.
 *
 * Following is an example of tp_req() usage (reading from stdin
 * and parsing reply it is completely read):
 *
 * struct tp rep;
 * tp_init(&rep, NULL, 0, tp_reallocator, NULL);
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
 *  // discard read data and make space available
 *  // for the next reply
 *  tp_use(&rep, to_read);
 * }
 *
 * (2) tp_reply() function can be used to find out if the request
 * is executed successfully or not :
 * server_code = tp_reply(&reply);
 *
 * if (server_code != 0) {
 *   printf("error: %-.*s\n", tp_replyerrorlen(&rep),
 *          tp_replyerror(&rep));
 * }
 *
 * Note: the library itself doesn't contain #defines for server
 * error codes. Different server error codes are defined in
 * https://github.com/mailru/tarantool/blob/master/include/errcode.h
 *
 * Generally, a server failure can be either transient, or
 * persistent.
 * For example, a failure to allocate memory is transient: as soon
 * as some data is deleted, the request can be executed again,
 * successfully. A constraint violation is a non-transient error:
 * it will persist regardless of how many times a request is
 * re-executed. Server error codes can be analyzed to better
 * handle an error.
 *
 * (3) The server usually responds to any kind of request with a
 * tuple. Tuple data can be accessed by tp_next(), tp_nextfield(),
 * tp_gettuple(), tp_getfield() functions.
 *
 * See the docs for tp_reply() and tp_next()/tp_nextfield() for an
 * example.
 *
 * API RETURN VALUE CONVENTION
 * ---------------------------
 *
 * Any API function, generally, returns 0 on success,
 * -1 on error.
 * Functions, which append to struct tp, return the
 * size appended to the buffer on success, or -1 on error.
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

/* request types. */
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
	uint32_t space, flags;
} tp_packed;

struct tp_hdelete {
	uint32_t space, flags;
} tp_packed;

struct tp_hupdate {
	uint32_t space, flags;
} tp_packed;

struct tp_hcall {
	uint32_t flags;
} tp_packed;

struct tp_hselect {
	uint32_t space, index;
	uint32_t offset;
	uint32_t limit;
	uint32_t keyc;
} tp_packed;

/*
 * Main tp object.
 *
 * Object contains private fields, that should
 * not be accessed directly. Appropriate accessors
 * should be used instead.
*/
struct tp {
	struct tp_h *h;        /* current request header */
	char *s, *p, *e;       /* start, pos, end */
	char *t, *f, *u;       /* tuple, tuple field, update operation */
	char *c;               /* reply parsing position */
	uint32_t tsz, fsz, tc; /* tuple size, field size, tuple count */
	uint32_t code;         /* reply server code */
	uint32_t cnt;          /* reply tuple count */
	tp_resizer resizer;    /* realloc function pointer */
	void *obj;             /* reallocation object pointer */
};

/* Get the size of allocated buffer */
static inline size_t
tp_size(struct tp *p) {
	return p->e - p->s;
}

/* Get the size of data in the buffer */
static inline size_t
tp_used(struct tp *p) {
	return p->p - p->s;
}

/* Get the size available for write */
static inline size_t
tp_unused(struct tp *p) {
	return p->e - p->p;
}

/* A common reallocation function.
 * Resizes the buffer twice the previous size.
 *
 * struct tp req;
 * tp_init(&req, NULL, tp_reallocator, NULL);
 * tp_ping(&req); // will call reallocator
 *
 * data must be manually freed on finish.
 * (eg. free(p->s));
*/
tp_function_unused static char*
tp_reallocator(struct tp *p, size_t required, size_t *size) {
	size_t toalloc = tp_size(p) * 2;
	if (tp_unlikely(toalloc < required))
		toalloc = required;
	*size = toalloc;
	return realloc(p->s, toalloc);
}

/* Main initialization function.
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

/* Ensure that buffer has enough space to fill size bytes, resize
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

/* Mark size bytes as used.
 * Can be used while appending data to complete reply. */
static inline ssize_t
tp_use(struct tp *p, size_t size) {
	p->p += size;
	return tp_used(p);
}

/* Append a data to the buffer.
 * Mostly unused, can be used to add any raw prepared
 * data to the buffer.
 * See: tp_tuple(), tp_field(), tp_sz().
 * */
static inline ssize_t
tp_append(struct tp *p, const void *data, size_t size) {
	if (tp_unlikely(tp_ensure(p, size) == -1))
		return -1;
	memcpy(p->p, data, size);
	return tp_use(p, size);
}

/* Set current request id.
 *
 * tp_ping(&req);
 * tp_reqid(&req, 777);
 */
static inline void
tp_reqid(struct tp *p, uint32_t reqid) {
	assert(p->h != NULL);
	p->h->reqid = reqid;
}

/* Get current request id */
static inline uint32_t
tp_getreqid(struct tp *p) {
	assert(p->h != NULL);
	return p->h->reqid;
}

/* Get tuple count */
static inline uint32_t
tp_tuplecount(struct tp *p) {
	assert(p->t != NULL);
	return *(uint32_t*)(p->t);
}

/* Write tuple header */
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

/* Leb128 calculation functions, internally used by the library */
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

/* Write tuple field, usable after tp_tuple() call */
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

/* Internal function, set current request */
static inline void
tp_setreq(struct tp *p) {
	p->h = (struct tp_h*)p->p;
	p->t = NULL;
	p->u = NULL;
}

/* Internal function, set current request and
 * append data to the buffer.  */
static inline ssize_t
tp_appendreq(struct tp *p, void *h, size_t size) {
	tp_setreq(p);
	return tp_append(p, h, size);
}

/* Write ping request.
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

/* Write insert request.
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
	h.i.space = space;
	h.i.flags = flags;
	return tp_appendreq(p, &h, sizeof(h));
}

/* Write delete request.
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
	h.d.space = space;
	h.d.flags = flags;
	return tp_appendreq(p, &h, sizeof(h));
}

/* Write call request.
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

/* Write select request.
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
	h.s.space = space;
	h.s.index = index;
	h.s.offset = offset;
	h.s.limit = limit;
	h.s.keyc = 0;
	return tp_appendreq(p, &h, sizeof(h));
}

/* Write update request.
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
	h.u.space = space;
	h.u.flags = flags;
	return tp_appendreq(p, &h, sizeof(h));
}

/* Write operation counter,
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

/* Write update operation.
 *
 * May be called after tp_updatebegin().
 * Can be used to request TP_OPSET, TP_OPADD, TP_OPAND,
 * TP_OPXOR, TP_OPOR operations.
 *
 * See: tp_update() for example.
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

/* Splice update operation */
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

/* Write tuple field as string */
static inline ssize_t
tp_sz(struct tp *p, const char *sz) {
	return tp_field(p, sz, strlen(sz));
}

/* Get how many bytes is still required to process reply.
 * Return value can be negative. */
static inline ssize_t
tp_reqbuf(const char *buf, size_t size) {
	if (tp_unlikely(size < sizeof(struct tp_h)))
		return sizeof(struct tp_h) - size;
	register int sz =
		((struct tp_h*)buf)->len + sizeof(struct tp_h);
	return (tp_likely(size < sz)) ?
	                  sz - size : -(size - sz);
}

/* Same as tp_reqbuf(), but tp initialized with buffer */
static inline ssize_t
tp_req(struct tp *p) {
	return tp_reqbuf(p->s, tp_size(p));
}

/* Get the size of a yet unprocessed reply data, used internally */
static inline size_t
tp_unfetched(struct tp *p) {
	return p->p - p->c;
}

/* Advance reply processed pointer, used internally */
static inline void*
tp_fetch(struct tp *p, int inc) {
	assert(tp_unfetched(p) >= inc);
	register char *po = p->c;
	p->c += inc;
	return po;
}

/* Get current reply error */
static inline char*
tp_replyerror(struct tp *p) {
	return p->c;
}

/* Get current reply error length */
static inline int
tp_replyerrorlen(struct tp *p) {
	return tp_unfetched(p);
}

/* Get current reply tuple count */
static inline uint32_t
tp_replycount(struct tp *p) {
	return p->cnt;
}

/* Get current reply returned coded */
static inline uint32_t
tp_replycode(struct tp *p) {
	return p->code;
}

/* Get current reply operation */
static inline uint32_t
tp_replyop(struct tp *p) {
	return p->h->type;
}

/*
 * Process reply.
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
 *
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

/* Example: iteration on returned tuples.
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

/* Rewind iteration to a first tuple */
static inline void
tp_rewind(struct tp *p) {
	p->t = NULL;
	p->f = NULL;
}

/* Rewind iteration to a first field */
static inline void
tp_rewindfield(struct tp *p) {
	p->f = NULL;
}

/* Get current tuple data */
static inline char*
tp_gettuple(struct tp *p) {
	return p->t;
}

/* Get current tuple size */
static inline uint32_t
tp_tuplesize(struct tp *p) {
	return p->tsz;
}

/* Get current field data */
static inline char*
tp_getfield(struct tp *p) {
	return p->f;
}

/* Get current field size */
static inline uint32_t
tp_getfieldsize(struct tp *p) {
	return p->fsz;
}

/* Get the pointer to the end of the current tuple */
static inline char*
tp_tupleend(struct tp *p) {
	/* tuple_size + p->t + cardinaltiy_size +
	 * fields_size */
	return p->t + 4 + p->tsz;
}

/* Check if reply has a result data.
 * Automatically checked during tp_next() iteration. */
static inline int
tp_hasdata(struct tp *p) {
	return tp_replyop(p) != TP_PING && tp_unfetched(p) > 0;
}

/* Check if there is more tuple */
static inline int
tp_hasnext(struct tp *p) {
	assert(p->t != NULL);
	return (p->p - tp_tupleend(p)) >= 4;
}

/* Check if tuple has next field */
static inline int
tp_hasnextfield(struct tp *p) {
	assert(p->t != NULL);
	register char *f = p->f + p->fsz;
	if (tp_unlikely(p->f == NULL))
		f = p->t + 4;
	return (tp_tupleend(p) - f) >= 1;
}

/* Skip to next tuple.
 * Tuple can be accessed using:
 * tp_tuplecount(), tp_tuplesize(), tp_gettuple(). */
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

/* Skip to next field.
 * Data can be accessed using: tp_getfieldsize(), tp_getfield(). */
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
