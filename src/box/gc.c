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
#include "gc.h"

#include <trivia/util.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RB_COMPACT 1
#include <small/rb.h>
#include <small/rlist.h>

#include "diag.h"
#include "say.h"
#include "latch.h"
#include "vclock.h"
#include "checkpoint.h"
#include "engine.h"		/* engine_collect_garbage() */
#include "wal.h"		/* wal_collect_garbage() */

typedef rb_node(struct gc_consumer) gc_node_t;

/**
 * The object of this type is used to prevent garbage
 * collection from removing files that are still in use.
 */
struct gc_consumer {
	/** Link in gc_state::consumers. */
	gc_node_t node;
	/** Human-readable name. */
	char *name;
	/** The vclock signature tracked by this consumer. */
	int64_t signature;
};

typedef rb_tree(struct gc_consumer) gc_tree_t;

/** Garbage collection state. */
struct gc_state {
	/** Number of checkpoints to maintain. */
	int checkpoint_count;
	/** Max signature garbage collection has been called for. */
	int64_t signature;
	/** Registered consumers, linked by gc_consumer::node. */
	gc_tree_t consumers;
	/**
	 * Latch serializing concurrent invocations of engine
	 * garbage collection callbacks.
	 */
	struct latch latch;
};
static struct gc_state gc;

/**
 * Comparator used for ordering gc_consumer objects by signature
 * in a binary tree.
 */
static inline int
gc_consumer_cmp(const struct gc_consumer *a, const struct gc_consumer *b)
{
	if (a->signature < b->signature)
		return -1;
	if (a->signature > b->signature)
		return 1;
	if ((intptr_t)a < (intptr_t)b)
		return -1;
	if ((intptr_t)a > (intptr_t)b)
		return 1;
	return 0;
}

rb_gen(MAYBE_UNUSED static inline, gc_tree_, gc_tree_t,
       struct gc_consumer, node, gc_consumer_cmp);

/** Allocate a consumer object. */
static struct gc_consumer *
gc_consumer_new(const char *name, int64_t signature)
{
	struct gc_consumer *consumer = calloc(1, sizeof(*consumer));
	if (consumer == NULL) {
		diag_set(OutOfMemory, sizeof(*consumer),
			 "malloc", "struct gc_consumer");
		return NULL;
	}
	consumer->name = strdup(name);
	if (consumer->name == NULL) {
		diag_set(OutOfMemory, strlen(name) + 1,
			 "malloc", "struct gc_consumer");
		free(consumer);
		return NULL;
	}
	consumer->signature = signature;
	return consumer;
}

/** Free a consumer object. */
static void
gc_consumer_delete(struct gc_consumer *consumer)
{
	free(consumer->name);
	TRASH(consumer);
	free(consumer);
}

void
gc_init(void)
{
	gc.signature = -1;
	gc_tree_new(&gc.consumers);
	latch_create(&gc.latch);
}

void
gc_free(void)
{
	/* Free all registered consumers. */
	struct gc_consumer *consumer = gc_tree_first(&gc.consumers);
	while (consumer != NULL) {
		struct gc_consumer *next = gc_tree_next(&gc.consumers,
							consumer);
		gc_tree_remove(&gc.consumers, consumer);
		gc_consumer_delete(consumer);
		consumer = next;
	}
	latch_destroy(&gc.latch);
}

void
gc_run(void)
{
	int checkpoint_count = gc.checkpoint_count;
	assert(checkpoint_count > 0);

	/* Look up the consumer that uses the oldest snapshot. */
	struct gc_consumer *leftmost = gc_tree_first(&gc.consumers);

	/*
	 * Find the oldest checkpoint that must be preserved.
	 * We have to maintain @checkpoint_count oldest snapshots,
	 * plus we can't remove snapshots that are still in use.
	 */
	int64_t gc_signature = -1;

	struct checkpoint_iterator checkpoints;
	checkpoint_iterator_init(&checkpoints);

	const struct vclock *vclock;
	while ((vclock = checkpoint_iterator_prev(&checkpoints)) != NULL) {
		if (--checkpoint_count > 0)
			continue;
		if (leftmost != NULL &&
		    leftmost->signature < vclock_sum(vclock))
			continue;
		gc_signature = vclock_sum(vclock);
		break;
	}

	if (gc_signature <= gc.signature)
		return; /* nothing to do */

	gc.signature = gc_signature;

	/*
	 * Engine callbacks may sleep, because they use coio for
	 * removing files. Make sure we won't try to remove the
	 * same file multiple times by serializing concurrent gc
	 * executions.
	 */
	latch_lock(&gc.latch);
	/*
	 * Run garbage collection.
	 *
	 * The order is important here: we must invoke garbage
	 * collection for memtx snapshots first and abort if it
	 * fails - see comment to memtx_engine_collect_garbage().
	 */
	if (engine_collect_garbage(gc_signature) != 0) {
		say_error("box garbage collection failed: %s",
			  diag_last_error(diag_get())->errmsg);
	} else {
		wal_collect_garbage(gc_signature);
	}
	latch_unlock(&gc.latch);
}

void
gc_set_checkpoint_count(int checkpoint_count)
{
	gc.checkpoint_count = checkpoint_count;
}

struct gc_consumer *
gc_consumer_register(const char *name, int64_t signature)
{
	struct gc_consumer *consumer = gc_consumer_new(name, signature);
	if (consumer != NULL)
		gc_tree_insert(&gc.consumers, consumer);
	return consumer;
}

void
gc_consumer_unregister(struct gc_consumer *consumer)
{
	int64_t signature = consumer->signature;

	gc_tree_remove(&gc.consumers, consumer);
	gc_consumer_delete(consumer);

	/*
	 * Rerun garbage collection after removing the consumer
	 * if it referenced the oldest vclock.
	 */
	struct gc_consumer *leftmost = gc_tree_first(&gc.consumers);
	if (leftmost == NULL || leftmost->signature > signature)
		gc_run();
}

void
gc_consumer_advance(struct gc_consumer *consumer, int64_t signature)
{
	int64_t prev_signature = consumer->signature;

	assert(signature >= prev_signature);
	if (signature == prev_signature)
		return; /* nothing to do */

	/*
	 * Do not update the tree unless the tree invariant
	 * is violated.
	 */
	struct gc_consumer *next = gc_tree_next(&gc.consumers, consumer);
	bool update_tree = (next != NULL && signature >= next->signature);

	if (update_tree)
		gc_tree_remove(&gc.consumers, consumer);

	consumer->signature = signature;

	if (update_tree)
		gc_tree_insert(&gc.consumers, consumer);

	/*
	 * Rerun garbage collection after advancing the consumer
	 * if it referenced the oldest vclock.
	 */
	struct gc_consumer *leftmost = gc_tree_first(&gc.consumers);
	if (leftmost == NULL || leftmost->signature > prev_signature)
		gc_run();
}

const char *
gc_consumer_name(const struct gc_consumer *consumer)
{
	return consumer->name;
}

int64_t
gc_consumer_signature(const struct gc_consumer *consumer)
{
	return consumer->signature;
}

struct gc_consumer *
gc_consumer_iterator_next(struct gc_consumer_iterator *it)
{
	if (it->curr != NULL)
		it->curr = gc_tree_next(&gc.consumers, it->curr);
	else
		it->curr = gc_tree_first(&gc.consumers);
	return it->curr;
}
