local t = require('luatest')
local server = require('test.luatest_helpers.server')
local cluster_config = require('internal.config.cluster_config')
local configdata = require('internal.config.configdata')
local treegen = require('test.treegen')
local justrun = require('test.justrun')
local json = require('json')

local g = t.group()

g.before_all(function()
    treegen.init(g)
end)

g.after_all(function()
    treegen.clean(g)
end)

g.after_each(function()
    if g.server ~= nil then
        g.server:stop()
    end
end)

g.test_configdata = function()
    local cconfig = {
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
                            ['instance-002'] = {
                                database = {
                                    mode = 'ro',
                                },
                            },
                        },
                    },
                },
            },
        },
    }
    cluster_config:validate(cconfig)
    local iconfig = cluster_config:instantiate(cconfig, 'instance-001')
    local data = configdata.new(iconfig, cconfig, 'instance-001')
    local expected = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },
        iproto = {
            listen = 'unix/:./instance-001.iproto',
        },
        sql = {
            cache_size = 2000,
        },
        database = {
            mode = 'rw',
        },
        memtx = {
            memory = 1000000,
        },
    }
    t.assert_equals(data:get(), expected)

    t.assert_equals(data:get('iproto'), expected.iproto)

    local f = function(w) return w.schema.type == 'integer' end
    local filtered_data = data:filter(f):totable()
    local expected_filtered_data = {
        {
            data = 2000,
            path = {"sql", "cache_size"},
            schema = {
                box_cfg = "sql_cache_size",
                default = 5242880,
                type = "integer",
                computed = {
                    annotations = {
                        config_version = "dev",
                        box_cfg = "sql_cache_size",
                    },
                },
            },
        },
        {
            data = 1000000,
            path = {"memtx", "memory"},
            schema = {
                box_cfg = "memtx_memory",
                default = 268435456,
                type = "integer",
                computed = {
                    annotations = {
                        config_version = "dev",
                        box_cfg = "memtx_memory",
                    },
                },
            },
        },
    }
    t.assert_items_equals(filtered_data, expected_filtered_data)

    local f2 = function(w) return w.path[#w.path], w.data end
    local mapped_data = data:filter(f):map(f2):tomap()
    local expected_filtered_mapped_data = {
        cache_size = 2000,
        memory = 1000000,
    }
    t.assert_equals(mapped_data, expected_filtered_mapped_data)

    local expected_names = {
        group_name = "group-001",
        instance_name = "instance-001",
        replicaset_name = "replicaset-001",
    }
    t.assert_equals(data:names(), expected_names)

    t.assert_equals(data:peers(), {'instance-001', 'instance-002'})
end

g.test_config_general = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local script = [[
        local json = require('json')
        local config = require('config')
        local file_config = "\
            log:\
              level: 7\
            memtx:\
              min_tuple_size: 16\
              memory: 100000000\
            groups:\
              group-001:\
                replicasets:\
                  replicaset-001:\
                    instances:\
                      instance-001: {}\
        "
        file = io.open('config.yaml', 'w')
        file:write(file_config)
        file:close()
        assert(config:info().status == 'uninitialized')
        config:_startup('instance-001', 'config.yaml')
        assert(config:info().status == 'ready')
        assert(box.cfg.memtx_min_tuple_size == 16)
        assert(box.cfg.memtx_memory == 100000000)
        assert(box.cfg.log_level == 0)
        local res = {old = config:info().alerts}

        file_config = "\
            log:\
              level: 7\
            memtx:\
              min_tuple_size: 32\
              memory: 100000001\
            groups:\
              group-001:\
                replicasets:\
                  replicaset-001:\
                    instances:\
                      instance-001: {}\
        "
        file = io.open('config.yaml', 'w')
        file:write(file_config)
        file:close()
        config:reload()
        assert(box.cfg.memtx_min_tuple_size == 16)
        assert(box.cfg.memtx_memory == 100000001)
        assert(box.cfg.log_level == 0)
        res.new = config:info().alerts
        print(json.encode(res))
        os.exit(0)
    ]]
    treegen.write_script(dir, 'main.lua', script)

    local env = {TT_LOG_LEVEL = 0}
    local opts = {nojson = true, stderr = false}
    local res = justrun.tarantool(dir, env, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0)
    local info = json.decode(res.stdout)
    t.assert_equals(info.old, {})
    t.assert_equals(#info.new, 1)
    t.assert_equals(info.new[1].type, 'warn')
    local exp = "box_cfg.apply: non-dynamic option memtx_min_tuple_size will "..
                "not be set until the instance is restarted"
    t.assert_equals(info.new[1].message, exp)
end

g.test_config_broadcast = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local file_config = [[
        app:
          file: 'script.lua'

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    treegen.write_script(dir, 'config.yaml', file_config)

    local main = [[
        local fiber = require('fiber')
        local config = require('config')
        local status = ''
        box.watch('config.info', function(_, v) status = v.status end)
        config:_startup('instance-001', 'config.yaml')
        while status ~= 'ready' do
            fiber.sleep(0.1)
        end
        print(status)
        config:reload()
        while status ~= 'ready' do
            fiber.sleep(0.1)
        end
        print(status)
        os.exit(0)
    ]]
    treegen.write_script(dir, 'main.lua', main)

    local script = [[
        local fiber = require('fiber')
        local status = ''
        box.watch('config.info', function(_, v) status = v.status end)
        while status ~= 'ready' do
            fiber.sleep(0.1)
        end
        print(status)
    ]]
    treegen.write_script(dir, 'script.lua', script)

    local opts = {nojson = true, stderr = false}
    local args = {'main.lua'}
    local res = justrun.tarantool(dir, {}, args, opts)
    t.assert_equals(res.exit_code, 0)
    local exp = {'ready', 'ready', 'ready', 'ready'}
    t.assert_equals(res.stdout, table.concat(exp, "\n"))
end

g.test_config_option = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local file_config = [[
        log:
          level: 7

        memtx:
          min_tuple_size: 16
          memory: 100000000

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    treegen.write_script(dir, 'config.yaml', file_config)

    local script = [[
        print(box.cfg.memtx_min_tuple_size)
        print(box.cfg.memtx_memory)
        print(box.cfg.log_level)
        os.exit(0)
    ]]
    treegen.write_script(dir, 'main.lua', script)

    local env = {TT_LOG_LEVEL = 0}
    local opts = {nojson = true, stderr = false}
    local args = {'--name', 'instance-001', '--config', 'config.yaml',
                  'main.lua'}
    local res = justrun.tarantool(dir, env, args, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, table.concat({16, 100000000, 0}, "\n"))
end

g.test_remaining_vinyl_options = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        vinyl:
          bloom_fpr: 0.37
          page_size: 777
          range_size: 5555
          run_count_per_level: 3
          run_size_ratio: 1.63
          read_threads: 11
          write_threads: 22
          cache: 111111111
          defer_deletes: true
          memory: 222222222
          timeout: 7.5

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    local config_file = treegen.write_script(dir, 'config.yaml', config)
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.cfg.vinyl_bloom_fpr, 0.37)
        t.assert_equals(box.cfg.vinyl_page_size, 777)
        t.assert_equals(box.cfg.vinyl_range_size, 5555)
        t.assert_equals(box.cfg.vinyl_run_count_per_level, 3)
        t.assert_equals(box.cfg.vinyl_run_size_ratio, 1.63)
        t.assert_equals(box.cfg.vinyl_read_threads, 11)
        t.assert_equals(box.cfg.vinyl_write_threads, 22)
        t.assert_equals(box.cfg.vinyl_cache, 111111111)
        t.assert_equals(box.cfg.vinyl_defer_deletes, true)
        t.assert_equals(box.cfg.vinyl_memory, 222222222)
        t.assert_equals(box.cfg.vinyl_timeout, 7.5)
    end)
end

g.test_feedback_options = function()
    t.skip_if(box.internal.feedback_daemon == nil, 'Feedback is disabled')
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        feedback:
          crashinfo: false
          host: 'https://feedback.tarantool.io'
          metrics_collect_interval: 120
          send_metrics: false
          enabled: false
          interval: 7200
          metrics_limit: 1000000

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    local config_file = treegen.write_script(dir, 'config.yaml', config)
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.cfg.feedback_crashinfo, false)
        t.assert_equals(box.cfg.feedback_host, 'https://feedback.tarantool.io')
        t.assert_equals(box.cfg.feedback_metrics_collect_interval, 120)
        t.assert_equals(box.cfg.feedback_send_metrics, false)
        t.assert_equals(box.cfg.feedback_enabled, false)
        t.assert_equals(box.cfg.feedback_interval, 7200)
        t.assert_equals(box.cfg.feedback_metrics_limit, 1000000)
    end)
end

g.test_memtx_sort_threads = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        memtx:
            sort_threads: 11

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    local config_file = treegen.write_script(dir, 'config.yaml', config)
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.cfg.memtx_sort_threads, 11)
    end)

    config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        memtx:
            sort_threads: 12

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    treegen.write_script(dir, 'config.yaml', config)
    g.server:exec(function()
        local config = require('config')
        config:reload()
        t.assert_equals(box.cfg.memtx_sort_threads, 11)
        t.assert_equals(#config:info().alerts, 1)
        local exp = "box_cfg.apply: non-dynamic option memtx_sort_threads "..
                    "will not be set until the instance is restarted"
        t.assert_equals(config:info().alerts[1].message, exp)
    end)
end

g.test_bootstrap_leader = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        groups:
          group-001:
            replicasets:
              replicaset-001:
                bootstrap_leader: 'instance-001'
                instances:
                  instance-001: {}
    ]]
    local config_file = treegen.write_script(dir, 'config.yaml', config)
    local env = {TT_LOG_LEVEL = 0}
    local args = {'--name', 'instance-001', '--config', config_file}
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, env, args, opts)
    local exp = 'LuajitError: The "bootstrap_leader" option cannot be set '..
                'for replicaset "replicaset-001" because '..
                '"bootstrap_strategy" for instance "instance-001" is not '..
                '"config"\nfatal error, exiting the event loop'
    t.assert_equals(res.exit_code, 1)
    t.assert_equals(res.stderr, exp)

    config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        replication:
          bootstrap_strategy: 'config'

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    treegen.write_script(dir, 'config.yaml', config)
    res = justrun.tarantool(dir, env, args, opts)
    exp = 'LuajitError: The \"bootstrap_leader\" option cannot be empty for '..
          'replicaset "replicaset-001" because "bootstrap_strategy" for '..
          'instance "instance-001" is "config"'..
          '\nfatal error, exiting the event loop'
    t.assert_equals(res.exit_code, 1)
    t.assert_equals(res.stderr, exp)

    config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        replication:
          bootstrap_strategy: 'config'

        groups:
          group-001:
            replicasets:
              replicaset-001:
                bootstrap_leader: 'instance-002'
                instances:
                  instance-001: {}
    ]]
    treegen.write_script(dir, 'config.yaml', config)
    res = justrun.tarantool(dir, env, args, opts)
    exp = 'LuajitError: "bootstrap_leader" = "instance-002" option is set '..
          'for replicaset "replicaset-001" of group "group-001", but '..
          'instance "instance-002" is not found in this replicaset'..
          '\nfatal error, exiting the event loop'
    t.assert_equals(res.exit_code, 1)
    t.assert_equals(res.stderr, exp)

    config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        replication:
          bootstrap_strategy: 'config'

        groups:
          group-001:
            replicasets:
              replicaset-001:
                bootstrap_leader: 'instance-001'
                instances:
                  instance-001: {}
    ]]
    treegen.write_script(dir, 'config.yaml', config)

    opts = {config_file = config_file, alias = 'instance-001', chdir = dir}
    g.server = server:new(opts)
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.cfg.bootstrap_leader, 'instance-001')
        t.assert_equals(box.cfg.bootstrap_strategy, 'config')
    end)
end

g.test_flightrec_options = function()
    t.tarantool.skip_if_not_enterprise()
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        flightrec:
            enabled: false
            logs_log_level: 5
            logs_max_msg_size: 8192
            logs_size: 1000000
            metrics_interval: 5
            metrics_period: 240
            requests_max_req_size: 10000
            requests_max_res_size: 20000
            requests_size: 2000000

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    local config_file = treegen.write_script(dir, 'config.yaml', config)
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.cfg.flightrec_enabled, false)
        t.assert_equals(box.cfg.flightrec_logs_log_level, 5)
        t.assert_equals(box.cfg.flightrec_logs_max_msg_size, 8192)
        t.assert_equals(box.cfg.flightrec_logs_size, 1000000)
        t.assert_equals(box.cfg.flightrec_metrics_interval, 5)
        t.assert_equals(box.cfg.flightrec_metrics_period, 240)
        t.assert_equals(box.cfg.flightrec_requests_max_req_size, 10000)
        t.assert_equals(box.cfg.flightrec_requests_max_res_size, 20000)
        t.assert_equals(box.cfg.flightrec_requests_size, 2000000)
    end)
end

g.test_security_options = function()
    t.tarantool.skip_if_not_enterprise()
    local dir = treegen.prepare_directory(g, {}, {})
    -- guest user is required for luatest.server helper to function properly,
    -- so it is enabled (`disable_guest: false`) unlike other options.
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        security:
            auth_type: pap-sha256
            auth_delay: 5
            disable_guest: false
            password_lifetime_days: 90
            password_min_length: 14
            password_enforce_uppercase: true
            password_enforce_lowercase: true
            password_enforce_digits: true
            password_enforce_specialchars: true
            password_history_length: 3

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    local config_file = treegen.write_script(dir, 'config.yaml', config)
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.cfg.auth_type, 'pap-sha256')
        t.assert_equals(box.cfg.auth_delay, 5)
        t.assert_equals(box.cfg.disable_guest, false)
        t.assert_equals(box.cfg.password_lifetime_days, 90)
        t.assert_equals(box.cfg.password_min_length, 14)
        t.assert_equals(box.cfg.password_enforce_uppercase, true)
        t.assert_equals(box.cfg.password_enforce_lowercase, true)
        t.assert_equals(box.cfg.password_enforce_digits, true)
        t.assert_equals(box.cfg.password_enforce_specialchars, true)
        t.assert_equals(box.cfg.password_history_length, 3)
    end)
end

g.test_metrics_options_default = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]

    -- Test defaults.
    local config_file = treegen.write_script(dir, 'base_config.yaml', config)
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.cfg.metrics.include, {'all'})
        t.assert_equals(box.cfg.metrics.exclude, { })
        t.assert_equals(box.cfg.metrics.labels, {alias = 'instance-001'})
    end)
end

g.test_metrics_options = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super

        iproto:
          listen: unix/:./{{ instance_name }}.iproto

        metrics:
          include: [cpu]
          exclude: [all]
          labels:
            foo: bar

        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]

    local config_file = treegen.write_script(dir, 'base_config.yaml', config)
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.cfg.metrics.include, {'cpu'})
        t.assert_equals(box.cfg.metrics.exclude, {'all'})
        t.assert_equals(box.cfg.metrics.labels, {foo = 'bar'})
    end)
end
