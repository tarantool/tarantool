local t = require('luatest')
local cluster_config = require('internal.config.cluster_config')

local g = t.group()

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
                                    rw = true,
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
            rw = true,
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
            rw = true,
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
        sql = {
            cache_size = 5242880,
        },
        log = {
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
        },
        snapshot = {
            dir = '{{ instance_name }}',
            by = {
                interval = 3600,
                wal_size = 1000000000000000000,
            },
            count = 2,
            snap_io_rate_limit = box.NULL,
        },
        iproto = {
            listen = box.NULL,
            advertise = box.NULL,
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
            pid_file = '{{ instance_name }}.pid',
        },
        vinyl = {
            dir = '{{ instance_name }}',
            max_tuple_size = 1048576,
        },
        database = {
            instance_uuid = box.NULL,
            replicaset_uuid = box.NULL,
            hot_standby = false,
            rw = false,
            txn_timeout = 3153600000,
            txn_isolation = 'best-effort',
            use_mvcc_engine = false,
        },
        replication = {
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
        },
        wal = {
            dir = '{{ instance_name }}',
            mode = 'write',
            max_size = 268435456,
            dir_rescan_delay = 2,
            queue_max_size = 16777216,
            cleanup_delay = 14400,
        },
        console = {
            enabled = true,
            socket = '{{ instance_name }}.control',
        },
        memtx = {
            memory = 268435456,
            allocator = 'small',
            slab_alloc_granularity = 8,
            slab_alloc_factor = 1.05,
            min_tuple_size = 16,
            max_tuple_size = 1048576,
        },
        config = {
            reload = 'auto',
        },
    }
    local res = cluster_config:apply_default({})
    t.assert_equals(res, exp)
end
