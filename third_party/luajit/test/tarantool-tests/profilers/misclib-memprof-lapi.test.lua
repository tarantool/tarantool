-- XXX: This comment is a reminder to reimplement memprof tests
-- assertions to make them more independent to the changes made.
local tap = require("tap")
local test = tap.test("misclib-memprof-lapi"):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
  ["Memprof is implemented for x86_64 only"] = jit.arch ~= "x86" and
                                               jit.arch ~= "x64",
  -- See also https://github.com/LuaJIT/LuaJIT/issues/606.
  ["Disabled due to LuaJIT/LuaJIT#606"] = os.getenv("LUAJIT_TABLE_BUMP"),
  ["Memprof is disabled"] = os.getenv("LUAJIT_DISABLE_MEMPROF"),
})

test:plan(5)

local jit_opt_default = {
    3, -- level
    "hotloop=56",
    "hotexit=10",
    "minstitch=0",
}

jit.off()
jit.flush()

local table_new = require "table.new"

local bufread = require "utils.bufread"
local memprof = require "memprof.parse"
local process = require "memprof.process"
local symtab = require "utils.symtab"
local profilename = require("utils").tools.profilename

local TMP_BINFILE = profilename("memprofdata.tmp.bin")
local BAD_PATH = profilename("memprofdata/tmp.bin")
local SRC_PATH = "@"..arg[0]

local function default_payload()
  -- Preallocate table to avoid table array part reallocations.
  local _ = table_new(20, 0)

  -- Want too see 20 objects here.
  for i = 1, 20 do
    -- Try to avoid crossing with "test" module objects.
    _[i] = "memprof-str-"..i
  end

  _ = nil
  -- VMSTATE == GC, reported as INTERNAL.
  collectgarbage()
end

local function generate_output(filename, payload)
  -- Clean up all garbage to avoid pollution of free.
  collectgarbage()

  local res, err = misc.memprof.start(filename)
  -- Should start successfully.
  assert(res, err)

  payload()

  res, err = misc.memprof.stop()
  -- Should stop successfully.
  assert(res, err)
end

local function generate_parsed_output(payload)
  local res, err = pcall(generate_output, TMP_BINFILE, payload)

  -- Want to cleanup carefully if something went wrong.
  if not res then
    os.remove(TMP_BINFILE)
    error(err)
  end

  local reader = bufread.new(TMP_BINFILE)
  local symbols = symtab.parse(reader)
  local events = memprof.parse(reader, symbols)

  -- We don't need it any more.
  os.remove(TMP_BINFILE)

  return events
end

local function form_source_line(line, source)
  return ("%s:%d"):format(source or SRC_PATH, line)
end

local function form_trace_line(traceno, line, source)
  return ("TRACE [%d] started at %s:%d"):format(
    traceno,
    source or SRC_PATH,
    line
  )
end

local function fill_ev_type(events, event_type)
  local ev_type = {}
  for _, event in pairs(events[event_type]) do
    ev_type[event.loc] = event.num
  end
  return ev_type
end

local function check_alloc_report(alloc, location, nevents)
  local expected_name, event
  local traceno = location.traceno

  local source = location.source or SRC_PATH
  if traceno then
    expected_name = form_trace_line(traceno, location.line, source)
  else
    expected_name = form_source_line(location.line, source)
  end
  event = alloc[expected_name]
  assert(event, ("expected='%s', but no such event exists"):format(
    expected_name
  ))
  assert(event == nevents, ("got=%d, expected=%d"):format(
    event,
    nevents
  ))
  return true
end

-- Test profiler API.
test:test("smoke", function(subtest)
  subtest:plan(6)

  -- Not a directory.
  local res, err, errno = misc.memprof.start(BAD_PATH)
  subtest:ok(res == nil and err:match("No such file or directory"))
  subtest:ok(type(errno) == "number")

  -- Profiler is running.
  res, err = misc.memprof.start("/dev/null")
  assert(res, err)
  res, err, errno = misc.memprof.start("/dev/null")
  subtest:ok(res == nil and err:match("profiler is running already"))
  subtest:ok(type(errno) == "number")

  res, err = misc.memprof.stop()
  assert(res, err)

  -- Profiler is not running.
  res, err, errno = misc.memprof.stop()
  subtest:ok(res == nil and err:match("profiler is not running"))
  subtest:ok(type(errno) == "number")
end)

-- Test profiler output and parse.
test:test("output", function(subtest)
  subtest:plan(7)

  local events = generate_parsed_output(default_payload)

  local alloc = fill_ev_type(events, "alloc")
  local free = fill_ev_type(events, "free")

  -- Check allocation reports. The second argument is a line
  -- number of the allocation event itself. The third is a line
  -- number of the corresponding function definition. The last
  -- one is the number of allocations. 1 event - allocation of
  -- table by itself + 1 allocation of array part as far it is
  -- bigger than LJ_MAX_COLOSIZE (16).
  subtest:ok(check_alloc_report(alloc, { line = 40, linedefined = 38 }, 2))
  -- 20 strings allocations.
  subtest:ok(check_alloc_report(alloc, { line = 45, linedefined = 38 }, 20))

  -- Collect all previous allocated objects.
  subtest:ok(free.INTERNAL == 22)

  -- Tests for leak-only option.
  -- See also https://github.com/tarantool/tarantool/issues/5812.
  local heap_delta = process.form_heap_delta(events)
  local tab_alloc_stats = heap_delta[form_source_line(40)]
  local str_alloc_stats = heap_delta[form_source_line(45)]
  subtest:ok(tab_alloc_stats.nalloc == tab_alloc_stats.nfree)
  subtest:ok(tab_alloc_stats.dbytes == 0)
  subtest:ok(str_alloc_stats.nalloc == str_alloc_stats.nfree)
  subtest:ok(str_alloc_stats.dbytes == 0)
end)

-- Test for https://github.com/tarantool/tarantool/issues/5842.
test:test("stack-resize", function(subtest)
  subtest:plan(0)

  -- We are not interested in this report.
  misc.memprof.start("/dev/null")
  -- We need to cause stack resize for local variables at function
  -- call. Let's create a new coroutine (all slots are free).
  -- It has 1 slot for dummy frame + 39 free slots + 5 extra slots
  -- (so-called red zone) + 2 * LJ_FR2 slots. So 50 local
  -- variables is enough.
  local payload_str = ""
  for i = 1, 50 do
    payload_str = payload_str..("local v%d = %d\n"):format(i, i)
  end
  local f, errmsg = loadstring(payload_str)
  assert(f, errmsg)
  local co = coroutine.create(f)
  coroutine.resume(co)
  misc.memprof.stop()
end)

-- Test for extending symtab with function prototypes
-- while profiler is running.
test:test("symtab-enrich-str", function(subtest)
  subtest:plan(2)

  local payloadstr = [[
    local M = {
      tmp = string.rep("tmpstr", 100) -- line 2.
    }

    function M.payload()
      local _ = string.rep("payloadstr", 100) -- line 6.
    end

    return M
  ]]

  local events = generate_parsed_output(function()
    local strchunk = assert(load(payloadstr, "strchunk"))()
    strchunk.payload()
  end)

  local alloc = fill_ev_type(events, "alloc")

  subtest:ok(check_alloc_report(
    alloc, { source = "strchunk", line = 2, linedefined = 0 }, 1)
  )
  subtest:ok(check_alloc_report(
    alloc, { source = "strchunk", line = 6, linedefined = 5 }, 1)
  )
end)

-- Test profiler with enabled JIT.
jit.on()

test:test("jit-output", function(subtest)

  subtest:plan(4)

  jit.opt.start(3, "hotloop=10")
  jit.flush()

  -- On this run traces are generated, JIT-related allocations
  -- will be recorded as well.
  local events = generate_parsed_output(default_payload)

  local alloc = fill_ev_type(events, "alloc")

  -- Test for marking JIT-related allocations as internal.
  -- See also https://github.com/tarantool/tarantool/issues/5679.
  subtest:is(alloc[form_source_line(0)], nil)

  -- We expect, that loop will be compiled into a trace.
  -- 10 allocations in interpreter mode, 1 allocation for a trace
  -- recording and assembling and next 9 allocations will happen
  -- while running the trace.
  subtest:ok(check_alloc_report(alloc, { line = 45, linedefined = 38 }, 11))
  subtest:ok(check_alloc_report(alloc, { traceno = 1, line = 43 }, 9))
  -- See same checks with jit.off().
  subtest:ok(check_alloc_report(alloc, { line = 40, linedefined = 38 }, 2))

  -- Restore default JIT settings.
  jit.opt.start(unpack(jit_opt_default))
end)

test:done(true)
