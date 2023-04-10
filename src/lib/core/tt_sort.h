/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#define TT_SORT_THREADS_MAX 256

typedef int
(*tt_sort_compare_f)(const void *a, const void *b, void *arg);

/**
 * A variant of sample sort algorithm. Sort is executed in multiple threads.
 * The calling thread itself does not take a working load and yields while
 * waiting for working threads to finish.
 *
 * Arguments:
 *  data         - data to be sorted
 *  elem_count   - number of elements in data
 *  elem_size    - sizeof of single data element
 *  cmp          - comparison function with usual semantics (as in qsort(3)) and
 *                 extra argument
 *  arg          - extra argument to be passed to comparison function
 *  thread_count - number of threads to execute the sort in
 */
void
tt_sort(void *data, size_t elem_count, size_t elem_size,
	tt_sort_compare_f cmp, void *cmp_arg, int thread_count);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
