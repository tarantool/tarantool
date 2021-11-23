#include "memtx_tx_memory.h"

const char *MEMTX_TX_ALLOC_TYPE_STRS[] = {
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"TRACKERS"
};

static_assert(lengthof(MEMTX_TX_ALLOC_TYPE_STRS) == MEMTX_TX_ALLOC_MAX,
	      "MEMTX_TX_ALLOC_TYPE_STRS does not match MEMTX_TX_ALLOC_TYPE");

const char *MEMTX_TX_PIN_TYPE_STRS[] = {
	"USED BY ACTIVE TXNS",
	"POTENTIALLY IN READ VIEW",
	"USED TO TRACK GAP"
};

struct memtx_story *
memtx_tx_memory_story_alloc(struct memtx_tx_memory_manager *stat, struct mempool *pool)
{
	assert(pool != NULL);

	struct mempool_stats pool_stats;
	mempool_stats(pool, &pool_stats);
	struct memtx_story *new_story = mempool_alloc(pool);
	if (new_story != NULL) {
		stat->stories_total[MEMTX_TX_PIN_USED] += pool_stats.objsize;
	}
	return new_story;
}

void
memtx_tx_memory_story_free(struct memtx_tx_memory_manager *stat,
			 struct mempool *pool, struct memtx_story *story,
			 enum MEMTX_TX_PIN_TYPE story_status)
{
	assert(pool != NULL);
	assert(story != NULL);
	assert(story_status < MEMTX_TX_PIN_MAX);

	struct mempool_stats pool_stats;
	mempool_stats(pool, &pool_stats);
	assert(stat->stories_total[story_status] >= pool_stats.objsize);
	stat->stories_total[story_status] -= pool_stats.objsize;
	mempool_free(pool, story);
}

void
memtx_tx_memory_get_stats(struct memtx_tx_memory_manager *stat_manager,
			struct memtx_tx_memory_stats *stats_out)
{
	assert(stat_manager != NULL);
	assert(stats_out != NULL);

	for (size_t i = 0; i < MEMTX_TX_ALLOC_MAX; ++i) {
		stats_out->total[i] = stat_manager->stats_storage[i].total;
		if (stat_manager->txn_stats.txn_num > 0) {
			stats_out->avg[i] = stat_manager->stats_storage[i].total /
					    stat_manager->txn_stats.txn_num;
		} else {
			stats_out->avg[i] = 0;
		}
		stats_out->max[i] = histogram_max(
			stat_manager->stats_storage[i].hist);
	}
	for (size_t i = 0; i < MEMTX_TX_PIN_MAX; ++i) {
		stats_out->pinned_tuples_total[i] =
			stat_manager->pinned_tuples_total[i];
		stats_out->pinned_tuples_count[i] =
			stat_manager->pinned_tuples_count[i];
		stats_out->stories_total[i] = stat_manager->stories_total[i];
	}
}

void
memtx_tx_memory_init(struct memtx_tx_memory_manager *stat)
{
	memset(&stat->stats_storage, 0, sizeof(uint64_t) * MEMTX_TX_PIN_MAX);
	memset(&stat->pinned_tuples_total, 0, sizeof(uint64_t) * MEMTX_TX_PIN_MAX);
	memset(&stat->pinned_tuples_count, 0, sizeof(uint64_t) * MEMTX_TX_PIN_MAX);
	memset(&stat->stories_total, 0, sizeof(uint64_t) * MEMTX_TX_PIN_MAX);
	tx_memory_init(&stat->txn_stats, MEMTX_TX_ALLOC_MAX,
		       stat->stats_storage);
}

void
memtx_tx_memory_free(struct memtx_tx_memory_manager *stat)
{
	tx_memory_free(&stat->txn_stats);
}
