/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct func_adapter;
struct trigger;

/**
 * List of triggers registered on event identified by name.
 */
struct event {
	/** List of triggers. */
	struct rlist triggers;
	/** Name of event. */
	char *name;
	/** Reference count. */
	uint32_t ref_count;
	/** Number of triggers. */
	uint32_t trigger_count;
};

/**
 * Destroys an event and removes it from event registry.
 * NB: The method is private and must not be called manually.
 */
void
event_delete(struct event *event);

/**
 * Increments event reference counter.
 */
static inline void
event_ref(struct event *event)
{
	event->ref_count++;
}

/**
 * Decrements event reference counter. The event is destroyed if the counter
 * reaches zero.
 */
static inline void
event_unref(struct event *event)
{
	assert(event->ref_count > 0);
	event->ref_count--;
	if (event->ref_count == 0) {
		assert(rlist_empty(&event->triggers));
		assert(event->trigger_count == 0);
		event_delete(event);
	}
}

/**
 * Checks if the event has no triggers.
 */
static inline bool
event_has_triggers(struct event *event)
{
	return event->trigger_count > 0;
}

/**
 * Finds a trigger by name in an event. All the arguments must not be NULL.
 */
struct func_adapter *
event_find_trigger(struct event *event, const char *name);

/**
 * Flags for event triggers.
 * Must be power of two - 1 bit in binary representation.
 */
enum event_trigger_flag {
	/**
	 * The trigger is temporary - all such triggers can be dropped with
	 * special method.
	 */
	EVENT_TRIGGER_IS_TEMPORARY = 1,
};

/**
 * Resets trigger by name in an event.
 * Arguments event and name must not be NULL.
 * If new_trigger is NULL, the function removes a trigger by name from the
 * event. Otherwise, it replaces trigger by name or inserts it in the beginning
 * of the underlying list of event. The new trigger is created with passed
 * flags.
 */
void
event_reset_trigger_with_flags(struct event *event, const char *name,
			       struct func_adapter *new_trigger, uint8_t flags);

static inline void
event_reset_trigger(struct event *event, const char *name,
		    struct func_adapter *new_trigger)
{
	event_reset_trigger_with_flags(event, name, new_trigger, 0);
}

/**
 * Remove all the triggers marked as temporary from the event.
 */
void
event_remove_temporary_triggers(struct event *event);

/**
 * Reference all non-deleted triggers to prevent their deletion.
 * They will be freed only with the event subsystem.
 */
void
event_ref_all_triggers(struct event *event);

/**
 * Iterator over triggers from event. Never invalidates.
 */
struct event_trigger_iterator {
	/**
	 * Current element in the list of triggers.
	 * Becomes NULL when the iterator is exhausted.
	 */
	struct rlist *curr;
	/** Underlying event. */
	struct event *event;
};

/**
 * Initializes iterator.
 */
void
event_trigger_iterator_create(struct event_trigger_iterator *it,
			      struct event *event);

/**
 * Advances iterator. Output arguments trigger and name must not be NULL.
 */
bool
event_trigger_iterator_next(struct event_trigger_iterator *it,
			    struct func_adapter **trigger, const char **name);

/**
 * Deinitializes iterator. Does not free memory.
 */
void
event_trigger_iterator_destroy(struct event_trigger_iterator *it);

/**
 * Finds an event by its name. Name must be a zero-terminated string.
 * Creates new event and inserts it to registry if there is no event with such
 * name when flag create_if_not_exist is true.
 */
struct event *
event_get(const char *name, bool create_if_not_exist);

typedef bool
event_foreach_f(struct event *event, void *arg);

/**
 * Invokes a callback for each registered event with no particular order.
 *
 * The callback is passed an event object and the given argument.
 * If it returns true, iteration continues. Otherwise, iteration breaks, and
 * the function returns false.
 *
 * Empty events are guaranteed to be skipped.
 */
bool
event_foreach(event_foreach_f cb, void *arg);

/**
 * Sets an internal `trigger' fired on change of any event in the registry.
 */
void
event_on_change(struct trigger *trigger);

/**
 * Initializes event submodule.
 */
void
event_init(void);

/**
 * Frees event submodule.
 */
void
event_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
