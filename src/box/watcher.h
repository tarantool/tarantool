/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct fiber;
struct mh_strnptr_t;

struct watcher;
struct watchable_node;

/**
 * Watcher callback.
 *
 * Invoked to notify a watcher about a change of the key it was registered for.
 */
typedef void
(*watcher_run_f)(struct watcher *watcher);

/**
 * Watcher destructor.
 *
 * Invoked after a watcher is unregistered. If the watcher callback is running,
 * the destructor will be called as soon as it returns, otherwise it will be
 * called immediately by watcher_unregister().
 *
 * The callback must not use watcher_key() or watcher_data().
 */
typedef void
(*watcher_destroy_f)(struct watcher *watcher);

enum watcher_flag {
	/**
	 * If set, the notification callback will be invoked in a new fiber,
	 * otherwise it will be invoked by the worker fiber. Set this flag if
	 * the callback yields so as not to block the worker fiber.
	 */
	WATCHER_RUN_ASYNC		= 0x01,
	/**
	 * By default, a watcher becomes ready for a new notification as soon
	 * as its callback returns. Setting this flag changes this behavior:
	 * the watcher has to explicitly acknowledge a notification by calling
	 * watcher_ack() before it can be notified again.
	 */
	WATCHER_EXPLICIT_ACK		= 0x02,
};

/**
 * Watcher state.
 */
struct watcher {
	/** See watcher_run_f. */
	watcher_run_f run;
	/** See watcher_destroy_f. */
	watcher_destroy_f destroy;
	/** Bitwise combination of watcher_flag. */
	unsigned flags;
	/** Node which this watcher is registered for. */
	struct watchable_node *node;
	/**
	 * Version of the data for which the watcher was last executed.
	 * See the comment to watchable_node::version for how it's used.
	 */
	uint64_t version;
	/** Number of callbacks currently running. */
	int n_running;
	/** Link in watchable_node::all_watchers. */
	struct rlist in_all_watchers;
	/**
	 * Link in one of the lists:
	 *  - watchable_node::idle_watchers
	 *  - watchable::pending_watchers
	 *
	 * Empty list head if the watcher was notified but hasn't acknowledged
	 * the notification.
	 */
	struct rlist in_idle_or_pending;
};

/**
 * Watchable node.
 *
 * Stores a notification key, associated data, and registered watchers.
 */
struct watchable_node {
	/** Watchable this node is a part of. */
	struct watchable *watchable;
	/** Pointer to the data (may be NULL). */
	char *data;
	/** End of the data. */
	char *data_end;
	/**
	 * Version of the data, incremented every time the data is updated.
	 *
	 * We remember the version before running a watcher callback. When the
	 * callback returns, we compare the version we saw with the current
	 * version: if they are different, it means that the data was updated
	 * while the watcher was running and it needs to run again with the new
	 * data.
	 */
	uint64_t version;
	/**
	 * List of all watchers registered for this node.
	 *
	 * Linked by watcher::in_all_watchers.
	 */
	struct rlist all_watchers;
	/**
	 * List of watchers that are not running or waiting to run. These
	 * watchers are moved to watchable::pending_watchers when the data
	 * is updated.
	 *
	 * Linked by watcher::in_idle_or_pending.
	 */
	struct rlist idle_watchers;
	/** Length of the notification key name. */
	size_t key_len;
	/**
	 * Notification key name (null terminated).
	 *
	 * Used as a key in the watchable::node_by_key map.
	 */
	char key[0];
};

/**
 * Collection of watchable nodes.
 *
 * A watcher can be registered for a specific node. Nodes are created on
 * demand, when the first watcher is registered or the data is set.
 */
struct watchable {
	/** Map: key name -> watchable_node. */
	struct mh_strnptr_t *node_by_key;
	/**
	 * List of watchers awaiting to run. When a node data is updated, all
	 * watchers from watchable_node::idle_watchers are moved to this list.
	 *
	 * Linked by watcher::in_idle_or_pending.
	 */
	struct rlist pending_watchers;
	/** Background fiber that runs watcher callbacks. */
	struct fiber *worker;
};

/**
 * Returns the name of the key for which the watcher was registered.
 * Must not be used in watcher_destroy_f.
 *
 * @param watcher Watcher
 * @param[out] len Length of the key name
 * @retval Key name
 */
static inline const char *
watcher_key(const struct watcher *watcher, size_t *len)
{
	struct watchable_node *node = watcher->node;
	assert(node != NULL);
	*len = node->key_len;
	return node->key;
}

/**
 * Returns the data attached to the key for which the watcher was registered.
 * Must not be used in watcher_destroy_f.
 *
 * @param watcher Watcher
 * @param[out] end End of the key data
 * @retval Key data
 */
static inline const char *
watcher_data(const struct watcher *watcher, const char **end)
{
	struct watchable_node *node = watcher->node;
	assert(node != NULL);
	*end = node->data_end;
	return node->data;
}

/**
 * Acknowledges a notification.
 *
 * @param watcher Watcher
 */
void
watcher_ack(struct watcher *watcher);

/**
 * Unregisters a watcher.
 *
 * @param watcher Watcher to unregister
 *
 * If the watcher callback is not running, the watcher will be destroyed by
 * this function. Otherwise, the watcher will be destroyed as soon as the
 * callback returns while this function will return immediately.
 */
void
watcher_unregister(struct watcher *watcher);

/**
 * Registers a watcher for the specified notification key.
 *
 * @param key Name of the notification key
 * @param key_len Length of the key name
 * @param run See watcher_run_f
 * @param destroy See watcher_destroy_f
 * @param flags Bitwise combination of watcher_flag
 * @param[out] watcher Watcher to register
 *
 * A watcher callback is scheduled for execution unconditionally after watcher
 * registration and then every time box_broadcast() is called for the specified
 * key.
 */
void
box_register_watcher(const char *key, size_t key_len,
		     watcher_run_f run, watcher_destroy_f destroy,
		     unsigned flags, struct watcher *watcher);

/**
 * Updates data attached to a notification key and notifies watchers.
 *
 * @param key Name of the notification key
 * @param key_len Length of the key name
 * @param data Pointer to the key data (MsgPack)
 * @param data_end Pointer to the end of the key data
 *
 * A notification key is created on demand and deleted when there is no
 * watchers or data attached to the key.
 *
 * @a data may be NULL.
 *
 * @a data is copied and stored internally so it can be safely destroyed after
 * calling this function.
 *
 * This function does not invoke registered callbacks, it just schedules them
 * for execution. Callbacks are invoked by a background fiber.
 */
void
box_broadcast(const char *key, size_t key_len,
	      const char *data, const char *data_end);

/**
 * A convenience wrapper around box_broadcast(), which takes a zero-terminated
 * string for key and generates msgpack with mp_vformat() from the given
 * format.
 */
void
box_broadcast_fmt(const char *key, const char *format, ...);

/**
 * Returns the data attached to a notification key.
 *
 * @param key Name of the notification key
 * @param key_len Length of the key name
 * @param[out] end End of the key data
 * @retval Key data
 *
 * The function never fails.
 *
 * If there's no data attached to the given notification key (box_broadcast()
 * has never been called for this key), the function returns NULL.
 *
 * Note that the data returned by this function may be updated by a concurrent
 * call to box_broadcast() so the caller must copy it if it intends to yield.
 */
const char *
box_watch_once(const char *key, size_t key_len, const char **end);

void
box_watcher_init(void);

void
box_watcher_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
