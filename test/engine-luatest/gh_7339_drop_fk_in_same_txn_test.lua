local t = require('luatest')
local server = require('luatest.server')

local engines = {'memtx', 'vinyl'}

local g = t.group('Drop constr and referenced obj in txn', t.helpers.matrix{
    engine_a = engines, engine_b = engines,
    alter = {true, false}
})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(engine_a, engine_b)
        box.schema.func.create('ck1', {
            is_deterministic = true,
            body = "function(x) return x[1] > 5 end"
        })
        box.schema.func.create('ck2', {
            is_deterministic = true,
            body = "function(x) return x < 20 end"
        })
        local a = box.schema.space.create('a', {
            format = {
                {name = 'i', type = 'integer'},
                {name = 'j', type = 'integer'}
            },
            engine = engine_a
        })
        a:create_index('pk', {parts = {{1}}})
        a:create_index('sk', {parts = {{2}}})
        local b = box.schema.space.create('b', {
            constraint = "ck1",
            foreign_key = {tup_fk = {space = 'a', field = {[2] = 2}}},
            format = {
                {
                    name = 'i', type = 'integer',
                    foreign_key = {space = 'a', field = 'i'},
                    constraint = "ck2"
                },
                {name = 'j', type = 'integer'}
            },
            engine = engine_b
        })
        b:create_index('pk', {parts = {{1}}})
        b:create_index('sk', {parts = {{2}}})
    end, {cg.params.engine_a, cg.params.engine_b})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.a ~= nil then box.space.a:drop() end
        if box.space.b ~= nil then box.space.b:drop() end
        if box.func.ck1 ~= nil then box.func.ck1:drop() end
    end)
end)

g.test_constraints_alter_in_txn = function(cg)
    cg.server:exec(function(alter)
        local a = box.space.a
        local b = box.space.b
        box.begin()
        if alter then
            b:alter({
                foreign_key = {},
                constraint = {},
                format = {
                    {name = 'i', type = 'integer'},
                    {name = 'j', type = 'integer', is_nullable = true}
                }
            })
        else
            b:drop()
        end
        a:drop()
        box.func.ck1:drop()
        box.rollback()
        a:insert{100, 100}
        -- Will fail with error
        local msg = "Foreign key constraint 'a' failed for field '1 (i)': " ..
            "foreign tuple was not found"
        t.assert_error_msg_content_equals(msg, b.insert, b, {10, 100})
        a:insert{10, 200}
        msg = "Foreign key constraint 'tup_fk' failed: " ..
            "foreign tuple was not found"
        t.assert_error_msg_content_equals(msg, b.insert, b, {10, 10})
        a:replace{10, 10}
        b:replace{10, 10}
        a:insert{0, 0}
        msg = "Check constraint 'ck1' failed for tuple"
        t.assert_error_msg_content_equals(msg, b.insert, b, {0, 0})
        a:insert{30, 30}
        msg = "Check constraint 'ck2' failed for field '1 (i)'"
        t.assert_error_msg_content_equals(msg, b.insert, b, {30, 30})
        box.begin()
        if alter then
            b:alter({
                foreign_key = {},
                constraint = {},
                format = {
                    {name = 'i', type = 'integer'},
                    {name = 'j', type = 'integer', is_nullable = true}
                }
            })
        else
            b:drop()
        end
        a:drop()
        box.func.ck1:drop()
        box.commit()
    end, {cg.params.alter})
end
