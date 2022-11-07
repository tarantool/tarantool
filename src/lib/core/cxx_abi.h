/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "trivia/config.h"

#ifdef ENABLE_BACKTRACE
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/*
 * Wrapper around `abi::__cxa_demangle` from the C++ ABI, for details see:
 * https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html.
 *
 * Returns pointer to demangled function name. Pointer is stored on a buffer
 * with thread local lifetime semantic.
 *
 * On demangle failures just return mangled name.
 */
const char *
cxx_abi_demangle(const char *mangled_name);
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
#endif /* ENABLE_BACKTRACE */
