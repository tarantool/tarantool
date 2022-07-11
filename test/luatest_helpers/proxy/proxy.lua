local socket = require('socket')
local fiber = require('fiber')
local checks = require('checks')
local Connection = require('test.luatest_helpers.proxy.connection')
local log = require('log')

local TIMEOUT = 0.001
local BACKLOG = 512

local Proxy = {
    constructor_checks = {
        client_socket_path = 'string',
        server_socket_path = 'string',
        process_client = '?table',
        process_server = '?table',
    },
}

function Proxy:inherit(object)
    setmetatable(object, self)
    self.__index = self
    return object
end

function Proxy:new(object)
    checks('table', self.constructor_checks)
    self:inherit(object)
    object:initialize()
    return object
end

function Proxy:initialize()
    self.connections = {}
    self.accept_new_connections = true
    self.running = false

    self.client_socket = socket('PF_UNIX', 'SOCK_STREAM', 0)
end

function Proxy:stop()
    self.running = false
    self.worker:join()
    for _, c in pairs(self.connections) do
        c:stop()
    end
end

function Proxy:pause()
    self.accept_new_connections = false
    for _, c in pairs(self.connections) do
        c:pause()
    end
end

function Proxy:resume()
    for _, c in pairs(self.connections) do
        c:resume()
    end
    self.accept_new_connections = true
end

function Proxy:start(opts)
    checks('table', {force = '?boolean'})
    if opts ~= nil and opts.force then
        os.remove(self.client_socket_path)
    end

    if not self.client_socket:bind('unix/', self.client_socket_path) then
        log.error("Failed to bind client socket: %s", self.client_socket:error())
        return false
    end

    self.client_socket:nonblock(true)
    if not self.client_socket:listen(BACKLOG) then
        log.error("Failed to listen on client socket: %s",
            self.client_socket:error())
        return false
    end

    self.running = true
    self.worker = fiber.new(function()
        while self.running do
            if not self.accept_new_connections then
                fiber.sleep(TIMEOUT)
                goto continue
            end

            if not self.client_socket:readable(TIMEOUT) then
                goto continue
            end

            local client = self.client_socket:accept()
            if client == nil then goto continue end
            client:nonblock(true)

            local conn = Connection:new({
                client_socket = client,
                server_socket_path = self.server_socket_path,
                process_client = self.process_client,
                process_server = self.process_server,
            })
            table.insert(self.connections, conn)
            conn:start()
            ::continue::
        end

        self.client_socket:shutdown(socket.SHUT_RW)
        self.client_socket:close()
        os.remove(self.client_socket_path)
    end)
    self.worker:set_joinable(true)
    self.worker:name('ProxyWorker')

    return true
end

return Proxy
