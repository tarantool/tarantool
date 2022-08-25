#pragma once

// This is a C module (compiled as bench.so) which is supposed to be used
// from Lua script (launch.lua). So see firstly Lua sources as entry point.

#include "module.h"

#if defined(__cplusplus)
extern "C" {
#endif

int
print_header(box_function_ctx_t *ctx, const char *args, const char *args_end);

int
run(box_function_ctx_t *ctx, const char *args, const char *args_end);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
