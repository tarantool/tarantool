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
	/**
	 * Reference counter. The run file is closed and the run
	 * in-memory structure is freed only when it reaches 0.
	 * Needed to prevent coeio thread from using a closed
	 * (worse, reopened) file descriptor.
	 */
	int refs;
	union {
		/** Link in range->runs list. */
		struct rlist in_range;
		/** Link in vy_join_ctx->runs list. */
		struct rlist in_join;
	};
	/** Unique ID of this run. */
	int64_t id;
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
	/* run */
	struct vy_run *run;

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
	/**
	 * For infiniruns it is the right border of the range to
	 * write. NULL for other runs.
	 */
	const char *end;
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

/** Increment a run's reference counter. */
static inline void
vy_run_ref(struct vy_run *run)
{
	assert(run->refs > 0);
	run->refs++;
}

/*
 * Decrement a run's reference counter.
 * Return true if the run was deleted.
 */
static inline bool
vy_run_unref(struct vy_run *run)
{
	assert(run->refs > 0);
	if (--run->refs == 0) {
		vy_run_delete(run);
		return true;
	}
	return false;
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
		     struct vy_run *run, enum iterator_type iterator_type,
		     const struct tuple *key, const char *end,
		     const struct vy_read_view **read_view,
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

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_RUN_H */
