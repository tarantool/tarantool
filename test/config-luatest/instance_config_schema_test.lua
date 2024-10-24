local fun = require('fun')
local log = require('log')
local t = require('luatest')
local instance_config = require('internal.config.instance_config')

local g = t.group()

local is_enterprise = require('tarantool').package == 'Tarantool Enterprise'

-- Check that all record element names can be found in the table and vice versa.
local function validate_fields(config, record)
    if record.enterprise_edition and not is_enterprise then
        return
    end
    local config_fields = {}
    if type(config) == 'table' then
        for k in pairs(config) do
            table.insert(config_fields, k)
        end
    end

    -- Only one of file, and module fields can appear at the same time.
    if record.validate == instance_config.schema.fields.app.validate then
        if type(config) == 'table' then
            if config.file ~= nil then
                table.insert(config_fields, 'module')
            elseif config.module ~= nil then
                table.insert(config_fields, 'file')
            end
        end
    end

    local record_fields = {}
    for k, v in pairs(record.fields) do
        if v.type == 'record' then
            validate_fields(config[k], v)
        elseif v.type == 'map' and v.value.type == 'record' then
            for _, v1 in pairs(config[k]) do
                validate_fields(v1, v.value)
            end
        elseif v.type == 'array' and v.items.type == 'record' then
            for _, v1 in pairs(config[k]) do
                validate_fields(v1, v.items)
            end
        end
        if not v.enterprise_edition or is_enterprise then
            table.insert(record_fields, k)
        end
    end

    t.assert_items_equals(config_fields, record_fields)
end

g.test_general = function()
    t.assert_equals(instance_config.name, 'instance_config')
end

g.test_config = function()
    t.tarantool.skip_if_enterprise()
    local iconfig = {
        config = {
            reload = 'auto',
            context = {},
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.config, instance_config.schema.fields.config)

    local exp = {
        reload = 'auto',
        storage = {
            timeout = 3,
            reconnect_after = 3,
        }
    }
    local res = instance_config:apply_default({}).config
    t.assert_equals(res, exp)
end

g.test_config_enterprise = function()
    t.tarantool.skip_if_not_enterprise()
    local iconfig = {
        config = {
            reload = 'auto',
            context = {},
            etcd = {
                prefix = '/one',
                endpoints = {'two', 'three'},
                username = 'four',
                password = 'five',
                http = {
                    request = {
                        timeout = 1,
                        unix_socket = 'six',
                    }
                },
                watchers = {
                    reconnect_timeout = 2,
                    reconnect_max_attempts = 3,
                },
                ssl = {
                    ssl_key = 'seven',
                    ssl_cert = 'eight',
                    ca_path = 'nine',
                    ca_file = 'ten',
                    verify_peer = true,
                    verify_host = false,
                },
            },
            storage = {
                prefix = '/one',
                endpoints = {{
                    uri = 'two',
                    login = 'three',
                    password = 'four',
                    params = {
                        transport = 'ssl',
                        ssl_key_file = 'five',
                        ssl_cert_file = 'six',
                        ssl_ca_file = 'seven',
                        ssl_ciphers = 'eight',
                        ssl_password = 'nine',
                        ssl_password_file = 'ten',
                    },
                }},
                timeout = 1.1,
                reconnect_after = 1.2,
            },
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.config, instance_config.schema.fields.config)

    local exp = {
        reload = 'auto',
        storage = {
            timeout = 3,
            reconnect_after = 3,
        }
    }
    local res = instance_config:apply_default({}).config
    t.assert_equals(res, exp)
end

-- A non-empty `config.etcd` table should contain the `prefix`
-- field.
--
-- The error is the same as for Tarantool Enterprise Edition as
-- well as for Tarantool Community Edition in the case.
--
-- However, if the prefix presence check succeeds, the validation
-- fails on CE, because the option is EE only.
g.test_config_etcd_no_prefix = function()
    local iconfig = {
        config = {
            etcd = {
                endpoints = {'localhost:2379'},
            },
        },
    }
    local err = '[instance_config] config.etcd: No config.etcd.prefix provided'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)
end

g.test_process = function()
    local iconfig = {
        process = {
            strip_core = true,
            coredump = true,
            background = true,
            title = 'one',
            username = 'two',
            work_dir = 'three',
            pid_file = 'four',
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.process, instance_config.schema.fields.process)

    local exp = {
        strip_core = true,
        coredump = false,
        background = false,
        title = 'tarantool - {{ instance_name }}',
        username = box.NULL,
        work_dir = box.NULL,
        pid_file = 'var/run/{{ instance_name }}/tarantool.pid',
    }
    local res = instance_config:apply_default({}).process
    t.assert_equals(res, exp)
end

g.test_console = function()
    local iconfig = {
        console = {
            enabled = true,
            socket = 'one',
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.console, instance_config.schema.fields.console)

    local exp = {
        enabled = true,
        socket = 'var/run/{{ instance_name }}/tarantool.control',
    }
    local res = instance_config:apply_default({}).console
    t.assert_equals(res, exp)
end

g.test_fiber = function()
    local iconfig = {
        fiber = {
            io_collect_interval = 1,
            too_long_threshold = 1,
            worker_pool_threads = 1,
            slice = {
                warn = 1,
                err = 1,
            },
            top = {
                enabled = true,
            },
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.fiber, instance_config.schema.fields.fiber)

    local exp = {
        io_collect_interval = box.NULL,
        too_long_threshold = 0.5,
        worker_pool_threads = 4,
        slice = {
            err = 1,
            warn = 0.5,
        },
        top = {
            enabled = false,
        },
    }
    local res = instance_config:apply_default({}).fiber
    t.assert_equals(res, exp)
end

g.test_log = function()
    local iconfig = {
        log = {
            to = 'stderr',
            file = 'one',
            pipe = 'two',
            syslog = {
                identity = 'three',
                facility = 'four',
                server = 'five',
            },
            nonblock = true,
            level = 'debug',
            format = 'json',
            modules = {
                seven = 'debug',
            },
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.log, instance_config.schema.fields.log)

    iconfig = {
        log = {
            level = 5,
        },
    }
    instance_config:validate(iconfig)

    iconfig = {
        log = {
            to = 'pipe',
        },
    }
    local err = '[instance_config] log: The pipe logger is set by the log.to '..
                'parameter but the command is not set (log.pipe parameter)'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    local exp = {
        to = 'stderr',
        file = 'var/log/{{ instance_name }}/tarantool.log',
        pipe = box.NULL,
        syslog = {
            identity = 'tarantool',
            facility = 'local7',
            server = box.NULL,
        },
        nonblock = false,
        level = 5,
        format = 'plain',
    }
    local res = instance_config:apply_default({}).log
    t.assert_equals(res, exp)
end

local function check_validate_iproto()
    local iconfig = {
        iproto = {
            advertise = {
                peer = {
                    uri = 'localhost:3301?transport=plain',
                }
            },
        },
    }
    local err = "[instance_config] iproto.advertise.peer.uri: URI " ..
                "parameters should be described in the 'params' field, not " ..
                "as the part of URI"
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    iconfig = {
        iproto = {
            advertise = {
                peer = {
                    password = 'secret',
                }
            },
        },
    }
    err = "[instance_config] iproto.advertise.peer: Password cannot be set " ..
          "without setting login"
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    iconfig = {
        iproto = {
            advertise = {
                sharding = {
                    uri = 'one:two@localhost:3301',
                }
            },
        },
    }
    err = "[instance_config] iproto.advertise.sharding.uri: Login must be " ..
          "set via the 'login' option"
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    iconfig = {
        iproto = {
            advertise = {
                sharding = {
                    params = {
                        transport = 'plain',
                    },
                }
            },
        },
    }
    err = "[instance_config] iproto.advertise.sharding: Params cannot be " ..
          "set without setting uri"
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    iconfig = {
        iproto = {
            listen = {{
                params = {
                    transport = 'plain',
                },
            }},
        },
    }
    err = '[instance_config] iproto.listen[1]: The URI is required '..
          'for iproto.listen'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    iconfig = {
        iproto = {
            listen = {{
                uri = 'localhost:3301?transport=plain',
            }},
        },
    }
    err = "[instance_config] iproto.listen[1].uri: URI parameters should be " ..
          "described in the 'params' field, not as the part of URI"
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)
end

g.test_iproto = function()
    t.tarantool.skip_if_enterprise()
    local iconfig = {
        iproto = {
            listen = {{
                uri = 'one',
                params = {
                    transport = 'ssl',
                },
            }},
            advertise = {
                client = 'two',
                peer = {
                    uri = 'three',
                    login = 'four',
                    params = {
                        transport = 'plain',
                    },
                    password = 'five',
                },
                sharding = {
                    uri = 'six',
                    login = 'seven',
                    params = {
                        transport = 'ssl',
                    },
                    password = 'ten',
                },
            },
            threads = 1,
            net_msg_max = 1,
            readahead = 1,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.iproto, instance_config.schema.fields.iproto)
    check_validate_iproto()
    local exp = {
        advertise = {
            client = box.NULL,
        },
        threads = 1,
        net_msg_max = 768,
        readahead = 16320,
    }
    local res = instance_config:apply_default({}).iproto
    t.assert_equals(res, exp)
end

g.test_iproto_enterprise = function()
    t.tarantool.skip_if_not_enterprise()
    local iconfig = {
        iproto = {
            listen = {{
                uri = 'one',
                params = {
                    transport = 'ssl',
                    ssl_password = 'twenty-three',
                    ssl_cert_file = 'twenty-four',
                    ssl_key_file = 'twenty-five',
                    ssl_password_file = 'twenty-six',
                    ssl_ciphers = 'twenty-seven',
                    ssl_ca_file = 'twenty-eight',
                },
            }},
            advertise = {
                client = 'two',
                peer = {
                    uri = 'three',
                    login = 'four',
                    params = {
                        transport = 'plain',
                        ssl_password = 'eleven',
                        ssl_cert_file = 'twelve',
                        ssl_key_file = 'thirteen',
                        ssl_password_file = 'fourteen',
                        ssl_ciphers = 'fifteen',
                        ssl_ca_file = 'sixteen',
                    },
                    password = 'five',
                },
                sharding = {
                    uri = 'six',
                    login = 'seven',
                    params = {
                        transport = 'ssl',
                        ssl_password = 'seventeen',
                        ssl_cert_file = 'eighteen',
                        ssl_key_file = 'nineteen',
                        ssl_password_file = 'twenty',
                        ssl_ciphers = 'twenty-one',
                        ssl_ca_file = 'twenty-two',
                    },
                    password = 'ten',
                },
            },
            threads = 1,
            net_msg_max = 1,
            readahead = 1,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.iproto, instance_config.schema.fields.iproto)
    check_validate_iproto()
    local exp = {
        advertise = {
            client = box.NULL,
        },
        threads = 1,
        net_msg_max = 768,
        readahead = 16320,
    }
    local res = instance_config:apply_default({}).iproto
    t.assert_equals(res, exp)
end

-- Verify iproto.advertise validation, bad cases.
for case_name, case in pairs({
    incorrect_uri = {
        advertise = {uri = ':3301'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'Unable to parse an URI',
            'Incorrect URI',
            'expected host:service or /unix.socket'
        }, ': '),
    },
    multiple_uris = {
        advertise = {uri = 'localhost:3301,localhost:3302'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'A single URI is expected, not a list of URIs',
        }, ': '),
    },
    inaddr_any_ipv4 = {
        advertise = {uri = '0.0.0.0:3301'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'bad URI "0.0.0.0:3301"',
            'INADDR_ANY (0.0.0.0) cannot be used to create a client socket',
        }, ': '),
    },
    inaddr_any_ipv4_user = {
        advertise = {uri = '0.0.0.0:3301', login = 'user'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'bad URI "0.0.0.0:3301"',
            'INADDR_ANY (0.0.0.0) cannot be used to create a client socket',
        }, ': '),
    },
    inaddr_any_ipv4_user_pass = {
        advertise = {uri = '0.0.0.0:3301', login = 'user', password = 'pass'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'bad URI "0.0.0.0:3301"',
            'INADDR_ANY (0.0.0.0) cannot be used to create a client socket',
        }, ': '),
    },
    inaddr_any_ipv6 = {
        advertise = {uri = '[::]:3301'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'bad URI "[::]:3301"',
            'in6addr_any (::) cannot be used to create a client socket',
        }, ': '),
    },
    inaddr_any_ipv6_user = {
        advertise = {uri = '[::]:3301', login = 'user'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'bad URI "[::]:3301"',
            'in6addr_any (::) cannot be used to create a client socket',
        }, ': '),
    },
    inaddr_any_ipv6_user_pass = {
        advertise = {uri = '[::]:3301', login = 'user', password = 'pass'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'bad URI "[::]:3301"',
            'in6addr_any (::) cannot be used to create a client socket',
        }, ': '),
    },
    zero_port = {
        advertise = {uri = 'localhost:0'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'bad URI "localhost:0"',
            'An URI with zero port cannot be used to create a client socket',
        }, ': '),
    },
    zero_port_user = {
        advertise = {uri = 'localhost:0', login = 'user'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'bad URI "localhost:0"',
            'An URI with zero port cannot be used to create a client socket',
        }, ': '),
    },
    zero_port_user_pass = {
        advertise = {uri = 'localhost:0', login = 'user', password = 'pass'},
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.%s.uri',
            'bad URI "localhost:0"',
            'An URI with zero port cannot be used to create a client socket',
        }, ': '),
    },
}) do
    g[('test_bad_iproto_advertise_%s'):format(case_name)] = function()
        for _, option_name in ipairs({'peer', 'sharding'}) do
            local exp_err_msg = case.exp_err_msg:format(option_name)
            t.assert_error_msg_equals(exp_err_msg, function()
                instance_config:validate({
                    iproto = {
                        advertise = {
                            [option_name] = case.advertise,
                        },
                    },
                })
            end)
        end
    end
end

-- Extra bad cases specific for iproto.advertise.client.
for case_name, case in pairs({
    incorrect_uri = {
        advertise = ':3301',
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.client',
            'Unable to parse an URI',
            'Incorrect URI',
            'expected host:service or /unix.socket'
        }, ': '),
    },
    multiple_uris = {
        advertise = 'localhost:3301,localhost:3302',
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.client',
            'A single URI is expected, not a list of URIs',
        }, ': '),
    },
    inaddr_any_ipv4 = {
        advertise = '0.0.0.0:3301',
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.client',
            'bad URI "0.0.0.0:3301"',
            'INADDR_ANY (0.0.0.0) cannot be used to create a client socket',
        }, ': '),
    },
    inaddr_any_ipv6 = {
        advertise = '[::]:3301',
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.client',
            'bad URI "[::]:3301"',
            'in6addr_any (::) cannot be used to create a client socket',
        }, ': '),
    },
    zero_port = {
        advertise = 'localhost:0',
        exp_err_msg = table.concat({
            '[instance_config] iproto.advertise.client',
            'bad URI "localhost:0"',
            'An URI with zero port cannot be used to create a client socket',
        }, ': '),
    },
    user_pass = {
        advertise = 'user:pass@unix/:/foo/bar.iproto',
        exp_err_msg = '[instance_config] iproto.advertise.client: Login ' ..
                      'cannot be set for as part of the URI',
    },
    params = {
        advertise = 'unix/:/foo/bar.iproto?transport=plain',
        exp_err_msg = '[instance_config] iproto.advertise.client: URI ' ..
                      "parameters should be described in the 'params' " ..
                      'field, not as the part of URI',
    },
}) do
    g[('test_bad_iproto_advertise_client_%s'):format(case_name)] = function()
        t.assert_error_msg_equals(case.exp_err_msg, function()
            instance_config:validate({
                iproto = {
                    advertise = {
                        client = case.advertise,
                    },
                },
            })
        end)
    end
end

-- Successful cases for iproto.advertise.{peer,sharding}.
for case_name, case in pairs({
    inet_socket = {
        advertise = {
            uri = 'localhost:3301',
        },
    },
    inet_socket_user = {
        advertise = {
            uri = 'localhost:3301',
            login = 'user',
        },
    },
    inet_socket_user_pass = {
        advertise = {
            uri = 'localhost:3301',
            login = 'user',
            password = 'pass',
        },
    },
    unix_socket = {
        advertise = {
            uri = 'unix/:/foo/bar.iproto',
        },
    },
    unix_socket_user = {
        advertise = {
            uri = 'unix/:/foo/bar.iproto',
            login = 'user',
        },
    },
    unix_socket_user_pass = {
        advertise = {
            uri = 'unix/:/foo/bar.iproto',
            login = 'user',
            password = 'pass',
        },
    },
    user = {
        advertise = {
            login = 'user',
        },
    },
    user_pass = {
        advertise = {
            login = 'user',
            password = 'pass',
        },
    },
}) do
    g[('test_good_iproto_advertise_peer_%s'):format(case_name)] = function()
        assert(case.advertise ~= nil)
        for _, option_name in ipairs({'peer', 'sharding'}) do
            instance_config:validate({
                iproto = {
                    advertise = {
                        [option_name] = case.advertise,
                    },
                },
            })
        end
    end
end

-- Successful cases for iproto.advertise.client.
for case_name, case in pairs({
    inet_socket = {
        advertise = 'localhost:3301',
    },
    unix_socket = {
        advertise = 'unix/:/foo/bar.iproto',
    },
    unix_socket_instance_name = {
        advertise = 'unix/:/foo/{{ instance_name }}.iproto',
    },
}) do
    g[('test_good_iproto_advertise_client_%s'):format(case_name)] = function()
        assert(case.advertise ~= nil)
        instance_config:validate({
            iproto = {
                advertise = {
                    client = case.advertise,
                },
            },
        })
    end
end

-- Verify iproto.listen validation, bad cases.
for case_name, case in pairs({
    incorrect_uri = {
        listen = {{
            uri = ':3301',
        }},
        exp_err_msg = table.concat({
            '[instance_config] iproto.listen[1].uri',
            'Unable to parse an URI',
            'Incorrect URI',
            'expected host:service or /unix.socket'
        }, ': '),
    },
    multiple_uri = {
        listen = {{
            uri = '3301, 3302',
        }},
        exp_err_msg = table.concat({
            '[instance_config] iproto.listen[1].uri',
            'A single URI is expected, not a list of URIs'
        }, ': '),
    },
    user_pass = {
        listen = {{
            uri = 'one:two@127.0.0.1',
        }},
        exp_err_msg = table.concat({
            '[instance_config] iproto.listen[1].uri',
            'Login cannot be set for as part of the URI'
        }, ': '),
    },
    params = {
        listen = {{
            uri = "127.0.0.1?transport='plain'",
        }},
        exp_err_msg = table.concat({
            '[instance_config] iproto.listen[1].uri',
            "URI parameters should be described in the 'params' field, not " ..
            'as the part of URI'
        }, ': '),
    },
    no_uri = {
        listen = {{}},
        exp_err_msg = table.concat({
            '[instance_config] iproto.listen[1]',
            'The URI is required for iproto.listen'
        }, ': '),
    },
}) do
    g[('test_bad_iproto_listen_%s'):format(case_name)] = function()
        t.assert_error_msg_equals(case.exp_err_msg, function()
            instance_config:validate({
                iproto = {
                    listen = case.listen,
                },
            })
        end)
    end
end

-- Successful cases for iproto.listen.
for case_name, case in pairs({
    inet_socket = {
        listen = {{
            uri = 'localhost:3301',
        }},
    },
    unix_socket = {
        listen = {{
            uri = 'unix/:/foo/bar.iproto',
        }},
    },
    multiple_uris = {
        listen = {
            {uri = 'localhost:3301',},
            {uri = 'localhost:3302',},
        },
    },
    inaddr_any_ipv4 = {
        listen = {{
            uri = '0.0.0.0:3301',
        }},
    },
    inaddr_any_ipv6 = {
        listen = {{
            uri = '[::]:3301',
        }},
    },
    zero_port = {
        listen = {{
            uri = 'localhost:0',
        }},
    },
}) do
    g[('test_good_iproto_listen_%s'):format(case_name)] = function()
        assert(case.listen ~= nil)
        instance_config:validate({
            iproto = {
                listen = case.listen,
            },
        })
    end
end

g.test_database = function()
    local iconfig = {
        database = {
            instance_uuid = '11111111-1111-1111-1111-111111111111',
            replicaset_uuid = '11111111-1111-1111-1111-111111111111',
            hot_standby = true,
            mode = 'ro',
            txn_timeout = 1,
            txn_isolation = 'best-effort',
            use_mvcc_engine = true,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.database, instance_config.schema.fields.database)

    iconfig = {
        database = {
            instance_uuid = '1',
        },
    }
    local err = '[instance_config] database.instance_uuid: Unable to parse '..
                'the value as a UUID: "1"'
    t.assert_error_msg_content_equals(err, function()
        instance_config:validate(iconfig)
    end)

    iconfig = {
        database = {
            replicaset_uuid = '1',
        },
    }
    err = '[instance_config] database.replicaset_uuid: Unable to parse the '..
          'value as a UUID: "1"'
    t.assert_error_msg_content_equals(err, function()
        instance_config:validate(iconfig)
    end)

    iconfig = {
        database = {
            mode = 'none',
        },
    }
    err = '[instance_config] database.mode: Got none, but only the ' ..
        'following values are allowed: ro, rw'
    t.assert_error_msg_content_equals(err, function()
        instance_config:validate(iconfig)
    end)

    local exp = {
        instance_uuid = box.NULL,
        replicaset_uuid = box.NULL,
        hot_standby = false,
        mode = box.NULL,
        txn_timeout = 3153600000,
        txn_isolation = 'best-effort',
        use_mvcc_engine = false,
    }
    local res = instance_config:apply_default({}).database
    t.assert_equals(res, exp)
end

g.test_sql = function()
    local iconfig = {
        sql = {
            cache_size = 1,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.sql, instance_config.schema.fields.sql)

    local exp = {
        cache_size = 5242880,
    }
    local res = instance_config:apply_default({}).sql
    t.assert_equals(res, exp)
end

g.test_memtx = function()
    local iconfig = {
        memtx = {
            memory = 1,
            allocator = 'small',
            slab_alloc_granularity = 1,
            slab_alloc_factor = 1,
            min_tuple_size = 1,
            max_tuple_size = 1,
            sort_threads = 1,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.memtx, instance_config.schema.fields.memtx)

    local exp = {
        memory = 268435456,
        allocator = 'small',
        slab_alloc_granularity = 8,
        slab_alloc_factor = 1.05,
        min_tuple_size = 16,
        max_tuple_size = 1048576,
        sort_threads = box.NULL,
    }
    local res = instance_config:apply_default({}).memtx
    t.assert_equals(res, exp)
end

g.test_vinyl = function()
    local iconfig = {
        vinyl = {
            dir = 'one',
            max_tuple_size = 1,
            bloom_fpr = 0.1,
            page_size = 123,
            range_size = 321,
            run_count_per_level = 11,
            run_size_ratio = 1.15,
            read_threads = 7,
            write_threads = 9,
            cache = 10,
            defer_deletes = true,
            memory = 11,
            timeout = 5.5,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.vinyl, instance_config.schema.fields.vinyl)

    local exp = {
        dir = 'var/lib/{{ instance_name }}',
        max_tuple_size = 1048576,
        bloom_fpr = 0.05,
        page_size = 8192,
        range_size = box.NULL,
        run_count_per_level = 2,
        run_size_ratio = 3.5,
        read_threads = 1,
        write_threads = 4,
        cache = 134217728,
        defer_deletes = false,
        memory = 134217728,
        timeout = 60,
    }
    local res = instance_config:apply_default({}).vinyl
    t.assert_equals(res, exp)
end

g.test_wal = function()
    t.tarantool.skip_if_enterprise()
    local iconfig = {
        wal = {
            dir = 'one',
            mode = 'none',
            max_size = 1,
            dir_rescan_delay = 1,
            queue_max_size = 1,
            cleanup_delay = 1,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.wal, instance_config.schema.fields.wal)

    local exp = {
        dir = 'var/lib/{{ instance_name }}',
        mode = 'write',
        max_size = 268435456,
        dir_rescan_delay = 2,
        queue_max_size = 16777216,
    }
    local res = instance_config:apply_default({}).wal
    t.assert_equals(res, exp)
end

g.test_wal_enterprise = function()
    t.tarantool.skip_if_not_enterprise()
    local iconfig = {
        wal = {
            dir = 'one',
            mode = 'none',
            max_size = 1,
            dir_rescan_delay = 1,
            queue_max_size = 1,
            cleanup_delay = 1,
            retention_period = 1,
            ext = {
                old = true,
                new = false,
                spaces = {
                    one = {
                        old = false,
                        new = true,
                    },
                },
            },
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.wal, instance_config.schema.fields.wal)

    local exp = {
        dir = 'var/lib/{{ instance_name }}',
        mode = 'write',
        max_size = 268435456,
        dir_rescan_delay = 2,
        queue_max_size = 16777216,
        retention_period = 0,
    }
    local res = instance_config:apply_default({}).wal
    t.assert_equals(res, exp)
end

g.test_snapshot = function()
    local iconfig = {
        snapshot = {
            dir = 'one',
            by = {
                interval = 1,
                wal_size = 1,
            },
            count = 1,
            snap_io_rate_limit = 1,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.snapshot, instance_config.schema.fields.snapshot)

    local exp = {
        dir = 'var/lib/{{ instance_name }}',
        by = {
            interval = 3600,
            wal_size = 1000000000000000000,
        },
        count = 2,
        snap_io_rate_limit = box.NULL,
    }
    local res = instance_config:apply_default({}).snapshot
    t.assert_equals(res, exp)
end

g.test_replication = function()
    local iconfig = {
        replication = {
            failover = 'off',
            peers = {'one', 'two'},
            anon = true,
            anon_gc_timeout = 1,
            threads = 1,
            timeout = 1,
            synchro_timeout = 1,
            synchro_queue_max_size = 1,
            connect_timeout = 1,
            sync_timeout = 1,
            sync_lag = 1,
            synchro_quorum = 1,
            skip_conflict = true,
            election_mode = 'off',
            election_timeout = 1,
            election_fencing_mode = 'off',
            bootstrap_strategy = 'auto',
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.replication,
                    instance_config.schema.fields.replication)

    local exp = {
        failover = 'off',
        anon = false,
        anon_gc_timeout = 60 * 60,
        threads = 1,
        timeout = 1,
        synchro_timeout = 5,
        synchro_queue_max_size = 16777216,
        connect_timeout = 30,
        sync_timeout = box.NULL,
        sync_lag = 10,
        synchro_quorum = 'N / 2 + 1',
        skip_conflict = false,
        election_mode = box.NULL,
        election_timeout = 5,
        election_fencing_mode = 'soft',
        bootstrap_strategy = 'auto',
    }
    local res = instance_config:apply_default({}).replication
    t.assert_equals(res, exp)
end

g.test_credentials = function()
    local iconfig = {
        credentials = {
            roles = {
                one = {
                    privileges = {
                        {
                            permissions = {
                                'create',
                                'drop',
                            },
                            universe = false,
                            spaces = {
                                'myspace1',
                            },
                            functions = {
                                'myfunc1',
                            },
                            sequences = { },
                            lua_eval = false,
                            lua_call = { 'all' },
                            sql = { 'all' },
                        },
                    },
                    roles = {'one', 'two'},
                },
            },
            users = {
                two = {
                    password = 'one',
                    privileges = {
                        {
                            permissions = {
                                'write',
                                'read',
                            },
                            universe = true,
                            spaces = { },
                            functions = {
                                'myfunc2',
                            },
                            sequences = {
                                'myseq2'
                            },
                            lua_eval = true,
                            lua_call = { },
                            sql = { },
                        },
                    },
                    roles = {'one', 'two'},
                },
            },
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.credentials,
                    instance_config.schema.fields.credentials)

    local res = instance_config:apply_default({}).credentials
    t.assert_equals(res, nil)

    iconfig = {
        credentials = {
            roles = {
                myrole = {
                    privileges = {
                        {
                            permissions = {
                                'execute',
                            },
                            lua_call = { 'myfunc' },
                        },
                    },
                },
            },
        },
    }

    res = instance_config:validate(iconfig)
    t.assert_equals(res, nil)

    iconfig = {
        credentials = {
            users = {
                myuser = {
                    privileges = {
                        {
                            permissions = {
                                'execute',
                            },
                            sql = { 'myfunc' },
                        },
                    },
                },
            },
        },
    }

    local err
    err = '[instance_config] credentials.users.myuser.privileges[1].sql[1]: ' ..
          'Got myfunc, but only the following values are allowed: all'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)
end

g.test_app = function()
    local iconfig = {
        app = {
            file = 'one',
            cfg = {three = 'four'},
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.app, instance_config.schema.fields.app)

    iconfig = {
        app = {
            file = 'one',
            module = 'two',
            cfg = {two = 'three'},
        },
    }
    local err = '[instance_config] app: Fields file and module cannot appear '..
                'at the same time'
    t.assert_error_msg_content_equals(err, function()
        instance_config:validate(iconfig)
    end)

    local res = instance_config:apply_default({}).app
    t.assert_equals(res, nil)
end

-- Whether all box.cfg() options can be set using the declarative
-- configuration.
--
-- The test also verifies that all the box_cfg annotations are
-- point to existing box.cfg() options.
--
-- And also compares default values in the schema against ones set
-- by the box.cfg() call.
g.test_box_cfg_coverage = function()
    -- There are box.cfg() options that are set by the box_cfg
    -- applier on its own and cannot be set directly in the
    -- declarative config.
    --
    -- Also, there are some options to be added into the declarative
    -- config soon.
    local ignore = {
        -- Handled by box_cfg applier without the box_cfg schema
        -- node annotation.
        instance_name = true,
        replicaset_name = true,
        cluster_name = true,
        log = true,
        metrics = true,
        audit_log = true,
        audit_filter = true,

        -- Controlled by the leader and database.mode options,
        -- handled by the box_cfg applier.
        read_only = true,

        -- Deliberately moved out of the config, because the
        -- box.cfg() options is deprecated.
        replication_connect_quorum = true,

        -- Cluster options that are not in the instance options.
        bootstrap_leader = true,

        -- Moved to the CLI options (see gh-8876).
        force_recovery = true,
    }

    -- There are options, where defaults are changed deliberately.
    local ignore_default = {
        -- box.cfg.log_nonblock is set to nil by default, but
        -- actually it means false.
        log_nonblock = true,
        audit_nonblock = true,

        -- Adjusted to use {{ instance_name }}.
        custom_proc_title = true,
        memtx_dir = true,
        pid_file = true,
        wal_dir = true,
        vinyl_dir = true,

        -- The effective default is determined depending of
        -- the replication.failover option.
        election_mode = true,

        -- The effective default is determined depending on
        -- the compat.box_cfg_replication_sync_timeout option.
        replication_sync_timeout = true,
        -- The option is deprecated so it has no default value.
        wal_cleanup_delay = true,
    }

    local log_prefix = 'test_box_cfg_coverage'
    local ljust = 33

    -- Collect box_cfg annotations from the schema.
    local box_cfg_options_in_schema = instance_config:pairs():filter(function(w)
        if w.schema.box_cfg == nil then
            return false
        end
        -- Skip EE options on CE.
        if w.schema.enterprise_edition and not is_enterprise then
            return false
        end
        -- Skip feedback options if feedback is disabled.
        if w.schema.box_cfg:startswith('feedback') then
            return box.internal.feedback_daemon ~= nil
        end
        return true
    end):map(function(w)
        return w.schema.box_cfg, {
            path = table.concat(w.path, '.'),
            default = w.schema.default,
        }
    end):tomap()

    -- <schema>:pairs() iterates over scalar, array and map schema
    -- nodes. It expands records on its own and don't add them
    -- into the iterator.
    --
    -- However, wal.ext schema node is a record. Let's add it
    -- manually.
    local records_to_traverse = {
        ['wal.ext'] = instance_config.schema.fields.wal.fields.ext,
    }
    fun.iter(records_to_traverse):each(function(path, schema)
        if schema.enterprise_edition and not is_enterprise then
            return
        end
        if schema.box_cfg ~= nil then
            box_cfg_options_in_schema[schema.box_cfg] = {
                path = path,
                default = schema.default,
            }
        end
    end)

    -- Verify box_cfg annotations in the instance config schema:
    -- they must correspond to existing box.cfg() options.
    for option_name, in_schema in pairs(box_cfg_options_in_schema) do
        log.info('%s: verify existence of box.cfg.%s that is pointed by %s...',
            log_prefix, option_name:ljust(ljust), in_schema.path)
        t.assert(box.internal.template_cfg[option_name] ~= nil,
            ('%s points to non-existing box.cfg.%s'):format(in_schema.path,
            option_name))
    end

    -- Verify that all box.cfg() option are present in the
    -- instance config schema (with known exceptions).
    for option_name, _ in pairs(box.internal.template_cfg) do
        if ignore[option_name] then
            log.info('%s: box.cfg.%s skip', log_prefix,
                option_name:ljust(ljust))
        else
            log.info('%s: box.cfg.%s find...', log_prefix,
                option_name:ljust(ljust))
            t.assert(box_cfg_options_in_schema[option_name] ~= nil,
                ('box.cfg.%s is not found in the instance config ' ..
                'schema'):format(option_name))
        end
    end

    -- Compare defaults.
    for option_name, in_schema in pairs(box_cfg_options_in_schema) do
        if not ignore_default[option_name] then
            t.assert_equals(in_schema.default,
                box.internal.default_cfg[option_name],
                ('defaults for box.cfg.%s are different'):format(option_name))
        end
    end
    for option_name, _ in pairs(box.internal.template_cfg) do
        if not ignore[option_name] and not ignore_default[option_name] then
            t.assert_equals(box_cfg_options_in_schema[option_name].default,
                box.internal.default_cfg[option_name],
                ('defaults for box.cfg.%s are different'):format(option_name))
        end
    end
end

g.test_feedback_enabled = function()
    t.skip_if(box.internal.feedback_daemon == nil, 'Feedback is disabled')
    local iconfig = {
        feedback = {
            crashinfo = false,
            host = 'one',
            metrics_collect_interval = 1,
            send_metrics = false,
            enabled = true,
            interval = 2,
            metrics_limit = 3,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.feedback, instance_config.schema.fields.feedback)

    local exp = {
        crashinfo = true,
        host = 'https://feedback.tarantool.io',
        metrics_collect_interval = 60,
        send_metrics = true,
        enabled = true,
        interval = 3600,
        metrics_limit = 1024*1024,
    }
    local res = instance_config:apply_default({}).feedback
    t.assert_equals(res, exp)
end

g.test_feedback_disabled = function()
    t.skip_if(box.internal.feedback_daemon ~= nil, 'Feedback is enabled')
    local iconfig = {
        feedback = {
            enabled = true,
        },
    }
    local ok, err = pcall(instance_config.validate, instance_config, iconfig)
    t.assert(not ok)
    local exp = '[instance_config] feedback.enabled: Tarantool is built '..
                'without feedback reports sending support'
    t.assert_equals(err, exp)
end

g.test_flightrec = function()
    t.tarantool.skip_if_not_enterprise()
    local iconfig = {
        flightrec = {
            enabled = false,
            logs_log_level = 1,
            logs_max_msg_size = 2,
            logs_size = 3,
            metrics_interval = 4,
            metrics_period = 5,
            requests_max_req_size = 6,
            requests_max_res_size = 7,
            requests_size = 8,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.flightrec, instance_config.schema.fields.flightrec)

    local exp = {
        enabled = false,
        logs_log_level = 6,
        logs_max_msg_size = 4096,
        logs_size = 10485760,
        metrics_interval = 1,
        metrics_period = 180,
        requests_max_req_size = 16384,
        requests_max_res_size = 16384,
        requests_size = 10485760,
    }
    local res = instance_config:apply_default({}).flightrec
    t.assert_equals(res, exp)
end

g.test_security_enterprise = function()
    t.tarantool.skip_if_not_enterprise()

    local iconfig = {
        security = {
            auth_type = 'pap-sha256',
            auth_delay = 5,
            auth_retries = 3,
            disable_guest = true,
            secure_erasing = true,
            password_lifetime_days = 90,
            password_min_length = 10,
            password_enforce_uppercase = true,
            password_enforce_lowercase = true,
            password_enforce_digits = true,
            password_enforce_specialchars = true,
            password_history_length = 3,
        }
    }

    instance_config:validate(iconfig)
    validate_fields(iconfig.security, instance_config.schema.fields.security)
end

g.test_security_community = function()
    t.tarantool.skip_if_enterprise()
    local iconfig = {
        security = {
            auth_type = 'pap-sha256',
        }
    }

    local ok, err = pcall(instance_config.validate, instance_config, iconfig)
    t.assert_not(ok)
    local exp = '[instance_config] security.auth_type: "chap-sha1" is the ' ..
                'only authentication method (auth_type) available in ' ..
                'Tarantool Community Edition (\"pap-sha256\" requested)'
    t.assert_equals(err, exp)

    iconfig = {
        security = {
            auth_type = 'chap-sha1',
            auth_delay = 5,
        }
    }

    ok, err = pcall(instance_config.validate, instance_config, iconfig)
    t.assert_not(ok)
    exp = '[instance_config] security.auth_delay: This configuration ' ..
          'parameter is available only in Tarantool Enterprise Edition'
    t.assert_equals(err, exp)

    iconfig = {
        security = {
            auth_type = 'chap-sha1',
        }
    }

    instance_config:validate(iconfig)
end

g.test_metrics = function()
    local iconfig = {
        metrics = {
            include = {'network', 'info', 'cpu'},
            exclude = {'info'},
            labels = {foo = 'bar'},
        }
    }

    instance_config:validate(iconfig)
    validate_fields(iconfig.metrics, instance_config.schema.fields.metrics)
end

g.test_sharding = function()
    local iconfig = {
        sharding = {
            roles = {'router', 'storage'},
            lock = false,
            zone = 1,
            weight = 1.5,
            sync_timeout = 2,
            connection_outdate_delay = 3,
            failover_ping_timeout = 4,
            discovery_mode = 'once',
            bucket_count = 5,
            shard_index = 'six',
            rebalancer_disbalance_threshold = 7,
            rebalancer_max_receiving = 8,
            rebalancer_max_sending = 9,
            rebalancer_mode = 'manual',
            sched_ref_quota = 10,
            sched_move_quota = 11,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.sharding, instance_config.schema.fields.sharding)

    local iconfig = {
        sharding = {
            roles = {'router', 'rebalancer'},
        },
    }
    local err = '[instance_config] sharding: The rebalancer role cannot be ' ..
                'present without the storage role'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    local exp = {
        bucket_count = 3000,
        discovery_mode = "on",
        failover_ping_timeout = 5,
        rebalancer_disbalance_threshold = 1,
        rebalancer_max_receiving = 100,
        rebalancer_max_sending = 1,
        rebalancer_mode = 'auto',
        sched_move_quota = 1,
        sched_ref_quota = 300,
        shard_index = "bucket_id",
        sync_timeout = 1,
        weight = 1,
    }
    local res = instance_config:apply_default({}).sharding
    t.assert_equals(res, exp)
end

g.test_audit_unavailable = function()
    t.tarantool.skip_if_enterprise()
    local iconfig = {
        audit_log = {
            to = 'file',
        },
    }
    local err = '[instance_config] audit_log.to: This configuration '..
                'parameter is available only in Tarantool Enterprise Edition'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    iconfig = {
        audit_log = {
            filter = {'all'},
        },
    }
    err = '[instance_config] audit_log: This configuration parameter is '..
          'available only in Tarantool Enterprise Edition'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)
end

g.test_audit_available = function()
    t.tarantool.skip_if_not_enterprise()
    local iconfig = {
        audit_log = {
            to = 'file',
            file = 'one',
            pipe = 'two',
            syslog = {
                identity = 'three',
                facility = 'four',
                server = 'five',
            },
            nonblock = true,
            format = 'plain',
            filter = {'all', 'none'},
            spaces = {'space1', 'space2', 'space3'},
            extract_key = true,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.audit_log, instance_config.schema.fields.audit_log)

    local exp = {
        file = "var/log/{{ instance_name }}/audit.log",
        format = "json",
        nonblock = false,
        pipe = box.NULL,
        syslog = {
            facility = "local7",
            identity = "tarantool",
            server = box.NULL
        },
        to = "devnull",
        extract_key = false,
    }
    local res = instance_config:apply_default({}).audit_log
    t.assert_equals(res, exp)
end

g.test_failover = function()
    local iconfig = {
        failover = {
            probe_interval = 5,
            connect_timeout = 2,
            call_timeout = 2,
            lease_interval = 10,
            renew_interval = 1,
            stateboard = {
                renew_interval = 1,
                keepalive_interval = 5,
            },
            replicasets = {
                replicaset001 = {
                    priority = {
                        instance001 = 1
                    }
                }
            }
        }
    }

    instance_config:validate(iconfig)
    validate_fields(iconfig.failover, instance_config.schema.fields.failover)

    local exp = {
        probe_interval = 10,
        connect_timeout = 1,
        call_timeout = 1,
        lease_interval = 30,
        renew_interval = 10,
        stateboard = {
            renew_interval = 2,
            keepalive_interval = 10,
        },
    }
    local res = instance_config:apply_default({}).failover
    t.assert_equals(res, exp)
end

g.test_labels = function()
    local iconfig = {
        labels = {
            foo = 'true',
            bar = 'false',
        },
    }

    instance_config:validate(iconfig)

    t.assert_equals(instance_config:apply_default({}).labels, nil)
end
