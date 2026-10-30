local t = require('luatest')
local fio = require('fio')

local justrun = require('luatest.justrun')
local treegen = require('luatest.treegen')

local g = t.group()

-- Check that environment cfg values are set correctly.
g.test_gh_5602_tarantool_env_variables_1 = function()
    local dir = treegen.prepare_directory({}, {})
    local listen_sock = fio.pathjoin(dir, 'listen.sock')
    local script_name = 'test_gh_5602_tarantool_env_variables_1.lua'
    local script_body = ([[
local test = require('tap').test('gh-5602')
local status, err = pcall(box.cfg, {background = false, vinyl_timeout = 70.1})

test:plan(6)
test:ok(status, 'box.cfg is successful')
test:is(box.cfg['listen'], '%s', 'listen')
test:is(box.cfg['readahead'], 10000, 'readahead')
test:is(box.cfg['strip_core'], false, 'strip_core')
test:is(box.cfg['log_format'], 'json', 'log_format is correct')
test:is(box.cfg['log_nonblock'], false, 'log_nonblock')

os.exit(test:check() and 0 or 1, true) ]]):format(listen_sock)
    treegen.write_file(dir, script_name, script_body)
    local opts = { nojson = true,  stderr = true }
    local env = {
        TT_LISTEN = listen_sock,
        TT_READAHEAD = 10000,
        TT_STRIP_CORE = false,
        TT_LOG_FORMAT = 'json',
        TT_LOG_NONBLOCK = false,
    }

    local res = justrun.tarantool(dir, env, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..6
ok - box.cfg is successful
ok - listen
ok - readahead
ok - strip_core
ok - log_format is correct
ok - log_nonblock]])
end

-- Check that environment cfg values are set correctly.
g.test_gh_5602_tarantool_env_variables_2 = function()
    local dir = treegen.prepare_directory({}, {})
    local listen_sock1 = fio.pathjoin(dir, 'listen1.sock')
    local listen_sock2 = fio.pathjoin(dir, 'listen2.sock')
    local script_name = 'test_gh_5602_tarantool_env_variables_2.lua'
    local script_body = ([[
local test = require('tap').test('gh-5602')
local status, err = pcall(box.cfg)

test:plan(6)
test:ok(status, 'box.cfg is successful')
local listen = box.cfg['listen']
test:is(type(listen), 'table', 'listen is table')
test:ok(listen[1] == '%s', 'listen URI 1')
test:ok(listen[2] == '%s', 'listen URI 2')
test:is(box.cfg['replication_connect_timeout'], 0.01,
        'replication_connect_timeout')
test:is(box.cfg['replication_synchro_quorum'], '4 + 1',
        'replication_synchro_quorum')

os.exit(test:check() and 0 or 1)]]):format(listen_sock1, listen_sock2)
    treegen.write_file(dir, script_name, script_body)
    local opts = { nojson = true,  stderr = true }
    local env = {
        TT_LISTEN = ('%s,%s'):format(listen_sock1, listen_sock2),
        TT_REPLICATION_CONNECT_TIMEOUT = 0.01,
        TT_REPLICATION_SYNCHRO_QUORUM = '4 + 1',
    }
    local res = justrun.tarantool(dir, env, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..6
ok - box.cfg is successful
ok - listen is table
ok - listen URI 1
ok - listen URI 2
ok - replication_connect_timeout
ok - replication_synchro_quorum]])
end

-- Check that box.cfg{} values are more prioritized than
-- environment cfg values.
g.test_gh_5602_tarantool_env_variables_3 = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_gh_5602_tarantool_env_variables_3.lua'
    treegen.write_file(dir, script_name, [[
local test = require('tap').test('gh-5602')
local status, err = pcall(box.cfg, {background = false, vinyl_timeout = 70.1})

test:plan(3)
test:ok(status, 'box.cfg is successful')
test:is(box.cfg['background'], false,
        'box.cfg{} background value is prioritized')
test:is(box.cfg['vinyl_timeout'], 70.1,
        'box.cfg{} vinyl_timeout value is prioritized')

os.exit(test:check() and 0 or 1, true)
    ]])
    local opts = { nojson = true,  stderr = true }
    local env = {
        TT_BACKGROUND = true,
        TT_VINYL_TIMEOUT = 60.1,
    }
    local res = justrun.tarantool(dir, env, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..3
ok - box.cfg is successful
ok - box.cfg{} background value is prioritized
ok - box.cfg{} vinyl_timeout value is prioritized]])
end

-- Check bad environment cfg values.
g.test_gh_5602_tarantool_env_variables_4 = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_gh_5602_tarantool_env_variables_4.lua'
    treegen.write_file(dir, script_name, [[
local test = require('tap').test('gh-5602')
local status, err = pcall(box.cfg, {background = false, vinyl_timeout = 70.1})
local err_msg_fmt = 'Environment variable TT_%s has incorrect value for ' ..
                    'option "%s": should be %s'

-- Check bad environment cfg values.
test:plan(2)
test:ok(not status, 'box.cfg is not successful')
local exp_err = err_msg_fmt:format('SQL_CACHE_SIZE', 'sql_cache_size',
    'convertible to a number')
local err_msg = tostring(err)
while err_msg:find('^.-:.-: ') do
    err_msg = err_msg:gsub('^.-:.-: ', '')
end
test:is(err_msg, exp_err, 'bad sql_cache_size value')

os.exit(test:check() and 0 or 1, true)
    ]])
    local opts = { nojson = true,  stderr = true }
    local env = {
        TT_SQL_CACHE_SIZE = 'a',
    }
    local res = justrun.tarantool(dir, env, { script_name }, opts)
    t.assert_equals(res.stderr, '')
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..2
ok - box.cfg is not successful
ok - bad sql_cache_size value]])
end

-- Check bad environment cfg values.
g.test_gh_5602_tarantool_env_variables_5 = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_gh_5602_tarantool_env_variables_5.lua'
    treegen.write_file(dir, script_name, [[
local status, err = pcall(box.cfg, {background = false, vinyl_timeout = 70.1})
local err_msg_fmt = 'Environment variable TT_%s has incorrect value for ' ..
                    'option "%s": should be %s'

local test = require('tap').test('gh-5602')

test:plan(2)
test:ok(not status, 'box.cfg is not successful')
local exp_err = err_msg_fmt:format('STRIP_CORE', 'strip_core',
    '"true" or "false"')
local err_msg = tostring(err)
while err_msg:find('^.-:.-: ') do
    err_msg = err_msg:gsub('^.-:.-: ', '')
end
test:is(err_msg, exp_err, 'bad strip_core value')

os.exit(test:check() and 0 or 1, true)
    ]])
    local opts = { nojson = true,  stderr = true }
    local env = {
        TT_STRIP_CORE = 'a',
    }
    local res = justrun.tarantool(dir, env, { script_name }, opts)
    t.assert_equals(res.stderr, '')
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..2
ok - box.cfg is not successful
ok - bad strip_core value]])
end
