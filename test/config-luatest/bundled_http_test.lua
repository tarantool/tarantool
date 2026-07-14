local fio = require('fio')
local t = require('luatest')
local treegen = require('luatest.treegen')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local http_client = require('http.client').new()

local g = t.group()

local ROCK_HTTP_SERVER = [[
local http_server = require('internal.http.server')
rawset(_G, 'rock_ports', {})
return {
    new = function(host, port, opts)
        table.insert(_G.rock_ports, port)
        local server = http_server.new(host, port, opts)
        server.from_rock = true
        return server
    end,
}
]]

local ROCK_ROLES_HTTPD = [[
local role = table.copy(require('internal.roles.httpd'))
local get_server = role.get_server
role.from_rock = true
rawset(_G, 'rock_role_calls', 0)
role.get_server = function(...)
    rawset(_G, 'rock_role_calls', _G.rock_role_calls + 1)
    return get_server(...)
end
return role
]]

local USER_ROLE = [[
return {
    dependencies = {'roles.httpd'},
    validate = function() end,
    apply = function()
        rawset(_G, 'user_role', {
            from_rock = require('roles.httpd').get_server().from_rock,
            http_server = pcall(require, 'http.server'),
        })
    end,
    stop = function() end,
}
]]

local function status(sock, path)
    local resp = http_client:get('http://localhost' .. path,
                                 {unix_socket = sock})
    return resp.status
end

g.before_each(function(cg)
    cg.dir = treegen.prepare_directory({}, {})
    cg.httpd_sock = fio.pathjoin(cg.dir, 'httpd.sock')
end)

local function start(cg, files, roles)
    for path, content in pairs(files) do
        treegen.write_file(cg.dir, path, content)
    end
    local builder = cbuilder:new()
        :add_instance('i-001', {})
        :set_instance_option('i-001', 'roles', roles)
        :set_instance_option('i-001', 'roles_cfg', {
            ['roles.httpd'] = {
                default = {listen = 'unix/:' .. cg.httpd_sock},
            },
        })
    local lua_path = cg.dir .. '/?.lua;' .. (os.getenv('LUA_PATH') or ';')
    cg.cluster = cluster:new(builder:config(), {env = {LUA_PATH = lua_path}})
    cg.cluster:start()
    return cg.cluster['i-001']
end

g.after_each(function(cg)
    if cg.cluster ~= nil then
        cg.cluster:stop()
        cg.cluster = nil
    end
end)

g.test_no_rock = function(cg)
    local server = start(cg, {['user_role.lua'] = USER_ROLE},
                         {'roles.httpd', 'user_role'})
    server:exec(function()
        for _, name in ipairs({'http.server', 'http.lib', 'http.sslsocket',
                               'http.codes', 'http.mime_types',
                               'http.version'}) do
            t.assert_not(pcall(require, name), name)
        end
        t.assert_type(require('internal.http.server').new, 'function')
        t.assert_is(require('roles.httpd'), require('internal.roles.httpd'))
        t.assert_equals(_G.user_role, {http_server = false})
    end)
    t.assert_equals(status(cg.httpd_sock, '/'), 404)
end

g.test_rock_http_server = function(cg)
    local server = start(cg, {
        ['http/server.lua'] = ROCK_HTTP_SERVER,
        ['user_role.lua'] = USER_ROLE,
    }, {'roles.httpd', 'user_role'})
    server:exec(function(httpd_sock)
        t.assert_is_not(require('http.server'),
                        require('internal.http.server'))
        t.assert_is(require('roles.httpd'), require('internal.roles.httpd'))
        t.assert(require('roles.httpd').get_server().from_rock)
        t.assert_equals(_G.user_role, {from_rock = true, http_server = true})
        t.assert_items_include(_G.rock_ports, {httpd_sock})
    end, {cg.httpd_sock})
    t.assert_equals(status(cg.httpd_sock, '/'), 404)
end

g.test_rock_http_server_and_role = function(cg)
    local server = start(cg, {
        ['http/server.lua'] = ROCK_HTTP_SERVER,
        ['roles/httpd.lua'] = ROCK_ROLES_HTTPD,
        ['user_role.lua'] = USER_ROLE,
    }, {'roles.httpd', 'user_role'})
    server:exec(function()
        t.assert(require('roles.httpd').from_rock)
        t.assert_gt(_G.rock_role_calls, 0)
        t.assert_equals(_G.user_role, {from_rock = true, http_server = true})
    end)
end

g.test_app_thread = function(cg)
    local config = cbuilder:new()
        :set_global_option('credentials.users.admin.password', 'secret')
        :add_instance('i-001', {})
        :set_instance_option('i-001', 'threads.groups',
                             {{name = 'app', size = 1}})
        :config()
    cg.cluster = cluster:new(config, {
        net_box_credentials = {user = 'admin', password = 'secret'},
    })
    cg.cluster:start()
    local res = cg.cluster['i-001']:eval([[
        return {(pcall(require, 'http.server')),
                (pcall(require, 'internal.http.server'))}
    ]], {}, {_thread_id = 1})
    t.assert_equals(res, {false, false})
end
