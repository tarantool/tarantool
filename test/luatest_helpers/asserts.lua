local t = require('luatest')

local asserts = {}

function asserts:new(object)
    self:inherit(object)
    object:initialize()
    return object
end

function asserts:inherit(object)
    object = object or {}
    setmetatable(object, self)
    self.__index = self
    return object
end

function asserts:assert_server_follow_upstream(server, id)
    local status = server:eval(
        ('return box.info.replication[%d].upstream.status'):format(id))
    t.assert_equals(status, 'follow',
        ('%s: this server does not follow others.'):format(server.alias))
end


function asserts:wait_fullmesh(servers, wait_time)
    wait_time = wait_time or 20
    t.helpers.retrying({timeout = wait_time}, function()
        for _, server in pairs(servers) do
            for _, server2 in pairs(servers) do
                if server ~= server2 then
                    local server_id = server:eval('return box.info.id')
                    local server2_id = server2:eval('return box.info.id')
                    if server_id ~= server2_id then
                            self:assert_server_follow_upstream(server, server2_id)
                    end
                end
            end
        end
    end)
end

return asserts
