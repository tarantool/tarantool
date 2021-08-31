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

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "txn.h"
#include "schema_def.h"
#include "small/mempool.h"

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

/**
 * An element that stores the fact that some transaction have read
 * a full key and found nothing.
 */
struct gap_item {
	/** A link in memtx_story_link::nearby_gaps OR index::nearby_gaps. */
	struct rlist in_nearby_gaps;
	/** Link in txn->gap_list. */
	struct rlist in_gap_list;
	/** The transaction that read it. */
	struct txn *txn;
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
 * An element that stores the fact that some transaction have read
 * a full index.
 */
struct full_scan_item {
	/** A link in index::full_scans. */
	struct rlist in_full_scans;
	/** Link in txn->full_scan_list. */
	struct rlist in_full_scan_list;
	/** The transaction that read it. */
	struct txn *txn;
};

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

struct tx_manager
{
	/**
	 * List of all transactions that are in a read view.
	 * New transactions are added to the tail of this list,
	 * so the list is ordered by rv_psn.
	 */
	struct rlist read_view_txs;
	/** Mempools for tx_story objects with different index count. */
	struct mempool memtx_tx_story_pool[BOX_INDEX_MAX];
	/** Hash table tuple -> memtx_story of that tuple. */
	struct mh_history_t *history;
	/** Mempool for point_hole_item objects. */
	struct mempool point_hole_item_pool;
	/** Hash table that hold point selects with empty result. */
	struct mh_point_holes_t *point_holes;
	/** Count of elements in point_holes table. */
	size_t point_holes_size;
	/** Mempool for gap_item objects. */
	struct mempool gap_item_mempoool;
	/** Mempool for full_scan_item objects. */
	struct mempool full_scan_item_mempool;
	/** List of all memtx_story objects. */
	struct rlist all_stories;
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
	if (txm.history == NULL)
		panic("mh_history_new()");
	mempool_create(&txm.point_hole_item_pool,
		       cord_slab_cache(), sizeof(struct point_hole_item));
	txm.point_holes = mh_point_holes_new();
	if (txm.point_holes == NULL)
		panic("mh_history_new()");
	mempool_create(&txm.gap_item_mempoool,
		       cord_slab_cache(), sizeof(struct gap_item));
	mempool_create(&txm.full_scan_item_mempool,
		       cord_slab_cache(), sizeof(struct full_scan_item));
	txm.point_holes_size = 0;
	rlist_create(&txm.all_stories);
	rlist_create(&txm.all_txs);
	txm.traverse_all_stories = &txm.all_stories;
	txm.must_do_gc_steps = 0;
}

void
memtx_tx_manager_free()
{
	for (size_t i = 0; i < BOX_INDEX_MAX; i++)
		mempool_destroy(&txm.memtx_tx_story_pool[i]);
	mh_history_delete(txm.history);
	mempool_destroy(&txm.point_hole_item_pool);
	mh_point_holes_delete(txm.point_holes);
	mempool_destroy(&txm.gap_item_mempoool);
	mempool_destroy(&txm.full_scan_item_mempool);
}

void
memtx_tx_register_tx(struct txn *tx)
{
	rlist_add_tail(&txm.all_txs, &tx->in_all_txs);
}

void
memtx_tx_acquire_ddl(struct txn *tx)
{
	tx->is_schema_changed = true;
	(void) txn_can_yield(tx, false);
}

void
memtx_tx_abort_all_for_ddl(struct txn *ddl_owner)
{
	struct txn *to_be_aborted;
	rlist_foreach_entry(to_be_aborted, &txm.all_txs, in_all_txs) {
		if (to_be_aborted == ddl_owner)
			continue;
		if (to_be_aborted->status != TXN_INPROGRESS &&
		    to_be_aborted->status != TXN_IN_READ_VIEW)
			continue;
		to_be_aborted->status = TXN_CONFLICTED;
		say_warn("Transaction committing DDL (id=%lld) has aborted "
			 "another TX (id=%lld)", (long long) ddl_owner->id,
			 (long long) to_be_aborted->id);
	}
}

int
memtx_tx_cause_conflict(struct txn *breaker, struct txn *victim)
{
	struct tx_conflict_tracker *tracker = NULL;
	struct rlist *r1 = breaker->conflict_list.next;
	struct rlist *r2 = victim->conflicted_by_list.next;
	while (r1 != &breaker->conflict_list &&
	       r2 != &victim->conflicted_by_list) {
		tracker = rlist_entry(r1, struct tx_conflict_tracker,
				      in_conflict_list);
		assert(tracker->breaker == breaker);
		if (tracker->victim == victim)
			break;
		tracker = rlist_entry(r2, struct tx_conflict_tracker,
				      in_conflicted_by_list);
		assert(tracker->victim == victim);
		if (tracker->breaker == breaker)
			break;
		tracker = NULL;
		r1 = r1->next;
		r2 = r2->next;
	}
	if (tracker != NULL) {
		/*
		 * Move to the beginning of a list
		 * for a case of subsequent lookups.
		 */
		rlist_del(&tracker->in_conflict_list);
		rlist_del(&tracker->in_conflicted_by_list);
	} else {
		size_t size;
		tracker = region_alloc_object(&victim->region,
					      struct tx_conflict_tracker,
					      &size);
		if (tracker == NULL) {
			diag_set(OutOfMemory, size, "tx region",
				 "conflict_tracker");
			return -1;
		}
		tracker->breaker = breaker;
		tracker->victim = victim;
	}
	rlist_add(&breaker->conflict_list, &tracker->in_conflict_list);
	rlist_add(&victim->conflicted_by_list, &tracker->in_conflicted_by_list);
	return 0;
}

void
memtx_tx_handle_conflict(struct txn *breaker, struct txn *victim)
{
	assert(breaker->psn != 0);
	if (victim->status != TXN_INPROGRESS) {
		/* Was conflicted by somebody else. */
		return;
	}
	if (stailq_empty(&victim->stmts)) {
		/* Send to read view. */
		victim->status = TXN_IN_READ_VIEW;
		victim->rv_psn = breaker->psn;
		rlist_add_tail(&txm.read_view_txs, &victim->in_read_view_txs);
	} else {
		/* Mark as conflicted. */
		victim->status = TXN_CONFLICTED;
	}
}

/**
 * Create a new story and link it with the @a tuple.
 * @return story on success, NULL on error (diag is set).
 */
static struct memtx_story *
memtx_tx_story_new(struct space *space, struct tuple *tuple)
{
	txm.must_do_gc_steps += TX_MANAGER_GC_STEPS_SIZE;
	assert(!tuple->is_dirty);
	uint32_t index_count = space->index_count;
	assert(index_count < BOX_INDEX_MAX);
	struct mempool *pool = &txm.memtx_tx_story_pool[index_count];
	struct memtx_story *story = (struct memtx_story *) mempool_alloc(pool);
	if (story == NULL) {
		size_t item_size = sizeof(struct memtx_story) +
				   index_count *
				   sizeof(struct memtx_story_link);
		diag_set(OutOfMemory, item_size, "mempool_alloc", "story");
		return NULL;
	}
	story->tuple = tuple;

	const struct memtx_story **put_story =
		(const struct memtx_story **) &story;
	struct memtx_story **empty = NULL;
	mh_int_t pos = mh_history_put(txm.history, put_story, &empty, 0);
	if (pos == mh_end(txm.history)) {
		mempool_free(pool, story);
		diag_set(OutOfMemory, pos + 1, "mh_history_put",
			 "mh_history_node");
		return NULL;
	}
	tuple->is_dirty = true;
	tuple_ref(tuple);

	story->space = space;
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
		rlist_create(&story->link[i].nearby_gaps);
		story->link[i].in_index = space->index[i];
	}
	return story;
}

static void
memtx_tx_story_delete(struct memtx_story *story)
{
	if (story->add_stmt != NULL) {
		assert(story->add_stmt->add_story == story);
		story->add_stmt->add_story = NULL;
		story->add_stmt = NULL;
	}
	while (story->del_stmt != NULL) {
		assert(story->del_stmt->del_story == story);
		story->del_stmt->del_story = NULL;
		struct txn_stmt *next = story->del_stmt->next_in_del_list;
		story->del_stmt->next_in_del_list = NULL;
		story->del_stmt = next;
	}

	if (txm.traverse_all_stories == &story->in_all_stories)
		txm.traverse_all_stories = rlist_next(txm.traverse_all_stories);
	rlist_del(&story->in_all_stories);
	rlist_del(&story->in_space_stories);

	mh_int_t pos = mh_history_find(txm.history, story->tuple, 0);
	assert(pos != mh_end(txm.history));
	mh_history_del(txm.history, pos, 0);

	story->tuple->is_dirty = false;
	tuple_unref(story->tuple);

#ifndef NDEBUG
	/* Expecting to delete fully unlinked story. */
	for (uint32_t i = 0; i < story->index_count; i++) {
		assert(story->link[i].newer_story == NULL);
		assert(story->link[i].older_story == NULL);
	}
#endif

	struct mempool *pool = &txm.memtx_tx_story_pool[story->index_count];
	mempool_free(pool, story);
}

/**
 * Find a story of a @a tuple. The story expected to be present (assert).
 */
static struct memtx_story *
memtx_tx_story_get(struct tuple *tuple)
{
	assert(tuple->is_dirty);

	mh_int_t pos = mh_history_find(txm.history, tuple, 0);
	assert(pos != mh_end(txm.history));
	return *mh_history_node(txm.history, pos);
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
 * Unlink a @a story with @ old_story in @a index (in both directions).
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
 * Link a @a story with @a old_story in @a index (in both directions),
 * where old_story was at the top of chain. This is a light version of
 * function, intended for the case when the appropriate index IS ALREADY
 * changed, and now stores story->tuple.
 * In addition to linking in list, this function also rebinds gap records
 * to the new top - story - in history chain.
 * @a old_story is allowed to be NULL.
 */
static void
memtx_tx_story_link_top_light(struct memtx_story *new_top,
			      struct memtx_story *old_top,
			      uint32_t idx)
{
	memtx_tx_story_link(new_top, old_top, idx);

	if (old_top == NULL)
		return;

	/* Rebind gap records to the top of the list */
	struct memtx_story_link *new_link = &new_top->link[idx];
	struct memtx_story_link *old_link = &old_top->link[idx];
	rlist_splice(&new_link->nearby_gaps, &old_link->nearby_gaps);
}

/**
 * Link a @a new_top with @a old_top in @a index (in both directions),
 * where old_top was at the top of chain (that means that index itself
 * stores a pointer to old_top->tuple).
 * In addition to linking in list, this function also rebinds gap records
 * to the new top - story - in history chain.
 * This function makes also changes in the index, replacing old_top->tuple
 * with new_top->tuple.
 */
static void
memtx_tx_story_link_top(struct memtx_story *new_top,
			struct memtx_story *old_top,
			uint32_t idx)
{
	assert(old_top != NULL);
	memtx_tx_story_link_top_light(new_top, old_top, idx);

	/* Make the change in index. */
	struct index *index = new_top->space->index[idx];
	assert(old_top->link[idx].in_index == index);
	assert(new_top->link[idx].in_index == NULL);
	struct tuple *removed, *unused;
	if (index_replace(index, old_top->tuple, new_top->tuple,
			  DUP_REPLACE, &removed, &unused) != 0) {
		diag_log();
		unreachable();
		panic("failed to rebind story in index");
	}
	assert(old_top->tuple == removed);
	old_top->link[idx].in_index = NULL;
	new_top->link[idx].in_index = index;

	/*
	 * A space holds references to all his tuples.
	 * All tuples that are physically in the primary index are referenced.
	 * Thus we have to reference the tuple that was added to the primar
	 * index and dereference the tuple that was removed from it.
	 */
	if (idx == 0) {
		tuple_ref(new_top->tuple);
		tuple_unref(old_top->tuple);
	}
}

/**
 * Unlink a @a story from history chain in @a index (in both directions),
 * where old_top was at the top of chain (that means that index itself
 * stores a pointer to story->tuple).
 * This is a light version of function, intended for the case when the
 * appropriate change in will be done later by caller.
 * In addition to unlinking from in list, this function also rebinds gap
 * records to the new top in history chain.
 */
static void
memtx_tx_story_unlink_top_light(struct memtx_story *story, uint32_t idx)
{
	assert(story != NULL);
	assert(idx < story->index_count);
	struct memtx_story_link *link = &story->link[idx];

	assert(link->newer_story == NULL);

	struct memtx_story *old_story = link->older_story;
	if (old_story != NULL) {
		memtx_tx_story_unlink(story, old_story, idx);

		/* Rebind gap records to the new top of the list */
		struct memtx_story_link *old_link = &old_story->link[idx];
		rlist_splice(&old_link->nearby_gaps, &link->nearby_gaps);
	}
}

/**
 * Unlink a @a story from history chain in @a index (in both directions),
 * where old_top was at the top of chain (that means that index itself
 * stores a pointer to story->tuple).
 * In addition to unlinking from in list, this function also rebinds gap
 * records to the new top in history chain.
 * This function makes also changes in the index, replacing old_top->tuple
 * with the correct tuple (the next in chain, maybe NULL).
 */
static void
memtx_tx_story_unlink_top(struct memtx_story *story, uint32_t idx)
{
	assert(story != NULL);
	assert(idx < story->index_count);
	struct memtx_story_link *link = &story->link[idx];

	assert(link->newer_story == NULL);
	/*
	 * Note that link[idx].in_index may not be the same as
	 * story->space->index[idx] in case space is going to be deleted
	 * in memtx_tx_on_space_delete(): during space alter operation we
	 * swap all indexes to the new space object and instead use dummy
	 * structs.
	 */
	struct index *index = story->link[idx].in_index;

	struct memtx_story *old_story = link->older_story;
	assert(old_story == NULL || old_story->link[idx].in_index == NULL);
	struct tuple *old_tuple = old_story == NULL ? NULL : old_story->tuple;
	struct tuple *removed, *unused;
	if (index_replace(index, story->tuple, old_tuple,
			  DUP_INSERT, &removed, &unused) != 0) {
		diag_log();
		unreachable();
		panic("failed to rebind story in index");
	}
	assert(story->tuple == removed);
	story->link[idx].in_index = NULL;
	if (old_story != NULL)
		old_story->link[idx].in_index = index;

	/*
	 * A space holds references to all his tuples.
	 * All tuples that are physically in the primary index are referenced.
	 * Thus we have to reference the tuple that was added to the primary
	 * index and dereference the tuple that was removed from it.
	 */
	if (idx == 0) {
		if (old_story != NULL)
			tuple_ref(old_story->tuple);
		tuple_unref(story->tuple);
	}

	memtx_tx_story_unlink_top_light(story, idx);
}

static int
memtx_tx_track_read_story_slow(struct txn *txn, struct memtx_story *story,
			       uint64_t index_mask);

/**
 * Unlink a @a story from history chain in @a index in both directions.
 * If the story was in the top of history chain - unlink from top.
 * Simply remove from list otherwise.
 */
static void
memtx_tx_story_unlink_both(struct memtx_story *story, uint32_t idx)
{
	assert(story != NULL);
	assert(idx < story->index_count);
	struct memtx_story_link *link = &story->link[idx];

	if (link->newer_story == NULL) {
		memtx_tx_story_unlink_top(story, idx);
	} else {
		struct memtx_story *newer_story = link->newer_story;
		struct memtx_story *older_story = link->older_story;
		memtx_tx_story_unlink(newer_story, story, idx);
		memtx_tx_story_unlink(story, older_story, idx);
		memtx_tx_story_link(newer_story, older_story, idx);

		/*
		 * Rebind read trackers in order to conflict
		 * readers in case of rollback of this txn.
		 */
		struct tx_read_tracker *tracker, *tmp;
		uint64_t index_mask = 1ull << (idx & 63);
		rlist_foreach_entry_safe(tracker, &story->reader_list,
					 in_reader_list, tmp) {
			if ((tracker->index_mask & index_mask) != 0) {
				memtx_tx_track_read_story_slow(tracker->reader,
							       newer_story,
							       index_mask);
			}
		}
	}
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
	memtx_tx_story_unlink(story, old_story, idx);
	memtx_tx_story_unlink(old_story, older_story, idx);
	if (newer_story != NULL) {
		/* Simple relink in list. */
		memtx_tx_story_unlink(newer_story, story, idx);
		memtx_tx_story_link(newer_story, old_story, idx);
		memtx_tx_story_link(old_story, story, idx);
	} else {
		/*
		 * story was in the top of history chain. In terms of reorder,
		 * we have to make old_story the new top of chain. */
		memtx_tx_story_link_top(old_story, story, idx);
	}
	memtx_tx_story_link(story, older_story, idx);
}

/**
 * Unlink @a story from all chains and remove corresponding tuple from
 * indexes if necessary.
 */
static void
memtx_tx_story_full_unlink(struct memtx_story *story)
{
	for (uint32_t i = 0; i < story->index_count; i++) {
		struct memtx_story_link *link = &story->link[i];
		if (link->newer_story == NULL) {
			/*
			 * We are at the top of the chain. That means
			 * that story->tuple is in index. If the story
			 * actually deletes the tuple, it must be deleted from
			 * index.
			 */
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
				assert(story->tuple == removed);
				link->in_index = NULL;
				/*
				 * All tuples in pk are referenced.
				 * Once removed it must be unreferenced.
				 */
				if (i == 0)
					tuple_unref(story->tuple);
			}

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
 * Run one step of a crawler that traverses all stories and removes no more
 * used stories.
 */
static void
memtx_tx_story_gc_step()
{
	if (txm.traverse_all_stories == &txm.all_stories) {
		/* We came to the head of the list. */
		txm.traverse_all_stories = txm.traverse_all_stories->next;
		return;
	}

	/* Lowest read view PSN */
	int64_t lowest_rv_psm = txn_last_psn;
	if (!rlist_empty(&txm.read_view_txs)) {
		struct txn *txn =
			rlist_first_entry(&txm.read_view_txs, struct txn,
					  in_read_view_txs);
		assert(txn->rv_psn != 0);
		lowest_rv_psm = txn->rv_psn;
	}

	struct memtx_story *story =
		rlist_entry(txm.traverse_all_stories, struct memtx_story,
			    in_all_stories);
	txm.traverse_all_stories = txm.traverse_all_stories->next;

	if (story->add_stmt != NULL || story->del_stmt != NULL ||
	    !rlist_empty(&story->reader_list)) {
		/* The story is used directly by some transactions. */
		return;
	}
	if (story->add_psn >= lowest_rv_psm ||
	    story->del_psn >= lowest_rv_psm) {
		/* The story can be used by a read view. */
		return;
	}
	for (uint32_t i = 0; i < story->index_count; i++) {
		if (!rlist_empty(&story->link[i].nearby_gaps)) {
			/* The story is used for gap tracking. */
			return;
		}
	}

	/* Unlink and delete the story */
	memtx_tx_story_full_unlink(story);
	memtx_tx_story_delete(story);
}

/**
 * Run several rounds of memtx_tx_story_gc_step()
 */
static void
memtx_tx_story_gc()
{
	for (size_t i = 0; i < txm.must_do_gc_steps; i++)
		memtx_tx_story_gc_step();
	txm.must_do_gc_steps = 0;
}

/**
 * Check if a @a story is visible for transaction @a txn. Return visible tuple
 * to @a visible_tuple (can be set to NULL).
 * @param is_prepared_ok - whether prepared (not committed) change is acceptable.
 * @param own_change - return true if the change was made by @txn itself.
 * @return true if the story is visible, false otherwise.
 */
static bool
memtx_tx_story_is_visible(struct memtx_story *story, struct txn *txn,
			  struct tuple **visible_tuple, bool is_prepared_ok,
			  bool *own_change)
{
	*own_change = false;
	*visible_tuple = NULL;

	int64_t rv_psn = INT64_MAX;
	if (txn != NULL && txn->rv_psn != 0)
		rv_psn = txn->rv_psn;

	struct txn_stmt *dels = story->del_stmt;
	bool was_deleted_by_prepared = false;
	while (dels != NULL) {
		if (dels->txn == txn) {
			/* Tuple is deleted by us (@txn). */
			*own_change = true;
			return true;
		}
		if (story->del_psn != 0 && dels->txn->psn == story->del_psn)
			was_deleted_by_prepared = true;
		dels = dels->next_in_del_list;
	}
	if (is_prepared_ok && story->del_psn != 0 && story->del_psn < rv_psn) {
		/* Tuple is deleted by prepared TX. */
		return true;
	}
	if (story->del_psn != 0 && !was_deleted_by_prepared &&
	    story->del_psn < rv_psn) {
		/* Tuple is deleted by committed TX. */
		return true;
	}

	if (story->add_stmt != NULL && story->add_stmt->txn == txn) {
		/* Tuple is added by us (@txn). */
		*visible_tuple = story->tuple;
		*own_change = true;
		return true;
	}
	if (is_prepared_ok && story->add_psn != 0 && story->add_psn < rv_psn) {
		/* Tuple is added by another prepared TX. */
		*visible_tuple = story->tuple;
		return true;
	}
	if (story->add_psn != 0 && story->add_stmt == NULL &&
	    story->add_psn < rv_psn) {
		/* Tuple is added by committed TX. */
		*visible_tuple = story->tuple;
		return true;
	}
	if (story->add_psn == 0 && story->add_stmt == NULL) {
		/* added long time ago. */
		*visible_tuple = story->tuple;
		return true;
	}
	return false;
}

/**
 * Temporary (allocated on region) struct that stores a conflicting TX.
 */
struct memtx_tx_conflict
{
	/* The transaction that will conflict us upon commit. */
	struct txn *breaker;
	/* The transaction that will conflicted by upon commit. */
	struct txn *victim;
	/* Link in single-linked list. */
	struct memtx_tx_conflict *next;
};

/**
 * Save @a breaker in list with head @a conflicts_head. New list node is
 * allocated on @a region.
 * @return 0 on success, -1 on memory error.
 */
static int
memtx_tx_save_conflict(struct txn *breaker, struct txn *victim,
		       struct memtx_tx_conflict **conflicts_head,
		       struct region *region)
{
	size_t err_size;
	struct memtx_tx_conflict *next_conflict;
	next_conflict = region_alloc_object(region, struct memtx_tx_conflict,
					    &err_size);
	if (next_conflict == NULL) {
		diag_set(OutOfMemory, err_size, "txn_region", "txn conflict");
		return -1;
	}
	next_conflict->breaker = breaker;
	next_conflict->victim = victim;
	next_conflict->next = *conflicts_head;
	*conflicts_head = next_conflict;
	return 0;
}

/**
 * Scan a history starting with @a stmt statement in @a index for a visible
 * tuple (prepared suits), returned via @a visible_replaced.
 * Collect a list of transactions that will abort current transaction if they
 * are committed.
 *
 * @return 0 on success, -1 on memory error.
 */
static int
memtx_tx_story_find_visible_tuple(struct memtx_story *story,
				  struct txn_stmt *stmt,
				  uint32_t index,
				  struct tuple **visible_replaced)
{
	for (; story != NULL; story = story->link[index].older_story) {
		assert(index < story->index_count);
		bool unused;
		if (memtx_tx_story_is_visible(story, stmt->txn,
					      visible_replaced, true, &unused))
			return 0;
	}
	*visible_replaced = NULL;
	return 0;
}

/**
 * replace_check_dup wrapper, that follows usual return code convention and
 * sets diag in case of error.
 */
static int
memtx_tx_check_dup(struct tuple *new_tuple, struct tuple *old_tuple,
		   struct tuple *dup_tuple, enum dup_replace_mode mode,
		   struct index *index, struct space *space)
{
	uint32_t errcode;
	errcode = replace_check_dup(old_tuple, dup_tuple, mode);
	if (errcode != 0) {
		if (errcode == ER_TUPLE_FOUND) {
			diag_set(ClientError, errcode,
				 index->def->name,
				 space_name(space),
				 tuple_str(dup_tuple),
				 tuple_str(new_tuple));
		} else {
			diag_set(ClientError, errcode,
				 index->def->name,
				 space_name(space));
		}
		 return -1;
	}
	return 0;
}

static struct point_hole_item *
point_hole_storage_find(struct index *index, struct tuple *tuple)
{
	struct point_hole_key key;
	key.index = index;
	key.tuple = tuple;
	mh_int_t pos = mh_point_holes_find(txm.point_holes, &key, 0);
	if (pos == mh_end(txm.point_holes))
		return NULL;
	return *mh_point_holes_node(txm.point_holes, pos);
}

/**
 * Check for possible conflicts during inserting @a new tuple, and given
 * that it was real insertion, not the replacement of existion tuple.
 * It's the moment where we can search for stored point hole trackers
 * and detect conflicts.
 * Since the insertions in not completed succesfully, we better store
 * conflicts to the special temporary storage @a collected_conflicts in
 * other to become real conflint only when insertion success is inevitable.
 */
static int
check_hole(struct space *space, uint32_t index,
	   struct tuple *new_tuple, struct txn *inserter,
	   struct memtx_tx_conflict **collected_conflicts,
	   struct region *region)
{
	struct point_hole_item *list =
		point_hole_storage_find(space->index[index], new_tuple);
	if (list == NULL)
		return 0;

	struct point_hole_item *item = list;
	do {
		if (memtx_tx_save_conflict(inserter, item->txn,
					   collected_conflicts, region) != 0)
			return -1;
		item = rlist_entry(item->ring.next,
				   struct point_hole_item, ring);

	} while (item != list);
	return 0;
}

/**
 * Check that replaced tuples in space's indexes does not violate common
 * replace rules. See memtx_space_replace_all_keys comment.
 * (!) Version for the case when replaced tuple in the primary index is
 * either NULL or clean.
 * @return 0 on success or -1 on fail.
 */
static int
check_dup_clean(struct txn_stmt *stmt, struct tuple *new_tuple,
		struct tuple **replaced, struct tuple **old_tuple,
		enum dup_replace_mode mode,
		struct memtx_tx_conflict **collected_conflicts,
		struct region *region)
{
	assert(replaced[0] == NULL || !replaced[0]->is_dirty);
	struct space *space = stmt->space;
	struct txn *txn = stmt->txn;

	if (memtx_tx_check_dup(new_tuple, *old_tuple, replaced[0],
			       mode, space->index[0], space) != 0) {
		if (replaced[0] != NULL)
			memtx_tx_track_read(txn, space, replaced[0]);
		return -1;
	}

	if (replaced[0] == NULL)
		check_hole(space, 0, new_tuple, stmt->txn,
			   collected_conflicts, region);

	for (uint32_t i = 1; i < space->index_count; i++) {
		/*
		 * Check that visible tuple is NULL or the same as in the
		 * primary index, namely replaced[0].
		 */
		if (replaced[i] == NULL) {
			/* NULL is OK. */
			check_hole(space, i, new_tuple, stmt->txn,
				   collected_conflicts, region);
			continue;
		}
		if (!replaced[i]->is_dirty) {
			/* Check like there's no mvcc. */
			if (memtx_tx_check_dup(new_tuple, replaced[0],
					       replaced[i], DUP_INSERT,
					       space->index[i], space) != 0) {
				memtx_tx_track_read(txn, space, replaced[i]);
				return -1;
			}
			continue;
		}

		/*
		 * The replaced tuple is dirty. A chain of changes cannot lead
		 * to clean tuple, but in can lead to NULL, that's the only
		 * chance to be OK.
		 */
		struct memtx_story *second_story = memtx_tx_story_get(replaced[i]);
		struct tuple *check_visible;

		if (memtx_tx_story_find_visible_tuple(second_story, stmt, i,
						      &check_visible) != 0)
			return -1;

		if (memtx_tx_check_dup(new_tuple, replaced[0], check_visible,
				       DUP_INSERT, space->index[i], space) != 0) {
			memtx_tx_track_read(txn, space, check_visible);
			return -1;
		}

		if (check_visible == NULL)
			check_hole(space, i, new_tuple, stmt->txn,
				   collected_conflicts, region);
	}

	*old_tuple = replaced[0];
	return 0;
}

/**
 * Check that replaced tuples in space's indexes does not violate common
 * replace rules. See memtx_space_replace_all_keys comment.
 * (!) Version for the case when replaced tuple is dirty.
 * @return 0 on success or -1 on fail.
 */
static int
check_dup_dirty(struct txn_stmt *stmt, struct tuple *new_tuple,
		struct tuple **replaced, struct tuple **old_tuple,
		enum dup_replace_mode mode,
		struct memtx_tx_conflict **collected_conflicts,
		struct region *region)
{
	assert(replaced[0] != NULL && replaced[0]->is_dirty);
	struct space *space = stmt->space;
	struct txn *txn = stmt->txn;

	struct memtx_story *old_story = memtx_tx_story_get(replaced[0]);
	struct tuple *visible_replaced;
	if (memtx_tx_story_find_visible_tuple(old_story, stmt, 0,
					      &visible_replaced) != 0)
		return -1;

	if (memtx_tx_check_dup(new_tuple, *old_tuple, visible_replaced,
			       mode, space->index[0], space) != 0) {
		memtx_tx_track_read(txn, space, visible_replaced);
		return -1;
	}

	if (visible_replaced == NULL)
		check_hole(space, 0, new_tuple, stmt->txn,
			   collected_conflicts, region);

	for (uint32_t i = 1; i < space->index_count; i++) {
		/*
		 * Check that visible tuple is NULL or the same as in the
		 * primary index, namely visible_replaced.
		 */
		if (replaced[i] == NULL) {
			/* NULL is OK. */
			check_hole(space, i, new_tuple, stmt->txn,
				   collected_conflicts, region);
			continue;
		}
		if (!replaced[i]->is_dirty) {
			/*
			 * Non-null clean tuple cannot be NULL or
			 * visible_replaced since visible_replaced is dirty.
			 */
			diag_set(ClientError, ER_TUPLE_FOUND,
				 space->index[i]->def->name,
				 space_name(space),
				 tuple_str(replaced[i]),
				 tuple_str(new_tuple));
			return -1;
		}

		struct memtx_story *second_story = memtx_tx_story_get(replaced[i]);
		struct tuple *check_visible;

		if (memtx_tx_story_find_visible_tuple(second_story, stmt, i,
						      &check_visible) != 0)
			return -1;

		if (memtx_tx_check_dup(new_tuple, visible_replaced,
				       check_visible, DUP_INSERT,
				       space->index[i], space) != 0) {
			memtx_tx_track_read(txn, space, visible_replaced);
			return -1;
		}

		if (check_visible == NULL)
			check_hole(space, i, new_tuple, stmt->txn,
				   collected_conflicts, region);
	}

	*old_tuple = visible_replaced;
	return 0;
}

/**
 * Check that replaced tuples in space's indexes does not violate common
 * replace rules. See memtx_space_replace_all_keys comment.
 * Call check_dup_clean or check_dup_dirty depending on situation.
 * @return 0 on success or -1 on fail.
 */
static int
check_dup_common(struct txn_stmt *stmt, struct tuple *new_tuple,
		 struct tuple **directly_replaced, struct tuple **old_tuple,
		 enum dup_replace_mode mode,
		 struct memtx_tx_conflict **collected_conflicts,
		 struct region *region)
{
	struct tuple *replaced = directly_replaced[0];
	if (replaced == NULL || !replaced->is_dirty)
		return check_dup_clean(stmt, new_tuple, directly_replaced,
				       old_tuple, mode,
				       collected_conflicts, region);
	else
		return check_dup_dirty(stmt, new_tuple, directly_replaced,
				       old_tuple, mode,
				       collected_conflicts, region);
}

static struct gap_item *
memtx_tx_gap_item_new(struct txn *txn, enum iterator_type type,
		      const char *key, uint32_t part_count);

static int
memtx_tx_track_read_story(struct txn *txn, struct space *space,
			  struct memtx_story *story, uint64_t index_mask);

/**
 * Handle insertion to a new place in index. There can be readers which
 * have read from this gap and thus must be sent to read view or conflicted.
 */
static int
memtx_tx_handle_gap_write(struct txn *txn, struct space *space,
			  struct memtx_story *story, struct tuple *tuple,
			  struct tuple *successor, uint32_t ind)
{
	struct index *index = space->index[ind];
	struct full_scan_item *fsc_item, *fsc_tmp;
	struct rlist *fsc_list = &index->full_scans;
	rlist_foreach_entry_safe(fsc_item, fsc_list, in_full_scans, fsc_tmp) {
		if (memtx_tx_cause_conflict(txn, fsc_item->txn) != 0)
			return -1;
	}
	if (successor != NULL && !successor->is_dirty)
		return 0; /* no gap records */

	struct rlist *list = &index->nearby_gaps;
	if (successor != NULL) {
		assert(successor->is_dirty);
		struct memtx_story *succ_story = memtx_tx_story_get(successor);
		assert(ind < succ_story->index_count);
		list = &succ_story->link[ind].nearby_gaps;
		assert(list->next != NULL && list->prev != NULL);
	}
	uint64_t index_mask = 1ull << (ind & 63);
	struct gap_item *item, *tmp;
	rlist_foreach_entry_safe(item, list, in_nearby_gaps, tmp) {
		bool is_split = false;
		if (item->key == NULL) {
			if (memtx_tx_track_read_story(item->txn, space, story,
						      index_mask) != 0)
				return -1;
			is_split = true;
		} else {
			struct key_def *def = index->def->key_def;
			hint_t oh = def->key_hint(item->key, item->part_count, def);
			hint_t kh = def->tuple_hint(tuple, def);
			int cmp = def->tuple_compare_with_key(tuple, kh,
							      item->key,
							      item->part_count,
							      oh, def);
			int dir = iterator_direction(item->type);
			if (cmp == 0 && (item->type == ITER_EQ ||
					 item->type == ITER_REQ ||
					 item->type == ITER_GE ||
					 item->type == ITER_LE)) {
				if (memtx_tx_track_read_story(item->txn, space,
							      story,
							      index_mask) != 0)
					return -1;
			}
			if (cmp * dir > 0 &&
			    item->type != ITER_EQ && item->type != ITER_REQ) {
				if (memtx_tx_track_read_story(item->txn, space,
							      story,
							      index_mask) != 0)
					return -1;
				is_split = true;
			}
			if (cmp > 0 && dir < 0) {
				/* The tracker must be moved to the left gap. */
				rlist_del(&item->in_nearby_gaps);
				rlist_add(&story->link[ind].nearby_gaps,
					  &item->in_nearby_gaps);
			}
		}

		if (is_split) {
			/*
			 * The insertion divided the gap into two parts.
			 * Old tracker is left in one gap, let's copy tracker
			 * to another.
			 */
			struct gap_item *copy =
				memtx_tx_gap_item_new(item->txn, item->type,
						      item->key,
						      item->part_count);
			if (copy == NULL)
				return -1;

			rlist_add(&story->link[ind].nearby_gaps,
				  &copy->in_nearby_gaps);
		}
	}
	return 0;
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
	struct memtx_story *add_story = NULL;
	struct memtx_story *created_story = NULL, *replaced_story = NULL;
	struct region *region = &stmt->txn->region;

	/*
	 * List of transactions that will conflict us once one of them
	 * become committed.
	 */
	struct memtx_tx_conflict *collected_conflicts = NULL;

	struct tuple *directly_replaced[space->index_count];
	struct tuple *direct_successor[space->index_count];
	uint32_t directly_replaced_count = 0;

	for (uint32_t i = 0; i < space->index_count; i++) {
		struct index *index = space->index[i];
		struct tuple **replaced = &directly_replaced[i];
		struct tuple **successor = &direct_successor[i];
		if (index_replace(index, NULL, new_tuple,
				  DUP_REPLACE_OR_INSERT,
				  replaced, successor) != 0)
		{
			directly_replaced_count = i;
			goto fail;
		}
	}
	directly_replaced_count = space->index_count;
	struct tuple *replaced = directly_replaced[0];

	/* Check overwritten tuple */
	int rc = check_dup_common(stmt, new_tuple, directly_replaced,
				  &old_tuple, mode,
				  &collected_conflicts, region);
	if (rc != 0)
		goto fail;

	/* Create add_story and replaced_story if necessary. */
	add_story = memtx_tx_story_new(space, new_tuple);
	if (add_story == NULL)
		goto fail;
	memtx_tx_story_link_added_by(add_story, stmt);

	if (replaced != NULL && !replaced->is_dirty) {
		created_story = memtx_tx_story_new(space, replaced);
		if (created_story == NULL)
			goto fail;
		replaced_story = created_story;
		memtx_tx_story_link_top_light(add_story, replaced_story, 0);
	} else if (replaced != NULL) {
		replaced_story = memtx_tx_story_get(replaced);
		memtx_tx_story_link_top_light(add_story, replaced_story, 0);
	} else {
		rc = memtx_tx_handle_gap_write(stmt->txn, space,
					       add_story, new_tuple,
					       direct_successor[0], 0);
		if (rc != 0)
			goto fail;
	}

	/* Purge found conflicts. */
	while (collected_conflicts != NULL) {
		if (memtx_tx_cause_conflict(collected_conflicts->breaker,
					    collected_conflicts->victim) != 0)
			goto fail;
		collected_conflicts = collected_conflicts->next;
	}

	if (replaced_story != NULL)
		replaced_story->link[0].in_index = NULL;
	for (uint32_t i = 1; i < space->index_count; i++) {
		if (directly_replaced[i] == NULL) {
			rc = memtx_tx_handle_gap_write(stmt->txn, space,
						       add_story, new_tuple,
						       direct_successor[i], i);
			if (rc != 0)
				goto fail;
			continue;
		}
		assert(directly_replaced[i]->is_dirty);
		struct memtx_story *secondary_replaced =
			memtx_tx_story_get(directly_replaced[i]);
		memtx_tx_story_link_top_light(add_story, secondary_replaced, i);
		secondary_replaced->link[i].in_index = NULL;
	}

	if (old_tuple != NULL) {
		assert(old_tuple->is_dirty);

		struct memtx_story *del_story = NULL;
		if (old_tuple == replaced)
			del_story = replaced_story;
		else
			del_story = memtx_tx_story_get(old_tuple);
		memtx_tx_story_link_deleted_by(del_story, stmt);
	}

	if (new_tuple != NULL) {
		/*
		 * A space holds references to all his tuples.
		 * It's made via primary index - all tuples that are physically
		 * in primary index must be referenced (a replaces tuple must
		 * be dereferenced).
		 */
		tuple_ref(new_tuple);
		if (directly_replaced[0] != NULL)
			tuple_unref(directly_replaced[0]);
	}

	*result = old_tuple;
	if (*result != NULL) {
		/*
		 * The result must be a referenced pointer. The caller must
		 * unreference it by itself.
		 * Actually now it goes only to stmt->old_tuple, and
		 * stmt->old_tuple is unreferenced when stmt is destroyed.
		 */
		tuple_ref(*result);
	}

	memtx_tx_story_gc();
	return 0;

fail:
	if (add_story != NULL) {
		for (uint32_t i = 0; i < add_story->index_count; i++) {
			struct memtx_story_link *link = &add_story->link[i];
			assert(link->newer_story == NULL); (void)link;
			memtx_tx_story_unlink_top_light(add_story, i);
		}
		memtx_tx_story_delete(add_story);
	}
	if (created_story)
		memtx_tx_story_delete(created_story);

	while (directly_replaced_count > 0) {
		uint32_t i = --directly_replaced_count;
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
	struct space *space = stmt->space;
	struct memtx_story *del_story;

	if (old_tuple->is_dirty) {
		del_story = memtx_tx_story_get(old_tuple);
	} else {
		del_story = memtx_tx_story_new(space, old_tuple);
		if (del_story == NULL)
			return -1;
	}

	memtx_tx_story_link_deleted_by(del_story, stmt);
	*result = old_tuple;

	if (*result != NULL) {
		/*
		 * The result must be a referenced pointer. The caller must
		 * unreference it by itself.
		 * Actually now it goes only to stmt->old_tuple, and
		 * stmt->old_tuple is unreferenced when stmt is destroyed.
		 */
		tuple_ref(*result);
	}
	memtx_tx_story_gc();
	return 0;
}

int
memtx_tx_history_add_stmt(struct txn_stmt *stmt, struct tuple *old_tuple,
			  struct tuple *new_tuple, enum dup_replace_mode mode,
			  struct tuple **result)
{
	assert(stmt != NULL);
	assert(stmt->space != NULL);
	assert(new_tuple != NULL || old_tuple != NULL);
	assert(new_tuple == stmt->new_tuple);
	assert(new_tuple == NULL || !new_tuple->is_dirty);
	assert(result == &stmt->old_tuple);

	if (new_tuple != NULL)
		return memtx_tx_history_add_insert_stmt(stmt, old_tuple,
							new_tuple, mode,
							result);
	else
		return memtx_tx_history_add_delete_stmt(stmt, old_tuple,
							result);
}

void
memtx_tx_history_rollback_stmt(struct txn_stmt *stmt)
{
	if (stmt->add_story != NULL) {
		assert(stmt->add_story->tuple == stmt->new_tuple);
		struct memtx_story *story = stmt->add_story;

		/*
		 * Relink those who delete this story and make them
		 * delete older story.
		 */
		struct memtx_story_link *link = &story->link[0];
		struct memtx_story *old_story = link->older_story;
		while (story->del_stmt) {
			struct txn_stmt *del_stmt = story->del_stmt;

			/* Unlink from old list in any case. */
			story->del_stmt = del_stmt->next_in_del_list;
			del_stmt->next_in_del_list = NULL;
			del_stmt->del_story = NULL;

			/* Link to old story's list. */
			if (old_story != NULL)
				memtx_tx_story_link_deleted_by(old_story,
							       del_stmt);
		}

		for (uint32_t i = 0; i < story->index_count; i++)
			memtx_tx_story_unlink_both(story, i);

		/* The story is no more allowed to change indexes. */
		stmt->add_story->space = NULL;

		memtx_tx_story_unlink_added_by(story, stmt);
	}

	if (stmt->del_story != NULL)
		memtx_tx_story_unlink_deleted_by(stmt->del_story, stmt);
}

/**
 * Helper of memtx_tx_history_prepare_stmt. Do the job in case when
 * stmt->add_story != NULL, that is REPLACE, INSERT, UPDATE etc.
 */
static void
memtx_tx_history_prepare_insert_stmt(struct txn_stmt *stmt)
{
	assert(stmt->add_story != NULL);
	/**
	 * History of a key in an index can consist of several stories.
	 * The list of stories is started with a dirty tuple that is in index.
	 * The list begins with several (or zero) of stories that are added by
	 * in-progress transactions, then the list continues with several
	 * (or zero) of prepared stories, which are followed by several
	 * (or zero) of committed stories.
	 * If a statement becomes prepared, its story must be moved to the
	 * point in list exactly between all still in-progress and all already
	 * prepared.
	 */
	struct memtx_story *story = stmt->add_story;
	uint32_t index_count = story->index_count;
	/*
	 * That's a common loop for both index iteration and sequential push
	 * of the story down in lists of stories.
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

	struct memtx_story *old_story = story->link[0].older_story;
	if (stmt->del_story == NULL)
		assert(old_story == NULL || old_story->del_psn != 0);
	else
		assert(stmt->del_story == story->link[0].older_story);

	if (stmt->del_story == NULL) {
		/*
		 * This statement replaced nothing. That means that before
		 * this preparation there was no visible tuple in index, and
		 * now there is.
		 * There also can be some in-progress transactions that think
		 * that the replaced nothing. Some of them must be aborted
		 * (for example inserts, that must replace nothing), other
		 * must be told that they replace this tuple now.
		 */
		struct memtx_story_link *link = &story->link[0];
		while (link->newer_story != NULL) {
			struct memtx_story *test = link->newer_story;
			link = &test->link[0];
			struct txn_stmt *test_stmt = test->add_stmt;
			if (test_stmt->txn == stmt->txn)
				continue;
			if (test_stmt->del_story != stmt->del_story) {
				assert(test_stmt->del_story->add_stmt->txn
				       == test_stmt->txn);
				continue;
			}
			if (test_stmt->does_require_old_tuple)
				memtx_tx_handle_conflict(stmt->txn,
							 test_stmt->txn);

			memtx_tx_story_link_deleted_by(story, test_stmt);
		}
	}

	if (old_story != NULL) {
		/*
		 * There can be some transactions that want to delete old_story.
		 * It can be this transaction, or some other prepared TX.
		 * All other transactions must be aborted or relinked to delete
		 * this tuple.
		 */
		struct txn_stmt **from = &old_story->del_stmt;
		struct txn_stmt **to = &story->del_stmt;
		while (*to != NULL)
			to = &((*to)->next_in_del_list);
		while (*from != NULL) {
			struct txn_stmt *test_stmt = *from;
			assert(test_stmt->del_story == old_story);
			if (test_stmt == stmt || test_stmt->txn->psn != 0) {
				/* This or prepared. Go to the next stmt. */
				from = &test_stmt->next_in_del_list;
				continue;
			}
			/* Unlink from old list in any case. */
			*from = test_stmt->next_in_del_list;
			test_stmt->next_in_del_list = NULL;
			test_stmt->del_story = NULL;

			if (test_stmt->does_require_old_tuple)
				memtx_tx_handle_conflict(stmt->txn,
							 test_stmt->txn);

			/* Link to story's list. */
			test_stmt->del_story = story;
			*to = test_stmt;
			to = &test_stmt->next_in_del_list;
		}
	}

	for (uint32_t i = 1; i < story->index_count; i++) {
		struct memtx_story_link *link = &story->link[i];
		while (link->newer_story != NULL) {
			struct memtx_story *test = link->newer_story;
			link = &test->link[i];
			struct txn_stmt *test_stmt = test->add_stmt;
			if (test_stmt->txn == stmt->txn)
				continue;
			if (test_stmt->del_story == story)
				continue;
			if (test_stmt->del_story != NULL &&
			    test_stmt->del_story->add_stmt != NULL &&
			    test_stmt->del_story->add_stmt->txn ==
			    test_stmt->txn)
				continue;
			memtx_tx_handle_conflict(stmt->txn, test_stmt->txn);
			memtx_tx_story_link_deleted_by(story, test_stmt);
		}
	}

	for (uint32_t i = 0; i < index_count; i++) {
		old_story = story->link[i].older_story;
		if (old_story == NULL)
			continue;

		struct tx_read_tracker *tracker;
		rlist_foreach_entry(tracker, &old_story->reader_list,
				    in_reader_list) {
			if (tracker->reader == stmt->txn)
				continue;
			uint64_t index_mask = 1ull << (i & 63);
			if ((tracker->index_mask & index_mask) == 0)
				continue;
			memtx_tx_handle_conflict(stmt->txn, tracker->reader);
		}
	}

	for (uint32_t i = 0; i < index_count; i++) {
		for (struct memtx_story *read_story = story;
		     read_story != NULL;
		     read_story = read_story->link[i].newer_story) {
			struct tx_read_tracker *tracker;
			rlist_foreach_entry(tracker, &read_story->reader_list,
					    in_reader_list) {
				if (tracker->reader == stmt->txn)
					continue;
				uint64_t index_mask = 1ull << (i & 63);
				if ((tracker->index_mask & index_mask) == 0)
					continue;
				memtx_tx_handle_conflict(stmt->txn,
							 tracker->reader);
			}
		}
	}
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

	struct memtx_story *story = stmt->del_story;
	/*
	 * There can be some transactions that want to delete old_story.
	 * All other transactions must be aborted.
	 */
	struct txn_stmt **itr = &story->del_stmt;
	while (*itr != NULL) {
		struct txn_stmt *test_stmt = *itr;
		assert(test_stmt->del_story == story);
		if (test_stmt == stmt && test_stmt->txn->psn != 0) {
			/* This statement. Go to the next stmt. */
			itr = &test_stmt->next_in_del_list;
			continue;
		}
		/* Unlink from old list in any case. */
		*itr = test_stmt->next_in_del_list;
		test_stmt->next_in_del_list = NULL;
		test_stmt->del_story = NULL;
		/* Conflict only in case of dependance. */
		if (test_stmt->does_require_old_tuple)
			memtx_tx_handle_conflict(stmt->txn, test_stmt->txn);
	}

	struct tx_read_tracker *tracker;
	rlist_foreach_entry(tracker, &story->reader_list, in_reader_list) {
		if (tracker->reader == stmt->txn)
			continue;
		memtx_tx_handle_conflict(stmt->txn, tracker->reader);
	}
}

void
memtx_tx_history_prepare_stmt(struct txn_stmt *stmt)
{
	assert(stmt->txn->psn != 0);

	if (stmt->add_story != NULL) {
		memtx_tx_history_prepare_insert_stmt(stmt);
	} else {
		assert(stmt->del_story != NULL);
		memtx_tx_history_prepare_delete_stmt(stmt);
	}

	if (stmt->add_story != NULL)
		stmt->add_story->add_psn = stmt->txn->psn;
	if (stmt->del_story != NULL)
		stmt->del_story->del_psn = stmt->txn->psn;
}

ssize_t
memtx_tx_history_commit_stmt(struct txn_stmt *stmt)
{
	ssize_t res = 0;
	if (stmt->add_story != NULL) {
		assert(stmt->add_story->add_stmt == stmt);
		res += stmt->add_story->tuple->bsize;
		memtx_tx_story_unlink_added_by(stmt->add_story, stmt);
	}
	if (stmt->del_story != NULL) {
		res -= stmt->del_story->tuple->bsize;
		memtx_tx_story_unlink_deleted_by(stmt->del_story, stmt);
	}
	return res;
}

struct tuple *
memtx_tx_tuple_clarify_slow(struct txn *txn, struct space *space,
			    struct tuple *tuple, struct index *index,
			    uint32_t mk_index, bool is_prepared_ok)
{
	assert(tuple->is_dirty);
	struct memtx_story *story = memtx_tx_story_get(tuple);
	struct memtx_story *last_checked_story = story;
	bool own_change = false;
	struct tuple *result = NULL;

	while (true) {
		if (memtx_tx_story_is_visible(story, txn, &result,
					      is_prepared_ok, &own_change)) {
			break;
		}
		story = story->link[index->dense_id].older_story;
		if (story == NULL)
			break;
		last_checked_story = story;
	}
	if (!own_change) {
		/*
		 * If the result tuple exists (is visible) - it is visible in
		 * every index. But if we found a story of deleted tuple - we
		 * should record that only in the given index this transaction
		 * have found nothing by this key.
		 */
		int shift = index->dense_id & 63;
		uint64_t mask = result == NULL ? 1ull << shift : UINT64_MAX;
		memtx_tx_track_read_story(txn, space, last_checked_story, mask);
	}
	if (mk_index != 0) {
		assert(false); /* TODO: multiindex */
		panic("multikey indexes are not supported int TX manager");
	}
	return result;
}

uint32_t
memtx_tx_index_invisible_count_slow(struct txn *txn,
				    struct space *space, struct index *index)
{
	uint32_t res = 0;
	struct memtx_story *story;
	rlist_foreach_entry(story, &space->memtx_stories, in_space_stories) {
		if (story->space == NULL) {
			/* The story is unlinked. */
			continue;
		}
		assert(story->space == space);
		assert(index->dense_id < story->index_count);
		struct memtx_story_link *link = &story->link[index->dense_id];
		if (link->newer_story != NULL) {
			/* The story in in chain, but not at top. */
			continue;
		}

		struct tuple *visible = NULL;
		struct memtx_story *lookup = story;
		for (; lookup != NULL;
		       lookup = lookup->link[index->dense_id].older_story) {
			assert(index->dense_id < lookup->index_count);
			bool unused;
			if (memtx_tx_story_is_visible(lookup, txn,
						      &visible, true, &unused))
				break;
		}
		if (visible == NULL)
			res++;
	}
	return res;
}

static void
memtx_tx_delete_gap(struct gap_item *item)
{
	rlist_del(&item->in_gap_list);
	rlist_del(&item->in_nearby_gaps);
	mempool_free(&txm.gap_item_mempoool, item);
}

static void
memtx_tx_full_scan_item_delete(struct full_scan_item *item)
{
	rlist_del(&item->in_full_scan_list);
	rlist_del(&item->in_full_scans);
	mempool_free(&txm.full_scan_item_mempool, item);
}

void
memtx_tx_on_index_delete(struct index *index)
{
	while (!rlist_empty(&index->nearby_gaps)) {
		struct gap_item *item =
			rlist_first_entry(&index->nearby_gaps,
					  struct gap_item,
					  in_nearby_gaps);
		memtx_tx_delete_gap(item);
	}
	while (!rlist_empty(&index->full_scans)) {
		struct full_scan_item *item =
			rlist_first_entry(&index->full_scans,
					  struct full_scan_item,
					  in_full_scans);
		memtx_tx_full_scan_item_delete(item);
	}
}

void
memtx_tx_on_space_delete(struct space *space)
{
	while (!rlist_empty(&space->memtx_stories)) {
		struct memtx_story *story
			= rlist_first_entry(&space->memtx_stories,
					    struct memtx_story,
					    in_space_stories);
		/*
		 * Space is to be altered (not necessarily dropped). Since
		 * this operation is considered to be DDL, all other
		 * transactions will be aborted anyway. We can't postpone
		 * rollback till actual call of commit/rollback since stories
		 * should be destroyed immediately.
		 */
		if (story->add_stmt != NULL)
			memtx_tx_history_rollback_stmt(story->add_stmt);
		if (story->del_stmt != NULL)
			memtx_tx_history_rollback_stmt(story->del_stmt);
		rlist_del(&story->in_space_stories);
		memtx_tx_story_full_unlink(story);
		memtx_tx_story_delete(story);
	}
}

/**
 * Allocate and initialize tx_read_tracker, return NULL in case of error
 * (diag is set). Links in lists are not initialized though.
 */
static struct tx_read_tracker *
tx_read_tracker_new(struct txn *reader, struct memtx_story *story,
		    uint64_t index_mask)
{
	size_t sz;
	struct tx_read_tracker *tracker;
	tracker = region_alloc_object(&reader->region,
				      struct tx_read_tracker, &sz);
	if (tracker == NULL) {
		diag_set(OutOfMemory, sz, "tx region", "read_tracker");
		return NULL;
	}
	tracker->reader = reader;
	tracker->story = story;
	tracker->index_mask = index_mask;
	return tracker;
}

static int
memtx_tx_track_read_story_slow(struct txn *txn, struct memtx_story *story,
			       uint64_t index_mask)
{
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
		tracker->index_mask |= index_mask;
	} else {
		tracker = tx_read_tracker_new(txn, story, index_mask);
		if (tracker == NULL)
			return -1;
	}
	rlist_add(&story->reader_list, &tracker->in_reader_list);
	rlist_add(&txn->read_set, &tracker->in_read_set);
	memtx_tx_story_gc();
	return 0;
}

static int
memtx_tx_track_read_story(struct txn *txn, struct space *space,
			  struct memtx_story *story, uint64_t index_mask)
{
	if (txn == NULL)
		return 0;
	if (space == NULL)
		return 0;
	if (space->def->opts.is_ephemeral)
		return 0;
	return memtx_tx_track_read_story_slow(txn, story, index_mask);
}

int
memtx_tx_track_read(struct txn *txn, struct space *space, struct tuple *tuple)
{
	if (tuple == NULL)
		return 0;
	if (txn == NULL)
		return 0;
	if (space == NULL)
		return 0;
	if (space->def->opts.is_ephemeral)
		return 0;

	if (tuple->is_dirty) {
		struct memtx_story *story = memtx_tx_story_get(tuple);
		return memtx_tx_track_read_story(txn, space, story, UINT64_MAX);
	} else {
		struct memtx_story *story = memtx_tx_story_new(space, tuple);
		if (story == NULL)
			return -1;
		struct tx_read_tracker *tracker;
		tracker = tx_read_tracker_new(txn, story, UINT64_MAX);
		if (tracker == NULL) {
			memtx_tx_story_delete(story);
			return -1;
		}
		rlist_add(&story->reader_list, &tracker->in_reader_list);
		rlist_add(&txn->read_set, &tracker->in_read_set);
		return 0;
	}
}

/**
 * Create new point_hole_item by given argumnets and put it to hash table.
 */
static int
point_hole_storage_new(struct index *index, const char *key,
		       size_t key_len, struct txn *txn)
{
	struct mempool *pool = &txm.point_hole_item_pool;
	struct point_hole_item *object =
		(struct point_hole_item *) mempool_alloc(pool);
	if (object == NULL) {
		diag_set(OutOfMemory, sizeof(*object),
			 "mempool_alloc", "point_hole_item");
		return -1;
	}

	rlist_create(&object->ring);
	rlist_create(&object->in_point_holes_list);
	object->txn = txn;
	object->index_unique_id = index->unique_id;
	if (key_len <= sizeof(object->short_key)) {
		object->key = object->short_key;
	} else {
		object->key = (char *)region_alloc(&txn->region, key_len);
		if (object->key == NULL) {
			mempool_free(pool, object);
			diag_set(OutOfMemory, key_len, "tx region",
				 "point key");
			return -1;
		}
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
	mh_int_t pos = mh_point_holes_put(txm.point_holes, put,
					    &preplaced, 0);
	if (pos == mh_end(txm.point_holes)) {
		mempool_free(pool, object);
		diag_set(OutOfMemory, pos + 1, "mh_holes_storage_put",
			 "mh_holes_storage_node");
		return -1;
	}
	if (preplaced != NULL) {
		/*
		 * The item in hash table was overwitten. It's OK, but
		 * we need replaced item to the item list.
		 * */
		rlist_add(&replaced->ring, &object->ring);
		assert(replaced->is_head);
		replaced->is_head = false;
	} else {
		txm.point_holes_size++;
	}
	rlist_add(&txn->point_holes_list, &object->in_point_holes_list);
	return 0;
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
		mh_int_t pos = mh_point_holes_put(txm.point_holes, put,
						    &preplaced, 0);
		assert(pos != mh_end(txm.point_holes)); (void)pos;
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
		assert(pos != mh_end(txm.point_holes));
		mh_point_holes_del(txm.point_holes, pos, 0);
		txm.point_holes_size--;
	}
	rlist_del(&object->in_point_holes_list);
	struct mempool *pool = &txm.point_hole_item_pool;
	mempool_free(pool, object);
}

/**
 * Record in TX manager that a transaction @a txn have read a nothing
 * from @a space and @ a index with @ key.
 * The key is expected to be full, that is has part count equal to part
 * count in unique cmp_key of the index.
 * @return 0 on success, -1 on memory error.
 */
int
memtx_tx_track_point_slow(struct txn *txn, struct index *index, const char *key)
{
	if (txn->status != TXN_INPROGRESS)
		return 0;

	struct key_def *def = index->def->key_def;
	const char *tmp = key;
	for (uint32_t i = 0; i < def->part_count; i++)
		mp_next(&tmp);
	size_t key_len = tmp - key;
	return point_hole_storage_new(index, key, key_len, txn);
}

static struct gap_item *
memtx_tx_gap_item_new(struct txn *txn, enum iterator_type type,
		      const char *key, uint32_t part_count)
{
	struct gap_item *item = (struct gap_item *)
		mempool_alloc(&txm.gap_item_mempoool);
	if (item == NULL) {
		diag_set(OutOfMemory, sizeof(*item), "mempool_alloc", "gap");
		return NULL;
	}

	item->txn = txn;
	item->type = type;
	item->part_count = part_count;
	const char *tmp = key;
	for (uint32_t i = 0; i < part_count; i++)
		mp_next(&tmp);
	item->key_len = tmp - key;
	if (part_count == 0) {
		item->key = NULL;
	} else if (item->key_len <= sizeof(item->short_key)) {
		item->key = item->short_key;
	} else {
		item->key = (char *)region_alloc(&txn->region, item->key_len);
		if (item->key == NULL) {
			mempool_free(&txm.gap_item_mempoool, item);
			diag_set(OutOfMemory, item->key_len, "tx region",
				 "point key");
			return NULL;
		}
	}
	memcpy((char *)item->key, key, item->key_len);
	rlist_add(&txn->gap_list, &item->in_gap_list);
	return item;
}

/**
 * Record in TX manager that a transaction @a txn have read nothing
 * from @a space and @ a index with @ key, somewhere from interval between
 * some unknown predecessor and @a successor.
 * This function must be used for ordered indexes, such as TREE, for queries
 * when interation type is not EQ or when the key is not full (otherwise
 * it's faster to use memtx_tx_track_point).
 * @return 0 on success, -1 on memory error.
 */
int
memtx_tx_track_gap_slow(struct txn *txn, struct space *space, struct index *index,
			struct tuple *successor, enum iterator_type type,
			const char *key, uint32_t part_count)
{
	if (txn->status != TXN_INPROGRESS)
		return 0;

	struct gap_item *item = memtx_tx_gap_item_new(txn, type, key,
						      part_count);
	if (item == NULL)
		return -1;

	if (successor != NULL) {
		struct memtx_story *story;
		if (successor->is_dirty) {
			story = memtx_tx_story_get(successor);
		} else {
			story = memtx_tx_story_new(space, successor);
			if (story == NULL) {
				mempool_free(&txm.gap_item_mempoool, item);
				return -1;
			}
		}
		assert(index->dense_id < story->index_count);
		rlist_add(&story->link[index->dense_id].nearby_gaps,
			  &item->in_nearby_gaps);
	} else {
		rlist_add(&index->nearby_gaps, &item->in_nearby_gaps);
	}
	memtx_tx_story_gc();
	return 0;
}

static struct full_scan_item *
memtx_tx_full_scan_item_new(struct txn *txn)
{
	struct full_scan_item *item = (struct full_scan_item *)
		mempool_alloc(&txm.full_scan_item_mempool);
	if (item == NULL) {
		diag_set(OutOfMemory, sizeof(*item), "mempool_alloc",
			 "full_scan_item");
		return NULL;
	}

	item->txn = txn;
	rlist_add(&txn->full_scan_list, &item->in_full_scan_list);
	return item;
}

/**
 * Record in TX manager that a transaction @a txn have read full @ a index.
 * This function must be used for unordered indexes, such as HASH, for queries
 * when interation type is ALL.
 * @return 0 on success, -1 on memory error.
 */
int
memtx_tx_track_full_scan_slow(struct txn *txn, struct index *index)
{
	if (txn->status != TXN_INPROGRESS)
		return 0;

	struct full_scan_item *item = memtx_tx_full_scan_item_new(txn);
	if (item == NULL)
		return -1;

	rlist_add(&index->full_scans, &item->in_full_scans);
	memtx_tx_story_gc();
	return 0;
}

/**
 * Clean memtx_tx part of @a txm.
 */
void
memtx_tx_clean_txn(struct txn *txn)
{
	while (!rlist_empty(&txn->point_holes_list)) {
		struct point_hole_item *object =
			rlist_first_entry(&txn->point_holes_list,
					  struct point_hole_item,
					  in_point_holes_list);
		point_hole_storage_delete(object);
	}
	while (!rlist_empty(&txn->gap_list)) {
		struct gap_item *item =
			rlist_first_entry(&txn->gap_list,
					  struct gap_item,
					  in_gap_list);
		memtx_tx_delete_gap(item);
	}
	while (!rlist_empty(&txn->full_scan_list)) {
		struct full_scan_item *item =
			rlist_first_entry(&txn->full_scan_list,
					  struct full_scan_item,
					  in_full_scan_list);
		memtx_tx_full_scan_item_delete(item);
	}
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

int
memtx_tx_snapshot_cleaner_create(struct memtx_tx_snapshot_cleaner *cleaner,
				 struct space *space, const char *index_name)
{
	cleaner->ht = NULL;
	if (space == NULL || rlist_empty(&space->memtx_stories))
		return 0;
	struct mh_snapshot_cleaner_t *ht = mh_snapshot_cleaner_new();
	if (ht == NULL) {
		diag_set(OutOfMemory, sizeof(*ht),
			 index_name, "snapshot cleaner");
		free(ht);
		return -1;
	}

	struct memtx_story *story;
	rlist_foreach_entry(story, &space->memtx_stories, in_space_stories) {
		struct tuple *tuple = story->tuple;
		struct tuple *clean =
			memtx_tx_tuple_clarify_slow(NULL, space, tuple,
						    space->index[0], 0, true);
		if (clean == tuple)
			continue;

		struct memtx_tx_snapshot_cleaner_entry entry;
		entry.from = tuple;
		entry.to = clean;
		mh_int_t res =  mh_snapshot_cleaner_put(ht,  &entry, NULL, 0);
		if (res == mh_end(ht)) {
			diag_set(OutOfMemory, sizeof(entry),
				 index_name, "snapshot rollback entry");
			mh_snapshot_cleaner_delete(ht);
			return -1;
		}
	}

	cleaner->ht = ht;
	return 0;
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
