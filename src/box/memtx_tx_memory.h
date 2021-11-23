#pragma once

#include <assert.h>
#include <trivia/util.h>
#include "tx_memory.h"
struct memtx_story;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocations types for txns used only by memtx tx manager.
 */
enum MEMTX_TX_ALLOC_TYPE {
	MEMTX_TX_ALLOC_MIN = 6,
	MEMTX_TX_ALLOC_TRACKER = 7,
	MEMTX_TX_ALLOC_MAX = 8
};

static_assert(MEMTX_TX_ALLOC_MIN == TXN_ALLOC_MAX - 1,
	      "enum MEMTX_TX_ALLOC_TYPE is not consistent with TXN_ALLOC_TYPE");

/**
 * String appearance of enum MEMTX_TX_ALLOC_TYPE
 */
extern const char *MEMTX_TX_ALLOC_TYPE_STRS[];

/**
 * @brief Status of memtx_story and a tuple it references to.
 */
enum MEMTX_TX_PIN_TYPE {
	MEMTX_TX_PIN_USED = 0,
	MEMTX_TX_PIN_RV = 1,
	MEMTX_TX_PIN_TRACK_GAP = 2,
	MEMTX_TX_PIN_MAX = 3
};

/**
 * String appearance of enum MEMTX_TX_PIN_TYPE
 */
extern const char *MEMTX_TX_PIN_TYPE_STRS[];

/**
 * @brief Stats that contains all the statistics collected by memory manager.
 * Made for statistics getter.
 */
struct memtx_tx_memory_stats {
	uint64_t total[MEMTX_TX_ALLOC_MAX];
	uint64_t avg[MEMTX_TX_ALLOC_MAX];
	uint64_t max[MEMTX_TX_ALLOC_MAX];
	uint64_t min[MEMTX_TX_ALLOC_MAX];

	uint64_t stories_total[MEMTX_TX_PIN_MAX];
	uint64_t pinned_tuples_total[MEMTX_TX_PIN_MAX];
	uint64_t pinned_tuples_count[MEMTX_TX_PIN_MAX];
};

/**
 * @brief Memory manager of memtx tx manager.
 */
struct memtx_tx_memory_manager {
	/** Base class of memory manager, */
	struct tx_memory_manager txn_stats;
	/** Statistics of memtx_stories and tuple they references to. */
	uint64_t stories_total[MEMTX_TX_PIN_MAX];
	uint64_t pinned_tuples_total[MEMTX_TX_PIN_MAX];
	uint64_t pinned_tuples_count[MEMTX_TX_PIN_MAX];
	/** Statistics storage. Passed to base class. */
	struct txn_stat_storage stats_storage[MEMTX_TX_ALLOC_MAX];
};

/**
 * @brief Allocate @a memtx_story object.
 * @param stat Memtx memory manager.
 * @param pool Mempool where story will be allocated.
 * @return Pointer to allocated story.
 * @note Allocate memtx_story only with this method to help memory manager
 * track this allocation.
 */
struct memtx_story *
memtx_tx_memory_story_alloc(struct memtx_tx_memory_manager *stat, struct mempool *pool);


/**
 * @brief Free @a memtx_story object.
 * @param stat Memtx memory manager.
 * @param pool Mempool where story will be deallocated.
 * @param story Story to deallocate.
 * @param story_status Status of story to track memory usage.
 */
void
memtx_tx_memory_story_free(struct memtx_tx_memory_manager *stat,
			 struct mempool *pool, struct memtx_story *story,
			 enum MEMTX_TX_PIN_TYPE story_status);

/**
 * @brief Pin tuple (it means that tuple is not placed in any space but
 * cannot be deleted because a story holds a reference).
 * @param stat Memtx memory manager.
 * @param status Status of story which pinned the tuple.
 * @param tuple_size Size of tuple.
 */
static inline void
memtx_tx_memory_tuple_pin(struct memtx_tx_memory_manager *stat,
			enum MEMTX_TX_PIN_TYPE status, size_t tuple_size)
{
	assert(status >= 0 && status < MEMTX_TX_PIN_MAX);

	stat->pinned_tuples_total[status] += tuple_size;
	stat->pinned_tuples_count[status]++;
}

/**
 * @brief Unpin tuple (it means that tuple is being placed in space).
 * @param stat Memtx memory manager.
 * @param status Status of story which pinned the tuple.
 * @param tuple_size Size of tuple.
 */
static inline void
memtx_tx_memory_tuple_unpin(struct memtx_tx_memory_manager *stat,
			    enum MEMTX_TX_PIN_TYPE status, size_t tuple_size)
{
	assert(status >= 0 && status < MEMTX_TX_PIN_MAX);
	assert(stat->pinned_tuples_count[status] > 0);

	stat->pinned_tuples_total[status] -= tuple_size;
	stat->pinned_tuples_count[status]--;
}

/**
 * @brief Change status of story (helps to detect garbage stories).
 * @param stat Memtx memory manager.
 * @param old_status Old status of story.
 * @param new_status New status of story.
 * @param size Size of story.
 */
static inline void
memtx_tx_memory_story_refresh_status(struct memtx_tx_memory_manager *stat,
				   enum MEMTX_TX_PIN_TYPE old_status,
				   enum MEMTX_TX_PIN_TYPE new_status,
				   size_t size)
{
	assert(old_status >= 0 && old_status < MEMTX_TX_PIN_MAX);
	assert(new_status >= 0 && new_status < MEMTX_TX_PIN_MAX);

	stat->stories_total[old_status] -= size;
	stat->stories_total[new_status] += size;
}

/**
 * @brief Change status of tuple which is referenced by story that changed its status.
 * @param stat Memtx memory manager.
 * @param old_status Old status of story.
 * @param new_status New status of story.
 * @param size Size of tuple.
 * @note Use this function only with pinned tuples!
 */
static inline void
memtx_tx_memory_tuple_refresh_pin_status(struct memtx_tx_memory_manager *stat,
					  enum MEMTX_TX_PIN_TYPE old_status,
					  enum MEMTX_TX_PIN_TYPE new_status,
					  size_t size)
{
	assert(old_status >= 0 && old_status < MEMTX_TX_PIN_MAX);
	assert(new_status >= 0 && new_status < MEMTX_TX_PIN_MAX);

	stat->pinned_tuples_total[old_status] -= size;
	stat->pinned_tuples_count[old_status]--;
	stat->pinned_tuples_total[new_status] += size;
	stat->pinned_tuples_count[new_status]++;
}

/**
 * @brief Get statistics collected by memory manager.
 * @param stat_manager Memtx memory manager.
 * @param out stats_out
 */
void
memtx_tx_memory_get_stats(struct memtx_tx_memory_manager *stat_manager,
			struct memtx_tx_memory_stats *stats_out);

/**
 * @brief Constructor of memtx memory manager.
 * @param stat Memtx memory manager.
 */
void
memtx_tx_memory_init(struct memtx_tx_memory_manager *stat);

/**
 * @brief Destructor of memtx memory manager.
 * @param stat Memtx memory manager.
 */
void
memtx_tx_memory_free(struct memtx_tx_memory_manager *stat);

#ifdef __cplusplus
}
#endif
