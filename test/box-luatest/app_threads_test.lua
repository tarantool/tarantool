local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {
            iproto_threads = 2,
            app_threads = 4,
        },
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_no_such_thread = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    t.assert_error_covers({
        type = 'ClientError',
        name = 'NO_SUCH_THREAD',
        thread_id = 99,
    }, conn.eval, conn, 'return true', {}, {_thread_id = 99})
    conn:close()
end

g.test_unable_to_process_in_thread = function(cg)
    local function check(func, ...)
        local args = {...}
        local opts = {}
        table.insert(args, opts)
        -- Check that the request succeeds if thread_id == 0.
        opts._thread_id = 0
        func(unpack(args))
        -- Check that the request fails if thread_id ~= 0.
        opts._thread_id = 1
        local err = {
            type = 'ClientError',
            name = 'UNABLE_TO_PROCESS_IN_THREAD',
            thread_id = 1,
        }
        t.assert_error_covers(err, func, unpack(args))
        -- Check the asynchronous request path.
        opts.is_async = true
        local fut = func(unpack(args))
        t.assert_error_covers(err, function()
            local ok, err = fut:wait_result()
            if not ok then
                error(err)
            end
        end)
    end
    local conn = net.connect(cg.server.net_box_uri)
    t.assert(conn:ping())
    t.assert(conn:ping({_thread_id = 0}))
    t.assert_not(conn:ping({_thread_id = 1}))
    check(conn.watch_once, conn, 'foo')
    check(conn.eval, conn, 'return true', {})
    check(conn.call, conn, 'tonumber', {'1'})
    check(function(opts) return conn:prepare('select true', {}, nil, opts) end)
    check(function(opts) return conn:execute('select true', {}, nil, opts) end)
    local stream = conn:new_stream()
    check(stream.begin, stream, {})
    check(stream.commit, stream, {})
    check(stream.rollback, stream)
    local space = conn.space._schema
    check(space.select, space, {})
    check(space.insert, space, {'foo', 'bar'})
    check(space.replace, space, {'foo', 'buzz'})
    check(space.update, space, {'foo'}, {{'=', 2, 'fuzz'}})
    check(space.upsert, space, {'foo'}, {{'=', 2, 'fuss'}})
    check(space.delete, space, {'foo'})
    conn:close()
end
