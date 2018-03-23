#ifndef INCLUDES_TARANTOOL_BOX_VY_STAT_H
#define INCLUDES_TARANTOOL_BOX_VY_STAT_H
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

#include "latency.h"
#include "tuple.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Used for accounting statements stored in memory. */
struct vy_stmt_counter {
	/** Number of statements. */
	int64_t rows;
	/** Size, in bytes. */
	int64_t bytes;
};

/** Used for accounting statements stored on disk. */
struct vy_disk_stmt_counter {
	/** Number of statements. */
	int64_t rows;
	/** Size when uncompressed, in bytes. */
	int64_t bytes;
	/** Size when compressed, in bytes */
	int64_t bytes_compressed;
	/** Number of pages. */
	int64_t pages;
};

/** Memory iterator statistics. */
struct vy_mem_iterator_stat {
	/** Number of lookups in the memory tree. */
	int64_t lookup;
	/** Number of statements returned by the iterator. */
	struct vy_stmt_counter get;
};

/** Run iterator statistics. */
struct vy_run_iterator_stat {
	/** Number of lookups in the page index. */
	int64_t lookup;
	/** Number of statements returned by the iterator. */
	struct vy_stmt_counter get;
	/**
	 * Number of times the bloom filter allowed to
	 * avoid a disk read.
	 */
	int64_t bloom_hit;
	/**
	 * Number of times the bloom filter failed to
	 * prevent a disk read.
	 */
	int64_t bloom_miss;
	/**
	 * Number of statements actually read from the disk.
	 * It may be greater than the number of statements
	 * returned by the iterator, because of page granularity
	 * of disk reads.
	 */
	struct vy_disk_stmt_counter read;
};

/** TX write set iterator statistics. */
struct vy_txw_iterator_stat {
	/** Number of lookups in the write set. */
	int64_t lookup;
	/** Number of statements returned by the iterator. */
	struct vy_stmt_counter get;
};

/** Dump/compaction statistics. */
struct vy_compact_stat {
	int32_t count;
	/** Number of input statements. */
	struct vy_stmt_counter in;
	/** Number of output statements. */
	struct vy_stmt_counter out;
};

/** LSM tree statistics. */
struct vy_lsm_stat {
	/** Number of lookups in the LSM tree. */
	int64_t lookup;
	/** Number of statements read from this LSM tree. */
	struct vy_stmt_counter get;
	/** Number of statements written to this LSM tree. */
	struct vy_stmt_counter put;
	/** Read latency. */
	struct latency latency;
	/** Upsert statistics. */
	struct {
		/** How many upsert chains have been squashed. */
		int64_t squashed;
		/** How many upserts have been applied on read. */
		int64_t applied;
	} upsert;
	/** Memory related statistics. */
	struct {
		/** Number of statements stored in memory. */
		struct vy_stmt_counter count;
		/** Memory iterator statistics. */
		struct vy_mem_iterator_stat iterator;
	} memory;
	/** Disk related statistics. */
	struct {
		/** Number of statements stored on disk. */
		struct vy_disk_stmt_counter count;
		/** Run iterator statistics. */
		struct vy_run_iterator_stat iterator;
		/** Dump statistics. */
		struct vy_compact_stat dump;
		/** Compaction statistics. */
		struct vy_compact_stat compact;
	} disk;
	/** TX write set statistics. */
	struct {
		/** Number of statements in the write set. */
		struct vy_stmt_counter count;
		/** TX write set iterator statistics. */
		struct vy_txw_iterator_stat iterator;
	} txw;
};

/** Tuple cache statistics. */
struct vy_cache_stat {
	/** Number of statements in the cache. */
	struct vy_stmt_counter count;
	/** Number of lookups in the cache. */
	int64_t lookup;
	/** Number of reads from the cache. */
	struct vy_stmt_counter get;
	/** Number of writes to the cache. */
	struct vy_stmt_counter put;
	/**
	 * Number of statements removed from the cache
	 * due to overwrite.
	 */
	struct vy_stmt_counter invalidate;
	/**
	 * Number of statements removed from the cache
	 * due to memory shortage.
	 */
	struct vy_stmt_counter evict;
};

/** Transaction statistics. */
struct vy_tx_stat {
	/** Number of committed transactions. */
	int64_t commit;
	/** Number of rolled back transactions. */
	int64_t rollback;
	/** Number of transactions aborted on conflict. */
	int64_t conflict;
};

static inline int
vy_lsm_stat_create(struct vy_lsm_stat *stat)
{
	return latency_create(&stat->latency);
}

static inline void
vy_lsm_stat_destroy(struct vy_lsm_stat *stat)
{
	latency_destroy(&stat->latency);
}

static inline void
vy_stmt_counter_acct_tuple(struct vy_stmt_counter *c,
			   const struct tuple *tuple)
{
	c->rows++;
	c->bytes += tuple_size(tuple);
}

static inline void
vy_stmt_counter_unacct_tuple(struct vy_stmt_counter *c,
			     const struct tuple *tuple)
{
	c->rows--;
	c->bytes -= tuple_size(tuple);
}

static inline void
vy_stmt_counter_add(struct vy_stmt_counter *c1,
		    const struct vy_stmt_counter *c2)
{
	c1->rows += c2->rows;
	c1->bytes += c2->bytes;
}

static inline void
vy_stmt_counter_sub(struct vy_stmt_counter *c1,
		    const struct vy_stmt_counter *c2)
{
	c1->rows -= c2->rows;
	c1->bytes -= c2->bytes;
}

static inline void
vy_stmt_counter_add_disk(struct vy_stmt_counter *c1,
			 const struct vy_disk_stmt_counter *c2)
{
	c1->rows += c2->rows;
	c1->bytes += c2->bytes;
}

static inline void
vy_disk_stmt_counter_add(struct vy_disk_stmt_counter *c1,
			 const struct vy_disk_stmt_counter *c2)
{
	c1->rows += c2->rows;
	c1->bytes += c2->bytes;
	c1->bytes_compressed += c2->bytes_compressed;
	c1->pages += c2->pages;
}

static inline void
vy_disk_stmt_counter_sub(struct vy_disk_stmt_counter *c1,
			 const struct vy_disk_stmt_counter *c2)
{
	c1->rows -= c2->rows;
	c1->bytes -= c2->bytes;
	c1->bytes_compressed -= c2->bytes_compressed;
	c1->pages -= c2->pages;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_STAT_H */
