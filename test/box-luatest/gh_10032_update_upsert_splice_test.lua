local t = require('luatest')
local server = require('luatest.server')
local helper = require('test.box-luatest.update_upsert_splice_helper')

-- Type data stored in tuple: string or varbinary.
local types = {{type = 'str'}, {type = 'bin'}}
local g = t.group('gh-10032-update-upsert-splice', types)

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
   cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Test lots of various variants of splice (':') of tuple update.
g.test_tuple_update_splice = function(cg)
    local data_type = cg.params.type
    local tmpl = helper.generate_tuple_template(data_type)
    local test_cases = helper.generate_test_cases(tmpl)

    local tuple = box.tuple.new(tmpl)

    for _, test_case in ipairs(test_cases) do
        local ok, ret = pcall(tuple.update, tuple, test_case.ops)
        helper.check_test_case_result(test_case, ok, ret)
    end
end

-- Test lots of various variants of splice (':') of memtx space update.
g.test_memtx_update_splice = function(cg)
    local data_type = cg.params.type
    cg.server:exec(function(data_type)
        local helper = require('test.box-luatest.update_upsert_splice_helper')
        local tmpl = helper.generate_tuple_template(data_type)
        local test_cases = helper.generate_test_cases(tmpl)

        local s = box.schema.space.create('test', {engine = 'memtx'})
        s:create_index('pk')

        for _, test_case in ipairs(test_cases) do
            s:replace(tmpl)
            local ok, ret = pcall(s.update, s, tmpl[1], test_case.ops)
            helper.check_test_case_result(test_case, ok, ret)
        end
    end, {data_type})
end

-- Test lots of various variants of splice (':') of memtx space upsert.
g.test_memtx_upsert_splice = function(cg)
    local data_type = cg.params.type
    cg.server:exec(function(data_type)
        local helper = require('test.box-luatest.update_upsert_splice_helper')
        local tmpl = helper.generate_tuple_template(data_type)
        local test_cases = helper.generate_test_cases(tmpl, true)

        local s = box.schema.space.create('test', {engine = 'memtx'})
        s:create_index('pk')

        for _, test_case in ipairs(test_cases) do
            s:replace(tmpl)
            local ok, ret = pcall(s.upsert, s, tmpl, test_case.ops)
            if ok then
                ret = s:get(tmpl[1])
            end
            helper.check_test_case_result(test_case, ok, ret)
        end
    end, {data_type})
end
