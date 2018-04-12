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
#include "vy_upsert.h"
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
	/** Statement the iterator is at. */
	struct tuple *stmt;
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
	if (itr->src_count > 0) {
		memcpy(new_src, itr->src, itr->src_count * sizeof(*new_src));
		free(itr->src);
	}
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
	itr->src[itr->src_count].front_id = 0;
	struct vy_read_src *src = &itr->src[itr->src_count++];
	memset(src, 0, sizeof(*src));
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
 * Return true if the current statement is outside the current
 * range and hence we should move to the next range.
 *
 * If we are looking for a match (EQ, REQ) and the search key
 * doesn't intersect with the current range's boundary, the next
 * range can't contain statements matching the search criteria
 * and hence there's no point in iterating to it.
 */
static bool
vy_read_iterator_range_is_done(struct vy_read_iterator *itr)
{
	struct tuple *stmt = itr->curr_stmt;
	struct vy_range *range = itr->curr_range;
	struct key_def *cmp_def = itr->lsm->cmp_def;
	int dir = iterator_direction(itr->iterator_type);

	if (dir > 0 && range->end != NULL &&
	    (stmt == NULL || vy_tuple_compare_with_key(stmt,
				range->end, cmp_def) >= 0) &&
	    (itr->iterator_type != ITER_EQ ||
	     vy_stmt_compare_with_key(itr->key, range->end, cmp_def) >= 0))
		return true;

	if (dir < 0 && range->begin != NULL &&
	    (stmt == NULL || vy_tuple_compare_with_key(stmt,
				range->begin, cmp_def) < 0) &&
	    (itr->iterator_type != ITER_REQ ||
	     vy_stmt_compare_with_key(itr->key, range->begin, cmp_def) <= 0))
		return true;

	return false;
}

/**
 * Compare two tuples from the read iterator perspective.
 *
 * Returns:
 *  -1 if statement @a precedes statement @b in the iterator output
 *   0 if statements @a and @b are at the same position
 *   1 if statement @a supersedes statement @b
 *
 * NULL denotes the statement following the last one.
 */
static inline int
vy_read_iterator_cmp_stmt(struct vy_read_iterator *itr,
			  const struct tuple *a, const struct tuple *b)
{
	if (a == NULL && b != NULL)
		return 1;
	if (a != NULL && b == NULL)
		return -1;
	if (a == NULL && b == NULL)
		return 0;
	return iterator_direction(itr->iterator_type) *
		vy_tuple_compare(a, b, itr->lsm->cmp_def);
}

/**
 * Return true if the statement matches search criteria
 * and older sources don't need to be scanned.
 */
static bool
vy_read_iterator_is_exact_match(struct vy_read_iterator *itr,
				struct tuple *stmt)
{
	struct tuple *key = itr->key;
	enum iterator_type type = itr->iterator_type;
	struct key_def *cmp_def = itr->lsm->cmp_def;

	/*
	 * If the index is unique and the search key is full,
	 * we can avoid disk accesses on the first iteration
	 * in case the key is found in memory.
	 */
	return itr->last_stmt == NULL && stmt != NULL &&
		(type == ITER_EQ || type == ITER_REQ ||
		 type == ITER_GE || type == ITER_LE) &&
		tuple_field_count(key) >= cmp_def->part_count &&
		vy_stmt_compare(stmt, key, cmp_def) == 0;
}

/**
 * Check if the statement at which the given read source
 * is positioned precedes the current candidate for the
 * next key ('curr_stmt') and update the latter if so.
 * The 'stop' flag is set if the next key is found and
 * older sources don't need to be evaluated.
 */
static void
vy_read_iterator_evaluate_src(struct vy_read_iterator *itr,
			      struct vy_read_src *src, bool *stop)
{
	uint32_t src_id = src - itr->src;
	int cmp = vy_read_iterator_cmp_stmt(itr, src->stmt, itr->curr_stmt);
	if (cmp < 0) {
		assert(src->stmt != NULL);
		tuple_ref(src->stmt);
		if (itr->curr_stmt != NULL)
			tuple_unref(itr->curr_stmt);
		itr->curr_stmt = src->stmt;
		itr->curr_src = src_id;
		itr->front_id++;
	}
	if (cmp <= 0)
		src->front_id = itr->front_id;

	itr->skipped_src = MAX(itr->skipped_src, src_id + 1);

	if (cmp < 0 && vy_read_iterator_is_exact_match(itr, src->stmt)) {
		itr->skipped_src = src_id + 1;
		*stop = true;
	}
}

/**
 * Check if a read iterator source is behind the current read
 * iterator position and hence needs to be fast-forwarded.
 */
static inline bool
vy_read_src_is_behind(struct vy_read_iterator *itr, struct vy_read_src *src)
{
	uint32_t src_id = src - itr->src;
	if (!src->is_started)
		return true;
	if (src_id < itr->skipped_src)
		return false;
	if (vy_read_iterator_cmp_stmt(itr, src->stmt, itr->last_stmt) > 0)
		return false;
	return true;
}

/*
 * Each of the functions from the vy_read_iterator_scan_* family
 * is used by vy_read_iterator_next_key() to:
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
 * 2. Update the candidate for the next key ('curr_stmt') if the
 *    statement at which the source is positioned precedes it.
 *    The 'stop' flag is set if older sources do not need to be
 *    scanned (e.g. because a chain was found in the cache).
 *    See also vy_read_iterator_evaluate_src().
 */

static void
vy_read_iterator_scan_txw(struct vy_read_iterator *itr, bool *stop)
{
	struct vy_read_src *src = &itr->src[itr->txw_src];
	struct vy_txw_iterator *src_itr = &src->txw_iterator;

	if (itr->tx == NULL)
		return;

	assert(itr->txw_src < itr->skipped_src);

	int rc = vy_txw_iterator_restore(src_itr, itr->last_stmt, &src->stmt);
	if (rc == 0) {
		if (!src->is_started) {
			vy_txw_iterator_skip(src_itr, itr->last_stmt,
					     &src->stmt);
		} else if (src->front_id == itr->prev_front_id) {
			vy_txw_iterator_next(src_itr, &src->stmt);
		}
		src->is_started = true;
	}
	vy_read_iterator_evaluate_src(itr, src, stop);
}

static void
vy_read_iterator_scan_cache(struct vy_read_iterator *itr, bool *stop)
{
	bool is_interval = false;
	struct vy_read_src *src = &itr->src[itr->cache_src];
	struct vy_cache_iterator *src_itr = &src->cache_iterator;

	int rc = vy_cache_iterator_restore(src_itr, itr->last_stmt,
					   &src->stmt, &is_interval);
	if (rc == 0) {
		if (vy_read_src_is_behind(itr, src)) {
			vy_cache_iterator_skip(src_itr, itr->last_stmt,
					       &src->stmt, &is_interval);
		} else if (src->front_id == itr->prev_front_id) {
			vy_cache_iterator_next(src_itr, &src->stmt,
					       &is_interval);
		}
		src->is_started = true;
	}
	vy_read_iterator_evaluate_src(itr, src, stop);

	if (is_interval) {
		itr->skipped_src = itr->cache_src + 1;
		*stop = true;
	}
}

static NODISCARD int
vy_read_iterator_scan_mem(struct vy_read_iterator *itr,
			  uint32_t mem_src, bool *stop)
{
	int rc;
	struct vy_read_src *src = &itr->src[mem_src];
	struct vy_mem_iterator *src_itr = &src->mem_iterator;

	assert(mem_src >= itr->mem_src && mem_src < itr->disk_src);

	rc = vy_mem_iterator_restore(src_itr, itr->last_stmt, &src->stmt);
	if (rc == 0) {
		if (vy_read_src_is_behind(itr, src)) {
			rc = vy_mem_iterator_skip(src_itr, itr->last_stmt,
						  &src->stmt);
		} else if (src->front_id == itr->prev_front_id) {
			rc = vy_mem_iterator_next_key(src_itr, &src->stmt);
		}
		src->is_started = true;
	}
	if (rc < 0)
		return -1;

	vy_read_iterator_evaluate_src(itr, src, stop);
	return 0;
}

static NODISCARD int
vy_read_iterator_scan_disk(struct vy_read_iterator *itr,
			   uint32_t disk_src, bool *stop)
{
	int rc = 0;
	struct vy_read_src *src = &itr->src[disk_src];
	struct vy_run_iterator *src_itr = &src->run_iterator;

	assert(disk_src >= itr->disk_src && disk_src < itr->src_count);

	if (vy_read_src_is_behind(itr, src))
		rc = vy_run_iterator_skip(src_itr, itr->last_stmt, &src->stmt);
	else if (src->front_id == itr->prev_front_id)
		rc = vy_run_iterator_next_key(src_itr, &src->stmt);
	src->is_started = true;

	if (rc < 0)
		return -1;

	vy_read_iterator_evaluate_src(itr, src, stop);
	return 0;
}

/**
 * Restore the position of the active in-memory tree iterator
 * after a yield caused by a disk read and update 'curr_stmt'
 * if necessary.
 */
static NODISCARD int
vy_read_iterator_restore_mem(struct vy_read_iterator *itr)
{
	int rc;
	int cmp;
	struct vy_read_src *src = &itr->src[itr->mem_src];

	rc = vy_mem_iterator_restore(&src->mem_iterator,
				     itr->last_stmt, &src->stmt);
	if (rc < 0)
		return -1; /* memory allocation error */
	if (rc == 0)
		return 0; /* nothing changed */

	cmp = vy_read_iterator_cmp_stmt(itr, src->stmt, itr->curr_stmt);
	if (cmp > 0) {
		/*
		 * Memory trees are append-only so if the
		 * source is not on top of the heap after
		 * restoration, it was not before.
		 */
		assert(src->front_id < itr->front_id);
		return 0;
	}
	if (cmp < 0 || itr->curr_src != itr->txw_src) {
		/*
		 * The new statement precedes the current
		 * candidate for the next key or it is a
		 * newer version of the same key.
		 */
		tuple_ref(src->stmt);
		if (itr->curr_stmt != NULL)
			tuple_unref(itr->curr_stmt);
		itr->curr_stmt = src->stmt;
		itr->curr_src = itr->mem_src;
	} else {
		/*
		 * Make sure we don't read the old value
		 * from the cache while applying UPSERTs.
		 */
		itr->src[itr->cache_src].front_id = 0;
	}
	if (cmp < 0)
		itr->front_id++;
	src->front_id = itr->front_id;
	return 0;
}

static void
vy_read_iterator_restore(struct vy_read_iterator *itr);

static void
vy_read_iterator_next_range(struct vy_read_iterator *itr);

static int
vy_read_iterator_track_read(struct vy_read_iterator *itr, struct tuple *stmt);

/**
 * Iterate to the next key
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read error
 */
static NODISCARD int
vy_read_iterator_next_key(struct vy_read_iterator *itr, struct tuple **ret)
{
	uint32_t i;
	bool stop = false;

	if (itr->last_stmt != NULL && (itr->iterator_type == ITER_EQ ||
				       itr->iterator_type == ITER_REQ) &&
	    tuple_field_count(itr->key) >= itr->lsm->cmp_def->part_count) {
		/*
		 * There may be one statement at max satisfying
		 * EQ with a full key.
		 */
		*ret = NULL;
		return 0;
	}
	/*
	 * Restore the iterator position if the LSM tree has changed
	 * since the last iteration.
	 */
	if (itr->mem_list_version != itr->lsm->mem_list_version ||
	    itr->range_tree_version != itr->lsm->range_tree_version ||
	    itr->range_version != itr->curr_range->version) {
		vy_read_iterator_restore(itr);
	}
restart:
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = NULL;
	itr->curr_src = UINT32_MAX;
	itr->prev_front_id = itr->front_id;

	/*
	 * Look up the next key in read sources starting
	 * from the one that stores newest data.
	 */
	vy_read_iterator_scan_txw(itr, &stop);
	if (stop)
		goto done;
	vy_read_iterator_scan_cache(itr, &stop);
	if (stop)
		goto done;

	for (i = itr->mem_src; i < itr->disk_src; i++) {
		if (vy_read_iterator_scan_mem(itr, i, &stop) != 0)
			return -1;
		if (stop)
			goto done;
	}
rescan_disk:
	/* The following code may yield as it needs to access disk. */
	vy_read_iterator_pin_slices(itr);
	for (i = itr->disk_src; i < itr->src_count; i++) {
		if (vy_read_iterator_scan_disk(itr, i, &stop) != 0)
			goto err_disk;
		if (stop)
			break;
	}
	vy_read_iterator_unpin_slices(itr);
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
	if (vy_read_iterator_restore_mem(itr) != 0)
		return -1;
	/*
	 * Scan the next range in case we transgressed the current
	 * range's boundaries.
	 */
	if (vy_read_iterator_range_is_done(itr)) {
		vy_read_iterator_next_range(itr);
		goto rescan_disk;
	}
done:
	if (itr->last_stmt != NULL && itr->curr_stmt != NULL)
	       assert(vy_read_iterator_cmp_stmt(itr, itr->curr_stmt,
						itr->last_stmt) > 0);

	if (itr->need_check_eq && itr->curr_stmt != NULL &&
	    vy_stmt_compare(itr->curr_stmt, itr->key,
			    itr->lsm->cmp_def) != 0)
		itr->curr_stmt = NULL;

	if (vy_read_iterator_track_read(itr, itr->curr_stmt) != 0)
		return -1;

	*ret = itr->curr_stmt;
	return 0;

err_disk:
	vy_read_iterator_unpin_slices(itr);
	return -1;
}

/**
 * Iterate to the next (elder) version of the same key
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read error
 */
static NODISCARD int
vy_read_iterator_next_lsn(struct vy_read_iterator *itr, struct tuple **ret)
{
	uint32_t i;
	bool unused;
	struct vy_read_src *src;

	assert(itr->curr_stmt != NULL);
	assert(itr->curr_src < itr->skipped_src);

	/* Cache stores only terminal statements. */
	assert(itr->curr_src != itr->cache_src);

	if (itr->curr_src == itr->txw_src) {
		/*
		 * Write set does not store statement history.
		 * Look up the older statement in the cache and
		 * if it isn't there proceed to mems and runs.
		 */
		src = &itr->src[itr->cache_src];
		if (itr->cache_src >= itr->skipped_src)
			vy_read_iterator_scan_cache(itr, &unused);
		if (src->front_id == itr->front_id)
			goto found;
	}

	/* Look up the older statement in in-memory trees. */
	for (i = MAX(itr->curr_src, itr->mem_src); i < itr->disk_src; i++) {
		src = &itr->src[i];
		if (i >= itr->skipped_src &&
		    vy_read_iterator_scan_mem(itr, i, &unused) != 0)
			return -1;
		if (src->front_id != itr->front_id)
			continue;
		if (i == itr->curr_src &&
		    vy_mem_iterator_next_lsn(&src->mem_iterator,
					     &src->stmt) != 0)
			return -1;
		if (src->stmt != NULL)
			goto found;
	}

	/*
	 * Look up the older statement in on-disk runs.
	 *
	 * Note, we don't need to check the LSM tree version after the yield
	 * caused by the disk read, because once we've come to this point,
	 * we won't read any source except run slices, which are pinned
	 * and hence cannot be removed during the yield.
	 */
	vy_read_iterator_pin_slices(itr);
	for (i = MAX(itr->curr_src, itr->disk_src); i < itr->src_count; i++) {
		src = &itr->src[i];
		if (i >= itr->skipped_src &&
		    vy_read_iterator_scan_disk(itr, i, &unused) != 0)
			goto err_disk;
		if (src->front_id != itr->front_id)
			continue;
		if (i == itr->curr_src &&
		    vy_run_iterator_next_lsn(&src->run_iterator,
					     &src->stmt) != 0)
			goto err_disk;
		if (src->stmt != NULL)
			break;
	}
	vy_read_iterator_unpin_slices(itr);

	if (i < itr->src_count)
		goto found;

	/* Searched everywhere, found nothing. */
	*ret = NULL;
	return 0;
found:
	tuple_ref(src->stmt);
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = src->stmt;
	itr->curr_src = src - itr->src;
	*ret = itr->curr_stmt;
	return 0;

err_disk:
	vy_read_iterator_unpin_slices(itr);
	return -1;
}

/**
 * Squash in a single REPLACE all UPSERTs for the current key.
 *
 * @retval 0 success
 * @retval -1 error
 */
static NODISCARD int
vy_read_iterator_squash_upsert(struct vy_read_iterator *itr,
			       struct tuple **ret)
{
	*ret = NULL;
	struct vy_lsm *lsm = itr->lsm;
	struct tuple *t = itr->curr_stmt;

	/* Upserts enabled only in the primary index LSM tree. */
	assert(vy_stmt_type(t) != IPROTO_UPSERT || lsm->index_id == 0);
	tuple_ref(t);
	while (vy_stmt_type(t) == IPROTO_UPSERT) {
		struct tuple *next;
		int rc = vy_read_iterator_next_lsn(itr, &next);
		if (rc != 0) {
			tuple_unref(t);
			return rc;
		}
		struct tuple *applied = vy_apply_upsert(t, next,
				lsm->cmp_def, lsm->mem_format, true);
		lsm->stat.upsert.applied++;
		tuple_unref(t);
		if (applied == NULL)
			return -1;
		t = applied;
		if (next == NULL)
			break;
	}
	*ret = t;
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
		/*
		 * vy_task_dump_complete() may yield after adding
		 * a new run slice to a range and before removing
		 * dumped in-memory trees. We must not add both
		 * the slice and the trees in this case, because
		 * the read iterator can't deal with duplicates.
		 * Since lsm->dump_lsn is bumped after deletion
		 * of dumped in-memory trees, we can filter out
		 * the run slice containing duplicates by LSN.
		 */
		if (slice->run->info.min_lsn > lsm->dump_lsn)
			continue;
		assert(slice->run->info.max_lsn <= lsm->dump_lsn);
		struct vy_read_src *sub_src = vy_read_iterator_add_src(itr);
		vy_run_iterator_open(&sub_src->run_iterator,
				     &lsm->stat.disk.iterator, slice,
				     iterator_type, itr->key,
				     itr->read_view, lsm->cmp_def,
				     lsm->key_def, lsm->disk_format,
				     lsm->index_id == 0);
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
		vy_txw_iterator_close(&src->txw_iterator);
	}
	if (itr->cache_src < itr->src_count) {
		src = &itr->src[itr->cache_src];
		vy_cache_iterator_close(&src->cache_iterator);
	}
	for (i = itr->mem_src; i < itr->disk_src; i++) {
		src = &itr->src[i];
		vy_mem_iterator_close(&src->mem_iterator);
	}
	for (i = itr->disk_src; i < itr->src_count; i++) {
		src = &itr->src[i];
		vy_run_iterator_close(&src->run_iterator);
	}

	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = NULL;
	itr->curr_src = UINT32_MAX;
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
		      struct tuple *key, const struct vy_read_view **rv)
{
	memset(itr, 0, sizeof(*itr));

	itr->lsm = lsm;
	itr->tx = tx;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;

	if (tuple_field_count(key) == 0) {
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
	itr->curr_range = vy_range_tree_find_by_key(itr->lsm->tree,
			itr->iterator_type, itr->last_stmt ?: itr->key);
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
		range = dir > 0 ? vy_range_tree_next(itr->lsm->tree, range) :
				  vy_range_tree_prev(itr->lsm->tree, range);
		assert(range != NULL);

		if (itr->last_stmt == NULL)
			break;
		/*
		 * We could skip an entire range due to the cache.
		 * Make sure the next statement falls in the range.
		 */
		if (dir > 0 && (range->end == NULL ||
				vy_tuple_compare_with_key(itr->last_stmt,
						range->end, cmp_def) < 0))
			break;
		if (dir < 0 && (range->begin == NULL ||
				vy_tuple_compare_with_key(itr->last_stmt,
						range->begin, cmp_def) > 0))
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
 * Track a read in the conflict manager.
 */
static int
vy_read_iterator_track_read(struct vy_read_iterator *itr, struct tuple *stmt)
{
	if (itr->tx == NULL)
		return 0;

	if (stmt == NULL) {
		stmt = (itr->iterator_type == ITER_EQ ||
			itr->iterator_type == ITER_REQ ?
			itr->key : itr->lsm->env->empty_key);
	}

	int rc;
	if (iterator_direction(itr->iterator_type) >= 0) {
		rc = vy_tx_track(itr->tx, itr->lsm, itr->key,
				 itr->iterator_type != ITER_GT,
				 stmt, true);
	} else {
		rc = vy_tx_track(itr->tx, itr->lsm, stmt, true,
				 itr->key, itr->iterator_type != ITER_LT);
	}
	return rc;
}

NODISCARD int
vy_read_iterator_next(struct vy_read_iterator *itr, struct tuple **result)
{
	ev_tstamp start_time = ev_monotonic_now(loop());

	*result = NULL;

	if (!itr->search_started) {
		itr->search_started = true;
		itr->lsm->stat.lookup++;
		vy_read_iterator_restore(itr);
	}

	struct tuple *prev_key = itr->last_stmt;
	if (prev_key != NULL)
		tuple_ref(prev_key);
	bool skipped_txw_delete = false;

	struct tuple *t = NULL;
	struct vy_lsm *lsm = itr->lsm;
	int rc = 0;
	while (true) {
		rc = vy_read_iterator_next_key(itr, &t);
		if (rc != 0)
			goto clear;
		if (t == NULL) {
			if (itr->last_stmt != NULL)
				tuple_unref(itr->last_stmt);
			itr->last_stmt = NULL;
			rc = 0; /* No more data. */
			break;
		}
		rc = vy_read_iterator_squash_upsert(itr, &t);
		if (rc != 0)
			goto clear;
		if (itr->last_stmt != NULL)
			tuple_unref(itr->last_stmt);
		itr->last_stmt = t;
		if (vy_stmt_type(t) == IPROTO_INSERT ||
		    vy_stmt_type(t) == IPROTO_REPLACE)
			break;
		assert(vy_stmt_type(t) == IPROTO_DELETE);
		if (vy_stmt_lsn(t) == INT64_MAX) /* t is from write set */
			skipped_txw_delete = true;
	}

	*result = itr->last_stmt;
	assert(*result == NULL ||
	       vy_stmt_type(*result) == IPROTO_INSERT ||
	       vy_stmt_type(*result) == IPROTO_REPLACE);
	if (*result != NULL)
		vy_stmt_counter_acct_tuple(&lsm->stat.get, *result);

#ifndef NDEBUG
	/* Check constraints. */
	int dir = iterator_direction(itr->iterator_type);
	/*
	 * Each result statement with iterator type GE/GT must
	 * be >= iterator key. And with LT/LE must
	 * be <= iterator_key. @sa gh-2614.
	 */
	if (itr->last_stmt != NULL && tuple_field_count(itr->key) > 0) {
		int cmp = dir * vy_stmt_compare(*result, itr->key,
						itr->lsm->cmp_def);
		assert(cmp >= 0);
	}
	/*
	 * Ensure the read iterator does not return duplicates
	 * and respects statements order (lsm->cmp_def includes
	 * primary parts, so prev_key != itr->last_stmt for any
	 * LSM tree).
	 */
	if (prev_key != NULL && itr->last_stmt != NULL) {
		assert(dir * vy_tuple_compare(prev_key, itr->last_stmt,
					      lsm->cmp_def) < 0);
	}
#endif

	/**
	 * Add a statement to the cache
	 */
	if ((**itr->read_view).vlsn == INT64_MAX) { /* Do not store non-latest data */
		struct tuple *cache_prev = prev_key;
		if (skipped_txw_delete) {
			/*
			 * If we skipped DELETE that was read from TX write
			 * set, there is a chance that the database actually
			 * has the deleted key and we must not consider
			 * previous+current tuple as an unbroken chain.
			 */
			cache_prev = NULL;
		}
		vy_cache_add(&itr->lsm->cache, *result, cache_prev,
			     itr->key, itr->iterator_type);
	}
clear:
	if (prev_key != NULL)
		tuple_unref(prev_key);

	ev_tstamp latency = ev_monotonic_now(loop()) - start_time;
	latency_collect(&lsm->stat.latency, latency);

	if (latency > lsm->env->too_long_threshold) {
		say_warn("%s: select(%s, %s) => %s took too long: %.3f sec",
			 vy_lsm_name(lsm), tuple_str(itr->key),
			 iterator_type_strs[itr->iterator_type],
			 vy_stmt_str(itr->last_stmt), latency);
	}
	return rc;
}

/**
 * Close the iterator and free resources
 */
void
vy_read_iterator_close(struct vy_read_iterator *itr)
{
	if (itr->last_stmt != NULL)
		tuple_unref(itr->last_stmt);
	vy_read_iterator_cleanup(itr);
	free(itr->src);
	TRASH(itr);
}
