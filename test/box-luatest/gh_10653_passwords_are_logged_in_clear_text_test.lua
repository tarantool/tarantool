local server = require("luatest.server")
local t = require("luatest")

local g = t.group()

g.before_all(function()
    g.single_uri_with_creds = "user:passwrd@127.0.0.1:0"
    g.server1 = server:new({ alias = "server1" })

    g.server1:start()
    g.server1:exec(function()
        box.session.su("admin")
        box.schema.user.create("user", { password = "passwrd" })
        box.schema.user.grant("user", "super")
    end)
end)

g.after_all(function()
    g.server1:drop()
end)

local function assert_value_not_in_cfg_logs(box_cfg, server, cfg_option, value)
    server:update_box_cfg(box_cfg)
    t.assert(server:grep_log(string.format(
        "set '%s' configuration option", cfg_option)))
    t.assert_not(server:grep_log(value))
end

g.test_password_not_in_combined_uris_listen_logs = function()
    assert_value_not_in_cfg_logs(
        {listen = {{g.single_uri_with_creds}, g.single_uri_with_creds}},
        g.server1, "listen", "passwrd")
end

g.test_password_not_in_several_uri_tables_listen_logs = function()
    assert_value_not_in_cfg_logs(
        {listen = {{g.single_uri_with_creds}, {g.single_uri_with_creds}}},
        g.server1, "listen", "passwrd")
end

g.test_password_not_in_uri_table_listen_logs = function()
    assert_value_not_in_cfg_logs(
        {listen = {uri = g.single_uri_with_creds}},
        g.server1, "listen", "passwrd")
end

g.test_password_not_in_comma_separated_uris_listen_logs = function()
    assert_value_not_in_cfg_logs(
        {listen = g.single_uri_with_creds .. ", " .. g.single_uri_with_creds},
        g.server1, "listen", "passwrd")
end

g.test_password_not_in_single_uri_listen_logs = function()
    assert_value_not_in_cfg_logs(
        {listen = g.single_uri_with_creds},
        g.server1, "listen", "passwrd")
end

local g_repl = t.group('password_not_in_replication_logs')

local function get_listen_uri_with_password()
    -- box.info.listen strips user and password.
    return 'user:passwrd@' .. box.info.listen
end

g_repl.before_all(function(cg)
    local single_uri = '127.0.0.1:0'

    -- We cannot start a server with `127.0.0.1:0` listen address in luatest,
    -- because we don't know which port the server will take and don't know
    -- which port to connect to. So instead start a server with predefined
    -- luatest listening address, and reconfigure box.cfg.listen afterwards.
    cg.server1 = server:new({ alias = "server1" })
    cg.server1:start()
    cg.server1:exec(function(single_uri)
        box.session.su("admin")
        box.schema.user.create("user", { password = "passwrd" })
        box.schema.user.grant("user", "super")
        box.cfg{listen = single_uri}
    end, {single_uri})
    cg.last_server1_uri = cg.server1:exec(get_listen_uri_with_password)

    local box_cfg = {
        replication = cg.last_server1_uri,
        bootstrap_strategy = 'config',
        bootstrap_leader = cg.last_server1_uri,
    }
    cg.server2 = server:new({
        alias = "server2",
        box_cfg = box_cfg,
    })
    cg.server2:start()
    cg.server2:update_box_cfg{
        listen = single_uri,
    }
    cg.last_server2_uri = cg.server2:exec(get_listen_uri_with_password)
end)

g_repl.after_all(function(cg)
    cg.server1:drop()
    cg.server2:drop()
end)

g_repl.test_password_not_in_bootstrap_leader_logs = function(cg)
    t.assert(cg.server2:grep_log(string.format(
        "set '%s' configuration option", 'bootstrap_leader')))
    t.assert_not(cg.server2:grep_log('passwrd'))
end

g_repl.test_password_not_in_single_uri_replication_logs = function(cg)
    cg.server2:update_box_cfg({replication = ""})
    assert_value_not_in_cfg_logs(
        {replication = cg.last_server1_uri},
        cg.server2, "replication", "passwrd")
end

g_repl.test_password_not_in_uri_table_replication_logs = function(cg)
    cg.server2:update_box_cfg({replication = ""})
    assert_value_not_in_cfg_logs(
        {replication = {uri = cg.last_server1_uri}},
        cg.server2, "replication", "passwrd")
end

g_repl.test_password_not_in_several_uris_replication_logs = function(cg)
    cg.server2:update_box_cfg({replication = ""})
    assert_value_not_in_cfg_logs(
        {replication = {cg.last_server1_uri, cg.last_server2_uri}},
        cg.server2, "replication", "passwrd")
end

g_repl.test_password_not_in_several_uri_tables_replication_logs = function(cg)
    cg.server2:update_box_cfg({replication = ""})
    assert_value_not_in_cfg_logs(
        {replication = {{cg.last_server1_uri}, {cg.last_server2_uri}}},
        cg.server2, "replication", "passwrd")
end

g_repl.test_password_not_in_combined_uris_replication_logs = function(cg)
    cg.server2:update_box_cfg({replication = ""})
    assert_value_not_in_cfg_logs(
        {replication = {{cg.last_server1_uri}, cg.last_server2_uri}},
        cg.server2, "replication", "passwrd")
end
