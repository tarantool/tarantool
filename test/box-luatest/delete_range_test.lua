local server = require('luatest.server')
local t = require('luatest')

local g = t.group('delete_range')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        if box.space.vinyl ~= nil then
            box.space.vinyl:drop()
        end
        box.schema.user.drop('test', {if_exists = true})
    end)
end)

-- Test the IPROTO interface is not implemented.
g.test_iproto = function(cg)
    cg.server:exec(function(net_box_uri)
        local uri = require('uri')
        local socket = require('socket')

        -- Connect to the server.
        local u = uri.parse(net_box_uri)
        local s = socket.tcp_connect(u.host, u.service)
        local greeting = s:read(box.iproto.GREETING_SIZE)
        greeting = box.iproto.decode_greeting(greeting)
        t.assert_covers(greeting, {protocol = 'Binary'})

        -- Send the request.
        local request = box.iproto.encode_packet(
            {request_type = box.iproto.type.DELETE_RANGE, sync = 123})
        t.assert_equals(s:write(request), #request)

        -- Read the response.
        local response = ''
        local header, body
        repeat
            header, body = box.iproto.decode_packet(response)
            if header == nil then
                local size = body
                local data = s:read(size)
                t.assert_is_not(data)
                response = response .. data
            end
        until header ~= nil
        s:close()

        t.assert_equals(body[box.iproto.key.ERROR_24],
                        "Unknown request type 18")
    end, {cg.server.net_box_uri})
end

-- Test the engines where the feature is not supported.
g.test_unsupported_engines = function(cg)
    cg.server:exec(function()
        local space = box.schema.create_space('vinyl', {engine = 'vinyl'})
        space:create_index('pk')

        t.assert_error_msg_equals('vinyl does not support range delete',
                                  space.delete_range, space, {}, {})
        t.assert_error_msg_equals('vinyl does not support range delete',
                                  space.index.pk.delete_range,
                                  space.index.pk, {}, {})
    end)
end

-- Test the indexes where the feature is not supported.
g.test_unsupported_indexes = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local i1 = s:create_index('i1', {type = 'hash'})
        local i2 = s:create_index('i2', {type = 'rtree', parts = {2, 'array'},})
        local i3 = s:create_index('i3', {type = 'bitset',
                                         parts = {3, 'unsigned'}})
        t.assert_error_covers({
            type = 'ClientError',
            message = "Index 'i1' (HASH) of space 'test' (memtx) " ..
                      "does not support delete_range()",
        }, i1.delete_range, i1, {}, {})
        t.assert_error_covers({
            type = 'ClientError',
            message = "Engine 'memtx' does not support" ..
                      " delete_range() on secondary index",
        }, i2.delete_range, i2, {}, {})
        t.assert_error_covers({
            type = 'ClientError',
            message = "Engine 'memtx' does not support" ..
                      " delete_range() on secondary index",
        }, i3.delete_range, i3, {}, {})
    end)
end

-- Test the API misusage.
g.test_schema_checks = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')

        -- No index.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_INDEX_ID',
        }, s.delete_range, s, 0.5)

        local i = s:create_index('pk')

        -- Wrong aprameters.
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'Use space:delete_range(...)' ..
                      ' instead of space.delete_range(...)',
        }, s.delete_range)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'Use index:delete_range(...)' ..
                      ' instead of index.delete_range(...)',
        }, i.delete_range)

        -- User access denied.
        box.schema.user.create('test')
        t.assert_error_covers({
            type = 'AccessDeniedError',
            user = 'test',
            object_type = 'space',
            object_name = 'test',
        }, box.session.su, 'test', i.delete_range, i, {}, {})

        -- Dropped index.
        i:drop()
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_INDEX_ID',
        }, i.delete_range, i, {}, {})

        -- Dropped space.
        s:drop()
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_SPACE',
        }, i.delete_range, i, {}, {})
    end)
end

-- Test the key range validation.
g.test_range_check = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local i = s:create_index('pk', {parts = {{1, 'unsigned'},
                                                 {2, 'unsigned'}}})

        -- Not passable key.
        local key = function() end
        local err = {
            type = 'LuajitError',
            message = "unsupported Lua type 'function'",
        }
        t.assert_error_covers(err, i.delete_range, i, key)
        t.assert_error_covers(err, i.delete_range, i, nil, key)

        -- Wrong part type.
        key = {'foo', 'bar'}
        err = {
            type = 'ClientError',
            name = 'KEY_PART_TYPE',
            message = 'Supplied key type of part 0 does not match ' ..
                      'index part type: expected unsigned',
        }
        t.assert_error_covers(err, i.delete_range, i, key)
        t.assert_error_covers(err, i.delete_range, i, nil, key)

        -- Wrong part count.
        key = {1, 2, 3}
        err = {
            type = 'ClientError',
            name = 'KEY_PART_COUNT',
            message = 'Invalid key part count (expected [0..2], got 3)',
        }
        t.assert_error_covers(err, i.delete_range, i, key)
        t.assert_error_covers(err, i.delete_range, i, nil, key)

        -- Invalid range.
        err = {
            type = 'IllegalParams',
            message = 'begin_key must be < end_key',
        }
        t.assert_error_covers(err, i.delete_range, i, {10}, {5})
        t.assert_error_covers(err, i.delete_range, i, {10}, {10})
        t.assert_error_covers(err, i.delete_range, i, {10, 10}, {10})
        t.assert_error_covers(err, i.delete_range, i, {10, 10}, {10, 5})
        t.assert_error_covers(err, i.delete_range, i, {10, 10}, {10, 10})
    end)
end

-- Check the range delete functionality.
g.test_api = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {3, 'unsigned'}}})

        -- Do the range delete on a space and expect a result.
        -- The space contents are restored after the test.
        local function check(begin_key, end_key)
            -- Save the space contents.
            local orig_space = s:select()

            -- Find contents expected after range delete.
            local before_count = s:offset_of(begin_key, 'ge')
            local after_offset = s:count(end_key, 'lt')
            local expect_before = s:select({}, {limit = before_count})
            local expect_after = s:select({}, {offset = after_offset})

            -- Do the test.
            s:delete_range(begin_key, end_key)
            t.assert_equals(s:select({}, {limit = before_count}), expect_before)
            t.assert_equals(s:select({}, {offset = before_count}), expect_after)

            -- Restore the space contents.
            s:truncate()
            for _, tuple in ipairs(orig_space) do
                s:insert(tuple)
            end
        end

        -- Check the empty space.
        check(nil, nil)
        check({}, {10})
        check({10}, {})
        check({10, 1}, {20, 2})

        -- Fill the space.
        s:insert({10, 'x', 30, 'a'})
        s:insert({10, 'y', 10, 'b'})
        s:insert({10, 'z', 20, 'c'})
        s:insert({20, box.NULL, 20})
        s:insert({30, box.NULL, 30})
        s:insert({40, box.NULL, 40})

        -- Delete everything.
        check({}, {})
        check({10}, {})
        check({10, 10}, {})
        check({5, 5}, {})
        check({0}, {})
        check({}, {40, 40})
        check({}, {45, 40})
        check({}, {50})

        -- Delete nothing.
        check({}, {10})
        check({}, {10, 10})
        check({25}, {30})
        check({30, 50}, {40, 40})
        check({30, 100}, {40})
        check({35}, {40, 35})
        check({40, 45}, {})
        check({45, 40}, {})
        check({50}, {})

        -- Full keys.
        check({10, 30}, {30, 30})
        check({10, 15}, {20, 20})
        check({10, 25}, {20, 15})
        check({10, 35}, {20, 10})

        -- Partial keys.
        check({10}, {10, 35})
        check({10}, {11})
        check({10}, {20})
        check({20}, {30})
        check({20}, {35})
        check({25}, {40})
        check({20}, {45})
        check({}, {10, 35})
        check({}, {11})
        check({}, {20})
    end)
end

local g_mvcc = t.group('delete_range-mvcc')

g_mvcc.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
end)

g_mvcc.after_all(function(cg)
    cg.server:drop()
end)

g_mvcc.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

-- Test the range delete is handled by the MVCC.
g_mvcc.test_mvcc = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        for i = 1, 100 do
            s:insert({i})
        end

        -- Prepare proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        local conflict = {{error = "Transaction has been aborted by conflict"}}
        local success = ''

        -- Rollback a range delete of a single element.
        tx1:begin()
        tx1('box.space.test:delete_range(11, 12)')
        t.assert_equals(tx1('box.space.test:len()')[1], 99)
        tx1:rollback()
        t.assert_equals(s:len(), 100)

        -- Commit a range delete of a single element.
        tx1:begin()
        tx1('box.space.test:delete_range(11, 12)')
        t.assert_equals(tx1('box.space.test:len()')[1], 99)
        tx1:commit()
        t.assert_equals(s:len(), 99)

        -- Rollback a range delete of multiple elements.
        tx1:begin()
        tx1('box.space.test:delete_range(12, 16)')
        t.assert_equals(tx1('box.space.test:len()')[1], 95)
        tx1:rollback()
        t.assert_equals(s:len(), 99)

        -- Commit a range delete of multiple elements.
        tx1:begin()
        tx1('box.space.test:delete_range(12, 16)')
        t.assert_equals(tx1('box.space.test:len()')[1], 95)
        tx1:commit()
        t.assert_equals(s:len(), 95)

        -- Delete one common (delete during delete_range).
        tx1:begin()
        tx1('box.space.test:delete_range(16, 21)')
        tx2:begin()
        tx2('box.space.test:delete(18)')
        t.assert_equals(tx2:commit(), success)
        t.assert_equals(tx1:commit(), conflict)
        t.assert_equals(s:len(), 94)

        -- Delete one common (delete_range during delete).
        tx1:begin()
        tx1('box.space.test:delete(17)')
        tx2:begin()
        tx2('box.space.test:delete_range(16, 21)')
        t.assert_equals(tx2:commit(), success)
        t.assert_equals(tx1:commit(), conflict)
        t.assert_equals(s:len(), 90)

        -- Delete multiple common.
        tx1:begin()
        tx1('box.space.test:delete_range(11, 31)')
        tx2:begin()
        tx2('box.space.test:delete_range(1, 41)')
        t.assert_equals(tx1:commit(), success)
        t.assert_equals(tx2:commit(), conflict)
        t.assert_equals(s:len(), 80)
    end)
end

-- Test the range delete is handled by the MVCC (using error injections).
g_mvcc.test_mvcc_errinj = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        for i = 1, 10 do
            s:insert({i})
        end

        -- Prepare proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        local tx3 = txn_proxy.new()
        local write_error = {{error = "Failed to write to disk"}}

        -- Relink stories with range deletes
        -- during replace preparation and rollback.
        tx1:begin()
        tx1('box.space.test:replace({8, 2, 2})')
        tx2:begin()
        tx2('box.space.test:delete_range(6, 11)')
        tx3:begin()
        tx3('box.space.test:delete_range(7, 10)')
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        t.assert_equals(tx1:commit(), write_error)
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        tx2:rollback()
        tx3:rollback()
        t.assert_equals(s:len(), 10)
    end)
end
