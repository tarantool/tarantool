local httpd_role = require('internal.httpd_role')
local json = require('json')
local log = require('log')
local uri = require('uri')

local M = {
    DEFAULT_SERVER_NAME = httpd_role.DEFAULT_SERVER_NAME,
    get_server = httpd_role.get_server,
}

local server_states = setmetatable({}, {__mode = 'k'})

local function response(info, is_healthy)
    return {
        status = is_healthy and 200 or 503,
        headers = {
            ['content-type'] = 'application/json',
        },
        body = json.encode(info),
    }
end

local function healthz()
    local info = box.info.health
    local is_healthy = info.liveness.verdict and info.readiness.status
    return response(info, is_healthy)
end

local function livez()
    local info = box.info.health.liveness
    return response(info, info.verdict)
end

local function readyz()
    local info = box.info.health.readiness
    return response(info, info.status)
end

local endpoints = {
    health = {
        default_path = '/healthz',
        name = 'tarantool.healthz',
        handler = healthz,
    },
    liveness = {
        default_path = '/livez',
        name = 'tarantool.livez',
        handler = livez,
    },
    readiness = {
        default_path = '/readyz',
        name = 'tarantool.readyz',
        handler = readyz,
    },
}

local function request_path(path)
    if path:sub(-1) ~= '/' then
        path = path .. '/'
    end
    return path
end

local function path_matches(pattern, path)
    local ok, match = pcall(string.match, request_path(path), pattern)
    return ok and match ~= nil
end

local function endpoint_paths(health)
    local configured_paths = health.paths or {}
    local paths = {}
    for key, endpoint in pairs(endpoints) do
        local path = configured_paths[key]
        if path == nil then
            path = endpoint.default_path
        end
        if path ~= false then
            paths[key] = path
        end
    end
    return paths
end

local function desired_paths(node)
    local health = node.health
    if health == nil or health.enabled ~= true then
        return {}
    end
    return endpoint_paths(health)
end

local function validate_health(server_name, node)
    local health = node.health
    if health == nil then
        return
    end
    if type(health) ~= 'table' then
        error(('health option of server %q must be a table'):format(
            server_name
        ))
    end
    for key in pairs(health) do
        if key ~= 'enabled' and key ~= 'paths' then
            error(('unknown health option %q of server %q'):format(
                key, server_name
            ))
        end
    end
    if health.enabled ~= nil and type(health.enabled) ~= 'boolean' then
        error(('health.enabled option of server %q must be a boolean'):format(
            server_name
        ))
    end

    local paths = health.paths
    if paths == nil then
        return
    end
    if type(paths) ~= 'table' then
        error(('health.paths option of server %q must be a table'):format(
            server_name
        ))
    end

    for key, path in pairs(paths) do
        if endpoints[key] == nil then
            error(('unknown health endpoint %q of server %q'):format(
                key, server_name
            ))
        end
        if path ~= false and type(path) ~= 'string' then
            error(('health.paths.%s option of server %q must be a string ' ..
                   'or false'):format(key, server_name))
        end
        if type(path) == 'string' then
            if path:sub(1, 1) ~= '/' then
                error(('health.paths.%s option of server %q must start ' ..
                       'with "/"'):format(key, server_name))
            end
            if path:find('[^A-Za-z0-9_/%-]') ~= nil then
                error(('health.paths.%s option of server %q must contain ' ..
                       'only letters, digits, "/", "_" and "-"'):format(
                           key, server_name
                       ))
            end
        end
    end

    local used_paths = {}
    for key, path in pairs(endpoint_paths(health)) do
        path = request_path(path)
        if used_paths[path] ~= nil then
            error(('health endpoints %q and %q of server %q have the ' ..
                   'same path'):format(used_paths[path], key,
                                       server_name))
        end
        used_paths[path] = key
    end
end

local function validate_health_config(conf)
    for name, node in pairs(conf or {}) do
        validate_health(name, node)
    end
end

function M.validate(conf)
    httpd_role.validate(conf)
    validate_health_config(conf)
end

local function route_conflicts(route, paths)
    if route.method ~= 'GET' and route.method ~= 'ANY' then
        return false
    end
    for _, path in pairs(paths) do
        if path_matches(route.match, path) then
            return true
        end
    end
    return false
end

local function routes_conflict(server, state, paths)
    local active_names = state ~= nil and state.active_names or {}
    local desired_names = {}
    for key in pairs(paths) do
        desired_names[endpoints[key].name] = true
    end
    for _, route in ipairs(server.routes) do
        if not active_names[route.name] then
            if desired_names[route.name] then
                error(('route name %q is reserved for an HTTP health ' ..
                       'check'):format(route.name))
            end
            if route_conflicts(route, paths) then
                error(('route path %q conflicts with an HTTP health ' ..
                       'check'):format(route.path))
            end
        end
    end
end

local function get_server_state(server)
    local state = server_states[server]
    if state ~= nil then
        return state
    end

    state = {
        active_names = {},
        configured_paths = {},
        route = server.route,
    }
    server_states[server] = state
    server.route = function(self, opts, handler)
        if type(opts) == 'table' and state.active_names[opts.name] then
            error(('route name %q is reserved for an HTTP health ' ..
                   'check'):format(opts.name))
        end
        local route_count = #self.routes
        local result = state.route(self, opts, handler)
        local route = self.routes[route_count + 1]
        if route ~= nil and route_conflicts(route,
                                            state.configured_paths) then
            if route.name ~= nil then
                self:delete(route.name)
            else
                table.remove(self.routes)
            end
            error(('route path %q conflicts with an HTTP health check'):format(
                route.path
            ))
        end
        return result
    end
    return state
end

local function routes_are_present(server, state)
    for key, path in pairs(state.configured_paths) do
        local name = endpoints[key].name
        local index = server.iroutes[name]
        local route = index ~= nil and server.routes[index] or nil
        if route == nil or route.method ~= 'GET' or route.path ~= path or
           route.sub ~= endpoints[key].handler then
            return false
        end
    end
    return true
end

local function drop_server_state(server, state)
    server.route = state.route
    server_states[server] = nil
end

local function apply_health(server, node)
    local paths = desired_paths(node)
    local state = server_states[server]
    if state == nil and next(paths) == nil then
        return
    end
    state = state or get_server_state(server)
    if next(paths) == nil and next(state.configured_paths) == nil then
        drop_server_state(server, state)
        return
    end
    local unchanged = true
    for key, path in pairs(paths) do
        if state.configured_paths[key] ~= path then
            unchanged = false
            break
        end
    end
    if unchanged then
        for key, path in pairs(state.configured_paths) do
            if paths[key] ~= path then
                unchanged = false
                break
            end
        end
    end
    if unchanged and routes_are_present(server, state) then
        return
    end

    local staged = {}
    local route_count = #server.routes
    local ok, err = pcall(function()
        for key, path in pairs(paths) do
            local endpoint = endpoints[key]
            state.route(server, {
                method = 'GET',
                path = path,
            }, endpoint.handler)
            staged[#staged + 1] = {
                key = key,
                route = server.routes[#server.routes],
            }
        end
    end)
    if not ok then
        while #server.routes > route_count do
            table.remove(server.routes)
        end
        if next(state.configured_paths) == nil then
            drop_server_state(server, state)
        end
        error(err, 0)
    end

    for name in pairs(state.active_names) do
        server:delete(name)
    end

    local active_names = {}
    local configured_paths = {}
    local first_index = #server.routes - #staged
    for i, item in ipairs(staged) do
        local index = first_index + i
        local endpoint = endpoints[item.key]
        item.route.name = endpoint.name
        server.iroutes[endpoint.name] = index
        active_names[endpoint.name] = true
        configured_paths[item.key] = paths[item.key]
    end
    state.active_names = active_names
    state.configured_paths = configured_paths
    if next(paths) == nil then
        drop_server_state(server, state)
    end
end

local function listen_address(listen)
    if type(listen) == 'number' then
        return '0.0.0.0', listen
    end
    local parsed = uri.parse(listen)
    if parsed.scheme == 'unix' then
        parsed.unix = parsed.path
    end
    if parsed.unix ~= nil then
        return 'unix/', parsed.unix
    end
    local host = parsed.host or parsed.ipv4 or parsed.ipv6 or '0.0.0.0'
    return host, tonumber(parsed.service)
end

local function server_will_change(server, node)
    local host, port = listen_address(node.listen)
    if server.host ~= host or server.port ~= port then
        return true
    end
    local log_requests = node.log_requests
    if log_requests ~= nil then
        log_requests = log[log_requests:lower()]
    end
    local params = {
        log_requests = log_requests,
        ssl_cert_file = node.ssl_cert_file,
        ssl_key_file = node.ssl_key_file,
        ssl_password = node.ssl_password,
        ssl_password_file = node.ssl_password_file,
        ssl_ca_file = node.ssl_ca_file,
        ssl_ciphers = node.ssl_ciphers,
        ssl_verify_client = node.ssl_verify_client,
    }
    for key, value in pairs(params) do
        if server.options[key] ~= value then
            return true
        end
    end
    return false
end

local function preflight(conf)
    for name, node in pairs(conf or {}) do
        local server = httpd_role.get_server(name)
        if server ~= nil and not server_will_change(server, node) then
            routes_conflict(server, server_states[server],
                            desired_paths(node))
        end
    end
end

function M.apply(conf)
    M.validate(conf)
    preflight(conf)
    httpd_role.apply(conf)
    for name, node in pairs(conf or {}) do
        apply_health(httpd_role.get_server(name), node)
    end
end

function M.stop()
    httpd_role.stop()
    server_states = setmetatable({}, {__mode = 'k'})
end

return M
