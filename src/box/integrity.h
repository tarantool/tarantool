/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "trivia/config.h"

#if defined(ENABLE_INTEGRITY)

/**
 * Auxiliary function of the integrity module that allows to verify
 * a file from C code. Must be called only after module initialization.
 */
bool
integrity_verify_file(const char *path, const char *buffer, size_t size);

#else /* !defined(ENABLE_INTEGRITY) */

/**
 * Placeholder function that is properly implemented inside integrity module.
 * It is available only inside Enterprise Edition builds.
 */
static inline bool
integrity_verify_file(const char *path, const char *buffer, size_t size)
{
	(void)path;
	(void)buffer;
	(void)size;
	return true;
}

#endif /* !defined(ENABLE_INTEGRITY) */
