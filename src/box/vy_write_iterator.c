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
#include "fiber.h"

#define HEAP_FORWARD_DECLARATION
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
	struct vy_entry entry;
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

static bool
heap_less(heap_t *heap, struct vy_write_src *src1, struct vy_write_src *src2);

#define HEAP_NAME vy_source_heap
#define HEAP_LESS heap_less
#define heap_value_t struct vy_write_src
#define heap_value_attr heap_node
#include "salad/heap.h"

/**
 * A sequence of versions of a key, sorted by LSN in ascending order.
 * (history->entry.stmt.lsn < history->next->entry.stmt.lsn).
 */
struct vy_write_history {
	/** Next version with greater LSN. */
	struct vy_write_history *next;
	/** Key. */
	struct vy_entry entry;
};

/**
 * Create a new vy_write_history object, save a statement into it
 * and link with a newer version. This function effectively
 * reverses key LSN order from newest first to oldest first, i.e.
 * orders statements on the same key chronologically.
 *
 * @param entry Key version.
 * @param next Next version of the key.
 *
 * @retval not NULL Created object.
 * @retval NULL     Memory error.
 */
static inline struct vy_write_history *
vy_write_history_new(struct vy_entry entry, struct vy_write_history *next)
{
	struct vy_write_history *h;
	int size;
	h = region_alloc_object(&fiber()->gc, struct vy_write_history, &size);
	if (h == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_object", "h");
		return NULL;
	}
	h->entry = entry;
	assert(next == NULL || (next->entry.stmt != NULL &&
	       vy_stmt_lsn(next->entry.stmt) > vy_stmt_lsn(entry.stmt)));
	h->next = next;
	vy_stmt_ref_if_possible(entry.stmt);
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
		if (history->entry.stmt != NULL)
			vy_stmt_unref_if_possible(history->entry.stmt);
		history = history->next;
	} while (history != NULL);
}

/** Read view of a key. */
struct vy_read_view_stmt {
	/** Read view LSN. */
	int64_t vlsn;
	/** Result key version, visible to this @vlsn. */
	struct vy_entry entry;
	/**
	 * A history of changes building up to this read
	 * view. Once built, it is merged into a single
	 * @entry.
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
	if (rv->entry.stmt != NULL)
		vy_stmt_unref_if_possible(rv->entry.stmt);
	rv->entry = vy_entry_none();
	/*
	 * History must be already cleaned up in
	 * vy_write_iterator_build_read_views().
	 */
	assert(rv->history == NULL);
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
	struct key_def *cmp_def;
	/* There is no LSM tree level older than the one we're writing to. */
	bool is_last_level;
	/**
	 * Set if this iterator is for a primary index.
	 * Not all implementation are applicable to the primary
	 * key and its tuple format is different.
	 */
	bool is_primary;
	/** Deferred DELETE handler. */
	struct vy_deferred_delete_handler *deferred_delete_handler;
	/**
	 * Last scanned REPLACE or DELETE statement that was
	 * inserted into the primary index without deletion
	 * of the old tuple from secondary indexes.
	 */
	struct vy_entry deferred_delete;
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
	 * Last statement returned to the caller, pinned in memory.
	 */
	struct vy_entry last;
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
heap_less(heap_t *heap, struct vy_write_src *src1, struct vy_write_src *src2)
{
	struct vy_write_iterator *stream =
		container_of(heap, struct vy_write_iterator, src_heap);

	int cmp = vy_entry_compare(src1->entry, src2->entry, stream->cmp_def);
	if (cmp != 0)
		return cmp < 0;

	/**
	 * Keys are equal, order by LSN, descending.
	 * Virtual sources use 0 for LSN, so they are ordered
	 * last automatically.
	 */
	int64_t lsn1 = src1->is_end_of_key  ? 0 : vy_stmt_lsn(src1->entry.stmt);
	int64_t lsn2 = src2->is_end_of_key  ? 0 : vy_stmt_lsn(src2->entry.stmt);
	if (lsn1 != lsn2)
		return lsn1 > lsn2;

	/*
	 * LSNs are equal. This may only happen if one of the statements
	 * is a deferred DELETE and the overwritten tuple which it is
	 * supposed to purge has the same key parts as the REPLACE that
	 * overwrote it. Discard the deferred DELETE as the overwritten
	 * tuple will be (or has already been) purged by the REPLACE.
	 */
	return (vy_stmt_type(src1->entry.stmt) == IPROTO_DELETE ? 1 : 0) <
	       (vy_stmt_type(src2->entry.stmt) == IPROTO_DELETE ? 1 : 0);

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
	heap_node_create(&res->heap_node);
	res->entry = vy_entry_none();
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
	if (src->stream.iface->close != NULL)
		src->stream.iface->close(&src->stream);
	rlist_del(&src->in_src_list);
	free(src);
}

/**
 * Start iteration in the given source, retrieve the first tuple,
 * and add the source to the write iterator heap.
 *
 * @return 0 - success, not 0 - error.
 */
static NODISCARD int
vy_write_iterator_add_src(struct vy_write_iterator *stream,
			  struct vy_write_src *src)
{
	if (src->stream.iface->start != NULL) {
		int rc = src->stream.iface->start(&src->stream);
		if (rc != 0)
			return rc;
	}
	int rc = src->stream.iface->next(&src->stream, &src->entry);
	if (rc != 0 || src->entry.stmt == NULL)
		goto stop;

	rc = vy_source_heap_insert(&stream->src_heap, src);
	if (rc != 0) {
		diag_set(OutOfMemory, sizeof(void *),
			 "malloc", "vinyl write stream heap");
		goto stop;
	}
	return 0;
stop:
	if (src->stream.iface->stop != NULL)
		src->stream.iface->stop(&src->stream);
	return rc;
}

/**
 * Remove a source from the heap and stop iteration.
 */
static void
vy_write_iterator_remove_src(struct vy_write_iterator *stream,
			   struct vy_write_src *src)
{
	if (heap_node_is_stray(&src->heap_node))
		return; /* already removed */
	vy_source_heap_delete(&stream->src_heap, src);
	if (src->stream.iface->stop != NULL)
		src->stream.iface->stop(&src->stream);
}

static const struct vy_stmt_stream_iface vy_slice_stream_iface;

/**
 * Open an empty write iterator. To add sources to the iterator
 * use vy_write_iterator_add_* functions.
 * @return the iterator or NULL on error (diag is set).
 */
struct vy_stmt_stream *
vy_write_iterator_new(struct key_def *cmp_def, bool is_primary,
		      bool is_last_level, struct rlist *read_views,
		      struct vy_deferred_delete_handler *handler)
{
	/*
	 * Deferred DELETE statements can only be produced by
	 * primary index compaction.
	 */
	assert(is_primary || handler == NULL);
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
	stream->read_views[0].entry = vy_entry_none();
	count--;
	struct vy_read_view *rv;
	/* Descending order. */
	rlist_foreach_entry(rv, read_views, in_read_views) {
		struct vy_read_view_stmt *p;
		p = &stream->read_views[count--];
		p->vlsn = rv->vlsn;
		p->entry = vy_entry_none();
	}
	assert(count == 0);

	stream->base.iface = &vy_slice_stream_iface;
	vy_source_heap_create(&stream->src_heap);
	rlist_create(&stream->src_list);
	stream->cmp_def = cmp_def;
	stream->is_primary = is_primary;
	stream->is_last_level = is_last_level;
	stream->deferred_delete_handler = handler;
	stream->deferred_delete = vy_entry_none();
	stream->last = vy_entry_none();
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
	struct vy_write_src *src;
	rlist_foreach_entry(src, &stream->src_list, in_src_list) {
		if (vy_write_iterator_add_src(stream, src) != 0)
			goto fail;
#ifndef NDEBUG
		struct errinj *inj =
			errinj(ERRINJ_VY_WRITE_ITERATOR_START_FAIL, ERRINJ_BOOL);
		if (inj != NULL && inj->bparam) {
			inj->bparam = false;
			diag_set(OutOfMemory, 666, "malloc", "struct vy_stmt");
			goto fail;
		}
#endif
	}
	return 0;
fail:
	rlist_foreach_entry(src, &stream->src_list, in_src_list)
		vy_write_iterator_remove_src(stream, src);
	return -1;
}

/**
 * Stop iteration in all sources.
 */
static void
vy_write_iterator_stop(struct vy_stmt_stream *vstream)
{
	assert(vstream->iface->stop == vy_write_iterator_stop);
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	for (int i = 0; i < stream->rv_count; ++i)
		vy_read_view_stmt_destroy(&stream->read_views[i]);
	struct vy_write_src *src;
	rlist_foreach_entry(src, &stream->src_list, in_src_list)
		vy_write_iterator_remove_src(stream, src);
	if (stream->last.stmt != NULL) {
		vy_stmt_unref_if_possible(stream->last.stmt);
		stream->last = vy_entry_none();
	}
	if (stream->deferred_delete.stmt != NULL) {
		vy_stmt_unref_if_possible(stream->deferred_delete.stmt);
		stream->deferred_delete = vy_entry_none();
	}
	struct vy_deferred_delete_handler *handler =
			stream->deferred_delete_handler;
	if (handler != NULL) {
		handler->iface->destroy(handler);
		stream->deferred_delete_handler = NULL;
	}
}

/**
 * Delete the iterator.
 */
static void
vy_write_iterator_close(struct vy_stmt_stream *vstream)
{
	assert(vstream->iface->close == vy_write_iterator_close);
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	struct vy_write_src *src, *tmp;
	rlist_foreach_entry_safe(src, &stream->src_list, in_src_list, tmp)
		vy_write_iterator_delete_src(stream, src);
	vy_source_heap_destroy(&stream->src_heap);
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
			    struct vy_slice *slice,
			    struct tuple_format *disk_format)
{
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	struct vy_write_src *src = vy_write_iterator_new_src(stream);
	if (src == NULL)
		return -1;
	vy_slice_stream_open(&src->slice_stream, slice, stream->cmp_def,
			     disk_format);
	return 0;
}

/**
 * Go to the next tuple in terms of sorted (merged) input steams.
 * @return 0 on success or not 0 on error (diag is set).
 */
static NODISCARD int
vy_write_iterator_merge_step(struct vy_write_iterator *stream)
{
	struct vy_write_src *src = vy_source_heap_top(&stream->src_heap);
	assert(src != NULL);
	int rc = src->stream.iface->next(&src->stream, &src->entry);
	if (rc != 0)
		return rc;
	if (src->entry.stmt != NULL)
		vy_source_heap_update(&stream->src_heap, src);
	else
		vy_write_iterator_remove_src(stream, src);
	return 0;
}

/**
 * Try to get VLSN of the read view with the specified number in
 * the vy_write_iterator.read_views array.
 * If the requested read view is older than all existing ones,
 * return -1, which is less than any possible VLSN.
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
		return -1;
	return stream->read_views[rv_i].vlsn;
}

/**
 * Remember a statement as a part of the current read view.
 * @param stream Write iterator.
 * @param entry The statement.
 * @param current_rv_i Index of the current read view.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static inline int
vy_write_iterator_push_rv(struct vy_write_iterator *stream,
			  struct vy_entry entry, int current_rv_i)
{
	assert(current_rv_i < stream->rv_count);
	struct vy_read_view_stmt *rv = &stream->read_views[current_rv_i];
	assert(rv->vlsn >= vy_stmt_lsn(entry.stmt));
	struct vy_write_history *h =
		vy_write_history_new(entry, rv->history);
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
static inline struct vy_entry
vy_write_iterator_pop_read_view_stmt(struct vy_write_iterator *stream)
{
	struct vy_read_view_stmt *rv;
	if (stream->rv_used_count == 0)
		return vy_entry_none();
	/* Find a next non-empty history element. */
	do {
		assert(stream->stmt_i + 1 < stream->rv_count);
		stream->stmt_i++;
		rv = &stream->read_views[stream->stmt_i];
		assert(rv->history == NULL);
	} while (rv->entry.stmt == NULL);
	assert(stream->rv_used_count > 0);
	stream->rv_used_count--;
	if (stream->last.stmt != NULL)
		vy_stmt_unref_if_possible(stream->last.stmt);
	stream->last = rv->entry;
	rv->entry = vy_entry_none();
	return stream->last;
}

/**
 * Generate a DELETE statement for the given tuple if its
 * deletion from secondary indexes was deferred.
 *
 * @param stream Write iterator.
 * @param entry Current statement.
 *
 * @retval  0 Success.
 * @retval -1 Error.
 */
static int
vy_write_iterator_deferred_delete(struct vy_write_iterator *stream,
				  struct vy_entry entry)
{
	struct tuple *stmt = entry.stmt;
	/*
	 * UPSERTs cannot change secondary index parts neither
	 * can they produce deferred DELETEs, so we skip them.
	 */
	if (vy_stmt_type(stmt) == IPROTO_UPSERT) {
		assert((vy_stmt_flags(stmt) & VY_STMT_DEFERRED_DELETE) == 0);
		return 0;
	}
	/*
	 * Invoke the callback to generate a deferred DELETE
	 * in case the current tuple was overwritten.
	 */
	if (stream->deferred_delete.stmt != NULL) {
		struct vy_deferred_delete_handler *handler =
				stream->deferred_delete_handler;
		if (handler != NULL && vy_stmt_type(stmt) != IPROTO_DELETE &&
		    handler->iface->process(handler, stmt,
					    stream->deferred_delete.stmt) != 0)
			return -1;
		vy_stmt_unref_if_possible(stream->deferred_delete.stmt);
		stream->deferred_delete = vy_entry_none();
	}
	/*
	 * Remember the current statement if it is marked with
	 * VY_STMT_DEFERRED_DELETE so that we can use it to
	 * generate a DELETE for the overwritten tuple when this
	 * function is called next time.
	 */
	if ((vy_stmt_flags(stmt) & VY_STMT_DEFERRED_DELETE) != 0) {
		assert(vy_stmt_type(stmt) == IPROTO_DELETE ||
		       vy_stmt_type(stmt) == IPROTO_REPLACE);
		vy_stmt_ref_if_possible(stmt);
		stream->deferred_delete = entry;
	}
	return 0;
}

/**
 * Build the history of the current key.
 * Apply optimizations 1 and 2 (@sa vy_write_iterator.h).
 * When building a history, some statements can be
 * skipped (e.g. multiple REPLACE statements on the same key),
 * but nothing can be merged yet, since we don't know the first
 * statement in the history.
 * This is why there is a special "merge" step which applies
 * UPSERTs and builds a tuple for each read view.
 *
 * @param stream Write iterator.
 * @param[out] count Count of statements saved in the history.
 * @param[out] is_first_insert Set if the oldest statement for
 * the current key among all sources is an INSERT.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static NODISCARD int
vy_write_iterator_build_history(struct vy_write_iterator *stream,
				int *count, bool *is_first_insert)
{
	*count = 0;
	*is_first_insert = false;
	assert(stream->stmt_i == -1);
	assert(stream->deferred_delete.stmt == NULL);
	struct vy_write_src *src = vy_source_heap_top(&stream->src_heap);
	if (src == NULL)
		return 0; /* no more data */
	/* Search must have been started already. */
	assert(src->entry.stmt != NULL);
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
	end_of_key_src.entry = src->entry;
	int rc = vy_source_heap_insert(&stream->src_heap, &end_of_key_src);
	if (rc) {
		diag_set(OutOfMemory, sizeof(void *),
			 "malloc", "vinyl write stream heap");
		return rc;
	}
	vy_stmt_ref_if_possible(src->entry.stmt);
	/*
	 * For each pair (merge_until_lsn, current_rv_lsn] build
	 * a history in the corresponding read view.
	 * current_rv_i - index of the current read view.
	 */
	int current_rv_i = 0;
	int64_t current_rv_lsn = vy_write_iterator_get_vlsn(stream, 0);
	int64_t merge_until_lsn = vy_write_iterator_get_vlsn(stream, 1);

	while (true) {
		*is_first_insert = vy_stmt_type(src->entry.stmt) == IPROTO_INSERT;

		if (!stream->is_primary &&
		    (vy_stmt_flags(src->entry.stmt) & VY_STMT_UPDATE) != 0) {
			/*
			 * If a REPLACE stored in a secondary index was
			 * generated by an update operation, it can be
			 * turned into an INSERT.
			 */
			*is_first_insert = true;
		}

		/*
		 * Even if the deferred DELETE handler is unset, as it is
		 * the case for dump, we still have to preserve the oldest
		 * statement marked with VY_STMT_DEFERRED_DELETE for each
		 * key in a primary indexes so that we can generate a
		 * deferred DELETE on the next compaction.
		 *
		 * For secondary indexes, we don't need to do that so
		 * we skip the function call below.
		 */
		if (stream->is_primary) {
			rc = vy_write_iterator_deferred_delete(stream,
							       src->entry);
			if (rc != 0)
				break;
		}

		if (vy_stmt_lsn(src->entry.stmt) > current_rv_lsn) {
			/*
			 * Skip statements invisible to the current read
			 * view but older than the previous read view,
			 * which is already fully built.
			 */
			goto next_lsn;
		}
		while (vy_stmt_lsn(src->entry.stmt) <= merge_until_lsn) {
			/*
			 * Skip read views which see the same
			 * version of the key, until src->entry is
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
		if (vy_stmt_type(src->entry.stmt) == IPROTO_DELETE &&
		    stream->is_last_level && merge_until_lsn < 0) {
			current_rv_lsn = -1; /* Force skip */
			goto next_lsn;
		}

		rc = vy_write_iterator_push_rv(stream, src->entry,
					       current_rv_i);
		if (rc != 0)
			break;
		++*count;

		/*
		 * Optimization 2: skip statements overwritten
		 * by a REPLACE or DELETE.
		 */
		if (vy_stmt_type(src->entry.stmt) == IPROTO_REPLACE ||
		    vy_stmt_type(src->entry.stmt) == IPROTO_INSERT ||
		    vy_stmt_type(src->entry.stmt) == IPROTO_DELETE) {
			current_rv_i++;
			current_rv_lsn = merge_until_lsn;
			merge_until_lsn =
				vy_write_iterator_get_vlsn(stream,
							   current_rv_i + 1);
		}
next_lsn:
		rc = vy_write_iterator_merge_step(stream);
		if (rc != 0)
			break;
		src = vy_source_heap_top(&stream->src_heap);
		assert(src != NULL);
		assert(src->entry.stmt != NULL);
		if (src->is_end_of_key)
			break;
	}

	/*
	 * No point in keeping the last VY_STMT_DEFERRED_DELETE
	 * statement around if this is major compaction, because
	 * there's no tuple it could overwrite.
	 */
	if (rc == 0 && stream->is_last_level &&
	    stream->deferred_delete.stmt != NULL) {
		vy_stmt_unref_if_possible(stream->deferred_delete.stmt);
		stream->deferred_delete = vy_entry_none();
	}

	vy_source_heap_delete(&stream->src_heap, &end_of_key_src);
	vy_stmt_unref_if_possible(end_of_key_src.entry.stmt);
	return rc;
}

/**
 * Apply accumulated UPSERTs in the read view with a hint from
 * a previous read view. After merge, the read view must contain
 * one statement.
 *
 * @param stream Write iterator.
 * @param prev Statement from the previous read view (can be NULL).
 * @param rv Read view to merge.
 * @param is_first_insert Set if the oldest statement for the
 * current key among all sources is an INSERT.
 *
 * @retval  0 Success.
 * @retval -1 Memory error.
 */
static NODISCARD int
vy_read_view_merge(struct vy_write_iterator *stream, struct vy_entry prev,
		   struct vy_read_view_stmt *rv, bool is_first_insert)
{
	assert(rv != NULL);
	assert(rv->entry.stmt == NULL);
	assert(rv->history != NULL);
	struct vy_write_history *h = rv->history;
	/*
	 * Optimization 4: discard a DELETE statement referenced
	 * by a read view if it is preceded by another DELETE for
	 * the same key.
	 */
	if (prev.stmt != NULL &&
	    vy_stmt_type(prev.stmt) == IPROTO_DELETE &&
	    vy_stmt_type(h->entry.stmt) == IPROTO_DELETE) {
		vy_write_history_destroy(h);
		rv->history = NULL;
		return 0;
	}
#ifndef NDEBUG
	struct errinj *inj =
		errinj(ERRINJ_VY_READ_VIEW_MERGE_FAIL, ERRINJ_BOOL);
	if (inj != NULL && inj->bparam) {
		inj->bparam = false;
		diag_set(OutOfMemory, 666, "malloc", "struct vy_stmt");
		return -1;
	}
#endif
	/*
	 * Two possible hints to remove the current UPSERT.
	 * 1. If the stream is working on the last level, we
	 *    know that this UPSERT is the oldest version of
	 *    the key and can convert it into REPLACE.
	 * 2. If the previous read view contains DELETE or
	 *    REPLACE, then the current UPSERT can be applied to
	 *    it, whether is_last_level is true or not.
	 */
	if (vy_stmt_type(h->entry.stmt) == IPROTO_UPSERT &&
	    (stream->is_last_level || (prev.stmt != NULL &&
	     vy_stmt_type(prev.stmt) != IPROTO_UPSERT))) {
		assert(!stream->is_last_level || prev.stmt == NULL ||
		       vy_stmt_type(prev.stmt) != IPROTO_UPSERT);
		struct vy_entry applied;
		applied = vy_entry_apply_upsert(h->entry, prev,
						stream->cmp_def, false);
		if (applied.stmt == NULL)
			return -1;
		vy_stmt_unref_if_possible(h->entry.stmt);
		h->entry = applied;
	}
	/* Squash the rest of UPSERTs. */
	struct vy_write_history *result = h;
	h = h->next;
	while (h != NULL) {
		assert(h->entry.stmt != NULL &&
		       vy_stmt_type(h->entry.stmt) == IPROTO_UPSERT);
		assert(result->entry.stmt != NULL);
		struct vy_entry applied;
		applied = vy_entry_apply_upsert(h->entry, result->entry,
						stream->cmp_def, false);
		if (applied.stmt == NULL)
			return -1;
		vy_stmt_unref_if_possible(result->entry.stmt);
		result->entry = applied;
		vy_stmt_unref_if_possible(h->entry.stmt);
		/*
		 * Don't bother freeing 'h' since it's
		 * allocated on a region.
		 */
		h = h->next;
		result->next = h;
	}
	rv->entry = result->entry;
	rv->history = NULL;
	result->entry = vy_entry_none();
	assert(result->next == NULL);
	/*
	 * The write iterator generates deferred DELETEs for all
	 * VY_STMT_DEFERRED_DELETE statements, except, may be,
	 * the last seen one. Clear the flag for all other output
	 * statements so as not to generate the same DELETEs on
	 * the next compaction.
	 */
	uint8_t flags = vy_stmt_flags(rv->entry.stmt);
	if ((flags & VY_STMT_DEFERRED_DELETE) != 0 &&
	    !vy_entry_is_equal(rv->entry, stream->deferred_delete)) {
		if (!vy_stmt_is_refable(rv->entry.stmt)) {
			rv->entry.stmt = vy_stmt_dup(rv->entry.stmt);
			if (rv->entry.stmt == NULL)
				return -1;
		}
		vy_stmt_set_flags(rv->entry.stmt,
				  flags & ~VY_STMT_DEFERRED_DELETE);
	}
	if (prev.stmt != NULL) {
		/* Not the first statement. */
		return 0;
	}
	if (is_first_insert && vy_stmt_type(rv->entry.stmt) == IPROTO_DELETE) {
		/*
		 * Optimization 5: discard the first DELETE if
		 * the oldest statement for the current key among
		 * all sources is an INSERT and hence there's no
		 * statements for this key in older runs or the
		 * last statement is a DELETE.
		 */
		vy_stmt_unref_if_possible(rv->entry.stmt);
		rv->entry = vy_entry_none();
	} else if ((is_first_insert &&
		    vy_stmt_type(rv->entry.stmt) == IPROTO_REPLACE) ||
		   (!is_first_insert &&
		    vy_stmt_type(rv->entry.stmt) == IPROTO_INSERT)) {
		/*
		 * If the oldest statement among all sources is an
		 * INSERT, convert the first REPLACE to an INSERT
		 * so that if the key gets deleted later, we will
		 * be able invoke optimization #5 to discard the
		 * DELETE statement.
		 *
		 * Otherwise convert the first INSERT to a REPLACE
		 * so as not to trigger optimization #5 on the next
		 * compaction.
		 */
		struct tuple *copy = vy_stmt_dup(rv->entry.stmt);
		if (is_first_insert)
			vy_stmt_set_type(copy, IPROTO_INSERT);
		else
			vy_stmt_set_type(copy, IPROTO_REPLACE);
		if (copy == NULL)
			return -1;
		vy_stmt_set_lsn(copy, vy_stmt_lsn(rv->entry.stmt));
		vy_stmt_unref_if_possible(rv->entry.stmt);
		rv->entry.stmt = copy;
	}
	return 0;
}

/**
 * Clean up all histories related to given write iterator.
 * Particular history is allocated using region, so single
 * region truncation is enough to release all memory at once.
 * Before that we should also unref tuples stored in those
 * histories (which is done in vy_write_history_destroy()).
 */
static void
vy_write_iterator_history_destroy(struct vy_write_iterator *stream,
				  struct region *region, size_t used)
{
	for (int i = 0; i < stream->rv_count; ++i) {
		if (stream->read_views[i].history != NULL)
			vy_write_history_destroy(stream->read_views[i].history);
		stream->read_views[i].history = NULL;
	}
	region_truncate(region, used);
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
	int rc = 0;
	if (vy_write_iterator_build_history(stream, &raw_count,
					    &is_first_insert) != 0) {
		rc = -1;
		goto cleanup;
	}
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
	struct vy_entry prev = vy_entry_none();
	for (; rv >= &stream->read_views[0]; --rv) {
		if (rv->history == NULL)
			continue;
		if (vy_read_view_merge(stream, prev, rv, is_first_insert) != 0) {
			rc = -1;
			goto cleanup;
		}
		assert(rv->history == NULL);
		if (rv->entry.stmt == NULL)
			continue;
		stream->rv_used_count++;
		++*count;
		prev = rv->entry;
	}

cleanup:
	vy_write_iterator_history_destroy(stream, region, used);
	return rc;
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
vy_write_iterator_next(struct vy_stmt_stream *vstream, struct vy_entry *ret)
{
	assert(vstream->iface->next == vy_write_iterator_next);
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	/*
	 * Try to get the next statement from the current key
	 * read view statements sequence.
	 */
	*ret = vy_write_iterator_pop_read_view_stmt(stream);
	if (ret->stmt != NULL)
		return 0;
	/*
	 * If we didn't generate a deferred DELETE corresponding to
	 * the last seen VY_STMT_DEFERRED_DELETE statement, we must
	 * include it into the output, because there still might be
	 * an overwritten tuple in an older source.
	 */
	if (stream->deferred_delete.stmt != NULL) {
		if (vy_entry_is_equal(stream->deferred_delete, stream->last)) {
			/*
			 * The statement was returned via a read view.
			 * Nothing to do.
			 */
			vy_stmt_unref_if_possible(stream->deferred_delete.stmt);
			stream->deferred_delete = vy_entry_none();
		} else {
			if (stream->last.stmt != NULL)
				vy_stmt_unref_if_possible(stream->last.stmt);
			*ret = stream->last = stream->deferred_delete;
			stream->deferred_delete = vy_entry_none();
			return 0;
		}
	}

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

