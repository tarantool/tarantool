local fun = require('fun')
local fio = require('fio')
local yaml = require('yaml')
local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local server = require('luatest.server')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

local g_configured = helpers.group('already_configured_work_dir', {
    {context = false},
    {context = true},
    {context = false, same = true},
    {context = true, same = true},
})

g_configured.test_work_dir_is_not_changed = function(cg)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'old/context.txt', 'already configured')
    treegen.write_file(dir, 'old/.rocks/share/tarantool/vshard.lua',
                       string.dump(function()
        assert(require('internal.loaders').get_searchroot() == nil)
        return {consts = {VERSION = '0.1.41'}}
    end))
    treegen.write_file(dir, 'bootstrap.lua', string.dump(function()
        box.cfg({
            work_dir = 'old',
            instance_name = 'instance-001',
            replicaset_name = 'replicaset-001',
        })
    end))
    treegen.write_file(dir, 'check.lua', string.dump(function()
        local fio = require('fio')
        local config = require('config')
        local expected = os.getenv('EXPECTED_WORKDIR')
        local function verify()
            assert(fio.cwd() == expected)
            assert(box.cfg.work_dir == 'old')
            assert(not fio.path.exists(expected .. '/new'))
            assert(require('internal.config.utils.workdir').is_prepared())
            assert(require('internal.loaders').get_searchroot() == nil)
            assert(config:get('sharding.rebalancer_bucket_send_timeout') ==
                   15768000000)
            if os.getenv('CHECK_CONTEXT') == 'true' then
                assert(config:get('process.title') == 'already configured')
            end
            local found = false
            for _, alert in ipairs(config:info().alerts) do
                if alert.message == 'box_cfg.apply: non-dynamic option ' ..
                    'work_dir will not be set until the instance is restarted'
                then
                    assert(alert.type == 'warn')
                    found = true
                end
            end
            assert(found == (os.getenv('EXPECT_WORKDIR_WARNING') == 'true'))
            assert(not fio.path.exists(expected .. '/old'))
        end
        verify()
        local s = box.schema.space.create('workdir_probe')
        s:create_index('pk')
        s:insert({1})
        box.snapshot()
        assert(#fio.glob(expected .. '/*.snap') == 2)
        config:reload()
        verify()
        print('configured workdir preserved')
        os.exit(0)
    end))
    local config = table.deepcopy(helpers.simple_config)
    config.process = {work_dir = cg.params.same and 'old' or 'new'}
    config.console = {enabled = false}
    config.snapshot = {dir = '.'}
    config.wal = {dir = '.'}
    if cg.params.context then
        config.config = {context = {
            title = {from = 'file', file = 'context.txt'},
        }}
        config.process.title = '{{ context.title }}'
    end
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {
        EXPECTED_WORKDIR = dir .. '/old',
        CHECK_CONTEXT = tostring(cg.params.context),
        EXPECT_WORKDIR_WARNING = tostring(not cg.params.same),
        BOOTSTRAP_FILE = dir .. '/bootstrap.lua',
        LUA_PATH = '',
    }, {'-e', 'dofile(os.getenv("BOOTSTRAP_FILE"))',
        '--name', 'instance-001', '--config', dir .. '/config.yaml',
        dir .. '/check.lua'},
        {nojson = true, stderr = true, quote_args = true,
         setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(res.stdout, 'configured workdir preserved')
end

g.test_box_cfg_before_work_dir_preparation = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'check.lua', string.dump(function()
        local fio = require('fio')
        local workdir = require('internal.config.utils.workdir')
        workdir.initialize()
        assert(not workdir.is_prepared())
        box.cfg({})
        assert(workdir.is_prepared())
        local cwd = fio.cwd()
        workdir.prepare('must-not-be-created')
        assert(workdir.is_prepared())
        assert(fio.cwd() == cwd)
        assert(not fio.path.exists('must-not-be-created'))
        print('late box.cfg respected')
        os.exit(0)
    end))
    local res = justrun.tarantool(dir, {}, {'check.lua'},
                                 {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(res.stdout, 'late box.cfg respected')
end

-- Verify that `--config <...>` with a relative path to the config
-- works good with `process.work_dir` option, which changes CWD of
-- the instance.
g.test_relative_config_path = function(g)
    local dir = treegen.prepare_directory({}, {})
    local config = table.copy(helpers.simple_config)
    config.process = {
        work_dir = 'x',
    }
    local config_file = 'config.yaml'
    treegen.write_file(dir, config_file, yaml.encode(config))

    -- Important: `--config <...>` is passed with a relative path.
    local opts = {config_file = config_file, chdir = dir}
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()

    local function server_is_ok(server)
        server:exec(function()
            local config = require('config')

            local info = config:info()
            t.assert_equals({
                status = info.status,
                alerts = info.alerts,
            }, {
                status = 'ready',
                alerts = {},
            })
        end)
    end

    -- Verify that the instance is started successfully. Reload
    -- the configuration. Verify that the reload is successful.
    server_is_ok(g.server)
    g.server:exec(function()
        local config = require('config')

        config:reload()
    end)
    server_is_ok(g.server)
end

g.test_symlink_parent_work_dir = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'target/child/.keep', '')
    t.assert(fio.symlink('target/child', fio.pathjoin(dir, 'link')))
    treegen.write_file(dir, 'target/main.lua', [[
        -- --- #tarantool.metadata.v1
        -- early_load: true
        -- ...
        assert(type(box.cfg) == 'function')
        assert(require('fio').cwd() == os.getenv('EXPECTED_WORKDIR'))
        print('filesystem traversal preserved')
        os.exit(0)
    ]])
    local config = table.deepcopy(helpers.simple_config)
    config.process = {work_dir = 'link/../created'}
    config.app = {file = dir .. '/target/main.lua'}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {
        EXPECTED_WORKDIR = fio.pathjoin(dir, 'target/created'),
    }, {'--name', 'instance-001', '--config', 'config.yaml'},
        {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'filesystem traversal preserved')
    t.assert(fio.path.is_dir(dir .. '/target/created'))
    t.assert_not(fio.path.exists(dir .. '/created'))
end

g.test_absolute_work_dir_before_appliers = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'fixture.txt', 'startup')
    treegen.write_file(dir, 'wd/fixture.txt', 'workdir')
    treegen.write_file(dir, 'wd/main.lua', [[
        -- --- #tarantool.metadata.v1
        -- early_load: true
        -- ...
        assert(type(box.cfg) == 'function')
        assert(require('fio').cwd() == os.getenv('EXPECTED_WORKDIR'))
        local f = assert(io.open('fixture.txt'))
        assert(f:read('*a') == 'workdir')
        f:close()
        print('absolute workdir before box.cfg')
        os.exit(0)
    ]])
    local config = table.deepcopy(helpers.simple_config)
    config.process = {work_dir = dir .. '/wd'}
    config.app = {file = 'main.lua'}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {EXPECTED_WORKDIR = dir .. '/wd'},
        {'--name', 'instance-001', '--config', 'config.yaml'},
        {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'absolute workdir before box.cfg')
end

-- An explicitly pinned module search root is respected:
-- process.work_dir must not override it. For example, luatest
-- pins the search root for the test servers it spawns and the
-- luatest module itself may be resolvable only through it.
g.test_pinned_searchroot = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'pinned/.rocks/share/tarantool/mymod.lua',
                       'return {}')
    treegen.write_file(dir, 'wd/main.lua', [[
        require('mymod')
        print('mymod is loaded')
        os.exit(0)
    ]])
    local config = [[
    process:
      work_dir: wd

    app:
      file: main.lua

    groups:
      group-001:
        replicasets:
          replicaset-001:
            instances:
              instance-001: {}
    ]]
    treegen.write_file(dir, 'config.yaml', config)

    -- LUA_PATH is emptied to not resolve anything from the
    -- testing environment: mymod is resolvable only through the
    -- pinned search root.
    local res = justrun.tarantool(dir, {
        LUA_PATH = '', TT_CONFIG_DEBUG = '1',
    }, {
        '-e', ('package.setsearchroot(%q)'):format(
            fio.pathjoin(dir, 'pinned')),
        '--name', 'instance-001', '--config', 'config.yaml',
    }, {nojson = true, stderr = true, quote_args = true,
        setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'mymod is loaded')
    t.assert_str_contains(res.stderr,
        ('the module search root is not set: it is already pinned to %q')
        :format(fio.pathjoin(dir, 'pinned')))
end

g.test_searchroot_pinned_to_startup_dir = function()
    local dir = treegen.prepare_directory({}, {})
    local vshard = [[
        return {
            consts = {VERSION = %q},
            router = {
                cfg = function() end,
                info = function() return {bucket = {unknown = 0}} end,
            },
        }
    ]]
    treegen.write_file(dir, '.rocks/share/tarantool/vshard/init.lua',
                       vshard:format('0.1.41'))
    treegen.write_file(dir, 'wd/.rocks/share/tarantool/vshard/init.lua',
                       vshard:format('0.1.30'))
    treegen.write_file(dir, '.rocks/share/tarantool/mymod.lua',
                       'return "startup"')
    treegen.write_file(dir, 'wd/.rocks/share/tarantool/mymod.lua',
                       'return "workdir"')
    treegen.write_file(dir, 'wd/main.lua', [[
        assert(require('mymod') == 'startup')
        assert(require('config'):get('sharding.rebalancer_bucket_send_timeout')
               == 15768000000)
        print('startup pin is preserved')
        os.exit(0)
    ]])
    local config = table.deepcopy(helpers.simple_config)
    config.process = {work_dir = 'wd'}
    config.app = {file = 'main.lua'}
    config.sharding = {roles = {'router'}}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {LUA_PATH = ''}, {
        '-e', 'package.setsearchroot(require("fio").cwd())',
        '--name', 'instance-001', '--config', 'config.yaml',
    }, {nojson = true, stderr = true, quote_args = true,
        setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'startup pin is preserved')
end

local searchroot_cases = {
    {name = 'unset', code = '', root = false},
    {
        name = 'different',
        code = "package.setsearchroot(os.getenv('PIN_SEARCHROOT'))",
        root = 'pinned',
    },
    {
        name = 'same',
        code = 'package.setsearchroot(package.searchroot())',
        root = 'wd',
    },
    {
        name = 'away_and_back',
        code = [[
            local root = package.searchroot()
            package.setsearchroot(root .. '/../pinned')
            package.setsearchroot(root)
        ]],
        root = 'wd',
    },
    {
        name = 'reset',
        code = 'package.setsearchroot(box.NULL)',
        root = false,
    },
    {
        name = 'invalid',
        code = 'assert(not pcall(package.setsearchroot, {}))',
        root = false,
    },
}

g.test_searchroot_after_startup = function()
    for _, case in ipairs(searchroot_cases) do
        local dir = treegen.prepare_directory({}, {})
        treegen.write_file(dir, 'wd/.rocks/share/tarantool/vshard/init.lua',
                           case.code .. '\n' ..
                           "return {consts = {VERSION = '0.1.30'}}")
        for _, root in ipairs({'other', 'wd', 'pinned'}) do
            treegen.write_file(dir, root .. '/.rocks/share/tarantool/probe.lua',
                               ('return %q'):format(root))
        end
        treegen.write_file(dir, 'wd/main.lua', [[
            local fio = require('fio')
            local pin = os.getenv('PIN_SEARCHROOT')
            assert(package.loaded.vshard.consts.VERSION == '0.1.30')
            assert(require('internal.loaders').get_searchroot() ==
                   (pin ~= '' and pin or nil))
            assert(fio.chdir('../other'))
            assert(require('probe') == os.getenv('EXPECTED_PROBE'))
            print('searchroot policy preserved after startup')
            os.exit(0)
        ]])
        local config = table.deepcopy(helpers.simple_config)
        config.process = {work_dir = 'wd'}
        config.app = {file = 'main.lua'}
        treegen.write_file(dir, 'config.yaml', yaml.encode(config))
        local res = justrun.tarantool(dir, {
            LUA_PATH = '',
            PIN_SEARCHROOT = case.root and dir .. '/' .. case.root or '',
            EXPECTED_PROBE = case.root or 'other',
        }, {
            '--name', 'instance-001', '--config', 'config.yaml',
        }, {nojson = true, stderr = true, setsearchroot = false})
        t.assert_equals(res.exit_code, 0, {case = case.name, stderr = res.stderr})
        t.assert_str_contains(res.stdout,
                              'searchroot policy preserved after startup')
    end
end

g.test_searchroot_after_validation_error = function()
    for _, case in ipairs(searchroot_cases) do
        local dir = treegen.prepare_directory({}, {})
        treegen.write_file(dir, 'wd/.rocks/share/tarantool/vshard/init.lua',
                           case.code .. '\n' ..
                           "return {consts = {VERSION = '0.1.30'}}")
        for _, root in ipairs({'wd', 'other', 'pinned'}) do
            treegen.write_file(dir, root .. '/.rocks/share/tarantool/probe.lua',
                               ('return %q'):format(root))
        end
        treegen.write_file(dir, 'check.lua', [[
            local fio = require('fio')
            local config = require('config')
            local startup_dir = fio.cwd()
            package.setsearchroot(box.NULL)
            -- Catch failure before the fatal startup wrapper exits.
            local ok, err = pcall(config._startup_impl, config,
                                  'instance-001', 'config.yaml')
            assert(not ok)
            assert(tostring(err):find('The isolated mode is enabled', 1, true),
                   tostring(err))
            print(err)
            assert(type(box.cfg) == 'function')
            assert(fio.cwd() == startup_dir)
            assert(package.loaded.vshard.consts.VERSION == '0.1.30')
            local pin = os.getenv('PIN_SEARCHROOT')
            assert(require('internal.loaders').get_searchroot() ==
                   (pin ~= '' and pin or nil))
            assert(fio.chdir('other'))
            assert(require('probe') == os.getenv('EXPECTED_PROBE'))
            print('searchroot policy preserved after validation error')
            os.exit(0)
        ]])
        local config = table.deepcopy(helpers.simple_config)
        config.process = {work_dir = 'wd'}
        config.sharding = {roles = {'router'}}
        config.groups['group-001'].replicasets['replicaset-001']
            .instances['instance-001'].isolated = true
        treegen.write_file(dir, 'config.yaml', yaml.encode(config))
        local res = justrun.tarantool(dir, {
            LUA_PATH = '',
            PIN_SEARCHROOT = case.root and dir .. '/' .. case.root or '',
            EXPECTED_PROBE = case.root or 'other',
        }, {'check.lua'}, {nojson = true, stderr = true, setsearchroot = false})
        t.assert_equals(res.exit_code, 0, {case = case.name, stderr = res.stderr})
        t.assert_str_contains(res.stdout, 'The isolated mode is enabled')
        t.assert_str_contains(res.stdout,
                              'searchroot policy preserved after ' ..
                              'validation error')
    end
end

g.test_work_dir_reload = function(g)
    for _, kind in ipairs({'relative', 'absolute', 'symlink', 'omitted',
                           'null'}) do
        local dir = treegen.prepare_directory({}, {})
        if kind == 'symlink' then
            treegen.write_file(dir, 'target/child/.keep', '')
            t.assert(fio.symlink('target/child', dir .. '/link'))
        end
        local work_dir = ({relative = 'wd/nested',
                           absolute = dir .. '/wd/nested',
                           symlink = 'link/../wd',
                           null = box.NULL})[kind]
        local config = table.deepcopy(helpers.simple_config)
        config.process = {work_dir = work_dir}
        treegen.write_file(dir, 'config.yaml', yaml.encode(config))
        g.server = server:new({alias = 'instance-001', chdir = dir,
                               config_file = 'config.yaml'})
        g.server:start()
        local active_dir = work_dir == nil and dir or dir .. '/wd/nested'
        if kind == 'symlink' then
            active_dir = dir .. '/target/wd'
        end
        local effective = work_dir or box.NULL
        local function verify(active_dir, effective, authored, warning)
            local fio = require('fio')
            local config = require('config')
            t.assert_equals(fio.cwd(), active_dir)
            t.assert_equals(box.cfg.work_dir, effective)
            box.cfg({work_dir = effective})
            t.assert_equals(config:get('process.work_dir'), authored)
            if warning then
                t.assert_covers(config:info().alerts, {{
                    type = 'warn',
                    message = 'box_cfg.apply: non-dynamic option work_dir ' ..
                        'will not be set until the instance is restarted',
                }})
            else
                t.assert_equals(config:info().alerts, {})
            end
        end
        g.server:exec(verify, {active_dir, effective, work_dir or box.NULL})
        g.server:exec(function() require('config'):reload() end)
        g.server:exec(verify, {active_dir, effective, work_dir or box.NULL})
        -- Change/add and then remove the explicit value. Neither request
        -- may prepare a directory, and both use the nondynamic contract.
        config.process.work_dir = 'requested/new'
        treegen.write_file(dir, 'config.yaml', yaml.encode(config))
        g.server:exec(function() require('config'):reload() end)
        g.server:exec(verify, {active_dir, effective, 'requested/new', true})
        config.process.work_dir = box.NULL
        treegen.write_file(dir, 'config.yaml', yaml.encode(config))
        g.server:exec(function() require('config'):reload() end)
        g.server:exec(verify, {active_dir, effective, box.NULL,
                               work_dir ~= nil})
        t.assert_not(fio.path.exists(dir .. '/requested'))
        t.assert_not(fio.path.exists(active_dir .. '/requested'))
        t.assert_not(fio.path.exists(active_dir .. '/wd'))
        g.server:stop()
        g.server = nil
    end
end

-- Verify an early box.cfg() can explicitly choose a different workdir.
g.test_early_box_cfg_work_dir = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'wd/other/.keep', '')
    local script = treegen.write_file(dir, 'main.lua', [[
        -- --- #tarantool.metadata.v1
        -- early_load: true
        -- ...
        assert(type(box.cfg) == 'function')
        box.cfg({work_dir = 'other'})
        assert(box.cfg.work_dir == 'other')
        assert(require('fio').cwd() == os.getenv('EXPECTED_WORKDIR'))
        print('early box.cfg workdir honored')
        os.exit(0)
    ]])
    local config = table.deepcopy(helpers.simple_config)
    config.process = {work_dir = 'wd'}
    config.app = {file = script}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {
        EXPECTED_WORKDIR = dir .. '/wd/other',
    }, {'--name', 'instance-001', '--config', 'config.yaml'},
        {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'early box.cfg workdir honored')
end

local g_changed_cwd = helpers.group('workdir_changed_cwd', {
    {kind = 'absolute', explicit = false},
    {kind = 'absolute', explicit = true},
    {kind = 'relative', explicit = false},
    {kind = 'relative', explicit = true},
    {kind = 'symlink', explicit = false},
    {kind = 'symlink', explicit = true},
})

-- Verify recovery returns to the prepared workdir after an early chdir().
g_changed_cwd.test_recover_prepared_directory = function(cg)
    local dir = treegen.prepare_directory({}, {})
    local physical = 'wd'
    local requested = cg.params.kind == 'absolute' and dir .. '/wd' or 'wd'
    if cg.params.kind == 'symlink' then
        treegen.write_file(dir, 'target/child/.keep', '')
        t.assert(fio.symlink('target/child', dir .. '/link'))
        physical = 'target/wd'
        requested = 'link/../wd'
    end
    treegen.write_file(dir, physical .. '/.keep', '')
    treegen.write_file(dir, 'other/.keep', '')
    local env = {
        EXPECTED_WORKDIR = dir .. '/' .. physical,
        REQUESTED_WORKDIR = requested,
        OTHER_DIR = dir .. '/other',
        PID_FILE = dir .. '/instance.pid',
        EXPLICIT_CFG = tostring(cg.params.explicit),
    }
    treegen.write_file(dir, 'bootstrap.lua', [[
        box.cfg({
            work_dir = os.getenv('EXPECTED_WORKDIR'),
            memtx_dir = '.', wal_dir = '.', vinyl_dir = '.',
            instance_name = 'instance-001',
            replicaset_name = 'replicaset-001',
        })
        local s = box.schema.space.create('marker')
        s:create_index('pk')
        s:insert({1, 'saved data'})
        box.snapshot()
        os.exit(0)
    ]])
    local opts = {nojson = true, stderr = true, setsearchroot = false}
    local res = justrun.tarantool(dir, env, {'bootstrap.lua'}, opts)
    t.assert_equals(res.exit_code, 0, res.stderr)
    local initial_snapshots = fio.glob(env.EXPECTED_WORKDIR .. '/*.snap')
    t.assert_equals(#initial_snapshots, 2)
    local script = treegen.write_file(dir, 'main.lua', [[
        -- --- #tarantool.metadata.v1
        -- early_load: true
        -- ...
        if rawget(_G, 'recovery_check_started') then return end
        rawset(_G, 'recovery_check_started', true)
        local fio = require('fio')
        local config = require('config')
        assert(type(box.cfg) == 'function')
        assert(fio.cwd() == os.getenv('EXPECTED_WORKDIR'))
        assert(fio.chdir(os.getenv('OTHER_DIR')))
        if os.getenv('EXPLICIT_CFG') == 'true' then
            box.cfg({
                work_dir = os.getenv('REQUESTED_WORKDIR'),
                memtx_dir = '.', wal_dir = '.', vinyl_dir = '.',
                pid_file = os.getenv('PID_FILE'), log_nonblock = false,
                instance_name = 'instance-001',
                replicaset_name = 'replicaset-001',
            })
        end
        require('fiber').new(function()
            config:wait_status('ready')
            local ok, err = pcall(function()
                assert(fio.cwd() == os.getenv('EXPECTED_WORKDIR'))
                assert(box.cfg.work_dir == os.getenv('REQUESTED_WORKDIR'))
                assert(box.space.marker ~= nil, 'old database not recovered')
                assert(box.space.marker:get(1)[2] == 'saved data')
                box.space.marker:insert({2, 'new data'})
                box.snapshot()
                config:reload()
                assert(box.space.marker:get(1)[2] == 'saved data')
                assert(fio.cwd() == os.getenv('EXPECTED_WORKDIR'))
            end)
            if not ok then print(tostring(err)) end
            print('prepared database recovery: ' .. tostring(ok))
            os.exit(ok and 0 or 1)
        end)
    ]])
    local config = table.deepcopy(helpers.simple_config)
    config.iproto = nil
    config.console = {enabled = false}
    config.process = {work_dir = requested, pid_file = env.PID_FILE}
    config.snapshot = {dir = '.'}
    config.wal = {dir = '.'}
    config.vinyl = {dir = '.'}
    config.app = {file = script}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    res = justrun.tarantool(dir, env,
        {'--name', 'instance-001', '--config', 'config.yaml'}, opts)
    t.assert_equals(res.exit_code, 0, {res.stdout, res.stderr})
    t.assert_str_contains(res.stdout, 'prepared database recovery: true')
    t.assert_not_equals(fio.glob(env.EXPECTED_WORKDIR .. '/*.snap'),
                        initial_snapshots)
    t.assert_equals(fio.glob(env.OTHER_DIR .. '/*.snap'), {})
    t.assert_equals(fio.glob(env.OTHER_DIR .. '/*.xlog'), {})
end

local g_snapshot_miss = helpers.group('snapshot_miss_changed_cwd',
    t.helpers.matrix({
        kind = {'absolute', 'relative', 'symlink', 'retargeted'},
        nested = {false, true},
        absolute_snapshot = {false, true},
    }))

-- Verify an initial snapshot miss stays anchored after early cwd changes.
g_snapshot_miss.test_discovery_in_prepared_directory = function(cg)
    local dir = treegen.prepare_directory({}, {})
    local physical = 'wd'
    local requested = cg.params.kind == 'absolute' and dir .. '/wd' or 'wd'
    if cg.params.kind == 'symlink' or cg.params.kind == 'retargeted' then
        treegen.write_file(dir, 'target/child/.keep', '')
        t.assert(fio.symlink('target/child', dir .. '/link'))
        physical = 'target/wd'
        requested = 'link/../wd'
    end
    local snap_dir = cg.params.nested and 'snap/nested' or '.'
    local configured_snap_dir = cg.params.absolute_snapshot and
        dir .. '/' .. physical .. '/' .. snap_dir or snap_dir
    local invalid_snapshot = 'other/' .. snap_dir ..
        '/00000000000000000000.snap'
    treegen.write_file(dir, invalid_snapshot, '')
    if cg.params.kind == 'retargeted' then
        treegen.write_file(dir, 'other/child/.keep', '')
        treegen.write_file(dir, 'other/wd/' .. snap_dir ..
                           '/00000000000000000000.snap', '')
    end
    local script = treegen.write_file(dir, 'main.lua', [[
        -- --- #tarantool.metadata.v1
        -- early_load: true
        -- ...
        if type(box.cfg) ~= 'function' then return end
        local fio = require('fio')
        local config = require('config')
        assert(fio.cwd() == os.getenv('EXPECTED_WORKDIR'))
        if os.getenv('RETARGET') == 'true' then
            assert(fio.unlink(os.getenv('WORKDIR_LINK')))
            assert(fio.symlink('other/child', os.getenv('WORKDIR_LINK')))
        end
        assert(fio.chdir(os.getenv('OTHER_DIR')))
        require('fiber').new(function()
            config:wait_status('ready')
            local ok, err = pcall(function()
                assert(fio.cwd() == os.getenv('EXPECTED_WORKDIR'))
                assert(box.cfg.work_dir == os.getenv('REQUESTED_WORKDIR'))
                assert(box.cfg.memtx_dir == os.getenv('SNAP_DIR'))
                local s = box.schema.space.create('marker')
                s:create_index('pk')
                s:insert({1, 'prepared directory'})
                box.snapshot()
                config:reload()
                assert(#config:info().alerts == 0)
                assert(s:get(1)[2] == 'prepared directory')
            end)
            if not ok then print(tostring(err)) end
            print('snapshot discovery anchored: ' .. tostring(ok))
            os.exit(ok and 0 or 1)
        end)
    ]])
    local config = table.deepcopy(helpers.simple_config)
    config.iproto = nil
    config.console = {enabled = false}
    config.process = {work_dir = requested, pid_file = dir .. '/instance.pid'}
    config.snapshot = {dir = configured_snap_dir}
    config.wal = {dir = '.'}
    config.vinyl = {dir = '.'}
    config.app = {file = script}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {
        EXPECTED_WORKDIR = dir .. '/' .. physical,
        REQUESTED_WORKDIR = requested,
        OTHER_DIR = dir .. '/other',
        SNAP_DIR = configured_snap_dir,
        RETARGET = tostring(cg.params.kind == 'retargeted'),
        WORKDIR_LINK = dir .. '/link',
    }, {'--name', 'instance-001', '--config', 'config.yaml'},
        {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, {res.stdout, res.stderr})
    t.assert_str_contains(res.stdout, 'snapshot discovery anchored: true')
    t.assert_equals(#fio.glob(dir .. '/' .. physical .. '/' .. snap_dir ..
                             '/*.snap'), 2)
    t.assert_equals(fio.stat(dir .. '/' .. invalid_snapshot).size, 0)
    t.assert_equals(fio.glob(dir .. '/other/*.xlog'), {})
end

-- Verify discovery still follows cwd when no workdir was configured.
g.test_snapshot_discovery_without_work_dir = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'other/00000000000000000000.snap', '')
    local script = treegen.write_file(dir, 'main.lua', [[
        -- --- #tarantool.metadata.v1
        -- early_load: true
        -- ...
        assert(type(box.cfg) == 'function')
        assert(require('fio').chdir(os.getenv('OTHER_DIR')))
    ]])
    local config = table.deepcopy(helpers.simple_config)
    config.iproto = nil
    config.console = {enabled = false}
    config.process = {pid_file = dir .. '/instance.pid'}
    config.snapshot = {dir = '.'}
    config.wal = {dir = '.'}
    config.vinyl = {dir = '.'}
    config.app = {file = script}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {OTHER_DIR = dir .. '/other'},
        {'--name', 'instance-001', '--config', 'config.yaml'},
        {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 1, {res.stdout, res.stderr})
    t.assert_str_contains(res.stderr, 'Unexpected end of file')
    t.assert_equals(fio.glob(dir .. '/*.snap'), {})
end

-- Verify TT_WORK_DIR preserves its raw value and same-value box.cfg() calls.
g.test_legacy_work_dir_env = function()
    local dir = treegen.prepare_directory({}, {})
    local script = treegen.write_file(dir, 'main.lua', [[
        if rawget(_G, 'reload_started') then return end
        rawset(_G, 'reload_started', true)
        local config = require('config')
        local function check()
            assert(require('fio').cwd() == os.getenv('EXPECTED_WORKDIR'))
            assert(config:get('process.work_dir') == 'wd')
            assert(box.cfg.work_dir == 'wd')
            box.cfg({work_dir = 'wd'})
        end
        check()
        require('fiber').new(function()
            config:wait_status('ready')
            config:reload()
            check()
            assert(#config:info().alerts == 0)
            print('legacy workdir env preserved')
            os.exit(0)
        end)
    ]])
    local config = table.deepcopy(helpers.simple_config)
    config.iproto = nil
    config.app = {file = script}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    local res = justrun.tarantool(dir, {
        TT_WORK_DIR = 'wd',
        EXPECTED_WORKDIR = dir .. '/wd',
    }, {'--name', 'instance-001', '--config', 'config.yaml'},
        {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'legacy workdir env preserved')
end

g.test_snapshot_restart = function(g)
    local dir = treegen.prepare_directory({}, {})
    local config = table.deepcopy(helpers.simple_config)
    config.process = {work_dir = 'wd'}
    config.log = {level = 'debug'}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    g.server = server:new({alias = 'instance-001', chdir = dir,
                           config_file = 'config.yaml'})
    g.server:start()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:insert({1, 'recovered'})
        box.snapshot()
    end)
    g.server:restart()
    t.assert(g.server:grep_log('box_cfg.apply %(is_startup = false%)'))
    g.server:exec(function(expected)
        local config = require('config')
        t.assert_equals(box.space.test:get(1), {1, 'recovered'})
        t.assert_equals(require('fio').cwd(), expected)
        t.assert_equals(box.cfg.work_dir, 'wd')
        t.assert_equals(config:get('process.work_dir'), 'wd')
        t.assert_equals(config:info().alerts, {})
        config:reload()
        t.assert_equals(config:info().alerts, {})
    end, {dir .. '/wd'})
    t.assert_not(fio.path.exists(dir .. '/wd/wd'))
end

g.test_snapshot_missing_parents = function(g)
    for _, kind in ipairs({'workdir', 'symlink', 'unset'}) do
        local dir = treegen.prepare_directory({}, {})
        local config = table.deepcopy(helpers.simple_config)
        local work_dir = 'missing'
        local expected_dir = dir .. '/missing'
        local snap_dir = '../snap'
        config.snapshot = {dir = 'snap'}
        if kind == 'unset' then
            work_dir = nil
            expected_dir = dir
            snap_dir = 'missing/../snap'
        elseif kind == 'symlink' then
            treegen.write_file(dir, 'target/child/.keep', '')
            t.assert(fio.symlink('target/child', dir .. '/link'))
            work_dir = 'link/../missing/./deep'
            expected_dir = dir .. '/target/missing/deep'
            snap_dir = '../../snap'
            config.snapshot.dir = 'target/snap'
            -- A lexical collapse of link/.. must not discover this file.
            treegen.write_file(dir, 'snap/00000000000000000000.snap', 'invalid')
        end
        treegen.write_file(dir, 'config.yaml', yaml.encode(config))
        g.server = server:new({alias = 'instance-001', chdir = dir,
                               config_file = 'config.yaml'})
        g.server:start()
        g.server:exec(function()
            local s = box.schema.space.create('test')
            s:create_index('pk')
            s:insert({1, 'recovered'})
            box.snapshot()
        end)
        g.server:stop()
        t.assert_not(fio.path.exists(dir .. '/missing'))
        if kind ~= 'unset' then
            t.assert_not(fio.path.exists(expected_dir))
        end
        config.process = {work_dir = work_dir}
        config.snapshot.dir = snap_dir
        config.groups['group-001'].replicasets['replicaset-001']
            .instances['instance-001'].isolated = true
        config.app = {file = dir .. '/main.lua'}
        treegen.write_file(dir, 'main.lua', [[
            -- --- #tarantool.metadata.v1
            -- early_load: true
            -- ...
            if type(box.cfg) == 'function' then
                -- Initial validation must find the snapshot before this.
                -- Storage later traverses the unnormalized snapshot.dir.
                if os.getenv('CREATE_MISSING_PARENT') == 'true' then
                    local fio = require('fio')
                    assert(not fio.path.lexists('missing'))
                    assert(fio.mkdir('missing'))
                end
                return
            end
            if rawget(_G, 'reloading') then
                return
            end
            _G.reloading = true
            local config = require('config')
            assert(require('fio').cwd() == os.getenv('EXPECTED_WORKDIR'))
            assert(box.space.test:get(1)[2] == 'recovered')
            require('fiber').new(function()
                config:wait_status({'ready', 'check_warnings'})
                config:reload()
                assert(box.space.test:get(1)[2] == 'recovered')
                print('snapshot with missing parents recovered')
                os.exit(0)
            end)
        ]])
        treegen.write_file(dir, 'config.yaml', yaml.encode(config))
        local res = justrun.tarantool(dir, {
            EXPECTED_WORKDIR = expected_dir,
            CREATE_MISSING_PARENT = tostring(kind == 'unset'),
        },
            {'--name', 'instance-001', '--config', 'config.yaml'},
            {nojson = true, stderr = true})
        t.assert_equals(res.exit_code, 0, res.stderr)
        t.assert_str_contains(res.stdout,
                              'snapshot with missing parents recovered')
        g.server = nil
    end
end

g.test_direct_box_cfg_work_dir = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        local fio = require('fio')
        local startup_dir = fio.cwd()
        assert(fio.mkdir('wd'))
        box.cfg({work_dir = 'wd'})
        assert(fio.cwd() == startup_dir .. '/wd')
        assert(box.cfg.work_dir == 'wd')
        print('direct box.cfg is unchanged')
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {}, {'main.lua'},
                                {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'direct box.cfg is unchanged')
end

-- An initial miss isn't cached. A later pre-box.cfg lookup must not
-- accidentally discover a snapshot under a double-prefixed workdir.
g.test_snapshot_absence_after_entry = function(g)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'wd/wd/snap/00000000000000000000.snap',
                       'not a snapshot')
    local config = table.deepcopy(helpers.simple_config)
    config.process = {work_dir = 'wd'}
    config.snapshot = {dir = 'snap'}
    treegen.write_file(dir, 'config.yaml', yaml.encode(config))
    g.server = server:new({alias = 'instance-001', chdir = dir,
                           config_file = 'config.yaml'})
    g.server:start()
    g.server:exec(function()
        t.assert_equals(box.info.status, 'running')
        t.assert_equals(require('config'):info().alerts, {})
        t.assert_equals(box.cfg.memtx_dir, 'snap')
    end)
    t.assert_equals(#fio.glob(dir .. '/wd/snap/*.snap'), 1)
end

g.test_work_dir_preparation_errors = function()
    for _, failure in ipairs({'validation', 'creation', 'entry'}) do
        local dir = treegen.prepare_directory({}, {})
        local config = table.deepcopy(helpers.simple_config)
        config.process = {work_dir = 'wd/nested'}
        config.app = {file = dir .. '/main.lua'}
        treegen.write_file(dir, 'main.lua', [[
            -- --- #tarantool.metadata.v1
            -- early_load: true
            -- ...
            local f = assert(io.open(os.getenv('SENTINEL'), 'w'))
            f:close()
        ]])
        local expected
        if failure == 'validation' then
            config.groups['group-001'].replicasets['replicaset-001']
                .instances['instance-001'].isolated = true
            expected = 'The isolated mode is enabled'
        elseif failure == 'creation' then
            treegen.write_file(dir, 'wd', 'not a directory')
            expected = 'unable to create directory'
        else
            expected = 'unable to enter directory'
        end
        treegen.write_file(dir, 'config.yaml', yaml.encode(config))
        treegen.write_file(dir, 'check.lua', [[
            local fio = require('fio')
            local startup_dir = fio.cwd()
            local original_chdir = fio.chdir
            if os.getenv('FAILURE') == 'entry' then
                fio.chdir = function(path)
                    if path == startup_dir .. '/wd/nested' then
                        require('errno')(require('errno').EACCES)
                        return false
                    end
                    return original_chdir(path)
                end
            end
            local config = require('config')
            local ok, err = pcall(config._startup_impl, config,
                                  'instance-001', 'config.yaml')
            assert(not ok)
            assert(type(box.cfg) == 'function')
            assert(fio.cwd() == startup_dir)
            assert(not fio.path.exists(os.getenv('SENTINEL')))
            print(err)
            os.exit(0)
        ]])
        local res = justrun.tarantool(dir, {
            FAILURE = failure, SENTINEL = dir .. '/early-loaded',
        }, {'check.lua'}, {nojson = true, stderr = true})
        t.assert_equals(res.exit_code, 0, res.stderr)
        t.assert_str_contains(res.stdout, expected)
        if failure ~= 'validation' then
            t.assert_str_contains(res.stdout, 'process.work_dir')
            t.assert_str_contains(res.stdout, dir .. '/wd')
        else
            t.assert_not(fio.path.exists(dir .. '/wd'))
        end
        t.assert_not(fio.path.exists(dir .. '/early-loaded'))
    end
end
