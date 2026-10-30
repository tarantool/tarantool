local server = require('luatest.server')
local t = require('luatest')

local helper = require('test.app-luatest.gh_7288_console_helper')

local g = t.group('gh-7288', {
    {flavour = 'alocal'},
    {flavour = 'remote_txt'},
    {flavour = 'remote_bin'},
})

g.before_all(function(cg)
    cg.remote = helper.StartRemote()
    cg.server = server:new{alias = 'main',
                           box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
    cg.server:exec(function(uri, flavour)
        local helper = require('test.app-luatest.gh_7288_console_helper')

        local console = helper.TestConsole:new(uri, flavour)
        rawset(_G, 'console', console)

        local true_output = [[
---
- true
...
]]
        rawset(_G, 'true_output', true_output)

        local false_output = [[
---
- false
...
]]
        rawset(_G, 'false_output', false_output)

    end, {cg.remote.uri, cg.params.flavour})
end)

g.after_all(function(cg)
    cg.server:drop()
    cg.remote.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        _G.console:connect()
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
       _G.console:disconnect()
    end)
end)

g.test_begin_in_expr_without_error = function(cg)
    cg.server:exec(function()
        local console = _G.console
        console:send('box.begin()')
        t.assert_equals(console:send('box.is_in_txn()'), _G.true_output)
    end)
end

g.test_begin_in_expr_with_error = function(cg)
    cg.server:exec(function()
        local console = _G.console
        local expected = [[
---
- error: '[string "box.begin() error("test error")"]:1: test error'
...
]]
        t.assert_equals(console:send('box.begin() error("test error")'), expected)
        t.assert_equals(console:send('box.is_in_txn()'), _G.false_output)
    end)
end

g.test_error_in_different_expr = function(cg)
    cg.server:exec(function()
        local console = _G.console
        console:send('box.begin()')
        console:send('error("test error")')
        t.assert_equals(console:send('box.is_in_txn()'), _G.true_output)
    end)
end

g = t.group('gh-7288-bin-backcompat')

g.before_all(function()
    g.remote = helper.StartRemote()
    g.server = server:new{alias = 'backcompat',
                           box_cfg = {memtx_use_mvcc_engine = true}}
    g.server:start()
    g.server:exec(function(uri)
        local helper = require('test.app-luatest.gh_7288_console_helper')
        local netbox = require('net.box')

        local connect_old = netbox.connect
        netbox.connect = function(...)
            local remote = connect_old(...)
            remote.peer_protocol_features.streams = false
            return remote
        end
        local console = helper.TestConsole:new(uri, 'remote_bin')
        rawset(_G, 'console', console)
    end, {g.remote.uri})
end)

g.after_all(function()
    g.server:drop()
    g.remote.server:drop()
end)

g.before_each(function()
    g.server:exec(function()
        _G.console:connect()
    end)
end)

g.after_each(function()
    g.server:exec(function()
       _G.console:disconnect()
    end)
end)

g.test_remote_bin_no_streams_works = function(g)
    g.server:exec(function()
        local console = _G.console
        -- first check we have backcompat mode without streams
        local expected = [[
---
- error: Transaction is active at return from function
...
]]
        t.assert_equals(console:send('box.begin()'), expected)
        local expected = [[
---
- 4
...
]]
        t.assert_equals(console:send('2 + 2'), expected)
    end)
end
