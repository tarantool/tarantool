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

#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"

static bool
heap_less(heap_t *heap, struct heap_node *n1, struct heap_node *n2);

#define HEAP_NAME src_heap
#define HEAP_LESS heap_less
#include "salad/heap.h"

/**
 * Merge source of write iterator. Represents a mem or a run.
 */
struct vy_write_src {
	/* Link in vy_write_iterator::src_list */
	struct rlist in_src_list;
	/* Node in vy_write_iterator::src_heap */
	struct heap_node heap_node;
	/* Current tuple in the source (lowest and with maximal lsn) */
	struct tuple *tuple;
	/**
	 * Is the tuple (@sa tuple) refable or not.
	 * Tuples from mems are reafble, from runs - not
	 */
	bool is_tuple_refable;
	/**
	 * There are special rules of comparison for virtual sources
	 * that represent a delimiter between the current key and
	 * the next key. They must be after (greater than) the sources with
	 * equal key despite of LSN. The flag below mean that the source is
	 * such a virtual source and must be compared correspondingly.
	 */
	bool is_end_of_key;
	/** Source iterator */
	union {
		struct vy_slice_stream slice_stream;
		struct vy_mem_stream mem_stream;
		struct vy_stmt_stream stream;
	};
};

/**
 * Write iterator itself.
 */
struct vy_write_iterator {
	/** Parent class, must be the first member */
	struct vy_stmt_stream base;
	/* List of all sources of the iterator */
	struct rlist src_list;
	/* Heap with sources with the lowest source in head */
	heap_t src_heap;
	/**
	 * Tuple that was returned in the last vy_write_iterator_next call,
	 * or the tuple to be returned in vy_write_iterator_next execution.
	 */
	struct tuple *tuple;
	/**
	 * Is the tuple (member) refable or not.
	 * Tuples from runs are reafable, from mems - not.
	 */
	bool is_tuple_refable;
	/** Index key definition used for storing statements on disk. */
	const struct key_def *key_def;
	/** Format to allocate new REPLACE and DELETE tuples from vy_run */
	struct tuple_format *format;
	/** Same as format, but for UPSERT tuples. */
	struct tuple_format *upsert_format;
	/* The minimal VLSN among all active transactions */
	int64_t oldest_vlsn;
	/* There are is no level older than the one we're writing to. */
	bool is_last_level;
	/** Set if this iterator is for a primary index. */
	bool is_primary;
};

/**
 * Comparator of the heap. Generally compares two sources and finds out
 * whether one source is less than another.
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

	int cmp = tuple_compare(src1->tuple, src2->tuple, stream->key_def);
	if (cmp != 0)
		return cmp < 0;

	/**
	 * Keys are equal, order by lsn, descending.
	 * Virtual sources that represents end-of-key just use 0 as LSN,
	 * so they are after all equal keys automatically.
	 */
	int64_t lsn1 = src1->is_end_of_key  ? 0 : vy_stmt_lsn(src1->tuple);
	int64_t lsn2 = src2->is_end_of_key  ? 0 : vy_stmt_lsn(src2->tuple);
	if (lsn1 != lsn2)
		return lsn1 > lsn2;

	/** LSNs are equal, prioritize terminal (non-upsert) statements */
	return (vy_stmt_type(src1->tuple) == IPROTO_UPSERT ? 1 : 0) <
	       (vy_stmt_type(src2->tuple) == IPROTO_UPSERT ? 1 : 0);

}

/**
 * Allocate a source and put it to the list.
 * The underlying stream (src->stream) must be opened immediately.
 * @param stream - the write iterator.
 * @param is_tuple_refable - true for runs and false for mems.
 * @return the source or NULL on memory error.
 */
static struct vy_write_src *
vy_write_iterator_new_src(struct vy_write_iterator *stream, bool is_tuple_refable)
{
	struct vy_write_src *res = (struct vy_write_src *) malloc(sizeof(*res));
	if (res == NULL) {
		diag_set(OutOfMemory, sizeof(*res),
			 "malloc", "write stream src");
		return NULL;
	}
	res->is_tuple_refable = is_tuple_refable;
	res->is_end_of_key = false;
	rlist_add(&stream->src_list, &res->in_src_list);
	return res;
}


/**
 * Close the stream of the source, remove from list and delete.
 */
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
 * Put the source to the heap. Source's stream must be opened.
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
	rc = src_heap_insert(&stream->src_heap, &src->heap_node);
	if (rc != 0) {
		diag_set(OutOfMemory, sizeof(void *),
			 "malloc", "write stream heap");
		vy_write_iterator_delete_src(stream, src);
		return rc;
	}
	return 0;
}

/**
 * Remove the source from the heap, destroy and free it.
 */
static void
vy_write_iterator_remove_src(struct vy_write_iterator *stream,
			   struct vy_write_src *src)
{
	src_heap_delete(&stream->src_heap, &src->heap_node);
	vy_write_iterator_delete_src(stream, src);
}

static const struct vy_stmt_stream_iface vy_slice_stream_iface;

/**
 * Open an empty write iterator. To add sources to the iterator
 * use vy_write_iterator_add_* functions.
 * @return the iterator or NULL on error (diag is set).
 */
struct vy_stmt_stream *
vy_write_iterator_new(const struct key_def *key_def, struct tuple_format *format,
		      struct tuple_format *upsert_format, bool is_primary,
		      bool is_last_level, int64_t oldest_vlsn)
{
	struct vy_write_iterator *stream =
		(struct vy_write_iterator *) malloc(sizeof(*stream));
	if (stream == NULL) {
		diag_set(OutOfMemory, sizeof(*stream), "malloc", "write stream");
		return NULL;
	}
	stream->base.iface = &vy_slice_stream_iface;
	src_heap_create(&stream->src_heap);
	rlist_create(&stream->src_list);
	stream->key_def = key_def;
	stream->format = format;
	tuple_format_ref(stream->format, 1);
	stream->upsert_format = upsert_format;
	tuple_format_ref(stream->upsert_format, 1);
	stream->is_primary = is_primary;
	stream->oldest_vlsn = oldest_vlsn;
	stream->is_last_level = is_last_level;
	stream->tuple = NULL;
	stream->is_tuple_refable = false;
	return &stream->base;
}

/**
 * Set stream->tuple as a tuple to be output as a result of .._next call.
 * Ref the new tuple if necessary, unref older value if needed.
 * @param stream - the write iterator.
 * @param tuple - the tuple to be saved.
 * @param is_tuple_refable - is the tuple must of must not be referenced.
 */
static void
vy_write_iterator_set_tuple(struct vy_write_iterator *stream,
			    struct tuple *tuple, bool is_tuple_refable)
{
	if (stream->tuple != NULL && tuple != NULL)
		assert(tuple_compare(stream->tuple, tuple, stream->key_def) < 0 ||
			vy_stmt_lsn(stream->tuple) >= vy_stmt_lsn(tuple));

	if (stream->tuple != NULL && stream->is_tuple_refable)
		tuple_unref(stream->tuple);

	stream->tuple = tuple;
	stream->is_tuple_refable = is_tuple_refable;

	if (stream->tuple != NULL && stream->is_tuple_refable)
		tuple_ref(stream->tuple);
}

/**
 * Start the search. Must be called after *add* methods and
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
	vy_write_iterator_set_tuple(stream, NULL, false);
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
	tuple_format_ref(stream->format, -1);
	tuple_format_ref(stream->upsert_format, -1);
	free(stream);
}

/**
 * Add a mem as a source of iterator.
 * @return 0 on success or not 0 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_add_mem(struct vy_stmt_stream *vstream, struct vy_mem *mem)
{
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	struct vy_write_src *src = vy_write_iterator_new_src(stream, false);
	if (src == NULL)
		return -1;
	vy_mem_stream_open(&src->mem_stream, mem);
	return 0;
}

/**
 * Add a run slice as a source of iterator.
 * @return 0 on success or not 0 on error (diag is set).
 */
NODISCARD int
vy_write_iterator_add_slice(struct vy_stmt_stream *vstream,
			    struct vy_slice *slice, struct vy_run_env *run_env)
{
	struct vy_write_iterator *stream = (struct vy_write_iterator *)vstream;
	struct vy_write_src *src = vy_write_iterator_new_src(stream, true);
	if (src == NULL)
		return -1;
	vy_slice_stream_open(&src->slice_stream, slice, stream->key_def,
			     stream->format, stream->upsert_format, run_env,
			     stream->is_primary);
	return 0;
}

/**
 * Go to next tuple in terms of sorted (merged) input steams.
 * @return 0 on success or not 0 on error (diag is set).
 */
static NODISCARD int
vy_write_iterator_merge_step(struct vy_write_iterator *stream)
{
	struct heap_node *node = src_heap_top(&stream->src_heap);
	assert(node != NULL);
	struct vy_write_src *src = container_of(node, struct vy_write_src,
						heap_node);
	int rc = src->stream.iface->next(&src->stream, &src->tuple);
	if (rc != 0)
		return rc;
	if (src->tuple != NULL)
		src_heap_update(&stream->src_heap, node);
	else
		vy_write_iterator_remove_src(stream, src);
	return 0;
}

/**
 * Squash in the single statement all rest statements of current key
 * starting from the current statement.
 */
static NODISCARD int
vy_write_iterator_next_key(struct vy_write_iterator *stream)
{
	assert(stream->tuple != NULL);
	/*
	 * A virtual source instance that represents the end on current key in
	 * source heap. Due to a special branch in heap's comparator the
	 * source will come into the heap head after all equal to the current
	 * key statement but before any greater statement. Having inserted
	 * the source to the heap, the moment we get this source from the heap
	 * signals that there are no statements that are equal to the current.
	 */
	struct vy_write_src end_of_key_src;
	end_of_key_src.is_end_of_key = true;
	end_of_key_src.tuple = stream->tuple;
	int rc = src_heap_insert(&stream->src_heap, &end_of_key_src.heap_node);
	if (rc) {
		diag_set(OutOfMemory, sizeof(void *),
			 "malloc", "write stream heap");
		return rc;
	}

	while (true) {
		struct heap_node *node = src_heap_top(&stream->src_heap);
		assert(node != NULL);
		struct vy_write_src *src =
			container_of(node, struct vy_write_src, heap_node);
		assert(src->tuple != NULL); /* Is search started? */

		if (vy_stmt_type(stream->tuple) == IPROTO_UPSERT &&
		    (!src->is_end_of_key || stream->is_last_level)) {
			const struct tuple *apply_to =
				src->is_end_of_key ? NULL : src->tuple;
			struct tuple *applied =
				vy_apply_upsert(stream->tuple, apply_to,
						stream->key_def, stream->format,
						stream->upsert_format, false);
			if (applied == NULL) {
				rc = -1;
				break;
			}
			vy_write_iterator_set_tuple(stream, applied, true);
			/* refresh tuple in virtual source */
			end_of_key_src.tuple = stream->tuple;
		}

		if (src->is_end_of_key)
			break;

		rc = vy_write_iterator_merge_step(stream);
		if (rc != 0)
			break;
	}

	src_heap_delete(&stream->src_heap, &end_of_key_src.heap_node);
	return rc;
}

/**
 * Get the next statement to write.
 * The user of the write iterator simply expects a stream
 * of statements to write to the output.
 * The tuple *ret is guaranteed to be valid until next tuple is
 *  returned (thus last non-null tuple is valid after EOF).
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
	 * Nullify the result stmt. If the next stmt is not
	 * found, this would be a marker of the end of the stream.
	 */
	*ret = NULL;

	while (true) {
		struct heap_node *node = src_heap_top(&stream->src_heap);
		if (node == NULL)
			return 0; /* no more data */
		struct vy_write_src *src =
			container_of(node, struct vy_write_src, heap_node);
		assert(src->tuple != NULL); /* Is search started? */
		vy_write_iterator_set_tuple(stream, src->tuple,
					    src->is_tuple_refable);

		int rc = vy_write_iterator_merge_step(stream);
		if (rc != 0)
			return -1;

		if (vy_stmt_lsn(stream->tuple) > stream->oldest_vlsn)
			break; /* Save the current stmt as the result. */

		if (vy_stmt_type(stream->tuple) == IPROTO_REPLACE ||
		    vy_stmt_type(stream->tuple) == IPROTO_DELETE) {
			/*
			 * If the tuple has extra size - it has
			 * column mask of an update operation.
			 * The tuples from secondary indexes
			 * which don't modify its keys can be
			 * skipped during dump,
			 * @sa vy_can_skip_update().
			 */
			if (!stream->is_primary &&
			    key_update_can_be_skipped(stream->key_def->column_mask,
					       vy_stmt_column_mask(stream->tuple)))
				continue;
		}

		/* Squash upserts and/or go to the next key */
		rc = vy_write_iterator_next_key(stream);
		if (rc != 0)
			return -1;

		if (vy_stmt_type(stream->tuple) == IPROTO_DELETE &&
		    stream->is_last_level)
			continue; /* Skip unnecessary DELETE */
		break;
	}
	*ret = stream->tuple;
	return 0;
}

static const struct vy_stmt_stream_iface vy_slice_stream_iface = {
	.start = vy_write_iterator_start,
	.next = vy_write_iterator_next,
	.stop = vy_write_iterator_stop,
	.close = vy_write_iterator_close
};

