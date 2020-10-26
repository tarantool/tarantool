# LuaJIT memory profiler

* **Status**: In progress
* **Start date**: 24-10-2020
* **Authors**: Sergey Kaplun @Buristan skaplun@tarantool.org,
               Igor Munkin @igormunkin imun@tarantool.org,
               Sergey Ostanevich @sergos sergos@tarantool.org
* **Issues**: [#5442](https://github.com/tarantool/tarantool/issues/5442), [#5490](https://github.com/tarantool/tarantool/issues/5490)

## Summary

LuaJIT memory profiler is a toolchain for analysis of memory usage by user's
application.

## Background and motivation

Garbage collector (GC) is a curse of performance for most of Lua applications.
Memory usage of Lua application should be profiled to locate various
memory-unoptimized code blocks. If the application has memory leaks they can be
found with the profiler also.

## Detailed design

The whole toolchain of memory profiling will be divided into several parts:
1) [Prerequisites](#prerequisites).
2) [Recording information about memory usage and saving it](#information-recording).
3) [Reading saved data and display it in human-readable format](#reading-and-displaying-saved-data).

### Prerequisites

This section describes additional changes in LuaJIT required for the feature
implementation. This version of LuaJIT memory profiler does not support verbose
reporting for allocations made on traces. All allocation from traces are
reported as internal. But trace code semantics should be totally the same as
for the Lua interpreter (excluding sink optimizations). Also all deallocations
are reported as internal too.

There are two different representations of functions in LuaJIT: the function's
prototype (`GCproto`) and the function object so called closure (`GCfunc`).
The closures are represented as `GCfuncL` and `GCfuncC` for Lua and C closures
respectively. Besides LuaJIT has a special function type, a.k.a. Fast Function
that is used for LuaJIT built-ins

Tail call optimization does not create a new call frame, so all allocations
inside the function called via `CALLT`/`CALLMT` are attributed to its caller.

As for allocations made inside the built-ins user can do nothing but reduce use
of these built-ins. So if fast function is called from a Lua function all
allocations made in its scope are attributed to this Lua function (i.e. the
built-in caller). Otherwise, this event is attributed to a C function.

Assume we have the following Lua chunk named <test.lua>:

```lua
1  jit.off()
2  misc.memprof.start("memprof_new.bin")
3  -- Lua does not create a new frame to call string.rep() and all allocations
4  -- are attributed not to append() function but to the parent scope.
5  local function append(str, rep)
6    return string.rep(str, rep)
7  end
8
9  local t = {}
10 for _ = 1, 1e5 do
11   -- table.insert() is a built-in and all corresponding allocations
12   -- are reported in the scope of main chunk
13   table.insert(t,
14     append('q', _)
15   )
16 end
17 misc.memprof.stop()
```

If one runs the chunk above the profiler reports approximately the following
(see legend [here](#reading-and-displaying-saved-data)):
```
ALLOCATIONS
@test.lua:0, line 14: 1002      531818  0
@test.lua:0, line 13: 1 24      0
@test.lua:0, line 9: 1  32      0
@test.lua:0, line 7: 1  20      0

REALLOCATIONS
@test.lua:0, line 13: 9 16424   8248
        Overrides:
                @test.lua:0, line 13

@test.lua:0, line 14: 5 1984    992
        Overrides:
                @test.lua:0, line 14


DEALLOCATIONS
INTERNAL: 20    0       1481
@test.lua:0, line 14: 3 0       7168
        Overrides:
                @test.lua:0, line 14
```

So we need to know a type of function being executed by the virtual machine
(VM). Originally VM state identifies C function execution only, so Fast and Lua
functions states are added.

To determine currently allocating coroutine (that may not be equal to currently
executed one) a new field called `mem_L` is added to `global_State` structure
to keep the coroutine address. This field is set on each reallocation to the
corresponding `L` with which it is called.

There is a static function (`lj_debug_getframeline`) that returns line number
for current `BCPos` in `lj_debug.c` already. It is added to the debug
module API to be used in memory profiler.

### Information recording

Each allocate/reallocate/free is considered as a type of event that are
reported. Event stream has the following format:

```c
/*
** Event stream format:
**
** stream         := symtab memprof
** symtab         := see symtab description
** memprof        := prologue event* epilogue
** prologue       := 'l' 'j' 'm' version reserved
** version        := <BYTE>
** reserved       := <BYTE> <BYTE> <BYTE>
** event          := event-alloc | event-realloc | event-free
** event-alloc    := event-header loc? naddr nsize
** event-realloc  := event-header loc? oaddr osize naddr nsize
** event-free     := event-header loc? oaddr osize
** event-header   := <BYTE>
** loc            := loc-lua | loc-c
** loc-lua        := sym-addr line-no
** loc-c          := sym-addr
** sym-addr       := <ULEB128>
** line-no        := <ULEB128>
** oaddr          := <ULEB128>
** naddr          := <ULEB128>
** osize          := <ULEB128>
** nsize          := <ULEB128>
** epilogue       := event-header
**
** <BYTE>   :  A single byte (no surprises here)
** <ULEB128>:  Unsigned integer represented in ULEB128 encoding
**
** (Order of bits below is hi -> lo)
**
** version: [VVVVVVVV]
**  * VVVVVVVV: Byte interpreted as a plain integer version number
**
** event-header: [FUUUSSEE]
**  * EE   : 2 bits for representing allocation event type (AEVENT_*)
**  * SS   : 2 bits for representing allocation source type (ASOURCE_*)
**  * UUU  : 3 unused bits
**  * F    : 0 for regular events, 1 for epilogue's *F*inal header
**           (if F is set to 1, all other bits are currently ignored)
*/
```

It is enough to know the address of LUA/C function to determine it. Symbolic
table (symtab) is dumped at the start of profiling to avoid dumping function
location on each memory event for saving both CPU usage and binary profile
size.

Each line contains the address, Lua chunk definition as the filename and line
number of the function's declaration. This table of symbols has the following
format described at <lj_memprof.h>:

```c
/*
** symtab format:
**
** symtab         := prologue sym*
** prologue       := 'l' 'j' 's' version reserved
** version        := <BYTE>
** reserved       := <BYTE> <BYTE> <BYTE>
** sym            := sym-lua | sym-final
** sym-lua        := sym-header sym-addr sym-chunk sym-line
** sym-header     := <BYTE>
** sym-addr       := <ULEB128>
** sym-chunk      := string
** sym-line       := <ULEB128>
** sym-final      := sym-header
** string         := string-len string-payload
** string-len     := <ULEB128>
** string-payload := <BYTE> {string-len}
**
** <BYTE>   :  A single byte (no surprises here)
** <ULEB128>:  Unsigned integer represented in ULEB128 encoding
**
** (Order of bits below is hi -> lo)
**
** version: [VVVVVVVV]
**  * VVVVVVVV: Byte interpreted as a plain numeric version number
**
** sym-header: [FUUUUUTT]
**  * TT    : 2 bits for representing symbol type
**  * UUUUU : 5 unused bits
**  * F     : 1 bit marking the end of the symtab (final symbol)
*/
```

So when memory profiling starts the current allocation function is replaced by
the new allocation function additionally wrapped to write the profiling events.
When profiler stops the previous allocation function is restored.

Starting profiler from Lua is quite simple:
```lua
local started, err, errno = misc.memprof.start(fname)
```
where `fname` is name of the file where profile events are written. Writer for
this function perform `fwrite()` for each call retrying in case of `EINTR`.
When the profiling is stopped `fclose()` is called. The profiler's function's
contract is similar to standard `io.*` interfaces. If it is impossible to open
a file for writing or profiler fails to start, `nil` is returned on failure
(plus an error message as a second result and a system-dependent error code as
a third result). Otherwise, returns `true` value.

Stopping profiler from Lua is simple too:
```lua
local stopped, err, errno = misc.memprof.stop()
```

If there is any error occurred at profiling stopping (an error when file
descriptor was closed) `memprof.stop()` returns `nil` (plus an error message as
a second result and a system-dependent error code as a third result). Returns
`true` otherwise.

If you want to build LuaJIT without memory profiler, you should build it with
`-DLUAJIT_DISABLE_MEMPROF`. If it is disabled `misc.memprof.start()` and
`misc.memprof.stop()` always return `false`.

### Reading and displaying saved data

Binary data can be read by `luajit-parse-memprof` utility. It parses the binary
format provided by memory profiler and render it to human-readable format.

The usage for LuaJIT itself is very simple:
```
$ ./luajit-parse-memprof --help
luajit-parse-memprof - parser of the memory usage profile collected
                       with LuaJIT's memprof.

SYNOPSIS

luajit-parse-memprof [options] memprof.bin

Supported options are:

  --help                            Show this help and exit
```

Plain text of profiled info has the following format:
```
@<filename>:<function_line>, line <line where event was detected>: <number of events>	<allocated>	<freed>
```
See the example [above](#prerequisites).

`INTERNAL` means that these allocations are caused by internal LuaJIT
structures. Note that events are sorted from the most often to the least.

`Overrides` means what allocation this reallocation overrides.

If you want to parse binary data via Tarantool only, use the following
command (dash is important):
```bash
$ tarantool -e 'require("memprof")(arg[1])' - memprof.bin
```

## Benchmarks

Benchmarks were taken from repo:
[LuaJIT-test-cleanup](https://github.com/LuaJIT/LuaJIT-test-cleanup).

Example of measuring:
```bash
/usr/bin/time -f"array3d %U" ./luajit $BENCH_DIR/array3d.lua 300 >/dev/null
```

This table shows performance deviation in relation to REFerence value (before
commit) with stopped and running profiler. The table shows the average value
for 11 runs. The first field of the column indicates the change in the average
time in seconds (less is better). The second field is the standard deviation
for the found difference.

```
     Name       | REF  | AFTER, memprof off | AFTER, memprof on
----------------+------+--------------------+------------------
array3d         | 0.21 |    +0.00 (0.01)    |    +0.00 (0.01)
binary-trees    | 3.25 |    -0.01 (0.06)    |    +0.53 (0.10)
chameneos       | 2.97 |    +0.14 (0.04)    |    +0.13 (0.06)
coroutine-ring  | 1.00 |    +0.01 (0.04)    |    +0.01 (0.04)
euler14-bit     | 1.03 |    +0.01 (0.02)    |    +0.00 (0.02)
fannkuch        | 6.81 |    -0.21 (0.06)    |    -0.20 (0.06)
fasta           | 8.20 |    -0.07 (0.05)    |    -0.08 (0.03)
life            | 0.46 |    +0.00 (0.01)    |    +0.35 (0.01)
mandelbrot      | 2.65 |    +0.00 (0.01)    |    +0.01 (0.01)
mandelbrot-bit  | 1.97 |    +0.00 (0.01)    |    +0.01 (0.02)
md5             | 1.58 |    -0.01 (0.04)    |    -0.04 (0.04)
nbody           | 1.34 |    +0.00 (0.01)    |    -0.02 (0.01)
nsieve          | 2.07 |    -0.03 (0.03)    |    -0.01 (0.04)
nsieve-bit      | 1.50 |    -0.02 (0.04)    |    +0.00 (0.04)
nsieve-bit-fp   | 4.44 |    -0.03 (0.07)    |    -0.01 (0.07)
partialsums     | 0.54 |    +0.00 (0.01)    |    +0.00 (0.01)
pidigits-nogmp  | 3.47 |    -0.01 (0.02)    |    -0.10 (0.02)
ray             | 1.62 |    -0.02 (0.03)    |    +0.00 (0.02)
recursive-ack   | 0.20 |    +0.00 (0.01)    |    +0.00 (0.01)
recursive-fib   | 1.63 |    +0.00 (0.01)    |    +0.01 (0.02)
scimark-fft     | 5.72 |    +0.06 (0.09)    |    -0.01 (0.10)
scimark-lu      | 3.47 |    +0.02 (0.27)    |    -0.03 (0.26)
scimark-sor     | 2.34 |    +0.00 (0.01)    |    -0.01 (0.01)
scimark-sparse  | 4.95 |    -0.02 (0.04)    |    -0.02 (0.04)
series          | 0.95 |    +0.00 (0.02)    |    +0.00 (0.01)
spectral-norm   | 0.96 |    +0.00 (0.02)    |    -0.01 (0.02)
```
