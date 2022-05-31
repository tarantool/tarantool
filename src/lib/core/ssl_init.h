/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

/**
 * Initializes OpenSSL library. Internal method. Use ssl_init() instead.
 */
void
ssl_init_impl(void);

/**
 * Frees OpenSSL library. Internal method. Use ssl_free() instead.
 */
void
ssl_free_impl(void);
