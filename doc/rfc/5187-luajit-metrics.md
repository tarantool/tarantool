# LuaJIT metrics

* **Status**: In progress
* **Start date**: 17-07-2020
* **Authors**: Sergey Kaplun @Buristan skaplun@tarantool.org,
               Igor Munkin @igormunkin imun@tarantool.org,
               Sergey Ostanevich @sergos sergos@tarantool.org
* **Issues**: [#5187](https://github.com/tarantool/tarantool/issues/5187)

## Summary

LuaJIT metrics provide extra information about the Lua state. They consist of
GC metrics (overall amount of objects and memory usage), JIT stats (both
related to the compiled traces and the engine itself), string hash hits/misses.

## Background and motivation

One can be curious about their application performance. We are going to provide
various metrics about the several platform subsystems behaviour. GC pressure
produced by user code can weight down all application performance. Irrelevant
traces compiled by the JIT engine can just burn CPU time with no benefits as a
result. String hash collisions can lead to DoS caused by a single request. All
these metrics should be well monitored by users wanting to improve the
performance of their application.

## Detailed design

The additional header <lmisclib.h> is introduced to extend the existing LuaJIT
C API with new interfaces. The first function provided via this header is the
following:

```c
/* API for obtaining various platform metrics. */

LUAMISC_API void luaM_metrics(lua_State *L, struct luam_Metrics *metrics);
```

This function fills the structure pointed to by `metrics` with the corresponding
metrics related to Lua state anchored to the given coroutine `L`.

The `struct luam_Metrics` has the following definition:

```c
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
```

Couple of words about how metrics are collected:
- `strhash_*` -- whenever the string with the same payload is found, so a new
  one is not created/allocated, there is incremented `strhash_hit` counter, if
  a new one string created/allocated then `strhash_miss` is incremented
  instead.
- `gc_*num`, `jit_trace_num` -- corresponding counter is incremented whenever a
  new object is allocated. When object is collected by GC its counter is
  decremented.
- `gc_total`, `gc_allocated`, `gc_freed` -- any time when allocation function
  is called `gc_allocated` and/or `gc_freed` is increased and `gc_total` is
  increased when memory is allocated or reallocated, is decreased when memory
  is freed.
- `gc_steps_*` -- corresponding counter is incremented whenever Garbage
  Collector starts to execute an incremental step of garbage collection.
- `jit_snap_restore` -- whenever JIT machine exits from the trace and restores
  interpreter state `jit_snap_restore` counter is incremented.
- `jit_trace_abort` -- whenever JIT compiler can't record the trace in case NYI
  BC or builtins this counter is incremented.
- `jit_mcode_size` -- whenever new MCode area is allocated `jit_mcode_size` is
  increased at corresponding size in bytes. Sets to 0 when all mcode area is
  freed. When a trace is collected by GC this value doesn't change. This area
  will be reused later for other traces. MCode area is linked with `jit_State`
  not with trace by itself. Traces just reserve MCode area that needed.

All metrics are collected throughout the platform uptime. These metrics
increase monotonically and can overflow:
  - `strhash_hit`
  - `strhash_miss`
  - `gc_freed`
  - `gc_allocated`
  - `gc_steps_pause`
  - `gc_steps_propagate`
  - `gc_steps_atomic`
  - `gc_steps_sweepstring`
  - `gc_steps_sweep`
  - `gc_steps_finalize`
  - `jit_snap_restore`
  - `jit_trace_abort`

They make sense only with comparing with their value from a previous
`luaM_metrics()` call.

There is also a complement introduced for Lua space -- `misc.getmetrics()`.
This function is just a wrapper for `luaM_metrics()` returning a Lua table with
the similar metrics. All returned values are presented as numbers with cast to
double, so there is a corresponding precision loss. Function usage is quite
simple:
```
$ ./src/tarantool
Tarantool 2.5.0-267-gbf047ad44
type 'help' for interactive help
tarantool> misc.getmetrics()
---
- gc_freed: 2245853
  strhash_hit: 53965
  gc_steps_atomic: 6
  strhash_miss: 6879
  gc_steps_sweepstring: 17920
  gc_strnum: 5759
  gc_tabnum: 1813
  gc_cdatanum: 89
  jit_snap_restore: 0
  gc_total: 1370812
  gc_udatanum: 17
  gc_steps_finalize: 0
  gc_allocated: 3616665
  jit_trace_num: 0
  gc_steps_sweep: 297
  jit_trace_abort: 0
  jit_mcode_size: 0
  gc_steps_propagate: 10181
  gc_steps_pause: 7
...
```

## How to use

This section describes small example of metrics usage.

For example amount of `strhash_misses` can be shown for tracking of new string
objects allocations. For example if we add code like:
```lua
local function sharded_storage_func(storage_name, func_name)
    return 'sharded_storage.storages.' .. storage_name .. '.' .. func_name
end
```
increase in slope curve of `strhash_misses` means, that after your changes
there are more new strings allocating at runtime. Of course slope curve of
`strhash_misses` _should_ be less than slope curve of `strhash_hits`.

Slope curves of `gc_freed` and `gc_allocated` can be used for analysis of GC
pressure of your application (less is better).

Also we can check some hacky optimization with these metrics. For example let's
assume that we have this code snippet:
```lua
local old_metrics = misc.getmetrics()
local t = {}
for i = 1, 513 do
    t[i] = i
end
local new_metrics = misc.getmetrics()
local diff = new_metrics.gc_allocated - old_metrics.gc_allocated
```
`diff` equals to 18879 after running of this chunk.

But if we change table initialization to
```lua
local table_new = require "table.new"
local old_metrics = misc.getmetrics()
local t = table_new(513,0)
for i = 1, 513 do
    t[i] = i
end
local new_metrics = misc.getmetrics()
local diff = new_metrics.gc_allocated - old_metrics.gc_allocated
```
`diff` shows us only 5895.

Slope curves of `gc_steps_*` can be used for tracking of GC pressure too. For
long time observations you will see periodic increment for `gc_steps_*` metrics
-- for example longer period of `gc_steps_atomic` increment is better. Also
additional amount of `gc_steps_propagate` in one period can be used to
indirectly estimate amount of objects. These values also correlate with the
step multiplier of the GC. The amount of incremental steps can grow, but
one step can process a small amount of objects. So these metrics should be
considered together with GC setup.

Amount of `gc_*num` is useful for control of memory leaks -- total amount of
these objects should not growth nonstop (you can also track `gc_total` for
this). Also `jit_mcode_size` can be used for tracking amount of allocated
memory for traces machine code.

Slope curves of `jit_trace_abort` shows how many times trace hasn't been
compiled when the attempt was made (less is better).

Amount of `gc_trace_num` is shown how much traces was generated (_usually_
more is better).

And the last one -- `gc_snap_restores` can be used for estimation when LuaJIT
is stop trace execution. If slope curves of this metric growth after changing
old code it can mean performance degradation.

Assumes that we have code like this:
```lua
local function foo(i)
    return i <= 5 and i or tostring(i)
end
-- minstitch option needs to emulate nonstitching behaviour
jit.opt.start(0, "hotloop=2", "hotexit=2", "minstitch=15")

local sum = 0
local old_metrics = misc.getmetrics()
for i = 1, 10 do
    sum = sum + foo(i)
end
local new_metrics = misc.getmetrics()
local diff = new_metrics.jit_snap_restore - old_metrics.jit_snap_restore
```
`diff` equals 3 (1 side exit on loop end, 2 side exits to the interpreter
before trace gets hot and compiled) after this chunk of code.

And now we decide to change `foo` function like this:
```lua
local function foo(i)
    -- math.fmod is not yet compiled!
    return i <= 5 and i or math.fmod(i, 11)
end
```
`diff` equals 6 (1 side exit on loop end, 2 side exits to the interpreter
before trace gets hot and compiled an 3 side exits from the root trace could
not get compiled) after the same chunk of code.

## Benchmarks

Benchmarks were taken from repo:
[LuaJIT-test-cleanup](https://github.com/LuaJIT/LuaJIT-test-cleanup).

Example of usage:
```bash
/usr/bin/time -f"array3d %U" ./luajit $BENCH_DIR/array3d.lua 300 >/dev/null
```
Taking into account the measurement error ~ 2%, it can be said that there is no
difference in the performance.

Benchmark results after and before patch (less is better):
```
   Benchmark   | AFTER (s) | BEFORE (s)
---------------+-----------+-----------
array3d        |   0.21    |   0.20
binary-trees   |   3.30    |   3.24
chameneos      |   2.86    |   2.99
coroutine-ring |   0.98    |   1.02
euler14-bit    |   1.01    |   1.05
fannkuch       |   6.74    |   6.81
fasta          |   8.25    |   8.28
life           |   0.47    |   0.46
mandelbrot     |   2.65    |   2.68
mandelbrot-bit |   1.96    |   1.97
md5            |   1.58    |   1.54
nbody          |   1.36    |   1.56
nsieve         |   2.02    |   2.06
nsieve-bit     |   1.47    |   1.50
nsieve-bit-fp  |   4.37    |   4.60
partialsums    |   0.54    |   0.55
pidigits-nogmp |   3.46    |   3.46
ray            |   1.62    |   1.63
recursive-ack  |   0.19    |   0.20
recursive-fib  |   1.63    |   1.67
scimark-fft    |   5.76    |   5.86
scimark-lu     |   3.58    |   3.64
scimark-sor    |   2.33    |   2.34
scimark-sparse |   4.88    |   4.93
series         |   0.94    |   0.94
spectral-norm  |   0.94    |   0.97
```
