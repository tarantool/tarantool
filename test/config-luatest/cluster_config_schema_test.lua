local yaml = require('yaml')
local fio = require('fio')
local t = require('luatest')
local cluster_config = require('internal.config.cluster_config')

local g = t.group()

local is_enterprise = require('tarantool').package == 'Tarantool Enterprise'

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
            txn_isolation = 'best-effort',
            use_mvcc_engine = false,
        },
        replication = {
            failover = 'off',
            anon = false,
            threads = 1,
            timeout = 1,
            synchro_timeout = 5,
            connect_timeout = 30,
            sync_timeout = 0,
            sync_lag = 10,
            synchro_quorum = 'N / 2 + 1',
            skip_conflict = false,
            election_mode = box.NULL,
            election_timeout = 5,
            election_fencing_mode = 'soft',
            bootstrap_strategy = 'auto',
        },
        wal = {
            dir = 'var/lib/{{ instance_name }}',
            mode = 'write',
            max_size = 268435456,
            dir_rescan_delay = 2,
            queue_max_size = 16777216,
            cleanup_delay = box.NULL,
            retention_period = is_enterprise and 0 or nil,
        },
        console = {
            enabled = true,
            socket = 'var/run/{{ instance_name }}/tarantool.control',
        },
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
                renew_interval = 2,
                keepalive_interval = 10,
            },
        },
        compat = {
            json_escape_forward_slash = 'new',
            yaml_pretty_multiline = 'new',
            fiber_channel_close_mode = 'new',
            box_cfg_replication_sync_timeout = 'new',
            box_error_serialize_verbose = 'old',
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
            box_cfg_wal_cleanup_delay = 'old',
        },
    }
    local res = cluster_config:apply_default({})
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

g.test_sharding = function()
    local config = {
        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        instances = {
                            ['instance-001'] = {
                                sharding = {
                                    roles = {'storage'},
                                },
                            },
                        },
                    },
                },
            },
        },
    }
    local err = '[cluster_config] groups.group-001.replicasets.' ..
                'replicaset-001.instances.instance-001.sharding: ' ..
                'sharding.roles cannot be defined in the instance scope'
    t.assert_error_msg_equals(err, function()
        cluster_config:validate(config)
    end)
end
