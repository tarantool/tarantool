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
#include "fio.h"
#include "ipc.h"
#include "cbus.h"
#include "memory.h"
#include "cfg.h"

#include "replication.h"
#include "tuple_hash.h" /* for bloom filter */
#include "xlog.h"
#include "xrow.h"

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

enum { VY_BLOOM_VERSION = 0 };

/** xlog meta type for .run files */
#define XLOG_META_TYPE_RUN "RUN"

/** xlog meta type for .index files */
#define XLOG_META_TYPE_INDEX "INDEX"

const char *vy_file_suffix[] = {
	"index",	/* VY_FILE_INDEX */
	"run",		/* VY_FILE_RUN */
};

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
	struct vy_page_info page_info;
	/** vy_slice with fd - ref. counted */
	struct vy_slice *slice;
	/** vy_run_env - contains environment with task mempool */
	struct vy_run_env *run_env;
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
	env->reader_pool_size = cfg_geti("vinyl_read_threads");
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

		cbus_stop_loop(&reader->reader_pipe);
		cpipe_destroy(&reader->reader_pipe);
		if (cord_join(&reader->cord) != 0)
			panic("failed to join vinyl reader thread");
	}
	free(env->reader_pool);
}

/**
 * Initialize vinyl run environment
 */
void
vy_run_env_create(struct vy_run_env *env)
{
	tt_pthread_key_create(&env->zdctx_key, vy_free_zdctx);

	struct slab_cache *slab_cache = cord_slab_cache();
	mempool_create(&env->read_task_pool, slab_cache,
		       sizeof(struct vy_page_read_task));

	vy_run_env_start_readers(env);
}

/**
 * Destroy vinyl run environment
 */
void
vy_run_env_destroy(struct vy_run_env *env)
{
	vy_run_env_stop_readers(env);
	mempool_destroy(&env->read_task_pool);
	tt_pthread_key_delete(env->zdctx_key);
}

/**
 * Initialize page info struct
 *
 * @retval 0 for Success
 * @retval -1 for error
 */
static int
vy_page_info_create(struct vy_page_info *page_info, uint64_t offset,
		    const struct tuple *min_key, const struct key_def *key_def)
{
	memset(page_info, 0, sizeof(*page_info));
	page_info->offset = offset;
	page_info->unpacked_size = 0;
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	uint32_t size;
	const char *region_key = tuple_extract_key(min_key, key_def, &size);
	if (region_key == NULL)
		return -1;
	page_info->min_key = vy_key_dup(region_key);
	region_truncate(region, used);
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
vy_run_new(int64_t id)
{
	struct vy_run *run = calloc(1, sizeof(struct vy_run));
	if (unlikely(run == NULL)) {
		diag_set(OutOfMemory, sizeof(struct vy_run), "malloc",
			 "struct vy_run");
		return NULL;
	}
	run->id = id;
	run->dump_lsn = -1;
	run->fd = -1;
	run->refs = 1;
	rlist_create(&run->in_index);
	rlist_create(&run->in_unused);
	TRASH(&run->info.bloom);
	return run;
}

void
vy_run_delete(struct vy_run *run)
{
	assert(run->refs == 0);
	if (run->fd >= 0 && close(run->fd) < 0)
		say_syserror("close failed");
	if (run->page_info != NULL) {
		uint32_t page_no;
		for (page_no = 0; page_no < run->info.page_count; ++page_no)
			vy_page_info_destroy(run->page_info + page_no);
		free(run->page_info);
	}
	if (run->info.has_bloom)
		bloom_destroy(&run->info.bloom, runtime.quota);
	free(run->info.min_key);
	free(run->info.max_key);
	TRASH(run);
	free(run);
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
vy_page_index_find_page(struct vy_run *run, const struct tuple *key,
			const struct key_def *key_def,
			enum iterator_type itype, bool *equal_key)
{
	if (itype == ITER_EQ)
		itype = ITER_GE; /* One day it'll become obsolete */
	assert(itype == ITER_GE || itype == ITER_GT ||
	       itype == ITER_LE || itype == ITER_LT);
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
		int cmp = vy_stmt_compare_with_raw_key(key, info->min_key,
						       key_def);
		if (is_lower_bound)
			range[cmp <= 0] = mid;
		else
			range[cmp < 0] = mid;
		*equal_key = *equal_key || cmp == 0;
	} while (range[1] - range[0] > 1);
	if (range[0] < 0)
		range[0] = run->info.page_count;
	uint32_t page = range[iterator_direction(itype) > 0];

	/**
	 * Since page search uses only min_key of pages,
	 *  for GE, GT and EQ the previous page can contain
	 *  the point where iteration must be started.
	 */
	if (page > 0 && iterator_direction(itype) > 0)
		return page - 1;
	return page;
}

struct vy_slice *
vy_slice_new(int64_t id, struct vy_run *run,
	     struct tuple *begin, struct tuple *end,
	     const struct key_def *key_def)
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
	vy_run_ref(run);
	if (begin != NULL)
		tuple_ref(begin);
	slice->begin = begin;
	if (end != NULL)
		tuple_ref(end);
	slice->end = end;
	rlist_create(&slice->in_range);
	ipc_cond_create(&slice->pin_cond);
	/** Lookup the first and the last pages spanned by the slice. */
	bool unused;
	if (slice->begin == NULL) {
		slice->first_page_no = 0;
	} else {
		slice->first_page_no =
			vy_page_index_find_page(run, slice->begin, key_def,
						ITER_GE, &unused);
		assert(slice->last_page_no < run->info.page_count);
	}
	if (slice->end == NULL) {
		slice->last_page_no = run->info.page_count ?
				      run->info.page_count - 1 : 0;
	} else {
		slice->last_page_no =
			vy_page_index_find_page(run, slice->end, key_def,
						ITER_LT, &unused);
		if (slice->last_page_no == run->info.page_count) {
			/* It's an empty slice */
			slice->last_page_no = 0;
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
	vy_run_unref(slice->run);
	if (slice->begin != NULL)
		tuple_unref(slice->begin);
	if (slice->end != NULL)
		tuple_unref(slice->end);
	ipc_cond_destroy(&slice->pin_cond);
	TRASH(slice);
	free(slice);
}

int
vy_slice_cut(struct vy_slice *slice, int64_t id,
	     struct tuple *begin, struct tuple *end,
	     const struct key_def *key_def,
	     struct vy_slice **result)
{
	*result = NULL;

	if (begin != NULL && slice->end != NULL &&
	    vy_key_compare(begin, slice->end, key_def) >= 0)
		return 0; /* no intersection: begin >= slice->end */

	if (end != NULL && slice->begin != NULL &&
	    vy_key_compare(end, slice->begin, key_def) <= 0)
		return 0; /* no intersection: end <= slice->end */

	/* begin = MAX(begin, slice->begin) */
	if (slice->begin != NULL &&
	    (begin == NULL || vy_key_compare(begin, slice->begin, key_def) < 0))
		begin = slice->begin;

	/* end = MIN(end, slice->end) */
	if (slice->end != NULL &&
	    (end == NULL || vy_key_compare(end, slice->end, key_def) > 0))
		end = slice->end;

	*result = vy_slice_new(id, slice->run, begin, end, key_def);
	if (*result == NULL)
		return -1; /* OOM */

	return 0;
}

/**
 * Decode page information from xrow.
 *
 * @param[out] page Page information.
 * @param xrow      Xrow to decode.
 * @param filename  Filename for error reporting.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
static int
vy_page_info_decode(struct vy_page_info *page, const struct xrow_header *xrow,
		    const char *filename)
{
	assert(xrow->type == VY_INDEX_PAGE_INFO);
	const char *pos = xrow->body->iov_base;
	memset(page, 0, sizeof(*page));
	uint64_t key_map = vy_page_info_key_map;
	uint32_t map_size = mp_decode_map(&pos);
	uint32_t map_item;
	const char *key_beg;
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
			break;
		case VY_PAGE_INFO_UNPACKED_SIZE:
			page->unpacked_size = mp_decode_uint(&pos);
			break;
		case VY_PAGE_INFO_ROW_INDEX_OFFSET:
			page->row_index_offset = mp_decode_uint(&pos);
			break;
		default:
			diag_set(ClientError, ER_INVALID_INDEX_FILE, filename,
				 tt_sprintf("Can't decode page info: "
					    "unknown key %u", (unsigned)key));
			return -1;
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

/**
 * Read bloom filter from given buffer.
 * @param bloom - a bloom filter to read.
 * @param buffer[in/out] - a buffer to read from.
 *  The pointer is incremented on the number of bytes read.
 * @param filename Filename for error reporting.
 * @return - 0 on success or -1 on format/memory error
 */
static int
vy_run_bloom_decode(struct bloom *bloom, const char **buffer,
		    const char *filename)
{
	const char **pos = buffer;
	memset(bloom, 0, sizeof(*bloom));
	uint32_t array_size = mp_decode_array(pos);
	if (array_size != 4) {
		diag_set(ClientError, ER_INVALID_INDEX_FILE, filename,
			 tt_sprintf("Can't decode bloom meta: "
				    "wrong array size (expected %d, got %u)",
				    4, (unsigned)array_size));
		return -1;
	}
	uint64_t version = mp_decode_uint(pos);
	if (version != VY_BLOOM_VERSION) {
		diag_set(ClientError, ER_INVALID_INDEX_FILE, filename,
			 tt_sprintf("Can't decode bloom meta: "
				    "wrong version (expected %d, got %u)",
				    VY_BLOOM_VERSION, (unsigned)version));
	}
	bloom->table_size = mp_decode_uint(pos);
	bloom->hash_count = mp_decode_uint(pos);
	size_t table_size = mp_decode_binl(pos);
	if (table_size != bloom_store_size(bloom)) {
		diag_set(ClientError, ER_INVALID_INDEX_FILE, filename,
			 tt_sprintf("Can't decode bloom meta: "
				    "wrong table size (expected %zu, got %zu)",
				    bloom_store_size(bloom), table_size));
		return -1;
	}
	if (bloom_load_table(bloom, *pos, runtime.quota) != 0) {
		diag_set(OutOfMemory, bloom_store_size(bloom), "mmap", "bloom");
		return -1;
	}
	*pos += table_size;
	return 0;
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
		case VY_RUN_INFO_BLOOM:
			if (vy_run_bloom_decode(&run_info->bloom, &pos,
						filename) == 0)
				run_info->has_bloom = true;
			else
				return -1;
			break;
		default:
			diag_set(ClientError, ER_INVALID_INDEX_FILE, filename,
				"Can't decode run info: unknown key %u",
				(unsigned)key);
			return -1;
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
	return xrow_header_decode(xrow, &data, data_end);
}

/* {{{ vy_run_iterator vy_run_iterator support functions */

/**
 * Read raw stmt data from the page
 * @param page          Page.
 * @param stmt_no       Statement position in the page.
 * @param key_def       Key definition of an index.
 * @param format        Format for REPLACE/DELETE tuples.
 * @param upsert_format Format for UPSERT tuples.
 * @param is_primary    True if the index is primary.
 *
 * @retval not NULL Statement read from page.
 * @retval     NULL Memory error.
 */
static struct tuple *
vy_page_stmt(struct vy_page *page, uint32_t stmt_no,
	     const struct key_def *key_def, struct tuple_format *format,
	     struct tuple_format *upsert_format, bool is_primary)
{
	struct xrow_header xrow;
	if (vy_page_xrow(page, stmt_no, &xrow) != 0)
		return NULL;
	struct tuple_format *format_to_use = (xrow.type == IPROTO_UPSERT)
					     ? upsert_format : format;
	return vy_stmt_decode(&xrow, key_def, format_to_use, is_primary);
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
	if (itr->curr_stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
		itr->curr_stmt_pos.page_no = UINT32_MAX;
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
		/* TODO: report filename */
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

/**
 * Read a page requests from vinyl xlog data file.
 *
 * @retval 0 on success
 * @retval -1 on error, check diag
 */
static int
vy_page_read(struct vy_page *page, const struct vy_page_info *page_info, int fd,
	     ZSTD_DStream *zdctx)
{
	/* read xlog tx from xlog file */
	size_t region_svp = region_used(&fiber()->gc);
	char *data = (char *)region_alloc(&fiber()->gc, page_info->size);
	if (data == NULL) {
		diag_set(OutOfMemory, page_info->size, "region gc", "page");
		return -1;
	}
	ssize_t readen = fio_pread(fd, data, page_info->size,
				   page_info->offset);
	if (readen < 0) {
		/* TODO: report filename */
		diag_set(SystemError, "failed to read from file");
		goto error;
	}
	if (readen != (ssize_t)page_info->size) {
		/* TODO: replace with XlogError, report filename */
		diag_set(ClientError, ER_INVALID_RUN_FILE,
			 "Unexpected end of file");
		goto error;
	}
	ERROR_INJECT(ERRINJ_VY_READ_PAGE_TIMEOUT, {usleep(50000);});

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
	if (xrow_header_decode(&xrow, &data_pos, data_end) == -1)
		goto error;
	if (xrow.type != VY_RUN_ROW_INDEX) {
		/* TODO: report filename */
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
	ZSTD_DStream *zdctx = vy_env_get_zdctx(task->run_env);
	if (zdctx == NULL)
		return -1;
	return vy_page_read(task->page, &task->page_info,
			    task->slice->run->fd, zdctx);
}

/**
 * vinyl read task cleanup callback
 */
static int
vy_page_read_cb_free(struct cbus_call_msg *base)
{
	struct vy_page_read_task *task = (struct vy_page_read_task *)base;
	vy_page_delete(task->page);
	vy_slice_unpin(task->slice);
	mempool_free(&task->run_env->read_task_pool, task);
	return 0;
}

/**
 * Get a page by the given number the cache or load it from the disk.
 *
 * @retval 0 success
 * @retval -1 critical error
 */
static NODISCARD int
vy_run_iterator_load_page(struct vy_run_iterator *itr, uint32_t page_no,
			  struct vy_page **result)
{
	struct vy_run_env *env = itr->run_env;
	struct vy_slice *slice = itr->slice;

	/* Check cache */
	*result = vy_run_iterator_cache_get(itr, page_no);
	if (*result != NULL)
		return 0;

	/* Allocate buffers */
	struct vy_page_info *page_info = vy_run_page_info(slice->run, page_no);
	struct vy_page *page = vy_page_new(page_info);
	if (page == NULL)
		return -1;

	/* Read page data from the disk */
	int rc;
	if (itr->coio_read) {
		/* Allocate a cbus task. */
		struct vy_page_read_task *task;
		task = mempool_alloc(&env->read_task_pool);
		if (task == NULL) {
			diag_set(OutOfMemory, sizeof(*task), "mempool",
				 "vy_page_read_task");
			vy_page_delete(page);
			return -1;
		}

		/* Pick a reader thread. */
		struct vy_run_reader *reader;
		reader = &env->reader_pool[env->next_reader++];
		env->next_reader %= env->reader_pool_size;

		/*
		 * Make sure the run file descriptor won't be closed
		 * (even worse, reopened) while a reader thread is
		 * reading it.
		 */
		vy_slice_pin(slice);

		task->slice = slice;
		task->page_info = *page_info;
		task->run_env = env;
		task->page = page;

		/* Post task to the reader thread. */
		rc = cbus_call(&reader->reader_pipe, &reader->tx_pipe,
			       &task->base, vy_page_read_cb,
			       vy_page_read_cb_free, TIMEOUT_INFINITY);
		if (!task->base.complete)
			return -1; /* timed out or cancelled */

		mempool_free(&env->read_task_pool, task);
		vy_slice_unpin(slice);

		if (rc != 0) {
			/* posted, but failed */
			vy_page_delete(page);
			return -1;
		}
	} else {
		/*
		 * Optimization: use blocked I/O for non-TX threads or
		 * during WAL recovery (env->status != VINYL_ONLINE).
		 */
		ZSTD_DStream *zdctx = vy_env_get_zdctx(env);
		if (zdctx == NULL) {
			vy_page_delete(page);
			return -1;
		}
		if (vy_page_read(page, page_info, slice->run->fd, zdctx) != 0) {
			vy_page_delete(page);
			return -1;
		}
	}

	/* Iterator is never used from multiple fibers */
	assert(vy_run_iterator_cache_get(itr, page_no) == NULL);

	/* Update cache */
	vy_run_iterator_cache_put(itr, page, page_no);

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
		     struct tuple **stmt)
{
	struct vy_page *page;
	int rc = vy_run_iterator_load_page(itr, pos.page_no, &page);
	if (rc != 0)
		return rc;
	*stmt = vy_page_stmt(page, pos.pos_in_page, itr->key_def,
			     itr->format, itr->upsert_format, itr->is_primary);
	if (*stmt == NULL)
		return -1;
	return 0;
}

/**
 * Binary search in page
 * In terms of STL, makes lower_bound for EQ,GE,LT and upper_bound for GT,LE
 * Additionally *equal_key argument is set to true if the found value is
 * equal to given key (untouched otherwise)
 * @retval position in the page
 */
static uint32_t
vy_run_iterator_search_in_page(struct vy_run_iterator *itr,
			       enum iterator_type iterator_type,
			       const struct tuple *key,
			       struct vy_page *page, bool *equal_key)
{
	uint32_t beg = 0;
	uint32_t end = page->row_count;
	/* for upper bound we change zero comparison result to -1 */
	int zero_cmp = (iterator_type == ITER_GT ||
			iterator_type == ITER_LE ? -1 : 0);
	while (beg != end) {
		uint32_t mid = beg + (end - beg) / 2;
		struct tuple *fnd_key = vy_page_stmt(page, mid, itr->key_def,
						     itr->format, itr->upsert_format,
						     itr->is_primary);
		if (fnd_key == NULL)
			return end;
		int cmp = vy_stmt_compare(fnd_key, key, itr->key_def);
		cmp = cmp ? cmp : zero_cmp;
		*equal_key = *equal_key || cmp == 0;
		if (cmp < 0)
			beg = mid + 1;
		else
			end = mid;
		tuple_unref(fnd_key);
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
static NODISCARD int
vy_run_iterator_search(struct vy_run_iterator *itr,
		       enum iterator_type iterator_type,
		       const struct tuple *key,
		       struct vy_run_iterator_pos *pos, bool *equal_key)
{
	pos->page_no = vy_page_index_find_page(itr->slice->run, key,
					       itr->key_def, iterator_type,
					       equal_key);
	if (pos->page_no == itr->slice->run->info.page_count)
		return 0;
	struct vy_page *page;
	int rc = vy_run_iterator_load_page(itr, pos->page_no, &page);
	if (rc != 0)
		return rc;
	bool equal_in_page = false;
	pos->pos_in_page = vy_run_iterator_search_in_page(itr, iterator_type,
							  key, page,
							  &equal_in_page);
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
	assert(pos->page_no < run->info.page_count);
	if (iterator_type == ITER_LE || iterator_type == ITER_LT) {
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

static NODISCARD int
vy_run_iterator_get(struct vy_run_iterator *itr, struct tuple **result);

/**
 * Find the next record with lsn <= itr->lsn record.
 * The current position must be at the beginning of a series of
 * records with the same key it terms of direction of iterator
 * (i.e. left for GE, right for LE).
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read or memory error
 * Affects: curr_loaded_page, curr_pos, search_ended
 */
static NODISCARD int
vy_run_iterator_find_lsn(struct vy_run_iterator *itr,
			 enum iterator_type iterator_type,
			 const struct tuple *key, struct tuple **ret)
{
	struct vy_slice *slice = itr->slice;
	assert(itr->curr_pos.page_no < slice->run->info.page_count);
	struct tuple *stmt;
	const struct key_def *key_def = itr->key_def;
	*ret = NULL;
	int rc = vy_run_iterator_read(itr, itr->curr_pos, &stmt);
	if (rc != 0)
		return rc;
	while (vy_stmt_lsn(stmt) > (**itr->read_view).vlsn) {
		tuple_unref(stmt);
		stmt = NULL;
		rc = vy_run_iterator_next_pos(itr, iterator_type,
					      &itr->curr_pos);
		if (rc > 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 0;
		}
		assert(rc == 0);
		rc = vy_run_iterator_read(itr, itr->curr_pos, &stmt);
		if (rc != 0)
			return rc;
		if (iterator_type == ITER_EQ &&
		    vy_stmt_compare(stmt, key, key_def)) {
			tuple_unref(stmt);
			stmt = NULL;
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 0;
		}
	}
	if (iterator_type == ITER_LE || iterator_type == ITER_LT) {
		/* Remember the page_no of stmt */
		uint32_t cur_key_page_no = itr->curr_pos.page_no;

		struct vy_run_iterator_pos test_pos;
		rc = vy_run_iterator_next_pos(itr, iterator_type, &test_pos);
		while (rc == 0) {
			/*
			 * The cache is at least two pages. Ensure that
			 * subsequent read keeps the stmt in the cache
			 * by moving its page to the start of LRU list.
			 */
			vy_run_iterator_cache_touch(itr, cur_key_page_no);

			struct tuple *test_stmt;
			rc = vy_run_iterator_read(itr, test_pos, &test_stmt);
			if (rc != 0)
				return rc;
			if (vy_stmt_lsn(test_stmt) > (**itr->read_view).vlsn ||
			    vy_tuple_compare(stmt, test_stmt, key_def) != 0) {
				tuple_unref(test_stmt);
				test_stmt = NULL;
				break;
			}
			tuple_unref(test_stmt);
			test_stmt = NULL;
			itr->curr_pos = test_pos;

			/* See above */
			vy_run_iterator_cache_touch(itr, cur_key_page_no);

			rc = vy_run_iterator_next_pos(itr, iterator_type,
						      &test_pos);
		}

		rc = rc > 0 ? 0 : rc;
	}
	tuple_unref(stmt);
	if (!rc) /* If next_pos() found something then get it. */
		rc = vy_run_iterator_get(itr, ret);
	if (rc != 0 || *ret == NULL)
		return rc;
	/* Check if the result is within the slice boundaries. */
	if (iterator_type == ITER_LE || iterator_type == ITER_LT) {
		if (slice->begin != NULL &&
		    vy_stmt_compare_with_key(*ret, slice->begin, key_def) < 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			*ret = NULL;
			return 0;
		}
	} else {
		assert(iterator_type == ITER_GE || iterator_type == ITER_GT ||
		       iterator_type == ITER_EQ);
		if (slice->end != NULL &&
		    vy_stmt_compare_with_key(*ret, slice->end, key_def) >= 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			*ret = NULL;
			return 0;
		}
	}
	return 0;
}

/*
 * FIXME: vy_run_iterator_next_key() calls vy_run_iterator_start() which
 * recursivly calls vy_run_iterator_next_key().
 */
static NODISCARD int
vy_run_iterator_next_key(struct vy_stmt_iterator *vitr, struct tuple **ret,
			 bool *stop);
/**
 * Start iteration for a given key and direction.
 * Note, this function doesn't check slice boundaries.
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read or memory error
 * Affects: curr_loaded_page, curr_pos, search_ended
 */
static NODISCARD int
vy_run_iterator_start_from(struct vy_run_iterator *itr,
			   enum iterator_type iterator_type,
			   const struct tuple *key, struct tuple **ret)
{
	struct vy_run *run = itr->slice->run;

	assert(!itr->search_started);
	itr->search_started = true;
	*ret = NULL;

	const struct key_def *user_key_def = itr->user_key_def;
	bool is_full_key = (tuple_field_count(key) >= user_key_def->part_count);
	if (run->info.has_bloom && iterator_type == ITER_EQ && is_full_key) {
		uint32_t hash;
		if (vy_stmt_type(key) == IPROTO_SELECT) {
			const char *data = tuple_data(key);
			mp_decode_array(&data);
			hash = key_hash(data, user_key_def);
		} else {
			hash = tuple_hash(key, user_key_def);
		}
		if (!bloom_possible_has(&run->info.bloom, hash)) {
			itr->search_ended = true;
			itr->stat->bloom_hit++;
			return 0;
		}
	}

	itr->stat->lookup++;

	if (run->info.page_count == 1) {
		/* there can be a stupid bootstrap run in which it's EOF */
		struct vy_page_info *page_info = run->page_info;

		if (page_info->row_count == 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 0;
		}
		struct vy_page *page;
		int rc = vy_run_iterator_load_page(itr, 0, &page);
		if (rc != 0)
			return rc;
	} else if (run->info.page_count == 0) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		return 0;
	}

	struct vy_run_iterator_pos end_pos = {run->info.page_count, 0};
	bool equal_found = false;
	int rc;
	if (tuple_field_count(key) > 0) {
		rc = vy_run_iterator_search(itr, iterator_type, key,
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
	if (iterator_type == ITER_EQ && !equal_found) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		if (run->info.has_bloom && is_full_key)
			itr->stat->bloom_miss++;
		return 0;
	}
	if ((iterator_type == ITER_GE || iterator_type == ITER_GT) &&
	    itr->curr_pos.page_no == end_pos.page_no) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		return 0;
	}
	if (iterator_type == ITER_LT || iterator_type == ITER_LE) {
		/**
		 * 1) in case of ITER_LT we now positioned on the value >= than
		 * given, so we need to make a step on previous key
		 * 2) in case if ITER_LE we now positioned on the value > than
		 * given (special branch of code in vy_run_iterator_search),
		 * so we need to make a step on previous key
		 */
		return vy_run_iterator_next_key(&itr->base, ret, NULL);
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
		return vy_run_iterator_find_lsn(itr, iterator_type, key, ret);
	}
}

/**
 * Start iteration in a run taking into account slice boundaries.
 * This function is a wrapper around vy_run_iterator_start_from()
 * which passes a contrived search key and the iterator
 * direction to make sure the result falls in the given slice.
 */
static NODISCARD int
vy_run_iterator_start(struct vy_run_iterator *itr, struct tuple **ret)
{
	enum iterator_type iterator_type = itr->iterator_type;
	const struct tuple *key = itr->key;
	const struct key_def *key_def = itr->key_def;
	struct vy_slice *slice = itr->slice;
	int cmp;

	if (slice->begin != NULL &&
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
		cmp = vy_stmt_compare_with_key(key, slice->begin, key_def);
		if (cmp < 0 && iterator_type == ITER_EQ) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 0;
		}
		if (cmp < 0 || (cmp == 0 && iterator_type != ITER_GT)) {
			iterator_type = ITER_GE;
			key = slice->begin;
		}
	}

	if (slice->end != NULL &&
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
		cmp = vy_stmt_compare_with_key(key, slice->end, key_def);
		if (cmp > 0 || (cmp == 0 && iterator_type != ITER_LT)) {
			iterator_type = ITER_LT;
			key = slice->end;
		}
	}

	return vy_run_iterator_start_from(itr, iterator_type, key, ret);
}

/* }}} vy_run_iterator vy_run_iterator support functions */

/* {{{ vy_run_iterator API implementation */

/** Vtable for vy_stmt_iterator - declared below */
static struct vy_stmt_iterator_iface vy_run_iterator_iface;

/**
 * Open the iterator.
 */
void
vy_run_iterator_open(struct vy_run_iterator *itr, bool coio_read,
		     struct vy_run_iterator_stat *stat, struct vy_run_env *run_env,
		     struct vy_slice *slice, enum iterator_type iterator_type,
		     const struct tuple *key, const struct vy_read_view **rv,
		     const struct key_def *key_def,
		     const struct key_def *user_key_def,
		     struct tuple_format *format,
		     struct tuple_format *upsert_format,
		     bool is_primary)
{
	itr->base.iface = &vy_run_iterator_iface;
	itr->stat = stat;
	itr->key_def = key_def;
	itr->user_key_def = user_key_def;
	itr->format = format;
	itr->upsert_format = upsert_format;
	itr->is_primary = is_primary;
	itr->run_env = run_env;
	itr->slice = slice;
	itr->coio_read = coio_read;

	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;
	if (tuple_field_count(key) == 0) {
		/* NULL key. change itr->iterator_type for simplification */
		itr->iterator_type = iterator_type == ITER_LT ||
				     iterator_type == ITER_LE ?
				     ITER_LE : ITER_GE;
	}

	itr->curr_stmt = NULL;
	itr->curr_pos.page_no = slice->run->info.page_count;
	itr->curr_stmt_pos.page_no = UINT32_MAX;
	itr->curr_page = NULL;
	itr->prev_page = NULL;

	itr->search_started = false;
	itr->search_ended = false;
}

/**
 * Create a stmt object from a its impression on a run page.
 * Uses the current iterator position in the page.
 *
 * @retval 0 success or EOF (*result == NULL)
 * @retval -1 memory or read error
 */
static NODISCARD int
vy_run_iterator_get(struct vy_run_iterator *itr, struct tuple **result)
{
	assert(itr->search_started);
	*result = NULL;
	if (itr->search_ended)
		return 0;
	if (itr->curr_stmt != NULL) {
		if (itr->curr_stmt_pos.page_no == itr->curr_pos.page_no &&
		    itr->curr_stmt_pos.pos_in_page == itr->curr_pos.pos_in_page) {
			*result = itr->curr_stmt;
			return 0;
		}
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = NULL;
		itr->curr_stmt_pos.page_no = UINT32_MAX;
	}
	int rc = vy_run_iterator_read(itr, itr->curr_pos, result);
	if (rc == 0) {
		itr->curr_stmt_pos = itr->curr_pos;
		itr->curr_stmt = *result;
		itr->stat->get.rows++;
		itr->stat->get.bytes += tuple_size(*result);
	}
	return rc;
}

/**
 * Find the next stmt in a page, i.e. a stmt with a different key
 * and fresh enough LSN (i.e. skipping the keys
 * too old for the current transaction).
 *
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 memory or read error
 */
static NODISCARD int
vy_run_iterator_next_key(struct vy_stmt_iterator *vitr, struct tuple **ret,
			 bool *stop)
{
	(void)stop;
	assert(vitr->iface->next_key == vy_run_iterator_next_key);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;
	*ret = NULL;
	int rc;

	if (itr->search_ended)
		return 0;
	if (!itr->search_started)
		return vy_run_iterator_start(itr, ret);
	uint32_t end_page = itr->slice->run->info.page_count;
	assert(itr->curr_pos.page_no <= end_page);
	const struct key_def *key_def = itr->key_def;
	if (itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT) {
		if (itr->curr_pos.page_no == 0 &&
		    itr->curr_pos.pos_in_page == 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			return 0;
		}
		if (itr->curr_pos.page_no == end_page) {
			/* A special case for reverse iterators */
			uint32_t page_no = end_page - 1;
			struct vy_page *page;
			int rc = vy_run_iterator_load_page(itr, page_no, &page);
			if (rc != 0)
				return rc;
			if (page->row_count == 0) {
				vy_run_iterator_cache_clean(itr);
				itr->search_ended = true;
				return 0;
			}
			itr->curr_pos.page_no = page_no;
			itr->curr_pos.pos_in_page = page->row_count - 1;
			return vy_run_iterator_find_lsn(itr, itr->iterator_type,
							itr->key, ret);
		}
	}
	assert(itr->curr_pos.page_no < end_page);

	struct tuple *cur_key;
	rc = vy_run_iterator_read(itr, itr->curr_pos, &cur_key);
	if (rc != 0)
		return rc;
	uint32_t cur_key_page_no = itr->curr_pos.page_no;

	struct tuple *next_key = NULL;
	do {
		if (next_key != NULL)
			tuple_unref(next_key);
		next_key = NULL;
		int rc = vy_run_iterator_next_pos(itr, itr->iterator_type,
						  &itr->curr_pos);
		if (rc > 0) {
			vy_run_iterator_cache_clean(itr);
			itr->search_ended = true;
			tuple_unref(cur_key);
			cur_key = NULL;
			return 0;
		}

		/*
		 * The cache is at least two pages. Ensure that
		 * subsequent read keeps the cur_key in the cache
		 * by moving its page to the start of LRU list.
		 */
		vy_run_iterator_cache_touch(itr, cur_key_page_no);

		rc = vy_run_iterator_read(itr, itr->curr_pos, &next_key);
		if (rc != 0) {
			tuple_unref(cur_key);
			cur_key = NULL;
			return rc;
		}

		/* See above */
		vy_run_iterator_cache_touch(itr, cur_key_page_no);
	} while (vy_tuple_compare(cur_key, next_key, key_def) == 0);
	tuple_unref(cur_key);
	cur_key = NULL;
	if (itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(next_key, itr->key, key_def) != 0) {
		vy_run_iterator_cache_clean(itr);
		itr->search_ended = true;
		tuple_unref(next_key);
		next_key = NULL;
		return 0;
	}
	tuple_unref(next_key);
	next_key = NULL;
	return vy_run_iterator_find_lsn(itr, itr->iterator_type, itr->key, ret);
}

/**
 * Find next (lower, older) record with the same key as current
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 memory or read error
 */
static NODISCARD int
vy_run_iterator_next_lsn(struct vy_stmt_iterator *vitr, struct tuple **ret)
{
	assert(vitr->iface->next_lsn == vy_run_iterator_next_lsn);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;
	*ret = NULL;
	int rc;

	if (itr->search_ended)
		return 0;
	if (!itr->search_started)
		return vy_run_iterator_start(itr, ret);
	assert(itr->curr_pos.page_no < itr->slice->run->info.page_count);

	struct vy_run_iterator_pos next_pos;
	rc = vy_run_iterator_next_pos(itr, ITER_GE, &next_pos);
	if (rc > 0)
		return 0;

	struct tuple *cur_key;
	rc = vy_run_iterator_read(itr, itr->curr_pos, &cur_key);
	if (rc != 0)
		return rc;

	struct tuple *next_key;
	rc = vy_run_iterator_read(itr, next_pos, &next_key);
	if (rc != 0) {
		tuple_unref(cur_key);
		return rc;
	}

	/**
	 * One can think that we had to lock page of itr->curr_pos,
	 *  to prevent freeing cur_key with entire page and avoid
	 *  segmentation fault in vy_stmt_compare_raw.
	 * But in fact the only case when curr_pos and next_pos
	 *  point to different pages is the case when next_pos points
	 *  to the beginning of the next page, and in this case
	 *  vy_run_iterator_read will read data from page index, not the page.
	 *  So in the case no page will be unloaded and we don't need
	 *  page lock
	 */
	int cmp = vy_tuple_compare(cur_key, next_key, itr->key_def);
	tuple_unref(cur_key);
	cur_key = NULL;
	tuple_unref(next_key);
	next_key = NULL;
	itr->curr_pos = cmp == 0 ? next_pos : itr->curr_pos;
	rc = cmp != 0;
	if (rc != 0)
		return 0;
	return vy_run_iterator_get(itr, ret);
}

/**
 * Restore the current position (if necessary) after a change in the set of
 * runs or ranges and check if the position was changed.
 * @sa struct vy_stmt_iterator comments.
 *
 * @pre the iterator is not started
 *
 * @param last_stmt the last key on which the iterator was
 *		      positioned
 *
 * @retval 0	if position did not change (iterator started)
 * @retval 1	if position changed
 * @retval -1	a read or memory error
 */
static NODISCARD int
vy_run_iterator_restore(struct vy_stmt_iterator *vitr,
			const struct tuple *last_stmt, struct tuple **ret,

			bool *stop)
{
	(void)stop;
	assert(vitr->iface->restore == vy_run_iterator_restore);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;
	*ret = NULL;
	int rc;

	if (itr->search_started || last_stmt == NULL) {
		if (!itr->search_started) {
			rc = vy_run_iterator_start(itr, ret);
		} else {
			rc = vy_run_iterator_get(itr, ret);
		}
		if (rc < 0)
			return rc;
		return 0;
	}
	/* Restoration is very similar to first search so we'll use that */
	enum iterator_type iterator_type = itr->iterator_type;
	if (iterator_type == ITER_GT || iterator_type == ITER_EQ)
		iterator_type = ITER_GE;
	else if (iterator_type == ITER_LT)
		iterator_type = ITER_LE;
	struct tuple *next;
	rc = vy_run_iterator_start_from(itr, iterator_type, last_stmt, &next);
	if (rc != 0)
		return rc;
	else if (next == NULL)
		return 0;
	const struct key_def *def = itr->key_def;
	bool position_changed = true;
	if (vy_stmt_compare(next, last_stmt, def) == 0) {
		position_changed = false;
		if (vy_stmt_lsn(next) >= vy_stmt_lsn(last_stmt)) {
			/* skip the same stmt to next stmt or older version */
			do {
				rc = vy_run_iterator_next_lsn(vitr, &next);
				if (rc != 0)
					return rc;
				if (next == NULL) {
					rc = vy_run_iterator_next_key(vitr,
								      &next,
								      NULL);
					if (rc != 0)
						return rc;
					break;
				}
			} while (vy_stmt_lsn(next) >= vy_stmt_lsn(last_stmt));
			if (next != NULL)
				position_changed = true;
		}
	} else if (itr->iterator_type == ITER_EQ &&
		   vy_stmt_compare(itr->key, next, def) != 0) {

		itr->search_ended = true;
		vy_run_iterator_cache_clean(itr);
		return position_changed;
	}
	*ret = next;
	return position_changed;
}

/**
 * Free all allocated resources in a worker thread.
 */
static void
vy_run_iterator_cleanup(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->cleanup == vy_run_iterator_cleanup);
	vy_run_iterator_cache_clean((struct vy_run_iterator *) vitr);
}

/**
 * Close the iterator and free resources.
 * Can be called only after cleanup().
 */
static void
vy_run_iterator_close(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->close == vy_run_iterator_close);
	struct vy_run_iterator *itr = (struct vy_run_iterator *) vitr;
	/* cleanup() must be called before */
	assert(itr->curr_stmt == NULL && itr->curr_page == NULL);
	TRASH(itr);
	(void) itr;
}

static struct vy_stmt_iterator_iface vy_run_iterator_iface = {
	.next_key = vy_run_iterator_next_key,
	.next_lsn = vy_run_iterator_next_lsn,
	.restore = vy_run_iterator_restore,
	.cleanup = vy_run_iterator_cleanup,
	.close = vy_run_iterator_close,
};

/* }}} vy_run_iterator API implementation */

/** Account a page to run statistics. */
static void
vy_run_acct_page(struct vy_run *run, struct vy_page_info *page)
{
	run->count.rows += page->row_count;
	run->count.bytes += page->unpacked_size;
	run->count.bytes_compressed += page->size;
	run->count.pages++;
}

int
vy_run_recover(struct vy_run *run, const char *dir,
	       uint32_t space_id, uint32_t iid)
{
	char path[PATH_MAX];
	vy_run_snprint_path(path, sizeof(path), dir,
			    space_id, iid, run->id, VY_FILE_INDEX);

	struct xlog_cursor cursor;
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
		if (vy_page_info_decode(page, &xrow, path) < 0) {
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
	return -1;
}

/* dump statement to the run page buffers (stmt header and data) */
static int
vy_run_dump_stmt(const struct tuple *value, struct xlog *data_xlog,
		 struct vy_page_info *info, const struct key_def *key_def,
		 bool is_primary)
{
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);

	struct xrow_header xrow;
	int rc = (is_primary ?
		  vy_stmt_encode_primary(value, key_def, 0, &xrow) :
		  vy_stmt_encode_secondary(value, key_def, &xrow));
	if (rc != 0)
		return -1;

	ssize_t row_size;
	if ((row_size = xlog_write_row(data_xlog, &xrow)) < 0)
		return -1;

	region_truncate(region, used);

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
 * Write statements from the iterator to a new page in the run,
 * update page and run statistics.
 *
 *  @retval  1 all is ok, the iterator is finished
 *  @retval  0 all is ok, the iterator isn't finished
 *  @retval -1 error occurred
 */
static int
vy_run_write_page(struct vy_run *run, struct xlog *data_xlog,
		  struct vy_stmt_stream *wi, struct tuple **curr_stmt,
		  uint64_t page_size, struct bloom_spectrum *bs,
		  const struct key_def *key_def,
		  const struct key_def *user_key_def, bool is_primary,
		  uint32_t *page_info_capacity)
{
	assert(curr_stmt != NULL);
	assert(*curr_stmt != NULL);
	struct vy_page_info *page = NULL;
	const char *region_key;
	bool end_of_run = false;

	/* row offsets accumulator */
	struct ibuf row_index_buf;
	ibuf_create(&row_index_buf, &cord()->slabc, sizeof(uint32_t) * 4096);

	if (run->info.page_count >= *page_info_capacity) {
		uint32_t cap = *page_info_capacity > 0 ?
			       *page_info_capacity * 2 : 16;
		struct vy_page_info *page_info = realloc(run->page_info,
						cap * sizeof(*page_info));
		if (page_info == NULL) {
			diag_set(OutOfMemory, cap * sizeof(*page_info),
				 "realloc", "struct vy_page_info");
			goto error_row_index;
		}
		run->page_info = page_info;
		*page_info_capacity = cap;
	}
	assert(*page_info_capacity >= run->info.page_count);

	if (run->info.page_count == 0) {
		/* See comment to run_info->max_key allocation below. */
		region_key = tuple_extract_key(*curr_stmt, key_def, NULL);
		if (region_key == NULL)
			goto error_row_index;
		assert(run->info.min_key == NULL);
		run->info.min_key = vy_key_dup(region_key);
		if (run->info.min_key == NULL)
			goto error_row_index;
	}

	page = run->page_info + run->info.page_count;
	vy_page_info_create(page, data_xlog->offset, *curr_stmt, key_def);
	xlog_tx_begin(data_xlog);

	/* Last written statement */
	struct tuple *last_stmt = *curr_stmt;
	do {
		uint32_t *offset = (uint32_t *) ibuf_alloc(&row_index_buf,
							   sizeof(uint32_t));
		if (offset == NULL) {
			diag_set(OutOfMemory, sizeof(uint32_t),
				 "ibuf", "row index");
			goto error_rollback;
		}
		*offset = page->unpacked_size;

		if (vy_run_dump_stmt(*curr_stmt, data_xlog, page,
				     key_def, is_primary) != 0)
			goto error_rollback;

		bloom_spectrum_add(bs, tuple_hash(*curr_stmt, user_key_def));

		int64_t lsn = vy_stmt_lsn(*curr_stmt);
		run->info.min_lsn = MIN(run->info.min_lsn, lsn);
		run->info.max_lsn = MAX(run->info.max_lsn, lsn);

		if (wi->iface->next(wi, curr_stmt))
			goto error_rollback;

		if (*curr_stmt == NULL)
			end_of_run = true;
		else
			last_stmt = *curr_stmt;
	} while (end_of_run == false &&
		 obuf_size(&data_xlog->obuf) < page_size);

	/* We don't write empty pages. */
	assert(last_stmt != NULL);

	if (end_of_run) {
		/*
		 * Tuple_extract_key allocates the key on a
		 * region, but the max_key must be allocated on
		 * the heap, because the max_key can live longer
		 * than a fiber. To reach this, we must copy the
		 * key into malloced memory.
		 */
		region_key = tuple_extract_key(last_stmt, key_def, NULL);
		if (region_key == NULL)
			goto error_rollback;
		assert(run->info.max_key == NULL);
		run->info.max_key = vy_key_dup(region_key);
		if (run->info.max_key == NULL)
			goto error_rollback;
	}

	/* Save offset to row index  */
	page->row_index_offset = page->unpacked_size;

	/* Write row index */
	struct xrow_header xrow;
	const uint32_t *row_index = (const uint32_t *) row_index_buf.rpos;
	assert(ibuf_used(&row_index_buf) == sizeof(uint32_t) * page->row_count);
	if (vy_row_index_encode(row_index, page->row_count, &xrow) < 0)
		goto error_rollback;

	ssize_t written = xlog_write_row(data_xlog, &xrow);
	if (written < 0)
		goto error_rollback;

	page->unpacked_size += written;

	written = xlog_tx_commit(data_xlog);
	if (written == 0)
		written = xlog_flush(data_xlog);
	if (written < 0)
		goto error_row_index;

	page->size = written;

	assert(page->row_count > 0);

	run->info.page_count++;
	vy_run_acct_page(run, page);

	ibuf_destroy(&row_index_buf);
	return !end_of_run ? 0: 1;

error_rollback:
	xlog_tx_rollback(data_xlog);
error_row_index:
	ibuf_destroy(&row_index_buf);
	return -1;
}

/**
 * Write statements from the iterator to a new run file.
 *
 *  @retval 0 success
 *  @retval -1 error occurred
 */
static int
vy_run_write_data(struct vy_run *run, const char *dirpath,
		  uint32_t space_id, uint32_t iid,
		  struct vy_stmt_stream *wi, uint64_t page_size,
		  const struct key_def *key_def,
		  const struct key_def *user_key_def,
		  size_t max_output_count, double bloom_fpr)
{
	struct tuple *stmt;

	/* Start iteration. */
	if (wi->iface->start(wi) != 0)
		goto err;
	if (wi->iface->next(wi, &stmt) != 0)
		goto err;

	/* Do not create empty run files. */
	if (stmt == NULL)
		goto done;

	struct bloom_spectrum bs;
	if (bloom_spectrum_create(&bs, max_output_count,
				  bloom_fpr, runtime.quota) != 0) {
		diag_set(OutOfMemory, 0,
			 "bloom_spectrum_create", "bloom_spectrum");
		goto err;
	}

	char path[PATH_MAX];
	vy_run_snprint_path(path, sizeof(path), dirpath,
			    space_id, iid, run->id, VY_FILE_RUN);
	struct xlog data_xlog;
	struct xlog_meta meta = {
		.filetype = XLOG_META_TYPE_RUN,
		.instance_uuid = INSTANCE_UUID,
	};
	if (xlog_create(&data_xlog, path, &meta) < 0)
		goto err_free_bloom;

	run->info.min_lsn = INT64_MAX;
	run->info.max_lsn = -1;

	assert(run->page_info == NULL);
	uint32_t page_info_capacity = 0;
	int rc;
	do {
		rc = vy_run_write_page(run, &data_xlog, wi, &stmt,
				       page_size, &bs, key_def, user_key_def,
				       iid == 0, &page_info_capacity);
		if (rc < 0)
			goto err_close_xlog;
		fiber_gc();
	} while (rc == 0);

	/* Sync data and link the file to the final name. */
	if (xlog_sync(&data_xlog) < 0 ||
	    xlog_rename(&data_xlog) < 0)
		goto err_close_xlog;

	run->fd = data_xlog.fd;
	xlog_close(&data_xlog, true);
	fiber_gc();

	bloom_spectrum_choose(&bs, &run->info.bloom);
	run->info.has_bloom = true;
	bloom_spectrum_destroy(&bs, runtime.quota);
	done:
	wi->iface->stop(wi);
	return 0;

	err_close_xlog:
	xlog_close(&data_xlog, false);
	fiber_gc();
	err_free_bloom:
	bloom_spectrum_destroy(&bs, runtime.quota);
	err:
	wi->iface->stop(wi);
	return -1;
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

/**
 * Calculate the size on disk that is needed to store give bloom filter.
 * @param bloom - storing bloom filter.
 * @return - calculated size.
 */
static size_t
vy_run_bloom_encode_size(const struct bloom *bloom)
{
	size_t size = mp_sizeof_array(4);
	size += mp_sizeof_uint(VY_BLOOM_VERSION); /* version */
	size += mp_sizeof_uint(bloom->table_size);
	size += mp_sizeof_uint(bloom->hash_count);
	size += mp_sizeof_bin(bloom_store_size(bloom));
	return size;
}

/**
 * Write bloom filter to given buffer.
 * The buffer must have at least vy_run_bloom_encode_size()
 * @param bloom - a bloom filter to write.
 * @param buffer - a buffer to write to.
 * @return - buffer + number of bytes written.
 */
char *
vy_run_bloom_encode(const struct bloom *bloom, char *buffer)
{
	char *pos = buffer;
	pos = mp_encode_array(pos, 4);
	pos = mp_encode_uint(pos, VY_BLOOM_VERSION);
	pos = mp_encode_uint(pos, bloom->table_size);
	pos = mp_encode_uint(pos, bloom->hash_count);
	pos = mp_encode_binl(pos, bloom_store_size(bloom));
	pos = bloom_store(bloom, pos);
	return pos;
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

	assert(run_info->has_bloom);
	size_t size = mp_sizeof_map(6);
	size += mp_sizeof_uint(VY_RUN_INFO_MIN_KEY) + min_key_size;
	size += mp_sizeof_uint(VY_RUN_INFO_MAX_KEY) + max_key_size;
	size += mp_sizeof_uint(VY_RUN_INFO_MIN_LSN) +
		mp_sizeof_uint(run_info->min_lsn);
	size += mp_sizeof_uint(VY_RUN_INFO_MAX_LSN) +
		mp_sizeof_uint(run_info->max_lsn);
	size += mp_sizeof_uint(VY_RUN_INFO_PAGE_COUNT) +
		mp_sizeof_uint(run_info->page_count);
	size += mp_sizeof_uint(VY_RUN_INFO_BLOOM) +
		vy_run_bloom_encode_size(&run_info->bloom);

	char *pos = region_alloc(&fiber()->gc, size);
	if (pos == NULL) {
		diag_set(OutOfMemory, size, "region", "run encode");
		return -1;
	}
	memset(xrow, 0, sizeof(*xrow));
	xrow->body->iov_base = pos;
	/* encode values */
	pos = mp_encode_map(pos, 6);
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
	pos = mp_encode_uint(pos, VY_RUN_INFO_BLOOM);
	pos = vy_run_bloom_encode(&run_info->bloom, pos);
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

	struct xlog index_xlog;
	struct xlog_meta meta = {
		.filetype = XLOG_META_TYPE_INDEX,
		.instance_uuid = INSTANCE_UUID,
	};
	if (xlog_create(&index_xlog, path, &meta) < 0)
		return -1;

	xlog_tx_begin(&index_xlog);

	struct xrow_header xrow;
	if (vy_run_info_encode(&run->info, &xrow) != 0 ||
	    xlog_write_row(&index_xlog, &xrow) < 0)
		goto fail;

	for (uint32_t page_no = 0; page_no < run->info.page_count; ++page_no) {
		struct vy_page_info *page_info = vy_run_page_info(run, page_no);
		if (vy_page_info_encode(page_info, &xrow) < 0) {
			goto fail;
		}
		if (xlog_write_row(&index_xlog, &xrow) < 0)
			goto fail;
	}

	if (xlog_tx_commit(&index_xlog) < 0 ||
	    xlog_flush(&index_xlog) < 0 ||
	    xlog_rename(&index_xlog) < 0)
		goto fail;
	xlog_close(&index_xlog, false);
	fiber_gc();
	return 0;
	fail:
	fiber_gc();
	xlog_tx_rollback(&index_xlog);
	xlog_close(&index_xlog, false);
	unlink(path);
	return -1;
}

/*
 * Create a run file, write statements returned by a write
 * iterator to it, and create an index file.
 */
int
vy_run_write(struct vy_run *run, const char *dirpath,
	     uint32_t space_id, uint32_t iid,
	     struct vy_stmt_stream *wi, uint64_t page_size,
	     const struct key_def *key_def,
	     const struct key_def *user_key_def,
	     size_t max_output_count, double bloom_fpr,
	     size_t *written, uint64_t *dumped_statements)
{
	ERROR_INJECT(ERRINJ_VY_RUN_WRITE,
		     {diag_set(ClientError, ER_INJECTION,
			       "vinyl dump"); return -1;});

	struct errinj *inj = errinj(ERRINJ_VY_RUN_WRITE_TIMEOUT, ERRINJ_DOUBLE);
	if (inj != NULL && inj->dparam > 0)
		usleep(inj->dparam * 1000000);

	if (vy_run_write_data(run, dirpath, space_id, iid,
			      wi, page_size, key_def, user_key_def,
			      max_output_count, bloom_fpr) != 0)
		return -1;

	if (vy_run_is_empty(run))
		return 0;

	if (vy_run_write_index(run, dirpath, space_id, iid) != 0)
		return -1;

	*written += run->count.bytes_compressed;
	*dumped_statements += run->count.rows;
	return 0;
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
	assert(stream->page == NULL);
	ZSTD_DStream *zdctx = vy_env_get_zdctx(stream->run_env);
	if (zdctx == NULL)
		return -1;

	struct vy_page_info *page_info = vy_run_page_info(stream->slice->run,
							  stream->page_no);
	stream->page = vy_page_new(page_info);
	if (stream->page == NULL)
		return -1;

	if (vy_page_read(stream->page, page_info,
			 stream->slice->run->fd, zdctx) != 0) {
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
	if (stream->slice->begin == NULL) {
		/* Already at the beginning */
		assert(stream->page_no == 0);
		assert(stream->pos_in_page == 0);
		return 0;
	}

	if (vy_slice_stream_read_page(stream) != 0)
		return -1;

	/**
	 * Binary search in page. Find the first position in page with
	 * tuple >= stream->slice->begin.
	 */
	uint32_t beg = 0;
	uint32_t end = stream->page->row_count;
	while (beg != end) {
		uint32_t mid = beg + (end - beg) / 2;
		struct tuple *fnd_key =
			vy_page_stmt(stream->page, mid, stream->key_def,
				     stream->format, stream->upsert_format,
				     stream->is_primary);
		if (fnd_key == NULL)
			return -1;
		int cmp = vy_stmt_compare(fnd_key, stream->slice->begin,
					  stream->key_def);
		if (cmp < 0)
			beg = mid + 1;
		else
			end = mid;
		tuple_unref(fnd_key);
	}
	stream->pos_in_page = end;

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
 * @param ret - pointer to pointer to the result.
 * @return 0 on success, -1 on memory or read error.
 */
static NODISCARD int
vy_slice_stream_next(struct vy_stmt_stream *virt_stream, struct tuple **ret)
{
	assert(virt_stream->iface->next == vy_slice_stream_next);
	struct vy_slice_stream *stream = (struct vy_slice_stream *)virt_stream;
	*ret = NULL;

	/* If the slice is ended, return EOF */
	if (stream->page_no > stream->slice->last_page_no)
		return 0;

	/* If current page is not already read, read it */
	if (stream->page == NULL && vy_slice_stream_read_page(stream) != 0)
		return -1;

	/* Read current tuple from the page */
	struct tuple *tuple =
		vy_page_stmt(stream->page, stream->pos_in_page,
			     stream->key_def, stream->format,
			     stream->upsert_format, stream->is_primary);
	if (tuple == NULL) /* Read or memory error */
		return -1;

	/* Check that the tuple is not out of slice bounds = */
	if (stream->slice->end != NULL &&
	    stream->page_no >= stream->slice->last_page_no &&
	    vy_stmt_compare_with_key(tuple, stream->slice->end,
				     stream->key_def) >= 0)
		return 0;

	/* We definitely has the next non-null tuple. Save it in stream */
	if (stream->tuple != NULL)
		tuple_unref(stream->tuple);
	stream->tuple = tuple;
	*ret = tuple;

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
vy_slice_stream_close(struct vy_stmt_stream *virt_stream)
{
	assert(virt_stream->iface->close == vy_slice_stream_close);
	struct vy_slice_stream *stream = (struct vy_slice_stream *)virt_stream;
	if (stream->page != NULL) {
		vy_page_delete(stream->page);
		stream->page = NULL;
	}
	if (stream->tuple != NULL) {
		tuple_unref(stream->tuple);
		stream->tuple = NULL;
	}
}

static const struct vy_stmt_stream_iface vy_slice_stream_iface = {
	.start = vy_slice_stream_search,
	.next = vy_slice_stream_next,
	.stop = NULL,
	.close = vy_slice_stream_close
};

void
vy_slice_stream_open(struct vy_slice_stream *stream, struct vy_slice *slice,
		   const struct key_def *key_def, struct tuple_format *format,
		   struct tuple_format *upsert_format,
		   struct vy_run_env *run_env, bool is_primary)
{
	stream->base.iface = &vy_slice_stream_iface;

	stream->page_no = slice->first_page_no;
	stream->pos_in_page = 0; /* We'll find it later */
	stream->page = NULL;
	stream->tuple = NULL;

	stream->slice = slice;
	stream->key_def = key_def;
	stream->format = format;
	stream->upsert_format = upsert_format;
	stream->run_env = run_env;
	stream->is_primary = is_primary;
}
