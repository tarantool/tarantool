-- net_box.lua (internal file)

local msgpack = require 'msgpack'
local fiber = require 'fiber'
local socket = require 'socket'
local log = require 'log'
        
local PING          = 64
local SELECT        = 1
local INSERT        = 2
local REPLACE       = 3
local UPDATE        = 4
local DELETE        = 5
local CALL          = 6
local TYPE          = 0x00
local SYNC          = 0x01
local SPACE_ID      = 0x10
local INDEX_ID      = 0x11
local LIMIT         = 0x12
local OFFSET        = 0x13
local ITERATOR      = 0x14
local KEY           = 0x20
local TUPLE         = 0x21
local FUNCTION_NAME = 0x22
local DATA          = 0x30
local ERROR         = 0x31
local GREETING_SIZE = 128
    
    
local function request(header, body)
    header = msgpack.encode(header)
    body = msgpack.encode(body)

    local len = msgpack.encode(string.len(header) + string.len(body))

    return len .. header .. body
end

local _sync = -1

local proto = {
    -- sync
    sync    = function(self)
        _sync = _sync + 1
        if _sync >= 0x7FFFFFFF then
            _sync = 0
        end
        return _sync
    end,


    ping    = function(sync)
        return request(
            { SYNC = sync, TYPE = PING },
            {}
        )
    end,


    -- lua call
    call = function(sync, proc, args)
        if args == nil then
            args = {}
        end
        return request(
            { SYNC = sync, TYPE = CALL  },
            { FUNCTION_NAME = proc, TUPLE = args }
        )
    end,

    -- insert
    insert = function(sync, spaceno, tuple)
        return request(
            { SYNC = sync, TYPE = INSERT },
            { SPACE_ID = spaceno, TUPLE = tuple }
        )
    end,
    
    -- replace
    replace = function(sync, spaceno, tuple)
        return request(
            { SYNC = sync, TYPE = REPLACE },
            { SPACE_ID = spaceno, TUPLE = tuple }
        )
    end,

    -- delete
    delete = function(sync, spaceno, key)
        return request(
            { SYNC = sync, TYPE = DELETE },
            { SPACE_ID = spaceno, KEY = key }
        )
    end,

    -- update
    -- select

}


local function connect(self)
    if self.state == 'connected' or self.state == 'connecting' then
        return
    end

    self.state = 'connecting'

    self.dns = socket.getaddrinfo(self.host, self.port,
                                            nil, { protocol = 'tcp' }) 
    if not self.is_run then
        return
    end
    
    if self.dns == nil or #self.dns < 1 then
        self:fatal("Cant resolve address %s:%s: %s", self.host, self.port,
            self.errno.strerror(self.errno()))
        return
    end

    for i, addr in pairs(self.dns) do
        if self.s ~= nil then
            self.s:close()
        end
        self.s = socket(addr.family, addr.type, addr.protocol)
        if self.s ~= nil then
            if self.s:sysconnect(addr.host, addr.port) then
                self.state = 'connected'
                return
            end
        end
    end

    if self.s ~= nil then
        self:fatal("Cant resolve address %s:%s: %s", self.host, self.port,
            self.s:errstr())
        self.s:close()
    else
        self:fatal("Cant resolve address %s:%s: %s", self.host, self.port,
            self.errno.strerror(self.errno()))
    end
end

local function run(self)
    fiber.wrap(
        function()
            while self.is_run do
                if self.state == 'connected' then
                    self:io()
                else
                    connect(self)
                end
            end
        end
    ) 
end

local connector = {

    new     = function(parent, host, port, opts)
        if opts == nil then
            opts = {}
        end
        local self = {
            host        = host,
            port        = port,
            state       = 'init',
            is_run      = true,
            opts        = opts
        }

        setmetatable(self, { __index = parent })

        run(self)
        return self
    end,


    on_error    = function(self, message) end,
    on_connect  = function(self) end,
    io          = function(self) fiber.sleep(1) end,


    fatal = function(self, msg, ...)
        msg = string.format(msg, ...)
        self.state = 'error'
        self.message = msg
        if self.s then
            self.s:close()
            self.s = nil
        end
        self:on_error(msg)
    end
}


local remote = {
    __index = {
        new     = function(class, host, port, user, password, opts)
            local self = connector:new(host, port, opts)
            self.user = user
            self.password = password

            setmetatable(self, class)
            return self
        end,

        proto   = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
        __index = connector
    }
}

setmetatable(remote, remote)

return remote
