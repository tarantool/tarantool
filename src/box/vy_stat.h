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
#include <string.h>

#include "latency.h"
#include "tuple.h"
#include "iproto_constants.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Number of statements of each type. */
struct vy_stmt_stat {
	int64_t inserts;
	int64_t replaces;
	int64_t deletes;
	int64_t upserts;
};

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

/** LSM tree statistics. */
struct vy_lsm_stat {
	/** Number of lookups in the LSM tree. */
	int64_t lookup;
	/** Number of statements read from this LSM tree. */
	struct vy_stmt_counter get;
	/** Number of statements skipped on read. */
	struct vy_stmt_counter skip;
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
		/** Number of statements stored in the last LSM level. */
		struct vy_disk_stmt_counter last_level_count;
		/** Statement statistics. */
		struct vy_stmt_stat stmt;
		/** Run iterator statistics. */
		struct vy_run_iterator_stat iterator;
		/** Dump statistics. */
		struct {
			/* Number of completed tasks. */
			int32_t count;
			/** Time spent on dump tasks, in seconds. */
			double time;
			/** Number of input statements. */
			struct vy_stmt_counter input;
			/** Number of output statements. */
			struct vy_disk_stmt_counter output;
		} dump;
		/** Compaction statistics. */
		struct {
			/* Number of completed tasks. */
			int32_t count;
			/** Time spent on compaction tasks, in seconds. */
			double time;
			/** Number of input statements. */
			struct vy_disk_stmt_counter input;
			/** Number of output statements. */
			struct vy_disk_stmt_counter output;
			/** Number of statements awaiting compaction. */
			struct vy_disk_stmt_counter queue;
		} compaction;
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

/**
 * Scheduler statistics.
 *
 * All byte counters are given without taking into account
 * disk compression.
 */
struct vy_scheduler_stat {
	/** Number of completed tasks. */
	int32_t tasks_completed;
	/** Number of failed tasks. */
	int32_t tasks_failed;
	/** Number of tasks in progress. */
	int32_t tasks_inprogress;
	/** Number of completed memory dumps. */
	int32_t dump_count;
	/** Time spent on dump tasks, in seconds. */
	double dump_time;
	/** Number of bytes read by dump tasks. */
	int64_t dump_input;
	/** Number of bytes written by dump tasks. */
	int64_t dump_output;
	/** Time spent on compaction tasks, in seconds. */
	double compaction_time;
	/** Number of bytes read by compaction tasks. */
	int64_t compaction_input;
	/** Number of bytes written by compaction tasks. */
	int64_t compaction_output;
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
vy_stmt_counter_reset(struct vy_stmt_counter *c)
{
	memset(c, 0, sizeof(*c));
}

static inline void
vy_disk_stmt_counter_reset(struct vy_disk_stmt_counter *c)
{
	memset(c, 0, sizeof(*c));
}

static inline void
vy_stmt_counter_acct_tuple(struct vy_stmt_counter *c, struct tuple *tuple)
{
	c->rows++;
	c->bytes += tuple_size(tuple);
}

static inline void
vy_stmt_counter_unacct_tuple(struct vy_stmt_counter *c, struct tuple *tuple)
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

/**
 * Account a single statement of the given type in @stat.
 */
static inline void
vy_stmt_stat_acct(struct vy_stmt_stat *stat, enum iproto_type type)
{
	switch (type) {
	case IPROTO_INSERT:
		stat->inserts++;
		break;
	case IPROTO_REPLACE:
		stat->replaces++;
		break;
	case IPROTO_DELETE:
		stat->deletes++;
		break;
	case IPROTO_UPSERT:
		stat->upserts++;
		break;
	default:
		break;
	}
}

/**
 * Add statistics accumulated in @s2 to @s1.
 */
static inline void
vy_stmt_stat_add(struct vy_stmt_stat *s1, const struct vy_stmt_stat *s2)
{
	s1->inserts += s2->inserts;
	s1->replaces += s2->replaces;
	s1->deletes += s2->deletes;
	s1->upserts += s2->upserts;
}

/**
 * Subtract statistics accumulated in @s2 from @s1.
 */
static inline void
vy_stmt_stat_sub(struct vy_stmt_stat *s1, const struct vy_stmt_stat *s2)
{
	s1->inserts -= s2->inserts;
	s1->replaces -= s2->replaces;
	s1->deletes -= s2->deletes;
	s1->upserts -= s2->upserts;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_STAT_H */
