local fio = require('fio')
local xlog = require('xlog')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local t = require('luatest')
local g = t.group()
local _ = nil

g.before_all(function()
    g.server = server:new({alias = 'master'})
end
)
g.after_all(function()
    _ = g.server.process and g.server:stop()
    _ = g.replica_set and g.replica_set:drop()
end)

g.before_each(function()
    if not g.server.process then
        g.server:start()
    end
end)

g.after_each(function()
    _ = g.server.process and g.server:exec(function()
        box.cfg { read_only = false }
        for _, s in pairs({'test', 'datatemp', 'temp'}) do
            local _ = box.space[s] and box.space[s]:drop()
        end
    end)
end)

-- check basic invariants of temporary spaces
g.test_temporary_create = function()
    g.server:exec(function()
        local _space = box.space._space

        local s = box.schema.space.create('temp', { type = 'temporary' })
        t.assert_equals(s.temporary, true)
        t.assert_equals(s.type, 'temporary')

        --
        -- Check automatic/manual space id.
        --
        s = box.schema.space.create('temp2', { type = 'temporary', id = 9999 })
        t.assert_equals(s.type, 'temporary')
        s:drop()

        -- If id is not specified, it will be chosen in the special range.
        s = box.schema.space.create('temp2', { type = 'temporary' })
        t.assert_equals(s.id, box.space.temp.id + 1)

        s = box.schema.space.create('temp3', { type = 'temporary' })
        t.assert_equals(s.id, box.space.temp.id + 2)

        -- Now we have an unoccupied space id box.space.temp.id + 1
        box.space.temp2:drop()

        -- But it's not chosen, because space ids grow, until they can't
        s = box.schema.space.create('temp4', { type = 'temporary' })
        t.assert_equals(s.id, box.space.temp.id + 3)
        s:drop()

        -- Now there's no more room to grow...
        box.schema.space.create('temp2', { type = 'temporary',
                                           id = box.schema.SPACE_MAX })

        -- ... therefore we start filling in the gaps
        s = box.schema.space.create('temp4', { type = 'temporary' })
        t.assert_equals(s.id, box.space.temp.id + 1)
        s:drop()

        box.space.temp2:drop()
        box.space.temp3:drop()

        --
        -- Here's all the ways you can create a data-temporary space now
        --
        s = box.schema.space.create('datatemp', { temporary = true })
        t.assert_equals(s.temporary, true)
        t.assert_equals(s.type, 'data-temporary')
        s:drop()

        s = box.schema.space.create('datatemp', { type = 'data-temporary' })
        t.assert_equals(s.temporary, true)
        t.assert_equals(s.type, 'data-temporary')
        s:drop()

        t.assert_error_msg_contains(
            "only one of 'type' or 'temporary' may be specified",
            box.schema.space.create, 'temp',
            { type = 'data-temporary', temporary = true }
        )

        --
        -- Here's all the ways you can create a normal space now
        --
        s = box.schema.space.create('normal', { temporary = false })
        t.assert_equals(s.temporary, false)
        t.assert_equals(s.type, 'normal')
        s:drop()

        s = box.schema.space.create('normal', { type = 'normal' })
        t.assert_equals(s.temporary, false)
        t.assert_equals(s.type, 'normal')
        s:drop()

        t.assert_error_msg_contains(
            "only one of 'type' or 'temporary' may be specified",
            box.schema.space.create, 'temp',
            { type = 'normal', temporary = false }
        )


        t.assert_error_msg_contains(
            "only one of 'type' or 'temporary' may be specified",
            box.schema.space.create, 'temp',
            { type = 'temporary', temporary = true }
        )

        t.assert_error_msg_contains(
            "unknown space type, must be one of: 'normal', 'data-temporary'," ..
            " 'temporary'",
            box.schema.space.create, 'super', { type = 'super-temporary' }
        )

        t.assert_error_msg_contains(
            "engine does not support data-temporary spaces",
            box.schema.space.create,
                'no-vinyl', { engine = 'vinyl', type = 'temporary' }
        )

        s = box.schema.space.create('datatemp', { type = 'data-temporary' })
        t.assert_error_msg_contains(
            "temporariness cannot change",
            s.alter, s, { type = 'temporary' }
        )
        t.assert_error_msg_contains(
            "temporariness cannot change",
            _space.update,
                _space, s.id, {{'=', 6, { type = 'temporary' }}}
        )

        s = box.space.temp
        t.assert_error_msg_contains(
            "temporariness cannot change",
            s.alter, s, { type = 'data-temporary' }
        )
        t.assert_error_msg_contains(
            "temporariness cannot change",
            _space.update,
                _space, s.id, {{'=', 6, { type = 'data-temporary' }}}
        )
    end)
end

-- check which features aren't supported for temporary spaces
g.test_temporary_dont_support = function()
    g.server:exec(function()
        local s = box.schema.space.create('temp', { type = 'temporary',
                                                        id = 1111111111 })
        s:format {{'k', 'unsigned'}}
        s:create_index('pk')
        box.schema.func.create('foo', {
            is_sandboxed = true,
            is_deterministic = true,
            body = "function() end"
        })
        t.assert_error_msg_equals(
            "temporary space does not support functional indexes",
            s.create_index, s, 'func', { func = 'foo' }
        )

        t.assert_error_msg_equals(
            "temporary space does not support privileges",
            box.schema.user.grant, 'guest', 'read', 'space', s.name
        )
    end)
end

-- check which sql features aren't supported for temporary spaces
g.test_temporary_sql_dont_support = function()
    g.server:exec(function()
        local _space = box.space._space

        box.schema.space.create('temp', { type = 'temporary',
                                              id = 1111111112 })
        box.schema.space.create('test', { format = { 'k', 'unsigned' } })

        local function sql(stmt)
            local ok, err = box.execute(stmt)
            local _ = ok == nil and error(err)
        end

        t.assert_error_msg_contains(
            "triggers are not supported for temporary spaces",
            sql, [[
                create trigger "my_trigger" before insert on "temp"
                for each row begin
                    insert into "temp" values (1);
                end;
            ]]
        )

        t.assert_error_msg_contains(
            "sequences are not supported for temporary spaces",
            sql, [[
                alter table "temp"
                    add column "k" integer primary key autoincrement
            ]]
        )

        -- alter table works though
        sql([[ alter table "temp" add column "k" int primary key ]])

        t.assert_error_msg_contains(
            "foreign space can't be temporary",
            sql, [[
                create table "my_table" (
                    "id" integer primary key,
                    "k" integer,
                    foreign key ("k") references "temp" ("k")
                )
            ]]
        )

        t.assert_error_msg_contains(
            "temporary space does not support constraints",
            sql, [[
                alter table "temp"
                add constraint "fk" foreign key ("k") references "test" ("k")
            ]]
        )

        -- CREATE VIEW

        local function assert_create_view_not_supported(create_view_sql)
            t.assert_error_msg_contains(
                "CREATE VIEW does not support temporary spaces",
                sql, create_view_sql
            )
            t.assert_error_msg_contains(
                "CREATE VIEW does not support temporary spaces",
                _space.insert,
                    _space, {
                        9999, 1, 'my_view', 'memtx', 1,
                        { sql = create_view_sql, view = true },
                        {{ type = 'unsigned', name = 'ID',
                           nullable_action = 'none', is_nullable = true }},
                    }
            )
        end

        box.space._session_settings:update({'sql_seq_scan'}, {{'=', 2, true}})
        assert_create_view_not_supported([[
            create view "my_view" as select * from "temp"
        ]])

        assert_create_view_not_supported([[
            create view "my_view" as
            select * from (select * from (select * from "temp"))
        ]])

        assert_create_view_not_supported([[
            create view "my_view" as
            select * from (select 1 x) where x in (select * from "temp")
        ]])

        assert_create_view_not_supported([[
            create view "my_view" as values ((select * from "temp"))
        ]])

        assert_create_view_not_supported([[
            create view "my_view" as
            values (1) union select * from "temp"
        ]])

        -- CHECK constraints

        t.assert_error_msg_contains(
            "temporary space does not support constraints",
            sql, [[
                alter table "temp" add constraint "c2" check ( "k" > 0 )
            ]]
        )
    end)
end

-- check that CRUD operations on space meta-data works in read-only mode
g.test_temporary_read_only = function()
    g.server:exec(function()
        local _space, _index = box.space._space, box.space._index
        box.cfg { read_only = true }
        t.assert(box.info.ro)

        t.assert_error_msg_contains(
            "Can't modify data on a read-only instance",
            box.schema.space.create, 'datatemp', { temporary = true }
        )

        -- space create works
        local s = box.schema.space.create('temp', { type = 'temporary',
                                                        id = 1111111113 })

        -- space rename works
        s:rename('newname')
        t.assert_equals(s.name, box.space.newname.name)
        _space:update(s.id, {{'=', 3, 'temp'}})
        t.assert_equals(s.name, box.space.temp.name)

        -- format change works
        t.assert_equals(s:format(), {})
        s:format {{'k', 'number'}}
        t.assert_equals(s:format(), {{name = 'k', type = 'number'}})
        s:alter { format = {{'k', 'number'}, {'v', 'any'}} }
        t.assert_equals(
            s:format(),
            {{name = 'k', type = 'number'}, {name = 'v', type = 'any'}}
        )

        -- index create works
        local i = s:create_index('first', { type = 'HASH' })
        t.assert(s.index.first)

        -- index rename works
        i:rename('pk')
        t.assert_equals(i.name, s.index.pk.name)
        _index:update({s.id, i.id}, {{'=', 3, 'first'}})
        t.assert_equals(i.name, s.index.first.name)

        -- index alter works
        t.assert_equals(i.type, 'HASH')
        i:alter { type = 'TREE' }
        t.assert_equals(i.type, 'TREE')

        -- on_replace triggers even work
        local tbl = {}
        s:on_replace(function(_, new_tuple) table.insert(tbl, new_tuple) end)
        t.assert_equals(#s:on_replace(), 1)
        s:auto_increment{'foo'}
        t.assert_equals(tbl, {{1, 'foo'}})
        s:on_replace(nil, s:on_replace()[1])
        t.assert_equals(#s:on_replace(), 0)

        -- truncate works
        t.assert_equals(s:len(), 1)
        s:truncate()
        t.assert_equals(s:len(), 0)

        box.space._session_settings:update({'sql_seq_scan'}, {{'=', 2, true}})
        -- basic sql works
        box.execute [[ insert into "temp" values (420, 69), (13, 37) ]]
        t.assert_equals(
            box.execute [[ select * from "temp" ]].rows,
            {{13, 37}, {420, 69}}
        )
        box.execute [[ truncate table "temp" ]]
        local count = box.execute [[ select count(*) from "temp" ]].rows[1]
        t.assert_equals(count, {0})
        box.execute [[ drop table "temp" ]]
        t.assert(not box.space.temp)

        s = box.schema.space.create('temp', { type = 'temporary',
                                                  id = 1111111114 })

        -- all kinds of indexes work
        s:create_index('tree', { type = 'TREE' })
        s:create_index('hash', { type = 'HASH' })
        s:create_index('rtree', {
            type = 'RTREE', unique = false, parts = {2, 'array'}
        })
        s:create_index('bitset', {
            type = 'BITSET', unique = false, parts = {3, 'unsigned'}
        })

        local row = box.tuple.new {1, {2, 3}, 4}
        s:insert(row)

        t.assert_equals(s.index.hash:get(1), row)
        t.assert_equals(s.index.tree:get(1), row)
        t.assert_equals(s.index.rtree:select({2, 3}), {row})
        t.assert_equals(s.index.bitset:select(4), {row})

        s:truncate()

        -- index drop works
        s.index.bitset:drop()
        s.index.rtree:drop()
        s.index.hash:drop()
        s.index.tree:drop()

        -- space drop works
        s:drop()
    end)
end

-- check temporary space definitions aren't replicated
g.test_meta_data_not_replicated = function()
    g.server:stop()
    g.replica_set = replica_set:new{}
    local replication = {
        server.build_listen_uri('master', g.replica_set.id),
        server.build_listen_uri('replica_1', g.replica_set.id),
    }
    g.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = { read_only = false, replication = replication },
    }
    g.replica_set:build_and_add_server{
        alias = 'replica_1',
        box_cfg = { read_only = true, replication = replication },
    }
    g.replica_set:start()

    g.master = g.replica_set:get_server('master')
    g.master:exec(function()
        box.schema.space.create('temp', { type = 'temporary',
                                              id = 1111111115 })
        box.schema.space.create('datatemp', { temporary = true })
    end)

    g.replica_1 = g.replica_set:get_server('replica_1')
    g.replica_1:wait_for_vclock_of(g.master)
    -- temporary is not replicated via relay
    g.replica_1:exec(function()
        t.assert(box.space.datatemp)
        t.assert(not box.space.temp)
    end)

    g.replica_2 = g.replica_set:build_and_add_server{
        alias = 'replica_2',
        box_cfg = { replication = replication },
    }
    g.replica_2:start{wait_until_ready = true}
    g.replica_2:wait_for_vclock_of(g.master)
    -- temporary is not replicated via snapshot
    g.replica_2:exec(function()
        t.assert(box.space.datatemp)
        t.assert(not box.space.temp)
    end)
end

g.after_test('test_meta_data_not_replicated', function()
    g.replica_set:drop()
end)

local function check_wal_and_snap(expected)
    local id_min = g.server:eval 'return box.schema.SPACE_ID_TEMPORARY_MIN'

    local function is_meta_local_space_id(id)
        return type(id) == 'number' and (id >= id_min or id == 9999)
    end

    local function contains_meta_local_space_id(entry)
        local key, tuple = entry.BODY.key, entry.BODY.tuple
        return key and key:pairs():any(is_meta_local_space_id)
            or tuple and tuple:pairs():any(is_meta_local_space_id)
    end

    for _, f in pairs(fio.listdir(g.server.workdir)) do
        if not f:endswith('.xlog') and not f:endswith('.xlog') then
            goto continue
        end

        local path = fio.pathjoin(g.server.workdir, f)
        local x = xlog.pairs(path)
            :filter(contains_meta_local_space_id)
            :totable()
        if #x > 0 then
            x = x[1]
            x = {
                type = x.HEADER.type,
                space_id = x.BODY.space_id,
                tuple = x.BODY.tuple,
                key = x.BODY.key,
            }
            t.assert_equals({f, x}, {f, expected})
        end

        ::continue::
    end
end

-- check temporary space definitions aren't persisted
g.test_meta_data_not_persisted = function()
    g.server:exec(function()
        -- temporary space's metadata will go into WAL and snapshot
        local s = box.schema.space.create('datatemp', { temporary = true })
        s:create_index('pk')
        s:put{1,2,3}
        s:truncate()

        -- temporary space's metadata will not go into WAL or snapshot
        s = box.schema.space.create('temp', { type = 'temporary',
                                                  id = 1111111116 })
        s:create_index('pk')
        s:put{1,2,3}
        s:truncate()

        -- there's nothing special about space ids
        s = box.schema.space.create('test', { type = 'temporary',
                                              id = 9999 })
        s:create_index('pk')
        s:put{1,2,3}
        s:truncate()
    end)

    check_wal_and_snap({})

    g.server:restart()

    g.server:exec(function()
        -- temporary space exists after restart
        t.assert(box.space.datatemp)
        -- temporary space doesn't
        t.assert(not box.space.temp)

        box.snapshot()
    end)

    check_wal_and_snap({})
end
