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
#include "vy_write_iterator.h"
#include "vy_mem.h"
#include "vy_run.h"
#include "vy_upsert.h"
#include "column_mask.h"
#include "fiber.h"

#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"

static bool
heap_less(heap_t *heap, struct heap_node *n1, struct heap_node *n2);

#define HEAP_NAME vy_source_heap
#define HEAP_LESS heap_less
#include "salad/heap.h"

/**
 * Merge source of a write iterator. Represents a mem or a run.
 */
struct vy_write_src {
	/* Link in vy_write_iterator::src_list */
	struct rlist in_src_list;
	/* Node in vy_write_iterator::src_heap */
	struct heap_node heap_node;
	/* Current tuple in the source (with minimal key and maximal LSN) */
	struct tuple *tuple;
	/**
	 * If this flag is set, this is a so called "virtual"
	 * source. A virtual source does not stand for any mem or
	 * run, but represents a delimiter between the current key
	 * and the next one. There is a special rule used by the
	 * write iterator heap when comparing with a virtual
	 * source. Such source is greater than any source with
	 * the same key and less than any source with a greater
	 * key, regardless of LSN.
	 */
	bool is_end_of_key;
	/** An iterator over the source */
	union {
		struct vy_slice_stream slice_stream;
		struct vy_mem_stream mem_stream;
		struct vy_stmt_stream stream;
	};
};

/**
 * A sequence of versions of a key, sorted by LSN in ascending order.
 * (history->tuple.lsn < history->next->tuple.lsn).
 */
struct vy_write_history {
	/** Next version with greater LSN. */
	struct vy_write_history *next;
	/** Key. */
	struct tuple *tuple;
};

/**
 * Create a new vy_write_history object, save a statement into it
 * and link with a newer version. This function effectively
 * reverses key LSN order from newest first to oldest first, i.e.
 * orders statements on the same key chronologically.
 *
 * @param region Allocator for the object.
 * @param tuple Key version.
 * @param next Next version of the key.
 *
 * @retval not NULL Created object.
 * @retval NULL     Memory error.
 */
static inline struct vy_write_history *
vy_write_history_new(struct region *region, struct tuple *tuple,
		     struct vy_write_history *next)
{
	struct vy_write_history *h =
		region_alloc_object(region, struct vy_write_history);
	if (h == NULL)
		return NULL;
	h->tuple = tuple;
	assert(next == NULL || (next->tuple != NULL &&
	       vy_stmt_lsn(next->tuple) > vy_stmt_lsn(tuple)));
	h->next = next;
	vy_stmt_ref_if_possible(tuple);
	return h;
}

/**
 * Clear an entire sequence of versions of a key. Free resources
 * of each version.
 * @param history History to clear.
 */
static inline void
vy_write_history_destroy(struct vy_write_history *history)
{
	do {
		if (history->tuple != NULL)
			vy_stmt_unref_if_possible(history->tuple);
		history = history->next;
	} while (history != NULL);
}

/** Read view of a key. */
struct vy_read_view_stmt {
	/** Read view LSN. */
	int64_t vlsn;
	/** Result key version, visible to this @vlsn. */
	struct tuple *tuple;
	/**
	 * A history of changes building up to this read
	 * view. Once built, it is merged into a single
	 * @tuple.
	 */
	struct vy_write_history *history;
};

/**
 * Free resources, unref tuples, including all tuples in the
 * history.
 * @param rv Read view to clear.
 */
static inline void
vy_read_view_stmt_destroy(struct vy_read_view_stmt *rv)
{
	if (rv->tuple != NULL)
		vy_stmt_unref_if_possible(rv->tuple);
	rv->tuple = NULL;
	if (rv->history != NULL)
		vy_write_history_destroy(rv->history);
	rv->history = NULL;
}

/* @sa vy_write_iterator.h */
struct vy_write_iterator {
	/** Parent class, must be the first member */
	struct vy_stmt_stream base;
	/* List of all sources of the iterator */
	struct rlist src_list;
	/* A heap to order the sources, newest LSN at heap top. */
	heap_t src_heap;
	/** Index key definition used to store statements on disk. */
	const struct key_def *cmp_def;
	/** Format to allocate new REPLACE and DELETE tuples from vy_run */
	struct tuple_format *format;
	/* There is no LSM tree level older than the one we're writing to. */
	bool is_last_level;
	/**
	 * Set if this iterator is for a primary index.
	 * Not all implementation are applicable to the primary
	 * key and its tuple format is different.
	 */
	bool is_primary;

	/** Length of the @read_views. */
	int rv_count;
	/**
	 * If there are no changes between two read views, the
	 * newer read view is left empty. This is a count of
	 * non-empty read views. It's used to speed up squashing.
	 */
	int rv_used_count;
	/**
	 * Current read view in @read_views. It is used to return
	 * key versions one by one from vy_write_iterator_next.
	 */
	int stmt_i;
	/**
	 * Read views of the same key sorted by LSN in descending
	 * order, starting from INT64_MAX.
	 *
	 * Some read views in @read_views can be empty,
	 * - if there are no changes since the previous read view
	 * - if there are no changes up until this read view since
	 *   the beginning of time.
	 */
	struct vy_read_view_stmt read_views[0];
};

/**
 * Comparator of the heap. Put newer LSNs first, unless
 * it's a virtual source (is_end_of_key).
 */
static bool
heap_less(heap_t *heap, struct heap_node *node1, struct heap_node *node2)
{
	struct vy_write_iterator *stream =
		container_of(heap, struct vy_write_iterator, src_heap);
	struct vy_write_src *src1 =
		container_of(node1, struct vy_write_src, heap_node);
	struct vy_write_src *src2 =
		container_of(node2, struct vy_write_src, heap_node);

	int cmp = vy_tuple_compare(src1->tuple, src2->tuple, stream->cmp_def);
	if (cmp != 0)
		return cmp < 0;

	/**
	 * Keys are equal, order by LSN, descending.
	 * Virtual sources use 0 for LSN, so they are ordered
	 * last automatically.
	 */
	int64_t lsn1 = src1->is_end_of_key  ? 0 : vy_stmt_lsn(src1->tuple);
	int64_t lsn2 = src2->is_end_of_key  ? 0 : vy_stmt_lsn(src2->tuple);
	if (lsn1 != lsn2)
		return lsn1 > lsn2;

	/**
	 * LSNs are equal. This may happen only during forced recovery.
	 * Prioritize terminal (non-UPSERT) statements
	 */
	return (vy_stmt_type(src1->tuple) == IPROTO_UPSERT ? 1 : 0) <
	       (vy_stmt_type(src2->tuple) == IPROTO_UPSERT ? 1 : 0);

}

/**
 * Allocate a source and add it to a write iterator.
 * @param stream - the write iterator.
 * @return the source or NULL on memory error.
 */
static struct vy_write_src *
vy_write_iterator_new_src(struct vy_write_iterator *stream)
{
	struct vy_write_src *res = (struct vy_write_src *) malloc(sizeof(*res));
	if (res == NULL) {
		diag_set(OutOfMemory, sizeof(*res),
			 "malloc", "vinyl write stream");
		return NULL;
	}
	res->is_end_of_key = false;
	rlist_add(&stream->src_list, &res->in_src_list);
	return res;
}


/** Close a stream, remove it from the write iterator and delete. */
static void
vy_write_iterator_delete_src(struct vy_write_iterator *stream,
			     struct vy_write_src *src)
{
	(void)stream;
	assert(!src->is_end_of_key);
	if (src->stream.iface->stop != NULL)
		src->stream.iface->stop(&src->stream);
	if (src->stream.iface->close != NULL)
		src->stream.iface->close(&src->stream);
	rlist_del(&src->in_src_list);
	free(src);
}

/**
 * Add a source to the write iterator heap. The added source
 * must be open.
 *
 * @return 0 - success, not 0 - error.
 */
static NODISCARD int
vy_write_iterator_add_src(struct vy_write_iterator *stream,
			  struct vy_write_src *src)
{
	if (src->stream.iface->start != NULL) {
		int rc = src->stream.iface->start(&src->stream);
		if (rc != 0) {
			vy_write_iterator_delete_src(stream, src);
			return rc;
		}
	}
	int rc = src->stream.iface->next(&src->stream, &src->tuple);
	if (rc != 0 || src->tuple == NULL) {
		vy_write_iterator_delete_src(stream, src);
		return rc;
	}
	rc = vy_source_heap_insert(&stream->src_heap, &src->heap_node);
	if (rc != 0) {
		diag_set(OutOfMemory, sizeof(void *),
			 "malloc", "vinyl write stream heap");
		vy_write_iterator_delete_src(stream, src);
		return rc;
	}
	return 0;
}

/**
 * Remove a source from the heap, destroy and free it.
 */
static void
vy_write_iterator_remove_src(struct vy_write_iterator *stream,
			   struct vy_write_src *src)
{
	vy_source_heap_delete(&stream->src_heap, &src->heap_node);
	vy_write_iterator_delete_src(stream, src);
}

static const struct vy_stmt_stream_iface vy_slice_stream_iface;

/**
 * Open an empty write iterator. To add sources to the iterator
 * use vy_write_iterator_add_* functions.
 * @return the iterator or NULL on error (diag is set).
 */
struct vy_stmt_stream *
vy_write_iterator_new(const struct key_def *cmp_def,
		      struct tuple_format *format,
		      bool is_primary, bool is_last_level,
		      struct rlist *read_views)
{
	/*
	 * One is reserved for INT64_MAX - maximal read view.
	 */
	int count = 1;
	struct rlist *unused;
	rlist_foreach(unused, read_views)
		++count;
	size_t size = sizeof(struct vy_write_iterator) +
		      count * sizeof(struct vy_read_view_stmt);
	struct vy_write_iterator *stream =
		(struct vy_write_iterator *) calloc(1, size);
	if (stream == NULL) {
		diag_set(OutOfMemory, size, "malloc", "write stream");
		return NULL;
	}
	stream->stmt_i = -1;
	stream->rv_count = count;
	stream->read_views[0].vlsn = INT64_MAX;
	count--;
	struct vy_read_view *rv;
	/* Descending order. */
	rlist_foreach_entry(rv, read_views, in_read_views)
		stream->read_views[count--].vlsn = rv->vlsn;
	assert(count == 0);

	stream->base.iface = &vy_slice_stream_iface;
	vy_source_heap_create(&stream->src_heap);
	rlist_create(&stream->src_list);
	stream->cmp_def = cmp_def;
	stream->format = format;
	tuple_format_ref(stream->format);
	stream->is_primary = is_primary;
	stream->is_last_level = is_last_level;
	return &stream->base;
}

/**
 * Start the search. Must be called after *new* methods and
 * before *next* method.
 * @return 0 on success or not 0 on error (diag is set).
 */
static int
vy_write_iterator_start(struct vy_stmt_stream *vstream)
{
	assert(vstream->iface->start == vy_write_iterator_start);
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	struct vy_write_src *src, *tmp;
	rlist_foreach_entry_safe(src, &stream->src_list, in_src_list, tmp) {
		if (vy_write_iterator_add_src(stream, src) != 0)
			return -1;
	}
	return 0;
}

/**
 * Free all resources.
 */
static void
vy_write_iterator_stop(struct vy_stmt_stream *vstream)
{
	assert(vstream->iface->stop == vy_write_iterator_stop);
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	for (int i = 0; i < stream->rv_count; ++i)
		vy_read_view_stmt_destroy(&stream->read_views[i]);
	struct vy_write_src *src, *tmp;
	rlist_foreach_entry_safe(src, &stream->src_list, in_src_list, tmp)
		vy_write_iterator_delete_src(stream, src);
}

/**
 * Delete the iterator.
 */
static void
vy_write_iterator_close(struct vy_stmt_stream *vstream)
{
	assert(vstream->iface->close == vy_write_iterator_close);
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	vy_write_iterator_stop(vstream);
	tuple_format_unref(stream->format);
	free(stream);
}

/**
 * Add a mem as a source of iterator.
 * @return 0 on success or -1 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_new_mem(struct vy_stmt_stream *vstream, struct vy_mem *mem)
{
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	struct vy_write_src *src = vy_write_iterator_new_src(stream);
	if (src == NULL)
		return -1;
	vy_mem_stream_open(&src->mem_stream, mem);
	return 0;
}

/**
 * Add a run slice as a source of iterator.
 * @return 0 on success or -1 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_new_slice(struct vy_stmt_stream *vstream,
			    struct vy_slice *slice)
{
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	struct vy_write_src *src = vy_write_iterator_new_src(stream);
	if (src == NULL)
		return -1;
	vy_slice_stream_open(&src->slice_stream, slice, stream->cmp_def,
			     stream->format, stream->is_primary);
	return 0;
}

/**
 * Go to the next tuple in terms of sorted (merged) input steams.
 * @return 0 on success or not 0 on error (diag is set).
 */
static NODISCARD int
vy_write_iterator_merge_step(struct vy_write_iterator *stream)
{
	struct heap_node *node = vy_source_heap_top(&stream->src_heap);
	assert(node != NULL);
	struct vy_write_src *src = container_of(node, struct vy_write_src,
						heap_node);
	int rc = src->stream.iface->next(&src->stream, &src->tuple);
	if (rc != 0)
		return rc;
	if (src->tuple != NULL)
		vy_source_heap_update(&stream->src_heap, node);
	else
		vy_write_iterator_remove_src(stream, src);
	return 0;
}

/**
 * Try to get VLSN of the read view with the specified number in
 * the vy_write_iterator.read_views array.
 * If the requested read view is older than all existing ones,
 * return 0, as the oldest possible VLSN.
 *
 * @param stream Write iterator.
 * @param current_rv_i Index of the read view.
 *
 * @retval VLSN.
 */
static inline int64_t
vy_write_iterator_get_vlsn(struct vy_write_iterator *stream, int rv_i)
{
	if (rv_i >= stream->rv_count)
		return 0;
	return stream->read_views[rv_i].vlsn;
}

/**
 * Remember the current tuple of the @src as a part of the
 * current read view.
 * @param History objects allocator.
 * @param stream Write iterator.
 * @param src Source of the wanted tuple.
 * @param current_rv_i Index of the current read view.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static inline int
vy_write_iterator_push_rv(struct region *region,
			  struct vy_write_iterator *stream,
			  struct tuple *tuple, int current_rv_i)
{
	assert(current_rv_i < stream->rv_count);
	struct vy_read_view_stmt *rv = &stream->read_views[current_rv_i];
	assert(rv->vlsn >= vy_stmt_lsn(tuple));
	struct vy_write_history *h =
		vy_write_history_new(region, tuple, rv->history);
	if (h == NULL)
		return -1;
	rv->history = h;
	return 0;
}

/**
 * Return the next statement from the current key read view
 * statements sequence. Unref the previous statement, if needed.
 * We can't unref the statement right before returning it to the
 * caller, because reference in the read_views array can be
 * the only one to this statement, e.g. if the statement is
 * read from a disk page.
 *
 * @param stream Write iterator.
 * @retval not NULL Next statement of the current key.
 * @retval     NULL End of the key (not the end of the sources).
 */
static inline struct tuple *
vy_write_iterator_pop_read_view_stmt(struct vy_write_iterator *stream)
{
	struct vy_read_view_stmt *rv;
	if (stream->stmt_i >= 0) {
		/* Destroy the current before getting to the next. */
		rv = &stream->read_views[stream->stmt_i];
		assert(rv->history == NULL);
		vy_read_view_stmt_destroy(rv);
	}
	if (stream->rv_used_count == 0)
		return NULL;
	/* Find a next non-empty history element. */
	do {
		assert(stream->stmt_i + 1 < stream->rv_count);
		stream->stmt_i++;
		rv = &stream->read_views[stream->stmt_i];
		assert(rv->history == NULL);
	} while (rv->tuple == NULL);
	assert(stream->rv_used_count > 0);
	stream->rv_used_count--;
	return rv->tuple;
}

/**
 * Build the history of the current key.
 * Apply optimizations 1, 2 and 3 (@sa vy_write_iterator.h).
 * When building a history, some statements can be
 * skipped (e.g. multiple REPLACE statements on the same key),
 * but nothing can be merged yet, since we don't know the first
 * statement in the history.
 * This is why there is a special "merge" step which applies
 * UPSERTs and builds a tuple for each read view.
 *
 * @param region History objects allocator.
 * @param stream Write iterator.
 * @param[out] count Count of statements saved in the history.
 * @param[out] is_first_insert Set if the oldest statement for
 * the current key among all sources is an INSERT.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static NODISCARD int
vy_write_iterator_build_history(struct region *region,
				struct vy_write_iterator *stream,
				int *count, bool *is_first_insert)
{
	*count = 0;
	*is_first_insert = false;
	assert(stream->stmt_i == -1);
	struct heap_node *node = vy_source_heap_top(&stream->src_heap);
	if (node == NULL)
		return 0; /* no more data */
	struct vy_write_src *src =
		container_of(node, struct vy_write_src, heap_node);
	/* Search must have been started already. */
	assert(src->tuple != NULL);
	/*
	 * A virtual source instance which represents the end on
	 * the current key in the source heap. It is greater
	 * than any statement on the current key and less than
	 * any statement on the next key.
	 * The moment we get this source from the heap we know
	 * that there are no statements that there are no more
	 * statements for the current key.
	 */
	struct vy_write_src end_of_key_src;
	end_of_key_src.is_end_of_key = true;
	end_of_key_src.tuple = src->tuple;
	int rc = vy_source_heap_insert(&stream->src_heap, &end_of_key_src.heap_node);
	if (rc) {
		diag_set(OutOfMemory, sizeof(void *),
			 "malloc", "vinyl write stream heap");
		return rc;
	}
	vy_stmt_ref_if_possible(src->tuple);
	/*
	 * For each pair (merge_until_lsn, current_rv_lsn] build
	 * a history in the corresponding read view.
	 * current_rv_i - index of the current read view.
	 */
	int current_rv_i = 0;
	int64_t current_rv_lsn = vy_write_iterator_get_vlsn(stream, 0);
	int64_t merge_until_lsn = vy_write_iterator_get_vlsn(stream, 1);
	uint64_t key_mask = stream->cmp_def->column_mask;

	while (true) {
		*is_first_insert = vy_stmt_type(src->tuple) == IPROTO_INSERT;

		if (!stream->is_primary &&
		    vy_stmt_type(src->tuple) == IPROTO_REPLACE) {
			/*
			 * If a REPLACE stored in a secondary index was
			 * generated by an update operation, it can be
			 * turned into an INSERT.
			 */
			uint64_t stmt_mask = vy_stmt_column_mask(src->tuple);
			if (stmt_mask != UINT64_MAX &&
			    !key_update_can_be_skipped(stmt_mask, key_mask))
				*is_first_insert = true;
		}

		if (vy_stmt_lsn(src->tuple) > current_rv_lsn) {
			/*
			 * Skip statements invisible to the current read
			 * view but older than the previous read view,
			 * which is already fully built.
			 */
			goto next_lsn;
		}
		while (vy_stmt_lsn(src->tuple) <= merge_until_lsn) {
			/*
			 * Skip read views which see the same
			 * version of the key, until src->tuple is
			 * between merge_until_lsn and
			 * current_rv_lsn.
			 */
			current_rv_i++;
			current_rv_lsn = merge_until_lsn;
			merge_until_lsn =
				vy_write_iterator_get_vlsn(stream,
							   current_rv_i + 1);
		}

		/*
		 * Optimization 1: skip last level delete.
		 * @sa vy_write_iterator for details about this
		 * and other optimizations.
		 */
		if (vy_stmt_type(src->tuple) == IPROTO_DELETE &&
		    stream->is_last_level && merge_until_lsn == 0) {
			current_rv_lsn = 0; /* Force skip */
			goto next_lsn;
		}

		/*
		 * Optimization 2: skip statements overwritten
		 * by a REPLACE or DELETE.
		 */
		if (vy_stmt_type(src->tuple) == IPROTO_REPLACE ||
		    vy_stmt_type(src->tuple) == IPROTO_INSERT ||
		    vy_stmt_type(src->tuple) == IPROTO_DELETE) {
			uint64_t stmt_mask = vy_stmt_column_mask(src->tuple);
			/*
			 * Optimization 3: skip statements which
			 * do not change this secondary key.
			 */
			if (!stream->is_primary &&
			    key_update_can_be_skipped(key_mask, stmt_mask))
				goto next_lsn;

			rc = vy_write_iterator_push_rv(region, stream,
						       src->tuple,
						       current_rv_i);
			if (rc != 0)
				break;
			++*count;
			current_rv_i++;
			current_rv_lsn = merge_until_lsn;
			merge_until_lsn =
				vy_write_iterator_get_vlsn(stream,
							   current_rv_i + 1);
			goto next_lsn;
		}

		assert(vy_stmt_type(src->tuple) == IPROTO_UPSERT);
		rc = vy_write_iterator_push_rv(region, stream, src->tuple,
					       current_rv_i);
		if (rc != 0)
			break;
		++*count;
next_lsn:
		rc = vy_write_iterator_merge_step(stream);
		if (rc != 0)
			break;
		node = vy_source_heap_top(&stream->src_heap);
		assert(node != NULL);
		src = container_of(node, struct vy_write_src, heap_node);
		assert(src->tuple != NULL);
		if (src->is_end_of_key)
			break;
	}

	vy_source_heap_delete(&stream->src_heap, &end_of_key_src.heap_node);
	vy_stmt_unref_if_possible(end_of_key_src.tuple);
	return rc;
}

/**
 * Apply accumulated UPSERTs in the read view with a hint from
 * a previous read view. After merge, the read view must contain
 * one statement.
 *
 * @param stream Write iterator.
 * @param hint   The tuple from a previous read view (can be NULL).
 * @param rv Read view to merge.
 * @param is_first_insert Set if the oldest statement for the
 * current key among all sources is an INSERT.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static NODISCARD int
vy_read_view_merge(struct vy_write_iterator *stream, struct tuple *hint,
		   struct vy_read_view_stmt *rv, bool is_first_insert)
{
	assert(rv != NULL);
	assert(rv->tuple == NULL);
	assert(rv->history != NULL);
	struct vy_write_history *h = rv->history;
	/*
	 * Optimization 5: discard a DELETE statement referenced
	 * by a read view if it is preceded by another DELETE for
	 * the same key.
	 */
	if (hint != NULL && vy_stmt_type(hint) == IPROTO_DELETE &&
	    vy_stmt_type(h->tuple) == IPROTO_DELETE) {
		vy_write_history_destroy(h);
		rv->history = NULL;
		return 0;
	}
	/*
	 * Two possible hints to remove the current UPSERT.
	 * 1. If the stream is working on the last level, we
	 *    know that this UPSERT is the oldest version of
	 *    the key and can convert it into REPLACE.
	 * 2. If the previous read view contains DELETE or
	 *    REPLACE, then the current UPSERT can be applied to
	 *    it, whether is_last_level is true or not.
	 */
	if (vy_stmt_type(h->tuple) == IPROTO_UPSERT &&
	    (stream->is_last_level || (hint != NULL &&
	     vy_stmt_type(hint) != IPROTO_UPSERT))) {
		assert(!stream->is_last_level || hint == NULL ||
		       vy_stmt_type(hint) != IPROTO_UPSERT);
		struct tuple *applied = vy_apply_upsert(h->tuple, hint,
				stream->cmp_def, stream->format, false);
		if (applied == NULL)
			return -1;
		vy_stmt_unref_if_possible(h->tuple);
		h->tuple = applied;
	}
	/* Squash the rest of UPSERTs. */
	struct vy_write_history *result = h;
	h = h->next;
	while (h != NULL) {
		assert(h->tuple != NULL &&
		       vy_stmt_type(h->tuple) == IPROTO_UPSERT);
		assert(result->tuple != NULL);
		struct tuple *applied = vy_apply_upsert(h->tuple, result->tuple,
					stream->cmp_def, stream->format, false);
		if (applied == NULL)
			return -1;
		vy_stmt_unref_if_possible(result->tuple);
		result->tuple = applied;
		vy_stmt_unref_if_possible(h->tuple);
		/*
		 * Don't bother freeing 'h' since it's
		 * allocated on a region.
		 */
		h = h->next;
		result->next = h;
	}
	rv->tuple = result->tuple;
	rv->history = NULL;
	result->tuple = NULL;
	assert(result->next == NULL);
	if (hint != NULL) {
		/* Not the first statement. */
		return 0;
	}
	struct tuple *tuple = rv->tuple;
	if (is_first_insert && vy_stmt_type(tuple) == IPROTO_DELETE) {
		/*
		 * Optimization 6: discard the first DELETE if
		 * the oldest statement for the current key among
		 * all sources is an INSERT and hence there's no
		 * statements for this key in older runs or the
		 * last statement is a DELETE.
		 */
		vy_stmt_unref_if_possible(tuple);
		rv->tuple = NULL;
	}
	if ((is_first_insert && vy_stmt_type(tuple) == IPROTO_REPLACE) ||
	    (!is_first_insert && vy_stmt_type(tuple) == IPROTO_INSERT)) {
		/*
		 * If the oldest statement among all sources is an
		 * INSERT, convert the first REPLACE to an INSERT
		 * so that if the key gets deleted later, we will
		 * be able invoke optimization #6 to discard the
		 * DELETE statement.
		 *
		 * Otherwise convert the first INSERT to a REPLACE
		 * so as not to trigger optimization #6 on the next
		 * compaction.
		 */
		uint32_t size;
		const char *data = tuple_data_range(tuple, &size);
		struct tuple *copy = is_first_insert ?
			vy_stmt_new_insert(stream->format, data, data + size) :
			vy_stmt_new_replace(stream->format, data, data + size);
		if (copy == NULL)
			return -1;
		vy_stmt_set_lsn(copy, vy_stmt_lsn(tuple));
		vy_stmt_unref_if_possible(tuple);
		rv->tuple = copy;
	}
	return 0;
}

/**
 * Split the current key into a sequence of read view
 * statements. @sa struct vy_write_iterator comment for details
 * about the algorithm and optimizations.
 *
 * @param stream Write iterator.
 * @param[out] count Length of the result key versions sequence.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static NODISCARD int
vy_write_iterator_build_read_views(struct vy_write_iterator *stream, int *count)
{
	*count = 0;
	int raw_count;
	bool is_first_insert;
	struct region *region = &fiber()->gc;
	size_t used = region_used(region);
	stream->rv_used_count = 0;
	if (vy_write_iterator_build_history(region, stream, &raw_count,
					    &is_first_insert) != 0)
		goto error;
	if (raw_count == 0) {
		/* A key is fully optimized. */
		region_truncate(region, used);
		return 0;
	}
	/* Find the first non-empty read view. */
	struct vy_read_view_stmt *rv =
		&stream->read_views[stream->rv_count - 1];
	while (rv > &stream->read_views[0] && rv->history == NULL)
		--rv;
	/*
	 * At least one statement has been found, since raw_count
	 * here > 0.
	 */
	assert(rv >= &stream->read_views[0] && rv->history != NULL);
	struct tuple *hint = NULL;
	for (; rv >= &stream->read_views[0]; --rv) {
		if (rv->history == NULL)
			continue;
		if (vy_read_view_merge(stream, hint, rv,
				       is_first_insert) != 0)
			goto error;
		assert(rv->history == NULL);
		if (rv->tuple == NULL)
			continue;
		stream->rv_used_count++;
		++*count;
		hint = rv->tuple;
	}
	region_truncate(region, used);
	return 0;
error:
	region_truncate(region, used);
	return -1;
}

/**
 * Get the next statement to write.
 * The user of the write iterator simply expects a stream
 * of statements to write to the output.
 * The tuple *ret is guaranteed to be valid until next tuple is
 * returned (thus last non-null tuple is valid after EOF).
 *
 * @return 0 on success or not 0 on error (diag is set).
 */
static NODISCARD int
vy_write_iterator_next(struct vy_stmt_stream *vstream,
		       struct tuple **ret)
{
	assert(vstream->iface->next == vy_write_iterator_next);
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	/*
	 * Try to get the next statement from the current key
	 * read view statements sequence.
	 */
	*ret = vy_write_iterator_pop_read_view_stmt(stream);
	if (*ret != NULL)
		return 0;

	/* Build the next key sequence. */
	stream->stmt_i = -1;
	int count = 0;

	while (true) {
		/* Squash UPSERTs and/or go to the next key */
		if (vy_write_iterator_build_read_views(stream, &count) != 0)
			return -1;
		/*
		 * next_key() routine could skip the next key, for
		 * example, if it was truncated by last level
		 * DELETE or it consisted only from optimized
		 * updates. Then try to get the next key.
		 */
		if (count != 0 || stream->src_heap.size == 0)
			break;
	}
	/* Again try to get the statement, after calling next_key(). */
	*ret = vy_write_iterator_pop_read_view_stmt(stream);
	return 0;
}

static const struct vy_stmt_stream_iface vy_slice_stream_iface = {
	.start = vy_write_iterator_start,
	.next = vy_write_iterator_next,
	.stop = vy_write_iterator_stop,
	.close = vy_write_iterator_close
};

