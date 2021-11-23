/*
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
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

#include <assert.h>
#include <lib/core/say.h>
#include "tx_memory.h"
#include "small/mempool.h"
#include "small/region.h"
#include "histogram.h"
#include "txn.h"

const char *TXN_ALLOC_TYPE_STRS[] = {
	"STATEMENTS",
	"SAVEPOINTS",
	"USER DATA",
	"REDO LOGS",
	"TRIGGERS",
	"TIMERS",
	"JOURNAL ENTRIES"
};

static_assert(lengthof(TXN_ALLOC_TYPE_STRS) == TXN_ALLOC_MAX,
	      "TXN_ALLOC_TYPE_STRS does not match TXN_ALLOC_TYPE");

/**
 * @brief Add info about allocation to @a stat.
 * @param stat Memory manager.
 * @param txn Txn that requires an allocation.
 * @param alloc_size Size of allocation.
 * @param alloc_type Type of allocation.
 * @param deallocate False on allocation, True on deallocation.
 */
static void
tx_track_allocation(struct tx_memory_manager *stat, struct txn *txn,
		    uint64_t alloc_size, size_t alloc_type, bool deallocate)
{
	assert(alloc_type < stat->alloc_max);
	assert(txn != NULL);

	histogram_discard(stat->stats_storage[alloc_type].hist,
			  txn->mem_used->total[alloc_type]);
	if (!deallocate) {
		txn->mem_used->total[alloc_type] += alloc_size;
		stat->stats_storage[alloc_type].total += alloc_size;
	} else {
		assert(txn->mem_used->total[alloc_type] >= alloc_size);
		assert(stat->stats_storage[alloc_type].total >= alloc_size);
		txn->mem_used->total[alloc_type] -= alloc_size;
		stat->stats_storage[alloc_type].total -= alloc_size;
	}
	histogram_collect(stat->stats_storage[alloc_type].hist,
			  txn->mem_used->total[alloc_type]);
}

int
tx_memory_register_txn(struct tx_memory_manager *stat, struct txn *txn)
{
	assert(txn != NULL);
	assert(txn->mem_used == NULL);
	assert(txn->given_region_used == 0);

	const size_t metadata_size = sizeof(struct txn_mem_used) +
				     sizeof(uint64_t) * stat->alloc_max;
	struct txn_mem_used *mem_used =
		region_aligned_alloc(&txn->region, metadata_size,
				     alignof(*(txn->mem_used)));
	// TODO: errinj
	if (mem_used == NULL) {
		diag_set(OutOfMemory, metadata_size, "tx region",
			 "tx memory metadata");
		return -1;
	}
	memset(mem_used, 0, metadata_size);
	txn->mem_used = mem_used;
	/* Collect zero memory consumption initially. */
	for (size_t i = 0; i < stat->alloc_max; ++i) {
		assert(mem_used->total[i] == 0);
		histogram_collect(stat->stats_storage[i].hist,0);
	}
	stat->txn_num++;
	return 0;
}

void
tx_memory_clear_txn(struct tx_memory_manager *stat, struct txn *txn)
{
	assert(txn != NULL);
	assert(txn->mem_used != NULL);
	assert(txn->given_region_used == 0);

	/*
	 * Check if txn does not owe any mempool allocation.
	 * In this case all the tracked allocation are from region
	 * and we will delete them via region_truncate.
	 */
	assert(txn->mem_used->mempool_total == 0);
	for (size_t i = 0; i < stat->alloc_max; ++i) {
		tx_track_allocation(stat, txn, txn->mem_used->total[i], i, true);
	}
	txn->mem_used = NULL;
	region_truncate(&txn->region, sizeof(struct txn));
	assert(stat->txn_num > 0);
	stat->txn_num--;
}

void *
tx_memory_mempool_alloc(struct tx_memory_manager *stat, struct txn *txn,
			struct mempool* pool, size_t alloc_type)
{
	assert(pool != NULL);
	assert(txn != NULL);
	assert(alloc_type < stat->alloc_max);

	void *allocation = mempool_alloc(pool);
	if (allocation == NULL)
		return NULL;
	/* Track an allocation only if it succeeded. */
	struct mempool_stats pool_stats;
	mempool_stats(pool, &pool_stats);
	tx_track_allocation(stat, txn, pool_stats.objsize, alloc_type, false);
#ifndef NDEBUG
	txn->mem_used->mempool_total += pool_stats.objsize;
#endif
	return allocation;
}

void
tx_memory_mempool_free(struct tx_memory_manager *stat, struct txn *txn,
		       struct mempool *pool, void *ptr, size_t alloc_type)
{
	assert(pool != NULL);
	assert(txn != NULL);
	assert(alloc_type < stat->alloc_max);

	struct mempool_stats pool_stats;
	mempool_stats(pool, &pool_stats);
	tx_track_allocation(stat, txn, pool_stats.objsize,
			    alloc_type, true);
#ifndef NDEBUG
	assert(txn->mem_used->mempool_total >= pool_stats.objsize);
	txn->mem_used->mempool_total -= pool_stats.objsize;
#endif
	mempool_free(pool, ptr);
}

void *
tx_memory_region_alloc(struct tx_memory_manager *stat, struct txn *txn,
		       size_t size, size_t alloc_type)
{
	assert(txn != NULL);
	assert(alloc_type < stat->alloc_max);
	void *allocation = region_alloc(&txn->region, size);
	if (allocation == NULL)
		return NULL;
	/* Track an allocation only if it succeeded. */
	tx_track_allocation(stat, txn, size, alloc_type, false);
	return allocation;
}

void *
tx_memory_region_aligned_alloc(struct tx_memory_manager *stat, struct txn *txn,
			size_t size, size_t alignment, size_t alloc_type)
{
	assert(txn != NULL);
	assert(alloc_type < stat->alloc_max);
	void *allocation = region_aligned_alloc(&txn->region, size, alignment);
	if (allocation == NULL)
		return NULL;
	/* Track an allocation only if it succeeded. */
	tx_track_allocation(stat, txn, size, alloc_type, false);
	return allocation;
}

struct region *
tx_memory_txn_region_give(struct txn *txn)
{
	assert(txn != NULL);
	assert(txn->given_region_used == 0);

	struct region *txn_region = &txn->region;
	txn->given_region_used = region_used(txn_region);
	return txn_region;
}

void
tx_memory_txn_region_take(struct tx_memory_manager *stat, struct txn *txn,
			  size_t alloc_type)
{
	assert(txn != NULL);
	assert(txn->given_region_used != 0);

	uint64_t new_alloc_size = region_used(&txn->region) - txn->given_region_used;
	txn->given_region_used = 0;
	if (new_alloc_size > 0) {
		tx_track_allocation(stat, txn, new_alloc_size, alloc_type,
				    false);
	}
}

void
tx_memory_init(struct tx_memory_manager *stat, size_t alloc_max,
	     struct txn_stat_storage *stat_storage)
{
	assert(stat_storage != NULL);
	assert(alloc_max >= TXN_ALLOC_MAX);

	const int64_t KB = 1024;
	const int64_t MB = 1024 * KB;
	const int64_t GB = 1024 * MB;
	int64_t buckets[] = {0, 128, 512, KB, 8 * KB, 32 * KB, 128 * KB, 512 * KB,
			     MB, 8 * MB, 32 * MB, 128 * MB, 512 * MB, GB};
	for (size_t i = 0; i < alloc_max; ++i) {
		stat_storage[i].hist =
			histogram_new(buckets, lengthof(buckets));
		histogram_collect(stat_storage[i].hist, 0);
	}
	stat->alloc_max = alloc_max;
	stat->stats_storage = stat_storage;
	stat->txn_num = 0;
}

void
tx_memory_free(struct tx_memory_manager *stat)
{
	for (size_t i = 0; i < stat->alloc_max; ++i) {
		histogram_delete(stat->stats_storage[i].hist);
	}
}
