local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')
local xlog = require('xlog')

local g = t.group()

local IPROTO_SPACE_ID = 16
local IPROTO_TUPLE = 33

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_test('test_bad_xlog', function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
        box.snapshot()
    end)
end)

g.test_bad_xlog = function(cg)
    t.tarantool.skip_if_not_debug()
    local info = cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('primary', {parts = {1, 'string'}})
        box.snapshot()
        local lsn = box.info.lsn
        local function test(errinj)
            box.error.injection.set(errinj, true)
            local ok = pcall(s.insert, s, {errinj})
            box.error.injection.set(errinj, false)
            t.assert(ok)
        end
        box.space.test:insert({'BEGIN'})
        test('ERRINJ_XLOG_WRITE_CORRUPTED_HEADER')
        test('ERRINJ_XLOG_WRITE_INVALID_HEADER')
        test('ERRINJ_XLOG_WRITE_CORRUPTED_BODY')
        test('ERRINJ_XLOG_WRITE_INVALID_BODY')
        test('ERRINJ_XLOG_WRITE_INVALID_KEY')
        test('ERRINJ_XLOG_WRITE_INVALID_VALUE')
        test('ERRINJ_XLOG_WRITE_UNKNOWN_KEY')
        test('ERRINJ_XLOG_WRITE_UNKNOWN_TYPE')
        box.space.test:insert({'END'})
        return {
            replica_id = box.info.id,
            lsn = lsn,
            space_id = box.space.test.id
        }
    end)
    local path = fio.pathjoin(cg.server.workdir,
                              string.format('%020d.xlog', info.lsn))
    local result = {}
    for _, row in xlog.pairs(path) do
        if type(row.HEADER) == 'table' then
            if type(row.HEADER.timestamp) == 'number' then
                row.HEADER.timestamp = '<timestamp>'
            end
        end
        table.insert(result, row)
    end
    t.assert_equals(result, {
        {
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 1,
                timestamp = '<timestamp>',
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'BEGIN'},
            },
        },
        -- Row with a corrupted header is skipped.
        -- Row with an invalid header is skipped.
        -- Row with a corrupted body is skipped.
        -- Row with an invalid body is skipped.
        {
            -- Invalid keys are ignored.
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 6,
                timestamp = '<timestamp>',
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'ERRINJ_XLOG_WRITE_INVALID_KEY'},
            },
        },
        {
            -- Invalid values are dumped as is.
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 7,
                timestamp = '<timestamp>',
                key = 1,
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'ERRINJ_XLOG_WRITE_INVALID_VALUE'},
                key = 2,
            },
        },
        {
            -- Unknown keys are dumped as is.
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 8,
                timestamp = '<timestamp>',
                [666] = 1,
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'ERRINJ_XLOG_WRITE_UNKNOWN_KEY'},
                [666] = 2,
            },
        },
        {
            -- Unknown type is dumped as is.
            HEADER = {
                type = 777,
                replica_id = info.replica_id,
                lsn = info.lsn + 9,
                timestamp = '<timestamp>',
            },
            BODY = {
                [IPROTO_SPACE_ID] = info.space_id,
                [IPROTO_TUPLE] = {'ERRINJ_XLOG_WRITE_UNKNOWN_TYPE'},
            },
        },
        {
            HEADER = {
                type = 'INSERT',
                replica_id = info.replica_id,
                lsn = info.lsn + 10,
                timestamp = '<timestamp>',
            },
            BODY = {
                space_id = info.space_id,
                tuple = {'END'},
            },
        },
    })
end
