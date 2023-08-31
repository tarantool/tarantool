local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun').tarantool

local g = t.group()

local misuse = {
    output = table.concat({
        'Invalid usage: please either provide a Lua script name',
        'or specify an instance name to be started',
        'or set -i CLI flag to spawn Lua REPL or run a',
        'failover coordinator using --failover CLI option.'
    }, '\n'),
    pattern = false,
}
local greeting = {
    output = "Tarantool [^\n]*\ntype 'help' for interactive help",
    pattern = true,
}
local prompt = {
    output = 'tarantool>',
    pattern = false,
}

local script = string.dump(function() print(_VERSION) end)

local rundir
g.before_all(function()
    treegen.init(g)
    treegen.add_template(g, '^script%.lua$', script)
    rundir = treegen.prepare_directory(g, {'script.lua'})
end)

g.after_all(function()
    treegen.clean(g)
end)

local function simplerun(args)
    return justrun(rundir, {}, args, {nojson = true, stderr = true})
end

g.test_tarantool_no_arg_run = function()
    local result = simplerun({})
    t.assert_equals(result.exit_code, 1,
                    'Running Tarantool w/o CLI arguments fails')
    t.assert_str_contains(result.stderr, misuse.output, misuse.pattern,
                          'Running Tarantool w/o CLI arguments yields usage')
end

g.test_tarantool_stdin_invalid_redirect = function()
    local result = simplerun({'</dev/null'})
    t.assert_equals(result.exit_code, 1,
                    'Redirect from /dev/null w/o dash in CLI args')
    t.assert_str_contains(result.stderr, misuse.output, misuse.pattern,
                          'Invalid redirect to /dev/null yields usage')
end

g.test_tarantool_stdin_valid_redirect = function()
    local result = simplerun({'-', '</dev/null'})
    t.assert_equals(result.exit_code, 0,
                    'Redirect from /dev/null with dash in CLI args is fine')
    t.assert_equals(result.stdout, '', 'stdout is empty')
    t.assert_equals(result.stderr, '', 'stderr is empty')
end

g.test_tarantool_execute = function()
    local result = simplerun({'-e "print(42)"'})
    t.assert_equals(result.exit_code, 0, 'Execute Lua one-liner is fine')
    t.assert_equals(result.stdout, '42', 'Lua one-liner output is fine')
    t.assert_equals(result.stderr, '', 'Lua one-liner spawns no errors')
end

g.test_tarantool_execute_with_script = function()
    local result = simplerun({'-e "print(42)"', 'script.lua'})
    t.assert_equals(result.exit_code, 0, 'Lua one-liner with script is fine')
    t.assert_equals(result.stdout, table.concat({'42', _VERSION}, '\n'),
                    'Lua one-liner output and script output is fine')
    t.assert_equals(result.stderr, '',
                    'Lua one-liner with script spawns no errors')
end

g.test_tarantool_interactive = function()
    local result = simplerun({'-i', '</dev/null'})
    t.assert_equals(result.exit_code, 0, 'Tarantool interactive mode is fine')
    -- XXX: Readline on CentOS 7 produces \e[?1034h escape
    -- sequence before tarantool> prompt, remove it.
    t.assert_str_contains(result.stdout:gsub('\x1b%[%?1034h', ''),
                          prompt.output, prompt.pattern,
                          'stdout is Tarantool prompt')
    t.assert_str_contains(result.stderr, greeting.output, greeting.pattern,
                          'stderr is Tarantool greeting')
end

g.test_tarantool_execute_and_interactive = function()
    local result = simplerun({'-e "print(42)"', '-i', '</dev/null'})
    t.assert_equals(result.exit_code, 0,
                    'Lua execute and interactive mode is fine')
    -- XXX: Readline on CentOS 7 produces \e[?1034h escape
    -- sequence before tarantool> prompt, remove it.
    t.assert_str_contains(result.stdout:gsub('\x1b%[%?1034h', ''),
                          table.concat({42, prompt.output}, '\n'),
                          prompt.pattern,
                          'stdout is Lua one-liner output and Tarantool prompt')
    t.assert_str_contains(result.stderr, greeting.output, greeting.pattern,
                          'stderr is Tarantool greeting')
end

g.test_tarantool_execute_with_script_and_interactive_with_no_effect = function()
    local result = simplerun({'-e "print(42)"', 'script.lua', '-i'})
    t.assert_equals(result.exit_code, 0,
                    'Lua execute script and Tarantool interactive mode is fine')
    -- XXX: Readline on CentOS 7 produces \e[?1034h escape
    -- sequence before tarantool> prompt, remove it.
    t.assert_str_contains(result.stdout:gsub('\x1b%[%?1034h', ''),
                          table.concat({42, _VERSION}, '\n'),
                          prompt.pattern,
                          'stdout is Lua one-liner output and script output')
    t.assert_str_contains(result.stderr, '', 'stderr is empty')
end
