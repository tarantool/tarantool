local checks = require('checks')
local fio = require('fio')
local luatest = require('luatest')

local Server = require('test.luatest_helpers.server')

local Cluster = {}

local ROOT = os.environ()['SOURCEDIR'] or '.'

function Cluster:new(object)
    self:inherit(object)
    object:initialize()
    self.servers = object.servers
    self.built_servers = object.built_servers
    return object
end

function Cluster:inherit(object)
    object = object or {}
    setmetatable(object, self)
    self.__index = self
    self.servers = {}
    self.built_servers = {}
    return object
end

function Cluster:initialize()
    self.servers = {}
end

function Cluster:server(alias)
    for _, server in ipairs(self.servers) do
        if server.alias == alias then
            return server
        end
    end
    return nil
end

function Cluster:drop()
    for _, server in ipairs(self.servers) do
        if server ~= nil then
            server:stop()
            server:cleanup()
        end
    end
end

function Cluster:get_index(server)
    local index
    for i, v in ipairs(self.servers) do
        if (v.id == server) then
          index = i
        end
    end
    return index
end

function Cluster:delete_server(server)
    local idx = self:get_index(server)
    if idx == nil then
        print("Key does not exist")
    else
        table.remove(self.servers, idx)
    end
end

function Cluster:stop()
    for _, server in ipairs(self.servers) do
        if server ~= nil then
            server:stop()
        end
    end
end

function Cluster:start(opts)
    for _, server in ipairs(self.servers) do
        if not server.process then
            server:start({wait_for_readiness = false})
        end
    end

    -- The option is true by default.
    local wait_for_readiness = true
    if opts ~= nil and opts.wait_for_readiness ~= nil then
        wait_for_readiness = opts.wait_for_readiness
    end

    if wait_for_readiness then
        for _, server in ipairs(self.servers) do
            server:wait_for_readiness()
        end
    end
end

function Cluster:build_server(server_config, instance_file)
    instance_file = instance_file or 'default.lua'
    server_config = table.deepcopy(server_config)
    server_config.command = fio.pathjoin(ROOT, 'test/instances/', instance_file)
    assert(server_config.alias, 'Either replicaset.alias or server.alias must be given')
    local server = Server:new(server_config)
    table.insert(self.built_servers, server)
    return server
end

function Cluster:add_server(server)
    if self:server(server.alias) ~= nil then
        error('Alias is not provided')
    end
    table.insert(self.servers, server)
end

function Cluster:build_and_add_server(config, replicaset_config, engine)
    local server = self:build_server(config, replicaset_config, engine)
    self:add_server(server)
    return server
end


function Cluster:get_leader()
    for _, instance in ipairs(self.servers) do
        if instance:eval('return box.info.ro') == false then
            return instance
        end
    end
end

function Cluster:exec_on_leader(bootstrap_function)
    local leader = self:get_leader()
    return leader:exec(bootstrap_function)
end

function Cluster:wait_fullmesh(params)
    checks('table', {timeout = '?number', delay = '?number'})
    if not params then params = {} end
    local config = {timeout = params.timeout or 30, delay = params.delay or 0.1}

    luatest.helpers.retrying(config, function(cluster)
        for _, server1 in ipairs(cluster.servers) do
            for _, server2 in ipairs(cluster.servers) do
                if server1 ~= server2 then
                    local server1_id = server1:exec(function()
                        return box.info.id
                    end)
                    local server2_id = server2:exec(function()
                        return box.info.id
                    end)
                    if server1_id ~= server2_id then
                        server1:assert_follows_upstream(server2_id)
                    end
                end
            end
        end
    end, self)
end

return Cluster
