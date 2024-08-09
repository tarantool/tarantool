local t = require('luatest')
local server = require('luatest.server')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group('deprecate_replication_synchro_timeout')
--
-- gh-7486: deprecate `replication_synchro_timeout`.
--
local wait_timeout = 10

local function get_env(behavior)
    return {
        TARANTOOL_RUN_BEFORE_BOX_CFG =
            "require('compat').box_cfg_replication_synchro_timeout = \'"
                .. behavior .. "\'"
    }
end

g.before_all(function(cg)
    cg.server = server:new()
    treegen.init(cg)
end)

g.after_all(function(cg)
    cg.server:drop()
    treegen.clean(cg)
end)

g.test_option_option_behaves_as_before_if_old = function(cg)
    cg.server:restart({
        alias = 'old_behavior',
        env = get_env('old'),
    })
    cg.server:exec(function()
        -- Default value of replication_synchro_timeout with old behavior.
        t.assert_equals(box.cfg.replication_synchro_timeout, 5)
        local ok, _ = pcall(box.cfg, { replication_synchro_timeout = 239, })
        t.assert(ok)
        t.assert_equals(box.cfg.replication_synchro_timeout, 239)
    end)
end

g.test_option_deprecated_if_new = function(cg)
    cg.server:restart({
        alias = 'new_behavior',
        env = get_env('new'),
    })
    cg.server:exec(function()
        -- Default value of replication_synchro_timeout with new behavior.
        t.assert_equals(box.cfg.replication_synchro_timeout, 0)
        local ok, err = pcall(box.cfg, { replication_synchro_timeout = 5, })
        t.assert(not ok)
        t.assert_equals(err.type, 'ClientError')
        t.assert_equals(err.message, "Option 'replication_synchro_timeout' " ..
            "is deprecated")
        local ok, _ = pcall(box.cfg, { replication_synchro_timeout = 0, })
        t.assert(ok)
    end)
end

g.test_switch_option_to_new = function(cg)
    cg.server:exec(function()
        require('compat').box_cfg_replication_synchro_timeout = 'old'
        box.cfg{ replication_synchro_timeout = 5, }
        local switch_to_new = function()
            require('compat').box_cfg_replication_synchro_timeout = 'new'
        end
        local exp_err =
            "The compat option box_cfg_replication_synchro_timeout switches" ..
            " to new only if box.cfg.replication_synchro_timeout is 0," ..
            " but it is 5 now"
        t.assert_error_msg_content_equals(exp_err, switch_to_new)
        box.cfg{ replication_synchro_timeout = 0, }
        local ok, _ = pcall(switch_to_new)
        t.assert(ok)
    end)
end

for _, case in ipairs({
    {
        'test_config_module_option_new_default',
        compat_value = "'new'",
        default = 0,
    },
    {
        'test_config_module_option_old_default',
        compat_value = "'old'",
        default = 5,
    },
}) do
    g[case[1]] = function(cg)
        local dir = treegen.prepare_directory(cg, {}, {})
        local file_config = [[
            compat:
              box_cfg_replication_synchro_timeout: %s

            groups:
              group-001:
                replicasets:
                  replicaset-001:
                    instances:
                      instance-001: {}
        ]]
        local config = string.format(file_config, case.compat_value)
        treegen.write_script(dir, 'config.yaml', config)

        local script = [[
            print(box.cfg.replication_synchro_timeout)
            os.exit(0)
        ]]
        treegen.write_script(dir, 'main.lua', script)

        local env = {TT_LOG_LEVEL = 0}
        local opts = {nojson = true, stderr = false}
        local args = {'--name', 'instance-001', '--config', 'config.yaml',
                      'main.lua'}
        local res = justrun.tarantool(dir, env, args, opts)
        t.assert_equals(res.exit_code, 0)
        t.assert_equals(res.stdout, table.concat({case.default}, "\n"))
    end
end

-- We cannot check that the timeout is truly infinite,
-- but we can check that it is not 0.
for _, case in ipairs({
    {
        'test_zero_value_does_not_rollback_immediately_if_new',
        compat_value = 'new',
    },
    {
        'test_zero_value_does_not_rollback_immediately_if_old',
        compat_value = 'old',
    },
}) do
    g[case[1]] = function(cg)
        cg.server:exec(function(compat_value, wait_timeout)
            box.cfg{
                replication_synchro_quorum = 2,
                replication_synchro_timeout = 0,
            }
            box.ctl.promote()
            box.ctl.wait_rw()
            box.schema.space.create('test', {is_sync = true})
            box.space.test:create_index('pk')
            local compat = require('compat')
            local fiber = require('fiber')
            compat.box_cfg_replication_synchro_timeout = compat_value
            local f = fiber.create(function() box.space.test:insert{1} end)
            t.helpers.retrying({timeout = wait_timeout}, function()
                t.assert_equals(box.info.synchro.queue.len, 1)
            end)
            fiber.sleep(1)
            t.assert_equals(box.info.synchro.queue.len, 1)
            box.cfg{ replication_synchro_quorum = 1, }
            t.helpers.retrying({timeout = wait_timeout},
                function() t.assert_equals(f:status(), 'dead') end)
            t.assert_equals(box.info.synchro.queue.len, 0)
            box.space.test:drop()
        end, {case.compat_value, wait_timeout})
    end
end
