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
#include "vy_index.h"
#include "vy_stat.h"
#include "vy_point_iterator.h"

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
		struct vy_stmt_iterator iterator;
	};
	/** Set if the source can change after yield. */
	bool is_mutable;
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
 * The resulting vy_stmt_iterator must be properly initialized before merge
 * iteration start.
 * param is_mutable - Source can change during merge iteration
 */
static struct vy_read_src *
vy_read_iterator_add_src(struct vy_read_iterator *itr, bool is_mutable)
{
	if (itr->src_count == itr->src_capacity) {
		if (vy_read_iterator_reserve(itr, itr->src_count + 1) != 0)
			return NULL;
	}
	if (is_mutable) {
		if (itr->mutable_start == itr->mutable_end)
			itr->mutable_start = itr->src_count;
		itr->mutable_end = itr->src_count + 1;
	}
	itr->src[itr->src_count].front_id = 0;
	struct vy_read_src *src = &itr->src[itr->src_count++];
	memset(src, 0, sizeof(*src));
	src->is_mutable = is_mutable;
	return src;
}

/**
 * Check if the read iterator needs to be restored.
 *
 * @retval 0	if position did not change (iterator started)
 * @retval -2	iterator is no more valid
 */
static NODISCARD int
vy_read_iterator_check_version(struct vy_read_iterator *itr)
{
	if (itr->index->mem_list_version != itr->mem_list_version)
		return -2;
	if (itr->index->range_tree_version != itr->range_tree_version)
		return -2;
	if (itr->curr_range != NULL &&
	    itr->curr_range->version != itr->range_version)
		return -2;
	return 0;
}

/**
 * Iterate to the next key
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read error
 * @retval -2 iterator is not valid anymore
 */
static NODISCARD int
vy_read_iterator_next_key(struct vy_read_iterator *itr, struct tuple **ret)
{
	*ret = NULL;
	const struct key_def *def = itr->index->cmp_def;
	if (itr->curr_stmt != NULL && itr->iterator_type == ITER_EQ &&
	    tuple_field_count(itr->key) >= def->part_count) {
		/*
		 * There may be one statement at max satisfying
		 * EQ with a full key.
		 */
		return 0;
	}
	if (vy_read_iterator_check_version(itr))
		return -2;
	int dir = iterator_direction(itr->iterator_type);
	uint32_t prev_front_id = itr->front_id;
	itr->front_id++;
	itr->curr_src = UINT32_MAX;
	struct tuple *min_stmt = NULL;
	int rc = 0;

	bool was_yield_possible = false;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		bool is_yield_possible = i >= itr->mutable_end;
		was_yield_possible = was_yield_possible || is_yield_possible;

		struct vy_read_src *src = &itr->src[i];
		bool stop = false;

		if (!src->is_started) {
			/*
			 * This is the first time the source is used.
			 * Start the iterator.
			 */
			src->is_started = true;
			rc = src->iterator.iface->next_key(&src->iterator,
							   &src->stmt, &stop);
		} else {
			/*
			 * The source might have changed since the last time
			 * it was used, so the iterator needs to be restored.
			 */
			rc = src->iterator.iface->restore(&src->iterator,
							  itr->curr_stmt,
							  &src->stmt, &stop);
			if (rc == 0 && src->front_id == prev_front_id) {
				/*
				 * The source was used on the previous iteration.
				 * Advance the iterator to the next key unless it
				 * was restored.
				 */
				assert(itr->curr_stmt != NULL);
				assert(i < itr->skipped_start);
				rc = src->iterator.iface->next_key(&src->iterator,
								   &src->stmt, &stop);
			}
		}
		if (rc < 0)
			return -1;
		if (vy_read_iterator_check_version(itr))
			return -2;
		if (i >= itr->skipped_start && itr->curr_stmt != NULL) {
			/*
			 * If the source was not used on the last iteration,
			 * it might have lagged behind the current merge key.
			 * Advance it until it is up-to-date.
			 */
			while (src->stmt != NULL &&
			       dir * vy_tuple_compare(src->stmt, itr->curr_stmt,
						      def) <= 0) {
				rc = src->iterator.iface->next_key(&src->iterator,
								   &src->stmt,
								   &stop);
				if (vy_read_iterator_check_version(itr))
					return -2;
				if (rc != 0)
					return rc;
			}
		}
		if (i >= itr->skipped_start)
			itr->skipped_start++;

		if (stop && src->stmt == NULL && min_stmt == NULL) {
			itr->front_id++;
			itr->curr_src = i;
			src->front_id = itr->front_id;
			itr->skipped_start = i + 1;
			break;
		}
		if (src->stmt == NULL)
			continue;

		if (itr->curr_stmt == NULL && (itr->iterator_type == ITER_EQ ||
					       itr->iterator_type == ITER_GE ||
					       itr->iterator_type == ITER_LE) &&
		    tuple_field_count(itr->key) >= def->part_count &&
		    vy_stmt_compare(src->stmt, itr->key, def) == 0) {
			/**
			 * If the index is unique and the search key
			 * is full, we can avoid disk accesses on the
			 * first iteration in case the key is found
			 * in memory.
			 */
			stop = true;
		}

		int cmp = min_stmt == NULL ? -1 :
			  dir * vy_tuple_compare(src->stmt, min_stmt, def);
		if (cmp < 0) {
			itr->front_id++;
			if (min_stmt)
				tuple_unref(min_stmt);
			min_stmt = src->stmt;
			tuple_ref(min_stmt);
			itr->curr_src = i;
		}
		if (cmp <= 0)
			src->front_id = itr->front_id;

		if (stop) {
			itr->skipped_start = i + 1;
			break;
		}
	}

	for (int i = MIN(itr->skipped_start, itr->mutable_end) - 1;
	     was_yield_possible && i >= (int) itr->mutable_start; i--) {
		struct vy_read_src *src = &itr->src[i];
		bool stop;
		rc = src->iterator.iface->restore(&src->iterator,
						  itr->curr_stmt,
						  &src->stmt, &stop);
		if (vy_read_iterator_check_version(itr))
			return -2;
		if (rc < 0)
			return rc;
		if (rc == 0)
			continue;

		int cmp = min_stmt == NULL ? -1 :
			  dir * vy_tuple_compare(src->stmt, min_stmt, def);
		if (cmp > 0)
			continue;

		if (cmp < 0 || vy_stmt_lsn(src->stmt) > vy_stmt_lsn(min_stmt)) {
			if (min_stmt)
				tuple_unref(min_stmt);
			min_stmt = src->stmt;
			tuple_ref(min_stmt);
		}

		if (cmp < 0) {
			itr->front_id++;
			itr->curr_src = i;
		} else
			itr->curr_src = MIN(itr->curr_src, (uint32_t)i);

		src->front_id = itr->front_id;
	}

	if (itr->curr_stmt != NULL && min_stmt != NULL)
		assert(dir * vy_tuple_compare(min_stmt, itr->curr_stmt, def) > 0);

	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = min_stmt;
	*ret = itr->curr_stmt;

	return 0;
}

/**
 * Iterate to the next (elder) version of the same key
 *
 * Note, we don't need to restore individual sources in this
 * function, because sources that may yield (i.e. runs) are
 * immutable and iterated last (after txw, cache, and mems)
 * as they contain the oldest data.
 *
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read error
 * @retval -2 iterator is not valid anymore
 */
static NODISCARD int
vy_read_iterator_next_lsn(struct vy_read_iterator *itr, struct tuple **ret)
{
	*ret = NULL;
	if (itr->curr_src == UINT32_MAX)
		return 0;
	assert(itr->curr_stmt != NULL);
	const struct key_def *def = itr->index->cmp_def;
	struct vy_read_src *src = &itr->src[itr->curr_src];
	struct vy_stmt_iterator *sub_itr = &src->iterator;
	int rc = sub_itr->iface->next_lsn(sub_itr, &src->stmt);
	if (vy_read_iterator_check_version(itr))
		return -2;
	if (rc != 0)
		return rc;
	if (src->stmt != NULL) {
		tuple_unref(itr->curr_stmt);
		itr->curr_stmt = src->stmt;
		tuple_ref(itr->curr_stmt);
		*ret = itr->curr_stmt;
		return 0;
	}
	for (uint32_t i = itr->curr_src + 1; i < itr->src_count; i++) {
		src = &itr->src[i];

		if (i >= itr->skipped_start) {
			itr->skipped_start++;
			bool stop = false;
			int cmp = -1;
			while (true) {
				rc = src->iterator.iface->next_key(&src->iterator,
								   &src->stmt,
								   &stop);
				if (vy_read_iterator_check_version(itr))
					return -2;
				if (rc != 0)
					return rc;
				if (src->stmt == NULL)
					break;
				cmp = vy_tuple_compare(src->stmt, itr->curr_stmt,
						       def);
				if (cmp >= 0)
					break;
			}
			if (cmp == 0)
				itr->src[i].front_id = itr->front_id;
		}

		if (itr->src[i].front_id == itr->front_id) {
			itr->curr_src = i;
			tuple_unref(itr->curr_stmt);
			itr->curr_stmt = itr->src[i].stmt;
			tuple_ref(itr->curr_stmt);
			*ret = itr->curr_stmt;
			return 0;
		}
	}
	itr->curr_src = UINT32_MAX;
	return 0;
}

/**
 * Squash in a single REPLACE all UPSERTs for the current key.
 *
 * @retval 0 success
 * @retval -1 error
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_read_iterator_squash_upsert(struct vy_read_iterator *itr,
			       struct tuple **ret)
{
	*ret = NULL;
	struct vy_index *index = itr->index;
	struct tuple *t = itr->curr_stmt;

	/* Upserts enabled only in the primary index. */
	assert(vy_stmt_type(t) != IPROTO_UPSERT || index->id == 0);
	tuple_ref(t);
	while (vy_stmt_type(t) == IPROTO_UPSERT) {
		struct tuple *next;
		int rc = vy_read_iterator_next_lsn(itr, &next);
		if (rc != 0) {
			tuple_unref(t);
			return rc;
		}
		struct tuple *applied = vy_apply_upsert(t, next,
				index->cmp_def, index->mem_format,
				index->upsert_format, true);
		index->stat.upsert.applied++;
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
vy_read_iterator_add_tx(struct vy_read_iterator *itr,
			enum iterator_type iterator_type, struct tuple *key)
{
	assert(itr->tx != NULL);
	struct vy_txw_iterator_stat *stat = &itr->index->stat.txw.iterator;
	struct vy_read_src *sub_src = vy_read_iterator_add_src(itr, true);
	vy_txw_iterator_open(&sub_src->txw_iterator, stat, itr->tx, itr->index,
			     iterator_type, key);
}

static void
vy_read_iterator_add_cache(struct vy_read_iterator *itr,
			   enum iterator_type iterator_type, struct tuple *key)
{
	struct vy_read_src *sub_src = vy_read_iterator_add_src(itr, true);
	vy_cache_iterator_open(&sub_src->cache_iterator,
			       &itr->index->cache, iterator_type,
			       key, itr->read_view);
}

static void
vy_read_iterator_add_mem(struct vy_read_iterator *itr,
			 enum iterator_type iterator_type, struct tuple *key)
{
	struct vy_index *index = itr->index;
	struct vy_read_src *sub_src;

	/* Add the active in-memory index. */
	assert(index->mem != NULL);
	sub_src = vy_read_iterator_add_src(itr, true);
	vy_mem_iterator_open(&sub_src->mem_iterator,
			     &index->stat.memory.iterator,
			     index->mem, iterator_type, key,
			     itr->read_view);
	/* Add sealed in-memory indexes. */
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &index->sealed, in_sealed) {
		sub_src = vy_read_iterator_add_src(itr, false);
		vy_mem_iterator_open(&sub_src->mem_iterator,
				     &index->stat.memory.iterator,
				     mem, iterator_type, key,
				     itr->read_view);
	}
}

static void
vy_read_iterator_add_disk(struct vy_read_iterator *itr,
			  enum iterator_type iterator_type, struct tuple *key)
{
	assert(itr->curr_range != NULL);
	struct vy_index *index = itr->index;
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
		 * Since index->dump_lsn is bumped after deletion
		 * of dumped in-memory trees, we can filter out
		 * the run slice containing duplicates by LSN.
		 */
		if (slice->run->info.min_lsn > index->dump_lsn)
			continue;
		assert(slice->run->info.max_lsn <= index->dump_lsn);
		struct vy_read_src *sub_src = vy_read_iterator_add_src(itr, false);
		vy_run_iterator_open(&sub_src->run_iterator,
				     &index->stat.disk.iterator,
				     itr->run_env, slice,
				     iterator_type, key,
				     itr->read_view, index->cmp_def,
				     index->key_def, index->disk_format,
				     index->upsert_format, index->id == 0);
	}
}

/**
 * Set up the read iterator for the current range.
 */
static void
vy_read_iterator_use_range(struct vy_read_iterator *itr)
{
	struct tuple *key = itr->key;
	enum iterator_type iterator_type = itr->iterator_type;

	/* Close all open sources and reset merge state. */
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	for (uint32_t i = 0; i < itr->src_count; i++)
		itr->src[i].iterator.iface->close(&itr->src[i].iterator);
	itr->src_count = 0;
	itr->mutable_start = 0;
	itr->mutable_end = 0;
	itr->skipped_start = 0;
	itr->curr_stmt = NULL;
	itr->curr_src = UINT32_MAX;
	itr->front_id = 1;

	/*
	 * Open all sources starting from the last statement
	 * returned to the user. Newer sources must be added
	 * first.
	 */
	if (itr->last_stmt != NULL) {
		if (iterator_type == ITER_EQ)
			itr->need_check_eq = true;
		iterator_type = iterator_direction(iterator_type) >= 0 ?
				ITER_GT : ITER_LT;
		key = itr->last_stmt;
	}

	if (itr->tx != NULL)
		vy_read_iterator_add_tx(itr, iterator_type, key);

	vy_read_iterator_add_cache(itr, iterator_type, key);
	vy_read_iterator_add_mem(itr, iterator_type, key);

	if (itr->curr_range != NULL) {
		itr->range_version = itr->curr_range->version;
		vy_read_iterator_add_disk(itr, iterator_type, key);
	}
}

void
vy_read_iterator_open(struct vy_read_iterator *itr, struct vy_run_env *run_env,
		      struct vy_index *index, struct vy_tx *tx,
		      enum iterator_type iterator_type, struct tuple *key,
		      const struct vy_read_view **rv)
{
	memset(itr, 0, sizeof(*itr));

	itr->run_env = run_env;
	itr->index = index;
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
		itr->iterator_type = iterator_type == ITER_LT ||
				     iterator_type == ITER_LE ?
				     ITER_LE : ITER_GE;
	}

	if (iterator_type == ITER_ALL)
		itr->iterator_type = ITER_GE;

	if (iterator_type == ITER_REQ) {
		if (index->opts.is_unique &&
		    tuple_field_count(key) == index->cmp_def->part_count) {
			/* Use point-lookup iterator (optimization). */
			itr->iterator_type = ITER_EQ;
		} else {
			itr->need_check_eq = true;
			itr->iterator_type = ITER_LE;
		}
	}
}

/**
 * Prepare the read iterator for the first iteration.
 */
static void
vy_read_iterator_start(struct vy_read_iterator *itr)
{
	assert(!itr->search_started);
	assert(itr->last_stmt == NULL);
	assert(itr->curr_range == NULL);
	itr->search_started = true;

	itr->mem_list_version = itr->index->mem_list_version;
	itr->range_tree_version = itr->index->range_tree_version;
	itr->curr_range = vy_range_tree_find_by_key(itr->index->tree,
					itr->iterator_type, itr->key);
	vy_read_iterator_use_range(itr);

	itr->index->stat.lookup++;
}

/**
 * Restart the read iterator from the position following
 * the last statement returned to the user. Called when
 * the current range or the whole range tree is changed.
 */
static void
vy_read_iterator_restore(struct vy_read_iterator *itr)
{
	itr->mem_list_version = itr->index->mem_list_version;
	itr->range_tree_version = itr->index->range_tree_version;
	itr->curr_range = vy_range_tree_find_by_key(itr->index->tree,
			itr->iterator_type, itr->last_stmt ?: itr->key);
	vy_read_iterator_use_range(itr);
}

static bool
vy_read_iterator_next_range(struct vy_read_iterator *itr)
{
	struct vy_index *index = itr->index;
	struct vy_range *range = itr->curr_range;

	assert(range != NULL);

	switch (itr->iterator_type) {
	case ITER_LT:
	case ITER_LE:
		range = vy_range_tree_prev(index->tree, range);
		break;
	case ITER_GT:
	case ITER_GE:
		range = vy_range_tree_next(index->tree, range);
		break;
	case ITER_EQ:
		/* A partial key can be found in more than one range. */
		if (range->end != NULL &&
		    vy_stmt_compare_with_key(itr->key, range->end,
					     range->cmp_def) >= 0) {
			range = vy_range_tree_next(index->tree, range);
		} else {
			range = NULL;
		}
		break;
	default:
		unreachable();
	}

	itr->curr_range = range;
	if (range == NULL)
		return false;

	vy_read_iterator_use_range(itr);
	return true;
}

/**
 * Conventional wrapper around vy_read_iterator_next_key() to automatically
 * re-create the merge iterator on vy_index/vy_range/vy_run changes.
 */
static NODISCARD int
vy_read_iterator_merge_next_key(struct vy_read_iterator *itr,
				struct tuple **ret)
{
	struct key_def *cmp_def = itr->index->cmp_def;
	int dir = iterator_direction(itr->iterator_type);
	struct tuple *stmt;

	while (true) {
		int rc = vy_read_iterator_next_key(itr, &stmt);
		if (rc == -1)
			return -1;
		if (rc == -2) {
			vy_read_iterator_restore(itr);
			continue;
		}

		/*
		 * Check if the statement is within the current range.
		 * If it is, return it right away, otherwise move to
		 * the next range and restart merge.
		 */
		struct vy_range *range = itr->curr_range;
		if (range == NULL) {
			/* All ranges have been merged. */
			break;
		}

		if (stmt != NULL) {
			if (dir > 0 && (range->end == NULL ||
					vy_tuple_compare_with_key(stmt,
						range->end, cmp_def) < 0))
				break;
			if (dir < 0 && (range->begin == NULL ||
					vy_tuple_compare_with_key(stmt,
						range->begin, cmp_def) >= 0))
				break;
		}

		if (!vy_read_iterator_next_range(itr)) {
			/* No more ranges to merge. */
			break;
		}
	}
	*ret = stmt;
	return 0;
}

NODISCARD int
vy_read_iterator_next(struct vy_read_iterator *itr, struct tuple **result)
{
	ev_tstamp start_time = ev_monotonic_now(loop());

	/* The key might be set to NULL during previous call, that means
	 * that there's no more data */
	if (itr->key == NULL) {
		*result = NULL;
		return 0;
	}

	/* Run a special iterator for a special case */
	if (itr->iterator_type == ITER_EQ &&
	    tuple_field_count(itr->key) >= itr->index->cmp_def->part_count) {
		struct vy_point_iterator one;
		vy_point_iterator_open(&one, itr->run_env, itr->index,
				       itr->tx, itr->read_view, itr->key);
		int rc = vy_point_iterator_get(&one, result);
		if (*result) {
			tuple_ref(*result);
			itr->last_stmt = *result;
		}
		vy_point_iterator_close(&one);
		itr->key = NULL;
		return rc;
	}

	*result = NULL;

	if (!itr->search_started)
		vy_read_iterator_start(itr);

	struct tuple *prev_key = itr->last_stmt;
	if (prev_key != NULL)
		tuple_ref(prev_key);
	bool skipped_txw_delete = false;

	struct tuple *t = NULL;
	struct vy_index *index = itr->index;
	int rc = 0;
	while (true) {
		if (vy_read_iterator_merge_next_key(itr, &t)) {
			rc = -1;
			goto clear;
		}
		if (t == NULL) {
			if (itr->last_stmt != NULL)
				tuple_unref(itr->last_stmt);
			itr->last_stmt = NULL;
			rc = 0; /* No more data. */
			break;
		}
		rc = vy_read_iterator_squash_upsert(itr, &t);
		if (rc == -1)
			goto clear;
		if (rc == -2) {
			vy_read_iterator_restore(itr);
			continue;
		}
		if (vy_stmt_type(t) == IPROTO_REPLACE) {
			if (itr->last_stmt != NULL)
				tuple_unref(itr->last_stmt);
			itr->last_stmt = t;
			break;
		} else {
			assert(vy_stmt_type(t) == IPROTO_DELETE);
			if (vy_stmt_lsn(t) == INT64_MAX) /* t is from write set */
				skipped_txw_delete = true;
			tuple_unref(t);
		}
	}

	*result = itr->last_stmt;
	assert(*result == NULL || vy_stmt_type(*result) == IPROTO_REPLACE);
	if (*result != NULL)
		vy_stmt_counter_acct_tuple(&index->stat.get, *result);

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
						itr->index->cmp_def);
		assert(cmp >= 0);
	}
	/*
	 * Ensure the read iterator does not return duplicates
	 * and respects statements order (index->cmp_def includes
	 * primary parts, so prev_key != itr->last_stmt for any
	 * index).
	 */
	if (prev_key != NULL && itr->last_stmt != NULL) {
		assert(dir * vy_tuple_compare(prev_key, itr->last_stmt,
					      index->cmp_def) < 0);
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
		vy_cache_add(&itr->index->cache, *result, cache_prev,
			     itr->key, itr->iterator_type);
	}

	if (itr->need_check_eq && *result != NULL &&
	    vy_tuple_compare_with_key(*result, itr->key,
				      index->cmp_def) != 0) {
		*result = NULL;
	}

	if (itr->tx != NULL) {
		struct tuple *last = *result;
		if (last == NULL) {
			last = (itr->need_check_eq ||
				itr->iterator_type == ITER_EQ ?
				itr->key :
				index->env->empty_key);
		}
		if (iterator_direction(itr->iterator_type) >= 0) {
			rc = vy_tx_track(itr->tx, index,
					 itr->key,
					 itr->iterator_type != ITER_GT,
					 last, true);
		} else {
			rc = vy_tx_track(itr->tx, index, last, true,
					 itr->key,
					 itr->iterator_type != ITER_LT);
		}
	}
clear:
	if (prev_key != NULL)
		tuple_unref(prev_key);

	latency_collect(&index->stat.latency,
			ev_monotonic_now(loop()) - start_time);
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
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	for (uint32_t i = 0; i < itr->src_count; i++)
		itr->src[i].iterator.iface->close(&itr->src[i].iterator);
	free(itr->src);
	TRASH(itr);
}
