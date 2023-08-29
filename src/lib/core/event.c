/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "event.h"

#include <assert.h>

#include "assoc.h"
#include "diag.h"
#include "trivia/util.h"
#include "func_adapter.h"

/** Registry of all events: name -> event. */
static struct mh_strnptr_t *event_registry;

/** Cached event 'tarantool.trigger.on_change'. */
static struct event *on_change_event;

/**
 * A named node of the list, containing func_adapter. Every event_trigger is
 * associated with an event. Since event_triggers have completely different
 * call interface, they are not inherited from core triggers.
 */
struct event_trigger {
	/** A link in a list of all registered event triggers. */
	struct rlist link;
	/** Trigger function. */
	struct func_adapter *func;
	/** Backlink to event. Needed for reference count of event. */
	struct event *event;
	/** Unique name of the trigger. */
	char *name;
	/** Trigger reference counter. */
	uint32_t ref_count;
	/** This flag is set when the trigger is deleted. */
	bool is_deleted;
};

/**
 * Creates event_trigger from func_adapter and name and binds it to passed
 * event. Created trigger owns the adapter and will destroy it in its
 * destructor. Passed name must be zero terminated and will be copied.
 * Passed event must not be NULL and will be referenced by created trigger.
 * Note that the function does not increment trigger_count field of the event.
 */
static struct event_trigger *
event_trigger_new(struct func_adapter *func, struct event *event,
		  const char *name)
{
	assert(name != NULL);
	assert(event != NULL);
	size_t name_len = strlen(name);
	struct event_trigger *trigger =
		xmalloc(sizeof(*trigger) + name_len + 1);
	trigger->func = func;
	trigger->name = (char *)(trigger + 1);
	strlcpy(trigger->name, name, name_len + 1);
	trigger->ref_count = 0;
	trigger->is_deleted = false;
	trigger->event = event;
	event_ref(event);
	rlist_create(&trigger->link);
	return trigger;
}

/**
 * Destroys an event_trigger. Underlying func_adapter is destroyed,
 * event_trigger is deallocated.
 */
static void
event_trigger_delete(struct event_trigger *trigger)
{
	assert(trigger->func != NULL);
	assert(trigger->event != NULL);
	rlist_del(&trigger->link);
	event_unref(trigger->event);
	func_adapter_destroy(trigger->func);
	TRASH(trigger);
	free(trigger);
}

/**
 * Increments event_trigger reference counter.
 */
static void
event_trigger_ref(struct event_trigger *trigger)
{
	trigger->ref_count++;
}

/**
 * Decrements event_trigger reference counter. The event_trigger is destroyed
 * if the counter reaches zero.
 */
static void
event_trigger_unref(struct event_trigger *trigger)
{
	assert(trigger != NULL);
	assert(trigger->ref_count > 0);
	trigger->ref_count--;
	assert(trigger->ref_count > 0 || trigger->is_deleted);
	if (trigger->ref_count == 0)
		event_trigger_delete(trigger);
}

/**
 * Allocates, initializes a new event and inserts it to event registry.
 * Passed name will be copied.
 */
static struct event *
event_new(const char *name, size_t name_len)
{
	assert(name != NULL);
	assert(name_len > 0);
	struct event *event = xmalloc(sizeof(*event) + name_len + 1);
	rlist_create(&event->triggers);
	event->ref_count = 0;
	event->trigger_count = 0;
	event->name = (char *)(event + 1);
	strlcpy(event->name, name, name_len + 1);

	struct mh_strnptr_t *h = event_registry;
	name = event->name;
	uint32_t name_hash = mh_strn_hash(name, name_len);
	struct mh_strnptr_node_t node = {name, name_len, name_hash, event};
	struct mh_strnptr_node_t prev;
	struct mh_strnptr_node_t *prev_ptr = &prev;
	mh_int_t pos = mh_strnptr_put(h, &node, &prev_ptr, NULL);
	(void)pos;
	assert(pos != mh_end(h));
	assert(prev_ptr == NULL);

	return event;
}

/**
 * Destroys an event and removes it from event registry.
 * Underlying trigger list is destroyed.
 */
void
event_delete(struct event *event)
{
	assert(event != NULL);

	struct mh_strnptr_t *h = event_registry;
	const char *name = event->name;
	size_t name_len = strlen(name);
	mh_int_t i = mh_strnptr_find_str(h, name, name_len);
	assert(i != mh_end(h));
	assert(mh_strnptr_node(h, i)->val == event);
	mh_strnptr_del(h, i, 0);
	TRASH(event);
	free(event);
}

/**
 * Finds internal event_trigger entity by name in the event.
 * All arguments must not be NULL.
 */
static struct event_trigger *
event_find_trigger_internal(struct event *event, const char *name)
{
	struct event_trigger *curr = NULL;
	rlist_foreach_entry(curr, &event->triggers, link) {
		if (!curr->is_deleted && strcmp(curr->name, name) == 0)
			return curr;
	}
	return NULL;
}

struct func_adapter *
event_find_trigger(struct event *event, const char *name)
{
	struct event_trigger *trigger =
		event_find_trigger_internal(event, name);
	return trigger != NULL ? trigger->func : NULL;
}

/**
 * Fires on_change triggers. Must be called after the change is applied.
 * Each returned value is ignored, all thrown errors are logged.
 */
static void
event_on_change(struct event *event)
{
	if (!event_has_triggers(on_change_event))
		return;
	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, on_change_event);
	struct func_adapter *func = NULL;
	const char *name = NULL;
	struct func_adapter_ctx ctx;
	while (event_trigger_iterator_next(&it, &func, &name)) {
		func_adapter_begin(func, &ctx);
		func_adapter_push_str0(func, &ctx, event->name);
		int rc = func_adapter_call(func, &ctx);
		func_adapter_end(func, &ctx);
		if (rc != 0)
			diag_log();
	}
	event_trigger_iterator_destroy(&it);
}

void
event_reset_trigger(struct event *event, const char *name,
		    struct func_adapter *new_trigger)
{
	assert(event != NULL);
	assert(name != NULL);
	/* Reference event to prevent it from deletion during modification. */
	event_ref(event);
	struct event_trigger *found_trigger =
		event_find_trigger_internal(event, name);
	if (new_trigger != NULL) {
		event->trigger_count++;
		struct event_trigger *trigger =
			event_trigger_new(new_trigger, event, name);
		event_trigger_ref(trigger);
		if (found_trigger == NULL) {
			rlist_add_entry(&event->triggers, trigger, link);
		} else {
			/*
			 * Insert new trigger before the replaced one not to
			 * iterate over both of them in the case when an
			 * iterator points to a replaced trigger.
			 */
			rlist_add_tail_entry(&found_trigger->link,
					     trigger, link);
		}
	}
	if (found_trigger != NULL) {
		assert(event->trigger_count > 0);
		event->trigger_count--;
		found_trigger->is_deleted = true;
		event_trigger_unref(found_trigger);
	}
	event_on_change(event);
	event_unref(event);
}

void
event_trigger_iterator_create(struct event_trigger_iterator *it,
			      struct event *event)
{
	event_ref(event);
	it->event = event;
	it->curr = &event->triggers;
}

bool
event_trigger_iterator_next(struct event_trigger_iterator *it,
			    struct func_adapter **func, const char **name)
{
	assert(func != NULL);
	assert(name != NULL);

	*func = NULL;
	*name = NULL;
	/* Iterator is exhausted - return. */
	if (it->curr == NULL)
		return false;
	struct event_trigger *trigger;
	struct rlist *old = it->curr;
	/* We need to skip all the deleted triggers. */
	do {
		it->curr = rlist_next(it->curr);
		trigger = rlist_entry(it->curr, struct event_trigger, link);
		/* We have traversed the whole list. */
		if (it->curr == &it->event->triggers) {
			it->curr = NULL;
			trigger = NULL;
			goto release;
		}
	} while (trigger->is_deleted);
	assert(trigger != NULL);
	event_trigger_ref(trigger);
release:
	if (old != NULL && old != &it->event->triggers) {
		struct event_trigger *old_trigger =
			rlist_entry(old, struct event_trigger, link);
		assert(old_trigger != trigger);
		event_trigger_unref(old_trigger);
	}
	if (trigger != NULL) {
		*func = trigger->func;
		*name = trigger->name;
		return true;
	}
	return false;
}

void
event_trigger_iterator_destroy(struct event_trigger_iterator *it)
{
	if (it->curr != NULL && it->curr != &it->event->triggers) {
		struct event_trigger *curr_trigger =
			rlist_entry(it->curr, struct event_trigger, link);
		event_trigger_unref(curr_trigger);
	}
	event_unref(it->event);
	it->event = NULL;
	it->curr = NULL;
}

struct event *
event_get(const char *name, bool create_if_not_exist)
{
	assert(event_registry != NULL);
	assert(name != NULL);
	struct mh_strnptr_t *h = event_registry;
	uint32_t name_len = strlen(name);
	mh_int_t i = mh_strnptr_find_str(h, name, name_len);
	if (i != mh_end(h))
		return mh_strnptr_node(h, i)->val;
	else if (!create_if_not_exist)
		return NULL;

	struct event *event = event_new(name, name_len);
	return event;
}

bool
event_foreach(event_foreach_f cb, void *arg)
{
	struct mh_strnptr_t *h = event_registry;
	mh_int_t i;
	mh_foreach(h, i) {
		struct mh_strnptr_node_t *node = mh_strnptr_node(h, i);
		struct event *event = node->val;
		if (!event_has_triggers(event))
			continue;
		if (!cb(event, arg))
			return false;
	}
	return true;
}

void
event_init(void)
{
	event_registry = mh_strnptr_new();
	on_change_event = event_get("tarantool.trigger.on_change", true);
	event_ref(on_change_event);
}

void
event_free(void)
{
	assert(event_registry != NULL);
	assert(on_change_event != NULL);
	event_unref(on_change_event);
	on_change_event = NULL;
	struct mh_strnptr_t *h = event_registry;
	mh_int_t i;
	mh_foreach(h, i) {
		struct event *event = mh_strnptr_node(h, i)->val;
		/*
		 * If the only thing that holds the event is its trigger list,
		 * the reference counter will reach zero when the list will be
		 * cleared and the destructor will be called. Since we will
		 * call destructor manually, let's reference the event in order
		 * to prevent such situation.
		 */
		event_ref(event);
		struct event_trigger *trigger, *tmp;
		rlist_foreach_entry_safe(trigger, &event->triggers, link, tmp)
			event_trigger_delete(trigger);
		event_delete(event);
	}
	mh_strnptr_delete(h);
	event_registry = NULL;
}
