#!/usr/bin/env tarantool

local tap = require('tap')
local popen = require('popen')
local process_timeout = require('process_timeout')
local fio = require('fio')
local clock = require('clock')
local fiber = require('fiber')

--
-- gh-2717: tarantool console quit on sigint.
--
local file_read_timeout = 60.0
local file_read_interval = 0.2
local file_open_timeout = 60.0
local prompt = 'tarantool> '

local TARANTOOL_PATH = arg[-1]
local test = tap.test('gh-2717-no-quit-sigint')

test:plan(8)
local cmd = 'INPUTRC=non_existent_file ' .. TARANTOOL_PATH .. ' -i 2>&1'
local ph = popen.new({cmd}, {
    shell = true,
    setsid = true,
    group_signal = true,
    stdout = popen.opts.PIPE,
    stderr = popen.opts.DEVNULL,
    stdin = popen.opts.PIPE,
})
assert(ph, 'process is not up')

local start_time = clock.monotonic()
local time_quota = 10.0

local output = ''
while output:find(prompt) == nil
        and clock.monotonic() - start_time < time_quota do
    local data = ph:read({timeout = 1.0})
    if data ~= nil then
        output = output .. data
    end
end
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')
ph:signal(popen.signal.SIGINT)

while output:find(prompt .. '^C\n---\n...\n\n' .. prompt) == nil and
        clock.monotonic() - start_time < time_quota do
    local data = ph:read({timeout = 1.0})
    if data ~= nil then
        output = output .. data
    end
end
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')

test:unlike(ph:info().status.state, popen.state.EXITED,
            'SIGINT doesn\'t kill tarantool in interactive mode')
test:like(output, prompt .. '^C\n---\n...\n\n' .. prompt,
          'Ctrl+C discards the input and calls the new prompt')

ph:shutdown({stdin = true})
ph:close()

--
-- gh-7109: Ctrl+C does not break multiline input.
--
local cmd = 'INPUTRC=non_existent_file ' .. TARANTOOL_PATH .. ' -i 2>&1'
ph = popen.new({cmd}, {
    shell = true,
    setsid = true,
    group_signal = true,
    stdout = popen.opts.PIPE,
    stderr = popen.opts.DEVNULL,
    stdin = popen.opts.PIPE,
})
assert(ph, 'process is not up')

start_time = clock.monotonic()
time_quota = 10.0
output = ''
local prompt_name = 'tarantool'
local expected_output = prompt
while output:find(expected_output) == nil
        and clock.monotonic() - start_time < time_quota do
    local data = ph:read({timeout = 1.0})
    if data ~= nil then
        output = output .. data
    end
end
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')

ph:write('foo{\n')

start_time = clock.monotonic()
time_quota = 10.0
expected_output = expected_output .. 'foo{' .. '\n' ..
        string.rep(' ', #prompt_name) .. '> '

while output:find(expected_output) == nil and
        clock.monotonic() - start_time < time_quota do
    local data = ph:read({timeout = 1.0})
    if data ~= nil then
        output = output .. data
    end
end
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')

ph:signal(popen.signal.SIGINT)

start_time = clock.monotonic()
time_quota = 10.0
expected_output = expected_output .. '^C\n---\n...\n\n' .. prompt

while output:find(expected_output) == nil and
        clock.monotonic() - start_time < time_quota do
    local data = ph:read({timeout = 1.0})
    if data ~= nil then
        output = output .. data
    end
end
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')

test:unlike(ph:info().status.state, popen.state.EXITED,
        'SIGINT doesn\'t kill tarantool in interactive mode')
test:like(output, expected_output,
        'Ctrl+C discards the multiline input mode and calls the new default prompt')

ph:shutdown({stdin = true})
ph:close()

--
-- gh-2717: testing daemonized tarantool on signaling INT
-- and nested console output.
--
local log_file = fio.abspath('tarantool.log')
local pid_file = fio.abspath('tarantool.pid')
local xlog_file = fio.abspath('00000000000000000000.xlog')
local snap_file = fio.abspath('00000000000000000000.snap')
local sock = fio.abspath('tarantool.soc')

local user_grant = ' box.schema.user.grant(\'guest\', \'super\')'
local arg = ' -e \"box.cfg{pid_file=\''
            .. pid_file .. '\', log=\'' .. log_file .. '\', listen=\'unix/:'
            .. sock .. '\'}' .. user_grant .. '\"'

start_time = clock.monotonic()
time_quota = 10.0

os.remove(log_file)
os.remove(pid_file)
os.remove(xlog_file)
os.remove(snap_file)

local ph_server = popen.shell(TARANTOOL_PATH .. arg, 'r')

local f = process_timeout.open_with_timeout(log_file, file_open_timeout)
assert(f, 'error while opening ' .. log_file)

cmd = TARANTOOL_PATH .. ' -i 2>&1'
local ph_client = popen.new({cmd}, {
    shell = true,
    setsid = true,
    group_signal = true,
    stdout = popen.opts.PIPE,
    stderr = popen.opts.DEVNULL,
    stdin = popen.opts.PIPE,
})
assert(ph_client, 'the nested console is not up')

ph_client:write('require("console").connect(\'unix/:' .. sock .. '\')\n')

local client_data = ''
while string.endswith(client_data, 'unix/:' .. sock .. '>') == nil
        and clock.monotonic() - start_time < time_quota do
    local cur_data = ph_client:read({timeout = 3.0})
    if cur_data ~= nil and cur_data ~= '' then
        client_data = client_data .. cur_data
    end
end
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')

start_time = clock.monotonic()
ph_client:signal(popen.signal.SIGINT)
test:unlike(ph_client:info().status.state, popen.state.EXITED,
            'SIGINT doesn\'t kill nested tarantool in interactive mode')
while string.endswith(client_data, 'C\n---\n...\n\nunix/:' .. sock .. '>') == nil
        and clock.monotonic() - start_time < time_quota do
    local cur_data = ph_client:read({timeout = 3.0})
    if cur_data ~= nil and cur_data ~= '' then
        client_data = client_data .. cur_data
    end
end
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')

ph_server:signal(popen.signal.SIGINT)

local status = ph_server:wait(nil, popen.signal.SIGINT)
test:unlike(status.state, popen.state.ALIVE,
            'SIGINT terminates tarantool in daemon mode')

start_time = clock.monotonic()
local data = ''
while data:match('got signal 2') == nil
        and clock.monotonic() - start_time < time_quota do
    data = data .. process_timeout.read_with_timeout(f,
            file_read_timeout,
            file_read_interval)
end
assert(data:match('got signal 2'), 'there is no one note about SIGINT')
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')

f:close()
ph_server:close()
ph_client:close()
os.remove(log_file)
os.remove(pid_file)
os.remove(xlog_file)
os.remove(snap_file)

--
-- Testing case when the client and instance are called in the same console.
--
cmd = 'INPUTRC=non_existent_file ' .. TARANTOOL_PATH .. ' -i 2>&1'
ph = popen.new({cmd}, {
    shell = true,
    setsid = true,
    group_signal = true,
    stdout = popen.opts.PIPE,
    stderr = popen.opts.DEVNULL,
    stdin = popen.opts.PIPE,
})
assert(ph, 'process is not up')

start_time = clock.monotonic()
time_quota = 10.0

output = ''
while output:find(prompt) == nil
        and clock.monotonic() - start_time < time_quota do
    data = ph:read({timeout = 1.0})
    if data ~= nil then
        output = output .. data
    end
end
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')

prompt = 'unix/:' .. sock .. '> '
ph:write('_ = require(\'console\').listen(\'' .. sock .. '\')\n',
         {timeout = 1.0})
ph:write('_ = require(\'console\').connect(\'' .. sock .. '\')\n',
         {timeout = 1.0})

fiber.sleep(0.2)
ph:signal(popen.signal.SIGINT)

start_time = clock.monotonic()
while string.endswith(output, prompt .. '^C\n---\n...\n\n' .. prompt) == false
        and clock.monotonic() - start_time < time_quota do
    local data = ph:read({timeout = 1.0})
    if data ~= nil then
        output = output .. data
    end
end
assert(clock.monotonic() - start_time < time_quota, 'time_quota is violated')

test:ok(string.endswith(output, prompt .. '^C\n---\n...\n\n' .. prompt),
        'Ctrl+C discards the input and calls the new prompt')
test:unlike(ph:info().status.state, popen.state.EXITED,
            'SIGINT doesn\'t kill tarantool in interactive mode')

ph:shutdown({stdin = true})
ph:close()

os.exit(test:check() and 0 or 1)
