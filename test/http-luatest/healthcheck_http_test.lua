local t = require('luatest')
local server = require('luatest.server')
local treegen = require('luatest.treegen')
local json = require('json')
local socket = require('socket')
local yaml = require('yaml')

local http_client = require('http.client').new()

local g = t.group()

local function get_free_port()
    local server = socket.tcp_server('127.0.0.1', 0, function() end)
    t.assert(server ~= nil)
    local address = server:name()
    server:close()
    if type(address) == 'table' then
        return address.port
    end
    return tonumber(tostring(address):match(':(%d+)$'))
end

local function get(cg, path, port)
    return http_client:get(
        ('http://127.0.0.1:%d%s'):format(port or cg.http_port, path),
        {no_proxy = '*'}
    )
end

local function assert_response(cg, path, status, port)
    local response = get(cg, path, port)
    t.assert_equals(response.status, status)
    if status ~= 404 then
        t.assert_equals(response.headers['content-type'], 'application/json')
        return json.decode(response.body)
    end
    return nil
end

local function reload(cg, config)
    treegen.write_file(cg.dir, 'config.yaml', yaml.encode(config))
    cg.config_changed = true
    return cg.server:eval("return require('config'):reload()")
end

g.before_all(function(cg)
    cg.http_port = get_free_port()
    cg.custom_http_port = get_free_port()
    cg.disabled_http_port = get_free_port()
    local config = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },
        iproto = {
            listen = {{uri = 'unix/:./{{ instance_name }}.iproto'}},
        },
        groups = {
            ['g-001'] = {
                replicasets = {
                    ['r-001'] = {
                        roles = {'roles.httpd'},
                        roles_cfg = {
                            ['roles.httpd'] = {
                                default = {
                                    listen = '127.0.0.1:' .. cg.http_port,
                                    health = {
                                        enabled = true,
                                    },
                                },
                                custom = {
                                    listen = '127.0.0.1:' ..
                                             cg.custom_http_port,
                                    health = {
                                        enabled = true,
                                        paths = {
                                            health = '/status',
                                            liveness = false,
                                            readiness = '/is-ready',
                                        },
                                    },
                                },
                                disabled = {
                                    listen = '127.0.0.1:' ..
                                             cg.disabled_http_port,
                                },
                            },
                        },
                        instances = {
                            ['i-001'] = {},
                        },
                    },
                },
            },
        },
    }
    cg.config = config
    cg.dir = treegen.prepare_directory({}, {})
    local config_file = treegen.write_file(
        cg.dir, 'config.yaml', yaml.encode(config)
    )
    cg.server = server:new({
        alias = 'i-001',
        config_file = config_file,
        chdir = cg.dir,
    })
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        local health = require('internal.healthcheck')
        health.remove_health_check('http-test', {if_exists = true})
        health.remove_liveness_probe('http-test', {if_exists = true})
        local httpd_role = require('roles.httpd')
        httpd_role.get_server('disabled'):delete('test-route')
        httpd_role.get_server('disabled'):delete('shadow')
        httpd_role.get_server('disabled'):delete('tarantool.healthz')
    end)
    if cg.config_changed then
        treegen.write_file(cg.dir, 'config.yaml', yaml.encode(cg.config))
        local _, err = cg.server:eval("return require('config'):reload()")
        t.assert_not(err)
        cg.config_changed = false
    end
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_healthy = function(cg)
    local health = assert_response(cg, '/healthz', 200)
    t.assert(health.liveness.verdict)
    t.assert(health.readiness.status)

    local liveness = assert_response(cg, '/livez', 200)
    t.assert(liveness.verdict)

    local readiness = assert_response(cg, '/readyz', 200)
    t.assert(readiness.status)
end

g.test_per_server_configuration = function(cg)
    local health = assert_response(
        cg, '/status', 200, cg.custom_http_port
    )
    t.assert(health.liveness.verdict)
    t.assert(health.readiness.status)
    assert_response(cg, '/livez', 404, cg.custom_http_port)
    local readiness = assert_response(
        cg, '/is-ready', 200, cg.custom_http_port
    )
    t.assert(readiness.status)

    assert_response(cg, '/healthz', 404, cg.disabled_http_port)
    assert_response(cg, '/livez', 404, cg.disabled_http_port)
    assert_response(cg, '/readyz', 404, cg.disabled_http_port)
end

g.test_not_ready = function(cg)
    cg.server:exec(function()
        local health = require('internal.healthcheck')
        t.assert(health.add_health_check('http-test', function()
            return false, 'not ready'
        end))
    end)

    local health = assert_response(cg, '/healthz', 503)
    t.assert(health.liveness.verdict)
    t.assert_not(health.readiness.status)

    local liveness = assert_response(cg, '/livez', 200)
    t.assert(liveness.verdict)

    local readiness = assert_response(cg, '/readyz', 503)
    t.assert_not(readiness.status)
    t.assert_equals(readiness.checks['http-test'].reason, 'not ready')
end

g.test_not_live = function(cg)
    cg.server:exec(function()
        t.assert(box.ctl.liveness_probe({
            name = 'http-test',
            check = function()
                return false, 'not live'
            end,
        }))
    end)

    local health = assert_response(cg, '/healthz', 503)
    t.assert_not(health.liveness.verdict)
    t.assert(health.readiness.status)

    local liveness = assert_response(cg, '/livez', 503)
    t.assert_not(liveness.verdict)
    t.assert_equals(liveness.checks['http-test'].reason, 'not live')

    local readiness = assert_response(cg, '/readyz', 200)
    t.assert(readiness.status)
end

g.test_reload_configuration = function(cg)
    local config = table.deepcopy(cg.config)
    local servers = config.groups['g-001'].replicasets['r-001'].roles_cfg
                          ['roles.httpd']
    servers.default.health.paths = {
        health = '/status',
        liveness = false,
        readiness = '/ready',
    }
    local _, err = reload(cg, config)
    t.assert_not(err)

    assert_response(cg, '/healthz', 404)
    assert_response(cg, '/livez', 404)
    assert_response(cg, '/readyz', 404)
    assert_response(cg, '/status', 200)
    assert_response(cg, '/ready', 200)
end

g.test_route_conflict = function(cg)
    cg.server:exec(function()
        local httpd = require('roles.httpd').get_server()
        t.assert_error_msg_contains(
            'route path "/health." conflicts with an HTTP health check',
            httpd.route, httpd, {
                method = 'GET',
                path = '/health.',
            }, function() end
        )
        t.assert_error_msg_contains(
            'route name "tarantool.healthz" is reserved',
            httpd.route, httpd, {
                method = 'GET',
                path = '/another-healthz',
                name = 'tarantool.healthz',
            }, function() end
        )
        t.assert_error_msg_contains(
            'route path "/health." conflicts with an HTTP health check',
            httpd.route, httpd, {
                method = 'BOGUS',
                path = '/health.',
            }, function() end
        )
    end)
end

g.test_disabled_does_not_reserve_routes = function(cg)
    cg.server:exec(function()
        local httpd = require('roles.httpd').get_server('disabled')
        httpd:route({
            method = 'GET',
            path = '/user-health',
            name = 'tarantool.healthz',
        }, function()
            return {status = 200, body = 'user health'}
        end)
    end)
    local response = get(cg, '/user-health', cg.disabled_http_port)
    t.assert_equals(response.status, 200)
    t.assert_equals(response.body, 'user health')
end

g.test_existing_pattern_route_conflict = function(cg)
    cg.server:exec(function()
        local httpd = require('roles.httpd').get_server('disabled')
        httpd:route({
            method = 'GET',
            path = '/health.',
            name = 'shadow',
        }, function()
            return {status = 200, body = 'shadow'}
        end)
    end)

    local config = table.deepcopy(cg.config)
    local servers = config.groups['g-001'].replicasets['r-001'].roles_cfg
                          ['roles.httpd']
    servers.disabled.health = {enabled = true}
    t.assert_error_msg_contains(
        'route path "/health." conflicts with an HTTP health check',
        reload, cg, config
    )
end

g.test_failed_enable_does_not_change_servers = function(cg)
    cg.server:exec(function()
        local config = require('config')
        local httpd_role = require('roles.httpd')
        local disabled = httpd_role.get_server('disabled')
        local custom = httpd_role.get_server('custom')
        local original_route = disabled.route
        local original_log_requests = custom.options.log_requests
        disabled:route({
            method = 'GET',
            path = '/health.',
            name = 'shadow',
        }, function()
            return {status = 200}
        end)

        local roles_cfg = config:get('roles_cfg')
        local conf = table.deepcopy(roles_cfg['roles.httpd'])
        conf.custom.log_requests = 'debug'
        conf.disabled.health = {enabled = true}
        t.assert_error_msg_contains(
            'route path "/health." conflicts with an HTTP health check',
            httpd_role.apply, conf
        )
        t.assert_equals(httpd_role.get_server('custom'), custom)
        t.assert_equals(custom.options.log_requests, original_log_requests)
        t.assert_equals(disabled.route, original_route)

        disabled:delete('shadow')
        httpd_role.apply(roles_cfg['roles.httpd'])
        t.assert_equals(disabled.route, original_route)
    end)
end

g.test_reload_restores_deleted_route = function(cg)
    cg.server:exec(function()
        require('roles.httpd').get_server():delete('tarantool.healthz')
    end)
    assert_response(cg, '/healthz', 404)

    local config = table.deepcopy(cg.config)
    local servers = config.groups['g-001'].replicasets['r-001'].roles_cfg
                          ['roles.httpd']
    servers.disabled.log_requests = 'debug'
    local _, err = reload(cg, config)
    t.assert_not(err)
    assert_response(cg, '/healthz', 200)
end

g.test_disable_and_enable = function(cg)
    local config = table.deepcopy(cg.config)
    local servers = config.groups['g-001'].replicasets['r-001'].roles_cfg
                          ['roles.httpd']
    servers.default.health.enabled = false
    local _, err = reload(cg, config)
    t.assert_not(err)
    assert_response(cg, '/healthz', 404)
    cg.server:exec(function()
        local httpd = require('roles.httpd').get_server()
        httpd:route({
            method = 'GET',
            path = '/temporary',
            name = 'tarantool.healthz',
        }, function()
            return {status = 200}
        end)
        httpd:delete('tarantool.healthz')
    end)

    _, err = reload(cg, cg.config)
    t.assert_not(err)
    assert_response(cg, '/healthz', 200)
end

g.test_listener_recreation = function(cg)
    local port = get_free_port()
    cg.server:exec(function()
        local httpd = require('roles.httpd').get_server('disabled')
        httpd:route({
            method = 'GET',
            path = '/health.',
            name = 'shadow',
        }, function()
            return {status = 200}
        end)
    end)
    local config = table.deepcopy(cg.config)
    local servers = config.groups['g-001'].replicasets['r-001'].roles_cfg
                          ['roles.httpd']
    servers.disabled.listen = '127.0.0.1:' .. port
    servers.disabled.health = {enabled = true}
    local _, err = reload(cg, config)
    t.assert_not(err)
    assert_response(cg, '/healthz', 200, port)
    assert_response(cg, '/livez', 200, port)
    assert_response(cg, '/readyz', 200, port)
end

g.test_failed_add_rolls_back = function(cg)
    cg.server:exec(function()
        local config = require('config')
        local httpd_role = require('roles.httpd')
        local httpd = httpd_role.get_server('disabled')
        local original_route = httpd.route
        local route_count = #httpd.routes
        local calls = 0
        local function failing_route(self, opts, handler)
            calls = calls + 1
            if calls == 2 then
                error('injected route error')
            end
            return original_route(self, opts, handler)
        end
        httpd.route = failing_route

        local roles_cfg = config:get('roles_cfg')
        local conf = table.deepcopy(roles_cfg['roles.httpd'])
        conf.disabled.health = {
            enabled = true,
            paths = {
                health = '/custom-health',
            },
        }
        local ok, err = pcall(httpd_role.apply, conf)
        local route_after = httpd.route
        httpd.route = original_route

        t.assert_not(ok)
        t.assert_str_contains(tostring(err), 'injected route error')
        t.assert_equals(route_after, failing_route)
        t.assert_equals(#httpd.routes, route_count)
        t.assert_equals(httpd.iroutes['tarantool.healthz'], nil)
        t.assert_equals(httpd.iroutes['tarantool.livez'], nil)
        t.assert_equals(httpd.iroutes['tarantool.readyz'], nil)
        httpd_role.apply(roles_cfg['roles.httpd'])
    end)
end

g.test_validate_configuration = function(cg)
    cg.server:exec(function()
        local httpd_role = require('roles.httpd')
        local function validate(health)
            httpd_role.validate({
                test = {
                    listen = 8080,
                    health = health,
                },
            })
        end

        local cases = {
            {
                health = true,
                error = 'health option of server "test" must be a table',
            },
            {
                health = {unknown = true},
                error = 'unknown health option "unknown"',
            },
            {
                health = {enabled = 'yes'},
                error = 'health.enabled option of server "test" must be a ' ..
                        'boolean',
            },
            {
                health = {paths = 'default'},
                error = 'health.paths option of server "test" must be a table',
            },
            {
                health = {paths = {unknown = '/unknown'}},
                error = 'unknown health endpoint "unknown"',
            },
            {
                health = {paths = {health = true}},
                error = 'health.paths.health option of server "test" must ' ..
                        'be a string or false',
            },
            {
                health = {paths = {health = 'healthz'}},
                error = 'health.paths.health option of server "test" must ' ..
                        'start with "/"',
            },
            {
                health = {
                    enabled = true,
                    paths = {health = '/livez/'},
                },
                error = 'same path',
            },
            {
                health = {paths = {health = '/.'}},
                error = 'must contain only letters, digits',
            },
            {
                health = {paths = {health = '/health*'}},
                error = 'must contain only letters, digits',
            },
            {
                health = {paths = {health = '/health%20check'}},
                error = 'must contain only letters, digits',
            },
            {
                health = {paths = {health = '/health/:name'}},
                error = 'must contain only letters, digits',
            },
        }
        for _, case in ipairs(cases) do
            t.assert_error_msg_contains(case.error, validate, case.health)
        end
        validate({
            enabled = true,
            paths = {health = '/health-check_1'},
        })
    end)
end
