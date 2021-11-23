#pragma once
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

/*
 * This file contains declaration class @a tx_memory_manager that is common
 * memory manager for every transaction manager and txns themselves.
 * Txns and transaction managers must allocate memory using methods @a tx_stat
 * only because it make possible to monitor memory usage,
 * and that is why every transaction manager must have its own memory manager
 * derived from this one.
 * It is also important to say that one should not create an instance of
 * @a tx_stat, create instance of derivated memory manager instead.
 */

#include <trivia/util.h>
#include "small/mempool.h"
#include "histogram.h"
struct txn;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocations types for txns.
 */
enum TXN_ALLOC_TYPE {
	TXN_ALLOC_STMT = 0,
	TXN_ALLOC_SVP = 1,
	TXN_ALLOC_USER_DATA = 2,
	TXN_ALLOC_REDO_LOG = 3,
	TXN_ALLOC_TRIGGER = 4,
	TXN_ALLOC_TIMER = 5,
	TXN_ALLOC_JOURNAL_ENTRY = 6,
	TXN_ALLOC_MAX = 7
};

/**
 * String appearance of enum TXN_ALLOC_TYPE
 */
extern const char *TXN_ALLOC_TYPE_STRS[];

/**
 * Storage for statistics of one allocation type for txn.
 */
struct txn_stat_storage {
	struct histogram *hist;
	uint64_t total;
};

/**
 * Memory manager itself.
 */
struct tx_memory_manager {
	/** Number of allocation types. */
	uint32_t alloc_max;
	/** Number of registered txn. */
	uint64_t txn_num;
	/**
	 * Pointer to array of @txn_stat_storage objects.
	 * It is better to allocate it as a field of derived memory manager.
	 */
	struct txn_stat_storage *stats_storage;
};

/**
 * Memory usage of one txn. Allocated on its region.
 */
struct txn_mem_used {
	/**
	 * This field is used only in debug mode to make sure that txn has
	 * deallocated all the mempool allocations before it was deleted.
	 */
	uint64_t mempool_total;
	/** Total memory used for every type of allocations. */
	uint64_t total[];
};

/**
 * @brief A wrapper over region_alloc.
 * @param txn Owner of a region.
 * @param size Bytes to allocate.
 * @param alloc_type See TXN_ALLOC_TYPE.
 * @note The only way to truncate a region of @a txn is to clear @a txn.
 */
void *
tx_memory_region_alloc(struct tx_memory_manager *stat, struct txn *txn,
		       size_t size, size_t alloc_type);

/**
 * @brief Register @a txn in @a tx_stat. It is very important
 * to register txn before using allocators from @a tx_stat.
 */
int
tx_memory_register_txn(struct tx_memory_manager *stat, struct txn *txn);

/**
 * @brief Unregister @a txn and truncate its region up to sizeof(txn).
 */
void
tx_memory_clear_txn(struct tx_memory_manager *stat, struct txn *txn);

/**
 * @brief A wrapper over region_aligned_alloc.
 * @param txn Owner of a region.
 * @param size Bytes to allocate.
 * @param alignment Alignment of allocation.
 * @param alloc_type Type of allocation.
 */
void *
tx_memory_region_aligned_alloc(struct tx_memory_manager *stat, struct txn *txn,
			size_t size, size_t alignment, size_t alloc_type);

#define tx_memory_region_alloc_object(stat, txn, T, size, alloc_type) ({  \
	*(size) = sizeof(T);                                              \
	(T *)tx_memory_region_aligned_alloc((stat), (txn), sizeof(T),     \
	                                    alignof(T), (alloc_type));    \
})

/**
 * @brief Getter for txn's region. Use only if region was not given before.
 * @param txn Owner of region.
 * @return Pointer to region of txn.
 * @note Do not use txn's region directly, use this method instead.
 */
struct region *
tx_memory_txn_region_give(struct txn *txn);

/**
 * @brief Notify @a stat that you finished using given @a txn to allow it
 * collect allocations statistics.
 * @param stat Memory manager.
 * @param txn Owner of region.
 * @param alloc_type Type of all the allocations via given region.
 */
void
tx_memory_txn_region_take(struct tx_memory_manager *stat, struct txn *txn,
			  size_t alloc_type);

/**
 * @brief A wrapper over mempool_alloc.
 * @param txn Txn that requires an allocation.
 * @param pool Mempool to allocate from.
 * @param alloc_type Type of allocation.
 */
void *
tx_memory_mempool_alloc(struct tx_memory_manager *stat, struct txn *txn,
		 struct mempool *pool, size_t alloc_type);

/**
 * @brief A wrapper over mempool_free.
 * @param txn Txn that requires a deallocation.
 * @param pool Mempool to deallocate from.
 * @param ptr Pointer to free.
 * @param alloc_type Type of allocation.
 */
void
tx_memory_mempool_free(struct tx_memory_manager *stat, struct txn *txn,
		struct mempool *pool, void *ptr, size_t alloc_type);

/**
 * @brief Constructor of @a tx_stat.
 * @param stat Pointer to an instance of @a tx_stat.
 * @param alloc_max Number of allocation types.
 * @param stat_storage Externally allocated storage of statistics.
 * @note Should be called only from constructor of derived memory manager.
 */
void
tx_memory_init(struct tx_memory_manager *stat, size_t alloc_max,
	     struct txn_stat_storage *stat_storage);

/**
 * @brief Destructor of @a tx_stat.
 * @param stat Pointer to an instance of @a tx_stat.
 * @note Should be called only from destructor of derived memory manager.
 */
void
tx_memory_free(struct tx_memory_manager *stat);

#ifdef __cplusplus
}
#endif
