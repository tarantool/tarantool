local datetime = require('datetime')
local decimal = require('decimal')
local ffi = require('ffi')
local net = require('net.box')
local socket = require('socket')
local uuid = require('uuid')
local varbinary = require('varbinary')

local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- FIXME(gh-12546): For server.exec to be able to find the luatest module,
-- searchroot must be set in the test server. For the main thread, this is
-- done automatically by luatest itself, but due to the bug in Tarantool,
-- searchroot isn't propagated to application threads so we have to set it
-- manually. Remove this when the bug is fixed.
--
local function setsearchroot(server)
    local searchroot, app_threads = server:exec(function()
        return package.searchroot(), box.cfg.app_threads
    end)
    for i = 1, app_threads do
        server:eval('package.setsearchroot(...)', {searchroot},
                    {_thread_id = i})
    end
end

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {
            iproto_threads = 2,
            app_threads = 4,
        },
    })
    cg.server:start()
    cg.server:call('box.iproto.internal.enable_thread_requests')
    setsearchroot(cg.server)
    cg.server:exec(function()
        box.schema.user.passwd('admin', 'secret')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_thread_requests_disabled = function(cg)
    local conn = net.connect(cg.server.net_box_uri, {
        user = 'admin', password = 'secret',
    })
    local err = {type = 'ClientError', name = 'THREAD_REQUESTS_DISABLED'}
    t.assert_error_covers(err, conn.call, conn, 'tonumber', {'123'},
                          {_thread_id = 1})
    t.assert_error_covers(err, conn.eval, conn, 'return tonumber(...)', {'123'},
                          {_thread_id = 1})
    conn:call('box.iproto.internal.enable_thread_requests')
    t.assert_equals(conn:call('tonumber', {'123'},
                              {_thread_id = 1}), 123)
    t.assert_equals(conn:eval('return tonumber(...)', {'123'},
                              {_thread_id = 1}), 123)
    conn:close()
end

g.test_no_such_thread = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    conn:call('box.iproto.internal.enable_thread_requests')
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
    conn:call('box.iproto.internal.enable_thread_requests')
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
    local conn = net.connect(cg.server.net_box_uri, {
        user = 'admin', password = 'secret',
    })
    conn:call('box.iproto.internal.enable_thread_requests')
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
    conn:call('box.iproto.internal.enable_thread_requests')
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
    check(box.tuple.new({1, 2, 3}))
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
    -- Check formatted tuples.
    local format1 = box.tuple.format.new({
        {'a', 'unsigned'}, {'b', 'unsigned'}, {'c', 'unsigned'}})
    local format2 = box.tuple.format.new({
        {'x', 'string'}, {'y', 'string'},
    })
    local tuple1 = box.tuple.new({10, 20, 30}, {format = format1})
    local tuple2 = box.tuple.new({'foo', 'bar'}, {format = format2})
    local ret_tuple1, ret_tuple2 = conn:eval([[return ...]], {tuple1, tuple2},
                                             {_thread_id = 1})
    t.assert(box.tuple.is(ret_tuple1))
    t.assert(box.tuple.is(ret_tuple2))
    t.assert_equals(ret_tuple1:tomap(), tuple1:tomap())
    t.assert_equals(ret_tuple2:tomap(), tuple2:tomap())
    conn:close()
end

g.test_call_ret_tuple_extension_unset = function(cg)
    t.tarantool.skip_if_not_debug()
    box.error.injection.set('ERRINJ_NETBOX_FLIP_FEATURE',
                            box.iproto.feature.call_ret_tuple_extension)
    local conn = net.connect(cg.server.net_box_uri)
    conn:call('box.iproto.internal.enable_thread_requests')
    local tuple = conn:eval([[
        local format = box.tuple.format.new({
            {'a', 'unsigned'}, {'b', 'unsigned'},
        })
        return box.tuple.new({10, 20}, {format = format})
    ]], {}, {_thread_id = 1})
    t.assert_type(tuple, 'table')
    t.assert_equals(tuple, {10, 20})
    conn:close()
end

g.after_test('test_call_ret_tuple_extension_unset', function()
    box.error.injection.set('ERRINJ_NETBOX_FLIP_FEATURE', -1)
end)

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
    check_module_func('fiber', 'cond')
    check_module_func('fiber', 'channel')
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
    check_module('fio')
    check_module('socket')
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
    check_module('msgpackffi')
    check_module('yaml')
    check_module('json')
    check_module('key_def')
    check_module('merger')
    check_module('http.client')
    check_module('checks')
    check_module('metrics')
    t.assert_covers(eval([[
        local e = box.error.new({type = 'MyError', name = 'FooBar'})
        return e:unpack()
    ]]), {type = 'MyError', name = 'FooBar'})
    t.assert_items_equals(eval([[
        local keys = {}
        for k in pairs(box.iproto) do
            table.insert(keys, k)
        end
        return keys
    ]]), {
        'GREETING_PROTOCOL_LEN_MAX',
        'GREETING_SALT_LEN_MAX',
        'GREETING_SIZE',
        'ballot_key',
        'decode_greeting',
        'decode_packet',
        'encode_greeting',
        'encode_packet',
        'export',
        'feature',
        'flag',
        'internal',
        'key',
        'metadata_key',
        'protocol_features',
        'protocol_version',
        'raft_key',
        'type',
    })
    t.assert_items_equals(eval([[
        local keys = {}
        for k in pairs(box.iproto.internal) do
            table.insert(keys, k)
        end
        return keys
    ]]), {
        'register_func',
    })
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

g.test_buffer = function(cg)
    t.assert_equals(cg.server:eval([[
        local ffi = require('ffi')
        local buffer = require('buffer')
        local ibuf = buffer.internal.cord_ibuf_take()
        local data = ibuf:alloc(5)
        ffi.copy(data, ffi.cast('const char *', 'abcdefghi'), ibuf:size())
        local str = ffi.string(ibuf.rpos, ibuf:size())
        return str
    ]], {}, {_thread_id = 1}), 'abcde')
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
    ]], {}, {_thread_id = 1}), {cpu = {['1/sched'] = {time = 0}}})
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

g.test_fio = function(cg)
    cg.server:eval([[
        local fio = require('fio')
        fio.mkdir('test_fio')
        local fh = fio.open('test_fio/file', {'O_CREAT', 'O_WRONLY'})
        fh:write('foobar')
        fh:close()
        fh = fio.open('test_fio/file', {'O_RDONLY'})
        assert(fh:read() == 'foobar')
        fh:close()
        fio.unlink('test_fio/file')
        fio.rmdir('test_fio')
    ]], {}, {_thread_id = 1})
end

g.test_socket = function(cg)
    local addr = cg.server:eval([[
        local socket = require('socket')
        local server = socket.tcp_server('127.0.0.1', 0, {
            handler = function(s)
                s:write('hello')
                s:close()
            end,
        })
        rawset(_G, 'test_server', server)
        return server:name()
    ]], {}, {_thread_id = 1})
    local s = socket.tcp_connect(addr.host, addr.port)
    t.assert_is_not(s, nil)
    t.assert_equals(s:read(10, 0.1), 'hello')
    s:close()
    cg.server:eval([[
        local server = rawget(_G, 'test_server')
        rawset(_G, 'test_server', nil)
        server:close()
    ]], {}, {_thread_id = 1})
end

g.test_tuple = function(cg)
    local function eval(expr, args)
        return cg.server:eval(expr, args or {}, {_thread_id = 1})
    end
    -- Creating and updating a tuple.
    t.assert_equals(eval([[
        local tuple = box.tuple.new({1, 1, 1})
        return tuple:update({{'+', 2, 1}, {'+', 3, 2}})
    ]]), {1, 2, 3})
    -- Addressing tuple fields by name.
    t.assert_equals(eval([[
        local format = box.tuple.format.new({
            {'x', 'unsigned'}, {'y', 'string'},
        })
        local tuple = box.tuple.new({1, 'foo'}, {format = format})
        return tuple:update({
            {'=', 'x', tuple.x * 1000},
            {':', 'y', -1, 0, 'bar'},
        })
    ]]), {1000, 'foobar'})
    -- Validating a tuple against a format.
    t.assert_error_covers({
        type = 'ClientError',
        name = 'FIELD_TYPE',
        expected = 'unsigned',
        actual = 'integer',
    }, eval, [[
        local format = box.tuple.format.new({
            {'a', 'unsigned'}, {'b', 'string'},
        })
        return box.tuple.new({-1, 'foo'}, {format = format})
    ]])
    -- Check errors raised on an attempt to refer to a global schema object
    -- while creating a tuple format.
    for _, v in ipairs({
        {
            {{'x', 'string', collation = 'unicode'}},
            "format[1]: collation was not found by name 'unicode'",
        },
        {
            {{'x', 'string', default_func = 'test'}},
            "format[1]: field default function was not found by name 'test'",
        },
        {
            {{'x', 'string', constraint = 'test'}},
            "format[1]: constraint function was not found by name 'test'",
        },
        {
            {{'x', 'string', foreign_key = {space = 'test', field = 'y'}}},
            "format[1]: foreign key: space test was not found",
        },
    }) do
        local format, err = unpack(v)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = err,
        }, eval, [[box.tuple.format.new(...)]], {format})
    end
end

g.test_key_def = function(cg)
    local function eval(expr, args)
        return cg.server:eval(expr, args or {}, {_thread_id = 1})
    end
    t.assert_equals(eval([[
        local key_def_lib = require('key_def')
        local key_def = key_def_lib.new({
            {fieldno = 1, type = 'string', collation = 'unicode_ci'},
            {fieldno = 3, type = 'unsigned'},
        })
        return key_def:compare(
            box.tuple.new({'FOO', 10, 20}),
            box.tuple.new({'foo', 10, 30})
        )
    ]]), -1)
    t.assert_equals(eval([[
        local merger_lib = require('merger')
        local key_def_lib = require('key_def')
        local key_def = key_def_lib.new({
            {fieldno = 1, type = 'unsigned'},
            {fieldno = 2, type = 'unsigned'},
        })
        local merger = merger_lib.new(key_def, {
            merger_lib.new_source_fromtable({
                {1, 10}, {1, 30}, {1, 50},
                {2, 10}, {2, 30}, {2, 50},
            }),
            merger_lib.new_source_fromtable({
                {1, 20}, {1, 40}, {2, 20}, {2, 40},
            }),
        })
        return merger:select()
    ]]), {
        {1, 10}, {1, 20}, {1, 30}, {1, 40}, {1, 50},
        {2, 10}, {2, 20}, {2, 30}, {2, 40}, {2, 50},
    })
end

g.test_net_box = function(cg)
    cg.server:eval([[
        local conn = require('net.box').connect(...)
        conn:eval("box.schema.space.create('test')")
        conn:eval("box.space.test:create_index('primary')")
        conn:reload_schema()
        conn.space.test:insert({1, 'a'})
        conn.space.test:insert({2, 'b'})
        conn.space.test:insert({3, 'c'})
        conn:close()
    ]], {cg.server.net_box_uri}, {_thread_id = 1})
    t.assert_equals(cg.server:eval([[
        local conn = require('net.box').connect(...)
        local ret = conn.space.test:select({}, {iterator = 'le'})
        conn:close()
        return ret
    ]], {cg.server.net_box_uri}, {_thread_id = 2}), {
        {3, 'c'}, {2, 'b'}, {1, 'a'},
    })
    t.assert(cg.server:eval([[
        return require('net.box').self == nil
    ]], {cg.server.net_box_uri}, {_thread_id = 3}))
end

g.after_test('test_net_box', function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_net_replicaset = function()
    local config = cbuilder:new()
        :set_global_option('credentials.users.storage.roles', {'super'})
        :set_global_option('credentials.users.storage.password', 'secret')
        :set_global_option('iproto.advertise.sharding.login', 'storage')
        :use_replicaset('storage')
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'storage_a')
        :add_instance('storage_a', {})
        :add_instance('storage_b', {})
        :use_replicaset('router')
        :add_instance('router', {})
        :set_instance_option('router', 'threads.groups', {
            {name = 'test', size = 1},
        })
        :config()
    local cluster = cluster:new(config)
    cluster:start()
    cluster.router:call('box.iproto.internal.enable_thread_requests')
    setsearchroot(cluster.router)
    local connect_cfg = cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        return net_replicaset.get_connect_cfg('storage')
    end)
    cluster.router:exec(function(connect_cfg)
        local net_replicaset = require('internal.net.replicaset')
        local err = 'config is available only in main thread'
        t.assert_error_msg_equals(err, net_replicaset.get_connect_cfg,
                                  'storage')
        t.assert_error_msg_equals(err, net_replicaset.connect,
                                  'storage')
        local conn = net_replicaset.connect(connect_cfg)
        local info = conn:call_leader('box.info')
        t.assert_covers(info, {ro = false, name = 'storage_a'})
        conn:close()
    end, {connect_cfg}, {_thread_id = 1})
end

g.test_luatest_exec = function(cg)
    cg.server:exec(function()
        t.assert(true)
    end, {}, {_thread_id = 1})
end

g.test_checks = function(cg)
    cg.server:exec(function()
        local checks = require('checks')
        local function test_func(arg1, arg2)
            checks('string', 'tuple')
            return {arg1, arg2}
        end
        t.assert_error_msg_contains(
            'bad argument #1', test_func, 10, 20)
        t.assert_error_msg_contains(
            'bad argument #2', test_func, 'foo', 20)
        t.assert_equals(test_func('foo', box.tuple.new({1, 2, 3})),
                        {'foo', box.tuple.new({1, 2, 3})})
    end, {}, {_thread_id = 1})
end

g.test_metrics = function(cg)
    cg.server:exec(function()
        local metrics = require('metrics')
        metrics.gauge('test_metric'):set(123, {test_label = 'foo'})
        t.assert_covers(metrics.collect(), {
            {
                metric_name = 'test_metric',
                label_pairs = {test_label = 'foo'},
                value = 123,
            },
        })
    end, {}, {_thread_id = 1})
end
