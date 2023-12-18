local fun = require('fun')
local yaml = require('yaml')
local net_box = require('net.box')
local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

local function setup(g, dir, config, env, opts)
    local config_file = treegen.write_script(dir, 'config.yaml',
                                             yaml.encode(config))
    local opts = fun.chain({
        config_file = config_file,
        chdir = dir,
        env = env,
    }, opts):tomap()
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()
end

local function reload(g)
    g.server:exec(function()
        local config = require('config')

        config:reload()
    end)
    g.server:wait_until_ready()
end

-- Verify that TT_LISTEN works before and after config:reload().
g.test_basic = function(g)
    -- Prepare a config w/o iproto.listen.
    local config = table.copy(helpers.simple_config)
    config.iproto = nil

    -- Start the server.
    local dir = treegen.prepare_directory(g, {}, {})
    local net_box_uri = ('unix/:%s/var/run/%s/tarantool.iproto'):format(dir,
        'instance-001')
    setup(g, dir, config, {
        ['TT_LISTEN'] =
            ('unix/:./var/run/%s/tarantool.iproto'):format('instance-001'),
    }, {
        net_box_uri = net_box_uri,
    })

    -- Reload and wait for its effect.
    reload(g)

    -- Just do some request.
    local c = net_box.connect(net_box_uri)
    local info = c:call('box.info')
    t.assert_equals(info.id, 1)
end

-- Verify that TT_IPROTO has lower priority than cluster config's
-- values.
g.test_priority = function(g)
    -- Prepare a config with iproto.listen.
    local dir = treegen.prepare_directory(g, {}, {})
    local low_prio_uri = ('unix/:%s/unique1.iproto'):format(dir)
    local high_prio_uri = ('unix/:%s/unique2.iproto'):format(dir)
    local config = table.copy(helpers.simple_config)
    config.iproto = {
        listen = {
            {
                uri = high_prio_uri,
            },
        },
    }

    -- Start the server.
    setup(g, dir, config, {
        ['TT_LISTEN'] = low_prio_uri,
    }, {
        net_box_uri = high_prio_uri,
    })

    local function check()
        -- Attempt to connect to TT_LISTEN URI fails.
        local exp_err = ('connect to %s'):format(low_prio_uri)
        t.assert_error_msg_contains(exp_err, function()
            local c = net_box.connect(low_prio_uri)
            c:call('box.info')
        end)

        -- 'iproto.listen' from cluster config works.
        local c = net_box.connect(high_prio_uri)
        local info = c:call('box.info')
        t.assert_equals(info.id, 1)
    end

    -- Verify that the URI from the cluster config is listening
    -- before and after config:reload().
    check()
    reload(g)
    check()
end

-- Various test cases.
--
-- They modify a process'es environment variables and so it is
-- undesirable to run them right in the main luatest process.
g.test_more = helpers.run_as_script(function()
    local fun = require('fun')
    local t = require('myluatest')

    local cases = {
        -- TT_LISTEN has many allowed forms.
        --
        -- TT_LISTEN: plain URI.
        {
            env = {['TT_LISTEN'] = '3301'},
            config = {iproto = {listen = {
                {uri = '3301'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = 'localhost:3301'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = 'localhost:3301?transport=plain'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301', params = {transport = 'plain'}},
            }}},
        },
        -- TT_LISTEN: array of plain URIs.
        {
            env = {['TT_LISTEN'] = '3301,3302'},
            config = {iproto = {listen = {
                {uri = '3301'},
                {uri = '3302'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = 'localhost:3301,localhost:3302'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301'},
                {uri = 'localhost:3302'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = 'localhost:3301?transport=plain,' ..
                'localhost:3302?transport=plain'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301', params = {transport = 'plain'}},
                {uri = 'localhost:3302', params = {transport = 'plain'}},
            }}},
        },
        -- TT_LISTEN: mapping.
        {
            env = {['TT_LISTEN'] = 'uri=3301'},
            config = {iproto = {listen = {
                {uri = '3301'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = 'uri=localhost:3301'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301'},
            }}},
        },
        -- TT_LISTEN: JSON object.
        {
            env = {['TT_LISTEN'] = '{"uri": "3301"}'},
            config = {iproto = {listen = {
                {uri = '3301'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = '{"uri": "localhost:3301"}'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = '{"uri": "localhost:3301", "params": ' ..
                '{"transport": "plain"}}'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301', params = {transport = 'plain'}},
            }}},
        },
        -- TT_LISTEN: JSON array of numbers.
        {
            env = {['TT_LISTEN'] = '[3301, 3302]'},
            config = {iproto = {listen = {
                {uri = '3301'},
                {uri = '3302'},
            }}},
        },
        -- TT_LISTEN: JSON array of strings.
        {
            env = {['TT_LISTEN'] = '["3301", "3302"]'},
            config = {iproto = {listen = {
                {uri = '3301'},
                {uri = '3302'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = '["localhost:3301", "localhost:3302"]'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301'},
                {uri = 'localhost:3302'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = '["localhost:3301?transport=plain", ' ..
                '"localhost:3302?transport=plain"]'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301', params = {transport = 'plain'}},
                {uri = 'localhost:3302', params = {transport = 'plain'}},
            }}},
        },
        -- TT_LISTEN: JSON array of objects.
        {
            env = {['TT_LISTEN'] = '[{"uri": "3301"}, {"uri": "3302"}]'},
            config = {iproto = {listen = {
                {uri = '3301'},
                {uri = '3302'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = '[{"uri": "localhost:3301"}, ' ..
                '{"uri": "localhost:3302"}]'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301'},
                {uri = 'localhost:3302'},
            }}},
        },
        {
            env = {['TT_LISTEN'] = '[{"uri": "localhost:3301", "params": ' ..
                '{"transport": "plain"}}, {"uri": "localhost:3302", ' ..
                '"params": {"transport": "plain"}}]'},
            config = {iproto = {listen = {
                {uri = 'localhost:3301', params = {transport = 'plain'}},
                {uri = 'localhost:3302', params = {transport = 'plain'}},
            }}},
        },
        -- String options.
        {
            env = {['TT_LOG_FORMAT'] = 'json'},
            config = {log = {format = 'json'}},
        },
        {
            env = {['TT_LOG_LEVEL'] = 'debug'},
            config = {log = {level = 'debug'}},
        },
        {
            env = {['TT_MEMTX_DIR'] = 'x/y/z'},
            config = {snapshot = {dir = 'x/y/z'}},
        },
        -- Numeric options.
        {
            env = {['TT_READAHEAD'] = '32640'},
            config = {iproto = {readahead = 32640}},
        },
        {
            env = {['TT_LOG_LEVEL'] = '7'},
            config = {log = {level = 7}},
        },
        {
            env = {['TT_REPLICATION_TIMEOUT'] = '0.3'},
            config = {replication = {timeout = 0.3}},
        },
        {
            env = {['TT_IPROTO_THREADS'] = '16'},
            config = {iproto = {threads = 16}},
        },
        -- Boolean options.
        {
            env = {['TT_REPLICATION_ANON'] = 'true'},
            config = {replication = {anon = true}},
        },
        {
            env = {['TT_REPLICATION_ANON'] = 'false'},
            config = {replication = {anon = false}},
        },
        {
            env = {['TT_LOG_NONBLOCK'] = 'true'},
            config = {log = {nonblock = true}},
        },
        {
            env = {['TT_STRIP_CORE'] = 'false'},
            config = {process = {strip_core = false}},
        },
    }

    local config_source = require('internal.config.source.env').new({
        env_var_suffix = 'default',
    })

    for _, case in ipairs(cases) do
        fun.iter(case.env):each(os.setenv)

        config_source:sync()
        t.assert_equals(config_source:get(), case.config)

        fun.iter(case.env):each(function(name)
            os.setenv(name, nil)
        end)
    end
end)
