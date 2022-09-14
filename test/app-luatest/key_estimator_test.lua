local t = require('luatest')
local g = t.group()
local key_def = require('key_def')
local key_estimator = require('key_estimator')

local function hll_n_counters(prec)
    return 2 ^ prec
end

--[[
Get the standard error of the estimation of the HyperLogLog algorithm.
The standard error determines the probability of deviation of the estimation
from the actual value. ** let sigma = srd_err * actual_value **
The deviation is less than 1*sigma with probability of 68.2%,
                           2*sigma with probability of 95.4%,
                           3*sigma with probability of 99.7%.
]]--
local function hll_std_error(prec)
    local n_counters = hll_n_counters(prec)
    return 1.04 / (n_counters ^ 0.5)
end

g.test_constants = function()
    t.assert_not_equals(key_estimator.SPARSE, key_estimator.DENSE)
    t.assert_lt(key_estimator.MIN_PRECISION, key_estimator.MAX_PRECISION)
    t.assert_lt(key_estimator.MAX_PRECISION, key_estimator.SPARSE_PRECISION)
end

g.test_common = function()
    t.assert_error_msg_equals(
        "Usage: key_estimator.new(format = <key_def>" ..
        "[, precision = <integer>), " ..
        "representation = key_estimator.(SPARSE|DENSE)]",
        key_estimator.new)

    local fmt = key_def.new({{type = "string", fieldno = 1}})
    local est = key_estimator.new(fmt)

    t.assert_error_msg_equals(
        "Invalid precision 999 " ..
        "(available values are" ..
        " from " .. key_estimator.MIN_PRECISION ..
        " to " .. key_estimator.MAX_PRECISION .. ")",
        key_estimator.new, fmt, 999)

    t.assert_equals(est:estimate(), 0)
    est:add({'bla'})
    t.assert_equals(est:estimate(), 1)
    est:add({'bla'})
    t.assert_equals(est:estimate(), 1)
    est:add({'bla', 'bla', 'bla..'})
    t.assert_equals(est:estimate(), 1)
    est:add({'another bla'})
    t.assert_equals(est:estimate(), 2)

    t.assert_error_msg_equals("Invalid tuple format.",
        key_estimator.add, est, {0})
end

g.test_hll_dense_estimation_error = function()
    local fmt = key_def.new({{type = "string", fieldno = 1}})

    local min_prec = key_estimator.MIN_PRECISION
    local max_prec = key_estimator.MAX_PRECISION
    for prec = min_prec, max_prec, 1 do
        local est = key_estimator.new(fmt, prec)
        local std_err = hll_std_error(prec)

        --- cardinality that is more than the LinearCounting algorithm
        --- threshold, therefore, the HyperLogLog algorithm will be used.
        local card = 5 * hll_n_counters(prec)
        for i = 1, card, 1 do
            est:add({tostring(i)})
        end

        --- 99.7% to lie in expected interval. See the comment to hll_std_error.
        local margin = card * std_err * 3
        t.assert_almost_equals(est:estimate(), card, margin)
    end
end

g.test_hll_sparse_estimation_error = function()
    --- Since the precision parameter defines only the maximal size of the
    --- sparse representation there is no need in tests for all available
    --- precision values.
    local max_prec = key_estimator.MAX_PRECISION
    local fmt = key_def.new({{type = "number", fieldno = 1}})
    local est = key_estimator.new(fmt, max_prec, key_estimator.SPARSE)
    --- Error for the sparse representation can't be computed but this error
    --- must be less than error for any available precision.
    local std_err = hll_std_error(key_estimator.SPARSE_PRECISION)

    --- Sparse representation stores 4-bite pairs instead of 6-bit counters,
    --- so 32/6<6 times fewer pairs can be stored in the same amount of memory.
    local max_card = hll_n_counters(key_estimator.MAX_PRECISION) / 6
    local n_steps = key_estimator.MAX_PRECISION -
                    key_estimator.MIN_PRECISION + 1
    local card_step = max_card / n_steps

    for n = 1, n_steps, 1 do
        for i = 1, card_step, 1 do
            est:add({n*card_step + i})
        end
        local card = n * card_step
        --- 99.7% to lie in expected interval. See the comment to hll_std_error.
        local margin = card * std_err * 3
        t.assert_almost_equals(est:estimate(), card, margin)
    end
end

g.test_merge = function()
    local fmt = key_def.new({{type = "string", fieldno = 1}})

    local min_prec = key_estimator.MIN_PRECISION
    local max_prec = key_estimator.MAX_PRECISION
    for prec = min_prec, max_prec, 1 do
        local est1 = key_estimator.new(fmt, prec, key_estimator.DENSE)
        local est2 = key_estimator.new(fmt, prec, key_estimator.DENSE)
        local std_err = hll_std_error(prec)

        --- cardinality that is more than the LinearCounting algorithm
        --- threshold, therefore, the HyperLogLog algorithm will be used.
        local card = 5 * hll_n_counters(prec)
        for i = 1, card, 1 do
            if i % 2 == 0 then
                est1:add({tostring(i)})
            elseif i % 3 == 0 then
                est2:add({tostring(i)})
            else
                est1:add({tostring(i)})
                est2:add({tostring(i)})
            end
        end

        est1:merge(est2)
        --- 99.7% to lie in expected interval. See the comment to hll_std_error.
        local margin = card * std_err * 3
        t.assert_almost_equals(est1:estimate(), card, margin)
    end
end

g.test_merge_function_errors = function()
    local fmt = key_def.new({{type = "string", fieldno = 1}})
    local est1 = key_estimator.new(fmt, key_estimator.MIN_PRECISION)
    local est2 = key_estimator.new(fmt, key_estimator.MAX_PRECISION)
    --- different precision
    t.assert_error_msg_equals("Estimators cannot be merged.",
        key_estimator.merge, est1, est2)

    local formats = {
        key_def.new({{type = 'string', fieldno = 1}}),
        key_def.new({{type = 'string', fieldno = 1},
                     {type = 'string', fieldno = 2}}),
        key_def.new({{type = 'num', fieldno = 1}}),
        key_def.new({{type = 'string', is_nullable = true, fieldno = 1}})
    }
    local estimators = {}
    for i = 1, table.getn(formats), 1 do
        estimators[i] = key_estimator.new(formats[i])
    end
    for i = 1, table.getn(estimators), 1 do
        for j = i + 1, table.getn(estimators), 1 do
            t.assert_error_msg_equals("Different key formats.",
                key_estimator.merge, estimators[i], estimators[j])
        end
    end
end

g.test_hash_collisions = function()
    --- The implementation of the HyperLogLog algorithm works with 64-bit
    --- hashes, but tuple_hash computes only 32-bit hashes, so expansion to
    --- 64-bits is performed. See key_estimator implementation for more details.
    --- This test verifies that this expansion decreases a number of collisions.
    --- Starting from  2^32 / 30 (< 2 ^ 28 ) the corrections is needed for the
    --- estimation if 32-bit hashes are used.
    --- If hashes were expanded correctly there is no need in the correction.
    local big_card = 2 ^ 28
    local fmt = key_def.new({{type = "number", fieldno = 1}})
    local prec = 14
    local est = key_estimator.new(fmt, prec)

    for i = 1, big_card, 1 do
        est:add({i});
    end

    --- 99.7% to lie in expected interval. See the comment to hll_std_error.
    local std_err = hll_std_error(prec)
    local margin = big_card * std_err * 3
    t.assert_almost_equals(est:estimate(), big_card, margin)
end
