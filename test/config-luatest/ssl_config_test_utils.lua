-- Common utils. This code may be executed in servers context.

local checks = require('checks')
local uri = require('uri')
local fio = require('fio')
local t = require('luatest')
local helpers = require('luatest.helpers')
local tarantool = require('tarantool')

local M = {}

-- Misc helpers.

function M.tarantool_build_ndebug()
    return string.find(tarantool.build.flags, '-DNDEBUG') ~= nil
end

-- Beware to pass msgpack-decoded tables
-- when msgpack.cfg.decode_save_metatables = true.
-- Decoder tables will be modified.
function M.set_serialize_mapping(tbl)
    checks('table')
    local mt = getmetatable(tbl) or {}
    mt.__serialize = 'mapping'
    setmetatable(tbl, mt)
    for _, e in pairs(tbl) do
        if type(e) == 'table' then M.set_serialize_mapping(e) end
    end
end

function M.replace_mt_with_serialize_mapping(tbl)
    checks('table')
    setmetatable(tbl, {__serialize = 'mapping'})
    for _, e in pairs(tbl) do
        if type(e) == 'table' then M.replace_mt_with_serialize_mapping(e) end
    end
end

function M.get_caller_loc(level)
    checks('number')
    local frame = debug.getinfo(2 + (level or 0), "Sl")
    if type(frame) == 'table' then
        local line = frame.currentline or 0
        local file = frame.short_src or frame.src or '?'
        file = fio.basename(file)
        return string.format('%s:%d', file, line)
    end
    return '?:?'
end

-- Workaround with uri.format() and config:instance_uri() gap.
function M.uri_format_config_instance_uri(res)
    checks('table')
    local t = table.deepcopy(res)
    t.path = t.uri
    t.uri = nil
    return uri.format(t, true)
end

-- Workaround with net.box.connect() and config:instance_uri() gap.
function M.netbox_connect_args_instance_uri(res)
    checks('table')
    assert(type(res.uri) == 'string')
    local uri = {uri = res.uri, params = res.params}
    local opts = table.deepcopy(res)
    opts.uri = nil
    opts.params = nil
    opts.user = opts.login
    opts.login = nil
    return uri, opts
end

-- Query (string) component is obsolete. It isn't used
-- in uri.format() and the order of parameters in query string
-- is undefined. The last is important for tests.
function M.uri_parse_drop_query(res)
    checks('table')
    res.query = nil
    return res
end

-- Query parameters order may differs, so we need to parse URIs
-- and drop the query string component also.
function M.uri_equal(a, b)
    checks('string', 'string')
    local pa = M.uri_parse_drop_query(uri.parse(a))
    local pb = M.uri_parse_drop_query(uri.parse(b))
    return table.equals(pa, pb)
end

-- Checker helpers.

M.conn_checkers = {}

function M.conn_checkers.assert_conn_active(conn)
    checks('table')
    t.assert_equals(conn.state, 'active')
    t.assert_equals(conn:eval('return true'), true)
end

function M.conn_checkers.assert_conn_error(conn)
    checks('table')
    t.assert_equals(conn.state, 'error')
end

function M.conn_checkers.assert_conn_reset(conn)
    checks('table')
    helpers.retrying({timeout = 5}, function()
        if conn.state == 'active' then
            conn:eval('return true')
        end
        M.conn_checkers.assert_conn_error(conn)
    end)
end

function M.assert_conn_reset_if_enabled(conn, enabled)
    checks('table', 'boolean')
    if not enabled then
        M.conn_checkers.assert_conn_active(conn)
        return
    end

    M.conn_checkers.assert_conn_reset(conn)
end

return M
