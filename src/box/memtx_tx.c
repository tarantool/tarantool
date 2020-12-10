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
	/** List of all memtx_story objects. */
	struct rlist all_stories;
	/** Iterator that sequentially traverses all memtx_story objects. */
	struct rlist *traverse_all_stories;
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
	rlist_create(&txm.all_stories);
	txm.traverse_all_stories = &txm.all_stories;
}

void
memtx_tx_manager_free()
{
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

/** See definition for details */
static void
memtx_tx_story_gc_step();

/**
 * Create a new story and link it with the @a tuple.
 * @return story on success, NULL on error (diag is set).
 */
static struct memtx_story *
memtx_tx_story_new(struct space *space, struct tuple *tuple)
{
	/* Free some memory. */
	for (size_t i = 0; i < TX_MANAGER_GC_STEPS_SIZE; i++)
		memtx_tx_story_gc_step();
	assert(!tuple_is_dirty(tuple));
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
	tuple_set_dirty_bit(tuple, true);
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
	memset(story->link, 0, sizeof(story->link[0]) * index_count);
	return story;
}

static void
memtx_tx_story_delete(struct memtx_story *story);

/**
 * Create a new story of a @a tuple that was added by @a stmt.
 * @return story on success, NULL on error (diag is set).
 */
static struct memtx_story *
memtx_tx_story_new_add_stmt(struct tuple *tuple, struct txn_stmt *stmt)
{
	struct memtx_story *res = memtx_tx_story_new(stmt->space, tuple);
	if (res == NULL)
		return NULL;
	res->add_stmt = stmt;
	assert(stmt->add_story == NULL);
	stmt->add_story = res;
	return res;
}

/**
 * Create a new story of a @a tuple that was deleted by @a stmt.
 * @return story on success, NULL on error (diag is set).
 */
static struct memtx_story *
memtx_tx_story_new_del_stmt(struct tuple *tuple, struct txn_stmt *stmt)
{
	struct memtx_story *res = memtx_tx_story_new(stmt->space, tuple);
	if (res == NULL)
		return NULL;
	res->del_stmt = stmt;
	assert(stmt->del_story == NULL);
	stmt->del_story = res;
	return res;
}

/**
 * Undo memtx_tx_story_new_add_stmt.
 */
static void
memtx_tx_story_delete_add_stmt(struct memtx_story *story)
{
	story->add_stmt->add_story = NULL;
	story->add_stmt = NULL;
	memtx_tx_story_delete(story);
}

/**
 * Undo memtx_tx_story_new_del_stmt.
 */
static void
memtx_tx_story_delete_del_stmt(struct memtx_story *story)
{
	story->del_stmt->del_story = NULL;
	story->del_stmt = NULL;
	memtx_tx_story_delete(story);
}


/**
 * Find a story of a @a tuple. The story expected to be present (assert).
 */
static struct memtx_story *
memtx_tx_story_get(struct tuple *tuple)
{
	assert(tuple_is_dirty(tuple));

	mh_int_t pos = mh_history_find(txm.history, tuple, 0);
	assert(pos != mh_end(txm.history));
	return *mh_history_node(txm.history, pos);
}

/**
 * Get the older tuple, extracting it from older story if necessary.
 */
static struct tuple *
memtx_tx_story_older_tuple(struct memtx_story_link *link)
{
	return link->older.is_story ? link->older.story->tuple
				    : link->older.tuple;
}

/**
 * Link a @a story with @a older_story in @a index (in both directions).
 */
static void
memtx_tx_story_link_story(struct memtx_story *story,
			  struct memtx_story *older_story,
			  uint32_t index)
{
	assert(older_story != NULL);
	struct memtx_story_link *link = &story->link[index];
	/* Must be unlinked. */
	assert(!link->older.is_story);
	assert(link->older.tuple == NULL);
	link->older.is_story = true;
	link->older.story = older_story;
	older_story->link[index].newer_story = story;
}

/**
 * Link a @a story with older @a tuple in @a index. In case if the tuple is
 * dirty -find and link with the corresponding story.
 */
static void
memtx_tx_story_link_tuple(struct memtx_story *story,
			  struct tuple *older_tuple,
			  uint32_t index)
{
	struct memtx_story_link *link = &story->link[index];
	/* Must be unlinked. */
	assert(!link->older.is_story);
	assert(link->older.tuple == NULL);
	if (older_tuple == NULL)
		return;
	if (tuple_is_dirty(older_tuple)) {
		memtx_tx_story_link_story(story,
					  memtx_tx_story_get(older_tuple),
					  index);
		return;
	}
	link->older.tuple = older_tuple;
	tuple_ref(link->older.tuple);
}

/**
 * Unlink a @a story with older story/tuple in @a index.
 */
static void
memtx_tx_story_unlink(struct memtx_story *story, uint32_t index)
{
	struct memtx_story_link *link = &story->link[index];
	if (link->older.is_story) {
		link->older.story->link[index].newer_story = NULL;
	} else if (link->older.tuple != NULL) {
		tuple_unref(link->older.tuple);
		link->older.tuple = NULL;
	}
	link->older.is_story = false;
	link->older.tuple = NULL;
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

	/* Unlink and delete the story */
	for (uint32_t i = 0; i < story->index_count; i++) {
		struct memtx_story_link *link = &story->link[i];
		if (link->newer_story == NULL) {
			/*
			 * We are at the top of the chain. That means
			 * that story->tuple is in index. If the story
			 * actually deletes the tuple, it must be deleted from
			 * index.
			 */
			if (story->del_psn > 0 && story->space != NULL) {
				struct index *index = story->space->index[i];
				struct tuple *unused;
				if (index_replace(index, story->tuple, NULL,
						  DUP_INSERT, &unused) != 0) {
					diag_log();
					unreachable();
					panic("failed to rollback change");
				}
				assert(story->tuple == unused);
			} else if (i == 0) {
				/*
				 * The tuple is left clean in the space.
				 * It now belongs to the space and must be referenced.
				 */
				tuple_ref(story->tuple);
			}

			if (link->older.is_story) {
				struct memtx_story *older_story = link->older.story;
				memtx_tx_story_unlink(story, i);
				older_story->link[i].newer_story = older_story;
			} else {
				memtx_tx_story_unlink(story, i);
			}
		} else {
			/* Just unlink from list */
			link->newer_story->link[i].older = link->older;
			if (link->older.is_story)
				link->older.story->link[i].newer_story =
					link->newer_story;
			link->older.is_story = false;
			link->older.story = NULL;
			link->newer_story = NULL;
		}
	}

	memtx_tx_story_delete(story);
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
	while (dels != NULL) {
		if (dels->txn == txn) {
			/* Tuple is deleted by us (@txn). */
			*own_change = true;
			return true;
		}
		dels = dels->next_in_del_list;
	}
	if (is_prepared_ok && story->del_psn != 0 && story->del_psn < rv_psn) {
		/* Tuple is deleted by prepared TX. */
		return true;
	}
	if (story->del_psn != 0 && story->del_stmt == NULL &&
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
	/* Link in single-linked list. */
	struct memtx_tx_conflict *next;
};

/**
 * Save @a breaker in list with head @a conflicts_head. New list node is
 * allocated on @a region.
 * @return 0 on success, -1 on memory error.
 */
static int
memtx_tx_save_conflict(struct txn *breaker,
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
				  struct tuple **visible_replaced,
				  struct memtx_tx_conflict **collected_conflicts,
				  struct region *region)
{
	while (true) {
		if (!story->link[index].older.is_story) {
			/* The tuple is so old that we don't know its story. */
			*visible_replaced = story->link[index].older.tuple;
			assert(*visible_replaced == NULL ||
			       !tuple_is_dirty(*visible_replaced));
			break;
		}
		story = story->link[index].older.story;
		bool unused;
		if (memtx_tx_story_is_visible(story, stmt->txn,
					      visible_replaced, true, &unused))
			break;

		/*
		 * We skip the story as invisible but the corresponding TX
		 * is committed our TX can become conflicted.
		 * The conflict will be unavoidable if this statement
		 * relies on old_tuple. If not (it's a replace),
		 * the conflict will take place only for secondary
		 * index if the story will not be overwritten in primary
		 * index.
		 */
		bool cross_conflict = false;
		if (stmt->does_require_old_tuple) {
			cross_conflict = true;
		} else if (index != 0) {
			struct memtx_story *look_up = story;
			cross_conflict = true;
			while (look_up->link[0].newer_story != NULL) {
				struct memtx_story *over;
				over = look_up->link[0].newer_story;
				if (over->add_stmt->txn == stmt->txn) {
					cross_conflict = false;
					break;
				}
				look_up = over;
			}
		}
		if (cross_conflict) {
			if (memtx_tx_save_conflict(story->add_stmt->txn,
						   collected_conflicts,
						   region) != 0)
				return -1;

		}
	}
	return 0;
}

int
memtx_tx_history_add_stmt(struct txn_stmt *stmt, struct tuple *old_tuple,
			  struct tuple *new_tuple, enum dup_replace_mode mode,
			  struct tuple **result)
{
	assert(new_tuple != NULL || old_tuple != NULL);
	struct space *space = stmt->space;
	assert(space != NULL);
	struct memtx_story *add_story = NULL;
	uint32_t add_story_linked = 0;
	struct memtx_story *del_story = NULL;
	bool del_story_created = false;
	struct region *region = &stmt->txn->region;
	size_t region_svp = region_used(region);

	/*
	 * List of transactions that will conflict us once one of them
	 * become committed.
	 */
	struct memtx_tx_conflict *collected_conflicts = NULL;

	/* Create add_story if necessary. */
	if (new_tuple != NULL) {
		add_story = memtx_tx_story_new_add_stmt(new_tuple, stmt);
		if (add_story == NULL)
			goto fail;

		for (uint32_t i = 0; i < space->index_count; i++) {
			struct tuple *replaced;
			struct index *index = space->index[i];
			if (index_replace(index, NULL, new_tuple,
					  DUP_REPLACE_OR_INSERT,
					  &replaced) != 0)
				goto fail;
			memtx_tx_story_link_tuple(add_story, replaced, i);
			if (i == 0 && replaced != NULL &&
				      !tuple_is_dirty(replaced)) {
				/*
				 * The tuple was clean and thus belonged to
				 * the space. Now tx manager takes ownership
				 * of it.
				 */
				tuple_unref(replaced);
			}
			add_story_linked++;

			struct tuple *visible_replaced = NULL;
			if (memtx_tx_story_find_visible_tuple(add_story, stmt, i,
							      &visible_replaced,
							      &collected_conflicts,
							      region) != 0)
				goto fail;

			uint32_t errcode;
			errcode = replace_check_dup(old_tuple, visible_replaced,
						    i == 0 ? mode : DUP_INSERT);
			if (errcode != 0) {
				diag_set(ClientError, errcode,
					 index->def->name,
					 space_name(space));
				goto fail;
			}

			if (i == 0)
				old_tuple = visible_replaced;
		}
	}

	/* Create del_story if necessary. */
	struct tuple *del_tuple = NULL;
	if (new_tuple != NULL) {
		struct memtx_story_link *link = &add_story->link[0];
		if (link->older.is_story) {
			del_story = link->older.story;
			del_tuple = del_story->tuple;
		} else {
			del_tuple = link->older.tuple;
		}
	} else {
		del_tuple = old_tuple;
	}
	if (del_tuple != NULL && del_story == NULL) {
		if (tuple_is_dirty(del_tuple)) {
			del_story = memtx_tx_story_get(del_tuple);
		} else {
			del_story = memtx_tx_story_new_del_stmt(del_tuple,
								stmt);
			if (del_story == NULL)
				goto fail;
			del_story_created = true;
		}
	}
	if (new_tuple != NULL && del_story_created) {
		for (uint32_t i = 0; i < add_story->index_count; i++) {
			struct memtx_story_link *link = &add_story->link[i];
			if (link->older.is_story)
				continue;
			if (link->older.tuple == del_tuple) {
				memtx_tx_story_unlink(add_story, i);
				memtx_tx_story_link_story(add_story, del_story,
							  i);
			}
		}
	}
	if (del_story != NULL && !del_story_created) {
		stmt->next_in_del_list = del_story->del_stmt;
		del_story->del_stmt = stmt;
		stmt->del_story = del_story;
	}

	/* Purge found conflicts. */
	while (collected_conflicts != NULL) {
		if (memtx_tx_cause_conflict(collected_conflicts->breaker,
					    stmt->txn) != 0)
			goto fail;
		collected_conflicts = collected_conflicts->next;
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
	return 0;

	fail:
	if (add_story != NULL) {
		while (add_story_linked > 0) {
			--add_story_linked;
			uint32_t i = add_story_linked;

			struct index *index = space->index[i];
			struct memtx_story_link *link = &add_story->link[i];
			struct tuple *was = memtx_tx_story_older_tuple(link);
			struct tuple *unused;
			if (index_replace(index, new_tuple, was,
					  DUP_INSERT, &unused) != 0) {
				diag_log();
				unreachable();
				panic("failed to rollback change");
			}
			if (i == 0 && was != NULL && !tuple_is_dirty(was)) {
				/* Just rollback previous tuple_unref. */
				tuple_ref(was);
			}

			memtx_tx_story_unlink(stmt->add_story, i);

		}
		memtx_tx_story_delete_add_stmt(stmt->add_story);
	}

	if (del_story != NULL && del_story->del_stmt == stmt) {
		del_story->del_stmt = stmt->next_in_del_list;
		stmt->next_in_del_list = NULL;
	}

	if (del_story_created)
		memtx_tx_story_delete_del_stmt(stmt->del_story);
	else
		stmt->del_story = NULL;

	region_truncate(region, region_svp);
	return -1;
}

void
memtx_tx_history_rollback_stmt(struct txn_stmt *stmt)
{
	if (stmt->add_story != NULL) {
		assert(stmt->add_story->tuple == stmt->new_tuple);
		struct memtx_story *story = stmt->add_story;

		for (uint32_t i = 0; i < story->index_count; i++) {
			struct memtx_story_link *link = &story->link[i];
			if (link->newer_story == NULL) {
				/*
				 * We are at top of story list and thus
				 * story->tuple is in index directly.
				 */
				struct tuple *unused;
				struct index *index = stmt->space->index[i];
				struct tuple *was = memtx_tx_story_older_tuple(link);
				if (index_replace(index, story->tuple, was,
						  DUP_INSERT, &unused) != 0) {
					diag_log();
					unreachable();
					panic("failed to rollback change");
				}
				if (i == 0 && was != NULL &&
				    !link->older.is_story) {
					/*
					 * That was the last story in history.
					 * The last tuple now belongs to space
					 * and the space must hold a reference
					 * to it.
					 */
					tuple_ref(was);
				}

			} else {
				struct memtx_story *newer = link->newer_story;
				assert(newer->link[i].older.is_story);
				assert(newer->link[i].older.story == story);
				memtx_tx_story_unlink(newer, i);
				if (link->older.is_story) {
					struct memtx_story *to = link->older.story;
					memtx_tx_story_link_story(newer, to, i);
				} else {
					struct tuple *to = link->older.tuple;
					memtx_tx_story_link_tuple(newer, to, i);
				}
			}
			memtx_tx_story_unlink(story, i);
		}
		stmt->add_story->add_stmt = NULL;
		stmt->add_story = NULL;
	}

	if (stmt->del_story != NULL) {
		struct memtx_story *story = stmt->del_story;

		struct txn_stmt **prev = &story->del_stmt;
		while (*prev != stmt) {
			prev = &(*prev)->next_in_del_list;
			assert(*prev != NULL);
		}
		*prev = stmt->next_in_del_list;
		stmt->next_in_del_list = NULL;

		stmt->del_story->del_stmt = NULL;
		stmt->del_story = NULL;
	}
}

void
memtx_tx_history_prepare_stmt(struct txn_stmt *stmt)
{
	assert(stmt->txn->psn != 0);

	/* Move story to the past to prepared stories. */

	struct memtx_story *story = stmt->add_story;
	uint32_t index_count = story == NULL ? 0 : story->index_count;
	/*
	 * Note that if stmt->add_story == NULL, the index_count is set to 0,
	 * and we will not enter the loop.
	 */
	for (uint32_t i = 0; i < index_count; ) {
		if (!story->link[i].older.is_story) {
			/* tuple is old. */
			i++;
			continue;
		}
		bool old_story_is_prepared = false;
		struct memtx_story *old_story = story->link[i].older.story;
		if (old_story->del_psn != 0) {
			/* if psn is set, the change is prepared. */
			old_story_is_prepared = true;
		} else if (old_story->add_psn != 0) {
			/* if psn is set, the change is prepared. */
			old_story_is_prepared = true;
		} else if (old_story->add_stmt == NULL) {
			/* ancient. */
			old_story_is_prepared = true;
		} else if (old_story->add_stmt->txn == stmt->txn) {
			/* added by us. */
		}

		if (old_story_is_prepared) {
			struct tx_read_tracker *tracker;
			rlist_foreach_entry(tracker, &old_story->reader_list,
					    in_reader_list) {
				if (tracker->reader == stmt->txn)
					continue;
				if (tracker->reader->status != TXN_INPROGRESS)
					continue;
				memtx_tx_handle_conflict(stmt->txn,
							 tracker->reader);
			}
			i++;
			continue;
		}

		if (old_story->add_stmt->does_require_old_tuple || i != 0)
			old_story->add_stmt->txn->status = TXN_CONFLICTED;

		/* Swap story and old story. */
		struct memtx_story_link *link = &story->link[i];
		if (link->newer_story == NULL) {
			/* we have to replace the tuple in index. */
			struct tuple *unused;
			struct index *index = stmt->space->index[i];
			if (index_replace(index, story->tuple, old_story->tuple,
					  DUP_INSERT, &unused) != 0) {
				diag_log();
				panic("failed to rollback change");
			}
		} else {
			struct memtx_story *newer = link->newer_story;
			assert(newer->link[i].older.is_story);
			assert(newer->link[i].older.story == story);
			memtx_tx_story_unlink(newer, i);
			memtx_tx_story_link_story(newer, old_story, i);
		}

		memtx_tx_story_unlink(story, i);
		if (old_story->link[i].older.is_story) {
			struct memtx_story *to =
				old_story->link[i].older.story;
			memtx_tx_story_unlink(old_story, i);
			memtx_tx_story_link_story(story, to, i);
		} else {
			struct tuple *to =
				old_story->link[i].older.tuple;
			memtx_tx_story_unlink(old_story, i);
			memtx_tx_story_link_tuple(story, to, i);
		}

		memtx_tx_story_link_story(old_story, story, i);

		if (i == 0) {
			assert(stmt->del_story == old_story);
			assert(story->link[0].older.is_story ||
			       story->link[0].older.tuple == NULL);

			struct txn_stmt *dels = old_story->del_stmt;
			assert(dels != NULL);
			do {
				if (dels->txn != stmt->txn)
					dels->txn->status = TXN_CONFLICTED;
				dels->del_story = NULL;
				struct txn_stmt *next = dels->next_in_del_list;
				dels->next_in_del_list = NULL;
				dels = next;
			} while (dels != NULL);
			old_story->del_stmt = NULL;

			if (story->link[0].older.is_story) {
				struct memtx_story *oldest_story =
					story->link[0].older.story;
				dels = oldest_story->del_stmt;
				while (dels != NULL) {
					assert(dels->txn != stmt->txn);
					dels->del_story = NULL;
					struct txn_stmt *next =
						dels->next_in_del_list;
					dels->next_in_del_list = NULL;
					dels = next;
				}
				oldest_story->del_stmt = stmt;
				stmt->del_story = oldest_story;
			}
		}
	}
	if (stmt->add_story != NULL)
		stmt->add_story->add_psn = stmt->txn->psn;

	if (stmt->del_story != NULL) {
		stmt->del_story->del_psn = stmt->txn->psn;
		// Let's conflict all other deleting stories.
		struct txn_stmt *dels = stmt->del_story->del_stmt;
		while (dels != NULL) {
			struct txn_stmt *next = dels->next_in_del_list;
			if (dels != stmt) {
				dels->del_story = NULL;
				dels->next_in_del_list = NULL;
			}
			dels = next;
		}
		// Set the only deleting statement for that story.
		stmt->del_story->del_stmt = stmt;
		stmt->next_in_del_list = NULL;
	}
}

ssize_t
memtx_tx_history_commit_stmt(struct txn_stmt *stmt)
{
	size_t res = 0;
	if (stmt->add_story != NULL) {
		assert(stmt->add_story->add_stmt == stmt);
		res += tuple_bsize(stmt->add_story->tuple);
		stmt->add_story->add_stmt = NULL;
		stmt->add_story = NULL;
	}
	if (stmt->del_story != NULL) {
		assert(stmt->del_story->del_stmt == stmt);
		assert(stmt->next_in_del_list == NULL);
		res -= tuple_bsize(stmt->del_story->tuple);
		stmt->del_story->del_stmt = NULL;
		stmt->del_story = NULL;
	}
	return res;
}

struct tuple *
memtx_tx_tuple_clarify_slow(struct txn *txn, struct space *space,
			    struct tuple *tuple, uint32_t index,
			    uint32_t mk_index, bool is_prepared_ok)
{
	assert(tuple_is_dirty(tuple));
	struct memtx_story *story = memtx_tx_story_get(tuple);
	bool own_change = false;
	struct tuple *result = NULL;

	while (true) {
		if (memtx_tx_story_is_visible(story, txn, &result,
					      is_prepared_ok, &own_change)) {
			break;
		}
		if (story->link[index].older.is_story) {
			story = story->link[index].older.story;
		} else {
			result = story->link[index].older.tuple;
			break;
		}
	}
	if (!own_change)
		memtx_tx_track_read(txn, space, tuple);
	if (mk_index != 0) {
		assert(false); /* TODO: multiindex */
		panic("multikey indexes are not supported int TX manager");
	}
	return result;
}

void
memtx_tx_on_space_delete(struct space *space)
{
	/* Just clear pointer to space, it will be handled in GC. */
	while (!rlist_empty(&space->memtx_stories)) {
		struct memtx_story *story
			= rlist_first_entry(&space->memtx_stories,
					    struct memtx_story,
					    in_space_stories);
		story->space = NULL;
		rlist_del(&story->in_space_stories);
	}
}

static void
memtx_tx_story_delete(struct memtx_story *story)
{
	assert(story->add_stmt == NULL);
	assert(story->del_stmt == NULL);

	if (txm.traverse_all_stories == &story->in_all_stories)
		txm.traverse_all_stories = rlist_next(txm.traverse_all_stories);
	rlist_del(&story->in_all_stories);
	rlist_del(&story->in_space_stories);

	mh_int_t pos = mh_history_find(txm.history, story->tuple, 0);
	assert(pos != mh_end(txm.history));
	mh_history_del(txm.history, pos, 0);

	tuple_set_dirty_bit(story->tuple, false);
	tuple_unref(story->tuple);

#ifndef NDEBUG
	/* Expecting to delete fully unlinked story. */
	for (uint32_t i = 0; i < story->index_count; i++) {
		assert(story->link[i].newer_story == NULL);
		assert(story->link[i].older.is_story == false);
		assert(story->link[i].older.tuple == NULL);
	}
#endif

	struct mempool *pool = &txm.memtx_tx_story_pool[story->index_count];
	mempool_free(pool, story);
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

	struct memtx_story *story;
	struct tx_read_tracker *tracker = NULL;

	if (!tuple_is_dirty(tuple)) {
		story = memtx_tx_story_new(space, tuple);
		if (story == NULL)
			return -1;
		size_t sz;
		tracker = region_alloc_object(&txn->region,
					      struct tx_read_tracker, &sz);
		if (tracker == NULL) {
			diag_set(OutOfMemory, sz, "tx region", "read_tracker");
			memtx_tx_story_delete(story);
			return -1;
		}
		tracker->reader = txn;
		tracker->story = story;
		rlist_add(&story->reader_list, &tracker->in_reader_list);
		rlist_add(&txn->read_set, &tracker->in_read_set);
		return 0;
	}
	story = memtx_tx_story_get(tuple);

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
		size_t sz;
		tracker = region_alloc_object(&txn->region,
					      struct tx_read_tracker, &sz);
		if (tracker == NULL) {
			diag_set(OutOfMemory, sz, "tx region", "read_tracker");
			return -1;
		}
		tracker->reader = txn;
		tracker->story = story;
	}
	rlist_add(&story->reader_list, &tracker->in_reader_list);
	rlist_add(&txn->read_set, &tracker->in_read_set);
	return 0;
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
			memtx_tx_tuple_clarify_slow(NULL, space, tuple, 0, 0,
						    true);
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
