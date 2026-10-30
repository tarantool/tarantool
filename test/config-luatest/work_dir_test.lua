local fun = require('fun')
local fio = require('fio')
local yaml = require('yaml')
local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local server = require('luatest.server')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

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
    local res = justrun.tarantool(dir, {LUA_PATH = ''}, {
        '-e', ('package.setsearchroot(%q)'):format(
            fio.pathjoin(dir, 'pinned')),
        '--name', 'instance-001', '--config', 'config.yaml',
    }, {nojson = true, stderr = true, quote_args = true,
        setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'mymod is loaded')
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
            work_dir = nil
            expected_dir = dir
            snap_dir = 'link/../snap'
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
        if kind == 'workdir' then
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
