local fio = require('fio')
local t = require('luatest')
local server = require('luatest.server')
local it = require('test.interactive_tarantool')

local g = t.group()

-- Create test instances and open connections.
g.before_all(function()
    g.server = server:new({alias = 'test-gh-8817-server'})
    g.server:start()

    local socket_path = fio.pathjoin(g.server.workdir, 'admin.socket')
    local listen_command  = "require('console').listen('%s')"
    local connect_command = "require('console').connect('%s')"

    listen_command  = listen_command:format(socket_path)
    connect_command = connect_command:format(socket_path)

    -- Listen on the server.
    g.server:eval(listen_command)

    g.first_client = it:new()
    g.second_client = it:new()

    -- Create first connection to the server.
    g.first_client:execute_command(connect_command)
    t.assert_equals(
        g.first_client:read_response(),
        true
    )
    g.first_client:set_prompt(('unix/:%s> '):format(socket_path))

    -- Create second connection to the server.
    g.second_client:execute_command(connect_command)
    t.assert_equals(
        g.second_client:read_response(),
        true
    )
    g.second_client:set_prompt(('unix/:%s> '):format(socket_path))

    -- Make sure that box.session.id are different on test clients.
    g.first_client:execute_command("box.session.id()")
    local first_client_session_id = g.first_client:read_response()
    g.second_client:execute_command("box.session.id()")
    local second_client_session_id = g.second_client:read_response()
    t.assert_not_equals(
        first_client_session_id,
        second_client_session_id
    )
end)

-- Stop test instances.
g.after_all(function()
    g.server:stop()
    g.first_client:close()
    g.second_client:close()
end)

-- Checks that the language setting is not shared between clients.
g.test_set_language_not_shared_between_clients = function()
    -- Check initial language on clients.
    g.first_client:execute_command("\\set language")
    t.assert_equals(
        g.first_client:read_response(),
        {language = "lua"}
    )
    g.second_client:execute_command("\\set language")
    t.assert_equals(
        g.second_client:read_response(),
        {language = "lua"}
    )

    -- Set new language on first client.
    g.first_client:execute_command("\\set language sql")
    t.assert_equals(
        g.first_client:read_response(),
        true
    )

    -- Check that language changes on the first client
    -- did not spread to the second.
    g.second_client:execute_command("\\set language")
    t.assert_equals(
        g.second_client:read_response(),
        {language = "lua"}
    )
end
