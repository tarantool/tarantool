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
#include "vy_stat.h"

#include "small/mempool.h"
#include "salad/bloom.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_run_reader;

/** Part of vinyl environment for run read/write */
struct vy_run_env {
	/** Mempool for struct vy_page_read_task */
	struct mempool read_task_pool;
	/** Key for thread-local ZSTD context */
	pthread_key_t zdctx_key;
	/** Pool of threads used for reading run files. */
	struct vy_run_reader *reader_pool;
	/** Number of threads in the reader pool. */
	int reader_pool_size;
	/**
	 * Index of the reader thread in the pool to be used for
	 * processing the next read request.
	 */
	int next_reader;
};

/**
 * Run metadata. Is a written to a file as a single chunk.
 */
struct vy_run_info {
	/** Min key in the run. */
	char *min_key;
	/** Max key in the run. */
	char *max_key;
	/** Min LSN over all statements in the run. */
	int64_t min_lsn;
	/** Max LSN over all statements in the run. */
	int64_t max_lsn;
	/** Number of pages in the run. */
	uint32_t page_count;
	/** Set iff bloom filter is available. */
	bool has_bloom;
	/** Bloom filter of all tuples in run */
	struct bloom bloom;
};

/**
 * Run page metadata. Is a written to a file as a single chunk.
 */
struct vy_page_info {
	/** Offset of page data in the run file. */
	uint64_t offset;
	/** Size of page data in the run file. */
	uint32_t size;
	/** Size of page data in memory, i.e. unpacked. */
	uint32_t unpacked_size;
	/** Number of statements in the page. */
	uint32_t row_count;
	/** Minimal key stored in the page. */
	char *min_key;
	/** Offset of the row index in the page. */
	uint32_t row_index_offset;
};

/**
 * Logical unit of vinyl index - a sorted file with data.
 */
struct vy_run {
	/** Info about the run stored in the index file. */
	struct vy_run_info info;
	/** Info about the run pages stored in the index file. */
	struct vy_page_info *page_info;
	/** Run data file. */
	int fd;
	/** Unique ID of this run. */
	int64_t id;
	/** Number of statements in this run. */
	struct vy_disk_stmt_counter count;
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
	/** An estimate of the number of statements in this slice. */
	struct vy_disk_stmt_counter count;
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
	struct vy_run_iterator_stat *stat;
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
 * Vinyl page stored in memory.
 */
struct vy_page {
	/** Page position in the run file. */
	uint32_t page_no;
	/** Size of page data in memory, i.e. unpacked. */
	uint32_t unpacked_size;
	/** Number of statements in the page. */
	uint32_t row_count;
	/** Array of row offsets. */
	uint32_t *row_index;
	/** Pointer to the page data. */
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

static inline struct vy_page_info *
vy_run_page_info(struct vy_run *run, uint32_t pos)
{
	assert(pos < run->info.page_count);
	return &run->page_info[pos];
}

static inline bool
vy_run_is_empty(struct vy_run *run)
{
	return run->info.page_count == 0;
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
 * @param dir - path to the vinyl directory
 * @param space_id - space id
 * @param iid - index id
 * @return - 0 on sucess, -1 on fail
 */
int
vy_run_recover(struct vy_run *run, const char *dir,
	       uint32_t space_id, uint32_t iid);

enum vy_file_type {
	VY_FILE_INDEX,
	VY_FILE_RUN,
	vy_file_MAX,
};

extern const char *vy_file_suffix[];

static int
vy_index_snprint_path(char *buf, int size, const char *dir,
		      uint32_t space_id, uint32_t iid)
{
	return snprintf(buf, size, "%s/%u/%u",
			dir, (unsigned)space_id, (unsigned)iid);
}

static inline int
vy_run_snprint_path(char *buf, int size, const char *dir,
		    uint32_t space_id, uint32_t iid,
		    int64_t run_id, enum vy_file_type type)
{
	int total = 0;
	SNPRINT(total, vy_index_snprint_path, buf, size,
		dir, (unsigned)space_id, (unsigned)iid);
	SNPRINT(total, snprintf, buf, size, "/%020lld.%s",
		(long long)run_id, vy_file_suffix[type]);
	return total;
}

int
vy_run_write(struct vy_run *run, const char *dirpath,
	     uint32_t space_id, uint32_t iid,
	     struct vy_stmt_stream *wi, uint64_t page_size,
	     const struct key_def *key_def,
	     const struct key_def *user_key_def,
	     size_t max_output_count, double bloom_fpr,
	     size_t *written, uint64_t *dumped_statements);

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

void
vy_run_iterator_open(struct vy_run_iterator *itr, bool coio_read,
		     struct vy_run_iterator_stat *stat, struct vy_run_env *run_env,
		     struct vy_slice *slice, enum iterator_type iterator_type,
		     const struct tuple *key, const struct vy_read_view **rv,
		     const struct key_def *key_def,
		     const struct key_def *user_key_def,
		     struct tuple_format *format,
		     struct tuple_format *upsert_format,
		     bool is_primary);

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
