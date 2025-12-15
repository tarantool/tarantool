#include "test.h"

static int test_ok(void *test_state)
{
	UNUSED(test_state);
	return TEST_EXIT_SUCCESS;
}

static int test_skip(void *test_state)
{
	UNUSED(test_state);
	return skip("test skip");
}

static int test_todo(void *test_state)
{
	UNUSED(test_state);
	return todo("test todo");
}

int main(void)
{
	const struct test_unit tgroup[] = {
		test_unit_def(test_ok),
		test_unit_def(test_skip),
		test_unit_def(test_todo)
	};
	return test_run_group(tgroup, NULL);
}
