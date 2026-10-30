local t = require('luatest')

local g = t.group('gh-8587', t.helpers.matrix{
                                is_unique = {true, false},
                                is_nullable = {true, false}})
g.before_all(function(cg)
    local server = require('luatest.server')
    cg.server = server:new{alias = 'master'}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then box.space.test:drop() end
    end)
end)

-- Test a simple functional index with all combinations of
-- unique = {true, false} and is_nullable = {true, false}.
g.test_simple = function(cg)
    cg.server:exec(function(is_unique, is_nullable)
        local opts = {format = {{name = 'id', type = 'unsigned'},
                                {name = 'f2', type = 'string',
                                 is_nullable = is_nullable}}}
        local s = box.schema.space.create('test', opts)
        s:create_index('pk')

        opts = {is_sandboxed = true,
                is_deterministic = true,
                body = "function(tuple) return {tuple[2]} end"}
        box.schema.func.create('func_ret_f2', opts)

        opts = {func = 'func_ret_f2',
                unique = is_unique,
                parts = {{field = 1, type = 'string',
                          is_nullable = is_nullable}}}
        local idx = s:create_index('idx', opts)

        if is_nullable then
            s:insert{1, box.NULL}
            s:insert{2, box.NULL}
            t.assert_equals(idx:select(), {{1}, {2}})
        end

        s:insert{3, 'aa'}
        if is_unique then
            t.assert_error_msg_content_equals(
                'Duplicate key exists in unique index "idx" in space "test" ' ..
                'with old tuple - [3, "aa"] and new tuple - [4, "aa"]',
                function() s:insert{4, 'aa'} end)
            t.assert_equals(idx:select{'aa'}, {{3, 'aa'}})
        else
            s:insert{4, 'aa'}
            t.assert_equals(idx:select{'aa'}, {{3, 'aa'}, {4, 'aa'}})
        end
    end, {cg.params.is_unique, cg.params.is_nullable})
end

-- Test a multikey multipart functional index.
g.test_multikey_multipart = function(cg)
    cg.server:exec(function(is_unique, is_nullable)
        local s = box.schema.space.create('test')
        s:format{{name = 'name', type = 'string'},
                 {name = 'address', type = 'string'},
                 {name = 'address2', type = 'string'}}
        s:create_index('name', {parts = {{field = 1, type = 'string'}}})
        local lua_code = [[function(tuple)
            local address = string.split(tuple[2])
            local ret = {}
            for _, v in pairs(address) do
                table.insert(ret, {utf8.upper(v), tuple[3]})
            end
            return ret
        end]]
        box.schema.func.create('address', {body = lua_code,
                                           is_sandboxed = true,
                                           is_deterministic = true,
                                           opts = {is_multikey = true}})
        s:create_index('addr', {unique = is_unique,
                                func = 'address',
                                parts = {{field = 1, type = 'string',
                                          is_nullable = is_nullable},
                                         {field = 2, type = 'string',
                                          is_nullable = is_nullable}}})
        s:insert{'James', 'SIS Building Lambeth London UK', '1'}
        s:insert{'Sherlock', '221B Baker St Marylebone London NW1 6XE UK', '2'}
        if is_unique then
            t.assert_error_msg_content_equals(
                'Duplicate key exists in unique index "addr" in space "test"' ..
                ' with old tuple - ["Sherlock", "221B Baker St Marylebone ' ..
                'London NW1 6XE UK", "2"] and new tuple - ["Sherlock2", "' ..
                '221B Baker St Marylebone London NW1 6XE UK", "2"]',
                function()
                    s:insert{'Sherlock2',
                             '221B Baker St Marylebone London NW1 6XE UK', '2'}
                end)
        else
            s:insert{'Sherlock2',
                     '221B Baker St Marylebone London NW1 6XE UK', '2'}
        end
    end, {cg.params.is_unique, cg.params.is_nullable})
end
