-- https://github.com/tarantool/tarantool/issues/8946
-- Test that a space that another empty space refers to can be truncated.
local server = require('luatest.server')
local t = require('luatest')

local engines = {{engine = 'memtx'}, {engine = 'vinyl'}}
local g = t.group('gh-8946-foreign-key-truncate-test', engines)

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.client_phones then
            box.space.client_phones:drop()
        end
        if box.space.client then
            box.space.client:drop()
        end
    end)
end)

-- Field foreign key must not disable truncate if referring space is empty.
g.test_field_foreign_key_truncate = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        box.schema.space.create('client', {engine = engine})
        box.space.client:format({
            {name = 'customer_id', type = 'string', is_nullable = false},
            {name = 'esia_id', type = 'string', is_nullable = false},
        })

        box.space.client:create_index('pk_client_customer_id', {
            parts = {{field = 'customer_id', collation = 'unicode'}},
            type = 'tree', unique = true
        })
        box.space.client:create_index('idx_client_esia_id', {
            parts = {{field = 'esia_id', collation = 'unicode'}},
            type = 'tree', unique = false
        })

        box.schema.space.create('client_phones', {engine = engine})
        box.space.client_phones:format({
            {name = 'phone', type = 'string', is_nullable = false},
            {name = 'customer_id',
             foreign_key = {space = 'client', field = 'customer_id'}},
        })

        box.space.client_phones:create_index('idx_client_phones_phone', {
            parts = {{field = 'phone', collation = 'unicode'}},
            type = 'tree', unique = true
        })

        box.space.client:insert{'01','esia-01'}
        box.space.client:insert{'02','esia-02'}

        box.space.client_phones:insert{'9121234','01'}
        box.space.client_phones:insert{'3222222','02'}

        -- Now truncate is prohibited.
        t.assert_error_msg_content_equals(
            "Can't modify space 'client': space is referenced by foreign key",
            box.space.client.truncate, box.space.client)

        box.space.client_phones:truncate()

        -- Now truncate is allowed.
        box.space.client:truncate()
    end, {engine})
end

-- Tuple foreign key must not disable truncate if referring space is empty.
g.test_tuple_foreign_key_truncate = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        box.schema.space.create('client', {engine = engine})
        box.space.client:format({
            {name = 'customer_id', type = 'string', is_nullable = false},
            {name = 'esia_id', type = 'string', is_nullable = false},
        })

        box.space.client:create_index('pk_client_customer_id', {
            parts = {{field = 'customer_id', collation = 'unicode'}},
            type = 'tree', unique = true
        })
        box.space.client:create_index('idx_client_esia_id', {
            parts = {{field = 'esia_id', collation = 'unicode'}},
            type = 'tree', unique = false
        })

        box.schema.space.create('client_phones', {
            engine = engine,
            foreign_key = {space = 'client',
                           field = {customer_id = 'customer_id'}}
        })
        box.space.client_phones:format({
            {name = 'phone', type = 'string', is_nullable = false},
            {name = 'customer_id'},
        })

        box.space.client_phones:create_index('idx_client_phones_phone', {
            parts = {{field = 'phone', collation = 'unicode'}},
            type = 'tree', unique = true
        })

        box.space.client:insert{'01','esia-01'}
        box.space.client:insert{'02','esia-02'}

        box.space.client_phones:insert{'9121234','01'}
        box.space.client_phones:insert{'3222222','02'}

        -- Now truncate is prohibited.
        t.assert_error_msg_content_equals(
            "Can't modify space 'client': space is referenced by foreign key",
            box.space.client.truncate, box.space.client)

        box.space.client_phones:truncate()

        -- Now truncate is allowed.
        box.space.client:truncate()
    end, {engine})
end
