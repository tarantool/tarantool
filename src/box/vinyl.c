/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "vinyl.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <pmatomic.h>

#include <lz4.h>
#include <lz4frame.h>
#include <zstd_static.h>

#include <bit/bit.h>
#include <small/rlist.h>
#define RB_COMPACT 1
#define RB_CMP_TREE_ARG 1
#include <small/rb.h>
#include <small/mempool.h>
#include <small/region.h>
#include <msgpuck/msgpuck.h>
#include <coeio_file.h>

#include "trivia/util.h"
#include "crc32.h"
#include "clock.h"
#include "trivia/config.h"
#include "tt_pthread.h"
#include "cfg.h"
#include "diag.h"
#include "fiber.h" /* cord_slab_cache() */
#include "ipc.h"
#include "coeio.h"

#include "errcode.h"
#include "key_def.h"
#include "tuple.h"
#include "tuple_update.h"
#include "txn.h" /* box_txn_alloc() */

#include "vclock.h"
#include "assoc.h"

#define vy_cmp(a, b) \
	((a) == (b) ? 0 : (((a) > (b)) ? 1 : -1))

enum vinyl_status {
	VINYL_OFFLINE,
	VINYL_INITIAL_RECOVERY,
	VINYL_FINAL_RECOVERY,
	VINYL_ONLINE,
	VINYL_DROP,
	VINYL_MALFUNCTION
};

struct vy_conf;
struct vy_quota;
struct tx_manager;
struct vy_scheduler;
struct vy_task;
struct vy_stat;
struct srzone;

struct vy_env {
	enum vinyl_status status;
	/** List of open spaces. */
	struct rlist indexes;
	struct vy_conf      *conf;
	struct vy_quota     *quota;
	struct tx_manager   *xm;
	struct vy_scheduler *scheduler;
	struct vy_stat      *stat;
	struct mempool      cursor_pool;
};

static inline struct srzone *
sr_zoneof(struct vy_env *r);

struct vy_buf {
	/** Start of the allocated buffer */
	char *s;
	/** End of the used area */
	char *p;
	/** End of the buffer */
	char *e;
};

static inline void
vy_buf_create(struct vy_buf *b)
{
	b->s = NULL;
	b->p = NULL;
	b->e = NULL;
}

static inline void
vy_buf_destroy(struct vy_buf *b)
{
	if (unlikely(b->s == NULL))
		return;
	free(b->s);
	b->s = NULL;
	b->p = NULL;
	b->e = NULL;
}

static inline size_t
vy_buf_size(struct vy_buf *b) {
	return b->e - b->s;
}

static inline size_t
vy_buf_used(struct vy_buf *b) {
	return b->p - b->s;
}

static inline size_t
vy_buf_unused(struct vy_buf *b) {
	return b->e - b->p;
}

static inline int
vy_buf_ensure(struct vy_buf *b, size_t size)
{
	if (likely(b->e - b->p >= (ptrdiff_t)size))
		return 0;
	size_t sz = vy_buf_size(b) * 2;
	size_t actual = vy_buf_used(b) + size;
	if (unlikely(actual > sz))
		sz = actual;
	char *p;
	if (b->s == NULL) {
		p = malloc(sz);
		if (unlikely(p == NULL)) {
			diag_set(OutOfMemory, sz, "malloc", "vy_buf->p");
			return -1;
		}
	} else {
		p = realloc(b->s, sz);
		if (unlikely(p == NULL)) {
			diag_set(OutOfMemory, sz, "realloc", "vy_buf->p");
			return -1;
		}
	}
	b->p = p + (b->p - b->s);
	b->e = p + sz;
	b->s = p;
	assert((b->e - b->p) >= (ptrdiff_t)size);
	return 0;
}

static inline void
vy_buf_advance(struct vy_buf *b, size_t size)
{
	b->p += size;
}

static inline int
vy_buf_add(struct vy_buf *b, void *buf, size_t size)
{
	int rc = vy_buf_ensure(b, size);
	if (unlikely(rc == -1))
		return -1;
	memcpy(b->p, buf, size);
	vy_buf_advance(b, size);
	return 0;
}

static inline int
vy_buf_in(struct vy_buf *b, void *v) {
	assert(b->s != NULL);
	return (char*)v >= b->s && (char*)v < b->p;
}

static inline void*
vy_buf_at(struct vy_buf *b, int size, int i) {
	return b->s + size * i;
}

#define vy_crcs(p, size, crc) \
	crc32_calc(crc, (char*)p + sizeof(uint32_t), size - sizeof(uint32_t))

struct vy_quota {
	bool enable;
	int64_t limit;
	int64_t used;
	struct ipc_cond cond;
};

static struct vy_quota *
vy_quota_new(int64_t);

static int
vy_quota_delete(struct vy_quota*);

static void
vy_quota_enable(struct vy_quota*);

static inline int64_t
vy_quota_used(struct vy_quota *q)
{
	return q->used;
}

static inline int
vy_quota_used_percent(struct vy_quota *q)
{
	if (q->limit == 0)
		return 0;
	return (q->used * 100) / q->limit;
}

/* range queue */

struct ssrqnode {
	uint32_t q, v;
	struct rlist link;
};

struct ssrqq {
	uint32_t count;
	uint32_t q;
	struct rlist list;
};

struct ssrq {
	uint32_t range_count;
	uint32_t range;
	uint32_t last;
	struct ssrqq *q;
};

static inline void
ss_rqinitnode(struct ssrqnode *n) {
	rlist_create(&n->link);
	n->q = UINT32_MAX;
	n->v = 0;
}

static inline int
ss_rqinit(struct ssrq *q, uint32_t range, uint32_t count)
{
	q->range_count = count + 1 /* zero */;
	q->range = range;
	q->q = malloc(sizeof(struct ssrqq) * q->range_count);
	if (unlikely(q->q == NULL)) {
		diag_set(OutOfMemory, sizeof(struct ssrqq) * q->range_count,
			 "malloc", "struct ssrq");
		return -1;
	}
	uint32_t i = 0;
	while (i < q->range_count) {
		struct ssrqq *p = &q->q[i];
		rlist_create(&p->list);
		p->count = 0;
		p->q = i;
		i++;
	}
	q->last = 0;
	return 0;
}

static inline void
ss_rqfree(struct ssrq *q)
{
	if (q->q) {
		free(q->q);
		q->q = NULL;
	}
}

static inline void
ss_rqadd(struct ssrq *q, struct ssrqnode *n, uint32_t v)
{
	uint32_t pos;
	if (unlikely(v == 0)) {
		pos = 0;
	} else {
		pos = (v / q->range) + 1;
		if (unlikely(pos >= q->range_count))
			pos = q->range_count - 1;
	}
	struct ssrqq *p = &q->q[pos];
	rlist_create(&n->link);
	n->v = v;
	n->q = pos;
	rlist_add(&p->list, &n->link);
	if (unlikely(p->count == 0)) {
		if (pos > q->last)
			q->last = pos;
	}
	p->count++;
}

static inline void
ss_rqdelete(struct ssrq *q, struct ssrqnode *n)
{
	struct ssrqq *p = &q->q[n->q];
	p->count--;
	rlist_del(&n->link);
	if (unlikely(p->count == 0 && q->last == n->q))
	{
		int i = n->q - 1;
		while (i >= 0) {
			struct ssrqq *p = &q->q[i];
			if (p->count > 0) {
				q->last = i;
				return;
			}
			i--;
		}
	}
}

static inline void
ss_rqupdate(struct ssrq *q, struct ssrqnode *n, uint32_t v)
{
	if (likely(n->q != UINT32_MAX))
		ss_rqdelete(q, n);
	ss_rqadd(q, n, v);
}

static inline struct ssrqnode*
ss_rqprev(struct ssrq *q, struct ssrqnode *n)
{
	int pos;
	struct ssrqq *p;
	if (likely(n)) {
		pos = n->q;
		p = &q->q[pos];
		if (n->link.next != (&p->list)) {
			return container_of(n->link.next, struct ssrqnode, link);
		}
		pos--;
	} else {
		pos = q->last;
	}
	for (; pos >= 0; pos--) {
		p = &q->q[pos];
		if (unlikely(p->count == 0))
			continue;
		return container_of(p->list.next, struct ssrqnode, link);
	}
	return NULL;
}

enum vy_filter_op {
	VINYL_FINPUT,
	VINYL_FOUTPUT
};

struct vy_filter;

struct vy_filterif {
	char *name;
	int (*create)(struct vy_filter*, va_list);
	int (*destroy)(struct vy_filter*);
	int (*start)(struct vy_filter*, struct vy_buf*);
	int (*next)(struct vy_filter*, struct vy_buf*, char*, int);
	int (*complete)(struct vy_filter*, struct vy_buf*);
};

struct vy_filter {
	struct vy_filterif *i;
	enum vy_filter_op op;
	char priv[90];
};

static inline int
vy_filter_create(struct vy_filter *c, struct vy_filterif *ci,
	       enum vy_filter_op op, ...)
{
	c->op = op;
	c->i  = ci;
	va_list args;
	va_start(args, op);
	int rc = c->i->create(c, args);
	va_end(args);
	return rc;
}

static inline int
vy_filter_destroy(struct vy_filter *c)
{
	return c->i->destroy(c);
}

static inline int
vy_filter_start(struct vy_filter *c, struct vy_buf *dest)
{
	return c->i->start(c, dest);
}

static inline int
vy_filter_next(struct vy_filter *c, struct vy_buf *dest, char *buf, int size)
{
	return c->i->next(c, dest, buf, size);
}

static inline int
vy_filter_complete(struct vy_filter *c, struct vy_buf *dest)
{
	return c->i->complete(c, dest);
}

static struct vy_filterif vy_filterif_lz4;

static struct vy_filterif vy_filterif_zstd;

static inline struct vy_filterif*
vy_filter_of(char *name)
{
	if (strcmp(name, "lz4") == 0)
		return &vy_filterif_lz4;
	if (strcmp(name, "zstd") == 0)
		return &vy_filterif_zstd;
	return NULL;
}

struct vy_iter;

struct vy_iterif {
	void  (*close)(struct vy_iter*);
	int   (*has)(struct vy_iter*);
	struct vy_tuple *(*get)(struct vy_iter*);
	void  (*next)(struct vy_iter*);
};

struct vy_iter {
	struct vy_iterif *vif;
	char priv[150];
};

#define vy_iter_get(i) (i)->vif->get(i)
#define vy_iter_next(i) (i)->vif->next(i)

struct vy_bufiter {
	struct vy_buf *buf;
	int vsize;
	void *v;
};

static inline void
vy_bufiter_open(struct vy_bufiter *bi, struct vy_buf *buf, int vsize)
{
	bi->buf = buf;
	bi->vsize = vsize;
	bi->v = bi->buf->s;
	if (bi->v != NULL && ! vy_buf_in(bi->buf, bi->v))
		bi->v = NULL;
}

static inline int
vy_bufiter_has(struct vy_bufiter *bi)
{
	return bi->v != NULL;
}

static inline void*
vy_bufiterref_get(struct vy_bufiter *bi)
{
	if (unlikely(bi->v == NULL))
		return NULL;
	return *(void**)bi->v;
}

static inline void
vy_bufiter_next(struct vy_bufiter *bi)
{
	if (unlikely(bi->v == NULL))
		return;
	bi->v = (char*)bi->v + bi->vsize;
	if (unlikely(! vy_buf_in(bi->buf, bi->v)))
		bi->v = NULL;
}

struct vy_avg {
	uint64_t count;
	uint64_t total;
	uint32_t min, max;
	double   avg;
	char sz[32];
};

static inline void
vy_avg_update(struct vy_avg *a, uint32_t v)
{
	a->count++;
	a->total += v;
	a->avg = (double)a->total / (double)a->count;
	if (v < a->min)
		a->min = v;
	if (v > a->max)
		a->max = v;
}

static inline void
vy_avg_prepare(struct vy_avg *a)
{
	snprintf(a->sz, sizeof(a->sz), "%"PRIu32" %"PRIu32" %.1f",
	         a->min, a->max, a->avg);
}

struct vy_filter_lz4 {
	LZ4F_compressionContext_t compress;
	LZ4F_decompressionContext_t decompress;
	size_t total_size;
};

static int
vy_filter_lz4_create(struct vy_filter *f, va_list args)
{
	(void) args;
	struct vy_filter_lz4 *z = (struct vy_filter_lz4*)f->priv;
	LZ4F_errorCode_t rc = -1;
	switch (f->op) {
	case VINYL_FINPUT:
		rc = LZ4F_createCompressionContext(&z->compress, LZ4F_VERSION);
		z->total_size = 0;
		break;
	case VINYL_FOUTPUT:
		rc = LZ4F_createDecompressionContext(&z->decompress,
						     LZ4F_VERSION);
		break;
	}
	if (unlikely(rc != 0))
		return -1;
	return 0;
}

static int
vy_filter_lz4_destroy(struct vy_filter *f)
{
	struct vy_filter_lz4 *z = (struct vy_filter_lz4*)f->priv;
	(void)z;
	switch (f->op) {
	case VINYL_FINPUT:
		LZ4F_freeCompressionContext(z->compress);
		break;
	case VINYL_FOUTPUT:
		LZ4F_freeDecompressionContext(z->decompress);
		break;
	}
	return 0;
}

#ifndef LZ4F_MAXHEADERFRAME_SIZE
/* Defined in lz4frame.c file */
#define LZ4F_MAXHEADERFRAME_SIZE 15
#endif

static int
vy_filter_lz4_start(struct vy_filter *f, struct vy_buf *dest)
{
	struct vy_filter_lz4 *z = (struct vy_filter_lz4*)f->priv;
	int rc;
	size_t block;
	size_t sz;
	switch (f->op) {
	case VINYL_FINPUT:;
		block = LZ4F_MAXHEADERFRAME_SIZE;
		rc = vy_buf_ensure(dest, block);
		if (unlikely(rc == -1))
			return -1;
		sz = LZ4F_compressBegin(z->compress, dest->p, block, NULL);
		if (unlikely(LZ4F_isError(sz)))
			return -1;
		vy_buf_advance(dest, sz);
		break;
	case VINYL_FOUTPUT:
		/* do nothing */
		break;
	}
	return 0;
}

static int
vy_filter_lz4_next(struct vy_filter *f, struct vy_buf *dest, char *buf, int size)
{
	struct vy_filter_lz4 *z = (struct vy_filter_lz4*)f->priv;
	if (unlikely(size == 0))
		return 0;
	int rc;
	switch (f->op) {
	case VINYL_FINPUT:;
		/* See comments in vy_filter_lz4_complete() */
		int capacity = LZ4F_compressBound(z->total_size + size, NULL);
		assert(capacity >= (ptrdiff_t)vy_buf_used(dest));
		rc = vy_buf_ensure(dest, capacity);
		if (unlikely(rc == -1))
			return -1;
		size_t sz = LZ4F_compressUpdate(z->compress, dest->p,
						vy_buf_unused(dest),
						buf, size, NULL);
		if (unlikely(LZ4F_isError(sz)))
			return -1;
		vy_buf_advance(dest, sz);
		z->total_size += size;
		break;
	case VINYL_FOUTPUT:;
		/* do a single-pass decompression.
		 *
		 * Assume that destination buffer is allocated to
		 * original size.
		 */
		size_t pos = 0;
		while (pos < (size_t)size)
		{
			size_t o_size = vy_buf_unused(dest);
			size_t i_size = size - pos;
			LZ4F_errorCode_t rc;
			rc = LZ4F_decompress(z->decompress, dest->p, &o_size,
					     buf + pos, &i_size, NULL);
			if (LZ4F_isError(rc))
				return -1;
			vy_buf_advance(dest, o_size);
			pos += i_size;
		}
		break;
	}
	return 0;
}

static int
vy_filter_lz4_complete(struct vy_filter *f, struct vy_buf *dest)
{
	struct vy_filter_lz4 *z = (struct vy_filter_lz4*)f->priv;
	int rc;
	switch (f->op) {
	case VINYL_FINPUT:;
		/*
		 * FIXME: LZ4F_compressXXX API is not designed for dynamically
		 * growing buffers. LZ4F_compressUpdate() compress data
		 * incrementally, but target buffer must be of fixed size.
		 * https://github.com/Cyan4973/lz4/blob/d86dc916771c126afb797637dda9f6421c0cb998/examples/frameCompress.c#L35
		 *
		 * z->compress (LZ4F_cctx_internal_t) has a temporary buffer
		 * cctxPtr->tmpIn which accumulates cctxPrr->tmpInSize bytes
		 * from the previous LZ4F_compressUpdate() calls. It may
		 * contain up to bufferSize ( 64KB - 4MB ) + 16 bytes.
		 * It is not efficient to pre-allocate, say, 4MB every time.
		 * This filter calculates the total size of input and then
		 * calls LZ4F_compressBound() to determine the total size
		 * of output (capacity).
		 */
#if 0
		LZ4F_cctx_internal_t* cctxPtr = z->compress;
		size_t block = (cctxPtr->tmpInSize + 16);
#endif
		int capacity = LZ4F_compressBound(z->total_size, NULL);
		assert(capacity >= (ptrdiff_t)vy_buf_used(dest));
		rc = vy_buf_ensure(dest, capacity);
		if (unlikely(rc == -1))
			return -1;
		size_t sz = LZ4F_compressEnd(z->compress, dest->p,
					     vy_buf_unused(dest), NULL);
		if (unlikely(LZ4F_isError(sz)))
			return -1;
		vy_buf_advance(dest, sz);
		break;
	case VINYL_FOUTPUT:
		/* do nothing */
		break;
	}
	return 0;
}

static struct vy_filterif vy_filterif_lz4 =
{
	.name     = "lz4",
	.create   = vy_filter_lz4_create,
	.destroy  = vy_filter_lz4_destroy,
	.start    = vy_filter_lz4_start,
	.next     = vy_filter_lz4_next,
	.complete = vy_filter_lz4_complete
};

static struct vy_quota *
vy_quota_new(int64_t limit)
{
	struct vy_quota *q = malloc(sizeof(*q));
	if (q == NULL) {
		diag_set(OutOfMemory, sizeof(*q), "quota", "struct");
		return NULL;
	}
	q->enable = false;
	q->limit  = limit;
	q->used   = 0;
	ipc_cond_create(&q->cond);
	return q;
}

static int
vy_quota_delete(struct vy_quota *q)
{
	ipc_cond_broadcast(&q->cond);
	ipc_cond_destroy(&q->cond);
	free(q);
	return 0;
}

static void
vy_quota_enable(struct vy_quota *q)
{
	q->enable = true;
}

static void
vy_quota_use(struct vy_quota *q, int64_t size)
{
	if (size == 0)
		return;
	while (q->enable && q->used + size >= q->limit)
		ipc_cond_wait(&q->cond);
	q->used += size;
}

static void
vy_quota_release(struct vy_quota *q, int64_t size)
{
	q->used -= size;
	if (q->used < q->limit)
		ipc_cond_broadcast(&q->cond);
}

static int
path_exists(const char *path)
{
	struct stat st;
	int rc = lstat(path, &st);
	return rc == 0;
}
struct vy_filter_zstd {
	void *ctx;
};

static int
vy_filter_zstd_create(struct vy_filter *f, va_list args)
{
	(void) args;
	struct vy_filter_zstd *z = (struct vy_filter_zstd*)f->priv;
	switch (f->op) {
	case VINYL_FINPUT:
		z->ctx = ZSTD_createCCtx();
		if (unlikely(z->ctx == NULL))
			return -1;
		break;
	case VINYL_FOUTPUT:
		z->ctx = NULL;
		break;
	}
	return 0;
}

static int
vy_filter_zstd_destroy(struct vy_filter *f)
{
	struct vy_filter_zstd *z = (struct vy_filter_zstd*)f->priv;
	switch (f->op) {
	case VINYL_FINPUT:
		ZSTD_freeCCtx(z->ctx);
		break;
	case VINYL_FOUTPUT:
		break;
	}
	return 0;
}

static int
vy_filter_zstd_start(struct vy_filter *f, struct vy_buf *dest)
{
	(void)dest;
	struct vy_filter_zstd *z = (struct vy_filter_zstd*)f->priv;
	size_t sz;
	switch (f->op) {
	case VINYL_FINPUT:;
		int compressionLevel = 3; /* fast */
		sz = ZSTD_compressBegin(z->ctx, compressionLevel);
		if (unlikely(ZSTD_isError(sz)))
			return -1;
		break;
	case VINYL_FOUTPUT:
		/* do nothing */
		break;
	}
	return 0;
}

static int
vy_filter_zstd_next(struct vy_filter *f, struct vy_buf *dest, char *buf, int size)
{
	struct vy_filter_zstd *z = (struct vy_filter_zstd*)f->priv;
	int rc;
	if (unlikely(size == 0))
		return 0;
	switch (f->op) {
	case VINYL_FINPUT:;
		size_t block = ZSTD_compressBound(size);
		rc = vy_buf_ensure(dest, block);
		if (unlikely(rc == -1))
			return -1;
		size_t sz = ZSTD_compressContinue(z->ctx, dest->p, block, buf, size);
		if (unlikely(ZSTD_isError(sz)))
			return -1;
		vy_buf_advance(dest, sz);
		break;
	case VINYL_FOUTPUT:
		/* do a single-pass decompression.
		 *
		 * Assume that destination buffer is allocated to
		 * original size.
		 */
		sz = ZSTD_decompress(dest->p, vy_buf_unused(dest), buf, size);
		if (unlikely(ZSTD_isError(sz)))
			return -1;
		break;
	}
	return 0;
}

static int
vy_filter_zstd_complete(struct vy_filter *f, struct vy_buf *dest)
{
	struct vy_filter_zstd *z = (struct vy_filter_zstd*)f->priv;
	int rc;
	switch (f->op) {
	case VINYL_FINPUT:;
		size_t block = ZSTD_compressBound(0);
		rc = vy_buf_ensure(dest, block);
		if (unlikely(rc == -1))
			return -1;
		size_t sz = ZSTD_compressEnd(z->ctx, dest->p, block);
		if (unlikely(ZSTD_isError(sz)))
			return -1;
		vy_buf_advance(dest, sz);
		break;
	case VINYL_FOUTPUT:
		/* do nothing */
		break;
	}
	return 0;
}

static struct vy_filterif vy_filterif_zstd =
{
	.name     = "zstd",
	.create   = vy_filter_zstd_create,
	.destroy  = vy_filter_zstd_destroy,
	.start    = vy_filter_zstd_start,
	.next     = vy_filter_zstd_next,
	.complete = vy_filter_zstd_complete
};

#define vy_e(type, fmt, ...) \
	({int res = -1;\
	  char errmsg[256];\
	  snprintf(errmsg, sizeof(errmsg), fmt, __VA_ARGS__);\
	  diag_set(ClientError, type, errmsg);\
	  res;})

#define vy_error(fmt, ...) \
	vy_e(ER_VINYL, fmt, __VA_ARGS__)

struct vy_stat {
	/* get */
	uint64_t get;
	struct vy_avg    get_read_disk;
	struct vy_avg    get_read_cache;
	struct vy_avg    get_latency;
	/* write */
	uint64_t write_count;
	/* transaction */
	uint64_t tx;
	uint64_t tx_rlb;
	uint64_t tx_conflict;
	struct vy_avg    tx_latency;
	struct vy_avg    tx_stmts;
	/* cursor */
	uint64_t cursor;
	struct vy_avg    cursor_latency;
	struct vy_avg    cursor_ops;
};

static inline struct vy_stat *
vy_stat_new()
{
	struct vy_stat *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		diag_set(OutOfMemory, sizeof(*s), "stat", "struct");
		return NULL;
	}
	return s;
}

static inline void
vy_stat_delete(struct vy_stat *s)
{
	free(s);
}

static inline void
vy_stat_prepare(struct vy_stat *s)
{
	vy_avg_prepare(&s->get_read_disk);
	vy_avg_prepare(&s->get_read_cache);
	vy_avg_prepare(&s->get_latency);
	vy_avg_prepare(&s->tx_latency);
	vy_avg_prepare(&s->tx_stmts);
	vy_avg_prepare(&s->cursor_latency);
	vy_avg_prepare(&s->cursor_ops);
}

struct vy_stat_get {
	int read_disk;
	int read_cache;
	uint64_t read_latency;
};

static inline void
vy_stat_get(struct vy_stat *s, const struct vy_stat_get *statget)
{
	s->get++;
	vy_avg_update(&s->get_read_disk, statget->read_disk);
	vy_avg_update(&s->get_read_cache, statget->read_cache);
	vy_avg_update(&s->get_latency, statget->read_latency);
}

static inline void
vy_stat_tx(struct vy_stat *s, uint64_t start, uint32_t count,
	   uint32_t write_count, bool is_rollback)
{
	uint64_t diff = clock_monotonic64() - start;
	s->tx++;
	if (is_rollback)
		s->tx_rlb++;
	s->write_count += write_count;
	vy_avg_update(&s->tx_stmts, count);
	vy_avg_update(&s->tx_latency, diff);
}

static inline void
vy_stat_cursor(struct vy_stat *s, uint64_t start, int ops)
{
	uint64_t diff = clock_monotonic64() - start;
	s->cursor++;
	vy_avg_update(&s->cursor_latency, diff);
	vy_avg_update(&s->cursor_ops, ops);
}

struct srzone {
	char     name[4];
	uint32_t compact_wm;
	uint32_t dump_prio;
	uint32_t dump_age;
};

struct srzonemap {
	struct srzone zones[11];
};

static inline void
sr_zonemap_set(struct srzonemap *m, uint32_t percent, struct srzone *z)
{
	if (unlikely(percent > 100))
		percent = 100;
	percent = percent - percent % 10;
	int p = percent / 10;
	m->zones[p] = *z;
	snprintf(m->zones[p].name, sizeof(m->zones[p].name), "%d", percent);
}

static inline struct srzone*
sr_zonemap(struct srzonemap *m, uint32_t percent)
{
	if (unlikely(percent > 100))
		percent = 100;
	percent = percent - percent % 10;
	int p = percent / 10;
	struct srzone *z = &m->zones[p];
	return z;
}

/** There was a backend read. This flag is additive. */
#define SVGET        1
/**
 * The last write operation on the tuple was REPLACE.
 * This flag resets other write flags.
 */
#define SVREPLACE    2
/**
 * The last write operation on the tuple was DELETE.
 * This flag resets other write flags.
 */
#define SVDELETE     4
/**
 * The last write operation on the tuple was UPSERT.
 * This flag resets other write flags.
 */
#define SVUPSERT     8
#define SVDUP        16

struct vy_tuple {
	int64_t  lsn;
	uint32_t size;
	uint16_t refs; /* atomic */
	uint8_t  flags;
	char data[0];
};

static inline uint32_t
vy_tuple_size(struct vy_tuple *v);

static struct vy_tuple *
vy_tuple_alloc(uint32_t size);

static inline const char *
vy_tuple_key_part(const char *tuple_data, uint32_t part_id);

static inline int
vy_tuple_compare(const char *tuple_data_a, const char *tuple_data_b,
		 const struct key_def *key_def);

static struct vy_tuple *
vy_tuple_from_key(struct vy_index *index, const char *key,
			  uint32_t part_count);
static void
vy_tuple_ref(struct vy_tuple *tuple);

static void
vy_tuple_unref(struct vy_tuple *tuple);

/** The tuple, while present in the transaction log, doesn't exist. */
static bool
vy_tuple_is_not_found(struct vy_tuple *tuple)
{
	return tuple->flags & SVDELETE;
}

static struct vy_tuple *
vy_tuple_extract_key_raw(struct vy_index *index, const char *tuple);

struct PACKED svmergesrc {
	struct vy_iter *i;
	struct vy_iter src;
	uint8_t dup;
	void *ptr;
};

struct svmerge {
	struct vy_index *index;
	struct key_def *key_def; /* TODO: use index->key_def when possible */
	struct vy_buf buf;
};

static inline void
sv_mergeinit(struct svmerge *m, struct vy_index *index,
	     struct key_def *key_def)
{
	vy_buf_create(&m->buf);
	m->index = index;
	m->key_def = key_def;
}

static inline int
sv_mergeprepare(struct svmerge *m, int count)
{
	return vy_buf_ensure(&m->buf, sizeof(struct svmergesrc) * count);
}

static inline void
sv_mergefree(struct svmerge *m)
{
	struct svmergesrc *beg = (struct svmergesrc *)m->buf.s;
	struct svmergesrc *end = (struct svmergesrc *)m->buf.p;
	for (struct svmergesrc *src = beg; src != end; ++src)
		src->i->vif->close(src->i);
	vy_buf_destroy(&m->buf);
}

static inline void
sv_mergereset(struct svmerge *m)
{
	struct svmergesrc *beg = (struct svmergesrc *)m->buf.s;
	struct svmergesrc *end = (struct svmergesrc *)m->buf.p;
	for (struct svmergesrc *src = beg; src != end; ++src)
		src->i->vif->close(src->i);
	m->buf.p = m->buf.s;
}

static inline struct svmergesrc*
sv_mergeadd(struct svmerge *m, struct vy_iter *i)
{
	assert(m->buf.p < m->buf.e);
	struct svmergesrc *s = (struct svmergesrc*)m->buf.p;
	s->dup = 0;
	s->i = i;
	s->ptr = NULL;
	if (i == NULL)
		s->i = &s->src;
	vy_buf_advance(&m->buf, sizeof(struct svmergesrc));
	return s;
}

/*
 * Merge several sorted streams into one.
 * Track duplicates.
 *
 * Merger does not recognize duplicates from
 * a single stream, assumed that they are tracked
 * by the incoming data sources.
*/

struct svmergeiter {
	enum vy_order order;
	struct svmerge *merge;
	struct svmergesrc *src, *end;
	struct svmergesrc *v;
};

static inline void
sv_mergeiter_dupreset(struct svmergeiter *i, struct svmergesrc *pos)
{
	for (struct svmergesrc *src = i->src; src != pos; src++)
		src->dup = 0;
}

static inline void
sv_mergeiter_next(struct svmergeiter *im)
{
	int direction = 0;
	switch (im->order) {
	case VINYL_GT:
	case VINYL_GE:
		direction = 1;
		break;
	case VINYL_LT:
	case VINYL_LE:
		direction = -1;
		break;
	default: unreachable();
	}

	if (im->v) {
		im->v->dup = 0;
		vy_iter_next(im->v->i);
	}
	im->v = NULL;
	struct svmergesrc *found_src = NULL;
	struct vy_tuple *found_val = NULL;
	for (struct svmergesrc *src = im->src; src < im->end; src++)
	{
		struct vy_tuple *v = vy_iter_get(src->i);
		if (v == NULL)
			continue;
		if (found_src == NULL) {
			found_val = v;
			found_src = src;
			continue;
		}
		int rc;
		rc = vy_tuple_compare(found_val->data, v->data,
				      im->merge->key_def);
		if (rc == 0) {
			/*
			assert(sv_lsn(v) < sv_lsn(maxv));
			*/
			src->dup = 1;
		} else if (direction * rc > 0) {
			sv_mergeiter_dupreset(im, src);
			found_val = v;
			found_src = src;
		}
	}
	if (unlikely(found_src == NULL))
		return;
	im->v = found_src;
}

static inline int
sv_mergeiter_open(struct svmergeiter *im, struct svmerge *m, enum vy_order o)
{
	im->merge = m;
	im->order = o;
	im->src   = (struct svmergesrc*)(im->merge->buf.s);
	im->end   = (struct svmergesrc*)(im->merge->buf.p);
	im->v     = NULL;
	sv_mergeiter_next(im);
	return 0;
}

static inline int
sv_mergeiter_has(struct svmergeiter *im)
{
	return im->v != NULL;
}

static inline struct vy_tuple *
sv_mergeiter_get(struct svmergeiter *im)
{
	if (unlikely(im->v == NULL))
		return NULL;
	return vy_iter_get(im->v->i);
}

static inline uint32_t
sv_mergeisdup(struct svmergeiter *im)
{
	assert(im->v != NULL);
	if (im->v->dup)
		return SVDUP;
	return 0;
}

struct svreaditer {
	struct svmergeiter *merge;
	int64_t vlsn;
	int next;
	int nextdup;
	int save_delete;
	struct vy_tuple *v;
	struct vy_tuple *upsert_tuple;
};

static struct vy_tuple *
vy_apply_upsert(struct vy_tuple *upsert, struct vy_tuple *object,
		struct vy_index *index, bool suppress_error);

static inline int
sv_readiter_upsert(struct svreaditer *i)
{
	assert(i->upsert_tuple == NULL);
	struct vy_index *index = i->merge->merge->index;
	/* upsert begin */
	struct vy_tuple *v = sv_mergeiter_get(i->merge);
	assert(v != NULL);
	assert(v->flags & SVUPSERT);
	i->upsert_tuple = vy_tuple_alloc(v->size);
	i->upsert_tuple->flags = SVUPSERT;
	memcpy(i->upsert_tuple->data, v->data, v->size);
	v = i->upsert_tuple;

	sv_mergeiter_next(i->merge);
	/* iterate over upsert statements */
	int skip = 0;
	for (; sv_mergeiter_has(i->merge); sv_mergeiter_next(i->merge))
	{
		struct vy_tuple *next_v = sv_mergeiter_get(i->merge);
		int dup = next_v->flags & SVDUP || sv_mergeisdup(i->merge);
		if (! dup)
			break;
		if (skip)
			continue;
		struct vy_tuple *up = vy_apply_upsert(v, next_v, index, true);
		if (up == NULL)
			return -1; /* memory error */
		vy_tuple_unref(i->upsert_tuple);
		i->upsert_tuple = up;
		v = i->upsert_tuple;
		if (! (next_v->flags & SVUPSERT))
			skip = 1;
	}
	if (v->flags & SVUPSERT) {
		struct vy_tuple *up = vy_apply_upsert(v, NULL, index, true);
		if (up == NULL)
			return -1; /* memory error */
		vy_tuple_unref(i->upsert_tuple);
		i->upsert_tuple = up;
		v = i->upsert_tuple;
	}
	return 0;
}

static inline void
sv_readiter_next(struct svreaditer *im)
{
	if (im->upsert_tuple != NULL) {
		vy_tuple_unref(im->upsert_tuple);
		im->upsert_tuple = NULL;
	}
	if (im->next)
		sv_mergeiter_next(im->merge);
	im->next = 0;
	im->v = NULL;
	for (; sv_mergeiter_has(im->merge); sv_mergeiter_next(im->merge))
	{
		struct vy_tuple *v = sv_mergeiter_get(im->merge);
		int dup = v->flags & SVDUP || sv_mergeisdup(im->merge);
		if (im->nextdup) {
			if (dup)
				continue;
			else
				im->nextdup = 0;
		}
		/* skip version out of visible range */
		if (v->lsn > im->vlsn) {
			continue;
		}
		im->nextdup = 1;
		if (!im->save_delete && v->flags & SVDELETE)
			continue;
		if (v->flags & SVUPSERT) {
			int rc = sv_readiter_upsert(im);
			if (unlikely(rc == -1))
				return;
			im->v = im->upsert_tuple;
			im->next = 0;
		} else {
			im->v = v;
			im->next = 1;
		}
		break;
	}
}

static inline void
sv_readiter_forward(struct svreaditer *im)
{
	if (im->next)
		sv_mergeiter_next(im->merge);
	im->next = 0;
	im->v = NULL;
	for (; sv_mergeiter_has(im->merge); sv_mergeiter_next(im->merge))
	{
		struct vy_tuple *v = sv_mergeiter_get(im->merge);
		int dup = v->flags & SVDUP || sv_mergeisdup(im->merge);
		if (dup)
			continue;
		im->next = 0;
		im->v = v;
		break;
	}
}

static inline int
sv_readiter_open(struct svreaditer *im, struct svmergeiter *merge,
		 int64_t vlsn, int save_delete)
{
	im->merge = merge;
	im->vlsn  = vlsn;
	im->v = NULL;
	im->next = 0;
	im->nextdup = 0;
	im->save_delete = save_delete;
	im->upsert_tuple = NULL;
	/* iteration can start from duplicate */
	sv_readiter_next(im);
	return 0;
}

static inline void
sv_readiter_close(struct svreaditer *im)
{
	if (im->upsert_tuple != NULL) {
		vy_tuple_unref(im->upsert_tuple);
		im->upsert_tuple = NULL;
	}
}

static inline struct vy_tuple *
sv_readiter_get(struct svreaditer *im)
{
	return im->v;
}

struct svwriteiter {
	int64_t   vlsn;
	int       save_delete;
	int       save_upsert;
	int       next;
	int       upsert;
	int64_t   prevlsn;
	int       vdup;
	struct vy_tuple *v;
	struct svmergeiter   *merge;
	struct vy_tuple *upsert_tuple;
};

static inline int
sv_writeiter_upsert(struct svwriteiter *i)
{
	assert(i->upsert_tuple == NULL);
	/* upsert begin */
	struct vy_index *index = i->merge->merge->index;
	struct vy_tuple *v = sv_mergeiter_get(i->merge);
	assert(v != NULL);
	assert(v->flags & SVUPSERT);
	assert(v->lsn <= i->vlsn);
	i->upsert_tuple = vy_tuple_alloc(v->size);
	i->upsert_tuple->flags = SVUPSERT;
	memcpy(i->upsert_tuple->data, v->data, v->size);
	v = i->upsert_tuple;
	sv_mergeiter_next(i->merge);

	/* iterate over upsert statements */
	int last_non_upd = 0;
	for (; sv_mergeiter_has(i->merge); sv_mergeiter_next(i->merge))
	{
		struct vy_tuple *next_v = sv_mergeiter_get(i->merge);
		int flags = next_v->flags;
		int dup = flags & SVDUP || sv_mergeisdup(i->merge);
		if (! dup)
			break;
		/* stop forming upserts on a second non-upsert stmt,
		 * but continue to iterate stream */
		if (last_non_upd)
			continue;
		last_non_upd = ! (flags & SVUPSERT);

		struct vy_tuple *up = vy_apply_upsert(v, next_v, index, false);
		if (up == NULL)
			return -1; /* memory error */
		vy_tuple_unref(i->upsert_tuple);
		i->upsert_tuple = up;
		v = i->upsert_tuple;
	}
	if (v->flags & SVUPSERT) {
		struct vy_tuple *up = vy_apply_upsert(v, NULL, index, false);
		if (up == NULL)
			return -1; /* memory error */
		vy_tuple_unref(i->upsert_tuple);
		i->upsert_tuple = up;
		v = i->upsert_tuple;
	}
	return 0;
}

static inline void
sv_writeiter_next(struct svwriteiter *im)
{
	if (im->upsert_tuple != NULL) {
		vy_tuple_unref(im->upsert_tuple);
		im->upsert_tuple = NULL;
	}
	if (im->next)
		sv_mergeiter_next(im->merge);
	im->next = 0;
	im->v = NULL;
	im->vdup = 0;

	for (; sv_mergeiter_has(im->merge); sv_mergeiter_next(im->merge))
	{
		struct vy_tuple *v = sv_mergeiter_get(im->merge);
		int64_t lsn = v->lsn;
		int flags = v->flags;
		int dup = flags & SVDUP || sv_mergeisdup(im->merge);

		if (unlikely(dup)) {
			/* keep atleast one visible version for <= vlsn */
			if (im->prevlsn <= im->vlsn) {
				if (im->upsert) {
					im->upsert = flags & SVUPSERT;
				} else {
					continue;
				}
			}
		} else {
			im->upsert = 0;
			/* delete (stray or on the run) */
			if (! im->save_delete) {
				int del = flags & SVDELETE;
				if (unlikely(del && (lsn <= im->vlsn))) {
					im->prevlsn = lsn;
					continue;
				}
			}
			/* upsert (track first statement start) */
			if (flags & SVUPSERT)
				im->upsert = 1;
		}

		/* upsert */
		if (flags & SVUPSERT) {
			if (! im->save_upsert) {
				if (lsn <= im->vlsn) {
					int rc;
					rc = sv_writeiter_upsert(im);
					if (unlikely(rc == -1))
						return;
					im->upsert = 0;
					im->prevlsn = lsn;
					im->v = im->upsert_tuple;
					im->vdup = dup;
					im->next = 0;
					break;
				}
			}
		}

		im->prevlsn = lsn;
		im->v = v;
		im->vdup = dup;
		im->next = 1;
		break;
	}
}

static inline int
sv_writeiter_open(struct svwriteiter *im, struct svmergeiter *merge,
		  int64_t vlsn, int save_delete,
		  int save_upsert)
{
	im->upsert_tuple = NULL;
	im->merge       = merge;
	im->vlsn        = vlsn;
	im->save_delete = save_delete;
	im->save_upsert = save_upsert;
	im->next  = 0;
	im->prevlsn  = 0;
	im->v = NULL;
	im->vdup = 0;
	im->upsert = 0;
	sv_writeiter_next(im);
	return 0;
}

static inline void
sv_writeiter_close(struct svwriteiter *im)
{
	if (im->upsert_tuple != NULL) {
		vy_tuple_unref(im->upsert_tuple);
		im->upsert_tuple = NULL;
	}
}

static inline int
sv_writeiter_has(struct svwriteiter *im)
{
	return im->v != NULL;
}

static inline struct vy_tuple *
sv_writeiter_get(struct svwriteiter *im)
{
	return im->v;
}

static inline int
sv_writeiter_is_duplicate(struct svwriteiter *im)
{
	assert(im->v != NULL);
	return im->vdup;
}

struct tree_mem_key {
	char *data;
	int64_t lsn;
};

struct vy_mem;

int
vy_mem_tree_cmp(struct vy_tuple *a, struct vy_tuple *b, struct vy_mem *index);

int
vy_mem_tree_cmp_key(struct vy_tuple *a, struct tree_mem_key *key,
			 struct vy_mem *index);

#define BPS_TREE_MEM_INDEX_PAGE_SIZE (16 * 1024)
#define BPS_TREE_NAME vy_mem_tree
#define BPS_TREE_BLOCK_SIZE 512
#define BPS_TREE_EXTENT_SIZE BPS_TREE_MEM_INDEX_PAGE_SIZE
#define BPS_TREE_COMPARE(a, b, index) vy_mem_tree_cmp(a, b, index)
#define BPS_TREE_COMPARE_KEY(a, b, index) vy_mem_tree_cmp_key(a, b, index)
#define bps_tree_elem_t struct vy_tuple *
#define bps_tree_key_t struct tree_mem_key *
#define bps_tree_arg_t struct vy_mem *
#define BPS_TREE_NO_DEBUG

#include "salad/bps_tree.h"

/*
 * vy_mem is an in-memory container for vy_tuple objects in
 * a single vinyl range.
 * Internally it uses bps_tree to stores struct vy_tuple *objects.
 * which are ordered by tuple key and, for the same key,
 * by lsn, in descending order.
 *
 * For example, assume there are two tuples with the same key,
 * but different LSN. These are duplicates of the same key,
 * maintained for the purpose of MVCC/consistent read view.
 * In Vinyl terms, they form a duplicate chain.
 *
 * vy_mem distinguishes between the first duplicate in the chain
 * and other keys in that chain.
 *
 * During insertion, the reference counter of vy_tuple is
 * incremented, during destruction all vy_tuple' reference
 * counters are decremented.
 */
struct vy_mem {
	struct vy_mem_tree tree;
	uint32_t used;
	int64_t min_lsn;
	struct key_def *key_def;
	/** version is initially 0 and is incremented on every write */
	uint32_t version;
};

int
vy_mem_tree_cmp(struct vy_tuple *a, struct vy_tuple *b, struct vy_mem *index)
{
	int res = vy_tuple_compare(a->data, b->data, index->key_def);
	res = res ? res : a->lsn > b->lsn ? -1 : a->lsn < b->lsn;
	return res;
}

int
vy_mem_tree_cmp_key(struct vy_tuple *a, struct tree_mem_key *key,
			 struct vy_mem *index)
{
	int res = vy_tuple_compare(a->data, key->data, index->key_def);
	if (res == 0) {
		if (key->lsn == INT64_MAX - 1)
			return 0;
		res = a->lsn > key->lsn ? -1 : a->lsn < key->lsn;
	}
	return res;
}

void *
vy_mem_alloc_matras_page()
{
	void *res = mmap(0, BPS_TREE_MEM_INDEX_PAGE_SIZE, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (res == MAP_FAILED) {
		diag_set(OutOfMemory, BPS_TREE_MEM_INDEX_PAGE_SIZE,
			 "mmap", "vinyl matras page");
	}
	return res;
}

void
vy_mem_free_matras_page(void *p)
{
	munmap(p, BPS_TREE_MEM_INDEX_PAGE_SIZE);
}

static int
vy_mem_create(struct vy_mem *index, struct key_def *key_def)
{
	index->min_lsn = INT64_MAX;
	index->used = 0;
	index->key_def = key_def;
	index->version = 0;
	vy_mem_tree_create(&index->tree, index,
			   vy_mem_alloc_matras_page,
			   vy_mem_free_matras_page);
	return 0;
}

static int
vy_mem_destroy(struct vy_mem *index)
{
	assert(index == index->tree.arg);
	struct vy_mem_tree_iterator itr = vy_mem_tree_itr_first(&index->tree);
	while (!vy_mem_tree_itr_is_invalid(&itr)) {
		struct vy_tuple *v =
			*vy_mem_tree_itr_get_elem(&index->tree, &itr);
		vy_tuple_unref(v);
		vy_mem_tree_itr_next(&index->tree, &itr);
	}
	vy_mem_tree_destroy(&index->tree);
	return 0;
}

static inline int
vy_mem_set(struct vy_mem *index, struct vy_tuple *v)
{
	/* see struct vy_mem comments */
	assert(index == index->tree.arg);
	if (vy_mem_tree_insert(&index->tree, v, NULL) != 0)
		return -1;
	index->version++;
	/* sic: sync this value with vy_range->used */
	index->used += vy_tuple_size(v);
	if (index->min_lsn > v->lsn)
		index->min_lsn = v->lsn;
	return 0;
}

static int
vy_mem_gc(struct vy_mem *i)
{
	vy_mem_destroy(i);
	vy_mem_create(i, i->key_def);
	return 0;
}

/**
 * The footprint of run metadata on disk.
 * Run metadata is a set of packed data structures which are
 * written to disk in host byte order. They describe the
 * format of the run itself, which is a collection of
 * equi-sized, aligned pages with tuples.
 *
 * This footprint is the first thing written to disk
 * when a run is dumped. It is a way to achieve
 * backward compatibility when restoring runs written
 * by previous versions of tarantool: it is assumed that
 * the data structures will get new members, which will
 * be stored at their end, and we'll be able to check
 * for absent members by looking at this footprint record.
 */
struct PACKED vy_run_footprint {
	/** Size of struct vy_run_info */
	uint16_t run_info_size;
	/* Size of struct vy_page_info */
	uint16_t page_info_size;
	/* Size struct vy_tuple_info */
	uint16_t tuple_info_size;
	/* Data alignment */
	uint16_t alignment;
};

/**
 * Run metadata. A run is a written to a file as a single
 * chunk.
 */
struct PACKED vy_run_info {
	/* sizes of containig structures */
	struct vy_run_footprint footprint;
	uint32_t  crc;
	/** Total run size when stored in a file. */
	uint64_t size;
	/** Offset of the run in the file */
	uint64_t offset;
	/** Run page count. */
	uint32_t  count;
	/** Size of the page index. */
	uint32_t pages_size;
	/** Offset of this run's page index in the file. */
	uint64_t  pages_offset;
	/** size of min-max data block */
	uint32_t  minmax_size;
	/** start of min-max keys array (global) */
	uint64_t  minmax_offset;
	/** Number of keys in the min-max key array. */
	uint32_t  keys;
	/* Min and max lsn over all tuples in the run. */
	int64_t  min_lsn;
	int64_t  max_lsn;

	uint64_t  total;
	uint64_t  totalorigin;
};

struct PACKED vy_page_info {
	uint32_t crc;
	/* offset of page data in run */
	uint64_t offset;
	/* size of page data in file */
	uint32_t size;
	/* size of page data in memory, i.e. unpacked */
	uint32_t unpacked_size;
	/* offset of page's min key in page index key storage */
	uint32_t min_key_offset;
	/* offset of page's max key in page index key storage */
	uint32_t max_key_offset;
	/* lsn of min key in page */
	int64_t min_key_lsn;
	/* lsn of max key in page */
	int64_t max_key_lsn;
	/* minimal lsn of all records in page */
	int64_t min_lsn;
	/* maximal lsn of all records in page */
	int64_t max_lsn;
	/* count of records */
	uint32_t count;
};

struct PACKED vy_tuple_info {
	/* record lsn */
	int64_t lsn;
	/* offset in data block */
	uint32_t offset;
	/* size of tuple */
	uint32_t size;
	/* flags */
	uint8_t  flags;
	/* for 4-byte alignment */
	uint8_t reserved[3];
};


struct vy_run_index {
	struct vy_run_info info;
	struct vy_buf pages, minmax;
};

struct PACKED vy_run {
	struct vy_run_index index;
	struct vy_run *next;
	struct vy_page *page_cache;
	pthread_mutex_t cache_lock;
};

struct PACKED vy_range {
	int64_t   id;
	struct vy_tuple *min_key;
	uint16_t   flags;
	uint64_t   update_time;
	uint32_t   used; /* sum of i0->used + i1->used */
	struct vy_run  *run;
	uint32_t   run_count;
	uint32_t   temperature;
	uint64_t   temperature_reads;
	struct vy_mem    i0, i1;
	/** The file where the run is stored or -1 if it's not dumped yet. */
	int fd;
	char path[PATH_MAX];
	rb_node(struct vy_range) tree_node;
	struct ssrqnode   nodecompact;
	struct ssrqnode   nodedump;
	struct rlist     split;
	uint32_t range_version;
};

typedef rb_tree(struct vy_range) vy_range_tree_t;

struct vy_profiler {
	uint32_t  total_range_count;
	uint64_t  total_range_size;
	uint64_t  total_range_origin_size;
	uint32_t  total_run_count;
	uint32_t  total_run_avg;
	uint32_t  total_run_max;
	uint32_t  total_page_count;
	uint64_t  total_snapshot_size;
	uint32_t  temperature_avg;
	uint32_t  temperature_min;
	uint32_t  temperature_max;
	uint64_t  memory_used;
	uint64_t  count;
	uint64_t  count_dup;
	uint64_t  read_disk;
	uint64_t  read_cache;
	int       histogram_run[20];
	int       histogram_run_20plus;
	char      histogram_run_sz[256];
	char     *histogram_run_ptr;
	struct vy_index  *i;
};

struct vy_planner {
	struct ssrq dump;
	struct ssrq compact;
};

/**
 * A single operation made by a transaction:
 * a single read or write in a vy_index.
 */
struct txv {
	/** Transaction start logical time - used by conflict manager. */
	int64_t tsn;
	struct vy_index *index;
	struct vy_tuple *tuple;
	struct vy_tx *tx;
	/** Next in the transaction log. */
	struct stailq_entry next_in_log;
	/** Member of the transaction manager index. */
	rb_node(struct txv) in_read_set;
	/** Member of the transaction log index. */
	rb_node(struct txv) in_write_set;
	/** true for read tx, false for write tx */
	bool is_read;
};

typedef rb_tree(struct txv) read_set_t;

struct vy_index {
	struct vy_env *env;
	struct vy_profiler rtp;
	/**
	 * Conflict manager index. Contains all changes
	 * made by transaction before they commit. Is used
	 * to implement read committed isolation level, i.e.
	 * the changes made by a transaction are only present
	 * in this tree, and thus not seen by other transactions.
	 */
	read_set_t read_set;
	vy_range_tree_t tree;
	int range_count;
	uint64_t read_disk;
	uint64_t read_cache;
	uint64_t size;
	pthread_mutex_t ref_lock;
	uint32_t refs;
	/** A schematic name for profiler output. */
	char       *name;
	/** The path with index files. */
	char       *path;
	/** Compression filter. */
	struct vy_filterif *compression_if;
	struct key_def *key_def;
	struct tuple_format *tuple_format;
	uint32_t key_map_size; /* size of key_map map */
	uint32_t *key_map; /* field_id -> part_id map */
	/** Member of env->db or scheduler->shutdown. */
	struct rlist link;

	/* {{{ Scheduler members */
	struct vy_planner p;
	/* Scheduler members }}} */

	/**
	 * LSN from the time when the first index impression on
	 * disk was created. For a newly created (not
	 * checkpointed) index this should be the min LSN over
	 * records from this index stored on disk.  For
	 * checkpointed index this should be LSN of
	 * the checkpoint.
	 */
	int64_t first_dump_lsn;
	/*
	 * For each index range list modification,
	 * get a new range id and increment this variable.
	 * For new ranges, use this id as a sequence.
	 */
	int64_t range_id_max;
	/* The newest range id that was dumped to disk. */
	int64_t last_dump_range_id;

	uint32_t range_index_version;
};


/** Transaction state. */
enum tx_state {
	/** Initial state. */
	VINYL_TX_READY,
	/**
	 * A transaction is finished and validated in the engine.
	 * It may still be rolled back if there is an error
	 * writing the WAL.
	 */
	VINYL_TX_COMMIT,
	/** A transaction is aborted or rolled back. */
	VINYL_TX_ROLLBACK
};

/** Transaction type. */
enum tx_type {
	VINYL_TX_RO,
	VINYL_TX_RW
};

struct read_set_key {
	char *data;
	int size;
	int64_t tsn;
};

typedef rb_tree(struct txv) write_set_t;

struct vy_tx {
	/**
	 * In memory transaction log. Contains both reads
	 * and writes.
	 */
	struct stailq log;
	/**
	 * Writes of the transaction segregated by the changed
	 * vy_index object.
	 */
	write_set_t write_set;
	/**
	 * Version of write_set state; if the state changes (insert/remove),
	 * the version increments.
	 */
	uint32_t write_set_version;
	uint64_t start;
	enum tx_type     type;
	enum tx_state    state;
	bool is_aborted;
	/** Transaction logical start time. */
	int64_t tsn;
	/**
	 * Consistent read view LSN: the LSN recorded
	 * at start of transaction and used to implement
	 * transactional read view.
	 */
	int64_t   vlsn;
	rb_node(struct vy_tx) tree_node;
	/*
	 * For non-autocommit transactions, the list of open
	 * cursors. When a transaction ends, all open cursors are
	 * forcibly closed.
	 */
	struct rlist cursors;
	struct tx_manager *manager;
};

/** Cursor. */
struct vy_cursor {
	/**
	 * A built-in transaction created when a cursor is open
	 * in autocommit mode.
	 */
	struct vy_tx tx_autocommit;
	struct vy_index *index;
	struct vy_tuple *key;
	struct vy_tx *tx;
	enum vy_order order;
	/** The number of vy_cursor_next() invocations. */
	int n_reads;
	/**
	 * All open cursors are registered in a transaction
	 * they belong to. When the transaction ends, the cursor
	 * is closed.
	 */
	struct rlist next_in_tx;
};


static inline struct txv *
txv_new(struct vy_index *index, struct vy_tuple *tuple, struct vy_tx *tx)
{
	struct txv *v = malloc(sizeof(struct txv));
	if (unlikely(v == NULL)) {
		diag_set(OutOfMemory, sizeof(struct txv), "malloc",
			 "struct txv");
		return NULL;
	}
	v->index = index;
	v->tsn = tx->tsn;
	v->tuple = tuple;
	vy_tuple_ref(tuple);
	v->tx = tx;
	return v;
}

static inline void
txv_delete(struct txv *v)
{
	vy_tuple_unref(v->tuple);
	free(v);
}

static inline void
txv_abort(struct txv *v)
{
	v->tx->is_aborted = true;
}

static int
read_set_cmp(read_set_t *rbtree, struct txv *a, struct txv *b);

static int
read_set_key_cmp(read_set_t *rbtree, struct read_set_key *a, struct txv *b);

rb_gen_ext_key(, read_set_, read_set_t, struct txv, in_read_set, read_set_cmp,
		 struct read_set_key *, read_set_key_cmp);

static struct txv *
read_set_search_key(read_set_t *rbtree, char *data, int size, int64_t tsn)
{
	struct read_set_key key;
	key.data = data;
	key.size = size;
	key.tsn = tsn;
	return read_set_search(rbtree, &key);
}

static int
read_set_cmp(read_set_t *rbtree, struct txv *a, struct txv *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vy_index, read_set)->key_def;
	int rc = vy_tuple_compare(a->tuple->data, b->tuple->data, key_def);
	/**
	 * While in svindex older value are "bigger" than newer
	 * ones, i.e. the newest value comes first, in
	 * transactional index (read_set), we want to look
	 * at data in chronological order.
	 * @sa vy_mem_tree_cmp
	 */
	if (rc == 0)
		rc = a->tsn < b->tsn ? -1 : a->tsn > b->tsn;
	return rc;
}

static int
read_set_key_cmp(read_set_t *rbtree, struct read_set_key *a, struct txv *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vy_index, read_set)->key_def;
	int rc = vy_tuple_compare(a->data, b->tuple->data, key_def);
	if (rc == 0)
		rc = a->tsn < b->tsn ? -1 : a->tsn > b->tsn;
	return rc;
}

/**
 * Abort all transaction which are reading the tuple v written by
 * tx.
 */
static void
txv_abort_all(struct vy_tx *tx, struct txv *v)
{
	read_set_t *tree = &v->index->read_set;
	struct key_def *key_def = v->index->key_def;
	struct read_set_key key;
	key.data = v->tuple->data;
	key.size = v->tuple->size;
	key.tsn = 0;
	/** Find the first value equal to or greater than key */
	struct txv *abort = read_set_nsearch(tree, &key);
	while (abort) {
		/* Check if we're still looking at the matching key. */
		if (vy_tuple_compare(key.data, abort->tuple->data,
				     key_def))
			break;
		/* Don't abort self. */
		if (abort->tx != tx)
			txv_abort(abort);
		abort = read_set_next(tree, abort);
	}
}

static int
write_set_cmp(write_set_t *index, struct txv *a, struct txv *b)
{
	(void) index;
	/* Order by index first, by key in the index second. */
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0) {
		struct key_def *key_def = a->index->key_def;
		rc = vy_tuple_compare(a->tuple->data, b->tuple->data, key_def);
	}
	return rc;
}

struct write_set_key {
	struct vy_index *index;
	char *data;
};

static int
write_set_key_cmp(write_set_t *index, struct write_set_key *a, struct txv *b)
{
	(void) index;
	/* Order by index first, by key in the index second. */
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0) {
		if (a->data == NULL) {
			/*
			 * A special key to position search at the
			 * beginning of the index.
			 */
			return -1;
		}
		struct key_def *key_def = a->index->key_def;
		rc = vy_tuple_compare(a->data, b->tuple->data, key_def);
	}
	return rc;
}

rb_gen_ext_key(, write_set_, write_set_t, struct txv, in_write_set, write_set_cmp,
	       struct write_set_key *, write_set_key_cmp);

static struct txv *
write_set_search_key(write_set_t *tree, struct vy_index *index, char *data)
{
	struct write_set_key key = { .index = index, .data = data};
	return write_set_search(tree, &key);
}

bool
vy_tx_is_ro(struct vy_tx *tx)
{
	return tx->type == VINYL_TX_RO ||
		tx->write_set.rbt_root == &tx->write_set.rbt_nil;
}

typedef rb_tree(struct vy_tx) tx_tree_t;

static int
tx_tree_cmp(tx_tree_t *rbtree, struct vy_tx *a, struct vy_tx *b)
{
	(void)rbtree;
	return vy_cmp(a->tsn, b->tsn);
}

rb_gen(, tx_tree_, tx_tree_t, struct vy_tx, tree_node,
       tx_tree_cmp);

struct tx_manager {
	tx_tree_t tree;
	uint32_t    count_rd;
	uint32_t    count_rw;
	/** Transaction logical time. */
	int64_t tsn;
	/**
	 * The last committed log sequence number known to
	 * vinyl. Updated in vy_commit().
	 */
	int64_t lsn;
	/**
	 * View sequence number: the oldest read view maintained
	 * by the front end.
	 */
	int64_t vlsn;
	struct vy_env *env;
};

static struct tx_manager *
tx_manager_new(struct vy_env*);

static int
tx_manager_delete(struct tx_manager*);

static struct tx_manager *
tx_manager_new(struct vy_env *env)
{
	struct tx_manager *m = malloc(sizeof(*m));
	if (m == NULL) {
		diag_set(OutOfMemory, sizeof(*m), "tx_manager", "struct");
		return NULL;
	}
	tx_tree_new(&m->tree);
	m->count_rd = 0;
	m->count_rw = 0;
	m->tsn = 0;
	m->lsn = 0;
	m->env = env;
	return m;
}

static int
tx_manager_delete(struct tx_manager *m)
{
	free(m);
	return 0;
}

static struct txv *
read_set_delete_cb(read_set_t *t, struct txv *v, void *arg)
{
	(void) t;
	(void) arg;
	txv_delete(v);
	return NULL;
}

static void
vy_tx_begin(struct tx_manager *m, struct vy_tx *tx, enum tx_type type)
{
	stailq_create(&tx->log);
	write_set_new(&tx->write_set);
	tx->write_set_version = 0;
	tx->start = clock_monotonic64();
	tx->manager = m;
	tx->state = VINYL_TX_READY;
	tx->type = type;
	tx->is_aborted = false;
	rlist_create(&tx->cursors);

	tx->tsn = ++m->tsn;
	tx->vlsn = m->lsn;

	tx_tree_insert(&m->tree, tx);
	if (type == VINYL_TX_RO)
		m->count_rd++;
	else
		m->count_rw++;
}

/**
 * Remember the read in the conflict manager index.
 */
int
vy_tx_track(struct vy_tx *tx, struct vy_index *index,
	    struct vy_tuple *key)
{
	struct txv *v = read_set_search_key(&index->read_set, key->data,
					    key->size, tx->tsn);
	if (v == NULL) {
		if ((v = txv_new(index, key, tx)) == NULL)
			return -1;
		v->is_read = true;
		stailq_add_tail_entry(&tx->log, v, next_in_log);
		read_set_insert(&index->read_set, v);
	}
	return 0;
}

static inline void
tx_manager_end(struct tx_manager *m, struct vy_tx *tx)
{
	bool was_oldest = tx == tx_tree_first(&m->tree);
	tx_tree_remove(&m->tree, tx);
	if (tx->type == VINYL_TX_RO)
		m->count_rd--;
	else
		m->count_rw--;
	if (was_oldest) {
		struct vy_tx *oldest = tx_tree_first(&m->tree);
		m->vlsn = oldest ? oldest->vlsn : m->lsn;
	}
}

static void
vy_tx_rollback(struct vy_env *e, struct vy_tx *tx)
{
	if (tx->state != VINYL_TX_COMMIT) {
		/** Abort all open cursors. */
		struct vy_cursor *c;
		rlist_foreach_entry(c, &tx->cursors, next_in_tx)
			c->tx = NULL;

		tx_manager_end(tx->manager, tx);
	}
	struct txv *v, *tmp;
	uint32_t count = 0;
	stailq_foreach_entry_safe(v, tmp, &tx->log, next_in_log) {
		/* Remove from the conflict manager index */
		if (v->is_read)
			read_set_remove(&v->index->read_set, v);
		/* Don't touch write_set, we're deleting all keys. */
		txv_delete(v);
		count++;
	}
	vy_stat_tx(e->stat, tx->start, count, 0, true);
}

struct vy_page {
	struct vy_page_info *info;
	void *data;
	uint32_t refs;
};

static inline void
vy_page_init(struct vy_page *p, struct vy_page_info *info, char *data)
{
	p->info = info;
	p->data = data;
	p->refs = 1;
}

static inline struct vy_tuple_info*
sd_pagev(struct vy_page *p, uint32_t pos)
{
	assert(pos < p->info->count);
	return (struct vy_tuple_info*)(p->data + sizeof(struct vy_tuple_info) * pos);
}

static inline void*
sd_pagepointer(struct vy_page *p, struct vy_tuple_info *v)
{
	assert((sizeof(struct vy_tuple_info) * p->info->count) + v->offset <=
	       p->info->unpacked_size);
	return (p->data + sizeof(struct vy_tuple_info) * p->info->count)
	       + v->offset;
}

static inline char *
vy_run_index_min_key(struct vy_run_index *i, struct vy_page_info *p)
{
	return i->minmax.s + p->min_key_offset;
}

static inline char *
vy_run_index_max_key(struct vy_run_index *i, struct vy_page_info *p)
{
	return i->minmax.s + p->max_key_offset;
}

static inline void
vy_run_index_init(struct vy_run_index *i)
{
	vy_buf_create(&i->pages);
	vy_buf_create(&i->minmax);
	memset(&i->info, 0, sizeof(i->info));
}

static inline void
vy_run_index_destroy(struct vy_run_index *i)
{
	vy_buf_destroy(&i->pages);
	vy_buf_destroy(&i->minmax);
}

static inline struct vy_page_info *
vy_run_index_get_page(struct vy_run_index *i, int pos)
{
	assert(pos >= 0);
	assert((uint32_t)pos < i->info.count);
	return (struct vy_page_info *)
		vy_buf_at(&i->pages, sizeof(struct vy_page_info), pos);
}

static inline struct vy_page_info *
vy_run_index_first_page(struct vy_run_index *i)
{
	return vy_run_index_get_page(i, 0);
}

static inline struct vy_page_info *
vy_run_index_last_page(struct vy_run_index *i)
{
	return vy_run_index_get_page(i, i->info.count - 1);
}

static inline uint32_t
vy_run_index_count(struct vy_run_index *i)
{
	if (unlikely(i->pages.s == NULL))
		return 0;
	return i->info.keys;
}

static inline uint32_t
vy_run_index_total(struct vy_run_index *i)
{
	if (unlikely(i->pages.s == NULL))
		return 0;
	return i->info.total;
}

static inline uint32_t
vy_run_index_size(struct vy_run_index *i)
{
	return sizeof(i->info) +
	       i->info.count * sizeof(struct vy_page_info) +
	       i->info.minmax_size;
}

static int
vy_index_dump_range_index(struct vy_index *index);
static int
vy_index_checkpoint_range_index(struct vy_index *index, int64_t lsn);

static inline struct vy_run *
vy_run_new()
{
	struct vy_run *run = (struct vy_run *)malloc(sizeof(struct vy_run));
	if (unlikely(run == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_run), "malloc",
			 "struct vy_run");
		return NULL;
	}
	vy_run_index_init(&run->index);
	run->next = NULL;
	run->page_cache = NULL;
	pthread_mutex_init(&run->cache_lock, NULL);
	return run;
}

static inline void
vy_run_delete(struct vy_run *run)
{
	vy_run_index_destroy(&run->index);
	if (run->page_cache != NULL) {
		free(run->page_cache);
		run->page_cache = NULL;
	}
	pthread_mutex_destroy(&run->cache_lock);
	free(run);
}

#define FILE_ALIGN	512
#define ALIGN_POS(pos)	(pos + (FILE_ALIGN - (pos % FILE_ALIGN)) % FILE_ALIGN)

static ssize_t
vy_read_file(int fd, void *buf, uint32_t size)
{
	ssize_t pos = 0;
	while (pos < size) {
		ssize_t readen = read(fd, buf + pos, size - pos);
		if (readen < 0)
			return -1;
		if (!readen)
			break;
		pos += readen;
	}
	return pos;
}

static ssize_t
vy_pread_file(int fd, void *buf, uint32_t size, off_t offset)
{
	ssize_t pos = 0;
	while (pos < size) {
		ssize_t readen = pread(fd, buf + pos,
				       size - pos, offset + pos);
		if (readen < 0)
			return -1;
		if (!readen)
			break;
		pos += readen;
	}
	return pos;
}

static ssize_t
vy_read_aligned(int fd, void *buf, uint32_t *size)
{
	ssize_t readen;
	uint32_t old_size = *size;
	*size = ALIGN_POS(*size);
	if (old_size != *size || (intptr_t)buf % FILE_ALIGN) {
		void *ptr = buf;
		if (posix_memalign(&ptr, FILE_ALIGN, *size)) {
			diag_set(OutOfMemory, *size,
				 "posix_memalign", "aligned buf");
			return -1;
		}
		readen = vy_read_file(fd, ptr, *size);
		memcpy(buf, ptr, old_size);
		free(ptr);
	} else {
		readen = vy_read_file(fd, buf, *size);
	}
	if (readen == -1) {
		diag_set(ClientError, ER_VINYL, "Can't read file");
	}
	return readen;

}

static ssize_t
vy_pread_aligned(int fd, void *buf, uint32_t *size, off_t offset)
{
	ssize_t readen;
	uint32_t old_size = *size;
	*size = ALIGN_POS(*size);
	if (old_size != *size || (intptr_t)buf % FILE_ALIGN) {
		void *ptr = buf;
		if (posix_memalign(&ptr, FILE_ALIGN, *size)) {
			diag_set(OutOfMemory, *size,
				 "posix_memalign", "aligned buf");
			return -1;
		}
		readen = vy_pread_file(fd, ptr, *size, offset);
		memcpy(buf, ptr, old_size);
		free(ptr);
	} else {
		readen = vy_pread_file(fd, buf, *size, offset);
	}
	if (readen == -1) {
		diag_set(ClientError, ER_VINYL, "Can't read file");
	}
	return readen;

}

/**
 * Load from page with given number
 * If the page is loaded by somebody else, it's returned from cache
 * In every case increments page's reference counter
 * After usage user must call vy_run_unload_page
 */
static struct vy_page *
vy_run_load_page(struct vy_run *run, uint32_t pos,
		 int fd, struct vy_filterif *compression)
{
	pthread_mutex_lock(&run->cache_lock);
	if (run->page_cache == NULL) {
		run->page_cache = calloc(run->index.info.count,
					 sizeof(*run->page_cache));
		if (run->page_cache == NULL) {
			pthread_mutex_unlock(&run->cache_lock);
			diag_set(OutOfMemory,
				 run->index.info.count * sizeof (*run->page_cache),
				 "load_page", "page cache");
			return NULL;
		}
	}
	if (run->page_cache[pos].refs) {
		run->page_cache[pos].refs++;
		pthread_mutex_unlock(&run->cache_lock);
		return &run->page_cache[pos];
	}
	pthread_mutex_unlock(&run->cache_lock);
	struct vy_page_info *page_info = vy_run_index_get_page(&run->index, pos);
	uint32_t alloc_size = page_info->unpacked_size;
	if (page_info->size > page_info->unpacked_size)
		alloc_size = page_info->size;
	char *data = malloc(alloc_size);
	if (data == NULL) {
		diag_set(OutOfMemory, alloc_size, "load_page", "page cache");
		return NULL;
	}

#if 0
	int rc = coeio_pread(file->fd, data, page_info->size, page_info->offset);
#else
	int rc = vy_pread_aligned(fd, data, &page_info->size,
				  page_info->offset);
#endif

	if (rc < 0) {
		free(data);
		/* TODO: get file name from range */
		vy_error("index file read error: %s",
			 strerror(errno));
		return NULL;
	}

	if (compression != NULL) {
		/* decompression */
		struct vy_filter f;
		rc = vy_filter_create(&f, compression, VINYL_FOUTPUT);
		if (unlikely(rc == -1)) {
			vy_error("%s", "index file decompression error");
			free(data);
			return NULL;
		}
		struct vy_buf buf;
		vy_buf_create(&buf);
		rc = vy_filter_next(&f, &buf, data,
				    page_info->size);
		vy_filter_destroy(&f);
		if (unlikely(rc == -1)) {
			vy_error("%s", "index file decompression error");
			vy_buf_destroy(&buf);
			free(data);
			return NULL;
		}
		assert(vy_buf_size(&buf) == page_info->unpacked_size);
		memcpy(data, buf.s,
		       page_info->unpacked_size);
		vy_buf_destroy(&buf);
	}

	pthread_mutex_lock(&run->cache_lock);
	run->page_cache[pos].refs++;
	if (run->page_cache[pos].refs == 1)
		vy_page_init(&run->page_cache[pos], page_info, data);
	else
		free(data);
	pthread_mutex_unlock(&run->cache_lock);
	return &run->page_cache[pos];
}

/**
 * Get a page from cache
 * Page must be loaded with vy_run_load_page before the call
 */
static struct vy_page *
vy_run_get_page(struct vy_run *run, uint32_t pos)
{
	assert(run->page_cache != NULL);
	assert(run->page_cache[pos].refs > 0);
	return &run->page_cache[pos];
}

/**
 * Free page data
 * Actually decrements reference counter and frees data only there are no users
 */
static void
vy_run_unload_page(struct vy_run *run, uint32_t pos)
{
	assert(run->page_cache != NULL);
	assert(run->page_cache[pos].refs > 0);
	pthread_mutex_lock(&run->cache_lock);
	run->page_cache[pos].refs--;
	if (run->page_cache[pos].refs == 0) {
		free(run->page_cache[pos].data);
		run->page_cache[pos].data = NULL;
	}
	pthread_mutex_unlock(&run->cache_lock);
}

#define VY_LOCK       1
#define VY_ROTATE     2
#define VY_SPLIT      4

static struct vy_range *vy_range_new(struct key_def *key_def);
static int
vy_range_open(struct vy_index*, struct vy_range*, char *);
static int
vy_range_create(struct vy_range*, struct vy_index*);
static int vy_range_delete(struct vy_range*, int);
static int vy_range_complete(struct vy_range*, struct vy_index*);

static inline void
vy_range_lock(struct vy_range *range)
{
	assert(! (range->flags & VY_LOCK));
	range->flags |= VY_LOCK;
}

static inline void
vy_range_unlock(struct vy_range *range)
{
	assert((range->flags & VY_LOCK) > 0);
	range->flags &= ~VY_LOCK;
}

static inline struct vy_mem *
vy_range_rotate(struct vy_range *range)
{
	range->flags |= VY_ROTATE;
	return &range->i0;
}

static inline void
vy_range_unrotate(struct vy_range *range)
{
	assert((range->flags & VY_ROTATE) > 0);
	range->flags &= ~VY_ROTATE;
	range->i0 = range->i1;
	range->i0.tree.arg = &range->i0;
	vy_mem_create(&range->i1, range->i0.key_def);
}

static inline struct vy_mem *
vy_range_mem(struct vy_range *range)
{
	if (range->flags & VY_ROTATE)
		return &range->i1;
	return &range->i0;
}

static inline struct vy_mem *
vy_range_index_priority(struct vy_range *range, struct vy_mem **second)
{
	if (unlikely(range->flags & VY_ROTATE)) {
		*second = &range->i0;
		return &range->i1;
	}
	*second = NULL;
	return &range->i0;
}

static inline int
vy_range_cmp(struct vy_range *range, void *key, struct key_def *key_def)
{
	assert(range->min_key != NULL);
	return vy_tuple_compare(range->min_key->data, key, key_def);
}

static inline int
vy_range_cmpnode(struct vy_range *n1, struct vy_range *n2, struct key_def *key_def)
{
	if (n1 == n2)
		return 0;
	assert(n1->min_key != NULL && n2->min_key != NULL);
	return vy_tuple_compare(n1->min_key->data, n2->min_key->data, key_def);
}

static inline uint64_t
vy_range_size(struct vy_range *range)
{
	uint64_t size = 0;
	struct vy_run *run = range->run;
	while (run) {
		size += vy_run_index_size(&run->index) +
		        vy_run_index_total(&run->index);
		run = run->next;
	}
	return size;
}

static int vy_planner_create(struct vy_planner*);
static void vy_planner_destroy(struct vy_planner*);
static void vy_planner_update(struct vy_planner*, struct vy_range*);
static void vy_planner_update_range(struct vy_planner *p,
				    struct vy_range *range);
static void vy_planner_remove(struct vy_planner*, struct vy_range*);

struct vy_range_tree_key {
	char *data;
	int size;
};

static int
vy_range_tree_cmp(vy_range_tree_t *rbtree, struct vy_range *a, struct vy_range *b);

static int
vy_range_tree_key_cmp(vy_range_tree_t *rbtree,
		    struct vy_range_tree_key *a, struct vy_range *b);

rb_gen_ext_key(, vy_range_tree_, vy_range_tree_t, struct vy_range, tree_node,
		 vy_range_tree_cmp, struct vy_range_tree_key *,
		 vy_range_tree_key_cmp);

struct vy_range *
vy_range_tree_free_cb(vy_range_tree_t *t, struct vy_range *range, void *arg)
{
	(void)t;
	(void)arg;
	vy_range_delete(range, 0);
	return NULL;
}

static void
vy_index_ref(struct vy_index *index);

static void
vy_index_unref(struct vy_index *index);

struct key_def *
vy_index_key_def(struct vy_index *index)
{
	return index->key_def;
}

static int
vy_range_tree_cmp(vy_range_tree_t *rbtree, struct vy_range *a, struct vy_range *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vy_index, tree)->key_def;
	return vy_range_cmpnode(a, b, key_def);
}

static int
vy_range_tree_key_cmp(vy_range_tree_t *rbtree,
		    struct vy_range_tree_key *a, struct vy_range *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vy_index, tree)->key_def;
	return (-vy_range_cmp(b, a->data, key_def));
}

static inline void
vy_index_delete(struct vy_index *index);

struct vy_rangeiter {
	struct vy_index *index;
	struct vy_range *cur_range;
	enum vy_order order;
	char *key;
	int key_size;
};

static inline void
vy_rangeiter_open(struct vy_rangeiter *itr, struct vy_index *index,
		  enum vy_order order, char *key, int key_size)
{
	itr->index = index;
	itr->order = order;
	itr->key = key;
	itr->key_size = key_size;
	itr->cur_range = NULL;
	if (unlikely(index->range_count == 1)) {
		itr->cur_range = vy_range_tree_first(&index->tree);
		return;
	}
	if (unlikely(itr->key == NULL)) {
		switch (itr->order) {
		case VINYL_LT:
		case VINYL_LE:
			itr->cur_range = vy_range_tree_last(&index->tree);
			break;
		case VINYL_GT:
		case VINYL_GE:
			itr->cur_range = vy_range_tree_first(&index->tree);
			break;
		default:
			unreachable();
			break;
		}
		return;
	}
	/* route */
	assert(itr->key != NULL);
	struct vy_range_tree_key tree_key;
	tree_key.data = itr->key;
	tree_key.size = itr->key_size;
	itr->cur_range = vy_range_tree_psearch(&index->tree, &tree_key);
	if (itr->cur_range == NULL)
		itr->cur_range = vy_range_tree_first(&index->tree);
	assert(itr->cur_range != NULL);
}

static inline struct vy_range *
vy_rangeiter_get(struct vy_rangeiter *ii)
{
	return ii->cur_range;
}

static inline void
vy_rangeiter_next(struct vy_rangeiter *ii)
{
	switch (ii->order) {
	case VINYL_LT:
	case VINYL_LE:
		ii->cur_range = vy_range_tree_prev(&ii->index->tree,
						   ii->cur_range);
		break;
	case VINYL_GT:
	case VINYL_GE:
		ii->cur_range = vy_range_tree_next(&ii->index->tree,
						   ii->cur_range);
		break;
	default: unreachable();
	}
}

static int
vy_index_add_range(struct vy_index *index, struct vy_range *range)
{
	if (range->run == NULL) {
		range->min_key = vy_tuple_from_key(index, NULL, 0);
	} else {
		assert(range->run != NULL);
		struct vy_page_info *min =
			vy_run_index_first_page(&range->run->index);
		const char *min_key = vy_run_index_min_key(&range->run->index,
							   min);
		range->min_key = vy_tuple_extract_key_raw(index, min_key);
	}
	if (range->min_key == NULL)
		return -1;

	vy_range_tree_insert(&index->tree, range);
	index->range_index_version++;
	index->range_count++;
	return 0;
}

static int
vy_index_remove_range(struct vy_index *index, struct vy_range *range)
{
	vy_range_tree_remove(&index->tree, range);
	index->range_index_version++;
	index->range_count--;

	assert (range->min_key != NULL);
	vy_tuple_unref(range->min_key);
	range->min_key = NULL;
	return 0;
}

static int
vy_index_replace_range(struct vy_index *index, struct vy_range *old,
	   struct vy_range *range)
{
	vy_index_remove_range(index, old);
	vy_index_add_range(index, range);
	return 0;
}

/* dump tuple to the run page buffers (tuple header and data) */
static int
vy_run_dump_tuple(struct svwriteiter *iwrite, struct vy_buf *info_buf,
		  struct vy_buf *data_buf, struct vy_page_info *info)
{
	struct vy_tuple *value = sv_writeiter_get(iwrite);
	int64_t lsn = value->lsn;
	uint8_t flags = value->flags;
	if (sv_writeiter_is_duplicate(iwrite))
		flags |= SVDUP;
	if (vy_buf_ensure(info_buf, sizeof(struct vy_tuple_info)))
		return -1;
	struct vy_tuple_info *tupleinfo = (struct vy_tuple_info *)info_buf->p;
	tupleinfo->flags = flags;
	tupleinfo->offset = vy_buf_used(data_buf);
	tupleinfo->size = value->size;
	tupleinfo->lsn = lsn;
	vy_buf_advance(info_buf, sizeof(struct vy_tuple_info));

	if (vy_buf_ensure(data_buf, value->size))
		return -1;
	memcpy(data_buf->p, value->data, value->size);
	vy_buf_advance(data_buf, value->size);

	++info->count;
	if (lsn > info->max_lsn)
		info->max_lsn = lsn;
	if (lsn < info->min_lsn)
		info->min_lsn = lsn;
	return 0;
}

static ssize_t
vy_write_file(int fd, void *buf, uint32_t size)
{
	ssize_t pos = 0;
	while (pos < size) {
		ssize_t written = write(fd, buf + pos, size - pos);
		if (written <= 0)
			return -1;
		pos += written;
	}
	return pos;
}

static ssize_t
vy_pwrite_file(int fd, void *buf, uint32_t size, off_t offset)
{
	ssize_t pos = 0;
	while (pos < size) {
		ssize_t written = pwrite(fd, buf + pos,
					 size - pos, offset + pos);
		if (written <= 0)
			return -1;
		pos += written;
	}
	return pos;
}

static ssize_t
vy_write_aligned(int fd, void *buf, uint32_t *size)
{
	ssize_t written;
	uint32_t old_size = *size;
	*size = ALIGN_POS(*size);
	if (old_size != *size || (intptr_t)buf % FILE_ALIGN) {
		void *ptr = buf;
		if (posix_memalign(&ptr, FILE_ALIGN, *size)) {
			diag_set(OutOfMemory, *size,
				 "posix_memalign", "aligned buf");
			return -1;
		}
		memcpy(ptr, buf, old_size);
		memset((char *)ptr + old_size, 0, *size - old_size);
		written = vy_write_file(fd, ptr, *size);
		free(ptr);
	} else {
		written = vy_write_file(fd, buf, *size);
	}
	if (written == -1) {
		diag_set(ClientError, ER_VINYL, "Can't write file");
	}
	return written;
}

static ssize_t
vy_pwrite_aligned(int fd, void *buf, uint32_t *size, uint64_t pos)
{
	int written;
	uint32_t old_size = *size;
	*size = ALIGN_POS(*size);
	if (old_size != *size || (intptr_t)buf % FILE_ALIGN) {
		void *ptr = buf;
		if (posix_memalign(&ptr, FILE_ALIGN, *size)) {
			diag_set(OutOfMemory, *size,
				 "posix_memalign", "aligned buf");
			return -1;
		}
		memcpy(ptr, buf, old_size);
		memset((char *)ptr + old_size, 0, *size - old_size);
		written = vy_pwrite_file(fd, ptr, *size, pos);
		free(ptr);
	} else {
		written = vy_pwrite_file(fd, buf, *size, pos);
	}
	if (written == -1) {
		diag_set(ClientError, ER_VINYL, "Can't write file");
	}
	return written;
}

/**
 * Write tuples from the iterator to a new page in the run,
 * update page and run statistics.
 */
static int
vy_run_write_page(int fd, struct svwriteiter *iwrite,
		  uint32_t page_size,
		  struct vy_filterif *compression,
		  struct vy_run_index *run_index)
{
	struct vy_run_info *run_info = &run_index->info;

	struct vy_buf tuplesinfo, values, compressed;
	vy_buf_create(&tuplesinfo);
	vy_buf_create(&values);
	vy_buf_create(&compressed);

	if (vy_buf_ensure(&run_index->pages, sizeof(struct vy_page_info)))
		goto err;

	struct vy_page_info *page =
		(struct vy_page_info *)run_index->pages.p;
	memset(page, 0, sizeof(*page));
	page->min_lsn = INT64_MAX;
	page->offset = run_info->offset + run_info->size;

	while (sv_writeiter_has(iwrite) &&
	       (vy_buf_used(&values) < page_size ||
	        sv_writeiter_is_duplicate(iwrite))) {
		int rc = vy_run_dump_tuple(iwrite, &tuplesinfo, &values,
					   page);
		if (rc != 0)
			goto err;
		sv_writeiter_next(iwrite);
	}
	page->unpacked_size = vy_buf_used(&tuplesinfo) + vy_buf_used(&values);
	page->unpacked_size = ALIGN_POS(page->unpacked_size);

	if (compression) {
		struct vy_filter f;
		if (vy_filter_create(&f, compression, VINYL_FINPUT))
			goto err;
		if (vy_filter_start(&f, &compressed) ||
		    vy_filter_next(&f, &compressed, tuplesinfo.s,
				   vy_buf_used(&tuplesinfo)) ||
		    vy_filter_next(&f, &compressed, values.s,
				   vy_buf_used(&values)) ||
		    vy_filter_complete(&f, &compressed)) {
			vy_filter_destroy(&f);
			goto err;
		}
		vy_filter_destroy(&f);
	} else {
		vy_buf_ensure(&compressed, page->unpacked_size);
		memcpy(compressed.p, tuplesinfo.s,
		       vy_buf_used(&tuplesinfo));
		vy_buf_advance(&compressed, vy_buf_used(&tuplesinfo));
		memcpy(compressed.p, values.s, vy_buf_used(&values));
		vy_buf_advance(&compressed, vy_buf_used(&values));
	}
	page->size = vy_buf_used(&compressed);
	vy_write_aligned(fd, compressed.s, &page->size);
	page->crc = crc32_calc(0, compressed.s, vy_buf_used(&compressed));

	if (page->count > 0) {
		struct vy_buf *minmax_buf = &run_index->minmax;
		struct vy_tuple_info *tuplesinfoarr = (struct vy_tuple_info *) tuplesinfo.s;
		struct vy_tuple_info *mininfo = &tuplesinfoarr[0];
		struct vy_tuple_info *maxinfo = &tuplesinfoarr[page->count - 1];
		if (vy_buf_ensure(minmax_buf, mininfo->size + maxinfo->size))
			goto err;

		page->min_key_offset = vy_buf_used(minmax_buf);
		page->min_key_lsn = mininfo->lsn;
		char *minvalue = values.s + mininfo->offset;
		memcpy(minmax_buf->p, minvalue, mininfo->size);
		vy_buf_advance(minmax_buf, mininfo->size);

		page->max_key_offset = vy_buf_used(minmax_buf);
		page->max_key_lsn = maxinfo->lsn;
		char *maxvalue = values.s + maxinfo->offset;
		memcpy(minmax_buf->p, maxvalue, maxinfo->size);
		vy_buf_advance(minmax_buf, maxinfo->size);
	}
	vy_buf_advance(&run_index->pages, sizeof(struct vy_page_info));

	run_info->size += page->size;
	++run_info->count;
	if (page->min_lsn < run_info->min_lsn)
		run_info->min_lsn = page->min_lsn;
	if (page->max_lsn > run_info->max_lsn)
		run_info->max_lsn = page->max_lsn;
	run_info->total += page->size;
	run_info->totalorigin += page->unpacked_size;

	run_info->keys += page->count;

	vy_buf_destroy(&compressed);
	vy_buf_destroy(&tuplesinfo);
	vy_buf_destroy(&values);
	return 0;
err:
	vy_buf_destroy(&compressed);
	vy_buf_destroy(&tuplesinfo);
	vy_buf_destroy(&values);
	return -1;
}

/**
 * Write tuples from the iterator to a new run
 * and set up the corresponding run index structures.
 */
static int
vy_run_write(int fd, struct svwriteiter *iwrite,
	     struct vy_filterif *compression,
	     uint32_t page_size, uint64_t run_size,
	     struct vy_run **result)
{
	struct vy_run *run = vy_run_new();
	if (!run)
		return -1;

	struct vy_run_index *run_index = &run->index;
	struct vy_run_info *header = &run_index->info;
	/*
	 * Store start run offset in file. In case of run write
	 * failure the file is truncated to this position.
	 *
	 * Start offset can be used in future for integrity
	 * checks, data restoration or if we decide to use
	 * relative offsets for run objects.
	 */
	header->offset = lseek(fd, 0, SEEK_CUR);
	header->footprint = (struct vy_run_footprint) {
		sizeof(struct vy_run_info),
		sizeof(struct vy_page_info),
		sizeof(struct vy_tuple_info),
		FILE_ALIGN
	};
	header->min_lsn = INT64_MAX;

	/* write run info header and adjust size */
	uint32_t header_size = sizeof(*header);
	vy_write_aligned(fd, header, &header_size);
	header->size += header_size;

	/*
	 * Read from the iterator until it's exhausted or range
	 * size limit is reached.
	 * The current write iterator emits "virtual" eofs
	 * at page size boundary and can be resumed
	 * with sv_writeiter_resume()
	 */
	do {
		if (vy_run_write_page(fd, iwrite, page_size,
				      compression, run_index) == -1)
			goto err;
	} while (sv_writeiter_has(iwrite) && header->total < run_size);

	/* Write pages index */
	int rc;
	header->pages_offset = header->offset +
				     header->size;
	header->pages_size = vy_buf_used(&run_index->pages);
	rc = vy_write_aligned(fd, run_index->pages.s,
			      &header->pages_size);
	if (rc == -1)
		goto err;
	header->size += header->pages_size;

	/* Write min-max keys for pages */
	header->minmax_offset = header->offset +
				      header->size;
	header->minmax_size = vy_buf_used(&run_index->minmax);
	rc = vy_write_aligned(fd, run_index->minmax.s, &header->minmax_size);
	if (rc == -1)
		goto err;
	header->size += header->minmax_size;

	/*
	 * Sync written data
	 * TODO: check, maybe we can use O_SYNC flag instead
	 * of explicitly syncing
	 */
	if (fdatasync(fd) == -1)
		goto err_file;

	/*
	 * Eval run_info header crc and rewrite it
	 * to finalize the run on disk
	 * */
	header->crc = vy_crcs(header, sizeof(struct vy_run_info), 0);

	header_size = sizeof(*header);
	if (vy_pwrite_aligned(fd, header, &header_size,
			      header->offset) == -1)
		goto err;
	if (fdatasync(fd) == -1)
		goto err_file;

	*result = run;
	return 0;

err_file:
	vy_error("index file error: %s", strerror(errno));
err:
	/*
	 * Reposition to end of file and trucate it
	 */
	lseek(fd, header->offset, SEEK_SET);
	ftruncate(fd, header->offset);
	free(run);
	return -1;
}

void
vy_tmp_mem_iterator_open(struct vy_iter *virt_itr, struct vy_mem *mem,
			 enum vy_order order, char *key);

static inline int
vy_run_create(struct vy_index *index,
	      struct vy_range *parent, struct vy_mem *mem,
	      int64_t vlsn, struct vy_run **result)
{
	/* in-memory mode blob */
	int rc;
	struct svmerge vmerge;
	sv_mergeinit(&vmerge, index, index->key_def);
	rc = sv_mergeprepare(&vmerge, 1);
	if (unlikely(rc == -1))
		return -1;
	struct svmergesrc *s = sv_mergeadd(&vmerge, NULL);
	vy_tmp_mem_iterator_open(&s->src, mem, VINYL_GE, NULL);

	struct svmergeiter imerge;
	sv_mergeiter_open(&imerge, &vmerge, VINYL_GE);

	struct svwriteiter iwrite;
	sv_writeiter_open(&iwrite, &imerge,
			  vlsn, 1, 1);
	if ((rc = vy_run_write(parent->fd, &iwrite,
			        index->compression_if,
				index->key_def->opts.page_size,
				UINT64_MAX, result)))
		goto err;

	sv_writeiter_close(&iwrite);
	sv_mergefree(&vmerge);
	return 0;
err:
	sv_writeiter_close(&iwrite);
	sv_mergefree(&vmerge);
	return -1;
}

static int64_t
vy_index_range_id_next(struct vy_index *index);

static int
vy_range_create(struct vy_range *range, struct vy_index *index);

static int
vy_range_complete(struct vy_range *range, struct vy_index *index);

static int
vy_dump_begin(struct vy_index *index, struct vy_range *range,
	      struct vy_mem *i, int64_t vlsn, struct vy_run **result)
{
	assert(range->flags & VY_LOCK);

	if (!range->run) {
		/* An empty range, create a temp file for it. */
		if (vy_range_create(range, index) < 0)
			return -1;
	}

	return vy_run_create(index, range, i, vlsn, result);
}

static int
vy_dump_commit(struct vy_index *index, struct vy_range *range,
	       struct vy_mem *i, struct vy_run *run)
{
	/* commit */
	run->next = range->run;
	range->run = run;
	range->run_count++;
	range->range_version++;
	index->range_index_version++;
	assert(range->used >= i->used);
	range->used -= i->used;
	vy_quota_release(index->env->quota, i->used);
	index->size += vy_run_index_size(&run->index) +
		       vy_run_index_total(&run->index);
	struct vy_mem swap = *i;
	swap.tree.arg = &swap;
	vy_range_unrotate(range);
	vy_range_unlock(range);
	vy_planner_update(&index->p, range);

	if (range->run_count == 1) {
		/* First non-empty run for this range, deploy the range. */
		if (vy_range_complete(range, index) < 0)
			return -1;
		/*
		 * The range file was created successfully,
		 * update the range index on disk.
		 */
		if (index->first_dump_lsn == 0)
			index->first_dump_lsn = run->index.info.min_lsn;
		vy_index_dump_range_index(index);
	}

	vy_mem_gc(&swap);
	return 0;
}

void
vy_tmp_run_iterator_open(struct vy_iter *virt_itr,
			 struct vy_index *index,
			 struct vy_run *run, int fd,
			 struct vy_filterif *compression,
			 enum vy_order order, char *key);

static inline int
vy_range_split(struct vy_index *index,
	       struct svmergeiter *merge_iter,
	       uint64_t  size_node,
	       uint64_t  size_stream,
	       uint32_t  stream,
	       int64_t  vlsn,
	       struct rlist *result);

static int
vy_range_compact_begin(struct vy_index *index, struct vy_range *range,
		 int64_t vlsn, struct vy_iter *vindex, uint64_t vindex_used,
		 struct rlist *result)
{
	assert(range->flags & VY_LOCK);

	/* prepare for compaction */
	int rc;
	struct svmerge merge;
	sv_mergeinit(&merge, index, index->key_def);
	rc = sv_mergeprepare(&merge, range->run_count + 1);
	if (unlikely(rc == -1))
		return -1;

	/* include vindex into merge process */
	uint32_t count = 0;
	uint64_t size_stream = 0;
	if (vindex) {
		sv_mergeadd(&merge, vindex);
		size_stream = vindex_used;
	}

	struct vy_run *run = range->run;
	while (run) {
		struct svmergesrc *s = sv_mergeadd(&merge, NULL);
		struct vy_filterif *compression = index->compression_if;
		vy_tmp_run_iterator_open(&s->src, index, run, range->fd,
				     compression, VINYL_GE, NULL);
		size_stream += vy_run_index_total(&run->index);
		count += vy_run_index_count(&run->index);
		run = run->next;
	}

	/* begin compaction.
	 *
	 * Split merge stream into a number of
	 * a new nodes.
	 */
	struct svmergeiter im;
	sv_mergeiter_open(&im, &merge, VINYL_GE);
	rc = vy_range_split(index, &im, index->key_def->opts.range_size,
			    size_stream, count, vlsn, result);
	sv_mergefree(&merge);

	return rc;
}

static int
vy_range_redistribute(struct vy_index *index, struct vy_range *range,
		struct rlist *result)
{
	(void)index;
	struct vy_mem *mem = vy_range_mem(range);
	struct vy_iter ii;
	vy_tmp_mem_iterator_open(&ii, mem, VINYL_GE, NULL);
	assert(!rlist_empty(result));
	struct vy_range *prev = rlist_first_entry(result, struct vy_range,
						  split);
	while (1)
	{
		if (rlist_next(&prev->split) == result) {
			/* no more ranges */
			assert(prev != NULL);
			while (ii.vif->has(&ii)) {
				struct vy_tuple *v = ii.vif->get(&ii);
				vy_mem_set(&prev->i0, v);
				ii.vif->next(&ii);
			}
			break;
		}
		struct vy_range *p = rlist_next_entry(prev, split);
		while (ii.vif->has(&ii))
		{
			struct vy_tuple *v = ii.vif->get(&ii);
			struct vy_page_info *page = vy_run_index_first_page(&p->run->index);
			int rc = vy_tuple_compare(v->data,
				vy_run_index_min_key(&p->run->index, page),
				index->key_def);
			if (unlikely(rc >= 0))
				break;
			vy_mem_set(&prev->i0, v);
			ii.vif->next(&ii);
		}
		if (unlikely(! ii.vif->has(&ii)))
			break;
		prev = p;
	}
	assert(ii.vif->get(&ii) == NULL);
	return 0;
}

static inline void
vy_range_redistribute_set(struct vy_index *index, uint64_t now, struct vy_tuple *v)
{
	/* match range */
	struct vy_rangeiter ii;
	vy_rangeiter_open(&ii, index, VINYL_GE, v->data, v->size);
	struct vy_range *range = vy_rangeiter_get(&ii);
	assert(range != NULL);
	/* update range */
	struct vy_mem *vindex = vy_range_mem(range);
	int rc = vy_mem_set(vindex, v);
	assert(rc == 0); /* TODO: handle BPS tree errors properly */
	(void) rc;
	range->update_time = now;
	range->used += vy_tuple_size(v);
	/* schedule range */
	vy_planner_update_range(&index->p, range);
}

static int
vy_range_redistribute_index(struct vy_index *index, struct vy_range *range)
{
	struct vy_buf buf;
	vy_buf_create(&buf);
	struct vy_mem *mem = vy_range_mem(range);
	struct vy_iter ii;
	vy_tmp_mem_iterator_open(&ii, mem, VINYL_GE, NULL);
	while (ii.vif->has(&ii)) {
		struct vy_tuple *v = ii.vif->get(&ii);
		int rc = vy_buf_add(&buf, v, sizeof(struct vy_tuple ***));
		if (unlikely(rc == -1)) {
			vy_buf_destroy(&buf);
			return -1;
		}
		ii.vif->next(&ii);
	}
	if (unlikely(vy_buf_used(&buf) == 0)) {
		vy_buf_destroy(&buf);
		return 0;
	}
	uint64_t now = clock_monotonic64();
	struct vy_bufiter i;
	vy_bufiter_open(&i, &buf, sizeof(struct vy_tuple **));
	while (vy_bufiter_has(&i)) {
		struct vy_tuple **v = vy_bufiterref_get(&i);
		vy_range_redistribute_set(index, now, *v);
		vy_bufiter_next(&i);
	}
	vy_buf_destroy(&buf);
	return 0;
}

static int
vy_range_splitfree(struct rlist *result)
{
	struct vy_range *range, *next;
	rlist_foreach_entry_safe(range, result, split, next) {
		rlist_del_entry(range, split);
		vy_range_delete(range, 0);
	}
	assert(rlist_empty(result));
	return 0;
}

static inline int
vy_range_split(struct vy_index *index,
	       struct svmergeiter *merge_iter,
	       uint64_t  size_node,
	       uint64_t  size_stream,
	       uint32_t  stream,
	       int64_t  vlsn,
	       struct rlist *result)
{
	(void) stream;
	(void) size_stream;
	int rc;
	struct vy_range *range = NULL;

	struct svwriteiter iwrite;
	sv_writeiter_open(&iwrite, merge_iter,
			  vlsn, 0, 0);

	while (sv_writeiter_has(&iwrite)) {
		/* create new range */
		range = vy_range_new(index->key_def);
		if (unlikely(range == NULL))
			goto error;
		rc = vy_range_create(range, index);
		if (unlikely(rc == -1))
			goto error;

		struct vy_run *run;
		if ((rc = vy_run_write(range->fd, &iwrite,
				       index->compression_if,
				       index->key_def->opts.page_size,
				       size_node, &run)))
			goto error;

		range->run = run;
		++range->run_count;

		rlist_add_entry(result, range, split);
		if (unlikely(rc == -1))
			goto error;
	}
	sv_writeiter_close(&iwrite);
	return 0;
error:
	sv_writeiter_close(&iwrite);
	if (range)
		vy_range_delete(range, 0);
	vy_range_splitfree(result);
	return -1;
}

static int
vy_range_compact_commit(struct vy_index *index, struct vy_range *range,
		      struct rlist *result)
{
	/* mask removal of a single range as a
	 * single range update */
	int rc;
	int count = 0;
	struct vy_range *n;
	rlist_foreach_entry(n, result, split) {
		 count++;
	}

	int range_count = index->range_count;

	if (unlikely(count == 0 && range_count == 1))
	{
		n = vy_range_new(index->key_def);
		if (unlikely(n == NULL))
			return -1;
		rlist_add_entry(result, n, split);
		count++;
	}

	/* commit compaction changes */
	struct vy_mem *j = vy_range_mem(range);
	vy_planner_remove(&index->p, range);
	range->flags |= VY_SPLIT;
	index->size -= vy_range_size(range);
	switch (count) {
	case 0: /* delete */
		vy_index_remove_range(index, range);
		vy_range_redistribute_index(index, range);
		break;
	case 1: /* self update */
		n = rlist_first_entry(result, struct vy_range, split);
		n->i0 = *j;
		n->i0.tree.arg = &n->i0;
		n->temperature = range->temperature;
		n->temperature_reads = range->temperature_reads;
		n->used = j->used;
		index->size += vy_range_size(n);
		vy_range_lock(n);
		vy_index_replace_range(index, range, n);
		vy_planner_update(&index->p, n);
		break;
	default: /* split */
		rc = vy_range_redistribute(index, range, result);
		if (unlikely(rc == -1)) {
			vy_range_splitfree(result);
			return -1;
		}
		rlist_foreach_entry(n, result, split) {
			n->used = n->i0.used;
			n->temperature = range->temperature;
			n->temperature_reads = range->temperature_reads;
			index->size += vy_range_size(n);
			vy_range_lock(n);
			if (rlist_first_entry(result, struct vy_range,
					      split) == n) {
				vy_index_replace_range(index, range, n);
			} else {
				vy_index_add_range(index, n);
			}
			vy_planner_update(&index->p, n);
		}
		break;
	}
	vy_mem_create(j, index->key_def);

	/* compaction completion */

	/* complete new nodes */
	rlist_foreach_entry(n, result, split) {
		rc = vy_range_complete(n, index);
		if (unlikely(rc == -1))
			return -1;
	}

	/* unlock */
	rlist_foreach_entry(n, result, split) {
		vy_range_unlock(n);
	}

	if (vy_index_dump_range_index(index)) {
		/*
		 * @todo: we should roll back the failed dump
		 * first, but it requires a redesign of the index
		 * change function.
		 */
		return -1;
	}

	return vy_range_delete(range, 1);
}

static struct vy_range *
vy_range_new(struct key_def *key_def)
{
	struct vy_range *range = (struct vy_range*) calloc(1, sizeof(*range));
	if (range == NULL) {
		diag_set(OutOfMemory, sizeof(struct vy_range), "malloc",
			 "struct vy_range");
		return NULL;
	}
	range->fd = -1;
	vy_mem_create(&range->i0, key_def);
	vy_mem_create(&range->i1, key_def);
	ss_rqinitnode(&range->nodecompact);
	ss_rqinitnode(&range->nodedump);
	rlist_create(&range->split);
	return range;
}

static inline int
vy_range_close(struct vy_range *range, int gc)
{
	int rcret = 0;

	int rc = close(range->fd);
	if (unlikely(rc == -1)) {
		vy_error("index file close error: %s",
		               strerror(errno));
		rcret = -1;
	}
	if (gc) {
		vy_mem_gc(&range->i0);
		vy_mem_gc(&range->i1);
	} else {
		vy_mem_destroy(&range->i0);
		vy_mem_destroy(&range->i1);
	}
	return rcret;
}

static inline int
vy_range_recover(struct vy_range *range)
{
	int fd = range->fd;
	int readen;
	uint32_t read_size = ALIGN_POS(sizeof(struct vy_run_info));
	void *read_buf;
	posix_memalign(&read_buf, FILE_ALIGN, read_size);
	while ((readen = vy_read_aligned(fd, read_buf, &read_size))
		== (ssize_t)read_size) {
		struct vy_run_info *run_info =
			(struct vy_run_info *)read_buf;
		if (!run_info->size) {
			 vy_error("run was not finished, range is"
				  "broken for file %s", range->path);
			 return -1;
		}
		struct vy_run *vy_run = vy_run_new();
		vy_run->index.info = *run_info;

		vy_buf_ensure(&vy_run->index.pages, run_info->pages_size);
		if (vy_pread_aligned(fd, vy_run->index.pages.s,
				     &run_info->pages_size,
				     run_info->pages_offset) == -1)
			return -1;

		if (vy_buf_ensure(&vy_run->index.minmax,
				  run_info->minmax_size))
			return -1;
		if (vy_pread_aligned(fd, vy_run->index.minmax.s,
				     &run_info->minmax_size,
				     run_info->minmax_offset) == -1)
			return -1;

		vy_run->next = range->run;
		range->run = vy_run;
		++range->run_count;
		if (lseek(fd, run_info->offset + run_info->size,
			  SEEK_SET) == -1)
			return -1;
	}
	return 0;
}

int
vy_range_open(struct vy_index *index, struct vy_range *range, char *path)
{
	snprintf(range->path, PATH_MAX, "%s", path);
	int rc = range->fd = open(path, O_RDWR);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' open error: %s ",
		         path, strerror(errno));
		return -1;
	}
	rc = vy_range_recover(range);
	if (unlikely(rc == -1))
		return -1;

	/* Attach range to the index and update statistics. */
	vy_index_add_range(index, range);
	index->size += vy_range_size(range);
	vy_planner_update(&index->p, range);
	return 0;
}

static int
vy_range_create(struct vy_range *range, struct vy_index *index)
{
	snprintf(range->path, PATH_MAX, "%s/.tmpXXXXXX", index->path);
	int rc = range->fd = mkstemp(range->path);
	if (unlikely(rc == -1)) {
		vy_error("temp file '%s' create error: %s",
		               range->path, strerror(errno));
		return -1;
	}
	return 0;
}

static inline void
vy_range_delete_runs(struct vy_range *range)
{
	struct vy_run *p = range->run;
	struct vy_run *next = NULL;
	while (p) {
		next = p->next;
		vy_run_delete(p);
		p = next;
	}
}

static int
vy_range_delete(struct vy_range *range, int gc)
{
	int rcret = 0;
	int rc;
	vy_range_delete_runs(range);
	rc = vy_range_close(range,gc);
	if (unlikely(rc == -1))
		rcret = -1;
	if (!range->id && range->fd > 0) {
		/* Range wasn't completed */
		unlink(range->path);
	}
	TRASH(range);
	free(range);
	return rcret;
}

static int
vy_range_complete(struct vy_range *range, struct vy_index *index)
{
	range->id = vy_index_range_id_next(index);
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/%016"PRIx64".range",
		 index->path, range->id);
	int rc = rename(range->path, path);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' rename error: %s",
		               range->path,
		               strerror(errno));
		range->id = 0;
	} else {
		snprintf(range->path, PATH_MAX, "%s", path);
	}
	return rc;
}

static int vy_planner_create(struct vy_planner *p)
{
	if (ss_rqinit(&p->compact, 1, 20) < 0)
		return -1;
	/* 1Mb step up to 4Gb */
	if (ss_rqinit(&p->dump, 1024 * 1024, 4000)) {
		ss_rqfree(&p->compact);
		return -1;
	}
	return 0;
}

static void
vy_planner_destroy(struct vy_planner *p)
{
	ss_rqfree(&p->compact);
	ss_rqfree(&p->dump);
}

static void
vy_planner_update(struct vy_planner *p, struct vy_range *range)
{
	ss_rqupdate(&p->dump, &range->nodedump, range->used);
	ss_rqupdate(&p->compact, &range->nodecompact, range->run_count);
}

static void
vy_planner_update_range(struct vy_planner *p, struct vy_range *range)
{
	ss_rqupdate(&p->dump, &range->nodedump, range->used);
}

static void
vy_planner_remove(struct vy_planner *p, struct vy_range *range)
{
	ss_rqdelete(&p->dump, &range->nodedump);
	ss_rqdelete(&p->compact, &range->nodecompact);
}

static void
vy_profiler_begin(struct vy_profiler *p, struct vy_index *i)
{
	memset(p, 0, sizeof(*p));
	p->i = i;
	p->temperature_min = 100;
}

static void
vy_profiler_end(struct vy_profiler *p)
{
	(void)p;
}

static void
vy_profiler_histogram_run(struct vy_profiler *p)
{
	/* prepare histogram string */
	int size = 0;
	int i = 0;
	while (i < 20) {
		if (p->histogram_run[i] == 0) {
			i++;
			continue;
		}
		size += snprintf(p->histogram_run_sz + size,
		                 sizeof(p->histogram_run_sz) - size,
		                 "[%d]:%d ", i,
		                 p->histogram_run[i]);
		i++;
	}
	if (p->histogram_run_20plus) {
		size += snprintf(p->histogram_run_sz + size,
		                 sizeof(p->histogram_run_sz) - size,
		                 "[20+]:%d ",
		                 p->histogram_run_20plus);
	}
	if (size == 0)
		p->histogram_run_ptr = NULL;
	else {
		p->histogram_run_ptr = p->histogram_run_sz;
	}
}

static int vy_profiler_(struct vy_profiler *p)
{
	uint32_t temperature_total = 0;
	uint64_t memory_used = 0;
	struct vy_range *range = vy_range_tree_first(&p->i->tree);
	while (range) {
		if (p->temperature_max < range->temperature)
			p->temperature_max = range->temperature;
		if (p->temperature_min > range->temperature)
			p->temperature_min = range->temperature;
		temperature_total += range->temperature;
		p->total_range_count++;
		p->count += range->i0.tree.size;
		p->count += range->i1.tree.size;
		p->total_run_count += range->run_count;
		if (p->total_run_max < range->run_count)
			p->total_run_max = range->run_count;
		if (range->run_count < 20)
			p->histogram_run[range->run_count]++;
		else
			p->histogram_run_20plus++;
		memory_used += range->i0.used;
		memory_used += range->i1.used;
		struct vy_run *run = range->run;
		while (run != NULL) {
			p->count += run->index.info.keys;
//			p->count_dup += run->index.header.dupkeys;
			int indexsize = vy_run_index_size(&run->index);
			p->total_snapshot_size += indexsize;
			p->total_range_size += indexsize + run->index.info.total;
			p->total_range_origin_size += indexsize + run->index.info.totalorigin;
			p->total_page_count += run->index.info.count;
			run = run->next;
		}
		range = vy_range_tree_next(&p->i->tree, range);
	}
	if (p->total_range_count > 0) {
		p->total_run_avg =
			p->total_run_count / p->total_range_count;
		p->temperature_avg =
			temperature_total / p->total_range_count;
	}
	p->memory_used = memory_used;
	p->read_disk  = p->i->read_disk;
	p->read_cache = p->i->read_cache;

	vy_profiler_histogram_run(p);
	return 0;
}

/* {{{ vy_run_itr API forward declaration */
/* TODO: move to header (with struct vy_run_itr) and remove static keyword */

/** Position of a particular tuple in vy_run. */
struct vy_run_iterator_pos {
	uint32_t page_no;
	uint32_t pos_in_page;
};

/**
 * Iterator over vy_run
 */
struct vy_run_iterator {
	/* Members needed for memory allocation and disk access */
	/* index */
	struct vy_index *index;
	/* run */
	struct vy_run *run;
	/* file of run */
	int fd;
	/* compression in file */
	struct vy_filterif *compression;

	/* Search options */
	/**
	 * Order, that specifies direction, start position and stop criteria
	 * if key == NULL: GT and EQ are changed to GE, LT to LE for beauty.
	 */
	enum vy_order order;
	/* Search key data in terms of vinyl, vy_tuple_compare argument */
	char *key;
	/* LSN visibility, iterator shows values with lsn <= vlsn */
	int64_t vlsn;

	/* State of the iterator */
	/** Position of the current record */
	struct vy_run_iterator_pos curr_pos;
	/**
	 * Last tuple returned by vy_run_iterator_get.
	 * The iterator holds this tuple until the next call to
	 * vy_run_iterator_get, when it's dereferenced.
	 */
	struct vy_tuple *curr_tuple;
	/** Position of record that spawned curr_tuple */
	struct vy_run_iterator_pos curr_tuple_pos;
	/**
	 * Page number of the currently loaded page, UINT32_MAX if
	 * no page is loaded.
	 */
	uint32_t curr_loaded_page;
	/** Is false until first .._get ot .._next_.. method is called */
	bool search_started;
	/** Search is finished, you will not get more values from iterator */
	bool search_ended;
};

/**
 * Open the iterator
 */
static void
vy_run_iterator_open(struct vy_run_iterator *itr, struct vy_index *index,
		     struct vy_run *run, int fd,
		     struct vy_filterif *compression, enum vy_order order,
		     char *key, int64_t vlsn);

static int
vy_run_iterator_get(struct vy_run_iterator *itr, struct vy_tuple **result);

static int
vy_run_iterator_next_key(struct vy_run_iterator *itr);

static int
vy_run_iterator_next_lsn(struct vy_run_iterator *itr);

static int
vy_run_iterator_restore(struct vy_run_iterator *itr, struct vy_tuple *last_tuple);

/**
 * Close an iterator and free all resources
 */
static void
vy_run_iterator_close(struct vy_run_iterator *itr);

/* }}} vy_run_iterator API forward declaration */

static int
vy_readcommited(struct vy_index *index, struct vy_tuple *tuple);

/**
 * Create an index directory for a new index.
 * TODO: create index files only after the WAL
 * record is committed.
 */
static int
vy_index_create(struct vy_index *index)
{
	/* create directory */
	int rc;
	char *path_sep = index->path;
	while (*path_sep == '/') {
		/* Don't create root */
		++path_sep;
	}
	while ((path_sep = strchr(path_sep, '/'))) {
		/* Recursively create path hierarchy */
		*path_sep = '\0';
		rc = mkdir(index->path, 0777);
		if (rc == -1 && errno != EEXIST) {
			vy_error("directory '%s' create error: %s",
		                 index->path, strerror(errno));
			*path_sep = '/';
			return -1;
		}
		*path_sep = '/';
		++path_sep;
	}
	rc = mkdir(index->path, 0777);
	if (rc == -1 && errno != EEXIST) {
		vy_error("directory '%s' create error: %s",
	                 index->path, strerror(errno));
		return -1;
	}

	index->range_id_max = 0;
	index->first_dump_lsn = 0;
	index->last_dump_range_id = 0;
	/* create initial range */
	struct vy_range *range = vy_range_new(index->key_def);
	if (unlikely(range == NULL))
		return -1;
	vy_index_add_range(index, range);
	vy_planner_update(&index->p, range);
	index->size = vy_range_size(range);
	return 0;
}

static int64_t
vy_index_range_id_next(struct vy_index *index)
{
	int64_t id = pm_atomic_fetch_add_explicit(&index->range_id_max,
						  1, pm_memory_order_relaxed);
	return id + 1;
}

/**
 * A quick intro into Vinyl cosmology and file format
 * --------------------------------------------------
 * A single vinyl index on disk consists of a set of "range"
 * objects. A range contains a sorted set of index keys;
 * keys in different ranges do not overlap, for example:
 * [0..100],[103..252],[304..360]
 *
 * The sorted set of keys in a range is called a run. A single
 * range may contain multiple runs, each run contains changes of
 * keys in the range over a certain period of time. The periods do
 * not overlap, while, of course, two runs of the same range may
 * contain changes of the same key.
 * All keys in a run are sorted and split between pages of
 * approximately equal size. The purpose of putting keys into
 * pages is a quicker key lookup, since (min,max) key of every
 * page is put into the page index, stored at the beginning of each
 * run. The page index of an active run is fully cached in RAM.
 *
 * All files of an index have the following name pattern:
 * <lsn>.<range_id>.index
 * and are stored together in the index directory.
 *
 * The <lsn> component represents LSN of index creation: it is used
 * to distinguish between different "incarnations" of the same index,
 * e.g. on create/drop events. In a most common case LSN is the
 * same for all files in an index.
 *
 * <range_id> component represents the id of the range in an
 * index. The id is a monotonically growing integer, and is
 * assigned to a range when it's created.  The header file of each
 * range contains a full list of range ids of all ranges known to
 * the index when this last range file was created. Thus by
 * navigating to the latest range and reading its range directory,
 * we can find out ids of all remaining ranges of the index and
 * open them.
 */
static int
vy_index_open_ex(struct vy_index *index)
{
	/*
	 * The main index file name has format <lsn>.<range_id>.index.
	 * Load the index with the greatest LSN (but at least
	 * as new as the current view LSN, to skip dropped
	 * indexes) and choose the maximal range_id among
	 * ranges within the same LSN.
	 */
	int64_t first_dump_lsn = INT64_MAX;
	int64_t last_dump_range_id = 0;
	DIR *index_dir;
	index_dir = opendir(index->path);
	if (!index_dir) {
		vy_error("Can't open dir %s", index->path);
		return -1;
	}
	struct dirent *dirent;
	while ((dirent = readdir(index_dir))) {
		if (!strstr(dirent->d_name, ".index"))
			continue;
		int64_t index_lsn;
		int64_t range_id;
		if (sscanf(dirent->d_name, "%"SCNu64".%"SCNx64,
			   &index_lsn, &range_id) != 2)
			continue;
		/*
		 * Find the newest range in the last incarnation
		 * of this index.
		 */
		if (index_lsn < index->env->xm->lsn)
			continue;
		if (index_lsn < first_dump_lsn) {
			first_dump_lsn = index_lsn;
			last_dump_range_id = range_id;
		} else if (index_lsn == first_dump_lsn &&
			   last_dump_range_id < range_id) {
			last_dump_range_id = range_id;
		}
	}
	closedir(index_dir);

	if (first_dump_lsn == INT64_MAX) {
		vy_error("No matching index files found for the current LSN"
			 " in path %s", index->path);
		return -1;
	}

	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/%016"PRIu64".%016"PRIx64".index",
		 index->path, first_dump_lsn, last_dump_range_id);
	int fd = open(path, O_RDWR);
	if (fd == -1) {
		vy_error("Can't open index file %s: %s",
			 path, strerror(errno));
		return -1;
	}

	int64_t range_id;
	int size;
	while ((size = read(fd, &range_id, sizeof(range_id))) ==
		sizeof(range_id)) {
		struct vy_range *range = vy_range_new(index->key_def);
		if (!range) {
			vy_error("%s", "Can't alloc range");
			vy_range_delete(range, 0);
			return -1;
		}
		char range_path[PATH_MAX];
		snprintf(range_path, PATH_MAX, "%s/%016"PRIx64".range",
			 index->path, range_id);
		range->id = range_id;
		if (vy_range_open(index, range, range_path)) {
			vy_range_delete(range, 0);
			return -1;
		}
	}

	close(fd);
	if (size != 0) {
		vy_error("Corrupted index file %s", path);
		return -1;
	}
	index->first_dump_lsn = first_dump_lsn;
	index->last_dump_range_id = last_dump_range_id;
	if (!index->range_count) {
		/*
		 * Special case: index has no ranges
		 * (merged out or empty index was checkpointed)
		 */
		/* create initial range */
		struct vy_range *range = vy_range_new(index->key_def);
		if (unlikely(range == NULL))
			return -1;
		vy_index_add_range(index, range);
		vy_planner_update(&index->p, range);
		index->size = vy_range_size(range);
	}

	return 0;
}

/**
 * Iterate over the write set of a single index
 * and flush it i0 tree of this index.
 *
 * Break when the write set begins pointing at the next index.
 */
static struct txv *
vy_tx_write(write_set_t *write_set, struct txv *v, uint64_t time,
	 enum vinyl_status status, int64_t lsn)
{
	struct vy_index *index = v->index;
	struct vy_range *prev_range = NULL;
	struct vy_range *range = NULL;
	size_t quota = 0;

	for (; v && v->index == index; v = write_set_next(write_set, v)) {

		struct vy_tuple *tuple = v->tuple;
		tuple->lsn = lsn;

		/**
		 * If we're recovering the WAL, it may happen so
		 * that this particular run was dumped after the
		 * checkpoint, and we're replaying records already
		 * present in the database.
		 * In this case avoid overwriting a newer version with
		 * an older one.
		 */
		if ((status == VINYL_FINAL_RECOVERY &&
		     vy_readcommited(index, tuple))) {

			continue;
		}
		/* match range */
		struct vy_rangeiter ii;
		vy_rangeiter_open(&ii, index, VINYL_GE, tuple->data, tuple->size);
		range = vy_rangeiter_get(&ii);
		assert(range != NULL);
		if (prev_range != NULL && range != prev_range) {
			/*
			 * The write set is key-ordered, hence
			 * we can safely assume there won't be new
			 * keys for this range.
			 */
			prev_range->update_time = time;
			vy_planner_update_range(&index->p, prev_range);
		}
		prev_range = range;
		vy_tuple_ref(tuple);
		/* insert into range index */
		struct vy_mem *vindex = vy_range_mem(range);
		int rc = vy_mem_set(vindex, tuple);
		assert(rc == 0); /* TODO: handle BPS tree errors properly */
		(void) rc;
		/* update range */
		range->used += vy_tuple_size(tuple);
		quota += vy_tuple_size(tuple);
	}
	if (range != NULL) {
		range->update_time = time;
		vy_planner_update_range(&index->p, range);
	}
	/* Take quota after having unlocked the index mutex. */
	vy_quota_use(index->env->quota, quota);
	return v;
}

/* {{{ Scheduler Task */

struct vy_task_ops {
	/**
	 * This function is called from a worker. It is supposed to do work
	 * which is too heavy for the tx thread (like IO or compression).
	 * Returns 0 on success.
	 */
	int (*execute)(struct vy_task *);
	/**
	 * This function is called by the scheduler upon task completion.
	 * It may be used to finish the task from the tx thread context.
	 * Returns 0 on success.
	 */
	int (*complete)(struct vy_task *);
};

struct vy_task {
	const struct vy_task_ops *ops;

	/*
	 * Set to ->execute retcode. If non-zero, ->complete won't be called.
	 */
	int status;

	struct vy_index *index;
	struct vy_range *range;

	/*
	 * Memory range to dump.
	 */
	struct vy_mem *range_mem;
	/*
	 * View sequence number at the time when the task was scheduled.
	 */
	int64_t vlsn;
	/*
	 * Result of vy_range_compact_begin() is stored here.
	 */
	struct rlist compact_result;
	/*
	 * Result of vy_dump_begin() is stored here.
	 */
	struct vy_run *dump_result;
	/*
	 * A link in the list of all pending tasks, generated by
	 * task scheduler.
	 */
	struct stailq_entry link;
};

static inline struct vy_task *
vy_task_new(struct mempool *pool, struct vy_index *index,
	    const struct vy_task_ops *ops)
{
	struct vy_task *task = mempool_alloc(pool);
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task), "scheduler", "task");
		return NULL;
	}
	task->ops = ops;
	task->index = index;
	rlist_create(&task->compact_result);
	vy_index_ref(index);
	return task;
}

static inline void
vy_task_delete(struct mempool *pool, struct vy_task *task)
{
	if (task->index) {
		vy_index_unref(task->index);
		task->index = NULL;
	}

	TRASH(task);
	mempool_free(pool, task);
}

static int
vy_task_dump_execute(struct vy_task *task)
{
	return vy_dump_begin(task->index, task->range, task->range_mem,
			     task->vlsn, &task->dump_result);
}

static int
vy_task_dump_complete(struct vy_task *task)
{
	return vy_dump_commit(task->index, task->range, task->range_mem,
			      task->dump_result);
}

static struct vy_task_ops vy_task_dump_ops = {
	.execute = vy_task_dump_execute,
	.complete = vy_task_dump_complete,
};

static int
vy_task_compact_execute(struct vy_task *task)
{
	return vy_range_compact_begin(task->index, task->range, task->vlsn,
				      NULL, 0, &task->compact_result);
}

static int
vy_task_compact_complete(struct vy_task *task)
{
	return vy_range_compact_commit(task->index, task->range,
				       &task->compact_result);
}

static struct vy_task_ops vy_task_compact_ops = {
	.execute = vy_task_compact_execute,
	.complete = vy_task_compact_complete,
};

static int
vy_task_drop_execute(struct vy_task *task)
{
	assert(task->index->refs == 1); /* referenced by this task */
	vy_index_delete(task->index);
	task->index = NULL;
	return 0;
}

static struct vy_task_ops vy_task_drop_ops = {
	.execute = vy_task_drop_execute,
};

/* Scheduler Task }}} */

/* {{{ Scheduler */

struct vy_scheduler {
	pthread_mutex_t        mutex;
	int64_t       checkpoint_lsn_last;
	int64_t       checkpoint_lsn;
	bool checkpoint_in_progress;
	int            rr;
	int            count;
	struct vy_index **indexes;
	struct rlist   shutdown;
	struct vy_env    *env;

	struct cord *worker_pool;
	struct fiber *scheduler;
	struct ev_loop *loop;
	int worker_pool_size;
	bool is_worker_pool_running;

	/**
	 * There is a pending task for workers in the pool,
	 * or we want to shutdown workers.
	 */
	pthread_cond_t worker_cond;
	/**
	 * There is no pending tasks for workers, so scheduler
	 * needs to create one, or we want to shutdown the
	 * scheduler. Scheduler is a fiber in TX, so ev_async + ipc_channel
	 * are used here instead of pthread_cond_t.
	 */
	struct ev_async scheduler_async;
	struct ipc_cond scheduler_cond;
	/**
	 * A queue with all vy_task objects created by the
	 * scheduler and not yet taken by a worker.
	 */
	struct stailq input_queue;
	/**
	 * A queue of processed vy_tasks objects.
	 */
	struct stailq output_queue;
	/**
	 * A memory pool for vy_tasks.
	 */
	struct mempool task_pool;
};

static void
vy_scheduler_start(struct vy_scheduler *scheduler);
static void
vy_scheduler_stop(struct vy_scheduler *scheduler);

static void
vy_scheduler_async_cb(ev_loop *loop, struct ev_async *watcher, int events)
{
	(void) loop;
	(void) events;
	struct vy_scheduler *scheduler =
		container_of(watcher, struct vy_scheduler, scheduler_async);
	ipc_cond_signal(&scheduler->scheduler_cond);
}

static struct vy_scheduler *
vy_scheduler_new(struct vy_env *env)
{
	struct vy_scheduler *scheduler = calloc(1, sizeof(*scheduler));
	if (scheduler == NULL) {
		diag_set(OutOfMemory, sizeof(*scheduler), "scheduler",
			 "struct");
		return NULL;
	}
	tt_pthread_mutex_init(&scheduler->mutex, NULL);
	scheduler->checkpoint_lsn = 0;
	scheduler->checkpoint_lsn_last = 0;
	scheduler->checkpoint_in_progress = false;
	scheduler->indexes = NULL;
	scheduler->count = 0;
	scheduler->rr = 0;
	scheduler->env = env;
	rlist_create(&scheduler->shutdown);
	tt_pthread_cond_init(&scheduler->worker_cond, NULL);
	scheduler->loop = loop();
	ev_async_init(&scheduler->scheduler_async, vy_scheduler_async_cb);
	ipc_cond_create(&scheduler->scheduler_cond);
	mempool_create(&scheduler->task_pool, cord_slab_cache(),
			sizeof(struct vy_task));
	return scheduler;
}

static void
vy_scheduler_delete(struct vy_scheduler *scheduler)
{
	if (scheduler->is_worker_pool_running)
		vy_scheduler_stop(scheduler);

	mempool_destroy(&scheduler->task_pool);

	struct vy_index *index, *next;
	rlist_foreach_entry_safe(index, &scheduler->shutdown, link, next) {
		vy_index_delete(index);
	}
	free(scheduler->indexes);
	tt_pthread_cond_destroy(&scheduler->worker_cond);
	TRASH(&scheduler->scheduler_async);
	ipc_cond_destroy(&scheduler->scheduler_cond);
	tt_pthread_mutex_destroy(&scheduler->mutex);
	free(scheduler);
}

static int
vy_scheduler_add_index(struct vy_scheduler *scheduler, struct vy_index *index)
{
	struct vy_index **indexes =
		realloc(scheduler->indexes,
			(scheduler->count + 1) * sizeof(*indexes));
	if (indexes == NULL) {
		diag_set(OutOfMemory, sizeof((scheduler->count + 1) *
			 sizeof(*indexes)), "scheduler", "indexes");
		tt_pthread_mutex_unlock(&scheduler->mutex);
		return -1;
	}
	scheduler->indexes = indexes;
	scheduler->indexes[scheduler->count++] = index;
	vy_index_ref(index);
	/* Start scheduler threads on demand */
	if (!scheduler->is_worker_pool_running)
		vy_scheduler_start(scheduler);
	return 0;
}

static int
vy_scheduler_del_index(struct vy_scheduler *scheduler, struct vy_index *index)
{
	int found = 0;
	while (found < scheduler->count && scheduler->indexes[found] != index)
		found++;
	assert(found < scheduler->count);
	for (int i = found + 1; i < scheduler->count; i++)
		scheduler->indexes[i - 1] = scheduler->indexes[i];
	scheduler->count--;
	if (unlikely(scheduler->rr >= scheduler->count))
		scheduler->rr = 0;
	vy_index_unref(index);
	/* add index to `shutdown` list */
	rlist_add(&scheduler->shutdown, &index->link);
	return 0;
}

static inline int
vy_scheduler_peek_checkpoint(struct vy_scheduler *scheduler,
			     struct vy_index *index, int64_t checkpoint_lsn,
			     struct vy_task **ptask)
{
	/* try to peek a range which has min
	 * lsn <= required value
	*/
	struct vy_range *range;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.dump, pn))) {
		range = container_of(pn, struct vy_range, nodedump);
		if (range->i0.min_lsn > checkpoint_lsn)
			continue;
		if (range->flags & VY_LOCK)
			continue;
		if (range->used == 0)
			continue;
		struct vy_task *task = vy_task_new(&scheduler->task_pool,
						   index, &vy_task_dump_ops);
		if (task == NULL)
			return -1; /* OOM */
		vy_range_lock(range);
		task->range = range;
		task->range_mem = vy_range_rotate(range);
		*ptask = task;
		return 0; /* new task */
	}
	*ptask = NULL;
	return 0; /* nothing to do */
}

static inline int
vy_scheduler_peek_dump(struct vy_scheduler *scheduler, struct vy_index *index,
		       struct vy_task **ptask)
{
	/* try to peek a range with a biggest in-memory index */
	struct vy_range *range;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.dump, pn))) {
		range = container_of(pn, struct vy_range, nodedump);
		if (range->flags & VY_LOCK)
			continue;
		if (range->used < 10 * 1024 * 1024 &&
		    range->used < index->key_def->opts.range_size)
			return 0; /* nothing to do */
		struct vy_task *task = vy_task_new(&scheduler->task_pool,
						   index, &vy_task_dump_ops);
		if (task == NULL)
			return -1; /* oom */
		vy_range_lock(range);
		task->range = range;
		task->range_mem = vy_range_rotate(range);
		*ptask = task;
		return 0; /* new task */
	}
	*ptask = NULL;
	return 0; /* nothing to do */
}

static inline int
vy_scheduler_peek_age(struct vy_scheduler *scheduler, struct vy_index *index,
		      uint32_t max_age, struct vy_task **ptask)
{
	/*
	 * Try to peek a range with no updates within max_age
	 * seconds and dump it to free memory.
	 */

	/* full scan */
	uint64_t now = clock_monotonic64();
	struct vy_range *range = NULL;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.dump, pn))) {
		range = container_of(pn, struct vy_range, nodedump);
		if (range->flags & VY_LOCK)
			continue;
		if (range->used == 0)
			continue;
		if (range->update_time + max_age > now)
			continue;
		struct vy_task *task = vy_task_new(&scheduler->task_pool,
						   index, &vy_task_dump_ops);
		if (task == NULL)
			return -1; /* oom */
		vy_range_lock(range);
		task->range = range;
		task->range_mem = vy_range_rotate(range);
		*ptask = task;
		return 0; /* new task */
	}
	*ptask = NULL;
	return 0; /* nothing to do */
}

static inline int
vy_scheduler_peek_compact(struct vy_scheduler *scheduler,
			  struct vy_index *index, uint32_t run_count,
			  struct vy_task **ptask)
{
	/* try to peek a range with a biggest number
	 * of runs */
	struct vy_range *range;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.compact, pn))) {
		range = container_of(pn, struct vy_range, nodecompact);
		if (range->flags & VY_LOCK)
			continue;
		if (range->run_count < run_count)
			break; /* TODO: why ? */
		struct vy_task *task = vy_task_new(&scheduler->task_pool,
						   index, &vy_task_compact_ops);
		if (task == NULL)
			return -1; /* OOM */
		vy_range_lock(range);
		task->range = range;
		*ptask = task;
		return 0; /* new task */
	}
	*ptask = NULL;
	return 0; /* nothing to do */
}

static inline int
vy_scheduler_peek_shutdown(struct vy_scheduler *scheduler,
			   struct vy_index *index, struct vy_task **ptask)
{
	if (index->refs > 0) {
		*ptask = NULL;
		return 0; /* index still has tasks */
	}
	*ptask = vy_task_new(&scheduler->task_pool, index, &vy_task_drop_ops);
	if (*ptask == NULL)
		return -1;
	return 0; /* new task */
}

static int
vy_schedule_index(struct vy_scheduler *scheduler, struct srzone *zone,
		  int64_t vlsn, struct vy_index *index, struct vy_task **ptask)
{
	int rc;
	*ptask = NULL;

	/* checkpoint */
	if (scheduler->checkpoint_in_progress) {
		rc = vy_scheduler_peek_checkpoint(scheduler, index,
			scheduler->checkpoint_lsn, ptask);
		if (rc != 0)
			return rc; /* error */
		if (*ptask != NULL)
			goto found;
	}

	/* index aging */
	if (zone->dump_prio && zone->dump_age) {
		uint32_t max_age = zone->dump_age * 1000000; /* ms */
		rc = vy_scheduler_peek_age(scheduler, index, max_age, ptask);
		if (rc != 0)
			return rc; /* error */
		if (*ptask != NULL)
			goto found;
	}

	/* dumping */
	rc = vy_scheduler_peek_dump(scheduler, index, ptask);
	if (rc != 0)
		return rc; /* error */
	if (*ptask != NULL)
		goto found;

	/* compaction */
	rc = vy_scheduler_peek_compact(scheduler, index, zone->compact_wm,
				       ptask);
	if (rc != 0)
		return rc; /* error */
	if (*ptask != NULL)
		goto found;

	/* no task to run */
	return 0;
found:
	(*ptask)->vlsn = vlsn;
	return 0;
}

static int
vy_schedule(struct vy_scheduler *scheduler, struct srzone *zone, int64_t vlsn,
	    struct vy_task **ptask)
{
	/* Schedule all pending shutdowns. */
	struct vy_index *index, *n;
	rlist_foreach_entry_safe(index, &scheduler->shutdown, link, n) {
		*ptask = NULL;
		int rc = vy_scheduler_peek_shutdown(scheduler, index, ptask);
		if (rc < 0)
			return rc;
		if (*ptask == NULL)
			continue;
		/* Remove from scheduler->shutdown list */
		rlist_del(&index->link);
		return 0;
	}

	/* peek an index */
	*ptask = NULL;
	if (scheduler->count == 0)
		return 0;
	assert(scheduler->rr < scheduler->count);
	index = scheduler->indexes[scheduler->rr];
	scheduler->rr = (scheduler->rr + 1) % scheduler->count;

	int rc = vy_schedule_index(scheduler, zone, vlsn, index, ptask);
	return rc;
}

static int
vy_worker_f(va_list va);

static int
vy_scheduler_f(va_list va)
{
	struct vy_scheduler *scheduler = va_arg(va, struct vy_scheduler *);
	struct vy_env *env = scheduler->env;

	bool warning_said = false;
	while (scheduler->is_worker_pool_running) {
		/* Get task */
		struct vy_task *task = NULL;
		struct srzone *zone = sr_zoneof(env);
		int rc = vy_schedule(scheduler, zone, env->xm->vlsn, &task);
		if (rc != 0){
			/* Log error message once */
			if (! warning_said) {
				error_log(diag_last_error(diag_get()));
				warning_said = true;
			}
		}
		assert(rc == 0);

		tt_pthread_mutex_lock(&scheduler->mutex);

		/* Swap output queue */
		struct stailq output_queue;
		stailq_create(&output_queue);
		stailq_splice(&scheduler->output_queue,
			      stailq_first(&scheduler->output_queue),
			      &output_queue);

		if (task != NULL) {
			/* Queue task */
			bool was_empty = stailq_empty(&scheduler->input_queue);
			stailq_add_tail_entry(&scheduler->input_queue, task,
					      link);
			if (was_empty)                  /* Notify workers */
				tt_pthread_cond_signal(&scheduler->worker_cond);
			warning_said = false;
		}

		tt_pthread_mutex_unlock(&scheduler->mutex);

		/* Complete and delete all processed tasks */
		struct vy_task *next;
		stailq_foreach_entry_safe(task, next, &output_queue, link) {
			if (task->status == 0 &&
			    task->ops->complete && task->ops->complete(task))
				error_log(diag_last_error(diag_get()));
			vy_task_delete(&scheduler->task_pool, task);
		}

		if (!stailq_empty(&output_queue)) {
			/*
			 * At least one task has been processed and
			 * initial conditions may have been changed.
			 * For example, VY_DUMP task increases run_count and
			 * may be followed by VY_COMPACT task.
			 * Don't wait for a while and re-run scheduler
			 * on the next event loop iteration.
			 */
			fiber_reschedule();
			continue;
		}

		/*
		 * ipc_channel_get_timeout() is used to
		 * schedule periodic tasks, 5 seconds is
		 * enough for periodic.
		 */
		ipc_cond_wait_timeout(&scheduler->scheduler_cond, 5.0);
	}

	return 0;
}

static int
vy_worker_f(va_list va)
{
	struct vy_scheduler *scheduler = va_arg(va, struct vy_scheduler *);
	coeio_enable();
	bool warning_said = false;
	struct vy_task *task = NULL;

	tt_pthread_mutex_lock(&scheduler->mutex);
	while (scheduler->is_worker_pool_running) {
		/* Wait for a task */
		if (stailq_empty(&scheduler->input_queue)) {
			/* Wake scheduler up if there are no more tasks */
			ev_async_send(scheduler->loop,
				      &scheduler->scheduler_async);
			tt_pthread_cond_wait(&scheduler->worker_cond,
					     &scheduler->mutex);
			continue;
		}
		task = stailq_shift_entry(&scheduler->input_queue,
					  struct vy_task, link);
		tt_pthread_mutex_unlock(&scheduler->mutex);
		assert(task != NULL);

		/* Execute task */
		task->status = task->ops->execute(task);
		if (task->status != 0) {
			if (!warning_said) {
				error_log(diag_last_error(diag_get()));
				warning_said = true;
			}
		} else {
			warning_said = false;
		}

		/* Return processed task to scheduler */
		tt_pthread_mutex_lock(&scheduler->mutex);
		stailq_add_tail_entry(&scheduler->output_queue, task, link);
	}
	tt_pthread_mutex_unlock(&scheduler->mutex);
	return 0;
}

static void
vy_scheduler_start(struct vy_scheduler *scheduler)
{
	assert(!scheduler->is_worker_pool_running);

	/* Start worker threads */
	scheduler->is_worker_pool_running = true;
	scheduler->worker_pool_size = cfg_geti("vinyl.threads");
	if (scheduler->worker_pool_size < 0)
		scheduler->worker_pool_size = 1;
	scheduler->worker_pool = NULL;
	stailq_create(&scheduler->input_queue);
	stailq_create(&scheduler->output_queue);
	scheduler->worker_pool = (struct cord *)
		calloc(scheduler->worker_pool_size, sizeof(struct cord));
	if (scheduler->worker_pool == NULL)
		panic("failed to allocate vinyl worker pool");
	for (int i = 0; i < scheduler->worker_pool_size; i++) {
		cord_costart(&scheduler->worker_pool[i], "vinyl.worker",
			     vy_worker_f, scheduler);
	}

	/* Start scheduler fiber */
	ev_async_start(scheduler->loop, &scheduler->scheduler_async);
	scheduler->scheduler = fiber_new("vinyl.scheduler", vy_scheduler_f);
	if (scheduler->scheduler == NULL)
		panic("failed to start vinyl scheduler fiber");
	fiber_set_joinable(scheduler->scheduler, false);
	fiber_start(scheduler->scheduler, scheduler);
}

static void
vy_scheduler_stop(struct vy_scheduler *scheduler)
{
	assert(scheduler->is_worker_pool_running);

	/* Stop scheduler fiber */
	scheduler->is_worker_pool_running = false;
	ev_async_stop(scheduler->loop, &scheduler->scheduler_async);
	/* Sic: fiber_cancel() can't be used here */
	ipc_cond_signal(&scheduler->scheduler_cond);
	scheduler->scheduler = NULL;

	/* Delete all pending tasks and wake up worker threads */
	tt_pthread_mutex_lock(&scheduler->mutex);
	struct vy_task *task, *next;
	stailq_foreach_entry_safe(task, next, &scheduler->input_queue, link)
		vy_task_delete(&scheduler->task_pool, task);
	stailq_create(&scheduler->input_queue);
	pthread_cond_broadcast(&scheduler->worker_cond);
	tt_pthread_mutex_unlock(&scheduler->mutex);

	/* Join worker threads */
	for (int i = 0; i < scheduler->worker_pool_size; i++)
		cord_join(&scheduler->worker_pool[i]);
	free(scheduler->worker_pool);
	scheduler->worker_pool = NULL;
	scheduler->worker_pool_size = 0;

	/* Delete all processed tasks */
	stailq_foreach_entry_safe(task, next, &scheduler->output_queue, link)
		vy_task_delete(&scheduler->task_pool, task);
	stailq_create(&scheduler->output_queue);
}

/*
 * Schedule checkpoint. Please call vy_wait_checkpoint() after that.
 */
int
vy_checkpoint(struct vy_env *env)
{
	int64_t lsn = env->xm->lsn;
	struct vy_scheduler *scheduler = env->scheduler;
	/* do not initiate checkpoint during bootstrap,
	 * thread pool is not up yet */
	if (!scheduler->is_worker_pool_running)
		return 0;
	scheduler->checkpoint_lsn = lsn;
	scheduler->checkpoint_in_progress = true;
	/* Wake scheduler up */
	ipc_cond_signal(&scheduler->scheduler_cond);

	return 0;
}

void
vy_wait_checkpoint(struct vy_env *env, struct vclock *vclock)
{
	int64_t checkpoint_lsn = vclock_sum(vclock);
	struct vy_scheduler *scheduler = env->scheduler;
	for (;;) {
		bool is_active = false;
		/* iterate over all indexes */
		for (int i = 0; i < scheduler->count; i++) {
			/*
			 * check that all ranges of index have lsn
			 * greater than checkpoint_lsn
			 */
			struct vy_index *index = scheduler->indexes[i];
			struct vy_range *range;
			range = vy_range_tree_first(&index->tree);
			while (range) {
				is_active |= (range->i0.min_lsn <=
					      scheduler->checkpoint_lsn);
				range = vy_range_tree_next(&index->tree, range);
			}
		}
		if (!is_active)
			break;
		/* TODO: use channel here */
		fiber_sleep(.020);
	}

	for (int i = 0; i < scheduler->count; i++) {
		struct vy_index *index = scheduler->indexes[i];
		if (index->first_dump_lsn != checkpoint_lsn) {
			vy_index_checkpoint_range_index(index,
							checkpoint_lsn);
		}
	}

	scheduler->checkpoint_lsn_last = scheduler->checkpoint_lsn;
	scheduler->checkpoint_lsn = 0;
}

/**
 * Unlink old ranges - i.e. ranges which are not relevant
 * any more because of a passed range split, or create/drop
 * index.
 */
static void
vy_index_gc(struct vy_index *index)
{
	struct mh_i32ptr_t *ranges = NULL;
	DIR *dir = NULL;
	ranges = mh_i32ptr_new();

	if (ranges == NULL)
		goto error;
	/*
	 * Construct a hash map of existing ranges, to quickly
	 * find a valid range by range id.
	 */
	struct vy_range *range = vy_range_tree_first(&index->tree);
	while (range) {
		const struct mh_i32ptr_node_t node = {range->id, range};
		struct mh_i32ptr_node_t old, *p_old = &old;
		mh_int_t k = mh_i32ptr_put(ranges, &node, &p_old, NULL);
		if (k == mh_end(ranges))
			goto error;
		range = vy_range_tree_next(&index->tree, range);
	}
	/*
	 * Scan the index directory and unlink files not
	 * referenced from any valid range.
	 */
	dir = opendir(index->path);
	if (dir == NULL)
		goto error;
	struct dirent *dirent;
	/**
	 * @todo: only remove files matching the pattern *and*
	 * identified as old, not all files.
	 */
	while ((dirent = readdir(dir))) {
		if (!(strcmp(".", dirent->d_name)))
			continue;
		if (!(strcmp("..", dirent->d_name)))
			continue;
		bool is_vinyl_file = false;
		/*
		 * Now we can delete in progress file, this is bad
		if (strstr(dirent->d_name, ".tmp") == dirent->d_name) {
			is_vinyl_file = true;
		}
		*/
		if (strstr(dirent->d_name, ".index")) {
			is_vinyl_file = true;
			int64_t lsn = 0;
			sscanf(dirent->d_name, "%"SCNx64, &lsn);
			if (lsn >= index->first_dump_lsn)
				continue;
		}
		if (strstr(dirent->d_name, ".range")) {
			is_vinyl_file = true;
			uint64_t range_id = 0;
			sscanf(dirent->d_name, "%"SCNx64, &range_id);
			mh_int_t range = mh_i32ptr_find(ranges, range_id, NULL);
			if (range != mh_end(ranges))
				continue;
		}
		if (!is_vinyl_file)
			continue;
		char path[PATH_MAX];
		snprintf(path, PATH_MAX, "%s/%s",
			 index->path, dirent->d_name);
		unlink(path);
	}
	goto end;
error:
	say_syserror("failed to cleanup index directory %s", index->path);
end:
	closedir(dir);
	mh_i32ptr_delete(ranges);
}

void
vy_commit_checkpoint(struct vy_env *env, struct vclock *vclock)
{
	struct vy_scheduler *scheduler = env->scheduler;
	int64_t checkpoint_lsn = vclock_sum(vclock);
	for (int i = 0; i < scheduler->count; i++) {
		struct vy_index *index;
		index = scheduler->indexes[i];
		index->first_dump_lsn = checkpoint_lsn;
		vy_index_gc(index);
	}
}

/* Scheduler }}} */

/**
 * Global configuration of an entire vinyl instance (env object).
 */
struct vy_conf {
	/* path to vinyl_dir */
	char *path;
	/* compaction */
	struct srzonemap zones;
	/* memory */
	uint64_t memory_limit;
};

static struct vy_conf *
vy_conf_new()
{
	struct vy_conf *conf = calloc(1, sizeof(*conf));
	if (conf == NULL) {
		diag_set(OutOfMemory, sizeof(*conf), "conf", "struct");
		return NULL;
	}
	conf->path = strdup(cfg_gets("vinyl_dir"));
	if (conf->path == NULL) {
		diag_set(OutOfMemory, sizeof(*conf), "conf", "path");
		goto error_1;
	}
	/* Ensure vinyl data directory exists. */
	if (!path_exists(conf->path)) {
		vy_error("directory '%s' does not exist", conf->path);
		goto error_2;
	}
	conf->memory_limit = cfg_getd("vinyl.memory_limit")*1024*1024*1024;
	struct srzone def = {
		.compact_wm        = 2,
		.dump_prio       = 1,
		.dump_age        = 40,
	};
	struct srzone redzone = {
		.compact_wm        = 4,
		.dump_prio       = 0,
		.dump_age        = 0,
	};
	sr_zonemap_set(&conf->zones, 0, &def);
	sr_zonemap_set(&conf->zones, 80, &redzone);
	/* configure zone = 0 */
	struct srzone *z = &conf->zones.zones[0];
	z->compact_wm = cfg_geti("vinyl.compact_wm");
	if (z->compact_wm <= 1) {
		vy_error("bad %d.compact_wm value", 0);
		goto error_2;
	}
	z->dump_age = cfg_geti("vinyl.dump_age");

	return conf;

error_2:
	free(conf->path);
error_1:
	free(conf);
	return NULL;
}

static void vy_conf_delete(struct vy_conf *c)
{
	free(c->path);
	free(c);
}

static inline struct srzone *
sr_zoneof(struct vy_env *env)
{
	int p = vy_quota_used_percent(env->quota);
	return sr_zonemap(&env->conf->zones, p);
}

int
vy_index_read(struct vy_index*, struct vy_tuple*, enum vy_order order,
		struct vy_tuple **, struct vy_tx*);

/** {{{ Introspection */

static inline struct vy_info_node *
vy_info_append(struct vy_info_node *root, const char *key)
{
	assert(root->childs_n < root->childs_cap);
	struct vy_info_node *node = &root->childs[root->childs_n];
	root->childs_n++;
	node->key = key;
	node->val_type = VINYL_NODE;
	return node;
}

static inline void
vy_info_append_u32(struct vy_info_node *root, const char *key, uint32_t value)
{
	struct vy_info_node *node = vy_info_append(root, key);
	node->value.u32 = value;
	node->val_type = VINYL_U32;
}

static inline void
vy_info_append_u64(struct vy_info_node *root, const char *key, uint64_t value)
{
	struct vy_info_node *node = vy_info_append(root, key);
	node->value.u64 = value;
	node->val_type = VINYL_U64;
}

static inline void
vy_info_append_str(struct vy_info_node *root, const char *key,
		   const char *value)
{
	struct vy_info_node *node = vy_info_append(root, key);
	node->value.str = value;
	node->val_type = VINYL_STRING;
}

static inline int
vy_info_reserve(struct vy_info *info, struct vy_info_node *node, int size)
{
	node->childs = region_alloc(&info->allocator,
				    size * sizeof(*node->childs));
	if (node->childs == NULL) {
		diag_set(OutOfMemory, sizeof(*node), "vy_info_node",
			"node->childs");
		return -1;
	}
	memset(node->childs, 0, size * sizeof(*node->childs));
	node->childs_cap = size;
	return 0;
}

static inline int
vy_info_append_global(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_info_node *node = vy_info_append(root, "vinyl");
	if (vy_info_reserve(info, node, 4) != 0)
		return 1;
	vy_info_append_str(node, "path", info->env->conf->path);
	vy_info_append_str(node, "build", PACKAGE_VERSION);
	return 0;
}

static inline int
vy_info_append_memory(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_info_node *node = vy_info_append(root, "memory");
	if (vy_info_reserve(info, node, 2) != 0)
		return 1;
	struct vy_env *env = info->env;
	vy_info_append_u64(node, "used", vy_quota_used(env->quota));
	vy_info_append_u64(node, "limit", env->conf->memory_limit);
	return 0;
}

static inline int
vy_info_append_compaction(struct vy_info *info, struct vy_info_node *root)
{
	int childs_cnt = 0;
	struct vy_env *env = info->env;
	for (int i = 0; i < 11; ++i) {
		++childs_cnt;
	}
	struct vy_info_node *node = vy_info_append(root, "compaction");
	if (vy_info_reserve(info, node, childs_cnt) != 0)
		return 1;
	for (int i = 0; i < 11; ++i) {
		struct srzone *z = &env->conf->zones.zones[i];

		struct vy_info_node *local_node = vy_info_append(node, z->name);
		if (vy_info_reserve(info, local_node, 13) != 0)
			return 1;
		vy_info_append_u32(local_node, "compact_wm", z->compact_wm);
		vy_info_append_u32(local_node, "dump_age", z->dump_age);
	}
	return 0;
}

static inline int
vy_info_append_scheduler(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_info_node *node = vy_info_append(root, "scheduler");
	if (vy_info_reserve(info, node, 3) != 0)
		return 1;

	struct vy_env *env = info->env;
	int v = vy_quota_used_percent(env->quota);
	struct srzone *z = sr_zonemap(&env->conf->zones, v);
	vy_info_append_str(node, "zone", z->name);
	return 0;
}

static inline int
vy_info_append_performance(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_info_node *node = vy_info_append(root, "performance");
	if (vy_info_reserve(info, node, 26) != 0)
		return 1;

	struct vy_env *env = info->env;
	struct vy_stat *stat = env->stat;
	vy_stat_prepare(stat);
	vy_info_append_u64(node, "tx", stat->tx);
	vy_info_append_u64(node, "get", stat->get);
	vy_info_append_u64(node, "cursor", stat->cursor);
	vy_info_append_str(node, "tx_ops", stat->tx_stmts.sz);
	vy_info_append_str(node, "tx_latency", stat->tx_latency.sz);
	vy_info_append_str(node, "cursor_ops", stat->cursor_ops.sz);
	vy_info_append_u64(node, "write_count", stat->write_count);
	vy_info_append_str(node, "get_latency", stat->get_latency.sz);
	vy_info_append_u64(node, "tx_rollback", stat->tx_rlb);
	vy_info_append_u64(node, "tx_conflict", stat->tx_conflict);
	vy_info_append_u32(node, "tx_active_rw", env->xm->count_rw);
	vy_info_append_u32(node, "tx_active_ro", env->xm->count_rd);
	vy_info_append_str(node, "get_read_disk", stat->get_read_disk.sz);
	vy_info_append_str(node, "get_read_cache", stat->get_read_cache.sz);
	vy_info_append_str(node, "cursor_latency", stat->cursor_latency.sz);
	return 0;
}

static inline int
vy_info_append_metric(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_info_node *node = vy_info_append(root, "metric");
	if (vy_info_reserve(info, node, 2) != 0)
		return 1;

	vy_info_append_u64(node, "lsn", info->env->xm->lsn);
	return 0;
}

static inline int
vy_info_append_indices(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_index *o;
	int indices_cnt = 0;
	rlist_foreach_entry(o, &info->env->indexes, link) {
		++indices_cnt;
	}
	struct vy_info_node *node = vy_info_append(root, "db");
	if (vy_info_reserve(info, node, indices_cnt) != 0)
		return 1;
	rlist_foreach_entry(o, &info->env->indexes, link) {
		vy_profiler_begin(&o->rtp, o);
		vy_profiler_(&o->rtp);
		vy_profiler_end(&o->rtp);
		struct vy_info_node *local_node = vy_info_append(node, o->name);
		if (vy_info_reserve(info, local_node, 19) != 0)
			return 1;
		vy_info_append_u64(local_node, "size", o->rtp.total_range_size);
		vy_info_append_u64(local_node, "count", o->rtp.count);
		vy_info_append_u64(local_node, "count_dup", o->rtp.count_dup);
		vy_info_append_u64(local_node, "read_disk", o->rtp.read_disk);
		vy_info_append_u32(local_node, "page_count", o->rtp.total_page_count);
		vy_info_append_u64(local_node, "read_cache", o->rtp.read_cache);
		vy_info_append_u32(local_node, "range_count", o->rtp.total_range_count);
		vy_info_append_u32(local_node, "run_avg", o->rtp.total_run_avg);
		vy_info_append_u32(local_node, "run_max", o->rtp.total_run_max);
		vy_info_append_u64(local_node, "memory_used", o->rtp.memory_used);
		vy_info_append_u32(local_node, "run_count", o->rtp.total_run_count);
		vy_info_append_u32(local_node, "temperature_avg", o->rtp.temperature_avg);
		vy_info_append_u32(local_node, "temperature_min", o->rtp.temperature_min);
		vy_info_append_u32(local_node, "temperature_max", o->rtp.temperature_max);
		vy_info_append_str(local_node, "run_histogram", o->rtp.histogram_run_ptr);
		vy_info_append_u64(local_node, "size_uncompressed", o->rtp.total_range_origin_size);
		vy_info_append_u64(local_node, "size_uncompressed", o->rtp.total_range_origin_size);
		vy_info_append_u64(local_node, "range_size", o->key_def->opts.range_size);
		vy_info_append_u64(local_node, "page_size", o->key_def->opts.range_size);
	}
	return 0;
}

int
vy_info_create(struct vy_info *info, struct vy_env *e)
{
	memset(info, 0, sizeof(*info));
	info->env = e;
	region_create(&info->allocator, cord_slab_cache());
	struct vy_info_node *root = &info->root;
	if (vy_info_reserve(info, root, 7) != 0 ||
	    vy_info_append_indices(info, root) != 0 ||
	    vy_info_append_global(info, root) != 0 ||
	    vy_info_append_memory(info, root) != 0 ||
	    vy_info_append_metric(info, root) != 0 ||
	    vy_info_append_scheduler(info, root) != 0 ||
	    vy_info_append_compaction(info, root) != 0 ||
	    vy_info_append_performance(info, root) != 0) {
		region_destroy(&info->allocator);
		return 1;
	}
	return 0;
}

void
vy_info_destroy(struct vy_info *info)
{
	region_destroy(&info->allocator);
	TRASH(info);
}

/** }}} Introspection */

/* {{{ Cursor */

struct vy_cursor *
vy_cursor_new(struct vy_tx *tx, struct vy_index *index, const char *key,
	      uint32_t part_count, enum vy_order order)
{
	struct vy_env *e = index->env;
	struct vy_cursor *c = mempool_alloc(&e->cursor_pool);
	if (c == NULL) {

		diag_set(OutOfMemory, sizeof(*c), "cursor", "cursor pool");
		return NULL;
	}
	c->key = vy_tuple_from_key(index, key, part_count);
	if (c->key == NULL) {
		mempool_free(&e->cursor_pool, c);
		return NULL;
	}
	c->index = index;
	c->n_reads = 0;
	c->order = order;
	if (tx == NULL) {
		tx = &c->tx_autocommit;
		vy_tx_begin(e->xm, tx, VINYL_TX_RO);
	} else {
		rlist_add(&tx->cursors, &c->next_in_tx);
	}
	c->tx = tx;
	return c;
}

void
vy_cursor_delete(struct vy_cursor *c)
{
	struct vy_env *e = c->index->env;
	if (c->tx != NULL) {
		if (c->tx == &c->tx_autocommit) {
			/* Rollback the automatic transaction. */
			vy_tx_rollback(c->index->env, c->tx);
		} else {
			/*
			 * Delete itself from the list of open cursors
			 * in the transaction
			 */
			rlist_del(&c->next_in_tx);
		}
	}
	if (c->key)
		vy_tuple_unref(c->key);
	vy_stat_cursor(e->stat, c->tx->start, c->n_reads);
	TRASH(c);
	mempool_free(&e->cursor_pool, c);
}

/*** }}} Cursor */

static int
vy_index_conf_create(struct vy_index *conf, struct key_def *key_def)
{
	/* compression */
	if (key_def->opts.compression[0] != '\0' &&
	    strcmp(key_def->opts.compression, "none")) {
		conf->compression_if = vy_filter_of(key_def->opts.compression);
		if (conf->compression_if == NULL) {
			vy_error("unknown compression type '%s'",
				 key_def->opts.compression);
			return -1;
		}
	}
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 "/%" PRIu32,
	         key_def->space_id, key_def->iid);
	conf->name = strdup(name);
	/* path */
	if (key_def->opts.path[0] == '\0') {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%" PRIu32 "/%" PRIu32,
			 cfg_gets("vinyl_dir"), key_def->space_id,
			 key_def->iid);
		conf->path = strdup(path);
	} else {
		conf->path = strdup(key_def->opts.path);
	}
	if (conf->name == NULL || conf->path == NULL) {
		if (conf->name)
			free(conf->name);
		if (conf->path)
			free(conf->path);
		conf->name = NULL;
		conf->path = NULL;
		diag_set(OutOfMemory, strlen(key_def->opts.path),
			 "strdup", "char *");
		return -1;
	}
	if (key_def->opts.range_size == 0)
		key_def->opts.range_size = cfg_geti("vinyl.range_size");
	if (key_def->opts.page_size == 0)
		key_def->opts.page_size = cfg_geti("vinyl.page_size");
	return 0;
}

static int
vy_index_dump_range_index(struct vy_index *index)
{
	if (index->range_id_max == index->last_dump_range_id)
		return 0;
	long int ranges_size = index->range_count * sizeof(int64_t);
	int64_t *ranges = (int64_t *)malloc(ranges_size);
	if (!ranges) {
		vy_error("Can't alloc %li bytes", (long int)ranges_size);
		return -1;
	}
	int range_no = 0;
	struct vy_range *range = vy_range_tree_first(&index->tree);
	do {
		if (!range->run_count) {
			continue;		/* Skip empty ranges */
		}
		ranges[range_no] = range->id;
		++range_no;
	} while ((range = vy_range_tree_next(&index->tree, range)));

	if (!range_no) {
		/*
		 * This index is entirely empty, we won't create
		 * any files on disk.
		 */
		free(ranges);
		return 0;
	}

	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/.tmpXXXXXX", index->path);
	int fd = mkstemp(path);
	if (fd == -1) {
		vy_error("Can't create temporary file in %s: %s",
			 index->path, strerror(errno));
		free(ranges);
		return -1;
	}
	int write_size = sizeof(uint64_t) * range_no;
	if (write(fd, ranges, write_size) != write_size) {
		free(ranges);
		close(fd);
		unlink(path);
		vy_error("Can't write index file: %s", strerror(errno));
		return -1;
	}
	free(ranges);
	fsync(fd);
	close(fd);

	char new_path[PATH_MAX];
	snprintf(new_path, PATH_MAX, "%s/%016"PRIu64".%016"PRIx64".index",
		 index->path, index->first_dump_lsn,
		 index->range_id_max);
	if (link(path, new_path)) {
		vy_error("Can't dump index range dict %s: %s",
			 new_path, strerror(errno));
		unlink(path);
		return -1;
	}
	index->last_dump_range_id = index->range_id_max;
	unlink(path);
	return 0;
}

/**
 * Link the range index file to the latest checkpoint LSN.
 */

static int
vy_index_checkpoint_range_index(struct vy_index *index, int64_t lsn)
{
	char new_path[PATH_MAX];
	snprintf(new_path, PATH_MAX, "%s/%016"PRIu64".%016"PRIx64".index",
		 index->path, lsn,
		 index->last_dump_range_id);
	if (!index->first_dump_lsn) {
		/* index is empty, just create an empty file */
		int fd = open(new_path, O_CREAT | O_RDWR | O_TRUNC,
			      S_IRUSR | S_IWUSR | S_IWGRP);
		if (fd < 0) {
			vy_error("File %s create error: %s",
				  new_path, strerror(errno));
			return -1;
		}
		close(fd);
		return 0;
	}
	char old_path[PATH_MAX];
	snprintf(old_path, PATH_MAX, "%s/%016"PRIu64".%016"PRIx64".index",
		 index->path, index->first_dump_lsn,
		 index->last_dump_range_id);
	if (link(old_path, new_path)) {
		return -1;
	}
	return 0;
}


/**
 * Check whether or not an index was created after the
 * given LSN.
 * @note: the index may have been dropped afterwards, and
 * we don't track this fact anywhere except the write
 * ahead log.
 *
 * @note: this function simply reports that the index
 * does not exist if it encounters a read error. It's
 * assumed that the error will be taken care of when
 * someone tries to create the index.
 */
static bool
vy_index_exists(struct vy_index *index, int64_t lsn)
{
	if (!path_exists(index->path))
		return false;
	DIR *dir = opendir(index->path);
	if (!dir) {
		return false;
	}
	/*
	 * Try to find an index file with a number in name
	 * greater or equal than the passed LSN.
	 */
	char target_name[PATH_MAX];
	snprintf(target_name, PATH_MAX, "%016"PRIu64, lsn);
	struct dirent *dirent;
	while ((dirent = readdir(dir))) {
		if (strstr(dirent->d_name, ".index") &&
			strcmp(dirent->d_name, target_name) > 0) {
			break;
		}
	}
	closedir(dir);
	return dirent != NULL;
}

/**
 * Detect whether we already have non-garbage index files,
 * and open an existing index if that's the case. Otherwise,
 * create a new index. Take the current recovery status into
 * account.
 */
static int
vy_index_open_or_create(struct vy_index *index)
{
	/*
	 * TODO: don't drop/recreate index in local wal
	 * recovery mode if all operations already done.
	 */
	if (index->env->status == VINYL_ONLINE) {
		/*
		 * The recovery is complete, simply
		 * create a new index.
		 */
		return vy_index_create(index);
	}
	if (index->env->status == VINYL_INITIAL_RECOVERY) {
		/*
		 * A local or remote snapshot recovery. For
		 * a local snapshot recovery, local checkpoint LSN
		 * is non-zero, while for a remote one (new
		 * replica bootstrap) it is zero. In either case
		 * the engine is being fed rows from  system spaces.
		 *
		 * If this is a recovery from a non-empty local
		 * snapshot (lsn != 0), we should have index files
		 * nicely put on disk.
		 *
		 * Otherwise, the index files do not exist
		 * locally, and we should create the index
		 * directory from scratch.
		 */
		return index->env->xm->lsn ?
			vy_index_open_ex(index) : vy_index_create(index);
	}
	/*
	 * Case of a WAL replay from either a local or remote
	 * master.
	 * If it is a remote WAL replay, there should be no
	 * local files for this index yet - it's just being
	 * created.
	 *
	 * For a local recovery, however, the index may or may not
	 * have any files on disk, depending on whether we dumped
	 * any rows of this index after it had been created and
	 * before shutdown.
	 * Moreover, even when the index directory is not empty,
	 * we need to be careful to not open files from the
	 * previous incarnation of this index. Imagine the case
	 * when the index was created, dropped, and created again
	 * - all without a checkpoint. In this case the index
	 * directory may contain files from the dropped index
	 * and we need to be careful to not use them. Fortunately,
	 * we can rely on the current LSN to check whether
	 * the files we're looking at belong to this incarnation
	 * of the index or not, since file names always contain
	 * this LSN.
	 */
	if (vy_index_exists(index, index->env->xm->lsn)) {
		/*
		 * We found a file with LSN greater or equal
		 * that the "index recovery" lsn.
		 */
		return vy_index_open_ex(index);
	}
	return vy_index_create(index);
}

int
vy_index_open(struct vy_index *index)
{
	if (vy_index_open_or_create(index) ||
	    vy_scheduler_add_index(index->env->scheduler, index))
		return -1;
	return 0;
}

static void
vy_index_ref(struct vy_index *index)
{
	tt_pthread_mutex_lock(&index->ref_lock);
	index->refs++;
	tt_pthread_mutex_unlock(&index->ref_lock);
}

static void
vy_index_unref(struct vy_index *index)
{
	/* reduce reference counter */
	tt_pthread_mutex_lock(&index->ref_lock);
	assert(index->refs > 0);
	--index->refs;
	tt_pthread_mutex_unlock(&index->ref_lock);
	/* index will be deleted by scheduler if ref == 0 */
}

int
vy_index_drop(struct vy_index *index)
{
	/* TODO:
	 * don't drop/recreate index in local wal recovery mode if all
	 * operations are already done.
	 */
	struct vy_env *e = index->env;
	rlist_del(&index->link);
	/* schedule index shutdown or drop */
	vy_scheduler_del_index(e->scheduler, index);
	return 0;
}

struct vy_index *
vy_index_new(struct vy_env *e, struct key_def *key_def,
		struct tuple_format *tuple_format)
{
	assert(key_def->part_count > 0);
	struct vy_index *index = malloc(sizeof(struct vy_index));
	if (index == NULL) {
		diag_set(OutOfMemory, sizeof(struct vy_index),
			 "malloc", "struct vy_index");
		return NULL;
	}
	memset(index, 0, sizeof(*index));
	index->env = e;
	if (vy_planner_create(&index->p))
		goto error_1;
	if (vy_index_conf_create(index, key_def))
		goto error_2;
	index->key_def = key_def_dup(key_def);
	if (index->key_def == NULL)
		goto error_3;
	index->tuple_format = tuple_format;
	tuple_format_ref(index->tuple_format, 1);

	/*
	 * Create field_id -> part_id mapping used by vy_tuple_from_data().
	 * This code partially duplicates tuple_format_new() logic.
	 */
	uint32_t key_map_size = 0;
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		uint32_t field_id = key_def->parts[part_id].fieldno;
		key_map_size = MAX(key_map_size, field_id + 1);
	}
	index->key_map = calloc(key_map_size, sizeof(*index->key_map));
	if (index->key_map == NULL) {
		diag_set(OutOfMemory, sizeof(*index->key_map),
			 "calloc", "uint32_t *");
		goto error_4;
	}
	index->key_map_size = key_map_size;
	for (uint32_t field_id = 0; field_id < key_map_size; field_id++) {
		index->key_map[field_id] = UINT32_MAX;
	}
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		uint32_t field_id = key_def->parts[part_id].fieldno;
		assert(index->key_map[field_id] == UINT32_MAX);
		index->key_map[field_id] = part_id;
	}

	vy_range_tree_new(&index->tree);
	index->range_index_version = 0;
	rlist_create(&index->link);
	index->size = 0;
	index->read_disk = 0;
	index->read_cache = 0;
	index->range_count = 0;
	tt_pthread_mutex_init(&index->ref_lock, NULL);
	index->refs = 0; /* referenced by scheduler */
	read_set_new(&index->read_set);
	rlist_add(&e->indexes, &index->link);

	return index;

error_4:
	tuple_format_ref(index->tuple_format, -1);
	key_def_delete(index->key_def);
error_3:
	free(index->name);
	free(index->path);
error_2:
	vy_planner_destroy(&index->p);
error_1:
	free(index);
	return NULL;
}

static inline void
vy_index_delete(struct vy_index *index)
{
	read_set_iter(&index->read_set, NULL, read_set_delete_cb, NULL);
	vy_range_tree_iter(&index->tree, NULL, vy_range_tree_free_cb, index->env);
	vy_planner_destroy(&index->p);
	tt_pthread_mutex_destroy(&index->ref_lock);
	free(index->name);
	free(index->path);
	free(index->key_map);
	key_def_delete(index->key_def);
	tuple_format_ref(index->tuple_format, -1);
	TRASH(index);
	free(index);
}

size_t
vy_index_bsize(struct vy_index *index)
{
	vy_profiler_begin(&index->rtp, index);
	vy_profiler_(&index->rtp);
	vy_profiler_end(&index->rtp);
	return index->rtp.memory_used;
}

/* {{{ Tuple */

enum {
	VY_TUPLE_KEY_MISSING = UINT32_MAX,
};

static inline uint32_t
vy_tuple_size(struct vy_tuple *v)
{
	return sizeof(struct vy_tuple) + v->size;
}

static struct vy_tuple *
vy_tuple_alloc(uint32_t size)
{
	struct vy_tuple *v = malloc(sizeof(struct vy_tuple) + size);
	if (unlikely(v == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_tuple) + size,
			 "malloc", "struct vy_tuple");
		return NULL;
	}
	v->size      = size;
	v->lsn       = 0;
	v->flags     = 0;
	v->refs      = 1;
	return v;
}

void
vy_tuple_delete(struct vy_tuple *tuple)
{
#ifndef NDEBUG
	memset(tuple, '#', vy_tuple_size(tuple)); /* fail early */
#endif
	free(tuple);
}

static struct vy_tuple *
vy_tuple_from_key(struct vy_index *index, const char *key, uint32_t part_count)
{
	struct key_def *key_def = index->key_def;
	assert(part_count == 0 || key != NULL);
	assert(part_count <= key_def->part_count);

	/* Calculate key length */
	const char *key_end = key;
	for (uint32_t i = 0; i < part_count; i++)
		mp_next(&key_end);

	/* Allocate tuple */
	uint32_t offsets_size = sizeof(uint32_t) * (key_def->part_count + 1);
	uint32_t key_size = key_end - key;
	uint32_t size = offsets_size + mp_sizeof_array(part_count) + key_size;
	struct vy_tuple *tuple = vy_tuple_alloc(size);
	if (tuple == NULL)
		return NULL;

	/* Calculate offsets for key parts */
	uint32_t *offsets = (uint32_t *) tuple->data;
	const char *key_pos = key;
	uint32_t part_offset = offsets_size + mp_sizeof_array(part_count);
	for (uint32_t i = 0; i < part_count; i++) {
		const char *part_start = key_pos;
		offsets[i] = part_offset;
		mp_next(&key_pos);
		part_offset += (key_pos - part_start);
	}
	assert(part_offset == size);
	/* Fill offsets for missing key parts + value */
	for (uint32_t i = part_count; i < key_def->part_count; i++)
		offsets[i] = VY_TUPLE_KEY_MISSING; /* part is missing */

	/* Copy MsgPack data */
	char *data = tuple->data + offsets_size;
	data = mp_encode_array(data, part_count);
	memcpy(data, key, key_size);
	data += key_size;
	/* Store offset of the end of msgpack data in the last entry */
	offsets[key_def->part_count] = size;
	assert(data == tuple->data + size);

	return tuple;
}

static struct vy_tuple *
vy_tuple_from_data_ex(struct vy_index *index,
			 const char *data, const char *data_end,
			 uint32_t extra_size, char **extra)
{
#ifndef NDEBUG
	const char *data_end_must_be = data;
	mp_next(&data_end_must_be);
	assert(data_end == data_end_must_be);
#endif
	struct key_def *key_def = index->key_def;

	uint32_t field_count = mp_decode_array(&data);
	assert(field_count >= key_def->part_count);

	/* Allocate tuple */
	uint32_t offsets_size = sizeof(uint32_t) * (key_def->part_count + 1);
	uint32_t data_size = data_end - data;
	uint32_t size = offsets_size + mp_sizeof_array(field_count) +
		data_size + extra_size;
	struct vy_tuple *tuple = vy_tuple_alloc(size);
	if (tuple == NULL)
		return NULL;

	/* Calculate offsets for key parts */
	uint32_t *offsets = (uint32_t *) tuple->data;
	uint32_t start_offset = offsets_size + mp_sizeof_array(field_count);
	const char *data_pos = data;
	for (uint32_t field_id = 0; field_id < field_count; field_id++) {
		const char *field = data_pos;
		mp_next(&data_pos);
		if (field_id >= index->key_map_size ||
		    index->key_map[field_id] == UINT32_MAX)
			continue; /* field is not indexed */
		/* Update offsets for indexed field */
		uint32_t part_id = index->key_map[field_id];
		assert(part_id < key_def->part_count);
		offsets[part_id] = start_offset + (field - data);
	}
	/* Store offset of the end of msgpack data in the last entry */
	offsets[key_def->part_count] = start_offset + (data_pos - data);
	assert(offsets[key_def->part_count] + extra_size == size);

	/* Copy MsgPack data */
	char *wpos = tuple->data + offsets_size;
	wpos = mp_encode_array(wpos, field_count);
	memcpy(wpos, data, data_size);
	wpos += data_size;
	assert(wpos == tuple->data + size - extra_size);
	*extra = wpos;
	return tuple;
}

/*
 * Create vy_tuple from raw MsgPack data.
 */
static struct vy_tuple *
vy_tuple_from_data(struct vy_index *index,
		      const char *data, const char *data_end)
{
	char *unused;
	return vy_tuple_from_data_ex(index, data, data_end, 0, &unused);
}

static struct vy_tuple *
vy_tuple_extract_key_raw(struct vy_index *index, const char *tuple)
{
	uint32_t part_count = index->key_def->part_count;
	uint32_t *offsets = (uint32_t *) tuple;
	uint32_t offsets_size = sizeof(uint32_t) * (part_count + 1);
	const char *mp = tuple + offsets_size;
	const char *mp_end = tuple + offsets[part_count];
	return vy_tuple_from_data(index, mp, mp_end);
}

static const char *
vy_tuple_data(struct vy_index *index, struct vy_tuple *tuple,
		 uint32_t *mp_size)
{
	uint32_t part_count = index->key_def->part_count;
	uint32_t *offsets = (uint32_t *) tuple->data;
	uint32_t offsets_size = sizeof(uint32_t) * (part_count + 1);
	const char *mp = tuple->data + offsets_size;
	const char *mp_end = tuple->data + offsets[part_count];
	assert(mp < mp_end);
	*mp_size = mp_end - mp;
	return mp;
}

static void
vy_tuple_data_ex(const struct key_def *key_def,
		    const char *data, const char *data_end,
		    const char **msgpack, const char **msgpack_end,
		    const char **extra, const char **extra_end)
{
	uint32_t part_count = key_def->part_count;
	uint32_t *offsets = (uint32_t *) data;
	uint32_t offsets_size = sizeof(uint32_t) * (part_count + 1);
	*msgpack = data + offsets_size;
	*msgpack_end = data + offsets[part_count];
	*extra = *msgpack_end;
	*extra_end = data_end;
}

static struct tuple *
vy_convert_tuple(struct vy_index *index, struct vy_tuple *vy_tuple)
{
	uint32_t bsize;
	const char *data = vy_tuple_data(index, vy_tuple, &bsize);
	return box_tuple_new(index->tuple_format, data, data + bsize);
}

static void
vy_tuple_ref(struct vy_tuple *v)
{
	uint16_t old_refs =
		pm_atomic_fetch_add_explicit(&v->refs, 1,
					     pm_memory_order_relaxed);
	if (old_refs == 0)
		panic("this is broken by design");
}

static void
vy_tuple_unref(struct vy_tuple *tuple)
{
	uint16_t old_refs = pm_atomic_fetch_sub_explicit(&tuple->refs, 1,
		pm_memory_order_relaxed);
	assert(old_refs > 0);
	if (likely(old_refs > 1))
		return;

	vy_tuple_delete(tuple);
}

/**
 * Extract key from tuple by part_id
 */
static inline const char *
vy_tuple_key_part(const char *tuple_data, uint32_t part_id)
{
	uint32_t *offsets = (uint32_t *) tuple_data;
	uint32_t offset = offsets[part_id];
	if (offset == VY_TUPLE_KEY_MISSING)
		return NULL;
	return tuple_data + offset;
}

/**
 * Determine if the key has no missing parts,
 *  i.e. it is not a key of range select
 */
static bool
vy_tuple_key_is_full(const char *tuple_data, const struct key_def *key_def)
{
	uint32_t *offsets = (uint32_t *) tuple_data;
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		if (offsets[part_id] == VY_TUPLE_KEY_MISSING)
			return false;
	}
	return true;
}

/**
 * Compare two tuples
 */
static inline int
vy_tuple_compare(const char *tuple_data_a, const char *tuple_data_b,
		 const struct key_def *key_def)
{
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		const struct key_part *part = &key_def->parts[part_id];
		const char *field_a = vy_tuple_key_part(tuple_data_a, part_id);
		const char *field_b = vy_tuple_key_part(tuple_data_b, part_id);
		if (field_a == NULL || field_b == NULL)
			break; /* no more parts in the key */
		int rc = tuple_compare_field(field_a, field_b, part->type);
		if (rc != 0)
			return rc;
	}
	return 0;
}


/* }}} Tuple */

/** {{{ Upsert */

static void *
vy_update_alloc(void *arg, size_t size)
{
	(void) arg;
	/* TODO: rewrite tuple_upsert_execute() without exceptions */
	void *data = box_txn_alloc(size);
	if (data == NULL)
		diag_raise();
	return data;
}

/**
 * vinyl wrapper of tuple_upsert_execute.
 * vibyl upsert opts are slightly different from tarantool ops,
 *  so they need some preparation before tuple_upsert_execute call.
 *  The function does this preparation.
 * On successfull upsert the result is placed into tuple and tuple_end args.
 * On fail the tuple and tuple_end args are not changed.
 * Possibly allocates new tuple via fiber region alloc,
 * so call fiber_gc() after usage
 */
static void
vy_apply_upsert_ops(const char **tuple, const char **tuple_end,
		    const char *ops, const char *ops_end,
		    bool suppress_error)
{
	if (ops == ops_end)
		return;
	uint64_t series_count = mp_decode_uint(&ops);
	for (uint64_t i = 0; i < series_count; i++) {
		int index_base = mp_decode_uint(&ops);
		const char *serie_end;
		if (i == series_count - 1) {
			serie_end = ops_end;
		} else {
			serie_end = ops;
			mp_next(&serie_end);
		}
#ifndef NDEBUG
		if (i == series_count - 1) {
			const char *serie_end_must_be = ops;
			mp_next(&serie_end_must_be);
			assert(serie_end == serie_end_must_be);
		}
#endif
		const char *result;
		uint32_t size;
		result = tuple_upsert_execute(vy_update_alloc, NULL,
					      ops, serie_end,
					      *tuple, *tuple_end,
					      &size, index_base, suppress_error);
		if (result != NULL) {
			/* if failed, just skip it and leave tuple the same */
			*tuple = result;
			*tuple_end = result + size;
		}
		ops = serie_end;
	}
}

extern const char *
space_name_by_id(uint32_t id);

/*
 * Get the upserted tuple by upsert tuple and original tuple
 */
static struct vy_tuple *
vy_apply_upsert(struct vy_tuple *new_tuple, struct vy_tuple *old_tuple,
		struct vy_index *index, bool suppress_error)
{
	/*
	 * old_tuple - previous (old) version of tuple
	 * new_tuple - next (new) version of tuple
	 * result_tuple - the result of merging new and old
	 */
	assert(new_tuple != NULL);
	assert(new_tuple != old_tuple);
	struct key_def *key_def = index->key_def;

	/*
	 * Unpack UPSERT operation from the new tuple
	 */
	const char *new_data = new_tuple->data;
	const char *new_data_end = new_data + new_tuple->size;
	const char *new_mp, *new_mp_end, *new_ops, *new_ops_end;
	vy_tuple_data_ex(key_def, new_data, new_data_end,
			    &new_mp, &new_mp_end,
			    &new_ops, &new_ops_end);
	if (old_tuple == NULL || old_tuple->flags & SVDELETE) {
		/*
		 * INSERT case: return new tuple.
		 */
		return vy_tuple_from_data(index, new_mp, new_mp_end);
	}

	/*
	 * Unpack UPSERT operation from the old tuple
	 */
	assert(old_tuple != NULL);
	const char *old_data = old_tuple->data;
	const char *old_data_end = old_data + old_tuple->size;
	const char *old_mp, *old_mp_end, *old_ops, *old_ops_end;
	vy_tuple_data_ex(key_def, old_data, old_data_end,
			    &old_mp, &old_mp_end, &old_ops, &old_ops_end);

	/*
	 * Apply new operations to the old tuple
	 */
	const char *result_mp = old_mp;
	const char *result_mp_end = old_mp_end;
	struct vy_tuple *result_tuple;
	vy_apply_upsert_ops(&result_mp, &result_mp_end, new_ops, new_ops_end,
			    suppress_error);
	if (!(old_tuple->flags & SVUPSERT)) {
		/*
		 * UPDATE case: return the updated old tuple.
		 */
		assert(old_ops_end - old_ops == 0);
		result_tuple = vy_tuple_from_data(index, result_mp,
						     result_mp_end);
		if (result_tuple == NULL)
			return NULL; /* OOM */
		goto check_key;
	}

	/*
	 * UPSERT + UPSERT case: combine operations
	 */
	assert(old_ops_end - old_ops > 0);
	uint64_t ops_series_count = mp_decode_uint(&new_ops) +
				    mp_decode_uint(&old_ops);
	uint32_t total_ops_size = mp_sizeof_uint(ops_series_count) +
				  (new_ops_end - new_ops) +
				  (old_ops_end - old_ops);
	char *extra;
	result_tuple = vy_tuple_from_data_ex(index, result_mp,
		result_mp_end, total_ops_size, &extra);
	if (result_tuple == NULL)
		return NULL; /* OOM */
	extra = mp_encode_uint(extra, ops_series_count);
	memcpy(extra, old_ops, old_ops_end - old_ops);
	extra += old_ops_end - old_ops;
	memcpy(extra, new_ops, new_ops_end - new_ops);
	result_tuple->flags = SVUPSERT;

check_key:
	/*
	 * Check that key hasn't been changed after applying operations.
	 */
	if (key_def->iid == 0 &&
	    vy_tuple_compare(old_data, result_tuple->data, key_def) != 0) {
		/*
		 * Key has been changed: ignore this UPSERT and
		 * @retval the old tuple.
		 */
		diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			 key_def->name, space_name_by_id(key_def->space_id));
		error_log(diag_last_error(diag_get()));
		vy_tuple_unref(result_tuple);
		return vy_tuple_from_data(index, old_mp, old_mp_end);
	}
	return result_tuple;
}

/* }}} Upsert */

static inline void
vy_tx_set(struct vy_tx *tx, struct vy_index *index,
	    struct vy_tuple *tuple, uint8_t flags)
{
	tuple->flags = flags;
	/* Update concurrent index */
	struct txv *old = write_set_search_key(&tx->write_set, index,
					       tuple->data);
	/* Found a match of the previous action of this transaction */
	if (old != NULL) {
		if (tuple->flags & SVUPSERT) {
			if (old->tuple->flags & (SVUPSERT | SVREPLACE
			    | SVDELETE)) {

				struct vy_tuple *old_tuple = old->tuple;
				struct vy_tuple *new_tuple = tuple;
				tuple = vy_apply_upsert(new_tuple, old_tuple,
							index, true);
				if (!tuple->flags)
					tuple->flags = SVREPLACE;
			}
		}
		vy_tuple_unref(old->tuple);
		vy_tuple_ref(tuple);
		old->tuple = tuple;
	} else {
		/* Allocate a MVCC container. */
		struct txv *v = txv_new(index, tuple, tx);
		v->is_read = false;
		write_set_insert(&tx->write_set, v);
		tx->write_set_version++;
		stailq_add_tail_entry(&tx->log, v, next_in_log);
	}
}

/* {{{ Public API of transaction control: start/end transaction,
 * read, write data in the context of a transaction.
 */

int
vy_replace(struct vy_tx *tx, struct vy_index *index,
	   const char *tuple, const char *tuple_end)
{
	struct vy_tuple *vytuple = vy_tuple_from_data(index,
						      tuple, tuple_end);
	if (vytuple == NULL)
		return -1;
	vy_tx_set(tx, index, vytuple, SVREPLACE);
	vy_tuple_unref(vytuple);
	return 0;
}

int
vy_upsert(struct vy_tx *tx, struct vy_index *index,
	  const char *tuple, const char *tuple_end,
	  const char *expr, const char *expr_end, int index_base)
{
	assert(index_base == 0 || index_base == 1);
	uint32_t extra_size = ((expr_end - expr) +
			       mp_sizeof_uint(1) + mp_sizeof_uint(index_base));
	char *extra;
	struct vy_tuple *vytuple =
		vy_tuple_from_data_ex(index, tuple, tuple_end,
				      extra_size, &extra);
	if (vytuple == NULL) {
		return -1;
	}
	extra = mp_encode_uint(extra, 1); /* 1 upsert ops record */
	extra = mp_encode_uint(extra, index_base);
	memcpy(extra, expr, expr_end - expr);
	vy_tx_set(tx, index, vytuple, SVUPSERT);
	vy_tuple_unref(vytuple);
	return 0;
}

int
vy_delete(struct vy_tx *tx, struct vy_index *index,
	  const char *key, uint32_t part_count)
{
	struct vy_tuple *vykey = vy_tuple_from_key(index, key, part_count);
	if (vykey == NULL)
		return -1;
	vy_tx_set(tx, index, vykey, SVDELETE);
	vy_tuple_unref(vykey);
	return 0;
}

void
vy_rollback(struct vy_env *e, struct vy_tx *tx)
{
	vy_tx_rollback(e, tx);
	free(tx);
}

int
vy_prepare(struct vy_env *e, struct vy_tx *tx)
{
	(void) e;
	/* prepare transaction */
	assert(tx->state == VINYL_TX_READY);

	/* proceed read-only transactions */
	if (!vy_tx_is_ro(tx) && tx->is_aborted) {
		tx->state = VINYL_TX_ROLLBACK;
		diag_set(ClientError, ER_TRANSACTION_CONFLICT);
		return -1;
	}

	struct txv *v = write_set_first(&tx->write_set);
	for (; v != NULL; v = write_set_next(&tx->write_set, v))
		txv_abort_all(tx, v);

	/** Abort all open cursors. */
	struct vy_cursor *c;
	rlist_foreach_entry(c, &tx->cursors, next_in_tx)
		c->tx = NULL;

	tx_manager_end(tx->manager, tx);

	tx->state = VINYL_TX_COMMIT;
	/*
	 * A half committed transaction is no longer
	 * being part of concurrent index, but still can be
	 * committed or rolled back.
	 * Yet, it is important to maintain external
	 * serial commit order.
	 */
	return 0;
}

int
vy_commit(struct vy_env *e, struct vy_tx *tx, int64_t lsn)
{
	assert(tx->state == VINYL_TX_COMMIT);
	if (lsn > e->xm->lsn)
		e->xm->lsn = lsn;

	/* Flush transactional changes to the index. */
	uint64_t now = clock_monotonic64();
	struct txv *v = write_set_first(&tx->write_set);

	uint64_t write_count = 0;
	/** @todo: check return value of vy_tx_write(). */
	while (v != NULL) {
		++write_count;
		v = vy_tx_write(&tx->write_set, v, now, e->status, lsn);
	}

	uint32_t count = 0;
	struct txv *tmp;
	stailq_foreach_entry_safe(v, tmp, &tx->log, next_in_log) {
		count++;
		if (v->is_read)
			read_set_remove(&v->index->read_set, v);
		/* Don't touch write_set, we're deleting all keys. */
		txv_delete(v);
	}
	vy_stat_tx(e->stat, tx->start, count, write_count, false);
	free(tx);
	return 0;
}

struct vy_tx *
vy_begin(struct vy_env *e)
{
	struct vy_tx *tx;
	tx = malloc(sizeof(struct vy_tx));
	if (unlikely(tx == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_tx), "malloc",
			 "struct vy_tx");
		return NULL;
	}
	vy_tx_begin(e->xm, tx, VINYL_TX_RW);
	return tx;
}

void *
vy_savepoint(struct vy_tx *tx)
{
	return stailq_last(&tx->log);
}

void
vy_rollback_to_savepoint(struct vy_tx *tx, void *svp)
{
	struct stailq_entry *last = svp;
	/* Start from the first statement after the savepoint. */
	last = last == NULL ? stailq_first(&tx->log) : stailq_next(last);
	if (last == NULL) {
		/* Empty transaction or no changes after the savepoint. */
		return;
	}
	struct stailq tail;
	stailq_create(&tail);
	stailq_splice(&tx->log, last, &tail);
	struct txv *v, *tmp;
	stailq_foreach_entry_safe(v, tmp, &tail, next_in_log) {
		/* Remove from the conflict manager index */
		if (v->is_read)
			read_set_remove(&v->index->read_set, v);
		/* Remove from the transaction write log. */
		if (!v->is_read) {
			write_set_remove(&tx->write_set, v);
			tx->write_set_version++;
		}
		txv_delete(v);
	}
}

/* }}} Public API of transaction control */

/**
 * Find a tuple by key using a thread pool thread.
 */
int
vy_get(struct vy_tx *tx, struct vy_index *index, const char *key,
       uint32_t part_count, struct tuple **result)
{
	int rc = -1;
	struct vy_tuple *vyresult = NULL;
	struct vy_tuple *vykey = vy_tuple_from_key(index, key, part_count);
	if (vykey == NULL)
		return -1;

	/* Try to look up the tuple in the cache */
	if (vy_index_read(index, vykey, VINYL_EQ, &vyresult, tx))
		goto end;

	if (vyresult && vy_tuple_is_not_found(vyresult)) {
		/*
		 * We deleted this tuple in this
		 * transaction. No need for a disk lookup.
		 */
		vy_tuple_unref(vyresult);
		vyresult = NULL;
	}
	if (tx != NULL && vy_tx_track(tx, index, vykey))
		goto end;
	if (vyresult == NULL) { /* not found */
		*result = NULL;
		rc = 0;
	} else {
		*result = vy_convert_tuple(index, vyresult);
		if (*result != NULL)
			rc = 0;
	}
end:
	vy_tuple_unref(vykey);
	if (vyresult)
		vy_tuple_unref(vyresult);
	return rc;
}

/**
 * Read the next value from a cursor in a thread pool thread.
 */
int
vy_cursor_next(struct vy_cursor *c, struct tuple **result)
{
	struct vy_tuple *vyresult = NULL;
	struct vy_index *index = c->index;

	if (c->tx == NULL) {
		diag_set(ClientError, ER_NO_ACTIVE_TRANSACTION);
		return -1;
	}

	assert(c->key != NULL);
	if (vy_index_read(index, c->key, c->order, &vyresult, c->tx))
		return -1;
	c->n_reads++;
	if (vyresult && vy_tuple_is_not_found(vyresult)) {
		/*
		 * We deleted this tuple in this
		 * transaction. No need for a disk lookup.
		 */
		vy_tuple_unref(vyresult);
		vyresult = NULL;
	}
	if (vy_tx_track(c->tx, index, vyresult ? vyresult : c->key)) {
		if (vyresult)
			vy_tuple_unref(vyresult);
		return -1;
	}
	if (vyresult != NULL) {
		/* Found. */
		if (c->order == VINYL_GE)
			c->order = VINYL_GT;
		else if (c->order == VINYL_LE)
			c->order = VINYL_LT;

		vy_tuple_unref(c->key);
		c->key = vyresult;
		vy_tuple_ref(c->key);

		*result = vy_convert_tuple(index, vyresult);
		vy_tuple_unref(vyresult);
		if (*result == NULL)
			return -1;
	} else {
		/* Not found. */
		vy_tuple_unref(c->key);
		c->key = NULL;
		*result = NULL;
	}
	return 0;
}

/** {{{ Environment */

struct vy_env *
vy_env_new(void)
{
	struct vy_env *e = malloc(sizeof(*e));
	if (unlikely(e == NULL)) {
		diag_set(OutOfMemory, sizeof(*e), "malloc", "struct vy_env");
		return NULL;
	}
	memset(e, 0, sizeof(*e));
	rlist_create(&e->indexes);
	e->status = VINYL_OFFLINE;
	e->conf = vy_conf_new();
	if (e->conf == NULL)
		goto error_conf;
	e->quota = vy_quota_new(e->conf->memory_limit);
	if (e->quota == NULL)
		goto error_quota;
	e->xm = tx_manager_new(e);
	if (e->xm == NULL)
		goto error_xm;
	e->stat = vy_stat_new();
	if (e->stat == NULL)
		goto error_stat;
	e->scheduler = vy_scheduler_new(e);
	if (e->scheduler == NULL)
		goto error_sched;

	mempool_create(&e->cursor_pool, cord_slab_cache(),
	               sizeof(struct vy_cursor));
	return e;
error_sched:
	vy_stat_delete(e->stat);
error_stat:
	tx_manager_delete(e->xm);
error_xm:
	vy_quota_delete(e->quota);
error_quota:
	vy_conf_delete(e->conf);
error_conf:
	free(e);
	return NULL;
}

void
vy_env_delete(struct vy_env *e)
{
	vy_scheduler_delete(e->scheduler);
	/* TODO: tarantool doesn't delete indexes during shutdown */
	//assert(rlist_empty(&e->db));
	tx_manager_delete(e->xm);
	vy_conf_delete(e->conf);
	vy_quota_delete(e->quota);
	vy_stat_delete(e->stat);
	mempool_destroy(&e->cursor_pool);
	free(e);
}

/** }}} Environment */

/** {{{ Recovery */

void
vy_bootstrap(struct vy_env *e)
{
	assert(e->status == VINYL_OFFLINE);
	e->status = VINYL_ONLINE;
	/* enable quota */
	vy_quota_enable(e->quota);
}

void
vy_begin_initial_recovery(struct vy_env *e, struct vclock *vclock)
{
	assert(e->status == VINYL_OFFLINE);
	e->status = VINYL_INITIAL_RECOVERY;
	if (vclock) {
		e->xm->lsn = vclock_sum(vclock);
	} else {
		e->xm->lsn = 0;
	}
}

void
vy_begin_final_recovery(struct vy_env *e)
{
	assert(e->status == VINYL_INITIAL_RECOVERY);
	e->status = VINYL_FINAL_RECOVERY;
}

void
vy_end_recovery(struct vy_env *e)
{
	assert(e->status == VINYL_FINAL_RECOVERY);
	e->status = VINYL_ONLINE;
	/* enable quota */
	vy_quota_enable(e->quota);
}

/** }}} Recovery */

/** {{{ Replication */

int
vy_index_send(struct vy_index *index, vy_send_row_f sendrow, void *ctx)
{
	int64_t vlsn = INT64_MAX;
	int rc = 0;

	struct svmerge merge;
	sv_mergeinit(&merge, index, index->key_def);
	struct vy_rangeiter range_iter;
	vy_rangeiter_open(&range_iter, index, VINYL_GT, NULL, 0);
	/*
	 * It is nested loop over all ranges in index, all runs on every range
	 * and all tuples in every run.
	 *
	 * First, iterate over all ranges.
	 */
	for (struct vy_range *range = vy_rangeiter_get(&range_iter); range;
	     vy_rangeiter_next(&range_iter),
	     range = vy_rangeiter_get(&range_iter)) {

		struct svmerge *m = &merge;
		rc = sv_mergeprepare(m, range->run_count);
		if (unlikely(rc == -1)) {
			diag_clear(diag_get());
			goto finish_send;
		}
		struct vy_run *run = range->run;

		/* Merge all runs. */
		while (run) {
			struct svmergesrc *s = sv_mergeadd(m, NULL);
			struct vy_filterif *compression = index->compression_if;
			vy_tmp_run_iterator_open(&s->src, index, run,
				range->fd, compression, VINYL_GT, NULL);
			run = run->next;
		}
		struct svmergeiter im;
		sv_mergeiter_open(&im, m, VINYL_GT);
		struct svreaditer ri;
		sv_readiter_open(&ri, &im, vlsn, 0);
		/*
		 * Iterate over the merger with getting and sending
		 * every tuple.
		 */
		for (struct vy_tuple *tuple = sv_readiter_get(&ri); tuple;
		     sv_readiter_next(&ri), tuple = sv_readiter_get(&ri)) {

			uint32_t mp_size;
			const char *mp_data = vy_tuple_data(index, tuple,
							    &mp_size);
			int64_t lsn = tuple->lsn;
			if ((rc = sendrow(ctx, mp_data, mp_size, lsn)))
				goto finish_send;
		}
		sv_readiter_forward(&ri);
		sv_readiter_close(&ri);
		sv_mergereset(&merge);
	}
finish_send:
	sv_mergefree(&merge);
	return rc;
}

/* }}} replication */

/* {{{ vy_run_iterator vy_run_iterator support functions */
/* TODO: move to appropriate c file and remove */

/**
 * Load page by given nubber from disk to memory, unload previosly load page
 * Does nothing if currently loaded page is the same as the querried
 * Return the page on success or NULL on read error
 * Affects: curr_loaded_page
 */
static struct vy_page *
vy_run_iterator_load_page(struct vy_run_iterator *itr, uint32_t page)
{
	assert(page < itr->run->index.info.count);
	if (itr->curr_loaded_page != page) {
		if (itr->curr_loaded_page != UINT32_MAX)
			vy_run_unload_page(itr->run, itr->curr_loaded_page);
		struct vy_page *result = vy_run_load_page(itr->run, page,
							 itr->fd,
							 itr->compression);
		if (result != NULL)
			itr->curr_loaded_page = page;
		else
			itr->curr_loaded_page = UINT32_MAX;
		return result;
	}
	return vy_run_get_page(itr->run, page);
}

/**
 * Compare two positions
 */
static int
vy_run_iterator_cmp_pos(struct vy_run_iterator_pos pos1,
			struct vy_run_iterator_pos pos2)
{
	return pos1.page_no < pos2.page_no ? -1 :
		pos1.page_no > pos2.page_no ? 1 :
		pos1.pos_in_page < pos2.pos_in_page ? -1 :
		pos1.pos_in_page > pos2.pos_in_page;
}

/**
 * Specific middle wide position calculation for binary search
 * Till possible, returns position of first record in page
 * This behaviour allows to read keys from page index instead of disk
 *  untill necessary page was found
 * @retval 0 success
 * @retval -1 or memory or read error
 * @retval 1 EOF (possible when page has no records, int bootstrap run)
 */
static int
vy_iterator_pos_mid(struct vy_run_iterator *itr,
		    struct vy_run_iterator_pos pos1,
		    struct vy_run_iterator_pos pos2,
		    struct vy_run_iterator_pos *result)
{
	assert(vy_run_iterator_cmp_pos(pos1, pos2) < 0);
	if (pos2.page_no - pos1.page_no > 1) {
		assert(pos1.pos_in_page == 0 && pos2.pos_in_page == 0);
		result->page_no =
			pos1.page_no + (pos2.page_no - pos1.page_no) / 2;
		result->pos_in_page = 0;
		return 0;
	}
	struct vy_page *page = vy_run_iterator_load_page(itr, pos1.page_no);
	if (page == NULL)
		return -1;
	assert(pos1.page_no == pos2.page_no || pos2.pos_in_page == 0);
	uint32_t diff = pos1.page_no == pos2.page_no ?
		pos2.pos_in_page - pos1.pos_in_page :
		page->info->count - pos1.pos_in_page;
	result->page_no = pos1.page_no;
	result->pos_in_page = pos1.pos_in_page + diff / 2;
	return result->pos_in_page == page->info->count ? 1 : 0;
}

/**
 * Specific increment of middle wide position for binary search
 * Actually does not do increment until search in page was started.
 * @retval 0 success
 * @retval -1 memory or read error
 */
static int
vy_iterator_pos_mid_next(struct vy_run_iterator *itr,
			 struct vy_run_iterator_pos mid,
			 struct vy_run_iterator_pos end,
			 struct vy_run_iterator_pos *result)
{
	if (end.page_no - mid.page_no > 1) {
		*result = mid;
		return 0;
	}
	struct vy_page *page = vy_run_iterator_load_page(itr, mid.page_no);
	if (page == NULL)
		return -1;
	mid.pos_in_page++;
	*result =  mid.pos_in_page == page->info->count ? end : mid;
	return 0;
}

/**
 * Read key and lsn by a given wide position.
 * For the first record in a page reads the result from the page
 * index instead of fetching it from disk.
 *
 * @retval NULL read error or out of memory.
 * Affects: curr_loaded_page
 */
static char *
vy_run_iterator_read(struct vy_run_iterator *itr,
		     struct vy_run_iterator_pos pos, int64_t *lsn)
{
	if (pos.pos_in_page == 0) {
		struct vy_page_info *page_info =
			vy_run_index_get_page(&itr->run->index, pos.page_no);
		*lsn = page_info->min_key_lsn;
		return vy_run_index_min_key(&itr->run->index, page_info);
	}
	struct vy_page *page = vy_run_iterator_load_page(itr, pos.page_no);
	if (page == NULL)
		return NULL;
	struct vy_tuple_info *info = sd_pagev(page, pos.pos_in_page);
	*lsn = info->lsn;
	return sd_pagepointer(page, info);
}

/**
 * Binary search in a run for the given key and lsn.
 * Resulting wide position is stored it *pos argument
 * Note that run is sorted by key ASC and lsn DESC.
 * Normally sets the position to first record that greater than given key or
 *  equal key and not greater lsn, i.e.
 *  (record.key > key || (record.key == key && record lsn <= lsn)),
 *  (!) but has a special case of order ==  VINYL_GT/VINYL_LE,
 *  when position is set to first record that greater than given key, i.e.
 *  (record.key > key),
 * If that value was not found then position is set to end_pos (invalid pos)
 * *equal_key is set to true if found value is equal to key of false otherwise
 * @retval 0 success
 * @retval -1 read or memory error
 * Beware of:
 * 1)VINYL_GT/VINYL_LE special case
 * 2)search with partial key and lsn != INT64_MAX is meaningless and dangerous
 * 3)if return false, the position was set to maximal lsn of the next key
 */
static int
vy_run_iterator_search(struct vy_run_iterator *itr, char *key, int64_t vlsn,
		       struct vy_run_iterator_pos *pos, bool *equal_key)
{
	struct vy_run_iterator_pos beg = {0, 0};
	struct vy_run_iterator_pos end = {itr->run->index.info.count, 0};
	*equal_key = false;
	while (vy_run_iterator_cmp_pos(beg, end) != 0) {
		struct vy_run_iterator_pos mid;
		int rc = vy_iterator_pos_mid(itr, beg, end, &mid);
		if (rc != 0)
			return rc;
		int64_t fnd_lsn;
		char *fnd_key = vy_run_iterator_read(itr, mid, &fnd_lsn);
		if (fnd_key == NULL)
			return -1;
		int cmp = vy_tuple_compare(fnd_key, key, itr->index->key_def);
		bool cur_equal_key = cmp == 0;
		if (cmp == 0 &&
		    (itr->order == VINYL_GT || itr->order == VINYL_LE)) {
			cmp = -1;
		}
		cmp = cmp ? cmp : fnd_lsn > vlsn ? -1 : fnd_lsn < vlsn;
		if (cmp < 0) {
			if (vy_iterator_pos_mid_next(itr, mid, end, &beg) != 0)
				return -1;
		} else {
			end = mid;
			*equal_key = cur_equal_key;
		}
	}
	*pos = end;
	return 0;
}

/**
 * Increment (or decrement, depending on the order) the current
 * wide position.
 * Return a new value on success, end_pos on read error or EOF.
 * @retval 0 success
 * @retval 1 EOF
 * @retval -1 read or memory error
 * Affects: curr_loaded_page
 */
static int
vy_run_iterator_next_pos(struct vy_run_iterator *itr, enum vy_order order,
			 struct vy_run_iterator_pos *pos)
{
	*pos = itr->curr_pos;
	assert(pos->page_no < itr->run->index.info.count);
	if (order == VINYL_LE || order == VINYL_LT) {
		if (pos->page_no == 0 && pos->pos_in_page == 0)
			return 1;
		if (pos->pos_in_page > 0) {
			pos->pos_in_page--;
		} else {
			pos->page_no--;
			struct vy_page *page =
				vy_run_iterator_load_page(itr, pos->page_no);
			if (page == NULL)
				return -1;
			pos->pos_in_page = page->info->count - 1;
		}
	} else {
		assert(order == VINYL_GE || order == VINYL_GT ||
		       order == VINYL_EQ);
		struct vy_page *page =
			vy_run_iterator_load_page(itr, pos->page_no);
		if (page == NULL)
			return -1;
		pos->pos_in_page++;
		if (pos->pos_in_page >= page->info->count) {
			pos->page_no++;
			pos->pos_in_page = 0;
			if (pos->page_no == itr->run->index.info.count)
				return 1;
		}
	}
	return 0;
}

/**
 * Temporary prevent unloading of given page if necessary
 * Returns a value that must be passed to vy_run_iterator_unlock_page
 *
 * @retval page no of the locked page
 */
static uint32_t
vy_run_iterator_lock_page(struct vy_run_iterator *itr, uint32_t page_no)
{
	if (itr->curr_loaded_page != page_no)
		return UINT32_MAX;
	/* just increment reference counter */
	vy_run_load_page(itr->run, page_no,
			 itr->fd, itr->compression);
	return page_no;
}

/**
 * Cleanup after vy_run_iterator_lock_page
 */
static void
vy_run_iterator_unlock_page(struct vy_run_iterator *itr, uint32_t lock)
{
	if (lock != UINT32_MAX)
		vy_run_unload_page(itr->run, lock);
}

/**
 * Find the next record with lsn <= itr->lsn record.
 * The current position must be at the beginning of a series of
 * records with the same key it terms of direction of iterator
 * (i.e. left for GE, right for LE).
 * @retval 0 success
 * @retval 1 EOF
 * @retval -1 read or memory error
 * Affects: curr_loaded_page, curr_pos, search_ended
 */
static int
vy_run_iterator_find_lsn(struct vy_run_iterator *itr)
{
	assert(itr->curr_pos.page_no < itr->run->index.info.count);
	int64_t cur_lsn;
	int rc = 0;
	char *cur_key = vy_run_iterator_read(itr, itr->curr_pos, &cur_lsn);
	if (cur_key == NULL)
		return -1;
	while (cur_lsn > itr->vlsn) {
		rc = vy_run_iterator_next_pos(itr, itr->order, &itr->curr_pos);
		if (rc != 0) {
			if (rc > 0)
				vy_run_iterator_close(itr);
			return rc;
		}
		cur_key = vy_run_iterator_read(itr, itr->curr_pos, &cur_lsn);
		if (cur_key == NULL)
			return -1;
		if (itr->order == VINYL_EQ &&
		    vy_tuple_compare(cur_key, itr->key, itr->index->key_def)) {
			vy_run_iterator_close(itr);
			return 1;
		}
	}
	if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		/* Lock the  page, i.e. prevent the cur_key from unloading. */
		uint32_t lock_page =
			vy_run_iterator_lock_page(itr, itr->curr_pos.page_no);

		struct vy_run_iterator_pos test_pos;
		rc = vy_run_iterator_next_pos(itr, itr->order, &test_pos);
		while (rc == 0) {
			int64_t test_lsn;
			char *test_key =
				vy_run_iterator_read(itr, test_pos, &test_lsn);
			if (test_key == NULL) {
				rc = -1;
				break;
			}
			struct key_def *key_def = itr->index->key_def;
			if (test_lsn > itr->vlsn ||
			    vy_tuple_compare(cur_key, test_key, key_def) != 0)
				break;
			itr->curr_pos = test_pos;
			rc = vy_run_iterator_next_pos(itr, itr->order, &test_pos);
		}
		vy_run_iterator_unlock_page(itr, lock_page);
		rc = rc > 0 ? 0 : rc;
	}
	return rc;
}

/**
 * Find next (lower, older) record with the same key as current
 * Return true if the record was found
 * Return false if no value was found (or EOF) or there is a read error
 * @retval 0 success
 * @retval 1 EOF
 * @retval -1 read or memory error
 * Affects: curr_loaded_page, curr_pos, search_ended
 */
static int
vy_run_iterator_start(struct vy_run_iterator *itr)
{
	assert(itr->curr_loaded_page == UINT32_MAX);
	assert(!itr->search_started);
	itr->search_started = true;

	if (itr->run->index.info.count == 1) {
		/* there can be a stupid bootstrap run in which it's EOF */
		struct vy_page_info *page_info =
			vy_run_index_get_page(&itr->run->index, 0);

		if (!page_info->count) {
			vy_run_iterator_close(itr);
			return 1;
		}
		struct vy_page *page =
			vy_run_iterator_load_page(itr, 0);
		if (page == NULL)
			return -1;
	} else if (itr->run->index.info.count == 0) {
		/* never seen that, but it could be possible in future */
		vy_run_iterator_close(itr);
		return 1;
	}

	struct vy_run_iterator_pos end_pos = {itr->run->index.info.count, 0};
	bool equal_found = false;
	int rc;
	if (itr->key != NULL) {
		rc = vy_run_iterator_search(itr, itr->key, INT64_MAX,
					    &itr->curr_pos, &equal_found);
		if (rc < 0)
			return rc;
	} else if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		itr->order = VINYL_LE;
		itr->curr_pos = end_pos;
	} else {
		assert(itr->order == VINYL_GE || itr->order == VINYL_GT ||
		       itr->order == VINYL_EQ);
		itr->order = VINYL_GE;
		itr->curr_pos.page_no = 0;
		itr->curr_pos.pos_in_page = 0;
	}
	if (itr->order == VINYL_EQ && !equal_found) {
		vy_run_iterator_close(itr);
		return 1;
	}
	if ((itr->order == VINYL_GE || itr->order == VINYL_GT) &&
	    itr->curr_pos.page_no == end_pos.page_no) {
		vy_run_iterator_close(itr);
		return 1;
	}
	if (itr->order == VINYL_LT || itr->order == VINYL_LE) {
		/**
		 * 1) in case of VINYL_LT we now positioned on the value >= than
		 * given, so we need to make a step on previous key
		 * 2) in case if VINYL_LE we now positioned on the value > than
		 * given (special branch of code in vy_run_iterator_search),
		 * so we need to make a step on previous key
		 */
		return vy_run_iterator_next_key(itr);
	} else {
		assert(itr->order == VINYL_GE || itr->order == VINYL_GT ||
		       itr->order == VINYL_EQ);
		/**
		 * 1) in case of VINYL_GT we now positioned on the value > than
		 * given (special branch of code in vy_run_iterator_search),
		 * so we need just to find proper lsn
		 * 2) in case if VINYL_GE or VINYL_EQ we now positioned on the
		 * value >= given, so we need just to find proper lsn
		 */
		return vy_run_iterator_find_lsn(itr);
	}
}

/* }}} vy_run_iterator vy_run_iterator support functions */

/* {{{ vy_run_iterator API implementation */
/* TODO: move to c file and remove static keyword */

/**
 * Open the iterator
 */
static void
vy_run_iterator_open(struct vy_run_iterator *itr, struct vy_index *index,
		     struct vy_run *run, int fd,
		     struct vy_filterif *compression, enum vy_order order,
		     char *key, int64_t vlsn)
{
	itr->index = index;
	itr->run = run;
	itr->fd = fd;
	itr->compression = compression;

	itr->order = order;
	itr->key = key;
	itr->vlsn = vlsn;

	itr->curr_tuple = NULL;
	itr->curr_loaded_page = UINT32_MAX;
	itr->curr_pos.page_no = itr->run->index.info.count;
	itr->curr_tuple_pos.page_no = UINT32_MAX;

	itr->search_started = false;
	itr->search_ended = false;
}

/**
 * Create a tuple object from a its impression on a run page.
 * Uses the current iterator position in the page.
 *
 * @retval 0 success
 * @retval 1 EOF
 * @retval -1 memory or read error
 */
static int
vy_run_iterator_get(struct vy_run_iterator *itr, struct vy_tuple **result)
{
	*result = NULL;
	if (itr->search_ended)
		return 1;
	if (!itr->search_started) {
		int rc = vy_run_iterator_start(itr);
		if (rc != 0)
			return rc;
	}
	if (itr->curr_tuple != NULL) {
		if (vy_run_iterator_cmp_pos(itr->curr_tuple_pos,
					    itr->curr_pos) == 0) {
			*result = itr->curr_tuple;
			return 0;
		}
		vy_tuple_unref(itr->curr_tuple);
		itr->curr_tuple = NULL;
		itr->curr_tuple_pos.page_no = UINT32_MAX;
	}

	struct vy_page *page =
		vy_run_iterator_load_page(itr, itr->curr_pos.page_no);
	if (page == NULL)
		return -1;
	struct vy_tuple_info *info = sd_pagev(page, itr->curr_pos.pos_in_page);
	char *key = sd_pagepointer(page, info);
	itr->curr_tuple = vy_tuple_alloc(info->size);
	if (itr->curr_tuple == NULL)
		diag_set(OutOfMemory, info->size, "run_itr", "tuple");
	memcpy(itr->curr_tuple->data, key, info->size);
	itr->curr_tuple->flags = info->flags;
	itr->curr_tuple->lsn = info->lsn;
	itr->curr_tuple_pos = itr->curr_pos;
	*result = itr->curr_tuple;
	return 0;
}

/**
 * Find the next tuple in a page, i.e. a tuple with a different key
 * and fresh enough LSN (i.e. skipping the keys
 * too old for the current transaction).
 *
 * @retval 0 success
 * @retval 1 EOF
 * @retval -1 memory or read error
 */
static int
vy_run_iterator_next_key(struct vy_run_iterator *itr)
{
	if (itr->search_ended)
		return 1;
	if (!itr->search_started) {
		int rc = vy_run_iterator_start(itr);
		if (rc != 0)
			return rc;
	}
	uint32_t end_page = itr->run->index.info.count;
	assert(itr->curr_pos.page_no <= end_page);
	struct key_def *key_def = itr->index->key_def;
	if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		if (itr->curr_pos.page_no == 0 &&
		    itr->curr_pos.pos_in_page == 0) {
			vy_run_iterator_close(itr);
			return 1;
		}
		if (itr->curr_pos.page_no == end_page) {
			/* A special case for reverse iterators */
			uint32_t page_no = end_page - 1;
			struct vy_page *page =
				vy_run_iterator_load_page(itr, page_no);
			if (page == NULL)
				return -1;
			if (page->info->count == 0) {
				vy_run_iterator_close(itr);
				return 1;
			}
			itr->curr_pos.page_no = page_no;
			itr->curr_pos.pos_in_page = page->info->count - 1;
			return vy_run_iterator_find_lsn(itr);
		}
	}
	assert(itr->curr_pos.page_no < end_page);

	int64_t cur_lsn;
	char *cur_key = vy_run_iterator_read(itr, itr->curr_pos, &cur_lsn);
	if (cur_key == NULL)
		return -1;

	/*
	 * Lock the page, i.e. prevent the memory of cur_key from
	 * being unloaded.
	 */
	uint32_t lock_page =
		vy_run_iterator_lock_page(itr, itr->curr_pos.page_no);

	int64_t next_lsn;
	char *next_key;
	do {
		int rc = vy_run_iterator_next_pos(itr, itr->order,
						  &itr->curr_pos);
		if (rc != 0) {
			if (rc > 0)
				vy_run_iterator_close(itr);
			vy_run_iterator_unlock_page(itr, lock_page);
			return rc;
		}
		next_key = vy_run_iterator_read(itr, itr->curr_pos, &next_lsn);
		if (next_key == NULL)
			return -1;

	} while (vy_tuple_compare(cur_key, next_key, key_def) == 0);

	vy_run_iterator_unlock_page(itr, lock_page);

	if (itr->order == VINYL_EQ &&
	    vy_tuple_compare(next_key, itr->key, key_def) != 0) {
		vy_run_iterator_close(itr);
		return 1;
	}

	return vy_run_iterator_find_lsn(itr);
}

/**
 * Find next (lower, older) record with the same key as current
 * @retval 0 success
 * @retval 1 on if no value found, the iterator position was not changed
 * @retval -1 memory or read error
 */
static int
vy_run_iterator_next_lsn(struct vy_run_iterator *itr)
{
	if (itr->search_ended)
		return 1;
	if (!itr->search_started) {
		int rc = vy_run_iterator_start(itr);
		if (rc != 0)
			return rc;
	}
	assert(itr->curr_pos.page_no < itr->run->index.info.count);

	struct vy_run_iterator_pos next_pos;
	int rc = vy_run_iterator_next_pos(itr, VINYL_GE, &next_pos);
	if (rc != 0)
		return rc;

	int64_t cur_lsn;
	char *cur_key = vy_run_iterator_read(itr, itr->curr_pos, &cur_lsn);
	if (cur_key == NULL)
		return -1; /* read error */

	int64_t next_lsn;
	char *next_key = vy_run_iterator_read(itr, next_pos, &next_lsn);
	if (next_key == NULL)
		return -1; /* read error */

	/**
	 * One can think that we had to lock page of itr->curr_pos,
	 *  to prevent freeing cur_key with entire page and avoid
	 *  segmentation fault in vy_tuple_compare.
	 * But in fact the only case when curr_pos and next_pos
	 *  point to different pages is the case when next_pos points
	 *  to the beginning of the next page, and in this case
	 *  vy_run_iterator_read will read data from page index, not the page.
	 *  So in the case no page will be unloaded and we don't need
	 *  page lock
	 */
	struct key_def *key_def = itr->index->key_def;
	int cmp = vy_tuple_compare(cur_key, next_key, key_def);
	itr->curr_pos = cmp == 0 ? next_pos : itr->curr_pos;
	return cmp != 0;
}

/**
 * Restore the current position (if necessary) after
 * a change in the set of runs or ranges.
 *
 * @pre the iterator is not started
 *
 * @param last_tuple the last key on which the iterator was
 *		      positioned
 *
 * @retval 0	if position did not change (iterator started)
 * @retval 1	if position changed
 * @retval -1	a read or memory error
 */
static int
vy_run_iterator_restore(struct vy_run_iterator *itr,
			struct vy_tuple *last_tuple)
{
	if (itr->search_started || last_tuple == NULL)
		return 0;
	/* Restoration is very similar to first search so we'll use that */
	enum vy_order save_order = itr->order;
	char *save_key = itr->key;
	int64_t save_vlsn = itr->vlsn;
	if (itr->order == VINYL_LE || itr->order == VINYL_LT)
		itr->order = VINYL_LT;
	else
		itr->order = VINYL_GT;
	itr->key = last_tuple->data;
	itr->vlsn = last_tuple->lsn;
	int rc = vy_run_iterator_start(itr);
	itr->order = (save_key != NULL ? save_order :
		      save_order == VINYL_LE || save_order == VINYL_LT ?
		      VINYL_LE : VINYL_GE);
	itr->key = save_key;
	itr->vlsn = save_vlsn;
	if (rc < 0)
		return rc;
	if (itr->order == VINYL_EQ && rc == 0) {
		struct vy_tuple *found_tuple;
		rc = vy_run_iterator_get(itr, &found_tuple);
		if (rc < 0)
			return rc;
		assert(rc == 0);
		if (vy_tuple_compare(found_tuple->data, itr->key,
				     itr->index->key_def) != 0) {
			vy_run_iterator_close(itr);
			return 0;
		}
	}
	return rc == 0 ? 1 : 0;
}

/**
 * Close an iterator and free all resources
 */
static void
vy_run_iterator_close(struct vy_run_iterator *itr)
{
	if (itr->curr_tuple != NULL) {
		vy_tuple_unref(itr->curr_tuple);
		itr->curr_tuple = NULL;
		itr->curr_tuple_pos.page_no = UINT32_MAX;
	}
	if (itr->curr_loaded_page != UINT32_MAX) {
		assert(itr->curr_loaded_page < itr->run->index.info.count);
		vy_run_unload_page(itr->run, itr->curr_loaded_page);
		itr->curr_loaded_page = UINT32_MAX;
	}
	itr->search_ended = true;
}

/* }}} vy_run_iterator API implementation */


/* {{{ Temporary wrap of new run iterator to old API */

static void
vy_tmp_run_iterator_close(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->close == vy_tmp_run_iterator_close);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)virt_iterator->priv;
	vy_run_iterator_close(itr);
}

static int
vy_tmp_run_iterator_has(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->has == vy_tmp_run_iterator_has);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)virt_iterator->priv;
	struct vy_tuple *t;
	int rc = vy_run_iterator_get(itr, &t);
	return rc == 0;
}

static struct vy_tuple *
vy_tmp_run_iterator_get(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->get == vy_tmp_run_iterator_get);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)virt_iterator->priv;
	bool *is_dup = (bool *)(virt_iterator->priv + sizeof(*itr) + sizeof(struct vy_tuple *));
	struct vy_tuple *t;
	int rc = vy_run_iterator_get(itr, &t);
	if (rc != 0)
		return NULL;
	t->flags &= ~SVDUP;
	t->flags |= *is_dup ? SVDUP : 0;
	struct vy_tuple **sv = (struct vy_tuple **)(virt_iterator->priv + sizeof(*itr));
	*sv = t;
	return *sv;
}

static void
vy_tmp_run_iterator_next(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->next == vy_tmp_run_iterator_next);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)virt_iterator->priv;
	bool *is_dup = (bool *)(virt_iterator->priv + sizeof(*itr) + sizeof(struct vy_tuple *));
	*is_dup = true;
	int rc = vy_run_iterator_next_lsn(itr);
	if (rc == 1) {
		*is_dup = false;
		vy_run_iterator_next_key(itr);
	}
}

void
vy_tmp_run_iterator_open(struct vy_iter *virt_iterator, struct vy_index *index,
		     struct vy_run *run, int fd,
		     struct vy_filterif *compression,
		     enum vy_order order, char *key)
{
	static struct vy_iterif vif = {
		.close = vy_tmp_run_iterator_close,
		.has = vy_tmp_run_iterator_has,
		.get = vy_tmp_run_iterator_get,
		.next = vy_tmp_run_iterator_next
	};
	virt_iterator->vif = &vif;
	struct vy_run_iterator *itr = (struct vy_run_iterator *)virt_iterator->priv;
	assert(sizeof(virt_iterator->priv) >= sizeof(*itr) + sizeof(struct vy_tuple *) + sizeof(bool));
	bool *is_dup = (bool *)(virt_iterator->priv + sizeof(*itr) + sizeof(struct vy_tuple *));
	*is_dup = false;
	vy_run_iterator_open(itr, index, run, fd, compression, order, key,
			     INT64_MAX);

}

/* }}} Temporary wrap of new run iterator to old API */

/* {{{ vy_mem_iterator API forward declaration */
/* TODO: move to header and remove static keyword */

/**
 * Iterator over vy_mem
 */
struct vy_mem_iterator {
	/* mem */
	struct vy_mem *mem;

	/* Search options */
	/**
	 * Order, that specifies direction, start position and stop criteria
	 * if key == NULL: GT and EQ are changed to GE, LT to LE for beauty.
	 */
	enum vy_order order;
	/* Search key data in terms of vinyl, vy_tuple_compare argument */
	char *key;
	/* LSN visibility, iterator shows values with lsn <= than that */
	int64_t vlsn;

	/* State of iterator */
	/* Current position in tree */
	struct vy_mem_tree_iterator curr_pos;
	/* Tuple in current position in tree */
	struct vy_tuple *curr_tuple;
	/* data version from vy_mem */
	uint32_t version;

	/* Is false until first .._get ot .._next_.. method is called */
	bool search_started;
	/* Search is finished, you will not get more values from iterator */
	bool search_ended;
};

/**
 * vy_mem_iterator API forward declaration
 */

static void
vy_mem_iterator_open(struct vy_mem_iterator *itr, struct vy_mem *mem,
		     enum vy_order order, char *key, int64_t vlsn);

static int
vy_mem_iterator_get(struct vy_mem_iterator *itr, struct vy_tuple **result);

static int
vy_mem_iterator_next_key(struct vy_mem_iterator *itr);

static int
vy_mem_iterator_restore(struct vy_mem_iterator *itr,
			struct vy_tuple *last_tuple);

static int
vy_mem_iterator_next_lsn(struct vy_mem_iterator *itr);

static void
vy_mem_iterator_close(struct vy_mem_iterator *itr);

/* }}} vy_mem_iterator API forward declaration */

/* {{{ vy_mem_iterator support functions */

/**
 * Get a tuple by current position
 */
static struct vy_tuple *
vy_mem_iterator_curr_tuple(struct vy_mem_iterator *itr)
{
	return *vy_mem_tree_itr_get_elem(&itr->mem->tree, &itr->curr_pos);
}

/**
 * Make a step in directions defined by itr->order
 * @retval 0 success
 * @retval 1 EOF
 */
static int
vy_mem_iterator_step(struct vy_mem_iterator *itr)
{
	if (itr->order == VINYL_LE || itr->order == VINYL_LT)
		vy_mem_tree_itr_prev(&itr->mem->tree, &itr->curr_pos);
	else
		vy_mem_tree_itr_next(&itr->mem->tree, &itr->curr_pos);
	if (vy_mem_tree_itr_is_invalid(&itr->curr_pos))
		return 1;
	itr->curr_tuple = vy_mem_iterator_curr_tuple(itr);
	return 0;
}

/**
 * Find next record with lsn <= itr->lsn record.
 * Current position must be at the beginning of serie of records with the
 * same key it terms of direction of iterator (i.e. left for GE, right for LE)
 * @retval 0 success
 * @retval 1 EOF
 */
static int
vy_mem_iterator_find_lsn(struct vy_mem_iterator *itr)
{
	assert(!vy_mem_tree_itr_is_invalid(&itr->curr_pos));
	assert(itr->curr_tuple == vy_mem_iterator_curr_tuple(itr));
	while (itr->curr_tuple->lsn > itr->vlsn) {
		if (vy_mem_iterator_step(itr) != 0 ||
		    (itr->order == VINYL_EQ &&
		     vy_tuple_compare(itr->curr_tuple->data, itr->key,
				      itr->mem->key_def))) {
			vy_mem_iterator_close(itr);
			return 1;
		}
	}
	if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		struct vy_mem_tree_iterator prev_pos = itr->curr_pos;
		vy_mem_tree_itr_prev(&itr->mem->tree, &prev_pos);

		while (!vy_mem_tree_itr_is_invalid(&prev_pos)) {
			struct vy_tuple *prev_tuple =
				*vy_mem_tree_itr_get_elem(&itr->mem->tree,
							  &prev_pos);
			struct key_def *key_def = itr->mem->key_def;
			if (prev_tuple->lsn > itr->vlsn ||
			    vy_tuple_compare(itr->curr_tuple->data,
					     prev_tuple->data, key_def) != 0)
				break;
			itr->curr_pos = prev_pos;
			itr->curr_tuple = prev_tuple;
			vy_mem_tree_itr_prev(&itr->mem->tree, &prev_pos);
		}
	}
	return 0;
}

/**
 * Find next (lower, older) record with the same key as current
 * @retval 0 success
 * @retval 1 EOF
 */
static int
vy_mem_iterator_start(struct vy_mem_iterator *itr)
{
	assert(!itr->search_started);
	itr->search_started = true;
	itr->version = itr->mem->version;

	struct tree_mem_key tree_key;
	tree_key.data = itr->key;
	/* (lsn == INT64_MAX - 1) means that lsn is ignored in comparison */
	tree_key.lsn = INT64_MAX - 1;
	if (itr->key != NULL) {
		if (itr->order == VINYL_EQ) {
			bool exact;
			itr->curr_pos =
				vy_mem_tree_lower_bound(&itr->mem->tree,
							&tree_key, &exact);
			if (!exact) {
				vy_mem_iterator_close(itr);
				return 1;
			}
		} else if (itr->order == VINYL_LE || itr->order == VINYL_GT) {
			itr->curr_pos =
				vy_mem_tree_upper_bound(&itr->mem->tree,
							&tree_key, NULL);
		} else {
			assert(itr->order == VINYL_GE || itr->order == VINYL_LT);
			itr->curr_pos =
				vy_mem_tree_lower_bound(&itr->mem->tree,
							&tree_key, NULL);
		}
	} else if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		itr->order = VINYL_LE;
		itr->curr_pos = vy_mem_tree_invalid_iterator();
	} else {
		itr->order = VINYL_GE;
		itr->curr_pos = vy_mem_tree_itr_first(&itr->mem->tree);
	}

	if (itr->order == VINYL_LT || itr->order == VINYL_LE)
		vy_mem_tree_itr_prev(&itr->mem->tree, &itr->curr_pos);
	if (vy_mem_tree_itr_is_invalid(&itr->curr_pos)) {
		vy_mem_iterator_close(itr);
		return 1;
	}
	itr->curr_tuple = vy_mem_iterator_curr_tuple(itr);

	return vy_mem_iterator_find_lsn(itr);
}

/**
 * Restores iterator if the mem have been changed
 */
static void
vy_mem_iterator_check_version(struct vy_mem_iterator *itr)
{
	assert(itr->curr_tuple != NULL);
	assert(!itr->search_ended);
	if (itr->version == itr->mem->version)
		return;
	itr->version = itr->mem->version;
	struct vy_tuple **record =
		vy_mem_tree_itr_get_elem(&itr->mem->tree, &itr->curr_pos);
	if (record != NULL && *record == itr->curr_tuple)
		return;
	struct tree_mem_key tree_key;
	tree_key.data = itr->curr_tuple->data;
	tree_key.lsn = itr->curr_tuple->lsn;
	bool exact;
	itr->curr_pos = vy_mem_tree_lower_bound(&itr->mem->tree,
						&tree_key, &exact);
	assert(exact);
	assert(itr->curr_tuple == vy_mem_iterator_curr_tuple(itr));
}

/* }}} vy_mem_iterator support functions */


/* {{{ vy_mem_iterator API implementation */
/* TODO: move to c file and remove static keyword */

/**
 * Open the iterator
 */
static void
vy_mem_iterator_open(struct vy_mem_iterator *itr, struct vy_mem *mem,
		     enum vy_order order, char *key, int64_t vlsn)
{
	itr->mem = mem;

	itr->order = order;
	itr->key = key;
	itr->vlsn = vlsn;

	itr->curr_pos = vy_mem_tree_invalid_iterator();
	itr->curr_tuple = NULL;

	itr->search_started = false;
	itr->search_ended = false;
}

/**
 * Get a tuple from a record, that iterator currently positioned on
 * @retval 0 success
 * @retval 1 EOF
 */
static int
vy_mem_iterator_get(struct vy_mem_iterator *itr, struct vy_tuple **result)
{
	if (itr->search_ended ||
	    (!itr->search_started && vy_mem_iterator_start(itr) != 0))
		return 1;
	*result = itr->curr_tuple;
	return 0;
}

/**
 * Find the next record with different key as current and visible lsn
 * @retval 0 success
 * @retval 1 EOF
 */
static int
vy_mem_iterator_next_key(struct vy_mem_iterator *itr)
{
	if (itr->search_ended ||
	    (!itr->search_started && vy_mem_iterator_start(itr) != 0))
		return 1;
	assert(!vy_mem_tree_itr_is_invalid(&itr->curr_pos));
	vy_mem_iterator_check_version(itr);
	assert(itr->curr_tuple == vy_mem_iterator_curr_tuple(itr));
	struct key_def *key_def = itr->mem->key_def;

	struct vy_tuple *prev_tuple = itr->curr_tuple;
	do {
		if (vy_mem_iterator_step(itr) != 0) {
			vy_mem_iterator_close(itr);
			return 1;
		}
	} while (vy_tuple_compare(prev_tuple->data, itr->curr_tuple->data,
				  key_def) == 0);

	if (itr->order == VINYL_EQ &&
	    vy_tuple_compare(itr->curr_tuple->data, itr->key, key_def) != 0) {
		vy_mem_iterator_close(itr);
		return 1;
	}

	return vy_mem_iterator_find_lsn(itr);
}

/**
 * Find next (lower, older) record with the same key as current
 * @retval 0 success
 * @retval 1 EOF
 */
static int
vy_mem_iterator_next_lsn(struct vy_mem_iterator *itr)
{
	if (itr->search_ended ||
	    (!itr->search_started && vy_mem_iterator_start(itr) != 0))
		return 1;
	assert(!vy_mem_tree_itr_is_invalid(&itr->curr_pos));
	vy_mem_iterator_check_version(itr);
	assert(itr->curr_tuple == vy_mem_iterator_curr_tuple(itr));
	struct key_def *key_def = itr->mem->key_def;

	struct vy_mem_tree_iterator next_pos = itr->curr_pos;
	vy_mem_tree_itr_next(&itr->mem->tree, &next_pos);
	if (vy_mem_tree_itr_is_invalid(&next_pos))
		return 1; /* EOF */

	struct vy_tuple *next_tuple =
		*vy_mem_tree_itr_get_elem(&itr->mem->tree, &next_pos);
	if (vy_tuple_compare(itr->curr_tuple->data,
			     next_tuple->data, key_def) == 0) {
		itr->curr_pos = next_pos;
		itr->curr_tuple = next_tuple;
		return 0;
	}
	return 1;
}

/**
 * Restore the current position (if necessary).
 *
 * @param last_tuple the key the iterator was positioned on
 *
 * @retval 0 nothing changed
 * @retval 1 iterator position was changed
 */
static int
vy_mem_iterator_restore(struct vy_mem_iterator *itr,
			struct vy_tuple *last_tuple)
{
	if (!itr->search_started || itr->version == itr->mem->version) {
		return 0;
	}
	if (last_tuple == NULL || itr->search_ended) {
		itr->version = itr->mem->version;
		struct vy_tuple *was_tuple =
			itr->search_ended ? NULL : itr->curr_tuple;
		itr->search_started = false;
		itr->search_ended = false;
		itr->curr_tuple = NULL;
		int rc = vy_mem_iterator_start(itr);
		struct vy_tuple *new_tuple = rc ? NULL : itr->curr_tuple;
		return was_tuple != new_tuple;
	}

	vy_mem_iterator_check_version(itr);
	struct vy_mem_tree_iterator pos = itr->curr_pos;
	int rc = 0;
	if (itr->order == VINYL_GE || itr->order == VINYL_GT || itr->order == VINYL_EQ) {
		while (true) {
			vy_mem_tree_itr_prev(&itr->mem->tree, &pos);
			if (vy_mem_tree_itr_is_invalid(&pos))
				return rc;
			struct vy_tuple *t =
				*vy_mem_tree_itr_get_elem(&itr->mem->tree, &pos);
			int cmp = vy_tuple_compare(t->data, last_tuple->data, itr->mem->key_def);
			if (cmp < 0 || (cmp == 0 && t->lsn >= last_tuple->lsn))
				return rc;
			if (t->lsn <= itr->vlsn) {
				itr->curr_pos = pos;
				itr->curr_tuple = t;
				rc = 1;
			}
		}
		return rc;
	}
	assert(itr->order == VINYL_LE || itr->order == VINYL_LT);
	int cmp = vy_tuple_compare(itr->curr_tuple->data, last_tuple->data, itr->mem->key_def);
	int64_t break_lsn = cmp == 0 ? last_tuple->lsn : itr->vlsn + 1;
	while (true) {
		vy_mem_tree_itr_prev(&itr->mem->tree, &pos);
		if (vy_mem_tree_itr_is_invalid(&pos))
			return rc;
		struct vy_tuple *t =
			*vy_mem_tree_itr_get_elem(&itr->mem->tree, &pos);
		int cmp = vy_tuple_compare(t->data, itr->curr_tuple->data, itr->mem->key_def);
		assert(cmp <= 0);
		if (cmp < 0 || t->lsn >= break_lsn)
			return rc;
		itr->curr_pos = pos;
		itr->curr_tuple = t;
		rc = 1;
	}
	if (cmp == 0)
		return rc;
	pos = itr->curr_pos;
	while (true) {
		vy_mem_tree_itr_next(&itr->mem->tree, &pos);
		if (vy_mem_tree_itr_is_invalid(&pos))
			return rc;
		struct vy_tuple *t =
			*vy_mem_tree_itr_get_elem(&itr->mem->tree, &pos);
		int cmp = vy_tuple_compare(t->data, last_tuple->data, itr->mem->key_def);
		if (cmp > 0)
			return rc;
		if (cmp == 0) {
			if (t->lsn < last_tuple->lsn) {
				itr->curr_pos = pos;
				itr->curr_tuple = t;
				return 1;
			}
		} else if (t->lsn <= itr->vlsn) {
			cmp = vy_tuple_compare(t->data, itr->curr_tuple->data, itr->mem->key_def);
			if (cmp != 0) {
				itr->curr_pos = pos;
				itr->curr_tuple = t;
				rc = 1;
			}
		}
	}
	assert(false);
	return rc;
}

/**
 * Close an iterator and free all resources
 */
static void
vy_mem_iterator_close(struct vy_mem_iterator *itr)
{
	itr->search_ended = true;
}

/* }}} vy_mem_iterator API implementation */

/* {{{ Temporary wrap of new mem iterator to old API */

static void
vy_tmp_mem_iterator_close(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->close == vy_tmp_mem_iterator_close);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)virt_iterator->priv;
	vy_mem_iterator_close(itr);
}

static int
vy_tmp_mem_iterator_has(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->has == vy_tmp_mem_iterator_has);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)virt_iterator->priv;
	struct vy_tuple *t;
	int rc = vy_mem_iterator_get(itr, &t);
	return rc == 0;
}

static struct vy_tuple *
vy_tmp_mem_iterator_get(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->get == vy_tmp_mem_iterator_get);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)virt_iterator->priv;
	bool *is_dup = (bool *)(virt_iterator->priv + sizeof(*itr) + sizeof(struct vy_tuple *));
	struct vy_tuple *t;
	int rc = vy_mem_iterator_get(itr, &t);
	if (rc != 0)
		return NULL;

	t->flags &= ~SVDUP;
	t->flags |= *is_dup ? SVDUP : 0;
	struct vy_tuple **sv = (struct vy_tuple **)(virt_iterator->priv + sizeof(*itr));
	*sv = t;
	return *sv;
}

static void
vy_tmp_mem_iterator_next(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->next == vy_tmp_mem_iterator_next);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)virt_iterator->priv;
	bool *is_dup = (bool *)(virt_iterator->priv + sizeof(*itr) + sizeof(struct vy_tuple *));
	*is_dup = true;
	int rc = vy_mem_iterator_next_lsn(itr);
	if (rc == 1) {
		*is_dup = false;
		vy_mem_iterator_next_key(itr);
	}
};

void
vy_tmp_mem_iterator_open(struct vy_iter *virt_iterator, struct vy_mem *mem,
			 enum vy_order order, char *key)
{
	static struct vy_iterif vif = {
		.close = vy_tmp_mem_iterator_close,
		.has = vy_tmp_mem_iterator_has,
		.get = vy_tmp_mem_iterator_get,
		.next = vy_tmp_mem_iterator_next
	};
	virt_iterator->vif = &vif;
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)virt_iterator->priv;
	assert(sizeof(virt_iterator->priv) >= sizeof(*itr) + sizeof(struct vy_tuple *) + sizeof(bool));
	bool *is_dup = (bool *)(virt_iterator->priv + sizeof(*itr) + sizeof(struct vy_tuple *));
	*is_dup = false;
	vy_mem_iterator_open(itr, mem, order, key, INT64_MAX);
}

/* }}} Temporary wrap of new mem iterator to old API */

/* {{{ Iteration over transaction writes */

struct vy_txw_iterator {
	struct vy_index *index;
	struct vy_tx *tx;

	/* Search options */
	/**
	 * Order, that specifies direction, start position and stop criteria
	 * if key == NULL: GT and EQ are changed to GE, LT to LE for beauty.
	 */
	enum vy_order order;
	/* Search key data in terms of vinyl, vy_tuple_compare argument */
	char *key;

	/* Last version of vy_tx */
	uint32_t version;
	/* Current pos in txw tree */
	struct txv *curr_txv;
	/* Is false until first .._get ot .._next_.. method is called */
	bool search_started;
};

void
vy_txw_iterator_open(struct vy_txw_iterator *itr,
		     struct vy_index *index, struct vy_tx *tx,
		     enum vy_order order, char *key)
{
	itr->index = index;
	itr->tx = tx;

	itr->order = order;
	itr->key = key;

	itr->version = UINT32_MAX;
	itr->curr_txv = NULL;
	itr->search_started = false;
}

int
vy_txw_iterator_start(struct vy_txw_iterator *itr)
{
	itr->search_started = true;
	itr->version = itr->tx->write_set_version;
	itr->curr_txv = NULL;
	struct write_set_key key = { itr->index, itr->key };
	struct txv *txv;
	if (itr->key != NULL) {
		if (itr->order == VINYL_EQ) {
			txv = write_set_search(&itr->tx->write_set, &key);
		} else if (itr->order == VINYL_GE || itr->order == VINYL_GT) {
			txv = write_set_nsearch(&itr->tx->write_set, &key);
		} else {
			txv = write_set_psearch(&itr->tx->write_set, &key);
		}
		if (txv == NULL || txv->index != itr->index)
			return 1;
		if (vy_tuple_compare(itr->key, txv->tuple->data,
				     itr->index->key_def) == 0) {
			while (true) {
				struct txv *next;
				if (itr->order == VINYL_LE ||
				    itr->order == VINYL_GT)
					next = write_set_next(&itr->tx->write_set, txv);
				else
					next = write_set_prev(&itr->tx->write_set, txv);
				if (next == NULL || next->index != itr->index)
					break;
				if (vy_tuple_compare(itr->key, next->tuple->data, itr->index->key_def) != 0)
					break;
				txv = next;
			}
			if (itr->order == VINYL_GT)
				txv = write_set_next(&itr->tx->write_set, txv);
			else if (itr->order == VINYL_LT)
				txv = write_set_prev(&itr->tx->write_set, txv);
		}
	} else if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		itr->order = VINYL_LE;
		key.index = (struct vy_index *)((uintptr_t)key.index + 1);
		txv = write_set_psearch(&itr->tx->write_set, &key);
	} else {
		assert(itr->order == VINYL_GE ||
		       itr->order == VINYL_GT || itr->order == VINYL_EQ);
		itr->order = VINYL_GE;
		txv = write_set_nsearch(&itr->tx->write_set, &key);
	}
	if (txv == NULL || txv->index != itr->index)
		return 1;
	itr->curr_txv = txv;
	return 0;
}

int
vy_txw_iterator_get(struct vy_txw_iterator *itr, struct vy_tuple **result)
{
	if (!itr->search_started && vy_txw_iterator_start(itr) != 0)
		return 1;
	if (itr->curr_txv == NULL)
		return 1;
	*result = itr->curr_txv->tuple;
	return 0;
}

int
vy_txw_iterator_next_key(struct vy_txw_iterator *itr)
{
	if (!itr->search_started && vy_txw_iterator_start(itr) != 0)
		return 1;
	itr->version = itr->tx->write_set_version;
	if (itr->curr_txv == NULL)
		return 1;
	if (itr->order == VINYL_EQ) {
		itr->curr_txv = write_set_next(&itr->tx->write_set, itr->curr_txv);
		if (itr->curr_txv != NULL &&
		    (itr->curr_txv->index != itr->index ||
		     vy_tuple_compare(itr->curr_txv->tuple->data, itr->key,
				      itr->index->key_def) != 0))
			itr->curr_txv = NULL;
	} else if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		itr->curr_txv = write_set_prev(&itr->tx->write_set, itr->curr_txv);
		if (itr->curr_txv != NULL && itr->curr_txv->index != itr->index)
			itr->curr_txv = NULL;
	} else {
		assert(itr->order == VINYL_GE || itr->order == VINYL_GT);
		itr->curr_txv = write_set_next(&itr->tx->write_set, itr->curr_txv);
		if (itr->curr_txv != NULL && itr->curr_txv->index != itr->index)
			itr->curr_txv = NULL;
	}
	return itr->curr_txv == NULL ? 1 : 0;
}

int
vy_txw_iterator_next_lsn(struct vy_txw_iterator *itr)
{
	(void)itr;
	return 1;
}

int
vy_txw_iterator_restore(struct vy_txw_iterator *itr,
			struct vy_tuple *last_tuple)
{
	if (last_tuple == NULL || !itr->search_started ||
	    itr->version == itr->tx->write_set_version) {

		return 0;
	}

	itr->search_started = true;
	itr->version = itr->tx->write_set_version;
	struct write_set_key key = { itr->index, last_tuple->data };
	struct vy_tuple *was_tuple = itr->curr_txv != NULL ?
				     itr->curr_txv->tuple : NULL;
	itr->curr_txv = NULL;
	struct txv *txv;
	if (itr->order == VINYL_LE || itr->order == VINYL_LT)
		txv = write_set_psearch(&itr->tx->write_set, &key);
	else
		txv = write_set_nsearch(&itr->tx->write_set, &key);
	if (txv != NULL && txv->index == itr->index &&
	    vy_tuple_compare(txv->tuple->data, last_tuple->data,
			     itr->index->key_def) == 0) {
		if (itr->order == VINYL_LE || itr->order == VINYL_LT)
			txv = write_set_prev(&itr->tx->write_set, txv);
		else
			txv = write_set_next(&itr->tx->write_set, txv);
	}
	if (txv != NULL && txv->index == itr->index && itr->order == VINYL_EQ &&
	    vy_tuple_compare(txv->tuple->data, itr->key,
			     itr->index->key_def) != 0)
		txv = NULL;
	if (txv == NULL || txv->index != itr->index) {
		assert(was_tuple == NULL);
		return 0;
	}
	itr->curr_txv = txv;
	return txv->tuple != was_tuple;
}

void
vy_txw_iterator_close(struct vy_txw_iterator *itr)
{
	(void)itr;
}

/* }}} Iteration over transaction writes */

/* {{{ vy_tuple_iterator: Common interface for iterator over run, mem, etc */

struct vy_tuple_iterator;

typedef int (*vy_iterator_get_f)(struct vy_tuple_iterator *virt_iterator, struct vy_tuple **result);
typedef int (*vy_iterator_next_key_f)(struct vy_tuple_iterator *virt_iterator);
typedef int (*vy_iterator_next_lsn_f)(struct vy_tuple_iterator *virt_iterator);
typedef int (*vy_iterator_restore_f)(struct vy_tuple_iterator *virt_iterator, struct vy_tuple *last_tuple);
typedef void (*vy_iterator_next_close_f)(struct vy_tuple_iterator *virt_iterator);

struct vy_tuple_iterator_iface {
	vy_iterator_get_f get;
	vy_iterator_next_key_f next_key;
	vy_iterator_next_lsn_f next_lsn;
	vy_iterator_restore_f restore;
	vy_iterator_next_close_f close;
};

struct vy_tuple_iterator {
	struct vy_tuple_iterator_iface *iface;
	char priv[128];
};

/* }}} vy_tuple_iterator: Common interface for iterator over run, mem, etc */

/* {{{ Virtual iterator over run */

int
vy_run_iterator_iface_get(struct vy_tuple_iterator *vitr,
			  struct vy_tuple **result)
{
	assert(vitr->iface->get == vy_run_iterator_iface_get);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)vitr->priv;
	return vy_run_iterator_get(itr, result);
}

int
vy_run_iterator_iface_next_key(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_key == vy_run_iterator_iface_next_key);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)vitr->priv;
	return vy_run_iterator_next_key(itr);
}

int
vy_run_iterator_iface_next_lsn(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_lsn == vy_run_iterator_iface_next_lsn);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)vitr->priv;
	return vy_run_iterator_next_lsn(itr);
}

int
vy_run_iterator_iface_restore(struct vy_tuple_iterator *vitr,
			      struct vy_tuple *last_tuple)
{
	assert(vitr->iface->restore == vy_run_iterator_iface_restore);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)vitr->priv;
	return vy_run_iterator_restore(itr, last_tuple);
}

static void
vy_run_iterator_iface_close(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->close == vy_run_iterator_iface_close);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)vitr->priv;
	vy_run_iterator_close(itr);
}

void
vy_run_iterator_iface_open(struct vy_tuple_iterator *vitr, struct vy_index *index,
			   struct vy_run *run, int fd,
			   struct vy_filterif *compression, enum vy_order order,
			   char *key, int64_t vlsn)
{
	static struct vy_tuple_iterator_iface iface = {
		.get = vy_run_iterator_iface_get,
		.next_key = vy_run_iterator_iface_next_key,
		.next_lsn = vy_run_iterator_iface_next_lsn,
		.restore = vy_run_iterator_iface_restore,
		.close = vy_run_iterator_iface_close
	};
	vitr->iface = &iface;
	struct vy_run_iterator *itr = (struct vy_run_iterator *)vitr->priv;
	assert(sizeof(vitr->priv) >= sizeof(*itr));
	vy_run_iterator_open(itr, index, run, fd, compression, order, key, vlsn);
}

/* }}} Virtual iterator over run */

/* {{{ Virtual iterator over mem */

int
vy_mem_iterator_iface_get(struct vy_tuple_iterator *vitr,
			  struct vy_tuple **result)
{
	assert(vitr->iface->get == vy_mem_iterator_iface_get);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)vitr->priv;
	return vy_mem_iterator_get(itr, result);
}

int
vy_mem_iterator_iface_next_key(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_key == vy_mem_iterator_iface_next_key);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)vitr->priv;
	return vy_mem_iterator_next_key(itr);
}

int
vy_mem_iterator_iface_next_lsn(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_lsn == vy_mem_iterator_iface_next_lsn);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)vitr->priv;
	return vy_mem_iterator_next_lsn(itr);
}

int
vy_mem_iterator_iface_restore(struct vy_tuple_iterator *vitr,
			      struct vy_tuple *last_tuple)
{
	assert(vitr->iface->restore == vy_mem_iterator_iface_restore);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)vitr->priv;
	return vy_mem_iterator_restore(itr, last_tuple);
}

static void
vy_mem_iterator_iface_close(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->close == vy_mem_iterator_iface_close);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)vitr->priv;
	vy_mem_iterator_close(itr);
}

void
vy_mem_iterator_iface_open(struct vy_tuple_iterator *vitr,
			   struct vy_mem *mem,
			   enum vy_order order, char *key, int64_t vlsn)
{
	static struct vy_tuple_iterator_iface iface = {
		.get = vy_mem_iterator_iface_get,
		.next_key = vy_mem_iterator_iface_next_key,
		.next_lsn = vy_mem_iterator_iface_next_lsn,
		.restore = vy_mem_iterator_iface_restore,
		.close = vy_mem_iterator_iface_close
	};
	vitr->iface = &iface;
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *)vitr->priv;
	assert(sizeof(vitr->priv) >= sizeof(*itr));
	vy_mem_iterator_open(itr, mem, order, key, vlsn);
}

/* }} Virtual iterator over mem */

/* {{{ Virtual iterator over transaction writes */

static int
vy_txw_iterator_iface_get(struct vy_tuple_iterator *vitr,
			  struct vy_tuple **result)
{
	assert(vitr->iface->get == vy_txw_iterator_iface_get);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *)vitr->priv;
	return vy_txw_iterator_get(itr, result);
}

static int
vy_txw_iterator_iface_next_key(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_key == vy_txw_iterator_iface_next_key);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *)vitr->priv;
	return vy_txw_iterator_next_key(itr);
}

static int
vy_txw_iterator_iface_next_lsn(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_lsn == vy_txw_iterator_iface_next_lsn);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *)vitr->priv;
	return vy_txw_iterator_next_lsn(itr);
}

static int
vy_txw_iterator_iface_restore(struct vy_tuple_iterator *vitr,
			      struct vy_tuple *last_tuple)
{
	assert(vitr->iface->restore == vy_txw_iterator_iface_restore);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *)vitr->priv;
	return vy_txw_iterator_restore(itr, last_tuple);
}

static void
vy_txw_iterator_iface_close(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->close == vy_txw_iterator_iface_close);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *)vitr->priv;
	vy_txw_iterator_close(itr);
}

void
vy_txw_iterator_iface_open(struct vy_tuple_iterator *vitr,
			   struct vy_index *index, struct vy_tx *tx,
			   enum vy_order order, char *key)
{
	static struct vy_tuple_iterator_iface iface = {
		.get = vy_txw_iterator_iface_get,
		.next_key = vy_txw_iterator_iface_next_key,
		.next_lsn = vy_txw_iterator_iface_next_lsn,
		.restore = vy_txw_iterator_iface_restore,
		.close = vy_txw_iterator_iface_close
	};
	vitr->iface = &iface;
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *)vitr->priv;
	assert(sizeof(vitr->priv) >= sizeof(*itr));
	vy_txw_iterator_open(itr, index, tx, order, key);
}

/* }} Virtual iterator over transaction writes */

/* {{{ Merge iterator */

struct vy_merge_src {
	struct vy_tuple_iterator itr;
	bool is_mutable;
	bool control_eof;
	uint32_t front_id;
};

struct vy_merge_iterator {
	struct vy_merge_src *src;
	uint32_t src_count;
	uint32_t src_capacity;
	uint32_t curr_src;
	uint32_t front_id;
	uint32_t mutable_start;
	uint32_t mutable_end;
	struct key_def *key_def;
	char *key;
	int64_t vlsn;
	enum vy_order order;
	struct vy_tuple *curr_tuple;
	bool unique_optimization;
	bool is_in_uniq_opt;
	bool search_started;
	bool eof_under_control;
};

void
vy_merge_iterator_open(struct vy_merge_iterator *itr, struct key_def *key_def, enum vy_order order, char *key, int64_t vlsn)
{
	itr->key_def = key_def;
	itr->key = key;
	itr->vlsn = vlsn;
	itr->order = order;
	itr->src = NULL;
	itr->src_count = 0;
	itr->src_capacity = 0;
	itr->src = NULL;
	itr->curr_src = UINT32_MAX;
	itr->front_id = 1;
	itr->mutable_start = 0;
	itr->mutable_end = 0;
	itr->curr_tuple = NULL;
	itr->unique_optimization = key != NULL &&
		(order == VINYL_EQ || order == VINYL_GE || order == VINYL_LE) &&
		vy_tuple_key_is_full(key, key_def);
	itr->is_in_uniq_opt = false;
	itr->search_started = false;
	itr->eof_under_control = false;
}

void
vy_merge_iterator_close(struct vy_merge_iterator *itr)
{
	if (itr->curr_tuple != NULL) {
		vy_tuple_unref(itr->curr_tuple);
		itr->curr_tuple = NULL;
	}
	for (size_t i = 0; i < itr->src_count; i++)
		itr->src[i].itr.iface->close(&itr->src[i].itr);
	free(itr->src);
	itr->src_count = 0;
	itr->src_capacity = 0;
	itr->src = NULL;
}

int
vy_merge_iterator_reserve(struct vy_merge_iterator *itr, uint32_t capacity)
{
	if (itr->src_capacity >= capacity)
		return 0;
	struct vy_merge_src *new_src = malloc(capacity * sizeof(*new_src));
	if (new_src == NULL)
		return -1;
	if (itr->src_count > 0) {
		memcpy(new_src, itr->src, itr->src_count * sizeof(*new_src));
		free(itr->src);
	}
	itr->src = new_src;
	itr->src_capacity = capacity;
	return 0;
}

struct vy_tuple_iterator *
vy_merge_iterator_add(struct vy_merge_iterator *itr, bool is_mutable, bool control_eof)
{
	assert(!itr->search_started);
	if (itr->src_count == itr->src_capacity) {
		if (vy_merge_iterator_reserve(itr, itr->src_count + 1) != 0)
			return NULL;
	}
	if (is_mutable) {
		if (itr->mutable_start == itr->mutable_end)
			itr->mutable_start = itr->src_count;
		itr->mutable_end = itr->src_count + 1;
	}
	itr->src[itr->src_count].front_id = 0;
	struct vy_merge_src *src = &itr->src[itr->src_count++];
	src->is_mutable = is_mutable;
	src->control_eof = control_eof;
	return &src->itr;
}

int
vy_merge_iterator_propagate(struct vy_merge_iterator *itr)
{
	for (uint32_t i = 0; i < itr->src_count; i++) {
		if (itr->src[i].front_id != itr->front_id)
			continue;
		if (itr->src[i].itr.iface->next_key(&itr->src[i].itr) < 0)
			return -1;
	}
	itr->front_id++;
	return 0;
}

int
vy_merge_iterator_locate_uniq_opt(struct vy_merge_iterator *itr)
{
	assert(itr->src_count);
	itr->eof_under_control = false;
	itr->search_started = true;
	itr->unique_optimization = false;
	struct vy_tuple *min_tuple;
restart:
	itr->is_in_uniq_opt = false;
	min_tuple = NULL;
	itr->curr_src = UINT32_MAX;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		struct vy_tuple_iterator *sub_itr = &itr->src[i].itr;
		struct vy_tuple *t;
		int rc = sub_itr->iface->get(sub_itr, &t);
		if (rc > 0)
			continue;
		if (rc < 0)
			return rc;
		if (vy_tuple_compare(t->data, itr->key, itr->key_def) == 0) {
			itr->src[i].front_id = ++itr->front_id;
			min_tuple = t;
			itr->curr_src = i;
			itr->is_in_uniq_opt = true;
			break;
		}
		int cmp = min_tuple == NULL ? -1 :
			  vy_tuple_compare(t->data, min_tuple->data, itr->key_def);
		if (cmp == 0) {
			itr->src[i].front_id = itr->front_id;
		} else if (cmp < 0) {
			itr->src[i].front_id = ++itr->front_id;
			min_tuple = t;
			itr->curr_src = i;
		}
	}
	bool must_restart = false;
	for (uint32_t i = itr->mutable_start; i < itr->mutable_end; i++) {
		struct vy_tuple_iterator *sub_itr = &itr->src[i].itr;
		int rc = sub_itr->iface->restore(sub_itr, NULL);
		if (rc < 0)
			return rc;
		if (rc > 0)
			must_restart = true;
	}
	if (must_restart)
		goto restart;
	if (itr->curr_tuple != NULL)
		vy_tuple_unref(itr->curr_tuple);
	itr->curr_tuple = min_tuple;
	if (min_tuple != NULL) {
		vy_tuple_ref(itr->curr_tuple);
		return 0;
	}
	return 1;
}

int
vy_merge_iterator_locate(struct vy_merge_iterator *itr)
{
	if (itr->src_count == 0)
		return 1;
	if (itr->unique_optimization)
		return vy_merge_iterator_locate_uniq_opt(itr);
	itr->search_started = true;
	struct vy_tuple *min_tuple = NULL;
	itr->curr_src = UINT32_MAX;
	itr->eof_under_control = true;
	for (uint32_t i = itr->src_count; i--;) {
		struct vy_tuple_iterator *sub_itr = &itr->src[i].itr;
		if (itr->src[i].is_mutable)
			sub_itr->iface->restore(sub_itr, itr->curr_tuple);
		struct vy_tuple *t;
		int rc = sub_itr->iface->get(sub_itr, &t);
		if (rc < 0)
			return rc;
		if (rc > 0)
			continue;
		itr->eof_under_control = itr->eof_under_control && !itr->src[i].control_eof;
		int cmp = min_tuple == NULL ? -1 :
			vy_tuple_compare(t->data, min_tuple->data, itr->key_def);
		if (cmp <= 0) {
			itr->front_id += cmp < 0;
			itr->src[i].front_id = itr->front_id;
			min_tuple = t;
			itr->curr_src = i;
		}
	}
	if (itr->curr_tuple != NULL)
		vy_tuple_unref(itr->curr_tuple);
	itr->curr_tuple = min_tuple;
	if (min_tuple != NULL) {
		vy_tuple_ref(itr->curr_tuple);
		return 0;
	}
	return 1;
}

int
vy_merge_iterator_get(struct vy_merge_iterator *itr, struct vy_tuple **result)
{
	if (!itr->search_started && vy_merge_iterator_locate(itr) < 0)
		return -1;
	*result = itr->curr_tuple;
	return itr->curr_tuple != NULL ? 0 : 1;
}

int
vy_merge_iterator_next_key(struct vy_merge_iterator *itr)
{
	if (!itr->search_started && vy_merge_iterator_locate(itr) < 0)
		return -1;
	if (itr->is_in_uniq_opt) {
		itr->is_in_uniq_opt = false;
		if (vy_merge_iterator_locate(itr) < 0)
			return -1;
	}
	if (vy_merge_iterator_propagate(itr) < 0)
		return -1;
	return vy_merge_iterator_locate(itr);
}

int
vy_merge_iterator_next_lsn(struct vy_merge_iterator *itr)
{
	if (!itr->search_started && vy_merge_iterator_locate(itr) < 0)
		return -1;
	if (itr->curr_src == UINT32_MAX)
		return 1;
	struct vy_tuple_iterator *sub_itr = &itr->src[itr->curr_src].itr;
	int rc = sub_itr->iface->next_lsn(sub_itr);
	if (rc < 0) {
		return rc;
	} else if (rc == 0) {
		if (itr->curr_tuple != NULL)
			vy_tuple_unref(itr->curr_tuple);
		rc = sub_itr->iface->get(sub_itr, &itr->curr_tuple);
		assert(rc == 0);
		vy_tuple_ref(itr->curr_tuple);
		return rc;
	}
	for (uint32_t i = itr->curr_src + 1; i < itr->src_count; i++) {
		if (itr->is_in_uniq_opt) {
			sub_itr = &itr->src[i].itr;
			struct vy_tuple *t;
			rc = sub_itr->iface->get(sub_itr, &t);
			if (rc < 0)
				return -1;
			if (rc > 0)
				continue;
			if (vy_tuple_compare(t->data, itr->key, itr->key_def) == 0) {
				itr->src[i].front_id = itr->front_id;
				itr->curr_src = i;
				if (itr->curr_tuple != NULL)
					vy_tuple_unref(itr->curr_tuple);
				itr->curr_tuple = t;
				vy_tuple_ref(t);
				return 0;
			}

		} else if (itr->src[i].front_id == itr->front_id) {
			sub_itr = &itr->src[i].itr;
			itr->curr_src = i;
			if (itr->curr_tuple != NULL)
				vy_tuple_unref(itr->curr_tuple);
			sub_itr->iface->get(sub_itr, &itr->curr_tuple);
			vy_tuple_ref(itr->curr_tuple);
			return 0;
		}
	}
	itr->is_in_uniq_opt = false;
	return 1;
}

int
vy_merge_iterator_restore(struct vy_merge_iterator *itr,
			  struct vy_tuple *last_tuple)
{
	int result = 0;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		struct vy_tuple_iterator *sub_itr = &itr->src[i].itr;
		int rc = sub_itr->iface->restore(sub_itr, last_tuple);
		if (rc < 0)
			return rc;
		result = result || rc;
	}
	return result;
}

/* }}} Merge iterator */

/* {{{ Iterator over index */

struct vy_read_iterator {
	struct vy_index *index;
	struct vy_tx *tx;

	enum vy_order order;
	char *key;
	int64_t vlsn;

	struct vy_rangeiter range_itr;
	struct vy_range *curr_range;
	struct vy_merge_iterator merge_itr;
	struct vy_tuple *curr_tuple;

	uint32_t range_index_version;
	uint32_t range_version;
};

void
vy_read_iterator_open(struct vy_read_iterator *itr,
		      struct vy_index *index, struct vy_tx *tx,
		      enum vy_order order, char *key, int64_t vlsn);

int
vy_read_iterator_get(struct vy_read_iterator *itr, struct vy_tuple **result);

int
vy_read_iterator_next(struct vy_read_iterator *itr);

void
vy_read_iterator_close(struct vy_read_iterator *itr);

void
vy_read_iterator_use_range(struct vy_read_iterator *itr)
{
	if (itr->tx != NULL) {
		struct vy_tuple_iterator *sub_itr = vy_merge_iterator_add(&itr->merge_itr, true, false);
		vy_txw_iterator_iface_open(sub_itr, itr->index, itr->tx, itr->order, itr->key);
		sub_itr->iface->restore(sub_itr, itr->curr_tuple);
	}
	if (itr->curr_range == NULL)
		return;
	itr->range_version = itr->curr_range->range_version;

	struct vy_mem *second;
	struct vy_mem *first = vy_range_index_priority(itr->curr_range, &second);

	struct vy_tuple_iterator *sub_itr = vy_merge_iterator_add(&itr->merge_itr, true, true);
	vy_mem_iterator_iface_open(sub_itr, first, itr->order, itr->key, itr->vlsn);

	if (second != NULL && second->tree.size != 0) {
		sub_itr = vy_merge_iterator_add(&itr->merge_itr, false, true);
		vy_mem_iterator_iface_open(sub_itr, second, itr->order, itr->key, itr->vlsn);
	}

	struct vy_run *run = itr->curr_range->run;
	struct vy_filterif *compression = itr->index->compression_if;
	while (run != NULL) {
		sub_itr = vy_merge_iterator_add(&itr->merge_itr, false, true);
		vy_run_iterator_iface_open(sub_itr, itr->index, run,
					   itr->curr_range->fd, compression,
					   itr->order, itr->key, itr->vlsn);
		run = run->next;
	}

}

void
vy_read_iterator_open(struct vy_read_iterator *itr,
		      struct vy_index *index, struct vy_tx *tx,
		      enum vy_order order, char *key, int64_t vlsn)
{
	itr->index = index;
	itr->tx = tx;
	itr->order = order;
	itr->key = key;
	itr->vlsn = vlsn;

	itr->curr_tuple = NULL;
	vy_rangeiter_open(&itr->range_itr, index, order == VINYL_EQ ? VINYL_GE : order, key, 0);
	itr->curr_range = vy_rangeiter_get(&itr->range_itr);
	vy_merge_iterator_open(&itr->merge_itr, index->key_def, order, key, vlsn);
	vy_read_iterator_use_range(itr);
	itr->range_index_version = index->range_index_version;
}

int
vy_read_iterator_next_range(struct vy_read_iterator *itr)
{
	assert(itr->curr_range != NULL);
	vy_merge_iterator_close(&itr->merge_itr);
	vy_merge_iterator_open(&itr->merge_itr, itr->index->key_def, itr->order, itr->key, itr->vlsn);
	vy_rangeiter_next(&itr->range_itr);
	itr->curr_range = vy_rangeiter_get(&itr->range_itr);
	if (itr->curr_range != NULL && itr->order == VINYL_EQ) {
		struct vy_page_info *min = vy_run_index_first_page(&itr->curr_range->run->index);
		char *min_key_data = vy_run_index_min_key(&itr->curr_range->run->index, min);
		if (vy_tuple_compare(min_key_data, itr->key, itr->index->key_def) > 0)
			itr->curr_range = NULL;
	}
	vy_read_iterator_use_range(itr);
	struct vy_tuple *t;
	int rc = vy_merge_iterator_get(&itr->merge_itr, &t);
	if (rc >= 0 && itr->merge_itr.eof_under_control && itr->curr_range != NULL)
		return vy_read_iterator_next_range(itr);
	if (itr->curr_tuple != NULL)
		vy_tuple_unref(itr->curr_tuple);
	itr->curr_tuple = t;
	if (itr->curr_tuple != NULL)
		vy_tuple_ref(itr->curr_tuple);
	return rc;
}

void
vy_read_iterator_check_versions(struct vy_read_iterator *itr)
{
	if (itr->range_index_version != itr->index->range_index_version) {
		itr->range_index_version = itr->index->range_index_version;
		char *key = itr->curr_tuple != 0 ? itr->curr_tuple->data : itr->key;
		enum vy_order order = itr->order == VINYL_EQ ? VINYL_GE : itr->order;
		//vy_rangeiter_close(&itr->range_itr);
		vy_rangeiter_open(&itr->range_itr, itr->index, order, key, 0);
		struct vy_range *range = vy_rangeiter_get(&itr->range_itr);
		if (range != itr->curr_range) {
			itr->range_version = range == NULL ? 1 : range->range_version + 1;
			itr->curr_range = range;
		}
	}
	uint32_t range_version = itr->curr_range == NULL ? 0 : itr->curr_range->range_version;
	if (itr->range_version != range_version) {
		itr->range_version = range_version;
		vy_merge_iterator_close(&itr->merge_itr);
		vy_merge_iterator_open(&itr->merge_itr, itr->index->key_def, itr->order, itr->key, itr->vlsn);
		vy_read_iterator_use_range(itr);
		vy_merge_iterator_restore(&itr->merge_itr, itr->curr_tuple);
	}
}

int
vy_read_iterator_next(struct vy_read_iterator *itr)
{
	vy_read_iterator_check_versions(itr);
	int rc = vy_merge_iterator_next_key(&itr->merge_itr);
	if (rc >= 0 && itr->merge_itr.eof_under_control && itr->curr_range != NULL)
		rc = vy_read_iterator_next_range(itr);
	return rc;

}

int
vy_read_iterator_get(struct vy_read_iterator *itr, struct vy_tuple **result)
{
	vy_read_iterator_check_versions(itr);
	int rc;
	while (true) {
		struct vy_tuple *t;
		rc = vy_merge_iterator_get(&itr->merge_itr, &t);
		if (rc >= 0 && itr->merge_itr.eof_under_control && itr->curr_range != NULL) {
			rc = vy_read_iterator_next_range(itr);
			if (rc == 0)
				vy_merge_iterator_get(&itr->merge_itr, &t);
		}
		if (rc != 0) {
			return rc;
		}
		if (itr->curr_tuple != NULL)
			vy_tuple_unref(itr->curr_tuple);
		itr->curr_tuple = t;
		vy_tuple_ref(itr->curr_tuple);
		while (itr->curr_tuple->flags & SVUPSERT) {
			int rc = vy_merge_iterator_next_lsn(&itr->merge_itr);
			if (rc < 0) {
				return rc;
			}
			struct vy_tuple *next = NULL;
			if (rc == 0)
				vy_merge_iterator_get(&itr->merge_itr, &next);
			struct vy_tuple *applied =
				vy_apply_upsert(itr->curr_tuple, next,
						itr->index, true);
			if (applied == NULL) {
				return -1;
			}
			vy_tuple_unref(itr->curr_tuple);
			itr->curr_tuple = applied;
		}
		if (rc != 0 || (itr->curr_tuple->flags & SVDELETE) == 0)
			break;
		rc = vy_read_iterator_next(itr);
		if (rc != 0)
			break;
	}
	*result = itr->curr_tuple;
	return rc;
}

void
vy_read_iterator_close(struct vy_read_iterator *itr)
{
	if (itr->curr_tuple != NULL)
		vy_tuple_unref(itr->curr_tuple);
	itr->curr_tuple = NULL;
	vy_merge_iterator_close(&itr->merge_itr);
}

/* }}} Iterator over index */

int
vy_index_read(struct vy_index *index, struct vy_tuple *key,
	      enum vy_order order, struct vy_tuple **result, struct vy_tx *tx)
{
	struct vy_env *e = index->env;
	uint64_t start  = clock_monotonic64();

	int64_t vlsn = tx != NULL ? tx->vlsn : e->xm->lsn;

	struct vy_read_iterator itr;
	vy_read_iterator_open(&itr, index, tx, order, key->data, vlsn);
	int rc = vy_read_iterator_get(&itr, result);
	if (rc == 0) {
		vy_tuple_ref(*result);
	} else if (rc > 0) {
		rc = 0;
		*result = NULL;
	}
	vy_read_iterator_close(&itr);

	struct vy_stat_get statget;
	statget.read_disk = 0; // q.read_disk;
	statget.read_cache = 0; // q.read_cache;
	statget.read_latency = clock_monotonic64() - start;
	vy_stat_get(e->stat, &statget);

	return rc;
}

static int
vy_readcommited(struct vy_index *index, struct vy_tuple *tuple)
{
	struct vy_read_iterator itr;
	vy_read_iterator_open(&itr, index, NULL, VINYL_EQ, tuple->data, INT64_MAX);
	struct vy_tuple *t;
	int rc = vy_read_iterator_get(&itr, &t);
	if (rc == 0) {
		if (t->lsn > tuple->lsn)
			rc = 1;
	} else if (rc > 0) {
		rc = 0;
	}
	vy_read_iterator_close(&itr);
	return rc;
}
