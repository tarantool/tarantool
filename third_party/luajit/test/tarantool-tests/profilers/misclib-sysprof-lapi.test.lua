local tap = require("tap")
local test = tap.test("misclib-sysprof-lapi"):skipcond({
  ["Sysprof is implemented for x86_64 only"] = jit.arch ~= "x86" and
                                               jit.arch ~= "x64",
  ["Sysprof is implemented for Linux only"] = jit.os ~= "Linux",
  -- See also https://github.com/LuaJIT/LuaJIT/issues/606.
  ["Disabled due to LuaJIT/LuaJIT#606"] = os.getenv("LUAJIT_TABLE_BUMP"),
  ["Sysprof is disabled"] = os.getenv('LUAJIT_DISABLE_SYSPROF'),
  -- See also https://github.com/tarantool/tarantool/issues/10803.
  ["Disabled due to #10803"] = os.getenv("LUAJIT_TEST_USE_VALGRIND"),
})

test:plan(45)

jit.off()
-- XXX: Run JIT tuning functions in a safe frame to avoid errors
-- thrown when LuaJIT is compiled with JIT engine disabled.
pcall(jit.flush)

local bufread = require("utils.bufread")
local symtab = require("utils.symtab")
local sysprof = require("sysprof.parse")
local profilename = require("utils").tools.profilename
local read_file = require("utils").tools.read_file

local SYSPROF_DEFAULT_OUTPUT_FILE = "sysprof.bin"
local TMP_BINFILE = profilename("sysprofdata.tmp.bin")
local BAD_PATH = profilename("sysprofdata/tmp.bin")
local BAD_MODE = "A"
local BAD_INTERVAL = -1

local function payload()
  local function fib(n)
    if n <= 1 then
      return n
    end
    return fib(n - 1) + fib(n - 2)
  end
  return fib(32)
end

local function generate_output(opts)
  local res, err = misc.sysprof.start(opts)
  assert(res, err)

  payload()

  res,err = misc.sysprof.stop()
  assert(res, err)
end

local function check_mode(mode, interval)
  local res = pcall(
    generate_output,
    { mode = mode, interval = interval, path = TMP_BINFILE }
  )

  if not res then
    test:fail(mode .. ' mode with interval ' .. interval)
    os.remove(TMP_BINFILE)
  end

  local reader = bufread.new(TMP_BINFILE)
  local symbols = symtab.parse(reader)
  sysprof.parse(reader, symbols)
end

-- GENERAL

-- Wrong profiling mode.
local res, err, errno = misc.sysprof.start{ mode = BAD_MODE }
test:ok(res == nil, "result status with wrong profiling mode")
test:ok(err:match("profiler mode must be 'D', 'L' or 'C'"),
        "error with wrong profiling mode")
test:ok(type(errno) == "number", "errno with wrong profiling mode")

-- Missed profiling mode.
res, err, errno = misc.sysprof.start{}
test:is(res, true, "res with missed profiling mode")
test:is(err, nil, "no error with missed profiling mode")
test:is(errno, nil, "no errno with missed profiling mode")
local ok, err_msg = pcall(read_file, SYSPROF_DEFAULT_OUTPUT_FILE)
test:ok(ok == false and err_msg:match("cannot open a file"),
        "default output file is empty")
assert(misc.sysprof.stop())

-- Not a table.
res, err = pcall(misc.sysprof.start, "NOT A TABLE")
test:is(res, false, "res with not a table")
test:ok(err:match("table expected, got string"),
        "error with not a table")

-- All parameters are invalid. Check parameter validation order
-- (not strict documented behaviour).
res, err, errno = misc.sysprof.start({
  mode = BAD_MODE, path = BAD_PATH, interval = BAD_INTERVAL })
test:ok(res == nil, "res with all invalid parameters")
test:ok(err:match("profiler misuse: profiler mode must be 'D', 'L' or 'C'"),
        "error with all invalid parameters")
test:ok(type(errno) == "number", "errno with all invalid parameters")

-- All parameters are invalid, except the first one. Check
-- parameter validation order (not strict documented behaviour).
res, err, errno = misc.sysprof.start({
  mode = "C", path = BAD_PATH, interval = BAD_INTERVAL })
test:ok(res == nil, "res with all invalid parameters except the first one")
test:ok(err:match("profiler misuse: profiler interval must be greater than 1"),
        "error with all invalid parameters except the first one")
test:ok(type(errno) == "number",
        "errno with all invalid parameters except the first one")

-- Already running.
res, err = misc.sysprof.start{ mode = "D" }
assert(res, err)

res, err, errno = misc.sysprof.start{ mode = "D" }
test:ok(res == nil and err:match("profiler is running already"),
        "ok with already running")
test:ok(type(errno) == "number", "errno with already running")

res, err = misc.sysprof.stop()
assert(res, err)

-- Not running.
res, err, errno = misc.sysprof.stop()
test:is(res, nil, "result status with not running")
test:ok(err:match("profiler is not running"), "error with not running")
test:ok(type(errno) == "number", "errno with not running")

-- Bad path.
res, err, errno = misc.sysprof.start({ mode = "C", path = BAD_PATH })
test:ok(res == nil, "result status with bad path")
local error_msg = ("%s: No such file or directory"):format(BAD_PATH)
test:ok(err == error_msg, "error with bad path")
test:ok(type(errno) == "number", "errno with bad path")

-- Path is not a string.
res, err, errno = misc.sysprof.start({ mode = 'C', path = 190 })
test:ok(res == nil, "result status with path is not a string")
test:ok(err:match("profiler path should be a string"),
        "err with path is not a string")
test:ok(type(errno) == "number", "errno with path is not a string")

-- Bad interval.
res, err, errno = misc.sysprof.start{ mode = "C", interval = BAD_INTERVAL }
test:is(res, nil, "result status and error with bad interval")
test:ok(err:match("profiler interval must be greater than 1"),
        "error with bad interval")
test:ok(type(errno) == "number", "errno with bad interval")

-- Bad interval (0).
res, err, errno = misc.sysprof.start{ mode = "C", interval = 0 }
test:ok(res == nil, "res with bad interval 0")
test:ok(err:match("profiler interval must be greater than 1"),
        "error with bad interval 0")
test:ok(type(errno) == "number", "errno with bad interval 0")

-- Good interval (1).
res, err, errno = misc.sysprof.start{
    mode = "C",
    interval = 1,
    path = "/dev/null",
}
test:is(res, true, "res with good interval 1")
test:is(err, nil, "no error with good interval 1")
test:is(errno, nil, "no errno with good interval 1")
misc.sysprof.stop()

-- Intermediate sysprof.report().
assert(misc.sysprof.start({
    mode = "D",
    interval = 1,
    path = "/dev/null",
}))

payload()

local report = misc.sysprof.report()
-- Check the profile is not empty.
test:ok(report.samples > 0,
        "number of samples is greater than 0 for the default payload")
assert(misc.sysprof.stop())

-- DEFAULT MODE

if not pcall(generate_output, { mode = "D", interval = 100 }) then
  test:fail('`default` mode with interval 100')
end

report = misc.sysprof.report()

-- Check the profile is not empty.
test:ok(report.samples > 0,
        "number of samples is greater than 0 for the default payload")
-- There is a Lua function with FNEW bytecode in it. Hence there
-- are only three possible sample types:
-- * LFUNC -- Lua payload is sampled.
-- * GC -- Lua GC machinery triggered in scope of FNEW bytecode
--   is sampled.
-- * INTERP -- VM is in a specific state when the sample is taken.
test:ok(report.vmstate.LFUNC + report.vmstate.GC + report.vmstate.INTERP > 0,
        "total number of LFUNC, GC and INTERP samples is greater than 0 " ..
        "for the default payload")
-- There is no fast functions and C function in default payload.
test:ok(report.vmstate.FFUNC + report.vmstate.CFUNC == 0,
        "total number of FFUNC and CFUNC samples is equal to 0 " ..
        "for the default payload")
-- Check all JIT-related VM states are not sampled.
local msg = "total number of VM state %s is equal to 0 for the default payload"
for _, vmstate in pairs({ 'TRACE', 'RECORD', 'OPT', 'ASM', 'EXIT' }) do
  test:ok(report.vmstate[vmstate] == 0, msg:format(vmstate))
end

-- With very big interval.
if not pcall(generate_output, { mode = "D", interval = 1000 }) then
  test:fail('`default` mode with interval 1000')
end

report = misc.sysprof.report()
test:ok(report.samples == 0, "total number of samples is equal to 0 for " ..
        "the too big sampling interval")

-- LEAF MODE
check_mode("L", 100)

-- CALL MODE
check_mode("C", 100)

os.remove(TMP_BINFILE)

test:done(true)
