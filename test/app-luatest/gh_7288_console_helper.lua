local console = require('console')
local fiber = require('fiber')
local fio = require('fio')
local server = require('luatest.server')
local string = require('string')
local t = require('luatest')

local remote_mt = {
    start = function(self)
        local server = server:new{
            alias = 'remote',
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

--
-- Mock read/print methods to run a console. Without running console we can't
-- call console.connect to test remote consoles.
--
local TestConsole = {}

TestConsole.new = function(self, flavour_name)
    local o = { flavour = flavours[flavour_name] }
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
    t.assert(on_start:get(3))
    if self.flavour.connect then
        self.flavour:connect(self)
    end
end

TestConsole.__index.send = function(self, input)
    self.ich:put(input)
    return t.assert(self.och:get(3))
end

TestConsole.__index.disconnect = function(self)
    if self.flavour.disconnect then
        self:send(nil)
    end
    self.ich:close()
    -- read console fiber exit message
    t.assert(self.och:get(3))
    self.och:close()
    self.ich = nil
    self.och = nil
    console.on_start(nil)
end

return {
    TestConsole = TestConsole,
}
