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
#include "vy_tx.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <small/mempool.h>
#include <small/rlist.h>

#include "diag.h"
#include "errcode.h"
#include "fiber.h"
#include "iproto_constants.h"
#include "iterator_type.h"
#include "salad/stailq.h"
#include "schema.h" /* schema_version */
#include "trigger.h"
#include "trivia/util.h"
#include "tuple.h"
#include "vy_cache.h"
#include "vy_index.h"
#include "vy_mem.h"
#include "vy_stat.h"
#include "vy_stmt.h"
#include "vy_stmt_iterator.h"
#include "vy_upsert.h"

int
read_set_cmp(struct txv *a, struct txv *b)
{
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0)
		rc = vy_stmt_compare(a->stmt, b->stmt, a->index->key_def);
	if (rc == 0)
		rc = a->tx < b->tx ? -1 : a->tx > b->tx;
	return rc;
}

int
read_set_key_cmp(struct read_set_key *a, struct txv *b)
{
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0)
		rc = vy_stmt_compare(a->stmt, b->stmt, b->index->key_def);
	if (rc == 0)
		rc = a->tx < b->tx ? -1 : a->tx > b->tx;
	return rc;
}

int
write_set_cmp(struct txv *a, struct txv *b)
{
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0)
		return vy_stmt_compare(a->stmt, b->stmt, a->index->key_def);
	return rc;
}

int
write_set_key_cmp(struct write_set_key *a, struct txv *b)
{
	int rc = a->index < b->index ? -1 : a->index > b->index;
	if (rc == 0)
		return vy_stmt_compare(a->stmt, b->stmt, a->index->key_def);
	return rc;
}

/**
 * Initialize an instance of a global read view.
 * To be used exclusively by the transaction manager.
 */
static void
vy_global_read_view_create(struct vy_read_view *rv, int64_t lsn)
{
	rlist_create(&rv->in_read_views);
	/*
	 * By default, the transaction is assumed to be
	 * read-write, and it reads the latest changes of all
	 * prepared transactions. This makes it possible to
	 * use the tuple cache in it.
	 */
	rv->vlsn = lsn;
	rv->refs = 0;
	rv->is_aborted = false;
}

struct tx_manager *
tx_manager_new(void)
{
	struct tx_manager *xm = calloc(1, sizeof(*xm));
	if (xm == NULL) {
		diag_set(OutOfMemory, sizeof(*xm),
			 "malloc", "struct tx_manager");
		return NULL;
	}

	read_set_new(&xm->read_set);
	rlist_create(&xm->read_views);
	vy_global_read_view_create((struct vy_read_view *)&xm->global_read_view,
				   INT64_MAX);
	xm->p_global_read_view = &xm->global_read_view;
	vy_global_read_view_create((struct vy_read_view *)&xm->committed_read_view,
				   MAX_LSN - 1);
	xm->p_committed_read_view = &xm->committed_read_view;

	struct slab_cache *slab_cache = cord_slab_cache();
	mempool_create(&xm->tx_mempool, slab_cache, sizeof(struct vy_tx));
	mempool_create(&xm->txv_mempool, slab_cache, sizeof(struct txv));
	mempool_create(&xm->read_view_mempool, slab_cache,
		       sizeof(struct vy_read_view));
	return xm;
}

void
tx_manager_delete(struct tx_manager *xm)
{
	mempool_destroy(&xm->read_view_mempool);
	mempool_destroy(&xm->txv_mempool);
	mempool_destroy(&xm->tx_mempool);
	free(xm);
}

/** Create or reuse an instance of a read view. */
static struct vy_read_view *
tx_manager_read_view(struct tx_manager *xm)
{
	struct vy_read_view *rv;
	/*
	 * Check if the last read view can be reused. Reference
	 * and return it if it's the case.
	 */
	if (!rlist_empty(&xm->read_views)) {
		rv = rlist_last_entry(&xm->read_views, struct vy_read_view,
				      in_read_views);
		/** Reuse an existing read view */
		if ((xm->last_prepared_tx == NULL && rv->vlsn == xm->lsn) ||
		    (xm->last_prepared_tx != NULL &&
		     rv->vlsn == MAX_LSN + xm->last_prepared_tx->psn)) {

			rv->refs++;
			return  rv;
		}
	}
	rv = mempool_alloc(&xm->read_view_mempool);
	if (rv == NULL) {
		diag_set(OutOfMemory, sizeof(*rv),
			 "mempool", "read view");
		return NULL;
	}
	rv->is_aborted = false;
	if (xm->last_prepared_tx != NULL) {
		rv->vlsn = MAX_LSN + xm->last_prepared_tx->psn;
		xm->last_prepared_tx->read_view = rv;
		rv->refs = 2;
	} else {
		rv->vlsn = xm->lsn;
		rv->refs = 1;
	}
	/*
	 * Add to the tail of the list, so that tx_manager_vlsn()
	 * works correctly.
	 */
	rlist_add_tail_entry(&xm->read_views, rv, in_read_views);
	return rv;
}

/** Dereference and possibly destroy a read view. */
static void
tx_manager_destroy_read_view(struct tx_manager *xm,
			     const struct vy_read_view *read_view)
{
	struct vy_read_view *rv = (struct vy_read_view *) read_view;
	if (rv == xm->p_global_read_view)
		return;
	assert(rv->refs);
	if (--rv->refs == 0) {
		rlist_del_entry(rv, in_read_views);
		mempool_free(&xm->read_view_mempool, rv);
	}
}

int64_t
tx_manager_vlsn(struct tx_manager *xm)
{
	if (rlist_empty(&xm->read_views))
		return xm->lsn;
	struct vy_read_view *oldest = rlist_first_entry(&xm->read_views,
							struct vy_read_view,
							in_read_views);
	return oldest->vlsn;
}

static struct txv *
txv_new(struct vy_tx *tx, struct vy_index *index, struct tuple *stmt)
{
	struct txv *v = mempool_alloc(&tx->xm->txv_mempool);
	if (v == NULL) {
		diag_set(OutOfMemory, sizeof(*v), "mempool", "struct txv");
		return NULL;
	}
	v->index = index;
	v->mem = NULL;
	v->stmt = stmt;
	tuple_ref(stmt);
	v->region_stmt = NULL;
	v->tx = tx;
	v->is_overwritten = false;
	v->overwritten = NULL;
	return v;
}

static void
txv_delete(struct txv *v)
{
	tuple_unref(v->stmt);
	mempool_free(&v->tx->xm->txv_mempool, v);
}

void
vy_tx_create(struct tx_manager *xm, struct vy_tx *tx)
{
	stailq_create(&tx->log);
	write_set_new(&tx->write_set);
	tx->write_set_version = 0;
	tx->write_size = 0;
	tx->xm = xm;
	tx->state = VINYL_TX_READY;
	tx->read_view = (struct vy_read_view *)xm->p_global_read_view;
	tx->psn = 0;
	rlist_create(&tx->on_destroy);
	xm->stat.active++;
}

void
vy_tx_destroy(struct vy_tx *tx)
{
	trigger_run(&tx->on_destroy, NULL);
	trigger_destroy(&tx->on_destroy);

	tx_manager_destroy_read_view(tx->xm, tx->read_view);

	/* Remove from the conflict manager index */
	struct txv *v, *tmp;
	stailq_foreach_entry_safe(v, tmp, &tx->log, next_in_log) {
		if (v->is_read)
			read_set_remove(&tx->xm->read_set, v);
		else
			vy_stmt_counter_unacct_tuple(&v->index->stat.txw.count,
						     v->stmt);
		txv_delete(v);
	}

	tx->xm->stat.active--;
}

/** Return true if the transaction is read-only. */
static bool
vy_tx_is_ro(struct vy_tx *tx)
{
	return tx->write_set.rbt_root == &tx->write_set.rbt_nil;
}

/** Return true if the transaction is in read view. */
static bool
vy_tx_is_in_read_view(struct vy_tx *tx)
{
	return tx->read_view->vlsn != INT64_MAX;
}

/**
 * Send to read view all transactions that are reading key @v
 * modified by transaction @tx.
 */
static int
vy_tx_send_to_read_view(struct vy_tx *tx, struct txv *v)
{
	read_set_t *tree = &tx->xm->read_set;
	struct read_set_key key;
	key.index = v->index;
	key.stmt = v->stmt;
	key.tx = NULL;
	/** Find the first value equal to or greater than key */
	for (struct txv *abort = read_set_nsearch(tree, &key);
	     abort != NULL && abort->index == v->index;
	     abort = read_set_next(tree, abort)) {
		/* Check if we're still looking at the matching key. */
		if (vy_stmt_compare(key.stmt, abort->stmt,
				    v->index->key_def) != 0)
			break;
		/* Don't abort self. */
		if (abort->tx == tx)
			continue;
		/* Abort only active TXs */
		if (abort->tx->state != VINYL_TX_READY)
			continue;
		/* Delete of nothing does not cause a conflict */
		if (abort->is_gap && vy_stmt_type(v->stmt) == IPROTO_DELETE)
			continue;
		/* already in (earlier) read view */
		if (vy_tx_is_in_read_view(abort->tx))
			continue;

		struct vy_read_view *rv = tx_manager_read_view(tx->xm);
		if (rv == NULL)
			return -1;
		abort->tx->read_view = rv;
	}
	return 0;
}

/**
 * Abort all transaction that are reading key @v modified
 * by transaction @tx.
 */
static void
vy_tx_abort_readers(struct vy_tx *tx, struct txv *v)
{
	read_set_t *tree = &tx->xm->read_set;
	struct read_set_key key;
	key.index = v->index;
	key.stmt = v->stmt;
	key.tx = NULL;
	/** Find the first value equal to or greater than key. */
	for (struct txv *abort = read_set_nsearch(tree, &key);
	     abort != NULL && abort->index == v->index;
	     abort = read_set_next(tree, abort)) {
		/* Check if we're still looking at the matching key. */
		if (vy_stmt_compare(key.stmt, abort->stmt,
				    v->index->key_def) != 0)
			break;
		/* Don't abort self. */
		if (abort->tx == tx)
			continue;
		/* Abort only active TXs */
		if (abort->tx->state != VINYL_TX_READY)
			continue;
		/* Delete of nothing does not cause a conflict. */
		if (abort->is_gap && vy_stmt_type(v->stmt) == IPROTO_DELETE)
			continue;
		abort->tx->state = VINYL_TX_ABORT;
	}
}

struct vy_tx *
vy_tx_begin(struct tx_manager *xm)
{
	struct vy_tx *tx = mempool_alloc(&xm->tx_mempool);
	if (unlikely(tx == NULL)) {
		diag_set(OutOfMemory, sizeof(*tx), "mempool", "struct vy_tx");
		return NULL;
	}
	vy_tx_create(xm, tx);
	return tx;
}

/**
 * Rotate the active in-memory tree if necessary and pin it to make
 * sure it is not dumped until the transaction is complete.
 */
static int
vy_tx_write_prepare(struct txv *v)
{
	struct vy_index *index = v->index;

	/*
	 * Allocate a new in-memory tree if either of the following
	 * conditions is true:
	 *
	 * - Generation has increased after the tree was created.
	 *   In this case we need to dump the tree as is in order to
	 *   guarantee dump consistency.
	 *
	 * - Schema version has increased after the tree was created.
	 *   We have to seal the tree, because we don't support mixing
	 *   statements of different formats in the same tree.
	 */
	if (unlikely(index->mem->schema_version != schema_version ||
		     index->mem->generation != *index->env->p_generation)) {
		if (vy_index_rotate_mem(index) != 0)
			return -1;
	}
	vy_mem_pin(index->mem);
	v->mem = index->mem;
	return 0;
}

/**
 * Write a single statement into an index. If the statement has
 * an lsregion copy then use it, else create it.
 *
 * @param index       Index to write to.
 * @param mem         In-memory tree to write to.
 * @param stmt        Statement allocated with malloc().
 * @param region_stmt NULL or the same statement as stmt,
 *                    but allocated on lsregion.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static int
vy_tx_write(struct vy_index *index, struct vy_mem *mem,
	    struct tuple *stmt, const struct tuple **region_stmt)
{
	assert(vy_stmt_is_refable(stmt));
	assert(*region_stmt == NULL || !vy_stmt_is_refable(*region_stmt));

	/*
	 * The UPSERT statement can be applied to the cached
	 * statement, because the cache always contains only
	 * newest REPLACE statements. In such a case the UPSERT,
	 * applied to the cached statement, can be inserted
	 * instead of the original UPSERT.
	 */
	if (vy_stmt_type(stmt) == IPROTO_UPSERT) {
		struct tuple *deleted = NULL;
		/* Invalidate cache element. */
		vy_cache_on_write(&index->cache, stmt, &deleted);
		if (deleted != NULL) {
			struct tuple *applied =
				vy_apply_upsert(stmt, deleted, mem->key_def,
						mem->format, mem->upsert_format,
						false);
			tuple_unref(deleted);
			if (applied != NULL) {
				assert(vy_stmt_type(applied) == IPROTO_REPLACE);
				int rc = vy_index_set(index, mem, applied,
						      region_stmt);
				tuple_unref(applied);
				return rc;
			}
			/*
			 * Ignore a memory error, because it is
			 * not critical to apply the optimization.
			 */
		}
	} else {
		/* Invalidate cache element. */
		vy_cache_on_write(&index->cache, stmt, NULL);
	}
	return vy_index_set(index, mem, stmt, region_stmt);
}

int
vy_tx_prepare(struct vy_tx *tx)
{
	struct tx_manager *xm = tx->xm;

	if (vy_tx_is_ro(tx)) {
		assert(tx->state == VINYL_TX_READY);
		tx->state = VINYL_TX_COMMIT;
		return 0;
	}

	if (vy_tx_is_in_read_view(tx) || tx->state == VINYL_TX_ABORT) {
		xm->stat.conflict++;
		diag_set(ClientError, ER_TRANSACTION_CONFLICT);
		return -1;
	}

	assert(tx->state == VINYL_TX_READY);
	tx->state = VINYL_TX_COMMIT;

	assert(tx->read_view == &xm->global_read_view);
	tx->psn = ++xm->psn;

	/** Send to read view read/write intersection. */
	for (struct txv *v = write_set_first(&tx->write_set);
	     v != NULL; v = write_set_next(&tx->write_set, v)) {
		if (vy_tx_send_to_read_view(tx, v))
			return -1;
	}

	/*
	 * Flush transactional changes to the index.
	 * Sic: the loop below must not yield after recovery.
	 */
	/* repsert - REPLACE/UPSERT */
	const struct tuple *delete = NULL, *repsert = NULL;
	MAYBE_UNUSED uint32_t current_space_id = 0;
	struct txv *v;
	stailq_foreach_entry(v, &tx->log, next_in_log) {
		/* Save only writes. */
		if (v->is_read)
			continue;

		struct vy_index *index = v->index;
		if (index->id == 0) {
			/* The beginning of the new txn_stmt is met. */
			current_space_id = index->space_id;
			repsert = NULL;
			delete = NULL;
		}
		assert(index->space_id == current_space_id);

		/* Do not save statements that was overwritten by the same tx */
		if (v->is_overwritten)
			continue;

		if (vy_tx_write_prepare(v) != 0)
			return -1;
		assert(v->mem != NULL);

		/* In secondary indexes only REPLACE/DELETE can be written. */
		vy_stmt_set_lsn(v->stmt, MAX_LSN + tx->psn);
		enum iproto_type type = vy_stmt_type(v->stmt);
		const struct tuple **region_stmt =
			(type == IPROTO_DELETE) ? &delete : &repsert;
		if (vy_tx_write(index, v->mem, v->stmt, region_stmt) != 0)
			return -1;
		v->region_stmt = *region_stmt;
	}
	xm->last_prepared_tx = tx;
	return 0;
}

void
vy_tx_commit(struct vy_tx *tx, int64_t lsn)
{
	assert(tx->state == VINYL_TX_COMMIT);
	struct tx_manager *xm = tx->xm;

	xm->stat.commit++;

	if (xm->last_prepared_tx == tx)
		xm->last_prepared_tx = NULL;

	if (vy_tx_is_ro(tx))
		goto out;

	assert(xm->lsn < lsn);
	xm->lsn = lsn;

	/* Fix LSNs of the records and commit changes. */
	struct txv *v;
	stailq_foreach_entry(v, &tx->log, next_in_log) {
		if (v->region_stmt != NULL) {
			vy_stmt_set_lsn((struct tuple *)v->region_stmt, lsn);
			vy_index_commit_stmt(v->index, v->mem, v->region_stmt);
		}
		if (v->mem != NULL)
			vy_mem_unpin(v->mem);
	}

	/* Update read views of dependant transactions. */
	if (tx->read_view != &xm->global_read_view)
		tx->read_view->vlsn = lsn;
out:
	vy_tx_destroy(tx);
	mempool_free(&xm->tx_mempool, tx);
}

static void
vy_tx_rollback_after_prepare(struct vy_tx *tx)
{
	assert(tx->state == VINYL_TX_COMMIT);

	struct tx_manager *xm = tx->xm;

	/*
	 * There are two reasons of rollback_after_prepare:
	 * 1) Fail in the middle of vy_tx_prepare call.
	 * 2) Cascading rollback after WAL fail.
	 *
	 * If a TX is the latest prepared TX and the it is rollbacked,
	 * it's certainly the case (2) and we should set xm->last_prepared_tx
	 * to the previous prepared TX, if any.
	 * But doesn't know the previous TX.
	 * On the other hand we may expect that cascading rollback will
	 * concern all the prepared TXs, all of them will be rollbacked
	 * and xm->last_prepared_tx must be set to NULL in the end.
	 * Thus we can set xm->last_prepared_tx to NULL now and it will be
	 * correct in the end of the cascading rollback.
	 *
	 * We must not change xm->last_prepared_tx in all other cases,
	 * it will be changed by the corresponding TX.
	 */
	if (xm->last_prepared_tx == tx)
		xm->last_prepared_tx = NULL;

	struct txv *v;
	stailq_foreach_entry(v, &tx->log, next_in_log) {
		if (v->region_stmt != NULL)
			vy_index_rollback_stmt(v->index, v->mem,
					       v->region_stmt);
		if (v->mem != NULL)
			vy_mem_unpin(v->mem);
	}

	/* Abort read views of dependent transactions. */
	if (tx->read_view != &xm->global_read_view)
		tx->read_view->is_aborted = true;

	for (v = write_set_first(&tx->write_set); v != NULL;
	     v = write_set_next(&tx->write_set, v)) {
		vy_tx_abort_readers(tx, v);
	}
}

void
vy_tx_rollback(struct vy_tx *tx)
{
	struct tx_manager *xm = tx->xm;

	xm->stat.rollback++;

	if (tx->state == VINYL_TX_COMMIT)
		vy_tx_rollback_after_prepare(tx);

	vy_tx_destroy(tx);
	mempool_free(&xm->tx_mempool, tx);
}

void
vy_tx_rollback_to_savepoint(struct vy_tx *tx, void *svp)
{
	assert(tx->state == VINYL_TX_READY);
	struct stailq_entry *last = svp;
	/* Start from the first statement after the savepoint. */
	last = last == NULL ? stailq_first(&tx->log) : stailq_next(last);
	if (last == NULL) {
		/* Empty transaction or no changes after the savepoint. */
		return;
	}
	struct stailq tail;
	stailq_create(&tail);
	stailq_splice(&tx->log, last, &tail);
	/* Rollback statements in LIFO order. */
	stailq_reverse(&tail);
	struct txv *v, *tmp;
	stailq_foreach_entry_safe(v, tmp, &tail, next_in_log) {
		/* Remove from the conflict manager index */
		if (v->is_read)
			read_set_remove(&tx->xm->read_set, v);
		/* Remove from the transaction write log. */
		if (!v->is_read) {
			write_set_remove(&tx->write_set, v);
			if (v->overwritten != NULL) {
				/* Restore overwritten statement. */
				write_set_insert(&tx->write_set, v->overwritten);
				v->overwritten->is_overwritten = false;
			}
			tx->write_set_version++;
		}
		txv_delete(v);
	}
}

int
vy_tx_track(struct vy_tx *tx, struct vy_index *index,
	    struct tuple *key, bool is_gap)
{
	if (vy_tx_is_in_read_view(tx)) {
		/* No point in tracking reads. */
		return 0;
	}
	uint32_t part_count = tuple_field_count(key);
	if (part_count >= index->key_def->part_count) {
		struct txv *v = write_set_search_key(&tx->write_set, index, key);
		if (v != NULL && (vy_stmt_type(v->stmt) == IPROTO_REPLACE ||
				  vy_stmt_type(v->stmt) == IPROTO_DELETE)) {
			/* Reading from own write set is serializable. */
			return 0;
		}
	}
	struct txv *v = read_set_search_key(&tx->xm->read_set, index, key, tx);
	if (v == NULL) {
		v = txv_new(tx, index, key);
		if (v == NULL)
			return -1;
		v->is_read = true;
		v->is_gap = is_gap;
		stailq_add_tail_entry(&tx->log, v, next_in_log);
		read_set_insert(&tx->xm->read_set, v);
	}
	return 0;
}

int
vy_tx_set(struct vy_tx *tx, struct vy_index *index, struct tuple *stmt)
{
	assert(vy_stmt_type(stmt) != 0);
	/**
	 * A statement in write set must have and unique lsn
	 * in order to differ it from cachable statements in mem and run.
	 */
	vy_stmt_set_lsn(stmt, INT64_MAX);
	struct tuple *applied = NULL;

	/* Update concurrent index */
	struct txv *old = write_set_search_key(&tx->write_set, index, stmt);
	/* Found a match of the previous action of this transaction */
	if (old != NULL && vy_stmt_type(stmt) == IPROTO_UPSERT) {
		assert(index->id == 0);
		uint8_t old_type = vy_stmt_type(old->stmt);
		assert(old_type == IPROTO_UPSERT ||
		       old_type == IPROTO_REPLACE ||
		       old_type == IPROTO_DELETE);
		(void) old_type;

		applied = vy_apply_upsert(stmt, old->stmt, index->key_def,
					  index->space_format,
					  index->upsert_format, true);
		index->stat.upsert.applied++;
		if (applied == NULL)
			return -1;
		stmt = applied;
		assert(vy_stmt_type(stmt) != 0);
		index->stat.upsert.squashed++;
	}

	/* Allocate a MVCC container. */
	struct txv *v = txv_new(tx, index, stmt);
	if (applied != NULL)
		tuple_unref(applied);
	if (v == NULL)
		return -1;

	if (old != NULL) {
		/* Leave the old txv in TX log but remove it from write set */
		assert(tx->write_size >= tuple_size(old->stmt));
		tx->write_count--;
		tx->write_size -= tuple_size(old->stmt);
		write_set_remove(&tx->write_set, old);
		old->is_overwritten = true;
	}

	v->is_read = false;
	v->is_gap = false;
	v->overwritten = old;
	write_set_insert(&tx->write_set, v);
	tx->write_set_version++;
	tx->write_count++;
	tx->write_size += tuple_size(stmt);
	vy_stmt_counter_acct_tuple(&index->stat.txw.count, stmt);
	stailq_add_tail_entry(&tx->log, v, next_in_log);
	return 0;
}

static struct vy_stmt_iterator_iface vy_txw_iterator_iface;

void
vy_txw_iterator_open(struct vy_txw_iterator *itr,
		     struct vy_txw_iterator_stat *stat,
		     struct vy_tx *tx, struct vy_index *index,
		     enum iterator_type iterator_type,
		     const struct tuple *key)
{
	if (tuple_field_count(key) == 0) {
		/* Change iterator_type for simplicity. */
		iterator_type = (iterator_type == ITER_LT ||
				 iterator_type == ITER_LE ?
				 ITER_LE : ITER_GE);
	}

	itr->base.iface = &vy_txw_iterator_iface;
	itr->stat = stat;
	itr->tx = tx;
	itr->index = index;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->version = UINT32_MAX;
	itr->curr_txv = NULL;
	itr->search_started = false;
}

static void
vy_txw_iterator_get(struct vy_txw_iterator *itr, struct tuple **ret)
{
	*ret = itr->curr_txv->stmt;
	vy_stmt_counter_acct_tuple(&itr->stat->get, *ret);
}

/** Start iteration over the transaction write set. */
static void
vy_txw_iterator_start(struct vy_txw_iterator *itr, struct tuple **ret)
{
	itr->stat->lookup++;
	*ret = NULL;
	itr->search_started = true;
	itr->version = itr->tx->write_set_version;
	itr->curr_txv = NULL;
	struct vy_index *index = itr->index;
	struct write_set_key key = { index, itr->key };
	struct txv *txv;
	if (tuple_field_count(itr->key) > 0) {
		if (itr->iterator_type == ITER_EQ)
			txv = write_set_search(&itr->tx->write_set, &key);
		else if (itr->iterator_type == ITER_GE ||
			 itr->iterator_type == ITER_GT)
			txv = write_set_nsearch(&itr->tx->write_set, &key);
		else
			txv = write_set_psearch(&itr->tx->write_set, &key);
		if (txv == NULL || txv->index != index)
			return;
		if (vy_stmt_compare(itr->key, txv->stmt, index->key_def) == 0) {
			while (true) {
				struct txv *next;
				if (itr->iterator_type == ITER_LE ||
				    itr->iterator_type == ITER_GT)
					next = write_set_next(&itr->tx->write_set, txv);
				else
					next = write_set_prev(&itr->tx->write_set, txv);
				if (next == NULL || next->index != index)
					break;
				if (vy_stmt_compare(itr->key, next->stmt,
						    index->key_def) != 0)
					break;
				txv = next;
			}
			if (itr->iterator_type == ITER_GT)
				txv = write_set_next(&itr->tx->write_set, txv);
			else if (itr->iterator_type == ITER_LT)
				txv = write_set_prev(&itr->tx->write_set, txv);
		}
	} else if (itr->iterator_type == ITER_LE) {
		txv = write_set_nsearch(&itr->tx->write_set, &key);
	} else {
		assert(itr->iterator_type == ITER_GE);
		txv = write_set_psearch(&itr->tx->write_set, &key);
	}
	if (txv == NULL || txv->index != index)
		return;
	itr->curr_txv = txv;
	vy_txw_iterator_get(itr, ret);
}

/**
 * Advance an iterator to the next statement.
 * Always returns 0. On EOF, *ret is set to NULL.
 */
static NODISCARD int
vy_txw_iterator_next_key(struct vy_stmt_iterator *vitr, struct tuple **ret,
			 bool *stop)
{
	(void)stop;
	assert(vitr->iface->next_key == vy_txw_iterator_next_key);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;
	*ret = NULL;

	if (!itr->search_started) {
		vy_txw_iterator_start(itr, ret);
		return 0;
	}
	itr->version = itr->tx->write_set_version;
	if (itr->curr_txv == NULL)
		return 0;
	if (itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT)
		itr->curr_txv = write_set_prev(&itr->tx->write_set, itr->curr_txv);
	else
		itr->curr_txv = write_set_next(&itr->tx->write_set, itr->curr_txv);
	if (itr->curr_txv != NULL && itr->curr_txv->index != itr->index)
		itr->curr_txv = NULL;
	if (itr->curr_txv != NULL && itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(itr->key, itr->curr_txv->stmt,
			    itr->index->key_def) != 0)
		itr->curr_txv = NULL;
	if (itr->curr_txv != NULL)
		vy_txw_iterator_get(itr, ret);
	return 0;
}

/**
 * This function does nothing. It is only needed to conform
 * to the common iterator interface.
 */
static NODISCARD int
vy_txw_iterator_next_lsn(struct vy_stmt_iterator *vitr, struct tuple **ret)
{
	assert(vitr->iface->next_lsn == vy_txw_iterator_next_lsn);
	(void)vitr;
	*ret = NULL;
	return 0;
}

/**
 * Restore the iterator position after a change in the write set.
 * Iterator is positioned to the statement following @last_stmt.
 * Can restore an iterator that was out of data previously.
 * Returns 1 if the iterator position changed, 0 otherwise.
  */
static NODISCARD int
vy_txw_iterator_restore(struct vy_stmt_iterator *vitr,
			const struct tuple *last_stmt, struct tuple **ret,
			bool *stop)
{
	(void)stop;
	assert(vitr->iface->restore == vy_txw_iterator_restore);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;
	*ret = NULL;

	if (!itr->search_started && last_stmt == NULL) {
		vy_txw_iterator_start(itr, ret);
		return 0;
	}
	if (last_stmt == NULL || itr->version == itr->tx->write_set_version) {
		if (itr->curr_txv)
			vy_txw_iterator_get(itr, ret);
		return 0;
	}

	itr->search_started = true;
	itr->version = itr->tx->write_set_version;
	struct write_set_key key = { itr->index, last_stmt };
	struct tuple *was_stmt = itr->curr_txv != NULL ?
				 itr->curr_txv->stmt : NULL;
	itr->curr_txv = NULL;
	struct txv *txv;
	struct key_def *def = itr->index->key_def;
	if (itr->iterator_type == ITER_LE || itr->iterator_type == ITER_LT)
		txv = write_set_psearch(&itr->tx->write_set, &key);
	else
		txv = write_set_nsearch(&itr->tx->write_set, &key);
	if (txv != NULL && txv->index == itr->index &&
	    vy_tuple_compare(txv->stmt, last_stmt, def) == 0) {
		if (itr->iterator_type == ITER_LE ||
		    itr->iterator_type == ITER_LT)
			txv = write_set_prev(&itr->tx->write_set, txv);
		else
			txv = write_set_next(&itr->tx->write_set, txv);
	}
	if (txv != NULL && txv->index == itr->index &&
	    itr->iterator_type == ITER_EQ &&
	    vy_stmt_compare(itr->key, txv->stmt, def) != 0)
		txv = NULL;
	if (txv == NULL || txv->index != itr->index) {
		assert(was_stmt == NULL);
		return 0;
	}
	itr->curr_txv = txv;
	vy_txw_iterator_get(itr, ret);
	return txv->stmt != was_stmt;
}

/**
 * Close a txw iterator.
 */
static void
vy_txw_iterator_close(struct vy_stmt_iterator *vitr)
{
	assert(vitr->iface->close == vy_txw_iterator_close);
	struct vy_txw_iterator *itr = (struct vy_txw_iterator *) vitr;
	(void)itr; /* suppress warn if NDEBUG */
	TRASH(itr);
}

static struct vy_stmt_iterator_iface vy_txw_iterator_iface = {
	.next_key = vy_txw_iterator_next_key,
	.next_lsn = vy_txw_iterator_next_lsn,
	.restore = vy_txw_iterator_restore,
	.cleanup = NULL,
	.close = vy_txw_iterator_close
};
