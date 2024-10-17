/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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
#include "memtx_tx.h"
#include "memtx_space.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "schema_def.h"
#include "small/mempool.h"

enum {
	/**
	 * Virtual PSN that will be set to del_psn of rolled-back story.
	 * Must be less that any existing "real" PSN.
	 */
	MEMTX_TX_ROLLBACKED_PSN = 1,
};

static_assert((int)MEMTX_TX_ROLLBACKED_PSN < (int)TXN_MIN_PSN,
	      "There must be a range for TX manager's internal use");

/**
 * Link that connects a memtx_story with older and newer stories of the same
 * key in index.
 */
struct memtx_story_link {
	/** Story that was happened after that story was ended. */
	struct memtx_story *newer_story;
	/** Story that was happened before that story was started. */
	struct memtx_story *older_story;
	/** List of gap items @sa gap_item. */
	struct rlist read_gaps;
	/**
	 * If the tuple of story is physically in index, here the pointer
	 * to that index is stored.
	 */
	struct index *in_index;
};

/**
 * A part of a history of a value in space.
 * It's a story about a tuple, from the point it was added to space to the
 * point when it was deleted from a space.
 * All stories are linked into a list of stories of the same key of each index.
 */
struct memtx_story {
	/** The story is about this tuple. The tuple is referenced. */
	struct tuple *tuple;
	/**
	 * Statement that introduced this story. Is set to NULL when the
	 * statement's transaction becomes committed. Can also be NULL if we
	 * don't know who introduced that story, the tuple was added by a
	 * transaction that was completed and destroyed some time ago.
	 */
	struct txn_stmt *add_stmt;
	/**
	 * Prepare sequence number of add_stmt's transaction. Is set when
	 * the transaction is prepared. Can be 0 if the transaction is
	 * in progress or we don't know who introduced that story.
	 */
	int64_t add_psn;
	/**
	 * Statement that ended this story. Is set to NULL when the statement's
	 * transaction becomes committed. Can also be NULL if the tuple has not
	 * been deleted yet.
	 */
	struct txn_stmt *del_stmt;
	/**
	 * Prepare sequence number of del_stmt's transaction. Is set when
	 * the transaction is prepared. Can be 0 if the transaction is
	 * in progress or if nobody has deleted the tuple.
	 */
	int64_t del_psn;
	/**
	 * List of trackers - transactions that has read this tuple.
	 */
	struct rlist reader_list;
	/**
	 * Link in tx_manager::all_stories
	 */
	struct rlist in_all_stories;
	/**
	 * Link in space::memtx_tx_stories.
	 */
	struct rlist in_space_stories;
	/**
	 * Number of indexes in this space - and the count of link[].
	 */
	uint32_t index_count;
	/**
	 * Status of story, describes the reason why story cannot be deleted.
	 * It is initialized in memtx_story constructor and is changed only in
	 * memtx_tx_story_gc.
	 */
	enum memtx_tx_story_status status;
	/**
	 * Flag is set when @a tuple is not placed in primary key and
	 * the story is the only reason why @a tuple cannot be deleted.
	 */
	bool tuple_is_retained;
	/**
	 * Link with older and newer stories (and just tuples) for each
	 * index respectively.
	 */
	struct memtx_story_link link[];
};

static uint32_t
memtx_tx_story_key_hash(const struct tuple *a)
{
	uintptr_t u = (uintptr_t)a;
	if (sizeof(uintptr_t) <= sizeof(uint32_t))
		return u;
	else
		return u ^ (u >> 32);
}

#define mh_name _history
#define mh_key_t struct tuple *
#define mh_node_t struct memtx_story *
#define mh_arg_t int
#define mh_hash(a, arg) (memtx_tx_story_key_hash((*(a))->tuple))
#define mh_hash_key(a, arg) (memtx_tx_story_key_hash(a))
#define mh_cmp(a, b, arg) ((*(a))->tuple != (*(b))->tuple)
#define mh_cmp_key(a, b, arg) ((a) != (*(b))->tuple)
#define MH_SOURCE
#include "salad/mhash.h"

/**
 * Record that links transaction and a story that the transaction have read.
 */
struct tx_read_tracker {
	/** The TX that read story. */
	struct txn *reader;
	/** The story that was read by reader. */
	struct memtx_story *story;
	/** Link in story->reader_list. */
	struct rlist in_reader_list;
	/** Link in reader->read_set. */
	struct rlist in_read_set;
};

/**
 * An element that stores the fact that some transaction have read
 * a full key and found nothing.
 */
struct point_hole_item {
	/** A link of headless list of items with the same index and key. */
	struct rlist ring;
	/** Link in txn->point_holes_list. */
	struct rlist in_point_holes_list;
	/** Saved index->unique_id. */
	uint32_t index_unique_id;
	/** Precalculated hash for storing in hash table.. */
	uint32_t hash;
	/** Saved txn. */
	struct txn *txn;
	/** Saved key. Points to @a short_key or allocated in txn's region. */
	const char *key;
	/** Saved key len. */
	size_t key_len;
	/** Storage for short key. @key may point here. */
	char short_key[16];
	/** Flag that the hash tables stores pointer to this item. */
	bool is_head;
};

/** Type of gap item. */
enum gap_item_type {
	/**
	 * The transaction have read some tuple that is not committed and thus
	 * not visible. In this case the further commit of that tuple can cause
	 * a conflict, as well as any overwrite of that tuple.
	 */
	GAP_INPLACE,
	/**
	 * The transaction made a select or range scan, reading a key or range
	 * between two adjacent tuples of the index. For that case a consequent
	 * write to that range can cause a conflict. Such an item will be stored
	 * in successor's story, or index->read_gaps if there's no successor.
	 */
	GAP_NEARBY,
	/**
	 * A transaction has completed a count of tuples matching a key and
	 * iterator. After that any consequent delete or insert of any tuple
	 * matching the key+iterator pair must lead to a conflict. Such an
	 * item will be stored in index->read_gaps.
	 */
	GAP_COUNT,
	/**
	 * A transaction completed a full scan of unordered index. After that
	 * any consequent write to any new place of the index must lead to
	 * conflict. Such an item will be store in index->read_gaps.
	 */
	GAP_FULL_SCAN,
};

/**
 * Common base of elements that store the fact that some transaction have read
 * something and found nothing. There are three cases of such a fact, described
 * by enum @sa gap_item_type.
 */
struct gap_item_base {
	/** Type of gap record. */
	enum gap_item_type type;
	/** A link in memtx_story_link::read_gaps OR index::read_gaps. */
	struct rlist in_read_gaps;
	/** Link in txn->gap_list. */
	struct rlist in_gap_list;
	/** The transaction that read it. */
	struct txn *txn;
};

/**
 * Derived class for inplace gap, @sa GAP_INPLACE.
 */
struct inplace_gap_item {
	/** Base class. */
	struct gap_item_base base;
};

/**
 * Derived class for nearby gap, @sa GAP_NEARBY.
 */
struct nearby_gap_item {
	/** Base class. */
	struct gap_item_base base;
	/** The key. Can be NULL. */
	const char *key;
	uint32_t key_len;
	uint32_t part_count;
	/** Search mode. */
	enum iterator_type type;
	/** Storage for short key. @key may point here. */
	char short_key[16];
};

/**
 * Derived class for full scan gap, @sa GAP_FULL_SCAN.
 */
struct full_scan_gap_item {
	/** Base class. */
	struct gap_item_base base;
};

/**
 * Derived class for count gap, @sa GAP_COUNT.
 */
struct count_gap_item {
	/** Base class. */
	struct gap_item_base base;
	/** The key. Can be NULL. */
	const char *key;
	/* Length of the key. */
	uint32_t key_len;
	/* Part count of the key. */
	uint32_t part_count;
	/** Search mode. */
	enum iterator_type type;
	/** Storage for short key. @key may point here. */
	char short_key[16];
	/** The bound tuple. */
	struct tuple *until;
	/** The bound tuple hint. */
	hint_t until_hint;
};

/**
 * Initialize common part of gap item, except for in_read_gaps member,
 * which initialization is specific for gap item type.
 */
static void
gap_item_base_create(struct gap_item_base *item, enum gap_item_type type,
		     struct txn *txn)
{
	item->type = type;
	item->txn = txn;
	rlist_add(&txn->gap_list, &item->in_gap_list);
}

/**
 * Allocate and create inplace gap item.
 * Note that in_read_gaps base member must be initialized later.
 */
static struct inplace_gap_item *
memtx_tx_inplace_gap_item_new(struct txn *txn);

/**
 * Allocate and create nearby gap item.
 * Note that in_read_gaps base member must be initialized later.
 */
static struct nearby_gap_item *
memtx_tx_nearby_gap_item_new(struct txn *txn, enum iterator_type type,
			     const char *key, uint32_t part_count);

/**
 * Allocate and create full scan gap item.
 * Note that in_read_gaps base member must be initialized later.
 */
static struct full_scan_gap_item *
memtx_tx_full_scan_gap_item_new(struct txn *txn);

/**
 * Allocate and create count gap item.
 * Note that in_read_gaps base member must be initialized later.
 */
static struct count_gap_item *
memtx_tx_count_gap_item_new(struct txn *txn, enum iterator_type type,
			    const char *key, uint32_t part_count,
			    struct tuple *until, hint_t until_hint);

/**
 * Helper structure for searching for point_hole_item in the hash table,
 * @sa point_hole_item_pool.
 */
struct point_hole_key {
	struct index *index;
	struct tuple *tuple;
};

/** Hash calculatore for the key. */
static uint32_t
point_hole_storage_key_hash(struct point_hole_key *key)
{
	struct key_def *def = key->index->def->key_def;
	return key->index->unique_id ^ def->tuple_hash(key->tuple, def);
}

/** point_hole_item comparator. */
static int
point_hole_storage_equal(const struct point_hole_item *obj1,
			 const struct point_hole_item *obj2)
{
	/* Canonical msgpack is comparable by memcmp. */
	if (obj1->index_unique_id != obj2->index_unique_id ||
	    obj1->key_len != obj2->key_len)
		return 1;
	return memcmp(obj1->key, obj2->key, obj1->key_len) != 0;
}

/** point_hole_item comparator with key. */
static int
point_hole_storage_key_equal(const struct point_hole_key *key,
			     const struct point_hole_item *object)
{
	if (key->index->unique_id != object->index_unique_id)
		return 1;
	assert(key->index != NULL);
	assert(key->tuple != NULL);
	struct key_def *def = key->index->def->key_def;
	hint_t oh = def->key_hint(object->key, def->part_count, def);
	hint_t kh = def->tuple_hint(key->tuple, def);
	return def->tuple_compare_with_key(key->tuple, kh, object->key,
					   def->part_count, oh, def);
}

/**
 * Hash table definition for hole read storage.
 * The key is constructed by unique index ID and search key.
 * Actually it stores pointers to point_hole_item structures.
 * If more than one point_hole_item is added to the hash table,
 * it is simply added to the headless list in existing point_hole_item.
 */

#define mh_name _point_holes
#define mh_key_t struct point_hole_key *
#define mh_node_t struct point_hole_item *
#define mh_arg_t int
#define mh_hash(a, arg) ((*(a))->hash)
#define mh_hash_key(a, arg) ( point_hole_storage_key_hash(a) )
#define mh_cmp(a, b, arg) point_hole_storage_equal(*(a), *(b))
#define mh_cmp_key(a, b, arg) point_hole_storage_key_equal((a), *(b))
#define MH_SOURCE
#include "salad/mhash.h"

/**
 * Collect an allocation to memtx_tx_stats.
 */
static inline void
memtx_tx_stats_collect(struct memtx_tx_stats *stats, size_t size)
{
	stats->count++;
	stats->total += size;
}

/**
 * Discard an allocation collected by memtx_tx_stats.
 */
static inline void
memtx_tx_stats_discard(struct memtx_tx_stats *stats, size_t size)
{
	assert(stats->count > 0);
	assert(stats->total >= size);

	stats->count--;
	stats->total -= size;
}

/**
 * Collect allocation statistics.
 */
static inline void
memtx_tx_track_allocation(struct txn *txn, size_t size,
			  enum memtx_tx_alloc_type alloc_type)
{
	assert(alloc_type < MEMTX_TX_ALLOC_TYPE_MAX);
	txn->memtx_tx_alloc_stats[alloc_type] += size;
}

/**
 * Collect deallocation statistics.
 */
static inline void
memtx_tx_track_deallocation(struct txn *txn, size_t size,
			    enum memtx_tx_alloc_type alloc_type)
{
	assert(alloc_type < MEMTX_TX_ALLOC_TYPE_MAX);
	assert(txn->memtx_tx_alloc_stats[alloc_type] >= size);
	txn->memtx_tx_alloc_stats[alloc_type] -= size;
}

/**
 * A wrapper over mempool.
 * Use it instead of mempool to track allocations!
 */
struct memtx_tx_mempool {
	/**
	 * Wrapped mempool.
	 */
	struct mempool pool;
	/**
	 * Each allocation is accounted with this type.
	 */
	enum memtx_tx_alloc_type alloc_type;
};

static inline void
memtx_tx_mempool_create(struct memtx_tx_mempool *mempool, uint32_t objsize,
			enum memtx_tx_alloc_type alloc_type)
{
	mempool_create(&mempool->pool, cord_slab_cache(), objsize);
	mempool->alloc_type = alloc_type;
}

static inline void
memtx_tx_mempool_destroy(struct memtx_tx_mempool *mempool)
{
	mempool_destroy(&mempool->pool);
	mempool->alloc_type = MEMTX_TX_ALLOC_TYPE_MAX;
}

/**
 * Allocate an object on given @a mempool and account allocated size in
 * statistics of transaction @a txn.
 */
static void *
memtx_tx_xmempool_alloc(struct txn *txn, struct memtx_tx_mempool *mempool)
{
	void *allocation = xmempool_alloc(&mempool->pool);
	uint32_t size = mempool->pool.objsize;
	memtx_tx_track_allocation(txn, size, mempool->alloc_type);
	return allocation;
}

static void
memtx_tx_mempool_free(struct txn *txn, struct memtx_tx_mempool *mempool, void *ptr)
{
	uint32_t size = mempool->pool.objsize;
	memtx_tx_track_deallocation(txn, size, mempool->alloc_type);
	mempool_free(&mempool->pool, ptr);
}

/**
 * Choose memtx_tx_alloc_type for alloc_obj.
 */
static inline enum memtx_tx_alloc_type
memtx_tx_region_object_to_type(enum memtx_tx_alloc_object alloc_obj)
{
	enum memtx_tx_alloc_type alloc_type = MEMTX_TX_ALLOC_TYPE_MAX;
	switch (alloc_obj) {
	case MEMTX_TX_OBJECT_CONFLICT_TRACKER:
	case MEMTX_TX_OBJECT_READ_TRACKER:
		alloc_type = MEMTX_TX_ALLOC_TRACKER;
		break;
	default:
		unreachable();
	};
	assert(alloc_type < MEMTX_TX_ALLOC_TYPE_MAX);
	return alloc_type;
}

/**
 * Alloc object on region. Pass object as enum memtx_tx_alloc_object.
 * Use this method to track txn's allocations!
 */
static inline void *
memtx_tx_xregion_alloc_object(struct txn *txn,
			      enum memtx_tx_alloc_object alloc_obj)
{
	size_t size = 0;
	void *alloc = NULL;
	enum memtx_tx_alloc_type alloc_type =
		memtx_tx_region_object_to_type(alloc_obj);
	switch (alloc_obj) {
	case MEMTX_TX_OBJECT_READ_TRACKER:
		alloc = xregion_alloc_object(&txn->region,
					     struct tx_read_tracker);
		size = sizeof(struct tx_read_tracker);
		break;
	default:
		unreachable();
	}
	assert(alloc_type < MEMTX_TX_ALLOC_TYPE_MAX);
	memtx_tx_track_allocation(txn, size, alloc_type);
	return alloc;
}

/**
 * Tx_region method for allocations of arbitrary size.
 * You must pass allocation type explicitly to categorize an allocation.
 * Use this method to track allocations!
 */
static inline void *
memtx_tx_xregion_alloc(struct txn *txn, size_t size,
		       enum memtx_tx_alloc_type alloc_type)
{
	void *allocation = xregion_alloc(&txn->region, size);
	if (allocation != NULL)
		memtx_tx_track_allocation(txn, size, alloc_type);
	return allocation;
}

/** String representation of enum memtx_tx_alloc_type. */
const char *memtx_tx_alloc_type_strs[MEMTX_TX_ALLOC_TYPE_MAX] = {
	"trackers",
	"conflicts",
};

/** String representation of enum memtx_tx_story_status. */
const char *memtx_tx_story_status_strs[MEMTX_TX_STORY_STATUS_MAX] = {
	"used",
	"read_view",
	"tracking",
};

struct tx_manager
{
	/**
	 * List of all transactions that are in a read view.
	 * New transactions are added to the tail of this list,
	 * so the list is ordered by rv_psn.
	 */
	struct rlist read_view_txs;
	/**
	 * Mempools for tx_story objects with different index count.
	 * It's the only case when we use bare mempool in memtx_tx because
	 * we cannot account story allocation to any particular txn.
	 */
	struct mempool memtx_tx_story_pool[BOX_INDEX_MAX];
	/** Hash table tuple -> memtx_story of that tuple. */
	struct mh_history_t *history;
	/** Mempool for point_hole_item objects. */
	struct memtx_tx_mempool point_hole_item_pool;
	/** Hash table that hold point selects with empty result. */
	struct mh_point_holes_t *point_holes;
	/** Mempool for inplace_gap_item objects. */
	struct memtx_tx_mempool inplace_gap_item_mempoool;
	/** Mempool for nearby_gap_item objects. */
	struct memtx_tx_mempool nearby_gap_item_mempoool;
	/** Mempool for count_gap_item objects. */
	struct memtx_tx_mempool count_gap_item_mempool;
	/** Mempool for full_scan_gap_item objects. */
	struct memtx_tx_mempool full_scan_gap_item_mempool;
	/** List of all memtx_story objects. */
	struct rlist all_stories;
	struct memtx_tx_stats story_stats[MEMTX_TX_STORY_STATUS_MAX];
	struct memtx_tx_stats retained_tuple_stats[MEMTX_TX_STORY_STATUS_MAX];
	/** Iterator that sequentially traverses all memtx_story objects. */
	struct rlist *traverse_all_stories;
	/** The list containing all transactions. */
	struct rlist all_txs;
	/** Accumulated number of GC steps that should be done. */
	size_t must_do_gc_steps;
};

enum {
	/**
	 * Number of iterations that is allowed for TX manager to do for
	 * searching and deleting no more used memtx_tx_stories per creation of
	 * a new story.
	 */
		TX_MANAGER_GC_STEPS_SIZE = 2,
};

/** That's a definition, see declaration for description. */
bool memtx_tx_manager_use_mvcc_engine = false;

/** The one and only instance of tx_manager. */
static struct tx_manager txm;

void
memtx_tx_manager_init()
{
	rlist_create(&txm.read_view_txs);
	for (size_t i = 0; i < BOX_INDEX_MAX; i++) {
		size_t item_size = sizeof(struct memtx_story) +
				   i * sizeof(struct memtx_story_link);
		mempool_create(&txm.memtx_tx_story_pool[i],
			       cord_slab_cache(), item_size);
	}
	txm.history = mh_history_new();
	memtx_tx_mempool_create(&txm.point_hole_item_pool,
				sizeof(struct point_hole_item),
				MEMTX_TX_ALLOC_TRACKER);
	txm.point_holes = mh_point_holes_new();
	memtx_tx_mempool_create(&txm.inplace_gap_item_mempoool,
				sizeof(struct inplace_gap_item),
				MEMTX_TX_ALLOC_TRACKER);
	memtx_tx_mempool_create(&txm.nearby_gap_item_mempoool,
				sizeof(struct nearby_gap_item),
				MEMTX_TX_ALLOC_TRACKER);
	memtx_tx_mempool_create(&txm.count_gap_item_mempool,
				sizeof(struct count_gap_item),
				MEMTX_TX_ALLOC_TRACKER);
	memtx_tx_mempool_create(&txm.full_scan_gap_item_mempool,
				sizeof(struct full_scan_gap_item),
				MEMTX_TX_ALLOC_TRACKER);
	rlist_create(&txm.all_stories);
	rlist_create(&txm.all_txs);
	txm.traverse_all_stories = &txm.all_stories;
	txm.must_do_gc_steps = 0;
	memset(&txm.story_stats, 0, sizeof(txm.story_stats));
}

void
memtx_tx_manager_free()
{
	for (size_t i = 0; i < BOX_INDEX_MAX; i++)
		mempool_destroy(&txm.memtx_tx_story_pool[i]);
	mh_history_delete(txm.history);
	memtx_tx_mempool_destroy(&txm.point_hole_item_pool);
	mh_point_holes_delete(txm.point_holes);
	memtx_tx_mempool_destroy(&txm.inplace_gap_item_mempoool);
	memtx_tx_mempool_destroy(&txm.nearby_gap_item_mempoool);
	memtx_tx_mempool_destroy(&txm.count_gap_item_mempool);
	memtx_tx_mempool_destroy(&txm.full_scan_gap_item_mempool);
}

void
memtx_tx_statistics_collect(struct memtx_tx_statistics *stats)
{
	memset(stats, 0, sizeof(*stats));
	for (size_t i = 0; i < MEMTX_TX_STORY_STATUS_MAX; ++i) {
		stats->stories[i] = txm.story_stats[i];
		stats->retained_tuples[i] = txm.retained_tuple_stats[i];
	}
	if (rlist_empty(&txm.all_txs)) {
		return;
	}
	struct txn *txn;
	size_t txn_count = 0;
	rlist_foreach_entry(txn, &txm.all_txs, in_all_txs) {
		txn_count++;
		for (size_t i = 0; i < MEMTX_TX_ALLOC_TYPE_MAX; ++i) {
			size_t txn_stat = txn->memtx_tx_alloc_stats[i];
			stats->memtx_tx_total[i] += txn_stat;
			if (txn_stat > stats->memtx_tx_max[i])
				stats->memtx_tx_max[i] = txn_stat;
		}
		for (size_t i = 0; i < TX_ALLOC_TYPE_MAX; ++i) {
			size_t txn_stat = txn->alloc_stats[i];
			stats->tx_total[i] += txn_stat;
			if (txn_stat > stats->tx_max[i])
				stats->tx_max[i] = txn_stat;
		}
	}
	stats->txn_count = txn_count;
}

void
memtx_tx_register_txn(struct txn *tx)
{
	tx->memtx_tx_alloc_stats =
		xregion_alloc_array(&tx->region,
				    typeof(*tx->memtx_tx_alloc_stats),
				    MEMTX_TX_ALLOC_TYPE_MAX);
	memset(tx->memtx_tx_alloc_stats, 0,
	       sizeof(*tx->memtx_tx_alloc_stats) * MEMTX_TX_ALLOC_TYPE_MAX);
	rlist_add_tail(&txm.all_txs, &tx->in_all_txs);
}

void
memtx_tx_acquire_ddl(struct txn *tx)
{
	tx->is_schema_changed = true;
	(void) txn_can_yield(tx, false);
}

/**
 * Fully temporary and remote transactions cannot possibly conflict each other
 * by definition, so we can filter them out when aborting transactions for DDL.
 * Temporary space transactions are a always nop, since they never have WAL rows
 * associated with them.
 */
static bool
memtx_tx_filter_temporary_and_remote_txs(struct txn *txn1, struct txn *txn2)
{
	return (txn_is_fully_temporary(txn1) && txn_is_fully_remote(txn2)) ||
	       (txn_is_fully_remote(txn1) && txn_is_fully_temporary(txn2));
}

/**
 * Clean and clear all read lists of @a txn.
 */
static void
memtx_tx_clear_txn_read_lists(struct txn *txn);

void
memtx_tx_abort_all_for_ddl(struct txn *ddl_owner)
{
	struct txn *to_be_aborted;
	rlist_foreach_entry(to_be_aborted, &txm.all_txs, in_all_txs) {
		if (to_be_aborted == ddl_owner)
			continue;
		if (txn_has_flag(to_be_aborted, TXN_HANDLES_DDL))
			continue;
		if (to_be_aborted->status != TXN_INPROGRESS &&
		    to_be_aborted->status != TXN_IN_READ_VIEW)
			continue;
		if (memtx_tx_filter_temporary_and_remote_txs(ddl_owner,
							     to_be_aborted))
			continue;
		to_be_aborted->status = TXN_ABORTED;
		txn_set_flags(to_be_aborted, TXN_IS_CONFLICTED);
		memtx_tx_clear_txn_read_lists(to_be_aborted);
		say_warn("Transaction processing DDL (id=%lld) has aborted "
			 "another TX (id=%lld)", (long long) ddl_owner->id,
			 (long long) to_be_aborted->id);
	}
}

/**
 * Fix position of @a txn in global read view list to preserve the list to
 * be ordered by rv_psn. Can only move txn to the beginning of the list.
 * The function must be called when a transaction A sends itself to a read view
 * (perhaps a deeper read view in case when it's already in a read view) because
 * it has to skip a statement of another B, prepared transaction.
 * The transaction is always added to the tail of read view list, but in this
 * case there's no guarantee that psn of B is the greatest psn of all prepared
 * transactions, so we have to additionally and push A in the global read view
 * list, jumping over read views with greater rv_psn.
 */
static void
memtx_tx_adjust_position_in_read_view_list(struct txn *txn)
{
	if (txn->in_read_view_txs.prev == &txm.read_view_txs)
		return; /* No transaction before */
	struct txn *prev_txn = rlist_prev_entry(txn, in_read_view_txs);
	if (prev_txn->rv_psn <= txn->rv_psn)
		return; /* The order is already correct. */
	/* Remove from list for a while. */
	rlist_del(&txn->in_read_view_txs);
	while (prev_txn->in_read_view_txs.prev != &txm.read_view_txs) {
		struct txn *scan = rlist_prev_entry(prev_txn, in_read_view_txs);
		if (scan->rv_psn <= txn->rv_psn)
			break;
		prev_txn = scan;
	}
	/* Insert before prev_txn. */
	rlist_add_tail(&prev_txn->in_read_view_txs, &txn->in_read_view_txs);
}

/**
 * Mark @a victim as conflicted and abort it.
 * Does nothing if the transaction is already aborted.
 */
static void
memtx_tx_abort_with_conflict(struct txn *victim)
{
	if (victim->status == TXN_ABORTED)
		return;
	if (victim->status == TXN_IN_READ_VIEW)
		rlist_del(&victim->in_read_view_txs);
	victim->status = TXN_ABORTED;
	txn_set_flags(victim, TXN_IS_CONFLICTED);
}

/**
 * Handle conflict when @a victim has read and @a breaker has written the same
 * key, and @a breaker is prepared. The functions must be called in two cases:
 * 1. @a breaker becomes prepared for every victim with non-empty intersection
 * of victim read set / breaker write set.
 * 2. @a victim has to read confirmed value and skips the value that prepared
 * @a breaker wrote.
 * If @a victim is read-only or hasn't made any changes, it should be sent
 * to read view, in which is will not see @a breaker's changes. If @a victim
 * is already in a read view - a read view that does not see every breaker
 * changes is chosen.
 * Otherwise @a victim must be marked as conflicted and aborted on occasion.
 *
 * NB: can trigger story garbage collection.
 */
static void
memtx_tx_handle_conflict(struct txn *breaker, struct txn *victim)
{
	assert(breaker != victim);
	assert(breaker->psn != 0);
	assert(victim->psn == 0);
	if (victim->status != TXN_INPROGRESS &&
	    victim->status != TXN_IN_READ_VIEW) {
		/* Was conflicted by somebody else. */
		return;
	}
	if (stailq_empty(&victim->stmts)) {
		assert((victim->status == TXN_IN_READ_VIEW) ==
		       (victim->rv_psn != 0));
		/* Send to read view, perhaps a deeper one. */
		if (victim->status != TXN_IN_READ_VIEW) {
			victim->status = TXN_IN_READ_VIEW;
			victim->rv_psn = breaker->psn;
			rlist_add_tail(&txm.read_view_txs,
				       &victim->in_read_view_txs);
		} else if (victim->rv_psn > breaker->psn) {
			/*
			 * Note that in every case for every key we may choose
			 * any read view psn between confirmed level and the
			 * oldest prepared transaction that changes that key.
			 * But we choose the latest level because it generally
			 * costs less, and if there are several breakers - we
			 * must sequentially decrease read view level.
			 */
			victim->rv_psn = breaker->psn;
			assert(victim->rv_psn != 0);
		}
		memtx_tx_adjust_position_in_read_view_list(victim);
	} else {
		/* Mark as conflicted. */
		memtx_tx_abort_with_conflict(victim);
	}
}

/**
 * Calculate size of story with its links.
 */
static inline size_t
memtx_story_size(struct memtx_story *story)
{
	struct mempool *pool = &txm.memtx_tx_story_pool[story->index_count];
	return pool->objsize;
}

/**
 * Notify memory manager that a tuple referenced by @a story
 * was replaced from primary key and that is why @a story
 * is the only reason why the tuple cannot be deleted.
 */
static inline void
memtx_tx_story_track_retained_tuple(struct memtx_story *story)
{
	assert(!story->tuple_is_retained);
	assert(story->status < MEMTX_TX_STORY_STATUS_MAX);

	story->tuple_is_retained = true;
	struct memtx_tx_stats *stats = &txm.retained_tuple_stats[story->status];
	size_t tuplesize = tuple_size(story->tuple);
	memtx_tx_stats_collect(stats, tuplesize);
}

/**
 * Notify memory manager that a tuple referenced by @a story
 * was placed to primary key.
 */
static inline void
memtx_tx_story_untrack_retained_tuple(struct memtx_story *story)
{
	assert(story->tuple_is_retained);
	assert(story->status < MEMTX_TX_STORY_STATUS_MAX);

	story->tuple_is_retained = false;
	struct memtx_tx_stats *stats = &txm.retained_tuple_stats[story->status];
	size_t tuplesize = tuple_size(story->tuple);
	memtx_tx_stats_discard(stats, tuplesize);
}

/** Set status of story (see memtx_tx_story_status) */
static inline void
memtx_tx_story_set_status(struct memtx_story *story,
			  enum memtx_tx_story_status new_status)
{
	assert(story->status < MEMTX_TX_STORY_STATUS_MAX);
	enum memtx_tx_story_status old_status = story->status;
	if (old_status == new_status)
		return;
	story->status = new_status;
	struct memtx_tx_stats *old_story_stats = &txm.story_stats[old_status];
	struct memtx_tx_stats *new_story_stats = &txm.story_stats[new_status];
	size_t story_size = memtx_story_size(story);
	memtx_tx_stats_discard(old_story_stats, story_size);
	memtx_tx_stats_collect(new_story_stats, story_size);
	if (story->tuple_is_retained) {
		size_t tuplesize = tuple_size(story->tuple);
		struct memtx_tx_stats *old =
			&txm.retained_tuple_stats[old_status];
		struct memtx_tx_stats *new =
			&txm.retained_tuple_stats[new_status];
		memtx_tx_stats_discard(old, tuplesize);
		memtx_tx_stats_collect(new, tuplesize);
	}
}

/**
 * Use this method to ref tuple that belongs to @a story
 * by primary index. Do not use bare tuple_ref!!!
 */
static inline void
memtx_tx_ref_to_primary(struct memtx_story *story)
{
	assert(story != NULL);
	tuple_ref(story->tuple);
	if (story->tuple_is_retained)
		memtx_tx_story_untrack_retained_tuple(story);
}

/**
 * Use this method to unref tuple that belongs to @a story
 * from primary index. Do not use bare tuple_unref!!!
 */
static inline void
memtx_tx_unref_from_primary(struct memtx_story *story)
{
	assert(story != NULL);
	tuple_unref(story->tuple);
	if (!story->tuple_is_retained)
		memtx_tx_story_track_retained_tuple(story);
}

/**
 * Create a new story and link it with the @a tuple.
 * There are two known scenarios of using this function:
 * * The story is created for a clean tuple that is in space (and thus in
 *   space indexes) now. Such a story is a top of degenerate chains that
 *   consist of this story only.
 * * The story is created for a new tuple that is to be inserted into space.
 *   Such a story will become the top of chains, and a special function
 *   memtx_tx_story_link_top must be called for that.
 * In any case this story is expected to be a top of chains, so we set
 * in_index members in story links to appropriate values.
 */
static struct memtx_story *
memtx_tx_story_new(struct space *space, struct tuple *tuple)
{
	txm.must_do_gc_steps += TX_MANAGER_GC_STEPS_SIZE;
	assert(!tuple_has_flag(tuple, TUPLE_IS_DIRTY));
	uint32_t index_count = space->index_count;
	assert(index_count < BOX_INDEX_MAX);
	struct mempool *pool = &txm.memtx_tx_story_pool[index_count];
	struct memtx_story *story = (struct memtx_story *)xmempool_alloc(pool);
	story->tuple = tuple;

	const struct memtx_story **put_story =
		(const struct memtx_story **) &story;
	struct memtx_story *replaced = NULL;
	struct memtx_story **preplaced = &replaced;
	mh_history_put(txm.history, put_story, &preplaced, 0);
	assert(preplaced == NULL);
	tuple_set_flag(tuple, TUPLE_IS_DIRTY);
	tuple_ref(tuple);
	story->status = MEMTX_TX_STORY_USED;
	struct memtx_tx_stats *stats = &txm.story_stats[story->status];
	memtx_tx_stats_collect(stats, pool->objsize);
	story->tuple_is_retained = false;
	story->index_count = index_count;
	story->add_stmt = NULL;
	story->add_psn = 0;
	story->del_stmt = NULL;
	story->del_psn = 0;
	rlist_create(&story->reader_list);
	rlist_add_tail(&txm.all_stories, &story->in_all_stories);
	rlist_add(&space->memtx_stories, &story->in_space_stories);
	for (uint32_t i = 0; i < index_count; i++) {
		story->link[i].newer_story = story->link[i].older_story = NULL;
		rlist_create(&story->link[i].read_gaps);
		story->link[i].in_index = space->index[i];
	}
	return story;
}

/**
 * Deletes a story. Expects the story to be fully unlinked.
 */
static void
memtx_tx_story_delete(struct memtx_story *story)
{
	assert(story->add_stmt == NULL);
	assert(story->del_stmt == NULL);
	assert(rlist_empty(&story->reader_list));
	for (uint32_t i = 0; i < story->index_count; i++) {
		assert(story->link[i].newer_story == NULL);
		assert(story->link[i].older_story == NULL);
		assert(rlist_empty(&story->link[i].read_gaps));
	}

	memtx_tx_stats_discard(&txm.story_stats[story->status],
			       memtx_story_size(story));
	if (story->tuple_is_retained)
		memtx_tx_story_untrack_retained_tuple(story);

	if (txm.traverse_all_stories == &story->in_all_stories)
		txm.traverse_all_stories = rlist_next(txm.traverse_all_stories);
	rlist_del(&story->in_all_stories);
	rlist_del(&story->in_space_stories);

	mh_int_t pos = mh_history_find(txm.history, story->tuple, 0);
	assert(pos != mh_end(txm.history));
	mh_history_del(txm.history, pos, 0);

	tuple_clear_flag(story->tuple, TUPLE_IS_DIRTY);
	tuple_unref(story->tuple);

	struct mempool *pool = &txm.memtx_tx_story_pool[story->index_count];
	mempool_free(pool, story);
}

/**
 * Find a story of a @a tuple. The story expected to be present (assert).
 */
static struct memtx_story *
memtx_tx_story_get(struct tuple *tuple)
{
	assert(tuple_has_flag(tuple, TUPLE_IS_DIRTY));

	mh_int_t pos = mh_history_find(txm.history, tuple, 0);
	assert(pos != mh_end(txm.history));
	struct memtx_story *story = *mh_history_node(txm.history, pos);
	if (story->add_stmt != NULL)
		assert(story->add_psn == story->add_stmt->txn->psn);
	if (story->del_stmt != NULL)
		assert(story->del_psn == story->del_stmt->txn->psn);
	return story;
}

/**
 * Link that @a story was added by @a stmt.
 */
static void
memtx_tx_story_link_added_by(struct memtx_story *story,
			     struct txn_stmt *stmt)
{
	assert(story->add_stmt == NULL);
	assert(stmt->add_story == NULL);
	story->add_stmt = stmt;
	stmt->add_story = story;
}

/**
 * Unlink that @a story from @a stmt which added it.
 * Effectively undo memtx_tx_story_link_added_by.
 */
static void
memtx_tx_story_unlink_added_by(struct memtx_story *story,
			       struct txn_stmt *stmt)
{
	assert(stmt->add_story == story);
	assert(story->add_stmt == stmt);
	stmt->add_story = NULL;
	story->add_stmt = NULL;
}

/**
 * Link that @a story was deleted by @a stmt.
 */
static void
memtx_tx_story_link_deleted_by(struct memtx_story *story,
			       struct txn_stmt *stmt)
{
	assert(stmt->del_story == NULL);
	assert(stmt->next_in_del_list == NULL);

	stmt->del_story = story;
	stmt->next_in_del_list = story->del_stmt;
	story->del_stmt = stmt;
}

/**
 * Unlink that @a story from @a stmt that deleted it.
 * Effectively undo memtx_tx_story_link_deleted_by.
 */
static void
memtx_tx_story_unlink_deleted_by(struct memtx_story *story,
				 struct txn_stmt *stmt)
{
	assert(stmt->del_story == story);

	/* Find a place in list from which stmt must be deleted. */
	struct txn_stmt **ptr = &story->del_stmt;
	while (*ptr != stmt) {
		ptr = &(*ptr)->next_in_del_list;
		assert(ptr != NULL);
	}
	*ptr = stmt->next_in_del_list;
	stmt->next_in_del_list = NULL;
	stmt->del_story = NULL;
}

/**
 * Link a @a story with @a old_story in @a index (in both directions).
 * @a old_story is allowed to be NULL.
 */
static void
memtx_tx_story_link(struct memtx_story *story,
		    struct memtx_story *old_story,
		    uint32_t idx)
{
	assert(idx < story->index_count);
	struct memtx_story_link *link = &story->link[idx];
	assert(link->older_story == NULL);

	if (old_story == NULL)
		return;

	assert(idx < old_story->index_count);
	struct memtx_story_link *old_link = &old_story->link[idx];
	assert(old_link->newer_story == NULL);

	link->older_story = old_story;
	old_link->newer_story = story;
}

/**
 * Unlink a @a story with @a old_story in @a index (in both directions).
 * Older story is allowed to be NULL.
 */
static void
memtx_tx_story_unlink(struct memtx_story *story,
		      struct memtx_story *old_story,
		      uint32_t idx)
{
	assert(idx < story->index_count);
	struct memtx_story_link *link = &story->link[idx];
	assert(link->older_story == old_story);

	if (old_story == NULL)
		return;

	assert(idx < old_story->index_count);
	struct memtx_story_link *old_link = &old_story->link[idx];
	assert(old_link->newer_story == story);

	link->older_story = NULL;
	old_link->newer_story = NULL;
}

/**
 * Link a @a new_top with @a old_top in @a idx (in both directions), where
 * @a old_top was at the top of chain.
 * There are two different but close in implementation scenarios in which
 * this function should be used:
 * * @a is_new_tuple is true:
 *   @a new_top is a newly created story of a new tuple, that (by design) was
 *   just inserted into indexes. @a old_top is the story that was previously
 *   in the top of chain or NULL if the chain was empty.
 * * @a is_new_tuple is false:
 *   @a old_top was in the top of chain while @a new_top was a story next to it,
 *   and the chain must be reordered and @a new_top must become at the top of
 *   chain and @a old_top must be linked after it. This case also requires
 *   physical replacement in index - it will point to new_top->tuple.
 */
static void
memtx_tx_story_link_top(struct memtx_story *new_top,
			struct memtx_story *old_top,
			uint32_t idx, bool is_new_tuple)
{
	assert(old_top != NULL || is_new_tuple);
	if (is_new_tuple && old_top == NULL) {
		if (idx == 0)
			memtx_tx_ref_to_primary(new_top);
		return;
	}
	struct memtx_story_link *new_link = &new_top->link[idx];
	struct memtx_story_link *old_link = &old_top->link[idx];
	assert(old_link->in_index != NULL);
	assert(old_link->newer_story == NULL);
	if (is_new_tuple) {
		assert(new_link->newer_story == NULL);
		assert(new_link->older_story == NULL);
	} else {
		assert(new_link->newer_story == old_top);
		assert(old_link->older_story == new_top);
	}

	if (!is_new_tuple) {
		/* Make the change in index. */
		struct index *index = old_link->in_index;
		struct tuple *removed, *unused;
		if (index_replace(index, old_top->tuple, new_top->tuple,
				  DUP_REPLACE, &removed, &unused) != 0) {
			diag_log();
			unreachable();
			panic("failed to rebind story in index");
		}
		assert(old_top->tuple == removed);
	}

	/* Link the list. */
	if (is_new_tuple) {
		memtx_tx_story_link(new_top, old_top, idx);
		/* in_index must be set in story_new. */
		assert(new_link->in_index == old_link->in_index);
		old_link->in_index = NULL;
	} else {
		struct memtx_story *older_story = new_link->older_story;
		memtx_tx_story_unlink(old_top, new_top, idx);
		memtx_tx_story_unlink(new_top, older_story, idx);
		memtx_tx_story_link(new_top, old_top, idx);
		memtx_tx_story_link(old_top, older_story, idx);
		new_link->in_index = old_link->in_index;
		old_link->in_index = NULL;
	}

	/*
	 * A space holds references to all his tuples.
	 * All tuples that are physically in the primary index are referenced.
	 * Thus we have to reference the tuple that was added to the primary
	 * index and dereference the tuple that was removed from it.
	 */
	if (idx == 0) {
		memtx_tx_ref_to_primary(new_top);
		memtx_tx_unref_from_primary(old_top);
	}

	/* Rebind gap records to the top of the list */
	rlist_splice(&new_link->read_gaps, &old_link->read_gaps);
}

/**
 * Change the order of stories in history chain.
 */
static void
memtx_tx_story_reorder(struct memtx_story *story,
		       struct memtx_story *old_story,
		       uint32_t idx)
{
	assert(idx < story->index_count);
	assert(idx < old_story->index_count);
	struct memtx_story_link *link = &story->link[idx];
	struct memtx_story_link *old_link = &old_story->link[idx];
	assert(link->older_story == old_story);
	assert(old_link->newer_story == story);
	struct memtx_story *newer_story = link->newer_story;
	struct memtx_story *older_story = old_link->older_story;

	/*
	 * We have a list of stories, and we have to reorder it.
	 *           What we have                 What we want
	 *      [ index/newer_story ]        [ index/newer_story ]
	 *      [       story       ]        [     old_story     ]
	 *      [     old_story     ]        [       story       ]
	 *      [    older_story    ]        [    older_story    ]
	 */
	if (newer_story != NULL) {
		/* Simple relink in list. */
		memtx_tx_story_unlink(newer_story, story, idx);
		memtx_tx_story_unlink(story, old_story, idx);
		memtx_tx_story_unlink(old_story, older_story, idx);

		memtx_tx_story_link(newer_story, old_story, idx);
		memtx_tx_story_link(old_story, story, idx);
		memtx_tx_story_link(story, older_story, idx);
	} else {
		/*
		 * story was in the top of history chain. In terms of reorder,
		 * we have to make old_story the new top of chain. */
		memtx_tx_story_link_top(old_story, story, idx, false);
	}
}

/**
 * Unlink @a story from all chains, must be already removed from the index.
 */
static void
memtx_tx_story_full_unlink_on_space_delete(struct memtx_story *story)
{
	for (uint32_t i = 0; i < story->index_count; i++) {
		struct memtx_story_link *link = &story->link[i];
		if (link->newer_story == NULL) {
			assert(link->in_index == NULL);
			memtx_tx_story_unlink(story, link->older_story, i);
		} else {
			/* Just unlink from list */
			link->newer_story->link[i].older_story = link->older_story;
			if (link->older_story != NULL)
				link->older_story->link[i].newer_story =
					link->newer_story;
			link->older_story = NULL;
			link->newer_story = NULL;
		}
	}
}

/**
 * Find a top story in chain of @a story by index @a ind.
 */
static struct memtx_story *
memtx_tx_story_find_top(struct memtx_story *story, uint32_t ind)
{
	while (story->link[ind].newer_story != NULL)
		story = story->link[ind].newer_story;
	return story;
}

/**
 * Unlink @a story from all chains and remove corresponding tuple from
 * indexes if necessary: used in garbage collection step and preserves the top
 * of the history chain invariant (as opposed to
 * `memtx_tx_story_full_unlink_on_space_delete`).
 */
static void
memtx_tx_story_full_unlink_story_gc_step(struct memtx_story *story)
{
	for (uint32_t i = 0; i < story->index_count; i++) {
		struct memtx_story_link *link = &story->link[i];
		if (link->newer_story == NULL) {
			/*
			 * We are at the top of the chain. That means
			 * that story->tuple is in index or the story is a
			 * rollbacked one. If the story actually deletes the
			 * tuple and is present in index, it must be deleted
			 * from index.
			 */
			assert(link->in_index != NULL);
			/*
			 * Invariant that the top of the history chain
			 * is always in the index: here we delete
			 * (sic: not replace) a tuple from the index,
			 * and  it must be the last story left in the
			 * history chain, otherwise `link->older_story`
			 * starts to be at the top of the history chain
			 * and is not present in index, which violates
			 * our invariant.
			 */
			assert(link->older_story == NULL);
			if (story->del_psn > 0 && link->in_index != NULL) {
				struct index *index = link->in_index;
				struct tuple *removed, *unused;
				if (index_replace(index, story->tuple, NULL,
						  DUP_INSERT,
						  &removed, &unused) != 0) {
					diag_log();
					unreachable();
					panic("failed to rollback change");
				}
				struct key_def *key_def = index->def->key_def;
				assert(story->tuple == removed ||
				       (removed == NULL &&
					tuple_key_is_excluded(story->tuple,
							      key_def,
							      MULTIKEY_NONE)));
				(void)key_def;
				link->in_index = NULL;
				/*
				 * All tuples in pk are referenced.
				 * Once removed it must be unreferenced.
				 */
				if (i == 0)
					memtx_tx_unref_from_primary(story);
			}

			memtx_tx_story_unlink(story, link->older_story, i);
		} else {
			/* Just unlink from list */
			link->newer_story->link[i].older_story =
				link->older_story;
			if (link->older_story != NULL)
				link->older_story->link[i].newer_story =
					link->newer_story;
			link->older_story = NULL;
			link->newer_story = NULL;
		}
	}
}

/**
 * Run one step of a crawler that traverses all stories and removes no more
 * used stories.
 */
void
memtx_tx_story_gc_step()
{
	if (txm.traverse_all_stories == &txm.all_stories) {
		/* We came to the head of the list. */
		txm.traverse_all_stories = txm.traverse_all_stories->next;
		return;
	}

	/*
	 * Lowest read view PSN.
	 * Default value is txn_next_psn because if it is not so some
	 * stories (stories produced by last txn at least) will be marked as
	 * potentially in read view even though there are no txns in read view.
	 */
	int64_t lowest_rv_psn = txn_next_psn;
	if (!rlist_empty(&txm.read_view_txs)) {
		struct txn *txn =
			rlist_first_entry(&txm.read_view_txs, struct txn,
					  in_read_view_txs);
		assert(txn->rv_psn != 0);
		lowest_rv_psn = txn->rv_psn;
	}

	struct memtx_story *story =
		rlist_entry(txm.traverse_all_stories, struct memtx_story,
			    in_all_stories);
	txm.traverse_all_stories = txm.traverse_all_stories->next;

	/**
	 * The order in which conditions are checked is important,
	 * see description of enum memtx_tx_story_status.
	 */
	if (story->add_stmt != NULL || story->del_stmt != NULL ||
	    !rlist_empty(&story->reader_list)) {
		memtx_tx_story_set_status(story, MEMTX_TX_STORY_USED);
		/* The story is used directly by some transactions. */
		return;
	}
	if (story->add_psn >= lowest_rv_psn ||
	    story->del_psn >= lowest_rv_psn) {
		memtx_tx_story_set_status(story, MEMTX_TX_STORY_READ_VIEW);
		/* The story can be used by a read view. */
		return;
	}
	for (uint32_t i = 0; i < story->index_count; i++) {
		struct memtx_story_link *link = &story->link[i];
		if (link->newer_story == NULL) {
			assert(link->in_index != NULL);
			/*
			 * We would have to unlink this tuple (and perhaps
			 * delete it from index if story->del_psn > 0), but we
			 * cannot do this since after that `link->older_story`
			 * starts to be at the top of the history chain, and it
			 * is not present in index, which violates our
			 * invariant.
			 */
			if (link->older_story != NULL) {
				memtx_tx_story_set_status(story,
							  MEMTX_TX_STORY_USED);
				return;
			}
		} else if (i > 0 && link->newer_story->add_stmt != NULL) {
			/*
			 * We need to retain the story since the newer story
			 * can get rolled back (this is maintained by delete
			 * statement list in case of primary index).
			 */
			memtx_tx_story_set_status(story,
						  MEMTX_TX_STORY_USED);
			return;
		}
		if (!rlist_empty(&link->read_gaps)) {
			memtx_tx_story_set_status(story,
						  MEMTX_TX_STORY_TRACK_GAP);
			/* The story is used for gap tracking. */
			return;
		}
	}

	/* Unlink and delete the story */
	memtx_tx_story_full_unlink_story_gc_step(story);
	memtx_tx_story_delete(story);
}

void
memtx_tx_story_gc()
{
	for (size_t i = 0; i < txm.must_do_gc_steps; i++)
		memtx_tx_story_gc_step();
	txm.must_do_gc_steps = 0;
}

/**
 * Check whether the beginning of a @a story (that is insertion of its tuple)
 * is visible for transaction @a txn.
 * @param is_prepared_ok - whether prepared, not confirmed change is acceptable.
 * @param own_change - return true if the change was made by @txn itself.
 * @return true if the story beginning is visible, false otherwise.
 */
static bool
memtx_tx_story_insert_is_visible(struct memtx_story *story, struct txn *txn,
				 bool is_prepared_ok, bool *is_own_change)
{
	*is_own_change = false;

	if (story->add_stmt != NULL && story->add_stmt->txn == txn) {
		/* Tuple is added by us (@txn). */
		*is_own_change = true;
		return true;
	}

	int64_t rv_psn = INT64_MAX;
	if (txn != NULL && txn->rv_psn != 0)
		rv_psn = txn->rv_psn;

	if (is_prepared_ok && story->add_psn != 0 && story->add_psn < rv_psn)
		return true; /* Tuple is added by another prepared TX. */

	if (story->add_psn != 0 && story->add_stmt == NULL &&
	    story->add_psn < rv_psn)
		return true; /* Tuple is added by committed TX. */

	if (story->add_psn == 0 && story->add_stmt == NULL)
		return true; /* Added long time ago. */

	return false;
}

/**
 * Check whether the end of a @a story (that is deletion of its tuple) is
 * visible for transaction @a txn.
 * @param is_prepared_ok - whether prepared, not confirmed change is acceptable.
 * @param own_change - return true if the change was made by @txn itself.
 * @return true if the story end is visible, false otherwise.
 */
static bool
memtx_tx_story_delete_is_visible(struct memtx_story *story, struct txn *txn,
				 bool is_prepared_ok, bool *is_own_change)
{
	*is_own_change = false;

	struct txn_stmt *dels = story->del_stmt;
	while (dels != NULL) {
		if (dels->txn == txn) {
			/* Tuple is deleted by us (@txn). */
			*is_own_change = true;
			return true;
		}
		dels = dels->next_in_del_list;
	}

	int64_t rv_psn = INT64_MAX;
	if (txn != NULL && txn->rv_psn != 0)
		rv_psn = txn->rv_psn;

	if (is_prepared_ok && story->del_psn != 0 && story->del_psn < rv_psn)
		return true; /* Tuple is deleted by prepared TX. */

	if (story->del_psn != 0 && story->del_stmt == NULL &&
	    story->del_psn < rv_psn)
		return true; /* Tuple is deleted by committed TX. */

	return false;
}

/**
 * Scan a history starting with @a story in @a index for a @a visible_tuple.
 * If @a is_prepared_ok is true that prepared statements are visible for
 * that lookup, and not visible otherwise.
 *
 * `is_own_change` is set to true iff `visible_tuple` was modified (either
 * added or deleted) by `txn`.
 */
static void
memtx_tx_story_find_visible_tuple(struct memtx_story *story, struct txn *tnx,
				  uint32_t index, bool is_prepared_ok,
				  struct tuple **visible_tuple,
				  bool *is_own_change)
{
	for (; story != NULL; story = story->link[index].older_story) {
		assert(index < story->index_count);
		if (memtx_tx_story_delete_is_visible(story, tnx, is_prepared_ok,
						     is_own_change)) {
			*visible_tuple = NULL;
			return;
		}
		if (memtx_tx_story_insert_is_visible(story, tnx, is_prepared_ok,
						     is_own_change)) {
			*visible_tuple = story->tuple;
			return;
		}
	}
	*visible_tuple = NULL;
}

/**
 * Track the fact that transaction @a txn have read @a story in @a space.
 * This fact could lead this transaction to read view or conflict state.
 */
static void
memtx_tx_track_read_story(struct txn *txn, struct space *space,
			  struct memtx_story *story);

/**
 * Track that the @a story was read by @a txn and in index @a ind,
 * by no tuple was visible here. The @a story must be on top of chain.
 */
static void
memtx_tx_track_story_gap(struct txn *txn, struct memtx_story *story,
			 uint32_t ind);

/**
 * Check for possible conflict relations during insertion of @a new tuple,
 * (with the corresponding @a story) into index @a ind. It is needed if and
 * only if that was real insertion - there was no replaced tuple in the index.
 * It's the moment where we can search for stored point hole trackers and find
 * conflict causes. If some transactions have been reading the key in the
 * index (and found nothing) - those transactions will be removed from
 * point hole tracker and will be rebind as normal reader of given tuple.
 */
static void
memtx_tx_handle_point_hole_write(struct space *space, struct memtx_story *story,
				 uint32_t ind)
{
	assert(story->link[ind].newer_story == NULL);
	struct mh_point_holes_t *ht = txm.point_holes;
	struct point_hole_key key;
	key.index = space->index[ind];
	key.tuple = story->tuple;
	mh_int_t pos = mh_point_holes_find(ht, &key, 0);
	if (pos == mh_end(ht))
		return;
	struct point_hole_item *item = *mh_point_holes_node(ht, pos);

	struct memtx_tx_mempool *pool = &txm.point_hole_item_pool;
	bool has_more_items;
	do {
		memtx_tx_track_story_gap(item->txn, story, ind);

		struct point_hole_item *next_item =
			rlist_entry(item->ring.next,
				    struct point_hole_item, ring);
		has_more_items = next_item != item;

		rlist_del(&item->ring);
		rlist_del(&item->in_point_holes_list);
		memtx_tx_mempool_free(item->txn, pool, item);

		item = next_item;
	} while (has_more_items);

	mh_point_holes_del(ht, pos, 0);
}

static bool
memtx_tx_tuple_matches(struct key_def *def, struct tuple *tuple,
		       enum iterator_type type, const char *key,
		       uint32_t part_count)
{
	if (key == NULL) {
		assert(part_count == 0);
		assert(type == ITER_LE || type == ITER_GE);

		/* An empty key matches to any tuple. */
		return true;
	}

	int cmp = tuple_compare_with_key(tuple, HINT_NONE, key,
					 part_count, HINT_NONE, def);

	bool equal_matches = type == ITER_EQ || type == ITER_REQ ||
			     type == ITER_LE || type == ITER_GE;
	bool less_matches = type == ITER_LT || type == ITER_LE;
	bool greater_matches = type == ITER_GT || type == ITER_GE;

	return (equal_matches && cmp == 0) ||
	       (greater_matches && cmp > 0) ||
	       (less_matches && cmp < 0);
}

/**
 * Check if @a tuple is positioned prior to @a until in @a index according
 * to the iterator @a type direction and the given @a cmp_def.
 */
static bool
memtx_tx_tuple_is_before(struct key_def *cmp_def, struct tuple *tuple,
			 struct tuple *until, hint_t until_hint,
			 enum iterator_type type)
{
	int dir = iterator_direction(type);
	hint_t th = tuple_hint(tuple, cmp_def);
	int until_cmp = tuple_compare(until, until_hint,
				      tuple, th, cmp_def);
	return dir * until_cmp > 0;
}

/**
 * Check if @a tuple matches the given @a key and iterator @a type by the
 * given @a key_def and is positioned prior to @a until in index according
 * to the iterator @a type direction and the given @a cmp_def.
 *
 * The @a until parameter is optional (can be NULL).
 */
static bool
memtx_tx_tuple_matches_until(struct key_def *key_def, struct tuple *tuple,
			     enum iterator_type type, const char *key,
			     uint32_t part_count, struct key_def *cmp_def,
			     struct tuple *until, hint_t until_hint)
{
	/* Check the border (if any) using the cmp_def. */
	if (until != NULL && !memtx_tx_tuple_is_before(cmp_def, tuple, until,
						       until_hint, type))
		return false;

	return memtx_tx_tuple_matches(key_def, tuple, type, key, part_count);
}

/**
 * Check for possible conflict relations with GAP_COUNT entries during insertion
 * or deletion of tuple (with the corresponding @a story) in index @a ind. It is
 * needed if and only if there was no replaced tuple in the index for insertion
 * or in case of a deletion. It's the moment where we can search for count gaps
 * and find conflict causes. If some transactions have counted tuples by the key
 * and iterator matching the tuple - those transactions will be bind as readers
 * of the tuple.
 */
static void
memtx_tx_handle_counted_write(struct space *space, struct memtx_story *story,
			      uint32_t ind)
{
	bool is_insert = story->del_stmt == NULL;

	assert(story->link[ind].newer_story == NULL || !is_insert);

	struct index *index = space->index[ind];

	struct gap_item_base *item_base, *tmp;
	rlist_foreach_entry_safe(item_base, &index->read_gaps,
				 in_read_gaps, tmp) {
		if (item_base->type != GAP_COUNT)
			continue;

		struct count_gap_item *item =
			(struct count_gap_item *)item_base;

		bool tuple_matches = memtx_tx_tuple_matches_until(
			index->def->key_def, story->tuple, item->type,
			item->key, item->part_count, index->def->cmp_def,
			item->until, item->until_hint);

		/*
		 * Someone has counted tuples in the index by a key and iterator
		 * matching to the inserted or deleted tuple, it's a conflict.
		 */
		if (tuple_matches) {
			if (is_insert) {
				/*
				 * Record like the counted transaction had read
				 * by a key matching the tuple and got nothing
				 * there. Now this insertion is conflicting.
				 */
				memtx_tx_track_story_gap(item_base->txn,
							 story, ind);
			} else {
				/*
				 * Record like the counted transaction had read
				 * the tuple. Now this deletion is conflicting.
				 */
				memtx_tx_track_read_story(item_base->txn,
							  space, story);
			}
		}
	}
}

/**
 * Record in TX manager that a transaction @txn have read a @tuple in @space.
 *
 * NB: can trigger story garbage collection.
 */
static void
memtx_tx_track_read(struct txn *txn, struct space *space, struct tuple *tuple);

/**
 * Check that replaced tuples in space's indexes does not violate common
 * replace rules. See memtx_space_replace_all_keys comment.
 * @return 0 on success or -1 on fail.
 *
 * `is_own_change` is set to true iff `old_tuple` was modified (either
 * added or deleted) by `stmt`'s transaction.
 */
static int
check_dup(struct txn_stmt *stmt, struct tuple *new_tuple,
	  struct tuple **directly_replaced, struct tuple **old_tuple,
	  enum dup_replace_mode mode, bool *is_own_change)
{
	struct space *space = stmt->space;
	struct txn *txn = stmt->txn;

	struct tuple *visible_replaced;
	if (directly_replaced[0] == NULL ||
	    !tuple_has_flag(directly_replaced[0], TUPLE_IS_DIRTY)) {
		*is_own_change = false;
		visible_replaced = directly_replaced[0];
	} else {
		struct memtx_story *story =
			memtx_tx_story_get(directly_replaced[0]);
		memtx_tx_story_find_visible_tuple(story, txn, 0, true,
						  &visible_replaced,
						  is_own_change);
	}

	if (index_check_dup(space->index[0], *old_tuple, new_tuple,
			    visible_replaced, mode) != 0) {
		memtx_tx_track_read(txn, space, visible_replaced);
		return -1;
	}

	for (uint32_t i = 1; i < space->index_count; i++) {
		/*
		 * Check that visible tuple is NULL or the same as in the
		 * primary index, namely replaced[0].
		 */
		if (directly_replaced[i] == NULL)
			continue; /* NULL is OK in any case. */

		struct tuple *visible;
		if (!tuple_has_flag(directly_replaced[i], TUPLE_IS_DIRTY)) {
			visible = directly_replaced[i];
		} else {
			/*
			 * The replaced tuple is dirty. A chain of changes
			 * cannot lead to clean tuple, but in can lead to NULL,
			 * that's the only chance to be OK.
			 */
			struct memtx_story *story =
				memtx_tx_story_get(directly_replaced[i]);
			bool unused;
			memtx_tx_story_find_visible_tuple(story, txn,
							  i, true, &visible,
							  &unused);
		}

		if (index_check_dup(space->index[i], visible_replaced,
				    new_tuple, visible, DUP_INSERT) != 0) {
			memtx_tx_track_read(txn, space, visible);
			return -1;
		}
	}

	*old_tuple = visible_replaced;
	return 0;
}

static void
memtx_tx_track_story_gap(struct txn *txn, struct memtx_story *story,
			 uint32_t ind)
{
	assert(story->link[ind].newer_story == NULL);
	assert(txn != NULL);
	struct inplace_gap_item *item = memtx_tx_inplace_gap_item_new(txn);
	rlist_add(&story->link[ind].read_gaps, &item->base.in_read_gaps);
}

/**
 * Handle insertion to a new place in index. There can be readers which
 * have read from this gap and thus must be sent to read view or conflicted.
 */
static void
memtx_tx_handle_gap_write(struct space *space, struct memtx_story *story,
			  struct tuple *successor, uint32_t ind)
{
	assert(story->link[ind].newer_story == NULL);
	struct tuple *tuple = story->tuple;
	struct index *index = space->index[ind];
	struct gap_item_base *item_base, *tmp;
	rlist_foreach_entry_safe(item_base, &index->read_gaps,
				 in_read_gaps, tmp) {
		if (item_base->type != GAP_FULL_SCAN)
			continue;
		memtx_tx_track_story_gap(item_base->txn, story, ind);
	}
	if (successor != NULL && !tuple_has_flag(successor, TUPLE_IS_DIRTY))
		return; /* no gap records */

	struct rlist *list = &index->read_gaps;
	if (successor != NULL) {
		assert(tuple_has_flag(successor, TUPLE_IS_DIRTY));
		struct memtx_story *succ_story = memtx_tx_story_get(successor);
		assert(ind < succ_story->index_count);
		list = &succ_story->link[ind].read_gaps;
		assert(list->next != NULL && list->prev != NULL);
	}
	rlist_foreach_entry_safe(item_base, list, in_read_gaps, tmp) {
		if (item_base->type != GAP_NEARBY)
			continue;
		struct nearby_gap_item *item =
			(struct nearby_gap_item *)item_base;
		int cmp = 0;
		if (item->key != NULL) {
			struct key_def *def = index->def->key_def;
			hint_t oh =
				def->key_hint(item->key, item->part_count, def);
			hint_t kh = def->tuple_hint(tuple, def);
			cmp = def->tuple_compare_with_key(tuple, kh, item->key,
							  item->part_count, oh,
							  def);
		}
		int dir = iterator_direction(item->type);
		bool is_full_key = item->part_count ==
				   index->def->cmp_def->part_count;
		bool is_eq = item->type == ITER_EQ || item->type == ITER_REQ;
		bool is_e = item->type == ITER_LE || item->type == ITER_GE;
		bool need_split = item->key == NULL ||
				  (dir * cmp > 0 && !is_eq) ||
				  (!is_full_key && cmp == 0 && (is_e || is_eq));
		bool need_move = !need_split &&
				 ((dir < 0 && cmp > 0) ||
				  (cmp > 0 && item->type == ITER_EQ) ||
				  (cmp == 0 && ((dir < 0 && is_full_key) ||
						item->type == ITER_LT)));
		bool need_track = need_split ||
				  (is_full_key && cmp == 0 && is_e);
		if (need_track)
			memtx_tx_track_story_gap(item_base->txn, story, ind);
		if (need_split) {
			/*
			 * The insertion divided the gap into two parts.
			 * Old tracker is left in one gap, let's copy tracker
			 * to another.
			 */
			struct nearby_gap_item *copy =
				memtx_tx_nearby_gap_item_new(item_base->txn,
							     item->type,
							     item->key,
							     item->part_count);

			rlist_add(&story->link[ind].read_gaps,
				  &copy->base.in_read_gaps);
		} else if (need_move) {
			/* The tracker must be moved to the left gap. */
			rlist_del(&item->base.in_read_gaps);
			rlist_add(&story->link[ind].read_gaps,
				  &item->base.in_read_gaps);
		} else {
			assert((dir > 0 && cmp < 0) ||
			       (cmp < 0 && item->type == ITER_REQ) ||
			       (cmp == 0 && ((dir > 0 && is_full_key) ||
					     item->type == ITER_GT)));
		}
	}
}

/**
 * Helper of memtx_tx_history_add_stmt, that sets @a result pointer to
 * @old_tuple and reference is if necessary.
 */
static void
memtx_tx_history_add_stmt_prepare_result(struct tuple *old_tuple,
					 struct tuple **result)
{
	*result = old_tuple;
	if (*result != NULL) {
		/*
		 * The result must be a referenced pointer. The caller must
		 * unreference it by itself.
		 */
		tuple_ref(*result);
	}
}

/**
 * Helper of @sa memtx_tx_history_add_stmt, does actual work in case when
 * new_tuple != NULL.
 * Just for understanding, that might be:
 * REPLACE, and old_tuple is NULL because it is unknown yet.
 * INSERT, and old_tuple is NULL because there's no such tuple.
 * UPDATE, and old_tuple is not NULL and is the updated tuple.
 */
static int
memtx_tx_history_add_insert_stmt(struct txn_stmt *stmt,
				 struct tuple *old_tuple,
				 struct tuple *new_tuple,
				 enum dup_replace_mode mode,
				 struct tuple **result)
{
	assert(new_tuple != NULL);
	struct space *space = stmt->space;

	/* Process replacement in indexes. */
	struct tuple *directly_replaced[space->index_count];
	struct tuple *direct_successor[space->index_count];
	bool tuple_excluded[space->index_count];
	uint32_t directly_replaced_count = 0;
	for (uint32_t i = 0; i < space->index_count; i++) {
		struct index *index = space->index[i];
		struct tuple **replaced = &directly_replaced[i];
		struct tuple **successor = &direct_successor[i];
		*replaced = *successor = NULL;
		tuple_excluded[i] = tuple_key_is_excluded(new_tuple,
							  index->def->key_def,
							  MULTIKEY_NONE);
		if (!tuple_excluded[i] &&
		    index_replace(index, NULL, new_tuple,
				  DUP_REPLACE_OR_INSERT,
				  replaced, successor) != 0)
		{
			directly_replaced_count = i;
			goto fail;
		}
	}
	directly_replaced_count = space->index_count;

	/* Check overwritten tuple. */
	bool is_own_change = false;
	int rc = check_dup(stmt, new_tuple, directly_replaced,
			   &old_tuple, mode, &is_own_change);
	if (rc != 0)
		goto fail;
	stmt->is_own_change = is_own_change;

	/* Create add_story. */
	struct memtx_story *add_story = memtx_tx_story_new(space, new_tuple);
	memtx_tx_story_link_added_by(add_story, stmt);

	/* Create next story in the primary index if necessary. */
	struct tuple *next_pk = directly_replaced[0];
	struct memtx_story *next_pk_story = NULL;
	if (next_pk != NULL && tuple_has_flag(next_pk, TUPLE_IS_DIRTY)) {
		next_pk_story = memtx_tx_story_get(next_pk);
	} else if (next_pk != NULL) {
		next_pk_story = memtx_tx_story_new(space, next_pk);
	}

	/* Collect conflicts or form chains. */
	for (uint32_t i = 0; i < space->index_count; i++) {
		struct tuple *next = directly_replaced[i];
		struct tuple *succ = direct_successor[i];
		if (next == NULL && !tuple_excluded[i]) {
			/* Collect conflicts. */
			memtx_tx_handle_gap_write(space, add_story, succ, i);
			memtx_tx_handle_point_hole_write(space, add_story, i);
			memtx_tx_handle_counted_write(space, add_story, i);
			memtx_tx_story_link_top(add_story, NULL, i, true);
		}
		if (next != NULL) {
			/* Form chains. */
			struct memtx_story *next_story = next_pk_story;
			if (next != next_pk) {
				assert(tuple_has_flag(next, TUPLE_IS_DIRTY));
				next_story = memtx_tx_story_get(next);
			}
			memtx_tx_story_link_top(add_story, next_story, i, true);
		}
	}

	/*
	 * Now old_tuple points to a tuple that is actually replaced by this
	 * statement. Let's find its story and link with the statement.
	 */
	struct memtx_story *del_story = NULL;
	if (old_tuple != NULL) {
		/* Link story of old_tuple as deleted_by. */
		assert(tuple_has_flag(old_tuple, TUPLE_IS_DIRTY));
		if (old_tuple == next_pk)
			del_story = next_pk_story;
		else
			del_story = memtx_tx_story_get(old_tuple);
		memtx_tx_story_link_deleted_by(del_story, stmt);
	}

	/*
	 * In case of DUP_INSERT there must be no visible replaced tuple. It is
	 * correct by now (checked in check_dup), but we must prevent further
	 * insertion to this place, so we have to track gap.
	 * In case of replace we usually does not depend on presence of absence
	 * of old tuple, but if there is a trigger - it takes old_tuple (NULL or
	 * non-NULL) as a side effect, so we must track it to remain the same.
	 * Note that none of the above is needed the previous action in this
	 * point of in index is made by the same transaction. For example, if
	 * a transaction replaces, deletes and then inserts some key - no other
	 * transaction can interfere with insert: due to serialization the
	 * previous delete statement guarantees that the insert will not fail.
	 */
	if (!is_own_change &&
	    (mode == DUP_INSERT ||
	     space_has_before_replace_triggers(stmt->space) ||
	     space_has_on_replace_triggers(stmt->space))) {
		assert(mode != DUP_INSERT || del_story == NULL);
		if (del_story == NULL)
			memtx_tx_track_story_gap(stmt->txn, add_story, 0);
		else
			memtx_tx_track_read_story(stmt->txn, space, del_story);
	}

	/* Finalize the result. */
	memtx_tx_history_add_stmt_prepare_result(old_tuple, result);
	return 0;

fail:
	for (uint32_t i = directly_replaced_count - 1; i + 1 > 0; i--) {
		struct index *index = space->index[i];
		struct tuple *unused;
		if (index_replace(index, new_tuple, directly_replaced[i],
				  DUP_INSERT, &unused, &unused) != 0) {
			diag_log();
			unreachable();
			panic("failed to rollback change");
		}
	}

	return -1;
}

/**
 * Helper of @sa memtx_tx_history_add_stmt, does actual work in case when
 * new_tuple == NULL and old_tuple is deleted (and obviously not NULL).
 * Just for understanding, that's a DELETE statement.
 */
static int
memtx_tx_history_add_delete_stmt(struct txn_stmt *stmt,
				 struct tuple *old_tuple,
				 struct tuple **result)
{
	/*
	 * Find deleted story and link it with the statement.
	 * The funny thing is that specific API of space->replace function
	 * requires old_tuple as an argument, which can only be acquired
	 * through mvcc clarification. That means that the story of old_tuple
	 * must have been already created and it already contains a read
	 * record by this transaction. That's why we expect old_tuple to
	 * be dirty and do not set read tracker as would be logically
	 * correct in this function, something like that:
	 * memtx_tx_track_read_story(stmt->txn, stmt->space, del_story)
	 */
	assert(tuple_has_flag(old_tuple, TUPLE_IS_DIRTY));
	struct memtx_story *del_story = memtx_tx_story_get(old_tuple);
	if (del_story->add_stmt != NULL)
		stmt->is_own_change = del_story->add_stmt->txn == stmt->txn;
	memtx_tx_story_link_deleted_by(del_story, stmt);

	/*
	 * The tuple is deleted from the space, let's see if anyone had
	 * counted it in the indexes the tuple is contained in.
	 */
	struct space *space = stmt->space;
	for (uint32_t i = 0; i < space->index_count; i++) {
		if (!tuple_key_is_excluded(del_story->tuple,
					   space->index[i]->def->key_def,
					   MULTIKEY_NONE)) {
			memtx_tx_handle_counted_write(space, del_story, i);
		}
	}

	/* Notify statistics. */
	if (!del_story->tuple_is_retained)
		memtx_tx_story_track_retained_tuple(del_story);

	/* Finalize the result. */
	memtx_tx_history_add_stmt_prepare_result(old_tuple, result);
	return 0;
}

int
memtx_tx_history_add_stmt(struct txn_stmt *stmt, struct tuple *old_tuple,
			  struct tuple *new_tuple, enum dup_replace_mode mode,
			  struct tuple **result)
{
	assert(stmt != NULL);
	assert(stmt->space != NULL && !stmt->space->def->opts.is_ephemeral);
	assert(new_tuple != NULL || old_tuple != NULL);
	assert(new_tuple == NULL || !tuple_has_flag(new_tuple, TUPLE_IS_DIRTY));

	memtx_tx_story_gc();
	if (new_tuple != NULL)
		return memtx_tx_history_add_insert_stmt(stmt, old_tuple,
							new_tuple, mode,
							result);
	else
		return memtx_tx_history_add_delete_stmt(stmt, old_tuple,
							result);
}

/*
 * Abort with conflict all transactions that have read @a story.
 */
static void
memtx_tx_abort_story_readers(struct memtx_story *story)
{
	struct tx_read_tracker *tracker, *tmp;
	rlist_foreach_entry_safe(tracker, &story->reader_list,
				 in_reader_list, tmp)
		memtx_tx_abort_with_conflict(tracker->reader);
}

/*
 * Rollback addition of story by statement.
 */
static void
memtx_tx_history_rollback_added_story(struct txn_stmt *stmt)
{
	struct memtx_story *add_story = stmt->add_story;
	struct memtx_story *del_story = stmt->del_story;

	/*
	 * In case of rollback of prepared statement we need to rollback
	 * preparation actions and abort other transactions that managed
	 * to read this prepared state.
	 */
	if (stmt->txn->psn != 0) {
		/*
		 * During preparation of this statement there were two cases:
		 * * del_story != NULL: all in-progress transactions that were
		 *   to delete del_story were relinked to delete add_story.
		 * * del_story == NULL: all in-progress transactions that were
		 *   to delete same nothing were relinked to delete add_story.
		 * See memtx_tx_history_prepare_insert_stmt for details.
		 * Note that by design of rollback, all statements are rolled
		 * back in reverse order, and thus at this point there can be no
		 * statements of the same transaction that deletes add_story.
		 * So we must scan delete statements and relink them to delete
		 * del_story if it's not NULL or to delete nothing otherwise.
		 */
		struct txn_stmt **from = &add_story->del_stmt;
		while (*from != NULL) {
			struct txn_stmt *test_stmt = *from;
			assert(test_stmt->del_story == add_story);
			assert(test_stmt->txn != stmt->txn);
			assert(!test_stmt->is_own_change);
			assert(test_stmt->txn->psn == 0);

			/* Unlink from add_story list. */
			*from = test_stmt->next_in_del_list;
			test_stmt->next_in_del_list = NULL;
			test_stmt->del_story = NULL;

			if (del_story != NULL) {
				/* Link to del_story's list. */
				memtx_tx_story_link_deleted_by(del_story,
							       test_stmt);
			}
		}

		/* Revert psn assignment. */
		add_story->add_psn = 0;
		if (del_story != NULL)
			del_story->del_psn = 0;

		/*
		 * If a transaction managed to read this story it must
		 * be aborted.
		 */
		memtx_tx_abort_story_readers(add_story);
	}

	/* Unlink stories from the statement. */
	memtx_tx_story_unlink_added_by(add_story, stmt);
	if (del_story != NULL)
		memtx_tx_story_unlink_deleted_by(del_story, stmt);

	/*
	 * Sink the story to the end of chain and mark is as deleted long
	 * time ago (with some very low del_psn). After that the story will
	 * be invisible to any reader (that's what is needed) and still be
	 * able to store read set, if necessary.
	 */
	for (uint32_t i = 0; i < add_story->index_count; ) {
		struct memtx_story *old_story = add_story->link[i].older_story;
		if (old_story == NULL) {
			/* Old story is absent. */
			i++; /* Go to the next index. */
			continue;
		}
		memtx_tx_story_reorder(add_story, old_story, i);
	}
	add_story->del_psn = MEMTX_TX_ROLLBACKED_PSN;
}

/*
 * Abort with conflict all transactions that have read absence of @a story.
 */
static void
memtx_tx_abort_gap_readers(struct memtx_story *story)
{
	for (uint32_t i = 0; i < story->index_count; i++) {
		/*
		 * We rely on the fact that all gap trackers are stored in the
		 * top story of history chain.
		 */
		struct memtx_story *top = memtx_tx_story_find_top(story, i);
		struct gap_item_base *item, *tmp;
		rlist_foreach_entry_safe(item, &top->link[i].read_gaps,
					 in_read_gaps, tmp) {
			if (item->type != GAP_INPLACE)
				continue;
			memtx_tx_abort_with_conflict(item->txn);
		}
	}
}

/*
 * Rollback deletion of story by statement.
 */
static void
memtx_tx_history_rollback_deleted_story(struct txn_stmt *stmt)
{
	struct memtx_story *del_story = stmt->del_story;

	/*
	 * In case of rollback of prepared statement we need to rollback
	 * preparation actions and abort other transactions that managed
	 * to read this prepared state.
	 */
	if (stmt->txn->psn != 0) {
		/*
		 * During preparation of deletion we could unlink other
		 * transactions that want to overwrite this story. Now we have
		 * to restore the link. Replace-like statements can be found in
		 * the story chain of primary index. Unfortunately DELETE
		 * statements cannot be found since after unlink they are not
		 * present in chains. The good news is that by design all their
		 * transactions are surely conflicted because of read-write
		 * conflict and thus does not matter anymore.
		 */
		struct memtx_story *test_story;
		for (test_story = del_story->link[0].newer_story;
		     test_story != NULL;
		     test_story = test_story->link[0].newer_story) {
			struct txn_stmt *test_stmt = test_story->add_stmt;
			if (test_stmt->is_own_change)
				continue;
			assert(test_stmt->txn != stmt->txn);
			assert(test_stmt->del_story == NULL);
			assert(test_stmt->txn->psn == 0);
			memtx_tx_story_link_deleted_by(del_story, test_stmt);
		}

		/* Revert psn assignment. */
		del_story->del_psn = 0;

		/*
		 * If a transaction managed to read absence this story it must
		 * be aborted.
		 */
		memtx_tx_abort_gap_readers(del_story);
	}

	/* Unlink the story from the statement. */
	memtx_tx_story_unlink_deleted_by(del_story, stmt);
}

/**
 * The helper rolls back a statements that is empty - has no stories
 * linked. It can happen due to several reasons:
 * 1. MVCC hasn't created stories for the stmt. It happens when space is
 *    ephemeral or when the statement has deleted nothing. In this case
 *    helper does nothing.
 * 2. MVCC created stories for the statement, but they were deleted due to
 *    DDL - here are 3 types of such transactions. First one is concurrent
 *    with DDL. We shouldn't roll them back because we have already handled
 *    them on DDL. Second one is DDL itself (`is_schema_changed` flag is set)
 *    since stories of all the DML operations that happened before DDL were
 *    deleted. We must roll its statements back because now the space contain
 *    all its tuples. Third type is transactions prepared before DDL. We've
 *    also removed their stories on DDL, so here we should roll them back
 *    without stories if they have failed to commit.
 */
void
memtx_tx_history_rollback_empty_stmt(struct txn_stmt *stmt)
{
	struct tuple *old_tuple = stmt->rollback_info.old_tuple;
	struct tuple *new_tuple = stmt->rollback_info.new_tuple;
	if (!stmt->txn->is_schema_changed && stmt->txn->psn == 0)
		return;
	if (stmt->space->def->opts.is_ephemeral ||
	    (old_tuple == NULL && new_tuple == NULL))
		return;
	for (size_t i = 0; i < stmt->space->index_count; i++) {
		struct tuple *unused;
		if (index_replace(stmt->space->index[i], new_tuple, old_tuple,
				  DUP_REPLACE_OR_INSERT, &unused,
				  &unused) != 0) {
			panic("failed to rebind story in index on "
			      "rollback of statement without story");
		}
	}
	/* We have no stories here so reference bare tuples instead. */
	if (new_tuple != NULL)
		tuple_unref(new_tuple);
	if (old_tuple != NULL)
		tuple_ref(old_tuple);
}

void
memtx_tx_history_rollback_stmt(struct txn_stmt *stmt)
{
	/* Consistency asserts. */
	if (stmt->add_story != NULL) {
		assert(stmt->add_story->tuple == stmt->rollback_info.new_tuple);
		assert(stmt->add_story->add_psn == stmt->txn->psn);
	}
	if (stmt->del_story != NULL)
		assert(stmt->del_story->del_psn == stmt->txn->psn);
	/*
	 * There can be no more than one prepared statement deleting a story at
	 * any point in time.
	 */
	assert(stmt->txn->psn == 0 || stmt->next_in_del_list == NULL);

	/*
	 * Note that both add_story and del_story can be NULL,
	 * see comment in memtx_tx_history_prepare_stmt.
	 */
	if (stmt->add_story != NULL)
		memtx_tx_history_rollback_added_story(stmt);
	else if (stmt->del_story != NULL)
		memtx_tx_history_rollback_deleted_story(stmt);
	else
		memtx_tx_history_rollback_empty_stmt(stmt);
	assert(stmt->add_story == NULL && stmt->del_story == NULL);
}

/**
 * Abort or send to read view readers of @a story, except the transaction
 * @a writer that is actually deletes the story.
 */
static void
memtx_tx_handle_conflict_story_readers(struct memtx_story *story,
				       struct txn *writer)
{
	struct tx_read_tracker *tracker, *tmp;
	rlist_foreach_entry_safe(tracker, &story->reader_list,
				 in_reader_list, tmp) {
		if (tracker->reader == writer)
			continue;
		memtx_tx_handle_conflict(writer, tracker->reader);
	}
}

/**
 * Abort or send to read view readers of @a story, except the transaction
 * @a writer that is actually deletes the story.
 */
static void
memtx_tx_handle_conflict_gap_readers(struct memtx_story *top_story,
				     uint32_t ind, struct txn *writer)
{
	assert(top_story->link[ind].newer_story == NULL);
	struct gap_item_base *item, *tmp;
	rlist_foreach_entry_safe(item, &top_story->link[ind].read_gaps,
				 in_read_gaps, tmp) {
		if (item->txn == writer || item->type != GAP_INPLACE)
			continue;
		memtx_tx_handle_conflict(writer, item->txn);
	}
}

/**
 * Helper of memtx_tx_history_prepare_stmt. Do the job in case when
 * stmt->add_story != NULL, that is REPLACE, INSERT, UPDATE etc.
 */
static void
memtx_tx_history_prepare_insert_stmt(struct txn_stmt *stmt)
{
	struct memtx_story *story = stmt->add_story;
	assert(story != NULL);
	/**
	 * History of a key in an index can consist of several stories.
	 * The list of stories is started with a dirty tuple that is in index.
	 * The list begins with several (or zero) of stories that are added by
	 * in-progress transactions, then the list continues with several
	 * (or zero) of prepared stories, which are followed by several
	 * (or zero) of committed stories, followed by rollbacked stories.
	 * We have the following totally ordered set over tuple stories:
	 *
	 * —————————————————————————————————————————————————> serialization time
	 * |- - - - - - - -|— — — — — -|— — — — — |— — — — — — -|— — — — — — — -
	 * | Rolled back   | Committed | Prepared | In-progress | One dirty
	 * |               |           |          |             | story in index
	 * |- - - - - - - -|— — — — — -| — — — — —|— — — — — — -|— — — — — — — —
	 *
	 * If a statement becomes prepared, the story it adds must be 'sunk' to
	 * the level of prepared stories.
	 */
	for (uint32_t i = 0; i < story->index_count; ) {
		struct memtx_story *old_story = story->link[i].older_story;
		if (old_story == NULL || old_story->add_psn != 0 ||
		    old_story->add_stmt == NULL) {
			/* Old story is absent or prepared or committed. */
			i++; /* Go to the next index. */
			continue;
		}
		memtx_tx_story_reorder(story, old_story, i);
	}

	/* Consistency asserts. */
	{
		assert(story->del_stmt == NULL ||
		       story->del_stmt->next_in_del_list == NULL);
		struct memtx_story *old_story = story->link[0].older_story;
		if (stmt->del_story == NULL)
			assert(old_story == NULL || old_story->del_psn != 0);
		else
			assert(old_story == stmt->del_story);
		(void)old_story;
	}

	/*
	 * Set newer (in-progress) statements in the primary chain to delete
	 * this story.
	 */
	if (stmt->del_story == NULL) {
		/*
		 * This statement replaced nothing. That means that before
		 * this preparation there was no visible tuple in index, and
		 * now there is.
		 * There could be some in-progress transactions that think
		 * that the replaced nothing. They must be told that they
		 * replace this tuple now.
		 */
		struct memtx_story *test_story;
		for (test_story = story->link[0].newer_story;
		     test_story != NULL;
		     test_story = test_story->link[0].newer_story) {
			struct txn_stmt *test_stmt = test_story->add_stmt;
			if (test_stmt->is_own_change)
				continue;
			assert(test_stmt->txn != stmt->txn);
			assert(test_stmt->del_story == NULL);
			assert(test_stmt->txn->psn == 0);
			memtx_tx_story_link_deleted_by(story, test_stmt);
		}
	} else {
		/*
		 * This statement replaced older story. That means that before
		 * this preparation there was another visible tuple in this
		 * place of index.
		 * There could be some in-progress transactions that think
		 * they deleted or replaced that another tuple. They must be
		 * told that they replace this tuple now.
		 */
		struct txn_stmt **from = &stmt->del_story->del_stmt;
		while (*from != NULL) {
			struct txn_stmt *test_stmt = *from;
			assert(test_stmt->del_story == stmt->del_story);
			if (test_stmt == stmt) {
				/* Leave this statement, go to the next. */
				from = &test_stmt->next_in_del_list;
				continue;
			}
			assert(test_stmt->txn != stmt->txn);
			assert(test_stmt->txn->psn == 0);

			/* Unlink from old story list. */
			*from = test_stmt->next_in_del_list;
			test_stmt->next_in_del_list = NULL;
			test_stmt->del_story = NULL;

			/* Link to story's list. */
			memtx_tx_story_link_deleted_by(story, test_stmt);
		}
	}

	/* Handle main conflicts. */
	if (stmt->del_story != NULL) {
		/*
		 * The story stmt->del_story ends by now. Every TX that
		 * depend on it must go to read view or be aborted.
		 */
		memtx_tx_handle_conflict_story_readers(stmt->del_story,
						       stmt->txn);
	} else {
		/*
		 * A tuple is inserted. Every TX that depends on absence of
		 * a tuple (in any index) must go to read view or be aborted.
		 * We check only primary index here, we will check all other
		 * indexes below.
		 */
		struct memtx_story *top_story =
			memtx_tx_story_find_top(story, 0);
		memtx_tx_handle_conflict_gap_readers(top_story, 0, stmt->txn);
	}

	/* Handle conflicts in the secondary indexes. */
	for (uint32_t i = 1; i < story->index_count; i++) {
		/*
		 * Handle secondary cross-write conflict. This case is too
		 * complicated and deserves an explanation with example.
		 * Imagine a space with primary index (pk) by the first field
		 * and secondary index (sk) by the second field.
		 * Imagine then three in-progress transactions that executes
		 * replaces {1, 1, 1}, {2, 1, 2} and {1, 1, 3} correspondingly.
		 * What must happen when the first transaction commits?
		 * Both other transactions intersect the current in the sk.
		 * But the second transaction with {2, 1, 2} must be aborted
		 * (or sent to read view) because of conflict: it now introduces
		 * duplicate insertion to the sk.
		 * On the other hand the third transactions with {1, 1, 3} has
		 * a right to live since it tends to overwrite {1, 1, 1} in
		 * both pk and sk.
		 * To handle those conflicts in general we must scan chains
		 * towards the top and check insert statements.
		 */
		struct memtx_story *newer_story = story;
		while (newer_story->link[i].newer_story != NULL) {
			newer_story = newer_story->link[i].newer_story;
			struct txn_stmt *test_stmt = newer_story->add_stmt;
			/* Don't conflict own changes. */
			if (test_stmt->txn == stmt->txn)
				continue;
			/*
			 * Ignore case when other TX executes insert after
			 * precedence delete.
			 */
			if (test_stmt->is_own_change &&
			    test_stmt->del_story == NULL)
				continue;
			/*
			 * Ignore the case when other TX overwrites in both
			 * primary and secondary index.
			 */
			if (test_stmt->del_story == story)
				continue;
			memtx_tx_handle_conflict(stmt->txn, test_stmt->txn);
		}
		/*
		 * We have already checked gap readers before for the case
		 * of insertion to the primary index.
		 * In any (replace or insert) case we must handle gap readers
		 * in the secondary indexes as well since in all kinds of
		 * statements can insert new value to secondary index.
		 * Note that newer_story is in top of chain due to previous
		 * manipulations.
		 */
		memtx_tx_handle_conflict_gap_readers(newer_story, i, stmt->txn);
	}

	/* Finally set PSNs in stories to mark them add/delete as prepared. */
	stmt->add_story->add_psn = stmt->txn->psn;
	if (stmt->del_story != NULL)
		stmt->del_story->del_psn = stmt->txn->psn;
}

/**
 * Helper of memtx_tx_history_prepare_stmt. Do the job in case when
 * stmt->add_story == NULL, that is DELETE etc.
 */
static void
memtx_tx_history_prepare_delete_stmt(struct txn_stmt *stmt)
{
	assert(stmt->add_story == NULL);
	assert(stmt->del_story != NULL);

	/*
	 * There can be other transactions that want to delete old_story.
	 * Since the story ends, all of them must be unlinked from the story.
	 */
	struct txn_stmt **from = &stmt->del_story->del_stmt;
	while (*from != NULL) {
		struct txn_stmt *test_stmt = *from;
		assert(test_stmt->del_story == stmt->del_story);
		if (test_stmt == stmt) {
			/* Leave this statement, go to the next. */
			from = &test_stmt->next_in_del_list;
			continue;
		}
		assert(test_stmt->txn != stmt->txn);
		assert(test_stmt->del_story == stmt->del_story);
		assert(test_stmt->txn->psn == 0);

		/* Unlink from old story list. */
		*from = test_stmt->next_in_del_list;
		test_stmt->next_in_del_list = NULL;
		test_stmt->del_story = NULL;
	}

	/*
	 * The story stmt->del_story ends by now. Every TX that
	 * depend on it must go to read view or be aborted.
	 */
	memtx_tx_handle_conflict_story_readers(stmt->del_story, stmt->txn);

	/* Finally set PSN in story to mark its deletion as prepared. */
	stmt->del_story->del_psn = stmt->txn->psn;
}

void
memtx_tx_history_prepare_stmt(struct txn_stmt *stmt)
{
	assert(stmt->txn->psn != 0);
	assert(stmt->space != NULL);
	if (stmt->space->def->opts.is_ephemeral)
		assert(stmt->add_story == NULL && stmt->del_story == NULL);

	/*
	 * Note that both add_story and del_story can be NULL in cases:
	 * * The space is is_ephemeral.
	 * * It's an initial recovery.
	 * * It's a deletion from space by key that was not found in the space.
	 * In all these cases nothing must be done in MVCC engine.
	 */
	if (stmt->add_story != NULL)
		memtx_tx_history_prepare_insert_stmt(stmt);
	else if (stmt->del_story != NULL)
		memtx_tx_history_prepare_delete_stmt(stmt);

	memtx_tx_story_gc();
}

void
memtx_tx_prepare_finalize(struct txn *txn)
{
	/* Just free all other lists - we don't need 'em anymore. */
	memtx_tx_clear_txn_read_lists(txn);
}

void
memtx_tx_history_commit_stmt(struct txn_stmt *stmt)
{
	struct tuple *old_tuple, *new_tuple;
	old_tuple = stmt->del_story == NULL ? NULL : stmt->del_story->tuple;
	new_tuple = stmt->add_story == NULL ? NULL : stmt->add_story->tuple;
	memtx_space_update_tuple_stat(stmt->space, old_tuple, new_tuple);

	if (stmt->add_story != NULL) {
		assert(stmt->add_story->add_stmt == stmt);
		memtx_tx_story_unlink_added_by(stmt->add_story, stmt);
	}
	if (stmt->del_story != NULL) {
		assert(stmt->del_story->del_stmt == stmt);
		memtx_tx_story_unlink_deleted_by(stmt->del_story, stmt);
	}
	memtx_tx_story_gc();
}

/**
 * Helper of @sa memtx_tx_tuple_clarify.
 * Do actual work.
 */
static struct tuple *
memtx_tx_story_clarify_impl(struct txn *txn, struct space *space,
			    struct memtx_story *top_story, struct index *index,
			    uint32_t mk_index, bool is_prepared_ok)
{
	struct memtx_story *story = top_story;
	bool own_change = false;
	struct tuple *result = NULL;

	while (true) {
		if (memtx_tx_story_delete_is_visible(story, txn, is_prepared_ok,
						     &own_change)) {
			result = NULL;
			break;
		}
		if (story->del_psn != 0 && story->del_stmt != NULL &&
		    txn != NULL) {
			assert(story->del_psn == story->del_stmt->txn->psn);
			/*
			 * If we skip deletion of a tuple by prepared
			 * transaction then the transaction must be before
			 * prepared in serialization order.
			 * That can be a read view or conflict already.
			 */
			memtx_tx_handle_conflict(story->del_stmt->txn, txn);
		}

		if (memtx_tx_story_insert_is_visible(story, txn, is_prepared_ok,
						     &own_change)) {
			result = story->tuple;
			break;
		}
		if (story->add_psn != 0 && story->add_stmt != NULL &&
		    txn != NULL) {
			assert(story->add_psn == story->add_stmt->txn->psn);
			/*
			 * If we skip addition of a tuple by prepared
			 * transaction then the transaction must be before
			 * prepared in serialization order.
			 * That can be a read view or conflict already.
			 */
			memtx_tx_handle_conflict(story->add_stmt->txn, txn);
		}

		if (story->link[index->dense_id].older_story == NULL)
			break;
		story = story->link[index->dense_id].older_story;
	}
	if (txn != NULL && !own_change) {
		/*
		 * If the result tuple exists (is visible) - it is visible in
		 * every index. But if we found a story of deleted tuple - we
		 * should record that only in the given index this transaction
		 * have found nothing by this key.
		 */
		if (result == NULL)
			memtx_tx_track_story_gap(txn, top_story,
						 index->dense_id);
		else
			memtx_tx_track_read_story(txn, space, story);
	}
	if (mk_index != 0) {
		assert(false); /* TODO: multiindex */
		panic("multikey indexes are not supported int TX manager");
	}
	return result;
}

/**
 * Helper of @sa memtx_tx_tuple_clarify.
 * Do actual work.
 */
static struct tuple *
memtx_tx_tuple_clarify_impl(struct txn *txn, struct space *space,
			    struct tuple *tuple, struct index *index,
			    uint32_t mk_index, bool is_prepared_ok)
{
	assert(tuple_has_flag(tuple, TUPLE_IS_DIRTY));
	struct memtx_story *story = memtx_tx_story_get(tuple);
	return memtx_tx_story_clarify_impl(txn, space, story, index,
					   mk_index, is_prepared_ok);
}

/**
 * Helper of @sa memtx_tx_tuple_clarify.
 * Detect whether the transaction can see prepared, but unconfirmed commits.
 */
static bool
detect_whether_prepared_ok(struct txn *txn, uint32_t space_id)
{
	if (space_id_is_system(space_id))
		return true;
	if (txn == NULL)
		return false;
	else if (txn->isolation == TXN_ISOLATION_READ_COMMITTED)
		return true;
	else if (txn->isolation == TXN_ISOLATION_READ_CONFIRMED ||
		 txn->isolation == TXN_ISOLATION_LINEARIZABLE)
		return false;
	assert(txn->isolation == TXN_ISOLATION_BEST_EFFORT);
	/*
	 * The best effort that we can make is to determine whether the
	 * transaction is read-only or not. For read only (including autocommit
	 * select, that is txn == NULL) we should see only confirmed changes,
	 * ignoring prepared. For read-write transaction we should see prepared
	 * changes in order to avoid conflicts.
	 */
	return !stailq_empty(&txn->stmts);
}

/**
 * Helper of @sa memtx_tx_tuple_clarify.
 * Detect is_prepared_ok flag and pass the job to memtx_tx_tuple_clarify_impl.
 */
struct tuple *
memtx_tx_tuple_clarify_slow(struct txn *txn, struct space *space,
			    struct tuple *tuple, struct index *index,
			    uint32_t mk_index)
{
	if (!tuple_has_flag(tuple, TUPLE_IS_DIRTY)) {
		memtx_tx_track_read(txn, space, tuple);
		return tuple;
	}
	bool is_prepared_ok = detect_whether_prepared_ok(txn, space_id(space));
	struct tuple *res =
		memtx_tx_tuple_clarify_impl(txn, space, tuple, index, mk_index,
					    is_prepared_ok);
	return res;
}

/**
 * Run @a code on stories of tuples actually existing in @a index of @a space.
 * Excluded tuples have their own chains consisting of the only excluded story,
 * these are skipped since they are not actually inserted to index.
 */
#define memtx_tx_foreach_in_index_tuple_story(space, index, story, code) do {  \
	rlist_foreach_entry(story, &(space)->memtx_stories, in_space_stories) {\
		assert((index)->dense_id < story->index_count);		       \
		struct memtx_story_link *link =				       \
			&story->link[index->dense_id];			       \
		if (link->in_index == NULL) {				       \
			assert(link->newer_story != NULL);		       \
			continue;					       \
		}							       \
		assert(link->newer_story == NULL);			       \
		if (tuple_key_is_excluded(story->tuple, index->def->key_def,   \
					  MULTIKEY_NONE)) {		       \
			assert(link->older_story == NULL);		       \
			continue;					       \
		}							       \
		code;							       \
	}								       \
} while (0)

uint32_t
memtx_tx_index_invisible_count_matching_until_slow(
	struct txn *txn, struct space *space, struct index *index,
	enum iterator_type type, const char *key, uint32_t part_count,
	struct tuple *until, hint_t until_hint)
{
	struct key_def *key_def = index->def->key_def;
	struct key_def *cmp_def = index->def->cmp_def;

	/*
	 * The border is only valid if it's located at or after the first
	 * tuple in the index according to the iterator direction and key.
	 */
	assert(until == NULL ||
	       memtx_tx_tuple_matches(key_def, until, type == ITER_EQ ?
				      ITER_GE : type == ITER_REQ ? ITER_LE :
				      type, key, part_count));

	uint32_t res = 0;
	struct memtx_story *story;
	memtx_tx_foreach_in_index_tuple_story(space, index, story, {
		/* All tuples in the story chain share the same key. */
		if (!memtx_tx_tuple_matches_until(key_def, story->tuple, type,
						  key, part_count, cmp_def,
						  until, until_hint))
			continue;

		struct tuple *visible = NULL;
		bool is_prepared_ok =
			detect_whether_prepared_ok(txn, space_id(space));
		bool unused;
		memtx_tx_story_find_visible_tuple(story, txn, index->dense_id,
						  is_prepared_ok, &visible,
						  &unused);
		if (visible == NULL)
			res++;
	});
	return res;
}

/**
 * Detect whether key of @a tuple from @a index is visible to @a txn.
 */
bool
memtx_tx_tuple_key_is_visible_slow(struct txn *txn, struct index *index,
				   struct tuple *tuple)
{
	if (!tuple_has_flag(tuple, TUPLE_IS_DIRTY))
		return true;

	struct memtx_story *story = memtx_tx_story_get(tuple);
	struct tuple *visible = NULL;
	bool is_prepared_ok =
		detect_whether_prepared_ok(txn, index->def->space_id);
	bool unused;
	memtx_tx_story_find_visible_tuple(story, txn, index->dense_id,
					  is_prepared_ok, &visible,
					  &unused);
	return visible != NULL;
}

/**
 * Destroy and free any kind of gap item.
 */
static void
memtx_tx_delete_gap(struct gap_item_base *item)
{
	rlist_del(&item->in_gap_list);
	rlist_del(&item->in_read_gaps);
	struct memtx_tx_mempool *pool;
	switch (item->type) {
	case GAP_INPLACE:
		pool = &txm.inplace_gap_item_mempoool;
		break;
	case GAP_NEARBY:
		pool = &txm.nearby_gap_item_mempoool;
		break;
	case GAP_COUNT:
		pool = &txm.count_gap_item_mempool;
		break;
	case GAP_FULL_SCAN:
		pool = &txm.full_scan_gap_item_mempool;
		break;
	default:
		unreachable();
	}
	memtx_tx_mempool_free(item->txn, pool, item);
}

void
memtx_tx_invalidate_space(struct space *space, struct txn *active_txn)
{
	struct memtx_story *story;
	/*
	 * Phase one: fill the indexes with actual tuples. Here we insert
	 * all tuples visible to `active_txn`.
	 */
	rlist_foreach_entry(story, &space->memtx_stories, in_space_stories) {
		assert(story->index_count == space->index_count);

		for (uint32_t i = 0; i < story->index_count; i++) {
			struct index *index = story->link[i].in_index;
			if (index == NULL)
				continue;

			/* Mark as not in index. */
			story->link[i].in_index = NULL;

			/* Skip chains of excluded tuples. */
			if (tuple_key_is_excluded(story->tuple,
						  index->def->key_def,
						  MULTIKEY_NONE))
				continue;

			struct tuple *new_tuple = NULL;
			bool is_own_change;
			memtx_tx_story_find_visible_tuple(story, active_txn, i,
							  true, &new_tuple,
							  &is_own_change);

			/* Visible tuple is already in index - do nothing. */
			if (new_tuple == story->tuple)
				continue;

			struct tuple *unused;
			if (index_replace(index, story->tuple, new_tuple,
					  DUP_REPLACE, &unused,
					  &unused) != 0) {
				diag_log();
				unreachable();
				panic("failed to rebind story in index on "
				      "space invalidation");
			}

			if (i == 0) {
				if (new_tuple != NULL) {
					memtx_tx_ref_to_primary(
						memtx_tx_story_get(new_tuple));
				}
				memtx_tx_unref_from_primary(story);
			}
		}
	}

	/*
	 * Phase two: destroy all the stories. They are expected to be unlinked
	 * from the indexes during the first phase.
	 */
	while (!rlist_empty(&space->memtx_stories)) {
		story = rlist_first_entry(&space->memtx_stories,
					  struct memtx_story,
					  in_space_stories);
		if (story->add_stmt != NULL)
			memtx_tx_story_unlink_added_by(story, story->add_stmt);
		while (story->del_stmt != NULL)
			memtx_tx_story_unlink_deleted_by(story,
							 story->del_stmt);
		memtx_tx_story_full_unlink_on_space_delete(story);
		for (uint32_t i = 0; i < story->index_count; i++) {
			struct rlist *read_gaps = &story->link[i].read_gaps;
			while (!rlist_empty(&story->link[i].read_gaps)) {
				struct gap_item_base *item =
					rlist_first_entry(read_gaps,
							  struct gap_item_base,
							  in_read_gaps);
				memtx_tx_delete_gap(item);
			}
		}
		/*
		 * Remove all read trackers since they point to the story that
		 * is going to be deleted.
		 */
		while (!rlist_empty(&story->reader_list)) {
			struct tx_read_tracker *tracker =
				rlist_first_entry(&story->reader_list,
						  struct tx_read_tracker,
						  in_reader_list);
			rlist_del(&tracker->in_reader_list);
			rlist_del(&tracker->in_read_set);
		}
		memtx_tx_story_delete(story);
	}

	/*
	 * Phase three: remove savepoints from all affected statements so that
	 * they won't be rolled back because we already did it. Moreover, they
	 * could access the old space that is going to be deleted leading to
	 * use-after-free.
	 */
	struct txn *txn;
	rlist_foreach_entry(txn, &txm.all_txs, in_all_txs) {
		if (txn->status != TXN_ABORTED || txn->psn != 0)
			continue;
		struct txn_stmt *stmt;
		stailq_foreach_entry(stmt, &txn->stmts, next) {
			if (stmt->space == space)
				stmt->engine_savepoint = NULL;
		}
	}

	/*
	 * Phase four: remove all read trackers from the space indexes. Since
	 * all concurrent transactions are aborted, we don't need them anymore.
	 */
	for (size_t i = 0; i < space->index_count; i++) {
		struct index *index = space->index[i];
		while (!rlist_empty(&index->read_gaps)) {
			struct gap_item_base *item =
				rlist_first_entry(&index->read_gaps,
						  struct gap_item_base,
						  in_read_gaps);
			memtx_tx_delete_gap(item);
		}
	}
}

/**
 * Allocate and initialize tx_read_tracker, return NULL in case of error
 * (diag is set). Links in lists are not initialized though.
 */
static struct tx_read_tracker *
tx_read_tracker_new(struct txn *reader, struct memtx_story *story)
{
	struct tx_read_tracker *tracker;
	tracker = memtx_tx_xregion_alloc_object(reader,
						MEMTX_TX_OBJECT_READ_TRACKER);
	tracker->reader = reader;
	tracker->story = story;
	return tracker;
}

/**
 * Track the fact that transaction @a txn have read @a story in @a space.
 * This fact could lead this transaction to read view or conflict state.
 */
static void
memtx_tx_track_read_story(struct txn *txn, struct space *space,
			  struct memtx_story *story)
{
	if (txn == NULL || space == NULL || space->def->opts.is_ephemeral)
		return;
	(void)space;
	assert(story != NULL);
	struct tx_read_tracker *tracker = NULL;

	struct rlist *r1 = story->reader_list.next;
	struct rlist *r2 = txn->read_set.next;
	while (r1 != &story->reader_list && r2 != &txn->read_set) {
		tracker = rlist_entry(r1, struct tx_read_tracker,
				      in_reader_list);
		assert(tracker->story == story);
		if (tracker->reader == txn)
			break;
		tracker = rlist_entry(r2, struct tx_read_tracker,
				      in_read_set);
		assert(tracker->reader == txn);
		if (tracker->story == story)
			break;
		tracker = NULL;
		r1 = r1->next;
		r2 = r2->next;
	}
	if (tracker != NULL) {
		/* Move to the beginning of a list for faster further lookups.*/
		rlist_del(&tracker->in_reader_list);
		rlist_del(&tracker->in_read_set);
	} else {
		tracker = tx_read_tracker_new(txn, story);
	}
	rlist_add(&story->reader_list, &tracker->in_reader_list);
	rlist_add(&txn->read_set, &tracker->in_read_set);
}

/**
 * Record in TX manager that a transaction @txn have read a @tuple in @space.
 *
 * NB: can trigger story garbage collection.
 */
static void
memtx_tx_track_read(struct txn *txn, struct space *space, struct tuple *tuple)
{
	if (tuple == NULL)
		return;
	if (txn == NULL || space == NULL || space->def->opts.is_ephemeral)
		return;

	if (tuple_has_flag(tuple, TUPLE_IS_DIRTY)) {
		struct memtx_story *story = memtx_tx_story_get(tuple);
		memtx_tx_track_read_story(txn, space, story);
	} else {
		struct memtx_story *story = memtx_tx_story_new(space, tuple);
		struct tx_read_tracker *tracker;
		tracker = tx_read_tracker_new(txn, story);
		rlist_add(&story->reader_list, &tracker->in_reader_list);
		rlist_add(&txn->read_set, &tracker->in_read_set);
	}
}

/**
 * Create new point_hole_item by given arguments and put it to hash table.
 */
static void
point_hole_storage_new(struct index *index, const char *key,
		       size_t key_len, struct txn *txn)
{
	struct memtx_tx_mempool *pool = &txm.point_hole_item_pool;
	struct point_hole_item *object = memtx_tx_xmempool_alloc(txn, pool);

	rlist_create(&object->ring);
	rlist_create(&object->in_point_holes_list);
	object->txn = txn;
	object->index_unique_id = index->unique_id;
	if (key_len <= sizeof(object->short_key)) {
		object->key = object->short_key;
	} else {
		object->key = memtx_tx_xregion_alloc(txn, key_len,
						     MEMTX_TX_ALLOC_TRACKER);
	}
	memcpy((char *)object->key, key, key_len);
	object->key_len = key_len;
	object->is_head = true;

	struct key_def *def = index->def->key_def;
	object->hash = object->index_unique_id ^ def->key_hash(key, def);

	const struct point_hole_item **put =
		(const struct point_hole_item **) &object;
	struct point_hole_item *replaced = NULL;
	struct point_hole_item **preplaced = &replaced;
	mh_point_holes_put(txm.point_holes, put, &preplaced, 0);
	if (preplaced != NULL) {
		/*
		 * The item in hash table was overwitten. It's OK, but
		 * we need replaced item to the item list.
		 * */
		rlist_add(&replaced->ring, &object->ring);
		assert(replaced->is_head);
		replaced->is_head = false;
	}
	rlist_add(&txn->point_holes_list, &object->in_point_holes_list);
}

static void
point_hole_storage_delete(struct point_hole_item *object)
{
	if (!object->is_head) {
		/*
		 * The deleting item is linked list, and the hash table
		 * doesn't point directly to this item. Delete from the
		 * list and that's enough.
		 */
		assert(!rlist_empty(&object->ring));
		rlist_del(&object->ring);
	} else if (!rlist_empty(&object->ring)) {
		/*
		 * Hash table point to this item, but there are more
		 * items in the list. Relink the hash table with any other
		 * item in the list, and delete this item from the list.
		 */
		struct point_hole_item *another =
			rlist_next_entry(object, ring);

		const struct point_hole_item **put =
			(const struct point_hole_item **) &another;
		struct point_hole_item *replaced = NULL;
		struct point_hole_item **preplaced = &replaced;
		mh_point_holes_put(txm.point_holes, put, &preplaced, 0);
		assert(replaced == object);
		rlist_del(&object->ring);
		another->is_head = true;
	} else {
		/*
		 * Hash table point to this item, and it's the last in the
		 * list. We have to remove the item from the hash table.
		 */
		int exist = 0;
		const struct point_hole_item **put =
			(const struct point_hole_item **) &object;
		mh_int_t pos = mh_point_holes_put_slot(txm.point_holes, put,
						       &exist, 0);
		assert(exist);
		mh_point_holes_del(txm.point_holes, pos, 0);
	}
	rlist_del(&object->in_point_holes_list);
	struct memtx_tx_mempool *pool = &txm.point_hole_item_pool;
	memtx_tx_mempool_free(object->txn, pool, object);
}

/**
 * Record in TX manager that a transaction @a txn have read a nothing
 * from @a space and @a index with @a key.
 * The key is expected to be full, that is has part count equal to part
 * count in unique cmp_key of the index.
 */
void
memtx_tx_track_point_slow(struct txn *txn, struct index *index, const char *key)
{
	if (txn->status != TXN_INPROGRESS)
		return;

	struct key_def *def = index->def->key_def;
	const char *tmp = key;
	for (uint32_t i = 0; i < def->part_count; i++)
		mp_next(&tmp);
	size_t key_len = tmp - key;
	memtx_tx_story_gc();
	point_hole_storage_new(index, key, key_len, txn);
}

/**
 * Allocate and create inplace gap item.
 * Note that in_read_gaps base member must be initialized later.
 */
static struct inplace_gap_item *
memtx_tx_inplace_gap_item_new(struct txn *txn)
{
	struct memtx_tx_mempool *pool = &txm.inplace_gap_item_mempoool;
	struct inplace_gap_item *item = memtx_tx_xmempool_alloc(txn, pool);
	gap_item_base_create(&item->base, GAP_INPLACE, txn);
	return item;
}

/*
 * Saves the given @a key in the @a short_key buffer of size @a short_key_size
 * if it fits or allocates a new one on the @a txn's region. Returns the key
 * length in the @a key_len output parameter.
 */
static char *
memtx_tx_save_key(struct txn *txn, const char *key, uint32_t part_count,
		  char *short_key, size_t short_key_size, uint32_t *key_len)
{
	const char *tmp = key;
	for (uint32_t i = 0; i < part_count; i++)
		mp_next(&tmp);
	*key_len = tmp - key;
	if (part_count == 0)
		return NULL;
	char *result = short_key;
	if (*key_len > short_key_size) {
		result = memtx_tx_xregion_alloc(txn, *key_len,
						MEMTX_TX_ALLOC_TRACKER);
	}
	memcpy(result, key, *key_len);
	return result;
}

/**
 * Allocate and create nearby gap item.
 * Note that in_read_gaps base member must be initialized later.
 */
static struct nearby_gap_item *
memtx_tx_nearby_gap_item_new(struct txn *txn, enum iterator_type type,
			     const char *key, uint32_t part_count)
{
	struct memtx_tx_mempool *pool = &txm.nearby_gap_item_mempoool;
	struct nearby_gap_item *item = memtx_tx_xmempool_alloc(txn, pool);
	gap_item_base_create(&item->base, GAP_NEARBY, txn);

	item->type = type;
	item->part_count = part_count;
	item->key = memtx_tx_save_key(txn, key, part_count, item->short_key,
				      sizeof(item->short_key), &item->key_len);
	return item;
}

/**
 * Allocate and create count gap item. The @a until tuple's story must have
 * a gap item from the @a txn transaction or be tracked by it, so the story
 * is not deleted by the garbage collector and the tuple is not deleted (if
 * it's not NULL).
 *
 * Note that in_read_gaps base member must be initialized later.
 */
static struct count_gap_item *
memtx_tx_count_gap_item_new(struct txn *txn, enum iterator_type type,
			    const char *key, uint32_t part_count,
			    struct tuple *until, hint_t until_hint)
{
	assert(until == NULL || tuple_has_flag(until, TUPLE_IS_DIRTY));

	struct memtx_tx_mempool *pool = &txm.count_gap_item_mempool;
	struct count_gap_item *item = memtx_tx_xmempool_alloc(txn, pool);
	gap_item_base_create(&item->base, GAP_COUNT, txn);

	item->type = type;
	item->part_count = part_count;
	item->key = memtx_tx_save_key(txn, key, part_count, item->short_key,
				      sizeof(item->short_key), &item->key_len);
	item->until = until;
	item->until_hint = until_hint;

	return item;
}

/**
 * Allocate and create full scan gap item.
 * Note that in_read_gaps base member must be initialized later.
 */
static struct full_scan_gap_item *
memtx_tx_full_scan_gap_item_new(struct txn *txn)
{
	struct memtx_tx_mempool *pool = &txm.full_scan_gap_item_mempool;
	struct full_scan_gap_item *item = memtx_tx_xmempool_alloc(txn, pool);
	gap_item_base_create(&item->base, GAP_FULL_SCAN, txn);
	return item;
}

/**
 * Record in TX manager that a transaction @a txn have read nothing
 * from @a space and @a index with @a key, somewhere from interval between
 * some unknown predecessor and @a successor.
 * This function must be used for ordered indexes, such as TREE, for queries
 * when iteration type is not EQ or when the key is not full (otherwise
 * it's faster to use memtx_tx_track_point).
 */
void
memtx_tx_track_gap_slow(struct txn *txn, struct space *space, struct index *index,
			struct tuple *successor, enum iterator_type type,
			const char *key, uint32_t part_count)
{
	if (txn->status != TXN_INPROGRESS)
		return;

	struct nearby_gap_item *item =
		memtx_tx_nearby_gap_item_new(txn, type, key, part_count);

	if (successor != NULL) {
		struct memtx_story *story;
		if (tuple_has_flag(successor, TUPLE_IS_DIRTY)) {
			story = memtx_tx_story_get(successor);
		} else {
			story = memtx_tx_story_new(space, successor);
		}
		assert(index->dense_id < story->index_count);
		assert(story->link[index->dense_id].in_index != NULL);
		rlist_add(&story->link[index->dense_id].read_gaps,
			  &item->base.in_read_gaps);
	} else {
		rlist_add(&index->read_gaps, &item->base.in_read_gaps);
	}
	memtx_tx_story_gc();
}

/**
 * Record in TX manager that a transaction @a txn have counted @a index of @a
 * space by @a key and iterator @a type. This function must be used for queries
 * that count tuples in indexes (for example, index:size or index:count) or if
 * tuples are skipped by a transaction without reading.
 *
 * @return the amount of invisible tuples counted.
 *
 * @pre The @a until tuple (if not NULL) must be clarified by @a txn.
 */
uint32_t
memtx_tx_track_count_until_slow(struct txn *txn, struct space *space,
				struct index *index, enum iterator_type type,
				const char *key, uint32_t part_count,
				struct tuple *until, hint_t until_hint)
{
	struct key_def *key_def = index->def->key_def;
	struct key_def *cmp_def = index->def->cmp_def;

	/*
	 * The border is only valid if it's located at or after the first
	 * tuple in the index according to the iterator direction and key.
	 */
	assert(until == NULL ||
	       memtx_tx_tuple_matches(key_def, until, type == ITER_EQ ?
				      ITER_GE : type == ITER_REQ ? ITER_LE :
				      type, key, part_count));

	if (txn != NULL && txn->status == TXN_INPROGRESS) {
		struct count_gap_item *item = memtx_tx_count_gap_item_new(
			txn, type, key, part_count, until, until_hint);
		rlist_add(&index->read_gaps, &item->base.in_read_gaps);
	}

	/*
	 * There may be stories that we have (or have not) counted. Since we
	 * don't iterate over the counted tuples, the fact we have counted
	 * these stories is not recorded anywhere. Let's make the counting
	 * transaction a reader of the stories he has counted and gap reader
	 * of the matching stories that hadn't been counted.
	 *
	 * So rollback of counted stories will roll this TX back too, and
	 * commit of the matching not counted stories will conflict with it.
	 *
	 * The downside is that we'll not only conflict with insertions and
	 * deletions, but also with replace stories.
	 */
	uint32_t invisible_count = 0;
	struct memtx_story *story;
	memtx_tx_foreach_in_index_tuple_story(space, index, story, {
		/* All tuples in the story chain share the same key. */
		if (!memtx_tx_tuple_matches_until(key_def, story->tuple, type,
						  key, part_count, cmp_def,
						  until, until_hint))
			continue;

		/*
		 * Track the story as read or gap read and conflict with the
		 * prepared transactions whose changes are invisible to us.
		 *
		 * Let's count invisible BTW, it's free.
		 */
		bool is_prepared_ok =
			detect_whether_prepared_ok(txn, space_id(space));
		if (memtx_tx_story_clarify_impl(txn, space, story, index,
						0, is_prepared_ok) == NULL)
			invisible_count++;
	});

	memtx_tx_story_gc();
	return invisible_count;
}

/**
 * Record in TX manager that a transaction @a txn have read full @a index.
 * This function must be used for unordered indexes, such as HASH, for queries
 * when iteration type is ALL.
 */
void
memtx_tx_track_full_scan_slow(struct txn *txn, struct index *index)
{
	if (txn->status != TXN_INPROGRESS)
		return;

	struct full_scan_gap_item *item = memtx_tx_full_scan_gap_item_new(txn);
	rlist_add(&index->read_gaps, &item->base.in_read_gaps);
	memtx_tx_story_gc();
}

/**
 * Clean and clear all read lists of @a txn.
 */
static void
memtx_tx_clear_txn_read_lists(struct txn *txn)
{
	while (!rlist_empty(&txn->point_holes_list)) {
		struct point_hole_item *object =
			rlist_first_entry(&txn->point_holes_list,
					  struct point_hole_item,
					  in_point_holes_list);
		point_hole_storage_delete(object);
	}
	while (!rlist_empty(&txn->gap_list)) {
		struct gap_item_base *item =
			rlist_first_entry(&txn->gap_list,
					  struct gap_item_base,
					  in_gap_list);
		memtx_tx_delete_gap(item);
	}

	struct tx_read_tracker *tracker, *tmp;
	rlist_foreach_entry_safe(tracker, &txn->read_set,
				 in_read_set, tmp) {
		rlist_del(&tracker->in_reader_list);
		rlist_del(&tracker->in_read_set);
	}
	assert(rlist_empty(&txn->read_set));

	rlist_del(&txn->in_read_view_txs);
}

/**
 * Clean memtx_tx part of @a txm.
 */
void
memtx_tx_clean_txn(struct txn *txn)
{
	memtx_tx_clear_txn_read_lists(txn);

	rlist_del(&txn->in_all_txs);

	memtx_tx_story_gc();
}

static uint32_t
memtx_tx_snapshot_cleaner_hash(const struct tuple *a)
{
	uintptr_t u = (uintptr_t)a;
	if (sizeof(uintptr_t) <= sizeof(uint32_t))
		return u;
	else
		return u ^ (u >> 32);
}

struct memtx_tx_snapshot_cleaner_entry
{
	struct tuple *from;
	struct tuple *to;
};

#define mh_name _snapshot_cleaner
#define mh_key_t struct tuple *
#define mh_node_t struct memtx_tx_snapshot_cleaner_entry
#define mh_arg_t int
#define mh_hash(a, arg) (memtx_tx_snapshot_cleaner_hash((a)->from))
#define mh_hash_key(a, arg) (memtx_tx_snapshot_cleaner_hash(a))
#define mh_cmp(a, b, arg) (((a)->from) != ((b)->from))
#define mh_cmp_key(a, b, arg) ((a) != ((b)->from))
#define MH_SOURCE
#include "salad/mhash.h"

void
memtx_tx_snapshot_cleaner_create(struct memtx_tx_snapshot_cleaner *cleaner,
				 struct space *space)
{
	cleaner->ht = NULL;
	if (rlist_empty(&space->memtx_stories) &&
	    rlist_empty(&space->alter_stmts))
		return;
	struct mh_snapshot_cleaner_t *ht = mh_snapshot_cleaner_new();
	struct memtx_story *story;
	rlist_foreach_entry(story, &space->memtx_stories, in_space_stories) {
		struct tuple *tuple = story->tuple;
		struct tuple *clean =
			memtx_tx_tuple_clarify_impl(NULL, space, tuple,
						    space->index[0], 0, true);
		if (clean == tuple)
			continue;

		struct memtx_tx_snapshot_cleaner_entry entry;
		entry.from = tuple;
		entry.to = clean;
		mh_snapshot_cleaner_put(ht,  &entry, NULL, 0);
	}
	struct space_alter_stmt *alter_stmt;
	rlist_foreach_entry(alter_stmt, &space->alter_stmts, link) {
		struct memtx_tx_snapshot_cleaner_entry entry;
		entry.from = alter_stmt->new_tuple;
		entry.to = alter_stmt->old_tuple;
		mh_snapshot_cleaner_put(ht, &entry, NULL, 0);
	}
	cleaner->ht = ht;
}

struct tuple *
memtx_tx_snapshot_clarify_slow(struct memtx_tx_snapshot_cleaner *cleaner,
			       struct tuple *tuple)
{
	assert(cleaner->ht != NULL);

	struct mh_snapshot_cleaner_t *ht = cleaner->ht;
	while (true) {
		mh_int_t pos =  mh_snapshot_cleaner_find(ht, tuple, 0);
		if (pos == mh_end(ht))
			break;
		struct memtx_tx_snapshot_cleaner_entry *entry =
			mh_snapshot_cleaner_node(ht, pos);
		assert(entry->from == tuple);
		tuple = entry->to;
	}
	return tuple;
}

void
memtx_tx_snapshot_cleaner_destroy(struct memtx_tx_snapshot_cleaner *cleaner)
{
	if (cleaner->ht != NULL)
		mh_snapshot_cleaner_delete(cleaner->ht);
}
