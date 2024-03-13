local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

-- Verify cluster configuration access methods.
--
-- * config:instances()
-- * config:get(<...>, {instance = <...>})
g.test_basic = function(g)
    local config = cbuilder.new()
        :set_global_option('replication.failover', 'election')
        :set_global_option('process.title', '{{ instance_name }}')

        :use_group('g-001')

        :use_replicaset('r-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})

        :use_replicaset('r-002')
        :add_instance('i-004', {})
        :add_instance('i-005', {})
        :add_instance('i-006', {})

        :use_group('g-002')

        :use_replicaset('r-003')
        :add_instance('i-007', {})
        :add_instance('i-008', {})
        :add_instance('i-009', {})

        :use_replicaset('r-004')
        :add_instance('i-010', {})
        :add_instance('i-011', {})
        :add_instance('i-012', {})

        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    cluster:each(function(server)
        server:exec(function()
            local config = require('config')

            local function instance_def(i, r, g)
                return {
                    instance_name = i,
                    replicaset_name = r,
                    group_name = g,
                }
            end

            -- Verify :instances().
            local instances = config:instances()
            t.assert_equals(instances, {
                ['i-001'] = instance_def('i-001', 'r-001', 'g-001'),
                ['i-002'] = instance_def('i-002', 'r-001', 'g-001'),
                ['i-003'] = instance_def('i-003', 'r-001', 'g-001'),
                ['i-004'] = instance_def('i-004', 'r-002', 'g-001'),
                ['i-005'] = instance_def('i-005', 'r-002', 'g-001'),
                ['i-006'] = instance_def('i-006', 'r-002', 'g-001'),
                ['i-007'] = instance_def('i-007', 'r-003', 'g-002'),
                ['i-008'] = instance_def('i-008', 'r-003', 'g-002'),
                ['i-009'] = instance_def('i-009', 'r-003', 'g-002'),
                ['i-010'] = instance_def('i-010', 'r-004', 'g-002'),
                ['i-011'] = instance_def('i-011', 'r-004', 'g-002'),
                ['i-012'] = instance_def('i-012', 'r-004', 'g-002'),
            })

            -- Verify :get(<...>, {instance = <...>}).
            local res = {}
            for instance in pairs(instances) do
                local opts = {instance = instance}
                res[instance] = config:get('process.title', opts)
            end
            t.assert_equals(res, {
                ['i-001'] = 'i-001',
                ['i-002'] = 'i-002',
                ['i-003'] = 'i-003',
                ['i-004'] = 'i-004',
                ['i-005'] = 'i-005',
                ['i-006'] = 'i-006',
                ['i-007'] = 'i-007',
                ['i-008'] = 'i-008',
                ['i-009'] = 'i-009',
                ['i-010'] = 'i-010',
                ['i-011'] = 'i-011',
                ['i-012'] = 'i-012',
            })
        end)
    end)
end

-- There is a difference in omitting the `instance` config:get()
-- option and passing the given instance explicitly. The former
-- takes into account configuration sources with the 'instance`
-- type: 'env' and 'env (default)'. The latter doesn't.
g.test_env = function(g)
    local config = cbuilder.new()
        :set_global_option('process.title', 'file')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config, {
        env = {
            TT_PROCESS_TITLE = 'env',
        },
    })
    cluster:start()

    cluster['i-001']:exec(function()
        local config = require('config')

        -- No `instance` option provided: environment variables
        -- are taken into account.
        local res = config:get('process.title')
        t.assert_equals(res, 'env')

        -- With the `instance` option the environment variables
        -- are ignored: only cluster configuration has an effect.
        local res = config:get('process.title', {instance = 'i-001'})
        t.assert_equals(res, 'file')
    end)
end

-- Attempt to pass an unknown instance name into config:get().
g.test_errors = function(g)
    local config = cbuilder.new()
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    cluster['i-001']:exec(function()
        local config = require('config')

        local exp_err = 'Unknown instance "unknown"'
        t.assert_error_msg_equals(exp_err, function()
            config:get('process.title', {instance = 'unknown'})
        end)
    end)
end
