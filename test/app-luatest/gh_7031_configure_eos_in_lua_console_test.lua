local server = require('luatest.server')
local it = require('test.interactive_tarantool')
local t = require('luatest')

local g = t.group()

local child

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
    child = it.new()

    repl('\\set output lua', 'true')
    repl('42', '42')
end

g.test_configure_local_eos = function()
    child = it.new()

    repl('\\set output lua,local_eos=/', 'true/')
    repl('42', '42/')
    repl('\\set output lua,local_eos=', 'true')
    repl('42', '42')
end

g.test_client_server_local_eos = function(g)
    g.server = server:new({alias = 'console_server'})
    g.server:start()

    -- Listen on the server.
    g.server:eval("require('console').listen('unix/:./tarantool.control')")

    -- Connect the interactive client to the server's console.
    child = it.connect(g.server)

    repl('\\set output lua,local_eos=/', 'true/')
    repl('42', '42/')
    repl('\\set output lua,local_eos=', 'true')
    repl('42', '42')
end
