/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "say.h"
#include "trivia/util.h"

/**
 * An x* variant of a memory allocation function calls the original function
 * and panics if it fails (i.e. it should never return NULL).
 */
#define xalloc_impl(size, func, args...)					\
	({									\
		void *ret = func(args);						\
		if (unlikely(ret == NULL))					\
			panic("Can't allocate %zu bytes", (size_t)(size));	\
		ret;								\
	})

#define xmalloc(size)		xalloc_impl((size), malloc, (size))
#define xcalloc(n, size)	xalloc_impl((n) * (size), calloc, (n), (size))
#define xrealloc(ptr, size)	xalloc_impl((size), realloc, (ptr), (size))
#define xstrdup(s)		xalloc_impl(strlen((s)) + 1, strdup, (s))
#define xstrndup(s, n)		xalloc_impl((n) + 1, strndup, (s), (n))
