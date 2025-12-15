local clock = require('clock')
local ffi = require('ffi')
-- Require 'cjson' only on demand for formatted output to file.
local json

local M = {}

local type, assert, error = type, assert, error
local format, rep = string.format, string.rep
local floor, max, min = math.floor, math.max, math.min
local table_remove = table.remove

local LJ_HASJIT = jit and jit.opt

-- Argument parsing. ---------------------------------------------

-- XXX: Make options compatible with Google Benchmark, since most
-- probably it will be used for the C benchmarks as well.
-- Compatibility isn't full: there is no support for environment
-- variables (since they are not so useful) and the output to the
-- terminal is suppressed if the --benchmark_out flag is
-- specified.

local HELP_MSG = [[
 Options:
   -j{off|on}                 Disable/Enable JIT for the benchmarks.
   --benchmark_color={true|false|auto}
                              Enables the colorized output for the terminal (not
                              the file). 'auto' means to use colors if the
                              output is being sent to a terminal and the TERM
                              environment variable is set to a terminal type
                              that supports colors. Default is 'auto'.
   --benchmark_min_time={number}
                              Minimum seconds to run the benchmark tests.
                              4.0 by default.
   --benchmark_out=<file>     Places the output into <file>.
   --benchmark_out_format={console|json}
                              The format is used when saving the results in the
                              file. The default format is the JSON format.
   -h, --help                 Display this message and exit.

 There are a bunch of suggestions on how to achieve the most
 stable benchmark results:
 https://github.com/tarantool/tarantool/wiki/Benchmarking
]]

local EXIT_FAILURE = 1

local function usage(ctx)
  local header = format('USAGE: luajit %s [options]\n', ctx.name)
  io.stderr:write(header, HELP_MSG)
  os.exit(EXIT_FAILURE)
end

local function check_param(check, strfmt, ...)
  if not check then
    io.stderr:write(format(strfmt, ...))
    os.exit(EXIT_FAILURE)
  end
end

-- Valid values: 'false'/'no'/'0'.
-- In case of an invalid value the 'auto' is used.
local function set_color(ctx, value)
  if value == 'false' or value == 'no' or value == '0' then
    ctx.color = false
  else
    -- In case of an invalid value, the Google Benchmark uses
    -- 'auto', which is true for the stdout output (the only
    -- colorizable output). So just set it to true by default.
    ctx.color = true
  end
end

local DEFAULT_MIN_TIME = 4.0
local function set_min_time(ctx, value)
  local time = tonumber(value)
  check_param(time, 'Invalid min time: "%s"\n', value)
  ctx.min_time = time
end

local function set_output(ctx, filename)
  check_param(type(filename) == "string", 'Invalid output value: "%s"\n',
              filename)
  ctx.output = filename
end

-- Determine the output format for the benchmark.
-- Supports only 'console' and 'json' for now.
local function set_output_format(ctx, value)
  local output_format = tostring(value)
  check_param(output_format, 'Invalid output format: "%s"\n', value)
  output_format = output_format:lower()
  check_param(output_format == 'json' or output_format == 'console',
              'Unsupported output format: "%s"\n', output_format)
  ctx.output_format = output_format
end

local function set_jit(ctx, value)
  check_param(value == 'on' or value == 'off',
             'Invalid jit value: "%s"\n', value)
  if value == 'off' then
    ctx.jit = false
  elseif value == 'on' then
    ctx.jit = true
  end
end

local function unrecognized_option(optname, dashes)
  local fullname = dashes .. (optname or '=')
  io.stderr:write(format('unrecognized command-line flag: %s\n', fullname))
  io.stderr:write(HELP_MSG)
  os.exit(EXIT_FAILURE)
end

local function unrecognized_long_option(_, optname)
  unrecognized_option(optname, '--')
end

local function unrecognized_short_option(_, optname)
  unrecognized_option(optname, '-')
end

local SHORT_OPTS = setmetatable({
  ['h'] = usage,
  ['j'] = set_jit,
}, {__index = unrecognized_short_option})

local LONG_OPTS = setmetatable({
  ['benchmark_color'] = set_color,
  ['benchmark_min_time'] = set_min_time,
  ['benchmark_out'] = set_output,
  -- XXX: For now support only JSON encoded and raw output.
  ['benchmark_out_format'] = set_output_format,
  ['help'] = usage,
}, {__index = unrecognized_long_option})

local function is_option(str)
  return type(str) == 'string' and str:sub(1, 1) == '-' and str ~= '-'
end

local function next_arg_value(arg, n)
  local opt_value = nil
  if arg[n] and not is_option(arg[n]) then
    opt_value = arg[n]
    table_remove(arg, n)
  end
  return opt_value
end

local function parse_long_option(arg, a, n)
  local opt_name, opt_value
  -- Remove dashes.
  local opt = a:sub(3)
  -- --option=value
  if opt:find('=', 1, true) then
    -- May match empty option name and/or value.
    opt_name, opt_value = opt:match('^([^=]+)=(.*)$')
  else
    -- --option value
    opt_name = opt
    opt_value = next_arg_value(arg, n)
  end
  return opt_name, opt_value
end

local function parse_short_option(arg, a, n)
  local opt_name, opt_value
  -- Remove the dash.
  local opt = a:sub(2)
  if #opt == 1 then
    -- -o value
    opt_name = opt
    opt_value = next_arg_value(arg, n)
  else
    -- -ovalue.
    opt_name = opt:sub(1, 1)
    opt_value = opt:sub(2)
  end
  return opt_name, opt_value
end

local function parse_opt(ctx, arg, a, n)
  if a:sub(1, 2) == '--' then
    local opt_name, opt_value = parse_long_option(arg, a, n)
    LONG_OPTS[opt_name](ctx, opt_value)
  else
    local opt_name, opt_value = parse_short_option(arg, a, n)
    SHORT_OPTS[opt_name](ctx, opt_value)
  end
end

-- Process the options and update the benchmark context.
local function argparse(arg, name)
  local ctx = {name = name}
  local n = 1
  while n <= #arg do
    local a = arg[n]
    if is_option(a) then
      table_remove(arg, n)
      parse_opt(ctx, arg, a, n)
    else
      -- Just ignore it.
      n = n + 1
    end
  end
  return ctx
end

-- Formatting. ---------------------------------------------------

local function format_console_header()
  -- Use a similar format to the Google Benchmark, except for the
  -- fixed benchmark name length.
  local header = format('%-37s %12s %15s %13s %-28s\n',
    'Benchmark', 'Time', 'CPU', 'Iterations', 'UserCounters...'
  )
  local border = rep('-', #header - 1) .. '\n'
  return border .. header .. border
end

local COLORS = {
  GREEN = '\027[32m%s\027[m',
  YELLOW = '\027[33m%s\027[m',
  CYAN = '\027[36m%s\027[m',
}

local function format_name(ctx, name)
  name = format('%-37s ', name)
  if ctx.color then
     name = format(COLORS.GREEN, name)
  end
  return name
end

local function format_time(ctx, real_time, cpu_time, time_unit)
  local timestr = format('%10.2f %-4s %10.2f %-4s ', real_time, time_unit,
                         cpu_time, time_unit)
  if ctx.color then
     timestr = format(COLORS.YELLOW, timestr)
  end
  return timestr
end

local function format_iterations(ctx, iterations)
  iterations = format('%10d ', iterations)
  if ctx.color then
     iterations = format(COLORS.CYAN, iterations)
  end
  return iterations
end

local function format_ips(ips)
  local ips_str
  if ips / 1e6 > 1 then
    ips_str = format('items_per_second=%.3fM/s', ips / 1e6)
  elseif ips / 1e3 > 1 then
    ips_str = format('items_per_second=%.3fk/s', ips / 1e3)
  else
    ips_str = format('items_per_second=%d/s', ips)
  end
  return ips_str
end

local function format_result_console(ctx, r)
  return format('%s%s%s%s\n',
    format_name(ctx, r.name),
    format_time(ctx, r.real_time, r.cpu_time, r.time_unit),
    format_iterations(ctx, r.iterations),
    format_ips(r.items_per_second)
  )
end

local function format_results(ctx)
  local output_format = ctx.output_format
  local res = ''
  if output_format == 'json' then
    res = json.encode({
      benchmarks = ctx.results,
      context = ctx.context,
    })
  else
    assert(output_format == 'console', 'Unknown format: ' .. output_format)
    res = res .. format_console_header()
    for _, r in ipairs(ctx.results) do
      res = res .. format_result_console(ctx, r)
    end
  end
  return res
end

local function report_results(ctx)
  ctx.fh:write(format_results(ctx))
end

-- Tests setup and run. ------------------------------------------

local function term_is_color()
  local term = os.getenv('TERM')
  return (term and term:match('color') or os.getenv('COLORTERM'))
end

local function benchmark_context(ctx)
  return {
    arch = jit.arch,
    -- Google Benchmark reports a date in ISO 8061 format.
    date = os.date('%Y-%m-%dT%H:%M:%S%z'),
    gc64 = ffi.abi('gc64'),
    host_name = io.popen('hostname'):read(),
    jit = ctx.jit,
  }
end

local function init(ctx)
  -- Array of benches to proceed with.
  ctx.benches = {}
  -- Array of the corresponding results.
  ctx.results = {}

  if ctx.jit == nil then
    if LJ_HASJIT then
      ctx.jit = jit.status()
    else
      ctx.jit = false
    end
  end
  ctx.color = ctx.color == nil and true or ctx.color
  if ctx.output then
    -- Don't bother with manual file closing. It will be closed
    -- automatically when the corresponding object is
    -- garbage-collected.
    ctx.fh = assert(io.open(ctx.output, 'w+'))
    ctx.output_format = ctx.output_format or 'json'
    -- Always without color.
    ctx.color = false
  else
    ctx.fh = io.stdout
    -- Always console outptut to the terminal.
    ctx.output_format = 'console'
    if ctx.color and term_is_color() then
      ctx.color = true
    else
      ctx.color = false
    end
  end
  ctx.min_time = ctx.min_time or DEFAULT_MIN_TIME

  if ctx.output_format == 'json' then
    json = require('cjson')
  end

  -- Google Benchmark's context, plus benchmark info.
  ctx.context = benchmark_context(ctx)

  return ctx
end

local function test_name()
  return debug.getinfo(3, 'S').short_src:match('([^/\\]+)$')
end

local function add_bench(ctx, bench)
  if bench.checker == nil and not bench.skip_check then
    error('Bench requires a checker to proof the results', 2)
  end
  table.insert(ctx.benches, bench)
end

local MAX_ITERATIONS = 1e9
-- Determine the number of iterations for the next benchmark run.
local function iterations_multiplier(min_time, get_time, iterations)
  -- When the last run is at least 10% of the required time, the
  -- maximum expansion should be 14x.
  local multiplier = min_time * 1.4 / max(get_time, 1e-9)
  local is_significant = get_time / min_time > 0.1
  multiplier = is_significant and multiplier or 10
  local new_iterations = max(floor(multiplier * iterations), iterations + 1)
  return min(new_iterations, MAX_ITERATIONS)
end

-- https://luajit.org/running.html#foot.
local JIT_DEFAULTS = {
  maxtrace = 1000,
  maxrecord = 4000,
  maxirconst = 500,
  maxside = 100,
  maxsnap = 500,
  hotloop = 56,
  hotexit = 10,
  tryside = 4,
  instunroll = 4,
  loopunroll = 15,
  callunroll = 3,
  recunroll = 2,
  sizemcode = 32,
  maxmcode = 512,
}

-- Basic setup for all tests to clean up after a previous
-- executor.
local function luajit_tests_setup(ctx)
  -- Reset the JIT to the defaults.
  if ctx.jit == false then
    jit.off()
  elseif LJ_HASJIT then
    jit.on()
    jit.flush()
    jit.opt.start(3)
    for k, v in pairs(JIT_DEFAULTS) do
      jit.opt.start(k .. '=' .. v)
    end
  end

  -- Reset the GC to the defaults.
  collectgarbage('setstepmul', 200)
  collectgarbage('setpause', 200)

  -- Collect all garbage at the end. Twice to be sure that all
  -- finalizers are run.
  collectgarbage()
  collectgarbage()
end

local function run_benches(ctx)
  -- Process the tests in the predefined order with ipairs.
  for _, bench in ipairs(ctx.benches) do
    luajit_tests_setup(ctx)
    if bench.setup then bench.setup() end

    -- The first run is used as a warm-up, plus results checks.
    local payload = bench.payload
    -- Generally you should never skip any checks. But sometimes
    -- a bench may generate so much output in one run that it is
    -- overkill to save the result in the file and test it.
    -- So to avoid double time for the test run, just skip the
    -- check.
    if not bench.skip_check then
      local result = payload()
      assert(bench.checker(result))
    end
    local N
    local delta_real, delta_cpu
    -- Iterations are specified manually.
    if bench.iterations then
      N = bench.iterations

      local start_real = clock.realtime()
      local start_cpu  = clock.process_cputime()
      for _ = 1, N do
        payload()
      end
      delta_real = clock.realtime() - start_real
      delta_cpu  = clock.process_cputime() - start_cpu
    else
      -- Iterations are determined dynamycally, adjusting to fit
      -- the minimum time to run for the benchmark.
      local min_time = bench.min_time or ctx.min_time
      local next_iterations = 1
      repeat
        N = next_iterations
        local start_real = clock.realtime()
        local start_cpu  = clock.process_cputime()
        for _ = 1, N do
          payload()
        end
        delta_real = clock.realtime() - start_real
        delta_cpu  = clock.process_cputime() - start_cpu
        next_iterations = iterations_multiplier(min_time, delta_real, N)
      until delta_real > min_time or N == next_iterations
    end

    if bench.teardown then bench.teardown() end

    local items = N * bench.items
    local items_per_second = math.floor(items / delta_real)
    table.insert(ctx.results, {
      cpu_time = delta_cpu,
      real_time = delta_real,
      items_per_second = items_per_second,
      iterations = N,
      name = bench.name,
      time_unit = 's',
      -- Fields below are used only for the Google Benchmark
      -- compatibility. We don't use them really.
      run_name = bench.name,
      run_type = 'iteration',
      repetitions = 1,
      repetition_index = 1,
      threads = 1,
    })
  end
end

local function run_and_report(ctx)
  run_benches(ctx)
  report_results(ctx)
end

function M.new(arg)
  assert(type(arg) == 'table', 'given argument should be a table')
  local name = test_name()
  local ctx = init(argparse(arg, name))
  return setmetatable(ctx, {__index = {
    add = add_bench,
    run = run_benches,
    report = report_results,
    run_and_report = run_and_report,
  }})
end

return M
