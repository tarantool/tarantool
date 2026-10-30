/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026, Tarantool AUTHORS, please see AUTHORS file.
 */

static void
hidden_function(void)
{
	__asm__ __volatile__("" ::: "memory");
}

void *
no_symbol_address(void)
{
	return (void *)hidden_function;
}
