local t = require('luatest')
local socket = require('socket')
local json = require('json')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local fio = require('fio')

local http_client = require('http.client').new()

local g = t.group()

local function http_get(uri, opts)
    local request_opts = {no_proxy = '*'}
    if opts ~= nil then
        for k, v in pairs(opts) do
            request_opts[k] = v
        end
    end
    return http_client:get(uri, request_opts)
end

local function get_free_port()
    local srv = socket.tcp_server('127.0.0.1', 0, function() end)
    t.assert(srv ~= nil)
    local addr = srv:name()
    srv:close()
    if type(addr) == 'table' then
        return addr.port
    end
    return tonumber(tostring(addr):match(':(%d+)$'))
end

local function register_metric(srv)
    srv:exec(function()
        local metrics = require('metrics')
        local counter = metrics.counter('some_counter')
        counter:inc(1, {label = 'ANY'})
    end)
end

local function assert_none(uri)
    t.helpers.retrying({timeout = 2}, function()
        local response = http_get(uri)
        t.assert_not_equals(response.status, 200)
        t.assert_not(response.body)
    end)
end

local function assert_json(uri)
    t.helpers.retrying({timeout = 2}, function()
        local response = http_get(uri)
        t.assert(response.body)

        local decoded = json.decode(response.body)
        for _, node in ipairs(decoded) do
            node.timestamp = nil
        end
        t.assert_equals(decoded, {
            {
                label_pairs = {alias = "i-001", label = "ANY"},
                metric_name = "some_counter",
                value = 1,
            },
        })
    end)
end

local function assert_prometheus(uri)
    t.helpers.retrying({timeout = 2}, function()
        local response = http_get(uri)
        t.assert(response.body)

        local expected_prometheus =
            '# HELP some_counter \n' ..
            '# TYPE some_counter counter\n' ..
            'some_counter{label="ANY",alias="i-001"} 1\n'
        t.assert_equals(response.body, expected_prometheus)
    end)
end

local base_config = cbuilder:new()
    :use_group('g-001')
    :use_replicaset('r-001')
    :add_instance('i-001', {
        database = {
            mode = 'rw',
        },
    })
    :config()

g.test_http_export_listen_smoke = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            include = {'some_counter'},
            export = {
                http = {
                    {
                        listen = listen,
                        endpoints = {
                            { path = '/metrics/json', format = 'json' },
                            { path = '/metrics/prom', format = 'prometheus' },
                        },
                    },
                },
            },
        })
        :config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    assert_json(('http://127.0.0.1:%d/metrics/json'):format(port))
    assert_prometheus(('http://127.0.0.1:%d/metrics/prom'):format(port))
    assert_none(('http://127.0.0.1:%d/metrics/absent'):format(port))
end

g.test_http_export_reload_add_endpoint = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            include = {'some_counter'},
            export = {
                http = {
                    {
                        listen = listen,
                        endpoints = {
                            { path = '/metrics/one', format = 'json' },
                        },
                    },
                },
            },
        }):config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    assert_json(('http://127.0.0.1:%d/metrics/one'):format(port))
    assert_none(('http://127.0.0.1:%d/metrics/two'):format(port))

    c:modify_config():set_instance_option('i-001', 'metrics', {
        include = {'some_counter'},
        export = {
            http = {
                {
                    listen = listen,
                    endpoints = {
                        { path = '/metrics/one', format = 'json' },
                        { path = '/metrics/two', format = 'prometheus' },
                    },
                },
            },
        },
    })
    c:apply_config_changes()
    c:reload()

    assert_json(('http://127.0.0.1:%d/metrics/one'):format(port))
    assert_prometheus(('http://127.0.0.1:%d/metrics/two'):format(port))
end

g.test_http_export_disable_when_http_is_empty = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            include = {'some_counter'},
            export = {
                http = {
                    {
                        listen = listen,
                        endpoints = {
                            { path = '/metrics/one', format = 'json' },
                        },
                    },
                },
            },
        }):config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    local uri = ('http://127.0.0.1:%d/metrics/one'):format(port)
    assert_json(uri)

    c:modify_config():set_instance_option('i-001', 'metrics', {
        include = {'some_counter'},
        export = {
            http = {},
        },
    })
    c:apply_config_changes()
    c:reload()

    assert_none(uri)
end

g.test_http_export_stop_disables_endpoints = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)
    local uri = ('http://127.0.0.1:%d/metrics/one'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            include = {'some_counter'},
            export = {
                http = {
                    {
                        listen = listen,
                        endpoints = {
                            { path = '/metrics/one', format = 'json' },
                        },
                    },
                },
            },
        }):config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    assert_json(uri)

    c:stop()

    assert_none(uri)
end

g.test_http_export_default_httpd_server = function()
    local httpd_port = get_free_port()

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'roles', { 'roles.httpd' })
        :set_instance_option('i-001', 'roles_cfg', {
            ['roles.httpd'] = {
                default = { listen = httpd_port },
            },
        })
        :set_instance_option('i-001', 'metrics', {
            include = {'some_counter'},
            export = {
                http = {
                    {
                        endpoints = {
                            { path = '/metrics/one', format = 'json' },
                        },
                    },
                },
            },
        })
        :config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    assert_json(('http://127.0.0.1:%d/metrics/one'):format(httpd_port))
end

g.test_http_export_server_requires_roles_httpd = function()
    local log_path = fio.pathjoin(fio.tempdir(), 'log.txt')

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'log', {
            to = 'file',
            file = log_path,
        })
        :set_instance_option('i-001', 'metrics', {
            include = {'some_counter'},
            export = {
                http = {
                    {
                        server = 'additional',
                        endpoints = {
                            { path = '/metrics/one', format = 'json' },
                        },
                    },
                },
            },
        }):config()

    local c = cluster:new(config)

    t.assert_error(c.start, c)

    local fh = fio.open(log_path, { 'O_RDONLY' })
    t.assert(fh ~= nil)
    local data = fh:read()
    fh:close()

    t.assert_str_contains(data, 'there is no configuration for httpd role')
end
