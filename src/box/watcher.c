/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "box/watcher.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "assoc.h"
#include "diag.h"
#include "fiber.h"
#include "msgpuck.h"
#include "small/rlist.h"
#include "trivia/util.h"
#include "tt_static.h"

/**
 * Global watchable object used by box.
 */
static struct watchable box_watchable;

/**
 * Returns true if the watchable node can be dropped, i.e. it doesn't have data
 * or registered watchers.
 */
static inline bool
watchable_node_is_unused(struct watchable_node *node)
{
	return node->data == NULL && rlist_empty(&node->all_watchers);
}

/**
 * Allocates and initializes a new watchable node.
 */
static struct watchable_node *
watchable_node_new(struct watchable *watchable,
		   const char *key, size_t key_len)
{
	struct watchable_node *node = xmalloc(sizeof(*node) + key_len + 1);
	node->watchable = watchable;
	memcpy(node->key, key, key_len);
	node->key[key_len] = '\0';
	node->key_len = key_len;
	node->data = NULL;
	node->data_end = NULL;
	node->version = 0;
	rlist_create(&node->all_watchers);
	rlist_create(&node->idle_watchers);
	return node;
}

/**
 * Frees a watchable node.
 */
static void
watchable_node_delete(struct watchable_node *node)
{
	assert(rlist_empty(&node->all_watchers));
	assert(rlist_empty(&node->idle_watchers));
	free(node->data);
	TRASH(node);
	free(node);
}

/**
 * Looks up an returns a node by key name. Returns NULL if not found.
 */
static struct watchable_node *
watchable_find_node(struct watchable *watchable,
		    const char *key, size_t key_len)
{
	struct mh_strnptr_t *h = watchable->node_by_key;
	uint32_t key_hash = mh_strn_hash(key, key_len);
	struct mh_strnptr_key_t k = {key, key_len, key_hash};
	mh_int_t i = mh_strnptr_find(h, &k, NULL);
	if (i != mh_end(h)) {
		struct watchable_node *node = mh_strnptr_node(h, i)->val;
		assert(strncmp(node->key, key, key_len) == 0);
		return node;
	}
	return NULL;
}

/**
 * Looks up and returns a node by key name. Creates a new node if not found.
 */
static struct watchable_node *
watchable_find_or_create_node(struct watchable *watchable,
			      const char *key, size_t key_len)
{
	struct mh_strnptr_t *h = watchable->node_by_key;
	uint32_t key_hash = mh_strn_hash(key, key_len);
	struct mh_strnptr_key_t k = {key, key_len, key_hash};
	mh_int_t i = mh_strnptr_find(h, &k, NULL);
	if (i != mh_end(h)) {
		struct watchable_node *node = mh_strnptr_node(h, i)->val;
		assert(strncmp(node->key, key, key_len) == 0);
		return node;
	}
	struct watchable_node *node = watchable_node_new(watchable,
							 key, key_len);
	struct mh_strnptr_node_t n = {node->key, key_len, key_hash, node};
	mh_strnptr_put(h, &n, NULL, NULL);
	return node;
}

/**
 * Deletes a watchable node. The node must have no watchers.
 */
static void
watchable_drop_node(struct watchable *watchable, struct watchable_node *node)
{
	assert(watchable == node->watchable);
	struct mh_strnptr_t *h = watchable->node_by_key;
	const char *key = node->key;
	size_t key_len = node->key_len;
	mh_int_t i = mh_strnptr_find_str(h, key, key_len);
	assert(i != mh_end(h));
	assert(mh_strnptr_node(h, i)->val == node);
	mh_strnptr_del(h, i, NULL);
	watchable_node_delete(node);
}

static int
watchable_worker_f(va_list);

/**
 * Wakes up the worker fiber. Creates the fiber on the first invocation.
 */
static void
watchable_wakeup_worker(struct watchable *watchable)
{
	if (watchable->is_shutdown)
		return;
	if (watchable->worker == NULL) {
		watchable->worker = fiber_new_system("box.watchable",
						     watchable_worker_f);
		if (watchable->worker == NULL) {
			diag_log();
			panic("failed to start box.watchable worker fiber");
		}
		fiber_set_joinable(watchable->worker, true);
		watchable->worker->f_arg = watchable;
	}
	fiber_wakeup(watchable->worker);
}

/**
 * Schedules the given watcher for execution.
 */
static void
watchable_schedule_watcher(struct watchable *watchable,
			   struct watcher *watcher)
{
	assert(watcher->node != NULL);
	assert(watcher->node->watchable == watchable);
	assert(rlist_empty(&watcher->in_idle_or_pending));
	/*
	 * Always append to the list tail to guarantee that all watchers
	 * eventually run no matter how often nodes are updated.
	 */
	rlist_add_tail_entry(&watchable->pending_watchers, watcher,
			     in_idle_or_pending);
	watchable_wakeup_worker(watchable);
}

/**
 * Schedules all idle watchers registered for the given node for execution.
 */
static void
watchable_schedule_node(struct watchable *watchable,
			struct watchable_node *node)
{
	assert(node->watchable == watchable);
	/*
	 * Always append to the list tail to guarantee that all watchers
	 * eventually run no matter how often nodes are updated.
	 */
	if (!rlist_empty(&node->idle_watchers)) {
		rlist_splice_tail(&watchable->pending_watchers,
				  &node->idle_watchers);
		watchable_wakeup_worker(watchable);
	}
}

/**
 * Registers a new watcher. The watcher is immediately scheduled for execution.
 */
static struct watcher *
watchable_register_watcher(struct watchable *watchable,
			   const char *key, size_t key_len,
			   watcher_run_f run, watcher_destroy_f destroy,
			   unsigned flags, struct watcher *watcher)
{
	struct watchable_node *node = watchable_find_or_create_node(
		watchable, key, key_len);
	watcher->run = run;
	watcher->destroy = destroy;
	watcher->flags = flags;
	watcher->node = node;
	watcher->version = 0;
	watcher->n_running = 0;
	rlist_add_entry(&node->all_watchers, watcher, in_all_watchers);
	rlist_create(&watcher->in_idle_or_pending);
	watchable_schedule_watcher(watchable, watcher);
	return watcher;
}

/**
 * Destroys a watcher. The watcher must be unregistered.
 */
static void
watcher_destroy(struct watcher *watcher)
{
	assert(watcher->node == NULL);
	assert(watcher->n_running == 0);
	assert(rlist_empty(&watcher->in_all_watchers));
	assert(rlist_empty(&watcher->in_idle_or_pending));
	watcher->destroy(watcher);
}

/**
 * Like watcher_unregister, but doesn't drop the node even if it becomes
 * unused.
 */
static void
watcher_unregister_keep_node(struct watcher *watcher)
{
	assert(watcher->node != NULL);
	watcher->node = NULL;
	rlist_del_entry(watcher, in_all_watchers);
	rlist_del_entry(watcher, in_idle_or_pending);
	if (watcher->n_running == 0)
		watcher_destroy(watcher);
}

void
watcher_unregister(struct watcher *watcher)
{
	struct watchable_node *node = watcher->node;
	assert(node != NULL);
	watcher_unregister_keep_node(watcher);
	if (watchable_node_is_unused(node))
		watchable_drop_node(node->watchable, node);
}

void
watcher_ack(struct watcher *watcher)
{
	struct watchable_node *node = watcher->node;
	if (node == NULL) {
		/*
		 * The watcher was unregistered (watcher_ack() may be called
		 * from the watcher callback, which could be running when the
		 * watcher was unregistered).
		 */
		return;
	}
	if (!rlist_empty(&watcher->in_idle_or_pending)) {
		/* Already acknowledged. */
		return;
	}
	assert(watcher->version <= node->version);
	if (watcher->version == node->version) {
		/*
		 * There were no updates while the watcher was running.
		 * Add it to the list of idle watchers.
		 */
		rlist_add_tail_entry(&node->idle_watchers, watcher,
				     in_idle_or_pending);
	} else {
		/*
		 * The node data was updated while the watcher was running.
		 * Schedule the watcher for execution.
		 */
		watchable_schedule_watcher(node->watchable, watcher);
	}
}

static void
watcher_do_run(struct watcher *watcher)
{
	struct watchable_node *node = watcher->node;
	watcher->version = node->version;
	watcher->n_running++;
	watcher->run(watcher);
	watcher->n_running--;
	if ((watcher->flags & WATCHER_EXPLICIT_ACK) == 0)
		watcher_ack(watcher);
	if (watcher->node == NULL && watcher->n_running == 0) {
		/*
		 * The watcher was unregistered while it was running.
		 * Destroy it once the last running callback returns.
		 */
		watcher_destroy(watcher);
	}
}

static int
watcher_run_async_f(va_list ap)
{
	struct watcher *watcher = va_arg(ap, struct watcher *);
	watcher_do_run(watcher);
	return 0;
}

/**
 * Invokes the callback of the given watcher synchronously or in a new fiber,
 * depending on the watcher flags.
 */
static void
watcher_run(struct watcher *watcher)
{
	if ((watcher->flags & WATCHER_RUN_ASYNC) != 0) {
		struct fiber *fiber = fiber_new("box.watcher",
						watcher_run_async_f);
		if (fiber != NULL) {
			fiber_start(fiber, watcher);
			return;
		}
		diag_log();
	}
	watcher_do_run(watcher);
}

/**
 * Runs a watcher from the pending list of the given watchable.
 * Returns false if there's no watchers to run.
 */
static bool
watchable_run(struct watchable *watchable)
{
	if (rlist_empty(&watchable->pending_watchers))
		return false;
	struct watcher *watcher = rlist_shift_entry(
		&watchable->pending_watchers, struct watcher,
		in_idle_or_pending);
	watcher_run(watcher);
	return true;
}

static int
watchable_worker_f(va_list ap)
{
	(void)ap;
	struct watchable *watchable = fiber()->f_arg;
	assert(watchable->worker == fiber());
	while (!fiber_is_cancelled()) {
		fiber_check_gc();
		if (!watchable_run(watchable)) {
			/* No more watchers to run, wait... */
			fiber_yield();
		}
	}
	return 0;
}

static void
watchable_create(struct watchable *watchable)
{
	watchable->node_by_key = mh_strnptr_new();
	rlist_create(&watchable->pending_watchers);
	watchable->worker = NULL;
	watchable->is_shutdown = false;
}

/**
 * Shutdown watchable. After shutdown it still can be used but
 * notifications are stopped.
 */
static void
watchable_shutdown(struct watchable *watchable)
{
	watchable->is_shutdown = true;
	if (watchable->worker != NULL) {
		fiber_cancel(watchable->worker);
		fiber_join(watchable->worker);
		watchable->worker = NULL;
	}
}

static void
watchable_destroy(struct watchable *watchable)
{
	struct mh_strnptr_t *h = watchable->node_by_key;
	mh_int_t i;
	mh_foreach(h, i) {
		struct watchable_node *node = mh_strnptr_node(h, i)->val;
		struct watcher *watcher, *next_watcher;
		rlist_foreach_entry_safe(watcher, &node->all_watchers,
					 in_all_watchers, next_watcher) {
			assert(watcher->node == node);
			watcher_unregister_keep_node(watcher);
		}
		watchable_node_delete(node);
	}
	mh_strnptr_delete(h);
	TRASH(watchable);
}

/**
 * Updates data attached a notification key and schedules watchers for
 * execution.
 */
static void
watchable_broadcast(struct watchable *watchable,
		    const char *key, size_t key_len,
		    const char *data, const char *data_end)
{
	struct watchable_node *node = watchable_find_or_create_node(
		watchable, key, key_len);
	free(node->data);
	if (data != NULL) {
		ssize_t data_size = data_end - data;
		assert(data_size > 0);
		node->data = xmalloc(data_size);
		memcpy(node->data, data, data_size);
		node->data_end = node->data + data_size;
	} else {
		node->data = NULL;
		node->data_end = NULL;
		if (watchable_node_is_unused(node)) {
			watchable_drop_node(watchable, node);
			return;
		}
	}
	node->version++;
	watchable_schedule_node(watchable, node);
}

void
box_register_watcher(const char *key, size_t key_len,
		     watcher_run_f run, watcher_destroy_f destroy,
		     unsigned flags, struct watcher *watcher)
{
	watchable_register_watcher(&box_watchable, key, key_len,
				   run, destroy, flags, watcher);
}

void
box_broadcast(const char *key, size_t key_len,
	      const char *data, const char *data_end)
{
	watchable_broadcast(&box_watchable, key, key_len, data, data_end);
}

void
box_broadcast_fmt(const char *key, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	size_t size = mp_vformat(NULL, 0, format, ap);
	va_end(ap);
	if (size > TT_STATIC_BUF_LEN)
		panic("not enough buffer space to broadcast '%s'", key);
	char *data = tt_static_buf();
	va_start(ap, format);
	if (mp_vformat(data, size, format, ap) != size)
		unreachable();
	va_end(ap);
	box_broadcast(key, strlen(key), data, data + size);
}

const char *
box_watch_once(const char *key, size_t key_len, const char **end)
{
	struct watchable_node *node = watchable_find_node(&box_watchable,
							  key, key_len);
	if (node == NULL) {
		*end = NULL;
		return NULL;
	}
	*end = node->data_end;
	return node->data;
}

void
box_watcher_init(void)
{
	watchable_create(&box_watchable);
}

void
box_watcher_shutdown(void)
{
	watchable_shutdown(&box_watchable);
}

void
box_watcher_free(void)
{
	watchable_destroy(&box_watchable);
}
