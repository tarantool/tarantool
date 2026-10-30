/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct func;
struct func_def;

/** Create new SQL user-defined function. */
struct func *
func_sql_expr_new(const struct func_def *def);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined __cplusplus */
