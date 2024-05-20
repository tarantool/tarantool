-- Helper of gh_10032_update_upsert_splice_test.lua
local t = require('luatest')
local varbinary = require('varbinary')

local M = {}

-- Reference function that applies update splice 'ops' to tuple-like 'array'.
local function reference_splice(array, ops, is_upsert)
    -- By design zero offset error is thrown while reading ops.
    for _, op in ipairs(ops) do
        local field = op[2]
        local pos = op[3]
        if pos == 0 then
            error('SPLICE error on field ' .. field ..
                  ': offset is out of bound', 0)
        end
    end
    array = table.copy(array)
    for _, op in ipairs(ops) do
        local strop, field, pos, cut, paste = unpack(op)
        assert(strop == ':')
        paste = tostring(paste)
        local value = array[field]
        if varbinary.is(value) then
            value = tostring(value)
        elseif type(value) ~= 'string' then
            if is_upsert then
                goto continue
            end
            error('Argument type in operation \':\' on field ' .. field ..
                  ' does not match field type: expected a string or varbinary',
                  0)
        end
        assert(pos ~= 0) -- checked before.
        if pos > #value + 1 then
            pos = #value + 1
        elseif pos < -#value - 1 then
            if is_upsert then
                goto continue
            end
            error('SPLICE error on field ' .. field ..
                  ': offset is out of bound', 0)
        elseif pos < 0 then
            pos = #value + 2 + pos
        end
        local l = string.sub(value, 1, pos - 1)
        local r = string.sub(value, pos + cut)
        value = l .. paste .. r
        if varbinary.is(array[field]) then
            array[field] = varbinary.new(value)
        else
            array[field] = value
        end
        ::continue::
    end
    return array
end

-- Test update operations.
local check_ops = {
    -- error.
    {':', 1, 1, 0, ''},
    {':', 2, 0, 0, ''},
    {':', 2, -6, 0, ''}, -- possible error.
    -- noop.
    {':', 2, 1, 0, ''},
    -- delete.
    {':', 2, 1, 1, ''},
    {':', 2, 3, 2, varbinary.new('')},
    {':', 2, 5, 1, ''},
    {':', 2, -1, 1, varbinary.new('')},
    {':', 2, -3, 2, ''},
    {':', 2, -5, 1, varbinary.new('')},
    -- insert.
    {':', 2, 1, 0, 'bbbb'},
    {':', 2, 3, 0, varbinary.new('cccc')},
    {':', 2, 5, 0, 'dddd'},
    {':', 2, -1, 0, varbinary.new('eeee')},
    {':', 2, -3, 0, 'ffff'},
    {':', 2, -5, 0, varbinary.new('gggg')},
    -- replace.
    {':', 2, 1, 2, varbinary.new('hhhh')},
    {':', 2, 3, 3, 'iiii'},
    {':', 2, 5, 1, varbinary.new('jjjj')},
    {':', 2, -1, 2, 'kkkk'},
    {':', 2, -3, 3, varbinary.new('llll')},
    {':', 2, -5, 1, 'mmmm'},
}

-- Get random update operation.
local rnd_op = function()
    return check_ops[math.random(#check_ops)]
end

-- Generate lots of test cases: update operations and expected results.
-- Test case result is map with two fields 'ok' and 'ret' that are expected
-- to get from pcall with test case update.
function M.generate_test_cases(tmpl, is_upsert)
    local cases = {}

    for _, op in ipairs(check_ops) do
        table.insert(cases, {ops = {op}})
    end
    for _ = 1, 100 do
        table.insert(cases, {ops = {rnd_op(), rnd_op()}})
    end
    for _ = 1, 200 do
        table.insert(cases, {ops = {rnd_op(), rnd_op(), rnd_op()}})
    end
    for _ = 1, 600 do
        table.insert(cases, {ops = {rnd_op(), rnd_op(), rnd_op(), rnd_op()}})
    end

    for _, test_case in pairs(cases) do
        local ok, ret = pcall(reference_splice, tmpl, test_case.ops, is_upsert)
        test_case.res = {
            ok = ok,
            ret = ret,
        }
    end

    return cases
end

-- Generate array with 2nd field of given type ('str' or 'bin').
function M.generate_tuple_template(data_type)
    assert(data_type == 'str' or data_type == 'bin')
    local tmpl = {100, '1234', 200}
    if data_type == 'bin' then
        tmpl[2] = varbinary.new(tmpl[2])
    end
    return tmpl
end

-- Get field type - 'bin' or 'str'.
-- This function is required both on host and on server, and unfortunately now
-- test engine cannot pass functions as exec arguments, so define source code.
local function field_type(field)
    local varbinary = require('varbinary')
    if type(field) == 'string' then
        return 'str'
    elseif varbinary.is(field) then
        return 'bin'
    end
end

-- Check that result of update (ok, ret as return values of the corresponding
-- pcall) comply test case's expected result.
function M.check_test_case_result(test_case, ok, ret)
    local ops = test_case.ops
    local expected_ok = test_case.res.ok
    local expected_ret = test_case.res.ret
    t.assert_equals(ok, expected_ok, ops)
    if ok then
        t.assert_equals(ret, expected_ret, ops)
        t.assert_equals(field_type(ret[2]), field_type(expected_ret[2]), ops)
    else
        t.assert_equals(tostring(ret), expected_ret, ops)
    end
end

return M
