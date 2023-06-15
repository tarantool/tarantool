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

    -- Only one of plain, sha1, and sha256 fields can appear at the same time.
    local users = instance_config.schema.fields.credentials.fields.users
    if record.validate == users.value.fields.password.validate then
        if type(config) == 'table' then
            if config.plain ~= nil then
                table.insert(config_fields, 'sha1')
                table.insert(config_fields, 'sha256')
            elseif config.sha1 ~= nil then
                table.insert(config_fields, 'plain')
                table.insert(config_fields, 'sha256')
            elseif config.sha256 ~= nil then
                table.insert(config_fields, 'sha1')
                table.insert(config_fields, 'plain')
            end
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
        if v.type ~= 'record' or not v.enterprise_edition or is_enterprise then
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
            version = 'dev',
            reload = 'auto',
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.config, instance_config.schema.fields.config)

    iconfig = {
        config = {
            version = '0.0.0',
            reload = 'auto',
        },
    }
    local err = '[instance_config] config.version: Got 0.0.0, but only the '..
                'following values are allowed: dev'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    local exp = {
        reload = 'auto',
    }
    local res = instance_config:apply_default({}).config
    t.assert_equals(res, exp)
end

g.test_config_enterprise = function()
    t.tarantool.skip_if_not_enterprise()
    local iconfig = {
        config = {
            version = 'dev',
            reload = 'auto',
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
                ssl = {
                    ssl_key = 'seven',
                    ca_path = 'eight',
                    ca_file = 'nine',
                    verify_peer = true,
                    verify_host = false,
                },
            }
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.config, instance_config.schema.fields.config)

    iconfig = {
        config = {
            version = '0.0.0',
            reload = 'auto',
        },
    }
    local err = '[instance_config] config.version: Got 0.0.0, but only the '..
                'following values are allowed: dev'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    local exp = {
        reload = 'auto',
    }
    local res = instance_config:apply_default({}).config
    t.assert_equals(res, exp)
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
        pid_file = '{{ instance_name }}.pid',
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
        socket = '{{ instance_name }}.control',
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
        file = '{{ instance_name }}.log',
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

g.test_iproto = function()
    local iconfig = {
        iproto = {
            listen = 'one',
            advertise = 'two',
            threads = 1,
            net_msg_max = 1,
            readahead = 1,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.iproto, instance_config.schema.fields.iproto)

    local exp = {
        listen = box.NULL,
        advertise = box.NULL,
        threads = 1,
        net_msg_max = 768,
        readahead = 16320,
    }
    local res = instance_config:apply_default({}).iproto
    t.assert_equals(res, exp)
end

g.test_database = function()
    local iconfig = {
        database = {
            instance_uuid = '11111111-1111-1111-1111-111111111111',
            replicaset_uuid = '11111111-1111-1111-1111-111111111111',
            hot_standby = true,
            rw = true,
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

    local exp = {
        instance_uuid = box.NULL,
        replicaset_uuid = box.NULL,
        hot_standby = false,
        rw = false,
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
    }
    local res = instance_config:apply_default({}).memtx
    t.assert_equals(res, exp)
end

g.test_vinyl = function()
    local iconfig = {
        vinyl = {
            dir = 'one',
            max_tuple_size = 1,
        },
    }
    instance_config:validate(iconfig)
    validate_fields(iconfig.vinyl, instance_config.schema.fields.vinyl)

    local exp = {
        dir = '{{ instance_name }}',
        max_tuple_size = 1048576,
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
        dir = '{{ instance_name }}',
        mode = 'write',
        max_size = 268435456,
        dir_rescan_delay = 2,
        queue_max_size = 16777216,
        cleanup_delay = 14400,
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
        dir = '{{ instance_name }}',
        mode = 'write',
        max_size = 268435456,
        dir_rescan_delay = 2,
        queue_max_size = 16777216,
        cleanup_delay = 14400,
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
        dir = '{{ instance_name }}',
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
            peers = {'one', 'two'},
            anon = true,
            threads = 1,
            timeout = 1,
            synchro_timeout = 1,
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
        anon = false,
        threads = 1,
        timeout = 1,
        synchro_timeout = 5,
        connect_timeout = 30,
        sync_timeout = 0,
        sync_lag = 10,
        synchro_quorum = 'N / 2 + 1',
        skip_conflict = false,
        election_mode = 'off',
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
                        },
                    },
                    roles = {'one', 'two'},
                },
            },
            users = {
                two = {
                    password = {
                        plain = 'one',
                    },
                    privileges = {
                        {
                            permissions = {
                                'write',
                                'read',
                            },
                            universe = true,
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

    iconfig = {
        credentials = {
            users = {
                one = {
                    password = {
                        plain = 'one',
                        sha1 = 'two',
                    },
                },
            },
        },
    }
    local err = '[instance_config] credentials.users.one.password: Only one '..
                'of plain, sha1, and sha256 can appear at the same time.'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    local res = instance_config:apply_default({}).credentials
    t.assert_equals(res, nil)
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
