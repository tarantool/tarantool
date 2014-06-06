#ifndef TP_H_INCLUDED
#define TP_H_INCLUDED

/*
 * TP - Tarantool Protocol library.
 * (http://tarantool.org)
 *
 * protocol description:
 * https://github.com/tarantool/tarantool/blob/master/doc/box-protocol.txt
 * -------------------
 *
 * TP - a C library designed to create requests and process
 * replies to or from a Tarantool server.
 *
 * The library is designed to be used by a C/C++ application which
 * requires sophisticated memory control and/or performance.
 *
 * The library does not support network operations. All operations
 * are done in a user supplied buffer and with help of
 * a user-supplied allocator.
 *
 * The primary purpose of the library was to spare implementors
 * of Tarantool drivers in other languages, such as Perl,
 * Ruby, Python, etc, from the details of the binary protocol, and
 * also to make it possible to avoid double-buffering by writing
 * directly to/from language objects from/to a serialized binary
 * packet stream. This paradigm makes data transfer from domain
 * language types (such as strings, scalars, numbers, etc) to
 * the network format direct, and, therefore, most efficient.
 *
 * As a side effect, the library is usable in any kind of
 * networking environment: synchronous with buffered sockets, or
 * asynchronous event-based, as well as with cooperative
 * multitasking.
 *
 * Before using the library, please get acquainted with
 * Tarnatool binary protocol, documented at
 * https://github.com/tarantool/tarantool/blob/master/doc/box-protocol.txt
 *
 * BASIC REQUEST STRUCTURE
 * -----------------------
 *
 * Any request in Tarantool consists of a 12-byte header,
 * containing request type, id and length, and an optional tuple
 * or tuples. Similarly, a response carries back the same request
 * type and id, and then a tuple or tuples.
 *
 * Below is a step-by-step tutorial for creating requests
 * and unpacking responses.
 *
 * TO ASSEMBLE A REQUEST
 * ---------------------
 *
 * (1) initialize an instance of struct tp with tp_init().
 * Provide tp_init() with a buffer and an (optional) allocator
 * function.
 *
 * (2) construct requests by sequentially calling necessary
 * operations, such as tp_insert(), tp_delete(), tp_update(),
 * tp_call(). Note: these operations only append to the buffer
 * a request header, a body of the request, which is usually
 * a tuple, must be appended to the buffer with a separate call.
 * Each next call of tp_*() API appends request data to
 * the tail of the buffer. If the buffer becomes too small to
 * contain the binary stream, the reallocation function is
 * invoked to enlarge the buffer.
 * A buffer can contain multiple requests: Tarantool
 * handles them all asynchronously, sending responses
 * back as soon as they are ready. The request id can be then
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
 * (3) the buffer can be used right after all requests are
 * appended to it. tp_used() can be used to get the current
 * buffer size:
 *
 * write(1, buf, tp_used(&req)); // write the buffer to stdout
 *
 * (4) When no longer needed, the buffer must be freed manually.
 *
 * For additional examples, please read the documentation for
 * buffer operations.
 *
 * PROCESSING A REPLY
 * ------------------
 *
 * (1) tp_init() must be called with a pointer to a buffer which
 * already stores or will eventually receive the server response.
 * Functions tp_reqbuf() and tp_req() can be then used to examine
 * if a network buffer contains a full reply or not.
 *
 * Following is an example of tp_req() usage (reading from stdin
 * and parsing it until a response is completely read):
 *
 * struct tp rep;
 * tp_init(&rep, NULL, 0, tp_realloc, NULL);
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
 *  // discard processed data and make space available
 *  // for new input:
 *  tp_use(&rep, to_read);
 * }
 *
 * (2) tp_reply() function can be used to find out if the request
 * is executed successfully or not:
 * server_code = tp_reply(&reply);
 *
 * if (server_code != 0) {
 *   printf("error: %-.*s\n", tp_replyerrorlen(&rep),
 *          tp_replyerror(&rep));
 * }
 *
 * Note: the library itself doesn't contain #defines for server
 * error codes. They are defined in
 * https://github.com/tarantool/tarantool/blob/master/include/errcode.h
 *
 * A server failure can be either transient or persistent. For
 * example, a failure to allocate memory is transient: as soon as
 * some data is deleted, the request can be executed again, this
 * time successfully. A constraint violation is a non-transient
 * error: it persists regardless of how many times a request
 * is re-executed. Server error codes can be analyzed to better
 * handle an error.
 *
 * (3) The server usually responds to any kind of request with a
 * tuple. Tuple data can be accessed via tp_next(), tp_nextfield(),
 * tp_gettuple(), tp_getfield().
 *
 * See the docs for tp_reply() and tp_next()/tp_nextfield() for an
 * example.
 *
 * API RETURN VALUE CONVENTION
 * ---------------------------
 *
 * API functions return 0 on success, -1 on error.
 * If a function appends data to struct tp, it returns the
 * size appended on success, or -1 on error.
 *
 * SEE ALSO
 * --------
 *
 * TP is used by Tarantool Perl driver:
 * https://github.com/dr-co/dr-tarantool/blob/master/Tarantool.xs
*/

/*
 * Copyright (c) 2012-2013 Tarantool AUTHORS
 * (https://github.com/tarantool/tarantool/blob/master/AUTHORS)
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

/* Reallocation function, can be customized for own use */
typedef char *(*tp_reserve)(struct tp *p, size_t req, size_t *size);

/* request types. */
#define TP_PING   65280
#define TP_INSERT 13
#define TP_SELECT 17
#define TP_UPDATE 19
#define TP_DELETE 21
#define TP_CALL   22

/* requests flags */
#define TP_BOX_RETURN_TUPLE   1
#define TP_BOX_ADD            2
#define TP_BOX_REPLACE        4

/* update operation codes */
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
 * Main tp object - points either to a request buffer, or to
 * a response.
 *
 * All fields except tp->p should not be accessed directly.
 * Appropriate accessors should be used instead.
*/
struct tp {
	struct tp_h *h;        /* current request header */
	char *s, *p, *e;       /* start, pos, end */
	char *t, *f, *u;       /* tuple, tuple field, update operation */
	char *c;               /* reply parsing position */
	uint32_t tsz, fsz, tc; /* tuple size, field size, tuple count */
	uint32_t code;         /* reply server code */
	uint32_t cnt;          /* reply tuple count */
	tp_reserve reserve;    /* realloc function pointer */
	void *obj;             /* reallocation context pointer */
};

/* Get the size of the allocated buffer */
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

/* A common reallocation function, can be used
 * for 'reserve' param in tp_init().
 * Resizes the buffer twice the previous size using realloc().
 *
 * struct tp req;
 * tp_init(&req, NULL, tp_realloc, NULL);
 * tp_ping(&req); // will call the reallocator
 *
 * data must be manually freed when the buffer is no longer
 * needed.
 * (eg. free(p->s));
 * if realloc will return NULL, then you must destroy previous memory.
 * (eg.
 * if (tp_realloc(p, ..) == NULL) {
 * 	free(p->s)
 * 	return NULL;
 * }
*/
tp_function_unused static char*
tp_realloc(struct tp *p, size_t required, size_t *size) {
	size_t sz = tp_size(p) * 2;
	size_t actual = tp_used(p) + required;
	if (tp_unlikely(actual > sz))
		sz = actual;
	*size = sz;
	return realloc(p->s, sz);
}

/* Free function for use in a pair with tp_realloc */
static inline void
tp_free(struct tp *p) {
	free(p->s);
}

/* Get currently allocated buffer pointer */
static inline char*
tp_buf(struct tp *p) {
	return p->s;
}

/* Main initialization function.
 *
 * reserve - reallocation function, may be NULL
 * obj     - pointer to be passed to the reallocation function as
 *           context
 * buf     - current buffer, may be NULL
 * size    - current buffer size
 *
 * Either a buffer pointer or a reserve function must be
 * provided.
*/
static inline void
tp_init(struct tp *p, char *buf, size_t size,
        tp_reserve reserve, void *obj) {
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
	p->reserve = reserve;
	p->obj = obj;
}

/* Ensure that buffer has enough space to fill size bytes, resize
 * buffer if needed. */
static tp_noinline ssize_t
tp_ensure(struct tp *p, size_t size) {
	if (tp_likely(tp_unused(p) >= size))
		return 0;
	if (tp_unlikely(p->reserve == NULL))
		return -1;
	size_t sz;
	register char *np = p->reserve(p, size, &sz);
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
 * Can be used to tell the buffer that a chunk has been read
 * from the network into it.
 */
static inline ssize_t
tp_use(struct tp *p, size_t size) {
	p->p += size;
	return tp_used(p);
}

/* Append data to the buffer.
 * Mostly unnecessary, but can be used to add any raw
 * iproto-format data to the buffer.
 * Normally tp_tuple(), tp_field() and tp_sz() should be used
 * instead.
 */
static inline ssize_t
tp_append(struct tp *p, const void *data, size_t size) {
	if (tp_unlikely(tp_ensure(p, size) == -1))
		return -1;
	memcpy(p->p, data, size);
	return tp_use(p, size);
}

/* Set the current request id.
 *
 * tp_ping(&req);
 * tp_reqid(&req, 777);
 */
static inline void
tp_reqid(struct tp *p, uint32_t reqid) {
	assert(p->h != NULL);
	p->h->reqid = reqid;
}

/* Return the current request id */
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

/* Write a tuple header */
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

/* Ber128 calculation functions, internally used by the library */
static inline size_t
tp_ber128sizeof(uint32_t value) {
	return (  tp_likely(value < (1 <<  7))) ? 1 :
	       (  tp_likely(value < (1 << 14))) ? 2 :
	       (tp_unlikely(value < (1 << 21))) ? 3 :
	       (tp_unlikely(value < (1 << 28))) ? 4 : 5;
}

static tp_noinline void tp_hot
tp_ber128save_slowpath(struct tp *p, uint32_t value) {
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
tp_ber128save(struct tp *p, uint32_t value) {
	if (tp_unlikely(value >= (1 << 14))) {
		tp_ber128save_slowpath(p, value);
		return;
	}
	if (tp_likely(value >= (1 << 7)))
		*(p->p++) = ((value >> 7) | 0x80);
	*(p->p++) = ((value) & 0x7F);
}

static tp_noinline int tp_hot
tp_ber128load_slowpath(struct tp *p, uint32_t *value) {
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
tp_ber128load(struct tp *p, uint32_t *value) {
	if (tp_likely(! (p->f[0] & 0x80))) {
		*value = *(p->f++) & 0x7f;
	} else
	if (tp_likely(! (p->f[1] & 0x80))) {
		*value = (p->f[0] & 0x7f) << 7 | (p->f[1] & 0x7f);
		p->f += 2;
	} else
		return tp_ber128load_slowpath(p, value);
	return 0;
}

/* Write a tuple field
 * Note: the tuple must be started prior to calling
 * this function with tp_tuple() call.
 */
static inline ssize_t
tp_field(struct tp *p, const char *data, size_t size) {
	assert(p->h != NULL);
	assert(p->t != NULL);
	register int esz = tp_ber128sizeof(size);
	if (tp_unlikely(tp_ensure(p, esz + size) == -1))
		return -1;
	tp_ber128save(p, size);
	memcpy(p->p, data, size);
	p->p += size;
	(*(uint32_t*)p->t)++;
	p->h->len += esz + size;
	return tp_used(p);
}

/* Set the current request.
 * Note: this is an internal helper function, not part of the
 * tp.h API.
 */
static inline void
tp_setreq(struct tp *p) {
	p->h = (struct tp_h*)p->p;
	p->t = NULL;
	p->u = NULL;
}

/* Set current request and append data to the buffer.
 * Note: this is an internal helper function, not part of the
 * tp.h API. tp_ping(), tp_update() and other functions
 * which directly create a request header should be used
 * instead.
 */
static inline ssize_t
tp_appendreq(struct tp *p, void *h, size_t size) {
	int isallocated = p->p != NULL;
	tp_setreq(p);
	ssize_t rc = tp_append(p, h, size);
	if (tp_unlikely(rc == -1))
		return -1;
	if (!isallocated)
		p->h = (struct tp_h*)p->s;
	return rc;
}

/* Create a ping request.
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

/* Create an insert request.
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

/* Create a delete request.
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

/* Create a call request.
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
tp_call(struct tp *p, uint32_t flags, const char *name, size_t name_len) {
	struct {
		struct tp_h h;
		struct tp_hcall c;
	} h;
	size_t sz = tp_ber128sizeof(name_len);
	h.h.type = TP_CALL;
	h.h.len = sizeof(struct tp_hcall) + sz + name_len;
	h.h.reqid = 0;
	h.c.flags = flags;
	if (tp_unlikely(tp_ensure(p, sizeof(h) + sz + name_len) == -1))
		return -1;
	tp_setreq(p);
	memcpy(p->p, &h, sizeof(h));
	p->p += sizeof(h);
	tp_ber128save(p, name_len);
	memcpy(p->p, name, name_len);
	p->p += name_len;
	return tp_used(p);
}

/* Append a select request.
 *
 * char buf[64];
 * struct tp req;
 * tp_init(&req, buf, sizeof(buf), NULL, NULL);
 * tp_select(&req, 0, 0, 0, 100);
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

/* Create an update request.
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

/* Append the number of operations the update request
 * is going to contain.
 * Must be called right after appending the key which
 * identifies the tuple which must be updated. Since
 * the key can be multipart, tp_tuple() must be used to
 * append it.
 *
 * In other words, this call sequence creates a proper
 * UPDATE request:
 * tp_init(...)
 * tp_update()
 * tp_tuple()
 * tp_sz(), tp_sz(), ...
 * tp_updatebegin()
 * tp_op(), tp_op(), ...
 */
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

/* Append a single UPDATE operation.
 *
 * May be called after tp_updatebegin().
 * Can be used to create TP_OPSET, TP_OPADD, TP_OPAND,
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
	size_t sz = 4 + 1 + tp_ber128sizeof(size) + size;
	if (tp_unlikely(tp_ensure(p, sz)) == -1)
		return -1;
	/* field */
	*(uint32_t*)(p->p) = field;
	p->p += sizeof(uint32_t);
	/* operation */
	*(uint8_t*)(p->p) = op;
	p->p += sizeof(uint8_t);
	/* data */
	tp_ber128save(p, size);
	if (tp_likely(data))
		memcpy(p->p, data, size);
	p->p += size;
	/* update offset and count */
	p->h->len += sz;
	(*(uint32_t*)p->u)++;
	return tp_used(p);
}

/* Append a SPLICE operation. This operation is unlike any other,
 * since it takes three arguments instead of one.
 */
static inline ssize_t
tp_opsplice(struct tp *p, uint32_t field, uint32_t offset,
	    uint32_t cut, const char *paste, size_t paste_len) {
	uint32_t olen = tp_ber128sizeof(sizeof(offset)),
	         clen = tp_ber128sizeof(sizeof(cut)),
	         plen = tp_ber128sizeof(paste_len);
	uint32_t sz = olen + sizeof(offset) + clen + sizeof(cut) +
	              plen + paste_len;
	ssize_t rc = tp_op(p, field, TP_OPSPLICE, NULL, sz);
	if (tp_unlikely(rc == -1))
		return -1;
	p->p -= sz;
	tp_ber128save(p, sizeof(offset));
	memcpy(p->p, &offset, sizeof(offset));
	p->p += sizeof(offset);
	tp_ber128save(p, sizeof(cut));
	memcpy(p->p, &cut, sizeof(cut));
	p->p += sizeof(cut);
	tp_ber128save(p, paste_len);
	memcpy(p->p, paste, paste_len);
	p->p += paste_len;
	return rc;
}

/* Append a '\0' terminated string as a tuple field. */
static inline ssize_t
tp_sz(struct tp *p, const char *sz) {
	return tp_field(p, sz, strlen(sz));
}

/*
 * Returns the number of bytes which are required to fully
 * store a reply in the buffer.
 * The return value can be negative, which indicates that
 * there is a complete reply in the buffer which is not parsed
 * and discarded yet.
 */
static inline ssize_t
tp_reqbuf(const char *buf, size_t size) {
	if (tp_unlikely(size < sizeof(struct tp_h)))
		return sizeof(struct tp_h) - size;
	register int sz =
		((struct tp_h*)buf)->len + sizeof(struct tp_h);
	return (tp_likely(size < sz)) ?
	                  sz - size : -(size - sz);
}

/* Same as tp_reqbuf(), but works on the buffer in struct tp.
 */
static inline ssize_t
tp_req(struct tp *p) {
	return tp_reqbuf(p->s, tp_size(p));
}

/* Get the size of a yet unprocessed reply data.
 *
 * This is not part of the API.
 */
static inline size_t
tp_unfetched(struct tp *p) {
	return p->p - p->c;
}

/* Advance the reply processed pointer.
 *
 * This is not part of the API, tp_use() is a higher level
 * function.
 */
static inline void*
tp_fetch(struct tp *p, int inc) {
	assert(tp_unfetched(p) >= inc);
	register char *po = p->c;
	p->c += inc;
	return po;
}

/* Get the last server error.
*/
static inline char*
tp_replyerror(struct tp *p) {
	return p->c;
}

/* Get the length of the last error message.
 */
static inline int
tp_replyerrorlen(struct tp *p) {
	return tp_unfetched(p);
}

/* Get the tuple count in the response (there must be
 * no error).
 */
static inline uint32_t
tp_replycount(struct tp *p) {
	return p->cnt;
}

/* Get the current response return code.
 */
static inline uint32_t
tp_replycode(struct tp *p) {
	return p->code;
}

/* Get the current response operation code. */
static inline uint32_t
tp_replyop(struct tp *p) {
	return p->h->type;
}

/*
 * Initialize the buffer with a fully read server response.
 * The response is parsed.
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

/* Example: iteration over returned tuples.
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

/* Rewind iteration to the first tuple. */
static inline void
tp_rewind(struct tp *p) {
	p->t = NULL;
	p->f = NULL;
}

/* Rewind iteration to the first tuple field of the current tuple. */
static inline void
tp_rewindfield(struct tp *p) {
	p->f = NULL;
}

/* Get the current tuple data, all fields. */
static inline char*
tp_gettuple(struct tp *p) {
	return p->t;
}

/* Get the current tuple size. */
static inline uint32_t
tp_tuplesize(struct tp *p) {
	return p->tsz;
}

/* Get the current field. */
static inline char*
tp_getfield(struct tp *p) {
	return p->f;
}

/* Get the current field size. */
static inline uint32_t
tp_getfieldsize(struct tp *p) {
	return p->fsz;
}

/* Get a pointer to the end of the current tuple. */
static inline char*
tp_tupleend(struct tp *p) {
	/* tuple_size + p->t + cardinaltiy_size +
	 * fields_size */
	return p->t + 4 + p->tsz;
}

/* Check if the response has a tuple.
 * Automatically checked during tp_next() iteration. */
static inline int
tp_hasdata(struct tp *p) {
	return tp_replyop(p) != TP_PING && tp_unfetched(p) > 0;
}

/* Check if there is a one more tuple. */
static inline int
tp_hasnext(struct tp *p) {
	assert(p->t != NULL);
	return (p->p - tp_tupleend(p)) >= 4;
}

/* Check if the current tuple has a one more field. */
static inline int
tp_hasnextfield(struct tp *p) {
	assert(p->t != NULL);
	register char *f = p->f + p->fsz;
	if (tp_unlikely(p->f == NULL))
		f = p->t + 4;
	return (tp_tupleend(p) - f) >= 1;
}

/* Skip to the next tuple.
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
	if (tp_unlikely((p->t + p->tsz) > p->e))
		return -1;
	p->f = NULL;
	return 1;
}

/* Skip to the next field.
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
	register int rc = tp_ber128load(p, &p->fsz);
	if (tp_unlikely(rc == -1))
		return -1;
	if (tp_unlikely((p->f + p->fsz) > p->e))
		return -1;
	return 1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TP_H_INCLUDED */
