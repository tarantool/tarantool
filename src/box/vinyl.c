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

#include <dirent.h>
#include <pmatomic.h>

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
#include "errinj.h"

#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"

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

static struct srzone *
sr_zoneof(struct vy_env *r);

struct vy_buf {
	/** Start of the allocated buffer */
	char *s;
	/** End of the used area */
	char *p;
	/** End of the buffer */
	char *e;
};

static void
vy_buf_create(struct vy_buf *b)
{
	b->s = NULL;
	b->p = NULL;
	b->e = NULL;
}

static void
vy_buf_destroy(struct vy_buf *b)
{
	if (unlikely(b->s == NULL))
		return;
	free(b->s);
	b->s = NULL;
	b->p = NULL;
	b->e = NULL;
}

static size_t
vy_buf_size(struct vy_buf *b) {
	return b->e - b->s;
}

static size_t
vy_buf_used(struct vy_buf *b) {
	return b->p - b->s;
}

static int
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

static void
vy_buf_advance(struct vy_buf *b, size_t size)
{
	b->p += size;
}

static void*
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

static int64_t
vy_quota_used(struct vy_quota *q)
{
	return q->used;
}

static int
vy_quota_used_percent(struct vy_quota *q)
{
	if (q->limit == 0)
		return 0;
	return (q->used * 100) / q->limit;
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

struct vy_avg {
	uint64_t count;
	uint64_t total;
	uint32_t min, max;
	double   avg;
	char sz[32];
};

static void
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

static void
vy_avg_prepare(struct vy_avg *a)
{
	snprintf(a->sz, sizeof(a->sz), "%"PRIu32" %"PRIu32" %.1f",
	         a->min, a->max, a->avg);
}

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

static struct vy_stat *
vy_stat_new()
{
	struct vy_stat *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		diag_set(OutOfMemory, sizeof(*s), "stat", "struct");
		return NULL;
	}
	return s;
}

static void
vy_stat_delete(struct vy_stat *s)
{
	free(s);
}

static void
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

static void
vy_stat_get(struct vy_stat *s, const struct vy_stat_get *statget)
{
	s->get++;
	vy_avg_update(&s->get_read_disk, statget->read_disk);
	vy_avg_update(&s->get_read_cache, statget->read_cache);
	vy_avg_update(&s->get_latency, statget->read_latency);
}

static void
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

static void
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
};

struct srzonemap {
	struct srzone zones[11];
};

static void
sr_zonemap_set(struct srzonemap *m, uint32_t percent, struct srzone *z)
{
	if (unlikely(percent > 100))
		percent = 100;
	percent = percent - percent % 10;
	int p = percent / 10;
	m->zones[p] = *z;
	snprintf(m->zones[p].name, sizeof(m->zones[p].name), "%d", percent);
}

static struct srzone*
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

static uint32_t
vy_tuple_size(struct vy_tuple *v);

static struct vy_tuple *
vy_tuple_alloc(uint32_t size);

static const char *
vy_tuple_key_part(const char *tuple_data, uint32_t part_id);

static int
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

static struct vy_tuple *
vy_apply_upsert(struct vy_tuple *upsert, struct vy_tuple *object,
		struct vy_index *index, bool suppress_error);

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
	struct vy_mem *next;
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

static struct vy_mem *
vy_mem_new(struct key_def *key_def)
{
	struct vy_mem *index = malloc(sizeof(*index));
	if (!index) {
		diag_set(OutOfMemory, sizeof(*index),
			 "malloc", "struct vy_mem");
		return NULL;
	}
	index->next = NULL;
	index->min_lsn = INT64_MAX;
	index->used = 0;
	index->key_def = key_def;
	index->version = 0;
	vy_mem_tree_create(&index->tree, index,
			   vy_mem_alloc_matras_page,
			   vy_mem_free_matras_page);
	return index;
}

static void
vy_mem_delete(struct vy_mem *index)
{
	assert(index == index->tree.arg);
	struct vy_mem_tree_iterator itr = vy_mem_tree_iterator_first(&index->tree);
	while (!vy_mem_tree_iterator_is_invalid(&itr)) {
		struct vy_tuple *v =
			*vy_mem_tree_iterator_get_elem(&index->tree, &itr);
		vy_tuple_unref(v);
		vy_mem_tree_iterator_next(&index->tree, &itr);
	}
	vy_mem_tree_destroy(&index->tree);
	free(index);
}

static int
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
	vy_tuple_ref(v);
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
	/* Sizes of containing structures */
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
	/** size of min data block */
	uint32_t  min_size;
	/** start of min keys array (global) */
	uint64_t  min_offset;
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
	/* count of records */
	uint32_t count;
	/* offset of page data in run */
	uint64_t offset;
	/* size of page data in file */
	uint32_t size;
	/* size of page data in memory, i.e. unpacked */
	uint32_t unpacked_size;
	/* offset of page's min key in page index key storage */
	uint32_t min_key_offset;
	/* minimal lsn of all records in page */
	int64_t min_lsn;
	/* maximal lsn of all records in page */
	int64_t max_lsn;
};

struct PACKED vy_tuple_info {
	/* record lsn */
	int64_t lsn;
	/* offset in data block */
	uint32_t offset;
	/* size of tuple */
	uint32_t size;
	/* flags */
	uint8_t flags;
	/* for 4-byte alignment */
	uint8_t reserved[3];
};

struct vy_run {
	struct vy_run_info info;
	/* buffer with struct vy_page_info */
	struct vy_buf pages;
	/* buffer with min keys of pages */
	struct vy_buf pages_min;
	struct vy_run *next;
};

struct vy_range {
	int64_t   id;
	/**
	 * Minimal key of the range
	 */
	struct vy_tuple *min_key;
	struct vy_index *index;
	uint64_t   update_time;
	uint32_t   used; /* sum of mem->used */
	struct vy_run  *run;
	uint32_t   run_count;
	struct vy_mem *mem;
	uint32_t   mem_count;
	/** Number of times the range was compacted. */
	int        merge_count;
	uint32_t   temperature;
	uint64_t   temperature_reads;
	/** The file where the run is stored or -1 if it's not dumped yet. */
	int fd;
	char path[PATH_MAX];
	rb_node(struct vy_range) tree_node;
	struct heap_node   nodecompact;
	struct heap_node   nodedump;
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
	struct key_def *key_def;
	struct tuple_format *tuple_format;
	uint32_t key_map_size; /* size of key_map map */
	uint32_t *key_map; /* field_id -> part_id map */
	/** Member of env->db or scheduler->shutdown. */
	struct rlist link;

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
	/** Cursor creation time, used for statistics. */
	uint64_t start;
	/**
	 * All open cursors are registered in a transaction
	 * they belong to. When the transaction ends, the cursor
	 * is closed.
	 */
	struct rlist next_in_tx;
};


static struct txv *
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

static void
txv_delete(struct txv *v)
{
	vy_tuple_unref(v->tuple);
	free(v);
}

static void
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

static void
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

static const char *
vy_run_min_key(struct vy_run *run, const struct vy_page_info *p)
{
	assert(run->info.count > 0);
	return run->pages_min.s + p->min_key_offset;
}

static struct vy_page_info *
vy_run_page(struct vy_run *run, uint32_t pos)
{
	assert(pos < run->info.count);
	return (struct vy_page_info *)
		vy_buf_at(&run->pages, sizeof(struct vy_page_info), pos);
}

static uint32_t
vy_run_total(struct vy_run *run)
{
	if (unlikely(run->pages.s == NULL))
		return 0;
	return run->info.total;
}

static uint32_t
vy_run_size(struct vy_run *run)
{
	return sizeof(run->info) +
	       run->info.count * sizeof(struct vy_page_info) +
	       run->info.min_size;
}

static int
vy_index_dump_range_index(struct vy_index *index);
static int
vy_index_checkpoint_range_index(struct vy_index *index, int64_t lsn);

static struct vy_run *
vy_run_new()
{
	struct vy_run *run = (struct vy_run *)malloc(sizeof(struct vy_run));
	if (unlikely(run == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_run), "malloc",
			 "struct vy_run");
		return NULL;
	}
	vy_buf_create(&run->pages);
	vy_buf_create(&run->pages_min);
	memset(&run->info, 0, sizeof(run->info));
	run->next = NULL;
	return run;
}

static void
vy_run_delete(struct vy_run *run)
{
	vy_buf_destroy(&run->pages);
	vy_buf_destroy(&run->pages_min);
	TRASH(run);
	free(run);
}

#define FILE_ALIGN	512
#define ALIGN_POS(pos)	(pos + (FILE_ALIGN - (pos % FILE_ALIGN)) % FILE_ALIGN)

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

static int64_t
vy_range_mem_min_lsn(struct vy_range *range)
{
	int64_t min_lsn = INT64_MAX;
	for (struct vy_mem *mem = range->mem; mem; mem = mem->next) {
		if (mem->min_lsn < min_lsn)
			min_lsn = mem->min_lsn;
	}
	return min_lsn;
}

static int
vy_range_cmp(struct vy_range *range, void *key, struct key_def *key_def)
{
	assert(range->min_key != NULL);
	return vy_tuple_compare(range->min_key->data, key, key_def);
}

static int
vy_range_cmpnode(struct vy_range *n1, struct vy_range *n2, struct key_def *key_def)
{
	if (n1 == n2)
		return 0;
	assert(n1->min_key != NULL && n2->min_key != NULL);
	return vy_tuple_compare(n1->min_key->data, n2->min_key->data, key_def);
}

static uint64_t
vy_range_size(struct vy_range *range)
{
	uint64_t size = 0;
	struct vy_run *run = range->run;
	while (run) {
		size += vy_run_size(run) + vy_run_total(run);
		run = run->next;
	}
	return size;
}

static void
vy_scheduler_add_range(struct vy_scheduler *, struct vy_range *range);
static void
vy_scheduler_update_range(struct vy_scheduler *, struct vy_range *range);
static void
vy_scheduler_remove_range(struct vy_scheduler *, struct vy_range*);

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

static int vy_range_delete(struct vy_range *);

struct vy_range *
vy_range_tree_free_cb(vy_range_tree_t *t, struct vy_range *range, void *arg)
{
	(void)t;
	(void)arg;
	vy_range_delete(range);
	return NULL;
}

struct vy_range *
vy_range_tree_unsched_cb(vy_range_tree_t *t, struct vy_range *range, void *arg)
{
	(void)t;
	struct vy_index *index = (struct vy_index *) arg;
	vy_scheduler_remove_range(index->env->scheduler, range);
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

static void
vy_index_delete(struct vy_index *index);

struct vy_range_iterator {
	struct vy_index *index;
	struct vy_range *cur_range;
	enum vy_order order;
	char *key;
	int key_size;
};

static void
vy_range_iterator_open(struct vy_range_iterator *itr, struct vy_index *index,
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
	if (vy_tuple_key_part(itr->key, 0) == NULL) {
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
	struct vy_range_tree_key tree_key;
	tree_key.data = itr->key;
	tree_key.size = itr->key_size;
	itr->cur_range = vy_range_tree_psearch(&index->tree, &tree_key);
	if (itr->cur_range == NULL)
		itr->cur_range = vy_range_tree_first(&index->tree);
	assert(itr->cur_range != NULL);
}

static struct vy_range *
vy_range_iterator_get(struct vy_range_iterator *ii)
{
	return ii->cur_range;
}

static void
vy_range_iterator_next(struct vy_range_iterator *ii)
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
vy_range_init_min_key(struct vy_range *range)
{
	struct vy_index *index = range->index;
	const char *min_key = NULL;

	assert(range->min_key == NULL);
	assert(range->used == 0);

	/* Find the minimal key if any. */
	for (struct vy_run *run = range->run; run != NULL; run = run->next) {
		struct vy_page_info *p = vy_run_page(run, 0);
		const char *key = vy_run_min_key(run, p);
		if (min_key == NULL ||
		    vy_tuple_compare(key, min_key, index->key_def) < 0) {
			min_key = key;
		}
	}

	/* Create a tuple with the minimal key. */
	if (min_key == NULL) {
		range->min_key = vy_tuple_from_key(index, NULL, 0);
	} else {
		range->min_key = vy_tuple_extract_key_raw(index, min_key);
	}

	if (range->min_key == NULL)
		return -1;

	return 0;
}

static void
vy_index_add_range(struct vy_index *index, struct vy_range *range)
{
	assert(range->min_key != NULL);
	vy_range_tree_insert(&index->tree, range);
	index->range_index_version++;
	index->range_count++;
}

static void
vy_index_remove_range(struct vy_index *index, struct vy_range *range)
{
	vy_range_tree_remove(&index->tree, range);
	index->range_index_version++;
	index->range_count--;
}

/* dump tuple to the run page buffers (tuple header and data) */
static int
vy_run_dump_tuple(struct vy_tuple *value, struct vy_buf *info_buf,
		  struct vy_buf *data_buf, struct vy_page_info *info)
{
	int64_t lsn = value->lsn;
	if (vy_buf_ensure(info_buf, sizeof(struct vy_tuple_info)))
		return -1;
	struct vy_tuple_info *tupleinfo = (struct vy_tuple_info *)info_buf->p;
	tupleinfo->flags = value->flags;
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

struct vy_write_iterator;

static struct vy_write_iterator *
vy_write_iterator_new(bool save_delete, struct vy_index *index,
		      int64_t purge_lsn);
static int
vy_write_iterator_add_run(struct vy_write_iterator *wi, struct vy_run *run,
			  int fd, bool is_mutable, bool control_eof);
static int
vy_write_iterator_add_mem(struct vy_write_iterator *wi, struct vy_mem *mem,
			  bool is_mutable, bool control_eof);
static int
vy_write_iterator_next(struct vy_write_iterator *wi);

static int
vy_write_iterator_get(struct vy_write_iterator *wi, struct vy_tuple **result);

static void
vy_write_iterator_delete(struct vy_write_iterator *wi);

/**
 * Write tuples from the iterator to a new page in the run,
 * update page and run statistics.
 *
 * Returns -1 in case of error, 0 if there's more data for this
 * run, or 1 if the caller should proceed to the next run.
 */
static int
vy_run_write_page(struct vy_run *run, int fd, struct vy_write_iterator *wi,
		  struct vy_tuple *split_key, struct key_def *key_def,
		  uint32_t page_size)
{
	struct vy_run_info *run_info = &run->info;
	bool run_done = false;

	struct vy_buf tuplesinfo, values, compressed;
	vy_buf_create(&tuplesinfo);
	vy_buf_create(&values);
	vy_buf_create(&compressed);

	if (vy_buf_ensure(&run->pages, sizeof(struct vy_page_info)))
		goto err;

	struct vy_page_info *page = (struct vy_page_info *)run->pages.p;
	memset(page, 0, sizeof(*page));
	page->min_lsn = INT64_MAX;
	page->offset = run_info->offset + run_info->size;

	while (true) {
		struct vy_tuple *tuple;
		int rc = vy_write_iterator_get(wi, &tuple);
		if (rc < 0) {
			goto err; /* error */
		} else if (rc == 1) {
			run_done = true; /* no more data */
			break;
		}
		assert(rc == 0);
		if (split_key != NULL && vy_tuple_compare(tuple->data,
					split_key->data, key_def) >= 0) {
			/* Split key reached, proceed to the next run. */
			run_done = true;
			break;
		}
		if (vy_buf_used(&values) >= page_size)
			break;
		if (vy_run_dump_tuple(tuple, &tuplesinfo, &values, page) != 0)
			goto err;
		vy_write_iterator_next(wi);
	}
	page->unpacked_size = vy_buf_used(&tuplesinfo) + vy_buf_used(&values);
	page->unpacked_size = ALIGN_POS(page->unpacked_size);

	vy_buf_ensure(&compressed, page->unpacked_size);
	memcpy(compressed.p, tuplesinfo.s, vy_buf_used(&tuplesinfo));
	vy_buf_advance(&compressed, vy_buf_used(&tuplesinfo));
	memcpy(compressed.p, values.s, vy_buf_used(&values));
	vy_buf_advance(&compressed, vy_buf_used(&values));

	page->size = vy_buf_used(&compressed);
	if (vy_write_file(fd, compressed.s, page->size) < 0) {
		vy_error("index file error: %s", strerror(errno));
		goto err;
	}

	page->crc = crc32_calc(0, compressed.s, vy_buf_used(&compressed));

	assert(page->count > 0);
	struct vy_buf *min_buf = &run->pages_min;
	struct vy_tuple_info *tuplesinfoarr = (struct vy_tuple_info *) tuplesinfo.s;
	struct vy_tuple_info *mininfo = &tuplesinfoarr[0];
	if (vy_buf_ensure(min_buf, mininfo->size))
		goto err;

	page->min_key_offset = vy_buf_used(min_buf);
	char *minvalue = values.s + mininfo->offset;
	memcpy(min_buf->p, minvalue, mininfo->size);
	vy_buf_advance(min_buf, mininfo->size);
	vy_buf_advance(&run->pages, sizeof(struct vy_page_info));

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
	return run_done ? 1 : 0;
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
vy_run_write(int fd, struct vy_write_iterator *wi,
	     struct vy_tuple *split_key, struct key_def *key_def,
	     uint32_t page_size, struct vy_run **result)
{
	int rc = 0;
	struct vy_run *run = vy_run_new();
	if (!run)
		return -1;

	struct vy_run_info *header = &run->info;
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
	if (vy_write_file(fd, header, header_size) < 0)
		goto err_file;
	header->size += header_size;

	/*
	 * Read from the iterator until it's exhausted or
	 * the split key is reached.
	 */
	do {
		rc = vy_run_write_page(run, fd, wi, split_key, key_def,
				       page_size);
		if (rc < 0)
			goto err;
	} while (rc == 0);

	/* Write pages index */
	header->pages_offset = header->offset +
				     header->size;
	header->pages_size = vy_buf_used(&run->pages);
	if (vy_write_file(fd, run->pages.s, header->pages_size) < 0)
		goto err_file;
	header->size += header->pages_size;

	/* Write min-max keys for pages */
	header->min_offset = header->offset + header->size;
	header->min_size = vy_buf_used(&run->pages_min);
	if (vy_write_file(fd, run->pages_min.s, header->min_size) < 0)
		goto err_file;
	header->size += header->min_size;

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
	if (vy_pwrite_file(fd, header, header_size, header->offset) < 0 ||
	    fdatasync(fd) != 0)
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
	rc = ftruncate(fd, header->offset);
	(void) rc;
	free(run);
	return -1;
}

static struct vy_range *
vy_range_new(struct vy_index *index, int64_t id)
{
	struct vy_range *range = (struct vy_range*) calloc(1, sizeof(*range));
	if (range == NULL) {
		diag_set(OutOfMemory, sizeof(struct vy_range), "malloc",
			 "struct vy_range");
		return NULL;
	}
	range->mem = vy_mem_new(index->key_def);
	if (!range->mem) {
		free(range);
		return NULL;
	}
	if (id != 0) {
		range->id = id;
		index->range_id_max = MAX(index->range_id_max, id + 1);
	} else {
		range->id = ++index->range_id_max;
	}
	range->mem_count = 1;
	range->fd = -1;
	range->index = index;
	range->nodedump.pos = UINT32_MAX;
	range->nodecompact.pos = UINT32_MAX;
	return range;
}

static int
vy_range_close(struct vy_range *range)
{
	int rcret = 0;

	if (range->fd >= 0 && close(range->fd) < 0) {
		vy_error("index file close error: %s", strerror(errno));
		rcret = -1;
	}
	return rcret;
}

static ssize_t
vy_range_read(struct vy_range *range, void *buf, size_t size, off_t offset)
{
	ssize_t n = vy_pread_file(range->fd, buf, size, offset);
	if (n < 0) {
		vy_error("error reading range file %s: %s",
			 range->path, strerror(errno));
		return -1;
	}
	if ((size_t)n != size) {
		vy_error("range file %s corrupted", range->path);
		return -1;
	}
	return n;
}

static int
vy_range_recover(struct vy_range *range)
{
	struct vy_run *run = NULL;
	off_t offset = 0;
	int rc = -1;

	while (true) {
		struct vy_run_info *h;
		ssize_t n;

		run = vy_run_new();
		if (run == NULL)
			goto out;

		/* Read run header. */
		h = &run->info;
		n = vy_pread_file(range->fd, h, sizeof(*h), offset);
		if (n == 0)
			break; /* eof */
		if (n < (ssize_t)sizeof(*h) || h->size == 0) {
			 vy_error("run was not finished, range is"
				  "broken for file %s", range->path);
			 goto out;
		}

		/* Allocate buffer for page info. */
		if (vy_buf_ensure(&run->pages, h->pages_size) == -1 ||
		    vy_buf_ensure(&run->pages_min, h->min_size) == -1)
			goto out;

		/* Read page info. */
		if (vy_range_read(range, run->pages.s,
				  h->pages_size, h->pages_offset) < 0 ||
		    vy_range_read(range, run->pages_min.s,
				  h->min_size, h->min_offset) < 0)
			goto out;

		run->next = range->run;
		range->run = run;
		++range->run_count;
		run = NULL;

		offset = h->offset + h->size;
	}

	/*
	 * Set file offset to the end of the last run so that new runs
	 * will be appended to the range file.
	 */
	if (lseek(range->fd, offset, SEEK_SET) == -1) {
		vy_error("failed to seek range file %s: %s",
			 range->path, strerror(errno));
		goto out;
	}

	if (vy_range_init_min_key(range) == -1)
		goto out;

	rc = 0; /* success */
out:
	if (run)
		vy_run_delete(run);
	return rc;
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
	/* schedule range */
	vy_scheduler_add_range(index->env->scheduler, range);
	return 0;
}

static void
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

static void
vy_range_delete_mems(struct vy_range *range)
{
	struct vy_mem *p = range->mem;
	struct vy_mem *next = NULL;
	while (p) {
		next = p->next;
		vy_mem_delete(p);
		p = next;
	}
}

static int
vy_range_delete(struct vy_range *range)
{
	assert(range->nodedump.pos == UINT32_MAX);
	assert(range->nodecompact.pos == UINT32_MAX);

	int rcret = 0;

	if (range->min_key)
		vy_tuple_unref(range->min_key);

	vy_range_delete_runs(range);
	vy_range_delete_mems(range);

	if (vy_range_close(range))
		rcret = -1;
	TRASH(range);
	free(range);
	return rcret;
}

/*
 * Append tuples returned by a write iterator to a range file until
 * split_key is encountered. p_fd is supposed to point to the range file
 * fd. If fd is < 0, a new file will be created for the range and p_fd
 * initialized appropriately. On success, the function returns 0 and the
 * new run is returned in p_run.
 */
static int
vy_range_write_run(struct vy_range *range, struct vy_write_iterator *wi,
		   struct vy_tuple *split_key, int *p_fd, struct vy_run **p_run)
{
	struct vy_index *index = range->index;
	char path[PATH_MAX];
	int fd = *p_fd;
	bool created = false;

	/* Do not write empty runs. */
	if (vy_write_iterator_get(wi, NULL) != 0)
		return 0;

	if (fd < 0) {
		/*
		 * The range hasn't been written to yet and hence
		 * doesn't have a file. Create a temporary file for it
		 * in the index directory to write the run to.
		 */
		ERROR_INJECT(ERRINJ_VY_RANGE_CREATE,
			     {errno = EMFILE; goto create_failed;});
		snprintf(path, PATH_MAX, "%s/.tmpXXXXXX", index->path);
		fd = mkstemp(path);
		if (fd < 0) {
create_failed:
			vy_error("Failed to create temp file '%s': %s",
				 path, strerror(errno));
			goto fail;
		}
		created = true;
	}

	/* Append tuples to the range file. */
	if (vy_run_write(fd, wi, split_key, index->key_def,
			 index->key_def->opts.page_size, p_run) != 0)
		goto fail;

	if (created) {
		/*
		 * We've successfully written a run to a new range file.
		 * Commit the range by linking the file to a proper name.
		 */
		snprintf(range->path, PATH_MAX, "%s/%016"PRIx64".range",
			 index->path, range->id);
		if (rename(path, range->path) != 0) {
			vy_error("Failed to link range file '%s': %s",
				 range->path, strerror(errno));
			goto fail;
		}
	}

	*p_fd = fd;
	return 0;
fail:
	if (created) {
		unlink(path);
		close(fd);
	}
	return -1;
}

/*
 * Return true and set split_key accordingly if the range needs to be
 * split in two.
 *
 * - We should never split a range until it was merged at least once
 *   (actually, it should be a function of compact_wm/number of runs
 *   used for the merge: with low compact_wm it's more than once, with
 *   high compact_wm it's once).
 * - We should use the last run size as the size of the range.
 * - We should split around the last run middle key.
 * - We should only split if the last run size is greater than
 *   4/3 * range_size.
 */
static bool
vy_range_need_split(struct vy_range *range, const char **split_key)
{
	struct key_def *key_def = range->index->key_def;
	struct vy_run *run = NULL;

	/* The range hasn't been merged yet - too early to split it. */
	if (range->merge_count < 1)
		return false;

	/* Find the oldest run. */
	assert(range->run != NULL);
	for (run = range->run; run->next; run = run->next) { }

	/* The range is too small to be split. */
	if (run->info.total < key_def->opts.range_size * 4 / 3)
		return false;

	/* Find the median key in the oldest run (approximately). */
	struct vy_page_info *p = vy_run_page(run, run->info.count / 2);
	const char *k = vy_run_min_key(run, p);

	/* No point in splitting if a new range is going to be empty. */
	if (vy_tuple_compare(range->min_key->data, k, key_def) == 0)
		return false;

	*split_key = k;
	return true;
}

struct vy_range_compact_part {
	struct vy_range *range;
	struct vy_run *run;
	int fd;
};

static int
vy_range_compact_prepare(struct vy_range *range,
			 struct vy_range_compact_part *parts, int *p_n_parts)
{
	struct vy_index *index = range->index;
	struct vy_tuple *split_key = NULL;
	const char *split_key_raw;
	int n_parts = 1;
	int i;

	if (vy_range_need_split(range, &split_key_raw)) {
		split_key = vy_tuple_extract_key_raw(index, split_key_raw);
		if (split_key == NULL)
			goto fail;
		n_parts = 2;
	}

	/* Allocate new ranges and initialize parts. */
	for (i = 0; i < n_parts; i++) {
		struct vy_range *r = vy_range_new(index, 0);
		if (r == NULL)
			goto fail;
		parts[i].range = r;
		parts[i].run = NULL;
		parts[i].fd = -1;
	}

	/* Set min keys for the new ranges. */
	vy_tuple_ref(range->min_key);
	parts[0].range->min_key = range->min_key;
	if (split_key != NULL)
		parts[1].range->min_key = split_key;

	/* Replace the old range with the new ones. */
	vy_index_remove_range(index, range);
	for (i = 0; i < n_parts; i++) {
		struct vy_range *r = parts[i].range;

		/* Link mem and run to the original range. */
		r->mem->next = range->mem;
		r->mem_count = range->mem_count + 1;
		r->run = range->run;
		r->run_count = range->run_count;
		r->fd = range->fd;

		vy_index_add_range(index, r);
	}

	*p_n_parts = n_parts;
	return 0;
fail:
	for (i = 0; i < n_parts; i++) {
		struct vy_range *r = parts[i].range;
		if (r != NULL)
			vy_range_delete(r);
	}
	if (split_key != NULL)
		vy_tuple_unref(split_key);
	return -1;
}

static void
vy_range_compact_commit(struct vy_range *range, int n_parts,
			struct vy_range_compact_part *parts)
{
	struct vy_index *index = range->index;
	int i;

	index->size -= vy_range_size(range);
	for (i = 0; i < n_parts; i++) {
		struct vy_range *r = parts[i].range;

		/* Unlink new ranges from the old one. */
		r->mem->next = NULL;
		r->mem_count = 1;
		r->run = parts[i].run;
		r->run_count = r->run ? 1 : 0;
		r->fd = parts[i].fd;

		/*
		 * If a new range is empty, delete it unless
		 * it's the only one.
		 */
		if (r->run == NULL && r->used == 0 && index->range_count > 1) {
			vy_index_remove_range(index, r);
			vy_range_delete(r);
			continue;
		}

		index->size += vy_range_size(r);

		/* Account merge w/o split. */
		if (n_parts == 1 && r->run != NULL)
			r->merge_count = range->merge_count + 1;

		/* Make the new range visible to the scheduler. */
		vy_scheduler_add_range(range->index->env->scheduler, r);
	}
	index->range_index_version++;
	vy_range_delete(range);
}

static void
vy_range_compact_abort(struct vy_range *range, int n_parts,
		       struct vy_range_compact_part *parts)
{
	struct vy_index *index = range->index;
	int i;

	/*
	 * So, we failed to write runs for the new ranges. Attach their
	 * in-memory indexes to the original range and delete them.
	 */

	for (i = 0; i < n_parts; i++) {
		struct vy_range *r = parts[i].range;

		vy_index_remove_range(index, r);

		/* No point in linking an empty mem. */
		if (r->used != 0) {
			r->mem->next = range->mem;
			range->mem = r->mem;
			range->mem_count++;
			range->used += r->used;
			r->mem = NULL;
		}

		/* Prevent original range's data from being destroyed. */
		if (r->mem != NULL)
			r->mem->next = NULL;
		r->run = NULL;
		r->fd = -1;
		vy_range_delete(r);

		if (parts[i].run != NULL)
			vy_run_delete(parts[i].run);
		if (parts[i].fd >= 0)
			close(parts[i].fd);
	}

	/*
	 * Finally, insert the old range back to the tree and make it
	 * visible to the scheduler.
	 */
	vy_index_add_range(index, range);
	vy_scheduler_add_range(index->env->scheduler, range);
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
		p->total_run_count += range->run_count;
		if (p->total_run_max < range->run_count)
			p->total_run_max = range->run_count;
		if (range->run_count < 20)
			p->histogram_run[range->run_count]++;
		else
			p->histogram_run_20plus++;
		for (struct vy_mem *mem = range->mem; mem; mem = mem->next) {
			p->count += mem->tree.size;
			memory_used += mem->used;
		}
		struct vy_run *run = range->run;
		while (run != NULL) {
			p->count += run->info.keys;
//			p->count_dup += run->header.dupkeys;
			int indexsize = vy_run_size(run);
			p->total_snapshot_size += indexsize;
			p->total_range_size += indexsize + run->info.total;
			p->total_range_origin_size += indexsize + run->info.totalorigin;
			p->total_page_count += run->info.count;
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
	struct vy_range *range = vy_range_new(index, 0);
	if (unlikely(range == NULL))
		return -1;
	if (vy_range_init_min_key(range) != 0) {
		vy_range_delete(range);
		return -1;
	}
	vy_index_add_range(index, range);
	vy_scheduler_add_range(index->env->scheduler, range);
	index->size = vy_range_size(range);
	return 0;
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
		struct vy_range *range = vy_range_new(index, range_id);
		if (!range) {
			vy_error("%s", "Can't alloc range");
			vy_range_delete(range);
			return -1;
		}
		char range_path[PATH_MAX];
		snprintf(range_path, PATH_MAX, "%s/%016"PRIx64".range",
			 index->path, range_id);
		if (vy_range_open(index, range, range_path)) {
			vy_range_delete(range);
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
		struct vy_range *range = vy_range_new(index, 0);
		if (unlikely(range == NULL))
			return -1;
		if (vy_range_init_min_key(range) != 0) {
			vy_range_delete(range);
			return -1;
		}
		vy_index_add_range(index, range);
		index->size = vy_range_size(range);
	}

	return 0;
}

/**
 * Iterate over the write set of a single index
 * and flush it to the active in-memory tree of this index.
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
		struct vy_range_iterator ii;
		vy_range_iterator_open(&ii, index, VINYL_GE, tuple->data, tuple->size);
		range = vy_range_iterator_get(&ii);
		assert(range != NULL);
		if (prev_range != NULL && range != prev_range) {
			/*
			 * The write set is key-ordered, hence
			 * we can safely assume there won't be new
			 * keys for this range.
			 */
			prev_range->update_time = time;
			vy_scheduler_update_range(index->env->scheduler,
						  prev_range);
		}
		prev_range = range;
		/* insert into range index */
		int rc = vy_mem_set(range->mem, tuple);
		assert(rc == 0); /* TODO: handle BPS tree errors properly */
		(void) rc;
		/* update range */
		range->used += vy_tuple_size(tuple);
		quota += vy_tuple_size(tuple);
	}
	if (range != NULL) {
		range->update_time = time;
		vy_scheduler_update_range(index->env->scheduler, range);
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

	/** ->execute ret code */
	int status;

	/** index this task is for */
	struct vy_index *index;

	/**
	 * View sequence number at the time when the task was scheduled.
	 * TODO: move it to arg as not all tasks need it.
	 */
	int64_t vlsn;

	/** extra arguments, depend on task kind */
	union {
		struct {
			struct vy_range *range;
			struct vy_run *new_run;
		} dump;
		struct {
			struct vy_range *range;
			int n_parts;
			struct vy_range_compact_part parts[2];
		} compact;
	};
	/**
	 * A link in the list of all pending tasks, generated by
	 * task scheduler.
	 */
	struct stailq_entry link;
};

static struct vy_task *
vy_task_new(struct mempool *pool, struct vy_index *index,
	    const struct vy_task_ops *ops)
{
	struct vy_task *task = mempool_alloc(pool);
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task), "scheduler", "task");
		return NULL;
	}
	memset(task, 0, sizeof(*task));
	task->ops = ops;
	task->index = index;
	vy_index_ref(index);
	return task;
}

static void
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
	struct vy_index *index = task->index;
	struct vy_range *range = task->dump.range;
	struct vy_write_iterator *wi;
	int rc;

	wi = vy_write_iterator_new(1, index, task->vlsn);
	if (wi == NULL)
		return -1;

	/*
	 * We dump all memory indexes but the newest one - see comment
	 * in vy_task_dump_new().
	 */
	for (struct vy_mem *mem = range->mem->next; mem; mem = mem->next) {
		rc = vy_write_iterator_add_mem(wi, mem, 0, 0);
		if (rc != 0)
			goto out;
	}

	rc = vy_range_write_run(range, wi, NULL,
				&range->fd, &task->dump.new_run);
out:
	vy_write_iterator_delete(wi);
	return rc;
}

static int
vy_task_dump_complete(struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->dump.range;
	struct vy_run *run = task->dump.new_run;
	struct vy_mem *mem;

	/*
	 * No need to roll back anything on dump failure - the range will just
	 * carry on with a new shadow memory tree.
	 */
	if (task->status != 0)
		goto out;

	run->next = range->run;
	range->run = run;
	range->run_count++;

	index->size += vy_run_size(run) + vy_run_total(run);

	range->range_version++;
	index->range_index_version++;

	/* Release dumped in-memory indexes */
	mem = range->mem->next;
	range->mem->next = NULL;
	range->mem_count = 1;
	while (mem) {
		struct vy_mem *next = mem->next;
		assert(range->used >= mem->used);
		range->used -= mem->used;
		vy_quota_release(index->env->quota, mem->used);
		vy_mem_delete(mem);
		mem = next;
	}

	if (range->run_count == 1) {
		/*
		 * The range file was created successfully,
		 * update the range index on disk.
		 */
		if (index->first_dump_lsn == 0)
			index->first_dump_lsn = run->info.min_lsn;
		vy_index_dump_range_index(index);
	}

out:
	vy_scheduler_add_range(index->env->scheduler, range);
	return 0;
}

static struct vy_task *
vy_task_dump_new(struct mempool *pool, struct vy_range *range)
{
	static struct vy_task_ops dump_ops = {
		.execute = vy_task_dump_execute,
		.complete = vy_task_dump_complete,
	};

	struct vy_task *task = vy_task_new(pool, range->index, &dump_ops);
	if (!task)
		return NULL;

	struct vy_mem *mem = vy_mem_new(range->index->key_def);
	if (!mem) {
		vy_task_delete(pool, task);
		return NULL;
	}

	/*
	 * New insertions will go to the new in-memory tree, while we will dump
	 * older trees. This way we don't need to bother about synchronization.
	 * To be consistent, lookups fall back on older trees.
	 */
	mem->next = range->mem;
	range->mem = mem;
	range->mem_count++;

	task->dump.range = range;
	return task;
}

static int
vy_task_compact_execute(struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->compact.range;
	struct vy_range_compact_part *parts = task->compact.parts;
	int n_parts = task->compact.n_parts;
	struct vy_write_iterator *wi;
	int rc = 0;

	assert(range->nodedump.pos == UINT32_MAX);
	assert(range->nodecompact.pos == UINT32_MAX);

	wi = vy_write_iterator_new(0, index, task->vlsn);
	if (wi == NULL)
		return -1;

	/* Compact in-memory indexes. First add mems then add runs. */
	for (struct vy_mem *mem = range->mem; mem; mem = mem->next) {
		rc = vy_write_iterator_add_mem(wi, mem, 0, 0);
		if (rc != 0)
			goto out;
	}

	/* Compact on disk runs. */
	for (struct vy_run *run = range->run; run; run = run->next) {
		rc = vy_write_iterator_add_run(wi, run, range->fd, 0, 0);
		if (rc != 0)
			goto out;
	}

	assert(n_parts > 0);
	for (int i = 0; i < n_parts; i++) {
		struct vy_range_compact_part *p = &parts[i];
		struct vy_tuple *split_key = i < n_parts - 1 ?
			parts[i + 1].range->min_key : NULL;

		rc = vy_range_write_run(p->range, wi, split_key,
					&parts[i].fd, &p->run);
		if (rc != 0)
			goto out;
	}
out:
	vy_write_iterator_delete(wi);
	return rc;
}

static int
vy_task_compact_complete(struct vy_task *task)
{
	struct vy_index *index = task->index;
	struct vy_range *range = task->compact.range;
	struct vy_range_compact_part *parts = task->compact.parts;
	int n_parts = task->compact.n_parts;

	if (task->status != 0) {
		vy_range_compact_abort(range, n_parts, parts);
		return 0;
	}

	vy_range_compact_commit(range, n_parts, parts);

	if (vy_index_dump_range_index(index)) {
		/*
		 * TODO: we should roll back the failed dump first, but it
		 * requires a redesign of the index change function.
		 */
		return -1;
	}

	return 0;
}

static struct vy_task *
vy_task_compact_new(struct mempool *pool, struct vy_range *range)
{
	static struct vy_task_ops compact_ops = {
		.execute = vy_task_compact_execute,
		.complete = vy_task_compact_complete,
	};

	struct vy_task *task = vy_task_new(pool, range->index, &compact_ops);
	if (!task)
		return NULL;

	if (vy_range_compact_prepare(range, task->compact.parts,
				     &task->compact.n_parts) != 0) {
		vy_task_delete(pool, task);
		return NULL;
	}
	task->compact.range = range;
	return task;
}

static int
vy_task_drop_execute(struct vy_task *task)
{
	assert(task->index->refs == 1); /* referenced by this task */
	vy_index_delete(task->index);
	task->index = NULL;
	return 0;
}

static struct vy_task *
vy_task_drop_new(struct mempool *pool, struct vy_index *index)
{
	static struct vy_task_ops drop_ops = {
		.execute = vy_task_drop_execute,
	};

	return vy_task_new(pool, index, &drop_ops);
}

/* Scheduler Task }}} */

/* {{{ Scheduler */

#define HEAP_NAME vy_dump_heap

static int
heap_dump_less(struct heap_node *a, struct heap_node *b)
{
	const struct vy_range *left =
		container_of(a, struct vy_range, nodedump);
	const struct vy_range *right =
		container_of(b, struct vy_range, nodedump);
	return left->used > right->used;
}

#define HEAP_LESS(h, l, r) heap_dump_less(l, r)

#include "salad/heap.h"

#undef HEAP_LESS
#undef HEAP_NAME

#define HEAP_NAME vy_compact_heap

static int
heap_compact_less(struct heap_node *a, struct heap_node *b)
{
	const struct vy_range *left =
				container_of(a, struct vy_range, nodecompact);
	const struct vy_range *right =
				container_of(b, struct vy_range, nodecompact);
	return left->run_count > right->run_count;
}

#define HEAP_LESS(h, l, r) heap_compact_less(l, r)

#include "salad/heap.h"

struct vy_scheduler {
	pthread_mutex_t        mutex;
	int64_t       checkpoint_lsn_last;
	int64_t       checkpoint_lsn;
	bool checkpoint_in_progress;
	int            count;
	struct vy_index **indexes;
	struct rlist   shutdown;
	struct vy_env    *env;
	heap_t dump_heap;
	heap_t compact_heap;

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
	scheduler->env = env;
	rlist_create(&scheduler->shutdown);
	vy_compact_heap_create(&scheduler->compact_heap);
	vy_dump_heap_create(&scheduler->dump_heap);
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

	free(scheduler->indexes);
	vy_compact_heap_destroy(&scheduler->compact_heap);
	vy_dump_heap_destroy(&scheduler->dump_heap);
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
	if (!scheduler->is_worker_pool_running &&
	     scheduler->env->status == VINYL_ONLINE)
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
	vy_index_unref(index);
	/* add index to `shutdown` list */
	rlist_add(&scheduler->shutdown, &index->link);
	return 0;
}

static bool
vy_range_need_dump(struct vy_range *range)
{
	return (range->used >= 10 * 1024 * 1024 ||
		range->used >= range->index->key_def->opts.range_size);
}

static void
vy_scheduler_add_range(struct vy_scheduler *scheduler,
		       struct vy_range *range)
{
	vy_dump_heap_insert(&scheduler->dump_heap, &range->nodedump);
	vy_compact_heap_insert(&scheduler->compact_heap, &range->nodecompact);
	assert(range->nodedump.pos != UINT32_MAX);
	assert(range->nodecompact.pos != UINT32_MAX);
}

static void
vy_scheduler_update_range(struct vy_scheduler *scheduler,
			  struct vy_range *range)
{
	if (likely(range->nodedump.pos == UINT32_MAX))
		return; /* range is being processed by a task */

	vy_dump_heap_update(&scheduler->dump_heap, &range->nodedump);
	assert(range->nodedump.pos != UINT32_MAX);
	assert(range->nodecompact.pos != UINT32_MAX);

	if (vy_range_need_dump(range)) {
		/* Wake up scheduler */
		ipc_cond_signal(&scheduler->scheduler_cond);
	}
}

static void
vy_scheduler_remove_range(struct vy_scheduler *scheduler,
			  struct vy_range *range)
{
	vy_dump_heap_delete(&scheduler->dump_heap, &range->nodedump);
	vy_compact_heap_delete(&scheduler->compact_heap, &range->nodecompact);
	range->nodedump.pos = UINT32_MAX;
	range->nodecompact.pos = UINT32_MAX;
}

static int
vy_scheduler_peek_checkpoint(struct vy_scheduler *scheduler,
			     int64_t checkpoint_lsn, struct vy_task **ptask)
{
	/* try to peek a range which has min
	 * lsn <= required value
	*/
	struct vy_range *range;
	struct heap_node *pn = NULL;
	struct heap_iterator it;
	vy_dump_heap_iterator_init(&scheduler->dump_heap, &it);
	while ((pn = vy_dump_heap_iterator_next(&it))) {
		range = container_of(pn, struct vy_range, nodedump);
		if (vy_range_mem_min_lsn(range) > checkpoint_lsn)
			continue;
		if (range->used == 0)
			continue;
		*ptask = vy_task_dump_new(&scheduler->task_pool, range);
		if (*ptask == NULL)
			return -1; /* OOM */
		vy_scheduler_remove_range(scheduler, range);
		return 0; /* new task */
	}
	*ptask = NULL;
	return 0; /* nothing to do */
}

static int
vy_scheduler_peek_dump(struct vy_scheduler *scheduler, struct vy_task **ptask)
{
	/* try to peek a range with a biggest in-memory index */
	struct vy_range *range;
	struct heap_node *pn = NULL;
	struct heap_iterator it;
	vy_dump_heap_iterator_init(&scheduler->dump_heap, &it);
	while ((pn = vy_dump_heap_iterator_next(&it))) {
		range = container_of(pn, struct vy_range, nodedump);
		if (!vy_range_need_dump(range))
			return 0; /* nothing to do */
		*ptask = vy_task_dump_new(&scheduler->task_pool, range);
		if (*ptask == NULL)
			return -1; /* oom */
		vy_scheduler_remove_range(scheduler, range);
		return 0; /* new task */
	}
	*ptask = NULL;
	return 0; /* nothing to do */
}

static int
vy_scheduler_peek_compact(struct vy_scheduler *scheduler, uint32_t run_count,
			  struct vy_task **ptask)
{
	/* try to peek a range with a biggest number
	 * of runs */
	struct vy_range *range;
	struct heap_node *pn = NULL;
	struct heap_iterator it;
	vy_compact_heap_iterator_init(&scheduler->compact_heap, &it);
	while ((pn = vy_compact_heap_iterator_next(&it))) {
		range = container_of(pn, struct vy_range, nodecompact);
		if (range->run_count < run_count)
			break; /* TODO: why ? */
		*ptask = vy_task_compact_new(&scheduler->task_pool,
					     range);
		if (*ptask == NULL)
			return -1; /* OOM */
		vy_scheduler_remove_range(scheduler, range);
		return 0; /* new task */
	}
	*ptask = NULL;
	return 0; /* nothing to do */
}

static int
vy_scheduler_peek_shutdown(struct vy_scheduler *scheduler,
			   struct vy_index *index, struct vy_task **ptask)
{
	if (index->refs > 0) {
		*ptask = NULL;
		return 0; /* index still has tasks */
	}

	/* make sure the index won't get scheduled any more */
	vy_range_tree_iter(&index->tree, NULL, vy_range_tree_unsched_cb, index);

	*ptask = vy_task_drop_new(&scheduler->task_pool, index);
	if (*ptask == NULL)
		return -1;
	return 0; /* new task */
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

	int rc;
	*ptask = NULL;

	/* checkpoint */
	if (scheduler->checkpoint_in_progress) {
		rc = vy_scheduler_peek_checkpoint(scheduler,
			scheduler->checkpoint_lsn, ptask);
		if (rc != 0)
			return rc; /* error */
		if (*ptask != NULL)
			goto found;
	}

	/* dumping */
	rc = vy_scheduler_peek_dump(scheduler, ptask);
	if (rc != 0)
		return rc; /* error */
	if (*ptask != NULL)
		goto found;

	/* compaction */
	rc = vy_scheduler_peek_compact(scheduler, zone->compact_wm, ptask);
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
			if (task->ops->complete && task->ops->complete(task))
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

		/* Wait for changes */
		ipc_cond_wait(&scheduler->scheduler_cond);
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
	assert(scheduler->env->status == VINYL_ONLINE);

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
				is_active |= (vy_range_mem_min_lsn(range) <=
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
	};
	struct srzone redzone = {
		.compact_wm        = 4,
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

static struct srzone *
sr_zoneof(struct vy_env *env)
{
	int p = vy_quota_used_percent(env->quota);
	return sr_zonemap(&env->conf->zones, p);
}

int
vy_index_read(struct vy_index*, struct vy_tuple*, enum vy_order order,
		struct vy_tuple **, struct vy_tx*);

/** {{{ Introspection */

static struct vy_info_node *
vy_info_append(struct vy_info_node *root, const char *key)
{
	assert(root->childs_n < root->childs_cap);
	struct vy_info_node *node = &root->childs[root->childs_n];
	root->childs_n++;
	node->key = key;
	node->val_type = VINYL_NODE;
	return node;
}

static void
vy_info_append_u32(struct vy_info_node *root, const char *key, uint32_t value)
{
	struct vy_info_node *node = vy_info_append(root, key);
	node->value.u32 = value;
	node->val_type = VINYL_U32;
}

static void
vy_info_append_u64(struct vy_info_node *root, const char *key, uint64_t value)
{
	struct vy_info_node *node = vy_info_append(root, key);
	node->value.u64 = value;
	node->val_type = VINYL_U64;
}

static void
vy_info_append_str(struct vy_info_node *root, const char *key,
		   const char *value)
{
	struct vy_info_node *node = vy_info_append(root, key);
	node->value.str = value;
	node->val_type = VINYL_STRING;
}

static int
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

static int
vy_info_append_global(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_info_node *node = vy_info_append(root, "vinyl");
	if (vy_info_reserve(info, node, 4) != 0)
		return 1;
	vy_info_append_str(node, "path", info->env->conf->path);
	vy_info_append_str(node, "build", PACKAGE_VERSION);
	return 0;
}

static int
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

static int
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
	}
	return 0;
}

static int
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

static int
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

static int
vy_info_append_metric(struct vy_info *info, struct vy_info_node *root)
{
	struct vy_info_node *node = vy_info_append(root, "metric");
	if (vy_info_reserve(info, node, 2) != 0)
		return 1;

	vy_info_append_u64(node, "lsn", info->env->xm->lsn);
	return 0;
}

static int
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
		vy_info_append_u64(local_node, "page_size", o->key_def->opts.page_size);
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
	c->start = tx->start;
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
	vy_stat_cursor(e->stat, c->start, c->n_reads);
	TRASH(c);
	mempool_free(&e->cursor_pool, c);
}

/*** }}} Cursor */

static int
vy_index_conf_create(struct vy_index *conf, struct key_def *key_def)
{
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
	free(index);
	return NULL;
}

static void
vy_index_delete(struct vy_index *index)
{
	read_set_iter(&index->read_set, NULL, read_set_delete_cb, NULL);
	vy_range_tree_iter(&index->tree, NULL, vy_range_tree_free_cb, index);
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

static uint32_t
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

#if 0
/** Prints tuple first field. */
static const char *
vy_tuple_to_str(struct vy_tuple *tuple)
{
	static __thread char buf[23];
	const char *kk = vy_tuple_key_part(tuple->data, 0);
	uint64_t k = 0;
	if (kk)
		k = mp_decode_uint(&kk);
	snprintf(buf, sizeof(buf), "%llu", (unsigned long long) k);
	return buf;
}
#endif

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
static const char *
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
static int
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
		struct vy_tuple *res;
		res = vy_tuple_from_data(index, new_mp, new_mp_end);
		res->flags |= SVREPLACE;
		return res;
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
		if (old_tuple->flags & (SVDELETE | SVREPLACE)) {
			result_tuple->flags |= SVREPLACE;
		}
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

static void
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

	/* Start scheduler if there is at least one index */
	if (e->scheduler->count > 0)
		vy_scheduler_start(e->scheduler);
}

/** }}} Recovery */

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
};

/* }}} vy_tuple_iterator: Common interface for iterator over run, mem, etc */

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
	/** Parent class, must be the first member */
	struct vy_tuple_iterator base;

	/* Members needed for memory allocation and disk access */
	/* index */
	struct vy_index *index;
	/* run */
	struct vy_run *run;
	/* file of run */
	int fd;

	/* Search options */
	/**
	 * Order, that specifies direction, start position and stop criteria
	 * if the key is not specified, GT and EQ are changed to
	 * GE, LT to LE for beauty.
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
	/** LRU cache of two active pages (two pages is enough). */
	struct vy_page *curr_page;
	struct vy_page *prev_page;
	/** Is false until first .._get ot .._next_.. method is called */
	bool search_started;
	/** Search is finished, you will not get more values from iterator */
	bool search_ended;
};

static void
vy_run_iterator_open(struct vy_run_iterator *itr, struct vy_index *index,
		     struct vy_run *run, int fd,
		     enum vy_order order,
		     char *key, int64_t vlsn);

/* }}} vy_run_iterator API forward declaration */

/* {{{ vy_run_iterator vy_run_iterator support functions */
/* TODO: move to appropriate c file and remove */

/**
 * Page
 */
struct vy_page {
	/** Page position in the run file (used by run_iterator->page_cache */
	uint32_t page_no;
	/** The number of tuples */
	uint32_t count;
	/** Size of raw page data */
	uint32_t size;
	/** Raw page data */
	char data[0];
};

static struct vy_page *
vy_page_new(struct vy_page_info *page_info)
{
	struct vy_page *page = malloc(sizeof(*page) + page_info->size);
	if (page == NULL) {
		diag_set(OutOfMemory, sizeof(*page) + page_info->size,
			"load_page", "page cache");
		return NULL;
	}
	page->count = page_info->count;
	page->size = page_info->size;
	return page;
}

static void
vy_page_delete(struct vy_page *page)
{
#if !defined(NDEBUG)
	memset(page, '#', sizeof(*page) + page->size); /* fail early */
#endif /* !defined(NDEBUG) */
	free(page);
}

/**
 * Read raw tuple data from the page
 * \param page page
 * \param tuple_no tuple position in the page
 * \param[out] pinfo tuple metadata
 * \return tuple data including offsets table
 */
static const char *
vy_page_tuple(struct vy_page *page, uint32_t tuple_no,
	      struct vy_tuple_info **pinfo)
{
	assert(tuple_no < page->count);
	struct vy_tuple_info *info = ((struct vy_tuple_info *) page->data) +
		tuple_no;
	const char *tuple_data = page->data +
		sizeof(struct vy_tuple_info) * page->count + info->offset;
	assert(tuple_data <= page->data + page->size);
	*pinfo = info;
	return tuple_data; /* includes offset table */
}

/**
 * Get page from LRU cache
 * @retval page if found
 * @retval NULL otherwise
 */
static struct vy_page *
vy_run_iterator_cache_get(struct vy_run_iterator *itr, uint32_t page_no)
{
	if (itr->curr_page != NULL) {
		if (itr->curr_page->page_no == page_no)
			return itr->curr_page;
		if (itr->prev_page != NULL &&
		    itr->prev_page->page_no == page_no) {
			struct vy_page *result = itr->prev_page;
			itr->prev_page = itr->curr_page;
			itr->curr_page = result;
			return result;
		}
	}
	return NULL;
}

/**
 * Touch page in LRU cache.
 * The cache is at least two pages. Ensure that subsequent read keeps
 * the page_no in the cache by moving it to the start of LRU list.
 * @pre page must be in the cache
 */
static void
vy_run_iterator_cache_touch(struct vy_run_iterator *itr, uint32_t page_no)
{
	struct vy_page *page = vy_run_iterator_cache_get(itr, page_no);
	assert(page != NULL);
	(void) page;
}

/**
 * Put page to LRU cache
 */
static void
vy_run_iterator_cache_put(struct vy_run_iterator *itr, struct vy_page *page,
			  uint32_t page_no)
{
	if (itr->prev_page != NULL)
		vy_page_delete(itr->prev_page);
	itr->prev_page = itr->curr_page;
	itr->curr_page = page;
	page->page_no = page_no;
}

/**
 * Clear LRU cache
 */
static void
vy_run_iterator_cache_clean(struct vy_run_iterator *itr)
{
	if (itr->curr_tuple != NULL) {
		vy_tuple_unref(itr->curr_tuple);
		itr->curr_tuple = NULL;
		itr->curr_tuple_pos.page_no = UINT32_MAX;
	}
	if (itr->curr_page != NULL) {
		vy_page_delete(itr->curr_page);
		if (itr->prev_page != NULL)
			vy_page_delete(itr->prev_page);
		itr->curr_page = itr->prev_page = NULL;
	}
}

/**
 * Get a page by the given number the cache or load it from the disk.
 */
static int
vy_run_iterator_load_page(struct vy_run_iterator *itr, uint32_t page_no,
			  struct vy_page **result)
{
	/* Check cache */
	*result = vy_run_iterator_cache_get(itr, page_no);
	if (*result != NULL)
		return 0;

	/* Allocate buffers */
	struct vy_page_info *page_info = vy_run_page(itr->run, page_no);
	struct vy_page *page = vy_page_new(page_info);
	if (page == NULL)
		return -1;

	/* Read page data from the disk */
	int rc = vy_pread_file(itr->fd, page->data, page_info->size,
			       page_info->offset);

	if (rc < 0) {
		vy_page_delete(page);
		/* TODO: get file name from range */
		vy_error("index file read error: %s", strerror(errno));
		return -1;
	}

	/* Iterator is never used from multiple fibers */
	assert(vy_run_iterator_cache_get(itr, page_no) == NULL);

	/* Update cache */
	vy_run_iterator_cache_put(itr, page, page_no);

	*result = page;
	return 0;
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
 * Read key and lsn by a given wide position.
 * For the first record in a page reads the result from the page
 * index instead of fetching it from disk.
 *
 * @retval 0 success
 * @retval -1 read error or out of memory.
 * Affects: curr_loaded_page
 */
static int
vy_run_iterator_read(struct vy_run_iterator *itr,
		     struct vy_run_iterator_pos pos,
		     const char **data, int64_t *lsn)
{
	struct vy_page *page;
	int rc = vy_run_iterator_load_page(itr, pos.page_no, &page);
	if (rc != 0)
		return rc;
	struct vy_tuple_info *info;
	*data = vy_page_tuple(page, pos.pos_in_page, &info);
	*lsn = info->lsn;
	return 0;
}

/**
 * Binary search in page index
 * In terms of STL, makes lower_bound for EQ,GE,LT and upper_bound for GT,LE
 * Additionally *equal_key argument is set to true if the found value is
 * equal to given key (untouched otherwise)
 * @retval page number
 */
static uint32_t
vy_run_iterator_search_page(struct vy_run_iterator *itr, const char *key,
			    bool *equal_key)
{
	uint32_t beg = 0;
	uint32_t end = itr->run->info.count;
	/* for upper bound we change zero comparison result to -1 */
	int zero_cmp = itr->order == VINYL_GT || itr->order == VINYL_LE ? -1 : 0;
	while (beg != end) {
		uint32_t mid = beg + (end - beg) / 2;
		struct vy_page_info *page_info =
			vy_run_page(itr->run, mid);
		const char *fnd_key = vy_run_min_key(itr->run, page_info);
		int cmp = vy_tuple_compare(fnd_key, key, itr->index->key_def);
		cmp = cmp ? cmp : zero_cmp;
		*equal_key = *equal_key || cmp == 0;
		if (cmp < 0)
			beg = mid + 1;
		else
			end = mid;
	}
	return end;
}

/**
 * Binary search in page
 * In terms of STL, makes lower_bound for EQ,GE,LT and upper_bound for GT,LE
 * Additionally *equal_key argument is set to true if the found value is
 * equal to given key (untouched otherwise)
 * @retval position in the page
 */
static uint32_t
vy_run_iterator_search_in_page(struct vy_run_iterator *itr, const char *key,
			       struct vy_page *page, bool *equal_key)
{
	uint32_t beg = 0;
	uint32_t end = page->count;
	/* for upper bound we change zero comparison result to -1 */
	int zero_cmp = itr->order == VINYL_GT || itr->order == VINYL_LE ? -1 : 0;
	while (beg != end) {
		uint32_t mid = beg + (end - beg) / 2;
		struct vy_tuple_info *info;
		const char *fnd_key = vy_page_tuple(page, mid, &info);
		int cmp = vy_tuple_compare(fnd_key, key, itr->index->key_def);
		cmp = cmp ? cmp : zero_cmp;
		*equal_key = *equal_key || cmp == 0;
		if (cmp < 0)
			beg = mid + 1;
		else
			end = mid;
	}
	return end;
}

/**
 * Binary search in a run for the given key.
 * In terms of STL, makes lower_bound for EQ,GE,LT and upper_bound for GT,LE
 * Resulting wide position is stored it *pos argument
 * Additionally *equal_key argument is set to true if the found value is
 * equal to given key (untouched otherwise)
 *
 * @retval 0 success
 * @retval -1 read or memory error
 */
static int
vy_run_iterator_search(struct vy_run_iterator *itr, const char *key,
		       struct vy_run_iterator_pos *pos, bool *equal_key)
{
	pos->page_no = vy_run_iterator_search_page(itr, key, equal_key);
	if (pos->page_no == 0) {
		pos->pos_in_page = 0;
		return 0;
	}
	pos->page_no--;
	struct vy_page *page;
	int rc = vy_run_iterator_load_page(itr, pos->page_no, &page);
	if (rc != 0)
		return rc;
	bool equal_in_page = false;
	pos->pos_in_page = vy_run_iterator_search_in_page(itr, key, page,
							  &equal_in_page);
	if (pos->pos_in_page == page->count) {
		pos->page_no++;
		pos->pos_in_page = 0;
	} else {
		*equal_key = equal_in_page;
	}
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
	assert(pos->page_no < itr->run->info.count);
	if (order == VINYL_LE || order == VINYL_LT) {
		if (pos->page_no == 0 && pos->pos_in_page == 0)
			return 1;
		if (pos->pos_in_page > 0) {
			pos->pos_in_page--;
		} else {
			pos->page_no--;
			struct vy_page *page;
			int rc = vy_run_iterator_load_page(itr, pos->page_no,
							   &page);
			if (rc != 0)
				return rc;
			pos->pos_in_page = page->count - 1;
		}
	} else {
		assert(order == VINYL_GE || order == VINYL_GT ||
		       order == VINYL_EQ);
		struct vy_page *page;
		int rc = vy_run_iterator_load_page(itr, pos->page_no, &page);
		if (rc != 0)
			return rc;
		pos->pos_in_page++;
		if (pos->pos_in_page >= page->count) {
			pos->page_no++;
			pos->pos_in_page = 0;
			if (pos->page_no == itr->run->info.count)
				return 1;
		}
	}
	return 0;
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
	assert(itr->curr_pos.page_no < itr->run->info.count);
	const char *cur_key;
	int64_t cur_lsn;
	int rc = vy_run_iterator_read(itr, itr->curr_pos, &cur_key, &cur_lsn);
	if (rc != 0)
		return rc;
	while (cur_lsn > itr->vlsn) {
		rc = vy_run_iterator_next_pos(itr, itr->order, &itr->curr_pos);
		if (rc != 0) {
			if (rc > 0) {
				vy_run_iterator_cache_clean(itr);
				itr->search_ended = true;
			}
			return rc;
		}
		rc = vy_run_iterator_read(itr, itr->curr_pos, &cur_key,
					  &cur_lsn);
		if (rc != 0)
			return rc;
		if (itr->order == VINYL_EQ &&
		    vy_tuple_compare(cur_key, itr->key, itr->index->key_def)) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 1;
		}
	}
	if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		/* Remember the page_no of cur_key */
		uint32_t cur_key_page_no = itr->curr_pos.page_no;

		struct vy_run_iterator_pos test_pos;
		rc = vy_run_iterator_next_pos(itr, itr->order, &test_pos);
		while (rc == 0) {
			/*
			 * The cache is at least two pages. Ensure that
			 * subsequent read keeps the cur_key in the cache
			 * by moving its page to the start of LRU list.
			 */
			vy_run_iterator_cache_touch(itr, cur_key_page_no);

			const char *test_key;
			int64_t test_lsn;
			rc = vy_run_iterator_read(itr, test_pos, &test_key,
						  &test_lsn);
			if (rc != 0)
				return rc;
			struct key_def *key_def = itr->index->key_def;
			if (test_lsn > itr->vlsn ||
			    vy_tuple_compare(cur_key, test_key, key_def) != 0)
				break;
			itr->curr_pos = test_pos;

			/* See above */
			vy_run_iterator_cache_touch(itr, cur_key_page_no);

			rc = vy_run_iterator_next_pos(itr, itr->order, &test_pos);
		}

		rc = rc > 0 ? 0 : rc;
	}
	return rc;
}

/*
 * FIXME: vy_run_iterator_next_key() calls vy_run_iterator_start() which
 * recursivly calls vy_run_iterator_next_key().
 */
static int
vy_run_iterator_next_key(struct vy_tuple_iterator *vitr);

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
	assert(!itr->search_started);
	itr->search_started = true;

	if (itr->run->info.count == 1) {
		/* there can be a stupid bootstrap run in which it's EOF */
		struct vy_page_info *page_info = vy_run_page(itr->run, 0);

		if (!page_info->count) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 1;
		}
		struct vy_page *page;
		int rc = vy_run_iterator_load_page(itr, 0, &page);
		if (rc != 0)
			return rc;
	} else if (itr->run->info.count == 0) {
		/* never seen that, but it could be possible in future */
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		return 1;
	}

	struct vy_run_iterator_pos end_pos = {itr->run->info.count, 0};
	bool equal_found = false;
	int rc;
	if (vy_tuple_key_part(itr->key, 0) != NULL) {
		rc = vy_run_iterator_search(itr, itr->key, &itr->curr_pos,
					    &equal_found);
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
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		return 1;
	}
	if ((itr->order == VINYL_GE || itr->order == VINYL_GT) &&
	    itr->curr_pos.page_no == end_pos.page_no) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
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
		return vy_run_iterator_next_key(&itr->base);
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

/** Vtable for vy_tuple_iterator - declared below */
static struct vy_tuple_iterator_iface vy_run_iterator_iface;

/**
 * Open the iterator
 */
static void
vy_run_iterator_open(struct vy_run_iterator *itr, struct vy_index *index,
		     struct vy_run *run, int fd,
		     enum vy_order order,
		     char *key, int64_t vlsn)
{
	itr->base.iface = &vy_run_iterator_iface;

	itr->index = index;
	itr->run = run;
	itr->fd = fd;

	itr->order = order;
	itr->key = key;
	itr->vlsn = vlsn;

	itr->curr_tuple = NULL;
	itr->curr_pos.page_no = itr->run->info.count;
	itr->curr_tuple_pos.page_no = UINT32_MAX;
	itr->curr_page = NULL;
	itr->prev_page = NULL;

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
vy_run_iterator_get(struct vy_tuple_iterator *vitr, struct vy_tuple **result)
{
	assert(vitr->iface->get == vy_run_iterator_get);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;

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

	struct vy_page *page;
	int rc = vy_run_iterator_load_page(itr, itr->curr_pos.page_no, &page);
	if (rc != 0)
		return rc;
	struct vy_tuple_info *info;
	const char *key = vy_page_tuple(page, itr->curr_pos.pos_in_page, &info);
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
vy_run_iterator_next_key(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_key == vy_run_iterator_next_key);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;

	if (itr->search_ended)
		return 1;
	if (!itr->search_started) {
		int rc = vy_run_iterator_start(itr);
		if (rc != 0)
			return rc;
	}
	uint32_t end_page = itr->run->info.count;
	assert(itr->curr_pos.page_no <= end_page);
	struct key_def *key_def = itr->index->key_def;
	if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		if (itr->curr_pos.page_no == 0 &&
		    itr->curr_pos.pos_in_page == 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 1;
		}
		if (itr->curr_pos.page_no == end_page) {
			/* A special case for reverse iterators */
			uint32_t page_no = end_page - 1;
			struct vy_page *page;
			int rc = vy_run_iterator_load_page(itr, page_no, &page);
			if (rc != 0)
				return rc;
			if (page->count == 0) {
				vy_run_iterator_cache_clean(itr);
				itr->search_ended = true;
				return 1;
			}
			itr->curr_pos.page_no = page_no;
			itr->curr_pos.pos_in_page = page->count - 1;
			return vy_run_iterator_find_lsn(itr);
		}
	}
	assert(itr->curr_pos.page_no < end_page);

	const char *cur_key;
	int64_t cur_lsn;
	int rc = vy_run_iterator_read(itr, itr->curr_pos, &cur_key, &cur_lsn);
	if (rc != 0)
		return rc;
	uint32_t cur_key_page_no = itr->curr_pos.page_no;

	const char *next_key;
	int64_t next_lsn;
	do {
		int rc = vy_run_iterator_next_pos(itr, itr->order,
						  &itr->curr_pos);
		if (rc != 0) {
			if (rc > 0) {
				vy_run_iterator_cache_clean(itr);
				itr->search_ended = true;
			}
			return rc;
		}

		/*
		 * The cache is at least two pages. Ensure that
		 * subsequent read keeps the cur_key in the cache
		 * by moving its page to the start of LRU list.
		 */
		vy_run_iterator_cache_touch(itr, cur_key_page_no);

		rc = vy_run_iterator_read(itr, itr->curr_pos, &next_key,
					  &next_lsn);
		if (rc != 0)
			return rc;

		/* See above */
		vy_run_iterator_cache_touch(itr, cur_key_page_no);
	} while (vy_tuple_compare(cur_key, next_key, key_def) == 0);

	if (itr->order == VINYL_EQ &&
	    vy_tuple_compare(next_key, itr->key, key_def) != 0) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
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
vy_run_iterator_next_lsn(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_lsn == vy_run_iterator_next_lsn);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;

	if (itr->search_ended)
		return 1;
	if (!itr->search_started) {
		int rc = vy_run_iterator_start(itr);
		if (rc != 0)
			return rc;
	}
	assert(itr->curr_pos.page_no < itr->run->info.count);

	int rc;
	struct vy_run_iterator_pos next_pos;
	rc = vy_run_iterator_next_pos(itr, VINYL_GE, &next_pos);
	if (rc != 0)
		return rc;

	const char *cur_key;
	int64_t cur_lsn;
	rc = vy_run_iterator_read(itr, itr->curr_pos, &cur_key, &cur_lsn);
	if (rc != 0)
		return rc;

	const char *next_key;
	int64_t next_lsn;
	rc = vy_run_iterator_read(itr, next_pos, &next_key, &next_lsn);
	if (rc != 0)
		return rc;

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
vy_run_iterator_restore(struct vy_tuple_iterator *vitr,
			struct vy_tuple *last_tuple)
{
	assert(vitr->iface->restore == vy_run_iterator_restore);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;

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
	itr->order = (vy_tuple_key_part(save_key, 0) != NULL ? save_order :
		      save_order == VINYL_LE || save_order == VINYL_LT ?
		      VINYL_LE : VINYL_GE);
	itr->key = save_key;
	itr->vlsn = save_vlsn;
	if (rc < 0)
		return rc;
	if (itr->order == VINYL_EQ && rc == 0) {
		struct vy_tuple *found_tuple;
		rc = vy_run_iterator_get(vitr, &found_tuple);
		if (rc < 0)
			return rc;
		assert(rc == 0);
		if (vy_tuple_compare(found_tuple->data, itr->key,
				     itr->index->key_def) != 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = false;
			return 0;
		}
	}
	return rc == 0 ? 1 : 0;
}

/**
 * Close an iterator and free all resources
 */
static void
vy_run_iterator_close(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->close == vy_run_iterator_close);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;

	vy_run_iterator_cache_clean(itr);

	TRASH(itr);
}

static struct vy_tuple_iterator_iface vy_run_iterator_iface = {
	.get = vy_run_iterator_get,
	.next_key = vy_run_iterator_next_key,
	.next_lsn = vy_run_iterator_next_lsn,
	.restore = vy_run_iterator_restore,
	.close = vy_run_iterator_close
};

/* }}} vy_run_iterator API implementation */

/* {{{ vy_mem_iterator API forward declaration */
/* TODO: move to header and remove static keyword */

/**
 * Iterator over vy_mem
 */
struct vy_mem_iterator {
	/** Parent class, must be the first member */
	struct vy_tuple_iterator base;

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

/* Vtable for vy_tuple_iterator - declared below */
static struct vy_tuple_iterator_iface vy_mem_iterator_iface;

/**
 * vy_mem_iterator API forward declaration
 */

static void
vy_mem_iterator_open(struct vy_mem_iterator *itr, struct vy_mem *mem,
		     enum vy_order order, char *key, int64_t vlsn);

/* }}} vy_mem_iterator API forward declaration */

/* {{{ vy_mem_iterator support functions */

/**
 * Get a tuple by current position
 */
static struct vy_tuple *
vy_mem_iterator_curr_tuple(struct vy_mem_iterator *itr)
{
	return *vy_mem_tree_iterator_get_elem(&itr->mem->tree, &itr->curr_pos);
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
		vy_mem_tree_iterator_prev(&itr->mem->tree, &itr->curr_pos);
	else
		vy_mem_tree_iterator_next(&itr->mem->tree, &itr->curr_pos);
	if (vy_mem_tree_iterator_is_invalid(&itr->curr_pos))
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
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	assert(itr->curr_tuple == vy_mem_iterator_curr_tuple(itr));
	while (itr->curr_tuple->lsn > itr->vlsn) {
		if (vy_mem_iterator_step(itr) != 0 ||
		    (itr->order == VINYL_EQ &&
		     vy_tuple_compare(itr->curr_tuple->data, itr->key,
				      itr->mem->key_def))) {
			itr->search_ended = true;
			return 1;
		}
	}
	if (itr->order == VINYL_LE || itr->order == VINYL_LT) {
		struct vy_mem_tree_iterator prev_pos = itr->curr_pos;
		vy_mem_tree_iterator_prev(&itr->mem->tree, &prev_pos);

		while (!vy_mem_tree_iterator_is_invalid(&prev_pos)) {
			struct vy_tuple *prev_tuple =
				*vy_mem_tree_iterator_get_elem(&itr->mem->tree,
							       &prev_pos);
			struct key_def *key_def = itr->mem->key_def;
			if (prev_tuple->lsn > itr->vlsn ||
			    vy_tuple_compare(itr->curr_tuple->data,
					     prev_tuple->data, key_def) != 0)
				break;
			itr->curr_pos = prev_pos;
			itr->curr_tuple = prev_tuple;
			vy_mem_tree_iterator_prev(&itr->mem->tree, &prev_pos);
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
	if (vy_tuple_key_part(itr->key, 0) != NULL) {
		if (itr->order == VINYL_EQ) {
			bool exact;
			itr->curr_pos =
				vy_mem_tree_lower_bound(&itr->mem->tree,
							&tree_key, &exact);
			if (!exact) {
				itr->search_ended = true;
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
		itr->curr_pos = vy_mem_tree_iterator_first(&itr->mem->tree);
	}

	if (itr->order == VINYL_LT || itr->order == VINYL_LE)
		vy_mem_tree_iterator_prev(&itr->mem->tree, &itr->curr_pos);
	if (vy_mem_tree_iterator_is_invalid(&itr->curr_pos)) {
		itr->search_ended = true;
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
		vy_mem_tree_iterator_get_elem(&itr->mem->tree, &itr->curr_pos);
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
	itr->base.iface = &vy_mem_iterator_iface;

	assert(key != NULL);
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
vy_mem_iterator_get(struct vy_tuple_iterator *vitr, struct vy_tuple **result)
{
	assert(vitr->iface->get == vy_mem_iterator_get);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;

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
vy_mem_iterator_next_key(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_key == vy_mem_iterator_next_key);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;

	if (itr->search_ended ||
	    (!itr->search_started && vy_mem_iterator_start(itr) != 0))
		return 1;
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	vy_mem_iterator_check_version(itr);
	assert(itr->curr_tuple == vy_mem_iterator_curr_tuple(itr));
	struct key_def *key_def = itr->mem->key_def;

	struct vy_tuple *prev_tuple = itr->curr_tuple;
	do {
		if (vy_mem_iterator_step(itr) != 0) {
			itr->search_ended = true;
			return 1;
		}
	} while (vy_tuple_compare(prev_tuple->data, itr->curr_tuple->data,
				  key_def) == 0);

	if (itr->order == VINYL_EQ &&
	    vy_tuple_compare(itr->curr_tuple->data, itr->key, key_def) != 0) {
		itr->search_ended = true;
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
vy_mem_iterator_next_lsn(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_lsn == vy_mem_iterator_next_lsn);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;

	if (itr->search_ended ||
	    (!itr->search_started && vy_mem_iterator_start(itr) != 0))
		return 1;
	assert(!vy_mem_tree_iterator_is_invalid(&itr->curr_pos));
	vy_mem_iterator_check_version(itr);
	assert(itr->curr_tuple == vy_mem_iterator_curr_tuple(itr));
	struct key_def *key_def = itr->mem->key_def;

	struct vy_mem_tree_iterator next_pos = itr->curr_pos;
	vy_mem_tree_iterator_next(&itr->mem->tree, &next_pos);
	if (vy_mem_tree_iterator_is_invalid(&next_pos))
		return 1; /* EOF */

	struct vy_tuple *next_tuple =
		*vy_mem_tree_iterator_get_elem(&itr->mem->tree, &next_pos);
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
vy_mem_iterator_restore(struct vy_tuple_iterator *vitr,
			struct vy_tuple *last_tuple)
{
	assert(vitr->iface->restore == vy_mem_iterator_restore);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;

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
			vy_mem_tree_iterator_prev(&itr->mem->tree, &pos);
			if (vy_mem_tree_iterator_is_invalid(&pos))
				return rc;
			struct vy_tuple *t =
				*vy_mem_tree_iterator_get_elem(&itr->mem->tree, &pos);
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
		vy_mem_tree_iterator_prev(&itr->mem->tree, &pos);
		if (vy_mem_tree_iterator_is_invalid(&pos))
			return rc;
		struct vy_tuple *t =
			*vy_mem_tree_iterator_get_elem(&itr->mem->tree, &pos);
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
		vy_mem_tree_iterator_next(&itr->mem->tree, &pos);
		if (vy_mem_tree_iterator_is_invalid(&pos))
			return rc;
		struct vy_tuple *t =
			*vy_mem_tree_iterator_get_elem(&itr->mem->tree, &pos);
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
vy_mem_iterator_close(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->close == vy_mem_iterator_close);
	struct vy_mem_iterator *itr = (struct vy_mem_iterator *) vitr;
	TRASH(itr);
}

static struct vy_tuple_iterator_iface vy_mem_iterator_iface = {
	.get = vy_mem_iterator_get,
	.next_key = vy_mem_iterator_next_key,
	.next_lsn = vy_mem_iterator_next_lsn,
	.restore = vy_mem_iterator_restore,
	.close = vy_mem_iterator_close
};

/* }}} vy_mem_iterator API implementation */

/* {{{ Iterator over transaction writes : forward declaration */

struct vy_txw_iterator {
	/** Parent class, must be the first member */
	struct vy_tuple_iterator base;

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

static void
vy_txw_iterator_open(struct vy_txw_iterator *itr,
		     struct vy_index *index, struct vy_tx *tx,
		     enum vy_order order, char *key);

static void
vy_txw_iterator_close(struct vy_tuple_iterator *vitr);

/* }}} Iterator over transaction writes : forward declaration */

/* {{{ Iterator over transaction writes : implementation */

/** Vtable for vy_tuple_iterator - declared below */
static struct vy_tuple_iterator_iface vy_txw_iterator_iface;

/* Open the iterator */
static void
vy_txw_iterator_open(struct vy_txw_iterator *itr,
		     struct vy_index *index, struct vy_tx *tx,
		     enum vy_order order, char *key)
{
	itr->base.iface = &vy_txw_iterator_iface;

	itr->index = index;
	itr->tx = tx;

	itr->order = order;
	itr->key = key;

	itr->version = UINT32_MAX;
	itr->curr_txv = NULL;
	itr->search_started = false;
}

/**
 * Find position in write set of transaction. Used once in first call of
 *  get/next
 * return 0: OK
 * return 1: not found
 */
static int
vy_txw_iterator_start(struct vy_txw_iterator *itr)
{
	itr->search_started = true;
	itr->version = itr->tx->write_set_version;
	itr->curr_txv = NULL;
	struct write_set_key key = { itr->index, itr->key };
	struct txv *txv;
	if (vy_tuple_key_part(itr->key, 0) != NULL) {
		if (itr->order == VINYL_EQ)
			txv = write_set_search(&itr->tx->write_set, &key);
		else if (itr->order == VINYL_GE || itr->order == VINYL_GT)
			txv = write_set_nsearch(&itr->tx->write_set, &key);
		else
			txv = write_set_psearch(&itr->tx->write_set, &key);
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

/**
 * Gut current tupple
 * return 0 : OK
 * return 1 : no more data
 */
static int
vy_txw_iterator_get(struct vy_tuple_iterator *vitr, struct vy_tuple **result)
{
	assert(vitr->iface->get == vy_txw_iterator_get);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;

	if (!itr->search_started && vy_txw_iterator_start(itr) != 0)
		return 1;
	if (itr->curr_txv == NULL)
		return 1;
	*result = itr->curr_txv->tuple;
	return 0;
}

/**
 * Move to next tuple
 * return 0 : OK
 * return 1 : no more data
 */
static int
vy_txw_iterator_next_key(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_key == vy_txw_iterator_next_key);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;

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

/**
 * Function for compatibility with run/mem iterators.
 * return 1 : no more data
 */
static int
vy_txw_iterator_next_lsn(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->next_lsn == vy_txw_iterator_next_lsn);
	(void)vitr;
	return 1;
}

/**
 * Restore iterator position after some changes in write set. Iterator
 *  position is placed to the next position after last_tuple
 * Can restore iterator that was out of data previously
 * return 0 : nothing significant was happend and itr position left the same
 * return 1 : iterator restored and position changed
 */
static int
vy_txw_iterator_restore(struct vy_tuple_iterator *vitr,
			struct vy_tuple *last_tuple)
{
	assert(vitr->iface->restore == vy_txw_iterator_restore);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;

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

/**
 * Close the iterator
 */
static void
vy_txw_iterator_close(struct vy_tuple_iterator *vitr)
{
	assert(vitr->iface->close == vy_txw_iterator_close);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;
	TRASH(itr);
}

static struct vy_tuple_iterator_iface vy_txw_iterator_iface = {
	.get = vy_txw_iterator_get,
	.next_key = vy_txw_iterator_next_key,
	.next_lsn = vy_txw_iterator_next_lsn,
	.restore = vy_txw_iterator_restore,
	.close = vy_txw_iterator_close
};

/* }}} Iterator over transaction writes : implementation */

/* {{{ Merge iterator */

/**
 * Merge source, support structure for vy_merge_iterator
 * Contains source iterator, additional properties and merge state
 */
struct vy_merge_src {
	/** Source iterator */
	union {
		struct vy_run_iterator run_iterator;
		struct vy_mem_iterator mem_iterator;
		struct vy_txw_iterator txw_iterator;
		struct vy_tuple_iterator iterator;
	};
	/** Source can change during merge iteration */
	bool is_mutable;
	/** Source belongs to a range (@sa vy_merge_iterator comments). */
	bool belong_range;
	/**
	 * All sources with the same front_id as in struct
	 * vy_merge_iterator are on the same key of current output
	 * tuple (optimization)
	 */
	uint32_t front_id;
};

/**
 * Merge iterator takes several iterators as a source and sorts
 * output from them by given order and LSN DESC. Nothing is
 * skipped, just sorted.  There are several optimizations, that
 * requires:
 * 1) all sources are given with increasing age.
 * 2) mutable sources are given before read-blocking sources.
 * The iterator designed for read iteration over write_set of
 * current transaction (that does not belong to any range but to
 * entire index) and mems and runs of some range. For this purpose
 * the iterator has special flag (range_ended) that signals to
 * read iterator that it must switch to the next range.
 */
struct vy_merge_iterator {
	/** Array of sources */
	struct vy_merge_src *src;
	/** Number of elements in the src array */
	uint32_t src_count;
	/** Number of elements allocated in the src array */
	uint32_t src_capacity;
	/** Current source offset that merge iterator is positioned on */
	uint32_t curr_src;
	/** Offset of the first source with is_mutable == true */
	uint32_t mutable_start;
	/** Next offset after the last source with is_mutable == true */
	uint32_t mutable_end;
	/** key_def for comparators */
	struct key_def *key_def;
	/** key to compare with */
	char *key;
	/** Order of iteration */
	enum vy_order order;
	/** Current tuple that merge iterator is positioned on */
	struct vy_tuple *curr_tuple;
	/**
	 * All sources with this front_id are on the same key of
	 * current iteration (optimization)
	 */
	uint32_t front_id;
	/**
	 * If index is unique and full key is given we can
	 * optimize first search in order to avoid unnecessary
	 * reading from disk.  That flag is set to true during
	 * initialization if index is unique and  full key is
	 * given. After first _get or _next_key call is set to
	 * false
	 */
	bool unique_optimization;
	/**
	 * After first search with unique_optimization we must do some extra
	 * moves and optimizations for _next_lsn call. So that flag is set to
	 * true after first search and will set to false after consequent
	 * _next_key call */
	bool is_in_uniq_opt;
	/**
	 * This flag is set to false during initialization and
	 * means that we must do lazy search for first _get or
	 * _next call. After that is set to false
	 */
	bool search_started;
	/**
	 * If all sources marked with belong_range = true comes to
	 * the end of data this flag is automatically set to true;
	 * is false otherwise.  For read iterator range_ended = true
	 * means that it must switch to next range
	 */
	bool range_ended;
};

/**
 * Open the iterator
 */
static void
vy_merge_iterator_open(struct vy_merge_iterator *itr, struct key_def *key_def,
		       enum vy_order order, char *key)
{
	assert(key != NULL);
	itr->key_def = key_def;
	itr->key = key;
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
	itr->unique_optimization =
		(order == VINYL_EQ || order == VINYL_GE || order == VINYL_LE) &&
		vy_tuple_key_is_full(key, key_def);
	itr->is_in_uniq_opt = false;
	itr->search_started = false;
	itr->range_ended = false;
}

/**
 * Close the iteator and free resources
 */
static void
vy_merge_iterator_close(struct vy_merge_iterator *itr)
{
	if (itr->curr_tuple != NULL) {
		vy_tuple_unref(itr->curr_tuple);
		itr->curr_tuple = NULL;
	}
	for (size_t i = 0; i < itr->src_count; i++)
		itr->src[i].iterator.iface->close(&itr->src[i].iterator);
	free(itr->src);
	itr->src_count = 0;
	itr->src_capacity = 0;
	itr->src = NULL;
}

/**
 * Extend internal source array capacity to fit capacity sources.
 * Not necessary to call is but calling it allows to optimize internal memory
 * allocation
 */
static int
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

/**
 * Add another source to merge iterator. Must be called before actual
 * iteration start and must not be called after.
 * See necessary order of adding requirements in struct vy_merge_iterator
 * comments.
 * The resulting vy_tuple_iterator must be properly initialized before merge
 * iteration start.
 * param is_mutable - Source can change during merge iteration
 * param belong_range - Source belongs to a range (see vy_merge_iterator comments)
 */
static struct vy_merge_src *
vy_merge_iterator_add(struct vy_merge_iterator *itr,
		      bool is_mutable, bool belong_range)
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
	src->belong_range = belong_range;
	return src;
}

/**
 * Move all source iterating positioned to equal to current tuple (previous
 * result of get) to the next position
 * return 0 : OK
 * return -1 : read error
 */
static int
vy_merge_iterator_propagate(struct vy_merge_iterator *itr)
{
	int rc;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		if (itr->src[i].front_id != itr->front_id)
			continue;
		rc = itr->src[i].iterator.iface->next_key(&itr->src[i].iterator);
		if (rc < 0)
			return rc;
	}
	itr->front_id++;
	return 0;
}

/**
 * Same as vy_merge_iterator_locate but optimized for first get in unique
 * index with will key given. See vy_merge_iterator::unique_optimization
 * member comment
 */
static int
vy_merge_iterator_locate_uniq_opt(struct vy_merge_iterator *itr)
{
	assert(itr->src_count);
	itr->range_ended = false;
	itr->search_started = true;
	itr->unique_optimization = false;
	struct vy_tuple *min_tuple;
	int order = (itr->order == VINYL_LE || itr->order == VINYL_LT ?
		     -1 : 1);
restart:
	itr->is_in_uniq_opt = false;
	min_tuple = NULL;
	itr->curr_src = UINT32_MAX;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		struct vy_tuple_iterator *sub_itr = &itr->src[i].iterator;
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
			  order * vy_tuple_compare(t->data, min_tuple->data,
						   itr->key_def);
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
		struct vy_tuple_iterator *sub_itr = &itr->src[i].iterator;
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

/**
 * Find minimal tuple from all the sources, mark all sources with tuple equal
 * to the minimum with specific front_id equal to itr->front_id.
 * Guaranteed that all other sources will have different front_id.
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
static int
vy_merge_iterator_locate(struct vy_merge_iterator *itr)
{
	if (itr->src_count == 0)
		return 1;
	if (itr->unique_optimization)
		return vy_merge_iterator_locate_uniq_opt(itr);
	itr->search_started = true;
	struct vy_tuple *min_tuple = NULL;
	itr->curr_src = UINT32_MAX;
	itr->range_ended = true;
	int order = (itr->order == VINYL_LE || itr->order == VINYL_LT ?
		     -1 : 1);
	for (uint32_t i = itr->src_count; i--;) {
		int rc;
		struct vy_tuple_iterator *sub_itr = &itr->src[i].iterator;
		if (itr->src[i].is_mutable) {
			rc = sub_itr->iface->restore(sub_itr, itr->curr_tuple);
			if (rc < 0)
				return rc;
		}
		struct vy_tuple *t;
		rc = sub_itr->iface->get(sub_itr, &t);
		if (rc < 0)
			return rc;
		if (rc > 0)
			continue;
		itr->range_ended = itr->range_ended && !itr->src[i].belong_range;
		int cmp = min_tuple == NULL ? -1 :
			order * vy_tuple_compare(t->data, min_tuple->data, itr->key_def);
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

/**
 * Get current tuple
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
static int
vy_merge_iterator_get(struct vy_merge_iterator *itr, struct vy_tuple **result)
{
	if (!itr->search_started) {
		int rc = vy_merge_iterator_locate(itr);
		if (rc < 0)
			return rc;
	}
	*result = itr->curr_tuple;
	return itr->curr_tuple != NULL ? 0 : 1;
}

/**
 * Iterate to the next key
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
static int
vy_merge_iterator_next_key(struct vy_merge_iterator *itr)
{
	int rc;
	if (!itr->search_started) {
		rc = vy_merge_iterator_locate(itr);
		if (rc < 0)
			return rc;
	}
	if (itr->is_in_uniq_opt) {
		itr->is_in_uniq_opt = false;
		rc = vy_merge_iterator_locate(itr);
		if (rc < 0)
			return rc;
	}
	rc = vy_merge_iterator_propagate(itr);
	if (rc < 0)
		return rc;
	return vy_merge_iterator_locate(itr);
}

/**
 * Iterate to the next (elder) version of the same key
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
static int
vy_merge_iterator_next_lsn(struct vy_merge_iterator *itr)
{
	int rc;
	if (!itr->search_started) {
		rc = vy_merge_iterator_locate(itr);
		if (rc < 0)
			return rc;
	}
	if (itr->curr_src == UINT32_MAX)
		return 1;
	struct vy_tuple_iterator *sub_itr = &itr->src[itr->curr_src].iterator;
	rc = sub_itr->iface->next_lsn(sub_itr);
	if (rc < 0) {
		return rc;
	} else if (rc == 0) {
		if (itr->curr_tuple != NULL)
			vy_tuple_unref(itr->curr_tuple);
		rc = sub_itr->iface->get(sub_itr, &itr->curr_tuple);
		if (rc < 0)
			return rc;
		assert(rc == 0);
		vy_tuple_ref(itr->curr_tuple);
		return rc;
	}
	for (uint32_t i = itr->curr_src + 1; i < itr->src_count; i++) {
		if (itr->is_in_uniq_opt) {
			sub_itr = &itr->src[i].iterator;
			struct vy_tuple *t;
			rc = sub_itr->iface->get(sub_itr, &t);
			if (rc < 0)
				return rc;
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
			sub_itr = &itr->src[i].iterator;
			itr->curr_src = i;
			if (itr->curr_tuple != NULL) {
				vy_tuple_unref(itr->curr_tuple);
				itr->curr_tuple = NULL;
			}
			rc = sub_itr->iface->get(sub_itr, &itr->curr_tuple);
			if (rc < 0)
				return rc;
			vy_tuple_ref(itr->curr_tuple);
			return 0;
		}
	}
	itr->is_in_uniq_opt = false;
	return 1;
}

/**
 * Restore position of merge iterator after given tuple according to order
 */
static int
vy_merge_iterator_restore(struct vy_merge_iterator *itr,
			  struct vy_tuple *last_tuple)
{
	int result = 0;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		struct vy_tuple_iterator *sub_itr = &itr->src[i].iterator;
		int rc = sub_itr->iface->restore(sub_itr, last_tuple);
		if (rc < 0)
			return rc;
		result = result || rc;
	}
	return result;
}

/* }}} Merge iterator */

/* {{{ Write iterator */

/**
 * The write iterator merges multiple tuple sources into one,
 * squashing multiple upserts on the same key and filtering out
 * replaces older than purge lsn.
 */
struct vy_write_iterator {
	struct vy_index *index;
	/*
	 * If the current tuple LSN is bigger than purge_lsn then
	 * it could be visible to an active transaction and must
	 * be preserved in the output. Otherwise it can be dropped
	 * if a newer version of the tuple exists.
	 * @sa vy_write_iterator_next
	 */
	int64_t purge_lsn;
	/*
	 * Users of a write iterator can specify if they need
	 * to preserve deletes. This is necessary, for example,
	 * when dumping a run, to ensure that the deleted tuple
	 * is eventually removed from all LSM layers.
	 * If save_delete is true then old deletes, i.e. those
	 * which  LSN is less or equal to purge LSN and for which
	 * there are no new replaces, are preserved in the output,
	 * otherwise such deletes are skipped and iteration
	 * continues from the next key.
	 */
	bool save_delete;
	bool goto_next_key;
	struct vy_tuple *key;
	struct vy_tuple *curr_tuple;
	struct vy_tuple *upsert_tuple;
	struct vy_merge_iterator mi;
};

/*
 * Open an empty write iterator. To add sources to the iterator
 * use vy_write_iterator_add_* functions
 */
static void
vy_write_iterator_open(struct vy_write_iterator *wi, bool save_delete,
		       struct vy_index *index, int64_t purge_lsn)
{
	wi->index = index;
	wi->purge_lsn = purge_lsn;
	wi->save_delete = save_delete;
	wi->curr_tuple = NULL;
	wi->goto_next_key = false;
	wi->key = vy_tuple_from_key(index, NULL, 0);
	vy_merge_iterator_open(&wi->mi, index->key_def, VINYL_GE,
			       wi->key->data);
}

static struct vy_write_iterator *
vy_write_iterator_new(bool save_delete, struct vy_index *index,
		      int64_t purge_lsn)
{
	struct vy_write_iterator *wi = calloc(1, sizeof(*wi));
	if (wi == NULL) {
		diag_set(OutOfMemory, sizeof(*wi), "calloc", "wi");
		return NULL;
	}
	vy_write_iterator_open(wi, save_delete, index, purge_lsn);
	return wi;
}

static int
vy_write_iterator_add_run(struct vy_write_iterator *wi, struct vy_run *run,
			  int fd, bool is_mutable, bool control_eof)
{
	struct vy_merge_src *src =
		vy_merge_iterator_add(&wi->mi, is_mutable, control_eof);
	if (src == NULL)
		return -1;
	vy_run_iterator_open(&src->run_iterator, wi->index, run, fd,
				   VINYL_GE, wi->key->data, INT64_MAX);
	return 0;
}

static int
vy_write_iterator_add_mem(struct vy_write_iterator *wi, struct vy_mem *mem,
			  bool is_mutable, bool control_eof)
{
	struct vy_merge_src *src =
		vy_merge_iterator_add(&wi->mi, is_mutable, control_eof);
	if (src == NULL)
		return -1;
	vy_mem_iterator_open(&src->mem_iterator, mem, VINYL_GE, wi->key->data,
			     INT64_MAX);
	return 0;
}

/**
 * The write iterator can return multiple LSNs for the same
 * key, thus next() will automatically switch to the next
 * key when it's appropriate.
 *
 * The user of the write iterator simply expects a stream
 * of tuples to write to the output.
 */
static int
vy_write_iterator_next(struct vy_write_iterator *wi)
{
	/*
	 * Nullify the result tuple. If the next tuple is not
	 * found, make sure we "forget" the current one.
	 */
	if (wi->curr_tuple)
		vy_tuple_unref(wi->curr_tuple);
	wi->curr_tuple = NULL;
	/*
	 * The upsert tuple can be hanging around from the previous
	 * invocation of next().
	 */
	if (wi->upsert_tuple)
		vy_tuple_unref(wi->upsert_tuple);
	wi->upsert_tuple = NULL;
	struct vy_merge_iterator *mi = &wi->mi;
	/*
	 * rc values mean the following:
	 *  - rc < 0 - an error.
	 *  - rc = 0 - all is ok.
	 *  - rc > 0 - iteration over the current key or LSN is finished.
	 */
	int rc;
	struct vy_tuple *tuple;
	if (!mi->search_started) {
		/* Start the search if it's the first iteration. */
		rc = vy_merge_iterator_locate(mi);
	} else {
		if (wi->goto_next_key) {
			rc = vy_merge_iterator_next_key(mi);
			wi->goto_next_key = false;
		} else {
			rc = vy_merge_iterator_next_lsn(mi);
		}
	}
	if (rc < 0)
		return rc;

	/*
	 * This cycle works as follows:
	 *  - iterate over all tuples with the same key.
	 *    The merge iterator returns versions of the same
	 *    tuple in newest-to-oldest order.
	 *    If the current tuple is newer than purge_lsn then
	 *    there is a transaction that may be using it and the
	 *    tuple is returned as is.
	 *    Otherwise, we can pick the first tuple from the
	 *    "top" of the version stack: it will reflect the
	 *    latest value we need to preserve. Special
	 *    care needs to be taken about upserts and deletes:
	 *    upserts squashed together and deletes preserved
	 *    if save_delete flag is true.
	 *  - when done with this key, proceed with the next one.
	 */
	for (; rc >= 0; rc = vy_merge_iterator_next_lsn(&wi->mi)) {

		if (wi->goto_next_key) {
			rc = vy_merge_iterator_next_key(mi);
			if (rc < 0)
				break; /* error */
			if (rc > 0)
				return 1; /* not found */
			wi->goto_next_key = false;
		}
		/*
		 * If we reached the end of the key LSNs then go to
		 * the next key. If the end of all keys is reached
		 * then it's the end of the iterator.
		 */
		if (rc > 0) {
			if (wi->upsert_tuple) {
				tuple = wi->upsert_tuple;
				rc = 0;
				break;
			}
			rc = vy_merge_iterator_next_key(mi);
			if (rc < 0)
				break; /* error */
			if (rc > 0)
				return 1; /* not found */
		}
		rc = vy_merge_iterator_get(mi, &tuple);
		if (rc < 0)
			break; /* error */
		if (rc > 0)
			return 1; /* not found */
		if (tuple->lsn > wi->purge_lsn) {
			/* Save the current tuple as the result. */
			break;
		}
		/* The merge iterator now is below the purge  LSN. */
		if (tuple->flags & SVREPLACE) {
			/* Replace. */
			wi->goto_next_key = true;
			if (wi->upsert_tuple) {
				/*
				 * If the previous tuple was upserted
				 * then combine it with the replace and return.
				 */
				tuple = vy_apply_upsert(wi->upsert_tuple,
							tuple, wi->index,
							false);
			}
			break;
		} else if (tuple->flags & SVUPSERT) {
			/*
			 * If the merge iterator now is below the
			 * purge LSN then upserts can be merged
			 * and all operations older than replace
			 * on the same key can be skipped.
			 */
			if (wi->upsert_tuple) {
				/*
				 * If it isn't the first upsert
				 * then squash the two of them
				 * into one.
				 */
				tuple = vy_apply_upsert(wi->upsert_tuple,
							tuple, wi->index,
							false);
				vy_tuple_unref(wi->upsert_tuple);
			} else {
				vy_tuple_ref(tuple);
			}
			wi->upsert_tuple = tuple;
		} else if (tuple->flags & SVDELETE) {
			/*
			 * The tuple on top of the stack is
			 * delete. We can disregard the rest of
			 * the stack.
			 */
			wi->goto_next_key = true;
			if (wi->save_delete) {
				/*
				 * Preserve the delete in output
				 * if it's a dump of a run, so it
				 * can annihilate an older version
				 * of this tuple when multiple
				 * runs are merged together.
				 */
				if (wi->upsert_tuple) {
					/*
					 * If DELETE was followed
					 * by UPSERT, convert
					 * UPSERT to REPLACE at
					 * once, and return it
					 * instead of DELETE.
					 */
					tuple = vy_apply_upsert(wi->upsert_tuple,
								tuple, wi->index,
								false);
				}
				break;
			}
		} else {
			unreachable();
		}
	}
	assert(rc == 0);
	vy_tuple_ref(tuple);
	wi->curr_tuple = tuple;
	return 0;
}

static int
vy_write_iterator_get(struct vy_write_iterator *wi, struct vy_tuple **result)
{
	if (wi->curr_tuple == NULL && vy_write_iterator_next(wi))
		return 1;
	if (result)
		*result = wi->curr_tuple;
	return 0;
}

static void
vy_write_iterator_close(struct vy_write_iterator *wi)
{
	if (wi->upsert_tuple) {
		vy_tuple_unref(wi->upsert_tuple);
	}
	wi->curr_tuple = NULL;
	wi->upsert_tuple = NULL;
	vy_merge_iterator_close(&wi->mi);
}

static void
vy_write_iterator_delete(struct vy_write_iterator *wi)
{
	vy_write_iterator_close(wi);
	vy_tuple_unref(wi->key);
	free(wi);
}

/* Write iterator }}} */

/* {{{ Iterator over index */

/**
 * Complex read iterator over vinyl index and write_set of current tx
 * Iterates over ranges, creates merge iterator for every range and outputs
 * the result.
 * Can also wor without transaction, just set tx = NULL in _open
 * Applyes upserts and skips deletes, so only one replace tuple for every key
 * is output
 */
struct vy_read_iterator {
	/* index to iterate over */
	struct vy_index *index;
	/* transaction to iterate over */
	struct vy_tx *tx;
	bool only_disk;

	/* search options */
	enum vy_order order;
	char *key;
	int64_t vlsn;

	/* iterator over ranges */
	struct vy_range_iterator range_iterator;
	/* current range */
	struct vy_range *curr_range;
	/* merge iterator over current range */
	struct vy_merge_iterator merge_iterator;

	struct vy_tuple *curr_tuple;

	/* copy of index->range_index_version to track range tree changes */
	uint32_t range_index_version;
	/* copy of curr_range->version to track mem/run lists changes */
	uint32_t range_version;
};

/**
 * Open the iterator
 */
void
vy_read_iterator_open(struct vy_read_iterator *itr,
		      struct vy_index *index, struct vy_tx *tx,
		      enum vy_order order, char *key, int64_t vlsn,
		      bool only_disk);

/**
 * Get current tuple
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
int
vy_read_iterator_get(struct vy_read_iterator *itr, struct vy_tuple **result);

/**
 * Goto next tuple
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
int
vy_read_iterator_next(struct vy_read_iterator *itr);

/**
 * Close the iterator and free resources
 */
void
vy_read_iterator_close(struct vy_read_iterator *itr);

static void
vy_read_iterator_add_tx(struct vy_read_iterator *itr)
{
	assert(itr->tx != NULL);
	struct vy_merge_src *sub_src =
		vy_merge_iterator_add(&itr->merge_iterator, true, false);
	vy_txw_iterator_open(&sub_src->txw_iterator, itr->index, itr->tx,
			     itr->order, itr->key);
	sub_src->iterator.iface->restore(&sub_src->iterator, itr->curr_tuple);
}

static void
vy_read_iterator_add_mem(struct vy_read_iterator *itr)
{
	assert(itr->curr_range != NULL);
	for (struct vy_mem *mem = itr->curr_range->mem; mem; mem = mem->next) {
		/* only the newest range is mutable */
		bool is_mutable = (mem == itr->curr_range->mem);
		struct vy_merge_src *sub_src = vy_merge_iterator_add(
			&itr->merge_iterator, is_mutable, true);
		vy_mem_iterator_open(&sub_src->mem_iterator, mem, itr->order,
				     itr->key, itr->vlsn);
	}
}

static void
vy_read_iterator_add_disk(struct vy_read_iterator *itr)
{
	assert(itr->curr_range != NULL);
	for (struct vy_run *run = itr->curr_range->run;
	     run != NULL; run = run->next) {
		struct vy_merge_src *sub_src = vy_merge_iterator_add(
			&itr->merge_iterator, false, true);
		vy_run_iterator_open(&sub_src->run_iterator, itr->index, run,
				     itr->curr_range->fd,
				     itr->order, itr->key, itr->vlsn);
	}
}

/**
 * Set up merge iterator for current range
 */
void
vy_read_iterator_use_range(struct vy_read_iterator *itr)
{
	if (!itr->only_disk && itr->tx != NULL)
		vy_read_iterator_add_tx(itr);

	if (itr->curr_range == NULL)
		return;

	itr->range_version = itr->curr_range->range_version;
	if (!itr->only_disk)
		vy_read_iterator_add_mem(itr);

	vy_read_iterator_add_disk(itr);
}

/**
 * Open the iterator
 */
void
vy_read_iterator_open(struct vy_read_iterator *itr,
		      struct vy_index *index, struct vy_tx *tx,
		      enum vy_order order, char *key, int64_t vlsn,
		      bool only_disk)
{
	itr->index = index;
	itr->tx = tx;
	itr->order = order;
	itr->key = key;
	itr->vlsn = vlsn;
	itr->only_disk = only_disk;

	itr->curr_tuple = NULL;
	vy_range_iterator_open(&itr->range_iterator, index,
			  order == VINYL_EQ ? VINYL_GE : order, key, 0);
	itr->curr_range = vy_range_iterator_get(&itr->range_iterator);
	vy_merge_iterator_open(&itr->merge_iterator, index->key_def, order, key);
	vy_read_iterator_use_range(itr);
	itr->range_index_version = index->range_index_version;
}

/**
 * Goto next range according to order
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
int
vy_read_iterator_next_range(struct vy_read_iterator *itr)
{
	assert(itr->curr_range != NULL);
	vy_merge_iterator_close(&itr->merge_iterator);
	vy_merge_iterator_open(&itr->merge_iterator, itr->index->key_def,
			       itr->order, itr->key);
	vy_range_iterator_next(&itr->range_iterator);
	itr->curr_range = vy_range_iterator_get(&itr->range_iterator);
	if (itr->curr_range != NULL && itr->order == VINYL_EQ) {
		struct vy_page_info *min = vy_run_page(itr->curr_range->run, 0);
		const char *min_key_data = vy_run_min_key(itr->curr_range->run,
							  min);
		if (vy_tuple_compare(min_key_data, itr->key,
				     itr->index->key_def) > 0)
			itr->curr_range = NULL;
	}
	vy_read_iterator_use_range(itr);
	struct vy_tuple *tuple = NULL;
	int rc = vy_merge_iterator_get(&itr->merge_iterator, &tuple);
	if (rc < 0)
		return rc;
	assert(rc >= 0);
	if (itr->merge_iterator.range_ended && itr->curr_range != NULL)
		return vy_read_iterator_next_range(itr);
	if (itr->curr_tuple != NULL)
		vy_tuple_unref(itr->curr_tuple);
	itr->curr_tuple = tuple;
	if (itr->curr_tuple != NULL) {
		vy_tuple_ref(itr->curr_tuple);
	}
	return rc;
}

/**
 * Check versions of index and current range and restores position if
 * something was changed
 */
void
vy_read_iterator_check_versions(struct vy_read_iterator *itr)
{
	if (itr->range_index_version != itr->index->range_index_version) {
		itr->range_index_version = itr->index->range_index_version;
		char *key = itr->curr_tuple != 0 ?
			    itr->curr_tuple->data : itr->key;
		enum vy_order order =
			itr->order == VINYL_EQ ? VINYL_GE : itr->order;
		vy_range_iterator_open(&itr->range_iterator,
				       itr->index, order, key, 0);
		struct vy_range *range = vy_range_iterator_get(&itr->range_iterator);
		if (range != itr->curr_range) {
			itr->range_version =
				range == NULL ? 1 : range->range_version + 1;
			itr->curr_range = range;
		}
	}
	uint32_t range_version =
		itr->curr_range == NULL ? 0 : itr->curr_range->range_version;
	if (itr->range_version != range_version) {
		itr->range_version = range_version;
		vy_merge_iterator_close(&itr->merge_iterator);
		vy_merge_iterator_open(&itr->merge_iterator, itr->index->key_def,
				       itr->order, itr->key);
		vy_read_iterator_use_range(itr);
		vy_merge_iterator_restore(&itr->merge_iterator, itr->curr_tuple);
	}
}

/**
 * Goto next tuple
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
int
vy_read_iterator_next(struct vy_read_iterator *itr)
{
	vy_read_iterator_check_versions(itr);
	int rc = vy_merge_iterator_next_key(&itr->merge_iterator);
	if (rc < 0)
		return rc;
	if (itr->merge_iterator.range_ended && itr->curr_range != NULL)
		rc = vy_read_iterator_next_range(itr);
	return rc;
}

/**
 * Get current tuple
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
int
vy_read_iterator_get(struct vy_read_iterator *itr, struct vy_tuple **result)
{
	vy_read_iterator_check_versions(itr);
	while (true) {
		struct vy_tuple *t;
		int rc = vy_merge_iterator_get(&itr->merge_iterator, &t);
		if (rc >= 0 && itr->merge_iterator.range_ended && itr->curr_range != NULL) {
			rc = vy_read_iterator_next_range(itr);
			if (rc == 0) {
				rc = vy_merge_iterator_get(&itr->merge_iterator,
							   &t);
			}
		}
		if (rc < 0)
			return rc; /* error */
		if (rc == 1)
			return 1; /* no more data */
		assert(rc == 0);
		if (itr->curr_tuple != NULL)
			vy_tuple_unref(itr->curr_tuple);
		itr->curr_tuple = t;
		vy_tuple_ref(itr->curr_tuple);
		while (itr->curr_tuple->flags & SVUPSERT) {
			rc = vy_merge_iterator_next_lsn(&itr->merge_iterator);
			struct vy_tuple *next = NULL;
			if (rc == 0) {
				rc = vy_merge_iterator_get(&itr->merge_iterator,
							   &next);
			}
			if (rc < 0)
				return rc;
			struct vy_tuple *applied =
				vy_apply_upsert(itr->curr_tuple, next,
						itr->index, true);
			if (applied == NULL)
				return -1;
			vy_tuple_unref(itr->curr_tuple);
			itr->curr_tuple = applied;
		}
		if (rc != 0 || (itr->curr_tuple->flags & SVDELETE) == 0)
			break;
		rc = vy_read_iterator_next(itr);
		if (rc < 0)
			return rc;
		if (rc == 1)
			return 1; /* no more data */
		assert(rc == 0);
	}
	*result = itr->curr_tuple;
	return 0;
}

/**
 * Close the iterator and free resources
 */
void
vy_read_iterator_close(struct vy_read_iterator *itr)
{
	if (itr->curr_tuple != NULL)
		vy_tuple_unref(itr->curr_tuple);
	itr->curr_tuple = NULL;
	vy_merge_iterator_close(&itr->merge_iterator);
}

/* }}} Iterator over index */

/** {{{ Replication */

int
vy_index_send(struct vy_index *index, vy_send_row_f sendrow, void *ctx)
{
	int64_t vlsn = INT64_MAX;
	int rc = 0;

	struct vy_read_iterator ri;
	struct vy_tuple *tuple;
	struct vy_tuple *key = vy_tuple_from_key(index, NULL, 0);
	if (key == NULL)
		return -1;
	vy_read_iterator_open(&ri, index, NULL, VINYL_GT, key->data,
			      vlsn, true);
	for (; rc == 0; rc = vy_read_iterator_next(&ri)) {
		rc = vy_read_iterator_get(&ri, &tuple);
		if (rc)
			goto finish_send;
		uint32_t mp_size;
		const char *mp_data = vy_tuple_data(index, tuple,
						    &mp_size);
		int64_t lsn = tuple->lsn;
		rc = sendrow(ctx, mp_data, mp_size, lsn);
		if (rc)
			goto finish_send;
	}
finish_send:
	vy_read_iterator_close(&ri);
	vy_tuple_unref(key);
	return rc;
}

/* }}} replication */

int
vy_index_read(struct vy_index *index, struct vy_tuple *key,
	      enum vy_order order, struct vy_tuple **result, struct vy_tx *tx)
{
	struct vy_env *e = index->env;
	uint64_t start  = clock_monotonic64();

	int64_t vlsn = tx != NULL ? tx->vlsn : e->xm->lsn;

	struct vy_read_iterator itr;
	vy_read_iterator_open(&itr, index, tx, order, key->data, vlsn, false);
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
	vy_read_iterator_open(&itr, index, NULL, VINYL_EQ, tuple->data,
			      INT64_MAX, false);
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
