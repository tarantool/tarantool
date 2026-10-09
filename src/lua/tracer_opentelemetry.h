#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;

/** Register the tracer_opentelemetry C part as a built-in module. */
void
tarantool_lua_tracer_opentelemetry_init(struct lua_State *L);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
