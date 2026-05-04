local errno = require('errno')
local net = require('net.box')
local socket = require('socket')
local utils = require('internal.utils')

-- This module.
local threads = {}

-- Functions used internally.
box.internal.threads = {}

-- Configuration of the threads subsystem copied from the global config.
local threads_cfg

-- Name of the group this thread belongs to.
local this_group_name

-- Identifier of this thread.
local this_thread_id

-- Connection used to call threads.
local threads_conn

-- Map: name -> thread group object.
local thread_groups = {}

-- Map: name -> function exported with threads.export().
local exported_functions = {}

--
-- Creates an IPROTO connection to the local instance over a socket pair.
-- Returns the socket file descriptor of the other end, which can be passed
-- to net.box.from_fd().
--
local function make_conn_fd()
    local s1, s2 = socket.socketpair('AF_UNIX', 'SOCK_STREAM', 0)
    if s1 == nil then
        error('Failed to create socket pair: ' .. errno.strerror())
    end
    local fd1, fd2 = s1:fd(), s2:fd()
    s1:detach()
    s2:detach()
    box.session.new({fd = fd1, user = 'admin'})
    return fd2
end

--
-- Thread group class metatable.
--
-- This is a helper class that is used for dispatching function calls to
-- the threads of a thread group.
--
local thread_group_methods = {}
local thread_group_mt = {__index = thread_group_methods}

--
-- Creates a thread group that spans the specified range of thread ids.
-- The new thread group is registered under the given name.
--
local function create_thread_group(group_name, first_thread_id, last_thread_id)
    assert(thread_groups[group_name] == nil)
    assert(last_thread_id >= first_thread_id)
    thread_groups[group_name] = setmetatable({
        first_thread_id = first_thread_id,
        last_thread_id = last_thread_id,
    }, thread_group_mt)
end

--
-- Returns a thread group with the given name. Raises an error if not found.
--
local function find_thread_group(group_name)
    local group = thread_groups[group_name]
    if group == nil then
        box.error(box.error.NO_SUCH_THREAD_GROUP, group_name, 3)
    end
    return group
end

--
-- Initializes thread groups from the config.
--
-- Note that it always creates the 'tx' group with thread 0 for calling
-- the main thread.
--
local function init_thread_groups(cfg)
    create_thread_group('tx', 0, 0)
    local thread_id = 1
    for _, group_cfg in ipairs(cfg.groups) do
        local first_thread_id = thread_id
        local last_thread_id = thread_id + group_cfg.size - 1
        create_thread_group(group_cfg.name, first_thread_id, last_thread_id)
        thread_id = last_thread_id + 1
    end
end

--
-- Dispatches a callback to this thread group.
--
-- The callback is passed the target thread id as the second argument
-- (the first argument is args). It's supposed to return a net.box
-- future object.
--
-- Available options:
-- * target := all | any | N: determines whether the callback is invoked for
--   all threads of the group, just one thread picked randomly, or the thread
--   with the given id N, 1 <= N <= thread group size.
--
-- On success returns an array of future results, one for each target thread.
-- If any of the returned futures fails with an error, re-throws the last
-- error.
--
function thread_group_methods:_dispatch(cb, args, opts)
    local first_thread_id, last_thread_id
    if opts.target == nil or opts.target == 'all' then
        first_thread_id = self.first_thread_id
        last_thread_id = self.last_thread_id
    elseif opts.target == 'any' then
        local thread_id = math.random(self.first_thread_id, self.last_thread_id)
        first_thread_id, last_thread_id = thread_id, thread_id
    else
        local thread_id = self.first_thread_id + opts.target - 1
        if thread_id < self.first_thread_id or
                thread_id > self.last_thread_id then
            box.error(box.error.NO_SUCH_THREAD, opts.target, 2)
        end
        first_thread_id, last_thread_id = thread_id, thread_id
    end
    local futures = {}
    for thread_id = first_thread_id, last_thread_id do
        table.insert(futures, cb(args, thread_id))
    end
    local last_err
    local results = {}
    for i, fut in ipairs(futures) do
        local ret, err = fut:wait_result()
        if err == nil then
            results[i] = ret
        else
            last_err = err
        end
    end
    if last_err ~= nil then
        box.error(last_err, 2)
    end
    return results
end

--
-- Callback for thread_group:init().
--
local function thread_init_cb(args, thread_id)
    assert(threads_conn ~= nil)
    local cfg, group_name = unpack(args)
    return threads_conn:call('box.internal.threads.init',
                             {cfg, group_name, thread_id, make_conn_fd()},
                             {_thread_id = thread_id, is_async = true})
end

--
-- Initializes the subsystem in all threads of this group.
--
function thread_group_methods:init(cfg, group_name)
    assert(threads_conn ~= nil)
    return self:_dispatch(thread_init_cb, {cfg, group_name}, {target = 'all'})
end

--
-- Callback for thread_group:reload_priv().
--
local function thread_reload_priv_cb(args, thread_id)
    assert(threads_conn ~= nil)
    return threads_conn:call('box.internal.threads.reload_priv', args,
                             {_thread_id = thread_id, is_async = true})
end

--
-- Reloads privileges in all threads of this group.
--
function thread_group_methods:reload_priv(priv)
    assert(threads_conn ~= nil)
    return self:_dispatch(thread_reload_priv_cb, {priv}, {target = 'all'})
end

--
-- Callback for thread_group:call().
--
local function thread_call_cb(args, thread_id)
    assert(threads_conn ~= nil)
    return threads_conn:call('box.internal.threads.call', args,
                             {_thread_id = thread_id, is_async = true})
end

--
-- Calls a function exported with threads.export() on this thread group.
--
-- See thread_group:_dispatch() for the description of available options.
--
function thread_group_methods:call(func_name, args, opts)
    return self:_dispatch(thread_call_cb, {func_name, args}, opts)
end

--
-- Callback for thread_group:eval().
--
local function thread_eval_cb(args, thread_id)
    assert(threads_conn ~= nil)
    return threads_conn:eval(args[1], args[2],
                             {_thread_id = thread_id, is_async = true})
end

--
-- Executes a Lua expression on this thread group.
--
-- See thread_group:_dispatch() for the description of available options.
--
function thread_group_methods:eval(expr, args, opts)
    return self:_dispatch(thread_eval_cb, {expr, args}, opts)
end

--
-- Initializes the subsystem in the current thread.
--
function box.internal.threads.init(cfg, group_name, thread_id, conn_fd)
    assert(threads_cfg == nil)
    threads_cfg = cfg
    this_group_name = group_name
    this_thread_id = thread_id
    threads_conn = net.from_fd(conn_fd, {fetch_schema = false})
    init_thread_groups(cfg)
end

--
-- Reloads privileges in the current thread.
--
function box.internal.threads.reload_priv(priv)
    box.internal.lua_call_runtime_priv_reset()
    for user_name, funcs in pairs(priv) do
        for func_name, _ in pairs(funcs) do
            box.internal.lua_call_runtime_priv_grant(user_name, func_name)
        end
    end
end

--
-- Configures the threads subsystem. Called once from the main thread.
-- If called without arguments, returns the current configuration.
--
function box.internal.threads.cfg(cfg)
    if cfg == nil then
        return threads_cfg
    end
    assert(threads_cfg == nil)
    box.internal.threads.init(cfg, 'tx', 0, make_conn_fd())
    -- Initialize the subsystem in threads.
    for group_name, group in pairs(thread_groups) do
        if group_name ~= this_group_name then
            group:init(cfg, group_name)
        end
    end
end

--
-- Configures runtime privileges in application threads. Note that the main
-- thread is excluded (it's supposed to be configured before box.cfg).
--
function box.internal.threads.cfg_priv(priv)
    for group_name, group in pairs(thread_groups) do
        if group_name ~= 'tx' then
            group:reload_priv(priv)
        end
    end
end

--
-- Calls a function exported with threads.export().
--
function box.internal.threads.call(func_name, args)
    local func = exported_functions[func_name]
    if func == nil then
        box.error(box.error.NO_SUCH_FUNCTION, func_name)
    end
    return func(unpack(args))
end

--
-- Exports a function so that it can be executed with threads.call().
--
function threads.export(func_name, func)
    utils.check_param(func_name, 'function name', 'string', 2)
    utils.check_param(func, 'function object', 'function', 2)
    if exported_functions[func_name] ~= nil then
        box.error(box.error.FUNCTION_EXISTS, func_name, 2)
    end
    exported_functions[func_name] = func
end

--
-- Raises an error if the threads module hasn't been configured.
--
local function check_configured()
    if threads_cfg == nil then
        box.error(box.error.THREADS_NOT_CONFIGURED, 3)
    end
end

-- call/eval options template used with utils.check_param_table().
local CALL_EVAL_OPTS = {
    target = 'string, number',
}

--
-- Raises an error if an option passed to call/eval is unknown or has
-- an invalid type/value.
--
local function check_call_eval_opts(opts)
    utils.check_param_table(opts, CALL_EVAL_OPTS, 3)
    if type(opts.target) == 'string' and
            opts.target ~= 'all' and opts.target ~= 'any' then
        box.error(box.error.ILLEGAL_PARAMS,
                  "unexpected value for option parameter 'target': " ..
                  "got '" .. opts.target .. "', " ..
                  "expected 'any', 'all', or a number", 3)
    end
end

--
-- Calls a function on a thread group. The function must be exported with
-- threads.export() on all target threads.
--
-- On success returns an array of results, one per each target thread.
-- If the function fails on any target thread, the last error is re-thrown.
--
-- See thread_group:_dispatch() for the description of available options.
--
function threads.call(group_name, func_name, args, opts)
    args = args or {}
    opts = opts or {}
    check_configured()
    utils.check_param(group_name, 'group name', 'string', 2)
    utils.check_param(func_name, 'function name', 'string', 2)
    utils.check_param(args, 'function arguments', 'table', 2)
    check_call_eval_opts(opts)
    return find_thread_group(group_name):call(func_name, args, opts)
end

--
-- Executes a Lua expression on a thread group.
--
-- On success returns an array of results, one per each target thread.
-- If the function fails on any target thread, the last error is re-thrown.
--
-- See thread_group:_dispatch() for the description of available options.
--
function threads.eval(group_name, expr, args, opts)
    args = args or {}
    opts = opts or {}
    check_configured()
    utils.check_param(group_name, 'group name', 'string', 2)
    utils.check_param(expr, 'expression', 'string', 2)
    utils.check_param(args, 'expression arguments', 'table', 2)
    check_call_eval_opts(opts)
    return find_thread_group(group_name):eval(expr, args, opts)
end

--
-- Returns information about configured thread groups.
--
function threads.info()
    check_configured()
    local info = {
        thread_id = this_thread_id,
        group_name = this_group_name,
        groups = {
            {name = 'tx', size = 1},
        },
    }
    for _, group_cfg in ipairs(threads_cfg.groups) do
        table.insert(info.groups, {
            name = group_cfg.name,
            size = group_cfg.size,
        })
    end
    return info
end

return threads
