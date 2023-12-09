local fun = require('fun')
local fio = require('fio')
local t = require('luatest')
local treegen = require('test.treegen')
local server = require('test.luatest_helpers.server')
local helpers = require('test.config-luatest.helpers')
local cluster_config = require('internal.config.cluster_config')

local g = helpers.group()

-- Verify that the example is working.
--
-- It has one conditional section that shouldn't be applied: it
-- would fail otherwise, because it contains an unknown option.
--
-- It also contains one more conditional section, which is to be
-- applied. It sets a custom process title, which is verified here.
g.test_example = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    local config_file = fio.abspath('doc/examples/config/upgrade.yaml')
    local opts = {config_file = config_file, chdir = dir}
    g.server = server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    g.server:start()
    t.assert_equals(g.server:exec(function()
        return box.cfg.custom_proc_title
    end), 'instance-001 -- in upgrade')
end

-- Basic scenario: one section that shouldn't be applied and one
-- to be applied.
g.test_basic = function(g)
    helpers.success_case(g, {
        options = {
            ['conditional'] = {
                {
                    ['if'] = 'tarantool_version < 1.0.0',
                    process = {
                        title = 'skipped section',
                    },
                },
                {
                    ['if'] = 'tarantool_version > 1.0.0',
                    process = {
                        title = 'applied section',
                    },
                },
            },
            ['process.title'] = 'main config',
        },
        verify = function()
            t.assert_equals(box.cfg.custom_proc_title, 'applied section')
        end,
    })
end

-- Verify that an unknown option is OK if it is in a sections that
-- is NOT going to be applied.
g.test_unknown_option = function(g)
    helpers.success_case(g, {
        options = {
            ['conditional'] = {
                {
                    ['if'] = 'tarantool_version < 1.0.0',
                    unknown_option = 'foo',
                },
                {
                    ['if'] = 'tarantool_version > 1.0.0',
                    process = {
                        title = 'applied section',
                    },
                },
            },
            ['process.title'] = 'main config',
        },
        verify = function()
            t.assert_equals(box.cfg.custom_proc_title, 'applied section')
        end,
    })
end

-- Verify that the last conditional sections wins if they set the
-- same option.
g.test_priority = function(g)
    helpers.success_case(g, {
        options = {
            ['conditional'] = {
                {
                    ['if'] = 'tarantool_version > 1.0.0',
                    process = {
                        title = 'foo',
                    },
                },
                {
                    ['if'] = 'tarantool_version > 1.0.0',
                    process = {
                        title = 'bar',
                    },
                },
                {
                    ['if'] = 'tarantool_version > 1.0.0',
                    process = {
                        title = 'baz',
                    },
                },
                {
                    ['if'] = 'tarantool_version < 1.0.0',
                    process = {
                        title = 'skipped',
                    },
                },

            },
        },
        verify = function()
            t.assert_equals(box.cfg.custom_proc_title, 'baz')
        end,
    })
end

-- Several incorrect configuration cases.
g.test_validation = function()
    -- 'if' should exists in each conditiona; section.
    local exp_err = '[cluster_config] conditional[1]: A conditional section ' ..
        'should have field "if"'
    t.assert_error_msg_equals(exp_err, function()
        cluster_config:validate({
            ['conditional'] = {
                {
                    process = {
                        title = 'foo',
                    },
                },
            },
        })
    end)

    -- The 'if' expression should be correct.
    local exp_err = '[cluster_config] conditional[1]: An expression should ' ..
        'be a predicate, got variable'
    t.assert_error_msg_equals(exp_err, function()
        cluster_config:validate({
            ['conditional'] = {
                {
                    ['if'] = 'incorrect',
                    process = {
                        title = 'foo',
                    },
                },
            },
        })
    end)

    -- A content of the section that is going to be applied is
    -- validated as a cluster config.
    local exp_err = '[nested_cluster_config] Unexpected field "unknown_option"'
    t.assert_error_msg_equals(exp_err, function()
        cluster_config:validate({
            ['conditional'] = {
                {
                    ['if'] = 'tarantool_version > 1.0.0',
                    unknown_option = 'foo',
                },
            },
        })
    end)
end

-- Verify that it is possible to set an option in a nested scope:
-- say, for particular instance.
g.test_basic = function(g)
    helpers.success_case(g, {
        options = {
            ['conditional'] = {
                {
                    ['if'] = 'tarantool_version > 1.0.0',
                    groups = {
                        ['group-001'] = {
                            replicasets = {
                                ['replicaset-001'] = {
                                    instances = {
                                        ['instance-001'] = {
                                            process = {
                                                title = 'applied section',
                                            },
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
            ['process.title'] = 'main config',
        },
        verify = function()
            t.assert_equals(box.cfg.custom_proc_title, 'applied section')
        end,
    })
end
