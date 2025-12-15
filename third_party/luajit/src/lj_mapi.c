/*
** Miscellaneous public C API extensions.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#define lj_mapi_c
#define LUA_CORE

#include "lua.h"
#include "lmisclib.h"

#include "lj_obj.h"
#include "lj_dispatch.h"

#if LJ_HASJIT
#include "lj_jit.h"
#endif

#include "lj_sysprof.h"

LUAMISC_API void luaM_metrics(lua_State *L, struct luam_Metrics *metrics)
{
  global_State *g = G(L);
  GCState *gc = &g->gc;
#if LJ_HASJIT
  jit_State *J = G2J(g);
#endif

  lj_assertL(metrics != NULL, "uninitialized metrics struct");

  metrics->strhash_hit = g->strhash_hit;
  metrics->strhash_miss = g->strhash_miss;

  metrics->gc_strnum = g->strnum;
  metrics->gc_tabnum = gc->tabnum;
  metrics->gc_udatanum = gc->udatanum;
#if LJ_HASFFI
  metrics->gc_cdatanum = gc->cdatanum;
#else
  metrics->gc_cdatanum = 0;
#endif

  metrics->gc_total = gc->total;
  metrics->gc_freed = gc->freed;
  metrics->gc_allocated = gc->allocated;

  metrics->gc_steps_pause = gc->state_count[GCSpause];
  metrics->gc_steps_propagate = gc->state_count[GCSpropagate];
  metrics->gc_steps_atomic = gc->state_count[GCSatomic];
  metrics->gc_steps_sweepstring = gc->state_count[GCSsweepstring];
  metrics->gc_steps_sweep = gc->state_count[GCSsweep];
  metrics->gc_steps_finalize = gc->state_count[GCSfinalize];

#if LJ_HASJIT
  metrics->jit_snap_restore = J->nsnaprestore;
  metrics->jit_trace_abort = J->ntraceabort;
  metrics->jit_mcode_size = J->szallmcarea;
  metrics->jit_trace_num = J->tracenum;
#else
  metrics->jit_snap_restore = 0;
  metrics->jit_trace_abort = 0;
  metrics->jit_mcode_size = 0;
  metrics->jit_trace_num = 0;
#endif
}

/* --- Platform and Lua profiler ------------------------------------------ */
LUAMISC_API int luaM_sysprof_set_writer(luam_Sysprof_writer writer)
{
  return lj_sysprof_set_writer(writer);
}

LUAMISC_API int luaM_sysprof_set_on_stop(luam_Sysprof_on_stop on_stop)
{
  return lj_sysprof_set_on_stop(on_stop);
}

LUAMISC_API int luaM_sysprof_set_backtracer(luam_Sysprof_backtracer backtracer)
{
  return lj_sysprof_set_backtracer(backtracer);
}

LUAMISC_API int luaM_sysprof_start(lua_State *L,
                                   const struct luam_Sysprof_Options *opt)
{
  return lj_sysprof_start(L, opt);
}

/* Stop profiling. */
LUAMISC_API int luaM_sysprof_stop(lua_State *L)
{
  return lj_sysprof_stop(L);
}

LUAMISC_API int luaM_sysprof_report(struct luam_Sysprof_Counters *counters)
{
  return lj_sysprof_report(counters);
}
