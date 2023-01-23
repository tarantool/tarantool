local server = require('luatest.server')
local fio = require('fio')
local it = require('test.interactive_tarantool')
local t = require('luatest')

local g = t.group()

local child

g.before_each(function()
    child = it.new()
end)

g.after_each(function()
    if g.server ~= nil then
        g.server:stop()
    end
    if child ~= nil then
        child:close()
    end
end)

-- Run a command and assert a single line output from it.
local function repl(command, exp_line)
    child:execute_command(command)
    local response = child:read_line()
    t.assert_is(response, exp_line)
end

g.test_empty_default_local_eos = function()
    repl('\\set output lua', 'true')
    repl('42', '42')
end

g.test_configure_local_eos = function()
    repl('\\set output lua,local_eos=/', 'true/')
    repl('42', '42/')
    repl('\\set output lua,local_eos=', 'true')
    repl('42', '42')
end

g.test_client_server_local_eos = function()
    g.server = server:new({alias = 'console_server'})
    g.server:start()

    local socket_path = fio.pathjoin(g.server.workdir, 'admin.socket')
    local listen_command  = "require('console').listen('%s')"
    local connect_command = "require('console').connect('%s')"

    listen_command  = listen_command:format(socket_path)
    connect_command = connect_command:format(socket_path)

    -- Listen on the server.
    g.server:eval(listen_command)

    -- Connect the interactive client to the server's console.
    child:execute_command(connect_command)
    local response = child:read_response()
    t.assert_equals(response, true)
    child:set_prompt(('unix/:%s> '):format(socket_path))

    repl('\\set output lua,local_eos=/', 'true/')
    repl('42', '42/')
    repl('\\set output lua,local_eos=', 'true')
    repl('42', '42')
end
