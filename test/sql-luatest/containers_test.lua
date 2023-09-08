local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'containers'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure that it is possible to get elements from MAP и ARRAY.
g.test_containers_success = function()
    g.server:exec(function()
        local sql = [[SELECT [123, 234, 356, 467][2];]]
        t.assert_equals(box.execute(sql).rows, {{234}})

        sql = [[SELECT {'one' : 123, 3 : 'two', '123' : true}[3];]]
        t.assert_equals(box.execute(sql).rows, {{'two'}})

        sql = [[SELECT {'one' : [11, 22, 33], 3 : 'two'}['one'][2];]]
        t.assert_equals(box.execute(sql).rows, {{22}})

        sql = [[SELECT {'one' : 123, 3 : 'two', '123' : true}['three'];]]
        t.assert_equals(box.execute(sql).rows, {{}})
    end)
end

--
-- Make sure that operator [] cannot get elements from values of types other
-- than MAP and ARRAY. Also, selecting from NULL throws an error.
--
g.test_containers_error = function()
    g.server:exec(function()
        local _, err = box.execute([[SELECT 1[1];]])
        local res = "Selecting is only possible from map and array values"
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT -1[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT 1.1[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT 1.2e0[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT '1'[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT x'31'[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT true[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT UUID()[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT NOW()[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT (NOW() - NOW())[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT CAST(1 AS NUMBER)[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT CAST(1 AS SCALAR)[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT CAST(1 AS ANY)[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT CAST([1] AS ANY)[1];]])
        t.assert_equals(err.message, res)

        _, err = box.execute([[SELECT NULL[1];]])
        t.assert_equals(err.message, res)

        res = [[Failed to execute SQL statement: Selecting is not possible ]]..
              [[from NULL]]
        _, err = box.execute([[SELECT CAST(NULL AS ARRAY)[1];]])
        t.assert_equals(err.message, res)
    end)
end

--
-- Make sure that the second and the following operators do not throw type
-- error.
--
g.test_containers_followers = function()
    g.server:exec(function()
        local sql = [[SELECT [1, 2, 3][1][2];]]
        t.assert_equals(box.execute(sql).rows, {{}})

        sql = [[SELECT [1, 2, 3][1][2][3][4][5][6][7];]]
        t.assert_equals(box.execute(sql).rows, {{}})

        sql = [[SELECT ([1, 2, 3][1])[2];]]
        local _, err = box.execute(sql)
        local res = "Selecting is only possible from map and array values"
        t.assert_equals(err.message, res)
    end)
end

-- Make sure that the received element is of type ANY.
g.test_containers_elem_type = function()
    g.server:exec(function()
        local sql = [[SELECT TYPEOF([123, 234, 356, 467][2]);]]
        t.assert_equals(box.execute(sql).rows, {{'any'}})

        sql = [[SELECT [123, 234, 356, 467][2];]]
        t.assert_equals(box.execute(sql).metadata[1].type, 'any')

        sql = [[SELECT TYPEOF({'one' : 123, 3 : 'two', '123' : true}[3]);]]
        t.assert_equals(box.execute(sql).rows, {{'any'}})

        sql = [[SELECT {'one' : 123, 3 : 'two', '123' : true}[3];]]
        t.assert_equals(box.execute(sql).metadata[1].type, 'any')

        sql = [[SELECT TYPEOF({'one' : [11, 22, 33], 3 : 'two'}['one']);]]
        t.assert_equals(box.execute(sql).rows, {{'any'}})

        sql = [[SELECT {'one' : [11, 22, 33], 3 : 'two'}['one'];]]
        t.assert_equals(box.execute(sql).metadata[1].type, 'any')
    end)
end
