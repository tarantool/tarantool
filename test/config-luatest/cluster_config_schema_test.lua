local yaml = require('yaml')
local fio = require('fio')
local t = require('luatest')
local cluster_config = require('internal.config.cluster_config')

local g = t.group()

local is_enterprise = require('tarantool').package == 'Tarantool Enterprise'

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
            listen = 'unix/:./{{ instance_name }}.iproto',
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
            listen = 'unix/:./{{ instance_name }}.iproto',
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
            listen = box.NULL,
            advertise = {
                client = box.NULL,
                peer = box.NULL,
                sharding = box.NULL,
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
            cleanup_delay = 14400,
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
            sched_move_quota = 1,
            sched_ref_quota = 300,
            shard_index = "bucket_id",
            sync_timeout = 1,
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
        } or nil,
    }
    local res = cluster_config:apply_default({})
    t.assert_equals(res, exp)
end

g.test_example_single = function()
    local config_file = fio.abspath('doc/examples/config/single.yaml')
    local fh = fio.open(config_file, {'O_RDONLY'})
    local config = yaml.decode(fh:read())
    fh:close()
    cluster_config:validate(config)
end

g.test_example_replicaset = function()
    local config_file = fio.abspath('doc/examples/config/replicaset.yaml')
    local fh = fio.open(config_file, {'O_RDONLY'})
    local config = yaml.decode(fh:read())
    fh:close()
    cluster_config:validate(config)
end

g.test_example_sharding = function()
    local config_file = fio.abspath('doc/examples/config/sharding.yaml')
    local fh = fio.open(config_file, {'O_RDONLY'})
    local config = yaml.decode(fh:read())
    fh:close()
    cluster_config:validate(config)
end
