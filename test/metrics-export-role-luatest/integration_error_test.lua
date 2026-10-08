local fio = require('fio')
local socket = require('socket')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

g.after_each(function(cg)
    if cg.cluster ~= nil then
        cg.cluster:stop()
    end
end)

local function get_free_port()
    local server = socket.tcp_server('127.0.0.1', 0, function() end)
    t.assert(server ~= nil)
    local addr = server:name()
    server:close()
    if type(addr) == 'table' then
        return addr.port
    end
    return tonumber(tostring(addr):match(':(%d+)$'))
end

g.test_incorrect_httpd_sequence = function(cg)
    local log_path = fio.pathjoin(fio.tempdir(), 'log.txt')
    local config = cbuilder:new()
        :set_global_option('log', {
            to = 'file',
            file = log_path,
        })
        :set_global_option('roles', {
            'roles.metrics-export',
            'roles.httpd',
        })
        :set_global_option('roles_cfg', {
            ['roles.metrics-export'] = {
                http = {{
                    server = 'default',
                    endpoints = {{
                        path = '/metrics',
                        format = 'prometheus',
                    }},
                }},
            },
            ['roles.httpd'] = {
                default = {listen = get_free_port()},
            },
        })
        :add_instance('i-001', {})
        :config()

    cg.cluster = cluster:new(config)
    t.assert_error(cg.cluster.start, cg.cluster)

    local file = fio.open(log_path, {'O_RDONLY'})
    t.assert(file ~= nil)
    local log = file:read()
    file:close()
    t.assert_str_contains(log, 'failed to get server by name "default"')
end
