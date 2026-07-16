-- Exercise the tarantool command line front-end: help and version
-- messages, rejection of bad options, the `arg` global, and handling of
-- `-e`/`-l` options.

local fun = require('fun')
local t = require('luatest')
local justrun = require('luatest.justrun')
local treegen = require('luatest.treegen')

local g = t.group()

g.before_all(function(cg)
    cg.dir = treegen.prepare_directory({})
end)

local function field_or_default(tbl, field_name, def)
    if tbl == nil then
        return def
    end

    local v = tbl[field_name]
    if v == nil then
        return def
    end

    return v
end

-- {{{ Catch/verify tarantool output, testing helpers

-- Run tarantool with the given arguments and catch its stdout and stderr.
local function catch_output(dir, args, opts)
    local opts = {
        nojson = field_or_default(opts, 'nojson', true),
        stderr = field_or_default(opts, 'stderr', true),
        quote_args = field_or_default(opts, 'quote_args', true),
    }
    return justrun.tarantool(dir, {}, args, opts)
end

local verify_output_methods = {
    exit_success = function(self)
        t.assert_equals({self._res.exit_code, self._res.stderr}, {0, ''})
        return self
    end,
    exit_failure = function(self)
        t.assert_not_equals(self._res.exit_code, 0)
        return self
    end,
    stdout_equals = function(self, exp)
        t.assert_equals(self._res.stdout, exp)
        return self
    end,
    stdout_contains = function(self, exp)
        t.assert_str_contains(self._res.stdout, exp)
        return self
    end,
    stdout_not_contains = function(self, exp)
        t.assert_not_str_contains(self._res.stdout, exp)
        return self
    end,
    stderr_contains = function(self, exp)
        t.assert_str_contains(self._res.stderr, exp)
        return self
    end,
}

local verify_output_mt = {
    __index = verify_output_methods,
}

-- Verify tarantool exit code, stdout, stderr in a chained manner.
local function verify_output(res)
    return setmetatable({_res = res}, verify_output_mt)
end

-- }}} Catch/verify tarantool output, testing helpers

-- `--help` and `-h` print the usage message and the list of options to stdout
-- and exit 0.
g.test_help = function(cg)
    for _, opt in ipairs({'--help', '-h'}) do
        -- Assert a few stable substrings only: the full output contains a
        -- version string, the list of options is changed from time to time.
        verify_output(catch_output(cg.dir, {opt}))
            :exit_success()
            :stdout_contains('Tarantool ')
            :stdout_contains('Run a Tarantool instance:')
            :stdout_contains('--help')
            :stdout_contains('--version')
            :stdout_contains('Print this help message.')
    end
end

-- An unknown short option is rejected on stderr with a non-zero exit, and
-- the error wins even when a valid action option (-v) follows it.
g.test_unknown_short_option = function(cg)
    verify_output(catch_output(cg.dir, {'-Z'}))
        :exit_failure()
        :stderr_contains('invalid option')

    verify_output(catch_output(cg.dir, {'-Z', '-v'}))
        :exit_failure()
        :stderr_contains('invalid option')
end

-- An unknown long option is rejected on stderr with a non-zero exit, and
-- the error wins even when a valid action option (--version) follows it.
g.test_unknown_long_option = function(cg)
    verify_output(catch_output(cg.dir, {'--no-such-option'}))
        :exit_failure()
        :stderr_contains('unrecognized option')

    verify_output(catch_output(cg.dir, {'--no-such-option', '--version'}))
        :exit_failure()
        :stderr_contains('unrecognized option')
end

-- `--version`, `-v` and `-V` print the version and build block to stdout and
-- exit 0.
g.test_version = function(cg)
    for _, opt in ipairs({'--version', '-v', '-V'}) do
        -- Assert stable substrings, never the exact version string or the build
        -- flags.
        verify_output(catch_output(cg.dir, {opt}))
            :exit_success()
            :stdout_contains('Tarantool ')
            :stdout_contains('Target:')
            :stdout_contains('Build options:')
    end
end

-- Verify content of the `arg` global variable.
g.test_script_args = function(cg)
    -- The script prints the `arg` content as JSON.
    treegen.write_file(cg.dir, 'print_args.lua', string.dump(function()
        local json = require('json')

        print(json.encode({
            ['arg[-1]'] = arg[-1],
            ['arg[0]'] = arg[0],
            ['arg[]'] = setmetatable(arg, {__serialize = 'seq'}),
        }))
    end))

    -- Run tarantool with the script and the given args.
    local function catch_args(script_args)
        local args = fun.chain({'print_args.lua'}, script_args):totable()
        return catch_output(cg.dir, args, {nojson = false})
    end

    -- Verify the output against the given expected script args.
    local function expected_stdout(script_args)
        return {
            {
                ['arg[-1]'] = arg[-1],
                ['arg[0]'] = 'print_args.lua',
                ['arg[]'] = script_args,
            },
        }
    end

    for _, args in ipairs({
        -- No args.
        {},
        -- Some args.
        {'1', '2', '3'},
        -- Options placed after the script name are not interpreted: they land
        -- in `arg` as ordinary positional values.
        {'1', '2', '3', '-V'},
        {'-V', '1', '2', '3'},
        {'1', '2', '3', '--help'},
    }) do
        verify_output(catch_args(args))
            :exit_success()
            :stdout_equals(expected_stdout(args))
    end

    -- An action option before the script name wins: -V prints the version
    -- and the script is never run (no script output).
    treegen.write_file(cg.dir, 'hello.lua', string.dump(function()
        print('Hello from hello.lua!')
    end))
    verify_output(catch_output(cg.dir, {'-V', 'hello.lua'}))
        :exit_success()
        :stdout_contains('Tarantool ')
        :stdout_contains('Target:')
        :stdout_not_contains('Hello from hello.lua!')
end

-- gh-3966: os.exit() inside a -e chunk stops execution immediately; only
-- the output before it appears.
g.test_os_exit_in_expr = function(cg)
    verify_output(catch_output(cg.dir, {'-e', 'print(1) os.exit() print(2)'}))
        :exit_success()
        :stdout_equals('1')
end

-- gh-3966: os.exit() also stops a sequence of -e chunks; only the first
-- chunk's output appears.
g.test_os_exit_across_multiple_exprs = function(cg)
    verify_output(catch_output(cg.dir, {
        '-e', 'print(1)',
        '-e', 'os.exit()',
        '-e', 'print(1)',
        '-e', 'os.exit()',
        '-e', 'print(1)',
    }))
        :exit_success()
        :stdout_equals('1')
end

-- A -e chunk is a modifier that runs before the script: it prints Hello
-- first, then the script runs and dumps `arg`.
g.test_expr_modifier_runs_before_script = function(cg)
    treegen.write_file(cg.dir, 'hello.lua', string.dump(function()
        print('Hello from hello.lua!')
    end))

    verify_output(catch_output(cg.dir, {
        '-e', "print('Hello from -e chunk!')",
        'hello.lua',
    }))
        :exit_success()
        :stdout_equals(table.concat({
            'Hello from -e chunk!',
            'Hello from hello.lua!',
        }, '\n'))
end

-- A variable set in one -e chunk is visible in a later -e chunk.
g.test_expr_state_shared_across_exprs = function(cg)
    verify_output(catch_output(cg.dir, {
        '-e', 'a = 10',
        '-e', 'print(a)',
    }))
        :exit_success()
        :stdout_equals('10')
end

-- `-l <module>` requires the module and exposes it as a global for later
-- -e chunks.
g.test_require_module_via_l = function(cg)
    verify_output(catch_output(cg.dir, {
        '-e', "print(type(rawget(_G, 'log')))",
        '-l', 'log',
        '-e', "print(type(rawget(_G, 'log')))",
        '-e', "print(rawget(_G, 'log') == require('log'))",
    }))
        :exit_success()
        :stdout_equals(table.concat({
            'nil',
            'table',
            'true',
        }, '\n'))
end
