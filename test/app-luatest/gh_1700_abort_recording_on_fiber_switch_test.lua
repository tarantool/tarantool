local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun').tarantool
local pathjoin = require('fio').pathjoin

local g = t.group()

local script = string.dump(function()
    local hotloop = assert(arg[1], 'hotloop argument is missing')
    local trigger = assert(arg[2], 'trigger argument is missing')

    local ffi = require('ffi')
    local ffiyield = ffi.load('libyield')
    ffi.cdef('void yield(struct yield *state, int i)')

    -- Set the value to trigger <yield> call switch the running fiber.
    local yield = require('libyield')(trigger)

    -- Depending on trigger and hotloop values the following contexts
    -- are possible:
    -- * if trigger <= hotloop -> trace recording is aborted
    -- * if trigger >  hotloop -> trace is recorded but execution
    --   leads to panic
    jit.opt.start(string.format('hotloop=%d', hotloop))

    for i = 0, trigger + hotloop do
      ffiyield.yield(yield, i)
    end

    -- XXX: Disable buffering for io.stdout, so the result of any
    -- output operation appears immediately in the corresponding
    -- popen pipe.
    io.stdout:setvbuf('no')
    -- Panic didn't occur earlier.
    io.write('#1700 still works')
end)

local rundir
local libext = package.cpath:match('?.(%a+);')
local libpath = pathjoin(os.getenv('BUILDDIR'), 'test', 'app-luatest', 'lib')

-- Add DYLD_LIBRARY_PATH/LD_LIBRARY_PATH and LUA_CPATH environment
-- variables to load the required shared library extension from
-- non-standart paths.
local env = {
    DYLD_LIBRARY_PATH = libpath,
    LD_LIBRARY_PATH = libpath,
    LUA_CPATH = ('%s/?.%s;%s;'):format(libpath, libext, os.getenv('LUA_CPATH')),
}

g.before_all(function(g)
    t.skip_if(jit.os == 'BSD', 'Disabled on *BSD due to #4819')

    treegen.init(g)
    treegen.add_template(g, '^script%.lua$', script)
    rundir = treegen.prepare_directory(g, {'script.lua'})
end)

g.after_all(function(g)
    treegen.clean(g)
end)

local function runcmd(...)
    local argv = {'script.lua', ...}
    local result = justrun(rundir, env, argv, {nojson = true, stderr = true})
    return result.stdout, result.stderr
end

g.test_abort_recording_on_fiber_switch = function()
    local stdout, stderr = runcmd('1', '1')
    t.assert_equals(stdout, '#1700 still works', 'Trace is aborted')
    t.assert_equals(stderr, '', 'No panic occurred')
end

g.test_panic_on_trace = function()
    local reason = 'fiber %d+ is switched while running the compiled code %b()'
    local stdout, stderr = runcmd('1', '2')
    t.assert_equals(stdout, nil, 'Trace is compiled')
    t.assert_str_contains(stderr, reason, true, 'Panic occurred')
end
