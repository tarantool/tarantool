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
	VINYL_SHUTDOWN,
	VINYL_DROP,
	VINYL_MALFUNCTION
};

struct vy_sequence;
struct vy_conf;
struct vy_quota;
struct vy_cachepool;
struct tx_manager;
struct scheduler;
struct vy_stat;
struct srzone;

struct vinyl_env {
	enum vinyl_status status;
	/** List of open spaces. */
	struct rlist indexes;
	struct vy_sequence  *seq;
	struct vy_conf      *conf;
	struct vy_quota     *quota;
	struct vy_cachepool *cachepool;
	struct tx_manager   *xm;
	struct scheduler    *scheduler;
	struct vy_stat      *stat;
	struct mempool      read_task_pool;
	struct mempool      cursor_pool;
	struct cord *worker_pool;
	int worker_pool_size;
	volatile int worker_pool_run;
};

static inline struct srzone *
sr_zoneof(struct vinyl_env *r);

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

static inline void
vy_buf_set(struct vy_buf *b, int size, int i, char *buf, size_t bufsize)
{
	assert(b->s + (size * i + bufsize) <= b->p);
	memcpy(b->s + size * i, buf, bufsize);
}

#define VINYL_INJECTION_SD_BUILD_0      0
#define VINYL_INJECTION_SD_BUILD_1      1
#define VINYL_INJECTION_SI_BRANCH_0     2
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

static inline void *
vy_bufiter_get(struct vy_bufiter *bi)
{
	return bi->v;
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

#define vy_oom() \
	vy_e(ER_VINYL, "%s", "memory allocation failed")

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
	case VINYL_SHUTDOWN:
	case VINYL_DROP:
	case VINYL_OFFLINE:
	case VINYL_MALFUNCTION:
		return false;
	}
	unreachable();
	return 0;
}

static inline bool
vy_status_active(struct vy_status *s) {
	return vy_status_is_active(vy_status(s));
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
	struct vy_avg    cursor_read_disk;
	struct vy_avg    cursor_read_cache;
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
	vy_avg_prepare(&s->cursor_read_disk);
	vy_avg_prepare(&s->cursor_read_cache);
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
vy_stat_cursor(struct vy_stat *s, uint64_t start, int read_disk, int read_cache, int ops)
{
	uint64_t diff = clock_monotonic64() - start;
	tt_pthread_mutex_lock(&s->lock);
	s->cursor++;
	vy_avg_update(&s->cursor_read_disk, read_disk);
	vy_avg_update(&s->cursor_read_cache, read_cache);
	vy_avg_update(&s->cursor_latency, diff);
	vy_avg_update(&s->cursor_ops, ops);
	tt_pthread_mutex_unlock(&s->lock);
}

struct srzone {
	uint32_t enable;
	char     name[4];
	uint32_t compact_wm;
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

#define SVNONE       0
#define SVDELETE     1
#define SVUPSERT     2
#define SVGET        4
#define SVDUP        8
#define SVCONFLICT  32

struct sv;

struct svif {
	uint8_t   (*flags)(struct sv*);
	uint64_t  (*lsn)(struct sv*);
	char     *(*pointer)(struct sv*);
	uint32_t  (*size)(struct sv*);
};

static struct svif svtuple_if;
static struct svif svref_if;
static struct svif sdv_if;
struct vinyl_tuple;
struct sdv;
struct sdpageheader;

struct sv {
	struct svif *i;
	void *v, *arg;
};

static inline struct vinyl_tuple *
sv_to_tuple(struct sv *v)
{
	assert(v->i == &svtuple_if);
	struct vinyl_tuple *tuple = (struct vinyl_tuple *)v->v;
	assert(tuple != NULL);
	return tuple;
}

static inline void
sv_from_tuple(struct sv *v, struct vinyl_tuple *tuple)
{
	v->i   = &svtuple_if;
	v->v   = tuple;
	v->arg = NULL;
}

static inline void
sv_from_sdv(struct sv *v, struct sdv *sdv, struct sdpageheader *h)
{
	v->i   = &sdv_if;
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
vinyl_tuple_size(struct vinyl_tuple *v);

static struct vinyl_tuple *
vinyl_tuple_alloc(struct vinyl_index *index, uint32_t size);

static inline const char *
vy_tuple_key_part(const char *tuple_data, uint32_t part_id);

static inline int
vy_tuple_compare(const char *tuple_data_a, const char *tuple_data_b,
		 const struct key_def *key_def);

static struct vinyl_tuple *
vinyl_tuple_from_key_data(struct vinyl_index *index, const char *key,
			  uint32_t part_count);
static void
vinyl_tuple_ref(struct vinyl_tuple *tuple);

static void
vinyl_tuple_unref(struct vinyl_index *index, struct vinyl_tuple *tuple);

static int
vinyl_tuple_unref_rt(struct vinyl_env *env, struct vinyl_tuple *v);

struct tx_index;
struct vinyl_index;

struct txv {
	/** Transaction start logical time - used by conflict manager. */
	uint64_t tsn;
	/** Transaction end logical time - used by conflict manager. */
	uint64_t csn;
	int log_read;
	struct tx_index *index;
	struct vinyl_tuple *tuple;
	struct txv *gc;
	/** Next tuple in the same index. */
	struct rlist next_in_index;
	rb_node(struct txv) tree_node;
};

static inline struct txv *
txv_new(struct vinyl_tuple *tuple, uint64_t tsn, struct tx_index *index)
{
	struct txv *v = malloc(sizeof(struct txv));
	if (unlikely(v == NULL))
		return NULL;
	v->index = index;
	v->tsn = tsn;
	v->log_read = 0;
	v->csn = UINT64_MAX;
	v->tuple = tuple;
	vinyl_tuple_ref(tuple);
	v->gc = NULL;
	rlist_create(&v->next_in_index);
	return v;
}

static inline void
txv_delete(struct vinyl_env *env, struct txv *v)
{
	rlist_del(&v->next_in_index);
	vinyl_tuple_unref_rt(env, v->tuple);
	free(v);
}

static inline void
txv_commit(struct txv *v, uint64_t csn)
{
	v->log_read  = INT_MAX;
	v->csn = csn;
}

static inline int
txv_committed(struct txv *v)
{
	return v->csn != UINT64_MAX;
}

static inline void
txv_abort(struct txv *v)
{
	v->tuple->flags |= SVCONFLICT;
}

static struct txv *
txv_prev(struct txv *v);

static struct txv *
txv_next(struct txv *v);

static inline void
txv_abort_all(struct txv *v)
{
	while (v) {
		txv_abort(v);
		v = txv_next(v);
	}
}

static inline int
txv_aborted(struct txv *v)
{
	return v->tuple->flags & SVCONFLICT;
}


struct PACKED txlogindex {
	struct rlist log;
	struct tx_index *index;
	struct rlist next;
};

/**
 * In-memory transaction log.
 * Transaction changes are made in multi-version
 * in-memory index (tx_index) and recorded in this log.
 * When the transaction is committed, the changes are written to the
 * in-memory single-version index (struct svindex) in a
 * specific vy_range object of an index.
 */
struct txlog {
	/**
	 * Number of writes (inserts,updates, deletes) done by
	 * the transaction.
	 */
	int write_count;
	struct rlist index;
	struct vy_buf buf;
};

static inline void
txlog_init(struct txlog *l)
{
	vy_buf_init(&l->buf);
	l->write_count = 0;
	rlist_create(&l->index);
}

static inline void
txlog_free(struct txlog *l)
{
	vy_buf_free(&l->buf);
	l->write_count = 0;
	struct txlogindex *i, *tmp;
	rlist_foreach_entry_safe(i, &l->index, next, tmp)
		free(i);
	rlist_create(&l->index);
}

static inline int
txlog_count(struct txlog *l) {
	return vy_buf_used(&l->buf) / sizeof(struct txv *);
}

static inline int
txlog_write_count(struct txlog *l) {
	return l->write_count;
}

static inline struct txv *
txlog_at(struct txlog *l, int pos) {
	return *(struct txv **)vy_buf_at(&l->buf, sizeof(struct txv *), pos);
}

void
txlogindex_del(struct txlog *l, struct txv *v)
{
	(void) l;
	rlist_del(&v->next_in_index);
}

int
txlogindex_add(struct txlog *l, struct txv *v)
{

	struct txlogindex *i;
	rlist_foreach_entry(i, &l->index, next) {
		if (i->index == v->index) {
			rlist_add_tail(&i->log, &v->next_in_index);
			return 0;
		}
	}
	i = malloc(sizeof(struct txlogindex));
	if (i == NULL)
		return -1;
	i->index = v->index;
	rlist_create(&i->log);
	rlist_create(&i->next);
	rlist_add_tail(&i->log, &v->next_in_index);
	rlist_add(&l->index, &i->next);
	return 0;
}

static inline int
txlog_add(struct txlog *l, struct txv *v)
{
	if (vy_buf_add(&l->buf, &v, sizeof(struct txv *)))
		return -1;

	if (txlogindex_add(l, v)) {
		l->buf.p -= sizeof(struct txv *);
		return -1;
	}
	if (! (v->tuple->flags & SVGET))
		l->write_count++;
	return 0;
}

static inline void
txlog_replace(struct txlog *l, int n, struct txv *v)
{
	struct txv *ov = txlog_at(l, n);
	txlogindex_del(l, ov);
	if (! (ov->tuple->flags & SVGET))
		l->write_count--;
	txlogindex_add(l, v);
	if (! (v->tuple->flags & SVGET))
		l->write_count++;
	vy_buf_set(&l->buf, sizeof(struct txv *), n, (char*) &v, sizeof(struct txv *));
}

struct PACKED svmergesrc {
	struct vy_iter *i;
	struct vy_iter src;
	uint8_t dup;
	void *ptr;
};

struct svmerge {
	struct vinyl_index *index;
	struct key_def *key_def; /* TODO: use index->key_def when possible */
	struct vy_buf buf;
};

static inline void
sv_mergeinit(struct svmerge *m, struct vinyl_index *index,
	     struct key_def *key_def)
{
	vy_buf_init(&m->buf);
	m->index = index;
	m->key_def = key_def;
}

static inline int
sv_mergeprepare(struct svmerge *m, int count)
{
	int rc = vy_buf_ensure(&m->buf, sizeof(struct svmergesrc) * count);
	if (unlikely(rc == -1))
		return vy_oom();
	return 0;
}

static inline void
sv_mergefree(struct svmerge *m)
{
	vy_buf_free(&m->buf);
}

static inline void
sv_mergereset(struct svmerge *m)
{
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
	struct vinyl_tuple *upsert_tuple;
};

static struct vinyl_tuple *
vy_apply_upsert(struct sv *upsert, struct sv *object,
		struct vinyl_index *index);

static inline int
sv_readiter_upsert(struct svreaditer *i)
{
	assert(i->upsert_tuple == NULL);
	struct vinyl_index *index = i->merge->merge->index;
	/* upsert begin */
	struct sv *v = sv_mergeiter_get(i->merge);
	assert(v != NULL);
	assert(sv_flags(v) & SVUPSERT);
	i->upsert_tuple = vinyl_tuple_alloc(index, sv_size(v));
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
		struct vinyl_tuple *up = vy_apply_upsert(v, next_v, index);
		if (up == NULL)
			return -1; /* memory error */
		vinyl_tuple_unref(index, i->upsert_tuple);
		i->upsert_tuple = up;
		sv_from_tuple(&i->upsert_sv, i->upsert_tuple);
		v = &i->upsert_sv;
		if (! (sv_flags(next_v) & SVUPSERT))
			skip = 1;
	}
	if (sv_flags(v) & SVUPSERT) {
		struct vinyl_tuple *up = vy_apply_upsert(v, NULL, index);
		if (up == NULL)
			return -1; /* memory error */
		vinyl_tuple_unref(index, i->upsert_tuple);
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
		vinyl_tuple_unref(im->merge->merge->index, im->upsert_tuple);
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
		vinyl_tuple_unref(im->merge->merge->index, im->upsert_tuple);
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
	struct vinyl_tuple *upsert_tuple;
};

static inline int
sv_writeiter_upsert(struct svwriteiter *i)
{
	assert(i->upsert_tuple == NULL);
	/* upsert begin */
	struct vinyl_index *index = i->merge->merge->index;
	struct sv *v = sv_mergeiter_get(i->merge);
	assert(v != NULL);
	assert(sv_flags(v) & SVUPSERT);
	assert(sv_lsn(v) <= i->vlsn);
	i->upsert_tuple = vinyl_tuple_alloc(index, sv_size(v));
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

		struct vinyl_tuple *up = vy_apply_upsert(v, next_v, index);
		if (up == NULL)
			return -1; /* memory error */
		vinyl_tuple_unref(index, i->upsert_tuple);
		i->upsert_tuple = up;
		sv_from_tuple(&i->upsert_sv, i->upsert_tuple);
		v = &i->upsert_sv;
	}
	if (sv_flags(v) & SVUPSERT) {
		struct vinyl_tuple *up = vy_apply_upsert(v, NULL, index);
		if (up == NULL)
			return -1; /* memory error */
		vinyl_tuple_unref(index, i->upsert_tuple);
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
		vinyl_tuple_unref(im->merge->merge->index, im->upsert_tuple);
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
		vinyl_tuple_unref(im->merge->merge->index, im->upsert_tuple);
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
sv_indexfree(struct svindex *i, struct vinyl_env *env)
{
	assert(i == i->tree.arg);
	struct bps_tree_svindex_iterator itr =
		bps_tree_svindex_itr_first(&i->tree);
	while (!bps_tree_svindex_itr_is_invalid(&itr)) {
		struct vinyl_tuple *v = bps_tree_svindex_itr_get_elem(&i->tree, &itr)->v;
		vinyl_tuple_unref_rt(env, v);
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
	i->used += vinyl_tuple_size(ref.v);
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
	enum vinyl_order order;
	void *key;
	int key_size;
};

static struct vy_iterif sv_indexiterif;

static inline int
sv_indexiter_open(struct vy_iter *i, struct svindex *index,
		  enum vinyl_order o, void *key, int key_size)
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

/** Transaction state. */
enum tx_state {
	/** Initial state. */
	VINYL_TX_READY,
	/**
	 * A transaction is finished and validated in the engine.
	 * It may still be rolled back if there is an error
	 * writing the WAL.
	 */
	VINYL_TX_PREPARE,
	/** A transaction is committed. */
	VINYL_TX_COMMIT,
	/** A transaction is aborted or rolled back. */
	VINYL_TX_ROLLBACK
};

/** Transaction type. */
enum tx_type {
	VINYL_TX_RO,
	VINYL_TX_RW
};

typedef rb_tree(struct txv) txv_tree_t;

struct txv_tree_key {
	char *data;
	int size;
	uint64_t tsn;
};

static int
txv_tree_cmp(txv_tree_t *rbtree, struct txv *a, struct txv *b);

static int
txv_tree_key_cmp(txv_tree_t *rbtree, struct txv_tree_key *a, struct txv *b);

rb_gen_ext_key(, txv_tree_, txv_tree_t, struct txv, tree_node, txv_tree_cmp,
		 struct txv_tree_key *, txv_tree_key_cmp);

static struct txv *
txv_tree_search_key(txv_tree_t *rbtree, char *data, int size, uint64_t tsn)
{
	struct txv_tree_key key;
	key.data = data;
	key.size = size;
	key.tsn = tsn;
	return txv_tree_search(rbtree, &key);
}

/**
 * Transaction operation index. Contains all changes
 * made by this transaction before it commits. Is used
 * to implement read committed isolation level, i.e.
 * the changes made by a transaction are only present
 * in its tx_index, and thus not seen by other transactions.
 */
struct tx_index {
	txv_tree_t tree;
	struct vinyl_index *index;
	struct key_def *key_def;
	pthread_mutex_t mutex;
};

static int
txv_tree_cmp(txv_tree_t *rbtree, struct txv *a, struct txv *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct tx_index, tree)->key_def;
	int rc = vy_tuple_compare(a->tuple->data, b->tuple->data, key_def);
	/**
	 * While in svindex older value are "bigger" than newer
	 * ones, i.e. the newest value comes first, in
	 * transactional index (tx_index), we want to look
	 * at data in chronological order.
	 * @sa tree_svindex_compare
	 */
	if (rc == 0)
		rc = a->tsn < b->tsn ? -1 : a->tsn > b->tsn;
	return rc;
}

static int
txv_tree_key_cmp(txv_tree_t *rbtree, struct txv_tree_key *a, struct txv *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct tx_index, tree)->key_def;
	int rc = vy_tuple_compare(a->data, b->tuple->data, key_def);
	if (rc == 0)
		rc = a->tsn < b->tsn ? -1 : a->tsn > b->tsn;
	return rc;
}

static struct txv *
txv_prev(struct txv *v)
{
	txv_tree_t *tree = &v->index->tree;
	struct key_def *key_def;
	key_def = container_of(tree, struct tx_index, tree)->key_def;
	struct txv *prev = txv_tree_prev(tree, v);
	if (prev && vy_tuple_compare(v->tuple->data,
				     prev->tuple->data, key_def) == 0)
		return prev;
	return NULL;
}

static struct txv *
txv_next(struct txv *v)
{
	txv_tree_t *tree = &v->index->tree;
	struct key_def *key_def;
	key_def = container_of(tree, struct tx_index, tree)->key_def;
	struct txv *next = txv_tree_next(tree, v);
	if (next && vy_tuple_compare(v->tuple->data,
				     next->tuple->data, key_def) == 0)
		return next;
	return NULL;
}

struct sicache;

struct vinyl_tx {
	struct txlog log;
	uint64_t start;
	enum tx_type     type;
	enum tx_state    state;
	/** Transaction logical start time. */
	uint64_t tsn;
	/** Transaction logical end time. */
	uint64_t   csn;
	/**
	 * Consistent read view LSN: the LSN recorded
	 * at start of transaction and used to implement
	 * transactional read view.
	 */
	uint64_t   vlsn;
	int log_read;
	rb_node(struct vinyl_tx) tree_node;
	struct tx_manager *manager;
};

typedef rb_tree(struct vinyl_tx) tx_tree_t;

static int
tx_tree_cmp(tx_tree_t *rbtree, struct vinyl_tx *a, struct vinyl_tx *b)
{
	(void)rbtree;
	return vy_cmp(a->tsn, b->tsn);
}

static int
tx_tree_key_cmp(tx_tree_t *rbtree, const char *a, struct vinyl_tx *b)
{
	(void)rbtree;
	return vy_cmp(load_u64(a), b->tsn);
}

rb_gen_ext_key(, tx_tree_, tx_tree_t, struct vinyl_tx, tree_node,
		 tx_tree_cmp, const char *, tx_tree_key_cmp);

struct tx_manager {
	tx_tree_t tree;
	uint32_t    count_rd;
	uint32_t    count_rw;
	uint32_t    count_gc;
	/** Transaction logical time. */
	uint64_t tsn;
	struct txv       *gc;
	struct vinyl_env *env;
};

static struct tx_manager *
tx_manager_new(struct vinyl_env*);

static int
tx_manager_delete(struct tx_manager*);

static int
tx_index_init(struct tx_index *, struct vinyl_index *,

			 struct key_def *key_def);
static int
tx_index_free(struct tx_index*, struct tx_manager*);

static void
tx_begin(struct tx_manager*, struct vinyl_tx*, enum tx_type);

static void
tx_delete(struct vinyl_tx*);

static enum tx_state
tx_rollback(struct vinyl_tx*);

static int
tx_set(struct vinyl_tx*, struct tx_index*, struct vinyl_tuple*);

static int
tx_get(struct vinyl_tx*, struct tx_index*, struct vinyl_tuple*, struct vinyl_tuple**);

static struct tx_manager *
tx_manager_new(struct vinyl_env *env)
{
	struct tx_manager *m = malloc(sizeof(*m));
	if (m == NULL) {
		diag_set(OutOfMemory, sizeof(*m), "tx_manager", "struct");
		return NULL;
	}
	tx_tree_new(&m->tree);
	m->count_rd = 0;
	m->count_rw = 0;
	m->count_gc = 0;
	m->tsn = 0;
	m->gc  = NULL;
	m->env = env;
	return m;
}

static int
tx_manager_delete(struct tx_manager *m)
{
	free(m);
	return 0;
}

static int
tx_index_init(struct tx_index *i, struct vinyl_index *index,
	      struct key_def *key_def)
{
	txv_tree_new(&i->tree);
	i->index = index;
	i->key_def = key_def;
	(void) tt_pthread_mutex_init(&i->mutex, NULL);
	return 0;
}

static struct txv *
txv_tree_delete_cb(txv_tree_t *t, struct txv *v, void *arg)
{
	(void) t;
	txv_delete(arg, v);
	return NULL;
}

static int
tx_index_free(struct tx_index *i, struct tx_manager *m)
{
	txv_tree_iter(&i->tree, NULL, txv_tree_delete_cb, m->env);
	(void) tt_pthread_mutex_destroy(&i->mutex);
	return 0;
}

static uint64_t
tx_manager_min(struct tx_manager *m)
{
	struct vinyl_tx *min = tx_tree_first(&m->tree);
	return min ? min->tsn : 0;
}

static uint64_t
tx_manager_max(struct tx_manager *m)
{
	struct vinyl_tx *max = tx_tree_last(&m->tree);
	return max ? max->tsn : 0;
}

static inline enum tx_state
tx_promote(struct vinyl_tx *tx, enum tx_state state)
{
	tx->state = state;
	return state;
}

static void
tx_begin(struct tx_manager *m, struct vinyl_tx *tx, enum tx_type type)
{
	txlog_init(&tx->log);
	tx->start = clock_monotonic64();
	tx->manager = m;
	tx->state = VINYL_TX_READY;
	tx->type = type;
	tx->log_read = -1;

	tx->csn = UINT64_MAX;
	tx->tsn = ++m->tsn;
	tx->vlsn = vy_sequence(m->env->seq, VINYL_LSN);

	tx_tree_insert(&m->tree, tx);
	if (type == VINYL_TX_RO)
		m->count_rd++;
	else
		m->count_rw++;
}

static inline void
txv_untrack(struct txv *v)
{
	struct tx_index *i = v->index;
	tt_pthread_mutex_lock(&i->mutex);
	txv_tree_remove(&i->tree, v);
	tt_pthread_mutex_unlock(&i->mutex);
}

static inline uint64_t
tx_manager_tsn(struct tx_manager *m)
{
	uint64_t tsn = UINT64_MAX;
	if (m->count_rw == 0)
		return tsn;

	struct vinyl_tx *min = tx_tree_first(&m->tree);
	while (min && min->type == VINYL_TX_RO)
		min = tx_tree_next(&m->tree, min);

	assert(min != NULL);
	return min->tsn;
}

static inline void
tx_manager_gc(struct tx_manager *m)
{
	if (m->count_gc == 0)
		return;
	uint64_t min_tsn = tx_manager_tsn(m);
	struct txv *gc = NULL;
	uint32_t count = 0;
	struct txv *next;
	struct txv *v = m->gc;
	for (; v; v = next)
	{
		next = v->gc;
		assert(v->tuple->flags & SVGET);
		assert(txv_committed(v));
		if (v->csn < min_tsn) {
			txv_untrack(v);
			txv_delete(m->env, v);
		} else {
			v->gc = gc;
			gc = v;
			count++;
		}
	}
	m->count_gc = count;
	m->gc = gc;
}

static void
tx_delete(struct vinyl_tx *tx)
{
	txlog_free(&tx->log);
	free(tx);
}

static inline void
tx_end(struct vinyl_tx *tx)
{
	struct tx_manager *m = tx->manager;
	bool was_oldest = tx == tx_tree_first(&m->tree);
	tx_tree_remove(&m->tree, tx);
	if (tx->type == VINYL_TX_RO)
		m->count_rd--;
	else
		m->count_rw--;
	if (was_oldest) {
		struct vinyl_tx *oldest = tx_tree_first(&m->tree);
		vy_sequence_lock(m->env->seq);
		m->env->seq->vlsn = oldest ? oldest->vlsn :
			m->env->seq->lsn;
		vy_sequence_unlock(m->env->seq);
		tx_manager_gc(m);
	}
}

static inline void
tx_rollback_svp(struct vinyl_tx *tx, struct vy_bufiter *i)
{
	void *new_p = i->v ? i->v : i->buf->p;
	struct tx_manager *m = tx->manager;
	for (; vy_bufiter_has(i); vy_bufiter_next(i))
	{
		struct txv *v = *(struct txv **) vy_bufiter_get(i);
		/* remove from index */
		txv_untrack(v);
		txv_delete(m->env, v);
	}
	i->buf->p = new_p;
}

static enum tx_state
tx_rollback(struct vinyl_tx *tx)
{
	struct vy_bufiter i;
	vy_bufiter_open(&i, &tx->log.buf, sizeof(struct txv *));
	/* support log free after commit and half-commit mode */
	tx_rollback_svp(tx, &i);
	tx_promote(tx, VINYL_TX_ROLLBACK);
	tx_end(tx);
	return VINYL_TX_ROLLBACK;
}

static enum tx_state
tx_prepare(struct vinyl_tx *tx)
{
	/* proceed read-only transactions */
	if (tx->type == VINYL_TX_RO || txlog_write_count(&tx->log) == 0)
		return tx_promote(tx, VINYL_TX_PREPARE);
	struct vy_bufiter i;
	vy_bufiter_open(&i, &tx->log.buf, sizeof(struct txv *));
	for (; vy_bufiter_has(&i); vy_bufiter_next(&i))
	{
		struct txv *v = *(struct txv ** )vy_bufiter_get(&i);
		if (v->log_read == tx->log_read)
			break;
		if (txv_aborted(v))
			return tx_promote(tx, VINYL_TX_ROLLBACK);
		struct txv *prev = txv_prev(v);
		if (prev == NULL)
			continue;
		if (txv_committed(prev))
			continue;
		/* force commit for read-only conflicts */
		if (prev->tuple->flags & SVGET)
			continue;
		return tx_promote(tx, VINYL_TX_ROLLBACK);
	}
	return tx_promote(tx, VINYL_TX_PREPARE);
}

static int
tx_set(struct vinyl_tx *tx, struct tx_index *index,
       struct vinyl_tuple *tuple)
{
	struct tx_manager *m = tx->manager;
	struct vinyl_env *env = m->env;
	/* allocate mvcc container */
	struct txv *v = txv_new(tuple, tx->tsn, index);
	if (v == NULL)
		return -1;

	/* update concurrent index */
	tt_pthread_mutex_lock(&index->mutex);
	struct txv *own = txv_tree_search_key(&index->tree, v->tuple->data,
					      v->tuple->size, tx->tsn);
	/* match previous update made by current transaction */
	if (own != NULL)
	{
		if (unlikely(tuple->flags & SVUPSERT)) {
			vy_error("%s", "only one upsert statement is "
			         "allowed per a transaction key");
			goto error;
		}
		/* replace old document with the new one */
		v->log_read = own->log_read;
		if (unlikely(txv_aborted(own)))
			txv_abort(v);
		txv_tree_remove(&index->tree, own);
		txv_tree_insert(&index->tree, v);
		/* update log */
		txlog_replace(&tx->log, v->log_read, v);

		txv_delete(env, own);
		tt_pthread_mutex_unlock(&index->mutex);
		return 0;
	}
	v->log_read = txlog_count(&tx->log);
	int rc = txlog_add(&tx->log, v);
	if (unlikely(rc == -1)) {
		vy_oom();
		goto error;
	}
	/* add version */
	txv_tree_insert(&index->tree, v);
	tt_pthread_mutex_unlock(&index->mutex);
	return 0;
error:
	tt_pthread_mutex_unlock(&index->mutex);
	txv_delete(env, v);
	return -1;
}

static int
tx_get(struct vinyl_tx *tx, struct tx_index *index, struct vinyl_tuple *key,
       struct vinyl_tuple **result)
{
	tt_pthread_mutex_lock(&index->mutex);
	struct txv *v = txv_tree_search_key(&index->tree,
					    key->data, key->size, tx->tsn);
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
	if (tx->log_read == -1)
		tx->log_read = txlog_count(&tx->log);
	tt_pthread_mutex_unlock(&index->mutex);
	int rc = tx_set(tx, index, key);
	if (unlikely(rc == -1))
		return -1;
	return 0;
}

#define SD_IDBRANCH 1

struct PACKED sdid {
	uint64_t parent;
	uint64_t id;
	uint8_t  flags;
};

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
	struct vy_buf *xfbuf;
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
	return vy_tuple_compare(sd_pagepointer(i->page, v), i->key, key_def);
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
		/* i->key is [] */
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
		/* i->key is [] */
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
		 struct vy_buf *xfbuf, struct sdpage *page, enum vinyl_order o,
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
	enum vinyl_order order;
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
		  struct vy_page_index *index, enum vinyl_order order, void *key)
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

static inline void
sd_indexiter_next(struct vy_page_iter *ii)
{
	switch (ii->order) {
	case VINYL_LT:
	case VINYL_LE: ii->cur_pos--;
		break;
	case VINYL_GT:
	case VINYL_GE: ii->cur_pos++;
		break;
	default:
		unreachable();
		break;
	}
	if (unlikely(ii->cur_pos < 0))
		ii->cur_page = NULL;
	else if (unlikely(ii->cur_pos >= (int)ii->index->header.count))
		ii->cur_page = NULL;
	else
		ii->cur_page = vy_page_index_get_page(ii->index, ii->cur_pos);
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

struct vinyl_service {
	struct vinyl_env *env;
	struct sdc sdc;
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
			if (buf == NULL)
				return -1;
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
	enum vinyl_order     o;
	int         has;
	uint64_t    has_vlsn;
	int         use_compression;
	struct vy_filterif *compression_if;
	struct key_def *key_def;
};

struct PACKED sdread {
	struct sdreadarg ra;
	struct vy_page_info *ref;
	struct sdpage page;
	int reads;
};

static inline int
sd_read_page(struct sdread *i, struct vy_page_info *info)
{
	struct sdreadarg *arg = &i->ra;

	vy_buf_reset(arg->buf);
	int rc = vy_buf_ensure(arg->buf, info->unpacked_size);
	if (unlikely(rc == -1))
		return vy_oom();
	vy_buf_reset(arg->buf_xf);
	rc = vy_buf_ensure(arg->buf_xf, arg->index->header.sizevmax);
	if (unlikely(rc == -1))
		return vy_oom();

	i->reads++;

	/* compression */
	if (arg->use_compression)
	{
		char *page_pointer;
		vy_buf_reset(arg->buf_read);
		rc = vy_buf_ensure(arg->buf_read, info->size);
		if (unlikely(rc == -1))
			return vy_oom();
		rc = vy_file_pread(arg->file, info->offset,
				   arg->buf_read->s, info->size);
		if (unlikely(rc == -1)) {
			vy_error("index file '%s' read error: %s",
				 arg->file->path,
				 strerror(errno));
			return -1;
		}
		vy_buf_advance(arg->buf_read, info->size);
		page_pointer = arg->buf_read->s;

		/* copy header */
		memcpy(arg->buf->p, page_pointer, sizeof(struct sdpageheader));
		vy_buf_advance(arg->buf, sizeof(struct sdpageheader));

		/* decompression */
		struct vy_filter f;
		rc = vy_filter_init(&f, arg->compression_if, VINYL_FOUTPUT);
		if (unlikely(rc == -1)) {
			vy_error("index file '%s' decompression error",
			         arg->file->path);
			return -1;
		}
		int size = info->size - sizeof(struct sdpageheader);
		rc = vy_filter_next(&f, arg->buf,
				    page_pointer + sizeof(struct sdpageheader),
				    size);
		if (unlikely(rc == -1)) {
			vy_error("index file '%s' decompression error",
			         arg->file->path);
			return -1;
		}
		vy_filter_free(&f);
		sd_pageinit(&i->page, (struct sdpageheader*)arg->buf->s);
		return 0;
	}

	/* default */
	assert(info->unpacked_size == info->size);
	rc = vy_file_pread(arg->file, info->offset, arg->buf->s, info->size);
	if (unlikely(rc == -1)) {
		vy_error("index file '%s' read error: %s",
		         arg->file->path,
		         strerror(errno));
		return -1;
	}
	vy_buf_advance(arg->buf, info->size);
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
sd_read_next(struct vy_iter*);

static struct vy_iterif sd_readif;

static inline int
sd_read_open(struct vy_iter *iptr, struct sdreadarg *arg, void *key, int keysize)
{
	iptr->vif = &sd_readif;
	struct sdread *i = (struct sdread*)iptr->priv;
	i->reads = 0;
	i->ra = *arg;
	sd_indexiter_open(arg->index_iter, arg->key_def, arg->index,
			  arg->o, key);
	i->ref = sd_indexiter_get(arg->index_iter);
	if (i->ref == NULL)
		return 0;
	if (arg->has) {
		assert(arg->o == VINYL_GE);
		if (likely(i->ref->max_lsn <= arg->has_vlsn)) {
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
sd_read_close(struct vy_iter *iptr)
{
	struct sdread *i = (struct sdread*)iptr->priv;
	i->ref = NULL;
}

static inline int
sd_read_has(struct vy_iter *iptr)
{
	struct sdread *i = (struct sdread*)iptr->priv;
	if (unlikely(i->ref == NULL))
		return 0;
	return sd_pageiter_has(i->ra.page_iter);
}

static inline void*
sd_read_get(struct vy_iter *iptr)
{
	struct sdread *i = (struct sdread*)iptr->priv;
	if (unlikely(i->ref == NULL))
		return NULL;
	return sd_pageiter_get(i->ra.page_iter);
}

static inline void
sd_read_next(struct vy_iter *iptr)
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
sd_read_stat(struct vy_iter *iptr)
{
	struct sdread *i = (struct sdread*)iptr->priv;
	return i->reads;
}

static int
vy_page_index_load(struct vy_page_index *i, void *ptr)
{
	struct vy_page_index_header *h = (struct vy_page_index_header *)ptr;
	uint32_t index_size = h->count * sizeof(struct vy_page_info);
	int rc = vy_buf_ensure(&i->pages, index_size);
	if (unlikely(rc == -1))
		return vy_oom();
	memcpy(i->pages.s, (char *)ptr + sizeof(struct vy_page_index_header),
	       index_size);
	vy_buf_advance(&i->pages, index_size);
	uint32_t minmax_size = h->size - index_size;
	rc = vy_buf_ensure(&i->minmax, minmax_size);
	if (unlikely(rc == -1))
		return vy_oom();
	memcpy(i->minmax.s,
	       (char *)ptr + sizeof(struct vy_page_index_header) + index_size,
	       minmax_size);
	vy_buf_advance(&i->minmax, minmax_size);
	i->header = *h;
	return 0;
}

static struct vy_iterif sd_readif =
{
	.close = sd_read_close,
	.has   = sd_read_has,
	.get   = sd_read_get,
	.next  = sd_read_next
};

struct PACKED sdrecover {
	struct vy_file *file;
	int corrupt;
	struct vy_page_index_header *v;
	struct vy_page_index_header *actual;
	struct sdseal *seal;
	struct vy_mmap map;
	struct vinyl_env *env;
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
sd_recover_open(struct sdrecover *ri, struct vinyl_env *env,
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

static uint8_t
sdv_flags(struct sv *v)
{
	return ((struct sdv*)v->v)->flags;
}

static uint64_t
sdv_lsn(struct sv *v)
{
	return ((struct sdv*)v->v)->lsn;
}

static char*
sdv_pointer(struct sv *v)
{
	struct sdpage p = {
		.h = (struct sdpageheader*)v->arg
	};
	return sd_pagepointer(&p, (struct sdv*)v->v);
}

static uint32_t
sdv_size(struct sv *v)
{
	return ((struct sdv*)v->v)->size;
}

static struct svif sdv_if =
{
	.flags     = sdv_flags,
	.lsn       = sdv_lsn,
	.pointer   = sdv_pointer,
	.size      = sdv_size
};

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

static void vy_index_conf_init(struct vy_index_conf*);
static void vy_index_conf_free(struct vy_index_conf*);

struct PACKED vy_run {
	struct sdid id;
	struct vy_page_index index;
	struct vy_run *link;
	struct vy_run *next;
};

static inline void
vy_run_init(struct vy_run *b)
{
	memset(&b->id, 0, sizeof(b->id));
	vy_page_index_init(&b->index);
	b->link = NULL;
	b->next = NULL;
}

static inline struct vy_run*
vy_run_new()
{
	struct vy_run *b = (struct vy_run*)malloc(sizeof(struct vy_run));
	if (unlikely(b == NULL)) {
		vy_oom();
		return NULL;
	}
	vy_run_init(b);
	return b;
}

static inline void
vy_run_set(struct vy_run *b, struct vy_page_index *i)
{
	b->id = i->header.id;
	b->index = *i;
}

static inline void
vy_run_free(struct vy_run *b)
{
	vy_page_index_free(&b->index);
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

struct PACKED vy_range {
	uint32_t   recover;
	uint16_t   flags;
	uint64_t   update_time;
	uint32_t   used; /* sum of i0->used + i1->used */
	uint64_t   ac;
	struct vy_run   self;
	struct vy_run  *branch;
	uint32_t   branch_count;
	uint32_t   temperature;
	uint64_t   temperature_reads;
	uint16_t   refs;
	pthread_mutex_t reflock;
	struct svindex    i0, i1;
	struct vy_file     file;
	rb_node(struct vy_range) tree_node;
	struct ssrqnode   nodecompact;
	struct ssrqnode   nodebranch;
	struct rlist     gc;
	struct rlist     commit;
};

static struct vy_range *vy_range_new(struct key_def *key_def);
static int
vy_range_open(struct vy_range*, struct vinyl_env*, char *);
static int
vy_range_create(struct vy_range*, struct vy_index_conf*, struct sdid*);
static int vy_range_free(struct vy_range*, struct vinyl_env*, int);
static int vy_range_gc_index(struct vinyl_env*, struct svindex*);
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
	struct vy_run *b = n->branch;
	while (b) {
		size += vy_page_index_size(&b->index) +
		        vy_page_index_total(&b->index);
		b = b->next;
	}
	return size;
}

struct vy_planner {
	struct ssrq branch;
	struct ssrq compact;
};

enum vy_task_type {
	VY_TASK_UNKNOWN = 0,
	VY_TASK_BRANCH,
	VY_TASK_AGE,
	VY_TASK_COMPACT,
	VY_TASK_CHECKPOINT,
	VY_TASK_GC,
	VY_TASK_SHUTDOWN,
	VY_TASK_DROP,
	VY_TASK_NODEGC
};

struct vy_task {
	enum vy_task_type type;
	struct vinyl_index *index;
	struct vy_range *node;
};

static int vy_planner_init(struct vy_planner*);
static int vy_planner_free(struct vy_planner*);
static int vy_planner_update(struct vy_planner*, struct vy_range*);
static int vy_planner_update_range(struct vy_planner *p, struct vy_range *n);
static int vy_planner_remove(struct vy_planner*, struct vy_range*);

typedef rb_tree(struct vy_range) vy_range_tree_t;

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
	struct vinyl_env *env = (struct vinyl_env *)arg;
	vy_range_free(n, env, 0);
	return NULL;
}

struct vy_profiler {
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
	struct vinyl_index  *i;
};

struct vinyl_index {
	struct vinyl_env *env;
	struct vy_profiler rtp;
	struct tx_index coindex;
	uint64_t tsn_min;
	uint64_t tsn_max;

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

static void
vinyl_index_ref(struct vinyl_index *index);

static void
vinyl_index_unref(struct vinyl_index *index);

struct key_def *
vy_index_key_def(struct vinyl_index *index)
{
	return index->key_def;
}

static int
vy_range_tree_cmp(vy_range_tree_t *rbtree, struct vy_range *a, struct vy_range *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vinyl_index, tree)->key_def;
	return vy_range_cmpnode(a, b, key_def);
}

static int
vy_range_tree_key_cmp(vy_range_tree_t *rbtree,
		    struct vy_range_tree_key *a, struct vy_range *b)
{
	struct key_def *key_def =
		container_of(rbtree, struct vinyl_index, tree)->key_def;
	return (-vy_range_cmp(b, a->data, key_def));
}

static inline void
vy_index_lock(struct vinyl_index *i) {
	tt_pthread_mutex_lock(&i->lock);
}

static inline void
vy_index_unlock(struct vinyl_index *i) {
	tt_pthread_mutex_unlock(&i->lock);
}

static inline int
vinyl_index_delete(struct vinyl_index *index);

static struct vy_range *si_bootstrap(struct vinyl_index*, uint64_t);

struct PACKED sicachebranch {
	struct vy_run *branch;
	struct vy_page_info *ref;
	struct sdpage page;
	struct vy_iter i;
	struct sdpageiter page_iter;
	struct vy_page_iter index_iter;
	struct vy_buf buf_a;
	struct vy_buf buf_b;
	int open;
	struct sicachebranch *next;
};

struct sicache {
	struct sicachebranch *path;
	struct sicachebranch *branch;
	uint32_t count;
	uint64_t nsn;
	struct vy_range *node;
	struct sicache *next;
	struct vy_cachepool *pool;
};

struct vy_cachepool {
	struct sicache *head;
	int n;
	struct vinyl_env *env;
	pthread_mutex_t mutex;
};

static inline void
si_cacheinit(struct sicache *c, struct vy_cachepool *pool)
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
		vy_buf_free(&cb->buf_a);
		vy_buf_free(&cb->buf_b);
		free(cb);
		cb = next;
	}
}

static inline void
si_cachereset(struct sicache *c)
{
	struct sicachebranch *cb = c->path;
	while (cb) {
		vy_buf_reset(&cb->buf_a);
		vy_buf_reset(&cb->buf_b);
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
si_cacheadd(struct vy_run *b)
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
	vy_buf_init(&nb->buf_a);
	vy_buf_init(&nb->buf_b);
	return nb;
}

static inline int
si_cachevalidate(struct sicache *c, struct vy_range *n)
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
		struct vy_run *b = n->branch;
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
	struct vy_run *b = n->branch;
	while (cb && b) {
		cb->branch = b;
		cb->ref = NULL;
		cb->open = 0;
		sd_read_close(&cb->i);
		vy_buf_reset(&cb->buf_a);
		vy_buf_reset(&cb->buf_b);
		last = cb;
		cb = cb->next;
		b  = b->next;
	}
	while (cb) {
		cb->branch = NULL;
		cb->ref = NULL;
		cb->open = 0;
		sd_read_close(&cb->i);
		vy_buf_reset(&cb->buf_a);
		vy_buf_reset(&cb->buf_b);
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
si_cachefollow(struct sicache *c, struct vy_run *seek)
{
	while (c->branch) {
		struct sicachebranch *cb = c->branch;
		c->branch = c->branch->next;
		if (likely(cb->branch == seek))
			return cb;
	}
	return NULL;
}

static inline struct vy_cachepool *
vy_cachepool_new(struct vinyl_env *env)
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
	enum vinyl_order order;
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
	struct vinyl_tuple *result;
	struct sicache *cache;
	struct vinyl_index *index;
};

struct vy_rangeiter {
	struct vinyl_index *index;
	struct vy_range *cur_range;
	enum vinyl_order order;
	char *key;
	int key_size;
};

static inline void
vy_rangeiter_open(struct vy_rangeiter *itr, struct vinyl_index *index,
		  enum vinyl_order order, char *key, int key_size)
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
			itr->cur_range = vy_range_tree_last(&index->tree);;
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

static int vy_index_drop(struct vinyl_index*);
static int vy_index_dropmark(struct vinyl_index*);
static int vy_index_droprepository(char*, int);

static int
si_merge(struct vinyl_index*, struct sdc*, struct vy_range*, uint64_t,
	 struct svmergeiter*, uint64_t, uint32_t);

static int vy_run(struct vinyl_index*, struct sdc*, struct vy_range*, uint64_t);
static int
si_compact(struct vinyl_index*, struct sdc*, struct vy_range*, uint64_t,
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
	struct vinyl_env *env = (struct vinyl_env *)arg;
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
si_trackfree(struct sitrack *t, struct vinyl_env *env)
{
	vy_range_id_tree_iter(&t->tree, NULL, vy_range_id_tree_free_cb, env);
#ifndef NDEBUG
	t->tree.rbt_root = (struct vy_range *)(intptr_t)0xDEADBEEF;
#endif
}

static inline void
si_trackmetrics(struct sitrack *t, struct vy_range *n)
{
	struct vy_run *b = n->branch;
	while (b) {
		struct vy_page_index_header *h = &b->index.header;
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
si_insert(struct vinyl_index *index, struct vy_range *range)
{
	vy_range_tree_insert(&index->tree, range);
	index->range_count++;
	return 0;
}

static int
si_remove(struct vinyl_index *index, struct vy_range *range)
{
	vy_range_tree_remove(&index->tree, range);
	index->range_count--;
	return 0;
}

static int
si_replace(struct vinyl_index *index, struct vy_range *o, struct vy_range *n)
{
	vy_range_tree_remove(&index->tree, o);
	vy_range_tree_insert(&index->tree, n);
	return 0;
}

static int
vy_task_execute(struct vy_task *task, struct sdc *c, uint64_t vlsn)
{
	struct vinyl_index *index = task->index;
	assert(index != NULL);
	int rc = -1;
	switch (task->type) {
	case VY_TASK_NODEGC:
		rc = vy_range_free(task->node, index->env, 1);
		break;
	case VY_TASK_CHECKPOINT:
	case VY_TASK_BRANCH:
	case VY_TASK_AGE:
		rc = vy_run(index, c, task->node, vlsn);
		break;
	case VY_TASK_GC:
	case VY_TASK_COMPACT:
		rc = si_compact(index, c, task->node, vlsn, NULL, 0);
		break;
	case VY_TASK_SHUTDOWN:
		assert(index->refs == 1); /* referenced by this task */
		rc = vinyl_index_delete(index);
		task->index = NULL;
		break;
	case VY_TASK_DROP:
		assert(index->refs == 1); /* referenced by this task */
		rc = vy_index_drop(index);
		task->index = NULL;
		break;
	default:
		unreachable();
	}
	/* garbage collect buffers */
	sd_cgc(c, index->conf.buf_gc_wm);
	return rc;
}

/* dump tuple to branch page buffers (tuple header and data) */
static int
vy_branch_dump_tuple(struct svwriteiter *iwrite, struct vy_buf *info_buf,
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

/* write tuples from iterator to new page in branch,
 * update page and branch statistics */
static int
vy_branch_write_page(struct vy_file *file, struct svwriteiter *iwrite,
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
		int rc = vy_branch_dump_tuple(iwrite, &tuplesinfo, &values,
					      &header);
		if (rc != 0) {
			vy_oom();
			goto err;
		}
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

/* write tuples for iterator to new branch
 * and setup corresponding sdindex structure */
static int
vy_branch_write(struct vy_file *file, struct svwriteiter *iwrite,
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

		if (vy_buf_ensure(&sdindex->pages, sizeof(struct vy_page_info))) {
			vy_oom();
			goto err;
		}
		struct vy_page_info *page = (struct vy_page_info *)sdindex->pages.p;
		vy_buf_advance(&sdindex->pages, sizeof(struct vy_page_info));
		if (vy_branch_write_page(file, iwrite, compression, index_header,
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
vy_run_create(struct vinyl_index *index, struct sdc *c,
		struct vy_range *parent, struct svindex *vindex,
		uint64_t vlsn, struct vy_run **result)
{
	(void)c;
	struct vinyl_env *env = index->env;

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
	id.flags = SD_IDBRANCH;
	id.id = vy_sequence(env->seq, VINYL_NSN_NEXT);
	id.parent = parent->self.id.id;
	struct vy_page_index sdindex;
	vy_page_index_init(&sdindex);
	if ((rc = vy_branch_write(&parent->file, &iwrite,
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
vy_run(struct vinyl_index *index, struct sdc *c, struct vy_range *n,
       uint64_t vlsn)
{
	struct vinyl_env *env = index->env;
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

	struct vy_run *branch = NULL;
	int rc = vy_run_create(index, c, n, i, vlsn, &branch);
	if (unlikely(rc == -1))
		return -1;
	if (unlikely(branch == NULL)) {
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
	branch->next = n->branch;
	n->branch->link = branch;
	n->branch = branch;
	n->branch_count++;
	assert(n->used >= i->used);
	n->used -= i->used;
	vy_quota_op(env->quota, VINYL_QREMOVE, i->used);
	index->size += vy_page_index_size(&branch->index) +
		       vy_page_index_total(&branch->index);
	struct svindex swap = *i;
	swap.tree.arg = &swap;
	vy_range_unrotate(n);
	vy_range_unlock(n);
	vy_planner_update(&index->p, n);
	vy_index_unlock(index);

	vy_range_gc_index(env, &swap);
	return 1;
}

static int
si_compact(struct vinyl_index *index, struct sdc *c, struct vy_range *node,
	   uint64_t vlsn, struct vy_iter *vindex, uint64_t vindex_used)
{
	assert(node->flags & SI_LOCK);

	/* prepare for compaction */
	int rc;
	rc = sd_censure(c, node->branch_count);
	if (unlikely(rc == -1))
		return vy_oom();
	struct svmerge merge;
	sv_mergeinit(&merge, index, index->key_def);
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
	struct vy_run *b = node->branch;
	while (b) {
		s = sv_mergeadd(&merge, NULL);
		/* choose compression type */
		int compression;
		struct vy_filterif *compression_if;
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
			.key_def		 = index->key_def
		};
		int rc = sd_read_open(&s->src, &arg, NULL, 0);
		if (unlikely(rc == -1))
			return vy_oom();
		size_stream += vy_page_index_total(&b->index);
		count += vy_page_index_count(&b->index);
		cbuf = cbuf->next;
		b = b->next;
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
	}
	return 0;
}

static int vy_index_dropmark(struct vinyl_index *i)
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

static int vy_index_drop(struct vinyl_index *i)
{
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s", i->conf.path);
	/* drop file must exists at this point */
	/* shutdown */
	int rc = vinyl_index_delete(i);
	if (unlikely(rc == -1))
		return -1;
	/* remove directory */
	rc = vy_index_droprepository(path, 1);
	return rc;
}

static int
si_redistribute(struct vinyl_index *index, struct sdc *c,
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
			return vy_oom();
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
si_redistribute_set(struct vinyl_index *index, uint64_t now, struct svref *v)
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
	node->used += vinyl_tuple_size(v->v);
	/* schedule node */
	vy_planner_update_range(&index->p, node);
}

static int
si_redistribute_index(struct vinyl_index *index, struct sdc *c, struct vy_range *node)
{
	struct svindex *vindex = vy_range_index(node);
	struct vy_iter ii;
	sv_indexiter_open(&ii, vindex, VINYL_GE, NULL, 0);
	while (sv_indexiter_has(&ii)) {
		struct sv *v = sv_indexiter_get(&ii);
		int rc = vy_buf_add(&c->b, &v->v, sizeof(struct svref**));
		if (unlikely(rc == -1))
			return vy_oom();
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
si_splitfree(struct vy_buf *result, struct vinyl_env *env)
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
si_split(struct vinyl_index *index, struct sdc *c, struct vy_buf *result,
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
	struct vinyl_env *env = index->env;
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
		n->branch = &n->self;
		n->branch_count++;

		if ((rc = vy_branch_write(&n->file, &iwrite,
				          index->conf.compression_if,
				          size_stream, &id, &sdindex)))
			goto error;

		rc = vy_buf_add(result, &n, sizeof(struct vy_range*));
		if (unlikely(rc == -1)) {
			vy_oom();
			goto error;
		}

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
si_merge(struct vinyl_index *index, struct sdc *c, struct vy_range *range,
	 uint64_t vlsn, struct svmergeiter *stream, uint64_t size_stream,
	 uint32_t n_stream)
{
	struct vinyl_env *env = index->env;
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
			vy_oom();
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
		vy_oom();
		return NULL;
	}
	n->recover = 0;
	n->ac = 0;
	n->flags = 0;
	n->update_time = 0;
	n->used = 0;
	vy_run_init(&n->self);
	n->branch = NULL;
	n->branch_count = 0;
	n->temperature = 0;
	n->temperature_reads = 0;
	n->refs = 0;
	tt_pthread_mutex_init(&n->reflock, NULL);
	vy_file_init(&n->file);
	sv_indexinit(&n->i0, key_def);
	sv_indexinit(&n->i1, key_def);
	ss_rqinitnode(&n->nodecompact);
	ss_rqinitnode(&n->nodebranch);
	rlist_create(&n->gc);
	rlist_create(&n->commit);
	return n;
}

static int vy_range_gc_index(struct vinyl_env *env, struct svindex *i)
{
	sv_indexfree(i, env);
	sv_indexinit(i, i->key_def);
	return 0;
}

static inline int
vy_range_close(struct vy_range *n, struct vinyl_env *env, int gc)
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
vy_range_recover(struct vy_range *n, struct vinyl_env *env)
{
	/* recover branches */
	struct vy_run *b = NULL;
	struct sdrecover ri;
	sd_recover_open(&ri, env, &n->file);
	int first = 1;
	int rc;
	while (sd_recover_has(&ri))
	{
		struct vy_page_index_header *h = sd_recover_get(&ri);
		if (first) {
			b = &n->self;
		} else {
			b = vy_run_new();
			if (unlikely(b == NULL))
				goto e0;
		}
		struct vy_page_index index;
		vy_page_index_init(&index);
		rc = vy_page_index_load(&index, h);
		if (unlikely(rc == -1))
			goto e0;
		vy_run_set(b, &index);

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
		vy_run_free(b);
e1:
	sd_recover_close(&ri);
	return -1;
}

static int
vy_range_open(struct vy_range *n, struct vinyl_env *env, char *path)
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
vy_range_free_branches(struct vy_range *n)
{
	struct vy_run *p = n->branch;
	struct vy_run *next = NULL;
	while (p && p != &n->self) {
		next = p->next;
		vy_run_free(p);
		p = next;
	}
	vy_page_index_free(&n->self.index);
}

static int vy_range_free(struct vy_range *n, struct vinyl_env *env, int gc)
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
	vy_range_free_branches(n);
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
	rc = ss_rqinit(&p->branch, 1024 * 1024, 4000);
	if (unlikely(rc == -1)) {
		ss_rqfree(&p->compact);
		return -1;
	}
	return 0;
}

static int vy_planner_free(struct vy_planner *p)
{
	ss_rqfree(&p->compact);
	ss_rqfree(&p->branch);
	return 0;
}

static int
vy_planner_update(struct vy_planner *p, struct vy_range *n)
{
	ss_rqupdate(&p->branch, &n->nodebranch, n->used);
	ss_rqupdate(&p->compact, &n->nodecompact, n->branch_count);
	return 0;
}

static int
vy_planner_update_range(struct vy_planner *p, struct vy_range *n)
{
	ss_rqupdate(&p->branch, &n->nodebranch, n->used);
	return 0;
}

static int
vy_planner_remove(struct vy_planner *p, struct vy_range *n)
{
	ss_rqdelete(&p->branch, &n->nodebranch);
	ss_rqdelete(&p->compact, &n->nodecompact);
	return 0;
}

static inline void
vy_task_create(struct vy_task *task, struct vinyl_index *index,
	       enum vy_task_type type)
{
	memset(task, 0, sizeof(*task));
	task->type = type;
	task->index = index;
	vinyl_index_ref(index);
}

static inline void
vy_task_destroy(struct vy_task *task)
{
	if (task->type != VY_TASK_DROP && task->type != VY_TASK_SHUTDOWN) {
		vinyl_index_unref(task->index);
		task->index = NULL;
	}
	TRASH(task);
}

static inline int
vy_planner_peek_checkpoint(struct vinyl_index *index, uint64_t checkpoint_lsn,
			   struct vy_task *task)
{
	/* try to peek a node which has min
	 * lsn <= required value
	*/
	bool in_progress = false;
	struct vy_range *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.branch, pn))) {
		n = container_of(pn, struct vy_range, nodebranch);
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
vy_planner_peek_branch(struct vinyl_index *index, uint32_t branch_wm,
		       struct vy_task *task)
{
	/* try to peek a node with a biggest in-memory index */
	struct vy_range *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.branch, pn))) {
		n = container_of(pn, struct vy_range, nodebranch);
		if (n->flags & SI_LOCK)
			continue;
		if (n->used < branch_wm)
			return 0; /* nothing to do */
		vy_task_create(task, index, VY_TASK_BRANCH);
		vy_range_lock(n);
		task->node = n;
		return 1; /* new task */
	}
	return 0; /* nothing to do */
}

static inline int
vy_planner_peek_age(struct vinyl_index *index, uint32_t ttl, uint32_t ttl_wm,
		    struct vy_task *task)
{
	/* try to peek a node with update >= a and in-memory
	 * index size >= b */

	/* full scan */
	uint64_t now = clock_monotonic64();
	struct vy_range *n = NULL;
	struct ssrqnode *pn = NULL;
	bool in_progress = false;
	while ((pn = ss_rqprev(&index->p.branch, pn))) {
		n = container_of(pn, struct vy_range, nodebranch);
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
vy_planner_peek_compact(struct vinyl_index *index, uint32_t branch_count,
			struct vy_task *task)
{
	/* try to peek a node with a biggest number
	 * of branches */
	struct vy_range *n;
	struct ssrqnode *pn = NULL;
	while ((pn = ss_rqprev(&index->p.compact, pn))) {
		n = container_of(pn, struct vy_range, nodecompact);
		if (n->flags & SI_LOCK)
			continue;
		if (n->branch_count >= branch_count) {
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
vy_planner_peek_gc(struct vinyl_index *index, uint64_t gc_lsn,
		   uint32_t gc_percent, struct vy_task *task)
{
	/* try to peek a node with a biggest number
	 * of branches which is ready for gc */
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
vy_planner_peek_shutdown(struct vinyl_index *index, struct vy_task *task)
{
	int status = vy_status(&index->status);
	switch (status) {
	case VINYL_DROP:
		if (index->refs > 0)
			return 0; /* index still has tasks */
		vy_task_create(task, index, VY_TASK_DROP);
		return 1; /* new task */
	case VINYL_SHUTDOWN:
		if (index->refs > 0)
				return 0; /* index still has tasks */
		vy_task_create(task, index, VY_TASK_SHUTDOWN);
		return 1; /* new task */
	default:
		unreachable();
		return -1;
	}
}

static inline int
vy_planner_peek_nodegc(struct vinyl_index *index, struct vy_task *task)
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

static int vy_profiler_begin(struct vy_profiler *p, struct vinyl_index *i)
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
vy_profiler_histogram_branch(struct vy_profiler *p)
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
		p->total_branch_count += n->branch_count;
		if (p->total_branch_max < n->branch_count)
			p->total_branch_max = n->branch_count;
		if (n->branch_count < 20)
			p->histogram_branch[n->branch_count]++;
		else
			p->histogram_branch_20plus++;
		memory_used += n->i0.used;
		memory_used += n->i1.used;
		struct vy_run *b = n->branch;
		while (b) {
			p->count += b->index.header.keys;
			p->count_dup += b->index.header.dupkeys;
			int indexsize = vy_page_index_size(&b->index);
			p->total_snapshot_size += indexsize;
			p->total_node_size += indexsize + b->index.header.total;
			p->total_node_origin_size += indexsize + b->index.header.totalorigin;
			p->total_page_count += b->index.header.count;
			b = b->next;
		}
		n = vy_range_tree_next(&p->i->tree, n);
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

	vy_profiler_histogram_branch(p);
	return 0;
}

static void
si_readopen(struct siread *q, struct vinyl_index *index, struct sicache *c,
	    enum vinyl_order order, uint64_t vlsn, void *key, uint32_t keysize)
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
	struct vinyl_tuple *v;
	if (likely(result->i == &svtuple_if)) {
		v = result->v;
		vinyl_tuple_ref(v);
	} else {
		/* Allocate new tuple and copy data */
		uint32_t size = sv_size(result);
		v = vinyl_tuple_alloc(q->index, size);
		if (unlikely(v == NULL))
			return vy_oom();
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
	struct vinyl_index *index = q->index;
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

static inline int
si_rangebranch(struct siread *q, struct vy_range *n,
	       struct vy_run *b, struct svmerge *m)
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
	struct vy_index_conf *conf = &q->index->conf;
	int compression;
	struct vy_filterif *compression_if;
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
	int count = node->branch_count + 2 + 1;
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

	/* cache and branches */
	rc = si_cachevalidate(q->cache, node);
	if (unlikely(rc == -1)) {
		vy_oom();
		return -1;
	}

	struct vy_run *b = node->branch;
	while (b) {
		rc = si_rangebranch(q, node, b, m);
		if (unlikely(rc == -1 || rc == 2))
			return rc;
		b = b->next;
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
si_readcommited(struct vinyl_index *index, struct vinyl_tuple *tuple)
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
	for (struct vy_run *run = range->branch; run != NULL; run = run->next)
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
si_bootstrap(struct vinyl_index *index, uint64_t parent)
{
	struct vinyl_env *env = index->env;
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
	n->branch = &n->self;
	n->branch_count++;

	/* create index with one empty page */
	struct vy_page_index sdindex;
	vy_page_index_init(&sdindex);
	vy_branch_write(&n->file, NULL, index->conf.compression_if, 0, &id,
			&sdindex);

	vy_run_set(&n->self, &sdindex);

	return n;
e0:
	vy_range_free(n, env, 0);
	return NULL;
}

static inline int
si_deploy(struct vinyl_index *index, struct vinyl_env *env, int create_directory)
{
	/* create directory */
	int rc;
	if (likely(create_directory)) {
		rc = mkdir(index->conf.path, 0755);
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
si_trackdir(struct sitrack *track, struct vinyl_env *env, struct vinyl_index *i)
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
si_trackvalidate(struct sitrack *track, struct vy_buf *buf, struct vinyl_index *i)
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
si_recovercomplete(struct sitrack *track, struct vinyl_env *env, struct vinyl_index *index, struct vy_buf *buf)
{
	/* prepare and build primary index */
	vy_buf_reset(buf);
	struct vy_range *n = vy_range_id_tree_first(&track->tree);
	while (n) {
		int rc = vy_buf_add(buf, &n, sizeof(struct vy_range*));
		if (unlikely(rc == -1))
			return vy_oom();
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
si_recoversize(struct vinyl_index *i)
{
	struct vy_range *n = vy_range_tree_first(&i->tree);
	while (n) {
		i->size += vy_range_size(n);
		n = vy_range_tree_next(&i->tree, n);
	}
}

static inline int
si_recoverindex(struct vinyl_index *index, struct vinyl_env *env)
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
si_recoverdrop(struct vinyl_index *i)
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
si_recover(struct vinyl_index *i)
{
	struct vinyl_env *env = i->env;
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

static void
si_write(struct txlogindex *li, uint64_t time,
	 enum vinyl_status status)
{
	struct vinyl_index *index = li->index->index;
	struct vinyl_env *env = index->env;
	struct rlist rangelist;
	size_t quota = 0;
	rlist_create(&rangelist);

	vy_index_lock(index);
	index->update_time = time;
	struct txv *v;
	rlist_foreach_entry(v, &li->log, next_in_index) {
		struct vinyl_tuple *tuple = v->tuple;
		if (tuple->flags & SVGET ||
		    (status == VINYL_FINAL_RECOVERY &&
		     si_readcommited(index, tuple))) {

			continue;
		}
		/* match node */
		struct vy_rangeiter ii;
		vy_rangeiter_open(&ii, index, VINYL_GE, tuple->data, tuple->size);
		struct vy_range *range = vy_rangeiter_get(&ii);
		assert(range != NULL);
		struct svref ref;
		vinyl_tuple_ref(tuple);
		ref.v = tuple;
		ref.flags = 0;
		/* insert into node index */
		struct svindex *vindex = vy_range_index(range);
		int rc = sv_indexset(vindex, ref);
		assert(rc == 0); /* TODO: handle BPS tree errors properly */
		(void) rc;
		/* update node */
		range->used += vinyl_tuple_size(tuple);
		quota += vinyl_tuple_size(tuple);
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
	return;
}

struct scheduler {
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
	struct vinyl_index **indexes;
	struct rlist   shutdown;
	struct vinyl_env    *env;
};

static struct scheduler *
scheduler_new(struct vinyl_env*);
static void
scheduler_delete(struct scheduler *);
static void
vy_workers_start(struct vinyl_env *env);
static void
vy_workers_stop(struct vinyl_env *env);

static int sc_ctl_checkpoint(struct scheduler*);

static struct scheduler *
scheduler_new(struct vinyl_env *env)
{
	struct scheduler *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		diag_set(OutOfMemory, sizeof(*s), "scheduler", "struct");
		return NULL;
	}
	uint64_t now = clock_monotonic64();
	tt_pthread_mutex_init(&s->lock, NULL);
	s->checkpoint_lsn           = 0;
	s->checkpoint_lsn_last      = 0;
	s->checkpoint_in_progress   = false;
	s->age_in_progress          = false;
	s->age_time                 = now;
	s->gc_in_progress           = false;
	s->gc_time                  = now;
	s->indexes                  = NULL;
	s->count                    = 0;
	s->rr                       = 0;
	s->env                      = env;
	rlist_create(&s->shutdown);
	return s;
}

static void
scheduler_delete(struct scheduler *s)
{
	if (s->count > 0)
		free(s->indexes);
	free(s);
}

static int
vy_scheduler_add_index(struct scheduler *s, struct vinyl_index *index)
{
	tt_pthread_mutex_lock(&s->lock);
	struct vinyl_index **indexes = realloc(s->indexes,
		(s->count + 1) * sizeof(*indexes));
	if (unlikely(indexes == NULL)) {
		diag_set(OutOfMemory, sizeof((s->count + 1) * sizeof(*indexes)),
			 "scheduler", "indexes");
		tt_pthread_mutex_unlock(&s->lock);
		return -1;
	}
	s->indexes = indexes;
	s->indexes[s->count++] = index;
	vinyl_index_ref(index);
	tt_pthread_mutex_unlock(&s->lock);
	return 0;
}

static int
vy_scheduler_del_index(struct scheduler *s, struct vinyl_index *index)
{
	tt_pthread_mutex_lock(&s->lock);
	int found = 0;
	while (found < s->count && s->indexes[found] != index)
		found++;
	assert(found < s->count);
	for (int i = found + 1; i < s->count; i++)
		s->indexes[i - 1] = s->indexes[i];
	s->count--;
	if (unlikely(s->rr >= s->count))
		s->rr = 0;
	vinyl_index_unref(index);
	/* add index to `shutdown` list */
	rlist_add(&s->shutdown, &index->link);
	tt_pthread_mutex_unlock(&s->lock);
	return 0;
}

static struct vinyl_index *
vy_scheduler_peek_index(struct scheduler *s)
{
	if (s->count == 0)
		return NULL;
	assert(s->rr < s->count);
	struct vinyl_index *index = s->indexes[s->rr];
	s->rr = (s->rr + 1) % s->count;
	return index;
}

static int sc_ctl_checkpoint(struct scheduler *s)
{
	tt_pthread_mutex_lock(&s->lock);
	uint64_t lsn = vy_sequence(s->env->seq, VINYL_LSN);
	s->checkpoint_lsn = lsn;
	s->checkpoint_in_progress = true;
	for (int i = 0; i < s->count; i++) {
		s->indexes[i]->checkpoint_in_progress = true;
	}
	tt_pthread_mutex_unlock(&s->lock);
	return 0;
}

static int
vy_plan_index(struct scheduler *s, struct srzone *zone, uint64_t vlsn,
	      struct vinyl_index *index, struct vy_task *task)
{
	int rc;

	/* node gc */
	rc = vy_planner_peek_nodegc(index, task);
	if (rc != 0)
		return rc; /* found or error */

	/* checkpoint */
	if (s->checkpoint_in_progress) {
		rc = vy_planner_peek_checkpoint(index, s->checkpoint_lsn, task);
		if (rc != 0)
			return rc; /* found or error */
	}

	/* garbage-collection */
	if (s->gc_in_progress) {
		rc = vy_planner_peek_gc(index, vlsn, zone->gc_wm, task);
		if (rc != 0)
			return rc; /* found or error */
	}

	/* index aging */
	if (s->age_in_progress) {
		uint32_t ttl = zone->branch_age * 1000000; /* ms */
		uint32_t ttl_wm = zone->branch_age_wm;
		rc = vy_planner_peek_age(index, ttl, ttl_wm, task);
		if (rc != 0)
			return rc; /* found or error */
	}

	/* branching */
	rc = vy_planner_peek_branch(index, zone->branch_wm, task);
	if (rc != 0)
		return rc; /* found or error */

	/* compaction */
	rc = vy_planner_peek_compact(index, zone->compact_wm, task);
	if (rc != 0)
		return rc; /* found or error */

	return 0; /* nothing to do */
}

static int
vy_plan(struct scheduler *s, struct srzone *zone, uint64_t vlsn,
	struct vy_task *task)
{
	/* pending shutdowns */
	struct vinyl_index *index, *n;
	rlist_foreach_entry_safe(index, &s->shutdown, link, n) {
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
	index = vy_scheduler_peek_index(s);
	if (index == NULL)
		return 0; /* nothing to do */

	vy_index_lock(index);
	int rc = vy_plan_index(s, zone, vlsn, index, task);
	vy_index_unlock(index);
	return rc;
}

static int
sc_schedule(struct vinyl_env *env, struct sdc *sdc, int64_t vlsn)
{
	uint64_t now = clock_monotonic64();
	struct scheduler *sc = env->scheduler;
	struct srzone *zone = sr_zoneof(env);
	int rc;
	tt_pthread_mutex_lock(&sc->lock);

	if (sc->age_in_progress) {
		/* Stop periodic aging */
		bool age_in_progress = false;
		for (int i = 0; i < sc->count; i++) {
			if (sc->indexes[i]->age_in_progress) {
				age_in_progress = true;
				break;
			}
		}
		if (!age_in_progress) {
			sc->age_in_progress = false;
			sc->age_time = now;
		}
	} else if (zone->branch_prio && zone->branch_age_period &&
		   (now - sc->age_time) >= zone->branch_age_period_us &&
		   sc->count > 0) {
		/* Start periodic aging */
		sc->age_in_progress = true;
		for (int i = 0; i < sc->count; i++) {
			sc->indexes[i]->age_in_progress = true;
		}
	}

	if (sc->gc_in_progress) {
		/* Stop periodic GC */
		bool gc_in_progress = false;
		for (int i = 0; i < sc->count; i++) {
			if (sc->indexes[i]->gc_in_progress) {
				gc_in_progress = true;
				break;
			}
		}
		if (!gc_in_progress) {
			sc->gc_in_progress = false;
			sc->gc_time = now;
		}
	} else if (zone->gc_prio && zone->gc_period &&
		   ((now - sc->gc_time) >= zone->gc_period_us) &&
		   sc->count > 0) {
		/* Start periodic GC */
		sc->gc_in_progress = true;
		for (int i = 0; i < sc->count; i++) {
			sc->indexes[i]->gc_in_progress = true;
		}
	}

	if (sc->checkpoint_in_progress) {
		bool checkpoint_in_progress = false;
		for (int i = 0; i < sc->count; i++) {
			if (sc->indexes[i]->checkpoint_in_progress) {
				checkpoint_in_progress = true;
				break;
			}
		}
		if (!checkpoint_in_progress) {
			sc->checkpoint_in_progress = false;
			sc->checkpoint_lsn_last = sc->checkpoint_lsn;
			sc->checkpoint_lsn = 0;
		}
	}

	/* Get task */
	struct vy_task task;
	rc = vy_plan(sc, zone, vlsn, &task);
	tt_pthread_mutex_unlock(&sc->lock);
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

static int
txlog_flush(struct txlog *log, uint64_t lsn, enum vinyl_status status)
{
	struct vy_bufiter iter;
	vy_bufiter_open(&iter, &log->buf, sizeof(struct txv *));
	for (; vy_bufiter_has(&iter); vy_bufiter_next(&iter))
	{
		struct txv *v = * (struct txv **) vy_bufiter_get(&iter);
		v->tuple->lsn = lsn;
	}

	/* index */
	uint64_t now = clock_monotonic64();
	struct txlogindex *i;
	rlist_foreach_entry(i, &log->index, next)
		si_write(i, now, status);
	return 0;
}

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
		.branch_prio       = 1,
		.branch_wm         = 10 * 1024 * 1024,
		.branch_age        = 40,
		.branch_age_period = 40,
		.branch_age_wm     = 1 * 1024 * 1024,
		.gc_prio           = 1,
		.gc_period         = 60,
		.gc_wm             = 30,
	};
	struct srzone redzone = {
		.enable            = 1,
		.compact_wm        = 4,
		.branch_prio       = 0,
		.branch_wm         = 0,
		.branch_age        = 0,
		.branch_age_period = 0,
		.branch_age_wm     = 0,
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
	z->branch_prio = cfg_geti("vinyl.branch_prio");
	z->branch_age = cfg_geti("vinyl.branch_age");
	z->branch_age_period = cfg_geti("vinyl.branch_age_period");
	z->branch_age_wm = cfg_geti("vinyl.branch_age_wm");

	/* convert periodic times from sec to usec */
	for (int i = 0; i < 11; i++) {
		z = &conf->zones.zones[i];
		z->branch_age_period_us = z->branch_age_period * 1000000;
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
sr_zoneof(struct vinyl_env *env)
{
	int p = vy_quota_used_percent(env->quota);
	return sr_zonemap(&env->conf->zones, p);
}

int
vinyl_index_read(struct vinyl_index*, struct vinyl_tuple*, enum vinyl_order order,
		struct vinyl_tuple **, struct vinyl_tx*, struct sicache*,
		bool cache_only, struct vy_stat_get *);
static int vinyl_index_visible(struct vinyl_index*, uint64_t);
static int vinyl_index_recoverbegin(struct vinyl_index*);
static int vinyl_index_recoverend(struct vinyl_index*);

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
	struct vinyl_env *env = info->env;
	vy_info_append_u64(node, "used", vy_quota_used(env->quota));
	vy_info_append_u64(node, "limit", env->conf->memory_limit);
	return 0;
}

static inline int
vy_info_append_compaction(struct vy_info *info, struct vy_info_node *root)
{
	int childs_cnt = 0;
	struct vinyl_env *env = info->env;
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
		vy_info_append_u32(local_node, "branch_wm", z->branch_wm);
		vy_info_append_u32(local_node, "gc_period", z->gc_period);
		vy_info_append_u32(local_node, "branch_age", z->branch_age);
		vy_info_append_u32(local_node, "compact_wm", z->compact_wm);
		vy_info_append_u32(local_node, "branch_prio", z->branch_prio);
		vy_info_append_u32(local_node, "branch_age_wm", z->branch_age_wm);
		vy_info_append_u32(local_node, "branch_age_period", z->branch_age_period);
	}
	return 0;
}

static inline int
vy_info_append_scheduler(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_info_node *node = vy_info_append(root, "scheduler");
	if (vy_info_reserve(info, node, 3) != 0)
		return 1;

	struct vinyl_env *env = info->env;
	int v = vy_quota_used_percent(env->quota);
	struct srzone *z = sr_zonemap(&env->conf->zones, v);
	vy_info_append_str(node, "zone", z->name);

	struct scheduler *scheduler = env->scheduler;
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

	struct vinyl_env *env = info->env;
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
	vy_info_append_u32(node, "tx_gc_queue", env->xm->count_gc);
	vy_info_append_u32(node, "tx_active_rw", env->xm->count_rw);
	vy_info_append_u32(node, "tx_active_ro", env->xm->count_rd);
	vy_info_append_str(node, "get_read_disk", stat->get_read_disk.sz);
	vy_info_append_u64(node, "documents_used", stat->v_allocated);
	vy_info_append_str(node, "delete_latency", stat->del_latency.sz);
	vy_info_append_str(node, "upsert_latency", stat->upsert_latency.sz);
	vy_info_append_str(node, "get_read_cache", stat->get_read_cache.sz);
	vy_info_append_str(node, "cursor_latency", stat->cursor_latency.sz);
	vy_info_append_str(node, "cursor_read_disk", stat->cursor_read_disk.sz);
	vy_info_append_str(node, "cursor_read_cache", stat->cursor_read_cache.sz);
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
	struct vinyl_index *o;
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
		vy_info_append_u32(local_node, "branch_avg", o->rtp.total_branch_avg);
		vy_info_append_u32(local_node, "branch_max", o->rtp.total_branch_max);
		vy_info_append_u64(local_node, "memory_used", o->rtp.memory_used);
		vy_info_append_u32(local_node, "branch_count", o->rtp.total_branch_count);
		vy_info_append_u32(local_node, "temperature_avg", o->rtp.temperature_avg);
		vy_info_append_u32(local_node, "temperature_min", o->rtp.temperature_min);
		vy_info_append_u32(local_node, "temperature_max", o->rtp.temperature_max);
		vy_info_append_str(local_node, "branch_histogram", o->rtp.histogram_branch_ptr);
		vy_info_append_u64(local_node, "size_uncompressed", o->rtp.total_node_origin_size);
	}
	return 0;
}

int
vy_info_create(struct vy_info *info, struct vinyl_env *e)
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

struct vinyl_cursor {
	struct vinyl_index *index;
	struct vinyl_tuple *key;
	enum vinyl_order order;
	struct vinyl_tx tx;
	int ops;
	int read_disk;
	int read_cache;
	struct sicache *cache;
};

struct vinyl_cursor *
vinyl_cursor_new(struct vinyl_index *index, const char *key,
		 uint32_t part_count, enum vinyl_order order)
{
	struct vinyl_env *e = index->env;
	struct vinyl_cursor *c = mempool_alloc(&e->cursor_pool);
	if (unlikely(c == NULL)) {
		diag_set(OutOfMemory, sizeof(*c), "cursor", "struct");
		return NULL;
	}
	vinyl_index_ref(index);
	c->index = index;
	c->ops = 0;
	c->read_disk = 0;
	c->read_cache = 0;
	c->key = vinyl_tuple_from_key_data(index, key, part_count);
	if (c->key == NULL)
		goto error_1;
	c->order = order;
	c->cache = vy_cachepool_pop(e->cachepool);
	if (unlikely(c->cache == NULL))
		goto error_2;

	tx_begin(e->xm, &c->tx, VINYL_TX_RO);
	return c;

error_2:
	vinyl_tuple_unref(index, c->key);
error_1:
	vinyl_index_unref(index);
	mempool_free(&e->cursor_pool, c);
	return NULL;
}

void
vinyl_cursor_delete(struct vinyl_cursor *c)
{
	struct vinyl_env *e = c->index->env;
	tx_rollback(&c->tx);
	if (c->cache)
		vy_cachepool_push(c->cache);
	if (c->key)
		vinyl_tuple_unref(c->index, c->key);
	vinyl_index_unref(c->index);
	vy_stat_cursor(e->stat, c->tx.start,
	              c->read_disk,
	              c->read_cache,
	              c->ops);
	TRASH(c);
	mempool_free(&e->cursor_pool, c);
}

static int
vinyl_cursor_next(struct vinyl_cursor *c, struct vinyl_tuple **result,
		  bool cache_only)
{
	struct vinyl_index *index = c->index;
	struct vinyl_tx *tx = &c->tx;

	struct vy_stat_get statget;
	assert(c->key != NULL);
	if (vinyl_index_read(index, c->key, c->order, result, tx, c->cache,
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

/*** }}} Cursor */

static int
vy_index_conf_create(struct vy_index_conf *conf, struct key_def *key_def)
{
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 ":%" PRIu32,
	         key_def->space_id, key_def->iid);
	conf->name = strdup(name);
	if (conf->name == NULL) {
		vy_oom();
		goto error;
	}
	conf->sync                  = cfg_geti("vinyl.sync");

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
		conf->compression = 1;
	} else {
		conf->compression = 0;
		conf->compression_if = NULL;
		conf->compression_sz = strdup("none");
	}
	if (conf->compression_sz == NULL) {
		vy_oom();
		goto error;
	}

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
		vy_oom();
		goto error;
	}
	conf->buf_gc_wm             = 1024 * 1024;

	return 0;
error:
	return -1;
}

int
vinyl_index_open(struct vinyl_index *index)
{
	struct vinyl_env *e = index->env;
	int status = vy_status(&index->status);
	if (status == VINYL_FINAL_RECOVERY)
		goto online;
	if (status != VINYL_OFFLINE)
		return -1;
	int rc = vinyl_index_recoverbegin(index);
	if (unlikely(rc == -1))
		return -1;

online:
	vinyl_index_recoverend(index);
	rc = vy_scheduler_add_index(e->scheduler, index);
	if (unlikely(rc == -1))
		return -1;
	return 0;
}

static void
vinyl_index_ref(struct vinyl_index *index)
{
	tt_pthread_mutex_lock(&index->ref_lock);
	index->refs++;
	tt_pthread_mutex_unlock(&index->ref_lock);
}

static void
vinyl_index_unref(struct vinyl_index *index)
{
	/* reduce reference counter */
	tt_pthread_mutex_lock(&index->ref_lock);
	assert(index->refs > 0);
	--index->refs;
	tt_pthread_mutex_unlock(&index->ref_lock);
	/* index will be deleted by scheduler if ref == 0 */
}

int
vinyl_index_close(struct vinyl_index *index)
{
	struct vinyl_env *e = index->env;
	int status = vy_status(&index->status);
	if (unlikely(! vy_status_is_active(status)))
		return -1;
	/* set last visible transaction id */
	index->tsn_max = tx_manager_max(e->xm);
	vy_status_set(&index->status, VINYL_SHUTDOWN);
	if (e->status == VINYL_SHUTDOWN || e->status == VINYL_OFFLINE) {
		return vinyl_index_delete(index);
	}
	/* schedule index shutdown or drop */
	vy_scheduler_del_index(e->scheduler, index);
	return 0;
}

int
vinyl_index_drop(struct vinyl_index *index)
{
	struct vinyl_env *e = index->env;
	int status = vy_status(&index->status);
	if (unlikely(! vy_status_is_active(status)))
		return -1;
	int rc = vy_index_dropmark(index);
	if (unlikely(rc == -1))
		return -1;
	/* set last visible transaction id */
	index->tsn_max = tx_manager_max(e->xm);
	vy_status_set(&index->status, VINYL_DROP);
	rlist_del(&index->link);
	if (e->status == VINYL_SHUTDOWN || e->status == VINYL_OFFLINE)
		return vinyl_index_delete(index);
	/* schedule index shutdown or drop */
	vy_scheduler_del_index(e->scheduler, index);
	return 0;
}

int
vinyl_index_read(struct vinyl_index *index, struct vinyl_tuple *key,
		 enum vinyl_order order,
		 struct vinyl_tuple **result, struct vinyl_tx *tx,
		 struct sicache *cache, bool cache_only,
		 struct vy_stat_get *statget)
{
	struct vinyl_env *e = index->env;
	uint64_t start  = clock_monotonic64();

	if (! vy_status_online(&index->status)) {
		vy_error("%s", "index is not online");
		return -1;
	}

	key->flags = SVGET;
	struct vinyl_tuple *vup = NULL;

	/* concurrent */
	if (tx != NULL && order == VINYL_EQ) {
		int rc = tx_get(tx, &index->coindex, key, &vup);
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
	}

	/* prepare read cache */
	int cachegc = 0;
	if (cache == NULL) {
		cachegc = 1;
		cache = vy_cachepool_pop(e->cachepool);
		if (unlikely(cache == NULL)) {
			if (vup != NULL) {
				vinyl_tuple_unref(index, vup);
			}
			vy_oom();
			return -1;
		}
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
	if (vup != NULL) {
		sv_from_tuple(&sv_vup, vup);
		q.upsert_v = &sv_vup;
	}
	q.upsert_eq = upsert_eq;
	q.cache_only = cache_only;
	assert(q.order != VINYL_EQ);
	int rc = si_range(&q);
	si_readclose(&q);

	if (vup != NULL) {
		vinyl_tuple_unref(index, vup);
	}
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
	statget->read_disk = q.read_disk;
	statget->read_cache = q.read_cache;
	statget->read_latency = clock_monotonic64() - start;

	*result = q.result;
	return 0;
}

struct vinyl_index *
vinyl_index_new(struct vinyl_env *e, struct key_def *key_def,
		struct tuple_format *tuple_format)
{
	assert(key_def->part_count > 0);
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 ":%" PRIu32,
	         key_def->space_id, key_def->iid);
	struct vinyl_index *dup = vinyl_index_by_name(e, name);
	if (unlikely(dup)) {
		vy_error("index '%s' already exists", name);
		return NULL;
	}
	struct vinyl_index *index = malloc(sizeof(struct vinyl_index));
	if (unlikely(index == NULL)) {
		vy_oom();
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
	 * Create field_id -> part_id mapping used by vinyl_tuple_from_data().
	 * This code partially duplicates tuple_format_new() logic.
	 */
	uint32_t key_map_size = 0;
	for (uint32_t part_id = 0; part_id < key_def->part_count; part_id++) {
		uint32_t field_id = key_def->parts[part_id].fieldno;
		key_map_size = MAX(key_map_size, field_id + 1);
	}
	index->key_map = calloc(key_map_size, sizeof(*index->key_map));
	if (index->key_map == NULL)
		goto error_4;
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
	tx_index_init(&index->coindex, index, index->key_def);
	index->tsn_min = tx_manager_min(e->xm);
	index->tsn_max = UINT64_MAX;
	rlist_add(&e->indexes, &index->link);
	vy_workers_start(e);
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

static inline int
vinyl_index_delete(struct vinyl_index *index)
{
	struct vinyl_env *e = index->env;
	tx_index_free(&index->coindex, e->xm);
	int rc_ret = 0;
	int rc = 0;
	struct vy_range *node, *n;
	rlist_foreach_entry_safe(node, &index->gc, gc, n) {
		rc = vy_range_free(node, index->env, 1);
		if (unlikely(rc == -1))
			rc_ret = -1;
	}
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

static int vinyl_index_visible(struct vinyl_index *index, uint64_t tsn)
{
	return tsn > index->tsn_min && tsn <= index->tsn_max;
}

size_t
vinyl_index_bsize(struct vinyl_index *index)
{
	vy_profiler_begin(&index->rtp, index);
	vy_profiler_(&index->rtp);
	vy_profiler_end(&index->rtp);
	return index->rtp.memory_used;
}

static int vinyl_index_recoverbegin(struct vinyl_index *index)
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

static int vinyl_index_recoverend(struct vinyl_index *index)
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
vinyl_tuple_size(struct vinyl_tuple *v)
{
	return sizeof(struct vinyl_tuple) + v->size;
}

static struct vinyl_tuple *
vinyl_tuple_alloc(struct vinyl_index *index, uint32_t size)
{
	struct vinyl_tuple *v = malloc(sizeof(struct vinyl_tuple) + size);
	if (unlikely(v == NULL))
		return NULL;
	v->size      = size;
	v->lsn       = 0;
	v->flags     = 0;
	v->refs      = 1;
	/* update runtime statistics */
	struct vinyl_env *env = index->env;
	tt_pthread_mutex_lock(&env->stat->lock);
	env->stat->v_count++;
	env->stat->v_allocated += sizeof(struct vinyl_tuple) + size;
	tt_pthread_mutex_unlock(&env->stat->lock);
	return v;
}

static struct vinyl_tuple *
vinyl_tuple_from_key_data(struct vinyl_index *index, const char *key,
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
	struct vinyl_tuple *tuple = vinyl_tuple_alloc(index, size);
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

static struct vinyl_tuple *
vinyl_tuple_from_data_ex(struct vinyl_index *index,
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
	struct vinyl_tuple *tuple = vinyl_tuple_alloc(index, size);
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
 * Create vinyl_tuple from raw MsgPack data.
 */
static struct vinyl_tuple *
vinyl_tuple_from_data(struct vinyl_index *index,
		      const char *data, const char *data_end)
{
	char *unused;
	return vinyl_tuple_from_data_ex(index, data, data_end, 0, &unused);
}

static const char *
vinyl_tuple_data(struct vinyl_index *index, struct vinyl_tuple *tuple,
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
vinyl_tuple_data_ex(const struct key_def *key_def,
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
vinyl_convert_tuple(struct vinyl_index *index, struct vinyl_tuple *vinyl_tuple)
{
	uint32_t bsize;
	const char *data = vinyl_tuple_data(index, vinyl_tuple, &bsize);
	return box_tuple_new(index->tuple_format, data, data + bsize);
}

static void
vinyl_tuple_ref(struct vinyl_tuple *v)
{
	uint16_t old_refs =
		pm_atomic_fetch_add_explicit(&v->refs, 1,
					     pm_memory_order_relaxed);
	if (old_refs == 0)
		panic("this is broken by design");
}

static int
vinyl_tuple_unref_rt(struct vinyl_env *env, struct vinyl_tuple *v)
{
	uint16_t old_refs = pm_atomic_fetch_sub_explicit(&v->refs, 1,
		pm_memory_order_relaxed);
	assert(old_refs > 0);
	if (likely(old_refs == 1)) {
		uint32_t size = vinyl_tuple_size(v);
		/* update runtime statistics */
		tt_pthread_mutex_lock(&env->stat->lock);
		assert(env->stat->v_count > 0);
		assert(env->stat->v_allocated >= size);
		env->stat->v_count--;
		env->stat->v_allocated -= size;
		tt_pthread_mutex_unlock(&env->stat->lock);
#ifndef NDEBUG
		memset(v, '#', vinyl_tuple_size(v)); /* fail early */
#endif
		free(v);
		return 1;
	}
	return 0;
}

static void
vinyl_tuple_unref(struct vinyl_index *index, struct vinyl_tuple *tuple)
{
	struct vinyl_env *env = index->env;
	vinyl_tuple_unref_rt(env, tuple);
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
		    const char *ops, const char *ops_end)
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
					      &size, index_base);
		if (result != NULL) {
			/* if failed, just skip it and leave tuple the same */
			*tuple = result;
			*tuple_end = result + size;
		}
		ops = serie_end;
	}
}

/*
 * Get the upserted tuple by upsert tuple and original tuple
 */
static struct vinyl_tuple *
vy_apply_upsert(struct sv *upsert, struct sv *object, struct vinyl_index *index)
{
	assert(upsert != object);
	struct key_def *key_def = index->key_def;
	const char *u_data = sv_pointer(upsert);
	const char *u_data_end = u_data + sv_size(upsert);
	const char *u_tuple, *u_tuple_end, *u_ops, *u_ops_end;
	vinyl_tuple_data_ex(key_def, u_data, u_data_end, &u_tuple, &u_tuple_end,
			    &u_ops, &u_ops_end);
	if (object == NULL || (sv_flags(object) & SVDELETE)) {
		/* replace version */
		struct vinyl_tuple *result =
			vinyl_tuple_from_data(index, u_tuple, u_tuple_end);
		return result;
	}
	const char *o_data = sv_pointer(object);
	const char *o_data_end = o_data + sv_size(object);
	const char *o_tuple, *o_tuple_end, *o_ops, *o_ops_end;
	vinyl_tuple_data_ex(key_def, o_data, o_data_end, &o_tuple, &o_tuple_end,
			    &o_ops, &o_ops_end);
	vy_apply_upsert_ops(&o_tuple, &o_tuple_end, u_ops, u_ops_end);
	if (!(sv_flags(object) & SVUPSERT)) {
		/* update version */
		assert(o_ops_end - o_ops == 0);
		struct vinyl_tuple *result =
			vinyl_tuple_from_data(index, o_tuple, o_tuple_end);
		fiber_gc();
		return result;
	}
	/* upsert of upsert .. */
	assert(o_ops_end - o_ops > 0);
	uint64_t ops_series_count = mp_decode_uint(&u_ops) +
				    mp_decode_uint(&o_ops);
	uint32_t total_ops_size = mp_sizeof_uint(ops_series_count) +
				  (u_ops_end - u_ops) + (o_ops_end - o_ops);
	char *extra;
	struct vinyl_tuple *result =
		vinyl_tuple_from_data_ex(index, o_tuple, o_tuple_end,
					 total_ops_size, &extra);
	extra = mp_encode_uint(extra, ops_series_count);
	memcpy(extra, o_ops, o_ops_end - o_ops);
	extra += o_ops_end - o_ops;
	memcpy(extra, u_ops, u_ops_end - u_ops);
	result->flags = SVUPSERT;
	fiber_gc();
	return result;
}

/* }}} Upsert */

static inline int
vy_tx_write(struct vinyl_tx *tx, struct vinyl_index *index,
	    struct vinyl_tuple *o, uint8_t flags)
{
	assert(tx->state == VINYL_TX_READY);

	/* validate index status */
	int status = vy_status(&index->status);
	switch (status) {
	case VINYL_DROP:
		if (unlikely(! vinyl_index_visible(index, tx->tsn))) {
			vy_error("%s", "index is invisible for the transaction");
			return -1;
		}
		break;
	case VINYL_INITIAL_RECOVERY:
	case VINYL_FINAL_RECOVERY:
	case VINYL_ONLINE: break;
	default:
		return vy_error("%s", "index in malfunction state");
	}

	o->flags = flags;
	/** Drop log_read counter */
	tx->log_read = -1;

	/* concurrent index only */
	return tx_set(tx, &index->coindex, o);
}

static inline void
vy_tx_end(struct vy_stat *stat, struct vinyl_tx *tx, int rlb, int conflict)
{
	uint32_t count = txlog_count(&tx->log);
	struct tx_manager *m = tx->manager;
	struct vy_bufiter iter;
	vy_bufiter_open(&iter, &tx->log.buf, sizeof(struct txv *));
	for (; vy_bufiter_has(&iter); vy_bufiter_next(&iter))
	{
		struct txv *v = * (struct txv **) vy_bufiter_get(&iter);
		/** Gets are collected by gc */
		if (v->tuple->flags & SVGET) {
			v->gc = m->gc;
			/*
			 * We will destroy tx, ensure the txv
			 * doesn't refer to it.
			 */
			rlist_del(&v->next_in_index);
			m->gc = v;
			m->count_gc++;
		} else {
			txv_untrack(v);
			txv_delete(m->env, v);
		}
	}
	vy_stat_tx(stat, tx->start, count, rlb, conflict);
	tx_delete(tx);
}

/* {{{ Public API of transaction control: start/end transaction,
 * read, write data in the context of a transaction.
 */

int
vinyl_replace(struct vinyl_tx *tx, struct vinyl_index *index,
	      const char *tuple, const char *tuple_end)
{
	struct vinyl_tuple *vytuple = vinyl_tuple_from_data(index,
		tuple, tuple_end);
	if (vytuple == NULL)
		return -1;
	int rc = vy_tx_write(tx, index, vytuple, 0);
	vinyl_tuple_unref(index, vytuple);
	return rc;
}

int
vinyl_upsert(struct vinyl_tx *tx, struct vinyl_index *index,
	    const char *tuple, const char *tuple_end,
	    const char *expr, const char *expr_end, int index_base)
{
	assert(index_base == 0 || index_base == 1);
	uint32_t extra_size = (expr_end - expr) +
			      mp_sizeof_uint(1) + mp_sizeof_uint(index_base);
	char *extra;
	struct vinyl_tuple *vinyl_tuple =
		vinyl_tuple_from_data_ex(index, tuple, tuple_end,
					 extra_size, &extra);
	if (vinyl_tuple == NULL) {
		return -1;
	}
	extra = mp_encode_uint(extra, 1); /* 1 upsert ops record */
	extra = mp_encode_uint(extra, index_base);
	memcpy(extra, expr, expr_end - expr);
	int rc = vy_tx_write(tx, index, vinyl_tuple, SVUPSERT);
	vinyl_tuple_unref(index, vinyl_tuple);
	return rc;
}

int
vinyl_delete(struct vinyl_tx *tx, struct vinyl_index *index,
	     const char *key, uint32_t part_count)
{
	struct vinyl_tuple *vykey =
		vinyl_tuple_from_key_data(index, key, part_count);
	int rc = vy_tx_write(tx, index, vykey, SVDELETE);
	vinyl_tuple_unref(index, vykey);
	return rc;
}

static inline int
vy_get(struct vinyl_tx *tx, struct vinyl_index *index, struct vinyl_tuple *key,
	 struct vinyl_tuple **result, bool cache_only)
{
	struct vy_stat_get statget;
	if (vinyl_index_read(index, key, VINYL_EQ, result, tx, NULL,
			    cache_only, &statget) != 0) {
		return -1;
	}

	if (*result == NULL)
		return 0;

	vy_stat_get(index->env->stat, &statget);
	return 0;
}

int
vinyl_rollback(struct vinyl_env *e, struct vinyl_tx *tx)
{
	tx_rollback(tx);
	vy_tx_end(e->stat, tx, 1, 0);
	return 0;
}

int
vinyl_prepare(struct vinyl_env *e, struct vinyl_tx *tx)
{
	if (unlikely(! vy_status_is_active(e->status)))
		return -1;

	/* prepare transaction */
	assert(tx->state == VINYL_TX_READY);

	enum tx_state s = tx_prepare(tx);

	if (s == VINYL_TX_ROLLBACK) {
		return 1;
	}

	struct tx_manager *m = tx->manager;
	struct vy_bufiter i;
	vy_bufiter_open(&i, &tx->log.buf, sizeof(struct txv *));
	uint64_t csn = m->tsn;
	for (; vy_bufiter_has(&i); vy_bufiter_next(&i))
	{
		struct txv *v = *(struct txv **) vy_bufiter_get(&i);
		if (v->log_read == tx->log_read)
			break;
		struct txv *prev = txv_prev(v);
		/* abort conflict reader */
		if (prev && !txv_committed(prev)) {
			assert(prev->tuple->flags & SVGET);
			txv_abort(prev);
		}
		/* abort waiters */
		txv_abort_all(txv_next(v));
		/* mark statement as committed */
		txv_commit(v, csn);
	}

	/* rollback latest reads */
	tx_rollback_svp(tx, &i);

	tx_promote(tx, VINYL_TX_COMMIT);
	tx_end(tx);
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
vinyl_commit(struct vinyl_env *e, struct vinyl_tx *tx, int64_t lsn)
{
	if (unlikely(! vy_status_is_active(e->status)))
		return -1;
	assert(tx->state == VINYL_TX_COMMIT);

	vy_sequence_lock(e->seq);
	if (lsn > (int64_t) e->seq->lsn)
		e->seq->lsn = lsn;
	vy_sequence_unlock(e->seq);
	/* do log write and backend commit */
	int rc = txlog_flush(&tx->log, lsn, e->status);
	if (unlikely(rc == -1))
		tx_rollback(tx);

	vy_tx_end(e->stat, tx, 0, 0);
	return rc;
}

struct vinyl_tx *
vinyl_begin(struct vinyl_env *e)
{
	struct vinyl_tx *tx;
	tx = malloc(sizeof(struct vinyl_tx));
	if (unlikely(tx == NULL)) {
		vy_oom();
		return NULL;
	}
	tx_begin(e->xm, tx, VINYL_TX_RW);
	return tx;
}

/* }}} Public API of transaction control */

/** {{{ vy_read_task - Asynchronous get/cursor I/O using eio pool */

/**
 * A context of asynchronous index get or cursor read.
 */
struct vy_read_task {
	struct coio_task base;
	struct vinyl_index *index;
	struct vinyl_cursor *cursor;
	struct vinyl_tx *tx;
	struct vinyl_tuple *key;
	struct vinyl_tuple *result;
};

static ssize_t
vy_get_cb(struct coio_task *ptr)
{
	struct vy_read_task *task = (struct vy_read_task *) ptr;
	return vy_get(task->tx, task->index, task->key, &task->result, false);
}

static ssize_t
vy_cursor_next_cb(struct coio_task *ptr)
{
	struct vy_read_task *task = (struct vy_read_task *) ptr;
	return vinyl_cursor_next(task->cursor, &task->result, false);
}

static ssize_t
vy_read_task_free_cb(struct coio_task *ptr)
{
	struct vy_read_task *task = (struct vy_read_task *) ptr;
	struct vinyl_env *env = task->index->env;
	assert(env != NULL);
	if (task->result != NULL)
		vinyl_tuple_unref(task->index, task->result);
	vinyl_index_unref(task->index);
	mempool_free(&env->read_task_pool, task);
	return 0;
}

/**
 * Create a thread pool task to run a callback,
 * execute the task, and return the result (tuple) back.
 */
static inline int
vy_read_task(struct vinyl_index *index, struct vinyl_tx *tx,
	     struct vinyl_cursor *cursor, struct vinyl_tuple *key,
	     struct vinyl_tuple **result,
	     coio_task_cb func)
{
	assert(index != NULL);
	struct vinyl_env *env = index->env;
	assert(env != NULL);
	struct vy_read_task *task = mempool_alloc(&env->read_task_pool);
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task), "mempool", "vy_read_task");
		return -1;
	}
	task->index = index;
	vinyl_index_ref(index);
	task->tx = tx;
	task->cursor = cursor;
	task->key = key;
	task->result = NULL;
	if (coio_task(&task->base, func, vy_read_task_free_cb,
	              TIMEOUT_INFINITY) == -1) {
		return -1;
	}
	vinyl_index_unref(index);
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
vinyl_coget(struct vinyl_tx *tx, struct vinyl_index *index, const char *key,
	    uint32_t part_count, struct tuple **result)
{
	struct vinyl_tuple *vykey =
		vinyl_tuple_from_key_data(index, key, part_count);
	if (vykey == NULL)
		return -1;

	struct vinyl_tuple *vyresult = NULL;
	int rc = vy_get(tx, index, vykey, &vyresult, true);
	if (rc == 0 && vyresult == NULL) { /* cache miss or not found */
		rc = vy_read_task(index, tx, NULL, vykey, &vyresult, vy_get_cb);
	}
	vinyl_tuple_unref(index, vykey);
	if (rc != 0)
		return -1;

	if (vyresult == NULL) { /* not found */
		*result = NULL;
		return 0;
	}

	/* found */
	*result = vinyl_convert_tuple(index, vyresult);
	vinyl_tuple_unref(index, vyresult);
	if (*result == NULL)
		return -1;
	return 0;
}

/**
 * Read the next value from a cursor in a thread pool thread.
 */
int
vinyl_cursor_conext(struct vinyl_cursor *cursor, struct tuple **result)
{
	struct vinyl_tuple *vyresult;
	int rc = vinyl_cursor_next(cursor, &vyresult, true);
	if (rc == 0 && vyresult == NULL) { /* cache miss or not found */
		rc = vy_read_task(cursor->index, NULL, cursor, NULL, &vyresult,
				  vy_cursor_next_cb);
	}
	if (rc != 0)
		return -1;

	if (vyresult == NULL) { /* not found */
		*result = NULL;
		return 0;
	}

	/* found */
	*result = vinyl_convert_tuple(cursor->index, vyresult);
	vinyl_tuple_unref(cursor->index, vyresult);
	if (*result == NULL)
		return -1;
	return 0;
}

/** }}} vy_read_task */

/** {{{ Environment */

struct vinyl_env *
vinyl_env_new(void)
{
	struct vinyl_env *e = malloc(sizeof(*e));
	if (unlikely(e == NULL))
		return NULL;
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
	e->scheduler = scheduler_new(e);
	if (e->scheduler == NULL)
		goto error_6;
	e->stat = vy_stat_new();
	if (e->stat == NULL)
		goto error_7;

	mempool_create(&e->read_task_pool, cord_slab_cache(),
	               sizeof(struct vy_read_task));
	mempool_create(&e->cursor_pool, cord_slab_cache(),
	               sizeof(struct vinyl_cursor));
	return e;
error_7:
	scheduler_delete(e->scheduler);
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

int
vinyl_env_delete(struct vinyl_env *e)
{
	int rcret = 0;
	e->status = VINYL_SHUTDOWN;
	vy_workers_stop(e);
	/* TODO: tarantool doesn't delete indexes during shutdown */
	//assert(rlist_empty(&e->db));
	int rc;
	struct vinyl_index *index, *next;
	rlist_foreach_entry_safe(index, &e->scheduler->shutdown, link, next) {
		rc = vinyl_index_delete(index);
		if (unlikely(rc == -1))
			rcret = -1;
	}
	tx_manager_delete(e->xm);
	vy_cachepool_delete(e->cachepool);
	vy_conf_delete(e->conf);
	vy_quota_delete(e->quota);
	vy_stat_delete(e->stat);
	vy_sequence_delete(e->seq);
	scheduler_delete(e->scheduler);
	mempool_destroy(&e->read_task_pool);
	mempool_destroy(&e->cursor_pool);
	free(e);
	return rcret;
}

/** }}} Environment */

/** {{{ Recovery */

void
vinyl_bootstrap(struct vinyl_env *e)
{
	assert(e->status == VINYL_OFFLINE);
	e->status = VINYL_ONLINE;
	/* enable quota */
	vy_quota_enable(e->quota);
}

void
vinyl_begin_initial_recovery(struct vinyl_env *e)
{
	assert(e->status == VINYL_OFFLINE);
	e->status = VINYL_INITIAL_RECOVERY;
}

void
vinyl_begin_final_recovery(struct vinyl_env *e)
{
	assert(e->status == VINYL_INITIAL_RECOVERY);
	e->status = VINYL_FINAL_RECOVERY;
}

void
vinyl_end_recovery(struct vinyl_env *e)
{
	assert(e->status == VINYL_FINAL_RECOVERY);
	e->status = VINYL_ONLINE;
	/* enable quota */
	vy_quota_enable(e->quota);
}

int
vinyl_checkpoint(struct vinyl_env *env)
{
	/* do not initiate checkpoint during bootstrap,
	 * thread pool is not up yet */
	if (! env->worker_pool_run)
		return 0;
	return sc_ctl_checkpoint(env->scheduler);
}

bool
vinyl_checkpoint_is_active(struct vinyl_env *env)
{
	tt_pthread_mutex_lock(&env->scheduler->lock);
	bool is_active = env->scheduler->checkpoint_in_progress;
	tt_pthread_mutex_unlock(&env->scheduler->lock);
	return is_active;
}

/** }}} Recovery */

/** {{{ Replication */

int
vy_index_send(struct vinyl_index *index, vy_send_row_f sendrow, void *ctx)
{
	double start_time = ev_now(loop());
	int read_disk = 0;
	int read_cache = 0;
	int ops = 0;
	int64_t vlsn = UINT64_MAX;

	struct sicache *cache = vy_cachepool_pop(index->env->cachepool);
	if (cache == NULL)
		return -1;

	vinyl_index_ref(index);

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
		struct vinyl_tuple *tuple = q.result;
		assert(tuple != NULL); /* found */
		uint32_t mp_size;
		const char *mp = vinyl_tuple_data(index, tuple, &mp_size);
		int64_t lsn = tuple->lsn;
		rc = sendrow(ctx, mp, mp_size, lsn);
		if (rc != 0) {
			vinyl_tuple_unref(index, tuple);
			break;
		}

		/* Next iteration */
		si_readopen(&q, index, cache, VINYL_GT, vlsn, tuple->data,
			    tuple->size);
		assert(!q.cache_only);
		rc = si_range(&q);
		si_readclose(&q);
		vinyl_tuple_unref(index, tuple);
	}

	vy_cachepool_push(cache);
	vy_stat_cursor(index->env->stat, start_time, read_disk, read_cache, ops);
	vinyl_index_unref(index);
	return rc;
}

/* }}} replication */

/** {{{ vinyl_service - context of a vinyl background thread */

static void*
vinyl_worker(void *arg)
{
	struct vinyl_env *env = (struct vinyl_env *) arg;
	struct sdc sdc;
	sd_cinit(&sdc);
	while (pm_atomic_load_explicit(&env->worker_pool_run,
				       pm_memory_order_relaxed)) {
		int rc = 0;
		if (vy_status_is_active(env->status)) {
			int64_t vlsn = vy_sequence(env->seq, VINYL_VIEW_LSN);
			rc = sc_schedule(env, &sdc, vlsn);
		}
		if (rc == -1)
			break;
		if (rc == 0)
			usleep(10000); /* 10ms */
	}
	sd_cfree(&sdc);
	return NULL;
}

static void
vy_workers_start(struct vinyl_env *env)
{
	if (env->worker_pool_run)
		return;
	/* prepare worker pool */
	env->worker_pool = NULL;
	env->worker_pool_size = cfg_geti("vinyl.threads");
	if (env->worker_pool_size < 0)
		env->worker_pool_size = 1;
	env->worker_pool = (struct cord *)calloc(env->worker_pool_size,
		sizeof(struct cord));
	if (env->worker_pool == NULL)
		panic("failed to allocate vinyl worker pool");
	env->worker_pool_run = 1;
	for (int i = 0; i < env->worker_pool_size; i++)
		cord_start(&env->worker_pool[i], "vinyl", vinyl_worker, env);
}

static void
vy_workers_stop(struct vinyl_env *env)
{
	if (!env->worker_pool_run)
		return;
	pm_atomic_store_explicit(&env->worker_pool_run, 0,
				 pm_memory_order_relaxed);
	for (int i = 0; i < env->worker_pool_size; i++)
		cord_join(&env->worker_pool[i]);
	free(env->worker_pool);
	env->worker_pool = NULL;
	env->worker_pool_size = 0;
}

/* }}} vinyl service */
