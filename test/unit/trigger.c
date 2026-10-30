#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include <stdbool.h>
#include <assert.h>
#include "memory.h"
#include "fiber.h"
#include "trigger.h"

/* Length of trigger chains to test. */
#define TEST_LENGTH 5
#define FUNC_COUNT (TEST_LENGTH + 3)

struct test_trigger {
	struct trigger base;
	int id;
	int target_id;
};

/* How many times each trigger was run. */
static int was_run[TEST_LENGTH];
/* Function codes for each trigger. */
static int funcs[TEST_LENGTH];
static struct test_trigger triggers[TEST_LENGTH];

static RLIST_HEAD(list_a);
static RLIST_HEAD(list_b);

static int
trigger_nop_f(struct trigger *trigger, void *event)
{
	struct test_trigger *trig = (struct test_trigger *)trigger;
	was_run[trig->id]++;
	return 0;
}

static int
trigger_err_f(struct trigger *trigger, void *event)
{
	trigger_nop_f(trigger, event);
	return -1;
}

static int
trigger_swap_f(struct trigger *trigger, void *event)
{
	trigger_nop_f(trigger, event);
	rlist_swap(&list_a, &list_b);
	return 0;
}

static int
trigger_clear_f(struct trigger *trigger, void *event)
{
	trigger_nop_f(trigger, event);
	struct test_trigger *trig = (struct test_trigger *)trigger;
	int clear_id = trig->target_id;
	trigger_clear(&triggers[clear_id].base);
	return 0;
}

/**
 * Types of trigger functions which might harm trigger_run one way or another.
 */
enum func_type {
	/** Do nothing. */
	FUNC_TYPE_NOP,
	/** Error. */
	FUNC_TYPE_ERR,
	/** Swap trigger list heads. */
	FUNC_TYPE_SWAP,
	/** Clear one of the triggers: self or other. */
	FUNC_TYPE_CLEAR,
};

static int
func_type_by_no(int func_no)
{
	assert(func_no >= 0 && func_no < FUNC_COUNT);
	if (func_no < TEST_LENGTH)
		return FUNC_TYPE_CLEAR;
	else if (func_no == TEST_LENGTH)
		return FUNC_TYPE_ERR;
	else if (func_no == TEST_LENGTH + 1)
		return FUNC_TYPE_NOP;
	else /* if (func_no == TEST_LENGTH + 2) */
		return FUNC_TYPE_SWAP;
}

static void
fill_trigger_list(struct rlist *list, bool *should_run, int direction)
{
	for (int i = 0; i < TEST_LENGTH; i++)
		should_run[i] = true;
	rlist_create(list);
	/*
	 * Create triggers and deduce which should and which shouldn't run based
	 * on trigger functions and run direction.
	 */
	for (int i = (TEST_LENGTH - 1) * (1 - direction) / 2;
	     i >= 0 && i < TEST_LENGTH; i += direction) {
		was_run[i] = 0;
		triggers[i].id = i;
		int func_no = funcs[i];
		switch (func_type_by_no(func_no)) {
		case FUNC_TYPE_CLEAR:
			trigger_create(&triggers[i].base,
				       trigger_clear_f, NULL, NULL);
			triggers[i].target_id = func_no;
			if (!should_run[i])
				break;
			int target_id = funcs[i];
			if (direction * i < direction * target_id)
				should_run[target_id] = false;
			break;
		case FUNC_TYPE_ERR:
			trigger_create(&triggers[i].base, trigger_err_f,
				       NULL, NULL);
			if (!should_run[i])
				break;
			for (int j = i + direction; j >= 0 && j < TEST_LENGTH;
			     j += direction) {
				should_run[j] = false;
			}
			break;
		case FUNC_TYPE_NOP:
			trigger_create(&triggers[i].base, trigger_nop_f,
				       NULL, NULL);
			break;
		case FUNC_TYPE_SWAP:
			trigger_create(&triggers[i].base,
				       trigger_swap_f, NULL, NULL);
			break;
		}
	}
	/*
	 * Add triggers in reverse order, so that trigger[0] is first to run in
	 * direct order and last to run in reverse order.
	 */
	for (int i = TEST_LENGTH - 1; i >= 0; i--)
		trigger_add(list, &triggers[i].base);
}

static void
test_trigger_one(void)
{
	plan(2 * TEST_LENGTH);
	for (int direction = -1; direction <= 1; direction += 2) {
		bool should_run[TEST_LENGTH];
		fill_trigger_list(&list_a, should_run, direction);
		if (direction == 1)
			trigger_run(&list_a, NULL);
		else
			trigger_run_reverse(&list_a, NULL);
		for (int i = 0; i < TEST_LENGTH; i++) {
			ok((should_run[i] && was_run[i] == 1) ||
			   (!should_run[i] && was_run[i] == 0),
			   "Triggers ran correctly");
		}
	}
	check_plan();
}

static void
test_trigger(int pos, bool had_clear)
{
	if (pos == TEST_LENGTH)
		return test_trigger_one();
	int i = had_clear ? TEST_LENGTH : 0;
	plan(FUNC_COUNT - i);
	for (i; i < FUNC_COUNT; i++) {
		funcs[pos] = i;
		test_trigger(pos + 1, had_clear || func_type_by_no(i) ==
						   FUNC_TYPE_CLEAR);
	}
	check_plan();
}

static int
test_trigger_clear_during_run(void)
{
	header();
	plan(1);

	test_trigger(0, false);

	footer();
	return check_plan();
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);

	plan(1);
	test_trigger_clear_during_run();

	fiber_free();
	memory_free();
	return check_plan();
}
