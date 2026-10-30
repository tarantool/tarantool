#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "diag.h"
#include "error.h"
#include "errcode.h"
#include "fiber.h"
#include "func_adapter.h"
#include "memory.h"
#include "trivia/util.h"
#include "event.h"
#include "tt_static.h"
#include "trigger.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

static int func_destroy_count;

/**
 * Virtual destructor for func_adapter for the test purposes.
 */
void
func_destroy(struct func_adapter *func)
{
	func_destroy_count++;
}

/**
 * The test creates event with different names and check if all the basic
 * operations works correctly.
 */
static void
test_basic(void)
{
	plan(4 * 14);

	struct func_adapter_vtab vtab = {.destroy = func_destroy};
	struct func_adapter func = {.vtab = &vtab};
	const char *trg_name = "my_triggers.trg[1]";
	const char *names[] = {
		"name",
		"name with spaces",
		"namespace.name",
		"NAMESPACE[123].name"
	};
	for (size_t i = 0; i < lengthof(names); ++i) {
		const char *name = names[i];
		struct event *event = event_get(name, false);
		is(event, NULL, "No such event - NULL must be returned");
		event = event_get(name, true);
		isnt(event, NULL, "Event must be created");
		/* Reference event to prevent deletion when it'll be empty. */
		event_ref(event);
		struct event *found_event = event_get(name, false);
		is(found_event, event, "Existing event must be found");
		struct func_adapter *old = event_find_trigger(event, trg_name);
		is(old, NULL, "No such trigger - NULL must be returned");
		ok(!event_has_triggers(event), "Created event must be empty");
		event_reset_trigger(event, trg_name, &func);
		found_event = event_get(name, false);
		is(found_event, event, "Event must still exist");
		old = event_find_trigger(event, trg_name);
		is(old, &func, "New trigger must be found");
		ok(event_has_triggers(event), "Event must not be empty");
		is(func_destroy_count, 0, "Func must not be destroyed yet");
		event_reset_trigger(event, trg_name, NULL);
		is(func_destroy_count, 1, "Func must be destroyed");
		old = event_find_trigger(event, trg_name);
		is(old, NULL, "Deleted trigger must not be found");
		ok(!event_has_triggers(event), "Event must be empty");
		found_event = event_get(name, false);
		is(found_event, event, "Referenced event must not be deleted");
		event_unref(event);
		found_event = event_get(name, false);
		is(found_event, NULL, "Empty unused event must be deleted");
		func_destroy_count = 0;
	}
	check_plan();
}

/**
 * A test argument for event_foreach function.
 */
struct test_event_foreach_arg {
	int names_len;
	const char **names;
	int traversed;
};

static bool
test_event_foreach_f(struct event *event, void *arg)
{
	struct test_event_foreach_arg *data =
		(struct test_event_foreach_arg *)arg;
	data->traversed++;
	bool name_found = false;
	for (int i = 0; i < data->names_len && !name_found; ++i) {
		name_found = strcmp(event->name, data->names[i]) == 0;
	}
	ok(name_found, "Traversed event must really exist");
	return true;
}

static bool
test_event_foreach_return_false_f(struct event *event, void *arg)
{
	(void)event;
	struct test_event_foreach_arg *data =
		(struct test_event_foreach_arg *)arg;
	data->traversed++;
	return false;
}

static void
test_event_foreach(void)
{
	plan(10);
	const char *names[] = {
		"event",
		"my_events.event1",
		"my_events.event3",
		"my_events[15].event"
	};
	struct func_adapter_vtab vtab = {.destroy = func_destroy};
	struct func_adapter func = {.vtab = &vtab};
	for (size_t i = 0; i < lengthof(names); ++i) {
		struct event *event = event_get(names[i], true);
		event_ref(event);
		const char *trg_name = tt_sprintf("%zu", i);
		event_reset_trigger(event, trg_name, &func);
	}

	struct test_event_foreach_arg arg = {
		.names_len = lengthof(names),
		.names = names,
		.traversed = 0,
	};

	bool ok = event_foreach(test_event_foreach_f, &arg);
	ok(ok, "Traversal must return true");
	is(arg.traversed, lengthof(names), "All the events must be traversed");

	arg.traversed = 0;
	ok = event_foreach(test_event_foreach_return_false_f, &arg);
	ok(!ok, "Failed traversal must return false");
	is(arg.traversed, 1, "Only one event must be traversed");

	for (size_t i = 0; i < lengthof(names); ++i) {
		struct event *event = event_get(names[i], false);
		const char *trg_name = tt_sprintf("%zu", i);
		event_reset_trigger(event, trg_name, NULL);
	}

	arg.traversed = 0;
	ok = event_foreach(test_event_foreach_f, &arg);
	ok(ok, "Traversal of empty registry must return true");
	is(arg.traversed, 0, "All the events are empty - nothing to traverse");

	/* Unreference all the events. */
	for (size_t i = 0; i < lengthof(names); ++i) {
		struct event *event = event_get(names[i], false);
		fail_if(event == NULL);
		event_unref(event);
	}

	check_plan();
}

static void
test_event_trigger_iterator(void)
{
	plan(14);
	const char *event_name = "test_event";
	const char *trigger_names[] = {
		"0", "1", "2", "3", "4", "5", "6", "7"
	};
	struct func_adapter_vtab vtab = {.destroy = func_destroy};
	struct func_adapter func = {.vtab = &vtab};

	struct event *event = event_get(event_name, true);
	for (int i = lengthof(trigger_names) - 1; i >= 0; --i)
		event_reset_trigger(event, trigger_names[i], &func);

	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, event);
	size_t idx = 0;
	struct func_adapter *curr_trg = NULL;
	const char *curr_name = NULL;
	while (event_trigger_iterator_next(&it, &curr_trg, &curr_name)) {
		is(strcmp(curr_name, trigger_names[idx]), 0,
		   "Triggers must be traversed in reversed order");
		idx++;
	}
	is(idx, lengthof(trigger_names), "All the triggers must be traversed");
	is(curr_trg, NULL, "Exhausted iterator must return NULL trigger");
	is(curr_name, NULL, "Exhausted iterator must return NULL name");

	curr_trg = &func;
	curr_name = "garbage";
	bool has_elems =
		event_trigger_iterator_next(&it, &curr_trg, &curr_name);
	ok(!has_elems, "Iterator must stay exhausted");
	is(curr_trg, NULL, "Exhausted iterator must return NULL trigger");
	is(curr_name, NULL, "Exhausted iterator must return NULL name");

	for (size_t i = 0; i < lengthof(trigger_names); ++i)
		event_reset_trigger(event, trigger_names[i], NULL);

	event_trigger_iterator_destroy(&it);

	check_plan();
}

/**
 * Stops at breakpoint and deletes triggers which are set in del mask.
 */
static void
test_event_iterator_stability_del_step(int breakpoint, const char *del_mask,
				       int trigger_num)
{
	fail_unless(breakpoint < trigger_num);
	size_t left_after_br = 0;
	for (int i = breakpoint + 1; i < trigger_num; ++i) {
		if (del_mask[i] == 0)
			left_after_br++;
	}
	plan((breakpoint + 1) * 2 + left_after_br + 3);

	const char *event_name = "test_event";
	struct func_adapter_vtab vtab = {.destroy = func_destroy};
	struct func_adapter func = {.vtab = &vtab};

	struct event *event = event_get(event_name, true);
	/*
	 * Reference the event to prevent deletion for the test cases that
	 * delete all the triggers from the event.
	 */
	event_ref(event);
	for (int i = trigger_num - 1; i >= 0; --i) {
		const char *trg_name = tt_sprintf("%d", i);
		event_reset_trigger(event, trg_name, &func);
	}

	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, event);
	struct func_adapter *trg = NULL;
	const char *name = NULL;
	for (int i = 0; i <= breakpoint; i++) {
		bool ok = event_trigger_iterator_next(&it, &trg, &name);
		ok(ok, "Iterator must not be exhausted yet");
		const char *trg_name = tt_sprintf("%d", i);
		is(strcmp(name, trg_name), 0,
		   "Triggers must be traversed in reversed order");
	}
	bool delete_all_triggers = true;
	for (int i = 0; i < trigger_num; ++i) {
		if (del_mask[i] != 0) {
			const char *trg_name = tt_sprintf("%d", i);
			event_reset_trigger(event, trg_name, NULL);
		} else {
			delete_all_triggers = false;
		}
	}
	is(event_has_triggers(event), !delete_all_triggers,
	   "Function event_has_triggers must work correctly");
	for (size_t i = 0; i < left_after_br; ++i) {
		bool ok = event_trigger_iterator_next(&it, &trg, &name);
		ok(ok, "Traversal must continue");
	}

	bool ok = event_trigger_iterator_next(&it, &trg, &name);
	ok(!ok, "Iterator must be exhausted");
	event_trigger_iterator_destroy(&it);
	is(event_has_triggers(event), !delete_all_triggers,
	   "Function event_has_triggers must work correctly");

	for (int i = 0; i < trigger_num; ++i) {
		if (del_mask[i] != 0)
			continue;
		const char *trg_name = tt_sprintf("%d", i);
		event_reset_trigger(event, trg_name, NULL);
	}
	event_unref(event);

	check_plan();
}

/**
 * Stops at breakpoint and replaces triggers which are set in replace mask.
 */
static void
test_event_iterator_stability_replace_step(int breakpoint,
					   const char *replace_mask,
					   int trigger_num)
{
	fail_unless(breakpoint < trigger_num);
	plan((breakpoint + 1) * 2 + 3 * (trigger_num - breakpoint - 1) + 3);

	const char *event_name = "test_event";
	struct func_adapter_vtab vtab = {.destroy = func_destroy};
	struct func_adapter func = {.vtab = &vtab};
	struct func_adapter new_func = {.vtab = &vtab};

	struct event *event = event_get(event_name, true);
	for (int i = trigger_num - 1; i >= 0; --i) {
		const char *trg_name = tt_sprintf("%d", i);
		event_reset_trigger(event, trg_name, &func);
	}

	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, event);
	struct func_adapter *trg = NULL;
	const char *name = NULL;
	for (int i = 0; i <= breakpoint; i++) {
		bool ok = event_trigger_iterator_next(&it, &trg, &name);
		ok(ok, "Iterator must not be exhausted yet");
		const char *trg_name = tt_sprintf("%d", i);
		is(strcmp(name, trg_name), 0,
		   "Triggers must be traversed in reversed order");
	}
	for (int i = 0; i < trigger_num; ++i) {
		if (replace_mask[i] != 0) {
			const char *trg_name = tt_sprintf("%d", i);
			event_reset_trigger(event, trg_name, &new_func);
		}
	}
	ok(event_has_triggers(event), "Event must not be empty");
	for (int i = breakpoint + 1; i < trigger_num; ++i) {
		bool ok = event_trigger_iterator_next(&it, &trg, &name);
		ok(ok, "Traversal must continue");
		const char *trg_name = tt_sprintf("%d", i);
		is(strcmp(name, trg_name), 0,
		   "Triggers must be traversed in reversed order");
		if (replace_mask[i] != 0) {
			is(trg, &new_func, "Trigger must be replaced");
		} else {
			is(trg, &func, "Trigger must be old");
		}
	}
	bool ok = event_trigger_iterator_next(&it, &trg, &name);
	ok(!ok, "Iterator must be exhausted");
	event_trigger_iterator_destroy(&it);
	ok(event_has_triggers(event), "Event must not be empty");

	for (int i = 0; i < trigger_num; ++i) {
		const char *trg_name = tt_sprintf("%d", i);
		event_reset_trigger(event, trg_name, NULL);
	}

	check_plan();
}

/**
 * Checks if iteration is stable in the cases of deletions and replaces.
 */
static void
test_event_trigger_iterator_stability(void)
{
	plan(6);
	char mask[8];
	const size_t trigger_num = lengthof(mask);
	memset(mask, 0, trigger_num);
	size_t br = trigger_num / 2;
	/**
	 * Delete or replace current trigger.
	 */
	mask[br] = 1;
	test_event_iterator_stability_del_step(br, mask, trigger_num);
	test_event_iterator_stability_replace_step(br, mask, trigger_num);
	memset(mask, 0, trigger_num);
	/**
	 * Delete or replace current, previous and next triggers.
	 */
	mask[br - 1] = 1;
	mask[br] = 1;
	mask[br + 1] = 1;
	test_event_iterator_stability_del_step(br, mask, trigger_num);
	test_event_iterator_stability_replace_step(br, mask, trigger_num);
	memset(mask, 0, trigger_num);
	/**
	 * Delete or replace all the triggers in the middle of iteration.
	 */
	memset(mask, 1, trigger_num);
	test_event_iterator_stability_del_step(br, mask, trigger_num);
	test_event_iterator_stability_replace_step(br, mask, trigger_num);
	memset(mask, 0, trigger_num);

	check_plan();
}

static void
test_event_trigger_temporary_step(const char *tmp_mask, int trigger_num)
{
	size_t non_tmp_count = 0;
	for (int i = 0; i < trigger_num; ++i) {
		if (tmp_mask[i] == 0)
			non_tmp_count++;
	}
	plan(2 * (trigger_num + non_tmp_count) + 2);

	const char *event_name = "test_event";
	struct func_adapter_vtab vtab = {.destroy = func_destroy};
	struct func_adapter func = {.vtab = &vtab};
	struct func_adapter new_func = {.vtab = &vtab};

	struct event *event = event_get(event_name, true);
	event_ref(event);
	for (int i = trigger_num; i >= 0; --i) {
		const char *trg_name = tt_sprintf("%d", i);
		/* Extra temporary trigger to delete it. */
		if (i == trigger_num || tmp_mask[i]) {
			event_reset_trigger_with_flags(
				event, trg_name, &func,
				EVENT_TRIGGER_IS_TEMPORARY);
		} else {
			event_reset_trigger(event, trg_name, &func);
		}
	}
	event_reset_trigger(event, tt_sprintf("%d", trigger_num), NULL);

	struct event_trigger_iterator it;
	event_trigger_iterator_create(&it, event);
	struct func_adapter *trg = NULL;
	const char *name = NULL;
	for (int i = 0; i < trigger_num; i++) {
		bool ok = event_trigger_iterator_next(&it, &trg, &name);
		ok(ok, "Iterator must not be exhausted yet");
		const char *trg_name = tt_sprintf("%d", i);
		is(strcmp(name, trg_name), 0,
		   "Triggers must be traversed in reversed order");
	}
	bool ok = event_trigger_iterator_next(&it, &trg, &name);
	ok(!ok, "Iterator must be exhausted");
	event_trigger_iterator_destroy(&it);

	event_remove_temporary_triggers(event);

	event_trigger_iterator_create(&it, event);
	for (int i = 0; i < trigger_num; ++i) {
		if (tmp_mask[i] != 0)
			continue;
		ok = event_trigger_iterator_next(&it, &trg, &name);
		ok(ok, "Traversal must continue");
		const char *trg_name = tt_sprintf("%d", i);
		is(strcmp(name, trg_name), 0,
		   "Triggers must be traversed in reversed order");
	}
	ok = event_trigger_iterator_next(&it, &trg, &name);
	ok(!ok, "Iterator must be exhausted");
	event_trigger_iterator_destroy(&it);

	for (int i = 0; i < trigger_num; ++i) {
		const char *trg_name = tt_sprintf("%d", i);
		event_reset_trigger(event, trg_name, NULL);
	}
	event_unref(event);

	check_plan();
}

static void
test_event_trigger_temporary(void)
{
	plan(3);
	char mask[8];
	const size_t trigger_num = lengthof(mask);
	memset(mask, 0, trigger_num);

	mask[trigger_num / 2] = 1;
	test_event_trigger_temporary_step(mask, trigger_num);
	memset(mask, 0, trigger_num);

	mask[0] = 1;
	mask[trigger_num / 2] = 1;
	mask[trigger_num - 1] = 1;
	test_event_trigger_temporary_step(mask, trigger_num);
	memset(mask, 0, trigger_num);

	memset(mask, 1, trigger_num);
	test_event_trigger_temporary_step(mask, trigger_num);
	memset(mask, 0, trigger_num);
	check_plan();
}

static void
test_event_free(void)
{
	plan(1);

	struct func_adapter_vtab vtab = {.destroy = func_destroy};
	struct func_adapter func = {.vtab = &vtab};
	const char *trigger_names[] = {
		"trigger[1]",
		"trigger.second",
		"another_trigger"
	};
	const char *event_names[] = {
		"name",
		"name with spaces",
		"namespace.name",
		"NAMESPACE[123].name"
	};
	func_destroy_count = 0;
	for (size_t i = 0; i < lengthof(event_names); ++i) {
		const char *name = event_names[i];
		struct event *event = event_get(name, true);
		for (size_t j = 0; j < lengthof(trigger_names); ++j)
			event_reset_trigger(event, trigger_names[j], &func);
	}
	event_free();
	is(func_destroy_count, lengthof(event_names) * lengthof(trigger_names),
	   "All triggers must be destroyed");
	/* Initialize event back. */
	event_init();

	check_plan();
}

static void
test_event_ref_all_triggers(void)
{
	plan(3);

	struct func_adapter_vtab vtab = {.destroy = func_destroy};
	struct func_adapter func = {.vtab = &vtab};
	const char *trg_names[] = {
		"test.trg[1]",
		"test.trg[2]",
		"test.trg[3]",
		"test.trg[4]",
		"test.trg[5]"
	};
	const char *name = "event_name";
	struct event *event = event_get(name, true);
	isnt(event, NULL, "Event must be created");
	/* Reference the event. */
	event_ref(event);
	for (size_t i = 0; i < lengthof(trg_names); i++)
		event_reset_trigger(event, trg_names[i], &func);

	func_destroy_count = 0;

	event_ref_all_triggers(event);
	for (size_t i = 0; i < lengthof(trg_names); i++)
		event_reset_trigger(event, trg_names[i], NULL);

	is(func_destroy_count, 0, "No triggers must be destroyed yet");

	event_free();
	is(func_destroy_count, lengthof(trg_names),
	   "All triggers must be destroyed");
	/* Initialize event back. */
	event_init();

	check_plan();
}

static int
on_change_trigger_run_f(struct trigger *trg, void *arg)
{
	trg->data = arg;
	return 0;
}

static void
test_event_on_change(void)
{
	const char *names[] = {
		"name",
		"name with spaces",
		"namespace.name",
		"NAMESPACE[123].name"
	};

	struct trigger triggers[3];
	for (size_t i = 0; i < lengthof(triggers); i++) {
		struct trigger *trigger = &triggers[i];
		trigger_create(trigger, on_change_trigger_run_f, NULL, NULL);
		event_on_change(trigger);
	}

	plan(3 * lengthof(names) * lengthof(triggers));

	struct func_adapter_vtab vtab = {.destroy = func_destroy};
	struct func_adapter func = {.vtab = &vtab};
	const char *trg_name = "my_triggers.trg[1]";
	for (size_t i = 0; i < lengthof(names); i++) {
		const char *name = names[i];
		struct event *event = event_get(name, true);
		event_reset_trigger(event, trg_name, NULL);
		for (size_t i = 0; i < lengthof(triggers); i++) {
			struct trigger *trigger = &triggers[i];
			is(trigger->data, event,
			   "On change triggers must be called");
		}
	}
	for (size_t i = 0; i < lengthof(names); i++) {
		const char *name = names[i];
		struct event *event = event_get(name, true);
		event_reset_trigger(event, trg_name, &func);
		for (size_t i = 0; i < lengthof(triggers); i++) {
			struct trigger *trigger = &triggers[i];
			is(trigger->data, event,
			   "On change triggers must be called");
		}
	}
	for (size_t i = 0; i < lengthof(names); i++) {
		const char *name = names[i];
		struct event *event = event_get(name, true);
		event_reset_trigger(event, trg_name, NULL);
		for (size_t i = 0; i < lengthof(triggers); i++) {
			struct trigger *trigger = &triggers[i];
			is(trigger->data, event,
			   "On change triggers must be called");
		}
	}
	for (size_t i = 0; i < lengthof(triggers); i++) {
		trigger_clear(&triggers[i]);
	}
	check_plan();
}

static int
test_main(void)
{
	plan(8);
	test_basic();
	test_event_foreach();
	test_event_trigger_iterator();
	test_event_trigger_iterator_stability();
	test_event_trigger_temporary();
	test_event_free();
	test_event_ref_all_triggers();
	test_event_on_change();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	event_init();
	int rc = test_main();
	event_free();
	fiber_free();
	memory_free();
	return rc;
}
