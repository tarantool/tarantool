/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct cord;

/**
 * On the first call, creates and returns a new thread-local cord object.
 * On all subsequent calls in the same thread, returns the object created
 * earlier. The cord object is destroyed at thread exit.
 */
struct cord *
cord_on_demand(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
