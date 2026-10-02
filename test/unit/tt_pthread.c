/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */

#define UNIT_TAP_COMPATIBLE 1

#include <stdint.h>

#include "tt_pthread.h"
#include "unit.h"

/** Keep the local on the native stack rather than ASan's fake stack. */
static NOINLINE NO_SANITIZE_ADDRESS bool
stack_contains_local(void)
{
	char local;
	void *bottom;
	size_t size;
	tt_pthread_attr_getstack(pthread_self(), &bottom, &size);
	uintptr_t addr = (uintptr_t)&local;
	return addr >= (uintptr_t)bottom && addr - (uintptr_t)bottom < size;
}

static void *
check_thread_stack(void *arg)
{
	*(bool *)arg = stack_contains_local();
	return NULL;
}

static void
test_stack_bounds(void)
{
	header();
	plan(4);
	ok(stack_contains_local(), "initial thread stack contains a local");
	bool contains_local = false;
	pthread_t thread;
	int rc = pthread_create(&thread, NULL, check_thread_stack,
				&contains_local);
	is(rc, 0, "create thread for stack bounds test");
	if (rc != 0)
		goto out;
	is(pthread_join(thread, NULL), 0, "join thread for stack bounds test");
	ok(contains_local, "pthread stack contains local variable");
out:
	check_plan();
	footer();
}

int
main(void)
{
	plan(1);
	test_stack_bounds();
	return check_plan();
}
