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
#include "coeio.h"

#include "errcode.h"
#include "key_def.h"
#include "tuple.h"
#include "tuple_update.h"
#include "txn.h" /* box_txn_alloc() */

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

struct vy_sequence;
struct vy_conf;
struct vy_quota;
struct vy_cachepool;
struct tx_manager;
struct vy_scheduler;
struct vy_stat;
struct srzone;

struct vy_env {
	enum vinyl_status status;
	/** List of open spaces. */
	struct rlist indexes;
	struct vy_sequence  *seq;
	struct vy_conf      *conf;
	struct vy_quota     *quota;
	struct vy_cachepool *cachepool;
	struct tx_manager   *xm;
	struct vy_scheduler *scheduler;
	struct vy_stat      *stat;
	struct mempool      read_task_pool;
	struct mempool      cursor_pool;
};

static inline struct srzone *
sr_zoneof(struct vy_env *r);

enum vy_sequence_op {
	/**
	 * The latest LSN of the write ahead log known to the
	 * engine.
	 */
	VINYL_LSN,
	/**
	 * The oldest LSN used in one of the read views in
	 * a transaction in the engine.
	 */
	VINYL_VIEW_LSN,
	VINYL_NSN_NEXT,
};

struct vy_sequence {
	pthread_mutex_t lock;
	/** Log sequence number. */
	uint64_t lsn;
	/**
	 * View sequence number: the oldest read view maintained
	 * by the front end.
	 */
	uint64_t vlsn;
	/** Node sequence number. */
	uint64_t nsn;
};

static inline struct vy_sequence *
vy_sequence_new()
{
	struct vy_sequence *seq = calloc(1, sizeof(*seq));
	if (seq == NULL) {
		diag_set(OutOfMemory, sizeof(*seq), "sequence",
			 "struct sequence");
		return NULL;
	}
	tt_pthread_mutex_init(&seq->lock, NULL);
	return seq;
}

static inline void
vy_sequence_delete(struct vy_sequence *n)
{
	tt_pthread_mutex_destroy(&n->lock);
	free(n);
}

static inline void
vy_sequence_lock(struct vy_sequence *n) {
	tt_pthread_mutex_lock(&n->lock);
}

static inline void
vy_sequence_unlock(struct vy_sequence *n) {
	tt_pthread_mutex_unlock(&n->lock);
}

static inline uint64_t
vy_sequence_do(struct vy_sequence *n, enum vy_sequence_op op)
{
	uint64_t v = 0;
	switch (op) {
	case VINYL_LSN:
		v = n->lsn;
		break;
	case VINYL_VIEW_LSN:
		v = n->vlsn;
		break;
	case VINYL_NSN_NEXT:
		v = ++n->nsn;
		break;
	}
	return v;
}

static inline uint64_t
vy_sequence(struct vy_sequence *n, enum vy_sequence_op op)
{
	vy_sequence_lock(n);
	uint64_t v = vy_sequence_do(n, op);
	vy_sequence_unlock(n);
	return v;
}

static void
vy_path_init(char *p, char *dir, uint64_t id, char *ext)
{
	snprintf(p, PATH_MAX, "%s/%020"PRIu64"%s", dir, id, ext);
}

static void
vy_path_compound(char *p, char *dir, uint64_t a, uint64_t b, char *ext)
{
	snprintf(p, PATH_MAX, "%s/%020"PRIu64".%020"PRIu64"%s", dir, a, b, ext);
}

struct vy_iov {
	struct iovec *v;
	int iovmax;
	int iovc;
};

static inline void
vy_iov_init(struct vy_iov *v, struct iovec *vp, int max)
{
	v->v = vp;
	v->iovc = 0;
	v->iovmax = max;
}

static inline void
vy_iov_add(struct vy_iov *v, void *ptr, size_t size)
{
	assert(v->iovc < v->iovmax);
	v->v[v->iovc].iov_base = ptr;
	v->v[v->iovc].iov_len = size;
	v->iovc++;
}

struct vy_mmap {
	char *p;
	size_t size;
};

static int
vy_mmap_map(struct vy_mmap *m, int fd, uint64_t size, int ro)
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
vy_mmap_unmap(struct vy_mmap *m)
{
	if (unlikely(m->p == NULL))
		return 0;
	int rc = munmap(m->p, m->size);
	m->p = NULL;
	return rc;
}

struct vy_file {
	int fd;
	uint64_t size;
	int creat;
	char path[PATH_MAX];
};

static inline void
vy_file_init(struct vy_file *f)
{
	f->path[0] = '\0';
	f->fd    = -1;
	f->size  = 0;
	f->creat = 0;
}

static inline int
vy_file_open_as(struct vy_file *f, char *path, int flags)
{
	f->creat = (flags & O_CREAT ? 1 : 0);
	f->fd = open(path, flags, 0644);
	if (unlikely(f->fd == -1))
		return -1;
	snprintf(f->path, PATH_MAX, "%s", path);
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
vy_file_open(struct vy_file *f, char *path) {
	return vy_file_open_as(f, path, O_RDWR);
}

static inline int
vy_file_new(struct vy_file *f, char *path) {
	return vy_file_open_as(f, path, O_RDWR|O_CREAT);
}

static inline int
vy_file_close(struct vy_file *f)
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
vy_file_rename(struct vy_file *f, char *path)
{
	int rc = rename(f->path, path);
	if (unlikely(rc == -1))
		return -1;
	snprintf(f->path, PATH_MAX, "%s", path);
	return 0;
}

static inline int
vy_file_sync(struct vy_file *f) {
	return fdatasync(f->fd);
}

static inline int
vy_file_advise(struct vy_file *f, int hint, uint64_t off, uint64_t len) {
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
vy_file_resize(struct vy_file *f, uint64_t size)
{
	int rc = ftruncate(f->fd, size);
	if (unlikely(rc == -1))
		return -1;
	f->size = size;
	return 0;
}

static inline int
vy_file_pread(struct vy_file *f, uint64_t off, void *buf, int size)
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
vy_file_pwrite(struct vy_file *f, uint64_t off, void *buf, int size)
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
vy_file_write(struct vy_file *f, void *buf, int size)
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
vy_file_writev(struct vy_file *f, struct vy_iov *iov)
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
vy_file_seek(struct vy_file *f, uint64_t off)
{
	return lseek(f->fd, off, SEEK_SET);
}

struct vy_buf {
	/** Start of the allocated buffer */
	char *s;
	/** End of the used area */
	char *p;
	/** End of the buffer */
	char *e;
};

static inline void
vy_buf_init(struct vy_buf *b)
{
	b->s = NULL;
	b->p = NULL;
	b->e = NULL;
}

static inline void
vy_buf_free(struct vy_buf *b)
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

static inline void
vy_buf_reset(struct vy_buf *b) {
	b->p = b->s;
}

static inline void
vy_buf_gc(struct vy_buf *b, size_t wm)
{
	if (unlikely(vy_buf_size(b) >= wm)) {
		vy_buf_free(b);
		vy_buf_init(b);
		return;
	}
	vy_buf_reset(b);
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

#define VINYL_INJECTION_SD_BUILD_0      0
#define VINYL_INJECTION_SD_BUILD_1      1
#define VINYL_INJECTION_SI_DUMP_0       2
#define VINYL_INJECTION_SI_COMPACTION_0 3
#define VINYL_INJECTION_SI_COMPACTION_1 4
#define VINYL_INJECTION_SI_COMPACTION_2 5
#define VINYL_INJECTION_SI_COMPACTION_3 6
#define VINYL_INJECTION_SI_COMPACTION_4 7
#define VINYL_INJECTION_SI_RECOVER_0    8

#ifdef VINYL_INJECTION_ENABLE
	#define VINYL_INJECTION(E, ID, X) \
	if ((E)->e[(ID)]) { \
		X; \
	} else {}
#else
	#define VINYL_INJECTION(E, ID, X)
#endif

#define vy_crcs(p, size, crc) \
	crc32_calc(crc, (char*)p + sizeof(uint32_t), size - sizeof(uint32_t))

enum vy_quotaop {
	VINYL_QADD,
	VINYL_QREMOVE
};

struct vy_quota {
	bool enable;
	int wait;
	int64_t limit;
	int64_t used;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

static struct vy_quota *
vy_quota_new(int64_t);

static int
vy_quota_delete(struct vy_quota*);

static void
vy_quota_enable(struct vy_quota*);

static int
vy_quota_op(struct vy_quota*, enum vy_quotaop, int64_t);

static inline uint64_t
vy_quota_used(struct vy_quota *q)
{
	tt_pthread_mutex_lock(&q->lock);
	uint64_t used = q->used;
	tt_pthread_mutex_unlock(&q->lock);
	return used;
}

static inline int
vy_quota_used_percent(struct vy_quota *q)
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
	int (*init)(struct vy_filter*, va_list);
	int (*free)(struct vy_filter*);
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
vy_filter_init(struct vy_filter *c, struct vy_filterif *ci,
	      enum vy_filter_op op, ...)
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
vy_filter_free(struct vy_filter *c)
{
	return c->i->free(c);
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
	void *(*get)(struct vy_iter*);
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
vy_filter_lz4_init(struct vy_filter *f, va_list args)
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
vy_filter_lz4_free(struct vy_filter *f)
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
	.init     = vy_filter_lz4_init,
	.free     = vy_filter_lz4_free,
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
	q->wait   = 0;
	q->limit  = limit;
	q->used   = 0;
	tt_pthread_mutex_init(&q->lock, NULL);
	tt_pthread_cond_init(&q->cond, NULL);
	return q;
}

static int
vy_quota_delete(struct vy_quota *q)
{
	tt_pthread_mutex_destroy(&q->lock);
	tt_pthread_cond_destroy(&q->cond);
	free(q);
	return 0;
}

static void
vy_quota_enable(struct vy_quota *q)
{
	q->enable = true;
}

static int
vy_quota_op(struct vy_quota *q, enum vy_quotaop op, int64_t v)
{
	if (likely(v == 0))
		return 0;
	tt_pthread_mutex_lock(&q->lock);
	switch (op) {
	case VINYL_QADD:
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
	case VINYL_QREMOVE:
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
struct vy_filter_zstd {
	void *ctx;
};

static int
vy_filter_zstd_init(struct vy_filter *f, va_list args)
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
vy_filter_zstd_free(struct vy_filter *f)
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
	.init     = vy_filter_zstd_init,
	.free     = vy_filter_zstd_free,
	.start    = vy_filter_zstd_start,
	.next     = vy_filter_zstd_next,
	.complete = vy_filter_zstd_complete
};

#define VINYL_VERSION_MAGIC      8529643324614668147ULL

#define VINYL_VERSION_A         '2'
#define VINYL_VERSION_B         '1'
#define VINYL_VERSION_C         '1'
#define VINYL_VERSION "2.1.1"

#define VINYL_VERSION_STORAGE_A '2'
#define VINYL_VERSION_STORAGE_B '1'
#define VINYL_VERSION_STORAGE_C '1'
#define VINYL_VERSION_STORAGE "2.1.1"

struct PACKED srversion {
	uint64_t magic;
	uint8_t  a, b, c;
};

static inline void
sr_version(struct srversion *v)
{
	v->magic = VINYL_VERSION_MAGIC;
	v->a = VINYL_VERSION_A;
	v->b = VINYL_VERSION_B;
	v->c = VINYL_VERSION_C;
}

static inline void
sr_version_storage(struct srversion *v)
{
	v->magic = VINYL_VERSION_MAGIC;
	v->a = VINYL_VERSION_STORAGE_A;
	v->b = VINYL_VERSION_STORAGE_B;
	v->c = VINYL_VERSION_STORAGE_C;
}

static inline int
sr_versionstorage_check(struct srversion *v)
{
	if (v->magic != VINYL_VERSION_MAGIC)
		return 0;
	if (v->a != VINYL_VERSION_STORAGE_A)
		return 0;
	if (v->b != VINYL_VERSION_STORAGE_B)
		return 0;
	if (v->c != VINYL_VERSION_STORAGE_C)
		return 0;
	return 1;
}


#define vy_e(type, fmt, ...) \
	({int res = -1;\
	  char errmsg[256];\
	  snprintf(errmsg, sizeof(errmsg), fmt, __VA_ARGS__);\
	  diag_set(ClientError, type, errmsg);\
	  res;})

#define vy_error(fmt, ...) \
	vy_e(ER_VINYL, fmt, __VA_ARGS__)

struct vy_status {
	enum vinyl_status status;
	pthread_mutex_t lock;
};

static inline void
vy_status_init(struct vy_status *s)
{
	s->status = VINYL_OFFLINE;
	tt_pthread_mutex_init(&s->lock, NULL);
}

static inline void
vy_status_free(struct vy_status *s)
{
	tt_pthread_mutex_destroy(&s->lock);
}

static inline enum vinyl_status
vy_status_set(struct vy_status *s, enum vinyl_status status)
{
	tt_pthread_mutex_lock(&s->lock);
	enum vinyl_status old = s->status;
	s->status = status;
	tt_pthread_mutex_unlock(&s->lock);
	return old;
}

static inline enum vinyl_status
vy_status(struct vy_status *s)
{
	tt_pthread_mutex_lock(&s->lock);
	enum vinyl_status status = s->status;
	tt_pthread_mutex_unlock(&s->lock);
	return status;
}

static inline bool
vy_status_is_active(enum vinyl_status status)
{
	switch (status) {
	case VINYL_ONLINE:
	case VINYL_INITIAL_RECOVERY:
	case VINYL_FINAL_RECOVERY:
		return true;
	case VINYL_DROP:
	case VINYL_OFFLINE:
	case VINYL_MALFUNCTION:
		return false;
	}
	unreachable();
	return 0;
}

static inline bool
vy_status_online(struct vy_status *s) {
	return vy_status(s) == VINYL_ONLINE;
}

struct vy_stat {
	pthread_mutex_t lock;
	/* memory */
	uint64_t v_count;
	uint64_t v_allocated;
	/* set */
	uint64_t set;
	struct vy_avg    set_latency;
	/* delete */
	uint64_t del;
	struct vy_avg    del_latency;
	/* upsert */
	uint64_t upsert;
	struct vy_avg    upsert_latency;
	/* get */
	uint64_t get;
	struct vy_avg    get_read_disk;
	struct vy_avg    get_read_cache;
	struct vy_avg    get_latency;
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
	tt_pthread_mutex_init(&s->lock, NULL);
	return s;
}

static inline void
vy_stat_delete(struct vy_stat *s)
{
	tt_pthread_mutex_destroy(&s->lock);
	free(s);
}

static inline void
vy_stat_prepare(struct vy_stat *s)
{
	vy_avg_prepare(&s->set_latency);
	vy_avg_prepare(&s->del_latency);
	vy_avg_prepare(&s->upsert_latency);
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
	tt_pthread_mutex_lock(&s->lock);
	s->get++;
	vy_avg_update(&s->get_read_disk, statget->read_disk);
	vy_avg_update(&s->get_read_cache, statget->read_cache);
	vy_avg_update(&s->get_latency, statget->read_latency);
	tt_pthread_mutex_unlock(&s->lock);
}

static inline void
vy_stat_tx(struct vy_stat *s, uint64_t start, uint32_t count,
          int rlb, int conflict)
{
	uint64_t diff = clock_monotonic64() - start;
	tt_pthread_mutex_lock(&s->lock);
	s->tx++;
	s->tx_rlb += rlb;
	s->tx_conflict += conflict;
	vy_avg_update(&s->tx_stmts, count);
	vy_avg_update(&s->tx_latency, diff);
	tt_pthread_mutex_unlock(&s->lock);
}

static inline void
vy_stat_cursor(struct vy_stat *s, uint64_t start, int ops)
{
	uint64_t diff = clock_monotonic64() - start;
	tt_pthread_mutex_lock(&s->lock);
	s->cursor++;
	vy_avg_update(&s->cursor_latency, diff);
	vy_avg_update(&s->cursor_ops, ops);
	tt_pthread_mutex_unlock(&s->lock);
}

struct srzone {
	uint32_t enable;
	char     name[4];
	uint32_t compact_wm;
	uint32_t dump_prio;
	uint32_t dump_wm;
	uint32_t dump_age;
	uint32_t dump_age_period;
	uint64_t dump_age_period_us;
	uint32_t dump_age_wm;
	uint32_t gc_prio;
	uint32_t gc_period;
	uint64_t gc_period_us;
	uint32_t gc_wm;
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

struct sv;

struct svif {
	uint8_t   (*flags)(struct sv*);
	uint64_t  (*lsn)(struct sv*);
	char     *(*pointer)(struct sv*);
	uint32_t  (*size)(struct sv*);
};

static struct svif svtuple_if;
static struct svif svref_if;
struct vy_tuple;
struct sdv;
struct sdpageheader;

struct sv {
	struct svif *i;
	void *v, *arg;
};

static inline struct vy_tuple *
sv_to_tuple(struct sv *v)
{
	assert(v->i == &svtuple_if);
	struct vy_tuple *tuple = (struct vy_tuple *)v->v;
	assert(tuple != NULL);
	return tuple;
}

static inline void
sv_from_tuple(struct sv *v, struct vy_tuple *tuple)
{
	v->i   = &svtuple_if;
	v->v   = tuple;
	v->arg = NULL;
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

static inline char*
sv_pointer(struct sv *v) {
	return v->i->pointer(v);
}

static inline uint32_t
sv_size(struct sv *v) {
	return v->i->size(v);
}

struct vy_tuple {
	uint64_t lsn;
	uint32_t size;
	uint16_t refs; /* atomic */
	uint8_t  flags;
	char data[0];
};

static inline uint32_t
vy_tuple_size(struct vy_tuple *v);

static struct vy_tuple *
vy_tuple_alloc(struct vy_index *index, uint32_t size);

static inline const char *
vy_tuple_key_part(const char *tuple_data, uint32_t part_id);

static inline int
vy_tuple_compare(const char *tuple_data_a, const char *tuple_data_b,
		 const struct key_def *key_def);

static struct vy_tuple *
vy_tuple_from_key_data(struct vy_index *index, const char *key,
			  uint32_t part_count);
static void
vy_tuple_ref(struct vy_tuple *tuple);

static void
vy_tuple_unref(struct vy_index *index, struct vy_tuple *tuple);

static int
vy_tuple_unref_rt(struct vy_stat *stat, struct vy_tuple *v);

/** This tuple was created for a read request. */
static bool
vy_tuple_is_r(struct vy_tuple *tuple)
{
	return tuple->flags & SVGET;
}

/** This tuple was created for a write request. */
static bool
vy_tuple_is_w(struct vy_tuple *tuple)
{
	return tuple->flags & (SVUPSERT|SVREPLACE|SVDELETE);
}

/** The tuple, while present in the transaction log, doesn't exist. */
static bool
vy_tuple_is_not_found(struct vy_tuple *tuple)
{
	return tuple->flags & SVDELETE;
}

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
	vy_buf_init(&m->buf);
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
	vy_buf_free(&m->buf);
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
	struct sv *found_val = NULL;
	for (struct svmergesrc *src = im->src; src < im->end; src++)
	{
		struct sv *v = vy_iter_get(src->i);
		if (v == NULL)
			continue;
		if (found_src == NULL) {
			found_val = v;
			found_src = src;
			continue;
		}
		int rc;
		rc = vy_tuple_compare(sv_pointer(found_val), sv_pointer(v),
				      im->merge->key_def);
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

static inline void *
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
	uint64_t vlsn;
	int next;
	int nextdup;
	int save_delete;
	struct sv *v;
	struct sv upsert_sv;
	struct vy_tuple *upsert_tuple;
};

static struct vy_tuple *
vy_apply_upsert(struct sv *upsert, struct sv *object,
		struct vy_index *index, bool suppress_error);

static inline int
sv_readiter_upsert(struct svreaditer *i)
{
	assert(i->upsert_tuple == NULL);
	struct vy_index *index = i->merge->merge->index;
	/* upsert begin */
	struct sv *v = sv_mergeiter_get(i->merge);
	assert(v != NULL);
	assert(sv_flags(v) & SVUPSERT);
	i->upsert_tuple = vy_tuple_alloc(index, sv_size(v));
	i->upsert_tuple->flags = SVUPSERT;
	memcpy(i->upsert_tuple->data, sv_pointer(v), sv_size(v));
	sv_from_tuple(&i->upsert_sv, i->upsert_tuple);
	v = &i->upsert_sv;

	sv_mergeiter_next(i->merge);
	/* iterate over upsert statements */
	int skip = 0;
	for (; sv_mergeiter_has(i->merge); sv_mergeiter_next(i->merge))
	{
		struct sv *next_v = sv_mergeiter_get(i->merge);
		int dup = sv_is(next_v, SVDUP) || sv_mergeisdup(i->merge);
		if (! dup)
			break;
		if (skip)
			continue;
		struct vy_tuple *up = vy_apply_upsert(v, next_v, index, true);
		if (up == NULL)
			return -1; /* memory error */
		vy_tuple_unref(index, i->upsert_tuple);
		i->upsert_tuple = up;
		sv_from_tuple(&i->upsert_sv, i->upsert_tuple);
		v = &i->upsert_sv;
		if (! (sv_flags(next_v) & SVUPSERT))
			skip = 1;
	}
	if (sv_flags(v) & SVUPSERT) {
		struct vy_tuple *up = vy_apply_upsert(v, NULL, index, true);
		if (up == NULL)
			return -1; /* memory error */
		vy_tuple_unref(index, i->upsert_tuple);
		i->upsert_tuple = up;
		sv_from_tuple(&i->upsert_sv, i->upsert_tuple);
		v = &i->upsert_sv;
	}
	return 0;
}

static inline void
sv_readiter_next(struct svreaditer *im)
{
	if (im->upsert_tuple != NULL) {
		vy_tuple_unref(im->merge->merge->index, im->upsert_tuple);
		im->upsert_tuple = NULL;
	}
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
			im->v = &im->upsert_sv;
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
		 uint64_t vlsn, int save_delete)
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
		vy_tuple_unref(im->merge->merge->index, im->upsert_tuple);
		im->upsert_tuple = NULL;
	}
}

static inline void*
sv_readiter_get(struct svreaditer *im)
{
	if (unlikely(im->v == NULL))
		return NULL;
	return im->v;
}

struct svwriteiter {
	uint64_t  vlsn;
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
	struct svmergeiter   *merge;
	struct sv upsert_sv;
	struct vy_tuple *upsert_tuple;
};

static inline int
sv_writeiter_upsert(struct svwriteiter *i)
{
	assert(i->upsert_tuple == NULL);
	/* upsert begin */
	struct vy_index *index = i->merge->merge->index;
	struct sv *v = sv_mergeiter_get(i->merge);
	assert(v != NULL);
	assert(sv_flags(v) & SVUPSERT);
	assert(sv_lsn(v) <= i->vlsn);
	i->upsert_tuple = vy_tuple_alloc(index, sv_size(v));
	i->upsert_tuple->flags = SVUPSERT;
	memcpy(i->upsert_tuple->data, sv_pointer(v), sv_size(v));
	sv_from_tuple(&i->upsert_sv, i->upsert_tuple);
	v = &i->upsert_sv;
	sv_mergeiter_next(i->merge);

	/* iterate over upsert statements */
	int last_non_upd = 0;
	for (; sv_mergeiter_has(i->merge); sv_mergeiter_next(i->merge))
	{
		struct sv *next_v = sv_mergeiter_get(i->merge);
		int flags = sv_flags(next_v);
		int dup = sv_isflags(flags, SVDUP) || sv_mergeisdup(i->merge);
		if (! dup)
			break;
		/* stop forming upserts on a second non-upsert stmt,
		 * but continue to iterate stream */
		if (last_non_upd)
			continue;
		last_non_upd = ! sv_isflags(flags, SVUPSERT);

		struct vy_tuple *up = vy_apply_upsert(v, next_v, index, false);
		if (up == NULL)
			return -1; /* memory error */
		vy_tuple_unref(index, i->upsert_tuple);
		i->upsert_tuple = up;
		sv_from_tuple(&i->upsert_sv, i->upsert_tuple);
		v = &i->upsert_sv;
	}
	if (sv_flags(v) & SVUPSERT) {
		struct vy_tuple *up = vy_apply_upsert(v, NULL, index, false);
		if (up == NULL)
			return -1; /* memory error */
		vy_tuple_unref(index, i->upsert_tuple);
		i->upsert_tuple = up;
		sv_from_tuple(&i->upsert_sv, i->upsert_tuple);
		v = &i->upsert_sv;
	}
	return 0;
}

static inline void
sv_writeiter_next(struct svwriteiter *im)
{
	if (im->upsert_tuple != NULL) {
		vy_tuple_unref(im->merge->merge->index, im->upsert_tuple);
		im->upsert_tuple = NULL;
	}
	if (im->next)
		sv_mergeiter_next(im->merge);
	im->next = 0;
	im->v = NULL;
	im->vdup = 0;

	for (; sv_mergeiter_has(im->merge); sv_mergeiter_next(im->merge))
	{
		struct sv *v = sv_mergeiter_get(im->merge);
		uint64_t lsn = sv_lsn(v);
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
			/* delete (stray or on the run) */
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
					im->v = &im->upsert_sv;
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
		  uint64_t limit,
		  uint32_t sizev, uint64_t vlsn, int save_delete,
		  int save_upsert)
{
	im->upsert_tuple = NULL;
	im->merge       = merge;
	im->limit       = limit;
	im->size        = 0;
	im->sizev       = sizev;
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
		vy_tuple_unref(im->merge->merge->index, im->upsert_tuple);
		im->upsert_tuple = NULL;
	}
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
	struct vy_tuple *v;
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
 * svindex is an in-memory container for vy_tuples in an
 * a single vinyl node.
 * Internally it uses bps_tree to stores struct svref objects,
 * which, in turn, hold pointers to vy_tuple objects.
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
 * During insertion, reference counter of vy_tuple is incremented,
 * during destruction all vy_tuple' reference counters are decremented.
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
	int res = vy_tuple_compare(a.v->data, b.v->data, index->key_def);
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
	int res = vy_tuple_compare(a.v->data, key->data, index->key_def);
	if (res == 0) {
		index->hint_key_is_equal = true;
		res = a.v->lsn > key->lsn ? -1 : a.v->lsn < key->lsn;
	}
	return res;
}

void *
sv_index_alloc_matras_page()
{
	void *res = malloc(BPS_TREE_VINDEX_PAGE_SIZE);
	if (res == NULL) {
		diag_set(OutOfMemory, BPS_TREE_VINDEX_PAGE_SIZE,
			 "malloc", "raw bytes");
	}
	return res;
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
sv_indexfree(struct svindex *i, struct vy_env *env)
{
	assert(i == i->tree.arg);
	struct bps_tree_svindex_iterator itr =
		bps_tree_svindex_itr_first(&i->tree);
	while (!bps_tree_svindex_itr_is_invalid(&itr)) {
		struct vy_tuple *v = bps_tree_svindex_itr_get_elem(&i->tree, &itr)->v;
		vy_tuple_unref_rt(env->stat, v);
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
	/* sic: sync this value with vy_range->used */
	i->used += vy_tuple_size(ref.v);
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
		if (vy_tuple_compare(curr->v->data, prev->v->data,
				     i->key_def) == 0) {
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

static uint8_t
svref_flags(struct sv *v)
{
	struct svref *ref = (struct svref *)v->v;
	return (ref->v)->flags | ref->flags;
}

static uint64_t
svref_lsn(struct sv *v)
{
	struct svref *ref = (struct svref *)v->v;
	return ref->v->lsn;
}

static char*
svref_pointer(struct sv *v)
{
	struct svref *ref = (struct svref *)v->v;
	return ref->v->data;
}

static uint32_t
svref_size(struct sv *v)
{
	struct svref *ref = (struct svref *)v->v;
	return ref->v->size;
}

static struct svif svref_if =
{
       .flags     = svref_flags,
       .lsn       = svref_lsn,
       .pointer   = svref_pointer,
       .size      = svref_size
};

struct svindexiter {
	struct svindex *index;
	struct bps_tree_svindex_iterator itr;
	struct sv current;
	enum vy_order order;
	void *key;
	int key_size;
};

static struct vy_iterif sv_indexiterif;

static inline int
sv_indexiter_open(struct vy_iter *i, struct svindex *index,
		  enum vy_order o, void *key, int key_size)
{
	assert(index == index->tree.arg);
	i->vif = &sv_indexiterif;
	struct svindexiter *ii = (struct svindexiter *)i->priv;
	struct bps_tree_svindex *tree = &index->tree;
	ii->index = index;
	ii->order = o;
	ii->key = key;
	ii->key_size = key_size;
	ii->current.i = &svref_if;
	if (key == NULL) {
		if (o == VINYL_GT || o == VINYL_GE || o == VINYL_EQ) {
			ii->itr = bps_tree_svindex_itr_first(tree);
		} else {
			assert(o == VINYL_LT || o == VINYL_LE);
			ii->itr = bps_tree_svindex_itr_last(tree);
		}
		return 0;
	}

	struct tree_svindex_key tree_key;
	tree_key.data = key;
	tree_key.size = key_size;
	tree_key.lsn = (o == VINYL_GE || o == VINYL_EQ || o == VINYL_LT) ?
		       UINT64_MAX : 0;
	bool exact;
	ii->index->hint_key_is_equal = false;
	ii->itr = bps_tree_svindex_lower_bound(tree, &tree_key, &exact);
	if (o == VINYL_LE || o == VINYL_LT)
		bps_tree_svindex_itr_prev(tree, &ii->itr);
	else if(o == VINYL_EQ)
		if (!ii->index->hint_key_is_equal)
			ii->itr = bps_tree_svindex_invalid_iterator();
	return (int)ii->index->hint_key_is_equal;
}

static inline void
sv_indexiter_close(struct vy_iter *i)
{
	(void)i;
}

static inline int
sv_indexiter_has(struct vy_iter *i)
{
	struct svindexiter *ii = (struct svindexiter *)i->priv;
	return !bps_tree_svindex_itr_is_invalid(&ii->itr);
}

static inline void *
sv_indexiter_get(struct vy_iter *i)
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
sv_indexiter_next(struct vy_iter *i)
{
	struct svindexiter *ii = (struct svindexiter *)i->priv;
	assert(!bps_tree_svindex_itr_is_invalid(&ii->itr));

	if (ii->order == VINYL_EQ) {
		bps_tree_svindex_itr_next(&ii->index->tree, &ii->itr);
		if (bps_tree_svindex_itr_is_invalid(&ii->itr))
			return;
		struct svref *ref =
			bps_tree_svindex_itr_get_elem(&ii->index->tree,
						      &ii->itr);
		if (vy_tuple_compare(ref->v->data, ii->key,
				     ii->index->key_def) != 0) {
			ii->itr = bps_tree_svindex_invalid_iterator();
		}
	} else if (ii->order == VINYL_GT || ii->order == VINYL_GE) {
		bps_tree_svindex_itr_next(&ii->index->tree, &ii->itr);
	} else {
		assert(ii->order == VINYL_LT || ii->order == VINYL_LE);
		bps_tree_svindex_itr_prev(&ii->index->tree, &ii->itr);
	}
}

static struct vy_iterif sv_indexiterif =
{
	.close   = sv_indexiter_close,
	.has     = sv_indexiter_has,
	.get     = sv_indexiter_get,
	.next    = sv_indexiter_next
};

static uint8_t
svtuple_flags(struct sv *v) {
	return sv_to_tuple(v)->flags;
}

static uint64_t
svtuple_lsn(struct sv *v) {
	return sv_to_tuple(v)->lsn;
}

static char*
svtuple_pointer(struct sv *v) {
	return sv_to_tuple(v)->data;
}

static uint32_t
svtuple_size(struct sv *v) {
	return sv_to_tuple(v)->size;
}

static struct svif svtuple_if =
{
	.flags     = svtuple_flags,
	.lsn       = svtuple_lsn,
	.pointer   = svtuple_pointer,
	.size      = svtuple_size
};

struct sicache;

struct PACKED sdid {
	uint64_t parent;
	uint64_t id;
	uint8_t  flags;
};

struct PACKED vy_page_index_header {
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

struct PACKED vy_page_info {
	/* offset of page data in file (0 for first page) */
	uint64_t offset;
	/* size of page data in file */
	uint32_t size;
	/* size of page data in memory, i.e. unpacked */
	uint32_t unpacked_size;
	/* offset of page's min key in page index key storage (0 for first) */
	uint32_t min_key_offset;
	/* offset of page's max key in page index key storage */
	uint32_t max_key_offset;
	/* lsn of min key in page */
	uint64_t min_key_lsn;
	/* lsn of max key in page */
	uint64_t max_key_lsn;
	/* minimal lsn of all records in page */
	uint64_t min_lsn;
	/* maximal lsn of all records in page */
	uint64_t max_lsn;
};


struct vy_page_index {
	struct vy_page_index_header header;
	struct vy_buf pages, minmax;
};

struct PACKED vy_run {
	struct sdid id;
	struct vy_page_index index;
	struct vy_run *link;
	struct vy_run *next;
	struct sdpage *page_cache;
	pthread_mutex_t cache_lock;
};

struct PACKED vy_range {
	uint32_t   recover;
	uint16_t   flags;
	uint64_t   update_time;
	uint32_t   used; /* sum of i0->used + i1->used */
	uint64_t   ac;
	struct vy_run   self;
	struct vy_run  *run;
	uint32_t   run_count;
	uint32_t   temperature;
	uint64_t   temperature_reads;
	uint16_t   refs;
	pthread_mutex_t reflock;
	struct svindex    i0, i1;
	struct vy_file     file;
	rb_node(struct vy_range) tree_node;
	struct ssrqnode   nodecompact;
	struct ssrqnode   nodedump;
	struct rlist     gc;
	struct rlist     commit;
};


typedef rb_tree(struct vy_range) vy_range_tree_t;

struct vy_index_conf {
	uint32_t    id;
	char       *name;
	char       *path;
	uint32_t    sync;
	uint32_t    compression;
	char       *compression_sz;
	struct vy_filterif *compression_if;
	uint32_t    buf_gc_wm;
	struct srversion   version;
	struct srversion   version_storage;
};

struct vy_profiler {
	uint32_t  total_node_count;
	uint64_t  total_node_size;
	uint64_t  total_node_origin_size;
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
	uint64_t tsn;
	struct vy_index *index;
	struct vy_tuple *tuple;
	struct vy_tx *tx;
	/** Next in the transaction log. */
	struct stailq_entry next_in_log;
	/** Member of the transaction manager index. */
	rb_node(struct txv) in_txvindex;
	/** Member of the transaction log index. */
	rb_node(struct txv) in_logindex;
};

typedef rb_tree(struct txv) txvindex_t;

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
	txvindex_t txvindex;
	vy_range_tree_t tree;
	struct vy_status status;
	pthread_mutex_t lock;
	int range_count;
	uint64_t update_time;
	uint64_t read_disk;
	uint64_t read_cache;
	uint64_t size;
	pthread_mutex_t ref_lock;
	uint32_t refs;
	struct vy_buf readbuf;
	struct vy_index_conf conf;
	struct key_def *key_def;
	struct tuple_format *tuple_format;
	uint32_t key_map_size; /* size of key_map map */
	uint32_t *key_map; /* field_id -> part_id map */
	/** Member of env->db or scheduler->shutdown. */
	struct rlist link;

	/* {{{ Scheduler members */
	struct rlist gc;
	struct vy_planner p;
	bool checkpoint_in_progress;
	bool age_in_progress;
	bool gc_in_progress;
	/* Scheduler members }}} */
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


struct txvindex_key {
	char *data;
	int size;
	uint64_t tsn;
};

typedef rb_tree(struct txv) logindex_t;

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
	logindex_t logindex;
	uint64_t start;
	enum tx_type     type;
	enum tx_state    state;
	bool is_aborted;
	/** Transaction logical start time. */
	uint64_t tsn;
	/**
	 * Consistent read view LSN: the LSN recorded
	 * at start of transaction and used to implement
	 * transactional read view.
	 */
	uint64_t   vlsn;
	rb_node(struct vy_tx) tree_node;
	struct tx_manager *manager;
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
txv_delete(struct vy_stat *stat, struct txv *v)
{
	vy_tuple_unref_rt(stat, v->tuple);
	free(v);
}

static inline void
txv_abort(struct txv *v)
{
	v->tx->is_aborted = true;
}

static int
txvindex_cmp(txvindex_t *rbtree, struct txv *a, struct txv *b);

static int
txvindex_key_cmp(txvindex_t *rbtree, struct txvindex_key *a, struct txv *b);

rb_gen_ext_key(, txvindex_, txvindex_t, struct txv, in_txvindex, txvindex_cmp,
		 struct txvindex_key *, txvindex_key_cmp);

static struct txv *
txvindex_search_key(txvindex_t *rbtree, char *data, int size, uint64_t tsn)
{
	struct txvindex_key key;
	key.data = data;
	key.size = size;
	key.tsn = tsn;
	return txvindex_search(rbtree, &key);
}

static int
txvindex_cmp(txvindex_t *rbtree, struct txv *a, struct txv *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vy_index, txvindex)->key_def;
	int rc = vy_tuple_compare(a->tuple->data, b->tuple->data, key_def);
	/**
	 * While in svindex older value are "bigger" than newer
	 * ones, i.e. the newest value comes first, in
	 * transactional index (txvindex), we want to look
	 * at data in chronological order.
	 * @sa tree_svindex_compare
	 */
	if (rc == 0)
		rc = a->tsn < b->tsn ? -1 : a->tsn > b->tsn;
	return rc;
}

static int
txvindex_key_cmp(txvindex_t *rbtree, struct txvindex_key *a, struct txv *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vy_index, txvindex)->key_def;
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
	txvindex_t *tree = &v->index->txvindex;
	struct key_def *key_def = v->index->key_def;
	struct txvindex_key key;
	key.data = v->tuple->data;
	key.size = v->tuple->size;
	key.tsn = 0;
	/** Find the first value equal to or greater than key */
	struct txv *abort = txvindex_nsearch(tree, &key);
	while (abort) {
		/* Check if we're still looking at the matching key. */
		if (vy_tuple_compare(key.data, abort->tuple->data,
				     key_def))
			break;
		/* Don't abort self. */
		if (abort->tx != tx)
			txv_abort(abort);
		abort = txvindex_next(tree, abort);
	}
}

static int
logindex_cmp(logindex_t *index, struct txv *a, struct txv *b)
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

struct logindex_key {
	struct vy_index *index;
	char *data;
};

static int
logindex_key_cmp(logindex_t *index, struct logindex_key *a, struct txv *b)
{
	(void) index;
	/* Order by index first, by key in the index second. */
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0) {
		struct key_def *key_def = a->index->key_def;
		rc = vy_tuple_compare(a->data, b->tuple->data, key_def);
	}
	return rc;
}

rb_gen_ext_key(, logindex_, logindex_t, struct txv, in_logindex, logindex_cmp,
	       struct logindex_key *, logindex_key_cmp);

static struct txv *
logindex_search_key(logindex_t *tree, struct vy_index *index, char *data)
{
	struct logindex_key key = { .index = index, .data = data};
	return logindex_search(tree, &key);
}

bool
vy_tx_is_ro(struct vy_tx *tx)
{
	return tx->type == VINYL_TX_RO ||
		tx->logindex.rbt_root == &tx->logindex.rbt_nil;
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
	uint64_t tsn;
	struct vy_env *env;
};

static struct tx_manager *
tx_manager_new(struct vy_env*);

static int
tx_manager_delete(struct tx_manager*);

static void
tx_begin(struct tx_manager*, struct vy_tx*, enum tx_type);

static enum tx_state
tx_rollback(struct vy_tx*);

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
txvindex_delete_cb(txvindex_t *t, struct txv *v, void *arg)
{
	(void) t;
	txv_delete(arg, v);
	return NULL;
}

static inline enum tx_state
tx_promote(struct vy_tx *tx, enum tx_state state)
{
	tx->state = state;
	return state;
}

static void
tx_begin(struct tx_manager *m, struct vy_tx *tx, enum tx_type type)
{
	stailq_create(&tx->log);
	logindex_new(&tx->logindex);
	tx->start = clock_monotonic64();
	tx->manager = m;
	tx->state = VINYL_TX_READY;
	tx->type = type;
	tx->is_aborted = false;

	tx->tsn = ++m->tsn;
	tx->vlsn = vy_sequence(m->env->seq, VINYL_LSN);

	tx_tree_insert(&m->tree, tx);
	if (type == VINYL_TX_RO)
		m->count_rd++;
	else
		m->count_rw++;
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
		vy_sequence_lock(m->env->seq);
		m->env->seq->vlsn = oldest ? oldest->vlsn :
			m->env->seq->lsn;
		vy_sequence_unlock(m->env->seq);
	}
}

static inline void
tx_rollback_svp(struct vy_tx *tx, struct txv *v)
{
	struct stailq tail;
	stailq_create(&tail);
	stailq_splice(&tx->log, &v->next_in_log, &tail);
	struct vy_stat *stat = tx->manager->env->stat;
	struct txv *tmp;
	stailq_foreach_entry_safe(v, tmp, &tail, next_in_log) {
		/* Remove from the conflict manager index */
		if (vy_tuple_is_r(v->tuple))
			txvindex_remove(&v->index->txvindex, v);
		if (vy_tuple_is_w(v->tuple))
			logindex_remove(&tx->logindex, v);
		txv_delete(stat, v);
	}
}

static enum tx_state
tx_rollback(struct vy_tx *tx)
{
	/* support log free after commit and half-commit mode */
	tx_rollback_svp(tx, stailq_first_entry(&tx->log, struct txv,
					       next_in_log));
	tx_promote(tx, VINYL_TX_ROLLBACK);
	tx_manager_end(tx->manager, tx);
	return VINYL_TX_ROLLBACK;
}

static struct vy_tuple *
vy_tx_get(struct vy_tx *tx, struct vy_index *index, struct vy_tuple *key)
{
	struct txv *v = logindex_search_key(&tx->logindex, index, key->data);
	if (v) {
		/* Do not track separately our own reads after writes. */
		vy_tuple_ref(v->tuple);
		return v->tuple;
	}
	v = txvindex_search_key(&index->txvindex, key->data,
				key->size, tx->tsn);
	if (v == NULL) {
		key->flags = SVGET;
		/* Allocate a MVCC container. */
		v = txv_new(index, key, tx);
		stailq_add_tail_entry(&tx->log, v, next_in_log);
		txvindex_insert(&index->txvindex, v);
	}
	return 0;
}

struct PACKED sdv {
	/* TODO: reorder: uint64_t, uint32_t, uint32_t, uint8_t */
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
	uint32_t refs;
};

static inline void
sd_pageinit(struct sdpage *p, char *data) {
	p->h = (struct sdpageheader *)data;
	p->refs = 1;
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

struct sdpageiter {
	struct sdpage *page;
	struct vy_buf *xfbuf;
	int64_t pos;
	struct sdv *v;
	struct sv current;
	enum vy_order order;
	void *key;
	int keysize;
	struct key_def *key_def;
};

static inline char *
vy_page_index_min_key(struct vy_page_index *i, struct vy_page_info *p) {
	return i->minmax.s + p->min_key_offset;
}

static inline char *
vy_page_index_max_key(struct vy_page_index *i, struct vy_page_info *p) {
	return i->minmax.s + p->max_key_offset;
}

static inline void
vy_page_index_init(struct vy_page_index *i) {
	vy_buf_init(&i->pages);
	vy_buf_init(&i->minmax);
	memset(&i->header, 0, sizeof(i->header));
}

static inline void
vy_page_index_free(struct vy_page_index *i) {
	vy_buf_free(&i->pages);
	vy_buf_free(&i->minmax);
}

static inline struct vy_page_info *
vy_page_index_get_page(struct vy_page_index *i, int pos)
{
	assert(pos >= 0);
	assert((uint32_t)pos < i->header.count);
	return (struct vy_page_info *)
		vy_buf_at(&i->pages, sizeof(struct vy_page_info), pos);
}

static inline struct vy_page_info *
vy_page_index_first_page(struct vy_page_index *i) {
	return vy_page_index_get_page(i, 0);
}

static inline struct vy_page_info *
vy_page_index_last_page(struct vy_page_index *i) {
	return vy_page_index_get_page(i, i->header.count - 1);
}

static inline uint32_t
vy_page_index_count(struct vy_page_index *i)
{
	if (unlikely(i->pages.s == NULL))
		return 0;
	return i->header.keys;
}

static inline uint32_t
vy_page_index_total(struct vy_page_index *i)
{
	if (unlikely(i->pages.s == NULL))
		return 0;
	return i->header.total;
}

static inline uint32_t
vy_page_index_size(struct vy_page_index *i)
{
	return sizeof(i->header) + i->header.size + i->header.extension;
}

static int vy_page_index_load(struct vy_page_index *, void *);

struct vy_page_iter {
	struct vy_page_index *index;
	struct key_def *key_def;
	int cur_pos;
	struct vy_page_info *cur_page;
	void *key;
	enum vy_order order;
};

static inline int
sd_indexiter_route(struct vy_page_iter *i)
{
	int begin = 0;
	int end = i->index->header.count - 1;
	while (begin != end) {
		int mid = begin + (end - begin) / 2;
		struct vy_page_info *page = vy_page_index_get_page(i->index, mid);
		int rc = vy_tuple_compare(vy_page_index_max_key(i->index, page),
					  i->key, i->key_def);
		if (rc < 0)
			begin = mid + 1;
		else
			end = mid;
	}
	if (unlikely(end >= (int)i->index->header.count))
		end = i->index->header.count - 1;
	return end;
}

static inline int
sd_indexiter_open(struct vy_page_iter *itr, struct key_def *key_def,
		  struct vy_page_index *index, enum vy_order order, void *key)
{
	itr->key_def = key_def;
	itr->index = index;
	itr->order = order;
	itr->key = key;
	itr->cur_pos = 0;
	itr->cur_page = NULL;
	if (unlikely(itr->index->header.count == 1)) {
		/* skip bootstrap node  */
		if (itr->index->header.lsnmin == UINT64_MAX &&
		    itr->index->header.lsnmax == 0)
			return 0;
	}
	if (itr->key == NULL) {
		/* itr->key is [] */
		switch (itr->order) {
		case VINYL_LT:
		case VINYL_LE: itr->cur_pos = itr->index->header.count - 1;
			break;
		case VINYL_GT:
		case VINYL_GE: itr->cur_pos = 0;
			break;
		default:
			unreachable();
		}
		itr->cur_page = vy_page_index_get_page(itr->index, itr->cur_pos);
		return 0;
	}
	if (likely(itr->index->header.count > 1))
		itr->cur_pos = sd_indexiter_route(itr);

	struct vy_page_info *p = vy_page_index_get_page(itr->index, itr->cur_pos);
	int rc;
	switch (itr->order) {
	case VINYL_LE:
	case VINYL_LT:
		rc = vy_tuple_compare(vy_page_index_min_key(itr->index, p),
				      itr->key, itr->key_def);
		if (rc ==  1 || (rc == 0 && itr->order == VINYL_LT))
			itr->cur_pos--;
		break;
	case VINYL_GE:
	case VINYL_GT:
		rc = vy_tuple_compare(vy_page_index_max_key(itr->index, p),
				      itr->key, itr->key_def);
		if (rc == -1 || (rc == 0 && itr->order == VINYL_GT))
			itr->cur_pos++;
		break;
	default: unreachable();
	}
	if (unlikely(itr->cur_pos == -1 ||
	               itr->cur_pos >= (int)itr->index->header.count))
		return 0;
	itr->cur_page = vy_page_index_get_page(itr->index, itr->cur_pos);
	return 0;
}

static inline struct vy_page_info *
sd_indexiter_get(struct vy_page_iter *ii)
{
	return ii->cur_page;
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
	s->crc = vy_crcs(s, sizeof(struct sdseal), 0);
}

static inline void
sd_sealset_close(struct sdseal *s, struct vy_page_index_header *h)
{
	sr_version_storage(&s->version);
	s->flags = SD_SEALED;
	s->index_crc = h->crc;
	s->index_offset = h->offset;
	s->crc = vy_crcs(s, sizeof(struct sdseal), 0);
}

static inline int
sd_sealvalidate(struct sdseal *s, struct vy_page_index_header *h)
{
	uint32_t crc = vy_crcs(s, sizeof(struct sdseal), 0);
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
	struct vy_buf a; /* decompression */
	struct vy_buf b; /* transformation */
	struct vy_page_iter index_iter;
	struct sdpageiter page_iter;
	struct sdcbuf *next;
};

struct sdc {
	struct vy_buf a;        /* result */
	struct vy_buf b;        /* redistribute buffer */
	struct vy_buf c;        /* file buffer */
	struct vy_buf d;        /* page read buffer */
	struct sdcbuf *head;   /* compression buffer list */
	int count;
};

static inline void
sd_cinit(struct sdc *sc)
{
	vy_buf_init(&sc->a);
	vy_buf_init(&sc->b);
	vy_buf_init(&sc->c);
	vy_buf_init(&sc->d);
	sc->count = 0;
	sc->head = NULL;
}

static inline void
sd_cfree(struct sdc *sc)
{
	vy_buf_free(&sc->a);
	vy_buf_free(&sc->b);
	vy_buf_free(&sc->c);
	vy_buf_free(&sc->d);
	struct sdcbuf *b = sc->head;
	struct sdcbuf *next;
	while (b) {
		next = b->next;
		vy_buf_free(&b->a);
		vy_buf_free(&b->b);
		free(b);
		b = next;
	}
}

static inline void
sd_cgc(struct sdc *sc, int wm)
{
	vy_buf_gc(&sc->a, wm);
	vy_buf_gc(&sc->b, wm);
	vy_buf_gc(&sc->c, wm);
	vy_buf_gc(&sc->d, wm);
	struct sdcbuf *it = sc->head;
	while (it) {
		vy_buf_gc(&it->a, wm);
		vy_buf_gc(&it->b, wm);
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
			if (buf == NULL) {
				diag_set(OutOfMemory, sizeof(struct sdcbuf),
					 "malloc", "struct sdcbuf");
				return -1;
			}
			vy_buf_init(&buf->a);
			vy_buf_init(&buf->b);
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
	struct vy_filterif *compression_if;
	uint64_t    vlsn;
	uint32_t    save_delete;
	uint32_t    save_upsert;
};

struct sdmerge {
	struct vy_page_index     index;
	struct svmergeiter *merge;
	struct svwriteiter i;
	struct sdmergeconf *conf;
	struct sdbuild     *build;
	uint64_t    processed;
	uint64_t    current;
	uint64_t    limit;
	int         resume;
};

struct sdreadarg {
	struct vy_page_index    *index;
	struct vy_buf      *buf;
	struct vy_buf      *buf_xf;
	struct vy_buf      *buf_read;
	struct vy_page_iter *index_iter;
	struct sdpageiter *page_iter;
	struct vy_file     *file;
	enum vy_order     o;
	int         has;
	uint64_t    has_vlsn;
	int         use_compression;
	struct vy_filterif *compression_if;
	struct key_def *key_def;
};

struct sdread {
	struct sdreadarg ra;
	struct vy_page_info *ref;
	struct sdpage page;
	int reads;
};

static int
vy_page_index_load(struct vy_page_index *i, void *ptr)
{
	struct vy_page_index_header *h = (struct vy_page_index_header *)ptr;
	uint32_t index_size = h->count * sizeof(struct vy_page_info);
	int rc = vy_buf_ensure(&i->pages, index_size);
	if (unlikely(rc == -1))
		return -1;
	memcpy(i->pages.s, (char *)ptr + sizeof(struct vy_page_index_header),
	       index_size);
	vy_buf_advance(&i->pages, index_size);
	uint32_t minmax_size = h->size - index_size;
	rc = vy_buf_ensure(&i->minmax, minmax_size);
	if (unlikely(rc == -1))
		return -1;
	memcpy(i->minmax.s,
	       (char *)ptr + sizeof(struct vy_page_index_header) + index_size,
	       minmax_size);
	vy_buf_advance(&i->minmax, minmax_size);
	i->header = *h;
	return 0;
}

struct sdrecover {
	struct vy_file *file;
	int corrupt;
	struct vy_page_index_header *v;
	struct vy_page_index_header *actual;
	struct sdseal *seal;
	struct vy_mmap map;
	struct vy_env *env;
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
		vy_error("corrupted index file '%s': bad seal size",
		               i->file->path);
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}
	pointer = i->map.p + next->index_offset;

	/* validate index pointer */
	if (unlikely(((pointer + sizeof(struct vy_page_index_header)) > eof))) {
		vy_error("corrupted index file '%s': bad index size",
		               i->file->path);
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}
	struct vy_page_index_header *index = (struct vy_page_index_header*)(pointer);

	/* validate index crc */
	uint32_t crc = vy_crcs(index, sizeof(struct vy_page_index_header), 0);
	if (index->crc != crc) {
		vy_error("corrupted index file '%s': bad index crc",
		               i->file->path);
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}

	/* validate index size */
	char *end = pointer + sizeof(struct vy_page_index_header)
		    + index->size + index->extension;
	if (unlikely(end > eof)) {
		vy_error("corrupted index file '%s': bad index size",
		               i->file->path);
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}

	/* validate seal */
	int rc = sd_sealvalidate(next, index);
	if (unlikely(rc == -1)) {
		vy_error("corrupted index file '%s': bad seal",
		               i->file->path);
		i->corrupt = 1;
		i->v = NULL;
		return -1;
	}
	i->seal = next;
	i->actual = index;
	i->v = index;
	return 1;
}

static int
sd_recover_open(struct sdrecover *ri, struct vy_env *env,
		struct vy_file *file)
{
	memset(ri, 0, sizeof(*ri));
	ri->env = env;
	ri->file = file;
	if (unlikely(ri->file->size < (sizeof(struct sdseal) + sizeof(struct vy_page_index_header)))) {
		vy_error("corrupted index file '%s': bad size",
		               ri->file->path);
		ri->corrupt = 1;
		return -1;
	}
	int rc = vy_mmap_map(&ri->map, ri->file->fd, ri->file->size, 1);
	if (unlikely(rc == -1)) {
		vy_error("failed to mmap index file '%s': %s",
		               ri->file->path,
		               strerror(errno));
		return -1;
	}
	struct sdseal *seal = (struct sdseal*)((char*)ri->map.p);
	rc = sd_recover_next_of(ri, seal);
	if (unlikely(rc == -1))
		vy_mmap_unmap(&ri->map);
	return rc;
}

static void
sd_recover_close(struct sdrecover *ri)
{
	vy_mmap_unmap(&ri->map);
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
	struct sdseal *next = (struct sdseal *)
		((char *) ri->v + sizeof(struct vy_page_index_header) +
		 ri->v->size + ri->v->extension);
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
	char *eof = ri->map.p +
		    ri->actual->offset + sizeof(struct vy_page_index_header) +
		    ri->actual->size - ri->actual->extension;
	uint64_t file_size = eof - ri->map.p;
	int rc = vy_file_resize(ri->file, file_size);
	if (unlikely(rc == -1))
		return -1;
	diag_clear(diag_get());
	return 0;
}

static void vy_index_conf_init(struct vy_index_conf*);
static void vy_index_conf_free(struct vy_index_conf*);

static inline void
vy_run_init(struct vy_run *run)
{
	memset(&run->id, 0, sizeof(run->id));
	vy_page_index_init(&run->index);
	run->link = NULL;
	run->next = NULL;
	run->page_cache = NULL;
	pthread_mutex_init(&run->cache_lock, NULL);
}

static inline struct vy_run *
vy_run_new()
{
	struct vy_run *run = (struct vy_run *)malloc(sizeof(struct vy_run));
	if (unlikely(run == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_run), "malloc",
			 "struct vy_run");
		return NULL;
	}
	vy_run_init(run);
	return run;
}

static inline void
vy_run_set(struct vy_run *run, struct vy_page_index *i)
{
	run->id = i->header.id;
	run->index = *i;
}

static inline void
vy_run_free(struct vy_run *run)
{
	vy_page_index_free(&run->index);
	if (run->page_cache != NULL) {
		free(run->page_cache);
		run->page_cache = NULL;
	}
	pthread_mutex_destroy(&run->cache_lock);
	free(run);
}

/**
 * Load from page with given number
 * If the page is loaded by somebody else, it's returned from cache
 * In every case increments page's reference counter
 * After usage user must call vy_run_unload_page
 */
static struct sdpage *
vy_run_load_page(struct vy_run *run, uint32_t pos,
		 struct vy_file *file, struct vy_filterif *compression)
{
	pthread_mutex_lock(&run->cache_lock);
	if (run->page_cache == NULL) {
		run->page_cache = calloc(run->index.header.count,
					 sizeof(*run->page_cache));
		if (run->page_cache == NULL) {
			pthread_mutex_unlock(&run->cache_lock);
			diag_set(OutOfMemory,
				 run->index.header.count * sizeof (*run->page_cache),
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
	struct vy_page_info *page_info = vy_page_index_get_page(&run->index, pos);
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
	int rc = vy_file_pread(file, page_info->offset, data, page_info->size);
#endif

	if (rc < 0) {
		free(data);
		vy_error("index file '%s' read error: %s",
			 file->path, strerror(errno));
		return NULL;
	}

	if (compression != NULL) {
		/* decompression */
		struct vy_filter f;
		rc = vy_filter_init(&f, compression, VINYL_FOUTPUT);
		if (unlikely(rc == -1)) {
			vy_error("index file '%s' decompression error",
				 file->path);
			free(data);
			return NULL;
		}
		struct vy_buf buf;
		vy_buf_init(&buf);
		rc = vy_filter_next(&f, &buf, data + sizeof(struct sdpageheader),
				    page_info->size - sizeof(struct sdpageheader));
		vy_filter_free(&f);
		if (unlikely(rc == -1)) {
			vy_error("index file '%s' decompression error",
				 file->path);
			vy_buf_free(&buf);
			free(data);
			return NULL;
		}
		assert(vy_buf_size(&buf) == page_info->unpacked_size -
		       sizeof(struct sdpageheader));
		memcpy(data + sizeof(struct sdpageheader), buf.s,
		       page_info->unpacked_size - sizeof(struct sdpageheader));
		vy_buf_free(&buf);
	}

	pthread_mutex_lock(&run->cache_lock);
	run->page_cache[pos].refs++;
	if (run->page_cache[pos].refs == 1)
		sd_pageinit(&run->page_cache[pos], data);
	else
		free(data);
	pthread_mutex_unlock(&run->cache_lock);
	return &run->page_cache[pos];
}

/**
 * Get a page from cache
 * Page must be loaded with vy_run_load_page before the call
 */
static struct sdpage *
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
		free(run->page_cache[pos].h);
		run->page_cache[pos].h = NULL;
	}
	pthread_mutex_unlock(&run->cache_lock);
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

static struct vy_range *vy_range_new(struct key_def *key_def);
static int
vy_range_open(struct vy_range*, struct vy_env*, char *);
static int
vy_range_create(struct vy_range*, struct vy_index_conf*, struct sdid*);
static int vy_range_free(struct vy_range*, struct vy_env*, int);
static int vy_range_gc_index(struct vy_env*, struct svindex*);
static int vy_range_seal(struct vy_range*, struct vy_index_conf*);
static int vy_range_complete(struct vy_range*, struct vy_index_conf*);

static inline void
vy_range_lock(struct vy_range *node) {
	assert(! (node->flags & SI_LOCK));
	node->flags |= SI_LOCK;
}

static inline void
vy_range_unlock(struct vy_range *node) {
	assert((node->flags & SI_LOCK) > 0);
	node->flags &= ~SI_LOCK;
}

static inline void
vy_range_split(struct vy_range *node) {
	node->flags |= SI_SPLIT;
}

static inline struct svindex*
vy_range_rotate(struct vy_range *node) {
	node->flags |= SI_ROTATE;
	return &node->i0;
}

static inline void
vy_range_unrotate(struct vy_range *node) {
	assert((node->flags & SI_ROTATE) > 0);
	node->flags &= ~SI_ROTATE;
	node->i0 = node->i1;
	node->i0.tree.arg = &node->i0;
	sv_indexinit(&node->i1, node->i0.key_def);
}

static inline struct svindex*
vy_range_index(struct vy_range *node) {
	if (node->flags & SI_ROTATE)
		return &node->i1;
	return &node->i0;
}

static inline struct svindex*
vy_range_index_priority(struct vy_range *node, struct svindex **second)
{
	if (unlikely(node->flags & SI_ROTATE)) {
		*second = &node->i0;
		return &node->i1;
	}
	*second = NULL;
	return &node->i0;
}

static inline int
vy_range_cmp(struct vy_range *n, void *key, struct key_def *key_def)
{
	struct vy_page_info *min = vy_page_index_first_page(&n->self.index);
	struct vy_page_info *max = vy_page_index_last_page(&n->self.index);
	int l = vy_tuple_compare(vy_page_index_min_key(&n->self.index, min),
				 key, key_def);
	int r = vy_tuple_compare(vy_page_index_max_key(&n->self.index, max),
				 key, key_def);
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
vy_range_cmpnode(struct vy_range *n1, struct vy_range *n2, struct key_def *key_def)
{
	if (n1 == n2)
		return 0;
	struct vy_page_info *min1 = vy_page_index_first_page(&n1->self.index);
	struct vy_page_info *min2 = vy_page_index_first_page(&n2->self.index);
	return vy_tuple_compare(vy_page_index_min_key(&n1->self.index, min1),
				vy_page_index_min_key(&n2->self.index, min2),
				key_def);
}

static inline uint64_t
vy_range_size(struct vy_range *n)
{
	uint64_t size = 0;
	struct vy_run *run = n->run;
	while (run) {
		size += vy_page_index_size(&run->index) +
		        vy_page_index_total(&run->index);
		run = run->next;
	}
	return size;
}

enum vy_task_type {
	VY_TASK_UNKNOWN = 0,
	VY_TASK_DUMP,
	VY_TASK_AGE,
	VY_TASK_COMPACT,
	VY_TASK_CHECKPOINT,
	VY_TASK_GC,
	VY_TASK_DROP,
	VY_TASK_NODEGC
};

struct vy_task {
	enum vy_task_type type;
	struct vy_index *index;
	struct vy_range *node;
};

static int vy_planner_init(struct vy_planner*);
static int vy_planner_free(struct vy_planner*);
static int vy_planner_update(struct vy_planner*, struct vy_range*);
static int vy_planner_update_range(struct vy_planner *p, struct vy_range *n);
static int vy_planner_remove(struct vy_planner*, struct vy_range*);

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
vy_range_tree_free_cb(vy_range_tree_t *t, struct vy_range * n, void *arg)
{
	(void)t;
	struct vy_env *env = (struct vy_env *)arg;
	vy_range_free(n, env, 0);
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
vy_index_lock(struct vy_index *i) {
	tt_pthread_mutex_lock(&i->lock);
}

static inline void
vy_index_unlock(struct vy_index *i) {
	tt_pthread_mutex_unlock(&i->lock);
}

static inline void
vy_index_delete(struct vy_index *index);

static struct vy_range *si_bootstrap(struct vy_index*, uint64_t);

struct sicacherun {
	struct vy_run *run;
	struct vy_page_info *ref;
	struct sdpage page;
	struct vy_iter i;
	struct sdpageiter page_iter;
	struct vy_page_iter index_iter;
	struct vy_buf buf_a;
	struct vy_buf buf_b;
	int open;
	struct sicacherun *next;
};

struct sicache {
	struct sicacherun *path;
	struct sicacherun *run;
	uint32_t count;
	uint64_t nsn;
	struct vy_range *node;
	struct sicache *next;
	struct vy_cachepool *pool;
};

struct vy_cachepool {
	struct sicache *head;
	int n;
	struct vy_env *env;
	pthread_mutex_t mutex;
};

static inline void
si_cacheinit(struct sicache *c, struct vy_cachepool *pool)
{
	c->path   = NULL;
	c->run = NULL;
	c->count  = 0;
	c->node   = NULL;
	c->nsn    = 0;
	c->next   = NULL;
	c->pool   = pool;
}

static inline void
si_cachefree(struct sicache *c)
{
	struct sicacherun *next;
	struct sicacherun *cb = c->path;
	while (cb) {
		next = cb->next;
		vy_buf_free(&cb->buf_a);
		vy_buf_free(&cb->buf_b);
		free(cb);
		cb = next;
	}
}

static inline void
si_cachereset(struct sicache *c)
{
	struct sicacherun *cb = c->path;
	while (cb) {
		vy_buf_reset(&cb->buf_a);
		vy_buf_reset(&cb->buf_b);
		cb->run = NULL;
		cb->ref = NULL;
		cb->open = 0;
		cb = cb->next;
	}
	c->run = NULL;
	c->node   = NULL;
	c->nsn    = 0;
	c->count  = 0;
}

static inline struct sicacherun*
si_cacheadd(struct vy_run *run)
{
	struct sicacherun *nb =
		malloc(sizeof(struct sicacherun));
	if (unlikely(nb == NULL)) {
		diag_set(OutOfMemory, sizeof(struct sicacherun),
			 "malloc", "struct sicacherun");
		return NULL;
	}
	nb->run = run;
	nb->ref = NULL;
	memset(&nb->i, 0, sizeof(nb->i));
	nb->open = 0;
	nb->next = NULL;
	vy_buf_init(&nb->buf_a);
	vy_buf_init(&nb->buf_b);
	return nb;
}

static inline int
si_cachevalidate(struct sicache *c, struct vy_range *n)
{
	if (likely(c->node == n && c->nsn == n->self.id.id))
	{
		if (likely(n->run_count == c->count)) {
			c->run = c->path;
			return 0;
		}
		assert(n->run_count > c->count);
		/* c b a */
		/* e d c b a */
		struct sicacherun *head = NULL;
		struct sicacherun *last = NULL;
		struct sicacherun *cb = c->path;
		struct vy_run *run = n->run;
		while (run != NULL) {
			if (cb->run == run) {
				assert(last != NULL);
				last->next = cb;
				break;
			}
			struct sicacherun *nb = si_cacheadd(run);
			if (unlikely(nb == NULL))
				return -1;
			if (! head)
				head = nb;
			if (last)
				last->next = nb;
			last = nb;
			run = run->next;
		}
		c->path = head;
		c->count = n->run_count;
		c->run = c->path;
		return 0;
	}
	struct sicacherun *last = c->path;
	struct sicacherun *cb = last;
	struct vy_run *run = n->run;
	while (cb != NULL && run != NULL) {
		cb->run = run;
		cb->ref = NULL;
		cb->open = 0;
		vy_buf_reset(&cb->buf_a);
		vy_buf_reset(&cb->buf_b);
		last = cb;
		cb = cb->next;
		run = run->next;
	}
	while (cb != NULL) {
		cb->run = NULL;
		cb->ref = NULL;
		cb->open = 0;
		vy_buf_reset(&cb->buf_a);
		vy_buf_reset(&cb->buf_b);
		cb = cb->next;
	}
	while (run != NULL) {
		cb = si_cacheadd(run);
		if (unlikely(cb == NULL))
			return -1;
		if (last)
			last->next = cb;
		last = cb;
		if (c->path == NULL)
			c->path = cb;
		run = run->next;
	}
	c->count  = n->run_count;
	c->node   = n;
	c->nsn    = n->self.id.id;
	c->run = c->path;
	return 0;
}

static inline struct vy_cachepool *
vy_cachepool_new(struct vy_env *env)
{
	struct vy_cachepool *cachepool = malloc(sizeof(*cachepool));
	if (cachepool == NULL) {
		diag_set(OutOfMemory, sizeof(*cachepool), "cachepool",
			 "struct");
		return NULL;
	}
	cachepool->head = NULL;
	cachepool->n = 0;
	cachepool->env = env;
	tt_pthread_mutex_init(&cachepool->mutex, NULL);
	return cachepool;
}

static inline void
vy_cachepool_delete(struct vy_cachepool *p)
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
	free(p);
}

static inline struct sicache*
vy_cachepool_pop(struct vy_cachepool *p)
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
		if (unlikely(c == NULL)) {
			diag_set(OutOfMemory, sizeof(*c), "sicache",
				 "struct");
			return NULL;
		}
		si_cacheinit(c, p);
	}
	tt_pthread_mutex_unlock(&p->mutex);
	return c;
}

static inline void
vy_cachepool_push(struct sicache *c)
{
	struct vy_cachepool *p = c->pool;
	tt_pthread_mutex_lock(&p->mutex);
	c->next = p->head;
	p->head = c;
	p->n++;
	tt_pthread_mutex_unlock(&p->mutex);
}

struct siread {
	enum vy_order order;
	void *key;
	uint32_t keysize;
	int has;
	uint64_t vlsn;
	struct svmerge merge;
	int cache_only;
	int read_disk;
	int read_cache;
	struct sv *upsert_v;
	int upsert_eq;
	struct vy_tuple *result;
	struct sicache *cache;
	struct vy_index *index;
};

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
	itr->cur_range = vy_range_tree_search(&index->tree, &tree_key);
	if (itr->cur_range == NULL)
		itr->cur_range = vy_range_tree_psearch(&index->tree, &tree_key);
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

static int vy_task_index_drop(struct vy_index*);
static int vy_index_dropmark(struct vy_index*);
static int vy_index_droprepository(char*, int);

static int
si_merge(struct vy_index*, struct sdc*, struct vy_range*, uint64_t,
	 struct svmergeiter*, uint64_t, uint32_t);

static int vy_dump(struct vy_index*, struct sdc*, struct vy_range*, uint64_t);
static int
si_compact(struct vy_index*, struct sdc*, struct vy_range*, uint64_t,
	   struct vy_iter*, uint64_t);

typedef rb_tree(struct vy_range) vy_range_id_tree_t;

static int
vy_range_id_tree_cmp(vy_range_id_tree_t *rbtree, struct vy_range *a, struct vy_range *b)
{
	(void)rbtree;
	return vy_cmp(a->self.id.id, b->self.id.id);
}

static int
vy_range_id_tree_key_cmp(vy_range_id_tree_t *rbtree, const char *a, struct vy_range *b)
{
	(void)rbtree;
	return vy_cmp(load_u64(a), b->self.id.id);
}

rb_gen_ext_key(, vy_range_id_tree_, vy_range_id_tree_t, struct vy_range,
		 tree_node, vy_range_id_tree_cmp, const char *,
		 vy_range_id_tree_key_cmp);

struct vy_range *
vy_range_id_tree_free_cb(vy_range_id_tree_t *t, struct vy_range * n, void *arg)
{
	(void)t;
	struct vy_env *env = (struct vy_env *)arg;
	vy_range_free(n, env, 0);
	return NULL;
}

struct sitrack {
	vy_range_id_tree_t tree;
	int count;
	uint64_t nsn;
	uint64_t lsn;
};

static inline void
si_trackinit(struct sitrack *t)
{
	vy_range_id_tree_new(&t->tree);
	t->count = 0;
	t->nsn = 0;
	t->lsn = 0;
}

static inline void
si_trackfree(struct sitrack *t, struct vy_env *env)
{
	vy_range_id_tree_iter(&t->tree, NULL, vy_range_id_tree_free_cb, env);
#ifndef NDEBUG
	t->tree.rbt_root = (struct vy_range *)(intptr_t)0xDEADBEEF;
#endif
}

static inline void
si_trackmetrics(struct sitrack *t, struct vy_range *n)
{
	struct vy_run *run = n->run;
	while (run != NULL) {
		struct vy_page_index_header *h = &run->index.header;
		if (run->id.parent > t->nsn)
			t->nsn = run->id.parent;
		if (run->id.id > t->nsn)
			t->nsn = run->id.id;
		if (h->lsnmin != UINT64_MAX && h->lsnmin > t->lsn)
			t->lsn = h->lsnmin;
		if (h->lsnmax > t->lsn)
			t->lsn = h->lsnmax;
		run = run->next;
	}
}

static inline void
si_tracknsn(struct sitrack *t, uint64_t nsn)
{
	if (t->nsn < nsn)
		t->nsn = nsn;
}

static inline void
si_trackset(struct sitrack *t, struct vy_range *n)
{
	vy_range_id_tree_insert(&t->tree, n);
	t->count++;
}

static inline struct vy_range*
si_trackget(struct sitrack *t, uint64_t id)
{
	return vy_range_id_tree_search(&t->tree, (const char *)&id);
}

static inline void
si_trackreplace(struct sitrack *t, struct vy_range *o, struct vy_range *n)
{
	vy_range_id_tree_remove(&t->tree, o);
	vy_range_id_tree_insert(&t->tree, n);
}

static int
si_insert(struct vy_index *index, struct vy_range *range)
{
	vy_range_tree_insert(&index->tree, range);
	index->range_count++;
	return 0;
}

static int
si_remove(struct vy_index *index, struct vy_range *range)
{
	vy_range_tree_remove(&index->tree, range);
	index->range_count--;
	return 0;
}

static int
si_replace(struct vy_index *index, struct vy_range *o, struct vy_range *n)
{
	vy_range_tree_remove(&index->tree, o);
	vy_range_tree_insert(&index->tree, n);
	return 0;
}

static int
vy_task_execute(struct vy_task *task, struct sdc *c, uint64_t vlsn)
{
	assert(task->index != NULL);
	int rc = -1;
	switch (task->type) {
	case VY_TASK_NODEGC:
		rc = vy_range_free(task->node, task->index->env, 1);
		sd_cgc(c, task->index->conf.buf_gc_wm);
		return rc;
	case VY_TASK_CHECKPOINT:
	case VY_TASK_DUMP:
	case VY_TASK_AGE:
		rc = vy_dump(task->index, c, task->node, vlsn);
		sd_cgc(c, task->index->conf.buf_gc_wm);
		return rc;
	case VY_TASK_GC:
	case VY_TASK_COMPACT:
		rc = si_compact(task->index, c, task->node, vlsn, NULL, 0);
		sd_cgc(c, task->index->conf.buf_gc_wm);
		return rc;
	case VY_TASK_DROP:
		assert(task->index->refs == 1); /* referenced by this task */
		rc = vy_task_index_drop(task->index);
		/* TODO: return index to shutdown list in case of error */
		task->index = NULL;
		return rc;
	default:
		unreachable();
		return -1;
	}
}

/* dump tuple to the run page buffers (tuple header and data) */
static int
vy_run_dump_tuple(struct svwriteiter *iwrite, struct vy_buf *info_buf,
		  struct vy_buf *data_buf, struct sdpageheader *header)
{
	struct sv *value = sv_writeiter_get(iwrite);
	uint64_t lsn = sv_lsn(value);
	uint8_t flags = sv_flags(value);
	if (sv_writeiter_is_duplicate(iwrite))
		flags |= SVDUP;
	if (vy_buf_ensure(info_buf, sizeof(struct sdv)))
		return -1;
	struct sdv *tupleinfo = (struct sdv *)info_buf->p;
	tupleinfo->flags = flags;
	tupleinfo->offset = vy_buf_used(data_buf);
	tupleinfo->size = sv_size(value);
	tupleinfo->lsn = lsn;
	vy_buf_advance(info_buf, sizeof(struct sdv));

	if (vy_buf_ensure(data_buf, sv_size(value)))
		return -1;
	memcpy(data_buf->p, sv_pointer(value), sv_size(value));
	vy_buf_advance(data_buf, sv_size(value));

	++header->count;
	if (lsn > header->lsnmax)
		header->lsnmax = lsn;
	if (lsn < header->lsnmin)
		header->lsnmin = lsn;
	if (flags & SVDUP) {
		++header->countdup;
		if (lsn < header->lsnmindup)
			header->lsnmindup = lsn;
	}
	return 0;
}

/* write tuples from iterator to new page in run,
 * update page and the run statistics */
static int
vy_run_write_page(struct vy_file *file, struct svwriteiter *iwrite,
		  struct vy_filterif *compression,
		  struct vy_page_index_header *index_header,
		  struct vy_page_info *page_info,
		  struct vy_buf *minmax_buf)
{
	memset(page_info, 0, sizeof(*page_info));

	struct vy_buf tuplesinfo, values;
	vy_buf_init(&tuplesinfo);
	vy_buf_init(&values);

	struct sdpageheader header;
	memset(&header, 0, sizeof(struct sdpageheader));
	header.lsnmin = UINT64_MAX;
	header.lsnmindup = UINT64_MAX;

	while (iwrite && sv_writeiter_has(iwrite)) {
		int rc = vy_run_dump_tuple(iwrite, &tuplesinfo, &values,
					   &header);
		if (rc != 0)
			goto err;
		sv_writeiter_next(iwrite);
	}
	struct vy_buf compressed;
	vy_buf_init(&compressed);
	header.sizeorigin = vy_buf_used(&tuplesinfo) + vy_buf_used(&values);
	header.size = header.sizeorigin;
	if (compression) {
		struct vy_filter f;
		if (vy_filter_init(&f, compression, VINYL_FINPUT))
			goto err;
		if (vy_filter_start(&f, &compressed) ||
		    vy_filter_next(&f, &compressed, tuplesinfo.s,
				   vy_buf_used(&tuplesinfo)) ||
		    vy_filter_next(&f, &compressed, values.s,
				   vy_buf_used(&values)) ||
		    vy_filter_complete(&f, &compressed)) {
			vy_filter_free(&f);
			goto err;
		}
		vy_filter_free(&f);
		header.size = vy_buf_used(&compressed);
	}

	header.crcdata = crc32_calc(0, tuplesinfo.s, vy_buf_used(&tuplesinfo));
	header.crcdata = crc32_calc(header.crcdata, values.s, vy_buf_used(&values));
	header.crc = vy_crcs(&header, sizeof(struct sdpageheader), 0);

	struct iovec iovv[3];
	struct vy_iov iov;
	vy_iov_init(&iov, iovv, 3);
	vy_iov_add(&iov, &header, sizeof(struct sdpageheader));
	if (compression) {
		vy_iov_add(&iov, compressed.s, vy_buf_used(&compressed));
	} else {
		vy_iov_add(&iov, tuplesinfo.s, vy_buf_used(&tuplesinfo));
		vy_iov_add(&iov, values.s, vy_buf_used(&values));
	}
	if (vy_file_writev(file, &iov) < 0) {
		vy_error("file '%s' write error: %s",
		               file->path,
		               strerror(errno));
		goto err;
	}

	/*
	 * Update statistic in page header and page index header.
	 */
	page_info->min_lsn = header.lsnmin;
	page_info->max_lsn = header.lsnmax;
	page_info->size = header.size + sizeof(struct sdpageheader);
	page_info->unpacked_size = header.sizeorigin + sizeof(struct sdpageheader);

	if (header.count > 0) {
		struct sdv *tuplesinfoarr = (struct sdv *) tuplesinfo.s;
		struct sdv *mininfo = &tuplesinfoarr[0];
		struct sdv *maxinfo = &tuplesinfoarr[header.count - 1];
		if (vy_buf_ensure(minmax_buf, mininfo->size + maxinfo->size))
			goto err;

		page_info->min_key_offset = vy_buf_used(minmax_buf);
		page_info->min_key_lsn = mininfo->lsn;
		char *minvalue = values.s + mininfo->offset;
		memcpy(minmax_buf->p, minvalue, mininfo->size);
		vy_buf_advance(minmax_buf, mininfo->size);

		page_info->max_key_offset = vy_buf_used(minmax_buf);
		page_info->max_key_lsn = maxinfo->lsn;
		char *maxvalue = values.s + maxinfo->offset;
		memcpy(minmax_buf->p, maxvalue, maxinfo->size);
		vy_buf_advance(minmax_buf, maxinfo->size);
	}

	++index_header->count;
	if (page_info->min_lsn < index_header->lsnmin)
		index_header->lsnmin = page_info->min_lsn;
	if (page_info->max_lsn > index_header->lsnmax)
		index_header->lsnmax = page_info->max_lsn;
	index_header->total += page_info->size;
	index_header->totalorigin += page_info->unpacked_size;

	if (index_header->dupmin > header.lsnmindup)
		index_header->dupmin = header.lsnmindup;
	index_header->keys += header.count;
	index_header->dupkeys += header.countdup;

	vy_buf_free(&compressed);
	vy_buf_free(&tuplesinfo);
	vy_buf_free(&values);
	return 0;
err:
	vy_buf_free(&compressed);
	vy_buf_free(&tuplesinfo);
	vy_buf_free(&values);
	return -1;
}

/* write tuples for iterator to new run
 * and setup corresponding sdindex structure */
static int
vy_run_write(struct vy_file *file, struct svwriteiter *iwrite,
	     struct vy_filterif *compression, uint64_t limit, struct sdid *id,
	     struct vy_page_index *sdindex)
{
	uint64_t seal_offset = file->size;
	struct sdseal seal;
	sd_sealset_open(&seal);
	if (vy_file_write(file, &seal, sizeof(struct sdseal)) < 0) {
		vy_error("file '%s' write error: %s",
		               file->path,
		               strerror(errno));
		goto err;
	}

	struct vy_page_index_header *index_header = &sdindex->header;
	memset(index_header, 0, sizeof(struct vy_page_index_header));
	sr_version_storage(&index_header->version);
	index_header->lsnmin = UINT64_MAX;
	index_header->dupmin = UINT64_MAX;
	index_header->id = *id;

	do {
		uint64_t page_offset = file->size;

		if (vy_buf_ensure(&sdindex->pages, sizeof(struct vy_page_info)))
			goto err;
		struct vy_page_info *page = (struct vy_page_info *)sdindex->pages.p;
		vy_buf_advance(&sdindex->pages, sizeof(struct vy_page_info));
		if (vy_run_write_page(file, iwrite, compression, index_header,
				      page, &sdindex->minmax))
			goto err;

		page->offset = page_offset;

	} while (index_header->total < limit && iwrite && sv_writeiter_resume(iwrite));

	index_header->size = vy_buf_used(&sdindex->pages) +
				vy_buf_used(&sdindex->minmax);
	index_header->offset = file->size;
	index_header->crc = vy_crcs(index_header, sizeof(struct vy_page_index_header), 0);

	sd_sealset_close(&seal, index_header);

	struct iovec iovv[3];
	struct vy_iov iov;
	vy_iov_init(&iov, iovv, 3);
	vy_iov_add(&iov, index_header, sizeof(struct vy_page_index_header));
	vy_iov_add(&iov, sdindex->pages.s, vy_buf_used(&sdindex->pages));
	vy_iov_add(&iov, sdindex->minmax.s, vy_buf_used(&sdindex->minmax));
	if (vy_file_writev(file, &iov) < 0 ||
		vy_file_pwrite(file, seal_offset, &seal, sizeof(struct sdseal)) < 0) {
		vy_error("file '%s' write error: %s",
		               file->path,
		               strerror(errno));
		goto err;
	}
	if (vy_file_sync(file) == -1) {
		vy_error("index file '%s' sync error: %s",
		               file->path, strerror(errno));
		return -1;
	}

	return 0;
err:
	return -1;
}

static inline int
vy_run_create(struct vy_index *index, struct sdc *c,
		struct vy_range *parent, struct svindex *vindex,
		uint64_t vlsn, struct vy_run **result)
{
	(void)c;
	struct vy_env *env = index->env;

	/* in-memory mode blob */
	int rc;
	struct svmerge vmerge;
	sv_mergeinit(&vmerge, index, index->key_def);
	rc = sv_mergeprepare(&vmerge, 1);
	if (unlikely(rc == -1))
		return -1;
	struct svmergesrc *s = sv_mergeadd(&vmerge, NULL);
	sv_indexiter_open(&s->src, vindex, VINYL_GE, NULL, 0);

	struct svmergeiter imerge;
	sv_mergeiter_open(&imerge, &vmerge, VINYL_GE);

	struct svwriteiter iwrite;
	sv_writeiter_open(&iwrite, &imerge,
			  index->key_def->opts.page_size,
			  sizeof(struct sdv),
			  vlsn, 1, 1);
	struct sdid id;
	id.flags = 1; /* run */;
	id.id = vy_sequence(env->seq, VINYL_NSN_NEXT);
	id.parent = parent->self.id.id;
	struct vy_page_index sdindex;
	vy_page_index_init(&sdindex);
	if ((rc = vy_run_write(&parent->file, &iwrite,
			        index->conf.compression_if, UINT64_MAX,
			        &id, &sdindex)))
		goto err;

	*result = vy_run_new();
	if (!(*result))
		goto err;
	(*result)->id = id;
	(*result)->index = sdindex;

	sv_writeiter_close(&iwrite);
	sv_mergefree(&vmerge);
	return 0;
err:
	sv_writeiter_close(&iwrite);
	sv_mergefree(&vmerge);
	return -1;
}

static int
vy_dump(struct vy_index *index, struct sdc *c, struct vy_range *n,
        uint64_t vlsn)
{
	struct vy_env *env = index->env;
	assert(n->flags & SI_LOCK);

	vy_index_lock(index);
	if (unlikely(n->used == 0)) {
		vy_range_unlock(n);
		vy_index_unlock(index);
		return 0;
	}
	struct svindex *i;
	i = vy_range_rotate(n);
	vy_index_unlock(index);

	struct vy_run *run = NULL;
	int rc = vy_run_create(index, c, n, i, vlsn, &run);
	if (unlikely(rc == -1))
		return -1;
	if (unlikely(run == NULL)) {
		vy_index_lock(index);
		assert(n->used >= i->used);
		n->used -= i->used;
		vy_quota_op(env->quota, VINYL_QREMOVE, i->used);
		struct svindex swap = *i;
		swap.tree.arg = &swap;
		vy_range_unrotate(n);
		vy_range_unlock(n);
		vy_planner_update(&index->p, n);
		vy_index_unlock(index);
		vy_range_gc_index(env, &swap);
		return 0;
	}

	/* commit */
	vy_index_lock(index);
	run->next = n->run;
	n->run->link = run;
	n->run = run;
	n->run_count++;
	assert(n->used >= i->used);
	n->used -= i->used;
	vy_quota_op(env->quota, VINYL_QREMOVE, i->used);
	index->size += vy_page_index_size(&run->index) +
		       vy_page_index_total(&run->index);
	struct svindex swap = *i;
	swap.tree.arg = &swap;
	vy_range_unrotate(n);
	vy_range_unlock(n);
	vy_planner_update(&index->p, n);
	vy_index_unlock(index);

	vy_range_gc_index(env, &swap);
	return 1;
}

void
vy_tmp_iterator_open(struct vy_iter *virt_itr, struct vy_index *index,
		     struct vy_run *run, struct vy_file *file,
		     struct vy_filterif *compression,
		     enum vy_order order, char *key);


static int
si_compact(struct vy_index *index, struct sdc *c, struct vy_range *node,
	   uint64_t vlsn, struct vy_iter *vindex, uint64_t vindex_used)
{
	assert(node->flags & SI_LOCK);

	/* prepare for compaction */
	int rc;
	rc = sd_censure(c, node->run_count);
	if (unlikely(rc == -1))
		return -1;
	struct svmerge merge;
	sv_mergeinit(&merge, index, index->key_def);
	rc = sv_mergeprepare(&merge, node->run_count + 1);
	if (unlikely(rc == -1))
		return -1;

	/* include vindex into merge process */
	uint32_t count = 0;
	uint64_t size_stream = 0;
	if (vindex) {
		sv_mergeadd(&merge, vindex);
		size_stream = vindex_used;
	}

	struct vy_run *run = node->run;
	while (run) {
		struct svmergesrc *s = sv_mergeadd(&merge, NULL);
		struct vy_filterif *compression = NULL;
		if (index->conf.compression)
			compression = index->conf.compression_if;
		vy_tmp_iterator_open(&s->src, index, run, &node->file,
				     compression, VINYL_GE, NULL);
		size_stream += vy_page_index_total(&run->index);
		count += vy_page_index_count(&run->index);
		run = run->next;
	}
	struct svmergeiter im;
	sv_mergeiter_open(&im, &merge, VINYL_GE);
	rc = si_merge(index, c, node, vlsn, &im, size_stream, count);
	sv_mergefree(&merge);
	return rc;
}

static int vy_index_droprepository(char *repo, int drop_directory)
{
	DIR *dir = opendir(repo);
	if (dir == NULL) {
		vy_error("directory '%s' open error: %s",
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
			vy_error("index file '%s' unlink error: %s",
			               path, strerror(errno));
			closedir(dir);
			return -1;
		}
	}
	closedir(dir);

	snprintf(path, sizeof(path), "%s/drop", repo);
	rc = unlink(path);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' unlink error: %s",
		               path, strerror(errno));
		return -1;
	}
	if (drop_directory) {
		rc = rmdir(repo);
		if (unlikely(rc == -1)) {
			vy_error("directory '%s' unlink error: %s",
			               repo, strerror(errno));
			return -1;
		}
		char space_path[PATH_MAX];
		char *last_path_sep = strrchr(repo, '/');
		if (last_path_sep) {
			int len = last_path_sep - repo + 1;
			snprintf(space_path, len, "%s", repo);
			rc = rmdir(space_path);
			if (unlikely(rc == -1)) {
				vy_error("directory '%s' unlink error: %s",
				         space_path, strerror(errno));
				return -1;
			}
		}
	}
	return 0;
}

static int vy_index_dropmark(struct vy_index *i)
{
	/* create drop file */
	char path[1024];
	snprintf(path, sizeof(path), "%s/drop", i->conf.path);
	struct vy_file drop;
	vy_file_init(&drop);
	int rc = vy_file_new(&drop, path);
	if (unlikely(rc == -1)) {
		vy_error("drop file '%s' create error: %s",
		               path, strerror(errno));
		return -1;
	}
	vy_file_close(&drop);
	return 0;
}

static int vy_task_index_drop(struct vy_index *index)
{
	struct vy_range *node, *n;
	rlist_foreach_entry_safe(node, &index->gc, gc, n) {
		if (vy_range_free(node, index->env, 1) != 0)
			return -1;
	}
	/* remove directory */
	if (vy_index_droprepository(index->conf.path, 1) != 0)
		return -1;
	/* free memory */
	vy_index_delete(index);
	return 0;
}

static int
si_redistribute(struct vy_index *index, struct sdc *c,
		struct vy_range *node, struct vy_buf *result)
{
	(void)index;
	struct svindex *vindex = vy_range_index(node);
	struct vy_iter ii;
	sv_indexiter_open(&ii, vindex, VINYL_GE, NULL, 0);
	while (sv_indexiter_has(&ii))
	{
		struct sv *v = sv_indexiter_get(&ii);
		int rc = vy_buf_add(&c->b, &v->v, sizeof(struct svref **));
		if (unlikely(rc == -1))
			return -1;
		sv_indexiter_next(&ii);
	}
	if (unlikely(vy_buf_used(&c->b) == 0))
		return 0;
	struct vy_bufiter i, j;
	vy_bufiter_open(&i, &c->b, sizeof(struct svref*));
	vy_bufiter_open(&j, result, sizeof(struct vy_range*));
	struct vy_range *prev = vy_bufiterref_get(&j);
	vy_bufiter_next(&j);
	while (1)
	{
		struct vy_range *p = vy_bufiterref_get(&j);
		if (p == NULL) {
			assert(prev != NULL);
			while (vy_bufiter_has(&i)) {
				struct svref *v = vy_bufiterref_get(&i);
				sv_indexset(&prev->i0, *v);
				vy_bufiter_next(&i);
			}
			break;
		}
		while (vy_bufiter_has(&i))
		{
			struct svref *v = vy_bufiterref_get(&i);
			struct vy_page_info *page = vy_page_index_first_page(&p->self.index);
			int rc = vy_tuple_compare(v->v->data,
				vy_page_index_min_key(&p->self.index, page),
				index->key_def);
			if (unlikely(rc >= 0))
				break;
			sv_indexset(&prev->i0, *v);
			vy_bufiter_next(&i);
		}
		if (unlikely(! vy_bufiter_has(&i)))
			break;
		prev = p;
		vy_bufiter_next(&j);
	}
	assert(vy_bufiterref_get(&i) == NULL);
	return 0;
}

static inline void
si_redistribute_set(struct vy_index *index, uint64_t now, struct svref *v)
{
	index->update_time = now;
	/* match node */
	struct vy_rangeiter ii;
	vy_rangeiter_open(&ii, index, VINYL_GE, v->v->data, v->v->size);
	struct vy_range *node = vy_rangeiter_get(&ii);
	assert(node != NULL);
	/* update node */
	struct svindex *vindex = vy_range_index(node);
	int rc = sv_indexset(vindex, *v);
	assert(rc == 0); /* TODO: handle BPS tree errors properly */
	(void) rc;
	node->update_time = index->update_time;
	node->used += vy_tuple_size(v->v);
	/* schedule node */
	vy_planner_update_range(&index->p, node);
}

static int
si_redistribute_index(struct vy_index *index, struct sdc *c, struct vy_range *node)
{
	struct svindex *vindex = vy_range_index(node);
	struct vy_iter ii;
	sv_indexiter_open(&ii, vindex, VINYL_GE, NULL, 0);
	while (sv_indexiter_has(&ii)) {
		struct sv *v = sv_indexiter_get(&ii);
		int rc = vy_buf_add(&c->b, &v->v, sizeof(struct svref**));
		if (unlikely(rc == -1))
			return -1;
		sv_indexiter_next(&ii);
	}
	if (unlikely(vy_buf_used(&c->b) == 0))
		return 0;
	uint64_t now = clock_monotonic64();
	struct vy_bufiter i;
	vy_bufiter_open(&i, &c->b, sizeof(struct svref*));
	while (vy_bufiter_has(&i)) {
		struct svref *v = vy_bufiterref_get(&i);
		si_redistribute_set(index, now, v);
		vy_bufiter_next(&i);
	}
	return 0;
}

static int
si_splitfree(struct vy_buf *result, struct vy_env *env)
{
	struct vy_bufiter i;
	vy_bufiter_open(&i, result, sizeof(struct vy_range*));
	while (vy_bufiter_has(&i))
	{
		struct vy_range *p = vy_bufiterref_get(&i);
		vy_range_free(p, env, 0);
		vy_bufiter_next(&i);
	}
	return 0;
}

static inline int
si_split(struct vy_index *index, struct sdc *c, struct vy_buf *result,
         struct vy_range   *parent,
         struct svmergeiter *merge_iter,
         uint64_t  size_node,
         uint64_t  size_stream,
         uint32_t  stream,
         uint64_t  vlsn)
{
	(void) stream;
	(void) size_node;
	(void) c;
	struct vy_env *env = index->env;
	int rc;
	struct vy_range *n = NULL;

	struct svwriteiter iwrite;
	sv_writeiter_open(&iwrite, merge_iter,
			  index->key_def->opts.page_size, sizeof(struct sdv),
			  vlsn, 0, 0);

	while (sv_writeiter_has(&iwrite)) {
		struct vy_page_index sdindex;
		vy_page_index_init(&sdindex);
		/* create new node */
		n = vy_range_new(index->key_def);
		if (unlikely(n == NULL))
			goto error;
		struct sdid id = {
			.parent = parent->self.id.id,
			.flags  = 0,
			.id     = vy_sequence(index->env->seq, VINYL_NSN_NEXT)
		};
		rc = vy_range_create(n, &index->conf, &id);
		if (unlikely(rc == -1))
			goto error;
		n->run = &n->self;
		n->run_count++;

		if ((rc = vy_run_write(&n->file, &iwrite,
				       index->conf.compression_if,
				       size_stream, &id, &sdindex)))
			goto error;

		rc = vy_buf_add(result, &n, sizeof(struct vy_range*));
		if (unlikely(rc == -1))
			goto error;

		n->self.id = id;
		n->self.index = sdindex;
	}
	sv_writeiter_close(&iwrite);
	return 0;
error:
	sv_writeiter_close(&iwrite);
	if (n)
		vy_range_free(n, env, 0);
	si_splitfree(result, env);
	return -1;
}

static int
si_merge(struct vy_index *index, struct sdc *c, struct vy_range *range,
	 uint64_t vlsn, struct svmergeiter *stream, uint64_t size_stream,
	 uint32_t n_stream)
{
	struct vy_env *env = index->env;
	struct vy_buf *result = &c->a;
	struct vy_bufiter i;

	/* begin compaction.
	 *
	 * Split merge stream into a number of
	 * a new nodes.
	 */
	int rc;
	rc = si_split(index, c, result, range, stream,
		      index->key_def->opts.node_size,
		      size_stream, n_stream, vlsn);
	if (unlikely(rc == -1))
		return -1;

	VINYL_INJECTION(r->i, VINYL_INJECTION_SI_COMPACTION_0,
			si_splitfree(result, r);
			vy_error("%s", "error injection");
			return -1);

	/* mask removal of a single range as a
	 * single range update */
	int count = vy_buf_used(result) / sizeof(struct vy_range*);

	vy_index_lock(index);
	int range_count = index->range_count;
	vy_index_unlock(index);

	struct vy_range *n;
	if (unlikely(count == 0 && range_count == 1))
	{
		n = si_bootstrap(index, range->self.id.id);
		if (unlikely(n == NULL))
			return -1;
		rc = vy_buf_add(result, &n, sizeof(struct vy_range*));
		if (unlikely(rc == -1)) {
			vy_range_free(n, env, 1);
			return -1;
		}
		count++;
	}

	/* commit compaction changes */
	vy_index_lock(index);
	struct svindex *j = vy_range_index(range);
	vy_planner_remove(&index->p, range);
	vy_range_split(range);
	index->size -= vy_range_size(range);
	switch (count) {
	case 0: /* delete */
		si_remove(index, range);
		si_redistribute_index(index, c, range);
		break;
	case 1: /* self update */
		n = *(struct vy_range**)result->s;
		n->i0 = *j;
		n->i0.tree.arg = &n->i0;
		n->temperature = range->temperature;
		n->temperature_reads = range->temperature_reads;
		n->used = j->used;
		index->size += vy_range_size(n);
		vy_range_lock(n);
		si_replace(index, range, n);
		vy_planner_update(&index->p, n);
		break;
	default: /* split */
		rc = si_redistribute(index, c, range, result);
		if (unlikely(rc == -1)) {
			vy_index_unlock(index);
			si_splitfree(result, env);
			return -1;
		}
		vy_bufiter_open(&i, result, sizeof(struct vy_range*));
		n = vy_bufiterref_get(&i);
		n->used = n->i0.used;
		n->temperature = range->temperature;
		n->temperature_reads = range->temperature_reads;
		index->size += vy_range_size(n);
		vy_range_lock(n);
		si_replace(index, range, n);
		vy_planner_update(&index->p, n);
		for (vy_bufiter_next(&i); vy_bufiter_has(&i);
		     vy_bufiter_next(&i)) {
			n = vy_bufiterref_get(&i);
			n->used = n->i0.used;
			n->temperature = range->temperature;
			n->temperature_reads = range->temperature_reads;
			index->size += vy_range_size(n);
			vy_range_lock(n);
			si_insert(index, n);
			vy_planner_update(&index->p, n);
		}
		break;
	}
	sv_indexinit(j, index->key_def);
	vy_index_unlock(index);

	/* compaction completion */

	/* seal nodes */
	vy_bufiter_open(&i, result, sizeof(struct vy_range*));
	while (vy_bufiter_has(&i))
	{
		n  = vy_bufiterref_get(&i);
		rc = vy_range_seal(n, &index->conf);
		if (unlikely(rc == -1)) {
			vy_range_free(range, env, 0);
			return -1;
		}
		VINYL_INJECTION(r->i, VINYL_INJECTION_SI_COMPACTION_3,
		             vy_range_free(range, env, 0);
		             vy_error("%s", "error injection");
		             return -1);
		vy_bufiter_next(&i);
	}

	VINYL_INJECTION(r->i, VINYL_INJECTION_SI_COMPACTION_1,
	             vy_range_free(range, env, 0);
	             vy_error("%s", "error injection");
	             return -1);

	/* gc range */
	rc = vy_range_free(range, env, 1);
	if (unlikely(rc == -1))
		return -1;
	VINYL_INJECTION(r->i, VINYL_INJECTION_SI_COMPACTION_2,
	             vy_error("%s", "error injection");
	             return -1);

	/* complete new nodes */
	vy_bufiter_open(&i, result, sizeof(struct vy_range*));
	while (vy_bufiter_has(&i))
	{
		n = vy_bufiterref_get(&i);
		rc = vy_range_complete(n, &index->conf);
		if (unlikely(rc == -1))
			return -1;
		VINYL_INJECTION(r->i, VINYL_INJECTION_SI_COMPACTION_4,
		             vy_error("%s", "error injection");
		             return -1);
		vy_bufiter_next(&i);
	}

	/* unlock */
	vy_index_lock(index);
	vy_bufiter_open(&i, result, sizeof(struct vy_range*));
	while (vy_bufiter_has(&i))
	{
		n = vy_bufiterref_get(&i);
		vy_range_unlock(n);
		vy_bufiter_next(&i);
	}
	vy_index_unlock(index);
	return 0;
}

static struct vy_range *
vy_range_new(struct key_def *key_def)
{
	struct vy_range *n = (struct vy_range*)malloc(sizeof(struct vy_range));
	if (unlikely(n == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_range), "malloc",
			 "struct vy_range");
		return NULL;
	}
	n->recover = 0;
	n->ac = 0;
	n->flags = 0;
	n->update_time = 0;
	n->used = 0;
	vy_run_init(&n->self);
	n->run = NULL;
	n->run_count = 0;
	n->temperature = 0;
	n->temperature_reads = 0;
	n->refs = 0;
	tt_pthread_mutex_init(&n->reflock, NULL);
	vy_file_init(&n->file);
	sv_indexinit(&n->i0, key_def);
	sv_indexinit(&n->i1, key_def);
	ss_rqinitnode(&n->nodecompact);
	ss_rqinitnode(&n->nodedump);
	rlist_create(&n->gc);
	rlist_create(&n->commit);
	return n;
}

static int vy_range_gc_index(struct vy_env *env, struct svindex *i)
{
	sv_indexfree(i, env);
	sv_indexinit(i, i->key_def);
	return 0;
}

static inline int
vy_range_close(struct vy_range *n, struct vy_env *env, int gc)
{
	int rcret = 0;

	int rc = vy_file_close(&n->file);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' close error: %s",
		               n->file.path,
		               strerror(errno));
		rcret = -1;
	}
	if (gc) {
		vy_range_gc_index(env, &n->i0);
		vy_range_gc_index(env, &n->i1);
	} else {
		sv_indexfree(&n->i0, env);
		sv_indexfree(&n->i1, env);
		tt_pthread_mutex_destroy(&n->reflock);
	}
	return rcret;
}

static inline int
vy_range_recover(struct vy_range *n, struct vy_env *env)
{
	/* recover runs */
	struct vy_run *run = NULL;
	struct sdrecover ri;
	sd_recover_open(&ri, env, &n->file);
	int first = 1;
	int rc;
	while (sd_recover_has(&ri))
	{
		struct vy_page_index_header *h = sd_recover_get(&ri);
		if (first) {
			run = &n->self;
		} else {
			run = vy_run_new();
			if (unlikely(run == NULL))
				goto e0;
		}
		struct vy_page_index index;
		vy_page_index_init(&index);
		rc = vy_page_index_load(&index, h);
		if (unlikely(rc == -1))
			goto e0;
		vy_run_set(run, &index);

		run->next = n->run;
		n->run = run;
		n->run_count++;

		first = 0;
		sd_recover_next(&ri);
	}
	rc = sd_recover_complete(&ri);
	if (unlikely(rc == -1))
		goto e1;
	sd_recover_close(&ri);
	return 0;
e0:
	if (run != NULL && !first)
		vy_run_free(run);
e1:
	sd_recover_close(&ri);
	return -1;
}

static int
vy_range_open(struct vy_range *n, struct vy_env *env, char *path)
{
	int rc = vy_file_open(&n->file, path);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' open error: %s "
		               "(please ensure storage version compatibility)",
		               n->file.path,
		               strerror(errno));
		return -1;
	}
	rc = vy_file_seek(&n->file, n->file.size);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' seek error: %s",
		               n->file.path,
		               strerror(errno));
		return -1;
	}
	rc = vy_range_recover(n, env);
	if (unlikely(rc == -1))
		return -1;
	return 0;
}

static int
vy_range_create(struct vy_range *n, struct vy_index_conf *scheme,
	      struct sdid *id)
{
	char path[PATH_MAX];
	vy_path_compound(path, scheme->path, id->parent, id->id,
	                ".index.incomplete");
	int rc = vy_file_new(&n->file, path);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' create error: %s",
		               path, strerror(errno));
		return -1;
	}
	return 0;
}

static inline void
vy_range_free_runs(struct vy_range *n)
{
	struct vy_run *p = n->run;
	struct vy_run *next = NULL;
	while (p && p != &n->self) {
		next = p->next;
		vy_run_free(p);
		p = next;
	}
	vy_page_index_free(&n->self.index);
}

static int vy_range_free(struct vy_range *n, struct vy_env *env, int gc)
{
	int rcret = 0;
	int rc;
	if (gc && n->file.path[0]) {
		vy_file_advise(&n->file, 0, 0, n->file.size);
		rc = unlink(n->file.path);
		if (unlikely(rc == -1)) {
			vy_error("index file '%s' unlink error: %s",
			               n->file.path,
			               strerror(errno));
			rcret = -1;
		}
	}
	vy_range_free_runs(n);
	rc = vy_range_close(n, env, gc);
	if (unlikely(rc == -1))
		rcret = -1;
	free(n);
	return rcret;
}

static int vy_range_seal(struct vy_range *n, struct vy_index_conf *scheme)
{
	int rc;
	if (scheme->sync) {
		rc = vy_file_sync(&n->file);
		if (unlikely(rc == -1)) {
			vy_error("index file '%s' sync error: %s",
			               n->file.path,
			               strerror(errno));
			return -1;
		}
	}
	char path[PATH_MAX];
	vy_path_compound(path, scheme->path,
	                n->self.id.parent, n->self.id.id,
	                ".index.seal");
	rc = vy_file_rename(&n->file, path);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' rename error: %s",
		               n->file.path,
		               strerror(errno));
		return -1;
	}
	return 0;
}

static int
vy_range_complete(struct vy_range *n, struct vy_index_conf *scheme)
{
	char path[PATH_MAX];
	vy_path_init(path, scheme->path, n->self.id.id, ".index");
	int rc = vy_file_rename(&n->file, path);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' rename error: %s",
		               n->file.path,
		               strerror(errno));
	}
	return rc;
}

static int vy_planner_init(struct vy_planner *p)
{
	int rc = ss_rqinit(&p->compact, 1, 20);
	if (unlikely(rc == -1))
		return -1;
	/* 1Mb step up to 4Gb */
	rc = ss_rqinit(&p->dump, 1024 * 1024, 4000);
	if (unlikely(rc == -1)) {
		ss_rqfree(&p->compact);
		return -1;
	}
	return 0;
}

static int vy_planner_free(struct vy_planner *p)
{
	ss_rqfree(&p->compact);
	ss_rqfree(&p->dump);
	return 0;
}

static int
vy_planner_update(struct vy_planner *p, struct vy_range *n)
{
	ss_rqupdate(&p->dump, &n->nodedump, n->used);
	ss_rqupdate(&p->compact, &n->nodecompact, n->run_count);
	return 0;
}

static int
vy_planner_update_range(struct vy_planner *p, struct vy_range *n)
{
	ss_rqupdate(&p->dump, &n->nodedump, n->used);
	return 0;
}

static int
vy_planner_remove(struct vy_planner *p, struct vy_range *n)
{
	ss_rqdelete(&p->dump, &n->nodedump);
	ss_rqdelete(&p->compact, &n->nodecompact);
	return 0;
}

static inline void
vy_task_create(struct vy_task *task, struct vy_index *index,
	       enum vy_task_type type)
{
	memset(task, 0, sizeof(*task));
	task->type = type;
	task->index = index;
	vy_index_ref(index);
}

static inline void
vy_task_destroy(struct vy_task *task)
{
	if (task->type != VY_TASK_DROP) {
		vy_index_unref(task->index);
		task->index = NULL;
	}
	TRASH(task);
}

static inline int
vy_planner_peek_checkpoint(struct vy_index *index, uint64_t checkpoint_lsn,
			   struct vy_task *task)
{
	/* try to peek a node which has min
	 * lsn <= required value
	*/
	bool in_progress = false;
	struct vy_range *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.dump, pn))) {
		n = container_of(pn, struct vy_range, nodedump);
		if (n->i0.lsnmin > checkpoint_lsn)
			continue;
		if (n->flags & SI_LOCK) {
			in_progress = true;
			continue;
		}
		vy_task_create(task, index, VY_TASK_CHECKPOINT);
		vy_range_lock(n);
		task->node = n;
		return 1; /* new task */
	}
	if (!in_progress) {
		/* no more ranges to dump */
		index->checkpoint_in_progress = false;
	}
	return 0; /* nothing to do */
}

static inline int
vy_planner_peek_dump(struct vy_index *index, uint32_t dump_wm,
		     struct vy_task *task)
{
	/* try to peek a node with a biggest in-memory index */
	struct vy_range *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.dump, pn))) {
		n = container_of(pn, struct vy_range, nodedump);
		if (n->flags & SI_LOCK)
			continue;
		if (n->used < dump_wm)
			return 0; /* nothing to do */
		vy_task_create(task, index, VY_TASK_DUMP);
		vy_range_lock(n);
		task->node = n;
		return 1; /* new task */
	}
	return 0; /* nothing to do */
}

static inline int
vy_planner_peek_age(struct vy_index *index, uint32_t ttl, uint32_t ttl_wm,
		    struct vy_task *task)
{
	/* try to peek a node with update >= a and in-memory
	 * index size >= b */

	/* full scan */
	uint64_t now = clock_monotonic64();
	struct vy_range *n = NULL;
	struct ssrqnode *pn = NULL;
	bool in_progress = false;
	while ((pn = ss_rqprev(&index->p.dump, pn))) {
		n = container_of(pn, struct vy_range, nodedump);
		if (n->flags & SI_LOCK) {
			in_progress = true;
			continue;
		}
		if (n->used < ttl_wm && (now - n->update_time) < ttl)
			continue;
		vy_task_create(task, index, VY_TASK_AGE);
		vy_range_lock(n);
		task->node = n;
		return 1; /* new task */
	}
	if (!in_progress) {
		/* no more ranges */
		index->age_in_progress = false;
	}
	return 0; /* nothing to do */
}

static inline int
vy_planner_peek_compact(struct vy_index *index, uint32_t run_count,
			struct vy_task *task)
{
	/* try to peek a node with a biggest number
	 * of runs */
	struct vy_range *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.compact, pn))) {
		n = container_of(pn, struct vy_range, nodecompact);
		if (n->flags & SI_LOCK)
			continue;
		if (n->run_count >= run_count) {
			vy_task_create(task, index, VY_TASK_COMPACT);
			vy_range_lock(n);
			task->node = n;
			return 1; /* new task */
		}
		break; /* TODO: why? */
	}
	return 0; /* nothing to do */
}

static inline int
vy_planner_peek_gc(struct vy_index *index, uint64_t gc_lsn,
		   uint32_t gc_percent, struct vy_task *task)
{
	/* try to peek a node with a biggest number
	 * of runs which is ready for gc */
	bool in_progress = false;
	struct vy_range *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.compact, pn))) {
		n = container_of(pn, struct vy_range, nodecompact);
		struct vy_page_index_header *h = &n->self.index.header;
		if (likely(h->dupkeys == 0) || (h->dupmin >= gc_lsn))
			continue;
		uint32_t used = (h->dupkeys * 100) / h->keys;
		if (used >= gc_percent) {
			if (n->flags & SI_LOCK) {
				in_progress = true;
				continue;
			}
			vy_task_create(task, index, VY_TASK_GC);
			vy_range_lock(n);
			task->node = n;
			return 1; /* new task */
		}
	}
	if (!in_progress) {
		/* no more ranges to gc */
		index->gc_in_progress = false;
	}
	return 0; /* nothing to do */
}

static inline int
vy_planner_peek_shutdown(struct vy_index *index, struct vy_task *task)
{
	int status = vy_status(&index->status);
	switch (status) {
	case VINYL_DROP:
		if (index->refs > 0)
			return 0; /* index still has tasks */
		vy_task_create(task, index, VY_TASK_DROP);
		return 1; /* new task */
	default:
		unreachable();
		return -1;
	}
}

static inline int
vy_planner_peek_nodegc(struct vy_index *index, struct vy_task *task)
{
	struct vy_range *n;
	rlist_foreach_entry(n, &index->gc, gc) {
		vy_task_create(task, index, VY_TASK_NODEGC);
		rlist_del(&n->gc);
		task->node = n;
		return 1; /* new task */
	}
	return 0; /* nothing to do */
}

static int vy_profiler_begin(struct vy_profiler *p, struct vy_index *i)
{
	memset(p, 0, sizeof(*p));
	p->i = i;
	p->temperature_min = 100;
	vy_index_lock(i);
	return 0;
}

static int vy_profiler_end(struct vy_profiler *p)
{
	vy_index_unlock(p->i);
	return 0;
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
	struct vy_range *n = vy_range_tree_first(&p->i->tree);
	while (n) {
		if (p->temperature_max < n->temperature)
			p->temperature_max = n->temperature;
		if (p->temperature_min > n->temperature)
			p->temperature_min = n->temperature;
		temperature_total += n->temperature;
		p->total_node_count++;
		p->count += n->i0.tree.size;
		p->count += n->i1.tree.size;
		p->total_run_count += n->run_count;
		if (p->total_run_max < n->run_count)
			p->total_run_max = n->run_count;
		if (n->run_count < 20)
			p->histogram_run[n->run_count]++;
		else
			p->histogram_run_20plus++;
		memory_used += n->i0.used;
		memory_used += n->i1.used;
		struct vy_run *run = n->run;
		while (run != NULL) {
			p->count += run->index.header.keys;
			p->count_dup += run->index.header.dupkeys;
			int indexsize = vy_page_index_size(&run->index);
			p->total_snapshot_size += indexsize;
			p->total_node_size += indexsize + run->index.header.total;
			p->total_node_origin_size += indexsize + run->index.header.totalorigin;
			p->total_page_count += run->index.header.count;
			run = run->next;
		}
		n = vy_range_tree_next(&p->i->tree, n);
	}
	if (p->total_node_count > 0) {
		p->total_run_avg =
			p->total_run_count / p->total_node_count;
		p->temperature_avg =
			temperature_total / p->total_node_count;
	}
	p->memory_used = memory_used;
	p->read_disk  = p->i->read_disk;
	p->read_cache = p->i->read_cache;

	vy_profiler_histogram_run(p);
	return 0;
}

static void
si_readopen(struct siread *q, struct vy_index *index, struct sicache *c,
	    enum vy_order order, uint64_t vlsn, void *key, uint32_t keysize)
{
	q->order = order;
	q->key = key;
	q->keysize = keysize;
	q->vlsn = vlsn;
	q->index = index;
	q->cache = c;
	q->has = 0;
	q->upsert_v = NULL;
	q->upsert_eq = 0;
	q->cache_only = 0;
	q->read_disk = 0;
	q->read_cache = 0;
	q->result = NULL;
	sv_mergeinit(&q->merge, index, index->key_def);
	vy_index_lock(index);
}

static int
si_readclose(struct siread *q)
{
	vy_index_unlock(q->index);
	sv_mergefree(&q->merge);
	return 0;
}

static inline int
si_readdup(struct siread *q, struct sv *result)
{
	struct vy_tuple *v;
	if (likely(result->i == &svtuple_if)) {
		v = result->v;
		vy_tuple_ref(v);
	} else {
		/* Allocate new tuple and copy data */
		uint32_t size = sv_size(result);
		v = vy_tuple_alloc(q->index, size);
		if (unlikely(v == NULL))
			return -1;
		memcpy(v->data, sv_pointer(result), size);
		v->flags = sv_flags(result);
		v->lsn = sv_lsn(result);
	}
	assert((v->flags & (SVUPSERT|SVDELETE|SVGET)) == 0);
	q->result = v;
	return 1;
}

static inline void
si_readstat(struct siread *q, int cache, struct vy_range *n, uint32_t reads)
{
	struct vy_index *index = q->index;
	if (cache) {
		index->read_cache += reads;
		q->read_cache += reads;
	} else {
		index->read_disk += reads;
		q->read_disk += reads;
	}
	/* update temperature */
	n->temperature_reads += reads;
	uint64_t total = index->read_disk + index->read_cache;
	if (unlikely(total == 0))
		return;
	n->temperature = (n->temperature_reads * 100ULL) / total;
}

static void
vy_upsert_iterator_close(struct vy_iter *itr)
{
	assert(itr->vif->close == vy_upsert_iterator_close);
	(void)itr;
}

static int
vy_upsert_iterator_has(struct vy_iter *itr)
{
	assert(itr->vif->has == vy_upsert_iterator_has);
	return *((struct sv **)itr->priv) != NULL;
}

static void *
vy_upsert_iterator_get(struct vy_iter *itr)
{
	assert(itr->vif->get == vy_upsert_iterator_get);
	return *((struct sv **)itr->priv);
}

static void
vy_upsert_iterator_next(struct vy_iter *itr)
{
	assert(itr->vif->next == vy_upsert_iterator_next);
	*((struct sv **)itr->priv) = NULL;
}

static void
vy_upsert_iterator_open(struct vy_iter *itr, struct sv *value)
{
	static struct vy_iterif vif = {
		.close = vy_upsert_iterator_close,
		.has = vy_upsert_iterator_has,
		.get = vy_upsert_iterator_get,
		.next = vy_upsert_iterator_next
	};
	itr->vif = &vif;
	*((struct sv **)itr->priv) = value;
}

static inline int
si_range(struct siread *q)
{
	assert(q->has == 0);

	struct vy_rangeiter ii;
	vy_rangeiter_open(&ii, q->index, q->order, q->key, q->keysize);
	struct vy_range *node;
next_node:
	node = vy_rangeiter_get(&ii);
	if (unlikely(node == NULL))
		return 0;

	/* prepare sources */
	struct svmerge *m = &q->merge;
	int count = node->run_count + 2 + 1;
	int rc = sv_mergeprepare(m, count);
	if (unlikely(rc == -1)) {
		diag_clear(diag_get());
		return -1;
	}

	/* external source (upsert) */
	if (unlikely(q->upsert_v && q->upsert_v->v)) {
		struct svmergesrc *s = sv_mergeadd(m, NULL);
		vy_upsert_iterator_open(&s->src, q->upsert_v);
	}

	/* in-memory indexes */
	struct svindex *second;
	struct svindex *first = vy_range_index_priority(node, &second);
	if (first->tree.size) {
		struct svmergesrc *s = sv_mergeadd(m, NULL);
		sv_indexiter_open(&s->src, first, q->order,
				  q->key, q->keysize);
	}
	if (unlikely(second && second->tree.size)) {
		struct svmergesrc *s = sv_mergeadd(m, NULL);
		sv_indexiter_open(&s->src, second, q->order,
				  q->key, q->keysize);
	}

	/* cache and runs */
	rc = si_cachevalidate(q->cache, node);
	if (unlikely(rc == -1))
		return -1;

	if (q->cache_only) {
		return 2;
	}
	si_readstat(q, 0, node, 0);

	struct vy_run *run = node->run;
	while (run) {
		struct svmergesrc *s = sv_mergeadd(m, NULL);
		struct vy_filterif *compression = NULL;
		if (q->index->conf.compression)
			compression = q->index->conf.compression_if;
		vy_tmp_iterator_open(&s->src, q->index, run, &node->file,
				     compression, q->order, q->key);
		run = run->next;
	}

	/* merge and filter data stream */
	struct svmergeiter im;
	sv_mergeiter_open(&im, m, q->order);
	struct svreaditer ri;
	sv_readiter_open(&ri, &im, q->vlsn, q->upsert_eq);
	struct sv *v = sv_readiter_get(&ri);
	if (unlikely(v == NULL)) {
		sv_mergereset(&q->merge);
		vy_rangeiter_next(&ii);
		sv_readiter_close(&ri);
		goto next_node;
	}

	rc = 1;
	/* convert upsert search to VINYL_EQ */
	if (q->upsert_eq && q->key == NULL) {
		/* key is [] */
		rc = !sv_is(v, SVDELETE);
	} else if (q->upsert_eq) {
		int res = vy_tuple_compare(sv_pointer(v), q->key,
					   q->merge.key_def);
		rc = res == 0;
		if (res == 0 && sv_is(v, SVDELETE))
			rc = 0; /* that is not what we wanted to find */
	}
	if (likely(rc == 1)) {
		if (unlikely(si_readdup(q, v) == -1)) {
			sv_readiter_close(&ri);
			return -1;
		}
	}

	/* skip a possible duplicates from data sources */
	sv_readiter_forward(&ri);
	sv_readiter_close(&ri);
	return rc;
}

static int
si_readcommited(struct vy_index *index, struct vy_tuple *tuple)
{
	/* search node index */
	struct vy_rangeiter ri;
	vy_rangeiter_open(&ri, index, VINYL_GE, tuple->data, tuple->size);
	struct vy_range *range = vy_rangeiter_get(&ri);
	assert(range != NULL);

	uint64_t lsn = tuple->lsn;
	/* search in-memory */
	struct svindex *second;
	struct svindex *first = vy_range_index_priority(range, &second);
	struct svref *ref = sv_indexfind(first, tuple->data,
					 tuple->size, UINT64_MAX);
	if ((ref == NULL || ref->v->lsn < lsn) && second != NULL)
		ref = sv_indexfind(second, tuple->data,
				   tuple->size, UINT64_MAX);
	if (ref != NULL && ref->v->lsn >= lsn)
		return 1;

	/* search runs */
	for (struct vy_run *run = range->run; run != NULL; run = run->next)
	{
		struct vy_page_iter ii;
		sd_indexiter_open(&ii, index->key_def, &run->index, VINYL_GE,
				  tuple->data);
		struct vy_page_info *page = sd_indexiter_get(&ii);
		if (page == NULL)
			continue;
		if (page->max_lsn >= lsn)
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

static struct vy_range *
si_bootstrap(struct vy_index *index, uint64_t parent)
{
	struct vy_env *env = index->env;
	/* create node */
	struct vy_range *n = vy_range_new(index->key_def);
	if (unlikely(n == NULL))
		return NULL;
	struct sdid id = {
		.parent = parent,
		.flags  = 0,
		.id     = vy_sequence(env->seq, VINYL_NSN_NEXT)
	};
	int rc;
	rc = vy_range_create(n, &index->conf, &id);
	if (unlikely(rc == -1))
		goto e0;
	n->run = &n->self;
	n->run_count++;

	/* create index with one empty page */
	struct vy_page_index sdindex;
	vy_page_index_init(&sdindex);
	vy_run_write(&n->file, NULL, index->conf.compression_if, 0, &id,
		     &sdindex);

	vy_run_set(&n->self, &sdindex);

	return n;
e0:
	vy_range_free(n, env, 0);
	return NULL;
}

static inline int
si_deploy(struct vy_index *index, struct vy_env *env, int create_directory)
{
	/* create directory */
	int rc;
	if (likely(create_directory)) {
		char space_path[PATH_MAX];
		char *last_path_sep = strrchr(index->conf.path, '/');
		if (last_path_sep) {
			int len = last_path_sep - index->conf.path + 1;
			snprintf(space_path, len, "%s", index->conf.path);
			rc = mkdir(space_path, 0777);
			if (unlikely(rc == -1) && errno != EEXIST) {
				vy_error("directory '%s' create error: %s",
				               space_path, strerror(errno));
				return -1;
			}
		}
		rc = mkdir(index->conf.path, 0777);
		if (unlikely(rc == -1)) {
			vy_error("directory '%s' create error: %s",
			               index->conf.path, strerror(errno));
			return -1;
		}
	}
	/* create initial node */
	struct vy_range *n = si_bootstrap(index, 0);
	if (unlikely(n == NULL))
		return -1;
	VINYL_INJECTION(r->i, VINYL_INJECTION_SI_RECOVER_0,
	             vy_range_free(n, env, 0);
	             vy_error("%s", "error injection");
	             return -1);
	rc = vy_range_complete(n, &index->conf);
	if (unlikely(rc == -1)) {
		vy_range_free(n, env, 1);
		return -1;
	}
	si_insert(index, n);
	vy_planner_update(&index->p, n);
	index->size = vy_range_size(n);
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
si_trackdir(struct sitrack *track, struct vy_env *env, struct vy_index *i)
{
	DIR *dir = opendir(i->conf.path);
	if (unlikely(dir == NULL)) {
		vy_error("directory '%s' open error: %s",
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

		struct vy_range *head, *node;
		char path[PATH_MAX];
		switch (rc) {
		case SI_RDB_DBI:
		case SI_RDB_DBSEAL: {
			/* find parent node and mark it as having
			 * incomplete compaction process */
			head = si_trackget(track, id_parent);
			if (likely(head == NULL)) {
				head = vy_range_new(i->key_def);
				if (unlikely(head == NULL))
					goto error;
				head->self.id.id = id_parent;
				head->recover = SI_RDB_UNDEF;
				si_trackset(track, head);
			}
			head->recover |= rc;
			/* remove any incomplete file made during compaction */
			if (rc == SI_RDB_DBI) {
				vy_path_compound(path, i->conf.path, id_parent, id,
				                ".index.incomplete");
				rc = unlink(path);
				if (unlikely(rc == -1)) {
					vy_error("index file '%s' unlink error: %s",
						 path, strerror(errno));
					goto error;
				}
				continue;
			}
			assert(rc == SI_RDB_DBSEAL);
			/* recover 'sealed' node */
			node = vy_range_new(i->key_def);
			if (unlikely(node == NULL))
				goto error;
			node->recover = SI_RDB_DBSEAL;
			vy_path_compound(path, i->conf.path, id_parent, id,
			                ".index.seal");
			rc = vy_range_open(node, env, path);
			if (unlikely(rc == -1)) {
				vy_range_free(node, env, 0);
				goto error;
			}
			si_trackset(track, node);
			si_trackmetrics(track, node);
			continue;
		}
		case SI_RDB_REMOVE:
			vy_path_init(path, i->conf.path, id, ".index.gc");
			rc = unlink(path);
			if (unlikely(rc == -1)) {
				vy_error("index file '%s' unlink error: %s",
					 path, strerror(errno));
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
		node = vy_range_new(i->key_def);
		if (unlikely(node == NULL))
			goto error;
		node->recover = SI_RDB;
		vy_path_init(path, i->conf.path, id, ".index");
		rc = vy_range_open(node, env, path);
		if (unlikely(rc == -1)) {
			vy_range_free(node, env, 0);
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
			vy_range_free(head, env, 0);
		}
	}
	closedir(dir);
	return 0;
error:
	closedir(dir);
	return -1;
}

static inline int
si_trackvalidate(struct sitrack *track, struct vy_buf *buf, struct vy_index *i)
{
	vy_buf_reset(buf);
	struct vy_range *n = vy_range_id_tree_last(&track->tree);
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
			struct vy_range *ancestor = si_trackget(track, n->self.id.parent);
			if (ancestor && (ancestor != n))
				ancestor->recover |= SI_RDB_REMOVE;
			break;
		}
		case SI_RDB_DBSEAL: {
			/* find parent */
			struct vy_range *parent = si_trackget(track, n->self.id.parent);
			if (parent) {
				/* schedule node for removal, if has incomplete merges */
				if (parent->recover & SI_RDB_DBI)
					n->recover |= SI_RDB_REMOVE;
				else
					parent->recover |= SI_RDB_REMOVE;
			}
			if (! (n->recover & SI_RDB_REMOVE)) {
				/* complete node */
				int rc = vy_range_complete(n, &i->conf);
				if (unlikely(rc == -1))
					return -1;
				n->recover = SI_RDB;
			}
			break;
		}
		default:
			/* corrupted states */
			return vy_error("corrupted index repository: %s",
					i->conf.path);
		}
		n = vy_range_id_tree_prev(&track->tree, n);
	}
	return 0;
}

static inline int
si_recovercomplete(struct sitrack *track, struct vy_env *env, struct vy_index *index, struct vy_buf *buf)
{
	/* prepare and build primary index */
	vy_buf_reset(buf);
	struct vy_range *n = vy_range_id_tree_first(&track->tree);
	while (n) {
		int rc = vy_buf_add(buf, &n, sizeof(struct vy_range*));
		if (unlikely(rc == -1))
			return -1;
		n = vy_range_id_tree_next(&track->tree, n);
	}
	struct vy_bufiter i;
	vy_bufiter_open(&i, buf, sizeof(struct vy_range*));
	while (vy_bufiter_has(&i))
	{
		struct vy_range *n = vy_bufiterref_get(&i);
		if (n->recover & SI_RDB_REMOVE) {
			int rc = vy_range_free(n, env, 1);
			if (unlikely(rc == -1))
				return -1;
			vy_bufiter_next(&i);
			continue;
		}
		n->recover = SI_RDB;
		si_insert(index, n);
		vy_planner_update(&index->p, n);
		vy_bufiter_next(&i);
	}
	return 0;
}

static inline void
si_recoversize(struct vy_index *i)
{
	struct vy_range *n = vy_range_tree_first(&i->tree);
	while (n) {
		i->size += vy_range_size(n);
		n = vy_range_tree_next(&i->tree, n);
	}
}

static inline int
si_recoverindex(struct vy_index *index, struct vy_env *env)
{
	struct sitrack track;
	si_trackinit(&track);
	struct vy_buf buf;
	vy_buf_init(&buf);
	int rc;
	rc = si_trackdir(&track, env, index);
	if (unlikely(rc == -1))
		goto error;
	if (unlikely(track.count == 0))
		return 1;
	rc = si_trackvalidate(&track, &buf, index);
	if (unlikely(rc == -1))
		goto error;
	rc = si_recovercomplete(&track, env, index, &buf);
	if (unlikely(rc == -1))
		goto error;
	/* set actual metrics */
	if (track.nsn > env->seq->nsn)
		env->seq->nsn = track.nsn;
	if (track.lsn > env->seq->lsn)
		env->seq->lsn = track.lsn;
	si_recoversize(index);
	vy_buf_free(&buf);
	return 0;
error:
	vy_buf_free(&buf);
	si_trackfree(&track, env);
	return -1;
}

static inline int
si_recoverdrop(struct vy_index *i)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/drop", i->conf.path);
	int rc = path_exists(path);
	if (likely(! rc))
		return 0;
	rc = vy_index_droprepository(i->conf.path, 0);
	if (unlikely(rc == -1))
		return -1;
	return 1;
}

static int
si_recover(struct vy_index *i)
{
	struct vy_env *env = i->env;
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
	rc = si_recoverindex(i, env);
	if (likely(rc <= 0))
		return rc;
deploy:
	return si_deploy(i, env, !exist);
}

static void
vy_index_conf_init(struct vy_index_conf *s)
{
	memset(s, 0, sizeof(*s));
	sr_version(&s->version);
	sr_version_storage(&s->version_storage);
}

static void
vy_index_conf_free(struct vy_index_conf *s)
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

static struct txv *
si_write(logindex_t *logindex, struct txv *v, uint64_t time,
	 enum vinyl_status status, uint64_t lsn)
{
	struct vy_index *index = v->index;
	struct vy_env *env = index->env;
	struct rlist rangelist;
	size_t quota = 0;
	rlist_create(&rangelist);

	vy_index_lock(index);
	index->update_time = time;
	for (; v != NULL && v->index == index;
	     v = logindex_next(logindex, v)) {

		struct vy_tuple *tuple = v->tuple;
		tuple->lsn = lsn;

		if ((status == VINYL_FINAL_RECOVERY &&
		     si_readcommited(index, tuple))) {

			continue;
		}
		/* match node */
		struct vy_rangeiter ii;
		vy_rangeiter_open(&ii, index, VINYL_GE, tuple->data, tuple->size);
		struct vy_range *range = vy_rangeiter_get(&ii);
		assert(range != NULL);
		struct svref ref;
		vy_tuple_ref(tuple);
		ref.v = tuple;
		ref.flags = 0;
		/* insert into node index */
		struct svindex *vindex = vy_range_index(range);
		int rc = sv_indexset(vindex, ref);
		assert(rc == 0); /* TODO: handle BPS tree errors properly */
		(void) rc;
		/* update node */
		range->used += vy_tuple_size(tuple);
		quota += vy_tuple_size(tuple);
		if (rlist_empty(&range->commit))
			rlist_add(&rangelist, &range->commit);
	}
	/* reschedule nodes */
	struct vy_range *range, *tmp;
	rlist_foreach_entry_safe(range, &rangelist, commit, tmp) {
		range->update_time = index->update_time;
		rlist_create(&range->commit);
		vy_planner_update_range(&index->p, range);
	}
	vy_index_unlock(index);
	/* Take quota after having unlocked the index mutex. */
	vy_quota_op(env->quota, VINYL_QADD, quota);
	return v;
}

/* {{{ Scheduler */

struct vy_scheduler {
	pthread_mutex_t        lock;
	uint64_t       checkpoint_lsn_last;
	uint64_t       checkpoint_lsn;
	bool checkpoint_in_progress;
	bool age_in_progress;
	uint64_t       age_time;
	uint64_t       gc_time;
	bool gc_in_progress;
	int            rr;
	int            count;
	struct vy_index **indexes;
	struct rlist   shutdown;
	struct vy_env    *env;
	struct cord *worker_pool;
	int worker_pool_size;
	volatile int worker_pool_run;
};

static void
vy_workers_start(struct vy_scheduler *scheduler);
static void
vy_workers_stop(struct vy_scheduler *scheduler);

static struct vy_scheduler *
vy_scheduler_new(struct vy_env *env)
{
	struct vy_scheduler *scheduler = calloc(1, sizeof(*scheduler));
	if (scheduler == NULL) {
		diag_set(OutOfMemory, sizeof(*scheduler), "scheduler",
			 "struct");
		return NULL;
	}
	uint64_t now = ev_now(loop());
	tt_pthread_mutex_init(&scheduler->lock, NULL);
	scheduler->checkpoint_lsn = 0;
	scheduler->checkpoint_lsn_last = 0;
	scheduler->checkpoint_in_progress = false;
	scheduler->age_in_progress = false;
	scheduler->age_time = now;
	scheduler->gc_in_progress = false;
	scheduler->gc_time = now;
	scheduler->indexes = NULL;
	scheduler->count = 0;
	scheduler->rr = 0;
	scheduler->env = env;
	rlist_create(&scheduler->shutdown);
	return scheduler;
}

static void
vy_scheduler_delete(struct vy_scheduler *scheduler)
{
	if (scheduler->worker_pool_run)
		vy_workers_stop(scheduler);
	struct vy_index *index, *next;
	rlist_foreach_entry_safe(index, &scheduler->shutdown, link, next) {
		vy_index_delete(index);
	}
	free(scheduler->indexes);
	free(scheduler);
}

static int
vy_scheduler_add_index(struct vy_scheduler *scheduler, struct vy_index *index)
{
	tt_pthread_mutex_lock(&scheduler->lock);
	struct vy_index **indexes = realloc(scheduler->indexes,
		(scheduler->count + 1) * sizeof(*indexes));
	if (unlikely(indexes == NULL)) {
		diag_set(OutOfMemory, sizeof((scheduler->count + 1) *
			 sizeof(*indexes)), "scheduler", "indexes");
		tt_pthread_mutex_unlock(&scheduler->lock);
		return -1;
	}
	scheduler->indexes = indexes;
	scheduler->indexes[scheduler->count++] = index;
	vy_index_ref(index);
	tt_pthread_mutex_unlock(&scheduler->lock);
	/* Start scheduler threads on demand */
	if (!scheduler->worker_pool_run)
		vy_workers_start(scheduler);
	return 0;
}

static int
vy_scheduler_del_index(struct vy_scheduler *scheduler, struct vy_index *index)
{
	tt_pthread_mutex_lock(&scheduler->lock);
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
	tt_pthread_mutex_unlock(&scheduler->lock);
	return 0;
}

static struct vy_index *
vy_scheduler_peek_index(struct vy_scheduler *scheduler)
{
	if (scheduler->count == 0)
		return NULL;
	assert(scheduler->rr < scheduler->count);
	struct vy_index *index = scheduler->indexes[scheduler->rr];
	scheduler->rr = (scheduler->rr + 1) % scheduler->count;
	return index;
}

static int
vy_plan_index(struct vy_scheduler *scheduler, struct srzone *zone,
	      uint64_t vlsn, struct vy_index *index, struct vy_task *task)
{
	int rc;

	/* node gc */
	rc = vy_planner_peek_nodegc(index, task);
	if (rc != 0)
		return rc; /* found or error */

	/* checkpoint */
	if (scheduler->checkpoint_in_progress) {
		rc = vy_planner_peek_checkpoint(index,
			scheduler->checkpoint_lsn, task);
		if (rc != 0)
			return rc; /* found or error */
	}

	/* garbage-collection */
	if (scheduler->gc_in_progress) {
		rc = vy_planner_peek_gc(index, vlsn, zone->gc_wm, task);
		if (rc != 0)
			return rc; /* found or error */
	}

	/* index aging */
	if (scheduler->age_in_progress) {
		uint32_t ttl = zone->dump_age * 1000000; /* ms */
		uint32_t ttl_wm = zone->dump_age_wm;
		rc = vy_planner_peek_age(index, ttl, ttl_wm, task);
		if (rc != 0)
			return rc; /* found or error */
	}

	/* dumping */
	rc = vy_planner_peek_dump(index, zone->dump_wm, task);
	if (rc != 0)
		return rc; /* found or error */

	/* compaction */
	rc = vy_planner_peek_compact(index, zone->compact_wm, task);
	if (rc != 0)
		return rc; /* found or error */

	return 0; /* nothing to do */
}

static int
vy_plan(struct vy_scheduler *scheduler, struct srzone *zone, uint64_t vlsn,
	struct vy_task *task)
{
	/* pending shutdowns */
	struct vy_index *index, *n;
	rlist_foreach_entry_safe(index, &scheduler->shutdown, link, n) {
		vy_index_lock(index);
		int rc = vy_planner_peek_shutdown(index, task);
		vy_index_unlock(index);
		if (rc == 0)
			continue;
		/* delete from scheduler->shutdown list */
		rlist_del(&index->link);
		return 1;
	}

	/* peek an index */
	index = vy_scheduler_peek_index(scheduler);
	if (index == NULL)
		return 0; /* nothing to do */

	vy_index_lock(index);
	int rc = vy_plan_index(scheduler, zone, vlsn, index, task);
	vy_index_unlock(index);
	return rc;
}

static int
vy_schedule(struct vy_scheduler *scheduler, struct sdc *sdc)
{
	struct vy_env *env = scheduler->env;
	int64_t vlsn = vy_sequence(env->seq, VINYL_VIEW_LSN);
	uint64_t now = clock_monotonic64();
	struct srzone *zone = sr_zoneof(env);
	int rc;
	tt_pthread_mutex_lock(&scheduler->lock);

	if (scheduler->age_in_progress) {
		/* Stop periodic aging */
		bool age_in_progress = false;
		for (int i = 0; i < scheduler->count; i++) {
			if (scheduler->indexes[i]->age_in_progress) {
				age_in_progress = true;
				break;
			}
		}
		if (!age_in_progress) {
			scheduler->age_in_progress = false;
			scheduler->age_time = now;
		}
	} else if (zone->dump_prio && zone->dump_age_period &&
		   (now - scheduler->age_time) >= zone->dump_age_period_us &&
		   scheduler->count > 0) {
		/* Start periodic aging */
		scheduler->age_in_progress = true;
		for (int i = 0; i < scheduler->count; i++) {
			scheduler->indexes[i]->age_in_progress = true;
		}
	}

	if (scheduler->gc_in_progress) {
		/* Stop periodic GC */
		bool gc_in_progress = false;
		for (int i = 0; i < scheduler->count; i++) {
			if (scheduler->indexes[i]->gc_in_progress) {
				gc_in_progress = true;
				break;
			}
		}
		if (!gc_in_progress) {
			scheduler->gc_in_progress = false;
			scheduler->gc_time = now;
		}
	} else if (zone->gc_prio && zone->gc_period &&
		   ((now - scheduler->gc_time) >= zone->gc_period_us) &&
		   scheduler->count > 0) {
		/* Start periodic GC */
		scheduler->gc_in_progress = true;
		for (int i = 0; i < scheduler->count; i++) {
			scheduler->indexes[i]->gc_in_progress = true;
		}
	}

	if (scheduler->checkpoint_in_progress) {
		bool checkpoint_in_progress = false;
		for (int i = 0; i < scheduler->count; i++) {
			if (scheduler->indexes[i]->checkpoint_in_progress) {
				checkpoint_in_progress = true;
				break;
			}
		}
		if (!checkpoint_in_progress) {
			scheduler->checkpoint_in_progress = false;
			scheduler->checkpoint_lsn_last =
				scheduler->checkpoint_lsn;
			scheduler->checkpoint_lsn = 0;
		}
	}

	/* Get task */
	struct vy_task task;
	rc = vy_plan(scheduler, zone, vlsn, &task);
	tt_pthread_mutex_unlock(&scheduler->lock);
	if (rc < 0) {
		return -1; /* error */
	} else if (rc == 0) {
		 return 0; /* nothing to do */
	}

	/* Execute task */
	rc = vy_task_execute(&task, sdc, vlsn);

	/* Delete task */
	vy_task_destroy(&task);

	if (unlikely(rc == -1))
		return -1; /* error */
	return 1; /* success */
}

static void*
vy_worker(void *arg)
{
	struct vy_scheduler *scheduler = (struct vy_scheduler *) arg;
	struct sdc sdc;
	sd_cinit(&sdc);
	while (pm_atomic_load_explicit(&scheduler->worker_pool_run,
				       pm_memory_order_relaxed)) {
		int rc = vy_schedule(scheduler, &sdc);
		if (rc == -1)
			break;
		if (rc == 0)
			usleep(10000); /* 10ms */
	}
	sd_cfree(&sdc);
	return NULL;
}

static void
vy_workers_start(struct vy_scheduler *scheduler)
{
	assert(!scheduler->worker_pool_run);
	/* prepare worker pool */
	scheduler->worker_pool = NULL;
	scheduler->worker_pool_size = cfg_geti("vinyl.threads");
	if (scheduler->worker_pool_size < 0)
		scheduler->worker_pool_size = 1;
	scheduler->worker_pool = (struct cord *)
		calloc(scheduler->worker_pool_size, sizeof(struct cord));
	if (scheduler->worker_pool == NULL)
		panic("failed to allocate vinyl worker pool");
	scheduler->worker_pool_run = 1;
	for (int i = 0; i < scheduler->worker_pool_size; i++) {
		cord_start(&scheduler->worker_pool[i], "vinyl", vy_worker,
			   scheduler);
	}
}

static void
vy_workers_stop(struct vy_scheduler *scheduler)
{
	assert(scheduler->worker_pool_run);
	pm_atomic_store_explicit(&scheduler->worker_pool_run, 0,
				 pm_memory_order_relaxed);
	for (int i = 0; i < scheduler->worker_pool_size; i++)
		cord_join(&scheduler->worker_pool[i]);
	free(scheduler->worker_pool);
	scheduler->worker_pool = NULL;
	scheduler->worker_pool_size = 0;
}

int
vy_checkpoint(struct vy_env *env)
{
	struct vy_scheduler *scheduler = env->scheduler;
	/* do not initiate checkpoint during bootstrap,
	 * thread pool is not up yet */
	if (!scheduler->worker_pool_run)
		return 0;
	tt_pthread_mutex_lock(&scheduler->lock);
	uint64_t lsn = vy_sequence(env->seq, VINYL_LSN);
	scheduler->checkpoint_lsn = lsn;
	scheduler->checkpoint_in_progress = true;
	for (int i = 0; i < scheduler->count; i++) {
		scheduler->indexes[i]->checkpoint_in_progress = true;
	}
	tt_pthread_mutex_unlock(&scheduler->lock);
	return 0;
}

void
vy_wait_checkpoint(struct vy_env *env)
{
	struct vy_scheduler *scheduler = env->scheduler;
	for (;;) {
		tt_pthread_mutex_lock(&scheduler->lock);
		bool is_active = scheduler->checkpoint_in_progress;
		tt_pthread_mutex_unlock(&scheduler->lock);
		if (!is_active)
			break;
		fiber_sleep(.020);
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
		.enable            = 1,
		.compact_wm        = 2,
		.dump_prio       = 1,
		.dump_wm         = 10 * 1024 * 1024,
		.dump_age        = 40,
		.dump_age_period = 40,
		.dump_age_wm     = 1 * 1024 * 1024,
		.gc_prio           = 1,
		.gc_period         = 60,
		.gc_wm             = 30,
	};
	struct srzone redzone = {
		.enable            = 1,
		.compact_wm        = 4,
		.dump_prio       = 0,
		.dump_wm         = 0,
		.dump_age        = 0,
		.dump_age_period = 0,
		.dump_age_wm     = 0,
		.gc_prio           = 0,
		.gc_period         = 0,
		.gc_wm             = 0,
	};
	sr_zonemap_set(&conf->zones, 0, &def);
	sr_zonemap_set(&conf->zones, 80, &redzone);
	/* configure zone = 0 */
	struct srzone *z = &conf->zones.zones[0];
	assert(z->enable);
	z->compact_wm = cfg_geti("vinyl.compact_wm");
	if (z->compact_wm <= 1) {
		vy_error("bad %d.compact_wm value", 0);
		goto error_2;
	}
	z->dump_prio = cfg_geti("vinyl.dump_prio");
	z->dump_age = cfg_geti("vinyl.dump_age");
	z->dump_age_period = cfg_geti("vinyl.dump_age_period");
	z->dump_age_wm = cfg_geti("vinyl.dump_age_wm");

	/* convert periodic times from sec to usec */
	for (int i = 0; i < 11; i++) {
		z = &conf->zones.zones[i];
		z->dump_age_period_us = z->dump_age_period * 1000000;
		z->gc_period_us         = z->gc_period * 1000000;
	}
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
		struct vy_tuple **, struct vy_tuple *,
		struct vy_tx*, struct sicache*, bool cache_only);
static int vy_index_recoverbegin(struct vy_index*);
static int vy_index_recoverend(struct vy_index*);

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
	vy_info_append_str(node, "version", VINYL_VERSION);
	vy_info_append_str(node, "version_storage", VINYL_VERSION_STORAGE);
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
		struct srzone *z = &env->conf->zones.zones[i];
		if (!z->enable)
			continue;
		++childs_cnt;
	}
	struct vy_info_node *node = vy_info_append(root, "compaction");
	if (vy_info_reserve(info, node, childs_cnt) != 0)
		return 1;
	for (int i = 0; i < 11; ++i) {
		struct srzone *z = &env->conf->zones.zones[i];
		if (!z->enable)
			continue;

		struct vy_info_node *local_node = vy_info_append(node, z->name);
		if (vy_info_reserve(info, local_node, 13) != 0)
			return 1;
		vy_info_append_u32(local_node, "gc_wm", z->gc_wm);
		vy_info_append_u32(local_node, "gc_prio", z->gc_prio);
		vy_info_append_u32(local_node, "dump_wm", z->dump_wm);
		vy_info_append_u32(local_node, "gc_period", z->gc_period);
		vy_info_append_u32(local_node, "dump_age", z->dump_age);
		vy_info_append_u32(local_node, "compact_wm", z->compact_wm);
		vy_info_append_u32(local_node, "dump_prio", z->dump_prio);
		vy_info_append_u32(local_node, "dump_age_wm", z->dump_age_wm);
		vy_info_append_u32(local_node, "dump_age_period", z->dump_age_period);
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

	struct vy_scheduler *scheduler = env->scheduler;
	tt_pthread_mutex_lock(&scheduler->lock);
	vy_info_append_u32(node, "gc_active", scheduler->gc_in_progress);
	tt_pthread_mutex_unlock(&scheduler->lock);
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
	tt_pthread_mutex_lock(&stat->lock);
	vy_info_append_u64(node, "tx", stat->tx);
	vy_info_append_u64(node, "set", stat->set);
	vy_info_append_u64(node, "get", stat->get);
	vy_info_append_u64(node, "delete", stat->del);
	vy_info_append_u64(node, "upsert", stat->upsert);
	vy_info_append_u64(node, "cursor", stat->cursor);
	vy_info_append_str(node, "tx_ops", stat->tx_stmts.sz);
	vy_info_append_u64(node, "documents", stat->v_count);
	vy_info_append_str(node, "tx_latency", stat->tx_latency.sz);
	vy_info_append_str(node, "cursor_ops", stat->cursor_ops.sz);
	vy_info_append_str(node, "get_latency", stat->get_latency.sz);
	vy_info_append_str(node, "set_latency", stat->set_latency.sz);
	vy_info_append_u64(node, "tx_rollback", stat->tx_rlb);
	vy_info_append_u64(node, "tx_conflict", stat->tx_conflict);
	vy_info_append_u32(node, "tx_active_rw", env->xm->count_rw);
	vy_info_append_u32(node, "tx_active_ro", env->xm->count_rd);
	vy_info_append_str(node, "get_read_disk", stat->get_read_disk.sz);
	vy_info_append_u64(node, "documents_used", stat->v_allocated);
	vy_info_append_str(node, "delete_latency", stat->del_latency.sz);
	vy_info_append_str(node, "upsert_latency", stat->upsert_latency.sz);
	vy_info_append_str(node, "get_read_cache", stat->get_read_cache.sz);
	vy_info_append_str(node, "cursor_latency", stat->cursor_latency.sz);
	tt_pthread_mutex_unlock(&stat->lock);
	return 0;
}

static inline int
vy_info_append_metric(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_info_node *node = vy_info_append(root, "metric");
	if (vy_info_reserve(info, node, 2) != 0)
		return 1;

	struct vy_sequence *seq = info->env->seq;
	vy_sequence_lock(seq);
	vy_info_append_u64(node, "lsn", seq->lsn);
	vy_info_append_u64(node, "nsn", seq->nsn);
	vy_sequence_unlock(seq);
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
		struct vy_info_node *local_node =
			vy_info_append(node, o->conf.name);
		if (vy_info_reserve(info, local_node, 17) != 0)
			return 1;
		vy_info_append_u64(local_node, "size", o->rtp.total_node_size);
		vy_info_append_u64(local_node, "count", o->rtp.count);
		vy_info_append_u64(local_node, "count_dup", o->rtp.count_dup);
		vy_info_append_u64(local_node, "read_disk", o->rtp.read_disk);
		vy_info_append_u32(local_node, "page_count", o->rtp.total_page_count);
		vy_info_append_u64(local_node, "read_cache", o->rtp.read_cache);
		vy_info_append_u32(local_node, "node_count", o->rtp.total_node_count);
		vy_info_append_u32(local_node, "run_avg", o->rtp.total_run_avg);
		vy_info_append_u32(local_node, "run_max", o->rtp.total_run_max);
		vy_info_append_u64(local_node, "memory_used", o->rtp.memory_used);
		vy_info_append_u32(local_node, "run_count", o->rtp.total_run_count);
		vy_info_append_u32(local_node, "temperature_avg", o->rtp.temperature_avg);
		vy_info_append_u32(local_node, "temperature_min", o->rtp.temperature_min);
		vy_info_append_u32(local_node, "temperature_max", o->rtp.temperature_max);
		vy_info_append_str(local_node, "run_histogram", o->rtp.histogram_run_ptr);
		vy_info_append_u64(local_node, "size_uncompressed", o->rtp.total_node_origin_size);
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

struct vy_cursor {
	struct vy_index *index;
	struct vy_tuple *key;
	enum vy_order order;
	struct vy_tx tx;
	int ops;
	struct sicache *cache;
};

struct vy_cursor *
vy_cursor_new(struct vy_index *index, const char *key,
		 uint32_t part_count, enum vy_order order)
{
	struct vy_env *e = index->env;
	struct vy_cursor *c = mempool_alloc(&e->cursor_pool);
	if (unlikely(c == NULL)) {
		diag_set(OutOfMemory, sizeof(*c), "cursor", "struct");
		return NULL;
	}
	vy_index_ref(index);
	c->index = index;
	c->ops = 0;
	c->key = vy_tuple_from_key_data(index, key, part_count);
	if (c->key == NULL)
		goto error_1;
	c->order = order;
	c->cache = vy_cachepool_pop(e->cachepool);
	if (unlikely(c->cache == NULL))
		goto error_2;

	tx_begin(e->xm, &c->tx, VINYL_TX_RO);
	return c;

error_2:
	vy_tuple_unref(index, c->key);
error_1:
	vy_index_unref(index);
	mempool_free(&e->cursor_pool, c);
	return NULL;
}

void
vy_cursor_delete(struct vy_cursor *c)
{
	struct vy_env *e = c->index->env;
	tx_rollback(&c->tx);
	if (c->cache)
		vy_cachepool_push(c->cache);
	if (c->key)
		vy_tuple_unref(c->index, c->key);
	vy_index_unref(c->index);
	vy_stat_cursor(e->stat, c->tx.start, c->ops);
	TRASH(c);
	mempool_free(&e->cursor_pool, c);
}

/*** }}} Cursor */

static int
vy_index_conf_create(struct vy_index_conf *conf, struct key_def *key_def)
{
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 "/%" PRIu32,
	         key_def->space_id, key_def->iid);
	conf->name = strdup(name);
	if (conf->name == NULL) {
		diag_set(OutOfMemory, strlen(name),
			 "strdup", "char *");
		goto error;
	}
	conf->sync = cfg_geti("vinyl.sync");

	/* compression */
	if (key_def->opts.compression[0] != '\0' &&
	    strcmp(key_def->opts.compression, "none")) {
		conf->compression_if = vy_filter_of(key_def->opts.compression);
		if (conf->compression_if == NULL) {
			vy_error("unknown compression type '%s'",
				 key_def->opts.compression);
			goto error;
		}
		conf->compression_sz = strdup(conf->compression_if->name);
		if (conf->compression_sz == NULL) {
			diag_set(OutOfMemory,
				 strlen(conf->compression_if->name), "strdup",
				 "char *");
			goto error;
		}
		conf->compression = 1;
	} else {
		conf->compression = 0;
		conf->compression_if = NULL;
		conf->compression_sz = strdup("none");
		if (conf->compression_sz == NULL) {
			diag_set(OutOfMemory, strlen("none"), "strdup",
				 "char *");
			goto error;
		}
	}

	/* path */
	if (key_def->opts.path[0] == '\0') {
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", cfg_gets("vinyl_dir"),
			 conf->name);
		conf->path = strdup(path);
		if (conf->path == NULL) {
			diag_set(OutOfMemory, strlen(path), "strdup",
				 "char *");
			goto error;
		}
	} else {
		conf->path = strdup(key_def->opts.path);
		if (conf->path == NULL) {
			diag_set(OutOfMemory, strlen(key_def->opts.path),
				"strdup", "char *");
			goto error;
		}
	}
	conf->buf_gc_wm = 1024 * 1024;

	return 0;
error:
	return -1;
}

int
vy_index_open(struct vy_index *index)
{
	struct vy_env *e = index->env;
	int status = vy_status(&index->status);
	if (status == VINYL_FINAL_RECOVERY)
		goto online;
	if (status != VINYL_OFFLINE)
		return -1;
	int rc = vy_index_recoverbegin(index);
	if (unlikely(rc == -1))
		return -1;

online:
	vy_index_recoverend(index);
	rc = vy_scheduler_add_index(e->scheduler, index);
	if (unlikely(rc == -1))
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
	struct vy_env *e = index->env;
	int status = vy_status(&index->status);
	if (unlikely(! vy_status_is_active(status)))
		return -1;
	int rc = vy_index_dropmark(index);
	if (unlikely(rc == -1))
		return -1;
	/* set last visible transaction id */
	vy_status_set(&index->status, VINYL_DROP);
	rlist_del(&index->link);
	/* schedule index shutdown or drop */
	vy_scheduler_del_index(e->scheduler, index);
	return 0;
}

int
vy_index_read(struct vy_index *index, struct vy_tuple *key,
	      enum vy_order order, struct vy_tuple **result,
	      struct vy_tuple *upsert, struct vy_tx *tx,
	      struct sicache *cache, bool cache_only)
{
	struct vy_env *e = index->env;
	uint64_t start  = clock_monotonic64();

	if (! vy_status_online(&index->status)) {
		vy_error("%s", "index is not online");
		return -1;
	}

	/* concurrent */
	if (cache_only && tx != NULL && order == VINYL_EQ) {
		assert(upsert == NULL);
		if ((*result = vy_tx_get(tx, index, key)))
			return 0;
		/* Tuple not found. */
	}

	/* prepare read cache */
	int cachegc = 0;
	if (cache == NULL) {
		cachegc = 1;
		cache = vy_cachepool_pop(e->cachepool);
		if (unlikely(cache == NULL))
			return -1;
	}

	int64_t vlsn;
	if (tx) {
		vlsn = tx->vlsn;
	} else {
		vlsn = vy_sequence(e->scheduler->env->seq, VINYL_LSN);
	}

	int upsert_eq = 0;
	if (order == VINYL_EQ) {
		order = VINYL_GE;
		upsert_eq = 1;
	}

	/* read index */
	struct siread q;
	if (vy_tuple_key_part(key->data, 0) == NULL) {
		/* key is [] */
		si_readopen(&q, index, cache, order, vlsn, NULL, 0);
	} else {
		si_readopen(&q, index, cache, order, vlsn, key->data, key->size);
	}
	struct sv sv_vup;
	if (upsert != NULL) {
		sv_from_tuple(&sv_vup, upsert);
		q.upsert_v = &sv_vup;
	}
	q.upsert_eq = upsert_eq;
	q.cache_only = cache_only;
	assert(q.order != VINYL_EQ);
	int rc = si_range(&q);
	si_readclose(&q);

	/* free read cache */
	if (likely(cachegc))
		vy_cachepool_push(cache);
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
	struct vy_stat_get statget;
	statget.read_disk = q.read_disk;
	statget.read_cache = q.read_cache;
	statget.read_latency = clock_monotonic64() - start;
	vy_stat_get(e->stat, &statget);

	*result = q.result;
	return 0;
}

struct vy_index *
vy_index_new(struct vy_env *e, struct key_def *key_def,
		struct tuple_format *tuple_format)
{
	assert(key_def->part_count > 0);
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 "/%" PRIu32,
	         key_def->space_id, key_def->iid);
	struct vy_index *dup = NULL;
	struct vy_index *index;
	rlist_foreach_entry(index, &e->indexes, link) {
		if (strcmp(index->conf.name, name) == 0) {
			dup = index;
			break;
		}
	}
	if (unlikely(dup)) {
		vy_error("index '%s' already exists", name);
		return NULL;
	}
	index = malloc(sizeof(struct vy_index));
	if (unlikely(index == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_index),
			 "malloc", "struct vy_index");
		return NULL;
	}
	memset(index, 0, sizeof(*index));
	index->env = e;
	vy_status_init(&index->status);
	int rc = vy_planner_init(&index->p);
	if (unlikely(rc == -1))
		goto error_1;
	index->checkpoint_in_progress = false;
	index->gc_in_progress = false;
	index->age_in_progress = false;
	vy_index_conf_init(&index->conf);
	if (vy_index_conf_create(&index->conf, key_def))
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

	vy_buf_init(&index->readbuf);
	vy_range_tree_new(&index->tree);
	tt_pthread_mutex_init(&index->lock, NULL);
	rlist_create(&index->link);
	rlist_create(&index->gc);
	index->update_time = 0;
	index->size = 0;
	index->read_disk = 0;
	index->read_cache = 0;
	index->range_count = 0;
	tt_pthread_mutex_init(&index->ref_lock, NULL);
	index->refs = 0; /* referenced by scheduler */
	vy_status_set(&index->status, VINYL_OFFLINE);
	txvindex_new(&index->txvindex);
	rlist_add(&e->indexes, &index->link);
	return index;

error_4:
	tuple_format_ref(index->tuple_format, -1);
	key_def_delete(index->key_def);
error_3:
	vy_index_conf_free(&index->conf);
error_2:
	vy_planner_free(&index->p);
error_1:
	free(index);
	return NULL;
}

static inline void
vy_index_delete(struct vy_index *index)
{
	struct vy_env *e = index->env;
	txvindex_iter(&index->txvindex, NULL, txvindex_delete_cb, e);
	rlist_create(&index->gc);
	vy_range_tree_iter(&index->tree, NULL, vy_range_tree_free_cb, index->env);
	vy_buf_free(&index->readbuf);
	vy_planner_free(&index->p);
	tt_pthread_mutex_destroy(&index->lock);
	tt_pthread_mutex_destroy(&index->ref_lock);
	vy_status_free(&index->status);
	vy_index_conf_free(&index->conf);
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

static int vy_index_recoverbegin(struct vy_index *index)
{
	/* open and recover repository */
	vy_status_set(&index->status, VINYL_FINAL_RECOVERY);
	/* do not allow to recover existing indexes
	 * during online (only create), since logpool
	 * reply is required. */
	int rc = si_recover(index);
	if (unlikely(rc == -1))
		goto error;
	return 0;
error:
	vy_status_set(&index->status, VINYL_MALFUNCTION);
	return -1;
}

static int vy_index_recoverend(struct vy_index *index)
{
	int status = vy_status(&index->status);
	if (unlikely(status == VINYL_DROP))
		return 0;
	vy_status_set(&index->status, VINYL_ONLINE);
	return 0;
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
vy_tuple_alloc(struct vy_index *index, uint32_t size)
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
	/* update runtime statistics */
	struct vy_env *env = index->env;
	tt_pthread_mutex_lock(&env->stat->lock);
	env->stat->v_count++;
	env->stat->v_allocated += sizeof(struct vy_tuple) + size;
	tt_pthread_mutex_unlock(&env->stat->lock);
	return v;
}

static struct vy_tuple *
vy_tuple_from_key_data(struct vy_index *index, const char *key,
			 uint32_t part_count)
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
	struct vy_tuple *tuple = vy_tuple_alloc(index, size);
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
	for (uint32_t i = part_count; i <= key_def->part_count; i++)
		offsets[i] = VY_TUPLE_KEY_MISSING; /* part is missing */

	/* Copy MsgPack data */
	char *data = tuple->data + offsets_size;
	data = mp_encode_array(data, part_count);
	memcpy(data, key, key_size);
	data += key_size;
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
	struct vy_tuple *tuple = vy_tuple_alloc(index, size);
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
vinyl_convert_tuple(struct vy_index *index, struct vy_tuple *vy_tuple)
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

static int
vy_tuple_unref_rt(struct vy_stat *stat, struct vy_tuple *v)
{
	uint16_t old_refs = pm_atomic_fetch_sub_explicit(&v->refs, 1,
		pm_memory_order_relaxed);
	assert(old_refs > 0);
	if (likely(old_refs == 1)) {
		uint32_t size = vy_tuple_size(v);
		/* update runtime statistics */
		tt_pthread_mutex_lock(&stat->lock);
		assert(stat->v_count > 0);
		assert(stat->v_allocated >= size);
		stat->v_count--;
		stat->v_allocated -= size;
		tt_pthread_mutex_unlock(&stat->lock);
#ifndef NDEBUG
		memset(v, '#', vy_tuple_size(v)); /* fail early */
#endif
		free(v);
		return 1;
	}
	return 0;
}

static void
vy_tuple_unref(struct vy_index *index, struct vy_tuple *tuple)
{
	struct vy_stat *stat = index->env->stat;
	vy_tuple_unref_rt(stat, tuple);
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
vinyl_update_alloc(void *arg, size_t size)
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
	uint64_t series_count = mp_decode_uint(&ops);
	assert(series_count > 0);
	(void)ops_end;
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
		result = tuple_upsert_execute(vinyl_update_alloc, NULL,
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
vy_apply_upsert(struct sv *new_tuple, struct sv *old_tuple,
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
	const char *new_data = sv_pointer(new_tuple);
	const char *new_data_end = new_data + sv_size(new_tuple);
	const char *new_mp, *new_mp_end, *new_ops, *new_ops_end;
	vy_tuple_data_ex(key_def, new_data, new_data_end,
			    &new_mp, &new_mp_end,
			    &new_ops, &new_ops_end);
	if (old_tuple == NULL || (sv_flags(old_tuple) & SVDELETE)) {
		/*
		 * INSERT case: return new tuple.
		 */
		return vy_tuple_from_data(index, new_mp, new_mp_end);
	}

	/*
	 * Unpack UPSERT operation from the old tuple
	 */
	assert(old_tuple != NULL);
	const char *old_data = sv_pointer(old_tuple);
	const char *old_data_end = old_data + sv_size(old_tuple);
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
	if (!(sv_flags(old_tuple) & SVUPSERT)) {
		/*
		 * UPDATE case: return the updated old tuple.
		 */
		assert(old_ops_end - old_ops == 0);
		result_tuple = vy_tuple_from_data(index, result_mp,
						     result_mp_end);
		fiber_gc(); /* free memory allocated by tuple_upsert_execute */
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
	fiber_gc(); /* free memory allocated by tuple_upsert_execute */
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
		 * return the old tuple.
		 */
		diag_set(ClientError, ER_CANT_UPDATE_PRIMARY_KEY,
			 key_def->name, space_name_by_id(key_def->space_id));
		error_log(diag_last_error(diag_get()));
		vy_tuple_unref(index, result_tuple);
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
	struct txv *old = logindex_search_key(&tx->logindex, index,
					      tuple->data);
	/* Found a match of the previous action of this transaction */
	if (old != NULL) {
		vy_tuple_unref_rt(tx->manager->env->stat, old->tuple);
		vy_tuple_ref(tuple);
		old->tuple = tuple;
	} else {
		/* Allocate a MVCC container. */
		struct txv *v = txv_new(index, tuple, tx);
		logindex_insert(&tx->logindex, v);
		stailq_add_tail_entry(&tx->log, v, next_in_log);
	}
}

static void
vy_tx_delete(struct vy_stat *stat, struct vy_tx *tx, int rlb, int conflict)
{
	uint32_t count = 0;
	struct txv *v, *tmp;
	stailq_foreach_entry_safe(v, tmp, &tx->log, next_in_log) {
		count++;
		if (vy_tuple_is_r(v->tuple))
			txvindex_remove(&v->index->txvindex, v);
		/* Don't touch logindex, we're deleting all keys. */
		txv_delete(stat, v);
	}
	vy_stat_tx(stat, tx->start, count, rlb, conflict);
	free(tx);
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
	vy_tuple_unref(index, vytuple);
	return 0;
}

int
vy_upsert(struct vy_tx *tx, struct vy_index *index,
	  const char *tuple, const char *tuple_end,
	  const char *expr, const char *expr_end, int index_base)
{
	assert(index_base == 0 || index_base == 1);
	uint32_t extra_size = (expr_end - expr) +
			      mp_sizeof_uint(1) + mp_sizeof_uint(index_base);
	char *extra;
	struct vy_tuple *vy_tuple =
		vy_tuple_from_data_ex(index, tuple, tuple_end,
					 extra_size, &extra);
	if (vy_tuple == NULL) {
		return -1;
	}
	extra = mp_encode_uint(extra, 1); /* 1 upsert ops record */
	extra = mp_encode_uint(extra, index_base);
	memcpy(extra, expr, expr_end - expr);
	vy_tx_set(tx, index, vy_tuple, SVUPSERT);
	vy_tuple_unref(index, vy_tuple);
	return 0;
}

int
vy_delete(struct vy_tx *tx, struct vy_index *index,
	  const char *key, uint32_t part_count)
{
	struct vy_tuple *vykey =
		vy_tuple_from_key_data(index, key, part_count);
	if (vykey == NULL)
		return -1;
	vy_tx_set(tx, index, vykey, SVDELETE);
	vy_tuple_unref(index, vykey);
	return 0;
}

void
vy_rollback(struct vy_env *e, struct vy_tx *tx)
{
	tx_rollback(tx);
	vy_tx_delete(e->stat, tx, 1, 0);
}

int
vy_prepare(struct vy_env *e, struct vy_tx *tx)
{
	if (unlikely(! vy_status_is_active(e->status)))
		return -1;

	/* prepare transaction */
	assert(tx->state == VINYL_TX_READY);

	/* proceed read-only transactions */
	if (!vy_tx_is_ro(tx) && tx->is_aborted) {
		tx_promote(tx, VINYL_TX_ROLLBACK);
		diag_set(ClientError, ER_TRANSACTION_CONFLICT);
		return -1;
	}
	tx_promote(tx, VINYL_TX_COMMIT);

	struct txv *v = logindex_first(&tx->logindex);
	for (; v != NULL; v = logindex_next(&tx->logindex, v))
		txv_abort_all(tx, v);

	tx_manager_end(tx->manager, tx);
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
	if (unlikely(! vy_status_is_active(e->status)))
		return -1;
	assert(tx->state == VINYL_TX_COMMIT);

	vy_sequence_lock(e->seq);
	if (lsn > (int64_t) e->seq->lsn)
		e->seq->lsn = lsn;
	vy_sequence_unlock(e->seq);

	/* Flush transactional changes to the index. */
	uint64_t now = clock_monotonic64();
	struct txv *v = logindex_first(&tx->logindex);
	while (v != NULL)
		v = si_write(&tx->logindex, v, now, e->status, lsn);

	vy_tx_delete(e->stat, tx, 0, 0);
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
	tx_begin(e->xm, tx, VINYL_TX_RW);
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
	if (last == NULL) {
		last = stailq_first(&tx->log);
	} else {
		last = stailq_next(last);
	}
	if (last) {
		struct txv *v = NULL;
		if (last != NULL) {
			v = stailq_entry(last, struct txv, next_in_log);
			tx_rollback_svp(tx, v);
		}
	}
}

/* }}} Public API of transaction control */

/** {{{ vy_read_task - Asynchronous get/cursor I/O using eio pool */

/**
 * A context of asynchronous index get or cursor read.
 */
struct vy_read_task {
	struct coio_task base;
	struct vy_index *index;
	struct vy_cursor *cursor;
	struct vy_tx *tx;
	struct vy_tuple *key;
	struct vy_tuple *result;
	struct vy_tuple *upsert;
};

static ssize_t
vy_get_cb(struct coio_task *ptr)
{
	struct vy_read_task *task = (struct vy_read_task *) ptr;
	return vy_index_read(task->index, task->key, VINYL_EQ,
			     &task->result, task->upsert, task->tx, NULL,
			     false);
}

static ssize_t
vy_cursor_next_cb(struct coio_task *ptr)
{
	struct vy_read_task *task = (struct vy_read_task *) ptr;
	struct vy_cursor *c = task->cursor;
	return vy_index_read(c->index, c->key, c->order,
			     &task->result, task->upsert, &c->tx,
			     c->cache, false);
}

static ssize_t
vy_read_task_free_cb(struct coio_task *ptr)
{
	struct vy_read_task *task = (struct vy_read_task *) ptr;
	struct vy_env *env = task->index->env;
	assert(env != NULL);
	if (task->result != NULL)
		vy_tuple_unref(task->index, task->result);
	if (task->upsert != NULL)
		vy_tuple_unref(task->index, task->upsert);
	vy_index_unref(task->index);
	mempool_free(&env->read_task_pool, task);
	return 0;
}

/**
 * Create a thread pool task to run a callback,
 * execute the task, and return the result (tuple) back.
 */
static inline int
vy_read_task(struct vy_index *index, struct vy_tx *tx,
	     struct vy_cursor *cursor, struct vy_tuple *key,
	     struct vy_tuple **result, struct vy_tuple *upsert,
	     coio_task_cb func)
{
	assert(index != NULL);
	struct vy_env *env = index->env;
	assert(env != NULL);
	struct vy_read_task *task = mempool_alloc(&env->read_task_pool);
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task), "mempool", "vy_read_task");
		return -1;
	}
	task->index = index;
	vy_index_ref(index);
	task->tx = tx;
	task->cursor = cursor;
	task->key = key;
	task->result = NULL;
	task->upsert = upsert;
	if (coio_task(&task->base, func, vy_read_task_free_cb,
	              TIMEOUT_INFINITY) == -1) {
		return -1;
	}
	vy_index_unref(index);
	*result = task->result;
	int rc = task->base.base.result; /* save original error code */
	mempool_free(&env->read_task_pool, task);
	assert(rc == 0 || !diag_is_empty(&fiber()->diag));
	return rc;
}

/**
 * Find a tuple by key using a thread pool thread.
 */
int
vy_get(struct vy_tx *tx, struct vy_index *index, const char *key,
       uint32_t part_count, struct tuple **result)
{
	struct vy_tuple *vykey =
		vy_tuple_from_key_data(index, key, part_count);
	if (vykey == NULL)
		return -1;

	struct vy_tuple *vyresult = NULL;
	int rc = vy_index_read(index, vykey, VINYL_EQ, &vyresult,
			       NULL, tx, NULL, true);
	if (rc == 0) {
		if (vyresult == NULL || (vyresult->flags & SVUPSERT)) {
			/* cache miss or not found */
			rc = vy_read_task(index, tx, NULL, vykey,
					  &vyresult, vyresult, vy_get_cb);
		} else if (vy_tuple_is_not_found(vyresult)) {
			/*
			 * We deleted this tuple in this
			 * transaction. No need for a disk lookup.
			 */
			vy_tuple_unref(index, vyresult);
			vyresult = NULL;
		}
	}
	vy_tuple_unref(index, vykey);
	if (rc != 0)
		return -1;

	if (vyresult == NULL) { /* not found */
		*result = NULL;
		return 0;
	}

	/* found */
	*result = vinyl_convert_tuple(index, vyresult);
	vy_tuple_unref(index, vyresult);
	if (*result == NULL)
		return -1;
	return 0;
}

/**
 * Read the next value from a cursor in a thread pool thread.
 */
int
vy_cursor_next(struct vy_cursor *c, struct tuple **result)
{
	struct vy_tuple *vyresult = NULL;
	struct vy_index *index = c->index;
	struct vy_tx *tx = &c->tx;

	assert(c->key != NULL);
	if (vy_index_read(index, c->key, c->order, &vyresult, NULL,
			  tx, c->cache, true))
		return -1;
	c->ops++;
	if (vyresult == NULL || (vyresult->flags & SVUPSERT)) {
		/* Cache miss. */
		if (vy_read_task(index, NULL, c, NULL, &vyresult,
				 vyresult, vy_cursor_next_cb))
			return -1;
	} else if (vy_tuple_is_not_found(vyresult)) {
		/*
		 * We deleted this tuple in this
		 * transaction. No need for a disk lookup.
		 */
		vy_tuple_unref(index, vyresult);
		vyresult = NULL;
	}
	if (vyresult != NULL) {
		/* Found. */
		if (c->order == VINYL_GE)
			c->order = VINYL_GT;
		else if (c->order == VINYL_LE)
			c->order = VINYL_LT;

		vy_tuple_unref(index, c->key);
		c->key = vyresult;
		vy_tuple_ref(c->key);

		*result = vinyl_convert_tuple(index, vyresult);
		vy_tuple_unref(index, vyresult);
		if (*result == NULL)
			return -1;
	} else {
		/* Not found. */
		vy_tuple_unref(c->index, c->key);
		c->key = NULL;
		*result = NULL;
	}
	return 0;
}

/** }}} vy_read_task */

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
	e->seq = vy_sequence_new();
	if (e->seq == NULL)
		goto error_1;
	e->conf = vy_conf_new();
	if (e->conf == NULL)
		goto error_2;
	e->quota = vy_quota_new(e->conf->memory_limit);
	if (e->quota == NULL)
		goto error_3;
	e->cachepool = vy_cachepool_new(e);
	if (e->cachepool == NULL)
		goto error_4;
	e->xm = tx_manager_new(e);
	if (e->xm == NULL)
		goto error_5;
	e->stat = vy_stat_new();
	if (e->stat == NULL)
		goto error_6;
	e->scheduler = vy_scheduler_new(e);
	if (e->scheduler == NULL)
		goto error_7;

	mempool_create(&e->read_task_pool, cord_slab_cache(),
	               sizeof(struct vy_read_task));
	mempool_create(&e->cursor_pool, cord_slab_cache(),
	               sizeof(struct vy_cursor));
	return e;
error_7:
	vy_stat_delete(e->stat);
error_6:
	tx_manager_delete(e->xm);
error_5:
	vy_cachepool_delete(e->cachepool);
error_4:
	vy_quota_delete(e->quota);
error_3:
	vy_conf_delete(e->conf);
error_2:
	vy_sequence_delete(e->seq);
error_1:
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
	vy_cachepool_delete(e->cachepool);
	vy_conf_delete(e->conf);
	vy_quota_delete(e->quota);
	vy_stat_delete(e->stat);
	vy_sequence_delete(e->seq);
	mempool_destroy(&e->read_task_pool);
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
vy_begin_initial_recovery(struct vy_env *e)
{
	assert(e->status == VINYL_OFFLINE);
	e->status = VINYL_INITIAL_RECOVERY;
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
	int read_disk = 0;
	int read_cache = 0;
	int ops = 0;
	int64_t vlsn = UINT64_MAX;

	struct sicache *cache = vy_cachepool_pop(index->env->cachepool);
	if (cache == NULL)
		return -1;

	vy_index_ref(index);

	/* First iteration */
	struct siread q;
	si_readopen(&q, index, cache, VINYL_GT, vlsn, NULL, 0);
	assert(!q.cache_only);
	int rc = si_range(&q);
	si_readclose(&q);

	while (rc == 1) { /* while found */
		/* Update statistics */
		ops++;
		read_disk += q.read_disk;
		read_cache += q.read_cache;

		/* Execute callback */
		struct vy_tuple *tuple = q.result;
		assert(tuple != NULL); /* found */
		uint32_t mp_size;
		const char *mp = vy_tuple_data(index, tuple, &mp_size);
		int64_t lsn = tuple->lsn;
		rc = sendrow(ctx, mp, mp_size, lsn);
		if (rc != 0) {
			vy_tuple_unref(index, tuple);
			break;
		}

		/* Next iteration */
		si_readopen(&q, index, cache, VINYL_GT, vlsn, tuple->data,
			    tuple->size);
		assert(!q.cache_only);
		rc = si_range(&q);
		si_readclose(&q);
		vy_tuple_unref(index, tuple);
	}

	vy_cachepool_push(cache);
	vy_index_unref(index);
	return rc;
}

/* }}} replication */

/** {{{ vinyl_service - context of a vinyl background thread */

/* }}} vinyl service */

/* {{{ vy_run_itr API forward declaration */
/* TODO: move to header (with struct vy_run_itr) and remove static keyword */

/**
 * Position of particular tuple in vy_run
 */
struct vy_run_iterator_pos {
	uint32_t page_no;
	uint32_t pos_in_page;
};

/**
 * Iterator over vy_run
 */
struct vy_run_iterator {
	/* Members needed for memory allocation and disk accesss */
	/* index */
	struct vy_index *index;
	/* run */
	struct vy_run *run;
	/* file of run */
	struct vy_file *file;
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
	uint64_t vlsn;

	/* State of iterator */
	/* Position of curent record */
	struct vy_run_iterator_pos curr_pos;
	/* last tuple returned by vy_run_iterator_get, iterator hold this tuple
	 *  until next call to vy_run_iterator_get, in which it's unreffed */
	struct vy_tuple *curr_tuple;
	/* Position of record that spawned curr_tuple */
	struct vy_run_iterator_pos curr_tuple_pos;
	/* Page number of currenly loaded into memory page
	 * (UINT32_MAX if no page is loaded */
	uint32_t curr_loaded_page;
	/* Is false until first .._get ot .._next_.. method is called */
	bool search_started;
	/* Search is finished, you will not get more values from iterator */
	bool search_ended;
};

/**
 * Open the iterator
 */
static void
vy_run_iterator_open(struct vy_run_iterator *itr, struct vy_index *index,
		     struct vy_run *run, struct vy_file *file,
		     struct vy_filterif *compression, enum vy_order order,
		     char *key, uint64_t vlsn);

/**
 * Get a tuple from a record, that iterator currently positioned on
 * return 0 on sucess
 * return 1 on EOF
 * return -1 on memory or read error
 */
static int
vy_run_iterator_get(struct vy_run_iterator *itr, struct vy_tuple **result);

/**
 * Find the next record with different key as current and visible lsn
 * return 0 on sucess
 * return 1 on EOF
 * return -1 on memory or read error
 */
static int
vy_run_iterator_next_key(struct vy_run_iterator *itr);

/**
 * Find next (lower, older) record with the same key as current
 * return 0 on sucess
 * return 1 on EOF
 * return -1 on memory or read error
 */
static int
vy_run_iterator_next_lsn(struct vy_run_iterator *itr);

/**
 * Close an iterator and free all resources
 */
static void
vy_run_iterator_close(struct vy_run_iterator *itr);

/* }}} vy_run_iterator API forward declaration */

/* {{{ vy_run_iterator vy_run_iterator support functions */
/* TODO: move to appropriate c file and remove */

/**
 * Load page by given nubber from disk to memory, unload previosly load page
 * Does nothing if currently loaded page is the same as the querried
 * Return the page on success or NULL on read error
 * Affects: curr_loaded_page
 */
static struct sdpage *
vy_run_iterator_load_page(struct vy_run_iterator *itr, uint32_t page)
{
	assert(page < itr->run->index.header.count);
	if (itr->curr_loaded_page != page) {
		if (itr->curr_loaded_page != UINT32_MAX)
			vy_run_unload_page(itr->run, itr->curr_loaded_page);
		struct sdpage *result = vy_run_load_page(itr->run, page,
							 itr->file,
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
 * Compare two position
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
 * return 0 on sucess
 * return -1 or memory or read error
 * return 1 on EOF (possible when page has no records, int bootstrap run)
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
	struct sdpage *page = vy_run_iterator_load_page(itr, pos1.page_no);
	if (page == NULL)
		return -1;
	assert(pos1.page_no == pos2.page_no || pos2.pos_in_page == 0);
	uint32_t diff = pos1.page_no == pos2.page_no ?
		pos2.pos_in_page - pos1.pos_in_page :
		page->h->count - pos1.pos_in_page;
	result->page_no = pos1.page_no;
	result->pos_in_page = pos1.pos_in_page + diff / 2;
	return result->pos_in_page == page->h->count ? 1 : 0;
}

/**
 * Specific increment of middle wide position for binary search
 * Actually does not do increment until search in page was started.
 * return 0 on sucess
 * return -1 or memory or read error
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
	struct sdpage *page = vy_run_iterator_load_page(itr, mid.page_no);
	if (page == NULL)
		return -1;
	mid.pos_in_page++;
	*result =  mid.pos_in_page == page->h->count ? end : mid;
	return 0;
}

/**
 * Read key and lsn by given wide position.
 * For first record in page reads tuple from page index
 *   instead of loading from disk
 * Return NULL if read error or memory.
 * Affects: curr_loaded_page
 */
static char *
vy_run_iterator_read(struct vy_run_iterator *itr,
		     struct vy_run_iterator_pos pos, uint64_t *lsn)
{
	if (pos.pos_in_page == 0) {
		struct vy_page_info *page_info =
			vy_page_index_get_page(&itr->run->index, pos.page_no);
		*lsn = page_info->min_key_lsn;
		return vy_page_index_min_key(&itr->run->index, page_info);
	}
	struct sdpage *page = vy_run_iterator_load_page(itr, pos.page_no);
	if (page == NULL)
		return NULL;
	struct sdv *info = sd_pagev(page, pos.pos_in_page);
	*lsn = info->lsn;
	return sd_pagepointer(page, info);
}

/**
 * Binary search in a run of given key and lsn.
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
 * return 0 on success
 * return -1 on read or memory error
 * Beware of:
 * 1)VINYL_GT/VINYL_LE special case
 * 2)search with partial key and lsn != UINT64_MAX is meaningless and dangerous
 * 3)if return false, the position was set to maximal lsn of the next key
 */
static int
vy_run_iterator_search(struct vy_run_iterator *itr, char *key, uint64_t vlsn,
		       struct vy_run_iterator_pos *pos, bool *equal_key)
{
	struct vy_run_iterator_pos beg = {0, 0};
	struct vy_run_iterator_pos end = {itr->run->index.header.count, 0};
	*equal_key = false;
	while (vy_run_iterator_cmp_pos(beg, end) != 0) {
		struct vy_run_iterator_pos mid;
		int rc = vy_iterator_pos_mid(itr, beg, end, &mid);
		if (rc != 0)
			return rc;
		uint64_t fnd_lsn;
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
 * Icrement (or decrement, depending on order) current wide position
 * Return new value on success, end_pos on read error or EOF
 * return 0 on success
 * return 1 on EOF
 * return -1 on read or memory error
 * Affects: curr_loaded_page
 */
static int
vy_run_iterator_next_pos(struct vy_run_iterator *itr, enum vy_order order,
			 struct vy_run_iterator_pos *pos)
{
	*pos = itr->curr_pos;
	assert(pos->page_no < itr->run->index.header.count);
	if (order == VINYL_LE || order == VINYL_LT) {
		if (pos->page_no == 0 && pos->pos_in_page == 0)
			return 1;
		if (pos->pos_in_page > 0) {
			pos->pos_in_page--;
		} else {
			pos->page_no--;
			struct sdpage *page =
				vy_run_iterator_load_page(itr, pos->page_no);
			if (page == NULL)
				return -1;
			pos->pos_in_page = page->h->count - 1;
		}
	} else {
		assert(order == VINYL_GE || order == VINYL_GT ||
		       order == VINYL_EQ);
		struct sdpage *page =
			vy_run_iterator_load_page(itr, pos->page_no);
		if (page == NULL)
			return -1;
		pos->pos_in_page++;
		if (pos->pos_in_page >= page->h->count) {
			pos->page_no++;
			pos->pos_in_page = 0;
			if (pos->page_no == itr->run->index.header.count)
				return 1;
		}
	}
	return 0;
}

/**
 * Temporary prevent unloading of given page if necessary
 * Returns a value that must be passed to vy_run_iterator_unlock_page
 */
static uint32_t
vy_run_iterator_lock_page(struct vy_run_iterator *itr, uint32_t page_no)
{
	if (itr->curr_loaded_page != page_no)
		return UINT32_MAX;
	/* just increment reference counter */
	vy_run_load_page(itr->run, page_no,
			 itr->file, itr->compression);
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
 * Find next record with lsn <= itr->lsn record.
 * Current position must be at the beginning of serie of records with the
 * same key it terms of direction of iterator (i.e. left for GE, right for LE)
 * return 0 on success
 * return 1 on EOF
 * return -1 on read or memory error
 * Affects: curr_loaded_page, curr_pos, search_ended
 */
static int
vy_run_iterator_find_lsn(struct vy_run_iterator *itr)
{
	assert(itr->curr_pos.page_no < itr->run->index.header.count);
	uint64_t cur_lsn;
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
		/* lock page, i.e. prevent from unloading from memory of cur_key */
		uint32_t lock_page =
			vy_run_iterator_lock_page(itr, itr->curr_pos.page_no);

		struct vy_run_iterator_pos test_pos;
		rc = vy_run_iterator_next_pos(itr, itr->order, &test_pos);
		while (rc == 0) {
			uint64_t test_lsn;
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
 * return 0 on success
 * return 1 on EOF
 * return -1 on read or memory error
 * Affects: curr_loaded_page, curr_pos, search_ended
 */
static int
vy_run_iterator_start(struct vy_run_iterator *itr)
{
	assert(itr->curr_loaded_page == UINT32_MAX);
	assert(!itr->search_started);
	itr->search_started = true;

	if (itr->run->index.header.count == 1) {
		/* there can be a stupid bootstrap run in which it's EOF */
		struct sdpage *page =
			vy_run_iterator_load_page(itr, 0);
		if (page == NULL)
			return -1;
		if (page->h->count == 0) {
			vy_run_iterator_close(itr);
			return 1;
		}
	} else if (itr->run->index.header.count == 0) {
		/* never seen that, but it could be possible in future */
		vy_run_iterator_close(itr);
		return 1;
	}

	struct vy_run_iterator_pos end_pos = {itr->run->index.header.count, 0};
	bool equal_found = false;
	int rc;
	if (itr->key != NULL) {
		rc = vy_run_iterator_search(itr, itr->key, UINT64_MAX,
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
		     struct vy_run *run, struct vy_file *file,
		     struct vy_filterif *compression, enum vy_order order,
		     char *key, uint64_t vlsn)
{
	itr->index = index;
	itr->run = run;
	itr->file = file;
	itr->compression = compression;

	itr->order = order;
	itr->key = key;
	itr->vlsn = vlsn;

	itr->curr_tuple = NULL;
	itr->curr_loaded_page = UINT32_MAX;
	itr->curr_pos.page_no = itr->run->index.header.count;
	itr->curr_tuple_pos.page_no = UINT32_MAX;

	itr->search_started = false;
	itr->search_ended = false;
}

/**
 * Get a tuple from a record, that iterator currently positioned on
 * return 0 on sucess
 * return 1 on EOF
 * return -1 on memory or read error
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
		vy_tuple_unref(itr->index, itr->curr_tuple);
		itr->curr_tuple = NULL;
		itr->curr_tuple_pos.page_no = UINT32_MAX;
	}

	struct sdpage *page =
		vy_run_iterator_load_page(itr, itr->curr_pos.page_no);
	if (page == NULL)
		return -1;
	struct sdv *info = sd_pagev(page, itr->curr_pos.pos_in_page);
	char *key = sd_pagepointer(page, info);
	itr->curr_tuple = vy_tuple_alloc(itr->index, info->size);
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
 * Find the next record with different key as current and visible lsn
 * return 0 on sucess
 * return 1 on EOF
 * return -1 on memory or read error
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
	uint32_t end_page = itr->run->index.header.count;
	assert(itr->curr_pos.page_no <= end_page);
	struct key_def *key_def = itr->index->key_def;
	if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		if (itr->curr_pos.page_no == 0 &&
		    itr->curr_pos.pos_in_page == 0) {
			vy_run_iterator_close(itr);
			return 1;
		}
		if (itr->curr_pos.page_no == end_page) {
			/* special case for reverse iterators */
			uint32_t page_no = end_page - 1;
			struct sdpage *page =
				vy_run_iterator_load_page(itr, page_no);
			if (page == NULL)
				return -1;
			if (page->h->count == 0) {
				vy_run_iterator_close(itr);
				return 1;
			}
			itr->curr_pos.page_no = page_no;
			itr->curr_pos.pos_in_page = page->h->count - 1;
			return vy_run_iterator_find_lsn(itr);
		}
	}
	assert(itr->curr_pos.page_no < end_page);

	uint64_t cur_lsn;
	char *cur_key = vy_run_iterator_read(itr, itr->curr_pos, &cur_lsn);
	if (cur_key == NULL)
		return -1;

	/* lock page, i.e. prevent from unloading from memory of cur_key */
	uint32_t lock_page =
		vy_run_iterator_lock_page(itr, itr->curr_pos.page_no);

	uint64_t next_lsn;
	char *next_key;
	do {
		int rc = vy_run_iterator_next_pos(itr, itr->order, &itr->curr_pos);
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
 * return 0 on sucess
 * return 1 on if no value found, the iterator position was not changed
 * return -1 on memory or read error
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
	assert(itr->curr_pos.page_no < itr->run->index.header.count);

	struct vy_run_iterator_pos next_pos;
	int rc = vy_run_iterator_next_pos(itr, VINYL_GE, &next_pos);
	if (rc != 0)
		return rc;

	uint64_t cur_lsn;
	char *cur_key = vy_run_iterator_read(itr, itr->curr_pos, &cur_lsn);
	if (cur_key == NULL)
		return -1; /* read error */

	uint64_t next_lsn;
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
 * Close an iterator and free all resources
 */
static void
vy_run_iterator_close(struct vy_run_iterator *itr)
{
	if (itr->curr_tuple != NULL) {
		vy_tuple_unref(itr->index, itr->curr_tuple);
		itr->curr_tuple = NULL;
		itr->curr_tuple_pos.page_no = UINT32_MAX;
	}
	if (itr->curr_loaded_page != UINT32_MAX) {
		assert(itr->curr_loaded_page < itr->run->index.header.count);
		vy_run_unload_page(itr->run, itr->curr_loaded_page);
		itr->curr_loaded_page = UINT32_MAX;
	}
	itr->search_ended = true;
}

/* }}} vy_run_iterator API implementation */


/* {{{ Temporary wrap of new iterator to old API */

static void
vy_tmp_iterator_close(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->close == vy_tmp_iterator_close);
	(void)virt_iterator;
}

static int
vy_tmp_iterator_has(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->has == vy_tmp_iterator_has);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)virt_iterator->priv;
	struct vy_tuple *t;
	int rc = vy_run_iterator_get(itr, &t);
	return rc == 0;
}

static void *
vy_tmp_iterator_get(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->get == vy_tmp_iterator_get);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)virt_iterator->priv;
	struct vy_tuple *t;
	int rc = vy_run_iterator_get(itr, &t);
	if (rc != 0)
		return NULL;
	struct sv *sv = (struct sv *)(virt_iterator->priv + sizeof(*itr));
	sv_from_tuple(sv, t);
	return sv;
}

static void
vy_tmp_iterator_next(struct vy_iter *virt_iterator)
{
	assert(virt_iterator->vif->next == vy_tmp_iterator_next);
	struct vy_run_iterator *itr = (struct vy_run_iterator *)virt_iterator->priv;
	int rc = vy_run_iterator_next_lsn(itr);
	if (rc == 1)
		vy_run_iterator_next_key(itr);
}

void
vy_tmp_iterator_open(struct vy_iter *virt_iterator, struct vy_index *index,
		     struct vy_run *run, struct vy_file *file,
		     struct vy_filterif *compression,
		     enum vy_order order, char *key)
{
	static struct vy_iterif vif = {
		.close = vy_tmp_iterator_close,
		.has = vy_tmp_iterator_has,
		.get = vy_tmp_iterator_get,
		.next = vy_tmp_iterator_next
	};
	virt_iterator->vif = &vif;
	struct vy_run_iterator *itr = (struct vy_run_iterator *)virt_iterator->priv;
	assert(sizeof(virt_iterator->priv) >= sizeof(*itr) + sizeof(struct sv));
	vy_run_iterator_open(itr, index, run, file, compression, order, key,
			UINT64_MAX);

}

/* }}} Temporary wrap of new iterator to old API */
