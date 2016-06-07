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
#include "phia.h"

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

#include <lz4.h>
#include <lz4frame.h>
#include <zstd_static.h>

#include <bit/bit.h>
#include <small/rlist.h>

#include "trivia/util.h"
#include "crc32.h"
#include "clock.h"
#include "trivia/config.h"
#include "tt_pthread.h"
#include "assoc.h"
#include "cfg.h"

#include "key_def.h"

#include "box/errcode.h"
#include "diag.h"

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

struct ssvfs;

struct ssvfsif {
	int     (*init)(struct ssvfs*, va_list);
	void    (*free)(struct ssvfs*);
	int64_t (*size)(struct ssvfs*, char*);
	int     (*exists)(struct ssvfs*, const char*);
	int     (*unlink)(struct ssvfs*, char*);
	int     (*rename)(struct ssvfs*, char*, char*);
	int     (*mkdir)(struct ssvfs*, const char*, int);
	int     (*rmdir)(struct ssvfs*, char*);
	int     (*open)(struct ssvfs*, char*, int, int);
	int     (*close)(struct ssvfs*, int);
	int     (*sync)(struct ssvfs*, int);
	int     (*advise)(struct ssvfs*, int, int, uint64_t, uint64_t);
	int     (*truncate)(struct ssvfs*, int, uint64_t);
	int64_t (*pread)(struct ssvfs*, int, uint64_t, void*, int);
	int64_t (*pwrite)(struct ssvfs*, int, uint64_t, void*, int);
	int64_t (*write)(struct ssvfs*, int, void*, int);
	int64_t (*writev)(struct ssvfs*, int, struct ssiov*);
	int64_t (*seek)(struct ssvfs*, int, uint64_t);
	int     (*mmap)(struct ssvfs*, struct ssmmap*, int, uint64_t, int);
	int     (*mmap_allocate)(struct ssvfs*, struct ssmmap*, uint64_t);
	int     (*mremap)(struct ssvfs*, struct ssmmap*, uint64_t);
	int     (*munmap)(struct ssvfs*, struct ssmmap*);
};

struct ssvfs {
	struct ssvfsif *i;
	char priv[48];
};

static inline int
ss_vfsinit(struct ssvfs *f, struct ssvfsif *i, ...)
{
	f->i = i;
	va_list args;
	va_start(args, i);
	int rc = i->init(f, args);
	va_end(args);
	return rc;
}

static inline void
ss_vfsfree(struct ssvfs *f)
{
	f->i->free(f);
}

#define ss_vfssize(fs, path)                 (fs)->i->size(fs, path)
#define ss_vfsexists(fs, path)               (fs)->i->exists(fs, path)
#define ss_vfsunlink(fs, path)               (fs)->i->unlink(fs, path)
#define ss_vfsrename(fs, src, dest)          (fs)->i->rename(fs, src, dest)
#define ss_vfsmkdir(fs, path, mode)          (fs)->i->mkdir(fs, path, mode)
#define ss_vfsrmdir(fs, path)                (fs)->i->rmdir(fs, path)
#define ss_vfsopen(fs, path, flags, mode)    (fs)->i->open(fs, path, flags, mode)
#define ss_vfsclose(fs, fd)                  (fs)->i->close(fs, fd)
#define ss_vfssync(fs, fd)                   (fs)->i->sync(fs, fd)
#define ss_vfsadvise(fs, fd, hint, off, len) (fs)->i->advise(fs, fd, hint, off, len)
#define ss_vfstruncate(fs, fd, size)         (fs)->i->truncate(fs, fd, size)
#define ss_vfspread(fs, fd, off, buf, size)  (fs)->i->pread(fs, fd, off, buf, size)
#define ss_vfspwrite(fs, fd, off, buf, size) (fs)->i->pwrite(fs, fd, off, buf, size)
#define ss_vfswrite(fs, fd, buf, size)       (fs)->i->write(fs, fd, buf, size)
#define ss_vfswritev(fs, fd, iov)            (fs)->i->writev(fs, fd, iov)
#define ss_vfsseek(fs, fd, off)              (fs)->i->seek(fs, fd, off)
#define ss_vfsmmap(fs, m, fd, size, ro)      (fs)->i->mmap(fs, m, fd, size, ro)
#define ss_vfsmunmap(fs, m)                  (fs)->i->munmap(fs, m)

static struct ssvfsif ss_stdvfs;

struct PACKED ssfile {
	int fd;
	uint64_t size;
	int creat;
	struct sspath path;
	struct ssvfs *vfs;
};

static inline void
ss_fileinit(struct ssfile *f, struct ssvfs *vfs)
{
	ss_pathinit(&f->path);
	f->vfs   = vfs;
	f->fd    = -1;
	f->size  = 0;
	f->creat = 0;
}

static inline int
ss_fileopen_as(struct ssfile *f, char *path, int flags)
{
	f->creat = (flags & O_CREAT ? 1 : 0);
	f->fd = ss_vfsopen(f->vfs, path, flags, 0644);
	if (unlikely(f->fd == -1))
		return -1;
	ss_pathset(&f->path, "%s", path);
	f->size = 0;
	if (f->creat)
		return 0;
	int64_t size = ss_vfssize(f->vfs, path);
	if (unlikely(size == -1)) {
		ss_vfsclose(f->vfs, f->fd);
		f->fd = -1;
		return -1;
	}
	f->size = size;
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
		int rc = ss_vfsclose(f->vfs, f->fd);
		if (unlikely(rc == -1))
			return -1;
		f->fd  = -1;
		f->vfs = NULL;
	}
	return 0;
}

static inline int
ss_filerename(struct ssfile *f, char *path)
{
	int rc = ss_vfsrename(f->vfs, ss_pathof(&f->path), path);
	if (unlikely(rc == -1))
		return -1;
	ss_pathset(&f->path, "%s", path);
	return 0;
}

static inline int
ss_filesync(struct ssfile *f) {
	return ss_vfssync(f->vfs, f->fd);
}

static inline int
ss_fileadvise(struct ssfile *f, int hint, uint64_t off, uint64_t len) {
	return ss_vfsadvise(f->vfs, f->fd, hint, off, len);
}

static inline int
ss_fileresize(struct ssfile *f, uint64_t size)
{
	int rc = ss_vfstruncate(f->vfs, f->fd, size);
	if (unlikely(rc == -1))
		return -1;
	f->size = size;
	return 0;
}

static inline int
ss_filepread(struct ssfile *f, uint64_t off, void *buf, int size)
{
	int64_t rc = ss_vfspread(f->vfs, f->fd, off, buf, size);
	if (unlikely(rc == -1))
		return -1;
	assert(rc == size);
	return rc;
}

static inline int
ss_filepwrite(struct ssfile *f, uint64_t off, void *buf, int size)
{
	int64_t rc = ss_vfspwrite(f->vfs, f->fd, off, buf, size);
	if (unlikely(rc == -1))
		return -1;
	assert(rc == size);
	return rc;
}

static inline int
ss_filewrite(struct ssfile *f, void *buf, int size)
{
	int64_t rc = ss_vfswrite(f->vfs, f->fd, buf, size);
	if (unlikely(rc == -1))
		return -1;
	assert(rc == size);
	f->size += rc;
	return rc;
}

static inline int
ss_filewritev(struct ssfile *f, struct ssiov *iov)
{
	int64_t rc = ss_vfswritev(f->vfs, f->fd, iov);
	if (unlikely(rc == -1))
		return -1;
	f->size += rc;
	return rc;
}

static inline int
ss_fileseek(struct ssfile *f, uint64_t off)
{
	return ss_vfsseek(f->vfs, f->fd, off);
}

struct ssa;

struct ssaif {
	int   (*open)(struct ssa*, va_list);
	int   (*close)(struct ssa*);
	void *(*malloc)(struct ssa*, size_t);
	void *(*realloc)(struct ssa*, void *, size_t);
	int   (*ensure)(struct ssa*, int, int);
	void  (*free)(struct ssa*, void*);
};

struct ssa {
	struct ssaif *i;
	char priv[48];
};

static inline int
ss_aopen(struct ssa *a, struct ssaif *i, ...) {
	a->i = i;
	va_list args;
	va_start(args, i);
	int rc = i->open(a, args);
	va_end(args);
	return rc;
}

static inline void*
ss_malloc(struct ssa *a, size_t size) {
	return a->i->malloc(a, size);
}

static inline void*
ss_realloc(struct ssa *a, void *ptr, size_t size) {
	return a->i->realloc(a, ptr, size);
}

static inline void
ss_free(struct ssa *a, void *ptr) {
	a->i->free(a, ptr);
}

static inline char*
ss_strdup(struct ssa *a, const char *str) {
	size_t sz = strlen(str) + 1;
	char *s = ss_malloc(a, sz);
	if (unlikely(s == NULL))
		return NULL;
	memcpy(s, str, sz);
	return s;
}

static struct ssaif ss_stda;

struct ssbuf {
	char *reserve;
	char *s, *p, *e;
};

static inline void
ss_bufinit(struct ssbuf *b)
{
	b->reserve = NULL;
	b->s = NULL;
	b->p = NULL;
	b->e = NULL;
}

static inline void
ss_bufinit_reserve(struct ssbuf *b, void *buf, int size)
{
	b->reserve = buf;
	b->s = buf;
	b->p = b->s;
	b->e = b->s + size;
}

static inline void
ss_buffree(struct ssbuf *b, struct ssa *a)
{
	if (unlikely(b->s == NULL))
		return;
	if (unlikely(b->s != b->reserve))
		ss_free(a, b->s);
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
ss_bufgc(struct ssbuf *b, struct ssa *a, size_t wm)
{
	if (unlikely(ss_bufsize(b) >= wm)) {
		ss_buffree(b, a);
		ss_bufinit(b);
		return;
	}
	ss_bufreset(b);
}

static inline int
ss_bufensure(struct ssbuf *b, struct ssa *a, size_t size)
{
	if (likely(b->e - b->p >= (ptrdiff_t)size))
		return 0;
	size_t sz = ss_bufsize(b) * 2;
	size_t actual = ss_bufused(b) + size;
	if (unlikely(actual > sz))
		sz = actual;
	char *p;
	if (unlikely(b->s == b->reserve)) {
		p = ss_malloc(a, sz);
		if (unlikely(p == NULL))
			return -1;
		memcpy(p, b->s, ss_bufused(b));
	} else {
		p = ss_realloc(a, b->s, sz);
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
ss_bufadd(struct ssbuf *b, struct ssa *a, void *buf, size_t size)
{
	int rc = ss_bufensure(b, a, size);
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
	SS_U32REV,
	SS_U64,
	SS_U64REV,
};

enum ssquotaop {
	SS_QADD,
	SS_QREMOVE
};

struct ssquota {
	int enable;
	int wait;
	int64_t limit;
	int64_t used;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

static int ss_quotainit(struct ssquota*);
static int ss_quotaset(struct ssquota*, int64_t);
static int ss_quotaenable(struct ssquota*, int);
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

struct PACKED ssrbnode {
	struct ssrbnode *p, *l, *r;
	uint8_t color;
};

struct PACKED ssrb {
	struct ssrbnode *root;
};

static inline void
ss_rbinit(struct ssrb *t) {
	t->root = NULL;
}

static inline void
ss_rbinitnode(struct ssrbnode *n) {
	n->color = 2;
	n->p = NULL;
	n->l = NULL;
	n->r = NULL;
}

#define ss_rbget(name, compare) \
\
static inline int \
name(struct ssrb *t, \
     void *scheme ssunused, \
     void *key ssunused, int keysize ssunused, \
     struct ssrbnode **match) \
{ \
	struct ssrbnode *n = t->root; \
	*match = NULL; \
	int rc = 0; \
	while (n) { \
		*match = n; \
		switch ((rc = (compare))) { \
		case  0: return 0; \
		case -1: n = n->r; \
			break; \
		case  1: n = n->l; \
			break; \
		} \
	} \
	return rc; \
}

#define ss_rbtruncate(name, executable) \
\
static inline void \
name(struct ssrbnode *n, void *arg) \
{ \
	if (n->l) \
		name(n->l, arg); \
	if (n->r) \
		name(n->r, arg); \
	executable; \
}

static struct ssrbnode *ss_rbmin(struct ssrb*);
static struct ssrbnode *ss_rbmax(struct ssrb*);
static struct ssrbnode *ss_rbnext(struct ssrb*, struct ssrbnode*);
static struct ssrbnode *ss_rbprev(struct ssrb*, struct ssrbnode*);

static void ss_rbset(struct ssrb*, struct ssrbnode*, int, struct ssrbnode*);
static void ss_rbreplace(struct ssrb*, struct ssrbnode*, struct ssrbnode*);
static void ss_rbremove(struct ssrb*, struct ssrbnode*);

struct ssqf {
	uint8_t   qf_qbits;
	uint8_t   qf_rbits;
	uint8_t   qf_elem_bits;
	uint32_t  qf_entries;
	uint64_t  qf_index_mask;
	uint64_t  qf_rmask;
	uint64_t  qf_elem_mask;
	uint64_t  qf_max_size;
	uint32_t  qf_table_size;
	uint64_t *qf_table;
	struct ssbuf     qf_buf;
};

static int  ss_qfinit(struct ssqf*);
static int  ss_qfensure(struct ssqf*, struct ssa*, uint32_t);
static void ss_qffree(struct ssqf*, struct ssa*);
static void ss_qfgc(struct ssqf*, struct ssa*, size_t);
static void ss_qfreset(struct ssqf*);
static void ss_qfrecover(struct ssqf*, int, int, uint32_t, uint64_t*);
static void ss_qfadd(struct ssqf*, uint64_t);
static int  ss_qfhas(struct ssqf*, uint64_t);

static inline unsigned int
ss_fnv(char *key, int len)
{
	unsigned char *p = (unsigned char*)key;
	unsigned char *end = p + len;
	unsigned h = 2166136261;
	while (p < end) {
		h = (h * 16777619) ^ *p;
		p++;
	}
	return h;
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
ss_rqinit(struct ssrq *q, struct ssa *a, uint32_t range, uint32_t count)
{
	q->range_count = count + 1 /* zero */;
	q->range = range;
	q->q = ss_malloc(a, sizeof(struct ssrqq) * q->range_count);
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
ss_rqfree(struct ssrq *q, struct ssa *a)
{
	if (q->q) {
		ss_free(a, q->q);
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
	struct ssa *a;
	char priv[90];
};

static inline int
ss_filterinit(struct ssfilter *c, struct ssfilterif *ci, struct ssa *a, enum ssfilterop op, ...)
{
	c->op = op;
	c->a  = a;
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
		rc = ss_bufensure(dest, f->a, block);
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
		rc = ss_bufensure(dest, f->a, capacity - ss_bufused(dest));
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
		rc = ss_bufensure(dest, f->a, capacity - ss_bufused(dest));
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

/*
 * Quotient Filter.
 *
 * Based on implementation made by Vedant Kumar <vsk@berkeley.edu>
*/

#define ss_qflmask(n) ((1ULL << (n)) - 1ULL)

static void
ss_qfrecover(struct ssqf *f, int q, int r, uint32_t size, uint64_t *table)
{
	f->qf_qbits      = q;
	f->qf_rbits      = r;
	f->qf_elem_bits  = f->qf_rbits + 3;
	f->qf_index_mask = ss_qflmask(q);
	f->qf_rmask      = ss_qflmask(r);
	f->qf_elem_mask  = ss_qflmask(f->qf_elem_bits);
	f->qf_entries    = 0;
	f->qf_max_size   = 1 << q;
	f->qf_table_size = size;
	f->qf_table      = table;
}

static int
ss_qfinit(struct ssqf *f)
{
	memset(f, 0, sizeof(*f));
	ss_bufinit(&f->qf_buf);
	return 0;
}

static int
ss_qfensure(struct ssqf *f, struct ssa *a, uint32_t count)
{
	int q = 6;
	int r = 1;
	while (q < 32) {
		if (count < (1UL << q))
			break;
		q++;
	}
	f->qf_qbits      = q;
	f->qf_rbits      = r;
	f->qf_elem_bits  = f->qf_rbits + 3;
	f->qf_index_mask = ss_qflmask(q);
	f->qf_rmask      = ss_qflmask(r);
	f->qf_elem_mask  = ss_qflmask(f->qf_elem_bits);
	f->qf_entries    = 0;
	f->qf_max_size   = 1 << q;
	f->qf_table_size = ((1 << q) * (r + 3)) / 8;
	if (f->qf_table_size % 8)
		f->qf_table_size++;
	int rc = ss_bufensure(&f->qf_buf, a, f->qf_table_size);
	if (unlikely(rc == -1))
		return -1;
	ss_bufadvance(&f->qf_buf, f->qf_table_size);
	f->qf_table = (uint64_t*)f->qf_buf.s;
	memset(f->qf_table, 0, f->qf_table_size);
	return 0;
}

static void
ss_qffree(struct ssqf *f, struct ssa *a)
{
	if (f->qf_table) {
		ss_buffree(&f->qf_buf, a);
		f->qf_table = NULL;
	}
}

static void
ss_qfgc(struct ssqf *f, struct ssa *a, size_t wm)
{
	if (unlikely(ss_bufsize(&f->qf_buf) >= wm)) {
		ss_buffree(&f->qf_buf, a);
		ss_bufinit(&f->qf_buf);
		return;
	}
	ss_bufreset(&f->qf_buf);
}

static void
ss_qfreset(struct ssqf *f)
{
	memset(f->qf_table, 0, f->qf_table_size);
	ss_bufreset(&f->qf_buf);
	f->qf_entries = 0;
}

static inline uint64_t
ss_qfincr(struct ssqf *f, uint64_t idx) {
	return (idx + 1) & f->qf_index_mask;
}

static inline uint64_t
ss_qfdecr(struct ssqf *f, uint64_t idx) {
	return (idx - 1) & f->qf_index_mask;
}

static inline int
ss_qfoccupied_is(uint64_t elt) {
	return elt & 1;
}

static inline uint64_t
ss_qfoccupied_set(uint64_t elt) {
	return elt | 1;
}

static inline uint64_t
ss_qfoccupied_clr(uint64_t elt) {
	return elt & ~1;
}

static inline int
ss_qfcontinuation_is(uint64_t elt) {
	return elt & 2;
}

static inline uint64_t
ss_qfcontinuation_set(uint64_t elt) {
	return elt | 2;
}

static inline int
ss_qfshifted_is(uint64_t elt) {
	return elt & 4;
}

static inline uint64_t
ss_qfshifted_set(uint64_t elt) {
	return elt | 4;
}

static inline int
ss_qfremainder_of(uint64_t elt) {
	return elt >> 3;
}

static inline int
ss_qfis_empty(uint64_t elt) {
	return (elt & 7) == 0;
}

static inline uint64_t
ss_qfhash_to_q(struct ssqf *f, uint64_t h) {
	return (h >> f->qf_rbits) & f->qf_index_mask;
}

static inline uint64_t
ss_qfhash_to_r(struct ssqf *f, uint64_t h) {
	return h & f->qf_rmask;
}

static inline uint64_t
ss_qfget(struct ssqf *f, uint64_t idx)
{
	size_t bitpos  = f->qf_elem_bits * idx;
	size_t tabpos  = bitpos / 64;
	size_t slotpos = bitpos % 64;
	int spillbits  = (slotpos + f->qf_elem_bits) - 64;
	uint64_t elt;
	elt = (f->qf_table[tabpos] >> slotpos) & f->qf_elem_mask;
	if (spillbits > 0) {
		tabpos++;
		uint64_t x = f->qf_table[tabpos] & ss_qflmask(spillbits);
		elt |= x << (f->qf_elem_bits - spillbits);
	}
	return elt;
}

static inline void
ss_qfset(struct ssqf *f, uint64_t idx, uint64_t elt)
{
	size_t bitpos = f->qf_elem_bits * idx;
	size_t tabpos = bitpos / 64;
	size_t slotpos = bitpos % 64;
	int spillbits = (slotpos + f->qf_elem_bits) - 64;
	elt &= f->qf_elem_mask;
	f->qf_table[tabpos] &= ~(f->qf_elem_mask << slotpos);
	f->qf_table[tabpos] |= elt << slotpos;
	if (spillbits > 0) {
		tabpos++;
		f->qf_table[tabpos] &= ~ss_qflmask(spillbits);
		f->qf_table[tabpos] |= elt >> (f->qf_elem_bits - spillbits);
	}
}

static inline uint64_t
ss_qffind(struct ssqf *f, uint64_t fq)
{
	uint64_t b = fq;
	while (ss_qfshifted_is( ss_qfget(f, b)))
		b = ss_qfdecr(f, b);
	uint64_t s = b;
	while (b != fq) {
		do {
			s = ss_qfincr(f, s);
		} while (ss_qfcontinuation_is(ss_qfget(f, s)));
		do {
			b = ss_qfincr(f, b);
		} while (! ss_qfoccupied_is(ss_qfget(f, b)));
	}
	return s;
}

static inline void
ss_qfinsert(struct ssqf *f, uint64_t s, uint64_t elt)
{
	uint64_t prev;
	uint64_t curr = elt;
	int empty;
	do {
		prev = ss_qfget(f, s);
		empty = ss_qfis_empty(prev);
		if (! empty) {
			prev = ss_qfshifted_set(prev);
			if (ss_qfoccupied_is(prev)) {
				curr = ss_qfoccupied_set(curr);
				prev = ss_qfoccupied_clr(prev);
			}
		}
		ss_qfset(f, s, curr);
		curr = prev;
		s = ss_qfincr(f, s);
	} while (! empty);
}

static inline int
ss_qffull(struct ssqf *f) {
	return f->qf_entries >= f->qf_max_size;
}

static void
ss_qfadd(struct ssqf *f, uint64_t h)
{
	if (unlikely(ss_qffull(f)))
		return;

	uint64_t fq    = ss_qfhash_to_q(f, h);
	uint64_t fr    = ss_qfhash_to_r(f, h);
	uint64_t T_fq  = ss_qfget(f, fq);
	uint64_t entry = (fr << 3) & ~7;

	if (likely(ss_qfis_empty(T_fq))) {
		ss_qfset(f, fq, ss_qfoccupied_set(entry));
		f->qf_entries++;
		return;
	}

	if (! ss_qfoccupied_is(T_fq))
		ss_qfset(f, fq, ss_qfoccupied_set(T_fq));

	uint64_t start = ss_qffind(f, fq);
	uint64_t s = start;

	if (ss_qfoccupied_is(T_fq)) {
		do {
			uint64_t rem = ss_qfremainder_of(ss_qfget(f, s));
			if (rem == fr) {
				return;
			} else if (rem > fr) {
				break;
			}
			s = ss_qfincr(f, s);
		} while (ss_qfcontinuation_is(ss_qfget(f, s)));

		if (s == start) {
			uint64_t old_head = ss_qfget(f, start);
			ss_qfset(f, start, ss_qfcontinuation_set(old_head));
		} else {
			entry = ss_qfcontinuation_set(entry);
		}
	}

	if (s != fq)
		entry = ss_qfshifted_set(entry);

	ss_qfinsert(f, s, entry);
	f->qf_entries++;
}

static int
ss_qfhas(struct ssqf *f, uint64_t h)
{
	uint64_t fq   = ss_qfhash_to_q(f, h);
	uint64_t fr   = ss_qfhash_to_r(f, h);
	uint64_t T_fq = ss_qfget(f, fq);

	if (! ss_qfoccupied_is(T_fq))
		return 0;

	uint64_t s = ss_qffind(f, fq);
	do {
		uint64_t rem = ss_qfremainder_of(ss_qfget(f, s));
		if (rem == fr)
			return 1;
		else
		if (rem > fr)
			return 0;
		s = ss_qfincr(f, s);
	} while (ss_qfcontinuation_is(ss_qfget(f, s)));

	return 0;
}

static int
ss_quotainit(struct ssquota *q)
{
	q->enable = 0;
	q->wait   = 0;
	q->limit  = 0;
	q->used   = 0;
	tt_pthread_mutex_init(&q->lock, NULL);
	tt_pthread_cond_init(&q->cond, NULL);
	return 0;
}

static int
ss_quotaset(struct ssquota *q, int64_t limit)
{
	q->limit = limit;
	return 0;
}

static int
ss_quotaenable(struct ssquota *q, int v)
{
	q->enable = v;
	return 0;
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

#define SS_RBBLACK 0
#define SS_RBRED   1
#define SS_RBUNDEF 2

static struct ssrbnode *
ss_rbmin(struct ssrb *t)
{
	struct ssrbnode *n = t->root;
	if (unlikely(n == NULL))
		return NULL;
	while (n->l)
		n = n->l;
	return n;
}

static struct ssrbnode *
ss_rbmax(struct ssrb *t)
{
	struct ssrbnode *n = t->root;
	if (unlikely(n == NULL))
		return NULL;
	while (n->r)
		n = n->r;
	return n;
}

static struct ssrbnode *
ss_rbnext(struct ssrb *t, struct ssrbnode *n)
{
	if (unlikely(n == NULL))
		return ss_rbmin(t);
	if (n->r) {
		n = n->r;
		while (n->l)
			n = n->l;
		return n;
	}
	struct ssrbnode *p;
	while ((p = n->p) && p->r == n)
		n = p;
	return p;
}

static struct ssrbnode *
ss_rbprev(struct ssrb *t, struct ssrbnode *n)
{
	if (unlikely(n == NULL))
		return ss_rbmax(t);
	if (n->l) {
		n = n->l;
		while (n->r)
			n = n->r;
		return n;
	}
	struct ssrbnode *p;
	while ((p = n->p) && p->l == n)
		n = p;
	return p;
}

static inline void
ss_rbrotate_left(struct ssrb *t, struct ssrbnode *n)
{
	struct ssrbnode *p = n;
	struct ssrbnode *q = n->r;
	struct ssrbnode *parent = n->p;
	if (likely(p->p != NULL)) {
		if (parent->l == p)
			parent->l = q;
		else
			parent->r = q;
	} else {
		t->root = q;
	}
	q->p = parent;
	p->p = q;
	p->r = q->l;
	if (p->r)
		p->r->p = p;
	q->l = p;
}

static inline void
ss_rbrotate_right(struct ssrb *t, struct ssrbnode *n)
{
	struct ssrbnode *p = n;
	struct ssrbnode *q = n->l;
	struct ssrbnode *parent = n->p;
	if (likely(p->p != NULL)) {
		if (parent->l == p)
			parent->l = q;
		else
			parent->r = q;
	} else {
		t->root = q;
	}
	q->p = parent;
	p->p = q;
	p->l = q->r;
	if (p->l)
		p->l->p = p;
	q->r = p;
}

static inline void
ss_rbset_fixup(struct ssrb *t, struct ssrbnode *n)
{
	struct ssrbnode *p;
	while ((p = n->p) && (p->color == SS_RBRED))
	{
		struct ssrbnode *g = p->p;
		if (p == g->l) {
			struct ssrbnode *u = g->r;
			if (u && u->color == SS_RBRED) {
				g->color = SS_RBRED;
				p->color = SS_RBBLACK;
				u->color = SS_RBBLACK;
				n = g;
			} else {
				if (n == p->r) {
					ss_rbrotate_left(t, p);
					n = p;
					p = n->p;
				}
				g->color = SS_RBRED;
				p->color = SS_RBBLACK;
				ss_rbrotate_right(t, g);
			}
		} else {
			struct ssrbnode *u = g->l;
			if (u && u->color == SS_RBRED) {
				g->color = SS_RBRED;
				p->color = SS_RBBLACK;
				u->color = SS_RBBLACK;
				n = g;
			} else {
				if (n == p->l) {
					ss_rbrotate_right(t, p);
					n = p;
					p = n->p;
				}
				g->color = SS_RBRED;
				p->color = SS_RBBLACK;
				ss_rbrotate_left(t, g);
			}
		}
	}
	t->root->color = SS_RBBLACK;
}

static void
ss_rbset(struct ssrb *t, struct ssrbnode *p, int prel, struct ssrbnode *n)
{
	n->color = SS_RBRED;
	n->p     = p;
	n->l     = NULL;
	n->r     = NULL;
	if (likely(p)) {
		assert(prel != 0);
		if (prel > 0)
			p->l = n;
		else
			p->r = n;
	} else {
		t->root = n;
	}
	ss_rbset_fixup(t, n);
}

static void
ss_rbreplace(struct ssrb *t, struct ssrbnode *o, struct ssrbnode *n)
{
	struct ssrbnode *p = o->p;
	if (p) {
		if (p->l == o) {
			p->l = n;
		} else {
			p->r = n;
		}
	} else {
		t->root = n;
	}
	if (o->l)
		o->l->p = n;
	if (o->r)
		o->r->p = n;
	*n = *o;
}

static void
ss_rbremove(struct ssrb *t, struct ssrbnode *n)
{
	if (unlikely(n->color == SS_RBUNDEF))
		return;
	struct ssrbnode *l = n->l;
	struct ssrbnode *r = n->r;
	struct ssrbnode *x = NULL;
	if (l == NULL) {
		x = r;
	} else
	if (r == NULL) {
		x = l;
	} else {
		x = r;
		while (x->l)
			x = x->l;
	}
	struct ssrbnode *p = n->p;
	if (p) {
		if (p->l == n) {
			p->l = x;
		} else {
			p->r = x;
		}
	} else {
		t->root = x;
	}
	uint8_t color;
	if (l && r) {
		color    = x->color;
		x->color = n->color;
		x->l     = l;
		l->p     = x;
		if (x != r) {
			p    = x->p;
			x->p = n->p;
			n    = x->r;
			p->l = n;
			x->r = r;
			r->p = x;
		} else {
			x->p = p;
			p    = x;
			n    = x->r;
		}
	} else {
		color = n->color;
		n     = x;
	}
	if (n)
		n->p = p;

	if (color == SS_RBRED)
		return;
	if (n && n->color == SS_RBRED) {
		n->color = SS_RBBLACK;
		return;
	}

	struct ssrbnode *s;
	do {
		if (unlikely(n == t->root))
			break;

		if (n == p->l) {
			s = p->r;
			if (s->color == SS_RBRED)
			{
				s->color = SS_RBBLACK;
				p->color = SS_RBRED;
				ss_rbrotate_left(t, p);
				s = p->r;
			}
			if ((!s->l || (s->l->color == SS_RBBLACK)) &&
			    (!s->r || (s->r->color == SS_RBBLACK)))
			{
				s->color = SS_RBRED;
				n = p;
				p = p->p;
				continue;
			}
			if ((!s->r || (s->r->color == SS_RBBLACK)))
			{
				s->l->color = SS_RBBLACK;
				s->color    = SS_RBRED;
				ss_rbrotate_right(t, s);
				s = p->r;
			}
			s->color    = p->color;
			p->color    = SS_RBBLACK;
			s->r->color = SS_RBBLACK;
			ss_rbrotate_left(t, p);
			n = t->root;
			break;
		} else {
			s = p->l;
			if (s->color == SS_RBRED)
			{
				s->color = SS_RBBLACK;
				p->color = SS_RBRED;
				ss_rbrotate_right(t, p);
				s = p->l;
			}
			if ((!s->l || (s->l->color == SS_RBBLACK)) &&
				(!s->r || (s->r->color == SS_RBBLACK)))
			{
				s->color = SS_RBRED;
				n = p;
				p = p->p;
				continue;
			}
			if ((!s->l || (s->l->color == SS_RBBLACK)))
			{
				s->r->color = SS_RBBLACK;
				s->color    = SS_RBRED;
				ss_rbrotate_left(t, s);
				s = p->l;
			}
			s->color    = p->color;
			p->color    = SS_RBBLACK;
			s->l->color = SS_RBBLACK;
			ss_rbrotate_right(t, p);
			n = t->root;
			break;
		}
	} while (n->color == SS_RBBLACK);
	if (n)
		n->color = SS_RBBLACK;
}

static inline int
ss_stdaopen(struct ssa *a ssunused, va_list args ssunused) {
	return 0;
}

static inline int
ss_stdaclose(struct ssa *a ssunused) {
	return 0;
}

static inline void*
ss_stdamalloc(struct ssa *a ssunused, size_t size) {
	return malloc(size);
}

static inline void*
ss_stdarealloc(struct ssa *a ssunused, void *ptr, size_t size) {
	return realloc(ptr,  size);
}

static inline void
ss_stdafree(struct ssa *a ssunused, void *ptr) {
	assert(ptr != NULL);
	free(ptr);
}

static struct ssaif ss_stda =
{
	.open    = ss_stdaopen,
	.close   = ss_stdaclose,
	.malloc  = ss_stdamalloc,
	.ensure  = NULL,
	.realloc = ss_stdarealloc,
	.free    = ss_stdafree
};

static inline int
ss_stdvfs_init(struct ssvfs *f ssunused, va_list args ssunused)
{
	return 0;
}

static inline void
ss_stdvfs_free(struct ssvfs *f ssunused)
{ }

static int64_t
ss_stdvfs_size(struct ssvfs *f ssunused, char *path)
{
	struct stat st;
	int rc = lstat(path, &st);
	if (unlikely(rc == -1))
		return -1;
	return st.st_size;
}

static int
ss_stdvfs_exists(struct ssvfs *f ssunused, const char *path)
{
	struct stat st;
	int rc = lstat(path, &st);
	return rc == 0;
}

static int
ss_stdvfs_unlink(struct ssvfs *f ssunused, char *path)
{
	return unlink(path);
}

static int
ss_stdvfs_rename(struct ssvfs *f ssunused, char *src, char *dest)
{
	return rename(src, dest);
}

static int
ss_stdvfs_mkdir(struct ssvfs *f ssunused, const char *path, int mode)
{
	return mkdir(path, mode);
}

static int
ss_stdvfs_rmdir(struct ssvfs *f ssunused, char *path)
{
	return rmdir(path);
}

static int
ss_stdvfs_open(struct ssvfs *f ssunused, char *path, int flags, int mode)
{
	return open(path, flags, mode);
}

static int
ss_stdvfs_close(struct ssvfs *f ssunused, int fd)
{
	return close(fd);
}

static int
ss_stdvfs_sync(struct ssvfs *f ssunused, int fd)
{
	return fdatasync(fd);
}

static int
ss_stdvfs_advise(struct ssvfs *f ssunused, int fd, int hint, uint64_t off, uint64_t len)
{
	(void)hint;
#if !defined(HAVE_POSIX_FADVISE)
	(void)fd;
	(void)off;
	(void)len;
	return 0;
#else
	return posix_fadvise(fd, off, len, POSIX_FADV_DONTNEED);
#endif
}

static int
ss_stdvfs_truncate(struct ssvfs *f ssunused, int fd, uint64_t size)
{
	return ftruncate(fd, size);
}

static int64_t
ss_stdvfs_pread(struct ssvfs *f ssunused, int fd, uint64_t off, void *buf, int size)
{
	int n = 0;
	do {
		int r;
		do {
			r = pread(fd, (char*)buf + n, size - n, off + n);
		} while (r == -1 && errno == EINTR);
		if (r <= 0)
			return -1;
		n += r;
	} while (n != size);

	return n;
}

static int64_t
ss_stdvfs_pwrite(struct ssvfs *f ssunused, int fd, uint64_t off, void *buf, int size)
{
	int n = 0;
	do {
		int r;
		do {
			r = pwrite(fd, (char*)buf + n, size - n, off + n);
		} while (r == -1 && errno == EINTR);
		if (r <= 0)
			return -1;
		n += r;
	} while (n != size);

	return n;
}

static int64_t
ss_stdvfs_write(struct ssvfs *f ssunused, int fd, void *buf, int size)
{
	int n = 0;
	do {
		int r;
		do {
			r = write(fd, (char*)buf + n, size - n);
		} while (r == -1 && errno == EINTR);
		if (r <= 0)
			return -1;
		n += r;
	} while (n != size);

	return n;
}

static int64_t
ss_stdvfs_writev(struct ssvfs *f ssunused, int fd, struct ssiov *iov)
{
	struct iovec *v = iov->v;
	int n = iov->iovc;
	int size = 0;
	do {
		int r;
		do {
			r = writev(fd, v, n);
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

	return size;
}

static int64_t
ss_stdvfs_seek(struct ssvfs *f ssunused, int fd, uint64_t off)
{
	return lseek(fd, off, SEEK_SET);
}

static int
ss_stdvfs_mmap(struct ssvfs *f ssunused, struct ssmmap *m, int fd, uint64_t size, int ro)
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
ss_stdvfs_mmap_allocate(struct ssvfs *f ssunused, struct ssmmap *m, uint64_t size)
{
	int flags = PROT_READ|PROT_WRITE;
	m->p = mmap(NULL, size, flags, MAP_PRIVATE|MAP_ANON, -1, 0);
	if (unlikely(m->p == MAP_FAILED)) {
		m->p = NULL;
		return -1;
	}
	m->size = size;
	return 0;
}

static int
ss_stdvfs_mremap(struct ssvfs *f ssunused, struct ssmmap *m, uint64_t size)
{
	if (unlikely(m->p == NULL))
		return ss_stdvfs_mmap_allocate(f, m, size);
	void *p;
#if !defined(HAVE_MREMAP)
	p = mmap(NULL, size, PROT_READ|PROT_WRITE,
	         MAP_PRIVATE|MAP_ANON, -1, 0);
	if (p == MAP_FAILED)
		return -1;
	size_t to_copy = m->size;
	if (to_copy > size)
		to_copy = size;
	memcpy(p, m->p, to_copy);
	munmap(m->p, m->size);
#else
	p = mremap(m->p, m->size, size, MREMAP_MAYMOVE);
	if (unlikely(p == MAP_FAILED))
		return -1;
#endif
	m->p = p;
	m->size = size;
	return 0;
}

static int
ss_stdvfs_munmap(struct ssvfs *f ssunused, struct ssmmap *m)
{
	if (unlikely(m->p == NULL))
		return 0;
	int rc = munmap(m->p, m->size);
	m->p = NULL;
	return rc;
}

static struct ssvfsif ss_stdvfs =
{
	.init          = ss_stdvfs_init,
	.free          = ss_stdvfs_free,
	.size          = ss_stdvfs_size,
	.exists        = ss_stdvfs_exists,
	.unlink        = ss_stdvfs_unlink,
	.rename        = ss_stdvfs_rename,
	.mkdir         = ss_stdvfs_mkdir,
	.rmdir         = ss_stdvfs_rmdir,
	.open          = ss_stdvfs_open,
	.close         = ss_stdvfs_close,
	.sync          = ss_stdvfs_sync,
	.advise        = ss_stdvfs_advise,
	.truncate      = ss_stdvfs_truncate,
	.pread         = ss_stdvfs_pread,
	.pwrite        = ss_stdvfs_pwrite,
	.write         = ss_stdvfs_write,
	.writev        = ss_stdvfs_writev,
	.seek          = ss_stdvfs_seek,
	.mmap          = ss_stdvfs_mmap,
	.mmap_allocate = ss_stdvfs_mmap_allocate,
	.mremap        = ss_stdvfs_mremap,
	.munmap        = ss_stdvfs_munmap
};

struct sszstdfilter {
	void *ctx;
};

static const size_t ZSTD_blockHeaderSize = 3;

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
		rc = ss_bufensure(dest, f->a, block);
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
		size_t block = ZSTD_blockHeaderSize;
		rc = ss_bufensure(dest, f->a, block);
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

typedef int (*sfcmpf)(char*, int, char*, int, void*);

struct sffield {
	enum sstype    type;
	int       position;
	int       position_ref;
	int       position_key;
	uint32_t  fixed_size;
	uint32_t  fixed_offset;
	char     *name;
	char     *options;
	int       key;
	sfcmpf    cmp;
};

enum sfstorage {
	SF_RAW,
	SF_SPARSE
};

struct sfscheme {
	struct sffield **fields;
	struct sffield **keys;
	struct key_def *key_def;
	int       fields_count;
	int       keys_count;
	sfcmpf    cmp;
	void     *cmparg;
	int       var_offset;
	int       var_count;
	enum sfstorage fmt_storage;
};

static inline struct sffield*
sf_fieldnew(struct ssa *a, char *name)
{
	struct sffield *f = ss_malloc(a, sizeof(struct sffield));
	if (unlikely(f == NULL))
		return NULL;
	f->key = 0;
	f->fixed_size = 0;
	f->fixed_offset = 0;
	f->position = 0;
	f->position_ref = 0;
	f->name = ss_strdup(a, name);
	if (unlikely(f->name == NULL)) {
		ss_free(a, f);
		return NULL;
	}
	f->type = SS_UNDEF;
	f->options = NULL;
	f->cmp = NULL;
	return f;
}

static inline void
sf_fieldfree(struct sffield *f, struct ssa *a)
{
	if (f->name) {
		ss_free(a, f->name);
		f->name = NULL;
	}
	if (f->options) {
		ss_free(a, f->options);
		f->options = NULL;
	}
	ss_free(a, f);
}

static inline int
sf_fieldoptions(struct sffield *f, struct ssa *a, char *options)
{
	char *sz = ss_strdup(a, options);
	if (unlikely(sz == NULL))
		return -1;
	if (f->options)
		ss_free(a, f->options);
	f->options = sz;
	return 0;
}

static void sf_schemeinit(struct sfscheme*);
static void sf_schemefree(struct sfscheme*, struct ssa*);
static int  sf_schemeadd(struct sfscheme*, struct ssa*, struct sffield*);
static int  sf_schemevalidate(struct sfscheme*, struct ssa*);
static struct sffield*
sf_schemefind(struct sfscheme*, char *);

static int  sf_schemecompare_prefix(struct sfscheme*, char*, uint32_t, char*);
static int  sf_schemecompare(char*, int, char*, int, void*);

static inline int
sf_compare(struct sfscheme *s, char *a, int asize, char *b, int bsize)
{
	return s->cmp(a, asize, b, bsize, s->cmparg);
}

static inline int
sf_compareprefix(struct sfscheme *s, char *a, int asize, char *b, int bsize ssunused)
{
	return sf_schemecompare_prefix(s, a, asize, b);
}

struct PACKED sfvar {
	uint32_t offset;
	uint32_t size;
};

struct phia_field {
	const char *data;
	uint32_t  size;
};

static inline char*
sf_fieldof_ptr(struct sfscheme *s, struct sffield *f, char *data, uint32_t *size)
{
	if (likely(f->fixed_size > 0)) {
		if (likely(size))
			*size = f->fixed_size;
		return data + f->fixed_offset;
	}
	register struct sfvar *v =
		&((struct sfvar*)(data + s->var_offset))[f->position_ref];
	if (likely(size))
		*size = v->size;
	return data + v->offset;
}

static inline char*
sf_fieldof(struct sfscheme *s, int pos, char *data, uint32_t *size)
{
	return sf_fieldof_ptr(s, s->fields[pos], data, size);
}

static inline char*
sf_field(struct sfscheme *s, int pos, char *data)
{
	register struct sffield *f = s->fields[pos];
	if (likely(f->fixed_size > 0))
		return data + f->fixed_offset;
	register struct sfvar *v =
		&((struct sfvar*)(data + s->var_offset))[f->position_ref];
	return data + v->offset;
}

static inline int
sf_fieldsize(struct sfscheme *s, int pos, char *data)
{
	register struct sffield *f = s->fields[pos];
	if (likely(f->fixed_size > 0))
		return f->fixed_size;
	register struct sfvar *v =
		&((struct sfvar*)(data + s->var_offset))[f->position_ref];
	return v->size;
}

static inline int
sf_writesize(struct sfscheme *s, struct phia_field *v)
{
	int sum = s->var_offset;
	int i;
	for (i = 0; i < s->fields_count; i++) {
		struct sffield *f = s->fields[i];
		if (f->fixed_size != 0)
			continue;
		sum += sizeof(struct sfvar)+ v[i].size;
	}
	return sum;
}

static inline void
sf_write(struct sfscheme *s, struct phia_field *v, char *dest)
{
	int var_value_offset =
		s->var_offset + sizeof(struct sfvar) * s->var_count;
	struct sfvar *var = (struct sfvar*)(dest + s->var_offset);
	int i;
	for (i = 0; i < s->fields_count; i++) {
		struct sffield *f = s->fields[i];
		if (f->fixed_size) {
			assert(f->fixed_size == v[i].size);
			memcpy(dest + f->fixed_offset, v[i].data, f->fixed_size);
			continue;
		}
		struct sfvar *current = &var[f->position_ref];
		current->offset = var_value_offset;
		current->size   = v[i].size;
		memcpy(dest + var_value_offset, v[i].data, v[i].size);
		var_value_offset += current->size;
	}
}

static inline uint64_t
sf_hash(struct sfscheme *s, char *data)
{
	uint64_t hash = 0;
	int i;
	for (i = 0; i < s->keys_count; i++)
		hash ^= ss_fnv(sf_field(s, i, data), sf_fieldsize(s, i, data));
	return hash;
}

static inline int
sf_comparable_size(struct sfscheme *s, char *data)
{
	int sum = s->var_offset;
	int i;
	for (i = 0; i < s->fields_count; i++) {
		struct sffield *f = s->fields[i];
		if (f->fixed_size != 0)
			continue;
		if (f->key)
			sum += sf_fieldsize(s, i, data);
		sum += sizeof(struct sfvar);
	}
	return sum;
}

static inline void
sf_comparable_write(struct sfscheme *s, char *src, char *dest)
{
	int var_value_offset =
		s->var_offset + sizeof(struct sfvar) * s->var_count;
	memcpy(dest, src, s->var_offset);
	struct sfvar *var = (struct sfvar*)(dest + s->var_offset);
	int i;
	for (i = 0; i < s->fields_count; i++) {
		struct sffield *f = s->fields[i];
		if (f->fixed_size != 0)
			continue;
		struct sfvar *current = &var[f->position_ref];
		current->offset = var_value_offset;
		if (! f->key) {
			current->size = 0;
			continue;
		}
		char *ptr = sf_fieldof_ptr(s, f, src, &current->size);
		memcpy(dest + var_value_offset, ptr, current->size);
		var_value_offset += current->size;
	}
}

struct sflimit {
	uint32_t u32_min;
	uint32_t u32_max;
	uint64_t u64_min;
	uint64_t u64_max;
	char    *string_min;
	int      string_min_size;
	char    *string_max;
	int      string_max_size;
};

static inline int
sf_limitinit(struct sflimit *b, struct ssa *a)
{
	b->u32_min = 0;
	b->u32_max = UINT32_MAX;
	b->u64_min = 0;
	b->u64_max = UINT64_MAX;
	b->string_min_size = 0;
	b->string_min = "";
	b->string_max_size = 1024;
	b->string_max = ss_malloc(a, b->string_max_size);
	if (unlikely(b->string_max == NULL))
		return -1;
	memset(b->string_max, 0xff, b->string_max_size);
	return 0;
}

static inline void
sf_limitfree(struct sflimit *b, struct ssa *a)
{
	if (b->string_max)
		ss_free(a, b->string_max);
}

static inline void
sf_limitset(struct sflimit *b, struct sfscheme *s, struct phia_field *fields, enum phia_order order)
{
	int i;
	for (i = 0; i < s->fields_count; i++) {
		struct phia_field *v = &fields[i];
		if (v->data)
			continue;
		struct sffield *part = s->fields[i];
		switch (part->type) {
		case SS_U32:
			if (order == PHIA_LT || order == PHIA_LE) {
				v->data = (char*)&b->u32_max;
				v->size = sizeof(uint32_t);
			} else {
				v->data = (char*)&b->u32_min;
				v->size = sizeof(uint32_t);
			}
			break;
		case SS_U32REV:
			if (order == PHIA_LT || order == PHIA_LE) {
				v->data = (char*)&b->u32_min;
				v->size = sizeof(uint32_t);
			} else {
				v->data = (char*)&b->u32_max;
				v->size = sizeof(uint32_t);
			}
			break;
		case SS_U64:
			if (order == PHIA_LT || order == PHIA_LE) {
				v->data = (char*)&b->u64_max;
				v->size = sizeof(uint64_t);
			} else {
				v->data = (char*)&b->u64_min;
				v->size = sizeof(uint64_t);
			}
			break;
		case SS_U64REV:
			if (order == PHIA_LT || order == PHIA_LE) {
				v->data = (char*)&b->u64_min;
				v->size = sizeof(uint64_t);
			} else {
				v->data = (char*)&b->u64_max;
				v->size = sizeof(uint64_t);
			}
			break;
		case SS_STRING:
			if (order == PHIA_LT || order == PHIA_LE) {
				v->data = b->string_max;
				v->size = b->string_max_size;
			} else {
				v->data = b->string_min;
				v->size = b->string_min_size;
			}
			break;
		default: assert(0);
			break;
		}
	}
}

static inline int
sf_cmpstring(char *a, int asz, char *b, int bsz, void *arg ssunused)
{
	int size = (asz < bsz) ? asz : bsz;
	int rc = memcmp(a, b, size);
	if (unlikely(rc == 0)) {
		if (likely(asz == bsz))
			return 0;
		return (asz < bsz) ? -1 : 1;
	}
	return rc > 0 ? 1 : -1;
}

static inline int
sf_cmpu32(char *a, int asz ssunused, char *b, int bsz ssunused, void *arg ssunused)
{
	uint32_t av = load_u32(a);
	uint32_t bv = load_u32(b);
	if (av == bv)
		return 0;
	return (av > bv) ? 1 : -1;
}

static inline int
sf_cmpu32_reverse(char *a, int asz ssunused, char *b, int bsz ssunused, void *arg ssunused)
{
	uint32_t av = load_u32(a);
	uint32_t bv = load_u32(b);
	if (av == bv)
		return 0;
	return (av > bv) ? -1 : 1;
}

static inline int
sf_cmpu64(char *a, int asz ssunused, char *b, int bsz ssunused,
              void *arg ssunused)
{
	uint64_t av = load_u64(a);
	uint64_t bv = load_u64(b);
	if (av == bv)
		return 0;
	return (av > bv) ? 1 : -1;
}

static inline int
sf_cmpu64_reverse(char *a, int asz ssunused, char *b, int bsz ssunused,
              void *arg ssunused)
{
	uint64_t av = load_u64(a);
	uint64_t bv = load_u64(b);
	if (av == bv)
		return 0;
	return (av > bv) ? -1 : 1;
}

static int
sf_schemecompare(char *a, int asize ssunused, char *b, int bsize ssunused, void *arg)
{
	struct sfscheme *s = arg;
	struct sffield **part = s->keys;
	struct sffield **last = part + s->keys_count;
	int rc;
	while (part < last) {
		struct sffield *key = *part;
		uint32_t a_fieldsize;
		char *a_field = sf_fieldof_ptr(s, key, a, &a_fieldsize);
		uint32_t b_fieldsize;
		char *b_field = sf_fieldof_ptr(s, key, b, &b_fieldsize);
		rc = key->cmp(a_field, a_fieldsize, b_field, b_fieldsize, NULL);
		if (rc != 0)
			return rc;
		part++;
	}
	return 0;
}

static int
sf_schemecompare_prefix(struct sfscheme *s, char *prefix, uint32_t prefixsize, char *key)
{
	uint32_t keysize;
	key = sf_fieldof(s, 0, key, &keysize);
	if (keysize < prefixsize)
		return 0;
	return (memcmp(prefix, key, prefixsize) == 0) ? 1 : 0;
}

static void
sf_schemeinit(struct sfscheme *s)
{
	s->fields = NULL;
	s->fields_count = 0;
	s->keys = NULL;
	s->keys_count = 0;
	s->var_offset = 0;
	s->var_count  = 0;
	s->cmp = sf_schemecompare;
	s->cmparg = s;
	s->key_def = NULL;
	s->fmt_storage = SF_RAW;
}

static void
sf_schemefree(struct sfscheme *s, struct ssa *a)
{
	if (s->fields) {
		int i = 0;
		while (i < s->fields_count) {
			sf_fieldfree(s->fields[i], a);
			i++;
		}
		ss_free(a, s->fields);
		s->fields = NULL;
	}
	if (s->keys) {
		ss_free(a, s->keys);
		s->keys = NULL;
	}
}

static int
sf_schemeadd(struct sfscheme *s, struct ssa *a, struct sffield *f)
{
	int size = sizeof(struct sffield*) * (s->fields_count + 1);
	struct sffield **fields = ss_malloc(a, size);
	if (unlikely(fields == NULL))
		return -1;
	memcpy(fields, s->fields, size - sizeof(struct sffield*));
	fields[s->fields_count] = f;
	f->position = s->fields_count;
	f->position_key = -1;
	if (s->fields)
		ss_free(a, s->fields);
	s->fields = fields;
	s->fields_count++;
	return 0;
}

static inline int
sf_schemeset(struct sfscheme *s, struct sffield *f, char *opt)
{
	(void)s;
	if (strcmp(opt, "string") == 0) {
		f->type = SS_STRING;
		f->fixed_size = 0;
		f->cmp = sf_cmpstring;
	} else
	if (strcmp(opt, "u32") == 0) {
		f->type = SS_U32;
		f->fixed_size = sizeof(uint32_t);
		f->cmp = sf_cmpu32;
	} else
	if (strcmp(opt, "u32_rev") == 0) {
		f->type = SS_U32REV;
		f->fixed_size = sizeof(uint32_t);
		f->cmp = sf_cmpu32_reverse;
	} else
	if (strcmp(opt, "u64") == 0) {
		f->type = SS_U64;
		f->fixed_size = sizeof(uint64_t);
		f->cmp = sf_cmpu64;
	} else
	if (strcmp(opt, "u64_rev") == 0) {
		f->type = SS_U64REV;
		f->fixed_size = sizeof(uint64_t);
		f->cmp = sf_cmpu64_reverse;
	} else
	if (strncmp(opt, "key", 3) == 0) {
		char *p = opt + 3;
		if (unlikely(*p != '('))
			return -1;
		p++;
		if (unlikely(! isdigit(*p)))
			return -1;
		int v = 0;
		while (isdigit(*p)) {
			v = (v * 10) + *p - '0';
			p++;
		}
		if (unlikely(*p != ')'))
			return -1;
		p++;
		f->position_key = v;
		f->key = 1;
	} else {
		return -1;
	}
	return 0;
}

static int
sf_schemevalidate(struct sfscheme *s, struct ssa *a)
{
	/* validate fields */
	if (s->fields_count == 0) {
		return -1;
	}
	int fixed_offset = 0;
	int fixed_pos = 0;
	int i = 0;
	while (i < s->fields_count)
	{
		/* validate and apply field options */
		struct sffield *f = s->fields[i];
		if (f->options == NULL) {
			return -1;
		}
		char opts[256];
		snprintf(opts, sizeof(opts), "%s", f->options);
		char *p;
		for (p = strtok(opts, " ,"); p;
		     p = strtok(NULL, " ,"))
		{
			int rc = sf_schemeset(s, f, p);
			if (unlikely(rc == -1))
				return -1;
		}
		/* calculate offset and position for fixed
		 * size types */
		if (f->fixed_size > 0) {
			f->position_ref = fixed_pos;
			fixed_pos++;
			f->fixed_offset = fixed_offset;
			fixed_offset += f->fixed_size;
		} else {
			s->var_count++;
		}
		if (f->key)
			s->keys_count++;
		i++;
	}
	s->var_offset = fixed_offset;

	/* validate keys */
	if (unlikely(s->keys_count == 0))
		return -1;
	int size = sizeof(struct sffield*) * s->keys_count;
	s->keys = ss_malloc(a, size);
	if (unlikely(s->keys == NULL))
		return -1;
	memset(s->keys, 0, size);
	int pos_var = 0;
	i = 0;
	while (i < s->fields_count) {
		struct sffield *f = s->fields[i];
		if (f->key) {
			if (unlikely(f->position_key < 0))
				return -1;
			if (unlikely(f->position_key >= s->fields_count))
				return -1;
			if (unlikely(f->position_key >= s->keys_count))
				return -1;
			if (unlikely(s->keys[f->position_key] != NULL))
				return -1;
			s->keys[f->position_key] = f;
		}
		if (f->fixed_size == 0)
			f->position_ref = pos_var++;
		i++;
	}
	i = 0;
	while (i < s->keys_count) {
		struct sffield *f = s->keys[i];
		if (f == NULL)
			return -1;
		i++;
	}
	return 0;
}

static struct sffield*
sf_schemefind(struct sfscheme *s, char *name)
{
	int i;
	for (i = 0; i < s->fields_count; i++)
		if (strcmp(s->fields[i]->name, name) == 0)
			return s->fields[i];
	return NULL;
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
	sr_e(ER_PHIA, fmt, __VA_ARGS__)

#define sr_malfunction(fmt, ...) \
	sr_e(ER_PHIA, fmt, __VA_ARGS__)

#define sr_oom() \
	sr_e(ER_PHIA, "%s", "memory allocation failed")

enum {
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
	int status;
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

static inline int
sr_statusset(struct srstatus *s, int status)
{
	tt_pthread_mutex_lock(&s->lock);
	int old = s->status;
	s->status = status;
	tt_pthread_mutex_unlock(&s->lock);
	return old;
}

static inline int
sr_status(struct srstatus *s)
{
	tt_pthread_mutex_lock(&s->lock);
	int status = s->status;
	tt_pthread_mutex_unlock(&s->lock);
	return status;
}

static inline int
sr_statusactive_is(int status)
{
	switch (status) {
	case SR_ONLINE:
	case SR_INITIAL_RECOVERY:
	case SR_FINAL_RECOVERY:
		return 1;
	case SR_SHUTDOWN_PENDING:
	case SR_SHUTDOWN:
	case SR_DROP_PENDING:
	case SR_DROP:
	case SR_OFFLINE:
	case SR_MALFUNCTION:
		return 0;
	}
	assert(0);
	return 0;
}

static inline int
sr_statusactive(struct srstatus *s) {
	return sr_statusactive_is(sr_status(s));
}

static inline int
sr_online(struct srstatus *s) {
	return sr_status(s) == SR_ONLINE;
}

struct srstat {
	pthread_mutex_t lock;
	/* memory */
	uint64_t v_count;
	uint64_t v_allocated;
	/* key-value */
	struct ssavg    key, value;
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
	ss_avgprepare(&s->key);
	ss_avgprepare(&s->value);
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

static inline void
sr_statkey(struct srstat *s, int size)
{
	tt_pthread_mutex_lock(&s->lock);
	ss_avgupdate(&s->key, size);
	tt_pthread_mutex_unlock(&s->lock);
}

struct phia_statget {
	int read_disk;
	int read_cache;
	uint64_t read_latency;
};

static inline void
sr_statget(struct srstat *s, const struct phia_statget *statget)
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
	SR_DSN,
	SR_DSNNEXT,
	SR_NSN,
	SR_NSNNEXT,
	SR_LSN,
	SR_LSNNEXT,
	SR_LFSN,
	SR_LFSNNEXT,
	SR_TSN,
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
	/** Log file sequence number. */
	uint64_t lfsn;
	/** Database sequence number. */
	uint32_t dsn;
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
	case SR_LSNNEXT:   v = ++n->lsn;
		break;
	case SR_TSN:       v = n->tsn;
		break;
	case SR_TSNNEXT:   v = ++n->tsn;
		break;
	case SR_NSN:       v = n->nsn;
		break;
	case SR_NSNNEXT:   v = ++n->nsn;
		break;
	case SR_LFSN:      v = n->lfsn;
		break;
	case SR_LFSNNEXT:  v = ++n->lfsn;
		break;
	case SR_DSN:       v = n->dsn;
		break;
	case SR_DSNNEXT:   v = ++n->dsn;
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
	struct srstatus *status;
	struct srseq *seq;
	struct ssa *a;
	struct ssvfs *vfs;
	struct ssquota *quota;
	struct srzonemap *zonemap;
	struct srstat *stat;
};

static inline void
sr_init(struct runtime *r,
        struct srstatus *status,
        struct ssa *a,
        struct ssvfs *vfs,
        struct ssquota *quota,
        struct srzonemap *zonemap,
        struct srseq *seq,
        struct srstat *stat)
{
	r->status      = status;
	r->a           = a;
	r->vfs         = vfs;
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

enum {
	SR_RO = 1,
	SR_NS = 2
};

struct srconf {
	char    *key;
	int      flags;
	enum sstype   type;
	srconff  function;
	void    *value;
	void    *ptr;
	struct srconf  *next;
};

struct PACKED srconfdump {
	uint8_t  type;
	uint16_t keysize;
	uint32_t valuesize;
};

struct srconfstmt {
	const char *path;
	void       *value;
	enum sstype valuetype;
	int         valuesize;
	struct srconf     *match;
	struct ssbuf      *serialize;
	void       *ptr;
	struct runtime         *r;
};

static int sr_confexec(struct srconf*, struct srconfstmt*);
static int sr_conf_serialize(struct srconf*, struct srconfstmt*);

static inline struct srconf*
sr_c(struct srconf **link, struct srconf **cp, srconff func,
     char *key, int type,
     void *value)
{
	struct srconf *c = *cp;
	c->key      = key;
	c->function = func;
	c->flags    = 0;
	c->type     = type;
	c->value    = value;
	c->ptr      = NULL;
	c->next     = NULL;
	*cp = c + 1;
	if (likely(link)) {
		if (likely(*link))
			(*link)->next = c;
		*link = c;
	}
	return c;
}

static inline struct srconf*
sr_C(struct srconf **link, struct srconf **cp, srconff func,
     char *key, int type,
     void *value, int flags, void *ptr)
{
	struct srconf *c = sr_c(link, cp, func, key, type, value);
	c->flags = flags;
	c->ptr = ptr;
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
sr_conf_serialize(struct srconf *m, struct srconfstmt *s)
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
	int rc = ss_bufensure(p, s->r->a, size);
	if (unlikely(rc == -1))
		return sr_oom();
	memcpy(p->p, &v, sizeof(v));
	memcpy(p->p + sizeof(v), name, v.keysize);
	memcpy(p->p + sizeof(v) + v.keysize, value, v.valuesize);
	ss_bufadvance(p, size);
	return 0;
}

static inline int
sr_confexec_serialize(struct srconf *c, struct srconfstmt *stmt, char *root)
{
	char path[256];
	while (c) {
		if (root)
			snprintf(path, sizeof(path), "%s.%s", root, c->key);
		else
			snprintf(path, sizeof(path), "%s", c->key);
		int rc;
		if (c->flags & SR_NS) {
			rc = sr_confexec_serialize(c->value, stmt, path);
			if (unlikely(rc == -1))
				return -1;
		} else {
			stmt->path = path;
			rc = c->function(c, stmt);
			if (unlikely(rc == -1))
				return -1;
			stmt->path = NULL;
		}
		c = c->next;
	}
	return 0;
}

static int sr_confexec(struct srconf *start, struct srconfstmt *s)
{
	return sr_confexec_serialize(start, s, NULL);
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
	void      (*lsnset)(struct sv*, uint64_t);
	uint64_t  (*lsn)(struct sv*);
	char     *(*pointer)(struct sv*);
	uint32_t  (*size)(struct sv*);
};

struct PACKED sv {
	struct svif *i;
	void *v, *arg;
};

static inline void
sv_init(struct sv *v, struct svif *i, void *vptr, void *arg) {
	v->i   = i;
	v->v   = vptr;
	v->arg = arg;
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
sv_lsnset(struct sv *v, uint64_t lsn) {
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

static inline char*
sv_field(struct sv *v, struct sfscheme *scheme, int pos, uint32_t *size) {
	return sf_fieldof(scheme, pos, v->i->pointer(v), size);
}

static inline uint64_t
sv_hash(struct sv *v, struct sfscheme *scheme) {
	return sf_hash(scheme, sv_pointer(v));
}

struct PACKED phia_tuple {
	uint64_t lsn;
	uint32_t size;
	uint16_t refs;
	uint8_t  flags;
	char data[0];
};

static inline uint32_t
phia_tuple_size(struct phia_tuple *v) {
	return sizeof(struct phia_tuple) + v->size;
}

static inline struct phia_tuple*
phia_tuple_from_sv(struct runtime *r, struct sv *src);

static inline void
phia_tuple_ref(struct phia_tuple *v);

static inline int
phia_tuple_unref(struct runtime *r, struct phia_tuple *v);

static inline int
phia_document_build(struct phia_document *o, enum phia_order order);

static struct svif sv_upsertvif;

struct svupsertnode {
	uint64_t lsn;
	uint8_t  flags;
	struct ssbuf    buf;
};

#define SV_UPSERTRESRV 16

struct svupsert {
	struct svupsertnode reserve[SV_UPSERTRESRV];
	struct ssbuf stack;
	struct ssbuf tmp;
	int max;
	int count;
	struct sv result;
};

static inline void
sv_upsertinit(struct svupsert *u)
{
	const int reserve = SV_UPSERTRESRV;
	int i = 0;
	while (i < reserve) {
		ss_bufinit(&u->reserve[i].buf);
		i++;
	}
	memset(&u->result, 0, sizeof(u->result));
	u->max = reserve;
	u->count = 0;
	ss_bufinit_reserve(&u->stack, u->reserve, sizeof(u->reserve));
	ss_bufinit(&u->tmp);
}

static inline void
sv_upsertfree(struct svupsert *u, struct ssa *a)
{
	struct svupsertnode *n = (struct svupsertnode*)u->stack.s;
	int i = 0;
	while (i < u->max) {
		ss_buffree(&n[i].buf, a);
		i++;
	}
	ss_buffree(&u->stack, a);
	ss_buffree(&u->tmp, a);
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
sv_upsertgc(struct svupsert *u, struct ssa *a, int wm_stack, int wm_buf)
{
	struct svupsertnode *n = (struct svupsertnode*)u->stack.s;
	if (u->max >= wm_stack) {
		sv_upsertfree(u, a);
		sv_upsertinit(u);
		return;
	}
	ss_bufgc(&u->tmp, a, wm_buf);
	int i = 0;
	while (i < u->count) {
		ss_bufgc(&n[i].buf, a, wm_buf);
		i++;
	}
	u->count = 0;
	memset(&u->result, 0, sizeof(u->result));
}

static inline int
sv_upsertpush_raw(struct svupsert *u, struct ssa *a,
		  char *pointer, int size,
                  uint8_t flags, uint64_t lsn)
{
	struct svupsertnode *n;
	int rc;
	if (likely(u->max > u->count)) {
		n = (struct svupsertnode*)u->stack.p;
		ss_bufreset(&n->buf);
	} else {
		rc = ss_bufensure(&u->stack, a, sizeof(struct svupsertnode));
		if (unlikely(rc == -1))
			return -1;
		n = (struct svupsertnode*)u->stack.p;
		ss_bufinit(&n->buf);
		u->max++;
	}
	rc = ss_bufensure(&n->buf, a, size);
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
sv_upsertpush(struct svupsert *u, struct ssa *a, struct sv *v)
{
	return sv_upsertpush_raw(u, a, sv_pointer(v),
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

/* TODO: move implementation from phia_space.cc */
extern int
phia_upsert_cb(int count,
	       char **src,    uint32_t *src_size,
	       char **upsert, uint32_t *upsert_size,
	       char **result, uint32_t *result_size,
	       struct key_def *key_def);

static inline int
sv_upsertdo(struct svupsert *u, struct ssa *a, struct sfscheme *scheme,
	    struct svupsertnode *n1, struct svupsertnode *n2)
{
	assert(scheme->fields_count <= 16);
	assert(n2->flags & SVUPSERT);

	uint32_t  src_size[16];
	char     *src[16];
	void     *src_ptr;
	uint32_t *src_size_ptr;

	uint32_t  upsert_size[16];
	char     *upsert[16];
	uint32_t  result_size[16];
	char     *result[16];

	int i = 0;
	if (n1 && !(n1->flags & SVDELETE))
	{
		src_ptr = src;
		src_size_ptr = src_size;
		for (; i < scheme->fields_count; i++) {
			src[i]    = sf_fieldof(scheme, i, n1->buf.s, &src_size[i]);
			upsert[i] = sf_fieldof(scheme, i, n2->buf.s, &upsert_size[i]);
			result[i] = src[i];
			result_size[i] = src_size[i];
		}
	} else {
		src_ptr = NULL;
		src_size_ptr = NULL;
		for (; i < scheme->fields_count; i++) {
			upsert[i] = sf_fieldof(scheme, i, n2->buf.s, &upsert_size[i]);
			result[i] = upsert[i];
			result_size[i] = upsert_size[i];
		}
	}

	/* execute */
	int rc = phia_upsert_cb(scheme->fields_count,
				src_ptr, src_size_ptr,
				upsert, upsert_size,
				result, result_size,
				scheme->key_def);
	if (unlikely(rc == -1))
		return -1;

	/* validate and create new record */
	struct phia_field v[16];
	i = 0;
	for ( ; i < scheme->fields_count; i++) {
		v[i].data = result[i];
		v[i].size = result_size[i];
	}
	int size = sf_writesize(scheme, v);
	ss_bufreset(&u->tmp);
	rc = ss_bufensure(&u->tmp, a, size);
	if (unlikely(rc == -1))
		goto cleanup;
	sf_write(scheme, v, u->tmp.s);
	ss_bufadvance(&u->tmp, size);

	/* save result */
	rc = sv_upsertpush_raw(u, a, u->tmp.s, ss_bufused(&u->tmp),
	                       n2->flags & ~SVUPSERT,
	                       n2->lsn);
cleanup:
	/* free fields */
	i = 0;
	for ( ; i < scheme->fields_count; i++) {
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
sv_upsert(struct svupsert *u, struct ssa *a, struct sfscheme *scheme)
{
	assert(u->count >= 1 );
	struct svupsertnode *f = ss_bufat(&u->stack,
					  sizeof(struct svupsertnode),
					  u->count - 1);
	int rc;
	if (f->flags & SVUPSERT) {
		f = sv_upsertpop(u);
		rc = sv_upsertdo(u, a, scheme, NULL, f);
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
		rc = sv_upsertdo(u, a, scheme, f, s);
		if (unlikely(rc == -1))
			return -1;
	}
done:
	sv_init(&u->result, &sv_upsertvif, u->stack.s, NULL);
	return 0;
}

struct si;

struct PACKED svlogindex {
	uint32_t id;
	uint32_t head, tail;
	uint32_t count;
	struct si *index;
};

struct PACKED svlogv {
	struct sv v;
	uint32_t id;
	uint32_t next;
};

struct svlog {
	int count_write;
	struct svlogindex reserve_i[2];
	struct svlogv reserve_v[1];
	struct ssbuf index;
	struct ssbuf buf;
};

static inline void
sv_loginit(struct svlog *l)
{
	ss_bufinit_reserve(&l->index, l->reserve_i, sizeof(l->reserve_i));
	ss_bufinit_reserve(&l->buf, l->reserve_v, sizeof(l->reserve_v));
	l->count_write = 0;
}

static inline void
sv_logfree(struct svlog *l, struct ssa *a)
{
	ss_buffree(&l->buf, a);
	ss_buffree(&l->index, a);
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
sv_logadd(struct svlog *l, struct ssa *a, struct svlogv *v,
	  struct si *index)
{
	uint32_t n = sv_logcount(l);
	int rc = ss_bufadd(&l->buf, a, v, sizeof(struct svlogv));
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
	rc = ss_bufensure(&l->index, a, sizeof(struct svlogindex));
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
	struct sfscheme *scheme;
	struct svmergesrc reserve[16];
	struct ssbuf buf;
};

static inline void
sv_mergeinit(struct svmerge *m, struct ssa *a, struct sfscheme *scheme)
{
	ss_bufinit_reserve(&m->buf, m->reserve, sizeof(m->reserve));
	m->a = a;
	m->scheme = scheme;
}

static inline int
sv_mergeprepare(struct svmerge *m, int count)
{
	int rc = ss_bufensure(&m->buf, m->a, sizeof(struct svmergesrc) * count);
	if (unlikely(rc == -1))
		return sr_oom();
	return 0;
}

static inline struct svmergesrc*
sv_mergenextof(struct svmergesrc *src)
{
	return src + 1;
}

static inline void
sv_mergefree(struct svmerge *m)
{
	ss_buffree(&m->buf, m->a);
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
	enum phia_order order;
	struct svmerge *merge;
	struct svmergesrc *src, *end;
	struct svmergesrc *v;
};

static inline void
sv_mergeiter_dupreset(struct svmergeiter *i, struct svmergesrc *pos)
{
	struct svmergesrc *v = i->src;
	while (v != pos) {
		v->dup = 0;
		v = sv_mergenextof(v);
	}
}

static inline void
sv_mergeiter_gt(struct svmergeiter *i)
{
	if (i->v) {
		i->v->dup = 0;
		ss_iteratornext(i->v->i);
	}
	i->v = NULL;
	struct svmergesrc *min, *src;
	struct sv *minv;
	minv = NULL;
	min  = NULL;
	src  = i->src;
	for (; src < i->end; src = sv_mergenextof(src))
	{
		struct sv *v = ss_iteratorof(src->i);
		if (v == NULL)
			continue;
		if (min == NULL) {
			minv = v;
			min = src;
			continue;
		}
		int rc;
		rc = sf_compare(i->merge->scheme,
		                sv_pointer(minv), sv_size(minv),
		                sv_pointer(v), sv_size(v));
		switch (rc) {
		case 0:
			/*
			assert(sv_lsn(v) < sv_lsn(minv));
			*/
			src->dup = 1;
			break;
		case 1:
			sv_mergeiter_dupreset(i, src);
			minv = v;
			min = src;
			break;
		}
	}
	if (unlikely(min == NULL))
		return;
	i->v = min;
}

static inline void
sv_mergeiter_lt(struct svmergeiter *i)
{
	if (i->v) {
		i->v->dup = 0;
		ss_iteratornext(i->v->i);
	}
	i->v = NULL;
	struct svmergesrc *max, *src;
	struct sv *maxv;
	maxv = NULL;
	max  = NULL;
	src  = i->src;
	for (; src < i->end; src = sv_mergenextof(src))
	{
		struct sv *v = ss_iteratorof(src->i);
		if (v == NULL)
			continue;
		if (max == NULL) {
			maxv = v;
			max = src;
			continue;
		}
		int rc;
		rc = sf_compare(i->merge->scheme,
		                sv_pointer(maxv), sv_size(maxv),
		                sv_pointer(v), sv_size(v));
		switch (rc) {
		case  0:
			/*
			assert(sv_lsn(v) < sv_lsn(maxv));
			*/
			src->dup = 1;
			break;
		case -1:
			sv_mergeiter_dupreset(i, src);
			maxv = v;
			max = src;
			break;
		}
	}
	if (unlikely(max == NULL))
		return;
	i->v = max;
}

static inline void
sv_mergeiter_next(struct svmergeiter *im)
{
	switch (im->order) {
	case PHIA_GT:
	case PHIA_GE:
		sv_mergeiter_gt(im);
		break;
	case PHIA_LT:
	case PHIA_LE:
		sv_mergeiter_lt(im);
		break;
	default: assert(0);
	}
}

static inline int
sv_mergeiter_open(struct svmergeiter *im, struct svmerge *m, enum phia_order o)
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
	int rc = sv_upsertpush(i->u, i->a, v);
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
		int rc = sv_upsertpush(i->u, i->a, v);
		if (unlikely(rc == -1))
			return -1;
		if (! (sv_flags(v) & SVUPSERT))
			skip = 1;
	}
	/* upsert */
	rc = sv_upsert(i->u, i->a, i->merge->merge->scheme);
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
	int rc = sv_upsertpush(i->u, i->a, v);
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
		int rc = sv_upsertpush(i->u, i->a, v);
		if (unlikely(rc == -1))
			return -1;
	}

	/* upsert */
	rc = sv_upsert(i->u, i->a, i->merge->merge->scheme);
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
	struct phia_tuple *v;
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
 * svindex is an in-memory container for phia_tuples in an
 * a single phia node.
 * Internally it uses bps_tree to stores struct svref objects,
 * which, in turn, hold pointers to phia_tuple objects.
 * svrefs are ordered by tuple key and, for the same key,
 * by lsn, in descending order.
 *
 * For example, assume there are two tuples with the same key,
 * but different LSN. These are duplicates of the same key,
 * maintained for the purpose of MVCC/consistent read view.
 * In Phia terms, they form a duplicate chain.
 *
 * Due to specifics of usage, svindex must distinguish between the
 * first duplicate in the chain and other keys in that chain.
 *
 * That's why svref objects additionally store 'flags' member
 * that could hold SVDUP bit. The first svref in a chain
 * has flags == 0,  and others have flags == SVDUP
 *
 * During insertion, reference counter of phia_tuple is incremented,
 * during destruction all phia_tuple' reference counters are decremented.
 */
struct svindex {
	struct bps_tree_svindex tree;
	uint32_t used;
	uint64_t lsnmin;
	struct sfscheme *scheme;
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
	int res = sf_compare(index->scheme, a.v->data, a.v->size,
			     b.v->data, b.v->size);
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
	int res = sf_compare(index->scheme, a.v->data, a.v->size,
			     key->data, key->size);
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
sv_indexinit(struct svindex *i, struct sfscheme *scheme)
{
	i->lsnmin = UINT64_MAX;
	i->used   = 0;
	i->scheme = scheme;
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
		phia_tuple_unref(r, bps_tree_svindex_itr_get_elem(&i->tree, &itr)->v);
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
	phia_tuple_ref(ref.v);
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
		if (sf_compare(i->scheme, curr->v->data, curr->v->size,
			        prev->v->data, prev->v->size) == 0) {
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
	return i->tree.size * sizeof(struct phia_tuple) + i->used +
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
sv_refiflsnset(struct sv *v, uint64_t lsn) {
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
	enum phia_order order;
};

static struct ssiterif sv_indexiterif;

static inline int
sv_indexiter_open(struct ssiter *i, struct svindex *index,
		  enum phia_order o, void *key, int keysize)
{
	assert(index == index->tree.arg);
	i->vif = &sv_indexiterif;
	struct svindexiter *ii = (struct svindexiter *)i->priv;
	struct bps_tree_svindex *tree = &index->tree;
	ii->index = index;
	ii->order = o;
	ii->current.i = &sv_refif;
	if (key == NULL) {
		if (o == PHIA_GT || o == PHIA_GE) {
			ii->itr = bps_tree_svindex_itr_first(tree);
		} else {
			assert(o == PHIA_LT || o == PHIA_LE);
			ii->itr = bps_tree_svindex_itr_last(tree);
		}
		return 0;
	}

	struct tree_svindex_key tree_key;
	tree_key.data = key;
	tree_key.size = keysize;
	tree_key.lsn = UINT64_MAX;
	bool exact;
	ii->index->hint_key_is_equal = false;
	ii->itr = bps_tree_svindex_lower_bound(tree, &tree_key, &exact);
	if (ii->index->hint_key_is_equal) {
		if (o == PHIA_GT)
			bps_tree_svindex_itr_next(tree, &ii->itr);
		else if(o == PHIA_LT)
			bps_tree_svindex_itr_prev(tree, &ii->itr);
	} else if(bps_tree_svindex_itr_is_invalid(&ii->itr)) {
		if(o == PHIA_LT || o == PHIA_LE)
			ii->itr = bps_tree_svindex_itr_last(tree);
	}
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

	if (ii->order == PHIA_GT || ii->order == PHIA_GE) {
		bps_tree_svindex_itr_next(&ii->index->tree, &ii->itr);
	} else {
		assert(ii->order == PHIA_LT || ii->order == PHIA_LE);
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
sv_upsertviflsnset(struct sv *v ssunused, uint64_t lsn ssunused) {
	assert(0);
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
sv_vifflags(struct sv *v) {
	return ((struct phia_tuple*)v->v)->flags;
}

static uint64_t
sv_viflsn(struct sv *v) {
	return ((struct phia_tuple*)v->v)->lsn;
}

static void
sv_viflsnset(struct sv *v, uint64_t lsn) {
	((struct phia_tuple*)v->v)->lsn = lsn;
}

static char*
sv_vifpointer(struct sv *v) {
	return ((struct phia_tuple*)v->v)->data;
}

static uint32_t
sv_vifsize(struct sv *v) {
	return ((struct phia_tuple*)v->v)->size;
}

static struct svif sv_vif =
{
	.flags     = sv_vifflags,
	.lsn       = sv_viflsn,
	.lsnset    = sv_viflsnset,
	.pointer   = sv_vifpointer,
	.size      = sv_vifsize
};

struct PACKED sxv {
	uint64_t id;
	uint32_t lo;
	uint64_t csn;
	void *index;
	struct phia_tuple *v;
	struct sxv *next;
	struct sxv *prev;
	struct sxv *gc;
	struct ssrbnode node;
};

struct sxvpool {
	struct sxv *head;
	int n;
	struct runtime *r;
};

static inline void
sx_vpool_init(struct sxvpool *p, struct runtime *r)
{
	p->head = NULL;
	p->n = 0;
	p->r = r;
}

static inline void
sx_vpool_free(struct sxvpool *p)
{
	struct sxv *n, *c = p->head;
	while (c) {
		n = c->next;
		ss_free(p->r->a, c);
		c = n;
	}
}

static inline struct sxv*
sx_vpool_pop(struct sxvpool *p)
{
	if (unlikely(p->n == 0))
		return NULL;
	struct sxv *v = p->head;
	p->head = v->next;
	p->n--;
	return v;
}

static inline void
sx_vpool_push(struct sxvpool *p, struct sxv *v)
{
	v->v    = NULL;
	v->next = NULL;
	v->prev = NULL;
	v->next = p->head;
	p->head = v;
	p->n++;
}

static inline struct sxv*
sx_valloc(struct sxvpool *p, struct phia_tuple *ref)
{
	struct sxv *v = sx_vpool_pop(p);
	if (unlikely(v == NULL)) {
		v = ss_malloc(p->r->a, sizeof(struct sxv));
		if (unlikely(v == NULL))
			return NULL;
	}
	v->index = NULL;
	v->id    = 0;
	v->lo    = 0;
	v->csn   = 0;
	v->v     = ref;
	v->next  = NULL;
	v->prev  = NULL;
	v->gc    = NULL;
	memset(&v->node, 0, sizeof(v->node));
	return v;
}

static inline void
sx_vfree(struct sxvpool *p, struct sxv *v)
{
	phia_tuple_unref(p->r, v->v);
	sx_vpool_push(p, v);
}

static inline void
sx_vfreeall(struct sxvpool *p, struct sxv *v)
{
	while (v) {
		struct sxv *next = v->next;
		sx_vfree(p, v);
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
	v->v->flags |= SVCONFLICT;
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
	return v->v->flags & SVCONFLICT;
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

struct sxindex {
	struct ssrb      i;
	uint32_t  dsn;
	struct phia_index *db;
	struct si *index;
	struct sfscheme *scheme;
	struct rlist    link;
	pthread_mutex_t mutex;
};

struct sx;
struct sicache;

typedef int (*sxpreparef)(struct sx*, struct sv*, struct phia_index*,
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
	struct ssrbnode   node;
	struct sxmanager *manager;
};

struct sxmanager {
	pthread_mutex_t  lock;
	struct rlist      indexes;
	struct ssrb        i;
	uint32_t    count_rd;
	uint32_t    count_rw;
	uint32_t    count_gc;
	uint64_t    csn;
	struct sxv        *gc;
	struct sxvpool     pool;
	struct runtime         *r;
};

static int sx_managerinit(struct sxmanager*, struct runtime*);
static int sx_managerfree(struct sxmanager*);
static int sx_indexinit(struct sxindex *, struct sxmanager *,
			struct phia_index *, struct si *, struct sfscheme *scheme);
static int sx_indexset(struct sxindex*, uint32_t);
static int sx_indexfree(struct sxindex*, struct sxmanager*);
static struct sx *sx_find(struct sxmanager*, uint64_t);
static void sx_init(struct sxmanager*, struct sx*, struct svlog*);
static enum sxstate sx_begin(struct sxmanager*, struct sx*, enum sxtype, struct svlog*, uint64_t);
static void sx_gc(struct sx*);
static enum sxstate sx_prepare(struct sx*, sxpreparef, void*);
static enum sxstate sx_commit(struct sx*);
static enum sxstate sx_rollback(struct sx*);
static int sx_set(struct sx*, struct sxindex*, struct phia_tuple*);
static int sx_get(struct sx*, struct sxindex*, struct phia_tuple*, struct phia_tuple**);
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
	ss_rbinit(&m->i);
	m->count_rd = 0;
	m->count_rw = 0;
	m->count_gc = 0;
	m->csn = 0;
	m->gc  = NULL;
	tt_pthread_mutex_init(&m->lock, NULL);
	rlist_create(&m->indexes);
	sx_vpool_init(&m->pool, r);
	m->r = r;
	return 0;
}

static int sx_managerfree(struct sxmanager *m)
{
	assert(sx_count(m) == 0);
	sx_vpool_free(&m->pool);
	tt_pthread_mutex_destroy(&m->lock);
	return 0;
}

static int sx_indexinit(struct sxindex *i, struct sxmanager *m,
			struct phia_index *db,
			struct si *index, struct sfscheme *scheme)
{
	ss_rbinit(&i->i);
	rlist_create(&i->link);
	i->dsn = 0;
	i->db = db;
	i->index = index;
	i->scheme = scheme;
	(void) tt_pthread_mutex_init(&i->mutex, NULL);
	rlist_add(&m->indexes, &i->link);
	return 0;
}

static int sx_indexset(struct sxindex *i, uint32_t dsn)
{
	i->dsn = dsn;
	return 0;
}

ss_rbtruncate(sx_truncate, sx_vfreeall(arg, container_of(n, struct sxv, node)))

static inline void
sx_indextruncate(struct sxindex *i, struct sxmanager *m)
{
	if (i->i.root == NULL)
		return;
	tt_pthread_mutex_lock(&i->mutex);
	sx_truncate(i->i.root, &m->pool);
	ss_rbinit(&i->i);
	tt_pthread_mutex_unlock(&i->mutex);
}

static int sx_indexfree(struct sxindex *i, struct sxmanager *m)
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
		struct ssrbnode *node = ss_rbmin(&m->i);
		struct sx *min = container_of(node, struct sx, node);
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
		struct ssrbnode *node = ss_rbmax(&m->i);
		struct sx *max = container_of(node, struct sx, node);
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
		struct ssrbnode *node = ss_rbmin(&m->i);
		struct sx *min = container_of(node, struct sx, node);
		vlsn = min->vlsn;
	} else {
		vlsn = sr_seq(m->r->seq, SR_LSN);
	}
	tt_pthread_mutex_unlock(&m->lock);
	return vlsn;
}

ss_rbget(sx_matchtx, ss_cmp((container_of(n, struct sx, node))->id, load_u64(key)))

static struct sx *sx_find(struct sxmanager *m, uint64_t id)
{
	struct ssrbnode *n = NULL;
	int rc = sx_matchtx(&m->i, NULL, (char*)&id, sizeof(id), &n);
	if (rc == 0 && n)
		return  container_of(n, struct sx, node);
	return NULL;
}

static void sx_init(struct sxmanager *m, struct sx *x, struct svlog *log)
{
	x->manager = m;
	x->log = log;
	rlist_create(&x->deadlock);
}

static inline enum sxstate
sx_promote(struct sx *x, enum sxstate state)
{
	x->state = state;
	return state;
}

static enum sxstate
sx_begin(struct sxmanager *m, struct sx *x, enum sxtype type,
	 struct svlog *log, uint64_t vlsn)
{
	sx_promote(x, SXREADY);
	x->type = type;
	x->log_read = -1;
	sr_seqlock(m->r->seq);
	x->csn = m->csn;
	x->id = sr_seqdo(m->r->seq, SR_TSNNEXT);
	if (likely(vlsn == UINT64_MAX))
		x->vlsn = sr_seqdo(m->r->seq, SR_LSN);
	else
		x->vlsn = vlsn;
	sr_sequnlock(m->r->seq);
	sx_init(m, x, log);
	tt_pthread_mutex_lock(&m->lock);
	struct ssrbnode *n = NULL;
	int rc = sx_matchtx(&m->i, NULL, (char*)&x->id, sizeof(x->id), &n);
	if (rc == 0 && n) {
		assert(0);
	} else {
		ss_rbset(&m->i, n, rc, &x->node);
	}
	if (type == SXRO)
		m->count_rd++;
	else
		m->count_rw++;
	tt_pthread_mutex_unlock(&m->lock);
	return SXREADY;
}

static inline void
sx_untrack(struct sxv *v)
{
	if (v->prev == NULL) {
		struct sxindex *i = v->index;
		tt_pthread_mutex_lock(&i->mutex);
		if (v->next == NULL)
			ss_rbremove(&i->i, &v->node);
		else
			ss_rbreplace(&i->i, &v->node, &v->next->node);
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
	struct ssrbnode *p = ss_rbmin(&m->i);
	struct sx *min = NULL;
	while (p) {
		min = container_of(p, struct sx, node);
		if (min->type == SXRO) {
			p = ss_rbnext(&m->i, p);
			continue;
		}
		break;
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
		assert(v->v->flags & SVGET);
		assert(sx_vcommitted(v));
		if (v->csn > min_csn) {
			v->gc = gc;
			gc = v;
			count++;
			continue;
		}
		sx_untrack(v);
		sx_vfree(&m->pool, v);
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
	ss_rbremove(&m->i, &x->node);
	if (x->type == SXRO)
		m->count_rd--;
	else
		m->count_rw--;
	tt_pthread_mutex_unlock(&m->lock);
}

static inline void
sx_rollback_svp(struct sx *x, struct ssbufiter *i, int free)
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
		/* translate log version from struct sxv to struct phia_tuple */
		sv_init(&lv->v, &sv_vif, v->v, NULL);
		if (free) {
			int size = phia_tuple_size((struct phia_tuple*)v->v);
			if (phia_tuple_unref(m->r, v->v))
				gc += size;
		}
		sx_vpool_push(&m->pool, v);
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
			struct phia_tuple *v = lv->v.v;
			int size = phia_tuple_size(v);
			if (phia_tuple_unref(m->r, v))
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
sx_preparecb(struct sx *x, struct svlogv *v, uint64_t lsn, sxpreparef prepare, void *arg)
{
	if (likely(lsn == x->vlsn))
		return 0;
	if (prepare) {
		struct sxindex *i = ((struct sxv*)v->v.v)->index;
		if (prepare(x, &v->v, i->db, arg))
			return 1;
	}
	return 0;
}

static enum sxstate sx_prepare(struct sx *x, sxpreparef prepare, void *arg)
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
			rc = sx_preparecb(x, lv, lsn, prepare, arg);
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
		if (v->prev->v->flags & SVGET) {
			rc = sx_preparecb(x, lv, lsn, prepare, arg);
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
			assert(v->prev->v->flags & SVGET);
			sx_vabort(v->prev);
		}
		/* abort waiters */
		sx_vabort_all(v->next);
		/* mark stmt as commited */
		sx_vcommit(v, csn);
		/* translate log version from struct sxv to struct phia_tuple */
		sv_init(&lv->v, &sv_vif, v->v, NULL);
		/* schedule read stmt for gc */
		if (v->v->flags & SVGET) {
			phia_tuple_ref(v->v);
			v->gc = m->gc;
			m->gc = v;
			m->count_gc++;
		} else {
			sx_untrack(v);
			sx_vpool_push(&m->pool, v);
		}
	}

	/* rollback latest reads */
	sx_rollback_svp(x, &i, 0);

	sx_promote(x, SXCOMMIT);
	sx_end(x);
	return SXCOMMIT;
}

ss_rbget(sx_match,
         sf_compare(scheme, ((container_of(n, struct sxv, node))->v)->data,
                    (container_of(n, struct sxv, node))->v->size,
                    key, keysize))

static int sx_set(struct sx *x, struct sxindex *index, struct phia_tuple *version)
{
	struct sxmanager *m = x->manager;
	struct runtime *r = m->r;
	if (! (version->flags & SVGET)) {
		x->log_read = -1;
	}
	/* allocate mvcc container */
	struct sxv *v = sx_valloc(&m->pool, version);
	if (unlikely(v == NULL)) {
		ss_quota(r->quota, SS_QREMOVE, phia_tuple_size(version));
		phia_tuple_unref(r, version);
		return -1;
	}
	v->id = x->id;
	v->index = index;
	struct svlogv lv;
	lv.id   = index->dsn;
	lv.next = UINT32_MAX;
	sv_init(&lv.v, &sx_vif, v, NULL);
	/* update concurrent index */
	tt_pthread_mutex_lock(&index->mutex);
	struct ssrbnode *n = NULL;
	int rc = sx_match(&index->i, index->scheme,
	                  version->data,
	                  version->size,
	                  &n);
	if (unlikely(rc == 0 && n)) {
		/* exists */
	} else {
		int pos = rc;
		/* unique */
		v->lo = sv_logcount(x->log);
		rc = sv_logadd(x->log, r->a, &lv, index->index);
		if (unlikely(rc == -1)) {
			sr_oom();
			goto error;
		}
		ss_rbset(&index->i, n, pos, &v->node);
		tt_pthread_mutex_unlock(&index->mutex);
		return 0;
	}
	struct sxv *head = container_of(n, struct sxv, node);
	/* match previous update made by current
	 * transaction */
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
		if (likely(head == own))
			ss_rbreplace(&index->i, &own->node, &v->node);
		/* update log */
		sv_logreplace(x->log, v->lo, &lv);

		ss_quota(r->quota, SS_QREMOVE, phia_tuple_size(own->v));
		sx_vfree(&m->pool, own);
		tt_pthread_mutex_unlock(&index->mutex);
		return 0;
	}
	/* update log */
	v->lo = sv_logcount(x->log);
	rc = sv_logadd(x->log, r->a, &lv, index->index);
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
	ss_quota(r->quota, SS_QREMOVE, phia_tuple_size(v->v));
	sx_vfree(&m->pool, v);
	return -1;
}

static int sx_get(struct sx *x, struct sxindex *index, struct phia_tuple *key,
		  struct phia_tuple **result)
{
	struct sxmanager *m = x->manager;
	struct ssrbnode *n = NULL;
	int rc;
	tt_pthread_mutex_lock(&index->mutex);
	rc = sx_match(&index->i, index->scheme,
	              key->data, key->size, &n);
	if (! (rc == 0 && n))
		goto add;
	struct sxv *head = container_of(n, struct sxv, node);
	struct sxv *v = sx_vmatch(head, x->id);
	if (v == NULL)
		goto add;
	tt_pthread_mutex_unlock(&index->mutex);
	if (unlikely((v->v->flags & SVGET) > 0))
		return 0;
	if (unlikely((v->v->flags & SVDELETE) > 0))
		return 2;
	struct sv vv;
	sv_init(&vv, &sv_vif, v->v, NULL);
	*result = phia_tuple_from_sv(m->r, &vv);
	if (unlikely(*result == NULL))
		return -1;
	return 1;

add:
	/* track a start of the latest read sequence in the
	 * transactional log */
	if (x->log_read == -1)
		x->log_read = sv_logcount(x->log);
	tt_pthread_mutex_unlock(&index->mutex);
	rc = sx_set(x, index, key);
	if (unlikely(rc == -1))
		return -1;
	phia_tuple_ref(key);
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
	return ((struct sxv*)v->v)->v->flags;
}

static uint64_t
sx_viflsn(struct sv *v) {
	return ((struct sxv*)v->v)->v->lsn;
}

static void
sx_viflsnset(struct sv *v, uint64_t lsn) {
	((struct sxv*)v->v)->v->lsn = lsn;
}

static char*
sx_vifpointer(struct sv *v) {
	return (((struct sxv*)v->v)->v)->data;
}

static uint32_t
sx_vifsize(struct sv *v) {
	return ((struct sxv*)v->v)->v->size;
}

static struct svif sx_vif =
{
	.flags     = sx_vifflags,
	.lsn       = sx_viflsn,
	.lsnset    = sx_viflsnset,
	.pointer   = sx_vifpointer,
	.size      = sx_vifsize
};

struct sltx {
	uint64_t lsn;
};

static int sl_begin(struct runtime *r, struct sltx *t, uint64_t lsn)
{
	if (likely(lsn == 0)) {
		lsn = sr_seq(r->seq, SR_LSNNEXT);
	} else {
		sr_seqlock(r->seq);
		if (lsn > r->seq->lsn)
			r->seq->lsn = lsn;
		sr_sequnlock(r->seq);
	}
	t->lsn = lsn;
	return 0;
}

static int sl_write(struct sltx *t, struct svlog *vlog)
{
	/*
	 * fast path for log-disabled, recover or
	 * ro-transactions
	 */
	struct ssbufiter i;
	ss_bufiter_open(&i, &vlog->buf, sizeof(struct svlogv));
	for (; ss_bufiter_has(&i); ss_bufiter_next(&i))
	{
		struct svlogv *v = ss_bufiter_get(&i);
		sv_lsnset(&v->v, t->lsn);
	}
	return 0;
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

static struct svif sd_vif;
static struct svif sd_vrawif;

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

static inline char*
sd_pagesparse_keyread(struct sdpage *p, uint32_t offset, uint32_t *size)
{
	char *ptr = (char*)p->h + sizeof(struct sdpageheader) +
	            (p->h->sizeorigin - p->h->sizekeys) + offset;
	*size = *(uint32_t*)ptr;
	return ptr + sizeof(uint32_t);
}

static inline char*
sd_pagesparse_field(struct sdpage *p, struct sdv *v, int pos, uint32_t *size)
{
	uint32_t *offsets = (uint32_t*)sd_pagepointer(p, v);
	return sd_pagesparse_keyread(p, offsets[pos], size);
}

static inline void
sd_pagesparse_convert(struct sdpage *p, struct sfscheme *scheme,
		      struct sdv *v, char *dest)
{
	char *ptr = dest;
	memcpy(ptr, v, sizeof(struct sdv));
	ptr += sizeof(struct sdv);
	struct phia_field fields[8];
	int i = 0;
	while (i < scheme->fields_count) {
		struct phia_field *k = &fields[i];
		k->data = sd_pagesparse_field(p, v, i, &k->size);
		i++;
	}
	sf_write(scheme, fields, ptr);
}

struct PACKED sdpageiter {
	struct sdpage *page;
	struct ssbuf *xfbuf;
	int64_t pos;
	struct sdv *v;
	struct sv current;
	enum phia_order order;
	void *key;
	int keysize;
	struct sfscheme *scheme;
};

static inline void
sd_pageiter_result(struct sdpageiter *i)
{
	if (unlikely(i->v == NULL))
		return;
	if (likely(i->scheme->fmt_storage == SF_RAW)) {
		sv_init(&i->current, &sd_vif, i->v, i->page->h);
		return;
	}
	sd_pagesparse_convert(i->page, i->scheme, i->v, i->xfbuf->s);
	sv_init(&i->current, &sd_vrawif, i->xfbuf->s, NULL);
}

static inline void
sd_pageiter_end(struct sdpageiter *i)
{
	i->pos = i->page->h->count;
	i->v   = NULL;
}

static inline int
sd_pageiter_cmp(struct sdpageiter *i, struct sfscheme *scheme, struct sdv *v)
{
	if (likely(scheme->fmt_storage == SF_RAW)) {
		return sf_compare(scheme, sd_pagepointer(i->page, v),
		                  v->size, i->key, i->keysize);
	}
	struct sffield **part = scheme->keys;
	struct sffield **last = part + scheme->keys_count;
	int rc;
	while (part < last) {
		struct sffield *key = *part;
		uint32_t a_fieldsize;
		char *a_field = sd_pagesparse_field(i->page, v, key->position, &a_fieldsize);
		uint32_t b_fieldsize;
		char *b_field = sf_fieldof_ptr(scheme, key, i->key, &b_fieldsize);
		rc = key->cmp(a_field, a_fieldsize, b_field, b_fieldsize, NULL);
		if (rc != 0)
			return rc;
		part++;
	}
	return 0;
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
		int rc = sd_pageiter_cmp(i, i->scheme, sd_pagev(i->page, mid));
		switch (rc) {
		case -1: min = mid + 1;
			continue;
		case  1: max = mid - 1;
			continue;
		default: return mid;
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
sd_pageiter_gt(struct sdpageiter *i, int e)
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
	int rc = sd_pageiter_cmp(i, i->scheme, i->v);
	int match = rc == 0;
	switch (rc) {
		case  0:
			if (e) {
				break;
			}
		case -1:
			sd_pageiter_chain_next(i);
			break;
	}
	return match;
}

static inline int
sd_pageiter_lt(struct sdpageiter *i, int e)
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
	int rc = sd_pageiter_cmp(i, i->scheme, i->v);
	int match = rc == 0;
	switch (rc) {
		case 0:
			if (e) {
				break;
			}
		case 1:
			sd_pageiter_chain_head(i, i->pos - 1);
			break;
	}
	return match;
}

static inline int
sd_pageiter_open(struct sdpageiter *pi, struct sfscheme *scheme,
		 struct ssbuf *xfbuf, struct sdpage *page, enum phia_order o,
		 void *key, int keysize)
{
	pi->scheme = scheme;
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
	case PHIA_GT:  rc = sd_pageiter_gt(pi, 0);
		break;
	case PHIA_GE: rc = sd_pageiter_gt(pi, 1);
		break;
	case PHIA_LT:  rc = sd_pageiter_lt(pi, 0);
		break;
	case PHIA_LE: rc = sd_pageiter_lt(pi, 1);
		break;
	default: assert(0);
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
	case PHIA_GE:
	case PHIA_GT:
		pi->pos++;
		if (unlikely(pi->pos >= pi->page->h->count)) {
			sd_pageiter_end(pi);
			return;
		}
		pi->v = sd_pagev(pi->page, pi->pos);
		break;
	case PHIA_LT:
	case PHIA_LE: {
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
	default: assert(0);
	}
	sd_pageiter_result(pi);
}

struct PACKED sdbuildref {
	uint32_t m, msize;
	uint32_t v, vsize;
	uint32_t k, ksize;
	uint32_t c, csize;
};

struct sdbuild {
	struct ssbuf list, m, v, k, c;
	struct ssfilterif *compress_if;
	int compress_dup;
	int compress;
	int crc;
	uint32_t vmax;
	uint32_t n;
	struct mh_strnptr_t *tracker;
	struct ssa *a;
	struct sfscheme *scheme;
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

static int sd_buildbegin(struct sdbuild*, struct ssa *a, struct sfscheme *scheme,
			 int, int, int, struct ssfilterif*);
static int sd_buildend(struct sdbuild*);
static int sd_buildcommit(struct sdbuild*);
static int sd_buildadd(struct sdbuild*, struct sv*, uint32_t);

#define SD_INDEXEXT_AMQF 1

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

struct PACKED sdindexamqf {
	uint8_t  q, r;
	uint32_t entries;
	uint32_t size;
	uint64_t table[];
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
sd_indexfree(struct sdindex *i, struct ssa *a) {
	ss_buffree(&i->i, a);
	ss_buffree(&i->v, a);
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

static inline struct sdindexamqf*
sd_indexamqf(struct sdindex *i) {
	struct sdindexheader *h = sd_indexheader(i);
	assert(h->extensions & SD_INDEXEXT_AMQF);
	return (struct sdindexamqf*)(i->i.s + sizeof(struct sdindexheader) + h->size);
}

static int sd_indexbegin(struct sdindex*, struct ssa *a);
static int sd_indexcommit(struct sdindex*, struct ssa *a, struct sdid*, struct ssqf*, uint64_t);
static int sd_indexadd(struct sdindex*, struct sdbuild*, uint64_t);
static int sd_indexcopy(struct sdindex*, struct ssa *a, struct sdindexheader*);

struct PACKED sdindexiter {
	struct sdindex *index;
	struct sdindexpage *v;
	int pos;
	enum phia_order cmp;
	void *key;
	int keysize;
	struct sfscheme *scheme;
};

static inline int
sd_indexiter_route(struct sdindexiter *i)
{
	int begin = 0;
	int end = i->index->h->count - 1;
	while (begin != end) {
		int mid = begin + (end - begin) / 2;
		struct sdindexpage *page = sd_indexpage(i->index, mid);
		int rc = sf_compare(i->scheme,
		                    sd_indexpage_max(i->index, page),
		                    page->sizemax,
		                    i->key,
		                    i->keysize);
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
sd_indexiter_open(struct sdindexiter *ii, struct sfscheme *scheme,
		  struct sdindex *index, enum phia_order o, void *key,
		  int keysize)
{
	ii->scheme = scheme;
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
		case PHIA_LT:
		case PHIA_LE: ii->pos = ii->index->h->count - 1;
			break;
		case PHIA_GT:
		case PHIA_GE: ii->pos = 0;
			break;
		default:
			assert(0);
		}
		ii->v = sd_indexpage(ii->index, ii->pos);
		return 0;
	}
	if (likely(ii->index->h->count > 1))
		ii->pos = sd_indexiter_route(ii);

	struct sdindexpage *p = sd_indexpage(ii->index, ii->pos);
	int rc;
	switch (ii->cmp) {
	case PHIA_LE:
	case PHIA_LT:
		rc = sf_compare(ii->scheme, sd_indexpage_min(ii->index, p),
		                p->sizemin, ii->key, ii->keysize);
		if (rc ==  1 || (rc == 0 && ii->cmp == PHIA_LT))
			ii->pos--;
		break;
	case PHIA_GE:
	case PHIA_GT:
		rc = sf_compare(ii->scheme, sd_indexpage_max(ii->index, p),
		                p->sizemax, ii->key, ii->keysize);
		if (rc == -1 || (rc == 0 && ii->cmp == PHIA_GT))
			ii->pos++;
		break;
	default: assert(0);
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
	case PHIA_LT:
	case PHIA_LE: ii->pos--;
		break;
	case PHIA_GT:
	case PHIA_GE: ii->pos++;
		break;
	default:
		assert(0);
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
	struct ssqf qf;
	struct svupsert upsert;
	struct ssbuf a;        /* result */
	struct ssbuf b;        /* redistribute buffer */
	struct ssbuf c;        /* file buffer */
	struct ssbuf d;        /* page read buffer */
	struct sdcbuf *head;   /* compression buffer list */
	int count;
};

struct phia_service {
	struct phia_env *env;
	struct sdc sdc;
};

static inline void
sd_cinit(struct sdc *sc)
{
	sv_upsertinit(&sc->upsert);
	sd_buildinit(&sc->build);
	ss_qfinit(&sc->qf);
	ss_bufinit(&sc->a);
	ss_bufinit(&sc->b);
	ss_bufinit(&sc->c);
	ss_bufinit(&sc->d);
	sc->count = 0;
	sc->head = NULL;
}

static inline void
sd_cfree(struct sdc *sc, struct ssa *a)
{
	sd_buildfree(&sc->build);
	ss_qffree(&sc->qf, a);
	sv_upsertfree(&sc->upsert, a);
	ss_buffree(&sc->a, a);
	ss_buffree(&sc->b, a);
	ss_buffree(&sc->c, a);
	ss_buffree(&sc->d, a);
	struct sdcbuf *b = sc->head;
	struct sdcbuf *next;
	while (b) {
		next = b->next;
		ss_buffree(&b->a, a);
		ss_buffree(&b->b, a);
		ss_free(a, b);
		b = next;
	}
}

static inline void
sd_cgc(struct sdc *sc, struct ssa *a, int wm)
{
	sd_buildgc(&sc->build, wm);
	ss_qfgc(&sc->qf, a, wm);
	sv_upsertgc(&sc->upsert, a, 600, 512);
	ss_bufgc(&sc->a, a, wm);
	ss_bufgc(&sc->b, a, wm);
	ss_bufgc(&sc->c, a, wm);
	ss_bufgc(&sc->d, a, wm);
	struct sdcbuf *it = sc->head;
	while (it) {
		ss_bufgc(&it->a, a, wm);
		ss_bufgc(&it->b, a, wm);
		it = it->next;
	}
}

static inline int
sd_censure(struct sdc *c, struct ssa *a, int count)
{
	if (c->count < count) {
		while (count-- >= 0) {
			struct sdcbuf *buf =
				ss_malloc(a, sizeof(struct sdcbuf));
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
	uint32_t    compression_key;
	uint32_t    compression;
	struct ssfilterif *compression_if;
	uint32_t    amqf;
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
	struct ssqf        *qf;
	uint64_t    processed;
	uint64_t    current;
	uint64_t    limit;
	int         resume;
};

static int
sd_mergeinit(struct sdmerge*, struct svmergeiter*, struct sdbuild*,
	     struct ssqf*, struct svupsert*, struct sdmergeconf*);
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
	enum phia_order     o;
	int         has;
	uint64_t    has_vlsn;
	int         use_compression;
	struct ssfilterif *compression_if;
	struct ssa *a;
	struct sfscheme *scheme;
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
	int rc = ss_bufensure(arg->buf, arg->a, ref->sizeorigin);
	if (unlikely(rc == -1))
		return sr_oom();
	ss_bufreset(arg->buf_xf);
	rc = ss_bufensure(arg->buf_xf, arg->a, arg->index->h->sizevmax);
	if (unlikely(rc == -1))
		return sr_oom();

	i->reads++;

	/* compression */
	if (arg->use_compression)
	{
		char *page_pointer;
		ss_bufreset(arg->buf_read);
		rc = ss_bufensure(arg->buf_read, arg->a, ref->size);
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
		rc = ss_filterinit(&f, arg->compression_if, arg->a, SS_FOUTPUT);
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
	return sd_pageiter_open(arg->page_iter, arg->scheme,
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
	sd_indexiter_open(arg->index_iter, arg->scheme, arg->index,
			  arg->o, key, keysize);
	i->ref = sd_indexiter_get(arg->index_iter);
	if (i->ref == NULL)
		return 0;
	if (arg->has) {
		assert(arg->o == PHIA_GE);
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
	b->tracker = NULL;
	ss_bufinit(&b->list);
	ss_bufinit(&b->m);
	ss_bufinit(&b->v);
	ss_bufinit(&b->c);
	ss_bufinit(&b->k);
	b->n = 0;
	b->compress = 0;
	b->compress_dup = 0;
	b->compress_if = NULL;
	b->crc = 0;
	b->vmax = 0;
}

static inline void
sd_buildfree_tracker(struct sdbuild *b)
{
	if (!b->tracker)
		return;
	mh_int_t i;
	mh_foreach(b->tracker, i) {
		ss_free(b->a, mh_strnptr_node(b->tracker, i)->val);
	}
	mh_strnptr_clear(b->tracker);
}

static void sd_buildfree(struct sdbuild *b)
{
	if (b->tracker) {
		sd_buildfree_tracker(b);
		mh_strnptr_delete(b->tracker);
		b->tracker = NULL;
	}
	ss_buffree(&b->list, b->a);
	ss_buffree(&b->m, b->a);
	ss_buffree(&b->v, b->a);
	ss_buffree(&b->c, b->a);
	ss_buffree(&b->k, b->a);
}

static void sd_buildreset(struct sdbuild *b)
{
	sd_buildfree_tracker(b);
	ss_bufreset(&b->list);
	ss_bufreset(&b->m);
	ss_bufreset(&b->v);
	ss_bufreset(&b->c);
	ss_bufreset(&b->k);
	b->n = 0;
	b->vmax = 0;
}

static void sd_buildgc(struct sdbuild *b, int wm)
{
	sd_buildfree_tracker(b);
	ss_bufgc(&b->list, b->a, wm);
	ss_bufgc(&b->m, b->a, wm);
	ss_bufgc(&b->v, b->a, wm);
	ss_bufgc(&b->c, b->a, wm);
	ss_bufgc(&b->k, b->a, wm);
	b->n = 0;
	b->vmax = 0;
}

static int
sd_buildbegin(struct sdbuild *b, struct ssa *a, struct sfscheme *scheme,
	      int crc, int compress_dup,
	      int compress, struct ssfilterif *compress_if)
{
	b->a = a;
	b->scheme = scheme;
	b->crc = crc;
	b->compress_dup = compress_dup;
	b->compress = compress;
	b->compress_if = compress_if;
	if (!b->tracker) {
		b->tracker = mh_strnptr_new();
		if (!b->tracker)
			return sr_oom();
	}
	int rc;
	if (compress_dup && mh_size(b->tracker) == 0) {
		if (mh_strnptr_reserve(b->tracker, 32768, NULL) == -1)
			return sr_oom();
	}
	rc = ss_bufensure(&b->list, b->a, sizeof(struct sdbuildref));
	if (unlikely(rc == -1))
		return sr_oom();
	struct sdbuildref *ref =
		(struct sdbuildref*)ss_bufat(&b->list, sizeof(struct sdbuildref), b->n);
	ref->m     = ss_bufused(&b->m);
	ref->msize = 0;
	ref->v     = ss_bufused(&b->v);
	ref->vsize = 0;
	ref->k     = ss_bufused(&b->k);
	ref->ksize = 0;
	ref->c     = ss_bufused(&b->c);
	ref->csize = 0;
	rc = ss_bufensure(&b->m, b->a, sizeof(struct sdpageheader));
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

struct sdbuildkey {
	uint32_t offset;
	uint32_t offsetstart;
	uint32_t size;
};

static inline int
sd_buildadd_sparse(struct sdbuild *b, struct sv *v)
{
	int i = 0;
	for (; i < b->scheme->fields_count; i++)
	{
		uint32_t fieldsize;
		char *field = sv_field(v, b->scheme, i, &fieldsize);

		int offsetstart = ss_bufused(&b->k);
		int offset = (offsetstart - sd_buildref(b)->k);

		/* match a field copy */
		int is_duplicate = 0;
		if (b->compress_dup) {
			mh_int_t pos = mh_strnptr_find_inp(b->tracker, field, fieldsize);
			if (pos != mh_end(b->tracker)) {
				is_duplicate = 1;
				struct sdbuildkey *ref = (struct sdbuildkey *)
					mh_strnptr_node(b->tracker, pos)->val;
				offset = ref->offset;
			}
		}

		/* offset */
		int rc;
		rc = ss_bufensure(&b->v, b->a, sizeof(uint32_t));
		if (unlikely(rc == -1))
			return sr_oom();
		*(uint32_t*)b->v.p = offset;
		ss_bufadvance(&b->v, sizeof(uint32_t));
		if (is_duplicate)
			continue;

		/* copy field */
		rc = ss_bufensure(&b->k, b->a, sizeof(uint32_t) + fieldsize);
		if (unlikely(rc == -1))
			return sr_oom();
		*(uint32_t*)b->k.p = fieldsize;
		ss_bufadvance(&b->k, sizeof(uint32_t));
		memcpy(b->k.p, field, fieldsize);
		ss_bufadvance(&b->k, fieldsize);

		/* add field reference */
		if (b->compress_dup) {
			struct sdbuildkey *ref = ss_malloc(b->a, sizeof(struct sdbuildkey));
			if (unlikely(ref == NULL))
				return sr_oom();
			ref->offset = offset;
			ref->offsetstart = offsetstart + sizeof(uint32_t);
			ref->size = fieldsize;
			uint32_t hash = mh_strn_hash(field, fieldsize);
			const struct mh_strnptr_node_t strnode = {
				field, fieldsize, hash, ref};
			mh_int_t ins_pos = mh_strnptr_put(b->tracker, &strnode,
							  NULL, NULL);
			if (unlikely(ins_pos == mh_end(b->tracker)))
				return sr_error("%s",
						"Can't insert assoc array item");
		}
	}

	return 0;
}

static inline int
sd_buildadd_raw(struct sdbuild *b, struct sv *v, uint32_t size)
{
	int rc = ss_bufensure(&b->v, b->a, size);
	if (unlikely(rc == -1))
		return sr_oom();
	memcpy(b->v.p, sv_pointer(v), size);
	ss_bufadvance(&b->v, size);
	return 0;
}

int sd_buildadd(struct sdbuild *b, struct sv *v, uint32_t flags)
{
	/* prepare document metadata */
	int rc = ss_bufensure(&b->m, b->a, sizeof(struct sdv));
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
	switch (b->scheme->fmt_storage) {
	case SF_RAW:
		rc = sd_buildadd_raw(b, v, size);
		break;
	case SF_SPARSE:
		rc = sd_buildadd_sparse(b, v);
		break;
	}
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
	int rc = ss_bufensure(&b->c, b->a, sizeof(struct sdpageheader));
	if (unlikely(rc == -1))
		return -1;
	ss_bufadvance(&b->c, sizeof(struct sdpageheader));
	/* compression (including meta-data) */
	struct sdbuildref *ref = sd_buildref(b);
	struct ssfilter f;
	rc = ss_filterinit(&f, b->compress_if, b->a, SS_FINPUT);
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
	rc = ss_filternext(&f, &b->c, b->k.s + ref->k, ref->ksize);
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
	ref->ksize = ss_bufused(&b->k) - ref->k;
	ref->csize = 0;
	/* calculate data crc (non-compressed) */
	struct sdpageheader *h = sd_buildheader(b);
	uint32_t crc = 0;
	if (likely(b->crc)) {
		crc = ss_crcp(b->m.s + ref->m, ref->msize, 0);
		crc = ss_crcp(b->v.s + ref->v, ref->vsize, crc);
		crc = ss_crcp(b->k.s + ref->k, ref->ksize, crc);
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
	int total = ref->msize + ref->vsize + ref->ksize;
	h->sizekeys = ref->ksize;
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
	if (b->compress_dup)
		sd_buildfree_tracker(b);
	if (b->compress) {
		ss_bufreset(&b->m);
		ss_bufreset(&b->v);
		ss_bufreset(&b->k);
	}
	b->n++;
	return 0;
}

static int sd_indexbegin(struct sdindex *i, struct ssa *a)
{
	int rc = ss_bufensure(&i->i, a, sizeof(struct sdindexheader));
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
sd_indexcommit(struct sdindex *i, struct ssa *a, struct sdid *id,
	       struct ssqf *qf, uint64_t offset)
{
	int size = ss_bufused(&i->v);
	int size_extension = 0;
	int extensions = 0;
	if (qf) {
		extensions = SD_INDEXEXT_AMQF;
		size_extension += sizeof(struct sdindexamqf);
		size_extension += qf->qf_table_size;
	}
	int rc = ss_bufensure(&i->i, a, size + size_extension);
	if (unlikely(rc == -1))
		return sr_oom();
	memcpy(i->i.p, i->v.s, size);
	ss_bufadvance(&i->i, size);
	if (qf) {
		struct sdindexamqf *qh = (struct sdindexamqf*)(i->i.p);
		qh->q       = qf->qf_qbits;
		qh->r       = qf->qf_rbits;
		qh->entries = qf->qf_entries;
		qh->size    = qf->qf_table_size;
		ss_bufadvance(&i->i, sizeof(struct sdindexamqf));
		memcpy(i->i.p, qf->qf_table, qf->qf_table_size);
		ss_bufadvance(&i->i, qf->qf_table_size);
	}
	ss_buffree(&i->v, a);
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
	p->sizemin = sf_comparable_size(build->scheme, min);
	p->sizemax = sf_comparable_size(build->scheme, max);
	int rc = ss_bufensure(&i->v, build->a, p->sizemin + p->sizemax);
	if (unlikely(rc == -1))
		return sr_oom();
	sf_comparable_write(build->scheme, min, i->v.p);
	ss_bufadvance(&i->v, p->sizemin);
	sf_comparable_write(build->scheme, max, i->v.p);
	ss_bufadvance(&i->v, p->sizemax);
	return 0;
}

static inline int
sd_indexadd_sparse(struct sdindex *i, struct sdbuild *build,
		   struct sdindexpage *p, char *min, char *max)
{
	struct phia_field fields[16];

	/* min */
	int part = 0;
	while (part < build->scheme->fields_count)
	{
		/* read field offset */
		uint32_t offset = *(uint32_t*)min;
		min += sizeof(uint32_t);
		/* read field */
		char *field = build->k.s + sd_buildref(build)->k + offset;
		int fieldsize = *(uint32_t*)field;
		field += sizeof(uint32_t);
		/* copy only key fields, others are set to zero */
		struct phia_field *k = &fields[part];
		if (build->scheme->fields[part]->key) {
			k->data = field;
			k->size = fieldsize;
		} else {
			k->data = NULL;
			k->size = 0;
		}
		part++;
	}
	p->sizemin = sf_writesize(build->scheme, fields);
	int rc = ss_bufensure(&i->v, build->a, p->sizemin);
	if (unlikely(rc == -1))
		return sr_oom();
	sf_write(build->scheme, fields, i->v.p);
	ss_bufadvance(&i->v, p->sizemin);

	/* max */
	part = 0;
	while (part < build->scheme->fields_count)
	{
		/* read field offset */
		uint32_t offset = *(uint32_t*)max;
		max += sizeof(uint32_t);

		/* read field */
		char *field = build->k.s + sd_buildref(build)->k + offset;
		int fieldsize = *(uint32_t*)field;
		field += sizeof(uint32_t);

		struct phia_field *k = &fields[part];
		if (build->scheme->fields[part]->key) {
			k->data = field;
			k->size = fieldsize;
		} else {
			k->data = NULL;
			k->size = 0;
		}
		part++;
	}
	p->sizemax = sf_writesize(build->scheme, fields);
	rc = ss_bufensure(&i->v, build->a, p->sizemax);
	if (unlikely(rc == -1))
		return sr_oom();
	sf_write(build->scheme, fields, i->v.p);
	ss_bufadvance(&i->v, p->sizemax);
	return 0;
}

static int
sd_indexadd(struct sdindex *i, struct sdbuild *build, uint64_t offset)
{
	int rc = ss_bufensure(&i->i, build->a, sizeof(struct sdindexpage));
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
		switch (build->scheme->fmt_storage) {
		case SF_RAW:
			rc = sd_indexadd_raw(i, build, p, min, max);
			break;
		case SF_SPARSE:
			rc = sd_indexadd_sparse(i, build, p, min, max);
			break;
		}
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

static int sd_indexcopy(struct sdindex *i, struct ssa *a, struct sdindexheader *h)
{
	int size = sd_indexsize_ext(h);
	int rc = ss_bufensure(&i->i, a, size);
	if (unlikely(rc == -1))
		return sr_oom();
	memcpy(i->i.s, (char*)h, size);
	ss_bufadvance(&i->i, size);
	i->h = sd_indexheader(i);
	return 0;
}

static int
sd_mergeinit(struct sdmerge *m, struct svmergeiter *im,
	     struct sdbuild *build, struct ssqf *qf,
	     struct svupsert *upsert, struct sdmergeconf *conf)
{
	m->conf      = conf;
	m->build     = build;
	m->qf        = qf;
	m->merge     = im;
	m->processed = 0;
	m->current   = 0;
	m->limit     = 0;
	m->resume    = 0;
	if (conf->amqf) {
		int rc = ss_qfensure(qf, im->merge->a, conf->stream);
		if (unlikely(rc == -1))
			return sr_oom();
	}
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
	sd_indexfree(&m->index, m->merge->merge->a);
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
	int rc = sd_indexbegin(&m->index, m->merge->merge->a);
	if (unlikely(rc == -1))
		return -1;
	if (conf->amqf)
		ss_qfreset(m->qf);
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
	rc = sd_buildbegin(m->build, m->merge->merge->a, m->merge->merge->scheme,
			   conf->checksum, conf->compression_key,
	                   conf->compression, conf->compression_if);
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
		if (conf->amqf) {
			ss_qfadd(m->qf, sv_hash(v, m->merge->merge->scheme));
		}
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
	struct ssqf *qf = NULL;
	if (m->conf->amqf)
		qf = m->qf;
	return sd_indexcommit(&m->index, m->merge->merge->a, id, qf, offset);
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
	int rc = ss_vfsmmap(r->vfs, &ri->map, ri->file->fd, ri->file->size, 1);
	if (unlikely(rc == -1)) {
		sr_malfunction("failed to mmap index file '%s': %s",
		               ss_pathof(&ri->file->path),
		               strerror(errno));
		return -1;
	}
	struct sdseal *seal = (struct sdseal*)((char*)ri->map.p);
	rc = sd_recover_next_of(ri, seal);
	if (unlikely(rc == -1))
		ss_vfsmunmap(r->vfs, &ri->map);
	return rc;
}

static void
sd_recover_close(struct sdrecover *ri)
{
	ss_vfsmunmap(ri->r->vfs, &ri->map);
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

static char*
sd_vrawifpointer(struct sv *v)
{
	return (char*)v->v + sizeof(struct sdv);
}

static struct svif sd_vrawif =
{
	.flags     = sd_vifflags,
	.lsn       = sd_viflsn,
	.lsnset    = NULL,
	.pointer   = sd_vrawifpointer,
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
		ss_iovadd(&iov, b->k.s + ref->k, ref->ksize);
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
	uint32_t    path_fail_on_exists;
	uint32_t    path_fail_on_drop;
	uint32_t    sync;
	uint64_t    node_size;
	uint32_t    node_page_size;
	uint32_t    node_page_checksum;
	uint32_t    compression;
	char       *compression_sz;
	struct ssfilterif *compression_if;
	uint32_t    compression_branch;
	char       *compression_branch_sz;
	struct ssfilterif *compression_branch_if;
	uint32_t    compression_key;
	uint32_t    temperature;
	uint32_t    amqf;
	uint64_t    lru;
	uint32_t    lru_step;
	uint32_t    buf_gc_wm;
	struct srversion   version;
	struct srversion   version_storage;
};

static void si_confinit(struct siconf*);
static void si_conffree(struct siconf*, struct ssa*);

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
si_branchnew(struct runtime *r)
{
	struct sibranch *b = (struct sibranch*)ss_malloc(r->a, sizeof(struct sibranch));
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
si_branchfree(struct sibranch *b, struct ssa *a)
{
	sd_indexfree(&b->index, a);
	ss_free(a, b);
}

static inline int
si_branchis_root(struct sibranch *b) {
	return b->next == NULL;
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
	struct ssrbnode   node;
	struct ssrqnode   nodecompact;
	struct ssrqnode   nodebranch;
	struct ssrqnode   nodetemp;
	struct rlist     gc;
	struct rlist     commit;
};

static struct sinode *si_nodenew(struct sfscheme *scheme, struct runtime *r);
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
	sv_indexinit(&node->i1, node->i0.scheme);
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

static inline struct sinode*
si_nodeof(struct ssrbnode *node) {
	return container_of(node, struct sinode, node);
}

static inline int
si_nodecmp(struct sinode *n, void *key, int size, struct sfscheme *s)
{
	struct sdindexpage *min = sd_indexmin(&n->self.index);
	struct sdindexpage *max = sd_indexmax(&n->self.index);
	int l = sf_compare(s, sd_indexpage_min(&n->self.index, min),
	                   min->sizemin, key, size);
	int r = sf_compare(s, sd_indexpage_max(&n->self.index, max),
	                   max->sizemax, key, size);
	/* inside range */
	if (l <= 0 && r >= 0)
		return 0;
	/* key > range */
	if (l == -1)
		return -1;
	/* key < range */
	assert(r == 1);
	return 1;
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
static int si_plannerinit(struct siplanner*, struct ssa*, void*);
static int si_plannerfree(struct siplanner*, struct ssa*);
static int si_plannerupdate(struct siplanner*, int, struct sinode*);
static int si_plannerremove(struct siplanner*, int, struct sinode*);
static int si_planner(struct siplanner*, struct siplan*);

static inline int
si_amqfhas_branch(struct sfscheme *scheme, struct sibranch *b, char *key)
{
	struct sdindexamqf *qh = sd_indexamqf(&b->index);
	struct ssqf qf;
	ss_qfrecover(&qf, qh->q, qh->r, qh->size, qh->table);
	return ss_qfhas(&qf, sf_hash(scheme, key));
}

enum siref {
	SI_REFFE,
	SI_REFBE
};

struct si {
	struct srstatus   status;
	pthread_mutex_t    lock;
	struct siplanner  p;
	struct ssrb       i;
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
	uint32_t   ref_fe;
	uint32_t   ref_be;
	uint32_t   gc_count;
	struct rlist     gc;
	struct ssbuf      readbuf;
	struct svupsert   u;
	struct siconf   conf;
	struct sfscheme    scheme;
	struct phia_index *db;
	struct runtime *r;
	struct rlist     link;
};

static inline int
si_active(struct si *i) {
	return sr_statusactive(&i->status);
}

static inline void
si_lock(struct si *i) {
	tt_pthread_mutex_lock(&i->lock);
}

static inline void
si_unlock(struct si *i) {
	tt_pthread_mutex_unlock(&i->lock);
}

static struct si *si_init(struct runtime*, struct phia_index*);
static int si_recover(struct si*);
static int si_close(struct si*);
static int si_insert(struct si*, struct sinode*);
static int si_remove(struct si*, struct sinode*);
static int si_replace(struct si*, struct sinode*, struct sinode*);
static int si_refs(struct si*);
static int si_ref(struct si*, enum siref);
static int si_unref(struct si*, enum siref);
static int si_plan(struct si*, struct siplan*);
static int
si_execute(struct si*, struct sdc*, struct siplan*, uint64_t, uint64_t);

static inline void
si_lru_add(struct si *i, struct svref *ref)
{
	i->lru_intr_sum += ref->v->size;
	if (unlikely(i->lru_intr_sum >= i->conf.lru_step))
	{
		uint64_t lsn = sr_seq(i->r->seq, SR_LSN);
		i->lru_v += (lsn - i->lru_intr_lsn);
		i->lru_steps++;
		i->lru_intr_lsn = lsn;
		i->lru_intr_sum = 0;
	}
}

static inline uint64_t
si_lru_vlsn_of(struct si *i)
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
si_lru_vlsn(struct si *i)
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
	struct ssa *a = c->pool->r->a;
	struct sicachebranch *next;
	struct sicachebranch *cb = c->path;
	while (cb) {
		next = cb->next;
		ss_buffree(&cb->buf_a, a);
		ss_buffree(&cb->buf_b, a);
		ss_free(a, cb);
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
si_cacheadd(struct sicache *c, struct sibranch *b)
{
	struct sicachebranch *nb =
		ss_malloc(c->pool->r->a, sizeof(struct sicachebranch));
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
			struct sicachebranch *nb = si_cacheadd(c, b);
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
		cb = si_cacheadd(c, b);
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
		ss_free(p->r->a, c);
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
		c = ss_malloc(p->r->a, sizeof(struct sicache));
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
	struct si *index;
};

static void si_begin(struct sitx*, struct si*);
static void si_commit(struct sitx*);

static inline void
si_txtrack(struct sitx *x, struct sinode *n) {
	if (rlist_empty(&n->commit))
		rlist_add(&x->nodelist, &n->commit);
}

static void
si_write(struct sitx*, struct svlog*, struct svlogindex*, uint64_t, int);

struct siread {
	enum phia_order   order;
	void     *prefix;
	void     *key;
	uint32_t  keysize;
	uint32_t  prefixsize;
	int       has;
	uint64_t  vlsn;
	struct svmerge   merge;
	int       cache_only;
	int       oldest_only;
	int       read_disk;
	int       read_cache;
	struct sv       *upsert_v;
	int       upsert_eq;
	struct phia_tuple *result;
	struct sicache  *cache;
	struct si       *index;
};

static int
si_readopen(struct siread*, struct si*, struct sicache*, enum phia_order,
	    uint64_t,
	    void*, uint32_t,
	    void*, uint32_t);
static int si_readclose(struct siread*);
static int  si_read(struct siread*);
static int  si_readcommited(struct si*, struct sv*, int);

struct PACKED siiter {
	struct si *index;
	struct ssrbnode *v;
	enum phia_order order;
	void *key;
	int keysize;
};

ss_rbget(si_itermatch,
         si_nodecmp(container_of(n, struct sinode, node), key, keysize, scheme))

static inline int
si_iter_open(struct siiter *ii, struct si *index, enum phia_order o,
	     void *key, int keysize)
{
	ii->index   = index;
	ii->order   = o;
	ii->key     = key;
	ii->keysize = keysize;
	ii->v       = NULL;
	int eq = 0;
	if (unlikely(index->n == 1)) {
		ii->v = ss_rbmin(&index->i);
		return 1;
	}
	if (unlikely(ii->key == NULL)) {
		switch (ii->order) {
		case PHIA_LT:
		case PHIA_LE:
			ii->v = ss_rbmax(&index->i);
			break;
		case PHIA_GT:
		case PHIA_GE:
			ii->v = ss_rbmin(&index->i);
			break;
		default:
			assert(0);
			break;
		}
		return 0;
	}
	/* route */
	assert(ii->key != NULL);
	int rc;
	rc = si_itermatch(&index->i, &index->scheme, ii->key, ii->keysize, &ii->v);
	if (unlikely(ii->v == NULL)) {
		assert(rc != 0);
		if (rc == 1)
			ii->v = ss_rbmin(&index->i);
		else
			ii->v = ss_rbmax(&index->i);
	} else {
		eq = rc == 0 && ii->v;
		if (rc == 1) {
			ii->v = ss_rbprev(&index->i, ii->v);
			if (unlikely(ii->v == NULL))
				ii->v = ss_rbmin(&index->i);
		}
	}
	assert(ii->v != NULL);
	return eq;
}

static inline void*
si_iter_get(struct siiter *ii)
{
	if (unlikely(ii->v == NULL))
		return NULL;
	struct sinode *n = si_nodeof(ii->v);
	return n;
}

static inline void
si_iter_next(struct siiter *ii)
{
	switch (ii->order) {
	case PHIA_LT:
	case PHIA_LE:
		ii->v = ss_rbprev(&ii->index->i, ii->v);
		break;
	case PHIA_GT:
	case PHIA_GE:
		ii->v = ss_rbnext(&ii->index->i, ii->v);
		break;
	default: assert(0);
	}
}

static int si_drop(struct si*);
static int si_dropmark(struct si*);
static int si_droprepository(struct runtime*, char*, int);

static int
si_merge(struct si*, struct sdc*, struct sinode*, uint64_t,
	 uint64_t, struct svmergeiter*, uint64_t, uint32_t);

static int si_branch(struct si*, struct sdc*, struct siplan*, uint64_t);
static int
si_compact(struct si*, struct sdc*, struct siplan*, uint64_t,
	   uint64_t, struct ssiter*, uint64_t);
static int
si_compact_index(struct si*, struct sdc*, struct siplan*, uint64_t, uint64_t);

struct sitrack {
	struct ssrb i;
	int count;
	uint64_t nsn;
	uint64_t lsn;
};

static inline void
si_trackinit(struct sitrack *t) {
	ss_rbinit(&t->i);
	t->count = 0;
	t->nsn = 0;
	t->lsn = 0;
}

ss_rbtruncate(si_tracktruncate,
              si_nodefree(container_of(n, struct sinode, node), (struct runtime*)arg, 0))

static inline void
si_trackfree(struct sitrack *t, struct runtime *r) {
	if (t->i.root)
		si_tracktruncate(t->i.root, r);
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

ss_rbget(si_trackmatch, ss_cmp((container_of(n, struct sinode, node))->self.id.id, load_u64(key)))

static inline void
si_trackset(struct sitrack *t, struct sinode *n)
{
	struct ssrbnode *p = NULL;
	int rc = si_trackmatch(&t->i, NULL, (char*)&n->self.id.id,
	                       sizeof(n->self.id.id), &p);
	assert(! (rc == 0 && p));
	ss_rbset(&t->i, p, rc, &n->node);
	t->count++;
}

static inline struct sinode*
si_trackget(struct sitrack *t, uint64_t id)
{
	struct ssrbnode *p = NULL;
	int rc = si_trackmatch(&t->i, NULL, (char*)&id, sizeof(id), &p);
	if (rc == 0 && p)
		return container_of(p, struct sinode, node);
	return NULL;
}

static inline void
si_trackreplace(struct sitrack *t, struct sinode *o, struct sinode *n)
{
	ss_rbreplace(&t->i, &o->node, &n->node);
}

static struct sinode *si_bootstrap(struct si*, uint64_t);
static int si_recover(struct si*);

struct PACKED siprofiler {
	uint32_t  total_node_count;
	uint64_t  total_node_size;
	uint64_t  total_node_origin_size;
	uint32_t  total_branch_count;
	uint32_t  total_branch_avg;
	uint32_t  total_branch_max;
	uint32_t  total_page_count;
	uint64_t  total_snapshot_size;
	uint64_t  total_amqf_size;
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
	struct si       *i;
};

static int si_profilerbegin(struct siprofiler*, struct si*);
static int si_profilerend(struct siprofiler*);
static int si_profiler(struct siprofiler*);

static struct si *si_init(struct runtime *r, struct phia_index *db)
{
	struct si *i = ss_malloc(r->a, sizeof(struct si));
	if (unlikely(i == NULL))
		return NULL;
	i->r = r;
	sr_statusinit(&i->status);
	int rc = si_plannerinit(&i->p, r->a, i);
	if (unlikely(rc == -1)) {
		ss_free(r->a, i);
		return NULL;
	}
	ss_bufinit(&i->readbuf);
	sv_upsertinit(&i->u);
	ss_rbinit(&i->i);
	tt_pthread_mutex_init(&i->lock, NULL);
	si_confinit(&i->conf);
	sf_schemeinit(&i->scheme);
	rlist_create(&i->link);
	rlist_create(&i->gc);
	i->gc_count     = 0;
	i->update_time  = 0;
	i->lru_run_lsn  = 0;
	i->lru_v        = 0;
	i->lru_steps    = 1;
	i->lru_intr_lsn = 0;
	i->lru_intr_sum = 0;
	i->size         = 0;
	i->read_disk    = 0;
	i->read_cache   = 0;
	i->n            = 0;
	tt_pthread_mutex_init(&i->ref_lock, NULL);
	i->ref_fe       = 0;
	i->ref_be       = 0;
	i->db           = db;
	return i;
}

ss_rbtruncate(si_truncate,
              si_nodefree(container_of(n, struct sinode, node), (struct runtime*)arg, 0))

static int si_close(struct si *i)
{
	int rc_ret = 0;
	int rc = 0;
	struct sinode *node, *n;
	rlist_foreach_entry_safe(node, &i->gc, gc, n) {
		rc = si_nodefree(node, i->r, 1);
		if (unlikely(rc == -1))
			rc_ret = -1;
	}
	rlist_create(&i->gc);
	i->gc_count = 0;
	if (i->i.root)
		si_truncate(i->i.root, i->r);
	i->i.root = NULL;
	sv_upsertfree(&i->u, i->r->a);
	ss_buffree(&i->readbuf, i->r->a);
	si_plannerfree(&i->p, i->r->a);
	tt_pthread_mutex_destroy(&i->lock);
	tt_pthread_mutex_destroy(&i->ref_lock);
	sr_statusfree(&i->status);
	si_conffree(&i->conf, i->r->a);
	sf_schemefree(&i->scheme, i->r->a);
	ss_free(i->r->a, i);
	return rc_ret;
}

ss_rbget(si_match,
         sf_compare(scheme,
                    sd_indexpage_min(&(container_of(n, struct sinode, node))->self.index,
                                     sd_indexmin(&(container_of(n, struct sinode, node))->self.index)),
                    sd_indexmin(&(container_of(n, struct sinode, node))->self.index)->sizemin,
                                key, keysize))

static int si_insert(struct si *i, struct sinode *n)
{
	struct sdindexpage *min = sd_indexmin(&n->self.index);
	struct ssrbnode *p = NULL;
	int rc = si_match(&i->i, &i->scheme,
	                  sd_indexpage_min(&n->self.index, min),
	                  min->sizemin, &p);
	assert(! (rc == 0 && p));
	ss_rbset(&i->i, p, rc, &n->node);
	i->n++;
	return 0;
}

static int si_remove(struct si *i, struct sinode *n)
{
	ss_rbremove(&i->i, &n->node);
	i->n--;
	return 0;
}

static int si_replace(struct si *i, struct sinode *o, struct sinode *n)
{
	ss_rbreplace(&i->i, &o->node, &n->node);
	return 0;
}

static int si_refs(struct si *i)
{
	tt_pthread_mutex_lock(&i->ref_lock);
	int v = i->ref_be + i->ref_fe;
	tt_pthread_mutex_unlock(&i->ref_lock);
	return v;
}

static int si_ref(struct si *i, enum siref ref)
{
	tt_pthread_mutex_lock(&i->ref_lock);
	if (ref == SI_REFBE)
		i->ref_be++;
	else
		i->ref_fe++;
	tt_pthread_mutex_unlock(&i->ref_lock);
	return 0;
}

static int si_unref(struct si *i, enum siref ref)
{
	int prev_ref = 0;
	tt_pthread_mutex_lock(&i->ref_lock);
	if (ref == SI_REFBE) {
		prev_ref = i->ref_be;
		if (i->ref_be > 0)
			i->ref_be--;
	} else {
		prev_ref = i->ref_fe;
		if (i->ref_fe > 0)
			i->ref_fe--;
	}
	tt_pthread_mutex_unlock(&i->ref_lock);
	return prev_ref;
}

static int si_plan(struct si *i, struct siplan *plan)
{
	si_lock(i);
	int rc = si_planner(&i->p, plan);
	si_unlock(i);
	return rc;
}

static int
si_execute(struct si *i, struct sdc *c, struct siplan *plan,
	   uint64_t vlsn, uint64_t vlsn_lru)
{
	int rc = -1;
	switch (plan->plan) {
	case SI_NODEGC:
		rc = si_nodefree(plan->node, i->r, 1);
		break;
	case SI_CHECKPOINT:
	case SI_BRANCH:
	case SI_AGE:
		rc = si_branch(i, c, plan, vlsn);
		break;
	case SI_LRU:
	case SI_GC:
	case SI_COMPACT:
		rc = si_compact(i, c, plan, vlsn, vlsn_lru, NULL, 0);
		break;
	case SI_COMPACT_INDEX:
		rc = si_compact_index(i, c, plan, vlsn, vlsn_lru);
		break;
	case SI_SHUTDOWN:
		rc = si_close(i);
		break;
	case SI_DROP:
		rc = si_drop(i);
		break;
	}
	/* garbage collect buffers */
	if (plan->plan != SI_SHUTDOWN &&
	    plan->plan != SI_DROP) {
		sd_cgc(c, i->r->a, i->conf.buf_gc_wm);
	}
	return rc;
}

static inline int
si_branchcreate(struct si *index, struct sdc *c,
		struct sinode *parent, struct svindex *vindex,
		uint64_t vlsn, struct sibranch **result)
{
	struct runtime *r = index->r;
	struct sibranch *branch = NULL;

	/* in-memory mode blob */
	int rc;
	struct svmerge vmerge;
	sv_mergeinit(&vmerge, r->a, &index->scheme);
	rc = sv_mergeprepare(&vmerge, 1);
	if (unlikely(rc == -1))
		return -1;
	struct svmergesrc *s = sv_mergeadd(&vmerge, NULL);
	sv_indexiter_open(&s->src, vindex, PHIA_GE, NULL, 0);
	struct svmergeiter im;
	sv_mergeiter_open(&im, &vmerge, PHIA_GE);

	/* merge iter is not used */
	struct sdmergeconf mergeconf = {
		.stream          = vindex->tree.size,
		.size_stream     = UINT32_MAX,
		.size_node       = UINT64_MAX,
		.size_page       = index->conf.node_page_size,
		.checksum        = index->conf.node_page_checksum,
		.compression_key = index->conf.compression_key,
		.compression     = index->conf.compression_branch,
		.compression_if  = index->conf.compression_branch_if,
		.amqf            = index->conf.amqf,
		.vlsn            = vlsn,
		.vlsn_lru        = 0,
		.save_delete     = 1,
		.save_upsert     = 1
	};
	struct sdmerge merge;
	rc = sd_mergeinit(&merge, &im, &c->build, &c->qf,
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
		branch = si_branchnew(r);
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
si_branch(struct si *index, struct sdc *c, struct siplan *plan, uint64_t vlsn)
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
si_compact(struct si *index, struct sdc *c, struct siplan *plan,
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
	rc = sd_censure(c, r->a, node->branch_count);
	if (unlikely(rc == -1))
		return sr_oom();
	struct svmerge merge;
	sv_mergeinit(&merge, r->a, &index->scheme);
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
		if (! si_branchis_root(b)) {
			compression    = index->conf.compression_branch;
			compression_if = index->conf.compression_branch_if;
		} else {
			compression    = index->conf.compression;
			compression_if = index->conf.compression_if;
		}
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
			.o               = PHIA_GE,
			.file            = &node->file,
			.a               = r->a,
			.scheme		 = &index->scheme
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
	sv_mergeiter_open(&im, &merge, PHIA_GE);
	rc = si_merge(index, c, node, vlsn, vlsn_lru, &im, size_stream, count);
	sv_mergefree(&merge);
	return rc;
}

static int
si_compact_index(struct si *index, struct sdc *c, struct siplan *plan,
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
	sv_indexiter_open(&i, vindex, PHIA_GE, NULL, 0);
	return si_compact(index, c, plan, vlsn, vlsn_lru, &i, size_stream);
}

static int si_droprepository(struct runtime *r, char *repo, int drop_directory)
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
		rc = ss_vfsunlink(r->vfs, path);
		if (unlikely(rc == -1)) {
			sr_malfunction("index file '%s' unlink error: %s",
			               path, strerror(errno));
			closedir(dir);
			return -1;
		}
	}
	closedir(dir);

	snprintf(path, sizeof(path), "%s/drop", repo);
	rc = ss_vfsunlink(r->vfs, path);
	if (unlikely(rc == -1)) {
		sr_malfunction("index file '%s' unlink error: %s",
		               path, strerror(errno));
		return -1;
	}
	if (drop_directory) {
		rc = ss_vfsrmdir(r->vfs, repo);
		if (unlikely(rc == -1)) {
			sr_malfunction("directory '%s' unlink error: %s",
			               repo, strerror(errno));
			return -1;
		}
	}
	return 0;
}

static int si_dropmark(struct si *i)
{
	/* create drop file */
	char path[1024];
	snprintf(path, sizeof(path), "%s/drop", i->conf.path);
	struct ssfile drop;
	ss_fileinit(&drop, i->r->vfs);
	int rc = ss_filenew(&drop, path);
	if (unlikely(rc == -1)) {
		sr_malfunction("drop file '%s' create error: %s",
		               path, strerror(errno));
		return -1;
	}
	ss_fileclose(&drop);
	return 0;
}

static int si_drop(struct si *i)
{
	struct runtime *r = i->r;
	struct sspath path;
	ss_pathinit(&path);
	ss_pathset(&path, "%s", i->conf.path);
	/* drop file must exists at this point */
	/* shutdown */
	int rc = si_close(i);
	if (unlikely(rc == -1))
		return -1;
	/* remove directory */
	rc = si_droprepository(r, path.path, 1);
	return rc;
}

static int
si_redistribute(struct si *index, struct ssa *a, struct sdc *c,
		struct sinode *node, struct ssbuf *result)
{
	(void)index;
	struct svindex *vindex = si_nodeindex(node);
	struct ssiter i;
	sv_indexiter_open(&i, vindex, PHIA_GE, NULL, 0);
	while (sv_indexiter_has(&i))
	{
		struct sv *v = sv_indexiter_get(&i);
		int rc = ss_bufadd(&c->b, a, &v->v, sizeof(struct svref**));
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
			int rc = sf_compare(&index->scheme, v->v->data,
					    v->v->size,
			                    sd_indexpage_min(&p->self.index, page),
			                    page->sizemin);
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
si_redistribute_set(struct si *index, uint64_t now, struct svref *v)
{
	index->update_time = now;
	/* match node */
	struct siiter ii;
	si_iter_open(&ii, index, PHIA_GE, v->v->data, v->v->size);
	struct sinode *node = si_iter_get(&ii);
	assert(node != NULL);
	/* update node */
	struct svindex *vindex = si_nodeindex(node);
	sv_indexset(vindex, *v);
	node->update_time = index->update_time;
	node->used += phia_tuple_size(v->v);
	/* schedule node */
	si_plannerupdate(&index->p, SI_BRANCH, node);
}

static int
si_redistribute_index(struct si *index, struct ssa *a, struct sdc *c, struct sinode *node)
{
	struct svindex *vindex = si_nodeindex(node);
	struct ssiter i;
	sv_indexiter_open(&i, vindex, PHIA_GE, NULL, 0);
	while (sv_indexiter_has(&i)) {
		struct sv *v = sv_indexiter_get(&i);
		int rc = ss_bufadd(&c->b, a, &v->v, sizeof(struct svref**));
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
si_split(struct si *index, struct sdc *c, struct ssbuf *result,
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
		.compression_key = index->conf.compression_key,
		.compression     = index->conf.compression,
		.compression_if  = index->conf.compression_if,
		.amqf            = index->conf.amqf,
		.vlsn            = vlsn,
		.vlsn_lru        = vlsn_lru,
		.save_delete     = 0,
		.save_upsert     = 0
	};
	struct sinode *n = NULL;
	struct sdmerge merge;
	rc = sd_mergeinit(&merge, i, &c->build, &c->qf, &c->upsert,
			  &mergeconf);
	if (unlikely(rc == -1))
		return -1;
	while ((rc = sd_merge(&merge)) > 0)
	{
		/* create new node */
		n = si_nodenew(&index->scheme, r);
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
		rc = ss_bufadd(result, index->r->a, &n, sizeof(struct sinode*));
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
si_merge(struct si *index, struct sdc *c, struct sinode *node,
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
		rc = ss_bufadd(result, r->a, &n, sizeof(struct sinode*));
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
		si_redistribute_index(index, r->a, c, node);
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
		rc = si_redistribute(index, r->a, c, node, result);
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
	sv_indexinit(j, &index->scheme);
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
si_nodenew(struct sfscheme *scheme, struct runtime *r)
{
	struct sinode *n = (struct sinode*)ss_malloc(r->a, sizeof(struct sinode));
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
	ss_fileinit(&n->file, r->vfs);
	sv_indexinit(&n->i0, scheme);
	sv_indexinit(&n->i1, scheme);
	ss_rbinitnode(&n->node);
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
	sv_indexinit(i, i->scheme);
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
			b = si_branchnew(r);
			if (unlikely(b == NULL))
				goto e0;
		}
		struct sdindex index;
		sd_indexinit(&index);
		rc = sd_indexcopy(&index, r->a, h);
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
		si_branchfree(b, r->a);
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
si_nodefree_branches(struct sinode *n, struct ssa *a)
{
	struct sibranch *p = n->branch;
	struct sibranch *next = NULL;
	while (p && p != &n->self) {
		next = p->next;
		si_branchfree(p, a);
		p = next;
	}
	sd_indexfree(&n->self.index, a);
}

static int si_nodefree(struct sinode *n, struct runtime *r, int gc)
{
	int rcret = 0;
	int rc;
	if (gc && ss_pathis_set(&n->file.path)) {
		ss_fileadvise(&n->file, 0, 0, n->file.size);
		rc = ss_vfsunlink(r->vfs, ss_pathof(&n->file.path));
		if (unlikely(rc == -1)) {
			sr_malfunction("index file '%s' unlink error: %s",
			               ss_pathof(&n->file.path),
			               strerror(errno));
			rcret = -1;
		}
	}
	si_nodefree_branches(n, r->a);
	rc = si_nodeclose(n, r, gc);
	if (unlikely(rc == -1))
		rcret = -1;
	ss_free(r->a, n);
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

static int si_plannerinit(struct siplanner *p, struct ssa *a, void *i)
{
	int rc = ss_rqinit(&p->compact, a, 1, 20);
	if (unlikely(rc == -1))
		return -1;
	/* 1Mb step up to 4Gb */
	rc = ss_rqinit(&p->branch, a, 1024 * 1024, 4000);
	if (unlikely(rc == -1)) {
		ss_rqfree(&p->compact, a);
		return -1;
	}
	rc = ss_rqinit(&p->temp, a, 1, 100);
	if (unlikely(rc == -1)) {
		ss_rqfree(&p->compact, a);
		ss_rqfree(&p->branch, a);
		return -1;
	}
	p->i = i;
	return 0;
}

static int si_plannerfree(struct siplanner *p, struct ssa *a)
{
	ss_rqfree(&p->compact, a);
	ss_rqfree(&p->branch, a);
	ss_rqfree(&p->temp, a);
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
	struct si *index = p->i;
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
	struct si *index = p->i;
	int status = sr_status(&index->status);
	switch (status) {
	case SR_DROP:
		if (si_refs(index) > 0)
			return 2;
		plan->plan = SI_DROP;
		return 1;
	case SR_SHUTDOWN:
		if (si_refs(index) > 0)
			return 2;
		plan->plan = SI_SHUTDOWN;
		return 1;
	}
	return 0;
}

static inline int
si_plannerpeek_nodegc(struct siplanner *p, struct siplan *plan)
{
	struct si *index = p->i;
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

static int si_profilerbegin(struct siprofiler *p, struct si *i)
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
	struct ssrbnode *pn;
	struct sinode *n;
	pn = ss_rbmin(&p->i->i);
	while (pn) {
		n = container_of(pn, struct sinode, node);
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
			if (b->index.h->extensions & SD_INDEXEXT_AMQF) {
				p->total_amqf_size +=
					sizeof(struct sdindexamqf) + sd_indexamqf(&b->index)->size;
			}
			b = b->next;
		}
		pn = ss_rbnext(&p->i->i, pn);
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
si_readopen(struct siread *q, struct si *index, struct sicache *c,
	    enum phia_order o, uint64_t vlsn, void *prefix, uint32_t prefixsize,
	    void *key, uint32_t keysize)
{
	q->order       = o;
	q->key         = key;
	q->keysize     = keysize;
	q->vlsn        = vlsn;
	q->index       = index;
	q->cache       = c;
	q->prefix      = prefix;
	q->prefixsize  = prefixsize;
	q->has         = 0;
	q->upsert_v    = NULL;
	q->upsert_eq   = 0;
	q->cache_only  = 0;
	q->oldest_only = 0;
	q->read_disk   = 0;
	q->read_cache  = 0;
	q->result      = NULL;
	sv_mergeinit(&q->merge, index->r->a, &index->scheme);
	si_lock(index);
	return 0;
}

static int si_readclose(struct siread *q)
{
	si_unlock(q->index);
	sv_mergefree(&q->merge);
	return 0;
}

static inline int
si_readdup(struct siread *q, struct sv *result)
{
	struct phia_tuple *v;
	if (likely(result->i == &sv_vif)) {
		v = result->v;
		phia_tuple_ref(v);
	} else {
		v = phia_tuple_from_sv(q->index->r, result);
		if (unlikely(v == NULL))
			return sr_oom();
	}
	q->result = v;
	return 1;
}

static inline void
si_readstat(struct siread *q, int cache, struct sinode *n, uint32_t reads)
{
	struct si *i = q->index;
	if (cache) {
		i->read_cache += reads;
		q->read_cache += reads;
	} else {
		i->read_disk += reads;
		q->read_disk += reads;
	}
	/* update temperature */
	if (i->conf.temperature) {
		n->temperature_reads += reads;
		uint64_t total = i->read_disk + i->read_cache;
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
		rc = sf_compare(q->merge.scheme, sv_pointer(v), sv_size(v),
		                q->key, q->keysize);
		if (unlikely(rc != 0))
			return 0;
	}
	if (q->prefix) {
		rc = sf_compareprefix(q->merge.scheme,
		                      q->prefix,
		                      q->prefixsize,
		                      sv_pointer(v), sv_size(v));
		if (unlikely(! rc))
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
	sv_init(&vret, &sv_vif, ref->v, NULL);
	return si_getresult(q, &vret, 0);
}

static inline int
si_getbranch(struct siread *q, struct sinode *n, struct sicachebranch *c)
{
	struct sibranch *b = c->branch;
	/* amqf */
	struct siconf *conf= &q->index->conf;
	int rc;
	if (conf->amqf) {
		rc = si_amqfhas_branch(q->merge.scheme, b, q->key);
		if (likely(! rc))
			return 0;
	}
	/* choose compression type */
	int compression;
	struct ssfilterif *compression_if;
	if (! si_branchis_root(b)) {
		compression    = conf->compression_branch;
		compression_if = conf->compression_branch_if;
	} else {
		compression    = conf->compression;
		compression_if = conf->compression_if;
	}
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
		.o               = PHIA_GE,
		.file            = &n->file,
		.a               = q->merge.a,
		.scheme          = q->merge.scheme
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
	sv_mergeiter_open(&im, &q->merge, PHIA_GE);
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
	si_iter_open(&ii, q->index, PHIA_GE, q->key, q->keysize);
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
	if (! si_branchis_root(b)) {
		compression    = conf->compression_branch;
		compression_if = conf->compression_branch_if;
	} else {
		compression    = conf->compression;
		compression_if = conf->compression_if;
	}
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
		.scheme          = q->merge.scheme
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
	struct sv upbuf_reserve;
	struct ssbuf upbuf;
	if (unlikely(q->upsert_v && q->upsert_v->v)) {
		ss_bufinit_reserve(&upbuf, &upbuf_reserve, sizeof(upbuf_reserve));
		ss_bufadd(&upbuf, NULL, (void*)&q->upsert_v, sizeof(struct sv*));
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
	sv_readiter_open(&ri, &im, &q->index->u, q->vlsn, 0);
	struct sv *v = sv_readiter_get(&ri);
	if (unlikely(v == NULL)) {
		sv_mergereset(&q->merge);
		si_iter_next(&ii);
		goto next_node;
	}

	rc = 1;
	/* convert upsert search to PHIA_EQ */
	if (q->upsert_eq) {
		rc = sf_compare(q->merge.scheme, sv_pointer(v), sv_size(v),
		                q->key, q->keysize);
		rc = rc == 0;
	}
	/* do prefix search */
	if (q->prefix && rc) {
		rc = sf_compareprefix(q->merge.scheme, q->prefix, q->prefixsize,
		                      sv_pointer(v),
		                      sv_size(v));
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
	case PHIA_EQ:
		return si_get(q);
	case PHIA_LT:
	case PHIA_LE:
	case PHIA_GT:
	case PHIA_GE:
		return si_range(q);
	default:
		break;
	}
	return -1;
}

static int
si_readcommited(struct si *index, struct sv *v, int recover)
{
	/* search node index */
	struct siiter ii;
	si_iter_open(&ii, index, PHIA_GE, sv_pointer(v), sv_size(v));
	struct sinode *node;
	node = si_iter_get(&ii);
	assert(node != NULL);

	uint64_t lsn = sv_lsn(v);
	/* search in-memory */
	if (recover == 2) {
		struct svindex *second;
		struct svindex *first = si_nodeindex_priority(node, &second);
		struct svref *ref = sv_indexfind(first, sv_pointer(v),
						 sv_size(v), UINT64_MAX);
		if ((ref == NULL || ref->v->lsn < lsn) && second != NULL)
			ref = sv_indexfind(second, sv_pointer(v),
					   sv_size(v), UINT64_MAX);
		if (ref != NULL && ref->v->lsn >= lsn)
			return 1;
	}

	/* search branches */
	struct sibranch *b;
	for (b = node->branch; b; b = b->next)
	{
		struct sdindexiter ii;
		sd_indexiter_open(&ii, &index->scheme, &b->index, PHIA_GE,
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
	see: scheme recover
	see: test/crash/durability.test.c
*/

static struct sinode *
si_bootstrap(struct si *i, uint64_t parent)
{
	struct runtime *r = i->r;
	/* create node */
	struct sinode *n = si_nodenew(&i->scheme, r);
	if (unlikely(n == NULL))
		return NULL;
	struct sdid id = {
		.parent = parent,
		.flags  = 0,
		.id     = sr_seq(r->seq, SR_NSNNEXT)
	};
	int rc;
	rc = si_nodecreate(n, &i->conf, &id);
	if (unlikely(rc == -1))
		goto e0;
	n->branch = &n->self;
	n->branch_count++;

	/* create index with one empty page */
	struct sdindex index;
	sd_indexinit(&index);
	rc = sd_indexbegin(&index, r->a);
	if (unlikely(rc == -1))
		goto e0;

	struct ssqf f, *qf = NULL;
	ss_qfinit(&f);

	struct sdbuild build;
	sd_buildinit(&build);
	rc = sd_buildbegin(&build, r->a, &i->scheme,
	                   i->conf.node_page_checksum,
	                   i->conf.compression_key,
	                   i->conf.compression,
	                   i->conf.compression_if);
	if (unlikely(rc == -1))
		goto e1;
	sd_buildend(&build);
	rc = sd_indexadd(&index, &build, sizeof(struct sdseal));
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
	/* amqf */
	if (i->conf.amqf) {
		rc = ss_qfensure(&f, r->a, 0);
		if (unlikely(rc == -1))
			goto e1;
		qf = &f;
	}
	rc = sd_indexcommit(&index, r->a, &id, qf, n->file.size);
	if (unlikely(rc == -1))
		goto e1;
	ss_qffree(&f, r->a);
	/* write index */
	rc = sd_writeindex(&n->file, &index);
	if (unlikely(rc == -1))
		goto e1;
	/* close seal */
	rc = sd_seal(&n->file, &index, seal);
	if (unlikely(rc == -1))
		goto e1;
	si_branchset(&n->self, &index);

	sd_buildcommit(&build);
	sd_buildfree(&build);
	return n;
e1:
	ss_qffree(&f, r->a);
	sd_indexfree(&index, r->a);
	sd_buildfree(&build);
e0:
	si_nodefree(n, r, 0);
	return NULL;
}

static inline int
si_deploy(struct si *i, struct runtime *r, int create_directory)
{
	/* create directory */
	int rc;
	if (likely(create_directory)) {
		rc = ss_vfsmkdir(r->vfs, i->conf.path, 0755);
		if (unlikely(rc == -1)) {
			sr_malfunction("directory '%s' create error: %s",
			               i->conf.path, strerror(errno));
			return -1;
		}
	}
	/* create initial node */
	struct sinode *n = si_bootstrap(i, 0);
	if (unlikely(n == NULL))
		return -1;
	SS_INJECTION(r->i, SS_INJECTION_SI_RECOVER_0,
	             si_nodefree(n, r, 0);
	             sr_malfunction("%s", "error injection");
	             return -1);
	rc = si_nodecomplete(n, &i->conf);
	if (unlikely(rc == -1)) {
		si_nodefree(n, r, 1);
		return -1;
	}
	si_insert(i, n);
	si_plannerupdate(&i->p, SI_COMPACT|SI_BRANCH|SI_TEMP, n);
	i->size = si_nodesize(n);
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
si_trackdir(struct sitrack *track, struct runtime *r, struct si *i)
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
				head = si_nodenew(&i->scheme, r);
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
				rc = ss_vfsunlink(r->vfs, path.path);
				if (unlikely(rc == -1)) {
					sr_malfunction("index file '%s' unlink error: %s",
					               path.path, strerror(errno));
					goto error;
				}
				continue;
			}
			assert(rc == SI_RDB_DBSEAL);
			/* recover 'sealed' node */
			node = si_nodenew(&i->scheme, r);
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
			rc = ss_vfsunlink(r->vfs, ss_pathof(&path));
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
		node = si_nodenew(&i->scheme, r);
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
si_trackvalidate(struct sitrack *track, struct ssbuf *buf, struct si *i)
{
	ss_bufreset(buf);
	struct ssrbnode *p = ss_rbmax(&track->i);
	while (p) {
		struct sinode *n = container_of(p, struct sinode, node);
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
		p = ss_rbprev(&track->i, p);
	}
	return 0;
}

static inline int
si_recovercomplete(struct sitrack *track, struct runtime *r, struct si *index, struct ssbuf *buf)
{
	/* prepare and build primary index */
	ss_bufreset(buf);
	struct ssrbnode *p = ss_rbmin(&track->i);
	while (p) {
		struct sinode *n = container_of(p, struct sinode, node);
		int rc = ss_bufadd(buf, r->a, &n, sizeof(struct sinode*));
		if (unlikely(rc == -1))
			return sr_oom();
		p = ss_rbnext(&track->i, p);
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
si_recoversize(struct si *i)
{
	struct ssrbnode *pn = ss_rbmin(&i->i);
	while (pn) {
		struct sinode *n = container_of(pn, struct sinode, node);
		i->size += si_nodesize(n);
		pn = ss_rbnext(&i->i, pn);
	}
}

static inline int
si_recoverindex(struct si *i, struct runtime *r)
{
	struct sitrack track;
	si_trackinit(&track);
	struct ssbuf buf;
	ss_bufinit(&buf);
	int rc;
	rc = si_trackdir(&track, r, i);
	if (unlikely(rc == -1))
		goto error;
	if (unlikely(track.count == 0))
		return 1;
	rc = si_trackvalidate(&track, &buf, i);
	if (unlikely(rc == -1))
		goto error;
	rc = si_recovercomplete(&track, r, i, &buf);
	if (unlikely(rc == -1))
		goto error;
	/* set actual metrics */
	if (track.nsn > r->seq->nsn)
		r->seq->nsn = track.nsn;
	if (track.lsn > r->seq->lsn)
		r->seq->lsn = track.lsn;
	si_recoversize(i);
	ss_buffree(&buf, r->a);
	return 0;
error:
	ss_buffree(&buf, r->a);
	si_trackfree(&track, r);
	return -1;
}

static inline int
si_recoverdrop(struct si *i, struct runtime *r)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/drop", i->conf.path);
	int rc = ss_vfsexists(r->vfs, path);
	if (likely(! rc))
		return 0;
	if (i->conf.path_fail_on_drop) {
		sr_malfunction("attempt to recover a dropped index: %s:",
		               i->conf.path);
		return -1;
	}
	rc = si_droprepository(r, i->conf.path, 0);
	if (unlikely(rc == -1))
		return -1;
	return 1;
}

static int si_recover(struct si *i)
{
	struct runtime *r = i->r;
	int exist = ss_vfsexists(r->vfs, i->conf.path);
	if (exist == 0)
		goto deploy;
	if (i->conf.path_fail_on_exists) {
		sr_error("directory '%s' already exists", i->conf.path);
		return -1;
	}
	int rc = si_recoverdrop(i, r);
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

enum {
	SI_SCHEME_NONE,
	SI_SCHEME_VERSION,
	SI_SCHEME_VERSION_STORAGE,
	SI_SCHEME_NAME,
	SI_SCHEME_FORMAT_STORAGE,
	SI_SCHEME_SCHEME,
	SI_SCHEME_NODE_SIZE,
	SI_SCHEME_NODE_PAGE_SIZE,
	SI_SCHEME_NODE_PAGE_CHECKSUM,
	SI_SCHEME_SYNC,
	SI_SCHEME_COMPRESSION,
	SI_SCHEME_COMPRESSION_KEY,
	SI_SCHEME_COMPRESSION_BRANCH,
	SI_SCHEME_COMPRESSION_RESERVED0,
	SI_SCHEME_COMPRESSION_RESERVED1,
	SI_SCHEME_AMQF,
	SI_SCHEME_CACHE_MODE,
};

static void si_confinit(struct siconf *s)
{
	memset(s, 0, sizeof(*s));
	sr_version(&s->version);
	sr_version_storage(&s->version_storage);
}

static void si_conffree(struct siconf *s, struct ssa *a)
{
	if (s->name) {
		ss_free(a, s->name);
		s->name = NULL;
	}
	if (s->path) {
		ss_free(a, s->path);
		s->path = NULL;
	}
	if (s->compression_sz) {
		ss_free(a, s->compression_sz);
		s->compression_sz = NULL;
	}
	if (s->compression_branch_sz) {
		ss_free(a, s->compression_branch_sz);
		s->compression_branch_sz = NULL;
	}
}

static void si_begin(struct sitx *x, struct si *index)
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

static inline int si_set(struct sitx *x, struct phia_tuple *v, uint64_t time)
{
	struct si *index = x->index;
	index->update_time = time;
	/* match node */
	struct siiter ii;
	si_iter_open(&ii, index, PHIA_GE, v->data, v->size);
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
	node->used += phia_tuple_size(v);
	if (index->conf.lru)
		si_lru_add(index, &ref);
	si_txtrack(x, node);
	return 0;
}

static void
si_write(struct sitx *x, struct svlog *l, struct svlogindex *li, uint64_t time,
	 int recover)
{
	struct runtime *r = x->index->r;
	struct svlogv *cv = sv_logat(l, li->head);
	int c = li->count;
	while (c) {
		struct phia_tuple *v = cv->v.v;
		if (recover) {
			if (si_readcommited(x->index, &cv->v, recover)) {
				size_t gc = phia_tuple_size(v);
				if (phia_tuple_unref(r, v))
					ss_quota(r->quota, SS_QREMOVE, gc);
				goto next;
			}
		}
		if (v->flags & SVGET) {
			phia_tuple_unref(r, v);
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
 * Create phia_dir if it doesn't exist.
 */
static int
sr_checkdir(struct runtime *r, const char *path)
{
	int exists = ss_vfsexists(r->vfs, path);
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
	struct si *index;
	uint32_t active;
};

struct sctask {
	struct siplan plan;
	struct scdb  *db;
	struct si    *shutdown;
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
static int sc_add(struct scheduler*, struct si*);
static int sc_del(struct scheduler*, struct si*, int);

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

static int sc_step(struct phia_service*, uint64_t);

static int sc_ctl_call(struct phia_service *, uint64_t);
static int sc_ctl_checkpoint(struct scheduler*);
static int sc_ctl_shutdown(struct scheduler*, struct si*);

static int sc_write(struct scheduler*, struct svlog*, uint64_t, int);

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

static int sc_add(struct scheduler *s, struct si *index)
{
	struct scdb *db = ss_malloc(s->r->a, sizeof(struct scdb));
	if (unlikely(db == NULL))
		return -1;
	db->index  = index;
	db->active = 0;
	memset(db->workers, 0, sizeof(db->workers));

	tt_pthread_mutex_lock(&s->lock);
	int count = s->count + 1;
	struct scdb **i = ss_malloc(s->r->a, count * sizeof(struct scdb*));
	if (unlikely(i == NULL)) {
		tt_pthread_mutex_unlock(&s->lock);
		ss_free(s->r->a, db);
		return -1;
	}
	memcpy(i, s->i, s->count * sizeof(struct scdb*));
	i[s->count] = db;
	void *iprev = s->i;
	s->i = i;
	s->count = count;
	tt_pthread_mutex_unlock(&s->lock);
	if (iprev)
		ss_free(s->r->a, iprev);
	return 0;
}

static int sc_del(struct scheduler *s, struct si *index, int lock)
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
	struct scdb **i = ss_malloc(s->r->a, count * sizeof(struct scdb*));
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
	ss_free(s->r->a, iprev);
	ss_free(s->r->a, db);
	return 0;
}

static struct scheduler *
phia_env_get_scheduler(struct phia_env *);

static int sc_ctl_call(struct phia_service *srv, uint64_t vlsn)
{
	struct scheduler *sc = phia_env_get_scheduler(srv->env);
	int rc = sr_statusactive(sc->r->status);
	if (unlikely(rc == 0))
		return 0;
	rc = sc_step(srv, vlsn);
	return rc;
}

static int sc_ctl_checkpoint(struct scheduler *s)
{
	tt_pthread_mutex_lock(&s->lock);
	sc_task_checkpoint(s);
	tt_pthread_mutex_unlock(&s->lock);
	return 0;
}

static int sc_ctl_shutdown(struct scheduler *s, struct si *i)
{
	tt_pthread_mutex_lock(&s->lock);
	s->shutdown_pending++;
	rlist_add(&s->shutdown, &i->link);
	tt_pthread_mutex_unlock(&s->lock);
	return 0;
}

static inline int
sc_execute(struct sctask *t, struct sdc *c, uint64_t vlsn)
{
	struct si *index;
	if (unlikely(t->shutdown))
		index = t->shutdown;
	else
		index = t->db->index;

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
		if (unlikely(! si_active(db->index))) {
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
	struct si *index, *n;
	rlist_foreach_entry_safe(index, &s->shutdown, link, n) {
		task->plan.plan = SI_SHUTDOWN;
		int rc;
		rc = si_plan(index, &task->plan);
		if (rc == 1) {
			s->shutdown_pending--;
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
		si_ref(db->index, SI_REFBE);
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
			si_ref(db->index, SI_REFBE);
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
			si_ref(db->index, SI_REFBE);
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
			si_ref(db->index, SI_REFBE);
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
			si_ref(db->index, SI_REFBE);
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
			si_ref(db->index, SI_REFBE);
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
		si_ref(db->index, SI_REFBE);
		task->db = db;
		return 1;
	}

	/* compaction */
	task->plan.plan = SI_COMPACT;
	task->plan.a = zone->compact_wm;
	task->plan.b = zone->compact_mode;
	rc = sc_plan(s, &task->plan);
	if (rc == 1) {
		si_ref(db->index, SI_REFBE);
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
		assert(0);
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
sc_schedule(struct sctask *task, struct phia_service *srv, uint64_t vlsn)
{
	uint64_t now = clock_monotonic64();
	struct scheduler *sc = phia_env_get_scheduler(srv->env);
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
	}
	if (db)
		si_unref(db->index, SI_REFBE);
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
sc_step(struct phia_service *srv, uint64_t vlsn)
{
	struct scheduler *sc = phia_env_get_scheduler(srv->env);
	struct sctask task;
	sc_taskinit(&task);
	int rc = sc_schedule(&task, srv, vlsn);
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

static int sc_write(struct scheduler *s, struct svlog *log, uint64_t lsn, int recover)
{
	/* write-ahead log */
	struct sltx tl;
	sl_begin(s->r, &tl, lsn);
	int rc = sl_write(&tl, log);
	if (unlikely(rc == -1)) {
		return -1;
	}

	/* index */
	uint64_t now = clock_monotonic64();
	struct svlogindex *i   = (struct svlogindex*)log->index.s;
	struct svlogindex *end = (struct svlogindex*)log->index.p;
	while (i < end) {
		struct si *index = i->index;
		struct sitx x;
		si_begin(&x, index);
		si_write(&x, log, i, now, recover);
		si_commit(&x);
		i++;
	}
	return 0;
}

struct seconfrt {
	/* phia */
	char      version[16];
	char      version_storage[16];
	char      build[32];
	/* memory */
	uint64_t  memory_used;
	uint32_t  pager_pools;
	uint32_t  pager_pool_size;
	uint32_t  pager_ref_pools;
	uint32_t  pager_ref_pool_size;
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
	/* phia */
	char         *path;
	uint32_t      path_create;
	int           recover;
	int           recover_complete;
	/* compaction */
	struct srzonemap     zones;
	/* memory */
	uint64_t      memory_limit;
	int           confmax;
	struct srconf       *conf;
	struct phia_env     *env;
};

static int se_confinit(struct seconf*, struct phia_env *o);
static void se_conffree(struct seconf*);
static int se_confvalidate(struct seconf*);
static int se_confserialize(struct seconf*, struct ssbuf*);

struct phia_confcursor {
	struct phia_env *env;
	struct ssbuf dump;
	int first;
	struct srconfdump *pos;
};

struct phia_env {
	struct srstatus    status;
	/** List of open spaces. */
	struct rlist db;
	struct srseq       seq;
	struct seconf      conf;
	struct ssquota     quota;
	struct ssvfs       vfs;
	struct ssa         a_oom;
	struct ssa         a;
	struct sicachepool cachepool;
	struct sxmanager   xm;
	struct scheduler          scheduler;
	struct srstat      stat;
	struct sflimit     limit;
	struct runtime          r;
};

struct phia_index {
	struct phia_env *env;
	uint32_t   created;
	struct siprofiler rtp;
	struct si        *index;
	struct sxindex    coindex;
	uint64_t   txn_min;
	uint64_t   txn_max;
	/** Member of env->db list. */
	struct rlist link;
};

struct phia_document {
	struct phia_index *db;
	struct phia_tuple *value;
	struct phia_field       fields[8];
	int       fields_count;
	int       fields_count_keys;
};

struct scheduler *
phia_env_get_scheduler(struct phia_env *env)
{
	return &env->scheduler;
}

void
phia_raise()
{
	diag_raise();
}

int
phia_index_read(struct phia_index*, struct phia_tuple*, enum phia_order order,
		struct phia_tuple **, struct sx*, int, struct sicache*,
		bool cache_only, struct phia_statget *);
static int phia_index_visible(struct phia_index*, uint64_t);
static void phia_index_bind(struct phia_index*);
static void phia_index_unbind(struct phia_index*, uint64_t);
static int phia_index_recoverbegin(struct phia_index*);
static int phia_index_recoverend(struct phia_index*);

struct phia_tx {
	struct phia_env *env;
	int64_t lsn;
	bool half_commit;
	uint64_t start;
	struct svlog log;
	struct sx t;
};

struct phia_cursor {
	struct phia_index *db;
	struct phia_tuple *key;
	enum phia_order order;
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
phia_bootstrap(struct phia_env *e)
{
	assert(sr_status(&e->status) == SR_OFFLINE);
	sr_statusset(&e->status, SR_ONLINE);
}

void
phia_begin_initial_recovery(struct phia_env *e)
{
	assert(sr_status(&e->status) == SR_OFFLINE);
	sr_statusset(&e->status, SR_INITIAL_RECOVERY);
}

void
phia_begin_final_recovery(struct phia_env *e)
{
	assert(sr_status(&e->status) == SR_INITIAL_RECOVERY);
	sr_statusset(&e->status, SR_FINAL_RECOVERY);
}

void
phia_end_recovery(struct phia_env *e)
{
	assert(sr_status(&e->status) == SR_FINAL_RECOVERY);
	sr_statusset(&e->status, SR_ONLINE);
}

int
phia_env_delete(struct phia_env *e)
{
	int rcret = 0;
	int rc;
	sr_statusset(&e->status, SR_SHUTDOWN);
	{
		struct phia_index *db, *next;
		rlist_foreach_entry_safe(db, &e->db, link, next) {
			rc = phia_index_delete(db);
			if (unlikely(rc == -1))
				rcret = -1;
		}
	}
	sx_managerfree(&e->xm);
	ss_vfsfree(&e->vfs);
	si_cachepool_free(&e->cachepool);
	se_conffree(&e->conf);
	ss_quotafree(&e->quota);
	sf_limitfree(&e->limit, &e->a);
	sr_statfree(&e->stat);
	sr_seqfree(&e->seq);
	sr_statusfree(&e->status);
	free(e);
	return rcret;
}

static inline int
se_confv(struct srconf *c, struct srconfstmt *s)
{
	return sr_conf_serialize(c, s);
}

static inline struct srconf*
se_confphia(struct phia_env *e, struct seconfrt *rt, struct srconf **pc)
{
	struct srconf *phia = *pc;
	struct srconf *p = NULL;
	sr_C(&p, pc, se_confv, "version", SS_STRING, rt->version, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "version_storage", SS_STRING, rt->version_storage, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "build", SS_STRING, rt->build, SR_RO, NULL);
	sr_c(&p, pc, se_confv, "path", SS_STRINGPTR, &e->conf.path);
	sr_c(&p, pc, se_confv, "path_create", SS_U32, &e->conf.path_create);
	return sr_C(NULL, pc, NULL, "phia", SS_UNDEF, phia, SR_NS, NULL);
}

static inline struct srconf*
se_confmemory(struct phia_env *e, struct seconfrt *rt, struct srconf **pc)
{
	struct srconf *memory = *pc;
	struct srconf *p = NULL;
	sr_c(&p, pc, se_confv, "limit", SS_U64, &e->conf.memory_limit);
	sr_C(&p, pc, se_confv, "used", SS_U64, &rt->memory_used, SR_RO, NULL);
	return sr_C(NULL, pc, NULL, "memory", SS_UNDEF, memory, SR_NS, NULL);
}

static inline struct srconf*
se_confcompaction(struct phia_env *e, struct seconfrt *rt ssunused, struct srconf **pc)
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
		sr_c(&p, pc, se_confv, "mode", SS_U32, &z->mode);
		sr_c(&p, pc, se_confv, "compact_wm", SS_U32, &z->compact_wm);
		sr_c(&p, pc, se_confv, "compact_mode", SS_U32, &z->compact_mode);
		sr_c(&p, pc, se_confv, "branch_prio", SS_U32, &z->branch_prio);
		sr_c(&p, pc, se_confv, "branch_wm", SS_U32, &z->branch_wm);
		sr_c(&p, pc, se_confv, "branch_age", SS_U32, &z->branch_age);
		sr_c(&p, pc, se_confv, "branch_age_period", SS_U32, &z->branch_age_period);
		sr_c(&p, pc, se_confv, "branch_age_wm", SS_U32, &z->branch_age_wm);
		sr_c(&p, pc, se_confv, "gc_wm", SS_U32, &z->gc_wm);
		sr_c(&p, pc, se_confv, "gc_prio", SS_U32, &z->gc_prio);
		sr_c(&p, pc, se_confv, "gc_period", SS_U32, &z->gc_period);
		sr_c(&p, pc, se_confv, "lru_prio", SS_U32, &z->lru_prio);
		sr_c(&p, pc, se_confv, "lru_period", SS_U32, &z->lru_period);
		prev = sr_C(&prev, pc, NULL, z->name, SS_UNDEF, zone, SR_NS, NULL);
		if (compaction == NULL)
			compaction = prev;
	}
	return sr_C(NULL, pc, NULL, "compaction", SS_UNDEF, compaction, SR_NS, NULL);
}

int
phia_checkpoint(struct phia_env *env)
{
	return sc_ctl_checkpoint(&env->scheduler);
}

bool
phia_checkpoint_is_active(struct phia_env *env)
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
	sr_C(&p, pc, se_confv, "zone", SS_STRING, rt->zone, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "gc_active", SS_U32, &rt->gc_active, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "lru_active", SS_U32, &rt->lru_active, SR_RO, NULL);
	return sr_C(NULL, pc, NULL, "scheduler", SS_UNDEF, scheduler, SR_NS, NULL);
}

static inline struct srconf*
se_confperformance(struct phia_env *e ssunused, struct seconfrt *rt, struct srconf **pc)
{
	struct srconf *perf = *pc;
	struct srconf *p = NULL;
	sr_C(&p, pc, se_confv, "documents", SS_U64, &rt->stat.v_count, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "documents_used", SS_U64, &rt->stat.v_allocated, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "key", SS_STRING, rt->stat.key.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "value", SS_STRING, rt->stat.value.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "set", SS_U64, &rt->stat.set, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "set_latency", SS_STRING, rt->stat.set_latency.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "delete", SS_U64, &rt->stat.del, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "delete_latency", SS_STRING, rt->stat.del_latency.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "upsert", SS_U64, &rt->stat.upsert, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "upsert_latency", SS_STRING, rt->stat.upsert_latency.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "get", SS_U64, &rt->stat.get, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "get_latency", SS_STRING, rt->stat.get_latency.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "get_read_disk", SS_STRING, rt->stat.get_read_disk.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "get_read_cache", SS_STRING, rt->stat.get_read_cache.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tx_active_rw", SS_U32, &rt->tx_rw, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tx_active_ro", SS_U32, &rt->tx_ro, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tx", SS_U64, &rt->stat.tx, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tx_rollback", SS_U64, &rt->stat.tx_rlb, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tx_conflict", SS_U64, &rt->stat.tx_conflict, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tx_lock", SS_U64, &rt->stat.tx_lock, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tx_latency", SS_STRING, rt->stat.tx_latency.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tx_ops", SS_STRING, rt->stat.tx_stmts.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tx_gc_queue", SS_U32, &rt->tx_gc_queue, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "cursor", SS_U64, &rt->stat.cursor, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "cursor_latency", SS_STRING, rt->stat.cursor_latency.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "cursor_read_disk", SS_STRING, rt->stat.cursor_read_disk.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "cursor_read_cache", SS_STRING, rt->stat.cursor_read_cache.sz, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "cursor_ops", SS_STRING, rt->stat.cursor_ops.sz, SR_RO, NULL);
	return sr_C(NULL, pc, NULL, "performance", SS_UNDEF, perf, SR_NS, NULL);
}

static inline struct srconf*
se_confmetric(struct phia_env *e ssunused, struct seconfrt *rt, struct srconf **pc)
{
	struct srconf *metric = *pc;
	struct srconf *p = NULL;
	sr_C(&p, pc, se_confv, "lsn",  SS_U64, &rt->seq.lsn, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "tsn",  SS_U64, &rt->seq.tsn, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "nsn",  SS_U64, &rt->seq.nsn, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "dsn",  SS_U32, &rt->seq.dsn, SR_RO, NULL);
	sr_C(&p, pc, se_confv, "lfsn", SS_U64, &rt->seq.lfsn, SR_RO, NULL);
	return sr_C(NULL, pc, NULL, "metric", SS_UNDEF, metric, SR_NS, NULL);
}

static inline struct srconf*
se_confdb(struct phia_env *e, struct seconfrt *rt ssunused, struct srconf **pc)
{
	struct srconf *db = NULL;
	struct srconf *prev = NULL;
	struct srconf *p;
	struct phia_index *o;
	rlist_foreach_entry(o, &e->db, link)
	{
		si_profilerbegin(&o->rtp, o->index);
		si_profiler(&o->rtp);
		si_profilerend(&o->rtp);
		/* database index */
		struct srconf *database = *pc;
		p = NULL;
		sr_C(&p, pc, se_confv, "memory_used", SS_U64, &o->rtp.memory_used, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "size", SS_U64, &o->rtp.total_node_size, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "size_uncompressed", SS_U64, &o->rtp.total_node_origin_size, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "size_amqf", SS_U64, &o->rtp.total_amqf_size, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "count", SS_U64, &o->rtp.count, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "count_dup", SS_U64, &o->rtp.count_dup, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "read_disk", SS_U64, &o->rtp.read_disk, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "read_cache", SS_U64, &o->rtp.read_cache, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "temperature_avg", SS_U32, &o->rtp.temperature_avg, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "temperature_min", SS_U32, &o->rtp.temperature_min, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "temperature_max", SS_U32, &o->rtp.temperature_max, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "temperature_histogram", SS_STRINGPTR, &o->rtp.histogram_temperature_ptr, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "node_count", SS_U32, &o->rtp.total_node_count, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "branch_count", SS_U32, &o->rtp.total_branch_count, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "branch_avg", SS_U32, &o->rtp.total_branch_avg, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "branch_max", SS_U32, &o->rtp.total_branch_max, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "branch_histogram", SS_STRINGPTR, &o->rtp.histogram_branch_ptr, SR_RO, NULL);
		sr_C(&p, pc, se_confv, "page_count", SS_U32, &o->rtp.total_page_count, SR_RO, NULL);
		sr_C(&prev, pc, NULL, o->index->conf.name, SS_UNDEF, database, SR_NS, o);
		if (db == NULL)
			db = prev;
	}
	return sr_C(NULL, pc, NULL, "db", SS_UNDEF, db, SR_NS, NULL);
}

static struct srconf*
se_confprepare(struct phia_env *e, struct seconfrt *rt, struct srconf *c)
{
	/* phia */
	struct srconf *pc = c;
	struct srconf *phia     = se_confphia(e, rt, &pc);
	struct srconf *memory     = se_confmemory(e, rt, &pc);
	struct srconf *compaction = se_confcompaction(e, rt, &pc);
	struct srconf *scheduler  = se_confscheduler(rt, &pc);
	struct srconf *perf       = se_confperformance(e, rt, &pc);
	struct srconf *metric     = se_confmetric(e, rt, &pc);
	struct srconf *db         = se_confdb(e, rt, &pc);

	phia->next     = memory;
	memory->next     = compaction;
	compaction->next = scheduler;
	scheduler->next  = perf;
	perf->next       = metric;
	metric->next     = db;
	return phia;
}

static int
se_confrt(struct phia_env *e, struct seconfrt *rt)
{
	/* phia */
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

static inline int
se_confensure(struct seconf *c)
{
	struct phia_env *e = (struct phia_env*)c->env;
	int confmax = 2048;
	confmax *= sizeof(struct srconf);
	if (likely(confmax <= c->confmax))
		return 0;
	struct srconf *cptr = ss_malloc(&e->a, confmax);
	if (unlikely(cptr == NULL))
		return sr_oom();
	ss_free(&e->a, c->conf);
	c->conf = cptr;
	c->confmax = confmax;
	return 0;
}

static int se_confserialize(struct seconf *c, struct ssbuf *buf)
{
	int rc;
	rc = se_confensure(c);
	if (unlikely(rc == -1))
		return -1;
	struct phia_env *e = (struct phia_env*)c->env;
	struct seconfrt rt;
	se_confrt(e, &rt);
	struct srconf *conf = c->conf;
	struct srconf *root;
	root = se_confprepare(e, &rt, conf);
	struct srconfstmt stmt = {
		.path      = NULL,
		.value     = NULL,
		.valuesize = 0,
		.valuetype = SS_UNDEF,
		.serialize = buf,
		.ptr       = e,
		.r         = &e->r
	};
	return sr_confexec(root, &stmt);
}

static int se_confinit(struct seconf *c, struct phia_env *o)
{
	c->confmax = 2048;
	c->conf = ss_malloc(&o->a, sizeof(struct srconf) * c->confmax);
	if (unlikely(c->conf == NULL))
		return -1;
	c->env                 = o;
	c->path                = NULL;
	c->path_create         = 1;
	c->recover             = 1;
	c->memory_limit        = 0;
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
	sr_zonemap_set(&o->conf.zones,  0, &def);
	sr_zonemap_set(&o->conf.zones, 80, &redzone);
	return 0;
}

static void se_conffree(struct seconf *c)
{
	struct phia_env *e = (struct phia_env*)c->env;
	if (c->conf) {
		ss_free(&e->a, c->conf);
		c->conf = NULL;
	}
	if (c->path) {
		ss_free(&e->a, c->path);
		c->path = NULL;
	}
}

static int se_confvalidate(struct seconf *c)
{
	struct phia_env *e = (struct phia_env*)c->env;
	if (c->path == NULL) {
		sr_error("%s", "repository path is not set");
		return -1;
	}
	int i = 0;
	for (; i < 11; i++) {
		struct srzone *z = &e->conf.zones.zones[i];
		if (! z->enable)
			continue;
		if (z->compact_wm <= 1) {
			sr_error("bad %d.compact_wm value", i * 10);
			return -1;
		}
		/* convert periodic times from sec to usec */
		z->branch_age_period_us = z->branch_age_period * 1000000;
		z->gc_period_us         = z->gc_period * 1000000;
		z->lru_period_us        = z->lru_period * 1000000;
	}
	return 0;
}

void
phia_confcursor_delete(struct phia_confcursor *c)
{
	struct phia_env *e = c->env;
	ss_buffree(&c->dump, &e->a);
	ss_free(&e->a, c);
}

int
phia_confcursor_next(struct phia_confcursor *c, const char **key,
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

struct phia_confcursor *
phia_confcursor_new(struct phia_env *e)
{
	struct phia_confcursor *c;
	c = ss_malloc(&e->a, sizeof(struct phia_confcursor));
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
		phia_confcursor_delete(c);
		sr_oom();
		return NULL;
	}
	return c;
}

void
phia_cursor_delete(struct phia_cursor *c)
{
	struct phia_env *e = c->db->env;
	uint64_t id = c->t.id;
	if (! c->read_commited)
		sx_rollback(&c->t);
	if (c->cache)
		si_cachepool_push(c->cache);
	if (c->key)
		phia_tuple_unref(c->db->index->r, c->key);
	phia_index_unbind(c->db, id);
	sr_statcursor(&e->stat, c->start,
	              c->read_disk,
	              c->read_cache,
	              c->ops);
	ss_free(&e->a, c);
}

int
phia_cursor_next(struct phia_cursor *c, struct phia_document **result,
		 bool cache_only)
{
	struct phia_index *db = c->db;
	struct sx *x = &c->t;
	if (c->read_commited)
		x = NULL;

	struct phia_tuple *value;
	struct phia_statget statget;
	assert(c->key != NULL);
	if (phia_index_read(db, c->key, c->order, &value, x, 0, c->cache,
			    cache_only, &statget) != 0) {
		return -1;
	}

	c->ops++;
	if (value == NULL) {
		if (!cache_only) {
			 phia_tuple_unref(db->index->r, c->key);
			 c->key = NULL;
		}
		 *result = NULL;
		 return 0;
	}

	struct phia_document *doc = phia_document_new(db);
	if (unlikely(doc == NULL)) {
		phia_tuple_unref(db->index->r, value);
		return -1;
	}
	doc->value = value;
	if (c->order == PHIA_GE)
		c->order = PHIA_GT;
	else if (c->order == PHIA_LE)
		c->order = PHIA_LT;

	c->read_disk += statget.read_disk;
	c->read_cache += statget.read_cache;

	phia_tuple_unref(db->index->r, c->key);
	c->key = value;
	phia_tuple_ref(c->key);

	*result = doc;
	return 0;
}

void
phia_cursor_set_read_commited(struct phia_cursor *c, bool read_commited)
{
	sx_rollback(&c->t);
	c->read_commited = read_commited;
}

struct phia_cursor *
phia_cursor_new(struct phia_index *db, struct phia_document *key,
		enum phia_order order)
{
	/* prepare the key */
	int rc = phia_document_build(key, order);
	if (unlikely(rc == -1))
		return NULL;

	struct phia_env *e = db->env;
	struct phia_cursor *c;
	c = ss_malloc(&e->a, sizeof(struct phia_cursor));
	if (unlikely(c == NULL)) {
		sr_oom();
		return NULL;
	}
	sv_loginit(&c->log);
	sx_init(&e->xm, &c->t, &c->log);
	c->db = db;
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
	uint64_t vlsn = UINT64_MAX;
	sx_begin(&e->xm, &c->t, SXRO, &c->log, vlsn);
	phia_index_bind(db);

	c->key = key->value;
	phia_tuple_ref(c->key);
	phia_document_delete(key);
	c->order = order;
	return c;
}

static int
si_confcreate(struct siconf *conf, struct ssa *a,
	      struct key_def *key_def)
{
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 ":%" PRIu32,
	         key_def->space_id, key_def->iid);
	conf->name = ss_strdup(a, name);
	if (conf->name == NULL) {
		sr_oom();
		goto error;
	}
	conf->id                    = key_def->space_id;
	conf->sync                  = cfg_geti("phia.sync");
	conf->node_size             = key_def->opts.node_size;
	conf->node_page_size        = key_def->opts.page_size;
	conf->node_page_checksum    = 1;
	conf->compression_key       = key_def->opts.compression_key;

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
	conf->compression_sz = ss_strdup(a, conf->compression_if->name);
	if (conf->compression_sz == NULL) {
		sr_oom();
		goto error;
	}

	/* compression_branch */
	if (key_def->opts.compression_branch[0] != '\0') {
		conf->compression_branch_if =
			ss_filterof(key_def->opts.compression_branch);
		if (unlikely(conf->compression_branch_if == NULL)) {
			sr_error("unknown compression type '%s'",
				 key_def->opts.compression_branch);
			goto error;
		}
		if (conf->compression_branch_if != &ss_nonefilter)
			conf->compression_branch = 1;
	} else {
		conf->compression_branch = 0;
		conf->compression_branch_if = &ss_nonefilter;
	}
	conf->compression_branch_sz = ss_strdup(a,
		conf->compression_branch_if->name);
	if (conf->compression_branch_sz == NULL) {
		sr_oom();
		goto error;
	}

	conf->temperature           = 0;
	conf->amqf                  = key_def->opts.amqf;
	/* path */
	if (key_def->opts.path[0] == '\0') {
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", cfg_gets("phia_dir"),
			 conf->name);
		conf->path = ss_strdup(a, path);
	} else {
		conf->path = ss_strdup(a, key_def->opts.path);
	}
	if (conf->path == NULL) {
		sr_oom();
		goto error;
	}
	conf->path_fail_on_exists   = 0;
	conf->path_fail_on_drop     = 0;
	conf->lru                   = 0;
	conf->lru_step              = 128 * 1024;
	conf->buf_gc_wm             = 1024 * 1024;

	return 0;
error:
	return -1;
}

static int
sf_schemecreate(struct sfscheme *scheme, struct ssa *a,
		struct key_def *key_def)
{
	scheme->key_def = key_def;
	scheme->fmt_storage = key_def->opts.compression_key ?
		SF_SPARSE : SF_RAW;

	for (uint32_t i = 0; i < key_def->part_count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "key_%" PRIu32, i);
		struct sffield *field = sf_fieldnew(a, name);
		if (field == NULL) {
			sr_oom();
			goto error;
		}

		/* set field type */
		char type[32];
		snprintf(type, sizeof(type), "%s,key(%" PRIu32 ")",
		         (key_def->parts[i].type == NUM ? "u64" : "string"),
		         i);
		int rc = sf_fieldoptions(field, a, type);
		if (rc == -1) {
			sf_fieldfree(field, a);
			sr_oom();
			goto error;
		}
		rc = sf_schemeadd(scheme, a, field);
		if (rc == -1) {
			sf_fieldfree(field, a);
			sr_oom();
			goto error;
		}
	}

	struct sffield *field = sf_fieldnew(a, "value");
	if (field == NULL) {
		sr_oom();
		goto error;
	}
	int rc = sf_fieldoptions(field, a, "string");
	if (rc == -1) {
		sf_fieldfree(field, a);
		goto error;
	}
	rc = sf_schemeadd(scheme, a, field);
	if (rc == -1) {
		sf_fieldfree(field, a);
		goto error;
	}

	/* validate scheme and set keys */
	rc = sf_schemevalidate(scheme, a);
	if (rc == -1) {
		sr_error("incomplete scheme %s", "");
		goto error;
	}
	return 0;
error:
	return -1;
}

int
phia_index_open(struct phia_index *db)
{
	struct phia_env *e = db->env;
	int status = sr_status(&db->index->status);
	if (status == SR_FINAL_RECOVERY ||
	    status == SR_DROP_PENDING)
		goto online;
	if (status != SR_OFFLINE)
		return -1;
	sx_indexset(&db->coindex, db->index->conf.id);
	int rc = phia_index_recoverbegin(db);
	if (unlikely(rc == -1))
		return -1;

online:
	phia_index_recoverend(db);
	rc = sc_add(&e->scheduler, db->index);
	if (unlikely(rc == -1))
		return -1;
	return 0;
}

static inline int
phia_index_free(struct phia_index *db, int close)
{
	struct phia_env *e = db->env;
	int rcret = 0;
	int rc;
	sx_indexfree(&db->coindex, &e->xm);
	if (close) {
		rc = si_close(db->index);
		if (unlikely(rc == -1))
			rcret = -1;
	}
	ss_free(&e->a, db);
	return rcret;
}

static inline void
phia_index_unref(struct phia_index *db)
{
	struct phia_env *e = db->env;
	/* do nothing during env shutdown */
	int status = sr_status(&e->status);
	if (status == SR_SHUTDOWN)
		return;
	/* reduce reference counter */
	int ref;
	ref = si_unref(db->index, SI_REFFE);
	if (ref > 1)
		return;
	/* drop/shutdown pending:
	 *
	 * switch state and transfer job to
	 * the scheduler.
	*/
	status = sr_status(&db->index->status);
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
	/* destroy index object */
	struct si *index = db->index;
	rlist_del(&db->link);
	phia_index_free(db, 0);

	/* schedule index shutdown or drop */
	sr_statusset(&index->status, status);
	sc_ctl_shutdown(&e->scheduler, index);
}

int
phia_index_delete(struct phia_index *db)
{
	struct phia_env *e = db->env;
	int status = sr_status(&e->status);
	if (status == SR_SHUTDOWN ||
	    status == SR_OFFLINE) {
		return phia_index_free(db, 1);
	}
	phia_index_unref(db);
	return 0;
}

int
phia_index_close(struct phia_index *db)
{
	struct phia_env *e = db->env;
	int status = sr_status(&db->index->status);
	if (unlikely(! sr_statusactive_is(status)))
		return -1;
	/* set last visible transaction id */
	db->txn_max = sx_max(&e->xm);
	sr_statusset(&db->index->status, SR_SHUTDOWN_PENDING);
	return 0;
}

int
phia_index_drop(struct phia_index *db)
{
	struct phia_env *e = db->env;
	int status = sr_status(&db->index->status);
	if (unlikely(! sr_statusactive_is(status)))
		return -1;
	int rc = si_dropmark(db->index);
	if (unlikely(rc == -1))
		return -1;
	/* set last visible transaction id */
	db->txn_max = sx_max(&e->xm);
	sr_statusset(&db->index->status, SR_DROP_PENDING);
	return 0;
}

int
phia_index_read(struct phia_index *db, struct phia_tuple *key, enum phia_order order,
		struct phia_tuple **result, struct sx *x, int x_search,
		struct sicache *cache, bool cache_only,
		struct phia_statget *statget)
{
	struct phia_env *e = db->env;
	uint64_t start  = clock_monotonic64();

	if (unlikely(! sr_online(&db->index->status))) {
		sr_error("%s", "index is not online");
		return -1;
	}

	key->flags = SVGET;
	struct phia_tuple *vup = NULL;

	/* concurrent */
	if (x_search && order == PHIA_EQ) {
		/* note: prefix is ignored during concurrent
		 * index search */
		int rc = sx_get(x, &db->coindex, key, &vup);
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
		sx_get_autocommit(&e->xm, &db->coindex);
	}

	/* prepare read cache */
	int cachegc = 0;
	if (cache == NULL) {
		cachegc = 1;
		cache = si_cachepool_pop(&e->cachepool);
		if (unlikely(cache == NULL)) {
			if (vup != NULL)
				phia_tuple_unref(db->index->r, vup);
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
	if (order == PHIA_EQ) {
		order = PHIA_GE;
		upsert_eq = 1;
	}

	/* read index */
	struct siread q;
	si_readopen(&q, db->index, cache,
	            order,
	            vlsn,
	            NULL, 0,
		    key->data, key->size);
	struct sv sv_vup;
	if (vup != NULL) {
		sv_init(&sv_vup, &sv_vif, vup, NULL);
		q.upsert_v = &sv_vup;
	}
	q.upsert_eq = upsert_eq;
	q.cache_only = cache_only;
	int rc = si_read(&q);
	si_readclose(&q);

	if (vup != NULL) {
		phia_tuple_unref(db->index->r, vup);
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

int
phia_index_get(struct phia_index *db, struct phia_document *key,
	       struct phia_document **result, bool cache_only)
{
	assert(key->db == db);

	/* prepare the key */
	int rc = phia_document_build(key, PHIA_EQ);
	if (unlikely(rc == -1))
		return -1;

	struct phia_tuple *value;
	struct phia_statget statget;
	if (phia_index_read(db, key->value, PHIA_EQ, &value, NULL, 0, NULL,
			    cache_only, &statget) != 0) {
		return -1;
	}

	if (value == NULL) {
		 *result = NULL;
		 return 0;
	}

	struct phia_document *doc = phia_document_new(db);
	if (unlikely(doc == NULL)) {
		phia_tuple_unref(db->index->r, value);
		return -1;
	}
	doc->value = value;
	sr_statget(&db->env->stat, &statget);

	*result = doc;
	return 0;
}

struct phia_index *
phia_index_new(struct phia_env *e, struct key_def *key_def)
{
	char name[128];
	snprintf(name, sizeof(name), "%" PRIu32 ":%" PRIu32,
	         key_def->space_id, key_def->iid);
	struct phia_index *dup = phia_index_by_name(e, name);
	if (unlikely(dup)) {
		sr_error("index '%s' already exists", name);
		return NULL;
	}
	struct phia_index *db = ss_malloc(&e->a, sizeof(struct phia_index));
	if (unlikely(db == NULL)) {
		sr_oom();
		return NULL;
	}
	memset(db, 0, sizeof(*db));
	db->env = e;
	db->index = si_init(&e->r, db);
	if (unlikely(db->index == NULL)) {
		ss_free(&e->a, db);
		return NULL;
	}
	if (si_confcreate(&db->index->conf, e->r.a, key_def) |
	    sf_schemecreate(&db->index->scheme, e->r.a, key_def)) {
		si_close(db->index);
		ss_free(&e->a, db);
		return NULL;
	}
	sr_statusset(&db->index->status, SR_OFFLINE);
	sx_indexinit(&db->coindex, &e->xm, db, db->index,
		     &db->index->scheme);
	db->txn_min = sx_min(&e->xm);
	db->txn_max = UINT32_MAX;
	rlist_add(&e->db, &db->link);
	return db;
}

struct phia_index *
phia_index_by_name(struct phia_env *e, const char *name)
{
	struct phia_index *db;
	rlist_foreach_entry(db, &e->db, link) {
		if (strcmp(db->index->conf.name, name) == 0)
			return db;
	}
	return NULL;
}

static int phia_index_visible(struct phia_index *db, uint64_t txn)
{
	return txn > db->txn_min && txn <= db->txn_max;
}

static void
phia_index_bind(struct phia_index *db)
{
	int status = sr_status(&db->index->status);
	if (sr_statusactive_is(status))
		si_ref(db->index, SI_REFFE);
}

static void
phia_index_unbind(struct phia_index *db, uint64_t txn)
{
	if (phia_index_visible(db, txn))
		phia_index_unref(db);
}

size_t
phia_index_bsize(struct phia_index *db)
{
	si_profilerbegin(&db->rtp, db->index);
	si_profiler(&db->rtp);
	si_profilerend(&db->rtp);
	return db->rtp.memory_used;
}

uint64_t
phia_index_size(struct phia_index *db)
{
	si_profilerbegin(&db->rtp, db->index);
	si_profiler(&db->rtp);
	si_profilerend(&db->rtp);
	return db->rtp.count;
}

static int phia_index_recoverbegin(struct phia_index *db)
{
	/* open and recover repository */
	sr_statusset(&db->index->status, SR_FINAL_RECOVERY);
	/* do not allow to recover existing indexes
	 * during online (only create), since logpool
	 * reply is required. */
	int rc = si_recover(db->index);
	if (unlikely(rc == -1))
		goto error;
	db->created = rc;
	return 0;
error:
	sr_statusset(&db->index->status, SR_MALFUNCTION);
	return -1;
}

static int phia_index_recoverend(struct phia_index *db)
{
	int status = sr_status(&db->index->status);
	if (unlikely(status == SR_DROP_PENDING))
		return 0;
	sr_statusset(&db->index->status, SR_ONLINE);
	return 0;
}

struct phia_document *
phia_document_new(struct phia_index *db)
{
	struct phia_env *e = db->env;
	struct phia_document *doc;
	doc = ss_malloc(&e->a, sizeof(struct phia_document));
	if (unlikely(doc == NULL)) {
		sr_oom();
		return NULL;
	}
	memset(doc, 0, sizeof(*doc));
	doc->db = db;
	return doc;
}

static inline struct phia_tuple*
phia_tuple_build(struct runtime *r, struct sfscheme *scheme, struct phia_field *fields)
{
	int size = sf_writesize(scheme, fields);
	struct phia_tuple *v = ss_malloc(r->a, sizeof(struct phia_tuple) + size);
	if (unlikely(v == NULL))
		return NULL;
	v->size      = size;
	v->lsn       = 0;
	v->flags     = 0;
	v->refs      = 1;
	char *ptr = v->data;
	sf_write(scheme, fields, ptr);
	/* update runtime statistics */
	tt_pthread_mutex_lock(&r->stat->lock);
	r->stat->v_count++;
	r->stat->v_allocated += sizeof(struct phia_tuple) + size;
	tt_pthread_mutex_unlock(&r->stat->lock);
	return v;
}

static inline int
phia_document_build(struct phia_document *o, enum phia_order order)
{
	struct phia_index *db = o->db;
	struct sfscheme *scheme = &db->index->scheme;
	struct phia_env *e = db->env;

	if (likely(o->value != NULL))
		return 0;

	assert(o->value == NULL);

	/* create document using current format, supplied
	 * key-chain and value */
	if (unlikely(o->fields_count_keys != scheme->keys_count))
	{
		/* set unspecified min/max keys, depending on
		 * iteration order */
		sf_limitset(&e->limit, scheme, o->fields, order);
		o->fields_count = scheme->fields_count;
		o->fields_count_keys = scheme->keys_count;
	}

	struct phia_tuple *v = phia_tuple_build(db->index->r, scheme, o->fields);
	if (unlikely(v == NULL))
		return sr_oom();
	o->value = v;
	return 0;
}

static inline struct phia_tuple*
phia_tuple_from_sv(struct runtime *r, struct sv *sv)
{
	char *src = sv_pointer(sv);
	size_t size = sv_size(sv);
	struct phia_tuple *v = ss_malloc(r->a, sizeof(struct phia_tuple) + size);
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
	r->stat->v_allocated += sizeof(struct phia_tuple) + size;
	tt_pthread_mutex_unlock(&r->stat->lock);
	return v;
}

static inline void
phia_tuple_ref(struct phia_tuple *v)
{
	v->refs++;
}

static inline int
phia_tuple_unref(struct runtime *r, struct phia_tuple *v)
{
	if (likely(--v->refs == 0)) {
		uint32_t size = phia_tuple_size(v);
		/* update runtime statistics */
		tt_pthread_mutex_lock(&r->stat->lock);
		assert(r->stat->v_count > 0);
		assert(r->stat->v_allocated >= size);
		r->stat->v_count--;
		r->stat->v_allocated -= size;
		tt_pthread_mutex_unlock(&r->stat->lock);
		ss_free(r->a, v);
		return 1;
	}
	return 0;
}

int
phia_document_delete(struct phia_document *v)
{
	struct phia_env *e = v->db->env;
	if (v->value != NULL)
		phia_tuple_unref(v->db->index->r, v->value);
	v->value = NULL;
	ss_free(&e->a, v);
	return 0;
}

int
phia_document_set_field(struct phia_document *v, const char *path,
			const char *pointer, int size)
{
	struct phia_index *db = v->db;
	struct phia_env *e = db->env;
	struct sffield *field = sf_schemefind(&db->index->scheme, (char*)path);
	if (unlikely(field == NULL))
		return -1;
	assert(field->position < (int)(sizeof(v->fields) / sizeof(struct phia_field)));
	struct phia_field *fv = &v->fields[field->position];
	if (size == 0)
		size = strlen(pointer);
	int fieldsize_max;
	if (field->key) {
		fieldsize_max = 1024;
	} else {
		fieldsize_max = 2 * 1024 * 1024;
	}
	if (unlikely(size > fieldsize_max)) {
		sr_error("field '%s' is too big (%d limit)",
		         pointer, fieldsize_max);
		return -1;
	}
	if (fv->data == NULL) {
		v->fields_count++;
		if (field->key)
			v->fields_count_keys++;
	}
	fv->data = (char *)pointer;
	fv->size = size;
	sr_statkey(&e->stat, size);
	return 0;
}

char *
phia_document_field(struct phia_document *v, const char *path, uint32_t *size)
{
	/* match field */
	struct phia_index *db = v->db;
	struct sffield *field = sf_schemefind(&db->index->scheme, (char*)path);
	if (unlikely(field == NULL))
		return NULL;
	/* result document */
	assert(v->value);
	return sf_fieldof(&db->index->scheme, field->position,
			  v->value->data, size);
}

int64_t
phia_document_lsn(struct phia_document *v)
{
	if (v->value == NULL)
		return -1;
	return v->value->lsn;
}

static inline int
phia_document_validate(struct phia_document *o, struct phia_index *dest, uint8_t flags)
{
	struct phia_env *e = o->db->env;
	if (unlikely(o->db != dest))
		return sr_error("%s", "incompatible document parent db");
	o->value->flags = flags;
	if (o->value->lsn != 0) {
		uint64_t lsn = sr_seq(&e->seq, SR_LSN);
		if (o->value->lsn <= lsn)
			return sr_error("%s", "incompatible document lsn");
	}
	return 0;
}

static inline int
phia_tx_write(struct phia_tx *t, struct phia_document *o, uint8_t flags)
{
	struct phia_env *e = t->env;
	struct phia_index *db = o->db;

	/* validate req */
	if (unlikely(t->t.state == SXPREPARE)) {
		sr_error("%s", "transaction is in 'prepare' state (read-only)");
		return -1;
	}

	/* validate index status */
	int status = sr_status(&db->index->status);
	switch (status) {
	case SR_SHUTDOWN_PENDING:
	case SR_DROP_PENDING:
		if (unlikely(! phia_index_visible(db, t->t.id))) {
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

	/* create document */
	int rc = phia_document_build(o, PHIA_EQ);
	if (unlikely(rc == -1))
		return -1;
	rc = phia_document_validate(o, db, flags);
	if (unlikely(rc == -1))
		return -1;

	phia_tuple_ref(o->value);
	int size = phia_tuple_size(o->value);

	/* concurrent index only */
	rc = sx_set(&t->t, &db->coindex, o->value);
	phia_document_delete(o);
	if (unlikely(rc != 0))
		return -1;
	ss_quota(&e->quota, SS_QADD, size);
	return 0;
}

int
phia_replace(struct phia_tx *tx, struct phia_document *key)
{
	return phia_tx_write(tx, key, 0);
}

int
phia_upsert(struct phia_tx *tx, struct phia_document *key)
{
	return phia_tx_write(tx, key, SVUPSERT);
}

int
phia_delete(struct phia_tx *tx, struct phia_document *key)
{
	return phia_tx_write(tx, key, SVDELETE);
}

int
phia_get(struct phia_tx *t, struct phia_document *key,
	 struct phia_document **result, bool cache_only)
{
	struct phia_index *db = key->db;
	/* validate index */
	int status = sr_status(&db->index->status);
	switch (status) {
	case SR_SHUTDOWN_PENDING:
	case SR_DROP_PENDING:
		if (unlikely(! phia_index_visible(db, t->t.id))) {
			sr_error("%s", "index is invisible to the transaction");
			return -1;
		}
		break;
	case SR_ONLINE:
	case SR_INITIAL_RECOVERY:
	case SR_FINAL_RECOVERY:
		break;
	default:
		assert(0);
		return -1;
	}

	/* prepare the key */
	int rc = phia_document_build(key, PHIA_EQ);
	if (unlikely(rc == -1))
		return -1;

	struct phia_tuple *value;
	struct phia_statget statget;
	if (phia_index_read(db, key->value, PHIA_EQ, &value, &t->t, 1, NULL,
			    cache_only, &statget) != 0) {
		return -1;
	}

	if (value == NULL) {
		 *result = NULL;
		 return 0;
	}

	struct phia_document *doc = phia_document_new(db);
	if (unlikely(doc == NULL)) {
		phia_tuple_unref(db->index->r, value);
		return -1;
	}
	doc->value = value;
	sr_statget(&db->env->stat, &statget);

	*result = doc;
	return 0;
}

static inline void
phia_tx_free(struct phia_tx *tx)
{
	struct phia_env *env = tx->env;
	sv_logfree(&tx->log, &env->a);
	ss_free(&env->a, tx);
}

static inline void
phia_tx_end(struct phia_tx *t, int rlb, int conflict)
{
	struct phia_env *e = t->env;
	uint32_t count = sv_logcount(&t->log);
	sx_gc(&t->t);
	sv_logreset(&t->log);
	sr_stattx(&e->stat, t->start, count, rlb, conflict);
	struct phia_index *db, *tmp;
	rlist_foreach_entry_safe(db, &e->db, link, tmp) {
		phia_index_unbind(db, t->t.id);
	}
	phia_tx_free(t);
}

int
phia_rollback(struct phia_tx *tx)
{
	sx_rollback(&tx->t);
	phia_tx_end(tx, 1, 0);
	return 0;
}

static int
phia_tx_prepare(struct sx *x, struct sv *v, struct phia_index *db,
		struct sicache *cache)
{
	assert(v->i == &sx_vif); /* sv holds sxv */

	struct siread q;
	si_readopen(&q, db->index, cache,
	            PHIA_EQ,
	            x->vlsn,
	            NULL,
	            0,
	            sv_pointer(v),
	            sv_size(v));
	q.has = 1;
	int rc = si_read(&q);
	si_readclose(&q);

	if (unlikely(q.result))
		phia_tuple_unref(db->index->r, q.result);
	return rc;
}

int
phia_commit(struct phia_tx *t)
{
	struct phia_env *e = t->env;
	int status = sr_status(&e->status);
	if (unlikely(! sr_statusactive_is(status)))
		return -1;
	int recover = (status == SR_FINAL_RECOVERY);

	/* prepare transaction */
	if (t->t.state == SXREADY || t->t.state == SXLOCK)
	{
		struct sicache *cache = NULL;
		sxpreparef prepare = NULL;
		if (! recover) {
			prepare = phia_tx_prepare;
			cache = si_cachepool_pop(&e->cachepool);
			if (unlikely(cache == NULL))
				return sr_oom();
		}
		enum sxstate s = sx_prepare(&t->t, prepare, cache);
		if (cache)
			si_cachepool_push(cache);
		if (s == SXLOCK) {
			sr_stattx_lock(&e->stat);
			return 2;
		}
		if (s == SXROLLBACK) {
			sx_rollback(&t->t);
			phia_tx_end(t, 0, 1);
			return 1;
		}
		assert(s == SXPREPARE);

		sx_commit(&t->t);

		if (t->half_commit) {
			/* Half commit mode.
			 *
			 * A half committed transaction is no longer
			 * being part of concurrent index, but still can be
			 * commited or rolled back.
			 * Yet, it is important to maintain external
			 * serial commit order.
			*/
			return 0;
		}
	}
	assert(t->t.state == SXCOMMIT);

	/* do wal write and backend commit */
	if (unlikely(recover))
		recover = 2;
	int rc;
	rc = sc_write(&e->scheduler, &t->log, t->lsn, recover);
	if (unlikely(rc == -1))
		sx_rollback(&t->t);

	phia_tx_end(t, 0, 0);
	return rc;
}

void
phia_tx_set_lsn(struct phia_tx *tx, int64_t lsn)
{
	tx->lsn = lsn;
}

void
phia_tx_set_half_commit(struct phia_tx *tx, bool half_commit)
{
	tx->half_commit = half_commit;
}

struct phia_tx *
phia_begin(struct phia_env *e)
{
	struct phia_tx *t;
	t = ss_malloc(&e->a, sizeof(struct phia_tx));
	if (unlikely(t == NULL)) {
		sr_oom();
		return NULL;
	}
	t->env = e;
	sv_loginit(&t->log);
	sx_init(&e->xm, &t->t, &t->log);
	t->start = clock_monotonic64();
	t->lsn = 0;
	t->half_commit = 0;
	sx_begin(&e->xm, &t->t, SXRW, &t->log, UINT64_MAX);
	struct phia_index *db;
	rlist_foreach_entry(db, &e->db, link) {
		phia_index_bind(db);
	}
	return t;
}

struct phia_env *
phia_env_new(void)
{
	struct phia_env *e = malloc(sizeof(*e));
	if (unlikely(e == NULL))
		return NULL;
	memset(e, 0, sizeof(*e));
	sr_statusinit(&e->status);
	sr_statusset(&e->status, SR_OFFLINE);
	ss_vfsinit(&e->vfs, &ss_stdvfs);
	ss_aopen(&e->a, &ss_stda);
	int rc;
	rc = se_confinit(&e->conf, e);
	if (unlikely(rc == -1))
		goto error;
	rlist_create(&e->db);
	ss_quotainit(&e->quota);
	sr_seqinit(&e->seq);
	sr_statinit(&e->stat);
	sf_limitinit(&e->limit, &e->a);
	sr_init(&e->r, &e->status, &e->a, &e->vfs, &e->quota,
		&e->conf.zones, &e->seq, &e->stat);
	sx_managerinit(&e->xm, &e->r);
	si_cachepool_init(&e->cachepool, &e->r);
	sc_init(&e->scheduler, &e->r);

	e->conf.path_create = 0;
	e->conf.path = ss_strdup(&e->a, cfg_gets("phia_dir"));
	if (e->conf.path == NULL) {
		sr_oom();
		goto error;
	}
	e->conf.memory_limit = cfg_getd("phia.memory_limit")*1024*1024*1024;

	/* configure zone = 0 */
	struct srzone *z = &e->conf.zones.zones[0];
	assert(z->enable);
	z->compact_wm = cfg_geti("phia.compact_wm");
	z->branch_prio = cfg_geti("phia.branch_prio");
	z->branch_age = cfg_geti("phia.branch_age");
	z->branch_age_period = cfg_geti("phia.branch_age_period");
	z->branch_age_wm = cfg_geti("phia.branch_age_wm");

	/* validate configuration */
	rc = se_confvalidate(&e->conf);
	if (unlikely(rc == -1))
		goto error;

	/* set memory quota (disable during recovery) */
	ss_quotaset(&e->quota, e->conf.memory_limit);
	ss_quotaenable(&e->quota, 0);

	/* Ensure phia data directory exists. */
	rc = sr_checkdir(&e->r, e->conf.path);
	if (unlikely(rc == -1))
		goto error;
	/* enable quota */
	ss_quotaenable(&e->quota, 1);

	return e;
error:
	phia_env_delete(e);
	return NULL;
}

struct phia_service *
phia_service_new(struct phia_env *env)
{
	struct phia_service *srv = ss_malloc(env->r.a, sizeof(struct phia_service));
	if (srv == NULL) {
		sr_oom();
		return NULL;
	}
	srv->env = env;
	sd_cinit(&srv->sdc);
	return srv;
}

int
phia_service_do(struct phia_service *srv)
{
	return sc_ctl_call(srv, sx_vlsn(&srv->env->xm));
}

void
phia_service_delete(struct phia_service *srv)
{
	sd_cfree(&srv->sdc, srv->env->r.a);
	ss_free(srv->env->r.a, srv);
}
