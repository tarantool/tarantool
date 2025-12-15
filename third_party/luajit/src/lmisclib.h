/*
** Miscellaneous public C API extensions.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#ifndef _LMISCLIB_H
#define _LMISCLIB_H

#include "lua.h"

/* API for obtaining various platform metrics. */

struct luam_Metrics {
  /*
  ** Number of strings being interned (i.e. the string with the
  ** same payload is found, so a new one is not created/allocated).
  */
  size_t strhash_hit;
  /* Total number of strings allocations during the platform lifetime. */
  size_t strhash_miss;

  /* Amount of allocated string objects. */
  size_t gc_strnum;
  /* Amount of allocated table objects. */
  size_t gc_tabnum;
  /* Amount of allocated udata objects. */
  size_t gc_udatanum;
  /* Amount of allocated cdata objects. */
  size_t gc_cdatanum;

  /* Memory currently allocated. */
  size_t gc_total;
  /* Total amount of freed memory. */
  size_t gc_freed;
  /* Total amount of allocated memory. */
  size_t gc_allocated;

  /* Count of incremental GC steps per state. */
  size_t gc_steps_pause;
  size_t gc_steps_propagate;
  size_t gc_steps_atomic;
  size_t gc_steps_sweepstring;
  size_t gc_steps_sweep;
  size_t gc_steps_finalize;

  /*
  ** Overall number of snap restores (amount of guard assertions
  ** leading to stopping trace executions).
  */
  size_t jit_snap_restore;
  /* Overall number of abort traces. */
  size_t jit_trace_abort;
  /* Total size of all allocated machine code areas. */
  size_t jit_mcode_size;
  /* Amount of JIT traces. */
  unsigned int jit_trace_num;
};

LUAMISC_API void luaM_metrics(lua_State *L, struct luam_Metrics *metrics);

/* --- Sysprof - platform and lua profiler -------------------------------- */

/* Profiler configurations. */
/*
** Writer function for profile events. Must be async-safe, see also
** `man 7 signal-safety`.
** Should return amount of written bytes on success or zero in case of error.
** Setting *data to NULL means end of profiling.
** For details see <lj_wbuf.h>.
*/
typedef size_t (*luam_Sysprof_writer)(const void **data, size_t len, void *ctx);
/*
** Callback on profiler stopping. Required for correctly cleaning
** at VM finalization when profiler is still running.
** Returns zero on success.
*/
typedef int (*luam_Sysprof_on_stop)(void *ctx, uint8_t *buf);
/*
** Backtracing function for the host stack. Should call `frame_writer` on
** each frame in the stack in the order from the stack top to the stack
** bottom. The `frame_writer` function is implemented inside the sysprof
** and will be passed to the `backtracer` function. If `frame_writer` returns
** NULL, backtracing should be stopped. If `frame_writer` returns not NULL,
** the backtracing should be continued if there are frames left.
*/
typedef void (*luam_Sysprof_backtracer)(void *(*frame_writer)(int frame_no, void *addr));

/*
** DEFAULT mode collects only data for luam_sysprof_counters, which is stored
** in memory and can be collected with luaM_sysprof_report after profiler
** stops.
*/
#define LUAM_SYSPROF_DEFAULT 0
/*
** LEAF mode = DEFAULT + streams samples with only top frames of host and
** guests stacks in format described in <lj_sysprof.h>
*/
#define LUAM_SYSPROF_LEAF 1
/*
** CALLGRAPH mode = DEFAULT + streams samples with full callchains of host
** and guest stacks in format described in <lj_sysprof.h>
*/
#define LUAM_SYSPROF_CALLGRAPH 2

struct luam_Sysprof_Counters {
  uint64_t vmst_interp;
  uint64_t vmst_lfunc;
  uint64_t vmst_ffunc;
  uint64_t vmst_cfunc;
  uint64_t vmst_gc;
  uint64_t vmst_exit;
  uint64_t vmst_record;
  uint64_t vmst_opt;
  uint64_t vmst_asm;
  uint64_t vmst_trace;
  /*
  ** XXX: Order of vmst counters is important: it should be the same as the
  ** order of the vmstates.
  */
  uint64_t samples;
};

/* Profiler options. */
struct luam_Sysprof_Options {
  /* Profiling mode. */
  uint8_t mode;
  /* Sampling interval in msec. */
  uint64_t interval;
  /* Custom buffer to write data. */
  uint8_t *buf;
  /* The buffer's size. */
  size_t len;
  /* Context for the profile writer and final callback. */
  void *ctx;
};

#define PROFILE_SUCCESS 0
#define PROFILE_ERRUSE  1
#define PROFILE_ERRRUN  2
#define PROFILE_ERRMEM  3
#define PROFILE_ERRIO   4

LUAMISC_API int luaM_sysprof_set_writer(luam_Sysprof_writer writer);

LUAMISC_API int luaM_sysprof_set_on_stop(luam_Sysprof_on_stop on_stop);

LUAMISC_API int luaM_sysprof_set_backtracer(luam_Sysprof_backtracer backtracer);

LUAMISC_API int luaM_sysprof_start(lua_State *L,
                                   const struct luam_Sysprof_Options *opt);

LUAMISC_API int luaM_sysprof_stop(lua_State *L);

LUAMISC_API int luaM_sysprof_report(struct luam_Sysprof_Counters *counters);


#define LUAM_MISCLIBNAME "misc"
LUALIB_API int luaopen_misc(lua_State *L);

#endif /* _LMISCLIB_H */
