local t = require('luatest')
local server = require('luatest.server')

local function before_all(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end

local function after_all(cg)
    cg.server:drop()
end

local function after_each(cg)
    cg.server:exec(function()
        if box.space.test then box.space.test:drop() end
    end)
end

local g = t.group('gh-8157-1', {{engine = 'memtx'}, {engine = 'vinyl'}})
g.before_all(before_all)
g.after_all(after_all)
g.after_each(after_each)

-- Test default field values.
g.test_basics = function(cg)
    cg.server:exec(function(engine)
        local format = {
            {name='c1', type='any'},
            {name='c2', type='any', default={key='val'}},
            {name='id', type='unsigned', default=0},
            {name='c4', type='integer', is_nullable=true, default=-100500},
            {name='c5', type='unsigned', is_nullable=false, default=0},
            {name='c6', type='string', is_nullable=true, default='hello'}
        }
        local opts = {engine = engine, format = format}
        local s = box.schema.space.create('test', opts)
        s:create_index('pk', {parts={'id'}})
        local sk1 = s:create_index('sk1', {parts={'c4'}})
        local sk2 = s:create_index('sk2', {parts={{'c5'}, {'c6'}},
                                           unique=false})
        -- c5 and c6 are null
        s:insert{11, 12, 13, 14}
        t.assert_equals(sk1:select{14}, {{11, 12, 13, 14, 0, 'hello'}})

        -- c4 is null
        s:insert{21, 22, 23, nil, 25, '26'}
        t.assert_equals(sk2:select{25}, {{21, 22, 23, -100500, 25, '26'}})

        -- c2, c5, c6 are null
        s:insert{nil, nil, 33, 34, box.NULL}
        t.assert_equals(s:select{33}, {{nil, {key='val'}, 33, 34, 0, 'hello'}})

        -- c6 is null + more fields
        s:insert{41, 42, 43, 44, 45, nil, ',', 'world', '!'}
        t.assert_equals(s:select{43}, {{41, 42, 43, 44, 45, 'hello',
                                        ',', 'world', '!'}})

        -- Primary indexed field id is null
        s:insert{51, 52, nil, 54, 55, ''}
        t.assert_equals(s:select{0}, {{51, 52, 0, 54, 55, ''}})
    end, {cg.params.engine})
end

-- Test default field values with UPDATE and UPSERT operations.
g.test_update_upsert = function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine=engine})
        s:create_index('pk')
        s:insert{1000, nil, 'qwerty'}

        -- Check that s:format(format) is successful although the space contains
        -- a tuple with non-nullable field `name', which is null.
        local format = {{name='id', type='unsigned'},
                        {name='name', type='string', default='Noname'},
                        {name='pass', type='string'},
                        {name='shell', type='string', default='/bin/sh',
                         is_nullable=true}}
        t.assert_equals(s:format(format), nil)

        -- Note that existing tuples are not updated automatically by s:format()
        t.assert_equals(s:select{1000}, {{1000, nil, 'qwerty'}})

        -- Check that UPDATE does not change the `name' field, which is null.
        s:update({1000}, {{'=', 'pass', 'secret'}})
        t.assert_equals(s:select{1000}, {{1000, nil, 'secret'}})

        -- Test UPSERT (acts as UPDATE)
        s:upsert({1000, nil, 'love'}, {{'=', 'pass', '123456'}})
        t.assert_equals(s:select{1000}, {{1000, nil, '123456'}})

        -- Test UPSERT (acts as INSERT)
        s:upsert({1001, nil, 'god'}, {})
        t.assert_equals(s:select{1001}, {{1001, 'Noname', 'god', '/bin/sh'}})
    end, {cg.params.engine})
end

-- Test default field values in a space with the field_count option set.
g.test_exact_field_count = function(cg)
    cg.server:exec(function(engine)
        local format = {{name='id', type='integer'},
                        {name='id1', type='integer'},
                        {name='id2', type='integer', default=0}}
        local opts = {engine = engine, format = format, field_count = 3}
        local s = box.schema.space.create('test', opts)
        s:create_index('pk')

        -- Default value is applied to field 3 (id2), but field 2 is null.
        t.assert_error_msg_content_equals('Tuple field 2 (id1) type does ' ..
            'not match one required by operation: expected integer, got nil',
            s.insert, s, {1})

        t.assert_equals(s:insert{2, 2}, {2, 2, 0})
        t.assert_equals(s:insert{3, 3, 3}, {3, 3, 3})

        t.assert_error_msg_content_equals(
            'Tuple field count 4 does not match space field count 3',
            s.insert, s, {4, 4, nil, 4})
    end, {cg.params.engine})
end

-- Test error messages.
g.test_errors = function(cg)
    cg.server:exec(function(engine)
        -- Bad type of the default value.
        local format = {{name='id', type='integer'},
                        {name='c2', type='integer', default='not_integer'}}
        local opts = {engine = engine, format = format}
        t.assert_error_msg_content_equals('Type of the default value does ' ..
            'not match tuple field 2 (c2) type: expected integer, got string',
            box.schema.space.create, 'test', opts)

        format = {{name='id', type='integer'},
                  {name='c2', type='integer', default=-1}}
        opts = {engine = engine, format = format}
        local s = box.schema.space.create('test', opts)

        -- Check upsert into a space without the primary index.
        t.assert_error_msg_content_equals(
            "No index #0 is defined in space 'test'",
            s.upsert, s, {1}, {})

        -- Check upsert with a wrong key type.
        s:create_index('pk')
        s:create_index('sk', {parts={'c2'}})
        t.assert_error_msg_content_equals('Tuple field 1 (id) type does not ' ..
            'match one required by operation: expected integer, got string',
            s.upsert, s, {'bad'}, {})

        -- Check upsert with a wrong update operation.
        t.assert_error_msg_content_equals(
            'Illegal parameters, update operation must be an array {op,..}',
            s.upsert, s, {0}, {'bad'})
        s:insert{0}
        t.assert_error_msg_content_equals(
            'Illegal parameters, update operation must be an array {op,..}',
            s.upsert, s, {0}, {'bad'})

        -- Check "duplicate key exists" error messages.
        t.assert_error_msg_content_equals('Duplicate key exists in unique ' ..
            'index "pk" in space "test" with old tuple - [0, -1] and new ' ..
            'tuple - [0, -1]', s.insert, s, {0})
        t.assert_error_msg_content_equals('Duplicate key exists in unique ' ..
            'index "sk" in space "test" with old tuple - [0, -1] and new ' ..
            'tuple - [1, -1]', s.insert, s, {1})
        s:truncate()

        -- Space format has more fields than the inserted tuple.
        -- Check that the error message complains only about c3.
        s:format{{name='id', type='integer', default=0},
                 {name='c2', type='integer', default=-1},
                 {name='c3', type='integer'}}
        t.assert_error_msg_content_equals(
            'Tuple field 3 (c3) required by space format is missing',
            s.insert, s, {0})
    end, {cg.params.engine})
end

-- Test default field values in conjunction with before_replace triggers.
g.test_triggers = function(cg)
    cg.server:exec(function(engine)
        local format = {{name='id', type='unsigned'},
                        {name='name', type='string', default='Noname'},
                        {name='pass', type='string'},
                        {name='shell', type='string', default='/bin/sh'}}
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')
        s:insert{1000, nil, '0000', '/bin/nologin'}
        s:format(format)

        local trigger1 = function(_, new)
            return box.tuple.update(new, {{'=', 3, 'hacked'}})
        end
        s:before_replace(trigger1)

        -- Check that UPSERT (acts as INSERT) applies default values.
        t.assert_equals(s:upsert({1001, nil, 'xxxxxxxxx'}, {}),
                        {1001, 'Noname', 'hacked', '/bin/sh'})

        -- Check that UPSERT (acts as UPDATE) doesn't apply the default.
        t.assert_equals(s:upsert({1000, nil, '0000', '/bin/zsh'},
                                 {{'=', 'shell', '/bin/zsh'}}),
                        {1000, nil, 'hacked', '/bin/zsh'})

        -- Check that REPLACE applies default values.
        t.assert_equals(s:replace{1000, nil, '123'},
                        {1000, 'Noname', 'hacked', '/bin/sh'})

        -- Check that defaults can be applied inside a trigger.
        local trigger2 = function(_, new)
            box.space.test:run_triggers(false)
            box.space.test:insert{9000, nil, new[3]}
        end
        s:before_replace(trigger2, trigger1)
        s:insert{1002, 'user', 'secret'}
        t.assert_equals(s:select{9000}, {{9000, 'Noname', 'secret', '/bin/sh'}})
    end, {cg.params.engine})
end

-- Test default field values with access via net.box.
g.test_netbox = function(cg)
    cg.server:exec(function(engine)
        local format = {{name='id1', type='integer', default=1000},
                        {name='id2', type='integer', default=2000},
                        {name='id3', type='integer', default=3000}}
        local opts = {engine = engine, format = format}
        local s = box.schema.space.create('test', opts)
        s:create_index('pk')
    end, {cg.params.engine})

    local netbox = require('net.box')
    local c = netbox.connect(cg.server.net_box_uri)
    local s = c.space.test

    t.assert_equals(s:insert{1, nil, 1}, {1, 2000, 1})
    t.assert_equals(s:insert{2}, {2, 2000, 3000})
    t.assert_equals(s:insert{}, {1000, 2000, 3000})
end

local g2 = t.group('gh-8157-2')
g2.before_all(before_all)
g2.after_all(after_all)
g2.after_each(after_each)

-- Test default field values after recovery from xlog and snap.
g2.test_recovery = function(cg)
    cg.server:exec(function()
        local format = {{name='id', type='integer'},
                        {name='name', type='string', default='guest'}}
        local opts = {format = format, id = 666}
        local s = box.schema.space.create('test', opts)
        s:create_index('pk')
        s:insert{-1}
        s:upsert({-2}, {})
    end)

    -- Check recovery from xlog.
    cg.server:restart()
    cg.server:exec(function()
        t.assert_equals(box.space.test:select{-1}, {{-1, 'guest'}})
        t.assert_equals(box.space.test:select{-2}, {{-2, 'guest'}})
    end)

    -- Check that xlog contains the actual default values.
    local fio = require('fio')
    local xlog = require('xlog')
    local xlog_tuples = {}
    local xlog_path = fio.pathjoin(cg.server.workdir,
                                   string.format("%020d.xlog", 0))
    for _, row in xlog.pairs(xlog_path) do
        if row.BODY.space_id == 666 then
            table.insert(xlog_tuples, row.BODY.tuple)
        end
    end
    t.assert_equals(xlog_tuples, {{-1, 'guest'}, {-2, 'guest'}})

    --- Check recovery from snap.
    cg.server:eval('box.snapshot()')
    cg.server:restart()
    cg.server:exec(function()
        t.assert_equals(box.space.test:select{-1}, {{-1, 'guest'}})
        t.assert_equals(box.space.test:select{-2}, {{-2, 'guest'}})
    end)
end
