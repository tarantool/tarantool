/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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

#include "box/merger.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"

#include "diag.h"             /* diag_set() */
#include "box/tuple.h"        /* tuple_ref(), tuple_unref(),
				 tuple_validate() */
#include "box/tuple_format.h" /* box_tuple_format_new(),
				 tuple_format_*() */
#include "box/key_def.h"      /* key_def_*(),
				 tuple_compare() */

/* {{{ Merger */

/**
 * Holds a source to fetch next tuples and a last fetched tuple to
 * compare the node against other nodes.
 *
 * The main reason why this structure is separated from a merge
 * source is that a heap node can not be a member of several
 * heaps.
 *
 * The second reason is that it allows to encapsulate all heap
 * related logic inside this compilation unit, without any traces
 * in externally visible structures.
 */
struct merger_heap_node {
	/* A source of tuples. */
	struct merge_source *source;
	/*
	 * A last fetched (refcounted) tuple to compare against
	 * other nodes.
	 */
	struct tuple *tuple;
	/* An anchor to make the structure a merger heap node. */
	struct heap_node in_merger;
};

static bool
merge_source_less(const heap_t *heap, const struct merger_heap_node *left,
		  const struct merger_heap_node *right);
#define HEAP_NAME merger_heap
#define HEAP_LESS merge_source_less
#define heap_value_t struct merger_heap_node
#define heap_value_attr in_merger
#include "salad/heap.h"
#undef HEAP_NAME
#undef HEAP_LESS
#undef heap_value_t
#undef heap_value_attr

/**
 * Holds a heap, parameters of a merge process and utility fields.
 */
struct merger {
	/* A merger is a source. */
	struct merge_source base;
	/*
	 * Whether a merge process started.
	 *
	 * The merger postpones charging of heap nodes until a
	 * first output tuple is acquired.
	 */
	bool started;
	/* A key_def to compare tuples. */
	struct key_def *key_def;
	/* A format to acquire compatible tuples from sources. */
	struct tuple_format *format;
	/*
	 * A heap of sources (of nodes that contains a source to
	 * be exact).
	 */
	heap_t heap;
	/* An array of heap nodes. */
	uint32_t node_count;
	struct merger_heap_node *nodes;
	/* Ascending (false) / descending (true) order. */
	bool reverse;
};

/* Helpers */

/**
 * Data comparing function to construct a heap of sources.
 */
static bool
merge_source_less(const heap_t *heap, const struct merger_heap_node *left,
		  const struct merger_heap_node *right)
{
	assert(left->tuple != NULL);
	assert(right->tuple != NULL);
	struct merger *merger = container_of(heap, struct merger, heap);
	int cmp = tuple_compare(left->tuple, HINT_NONE, right->tuple, HINT_NONE,
				merger->key_def);
	return merger->reverse ? cmp >= 0 : cmp < 0;
}

/**
 * Initialize a new merger heap node.
 */
static void
merger_heap_node_create(struct merger_heap_node *node,
			struct merge_source *source)
{
	node->source = source;
	merge_source_ref(node->source);
	node->tuple = NULL;
	heap_node_create(&node->in_merger);
}

/**
 * Free a merger heap node.
 */
static void
merger_heap_node_delete(struct merger_heap_node *node)
{
	merge_source_unref(node->source);
	if (node->tuple != NULL)
		tuple_unref(node->tuple);
}

/**
 * The helper to add a new heap node to a merger heap.
 *
 * Return -1 at an error and set a diag.
 *
 * Otherwise store a next tuple in node->tuple, add the node to
 * merger->heap and return 0.
 */
static int
merger_add_heap_node(struct merger *merger, struct merger_heap_node *node)
{
	struct tuple *tuple = NULL;

	/* Acquire a next tuple. */
	struct merge_source *source = node->source;
	if (merge_source_next(source, merger->format, &tuple) != 0)
		return -1;

	/* Don't add an empty source to a heap. */
	if (tuple == NULL)
		return 0;

	node->tuple = tuple;

	/* Add a node to a heap. */
	if (merger_heap_insert(&merger->heap, node) != 0) {
		diag_set(OutOfMemory, 0, "malloc", "merger->heap");
		return -1;
	}

	return 0;
}

/* Virtual methods declarations */

static void
merger_delete(struct merge_source *base);
static int
merger_next(struct merge_source *base, struct tuple_format *format,
	    struct tuple **out);

/* Non-virtual methods */

/**
 * Set sources for a merger.
 *
 * It is the helper for merger_new().
 *
 * Return 0 at success. Return -1 at an error and set a diag.
 */
static int
merger_set_sources(struct merger *merger, struct merge_source **sources,
		   uint32_t source_count)
{
	const size_t nodes_size = sizeof(struct merger_heap_node) *
		source_count;
	struct merger_heap_node *nodes = malloc(nodes_size);
	if (nodes == NULL) {
		diag_set(OutOfMemory, nodes_size, "malloc",
			 "merger heap nodes");
		return -1;
	}

	for (uint32_t i = 0; i < source_count; ++i)
		merger_heap_node_create(&nodes[i], sources[i]);

	merger->node_count = source_count;
	merger->nodes = nodes;
	return 0;
}


struct merge_source *
merger_new(struct key_def *key_def, struct merge_source **sources,
	   uint32_t source_count, bool reverse)
{
	static struct merge_source_vtab merger_vtab = {
		.destroy = merger_delete,
		.next = merger_next,
	};

	struct merger *merger = malloc(sizeof(struct merger));
	if (merger == NULL) {
		diag_set(OutOfMemory, sizeof(struct merger), "malloc",
			 "merger");
		return NULL;
	}

	/*
	 * We need to copy the key_def because it can be collected
	 * before a merge process ends (say, by LuaJIT GC if the
	 * key_def comes from Lua).
	 */
	key_def = key_def_dup(key_def);
	if (key_def == NULL) {
		free(merger);
		return NULL;
	}

	struct tuple_format *format = box_tuple_format_new(&key_def, 1);
	if (format == NULL) {
		key_def_delete(key_def);
		free(merger);
		return NULL;
	}

	merge_source_create(&merger->base, &merger_vtab);
	merger->started = false;
	merger->key_def = key_def;
	merger->format = format;
	merger_heap_create(&merger->heap);
	merger->node_count = 0;
	merger->nodes = NULL;
	merger->reverse = reverse;

	if (merger_set_sources(merger, sources, source_count) != 0) {
		key_def_delete(merger->key_def);
		tuple_format_unref(merger->format);
		merger_heap_destroy(&merger->heap);
		free(merger);
		return NULL;
	}

	return &merger->base;
}

/* Virtual methods */

static void
merger_delete(struct merge_source *base)
{
	struct merger *merger = container_of(base, struct merger, base);

	key_def_delete(merger->key_def);
	tuple_format_unref(merger->format);
	merger_heap_destroy(&merger->heap);

	for (uint32_t i = 0; i < merger->node_count; ++i)
		merger_heap_node_delete(&merger->nodes[i]);

	if (merger->nodes != NULL)
		free(merger->nodes);

	free(merger);
}

static int
merger_next(struct merge_source *base, struct tuple_format *format,
	    struct tuple **out)
{
	struct merger *merger = container_of(base, struct merger, base);

	/*
	 * Fetch a first tuple for each source and add all heap
	 * nodes to a merger heap.
	 */
	if (!merger->started) {
		for (uint32_t i = 0; i < merger->node_count; ++i) {
			struct merger_heap_node *node = &merger->nodes[i];
			if (merger_add_heap_node(merger, node) != 0)
				return -1;
		}
		merger->started = true;
	}

	/* Get a next tuple. */
	struct merger_heap_node *node = merger_heap_top(&merger->heap);
	if (node == NULL) {
		*out = NULL;
		return 0;
	}
	struct tuple *tuple = node->tuple;
	assert(tuple != NULL);

	/* Validate the tuple. */
	if (format != NULL && tuple_validate(format, tuple) != 0)
		return -1;

	/*
	 * Note: An old node->tuple pointer will be written to
	 * *out as refcounted tuple, so we don't unreference it
	 * here.
	 */
	struct merge_source *source = node->source;
	if (merge_source_next(source, merger->format, &node->tuple) != 0)
		return -1;

	/* Update a heap. */
	if (node->tuple == NULL)
		merger_heap_delete(&merger->heap, node);
	else
		merger_heap_update(&merger->heap, node);

	*out = tuple;
	return 0;
}

/* }}} */
