--- Manage connection to Tarantool replication instances via proxy.
--
-- @module custom_proxy

local checks = require('checks')
local fiber = require('fiber')
local fio = require('fio')
local socket = require('socket')
local uri = require('uri')

local log = require('luatest.log')
local utils = require('luatest.utils')
local Connection = require('luatest.replica_conn')

local TIMEOUT = 0.1 -- Increased from 1ms to 100ms to reduce CPU usage
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
    local mt = { __index = self }
    setmetatable(object, mt)
    return object
end

local function check_tarantool_version()
    if not utils.version_current_ge_than(2, 10, 1) then
        error('Proxy requires Tarantool version 2.10.1 or later')
    end
end

--- Build a proxy object.
--
-- @param object
-- @string object.client_socket_path Path to a UNIX socket where proxy will await new connections.
-- @string object.server_socket_path Path to a UNIX socket where Tarantool server is listening to.
-- @tab[opt] object.process_client Table describing how to process the client socket.
-- @tab[opt] object.process_server Table describing how to process the server socket.
-- @return Input object with proxy methods.
function Proxy:new(object)
    checks('table', self.constructor_checks)
    check_tarantool_version()
    self:inherit(object)
    object:initialize()
    return object
end

--- Initialize proxy instance.
function Proxy:initialize()
    self.connections = {}
    self.accept_new_connections = true
    self.running = false
    self.client_socket = socket('PF_UNIX', 'SOCK_STREAM', 0)
    self.lock = fiber.channel(1) -- For synchronization
    self.lock:put(true) -- Initialize unlocked
end

--- Stop accepting new connections on the client socket.
-- Join the fiber created by proxy:start() and close the client socket.
-- Also, stop all active connections.
function Proxy:stop()
    checks('table')
    
    -- Take lock to ensure atomic operation
    self.lock:get()
    
    self.running = false
    if self.worker then
        self.worker:join()
    end
    
    -- Close all connections
    for id, c in pairs(self.connections) do
        c:stop()
        self.connections[id] = nil
    end
    
    -- Close client socket
    if self.client_socket then
        self.client_socket:shutdown(socket.SHUT_RW)
        self.client_socket:close()
    end
    
    -- Release lock
    self.lock:put(true)
end

--- Pause accepting new connections and pause all active connections.
function Proxy:pause()
    checks('table')
    self.lock:get()
    self.accept_new_connections = false
    for _, c in pairs(self.connections) do
        c:pause()
    end
    self.lock:put(true)
end

--- Resume accepting new connections and resume all paused connections.
function Proxy:resume()
    checks('table')
    self.lock:get()
    for _, c in pairs(self.connections) do
        c:resume()
    end
    self.accept_new_connections = true
    self.lock:put(true)
end

--- Start accepting new connections on the client socket in a new fiber.
--
-- @tab[opt] opts
-- @bool[opt] opts.force Remove the client socket before start.
-- @return true on success, false on failure
function Proxy:start(opts)
    checks('table', {force = '?boolean'})
    
    self.lock:get()
    local success, err = pcall(function()
        if opts and opts.force then
            os.remove(self.client_socket_path)
        end

        -- Create directory for socket if needed
        local dir = fio.dirname(self.client_socket_path)
        if not fio.path.exists(dir) then
            fio.mktree(dir)
        end

        if not self.client_socket:bind('unix/', self.client_socket_path) then
            error(string.format("Failed to bind client socket: %s", self.client_socket:error()))
        end


        self.client_socket:nonblock(true)
        if not self.client_socket:listen(BACKLOG) then
            error(string.format("Failed to listen on client socket: %s", self.client_socket:error()))
        end

        self.running = true
        self.worker = fiber.new(function()
            while self.running do
                -- Use lock for thread-safe flag checking
                self.lock:get()
                local accept_connections = self.accept_new_connections
                self.lock:put(true)

                if not accept_connections then
                    fiber.sleep(TIMEOUT)
                else
                    if self.client_socket:readable(TIMEOUT) then
                        local client = self.client_socket:accept()
                        if client then
                            if pcall(client.nonblock, client, true) then
                                local conn = Connection:new({
                                    client_socket = client,
                                    server_socket_path = self.server_socket_path,
                                    process_client = self.process_client,
                                    process_server = self.process_server,
                                    on_close = function() 
                                        self.lock:get()
                                        for i, c in ipairs(self.connections) do
                                            if c == conn then
                                                table.remove(self.connections, i)
                                                break
                                            end
                                        end
                                        self.lock:put(true)
                                    end
                                })
                                self.lock:get()
                                table.insert(self.connections, conn)
                                self.lock:put(true)
                                conn:start()
                            else
                                client:close()
                            end
                        end
                    end
                end
            end

            -- Cleanup when stopping
            if self.client_socket then
                self.client_socket:shutdown(socket.SHUT_RW)
                self.client_socket:close()
            end
        end)
        self.worker:set_joinable(true)
        self.worker:name('ProxyWorker')
    end)
    
    self.lock:put(true)
    
    if not success then
        log.error(err)
        return false
    end
    return true
end

return Proxy
