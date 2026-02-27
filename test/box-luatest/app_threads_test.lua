local datetime = require('datetime')
local decimal = require('decimal')
local ffi = require('ffi')
local net = require('net.box')
local uuid = require('uuid')
local varbinary = require('varbinary')

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

g.test_call_eval = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    -- Call a box function in the main thread.
    t.assert_covers(conn:call('box.info', {}, {_thread_id = 0}),
                    {status = 'running'})
    -- Check that the box function is unavailable in an application thread.
    local err = {
        type = 'ClientError',
        name = 'NO_SUCH_PROC',
        func = 'box.info',
    }
    t.assert_error_covers(err, conn.call, conn, 'box.info', {},
                          {_thread_id = 1})
    -- Register a function in an application thread.
    conn:eval([[rawset(_G, 'test_func', function() return 42 end)]], {},
              {_thread_id = 1})
    -- Check that it's available in this thread, but not in other threads.
    t.assert_equals(conn:call('test_func', {}, {_thread_id = 1}), 42)
    err.func = 'test_func'
    t.assert_error_covers(err, conn.call, conn, 'test_func', {},
                          {_thread_id = 0})
    t.assert_error_covers(err, conn.call, conn, 'test_func', {},
                          {_thread_id = 2})
    -- Clear the function and check that it's no longer available.
    conn:eval([[rawset(_G, 'test_func', nil)]], {},
              {_thread_id = 1})
    t.assert_error_covers(err, conn.call, conn, 'test_func', {},
                          {_thread_id = 1})
    conn:close()
end

g.test_builtin_types_serialization = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    -- Check basic types.
    local function check(obj)
        local ret_obj = conn:eval([[return ...]], {obj}, {_thread_id = 1})
        t.assert_is(type(ret_obj), type(obj))
        if type(ret_obj) == 'cdata' then
            t.assert_is(ffi.typeof(ret_obj), ffi.typeof(obj))
        end
        t.assert_equals(ret_obj, obj)
    end
    check('foo')
    check(123)
    check(123456789123456789ULL)
    check(-123456789123456789LL)
    check(datetime.new())
    check(datetime.interval.new())
    check(decimal.new(123))
    check(uuid.new())
    check(varbinary.new('foo'))
    -- Check errors.
    local err = box.error.new({
        type = 'CustomType',
        name = 'MyError',
        message = 'foobar',
    })
    local ret_err = conn:eval([[return ...]], {err}, {_thread_id = 1})
    box.error.is(err)
    t.assert_equals(ret_err:unpack(), err:unpack())
    t.assert_error_covers(err:unpack(), conn.eval, conn, [[error(...)]], {err},
                          {_thread_id = 1})
    -- Tuples aren't supported yet (serialized as array).
    local tuple = box.tuple.new({1, 2, 3})
    local ret_type, ret_tuple = conn:eval([[
        local tuple = ...
        return type(tuple), tuple
    ]], {tuple}, {_thread_id = 1})
    t.assert_equals(ret_type, 'table')
    t.assert_equals(type(ret_tuple), 'table')
    t.assert_equals(ret_tuple, {1, 2, 3})
    conn:close()
end

g.test_os_exit_disabled = function(cg)
    t.assert_error_msg_contains(
        'Only main thread may call os.exit()',
        cg.server.eval, cg.server, [[os.exit()]], {}, {_thread_id = 1})
end

g.test_builtin_modules = function(cg)
    local function eval(expr, args)
        return cg.server:eval(expr, args or {}, {_thread_id = 1})
    end
    local function check_module(module)
        local type_ = eval([[
            local module = ...
            return type(require(module))]], {module})
        t.assert_equals(type_, 'table')
    end
    local function check_module_func(module, func)
        local type_ = eval([[
            local module, func = ...
            return type(require(module)[func])
        ]], {module, func})
        t.assert_equals(type_, 'function')
    end
    check_module('strict')
    check_module('debug')
    check_module_func('debug', 'sourcefile')
    check_module_func('debug', 'sourcedir')
    check_module('tarantool')
    check_module('log')
    check_module('fiber')
    check_module('buffer')
    check_module('fun')
    check_module('table')
    check_module_func('table', 'copy')
    check_module_func('table', 'deepcopy')
    check_module_func('table', 'equals')
    check_module('string')
    check_module_func('string', 'split')
    check_module_func('string', 'ljust')
    check_module_func('string', 'rjust')
    check_module_func('string', 'center')
    check_module_func('string', 'startswith')
    check_module_func('string', 'endswith')
    check_module_func('string', 'hex')
    check_module_func('string', 'fromhex')
    check_module_func('string', 'strip')
    check_module_func('string', 'lstrip')
    check_module_func('string', 'rstrip')
    check_module('errno')
    check_module('version')
    check_module('clock')
    check_module('iconv')
    check_module('crypto')
    check_module('digest')
    check_module('uri')
    check_module('uuid')
    check_module('decimal')
    check_module('varbinary')
    check_module('timezones')
    check_module('datetime')
    check_module('pickle')
    check_module('msgpack')
    check_module('yaml')
    check_module('json')
    t.assert_covers(eval([[
        local e = box.error.new({type = 'MyError', name = 'FooBar'})
        return e:unpack()
    ]]), {type = 'MyError', name = 'FooBar'})
end

g.test_lua_alloc = function(cg)
    t.assert_error_covers({
        message = 'not enough memory',
    }, cg.server.eval, cg.server, [[
        require('internal.alloc').setlimit(32 * 1024 * 1024)
        string.rep('x', 16 * 1024 * 1024) -- fail
    ]], {}, {_thread_id = 3})
    local limit = cg.server:eval([[
        string.rep('x', 16 * 1024 * 1024) -- ok
        collectgarbage('collect')
        return require('internal.alloc').getlimit()
    ]], {}, {_thread_id = 4})
    t.assert_equals(limit, 2 * 1024 * 1024 * 1024)
    cg.server:eval([[
        require('internal.alloc').setlimit(...)
        string.rep('x', 16 * 1024 * 1024) -- ok
        collectgarbage('collect')
    ]], {limit}, {_thread_id = 3})
end

g.test_fiber = function(cg)
    -- fiber.sleep
    t.assert_equals(cg.server:eval([[
        require('fiber').sleep(0.1)
        return ...
    ]], {'foobar'}, {_thread_id = 1}), 'foobar')

    -- fiber.slice
    local max_slice = cg.server:eval([[
        return require('fiber').self():info().max_slice
    ]], {}, {_thread_id = 1})
    t.assert_error_covers({
        message = 'fiber slice is exceeded',
    }, cg.server.eval, cg.server, [[
        local fiber = require('fiber')
        fiber.set_max_slice(0.001)
        for i = 1, 10 * 1000 * 1000 do
            fiber.check_slice()
        end
    ]], {}, {_thread_id = 1})
    t.assert_equals(cg.server:eval([[
        return require('fiber').self():info().max_slice
    ]], {}, {_thread_id = 2}), max_slice)
    cg.server:eval([[
        local max_slice = ...
        if max_slice == nil then
            max_slice = 100 * 365 * 86400 -- TIMEOUT_INFINITY
        end
        require('fiber').set_max_slice(max_slice)
    ]], {max_slice}, {_thread_id = 1})

    -- fiber.top
    t.assert_covers(cg.server:eval([[
        local fiber = require('fiber')
        fiber.top_enable()
        return fiber.top()
    ]], {}, {_thread_id = 1}), {cpu = {['1/sched'] = {}}})
    t.assert_error_covers({
        message = 'fiber.top() is disabled. ' ..
                  'Enable it with fiber.top_enable() first',
    }, cg.server.eval, cg.server, [[
        require('fiber').top()
    ]], {}, {_thread_id = 2})
    cg.server:eval([[
        require('fiber').top_disable()
    ]], {max_slice}, {_thread_id = 1})
end

g.test_log = function(cg)
    local function eval(expr, args)
        return cg.server:eval(expr, args or {}, {_thread_id = 1})
    end
    local function check_func(func, exists)
        t.assert_equals(eval([[return type(require('log')[...])]], {func}),
                        exists and 'function' or 'nil')
    end

    -- Check that logging functions are available.
    check_func('new', true)
    check_func('debug', true)
    check_func('verbose', true)
    check_func('info', true)
    check_func('warn', true)
    check_func('error', true)

    -- Check that configuration/management functions are unavailable.
    check_func('rotate', false)
    check_func('pid', false)
    check_func('level', false)
    check_func('log_format', false)
    check_func('cfg', false)
    check_func('box_api', false)

    -- Check that logging works and the thread name is logged.
    eval([[require('log').info('fuzzbuzz')]])
    t.assert_str_contains(cg.server:grep_log('.*fuzzbuzz.*'), '/app1/')

    -- Check that log levels are propagated.
    local old_level = cg.server:exec(function()
        local log = require('log')
        local old_level = log.cfg.level
        require('log').cfg({
            level = 'verbose',
            modules = {
                test1 = 'debug',
                test2 = 'info',
            },
        })
        return old_level
    end)

    eval([[
        local log = require('log')
        local log1 = require('log').new('test1')
        local log2 = require('log').new('test2')
        local log3 = require('log').new('test3')
        log.debug('foobar1')
        log.verbose('foobar2')
        log1.debug('foobar3')
        log2.verbose('foobar4')
        log2.info('foobar5')
        log3.debug('foobar6')
        log3.verbose('foobar7')
    ]])

    t.assert_not(cg.server:grep_log('foobar1'))
    t.assert(cg.server:grep_log('foobar2'))
    t.assert(cg.server:grep_log('foobar3'))
    t.assert_not(cg.server:grep_log('foobar4'))
    t.assert(cg.server:grep_log('foobar5'))
    t.assert_not(cg.server:grep_log('foobar6'))
    t.assert(cg.server:grep_log('foobar7'))

    cg.server:exec(function(old_level)
        require('log').cfg({
            level = old_level,
            modules = {},
        })
    end, {old_level})

    -- Check that log format is propagated.
    cg.server:exec(function()
        local log = require('log')
        t.assert_equals(log.cfg.format, 'plain')
        log.log_format('json')
    end)

    eval([[require('log').info('foobarbuzz')]])
    t.assert_str_contains(cg.server:grep_log('.*foobarbuzz.*'),
                          [["file": "eval"]])

    cg.server:exec(function()
        local log = require('log')
        log.log_format('plain')
    end)
end
