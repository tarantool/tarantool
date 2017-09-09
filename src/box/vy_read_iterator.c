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

/* {{{ Merge iterator */

/**
 * Merge source, support structure for vy_merge_iterator
 * Contains source iterator, additional properties and merge state
 */
struct vy_merge_src {
	/** Source iterator */
	union {
		struct vy_run_iterator run_iterator;
		struct vy_mem_iterator mem_iterator;
		struct vy_txw_iterator txw_iterator;
		struct vy_cache_iterator cache_iterator;
		struct vy_stmt_iterator iterator;
	};
	/** Source can change during merge iteration */
	bool is_mutable;
	/** Source belongs to a range (@sa vy_merge_iterator comments). */
	bool belong_range;
	/** Set if the iterator was started. */
	bool is_started;
	/**
	 * All sources with the same front_id as in struct
	 * vy_merge_iterator are on the same key of current output
	 * stmt (optimization)
	 */
	uint32_t front_id;
	struct tuple *stmt;
};

/**
 * Open the iterator.
 */
static void
vy_merge_iterator_open(struct vy_merge_iterator *itr,
		       enum iterator_type iterator_type,
		       struct tuple *key,
		       const struct key_def *cmp_def,
		       struct tuple_format *format,
		       struct tuple_format *upsert_format,
		       bool is_primary)
{
	assert(key != NULL);
	itr->cmp_def = cmp_def;
	itr->format = format;
	itr->upsert_format = upsert_format;
	itr->is_primary = is_primary;
	itr->range_tree_version = 0;
	itr->p_mem_list_version = 0;
	itr->range_version = 0;
	itr->p_range_tree_version = NULL;
	itr->p_mem_list_version = NULL;
	itr->p_range_version = NULL;
	itr->key = key;
	itr->iterator_type = iterator_type;
	itr->src = NULL;
	itr->src_count = 0;
	itr->src_capacity = 0;
	itr->src = NULL;
	itr->curr_src = UINT32_MAX;
	itr->front_id = 1;
	itr->mutable_start = 0;
	itr->mutable_end = 0;
	itr->skipped_start = 0;
	itr->curr_stmt = NULL;
	itr->is_one_value = iterator_type == ITER_EQ &&
			    tuple_field_count(key) >= cmp_def->part_count;
	itr->unique_optimization =
		(iterator_type == ITER_EQ || iterator_type == ITER_GE ||
		 iterator_type == ITER_LE) &&
		tuple_field_count(key) >= cmp_def->part_count;
	itr->search_started = false;
	itr->range_ended = false;
}

/**
 * Close the iterator and free resources.
 */
static void
vy_merge_iterator_close(struct vy_merge_iterator *itr)
{
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	for (size_t i = 0; i < itr->src_count; i++)
		itr->src[i].iterator.iface->close(&itr->src[i].iterator);
	free(itr->src);
}

/**
 * Extend internal source array capacity to fit capacity sources.
 * Not necessary to call is but calling it allows to optimize internal memory
 * allocation
 */
static NODISCARD int
vy_merge_iterator_reserve(struct vy_merge_iterator *itr, uint32_t capacity)
{
	if (itr->src_capacity >= capacity)
		return 0;
	struct vy_merge_src *new_src = calloc(capacity, sizeof(*new_src));
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
 * Add another source to merge iterator. Must be called before actual
 * iteration start and must not be called after.
 * @sa necessary order of adding requirements in struct vy_merge_iterator
 * comments.
 * The resulting vy_stmt_iterator must be properly initialized before merge
 * iteration start.
 * param is_mutable - Source can change during merge iteration
 * param belong_range - Source belongs to a range (see vy_merge_iterator comments)
 */
static struct vy_merge_src *
vy_merge_iterator_add(struct vy_merge_iterator *itr,
		      bool is_mutable, bool belong_range)
{
	assert(!itr->search_started);
	if (itr->src_count == itr->src_capacity) {
		if (vy_merge_iterator_reserve(itr, itr->src_count + 1) != 0)
			return NULL;
	}
	if (is_mutable) {
		if (itr->mutable_start == itr->mutable_end)
			itr->mutable_start = itr->src_count;
		itr->mutable_end = itr->src_count + 1;
	}
	itr->src[itr->src_count].front_id = 0;
	struct vy_merge_src *src = &itr->src[itr->src_count++];
	src->is_mutable = is_mutable;
	src->belong_range = belong_range;
	return src;
}

/*
 * Enable version checking.
 */
static void
vy_merge_iterator_set_version(struct vy_merge_iterator *itr,
			      const uint32_t *p_range_tree_version,
			      const uint32_t *p_mem_list_version,
			      const uint32_t *p_range_version)
{
	itr->p_range_tree_version = p_range_tree_version;
	if (itr->p_range_tree_version != NULL)
		itr->range_tree_version = *p_range_tree_version;
	itr->p_mem_list_version = p_mem_list_version;
	if (itr->p_mem_list_version != NULL)
		itr->mem_list_version = *p_mem_list_version;
	itr->p_range_version = p_range_version;
	if (itr->p_range_version != NULL)
		itr->range_version = *p_range_version;
}

/*
 * Try to restore position of merge iterator
 * @retval 0	if position did not change (iterator started)
 * @retval -2	iterator is no more valid
 */
static NODISCARD int
vy_merge_iterator_check_version(struct vy_merge_iterator *itr)
{
	if (itr->p_range_tree_version != NULL &&
	    *itr->p_range_tree_version != itr->range_tree_version)
		return -2;
	if (itr->p_mem_list_version != NULL &&
	    *itr->p_mem_list_version != itr->mem_list_version)
		return -2;
	if (itr->p_range_version != NULL &&
	    *itr->p_range_version != itr->range_version)
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
vy_merge_iterator_next_key(struct vy_merge_iterator *itr, struct tuple **ret)
{
	*ret = NULL;
	if (itr->search_started && itr->is_one_value)
		return 0;
	itr->search_started = true;
	if (vy_merge_iterator_check_version(itr))
		return -2;
	const struct key_def *def = itr->cmp_def;
	int dir = iterator_direction(itr->iterator_type);
	uint32_t prev_front_id = itr->front_id;
	itr->front_id++;
	itr->curr_src = UINT32_MAX;
	struct tuple *min_stmt = NULL;
	itr->range_ended = true;
	int rc = 0;

	bool was_yield_possible = false;
	for (uint32_t i = 0; i < itr->src_count; i++) {
		bool is_yield_possible = i >= itr->mutable_end;
		was_yield_possible = was_yield_possible || is_yield_possible;

		struct vy_merge_src *src = &itr->src[i];
		bool stop = false;

		if (src->front_id == prev_front_id) {
			/*
			 * The source was used on the previous iteration.
			 * Advance it to the next key.
			 */
			assert(src->is_started);
			assert(itr->curr_stmt != NULL);
			assert(i < itr->skipped_start);
			rc = src->iterator.iface->next_key(&src->iterator,
							   &src->stmt, &stop);
		} else if (!src->is_started) {
			/*
			 * This is the first time the source is used.
			 * Start the iterator.
			 */
			src->is_started = true;
			rc = src->iterator.iface->next_key(&src->iterator,
							   &src->stmt, &stop);
		} else if (i < itr->skipped_start) {
			/*
			 * The source was updated, but was not used, so it
			 * does not need to be advanced. However, it might
			 * have changed since the last iteration, so the
			 * iterator needs to be restored.
			 */
			rc = src->iterator.iface->restore(&src->iterator,
							  itr->curr_stmt,
							  &src->stmt, &stop);
			rc = rc > 0 ? 0 : rc;
		}
		if (vy_merge_iterator_check_version(itr))
			return -2;
		if (rc != 0)
			return rc;
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
				if (vy_merge_iterator_check_version(itr))
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

		itr->range_ended = itr->range_ended && !src->belong_range;

		if (itr->unique_optimization &&
		    vy_stmt_compare(src->stmt, itr->key, def) == 0)
			stop = true;

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
	if (itr->skipped_start < itr->src_count)
		itr->range_ended = false;

	if (itr->curr_stmt != NULL && min_stmt != NULL)
		assert(dir * vy_tuple_compare(min_stmt, itr->curr_stmt, def) >= 0);

	for (int i = MIN(itr->skipped_start, itr->mutable_end) - 1;
	     was_yield_possible && i >= (int) itr->mutable_start; i--) {
		struct vy_merge_src *src = &itr->src[i];
		bool stop;
		rc = src->iterator.iface->restore(&src->iterator,
						  itr->curr_stmt,
						  &src->stmt, &stop);
		if (vy_merge_iterator_check_version(itr))
			return -2;
		if (rc < 0)
			return rc;
		if (rc == 0)
			continue;

		int cmp = min_stmt == NULL ? -1 :
			  dir * vy_tuple_compare(src->stmt, min_stmt, def);
		if (cmp < 0) {
			itr->front_id++;
			if (min_stmt)
				tuple_unref(min_stmt);
			min_stmt = src->stmt;
			tuple_ref(min_stmt);
			itr->curr_src = i;
			src->front_id = itr->front_id;
		} else if (cmp == 0) {
			itr->curr_src = MIN(itr->curr_src, (uint32_t)i);
			src->front_id = itr->front_id;
		}
		if (itr->curr_stmt != NULL && min_stmt != NULL)
			assert(dir * vy_tuple_compare(min_stmt, itr->curr_stmt, def) >= 0);
	}

	if (itr->skipped_start < itr->src_count)
		itr->range_ended = false;

	itr->unique_optimization = false;

	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = min_stmt;
	*ret = itr->curr_stmt;

	return 0;
}

/**
 * Iterate to the next (elder) version of the same key
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 read error
 * @retval -2 iterator is not valid anymore
 */
static NODISCARD int
vy_merge_iterator_next_lsn(struct vy_merge_iterator *itr, struct tuple **ret)
{
	if (!itr->search_started)
		return vy_merge_iterator_next_key(itr, ret);
	*ret = NULL;
	if (itr->curr_src == UINT32_MAX)
		return 0;
	assert(itr->curr_stmt != NULL);
	const struct key_def *def = itr->cmp_def;
	struct vy_merge_src *src = &itr->src[itr->curr_src];
	struct vy_stmt_iterator *sub_itr = &src->iterator;
	int rc = sub_itr->iface->next_lsn(sub_itr, &src->stmt);
	if (vy_merge_iterator_check_version(itr))
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
				if (vy_merge_iterator_check_version(itr))
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
 * Squash in the single statement all rest statements of current key
 * starting from the current statement.
 *
 * @retval 0 success or EOF (*ret == NULL)
 * @retval -1 error
 * @retval -2 invalid iterator
 */
static NODISCARD int
vy_merge_iterator_squash_upsert(struct vy_merge_iterator *itr,
				struct tuple **ret, bool suppress_error,
				int64_t *upserts_applied)
{
	*ret = NULL;
	struct tuple *t = itr->curr_stmt;

	if (t == NULL)
		return 0;
	/* Upserts enabled only in the primary index. */
	assert(vy_stmt_type(t) != IPROTO_UPSERT || itr->is_primary);
	tuple_ref(t);
	while (vy_stmt_type(t) == IPROTO_UPSERT) {
		struct tuple *next;
		int rc = vy_merge_iterator_next_lsn(itr, &next);
		if (rc != 0) {
			tuple_unref(t);
			return rc;
		}
		if (next == NULL)
			break;
		struct tuple *applied;
		assert(itr->is_primary);
		applied = vy_apply_upsert(t, next, itr->cmp_def, itr->format,
					  itr->upsert_format, suppress_error);
		++*upserts_applied;
		tuple_unref(t);
		if (applied == NULL)
			return -1;
		t = applied;
	}
	*ret = t;
	return 0;
}

/* }}} Merge iterator */

static void
vy_read_iterator_add_tx(struct vy_read_iterator *itr,
			enum iterator_type iterator_type, struct tuple *key)
{
	assert(itr->tx != NULL);
	struct vy_txw_iterator_stat *stat = &itr->index->stat.txw.iterator;
	struct vy_merge_src *sub_src =
		vy_merge_iterator_add(&itr->merge_iterator, true, false);
	vy_txw_iterator_open(&sub_src->txw_iterator, stat, itr->tx, itr->index,
			     iterator_type, key);
}

static void
vy_read_iterator_add_cache(struct vy_read_iterator *itr,
			   enum iterator_type iterator_type, struct tuple *key)
{
	struct vy_merge_src *sub_src =
		vy_merge_iterator_add(&itr->merge_iterator, true, false);
	vy_cache_iterator_open(&sub_src->cache_iterator,
			       &itr->index->cache, iterator_type,
			       key, itr->read_view);
}

static void
vy_read_iterator_add_mem(struct vy_read_iterator *itr,
			 enum iterator_type iterator_type, struct tuple *key)
{
	struct vy_index *index = itr->index;
	struct vy_merge_src *sub_src;

	/* Add the active in-memory index. */
	assert(index->mem != NULL);
	sub_src = vy_merge_iterator_add(&itr->merge_iterator, true, false);
	vy_mem_iterator_open(&sub_src->mem_iterator,
			     &index->stat.memory.iterator,
			     index->mem, iterator_type, key,
			     itr->read_view);
	/* Add sealed in-memory indexes. */
	struct vy_mem *mem;
	rlist_foreach_entry(mem, &index->sealed, in_sealed) {
		sub_src = vy_merge_iterator_add(&itr->merge_iterator,
						false, false);
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
		 * merge iterator can't deal with duplicates.
		 * Since index->dump_lsn is bumped after deletion
		 * of dumped in-memory trees, we can filter out
		 * the run slice containing duplicates by LSN.
		 */
		if (slice->run->info.min_lsn > index->dump_lsn)
			continue;
		assert(slice->run->info.max_lsn <= index->dump_lsn);
		struct vy_merge_src *sub_src = vy_merge_iterator_add(
			&itr->merge_iterator, false, true);
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
 * Set up merge iterator for the current range.
 */
static void
vy_read_iterator_use_range(struct vy_read_iterator *itr)
{
	struct tuple *key = itr->key;
	enum iterator_type iterator_type = itr->iterator_type;

	if (itr->curr_stmt != NULL) {
		if (iterator_type == ITER_EQ)
			itr->need_check_eq = true;
		iterator_type = iterator_direction(iterator_type) >= 0 ?
				ITER_GT : ITER_LT;
		key = itr->curr_stmt;
	}

	if (itr->tx != NULL)
		vy_read_iterator_add_tx(itr, iterator_type, key);

	vy_read_iterator_add_cache(itr, iterator_type, key);
	vy_read_iterator_add_mem(itr, iterator_type, key);

	if (itr->curr_range != NULL)
		vy_read_iterator_add_disk(itr, iterator_type, key);

	/* Enable range and range index version checks */
	vy_merge_iterator_set_version(&itr->merge_iterator,
				      &itr->index->range_tree_version,
				      &itr->index->mem_list_version,
				      itr->curr_range != NULL ?
				      &itr->curr_range->version : NULL);
}

/**
 * Open the iterator.
 */
void
vy_read_iterator_open(struct vy_read_iterator *itr, struct vy_run_env *run_env,
		      struct vy_index *index, struct vy_tx *tx,
		      enum iterator_type iterator_type, struct tuple *key,
		      const struct vy_read_view **rv)
{
	itr->run_env = run_env;
	itr->index = index;
	itr->tx = tx;
	itr->iterator_type = iterator_type;
	itr->key = key;
	itr->read_view = rv;
	itr->search_started = false;
	itr->need_check_eq = false;
	itr->curr_stmt = NULL;
	itr->curr_range = NULL;

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
 * Start lazy search
 */
static void
vy_read_iterator_start(struct vy_read_iterator *itr)
{
	assert(!itr->search_started);
	assert(itr->curr_stmt == NULL);
	assert(itr->curr_range == NULL);
	itr->search_started = true;

	vy_range_iterator_open(&itr->range_iterator, itr->index->tree,
			       itr->iterator_type, itr->key);
	vy_range_iterator_next(&itr->range_iterator, &itr->curr_range);
	vy_merge_iterator_open(&itr->merge_iterator, itr->iterator_type,
			       itr->key, itr->index->cmp_def,
			       itr->index->mem_format,
			       itr->index->upsert_format, itr->index->id == 0);
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
	vy_range_iterator_restore(&itr->range_iterator, itr->curr_stmt,
				  &itr->curr_range);
	/* Re-create merge iterator */
	vy_merge_iterator_close(&itr->merge_iterator);
	vy_merge_iterator_open(&itr->merge_iterator, itr->iterator_type,
			       itr->key, itr->index->cmp_def,
			       itr->index->mem_format,
			       itr->index->upsert_format, itr->index->id == 0);
	vy_read_iterator_use_range(itr);
}

/**
 * Conventional wrapper around vy_merge_iterator_next_key() to automatically
 * re-create the merge iterator on vy_index/vy_range/vy_run changes.
 */
static NODISCARD int
vy_read_iterator_merge_next_key(struct vy_read_iterator *itr,
				struct tuple **ret)
{
	int rc;
	struct vy_merge_iterator *mi = &itr->merge_iterator;
retry:
	*ret = NULL;
	while ((rc = vy_merge_iterator_next_key(mi, ret)) == -2)
		vy_read_iterator_restore(itr);
	/*
	 * If the iterator after next_key is on the same key then
	 * go to the next.
	 */
	if (*ret != NULL && itr->curr_stmt != NULL &&
	    vy_tuple_compare(itr->curr_stmt, *ret, itr->index->cmp_def) == 0)
		goto retry;
	return rc;
}

/**
 * Goto next range according to order
 * return 0 : something was found
 * return 1 : no more data
 * return -1 : read error
 */
static NODISCARD int
vy_read_iterator_next_range(struct vy_read_iterator *itr, struct tuple **ret)
{
	assert(itr->curr_range != NULL);
	*ret = NULL;
	struct tuple *stmt = NULL;
	int rc = 0;
	struct vy_index *index = itr->index;
restart:
	vy_merge_iterator_close(&itr->merge_iterator);
	vy_merge_iterator_open(&itr->merge_iterator, itr->iterator_type,
			       itr->key, index->cmp_def, index->mem_format,
			       index->upsert_format, index->id == 0);
	vy_range_iterator_next(&itr->range_iterator, &itr->curr_range);
	vy_read_iterator_use_range(itr);
	rc = vy_read_iterator_merge_next_key(itr, &stmt);
	if (rc < 0)
		return -1;
	assert(rc >= 0);
	if (!stmt && itr->merge_iterator.range_ended && itr->curr_range != NULL)
		goto restart;

	if (stmt != NULL && itr->curr_range != NULL) {
		/** Check if the statement is out of the range. */
		int dir = iterator_direction(itr->iterator_type);
		if (dir >= 0 && itr->curr_range->end != NULL &&
		    vy_tuple_compare_with_key(stmt, itr->curr_range->end,
					      index->cmp_def) >= 0) {
			goto restart;
		}
		if (dir < 0 && itr->curr_range->begin != NULL &&
		    vy_tuple_compare_with_key(stmt, itr->curr_range->begin,
					      index->cmp_def) < 0) {
			goto restart;
		}
	}

	*ret = stmt;
	return rc;
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
			itr->curr_stmt = *result;
		}
		vy_point_iterator_close(&one);
		itr->key = NULL;
		return rc;
	}

	*result = NULL;

	if (!itr->search_started)
		vy_read_iterator_start(itr);

	struct tuple *prev_key = itr->curr_stmt;
	if (prev_key != NULL)
		tuple_ref(prev_key);
	bool skipped_txw_delete = false;

	struct tuple *t = NULL;
	struct vy_merge_iterator *mi = &itr->merge_iterator;
	struct vy_index *index = itr->index;
	int rc = 0;
	while (true) {
		if (vy_read_iterator_merge_next_key(itr, &t)) {
			rc = -1;
			goto clear;
		}
restart:
		if (mi->range_ended && itr->curr_range != NULL &&
		    vy_read_iterator_next_range(itr, &t)) {
			rc = -1;
			goto clear;
		}
		if (t == NULL) {
			if (itr->curr_stmt != NULL)
				tuple_unref(itr->curr_stmt);
			itr->curr_stmt = NULL;
			rc = 0; /* No more data. */
			break;
		}
		rc = vy_merge_iterator_squash_upsert(mi, &t, true,
						     &index->stat.upsert.applied);
		if (rc != 0) {
			if (rc == -1)
				goto clear;
			do {
				vy_read_iterator_restore(itr);
				rc = vy_merge_iterator_next_lsn(mi, &t);
			} while (rc == -2);
			if (rc != 0)
				goto clear;
			goto restart;
		}
		assert(t != NULL);
		if (vy_stmt_type(t) != IPROTO_DELETE) {
			if (vy_stmt_type(t) == IPROTO_UPSERT) {
				struct tuple *applied;
				assert(index->id == 0);
				applied = vy_apply_upsert(t, NULL,
							  index->cmp_def,
							  mi->format,
							  mi->upsert_format,
							  true);
				index->stat.upsert.applied++;
				tuple_unref(t);
				t = applied;
				assert(vy_stmt_type(t) == IPROTO_REPLACE);
			}
			if (itr->curr_stmt != NULL)
				tuple_unref(itr->curr_stmt);
			itr->curr_stmt = t;
			break;
		} else {
			if (vy_stmt_lsn(t) == INT64_MAX) { /* t is from write set */
				assert(vy_stmt_type(t) == IPROTO_DELETE);
				skipped_txw_delete = true;
			}
			tuple_unref(t);
		}
	}

	*result = itr->curr_stmt;
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
	if (itr->curr_stmt != NULL && tuple_field_count(itr->key) > 0) {
		int cmp = dir * vy_stmt_compare(*result, itr->key,
						itr->index->cmp_def);
		assert(cmp >= 0);
	}
	/*
	 * Ensure the read iterator does not return duplicates
	 * and respects statements order (index->cmp_def includes
	 * primary parts, so prev_key != itr->curr_stmt for any
	 * index).
	 */
	if (prev_key != NULL && itr->curr_stmt != NULL) {
		assert(dir * vy_tuple_compare(prev_key, itr->curr_stmt,
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
	if (itr->curr_stmt != NULL)
		tuple_unref(itr->curr_stmt);
	itr->curr_stmt = NULL;
	if (itr->search_started)
		vy_merge_iterator_close(&itr->merge_iterator);
}

/* }}} Iterator over index */
