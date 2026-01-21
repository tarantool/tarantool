local network = require('internal.config.utils.network')
local textutils = require('internal.config.utils.textutils')

local has_http_server, http_server = pcall(require, 'http.server')

local httpd_role

local function get_httpd_role()
    if httpd_role == nil then
        local ok, role = pcall(require, 'roles.httpd')
        if ok then
            httpd_role = role
        else
            httpd_role = false
        end
    end
    if httpd_role == false then
        return nil
    end
    return httpd_role
end

local state = {
    servers = {},
}

local function options_equal(left, right)
    local fields = {
        'ssl_cert_file',
        'ssl_key_file',
        'ssl_ca_file',
        'ssl_ciphers',
        'ssl_password',
        'ssl_password_file',
    }
    for _, field in ipairs(fields) do
        if left[field] ~= right[field] then
            return false
        end
    end
    return true
end

local http_handlers = {
    json = function(req)
        local json_exporter = require('metrics.plugins.json')
        return req:render({text = json_exporter.export()})
    end,
    prometheus = function(...)
        local http_handler = require('metrics.plugins.prometheus').collect_http
        return http_handler(...)
    end,
}

local function wrap_handler(handler, metrics)
    if metrics ~= nil and metrics.enabled == true then
        local http_middleware = require('metrics.http_middleware')
        return http_middleware.v1(handler)
    end
    return handler
end

local function routes_equal(old, new)
    assert(type(old.metrics) == 'table')
    assert(type(new.metrics) == 'table')

    if old.format ~= new.format or
       old.metrics.enabled ~= new.metrics.enabled then
        return false
    end

    return true
end

local function disable_server(server)
    for path in pairs(server.routes or {}) do
        server.httpd:delete(path)
    end
    if server.httpd.is_run == true then
        server.httpd:stop()
    end
end

local function post_apply(config)
    local configdata = config._configdata
    local http = configdata:get('metrics.export.http',
                                {use_default = true}) or {}
    if next(http) == nil then
        for key, server in pairs(state.servers) do
            disable_server(server)
            state.servers[key] = nil
        end
        return
    end

    if not has_http_server then
        local log = require('internal.config.utils.log')
        log.warn('metrics.export: module http.server is not available, ' ..
                 'HTTP metrics export is disabled')
        return
    end

    local listen_servers_to_start = {}
    local applied_servers = {}

    for _, node in ipairs(http) do
        if #(node.endpoints or {}) > 0 then
            local target
            if node.server ~= nil then
                target = {
                    value = ('httpd_%s'):format(node.server),
                    httpd_name = node.server,
                }
            elseif node.listen ~= nil then
                local host, port, err = network.parse_listen(node.listen)
                if err ~= nil then
                    error(('failed to parse metrics.export.http.listen: %s'):
                        format(err), 0)
                end
                target = {
                    value = ('listen_%s:%s'):format(host, tostring(port)),
                    host = host,
                    port = port,
                }
            else
                local role = get_httpd_role()
                if role == nil then
                    error('metrics.export.http.server requires roles.httpd',
                        0)
                end
                target = {
                    value = ('httpd_%s'):format(role.DEFAULT_SERVER_NAME),
                    httpd_name = role.DEFAULT_SERVER_NAME,
                }
            end

            applied_servers[target.value] = true

            local server = state.servers[target.value]
            local options = nil
            if target.httpd_name == nil then
                options = {
                    ssl_cert_file = node.ssl_cert_file,
                    ssl_key_file = node.ssl_key_file,
                    ssl_ca_file = node.ssl_ca_file,
                    ssl_ciphers = node.ssl_ciphers,
                    ssl_password = node.ssl_password,
                    ssl_password_file = node.ssl_password_file,
                }
                if server ~= nil and
                   not options_equal(server.options, options) then
                    disable_server(server)
                    state.servers[target.value] = nil
                    server = nil
                end
            end

            if server == nil then
                local httpd
                if target.httpd_name == nil then
                    httpd = http_server.new(target.host, target.port, options)
                else
                    local role = get_httpd_role()
                    if role == nil then
                        error('metrics.export.http.server ' ..
                             'requires roles.httpd', 0)
                    end
                    httpd = role.get_server(target.httpd_name)
                    if httpd == nil then
                        error(('failed to get server by name %q, check ' ..
                               'that roles.httpd is already applied'):format(
                            target.httpd_name), 0)
                    end
                end
                server = {
                    httpd = httpd,
                    routes = {},
                    options = options,
                    httpd_name = target.httpd_name,
                }
                state.servers[target.value] = server
                if target.httpd_name == nil then
                    table.insert(listen_servers_to_start, server)
                end
            elseif target.httpd_name ~= nil then
                local role = get_httpd_role()
                if role == nil then
                    error('metrics.export.http.server requires roles.httpd', 0)
                end
                server.httpd = role.get_server(target.httpd_name)
                if server.httpd == nil then
                    error(('failed to get server by name %q, check that ' ..
                           'roles.httpd is already applied'):format(
                        target.httpd_name), 0)
                end
            end

            local new_routes = {}
            for _, endpoint in ipairs(node.endpoints) do
                local path = textutils.remove_side_slashes(endpoint.path)
                new_routes[path] = {
                    format = endpoint.format,
                    metrics = endpoint.metrics or {},
                }
            end

            for path, route in pairs(server.routes) do
                if new_routes[path] == nil or
                   not routes_equal(route, new_routes[path]) then
                    server.httpd:delete(path)
                    server.routes[path] = nil
                end
            end

            for path, endpoint in pairs(new_routes) do
                if server.routes[path] == nil or
                   server.httpd.iroutes[path] == nil then
                    server.httpd:route({
                        method = 'GET',
                        path = path,
                        name = path,
                    }, wrap_handler(http_handlers[endpoint.format],
                                    endpoint.metrics))
                end
            end

            server.routes = new_routes
        end
    end

    for key, server in pairs(state.servers) do
        if applied_servers[key] == nil then
            disable_server(server)
            state.servers[key] = nil
        end
    end

    for _, server in ipairs(listen_servers_to_start) do
        server.httpd:start()
    end
end

return {
    name = 'metrics.export',
    apply = function() end,

    -- This module depends on roles.httpd and must be applied after it.
    -- Existing HTTP servers from roles.httpd can be reused for metrics export.
    post_apply = post_apply,
}
