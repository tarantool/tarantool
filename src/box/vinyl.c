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
#include <msgpuck/msgpuck.h>

#include "trivia/util.h"
#include "crc32.h"
#include "clock.h"
#include "trivia/config.h"
#include "tt_pthread.h"
#include "cfg.h"
#include "diag.h"
#include "fiber.h" /* cord_slab_cache() */
#include "coeio.h"

#include "errcode.h"
#include "key_def.h"
#include "tuple.h"
#include "tuple_update.h"
#include "txn.h" /* box_txn_alloc() */

#define ssunused __attribute__((unused))

#define ss_cmp(a, b) \
	((a) == (b) ? 0 : (((a) > (b)) ? 1 : -1))

struct sspath {
	char path[PATH_MAX];
};

static inline void
ss_pathinit(struct sspath *p)
{
	p->path[0] = 0;
}

static inline void
ss_pathset(struct sspath *p, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(p->path, sizeof(p->path), fmt, args);
	va_end(args);
}

static inline void
ss_path(struct sspath *p, char *dir, uint64_t id, char *ext)
{
	ss_pathset(p, "%s/%020"PRIu64"%s", dir, id, ext);
}

static inline void
ss_pathcompound(struct sspath *p, char *dir, uint64_t a, uint64_t b, char *ext)
{
	ss_pathset(p, "%s/%020"PRIu64".%020"PRIu64"%s", dir, a, b, ext);
}

static inline char*
ss_pathof(struct sspath *p) {
	return p->path;
}

static inline int
ss_pathis_set(struct sspath *p) {
	return p->path[0] != 0;
}

struct ssiov {
	struct iovec *v;
	int iovmax;
	int iovc;
};

static inline void
ss_iovinit(struct ssiov *v, struct iovec *vp, int max)
{
	v->v = vp;
	v->iovc = 0;
	v->iovmax = max;
}

static inline void
ss_iovadd(struct ssiov *v, void *ptr, size_t size)
{
	assert(v->iovc < v->iovmax);
	v->v[v->iovc].iov_base = ptr;
	v->v[v->iovc].iov_len = size;
	v->iovc++;
}

struct ssmmap {
	char *p;
	size_t size;
};

struct PACKED ssfile {
	int fd;
	uint64_t size;
	int creat;
	struct sspath path;
};

static inline void
ss_fileinit(struct ssfile *f)
{
	ss_pathinit(&f->path);
	f->fd    = -1;
	f->size  = 0;
	f->creat = 0;
}

static inline int
ss_fileopen_as(struct ssfile *f, char *path, int flags)
{
	f->creat = (flags & O_CREAT ? 1 : 0);
	f->fd = open(path, flags, 0644);
	if (unlikely(f->fd == -1))
		return -1;
	ss_pathset(&f->path, "%s", path);
	f->size = 0;
	if (f->creat)
		return 0;
	struct stat st;
	int rc = lstat(path, &st);
	if (unlikely(rc == -1)) {
		close(f->fd);
		f->fd = -1;
		return -1;
	}
	f->size = st.st_size;
	return 0;
}

static inline int
ss_fileopen(struct ssfile *f, char *path) {
	return ss_fileopen_as(f, path, O_RDWR);
}

static inline int
ss_filenew(struct ssfile *f, char *path) {
	return ss_fileopen_as(f, path, O_RDWR|O_CREAT);
}

static inline int
ss_fileclose(struct ssfile *f)
{
	if (unlikely(f->fd != -1)) {
		int rc = close(f->fd);
		if (unlikely(rc == -1))
			return -1;
		f->fd  = -1;
	}
	return 0;
}

static inline int
ss_filerename(struct ssfile *f, char *path)
{
	int rc = rename(ss_pathof(&f->path), path);
	if (unlikely(rc == -1))
		return -1;
	ss_pathset(&f->path, "%s", path);
	return 0;
}

static inline int
ss_filesync(struct ssfile *f) {
	return fdatasync(f->fd);
}

static inline int
ss_fileadvise(struct ssfile *f, int hint, uint64_t off, uint64_t len) {
	(void)hint;
#if !defined(HAVE_POSIX_FADVISE)
	(void)f;
	(void)off;
	(void)len;
	return 0;
#else
	return posix_fadvise(f->fd, off, len, POSIX_FADV_DONTNEED);
#endif
}

static inline int
ss_fileresize(struct ssfile *f, uint64_t size)
{
	int rc = ftruncate(f->fd, size);
	if (unlikely(rc == -1))
		return -1;
	f->size = size;
	return 0;
}

static inline int
ss_filepread(struct ssfile *f, uint64_t off, void *buf, int size)
{
	int64_t n = 0;
	do {
		int r;
		do {
			r = pread(f->fd, (char*)buf + n, size - n, off + n);
		} while (r == -1 && errno == EINTR);
		if (r <= 0)
			return -1;
		n += r;
	} while (n != size);

	if (unlikely(n == -1))
		return -1;
	assert(n == size);
	return n;
}

static inline int
ss_filepwrite(struct ssfile *f, uint64_t off, void *buf, int size)
{
	int n = 0;
	do {
		int r;
		do {
			r = pwrite(f->fd, (char*)buf + n, size - n, off + n);
		} while (r == -1 && errno == EINTR);
		if (r <= 0)
			return -1;
		n += r;
	} while (n != size);
	assert(n == size);
	return n;
}

static inline int
ss_filewrite(struct ssfile *f, void *buf, int size)
{
	int n = 0;
	do {
		int r;
		do {
			r = write(f->fd, (char*)buf + n, size - n);
		} while (r == -1 && errno == EINTR);
		if (r <= 0)
			return -1;
		n += r;
	} while (n != size);
	assert(n == size);
	//FIXME: this may be incorrect
	f->size += n;
	return n;
}

static inline int
ss_filewritev(struct ssfile *f, struct ssiov *iov)
{
	struct iovec *v = iov->v;
	int n = iov->iovc;
	int size = 0;
	do {
		int r;
		do {
			r = writev(f->fd, v, n);
		} while (r == -1 && errno == EINTR);
		if (r < 0)
			return -1;
		size += r;
		while (n > 0) {
			if (v->iov_len > (size_t)r) {
				v->iov_base = (char*)v->iov_base + r;
				v->iov_len -= r;
				break;
			} else {
				r -= v->iov_len;
				v++;
				n--;
			}
		}
	} while (n > 0);
	//FIXME: this may be incorrect
	f->size += size;
	return size;
}

static inline int
ss_fileseek(struct ssfile *f, uint64_t off)
{
	return lseek(f->fd, off, SEEK_SET);
}

struct ssbuf {
	char *s, *p, *e;
};

static inline void
ss_bufinit(struct ssbuf *b)
{
	b->s = NULL;
	b->p = NULL;
	b->e = NULL;
}

static inline void
ss_buffree(struct ssbuf *b)
{
	if (unlikely(b->s == NULL))
		return;
	free(b->s);
	b->s = NULL;
	b->p = NULL;
	b->e = NULL;
}

static inline size_t
ss_bufsize(struct ssbuf *b) {
	return b->e - b->s;
}

static inline size_t
ss_bufused(struct ssbuf *b) {
	return b->p - b->s;
}

static inline size_t
ss_bufunused(struct ssbuf *b) {
	return b->e - b->p;
}

static inline void
ss_bufreset(struct ssbuf *b) {
	b->p = b->s;
}

static inline void
ss_bufgc(struct ssbuf *b, size_t wm)
{
	if (unlikely(ss_bufsize(b) >= wm)) {
		ss_buffree(b);
		ss_bufinit(b);
		return;
	}
	ss_bufreset(b);
}

static inline int
ss_bufensure(struct ssbuf *b, size_t size)
{
	if (likely(b->e - b->p >= (ptrdiff_t)size))
		return 0;
	size_t sz = ss_bufsize(b) * 2;
	size_t actual = ss_bufused(b) + size;
	if (unlikely(actual > sz))
		sz = actual;
	char *p;
	if (b->s == NULL) {
		p = malloc(sz);
		if (unlikely(p == NULL))
			return -1;
	} else {
		p = realloc(b->s, sz);
		if (unlikely(p == NULL))
			return -1;
	}
	b->p = p + (b->p - b->s);
	b->e = p + sz;
	b->s = p;
	assert((b->e - b->p) >= (ptrdiff_t)size);
	return 0;
}

static inline void
ss_bufadvance(struct ssbuf *b, size_t size)
{
	b->p += size;
}

static inline int
ss_bufadd(struct ssbuf *b, void *buf, size_t size)
{
	int rc = ss_bufensure(b, size);
	if (unlikely(rc == -1))
		return -1;
	memcpy(b->p, buf, size);
	ss_bufadvance(b, size);
	return 0;
}

static inline int
ss_bufin(struct ssbuf *b, void *v) {
	assert(b->s != NULL);
	return (char*)v >= b->s && (char*)v < b->p;
}

static inline void*
ss_bufat(struct ssbuf *b, int size, int i) {
	return b->s + size * i;
}

static inline void
ss_bufset(struct ssbuf *b, int size, int i, char *buf, size_t bufsize)
{
	assert(b->s + (size * i + bufsize) <= b->p);
	memcpy(b->s + size * i, buf, bufsize);
}

#define SS_INJECTION_SD_BUILD_0      0
#define SS_INJECTION_SD_BUILD_1      1
#define SS_INJECTION_SI_BRANCH_0     2
#define SS_INJECTION_SI_COMPACTION_0 3
#define SS_INJECTION_SI_COMPACTION_1 4
#define SS_INJECTION_SI_COMPACTION_2 5
#define SS_INJECTION_SI_COMPACTION_3 6
#define SS_INJECTION_SI_COMPACTION_4 7
#define SS_INJECTION_SI_RECOVER_0    8

#ifdef SS_INJECTION_ENABLE
	#define SS_INJECTION(E, ID, X) \
	if ((E)->e[(ID)]) { \
		X; \
	} else {}
#else
	#define SS_INJECTION(E, ID, X)
#endif

#define ss_crcp(p, size, crc) \
	crc32_calc(crc, p, size)

#define ss_crcs(p, size, crc) \
	crc32_calc(crc, (char*)p + sizeof(uint32_t), size - sizeof(uint32_t))

enum sstype {
	SS_UNDEF,
	SS_STRING,
	SS_STRINGPTR,
	SS_U32,
	SS_U64,
};

enum ssquotaop {
	SS_QADD,
	SS_QREMOVE
};

struct ssquota {
	bool enable;
	int wait;
	int64_t limit;
	int64_t used;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

static void ss_quotainit(struct ssquota*, int64_t);
static void ss_quotaenable(struct ssquota*);
static int ss_quotafree(struct ssquota*);
static int ss_quota(struct ssquota*, enum ssquotaop, int64_t);

static inline uint64_t
ss_quotaused(struct ssquota *q)
{
	tt_pthread_mutex_lock(&q->lock);
	uint64_t used = q->used;
	tt_pthread_mutex_unlock(&q->lock);
	return used;
}

static inline int
ss_quotaused_percent(struct ssquota *q)
{
	tt_pthread_mutex_lock(&q->lock);
	int percent;
	if (q->limit == 0) {
		percent = 0;
	} else {
		percent = (q->used * 100) / q->limit;
	}
	tt_pthread_mutex_unlock(&q->lock);
	return percent;
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
	if (unlikely(q->q == NULL))
		return -1;
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

enum ssfilterop {
	SS_FINPUT,
	SS_FOUTPUT
};

struct ssfilter;

struct ssfilterif {
	char *name;
	int (*init)(struct ssfilter*, va_list);
	int (*free)(struct ssfilter*);
	int (*start)(struct ssfilter*, struct ssbuf*);
	int (*next)(struct ssfilter*, struct ssbuf*, char*, int);
	int (*complete)(struct ssfilter*, struct ssbuf*);
};

struct ssfilter {
	struct ssfilterif *i;
	enum ssfilterop op;
	char priv[90];
};

static inline int
ss_filterinit(struct ssfilter *c, struct ssfilterif *ci, enum ssfilterop op, ...)
{
	c->op = op;
	c->i  = ci;
	va_list args;
	va_start(args, op);
	int rc = c->i->init(c, args);
	va_end(args);
	return rc;
}

static inline int
ss_filterfree(struct ssfilter *c)
{
	return c->i->free(c);
}

static inline int
ss_filterstart(struct ssfilter *c, struct ssbuf *dest)
{
	return c->i->start(c, dest);
}

static inline int
ss_filternext(struct ssfilter *c, struct ssbuf *dest, char *buf, int size)
{
	return c->i->next(c, dest, buf, size);
}

static inline int
ss_filtercomplete(struct ssfilter *c, struct ssbuf *dest)
{
	return c->i->complete(c, dest);
}

static struct ssfilterif ss_nonefilter;

static struct ssfilterif ss_lz4filter;

static struct ssfilterif ss_zstdfilter;

static inline struct ssfilterif*
ss_filterof(char *name)
{
	if (strcmp(name, "none") == 0)
		return &ss_nonefilter;
	if (strcmp(name, "lz4") == 0)
		return &ss_lz4filter;
	if (strcmp(name, "zstd") == 0)
		return &ss_zstdfilter;
	return NULL;
}

struct ssiter;

struct ssiterif {
	void  (*close)(struct ssiter*);
	int   (*has)(struct ssiter*);
	void *(*get)(struct ssiter*);
	void  (*next)(struct ssiter*);
};

struct ssiter {
	struct ssiterif *vif;
	char priv[150];
};

#define ss_iteratorof(i) (i)->vif->get(i)
#define ss_iteratornext(i) (i)->vif->next(i)

static struct ssiterif ss_bufiterrefif;

struct ssbufiter {
	struct ssbuf *buf;
	int vsize;
	void *v;
};

static inline void
ss_bufiter_open(struct ssbufiter *bi, struct ssbuf *buf, int vsize)
{
	bi->buf = buf;
	bi->vsize = vsize;
	bi->v = bi->buf->s;
	if (bi->v != NULL && ! ss_bufin(bi->buf, bi->v))
		bi->v = NULL;
}

static inline int
ss_bufiter_has(struct ssbufiter *bi)
{
	return bi->v != NULL;
}

static inline void *
ss_bufiter_get(struct ssbufiter *bi)
{
	return bi->v;
}

static inline void
ss_bufiter_next(struct ssbufiter *bi)
{
	if (unlikely(bi->v == NULL))
		return;
	bi->v = (char*)bi->v + bi->vsize;
	if (unlikely(! ss_bufin(bi->buf, bi->v)))
		bi->v = NULL;
}

static inline void
ss_bufiterref_open(struct ssiter *i, struct ssbuf *buf, int vsize)
{
	i->vif = &ss_bufiterrefif;
	struct ssbufiter *bi = (struct ssbufiter*)i->priv;
	ss_bufiter_open(bi, buf, vsize);
}

static inline void
ss_bufiterref_close(struct ssiter *i)
{
	(void) i;
}

static inline int
ss_bufiterref_has(struct ssiter *i)
{
	struct ssbufiter *bi = (struct ssbufiter*)i->priv;
	return ss_bufiter_has(bi);
}

static inline void*
ss_bufiterref_get(struct ssiter *i)
{
	struct ssbufiter *bi = (struct ssbufiter*)i->priv;
	if (unlikely(bi->v == NULL))
		return NULL;
	return *(void**)bi->v;
}

static inline void
ss_bufiterref_next(struct ssiter *i)
{
	struct ssbufiter *bi = (struct ssbufiter*)i->priv;
	ss_bufiter_next(bi);
}

struct ssavg {
	uint64_t count;
	uint64_t total;
	uint32_t min, max;
	double   avg;
	char sz[32];
};

static inline void
ss_avgupdate(struct ssavg *a, uint32_t v)
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
ss_avgprepare(struct ssavg *a)
{
	snprintf(a->sz, sizeof(a->sz), "%"PRIu32" %"PRIu32" %.1f",
	         a->min, a->max, a->avg);
}

static struct ssiterif ss_bufiterrefif =
{
	.close   = ss_bufiterref_close,
	.has     = ss_bufiterref_has,
	.get     = ss_bufiterref_get,
	.next    = ss_bufiterref_next
};

struct sslz4filter {
	LZ4F_compressionContext_t compress;
	LZ4F_decompressionContext_t decompress;
	size_t total_size;
};

static int
ss_lz4filter_init(struct ssfilter *f, va_list args ssunused)
{
	struct sslz4filter *z = (struct sslz4filter*)f->priv;
	LZ4F_errorCode_t rc = -1;
	switch (f->op) {
	case SS_FINPUT:
		rc = LZ4F_createCompressionContext(&z->compress, LZ4F_VERSION);
		z->total_size = 0;
		break;
	case SS_FOUTPUT:
		rc = LZ4F_createDecompressionContext(&z->decompress,
						     LZ4F_VERSION);
		break;
	}
	if (unlikely(rc != 0))
		return -1;
	return 0;
}

static int
ss_lz4filter_free(struct ssfilter *f)
{
	struct sslz4filter *z = (struct sslz4filter*)f->priv;
	(void)z;
	switch (f->op) {
	case SS_FINPUT:
		LZ4F_freeCompressionContext(z->compress);
		break;
	case SS_FOUTPUT:
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
ss_lz4filter_start(struct ssfilter *f, struct ssbuf *dest)
{
	struct sslz4filter *z = (struct sslz4filter*)f->priv;
	int rc;
	size_t block;
	size_t sz;
	switch (f->op) {
	case SS_FINPUT:;
		block = LZ4F_MAXHEADERFRAME_SIZE;
		rc = ss_bufensure(dest, block);
		if (unlikely(rc == -1))
			return -1;
		sz = LZ4F_compressBegin(z->compress, dest->p, block, NULL);
		if (unlikely(LZ4F_isError(sz)))
			return -1;
		ss_bufadvance(dest, sz);
		break;
	case SS_FOUTPUT:
		/* do nothing */
		break;
	}
	return 0;
}

static int
ss_lz4filter_next(struct ssfilter *f, struct ssbuf *dest, char *buf, int size)
{
	struct sslz4filter *z = (struct sslz4filter*)f->priv;
	if (unlikely(size == 0))
		return 0;
	int rc;
	switch (f->op) {
	case SS_FINPUT:;
		/* See comments in ss_lz4filter_complete() */
		int capacity = LZ4F_compressBound(z->total_size + size, NULL);
		assert(capacity >= (ptrdiff_t)ss_bufused(dest));
		rc = ss_bufensure(dest, capacity);
		if (unlikely(rc == -1))
			return -1;
		size_t sz = LZ4F_compressUpdate(z->compress, dest->p,
						ss_bufunused(dest),
						buf, size, NULL);
		if (unlikely(LZ4F_isError(sz)))
			return -1;
		ss_bufadvance(dest, sz);
		z->total_size += size;
		break;
	case SS_FOUTPUT:;
		/* do a single-pass decompression.
		 *
		 * Assume that destination buffer is allocated to
		 * original size.
		 */
		size_t pos = 0;
		while (pos < (size_t)size)
		{
			size_t o_size = ss_bufunused(dest);
			size_t i_size = size - pos;
			LZ4F_errorCode_t rc;
			rc = LZ4F_decompress(z->decompress, dest->p, &o_size,
					     buf + pos, &i_size, NULL);
			if (LZ4F_isError(rc))
				return -1;
			ss_bufadvance(dest, o_size);
			pos += i_size;
		}
		break;
	}
	return 0;
}

static int
ss_lz4filter_complete(struct ssfilter *f, struct ssbuf *dest)
{
	struct sslz4filter *z = (struct sslz4filter*)f->priv;
	int rc;
	switch (f->op) {
	case SS_FINPUT:;
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
		assert(capacity >= (ptrdiff_t)ss_bufused(dest));
		rc = ss_bufensure(dest, capacity);
		if (unlikely(rc == -1))
			return -1;
		size_t sz = LZ4F_compressEnd(z->compress, dest->p,
					     ss_bufunused(dest), NULL);
		if (unlikely(LZ4F_isError(sz)))
			return -1;
		ss_bufadvance(dest, sz);
		break;
	case SS_FOUTPUT:
		/* do nothing */
		break;
	}
	return 0;
}

static struct ssfilterif ss_lz4filter =
{
	.name     = "lz4",
	.init     = ss_lz4filter_init,
	.free     = ss_lz4filter_free,
	.start    = ss_lz4filter_start,
	.next     = ss_lz4filter_next,
	.complete = ss_lz4filter_complete
};

static int
ss_nonefilter_init(struct ssfilter *f ssunused, va_list args ssunused)
{
	return 0;
}

static int
ss_nonefilter_free(struct ssfilter *f ssunused)
{
	return 0;
}

static int
ss_nonefilter_start(struct ssfilter *f ssunused, struct ssbuf *dest ssunused)
{
	return 0;
}

static int
ss_nonefilter_next(struct ssfilter *f ssunused,
                   struct ssbuf *dest ssunused,
                   char *buf ssunused, int size ssunused)
{
	return 0;
}

static int
ss_nonefilter_complete(struct ssfilter *f ssunused, struct ssbuf *dest ssunused)
{
	return 0;
}

static struct ssfilterif ss_nonefilter =
{
	.name     = "none",
	.init     = ss_nonefilter_init,
	.free     = ss_nonefilter_free,
	.start    = ss_nonefilter_start,
	.next     = ss_nonefilter_next,
	.complete = ss_nonefilter_complete
};

static void
ss_quotainit(struct ssquota *q, int64_t limit)
{
	q->enable = false;
	q->wait   = 0;
	q->limit  = limit;
	q->used   = 0;
	tt_pthread_mutex_init(&q->lock, NULL);
	tt_pthread_cond_init(&q->cond, NULL);
}

static void
ss_quotaenable(struct ssquota *q)
{
	q->enable = true;
}

static int
ss_quotafree(struct ssquota *q)
{
	tt_pthread_mutex_destroy(&q->lock);
	tt_pthread_cond_destroy(&q->cond);
	return 0;
}

static int
ss_quota(struct ssquota *q, enum ssquotaop op, int64_t v)
{
	if (likely(v == 0))
		return 0;
	tt_pthread_mutex_lock(&q->lock);
	switch (op) {
	case SS_QADD:
		if (unlikely(!q->enable || q->limit == 0)) {
			/*
			 * Fall through to quota accounting, skip
			 * the wait.
			 */
		} else {
			while (q->used + v >= q->limit) {
				q->wait++;
				tt_pthread_cond_wait(&q->cond, &q->lock);
				q->wait--;
			}
		}
		q->used += v;
		break;
	case SS_QREMOVE:
		q->used -= v;
		if (q->wait) {
			tt_pthread_cond_signal(&q->cond);
		}
		break;
	}
	tt_pthread_mutex_unlock(&q->lock);
	return 0;
}

static int
path_exists(const char *path)
{
	struct stat st;
	int rc = lstat(path, &st);
	return rc == 0;
}
static int
ss_mmap(struct ssmmap *m, int fd, uint64_t size, int ro)
{
	int flags = PROT_READ;
	if (! ro)
		flags |= PROT_WRITE;
	m->p = mmap(NULL, size, flags, MAP_SHARED, fd, 0);
	if (m->p == MAP_FAILED) {
		m->p = NULL;
		return -1;
	}
	m->size = size;
	return 0;
}

static int
ss_munmap(struct ssmmap *m)
{
	if (unlikely(m->p == NULL))
		return 0;
	int rc = munmap(m->p, m->size);
	m->p = NULL;
	return rc;
}

struct sszstdfilter {
	void *ctx;
};

static int
ss_zstdfilter_init(struct ssfilter *f, va_list args ssunused)
{
	struct sszstdfilter *z = (struct sszstdfilter*)f->priv;
	switch (f->op) {
	case SS_FINPUT:
		z->ctx = ZSTD_createCCtx();
		if (unlikely(z->ctx == NULL))
			return -1;
		break;
	case SS_FOUTPUT:
		z->ctx = NULL;
		break;
	}
	return 0;
}

static int
ss_zstdfilter_free(struct ssfilter *f)
{
	struct sszstdfilter *z = (struct sszstdfilter*)f->priv;
	switch (f->op) {
	case SS_FINPUT:
		ZSTD_freeCCtx(z->ctx);
		break;
	case SS_FOUTPUT:
		break;
	}
	return 0;
}

static int
ss_zstdfilter_start(struct ssfilter *f, struct ssbuf *dest)
{
	(void)dest;
	struct sszstdfilter *z = (struct sszstdfilter*)f->priv;
	size_t sz;
	switch (f->op) {
	case SS_FINPUT:;
		int compressionLevel = 3; /* fast */
		sz = ZSTD_compressBegin(z->ctx, compressionLevel);
		if (unlikely(ZSTD_isError(sz)))
			return -1;
		break;
	case SS_FOUTPUT:
		/* do nothing */
		break;
	}
	return 0;
}

static int
ss_zstdfilter_next(struct ssfilter *f, struct ssbuf *dest, char *buf, int size)
{
	struct sszstdfilter *z = (struct sszstdfilter*)f->priv;
	int rc;
	if (unlikely(size == 0))
		return 0;
	switch (f->op) {
	case SS_FINPUT:;
		size_t block = ZSTD_compressBound(size);
		rc = ss_bufensure(dest, block);
		if (unlikely(rc == -1))
			return -1;
		size_t sz = ZSTD_compressContinue(z->ctx, dest->p, block, buf, size);
		if (unlikely(ZSTD_isError(sz)))
			return -1;
		ss_bufadvance(dest, sz);
		break;
	case SS_FOUTPUT:
		/* do a single-pass decompression.
		 *
		 * Assume that destination buffer is allocated to
		 * original size.
		 */
		sz = ZSTD_decompress(dest->p, ss_bufunused(dest), buf, size);
		if (unlikely(ZSTD_isError(sz)))
			return -1;
		break;
	}
	return 0;
}

static int
ss_zstdfilter_complete(struct ssfilter *f, struct ssbuf *dest)
{
	struct sszstdfilter *z = (struct sszstdfilter*)f->priv;
	int rc;
	switch (f->op) {
	case SS_FINPUT:;
		size_t block = ZSTD_compressBound(0);
		rc = ss_bufensure(dest, block);
		if (unlikely(rc == -1))
			return -1;
		size_t sz = ZSTD_compressEnd(z->ctx, dest->p, block);
		if (unlikely(ZSTD_isError(sz)))
			return -1;
		ss_bufadvance(dest, sz);
		break;
	case SS_FOUTPUT:
		/* do nothing */
		break;
	}
	return 0;
}

static struct ssfilterif ss_zstdfilter =
{
	.name     = "zstd",
	.init     = ss_zstdfilter_init,
	.free     = ss_zstdfilter_free,
	.start    = ss_zstdfilter_start,
	.next     = ss_zstdfilter_next,
	.complete = ss_zstdfilter_complete
};

static int  sf_compare(struct key_def*, char *, char *b);

struct vinyl_field {
	const char *data;
	uint32_t size;
};

struct PACKED vinyl_field_meta {
	uint32_t offset;
	uint32_t size;
};

static inline struct vinyl_field_meta *
sf_fieldmeta(struct key_def *key_def, uint32_t part_id, char *data)
{
	assert(part_id <= key_def->part_count);
	(void)key_def;
	uint32_t offset_slot = part_id;
	return &((struct vinyl_field_meta*)(data))[offset_slot];
}

static inline char*
sf_field(struct key_def *key_def, uint32_t part_id, char *data, uint32_t *size)
{
	struct vinyl_field_meta *v = sf_fieldmeta(key_def, part_id, data);
	if (likely(size))
		*size = v->size;
	return data + v->offset;
}

static inline int
sf_writesize(struct key_def *key_def, struct vinyl_field *v)
{
	int sum = 0;
	/* for each key_part + value */
	for (uint32_t part_id = 0; part_id <= key_def->part_count; part_id++) {
		sum += sizeof(struct vinyl_field_meta)+ v[part_id].size;
	}
	return sum;
}

static inline void
sf_write(struct key_def *key_def, struct vinyl_field *fields, char *dest)
{
	int var_value_offset = sizeof(struct vinyl_field_meta) * (key_def->part_count + 1);
	/* for each key_part + value */
	for (uint32_t part_id = 0; part_id <= key_def->part_count; part_id++) {
		struct vinyl_field *field = &fields[part_id];
		struct vinyl_field_meta *current = sf_fieldmeta(key_def, part_id, dest);
		current->offset = var_value_offset;
		current->size   = field->size;
		memcpy(dest + var_value_offset, field->data, field->size);
		var_value_offset += current->size;
	}
}

static inline int
sf_comparable_size(struct key_def *key_def, char *data)
{
	int sum = 0;
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		uint32_t field_size;
		(void) sf_field(key_def, part_id, data, &field_size);
		sum += field_size;
		sum += sizeof(struct vinyl_field_meta);
	}
	/* fields: [key_part, key_part, ..., value] */
	sum += sizeof(struct vinyl_field_meta);
	return sum;
}

static inline void
sf_comparable_write(struct key_def *key_def, char *src, char *dest)
{
	int var_value_offset = sizeof(struct vinyl_field_meta) *
		(key_def->part_count + 1);
	/* for each key_part + value */
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		struct vinyl_field_meta *current = sf_fieldmeta(key_def, part_id, dest);
		current->offset = var_value_offset;
		char *ptr = sf_field(key_def, part_id, src, &current->size);
		memcpy(dest + var_value_offset, ptr, current->size);
		var_value_offset += current->size;
	}
	/* fields: [key_part, key_part, ..., value] */
	struct vinyl_field_meta *current =
		sf_fieldmeta(key_def, key_def->part_count, dest);
	current->offset = var_value_offset;
	current->size = 0;
}

static int
sf_compare(struct key_def *key_def, char *a, char *b)
{
	int rc;
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		struct key_part *part = &key_def->parts[part_id];
		uint32_t a_fieldsize;
		char *a_field = sf_field(key_def, part_id, a, &a_fieldsize);
		uint32_t b_fieldsize;
		char *b_field = sf_field(key_def, part_id, b, &b_fieldsize);
		rc = tuple_compare_field(a_field, b_field, part->type);
		if (rc != 0)
			return rc;
		part++;
	}
	return 0;
}

#define SR_VERSION_MAGIC      8529643324614668147ULL

#define SR_VERSION_A         '2'
#define SR_VERSION_B         '1'
#define SR_VERSION_C         '1'

#define SR_VERSION_STORAGE_A '2'
#define SR_VERSION_STORAGE_B '1'
#define SR_VERSION_STORAGE_C '1'

struct PACKED srversion {
	uint64_t magic;
	uint8_t  a, b, c;
};

static inline void
sr_version(struct srversion *v)
{
	v->magic = SR_VERSION_MAGIC;
	v->a = SR_VERSION_A;
	v->b = SR_VERSION_B;
	v->c = SR_VERSION_C;
}

static inline void
sr_version_storage(struct srversion *v)
{
	v->magic = SR_VERSION_MAGIC;
	v->a = SR_VERSION_STORAGE_A;
	v->b = SR_VERSION_STORAGE_B;
	v->c = SR_VERSION_STORAGE_C;
}

static inline int
sr_versionstorage_check(struct srversion *v)
{
	if (v->magic != SR_VERSION_MAGIC)
		return 0;
	if (v->a != SR_VERSION_STORAGE_A)
		return 0;
	if (v->b != SR_VERSION_STORAGE_B)
		return 0;
	if (v->c != SR_VERSION_STORAGE_C)
		return 0;
	return 1;
}


#define sr_e(type, fmt, ...) \
	({int res = -1;\
	  char errmsg[256];\
	  snprintf(errmsg, sizeof(errmsg), fmt, __VA_ARGS__);\
	  diag_set(ClientError, type, errmsg);\
	  res;})

#define sr_error(fmt, ...) \
	sr_e(ER_VINYL, fmt, __VA_ARGS__)

#define sr_malfunction(fmt, ...) \
	sr_e(ER_VINYL, fmt, __VA_ARGS__)

#define sr_oom() \
	sr_e(ER_VINYL, "%s", "memory allocation failed")

enum vinyl_status {
	SR_OFFLINE,
	SR_INITIAL_RECOVERY,
	SR_FINAL_RECOVERY,
	SR_ONLINE,
	SR_SHUTDOWN_PENDING,
	SR_SHUTDOWN,
	SR_DROP_PENDING,
	SR_DROP,
	SR_MALFUNCTION
};

struct srstatus {
	enum vinyl_status status;
	pthread_mutex_t lock;
};

static inline void
sr_statusinit(struct srstatus *s)
{
	s->status = SR_OFFLINE;
	tt_pthread_mutex_init(&s->lock, NULL);
}

static inline void
sr_statusfree(struct srstatus *s)
{
	tt_pthread_mutex_destroy(&s->lock);
}

static inline enum vinyl_status
sr_statusset(struct srstatus *s, enum vinyl_status status)
{
	tt_pthread_mutex_lock(&s->lock);
	enum vinyl_status old = s->status;
	s->status = status;
	tt_pthread_mutex_unlock(&s->lock);
	return old;
}

static inline enum vinyl_status
sr_status(struct srstatus *s)
{
	tt_pthread_mutex_lock(&s->lock);
	enum vinyl_status status = s->status;
	tt_pthread_mutex_unlock(&s->lock);
	return status;
}

static inline bool
sr_statusactive_is(enum vinyl_status status)
{
	switch (status) {
	case SR_ONLINE:
	case SR_INITIAL_RECOVERY:
	case SR_FINAL_RECOVERY:
		return true;
	case SR_SHUTDOWN_PENDING:
	case SR_SHUTDOWN:
	case SR_DROP_PENDING:
	case SR_DROP:
	case SR_OFFLINE:
	case SR_MALFUNCTION:
		return false;
	}
	unreachable();
	return 0;
}

static inline bool
sr_statusactive(struct srstatus *s) {
	return sr_statusactive_is(sr_status(s));
}

static inline bool
sr_online(struct srstatus *s) {
	return sr_status(s) == SR_ONLINE;
}

struct srstat {
	pthread_mutex_t lock;
	/* memory */
	uint64_t v_count;
	uint64_t v_allocated;
	/* set */
	uint64_t set;
	struct ssavg    set_latency;
	/* delete */
	uint64_t del;
	struct ssavg    del_latency;
	/* upsert */
	uint64_t upsert;
	struct ssavg    upsert_latency;
	/* get */
	uint64_t get;
	struct ssavg    get_read_disk;
	struct ssavg    get_read_cache;
	struct ssavg    get_latency;
	/* transaction */
	uint64_t tx;
	uint64_t tx_rlb;
	uint64_t tx_conflict;
	uint64_t tx_lock;
	struct ssavg    tx_latency;
	struct ssavg    tx_stmts;
	/* cursor */
	uint64_t cursor;
	struct ssavg    cursor_latency;
	struct ssavg    cursor_read_disk;
	struct ssavg    cursor_read_cache;
	struct ssavg    cursor_ops;
};

static inline void
sr_statinit(struct srstat *s)
{
	memset(s, 0, sizeof(*s));
	tt_pthread_mutex_init(&s->lock, NULL);
}

static inline void
sr_statfree(struct srstat *s) {
	tt_pthread_mutex_destroy(&s->lock);
}

static inline void
sr_statprepare(struct srstat *s)
{
	ss_avgprepare(&s->set_latency);
	ss_avgprepare(&s->del_latency);
	ss_avgprepare(&s->upsert_latency);
	ss_avgprepare(&s->get_read_disk);
	ss_avgprepare(&s->get_read_cache);
	ss_avgprepare(&s->get_latency);
	ss_avgprepare(&s->tx_latency);
	ss_avgprepare(&s->tx_stmts);
	ss_avgprepare(&s->cursor_latency);
	ss_avgprepare(&s->cursor_read_disk);
	ss_avgprepare(&s->cursor_read_cache);
	ss_avgprepare(&s->cursor_ops);
}

struct vinyl_statget {
	int read_disk;
	int read_cache;
	uint64_t read_latency;
};

static inline void
sr_statget(struct srstat *s, const struct vinyl_statget *statget)
{
	tt_pthread_mutex_lock(&s->lock);
	s->get++;
	ss_avgupdate(&s->get_read_disk, statget->read_disk);
	ss_avgupdate(&s->get_read_cache, statget->read_cache);
	ss_avgupdate(&s->get_latency, statget->read_latency);
	tt_pthread_mutex_unlock(&s->lock);
}

static inline void
sr_stattx(struct srstat *s, uint64_t start, uint32_t count,
          int rlb, int conflict)
{
	uint64_t diff = clock_monotonic64() - start;
	tt_pthread_mutex_lock(&s->lock);
	s->tx++;
	s->tx_rlb += rlb;
	s->tx_conflict += conflict;
	ss_avgupdate(&s->tx_stmts, count);
	ss_avgupdate(&s->tx_latency, diff);
	tt_pthread_mutex_unlock(&s->lock);
}

static inline void
sr_stattx_lock(struct srstat *s)
{
	tt_pthread_mutex_lock(&s->lock);
	s->tx_lock++;
	tt_pthread_mutex_unlock(&s->lock);
}

static inline void
sr_statcursor(struct srstat *s, uint64_t start, int read_disk, int read_cache, int ops)
{
	uint64_t diff = clock_monotonic64() - start;
	tt_pthread_mutex_lock(&s->lock);
	s->cursor++;
	ss_avgupdate(&s->cursor_read_disk, read_disk);
	ss_avgupdate(&s->cursor_read_cache, read_cache);
	ss_avgupdate(&s->cursor_latency, diff);
	ss_avgupdate(&s->cursor_ops, ops);
	tt_pthread_mutex_unlock(&s->lock);
}

enum srseqop {
	SR_LSN,
	SR_NSNNEXT,
	SR_TSNNEXT
};

struct srseq {
	pthread_mutex_t lock;
	/** Log sequence number. */
	uint64_t lsn;
	/** Transaction sequence number. */
	uint64_t tsn;
	/** Node sequence number. */
	uint64_t nsn;
};

static inline void
sr_seqinit(struct srseq *n) {
	memset(n, 0, sizeof(*n));
	tt_pthread_mutex_init(&n->lock, NULL);
}

static inline void
sr_seqfree(struct srseq *n) {
	tt_pthread_mutex_destroy(&n->lock);
}

static inline void
sr_seqlock(struct srseq *n) {
	tt_pthread_mutex_lock(&n->lock);
}

static inline void
sr_sequnlock(struct srseq *n) {
	tt_pthread_mutex_unlock(&n->lock);
}

static inline uint64_t
sr_seqdo(struct srseq *n, enum srseqop op)
{
	uint64_t v = 0;
	switch (op) {
	case SR_LSN:       v = n->lsn;
		break;
	case SR_TSNNEXT:   v = ++n->tsn;
		break;
	case SR_NSNNEXT:   v = ++n->nsn;
		break;
	}
	return v;
}

static inline uint64_t
sr_seq(struct srseq *n, enum srseqop op)
{
	sr_seqlock(n);
	uint64_t v = sr_seqdo(n, op);
	sr_sequnlock(n);
	return v;
}

struct srzone {
	uint32_t enable;
	char     name[4];
	uint32_t mode;
	uint32_t compact_wm;
	uint32_t compact_mode;
	uint32_t branch_prio;
	uint32_t branch_wm;
	uint32_t branch_age;
	uint32_t branch_age_period;
	uint64_t branch_age_period_us;
	uint32_t branch_age_wm;
	uint32_t gc_prio;
	uint32_t gc_period;
	uint64_t gc_period_us;
	uint32_t gc_wm;
	uint32_t lru_prio;
	uint32_t lru_period;
	uint64_t lru_period_us;
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
	if (!z->enable) {
		while (p >= 0) {
			z = &m->zones[p];
			if (z->enable)
				return z;
			p--;
		}
		return NULL;
	}
	return z;
}

struct runtime {
	struct srseq *seq;
	struct ssa *a;
	struct ssquota *quota;
	struct srzonemap *zonemap;
	struct srstat *stat;
};

static inline void
sr_init(struct runtime *r,
        struct ssquota *quota,
        struct srzonemap *zonemap,
        struct srseq *seq,
        struct srstat *stat)
{
	r->quota       = quota;
	r->zonemap     = zonemap;
	r->seq         = seq;
	r->stat        = stat;
}

static inline struct srzone *sr_zoneof(struct runtime *r)
{
	int p = ss_quotaused_percent(r->quota);
	return sr_zonemap(r->zonemap, p);
}

struct srconfstmt;
struct srconf;

typedef int (*srconff)(struct srconf*, struct srconfstmt*);

struct srconf {
	char *key;
	enum sstype type;
	void *value;
	struct srconf  *next;
};

struct PACKED srconfdump {
	uint8_t  type;
	uint16_t keysize;
	uint32_t valuesize;
};

struct srconfstmt {
	const char *path;
	struct ssbuf      *serialize;
	struct runtime         *r;
};

static inline struct srconf *
sr_C(struct srconf **link, struct srconf **cp, char *key,
     int type, void *value)
{
	struct srconf *c = *cp;
	*cp += 1;
	c->key      = key;
	c->type     = type;
	c->value    = value;
	c->next     = NULL;
	if (link) {
		if (*link)
			(*link)->next = c;
		*link = c;
	}
	return c;
}

static inline char*
sr_confkey(struct srconfdump *v) {
	return (char*)v + sizeof(struct srconfdump);
}

static inline char*
sr_confvalue(struct srconfdump *v) {
	return sr_confkey(v) + v->keysize;
}

static int
sr_valueserialize(struct srconf *m, struct srconfstmt *s)
{
	char buf[128];
	void *value = NULL;
	struct srconfdump v = {
		.type = m->type
	};
	switch (m->type) {
	case SS_U32:
		v.valuesize  = snprintf(buf, sizeof(buf), "%" PRIu32, load_u32(m->value));
		v.valuesize += 1;
		value = buf;
		break;
	case SS_U64:
		v.valuesize  = snprintf(buf, sizeof(buf), "%" PRIu64, load_u64(m->value));
		v.valuesize += 1;
		value = buf;
		break;
	case SS_STRING: {
		char *string = m->value;
		if (string) {
			v.valuesize = strlen(string) + 1;
			value = string;
		} else {
			v.valuesize = 0;
		}
		break;
	}
	case SS_STRINGPTR: {
		char **string = (char**)m->value;
		if (*string) {
			v.valuesize = strlen(*string) + 1;
			value = *string;
		} else {
			v.valuesize = 0;
		}
		v.type = SS_STRING;
		break;
	}
	default:
		return -1;
	}
	char name[128];
	v.keysize  = snprintf(name, sizeof(name), "%s", s->path);
	v.keysize += 1;
	struct ssbuf *p = s->serialize;
	int size = sizeof(v) + v.keysize + v.valuesize;
	int rc = ss_bufensure(p, size);
	if (unlikely(rc == -1))
		return sr_oom();
	memcpy(p->p, &v, sizeof(v));
	memcpy(p->p + sizeof(v), name, v.keysize);
	memcpy(p->p + sizeof(v) + v.keysize, value, v.valuesize);
	ss_bufadvance(p, size);
	return 0;
}

static inline int
sr_confserialize(struct srconf *c, struct srconfstmt *stmt)
{
	int rc = 0;
	while (c) {
		char path[256];
		const char *old_path = stmt->path;

		if (old_path)
			snprintf(path, sizeof(path), "%s.%s", old_path, c->key);
		else
			snprintf(path, sizeof(path), "%s", c->key);

		stmt->path = path;

		if (c->type == SS_UNDEF)
			rc = sr_confserialize(c->value, stmt);
		else
			rc = sr_valueserialize(c, stmt);

		stmt->path = old_path;
		if (rc == -1)
			break;
		c = c->next;
	}
	return rc;
}


#define SVNONE       0
#define SVDELETE     1
#define SVUPSERT     2
#define SVGET        4
#define SVDUP        8
#define SVCONFLICT  32

struct sv;

struct svif {
	uint8_t   (*flags)(struct sv*);
	void      (*lsnset)(struct sv*, int64_t);
	uint64_t  (*lsn)(struct sv*);
	char     *(*pointer)(struct sv*);
	uint32_t  (*size)(struct sv*);
};

static struct svif sv_tupleif;
static struct svif sv_upsertvif;
static struct svif sv_refif;
static struct svif sv_upsertvif;
static struct svif sx_vif;
static struct svif sd_vif;
struct vinyl_tuple;
struct sdv;
struct sxv;
struct svupsert;
struct sdpageheader;

struct PACKED sv {
	struct svif *i;
	void *v, *arg;
};

static inline struct vinyl_tuple *
sv_to_tuple(struct sv *v)
{
	assert(v->i == &sv_tupleif);
	struct vinyl_tuple *tuple = (struct vinyl_tuple *)v->v;
	assert(tuple != NULL);
	return tuple;
}

static inline void
sv_from_tuple(struct sv *v, struct vinyl_tuple *tuple)
{
	v->i   = &sv_tupleif;
	v->v   = tuple;
	v->arg = NULL;
}

static inline struct sxv *
sv_to_sxv(struct sv *v)
{
	assert(v->i == &sx_vif);
	struct sxv *sxv = (struct sxv *)v->v;
	assert(sxv != NULL);
	return sxv;
}

static inline void
sv_from_sxv(struct sv *v, struct sxv *sxv)
{
	v->i   = &sx_vif;
	v->v   = sxv;
	v->arg = NULL;
}

static inline void
sv_from_svupsert(struct sv *v, char *s)
{
	v->i   = &sv_upsertvif;
	v->v   = s;
	v->arg = NULL;
}

static inline void
sv_from_sdv(struct sv *v, struct sdv *sdv, struct sdpageheader *h)
{
	v->i   = &sd_vif;
	v->v   = sdv;
	v->arg = h;
}

static inline uint8_t
sv_flags(struct sv *v) {
	return v->i->flags(v);
}

static inline int
sv_isflags(int flags, int value) {
	return (flags & value) > 0;
}

static inline int
sv_is(struct sv *v, int flags) {
	return sv_isflags(sv_flags(v), flags) > 0;
}

static inline uint64_t
sv_lsn(struct sv *v) {
	return v->i->lsn(v);
}

static inline void
sv_lsnset(struct sv *v, int64_t lsn) {
	v->i->lsnset(v, lsn);
}

static inline char*
sv_pointer(struct sv *v) {
	return v->i->pointer(v);
}

static inline uint32_t
sv_size(struct sv *v) {
	return v->i->size(v);
}

struct vinyl_tuple {
	uint64_t lsn;
	uint32_t size;
	uint16_t refs; /* atomic */
	uint8_t  flags;
	char data[0];
};

static inline uint32_t
vinyl_tuple_size(struct vinyl_tuple *v) {
	return sizeof(struct vinyl_tuple) + v->size;
}

static inline struct vinyl_tuple*
vinyl_tuple_from_sv(struct runtime *r, struct sv *src);

static int
vinyl_tuple_unref_rt(struct runtime *r, struct vinyl_tuple *v);

struct svupsertnode {
	uint64_t lsn;
	uint8_t  flags;
	struct ssbuf    buf;
};

struct svupsert {
	struct ssbuf stack;
	struct ssbuf tmp;
	int max;
	int count;
	struct sv result;
};

static inline void
sv_upsertinit(struct svupsert *u)
{
	u->max = 0;
	u->count = 0;
	memset(&u->result, 0, sizeof(u->result));
	ss_bufinit(&u->stack);
	ss_bufinit(&u->tmp);
}

static inline void
sv_upsertfree(struct svupsert *u)
{
	struct svupsertnode *n = (struct svupsertnode*)u->stack.s;
	int i = 0;
	while (i < u->max) {
		ss_buffree(&n[i].buf);
		i++;
	}
	ss_buffree(&u->stack);
	ss_buffree(&u->tmp);
}

static inline void
sv_upsertreset(struct svupsert *u)
{
	struct svupsertnode *n = (struct svupsertnode*)u->stack.s;
	int i = 0;
	while (i < u->count) {
		ss_bufreset(&n[i].buf);
		i++;
	}
	u->count = 0;
	ss_bufreset(&u->stack);
	ss_bufreset(&u->tmp);
	memset(&u->result, 0, sizeof(u->result));
}

static inline void
sv_upsertgc(struct svupsert *u, int wm_stack, int wm_buf)
{
	struct svupsertnode *n = (struct svupsertnode*)u->stack.s;
	if (u->max >= wm_stack) {
		sv_upsertfree(u);
		sv_upsertinit(u);
		return;
	}
	ss_bufgc(&u->tmp, wm_buf);
	int i = 0;
	while (i < u->count) {
		ss_bufgc(&n[i].buf, wm_buf);
		i++;
	}
	u->count = 0;
	memset(&u->result, 0, sizeof(u->result));
}

static inline int
sv_upsertpush_raw(struct svupsert *u,
		  char *pointer, int size,
                  uint8_t flags, uint64_t lsn)
{
	struct svupsertnode *n;
	int rc;
	if (likely(u->max > u->count)) {
		n = (struct svupsertnode*)u->stack.p;
		ss_bufreset(&n->buf);
	} else {
		rc = ss_bufensure(&u->stack, sizeof(struct svupsertnode));
		if (unlikely(rc == -1))
			return -1;
		n = (struct svupsertnode*)u->stack.p;
		ss_bufinit(&n->buf);
		u->max++;
	}
	rc = ss_bufensure(&n->buf, size);
	if (unlikely(rc == -1))
		return -1;
	memcpy(n->buf.p, pointer, size);
	n->flags = flags;
	n->lsn = lsn;
	ss_bufadvance(&n->buf, size);
	ss_bufadvance(&u->stack, sizeof(struct svupsertnode));
	u->count++;
	return 0;
}

static inline int
sv_upsertpush(struct svupsert *u, struct sv *v)
{
	return sv_upsertpush_raw(u, sv_pointer(v),
	                         sv_size(v),
	                         sv_flags(v), sv_lsn(v));
}

static inline struct svupsertnode*
sv_upsertpop(struct svupsert *u)
{
	if (u->count == 0)
		return NULL;
	int pos = u->count - 1;
	u->count--;
	u->stack.p -= sizeof(struct svupsertnode);
	return ss_bufat(&u->stack, sizeof(struct svupsertnode), pos);
}

static inline int
vinyl_upsert_prepare(char **src, uint32_t *src_size,
                      char **mp, uint32_t *mp_size, uint32_t *mp_size_key,
                      struct key_def *key_def)
{
	/* calculate msgpack size */
	*mp_size_key = 0;
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		if (key_def->parts[i].type == STRING)
			*mp_size_key += mp_sizeof_str(src_size[i]);
		else
			*mp_size_key += mp_sizeof_uint(load_u64(src[i]));
	}

	/* count msgpack fields */
	uint32_t count = key_def->part_count;
	uint32_t value_field = key_def->part_count;
	uint32_t value_size = src_size[value_field];
	char *p = src[value_field];
	char *end = p + value_size;
	while (p < end) {
		count++;
		mp_next((const char **)&p);
	}

	/* allocate and encode tuple */
	*mp_size = mp_sizeof_array(count) + *mp_size_key + value_size;
	*mp = (char *)malloc(*mp_size);
	if (mp == NULL)
		return -1;
	p = *mp;
	p = mp_encode_array(p, count);
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		if (key_def->parts[i].type == STRING)
			p = mp_encode_str(p, src[i], src_size[i]);
		else
			p = mp_encode_uint(p, load_u64(src[i]));
	}
	memcpy(p, src[value_field], src_size[value_field]);
	return 0;
}

static void *
vinyl_update_alloc(void *arg, size_t size)
{
	(void) arg;
	/* TODO: rewrite tuple_upsert_execute() without exceptions */
	void *data = box_txn_alloc(size);
	if (data != NULL)
		diag_raise();
	return data;
}

static inline int
vinyl_upsert_do(char **result, uint32_t *result_size,
              char *tuple, uint32_t tuple_size, uint32_t tuple_size_key,
              char *upsert, int upsert_size)
{
	char *p = upsert;
	uint8_t index_base = *(uint8_t *)p;
	p += sizeof(uint8_t);
	uint32_t default_tuple_size = *(uint32_t *)p;
	p += sizeof(uint32_t);
	p += default_tuple_size;
	char *expr = p;
	char *expr_end = upsert + upsert_size;
	const char *up;
	uint32_t up_size;

	/* emit upsert */
	up = tuple_upsert_execute(vinyl_update_alloc, NULL,
				  expr, expr_end,
		                  tuple, tuple + tuple_size,
		                  &up_size, index_base);
	if (up == NULL)
		goto error;

	/* skip array size and key */
	const char *ptr = up;
	mp_decode_array(&ptr);
	ptr += tuple_size_key;

	/* get new value */
	*result_size = (uint32_t)((up + up_size) -  ptr);
	*result = (char *)malloc(*result_size);
	if (*result == NULL)
		goto error;
	memcpy(*result, ptr, *result_size);
	fiber_gc();
	return 0;
error:
	fiber_gc();
	return -1;
}

static int
vinyl_upsert_cb(int count,
	       char **src,   uint32_t *src_size,
	       char **upsert, uint32_t *upsert_size,
	       char **result, uint32_t *result_size,
	       struct key_def *key_def)
{
	uint32_t value_field;
	value_field = key_def->part_count;

	/* use default tuple value */
	if (src == NULL)
	{
		/* result key fields are initialized to upsert
		 * fields by default */
		char *p = upsert[value_field];
		p += sizeof(uint8_t); /* index base */
		uint32_t value_size = *(uint32_t *)p;
		p += sizeof(uint32_t);
		void *value = (char *)malloc(value_size);
		if (value == NULL)
			return -1;
		memcpy(value, p, value_size);
		result[value_field] = (char*)value;
		result_size[value_field] = value_size;
		return 0;
	}

	/* convert src to msgpack */
	char *tuple;
	uint32_t tuple_size_key;
	uint32_t tuple_size;
	int rc;
	rc = vinyl_upsert_prepare(src, src_size,
	                           &tuple, &tuple_size, &tuple_size_key,
	                           key_def);
	if (rc == -1)
		return -1;

	/* execute upsert */
	rc = vinyl_upsert_do(&result[value_field],
	                   &result_size[value_field],
	                   tuple, tuple_size, tuple_size_key,
	                   upsert[value_field],
	                   upsert_size[value_field]);
	free(tuple);

	(void)count;
	(void)upsert_size;
	return rc;
}


static inline int
sv_upsertdo(struct svupsert *u, struct key_def *key_def,
	    struct svupsertnode *n1, struct svupsertnode *n2)
{
	assert(key_def->part_count <= BOX_INDEX_PART_MAX);
	assert(n2->flags & SVUPSERT);

	uint32_t  src_size[BOX_INDEX_PART_MAX + 1];
	char     *src[BOX_INDEX_PART_MAX + 1];
	void     *src_ptr;
	uint32_t *src_size_ptr;

	uint32_t  upsert_size[BOX_INDEX_PART_MAX + 1];
	char     *upsert[BOX_INDEX_PART_MAX + 1];

	char     *result[BOX_INDEX_PART_MAX + 1];
	uint32_t  result_size[BOX_INDEX_PART_MAX + 1];

	if (n1 && !(n1->flags & SVDELETE))
	{
		src_ptr = src;
		src_size_ptr = src_size;
		/* for each key part + value */
		for (uint32_t i = 0; i <= key_def->part_count; i++) {
			src[i]    = sf_field(key_def, i, n1->buf.s, &src_size[i]);
			upsert[i] = sf_field(key_def, i, n2->buf.s, &upsert_size[i]);
			result[i] = src[i];
			result_size[i] = src_size[i];
		}
	} else {
		src_ptr = NULL;
		src_size_ptr = NULL;
		/* for each key part + value */
		for (uint32_t i = 0; i <= key_def->part_count; i++) {
			upsert[i] = sf_field(key_def, i, n2->buf.s, &upsert_size[i]);
			result[i] = upsert[i];
			result_size[i] = upsert_size[i];
		}
	}

	/* execute */
	int rc = vinyl_upsert_cb(key_def->part_count + 1,
				src_ptr, src_size_ptr,
				upsert, upsert_size,
				result, result_size,
				key_def);
	if (unlikely(rc == -1))
		return -1;

	/* validate and create new record */
	struct vinyl_field v[BOX_INDEX_PART_MAX + 1];
	/* for each key part + value */
	for (uint32_t i = 0; i <= key_def->part_count; i++) {
		v[i].data = result[i];
		v[i].size = result_size[i];
	}
	int size = sf_writesize(key_def, v);
	ss_bufreset(&u->tmp);
	rc = ss_bufensure(&u->tmp, size);
	if (unlikely(rc == -1))
		goto cleanup;
	sf_write(key_def, v, u->tmp.s);
	ss_bufadvance(&u->tmp, size);

	/* save result */
	rc = sv_upsertpush_raw(u, u->tmp.s, ss_bufused(&u->tmp),
	                       n2->flags & ~SVUPSERT,
	                       n2->lsn);
cleanup:
	/* free key parts + value */
	for (uint32_t i = 0 ; i <= key_def->part_count; i++) {
		if (src_ptr == NULL) {
			if (v[i].data != upsert[i])
				free((char *)v[i].data);
		} else {
			if (v[i].data != src[i])
				free((char *)v[i].data);
		}
	}
	return rc;
}

static inline int
sv_upsert(struct svupsert *u, struct key_def *key_def)
{
	assert(u->count >= 1 );
	struct svupsertnode *f = ss_bufat(&u->stack,
					  sizeof(struct svupsertnode),
					  u->count - 1);
	int rc;
	if (f->flags & SVUPSERT) {
		f = sv_upsertpop(u);
		rc = sv_upsertdo(u, key_def, NULL, f);
		if (unlikely(rc == -1))
			return -1;
	}
	if (u->count == 1)
		goto done;
	while (u->count > 1) {
		struct svupsertnode *f = sv_upsertpop(u);
		struct svupsertnode *s = sv_upsertpop(u);
		assert(f != NULL);
		assert(s != NULL);
		rc = sv_upsertdo(u, key_def, f, s);
		if (unlikely(rc == -1))
			return -1;
	}
done:
	sv_from_svupsert(&u->result, u->stack.s);
	return 0;
}

struct vinyl_index;

struct PACKED svlogindex {
	uint32_t id;
	uint32_t head, tail;
	uint32_t count;
	struct vinyl_index *index;
};

struct PACKED svlogv {
	struct sv v;
	uint32_t id;
	uint32_t next;
};

/**
 * In-memory transaction log.
 * Transaction changes are made in multi-version
 * in-memory index (sxindex) and recorded in this log.
 * When the transaction is committed, the changes are written to the
 * in-memory single-version index (struct svindex) in a
 * specific sinode object of an index.
 */
struct svlog {
	/**
	 * Number of writes (inserts,updates, deletes) done by
	 * the transaction.
	 */
	int count_write;
	struct ssbuf index;
	struct ssbuf buf;
};

static inline void
sv_loginit(struct svlog *l)
{
	ss_bufinit(&l->buf);
	ss_bufinit(&l->index);
	l->count_write = 0;
}

static inline void
sv_logfree(struct svlog *l)
{
	ss_buffree(&l->buf);
	ss_buffree(&l->index);
	l->count_write = 0;
}

static inline void
sv_logreset(struct svlog *l)
{
	ss_bufreset(&l->buf);
	ss_bufreset(&l->index);
	l->count_write = 0;
}

static inline int
sv_logcount(struct svlog *l) {
	return ss_bufused(&l->buf) / sizeof(struct svlogv);
}

static inline int
sv_logcount_write(struct svlog *l) {
	return l->count_write;
}

static inline struct svlogv*
sv_logat(struct svlog *l, int pos) {
	return ss_bufat(&l->buf, sizeof(struct svlogv), pos);
}

static inline int
sv_logadd(struct svlog *l, struct svlogv *v,
	  struct vinyl_index *index)
{
	uint32_t n = sv_logcount(l);
	int rc = ss_bufadd(&l->buf, v, sizeof(struct svlogv));
	if (unlikely(rc == -1))
		return -1;
	struct svlogindex *i = (struct svlogindex*)l->index.s;
	while ((char*)i < l->index.p) {
		if (likely(i->id == v->id)) {
			struct svlogv *tail = sv_logat(l, i->tail);
			tail->next = n;
			i->tail = n;
			i->count++;
			goto done;
		}
		i++;
	}
	rc = ss_bufensure(&l->index, sizeof(struct svlogindex));
	if (unlikely(rc == -1)) {
		l->buf.p -= sizeof(struct svlogv);
		return -1;
	}
	i = (struct svlogindex*)l->index.p;
	i->id    = v->id;
	i->head  = n;
	i->tail  = n;
	i->index = index;
	i->count = 1;
	ss_bufadvance(&l->index, sizeof(struct svlogindex));
done:
	if (! (sv_flags(&v->v) & SVGET))
		l->count_write++;
	return 0;
}

static inline void
sv_logreplace(struct svlog *l, int n, struct svlogv *v)
{
	struct svlogv *ov = sv_logat(l, n);
	if (! (sv_flags(&ov->v) & SVGET))
		l->count_write--;
	if (! (sv_flags(&v->v) & SVGET))
		l->count_write++;
	ss_bufset(&l->buf, sizeof(struct svlogv), n, (char*)v, sizeof(struct svlogv));
}

struct PACKED svmergesrc {
	struct ssiter *i;
	struct ssiter src;
	uint8_t dup;
	void *ptr;
};

struct svmerge {
	struct ssa *a;
	struct key_def *key_def;
	struct ssbuf buf;
};

static inline void
sv_mergeinit(struct svmerge *m, struct key_def *key_def)
{
	ss_bufinit(&m->buf);
	m->key_def = key_def;
}

static inline int
sv_mergeprepare(struct svmerge *m, int count)
{
	int rc = ss_bufensure(&m->buf, sizeof(struct svmergesrc) * count);
	if (unlikely(rc == -1))
		return sr_oom();
	return 0;
}

static inline void
sv_mergefree(struct svmerge *m)
{
	ss_buffree(&m->buf);
}

static inline void
sv_mergereset(struct svmerge *m)
{
	m->buf.p = m->buf.s;
}

static inline struct svmergesrc*
sv_mergeadd(struct svmerge *m, struct ssiter *i)
{
	assert(m->buf.p < m->buf.e);
	struct svmergesrc *s = (struct svmergesrc*)m->buf.p;
	s->dup = 0;
	s->i = i;
	s->ptr = NULL;
	if (i == NULL)
		s->i = &s->src;
	ss_bufadvance(&m->buf, sizeof(struct svmergesrc));
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

struct PACKED svmergeiter {
	enum vinyl_order order;
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
		ss_iteratornext(im->v->i);
	}
	im->v = NULL;
	struct svmergesrc *found_src = NULL;
	struct sv *found_val = NULL;
	for (struct svmergesrc *src = im->src; src < im->end; src++)
	{
		struct sv *v = ss_iteratorof(src->i);
		if (v == NULL)
			continue;
		if (found_src == NULL) {
			found_val = v;
			found_src = src;
			continue;
		}
		int rc;
		rc = sf_compare(im->merge->key_def,
				sv_pointer(found_val), sv_pointer(v));
		if (rc == 0) {
			/*
			assert(sv_lsn(v) < sv_lsn(maxv));
			*/
			src->dup = 1;
			break;
		} else if (direction * rc > 0) {
			sv_mergeiter_dupreset(im, src);
			found_val = v;
			found_src = src;
			break;
		}
	}
	if (unlikely(found_src == NULL))
		return;
	im->v = found_src;
}

static inline int
sv_mergeiter_open(struct svmergeiter *im, struct svmerge *m, enum vinyl_order o)
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

static inline void *
sv_mergeiter_get(struct svmergeiter *im)
{
	if (unlikely(im->v == NULL))
		return NULL;
	return ss_iteratorof(im->v->i);
}

static inline uint32_t
sv_mergeisdup(struct svmergeiter *im)
{
	assert(im->v != NULL);
	if (im->v->dup)
		return SVDUP;
	return 0;
}

struct PACKED svreaditer {
	struct svmergeiter *merge;
	uint64_t vlsn;
	int next;
	int nextdup;
	int save_delete;
	struct svupsert *u;
	struct ssa *a;
	struct sv *v;
};

static inline int
sv_readiter_upsert(struct svreaditer *i)
{
	sv_upsertreset(i->u);
	/* upsert begin */
	struct sv *v = sv_mergeiter_get(i->merge);
	assert(v != NULL);
	assert(sv_flags(v) & SVUPSERT);
	int rc = sv_upsertpush(i->u, v);
	if (unlikely(rc == -1))
		return -1;
	sv_mergeiter_next(i->merge);
	/* iterate over upsert statements */
	int skip = 0;
	for (; sv_mergeiter_has(i->merge); sv_mergeiter_next(i->merge))
	{
		v = sv_mergeiter_get(i->merge);
		int dup = sv_is(v, SVDUP) || sv_mergeisdup(i->merge);
		if (! dup)
			break;
		if (skip)
			continue;
		int rc = sv_upsertpush(i->u, v);
		if (unlikely(rc == -1))
			return -1;
		if (! (sv_flags(v) & SVUPSERT))
			skip = 1;
	}
	/* upsert */
	rc = sv_upsert(i->u, i->merge->merge->key_def);
	if (unlikely(rc == -1))
		return -1;
	return 0;
}

static inline void
sv_readiter_next(struct svreaditer *im)
{
	if (im->next)
		sv_mergeiter_next(im->merge);
	im->next = 0;
	im->v = NULL;
	for (; sv_mergeiter_has(im->merge); sv_mergeiter_next(im->merge))
	{
		struct sv *v = sv_mergeiter_get(im->merge);
		int dup = sv_is(v, SVDUP) || sv_mergeisdup(im->merge);
		if (im->nextdup) {
			if (dup)
				continue;
			else
				im->nextdup = 0;
		}
		/* skip version out of visible range */
		if (sv_lsn(v) > im->vlsn) {
			continue;
		}
		im->nextdup = 1;
		if (unlikely(!im->save_delete && sv_is(v, SVDELETE)))
			continue;
		if (unlikely(sv_is(v, SVUPSERT))) {
			int rc = sv_readiter_upsert(im);
			if (unlikely(rc == -1))
				return;
			im->v = &im->u->result;
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
		struct sv *v = sv_mergeiter_get(im->merge);
		int dup = sv_is(v, SVDUP) || sv_mergeisdup(im->merge);
		if (dup)
			continue;
		im->next = 0;
		im->v = v;
		break;
	}
}

static inline int
sv_readiter_open(struct svreaditer *im, struct svmergeiter *merge,
		 struct svupsert *u, uint64_t vlsn, int save_delete)
{
	im->a     = merge->merge->a;
	im->u     = u;
	im->merge = merge;
	im->vlsn  = vlsn;
	im->v = NULL;
	im->next = 0;
	im->nextdup = 0;
	im->save_delete = save_delete;
	/* iteration can start from duplicate */
	sv_readiter_next(im);
	return 0;
}

static inline void*
sv_readiter_get(struct svreaditer *im)
{
	if (unlikely(im->v == NULL))
		return NULL;
	return im->v;
}

struct PACKED svwriteiter {
	uint64_t  vlsn;
	uint64_t  vlsn_lru;
	uint64_t  limit;
	uint64_t  size;
	uint32_t  sizev;
	uint32_t  now;
	int       save_delete;
	int       save_upsert;
	int       next;
	int       upsert;
	uint64_t  prevlsn;
	int       vdup;
	struct sv       *v;
	struct svupsert *u;
	struct svmergeiter   *merge;
	struct ssa *a;
};

static inline int
sv_writeiter_upsert(struct svwriteiter *i)
{
	/* apply upsert only on statements which are the latest or
	 * ready to be garbage-collected */
	sv_upsertreset(i->u);

	/* upsert begin */
	struct sv *v = sv_mergeiter_get(i->merge);
	assert(v != NULL);
	assert(sv_flags(v) & SVUPSERT);
	assert(sv_lsn(v) <= i->vlsn);
	int rc = sv_upsertpush(i->u, v);
	if (unlikely(rc == -1))
		return -1;
	sv_mergeiter_next(i->merge);

	/* iterate over upsert statements */
	int last_non_upd = 0;
	for (; sv_mergeiter_has(i->merge); sv_mergeiter_next(i->merge))
	{
		v = sv_mergeiter_get(i->merge);
		int flags = sv_flags(v);
		int dup = sv_isflags(flags, SVDUP) || sv_mergeisdup(i->merge);
		if (! dup)
			break;
		/* stop forming upserts on a second non-upsert stmt,
		 * but continue to iterate stream */
		if (last_non_upd)
			continue;
		last_non_upd = ! sv_isflags(flags, SVUPSERT);
		int rc = sv_upsertpush(i->u, v);
		if (unlikely(rc == -1))
			return -1;
	}

	/* upsert */
	rc = sv_upsert(i->u, i->merge->merge->key_def);
	if (unlikely(rc == -1))
		return -1;
	return 0;
}

static inline void
sv_writeiter_next(struct svwriteiter *im)
{
	if (im->next)
		sv_mergeiter_next(im->merge);
	im->next = 0;
	im->v = NULL;
	im->vdup = 0;

	for (; sv_mergeiter_has(im->merge); sv_mergeiter_next(im->merge))
	{
		struct sv *v = sv_mergeiter_get(im->merge);
		uint64_t lsn = sv_lsn(v);
		if (lsn < im->vlsn_lru)
			continue;
		int flags = sv_flags(v);
		int dup = sv_isflags(flags, SVDUP) || sv_mergeisdup(im->merge);
		if (im->size >= im->limit) {
			if (! dup)
				break;
		}

		if (unlikely(dup)) {
			/* keep atleast one visible version for <= vlsn */
			if (im->prevlsn <= im->vlsn) {
				if (im->upsert) {
					im->upsert = sv_isflags(flags, SVUPSERT);
				} else {
					continue;
				}
			}
		} else {
			im->upsert = 0;
			/* delete (stray or on branch) */
			if (! im->save_delete) {
				int del = sv_isflags(flags, SVDELETE);
				if (unlikely(del && (lsn <= im->vlsn))) {
					im->prevlsn = lsn;
					continue;
				}
			}
			im->size += im->sizev + sv_size(v);
			/* upsert (track first statement start) */
			if (sv_isflags(flags, SVUPSERT))
				im->upsert = 1;
		}

		/* upsert */
		if (sv_isflags(flags, SVUPSERT)) {
			if (! im->save_upsert) {
				if (lsn <= im->vlsn) {
					int rc;
					rc = sv_writeiter_upsert(im);
					if (unlikely(rc == -1))
						return;
					im->upsert = 0;
					im->prevlsn = lsn;
					im->v = &im->u->result;
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
		  struct svupsert *u, uint64_t limit,
                  uint32_t sizev, uint64_t vlsn, uint64_t vlsn_lru,
                  int save_delete, int save_upsert)
{
	im->u           = u;
	im->a           = merge->merge->a;
	im->merge       = merge;
	im->limit       = limit;
	im->size        = 0;
	im->sizev       = sizev;
	im->vlsn        = vlsn;
	im->vlsn_lru    = vlsn_lru;
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

static inline int
sv_writeiter_has(struct svwriteiter *im)
{
	return im->v != NULL;
}

static inline void *
sv_writeiter_get(struct svwriteiter *im)
{
	return im->v;
}

static inline int
sv_writeiter_resume(struct svwriteiter *im)
{
	im->v       = sv_mergeiter_get(im->merge);
	if (unlikely(im->v == NULL))
		return 0;
	im->vdup    = sv_is(im->v, SVDUP) || sv_mergeisdup(im->merge);
	im->prevlsn = sv_lsn(im->v);
	im->next    = 1;
	im->upsert  = 0;
	im->size    = im->sizev + sv_size(im->v);
	return 1;
}

static inline int
sv_writeiter_is_duplicate(struct svwriteiter *im)
{
	assert(im->v != NULL);
	return im->vdup;
}

struct svref {
	struct vinyl_tuple *v;
	uint8_t flags;
};

struct tree_svindex_key {
	char *data;
	int size;
	uint64_t lsn;
};

struct svindex;

int
tree_svindex_compare(struct svref a, struct svref b, struct svindex *index);

int
tree_svindex_compare_key(struct svref a, struct tree_svindex_key *key,
			 struct svindex *index);

#define BPS_TREE_VINDEX_PAGE_SIZE (16 * 1024)
#define BPS_TREE_NAME _svindex
#define BPS_TREE_BLOCK_SIZE 512
#define BPS_TREE_EXTENT_SIZE BPS_TREE_VINDEX_PAGE_SIZE
#define BPS_TREE_COMPARE(a, b, index) tree_svindex_compare(a, b, index)
#define BPS_TREE_COMPARE_KEY(a, b, index) tree_svindex_compare_key(a, b, index)
#define bps_tree_elem_t struct svref
#define bps_tree_key_t struct tree_svindex_key *
#define bps_tree_arg_t struct svindex *
#define BPS_TREE_NO_DEBUG

#include "salad/bps_tree.h"

/*
 * svindex is an in-memory container for vinyl_tuples in an
 * a single vinyl node.
 * Internally it uses bps_tree to stores struct svref objects,
 * which, in turn, hold pointers to vinyl_tuple objects.
 * svrefs are ordered by tuple key and, for the same key,
 * by lsn, in descending order.
 *
 * For example, assume there are two tuples with the same key,
 * but different LSN. These are duplicates of the same key,
 * maintained for the purpose of MVCC/consistent read view.
 * In Vinyl terms, they form a duplicate chain.
 *
 * Due to specifics of usage, svindex must distinguish between the
 * first duplicate in the chain and other keys in that chain.
 *
 * That's why svref objects additionally store 'flags' member
 * that could hold SVDUP bit. The first svref in a chain
 * has flags == 0, and others have flags == SVDUP
 *
 * During insertion, reference counter of vinyl_tuple is incremented,
 * during destruction all vinyl_tuple' reference counters are decremented.
 */
struct svindex {
	struct bps_tree_svindex tree;
	uint32_t used;
	uint64_t lsnmin;
	struct key_def *key_def;
	/*
	 * This is a search state flag, which is set
	 * to true to true when the tree comparator finds
	 * a duplicate key in a chain (same key, different LSN).
	 * Used in insert and range search operations with the
	 * index.
	 */
	bool hint_key_is_equal;
};

int
tree_svindex_compare(struct svref a, struct svref b, struct svindex *index)
{
	int res = sf_compare(index->key_def, a.v->data, b.v->data);
	if (res == 0) {
		index->hint_key_is_equal = true;
		res = a.v->lsn > b.v->lsn ? -1 : a.v->lsn < b.v->lsn;
	}
	return res;
}

int
tree_svindex_compare_key(struct svref a, struct tree_svindex_key *key,
			 struct svindex *index)
{
	int res = sf_compare(index->key_def, a.v->data, key->data);
	if (res == 0) {
		index->hint_key_is_equal = true;
		res = a.v->lsn > key->lsn ? -1 : a.v->lsn < key->lsn;
	}
	return res;
}

void *
sv_index_alloc_matras_page()
{
	return malloc(BPS_TREE_VINDEX_PAGE_SIZE);
}

void
sv_index_free_matras_page(void *p)
{
	return free(p);
}

static int
sv_indexinit(struct svindex *i, struct key_def *key_def)
{
	i->lsnmin = UINT64_MAX;
	i->used   = 0;
	i->key_def = key_def;
	bps_tree_svindex_create(&i->tree, i,
				sv_index_alloc_matras_page,
				sv_index_free_matras_page);
	return 0;
}

static int
sv_indexfree(struct svindex *i, struct runtime *r)
{
	assert(i == i->tree.arg);
	struct bps_tree_svindex_iterator itr =
		bps_tree_svindex_itr_first(&i->tree);
	while (!bps_tree_svindex_itr_is_invalid(&itr)) {
		struct vinyl_tuple *v = bps_tree_svindex_itr_get_elem(&i->tree, &itr)->v;
		vinyl_tuple_unref_rt(r, v);
		bps_tree_svindex_itr_next(&i->tree, &itr);
	}
	bps_tree_svindex_destroy(&i->tree);
	return 0;
}

static inline int
sv_indexset(struct svindex *i, struct svref ref)
{
	/* see struct svindex comments */
	assert(i == i->tree.arg);
	i->hint_key_is_equal = false;
	if (bps_tree_svindex_insert(&i->tree, ref, NULL) != 0)
		return -1;
	i->used += ref.v->size;
	if (i->lsnmin > ref.v->lsn)
		i->lsnmin = ref.v->lsn;
	if (!i->hint_key_is_equal) {
		/* there no duplicates, no need to change and flags */
		return 0;
	}
	/*
	 * There is definitely a duplicate.
	 * If current ref was inserted into the head of the chain,
	 * the previous head's flags must be set to SVDUP. (1)
	 * Otherwise, the new inserted ref's flags must be set to SVDUP. (2)
	 * First of all, let's find the just inserted svref.
	 */
	struct tree_svindex_key tree_key;
	tree_key.data = ref.v->data;
	tree_key.size = ref.v->size;
	tree_key.lsn = ref.v->lsn;
	bool exact;
	struct bps_tree_svindex_iterator itr =
		bps_tree_svindex_lower_bound(&i->tree, &tree_key, &exact);
	assert(!bps_tree_svindex_itr_is_invalid(&itr));
	struct svref *curr =
		bps_tree_svindex_itr_get_elem(&i->tree, &itr);
	/* Find previous position */
	struct bps_tree_svindex_iterator itr_prev = itr;
	bps_tree_svindex_itr_prev(&i->tree, &itr_prev);
	if (!bps_tree_svindex_itr_is_invalid(&itr_prev)) {
		struct svref *prev =
			bps_tree_svindex_itr_get_elem(&i->tree, &itr_prev);
		if (sf_compare(i->key_def, curr->v->data, prev->v->data) == 0) {
			/*
			 * Previous position exists and holds same key,
			 * it's case (2)
			 */
			curr->flags |= SVDUP;
			return 0;
		}
	}
	/*
	 * Previous position does not exist or holds another key,
	 * it's case (1). Next position holds previous head of chain.
	 */
	struct bps_tree_svindex_iterator itr_next = itr;
	bps_tree_svindex_itr_next(&i->tree, &itr_next);
	assert(!bps_tree_svindex_itr_is_invalid(&itr_next));
	struct svref *next =
		bps_tree_svindex_itr_get_elem(&i->tree, &itr_next);
	next->flags |= SVDUP;
	return 0;
}
/*
 * Find a value in index with given key and biggest lsn <= given lsn
 */
static struct svref *
sv_indexfind(struct svindex *i, char *key, int size, uint64_t lsn)
{
	assert(i == i->tree.arg);
	struct tree_svindex_key tree_key;
	tree_key.data = key;
	tree_key.size = size;
	tree_key.lsn = lsn;
	bool exact;
	struct bps_tree_svindex_iterator itr =
		bps_tree_svindex_lower_bound(&i->tree, &tree_key, &exact);
	struct svref *ref = bps_tree_svindex_itr_get_elem(&i->tree, &itr);
	if (ref != NULL && tree_svindex_compare_key(*ref, &tree_key, i) != 0)
		ref = NULL;
	return ref;
}

static inline uint32_t
sv_indexused(struct svindex *i) {
	return i->tree.size * sizeof(struct vinyl_tuple) + i->used +
		bps_tree_svindex_mem_used(&i->tree);
}

static uint8_t
sv_refifflags(struct sv *v) {
	struct svref *ref = (struct svref *)v->v;
	return (ref->v)->flags | ref->flags;
}

static uint64_t
sv_refiflsn(struct sv *v) {
	struct svref *ref = (struct svref *)v->v;
	return ref->v->lsn;
}

static void
sv_refiflsnset(struct sv *v, int64_t lsn) {
	struct svref *ref = (struct svref *)v->v;
	ref->v->lsn = lsn;
}

static char*
sv_refifpointer(struct sv *v) {
	struct svref *ref = (struct svref *)v->v;
	return ref->v->data;
}

static uint32_t
sv_refifsize(struct sv *v) {
	struct svref *ref = (struct svref *)v->v;
	return ref->v->size;
}

static struct svif sv_refif =
{
       .flags     = sv_refifflags,
       .lsn       = sv_refiflsn,
       .lsnset    = sv_refiflsnset,
       .pointer   = sv_refifpointer,
       .size      = sv_refifsize
};

struct svindexiter {
	struct svindex *index;
	struct bps_tree_svindex_iterator itr;
	struct sv current;
	enum vinyl_order order;
};

static struct ssiterif sv_indexiterif;

static inline int
sv_indexiter_open(struct ssiter *i, struct svindex *index,
		  enum vinyl_order o, void *key, int keysize)
{
	assert(index == index->tree.arg);
	assert(o == VINYL_GT || o == VINYL_GE || o == VINYL_LT || o == VINYL_LE);
	i->vif = &sv_indexiterif;
	struct svindexiter *ii = (struct svindexiter *)i->priv;
	struct bps_tree_svindex *tree = &index->tree;
	ii->index = index;
	ii->order = o;
	ii->current.i = &sv_refif;
	if (key == NULL) {
		if (o == VINYL_GT || o == VINYL_GE) {
			ii->itr = bps_tree_svindex_itr_first(tree);
		} else {
			assert(o == VINYL_LT || o == VINYL_LE);
			ii->itr = bps_tree_svindex_itr_last(tree);
		}
		return 0;
	}

	struct tree_svindex_key tree_key;
	tree_key.data = key;
	tree_key.size = keysize;
	tree_key.lsn = (o == VINYL_GE || o == VINYL_LT) ? UINT64_MAX : 0;
	bool exact;
	ii->index->hint_key_is_equal = false;
	ii->itr = bps_tree_svindex_lower_bound(tree, &tree_key, &exact);
	if (o == VINYL_LE || o == VINYL_LT)
		bps_tree_svindex_itr_prev(tree, &ii->itr);
	return (int)ii->index->hint_key_is_equal;
}

static inline void
sv_indexiter_close(struct ssiter *i)
{
	(void)i;
}

static inline int
sv_indexiter_has(struct ssiter *i)
{
	struct svindexiter *ii = (struct svindexiter *)i->priv;
	return !bps_tree_svindex_itr_is_invalid(&ii->itr);
}

static inline void *
sv_indexiter_get(struct ssiter *i)
{
	struct svindexiter *ii = (struct svindexiter *)i->priv;
	if (bps_tree_svindex_itr_is_invalid(&ii->itr))
		return NULL;
	ii->current.v = (void *)
		bps_tree_svindex_itr_get_elem(&ii->index->tree, &ii->itr);
	assert(ii->current.v != NULL);
	return (void *)&ii->current;
}

static inline void
sv_indexiter_next(struct ssiter *i)
{
	struct svindexiter *ii = (struct svindexiter *)i->priv;
	assert(!bps_tree_svindex_itr_is_invalid(&ii->itr));

	if (ii->order == VINYL_GT || ii->order == VINYL_GE) {
		bps_tree_svindex_itr_next(&ii->index->tree, &ii->itr);
	} else {
		assert(ii->order == VINYL_LT || ii->order == VINYL_LE);
		bps_tree_svindex_itr_prev(&ii->index->tree, &ii->itr);
	}
}

static struct ssiterif sv_indexiterif =
{
	.close   = sv_indexiter_close,
	.has     = sv_indexiter_has,
	.get     = sv_indexiter_get,
	.next    = sv_indexiter_next
};

static uint8_t
sv_upsertvifflags(struct sv *v) {
	struct svupsertnode *n = v->v;
	return n->flags;
}

static uint64_t
sv_upsertviflsn(struct sv *v) {
	struct svupsertnode *n = v->v;
	return n->lsn;
}

static void
sv_upsertviflsnset(struct sv *v ssunused, int64_t lsn ssunused) {
	unreachable();
}

static char*
sv_upsertvifpointer(struct sv *v) {
	struct svupsertnode *n = v->v;
	return n->buf.s;
}

static uint32_t
sv_upsertvifsize(struct sv *v) {
	struct svupsertnode *n = v->v;
	return ss_bufused(&n->buf);
}

static struct svif sv_upsertvif =
{
	.flags     = sv_upsertvifflags,
	.lsn       = sv_upsertviflsn,
	.lsnset    = sv_upsertviflsnset,
	.pointer   = sv_upsertvifpointer,
	.size      = sv_upsertvifsize
};

static uint8_t
sv_tupleflags(struct sv *v) {
	return sv_to_tuple(v)->flags;
}

static uint64_t
sv_tuplelsn(struct sv *v) {
	return sv_to_tuple(v)->lsn;
}

static void
sv_tuplelsnset(struct sv *v, int64_t lsn) {
	sv_to_tuple(v)->lsn = lsn;
}

static char*
sv_tuplepointer(struct sv *v) {
	return sv_to_tuple(v)->data;
}

static uint32_t
sv_tuplesize(struct sv *v) {
	return sv_to_tuple(v)->size;
}

static struct svif sv_tupleif =
{
	.flags     = sv_tupleflags,
	.lsn       = sv_tuplelsn,
	.lsnset    = sv_tuplelsnset,
	.pointer   = sv_tuplepointer,
	.size      = sv_tuplesize
};

struct sxv {
	uint64_t id;
	uint32_t lo;
	uint64_t csn;
	struct sxindex *index;
	struct vinyl_tuple *tuple;
	struct sxv *next;
	struct sxv *prev;
	struct sxv *gc;
	rb_node(struct sxv) tree_node;
};

static inline struct sxv*
sx_valloc(struct vinyl_tuple *ref)
{
	struct sxv *v = malloc(sizeof(struct sxv));
	if (unlikely(v == NULL))
		return NULL;
	v->index = NULL;
	v->id    = 0;
	v->lo    = 0;
	v->csn   = 0;
	v->tuple = ref;
	v->next  = NULL;
	v->prev  = NULL;
	v->gc    = NULL;
	return v;
}

static inline void
sx_vfree(struct runtime *r, struct sxv *v)
{
	vinyl_tuple_unref_rt(r, v->tuple);
	free(v);
}

static inline void
sx_vfreeall(struct runtime *r, struct sxv *v)
{
	while (v) {
		struct sxv *next = v->next;
		sx_vfree(r, v);
		v = next;
	}
}

static inline struct sxv*
sx_vmatch(struct sxv *head, uint64_t id)
{
	struct sxv *c = head;
	while (c) {
		if (c->id == id)
			break;
		c = c->next;
	}
	return c;
}

static inline void
sx_vreplace(struct sxv *v, struct sxv *n)
{
	if (v->prev)
		v->prev->next = n;
	if (v->next)
		v->next->prev = n;
	n->next = v->next;
	n->prev = v->prev;
}

static inline void
sx_vlink(struct sxv *head, struct sxv *v)
{
	struct sxv *c = head;
	while (c->next)
		c = c->next;
	c->next = v;
	v->prev = c;
	v->next = NULL;
}

static inline void
sx_vunlink(struct sxv *v)
{
	if (v->prev)
		v->prev->next = v->next;
	if (v->next)
		v->next->prev = v->prev;
	v->prev = NULL;
	v->next = NULL;
}

static inline void
sx_vcommit(struct sxv *v, uint32_t csn)
{
	v->id  = UINT64_MAX;
	v->lo  = UINT32_MAX;
	v->csn = csn;
}

static inline int
sx_vcommitted(struct sxv *v)
{
	return v->id == UINT64_MAX && v->lo == UINT32_MAX;
}

static inline void
sx_vabort(struct sxv *v)
{
	v->tuple->flags |= SVCONFLICT;
}

static inline void
sx_vabort_all(struct sxv *v)
{
	while (v) {
		sx_vabort(v);
		v = v->next;
	}
}

static inline int
sx_vaborted(struct sxv *v)
{
	return v->tuple->flags & SVCONFLICT;
}

static struct svif sx_vif;

enum sxstate {
	SXUNDEF,
	SXREADY,
	SXCOMMIT,
	SXPREPARE,
	SXROLLBACK,
	SXLOCK
};

enum sxtype {
	SXRO,
	SXRW
};

typedef rb_tree(struct sxv) sxv_tree_t;

struct sxv_tree_key {
	char *data;
	int size;
};

static int
sxv_tree_cmp(sxv_tree_t *rbtree, struct sxv *a, struct sxv *b);

static int
sxv_tree_key_cmp(sxv_tree_t *rbtree, struct sxv_tree_key *a, struct sxv *b);

rb_gen_ext_key(, sxv_tree_, sxv_tree_t, struct sxv, tree_node, sxv_tree_cmp,
		 struct sxv_tree_key *, sxv_tree_key_cmp);

static struct sxv *
sxv_tree_search_key(sxv_tree_t *rbtree, char *data, int size)
{
	struct sxv_tree_key key;
	key.data = data;
	key.size = size;
	return sxv_tree_search(rbtree, &key);
}

struct sxindex {
	sxv_tree_t tree;
	uint32_t  dsn;
	struct vinyl_index *index;
	struct key_def *key_def;
	struct rlist    link;
	pthread_mutex_t mutex;
};

static int
sxv_tree_cmp(sxv_tree_t *rbtree, struct sxv *a, struct sxv *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct sxindex, tree)->key_def;
	return sf_compare(key_def, a->tuple->data, b->tuple->data);
}

static int
sxv_tree_key_cmp(sxv_tree_t *rbtree, struct sxv_tree_key *a, struct sxv *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct sxindex, tree)->key_def;
	return sf_compare(key_def, a->data, b->tuple->data);
}

struct sx;
struct sicache;

typedef int (*sxpreparef)(struct sx*, struct sv*, struct vinyl_index*,
			  struct sicache *);

struct sx {
	enum sxtype     type;
	enum sxstate    state;
	uint64_t   id;
	uint64_t   vlsn;
	uint64_t   csn;
	int        log_read;
	struct svlog     *log;
	struct rlist     deadlock;
	rb_node(struct sx) tree_node;
	struct sxmanager *manager;
};

typedef rb_tree(struct sx) sx_tree_t;

static int
sx_tree_cmp(sx_tree_t *rbtree, struct sx *a, struct sx *b)
{
	(void)rbtree;
	return ss_cmp(a->id, b->id);
}

static int
sx_tree_key_cmp(sx_tree_t *rbtree, const char *a, struct sx *b)
{
	(void)rbtree;
	return ss_cmp(load_u64(a), b->id);
}

rb_gen_ext_key(, sx_tree_, sx_tree_t, struct sx, tree_node,
		 sx_tree_cmp, const char *, sx_tree_key_cmp);

struct sxmanager {
	pthread_mutex_t  lock;
	sx_tree_t tree;
	struct rlist      indexes;
	uint32_t    count_rd;
	uint32_t    count_rw;
	uint32_t    count_gc;
	uint64_t    csn;
	struct sxv        *gc;
	struct runtime         *r;
};

static int sx_managerinit(struct sxmanager*, struct runtime*);
static int sx_managerfree(struct sxmanager*);
static int sx_indexinit(struct sxindex *, struct sxmanager *,
			struct vinyl_index *, struct key_def *key_def);
static int sx_indexset(struct sxindex*, uint32_t);
static int sx_indexfree(struct sxindex*, struct sxmanager*);
static struct sx *sx_find(struct sxmanager*, uint64_t);
static void sx_begin(struct sxmanager*, struct sx*, enum sxtype, struct svlog*);
static void sx_gc(struct sx*);
static enum sxstate sx_commit(struct sx*);
static enum sxstate sx_rollback(struct sx*);
static int sx_set(struct sx*, struct sxindex*, struct vinyl_tuple*);
static int sx_get(struct sx*, struct sxindex*, struct vinyl_tuple*, struct vinyl_tuple**);
static uint64_t sx_min(struct sxmanager*);
static uint64_t sx_max(struct sxmanager*);
static uint64_t sx_vlsn(struct sxmanager*);
static enum sxstate sx_get_autocommit(struct sxmanager*, struct sxindex*);

static int sx_deadlock(struct sx*);

static inline int
sx_count(struct sxmanager *m) {
	return m->count_rd + m->count_rw;
}

static int sx_managerinit(struct sxmanager *m, struct runtime *r)
{
	sx_tree_new(&m->tree);
	m->count_rd = 0;
	m->count_rw = 0;
	m->count_gc = 0;
	m->csn = 0;
	m->gc  = NULL;
	tt_pthread_mutex_init(&m->lock, NULL);
	rlist_create(&m->indexes);
	m->r = r;
	return 0;
}

static int sx_managerfree(struct sxmanager *m)
{
	assert(sx_count(m) == 0);
	tt_pthread_mutex_destroy(&m->lock);
	return 0;
}

static int sx_indexinit(struct sxindex *i, struct sxmanager *m,
			struct vinyl_index *index, struct key_def *key_def)
{
	sxv_tree_new(&i->tree);
	rlist_create(&i->link);
	i->dsn = 0;
	i->index = index;
	i->key_def = key_def;
	(void) tt_pthread_mutex_init(&i->mutex, NULL);
	rlist_add(&m->indexes, &i->link);
	return 0;
}

static int sx_indexset(struct sxindex *i, uint32_t dsn)
{
	i->dsn = dsn;
	return 0;
}

static struct sxv *
sxv_tree_free_cb(sxv_tree_t *t, struct sxv *v, void *arg)
{
	(void)t;
	sx_vfreeall((struct runtime *)arg, v);
	return NULL;
}

static inline void
sx_indextruncate(struct sxindex *i, struct sxmanager *m)
{
	tt_pthread_mutex_lock(&i->mutex);
	sxv_tree_iter(&i->tree, NULL, sxv_tree_free_cb, m->r);
	sxv_tree_new(&i->tree);
	tt_pthread_mutex_unlock(&i->mutex);
}

static int
sx_indexfree(struct sxindex *i, struct sxmanager *m)
{
	sx_indextruncate(i, m);
	rlist_del(&i->link);
	(void) tt_pthread_mutex_destroy(&i->mutex);
	return 0;
}

static uint64_t sx_min(struct sxmanager *m)
{
	tt_pthread_mutex_lock(&m->lock);
	uint64_t id = 0;
	if (sx_count(m) > 0) {
		struct sx *min = sx_tree_first(&m->tree);
		id = min->id;
	}
	tt_pthread_mutex_unlock(&m->lock);
	return id;
}

static uint64_t sx_max(struct sxmanager *m)
{
	tt_pthread_mutex_lock(&m->lock);
	uint64_t id = 0;
	if (sx_count(m) > 0) {
		struct sx *max = sx_tree_last(&m->tree);
		id = max->id;
	}
	tt_pthread_mutex_unlock(&m->lock);
	return id;
}

static uint64_t sx_vlsn(struct sxmanager *m)
{
	tt_pthread_mutex_lock(&m->lock);
	uint64_t vlsn;
	if (sx_count(m) > 0) {
		struct sx *min = sx_tree_first(&m->tree);
		vlsn = min->vlsn;
	} else {
		vlsn = sr_seq(m->r->seq, SR_LSN);
	}
	tt_pthread_mutex_unlock(&m->lock);
	return vlsn;
}

static struct sx *sx_find(struct sxmanager *m, uint64_t id)
{
	return sx_tree_search(&m->tree, (char*)&id);
}

static inline enum sxstate
sx_promote(struct sx *x, enum sxstate state)
{
	x->state = state;
	return state;
}

static void
sx_begin(struct sxmanager *m, struct sx *x, enum sxtype type,
	 struct svlog *log)
{
	x->manager = m;
	x->log = log;
	x->state = SXREADY;
	x->type = type;
	x->log_read = -1;
	rlist_create(&x->deadlock);

	sr_seqlock(m->r->seq);
	x->csn = m->csn;
	x->id = sr_seqdo(m->r->seq, SR_TSNNEXT);
	x->vlsn = sr_seqdo(m->r->seq, SR_LSN);
	sr_sequnlock(m->r->seq);

	tt_pthread_mutex_lock(&m->lock);
	sx_tree_insert(&m->tree, x);
	if (type == SXRO)
		m->count_rd++;
	else
		m->count_rw++;
	tt_pthread_mutex_unlock(&m->lock);
}

static inline void
sx_untrack(struct sxv *v)
{
	if (v->prev == NULL) {
		struct sxindex *i = v->index;
		tt_pthread_mutex_lock(&i->mutex);
		sxv_tree_remove(&i->tree, v);
		if (v->next != NULL)
			sxv_tree_insert(&i->tree, v->next);
		tt_pthread_mutex_unlock(&i->mutex);
	}
	sx_vunlink(v);
}

static inline uint64_t
sx_csn(struct sxmanager *m)
{
	uint64_t csn = UINT64_MAX;
	if (m->count_rw == 0)
		return csn;
	struct sx *min = sx_tree_first(&m->tree);
	while (min) {
		if (min->type != SXRO) {
			break;
		}
		min = sx_tree_next(&m->tree, min);
	}
	assert(min != NULL);
	return min->csn;
}

static inline void
sx_garbage_collect(struct sxmanager *m)
{
	uint64_t min_csn = sx_csn(m);
	struct sxv *gc = NULL;
	uint32_t count = 0;
	struct sxv *next;
	struct sxv *v = m->gc;
	for (; v; v = next)
	{
		next = v->gc;
		assert(v->tuple->flags & SVGET);
		assert(sx_vcommitted(v));
		if (v->csn > min_csn) {
			v->gc = gc;
			gc = v;
			count++;
			continue;
		}
		sx_untrack(v);
		sx_vfree(m->r, v);
	}
	m->count_gc = count;
	m->gc = gc;
}

static void sx_gc(struct sx *x)
{
	struct sxmanager *m = x->manager;
	sx_promote(x, SXUNDEF);
	x->log = NULL;
	if (m->count_gc == 0)
		return;
	sx_garbage_collect(m);
}

static inline void
sx_end(struct sx *x)
{
	struct sxmanager *m = x->manager;
	tt_pthread_mutex_lock(&m->lock);
	sx_tree_remove(&m->tree, x);
	if (x->type == SXRO)
		m->count_rd--;
	else
		m->count_rw--;
	tt_pthread_mutex_unlock(&m->lock);
}

static inline void
sx_rollback_svp(struct sx *x, struct ssbufiter *i, int tuple_free)
{
	struct sxmanager *m = x->manager;
	int gc = 0;
	for (; ss_bufiter_has(i); ss_bufiter_next(i))
	{
		struct svlogv *lv = ss_bufiter_get(i);
		struct sxv *v = lv->v.v;
		/* remove from index and replace head with
		 * a first waiter */
		sx_untrack(v);
		/* translate log version from struct sxv to struct vinyl_tuple */
		sv_from_tuple(&lv->v, v->tuple);
		if (tuple_free) {
			int size = vinyl_tuple_size(v->tuple);
			if (vinyl_tuple_unref_rt(m->r, v->tuple))
				gc += size;
		}
		free(v);
	}
	ss_quota(m->r->quota, SS_QREMOVE, gc);
}

static enum sxstate sx_rollback(struct sx *x)
{
	struct sxmanager *m = x->manager;
	struct ssbufiter i;
	ss_bufiter_open(&i, &x->log->buf, sizeof(struct svlogv));
	/* support log free after commit and half-commit mode */
	if (x->state == SXCOMMIT) {
		int gc = 0;
		for (; ss_bufiter_has(&i); ss_bufiter_next(&i))
		{
			struct svlogv *lv = ss_bufiter_get(&i);
			struct vinyl_tuple *v = lv->v.v;
			int size = vinyl_tuple_size(v);
			if (vinyl_tuple_unref_rt(m->r, v))
				gc += size;
		}
		ss_quota(m->r->quota, SS_QREMOVE, gc);
		sx_promote(x, SXROLLBACK);
		return SXROLLBACK;
	}
	sx_rollback_svp(x, &i, 1);
	sx_promote(x, SXROLLBACK);
	sx_end(x);
	return SXROLLBACK;
}


static inline int
sx_preparev(struct sx *x, struct svlogv *v, uint64_t lsn,
	    struct sicache *cache, enum vinyl_status status);

static enum sxstate
sx_prepare(struct sx *x, struct sicache *cache, enum vinyl_status status)
{
	uint64_t lsn = sr_seq(x->manager->r->seq, SR_LSN);
	/* proceed read-only transactions */
	if (x->type == SXRO || sv_logcount_write(x->log) == 0)
		return sx_promote(x, SXPREPARE);
	struct ssbufiter i;
	ss_bufiter_open(&i, &x->log->buf, sizeof(struct svlogv));
	enum sxstate rc;
	for (; ss_bufiter_has(&i); ss_bufiter_next(&i))
	{
		struct svlogv *lv = ss_bufiter_get(&i);
		struct sxv *v = lv->v.v;
		if ((int)v->lo == x->log_read)
			break;
		if (sx_vaborted(v))
			return sx_promote(x, SXROLLBACK);
		if (likely(v->prev == NULL)) {
			rc = sx_preparev(x, lv, lsn, cache, status);
			if (unlikely(rc != 0))
				return sx_promote(x, SXROLLBACK);
			continue;
		}
		if (sx_vcommitted(v->prev)) {
			if (v->prev->csn > x->csn)
				return sx_promote(x, SXROLLBACK);
			continue;
		}
		/* force commit for read-only conflicts */
		if (v->prev->tuple->flags & SVGET) {
			rc = sx_preparev(x, lv, lsn, cache, status);
			if (unlikely(rc != 0))
				return sx_promote(x, SXROLLBACK);
			continue;
		}
		return sx_promote(x, SXLOCK);
	}
	return sx_promote(x, SXPREPARE);
}

static enum sxstate sx_commit(struct sx *x)
{
	assert(x->state == SXPREPARE);

	struct sxmanager *m = x->manager;
	struct ssbufiter i;
	ss_bufiter_open(&i, &x->log->buf, sizeof(struct svlogv));
	uint64_t csn = ++m->csn;
	for (; ss_bufiter_has(&i); ss_bufiter_next(&i))
	{
		struct svlogv *lv = ss_bufiter_get(&i);
		struct sxv *v = lv->v.v;
		if ((int)v->lo == x->log_read)
			break;
		/* abort conflict reader */
		if (v->prev && !sx_vcommitted(v->prev)) {
			assert(v->prev->tuple->flags & SVGET);
			sx_vabort(v->prev);
		}
		/* abort waiters */
		sx_vabort_all(v->next);
		/* mark stmt as commited */
		sx_vcommit(v, csn);
		/* translate log version from struct sxv to struct vinyl_tuple */
		sv_from_tuple(&lv->v, v->tuple);
		/* schedule read stmt for gc */
		if (v->tuple->flags & SVGET) {
			vinyl_tuple_ref(v->tuple);
			v->gc = m->gc;
			m->gc = v;
			m->count_gc++;
		} else {
			sx_untrack(v);
			free(v);
		}
	}

	/* rollback latest reads */
	sx_rollback_svp(x, &i, 0);

	sx_promote(x, SXCOMMIT);
	sx_end(x);
	return SXCOMMIT;
}

static int sx_set(struct sx *x, struct sxindex *index, struct vinyl_tuple *version)
{
	struct sxmanager *m = x->manager;
	struct runtime *r = m->r;
	if (! (version->flags & SVGET)) {
		x->log_read = -1;
	}
	/* allocate mvcc container */
	struct sxv *v = sx_valloc(version);
	if (unlikely(v == NULL)) {
		ss_quota(r->quota, SS_QREMOVE, vinyl_tuple_size(version));
		vinyl_tuple_unref_rt(r, version);
		return -1;
	}
	v->id = x->id;
	v->index = index;
	struct svlogv lv;
	lv.id   = index->dsn;
	lv.next = UINT32_MAX;
	sv_from_sxv(&lv.v, v);
	/* update concurrent index */
	tt_pthread_mutex_lock(&index->mutex);
	struct sxv *head = sxv_tree_search_key(&index->tree,
					       v->tuple->data, v->tuple->size);
	if (unlikely(head == NULL)) {
		/* unique */
		v->lo = sv_logcount(x->log);
		int rc = sv_logadd(x->log, &lv, index->index);
		if (unlikely(rc == -1)) {
			sr_oom();
			goto error;
		}
		sxv_tree_insert(&index->tree, v);
		tt_pthread_mutex_unlock(&index->mutex);
		return 0;
	}
	/* exists */
	/* match previous update made by current transaction */
	struct sxv *own = sx_vmatch(head, x->id);
	if (unlikely(own))
	{
		if (unlikely(version->flags & SVUPSERT)) {
			sr_error("%s", "only one upsert statement is "
			         "allowed per a transaction key");
			goto error;
		}
		/* replace old document with the new one */
		lv.next = sv_logat(x->log, own->lo)->next;
		v->lo = own->lo;
		if (unlikely(sx_vaborted(own)))
			sx_vabort(v);
		sx_vreplace(own, v);
		if (likely(head == own)) {
			sxv_tree_remove(&index->tree, own);
			sxv_tree_insert(&index->tree, v);
		}
		/* update log */
		sv_logreplace(x->log, v->lo, &lv);

		ss_quota(r->quota, SS_QREMOVE, vinyl_tuple_size(own->tuple));
		sx_vfree(r, own);
		tt_pthread_mutex_unlock(&index->mutex);
		return 0;
	}
	/* update log */
	v->lo = sv_logcount(x->log);
	int rc = sv_logadd(x->log, &lv, index->index);
	if (unlikely(rc == -1)) {
		sr_oom();
		goto error;
	}
	/* add version */
	sx_vlink(head, v);
	tt_pthread_mutex_unlock(&index->mutex);
	return 0;
error:
	tt_pthread_mutex_unlock(&index->mutex);
	ss_quota(r->quota, SS_QREMOVE, vinyl_tuple_size(v->tuple));
	sx_vfree(r, v);
	return -1;
}

static int sx_get(struct sx *x, struct sxindex *index, struct vinyl_tuple *key,
		  struct vinyl_tuple **result)
{
	tt_pthread_mutex_lock(&index->mutex);
	struct sxv *head = sxv_tree_search_key(&index->tree,
					       key->data, key->size);
	if (head == NULL)
		goto add;
	struct sxv *v = sx_vmatch(head, x->id);
	if (v == NULL)
		goto add;
	tt_pthread_mutex_unlock(&index->mutex);
	struct vinyl_tuple *tuple = v->tuple;
	if (unlikely((tuple->flags & SVGET) > 0))
		return 0;
	if (unlikely((tuple->flags & SVDELETE) > 0))
		return 2;
	*result = tuple;
	vinyl_tuple_ref(tuple);
	return 1;

add:
	/* track a start of the latest read sequence in the
	 * transactional log */
	if (x->log_read == -1)
		x->log_read = sv_logcount(x->log);
	tt_pthread_mutex_unlock(&index->mutex);
	vinyl_tuple_ref(key);
	int rc = sx_set(x, index, key);
	if (unlikely(rc == -1))
		return -1;
	return 0;
}

static enum sxstate
sx_get_autocommit(struct sxmanager *m, struct sxindex *index ssunused)
{
	sr_seq(m->r->seq, SR_TSNNEXT);
	return SXCOMMIT;
}

static inline int
sx_deadlock_in(struct sxmanager *m, struct rlist *mark, struct sx *t, struct sx *p)
{
	if (p->deadlock.next != &p->deadlock)
		return 0;
	rlist_add(mark, &p->deadlock);
	struct ssbufiter i;
	ss_bufiter_open(&i, &p->log->buf, sizeof(struct svlogv));
	for (; ss_bufiter_has(&i); ss_bufiter_next(&i))
	{
		struct svlogv *lv = ss_bufiter_get(&i);
		struct sxv *v = lv->v.v;
		if (v->prev == NULL)
			continue;
		do {
			struct sx *n = sx_find(m, v->id);
			assert(n != NULL);
			if (unlikely(n == t))
				return 1;
			int rc = sx_deadlock_in(m, mark, t, n);
			if (unlikely(rc == 1))
				return 1;
			v = v->prev;
		} while (v);
	}
	return 0;
}

static inline void
sx_deadlock_unmark(struct rlist *mark)
{
	struct sx *t, *n;
	rlist_foreach_entry_safe(t, mark, deadlock, n) {
		rlist_create(&t->deadlock);
	}
}

static int ssunused sx_deadlock(struct sx *t)
{
	struct sxmanager *m = t->manager;
	struct rlist mark;
	rlist_create(&mark);
	struct ssbufiter i;
	ss_bufiter_open(&i, &t->log->buf, sizeof(struct svlogv));
	while (ss_bufiter_has(&i))
	{
		struct svlogv *lv = ss_bufiter_get(&i);
		struct sxv *v = lv->v.v;
		if (v->prev == NULL) {
			ss_bufiter_next(&i);
			continue;
		}
		struct sx *p = sx_find(m, v->prev->id);
		assert(p != NULL);
		int rc = sx_deadlock_in(m, &mark, t, p);
		if (unlikely(rc)) {
			sx_deadlock_unmark(&mark);
			return 1;
		}
		ss_bufiter_next(&i);
	}
	sx_deadlock_unmark(&mark);
	return 0;
}

static uint8_t
sx_vifflags(struct sv *v) {
	return sv_to_sxv(v)->tuple->flags;
}

static uint64_t
sx_viflsn(struct sv *v) {
	return sv_to_sxv(v)->tuple->lsn;
}

static void
sx_viflsnset(struct sv *v, int64_t lsn) {
	sv_to_sxv(v)->tuple->lsn = lsn;
}

static char*
sx_vifpointer(struct sv *v) {
	return sv_to_sxv(v)->tuple->data;
}

static uint32_t
sx_vifsize(struct sv *v) {
	return sv_to_sxv(v)->tuple->size;
}

static struct svif sx_vif =
{
	.flags     = sx_vifflags,
	.lsn       = sx_viflsn,
	.lsnset    = sx_viflsnset,
	.pointer   = sx_vifpointer,
	.size      = sx_vifsize
};

static void
sl_write(struct svlog *vlog, int64_t lsn)
{
	struct ssbufiter i;
	ss_bufiter_open(&i, &vlog->buf, sizeof(struct svlogv));
	for (; ss_bufiter_has(&i); ss_bufiter_next(&i))
	{
		struct svlogv *v = ss_bufiter_get(&i);
		sv_lsnset(&v->v, lsn);
	}
}

#define SD_IDBRANCH 1

struct PACKED sdid {
	uint64_t parent;
	uint64_t id;
	uint8_t  flags;
};

static inline void
sd_idinit(struct sdid *i, uint64_t id, uint64_t parent, uint8_t flags)
{
	i->id     = id;
	i->parent = parent;
	i->flags  = flags;
}

struct PACKED sdv {
	uint32_t offset;
	uint8_t  flags;
	uint64_t lsn;
	uint32_t size;
};

struct PACKED sdpageheader {
	uint32_t crc;
	uint32_t crcdata;
	uint32_t count;
	uint32_t countdup;
	uint32_t sizeorigin;
	uint32_t sizekeys;
	uint32_t size;
	uint64_t lsnmin;
	uint64_t lsnmindup;
	uint64_t lsnmax;
	uint32_t reserve;
};

struct sdpage {
	struct sdpageheader *h;
};

static inline void
sd_pageinit(struct sdpage *p, struct sdpageheader *h) {
	p->h = h;
}

static inline struct sdv*
sd_pagev(struct sdpage *p, uint32_t pos) {
	assert(pos < p->h->count);
	return (struct sdv*)((char*)p->h + sizeof(struct sdpageheader) + sizeof(struct sdv) * pos);
}

static inline void*
sd_pagepointer(struct sdpage *p, struct sdv *v) {
	assert((sizeof(struct sdv) * p->h->count) + v->offset <= p->h->sizeorigin);
	return ((char*)p->h + sizeof(struct sdpageheader) +
	         sizeof(struct sdv) * p->h->count) + v->offset;
}

struct PACKED sdpageiter {
	struct sdpage *page;
	struct ssbuf *xfbuf;
	int64_t pos;
	struct sdv *v;
	struct sv current;
	enum vinyl_order order;
	void *key;
	int keysize;
	struct key_def *key_def;
};

static inline void
sd_pageiter_result(struct sdpageiter *i)
{
	if (unlikely(i->v == NULL))
		return;
	sv_from_sdv(&i->current, i->v, i->page->h);
}

static inline void
sd_pageiter_end(struct sdpageiter *i)
{
	i->pos = i->page->h->count;
	i->v   = NULL;
}

static inline int
sd_pageiter_cmp(struct sdpageiter *i, struct key_def *key_def, struct sdv *v)
{
	return sf_compare(key_def, sd_pagepointer(i->page, v), i->key);
}

static inline int
sd_pageiter_search(struct sdpageiter *i)
{
	int min = 0;
	int mid = 0;
	int max = i->page->h->count - 1;
	while (max >= min)
	{
		mid = min + (max - min) / 2;
		int rc = sd_pageiter_cmp(i, i->key_def, sd_pagev(i->page, mid));
		if (rc < 0) {
			min = mid + 1;
		} else if (rc > 0) {
			max = mid - 1;
		} else {
			return mid;
		}
	}
	return min;
}

static inline void
sd_pageiter_chain_head(struct sdpageiter *i, int64_t pos)
{
	/* find first non-duplicate key */
	while (pos >= 0) {
		struct sdv *v = sd_pagev(i->page, pos);
		if (likely(! (v->flags & SVDUP))) {
			i->pos = pos;
			i->v = v;
			return;
		}
		pos--;
	}
	sd_pageiter_end(i);
}

static inline void
sd_pageiter_chain_next(struct sdpageiter *i)
{
	/* skip to next duplicate chain */
	int64_t pos = i->pos + 1;
	while (pos < i->page->h->count) {
		struct sdv *v = sd_pagev(i->page, pos);
		if (likely(! (v->flags & SVDUP))) {
			i->pos = pos;
			i->v = v;
			return;
		}
		pos++;
	}
	sd_pageiter_end(i);
}

static inline int
sd_pageiter_gt(struct sdpageiter *i, bool eq)
{
	if (i->key == NULL) {
		i->pos = 0;
		i->v = sd_pagev(i->page, i->pos);
		return 0;
	}
	int64_t pos = sd_pageiter_search(i);
	if (unlikely(pos >= i->page->h->count))
		pos = i->page->h->count - 1;
	sd_pageiter_chain_head(i, pos);
	if (i->v == NULL)
		return 0;
	int rc = sd_pageiter_cmp(i, i->key_def, i->v);
	if (rc < 0) {
		sd_pageiter_chain_next(i);
		return 0;
	} else if (rc > 0) {
		return 0;
	} else {
		assert(rc == 0);
		if (!eq)
			sd_pageiter_chain_next(i);
		return 1;
	}
}

static inline int
sd_pageiter_lt(struct sdpageiter *i, bool eq)
{
	if (i->key == NULL) {
		sd_pageiter_chain_head(i, i->page->h->count - 1);
		return 0;
	}
	int64_t pos = sd_pageiter_search(i);
	if (unlikely(pos >= i->page->h->count))
		pos = i->page->h->count - 1;
	sd_pageiter_chain_head(i, pos);
	if (i->v == NULL)
		return 0;
	int rc = sd_pageiter_cmp(i, i->key_def, i->v);
	if (rc < 0) {
		return 0;
	} else if (rc > 0) {
		sd_pageiter_chain_head(i, i->pos - 1);
		return 1;
	} else {
		assert(rc == 0);
		if (!eq)
			sd_pageiter_chain_head(i, i->pos - 1);
		return 1;
	}
}

static inline int
sd_pageiter_open(struct sdpageiter *pi, struct key_def *key_def,
		 struct ssbuf *xfbuf, struct sdpage *page, enum vinyl_order o,
		 void *key, int keysize)
{
	pi->key_def = key_def;
	pi->page    = page;
	pi->xfbuf   = xfbuf;
	pi->order   = o;
	pi->key     = key;
	pi->keysize = keysize;
	pi->v       = NULL;
	pi->pos     = 0;
	if (unlikely(pi->page->h->count == 0)) {
		sd_pageiter_end(pi);
		return 0;
	}
	int rc = 0;
	switch (pi->order) {
	case VINYL_GT:  rc = sd_pageiter_gt(pi, false);
		break;
	case VINYL_GE: rc = sd_pageiter_gt(pi, true);
		break;
	case VINYL_LT:  rc = sd_pageiter_lt(pi, false);
		break;
	case VINYL_LE: rc = sd_pageiter_lt(pi, true);
		break;
	default: unreachable();
	}
	sd_pageiter_result(pi);
	return rc;
}

static inline int
sd_pageiter_has(struct sdpageiter *pi)
{
	return pi->v != NULL;
}

static inline void *
sd_pageiter_get(struct sdpageiter *pi)
{
	if (unlikely(pi->v == NULL))
		return NULL;
	return &pi->current;
}

static inline void
sd_pageiter_next(struct sdpageiter *pi)
{
	if (pi->v == NULL)
		return;
	switch (pi->order) {
	case VINYL_GE:
	case VINYL_GT:
		pi->pos++;
		if (unlikely(pi->pos >= pi->page->h->count)) {
			sd_pageiter_end(pi);
			return;
		}
		pi->v = sd_pagev(pi->page, pi->pos);
		break;
	case VINYL_LT:
	case VINYL_LE: {
		/* key (dup) (dup) key (eof) */
		struct sdv *v;
		int64_t pos = pi->pos + 1;
		if (pos < pi->page->h->count) {
			v = sd_pagev(pi->page, pos);
			if (v->flags & SVDUP) {
				pi->pos = pos;
				pi->v   = v;
				break;
			}
		}
		/* skip current chain and position to
		 * the previous one */
		sd_pageiter_chain_head(pi, pi->pos);
		sd_pageiter_chain_head(pi, pi->pos - 1);
		break;
	}
	default: unreachable();
	}
	sd_pageiter_result(pi);
}

struct PACKED sdbuildref {
	uint32_t m, msize;
	uint32_t v, vsize;
	uint32_t c, csize;
};

struct sdbuild {
	struct ssbuf list, m, v, c;
	struct ssfilterif *compress_if;
	int compress;
	int crc;
	uint32_t vmax;
	uint32_t n;
	struct ssa *a;
	struct key_def *key_def;
};

static void sd_buildinit(struct sdbuild*);
static void sd_buildfree(struct sdbuild*);
static void sd_buildreset(struct sdbuild*);
static void sd_buildgc(struct sdbuild*, int);

static inline struct sdbuildref*
sd_buildref(struct sdbuild *b) {
	return ss_bufat(&b->list, sizeof(struct sdbuildref), b->n);
}

static inline struct sdpageheader*
sd_buildheader(struct sdbuild *b) {
	return (struct sdpageheader*)(b->m.s + sd_buildref(b)->m);
}

static inline struct sdv*
sd_buildmin(struct sdbuild *b) {
	return (struct sdv*)((char*)sd_buildheader(b) + sizeof(struct sdpageheader));
}

static inline char*
sd_buildminkey(struct sdbuild *b) {
	struct sdbuildref *r = sd_buildref(b);
	return b->v.s + r->v + sd_buildmin(b)->offset;
}

static inline struct sdv*
sd_buildmax(struct sdbuild *b) {
	struct sdpageheader *h = sd_buildheader(b);
	return (struct sdv*)((char*)h + sizeof(struct sdpageheader) + sizeof(struct sdv) * (h->count - 1));
}

static inline char*
sd_buildmaxkey(struct sdbuild *b) {
	struct sdbuildref *r = sd_buildref(b);
	return b->v.s + r->v + sd_buildmax(b)->offset;
}

static int sd_buildbegin(struct sdbuild*, struct key_def *key_def,
			 int, int, struct ssfilterif*);
static int sd_buildend(struct sdbuild*);
static int sd_buildcommit(struct sdbuild*);
static int sd_buildadd(struct sdbuild*, struct sv*, uint32_t);

struct PACKED sdindexheader {
	uint32_t  crc;
	struct srversion version;
	struct sdid      id;
	uint64_t  offset;
	uint32_t  size;
	uint32_t  sizevmax;
	uint32_t  count;
	uint32_t  keys;
	uint64_t  total;
	uint64_t  totalorigin;
	uint64_t  lsnmin;
	uint64_t  lsnmax;
	uint32_t  dupkeys;
	uint64_t  dupmin;
	uint32_t  extension;
	uint8_t   extensions;
	char      reserve[31];
};

struct PACKED sdindexpage {
	uint64_t offset;
	uint32_t offsetindex;
	uint32_t size;
	uint32_t sizeorigin;
	uint16_t sizemin;
	uint16_t sizemax;
	uint64_t lsnmin;
	uint64_t lsnmax;
};

struct sdindex {
	struct ssbuf i, v;
	struct sdindexheader *h;
};

static inline char*
sd_indexpage_min(struct sdindex *i, struct sdindexpage *p) {
	return (char*)i->i.s + sizeof(struct sdindexheader) +
	             (i->h->count * sizeof(struct sdindexpage)) + p->offsetindex;
}

static inline char*
sd_indexpage_max(struct sdindex *i, struct sdindexpage *p) {
	return sd_indexpage_min(i, p) + p->sizemin;
}

static inline void
sd_indexinit(struct sdindex *i) {
	ss_bufinit(&i->i);
	ss_bufinit(&i->v);
	i->h = NULL;
}

static inline void
sd_indexfree(struct sdindex *i) {
	ss_buffree(&i->i);
	ss_buffree(&i->v);
}

static inline struct sdindexheader*
sd_indexheader(struct sdindex *i) {
	return (struct sdindexheader*)(i->i.s);
}

static inline struct sdindexpage*
sd_indexpage(struct sdindex *i, uint32_t pos)
{
	assert(pos < i->h->count);
	char *p = (char*)ss_bufat(&i->i, sizeof(struct sdindexpage), pos);
	p += sizeof(struct sdindexheader);
	return (struct sdindexpage*)p;
}

static inline struct sdindexpage*
sd_indexmin(struct sdindex *i) {
	return sd_indexpage(i, 0);
}

static inline struct sdindexpage*
sd_indexmax(struct sdindex *i) {
	return sd_indexpage(i, i->h->count - 1);
}

static inline uint32_t
sd_indexkeys(struct sdindex *i)
{
	if (unlikely(i->i.s == NULL))
		return 0;
	return sd_indexheader(i)->keys;
}

static inline uint32_t
sd_indextotal(struct sdindex *i)
{
	if (unlikely(i->i.s == NULL))
		return 0;
	return sd_indexheader(i)->total;
}

static inline uint32_t
sd_indexsize_ext(struct sdindexheader *h)
{
	return sizeof(struct sdindexheader) + h->size + h->extension;
}

static int sd_indexbegin(struct sdindex*);
static int sd_indexcommit(struct sdindex*, struct sdid*, uint64_t);
static int sd_indexadd(struct sdindex*, struct sdbuild*, uint64_t);
static int sd_indexcopy(struct sdindex*, struct sdindexheader*);

struct PACKED sdindexiter {
	struct sdindex *index;
	struct sdindexpage *v;
	int pos;
	enum vinyl_order cmp;
	void *key;
	int keysize;
	struct key_def *key_def;
};

static inline int
sd_indexiter_route(struct sdindexiter *i)
{
	int begin = 0;
	int end = i->index->h->count - 1;
	while (begin != end) {
		int mid = begin + (end - begin) / 2;
		struct sdindexpage *page = sd_indexpage(i->index, mid);
		int rc = sf_compare(i->key_def,
		                    sd_indexpage_max(i->index, page),
		                    i->key);
		if (rc < 0) {
			begin = mid + 1;
		} else {
			/* rc >= 0 */
			end = mid;
		}
	}
	if (unlikely(end >= (int)i->index->h->count))
		end = i->index->h->count - 1;
	return end;
}

static inline int
sd_indexiter_open(struct sdindexiter *ii, struct key_def *key_def,
		  struct sdindex *index, enum vinyl_order o, void *key,
		  int keysize)
{
	ii->key_def = key_def;
	ii->index   = index;
	ii->cmp     = o;
	ii->key     = key;
	ii->keysize = keysize;
	ii->v       = NULL;
	ii->pos     = 0;
	if (unlikely(ii->index->h->count == 1)) {
		/* skip bootstrap node  */
		if (ii->index->h->lsnmin == UINT64_MAX &&
		    ii->index->h->lsnmax == 0)
			return 0;
	}
	if (ii->key == NULL) {
		switch (ii->cmp) {
		case VINYL_LT:
		case VINYL_LE: ii->pos = ii->index->h->count - 1;
			break;
		case VINYL_GT:
		case VINYL_GE: ii->pos = 0;
			break;
		default:
			unreachable();
		}
		ii->v = sd_indexpage(ii->index, ii->pos);
		return 0;
	}
	if (likely(ii->index->h->count > 1))
		ii->pos = sd_indexiter_route(ii);

	struct sdindexpage *p = sd_indexpage(ii->index, ii->pos);
	int rc;
	switch (ii->cmp) {
	case VINYL_LE:
	case VINYL_LT:
		rc = sf_compare(ii->key_def, sd_indexpage_min(ii->index, p),
		                ii->key);
		if (rc > 0 || (rc == 0 && ii->cmp == VINYL_LT))
			ii->pos--;
		break;
	case VINYL_GE:
	case VINYL_GT:
		rc = sf_compare(ii->key_def, sd_indexpage_max(ii->index, p),
				ii->key);
		if (rc < 0 || (rc == 0 && ii->cmp == VINYL_GT))
			ii->pos++;
		break;
	default: unreachable();
	}
	if (unlikely(ii->pos == -1 ||
	               ii->pos >= (int)ii->index->h->count))
		return 0;
	ii->v = sd_indexpage(ii->index, ii->pos);
	return 0;
}

static inline void*
sd_indexiter_get(struct sdindexiter *ii)
{
	return ii->v;
}

static inline void
sd_indexiter_next(struct sdindexiter *ii)
{
	switch (ii->cmp) {
	case VINYL_LT:
	case VINYL_LE: ii->pos--;
		break;
	case VINYL_GT:
	case VINYL_GE: ii->pos++;
		break;
	default:
		unreachable();
		break;
	}
	if (unlikely(ii->pos < 0))
		ii->v = NULL;
	else
	if (unlikely(ii->pos >= (int)ii->index->h->count))
		ii->v = NULL;
	else
		ii->v = sd_indexpage(ii->index, ii->pos);
}

#define SD_SEALED 1

struct PACKED sdseal {
	uint32_t  crc;
	struct srversion version;
	uint8_t   flags;
	uint32_t  index_crc;
	uint64_t  index_offset;
};

static inline void
sd_sealset_open(struct sdseal *s)
{
	sr_version_storage(&s->version);
	s->flags = 0;
	s->index_crc = 0;
	s->index_offset = 0;
	s->crc = ss_crcs(s, sizeof(struct sdseal), 0);
}

static inline void
sd_sealset_close(struct sdseal *s, struct sdindexheader *h)
{
	sr_version_storage(&s->version);
	s->flags = SD_SEALED;
	s->index_crc = h->crc;
	s->index_offset = h->offset;
	s->crc = ss_crcs(s, sizeof(struct sdseal), 0);
}

static inline int
sd_sealvalidate(struct sdseal *s, struct sdindexheader *h)
{
	uint32_t crc = ss_crcs(s, sizeof(struct sdseal), 0);
	if (unlikely(s->crc != crc))
		return -1;
	if (unlikely(h->crc != s->index_crc))
		return -1;
	if (unlikely(h->offset != s->index_offset))
		return -1;
	if (unlikely(! sr_versionstorage_check(&s->version)))
		return -1;
	if (unlikely(s->flags != SD_SEALED))
		return -1;
	return 0;
}

struct sdcbuf {
	struct ssbuf a; /* decompression */
	struct ssbuf b; /* transformation */
	struct sdindexiter index_iter;
	struct sdpageiter page_iter;
	struct sdcbuf *next;
};

struct sdc {
	struct sdbuild build;
	struct svupsert upsert;
	struct ssbuf a;        /* result */
	struct ssbuf b;        /* redistribute buffer */
	struct ssbuf c;        /* file buffer */
	struct ssbuf d;        /* page read buffer */
	struct sdcbuf *head;   /* compression buffer list */
	int count;
};

struct vinyl_service {
	struct vinyl_env *env;
	struct sdc sdc;
};

static inline void
sd_cinit(struct sdc *sc)
{
	sv_upsertinit(&sc->upsert);
	sd_buildinit(&sc->build);
	ss_bufinit(&sc->a);
	ss_bufinit(&sc->b);
	ss_bufinit(&sc->c);
	ss_bufinit(&sc->d);
	sc->count = 0;
	sc->head = NULL;
}

static inline void
sd_cfree(struct sdc *sc)
{
	sd_buildfree(&sc->build);
	sv_upsertfree(&sc->upsert);
	ss_buffree(&sc->a);
	ss_buffree(&sc->b);
	ss_buffree(&sc->c);
	ss_buffree(&sc->d);
	struct sdcbuf *b = sc->head;
	struct sdcbuf *next;
	while (b) {
		next = b->next;
		ss_buffree(&b->a);
		ss_buffree(&b->b);
		free(b);
		b = next;
	}
}

static inline void
sd_cgc(struct sdc *sc, int wm)
{
	sd_buildgc(&sc->build, wm);
	sv_upsertgc(&sc->upsert, 600, 512);
	ss_bufgc(&sc->a, wm);
	ss_bufgc(&sc->b, wm);
	ss_bufgc(&sc->c, wm);
	ss_bufgc(&sc->d, wm);
	struct sdcbuf *it = sc->head;
	while (it) {
		ss_bufgc(&it->a, wm);
		ss_bufgc(&it->b, wm);
		it = it->next;
	}
}

static inline int
sd_censure(struct sdc *c, int count)
{
	if (c->count < count) {
		while (count-- >= 0) {
			struct sdcbuf *buf =
				malloc(sizeof(struct sdcbuf));
			if (buf == NULL)
				return -1;
			ss_bufinit(&buf->a);
			ss_bufinit(&buf->b);
			buf->next = c->head;
			c->head = buf;
			c->count++;
		}
	}
	return 0;
}

struct sdmergeconf {
	uint32_t    write;
	uint32_t    stream;
	uint64_t    size_stream;
	uint64_t    size_node;
	uint32_t    size_page;
	uint32_t    checksum;
	uint32_t    compression;
	struct ssfilterif *compression_if;
	uint64_t    vlsn;
	uint64_t    vlsn_lru;
	uint32_t    save_delete;
	uint32_t    save_upsert;
};

struct sdmerge {
	struct sdindex     index;
	struct svmergeiter *merge;
	struct svwriteiter i;
	struct sdmergeconf *conf;
	struct sdbuild     *build;
	uint64_t    processed;
	uint64_t    current;
	uint64_t    limit;
	int         resume;
};

static int
sd_mergeinit(struct sdmerge*, struct svmergeiter*, struct sdbuild*,
	     struct svupsert*, struct sdmergeconf*);
static int sd_mergefree(struct sdmerge*);
static int sd_merge(struct sdmerge*);
static int sd_mergepage(struct sdmerge*, uint64_t);
static int sd_mergecommit(struct sdmerge*, struct sdid*, uint64_t);

struct sdreadarg {
	struct sdindex    *index;
	struct ssbuf      *buf;
	struct ssbuf      *buf_xf;
	struct ssbuf      *buf_read;
	struct sdindexiter *index_iter;
	struct sdpageiter *page_iter;
	struct ssfile     *file;
	enum vinyl_order     o;
	int         has;
	uint64_t    has_vlsn;
	int         use_compression;
	struct ssfilterif *compression_if;
	struct ssa *a;
	struct key_def *key_def;
};

struct PACKED sdread {
	struct sdreadarg ra;
	struct sdindexpage *ref;
	struct sdpage page;
	int reads;
};

static inline int
sd_read_page(struct sdread *i, struct sdindexpage *ref)
{
	struct sdreadarg *arg = &i->ra;

	ss_bufreset(arg->buf);
	int rc = ss_bufensure(arg->buf, ref->sizeorigin);
	if (unlikely(rc == -1))
		return sr_oom();
	ss_bufreset(arg->buf_xf);
	rc = ss_bufensure(arg->buf_xf, arg->index->h->sizevmax);
	if (unlikely(rc == -1))
		return sr_oom();

	i->reads++;

	/* compression */
	if (arg->use_compression)
	{
		char *page_pointer;
		ss_bufreset(arg->buf_read);
		rc = ss_bufensure(arg->buf_read, ref->size);
		if (unlikely(rc == -1))
			return sr_oom();
		rc = ss_filepread(arg->file, ref->offset, arg->buf_read->s, ref->size);
		if (unlikely(rc == -1)) {
			sr_error("index file '%s' read error: %s",
				 ss_pathof(&arg->file->path),
				 strerror(errno));
			return -1;
		}
		ss_bufadvance(arg->buf_read, ref->size);
		page_pointer = arg->buf_read->s;

		/* copy header */
		memcpy(arg->buf->p, page_pointer, sizeof(struct sdpageheader));
		ss_bufadvance(arg->buf, sizeof(struct sdpageheader));

		/* decompression */
		struct ssfilter f;
		rc = ss_filterinit(&f, arg->compression_if, SS_FOUTPUT);
		if (unlikely(rc == -1)) {
			sr_error("index file '%s' decompression error",
			         ss_pathof(&arg->file->path));
			return -1;
		}
		int size = ref->size - sizeof(struct sdpageheader);
		rc = ss_filternext(&f, arg->buf, page_pointer + sizeof(struct sdpageheader), size);
		if (unlikely(rc == -1)) {
			sr_error("index file '%s' decompression error",
			         ss_pathof(&arg->file->path));
			return -1;
		}
		ss_filterfree(&f);
		sd_pageinit(&i->page, (struct sdpageheader*)arg->buf->s);
		return 0;
	}

	/* default */
	rc = ss_filepread(arg->file, ref->offset, arg->buf->s, ref->sizeorigin);
	if (unlikely(rc == -1)) {
		sr_error("index file '%s' read error: %s",
		         ss_pathof(&arg->file->path),
		         strerror(errno));
		return -1;
	}
	ss_bufadvance(arg->buf, ref->sizeorigin);
	sd_pageinit(&i->page, (struct sdpageheader*)(arg->buf->s));
	return 0;
}

static inline int
sd_read_openpage(struct sdread *i, void *key, int keysize)
{
	struct sdreadarg *arg = &i->ra;
	assert(i->ref != NULL);
	int rc = sd_read_page(i, i->ref);
	if (unlikely(rc == -1))
		return -1;
	return sd_pageiter_open(arg->page_iter, arg->key_def,
				arg->buf_xf,
				&i->page, arg->o, key, keysize);
}

static inline void
sd_read_next(struct ssiter*);

static struct ssiterif sd_readif;

static inline int
sd_read_open(struct ssiter *iptr, struct sdreadarg *arg, void *key, int keysize)
{
	iptr->vif = &sd_readif;
	struct sdread *i = (struct sdread*)iptr->priv;
	i->reads = 0;
	i->ra = *arg;
	sd_indexiter_open(arg->index_iter, arg->key_def, arg->index,
			  arg->o, key, keysize);
	i->ref = sd_indexiter_get(arg->index_iter);
	if (i->ref == NULL)
		return 0;
	if (arg->has) {
		assert(arg->o == VINYL_GE);
		if (likely(i->ref->lsnmax <= arg->has_vlsn)) {
			i->ref = NULL;
			return 0;
		}
	}
	int rc = sd_read_openpage(i, key, keysize);
	if (unlikely(rc == -1)) {
		i->ref = NULL;
		return -1;
	}
	if (unlikely(! sd_pageiter_has(i->ra.page_iter))) {
		sd_read_next(iptr);
		rc = 0;
	}
	return rc;
}

static inline void
sd_read_close(struct ssiter *iptr)
{
	struct sdread *i = (struct sdread*)iptr->priv;
	i->ref = NULL;
}

static inline int
sd_read_has(struct ssiter *iptr)
{
	struct sdread *i = (struct sdread*)iptr->priv;
	if (unlikely(i->ref == NULL))
		return 0;
	return sd_pageiter_has(i->ra.page_iter);
}

static inline void*
sd_read_get(struct ssiter *iptr)
{
	struct sdread *i = (struct sdread*)iptr->priv;
	if (unlikely(i->ref == NULL))
		return NULL;
	return sd_pageiter_get(i->ra.page_iter);
}

static inline void
sd_read_next(struct ssiter *iptr)
{
	struct sdread *i = (struct sdread*)iptr->priv;
	if (unlikely(i->ref == NULL))
		return;
	sd_pageiter_next(i->ra.page_iter);
retry:
	if (likely(sd_pageiter_has(i->ra.page_iter)))
		return;
	sd_indexiter_next(i->ra.index_iter);
	i->ref = sd_indexiter_get(i->ra.index_iter);
	if (i->ref == NULL)
		return;
	int rc = sd_read_openpage(i, NULL, 0);
	if (unlikely(rc == -1)) {
		i->ref = NULL;
		return;
	}
	goto retry;
}

static inline int
sd_read_stat(struct ssiter *iptr)
{
	struct sdread *i = (struct sdread*)iptr->priv;
	return i->reads;
}

static int sd_writeseal(struct ssfile*);
static int sd_writepage(struct ssfile*, struct sdbuild*);
static int sd_writeindex(struct ssfile*, struct sdindex*);
static int sd_seal(struct ssfile*, struct sdindex*, uint64_t);

static void sd_buildinit(struct sdbuild *b)
{
	ss_bufinit(&b->list);
	ss_bufinit(&b->m);
	ss_bufinit(&b->v);
	ss_bufinit(&b->c);
	b->n = 0;
	b->compress = 0;
	b->compress_if = NULL;
	b->crc = 0;
	b->vmax = 0;
}

static void sd_buildfree(struct sdbuild *b)
{
	ss_buffree(&b->list);
	ss_buffree(&b->m);
	ss_buffree(&b->v);
	ss_buffree(&b->c);
}

static void sd_buildreset(struct sdbuild *b)
{
	ss_bufreset(&b->list);
	ss_bufreset(&b->m);
	ss_bufreset(&b->v);
	ss_bufreset(&b->c);
	b->n = 0;
	b->vmax = 0;
}

static void sd_buildgc(struct sdbuild *b, int wm)
{
	ss_bufgc(&b->list, wm);
	ss_bufgc(&b->m, wm);
	ss_bufgc(&b->v, wm);
	ss_bufgc(&b->c, wm);
	b->n = 0;
	b->vmax = 0;
}

static int
sd_buildbegin(struct sdbuild *b, struct key_def *key_def,
	      int crc, int compress, struct ssfilterif *compress_if)
{
	b->key_def = key_def;
	b->crc = crc;
	b->compress = compress;
	b->compress_if = compress_if;
	int rc;
	rc = ss_bufensure(&b->list, sizeof(struct sdbuildref));
	if (unlikely(rc == -1))
		return sr_oom();
	struct sdbuildref *ref =
		(struct sdbuildref*)ss_bufat(&b->list, sizeof(struct sdbuildref), b->n);
	ref->m     = ss_bufused(&b->m);
	ref->msize = 0;
	ref->v     = ss_bufused(&b->v);
	ref->vsize = 0;
	ref->c     = ss_bufused(&b->c);
	ref->csize = 0;
	rc = ss_bufensure(&b->m, sizeof(struct sdpageheader));
	if (unlikely(rc == -1))
		return sr_oom();
	struct sdpageheader *h = sd_buildheader(b);
	memset(h, 0, sizeof(*h));
	h->lsnmin    = UINT64_MAX;
	h->lsnmindup = UINT64_MAX;
	h->reserve   = 0;
	ss_bufadvance(&b->list, sizeof(struct sdbuildref));
	ss_bufadvance(&b->m, sizeof(struct sdpageheader));
	return 0;
}

static inline int
sd_buildadd_raw(struct sdbuild *b, struct sv *v, uint32_t size)
{
	int rc = ss_bufensure(&b->v, size);
	if (unlikely(rc == -1))
		return sr_oom();
	memcpy(b->v.p, sv_pointer(v), size);
	ss_bufadvance(&b->v, size);
	return 0;
}

int sd_buildadd(struct sdbuild *b, struct sv *v, uint32_t flags)
{
	/* prepare document metadata */
	int rc = ss_bufensure(&b->m, sizeof(struct sdv));
	if (unlikely(rc == -1))
		return sr_oom();
	uint64_t lsn = sv_lsn(v);
	uint32_t size = sv_size(v);
	struct sdpageheader *h = sd_buildheader(b);
	struct sdv *sv = (struct sdv*)b->m.p;
	sv->flags = flags;
	sv->offset = ss_bufused(&b->v) - sd_buildref(b)->v;
	sv->size = size;
	sv->lsn = lsn;
	ss_bufadvance(&b->m, sizeof(struct sdv));
	/* copy document */
	rc = sd_buildadd_raw(b, v, size);
	if (unlikely(rc == -1))
		return -1;
	/* update page header */
	h->count++;
	size += sizeof(struct sdv) + size;
	if (size > b->vmax)
		b->vmax = size;
	if (lsn > h->lsnmax)
		h->lsnmax = lsn;
	if (lsn < h->lsnmin)
		h->lsnmin = lsn;
	if (sv->flags & SVDUP) {
		h->countdup++;
		if (lsn < h->lsnmindup)
			h->lsnmindup = lsn;
	}
	return 0;
}

static inline int
sd_buildcompress(struct sdbuild *b)
{
	assert(b->compress_if != &ss_nonefilter);
	/* reserve header */
	int rc = ss_bufensure(&b->c, sizeof(struct sdpageheader));
	if (unlikely(rc == -1))
		return -1;
	ss_bufadvance(&b->c, sizeof(struct sdpageheader));
	/* compression (including meta-data) */
	struct sdbuildref *ref = sd_buildref(b);
	struct ssfilter f;
	rc = ss_filterinit(&f, b->compress_if, SS_FINPUT);
	if (unlikely(rc == -1))
		return -1;
	rc = ss_filterstart(&f, &b->c);
	if (unlikely(rc == -1))
		goto error;
	rc = ss_filternext(&f, &b->c, b->m.s + ref->m + sizeof(struct sdpageheader),
	                   ref->msize - sizeof(struct sdpageheader));
	if (unlikely(rc == -1))
		goto error;
	rc = ss_filternext(&f, &b->c, b->v.s + ref->v, ref->vsize);
	if (unlikely(rc == -1))
		goto error;
	rc = ss_filtercomplete(&f, &b->c);
	if (unlikely(rc == -1))
		goto error;
	ss_filterfree(&f);
	return 0;
error:
	ss_filterfree(&f);
	return -1;
}

static int sd_buildend(struct sdbuild *b)
{
	/* update sizes */
	struct sdbuildref *ref = sd_buildref(b);
	ref->msize = ss_bufused(&b->m) - ref->m;
	ref->vsize = ss_bufused(&b->v) - ref->v;
	ref->csize = 0;
	/* calculate data crc (non-compressed) */
	struct sdpageheader *h = sd_buildheader(b);
	uint32_t crc = 0;
	if (likely(b->crc)) {
		crc = ss_crcp(b->m.s + ref->m, ref->msize, 0);
		crc = ss_crcp(b->v.s + ref->v, ref->vsize, crc);
	}
	h->crcdata = crc;
	/* compression */
	if (b->compress) {
		int rc = sd_buildcompress(b);
		if (unlikely(rc == -1))
			return -1;
		ref->csize = ss_bufused(&b->c) - ref->c;
	}
	/* update page header */
	int total = ref->msize + ref->vsize;
	h->sizekeys = 0;
	h->sizeorigin = total - sizeof(struct sdpageheader);
	h->size = h->sizeorigin;
	if (b->compress)
		h->size = ref->csize - sizeof(struct sdpageheader);
	else
		h->size = h->sizeorigin;
	h->crc = ss_crcs(h, sizeof(struct sdpageheader), 0);
	if (b->compress)
		memcpy(b->c.s + ref->c, h, sizeof(struct sdpageheader));
	return 0;
}

static int sd_buildcommit(struct sdbuild *b)
{
	if (b->compress) {
		ss_bufreset(&b->m);
		ss_bufreset(&b->v);
	}
	b->n++;
	return 0;
}

static int sd_indexbegin(struct sdindex *i)
{
	int rc = ss_bufensure(&i->i, sizeof(struct sdindexheader));
	if (unlikely(rc == -1))
		return sr_oom();
	struct sdindexheader *h = sd_indexheader(i);
	sr_version_storage(&h->version);
	h->crc         = 0;
	h->size        = 0;
	h->sizevmax    = 0;
	h->count       = 0;
	h->keys        = 0;
	h->total       = 0;
	h->totalorigin = 0;
	h->extension   = 0;
	h->extensions  = 0;
	h->lsnmin      = UINT64_MAX;
	h->lsnmax      = 0;
	h->offset      = 0;
	h->dupkeys     = 0;
	h->dupmin      = UINT64_MAX;
	memset(h->reserve, 0, sizeof(h->reserve));
	sd_idinit(&h->id, 0, 0, 0);
	i->h = NULL;
	ss_bufadvance(&i->i, sizeof(struct sdindexheader));
	return 0;
}

static int
sd_indexcommit(struct sdindex *i, struct sdid *id,
	       uint64_t offset)
{
	int size = ss_bufused(&i->v);
	int size_extension = 0;
	int extensions = 0;
	int rc = ss_bufensure(&i->i, size + size_extension);
	if (unlikely(rc == -1))
		return sr_oom();
	memcpy(i->i.p, i->v.s, size);
	ss_bufadvance(&i->i, size);
	ss_buffree(&i->v);
	i->h = sd_indexheader(i);
	i->h->offset     = offset;
	i->h->id         = *id;
	i->h->extension  = size_extension;
	i->h->extensions = extensions;
	i->h->crc = ss_crcs(i->h, sizeof(struct sdindexheader), 0);
	return 0;
}

static inline int
sd_indexadd_raw(struct sdindex *i, struct sdbuild *build,
		struct sdindexpage *p, char *min, char *max)
{
	/* reformat document to exclude non-key fields */
	p->sizemin = sf_comparable_size(build->key_def, min);
	p->sizemax = sf_comparable_size(build->key_def, max);
	int rc = ss_bufensure(&i->v, p->sizemin + p->sizemax);
	if (unlikely(rc == -1))
		return sr_oom();
	sf_comparable_write(build->key_def, min, i->v.p);
	ss_bufadvance(&i->v, p->sizemin);
	sf_comparable_write(build->key_def, max, i->v.p);
	ss_bufadvance(&i->v, p->sizemax);
	return 0;
}

static int
sd_indexadd(struct sdindex *i, struct sdbuild *build, uint64_t offset)
{
	int rc = ss_bufensure(&i->i, sizeof(struct sdindexpage));
	if (unlikely(rc == -1))
		return sr_oom();
	struct sdpageheader *ph = sd_buildheader(build);

	int size = ph->size + sizeof(struct sdpageheader);
	int sizeorigin = ph->sizeorigin + sizeof(struct sdpageheader);

	/* prepare page header */
	struct sdindexpage *p = (struct sdindexpage*)i->i.p;
	p->offset      = offset;
	p->offsetindex = ss_bufused(&i->v);
	p->lsnmin      = ph->lsnmin;
	p->lsnmax      = ph->lsnmax;
	p->size        = size;
	p->sizeorigin  = sizeorigin;
	p->sizemin     = 0;
	p->sizemax     = 0;

	/* copy keys */
	if (unlikely(ph->count > 0))
	{
		char *min = sd_buildminkey(build);
		char *max = sd_buildmaxkey(build);
		rc = sd_indexadd_raw(i, build, p, min, max);
		if (unlikely(rc == -1))
			return -1;
	}

	/* update index info */
	struct sdindexheader *h = sd_indexheader(i);
	h->count++;
	h->size  += sizeof(struct sdindexpage) + p->sizemin + p->sizemax;
	h->keys  += ph->count;
	h->total += size;
	h->totalorigin += sizeorigin;
	if (build->vmax > h->sizevmax)
		h->sizevmax = build->vmax;
	if (ph->lsnmin < h->lsnmin)
		h->lsnmin = ph->lsnmin;
	if (ph->lsnmax > h->lsnmax)
		h->lsnmax = ph->lsnmax;
	h->dupkeys += ph->countdup;
	if (ph->lsnmindup < h->dupmin)
		h->dupmin = ph->lsnmindup;
	ss_bufadvance(&i->i, sizeof(struct sdindexpage));
	return 0;
}

static int sd_indexcopy(struct sdindex *i, struct sdindexheader *h)
{
	int size = sd_indexsize_ext(h);
	int rc = ss_bufensure(&i->i, size);
	if (unlikely(rc == -1))
		return sr_oom();
	memcpy(i->i.s, (char*)h, size);
	ss_bufadvance(&i->i, size);
	i->h = sd_indexheader(i);
	return 0;
}

static int
sd_mergeinit(struct sdmerge *m, struct svmergeiter *im,
	     struct sdbuild *build,
	     struct svupsert *upsert, struct sdmergeconf *conf)
{
	m->conf      = conf;
	m->build     = build;
	m->merge     = im;
	m->processed = 0;
	m->current   = 0;
	m->limit     = 0;
	m->resume    = 0;
	sd_indexinit(&m->index);
	sv_writeiter_open(&m->i, im, upsert,
			  (uint64_t)conf->size_page, sizeof(struct sdv),
			  conf->vlsn,
			  conf->vlsn_lru,
			  conf->save_delete,
			  conf->save_upsert);
	return 0;
}

static int sd_mergefree(struct sdmerge *m)
{
	sd_indexfree(&m->index);
	return 0;
}

static inline int
sd_mergehas(struct sdmerge *m)
{
	if (! sv_writeiter_has(&m->i))
		return 0;
	if (m->current > m->limit)
		return 0;
	return 1;
}

static int sd_merge(struct sdmerge *m)
{
	if (unlikely(! sv_writeiter_has(&m->i)))
		return 0;
	struct sdmergeconf *conf = m->conf;
	sd_indexinit(&m->index);
	int rc = sd_indexbegin(&m->index);
	if (unlikely(rc == -1))
		return -1;
	m->current = 0;
	m->limit   = 0;
	uint64_t processed = m->processed;
	uint64_t left = (conf->size_stream - processed);
	if (left >= (conf->size_node * 2)) {
		m->limit = conf->size_node;
	} else
	if (left > (conf->size_node)) {
		m->limit = conf->size_node * 2;
	} else {
		m->limit = UINT64_MAX;
	}
	return sd_mergehas(m);
}

static int sd_mergepage(struct sdmerge *m, uint64_t offset)
{
	struct sdmergeconf *conf = m->conf;
	sd_buildreset(m->build);
	if (m->resume) {
		m->resume = 0;
		if (unlikely(! sv_writeiter_resume(&m->i)))
			return 0;
	}
	if (! sd_mergehas(m))
		return 0;
	int rc;
	rc = sd_buildbegin(m->build, m->merge->merge->key_def,
			   conf->checksum, conf->compression, conf->compression_if);
	if (unlikely(rc == -1))
		return -1;
	while (sv_writeiter_has(&m->i))
	{
		struct sv *v = sv_writeiter_get(&m->i);
		uint8_t flags = sv_flags(v);
		if (sv_writeiter_is_duplicate(&m->i))
			flags |= SVDUP;
		rc = sd_buildadd(m->build, v, flags);
		if (unlikely(rc == -1))
			return -1;
		sv_writeiter_next(&m->i);
	}
	rc = sd_buildend(m->build);
	if (unlikely(rc == -1))
		return -1;
	rc = sd_indexadd(&m->index, m->build, offset);
	if (unlikely(rc == -1))
		return -1;
	m->current = sd_indextotal(&m->index);
	m->resume  = 1;
	return 1;
}

static int sd_mergecommit(struct sdmerge *m, struct sdid *id, uint64_t offset)
{
	m->processed += sd_indextotal(&m->index);
	return sd_indexcommit(&m->index, id, offset);
}

static struct ssiterif sd_readif =
{
	.close = sd_read_close,
	.has   = sd_read_has,
	.get   = sd_read_get,
	.next  = sd_read_next
};

struct PACKED sdrecover {
	struct ssfile *file;
	int corrupt;
	struct sdindexheader *v;
	struct sdindexheader *actual;
	struct sdseal *seal;
	struct ssmmap map;
	struct runtime *r;
};

static int
sd_recover_next_of(struct sdrecover *i, struct sdseal *next)
{
	if (next == NULL)
		return 0;

	char *eof = i->map.p + i->map.size;
	char *pointer = (char*)next;

	/* eof */
	if (unlikely(pointer == eof)) {
		i->v = NULL;
		return 0;
	}

	/* validate seal pointer */
	if (unlikely(((pointer + sizeof(struct sdseal)) > eof))) {
		sr_malfunction("corrupted index file '%s': bad seal size",
		               ss_pathof(&i->file->path));
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}
	pointer = i->map.p + next->index_offset;

	/* validate index pointer */
	if (unlikely(((pointer + sizeof(struct sdindexheader)) > eof))) {
		sr_malfunction("corrupted index file '%s': bad index size",
		               ss_pathof(&i->file->path));
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}
	struct sdindexheader *index = (struct sdindexheader*)(pointer);

	/* validate index crc */
	uint32_t crc = ss_crcs(index, sizeof(struct sdindexheader), 0);
	if (index->crc != crc) {
		sr_malfunction("corrupted index file '%s': bad index crc",
		               ss_pathof(&i->file->path));
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}

	/* validate index size */
	char *end = pointer + sizeof(struct sdindexheader) + index->size +
	            index->extension;
	if (unlikely(end > eof)) {
		sr_malfunction("corrupted index file '%s': bad index size",
		               ss_pathof(&i->file->path));
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}

	/* validate seal */
	int rc = sd_sealvalidate(next, index);
	if (unlikely(rc == -1)) {
		sr_malfunction("corrupted index file '%s': bad seal",
		               ss_pathof(&i->file->path));
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}
	i->seal = next;
	i->actual = index;
	i->v = index;
	return 1;
}

static int sd_recover_open(struct sdrecover *ri, struct runtime *r,
			   struct ssfile *file)
{
	memset(ri, 0, sizeof(*ri));
	ri->r = r;
	ri->file = file;
	if (unlikely(ri->file->size < (sizeof(struct sdseal) + sizeof(struct sdindexheader)))) {
		sr_malfunction("corrupted index file '%s': bad size",
		               ss_pathof(&ri->file->path));
		ri->corrupt = 1;
		return -1;
	}
	int rc = ss_mmap(&ri->map, ri->file->fd, ri->file->size, 1);
	if (unlikely(rc == -1)) {
		sr_malfunction("failed to mmap index file '%s': %s",
		               ss_pathof(&ri->file->path),
		               strerror(errno));
		return -1;
	}
	struct sdseal *seal = (struct sdseal*)((char*)ri->map.p);
	rc = sd_recover_next_of(ri, seal);
	if (unlikely(rc == -1))
		ss_munmap(&ri->map);
	return rc;
}

static void
sd_recover_close(struct sdrecover *ri)
{
	ss_munmap(&ri->map);
}

static int
sd_recover_has(struct sdrecover *ri)
{
	return ri->v != NULL;
}

static void*
sd_recover_get(struct sdrecover *ri)
{
	return ri->v;
}

static void
sd_recover_next(struct sdrecover *ri)
{
	if (unlikely(ri->v == NULL))
		return;
	struct sdseal *next =
		(struct sdseal*)((char*)ri->v +
		    (sizeof(struct sdindexheader) + ri->v->size) +
		     ri->v->extension);
	sd_recover_next_of(ri, next);
}

static int sd_recover_complete(struct sdrecover *ri)
{
	if (unlikely(ri->seal == NULL))
		return -1;
	if (likely(ri->corrupt == 0))
		return  0;
	/* truncate file to the end of a latest actual
	 * index */
	char *eof =
		(char*)ri->map.p +
		       ri->actual->offset + sizeof(struct sdindexheader) +
		       ri->actual->size +
		       ri->actual->extension;
	uint64_t file_size = eof - ri->map.p;
	int rc = ss_fileresize(ri->file, file_size);
	if (unlikely(rc == -1))
		return -1;
	diag_clear(diag_get());
	return 0;
}

static uint8_t
sd_vifflags(struct sv *v)
{
	return ((struct sdv*)v->v)->flags;
}

static uint64_t
sd_viflsn(struct sv *v)
{
	return ((struct sdv*)v->v)->lsn;
}

static char*
sd_vifpointer(struct sv *v)
{
	struct sdpage p = {
		.h = (struct sdpageheader*)v->arg
	};
	return sd_pagepointer(&p, (struct sdv*)v->v);
}

static uint32_t
sd_vifsize(struct sv *v)
{
	return ((struct sdv*)v->v)->size;
}

static struct svif sd_vif =
{
	.flags     = sd_vifflags,
	.lsn       = sd_viflsn,
	.lsnset    = NULL,
	.pointer   = sd_vifpointer,
	.size      = sd_vifsize
};

static int
sd_writeseal(struct ssfile *file)
{
	struct sdseal seal;
	sd_sealset_open(&seal);
	SS_INJECTION(r->i, SS_INJECTION_SD_BUILD_1,
	             seal.crc++); /* corrupt seal */
	int rc;
	rc = ss_filewrite(file, &seal, sizeof(seal));
	if (unlikely(rc == -1)) {
		sr_malfunction("file '%s' write error: %s",
		               ss_pathof(&file->path),
		               strerror(errno));
		return -1;
	}
	return 0;
}

static int
sd_writepage(struct ssfile *file, struct sdbuild *b)
{
	SS_INJECTION(r->i, SS_INJECTION_SD_BUILD_0,
	             sr_malfunction("%s", "error injection");
	             return -1);
	struct sdbuildref *ref = sd_buildref(b);
	struct iovec iovv[3];
	struct ssiov iov;
	ss_iovinit(&iov, iovv, 3);
	if (ss_bufused(&b->c) > 0) {
		/* compressed */
		ss_iovadd(&iov, b->c.s, ref->csize);
	} else {
		/* uncompressed */
		ss_iovadd(&iov, b->m.s + ref->m, ref->msize);
		ss_iovadd(&iov, b->v.s + ref->v, ref->vsize);
	}
	int rc;
	rc = ss_filewritev(file, &iov);
	if (unlikely(rc == -1)) {
		sr_malfunction("file '%s' write error: %s",
		               ss_pathof(&file->path),
		               strerror(errno));
		return -1;
	}
	return 0;
}

static int
sd_writeindex(struct ssfile *file, struct sdindex *index)
{
	int rc;
	rc = ss_filewrite(file, index->i.s, ss_bufused(&index->i));
	if (unlikely(rc == -1)) {
		sr_malfunction("file '%s' write error: %s",
		               ss_pathof(&file->path),
		               strerror(errno));
		return -1;
	}
	return 0;
}

static int
sd_seal(struct ssfile *file, struct sdindex *index,
	uint64_t offset)
{
	struct sdseal seal;
	sd_sealset_close(&seal, index->h);
	int rc;
	rc = ss_filepwrite(file, offset, &seal, sizeof(seal));
	if (unlikely(rc == -1)) {
		sr_malfunction("file '%s' write error: %s",
		               ss_pathof(&file->path),
		               strerror(errno));
		return -1;
	}
	return 0;
}

struct siconf {
	uint32_t    id;
	char       *name;
	char       *path;
	uint32_t    sync;
	uint64_t    node_size;
	uint32_t    node_page_size;
	uint32_t    node_page_checksum;
	uint32_t    compression;
	char       *compression_sz;
	struct ssfilterif *compression_if;
	uint32_t    temperature;
	uint64_t    lru;
	uint32_t    lru_step;
	uint32_t    buf_gc_wm;
	struct srversion   version;
	struct srversion   version_storage;
};

static void si_confinit(struct siconf*);
static void si_conffree(struct siconf*);

struct PACKED sibranch {
	struct sdid id;
	struct sdindex index;
	struct sibranch *link;
	struct sibranch *next;
};

static inline void
si_branchinit(struct sibranch *b)
{
	memset(&b->id, 0, sizeof(b->id));
	sd_indexinit(&b->index);
	b->link = NULL;
	b->next = NULL;
}

static inline struct sibranch*
si_branchnew()
{
	struct sibranch *b = (struct sibranch*)malloc(sizeof(struct sibranch));
	if (unlikely(b == NULL)) {
		sr_oom();
		return NULL;
	}
	si_branchinit(b);
	return b;
}

static inline void
si_branchset(struct sibranch *b, struct sdindex *i)
{
	b->id = i->h->id;
	b->index = *i;
}

static inline void
si_branchfree(struct sibranch *b)
{
	sd_indexfree(&b->index);
	free(b);
}

#define SI_NONE       0
#define SI_LOCK       1
#define SI_ROTATE     2
#define SI_SPLIT      4
#define SI_PROMOTE    8
#define SI_REVOKE     16
#define SI_RDB        32
#define SI_RDB_DBI    64
#define SI_RDB_DBSEAL 128
#define SI_RDB_UNDEF  256
#define SI_RDB_REMOVE 512

struct PACKED sinode {
	uint32_t   recover;
	uint16_t   flags;
	uint64_t   update_time;
	uint32_t   used;
	uint64_t   lru;
	uint64_t   ac;
	struct sibranch   self;
	struct sibranch  *branch;
	uint32_t   branch_count;
	uint32_t   temperature;
	uint64_t   temperature_reads;
	uint16_t   refs;
	pthread_mutex_t reflock;
	struct svindex    i0, i1;
	struct ssfile     file;
	rb_node(struct sinode) tree_node;
	struct ssrqnode   nodecompact;
	struct ssrqnode   nodebranch;
	struct ssrqnode   nodetemp;
	struct rlist     gc;
	struct rlist     commit;
};

static struct sinode *si_nodenew(struct key_def *key_def);
static int
si_nodeopen(struct sinode*, struct runtime*, struct sspath*);
static int
si_nodecreate(struct sinode*, struct siconf*, struct sdid*);
static int si_nodefree(struct sinode*, struct runtime*, int);
static int si_nodegc_index(struct runtime*, struct svindex*);
static int si_nodegc(struct sinode*, struct siconf*);
static int si_nodeseal(struct sinode*, struct siconf*);
static int si_nodecomplete(struct sinode*, struct siconf*);

static inline void
si_nodelock(struct sinode *node) {
	assert(! (node->flags & SI_LOCK));
	node->flags |= SI_LOCK;
}

static inline void
si_nodeunlock(struct sinode *node) {
	assert((node->flags & SI_LOCK) > 0);
	node->flags &= ~SI_LOCK;
}

static inline void
si_nodesplit(struct sinode *node) {
	node->flags |= SI_SPLIT;
}

static inline void
si_noderef(struct sinode *node)
{
	tt_pthread_mutex_lock(&node->reflock);
	node->refs++;
	tt_pthread_mutex_unlock(&node->reflock);
}

static inline uint16_t
si_nodeunref(struct sinode *node)
{
	tt_pthread_mutex_lock(&node->reflock);
	assert(node->refs > 0);
	uint16_t v = node->refs--;
	tt_pthread_mutex_unlock(&node->reflock);
	return v;
}

static inline uint16_t
si_noderefof(struct sinode *node)
{
	tt_pthread_mutex_lock(&node->reflock);
	uint16_t v = node->refs;
	tt_pthread_mutex_unlock(&node->reflock);
	return v;
}

static inline struct svindex*
si_noderotate(struct sinode *node) {
	node->flags |= SI_ROTATE;
	return &node->i0;
}

static inline void
si_nodeunrotate(struct sinode *node) {
	assert((node->flags & SI_ROTATE) > 0);
	node->flags &= ~SI_ROTATE;
	node->i0 = node->i1;
	node->i0.tree.arg = &node->i0;
	sv_indexinit(&node->i1, node->i0.key_def);
}

static inline struct svindex*
si_nodeindex(struct sinode *node) {
	if (node->flags & SI_ROTATE)
		return &node->i1;
	return &node->i0;
}

static inline struct svindex*
si_nodeindex_priority(struct sinode *node, struct svindex **second)
{
	if (unlikely(node->flags & SI_ROTATE)) {
		*second = &node->i0;
		return &node->i1;
	}
	*second = NULL;
	return &node->i0;
}

static inline int
si_nodecmp(struct sinode *n, void *key, struct key_def *key_def)
{
	struct sdindexpage *min = sd_indexmin(&n->self.index);
	struct sdindexpage *max = sd_indexmax(&n->self.index);
	int l = sf_compare(key_def, sd_indexpage_min(&n->self.index, min), key);
	int r = sf_compare(key_def, sd_indexpage_max(&n->self.index, max), key);
	/* inside range */
	if (l <= 0 && r >= 0)
		return 0;
	/* key > range */
	if (l < 0)
		return -1;
	/* key < range */
	assert(r > 0);
	return 1;
}

static inline int
si_nodecmpnode(struct sinode *n1, struct sinode *n2, struct key_def *key_def)
{
	if (n1 == n2)
		return 0;
	struct sdindexpage *min1 = sd_indexmin(&n1->self.index);
	struct sdindexpage *min2 = sd_indexmin(&n2->self.index);
	return sf_compare(key_def,
			  sd_indexpage_min(&n1->self.index, min1),
			  sd_indexpage_min(&n2->self.index, min2));
}

static inline uint64_t
si_nodesize(struct sinode *n)
{
	uint64_t size = 0;
	struct sibranch *b = n->branch;
	while (b) {
		size += sd_indexsize_ext(b->index.h) +
		        sd_indextotal(&b->index);
		b = b->next;
	}
	return size;
}

struct sinodeview {
	struct sinode   *node;
	uint16_t  flags;
	uint32_t  branch_count;
};

static inline void
si_nodeview_init(struct sinodeview *v, struct sinode *node)
{
	v->node         = node;
	v->branch_count = node->branch_count;
	v->flags        = node->flags;
}

static inline void
si_nodeview_open(struct sinodeview *v, struct sinode *node)
{
	si_noderef(node);
	si_nodeview_init(v, node);
}

static inline void
si_nodeview_close(struct sinodeview *v)
{
	si_nodeunref(v->node);
	v->node = NULL;
}

struct siplanner {
	struct ssrq branch;
	struct ssrq compact;
	struct ssrq temp;
	void *i;
};

/* plan */
#define SI_BRANCH        1
#define SI_AGE           2
#define SI_COMPACT       4
#define SI_COMPACT_INDEX 8
#define SI_CHECKPOINT    16
#define SI_GC            32
#define SI_TEMP          64
#define SI_SHUTDOWN      512
#define SI_DROP          1024
#define SI_LRU           8192
#define SI_NODEGC        16384

/* explain */
#define SI_ENONE         0
#define SI_ERETRY        1
#define SI_EINDEX_SIZE   2
#define SI_EINDEX_AGE    3
#define SI_EBRANCH_COUNT 4

struct siplan {
	int explain;
	int plan;
	/* branch:
	 *   a: index_size
	 * age:
	 *   a: ttl
	 *   b: ttl_wm
	 * compact:
	 *   a: branches
	 *   b: mode
	 * compact_index:
	 *   a: index_size
	 * checkpoint:
	 *   a: lsn
	 * nodegc:
	 * gc:
	 *   a: lsn
	 *   b: percent
	 * lru:
	 * temperature:
	 * snapshot:
	 *   a: ssn
	 * shutdown:
	 * drop:
	 */
	uint64_t a, b, c;
	struct sinode *node;
};

static int si_planinit(struct siplan*);
static int si_plannerinit(struct siplanner*, void*);
static int si_plannerfree(struct siplanner*);
static int si_plannerupdate(struct siplanner*, int, struct sinode*);
static int si_plannerremove(struct siplanner*, int, struct sinode*);
static int si_planner(struct siplanner*, struct siplan*);

typedef rb_tree(struct sinode) sinode_tree_t;

struct sinode_tree_key {
	char *data;
	int size;
};

static int
sinode_tree_cmp(sinode_tree_t *rbtree, struct sinode *a, struct sinode *b);

static int
sinode_tree_key_cmp(sinode_tree_t *rbtree,
		    struct sinode_tree_key *a, struct sinode *b);

rb_gen_ext_key(, sinode_tree_, sinode_tree_t, struct sinode, tree_node,
		 sinode_tree_cmp, struct sinode_tree_key *,
		 sinode_tree_key_cmp);

struct sinode *
sinode_tree_free_cb(sinode_tree_t *t, struct sinode * n, void *arg)
{
	(void)t;
	struct runtime *r = (struct runtime *)arg;
	si_nodefree(n, r, 0);
	return NULL;
}

struct siprofiler {
	uint32_t  total_node_count;
	uint64_t  total_node_size;
	uint64_t  total_node_origin_size;
	uint32_t  total_branch_count;
	uint32_t  total_branch_avg;
	uint32_t  total_branch_max;
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
	int       histogram_branch[20];
	int       histogram_branch_20plus;
	char      histogram_branch_sz[256];
	char     *histogram_branch_ptr;
	char      histogram_temperature_sz[256];
	char     *histogram_temperature_ptr;
	struct vinyl_index  *i;
};

struct vinyl_index {
	struct vinyl_env *env;
	struct siprofiler rtp;
	struct sxindex    coindex;
	uint64_t   txn_min;
	uint64_t   txn_max;

	sinode_tree_t tree;
	struct srstatus   status;
	pthread_mutex_t    lock;
	struct siplanner  p;
	int        n;
	uint64_t   update_time;
	uint64_t   lru_run_lsn;
	uint64_t   lru_v;
	uint64_t   lru_steps;
	uint64_t   lru_intr_lsn;
	uint64_t   lru_intr_sum;
	uint64_t   read_disk;
	uint64_t   read_cache;
	uint64_t   size;
	pthread_mutex_t ref_lock;
	uint32_t   refs;
	uint32_t   gc_count;
	struct rlist     gc;
	struct ssbuf      readbuf;
	struct svupsert   u;
	struct siconf   conf;
	struct key_def *key_def;
	struct runtime *r;
	/** Member of env->db or scheduler->shutdown. */
	struct rlist     link;
};

static int
sinode_tree_cmp(sinode_tree_t *rbtree, struct sinode *a, struct sinode *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vinyl_index, tree)->key_def;
	return si_nodecmpnode(a, b, key_def);
}

static int
sinode_tree_key_cmp(sinode_tree_t *rbtree,
		    struct sinode_tree_key *a, struct sinode *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vinyl_index, tree)->key_def;
	return (-si_nodecmp(b, a->data, key_def));
}

static inline bool
si_active(struct vinyl_index *i) {
	return sr_statusactive(&i->status);
}

static inline void
si_lock(struct vinyl_index *i) {
	tt_pthread_mutex_lock(&i->lock);
}

static inline void
si_unlock(struct vinyl_index *i) {
	tt_pthread_mutex_unlock(&i->lock);
}

static inline int
vinyl_index_delete(struct vinyl_index *index);
static int si_recover(struct vinyl_index*);
static int si_insert(struct vinyl_index*, struct sinode*);
static int si_remove(struct vinyl_index*, struct sinode*);
static int si_replace(struct vinyl_index*, struct sinode*, struct sinode*);
static int si_plan(struct vinyl_index*, struct siplan*);
static int
si_execute(struct vinyl_index*, struct sdc*, struct siplan*, uint64_t, uint64_t);

static inline void
si_lru_add(struct vinyl_index *index, struct svref *ref)
{
	index->lru_intr_sum += ref->v->size;
	if (unlikely(index->lru_intr_sum >= index->conf.lru_step))
	{
		uint64_t lsn = sr_seq(index->r->seq, SR_LSN);
		index->lru_v += (lsn - index->lru_intr_lsn);
		index->lru_steps++;
		index->lru_intr_lsn = lsn;
		index->lru_intr_sum = 0;
	}
}

static inline uint64_t
si_lru_vlsn_of(struct vinyl_index *i)
{
	assert(i->conf.lru_step != 0);
	uint64_t size = i->size;
	if (likely(size <= i->conf.lru))
		return 0;
	uint64_t lru_v = i->lru_v;
	uint64_t lru_steps = i->lru_steps;
	uint64_t lru_avg_step;
	uint64_t oversize = size - i->conf.lru;
	uint64_t steps = 1 + oversize / i->conf.lru_step;
	lru_avg_step = lru_v / lru_steps;
	return i->lru_intr_lsn + (steps * lru_avg_step);
}

static inline uint64_t
si_lru_vlsn(struct vinyl_index *i)
{
	if (likely(i->conf.lru == 0))
		return 0;
	si_lock(i);
	int rc = si_lru_vlsn_of(i);
	si_unlock(i);
	return rc;
}

struct PACKED sicachebranch {
	struct sibranch *branch;
	struct sdindexpage *ref;
	struct sdpage page;
	struct ssiter i;
	struct sdpageiter page_iter;
	struct sdindexiter index_iter;
	struct ssbuf buf_a;
	struct ssbuf buf_b;
	int open;
	struct sicachebranch *next;
};

struct sicache {
	struct sicachebranch *path;
	struct sicachebranch *branch;
	uint32_t count;
	uint64_t nsn;
	struct sinode *node;
	struct sicache *next;
	struct sicachepool *pool;
};

struct sicachepool {
	struct sicache *head;
	int n;
	struct runtime *r;
	pthread_mutex_t mutex;
};

static inline void
si_cacheinit(struct sicache *c, struct sicachepool *pool)
{
	c->path   = NULL;
	c->branch = NULL;
	c->count  = 0;
	c->node   = NULL;
	c->nsn    = 0;
	c->next   = NULL;
	c->pool   = pool;
}

static inline void
si_cachefree(struct sicache *c)
{
	struct sicachebranch *next;
	struct sicachebranch *cb = c->path;
	while (cb) {
		next = cb->next;
		ss_buffree(&cb->buf_a);
		ss_buffree(&cb->buf_b);
		free(cb);
		cb = next;
	}
}

static inline void
si_cachereset(struct sicache *c)
{
	struct sicachebranch *cb = c->path;
	while (cb) {
		ss_bufreset(&cb->buf_a);
		ss_bufreset(&cb->buf_b);
		cb->branch = NULL;
		cb->ref = NULL;
		sd_read_close(&cb->i);
		cb->open = 0;
		cb = cb->next;
	}
	c->branch = NULL;
	c->node   = NULL;
	c->nsn    = 0;
	c->count  = 0;
}

static inline struct sicachebranch*
si_cacheadd(struct sibranch *b)
{
	struct sicachebranch *nb =
		malloc(sizeof(struct sicachebranch));
	if (unlikely(nb == NULL))
		return NULL;
	nb->branch  = b;
	nb->ref     = NULL;
	memset(&nb->i, 0, sizeof(nb->i));
	nb->open    = 0;
	nb->next    = NULL;
	ss_bufinit(&nb->buf_a);
	ss_bufinit(&nb->buf_b);
	return nb;
}

static inline int
si_cachevalidate(struct sicache *c, struct sinode *n)
{
	if (likely(c->node == n && c->nsn == n->self.id.id))
	{
		if (likely(n->branch_count == c->count)) {
			c->branch = c->path;
			return 0;
		}
		assert(n->branch_count > c->count);
		/* c b a */
		/* e d c b a */
		struct sicachebranch *head = NULL;
		struct sicachebranch *last = NULL;
		struct sicachebranch *cb = c->path;
		struct sibranch *b = n->branch;
		while (b) {
			if (cb->branch == b) {
				assert(last != NULL);
				last->next = cb;
				break;
			}
			struct sicachebranch *nb = si_cacheadd(b);
			if (unlikely(nb == NULL))
				return -1;
			if (! head)
				head = nb;
			if (last)
				last->next = nb;
			last = nb;
			b = b->next;
		}
		c->path   = head;
		c->count  = n->branch_count;
		c->branch = c->path;
		return 0;
	}
	struct sicachebranch *last = c->path;
	struct sicachebranch *cb = last;
	struct sibranch *b = n->branch;
	while (cb && b) {
		cb->branch = b;
		cb->ref = NULL;
		cb->open = 0;
		sd_read_close(&cb->i);
		ss_bufreset(&cb->buf_a);
		ss_bufreset(&cb->buf_b);
		last = cb;
		cb = cb->next;
		b  = b->next;
	}
	while (cb) {
		cb->branch = NULL;
		cb->ref = NULL;
		cb->open = 0;
		sd_read_close(&cb->i);
		ss_bufreset(&cb->buf_a);
		ss_bufreset(&cb->buf_b);
		cb = cb->next;
	}
	while (b) {
		cb = si_cacheadd(b);
		if (unlikely(cb == NULL))
			return -1;
		if (last)
			last->next = cb;
		last = cb;
		if (c->path == NULL)
			c->path = cb;
		b = b->next;
	}
	c->count  = n->branch_count;
	c->node   = n;
	c->nsn    = n->self.id.id;
	c->branch = c->path;
	return 0;
}

static inline struct sicachebranch*
si_cacheseek(struct sicache *c, struct sibranch *seek)
{
	while (c->branch) {
		struct sicachebranch *cb = c->branch;
		c->branch = c->branch->next;
		if (likely(cb->branch == seek))
			return cb;
	}
	return NULL;
}

static inline struct sicachebranch*
si_cachefollow(struct sicache *c, struct sibranch *seek)
{
	while (c->branch) {
		struct sicachebranch *cb = c->branch;
		c->branch = c->branch->next;
		if (likely(cb->branch == seek))
			return cb;
	}
	return NULL;
}

static inline void
si_cachepool_init(struct sicachepool *p, struct runtime *r)
{
	p->head = NULL;
	p->n    = 0;
	p->r    = r;
	tt_pthread_mutex_init(&p->mutex, NULL);
}

static inline void
si_cachepool_free(struct sicachepool *p)
{
	struct sicache *next;
	struct sicache *c = p->head;
	while (c) {
		next = c->next;
		si_cachefree(c);
		free(c);
		c = next;
	}
	tt_pthread_mutex_destroy(&p->mutex);
}

static inline struct sicache*
si_cachepool_pop(struct sicachepool *p)
{
	tt_pthread_mutex_lock(&p->mutex);
	struct sicache *c;
	if (likely(p->n > 0)) {
		c = p->head;
		p->head = c->next;
		p->n--;
		si_cachereset(c);
		c->pool = p;
	} else {
		c = malloc(sizeof(struct sicache));
		if (unlikely(c == NULL))
			return NULL;
		si_cacheinit(c, p);
	}
	tt_pthread_mutex_unlock(&p->mutex);
	return c;
}

static inline void
si_cachepool_push(struct sicache *c)
{
	struct sicachepool *p = c->pool;
	tt_pthread_mutex_lock(&p->mutex);
	c->next = p->head;
	p->head = c;
	p->n++;
	tt_pthread_mutex_unlock(&p->mutex);
}

struct sitx {
	int ro;
	struct rlist nodelist;
	struct vinyl_index *index;
};

static void si_begin(struct sitx*, struct vinyl_index*);
static void si_commit(struct sitx*);

static inline void
si_txtrack(struct sitx *x, struct sinode *n) {
	if (rlist_empty(&n->commit))
		rlist_add(&x->nodelist, &n->commit);
}

static void
si_write(struct sitx*, struct svlog*, struct svlogindex*, uint64_t,
	 enum vinyl_status status);

struct siread {
	enum vinyl_order   order;
	void     *key;
	uint32_t  keysize;
	int       has;
	uint64_t  vlsn;
	struct svmerge   merge;
	int       cache_only;
	int       oldest_only;
	int       read_disk;
	int       read_cache;
	struct sv       *upsert_v;
	int       upsert_eq;
	struct vinyl_tuple *result;
	struct sicache  *cache;
	struct vinyl_index       *index;
};

struct PACKED siiter {
	struct vinyl_index *index;
	struct sinode *node;
	enum vinyl_order order;
	void *key;
	int keysize;
};

static inline int
si_iter_open(struct siiter *ii, struct vinyl_index *index, enum vinyl_order o,
	     void *key, int keysize)
{
	ii->index   = index;
	ii->order   = o;
	ii->key     = key;
	ii->keysize = keysize;
	ii->node = NULL;
	int eq = 0;
	if (unlikely(index->n == 1)) {
		ii->node = sinode_tree_first(&index->tree);
		return 1;
	}
	if (unlikely(ii->key == NULL)) {
		switch (ii->order) {
		case VINYL_LT:
		case VINYL_LE:
			ii->node = sinode_tree_last(&index->tree);;
			break;
		case VINYL_GT:
		case VINYL_GE:
			ii->node = sinode_tree_first(&index->tree);
			break;
		default:
			unreachable();
			break;
		}
		return 0;
	}
	/* route */
	assert(ii->key != NULL);
	struct sinode_tree_key tree_key;
	tree_key.data = ii->key;
	tree_key.size = ii->keysize;
	ii->node = sinode_tree_search(&index->tree, &tree_key);
	if (ii->node != NULL) {
		eq = 1;
	} else {
		ii->node = sinode_tree_psearch(&index->tree, &tree_key);
	}
	assert(ii->node != NULL);
	return eq;
}

static inline struct sinode *
si_iter_get(struct siiter *ii)
{
	return ii->node;
}

static inline void
si_iter_next(struct siiter *ii)
{
	switch (ii->order) {
	case VINYL_LT:
	case VINYL_LE:
		ii->node = sinode_tree_prev(&ii->index->tree, ii->node);
		break;
	case VINYL_GT:
	case VINYL_GE:
		ii->node = sinode_tree_next(&ii->index->tree, ii->node);
		break;
	default: unreachable();
	}
}

static int si_drop(struct vinyl_index*);
static int si_dropmark(struct vinyl_index*);
static int si_droprepository(char*, int);

static int
si_merge(struct vinyl_index*, struct sdc*, struct sinode*, uint64_t,
	 uint64_t, struct svmergeiter*, uint64_t, uint32_t);

static int si_branch(struct vinyl_index*, struct sdc*, struct siplan*, uint64_t);
static int
si_compact(struct vinyl_index*, struct sdc*, struct siplan*, uint64_t,
	   uint64_t, struct ssiter*, uint64_t);
static int
si_compact_index(struct vinyl_index*, struct sdc*, struct siplan*, uint64_t, uint64_t);

typedef rb_tree(struct sinode) sinode_id_tree_t;

static int
sinode_id_tree_cmp(sinode_id_tree_t *rbtree, struct sinode *a, struct sinode *b)
{
	(void)rbtree;
	return ss_cmp(a->self.id.id, b->self.id.id);
}

static int
sinode_id_tree_key_cmp(sinode_id_tree_t *rbtree, const char *a, struct sinode *b)
{
	(void)rbtree;
	return ss_cmp(load_u64(a), b->self.id.id);
}

rb_gen_ext_key(, sinode_id_tree_, sinode_id_tree_t, struct sinode,
		 tree_node, sinode_id_tree_cmp, const char *,
		 sinode_id_tree_key_cmp);

struct sinode *
sinode_id_tree_free_cb(sinode_id_tree_t *t, struct sinode * n, void *arg)
{
	(void)t;
	struct runtime *r = (struct runtime *)arg;
	si_nodefree(n, r, 0);
	return NULL;
}

struct sitrack {
	sinode_id_tree_t tree;
	int count;
	uint64_t nsn;
	uint64_t lsn;
};

static inline void
si_trackinit(struct sitrack *t)
{
	sinode_id_tree_new(&t->tree);
	t->count = 0;
	t->nsn = 0;
	t->lsn = 0;
}

static inline void
si_trackfree(struct sitrack *t, struct runtime *r)
{
	sinode_id_tree_iter(&t->tree, NULL, sinode_id_tree_free_cb, r);
#ifndef NDEBUG
	t->tree.rbt_root = (struct sinode *)(intptr_t)0xDEADBEEF;
#endif
}

static inline void
si_trackmetrics(struct sitrack *t, struct sinode *n)
{
	struct sibranch *b = n->branch;
	while (b) {
		struct sdindexheader *h = b->index.h;
		if (b->id.parent > t->nsn)
			t->nsn = b->id.parent;
		if (b->id.id > t->nsn)
			t->nsn = b->id.id;
		if (h->lsnmin != UINT64_MAX && h->lsnmin > t->lsn)
			t->lsn = h->lsnmin;
		if (h->lsnmax > t->lsn)
			t->lsn = h->lsnmax;
		b = b->next;
	}
}

static inline void
si_tracknsn(struct sitrack *t, uint64_t nsn)
{
	if (t->nsn < nsn)
		t->nsn = nsn;
}

static inline void
si_trackset(struct sitrack *t, struct sinode *n)
{
	sinode_id_tree_insert(&t->tree, n);
	t->count++;
}

static inline struct sinode*
si_trackget(struct sitrack *t, uint64_t id)
{
	return sinode_id_tree_search(&t->tree, (const char *)&id);
}

static inline void
si_trackreplace(struct sitrack *t, struct sinode *o, struct sinode *n)
{
	sinode_id_tree_remove(&t->tree, o);
	sinode_id_tree_insert(&t->tree, n);
}
static struct sinode *si_bootstrap(struct vinyl_index*, uint64_t);
static int si_recover(struct vinyl_index*);

static int si_profilerbegin(struct siprofiler*, struct vinyl_index*);
static int si_profilerend(struct siprofiler*);
static int si_profiler(struct siprofiler*);

static int si_insert(struct vinyl_index *index, struct sinode *n)
{
	sinode_tree_insert(&index->tree, n);
	index->n++;
	return 0;
}

static int si_remove(struct vinyl_index *index, struct sinode *n)
{
	sinode_tree_remove(&index->tree, n);
	index->n--;
	return 0;
}

static int si_replace(struct vinyl_index *index, struct sinode *o, struct sinode *n)
{
	sinode_tree_remove(&index->tree, o);
	sinode_tree_insert(&index->tree, n);
	return 0;
}

static int si_plan(struct vinyl_index *index, struct siplan *plan)
{
	si_lock(index);
	int rc = si_planner(&index->p, plan);
	si_unlock(index);
	return rc;
}

static int
si_execute(struct vinyl_index *index, struct sdc *c, struct siplan *plan,
	   uint64_t vlsn, uint64_t vlsn_lru)
{
	int rc = -1;
	switch (plan->plan) {
	case SI_NODEGC:
		rc = si_nodefree(plan->node, index->r, 1);
		break;
	case SI_CHECKPOINT:
	case SI_BRANCH:
	case SI_AGE:
		rc = si_branch(index, c, plan, vlsn);
		break;
	case SI_LRU:
	case SI_GC:
	case SI_COMPACT:
		rc = si_compact(index, c, plan, vlsn, vlsn_lru, NULL, 0);
		break;
	case SI_COMPACT_INDEX:
		rc = si_compact_index(index, c, plan, vlsn, vlsn_lru);
		break;
	default:
		unreachable();
	}
	/* garbage collect buffers */
	sd_cgc(c, index->conf.buf_gc_wm);
	return rc;
}

static inline int
si_branchcreate(struct vinyl_index *index, struct sdc *c,
		struct sinode *parent, struct svindex *vindex,
		uint64_t vlsn, struct sibranch **result)
{
	struct runtime *r = index->r;
	struct sibranch *branch = NULL;

	/* in-memory mode blob */
	int rc;
	struct svmerge vmerge;
	sv_mergeinit(&vmerge, index->key_def);
	rc = sv_mergeprepare(&vmerge, 1);
	if (unlikely(rc == -1))
		return -1;
	struct svmergesrc *s = sv_mergeadd(&vmerge, NULL);
	sv_indexiter_open(&s->src, vindex, VINYL_GE, NULL, 0);
	struct svmergeiter im;
	sv_mergeiter_open(&im, &vmerge, VINYL_GE);

	/* merge iter is not used */
	struct sdmergeconf mergeconf = {
		.stream          = vindex->tree.size,
		.size_stream     = UINT32_MAX,
		.size_node       = UINT64_MAX,
		.size_page       = index->conf.node_page_size,
		.checksum        = index->conf.node_page_checksum,
		.compression     = index->conf.compression,
		.compression_if  = index->conf.compression_if,
		.vlsn            = vlsn,
		.vlsn_lru        = 0,
		.save_delete     = 1,
		.save_upsert     = 1
	};
	struct sdmerge merge;
	rc = sd_mergeinit(&merge, &im, &c->build,
	                  &c->upsert, &mergeconf);
	if (unlikely(rc == -1))
		return -1;

	while ((rc = sd_merge(&merge)) > 0)
	{
		assert(branch == NULL);

		/* write open seal */
		uint64_t seal = parent->file.size;
		rc = sd_writeseal(&parent->file);
		if (unlikely(rc == -1))
			goto e0;

		/* write pages */
		uint64_t offset = parent->file.size;
		while ((rc = sd_mergepage(&merge, offset)) == 1)
		{
			rc = sd_writepage(&parent->file, merge.build);
			if (unlikely(rc == -1))
				goto e0;
			offset = parent->file.size;
		}
		if (unlikely(rc == -1))
			goto e0;
		struct sdid id = {
			.parent = parent->self.id.id,
			.flags  = SD_IDBRANCH,
			.id     = sr_seq(r->seq, SR_NSNNEXT)
		};
		rc = sd_mergecommit(&merge, &id, parent->file.size);
		if (unlikely(rc == -1))
			goto e0;

		/* write index */
		rc = sd_writeindex(&parent->file, &merge.index);
		if (unlikely(rc == -1))
			goto e0;
		if (index->conf.sync) {
			rc = ss_filesync(&parent->file);
			if (unlikely(rc == -1)) {
				sr_malfunction("file '%s' sync error: %s",
				               ss_pathof(&parent->file.path),
				               strerror(errno));
				goto e0;
			}
		}

		SS_INJECTION(r->i, SS_INJECTION_SI_BRANCH_0,
		             sd_mergefree(&merge);
		             sr_malfunction("%s", "error injection");
		             return -1);

		/* seal the branch */
		rc = sd_seal(&parent->file, &merge.index, seal);
		if (unlikely(rc == -1))
			goto e0;
		if (index->conf.sync == 2) {
			rc = ss_filesync(&parent->file);
			if (unlikely(rc == -1)) {
				sr_malfunction("file '%s' sync error: %s",
				               ss_pathof(&parent->file.path),
				               strerror(errno));
				goto e0;
			}
		}

		/* create new branch object */
		branch = si_branchnew();
		if (unlikely(branch == NULL))
			goto e0;
		si_branchset(branch, &merge.index);
	}
	sv_mergefree(&vmerge);

	if (unlikely(rc == -1)) {
		sr_oom();
		goto e0;
	}

	/* in case of expire, branch may not be created if there
	 * are no keys left */
	if (unlikely(branch == NULL))
		return 0;

	*result = branch;
	return 0;
e0:
	sd_mergefree(&merge);
	sv_mergefree(&vmerge);
	return -1;
}

static int
si_branch(struct vinyl_index *index, struct sdc *c, struct siplan *plan, uint64_t vlsn)
{
	struct runtime *r = index->r;
	struct sinode *n = plan->node;
	assert(n->flags & SI_LOCK);

	si_lock(index);
	if (unlikely(n->used == 0)) {
		si_nodeunlock(n);
		si_unlock(index);
		return 0;
	}
	struct svindex *i;
	i = si_noderotate(n);
	si_unlock(index);

	struct sibranch *branch = NULL;
	int rc = si_branchcreate(index, c, n, i, vlsn, &branch);
	if (unlikely(rc == -1))
		return -1;
	if (unlikely(branch == NULL)) {
		si_lock(index);
		uint32_t used = sv_indexused(i);
		n->used -= used;
		ss_quota(r->quota, SS_QREMOVE, used);
		struct svindex swap = *i;
		swap.tree.arg = &swap;
		si_nodeunrotate(n);
		si_nodeunlock(n);
		si_plannerupdate(&index->p, SI_BRANCH|SI_COMPACT, n);
		si_unlock(index);
		si_nodegc_index(r, &swap);
		return 0;
	}

	/* commit */
	si_lock(index);
	branch->next = n->branch;
	n->branch->link = branch;
	n->branch = branch;
	n->branch_count++;
	uint32_t used = sv_indexused(i);
	n->used -= used;
	ss_quota(r->quota, SS_QREMOVE, used);
	index->size +=
		sd_indexsize_ext(branch->index.h) +
		sd_indextotal(&branch->index);
	struct svindex swap = *i;
	swap.tree.arg = &swap;
	si_nodeunrotate(n);
	si_nodeunlock(n);
	si_plannerupdate(&index->p, SI_BRANCH|SI_COMPACT, n);
	si_unlock(index);

	si_nodegc_index(r, &swap);
	return 1;
}

static int
si_compact(struct vinyl_index *index, struct sdc *c, struct siplan *plan,
	   uint64_t vlsn,
	   uint64_t vlsn_lru,
	   struct ssiter *vindex,
	   uint64_t vindex_used)
{
	struct runtime *r = index->r;
	struct sinode *node = plan->node;
	assert(node->flags & SI_LOCK);

	/* prepare for compaction */
	int rc;
	rc = sd_censure(c, node->branch_count);
	if (unlikely(rc == -1))
		return sr_oom();
	struct svmerge merge;
	sv_mergeinit(&merge, index->key_def);
	rc = sv_mergeprepare(&merge, node->branch_count + 1);
	if (unlikely(rc == -1))
		return -1;

	/* include vindex into merge process */
	struct svmergesrc *s;
	uint32_t count = 0;
	uint64_t size_stream = 0;
	if (vindex) {
		s = sv_mergeadd(&merge, vindex);
		size_stream = vindex_used;
	}

	struct sdcbuf *cbuf = c->head;
	struct sibranch *b = node->branch;
	while (b) {
		s = sv_mergeadd(&merge, NULL);
		/* choose compression type */
		int compression;
		struct ssfilterif *compression_if;
		compression    = index->conf.compression;
		compression_if = index->conf.compression_if;
		struct sdreadarg arg = {
			.index           = &b->index,
			.buf             = &cbuf->a,
			.buf_xf          = &cbuf->b,
			.buf_read        = &c->d,
			.index_iter      = &cbuf->index_iter,
			.page_iter       = &cbuf->page_iter,
			.use_compression = compression,
			.compression_if  = compression_if,
			.has             = 0,
			.has_vlsn        = 0,
			.o               = VINYL_GE,
			.file            = &node->file,
			.a               = r->a,
			.key_def		 = index->key_def
		};
		int rc = sd_read_open(&s->src, &arg, NULL, 0);
		if (unlikely(rc == -1))
			return sr_oom();
		size_stream += sd_indextotal(&b->index);
		count += sd_indexkeys(&b->index);
		cbuf = cbuf->next;
		b = b->next;
	}
	struct svmergeiter im;
	sv_mergeiter_open(&im, &merge, VINYL_GE);
	rc = si_merge(index, c, node, vlsn, vlsn_lru, &im, size_stream, count);
	sv_mergefree(&merge);
	return rc;
}

static int
si_compact_index(struct vinyl_index *index, struct sdc *c, struct siplan *plan,
		 uint64_t vlsn,
		 uint64_t vlsn_lru)
{
	struct sinode *node = plan->node;

	si_lock(index);
	if (unlikely(node->used == 0)) {
		si_nodeunlock(node);
		si_unlock(index);
		return 0;
	}
	struct svindex *vindex;
	vindex = si_noderotate(node);
	si_unlock(index);

	uint64_t size_stream = sv_indexused(vindex);
	struct ssiter i;
	sv_indexiter_open(&i, vindex, VINYL_GE, NULL, 0);
	return si_compact(index, c, plan, vlsn, vlsn_lru, &i, size_stream);
}

static int si_droprepository(char *repo, int drop_directory)
{
	DIR *dir = opendir(repo);
	if (dir == NULL) {
		sr_malfunction("directory '%s' open error: %s",
		               repo, strerror(errno));
		return -1;
	}
	char path[1024];
	int rc;
	struct dirent *de;
	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.')
			continue;
		/* skip drop file */
		if (unlikely(strcmp(de->d_name, "drop") == 0))
			continue;
		snprintf(path, sizeof(path), "%s/%s", repo, de->d_name);
		rc = unlink(path);
		if (unlikely(rc == -1)) {
			sr_malfunction("index file '%s' unlink error: %s",
			               path, strerror(errno));
			closedir(dir);
			return -1;
		}
	}
	closedir(dir);

	snprintf(path, sizeof(path), "%s/drop", repo);
	rc = unlink(path);
	if (unlikely(rc == -1)) {
		sr_malfunction("index file '%s' unlink error: %s",
		               path, strerror(errno));
		return -1;
	}
	if (drop_directory) {
		rc = rmdir(repo);
		if (unlikely(rc == -1)) {
			sr_malfunction("directory '%s' unlink error: %s",
			               repo, strerror(errno));
			return -1;
		}
	}
	return 0;
}

static int si_dropmark(struct vinyl_index *i)
{
	/* create drop file */
	char path[1024];
	snprintf(path, sizeof(path), "%s/drop", i->conf.path);
	struct ssfile drop;
	ss_fileinit(&drop);
	int rc = ss_filenew(&drop, path);
	if (unlikely(rc == -1)) {
		sr_malfunction("drop file '%s' create error: %s",
		               path, strerror(errno));
		return -1;
	}
	ss_fileclose(&drop);
	return 0;
}

static int si_drop(struct vinyl_index *i)
{
	struct sspath path;
	ss_pathinit(&path);
	ss_pathset(&path, "%s", i->conf.path);
	/* drop file must exists at this point */
	/* shutdown */
	int rc = vinyl_index_delete(i);
	if (unlikely(rc == -1))
		return -1;
	/* remove directory */
	rc = si_droprepository(path.path, 1);
	return rc;
}

static int
si_redistribute(struct vinyl_index *index, struct sdc *c,
		struct sinode *node, struct ssbuf *result)
{
	(void)index;
	struct svindex *vindex = si_nodeindex(node);
	struct ssiter i;
	sv_indexiter_open(&i, vindex, VINYL_GE, NULL, 0);
	while (sv_indexiter_has(&i))
	{
		struct sv *v = sv_indexiter_get(&i);
		int rc = ss_bufadd(&c->b, &v->v, sizeof(struct svref**));
		if (unlikely(rc == -1))
			return sr_oom();
		sv_indexiter_next(&i);
	}
	if (unlikely(ss_bufused(&c->b) == 0))
		return 0;
	ss_bufiterref_open(&i, &c->b, sizeof(struct svref*));
	struct ssiter j;
	ss_bufiterref_open(&j, result, sizeof(struct sinode*));
	struct sinode *prev = ss_bufiterref_get(&j);
	ss_bufiterref_next(&j);
	while (1)
	{
		struct sinode *p = ss_bufiterref_get(&j);
		if (p == NULL) {
			assert(prev != NULL);
			while (ss_bufiterref_has(&i)) {
				struct svref *v = ss_bufiterref_get(&i);
				sv_indexset(&prev->i0, *v);
				ss_bufiterref_next(&i);
			}
			break;
		}
		while (ss_bufiterref_has(&i))
		{
			struct svref *v = ss_bufiterref_get(&i);
			struct sdindexpage *page = sd_indexmin(&p->self.index);
			int rc = sf_compare(index->key_def, v->v->data,
			                    sd_indexpage_min(&p->self.index, page));
			if (unlikely(rc >= 0))
				break;
			sv_indexset(&prev->i0, *v);
			ss_bufiterref_next(&i);
		}
		if (unlikely(! ss_bufiterref_has(&i)))
			break;
		prev = p;
		ss_bufiterref_next(&j);
	}
	assert(ss_bufiterref_get(&i) == NULL);
	return 0;
}

static inline void
si_redistribute_set(struct vinyl_index *index, uint64_t now, struct svref *v)
{
	index->update_time = now;
	/* match node */
	struct siiter ii;
	si_iter_open(&ii, index, VINYL_GE, v->v->data, v->v->size);
	struct sinode *node = si_iter_get(&ii);
	assert(node != NULL);
	/* update node */
	struct svindex *vindex = si_nodeindex(node);
	sv_indexset(vindex, *v);
	node->update_time = index->update_time;
	node->used += vinyl_tuple_size(v->v);
	/* schedule node */
	si_plannerupdate(&index->p, SI_BRANCH, node);
}

static int
si_redistribute_index(struct vinyl_index *index, struct sdc *c, struct sinode *node)
{
	struct svindex *vindex = si_nodeindex(node);
	struct ssiter i;
	sv_indexiter_open(&i, vindex, VINYL_GE, NULL, 0);
	while (sv_indexiter_has(&i)) {
		struct sv *v = sv_indexiter_get(&i);
		int rc = ss_bufadd(&c->b, &v->v, sizeof(struct svref**));
		if (unlikely(rc == -1))
			return sr_oom();
		sv_indexiter_next(&i);
	}
	if (unlikely(ss_bufused(&c->b) == 0))
		return 0;
	uint64_t now = clock_monotonic64();
	ss_bufiterref_open(&i, &c->b, sizeof(struct svref*));
	while (ss_bufiterref_has(&i)) {
		struct svref *v = ss_bufiterref_get(&i);
		si_redistribute_set(index, now, v);
		ss_bufiterref_next(&i);
	}
	return 0;
}

static int
si_splitfree(struct ssbuf *result, struct runtime *r)
{
	struct ssiter i;
	ss_bufiterref_open(&i, result, sizeof(struct sinode*));
	while (ss_bufiterref_has(&i))
	{
		struct sinode *p = ss_bufiterref_get(&i);
		si_nodefree(p, r, 0);
		ss_bufiterref_next(&i);
	}
	return 0;
}

static inline int
si_split(struct vinyl_index *index, struct sdc *c, struct ssbuf *result,
         struct sinode   *parent,
         struct svmergeiter *i,
         uint64_t  size_node,
         uint64_t  size_stream,
         uint32_t  stream,
         uint64_t  vlsn,
         uint64_t  vlsn_lru)
{
	struct runtime *r = index->r;
	int rc;
	struct sdmergeconf mergeconf = {
		.stream          = stream,
		.size_stream     = size_stream,
		.size_node       = size_node,
		.size_page       = index->conf.node_page_size,
		.checksum        = index->conf.node_page_checksum,
		.compression     = index->conf.compression,
		.compression_if  = index->conf.compression_if,
		.vlsn            = vlsn,
		.vlsn_lru        = vlsn_lru,
		.save_delete     = 0,
		.save_upsert     = 0
	};
	struct sinode *n = NULL;
	struct sdmerge merge;
	rc = sd_mergeinit(&merge, i, &c->build, &c->upsert,
			  &mergeconf);
	if (unlikely(rc == -1))
		return -1;
	while ((rc = sd_merge(&merge)) > 0)
	{
		/* create new node */
		n = si_nodenew(index->key_def);
		if (unlikely(n == NULL))
			goto error;
		struct sdid id = {
			.parent = parent->self.id.id,
			.flags  = 0,
			.id     = sr_seq(index->r->seq, SR_NSNNEXT)
		};
		rc = si_nodecreate(n, &index->conf, &id);
		if (unlikely(rc == -1))
			goto error;
		n->branch = &n->self;
		n->branch_count++;

		/* write open seal */
		uint64_t seal = n->file.size;
		rc = sd_writeseal(&n->file);
		if (unlikely(rc == -1))
			goto error;

		/* write pages */
		uint64_t offset = n->file.size;
		while ((rc = sd_mergepage(&merge, offset)) == 1) {
			rc = sd_writepage(&n->file, merge.build);
			if (unlikely(rc == -1))
				goto error;
			offset = n->file.size;
		}
		if (unlikely(rc == -1))
			goto error;

		rc = sd_mergecommit(&merge, &id, n->file.size);
		if (unlikely(rc == -1))
			goto error;

		/* write index */
		rc = sd_writeindex(&n->file, &merge.index);
		if (unlikely(rc == -1))
			goto error;

		/* update seal */
		rc = sd_seal(&n->file, &merge.index, seal);
		if (unlikely(rc == -1))
			goto error;

		/* add node to the list */
		rc = ss_bufadd(result, &n, sizeof(struct sinode*));
		if (unlikely(rc == -1)) {
			sr_oom();
			goto error;
		}

		si_branchset(&n->self, &merge.index);
	}
	if (unlikely(rc == -1))
		goto error;
	return 0;
error:
	if (n)
		si_nodefree(n, r, 0);
	sd_mergefree(&merge);
	si_splitfree(result, r);
	return -1;
}

static int
si_merge(struct vinyl_index *index, struct sdc *c, struct sinode *node,
	 uint64_t vlsn,
	 uint64_t vlsn_lru,
	 struct svmergeiter *stream,
	 uint64_t size_stream,
	 uint32_t n_stream)
{
	struct runtime *r = index->r;
	struct ssbuf *result = &c->a;
	struct ssiter i;

	/* begin compaction.
	 *
	 * Split merge stream into a number of
	 * a new nodes.
	 */
	int rc;
	rc = si_split(index, c, result,
	              node, stream,
	              index->conf.node_size,
	              size_stream,
	              n_stream,
	              vlsn,
	              vlsn_lru);
	if (unlikely(rc == -1))
		return -1;

	SS_INJECTION(r->i, SS_INJECTION_SI_COMPACTION_0,
	             si_splitfree(result, r);
	             sr_malfunction("%s", "error injection");
	             return -1);

	/* mask removal of a single node as a
	 * single node update */
	int count = ss_bufused(result) / sizeof(struct sinode*);
	int count_index;

	si_lock(index);
	count_index = index->n;
	si_unlock(index);

	struct sinode *n;
	if (unlikely(count == 0 && count_index == 1))
	{
		n = si_bootstrap(index, node->self.id.id);
		if (unlikely(n == NULL))
			return -1;
		rc = ss_bufadd(result, &n, sizeof(struct sinode*));
		if (unlikely(rc == -1)) {
			sr_oom();
			si_nodefree(n, r, 1);
			return -1;
		}
		count++;
	}

	/* commit compaction changes */
	si_lock(index);
	struct svindex *j = si_nodeindex(node);
	si_plannerremove(&index->p, SI_COMPACT|SI_BRANCH|SI_TEMP, node);
	si_nodesplit(node);
	index->size -= si_nodesize(node);
	switch (count) {
	case 0: /* delete */
		si_remove(index, node);
		si_redistribute_index(index, c, node);
		break;
	case 1: /* self update */
		n = *(struct sinode**)result->s;
		n->i0 = *j;
		n->i0.tree.arg = &n->i0;
		n->temperature = node->temperature;
		n->temperature_reads = node->temperature_reads;
		n->used = sv_indexused(j);
		index->size += si_nodesize(n);
		si_nodelock(n);
		si_replace(index, node, n);
		si_plannerupdate(&index->p, SI_COMPACT|SI_BRANCH|SI_TEMP, n);
		break;
	default: /* split */
		rc = si_redistribute(index, c, node, result);
		if (unlikely(rc == -1)) {
			si_unlock(index);
			si_splitfree(result, r);
			return -1;
		}
		ss_bufiterref_open(&i, result, sizeof(struct sinode*));
		n = ss_bufiterref_get(&i);
		n->used = sv_indexused(&n->i0);
		n->temperature = node->temperature;
		n->temperature_reads = node->temperature_reads;
		index->size += si_nodesize(n);
		si_nodelock(n);
		si_replace(index, node, n);
		si_plannerupdate(&index->p, SI_COMPACT|SI_BRANCH|SI_TEMP, n);
		for (ss_bufiterref_next(&i); ss_bufiterref_has(&i);
		     ss_bufiterref_next(&i)) {
			n = ss_bufiterref_get(&i);
			n->used = sv_indexused(&n->i0);
			n->temperature = node->temperature;
			n->temperature_reads = node->temperature_reads;
			index->size += si_nodesize(n);
			si_nodelock(n);
			si_insert(index, n);
			si_plannerupdate(&index->p, SI_COMPACT|SI_BRANCH|SI_TEMP, n);
		}
		break;
	}
	sv_indexinit(j, index->key_def);
	si_unlock(index);

	/* compaction completion */

	/* seal nodes */
	ss_bufiterref_open(&i, result, sizeof(struct sinode*));
	while (ss_bufiterref_has(&i))
	{
		n  = ss_bufiterref_get(&i);
		rc = si_nodeseal(n, &index->conf);
		if (unlikely(rc == -1)) {
			si_nodefree(node, r, 0);
			return -1;
		}
		SS_INJECTION(r->i, SS_INJECTION_SI_COMPACTION_3,
		             si_nodefree(node, r, 0);
		             sr_malfunction("%s", "error injection");
		             return -1);
		ss_bufiterref_next(&i);
	}

	SS_INJECTION(r->i, SS_INJECTION_SI_COMPACTION_1,
	             si_nodefree(node, r, 0);
	             sr_malfunction("%s", "error injection");
	             return -1);

	/* gc node */
	uint16_t refs = si_noderefof(node);
	if (likely(refs == 0)) {
		rc = si_nodefree(node, r, 1);
		if (unlikely(rc == -1))
			return -1;
	} else {
		/* node concurrently being read, schedule for
		 * delayed removal */
		si_nodegc(node, &index->conf);
		si_lock(index);
		rlist_add(&index->gc, &node->gc);
		index->gc_count++;
		si_unlock(index);
	}

	SS_INJECTION(r->i, SS_INJECTION_SI_COMPACTION_2,
	             sr_malfunction("%s", "error injection");
	             return -1);

	/* complete new nodes */
	ss_bufiterref_open(&i, result, sizeof(struct sinode*));
	while (ss_bufiterref_has(&i))
	{
		n = ss_bufiterref_get(&i);
		rc = si_nodecomplete(n, &index->conf);
		if (unlikely(rc == -1))
			return -1;
		SS_INJECTION(r->i, SS_INJECTION_SI_COMPACTION_4,
		             sr_malfunction("%s", "error injection");
		             return -1);
		ss_bufiterref_next(&i);
	}

	/* unlock */
	si_lock(index);
	ss_bufiterref_open(&i, result, sizeof(struct sinode*));
	while (ss_bufiterref_has(&i))
	{
		n = ss_bufiterref_get(&i);
		si_nodeunlock(n);
		ss_bufiterref_next(&i);
	}
	si_unlock(index);
	return 0;
}

static struct sinode *
si_nodenew(struct key_def *key_def)
{
	struct sinode *n = (struct sinode*)malloc(sizeof(struct sinode));
	if (unlikely(n == NULL)) {
		sr_oom();
		return NULL;
	}
	n->recover = 0;
	n->lru = 0;
	n->ac = 0;
	n->flags = 0;
	n->update_time = 0;
	n->used = 0;
	si_branchinit(&n->self);
	n->branch = NULL;
	n->branch_count = 0;
	n->temperature = 0;
	n->temperature_reads = 0;
	n->refs = 0;
	tt_pthread_mutex_init(&n->reflock, NULL);
	ss_fileinit(&n->file);
	sv_indexinit(&n->i0, key_def);
	sv_indexinit(&n->i1, key_def);
	ss_rqinitnode(&n->nodecompact);
	ss_rqinitnode(&n->nodebranch);
	ss_rqinitnode(&n->nodetemp);
	rlist_create(&n->gc);
	rlist_create(&n->commit);
	return n;
}

static int si_nodegc_index(struct runtime *r, struct svindex *i)
{
	sv_indexfree(i, r);
	sv_indexinit(i, i->key_def);
	return 0;
}

static inline int
si_nodeclose(struct sinode *n, struct runtime *r, int gc)
{
	int rcret = 0;

	int rc = ss_fileclose(&n->file);
	if (unlikely(rc == -1)) {
		sr_malfunction("index file '%s' close error: %s",
		               ss_pathof(&n->file.path),
		               strerror(errno));
		rcret = -1;
	}
	if (gc) {
		si_nodegc_index(r, &n->i0);
		si_nodegc_index(r, &n->i1);
	} else {
		sv_indexfree(&n->i0, r);
		sv_indexfree(&n->i1, r);
		tt_pthread_mutex_destroy(&n->reflock);
	}
	return rcret;
}

static inline int
si_noderecover(struct sinode *n, struct runtime *r)
{
	/* recover branches */
	struct sibranch *b = NULL;
	struct sdrecover ri;
	sd_recover_open(&ri, r, &n->file);
	int first = 1;
	int rc;
	while (sd_recover_has(&ri))
	{
		struct sdindexheader *h = sd_recover_get(&ri);
		if (first) {
			b = &n->self;
		} else {
			b = si_branchnew();
			if (unlikely(b == NULL))
				goto e0;
		}
		struct sdindex index;
		sd_indexinit(&index);
		rc = sd_indexcopy(&index, h);
		if (unlikely(rc == -1))
			goto e0;
		si_branchset(b, &index);

		b->next   = n->branch;
		n->branch = b;
		n->branch_count++;

		first = 0;
		sd_recover_next(&ri);
	}
	rc = sd_recover_complete(&ri);
	if (unlikely(rc == -1))
		goto e1;
	sd_recover_close(&ri);

	return 0;
e0:
	if (b && !first)
		si_branchfree(b);
e1:
	sd_recover_close(&ri);
	return -1;
}

static int
si_nodeopen(struct sinode *n, struct runtime *r, struct sspath *path)
{
	int rc = ss_fileopen(&n->file, path->path);
	if (unlikely(rc == -1)) {
		sr_malfunction("index file '%s' open error: %s "
		               "(please ensure storage version compatibility)",
		               ss_pathof(&n->file.path),
		               strerror(errno));
		return -1;
	}
	rc = ss_fileseek(&n->file, n->file.size);
	if (unlikely(rc == -1)) {
		sr_malfunction("index file '%s' seek error: %s",
		               ss_pathof(&n->file.path),
		               strerror(errno));
		return -1;
	}
	rc = si_noderecover(n, r);
	if (unlikely(rc == -1))
		return -1;
	return 0;
}

static int
si_nodecreate(struct sinode *n, struct siconf *scheme,
	      struct sdid *id)
{
	struct sspath path;
	ss_pathcompound(&path, scheme->path, id->parent, id->id,
	                ".index.incomplete");
	int rc = ss_filenew(&n->file, path.path);
	if (unlikely(rc == -1)) {
		sr_malfunction("index file '%s' create error: %s",
		               path.path, strerror(errno));
		return -1;
	}
	return 0;
}

static inline void
si_nodefree_branches(struct sinode *n)
{
	struct sibranch *p = n->branch;
	struct sibranch *next = NULL;
	while (p && p != &n->self) {
		next = p->next;
		si_branchfree(p);
		p = next;
	}
	sd_indexfree(&n->self.index);
}

static int si_nodefree(struct sinode *n, struct runtime *r, int gc)
{
	int rcret = 0;
	int rc;
	if (gc && ss_pathis_set(&n->file.path)) {
		ss_fileadvise(&n->file, 0, 0, n->file.size);
		rc = unlink(ss_pathof(&n->file.path));
		if (unlikely(rc == -1)) {
			sr_malfunction("index file '%s' unlink error: %s",
			               ss_pathof(&n->file.path),
			               strerror(errno));
			rcret = -1;
		}
	}
	si_nodefree_branches(n);
	rc = si_nodeclose(n, r, gc);
	if (unlikely(rc == -1))
		rcret = -1;
	free(n);
	return rcret;
}

static int si_nodeseal(struct sinode *n, struct siconf *scheme)
{
	int rc;
	if (scheme->sync) {
		rc = ss_filesync(&n->file);
		if (unlikely(rc == -1)) {
			sr_malfunction("index file '%s' sync error: %s",
			               ss_pathof(&n->file.path),
			               strerror(errno));
			return -1;
		}
	}
	struct sspath path;
	ss_pathcompound(&path, scheme->path,
	                n->self.id.parent, n->self.id.id,
	                ".index.seal");
	rc = ss_filerename(&n->file, path.path);
	if (unlikely(rc == -1)) {
		sr_malfunction("index file '%s' rename error: %s",
		               ss_pathof(&n->file.path),
		               strerror(errno));
		return -1;
	}
	return 0;
}

static int
si_nodecomplete(struct sinode *n, struct siconf *scheme)
{
	struct sspath path;
	ss_path(&path, scheme->path, n->self.id.id, ".index");
	int rc = ss_filerename(&n->file, path.path);
	if (unlikely(rc == -1)) {
		sr_malfunction("index file '%s' rename error: %s",
		               ss_pathof(&n->file.path),
		               strerror(errno));
	}
	return rc;
}

static int si_nodegc(struct sinode *n, struct siconf *scheme)
{
	struct sspath path;
	ss_path(&path, scheme->path, n->self.id.id, ".index.gc");
	int rc = ss_filerename(&n->file, path.path);
	if (unlikely(rc == -1)) {
		sr_malfunction("index file '%s' rename error: %s",
		               ss_pathof(&n->file.path),
		               strerror(errno));
	}
	return rc;
}

static int si_planinit(struct siplan *p)
{
	p->plan    = SI_NONE;
	p->explain = SI_ENONE;
	p->a       = 0;
	p->b       = 0;
	p->c       = 0;
	p->node    = NULL;
	return 0;
}

static int si_plannerinit(struct siplanner *p, void *i)
{
	int rc = ss_rqinit(&p->compact, 1, 20);
	if (unlikely(rc == -1))
		return -1;
	/* 1Mb step up to 4Gb */
	rc = ss_rqinit(&p->branch, 1024 * 1024, 4000);
	if (unlikely(rc == -1)) {
		ss_rqfree(&p->compact);
		return -1;
	}
	rc = ss_rqinit(&p->temp, 1, 100);
	if (unlikely(rc == -1)) {
		ss_rqfree(&p->compact);
		ss_rqfree(&p->branch);
		return -1;
	}
	p->i = i;
	return 0;
}

static int si_plannerfree(struct siplanner *p)
{
	ss_rqfree(&p->compact);
	ss_rqfree(&p->branch);
	ss_rqfree(&p->temp);
	return 0;
}

static int si_plannerupdate(struct siplanner *p, int mask, struct sinode *n)
{
	if (mask & SI_BRANCH)
		ss_rqupdate(&p->branch, &n->nodebranch, n->used);
	if (mask & SI_COMPACT)
		ss_rqupdate(&p->compact, &n->nodecompact, n->branch_count);
	if (mask & SI_TEMP)
		ss_rqupdate(&p->temp, &n->nodetemp, n->temperature);
	return 0;
}

static int si_plannerremove(struct siplanner *p, int mask, struct sinode *n)
{
	if (mask & SI_BRANCH)
		ss_rqdelete(&p->branch, &n->nodebranch);
	if (mask & SI_COMPACT)
		ss_rqdelete(&p->compact, &n->nodecompact);
	if (mask & SI_TEMP)
		ss_rqdelete(&p->temp, &n->nodetemp);
	return 0;
}

static inline int
si_plannerpeek_checkpoint(struct siplanner *p, struct siplan *plan)
{
	/* try to peek a node which has min
	 * lsn <= required value
	*/
	int rc_inprogress = 0;
	struct sinode *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&p->branch, pn))) {
		n = container_of(pn, struct sinode, nodebranch);
		if (n->i0.lsnmin <= plan->a) {
			if (n->flags & SI_LOCK) {
				rc_inprogress = 2;
				continue;
			}
			goto match;
		}
	}
	if (rc_inprogress)
		plan->explain = SI_ERETRY;
	return rc_inprogress;
match:
	si_nodelock(n);
	plan->explain = SI_ENONE;
	plan->node = n;
	return 1;
}

static inline int
si_plannerpeek_branch(struct siplanner *p, struct siplan *plan)
{
	/* try to peek a node with a biggest in-memory index */
	struct sinode *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&p->branch, pn))) {
		n = container_of(pn, struct sinode, nodebranch);
		if (n->flags & SI_LOCK)
			continue;
		if (n->used >= plan->a)
			goto match;
		return 0;
	}
	return 0;
match:
	si_nodelock(n);
	plan->explain = SI_EINDEX_SIZE;
	plan->node = n;
	return 1;
}

static inline int
si_plannerpeek_age(struct siplanner *p, struct siplan *plan)
{
	/* try to peek a node with update >= a and in-memory
	 * index size >= b */

	/* full scan */
	uint64_t now = clock_monotonic64();
	struct sinode *n = NULL;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&p->branch, pn))) {
		n = container_of(pn, struct sinode, nodebranch);
		if (n->flags & SI_LOCK)
			continue;
		if (n->used >= plan->b && ((now - n->update_time) >= plan->a))
			goto match;
	}
	return 0;
match:
	si_nodelock(n);
	plan->explain = SI_EINDEX_AGE;
	plan->node = n;
	return 1;
}

static inline int
si_plannerpeek_compact(struct siplanner *p, struct siplan *plan)
{
	/* try to peek a node with a biggest number
	 * of branches */
	struct sinode *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&p->compact, pn))) {
		n = container_of(pn, struct sinode, nodecompact);
		if (n->flags & SI_LOCK)
			continue;
		if (n->branch_count >= plan->a)
			goto match;
		return 0;
	}
	return 0;
match:
	si_nodelock(n);
	plan->explain = SI_EBRANCH_COUNT;
	plan->node = n;
	return 1;
}

static inline int
si_plannerpeek_compact_temperature(struct siplanner *p, struct siplan *plan)
{
	/* try to peek a hottest node with number of
	 * branches >= watermark */
	struct sinode *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&p->temp, pn))) {
		n = container_of(pn, struct sinode, nodetemp);
		if (n->flags & SI_LOCK)
			continue;
		if (n->branch_count >= plan->a)
			goto match;
		return 0;
	}
	return 0;
match:
	si_nodelock(n);
	plan->explain = SI_ENONE;
	plan->node = n;
	return 1;
}

static inline int
si_plannerpeek_gc(struct siplanner *p, struct siplan *plan)
{
	/* try to peek a node with a biggest number
	 * of branches which is ready for gc */
	int rc_inprogress = 0;
	struct sinode *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&p->compact, pn))) {
		n = container_of(pn, struct sinode, nodecompact);
		struct sdindexheader *h = n->self.index.h;
		if (likely(h->dupkeys == 0) || (h->dupmin >= plan->a))
			continue;
		uint32_t used = (h->dupkeys * 100) / h->keys;
		if (used >= plan->b) {
			if (n->flags & SI_LOCK) {
				rc_inprogress = 2;
				continue;
			}
			goto match;
		}
	}
	if (rc_inprogress)
		plan->explain = SI_ERETRY;
	return rc_inprogress;
match:
	si_nodelock(n);
	plan->explain = SI_ENONE;
	plan->node = n;
	return 1;
}

static inline int
si_plannerpeek_lru(struct siplanner *p, struct siplan *plan)
{
	struct vinyl_index *index = p->i;
	if (likely(! index->conf.lru))
		return 0;
	if (! index->lru_run_lsn) {
		index->lru_run_lsn = si_lru_vlsn_of(index);
		if (likely(index->lru_run_lsn == 0))
			return 0;
	}
	int rc_inprogress = 0;
	struct sinode *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&p->compact, pn))) {
		n = container_of(pn, struct sinode, nodecompact);
		struct sdindexheader *h = n->self.index.h;
		if (h->lsnmin < index->lru_run_lsn) {
			if (n->flags & SI_LOCK) {
				rc_inprogress = 2;
				continue;
			}
			goto match;
		}
	}
	if (rc_inprogress)
		plan->explain = SI_ERETRY;
	else
		index->lru_run_lsn = 0;
	return rc_inprogress;
match:
	si_nodelock(n);
	plan->explain = SI_ENONE;
	plan->node = n;
	return 1;
}

static inline int
si_plannerpeek_shutdown(struct siplanner *p, struct siplan *plan)
{
	struct vinyl_index *index = p->i;
	int status = sr_status(&index->status);
	switch (status) {
	case SR_DROP:
		if (index->refs > 0)
			return 2;
		plan->plan = SI_DROP;
		return 1;
	case SR_SHUTDOWN:
		if (index->refs > 0)
			return 2;
		plan->plan = SI_SHUTDOWN;
		return 1;
	}
	return 0;
}

static inline int
si_plannerpeek_nodegc(struct siplanner *p, struct siplan *plan)
{
	struct vinyl_index *index = p->i;
	if (likely(index->gc_count == 0))
		return 0;
	int rc_inprogress = 0;
	struct sinode *n;
	rlist_foreach_entry(n, &index->gc, gc) {
		if (likely(si_noderefof(n) == 0)) {
			rlist_del(&n->gc);
			index->gc_count--;
			plan->explain = SI_ENONE;
			plan->node = n;
			return 1;
		} else {
			rc_inprogress = 2;
		}
	}
	return rc_inprogress;
}

static int si_planner(struct siplanner *p, struct siplan *plan)
{
	switch (plan->plan) {
	case SI_BRANCH:
	case SI_COMPACT_INDEX:
		return si_plannerpeek_branch(p, plan);
	case SI_COMPACT:
		if (plan->b == 1)
			return si_plannerpeek_compact_temperature(p, plan);
		return si_plannerpeek_compact(p, plan);
	case SI_NODEGC:
		return si_plannerpeek_nodegc(p, plan);
	case SI_GC:
		return si_plannerpeek_gc(p, plan);
	case SI_CHECKPOINT:
		return si_plannerpeek_checkpoint(p, plan);
	case SI_AGE:
		return si_plannerpeek_age(p, plan);
	case SI_LRU:
		return si_plannerpeek_lru(p, plan);
	case SI_SHUTDOWN:
	case SI_DROP:
		return si_plannerpeek_shutdown(p, plan);
	}
	return -1;
}

static int si_profilerbegin(struct siprofiler *p, struct vinyl_index *i)
{
	memset(p, 0, sizeof(*p));
	p->i = i;
	p->temperature_min = 100;
	si_lock(i);
	return 0;
}

static int si_profilerend(struct siprofiler *p)
{
	si_unlock(p->i);
	return 0;
}

static void
si_profiler_histogram_branch(struct siprofiler *p)
{
	/* prepare histogram string */
	int size = 0;
	int i = 0;
	while (i < 20) {
		if (p->histogram_branch[i] == 0) {
			i++;
			continue;
		}
		size += snprintf(p->histogram_branch_sz + size,
		                 sizeof(p->histogram_branch_sz) - size,
		                 "[%d]:%d ", i,
		                 p->histogram_branch[i]);
		i++;
	}
	if (p->histogram_branch_20plus) {
		size += snprintf(p->histogram_branch_sz + size,
		                 sizeof(p->histogram_branch_sz) - size,
		                 "[20+]:%d ",
		                 p->histogram_branch_20plus);
	}
	if (size == 0)
		p->histogram_branch_ptr = NULL;
	else {
		p->histogram_branch_ptr = p->histogram_branch_sz;
	}
}

static void
si_profiler_histogram_temperature(struct siprofiler *p)
{
	/* build histogram */
	static struct {
		int nodes;
		int branches;
	} h[101];
	memset(h, 0, sizeof(h));
	struct sinode *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&p->i->p.temp, pn)))
	{
		n = container_of(pn, struct sinode, nodetemp);
		h[pn->v].nodes++;
		h[pn->v].branches += n->branch_count;
	}

	/* prepare histogram string */
	int count = 0;
	int i = 100;
	int size = 0;
	while (i >= 0 && count < 10) {
		if (h[i].nodes == 0) {
			i--;
			continue;
		}
		size += snprintf(p->histogram_temperature_sz + size,
		                 sizeof(p->histogram_temperature_sz) - size,
		                 "[%d]:%d-%d ", i,
		                 h[i].nodes, h[i].branches);
		i--;
		count++;
	}
	if (size == 0)
		p->histogram_temperature_ptr = NULL;
	else {
		p->histogram_temperature_ptr = p->histogram_temperature_sz;
	}
}

static int si_profiler(struct siprofiler *p)
{
	uint32_t temperature_total = 0;
	uint64_t memory_used = 0;
	struct sinode *n = sinode_tree_first(&p->i->tree);
	while (n) {
		if (p->temperature_max < n->temperature)
			p->temperature_max = n->temperature;
		if (p->temperature_min > n->temperature)
			p->temperature_min = n->temperature;
		temperature_total += n->temperature;
		p->total_node_count++;
		p->count += n->i0.tree.size;
		p->count += n->i1.tree.size;
		p->total_branch_count += n->branch_count;
		if (p->total_branch_max < n->branch_count)
			p->total_branch_max = n->branch_count;
		if (n->branch_count < 20)
			p->histogram_branch[n->branch_count]++;
		else
			p->histogram_branch_20plus++;
		memory_used += sv_indexused(&n->i0);
		memory_used += sv_indexused(&n->i1);
		struct sibranch *b = n->branch;
		while (b) {
			p->count += b->index.h->keys;
			p->count_dup += b->index.h->dupkeys;
			int indexsize = sd_indexsize_ext(b->index.h);
			p->total_snapshot_size += indexsize;
			p->total_node_size += indexsize + b->index.h->total;
			p->total_node_origin_size += indexsize + b->index.h->totalorigin;
			p->total_page_count += b->index.h->count;
			b = b->next;
		}
		n = sinode_tree_next(&p->i->tree, n);
	}
	if (p->total_node_count > 0) {
		p->total_branch_avg =
			p->total_branch_count / p->total_node_count;
		p->temperature_avg =
			temperature_total / p->total_node_count;
	}
	p->memory_used = memory_used;
	p->read_disk  = p->i->read_disk;
	p->read_cache = p->i->read_cache;

	si_profiler_histogram_branch(p);
	si_profiler_histogram_temperature(p);
	return 0;
}

static int
si_readopen(struct siread *q, struct vinyl_index *index, struct sicache *c,
	    enum vinyl_order o, uint64_t vlsn, void *key, uint32_t keysize)
{
	q->order       = o;
	q->key         = key;
	q->keysize     = keysize;
	q->vlsn        = vlsn;
	q->index       = index;
	q->cache       = c;
	q->has         = 0;
	q->upsert_v    = NULL;
	q->upsert_eq   = 0;
	q->cache_only  = 0;
	q->oldest_only = 0;
	q->read_disk   = 0;
	q->read_cache  = 0;
	q->result      = NULL;
	sv_mergeinit(&q->merge, index->key_def);
	si_lock(index);
	return 0;
}

static int
si_readclose(struct siread *q)
{
	si_unlock(q->index);
	sv_mergefree(&q->merge);
	return 0;
}

static inline int
si_readdup(struct siread *q, struct sv *result)
{
	struct vinyl_tuple *v;
	if (likely(result->i == &sv_tupleif)) {
		v = result->v;
		vinyl_tuple_ref(v);
	} else {
		v = vinyl_tuple_from_sv(q->index->r, result);
		if (unlikely(v == NULL))
			return sr_oom();
	}
	q->result = v;
	return 1;
}

static inline void
si_readstat(struct siread *q, int cache, struct sinode *n, uint32_t reads)
{
	struct vinyl_index *index = q->index;
	if (cache) {
		index->read_cache += reads;
		q->read_cache += reads;
	} else {
		index->read_disk += reads;
		q->read_disk += reads;
	}
	/* update temperature */
	if (index->conf.temperature) {
		n->temperature_reads += reads;
		uint64_t total = index->read_disk + index->read_cache;
		if (unlikely(total == 0))
			return;
		n->temperature = (n->temperature_reads * 100ULL) / total;
		si_plannerupdate(&q->index->p, SI_TEMP, n);
	}
}

static inline int
si_getresult(struct siread *q, struct sv *v, int compare)
{
	int rc;
	if (compare) {
		rc = sf_compare(q->merge.key_def, sv_pointer(v), q->key);
		if (unlikely(rc != 0))
			return 0;
	}
	if (unlikely(q->has))
		return sv_lsn(v) > q->vlsn;
	if (unlikely(sv_is(v, SVDELETE)))
		return 2;
	rc = si_readdup(q, v);
	if (unlikely(rc == -1))
		return -1;
	return 1;
}

static inline int
si_getindex(struct siread *q, struct sinode *n)
{
	struct svindex *second;
	struct svindex *first = si_nodeindex_priority(n, &second);

	uint64_t lsn = q->has ? UINT64_MAX : q->vlsn;
	struct svref *ref = sv_indexfind(first, q->key, q->keysize, lsn);
	if (ref == NULL && second != NULL)
		ref = sv_indexfind(second, q->key, q->keysize, lsn);
	if (ref == NULL)
		return 0;

	si_readstat(q, 1, n, 1);
	struct sv vret;
	sv_from_tuple(&vret, ref->v);
	return si_getresult(q, &vret, 0);
}

static inline int
si_getbranch(struct siread *q, struct sinode *n, struct sicachebranch *c)
{
	struct sibranch *b = c->branch;
	struct siconf *conf= &q->index->conf;
	int rc;
	/* choose compression type */
	int compression;
	struct ssfilterif *compression_if;
	compression    = conf->compression;
	compression_if = conf->compression_if;
	struct sdreadarg arg = {
		.index           = &b->index,
		.buf             = &c->buf_a,
		.buf_xf          = &c->buf_b,
		.buf_read        = &q->index->readbuf,
		.index_iter      = &c->index_iter,
		.page_iter       = &c->page_iter,
		.use_compression = compression,
		.compression_if  = compression_if,
		.has             = q->has,
		.has_vlsn        = q->vlsn,
		.o               = VINYL_GE,
		.file            = &n->file,
		.a               = q->merge.a,
		.key_def          = q->merge.key_def
	};
	rc = sd_read_open(&c->i, &arg, q->key, q->keysize);
	int reads = sd_read_stat(&c->i);
	si_readstat(q, 0, n, reads);
	if (unlikely(rc <= 0))
		return rc;
	/* prepare sources */
	sv_mergereset(&q->merge);
	sv_mergeadd(&q->merge, &c->i);
	struct svmergeiter im;
	sv_mergeiter_open(&im, &q->merge, VINYL_GE);
	uint64_t vlsn = q->vlsn;
	if (unlikely(q->has))
		vlsn = UINT64_MAX;
	struct svreaditer ri;
	sv_readiter_open(&ri, &im, &q->index->u, vlsn, 1);
	struct sv *v = sv_readiter_get(&ri);
	if (unlikely(v == NULL))
		return 0;
	return si_getresult(q, v, 1);
}

static inline int
si_get(struct siread *q)
{
	assert(q->key != NULL);
	struct siiter ii;
	si_iter_open(&ii, q->index, VINYL_GE, q->key, q->keysize);
	struct sinode *node;
	node = si_iter_get(&ii);
	assert(node != NULL);

	/* search in memory */
	int rc;
	rc = si_getindex(q, node);
	if (rc != 0)
		return rc;
	if (q->cache_only)
		return 2;
	struct sinodeview view;
	si_nodeview_open(&view, node);
	rc = si_cachevalidate(q->cache, node);
	if (unlikely(rc == -1)) {
		sr_oom();
		return -1;
	}
	si_unlock(q->index);

	/* search on disk */
	struct svmerge *m = &q->merge;
	rc = sv_mergeprepare(m, 1);
	assert(rc == 0);
	struct sicachebranch *b;
	if (q->oldest_only) {
		b = si_cacheseek(q->cache, &node->self);
		assert(b != NULL);
		rc = si_getbranch(q, node, b);
	} else {
		b = q->cache->branch;
		while (b && b->branch) {
			rc = si_getbranch(q, node, b);
			if (rc != 0)
				break;
			b = b->next;
		}
	}

	si_lock(q->index);
	si_nodeview_close(&view);
	return rc;
}

static inline int
si_rangebranch(struct siread *q, struct sinode *n,
	       struct sibranch *b, struct svmerge *m)
{
	struct sicachebranch *c = si_cachefollow(q->cache, b);
	assert(c->branch == b);
	/* iterate cache */
	if (sd_read_has(&c->i)) {
		struct svmergesrc *s = sv_mergeadd(m, &c->i);
		si_readstat(q, 1, n, 1);
		s->ptr = c;
		return 1;
	}
	if (c->open) {
		return 1;
	}
	if (q->cache_only) {
		return 2;
	}
	c->open = 1;
	/* choose compression type */
	struct siconf *conf = &q->index->conf;
	int compression;
	struct ssfilterif *compression_if;
	compression    = conf->compression;
	compression_if = conf->compression_if;
	struct sdreadarg arg = {
		.index           = &b->index,
		.buf             = &c->buf_a,
		.buf_xf          = &c->buf_b,
		.buf_read        = &q->index->readbuf,
		.index_iter      = &c->index_iter,
		.page_iter       = &c->page_iter,
		.use_compression = compression,
		.compression_if  = compression_if,
		.has             = 0,
		.has_vlsn        = 0,
		.o               = q->order,
		.file            = &n->file,
		.a               = q->merge.a,
		.key_def          = q->merge.key_def
	};
	int rc = sd_read_open(&c->i, &arg, q->key, q->keysize);
	int reads = sd_read_stat(&c->i);
	si_readstat(q, 0, n, reads);
	if (unlikely(rc == -1))
		return -1;
	if (unlikely(! sd_read_has(&c->i)))
		return 0;
	struct svmergesrc *s = sv_mergeadd(m, &c->i);
	s->ptr = c;
	return 1;
}

static inline int
si_range(struct siread *q)
{
	assert(q->has == 0);

	struct siiter ii;
	si_iter_open(&ii, q->index, q->order, q->key, q->keysize);
	struct sinode *node;
next_node:
	node = si_iter_get(&ii);
	if (unlikely(node == NULL))
		return 0;

	/* prepare sources */
	struct svmerge *m = &q->merge;
	int count = node->branch_count + 2 + 1;
	int rc = sv_mergeprepare(m, count);
	if (unlikely(rc == -1)) {
		diag_clear(diag_get());
		return -1;
	}

	/* external source (upsert) */
	struct svmergesrc *s;
	struct ssbuf upbuf;
	if (unlikely(q->upsert_v && q->upsert_v->v)) {
		ss_bufinit(&upbuf);
		ss_bufadd(&upbuf, (void*)&q->upsert_v, sizeof(struct sv*));
		s = sv_mergeadd(m, NULL);
		ss_bufiterref_open(&s->src, &upbuf, sizeof(struct sv*));
	}

	/* in-memory indexes */
	struct svindex *second;
	struct svindex *first = si_nodeindex_priority(node, &second);
	if (first->tree.size) {
		s = sv_mergeadd(m, NULL);
		sv_indexiter_open(&s->src, first, q->order,
				  q->key, q->keysize);
	}
	if (unlikely(second && second->tree.size)) {
		s = sv_mergeadd(m, NULL);
		sv_indexiter_open(&s->src, second, q->order,
				  q->key, q->keysize);
	}

	/* cache and branches */
	rc = si_cachevalidate(q->cache, node);
	if (unlikely(rc == -1)) {
		sr_oom();
		return -1;
	}

	if (q->oldest_only) {
		rc = si_rangebranch(q, node, &node->self, m);
		if (unlikely(rc == -1 || rc == 2))
			return rc;
	} else {
		struct sibranch *b = node->branch;
		while (b) {
			rc = si_rangebranch(q, node, b, m);
			if (unlikely(rc == -1 || rc == 2))
				return rc;
			b = b->next;
		}
	}

	/* merge and filter data stream */
	struct svmergeiter im;
	sv_mergeiter_open(&im, m, q->order);
	struct svreaditer ri;
	sv_readiter_open(&ri, &im, &q->index->u, q->vlsn, q->upsert_eq);
	struct sv *v = sv_readiter_get(&ri);
	if (unlikely(v == NULL)) {
		sv_mergereset(&q->merge);
		si_iter_next(&ii);
		goto next_node;
	}

	rc = 1;
	/* convert upsert search to VINYL_EQ */
	if (q->upsert_eq) {
		int res = sf_compare(q->merge.key_def, sv_pointer(v), q->key);
		rc = res == 0;
		if (res == 0 && sv_is(v, SVDELETE))
			rc = 0; /* that is not what we wanted to find */
	}
	if (likely(rc == 1)) {
		if (unlikely(si_readdup(q, v) == -1))
			return -1;
	}

	/* skip a possible duplicates from data sources */
	sv_readiter_forward(&ri);
	return rc;
}

static int si_read(struct siread *q)
{
	switch (q->order) {
	case VINYL_EQ:
		return si_get(q);
	case VINYL_LT:
	case VINYL_LE:
	case VINYL_GT:
	case VINYL_GE:
		return si_range(q);
	default:
		break;
	}
	return -1;
}

static int
si_readcommited(struct vinyl_index *index, struct sv *v)
{
	/* search node index */
	struct siiter ii;
	si_iter_open(&ii, index, VINYL_GE, sv_pointer(v), sv_size(v));
	struct sinode *node;
	node = si_iter_get(&ii);
	assert(node != NULL);

	uint64_t lsn = sv_lsn(v);
	/* search in-memory */
	struct svindex *second;
	struct svindex *first = si_nodeindex_priority(node, &second);
	struct svref *ref = sv_indexfind(first, sv_pointer(v),
					 sv_size(v), UINT64_MAX);
	if ((ref == NULL || ref->v->lsn < lsn) && second != NULL)
		ref = sv_indexfind(second, sv_pointer(v),
				   sv_size(v), UINT64_MAX);
	if (ref != NULL && ref->v->lsn >= lsn)
		return 1;

	/* search branches */
	struct sibranch *b;
	for (b = node->branch; b; b = b->next)
	{
		struct sdindexiter ii;
		sd_indexiter_open(&ii, index->key_def, &b->index, VINYL_GE,
				  sv_pointer(v), sv_size(v));
		struct sdindexpage *page = sd_indexiter_get(&ii);
		if (page == NULL)
			continue;
		if (page->lsnmax >= lsn)
			return 1;
	}
	return 0;
}

/*
	repository recover states
	-------------------------

	compaction

	000000001.000000002.index.incomplete  (1)
	000000001.000000002.index.seal        (2)
	000000002.index                       (3)
	000000001.000000003.index.incomplete
	000000001.000000003.index.seal
	000000003.index
	(4)

	1. remove incomplete, mark parent as having incomplete
	2. find parent, mark as having seal
	3. add
	4. recover:
		a. if parent has incomplete and seal - remove both
		b. if parent has incomplete - remove incomplete
		c. if parent has seal - remove parent, complete seal

	see: snapshot recover
	see: key_def recover
	see: test/crash/durability.test.c
*/

static struct sinode *
si_bootstrap(struct vinyl_index *index, uint64_t parent)
{
	struct runtime *r = index->r;
	/* create node */
	struct sinode *n = si_nodenew(index->key_def);
	if (unlikely(n == NULL))
		return NULL;
	struct sdid id = {
		.parent = parent,
		.flags  = 0,
		.id     = sr_seq(r->seq, SR_NSNNEXT)
	};
	int rc;
	rc = si_nodecreate(n, &index->conf, &id);
	if (unlikely(rc == -1))
		goto e0;
	n->branch = &n->self;
	n->branch_count++;

	/* create index with one empty page */
	struct sdindex sdindex;
	sd_indexinit(&sdindex);
	rc = sd_indexbegin(&sdindex);
	if (unlikely(rc == -1))
		goto e0;

	struct sdbuild build;
	sd_buildinit(&build);
	rc = sd_buildbegin(&build, index->key_def,
	                   index->conf.node_page_checksum,
	                   index->conf.compression,
	                   index->conf.compression_if);
	if (unlikely(rc == -1))
		goto e1;
	sd_buildend(&build);
	rc = sd_indexadd(&sdindex, &build, sizeof(struct sdseal));
	if (unlikely(rc == -1))
		goto e1;

	/* write seal */
	uint64_t seal = n->file.size;
	rc = sd_writeseal(&n->file);
	if (unlikely(rc == -1))
		goto e1;
	/* write page */
	rc = sd_writepage(&n->file, &build);
	if (unlikely(rc == -1))
		goto e1;
	rc = sd_indexcommit(&sdindex, &id, n->file.size);
	if (unlikely(rc == -1))
		goto e1;
	/* write index */
	rc = sd_writeindex(&n->file, &sdindex);
	if (unlikely(rc == -1))
		goto e1;
	/* close seal */
	rc = sd_seal(&n->file, &sdindex, seal);
	if (unlikely(rc == -1))
		goto e1;
	si_branchset(&n->self, &sdindex);

	sd_buildcommit(&build);
	sd_buildfree(&build);
	return n;
e1:
	sd_indexfree(&sdindex);
	sd_buildfree(&build);
e0:
	si_nodefree(n, r, 0);
	return NULL;
}

static inline int
si_deploy(struct vinyl_index *index, struct runtime *r, int create_directory)
{
	/* create directory */
	int rc;
	if (likely(create_directory)) {
		rc = mkdir(index->conf.path, 0755);
		if (unlikely(rc == -1)) {
			sr_malfunction("directory '%s' create error: %s",
			               index->conf.path, strerror(errno));
			return -1;
		}
	}
	/* create initial node */
	struct sinode *n = si_bootstrap(index, 0);
	if (unlikely(n == NULL))
		return -1;
	SS_INJECTION(r->i, SS_INJECTION_SI_RECOVER_0,
	             si_nodefree(n, r, 0);
	             sr_malfunction("%s", "error injection");
	             return -1);
	rc = si_nodecomplete(n, &index->conf);
	if (unlikely(rc == -1)) {
		si_nodefree(n, r, 1);
		return -1;
	}
	si_insert(index, n);
	si_plannerupdate(&index->p, SI_COMPACT|SI_BRANCH|SI_TEMP, n);
	index->size = si_nodesize(n);
	return 1;
}

static inline int64_t
si_processid(char **str)
{
	char *s = *str;
	size_t v = 0;
	while (*s && *s != '.') {
		if (unlikely(! isdigit(*s)))
			return -1;
		v = (v * 10) + *s - '0';
		s++;
	}
	*str = s;
	return v;
}

static inline int
si_process(char *name, uint64_t *nsn, uint64_t *parent)
{
	/* id.index */
	/* id.id.index.incomplete */
	/* id.id.index.seal */
	/* id.id.index.gc */
	char *token = name;
	int64_t id = si_processid(&token);
	if (unlikely(id == -1))
		return -1;
	*parent = id;
	*nsn = id;
	if (strcmp(token, ".index") == 0)
		return SI_RDB;
	else
	if (strcmp(token, ".index.gc") == 0)
		return SI_RDB_REMOVE;
	if (unlikely(*token != '.'))
		return -1;
	token++;
	id = si_processid(&token);
	if (unlikely(id == -1))
		return -1;
	*nsn = id;
	if (strcmp(token, ".index.incomplete") == 0)
		return SI_RDB_DBI;
	else
	if (strcmp(token, ".index.seal") == 0)
		return SI_RDB_DBSEAL;
	return -1;
}

static inline int
si_trackdir(struct sitrack *track, struct runtime *r, struct vinyl_index *i)
{
	DIR *dir = opendir(i->conf.path);
	if (unlikely(dir == NULL)) {
		sr_malfunction("directory '%s' open error: %s",
		               i->conf.path, strerror(errno));
		return -1;
	}
	struct dirent *de;
	while ((de = readdir(dir))) {
		if (unlikely(de->d_name[0] == '.'))
			continue;
		uint64_t id_parent = 0;
		uint64_t id = 0;
		int rc = si_process(de->d_name, &id, &id_parent);
		if (unlikely(rc == -1))
			continue; /* skip unknown file */
		si_tracknsn(track, id_parent);
		si_tracknsn(track, id);

		struct sinode *head, *node;
		struct sspath path;
		switch (rc) {
		case SI_RDB_DBI:
		case SI_RDB_DBSEAL: {
			/* find parent node and mark it as having
			 * incomplete compaction process */
			head = si_trackget(track, id_parent);
			if (likely(head == NULL)) {
				head = si_nodenew(i->key_def);
				if (unlikely(head == NULL))
					goto error;
				head->self.id.id = id_parent;
				head->recover = SI_RDB_UNDEF;
				si_trackset(track, head);
			}
			head->recover |= rc;
			/* remove any incomplete file made during compaction */
			if (rc == SI_RDB_DBI) {
				ss_pathcompound(&path, i->conf.path, id_parent, id,
				                ".index.incomplete");
				rc = unlink(path.path);
				if (unlikely(rc == -1)) {
					sr_malfunction("index file '%s' unlink error: %s",
					               path.path, strerror(errno));
					goto error;
				}
				continue;
			}
			assert(rc == SI_RDB_DBSEAL);
			/* recover 'sealed' node */
			node = si_nodenew(i->key_def);
			if (unlikely(node == NULL))
				goto error;
			node->recover = SI_RDB_DBSEAL;
			ss_pathcompound(&path, i->conf.path, id_parent, id,
			                ".index.seal");
			rc = si_nodeopen(node, r, &path);
			if (unlikely(rc == -1)) {
				si_nodefree(node, r, 0);
				goto error;
			}
			si_trackset(track, node);
			si_trackmetrics(track, node);
			continue;
		}
		case SI_RDB_REMOVE:
			ss_path(&path, i->conf.path, id, ".index.gc");
			rc = unlink(ss_pathof(&path));
			if (unlikely(rc == -1)) {
				sr_malfunction("index file '%s' unlink error: %s",
				               ss_pathof(&path), strerror(errno));
				goto error;
			}
			continue;
		}
		assert(rc == SI_RDB);

		head = si_trackget(track, id);
		if (head != NULL && (head->recover & SI_RDB)) {
			/* loaded by snapshot */
			continue;
		}

		/* recover node */
		node = si_nodenew(i->key_def);
		if (unlikely(node == NULL))
			goto error;
		node->recover = SI_RDB;
		ss_path(&path, i->conf.path, id, ".index");
		rc = si_nodeopen(node, r, &path);
		if (unlikely(rc == -1)) {
			si_nodefree(node, r, 0);
			goto error;
		}
		si_trackmetrics(track, node);

		/* track node */
		if (likely(head == NULL)) {
			si_trackset(track, node);
		} else {
			/* replace a node previously created by a
			 * incomplete compaction */
			si_trackreplace(track, head, node);
			head->recover &= ~SI_RDB_UNDEF;
			node->recover |= head->recover;
			si_nodefree(head, r, 0);
		}
	}
	closedir(dir);
	return 0;
error:
	closedir(dir);
	return -1;
}

static inline int
si_trackvalidate(struct sitrack *track, struct ssbuf *buf, struct vinyl_index *i)
{
	ss_bufreset(buf);
	struct sinode *n = sinode_id_tree_last(&track->tree);
	while (n) {
		switch (n->recover) {
		case SI_RDB|SI_RDB_DBI|SI_RDB_DBSEAL|SI_RDB_REMOVE:
		case SI_RDB|SI_RDB_DBSEAL|SI_RDB_REMOVE:
		case SI_RDB|SI_RDB_REMOVE:
		case SI_RDB_UNDEF|SI_RDB_DBSEAL|SI_RDB_REMOVE:
		case SI_RDB|SI_RDB_DBI|SI_RDB_DBSEAL:
		case SI_RDB|SI_RDB_DBI:
		case SI_RDB:
		case SI_RDB|SI_RDB_DBSEAL:
		case SI_RDB_UNDEF|SI_RDB_DBSEAL: {
			/* match and remove any leftover ancestor */
			struct sinode *ancestor = si_trackget(track, n->self.id.parent);
			if (ancestor && (ancestor != n))
				ancestor->recover |= SI_RDB_REMOVE;
			break;
		}
		case SI_RDB_DBSEAL: {
			/* find parent */
			struct sinode *parent = si_trackget(track, n->self.id.parent);
			if (parent) {
				/* schedule node for removal, if has incomplete merges */
				if (parent->recover & SI_RDB_DBI)
					n->recover |= SI_RDB_REMOVE;
				else
					parent->recover |= SI_RDB_REMOVE;
			}
			if (! (n->recover & SI_RDB_REMOVE)) {
				/* complete node */
				int rc = si_nodecomplete(n, &i->conf);
				if (unlikely(rc == -1))
					return -1;
				n->recover = SI_RDB;
			}
			break;
		}
		default:
			/* corrupted states */
			return sr_malfunction("corrupted index repository: %s",
			                      i->conf.path);
		}
		n = sinode_id_tree_prev(&track->tree, n);
	}
	return 0;
}

static inline int
si_recovercomplete(struct sitrack *track, struct runtime *r, struct vinyl_index *index, struct ssbuf *buf)
{
	/* prepare and build primary index */
	ss_bufreset(buf);
	struct sinode *n = sinode_id_tree_first(&track->tree);
	while (n) {
		int rc = ss_bufadd(buf, &n, sizeof(struct sinode*));
		if (unlikely(rc == -1))
			return sr_oom();
		n = sinode_id_tree_next(&track->tree, n);
	}
	struct ssiter i;
	ss_bufiterref_open(&i, buf, sizeof(struct sinode*));
	while (ss_bufiterref_has(&i))
	{
		struct sinode *n = ss_bufiterref_get(&i);
		if (n->recover & SI_RDB_REMOVE) {
			int rc = si_nodefree(n, r, 1);
			if (unlikely(rc == -1))
				return -1;
			ss_bufiterref_next(&i);
			continue;
		}
		n->recover = SI_RDB;
		si_insert(index, n);
		si_plannerupdate(&index->p, SI_COMPACT|SI_BRANCH|SI_TEMP, n);
		ss_bufiterref_next(&i);
	}
	return 0;
}

static inline void
si_recoversize(struct vinyl_index *i)
{
	struct sinode *n = sinode_tree_first(&i->tree);
	while (n) {
		i->size += si_nodesize(n);
		n = sinode_tree_next(&i->tree, n);
	}
}

static inline int
si_recoverindex(struct vinyl_index *index, struct runtime *r)
{
	struct sitrack track;
	si_trackinit(&track);
	struct ssbuf buf;
	ss_bufinit(&buf);
	int rc;
	rc = si_trackdir(&track, r, index);
	if (unlikely(rc == -1))
		goto error;
	if (unlikely(track.count == 0))
		return 1;
	rc = si_trackvalidate(&track, &buf, index);
	if (unlikely(rc == -1))
		goto error;
	rc = si_recovercomplete(&track, r, index, &buf);
	if (unlikely(rc == -1))
		goto error;
	/* set actual metrics */
	if (track.nsn > r->seq->nsn)
		r->seq->nsn = track.nsn;
	if (track.lsn > r->seq->lsn)
		r->seq->lsn = track.lsn;
	si_recoversize(index);
	ss_buffree(&buf);
	return 0;
error:
	ss_buffree(&buf);
	si_trackfree(&track, r);
	return -1;
}

static inline int
si_recoverdrop(struct vinyl_index *i)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/drop", i->conf.path);
	int rc = path_exists(path);
	if (likely(! rc))
		return 0;
	rc = si_droprepository(i->conf.path, 0);
	if (unlikely(rc == -1))
		return -1;
	return 1;
}

static int si_recover(struct vinyl_index *i)
{
	struct runtime *r = i->r;
	int exist = path_exists(i->conf.path);
	if (exist == 0)
		goto deploy;
	int rc = si_recoverdrop(i);
	switch (rc) {
	case -1: return -1;
	case  1: goto deploy;
	}
	if (unlikely(rc == -1))
		return -1;
	rc = si_recoverindex(i, r);
	if (likely(rc <= 0))
		return rc;
deploy:
	return si_deploy(i, r, !exist);
}

static void si_confinit(struct siconf *s)
{
	memset(s, 0, sizeof(*s));
	sr_version(&s->version);
	sr_version_storage(&s->version_storage);
}

static void si_conffree(struct siconf *s)
{
	if (s->name) {
		free(s->name);
		s->name = NULL;
	}
	if (s->path) {
		free(s->path);
		s->path = NULL;
	}
	if (s->compression_sz) {
		free(s->compression_sz);
		s->compression_sz = NULL;
	}
}

static void si_begin(struct sitx *x, struct vinyl_index *index)
{
	x->index = index;
	rlist_create(&x->nodelist);
	si_lock(index);
}

static void si_commit(struct sitx *x)
{
	/* reschedule nodes */
	struct sinode *node, *n;
	rlist_foreach_entry_safe(node, &x->nodelist, commit, n) {
		rlist_create(&node->commit);
		si_plannerupdate(&x->index->p, SI_BRANCH, node);
	}
	si_unlock(x->index);
}

static inline int si_set(struct sitx *x, struct vinyl_tuple *v, uint64_t time)
{
	struct vinyl_index *index = x->index;
	index->update_time = time;
	/* match node */
	struct siiter ii;
	si_iter_open(&ii, index, VINYL_GE, v->data, v->size);
	struct sinode *node = si_iter_get(&ii);
	assert(node != NULL);
	struct svref ref;
	ref.v = v;
	ref.flags = 0;
	/* insert into node index */
	struct svindex *vindex = si_nodeindex(node);
	sv_indexset(vindex, ref);
	/* update node */
	node->update_time = index->update_time;
	node->used += vinyl_tuple_size(v);
	if (index->conf.lru)
		si_lru_add(index, &ref);
	si_txtrack(x, node);
	return 0;
}

static void
si_write(struct sitx *x, struct svlog *l, struct svlogindex *li, uint64_t time,
	 enum vinyl_status status)
{
	struct runtime *r = x->index->r;
	struct svlogv *cv = sv_logat(l, li->head);
	int c = li->count;
	while (c) {
		struct sv *sv = &cv->v;
		struct vinyl_tuple *v = sv_to_tuple(sv);
		if (status == SR_FINAL_RECOVERY) {
			if (si_readcommited(x->index, &cv->v)) {
				size_t gc = vinyl_tuple_size(v);
				if (vinyl_tuple_unref_rt(r, v))
					ss_quota(r->quota, SS_QREMOVE, gc);
				goto next;
			}
		}
		if (v->flags & SVGET) {
			vinyl_tuple_unref_rt(r, v);
			goto next;
		}
		si_set(x, v, time);
next:
		cv = sv_logat(l, cv->next);
		c--;
	}
	return;
}

/**
 * Create vinyl_dir if it doesn't exist.
 */
static int
sr_checkdir(const char *path)
{
	int exists = path_exists(path);
	if (exists == 0) {
		sr_error("directory '%s' does not exist", path);
		return -1;
	}
	return 0;
}

enum {
	SC_QBRANCH  = 0,
	SC_QGC      = 1,
	SC_QLRU     = 3,
	SC_QMAX
};

struct scdb {
	uint32_t workers[SC_QMAX];
	struct vinyl_index *index;
	uint32_t active;
};

struct sctask {
	struct siplan plan;
	struct scdb  *db;
	struct vinyl_index    *shutdown;
};

struct scheduler {
	pthread_mutex_t        lock;
	uint64_t       checkpoint_lsn_last;
	uint64_t       checkpoint_lsn;
	bool           checkpoint;
	uint32_t       age;
	uint64_t       age_time;
	uint64_t       gc_time;
	uint32_t       gc;
	uint64_t       lru_time;
	uint32_t       lru;
	int            rr;
	int            count;
	struct scdb         **i;
	struct rlist   shutdown;
	int            shutdown_pending;
	struct runtime            *r;
};

static int sc_init(struct scheduler*, struct runtime*);
static int sc_add(struct scheduler*, struct vinyl_index*);
static int sc_del(struct scheduler*, struct vinyl_index*, int);

static inline void
sc_start(struct scheduler *s, int task)
{
	int i = 0;
	while (i < s->count) {
		s->i[i]->active |= task;
		i++;
	}
}

static inline int
sc_end(struct scheduler *s, struct scdb *db, int task)
{
	db->active &= ~task;
	int complete = 1;
	int i = 0;
	while (i < s->count) {
		if (s->i[i]->active & task)
			complete = 0;
		i++;
	}
	return complete;
}

static inline void
sc_task_checkpoint(struct scheduler *s)
{
	uint64_t lsn = sr_seq(s->r->seq, SR_LSN);
	s->checkpoint_lsn = lsn;
	s->checkpoint = true;
	sc_start(s, SI_CHECKPOINT);
}

static inline void
sc_task_checkpoint_done(struct scheduler *s)
{
	s->checkpoint = false;
	s->checkpoint_lsn_last = s->checkpoint_lsn;
	s->checkpoint_lsn = 0;
}

static inline void
sc_task_gc(struct scheduler *s)
{
	s->gc = 1;
	sc_start(s, SI_GC);
}

static inline void
sc_task_gc_done(struct scheduler *s, uint64_t now)
{
	s->gc = 0;
	s->gc_time = now;
}

static inline void
sc_task_lru(struct scheduler *s)
{
	s->lru = 1;
	sc_start(s, SI_LRU);
}

static inline void
sc_task_lru_done(struct scheduler *s, uint64_t now)
{
	s->lru = 0;
	s->lru_time = now;
}

static inline void
sc_task_age(struct scheduler *s)
{
	s->age = 1;
	sc_start(s, SI_AGE);
}

static inline void
sc_task_age_done(struct scheduler *s, uint64_t now)
{
	s->age = 0;
	s->age_time = now;
}

static int sc_step(struct vinyl_service*, uint64_t);

static int sc_ctl_checkpoint(struct scheduler*);
static int sc_ctl_shutdown(struct scheduler*, struct vinyl_index*);


static int
sc_init(struct scheduler *s, struct runtime *r)
{
	uint64_t now = clock_monotonic64();
	tt_pthread_mutex_init(&s->lock, NULL);
	s->checkpoint_lsn           = 0;
	s->checkpoint_lsn_last      = 0;
	s->checkpoint               = false;
	s->age                      = 0;
	s->age_time                 = now;
	s->gc                       = 0;
	s->gc_time                  = now;
	s->lru                      = 0;
	s->lru_time                 = now;
	s->i                        = NULL;
	s->count                    = 0;
	s->rr                       = 0;
	s->r                        = r;
	rlist_create(&s->shutdown);
	s->shutdown_pending = 0;
	return 0;
}

static int sc_add(struct scheduler *s, struct vinyl_index *index)
{
	struct scdb *db = malloc(sizeof(struct scdb));
	if (unlikely(db == NULL))
		return -1;
	db->index  = index;
	db->active = 0;
	memset(db->workers, 0, sizeof(db->workers));

	tt_pthread_mutex_lock(&s->lock);
	int count = s->count + 1;
	struct scdb **i = malloc(count * sizeof(struct scdb*));
	if (unlikely(i == NULL)) {
		tt_pthread_mutex_unlock(&s->lock);
		free(db);
		return -1;
	}
	memcpy(i, s->i, s->count * sizeof(struct scdb*));
	i[s->count] = db;
	void *iprev = s->i;
	s->i = i;
	s->count = count;
	tt_pthread_mutex_unlock(&s->lock);
	if (iprev)
		free(iprev);
	return 0;
}

static int sc_del(struct scheduler *s, struct vinyl_index *index, int lock)
{
	if (unlikely(s->i == NULL))
		return 0;
	if (lock)
		tt_pthread_mutex_lock(&s->lock);
	struct scdb *db = NULL;
	struct scdb **iprev;
	int count = s->count - 1;
	if (unlikely(count == 0)) {
		iprev = s->i;
		db = s->i[0];
		s->count = 0;
		s->i = NULL;
		goto free;
	}
	struct scdb **i = malloc(count * sizeof(struct scdb*));
	if (unlikely(i == NULL)) {
		if (lock)
			tt_pthread_mutex_unlock(&s->lock);
		return -1;
	}
	int j = 0;
	int k = 0;
	while (j < s->count) {
		if (s->i[j]->index == index) {
			db = s->i[j];
			j++;
			continue;
		}
		i[k] = s->i[j];
		k++;
		j++;
	}
	iprev = s->i;
	s->i = i;
	s->count = count;
	if (unlikely(s->rr >= s->count))
		s->rr = 0;
free:
	if (lock)
		tt_pthread_mutex_unlock(&s->lock);
	free(iprev);
	free(db);
	return 0;
}

static struct scheduler *
vinyl_env_get_scheduler(struct vinyl_env *);

static int sc_ctl_checkpoint(struct scheduler *s)
{
	tt_pthread_mutex_lock(&s->lock);
	sc_task_checkpoint(s);
	tt_pthread_mutex_unlock(&s->lock);
	return 0;
}

static int sc_ctl_shutdown(struct scheduler *s, struct vinyl_index *i)
{
	/* Add a special 'shutdown' task to the scheduler */
	assert(i != NULL && i->refs == 0);
	tt_pthread_mutex_lock(&s->lock);
	s->shutdown_pending++;
	rlist_add(&s->shutdown, &i->link);
	tt_pthread_mutex_unlock(&s->lock);
	return 0;
}

static inline int
sc_execute(struct sctask *t, struct sdc *c, uint64_t vlsn)
{
	if (unlikely(t->shutdown)) {
		/* handle special 'shutdown' task (index drop or shutdown) */
		struct vinyl_index *index = t->shutdown;
		assert(index != NULL && index->refs == 0);
		int rc = 0;
		switch (t->plan.plan) {
		case SI_SHUTDOWN:
			rc = vinyl_index_delete(index);
			break;
		case SI_DROP:
			rc = si_drop(index);
			break;
		default:
			unreachable();
		}
		return rc;
	}

	/* regular task */
	struct vinyl_index *index = t->db->index;
	assert(index != NULL && index->refs > 0);
	uint64_t vlsn_lru = si_lru_vlsn(index);
	return si_execute(index, c, &t->plan, vlsn, vlsn_lru);
}

static inline struct scdb*
sc_peek(struct scheduler *s)
{
	if (s->rr >= s->count)
		s->rr = 0;
	int start = s->rr;
	int limit = s->count;
	int i = start;
first_half:
	while (i < limit) {
		struct scdb *db = s->i[i];
		if (unlikely(db->index == NULL || !si_active(db->index))) {
			i++;
			continue;
		}
		s->rr = i;
		return db;
	}
	if (i > start) {
		i = 0;
		limit = start;
		goto first_half;
	}
	s->rr = 0;
	return NULL;
}

static inline void
sc_next(struct scheduler *s)
{
	s->rr++;
	if (s->rr >= s->count)
		s->rr = 0;
}

static inline int
sc_plan(struct scheduler *s, struct siplan *plan)
{
	struct scdb *db = s->i[s->rr];
	return si_plan(db->index, plan);
}

static inline int
sc_planquota(struct scheduler *s, struct siplan *plan, uint32_t quota, uint32_t quota_limit)
{
	struct scdb *db = s->i[s->rr];
	if (db->workers[quota] >= quota_limit)
		return 2;
	return si_plan(db->index, plan);
}

static inline int
sc_do_shutdown(struct scheduler *s, struct sctask *task)
{
	if (likely(s->shutdown_pending == 0))
		return 0;
	struct vinyl_index *index, *n;
	rlist_foreach_entry_safe(index, &s->shutdown, link, n) {
		task->plan.plan = SI_SHUTDOWN;
		int rc;
		rc = si_plan(index, &task->plan);
		if (rc == 1) {
			s->shutdown_pending--;
			/* delete from scheduler->shutdown list */
			rlist_del(&index->link);
			sc_del(s, index, 0);
			task->shutdown = index;
			task->db = NULL;
			return 1;
		}
	}
	return 0;
}

static int
sc_do(struct scheduler *s, struct sctask *task, struct srzone *zone,
      struct scdb *db, uint64_t vlsn, uint64_t now)
{
	int rc;

	/* node gc */
	task->plan.plan = SI_NODEGC;
	rc = sc_plan(s, &task->plan);
	if (rc == 1) {
		vinyl_index_ref(db->index);
		assert(db->index != NULL && db->index->refs > 0);
		task->db = db;
		return 1;
	}

	/* checkpoint */
	if (s->checkpoint) {
		task->plan.plan = SI_CHECKPOINT;
		task->plan.a = s->checkpoint_lsn;
		rc = sc_plan(s, &task->plan);
		switch (rc) {
		case 1:
			db->workers[SC_QBRANCH]++;
			assert(db->index != NULL && db->index->refs > 0);
			vinyl_index_ref(db->index);
			task->db = db;
			return 1;
		case 0: /* complete */
			if (sc_end(s, db, SI_CHECKPOINT))
				sc_task_checkpoint_done(s);
			break;
		}
	}

	/* garbage-collection */
	if (s->gc) {
		task->plan.plan = SI_GC;
		task->plan.a = vlsn;
		task->plan.b = zone->gc_wm;
		rc = sc_planquota(s, &task->plan, SC_QGC, zone->gc_prio);
		switch (rc) {
		case 1:
			if (zone->mode == 0)
				task->plan.plan = SI_COMPACT_INDEX;
			assert(db->index != NULL && db->index->refs > 0);
			vinyl_index_ref(db->index);
			db->workers[SC_QGC]++;
			task->db = db;
			return 1;
		case 0: /* complete */
			if (sc_end(s, db, SI_GC))
				sc_task_gc_done(s, now);
			break;
		}
	}

	/* lru */
	if (s->lru) {
		task->plan.plan = SI_LRU;
		rc = sc_planquota(s, &task->plan, SC_QLRU, zone->lru_prio);
		switch (rc) {
		case 1:
			if (zone->mode == 0)
				task->plan.plan = SI_COMPACT_INDEX;
			assert(db->index != NULL && db->index->refs > 0);
			vinyl_index_ref(db->index);
			db->workers[SC_QLRU]++;
			task->db = db;
			return 1;
		case 0: /* complete */
			if (sc_end(s, db, SI_LRU))
				sc_task_lru_done(s, now);
			break;
		}
	}

	/* index aging */
	if (s->age) {
		task->plan.plan = SI_AGE;
		task->plan.a = zone->branch_age * 1000000; /* ms */
		task->plan.b = zone->branch_age_wm;
		rc = sc_planquota(s, &task->plan, SC_QBRANCH, zone->branch_prio);
		switch (rc) {
		case 1:
			if (zone->mode == 0)
				task->plan.plan = SI_COMPACT_INDEX;
			assert(db->index != NULL && db->index->refs > 0);
			vinyl_index_ref(db->index);
			db->workers[SC_QBRANCH]++;
			task->db = db;
			return 1;
		case 0: /* complete */
			if (sc_end(s, db, SI_AGE))
				sc_task_age_done(s, now);
			break;
		}
	}

	/* compact_index (compaction with in-memory index) */
	if (zone->mode == 0) {
		task->plan.plan = SI_COMPACT_INDEX;
		task->plan.a = zone->branch_wm;
		rc = sc_plan(s, &task->plan);
		if (rc == 1) {
			assert(db->index != NULL && db->index->refs > 0);
			vinyl_index_ref(db->index);
			task->db = db;
			return 1;
		}
		goto no_job;
	}

	/* branching */
	task->plan.plan = SI_BRANCH;
	task->plan.a = zone->branch_wm;
	rc = sc_planquota(s, &task->plan, SC_QBRANCH, zone->branch_prio);
	if (rc == 1) {
		db->workers[SC_QBRANCH]++;
		assert(db->index != NULL && db->index->refs > 0);
		vinyl_index_ref(db->index);
		task->db = db;
		return 1;
	}

	/* compaction */
	task->plan.plan = SI_COMPACT;
	task->plan.a = zone->compact_wm;
	task->plan.b = zone->compact_mode;
	rc = sc_plan(s, &task->plan);
	if (rc == 1) {
		assert(db->index != NULL && db->index->refs > 0);
		vinyl_index_ref(db->index);
		task->db = db;
		return 1;
	}

no_job:
	si_planinit(&task->plan);
	return 0;
}

static inline void
sc_periodic_done(struct scheduler *s, uint64_t now)
{
	/* checkpoint */
	if (unlikely(s->checkpoint))
		sc_task_checkpoint_done(s);
	/* gc */
	if (unlikely(s->gc))
		sc_task_gc_done(s, now);
	/* lru */
	if (unlikely(s->lru))
		sc_task_lru_done(s, now);
	/* age */
	if (unlikely(s->age))
		sc_task_age_done(s, now);
}

static inline void
sc_periodic(struct scheduler *s, struct srzone *zone, uint64_t now)
{
	if (unlikely(s->count == 0))
		return;
	/* checkpoint */
	switch (zone->mode) {
	case 0:  /* compact_index */
		break;
	case 1:  /* compact_index + branch_count prio */
		unreachable();
		break;
	case 2:  /* checkpoint */
	{
		if (!s->checkpoint)
			sc_task_checkpoint(s);
		break;
	}
	default: /* branch + compact */
		assert(zone->mode == 3);
	}
	/* gc */
	if (s->gc == 0 && zone->gc_prio && zone->gc_period) {
		if ((now - s->gc_time) >= zone->gc_period_us)
			sc_task_gc(s);
	}
	/* lru */
	if (s->lru == 0 && zone->lru_prio && zone->lru_period) {
		if ((now - s->lru_time) >= zone->lru_period_us)
			sc_task_lru(s);
	}
	/* aging */
	if (s->age == 0 && zone->branch_prio && zone->branch_age_period) {
		if ((now - s->age_time) >= zone->branch_age_period_us)
			sc_task_age(s);
	}
}

static int
sc_schedule(struct sctask *task, struct vinyl_service *srv, uint64_t vlsn)
{
	uint64_t now = clock_monotonic64();
	struct scheduler *sc = vinyl_env_get_scheduler(srv->env);
	struct srzone *zone = sr_zoneof(sc->r);
	int rc;
	tt_pthread_mutex_lock(&sc->lock);
	/* start periodic tasks */
	sc_periodic(sc, zone, now);
	/* index shutdown-drop */
	rc = sc_do_shutdown(sc, task);
	if (rc) {
		tt_pthread_mutex_unlock(&sc->lock);
		return rc;
	}
	/* peek an index */
	struct scdb *db = sc_peek(sc);
	if (unlikely(db == NULL)) {
		/* complete on-going periodic tasks when there
		 * are no active index maintenance tasks left */
		sc_periodic_done(sc, now);
		tt_pthread_mutex_unlock(&sc->lock);
		return 0;
	}
	assert(db != NULL);
	rc = sc_do(sc, task, zone, db, vlsn, now);
	/* schedule next index */
	sc_next(sc);
	tt_pthread_mutex_unlock(&sc->lock);
	return rc;
}

static inline int
sc_complete(struct scheduler *s, struct sctask *t)
{
	tt_pthread_mutex_lock(&s->lock);
	struct scdb *db = t->db;
	switch (t->plan.plan) {
	case SI_BRANCH:
	case SI_AGE:
	case SI_CHECKPOINT:
		db->workers[SC_QBRANCH]--;
		break;
	case SI_COMPACT_INDEX:
		break;
	case SI_GC:
		db->workers[SC_QGC]--;
		break;
	case SI_LRU:
		db->workers[SC_QLRU]--;
		break;
	case SI_SHUTDOWN:
	case SI_DROP:
		break;
	case SI_COMPACT:
		break;
	default:
		unreachable();
	}
	if (db && db->index != NULL) {
		vinyl_index_unref(db->index);
		db->index = NULL;
	}
	tt_pthread_mutex_unlock(&s->lock);
	return 0;
}

static inline void
sc_taskinit(struct sctask *task)
{
	si_planinit(&task->plan);
	task->db = NULL;
	task->shutdown = NULL;
}

static int
sc_step(struct vinyl_service *srv, uint64_t vlsn)
{
	struct scheduler *sc = vinyl_env_get_scheduler(srv->env);
	struct sctask task;
	sc_taskinit(&task);
	int rc = sc_schedule(&task, srv, vlsn);
	if (task.plan.plan == 0) {
		/*
		 * TODO: no-op task.
		 * Execute some useless instructions, utilize CPU
		 * and spent our money. Absolute senseless.
		 * See no_job case from sc_do().
		 */
		return 0; /* no_job case */
	}
	int rc_job = rc;
	if (rc_job > 0) {
		rc = sc_execute(&task, &srv->sdc, vlsn);
		if (unlikely(rc == -1)) {
			if (task.db != NULL) {
				sr_statusset(&task.db->index->status,
					     SR_MALFUNCTION);
			}
			return -1;
		}
	}
	sc_complete(sc, &task);
	return rc_job;
}

static int sc_write(struct scheduler *s, struct svlog *log, uint64_t lsn,
		    enum vinyl_status status)
{
	/* write-ahead log */
	sr_seqlock(s->r->seq);
	if (lsn > s->r->seq->lsn)
		s->r->seq->lsn = lsn;
	sr_sequnlock(s->r->seq);
	sl_write(log, lsn);

	/* index */
	uint64_t now = clock_monotonic64();
	struct svlogindex *i   = (struct svlogindex*)log->index.s;
	struct svlogindex *end = (struct svlogindex*)log->index.p;
	while (i < end) {
		struct vinyl_index *index = i->index;
		struct sitx x;
		si_begin(&x, index);
		si_write(&x, log, i, now, status);
		si_commit(&x);
		i++;
	}
	return 0;
}

struct seconfrt {
	/* vinyl */
	char      version[16];
	char      version_storage[16];
	char      build[32];
	/* memory */
	uint64_t  memory_used;
	/* scheduler */
	char      zone[4];
	uint32_t  checkpoint;
	uint64_t  checkpoint_lsn;
	uint64_t  checkpoint_lsn_last;
	uint32_t  gc_active;
	uint32_t  lru_active;
	/* metric */
	struct srseq     seq;
	/* performance */
	uint32_t  tx_rw;
	uint32_t  tx_ro;
	uint32_t  tx_gc_queue;
	struct srstat    stat;
};

struct seconf {
	/* path to vinyl_dir */
	char *path;
	/* compaction */
	struct srzonemap zones;
	/* memory */
	uint64_t memory_limit;
	struct srconf *conf;
	struct vinyl_env *env;
};

static int se_confinit(struct seconf*, struct vinyl_env *o);
static void se_conffree(struct seconf*);
static int se_confserialize(struct seconf*, struct ssbuf*);

struct vinyl_confcursor {
	struct vinyl_env *env;
	struct ssbuf dump;
	int first;
	struct srconfdump *pos;
};

struct vinyl_env {
	enum vinyl_status status;
	/** List of open spaces. */
	struct rlist indexes;
	struct srseq       seq;
	struct seconf      conf;
	struct ssquota     quota;
	struct sicachepool cachepool;
	struct sxmanager   xm;
	struct scheduler          scheduler;
	struct srstat      stat;
	struct runtime          r;
	struct mempool read_pool;
};

struct scheduler *
vinyl_env_get_scheduler(struct vinyl_env *env)
{
	return &env->scheduler;
}

void
vinyl_raise()
{
	diag_raise();
}

int
vinyl_index_read(struct vinyl_index*, struct vinyl_tuple*, enum vinyl_order order,
		struct vinyl_tuple **, struct sx*, struct sicache*,
		bool cache_only, struct vinyl_statget *);
static int vinyl_index_visible(struct vinyl_index*, uint64_t);
static int vinyl_index_recoverbegin(struct vinyl_index*);
static int vinyl_index_recoverend(struct vinyl_index*);

struct vinyl_tx {
	struct vinyl_env *env;
	uint64_t start;
	struct svlog log;
	struct sx t;
};

struct vinyl_cursor {
	struct vinyl_index *index;
	struct vinyl_tuple *key;
	enum vinyl_order order;
	struct svlog log;
	struct sx t;
	uint64_t start;
	int ops;
	int read_disk;
	int read_cache;
	int read_commited;
	struct sicache *cache;
};

void
vinyl_bootstrap(struct vinyl_env *e)
{
	assert(e->status == SR_OFFLINE);
	e->status = SR_ONLINE;
	/* enable quota */
	ss_quotaenable(&e->quota);
}

void
vinyl_begin_initial_recovery(struct vinyl_env *e)
{
	assert(e->status == SR_OFFLINE);
	e->status = SR_INITIAL_RECOVERY;
}

void
vinyl_begin_final_recovery(struct vinyl_env *e)
{
	assert(e->status == SR_INITIAL_RECOVERY);
	e->status = SR_FINAL_RECOVERY;
}

void
vinyl_end_recovery(struct vinyl_env *e)
{
	assert(e->status == SR_FINAL_RECOVERY);
	e->status = SR_ONLINE;
	/* enable quota */
	ss_quotaenable(&e->quota);
}

int
vinyl_env_delete(struct vinyl_env *e)
{
	int rcret = 0;
	e->status = SR_SHUTDOWN;
	/* TODO: tarantool doesn't delete indexes during shutdown */
	//assert(rlist_empty(&e->db));
	int rc;
	struct vinyl_index *index, *next;
	rlist_foreach_entry_safe(index, &e->scheduler.shutdown, link, next) {
		rc = vinyl_index_delete(index);
		if (unlikely(rc == -1))
			rcret = -1;
	}
	sx_managerfree(&e->xm);
	si_cachepool_free(&e->cachepool);
	se_conffree(&e->conf);
	ss_quotafree(&e->quota);
	sr_statfree(&e->stat);
	sr_seqfree(&e->seq);
	mempool_destroy(&e->read_pool);
	free(e);
	return rcret;
}

static inline struct srconf*
se_confvinyl(struct vinyl_env *e, struct seconfrt *rt, struct srconf **pc)
{
	struct srconf *vinyl = *pc;
	struct srconf *p = NULL;
	sr_C(&p, pc, "version", SS_STRING, rt->version);
	sr_C(&p, pc, "version_storage", SS_STRING, rt->version_storage);
	sr_C(&p, pc, "build", SS_STRING, rt->build);
	sr_C(&p, pc, "path", SS_STRINGPTR, &e->conf.path);
	return sr_C(NULL, pc, "vinyl", SS_UNDEF, vinyl);
}

static inline struct srconf*
se_confmemory(struct vinyl_env *e, struct seconfrt *rt, struct srconf **pc)
{
	struct srconf *memory = *pc;
	struct srconf *p = NULL;
	sr_C(&p, pc, "limit", SS_U64, &e->conf.memory_limit);
	sr_C(&p, pc, "used", SS_U64, &rt->memory_used);
	return sr_C(NULL, pc, "memory", SS_UNDEF, memory);
}

static inline struct srconf*
se_confcompaction(struct vinyl_env *e, struct srconf **pc)
{
	struct srconf *compaction = NULL;
	struct srconf *prev = NULL;
	struct srconf *p;
	int i = 0;
	for (; i < 11; i++) {
		struct srzone *z = &e->conf.zones.zones[i];
		if (! z->enable)
			continue;
		struct srconf *zone = *pc;
		p = NULL;
		sr_C(&p, pc, "mode", SS_U32, &z->mode);
		sr_C(&p, pc, "compact_wm", SS_U32, &z->compact_wm);
		sr_C(&p, pc, "compact_mode", SS_U32, &z->compact_mode);
		sr_C(&p, pc, "branch_prio", SS_U32, &z->branch_prio);
		sr_C(&p, pc, "branch_wm", SS_U32, &z->branch_wm);
		sr_C(&p, pc, "branch_age", SS_U32, &z->branch_age);
		sr_C(&p, pc, "branch_age_period", SS_U32, &z->branch_age_period);
		sr_C(&p, pc, "branch_age_wm", SS_U32, &z->branch_age_wm);
		sr_C(&p, pc, "gc_wm", SS_U32, &z->gc_wm);
		sr_C(&p, pc, "gc_prio", SS_U32, &z->gc_prio);
		sr_C(&p, pc, "gc_period", SS_U32, &z->gc_period);
		sr_C(&p, pc, "lru_prio", SS_U32, &z->lru_prio);
		sr_C(&p, pc, "lru_period", SS_U32, &z->lru_period);
		prev = sr_C(&prev, pc, z->name, SS_UNDEF, zone);
		if (compaction == NULL)
			compaction = prev;
	}
	return sr_C(NULL, pc, "compaction", SS_UNDEF, compaction);
}

int
vinyl_checkpoint(struct vinyl_env *env)
{
	return sc_ctl_checkpoint(&env->scheduler);
}

bool
vinyl_checkpoint_is_active(struct vinyl_env *env)
{
	tt_pthread_mutex_lock(&env->scheduler.lock);
	bool is_active = env->scheduler.checkpoint;
	tt_pthread_mutex_unlock(&env->scheduler.lock);
	return is_active;
}

static inline struct srconf*
se_confscheduler(struct seconfrt *rt, struct srconf **pc)
{
	struct srconf *scheduler = *pc;
	struct srconf *p = NULL;
	sr_C(&p, pc, "zone", SS_STRING, rt->zone);
	sr_C(&p, pc, "gc_active", SS_U32, &rt->gc_active);
	sr_C(&p, pc, "lru_active", SS_U32, &rt->lru_active);
	return sr_C(NULL, pc, "scheduler", SS_UNDEF, scheduler);
}

static inline struct srconf*
se_confperformance(struct seconfrt *rt, struct srconf **pc)
{
	struct srconf *perf = *pc;
	struct srconf *p = NULL;
	sr_C(&p, pc, "documents", SS_U64, &rt->stat.v_count);
	sr_C(&p, pc, "documents_used", SS_U64, &rt->stat.v_allocated);
	sr_C(&p, pc, "set", SS_U64, &rt->stat.set);
	sr_C(&p, pc, "set_latency", SS_STRING, rt->stat.set_latency.sz);
	sr_C(&p, pc, "delete", SS_U64, &rt->stat.del);
	sr_C(&p, pc, "delete_latency", SS_STRING, rt->stat.del_latency.sz);
	sr_C(&p, pc, "upsert", SS_U64, &rt->stat.upsert);
	sr_C(&p, pc, "upsert_latency", SS_STRING, rt->stat.upsert_latency.sz);
	sr_C(&p, pc, "get", SS_U64, &rt->stat.get);
	sr_C(&p, pc, "get_latency", SS_STRING, rt->stat.get_latency.sz);
	sr_C(&p, pc, "get_read_disk", SS_STRING, rt->stat.get_read_disk.sz);
	sr_C(&p, pc, "get_read_cache", SS_STRING, rt->stat.get_read_cache.sz);
	sr_C(&p, pc, "tx_active_rw", SS_U32, &rt->tx_rw);
	sr_C(&p, pc, "tx_active_ro", SS_U32, &rt->tx_ro);
	sr_C(&p, pc, "tx", SS_U64, &rt->stat.tx);
	sr_C(&p, pc, "tx_rollback", SS_U64, &rt->stat.tx_rlb);
	sr_C(&p, pc, "tx_conflict", SS_U64, &rt->stat.tx_conflict);
	sr_C(&p, pc, "tx_lock", SS_U64, &rt->stat.tx_lock);
	sr_C(&p, pc, "tx_latency", SS_STRING, rt->stat.tx_latency.sz);
	sr_C(&p, pc, "tx_ops", SS_STRING, rt->stat.tx_stmts.sz);
	sr_C(&p, pc, "tx_gc_queue", SS_U32, &rt->tx_gc_queue);
	sr_C(&p, pc, "cursor", SS_U64, &rt->stat.cursor);
	sr_C(&p, pc, "cursor_latency", SS_STRING, rt->stat.cursor_latency.sz);
	sr_C(&p, pc, "cursor_read_disk", SS_STRING, rt->stat.cursor_read_disk.sz);
	sr_C(&p, pc, "cursor_read_cache", SS_STRING, rt->stat.cursor_read_cache.sz);
	sr_C(&p, pc, "cursor_ops", SS_STRING, rt->stat.cursor_ops.sz);
	return sr_C(NULL, pc, "performance", SS_UNDEF, perf);
}

static inline struct srconf*
se_confmetric(struct seconfrt *rt, struct srconf **pc)
{
	struct srconf *metric = *pc;
	struct srconf *p = NULL;
	sr_C(&p, pc, "lsn", SS_U64, &rt->seq.lsn);
	sr_C(&p, pc, "tsn", SS_U64, &rt->seq.tsn);
	sr_C(&p, pc, "nsn", SS_U64, &rt->seq.nsn);
	return sr_C(NULL, pc, "metric", SS_UNDEF, metric);
}

static inline struct srconf*
se_confdb(struct vinyl_env *e, struct srconf **pc)
{
	struct srconf *db = NULL;
	struct srconf *prev = NULL;
	struct srconf *p;
	struct vinyl_index *o;
	rlist_foreach_entry(o, &e->indexes, link)
	{
		si_profilerbegin(&o->rtp, o);
		si_profiler(&o->rtp);
		si_profilerend(&o->rtp);
		/* database index */
		struct srconf *database = *pc;
		p = NULL;
		sr_C(&p, pc, "memory_used", SS_U64, &o->rtp.memory_used);
		sr_C(&p, pc, "size", SS_U64, &o->rtp.total_node_size);
		sr_C(&p, pc, "size_uncompressed", SS_U64, &o->rtp.total_node_origin_size);
		sr_C(&p, pc, "count", SS_U64, &o->rtp.count);
		sr_C(&p, pc, "count_dup", SS_U64, &o->rtp.count_dup);
		sr_C(&p, pc, "read_disk", SS_U64, &o->rtp.read_disk);
		sr_C(&p, pc, "read_cache", SS_U64, &o->rtp.read_cache);
		sr_C(&p, pc, "temperature_avg", SS_U32, &o->rtp.temperature_avg);
		sr_C(&p, pc, "temperature_min", SS_U32, &o->rtp.temperature_min);
		sr_C(&p, pc, "temperature_max", SS_U32, &o->rtp.temperature_max);
		sr_C(&p, pc, "temperature_histogram", SS_STRINGPTR, &o->rtp.histogram_temperature_ptr);
		sr_C(&p, pc, "node_count", SS_U32, &o->rtp.total_node_count);
		sr_C(&p, pc, "branch_count", SS_U32, &o->rtp.total_branch_count);
		sr_C(&p, pc, "branch_avg", SS_U32, &o->rtp.total_branch_avg);
		sr_C(&p, pc, "branch_max", SS_U32, &o->rtp.total_branch_max);
		sr_C(&p, pc, "branch_histogram", SS_STRINGPTR, &o->rtp.histogram_branch_ptr);
		sr_C(&p, pc, "page_count", SS_U32, &o->rtp.total_page_count);
		sr_C(&prev, pc, o->conf.name, SS_UNDEF, database);
		if (db == NULL)
			db = prev;
	}
	return sr_C(NULL, pc, "db", SS_UNDEF, db);
}

static struct srconf*
se_confprepare(struct vinyl_env *e, struct seconfrt *rt, struct srconf *c)
{
	/* vinyl */
	struct srconf *pc = c;
	struct srconf *vinyl     = se_confvinyl(e, rt, &pc);
	struct srconf *memory     = se_confmemory(e, rt, &pc);
	struct srconf *compaction = se_confcompaction(e, &pc);
	struct srconf *scheduler  = se_confscheduler(rt, &pc);
	struct srconf *perf       = se_confperformance(rt, &pc);
	struct srconf *metric     = se_confmetric(rt, &pc);
	struct srconf *db         = se_confdb(e, &pc);

	vinyl->next     = memory;
	memory->next     = compaction;
	compaction->next = scheduler;
	scheduler->next  = perf;
	perf->next       = metric;
	metric->next     = db;
	return vinyl;
}

static int
se_confrt(struct vinyl_env *e, struct seconfrt *rt)
{
	/* vinyl */
	snprintf(rt->version, sizeof(rt->version),
	         "%d.%d.%d",
	         SR_VERSION_A - '0',
	         SR_VERSION_B - '0',
	         SR_VERSION_C - '0');
	snprintf(rt->version_storage, sizeof(rt->version_storage),
	         "%d.%d.%d",
	         SR_VERSION_STORAGE_A - '0',
	         SR_VERSION_STORAGE_B - '0',
	         SR_VERSION_STORAGE_C - '0');
	snprintf(rt->build, sizeof(rt->build), "%s",
	         PACKAGE_VERSION);

	/* memory */
	rt->memory_used = ss_quotaused(&e->quota);

	/* scheduler */
	tt_pthread_mutex_lock(&e->scheduler.lock);
	rt->checkpoint           = e->scheduler.checkpoint;
	rt->checkpoint_lsn_last  = e->scheduler.checkpoint_lsn_last;
	rt->checkpoint_lsn       = e->scheduler.checkpoint_lsn;
	rt->gc_active            = e->scheduler.gc;
	rt->lru_active           = e->scheduler.lru;
	tt_pthread_mutex_unlock(&e->scheduler.lock);

	int v = ss_quotaused_percent(&e->quota);
	struct srzone *z = sr_zonemap(&e->conf.zones, v);
	memcpy(rt->zone, z->name, sizeof(rt->zone));

	/* metric */
	sr_seqlock(&e->seq);
	rt->seq = e->seq;
	sr_sequnlock(&e->seq);

	/* performance */
	rt->tx_rw = e->xm.count_rw;
	rt->tx_ro = e->xm.count_rd;
	rt->tx_gc_queue = e->xm.count_gc;

	tt_pthread_mutex_lock(&e->stat.lock);
	rt->stat = e->stat;
	tt_pthread_mutex_unlock(&e->stat.lock);
	sr_statprepare(&rt->stat);
	return 0;
}

static int se_confserialize(struct seconf *c, struct ssbuf *buf)
{
	struct vinyl_env *e = c->env;
	struct seconfrt rt;
	se_confrt(e, &rt);
	struct srconf *root = se_confprepare(e, &rt, c->conf);
	struct srconfstmt stmt = {
		.path      = NULL,
		.serialize = buf,
		.r         = &e->r
	};
	return sr_confserialize(root, &stmt);
}

static int se_confinit(struct seconf *c, struct vinyl_env *e)
{
	c->conf = malloc(sizeof(struct srconf) * 2048);
	if (unlikely(c->conf == NULL))
		return -1;
	c->env = e;
	c->path = strdup(cfg_gets("vinyl_dir"));
	if (c->path == NULL) {
		sr_oom();
		return -1;
	}
	/* Ensure vinyl data directory exists. */
	if (sr_checkdir(c->path))
		return -1;
	c->memory_limit = cfg_getd("vinyl.memory_limit")*1024*1024*1024;
	struct srzone def = {
		.enable            = 1,
		.mode              = 3, /* branch + compact */
		.compact_wm        = 2,
		.compact_mode      = 0, /* branch priority */
		.branch_prio       = 1,
		.branch_wm         = 10 * 1024 * 1024,
		.branch_age        = 40,
		.branch_age_period = 40,
		.branch_age_wm     = 1 * 1024 * 1024,
		.gc_prio           = 1,
		.gc_period         = 60,
		.gc_wm             = 30,
		.lru_prio          = 0,
		.lru_period        = 0
	};
	struct srzone redzone = {
		.enable            = 1,
		.mode              = 2, /* checkpoint */
		.compact_wm        = 4,
		.compact_mode      = 0,
		.branch_prio       = 0,
		.branch_wm         = 0,
		.branch_age        = 0,
		.branch_age_period = 0,
		.branch_age_wm     = 0,
		.gc_prio           = 0,
		.gc_period         = 0,
		.gc_wm             = 0,
		.lru_prio          = 0,
		.lru_period        = 0
	};
	sr_zonemap_set(&c->zones, 0, &def);
	sr_zonemap_set(&c->zones, 80, &redzone);
	/* configure zone = 0 */
	struct srzone *z = &c->zones.zones[0];
	assert(z->enable);
	z->compact_wm = cfg_geti("vinyl.compact_wm");
	if (z->compact_wm <= 1) {
		sr_error("bad %d.compact_wm value", 0);
		return -1;
	}
	z->branch_prio = cfg_geti("vinyl.branch_prio");
	z->branch_age = cfg_geti("vinyl.branch_age");
	z->branch_age_period = cfg_geti("vinyl.branch_age_period");
	z->branch_age_wm = cfg_geti("vinyl.branch_age_wm");

	/* convert periodic times from sec to usec */
	for (int i = 0; i < 11; i++) {
		z = &c->zones.zones[i];
		z->branch_age_period_us = z->branch_age_period * 1000000;
		z->gc_period_us         = z->gc_period * 1000000;
		z->lru_period_us        = z->lru_period * 1000000;
	}
	return 0;
}

static void se_conffree(struct seconf *c)
{
	if (c->conf) {
		free(c->conf);
		c->conf = NULL;
	}
	if (c->path) {
		free(c->path);
		c->path = NULL;
	}
}

void
vinyl_confcursor_delete(struct vinyl_confcursor *c)
{
	ss_buffree(&c->dump);
	free(c);
}

int
vinyl_confcursor_next(struct vinyl_confcursor *c, const char **key,
		    const char **value)
{
	if (c->first) {
		assert( ss_bufsize(&c->dump) >= (int)sizeof(struct srconfdump) );
		c->first = 0;
		c->pos = (struct srconfdump*)c->dump.s;
	} else {
		int size = sizeof(struct srconfdump) + c->pos->keysize + c->pos->valuesize;
		c->pos = (struct srconfdump*)((char*)c->pos + size);
		if ((char*)c->pos >= c->dump.p)
			c->pos = NULL;
	}
	if (unlikely(c->pos == NULL))
		return 1;
	*key = sr_confkey(c->pos);
	*value = sr_confvalue(c->pos);
	return 0;
}

struct vinyl_confcursor *
vinyl_confcursor_new(struct vinyl_env *e)
{
	struct vinyl_confcursor *c;
	c = malloc(sizeof(struct vinyl_confcursor));
	if (unlikely(c == NULL)) {
		sr_oom();
		return NULL;
	}
	c->env = e;
	c->pos = NULL;
	c->first = 1;
	ss_bufinit(&c->dump);
	int rc = se_confserialize(&e->conf, &c->dump);
	if (unlikely(rc == -1)) {
		vinyl_confcursor_delete(c);
		sr_oom();
		return NULL;
	}
	return c;
}

void
vinyl_cursor_delete(struct vinyl_cursor *c)
{
	struct vinyl_env *e = c->index->env;
	if (! c->read_commited)
		sx_rollback(&c->t);
	if (c->cache)
		si_cachepool_push(c->cache);
	if (c->key)
		vinyl_tuple_unref(c->index, c->key);
	vinyl_index_unref(c->index);
	sr_statcursor(&e->stat, c->start,
	              c->read_disk,
	              c->read_cache,
	              c->ops);
	TRASH(c);
	free(c);
}

int
vinyl_cursor_next(struct vinyl_cursor *c, struct vinyl_tuple **result,
		 bool cache_only)
{
	struct vinyl_index *index = c->index;
	struct sx *x = &c->t;
	if (c->read_commited)
		x = NULL;

	struct vinyl_statget statget;
	assert(c->key != NULL);
	if (vinyl_index_read(index, c->key, c->order, result, x, c->cache,
			    cache_only, &statget) != 0) {
		return -1;
	}

	c->ops++;
	if (*result == NULL) {
		if (!cache_only) {
			 vinyl_tuple_unref(c->index, c->key);
			 c->key = NULL;
		}
		 return 0;
	}

	if (c->order == VINYL_GE)
		c->order = VINYL_GT;
	else if (c->order == VINYL_LE)
		c->order = VINYL_LT;

	c->read_disk += statget.read_disk;
	c->read_cache += statget.read_cache;

	vinyl_tuple_unref(c->index, c->key);
	c->key = *result;
	vinyl_tuple_ref(c->key);

	return 0;
}

void
vinyl_cursor_set_read_commited(struct vinyl_cursor *c, bool read_commited)
{
	sx_rollback(&c->t);
	c->read_commited = read_commited;
}

struct vinyl_cursor *
vinyl_cursor_new(struct vinyl_index *index, struct vinyl_tuple *key,
		enum vinyl_order order)
{
	struct vinyl_env *e = index->env;
	struct vinyl_cursor *c;
	c = malloc(sizeof(struct vinyl_cursor));
	if (unlikely(c == NULL)) {
		sr_oom();
		return NULL;
	}
	sv_loginit(&c->log);
	vinyl_index_ref(index);
	c->index = index;
	c->start = clock_monotonic64();
	c->ops = 0;
	c->read_disk = 0;
	c->read_cache = 0;
	c->t.state = SXUNDEF;
	c->cache = si_cachepool_pop(&e->cachepool);
	if (unlikely(c->cache == NULL)) {
		sr_oom();
		return NULL;
	}
	c->read_commited = 0;
	sx_begin(&e->xm, &c->t, SXRO, &c->log);

	c->key = key;
	vinyl_tuple_ref(key);
	c->order = order;
	return c;
}

static int
si_confcreate(struct siconf *conf, struct key_def *key_def)
{
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 ":%" PRIu32,
	         key_def->space_id, key_def->iid);
	conf->name = strdup(name);
	if (conf->name == NULL) {
		sr_oom();
		goto error;
	}
	conf->id                    = key_def->space_id;
	conf->sync                  = cfg_geti("vinyl.sync");
	conf->node_size             = key_def->opts.node_size;
	conf->node_page_size        = key_def->opts.page_size;
	conf->node_page_checksum    = 1;

	/* compression */
	if (key_def->opts.compression[0] != '\0') {
		conf->compression_if = ss_filterof(key_def->opts.compression);
		if (conf->compression_if == NULL) {
			sr_error("unknown compression type '%s'",
				 key_def->opts.compression);
			goto error;
		}
		if (conf->compression_if != &ss_nonefilter)
			conf->compression = 1;
	} else {
		conf->compression = 0;
		conf->compression_if = &ss_nonefilter;
	}
	conf->compression_sz = strdup(conf->compression_if->name);
	if (conf->compression_sz == NULL) {
		sr_oom();
		goto error;
	}

	conf->temperature           = 1;
	/* path */
	if (key_def->opts.path[0] == '\0') {
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", cfg_gets("vinyl_dir"),
			 conf->name);
		conf->path = strdup(path);
	} else {
		conf->path = strdup(key_def->opts.path);
	}
	if (conf->path == NULL) {
		sr_oom();
		goto error;
	}
	conf->lru                   = 0;
	conf->lru_step              = 128 * 1024;
	conf->buf_gc_wm             = 1024 * 1024;

	return 0;
error:
	return -1;
}

int
vinyl_index_open(struct vinyl_index *index)
{
	struct vinyl_env *e = index->env;
	int status = sr_status(&index->status);
	if (status == SR_FINAL_RECOVERY ||
	    status == SR_DROP_PENDING)
		goto online;
	if (status != SR_OFFLINE)
		return -1;
	sx_indexset(&index->coindex, index->conf.id);
	int rc = vinyl_index_recoverbegin(index);
	if (unlikely(rc == -1))
		return -1;

online:
	vinyl_index_recoverend(index);
	rc = sc_add(&e->scheduler, index);
	if (unlikely(rc == -1))
		return -1;
	return 0;
}

void
vinyl_index_ref(struct vinyl_index *index)
{
	tt_pthread_mutex_lock(&index->ref_lock);
	index->refs++;
	tt_pthread_mutex_unlock(&index->ref_lock);
}

void
vinyl_index_unref(struct vinyl_index *index)
{
	struct vinyl_env *e = index->env;
	/* do nothing during env shutdown */
	if (e->status == SR_SHUTDOWN)
		return;
	/* reduce reference counter */
	tt_pthread_mutex_lock(&index->ref_lock);
	int ref = --index->refs;
	tt_pthread_mutex_unlock(&index->ref_lock);
	assert(ref >= 0);
	if (ref > 0)
		return;
	/* drop/shutdown pending:
	 *
	 * switch state and transfer job to
	 * the scheduler.
	*/
	enum vinyl_status status = sr_status(&index->status);
	switch (status) {
	case SR_SHUTDOWN_PENDING:
		status = SR_SHUTDOWN;
		break;
	case SR_DROP_PENDING:
		status = SR_DROP;
		break;
	default:
		return;
	}
	rlist_del(&index->link);

	/* schedule index shutdown or drop */
	sr_statusset(&index->status, status);
	sc_ctl_shutdown(&e->scheduler, index);
}

int
vinyl_index_close(struct vinyl_index *index)
{
	struct vinyl_env *e = index->env;
	int status = sr_status(&index->status);
	if (unlikely(! sr_statusactive_is(status)))
		return -1;
	/* set last visible transaction id */
	index->txn_max = sx_max(&e->xm);
	sr_statusset(&index->status, SR_SHUTDOWN_PENDING);
	if (e->status == SR_SHUTDOWN || e->status == SR_OFFLINE) {
		return vinyl_index_delete(index);
	}
	vinyl_index_unref(index);
	return 0;
}

int
vinyl_index_drop(struct vinyl_index *index)
{
	struct vinyl_env *e = index->env;
	int status = sr_status(&index->status);
	if (unlikely(! sr_statusactive_is(status)))
		return -1;
	int rc = si_dropmark(index);
	if (unlikely(rc == -1))
		return -1;
	/* set last visible transaction id */
	index->txn_max = sx_max(&e->xm);
	sr_statusset(&index->status, SR_DROP_PENDING);
	if (e->status == SR_SHUTDOWN || e->status == SR_OFFLINE)
		return vinyl_index_delete(index);
	vinyl_index_unref(index);
	return 0;
}

int
vinyl_index_read(struct vinyl_index *index, struct vinyl_tuple *key, enum vinyl_order order,
		struct vinyl_tuple **result, struct sx *x,
		struct sicache *cache, bool cache_only,
		struct vinyl_statget *statget)
{
	struct vinyl_env *e = index->env;
	uint64_t start  = clock_monotonic64();

	if (unlikely(! sr_online(&index->status))) {
		sr_error("%s", "index is not online");
		return -1;
	}

	key->flags = SVGET;
	struct vinyl_tuple *vup = NULL;

	/* concurrent */
	if (x != NULL && order == VINYL_EQ) {
		int rc = sx_get(x, &index->coindex, key, &vup);
		if (unlikely(rc == -1))
			return -1;
		if (rc == 2) { /* delete */
			*result = NULL;
			return 0;
		}
		if (rc == 1 && ((vup->flags & SVUPSERT) == 0)) {
			*result = vup;
			return 0;
		}
	} else {
		sx_get_autocommit(&e->xm, &index->coindex);
	}

	/* prepare read cache */
	int cachegc = 0;
	if (cache == NULL) {
		cachegc = 1;
		cache = si_cachepool_pop(&e->cachepool);
		if (unlikely(cache == NULL)) {
			if (vup != NULL) {
				vinyl_tuple_unref(index, vup);
			}
			sr_oom();
			return -1;
		}
	}

	int64_t vlsn;
	if (x) {
		vlsn = x->vlsn;
	} else {
		vlsn = sr_seq(e->scheduler.r->seq, SR_LSN);
	}

	int upsert_eq = 0;
	if (order == VINYL_EQ) {
		order = VINYL_GE;
		upsert_eq = 1;
	}

	/* read index */
	struct siread q;
	si_readopen(&q, index, cache, order, vlsn, key->data, key->size);
	struct sv sv_vup;
	if (vup != NULL) {
		sv_from_tuple(&sv_vup, vup);
		q.upsert_v = &sv_vup;
	}
	q.upsert_eq = upsert_eq;
	q.cache_only = cache_only;
	int rc = si_read(&q);
	si_readclose(&q);

	if (vup != NULL) {
		vinyl_tuple_unref(index, vup);
	}
	/* free read cache */
	if (likely(cachegc))
		si_cachepool_push(cache);
	if (rc < 0) {
		/* error */
		assert(q.result == NULL);
		return -1;
	} else if (rc == 0) {
		/* not found */
		assert(q.result == NULL);
		*result = NULL;
		return 0;
	} else if (rc == 2) {
		/* cache miss */
		assert(q.result == NULL);
		assert(cache_only);
		*result = NULL;
		return 0;
	}

	/* found */
	assert(rc == 1);

	assert(q.result != NULL);
	statget->read_disk = q.read_disk;
	statget->read_cache = q.read_cache;
	statget->read_latency = clock_monotonic64() - start;

	*result = q.result;
	return 0;
}

struct vinyl_index *
vinyl_index_new(struct vinyl_env *e, struct key_def *key_def)
{
	assert(key_def->part_count > 0);
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 ":%" PRIu32,
	         key_def->space_id, key_def->iid);
	struct vinyl_index *dup = vinyl_index_by_name(e, name);
	if (unlikely(dup)) {
		sr_error("index '%s' already exists", name);
		return NULL;
	}
	struct vinyl_index *index = malloc(sizeof(struct vinyl_index));
	if (unlikely(index == NULL)) {
		sr_oom();
		return NULL;
	}
	memset(index, 0, sizeof(*index));
	index->env = e;
	index->r = &e->r;
	sr_statusinit(&index->status);
	int rc = si_plannerinit(&index->p, index);
	if (unlikely(rc == -1))
		goto error_1;
	si_confinit(&index->conf);
	if (si_confcreate(&index->conf, key_def))
		goto error_2;
	index->key_def = key_def_dup(key_def);
	if (index->key_def == NULL)
		goto error_3;
	ss_bufinit(&index->readbuf);
	sv_upsertinit(&index->u);
	sinode_tree_new(&index->tree);
	tt_pthread_mutex_init(&index->lock, NULL);
	rlist_create(&index->link);
	rlist_create(&index->gc);
	index->gc_count     = 0;
	index->update_time  = 0;
	index->lru_run_lsn  = 0;
	index->lru_v        = 0;
	index->lru_steps    = 1;
	index->lru_intr_lsn = 0;
	index->lru_intr_sum = 0;
	index->size         = 0;
	index->read_disk    = 0;
	index->read_cache   = 0;
	index->n            = 0;
	tt_pthread_mutex_init(&index->ref_lock, NULL);
	index->refs         = 1;
	sr_statusset(&index->status, SR_OFFLINE);
	sx_indexinit(&index->coindex, &e->xm, index, index->key_def);
	index->txn_min = sx_min(&e->xm);
	index->txn_max = UINT32_MAX;
	rlist_add(&e->indexes, &index->link);
	return index;

error_3:
	si_conffree(&index->conf);
error_2:
	si_plannerfree(&index->p);
error_1:
	free(index);
	return NULL;
}

static inline int
vinyl_index_delete(struct vinyl_index *index)
{
	struct vinyl_env *e = index->env;
	sx_indexfree(&index->coindex, &e->xm);
	int rc_ret = 0;
	int rc = 0;
	struct sinode *node, *n;
	rlist_foreach_entry_safe(node, &index->gc, gc, n) {
		rc = si_nodefree(node, index->r, 1);
		if (unlikely(rc == -1))
			rc_ret = -1;
	}
	rlist_create(&index->gc);
	index->gc_count = 0;

	sinode_tree_iter(&index->tree, NULL, sinode_tree_free_cb, index->r);
	sv_upsertfree(&index->u);
	ss_buffree(&index->readbuf);
	si_plannerfree(&index->p);
	tt_pthread_mutex_destroy(&index->lock);
	tt_pthread_mutex_destroy(&index->ref_lock);
	sr_statusfree(&index->status);
	si_conffree(&index->conf);
	key_def_delete(index->key_def);
	TRASH(index);
	free(index);
	return rc_ret;
}

struct vinyl_index *
vinyl_index_by_name(struct vinyl_env *e, const char *name)
{
	struct vinyl_index *index;
	rlist_foreach_entry(index, &e->indexes, link) {
		if (strcmp(index->conf.name, name) == 0)
			return index;
	}
	return NULL;
}

static int vinyl_index_visible(struct vinyl_index *index, uint64_t txn)
{
	return txn > index->txn_min && txn <= index->txn_max;
}

size_t
vinyl_index_bsize(struct vinyl_index *index)
{
	si_profilerbegin(&index->rtp, index);
	si_profiler(&index->rtp);
	si_profilerend(&index->rtp);
	return index->rtp.memory_used;
}

uint64_t
vinyl_index_size(struct vinyl_index *index)
{
	si_profilerbegin(&index->rtp, index);
	si_profiler(&index->rtp);
	si_profilerend(&index->rtp);
	return index->rtp.count;
}

static int vinyl_index_recoverbegin(struct vinyl_index *index)
{
	/* open and recover repository */
	sr_statusset(&index->status, SR_FINAL_RECOVERY);
	/* do not allow to recover existing indexes
	 * during online (only create), since logpool
	 * reply is required. */
	int rc = si_recover(index);
	if (unlikely(rc == -1))
		goto error;
	return 0;
error:
	sr_statusset(&index->status, SR_MALFUNCTION);
	return -1;
}

static int vinyl_index_recoverend(struct vinyl_index *index)
{
	int status = sr_status(&index->status);
	if (unlikely(status == SR_DROP_PENDING))
		return 0;
	sr_statusset(&index->status, SR_ONLINE);
	return 0;
}

static struct vinyl_tuple *
vinyl_tuple_new(struct vinyl_index *index, struct vinyl_field *fields,
	       uint32_t fields_count)
{
	struct key_def *key_def = index->key_def;
	struct runtime *r = index->r;
	assert(fields_count == key_def->part_count + 1);
	(void) fields_count;
	int size = sf_writesize(key_def, fields);
	struct vinyl_tuple *v = malloc(sizeof(struct vinyl_tuple) + size);
	if (unlikely(v == NULL))
		return NULL;
	v->size      = size;
	v->lsn       = 0;
	v->flags     = 0;
	v->refs      = 1;
	char *ptr = v->data;
	sf_write(key_def, fields, ptr);
	/* update runtime statistics */
	tt_pthread_mutex_lock(&r->stat->lock);
	r->stat->v_count++;
	r->stat->v_allocated += sizeof(struct vinyl_tuple) + size;
	tt_pthread_mutex_unlock(&r->stat->lock);
	return v;
}

static inline struct vinyl_tuple*
vinyl_tuple_from_sv(struct runtime *r, struct sv *sv)
{
	char *src = sv_pointer(sv);
	size_t size = sv_size(sv);
	struct vinyl_tuple *v = malloc(sizeof(struct vinyl_tuple) + size);
	if (unlikely(v == NULL))
		return NULL;
	v->size      = size;
	v->flags     = 0;
	v->refs      = 1;
	v->lsn       = 0;
	v->flags     = sv_flags(sv);
	v->lsn       = sv_lsn(sv);
		memcpy(v->data, src, size);
	/* update runtime statistics */
	tt_pthread_mutex_lock(&r->stat->lock);
	r->stat->v_count++;
	r->stat->v_allocated += sizeof(struct vinyl_tuple) + size;
	tt_pthread_mutex_unlock(&r->stat->lock);
	return v;
}

enum { VINYL_KEY_MAXLEN = 1024 };
static char VINYL_MP_STRING_MIN[1];
static char VINYL_MP_STRING_MAX[VINYL_KEY_MAXLEN];
static char VINYL_MP_UINT_MIN[1];
static char VINYL_MP_UINT_MAX[9];

static inline int
vinyl_set_fields(struct vinyl_field *fields,
		 const char **data, uint32_t part_count)
{
	for (uint32_t i = 0; i < part_count; i++) {
		struct vinyl_field *field = &fields[i];
		field->data = *data;
		mp_next(data);
		field->size = *data - field->data;
		if (field->size > VINYL_KEY_MAXLEN) {
			diag_set(ClientError, ER_KEY_PART_IS_TOO_LONG,
				  field->size, VINYL_KEY_MAXLEN);
			return -1;
		}
	}
	return 0;
}

struct vinyl_tuple *
vinyl_tuple_from_key_data(struct vinyl_index *index, const char *key,
			 uint32_t part_count, int order)
{
	struct key_def *key_def = index->key_def;
	assert(part_count == 0 || key != NULL);
	assert(part_count <= key_def->part_count);
	struct vinyl_field fields[BOX_INDEX_PART_MAX + 1]; /* parts + value */
	if (vinyl_set_fields(fields, &key, part_count) != 0)
		return NULL;
	/* Fill remaining parts of key */
	for (uint32_t i = part_count; i < key_def->part_count; i++) {
		struct vinyl_field *field = &fields[i];
		if ((i > 0 && (order == VINYL_GT || order == VINYL_LE)) ||
		    (i == 0 && (order == VINYL_LT || order == VINYL_LE))) {
			switch (key_def->parts[i].type) {
			case NUM:
				field->data = VINYL_MP_UINT_MAX;
				field->size = sizeof(VINYL_MP_UINT_MAX);
				break;
			case STRING:
				field->data = VINYL_MP_STRING_MAX;
				field->size = sizeof(VINYL_MP_STRING_MAX);
				break;
			default:
				unreachable();
			}
		} else if ((i > 0 && (order == VINYL_GE || order == VINYL_LT)) ||
			   (i == 0 && (order == VINYL_GT || order == VINYL_GE))) {
			switch (key_def->parts[i].type) {
			case NUM:
				field->data = VINYL_MP_UINT_MIN;
				field->size = sizeof(VINYL_MP_UINT_MIN);
				break;
			case STRING:
				field->data = VINYL_MP_STRING_MIN;
				field->size = sizeof(VINYL_MP_STRING_MAX);
				break;
			default:
				unreachable();
			}
		} else {
			unreachable();
		}
	}
	/* Add an empty value. Value is stored after key parts. */
	struct vinyl_field *value_field = &fields[key_def->part_count];
	if (order == VINYL_LT || order == VINYL_LE) {
		value_field->data = VINYL_MP_STRING_MAX;
		value_field->size = sizeof(VINYL_MP_STRING_MAX);
	} else {
		value_field->data = VINYL_MP_STRING_MIN;
		value_field->size = sizeof(VINYL_MP_STRING_MIN);
	}
	/* Create tuple */
	return vinyl_tuple_new(index, fields, key_def->part_count + 1);
}

/*
 * Create vinyl_tuple from raw MsgPack data.
 */
struct vinyl_tuple *
vinyl_tuple_from_data(struct vinyl_index *index, const char *data,
		     const char *data_end)
{
	struct key_def *key_def = index->key_def;

	uint32_t count = mp_decode_array(&data);
	assert(count >= key_def->part_count);
	(void) count;

	struct vinyl_field fields[BOX_INDEX_PART_MAX + 1]; /* parts + value */
	if (vinyl_set_fields(fields, &data, key_def->part_count) != 0)
		return NULL;

	/* Value is stored after key parts */
	struct vinyl_field *value = &fields[key_def->part_count];
	value->data = data;
	value->size = data_end - data;
	return vinyl_tuple_new(index, fields, key_def->part_count + 1);
}

static inline uint32_t
vinyl_calc_fieldslen(struct key_def *key_def, struct vinyl_field *fields,
		     uint32_t *field_count)
{
	/* prepare keys */
	uint32_t size = 0;
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		struct vinyl_field *field = &fields[i];
		assert(field->data != NULL);
		size += field->size;
	}

	uint32_t count = key_def->part_count;
	struct vinyl_field *value_field = &fields[key_def->part_count];
	const char *value = value_field->data;
	const char *valueend = value + value_field->size;
	while (value < valueend) {
		count++;
		mp_next(&value);
	}
	size += mp_sizeof_array(count);
	size += value_field->size;

	*field_count = count;
	return size;
}

char *
vinyl_convert_tuple_data(struct vinyl_index *index,
			 struct vinyl_tuple *vinyl_tuple, uint32_t *bsize)
{
	struct key_def *key_def = index->key_def;
	assert(key_def->part_count <= BOX_INDEX_PART_MAX);
	struct vinyl_field fields[BOX_INDEX_PART_MAX + 1]; /* parts + value */
	for (uint32_t i = 0; i <= key_def->part_count; i++) {
		struct vinyl_field *field = &fields[i];
		field->data = sf_field(index->key_def, i, vinyl_tuple->data,
				       &field->size);
	}
	uint32_t field_count = 0;
	size_t size = vinyl_calc_fieldslen(key_def, fields, &field_count);
	char *tuple_data = (char *) malloc(size);
	if (tuple_data == NULL) {
		diag_set(OutOfMemory, size, "malloc", "tuple");
		return NULL;
	}
	char *d = tuple_data;
	d = mp_encode_array(d, field_count);
	/* For each key part + value */
	for (uint32_t i = 0; i <= key_def->part_count; i++) {
		struct vinyl_field *field = &fields[i];
		memcpy(d, field->data, field->size);
		d += field->size;
	}
	assert(tuple_data + size == d);
	*bsize = size;
	return tuple_data;
}

struct tuple *
vinyl_convert_tuple(struct vinyl_index *index, struct vinyl_tuple *vinyl_tuple,
		    struct tuple_format *format)
{
	assert(format);
	uint32_t bsize;
	char *data = vinyl_convert_tuple_data(index, vinyl_tuple, &bsize);
	if (data == NULL)
		return NULL;

	struct tuple *tuple = box_tuple_new(format, data, data + bsize);
	free(data);
	return tuple;
}

void
vinyl_tuple_ref(struct vinyl_tuple *v)
{
	pm_atomic_fetch_add_explicit(&v->refs, 1, pm_memory_order_relaxed);
}

static int
vinyl_tuple_unref_rt(struct runtime *r, struct vinyl_tuple *v)
{
	uint16_t old_refs = pm_atomic_fetch_sub_explicit(&v->refs, 1,
		pm_memory_order_relaxed);
	assert(old_refs > 0);
	if (likely(old_refs == 1)) {
		uint32_t size = vinyl_tuple_size(v);
		/* update runtime statistics */
		tt_pthread_mutex_lock(&r->stat->lock);
		assert(r->stat->v_count > 0);
		assert(r->stat->v_allocated >= size);
		r->stat->v_count--;
		r->stat->v_allocated -= size;
		tt_pthread_mutex_unlock(&r->stat->lock);
#ifndef NDEBUG
		memset(v, '#', vinyl_tuple_size(v)); /* fail early */
#endif
		free(v);
		return 1;
	}
	return 0;
}

void
vinyl_tuple_unref(struct vinyl_index *index, struct vinyl_tuple *tuple)
{
	struct runtime *r = index->r;
	vinyl_tuple_unref_rt(r, tuple);
}

int64_t
vinyl_tuple_lsn(struct vinyl_tuple *tuple)
{
	return tuple->lsn;
}

static inline int
vinyl_tuple_validate(struct vinyl_tuple *o, struct vinyl_index *index,
		    uint8_t flags)
{
	struct vinyl_env *e = index->env;
	o->flags = flags;
	if (o->lsn != 0) {
		uint64_t lsn = sr_seq(&e->seq, SR_LSN);
		if (o->lsn <= lsn)
			return sr_error("%s", "incompatible document lsn");
	}
	return 0;
}

static inline int
vinyl_tx_write(struct vinyl_tx *t, struct vinyl_index *index,
	      struct vinyl_tuple *o, uint8_t flags)
{
	struct vinyl_env *e = t->env;

	/* validate req */
	if (unlikely(t->t.state == SXPREPARE)) {
		sr_error("%s", "transaction is in 'prepare' state (read-only)");
		return -1;
	}

	/* validate index status */
	int status = sr_status(&index->status);
	switch (status) {
	case SR_SHUTDOWN_PENDING:
	case SR_DROP_PENDING:
		if (unlikely(! vinyl_index_visible(index, t->t.id))) {
			sr_error("%s", "index is invisible for the transaction");
			return -1;
		}
		break;
	case SR_INITIAL_RECOVERY:
	case SR_FINAL_RECOVERY:
	case SR_ONLINE: break;
	default:
		return sr_malfunction("%s", "index in malfunction state");
	}

	int rc = vinyl_tuple_validate(o, index, flags);
	if (unlikely(rc == -1))
		return -1;

	vinyl_tuple_ref(o);

	/* concurrent index only */
	rc = sx_set(&t->t, &index->coindex, o);
	if (unlikely(rc != 0))
		return -1;

	int size = vinyl_tuple_size(o);
	ss_quota(&e->quota, SS_QADD, size);
	return 0;
}

int
vinyl_replace(struct vinyl_tx *tx, struct vinyl_index *index,
	     struct vinyl_tuple *tuple)
{
	return vinyl_tx_write(tx, index, tuple, 0);
}

int
vinyl_upsert(struct vinyl_tx *tx, struct vinyl_index *index,
	    const char *tuple, const char *tuple_end,
	    const char *expr, const char *expr_end, int index_base)
{
	struct key_def *key_def = index->key_def;

	/* upsert */
	mp_decode_array(&tuple);

	/* Set key fields */
	struct vinyl_field fields[BOX_INDEX_PART_MAX + 1]; /* parts + value */
	const char *tuple_value = tuple;
	if (vinyl_set_fields(fields, &tuple_value, key_def->part_count) != 0)
		return -1;

	/*
	 * Set value field:
	 *  - index_base: uint8_t
	 *  - tuple_tail_size: uint32_t
	 *  - tuple_tail: char
	 *  - expr: char
	 */
	uint32_t expr_size  = expr_end - expr;
	uint32_t tuple_value_size = tuple_end - tuple_value;
	uint32_t value_size = sizeof(uint8_t) + sizeof(uint32_t) +
		tuple_value_size + expr_size;
	char *value = (char *)malloc(value_size);
	if (value == NULL) {
		diag_set(OutOfMemory, sizeof(value_size), "vinyl",
		          "upsert");
		return -1;
	}
	char *p = value;
	memcpy(p, &index_base, sizeof(uint8_t));
	p += sizeof(uint8_t);
	memcpy(p, &tuple_value_size, sizeof(uint32_t));
	p += sizeof(uint32_t);
	memcpy(p, tuple_value, tuple_value_size);
	p += tuple_value_size;
	memcpy(p, expr, expr_size);
	p += expr_size;
	assert(p == value + value_size);

	/* Value is stored after key parts */
	struct vinyl_field *value_field = &fields[key_def->part_count];
	value_field->data = value;
	value_field->size = value_size;

	struct vinyl_tuple *vinyl_tuple =
		vinyl_tuple_new(index, fields, key_def->part_count + 1);
	free(value);
	if (vinyl_tuple == NULL)
		return -1;

	int rc = vinyl_tx_write(tx, index, vinyl_tuple, SVUPSERT);
	vinyl_tuple_unref(index, vinyl_tuple);
	return rc;
}

int
vinyl_delete(struct vinyl_tx *tx, struct vinyl_index *index,
	    struct vinyl_tuple *tuple)
{
	return vinyl_tx_write(tx, index, tuple, SVDELETE);
}

static inline int
vinyl_get(struct vinyl_tx *tx, struct vinyl_index *index, struct vinyl_tuple *key,
	 struct vinyl_tuple **result, bool cache_only)
{
	/* Optimization: allow NULL tx */
	struct sx *sx = tx != NULL ? &tx->t : NULL;

	struct vinyl_statget statget;
	if (vinyl_index_read(index, key, VINYL_EQ, result, sx, NULL,
			    cache_only, &statget) != 0) {
		return -1;
	}

	if (*result == NULL)
		return 0;

	sr_statget(&index->env->stat, &statget);
	return 0;
}

static inline void
vinyl_tx_end(struct vinyl_tx *t, int rlb, int conflict)
{
	struct vinyl_env *e = t->env;
	uint32_t count = sv_logcount(&t->log);
	sx_gc(&t->t);
	sv_logreset(&t->log);
	sr_stattx(&e->stat, t->start, count, rlb, conflict);
	sv_logfree(&t->log);
	free(t);
}

int
vinyl_rollback(struct vinyl_tx *tx)
{
	sx_rollback(&tx->t);
	vinyl_tx_end(tx, 1, 0);
	return 0;
}

static inline int
sx_preparev(struct sx *x, struct svlogv *v, uint64_t lsn,
	    struct sicache *cache, enum vinyl_status status)
{
	if (lsn == x->vlsn || status == SR_FINAL_RECOVERY)
		return 0;

	struct sv *sv = &v->v;
	struct sxv *sxv = sv_to_sxv(sv);
	struct vinyl_tuple *key = sxv->tuple;
	struct sxindex *sxindex = sxv->index;
	struct vinyl_index *index = sxindex->index;

	struct siread q;
	si_readopen(&q, index, cache,
	            VINYL_EQ,
	            x->vlsn,
	            key->data,
	            key->size);
	q.has = 1;
	int rc = si_read(&q);
	si_readclose(&q);

	if (unlikely(q.result))
		vinyl_tuple_unref(index, q.result);
	if (rc)
		return 1;

	return 0;
}

int
vinyl_prepare(struct vinyl_tx *t)
{
	struct vinyl_env *e = t->env;
	if (unlikely(! sr_statusactive_is(e->status)))
		return -1;

	/* prepare transaction */
	assert(t->t.state == SXREADY);
	struct sicache *cache = si_cachepool_pop(&e->cachepool);
	if (unlikely(cache == NULL))
		return sr_oom();
	enum sxstate s = sx_prepare(&t->t, cache, e->status);

	si_cachepool_push(cache);
	if (s == SXLOCK) {
		sr_stattx_lock(&e->stat);
		return 2;
	}
	if (s == SXROLLBACK) {
		return 1;
	}
	assert(s == SXPREPARE);

	sx_commit(&t->t);

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
vinyl_commit(struct vinyl_tx *t, int64_t lsn)
{
	struct vinyl_env *e = t->env;
	if (unlikely(! sr_statusactive_is(e->status)))
		return -1;
	assert(t->t.state == SXCOMMIT);

	/* do wal write and backend commit */
	int rc = sc_write(&e->scheduler, &t->log, lsn, e->status);
	if (unlikely(rc == -1))
		sx_rollback(&t->t);

	vinyl_tx_end(t, 0, 0);
	return rc;
}

struct vinyl_tx *
vinyl_begin(struct vinyl_env *e)
{
	struct vinyl_tx *t;
	t = malloc(sizeof(struct vinyl_tx));
	if (unlikely(t == NULL)) {
		sr_oom();
		return NULL;
	}
	t->env = e;
	sv_loginit(&t->log);
	t->start = clock_monotonic64();
	sx_begin(&e->xm, &t->t, SXRW, &t->log);
	return t;
}

/** {{ vinyl_read_task - Asynchronous I/O using eio pool */

struct vinyl_read_task {
	struct coio_task base;
	struct vinyl_index *index;
	struct vinyl_cursor *cursor;
	struct vinyl_tx *tx;
	struct vinyl_tuple *key;
	struct vinyl_tuple *result;
};

static ssize_t
vinyl_get_cb(struct coio_task *ptr)
{
	struct vinyl_read_task *task =
		(struct vinyl_read_task *) ptr;
	return vinyl_get(task->tx, task->index, task->key, &task->result, false);
}

static ssize_t
vinyl_cursor_next_cb(struct coio_task *ptr)
{
	struct vinyl_read_task *task =
		(struct vinyl_read_task *) ptr;
	return vinyl_cursor_next(task->cursor, &task->result, false);
}

static ssize_t
vinyl_read_task_free_cb(struct coio_task *ptr)
{
	struct vinyl_read_task *task =
		(struct vinyl_read_task *) ptr;
	struct vinyl_env *env = task->index->env;
	assert(env != NULL);
	if (task->result != NULL)
		vinyl_tuple_unref(task->index, task->result);
	vinyl_index_unref(task->index);
	mempool_free(&env->read_pool, task);
	return 0;
}

static inline int
vinyl_read_task(struct vinyl_index *index, struct vinyl_tx *tx,
	       struct vinyl_cursor *cursor, struct vinyl_tuple *key,
	       struct vinyl_tuple **result,
	       coio_task_cb func)
{
	assert(index != NULL);
	struct vinyl_env *env = index->env;
	assert(env != NULL);
	struct vinyl_read_task *task = (struct vinyl_read_task *)
		mempool_alloc(&env->read_pool);
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task), "mempool",
			 "vinyl_read_task");
		return -1;
	}
	task->index = index;
	vinyl_index_ref(index);
	task->tx = tx;
	task->cursor = cursor;
	task->key = key;
	task->result = NULL;
	if (coio_task(&task->base, func, vinyl_read_task_free_cb,
	              TIMEOUT_INFINITY) == -1) {
		return -1;
	}
	vinyl_index_unref(index);
	*result = task->result;
	int rc = task->base.base.result; /* save original error code */
	mempool_free(&env->read_pool, task);
	assert(rc == 0 || !diag_is_empty(&fiber()->diag));
	return rc;
}

int
vinyl_coget(struct vinyl_tx *tx, struct vinyl_index *index,
	    struct vinyl_tuple *key, struct vinyl_tuple **result)
{
	*result = NULL;
	int rc = vinyl_get(tx, index, key, result, true);
	if (rc != 0)
		return rc;
	if (*result != NULL) /* found */
		return 0;

	 /* cache miss or not found */
	return vinyl_read_task(index, tx, NULL, key, result,
				     vinyl_get_cb);
}

int
vinyl_cursor_conext(struct vinyl_cursor *cursor, struct vinyl_tuple **result)
{
	*result = NULL;
	int rc = vinyl_cursor_next(cursor, result, true);
	if (rc != 0)
		return rc;
	if (*result != NULL)
		return 0; /* found */

	return vinyl_read_task(cursor->index, NULL, cursor, NULL, result,
			      vinyl_cursor_next_cb);
}

/** }} vinyl_read_task */

struct vinyl_env *
vinyl_env_new(void)
{
	struct vinyl_env *e = malloc(sizeof(*e));
	if (unlikely(e == NULL))
		return NULL;
	memset(e, 0, sizeof(*e));
	rlist_create(&e->indexes);
	e->status = SR_OFFLINE;

	if (se_confinit(&e->conf, e))
		goto error;
	/* set memory quota (disable during recovery) */
	ss_quotainit(&e->quota, e->conf.memory_limit);
	sr_seqinit(&e->seq);
	sr_statinit(&e->stat);
	sr_init(&e->r, &e->quota,
		&e->conf.zones, &e->seq, &e->stat);
	sx_managerinit(&e->xm, &e->r);
	si_cachepool_init(&e->cachepool, &e->r);
	sc_init(&e->scheduler, &e->r);

	/*
	 * Initialize limits
	 */

	char *d;

	d = mp_encode_uint(VINYL_MP_UINT_MIN, 0);
	assert(d - VINYL_MP_UINT_MIN == sizeof(VINYL_MP_UINT_MIN));

	d = mp_encode_uint(VINYL_MP_UINT_MAX, UINT64_MAX);
	assert(d - VINYL_MP_UINT_MAX == sizeof(VINYL_MP_UINT_MAX));

	d = mp_encode_strl(VINYL_MP_STRING_MIN, 0);
	assert(d - VINYL_MP_STRING_MIN == sizeof(VINYL_MP_STRING_MIN));

	uint32_t len = VINYL_KEY_MAXLEN - mp_sizeof_strl(VINYL_KEY_MAXLEN);
	d = mp_encode_strl(VINYL_MP_STRING_MAX, len);
	assert(d + len - VINYL_MP_STRING_MAX == sizeof(VINYL_MP_STRING_MAX));
	memset(d, 0xff, len);

	mempool_create(&e->read_pool, cord_slab_cache(),
	               sizeof(struct vinyl_read_task));
	return e;
error:
	se_conffree(&e->conf);
	free(e);
	return NULL;
}

/** {{{ vinyl_service - context of a vinyl background thread */

struct vinyl_service *
vinyl_service_new(struct vinyl_env *env)
{
	struct vinyl_service *srv = malloc(sizeof(struct vinyl_service));
	if (srv == NULL) {
		sr_oom();
		return NULL;
	}
	srv->env = env;
	sd_cinit(&srv->sdc);
	return srv;
}

int
vinyl_service_do(struct vinyl_service *srv)
{
	if (! sr_statusactive_is(srv->env->status))
		return 0;

	return sc_step(srv, sx_vlsn(&srv->env->xm));
}

void
vinyl_service_delete(struct vinyl_service *srv)
{
	sd_cfree(&srv->sdc);
	free(srv);
}

/* }}} vinyl service */
