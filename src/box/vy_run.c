/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "vy_run.h"

#include <zstd.h>

#include "fiber.h"
#include "fiber_cond.h"
#include "fio.h"
#include "cbus.h"
#include "memory.h"
#include "coio_file.h"

#include "replication.h"
#include "tuple_bloom.h"
#include "xlog.h"
#include "xrow.h"
#include "vy_history.h"

static const uint64_t vy_page_info_key_map = (1 << VY_PAGE_INFO_OFFSET) |
					     (1 << VY_PAGE_INFO_SIZE) |
					     (1 << VY_PAGE_INFO_UNPACKED_SIZE) |
					     (1 << VY_PAGE_INFO_ROW_COUNT) |
					     (1 << VY_PAGE_INFO_MIN_KEY) |
					     (1 << VY_PAGE_INFO_ROW_INDEX_OFFSET);

static const uint64_t vy_run_info_key_map = (1 << VY_RUN_INFO_MIN_KEY) |
					    (1 << VY_RUN_INFO_MAX_KEY) |
					    (1 << VY_RUN_INFO_MIN_LSN) |
					    (1 << VY_RUN_INFO_MAX_LSN) |
					    (1 << VY_RUN_INFO_PAGE_COUNT);

/** xlog meta type for .run files */
#define XLOG_META_TYPE_RUN "RUN"

/** xlog meta type for .index files */
#define XLOG_META_TYPE_INDEX "INDEX"

const char *vy_file_suffix[] = {
	"index",			/* VY_FILE_INDEX */
	"index" inprogress_suffix, 	/* VY_FILE_INDEX_INPROGRESS */
	"run",				/* VY_FILE_RUN */
	"run" inprogress_suffix, 	/* VY_FILE_RUN_INPROGRESS */
};

/* sync run and index files very 16 MB */
#define VY_RUN_SYNC_INTERVAL (1 << 24)

/**
 * We read runs in background threads so as not to stall tx.
 * This structure represents such a thread.
 */
struct vy_run_reader {
	/** Thread that processes read requests. */
	struct cord cord;
	/** Pipe from tx to the reader thread. */
	struct cpipe reader_pipe;
	/** Pipe from the reader thread to tx. */
	struct cpipe tx_pipe;
};

/** Cbus task for vinyl page read. */
struct vy_page_read_task {
	/** parent */
	struct cbus_call_msg base;
	/** vinyl page metadata */
	struct vy_page_info *page_info;
	/** vy_run with fd - ref. counted */
	struct vy_run *run;
	/** key to lookup within the page */
	struct vy_entry key;
	/** iterator type (needed for for key lookup) */
	enum iterator_type iterator_type;
	/** key definition (needed for key lookup) */
	struct key_def *cmp_def;
	/** disk format (needed for key lookup) */
	struct tuple_format *format;
	/** [out] position of the key in the page */
	uint32_t pos_in_page;
	/** [out] true if key was found in the page */
	bool equal_found;
	/** [out] resulting vinyl page */
	struct vy_page *page;
};

/** Destructor for env->zdctx_key thread-local variable */
static void
vy_free_zdctx(void *arg)
{
	assert(arg != NULL);
	ZSTD_freeDStream(arg);
}

/** Run reader thread function. */
static int
vy_run_reader_f(va_list ap)
{
	struct vy_run_reader *reader = va_arg(ap, struct vy_run_reader *);
	struct cbus_endpoint endpoint;

	cpipe_create(&reader->tx_pipe, "tx_prio");
	cbus_endpoint_create(&endpoint, cord_name(cord()),
			     fiber_schedule_cb, fiber());
	cbus_loop(&endpoint);
	cbus_endpoint_destroy(&endpoint, cbus_process);
	cpipe_destroy(&reader->tx_pipe);
	return 0;
}

/** Start run reader threads. */
static void
vy_run_env_start_readers(struct vy_run_env *env)
{
	assert(env->reader_pool == NULL);
	assert(env->reader_pool_size > 0);

	env->reader_pool = calloc(env->reader_pool_size,
				  sizeof(*env->reader_pool));
	if (env->reader_pool == NULL)
		panic("failed to allocate vinyl reader thread pool");

	for (int i = 0; i < env->reader_pool_size; i++) {
		struct vy_run_reader *reader = &env->reader_pool[i];
		char name[FIBER_NAME_MAX];

		snprintf(name, sizeof(name), "vinyl.reader.%d", i);
		if (cord_costart(&reader->cord, name,
				 vy_run_reader_f, reader) != 0)
			panic("failed to start vinyl reader thread");
		cpipe_create(&reader->reader_pipe, name);
	}
	env->next_reader = 0;
}

/** Join run reader threads. */
static void
vy_run_env_stop_readers(struct vy_run_env *env)
{
	for (int i = 0; i < env->reader_pool_size; i++) {
		struct vy_run_reader *reader = &env->reader_pool[i];
		tt_pthread_cancel(reader->cord.id);
		tt_pthread_join(reader->cord.id, NULL);
	}
	free(env->reader_pool);
}

/**
 * Initialize vinyl run environment
 */
void
vy_run_env_create(struct vy_run_env *env, int read_threads)
{
	memset(env, 0, sizeof(*env));
	env->reader_pool_size = read_threads;
	tt_pthread_key_create(&env->zdctx_key, vy_free_zdctx);
	mempool_create(&env->read_task_pool, cord_slab_cache(),
		       sizeof(struct vy_page_read_task));
	env->initial_join = false;
}

/**
 * Destroy vinyl run environment
 */
void
vy_run_env_destroy(struct vy_run_env *env)
{
	if (env->reader_pool != NULL)
		vy_run_env_stop_readers(env);
	mempool_destroy(&env->read_task_pool);
	tt_pthread_key_delete(env->zdctx_key);
}

/**
 * Enable coio reads for a vinyl run environment.
 */
void
vy_run_env_enable_coio(struct vy_run_env *env)
{
	if (env->reader_pool != NULL)
		return; /* already enabled */
	vy_run_env_start_readers(env);
}

/**
 * Execute a task on behalf of a reader thread.
 */
static int
vy_run_env_coio_call(struct vy_run_env *env, struct cbus_call_msg *msg,
		     cbus_call_f func)
{
	/* Optimization: use blocking I/O during WAL recovery. */
	if (env->reader_pool == NULL)
		return func(msg);

	/* Pick a reader thread. */
	struct vy_run_reader *reader;
	reader = &env->reader_pool[env->next_reader++];
	env->next_reader %= env->reader_pool_size;

	/* Post the task to the reader thread. */
	bool cancellable = fiber_set_cancellable(false);
	int rc = cbus_call(&reader->reader_pipe, &reader->tx_pipe,
			   msg, func, NULL, TIMEOUT_INFINITY);
	fiber_set_cancellable(cancellable);
	if (rc != 0)
		return -1;

	if (fiber_is_cancelled()) {
		diag_set(FiberIsCancelled);
		return -1;
	}
	return 0;
}

/**
 * Initialize page info struct
 *
 * @retval 0 for Success
 * @retval -1 for error
 */
static int
vy_page_info_create(struct vy_page_info *page_info, uint64_t offset,
		    const char *min_key, struct key_def *cmp_def)
{
	memset(page_info, 0, sizeof(*page_info));
	page_info->offset = offset;
	page_info->unpacked_size = 0;
	page_info->min_key = vy_key_dup(min_key);
	if (page_info->min_key == NULL)
		return -1;
	uint32_t part_count = mp_decode_array(&min_key);
	page_info->min_key_hint = key_hint(min_key, part_count, cmp_def);
	return page_info->min_key == NULL ? -1 : 0;
}

/**
 * Destroy page info struct
 */
static void
vy_page_info_destroy(struct vy_page_info *page_info)
{
	if (page_info->min_key != NULL)
		free(page_info->min_key);
}

struct vy_run *
vy_run_new(struct vy_run_env *env, int64_t id)
{
	struct vy_run *run = calloc(1, sizeof(struct vy_run));
	if (unlikely(run == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_run), "malloc",
			 "struct vy_run");
		return NULL;
	}
	run->env = env;
	run->id = id;
	run->dump_lsn = -1;
	run->fd = -1;
	run->refs = 1;
	rlist_create(&run->in_lsm);
	rlist_create(&run->in_unused);
	return run;
}

static void
vy_run_clear(struct vy_run *run)
{
	if (run->page_info != NULL) {
		uint32_t page_no;
		for (page_no = 0; page_no < run->info.page_count; ++page_no)
			vy_page_info_destroy(run->page_info + page_no);
		free(run->page_info);
	}
	run->page_info = NULL;
	run->page_index_size = 0;
	run->info.page_count = 0;
	if (run->info.bloom != NULL) {
		tuple_bloom_delete(run->info.bloom);
		run->info.bloom = NULL;
	}
	free(run->info.min_key);
	run->info.min_key = NULL;
	free(run->info.max_key);
	run->info.max_key = NULL;
}

void
vy_run_delete(struct vy_run *run)
{
	assert(run->refs == 0);
	if (run->fd >= 0 && close(run->fd) < 0)
		say_syserror("close failed");
	vy_run_clear(run);
	TRASH(run);
	free(run);
}

size_t
vy_run_bloom_size(struct vy_run *run)
{
	return run->info.bloom == NULL ? 0 : tuple_bloom_size(run->info.bloom);
}

/**
 * Find a page from which the iteration of a given key must be started.
 * LE and LT: the found page definitely contains the position
 *  for iteration start.
 * GE, GT, EQ: Since page search uses only min_key of pages,
 *  it may happen that the found page doesn't contain the position
 *  for iteration start. In this case it is certain that the iteration
 *  must be started from the beginning of the next page.
 *
 * @param run - run
 * @param key - key to find
 * @param key_def - key_def for comparison
 * @param itype - iterator type (see above)
 * @param equal_key: *equal_key is set to true if there is a page
 *  with min_key equal to the given key.
 * @return offset of the page in page index OR run->info.page_count if
 *  there no pages fulfilling the conditions.
 */
static uint32_t
vy_page_index_find_page(struct vy_run *run, struct vy_entry key,
			struct key_def *cmp_def, enum iterator_type itype,
			bool *equal_key)
{
	if (itype == ITER_EQ)
		itype = ITER_GE; /* One day it'll become obsolete */
	assert(itype == ITER_GE || itype == ITER_GT ||
	       itype == ITER_LE || itype == ITER_LT);
	int dir = iterator_direction(itype);
	*equal_key = false;

	/**
	 * Binary search in page index. Depends on given iterator_type:
	 *  ITER_GE: lowest page with min_key >= given key.
	 *  ITER_GT: lowest page with min_key > given key.
	 *  ITER_LE: highest page with min_key <= given key.
	 *  ITER_LT: highest page with min_key < given key.
	 *
	 * Example: we are searching for a value 2 in the run of 10 pages:
	 * min_key:         [1   1   2   2   2   2   2   3   3   3]
	 * we want to find: [    LT  GE              LE  GT       ]
	 * For LT and GE it's a classical lower_bound search.
	 * Let's set up a range with left page's min_key < key and
	 *  right page's min >= key; binary cut the range until it
	 *  becomes of length 1 and then LT pos = left bound of the range
	 *  and GE pos = right bound of the range.
	 * For LE and GT it's a classical upper_bound search.
	 * Let's set up a range with left page's min_key <= key and
	 *  right page's min > key; binary cut the range until it
	 *  becomes of length 1 and then LE pos = left bound of the range
	 *  and GT pos = right bound of the range.
	 */
	bool is_lower_bound = itype == ITER_LT || itype == ITER_GE;

	assert(run->info.page_count > 0);
	/* Initially the range is set with virtual positions */
	int32_t range[2] = { -1, run->info.page_count };
	assert(run->info.page_count > 0);
	do {
		int32_t mid = range[0] + (range[1] - range[0]) / 2;
		struct vy_page_info *info = vy_run_page_info(run, mid);
		int cmp = vy_entry_compare_with_raw_key(key, info->min_key,
							info->min_key_hint,
							cmp_def);
		if (is_lower_bound)
			range[cmp <= 0] = mid;
		else
			range[cmp < 0] = mid;
		*equal_key = *equal_key || cmp == 0;
	} while (range[1] - range[0] > 1);
	if (range[0] < 0)
		range[0] = run->info.page_count;
	uint32_t page = range[dir > 0];

	/**
	 * Since page search uses only min_key of pages,
	 *  for GE, GT and EQ the previous page can contain
	 *  the point where iteration must be started.
	 */
	if (page > 0 && dir > 0)
		return page - 1;
	return page;
}

struct vy_slice *
vy_slice_new(int64_t id, struct vy_run *run, struct vy_entry begin,
	     struct vy_entry end, struct key_def *cmp_def)
{
	struct vy_slice *slice = malloc(sizeof(*slice));
	if (slice == NULL) {
		diag_set(OutOfMemory, sizeof(*slice),
			 "malloc", "struct vy_slice");
		return NULL;
	}
	memset(slice, 0, sizeof(*slice));
	slice->id = id;
	slice->run = run;
	slice->seed = rand();
	vy_run_ref(run);
	run->slice_count++;
	if (begin.stmt != NULL)
		tuple_ref(begin.stmt);
	slice->begin = begin;
	if (end.stmt != NULL)
		tuple_ref(end.stmt);
	slice->end = end;
	rlist_create(&slice->in_range);
	fiber_cond_create(&slice->pin_cond);
	if (run->info.page_count == 0) {
		/* The run is empty hence the slice is empty too. */
		return slice;
	}
	/** Lookup the first and the last pages spanned by the slice. */
	bool unused;
	if (slice->begin.stmt == NULL) {
		slice->first_page_no = 0;
	} else {
		slice->first_page_no =
			vy_page_index_find_page(run, slice->begin, cmp_def,
						ITER_GE, &unused);
		assert(slice->first_page_no < run->info.page_count);
	}
	if (slice->end.stmt == NULL) {
		slice->last_page_no = run->info.page_count - 1;
	} else {
		slice->last_page_no =
			vy_page_index_find_page(run, slice->end, cmp_def,
						ITER_LT, &unused);
		if (slice->last_page_no == run->info.page_count) {
			/* It's an empty slice */
			slice->first_page_no = 0;
			slice->last_page_no = 0;
			return slice;
		}
	}
	assert(slice->last_page_no >= slice->first_page_no);
	/** Estimate the number of statements in the slice. */
	uint32_t run_pages = run->info.page_count;
	uint32_t slice_pages = slice->last_page_no - slice->first_page_no + 1;
	slice->count.pages = slice_pages;
	slice->count.rows = DIV_ROUND_UP(run->count.rows *
					 slice_pages, run_pages);
	slice->count.bytes = DIV_ROUND_UP(run->count.bytes *
					  slice_pages, run_pages);
	slice->count.bytes_compressed = DIV_ROUND_UP(
		run->count.bytes_compressed * slice_pages, run_pages);
	return slice;
}

void
vy_slice_delete(struct vy_slice *slice)
{
	assert(slice->pin_count == 0);
	assert(slice->run->slice_count > 0);
	slice->run->slice_count--;
	vy_run_unref(slice->run);
	if (slice->begin.stmt != NULL)
		tuple_unref(slice->begin.stmt);
	if (slice->end.stmt != NULL)
		tuple_unref(slice->end.stmt);
	fiber_cond_destroy(&slice->pin_cond);
	TRASH(slice);
	free(slice);
}

int
vy_slice_cut(struct vy_slice *slice, int64_t id, struct vy_entry begin,
	     struct vy_entry end, struct key_def *cmp_def,
	     struct vy_slice **result)
{
	*result = NULL;

	if (begin.stmt != NULL && slice->end.stmt != NULL &&
	    vy_entry_compare(begin, slice->end, cmp_def) >= 0)
		return 0; /* no intersection: begin >= slice->end */

	if (end.stmt != NULL && slice->begin.stmt != NULL &&
	    vy_entry_compare(end, slice->begin, cmp_def) <= 0)
		return 0; /* no intersection: end <= slice->end */

	/* begin = MAX(begin, slice->begin) */
	if (slice->begin.stmt != NULL &&
	    (begin.stmt == NULL || vy_entry_compare(begin, slice->begin,
						    cmp_def) < 0))
		begin = slice->begin;

	/* end = MIN(end, slice->end) */
	if (slice->end.stmt != NULL &&
	    (end.stmt == NULL || vy_entry_compare(end, slice->end,
						  cmp_def) > 0))
		end = slice->end;

	*result = vy_slice_new(id, slice->run, begin, end, cmp_def);
	if (*result == NULL)
		return -1; /* OOM */

	return 0;
}

/**
 * Decode page information from xrow.
 *
 * @param[out] page Page information.
 * @param xrow      Xrow to decode.
 * @param cmp_def   Definition of keys stored in the page.
 * @param filename  Filename for error reporting.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
static int
vy_page_info_decode(struct vy_page_info *page, const struct xrow_header *xrow,
		    struct key_def *cmp_def, const char *filename)
{
	assert(xrow->type == VY_INDEX_PAGE_INFO);
	const char *pos = xrow->body->iov_base;
	memset(page, 0, sizeof(*page));
	uint64_t key_map = vy_page_info_key_map;
	uint32_t map_size = mp_decode_map(&pos);
	uint32_t map_item;
	const char *key_beg;
	uint32_t part_count;
	for (map_item = 0; map_item < map_size; ++map_item) {
		uint32_t key = mp_decode_uint(&pos);
		key_map &= ~(1ULL << key);
		switch (key) {
		case VY_PAGE_INFO_OFFSET:
			page->offset = mp_decode_uint(&pos);
			break;
		case VY_PAGE_INFO_SIZE:
			page->size = mp_decode_uint(&pos);
			break;
		case VY_PAGE_INFO_ROW_COUNT:
			page->row_count = mp_decode_uint(&pos);
			break;
		case VY_PAGE_INFO_MIN_KEY:
			key_beg = pos;
			mp_next(&pos);
			page->min_key = vy_key_dup(key_beg);
			if (page->min_key == NULL)
				return -1;
			part_count = mp_decode_array(&key_beg);
			page->min_key_hint = key_hint(key_beg, part_count,
						      cmp_def);
			break;
		case VY_PAGE_INFO_UNPACKED_SIZE:
			page->unpacked_size = mp_decode_uint(&pos);
			break;
		case VY_PAGE_INFO_ROW_INDEX_OFFSET:
			page->row_index_offset = mp_decode_uint(&pos);
			break;
		default:
			mp_next(&pos); /* unknown key, ignore */
			break;
		}
	}
	if (key_map) {
		enum vy_page_info_key key = bit_ctz_u64(key_map);
		diag_set(ClientError, ER_INVALID_INDEX_FILE, filename,
			 tt_sprintf("Can't decode page info: "
				    "missing mandatory key %s",
				    vy_page_info_key_name(key)));
		return -1;
	}

	return 0;
}

/** Decode statement statistics from @data and advance @data. */
static void
vy_stmt_stat_decode(struct vy_stmt_stat *stat, const char **data)
{
	uint32_t size = mp_decode_map(data);
	for (uint32_t i = 0; i < size; i++) {
		uint64_t key = mp_decode_uint(data);
		uint64_t value = mp_decode_uint(data);
		switch (key) {
		case IPROTO_INSERT:
			stat->inserts = value;
			break;
		case IPROTO_REPLACE:
			stat->replaces = value;
			break;
		case IPROTO_DELETE:
			stat->deletes = value;
			break;
		case IPROTO_UPSERT:
			stat->upserts = value;
			break;
		default:
			break;
		}
	}
}

/**
 * Decode the run metadata from xrow.
 *
 * @param xrow xrow to decode
 * @param[out] run_info the run information
 * @param filename File name for error reporting.
 *
 * @retval  0 success
 * @retval -1 error (check diag)
 */
int
vy_run_info_decode(struct vy_run_info *run_info,
		   const struct xrow_header *xrow,
		   const char *filename)
{
	assert(xrow->type == VY_INDEX_RUN_INFO);
	/* decode run */
	const char *pos = xrow->body->iov_base;
	memset(run_info, 0, sizeof(*run_info));
	uint64_t key_map = vy_run_info_key_map;
	uint32_t map_size = mp_decode_map(&pos);
	uint32_t map_item;
	const char *tmp;
	/* decode run values */
	for (map_item = 0; map_item < map_size; ++map_item) {
		uint32_t key = mp_decode_uint(&pos);
		key_map &= ~(1ULL << key);
		switch (key) {
		case VY_RUN_INFO_MIN_KEY:
			tmp = pos;
			mp_next(&pos);
			run_info->min_key = vy_key_dup(tmp);
			if (run_info->min_key == NULL)
				return -1;
			break;
		case VY_RUN_INFO_MAX_KEY:
			tmp = pos;
			mp_next(&pos);
			run_info->max_key = vy_key_dup(tmp);
			if (run_info->max_key == NULL)
				return -1;
			break;
		case VY_RUN_INFO_MIN_LSN:
			run_info->min_lsn = mp_decode_uint(&pos);
			break;
		case VY_RUN_INFO_MAX_LSN:
			run_info->max_lsn = mp_decode_uint(&pos);
			break;
		case VY_RUN_INFO_PAGE_COUNT:
			run_info->page_count = mp_decode_uint(&pos);
			break;
		case VY_RUN_INFO_BLOOM_LEGACY:
			run_info->bloom = tuple_bloom_decode_legacy(&pos);
			if (run_info->bloom == NULL)
				return -1;
			break;
		case VY_RUN_INFO_BLOOM:
			run_info->bloom = tuple_bloom_decode(&pos);
			if (run_info->bloom == NULL)
				return -1;
			break;
		case VY_RUN_INFO_STMT_STAT:
			vy_stmt_stat_decode(&run_info->stmt_stat, &pos);
			break;
		default:
			mp_next(&pos); /* unknown key, ignore */
			break;
		}
	}
	if (key_map) {
		enum vy_run_info_key key = bit_ctz_u64(key_map);
		diag_set(ClientError, ER_INVALID_INDEX_FILE, filename,
			 tt_sprintf("Can't decode run info: "
				    "missing mandatory key %s",
				    vy_run_info_key_name(key)));
		return -1;
	}
	return 0;
}

static struct vy_page *
vy_page_new(const struct vy_page_info *page_info)
{
	struct vy_page *page = malloc(sizeof(*page));
	if (page == NULL) {
		diag_set(OutOfMemory, sizeof(*page),
			 "load_page", "page cache");
		return NULL;
	}
	page->unpacked_size = page_info->unpacked_size;
	page->row_count = page_info->row_count;
	page->row_index = calloc(page_info->row_count, sizeof(uint32_t));
	if (page->row_index == NULL) {
		diag_set(OutOfMemory, page_info->row_count * sizeof(uint32_t),
			 "malloc", "page->row_index");
		free(page);
		return NULL;
	}

	page->data = (char *)malloc(page_info->unpacked_size);
	if (page->data == NULL) {
		diag_set(OutOfMemory, page_info->unpacked_size,
			 "malloc", "page->data");
		free(page->row_index);
		free(page);
		return NULL;
	}
	return page;
}

static void
vy_page_delete(struct vy_page *page)
{
	uint32_t *row_index = page->row_index;
	char *data = page->data;
#if !defined(NDEBUG)
	memset(row_index, '#', sizeof(uint32_t) * page->row_count);
	memset(data, '#', page->unpacked_size);
	memset(page, '#', sizeof(*page));
#endif /* !defined(NDEBUG) */
	free(row_index);
	free(data);
	free(page);
}

static int
vy_page_xrow(struct vy_page *page, uint32_t stmt_no,
	     struct xrow_header *xrow)
{
	assert(stmt_no < page->row_count);
	const char *data = page->data + page->row_index[stmt_no];
	const char *data_end = stmt_no + 1 < page->row_count ?
			       page->data + page->row_index[stmt_no + 1] :
			       page->data + page->unpacked_size;
	return xrow_header_decode(xrow, &data, data_end, false);
}

/* {{{ vy_run_iterator vy_run_iterator support functions */

/**
 * Read raw stmt data from the page
 * @param page          Page.
 * @param stmt_no       Statement position in the page.
 * @param cmp_def       Definition of keys stored in the page.
 * @param format        Format for REPLACE/DELETE tuples.
 *
 * @retval not NULL Statement read from page.
 * @retval     NULL Memory error.
 */
static struct vy_entry
vy_page_stmt(struct vy_page *page, uint32_t stmt_no,
	     struct key_def *cmp_def, struct tuple_format *format)
{
	struct xrow_header xrow;
	if (vy_page_xrow(page, stmt_no, &xrow) != 0)
		return vy_entry_none();
	struct vy_entry entry;
	entry.stmt = vy_stmt_decode(&xrow, format);
	if (entry.stmt == NULL)
		return vy_entry_none();
	entry.hint = vy_stmt_hint(entry.stmt, cmp_def);
	return entry;
}

/**
 * Binary search in page
 * In terms of STL, makes lower_bound for EQ,GE,LT and upper_bound for GT,LE
 * Additionally *equal_key argument is set to true if the found value is
 * equal to given key (set to false otherwise).
 * @retval position in the page
 */
static uint32_t
vy_page_find_key(struct vy_page *page, struct vy_entry key,
		 struct key_def *cmp_def, struct tuple_format *format,
		 enum iterator_type iterator_type, bool *equal_key)
{
	uint32_t beg = 0;
	uint32_t end = page->row_count;
	*equal_key = false;
	/* for upper bound we change zero comparison result to -1 */
	int zero_cmp = (iterator_type == ITER_GT ||
			iterator_type == ITER_LE ? -1 : 0);
	while (beg != end) {
		uint32_t mid = beg + (end - beg) / 2;
		struct vy_entry fnd_key = vy_page_stmt(page, mid, cmp_def,
						       format);
		if (fnd_key.stmt == NULL)
			return end;
		int cmp = vy_entry_compare(fnd_key, key, cmp_def);
		cmp = cmp ? cmp : zero_cmp;
		*equal_key = *equal_key || cmp == 0;
		if (cmp < 0)
			beg = mid + 1;
		else
			end = mid;
		tuple_unref(fnd_key.stmt);
	}
	return end;
}

/**
 * End iteration and free cached data.
 */
static void
vy_run_iterator_stop(struct vy_run_iterator *itr)
{
	if (itr->curr.stmt != NULL) {
		tuple_unref(itr->curr.stmt);
		itr->curr = vy_entry_none();
	}
	if (itr->curr_page != NULL) {
		vy_page_delete(itr->curr_page);
		if (itr->prev_page != NULL)
			vy_page_delete(itr->prev_page);
		itr->curr_page = itr->prev_page = NULL;
	}
}

static int
vy_row_index_decode(uint32_t *row_index, uint32_t row_count,
		    struct xrow_header *xrow)
{
	assert(xrow->type == VY_RUN_ROW_INDEX);
	const char *pos = xrow->body->iov_base;
	uint32_t map_size = mp_decode_map(&pos);
	uint32_t map_item;
	uint32_t size = 0;
	for (map_item = 0; map_item < map_size; ++map_item) {
		uint32_t key = mp_decode_uint(&pos);
		switch (key) {
		case VY_ROW_INDEX_DATA:
			size = mp_decode_binl(&pos);
			break;
		}
	}
	if (size != sizeof(uint32_t) * row_count) {
		diag_set(ClientError, ER_INVALID_RUN_FILE,
			 tt_sprintf("Wrong row index size "
				    "(expected %zu, got %u",
				    sizeof(uint32_t) * row_count,
				    (unsigned)size));
		return -1;
	}
	for (uint32_t i = 0; i < row_count; ++i) {
		row_index[i] = mp_load_u32(&pos);
	}
	assert(pos == xrow->body->iov_base + xrow->body->iov_len);
	return 0;
}

/** Return the name of a run data file. */
static inline const char *
vy_run_filename(struct vy_run *run)
{
	char *buf = tt_static_buf();
	vy_run_snprint_filename(buf, TT_STATIC_BUF_LEN, run->id, VY_FILE_RUN);
	return buf;
}

/**
 * Read a page requests from vinyl xlog data file.
 *
 * @retval 0 on success
 * @retval -1 on error, check diag
 */
static int
vy_page_read(struct vy_page *page, const struct vy_page_info *page_info,
	     struct vy_run *run, ZSTD_DStream *zdctx)
{
	/* read xlog tx from xlog file */
	size_t region_svp = region_used(&fiber()->gc);
	char *data = (char *)region_alloc(&fiber()->gc, page_info->size);
	if (data == NULL) {
		diag_set(OutOfMemory, page_info->size, "region gc", "page");
		return -1;
	}
	ssize_t readen = fio_pread(run->fd, data, page_info->size,
				   page_info->offset);
	ERROR_INJECT(ERRINJ_VYRUN_DATA_READ, {
		readen = -1;
		errno = EIO;});
	if (readen < 0) {
		diag_set(SystemError, "failed to read from file");
		goto error;
	}
	if (readen != (ssize_t)page_info->size) {
		diag_set(ClientError, ER_INVALID_RUN_FILE,
			 "Unexpected end of file");
		goto error;
	}

	struct errinj *inj = errinj(ERRINJ_VY_READ_PAGE_TIMEOUT, ERRINJ_DOUBLE);
	if (inj != NULL && inj->dparam > 0)
		thread_sleep(inj->dparam);

	ERROR_INJECT_SLEEP(ERRINJ_VY_READ_PAGE_DELAY);

	/* decode xlog tx */
	const char *data_pos = data;
	const char *data_end = data + readen;
	char *rows = page->data;
	char *rows_end = rows + page_info->unpacked_size;
	if (xlog_tx_decode(data, data_end, rows, rows_end, zdctx) != 0)
		goto error;

	struct xrow_header xrow;
	data_pos = page->data + page_info->row_index_offset;
	data_end = page->data + page_info->unpacked_size;
	if (xrow_header_decode(&xrow, &data_pos, data_end, true) == -1)
		goto error;
	if (xrow.type != VY_RUN_ROW_INDEX) {
		diag_set(ClientError, ER_INVALID_RUN_FILE,
			 tt_sprintf("Wrong row index type "
				    "(expected %d, got %u)",
				    VY_RUN_ROW_INDEX, (unsigned)xrow.type));
		goto error;
	}
	if (vy_row_index_decode(page->row_index, page->row_count, &xrow) != 0)
		goto error;
	region_truncate(&fiber()->gc, region_svp);
	ERROR_INJECT(ERRINJ_VY_READ_PAGE, {
		diag_set(ClientError, ER_INJECTION, "vinyl page read");
		return -1;});
	return 0;
error:
	region_truncate(&fiber()->gc, region_svp);
	diag_log();
	say_error("error reading %s@%llu:%u", vy_run_filename(run),
		  (unsigned long long)page_info->offset,
		  (unsigned)page_info->size);
	return -1;
}

/**
 * Get thread local zstd decompression context
 */
static ZSTD_DStream *
vy_env_get_zdctx(struct vy_run_env *env)
{
	ZSTD_DStream *zdctx = tt_pthread_getspecific(env->zdctx_key);
	if (zdctx == NULL) {
		zdctx = ZSTD_createDStream();
		if (zdctx == NULL) {
			diag_set(OutOfMemory, sizeof(zdctx), "malloc",
				 "zstd context");
			return NULL;
		}
		tt_pthread_setspecific(env->zdctx_key, zdctx);
	}
	return zdctx;
}

/**
 * vinyl read task callback
 */
static int
vy_page_read_cb(struct cbus_call_msg *base)
{
	struct vy_page_read_task *task = (struct vy_page_read_task *)base;
	ZSTD_DStream *zdctx = vy_env_get_zdctx(task->run->env);
	if (zdctx == NULL)
		return -1;
	if (vy_page_read(task->page, task->page_info, task->run, zdctx) != 0)
		return -1;
	if (task->key.stmt != NULL) {
		task->pos_in_page = vy_page_find_key(task->page, task->key,
						     task->cmp_def, task->format,
						     task->iterator_type,
						     &task->equal_found);
	}
	return 0;
}

/**
 * Read a page from disk given its number.
 * The function caches two most recently read pages.
 *
 * @retval 0 success
 * @retval -1 critical error
 */
static NODISCARD int
vy_run_iterator_load_page(struct vy_run_iterator *itr, uint32_t page_no,
			  struct vy_entry key, enum iterator_type iterator_type,
			  struct vy_page **result, uint32_t *pos_in_page,
			  bool *equal_found)
{
	struct vy_slice *slice = itr->slice;
	struct vy_run_env *env = slice->run->env;

	/* Check cache */
	struct vy_page *page = NULL;
	if (itr->curr_page != NULL &&
	    itr->curr_page->page_no == page_no) {
		page = itr->curr_page;
	} else if (itr->prev_page != NULL &&
		   itr->prev_page->page_no == page_no) {
		SWAP(itr->prev_page, itr->curr_page);
		page = itr->curr_page;
	}
	if (page != NULL) {
		if (key.stmt != NULL)
			*pos_in_page = vy_page_find_key(page, key, itr->cmp_def,
							itr->format, iterator_type,
							equal_found);
		*result = page;
		return 0;
	}

	/* Allocate buffers */
	struct vy_page_info *page_info = vy_run_page_info(slice->run, page_no);
	page = vy_page_new(page_info);
	if (page == NULL)
		return -1;

	/* Read page data from the disk */
	struct vy_page_read_task *task = mempool_alloc(&env->read_task_pool);
	if (task == NULL) {
		diag_set(OutOfMemory, sizeof(*task),
			 "mempool", "vy_page_read_task");
		vy_page_delete(page);
		return -1;
	}
	task->run = slice->run;
	task->page_info = page_info;
	task->page = page;
	task->key = key;
	task->iterator_type = iterator_type;
	task->cmp_def = itr->cmp_def;
	task->format = itr->format;
	task->pos_in_page = 0;
	task->equal_found = false;

	int rc = vy_run_env_coio_call(env, &task->base, vy_page_read_cb);

	*pos_in_page = task->pos_in_page;
	*equal_found = task->equal_found;

	mempool_free(&env->read_task_pool, task);
	if (rc != 0) {
		vy_page_delete(page);
		return -1;
	}

	/* Update cache */
	if (itr->prev_page != NULL)
		vy_page_delete(itr->prev_page);
	itr->prev_page = itr->curr_page;
	itr->curr_page = page;
	page->page_no = page_no;

	/* Update read statistics. */
	itr->stat->read.rows += page_info->row_count;
	itr->stat->read.bytes += page_info->unpacked_size;
	itr->stat->read.bytes_compressed += page_info->size;
	itr->stat->read.pages++;

	*result = page;
	return 0;
}

/**
 * Read key and lsn by a given wide position.
 * For the first record in a page reads the result from the page
 * index instead of fetching it from disk.
 *
 * @retval 0 success
 * @retval -1 read error or out of memory.
 */
static NODISCARD int
vy_run_iterator_read(struct vy_run_iterator *itr,
		     struct vy_run_iterator_pos pos,
		     struct vy_entry *ret)
{
	struct vy_page *page;
	bool equal_found;
	uint32_t pos_in_page;
	int rc = vy_run_iterator_load_page(itr, pos.page_no, vy_entry_none(),
					   ITER_GE, &page, &pos_in_page,
					   &equal_found);
	if (rc != 0)
		return rc;
	*ret = vy_page_stmt(page, pos.pos_in_page, itr->cmp_def, itr->format);
	if (ret->stmt == NULL)
		return -1;
	return 0;
}

/**
 * Binary search in a run for the given key.
 * In terms of STL, makes lower_bound for EQ,GE,LT and upper_bound for GT,LE
 * Resulting wide position is stored it *pos argument
 * Additionally *equal_key argument is set to true if the found value is
 * equal to given key (untouched otherwise)
 *
 * @retval 0 success
 * @retval 1 EOF
 * @retval -1 read or memory error
 */
static NODISCARD int
vy_run_iterator_search(struct vy_run_iterator *itr,
		       enum iterator_type iterator_type, struct vy_entry key,
		       struct vy_run_iterator_pos *pos, bool *equal_key)
{
	pos->page_no = vy_page_index_find_page(itr->slice->run, key,
					       itr->cmp_def, iterator_type,
					       equal_key);
	if (pos->page_no == itr->slice->run->info.page_count)
		return 1;
	bool equal_in_page;
	struct vy_page *page;
	int rc = vy_run_iterator_load_page(itr, pos->page_no, key,
					   iterator_type, &page,
					   &pos->pos_in_page, &equal_in_page);
	if (rc != 0)
		return rc;
	if (pos->pos_in_page == page->row_count) {
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
 * @retval 0 success, set *pos to new value
 * @retval 1 EOF
 * Affects: curr_loaded_page
 */
static NODISCARD int
vy_run_iterator_next_pos(struct vy_run_iterator *itr,
			 enum iterator_type iterator_type,
			 struct vy_run_iterator_pos *pos)
{
	struct vy_run *run = itr->slice->run;
	*pos = itr->curr_pos;
	if (iterator_type == ITER_LE || iterator_type == ITER_LT) {
		assert(pos->page_no <= run->info.page_count);
		if (pos->pos_in_page > 0) {
			pos->pos_in_page--;
		} else {
			if (pos->page_no == 0)
				return 1;
			pos->page_no--;
			struct vy_page_info *page_info =
				vy_run_page_info(run, pos->page_no);
			assert(page_info->row_count > 0);
			pos->pos_in_page = page_info->row_count - 1;
		}
	} else {
		assert(iterator_type == ITER_GE || iterator_type == ITER_GT ||
		       iterator_type == ITER_EQ);
		assert(pos->page_no < run->info.page_count);
		struct vy_page_info *page_info =
			vy_run_page_info(run, pos->page_no);
		assert(page_info->row_count > 0);
		pos->pos_in_page++;
		if (pos->pos_in_page >= page_info->row_count) {
			pos->page_no++;
			pos->pos_in_page = 0;
			if (pos->page_no == run->info.page_count)
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
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read or memory error
 * Affects: curr_loaded_page, curr_pos
 */
static NODISCARD int
vy_run_iterator_find_lsn(struct vy_run_iterator *itr, struct vy_entry *ret)
{
	struct vy_slice *slice = itr->slice;
	struct key_def *cmp_def = itr->cmp_def;

	*ret = vy_entry_none();

	assert(itr->search_started);
	assert(itr->curr.stmt != NULL);
	assert(itr->curr_pos.page_no < slice->run->info.page_count);

	while (vy_stmt_lsn(itr->curr.stmt) > (**itr->read_view).vlsn ||
	       vy_stmt_flags(itr->curr.stmt) & VY_STMT_SKIP_READ) {
		if (vy_run_iterator_next_pos(itr, itr->iterator_type,
					     &itr->curr_pos) != 0) {
			vy_run_iterator_stop(itr);
			return 0;
		}
		tuple_unref(itr->curr.stmt);
		itr->curr = vy_entry_none();
		if (vy_run_iterator_read(itr, itr->curr_pos, &itr->curr) != 0)
			return -1;
		if (itr->iterator_type == ITER_EQ &&
		    vy_entry_compare(itr->curr, itr->key, cmp_def) != 0) {
			vy_run_iterator_stop(itr);
			return 0;
		}
	}
	if (itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT) {
		struct vy_run_iterator_pos test_pos;
		while (vy_run_iterator_next_pos(itr, itr->iterator_type,
						&test_pos) == 0) {
			struct vy_entry test;
			if (vy_run_iterator_read(itr, test_pos, &test) != 0)
				return -1;
			if (vy_stmt_lsn(test.stmt) > (**itr->read_view).vlsn ||
			    vy_stmt_flags(test.stmt) & VY_STMT_SKIP_READ ||
			    vy_entry_compare(itr->curr, test, cmp_def) != 0) {
				tuple_unref(test.stmt);
				break;
			}
			tuple_unref(itr->curr.stmt);
			itr->curr = test;
			itr->curr_pos = test_pos;
		}
	}
	/* Check if the result is within the slice boundaries. */
	if (itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT) {
		if (slice->begin.stmt != NULL &&
		    vy_entry_compare(itr->curr, slice->begin, cmp_def) < 0) {
			vy_run_iterator_stop(itr);
			return 0;
		}
	} else {
		assert(itr->iterator_type == ITER_GE ||
		       itr->iterator_type == ITER_GT ||
		       itr->iterator_type == ITER_EQ);
		if (slice->end.stmt != NULL &&
		    vy_entry_compare(itr->curr, slice->end, cmp_def) >= 0) {
			vy_run_iterator_stop(itr);
			return 0;
		}
	}
	vy_stmt_counter_acct_tuple(&itr->stat->get, itr->curr.stmt);
	*ret = itr->curr;
	return 0;
}

/**
 * Helper function for vy_run_iterator_seek().
 *
 * Positions the iterator to the beginning (i.e. leftmost for GE,
 * rightmost for LE) of a series of statements matching the given
 * search criteria.
 *
 * Updates itr->curr_pos. Doesn't affect itr->curr.
 *
 * @retval 0 success
 * @retval 1 EOF
 * @retval -1 read or memory error
 */
static NODISCARD int
vy_run_iterator_do_seek(struct vy_run_iterator *itr,
			enum iterator_type iterator_type, struct vy_entry key)
{
	struct vy_run *run = itr->slice->run;
	struct vy_run_iterator_pos end_pos = {run->info.page_count, 0};
	bool equal_found = false;
	if (!vy_stmt_is_empty_key(key.stmt)) {
		int rc = vy_run_iterator_search(itr, iterator_type, key,
						&itr->curr_pos, &equal_found);
		if (rc != 0)
			return rc;
	} else if (iterator_type == ITER_LE) {
		itr->curr_pos = end_pos;
	} else {
		assert(iterator_type == ITER_GE);
		itr->curr_pos.page_no = 0;
		itr->curr_pos.pos_in_page = 0;
	}
	if (iterator_type == ITER_EQ && !equal_found)
		return 1;
	if ((iterator_type == ITER_GE || iterator_type == ITER_GT) &&
	    itr->curr_pos.page_no == end_pos.page_no)
		return 1;
	if (iterator_type == ITER_LT || iterator_type == ITER_LE) {
		/**
		 * 1) in case of ITER_LT we now positioned on the value >= than
		 * given, so we need to make a step on previous key
		 * 2) in case if ITER_LE we now positioned on the value > than
		 * given (special branch of code in vy_run_iterator_search),
		 * so we need to make a step on previous key
		 */
		return vy_run_iterator_next_pos(itr, iterator_type,
						&itr->curr_pos);
	} else {
		assert(iterator_type == ITER_GE || iterator_type == ITER_GT ||
		       iterator_type == ITER_EQ);
		/**
		 * 1) in case of ITER_GT we now positioned on the value > than
		 * given (special branch of code in vy_run_iterator_search),
		 * so we need just to find proper lsn
		 * 2) in case if ITER_GE or ITER_EQ we now positioned on the
		 * value >= given, so we need just to find proper lsn
		 */
		return 0;
	}
}

/**
 * Position the iterator to the first statement satisfying
 * the iterator search criteria and following the given key
 * (pass NULL to start iteration).
 */
static NODISCARD int
vy_run_iterator_seek(struct vy_run_iterator *itr, struct vy_entry last,
		     struct vy_entry *ret)
{
	struct key_def *cmp_def = itr->cmp_def;
	struct vy_slice *slice = itr->slice;
	struct tuple_bloom *bloom = slice->run->info.bloom;
	struct vy_entry key = itr->key;
	enum iterator_type iterator_type = itr->iterator_type;

	*ret = vy_entry_none();
	assert(itr->search_started);

	/* Check the bloom filter on the first iteration. */
	bool check_bloom = (itr->iterator_type == ITER_EQ &&
			    itr->curr.stmt == NULL && bloom != NULL);
	if (check_bloom && !vy_bloom_maybe_has(bloom, itr->key, itr->key_def)) {
		vy_run_iterator_stop(itr);
		itr->stat->bloom_hit++;
		return 0;
	}

	/*
	 * vy_run_iterator_do_seek() implements its own EQ check.
	 * We only need to check EQ here if iterator type and key
	 * passed to it differ from the original.
	 */
	bool check_eq = false;

	/*
	 * Modify iterator type and key so as to position it to
	 * the first statement following the given key.
	 */
	if (last.stmt != NULL) {
		if (iterator_type == ITER_EQ)
			check_eq = true;
		iterator_type = iterator_direction(iterator_type) > 0 ?
				ITER_GT : ITER_LT;
		key = last;
	}

	/* Take slice boundaries into account. */
	if (slice->begin.stmt != NULL &&
	    (iterator_type == ITER_GT || iterator_type == ITER_GE ||
	     iterator_type == ITER_EQ)) {
		/*
		 *    original   |     start
		 * --------------+-------+-----+
		 *   KEY   | DIR |  KEY  | DIR |
		 * --------+-----+-------+-----+
		 * > begin | *   | key   | *   |
		 * = begin | gt  | key   | gt  |
		 *         | ge  | begin | ge  |
		 *         | eq  | begin | ge  |
		 * < begin | gt  | begin | ge  |
		 *         | ge  | begin | ge  |
		 *         | eq  |    stop     |
		 */
		int cmp = vy_entry_compare(key, slice->begin, cmp_def);
		if (cmp < 0 && iterator_type == ITER_EQ) {
			vy_run_iterator_stop(itr);
			return 0;
		}
		if (cmp < 0 || (cmp == 0 && iterator_type != ITER_GT)) {
			if (iterator_type == ITER_EQ)
				check_eq = true;
			iterator_type = ITER_GE;
			key = slice->begin;
		}
	}
	if (slice->end.stmt != NULL &&
	    (iterator_type == ITER_LT || iterator_type == ITER_LE)) {
		/*
		 *    original   |     start
		 * --------------+-------+-----+
		 *   KEY   | DIR |  KEY  | DIR |
		 * --------+-----+-------+-----+
		 * < end   | *   | key   | *   |
		 * = end   | lt  | key   | lt  |
		 *         | le  | end   | lt  |
		 * > end   | lt  | end   | lt  |
		 *         | le  | end   | lt  |
		 */
		int cmp = vy_entry_compare(key, slice->end, cmp_def);
		if (cmp > 0 || (cmp == 0 && iterator_type != ITER_LT)) {
			iterator_type = ITER_LT;
			key = slice->end;
		}
	}

	/* Perform a lookup in the run. */
	itr->stat->lookup++;
	int rc = vy_run_iterator_do_seek(itr, iterator_type, key);
	if (rc < 0)
		return -1;
	if (rc > 0)
		goto not_found;

	/* Load the found statement. */
	if (itr->curr.stmt != NULL) {
		tuple_unref(itr->curr.stmt);
		itr->curr = vy_entry_none();
	}
	if (vy_run_iterator_read(itr, itr->curr_pos, &itr->curr) != 0)
		return -1;

	/* Check EQ constraint if necessary. */
	if (check_eq && vy_entry_compare(itr->curr, itr->key,
					 itr->cmp_def) != 0)
		goto not_found;

	/* Skip statements invisible from the iterator read view. */
	return vy_run_iterator_find_lsn(itr, ret);

not_found:
	if (check_bloom)
		itr->stat->bloom_miss++;
	vy_run_iterator_stop(itr);
	return 0;
}

/* }}} vy_run_iterator vy_run_iterator support functions */

/* {{{ vy_run_iterator API implementation */

void
vy_run_iterator_open(struct vy_run_iterator *itr,
		     struct vy_run_iterator_stat *stat,
		     struct vy_slice *slice, enum iterator_type iterator_type,
		     struct vy_entry key, const struct vy_read_view **rv,
		     struct key_def *cmp_def, struct key_def *key_def,
		     struct tuple_format *format)
{
	itr->stat = stat;
	itr->cmp_def = cmp_def;
	itr->key_def = key_def;
	itr->format = format;
	itr->slice = slice;

	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;

	itr->curr = vy_entry_none();
	itr->curr_pos.page_no = slice->run->info.page_count;
	itr->curr_page = NULL;
	itr->prev_page = NULL;
	itr->search_started = false;

	/*
	 * Make sure the format we use to create tuples won't
	 * go away if DDL is called while the iterator is used.
	 *
	 * XXX: Please remove this kludge when proper DDL locking
	 * is implemented on transaction management level or multi
	 * version data dictionary is in place.
	 */
	tuple_format_ref(format);
}

/**
 * Advance a run iterator to the newest statement for the next key.
 * The statement is returned in @ret (NULL if EOF).
 * Returns 0 on success, -1 on memory allocation or IO error.
 */
static NODISCARD int
vy_run_iterator_next_key(struct vy_run_iterator *itr, struct vy_entry *ret)
{
	*ret = vy_entry_none();

	if (!itr->search_started) {
		itr->search_started = true;
		return vy_run_iterator_seek(itr, vy_entry_none(), ret);
	}
	if (itr->curr.stmt == NULL)
		return 0;

	assert(itr->curr_pos.page_no < itr->slice->run->info.page_count);

	struct vy_entry next = vy_entry_none();
	do {
		if (next.stmt != NULL)
			tuple_unref(next.stmt);
		if (vy_run_iterator_next_pos(itr, itr->iterator_type,
					     &itr->curr_pos) != 0) {
			vy_run_iterator_stop(itr);
			return 0;
		}

		if (vy_run_iterator_read(itr, itr->curr_pos, &next) != 0)
			return -1;
	} while (vy_entry_compare(itr->curr, next, itr->cmp_def) == 0);

	tuple_unref(itr->curr.stmt);
	itr->curr = next;

	if (itr->iterator_type == ITER_EQ &&
	    vy_entry_compare(next, itr->key, itr->cmp_def) != 0) {
		vy_run_iterator_stop(itr);
		return 0;
	}
	return vy_run_iterator_find_lsn(itr, ret);
}

/**
 * Advance a run iterator to the next (older) statement for the
 * current key. The statement is returned in @ret (NULL if EOF).
 * Returns 0 on success, -1 on memory allocation or IO error.
 */
static NODISCARD int
vy_run_iterator_next_lsn(struct vy_run_iterator *itr, struct vy_entry *ret)
{
	*ret = vy_entry_none();

	assert(itr->search_started);
	assert(itr->curr.stmt != NULL);
	assert(itr->curr_pos.page_no < itr->slice->run->info.page_count);

	struct vy_run_iterator_pos next_pos;
next:
	if (vy_run_iterator_next_pos(itr, ITER_GE, &next_pos) != 0) {
		vy_run_iterator_stop(itr);
		return 0;
	}

	struct vy_entry next;
	if (vy_run_iterator_read(itr, next_pos, &next) != 0)
		return -1;

	if (vy_entry_compare(itr->curr, next, itr->cmp_def) != 0) {
		tuple_unref(next.stmt);
		return 0;
	}

	tuple_unref(itr->curr.stmt);
	itr->curr = next;
	itr->curr_pos = next_pos;
	if (vy_stmt_flags(itr->curr.stmt) & VY_STMT_SKIP_READ)
		goto next;

	vy_stmt_counter_acct_tuple(&itr->stat->get, itr->curr.stmt);
	*ret = itr->curr;
	return 0;
}

NODISCARD int
vy_run_iterator_next(struct vy_run_iterator *itr,
		     struct vy_history *history)
{
	vy_history_cleanup(history);
	struct vy_entry entry;
	if (vy_run_iterator_next_key(itr, &entry) != 0)
		return -1;
	while (entry.stmt != NULL) {
		if (vy_history_append_stmt(history, entry) != 0)
			return -1;
		if (vy_history_is_terminal(history))
			break;
		if (vy_run_iterator_next_lsn(itr, &entry) != 0)
			return -1;
	}
	return 0;
}

NODISCARD int
vy_run_iterator_skip(struct vy_run_iterator *itr, struct vy_entry last,
		     struct vy_history *history)
{
	/*
	 * Check if the iterator is already positioned
	 * at the statement following last.
	 */
	if (itr->search_started &&
	    (itr->curr.stmt == NULL || last.stmt == NULL ||
	     iterator_direction(itr->iterator_type) *
	     vy_entry_compare(itr->curr, last, itr->cmp_def) > 0))
		return 0;

	vy_history_cleanup(history);

	itr->search_started = true;
	struct vy_entry entry;
	if (vy_run_iterator_seek(itr, last, &entry) != 0)
		return -1;

	while (entry.stmt != NULL) {
		if (vy_history_append_stmt(history, entry) != 0)
			return -1;
		if (vy_history_is_terminal(history))
			break;
		if (vy_run_iterator_next_lsn(itr, &entry) != 0)
			return -1;
	}
	return 0;
}

void
vy_run_iterator_close(struct vy_run_iterator *itr)
{
	vy_run_iterator_stop(itr);
	tuple_format_unref(itr->format);
	TRASH(itr);
}

/* }}} vy_run_iterator API implementation */

/** Account a page to run statistics. */
static void
vy_run_acct_page(struct vy_run *run, struct vy_page_info *page)
{
	const char *min_key_end = page->min_key;
	mp_next(&min_key_end);
	run->page_index_size += sizeof(struct vy_page_info);
	run->page_index_size += min_key_end - page->min_key;
	run->count.rows += page->row_count;
	run->count.bytes += page->unpacked_size;
	run->count.bytes_compressed += page->size;
	run->count.pages++;
}

int
vy_run_recover(struct vy_run *run, const char *dir,
	       uint32_t space_id, uint32_t iid, struct key_def *cmp_def)
{
	char path[PATH_MAX];
	vy_run_snprint_path(path, sizeof(path), dir,
			    space_id, iid, run->id, VY_FILE_INDEX);

	struct xlog_cursor cursor;
	ERROR_INJECT_COUNTDOWN(ERRINJ_VY_RUN_OPEN, {
		diag_set(SystemError, "failed to open '%s' file", path);
		goto fail;
	});
	if (xlog_cursor_open(&cursor, path))
		goto fail;

	struct xlog_meta *meta = &cursor.meta;
	if (strcmp(meta->filetype, XLOG_META_TYPE_INDEX) != 0) {
		diag_set(ClientError, ER_INVALID_XLOG_TYPE,
			 XLOG_META_TYPE_INDEX, meta->filetype);
		goto fail_close;
	}

	/* Read run header. */
	struct xrow_header xrow;
	/* all rows should be in one tx */
	int rc = xlog_cursor_next_tx(&cursor);

	if (rc != 0) {
		if (rc > 0)
			diag_set(ClientError, ER_INVALID_INDEX_FILE,
				 path, "Unexpected end of file");
		goto fail_close;
	}
	rc = xlog_cursor_next_row(&cursor, &xrow);
	if (rc != 0) {
		if (rc > 0)
			diag_set(ClientError, ER_INVALID_INDEX_FILE,
				 path, "Unexpected end of file");
		goto fail_close;
	}

	if (xrow.type != VY_INDEX_RUN_INFO) {
		diag_set(ClientError, ER_INVALID_INDEX_FILE, path,
			 tt_sprintf("Wrong xrow type (expected %d, got %u)",
				    VY_INDEX_RUN_INFO, (unsigned)xrow.type));
		goto fail_close;
	}

	if (vy_run_info_decode(&run->info, &xrow, path) != 0)
		goto fail_close;

	/* Allocate buffer for page info. */
	run->page_info = calloc(run->info.page_count,
				      sizeof(struct vy_page_info));
	if (run->page_info == NULL) {
		diag_set(OutOfMemory,
			 run->info.page_count * sizeof(struct vy_page_info),
			 "malloc", "struct vy_page_info");
		goto fail_close;
	}

	for (uint32_t page_no = 0; page_no < run->info.page_count; page_no++) {
		int rc = xlog_cursor_next_row(&cursor, &xrow);
		if (rc != 0) {
			if (rc > 0) {
				/** To few pages in file */
				diag_set(ClientError, ER_INVALID_INDEX_FILE,
					 path, "Unexpected end of file");
			}
			/*
			 * Limit the count of pages to
			 * successfully created pages.
			 */
			run->info.page_count = page_no;
			goto fail_close;
		}
		if (xrow.type != VY_INDEX_PAGE_INFO) {
			diag_set(ClientError, ER_INVALID_INDEX_FILE,
				 tt_sprintf("Wrong xrow type "
					    "(expected %d, got %u)",
					    VY_INDEX_PAGE_INFO,
					    (unsigned)xrow.type));
			goto fail_close;
		}
		struct vy_page_info *page = run->page_info + page_no;
		if (vy_page_info_decode(page, &xrow, cmp_def, path) < 0) {
			/**
			 * Limit the count of pages to successfully
			 * created pages
			 */
			run->info.page_count = page_no;
			goto fail_close;
		}
		vy_run_acct_page(run, page);
	}

	/* We don't need to keep metadata file open any longer. */
	xlog_cursor_close(&cursor, false);

	/* Prepare data file for reading. */
	vy_run_snprint_path(path, sizeof(path), dir,
			    space_id, iid, run->id, VY_FILE_RUN);
	if (xlog_cursor_open(&cursor, path))
		goto fail;
	meta = &cursor.meta;
	if (strcmp(meta->filetype, XLOG_META_TYPE_RUN) != 0) {
		diag_set(ClientError, ER_INVALID_XLOG_TYPE,
			 XLOG_META_TYPE_RUN, meta->filetype);
		goto fail_close;
	}
	run->fd = cursor.fd;
	xlog_cursor_close(&cursor, true);
	return 0;

fail_close:
	xlog_cursor_close(&cursor, false);
fail:
	vy_run_clear(run);
	diag_log();
	say_error("failed to load `%s'", path);
	return -1;
}

/* dump statement to the run page buffers (stmt header and data) */
static int
vy_run_dump_stmt(struct vy_entry entry, struct xlog *data_xlog,
		 struct vy_page_info *info, struct key_def *key_def,
		 bool is_primary)
{
	struct xrow_header xrow;
	int rc = (is_primary ?
		  vy_stmt_encode_primary(entry.stmt, key_def, 0, &xrow) :
		  vy_stmt_encode_secondary(entry.stmt, key_def,
					   vy_entry_multikey_idx(entry, key_def),
					   &xrow));
	if (rc != 0)
		return -1;

	ssize_t row_size;
	if ((row_size = xlog_write_row(data_xlog, &xrow)) < 0)
		return -1;

	info->unpacked_size += row_size;
	info->row_count++;
	return 0;
}

/**
 * Encode uint32_t array of row offsets (row index) as xrow
 *
 * @param row_index row index
 * @param row_count size of row index
 * @param[out] xrow xrow to fill.
 * @retval 0 for success
 * @retval -1 for error
 */
static int
vy_row_index_encode(const uint32_t *row_index, uint32_t row_count,
		    struct xrow_header *xrow)
{
	memset(xrow, 0, sizeof(*xrow));
	xrow->type = VY_RUN_ROW_INDEX;

	size_t size = mp_sizeof_map(1) +
		      mp_sizeof_uint(VY_ROW_INDEX_DATA) +
		      mp_sizeof_bin(sizeof(uint32_t) * row_count);
	char *pos = region_alloc(&fiber()->gc, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "region", "row index");
		return -1;
	}
	xrow->body->iov_base = pos;
	pos = mp_encode_map(pos, 1);
	pos = mp_encode_uint(pos, VY_ROW_INDEX_DATA);
	pos = mp_encode_binl(pos, sizeof(uint32_t) * row_count);
	for (uint32_t i = 0; i < row_count; ++i)
		pos = mp_store_u32(pos, row_index[i]);
	xrow->body->iov_len = (void *)pos - xrow->body->iov_base;
	assert(xrow->body->iov_len == size);
	xrow->bodycnt = 1;
	return 0;
}

/**
 * Helper to extend run page info array
 */
static inline int
vy_run_alloc_page_info(struct vy_run *run, uint32_t *page_info_capacity)
{
	uint32_t cap = *page_info_capacity > 0 ?
		       *page_info_capacity * 2 : 16;
	struct vy_page_info *page_info = realloc(run->page_info,
					cap * sizeof(*page_info));
	if (page_info == NULL) {
		diag_set(OutOfMemory, cap * sizeof(*page_info),
			 "realloc", "struct vy_page_info");
		return -1;
	}
	run->page_info = page_info;
	*page_info_capacity = cap;
	return 0;
}

/** {{{ vy_page_info */

/**
 * Encode vy_page_info as xrow.
 * Allocates using region_alloc.
 *
 * @param page_info page information to encode
 * @param[out] xrow xrow to fill
 *
 * @retval  0 success
 * @retval -1 error, check diag
 */
static int
vy_page_info_encode(const struct vy_page_info *page_info,
		    struct xrow_header *xrow)
{
	struct region *region = &fiber()->gc;

	uint32_t min_key_size;
	const char *tmp = page_info->min_key;
	assert(mp_typeof(*tmp) == MP_ARRAY);
	mp_next(&tmp);
	min_key_size = tmp - page_info->min_key;

	/* calc tuple size */
	uint32_t size;
	/* 3 items: page offset, size, and map */
	size = mp_sizeof_map(6) +
	       mp_sizeof_uint(VY_PAGE_INFO_OFFSET) +
	       mp_sizeof_uint(page_info->offset) +
	       mp_sizeof_uint(VY_PAGE_INFO_SIZE) +
	       mp_sizeof_uint(page_info->size) +
	       mp_sizeof_uint(VY_PAGE_INFO_ROW_COUNT) +
	       mp_sizeof_uint(page_info->row_count) +
	       mp_sizeof_uint(VY_PAGE_INFO_MIN_KEY) +
	       min_key_size +
	       mp_sizeof_uint(VY_PAGE_INFO_UNPACKED_SIZE) +
	       mp_sizeof_uint(page_info->unpacked_size) +
	       mp_sizeof_uint(VY_PAGE_INFO_ROW_INDEX_OFFSET) +
	       mp_sizeof_uint(page_info->row_index_offset);

	char *pos = region_alloc(region, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "region", "page encode");
		return -1;
	}

	memset(xrow, 0, sizeof(*xrow));
	/* encode page */
	xrow->body->iov_base = pos;
	pos = mp_encode_map(pos, 6);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_OFFSET);
	pos = mp_encode_uint(pos, page_info->offset);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_SIZE);
	pos = mp_encode_uint(pos, page_info->size);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_ROW_COUNT);
	pos = mp_encode_uint(pos, page_info->row_count);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_MIN_KEY);
	memcpy(pos, page_info->min_key, min_key_size);
	pos += min_key_size;
	pos = mp_encode_uint(pos, VY_PAGE_INFO_UNPACKED_SIZE);
	pos = mp_encode_uint(pos, page_info->unpacked_size);
	pos = mp_encode_uint(pos, VY_PAGE_INFO_ROW_INDEX_OFFSET);
	pos = mp_encode_uint(pos, page_info->row_index_offset);
	xrow->body->iov_len = (void *)pos - xrow->body->iov_base;
	xrow->bodycnt = 1;

	xrow->type = VY_INDEX_PAGE_INFO;
	return 0;
}

/** vy_page_info }}} */

/** {{{ vy_run_info */

/** Return the size of encoded statement statistics. */
static size_t
vy_stmt_stat_sizeof(const struct vy_stmt_stat *stat)
{
	return mp_sizeof_map(4) +
		mp_sizeof_uint(IPROTO_INSERT) +
		mp_sizeof_uint(IPROTO_REPLACE) +
		mp_sizeof_uint(IPROTO_DELETE) +
		mp_sizeof_uint(IPROTO_UPSERT) +
		mp_sizeof_uint(stat->inserts) +
		mp_sizeof_uint(stat->replaces) +
		mp_sizeof_uint(stat->deletes) +
		mp_sizeof_uint(stat->upserts);
}

/** Encode statement statistics to @buf and return advanced @buf. */
static char *
vy_stmt_stat_encode(const struct vy_stmt_stat *stat, char *buf)
{
	buf = mp_encode_map(buf, 4);
	buf = mp_encode_uint(buf, IPROTO_INSERT);
	buf = mp_encode_uint(buf, stat->inserts);
	buf = mp_encode_uint(buf, IPROTO_REPLACE);
	buf = mp_encode_uint(buf, stat->replaces);
	buf = mp_encode_uint(buf, IPROTO_DELETE);
	buf = mp_encode_uint(buf, stat->deletes);
	buf = mp_encode_uint(buf, IPROTO_UPSERT);
	buf = mp_encode_uint(buf, stat->upserts);
	return buf;
}

/**
 * Encode vy_run_info as xrow
 * Allocates using region alloc
 *
 * @param run_info the run information
 * @param xrow xrow to fill.
 *
 * @retval  0 success
 * @retval -1 on error, check diag
 */
static int
vy_run_info_encode(const struct vy_run_info *run_info,
		   struct xrow_header *xrow)
{
	const char *tmp;
	tmp = run_info->min_key;
	mp_next(&tmp);
	size_t min_key_size = tmp - run_info->min_key;
	tmp = run_info->max_key;
	mp_next(&tmp);
	size_t max_key_size = tmp - run_info->max_key;

	uint32_t key_count = 6;
	if (run_info->bloom != NULL)
		key_count++;

	size_t size = mp_sizeof_map(key_count);
	size += mp_sizeof_uint(VY_RUN_INFO_MIN_KEY) + min_key_size;
	size += mp_sizeof_uint(VY_RUN_INFO_MAX_KEY) + max_key_size;
	size += mp_sizeof_uint(VY_RUN_INFO_MIN_LSN) +
		mp_sizeof_uint(run_info->min_lsn);
	size += mp_sizeof_uint(VY_RUN_INFO_MAX_LSN) +
		mp_sizeof_uint(run_info->max_lsn);
	size += mp_sizeof_uint(VY_RUN_INFO_PAGE_COUNT) +
		mp_sizeof_uint(run_info->page_count);
	if (run_info->bloom != NULL)
		size += mp_sizeof_uint(VY_RUN_INFO_BLOOM) +
			tuple_bloom_size(run_info->bloom);
	size += mp_sizeof_uint(VY_RUN_INFO_STMT_STAT) +
		vy_stmt_stat_sizeof(&run_info->stmt_stat);

	char *pos = region_alloc(&fiber()->gc, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "region", "run encode");
		return -1;
	}
	memset(xrow, 0, sizeof(*xrow));
	xrow->body->iov_base = pos;
	/* encode values */
	pos = mp_encode_map(pos, key_count);
	pos = mp_encode_uint(pos, VY_RUN_INFO_MIN_KEY);
	memcpy(pos, run_info->min_key, min_key_size);
	pos += min_key_size;
	pos = mp_encode_uint(pos, VY_RUN_INFO_MAX_KEY);
	memcpy(pos, run_info->max_key, max_key_size);
	pos += max_key_size;
	pos = mp_encode_uint(pos, VY_RUN_INFO_MIN_LSN);
	pos = mp_encode_uint(pos, run_info->min_lsn);
	pos = mp_encode_uint(pos, VY_RUN_INFO_MAX_LSN);
	pos = mp_encode_uint(pos, run_info->max_lsn);
	pos = mp_encode_uint(pos, VY_RUN_INFO_PAGE_COUNT);
	pos = mp_encode_uint(pos, run_info->page_count);
	if (run_info->bloom != NULL) {
		pos = mp_encode_uint(pos, VY_RUN_INFO_BLOOM);
		pos = tuple_bloom_encode(run_info->bloom, pos);
	}
	pos = mp_encode_uint(pos, VY_RUN_INFO_STMT_STAT);
	pos = vy_stmt_stat_encode(&run_info->stmt_stat, pos);
	xrow->body->iov_len = (void *)pos - xrow->body->iov_base;
	xrow->bodycnt = 1;
	xrow->type = VY_INDEX_RUN_INFO;
	return 0;
}

/* vy_run_info }}} */

/**
 * Write run index to file.
 */
static int
vy_run_write_index(struct vy_run *run, const char *dirpath,
		   uint32_t space_id, uint32_t iid)
{
	char path[PATH_MAX];
	vy_run_snprint_path(path, sizeof(path), dirpath,
			    space_id, iid, run->id, VY_FILE_INDEX);

	say_info("writing `%s'", path);

	struct xlog index_xlog;
	struct xlog_meta meta;
	xlog_meta_create(&meta, XLOG_META_TYPE_INDEX, &INSTANCE_UUID,
			 NULL, NULL);
	struct xlog_opts opts = xlog_opts_default;
	opts.rate_limit = run->env->snap_io_rate_limit;
	opts.sync_interval = VY_RUN_SYNC_INTERVAL;
	if (xlog_create(&index_xlog, path, 0, &meta, &opts) < 0)
		return -1;

	xlog_tx_begin(&index_xlog);
	struct region *region = &fiber()->gc;
	size_t mem_used = region_used(region);

	struct xrow_header xrow;
	if (vy_run_info_encode(&run->info, &xrow) != 0 ||
	    xlog_write_row(&index_xlog, &xrow) < 0)
		goto fail_rollback;

	for (uint32_t page_no = 0; page_no < run->info.page_count; ++page_no) {
		struct vy_page_info *page_info = vy_run_page_info(run, page_no);
		if (vy_page_info_encode(page_info, &xrow) < 0) {
			goto fail_rollback;
		}
		if (xlog_write_row(&index_xlog, &xrow) < 0)
			goto fail_rollback;
	}

	region_truncate(region, mem_used);
	if (xlog_tx_commit(&index_xlog) < 0)
		goto fail;

	ERROR_INJECT(ERRINJ_VY_INDEX_FILE_RENAME, {
		diag_set(ClientError, ER_INJECTION, "vinyl index file rename");
		xlog_close(&index_xlog, false);
		return -1;
	});

	if (xlog_flush(&index_xlog) < 0 ||
	    xlog_rename(&index_xlog) < 0)
		goto fail;

	xlog_close(&index_xlog, false);
	return 0;

fail_rollback:
	region_truncate(region, mem_used);
	xlog_tx_rollback(&index_xlog);
fail:
	xlog_close(&index_xlog, false);
	unlink(path);
	return -1;
}

int
vy_run_writer_create(struct vy_run_writer *writer, struct vy_run *run,
		     const char *dirpath, uint32_t space_id, uint32_t iid,
		     struct key_def *cmp_def, struct key_def *key_def,
		     uint64_t page_size, double bloom_fpr, bool no_compression)
{
	memset(writer, 0, sizeof(*writer));
	writer->run = run;
	writer->dirpath = dirpath;
	writer->space_id = space_id;
	writer->iid = iid;
	writer->cmp_def = cmp_def;
	writer->key_def = key_def;
	writer->page_size = page_size;
	writer->bloom_fpr = bloom_fpr;
	writer->no_compression = no_compression;
	if (bloom_fpr < 1) {
		writer->bloom = tuple_bloom_builder_new(key_def->part_count);
		if (writer->bloom == NULL)
			return -1;
	}
	xlog_clear(&writer->data_xlog);
	ibuf_create(&writer->row_index_buf, &cord()->slabc,
		    4096 * sizeof(uint32_t));
	run->info.min_lsn = INT64_MAX;
	run->info.max_lsn = -1;
	assert(run->page_info == NULL);
	return 0;
}

/**
 * Create an xlog to write run.
 * @param writer Run writer.
 * @retval -1 Memory or IO error.
 * @retval  0 Success.
 */
static int
vy_run_writer_create_xlog(struct vy_run_writer *writer)
{
	assert(!xlog_is_open(&writer->data_xlog));
	char path[PATH_MAX];
	vy_run_snprint_path(path, sizeof(path), writer->dirpath,
			    writer->space_id, writer->iid, writer->run->id,
			    VY_FILE_RUN);
	say_info("writing `%s'", path);
	struct xlog_meta meta;
	xlog_meta_create(&meta, XLOG_META_TYPE_RUN, &INSTANCE_UUID,
			 NULL, NULL);
	struct xlog_opts opts = xlog_opts_default;
	opts.rate_limit = writer->run->env->snap_io_rate_limit;
	opts.sync_interval = VY_RUN_SYNC_INTERVAL;
	opts.no_compression = writer->no_compression;
	if (xlog_create(&writer->data_xlog, path, 0, &meta, &opts) != 0)
		return -1;
	return 0;
}

/**
 * Start a new page with a min_key stored in @a first_entry.
 * @param writer Run writer.
 * @param first_entry First statement of a page.
 *
 * @retval -1 Memory error.
 * @retval  0 Success.
 */
static int
vy_run_writer_start_page(struct vy_run_writer *writer,
			 struct vy_entry first_entry)
{
	struct vy_run *run = writer->run;
	if (run->info.page_count >= writer->page_info_capacity &&
	    vy_run_alloc_page_info(run, &writer->page_info_capacity) != 0)
		return -1;
	const char *key = vy_stmt_is_key(first_entry.stmt) ?
			  tuple_data(first_entry.stmt) :
			  tuple_extract_key(first_entry.stmt, writer->cmp_def,
					    vy_entry_multikey_idx(first_entry,
								  writer->cmp_def),
					    NULL);
	if (key == NULL)
		return -1;
	if (run->info.page_count == 0) {
		assert(run->info.min_key == NULL);
		run->info.min_key = vy_key_dup(key);
		if (run->info.min_key == NULL)
			return -1;
	}
	struct vy_page_info *page = run->page_info + run->info.page_count;
	if (vy_page_info_create(page, writer->data_xlog.offset,
				key, writer->cmp_def) != 0)
		return -1;
	xlog_tx_begin(&writer->data_xlog);
	return 0;
}

/**
 * Write @a stmt into a current page.
 * @param writer Run writer.
 * @param entry Statement to write.
 *
 * @retval -1 Memory or IO error.
 * @retval  0 Success.
 */
static int
vy_run_writer_write_to_page(struct vy_run_writer *writer, struct vy_entry entry)
{
	if (writer->bloom != NULL &&
	    vy_bloom_builder_add(writer->bloom, entry, writer->key_def) != 0)
		return -1;
	if (writer->last.stmt != NULL)
		vy_stmt_unref_if_possible(writer->last.stmt);
	writer->last = entry;
	vy_stmt_ref_if_possible(entry.stmt);
	struct vy_run *run = writer->run;
	struct vy_page_info *page = run->page_info + run->info.page_count;
	uint32_t *offset = (uint32_t *)ibuf_alloc(&writer->row_index_buf,
						  sizeof(uint32_t));
	if (offset == NULL) {
		diag_set(OutOfMemory, sizeof(uint32_t), "ibuf", "row index");
		return -1;
	}
	*offset = page->unpacked_size;
	if (vy_run_dump_stmt(entry, &writer->data_xlog, page,
			     writer->cmp_def, writer->iid == 0) != 0)
		return -1;
	int64_t lsn = vy_stmt_lsn(entry.stmt);
	run->info.min_lsn = MIN(run->info.min_lsn, lsn);
	run->info.max_lsn = MAX(run->info.max_lsn, lsn);
	vy_stmt_stat_acct(&run->info.stmt_stat, vy_stmt_type(entry.stmt));
	return 0;
}

/**
 * Finish a current page.
 * @param writer Run writer.
 * @retval -1 Memory or IO error.
 * @retval  0 Success.
 */
static int
vy_run_writer_end_page(struct vy_run_writer *writer)
{
	struct vy_run *run = writer->run;
	struct vy_page_info *page = run->page_info + run->info.page_count;

	assert(page->row_count > 0);
	assert(ibuf_used(&writer->row_index_buf) ==
	       sizeof(uint32_t) * page->row_count);

	struct xrow_header xrow;
	uint32_t *row_index = (uint32_t *)writer->row_index_buf.rpos;
	if (vy_row_index_encode(row_index, page->row_count, &xrow) < 0)
		return -1;
	ssize_t written = xlog_write_row(&writer->data_xlog, &xrow);
	if (written < 0)
		return -1;
	page->row_index_offset = page->unpacked_size;
	page->unpacked_size += written;

	written = xlog_tx_commit(&writer->data_xlog);
	if (written == 0)
		written = xlog_flush(&writer->data_xlog);
	if (written < 0)
		return -1;
	page->size = written;
	run->info.page_count++;
	vy_run_acct_page(run, page);
	ibuf_reset(&writer->row_index_buf);
	return 0;
}

int
vy_run_writer_append_stmt(struct vy_run_writer *writer, struct vy_entry entry)
{
	int rc = -1;
	size_t region_svp = region_used(&fiber()->gc);
	if (!xlog_is_open(&writer->data_xlog) &&
	    vy_run_writer_create_xlog(writer) != 0)
		goto out;
	if (ibuf_used(&writer->row_index_buf) == 0 &&
	    vy_run_writer_start_page(writer, entry) != 0)
		goto out;
	if (vy_run_writer_write_to_page(writer, entry) != 0)
		goto out;
	if (obuf_size(&writer->data_xlog.obuf) >= writer->page_size &&
	    vy_run_writer_end_page(writer) != 0)
		goto out;
	rc = 0;
out:
	region_truncate(&fiber()->gc, region_svp);
	return rc;
}

/**
 * Destroy a run writer.
 * @param writer Writer to destroy.
 * @param reuse_fd True in a case of success run write. And else
 *        false.
 */
static void
vy_run_writer_destroy(struct vy_run_writer *writer, bool reuse_fd)
{
	if (writer->last.stmt != NULL)
		vy_stmt_unref_if_possible(writer->last.stmt);
	if (xlog_is_open(&writer->data_xlog))
		xlog_close(&writer->data_xlog, reuse_fd);
	if (writer->bloom != NULL)
		tuple_bloom_builder_delete(writer->bloom);
	ibuf_destroy(&writer->row_index_buf);
}

int
vy_run_writer_commit(struct vy_run_writer *writer)
{
	int rc = -1;
	size_t region_svp = region_used(&fiber()->gc);

	if (ibuf_used(&writer->row_index_buf) != 0 &&
	    vy_run_writer_end_page(writer) != 0)
		goto out;

	struct vy_run *run = writer->run;
	if (vy_run_is_empty(run)) {
		vy_run_writer_destroy(writer, false);
		rc = 0;
		goto out;
	}

	assert(writer->last.stmt != NULL);
	const char *key = vy_stmt_is_key(writer->last.stmt) ?
		          tuple_data(writer->last.stmt) :
			  tuple_extract_key(writer->last.stmt, writer->cmp_def,
					    vy_entry_multikey_idx(writer->last,
								  writer->cmp_def),
					    NULL);
	if (key == NULL)
		goto out;

	assert(run->info.max_key == NULL);
	run->info.max_key = vy_key_dup(key);
	if (run->info.max_key == NULL)
		goto out;

	ERROR_INJECT(ERRINJ_VY_RUN_FILE_RENAME, {
		diag_set(ClientError, ER_INJECTION, "vinyl run file rename");
		goto out;
	});

	/* Sync data and link the file to the final name. */
	if (xlog_sync(&writer->data_xlog) < 0 ||
	    xlog_rename(&writer->data_xlog) < 0)
		goto out;

	if (writer->bloom != NULL) {
		run->info.bloom = tuple_bloom_new(writer->bloom,
						  writer->bloom_fpr);
		if (run->info.bloom == NULL)
			goto out;
	}
	if (vy_run_write_index(run, writer->dirpath,
			       writer->space_id, writer->iid) != 0)
		goto out;

	run->fd = writer->data_xlog.fd;
	vy_run_writer_destroy(writer, true);
	rc = 0;
out:
	region_truncate(&fiber()->gc, region_svp);
	return rc;
}

void
vy_run_writer_abort(struct vy_run_writer *writer)
{
	vy_run_writer_destroy(writer, false);
}

int
vy_run_rebuild_index(struct vy_run *run, const char *dir,
		     uint32_t space_id, uint32_t iid,
		     struct key_def *cmp_def, struct key_def *key_def,
		     struct tuple_format *format, const struct index_opts *opts)
{
	assert(run->info.bloom == NULL);
	assert(run->page_info == NULL);
	struct region *region = &fiber()->gc;
	size_t mem_used = region_used(region);

	struct xlog_cursor cursor;
	char path[PATH_MAX];
	vy_run_snprint_path(path, sizeof(path), dir,
			    space_id, iid, run->id, VY_FILE_RUN);

	say_info("rebuilding index for `%s'", path);
	if (xlog_cursor_open(&cursor, path))
		return -1;

	int rc = 0;
	uint32_t page_info_capacity = 0;

	const char *key = NULL;
	int64_t max_lsn = 0;
	int64_t min_lsn = INT64_MAX;
	struct tuple *prev_tuple = NULL;
	char *page_min_key = NULL;

	struct tuple_bloom_builder *bloom_builder = NULL;
	if (opts->bloom_fpr < 1) {
		bloom_builder = tuple_bloom_builder_new(key_def->part_count);
		if (bloom_builder == NULL)
			goto close_err;
	}

	off_t page_offset, next_page_offset = xlog_cursor_pos(&cursor);
	while ((rc = xlog_cursor_next_tx(&cursor)) == 0) {
		region_truncate(region, mem_used);
		page_offset = next_page_offset;
		next_page_offset = xlog_cursor_pos(&cursor);

		if (run->info.page_count == page_info_capacity &&
		    vy_run_alloc_page_info(run, &page_info_capacity) != 0)
			goto close_err;
		uint32_t page_row_count = 0;
		uint64_t page_row_index_offset = 0;
		uint64_t row_offset = xlog_cursor_tx_pos(&cursor);

		struct xrow_header xrow;
		while ((rc = xlog_cursor_next_row(&cursor, &xrow)) == 0) {
			if (xrow.type == VY_RUN_ROW_INDEX) {
				page_row_index_offset = row_offset;
				row_offset = xlog_cursor_tx_pos(&cursor);
				continue;
			}
			++page_row_count;
			struct tuple *tuple = vy_stmt_decode(&xrow, format);
			if (tuple == NULL)
				goto close_err;
			if (bloom_builder != NULL) {
				struct vy_entry entry = {tuple, HINT_NONE};
				if (vy_bloom_builder_add(bloom_builder, entry,
							 key_def) != 0) {
					tuple_unref(tuple);
					goto close_err;
				}
			}
			key = vy_stmt_is_key(tuple) ? tuple_data(tuple) :
			      tuple_extract_key(tuple, cmp_def,
						MULTIKEY_NONE, NULL);
			if (prev_tuple != NULL)
				tuple_unref(prev_tuple);
			prev_tuple = tuple;
			if (key == NULL)
				goto close_err;
			if (run->info.min_key == NULL) {
				run->info.min_key = vy_key_dup(key);
				if (run->info.min_key == NULL)
					goto close_err;
			}
			if (page_min_key == NULL) {
				page_min_key = vy_key_dup(key);
				if (page_min_key == NULL)
					goto close_err;
			}
			if (xrow.lsn > max_lsn)
				max_lsn = xrow.lsn;
			if (xrow.lsn < min_lsn)
				min_lsn = xrow.lsn;
			row_offset = xlog_cursor_tx_pos(&cursor);
		}
		struct vy_page_info *info;
		info = run->page_info + run->info.page_count;
		if (vy_page_info_create(info, page_offset,
					page_min_key, cmp_def) != 0)
			goto close_err;
		info->row_count = page_row_count;
		info->size = next_page_offset - page_offset;
		info->unpacked_size = xlog_cursor_tx_pos(&cursor);
		info->row_index_offset = page_row_index_offset;
		++run->info.page_count;
		vy_run_acct_page(run, info);

		free(page_min_key);
		page_min_key = NULL;
	}

	if (key != NULL) {
		run->info.max_key = vy_key_dup(key);
		if (run->info.max_key == NULL)
			goto close_err;
	}
	run->info.max_lsn = max_lsn;
	run->info.min_lsn = min_lsn;

	if (prev_tuple != NULL) {
		tuple_unref(prev_tuple);
		prev_tuple = NULL;
	}
	region_truncate(region, mem_used);
	run->fd = cursor.fd;
	xlog_cursor_close(&cursor, true);

	if (bloom_builder != NULL) {
		run->info.bloom = tuple_bloom_new(bloom_builder,
						  opts->bloom_fpr);
		if (run->info.bloom == NULL)
			goto close_err;
		tuple_bloom_builder_delete(bloom_builder);
		bloom_builder = NULL;
	}

	/* New run index is ready for write, unlink old file if exists */
	vy_run_snprint_path(path, sizeof(path), dir,
			    space_id, iid, run->id, VY_FILE_INDEX);
	if (unlink(path) < 0 && errno != ENOENT) {
		diag_set(SystemError, "failed to unlink file '%s'",
			 path);
		goto close_err;
	}
	if (vy_run_write_index(run, dir, space_id, iid) != 0)
		goto close_err;
	return 0;
close_err:
	vy_run_clear(run);
	region_truncate(region, mem_used);
	if (prev_tuple != NULL)
		tuple_unref(prev_tuple);
	if (page_min_key != NULL)
		free(page_min_key);
	if (bloom_builder != NULL)
		tuple_bloom_builder_delete(bloom_builder);
	if (xlog_cursor_is_open(&cursor))
		xlog_cursor_close(&cursor, false);
	return -1;
}

int
vy_run_remove_files(const char *dir, uint32_t space_id,
		    uint32_t iid, int64_t run_id)
{
	ERROR_INJECT(ERRINJ_VY_GC,
		     {say_error("error injection: vinyl run %lld not deleted",
				(long long)run_id); return -1;});
	int ret = 0;
	char path[PATH_MAX];
	for (int type = 0; type < vy_file_MAX; type++) {
		vy_run_snprint_path(path, sizeof(path), dir,
				    space_id, iid, run_id, type);
		if (coio_unlink(path) < 0) {
			if (errno != ENOENT) {
				say_syserror("error while removing %s", path);
				ret = -1;
			}
		} else
			say_info("removed %s", path);
	}
	return ret;
}

/**
 * Read a page with stream->page_no from the run and save it in stream->page.
 * Support function of slice stream.
 * @param stream - the stream.
 * @return 0 on success, -1 of memory or read error (diag is set).
 */
static NODISCARD int
vy_slice_stream_read_page(struct vy_slice_stream *stream)
{
	struct vy_run *run = stream->slice->run;

	assert(stream->page == NULL);
	ZSTD_DStream *zdctx = vy_env_get_zdctx(run->env);
	if (zdctx == NULL)
		return -1;

	struct vy_page_info *page_info = vy_run_page_info(run, stream->page_no);
	stream->page = vy_page_new(page_info);
	if (stream->page == NULL)
		return -1;

	if (vy_page_read(stream->page, page_info, run, zdctx) != 0) {
		vy_page_delete(stream->page);
		stream->page = NULL;
		return -1;
	}
	return 0;
}

/**
 * Binary search in a run for the given key. Find the first position with
 * a tuple greater or equal to slice
 * @retval 0 success
 * @retval -1 read or memory error
 */
static NODISCARD int
vy_slice_stream_search(struct vy_stmt_stream *virt_stream)
{
	assert(virt_stream->iface->start == vy_slice_stream_search);
	struct vy_slice_stream *stream = (struct vy_slice_stream *)virt_stream;
	assert(stream->page == NULL);
	if (stream->slice->begin.stmt == NULL) {
		/* Already at the beginning */
		assert(stream->page_no == 0);
		assert(stream->pos_in_page == 0);
		return 0;
	}

	if (vy_slice_stream_read_page(stream) != 0)
		return -1;

	bool unused;
	stream->pos_in_page = vy_page_find_key(stream->page,
					       stream->slice->begin,
					       stream->cmp_def,
					       stream->format,
					       ITER_GE, &unused);

	if (stream->pos_in_page == stream->page->row_count) {
		/* The first tuple is in the beginning of the next page */
		vy_page_delete(stream->page);
		stream->page = NULL;
		stream->page_no++;
		stream->pos_in_page = 0;
	}
	return 0;
}

/**
 * Get the value from the stream and move to the next position.
 * Set *ret to the value or NULL if EOF.
 * @param virt_stream - virtual stream.
 * @param ret - pointer to the result.
 * @return 0 on success, -1 on memory or read error.
 */
static NODISCARD int
vy_slice_stream_next(struct vy_stmt_stream *virt_stream, struct vy_entry *ret)
{
	assert(virt_stream->iface->next == vy_slice_stream_next);
	struct vy_slice_stream *stream = (struct vy_slice_stream *)virt_stream;
	*ret = vy_entry_none();

	/* If the slice is ended, return EOF */
	if (stream->page_no > stream->slice->last_page_no)
		return 0;

	/* If current page is not already read, read it */
	if (stream->page == NULL && vy_slice_stream_read_page(stream) != 0)
		return -1;

	/* Read current tuple from the page */
	struct vy_entry entry = vy_page_stmt(stream->page, stream->pos_in_page,
					     stream->cmp_def, stream->format);
	if (entry.stmt == NULL) /* Read or memory error */
		return -1;

	/* Check that the tuple is not out of slice bounds = */
	if (stream->slice->end.stmt != NULL &&
	    stream->page_no >= stream->slice->last_page_no &&
	    vy_entry_compare(entry, stream->slice->end, stream->cmp_def) >= 0) {
		tuple_unref(entry.stmt);
		return 0;
	}

	/* We definitely has the next non-null tuple. Save it in stream */
	if (stream->entry.stmt != NULL)
		tuple_unref(stream->entry.stmt);
	stream->entry = entry;
	*ret = entry;

	/* Increment position */
	stream->pos_in_page++;

	/* Check whether the position is out of page */
	struct vy_page_info *page_info = vy_run_page_info(stream->slice->run,
							  stream->page_no);
	if (stream->pos_in_page >= page_info->row_count) {
		/**
		 * Out of page. Free page, move the position to the next page
		 * and * nullify page pointer to read it on the next iteration.
		 */
		vy_page_delete(stream->page);
		stream->page = NULL;
		stream->page_no++;
		stream->pos_in_page = 0;
	}

	return 0;
}

/**
 * Free resources.
 */
static void
vy_slice_stream_stop(struct vy_stmt_stream *virt_stream)
{
	assert(virt_stream->iface->stop == vy_slice_stream_stop);
	struct vy_slice_stream *stream = (struct vy_slice_stream *)virt_stream;
	if (stream->page != NULL) {
		vy_page_delete(stream->page);
		stream->page = NULL;
	}
	if (stream->entry.stmt != NULL) {
		tuple_unref(stream->entry.stmt);
		stream->entry = vy_entry_none();
	}
}

static void
vy_slice_stream_close(struct vy_stmt_stream *virt_stream)
{
	assert(virt_stream->iface->close == vy_slice_stream_close);
	struct vy_slice_stream *stream = (struct vy_slice_stream *)virt_stream;
	tuple_format_unref(stream->format);
}

static const struct vy_stmt_stream_iface vy_slice_stream_iface = {
	.start = vy_slice_stream_search,
	.next = vy_slice_stream_next,
	.stop = vy_slice_stream_stop,
	.close = vy_slice_stream_close,
};

void
vy_slice_stream_open(struct vy_slice_stream *stream, struct vy_slice *slice,
		     struct key_def *cmp_def, struct tuple_format *format)
{
	stream->base.iface = &vy_slice_stream_iface;

	stream->page_no = slice->first_page_no;
	stream->pos_in_page = 0; /* We'll find it later */
	stream->page = NULL;
	stream->entry = vy_entry_none();

	stream->slice = slice;
	stream->cmp_def = cmp_def;
	stream->format = format;
	tuple_format_ref(format);
}
