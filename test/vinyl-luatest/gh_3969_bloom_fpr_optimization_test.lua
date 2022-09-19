local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

--
-- Difficulty: inside of a server:exec block you may call only local things.
-- Idea: we can compensate the lack of global valyes and functions with calls to common table
--

g.before_all(function()
    g.server = server:new{alias = 'master'}
    g.server:start()

    g.update_common_bloom_size_value = function()
        g.server:exec(function()
            local common = require('test.vinyl-luatest.common')
            local index = box.space[common.default_test_space_name].index[common.default_primary_index_name]
            common.update_bloom_size(index:stat().disk.bloom_size)
        end)
    end
    g.assert_common_current_bloom_size_is_bigger = function()
        g.server:exec(function()
            local common = require('test.vinyl-luatest.common')
            local t = require('luatest')
            t.assert_lt(common.previous_bloom_size, common.bloom_size)
        end)
    end
    g.create_default_space = function()
        g.server:exec(function()
            local common = require('test.vinyl-luatest.common')
            local s = box.schema.create_space(common.default_test_space_name, {engine = 'vinyl'})
            s:create_index(common.default_primary_index_name, {lookup_cost_coeff = common.selected_lookup_cost_coeff})
        end)
    end
    g.drop_default_space = function()
        g.server:exec(function()
            local common = require('test.vinyl-luatest.common')
            common.bloom_size = 0
            common.previous_bloom_size = 0
            common.counter = 0
            common.selected_lookup_cost_coeff = 0
            box.space[common.default_test_space_name]:drop()
        end)
    end

    g.big_insert = function()
        g.server:exec(function()
            local common = require('test.vinyl-luatest.common')
            local s = box.space[common.default_test_space_name]
            common.big_insert(s, common.default_big_insert_value_count)
        end)
    end
    g.big_update = function()
        g.server:exec(function()
            local common = require('test.vinyl-luatest.common')
            local s = box.space[common.default_test_space_name]
            common.big_update(s)
        end)
    end
    g.assert_count_equals = function()
        g.server:exec(function()
            local common = require('test.vinyl-luatest.common')
            local t = require('luatest')
            local s = box.space[common.default_test_space_name]
            t.assert_equals(s:count(), common.counter)
        end)
    end
    g.double_vinyl_memory = function()
        g.server:exec(function()
            local common = require('test.vinyl-luatest.common')
            box.cfg{vinyl_memory=common.default_vinyl_memory * 2}
        end)
    end
    g.quadruple_vinyl_memory = function()
        g.server:exec(function()
            local common = require('test.vinyl-luatest.common')
            box.cfg{vinyl_memory=common.default_vinyl_memory * 4}
        end)
    end
end)

g.after_all(function()
    g.server:drop()
end)


--
-- Checks that bloom_size of index with lookup_cost_coeff = 1 is 0
--
g.test_bloom_size_is_zero_when_lookup_cost_coeff_is_max = function()
    g.server:exec(function()
        local common = require('test.vinyl-luatest.common')
        common.selected_lookup_cost_coeff = common.max_lookup_cost_coeff_value
    end)
    g.create_default_space()
    g.big_insert()
    g.update_common_bloom_size_value()
    g.server:exec(function()
        local common = require('test.vinyl-luatest.common')
        local t = require('luatest')
        t.assert_equals(0, common.bloom_size)
    end)
    g.drop_default_space()
end

--
-- Checks that bloom_size actually increasing when setting less lookup_cost_coeff
--
g.test_bloom_size_decreasing_with_lookup_coeff_increase = function()
    g.server:exec(function()
        local common = require('test.vinyl-luatest.common')
        common.selected_lookup_cost_coeff = common.big_lookup_cost_coeff_value
    end)
    g.create_default_space()
    g.big_insert()
    g.update_common_bloom_size_value()
    g.drop_default_space()

    g.server:exec(function()
        local common = require('test.vinyl-luatest.common')
        common.selected_lookup_cost_coeff = common.middle_lookup_cost_coeff_value
    end)
    g.create_default_space()
    g.big_insert()
    g.update_common_bloom_size_value()
    g.assert_common_current_bloom_size_is_bigger()
    g.drop_default_space()

    g.server:exec(function()
        local common = require('test.vinyl-luatest.common')
        common.selected_lookup_cost_coeff = common.small_lookup_cost_coeff_value
    end)
    g.create_default_space()
    g.big_insert()
    g.update_common_bloom_size_value()
    g.assert_common_current_bloom_size_is_bigger()

    g.drop_default_space()
end

--
-- Checks that algorithm of optimal bloom_fpr calculation does not break
-- after changing the vinyl_memory value (dump size increase) (gh-3969).
--
g.test_change_vinyl_memory = function()
    g.server:exec(function()
        local common = require('test.vinyl-luatest.common')
        common.selected_lookup_cost_coeff = common.big_lookup_cost_coeff_value
    end)

    g.create_default_space()

    g.big_insert()
    g.assert_count_equals()
    g.quadruple_vinyl_memory()
    g.big_insert()
    g.assert_count_equals()

    g.drop_default_space()
end
