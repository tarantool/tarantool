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
#include "vy_read_iterator.h"
#include "vy_run.h"
#include "vy_mem.h"
#include "vy_cache.h"
#include "vy_tx.h"
#include "fiber.h"
#include "vy_history.h"
#include "vy_lsm.h"
#include "vy_stat.h"

/**
 * Merge source, support structure for vy_read_iterator.
 * Contains source iterator and merge state.
 */
struct vy_read_src {
	/** Source iterator. */
	union {
		struct vy_run_iterator run_iterator;
		struct vy_mem_iterator mem_iterator;
		struct vy_txw_iterator txw_iterator;
		struct vy_cache_iterator cache_iterator;
	};
	/** Set if the iterator was started. */
	bool is_started;
	/** See vy_read_iterator->front_id. */
	uint32_t front_id;
	/** History of the key the iterator is positioned at. */
	struct vy_history history;
};

/**
 * Extend internal source array capacity to fit capacity sources.
 * Not necessary to call is but calling it allows to optimize internal memory
 * allocation
 */
static NODISCARD int
vy_read_iterator_reserve(struct vy_read_iterator *itr, uint32_t capacity)
{
	if (itr->src_capacity >= capacity)
		return 0;
	struct vy_read_src *new_src = calloc(capacity, sizeof(*new_src));
	if (new_src == NULL) {
		diag_set(OutOfMemory, capacity * sizeof(*new_src),
			 "calloc", "new_src");
		return -1;
	}
	memcpy(new_src, itr->src, itr->src_count * sizeof(*new_src));
	for (uint32_t i = 0; i < itr->src_count; i++) {
		vy_history_create(&new_src[i].history,
				  &itr->lsm->env->history_node_pool);
		vy_history_splice(&new_src[i].history, &itr->src[i].history);
	}
	free(itr->src);
	itr->src = new_src;
	itr->src_capacity = capacity;
	return 0;
}

/**
 * Add another source to read iterator. Must be called before actual
 * iteration start and must not be called after.
 */
static struct vy_read_src *
vy_read_iterator_add_src(struct vy_read_iterator *itr)
{
	if (itr->src_count == itr->src_capacity) {
		if (vy_read_iterator_reserve(itr, itr->src_count + 1) != 0)
			return NULL;
	}
	struct vy_read_src *src = &itr->src[itr->src_count++];
	memset(src, 0, sizeof(*src));
	vy_history_create(&src->history, &itr->lsm->env->history_node_pool);
	return src;
}

/**
 * Pin all slices open by the read iterator.
 * Used to make sure no run slice is invalidated by
 * compaction while we are fetching data from disk.
 */
static void
vy_read_iterator_pin_slices(struct vy_read_iterator *itr)
{
	for (uint32_t i = itr->disk_src; i < itr->src_count; i++) {
		struct vy_read_src *src = &itr->src[i];
		vy_slice_pin(src->run_iterator.slice);
	}
}

/**
 * Unpin all slices open by the read iterator.
 * See also: vy_read_iterator_pin_slices().
 */
static void
vy_read_iterator_unpin_slices(struct vy_read_iterator *itr)
{
	for (uint32_t i = itr->disk_src; i < itr->src_count; i++) {
		struct vy_read_src *src = &itr->src[i];
		vy_slice_unpin(src->run_iterator.slice);
	}
}

/**
 * Return true if the current candidate for the next key is outside
 * the current range and hence we should move to the next range.
 *
 * If we are looking for a match (EQ, REQ) and the search key
 * doesn't intersect with the current range's boundary, the next
 * range can't contain statements matching the search criteria
 * and hence there's no point in iterating to it.
 */
static bool
vy_read_iterator_range_is_done(struct vy_read_iterator *itr,
			       struct vy_entry next)
{
	struct vy_range *range = itr->curr_range;
	struct key_def *cmp_def = itr->lsm->cmp_def;
	int dir = iterator_direction(itr->iterator_type);

	if (dir > 0 && range->end.stmt != NULL &&
	    (next.stmt == NULL || vy_entry_compare(next, range->end,
						   cmp_def) >= 0) &&
	    (itr->iterator_type != ITER_EQ ||
	     vy_entry_compare(itr->key, range->end, cmp_def) >= 0))
		return true;

	if (dir < 0 && range->begin.stmt != NULL &&
	    (next.stmt == NULL || vy_entry_compare(next, range->begin,
						   cmp_def) < 0) &&
	    (itr->iterator_type != ITER_REQ ||
	     vy_entry_compare(itr->key, range->begin, cmp_def) <= 0))
		return true;

	return false;
}

/**
 * Compare two tuples from the read iterator perspective.
 *
 * Returns:
 *  < 0 if statement @a precedes statement @b in the iterator output
 * == 0 if statements @a and @b are at the same position
 *  > 0 if statement @a supersedes statement @b
 *
 * NULL denotes the statement following the last one.
 */
static inline int
vy_read_iterator_cmp_stmt(struct vy_read_iterator *itr,
			  struct vy_entry a, struct vy_entry b)
{
	if (a.stmt == NULL && b.stmt != NULL)
		return 1;
	if (a.stmt != NULL && b.stmt == NULL)
		return -1;
	if (a.stmt == NULL && b.stmt == NULL)
		return 0;
	return iterator_direction(itr->iterator_type) *
		vy_entry_compare(a, b, itr->lsm->cmp_def);
}

/**
 * Return true if the statement matches search criteria
 * and older sources don't need to be scanned.
 */
static bool
vy_read_iterator_is_exact_match(struct vy_read_iterator *itr,
				struct vy_entry entry)
{
	enum iterator_type type = itr->iterator_type;
	struct key_def *cmp_def = itr->lsm->cmp_def;

	/*
	 * If the index is unique and the search key is full,
	 * we can avoid disk accesses on the first iteration
	 * in case the key is found in memory.
	 */
	return itr->last.stmt == NULL && entry.stmt != NULL &&
		(type == ITER_EQ || type == ITER_REQ ||
		 type == ITER_GE || type == ITER_LE) &&
		vy_stmt_is_full_key(itr->key.stmt, cmp_def) &&
		vy_entry_compare(entry, itr->key, cmp_def) == 0;
}

/**
 * Check if the statement at which the given read source
 * is positioned precedes the current candidate for the
 * next key ('next') and update the latter if so.
 * The 'stop' flag is set if the next key is found and
 * older sources don't need to be evaluated.
 */
static void
vy_read_iterator_evaluate_src(struct vy_read_iterator *itr,
			      struct vy_read_src *src,
			      struct vy_entry *next, bool *stop)
{
	uint32_t src_id = src - itr->src;
	struct vy_entry entry = vy_history_last_stmt(&src->history);
	int cmp = vy_read_iterator_cmp_stmt(itr, entry, *next);
	if (cmp < 0) {
		assert(entry.stmt != NULL);
		*next = entry;
		itr->front_id++;
	}
	if (cmp <= 0)
		src->front_id = itr->front_id;

	itr->skipped_src = MAX(itr->skipped_src, src_id + 1);

	if (cmp < 0 && vy_history_is_terminal(&src->history) &&
	    vy_read_iterator_is_exact_match(itr, entry)) {
		itr->skipped_src = src_id + 1;
		*stop = true;
	}
}

/*
 * Each of the functions from the vy_read_iterator_scan_* family
 * is used by vy_read_iterator_advance() to:
 *
 * 1. Update the position of a read source, which implies:
 *
 *    - Starting iteration over the source if it has not been done
 *      yet or restoring the iterator position in case the source
 *      has been modified since the last iteration.
 *
 *    - Advancing the iterator position to the first statement
 *      following the one returned on the previous iteration.
 *      To avoid an extra tuple comparison, we maintain front_id
 *      for each source: all sources with front_id equal to the
 *      front_id of the read iterator were used on the previous
 *      iteration and hence need to be advanced.
 *
 * 2. Update the candidate for the next key ('next') if the
 *    statement at which the source is positioned precedes it.
 *    The 'stop' flag is set if older sources do not need to be
 *    scanned (e.g. because a chain was found in the cache).
 *    See also vy_read_iterator_evaluate_src().
 */

static NODISCARD int
vy_read_iterator_scan_txw(struct vy_read_iterator *itr,
			  struct vy_entry *next, bool *stop)
{
	struct vy_read_src *src = &itr->src[itr->txw_src];
	struct vy_txw_iterator *src_itr = &src->txw_iterator;

	if (itr->tx == NULL)
		return 0;

	assert(itr->txw_src < itr->skipped_src);

	int rc = vy_txw_iterator_restore(src_itr, itr->last, &src->history);
	if (rc == 0) {
		if (!src->is_started) {
			rc = vy_txw_iterator_skip(src_itr, itr->last,
						  &src->history);
		} else if (src->front_id == itr->prev_front_id) {
			rc = vy_txw_iterator_next(src_itr, &src->history);
		}
		src->is_started = true;
	}
	if (rc < 0)
		return -1;

	vy_read_iterator_evaluate_src(itr, src, next, stop);
	return 0;
}

static NODISCARD int
vy_read_iterator_scan_cache(struct vy_read_iterator *itr,
			    struct vy_entry *next, bool *stop)
{
	bool is_interval = false;
	struct vy_read_src *src = &itr->src[itr->cache_src];
	struct vy_cache_iterator *src_itr = &src->cache_iterator;

	int rc = vy_cache_iterator_restore(src_itr, itr->last,
					   &src->history, &is_interval);
	if (rc == 0) {
		if (!src->is_started || itr->cache_src >= itr->skipped_src) {
			rc = vy_cache_iterator_skip(src_itr, itr->last,
						&src->history, &is_interval);
		} else if (src->front_id == itr->prev_front_id) {
			rc = vy_cache_iterator_next(src_itr, &src->history,
						    &is_interval);
		}
		src->is_started = true;
	}
	if (rc < 0)
		return -1;

	vy_read_iterator_evaluate_src(itr, src, next, stop);
	if (is_interval) {
		itr->skipped_src = itr->cache_src + 1;
		*stop = true;
	}
	return 0;
}

static NODISCARD int
vy_read_iterator_scan_mem(struct vy_read_iterator *itr, uint32_t mem_src,
			  struct vy_entry *next, bool *stop)
{
	int rc;
	struct vy_read_src *src = &itr->src[mem_src];
	struct vy_mem_iterator *src_itr = &src->mem_iterator;

	assert(mem_src >= itr->mem_src && mem_src < itr->disk_src);

	rc = vy_mem_iterator_restore(src_itr, itr->last, &src->history);
	if (rc == 0) {
		if (!src->is_started || mem_src >= itr->skipped_src) {
			rc = vy_mem_iterator_skip(src_itr, itr->last,
						  &src->history);
		} else if (src->front_id == itr->prev_front_id) {
			rc = vy_mem_iterator_next(src_itr, &src->history);
		}
		src->is_started = true;
	}
	if (rc < 0)
		return -1;

	vy_read_iterator_evaluate_src(itr, src, next, stop);
	return 0;
}

static NODISCARD int
vy_read_iterator_scan_disk(struct vy_read_iterator *itr, uint32_t disk_src,
			   struct vy_entry *next, bool *stop)
{
	int rc = 0;
	struct vy_read_src *src = &itr->src[disk_src];
	struct vy_run_iterator *src_itr = &src->run_iterator;

	assert(disk_src >= itr->disk_src && disk_src < itr->src_count);

	if (!src->is_started || disk_src >= itr->skipped_src)
		rc = vy_run_iterator_skip(src_itr, itr->last,
					  &src->history);
	else if (src->front_id == itr->prev_front_id)
		rc = vy_run_iterator_next(src_itr, &src->history);
	src->is_started = true;

	if (rc < 0)
		return -1;

	vy_read_iterator_evaluate_src(itr, src, next, stop);
	return 0;
}

static void
vy_read_iterator_restore(struct vy_read_iterator *itr);

static void
vy_read_iterator_next_range(struct vy_read_iterator *itr);

/**
 * Advance the iterator to the next key.
 * Returns 0 on success, -1 on error.
 */
static NODISCARD int
vy_read_iterator_advance(struct vy_read_iterator *itr)
{
	if (itr->last.stmt != NULL && (itr->iterator_type == ITER_EQ ||
				       itr->iterator_type == ITER_REQ) &&
	    vy_stmt_is_full_key(itr->key.stmt, itr->lsm->cmp_def)) {
		/*
		 * There may be one statement at max satisfying
		 * EQ with a full key.
		 */
		itr->front_id++;
		return 0;
	}
	/*
	 * Restore the iterator position if the LSM tree has changed
	 * since the last iteration or this is the first iteration.
	 */
	if (itr->last.stmt == NULL ||
	    itr->mem_list_version != itr->lsm->mem_list_version ||
	    itr->range_tree_version != itr->lsm->range_tree_version ||
	    itr->range_version != itr->curr_range->version) {
		vy_read_iterator_restore(itr);
	}
restart:
	itr->prev_front_id = itr->front_id;
	itr->front_id++;

	/*
	 * Look up the next key in read sources starting
	 * from the one that stores newest data.
	 */
	bool stop = false;
	struct vy_entry next = vy_entry_none();
	if (vy_read_iterator_scan_txw(itr, &next, &stop) != 0)
		return -1;
	if (stop)
		goto done;
	if (vy_read_iterator_scan_cache(itr, &next, &stop) != 0)
		return -1;
	if (stop)
		goto done;

	for (uint32_t i = itr->mem_src; i < itr->disk_src; i++) {
		if (vy_read_iterator_scan_mem(itr, i, &next, &stop) != 0)
			return -1;
		if (stop)
			goto done;
	}
rescan_disk:
	/* The following code may yield as it needs to access disk. */
	vy_read_iterator_pin_slices(itr);
	for (uint32_t i = itr->disk_src; i < itr->src_count; i++) {
		if (vy_read_iterator_scan_disk(itr, i, &next, &stop) != 0) {
			vy_read_iterator_unpin_slices(itr);
			return -1;
		}
		if (stop)
			break;
	}
	vy_read_iterator_unpin_slices(itr);
	/*
	 * The transaction could have been aborted while we were
	 * reading disk. We must stop now and return an error as
	 * this function could be called by a DML request that
	 * was aborted by a DDL operation: failing will prevent
	 * it from dereferencing a destroyed space.
	 */
	if (itr->tx != NULL && itr->tx->state == VINYL_TX_ABORT) {
		diag_set(ClientError, ER_TRANSACTION_CONFLICT);
		return -1;
	}
	/*
	 * The list of in-memory indexes and/or the range tree could
	 * have been modified by dump/compaction while we were fetching
	 * data from disk. Restart the iterator if this is the case.
	 * Note, we don't need to check the current range's version,
	 * because all slices were pinned and hence could not be
	 * removed.
	 */
	if (itr->mem_list_version != itr->lsm->mem_list_version ||
	    itr->range_tree_version != itr->lsm->range_tree_version) {
		vy_read_iterator_restore(itr);
		goto restart;
	}
	/*
	 * The transaction write set couldn't change during the yield
	 * as it is owned exclusively by the current fiber so the only
	 * source to check is the active in-memory tree.
	 */
	struct vy_mem_iterator *mem_itr = &itr->src[itr->mem_src].mem_iterator;
	if (mem_itr->version != mem_itr->mem->version) {
		vy_read_iterator_restore(itr);
		goto restart;
	}
	/*
	 * Scan the next range in case we transgressed the current
	 * range's boundaries.
	 */
	if (vy_read_iterator_range_is_done(itr, next)) {
		vy_read_iterator_next_range(itr);
		goto rescan_disk;
	}
done:
#ifndef NDEBUG
	/* Check that the statement meets search criteria. */
	if (next.stmt != NULL) {
		int cmp = vy_entry_compare(next, itr->key, itr->lsm->cmp_def);
		cmp *= iterator_direction(itr->iterator_type);
		if (itr->iterator_type == ITER_GT ||
		    itr->iterator_type == ITER_LT)
			assert(cmp > 0);
		else
			assert(cmp >= 0);
	}
	/*
	 * Ensure the read iterator does not return duplicates
	 * and respects statement order.
	 */
	if (itr->last.stmt != NULL && next.stmt != NULL) {
	       assert(vy_read_iterator_cmp_stmt(itr, next, itr->last) > 0);
	}
#endif
	if (itr->need_check_eq && next.stmt != NULL &&
	    vy_entry_compare(next, itr->key, itr->lsm->cmp_def) != 0)
		itr->front_id++;
	return 0;
}

static void
vy_read_iterator_add_tx(struct vy_read_iterator *itr)
{
	assert(itr->tx != NULL);
	enum iterator_type iterator_type = (itr->iterator_type != ITER_REQ ?
					    itr->iterator_type : ITER_LE);
	struct vy_txw_iterator_stat *stat = &itr->lsm->stat.txw.iterator;
	struct vy_read_src *sub_src = vy_read_iterator_add_src(itr);
	vy_txw_iterator_open(&sub_src->txw_iterator, stat, itr->tx, itr->lsm,
			     iterator_type, itr->key);
}

static void
vy_read_iterator_add_cache(struct vy_read_iterator *itr)
{
	enum iterator_type iterator_type = (itr->iterator_type != ITER_REQ ?
					    itr->iterator_type : ITER_LE);
	struct vy_read_src *sub_src = vy_read_iterator_add_src(itr);
	vy_cache_iterator_open(&sub_src->cache_iterator,
			       &itr->lsm->cache, iterator_type,
			       itr->key, itr->read_view);
}

static void
vy_read_iterator_add_mem(struct vy_read_iterator *itr)
{
	enum iterator_type iterator_type = (itr->iterator_type != ITER_REQ ?
					    itr->iterator_type : ITER_LE);
	struct vy_lsm *lsm = itr->lsm;
	struct vy_read_src *sub_src;

	/* Add the active in-memory index. */
	assert(lsm->mem != NULL);
	sub_src = vy_read_iterator_add_src(itr);
	vy_mem_iterator_open(&sub_src->mem_iterator, &lsm->stat.memory.iterator,
			     lsm->mem, iterator_type, itr->key, itr->read_view);
	/* Add sealed in-memory indexes. */
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &lsm->sealed, in_sealed) {
		sub_src = vy_read_iterator_add_src(itr);
		vy_mem_iterator_open(&sub_src->mem_iterator,
				     &lsm->stat.memory.iterator,
				     mem, iterator_type, itr->key,
				     itr->read_view);
	}
}

static void
vy_read_iterator_add_disk(struct vy_read_iterator *itr)
{
	assert(itr->curr_range != NULL);
	enum iterator_type iterator_type = (itr->iterator_type != ITER_REQ ?
					    itr->iterator_type : ITER_LE);
	struct vy_lsm *lsm = itr->lsm;
	struct vy_slice *slice;
	/*
	 * The format of the statement must be exactly the space
	 * format with the same identifier to fully match the
	 * format in vy_mem.
	 */
	rlist_foreach_entry(slice, &itr->curr_range->slices, in_range) {
		struct vy_read_src *sub_src = vy_read_iterator_add_src(itr);
		vy_run_iterator_open(&sub_src->run_iterator,
				     &lsm->stat.disk.iterator, slice,
				     iterator_type, itr->key,
				     itr->read_view, lsm->cmp_def,
				     lsm->key_def, lsm->disk_format);
	}
}

/**
 * Close all open sources and reset the merge state.
 */
static void
vy_read_iterator_cleanup(struct vy_read_iterator *itr)
{
	uint32_t i;
	struct vy_read_src *src;

	if (itr->txw_src < itr->src_count) {
		src = &itr->src[itr->txw_src];
		vy_history_cleanup(&src->history);
		vy_txw_iterator_close(&src->txw_iterator);
	}
	if (itr->cache_src < itr->src_count) {
		src = &itr->src[itr->cache_src];
		vy_history_cleanup(&src->history);
		vy_cache_iterator_close(&src->cache_iterator);
	}
	for (i = itr->mem_src; i < itr->disk_src; i++) {
		src = &itr->src[i];
		vy_history_cleanup(&src->history);
		vy_mem_iterator_close(&src->mem_iterator);
	}
	for (i = itr->disk_src; i < itr->src_count; i++) {
		src = &itr->src[i];
		vy_history_cleanup(&src->history);
		vy_run_iterator_close(&src->run_iterator);
	}

	itr->txw_src = UINT32_MAX;
	itr->cache_src = UINT32_MAX;
	itr->mem_src = UINT32_MAX;
	itr->disk_src = UINT32_MAX;
	itr->skipped_src = UINT32_MAX;
	itr->src_count = 0;
}

void
vy_read_iterator_open(struct vy_read_iterator *itr, struct vy_lsm *lsm,
		      struct vy_tx *tx, enum iterator_type iterator_type,
		      struct vy_entry key, const struct vy_read_view **rv)
{
	memset(itr, 0, sizeof(*itr));

	itr->lsm = lsm;
	itr->tx = tx;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;
	itr->last = vy_entry_none();
	itr->last_cached = vy_entry_none();

	if (vy_stmt_is_empty_key(key.stmt)) {
		/*
		 * Strictly speaking, a GT/LT iterator should return
		 * nothing if the key is empty, because every key is
		 * equal to the empty key, but historically we return
		 * all keys instead. So use GE/LE instead of GT/LT
		 * in this case.
		 */
		itr->iterator_type = iterator_direction(iterator_type) > 0 ?
				     ITER_GE : ITER_LE;
	}

	if (iterator_type == ITER_ALL)
		itr->iterator_type = ITER_GE;

	if (iterator_type == ITER_REQ) {
		/*
		 * Source iterators cannot handle ITER_REQ and
		 * use ITER_LE instead, so we need to enable EQ
		 * check in this case.
		 *
		 * See vy_read_iterator_add_{tx,cache,mem,run}.
		 */
		itr->need_check_eq = true;
	}

}

/**
 * Restart the read iterator from the position following
 * the last statement returned to the user. Called when
 * the current range or the whole range tree is changed.
 * Also used for preparing the iterator for the first
 * iteration.
 */
static void
vy_read_iterator_restore(struct vy_read_iterator *itr)
{
	vy_read_iterator_cleanup(itr);

	itr->mem_list_version = itr->lsm->mem_list_version;
	itr->range_tree_version = itr->lsm->range_tree_version;
	itr->curr_range = vy_range_tree_find_by_key(&itr->lsm->range_tree,
						    itr->iterator_type,
						    itr->last.stmt != NULL ?
						    itr->last : itr->key);
	itr->range_version = itr->curr_range->version;

	if (itr->tx != NULL) {
		itr->txw_src = itr->src_count;
		vy_read_iterator_add_tx(itr);
	}

	itr->cache_src = itr->src_count;
	vy_read_iterator_add_cache(itr);

	itr->mem_src = itr->src_count;
	vy_read_iterator_add_mem(itr);

	itr->disk_src = itr->src_count;
	vy_read_iterator_add_disk(itr);
}

/**
 * Iterate to the next range.
 */
static void
vy_read_iterator_next_range(struct vy_read_iterator *itr)
{
	struct vy_range *range = itr->curr_range;
	struct key_def *cmp_def = itr->lsm->cmp_def;
	int dir = iterator_direction(itr->iterator_type);

	assert(range != NULL);
	while (true) {
		range = dir > 0 ?
			vy_range_tree_next(&itr->lsm->range_tree, range) :
			vy_range_tree_prev(&itr->lsm->range_tree, range);
		assert(range != NULL);

		if (itr->last.stmt == NULL)
			break;
		/*
		 * We could skip an entire range due to the cache.
		 * Make sure the next statement falls in the range.
		 */
		if (dir > 0 && (range->end.stmt == NULL ||
				vy_entry_compare(itr->last, range->end,
						 cmp_def) < 0))
			break;
		if (dir < 0 && (range->begin.stmt == NULL ||
				vy_entry_compare(itr->last, range->begin,
						 cmp_def) > 0))
			break;
	}
	itr->curr_range = range;
	itr->range_version = range->version;

	for (uint32_t i = itr->disk_src; i < itr->src_count; i++) {
		struct vy_read_src *src = &itr->src[i];
		vy_run_iterator_close(&src->run_iterator);
	}
	itr->src_count = itr->disk_src;

	vy_read_iterator_add_disk(itr);
}

/**
 * Get a resultant statement for the current key.
 * Returns 0 on success, -1 on error.
 */
static NODISCARD int
vy_read_iterator_apply_history(struct vy_read_iterator *itr,
			       struct vy_entry *ret)
{
	struct vy_lsm *lsm = itr->lsm;
	struct vy_history history;
	vy_history_create(&history, &lsm->env->history_node_pool);

	for (uint32_t i = 0; i < itr->src_count; i++) {
		struct vy_read_src *src = &itr->src[i];
		if (src->front_id == itr->front_id) {
			vy_history_splice(&history, &src->history);
			if (vy_history_is_terminal(&history))
				break;
		}
	}

	int upserts_applied = 0;
	int rc = vy_history_apply(&history, lsm->cmp_def,
				  true, &upserts_applied, ret);

	lsm->stat.upsert.applied += upserts_applied;
	vy_history_cleanup(&history);
	return rc;
}

/**
 * Track a read in the conflict manager.
 */
static int
vy_read_iterator_track_read(struct vy_read_iterator *itr, struct vy_entry entry)
{
	if (itr->tx == NULL)
		return 0;

	if (entry.stmt == NULL) {
		entry = (itr->iterator_type == ITER_EQ ||
			 itr->iterator_type == ITER_REQ ?
			 itr->key : itr->lsm->env->empty_key);
	}

	int rc;
	if (iterator_direction(itr->iterator_type) >= 0) {
		rc = vy_tx_track(itr->tx, itr->lsm, itr->key,
				 itr->iterator_type != ITER_GT,
				 entry, true);
	} else {
		rc = vy_tx_track(itr->tx, itr->lsm, entry, true,
				 itr->key, itr->iterator_type != ITER_LT);
	}
	return rc;
}

NODISCARD int
vy_read_iterator_next(struct vy_read_iterator *itr, struct vy_entry *result)
{
	assert(itr->tx == NULL || itr->tx->state == VINYL_TX_READY);

	struct vy_entry entry;
next_key:
	if (vy_read_iterator_advance(itr) != 0)
		return -1;
	if (vy_read_iterator_apply_history(itr, &entry) != 0)
		return -1;
	if (vy_read_iterator_track_read(itr, entry) != 0)
		return -1;

	if (itr->last.stmt != NULL)
		tuple_unref(itr->last.stmt);
	itr->last = entry;

	if (entry.stmt != NULL && vy_stmt_type(entry.stmt) == IPROTO_DELETE) {
		/*
		 * We don't return DELETEs so skip to the next key.
		 * If the DELETE was read from TX write set, there
		 * is a good chance that the space actually has
		 * the deleted key and hence we must not consider
		 * previous + current tuple as an unbroken chain.
		 */
		if (vy_stmt_lsn(entry.stmt) == INT64_MAX) {
			if (itr->last_cached.stmt != NULL)
				tuple_unref(itr->last_cached.stmt);
			itr->last_cached = vy_entry_none();
		}
		goto next_key;
	}
	assert(entry.stmt == NULL ||
	       vy_stmt_type(entry.stmt) == IPROTO_INSERT ||
	       vy_stmt_type(entry.stmt) == IPROTO_REPLACE);

	*result = entry;
	return 0;
}

void
vy_read_iterator_cache_add(struct vy_read_iterator *itr, struct vy_entry entry)
{
	if ((**itr->read_view).vlsn != INT64_MAX) {
		if (itr->last_cached.stmt != NULL)
			tuple_unref(itr->last_cached.stmt);
		itr->last_cached = vy_entry_none();
		return;
	}
	vy_cache_add(&itr->lsm->cache, entry, itr->last_cached,
		     itr->key, itr->iterator_type);
	if (entry.stmt != NULL)
		tuple_ref(entry.stmt);
	if (itr->last_cached.stmt != NULL)
		tuple_unref(itr->last_cached.stmt);
	itr->last_cached = entry;
}

/**
 * Close the iterator and free resources
 */
void
vy_read_iterator_close(struct vy_read_iterator *itr)
{
	if (itr->last.stmt != NULL)
		tuple_unref(itr->last.stmt);
	if (itr->last_cached.stmt != NULL)
		tuple_unref(itr->last_cached.stmt);
	vy_read_iterator_cleanup(itr);
	free(itr->src);
	TRASH(itr);
}
