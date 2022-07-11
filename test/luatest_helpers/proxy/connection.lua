local checks = require('checks')
local fiber = require('fiber')
local socket = require('socket')

local TIMEOUT = 0.001

local Connection = {
    constructor_checks = {
        client_socket = '?table',
        server_socket_path = 'string',
        process_client = '?table',
        process_server = '?table',
    },
}

function Connection:inherit(object)
    setmetatable(object, self)
    self.__index = self
    return object
end

function Connection:new(object)
    checks('table', self.constructor_checks)
    self:inherit(object)
    object:initialize()
    return object
end

function Connection:initialize()
    self.running = false
    self.client_connected = true
    self.server_connected = false
    self.client_fiber = nil
    self.server_fiber = nil

    if self.process_client == nil then
        self.process_client = {
            pre = nil,
            func = self.forward_to_server,
            post = self.close_client_socket,
        }
    end

    if self.process_server == nil then
        self.process_server = {
            pre = nil,
            func = self.forward_to_client,
            post = self.close_server_socket,
        }
    end

    self:connect_server_socket()
end

function Connection:connect_server_socket()
    self.server_socket = socket('PF_UNIX', 'SOCK_STREAM', 0)
    if self.server_socket:sysconnect('unix/',self.server_socket_path) == false
    then
        self.server_socket:close()
        self.server_socket = nil
        return
    end
    self.server_socket:nonblock(true)
    self.server_connected = true

    self.server_fiber = self:process_socket(self.server_socket,
                                            self.process_server)
end

function Connection:process_socket(sock, process)
    local f = fiber.new(function()
        if process.pre ~= nil then process.pre(self) end

        while sock:peer() do
            if not self.running then
                fiber.sleep(TIMEOUT)
            elseif sock:readable(TIMEOUT) then
                local request = sock:recv()
                if request == nil or #request == 0 then break end
                if process.func ~= nil then process.func(self, request) end
            end
        end

        if process.post ~= nil then process.post(self) end
    end)
    f:set_joinable(true)
    f:name('ProxyConnectionIO')
    return f
end

function Connection:start()
    self.running = true
    if self.client_fiber == nil or self.client_fiber:status() == 'dead' then
        self.client_fiber = self:process_socket(self.client_socket,
                                                self.process_client)
    end
end

function Connection:pause()
    self.running = false
end

function Connection:resume()
    self.running = true
end

function Connection:stop()
    self:close_client_socket()
    self:close_server_socket()
end

function Connection:forward_to_server(data)
    if not self.server_connected then
        self:connect_server_socket()
    end
    if self.server_connected and self.server_socket:writable() then
        self.server_socket:write(data)
    end
end

function Connection:forward_to_client(data)
    if self.client_connected and self.client_socket:writable() then
        self.client_socket:write(data)
    end
end

function Connection:close_server_socket()
    if self.server_connected then
        self.server_socket:shutdown(socket.SHUT_RW)
        self.server_socket:close()
        self.server_connected = false
        self.server_fiber:join()
    end
end

function Connection:close_client_socket()
    if self.client_connected then
        self.client_socket:shutdown(socket.SHUT_RW)
        self.client_socket:close()
        self.client_connected = false
        self.client_fiber:join()
    end
end

return Connection
