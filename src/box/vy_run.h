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

#include "fiber_cond.h"
#include "iterator_type.h"
#include "vy_stmt.h" /* for comparators */
#include "vy_stmt_stream.h"
#include "vy_read_view.h"
#include "vy_stat.h"
#include "index_def.h"
#include "xlog.h"

#include "small/mempool.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_history;
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
	/** Bloom filter of all tuples in run */
	struct tuple_bloom *bloom;
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
	/** Vinyl run environment. */
	struct vy_run_env *env;
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
	/** Size of memory used for storing page index. */
	size_t page_index_size;
	/** Max LSN stored on disk. */
	int64_t dump_lsn;
	/**
	 * Run reference counter, the run is deleted once it hits 0.
	 * A new run is created with the reference counter set to 1.
	 * A run is referenced by each slice created for it and each
	 * pending read or write task.
	 */
	int refs;
	/** Number of slices created for this run. */
	int slice_count;
	/**
	 * Counter used on completion of a compaction task to check if
	 * all slices of the run have been compacted and so the run is
	 * not used any more and should be deleted.
	 */
	int compacted_slice_count;
	/**
	 * Link in the list of runs that became unused
	 * after compaction.
	 */
	struct rlist in_unused;
	/** Link in vy_lsm::runs list. */
	struct rlist in_lsm;
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
	struct fiber_cond pin_cond;
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
	/** Usage statistics */
	struct vy_run_iterator_stat *stat;

	/* Members needed for memory allocation and disk access */
	/** Key definition used for comparing statements on disk. */
	const struct key_def *cmp_def;
	/** Key definition provided by the user. */
	const struct key_def *key_def;
	/**
	 * Format ot allocate REPLACE and DELETE tuples read from
	 * pages.
	 */
	struct tuple_format *format;
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
	/** Statement at curr_pos. */
	struct tuple *curr_stmt;
	/**
	 * Last two pages read by the iterator. We keep two pages
	 * rather than just one, because we often probe a page for
	 * a better match. Keeping the previous page makes sure we
	 * won't throw out the current page if probing fails to
	 * find a better match.
	 */
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

/**
 * Enable coio reads for a vinyl run environment.
 *
 * This function starts @threads reader threads and makes
 * the run iterator hand disk reads over to them rather than
 * read run files directly blocking the current fiber.
 *
 * Subsequent calls to this function will silently return.
 */
void
vy_run_env_enable_coio(struct vy_run_env *env, int threads);

/**
 * Return the size of a run bloom filter.
 */
size_t
vy_run_bloom_size(struct vy_run *run);

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
vy_run_new(struct vy_run_env *env, int64_t id);

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

/**
 * Rebuild run index
 * @param run - run to rebuild index for
 * @param dir - path to the vinyl directory
 * @param space_id - space id
 * @param iid - index id
 * @param cmp_def - key definition with primary key parts
 * @param key_def - user defined key definition
 * @param format - format for allocating tuples read from disk
 * @param opts - index options
 * @return - 0 on sucess, -1 on fail
 */
int
vy_run_rebuild_index(struct vy_run *run, const char *dir,
		     uint32_t space_id, uint32_t iid,
		     const struct key_def *cmp_def,
		     const struct key_def *key_def,
		     struct tuple_format *format,
		     const struct index_opts *opts);

enum vy_file_type {
	VY_FILE_INDEX,
	VY_FILE_RUN,
	vy_file_MAX,
};

extern const char *vy_file_suffix[];

static inline int
vy_lsm_snprint_path(char *buf, int size, const char *dir,
		    uint32_t space_id, uint32_t iid)
{
	return snprintf(buf, size, "%s/%u/%u",
			dir, (unsigned)space_id, (unsigned)iid);
}

static inline int
vy_run_snprint_filename(char *buf, int size, int64_t run_id,
			enum vy_file_type type)
{
	return snprintf(buf, size, "%020lld.%s",
			(long long)run_id, vy_file_suffix[type]);
}

static inline int
vy_run_snprint_path(char *buf, int size, const char *dir,
		    uint32_t space_id, uint32_t iid,
		    int64_t run_id, enum vy_file_type type)
{
	int total = 0;
	SNPRINT(total, vy_lsm_snprint_path, buf, size,
		dir, (unsigned)space_id, (unsigned)iid);
	SNPRINT(total, snprintf, buf, size, "/");
	SNPRINT(total, vy_run_snprint_filename, buf, size, run_id, type);
	return total;
}

/**
 * Remove all files (data, index) corresponding to a run
 * with the given id. Return 0 on success, -1 if unlink()
 * failed.
 */
int
vy_run_remove_files(const char *dir, uint32_t space_id,
		    uint32_t iid, int64_t run_id);

/**
 * Allocate a new run slice.
 * This function increments @run->refs.
 */
struct vy_slice *
vy_slice_new(int64_t id, struct vy_run *run,
	     struct tuple *begin, struct tuple *end,
	     const struct key_def *cmp_def);

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
		fiber_cond_broadcast(&slice->pin_cond);
}

/**
 * Wait until a run slice is unpinned.
 */
static inline void
vy_slice_wait_pinned(struct vy_slice *slice)
{
	while (slice->pin_count > 0)
		fiber_cond_wait(&slice->pin_cond);
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
	     const struct key_def *cmp_def,
	     struct vy_slice **result);

/**
 * Open an iterator over on-disk run.
 *
 * Note, it is the caller's responsibility to make sure the slice
 * is not compacted while the iterator is reading it.
 */
void
vy_run_iterator_open(struct vy_run_iterator *itr,
		     struct vy_run_iterator_stat *stat,
		     struct vy_slice *slice, enum iterator_type iterator_type,
		     const struct tuple *key, const struct vy_read_view **rv,
		     const struct key_def *cmp_def,
		     const struct key_def *key_def,
		     struct tuple_format *format, bool is_primary);

/**
 * Advance a run iterator to the next key.
 * The key history is returned in @history (empty if EOF).
 * Returns 0 on success, -1 on memory allocation or IO error.
 */
NODISCARD int
vy_run_iterator_next(struct vy_run_iterator *itr,
		     struct vy_history *history);

/**
 * Advance a run iterator to the key following @last_stmt.
 * The key history is returned in @history (empty if EOF).
 * Returns 0 on success, -1 on memory allocation or IO error.
 */
NODISCARD int
vy_run_iterator_skip(struct vy_run_iterator *itr,
		     const struct tuple *last_stmt,
		     struct vy_history *history);

/**
 * Close a run iterator.
 */
void
vy_run_iterator_close(struct vy_run_iterator *itr);

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
	/**
	 * Key def for comparing with slice boundaries,
	 * includes secondary key parts.
	 */
	const struct key_def *cmp_def;
	/** Format for allocating REPLACE and DELETE tuples read from pages. */
	struct tuple_format *format;
	/** Set if this iterator is for a primary index. */
	bool is_primary;
};

/**
 * Open a run stream. Use vy_stmt_stream api for further work.
 */
void
vy_slice_stream_open(struct vy_slice_stream *stream, struct vy_slice *slice,
		     const struct key_def *cmp_def, struct tuple_format *format,
		     bool is_primary);

/**
 * Run_writer fills a created run with statements one by one,
 * splitting them into pages.
 */
struct vy_run_writer {
	/** Run to fill. */
	struct vy_run *run;
	/** Path to directory with run files. */
	const char *dirpath;
	/** Identifier of a space owning the run. */
	uint32_t space_id;
	/** Identifier of an index owning the run. */
	uint32_t iid;
	/**
	 * Key definition to extract from tuple and store as page
	 * min key, run min/max keys, and secondary index
	 * statements.
	 */
	const struct key_def *cmp_def;
	/** Key definition to calculate bloom. */
	const struct key_def *key_def;
	/**
	 * Minimal page size. When a page becames bigger, it is
	 * dumped.
	 */
	uint64_t page_size;
	/**
	 * Current page info capacity. Can grow with page number.
	 */
	uint32_t page_info_capacity;
	/** Xlog to write data. */
	struct xlog data_xlog;
	/** Bloom filter false positive rate. */
	double bloom_fpr;
	/** Bloom filter. */
	struct tuple_bloom_builder *bloom;
	/** Buffer of a current page row offsets. */
	struct ibuf row_index_buf;
	/**
	 * Remember a last written statement to use it as a source
	 * of max key of a finished run.
	 */
	struct tuple *last_stmt;
};

/** Create a run writer to fill a run with statements. */
int
vy_run_writer_create(struct vy_run_writer *writer, struct vy_run *run,
		const char *dirpath, uint32_t space_id, uint32_t iid,
		const struct key_def *cmp_def, const struct key_def *key_def,
		uint64_t page_size, double bloom_fpr);

/**
 * Write a specified statement into a run.
 * @param writer Writer to write a statement.
 * @param stmt Statement to write.
 *
 * @retval -1 Memory error.
 * @retval  0 Success.
 */
int
vy_run_writer_append_stmt(struct vy_run_writer *writer, struct tuple *stmt);

/**
 * Finalize run writing by writing run index into file. The writer
 * is deleted after call.
 * @param writer Run writer.
 * @retval -1 Memory or IO error.
 * @retval  0 Success.
 */
int
vy_run_writer_commit(struct vy_run_writer *writer);

/**
 * Abort run writing. Can not delete a run and run's file here,
 * becase it must be done from tx thread. The writer is deleted
 * after call.
 * @param Run writer.
 */
void
vy_run_writer_abort(struct vy_run_writer *writer);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_RUN_H */
