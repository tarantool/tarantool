/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct tuple_constraint;
struct space;

/**
 * Initialize constraint assuming that a func_name identifies a struct func
 * in func_cache. If this is a field constraint, @a is_field must be true.
 * If this is a complex constraint, @a is_field must be false.
 */
int
tuple_constraint_func_init(struct tuple_constraint *constr,
			   struct space *space, bool is_field);

#ifdef __cplusplus
} /* extern "C" */
#endif
