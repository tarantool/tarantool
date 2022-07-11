local server = require('test.luatest_helpers.server')
local console = require('console')
local fiber = require('fiber')
local fio = require('fio')
local string = require('string')
local netbox = require('net.box')
local t = require('luatest')

local function configure_box()
    local workdir = fio.pathjoin(server.vardir, 'gh-7288')
    fio.rmtree(workdir)
    fio.mkdir(workdir)
    box.cfg{
        memtx_use_mvcc_engine = true,
        work_dir = workdir,
        log = fio.pathjoin(workdir, 'self.log'),
    }
end

local remote_mt = {
    start = function(self)
        local server = server:new{
            alias = 'default',
            box_cfg = {memtx_use_mvcc_engine = true},
        }
        server:start()
        if not self.bin then
            self.uri = fio.pathjoin(server.vardir, 'gh-7288.sock')
            server:eval(string.format('require("console").listen("%s")', self.uri))
        else
            self.uri = server.net_box_uri
        end
        self.server = server
    end,

    stop = function(self)
        self.server:stop()
        self.server = nil
        self.uri = nil
    end,

    connect = function(self, console)
        console:send(string.format('require("console").connect("%s")', self.uri))
    end,

    disconnect = true,
}

local flavours = {
    alocal = {},
    remote_txt = setmetatable({bin = false}, {__index = remote_mt}),
    remote_bin = setmetatable({bin = true}, {__index = remote_mt}),
}

local g = t.group('gh-7288', {
    {name = 'alocal'},
    {name = 'remote_txt'},
    {name = 'remote_bin'},
})


--
-- Mock read/print methods to run a console. Without running console we can't
-- call console.connect to test remote consoles.
--
local TestConsole = {}

TestConsole.new = function(self, flavour)
    local o = { flavour = flavour }
    setmetatable(o, self)
    return o
end

TestConsole.__index = {}
TestConsole.__index.start = function(self)
    if self.flavour.start then
        self.flavour:start()
    end
end

TestConsole.__index.stop = function(self)
    if self.flavour.stop then
        self.flavour:stop()
    end
end

TestConsole.__index.connect = function(self)
    self.ich = fiber.channel(10)
    self.och = fiber.channel(10)
    local on_start = fiber.channel()
    console.on_start(function(console)
        console.read = function(_)
            return self.ich:get()
        end
        console.print = function(_, output)
            self.och:put(output)
        end
        on_start:put(true)
    end)
    fiber.create(function()
        console.start()
        -- write console fiber exit message
        self.och:put(true)
    end)
    assert(on_start:get(3))
    if self.flavour.connect then
        self.flavour:connect(self)
    end
end

TestConsole.__index.send = function(self, input)
    self.ich:put(input)
    return assert(self.och:get(3))
end

TestConsole.__index.disconnect = function(self)
    if self.flavour.disconnect then
        self:send(nil)
    end
    self.ich:close()
    -- read console fiber exit message
    assert(self.och:get(3))
    self.och:close()
    self.ich = nil
    self.och = nil
    console.on_start(nil)
end

configure_box()

g.before_all(function(cg)
    cg.console = TestConsole:new(flavours[cg.params.name])
    cg.console:start()
end)

g.after_all(function(cg)
    cg.console:stop()
    cg.console = nil
end)

g.before_each(function(cg)
    cg.console:connect()
end)

g.after_each(function(cg)
    cg.console:disconnect()
end)

local true_output = [[
---
- true
...
]]

local false_output = [[
---
- false
...
]]

g.test_begin_in_expr_without_error = function(cg)
    local console = cg.console
    console:send('box.begin()')
    t.assert_equals(console:send('box.is_in_txn()'), true_output)
end

g.test_begin_in_expr_with_error = function(cg)
    local console = cg.console
    local expected = [[
---
- error: '[string "box.begin() error("test error")"]:1: test error'
...
]]
    t.assert_equals(console:send('box.begin() error("test error")'), expected)
    t.assert_equals(console:send('box.is_in_txn()'), false_output)
end

g.test_error_in_different_expr = function(cg)
    local console = cg.console
    console:send('box.begin()')
    console:send('error("test error")')
    t.assert_equals(console:send('box.is_in_txn()'), true_output)
end

local gb = t.group('gh-7288-bin-backcompat')

gb.before_all(function()
    gb.save = netbox.connect
    netbox.connect = function(...)
        local remote = gb.save(...)
        remote.peer_protocol_features.streams = false
        return remote
    end
    gb.console = TestConsole:new(flavours.remote_bin)
    gb.console:start()
end)

gb.after_all(function()
    gb.console:stop()
    gb.console = nil
    netbox.connect = gb.save
end)

gb.before_each(function()
    gb.console:connect()
end)

gb.after_each(function()
    gb.console:disconnect()
end)

gb.test_remote_bin_no_streams_works = function(cg)
    local console = cg.console
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
end
