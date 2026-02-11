local t = require('luatest')
local socket = require('socket')
local json = require('json')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local fio = require('fio')

local http_client = require('http.client').new()

local g = t.group()

local SSL_DIR = fio.abspath(fio.pathjoin('third_party', 'metrics-export-role',
                                         'test', 'ssl_data'))

local SSL = {
    ca_crt = fio.pathjoin(SSL_DIR, 'ca.crt'),

    server_crt = fio.pathjoin(SSL_DIR, 'server.crt'),
    server_key = fio.pathjoin(SSL_DIR, 'server.key'),

    server_enc_key = fio.pathjoin(SSL_DIR, 'server.enc.key'),
    passwd_file = fio.pathjoin(SSL_DIR, 'passwd'),

    client_crt = fio.pathjoin(SSL_DIR, 'client.crt'),
    client_key = fio.pathjoin(SSL_DIR, 'client.key'),
}

local function http_get(uri, opts)
    local request_opts = {no_proxy = '*'}
    if opts ~= nil then
        for k, v in pairs(opts) do
            request_opts[k] = v
        end
    end
    return http_client:get(uri, request_opts)
end

g.before_all(function()
    local ok, _ = pcall(require, 'http.server')
    t.skip_if(not ok, 'http.server module is not available')
    for ssl_file, path in pairs(SSL) do
        t.assert(fio.path.exists(path), ssl_file)
    end
end)

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

local function assert_none(uri, tls_opts)
    t.helpers.retrying({timeout = 2}, function()
        local response = http_get(uri, tls_opts)
        t.assert_not_equals(response.status, 200)
        t.assert_not(response.body)
    end)
end

local function assert_json(uri, tls_opts)
    t.helpers.retrying({timeout = 2}, function()
        local response = http_get(uri, tls_opts)
        t.assert(response.body)

        local data = response.body
        local decoded = json.decode(data)
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

local function assert_prometheus(uri, tls_opts)
    t.helpers.retrying({timeout = 2}, function()
        local response = http_get(uri, tls_opts)
        t.assert(response.body)

        local data = response.body
        local expected_prometheus =
            '# HELP some_counter \n' ..
            '# TYPE some_counter counter\n' ..
            'some_counter{label="ANY",alias="i-001"} 1\n'
        t.assert_equals(data, expected_prometheus)
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

g.test_http_export_listen_mixed_endpoints = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
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

g.test_http_export_two_listen_servers_isolated = function()
    local port1 = get_free_port()
    local port2 = get_free_port()
    t.assert_not_equals(port1, nil)
    t.assert_not_equals(port2, nil)
    t.assert_not_equals(port1, port2)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = ('127.0.0.1:%d'):format(port1),
                        endpoints = {
                            { path = '/s1/json', format = 'json' },
                            { path = '/s1/prom', format = 'prometheus' },
                        },
                    },
                    {
                        listen = ('127.0.0.1:%d'):format(port2),
                        endpoints = {
                            { path = '/s2/json', format = 'json' },
                            { path = '/s2/prom', format = 'prometheus' },
                        },
                    },
                },
            },
        })
        :config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    assert_json(('http://127.0.0.1:%d/s1/json'):format(port1))
    assert_prometheus(('http://127.0.0.1:%d/s1/prom'):format(port1))
    assert_json(('http://127.0.0.1:%d/s2/json'):format(port2))
    assert_prometheus(('http://127.0.0.1:%d/s2/prom'):format(port2))

    assert_none(('http://127.0.0.1:%d/s2/json'):format(port1))
    assert_none(('http://127.0.0.1:%d/s2/prom'):format(port1))
    assert_none(('http://127.0.0.1:%d/s1/json'):format(port2))
    assert_none(('http://127.0.0.1:%d/s1/prom'):format(port2))
end

g.test_http_export_reapply_replace_endpoint_format = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
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

    c:modify_config():set_instance_option('i-001', 'metrics', {
        exclude = { 'all' },
        export = {
            http = {
                {
                    listen = listen,
                    endpoints = {
                        { path = '/metrics/one', format = 'prometheus' },
                    },
                },
            },
        },
    })
    c:apply_config_changes()
    c:reload()

    assert_prometheus(('http://127.0.0.1:%d/metrics/one'):format(port))
end

g.test_http_export_reapply_delete_endpoint = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
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
        }):config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    assert_json(('http://127.0.0.1:%d/metrics/one'):format(port))
    assert_prometheus(('http://127.0.0.1:%d/metrics/two'):format(port))

    c:modify_config():set_instance_option('i-001', 'metrics', {
        exclude = { 'all' },
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
    })
    c:apply_config_changes()
    c:reload()

    assert_json(('http://127.0.0.1:%d/metrics/one'):format(port))
    assert_none(('http://127.0.0.1:%d/metrics/two'):format(port))
end

g.test_http_export_reapply_add_endpoint = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
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
        exclude = { 'all' },
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

g.test_http_export_disable_all_when_http_is_empty = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
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
        exclude = { 'all' },
        export = {
            http = {},
        },
    })
    c:apply_config_changes()
    c:reload()

    assert_none(uri)
end

g.test_http_export_disable_one_server_keep_another = function()
    local port1 = get_free_port()
    local port2 = get_free_port()
    t.assert_not_equals(port1, port2)

    local listen1 = ('127.0.0.1:%d'):format(port1)
    local listen2 = ('127.0.0.1:%d'):format(port2)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = listen1,
                        endpoints = {
                            { path = '/s1/metrics', format = 'json' },
                        },
                    },
                    {
                        listen = listen2,
                        endpoints = {
                            { path = '/s2/metrics', format = 'prometheus' },
                        },
                    },
                },
            },
        }):config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    local uri1 = ('http://127.0.0.1:%d/s1/metrics'):format(port1)
    local uri2 = ('http://127.0.0.1:%d/s2/metrics'):format(port2)

    assert_json(uri1)
    assert_prometheus(uri2)

    c:modify_config():set_instance_option('i-001', 'metrics', {
        exclude = { 'all' },
        export = {
            http = {
                {
                    listen = listen2,
                    endpoints = {
                        { path = '/s2/metrics', format = 'prometheus' },
                    },
                },
            },
        },
    })
    c:apply_config_changes()
    c:reload()

    assert_none(uri1)
    assert_prometheus(uri2)
end

g.test_http_export_endpoint_path_side_slashes = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = listen,
                        endpoints = {
                            { path = '/endpoint', format = 'json' },
                            { path = '/endpoint/2/', format = 'json' },
                        },
                    },
                },
            },
        }):config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    assert_json(('http://127.0.0.1:%d/endpoint'):format(port))
    assert_json(('http://127.0.0.1:%d/endpoint/2'):format(port))
end

g.test_http_export_listen_global_addr = function()
    local port = get_free_port()
    local listen = ('0.0.0.0:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = listen,
                        endpoints = {
                            { path = '/metrics/one', format = 'prometheus' },
                        },
                    },
                },
            },
        }):config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    assert_prometheus(('http://127.0.0.1:%d/metrics/one'):format(port))
end

g.test_http_export_listen_tls_basic = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = listen,
                        ssl_key_file = SSL.server_key,
                        ssl_cert_file = SSL.server_crt,
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

    assert_json(('https://localhost:%d/metrics/one'):format(port), {
        ca_file = SSL.ca_crt,
    })
end

g.test_http_export_listen_tls_encrypted_key_password_file = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = listen,
                        ssl_key_file = SSL.server_enc_key,
                        ssl_cert_file = SSL.server_crt,
                        ssl_password_file = SSL.passwd_file,
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

    assert_json(('https://localhost:%d/metrics/one'):format(port), {
        ca_file = SSL.ca_crt,
    })
end

g.test_http_export_listen_tls_encrypted_key_password = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = listen,
                        ssl_key_file = SSL.server_enc_key,
                        ssl_cert_file = SSL.server_crt,
                        ssl_password = '1q2w3e',
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

    assert_json(('https://localhost:%d/metrics/one'):format(port), {
        ca_file = SSL.ca_crt,
    })
end

g.test_http_export_listen_tls_with_ca_and_client_cert = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = listen,
                        ssl_key_file = SSL.server_key,
                        ssl_cert_file = SSL.server_crt,
                        ssl_ca_file = SSL.ca_crt,
                        ssl_ciphers = 'ECDHE-RSA-AES256-GCM-SHA384',
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

    assert_json(('https://localhost:%d/metrics/one'):format(port), {
        ca_file = SSL.ca_crt,
        ssl_cert = SSL.client_crt,
        ssl_key = SSL.client_key,
    })
end

g.test_http_export_reapply_listen_tls_options_change = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)
    local uri = ('https://localhost:%d/metrics/one'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = listen,
                        ssl_key_file = SSL.server_key,
                        ssl_cert_file = SSL.server_crt,
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

    -- initial TLS works
    assert_json(uri, { ca_file = SSL.ca_crt })

    -- change TLS options -> server must be recreated
    c:modify_config():set_instance_option('i-001', 'metrics', {
        exclude = { 'all' },
        export = {
            http = {
                {
                    listen = listen,
                    ssl_key_file = SSL.server_key,
                    ssl_cert_file = SSL.server_crt,
                    ssl_ciphers = 'ECDHE-RSA-AES256-GCM-SHA384',
                    endpoints = {
                        { path = '/metrics/one', format = 'json' },
                    },
                },
            },
        },
    })
    c:apply_config_changes()
    c:reload()

    -- TLS with the same CA must still work after recreate
    assert_json(uri, { ca_file = SSL.ca_crt })
end

g.test_http_export_listen_and_httpd_same_name_no_state_collision = function()
    local listen_port = get_free_port()
    local httpd_port = get_free_port()
    t.assert_not_equals(listen_port, httpd_port)

    local listen = ('127.0.0.1:%d'):format(listen_port)
    local httpd_name = listen

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'roles', { 'roles.httpd' })
        :set_instance_option('i-001', 'roles_cfg', {
            ['roles.httpd'] = {
                [httpd_name] = { listen = httpd_port },
            },
        })
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
            export = {
                http = {
                    {
                        listen = listen,
                        endpoints = {
                            { path = '/listen/metrics', format = 'json' },
                        },
                    },
                    {
                        server = httpd_name,
                        endpoints = {
                            { path = '/httpd/metrics', format = 'prometheus' },
                        },
                    },
                },
            },
        })
        :config()

    local c = cluster:new(config)
    c:start()
    register_metric(c['i-001'])

    assert_json(('http://127.0.0.1:%d/listen/metrics'):format(listen_port))
    assert_prometheus(('http://127.0.0.1:%d/httpd/metrics'):format(httpd_port))
end

g.test_http_export_default_httpd_server_no_listen_and_no_server = function()
    local httpd_port = get_free_port()

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'roles', { 'roles.httpd' })
        :set_instance_option('i-001', 'roles_cfg', {
            ['roles.httpd'] = {
                default = { listen = httpd_port },
            },
        })
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
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
            exclude = { 'all' },
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

    local exp_msg = 'there is no configuration for httpd role'

    local fh = fio.open(log_path, { 'O_RDONLY' })
    t.assert(fh ~= nil)
    local data = fh:read()
    fh:close()

    t.assert_str_contains(data, exp_msg)
end

g.test_http_export_stop_disables_endpoints = function()
    local port = get_free_port()
    local listen = ('127.0.0.1:%d'):format(port)
    local uri = ('http://127.0.0.1:%d/metrics/one'):format(port)

    local config = cbuilder:new(base_config)
        :set_instance_option('i-001', 'metrics', {
            exclude = { 'all' },
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
