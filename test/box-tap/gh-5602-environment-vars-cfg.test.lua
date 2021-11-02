#!/usr/bin/env tarantool

local os = require('os')
local fio = require('fio')
local tap = require('tap')

local test = tap.test('gh-5602')

-- gh-5602: Check that environment cfg variables working.
local TARANTOOL_PATH = arg[-1]
local script_name = 'gh-5602-environment-cfg-test-cases.lua'
local path_to_script = fio.pathjoin(
        os.getenv('PWD'),
        'box-tap',
        script_name)

-- Generate a shell command like
-- `FOO=x BAR=y /path/to/tarantool /path/to/script.lua 42`.
local function shell_command(case, i)
    return ('%s %s %s %d'):format(
        case,
        TARANTOOL_PATH,
        path_to_script,
        i)
end

local listen_sock = fio.pathjoin(fio.cwd(), 'listen.sock')
local repl1_sock = fio.pathjoin(fio.cwd(), 'repl1.sock')
local repl2_sock = fio.pathjoin(fio.cwd(), 'repl2.sock')

local cases = {
    ('%s %s %s %s %s'):format(
        string.format('TT_LISTEN=%s', listen_sock),
        'TT_READAHEAD=10000',
        'TT_STRIP_CORE=false',
        'TT_LOG_FORMAT=json',
        'TT_LOG_NONBLOCK=false'),
    ('%s %s %s %s'):format(
        string.format('TT_LISTEN=%s', listen_sock),
        string.format('TT_REPLICATION=%s,%s', repl1_sock,repl2_sock),
        'TT_REPLICATION_CONNECT_TIMEOUT=0.01',
        'TT_REPLICATION_SYNCHRO_QUORUM=\'4 + 1\''),
    'TT_BACKGROUND=true TT_VINYL_TIMEOUT=60.1',
    'TT_SQL_CACHE_SIZE=a',
    'TT_STRIP_CORE=a',
}

test:plan(1)
local exit_status_list = {}
local exit_status_list_exp = {}
for i, case in ipairs(cases) do
    exit_status_list[i] = os.execute(shell_command(case, i))
    exit_status_list_exp[i] = 0
end

test:is_deeply(exit_status_list, exit_status_list_exp, 'exit status list')

os.exit(test:check() and 0 or 1)
