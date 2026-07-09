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

        -- Wrong parameters.
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

-- Test the range delete during background index build.
g.test_background_index_build = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        -- Create space.
        local s = box.schema.space.create('test')
        s:create_index('pk')

        -- Do the test function during the SK build and check that the PK
        -- operations are applied to the SK after the build is finished.
        local function check(test_function)
            -- Initialize the space.
            s:truncate()
            for i = 1, 2000 do
                s:replace{i}
            end

            -- Start the background index build and make us
            -- able to check when the index build is finished.
            local index_built = false
            local f = fiber.new(function()
                s:create_index('sk')
                index_built = true
            end)
            f:set_joinable(true)
            fiber.yield() -- Start the background index build.

            -- Do the test.
            test_function()

            -- Wait until the build completes.
            assert(not index_built)
            local ok, err = f:join()
            t.assert(ok, err)
            t.assert(index_built)

            -- Check the resulting SK contents (should match with PK).
            t.assert_equals(s.index.sk:select(), s.index.pk:select())

            -- Drop the new index.
            s.index.sk:drop()
        end

        -- Same as above but wrap the function into transaction and rollback it.
        local function check_rollback(test_function)
            check(function()
                box.begin()
                test_function()
                box.rollback()
            end)
        end

        -- Concurrently delete tuple range before the build cursor.
        check(function() box.space.test:delete_range({}, {11}) end)
        check(function() box.space.test:delete_range({1}, {11}) end)
        check(function() box.space.test:delete_range({1}, {6}) end)
        check(function() box.space.test:delete_range({6}, {11}) end)
        check(function() box.space.test:delete_range({3}, {8}) end)

        -- Concurrently delete tuple range after the build cursor.
        check(function() box.space.test:delete_range({1001}, {}) end)
        check(function() box.space.test:delete_range({1001}, {2001}) end)
        check(function() box.space.test:delete_range({1501}, {2001}) end)
        check(function() box.space.test:delete_range({1301}, {1801}) end)

        -- Rollback a concurrent delete of a tuple
        -- range before and after the build cursor.
        check_rollback(function() box.space.test:delete_range({}, {}) end)
        check_rollback(function() box.space.test:delete_range({}, {2001}) end)
        check_rollback(function() box.space.test:delete_range({1}, {}) end)
        check_rollback(function() box.space.test:delete_range({1}, {2001}) end)
        check_rollback(function() box.space.test:delete_range({6}, {}) end)
        check_rollback(function() box.space.test:delete_range({6}, {2001}) end)
        check_rollback(function() box.space.test:delete_range({6}, {1501}) end)
    end)
end

-- Test the range delete during background index build (using error injections).
g.test_background_index_build_errinj = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')

        -- Create space with tuples.
        local s = box.schema.space.create('test')
        s:create_index('pk')
        for i = 1, 2000 do
            s:replace{i}
        end

        -- Start the background index build and make us
        -- able to check when the index build is finished.
        local index_built = false
        local index_builder = fiber.new(function()
            s:create_index('sk')
            index_built = true
        end)
        index_builder:set_joinable(true)
        fiber.yield() -- Start the background index build.

        -- Rollback a concurrent delete of a tuple range before and after the
        -- build cursor, but advance the cursor further before the rollback:
        -- 1. Create a fiber and wait till it yields on commit. The background
        --    SK build cursor makes it further while the fiber waits for WAL.
        -- 2. Make that fiber continue, but inject an error, so it fails to
        --    commit. This will cause it to roll back. But the index build has
        --    advanced its cursor since the on_replace has been fired for the
        --    range delete, so now we roll back more deletes than we've applied
        --    during the `memtx_build_on_replace`.
        -- 3. Drop the injection so the background index build can successfully
        --    commit itself and check that the range delete is rolled back
        --    correctly.
        local errinj = box.error.injection
        local range_deleter = fiber.new(function()
            t.assert_error_msg_content_equals('Failed to write to disk',
                                              box.space.test.delete_range,
                                              box.space.test, {6}, {1501})
        end)
        range_deleter:set_joinable(true)
        -- Let the fiber do the range delete and start commit.
        fiber.yield()
        -- Make the fiber fail to commit and roll back.
        -- Don't complete the index build yet.
        errinj.set('ERRINJ_BUILD_INDEX_DELAY', true)
        errinj.set('ERRINJ_WAL_WRITE', true)
        range_deleter:join()

        -- Let the SK build end successfully.
        assert(not index_built)
        errinj.set('ERRINJ_BUILD_INDEX_DELAY', false)
        errinj.set('ERRINJ_WAL_WRITE', false)
        local ok, err = index_builder:join()
        t.assert(ok, err)
        t.assert(index_built)

        -- Check the resulting SK contents (should match with PK).
        t.assert_equals(s.index.sk:select(), s.index.pk:select())
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
