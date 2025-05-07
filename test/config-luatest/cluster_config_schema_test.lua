local yaml = require('yaml')
local fio = require('fio')
local t = require('luatest')
local instance_config = require('internal.config.instance_config')
local cluster_config = require('internal.config.cluster_config')

local g = t.group()

local is_enterprise = require('tarantool').package == 'Tarantool Enterprise'

-- Keys of the given table.
local function table_keys(t)
    local res = {}

    for k in pairs(t) do
        table.insert(res, k)
    end

    return res
end

-- Concatenate two array-like tables.
local function array_concat(a, b)
    local res = {}

    for _, v in ipairs(a) do
        table.insert(res, v)
    end

    for _, v in ipairs(b) do
        table.insert(res, v)
    end

    return res
end

-- Generate a config with specific names.
--
-- Set some default valid names if they're not provided.
--
-- Usage:
--
-- gen_config_from_names({
--     group = <...>,
--     replicaset = <...>,
--     instance = <...>,
-- })
--
-- All the options are optional.
local function gen_config_from_names(names)
    return {
        groups = {
            [names.group or 'a'] = {
                replicasets = {
                    [names.replicaset or 'b'] = {
                        instances = {
                            [names.instance or 'c'] = {},
                        },
                    },
                },
            },
        },
    }
end

-- The list of instance config fields on the outmost level.
--
-- The order follows src/box/lua/config/instance_config.lua.
local instance_config_fields = {
    'config',
    'process',
    'lua',
    'console',
    'fiber',
    'log',
    'iproto',
    'database',
    'sql',
    'memtx',
    'vinyl',
    'wal',
    'snapshot',
    'replication',
    'credentials',
    'app',
    'feedback',
    'flightrec',
    'security',
    'metrics',
    'sharding',
    'audit_log',
    'roles_cfg',
    'roles',
    'stateboard',
    'failover',
    'compat',
    'labels',
    'isolated',
}

-- Verify that the fields of the given schema correspond to the
-- instance config fields plus the given additional ones.
local function verify_fields(schema, additional_fields)
    t.assert_equals(schema.type, 'record')

    local exp_fields = array_concat(instance_config_fields, additional_fields)
    local fields = table_keys(schema.fields)

    -- t.assert_equals() reports the difference in case of large
    -- comparing values, so let's sort the arrays and use it
    -- instead of t.assert_items_equals().
    table.sort(exp_fields)
    table.sort(fields)
    t.assert_equals(fields, exp_fields)
end

g.test_cluster_config = function()
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
            ['group-001'] = {
                memtx = {
                    memory = 1000000,
                },
                replicasets = {
                    ['replicaset-001'] = {
                        sql = {
                            cache_size = 2000,
                        },
                        instances = {
                            ['instance-001'] = {
                                database = {
                                    mode = 'rw',
                                },
                            },
                        },
                    },
                },
            },
        },
    }
    cluster_config:validate(config)

    t.assert(cluster_config.methods.instantiate == cluster_config.instantiate)
    local instance_config = cluster_config:instantiate(config, 'instance-001')
    local expected_config = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },
        memtx = {
            memory = 1000000,
        },
        sql = {
            cache_size = 2000,
        },
        database = {
            mode = 'rw',
        },
        iproto = {
            listen = {{uri = 'unix/:./{{ instance_name }}.iproto'}},
        },
    }
    t.assert_equals(instance_config, expected_config)

    t.assert(cluster_config.methods.find_instance ==
             cluster_config.find_instance)
    local result = cluster_config:find_instance(config, 'instance-001')
    local expected_instance = {
        database = {
            mode = 'rw',
        },
    }
    local expected_replicaset = {
        instances = {
            ['instance-001'] = expected_instance,
        },
        sql = {
            cache_size = 2000,
        },
    }
    local expected_group = {
        memtx = {
            memory = 1000000,
        },
        replicasets = {
            ['replicaset-001'] = expected_replicaset,
        },
    }
    local expected = {
        instance = expected_instance,
        replicaset = expected_replicaset,
        replicaset_name = 'replicaset-001',
        group = expected_group,
        group_name = 'group-001',
    }
    t.assert_equals(result, expected)
end

g.test_defaults = function()
    local exp = {
        fiber = {
            io_collect_interval = box.NULL,
            too_long_threshold = 0.5,
            worker_pool_threads = 4,
            tx_user_pool_size = 768,
            slice = {
                err = 1,
                warn = 0.5,
            },
            top = {
                enabled = false,
            },
        },
        flightrec = is_enterprise and {
            enabled = false,
            logs_log_level = 6,
            logs_max_msg_size = 4096,
            logs_size = 10485760,
            metrics_interval = 1,
            metrics_period = 180,
            requests_max_req_size = 16384,
            requests_max_res_size = 16384,
            requests_size = 10485760,
        } or nil,
        sql = {
            cache_size = 5242880,
        },
        log = {
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
        },
        snapshot = {
            dir = 'var/lib/{{ instance_name }}',
            by = {
                interval = 3600,
                wal_size = 1000000000000000000,
            },
            count = 2,
            snap_io_rate_limit = box.NULL,
        },
        iproto = {
            advertise = {
                client = box.NULL,
            },
            threads = 1,
            net_msg_max = 768,
            readahead = 16320,
        },
        process = {
            strip_core = true,
            coredump = false,
            background = false,
            title = 'tarantool - {{ instance_name }}',
            username = box.NULL,
            work_dir = box.NULL,
            pid_file = 'var/run/{{ instance_name }}/tarantool.pid',
        },
        vinyl = {
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
        },
        database = {
            instance_uuid = box.NULL,
            replicaset_uuid = box.NULL,
            hot_standby = false,
            mode = box.NULL,
            txn_timeout = 3153600000,
            txn_synchro_timeout = 5,
            txn_isolation = 'best-effort',
            use_mvcc_engine = false,
        },
        replication = {
            failover = 'off',
            anon = false,
            anon_ttl = 60 * 60,
            threads = 1,
            timeout = 1,
            reconnect_timeout = box.NULL,
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
            autoexpel = {
                enabled = false,
            },
        },
        wal = {
            dir = 'var/lib/{{ instance_name }}',
            mode = 'write',
            max_size = 268435456,
            dir_rescan_delay = 2,
            queue_max_size = 16777216,
            retention_period = is_enterprise and 0 or nil,
        },
        console = {
            enabled = true,
            socket = 'var/run/{{ instance_name }}/tarantool.control',
        },
        lua = {memory = 2147483648},
        memtx = {
            memory = 268435456,
            allocator = 'small',
            slab_alloc_granularity = 8,
            slab_alloc_factor = 1.05,
            min_tuple_size = 16,
            max_tuple_size = 1048576,
            sort_threads = box.NULL,
        },
        config = {
            reload = 'auto',
            storage = {
                timeout = 3,
                reconnect_after = 3,
            },
        },
        feedback = box.internal.feedback_daemon ~= nil and {
            crashinfo = true,
            host = 'https://feedback.tarantool.io',
            metrics_collect_interval = 60,
            send_metrics = true,
            enabled = true,
            interval = 3600,
            metrics_limit = 1024*1024,
        } or nil,
        security = is_enterprise and {
            auth_delay = 0,
            auth_retries = 0,
            auth_type = "chap-sha1",
            disable_guest = false,
            secure_erasing = false,
            password_enforce_digits = false,
            password_enforce_lowercase = false,
            password_enforce_specialchars = false,
            password_enforce_uppercase = false,
            password_history_length = 0,
            password_lifetime_days = 0,
            password_min_length = 0,
        } or {
            auth_type = "chap-sha1"
        },
        sharding = {
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
        },
        audit_log = is_enterprise and {
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
        } or nil,
        failover = {
            probe_interval = 10,
            connect_timeout = 1,
            call_timeout = 1,
            lease_interval = 30,
            renew_interval = 10,
            stateboard = {
                enabled = true,
                renew_interval = 2,
                keepalive_interval = 10,
            },
            log = {
                to = 'stderr',
            },
        },
        compat = {
            json_escape_forward_slash = 'new',
            yaml_pretty_multiline = 'new',
            fiber_channel_close_mode = 'new',
            box_cfg_replication_sync_timeout = 'new',
            box_consider_system_spaces_synchronous = 'old',
            box_error_serialize_verbose = 'old',
            replication_synchro_timeout = 'old',
            sql_seq_scan_default = 'new',
            fiber_slice_default = 'new',
            box_info_cluster_meaning = 'new',
            binary_data_decoding = 'new',
            box_tuple_new_vararg = 'new',
            box_session_push_deprecation = 'old',
            sql_priv = 'new',
            c_func_iproto_multireturn = 'new',
            box_space_execute_priv = 'new',
            box_tuple_extension = 'new',
            box_space_max = 'new',
            box_error_unpack_type_and_code = 'old',
            console_session_scope_vars = 'old',
            wal_cleanup_delay_deprecation = 'old',
        },
        isolated = false,
        stateboard = {
            enabled = false,
            renew_interval = 2,
            keepalive_interval = 10,
        },
    }

    -- Global defaults.
    --
    -- There are additional options:
    --
    -- * groups      (no default)
    -- * conditional (no default)
    local res = cluster_config:apply_default({})
    t.assert_equals(res, exp)

    -- Group defaults.
    --
    -- There is an additional option:
    --
    -- * replicasets (no default)
    local res = cluster_config:apply_default({
        groups = {
            g = {},
        },
    }).groups.g
    t.assert_equals(res, exp)

    -- Replicaset defaults.
    --
    -- There are additional options:
    --
    -- * instances        (no default)
    -- * leader           (no default)
    -- * bootstrap_leader (no default)
    local res = cluster_config:apply_default({
        groups = {
            g = {
                replicasets = {
                    r = {},
                },
            },
        },
    }).groups.g.replicasets.r
    t.assert_equals(res, exp)

    -- Instance defaults.
    --
    -- There are no additional options.
    local res = cluster_config:apply_default({
        groups = {
            g = {
                replicasets = {
                    r = {
                        instances = {
                            i = {}
                        },
                    },
                },
            },
        },
    }).groups.g.replicasets.r.instances.i
    t.assert_equals(res, exp)
end

local examples = {
    single = 'single.yaml',
    upgrade = 'upgrade.yaml',
    sharding = 'sharding.yaml',
    replicaset = 'replicaset.yaml',
    replicaset_manual_failover = 'replicaset_manual_failover.yaml',
    replicaset_election_failover = 'replicaset_election_failover.yaml',
}

for case, path in pairs(examples) do
    local test_name = ('test_example_%s'):format(case)
    local config_path = ('doc/examples/config/%s'):format(path)
    g[test_name] = function()
        local config_file = fio.abspath(config_path)
        local fh = fio.open(config_file, {'O_RDONLY'})
        local config = yaml.decode(fh:read())
        fh:close()
        cluster_config:validate(config)
    end
end

local enterprise_examples = {
    etcd_local = 'etcd/local.yaml',
    etcd_remote = 'etcd/remote.yaml',
    config_storage = 'config-storage/config.yaml',
}

for case, path in pairs(enterprise_examples) do
    local test_name = ('test_example_%s'):format(case)
    local config_path = ('doc/examples/config/%s'):format(path)
    g[test_name] = function()
        t.tarantool.skip_if_not_enterprise()
        local config_file = fio.abspath(config_path)
        local fh = fio.open(config_file, {'O_RDONLY'})
        local config = yaml.decode(fh:read())
        fh:close()
        cluster_config:validate(config)
    end
end

-- Verify that a valid name is accepted as an instance name,
-- a replicaset name, a group name.
g.test_name_success = function()
    local names = {
        'instance-001',
        'instance_001',
        ('x'):rep(63),
    }

    for _, name in ipairs(names) do
        -- Pass a given name as a group name, a replicaset name
        -- and an instance name.
        for _, target in ipairs({'group', 'replicaset', 'instance'}) do
            local config = gen_config_from_names({[target] = name})
            cluster_config:validate(config)
        end
    end
end

-- Verify that an invalid name is not accepted as an instance
-- name, a replicaset name, a group name.
g.test_name_failure = function()
    local err_must_start_from = 'A name must start from a lowercase letter, ' ..
        'got %q'
    local err_must_contain_only = 'A name must contain only lowercase ' ..
        'letters, digits, dash and underscore, got %q'

    local cases = {
        {
            name = '',
            exp_err = 'Zero length name is forbidden',
        },
        {
            name = ('x'):rep(64),
            exp_err = 'A name must fit 63 characters limit, got %q',
        },
        {
            name = '1st',
            exp_err = err_must_start_from,
        },
        {
            name = '_abc',
            exp_err = err_must_start_from,
        },
        {
            name = '_abC',
            exp_err = err_must_contain_only,
        },
        {
            name = 'Abc',
            exp_err = err_must_contain_only,
        },
        {
            name = 'abC',
            exp_err = err_must_contain_only,
        },
        {
            name = 'a.b',
            exp_err = err_must_contain_only,
        },
        {
            name = 'a b',
            exp_err = err_must_contain_only,
        },
    }

    for _, case in ipairs(cases) do
        -- Prepare expected error message.
        local exp_err = case.exp_err
        if exp_err:match('%%q') then
            exp_err = exp_err:format(case.name)
        end

        -- Pass a given name as a group name, a replicaset name
        -- and an instance name.
        for _, target in ipairs({'group', 'replicaset', 'instance'}) do
            local config = gen_config_from_names({[target] = case.name})
            t.assert_error_msg_contains(exp_err, cluster_config.validate,
                cluster_config, config)
        end
    end
end

-- Verify options consistency on the global level.
--
-- Expected instance config fields plus the following additional
-- ones:
--
-- * groups (map)
-- * conditional (array of maps)
g.test_additional_options_global = function()
    -- Some valid values for the additional fields.
    local additional_options = {
        groups = {},
        conditional = {
            {
                ['if'] = '1.2.3 == 1.2.3',
            },
        }
    }

    -- Verify that the fields on the given level are instance
    -- config fields plus the given additional ones.
    local schema = cluster_config.schema
    verify_fields(schema, table_keys(additional_options))

    -- Verify that the given example values for the additional
    -- fields are accepted by the schema on the given level.
    cluster_config:validate(additional_options)
end

-- Verify options consistency on the group level.
--
-- Expected instance config fields plus the following additional
-- ones:
--
-- * replicasets (map)
g.test_additional_options_group = function()
    -- Some valid values for the additional fields.
    local additional_options = {
        replicasets = {},
    }

    -- Verify that the fields on the given level are instance
    -- config fields plus the given additional ones.
    local schema = cluster_config.schema
        .fields.groups.value
    verify_fields(schema, table_keys(additional_options))

    -- Verify that the given example values for the additional
    -- fields are accepted by the schema on the given level.
    cluster_config:validate({
        groups = {
            g = additional_options,
        },
    })
end

-- Verify options consistency on the replicaset level.
--
-- Expected instance config fields plus the following additional
-- ones:
--
-- * instances        (map)
-- * leader           (string)
-- * bootstrap_leader (string)
g.test_additional_options_replicaset = function()
    -- Some valid values for the additional fields.
    local additional_options = {
        instances = {},
        leader = 'x',
        bootstrap_leader = 'y',
    }

    -- Verify that the fields on the given level are instance
    -- config fields plus the given additional ones.
    local schema = cluster_config.schema
        .fields.groups.value
        .fields.replicasets.value
    verify_fields(schema, table_keys(additional_options))

    -- Verify that the given example values for the additional
    -- fields are accepted by the schema on the given level.
    cluster_config:validate({
        groups = {
            g = {
                replicasets = {
                    r = additional_options,
                },
            },
        },
    })
end

-- Verify options consistency on the instance level.
--
-- Expected only instance config fields.
g.test_additional_options_instance = function()
    -- Verify that the fields on the given level are instance
    -- config fields.
    local schema = cluster_config.schema
        .fields.groups.value
        .fields.replicasets.value
        .fields.instances.value
    verify_fields(schema, {})
end

-- Attempt to pass options to different cluster configuration
-- levels and also try to validate it against the instance config.
--
-- The following options are verified here:
--
-- * isolated
-- * replication.autoexpel
-- * failover
-- * replication.failover
-- * sharding.bucket_count
-- * sharding.connection_outdate_delay
-- * sharding.discovery_mode
-- * sharding.failover_ping_timeout
-- * sharding.lock
-- * sharding.rebalancer_disbalance_threshold
-- * sharding.rebalancer_max_receiving
-- * sharding.rebalancer_max_sending
-- * sharding.rebalancer_mode
-- * sharding.sched_move_quota
-- * sharding.sched_ref_quota
-- * sharding.shard_index
-- * sharding.sync_timeout
-- * sharding.weight
-- * sharding.roles
g.test_scope = function()
    local function exp_err(path, scope)
        return ('[cluster_config] %s: The option must not be present in the ' ..
            '%s scope'):format(path, scope)
    end

    local cases = {
        {
            name = 'isolated',
            data = {isolated = true},
            global = false,
            group = false,
            replicaset = false,
            instance = true,
        },
        {
            name = 'replication.autoexpel',
            data = {
                replication = {
                    autoexpel = {
                        enabled = true,
                        by = 'prefix',
                        prefix = 'i-',
                    },
                },
            },
            global = true,
            group = true,
            replicaset = true,
            instance = false,
        },
        {
            name = 'failover',
            data = {
                failover = {
                    stateboard = {
                        enabled = true,
                    },
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'replication.bootstrap_strategy',
            data = {
                replication = {
                    bootstrap_strategy = 'auto',
                },
            },
            global = true,
            group = true,
            replicaset = true,
            instance = false,
        },
        {
            name = 'replication.failover',
            data = {
                replication = {
                    failover = 'manual',
                },
            },
            global = true,
            group = true,
            replicaset = true,
            instance = false,
        },
        {
            name = 'sharding.bucket_count',
            data = {
                sharding = {
                    bucket_count = 30000,
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.connection_outdate_delay',
            data = {
                sharding = {
                    connection_outdate_delay = 10,
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.discovery_mode',
            data = {
                sharding = {
                    discovery_mode = 'off',
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.failover_ping_timeout',
            data = {
                sharding = {
                    failover_ping_timeout = 10,
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.lock',
            data = {
                sharding = {
                    lock = true,
                },
            },
            global = true,
            group = true,
            replicaset = true,
            instance = false,
        },
        {
            name = 'sharding.rebalancer_disbalance_threshold',
            data = {
                sharding = {
                    rebalancer_disbalance_threshold = 7,
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.rebalancer_max_receiving',
            data = {
                sharding = {
                    rebalancer_max_receiving = 1000,
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.rebalancer_max_sending',
            data = {
                sharding = {
                    rebalancer_max_sending = 10,
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.rebalancer_mode',
            data = {
                sharding = {
                    rebalancer_mode = 'off',
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.sched_move_quota',
            data = {
                sharding = {
                    sched_move_quota = 10,
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.sched_ref_quota',
            data = {
                sharding = {
                    sched_ref_quota = 1000,
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.shard_index',
            data = {
                sharding = {
                    shard_index = 'my_bucket_id',
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.sync_timeout',
            data = {
                sharding = {
                    sync_timeout = 10,
                },
            },
            global = true,
            group = false,
            replicaset = false,
            instance = false,
        },
        {
            name = 'sharding.weight',
            data = {
                sharding = {
                    weight = 10,
                },
            },
            global = true,
            group = true,
            replicaset = true,
            instance = false,
        },
        {
            name = 'sharding.roles',
            data = {
                sharding = {
                    roles = {'storage'},
                },
            },
            global = true,
            group = true,
            replicaset = true,
            instance = false,
        },
    }

    for _, case in ipairs(cases) do
        -- Global level.
        local global_data = case.data
        if case.global then
            cluster_config:validate(global_data)
        else
            local path = case.name
            t.assert_error_msg_equals(exp_err(path, 'global'), function()
                cluster_config:validate(global_data)
            end)
        end

        -- Group level.
        local group_data = {
            groups = {
                g = case.data,
            },
        }
        if case.group then
            cluster_config:validate(group_data)
        else
            local path = ('groups.g.%s'):format(case.name)
            t.assert_error_msg_equals(exp_err(path, 'group'), function()
                cluster_config:validate(group_data)
            end)
        end

        -- Replicaset level.
        local replicaset_data = {
            groups = {
                g = {
                    replicasets = {
                        r = case.data,
                    },
                },
            },
        }
        if case.replicaset then
            cluster_config:validate(replicaset_data)
        else
            local path = ('groups.g.replicasets.r.%s'):format(case.name)
            t.assert_error_msg_equals(exp_err(path, 'replicaset'), function()
                cluster_config:validate(replicaset_data)
            end)
        end

        -- Instance level.
        local instance_data = {
            groups = {
                g = {
                    replicasets = {
                        r = {
                            instances = {
                                i = case.data,
                            },
                        },
                    },
                },
            },
        }
        if case.instance then
            cluster_config:validate(instance_data)
        else
            local path = ('groups.g.replicasets.r.instances.i.%s'):format(
                case.name)
            t.assert_error_msg_equals(exp_err(path, 'instance'), function()
                cluster_config:validate(instance_data)
            end)
        end

        -- Validation against the instance config: accepted for
        -- all the options.
        instance_config:validate(case.data)
    end
end

g.test_cluster_config_schema_description_completeness = function()
    local function check_schema_description(schema, ctx)
        local field_path = table.concat(ctx.path, '.')
        t.assert(schema.description ~= nil,
                 string.format('%q is missing description', field_path))
        if schema.type == 'record' then
            for field_name, field_def in pairs(schema.fields) do
                table.insert(ctx.path, field_name)
                check_schema_description(field_def, ctx)
                table.remove(ctx.path)
            end
        elseif schema.type == 'map' then
            table.insert(ctx.path, '*')
            check_schema_description(schema.value, ctx)
            table.remove(ctx.path)
        elseif schema.type == 'array' then
            table.insert(ctx.path, '*')
            check_schema_description(schema.items, ctx)
            table.remove(ctx.path)
        end
    end

    local cluster_config_schema = rawget(cluster_config, 'schema')
    t.assert(cluster_config_schema ~= nil)
    check_schema_description(cluster_config_schema, {path = {}})
end
