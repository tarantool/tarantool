#ifndef INCLUDES_TARANTOOL_BOX_VY_RUN_H
#define INCLUDES_TARANTOOL_BOX_VY_RUN_H
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

#include <stdint.h>
#include <stdbool.h>

#include "ipc.h"
#include "index.h" /* enum iterator_type */
#include "vy_stmt.h" /* for comparators */
#include "vy_stmt_iterator.h" /* struct vy_stmt_iterator */

#include "small/mempool.h"
#include "salad/bloom.h"
#include "zstd.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

static const uint64_t vy_page_info_key_map = (1 << VY_PAGE_INFO_OFFSET) |
					     (1 << VY_PAGE_INFO_SIZE) |
					     (1 << VY_PAGE_INFO_UNPACKED_SIZE) |
					     (1 << VY_PAGE_INFO_ROW_COUNT) |
					     (1 << VY_PAGE_INFO_MIN_KEY) |
					     (1 << VY_PAGE_INFO_PAGE_INDEX_OFFSET);

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

/** Part of vinyl environment for run read/write */
struct vy_run_env {
	/** Mempool for struct vy_page_read_task */
	struct mempool read_task_pool;
	/** Key for thread-local ZSTD context */
	pthread_key_t zdctx_key;
};

/**
 * Run metadata. Is a written to a file as a single chunk.
 */
struct vy_run_info {
	/** Run page count. */
	uint32_t  count;
	/** Number of keys. */
	uint32_t  keys;
	/** Min and max keys in the run. */
	char *min_key;
	char *max_key;
	/* Min and max lsn over all statements in the run. */
	int64_t  min_lsn;
	int64_t  max_lsn;
	/** Size of run on disk. */
	uint64_t size;
	/** Bloom filter of all tuples in run */
	bool has_bloom;
	struct bloom bloom;
	/** Pages meta. */
	struct vy_page_info *page_infos;
};

/**
 * Run page metadata. Is a written to a file as a single chunk.
 */
struct vy_page_info {
	/* count of statements in the page */
	uint32_t count;
	/* offset of page data in run */
	uint64_t offset;
	/* size of page data in file */
	uint32_t size;
	/* size of page data in memory, i.e. unpacked */
	uint32_t unpacked_size;
	/* Offset of the min key in the parent run->pages_min. */
	uint32_t min_key_offset;
	/* minimal key */
	char *min_key;
	/* row index offset in page */
	uint32_t page_index_offset;
};

/**
 * Logical unit of vinyl index - a sorted file with data.
 */
struct vy_run {
	struct vy_run_info info;
	/** Run data file. */
	int fd;
	/** Unique ID of this run. */
	int64_t id;
	/** Max LSN stored on disk. */
	int64_t dump_lsn;
	/**
	 * Run reference counter, the run is deleted once it hits 0.
	 * A new run is created with the reference counter set to 1.
	 * A run is referenced by each slice created for it.
	 */
	int refs;
	/**
	 * Counter used on completion of a compaction task to check if
	 * all slices of the run have been compacted and so the run is
	 * not used any more and should be deleted.
	 */
	int64_t compacted_slice_count;
	/**
	 * Link in the list of runs that became unused
	 * after compaction.
	 */
	struct rlist in_unused;
	/** Link in vy_index::runs list. */
	struct rlist in_index;
};

/**
 * Slice of a run, used to organize runs in ranges.
 */
struct vy_slice {
	/** Unique ID of this slice. */
	int64_t id;
	/** Run this slice is for (increments vy_run::refs). */
	struct vy_run *run;
	/**
	 * Slice begin and end (increments tuple::refs).
	 * If @begin is NULL, the slice starts from the beginning
	 * of the run. If @end is NULL, the slice ends at the end
	 * of the run.
	 */
	struct tuple *begin;
	struct tuple *end;
	/**
	 * Number of async users of this slice. Slice must not
	 * be removed until it hits 0. Used by the iterator to
	 * prevent use-after-free after waiting for IO.
	 * See also vy_run_wait_pinned().
	 */
	int pin_count;
	/**
	 * Condition variable signaled by vy_slice_unpin()
	 * if pin_count reaches 0.
	 */
	struct ipc_cond pin_cond;
	union {
		/** Link in range->slices list. */
		struct rlist in_range;
		/** Link in vy_join_ctx->slices list. */
		struct rlist in_join;
	};
	/**
	 * Indexes of the first and the last page in the run
	 * that belong to this slice.
	 */
	uint32_t first_page_no;
	uint32_t last_page_no;
	/** An estimate of the number of keys in this slice. */
	uint32_t keys;
	/** An estimate of the size of this slice on disk. */
	uint64_t size;
};

/** Position of a particular stmt in vy_run. */
struct vy_run_iterator_pos {
	uint32_t page_no;
	uint32_t pos_in_page;
};

/**
 * Return statements from vy_run based on initial search key,
 * iteration order and view lsn.
 *
 * All statements with lsn > vlsn are skipped.
 * The API allows to traverse over resulting statements within two
 * dimensions - key and lsn. next_key() switches to the youngest
 * statement of the next key, according to the iteration order,
 * and next_lsn() switches to an older statement for the same
 * key.
 */
struct vy_run_iterator {
	/** Parent class, must be the first member */
	struct vy_stmt_iterator base;
	/** Usage statistics */
	struct vy_iterator_stat *stat;
	/** Vinyl run environment. */
	struct vy_run_env *run_env;

	/* Members needed for memory allocation and disk access */
	/** Index key definition used for storing statements on disk. */
	const struct key_def *key_def;
	/** Index key definition defined by the user. */
	const struct key_def *user_key_def;
	/** Should the iterator use coio task for reading or not */
	bool coio_read;
	/**
	 * Format ot allocate REPLACE and DELETE tuples read from
	 * pages.
	 */
	struct tuple_format *format;
	/** Same as format, but for UPSERT tuples. */
	struct tuple_format *upsert_format;
	/** Set if this iterator is for a primary index. */
	bool is_primary;
	/** The run slice to iterate. */
	struct vy_slice *slice;

	/* Search options */
	/**
	 * Iterator type, that specifies direction, start position and stop
	 * criteria if the key is not specified, GT and EQ are changed to
	 * GE, LT to LE for beauty.
	 */
	enum iterator_type iterator_type;
	/** Key to search. */
	const struct tuple *key;
	/* LSN visibility, iterator shows values with lsn <= vlsn */
	const struct vy_read_view **read_view;

	/* State of the iterator */
	/** Position of the current record */
	struct vy_run_iterator_pos curr_pos;
	/**
	 * Last stmt returned by vy_run_iterator_get.
	 * The iterator holds this stmt until the next call to
	 * vy_run_iterator_get, when it's dereferenced.
	 */
	struct tuple *curr_stmt;
	/** Position of record that spawned curr_stmt */
	struct vy_run_iterator_pos curr_stmt_pos;
	/** LRU cache of two active pages (two pages is enough). */
	struct vy_page *curr_page;
	struct vy_page *prev_page;
	/** Is false until first .._get or .._next_.. method is called */
	bool search_started;
	/** Search is finished, you will not get more values from iterator */
	bool search_ended;
};

/**
 * Page
 */
struct vy_page {
	/** Page position in the run file (used by run_iterator->page_cache */
	uint32_t page_no;
	/** The number of statements */
	uint32_t count;
	/** Page data size */
	uint32_t unpacked_size;
	/** Array with row offsets in page data */
	uint32_t *page_index;
	/** Page data */
	char *data;
};

/**
 * Initialize vinyl run environment
 */
void
vy_run_env_create(struct vy_run_env *env);

/**
 * Destroy vinyl run environment
 */
void
vy_run_env_destroy(struct vy_run_env *env);

int
vy_page_info_create(struct vy_page_info *page_info, uint64_t offset,
		    const struct tuple *min_key, const struct key_def *key_def);

void
vy_page_info_destroy(struct vy_page_info *page_info);

static inline struct vy_page_info *
vy_run_page_info(struct vy_run *run, uint32_t pos)
{
	assert(pos < run->info.count);
	return &run->info.page_infos[pos];
}

static inline uint64_t
vy_run_size(struct vy_run *run)
{
	return run->info.size;
}

static inline bool
vy_run_is_empty(struct vy_run *run)
{
	return run->info.count == 0;
}

struct vy_run *
vy_run_new(int64_t id);

void
vy_run_delete(struct vy_run *run);

static inline void
vy_run_ref(struct vy_run *run)
{
	assert(run->refs > 0);
	run->refs++;
}

static inline void
vy_run_unref(struct vy_run *run)
{
	assert(run->refs > 0);
	if (--run->refs == 0)
		vy_run_delete(run);
}

/**
 * Load run from disk
 * @param run - run to laod
 * @param index_path - path to index part of the run
 * @param run_path - path to run part of the run
 * @return - 0 on sucess, -1 on fail
 */
int
vy_run_recover(struct vy_run *run, const char *index_path,
	       const char *run_path);

/**
 * Allocate a new run slice.
 * This function increments @run->refs.
 */
struct vy_slice *
vy_slice_new(int64_t id, struct vy_run *run,
	     struct tuple *begin, struct tuple *end,
	     const struct key_def *key_def);

/**
 * Free a run slice.
 * This function decrements @run->refs and
 * deletes the run if the counter hits 0.
 */
void
vy_slice_delete(struct vy_slice *slice);

/**
 * Pin a run slice.
 * A pinned slice can't be deleted until it's unpinned.
 */
static inline void
vy_slice_pin(struct vy_slice *slice)
{
	slice->pin_count++;
}

/*
 * Unpin a run slice.
 * This function reverts the effect of vy_slice_pin().
 */
static inline void
vy_slice_unpin(struct vy_slice *slice)
{
	assert(slice->pin_count > 0);
	if (--slice->pin_count == 0)
		ipc_cond_broadcast(&slice->pin_cond);
}

/**
 * Wait until a run slice is unpinned.
 */
static inline void
vy_slice_wait_pinned(struct vy_slice *slice)
{
	while (slice->pin_count > 0)
		ipc_cond_wait(&slice->pin_cond);
}

/**
 * Cut a sub-slice of @slice starting at @begin and ending at @end.
 * Return 0 on success, -1 on OOM.
 *
 * The new slice is returned in @result. If @slice does not intersect
 * with [@begin, @end), @result is set to NULL.
 */
int
vy_slice_cut(struct vy_slice *slice, int64_t id,
	     struct tuple *begin, struct tuple *end,
	     const struct key_def *key_def,
	     struct vy_slice **result);

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
int
vy_page_info_decode(struct vy_page_info *page, const struct xrow_header *xrow,
		    const char *filename);

/**
 * Read bloom filter from given buffer.
 * @param bloom - a bloom filter to read.
 * @param buffer[in/out] - a buffer to read from.
 *  The pointer is incremented on the number of bytes read.
 * @param filename Filename for error reporting.
 * @return - 0 on success or -1 on format/memory error
 */
int
vy_run_bloom_decode(struct bloom *bloom, const char **buffer,
		    const char *filename);

void
vy_run_iterator_open(struct vy_run_iterator *itr, bool coio_read,
		     struct vy_iterator_stat *stat, struct vy_run_env *run_env,
		     struct vy_slice *slice, enum iterator_type iterator_type,
		     const struct tuple *key, const struct vy_read_view **rv,
		     const struct key_def *key_def,
		     const struct key_def *user_key_def,
		     struct tuple_format *format,
		     struct tuple_format *upsert_format,
		     bool is_primary);

/**
 * Get thread local zstd decompression context
 */
ZSTD_DStream *
vy_env_get_zdctx(struct vy_run_env *env);

struct vy_page *
vy_page_new(const struct vy_page_info *page_info);

void
vy_page_delete(struct vy_page *page);

int
vy_page_xrow(struct vy_page *page, uint32_t stmt_no,
	     struct xrow_header *xrow);

/**
 * Read a page requests from vinyl xlog data file.
 *
 * @retval 0 on success
 * @retval -1 on error, check diag
 */
int
vy_page_read(struct vy_page *page, const struct vy_page_info *page_info, int fd,
	     ZSTD_DStream *zdctx);

/**
 * Simple stream over a slice. @see vy_stmt_stream.
 */
struct vy_slice_stream {
	/** Parent class, must be the first member */
	struct vy_stmt_stream base;

	/** Current position */
	uint32_t page_no;
	uint32_t pos_in_page;
	/** Last page read */
	struct vy_page *page;
	/** The last tuple returned to user */
	struct tuple *tuple;

	/** Members needed for memory allocation and disk access */
	/** Slice to stream */
	struct vy_slice *slice;
	/** Key def for comparing with slice boundaries */
	const struct key_def *key_def;
	/** Format for allocating REPLACE and DELETE tuples read from pages. */
	struct tuple_format *format;
	/** Same as format, but for UPSERT tuples. */
	struct tuple_format *upsert_format;
	/** Vinyl run environment. */
	struct vy_run_env *run_env;
	/** Set if this iterator is for a primary index. */
	bool is_primary;
};

/**
 * Open a run stream. Use vy_stmt_stream api for further work.
 */
void
vy_slice_stream_open(struct vy_slice_stream *stream, struct vy_slice *slice,
		     const struct key_def *key_def, struct tuple_format *format,
		     struct tuple_format *upsert_format,
		     struct vy_run_env *run_env, bool is_primary);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_RUN_H */
