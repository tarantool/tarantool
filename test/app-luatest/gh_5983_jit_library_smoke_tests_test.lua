local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun').tarantool

local g = t.group()

local script = string.dump(function()
    -- Use *all* jit.dump options, so the test can check them all.
    require('jit.dump').start('+tbisrmXaT')
    -- Tune JIT engine to make the test faster and more robust.
    jit.opt.start('hotloop=1')
    -- Purge all existing traces generated on Tarantool startup.
    jit.flush()
    -- Record primitive loop.
    for _ = 1, 3 do end
end)

local rundir
g.before_each(function()
    t.skip_if(jit.os == 'BSD', 'Disabled on *BSD due to #4819')

    treegen.init(g)
    treegen.add_template(g, '^script%.lua$', script)
    rundir = treegen.prepare_directory(g, {'script.lua'})
end)

g.test_jit_dump = function()
    local result = justrun(rundir, {},  {'script.lua'}, {nojson = true})
    local needle = table.concat({
        '---- TRACE 1 start',
        '---- TRACE 1 IR',
        '---- TRACE 1 mcode',
        '---- TRACE 1 stop',
        '---- TRACE 1 exit',
    }, '.+')
    t.assert_str_contains(result.stdout, needle, true, 'jit.dump smoke tests')
end
